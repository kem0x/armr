#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "aarch64_guest_session.h"

#include "elf64_modules.h"
#include "guest_heap.h"
#include "guest_thread_stack.h"
#include "import_registry.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <limits>
#include <mutex>
#include <optional>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "dynarmic/interface/A64/a64.h"
#include "dynarmic/interface/A64/config.h"
#include "dynarmic/interface/exclusive_monitor.h"

namespace isomr {

// Global guest-instruction counter incremented by every session's AddTicks.
// Used for coarse instruction-throughput diagnostics.
std::atomic<std::uint64_t> g_armr_instructions_executed{0};

struct Aarch64SvcImportTelemetryAccumulator::Storage {
    std::vector<Aarch64SvcImportTelemetry> entries;
    std::size_t active_count = 0;
};

bool Aarch64SvcImportTelemetryAccumulator::Prepare(
    std::span<const std::string_view> import_names) noexcept {
    try {
        auto storage = std::make_shared<Storage>();
        storage->entries.resize(import_names.size());
        for (std::size_t index = 0; index < import_names.size(); ++index)
            storage->entries[index].name = std::string(import_names[index]);
        storage_ = std::move(storage);
        return true;
    } catch (...) {
        storage_.reset();
        return false;
    }
}

void Aarch64SvcImportTelemetryAccumulator::Accumulate(
    std::uint32_t immediate, std::uint64_t wall_time_ns) noexcept {
    if (!storage_ || immediate == 0 || immediate > storage_->entries.size())
        return;
    Aarch64SvcImportTelemetry& entry = storage_->entries[immediate - 1];
    if (entry.invocations == 0) ++storage_->active_count;
    ++entry.invocations;
    entry.wall_time_ns += wall_time_ns;
    entry.max_wall_time_ns = std::max(entry.max_wall_time_ns, wall_time_ns);
    if (wall_time_ns > 1'000'000ULL) {
        ++entry.over_1ms_invocations;
        entry.over_1ms_wall_time_ns += wall_time_ns;
    }
}

void Aarch64SvcImportTelemetryAccumulator::
    AccumulateSuccessfulNanosleepRequest(
        std::uint32_t immediate, std::uint64_t requested_ns) noexcept {
    AccumulateSuccessfulNanosleepRequests(
        immediate, 1, requested_ns, requested_ns);
}

void Aarch64SvcImportTelemetryAccumulator::
    AccumulateSuccessfulNanosleepRequests(
        std::uint32_t immediate, std::uint64_t request_count,
        std::uint64_t requested_total_ns,
        std::uint64_t requested_max_ns) noexcept {
    if (!storage_ || immediate == 0 || immediate > storage_->entries.size())
        return;
    Aarch64SvcImportTelemetry& entry = storage_->entries[immediate - 1];
    if (!entry.nanosleep_requested_accumulation_valid) return;
    constexpr std::uint64_t kMaximum =
        (std::numeric_limits<std::uint64_t>::max)();
    if (request_count >
            kMaximum - entry.successful_nanosleep_requests ||
        requested_total_ns >
            kMaximum - entry.nanosleep_requested_total_ns) {
        entry.nanosleep_requested_accumulation_valid = false;
        return;
    }
    entry.successful_nanosleep_requests += request_count;
    entry.nanosleep_requested_total_ns += requested_total_ns;
    entry.nanosleep_requested_max_ns =
        std::max(entry.nanosleep_requested_max_ns, requested_max_ns);
}

std::size_t Aarch64SvcImportTelemetryAccumulator::size() const noexcept {
    return storage_ ? storage_->active_count : 0;
}

const Aarch64SvcImportTelemetry&
Aarch64SvcImportTelemetryAccumulator::operator[](
    std::size_t active_index) const noexcept {
    static const Aarch64SvcImportTelemetry empty;
    if (!storage_) return empty;
    for (const Aarch64SvcImportTelemetry& entry : storage_->entries) {
        if (entry.invocations != 0 && active_index-- == 0) return entry;
    }
    return empty;
}

std::span<const Aarch64SvcImportTelemetry>
Aarch64SvcImportTelemetryAccumulator::entries() const noexcept {
    return storage_ ? std::span<const Aarch64SvcImportTelemetry>(
                          storage_->entries)
                    : std::span<const Aarch64SvcImportTelemetry>();
}

void AccumulateAarch64SvcImportTelemetry(
    Aarch64GuestCallTelemetry* telemetry, std::uint32_t immediate,
    std::uint64_t wall_time_ns) noexcept {
    if (telemetry == nullptr) return;
    telemetry->svc_imports.Accumulate(immediate, wall_time_ns);
}

void AccumulateAarch64GuestPcTelemetry(
    Aarch64GuestCallTelemetry* telemetry,
    std::uint64_t program_counter,
    std::uint64_t link_register,
    std::uint64_t parent_link_register,
    std::uint64_t register_0,
    std::uint64_t register_1,
    std::uint64_t register_2,
    std::uint64_t register_3,
    std::uint64_t register_8,
    std::uint64_t register_10,
    std::uint64_t register_11,
    std::uint64_t register_19,
    std::uint64_t register_22,
    std::uint64_t register_24) noexcept {
    if (telemetry == nullptr || program_counter == 0 ||
        !telemetry->guest_pc_histogram)
        return;
    ++telemetry->guest_pc_samples;
    Aarch64GuestPcTelemetry* minimum = nullptr;
    for (Aarch64GuestPcTelemetry& entry :
         *telemetry->guest_pc_histogram) {
        if (entry.samples != 0 && entry.program_counter == program_counter &&
            entry.link_register == link_register &&
            entry.parent_link_register == parent_link_register) {
            entry.register_0_min =
                std::min(entry.register_0_min, register_0);
            entry.register_0_max =
                std::max(entry.register_0_max, register_0);
            entry.register_1_min =
                std::min(entry.register_1_min, register_1);
            entry.register_1_max =
                std::max(entry.register_1_max, register_1);
            entry.register_2_min =
                std::min(entry.register_2_min, register_2);
            entry.register_2_max =
                std::max(entry.register_2_max, register_2);
            entry.register_3_min =
                std::min(entry.register_3_min, register_3);
            entry.register_3_max =
                std::max(entry.register_3_max, register_3);
            entry.register_8_min =
                std::min(entry.register_8_min, register_8);
            entry.register_8_max =
                std::max(entry.register_8_max, register_8);
            entry.register_10_min =
                std::min(entry.register_10_min, register_10);
            entry.register_10_max =
                std::max(entry.register_10_max, register_10);
            entry.register_11_min =
                std::min(entry.register_11_min, register_11);
            entry.register_11_max =
                std::max(entry.register_11_max, register_11);
            entry.register_19_min =
                std::min(entry.register_19_min, register_19);
            entry.register_19_max =
                std::max(entry.register_19_max, register_19);
            entry.register_22_min =
                std::min(entry.register_22_min, register_22);
            entry.register_22_max =
                std::max(entry.register_22_max, register_22);
            entry.register_24_min =
                std::min(entry.register_24_min, register_24);
            entry.register_24_max =
                std::max(entry.register_24_max, register_24);
            ++entry.samples;
            return;
        }
        if (entry.samples == 0) {
            entry.program_counter = program_counter;
            entry.link_register = link_register;
            entry.parent_link_register = parent_link_register;
            entry.register_0_min = entry.register_0_max = register_0;
            entry.register_1_min = entry.register_1_max = register_1;
            entry.register_2_min = entry.register_2_max = register_2;
            entry.register_3_min = entry.register_3_max = register_3;
            entry.register_8_min = entry.register_8_max = register_8;
            entry.register_10_min = entry.register_10_max = register_10;
            entry.register_11_min = entry.register_11_max = register_11;
            entry.register_19_min = entry.register_19_max = register_19;
            entry.register_22_min = entry.register_22_max = register_22;
            entry.register_24_min = entry.register_24_max = register_24;
            entry.samples = 1;
            return;
        }
        if (minimum == nullptr || entry.samples < minimum->samples)
            minimum = &entry;
    }
    const std::uint64_t prior_minimum = minimum->samples;
    *minimum = {};
    minimum->program_counter = program_counter;
    minimum->link_register = link_register;
    minimum->parent_link_register = parent_link_register;
    minimum->register_0_min = minimum->register_0_max = register_0;
    minimum->register_1_min = minimum->register_1_max = register_1;
    minimum->register_2_min = minimum->register_2_max = register_2;
    minimum->register_3_min = minimum->register_3_max = register_3;
    minimum->register_8_min = minimum->register_8_max = register_8;
    minimum->register_10_min = minimum->register_10_max = register_10;
    minimum->register_11_min = minimum->register_11_max = register_11;
    minimum->register_19_min = minimum->register_19_max = register_19;
    minimum->register_22_min = minimum->register_22_max = register_22;
    minimum->register_24_min = minimum->register_24_max = register_24;
    minimum->samples = prior_minimum + 1;
    minimum->error = prior_minimum;
}

namespace {

namespace A64 = Dynarmic::A64;

constexpr std::uint64_t kTerminalAddress = 0x7ffffffff000ULL;
constexpr std::uint32_t kTerminalBreak = 0xd4200000U;
constexpr std::uint64_t kAarch64FutexSyscall = 98;
constexpr std::uint64_t kFutexWaitPrivateOperation = 0x80;

Aarch64SvcCategory ClassifySvcCategory(
    std::optional<std::string_view> import_name,
    std::uint64_t x0, std::uint64_t x2) {
    if (!import_name) return Aarch64SvcCategory::kOther;
    const std::string_view name = *import_name;
    if (name == "pthread_cond_wait" ||
        name == "pthread_cond_timedwait")
        return Aarch64SvcCategory::kConditionWait;
    if (name == "pthread_mutex_lock" ||
        name == "pthread_rwlock_rdlock" ||
        name == "pthread_rwlock_wrlock")
        return Aarch64SvcCategory::kMutexRwlockBlocking;
    if (name == "syscall" && x0 == kAarch64FutexSyscall &&
        x2 == kFutexWaitPrivateOperation)
        return Aarch64SvcCategory::kFutexWait;
    if (name == "pthread_once" || name == "pthread_join")
        return Aarch64SvcCategory::kPthreadOnceJoin;
    if (name == "qsort" || name == "bsearch")
        return Aarch64SvcCategory::kQsortBsearch;
    if (name == "malloc" || name == "calloc" || name == "realloc" ||
        name == "free" || name == "posix_memalign" ||
        name == "aligned_alloc" || name == "memalign" ||
        name == "valloc" || name == "pvalloc" ||
        name == "malloc_usable_size")
        return Aarch64SvcCategory::kAllocationFree;
    if (name.starts_with("__isomr_jni_") ||
        name.starts_with("__isomr_jvm_") || name.starts_with("gl") ||
        name.starts_with("egl"))
        return Aarch64SvcCategory::kJniGles;
    return Aarch64SvcCategory::kOther;
}

void SetError(std::string* error, std::string message) {
    if (error != nullptr) *error = std::move(message);
}

std::string HexAddress(std::uint64_t address) {
    std::ostringstream text;
    text << "0x" << std::hex << address;
    return text.str();
}

const char* InstructionClass(std::uint32_t instruction) {
    if (instruction == 0xd503205fU) return "wait-for-event";
    if (instruction == 0xd503207fU) return "wait-for-interrupt";
    if ((instruction & 0x7c000000U) == 0x14000000U) {
        const std::uint32_t immediate = instruction & 0x03ffffffU;
        if (immediate == 0) return "branch-self";
        return (immediate & 0x02000000U) != 0
                   ? "branch-backward"
                   : "branch-forward";
    }
    if ((instruction & 0xff000010U) == 0x54000000U ||
        (instruction & 0x7e000000U) == 0x34000000U) {
        const std::uint32_t immediate = (instruction >> 5) & 0x7ffffU;
        if (immediate == 0) return "conditional-branch-self";
        return (immediate & 0x40000U) != 0
                   ? "conditional-branch-backward"
                   : "conditional-branch-forward";
    }
    if ((instruction & 0x7e000000U) == 0x36000000U) {
        const std::uint32_t immediate = (instruction >> 5) & 0x3fffU;
        if (immediate == 0) return "test-branch-self";
        return (immediate & 0x2000U) != 0
                   ? "test-branch-backward"
                   : "test-branch-forward";
    }
    if ((instruction & 0x3f000000U) == 0x08000000U)
        return (instruction & 0x00400000U) != 0
                   ? "exclusive-load"
                   : "exclusive-store";
    return "other";
}

const char* ExceptionName(A64::Exception exception) {
    switch (exception) {
        case A64::Exception::UnallocatedEncoding:
            return "unallocated-encoding";
        case A64::Exception::ReservedValue:
            return "reserved-value";
        case A64::Exception::UnpredictableInstruction:
            return "unpredictable-instruction";
        case A64::Exception::WaitForInterrupt:
            return "wait-for-interrupt";
        case A64::Exception::WaitForEvent:
            return "wait-for-event";
        case A64::Exception::SendEvent:
            return "send-event";
        case A64::Exception::SendEventLocal:
            return "send-event-local";
        case A64::Exception::Yield:
            return "yield";
        case A64::Exception::Breakpoint:
            return "breakpoint";
        case A64::Exception::NoExecuteFault:
            return "no-execute-fault";
    }
    return "unknown";
}

std::uint64_t FileTimeValue(const FILETIME& value) {
    ULARGE_INTEGER converted{};
    converted.LowPart = value.dwLowDateTime;
    converted.HighPart = value.dwHighDateTime;
    return converted.QuadPart;
}

bool ReadCurrentThreadCpuTime(std::uint64_t* result) {
    if (result == nullptr) return false;
    FILETIME creation{};
    FILETIME exit{};
    FILETIME kernel{};
    FILETIME user{};
    if (GetThreadTimes(GetCurrentThread(), &creation, &exit, &kernel,
                       &user) == FALSE)
        return false;
    const std::uint64_t kernel_ticks = FileTimeValue(kernel);
    const std::uint64_t user_ticks = FileTimeValue(user);
    if (kernel_ticks > UINT64_MAX - user_ticks) return false;
    *result = kernel_ticks + user_ticks;
    return true;
}

}  // namespace

class Aarch64ExclusiveMonitor::Impl {
public:
    explicit Impl(std::size_t processor_capacity)
        : monitor(processor_capacity), used(processor_capacity, false) {}

    std::optional<std::size_t> Acquire() {
        const std::lock_guard lock(mutex);
        for (std::size_t index = 0; index < used.size(); ++index) {
            if (used[index]) continue;
            used[index] = true;
            return index;
        }
        return std::nullopt;
    }

    void Release(std::size_t processor) {
        const std::lock_guard lock(mutex);
        if (processor >= used.size() || !used[processor]) return;
        monitor.ClearProcessor(processor);
        used[processor] = false;
    }

    Dynarmic::ExclusiveMonitor monitor;
    std::mutex mutex;
    std::vector<bool> used;
};

Aarch64ExclusiveMonitor::Aarch64ExclusiveMonitor(
    std::size_t processor_capacity)
    : impl_(processor_capacity == 0
                ? nullptr
                : std::make_unique<Impl>(processor_capacity)) {}
Aarch64ExclusiveMonitor::~Aarch64ExclusiveMonitor() = default;

Aarch64CallArgument Aarch64CallArgument::General64(std::uint64_t value) {
    return {Aarch64CallArgumentKind::kGeneral64, {value, 0}};
}

Aarch64CallArgument Aarch64CallArgument::Float32Bits(std::uint32_t value) {
    return {Aarch64CallArgumentKind::kFloat32, {value, 0}};
}

Aarch64CallArgument Aarch64CallArgument::Float64Bits(std::uint64_t value) {
    return {Aarch64CallArgumentKind::kFloat64, {value, 0}};
}

Aarch64CallArgument Aarch64CallArgument::Vector128Bits(
    std::array<std::uint64_t, 2> value) {
    return {Aarch64CallArgumentKind::kVector128, value};
}

class Aarch64GuestSession::Impl final : public A64::UserCallbacks {
public:
    class CallTelemetryScope {
    public:
        explicit CallTelemetryScope(Impl* owner)
            : owner_(owner), active_(owner_->BeginCallTelemetry()) {}
        ~CallTelemetryScope() {
            if (active_) owner_->FinishCallTelemetry(succeeded_);
        }
        void MarkSucceeded() { succeeded_ = true; }

    private:
        Impl* owner_ = nullptr;
        bool active_ = false;
        bool succeeded_ = false;
    };

    Impl(Aarch64ExclusiveMonitor* exclusive_monitor,
         Elf64ModuleSet* modules, const Aarch64ImportRegistry* imports,
         GuestHeap* heap, std::uint64_t stack, std::size_t stack_size,
         std::uint64_t thread_pointer, std::uint64_t instruction_budget,
         Aarch64MemoryCaptureConfig memory_capture,
         std::shared_ptr<WorkerExecutionTelemetry> execution_telemetry,
         Aarch64GuestProcessExitCancellation process_exit_cancellation)
        : exclusive_monitor_(exclusive_monitor), modules_(modules),
          imports_(imports), heap_(heap), stack_(stack),
          stack_size_(stack_size), thread_pointer_(thread_pointer),
          instruction_budget_(instruction_budget),
          memory_capture_config_(memory_capture),
          execution_telemetry_(std::move(execution_telemetry)),
          process_exit_cancellation_(
              std::move(process_exit_cancellation)) {}

    ~Impl() override {
        if (execution_telemetry_)
            execution_telemetry_->Publish(WorkerExecutionPhase::kDestroyed,
                                          false, 0);
        jit_.reset();
        if (exclusive_monitor_ != nullptr && processor_id_)
            exclusive_monitor_->impl_->Release(*processor_id_);
        if (heap_ != nullptr && stack_ != 0) {
            std::string ignored;
            (void)heap_->Release(stack_, &ignored);
        }
    }

    bool Initialize(std::string* error) {
        const bool memory_capture_valid =
            (memory_capture_config_.access ==
                 Aarch64MemoryCaptureAccess::kNone) ==
                (memory_capture_config_.watch_generation == 0) &&
            memory_capture_config_.occurrence != 0;
        const bool process_exit_cancellation_valid =
            static_cast<bool>(process_exit_cancellation_.requested) ==
            static_cast<bool>(process_exit_cancellation_.observed);
        if (modules_ == nullptr || imports_ == nullptr || heap_ == nullptr ||
            stack_ == 0 || stack_size_ < 4096 ||
            stack_size_ % 16 != 0 || instruction_budget_ == 0 ||
            stack_ > std::numeric_limits<std::uint64_t>::max() - stack_size_ ||
            (stack_ + stack_size_) % 16 != 0 ||
            heap_->ContainsAddress(kTerminalAddress) ||
            modules_->ReadCode(kTerminalAddress, &terminal_probe_,
                               sizeof(terminal_probe_)) ||
            (kTerminalAddress >= imports_->guest_base() &&
             kTerminalAddress <
                 imports_->guest_base() + imports_->code_size()) ||
            !memory_capture_valid || !process_exit_cancellation_valid) {
            SetError(error, "AArch64 guest session configuration is invalid");
            return false;
        }
        if (exclusive_monitor_ != nullptr) {
            if (exclusive_monitor_->impl_ == nullptr) {
                SetError(error,
                         "AArch64 exclusive monitor configuration is invalid");
                return false;
            }
            processor_id_ = exclusive_monitor_->impl_->Acquire();
            if (!processor_id_) {
                SetError(error,
                         "AArch64 exclusive monitor has no free processor slot");
                return false;
            }
        } else {
            owned_monitor_ =
                std::make_unique<Dynarmic::ExclusiveMonitor>(1);
            processor_id_ = 0;
        }
        forensic_memory_callbacks_ =
            heap_->heap_watch_diagnostics_configured() ||
            memory_capture_config_.access !=
                Aarch64MemoryCaptureAccess::kNone ||
            memory_capture_config_.breakpoint_address != 0;
        static std::once_flag fast_marker;
        static std::once_flag forensic_marker;
        std::call_once(forensic_memory_callbacks_ ? forensic_marker
                                                  : fast_marker,
                       [this]() {
                           std::printf(
                               "ISOMR_AARCH64_MEMORY_CALLBACK mode=%s\n",
                               forensic_memory_callbacks_ ? "forensic"
                                                          : "fast");
                       });
        A64::UserConfig config;
        config.callbacks = this;
        config.tpidr_el0 = &thread_pointer_;
        config.processor_id = *processor_id_;
        config.global_monitor =
            exclusive_monitor_ != nullptr
                ? &exclusive_monitor_->impl_->monitor
                : owned_monitor_.get();
        config.check_halt_on_memory_access =
            memory_capture_config_.access !=
                Aarch64MemoryCaptureAccess::kNone &&
            memory_capture_config_.watch_generation != 0;
        // Pick the smallest legal direct-table address space that covers the
        // whole heap arena [guest_base, guest_base + capacity). A high or
        // oversized arena cannot be covered and leaves the table disabled,
        // so all accesses keep the exact checked-callback behavior.
        direct_heap_table_used_ = false;
        static const bool no_direct_table = [] {
            wchar_t buf[8] = {};
            return GetEnvironmentVariableW(L"ARMR_NO_DIRECT_TABLE", buf,
                                           8) > 0;
        }();
        if (!forensic_memory_callbacks_ && !no_direct_table &&
            heap_ != nullptr) {
            const std::uint64_t arena_end =
                heap_->guest_base() + heap_->capacity();
            for (std::size_t bits = 32; bits <= 36; ++bits) {
                if ((std::uint64_t{1} << bits) >= arena_end) {
                    direct_heap_table_used_ =
                        heap_->BuildDirectPageTable(bits);
                    break;
                }
            }
        }
        if (direct_heap_table_used_) {
            config.page_table = heap_->direct_page_table_entries();
            config.page_table_address_space_bits =
                heap_->direct_page_table_address_space_bits();
            config.silently_mirror_page_table = false;
        }
        try {
            jit_ = std::make_unique<A64::Jit>(config);
        } catch (...) {
            SetError(error, "AArch64 guest JIT creation failed");
            return false;
        }
        return true;
    }

    bool Call(std::uint64_t function,
              const std::array<std::uint64_t, 8>& arguments,
              std::uint64_t* result, std::string* error) {
        if (result == nullptr) {
            SetError(error, "guest result is null");
            return false;
        }
        std::vector<Aarch64CallArgument> entry_arguments;
        entry_arguments.reserve(arguments.size());
        for (const std::uint64_t argument : arguments)
            entry_arguments.push_back(Aarch64CallArgument::General64(argument));
        Aarch64CallResult entry_result{};
        if (!CallEntry(function, entry_arguments, &entry_result, error))
            return false;
        *result = entry_result.x0;
        return true;
    }

    bool SetInstructionBudgetForNextCall(
        std::uint64_t instruction_budget, std::string* error) {
        if (instruction_budget == 0) {
            SetError(error, "guest instruction budget is zero");
            return false;
        }
        const std::lock_guard lock(call_mutex_);
        instruction_budget_ = instruction_budget;
        return true;
    }

    bool SetOwnerThreadInterSliceCallback(
        std::uint64_t slice_instruction_budget,
        Aarch64OwnerThreadInterSliceCallback callback,
        std::string* error,
        bool renew_total_budget_after_slice) {
        if (slice_instruction_budget == 0 || !callback) {
            SetError(error,
                     "guest inter-slice callback configuration is invalid");
            return false;
        }
        const std::lock_guard lock(call_mutex_);
        inter_slice_instruction_budget_ = slice_instruction_budget;
        inter_slice_callback_ = std::move(callback);
        inter_slice_owner_thread_ = std::this_thread::get_id();
        inter_slice_renews_total_budget_ = renew_total_budget_after_slice;
        return true;
    }

    bool SetOwnerThreadExecutionBreakpoint(
        std::uint64_t guest_address,
        Aarch64OwnerThreadExecutionBreakpointCallback callback,
        std::string* error) {
        if (guest_address == 0 || (guest_address & 3U) != 0 || !callback) {
            SetError(error,
                     "guest execution breakpoint configuration is invalid");
            return false;
        }
        const std::lock_guard lock(call_mutex_);
        std::uint32_t instruction = 0;
        if (jit_ == nullptr ||
            !ReadExecutableInstruction(guest_address, &instruction)) {
            SetError(error,
                     "guest execution breakpoint is not executable");
            return false;
        }
        for (const ExecutionBreakpoint& breakpoint : execution_breakpoints_)
            jit_->InvalidateCacheRange(breakpoint.address,
                                       sizeof(instruction));
        execution_breakpoints_.clear();
        execution_breakpoints_.push_back(
            ExecutionBreakpoint{guest_address, std::move(callback)});
        execution_breakpoint_owner_thread_ = std::this_thread::get_id();
        pending_execution_breakpoint_address_ = 0;
        jit_->InvalidateCacheRange(guest_address, sizeof(instruction));
        return true;
    }

    void ClearOwnerThreadExecutionBreakpoint() {
        const std::lock_guard lock(call_mutex_);
        if (jit_ != nullptr) {
            for (const ExecutionBreakpoint& breakpoint :
                 execution_breakpoints_) {
                jit_->InvalidateCacheRange(breakpoint.address,
                                           sizeof(std::uint32_t));
            }
        }
        execution_breakpoints_.clear();
        execution_breakpoint_owner_thread_ = {};
        pending_execution_breakpoint_address_ = 0;
    }

    bool AddOwnerThreadExecutionBreakpoint(
        std::uint64_t guest_address,
        Aarch64OwnerThreadExecutionBreakpointCallback callback,
        std::string* error) {
        if (guest_address == 0 || (guest_address & 3U) != 0 || !callback) {
            SetError(error,
                     "guest execution breakpoint configuration is invalid");
            return false;
        }
        const std::lock_guard lock(call_mutex_);
        std::uint32_t instruction = 0;
        if (jit_ == nullptr ||
            !ReadExecutableInstruction(guest_address, &instruction)) {
            SetError(error,
                     "guest execution breakpoint is not executable");
            return false;
        }
        const auto duplicate = std::find_if(
            execution_breakpoints_.begin(), execution_breakpoints_.end(),
            [guest_address](const ExecutionBreakpoint& breakpoint) {
                return breakpoint.address == guest_address;
            });
        if (duplicate != execution_breakpoints_.end()) {
            SetError(error, "guest execution breakpoint already exists");
            return false;
        }
        const std::thread::id owner = std::this_thread::get_id();
        if (!execution_breakpoints_.empty() &&
            execution_breakpoint_owner_thread_ != owner) {
            SetError(error,
                     "guest execution breakpoints belong to a different thread");
            return false;
        }
        execution_breakpoints_.push_back(
            ExecutionBreakpoint{guest_address, std::move(callback)});
        execution_breakpoint_owner_thread_ = owner;
        pending_execution_breakpoint_address_ = 0;
        jit_->InvalidateCacheRange(guest_address, sizeof(instruction));
        return true;
    }

    void RemoveOwnerThreadExecutionBreakpoint(std::uint64_t guest_address) {
        const std::lock_guard lock(call_mutex_);
        const auto breakpoint = std::find_if(
            execution_breakpoints_.begin(), execution_breakpoints_.end(),
            [guest_address](const ExecutionBreakpoint& candidate) {
                return candidate.address == guest_address;
            });
        if (breakpoint == execution_breakpoints_.end()) return;
        if (jit_ != nullptr)
            jit_->InvalidateCacheRange(breakpoint->address,
                                       sizeof(std::uint32_t));
        execution_breakpoints_.erase(breakpoint);
        if (pending_execution_breakpoint_address_ == guest_address)
            pending_execution_breakpoint_address_ = 0;
        if (execution_breakpoints_.empty())
            execution_breakpoint_owner_thread_ = {};
    }

    bool SetOwnerThreadInterSliceBudgetForNextSlice(
        std::uint64_t slice_instruction_budget, std::string* error) {
        if (!inter_slice_callback_active_ ||
            std::this_thread::get_id() != inter_slice_owner_thread_) {
            SetError(error,
                     "next inter-slice budget requires an active owner-thread "
                     "inter-slice callback");
            return false;
        }
        if (slice_instruction_budget == 0) {
            SetError(error, "next inter-slice budget is zero");
            return false;
        }
        next_inter_slice_instruction_budget_ = slice_instruction_budget;
        return true;
    }

    void ClearOwnerThreadInterSliceCallback() {
        const std::lock_guard lock(call_mutex_);
        inter_slice_callback_ = {};
        inter_slice_instruction_budget_ = 0;
        next_inter_slice_instruction_budget_ = 0;
        inter_slice_owner_thread_ = {};
        inter_slice_renews_total_budget_ = false;
    }

    bool CaptureGuestPcTelemetrySample(
        Aarch64GuestCallTelemetry* telemetry, std::string* error) const {
        if (!inter_slice_callback_active_ ||
            std::this_thread::get_id() != inter_slice_owner_thread_ ||
            jit_ == nullptr || telemetry == nullptr ||
            !telemetry->enabled ||
            telemetry->mode != Aarch64CallTelemetryMode::kFull ||
            !telemetry->guest_pc_histogram) {
            SetError(error,
                     "guest PC sample requires an active owner-thread "
                     "inter-slice callback and a full telemetry sink");
            return false;
        }
        std::uint64_t parent_link_register = 0;
        const std::uint64_t frame_pointer = jit_->GetRegister(29);
        if ((frame_pointer & 15U) == 0 && frame_pointer >= stack_ &&
            frame_pointer <= stack_ + stack_size_ - 16) {
            (void)heap_->Read(frame_pointer + sizeof(std::uint64_t),
                              &parent_link_register,
                              sizeof(parent_link_register));
        }
        AccumulateAarch64GuestPcTelemetry(
            telemetry, jit_->GetPC(), jit_->GetRegister(30),
            parent_link_register, jit_->GetRegister(0),
            jit_->GetRegister(1), jit_->GetRegister(2),
            jit_->GetRegister(3), jit_->GetRegister(8),
            jit_->GetRegister(10), jit_->GetRegister(11),
            jit_->GetRegister(19), jit_->GetRegister(22),
            jit_->GetRegister(24));
        return true;
    }

    void SetCallTelemetryEnabled(bool enabled) {
        SetCallTelemetryMode(enabled ? Aarch64CallTelemetryMode::kFull
                                     : Aarch64CallTelemetryMode::kOff);
    }

    void SetCallTelemetryMode(Aarch64CallTelemetryMode mode) {
        const std::lock_guard lock(call_mutex_);
        if (mode != Aarch64CallTelemetryMode::kOff &&
            mode != Aarch64CallTelemetryMode::kEnvelope &&
            mode != Aarch64CallTelemetryMode::kFull &&
            mode != Aarch64CallTelemetryMode::kSvcCategory)
            mode = Aarch64CallTelemetryMode::kOff;
        telemetry_mode_ = mode;
        if (mode == Aarch64CallTelemetryMode::kOff)
            last_call_telemetry_ = {};
    }

    Aarch64GuestCallTelemetry LastCallTelemetry() const {
        const std::lock_guard lock(call_mutex_);
        return last_call_telemetry_;
    }

    bool CallEntry(std::uint64_t function,
                   const std::vector<Aarch64CallArgument>& arguments,
                   Aarch64CallResult* result, std::string* error) {
        const std::lock_guard lock(call_mutex_);
        // Publish this thread's guest stack bounds so bionic's
        // pthread_getattr_np/pthread_attr_getstack report the real range for
        // the il2cpp Boehm GC's conservative stack scan.
        g_guest_thread_stack_base = stack_;
        g_guest_thread_stack_size = stack_size_;
        CallTelemetryScope telemetry_scope(this);
        struct ExecutionCallScope {
            std::shared_ptr<WorkerExecutionTelemetry> telemetry;
            A64::Jit* jit = nullptr;
            bool pc_valid = false;
            bool succeeded = false;
            ~ExecutionCallScope() {
                if (telemetry && !succeeded)
                    telemetry->Publish(WorkerExecutionPhase::kCallFailure,
                                       pc_valid, pc_valid ? jit->GetPC() : 0);
            }
        } execution_scope{execution_telemetry_, jit_.get()};
        std::uint32_t instruction = 0;
        if (inter_slice_callback_ &&
            inter_slice_owner_thread_ != std::this_thread::get_id()) {
            SetError(error,
                     "guest inter-slice callback belongs to a different thread");
            return false;
        }
        if (!execution_breakpoints_.empty() &&
            execution_breakpoint_owner_thread_ !=
                std::this_thread::get_id()) {
            SetError(
                error,
                "guest execution breakpoint belongs to a different thread");
            return false;
        }
        if (result == nullptr || jit_ == nullptr ||
            !modules_->ReadCode(function, &instruction, sizeof(instruction))) {
            SetError(error, "guest function is null or not executable");
            return false;
        }

        struct Spill {
            std::size_t offset;
            const Aarch64CallArgument* argument;
        };
        std::array<A64::Vector, 8> vector_registers{};
        std::array<std::uint64_t, 8> general_registers{};
        std::vector<Spill> spills;
        spills.reserve(arguments.size());
        std::size_t general_count = 0;
        std::size_t vector_count = 0;
        std::size_t stack_image_size = 0;
        const auto align_stack = [&](std::size_t alignment) {
            if (stack_image_size >
                std::numeric_limits<std::size_t>::max() - (alignment - 1))
                return false;
            stack_image_size =
                (stack_image_size + alignment - 1) & ~(alignment - 1);
            return true;
        };
        for (const Aarch64CallArgument& argument : arguments) {
            const bool general =
                argument.kind == Aarch64CallArgumentKind::kGeneral64;
            const bool vector =
                argument.kind == Aarch64CallArgumentKind::kFloat32 ||
                argument.kind == Aarch64CallArgumentKind::kFloat64 ||
                argument.kind == Aarch64CallArgumentKind::kVector128;
            if (!general && !vector) {
                SetError(error, "guest call argument kind is unsupported");
                return false;
            }
            if (general && general_count < general_registers.size()) {
                general_registers[general_count++] = argument.bits[0];
                continue;
            }
            if (vector && vector_count < vector_registers.size()) {
                A64::Vector value{};
                value[0] = argument.kind == Aarch64CallArgumentKind::kFloat32
                               ? argument.bits[0] & 0xffffffffULL
                               : argument.bits[0];
                if (argument.kind == Aarch64CallArgumentKind::kVector128)
                    value[1] = argument.bits[1];
                vector_registers[vector_count++] = value;
                continue;
            }
            const std::size_t alignment =
                argument.kind == Aarch64CallArgumentKind::kVector128 ? 16 : 8;
            const std::size_t size =
                argument.kind == Aarch64CallArgumentKind::kVector128 ? 16 : 8;
            if (!align_stack(alignment) ||
                stack_image_size > stack_size_ ||
                size > stack_size_ - stack_image_size) {
                SetError(error, "guest call stack image is oversized");
                return false;
            }
            spills.push_back({stack_image_size, &argument});
            stack_image_size += size;
            if (general) {
                ++general_count;
            } else {
                ++vector_count;
            }
        }
        if (!align_stack(16) || stack_image_size > stack_size_) {
            SetError(error, "guest call stack image is oversized");
            return false;
        }
        std::vector<std::uint8_t> stack_image(stack_image_size, 0);
        for (const Spill& spill : spills) {
            const Aarch64CallArgument& argument = *spill.argument;
            const std::size_t size =
                argument.kind == Aarch64CallArgumentKind::kFloat32
                    ? sizeof(std::uint32_t)
                    : argument.kind == Aarch64CallArgumentKind::kVector128
                          ? 2 * sizeof(std::uint64_t)
                          : sizeof(std::uint64_t);
            std::memcpy(stack_image.data() + spill.offset,
                        argument.bits.data(), size);
        }
        const std::uint64_t stack_top = stack_ + stack_size_;
        const std::uint64_t entry_stack = stack_top - stack_image_size;
        // Zero the first 64KB below the entry stack pointer: uninitialized
        // stack data causes native calls (like the atlas init loop) to read
        // garbage pointers and fault.
        {
            constexpr std::uint64_t kStackZeroBytes = 64U * 1024U;
            const std::uint64_t zero_start =
                entry_stack > kStackZeroBytes ? entry_stack - kStackZeroBytes
                                              : stack_;
            const std::uint64_t zero_size = entry_stack - zero_start;
            if (zero_size > 0) {
                std::vector<std::uint8_t> zeros(
                    static_cast<std::size_t>(zero_size), 0);
                WriteMemory(zero_start, zeros.data(),
                            static_cast<std::size_t>(zero_size));
            }
        }

        valid_ = true;
        stopped_ = false;
        failure_.clear();
        read_trace_count_ = 0;
        read_trace_next_ = 0;
        write_trace_count_ = 0;
        write_trace_next_ = 0;
        memory_access_capture_ = {};
        memory_capture_pending_ = false;
        pending_execution_breakpoint_address_ = 0;
        process_exit_cancellation_observed_ = false;
        next_inter_slice_instruction_budget_ = 0;
        total_ticks_remaining_ = instruction_budget_;
        slice_ticks_remaining_ =
            inter_slice_callback_
                ? std::min(total_ticks_remaining_,
                           inter_slice_instruction_budget_)
                : total_ticks_remaining_;
        for (std::size_t index = 0; index < 30; ++index)
            jit_->SetRegister(index, 0);
        for (std::size_t index = 0; index < 32; ++index)
            jit_->SetVector(index, {});
        for (std::size_t index = 0; index < general_registers.size(); ++index)
            jit_->SetRegister(index, general_registers[index]);
        for (std::size_t index = 0; index < vector_registers.size(); ++index)
            jit_->SetVector(index, vector_registers[index]);
        jit_->SetRegister(30, kTerminalAddress);
        if (!stack_image.empty() &&
            !heap_->Write(entry_stack, stack_image.data(), stack_image.size())) {
            SetError(error, "guest call stack staging failed");
            jit_->SetSP(stack_top);
            return false;
        }
        jit_->SetSP(entry_stack);
        jit_->SetPC(function);
        execution_scope.pc_valid = true;
        if (execution_telemetry_)
            execution_telemetry_->Publish(WorkerExecutionPhase::kCallEntry,
                                          true, jit_->GetPC());
        Dynarmic::HaltReason halt{};
        static const bool trace_call = [] {
            wchar_t flag[8] = {};
            return GetEnvironmentVariableW(L"ISOMR_CTR_TRACE", flag, 8) > 0;
        }();
        if (trace_call)
            std::fprintf(stderr, "CE enter fn=0x%llx\n",
                         (unsigned long long)function);
        for (;;) {
            if (ObserveProcessExitCancellation()) break;
            if (call_telemetry_active_)
                ++last_call_telemetry_.jit_run_invocations;
            if (execution_telemetry_)
                execution_telemetry_->Publish(
                    WorkerExecutionPhase::kBeforeRun, true, jit_->GetPC());
            if (trace_call)
                std::fprintf(stderr, "CE pre-run fn=0x%llx pc=0x%llx\n",
                             (unsigned long long)function,
                             (unsigned long long)jit_->GetPC());
            halt = jit_->Run();
            if (trace_call)
                std::fprintf(stderr,
                             "CE post-run fn=0x%llx pc=0x%llx halt=%d\n",
                             (unsigned long long)function,
                             (unsigned long long)jit_->GetPC(), (int)halt);
            if (execution_telemetry_)
                execution_telemetry_->Publish(
                    WorkerExecutionPhase::kAfterRun, true, jit_->GetPC());
            if (call_telemetry_active_)
                last_call_telemetry_.halt_reason =
                    static_cast<std::uint64_t>(halt);
            if (pending_execution_breakpoint_address_ != 0) {
                const std::uint64_t breakpoint_address =
                    pending_execution_breakpoint_address_;
                pending_execution_breakpoint_address_ = 0;
                const auto breakpoint = std::find_if(
                    execution_breakpoints_.begin(),
                    execution_breakpoints_.end(),
                    [breakpoint_address](const ExecutionBreakpoint& candidate) {
                        return candidate.address == breakpoint_address;
                    });
                if (breakpoint == execution_breakpoints_.end()) {
                    valid_ = false;
                    failure_ =
                        "guest execution breakpoint callback disappeared";
                    break;
                }
                Aarch64PausedExecutionState state;
                state.registers = jit_->GetRegisters();
                state.stack_pointer = jit_->GetSP();
                // Dynarmic advances its visible PC past the synthesized BRK
                // before raising the exception. Expose the exact skipped
                // guest instruction address promised by this API.
                state.program_counter = breakpoint_address;
                std::string callback_error;
                bool callback_succeeded = false;
                try {
                    callback_succeeded = breakpoint->callback(
                        &state, &callback_error);
                } catch (...) {
                    callback_error =
                        "guest execution breakpoint callback threw an exception";
                }
                std::uint32_t resumed_instruction = 0;
                const bool resumed_state_valid =
                    (state.program_counter & 3U) == 0 &&
                    (state.stack_pointer & 15U) == 0 &&
                    state.stack_pointer >= stack_ &&
                    state.stack_pointer <= stack_ + stack_size_ &&
                    (state.program_counter == kTerminalAddress ||
                     ReadExecutableInstruction(state.program_counter,
                                               &resumed_instruction));
                if (!callback_succeeded || !resumed_state_valid) {
                    valid_ = false;
                    if (!callback_succeeded) {
                        failure_ = callback_error.empty()
                                       ? "guest execution breakpoint callback failed"
                                       : "guest execution breakpoint callback failed: " +
                                             callback_error;
                    } else {
                        failure_ =
                            "guest execution breakpoint callback returned an invalid resume state";
                    }
                    break;
                }
                for (std::size_t index = 0; index < state.registers.size();
                     ++index)
                    jit_->SetRegister(index, state.registers[index]);
                jit_->SetSP(state.stack_pointer);
                jit_->SetPC(state.program_counter);
                if (total_ticks_remaining_ == 0) break;
                continue;
            }
            const bool slice_expired =
                inter_slice_callback_ && valid_ && !stopped_ &&
                !memory_capture_pending_ &&
                (inter_slice_renews_total_budget_ ||
                 total_ticks_remaining_ != 0) &&
                slice_ticks_remaining_ == 0;
            if (!slice_expired) {
                if (trace_call)
                    std::fprintf(stderr,
                                 "CE loop-break fn=0x%llx pc=0x%llx\n",
                                 (unsigned long long)function,
                                 (unsigned long long)jit_->GetPC());
                break;
            }
            if (execution_telemetry_)
                execution_telemetry_->Publish(
                    WorkerExecutionPhase::kInterSlice, true, jit_->GetPC());
            if (full_call_telemetry_active_) {
                std::uint64_t parent_link_register = 0;
                const std::uint64_t frame_pointer = jit_->GetRegister(29);
                if ((frame_pointer & 15U) == 0 && frame_pointer >= stack_ &&
                    frame_pointer <= stack_ + stack_size_ - 16) {
                    (void)heap_->Read(frame_pointer + sizeof(std::uint64_t),
                                      &parent_link_register,
                                      sizeof(parent_link_register));
                }
                AccumulateAarch64GuestPcTelemetry(
                    &last_call_telemetry_, jit_->GetPC(),
                    jit_->GetRegister(30), parent_link_register,
                    jit_->GetRegister(0), jit_->GetRegister(1),
                    jit_->GetRegister(2), jit_->GetRegister(3),
                    jit_->GetRegister(8), jit_->GetRegister(10),
                    jit_->GetRegister(11),
                    jit_->GetRegister(19), jit_->GetRegister(22),
                    jit_->GetRegister(24));
            }
            std::string callback_error;
            bool callback_succeeded = false;
            std::optional<std::chrono::steady_clock::time_point>
                callback_start;
            if (call_telemetry_active_)
                callback_start = std::chrono::steady_clock::now();
            inter_slice_callback_active_ = true;
            try {
                callback_succeeded = inter_slice_callback_(&callback_error);
            } catch (...) {
                callback_error =
                    "guest inter-slice callback threw an exception";
            }
            inter_slice_callback_active_ = false;
            if (call_telemetry_active_ && callback_start) {
                ++last_call_telemetry_.inter_slice_callback_invocations;
                last_call_telemetry_.inter_slice_callback_wall_time_ns +=
                    static_cast<std::uint64_t>(
                        std::chrono::duration_cast<std::chrono::nanoseconds>(
                            std::chrono::steady_clock::now() -
                            *callback_start)
                            .count());
            }
            if (!callback_succeeded) {
                valid_ = false;
                failure_ = callback_error.empty()
                               ? "guest inter-slice callback failed"
                               : "guest inter-slice callback failed: " +
                                     callback_error;
                break;
            }
            const std::uint64_t next_slice_instruction_budget =
                next_inter_slice_instruction_budget_ != 0
                    ? next_inter_slice_instruction_budget_
                    : inter_slice_instruction_budget_;
            next_inter_slice_instruction_budget_ = 0;
            if (inter_slice_renews_total_budget_)
                total_ticks_remaining_ = next_slice_instruction_budget;
            slice_ticks_remaining_ = std::min(
                total_ticks_remaining_, next_slice_instruction_budget);
        }
        bool call_succeeded = false;
        std::string call_error;
        Aarch64CallResult captured{};
        if (trace_call)
            std::fprintf(stderr,
                         "CE postloop fn=0x%llx pc=0x%llx valid=%d stopped=%d\n",
                         (unsigned long long)function,
                         (unsigned long long)jit_->GetPC(), (int)valid_,
                         (int)stopped_);
        if (process_exit_cancellation_observed_) {
            call_error = "guest call cancelled for process exit";
        } else if (memory_capture_pending_) {
            memory_access_capture_.program_counter = jit_->GetPC();
            memory_access_capture_.registers = jit_->GetRegisters();
            memory_access_capture_.stack_pointer = jit_->GetSP();
            std::ostringstream message;
            message << "guest memory capture access="
                    << (memory_access_capture_.access ==
                                Aarch64MemoryCaptureAccess::kRead
                            ? "read"
                            : "write")
                    << " generation="
                    << memory_access_capture_.watch_generation
                    << " occurrence=" << std::dec
                    << memory_access_capture_.occurrence
                    << " address=0x" << std::hex
                    << memory_access_capture_.address << " size=" << std::dec
                    << memory_access_capture_.size << " value=0x" << std::hex
                    << memory_access_capture_.value[0] << ":0x"
                    << memory_access_capture_.value[1] << " pc=0x"
                    << memory_access_capture_.program_counter;
            for (std::size_t index = 0;
                 index < memory_access_capture_.registers.size(); ++index)
                message << " x" << std::dec << index << "=0x" << std::hex
                        << memory_access_capture_.registers[index];
            message << " sp=0x" << memory_access_capture_.stack_pointer;
            const std::string recent_imports =
                imports_->RecentDispatchedNamesForCurrentThread(64);
            message << " recent-imports="
                    << (recent_imports.empty() ? "none" : recent_imports);
            const std::string recent_notes =
                imports_->RecentSemanticNotesForCurrentThread(64);
            message << " recent-notes="
                    << (recent_notes.empty() ? "none" : recent_notes);
            for (std::size_t index = 0;
                 index < memory_access_capture_.registers.size(); ++index) {
                const std::uint64_t address =
                    memory_access_capture_.registers[index];
                if (heap_->ContainsAddress(address)) {
                    message << " heap-x" << std::dec << index << '='
                            << heap_->DescribeRange(address, 1);
                    std::uint64_t first_word = 0;
                    if (heap_->Read(address, &first_word,
                                    sizeof(first_word)))
                        message << "-word0x" << std::hex << first_word;
                    if (index >= 19 && index <= 22) {
                        for (std::size_t word = 1; word < 10; ++word) {
                            std::uint64_t contents = 0;
                            if (!heap_->Read(address + word * sizeof(contents),
                                             &contents, sizeof(contents)))
                                break;
                            message << "-word" << std::dec << word << "x"
                                    << std::hex << contents;
                            if (heap_->ContainsAddress(contents)) {
                                std::uint64_t target_word = 0;
                                if (heap_->Read(contents, &target_word,
                                                sizeof(target_word)))
                                    message << "-word" << std::dec << word
                                            << "target"
                                            << heap_->DescribeRange(contents, 1)
                                            << "-targetword0x" << std::hex
                                            << target_word;
                            }
                        }
                    }
                }
            }
            call_error = message.str();
        } else if (!valid_ || !stopped_) {
            if (failure_.empty()) {
                std::uint32_t current_instruction = 0;
                const char* instruction_class = "unreadable";
                if (modules_->ReadCode(jit_->GetPC(), &current_instruction,
                                       sizeof(current_instruction)))
                    instruction_class = InstructionClass(current_instruction);
                std::ostringstream message;
                message << "guest call exhausted its instruction budget at pc=0x"
                        << std::hex << jit_->GetPC() << " class="
                        << instruction_class << " last-read="
                        << (last_read_heap_ ? "heap" : "module") << '-'
                        << (last_read_zero_ ? "zero" : "nonzero")
                        << " frame-chain=" << DescribeFrameChain();
                const std::string recent_imports =
                    imports_->RecentDispatchedNamesForCurrentThread(64);
                const std::string recent_jni =
                    imports_->RecentJniDispatchedNamesForCurrentThread(8);
                const std::string recent_notes =
                    imports_->RecentSemanticNotesForCurrentThread(64);
                message << " recent-imports="
                        << (recent_imports.empty() ? "none" : recent_imports)
                        << " recent-jni="
                        << (recent_jni.empty() ? "none" : recent_jni)
                        << " recent-notes="
                        << (recent_notes.empty() ? "none" : recent_notes);
                // Scan the live stack image for return addresses: the fp
                // chain is often broken on forced halts, but leaf/deep
                // frames still leave module-return pointers on the stack.
                {
                    std::ostringstream stack_scan;
                    int hits = 0;
                    int reads = 0;
                    const std::uint64_t sp = jit_->GetSP();
                    const std::uint64_t scan_end =
                        stack_ + stack_size_;
                    for (std::uint64_t addr = sp;
                         addr + sizeof(std::uint64_t) <= scan_end &&
                         hits < 24;
                         addr += sizeof(std::uint64_t)) {
                        std::uint64_t value = 0;
                        if (!heap_->Read(addr, &value, sizeof(value)))
                            break;
                        ++reads;
                        const auto resolved =
                            modules_->ResolveCodeAddress(value);
                        if (!resolved) continue;
                        stack_scan << (hits ? "," : "") << resolved->module
                                   << "+0x" << std::hex << resolved->offset
                                   << std::dec;
                        ++hits;
                    }
                    message << " stack-scan="
                            << (hits ? stack_scan.str()
                                     : std::string("none"))
                            << " scan-sp=0x" << std::hex << sp
                            << " stack-base=0x" << stack_
                            << " stack-size=0x" << stack_size_
                            << " reads=" << std::dec << reads;
                }
                call_error = message.str();
            } else {
                call_error = failure_;
            }
        } else if (jit_->GetSP() != entry_stack) {
            call_error = "guest call returned with an unbalanced stack";
        } else {
            captured.x0 = jit_->GetRegister(0);
            captured.v0 = jit_->GetVector(0);
            call_succeeded = true;
        }
        bool cleanup_succeeded = true;
        if (!stack_image.empty()) {
            std::fill(stack_image.begin(), stack_image.end(),
                      std::uint8_t{0});
            cleanup_succeeded = heap_->Write(
                entry_stack, stack_image.data(), stack_image.size());
        }
        jit_->SetSP(stack_top);
        if (!cleanup_succeeded) {
            SetError(error, call_error.empty()
                                ? "guest call stack cleanup failed"
                                : "guest call stack cleanup failed after: " +
                                      call_error);
            return false;
        }
        if (!call_succeeded) {
            if (trace_call)
                std::fprintf(stderr, "CE fail fn=0x%llx err=%.200s\n",
                             (unsigned long long)function,
                             call_error.c_str());
            SetError(error, std::move(call_error));
            return false;
        }
        *result = captured;
        if (trace_call)
            std::fprintf(stderr, "CE success fn=0x%llx x0=0x%llx\n",
                         (unsigned long long)function,
                         (unsigned long long)captured.x0);
        if (execution_telemetry_)
            execution_telemetry_->Publish(
                WorkerExecutionPhase::kCallSuccess, true, jit_->GetPC());
        execution_scope.succeeded = true;
        telemetry_scope.MarkSucceeded();
        return true;
    }

    void RequestHalt() {
        if (jit_ != nullptr)
            jit_->HaltExecution(Dynarmic::HaltReason::UserDefined1);
    }
    std::uint64_t RegisterValue(std::size_t index) const {
        return jit_ != nullptr && index < 31 ? jit_->GetRegister(index) : 0;
    }
    std::uint64_t ProgramCounter() const {
        return jit_ != nullptr ? jit_->GetPC() : 0;
    }
    std::uint64_t StackPointer() const {
        return jit_ != nullptr ? jit_->GetSP() : 0;
    }
    std::uint64_t InstructionsExecuted() const {
        return instructions_executed_total_;
    }
    std::uint64_t StackBase() const { return stack_; }
    std::uint64_t StackTop() const { return stack_ + stack_size_; }
    std::uint64_t ThreadPointer() const { return thread_pointer_; }
    Aarch64MemoryAccessCapture MemoryAccessCapture() const {
        return memory_access_capture_;
    }

    std::optional<std::uint32_t> MemoryReadCode(
        std::uint64_t address) override {
        if (call_telemetry_active_)
            ++last_call_telemetry_.memory_read_code_invocations;
        if (address == kTerminalAddress) return kTerminalBreak;
        const auto execution_breakpoint = std::find_if(
            execution_breakpoints_.begin(), execution_breakpoints_.end(),
            [address](const ExecutionBreakpoint& breakpoint) {
                return breakpoint.address == address;
            });
        if (execution_breakpoint != execution_breakpoints_.end())
            return kTerminalBreak;
        if (memory_capture_config_.breakpoint_address != 0 &&
            address == memory_capture_config_.breakpoint_address)
            return kTerminalBreak;
        std::uint32_t value = 0;
        if (address >= imports_->guest_base() &&
            address < imports_->guest_base() + imports_->code_size()) {
            if (imports_->ReadCode(address, &value, sizeof(value))) return value;
        } else if (modules_->ReadCode(address, &value, sizeof(value))) {
            return value;
        } else if (heap_ != nullptr && heap_->IsExecutable(address) &&
                   heap_->Read(address, &value, sizeof(value))) {
            // A guest PROT_EXEC mmap/mprotect page outside any loaded module:
            // packers (pglarmor, il2cpp trampolines, VMP) decrypt code into
            // anonymous memory and jump into it, so the JIT may fetch from it.
            return value;
        }
        // A `blr`/`br` into the null page is a call on a never-constructed
        // vtable slot (e.g. Dac+0x198, the SL buffer-queue interface that
        // PlatformAudioInitAndroid leaves 0 when its earlier SL step fails).
        // Data reads already demand-zero; making the call target a `ret`
        // turns the null virtual into a benign no-op instead of aborting the
        // whole session. Evidence stays visible in the log.
        if (address < 0x10000ULL) {
            static std::size_t null_code_calls = 0;
            if (null_code_calls < 12) {
                ++null_code_calls;
                std::fprintf(stderr,
                             "ISOMR_NULL_CALL target=%llx lr=%llx\n",
                             static_cast<unsigned long long>(address),
                             static_cast<unsigned long long>(
                                 jit_ != nullptr ? jit_->GetRegister(30)
                                                 : 0));
                std::fflush(stderr);
            }
            return 0xd65f03c0U;  // ret
        }
        Abort("guest instruction fetch touched an invalid range");
        return std::nullopt;
    }

    std::uint8_t MemoryRead8(std::uint64_t address) override {
        if (full_call_telemetry_active_)
            ++last_call_telemetry_.scalar_read_invocations;
        const auto sample = BeginMemoryCallback(true, address);
        const std::uint8_t value = Read<std::uint8_t>(address);
        FinishMemoryCallback(sample);
        return value;
    }
    std::uint16_t MemoryRead16(std::uint64_t address) override {
        if (full_call_telemetry_active_)
            ++last_call_telemetry_.scalar_read_invocations;
        const auto sample = BeginMemoryCallback(true, address);
        const std::uint16_t value = Read<std::uint16_t>(address);
        FinishMemoryCallback(sample);
        return value;
    }
    std::uint32_t MemoryRead32(std::uint64_t address) override {
        if (full_call_telemetry_active_)
            ++last_call_telemetry_.scalar_read_invocations;
        const auto sample = BeginMemoryCallback(true, address);
        const std::uint32_t value = Read<std::uint32_t>(address);
        FinishMemoryCallback(sample);
        return value;
    }
    std::uint64_t MemoryRead64(std::uint64_t address) override {
        if (full_call_telemetry_active_)
            ++last_call_telemetry_.scalar_read_invocations;
        const auto sample = BeginMemoryCallback(true, address);
        const std::uint64_t value = Read<std::uint64_t>(address);
        FinishMemoryCallback(sample);
        return value;
    }
    A64::Vector MemoryRead128(std::uint64_t address) override {
        if (full_call_telemetry_active_)
            ++last_call_telemetry_.vector_read_invocations;
        const auto sample = BeginMemoryCallback(true, address);
        A64::Vector value{};
        if (!forensic_memory_callbacks_) {
            if (!ReadFastMemory(address, value.data(), sizeof(value)))
                Abort("guest vector read touched an invalid range");
            FinishMemoryCallback(sample);
            return value;
        }
        std::uint64_t watched_generation = 0;
        if (!ReadMemory(address, value.data(), sizeof(value),
                        &watched_generation)) {
            Abort("guest vector read touched an invalid range");
        } else {
            CaptureMemoryAccess(Aarch64MemoryCaptureAccess::kRead, address,
                                value.data(), sizeof(value),
                                watched_generation);
        }
        FinishMemoryCallback(sample);
        return value;
    }
    void MemoryWrite8(std::uint64_t address, std::uint8_t value) override {
        const auto sample = BeginWriteCallback(address, false);
        Write(address, value);
        FinishMemoryCallback(sample);
    }
    void MemoryWrite16(std::uint64_t address, std::uint16_t value) override {
        const auto sample = BeginWriteCallback(address, false);
        Write(address, value);
        FinishMemoryCallback(sample);
    }
    void MemoryWrite32(std::uint64_t address, std::uint32_t value) override {
        const auto sample = BeginWriteCallback(address, false);
        Write(address, value);
        FinishMemoryCallback(sample);
    }
    void MemoryWrite64(std::uint64_t address, std::uint64_t value) override {
        const auto sample = BeginWriteCallback(address, false);
        Write(address, value);
        FinishMemoryCallback(sample);
    }
    void MemoryWrite128(std::uint64_t address, A64::Vector value) override {
        const auto sample = BeginWriteCallback(address, false);
        if (!forensic_memory_callbacks_) {
            if (!WriteFastMemory(address, value.data(), sizeof(value)))
                Abort("guest vector write touched an invalid range");
            FinishMemoryCallback(sample);
            return;
        }
        std::uint64_t watched_generation = 0;
        if (!WriteMemory(address, value.data(), sizeof(value),
                         &watched_generation)) {
            Abort("guest vector write touched an invalid range");
        } else {
            CaptureMemoryAccess(Aarch64MemoryCaptureAccess::kWrite, address,
                                value.data(), sizeof(value),
                                watched_generation);
        }
        FinishMemoryCallback(sample);
    }
    bool MemoryWriteExclusive8(std::uint64_t address, std::uint8_t value,
                               std::uint8_t expected) override {
        const auto sample = BeginWriteCallback(address, true);
        const bool written = ExclusiveWrite(address, value, expected);
        FinishMemoryCallback(sample);
        return written;
    }
    bool MemoryWriteExclusive16(std::uint64_t address, std::uint16_t value,
                                std::uint16_t expected) override {
        const auto sample = BeginWriteCallback(address, true);
        const bool written = ExclusiveWrite(address, value, expected);
        FinishMemoryCallback(sample);
        return written;
    }
    bool MemoryWriteExclusive32(std::uint64_t address, std::uint32_t value,
                                std::uint32_t expected) override {
        const auto sample = BeginWriteCallback(address, true);
        const bool written = ExclusiveWrite(address, value, expected);
        FinishMemoryCallback(sample);
        return written;
    }
    bool MemoryWriteExclusive64(std::uint64_t address, std::uint64_t value,
                                std::uint64_t expected) override {
        const auto sample = BeginWriteCallback(address, true);
        const bool written = ExclusiveWrite(address, value, expected);
        FinishMemoryCallback(sample);
        return written;
    }
    bool MemoryWriteExclusive128(std::uint64_t address, A64::Vector value,
                                 A64::Vector expected) override {
        const auto sample = BeginWriteCallback(address, true);
        A64::Vector current{};
        if (!ReadMemory(address, current.data(), sizeof(current)) ||
            current != expected) {
            FinishMemoryCallback(sample);
            return false;
        }
        if (WriteMemory(address, value.data(), sizeof(value))) {
            FinishMemoryCallback(sample);
            return true;
        }
        Abort("guest exclusive vector write touched an invalid range");
        FinishMemoryCallback(sample);
        return false;
    }
    void InterpreterFallback(std::uint64_t address, std::size_t) override {
        StopOrAbort(address, "guest instruction required interpreter fallback");
    }
    void CallSVC(std::uint32_t immediate) override {
        std::optional<std::chrono::steady_clock::time_point> telemetry_start;
        Aarch64SvcCategory telemetry_category =
            Aarch64SvcCategory::kOther;
        const std::optional<std::string_view> telemetry_import_name =
            svc_category_telemetry_active_
                ? imports_->NameForImmediate(immediate)
                : std::nullopt;
        if (call_telemetry_active_) {
            ++last_call_telemetry_.svc_invocations;
            if (full_call_telemetry_active_ ||
                svc_category_telemetry_active_)
                telemetry_start = std::chrono::steady_clock::now();
        }
        Aarch64ImportRegistry::Registers registers{};
        for (std::size_t index = 0; index < registers.size(); ++index) {
            registers[index] = jit_->GetRegister(index);
            registers.vector[index] = jit_->GetVector(index);
        }
        for (std::size_t index = 0;
             index < registers.callee_saved_general.size(); ++index)
            registers.callee_saved_general[index] =
                jit_->GetRegister(index + 19);
        registers.platform_register = jit_->GetRegister(18);
        for (std::size_t index = 0;
             index < registers.callee_saved_vector.size(); ++index)
            registers.callee_saved_vector[index] =
                jit_->GetVector(index + 8);
        registers.stack_pointer = jit_->GetSP();
        if (svc_category_telemetry_active_ && telemetry_import_name &&
            *telemetry_import_name == "clock_gettime") {
            const std::uint64_t clock_id = registers[0];
            if (clock_id <
                last_call_telemetry_.clock_gettime_clock_ids.size()) {
                ++last_call_telemetry_.clock_gettime_clock_ids[
                    static_cast<std::size_t>(clock_id)];
            } else {
                ++last_call_telemetry_.clock_gettime_unknown_clock_ids;
            }
        }
        registers.nanosleep_requested_duration_diagnostic.Reset(
            telemetry_import_name &&
            *telemetry_import_name == "nanosleep");
        if (svc_category_telemetry_active_)
            telemetry_category = ClassifySvcCategory(
                telemetry_import_name, registers[0],
                registers[2]);
        std::string error;
        bool dispatched = false;
        if (immediate == 0) {
            // A raw `svc #0` is the real AArch64 Linux syscall instruction:
            // the syscall number is in x8 and the arguments in x0-x5.
            // Protected/packed loaders issue syscalls this way to bypass
            // hooked libc, so route it to the "syscall" dispatcher rather than
            // treating immediate 0 as an invalid import. Remap the frame to
            // the libc-syscall convention the handler expects: slot0 = number
            // (from x8), slots1-6 = args (x0-x5). The result lands back in x0.
            Aarch64ImportRegistry::Registers sysregs = registers;
            sysregs[0] = jit_->GetRegister(8);
            for (std::size_t arg = 0; arg < 6; ++arg)
                sysregs[arg + 1] = registers[arg];
            // futex (98) is where a single-threaded loader can park forever;
            // log its pc/lr/args so the blocking call site is identifiable.
            if (sysregs[0] == 98) {
                static std::size_t futex_log_count = 0;
                if (futex_log_count++ < 20) {
                    std::fprintf(stderr,
                                 "ISOMR_SVC_FUTEX pc=0x%llx lr=0x%llx "
                                 "addr=0x%llx op=0x%llx val=0x%llx to=0x%llx\n",
                                 (unsigned long long)jit_->GetPC(),
                                 (unsigned long long)jit_->GetRegister(30),
                                 (unsigned long long)sysregs[1],
                                 (unsigned long long)sysregs[2],
                                 (unsigned long long)sysregs[3],
                                 (unsigned long long)sysregs[4]);
                    if ((sysregs[2] & 0x7f) == 0)  // FUTEX_WAIT: dump call chain
                        std::fprintf(stderr, "ISOMR_SVC_FUTEX_FRAMES %s\n",
                                     DescribeFrameChain().c_str());
                }
            }
            dispatched = imports_->Dispatch("syscall", &sysregs, &error);
            if (dispatched) {
                registers[0] = sysregs[0];
            } else if (error.empty()) {
                error = "unsupported raw AArch64 svc #0 syscall nr=" +
                        std::to_string(sysregs[0]);
            }
        } else {
            dispatched = imports_->Dispatch(immediate, &registers, &error);
        }
        if (!dispatched) {
            error += " x0-producer=" +
                     DescribeX0Producer(jit_->GetRegister(30));
            error += " heap-registers=" + DescribeHeapRegisters();
            error += " frame-chain=" + DescribeFrameChain();
            Abort(error.empty() ? "guest import dispatch failed" : error);
            FinishSvcTelemetry(telemetry_start, telemetry_category,
                               telemetry_import_name, immediate);
            return;
        }
        if (ObserveProcessExitCancellation()) {
            FinishSvcTelemetry(telemetry_start, telemetry_category,
                               telemetry_import_name, immediate);
            return;
        }
        if (registers[0] == 0 &&
            registers.nanosleep_requested_duration_diagnostic.result)
            last_call_telemetry_.svc_imports.
                AccumulateSuccessfulNanosleepRequest(
                    immediate,
                    *registers.nanosleep_requested_duration_diagnostic.result);
        if (registers.non_local_return.requested) {
            std::uint32_t instruction = 0;
            const std::uint64_t resumed_sp =
                registers.non_local_return.stack_pointer;
            if ((resumed_sp & 15U) != 0 || resumed_sp < stack_ ||
                resumed_sp > stack_ + stack_size_ ||
                !modules_->ReadCode(
                    registers.non_local_return.program_counter,
                    &instruction, sizeof(instruction))) {
                Abort("guest non-local return target is invalid");
                FinishSvcTelemetry(telemetry_start, telemetry_category,
                                   telemetry_import_name, immediate);
                return;
            }
        }
        for (std::size_t index = 0; index < registers.size(); ++index) {
            jit_->SetRegister(index, registers[index]);
            jit_->SetVector(index, registers.vector[index]);
        }
        for (std::size_t index = 0;
             index < registers.callee_saved_general.size(); ++index)
            jit_->SetRegister(index + 19,
                              registers.callee_saved_general[index]);
        jit_->SetRegister(18, registers.platform_register);
        for (std::size_t index = 0;
             index < registers.callee_saved_vector.size(); ++index)
            jit_->SetVector(index + 8,
                            registers.callee_saved_vector[index]);
        if (registers.non_local_return.requested) {
            jit_->SetSP(registers.non_local_return.stack_pointer);
            jit_->SetPC(registers.non_local_return.program_counter);
        }
        FinishSvcTelemetry(telemetry_start, telemetry_category,
                           telemetry_import_name, immediate);
    }
    void ExceptionRaised(std::uint64_t address,
                         A64::Exception exception) override {
        const auto execution_breakpoint = std::find_if(
            execution_breakpoints_.begin(), execution_breakpoints_.end(),
            [address](const ExecutionBreakpoint& breakpoint) {
                return breakpoint.address == address;
            });
        if (execution_breakpoint != execution_breakpoints_.end() &&
            execution_breakpoint->callback &&
            std::this_thread::get_id() ==
                execution_breakpoint_owner_thread_) {
            pending_execution_breakpoint_address_ = address;
            jit_->HaltExecution(Dynarmic::HaltReason::MemoryAbort);
            return;
        }
        const std::string failure =
            std::string("guest raised an exception: ") +
            ExceptionName(exception) + " target=" +
            DescribeExceptionTarget(address) + " target-address=" +
            HexAddress(address) + " link=" +
            DescribeExceptionTarget(jit_->GetRegister(30)) +
            " link-address=" + HexAddress(jit_->GetRegister(30)) +
            " link-module=" + [&] {
                const auto resolved =
                    modules_->ResolveCodeAddress(jit_->GetRegister(30));
                return resolved ? (resolved->module + "+" +
                                   std::to_string(resolved->offset))
                                : std::string("unmapped");
            }() +
            " sp=" + HexAddress(jit_->GetSP()) +
            " x0dump=[" + [&] {
                std::string dump;
                const std::uint64_t obj = jit_->GetRegister(0);
                if (obj == 0) return std::string("null") + "]";
                for (std::uint64_t off = 0; off < 64; off += 8) {
                    std::uint64_t word = 0;
                    if (!ReadMemory(obj + off, &word, sizeof(word), nullptr))
                        dump += " ?";
                    else
                        dump += " " + HexAddress(word);
                }
                return dump + " ]";
            }() +
            " stack=[" + [&] {
                std::string dump;
                const std::uint64_t sp = jit_->GetSP();
                for (std::uint64_t off = 0; off < 128; off += 8) {
                    std::uint64_t word = 0;
                    if (ReadMemory(sp + off, &word, sizeof(word), nullptr) ||
                        ReadMemory(sp + off, &word, sizeof(word), nullptr))
                        dump += (off != 0 ? std::string(" ") : std::string()) + HexAddress(word);
                    else
                        dump += " ?";
                }
                return dump;
            }() +
            " vmobj=[" + [&] {
                // The Nimble init path passes the guest JavaVM in a stack
                // slot; dump its first words to expose the function table.
                std::string dump;
                const std::uint64_t candidate = 0x710000000760ULL;
                {
                    std::uint64_t probe = 0;
                    if (!ReadMemory(candidate, &probe, sizeof(probe),
                                    nullptr)) {
                        return std::string("vm-unreadable");
                    }
                }
                dump += "vm@0x710000000760: ";
                for (std::uint64_t off = 0; off < 64; off += 8) {
                    std::uint64_t word = 0;
                    if (!ReadMemory(candidate + off, &word, sizeof(word),
                                    nullptr))
                        dump += "? ";
                    else
                        dump += HexAddress(word) + " ";
                }
                return dump;
            }() +
            " fp=" + HexAddress(jit_->GetRegister(29)) +
            " x19glob=[" + [&] {
                std::string dump;
                const std::uint64_t page = jit_->GetRegister(19);
                if (page == 0) return std::string("x19-null") + "]";
                for (std::uint64_t off = 0; off < 256; off += 8) {
                    std::uint64_t word = 0;
                    if (!ReadMemory(page + 0xd28 - 0x40 + off, &word,
                                    sizeof(word), nullptr)) {
                        dump += " ? ";
                        continue;
                    }
                    if (word != 0)
                        dump += HexAddress(word) + " ";
                    else
                        dump += "0 ";
                }
                return dump;
            }() +
            " x0vtbl=[" + [&] {
                std::string dump;
                const std::uint64_t obj = jit_->GetRegister(0);
                if (obj == 0) return std::string("null");
                std::uint64_t vtable = 0;
                if (!ReadMemory(obj, &vtable, sizeof(vtable), nullptr))
                    return std::string("unreadable");
                dump += HexAddress(vtable) + " entries:";
                for (std::uint64_t slot = 0; slot < 8; ++slot) {
                    std::uint64_t fn = 0;
                    if (!ReadMemory(vtable + slot * 8, &fn, sizeof(fn), nullptr)) {
                        dump += " ?";
                        continue;
                    }
                    dump += " " + HexAddress(fn);
                }
                return dump;
            }() +
            " x0data=[" + [&] {
                std::string dump;
                const std::uint64_t obj = jit_->GetRegister(0);
                if (obj == 0) return std::string("null");
                for (std::uint64_t off = 8; off < 72; off += 8) {
                    std::uint64_t word = 0;
                    if (!ReadMemory(obj + off, &word, sizeof(word), nullptr)) {
                        dump += " ?";
                        continue;
                    }
                    dump += " " + HexAddress(word);
                }
                return dump;
            }() +
            " x0obj=[" + [&] {
                std::string dump;
                const std::uint64_t obj = jit_->GetRegister(0);
                if (obj == 0) return std::string("null") + "]";
                for (std::uint64_t off = 0; off < 64; off += 8) {
                    std::uint64_t word = 0;
                    if (!ReadMemory(obj + off, &word, sizeof(word), nullptr)) {
                        dump += "? ";
                        continue;
                    }
                    dump += (off ? " " : "") + HexAddress(word);
                }
                return dump;
            }() + "]" +
            [&] {
                std::string regs;
                for (unsigned r = 0; r <= 30; ++r)
                    regs += " x" + std::to_string(r) + "=" +
                            HexAddress(jit_->GetRegister(r));
                return regs;
            }() +
            " thread=" +
            HexAddress(static_cast<std::uint64_t>(
                std::hash<std::thread::id>{}(std::this_thread::get_id()))) +
            " dispatch-seq=" +
            std::to_string(
                imports_->CurrentDispatchSequenceForCurrentThread()
                    .value_or(0)) +
            " last-import=" +
            std::string(imports_->LastDispatchedNameForCurrentThread()
                            .value_or("none")) +
            " recent-imports=" +
            [&] {
                const std::string recent =
                    imports_->RecentDispatchedNamesForCurrentThread(64);
                return recent.empty() ? std::string("none") : recent;
            }() +
            " recent-jni=" +
            [&] {
                const std::string recent =
                    imports_->RecentJniDispatchedNamesForCurrentThread(8);
                return recent.empty() ? std::string("none") : recent;
            }() +
            " last-jni-note=" +
            std::string(imports_->LastJniSemanticNoteForCurrentThread()
                            .value_or("none")) +
            " last-jni-seq=" +
            std::to_string(
                imports_->LastJniDispatchSequenceForCurrentThread()
                    .value_or(0)) +
            " recent-jni-notes=" +
            [&] {
                const std::string recent =
                    imports_->RecentJniSemanticNotesForCurrentThread(64);
                return recent.empty() ? std::string("none") : recent;
            }() +
            " recent-notes=" +
            [&] {
                const std::string recent =
                    imports_->RecentSemanticNotesForCurrentThread(8);
                return recent.empty() ? std::string("none") : recent;
            }() +
            " all-thread-imports=" +
            [&] {
                const std::string recent =
                    imports_->RecentDispatchJournal(64);
                return recent.empty() ? std::string("none") : recent;
            }() +
            " branch=" + DescribeNullBranch(jit_->GetRegister(30)) +
            " read-chain=" +
            DescribeNullReadChain(jit_->GetRegister(30)) +
            " registers=" + DescribeGeneralRegisters() +
            " heap-register-words=" + DescribeHeapRegisters() +
            " heap-register-ranges=" + DescribeHeapRegisterRanges() +
            " stack-words=" + DescribeStackWords() +
            " frame-chain=" + DescribeFrameChain();
        StopOrAbort(address, failure.c_str());
    }
    void AddTicks(std::uint64_t ticks) override {
        g_armr_instructions_executed.fetch_add(
            ticks, std::memory_order_relaxed);
        if (call_telemetry_active_) {
            const std::uint64_t current =
                last_call_telemetry_.translated_ticks;
            last_call_telemetry_.translated_ticks =
                ticks > UINT64_MAX - current ? UINT64_MAX : current + ticks;
        }
        instructions_executed_total_ =
            ticks > UINT64_MAX - instructions_executed_total_
                ? UINT64_MAX
                : instructions_executed_total_ + ticks;
        total_ticks_remaining_ =
            ticks >= total_ticks_remaining_ ? 0
                                            : total_ticks_remaining_ - ticks;
        slice_ticks_remaining_ =
            ticks >= slice_ticks_remaining_ ? 0
                                            : slice_ticks_remaining_ - ticks;
    }
    std::uint64_t GetTicksRemaining() override {
        return slice_ticks_remaining_;
    }
    std::uint64_t GetCNTPCT() override {
        if (full_call_telemetry_active_)
            ++last_call_telemetry_.cntpct_invocations;
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch())
                .count());
    }

private:
    bool ReadExecutableInstruction(std::uint64_t address,
                                   std::uint32_t* instruction) const {
        if (instruction == nullptr) return false;
        if (address >= imports_->guest_base() &&
            address < imports_->guest_base() + imports_->code_size())
            return imports_->ReadCode(address, instruction,
                                      sizeof(*instruction));
        return modules_->ReadCode(address, instruction,
                                  sizeof(*instruction));
    }

    bool BeginCallTelemetry() {
        call_telemetry_active_ =
            telemetry_mode_ != Aarch64CallTelemetryMode::kOff;
        if (!call_telemetry_active_) return false;
        full_call_telemetry_active_ =
            telemetry_mode_ == Aarch64CallTelemetryMode::kFull;
        svc_category_telemetry_active_ =
            telemetry_mode_ == Aarch64CallTelemetryMode::kSvcCategory;
        last_call_telemetry_ = {};
        last_call_telemetry_.enabled = true;
        last_call_telemetry_.mode = telemetry_mode_;
        if (full_call_telemetry_active_) {
            try {
                last_call_telemetry_.guest_pc_histogram =
                    std::make_shared<
                        std::array<Aarch64GuestPcTelemetry, 64>>();
            } catch (...) {
                // Guest-PC telemetry is diagnostic only. Full-call counters
                // remain authoritative if bounded storage cannot be created.
            }
        }
        if (svc_category_telemetry_active_) {
            try {
                std::vector<std::string_view> import_names;
                import_names.reserve(imports_->size());
                for (std::size_t index = 0; index < imports_->size(); ++index) {
                    const auto name = imports_->NameForImmediate(
                        static_cast<std::uint32_t>(index + 1));
                    if (!name) {
                        import_names.clear();
                        break;
                    }
                    import_names.push_back(*name);
                }
                if (!import_names.empty())
                    (void)last_call_telemetry_.svc_imports.Prepare(
                        import_names);
            } catch (...) {
                // Import telemetry is diagnostic only. Category totals remain
                // authoritative if stable storage cannot be prepared.
            }
        }
        memory_callback_ordinal_ = 0;
        call_wall_start_ = std::chrono::steady_clock::now();
        call_cpu_start_available_ =
            ReadCurrentThreadCpuTime(&call_cpu_start_);
        call_heap_metadata_start_ =
            GuestHeap::GlobalMetadataGenerationAdvanceCount();
        return true;
    }

    void FinishCallTelemetry(bool succeeded) {
        const auto wall_end = std::chrono::steady_clock::now();
        std::uint64_t cpu_end = 0;
        const bool cpu_end_available = ReadCurrentThreadCpuTime(&cpu_end);
        last_call_telemetry_.result_state =
            succeeded ? Aarch64GuestCallResultState::kSucceeded
                      : Aarch64GuestCallResultState::kFailed;
        last_call_telemetry_.completion_sequence =
            ++call_telemetry_completion_sequence_;
        last_call_telemetry_.wall_time_ns =
            static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    wall_end - call_wall_start_)
                    .count());
        last_call_telemetry_.thread_cpu_time_available =
            call_cpu_start_available_ && cpu_end_available &&
            cpu_end >= call_cpu_start_;
        if (last_call_telemetry_.thread_cpu_time_available)
            last_call_telemetry_.thread_cpu_time_100ns =
                cpu_end - call_cpu_start_;
        last_call_telemetry_.heap_metadata_generation_advances =
            GuestHeap::MetadataGenerationAdvanceDelta(
                call_heap_metadata_start_,
                GuestHeap::GlobalMetadataGenerationAdvanceCount());
        if (!full_call_telemetry_active_) {
            call_telemetry_active_ = false;
            full_call_telemetry_active_ = false;
            svc_category_telemetry_active_ = false;
            return;
        }
        const char* result = succeeded ? "success" : "failure";
        std::printf(
            "ISOMR_AARCH64_CALL_TELEMETRY result=%s halt=0x%llx wall-ns=%llu cpu-available=%u cpu-100ns=%llu ticks=%llu runs=%llu code-reads=%llu scalar-reads=%llu vector-reads=%llu writes=%llu exclusive-writes=%llu svc=%llu svc-wall-ns=%llu cntpct=%llu heap-reads=%llu module-reads=%llu heap-writes=%llu module-writes=%llu heap-read-cache-hits=%llu heap-read-cache-validations=%llu heap-write-cache-hits=%llu heap-write-cache-validations=%llu heap-metadata-advances=%llu memory-samples=%llu memory-sample-wall-ns=%llu\n",
            result,
            static_cast<unsigned long long>(
                last_call_telemetry_.halt_reason),
            static_cast<unsigned long long>(
                last_call_telemetry_.wall_time_ns),
            last_call_telemetry_.thread_cpu_time_available ? 1U : 0U,
            static_cast<unsigned long long>(
                last_call_telemetry_.thread_cpu_time_100ns),
            static_cast<unsigned long long>(
                last_call_telemetry_.translated_ticks),
            static_cast<unsigned long long>(
                last_call_telemetry_.jit_run_invocations),
            static_cast<unsigned long long>(
                last_call_telemetry_.memory_read_code_invocations),
            static_cast<unsigned long long>(
                last_call_telemetry_.scalar_read_invocations),
            static_cast<unsigned long long>(
                last_call_telemetry_.vector_read_invocations),
            static_cast<unsigned long long>(
                last_call_telemetry_.write_invocations),
            static_cast<unsigned long long>(
                last_call_telemetry_.exclusive_write_invocations),
            static_cast<unsigned long long>(
                last_call_telemetry_.svc_invocations),
            static_cast<unsigned long long>(
                last_call_telemetry_.svc_wall_time_ns),
            static_cast<unsigned long long>(
                last_call_telemetry_.cntpct_invocations),
            static_cast<unsigned long long>(
                last_call_telemetry_.heap_read_invocations),
            static_cast<unsigned long long>(
                last_call_telemetry_.module_read_invocations),
            static_cast<unsigned long long>(
                last_call_telemetry_.heap_write_invocations),
            static_cast<unsigned long long>(
                last_call_telemetry_.module_write_invocations),
            static_cast<unsigned long long>(
                last_call_telemetry_.heap_read_cache_hits),
            static_cast<unsigned long long>(
                last_call_telemetry_.heap_read_cache_validations),
            static_cast<unsigned long long>(
                last_call_telemetry_.heap_write_cache_hits),
            static_cast<unsigned long long>(
                last_call_telemetry_.heap_write_cache_validations),
            static_cast<unsigned long long>(
                last_call_telemetry_.heap_metadata_generation_advances),
            static_cast<unsigned long long>(
                last_call_telemetry_.memory_callback_timing_samples),
            static_cast<unsigned long long>(
                last_call_telemetry_.memory_callback_sample_wall_time_ns));
        call_telemetry_active_ = false;
        full_call_telemetry_active_ = false;
        svc_category_telemetry_active_ = false;
    }

    std::optional<std::chrono::steady_clock::time_point>
    BeginMemoryCallback(bool read, std::uint64_t address) {
        if (!full_call_telemetry_active_) return std::nullopt;
        const bool heap = heap_->ContainsAddress(address);
        if (read) {
            if (heap)
                ++last_call_telemetry_.heap_read_invocations;
            else
                ++last_call_telemetry_.module_read_invocations;
        } else if (heap) {
            ++last_call_telemetry_.heap_write_invocations;
        } else {
            ++last_call_telemetry_.module_write_invocations;
        }
        ++memory_callback_ordinal_;
        if ((memory_callback_ordinal_ & 1023U) != 0) return std::nullopt;
        ++last_call_telemetry_.memory_callback_timing_samples;
        return std::chrono::steady_clock::now();
    }

    std::optional<std::chrono::steady_clock::time_point>
    BeginWriteCallback(std::uint64_t address, bool exclusive) {
        if (full_call_telemetry_active_) {
            ++last_call_telemetry_.write_invocations;
            if (exclusive)
                ++last_call_telemetry_.exclusive_write_invocations;
        }
        return BeginMemoryCallback(false, address);
    }

    void FinishMemoryCallback(
        const std::optional<std::chrono::steady_clock::time_point>& start) {
        if (!start) return;
        last_call_telemetry_.memory_callback_sample_wall_time_ns +=
            static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() - *start)
                    .count());
    }

    void FinishSvcTelemetry(
        const std::optional<std::chrono::steady_clock::time_point>& start,
        Aarch64SvcCategory category,
        std::optional<std::string_view> import_name,
        std::uint32_t immediate) {
        if (!start) return;
        const std::uint64_t wall_time_ns = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - *start)
                .count());
        last_call_telemetry_.svc_wall_time_ns += wall_time_ns;
        if (svc_category_telemetry_active_) {
            Aarch64SvcCategoryTelemetry& bucket =
                last_call_telemetry_.svc_categories[
                    static_cast<std::size_t>(category)];
            ++bucket.invocations;
            bucket.wall_time_ns += wall_time_ns;
            if (import_name)
                AccumulateAarch64SvcImportTelemetry(
                    &last_call_telemetry_, immediate, wall_time_ns);
        }
    }

    struct ReadTrace {
        std::uint64_t address = 0;
        std::uint64_t value = 0;
        std::size_t width = 0;
        std::uint64_t program_counter = 0;
        std::uint64_t link_register = 0;
        std::uint64_t stack_pointer = 0;
        std::uint64_t frame_pointer = 0;
    };

    struct WriteTrace {
        std::uint64_t address = 0;
        std::uint64_t value = 0;
        std::size_t width = 0;
        std::uint64_t program_counter = 0;
    };

    std::string DescribeExceptionTarget(std::uint64_t address) const {
        if (address == 0) return "null";
        if ((address & 3U) != 0) return "unaligned";
        std::uint32_t instruction = 0;
        if (imports_->ReadCode(address, &instruction, sizeof(instruction)))
            return "import-cell";
        if (modules_->ReadCode(address, &instruction, sizeof(instruction)))
            return "module-code";
        std::uint8_t byte = 0;
        if (modules_->Read(address, &byte, sizeof(byte)))
            return "module-nonexecute";
        if (heap_->ContainsAddress(address)) return "guest-heap";
        if (address == kTerminalAddress) return "terminal";
        return "unmapped";
    }

    std::string DescribeDataAddress(std::uint64_t address) const {
        if (address >= stack_ && address < stack_ + stack_size_)
            return "guest-stack";
        if (heap_->ContainsAddress(address)) return "guest-heap";
        std::uint8_t byte = 0;
        if (modules_->Read(address, &byte, sizeof(byte))) return "module-data";
        return "unmapped";
    }

    std::string DescribeModuleAddress(std::uint64_t address) const {
        for (const Elf64LoadedModule& loaded :
             modules_->LoadedModulesRootFirst()) {
            if (loaded.image == nullptr) continue;
            const std::uint64_t base = loaded.image->guest_base();
            if (address < base ||
                address - base >= loaded.image->mapped_size())
                continue;
            return loaded.name + "+" + HexAddress(address - base);
        }
        return DescribeDataAddress(address);
    }

    std::string DescribeCodeAddress(std::uint64_t address) const {
        for (const Elf64LoadedModule& loaded :
             modules_->LoadedModulesRootFirst()) {
            if (loaded.image == nullptr) continue;
            const std::uint64_t base = loaded.image->guest_base();
            if (address < base ||
                address - base >= loaded.image->mapped_size())
                continue;
            return loaded.name + "+" + HexAddress(address - base);
        }
        return DescribeExceptionTarget(address);
    }

    std::string DescribeCodeWindow(std::uint64_t address) const {
        std::ostringstream description;
        const std::uint64_t start = address >= 16 ? address - 16 : 0;
        for (std::size_t index = 0; index < 37; ++index) {
            std::uint32_t instruction = 0;
            const std::uint64_t current = start + index * 4;
            if (!modules_->ReadCode(current, &instruction,
                                    sizeof(instruction)))
                continue;
            if (!description.str().empty()) description << ',';
            description << (current == address ? '*' : '-') << std::hex
                        << instruction << std::dec;
        }
        return description.str().empty() ? "unavailable"
                                         : description.str();
    }

    std::string DescribeLiveRegisterMatch(std::uint64_t value) const {
        for (std::size_t index = 0; index < 31; ++index) {
            if (jit_->GetRegister(index) == value)
                return "live-x" + std::to_string(index);
        }
        return jit_->GetSP() == value ? "live-sp" : "none";
    }

    std::string DescribeNullReadChain(std::uint64_t return_address) const {
        if (return_address < 8 || read_trace_count_ == 0)
            return "unavailable";
        std::uint32_t branch = 0;
        if (!modules_->ReadCode(return_address - 4, &branch,
                                sizeof(branch)) ||
            (branch & 0xfffffc1fU) != 0xd63f0000U)
            return "not-indirect-link";
        const std::size_t source = (branch >> 5) & 31U;
        std::optional<std::uint64_t> field_offset;
        std::optional<std::size_t> field_base_register;
        for (std::uint64_t distance = 8; distance <= 80; distance += 4) {
            std::uint32_t instruction = 0;
            if (!modules_->ReadCode(return_address - distance, &instruction,
                                    sizeof(instruction)))
                break;
            if ((instruction & 31U) != source) continue;
            if ((instruction & 0xffc00000U) == 0xf9400000U) {
                field_offset =
                    static_cast<std::uint64_t>((instruction >> 10) & 0xfffU) *
                    8U;
                field_base_register =
                    static_cast<std::size_t>((instruction >> 5) & 31U);
            }
            break;
        }
        if (!field_offset) return "producer-not-unsigned-load64";
        const std::size_t newest =
            (read_trace_next_ + read_traces_.size() - 1) %
            read_traces_.size();
        const ReadTrace& producer = read_traces_[newest];
        if (producer.width != sizeof(std::uint64_t) ||
            producer.value != jit_->GetRegister(source) ||
            producer.address < *field_offset)
            return "latest-read-does-not-match-producer";
        const std::uint64_t base = producer.address - *field_offset;
        std::string description =
            std::string("producer-read64-") +
            (producer.value == 0 ? "zero" : "nonzero") + "-pc" +
            HexAddress(producer.program_counter) + "-lr" +
            HexAddress(producer.link_register) + "-sp" +
            HexAddress(producer.stack_pointer) + "-fp" +
            HexAddress(producer.frame_pointer) + "-address" +
            HexAddress(producer.address) + "-base" + HexAddress(base) +
            "-load-target-x" + std::to_string(source) +
            "-load-base-" +
            (*field_base_register == 31
                 ? std::string("sp")
                 : "x" + std::to_string(*field_base_register)) +
            "-from-" +
            DescribeDataAddress(producer.address) + "-offset" +
            std::to_string(*field_offset) + "-target-range-" +
            heap_->DescribeRange(producer.address, sizeof(std::uint64_t)) +
            "-object-range-" +
            heap_->DescribeRange(
                base, static_cast<std::size_t>(*field_offset) +
                          sizeof(std::uint64_t));
        description += DescribeLastWrite(producer.address);
        const std::size_t available =
            std::min(read_trace_count_, read_traces_.size());
        for (std::size_t age = 1; age < available; ++age) {
            const std::size_t index =
                (read_trace_next_ + read_traces_.size() - 1 - age) %
                read_traces_.size();
            const ReadTrace& candidate = read_traces_[index];
            if (candidate.width == sizeof(std::uint64_t) &&
                candidate.value == base) {
                return description + "-base-from-prior-read64-back" +
                       std::to_string(age) + "-prior-address-" +
                       DescribeDataAddress(candidate.address) +
                       "-base-register-" +
                       DescribeLiveRegisterMatch(base);
            }
        }
        return description + "-base-source-not-recent-base-register-" +
               DescribeLiveRegisterMatch(base);
    }

    std::string DescribeLastWrite(std::uint64_t address) const {
        const std::size_t available =
            std::min(write_trace_count_, write_traces_.size());
        for (std::size_t age = 0; age < available; ++age) {
            const std::size_t index =
                (write_trace_next_ + write_traces_.size() - 1 - age) %
                write_traces_.size();
            const WriteTrace& trace = write_traces_[index];
            if (trace.address != address ||
                trace.width != sizeof(std::uint64_t))
                continue;
            std::uint32_t instruction = 0;
            if (!modules_->ReadCode(trace.program_counter, &instruction,
                                    sizeof(instruction)))
                return "-last-write64-code-unreadable";
            if ((instruction & 0xffc00000U) == 0xf9000000U) {
                const std::size_t source = instruction & 31U;
                const std::size_t base = (instruction >> 5) & 31U;
                const std::uint64_t offset =
                    static_cast<std::uint64_t>((instruction >> 10) & 0xfffU) *
                    sizeof(std::uint64_t);
                return "-last-write64-" +
                       std::string(trace.value == 0 ? "zero" : "nonzero") +
                       "-pc" + HexAddress(trace.program_counter) +
                       "-instruction-str-x" + std::to_string(source) +
                       "-base-" +
                       (base == 31 ? std::string("sp")
                                   : "x" + std::to_string(base)) +
                       "-offset" + std::to_string(offset);
            }
            if ((instruction & 0xffe00c00U) == 0xf8000000U) {
                const std::size_t source = instruction & 31U;
                const std::size_t base = (instruction >> 5) & 31U;
                std::int64_t offset =
                    static_cast<std::int64_t>((instruction >> 12) & 0x1ffU);
                if ((offset & 0x100) != 0) offset -= 0x200;
                return "-last-write64-" +
                       std::string(trace.value == 0 ? "zero" : "nonzero") +
                       "-pc" + HexAddress(trace.program_counter) +
                       "-instruction-stur-x" + std::to_string(source) +
                       "-base-" +
                       (base == 31 ? std::string("sp")
                                   : "x" + std::to_string(base)) +
                       "-offset" + std::to_string(offset);
            }
            const std::uint32_t pair_class = instruction & 0xffc00000U;
            if (pair_class == 0xa8000000U ||
                pair_class == 0xa8800000U ||
                pair_class == 0xa9000000U ||
                pair_class == 0xa9800000U) {
                const std::size_t first_source = instruction & 31U;
                const std::size_t base = (instruction >> 5) & 31U;
                const std::size_t second_source =
                    (instruction >> 10) & 31U;
                std::int64_t offset =
                    static_cast<std::int64_t>((instruction >> 15) & 0x7fU);
                if ((offset & 0x40) != 0) offset -= 0x80;
                offset *= static_cast<std::int64_t>(sizeof(std::uint64_t));
                const char* mode =
                    pair_class == 0xa8000000U ? "non-temporal"
                    : pair_class == 0xa8800000U ? "post-index"
                    : pair_class == 0xa9800000U ? "pre-index"
                                                : "offset";
                return "-last-write64-" +
                       std::string(trace.value == 0 ? "zero" : "nonzero") +
                       "-pc" + HexAddress(trace.program_counter) +
                       "-instruction-stp-x" +
                       std::to_string(first_source) + "-x" +
                       std::to_string(second_source) + "-base-" +
                       (base == 31 ? std::string("sp")
                                   : "x" + std::to_string(base)) +
                       "-mode-" + mode + "-offset" +
                       std::to_string(offset);
            }
            if ((instruction & 0xffe00c00U) == 0xf8200800U) {
                const std::size_t source = instruction & 31U;
                const std::size_t base = (instruction >> 5) & 31U;
                const std::size_t offset_register =
                    (instruction >> 16) & 31U;
                return "-last-write64-" +
                       std::string(trace.value == 0 ? "zero" : "nonzero") +
                       "-pc" + HexAddress(trace.program_counter) +
                       "-instruction-str-x" + std::to_string(source) +
                       "-base-" +
                       (base == 31 ? std::string("sp")
                                   : "x" + std::to_string(base)) +
                       "-register-offset-x" +
                       std::to_string(offset_register);
            }
            return "-last-write64-" +
                   std::string(trace.value == 0 ? "zero" : "nonzero") +
                   "-block-pc" + HexAddress(trace.program_counter) +
                   "-instruction-unclassified" +
                   DescribeNearbyZeroStores(trace.program_counter, address);
        }
        return "-last-write64-not-in-current-call";
    }

    std::string DescribeNearbyZeroStores(std::uint64_t block_pc,
                                         std::uint64_t address) const {
        std::string preferred;
        std::string candidates;
        std::size_t candidate_count = 0;
        for (std::int64_t delta = -64; delta <= 128; delta += 4) {
            if (delta < 0 &&
                block_pc < static_cast<std::uint64_t>(-delta))
                continue;
            const std::uint64_t pc =
                delta < 0 ? block_pc - static_cast<std::uint64_t>(-delta)
                          : block_pc + static_cast<std::uint64_t>(delta);
            std::uint32_t instruction = 0;
            if (!modules_->ReadCode(pc, &instruction, sizeof(instruction)))
                continue;
            std::optional<std::uint64_t> inferred_base;
            std::string classification;
            if ((instruction & 0xffc0001fU) == 0xf900001fU) {
                const std::size_t base = (instruction >> 5) & 31U;
                const std::uint64_t offset =
                    static_cast<std::uint64_t>((instruction >> 10) & 0xfffU) *
                    sizeof(std::uint64_t);
                if (address >= offset) inferred_base = address - offset;
                classification = "str-xzr-base-" +
                    (base == 31 ? std::string("sp")
                                : "x" + std::to_string(base)) +
                    "-offset" + std::to_string(offset);
            } else if ((instruction & 0xffe00c1fU) == 0xf800001fU) {
                const std::size_t base = (instruction >> 5) & 31U;
                std::int64_t offset =
                    static_cast<std::int64_t>((instruction >> 12) & 0x1ffU);
                if ((offset & 0x100) != 0) offset -= 0x200;
                if (offset >= 0 &&
                    address >= static_cast<std::uint64_t>(offset))
                    inferred_base =
                        address - static_cast<std::uint64_t>(offset);
                else if (offset < 0 &&
                         address <= std::numeric_limits<std::uint64_t>::max() -
                                        static_cast<std::uint64_t>(-offset))
                    inferred_base =
                        address + static_cast<std::uint64_t>(-offset);
                classification = "stur-xzr-base-" +
                    (base == 31 ? std::string("sp")
                                : "x" + std::to_string(base)) +
                    "-offset" + std::to_string(offset);
            }
            if (classification.empty()) continue;
            ++candidate_count;
            std::string candidate =
                "-candidate" + std::to_string(candidate_count - 1) +
                "-pc" + HexAddress(pc) + "-delta" +
                std::to_string(delta) + "-" + classification;
            if (inferred_base) {
                const std::string range =
                    heap_->DescribeRange(*inferred_base,
                                         sizeof(std::uint64_t));
                candidate += "-inferred-base" +
                             HexAddress(*inferred_base) + "-" + range;
                if (range.find("range-offset0-range-size8") !=
                    std::string::npos)
                    preferred = candidate;
            }
            if (candidate_count <= 8) candidates += candidate;
        }
        return "-nearby-zero-store-count" +
               std::to_string(candidate_count) +
               (preferred.empty() ? "-preferred-none"
                                  : "-preferred" + preferred) +
               candidates;
    }

    std::string DescribeNullBranch(std::uint64_t return_address) const {
        if (return_address < 4) return "return-address-invalid";
        std::uint32_t instruction = 0;
        if (!modules_->ReadCode(return_address - 4, &instruction,
                                sizeof(instruction)))
            return "link-predecessor-unavailable";
        if ((instruction & 0xfffffc1fU) == 0xd63f0000U) {
            const std::size_t source = (instruction >> 5) & 31U;
            std::string predecessor = "unavailable";
            std::uint32_t previous = 0;
            if (return_address >= 8 &&
                modules_->ReadCode(return_address - 8, &previous,
                                   sizeof(previous)))
                predecessor = DescribeInstruction(previous);
            return "blr-x" + std::to_string(source) + "-live-" +
                   DescribeExceptionTarget(jit_->GetRegister(source)) +
                   "-previous-" + predecessor + "-producer-" +
                   DescribeRegisterProducer(return_address, source);
        }
        return std::string("link-predecessor-") +
               InstructionClass(instruction);
    }

    std::string DescribeInstruction(std::uint32_t instruction) const {
        const std::size_t destination = instruction & 31U;
        const std::size_t base = (instruction >> 5) & 31U;
        if ((instruction & 0xffe0ffe0U) == 0xaa0003e0U)
            return "move-x" + std::to_string(destination) + "-from-x" +
                   std::to_string((instruction >> 16) & 31U);
        if ((instruction & 0xffc00000U) == 0xf9400000U)
            return "load-x" + std::to_string(destination) + "-from-x" +
                   std::to_string(base);
        if ((instruction & 0xffc00000U) == 0xf9000000U)
            return "store-x" + std::to_string(destination) + "-to-x" +
                   std::to_string(base);
        if ((instruction & 0xff000000U) == 0x91000000U) {
            std::uint64_t immediate = (instruction >> 10) & 0xfffU;
            if ((instruction & (1U << 22)) != 0) immediate <<= 12;
            return "add-x" + std::to_string(destination) + "-from-x" +
                   std::to_string(base) + "-immediate" +
                   std::to_string(immediate);
        }
        if ((instruction & 0xff000000U) == 0xd1000000U)
            return "sub-x" + std::to_string(destination) + "-from-x" +
                   std::to_string(base);
        return InstructionClass(instruction);
    }

    std::string DescribeX0Producer(std::uint64_t return_address) const {
        return DescribeRegisterProducer(return_address, 0);
    }

    std::string DescribeRegisterProducer(std::uint64_t return_address,
                                         std::size_t target) const {
        if (return_address < 8) return "return-address-invalid";
        std::string chain;
        for (std::uint64_t distance = 8; distance <= 80; distance += 4) {
            std::uint32_t instruction = 0;
            if (!modules_->ReadCode(return_address - distance, &instruction,
                                    sizeof(instruction)))
                return "code-unavailable";
            if ((instruction & 0xffe0ffe0U) == 0xaa0003e0U &&
                (instruction & 31U) == target) {
                const std::size_t source = (instruction >> 16) & 31U;
                chain += "move-x" + std::to_string(source) + "<-";
                if (source == 31) return chain + "zero-register";
                target = source;
                continue;
            }
            if ((instruction & 0xffc00000U) == 0xf9400000U &&
                (instruction & 31U) == target) {
                const std::size_t base = (instruction >> 5) & 31U;
                const std::uint64_t offset =
                    static_cast<std::uint64_t>((instruction >> 10) & 0xfffU) *
                    8U;
                const std::uint64_t base_value =
                    base == 31 ? jit_->GetSP() : jit_->GetRegister(base);
                std::uint64_t value = 0;
                if (base_value >
                        std::numeric_limits<std::uint64_t>::max() - offset ||
                    !ReadMemory(base_value + offset, &value, sizeof(value)))
                    return chain + "load-unsigned-from-x" +
                           std::to_string(base) + "-offset" +
                           std::to_string(offset) + "-back" +
                           std::to_string(distance) + "-address-invalid";
                return chain + "load-unsigned-from-x" +
                       std::to_string(base) + "-offset" +
                       std::to_string(offset) + "-back" +
                       std::to_string(distance) + "-base-" +
                       DescribeExceptionTarget(base_value) +
                       (value == 0 ? "-current-field-null"
                                   : "-current-field-nonnull");
            }
            if ((instruction & 0xffe00000U) == 0xf8400000U &&
                (instruction & 31U) == target)
                return chain + "load-indexed-field";
            if ((instruction & 0xff000000U) == 0x91000000U &&
                (instruction & 31U) == target)
                return chain + "add-immediate";
            if ((instruction & 0xff000000U) == 0xd1000000U &&
                (instruction & 31U) == target)
                return chain + "sub-immediate";
            if ((instruction & 0x9f000000U) == 0x10000000U &&
                (instruction & 31U) == target)
                return chain + "address-immediate";
            if ((instruction & 0xff000000U) == 0x58000000U &&
                (instruction & 31U) == target)
                return chain + "load-literal";
            if ((instruction & 0x7f800000U) == 0x52800000U &&
                (instruction & 31U) == target)
                return chain + "move-immediate";
            if (target <= 17 &&
                (instruction & 0xfc000000U) == 0x94000000U) {
                std::int64_t displacement = instruction & 0x03ffffffU;
                if ((displacement & 0x02000000) != 0)
                    displacement -= 0x04000000;
                displacement *= 4;
                const std::uint64_t instruction_address =
                    return_address - distance;
                const std::uint64_t function =
                    displacement < 0
                        ? instruction_address -
                              static_cast<std::uint64_t>(-displacement)
                        : instruction_address +
                              static_cast<std::uint64_t>(displacement);
                std::ostringstream description;
                description << chain;
                if (target == 0) {
                    description << "function-return-target0x";
                } else {
                    description << "call-clobbered-x" << target
                                << "-after-target0x";
                }
                description << std::hex << function;
                return description.str();
            }
            if (target <= 17 &&
                (instruction & 0xfffffc1fU) == 0xd63f0000U)
                return chain + "indirect-function-return";
        }
        return chain.empty() ? "unclassified" : chain + "origin-unclassified";
    }

    std::string DescribeHeapRegisters() const {
        std::ostringstream description;
        std::size_t reported = 0;
        for (std::size_t index = 19; index <= 28 && reported < 8; ++index) {
            const std::uint64_t address = jit_->GetRegister(index);
            if (!heap_->ContainsAddress(address)) continue;
            std::uint32_t nonzero_mask = 0;
            std::array<std::uint64_t, 24> values{};
            std::size_t words = 0;
            for (; words < 24; ++words) {
                if (!heap_->Read(address + words * sizeof(values[words]),
                                 &values[words], sizeof(values[words])))
                    break;
                if (values[words] != 0) nonzero_mask |= 1U << words;
            }
            if (reported != 0) description << ',';
            description << 'x' << index << "-words" << words << "-nz0x"
                        << std::hex << nonzero_mask;
            for (std::size_t word = 0; word < words; ++word)
                description << "-w" << std::dec << word << "=0x"
                            << std::hex << values[word];
            if (index == 19 || index == 20 || index == 24) {
                for (std::size_t word = 0; word < words; ++word) {
                    if (!heap_->ContainsAddress(values[word]) ||
                        heap_->DescribeRange(values[word], 1) ==
                            "not-active-allocation")
                        continue;
                    description << "-w" << std::dec << word << "-pointee-"
                                << heap_->DescribeRange(values[word], 1);
                    for (std::size_t head = 0; head < 8; ++head) {
                        std::uint64_t value = 0;
                        if (!heap_->Read(values[word] + head * sizeof(value),
                                         &value, sizeof(value)))
                            break;
                        description << "-h" << std::dec << head << "=0x"
                                    << std::hex << value;
                    }
                }
            }
            description << std::dec;
            ++reported;
        }
        return reported == 0 ? "none" : description.str();
    }

    std::string DescribeHeapRegisterRanges() const {
        std::ostringstream description;
        std::size_t reported = 0;
        for (std::size_t index = 19; index <= 28 && reported < 8; ++index) {
            const std::uint64_t address = jit_->GetRegister(index);
            if (!heap_->ContainsAddress(address)) continue;
            if (reported != 0) description << ',';
            description << 'x' << index << '='
                        << heap_->DescribeRange(address, 1);
            ++reported;
        }
        return reported == 0 ? "none" : description.str();
    }

    std::string DescribeFrameChain() const {
        std::ostringstream description;
        std::uint64_t frame = jit_->GetRegister(29);
        std::size_t count = 0;
        while (count < 12 && (frame & 15U) == 0 && frame >= stack_ &&
               frame <= stack_ + stack_size_ - 16) {
            std::array<std::uint64_t, 2> record{};
            if (!heap_->Read(frame, record.data(), sizeof(record))) break;
            if (count != 0) description << ',';
            description << "0x" << std::hex << record[1] << std::dec;
            ++count;
            if (record[0] <= frame || record[0] > stack_ + stack_size_ - 16)
                break;
            frame = record[0];
        }
        return count == 0 ? "none" : description.str();
    }

    std::string DescribeGeneralRegisters() const {
        std::ostringstream description;
        for (std::size_t index = 0; index < 31; ++index) {
            if (index != 0) description << ',';
            description << 'x' << std::dec << index << "=0x" << std::hex
                        << jit_->GetRegister(index) << std::dec;
        }
        return description.str();
    }

    std::string DescribeRegister19ObjectTail() const {
        if (jit_ == nullptr) return "unavailable";
        const std::uint64_t object = jit_->GetRegister(19);
        std::ostringstream description;
        // Resolve the last-blur callee: callers do ldr x8,[x19];
        // ldr x8,[x8,#0x18]; blr x8 — report the vtable slot target so
        // sentinel-returning callees can be named.
        std::uint64_t vtable = 0;
        if (heap_->Read(object, &vtable, sizeof(vtable))) {
            description << "vt=" << DescribeModuleAddress(vtable);
            std::uint64_t slot = 0;
            if (modules_->Read(vtable + 0x18, &slot, sizeof(slot)) ||
                heap_->Read(vtable + 0x18, &slot, sizeof(slot))) {
                description << " vt3=" << DescribeModuleAddress(slot);
            }
            description << " ";
        }
        for (std::size_t index = 0; index < 9; ++index) {
            std::uint64_t value = 0;
            if (!heap_->Read(object + 0x100 + index * sizeof(value),
                             &value, sizeof(value)))
                return "unavailable";
            if (index != 0) description << ',';
            description << "+0x" << std::hex
                        << 0x100 + index * sizeof(value) << "=0x"
                        << value << std::dec;
        }
        return description.str();
    }

    std::string DescribeStackWords() const {
        std::ostringstream description;
        const std::uint64_t stack_pointer = jit_->GetSP();
        for (std::size_t index = 0; index < 32; ++index) {
            std::uint64_t value = 0;
            if (!heap_->Read(stack_pointer + index * sizeof(value), &value,
                             sizeof(value)))
                break;
            if (index != 0) description << ',';
            description << index << "=0x" << std::hex << value << std::dec;
        }
        return description.str().empty() ? "none" : description.str();
    }

    // Leaf-style frames (stp x29,x30,[sp,#N] without mov x29,sp) leave no fp
    // chain, so scan the live stack for words that land in a loaded module's
    // code range: each is a candidate return address, giving a best-effort
    // call-stack reconstruction for null-object faults.
    std::string DescribeStackReturnSites() const {
        std::ostringstream description;
        const std::uint64_t stack_pointer = jit_->GetSP();
        std::size_t count = 0;
        for (std::size_t index = 0; index < 256 && count < 12; ++index) {
            std::uint64_t value = 0;
            if (!heap_->Read(stack_pointer + index * sizeof(value), &value,
                             sizeof(value)))
                break;
            std::uint32_t instruction = 0;
            if (value < 4 ||
                !modules_->ReadCode(value - 4, &instruction,
                                    sizeof(instruction)))
                continue;
            if (count != 0) description << ',';
            description << DescribeCodeAddress(value);
            ++count;
        }
        return count == 0 ? "none" : description.str();
    }

    // Dump the guest general registers that matter for a null-operand
    // write fault: the arg/temporaries (x0-x8), callee-saved pointers
    // (x19-x22), frame/link and stack, so the faulted address and the
    // pointers feeding it are visible in one abort line.
    std::string DescribeFaultRegisters() const {
        std::ostringstream description;
        description << std::hex;
        for (int index = 0; index <= 8; ++index)
            description << " x" << index << "=" << jit_->GetRegister(index);
        for (int index : {19, 20, 21, 22, 28, 29, 30})
            description << " x" << index << "=" << jit_->GetRegister(index);
        description << " sp=" << jit_->GetSP();
        // Dereference each guest-space pointer register so the fault line
        // shows which object slot actually holds the null that propagated.
        for (int index : {0, 1, 6, 7, 19, 20, 21, 22, 29}) {
            const std::uint64_t reg = jit_->GetRegister(index);
            if (reg == 0) continue;
            std::uint64_t target = 0;
            if (heap_->Read(reg, &target, sizeof(target)) && target != 0)
                description << " *x" << index << "=" << target;
        }
        return description.str();
    }

    template <typename T>
    T Read(std::uint64_t address) {
        T value{};
        std::uint64_t watched_generation = 0;
        const bool read = forensic_memory_callbacks_
                              ? ReadMemory(address, &value, sizeof(value),
                                           &watched_generation)
                              : ReadFastMemory(address, &value,
                                               sizeof(value));
        if (!read) {
            std::string import_cell_name;
            if (imports_ != nullptr && address >= imports_->guest_base() &&
                address < imports_->guest_base() + imports_->code_size()) {
                const std::uint64_t cell_index =
                    (address - imports_->guest_base()) / 8;
                const std::optional<std::string_view> resolved =
                    imports_->NameForImmediate(
                        static_cast<std::uint32_t>(cell_index + 1));
                if (resolved.has_value()) {
                    import_cell_name =
                        " import-cell-name=" + std::string(*resolved);
                }
            }
            Abort("guest read touched an invalid range address=" +
                  HexAddress(address) + " size=" +
                  std::to_string(sizeof(T)) + " pc=" +
                  HexAddress(jit_ != nullptr ? jit_->GetPC() : 0) +
                  " pc-module=" +
                  (jit_ != nullptr
                       ? DescribeCodeAddress(jit_->GetPC())
                       : std::string("none")) +
                  " code=" +
                  (jit_ != nullptr
                       ? DescribeCodeWindow(jit_->GetPC())
                       : std::string("unavailable")) +
                  " lr=" +
                  HexAddress(jit_ != nullptr
                                 ? jit_->GetRegister(30)
                                 : 0) +
                  " source=" + DescribeDataAddress(address) +
                  import_cell_name +
                  " register-match=" +
                  (jit_ != nullptr
                       ? DescribeLiveRegisterMatch(address)
                       : std::string("none")) +
                  " registers=" +
                  (jit_ != nullptr ? DescribeGeneralRegisters()
                                   : std::string("none")) +
                  " x19-tail=" +
                  (jit_ != nullptr
                       ? DescribeRegister19ObjectTail()
                       : std::string("unavailable")) +
                  " frame-chain=" +
                  (jit_ != nullptr ? DescribeFrameChain()
                                   : std::string("none")));
        } else {
            if (forensic_memory_callbacks_) {
                CaptureMemoryAccess(Aarch64MemoryCaptureAccess::kRead,
                                    address, &value, sizeof(value),
                                    watched_generation);
                if constexpr (sizeof(T) <= sizeof(std::uint64_t)) {
                    ReadTrace& trace = read_traces_[read_trace_next_];
                    trace.address = address;
                    trace.value = 0;
                    trace.width = sizeof(T);
                    trace.program_counter =
                        jit_ != nullptr ? jit_->GetPC() : 0;
                    trace.link_register =
                        jit_ != nullptr ? jit_->GetRegister(30) : 0;
                    trace.stack_pointer =
                        jit_ != nullptr ? jit_->GetSP() : 0;
                    trace.frame_pointer =
                        jit_ != nullptr ? jit_->GetRegister(29) : 0;
                    std::memcpy(&trace.value, &value, sizeof(T));
                    read_trace_next_ =
                        (read_trace_next_ + 1) % read_traces_.size();
                    if (read_trace_count_ < read_traces_.size())
                        ++read_trace_count_;
                }
            }
            last_read_heap_ = heap_->ContainsAddress(address);
            const std::array<std::uint8_t, sizeof(T)> zero{};
            last_read_zero_ =
                std::memcmp(&value, zero.data(), sizeof(value)) == 0;
        }
        return value;
    }
    template <typename T>
    void Write(std::uint64_t address, T value) {
        std::uint64_t watched_generation = 0;
        const bool written =
            forensic_memory_callbacks_
                ? WriteMemory(address, &value, sizeof(value),
                              &watched_generation)
                : WriteFastMemory(address, &value, sizeof(value));
        if (!written) {
            Abort("guest write touched an invalid range address=" +
                  HexAddress(address) + " size=" +
                  std::to_string(sizeof(T)) + " pc=" +
                  HexAddress(jit_ != nullptr ? jit_->GetPC() : 0) +
                  " pc-module=" +
                  (jit_ != nullptr
                       ? DescribeCodeAddress(jit_->GetPC())
                       : std::string("none")) +
                  " frame-chain=" +
                  (jit_ != nullptr ? DescribeFrameChain()
                                   : std::string("none")) +
                  " return-sites=" +
                  (jit_ != nullptr ? DescribeStackReturnSites()
                                   : std::string("none")) +
                  " regs=" +
                  (jit_ != nullptr ? DescribeFaultRegisters()
                                   : std::string("none")));
        } else if (forensic_memory_callbacks_) {
            CaptureMemoryAccess(Aarch64MemoryCaptureAccess::kWrite, address,
                                &value, sizeof(value), watched_generation);
            if constexpr (sizeof(T) <= sizeof(std::uint64_t)) {
                WriteTrace& trace = write_traces_[write_trace_next_];
                trace = {};
                trace.address = address;
                trace.width = sizeof(T);
                trace.program_counter =
                    jit_ != nullptr ? jit_->GetPC() : 0;
                std::memcpy(&trace.value, &value, sizeof(T));
                write_trace_next_ =
                    (write_trace_next_ + 1) % write_traces_.size();
                if (write_trace_count_ < write_traces_.size())
                    ++write_trace_count_;
            }
        }
    }
    void CaptureMemoryAccess(Aarch64MemoryCaptureAccess access,
                             std::uint64_t address, const void* value,
                             std::size_t size,
                             std::uint64_t watched_generation) {
        if (memory_capture_pending_ || value == nullptr || size == 0 ||
            memory_capture_config_.access != access ||
            memory_capture_config_.watch_generation == 0 ||
            memory_capture_config_.watch_generation != watched_generation)
            return;
        ++memory_capture_match_count_;
        if (memory_capture_match_count_ != memory_capture_config_.occurrence)
            return;
        memory_access_capture_ = {};
        memory_access_capture_.access = access;
        memory_access_capture_.watch_generation = watched_generation;
        memory_access_capture_.occurrence = memory_capture_match_count_;
        memory_access_capture_.address = address;
        memory_access_capture_.size = size;
        std::memcpy(memory_access_capture_.value.data(), value,
                    std::min(size, sizeof(memory_access_capture_.value)));
        memory_capture_pending_ = true;
        if (jit_ != nullptr)
            jit_->HaltExecution(Dynarmic::HaltReason::MemoryAbort);
    }
    template <typename T>
    bool ExclusiveWrite(std::uint64_t address, T value, T expected) {
        T current{};
        if (!ReadMemory(address, &current, sizeof(current)) ||
            current != expected)
            return false;
        if (WriteMemory(address, &value, sizeof(value))) return true;
        Abort("guest exclusive write touched an invalid range");
        return false;
    }
    bool ReadMemory(std::uint64_t address, void* destination,
                    std::size_t size,
                    std::uint64_t* watched_generation = nullptr) const {
        if (watched_generation != nullptr) *watched_generation = 0;
        if (heap_->ContainsAddress(address))
            return heap_->Read(address, destination, size,
                               watched_generation);
        // Null-page reads (partially-initialized render-path pointers)
        // read as zeros so the walk continues.
        if (address < 0x10000ULL) {
            std::memset(destination, 0, size);
            return true;
        }
        // The call-entry terminal address doubles as the resume-PC slot of a
        // captured guest context (Unity fibers snapshot LR into a ucontext-
        // style record). Frame walkers read the saved PC to test for a
        // syscall-stub signature; on a device that slot holds a real
        // trampoline address. Reading zeros answers "not a stub" without
        // faulting the walk. Fetching it still terminates via ReadCode.
        if (address >= kTerminalAddress &&
            address - kTerminalAddress < 0x1000ULL) {
            std::memset(destination, 0, size);
            return true;
        }
        return modules_->Read(address, destination, size);
    }
    bool ReadFastMemory(std::uint64_t address, void* destination,
                        std::size_t size) {
        if (heap_->ContainsAddress(address)) {
            const GuestHeap::AccessSpanResult result =
                heap_->ReadWithAccessSpanCache(
                    address, destination, size, &read_access_cache_);
            if (full_call_telemetry_active_) {
                if (result == GuestHeap::AccessSpanResult::kCacheHit)
                    ++last_call_telemetry_.heap_read_cache_hits;
                else if (result == GuestHeap::AccessSpanResult::kValidated)
                    ++last_call_telemetry_.heap_read_cache_validations;
            }
            if (result == GuestHeap::AccessSpanResult::kFailed) {
                // Committed-but-untracked arena pages (bionic mmap
                // allocations, freed mapping pages kept committed) have no
                // blocks_ entry, so the tracked span path refuses them even
                // though they hold real data. Read it via the untracked
                // committed path before falling back to zero-fill.
                if (heap_->ReadCommittedUntracked(address, destination, size))
                    return true;
                // Null-page reads (pointers of 0/1 + small offsets in the
                // partially-initialized render path) also read as zeros —
                // the same semantics as reading a freshly-zeroed object —
                // so the worker's walk continues instead of aborting the
                // session. Bounded log preserves the evidence.
                if (address < 0x10000ULL) {
                    static std::size_t null_page_reads = 0;
                    if (null_page_reads < 12) {
                        ++null_page_reads;
                        std::uint64_t r[10] = {0}, fp = 0, sp = 0,
                                       s_this_a = 0, s_this_b = 0;
                        if (jit_ != nullptr) {
                            for (int i = 0; i < 9; ++i)
                                r[i] = jit_->GetRegister(i);
                            fp = jit_->GetRegister(29);
                            sp = jit_->GetSP();
                            ReadMemory(sp + 0x10, &s_this_a, 8, nullptr);
                            if (fp != 0)
                                ReadMemory(fp - 0x18, &s_this_b, 8, nullptr);
                        }
                        std::fprintf(
                            stderr,
                            "ISOMR_NULL_PAGE_READ address=%llx size=%zu "
                            "pc=%llx lr=%llx x0=%llx x1=%llx x8=%llx fp=%llx "
                            "sp=%llx sp10=%llx fp18=%llx\n",
                            static_cast<unsigned long long>(address), size,
                            static_cast<unsigned long long>(
                                jit_ != nullptr ? jit_->GetPC() : 0),
                            static_cast<unsigned long long>(
                                jit_ != nullptr ? jit_->GetRegister(30) : 0),
                            static_cast<unsigned long long>(r[0]),
                            static_cast<unsigned long long>(r[1]),
                            static_cast<unsigned long long>(r[8]),
                            static_cast<unsigned long long>(fp),
                            static_cast<unsigned long long>(sp),
                            static_cast<unsigned long long>(s_this_a),
                            static_cast<unsigned long long>(s_this_b));
                        std::fflush(stderr);
                    }
                    std::memset(destination, 0, size);
                    return true;
                }
                // Demand-zero: uncommitted arena pages read as zeros,
                // matching Android mmap semantics for fresh anonymous
                // pages. The wild pointer that produced the access is
                // still visible in the bounded log.
                static std::size_t demand_zero_reads = 0;
                if (demand_zero_reads < 12) {
                    ++demand_zero_reads;
                    // Distinguish a genuinely-unreadable page from a
                    // fast-path cache miss: retry through the uncached
                    // heap_->Read and report the real stored value, plus
                    // the committed/protection state so we can tell a
                    // freed block from a decommitted or deprotected one.
                    std::uint64_t recheck = 0;
                    const bool recheck_ok =
                        heap_ != nullptr &&
                        heap_->Read(address, &recheck,
                                    sizeof(recheck) <= size
                                        ? sizeof(recheck)
                                        : size);
                    const bool committed =
                        heap_ != nullptr &&
                        heap_->ReadCommittedUntracked(address, &recheck, 1);
                    const bool readable =
                        heap_ != nullptr &&
                        heap_->IsReadableRange(address, size);
                    const bool writable =
                        heap_ != nullptr &&
                        heap_->IsWritableRange(address, size);
                    std::fprintf(
                        stderr,
                        "ISOMR_DEMAND_ZERO read address=%llx size=%zu "
                        "pc=%llx lr=%llx recheck_ok=%d recheck_val=%llx "
                        "committed=%d readable=%d writable=%d\n",
                        static_cast<unsigned long long>(address), size,
                        static_cast<unsigned long long>(
                            jit_ != nullptr ? jit_->GetPC() : 0),
                        static_cast<unsigned long long>(
                            jit_ != nullptr ? jit_->GetRegister(30) : 0),
                        recheck_ok ? 1 : 0,
                        static_cast<unsigned long long>(recheck),
                        committed ? 1 : 0, readable ? 1 : 0,
                        writable ? 1 : 0);
                    std::fflush(stderr);
                }
                std::memset(destination, 0, size);
                return true;
            }
            return true;
        }
        // Null-page reads (partially-initialized render-path pointers like
        // Image*=1 dereferenced at +0x10) read as zeros so the worker's
        // walk continues; bounded log preserves the evidence.
        if (address < 0x10000ULL) {
            static std::size_t null_page_module_reads = 0;
            if (null_page_module_reads < 12) {
                ++null_page_module_reads;
                std::uint64_t r[10] = {0}, fp = 0, sp = 0, s_this = 0,
                               s_this_vt = 0;
                const std::uint64_t crash_pc =
                    jit_ != nullptr ? jit_->GetPC() : 0;
                if (jit_ != nullptr) {
                    for (int i = 0; i < 9; ++i) r[i] = jit_->GetRegister(i);
                    fp = jit_->GetRegister(29);
                    sp = jit_->GetSP();
                    // EnqueueAndroidBuffer stores `this` at [sp+0x10] and
                    // [x29-0x18]; both name the Dac. Read [sp+0x10] and its
                    // vtable so the fault identifies the unbuilt member.
                    if (ReadMemory(sp + 0x10, &s_this, 8, nullptr) &&
                        s_this != 0)
                        (void)ReadMemory(s_this, &s_this_vt, 8, nullptr);
                }
                // Diagnostic: the usdk TrackingIdProvider null-store crash
                // (pc ~= game library + 0x4afdee8) happens when the
                // LocalDatabaseManager session store is empty. Dump the
                // usdk File-class static globals (game library +
                // 0x5640630/0x5640638) and the KeyValueStoreManager fields
                // to see whether the FS bridge init ran.
                if (crash_pc >= 0x4004afd000ULL &&
                    crash_pc <= 0x4004b00000ULL && jit_ != nullptr) {
                    std::uint64_t g630 = 0, g638 = 0;
                    ReadMemory(0x4005640630ULL, &g630, 8, nullptr);
                    ReadMemory(0x4005640638ULL, &g638, 8, nullptr);
                    // x19 = the TrackingIdProvider under construction;
                    // x19+0x30 = the store getStore wrote (0 on crash).
                    // x20/x21/x22/x23/x24 may name the provider/manager.
                    std::uint64_t x19 = jit_->GetRegister(19);
                    std::uint64_t x20 = jit_->GetRegister(20);
                    std::uint64_t x23 = jit_->GetRegister(23);
                    std::uint64_t x24 = jit_->GetRegister(24);
                    std::uint64_t st30 = 0, mgr58 = 0;
                    if (x19) ReadMemory(x19 + 0x30, &st30, 8, nullptr);
                    if (x24) ReadMemory(x24 + 0x58, &mgr58, 8, nullptr);
                    // Dump the manager's File object (x24+0x60): a tagged
                    // string {tagbyte/ptr, len} for the db path, plus the
                    // session-store slot. Also dump the raw 0x40 bytes at
                    // x24+0x40..+0x78 so the File/path layout is visible.
                    std::uint64_t raw[8] = {0};
                    if (x24)
                        ReadMemory(x24 + 0x40, raw, sizeof(raw), nullptr);
                    std::fprintf(
                        stderr,
                        "ISOMR_USDK_DIAG crash_pc=%llx tid=%lu fs_vt_630=%llx "
                        "fs_vt_638=%llx x19=%llx x20=%llx x23=%llx x24=%llx "
                        "store30=%llx mgr58=%llx\n"
                        "  mgr+40: %016llx %016llx %016llx %016llx "
                        "%016llx %016llx %016llx %016llx\n",
                        (unsigned long long)crash_pc,
                        static_cast<unsigned long>(GetCurrentThreadId()),
                        (unsigned long long)g630,
                        (unsigned long long)g638,
                        (unsigned long long)x19,
                        (unsigned long long)x20,
                        (unsigned long long)x23,
                        (unsigned long long)x24,
                        (unsigned long long)st30,
                        (unsigned long long)mgr58,
                        (unsigned long long)raw[0],
                        (unsigned long long)raw[1],
                        (unsigned long long)raw[2],
                        (unsigned long long)raw[3],
                        (unsigned long long)raw[4],
                        (unsigned long long)raw[5],
                        (unsigned long long)raw[6],
                        (unsigned long long)raw[7]);
                    std::fflush(stderr);
                }
                std::fprintf(
                    stderr,
                    "ISOMR_NULL_PAGE_READ address=%llx size=%zu "
                    "pc=%llx lr=%llx x0=%llx x1=%llx x8=%llx fp=%llx "
                    "sp=%llx this=%llx this_vt=%llx\n",
                    static_cast<unsigned long long>(address), size,
                    static_cast<unsigned long long>(
                        jit_ != nullptr ? jit_->GetPC() : 0),
                    static_cast<unsigned long long>(
                        jit_ != nullptr ? jit_->GetRegister(30) : 0),
                    static_cast<unsigned long long>(r[0]),
                    static_cast<unsigned long long>(r[1]),
                    static_cast<unsigned long long>(r[8]),
                    static_cast<unsigned long long>(fp),
                    static_cast<unsigned long long>(sp),
                    static_cast<unsigned long long>(s_this),
                    static_cast<unsigned long long>(s_this_vt));
                std::fflush(stderr);
            }
            std::memset(destination, 0, size);
            return true;
        }
        // The call-entry terminal address doubles as the resume-PC slot of a
        // captured guest context (Unity fibers snapshot LR into a ucontext-
        // style record). Frame walkers read the saved PC to test for a
        // syscall-stub signature; reading zeros answers "not a stub"
        // without faulting the walk.
        if (address >= kTerminalAddress &&
            address - kTerminalAddress < 0x1000ULL) {
            std::memset(destination, 0, size);
            return true;
        }
        return modules_->Read(address, destination, size);
    }
    bool WriteMemory(std::uint64_t address, const void* source,
                     std::size_t size,
                     std::uint64_t* watched_generation = nullptr) {
        if (watched_generation != nullptr) *watched_generation = 0;
        // Tracer: who writes the SexyAppFramework global that later holds
        // the 0x6800 trampoline value GameAppInitialize dereferences.
        if (address == 0x4000000000ULL + 0x2d38118ULL) {
            static std::size_t target_store_logs = 0;
            if (target_store_logs < 8) {
                ++target_store_logs;
                std::uint64_t stored = 0;
                std::memcpy(&stored, source, size < 8 ? size : 8);
                std::fprintf(stderr,
                             "ISOMR_GLOBAL_STORE address=%llx value=%llx "
                             "size=%zu pc=%llx\n",
                             static_cast<unsigned long long>(address),
                             static_cast<unsigned long long>(stored),
                             size,
                             static_cast<unsigned long long>(
                                 jit_ != nullptr ? jit_->GetPC() : 0));
                std::fflush(stderr);
            }
        }
        if (heap_->ContainsAddress(address)) {
            bool heap_written = false;
            if (forensic_memory_callbacks_) {
                heap_written = heap_->WriteFromGuest(
                address, source, size,
                jit_ != nullptr ? jit_->GetPC() : 0,
                jit_ != nullptr ? jit_->GetRegister(30) : 0,
                thread_pointer_,
                jit_ != nullptr ? jit_->GetSP() : 0,
                imports_->LastDispatchedNameForCurrentThread()
                    .value_or("none"),
                imports_->LastJniDispatchedNameForCurrentThread()
                    .value_or("none"),
                imports_->LastJniSemanticNoteForCurrentThread()
                    .value_or("none"),
                imports_->CurrentDispatchSequenceForCurrentThread()
                    .value_or(0),
                imports_->LastJniDispatchSequenceForCurrentThread()
                    .value_or(0),
                watched_generation);
            } else {
                heap_written =
                    heap_->WriteUntrackedChecked(address, source, size);
            }
            if (!heap_written) {
                static std::size_t heap_write_refusals = 0;
                if (heap_write_refusals < 8) {
                    ++heap_write_refusals;
                    std::fprintf(stderr,
                                 "ISOMR_HEAP_WRITE_REFUSED address=%llx "
                                 "size=%zu pc=%llx\n",
                                 static_cast<unsigned long long>(address),
                                 size,
                                 static_cast<unsigned long long>(
                                     jit_ != nullptr ? jit_->GetPC() : 0));
                    std::fflush(stderr);
                }
                return false;
            }
            return true;
        }
        // Null-page writes through never-built singletons sink harmlessly;
        // the null-page zero-read keeps the object's state at zero.
        if (address < 0x10000ULL) {
            static std::size_t null_page_writes = 0;
            if (null_page_writes < 12) {
                ++null_page_writes;
                std::uint64_t stored = 0;
                std::memcpy(&stored, source, size < 8 ? size : 8);
                std::fprintf(stderr,
                             "ISOMR_NULL_PAGE_WRITE address=%llx size=%zu "
                             "value=%llx pc=%llx\n",
                             static_cast<unsigned long long>(address), size,
                             static_cast<unsigned long long>(stored),
                             static_cast<unsigned long long>(
                                 jit_ != nullptr ? jit_->GetPC() : 0));
                std::fflush(stderr);
            }
            return true;
        }
        if (!modules_->Write(address, source, size)) {
            static std::size_t module_write_refusals = 0;
            if (module_write_refusals < 8) {
                ++module_write_refusals;
                std::fprintf(stderr,
                             "ISOMR_MODULE_WRITE_REFUSED address=%llx "
                             "size=%zu pc=%llx\n",
                             static_cast<unsigned long long>(address),
                             size,
                             static_cast<unsigned long long>(
                                 jit_ != nullptr ? jit_->GetPC() : 0));
                std::fflush(stderr);
            }
            return false;
        }
        return true;
    }
    bool WriteFastMemory(std::uint64_t address, const void* source,
                         std::size_t size) {
        if (heap_->ContainsAddress(address)) {
            const GuestHeap::AccessSpanResult result =
                heap_->WriteWithAccessSpanCache(
                    address, source, size, &write_access_cache_);
            if (full_call_telemetry_active_) {
                if (result == GuestHeap::AccessSpanResult::kCacheHit)
                    ++last_call_telemetry_.heap_write_cache_hits;
                else if (result == GuestHeap::AccessSpanResult::kValidated)
                    ++last_call_telemetry_.heap_write_cache_validations;
            }
            if (result == GuestHeap::AccessSpanResult::kFailed) {
                // Committed-but-untracked arena pages (bionic mmap
                // allocations, freed mapping pages kept committed) have no
                // blocks_ entry, so the tracked span path refuses them even
                // though they are guest-writable by default. Fall back to
                // the untracked committed writer before reporting a fault.
                if (heap_->WriteCommittedUntracked(address, source, size))
                    return true;
                static std::size_t fast_write_refusals = 0;
                if (fast_write_refusals < 8) {
                    ++fast_write_refusals;
                    std::fprintf(stderr,
                                 "ISOMR_FAST_WRITE_REFUSED address=%llx "
                                 "size=%zu pc=%llx\n",
                                 static_cast<unsigned long long>(address),
                                 size,
                                 static_cast<unsigned long long>(
                                     jit_ != nullptr ? jit_->GetPC() : 0));
                    std::fflush(stderr);
                }
            }
            return result != GuestHeap::AccessSpanResult::kFailed;
        }
        // Null-page writes through never-built singletons (Unity's
        // display-manager path touches obj+0x58/0x80 with obj=null) sink
        // harmlessly — the matching null-page zero-read keeps the object's
        // state consistent at zero, which is how the early-out path sees it.
        if (address < 0x10000ULL) {
            static std::size_t null_page_writes = 0;
            if (null_page_writes < 12) {
                ++null_page_writes;
                std::uint64_t stored = 0;
                std::memcpy(&stored, source, size < 8 ? size : 8);
                std::fprintf(stderr,
                             "ISOMR_NULL_PAGE_WRITE address=%llx size=%zu "
                             "value=%llx pc=%llx\n",
                             static_cast<unsigned long long>(address), size,
                             static_cast<unsigned long long>(stored),
                             static_cast<unsigned long long>(
                                 jit_ != nullptr ? jit_->GetPC() : 0));
                std::fflush(stderr);
            }
            return true;
        }
        if (!modules_->Write(address, source, size)) {
            static std::size_t fast_module_write_refusals = 0;
            if (fast_module_write_refusals < 8) {
                ++fast_module_write_refusals;
                std::fprintf(stderr,
                             "ISOMR_MODULE_FAST_WRITE_REFUSED address=%llx "
                             "size=%zu pc=%llx\n",
                             static_cast<unsigned long long>(address),
                             size,
                             static_cast<unsigned long long>(
                                 jit_ != nullptr ? jit_->GetPC() : 0));
                std::fflush(stderr);
            }
            return false;
        }
        return true;
    }
    void StopOrAbort(std::uint64_t address, const char* failure) {
        if (address != kTerminalAddress ||
            jit_->GetRegister(30) != kTerminalAddress) {
            Abort(failure);
            return;
        }
        stopped_ = true;
        jit_->HaltExecution(Dynarmic::HaltReason::MemoryAbort);
    }
    bool ObserveProcessExitCancellation() {
        if (!process_exit_cancellation_.requested ||
            !process_exit_cancellation_.requested())
            return false;
        if (!process_exit_cancellation_observed_) {
            process_exit_cancellation_observed_ = true;
            process_exit_cancellation_.observed();
        }
        if (jit_ != nullptr)
            jit_->HaltExecution(Dynarmic::HaltReason::MemoryAbort);
        return true;
    }
    void Abort(std::string failure) {
        valid_ = false;
        failure_ = std::move(failure);
        if (jit_ != nullptr)
            jit_->HaltExecution(Dynarmic::HaltReason::MemoryAbort);
    }

    Aarch64ExclusiveMonitor* exclusive_monitor_ = nullptr;
    Elf64ModuleSet* modules_ = nullptr;
    const Aarch64ImportRegistry* imports_ = nullptr;
    GuestHeap* heap_ = nullptr;
    std::uint64_t stack_ = 0;
    std::size_t stack_size_ = 0;
    std::uint64_t thread_pointer_ = 0;
    std::uint64_t instruction_budget_ = 0;
    Aarch64MemoryCaptureConfig memory_capture_config_{};
    std::uint64_t total_ticks_remaining_ = 0;
    std::uint64_t instructions_executed_total_ = 0;
    std::uint64_t slice_ticks_remaining_ = 0;
    std::uint64_t inter_slice_instruction_budget_ = 0;
    std::uint64_t next_inter_slice_instruction_budget_ = 0;
    Aarch64OwnerThreadInterSliceCallback inter_slice_callback_;
    std::thread::id inter_slice_owner_thread_{};
    bool inter_slice_callback_active_ = false;
    bool inter_slice_renews_total_budget_ = false;
    struct ExecutionBreakpoint {
        std::uint64_t address = 0;
        Aarch64OwnerThreadExecutionBreakpointCallback callback;
    };
    std::vector<ExecutionBreakpoint> execution_breakpoints_;
    std::thread::id execution_breakpoint_owner_thread_{};
    std::uint64_t pending_execution_breakpoint_address_ = 0;
    std::uint32_t terminal_probe_ = 0;
    std::unique_ptr<A64::Jit> jit_;
    std::unique_ptr<Dynarmic::ExclusiveMonitor> owned_monitor_;
    std::optional<std::size_t> processor_id_;
    // Recursive so an owner-thread breakpoint callback (which runs inside
    // CallEntry's locked region) may remove its own breakpoint.
    mutable std::recursive_mutex call_mutex_;
    Aarch64CallTelemetryMode telemetry_mode_ =
        Aarch64CallTelemetryMode::kOff;
    bool call_telemetry_active_ = false;
    bool full_call_telemetry_active_ = false;
    bool svc_category_telemetry_active_ = false;
    bool call_cpu_start_available_ = false;
    std::uint64_t call_cpu_start_ = 0;
    std::uint64_t call_heap_metadata_start_ = 0;
    std::uint64_t call_telemetry_completion_sequence_ = 0;
    std::uint64_t memory_callback_ordinal_ = 0;
    std::chrono::steady_clock::time_point call_wall_start_{};
    Aarch64GuestCallTelemetry last_call_telemetry_{};
    bool valid_ = true;
    bool stopped_ = false;
    bool memory_capture_pending_ = false;
    bool forensic_memory_callbacks_ = false;
    bool direct_heap_table_used_ = false;
    mutable GuestHeap::AccessSpanCache read_access_cache_{};
    GuestHeap::AccessSpanCache write_access_cache_{};
    std::uint64_t memory_capture_match_count_ = 0;
    Aarch64MemoryAccessCapture memory_access_capture_{};
    bool last_read_heap_ = false;
    bool last_read_zero_ = true;
    std::array<ReadTrace, 4096> read_traces_{};
    std::size_t read_trace_count_ = 0;
    std::size_t read_trace_next_ = 0;
    std::array<WriteTrace, 4096> write_traces_{};
    std::size_t write_trace_count_ = 0;
    std::size_t write_trace_next_ = 0;
    std::string failure_;
    std::shared_ptr<WorkerExecutionTelemetry> execution_telemetry_;
    Aarch64GuestProcessExitCancellation process_exit_cancellation_;
    bool process_exit_cancellation_observed_ = false;
};

std::unique_ptr<Aarch64GuestSession> Aarch64GuestSession::Create(
    Elf64ModuleSet* modules, const Aarch64ImportRegistry* imports,
    GuestHeap* heap, std::uint64_t thread_pointer, std::size_t stack_size,
    std::uint64_t instruction_budget, std::string* error,
    Aarch64MemoryCaptureConfig memory_capture,
    std::shared_ptr<WorkerExecutionTelemetry> execution_telemetry,
    Aarch64GuestProcessExitCancellation process_exit_cancellation) {
    return Create(nullptr, modules, imports, heap, thread_pointer,
                  stack_size, instruction_budget, error, memory_capture,
                  std::move(execution_telemetry),
                  std::move(process_exit_cancellation));
}

std::unique_ptr<Aarch64GuestSession> Aarch64GuestSession::Create(
    Aarch64ExclusiveMonitor* exclusive_monitor,
    Elf64ModuleSet* modules, const Aarch64ImportRegistry* imports,
    GuestHeap* heap, std::uint64_t thread_pointer, std::size_t stack_size,
    std::uint64_t instruction_budget, std::string* error,
    Aarch64MemoryCaptureConfig memory_capture,
    std::shared_ptr<WorkerExecutionTelemetry> execution_telemetry,
    Aarch64GuestProcessExitCancellation process_exit_cancellation) {
    if (heap == nullptr || stack_size < 4096) {
        SetError(error, "AArch64 guest stack configuration is invalid");
        return nullptr;
    }
    const std::uint64_t stack = heap->Allocate(stack_size);
    if (stack == 0) {
        SetError(error, "AArch64 guest stack allocation failed: " +
                            heap->DescribeLastAllocationFailure());
        return nullptr;
    }
    auto impl = std::make_unique<Impl>(
        exclusive_monitor, modules, imports, heap, stack, stack_size,
        thread_pointer, instruction_budget, memory_capture,
        std::move(execution_telemetry),
        std::move(process_exit_cancellation));
    if (!impl->Initialize(error)) return nullptr;
    return std::unique_ptr<Aarch64GuestSession>(
        new Aarch64GuestSession(std::move(impl)));
}

Aarch64GuestSession::Aarch64GuestSession(std::unique_ptr<Impl> impl)
    : impl_(std::move(impl)) {}
Aarch64GuestSession::~Aarch64GuestSession() = default;

bool Aarch64GuestSession::Call(
    std::uint64_t function,
    const std::array<std::uint64_t, 8>& arguments, std::uint64_t* result,
    std::string* error) {
    return impl_ != nullptr && impl_->Call(function, arguments, result, error);
}
bool Aarch64GuestSession::CallEntry(
    std::uint64_t function,
    const std::vector<Aarch64CallArgument>& arguments,
    Aarch64CallResult* result, std::string* error) {
    return impl_ != nullptr &&
           impl_->CallEntry(function, arguments, result, error);
}
bool Aarch64GuestSession::SetInstructionBudgetForNextCall(
    std::uint64_t instruction_budget, std::string* error) {
    return impl_ != nullptr &&
           impl_->SetInstructionBudgetForNextCall(instruction_budget, error);
}
bool Aarch64GuestSession::SetOwnerThreadInterSliceCallback(
    std::uint64_t slice_instruction_budget,
    Aarch64OwnerThreadInterSliceCallback callback,
    std::string* error,
    bool renew_total_budget_after_slice) {
    return impl_ != nullptr && impl_->SetOwnerThreadInterSliceCallback(
                                   slice_instruction_budget,
                                   std::move(callback), error,
                                   renew_total_budget_after_slice);
}
void Aarch64GuestSession::ClearOwnerThreadInterSliceCallback() {
    if (impl_ != nullptr) impl_->ClearOwnerThreadInterSliceCallback();
}
bool Aarch64GuestSession::SetOwnerThreadInterSliceBudgetForNextSlice(
    std::uint64_t slice_instruction_budget, std::string* error) {
    return impl_ != nullptr &&
           impl_->SetOwnerThreadInterSliceBudgetForNextSlice(
               slice_instruction_budget, error);
}
bool Aarch64GuestSession::CaptureGuestPcTelemetrySample(
    Aarch64GuestCallTelemetry* telemetry, std::string* error) const {
    return impl_ != nullptr &&
           impl_->CaptureGuestPcTelemetrySample(telemetry, error);
}
bool Aarch64GuestSession::SetOwnerThreadExecutionBreakpoint(
    std::uint64_t guest_address,
    Aarch64OwnerThreadExecutionBreakpointCallback callback,
    std::string* error) {
    return impl_ != nullptr && impl_->SetOwnerThreadExecutionBreakpoint(
                                   guest_address, std::move(callback), error);
}
void Aarch64GuestSession::ClearOwnerThreadExecutionBreakpoint() {
    if (impl_ != nullptr) impl_->ClearOwnerThreadExecutionBreakpoint();
}
bool Aarch64GuestSession::AddOwnerThreadExecutionBreakpoint(
    std::uint64_t guest_address,
    Aarch64OwnerThreadExecutionBreakpointCallback callback,
    std::string* error) {
    return impl_ != nullptr && impl_->AddOwnerThreadExecutionBreakpoint(
                                   guest_address, std::move(callback), error);
}
void Aarch64GuestSession::RemoveOwnerThreadExecutionBreakpoint(
    std::uint64_t guest_address) {
    if (impl_ != nullptr)
        impl_->RemoveOwnerThreadExecutionBreakpoint(guest_address);
}
void Aarch64GuestSession::SetCallTelemetryEnabled(bool enabled) {
    if (impl_ != nullptr) impl_->SetCallTelemetryEnabled(enabled);
}
void Aarch64GuestSession::SetCallTelemetryMode(Aarch64CallTelemetryMode mode) {
    if (impl_ != nullptr) impl_->SetCallTelemetryMode(mode);
}
void Aarch64GuestSession::RequestHalt() {
    if (impl_ != nullptr) impl_->RequestHalt();
}
std::uint64_t Aarch64GuestSession::register_value(std::size_t index) const {
    return impl_ != nullptr ? impl_->RegisterValue(index) : 0;
}
std::uint64_t Aarch64GuestSession::program_counter() const {
    return impl_ != nullptr ? impl_->ProgramCounter() : 0;
}
std::uint64_t Aarch64GuestSession::instructions_executed() const {
    return impl_ != nullptr ? impl_->InstructionsExecuted() : 0;
}
std::uint64_t Aarch64GuestSession::stack_base() const {
    return impl_ != nullptr ? impl_->StackBase() : 0;
}
std::uint64_t Aarch64GuestSession::stack_top() const {
    return impl_ != nullptr ? impl_->StackTop() : 0;
}
std::uint64_t Aarch64GuestSession::stack_pointer() const {
    return impl_ != nullptr ? impl_->StackPointer() : 0;
}
std::uint64_t Aarch64GuestSession::thread_pointer() const {
    return impl_ != nullptr ? impl_->ThreadPointer() : 0;
}
Aarch64MemoryAccessCapture Aarch64GuestSession::memory_access_capture() const {
    return impl_ != nullptr ? impl_->MemoryAccessCapture()
                            : Aarch64MemoryAccessCapture{};
}
Aarch64GuestCallTelemetry Aarch64GuestSession::last_call_telemetry() const {
    return impl_ != nullptr ? impl_->LastCallTelemetry()
                            : Aarch64GuestCallTelemetry{};
}

}  // namespace isomr
