#include "bionic_threads.h"

#include "bionic_thread_state.h"
#include "bionic_tls.h"
#include "guest_address_space.h"
#include "guest_heap.h"
#include "guest_thread_stack.h"
#include "import_registry.h"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <chrono>
#include <cstdio>
#include <limits>
#include <mutex>
#include <new>
#include <optional>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace isomr {
namespace {

constexpr int kSuccess = 0;
constexpr int kIoError = 5;
constexpr int kTryAgain = 11;
constexpr int kBadAddress = 14;
constexpr int kInvalid = 22;
constexpr int kDeadlock = 35;
constexpr std::size_t kDestructorIterations = 4;

void SetError(std::string* error, std::string message) {
    if (error != nullptr) *error = std::move(message);
}

bool IsExecutableProtection(DWORD protection) {
    const DWORD base = protection & 0xffU;
    return base == PAGE_EXECUTE || base == PAGE_EXECUTE_READ ||
           base == PAGE_EXECUTE_READWRITE ||
           base == PAGE_EXECUTE_WRITECOPY;
}

BionicThreads::WorkerHostResidency ClassifyHostInstructionPointer(
    std::uintptr_t instruction_pointer) {
    MEMORY_BASIC_INFORMATION memory{};
    if (instruction_pointer == 0 ||
        VirtualQuery(reinterpret_cast<LPCVOID>(instruction_pointer), &memory,
                     sizeof(memory)) != sizeof(memory))
        return BionicThreads::WorkerHostResidency::kSampleFailed;
    if (!IsExecutableProtection(memory.Protect))
        return BionicThreads::WorkerHostResidency::kNonExecutable;
    if (memory.Type == MEM_PRIVATE)
        return BionicThreads::WorkerHostResidency::kGeneratedPrivateExecutable;
    if (memory.Type == MEM_IMAGE) {
        const HMODULE main_image = GetModuleHandleW(nullptr);
        return main_image != nullptr && memory.AllocationBase == main_image
                   ? BionicThreads::WorkerHostResidency::kArmrMainImage
                   : BionicThreads::WorkerHostResidency::kOtherImage;
    }
    return BionicThreads::WorkerHostResidency::kNonExecutable;
}

BionicThreads::WorkerHostResidency SampleWorkerHostResidency(
    HANDLE thread_handle, DWORD thread_id) {
    if (thread_handle == nullptr || thread_id == 0)
        return BionicThreads::WorkerHostResidency::kUnavailable;
    if (thread_id == GetCurrentThreadId())
        return BionicThreads::WorkerHostResidency::kSampleFailed;
    if (SuspendThread(thread_handle) == static_cast<DWORD>(-1))
        return BionicThreads::WorkerHostResidency::kSampleFailed;

    CONTEXT context{};
    context.ContextFlags = CONTEXT_CONTROL;
    const bool captured = GetThreadContext(thread_handle, &context) != FALSE;
    const bool resumed =
        ResumeThread(thread_handle) != static_cast<DWORD>(-1);
    if (!captured || !resumed)
        return BionicThreads::WorkerHostResidency::kSampleFailed;
#if defined(_M_X64)
    return ClassifyHostInstructionPointer(
        static_cast<std::uintptr_t>(context.Rip));
#else
    return BionicThreads::WorkerHostResidency::kSampleFailed;
#endif
}

}  // namespace

struct BionicThreads::ThreadRecord {
    ThreadRecord(std::uint64_t guest_handle, std::uint64_t worker_ordinal)
        : ThreadRecord(guest_handle, worker_ordinal, worker_ordinal) {}
    ThreadRecord(std::uint64_t guest_handle, std::uint64_t worker_ordinal,
                 std::uint64_t worker_start_group)
        : handle(guest_handle), ordinal(worker_ordinal),
          start_group(worker_start_group) {}
    ~ThreadRecord() {
        if (cpu_thread_handle != nullptr)
            (void)CloseHandle(cpu_thread_handle);
    }

    std::uint64_t handle = 0;
    std::uint64_t ordinal = 0;
    const std::uint64_t start_group = 0;
    std::uint64_t start_routine = 0;
    std::uint64_t argument = 0;
    std::uint64_t result = 0;
    std::uint64_t host_thread_hash = 0;
    DWORD host_thread_id = 0;
    std::string error;
    std::thread worker;
    HANDLE cpu_thread_handle = nullptr;
    std::uint64_t execution_slice_ticks = 0;
    std::shared_ptr<WorkerExecutionTelemetry> execution_telemetry;
    std::shared_ptr<std::atomic<bool>> process_exit_cancellation_requested =
        std::make_shared<std::atomic<bool>>(false);
    std::shared_ptr<std::atomic<bool>> process_exit_cancellation_observed =
        std::make_shared<std::atomic<bool>>(false);
    std::mutex mutex;
    bool completed = false;
    bool execution_ok = false;
    bool detached = false;
    bool join_claimed = false;
    bool cleaned = false;
    bool process_exit_cancelled = false;
};

struct BionicThreads::SharedState {
    struct WorkerStartGroup {
        std::uint64_t start_routine = 0;
        std::uint64_t first_ordinal = 0;
    };
    struct OnceRecord {
        std::mutex mutex;
        std::condition_variable changed;
        std::thread::id owner;
        bool running = false;
        bool complete = false;
    };
    GuestHeap* heap = nullptr;
    GuestAddressSpace* address_space = nullptr;
    BionicThreadState* thread_state = nullptr;
    BionicTls* tls = nullptr;
    GuestExecutorFactory executor_factory;
    ThreadHook attach_hook;
    ThreadHook detach_hook;
    mutable std::mutex mutex;
    std::condition_variable workers_changed;
    std::unordered_map<std::uint64_t, std::shared_ptr<ThreadRecord>> threads;
    std::unordered_map<std::uint64_t, std::shared_ptr<OnceRecord>> once;
    std::size_t active_workers = 0;
    std::uint64_t next_worker_ordinal = 0;
    // Raw routines remain internal equality keys. Representatives survive
    // record cleanup for the lifetime of this BionicThreads instance.
    std::vector<WorkerStartGroup> worker_start_groups;
    bool worker_creation_started = false;
    std::atomic<bool> worker_cpu_sampling_enabled{false};
    std::atomic<QsortOperationTelemetryMode> qsort_operation_telemetry_mode{
        QsortOperationTelemetryMode::kDisabled};
    std::atomic<std::uint64_t> next_qsort_operation_sequence{0};
    mutable std::mutex qsort_telemetry_mutex;
    QsortOperationSnapshot qsort_operation_snapshot;
    std::atomic<QsortAlgorithm> qsort_algorithm{QsortAlgorithm::kHeapSort};
    bool worker_execution_sampling_enabled = false;
    bool worker_execution_sampling_configured = false;
    bool process_exit_cleanup_started = false;
    std::atomic<bool> process_exit_requested{false};
    std::atomic<int> process_exit_code{0};
    std::uint64_t worker_execution_slice_ticks = 0;
    std::vector<std::uint64_t> worker_pc_groups;
    std::string first_completed_worker_failure;
};

BionicThreads::BionicThreads(GuestHeap* heap,
                             GuestAddressSpace* address_space,
                             BionicThreadState* thread_state, BionicTls* tls,
                             GuestExecutorFactory executor_factory,
                             ThreadHook attach_hook,
                             ThreadHook detach_hook)
    : shared_(std::make_shared<SharedState>()) {
    shared_->heap = heap;
    shared_->address_space = address_space;
    shared_->thread_state = thread_state;
    shared_->tls = tls;
    shared_->executor_factory = std::move(executor_factory);
    shared_->attach_hook = std::move(attach_hook);
    shared_->detach_hook = std::move(detach_hook);
}

BionicThreads::~BionicThreads() {
    std::vector<std::shared_ptr<ThreadRecord>> joinable;
    {
        const std::lock_guard lock(shared_->mutex);
        for (const auto& entry : shared_->threads) joinable.push_back(entry.second);
    }
    for (const std::shared_ptr<ThreadRecord>& record : joinable) {
        bool should_join = false;
        {
            const std::lock_guard lock(record->mutex);
            if (!record->detached && !record->join_claimed &&
                record->worker.joinable()) {
                record->join_claimed = true;
                should_join = true;
            }
        }
        if (should_join) {
            record->worker.join();
            CleanupRecord(shared_, record);
        }
    }
    std::unique_lock lock(shared_->mutex);
    shared_->workers_changed.wait(
        lock, [&] { return shared_->active_workers == 0; });
}

bool BionicThreads::AttrInit(std::uint64_t attributes_address, int* result,
                             std::string* error) {
    if (result == nullptr || shared_ == nullptr ||
        shared_->address_space == nullptr || attributes_address == 0) {
        SetError(error, "pthread_attr_init arguments are invalid");
        return false;
    }
    // Bionic's pthread_attr_init zeroes the attribute object; pthread_create
    // in this runtime applies its own defaults regardless of the attributes.
    static constexpr std::array<std::uint8_t, 32> kZeroAttributes{};
    if (!shared_->address_space->Write(attributes_address, kZeroAttributes.data(),
                                       kZeroAttributes.size())) {
        SetError(error,
                 "pthread_attr_init could not write the attribute object");
        return false;
    }
    *result = kSuccess;
    return true;
}

bool BionicThreads::AttrSetDetachState(std::uint64_t attributes_address,
                                       int state, int* result,
                                       std::string* error) {
    if (result == nullptr || shared_ == nullptr ||
        shared_->address_space == nullptr || attributes_address == 0 ||
        (state != 0 && state != 1)) {
        SetError(error, "pthread_attr_setdetachstate arguments are invalid");
        return false;
    }
    // pthread_create in this runtime always runs detached workers, so the
    // requested state is validated and otherwise recorded nowhere.
    *result = kSuccess;
    return true;
}

bool BionicThreads::AttrSetStackSize(std::uint64_t attributes_address,
                                     std::uint64_t stack_size, int* result,
                                     std::string* error) {
    if (result == nullptr || shared_ == nullptr ||
        shared_->address_space == nullptr || attributes_address == 0) {
        SetError(error, "pthread_attr_setstacksize arguments are invalid");
        return false;
    }
    if (stack_size != 0 && stack_size < 4096U) {
        SetError(error,
                 "pthread_attr_setstacksize size is below the page minimum");
        return false;
    }
    // Worker stacks are sized by the runtime; the guest's request is
    // validated and otherwise ignored.
    *result = kSuccess;
    return true;
}

bool BionicThreads::AttrDestroy(std::uint64_t attributes_address,
                                int* result, std::string* error) {
    if (result == nullptr || shared_ == nullptr ||
        shared_->address_space == nullptr || attributes_address == 0) {
        SetError(error, "pthread_attr_destroy arguments are invalid");
        return false;
    }
    // Bionic's pthread_attr_destroy is a no-op for these attributes.
    *result = kSuccess;
    return true;
}

bool BionicThreads::AttrGetNp(std::uint64_t thread, std::uint64_t attributes_address,
                              int* result, std::string* error) {
    // The owning thread is accepted per the signature but not tracked; for
    // pthread_self() the calling thread's live stack is reported below.
    (void)thread;
    if (result == nullptr || shared_ == nullptr ||
        shared_->address_space == nullptr || attributes_address == 0) {
        SetError(error, "pthread_getattr_np arguments are invalid");
        return false;
    }
    // Bionic pthread_attr_t (aarch64): flags@0, stack_base@8, stack_size@16,
    // guard_size@24, sched_policy@32, sched_priority@36, reserved@40..55.
    // The il2cpp Boehm GC queries the calling thread's stack bounds here to
    // bound its conservative stack scan; report the real guest stack.
    std::array<std::uint8_t, 56> attributes{};
    const auto put64 = [&attributes](std::size_t offset, std::uint64_t value) {
        for (std::size_t byte = 0; byte < 8; ++byte)
            attributes[offset + byte] =
                static_cast<std::uint8_t>((value >> (8 * byte)) & 0xff);
    };
    put64(8, g_guest_thread_stack_base);
    put64(16, g_guest_thread_stack_size);
    if (!shared_->address_space->Write(attributes_address, attributes.data(),
                                       attributes.size())) {
        SetError(error, "pthread_getattr_np could not write the attribute object");
        return false;
    }
    *result = kSuccess;
    return true;
}

bool BionicThreads::AttrGetStack(std::uint64_t attributes_address,
                                 std::uint64_t stack_base_address,
                                 std::uint64_t stack_size_address,
                                 int* result, std::string* error) {
    if (result == nullptr || shared_ == nullptr ||
        shared_->address_space == nullptr || attributes_address == 0 ||
        stack_base_address == 0 || stack_size_address == 0) {
        SetError(error, "pthread_attr_getstack arguments are invalid");
        return false;
    }
    // Read the recorded stack out of the bionic pthread_attr_t the getattr
    // path wrote (stack_base@8, stack_size@16) so it round-trips correctly.
    std::uint64_t base = 0;
    std::uint64_t size = 0;
    if (!shared_->address_space->Read(attributes_address + 8, &base,
                                      sizeof(base)) ||
        !shared_->address_space->Read(attributes_address + 16, &size,
                                      sizeof(size))) {
        SetError(error, "pthread_attr_getstack could not read the attribute");
        return false;
    }
    if (!shared_->address_space->Write(stack_base_address, &base,
                                       sizeof(base)) ||
        !shared_->address_space->Write(stack_size_address, &size,
                                       sizeof(size))) {
        SetError(error, "pthread_attr_getstack could not write outputs");
        return false;
    }
    *result = kSuccess;
    return true;
}

bool BionicThreads::AttrGetSched(std::uint64_t attributes_address,
                                 std::uint64_t output_address, int* result,
                                 std::string* error) {
    if (result == nullptr || shared_ == nullptr ||
        shared_->address_space == nullptr || attributes_address == 0 ||
        output_address == 0) {
        SetError(error, "pthread_attr_getschedparam arguments are invalid");
        return false;
    }
    // sched_param's only portable field is sched_priority; a default
    // attribute reads back priority zero (SCHED_OTHER default).
    const std::int32_t zero_priority = 0;
    if (!shared_->address_space->Write(output_address, &zero_priority,
                                       sizeof(zero_priority))) {
        SetError(error, "pthread_attr_getschedparam could not write output");
        return false;
    }
    *result = kSuccess;
    return true;
}

bool BionicThreads::AttrSetSchedPolicy(std::uint64_t attributes_address,
                                       int policy, int* result,
                                       std::string* error) {
    if (result == nullptr || shared_ == nullptr ||
        shared_->address_space == nullptr || attributes_address == 0 ||
        policy < 0 || policy > 2) {
        SetError(error, "pthread_attr_setschedpolicy arguments are invalid");
        return false;
    }
    // Worker scheduling is owned by the host; the requested policy is
    // validated and otherwise ignored.
    *result = kSuccess;
    return true;
}

bool BionicThreads::AttrSetSchedParam(std::uint64_t attributes_address,
                                      std::uint64_t param_address, int* result,
                                      std::string* error) {
    if (result == nullptr || shared_ == nullptr ||
        shared_->address_space == nullptr || attributes_address == 0 ||
        param_address == 0) {
        SetError(error, "pthread_attr_setschedparam arguments are invalid");
        return false;
    }
    // Worker scheduling is owned by the host; the requested priority is
    // read for validation and otherwise ignored.
    std::int32_t priority = 0;
    if (!shared_->address_space->Read(param_address, &priority,
                                      sizeof(priority))) {
        SetError(error, "pthread_attr_setschedparam param is unreadable");
        return false;
    }
    *result = kSuccess;
    return true;
}

// Default-off diagnostic: ISOMR_TRACE_ONCE=1 logs each pthread_once
// dispatch (control address, routine, control word before/after, and whether
// the guest initializer actually ran). Used to prove Unity's CPU-topology
// once-init executes under emulation.
static bool OnceTraceEnabled() {
    static const bool enabled = [] {
        wchar_t flag[8] = {};
        return GetEnvironmentVariableW(L"ISOMR_TRACE_ONCE", flag, 8) > 0 &&
               flag[0] != L'0';
    }();
    return enabled;
}

bool BionicThreads::Once(std::uint64_t once_address,
                         std::uint64_t routine, int* result,
                         std::string* error) {
    const bool trace = OnceTraceEnabled();
    if (result == nullptr || shared_->address_space == nullptr ||
        shared_->thread_state == nullptr || !shared_->executor_factory ||
        once_address == 0 || routine == 0 ||
        (once_address & (alignof(std::uint32_t) - 1)) != 0) {
        SetError(error, "pthread_once arguments are invalid");
        return false;
    }
    std::uint32_t word = 0;
    if (!shared_->address_space->Read(once_address, &word, sizeof(word)) ||
        word > 2) {
        SetError(error, "pthread_once state is invalid");
        return false;
    }
    if (trace && word != 2) {
        std::fprintf(stderr,
                     "ISOMR_ONCE enter ctl=0x%llx fn=0x%llx word=%u\n",
                     static_cast<unsigned long long>(once_address),
                     static_cast<unsigned long long>(routine), word);
        std::fflush(stderr);
    }
    std::shared_ptr<SharedState::OnceRecord> record;
    {
        const std::lock_guard lock(shared_->mutex);
        auto [entry, inserted] = shared_->once.try_emplace(
            once_address, std::make_shared<SharedState::OnceRecord>());
        (void)inserted;
        record = entry->second;
    }
    std::unique_lock lock(record->mutex);
    if (!shared_->address_space->Read(once_address, &word, sizeof(word)) ||
        word > 2) {
        SetError(error, "pthread_once state changed to an invalid value");
        return false;
    }
    if (word == 2) {
        if (trace) {
            std::fprintf(stderr,
                         "ISOMR_ONCE skip-done ctl=0x%llx fn=0x%llx\n",
                         static_cast<unsigned long long>(once_address),
                         static_cast<unsigned long long>(routine));
            std::fflush(stderr);
        }
        record->complete = true;
        *result = kSuccess;
        return true;
    }
    // The guest control word owns the lifetime. A completed host record at a
    // reset address belongs to an earlier allocation generation and must not
    // suppress the new initializer.
    if (record->complete) record->complete = false;
    while (record->running) {
        if (record->owner == std::this_thread::get_id()) {
            if (trace) {
                std::fprintf(
                    stderr,
                    "ISOMR_ONCE deadlock ctl=0x%llx fn=0x%llx\n",
                    static_cast<unsigned long long>(once_address),
                    static_cast<unsigned long long>(routine));
                std::fflush(stderr);
            }
            *result = kDeadlock;
            return true;
        }
        record->changed.wait(lock, [&] { return !record->running; });
        if (record->complete) {
            *result = kSuccess;
            return true;
        }
    }
    record->running = true;
    record->owner = std::this_thread::get_id();
    const std::uint32_t running = 1;
    if (!shared_->address_space->Write(once_address, &running,
                                       sizeof(running))) {
        record->running = false;
        record->owner = {};
        lock.unlock();
        record->changed.notify_all();
        SetError(error, "pthread_once could not write running state");
        return false;
    }
    lock.unlock();

    const std::optional<std::uint64_t> thread_handle =
        shared_->thread_state->CurrentErrnoAddress();
    GuestCall executor = thread_handle
                             ? shared_->executor_factory(*thread_handle, error)
                             : GuestCall{};
    std::array<std::uint64_t, 8> arguments{};
    std::uint64_t ignored = 0;
    const bool ok = executor && executor(routine, arguments, &ignored, error);
    if (trace) {
        std::fprintf(stderr,
                     "ISOMR_ONCE ran ctl=0x%llx fn=0x%llx ok=%d "
                     "executor=%d handle=%s\n",
                     static_cast<unsigned long long>(once_address),
                     static_cast<unsigned long long>(routine), ok ? 1 : 0,
                     executor ? 1 : 0,
                     thread_handle ? "set" : "null");
        std::fflush(stderr);
    }

    lock.lock();
    const std::uint32_t final_word = ok ? 2U : 0U;
    const bool wrote = shared_->address_space->Write(
        once_address, &final_word, sizeof(final_word));
    record->complete = ok && wrote;
    record->running = false;
    record->owner = {};
    lock.unlock();
    record->changed.notify_all();
    if (!ok) {
        if (error != nullptr && error->empty())
            *error = "pthread_once initializer failed";
        return false;
    }
    if (!wrote) {
        SetError(error, "pthread_once could not write completed state");
        return false;
    }
    *result = kSuccess;
    return true;
}

bool BionicThreads::BinarySearch(std::uint64_t key, std::uint64_t base,
                                 std::uint64_t count,
                                 std::uint64_t element_size,
                                 std::uint64_t comparator,
                                 std::uint64_t* result,
                                 std::string* error) {
    if (result == nullptr || shared_->address_space == nullptr ||
        shared_->thread_state == nullptr || !shared_->executor_factory) {
        SetError(error, "bsearch runtime is unavailable");
        return false;
    }
    *result = 0;
    if (count == 0) return true;
    if (key == 0 || base == 0 || element_size == 0 || comparator == 0 ||
        count > std::numeric_limits<std::uint64_t>::max() / element_size) {
        SetError(error, "bsearch arguments are invalid");
        return false;
    }
    const std::uint64_t byte_count = count * element_size;
    if (base > std::numeric_limits<std::uint64_t>::max() -
                   (byte_count - 1)) {
        SetError(error, "bsearch array range overflows");
        return false;
    }
    std::uint8_t boundary = 0;
    if (!shared_->address_space->Read(base, &boundary, sizeof(boundary)) ||
        !shared_->address_space->Read(base + byte_count - 1, &boundary,
                                      sizeof(boundary))) {
        SetError(error, "bsearch array range is unreadable");
        return false;
    }
    const std::optional<std::uint64_t> thread_handle =
        shared_->thread_state->CurrentErrnoAddress();
    GuestCall executor = thread_handle
                             ? shared_->executor_factory(*thread_handle, error)
                             : GuestCall{};
    if (!executor) {
        if (error != nullptr && error->empty())
            *error = "bsearch guest comparator executor is unavailable";
        return false;
    }

    std::uint64_t first = 0;
    std::uint64_t last = count;
    while (first < last) {
        const std::uint64_t middle = first + (last - first) / 2;
        const std::uint64_t element = base + middle * element_size;
        std::array<std::uint64_t, 8> arguments{};
        arguments[0] = key;
        arguments[1] = element;
        std::uint64_t comparison_result = 0;
        if (!executor(comparator, arguments, &comparison_result, error)) {
            if (error != nullptr && error->empty())
                *error = "bsearch guest comparator failed";
            return false;
        }
        const std::int32_t comparison = static_cast<std::int32_t>(
            static_cast<std::uint32_t>(comparison_result));
        if (comparison < 0) {
            last = middle;
        } else if (comparison > 0) {
            first = middle + 1;
        } else {
            *result = element;
            return true;
        }
    }
    return true;
}

bool BionicThreads::Sort(std::uint64_t base, std::uint64_t count,
                         std::uint64_t element_size,
                         std::uint64_t comparator,
                         std::string* error) {
    if (shared_->address_space == nullptr ||
        shared_->thread_state == nullptr || !shared_->executor_factory) {
        SetError(error, "qsort runtime is unavailable");
        return false;
    }
    if (count == 0) return true;
    if (base == 0 || element_size == 0 || comparator == 0 ||
        count > std::numeric_limits<std::uint64_t>::max() / element_size) {
        SetError(error, "qsort arguments are invalid");
        return false;
    }
    const std::uint64_t byte_count = count * element_size;
    if (base > std::numeric_limits<std::uint64_t>::max() -
                   (byte_count - 1) ||
        byte_count > std::numeric_limits<std::size_t>::max() ||
        element_size > std::numeric_limits<std::size_t>::max()) {
        SetError(error, "qsort array range overflows");
        return false;
    }
    if (!shared_->address_space->IsWritableRange(
            base, static_cast<std::size_t>(byte_count))) {
        SetError(error, "qsort array range is not writable");
        return false;
    }
    if (count == 1) return true;
    struct OperationTelemetry {
        SharedState* shared = nullptr;
        QsortOperationTelemetryMode mode =
            QsortOperationTelemetryMode::kDisabled;
        std::uint64_t sequence = 0;
        std::uint64_t count = 0;
        std::uint64_t element_size = 0;
        std::uint64_t comparisons = 0;
        std::uint64_t swaps = 0;
        std::uint64_t executor_setup_wall_ns = 0;
        std::uint64_t comparator_wall_ns = 0;
        std::uint64_t swap_wall_ns = 0;
        bool passed = false;
        std::chrono::steady_clock::time_point started{};

        OperationTelemetry(SharedState* telemetry_shared,
                           QsortOperationTelemetryMode telemetry_mode,
                           std::uint64_t operation_sequence,
                           std::uint64_t operation_count,
                           std::uint64_t operation_element_size)
            : shared(telemetry_shared), mode(telemetry_mode),
              sequence(operation_sequence),
              count(operation_count), element_size(operation_element_size),
              started(telemetry_mode !=
                              QsortOperationTelemetryMode::kDisabled
                          ? std::chrono::steady_clock::now()
                          : std::chrono::steady_clock::time_point{}) {
            if (mode != QsortOperationTelemetryMode::kDisabled) {
                const std::lock_guard lock(
                    shared->qsort_telemetry_mutex);
                ++shared->qsort_operation_snapshot.started;
            }
            if (mode == QsortOperationTelemetryMode::kOperationBoundary) {
                std::fprintf(
                    stdout,
                    "ISOMR_BIONIC_QSORT_OPERATION sequence=%llu state=begin count=%llu element-size=%llu\n",
                    static_cast<unsigned long long>(sequence),
                    static_cast<unsigned long long>(count),
                    static_cast<unsigned long long>(element_size));
            }
        }

        ~OperationTelemetry() {
            if (mode == QsortOperationTelemetryMode::kDisabled) return;
            const auto elapsed_count =
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() - started)
                    .count();
            const std::uint64_t elapsed = elapsed_count > 0
                ? static_cast<std::uint64_t>(elapsed_count)
                : 0;
            const std::uint64_t component_wall_ns =
                executor_setup_wall_ns + comparator_wall_ns + swap_wall_ns;
            const std::uint64_t residual_wall_ns =
                elapsed >= component_wall_ns ? elapsed - component_wall_ns : 0;
            {
                const std::lock_guard lock(
                    shared->qsort_telemetry_mutex);
                ++shared->qsort_operation_snapshot.completed;
                if (!passed)
                    ++shared->qsort_operation_snapshot.failed;
                shared->qsort_operation_snapshot.comparisons += comparisons;
                shared->qsort_operation_snapshot.swaps += swaps;
                shared->qsort_operation_snapshot.wall_ns += elapsed;
                shared->qsort_operation_snapshot.executor_setup_wall_ns +=
                    executor_setup_wall_ns;
                shared->qsort_operation_snapshot.comparator_wall_ns +=
                    comparator_wall_ns;
                shared->qsort_operation_snapshot.swap_wall_ns += swap_wall_ns;
            }
            if (mode != QsortOperationTelemetryMode::kOperationBoundary)
                return;
            std::fprintf(
                stdout,
                "ISOMR_BIONIC_QSORT_OPERATION sequence=%llu state=end result=%s comparisons=%llu swaps=%llu wall-ns=%llu executor-setup-wall-ns=%llu comparator-wall-ns=%llu swap-wall-ns=%llu residual-wall-ns=%llu\n",
                static_cast<unsigned long long>(sequence),
                passed ? "pass" : "fail",
                static_cast<unsigned long long>(comparisons),
                static_cast<unsigned long long>(swaps),
                static_cast<unsigned long long>(elapsed),
                static_cast<unsigned long long>(executor_setup_wall_ns),
                static_cast<unsigned long long>(comparator_wall_ns),
                static_cast<unsigned long long>(swap_wall_ns),
                static_cast<unsigned long long>(residual_wall_ns));
        }
    };
    const QsortOperationTelemetryMode telemetry_mode =
        shared_->qsort_operation_telemetry_mode.load(
            std::memory_order_acquire);
    const bool telemetry_enabled = telemetry_mode !=
        QsortOperationTelemetryMode::kDisabled;
    const std::uint64_t telemetry_sequence =
        telemetry_mode == QsortOperationTelemetryMode::kOperationBoundary
        ? shared_->next_qsort_operation_sequence.fetch_add(
              1, std::memory_order_relaxed) +
              1
        : 0;
    OperationTelemetry telemetry(shared_.get(), telemetry_mode,
                                 telemetry_sequence, count, element_size);
    const auto executor_setup_started = telemetry_enabled
        ? std::chrono::steady_clock::now()
        : std::chrono::steady_clock::time_point{};
    const std::optional<std::uint64_t> thread_handle =
        shared_->thread_state->CurrentErrnoAddress();
    GuestCall executor = thread_handle
                             ? shared_->executor_factory(*thread_handle,
                                                         error)
                             : GuestCall{};
    if (telemetry_enabled) {
        const auto elapsed =
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - executor_setup_started)
                .count();
        if (elapsed > 0)
            telemetry.executor_setup_wall_ns +=
                static_cast<std::uint64_t>(elapsed);
    }
    if (!executor) {
        if (error != nullptr && error->empty())
            *error = "qsort guest comparator executor is unavailable";
        return false;
    }
    std::vector<std::uint8_t> temporary;
    try {
        temporary.resize(static_cast<std::size_t>(element_size));
    } catch (const std::bad_alloc&) {
        SetError(error, "qsort swap buffer allocation failed");
        return false;
    }
    const auto element_address = [&](std::uint64_t index) {
        return base + index * element_size;
    };
    const auto compare = [&](std::uint64_t left, std::uint64_t right,
                             std::int32_t* comparison) {
        if (telemetry_enabled) ++telemetry.comparisons;
        const auto comparator_started = telemetry_enabled
            ? std::chrono::steady_clock::now()
            : std::chrono::steady_clock::time_point{};
        std::array<std::uint64_t, 8> arguments{};
        arguments[0] = element_address(left);
        arguments[1] = element_address(right);
        std::uint64_t comparison_result = 0;
        const bool compared =
            executor(comparator, arguments, &comparison_result, error);
        if (telemetry_enabled) {
            const auto elapsed =
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() - comparator_started)
                    .count();
            if (elapsed > 0)
                telemetry.comparator_wall_ns +=
                    static_cast<std::uint64_t>(elapsed);
        }
        if (!compared) {
            if (error != nullptr && error->empty())
                *error = "qsort guest comparator failed";
            return false;
        }
        *comparison = static_cast<std::int32_t>(
            static_cast<std::uint32_t>(comparison_result));
        return true;
    };
    const auto swap = [&](std::uint64_t left, std::uint64_t right) {
        if (left == right) return true;
        const auto swap_started = telemetry_enabled
            ? std::chrono::steady_clock::now()
            : std::chrono::steady_clock::time_point{};
        const std::uint64_t left_address = element_address(left);
        const std::uint64_t right_address = element_address(right);
        const bool swapped =
            shared_->address_space->Read(left_address, temporary.data(),
                                         temporary.size()) &&
            shared_->address_space->Copy(left_address, right_address,
                                         element_size, error) &&
            shared_->address_space->Write(right_address, temporary.data(),
                                          temporary.size());
        if (telemetry_enabled) {
            const auto elapsed =
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() - swap_started)
                    .count();
            if (elapsed > 0)
                telemetry.swap_wall_ns +=
                    static_cast<std::uint64_t>(elapsed);
        }
        if (!swapped) {
            if (error != nullptr && error->empty())
                *error = "qsort element swap failed";
            return false;
        }
        if (telemetry_enabled) ++telemetry.swaps;
        return true;
    };
    if (shared_->qsort_algorithm.load(std::memory_order_acquire) ==
        QsortAlgorithm::kBionicPartition) {
        const auto median_of_three =
            [&](std::uint64_t first, std::uint64_t middle,
                std::uint64_t last, std::uint64_t* median) {
                std::int32_t comparison = 0;
                if (!compare(first, middle, &comparison)) return false;
                if (comparison < 0) {
                    if (!compare(middle, last, &comparison)) return false;
                    if (comparison < 0) {
                        *median = middle;
                        return true;
                    }
                    if (!compare(first, last, &comparison)) return false;
                    *median = comparison < 0 ? last : first;
                    return true;
                }
                if (!compare(middle, last, &comparison)) return false;
                if (comparison > 0) {
                    *median = middle;
                    return true;
                }
                if (!compare(first, last, &comparison)) return false;
                *median = comparison < 0 ? first : last;
                return true;
            };
        const auto insertion_sort =
            [&](std::uint64_t first, std::uint64_t length) {
                for (std::uint64_t current = first + 1;
                     current < first + length; ++current) {
                    for (std::uint64_t position = current;
                         position > first; --position) {
                        std::int32_t comparison = 0;
                        if (!compare(position - 1, position, &comparison))
                            return false;
                        if (comparison <= 0) break;
                        if (!swap(position - 1, position)) return false;
                    }
                }
                return true;
            };
        const auto swap_ranges =
            [&](std::uint64_t left, std::uint64_t right,
                std::uint64_t length) {
                for (std::uint64_t index = 0; index < length; ++index) {
                    if (!swap(left + index, right + index)) return false;
                }
                return true;
            };
        const auto partition_sort =
            [&](auto&& self, std::uint64_t first,
                std::uint64_t length) -> bool {
                while (length > 1) {
                    if (length < 7)
                        return insertion_sort(first, length);

                    const std::uint64_t last = first + length - 1;
                    std::uint64_t pivot = first + length / 2;
                    if (length > 7) {
                        std::uint64_t low = first;
                        std::uint64_t high = last;
                        if (length > 40) {
                            const std::uint64_t spacing = length / 8;
                            if (!median_of_three(
                                    low, low + spacing,
                                    low + spacing * 2, &low) ||
                                !median_of_three(
                                    pivot - spacing, pivot,
                                    pivot + spacing, &pivot) ||
                                !median_of_three(
                                    high - spacing * 2,
                                    high - spacing, high, &high))
                                return false;
                        }
                        if (!median_of_three(low, pivot, high, &pivot))
                            return false;
                    }
                    if (!swap(first, pivot)) return false;

                    std::uint64_t equal_left = first + 1;
                    std::uint64_t scan_left = first + 1;
                    std::uint64_t scan_right = last;
                    std::uint64_t equal_right = last;
                    bool exchanged = false;
                    for (;;) {
                        while (scan_left <= scan_right) {
                            std::int32_t comparison = 0;
                            if (!compare(scan_left, first, &comparison))
                                return false;
                            if (comparison > 0) break;
                            if (comparison == 0) {
                                exchanged = true;
                                if (!swap(equal_left, scan_left))
                                    return false;
                                ++equal_left;
                            }
                            ++scan_left;
                        }
                        while (scan_left <= scan_right) {
                            std::int32_t comparison = 0;
                            if (!compare(scan_right, first, &comparison))
                                return false;
                            if (comparison < 0) break;
                            if (comparison == 0) {
                                exchanged = true;
                                if (!swap(scan_right, equal_right))
                                    return false;
                                --equal_right;
                            }
                            --scan_right;
                        }
                        if (scan_left > scan_right) break;
                        if (!swap(scan_left, scan_right)) return false;
                        exchanged = true;
                        ++scan_left;
                        --scan_right;
                    }
                    if (!exchanged)
                        return insertion_sort(first, length);

                    const std::uint64_t left_equal_count = std::min(
                        equal_left - first, scan_left - equal_left);
                    if (!swap_ranges(first, scan_left - left_equal_count,
                                     left_equal_count))
                        return false;
                    const std::uint64_t right_equal_count = std::min(
                        equal_right - scan_right,
                        first + length - (equal_right + 1));
                    if (!swap_ranges(scan_left,
                                     first + length - right_equal_count,
                                     right_equal_count))
                        return false;

                    const std::uint64_t left_length =
                        scan_left - equal_left;
                    const std::uint64_t right_length =
                        equal_right - scan_right;
                    const std::uint64_t right_first =
                        first + length - right_length;
                    if (left_length < right_length) {
                        if (left_length > 1 &&
                            !self(self, first, left_length))
                            return false;
                        first = right_first;
                        length = right_length;
                    } else {
                        if (right_length > 1 &&
                            !self(self, right_first, right_length))
                            return false;
                        length = left_length;
                    }
                }
                return true;
            };
        const bool partition_ok =
            partition_sort(partition_sort, 0, count);
        telemetry.passed = partition_ok;
        return partition_ok;
    }
    const auto sift_down = [&](std::uint64_t root,
                               std::uint64_t end) {
        if (end == 0) return true;
        while (root <= (end - 1) / 2) {
            const std::uint64_t child = root * 2 + 1;
            std::uint64_t maximum = root;
            std::int32_t comparison = 0;
            if (!compare(maximum, child, &comparison)) return false;
            if (comparison < 0) maximum = child;
            if (child < end) {
                if (!compare(maximum, child + 1, &comparison)) return false;
                if (comparison < 0) maximum = child + 1;
            }
            if (maximum == root) return true;
            if (!swap(root, maximum)) return false;
            root = maximum;
        }
        return true;
    };
    for (std::uint64_t start = count / 2; start != 0; --start) {
        if (!sift_down(start - 1, count - 1)) return false;
    }
    for (std::uint64_t end = count - 1; end != 0; --end) {
        if (!swap(0, end) || !sift_down(0, end - 1)) return false;
    }
    telemetry.passed = true;
    return true;
}

bool BionicThreads::ConfigureQsortOperationTelemetry(
    QsortOperationTelemetryMode mode, std::string* error) {
    (void)error;
    shared_->qsort_operation_telemetry_mode.store(mode,
                                                  std::memory_order_release);
    return true;
}

bool BionicThreads::TakeQsortOperationSnapshot(
    QsortOperationSnapshot* snapshot) const {
    if (snapshot == nullptr) return false;
    const std::lock_guard lock(shared_->qsort_telemetry_mutex);
    *snapshot = shared_->qsort_operation_snapshot;
    return true;
}

bool BionicThreads::ConfigureQsortAlgorithm(
    QsortAlgorithm algorithm, std::string* error) {
    (void)error;
    shared_->qsort_algorithm.store(algorithm, std::memory_order_release);
    return true;
}

void BionicThreads::CleanupRecord(
    const std::shared_ptr<SharedState>& shared,
    const std::shared_ptr<ThreadRecord>& record) {
    {
        const std::lock_guard record_lock(record->mutex);
        if (record->cleaned) return;
        record->cleaned = true;
    }
    {
        const std::lock_guard shared_lock(shared->mutex);
        const auto found = shared->threads.find(record->handle);
        if (found != shared->threads.end() && found->second == record)
            shared->threads.erase(found);
    }
    if (shared->heap != nullptr) {
        std::string ignored;
        (void)shared->heap->Release(record->handle, &ignored);
    }
}

void BionicThreads::WorkerMain(
    const std::shared_ptr<SharedState>& shared,
    const std::shared_ptr<ThreadRecord>& record) {
    {
        const std::lock_guard lock(record->mutex);
        record->host_thread_hash = static_cast<std::uint64_t>(
            std::hash<std::thread::id>{}(std::this_thread::get_id()));
        record->host_thread_id = GetCurrentThreadId();
    }
    const bool capture_host_handle =
        shared->worker_cpu_sampling_enabled.load(std::memory_order_acquire) ||
        [] {
            wchar_t flag[4] = {};
            return GetEnvironmentVariableW(L"ISOMR_THREAD_SAMPLE", flag, 4) > 0 &&
                   flag[0] != L'0';
        }();
    if (capture_host_handle) {
        HANDLE cpu_thread_handle = nullptr;
        (void)DuplicateHandle(GetCurrentProcess(), GetCurrentThread(),
                              GetCurrentProcess(), &cpu_thread_handle, 0,
                              FALSE, DUPLICATE_SAME_ACCESS);
        const std::lock_guard lock(record->mutex);
        record->cpu_thread_handle = cpu_thread_handle;
    }
    std::string error;
    bool ok = shared->thread_state != nullptr && shared->tls != nullptr &&
              shared->executor_factory &&
              shared->thread_state->AttachCurrentThreadAt(record->handle,
                                                           &error);
    bool hook_attached = false;
    if (ok && shared->attach_hook) {
        ok = shared->attach_hook(&error);
        hook_attached = ok;
    }
    GuestCall executor;
    if (ok) {
        GuestExecutorContext context;
        context.thread_handle = record->handle;
        context.ordinal = record->ordinal;
        context.start_group = record->start_group;
        context.execution_slice_ticks = record->execution_slice_ticks;
        context.worker_execution = true;
        context.execution_telemetry = record->execution_telemetry;
        context.process_exit_cancellation_requested =
            [requested = record->process_exit_cancellation_requested] {
                return requested->load(std::memory_order_acquire);
            };
        context.process_exit_cancellation_observed =
            [observed = record->process_exit_cancellation_observed] {
                observed->store(true, std::memory_order_release);
            };
        executor = shared->executor_factory(std::move(context), &error);
        ok = static_cast<bool>(executor);
    }
    std::uint64_t result = 0;
    std::array<std::uint64_t, 8> arguments{};
    arguments[0] = record->argument;
    if (ok) {
        std::uint64_t ctx_head = 0;
        if (shared->address_space != nullptr && record->argument != 0) {
            (void)shared->address_space->Read(record->argument, &ctx_head,
                                              sizeof(ctx_head));
        }
        std::fprintf(stdout,
            "ISOMR_CTR_THREAD_BEGIN start=0x%llx arg=0x%llx head=0x%llx tid=%u\n",
            (unsigned long long)record->start_routine,
            (unsigned long long)record->argument,
            (unsigned long long)ctx_head,
            static_cast<unsigned>(
                std::hash<std::thread::id>{}(std::this_thread::get_id())));
        ok = executor(record->start_routine, arguments, &result, &error);
        if (!ok && shared->address_space != nullptr &&
            record->argument != 0) {
            (void)shared->address_space->Read(record->argument, &ctx_head,
                                              sizeof(ctx_head));
            std::fprintf(stdout,
                "ISOMR_CTR_THREAD_END ctx-head=0x%llx\n",
                (unsigned long long)ctx_head);
        }
        std::fprintf(stdout,
            "ISOMR_CTR_THREAD_END start=0x%llx ok=%d error=%s\n",
            (unsigned long long)record->start_routine, ok ? 1 : 0,
            error.c_str());
    }

    bool process_exit_cancelled = false;
    const auto accept_process_exit_cancellation = [&] {
        if (!ok && record->process_exit_cancellation_observed->load(
                       std::memory_order_acquire)) {
            process_exit_cancelled = true;
            ok = true;
            error.clear();
        }
    };
    accept_process_exit_cancellation();

    if (ok && !process_exit_cancelled) {
        for (std::size_t iteration = 0; iteration < kDestructorIterations;
             ++iteration) {
            const std::vector<BionicTlsDestructorCall> calls =
                shared->tls->DrainCurrentDestructorPass();
            if (calls.empty()) break;
            for (const BionicTlsDestructorCall& call : calls) {
                std::uint64_t ignored_result = 0;
                arguments = {};
                arguments[0] = call.value;
                if (!executor(call.function, arguments, &ignored_result,
                              &error)) {
                    ok = false;
                    break;
                }
                if (!shared->tls->CompleteCurrentDestructorCall()) {
                    error =
                        "guest worker TLS destructor completion state is invalid";
                    ok = false;
                    break;
                }
            }
            if (!ok) break;
        }
    }
    accept_process_exit_cancellation();

    if (shared->tls != nullptr &&
        !shared->tls->ClearCurrentThreadValues(&error))
        ok = false;
    if (hook_attached && shared->detach_hook &&
        !shared->detach_hook(&error))
        ok = false;
    if (shared->thread_state != nullptr &&
        !shared->thread_state->DetachCurrentThread(false, &error))
        ok = false;
    executor = {};

    std::string published_failure;
    if (!ok) {
        std::ostringstream message;
        message << "guest worker handle=0x" << std::hex << record->handle
                << " start=0x" << record->start_routine << std::dec
                << " failed: "
                << (error.empty() ? "unspecified execution failure" : error);
        published_failure = message.str();
    }
    bool detached = false;
    {
        const std::lock_guard lock(record->mutex);
        record->result = result;
        record->execution_ok = ok;
        record->error = std::move(error);
        record->completed = true;
        record->process_exit_cancelled = process_exit_cancelled;
        detached = record->detached;
    }
    if (detached) CleanupRecord(shared, record);
    {
        const std::lock_guard lock(shared->mutex);
        if (!published_failure.empty() &&
            shared->first_completed_worker_failure.empty()) {
            shared->first_completed_worker_failure =
                std::move(published_failure);
        }
        --shared->active_workers;
    }
    shared->workers_changed.notify_all();
}

int BionicThreads::Create(std::uint64_t output_address,
                          std::uint64_t attributes,
                          std::uint64_t start_routine,
                          std::uint64_t argument) {
    // Attribute objects are accepted: callers may prepare one through
    // pthread_attr_init/setdetachstate/setstacksize. The runtime applies
    // its own worker stack and scheduling defaults regardless of contents.
    (void)attributes;
    if (start_routine == 0) return kInvalid;
    if (shared_->heap == nullptr || shared_->address_space == nullptr ||
        shared_->thread_state == nullptr || shared_->tls == nullptr ||
        !shared_->executor_factory) {
        std::fprintf(stderr,
                     "ISOMR_PTHREAD_CREATE_FAIL start=0x%llx reason=runtime-missing\n",
                     (unsigned long long)start_routine);
        return kInvalid;
    }
    const std::uint64_t zero = 0;
    if (!shared_->address_space->Write(output_address, &zero, sizeof(zero)))
        return kBadAddress;
    {
        const std::lock_guard lock(shared_->mutex);
        if (shared_->process_exit_cleanup_started) {
            std::fprintf(stderr,
                         "ISOMR_PTHREAD_CREATE_FAIL start=0x%llx reason=exiting\n",
                         (unsigned long long)start_routine);
            return kTryAgain;
        }
    }
    const std::uint64_t handle =
        shared_->heap->Callocate(1, BionicThreadState::kStorageSize);
    if (handle == 0) {
        std::fprintf(stderr,
                     "ISOMR_PTHREAD_CREATE_FAIL start=0x%llx reason=storage-alloc\n",
                     (unsigned long long)start_routine);
        return kTryAgain;
    }
    if (!shared_->worker_cpu_sampling_enabled.load(
            std::memory_order_acquire)) {
        std::shared_ptr<ThreadRecord> record;
        {
            const std::lock_guard lock(shared_->mutex);
            if (shared_->process_exit_cleanup_started) {
                std::string ignored;
                (void)shared_->heap->Release(handle, &ignored);
                return kTryAgain;
            }
            shared_->worker_creation_started = true;
            record = std::make_shared<ThreadRecord>(
                handle, shared_->next_worker_ordinal++);
            record->start_routine = start_routine;
            record->argument = argument;
            shared_->threads.emplace(handle, record);
            ++shared_->active_workers;
        }
        if (!shared_->address_space->Write(output_address, &handle,
                                            sizeof(handle))) {
            {
                const std::lock_guard lock(shared_->mutex);
                shared_->threads.erase(handle);
                --shared_->active_workers;
            }
            std::string ignored;
            (void)shared_->heap->Release(handle, &ignored);
            return kBadAddress;
        }
        try {
            record->worker =
                std::thread(&BionicThreads::WorkerMain, shared_, record);
        } catch (...) {
            {
                const std::lock_guard lock(shared_->mutex);
                shared_->threads.erase(handle);
                --shared_->active_workers;
            }
            (void)shared_->address_space->Write(output_address, &zero,
                                                 sizeof(zero));
            std::string ignored;
            (void)shared_->heap->Release(handle, &ignored);
            return kTryAgain;
        }
        return kSuccess;
    }

    // Enabled creation is one bounded publication transaction. A worker may
    // start and briefly wait for this lock before observing shared state.
    std::shared_ptr<ThreadRecord> record;
    bool published_record = false;
    bool appended_group = false;
    int failure = kTryAgain;
    {
        std::unique_lock lock(shared_->mutex);
        if (shared_->process_exit_cleanup_started) {
            lock.unlock();
            std::string ignored;
            (void)shared_->heap->Release(handle, &ignored);
            return kTryAgain;
        }
        shared_->worker_creation_started = true;
        try {
            const std::uint64_t ordinal = shared_->next_worker_ordinal++;
            std::uint64_t start_group = ordinal;
            bool found_group = false;
            for (const SharedState::WorkerStartGroup& group :
                 shared_->worker_start_groups) {
                if (group.start_routine == start_routine) {
                    start_group = group.first_ordinal;
                    found_group = true;
                    break;
                }
            }
            record = std::make_shared<ThreadRecord>(handle, ordinal,
                                                    start_group);
            if (shared_->worker_execution_sampling_enabled && ordinal < 2) {
                record->execution_slice_ticks =
                    shared_->worker_execution_slice_ticks;
                record->execution_telemetry =
                    std::make_shared<WorkerExecutionTelemetry>();
            }
            record->start_routine = start_routine;
            record->argument = argument;
            if (!found_group) {
                shared_->worker_start_groups.push_back(
                    {start_routine, ordinal});
                appended_group = true;
            }
            shared_->threads.emplace(handle, record);
            ++shared_->active_workers;
            published_record = true;
            if (!shared_->address_space->Write(output_address, &handle,
                                                sizeof(handle))) {
                failure = kBadAddress;
            } else {
                record->worker = std::thread(&BionicThreads::WorkerMain,
                                             shared_, record);
                return kSuccess;
            }
        } catch (...) {
            (void)shared_->address_space->Write(output_address, &zero,
                                                 sizeof(zero));
        }
        if (published_record) {
            shared_->threads.erase(handle);
            --shared_->active_workers;
        }
        if (appended_group) shared_->worker_start_groups.pop_back();
    }
    std::string ignored;
    (void)shared_->heap->Release(handle, &ignored);
    return failure;
}

int BionicThreads::Join(std::uint64_t thread,
                        std::uint64_t result_address) {
    if (shared_->address_space == nullptr) return kInvalid;
    const std::optional<std::uint64_t> current =
        shared_->thread_state == nullptr
            ? std::nullopt
            : shared_->thread_state->CurrentErrnoAddress();
    if (current && *current == thread) return kDeadlock;
    if (result_address != 0) {
        std::uint64_t probe = 0;
        if (!shared_->address_space->Read(result_address, &probe,
                                           sizeof(probe)) ||
            !shared_->address_space->Write(result_address, &probe,
                                            sizeof(probe)))
            return kBadAddress;
    }
    std::shared_ptr<ThreadRecord> record;
    {
        const std::lock_guard lock(shared_->mutex);
        const auto found = shared_->threads.find(thread);
        if (found == shared_->threads.end()) return kInvalid;
        record = found->second;
    }
    {
        const std::lock_guard lock(record->mutex);
        if (record->detached || record->join_claimed) return kInvalid;
        record->join_claimed = true;
    }
    if (!record->worker.joinable()) return kInvalid;
    record->worker.join();
    std::uint64_t result = 0;
    bool execution_ok = false;
    {
        const std::lock_guard lock(record->mutex);
        result = record->result;
        execution_ok = record->execution_ok;
    }
    int status = execution_ok ? kSuccess : kIoError;
    if (result_address != 0 &&
        !shared_->address_space->Write(result_address, &result,
                                        sizeof(result)))
        status = kBadAddress;
    CleanupRecord(shared_, record);
    return status;
}

int BionicThreads::Detach(std::uint64_t thread) {
    std::shared_ptr<ThreadRecord> record;
    {
        const std::lock_guard lock(shared_->mutex);
        const auto found = shared_->threads.find(thread);
        if (found == shared_->threads.end()) return kInvalid;
        record = found->second;
    }
    bool completed = false;
    {
        const std::lock_guard lock(record->mutex);
        if (record->detached || record->join_claimed ||
            !record->worker.joinable())
            return kInvalid;
        record->detached = true;
        completed = record->completed;
        record->worker.detach();
    }
    if (completed) CleanupRecord(shared_, record);
    return kSuccess;
}

std::size_t BionicThreads::thread_count() const {
    const std::lock_guard lock(shared_->mutex);
    return shared_->threads.size();
}

std::size_t BionicThreads::active_count() const {
    const std::lock_guard lock(shared_->mutex);
    return shared_->active_workers;
}

bool BionicThreads::CancelAndReapWorkersForProcessExit(
    ProcessExitWakeBlockedCalls wake_blocked_calls,
    std::chrono::milliseconds timeout,
    ProcessExitWorkerCleanup* cleanup, std::string* error) {
    if (!wake_blocked_calls || cleanup == nullptr ||
        timeout < std::chrono::milliseconds::zero()) {
        SetError(error, "process-exit worker cleanup arguments are invalid");
        return false;
    }
    *cleanup = {};

    std::vector<std::shared_ptr<ThreadRecord>> records;
    {
        const std::lock_guard lock(shared_->mutex);
        if (shared_->process_exit_cleanup_started) {
            SetError(error, "process-exit worker cleanup already started");
            return false;
        }
        shared_->process_exit_cleanup_started = true;
        records.reserve(shared_->threads.size());
        for (const auto& [handle, record] : shared_->threads) {
            (void)handle;
            records.push_back(record);
        }
    }
    for (const std::shared_ptr<ThreadRecord>& record : records) {
        record->process_exit_cancellation_requested->store(
            true, std::memory_order_release);
        ++cleanup->cancellation_requests;
    }
    try {
        cleanup->blocked_calls_woken = wake_blocked_calls();
    } catch (...) {
        SetError(error, "process-exit blocked-call wake threw an exception");
        return false;
    }

    bool workers_stopped = false;
    {
        std::unique_lock lock(shared_->mutex);
        workers_stopped = shared_->workers_changed.wait_for(
            lock, timeout,
            [&] { return shared_->active_workers == 0; });
    }
    if (!workers_stopped) {
        SetError(error, "process-exit worker cancellation timed out: " +
                            DescribeThreads());
        return false;
    }

    for (const std::shared_ptr<ThreadRecord>& record : records) {
        const std::lock_guard lock(record->mutex);
        if (record->process_exit_cancelled)
            ++cleanup->cancellation_observed;
    }

    std::vector<std::shared_ptr<ThreadRecord>> joinable;
    {
        const std::lock_guard lock(shared_->mutex);
        joinable.reserve(shared_->threads.size());
        for (const auto& [handle, record] : shared_->threads) {
            (void)handle;
            joinable.push_back(record);
        }
    }
    for (const std::shared_ptr<ThreadRecord>& record : joinable) {
        {
            const std::lock_guard lock(record->mutex);
            if (!record->completed || record->detached ||
                record->join_claimed || !record->worker.joinable()) {
                SetError(error,
                         "process-exit worker record is not safely joinable");
                return false;
            }
            record->join_claimed = true;
        }
        record->worker.join();
        CleanupRecord(shared_, record);
        ++cleanup->workers_reaped;
    }
    {
        const std::lock_guard lock(shared_->mutex);
        if (shared_->active_workers != 0 || !shared_->threads.empty()) {
            SetError(error,
                     "process-exit worker cleanup left live records");
            return false;
        }
    }
    return true;
}

void BionicThreads::RequestProcessExit(int exit_code) noexcept {
    shared_->process_exit_code.store(exit_code, std::memory_order_relaxed);
    shared_->process_exit_requested.store(true, std::memory_order_release);
    const std::lock_guard lock(shared_->mutex);
    for (const auto& [handle, record] : shared_->threads) {
        (void)handle;
        if (record != nullptr)
            record->process_exit_cancellation_requested->store(
                true, std::memory_order_release);
    }
}

bool BionicThreads::process_exit_requested() const noexcept {
    return shared_->process_exit_requested.load(std::memory_order_acquire);
}

int BionicThreads::process_exit_code() const noexcept {
    return shared_->process_exit_code.load(std::memory_order_relaxed);
}

bool BionicThreads::CheckCompletedWorkerFailure(std::string* error) const {
    const std::lock_guard lock(shared_->mutex);
    if (shared_->first_completed_worker_failure.empty()) return true;
    SetError(error, shared_->first_completed_worker_failure);
    return false;
}

bool BionicThreads::AcknowledgeCompletedWorkerFailure(std::string* error) {
    const std::lock_guard lock(shared_->mutex);
    if (shared_->first_completed_worker_failure.empty()) return false;
    if (error != nullptr) *error = shared_->first_completed_worker_failure;
    shared_->first_completed_worker_failure.clear();
    return true;
}

bool BionicThreads::RunManagedGuestLoop(ManagedGuestLoop loop,
                                        std::string* error) {
    if (!loop || shared_->heap == nullptr || shared_->thread_state == nullptr ||
        shared_->tls == nullptr || !shared_->executor_factory) {
        SetError(error, "managed guest loop configuration is invalid");
        return false;
    }
    const std::uint64_t handle =
        shared_->heap->Callocate(1, BionicThreadState::kStorageSize);
    if (handle == 0) {
        SetError(error, "managed guest loop thread-state allocation failed");
        return false;
    }

    bool ok = shared_->thread_state->AttachCurrentThreadAt(handle, error);
    bool hook_attached = false;
    if (ok && shared_->attach_hook) {
        ok = shared_->attach_hook(error);
        hook_attached = ok;
    }
    GuestCall executor;
    if (ok) {
        executor = shared_->executor_factory(GuestExecutorContext{handle},
                                             error);
        ok = static_cast<bool>(executor);
    }
    if (ok) ok = loop(executor, error);

    if (ok) {
        std::array<std::uint64_t, 8> arguments{};
        for (std::size_t iteration = 0; iteration < kDestructorIterations;
             ++iteration) {
            const std::vector<BionicTlsDestructorCall> calls =
                shared_->tls->DrainCurrentDestructorPass();
            if (calls.empty()) break;
            for (const BionicTlsDestructorCall& call : calls) {
                std::uint64_t ignored_result = 0;
                arguments = {};
                arguments[0] = call.value;
                if (!executor(call.function, arguments, &ignored_result,
                              error)) {
                    ok = false;
                    break;
                }
                if (!shared_->tls->CompleteCurrentDestructorCall()) {
                    SetError(error,
                             "managed guest loop TLS destructor completion "
                             "state is invalid");
                    ok = false;
                    break;
                }
            }
            if (!ok) break;
        }
    }

    if (!shared_->tls->ClearCurrentThreadValues(error)) ok = false;
    if (hook_attached && shared_->detach_hook &&
        !shared_->detach_hook(error))
        ok = false;
    if (!shared_->thread_state->DetachCurrentThread(false, error)) ok = false;
    executor = {};
    std::string release_error;
    if (!shared_->heap->Release(handle, &release_error)) {
        if (ok) SetError(error, std::move(release_error));
        ok = false;
    }

    if (!ok) {
        std::string message = "managed guest loop failed: ";
        message += error != nullptr && !error->empty()
                       ? *error
                       : "unspecified execution failure";
        const std::lock_guard lock(shared_->mutex);
        if (shared_->first_completed_worker_failure.empty())
            shared_->first_completed_worker_failure = std::move(message);
    }
    return ok;
}

std::string BionicThreads::DescribeThreads() const {
    std::vector<std::shared_ptr<ThreadRecord>> records;
    {
        const std::lock_guard lock(shared_->mutex);
        records.reserve(shared_->threads.size());
        for (const auto& entry : shared_->threads)
            records.push_back(entry.second);
    }
    std::sort(records.begin(), records.end(),
              [](const std::shared_ptr<ThreadRecord>& left,
                 const std::shared_ptr<ThreadRecord>& right) {
                  return left->handle < right->handle;
              });
    std::ostringstream description;
    description << "count" << records.size();
    for (const std::shared_ptr<ThreadRecord>& record : records) {
        const std::lock_guard lock(record->mutex);
        description << "|handle0x" << std::hex << record->handle
                    << "-start0x" << record->start_routine
                    << "-host0x" << record->host_thread_hash << std::dec
                    << "-state" << (record->completed ? "complete" : "active")
                    << "-execution";
        if (!record->completed) {
            description << "pending";
        } else {
            description << (record->execution_ok ? "pass" : "fail");
        }
        description << "-detached" << (record->detached ? 1 : 0)
                    << "-join" << (record->join_claimed ? 1 : 0);
        if (!record->completed) {
            const BionicThreads::WorkerHostResidency residency =
                SampleWorkerHostResidency(record->cpu_thread_handle,
                                          record->host_thread_id);
            const char* residency_name = "unavailable";
            switch (residency) {
                case BionicThreads::WorkerHostResidency::
                    kGeneratedPrivateExecutable:
                    residency_name = "jit";
                    break;
                case BionicThreads::WorkerHostResidency::kArmrMainImage:
                    residency_name = "host";
                    break;
                case BionicThreads::WorkerHostResidency::kOtherImage:
                    residency_name = "other";
                    break;
                case BionicThreads::WorkerHostResidency::kNonExecutable:
                    residency_name = "nonexec";
                    break;
                case BionicThreads::WorkerHostResidency::kSampleFailed:
                    residency_name = "samplefail";
                    break;
                case BionicThreads::WorkerHostResidency::kUnavailable:
                default:
                    break;
            }
            description << "-residency" << residency_name;
        }
        if (record->completed && !record->execution_ok) {
            std::string failure = record->error.substr(0, 512);
            for (char& character : failure) {
                if (character == '|' || character == '\r' ||
                    character == '\n')
                    character = '_';
            }
            description << "-error"
                        << (failure.empty() ? "unspecified" : failure);
            if (record->error.size() > failure.size())
                description << "-truncated1";
        }
    }
    return description.str();
}

bool BionicThreads::ConfigureWorkerCpuSampling(
    WorkerCpuSamplingMode mode, std::string* error) {
    const std::lock_guard lock(shared_->mutex);
    if (shared_->worker_creation_started) {
        SetError(error,
                 "worker CPU sampling must be configured before worker creation");
        return false;
    }
    shared_->worker_cpu_sampling_enabled.store(
        mode == WorkerCpuSamplingMode::kEnabled,
        std::memory_order_release);
    return true;
}

bool BionicThreads::TakeWorkerCpuSnapshot(
    WorkerCpuSnapshot* snapshot) const {
    if (snapshot == nullptr ||
        !shared_->worker_cpu_sampling_enabled.load(std::memory_order_acquire))
        return false;

    std::array<std::shared_ptr<ThreadRecord>,
               kMaxWorkerCpuSnapshotRecords>
        selected{};
    std::size_t selected_count = 0;
    std::size_t total = 0;
    {
        const std::lock_guard lock(shared_->mutex);
        total = shared_->threads.size();
        for (const auto& entry : shared_->threads) {
            const std::shared_ptr<ThreadRecord>& candidate = entry.second;
            std::size_t position = selected_count;
            if (position == selected.size()) {
                if (candidate->ordinal >= selected[position - 1]->ordinal)
                    continue;
                --position;
            } else {
                ++selected_count;
            }
            while (position != 0 &&
                   candidate->ordinal < selected[position - 1]->ordinal) {
                if (position < selected.size())
                    selected[position] = selected[position - 1];
                --position;
            }
            selected[position] = candidate;
        }
    }

    WorkerCpuSnapshot result;
    result.total = total;
    result.sampled = selected_count;
    result.dropped = total - selected_count;
    result.records.reserve(selected_count);
    for (std::size_t index = 0; index < selected_count; ++index) {
        const std::shared_ptr<ThreadRecord>& record = selected[index];
        WorkerCpuRecord sample;
        sample.ordinal = record->ordinal;
        {
            const std::lock_guard lock(record->mutex);
            sample.start_group = record->start_group;
            sample.lifecycle = record->completed
                                   ? WorkerLifecycleState::kCompleted
                                   : WorkerLifecycleState::kActive;
            FILETIME creation{};
            FILETIME exit{};
            FILETIME kernel{};
            FILETIME user{};
            if (record->cpu_thread_handle != nullptr &&
                GetThreadTimes(record->cpu_thread_handle, &creation, &exit,
                               &kernel, &user)) {
                ULARGE_INTEGER kernel_units{};
                kernel_units.LowPart = kernel.dwLowDateTime;
                kernel_units.HighPart = kernel.dwHighDateTime;
                ULARGE_INTEGER user_units{};
                user_units.LowPart = user.dwLowDateTime;
                user_units.HighPart = user.dwHighDateTime;
                sample.cpu_available = true;
                sample.cpu_100ns =
                    kernel_units.QuadPart >
                            (std::numeric_limits<std::uint64_t>::max)() -
                                user_units.QuadPart
                        ? (std::numeric_limits<std::uint64_t>::max)()
                        : kernel_units.QuadPart + user_units.QuadPart;
            }
        }
        result.records.push_back(sample);
    }
    *snapshot = std::move(result);
    return true;
}

bool BionicThreads::ConfigureWorkerExecutionSampling(
    WorkerExecutionSamplingMode mode,
    const std::vector<std::uint64_t>& ordinals,
    std::uint64_t slice_ticks, std::string* error) {
    const std::lock_guard lock(shared_->mutex);
    if (shared_->worker_creation_started) {
        SetError(error,
                 "worker execution sampling must be configured before worker creation");
        return false;
    }
    if (shared_->worker_execution_sampling_configured) {
        SetError(error, "worker execution sampling is already configured");
        return false;
    }
    const bool enabled = mode == WorkerExecutionSamplingMode::kEnabled;
    if (enabled && !shared_->worker_cpu_sampling_enabled.load(
                       std::memory_order_acquire)) {
        SetError(error, "worker execution sampling requires worker CPU sampling");
        return false;
    }
    if (enabled && (ordinals.size() != 2 || ordinals[0] != 0 ||
                    ordinals[1] != 1)) {
        SetError(error,
                 "worker execution sampling selector must be ordinals:0,1");
        return false;
    }
    if (!enabled && !ordinals.empty()) {
        SetError(error, "disabled worker execution sampling has a selector");
        return false;
    }
    if (!enabled && slice_ticks != 0) {
        SetError(error,
                 "disabled worker execution sampling has slice ticks");
        return false;
    }
    shared_->worker_execution_sampling_enabled = enabled;
    shared_->worker_execution_slice_ticks = slice_ticks;
    shared_->worker_execution_sampling_configured = true;
    return true;
}

bool BionicThreads::TakeWorkerExecutionSnapshot(
    WorkerExecutionSnapshot* snapshot) const {
    if (snapshot == nullptr) return false;
    std::array<std::pair<std::uint64_t, std::shared_ptr<ThreadRecord>>, 2>
        selected{};
    std::size_t count = 0;
    {
        const std::lock_guard lock(shared_->mutex);
        if (!shared_->worker_execution_sampling_enabled) return false;
        for (const auto& entry : shared_->threads) {
            if (entry.second->execution_telemetry && count < selected.size())
                selected[count++] = {entry.second->ordinal, entry.second};
        }
    }
    std::sort(selected.begin(), selected.begin() + count,
              [](const auto& left, const auto& right) {
                  return left.first < right.first;
              });
    WorkerExecutionSnapshot result;
    result.records.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        const auto& record = selected[index].second;
        WorkerExecutionTelemetrySnapshot internal;
        if (!record->execution_telemetry->TakeSnapshot(&internal)) continue;
        WorkerExecutionRecord output;
        output.ordinal = record->ordinal;
        output.start_group = record->start_group;
        output.phase = internal.phase;
        output.sample_sequence = internal.sample_sequence;
        HANDLE host_thread_handle = nullptr;
        DWORD host_thread_id = 0;
        bool completed = false;
        {
            const std::lock_guard lock(record->mutex);
            host_thread_handle = record->cpu_thread_handle;
            host_thread_id = record->host_thread_id;
            completed = record->completed;
        }
        if (!completed) {
            output.host_residency = SampleWorkerHostResidency(
                host_thread_handle, host_thread_id);
        }
        if (internal.pc_valid) {
            const std::lock_guard lock(shared_->mutex);
            const auto found = std::find(shared_->worker_pc_groups.begin(),
                                         shared_->worker_pc_groups.end(),
                                         internal.raw_pc);
            if (found != shared_->worker_pc_groups.end()) {
                output.pc_group_state = WorkerPcGroupState::kAvailable;
                output.pc_group = static_cast<std::uint64_t>(
                    found - shared_->worker_pc_groups.begin());
            } else if (shared_->worker_pc_groups.size() < kMaxWorkerPcGroups) {
                output.pc_group_state = WorkerPcGroupState::kAvailable;
                output.pc_group = shared_->worker_pc_groups.size();
                shared_->worker_pc_groups.push_back(internal.raw_pc);
            } else {
                output.pc_group_state = WorkerPcGroupState::kOverflow;
            }
        }
        result.records.push_back(output);
    }
    *snapshot = std::move(result);
    return true;
}

bool BionicThreads::ResolveWorkerExecutionGuestPcLocalOnly(
    const WorkerExecutionRecord& record, std::uint64_t* guest_pc) const {
    if (guest_pc == nullptr ||
        record.pc_group_state != WorkerPcGroupState::kAvailable)
        return false;
    const std::lock_guard lock(shared_->mutex);
    if (!shared_->worker_execution_sampling_enabled ||
        record.pc_group >= shared_->worker_pc_groups.size())
        return false;
    *guest_pc = shared_->worker_pc_groups[record.pc_group];
    return true;
}

bool RegisterBionicThreadLifecycleImports(Aarch64ImportRegistry* imports,
                                          BionicThreads* threads,
                                          std::string* error) {
    if (imports == nullptr || threads == nullptr) {
        SetError(error, "bionic thread lifecycle import dependencies are null");
        return false;
    }
    if (!imports->Register(
            "bsearch",
            [threads](Aarch64ImportRegistry::Registers* registers,
                      std::string* dispatch_error) {
                std::uint64_t result = 0;
                if (!threads->BinarySearch(
                        (*registers)[0], (*registers)[1], (*registers)[2],
                        (*registers)[3], (*registers)[4], &result,
                        dispatch_error))
                    return false;
                (*registers)[0] = result;
                return true;
            },
            error) ||
        !imports->Register(
            "qsort",
            [threads](Aarch64ImportRegistry::Registers* registers,
                      std::string* dispatch_error) {
                if (!threads->Sort((*registers)[0], (*registers)[1],
                                   (*registers)[2], (*registers)[3],
                                   dispatch_error))
                    return false;
                (*registers)[0] = 0;
                return true;
            },
            error) ||
        !imports->Register(
            "pthread_once",
            [threads](Aarch64ImportRegistry::Registers* registers,
                      std::string* dispatch_error) {
                int result = kInvalid;
                if (!threads->Once((*registers)[0], (*registers)[1],
                                   &result, dispatch_error))
                    return false;
                (*registers)[0] = static_cast<std::uint64_t>(result);
                return true;
            },
            error) ||
        !imports->Register(
            "pthread_attr_init",
            [threads](Aarch64ImportRegistry::Registers* registers,
                      std::string* dispatch_error) {
                int result = kInvalid;
                if (!threads->AttrInit((*registers)[0], &result,
                                       dispatch_error))
                    return false;
                (*registers)[0] = static_cast<std::uint64_t>(result);
                return true;
            },
            error) ||
        !imports->Register(
            "pthread_attr_setdetachstate",
            [threads](Aarch64ImportRegistry::Registers* registers,
                      std::string* dispatch_error) {
                int result = kInvalid;
                if (!threads->AttrSetDetachState((*registers)[0],
                                                 static_cast<int>(
                                                     (*registers)[1]),
                                                 &result, dispatch_error))
                    return false;
                (*registers)[0] = static_cast<std::uint64_t>(result);
                return true;
            },
            error) ||
        !imports->Register(
            "pthread_attr_setstacksize",
            [threads](Aarch64ImportRegistry::Registers* registers,
                      std::string* dispatch_error) {
                int result = kInvalid;
                if (!threads->AttrSetStackSize((*registers)[0],
                                               (*registers)[1], &result,
                                               dispatch_error))
                    return false;
                (*registers)[0] = static_cast<std::uint64_t>(result);
                return true;
            },
            error) ||
        !imports->Register(
            "pthread_getattr_np",
            [threads](Aarch64ImportRegistry::Registers* registers,
                      std::string* dispatch_error) {
                int result = kInvalid;
                if (!threads->AttrGetNp((*registers)[0], (*registers)[1],
                                        &result, dispatch_error))
                    return false;
                (*registers)[0] = static_cast<std::uint64_t>(result);
                return true;
            },
            error) ||
        !imports->Register(
            "pthread_attr_getstack",
            [threads](Aarch64ImportRegistry::Registers* registers,
                      std::string* dispatch_error) {
                int result = kInvalid;
                if (!threads->AttrGetStack((*registers)[0], (*registers)[1],
                                           (*registers)[2], &result,
                                           dispatch_error))
                    return false;
                (*registers)[0] = static_cast<std::uint64_t>(result);
                return true;
            },
            error) ||
        !imports->Register(
            "pthread_attr_destroy",
            [threads](Aarch64ImportRegistry::Registers* registers,
                      std::string* dispatch_error) {
                int result = kInvalid;
                if (!threads->AttrDestroy((*registers)[0], &result,
                                          dispatch_error))
                    return false;
                (*registers)[0] = static_cast<std::uint64_t>(result);
                return true;
            },
            error) ||
        !imports->Register(
            "pthread_attr_getschedparam",
            [threads](Aarch64ImportRegistry::Registers* registers,
                      std::string* dispatch_error) {
                int result = kInvalid;
                if (!threads->AttrGetSched((*registers)[0], (*registers)[1],
                                           &result, dispatch_error))
                    return false;
                (*registers)[0] = static_cast<std::uint64_t>(result);
                return true;
            },
            error) ||
        !imports->Register(
            "pthread_attr_getschedpolicy",
            [threads](Aarch64ImportRegistry::Registers* registers,
                      std::string* dispatch_error) {
                int result = kInvalid;
                if (!threads->AttrGetSched((*registers)[0], (*registers)[1],
                                           &result, dispatch_error))
                    return false;
                (*registers)[0] = static_cast<std::uint64_t>(result);
                return true;
            },
            error) ||
        !imports->Register(
            "pthread_attr_setschedpolicy",
            [threads](Aarch64ImportRegistry::Registers* registers,
                      std::string* dispatch_error) {
                int result = kInvalid;
                if (!threads->AttrSetSchedPolicy((*registers)[0],
                                                 static_cast<int>(
                                                     (*registers)[1]),
                                                 &result, dispatch_error))
                    return false;
                (*registers)[0] = static_cast<std::uint64_t>(result);
                return true;
            },
            error) ||
        !imports->Register(
            "pthread_attr_setschedparam",
            [threads](Aarch64ImportRegistry::Registers* registers,
                      std::string* dispatch_error) {
                int result = kInvalid;
                if (!threads->AttrSetSchedParam((*registers)[0],
                                                (*registers)[1], &result,
                                                dispatch_error))
                    return false;
                (*registers)[0] = static_cast<std::uint64_t>(result);
                return true;
            },
            error) ||
        !imports->Register(
            "pthread_create",
            [threads](Aarch64ImportRegistry::Registers* registers,
                      std::string*) {
                (*registers)[0] = static_cast<std::uint64_t>(threads->Create(
                    (*registers)[0], (*registers)[1], (*registers)[2],
                    (*registers)[3]));
                return true;
            },
            error) ||
        !imports->Register(
            "pthread_join",
            [threads](Aarch64ImportRegistry::Registers* registers,
                      std::string*) {
                (*registers)[0] = static_cast<std::uint64_t>(
                    threads->Join((*registers)[0], (*registers)[1]));
                return true;
            },
            error))
        return false;
    // pthread_setschedparam: the compat runtime models SCHED_OTHER only,
    // where Linux accepts priority 0 and rejects anything else with
    // EINVAL (22). Requests are recorded but never applied.
    if (!imports->Register(
            "pthread_setschedparam",
            [](Aarch64ImportRegistry::Registers* registers, std::string*) {
                (*registers)[0] = (*registers)[2] == 0 ? 0 : 22;
                return true;
            },
            error))
        return false;
    // pthread_getschedparam: report SCHED_OTHER (0) with the flat 0
    // priority and a successful status. The outputs are not written
    // because this registration scope has no guest memory access; the
    // callers treat the status as the authoritative result.
    if (!imports->Register(
            "pthread_getschedparam",
            [](Aarch64ImportRegistry::Registers* registers, std::string*) {
                (*registers)[0] = 0;
                return true;
            },
            error))
        return false;
    return imports->Register(
        "pthread_detach",
        [threads](Aarch64ImportRegistry::Registers* registers, std::string*) {
            (*registers)[0] = static_cast<std::uint64_t>(
                threads->Detach((*registers)[0]));
            return true;
        },
        error);
}

}  // namespace isomr
