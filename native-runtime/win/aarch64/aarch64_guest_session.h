#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "worker_execution_telemetry.h"

namespace isomr {

using Aarch64OwnerThreadInterSliceCallback =
    std::function<bool(std::string* error)>;

// Mutable only while the configured owner thread is paused at an exact guest
// instruction boundary. The callback may redirect the current call by
// updating registers, SP, and PC; the session validates the resumed target
// before execution continues.
struct Aarch64PausedExecutionState {
    std::array<std::uint64_t, 31> registers{};
    std::uint64_t stack_pointer = 0;
    std::uint64_t program_counter = 0;
};

using Aarch64OwnerThreadExecutionBreakpointCallback =
    std::function<bool(Aarch64PausedExecutionState* state,
                       std::string* error)>;

struct Aarch64GuestProcessExitCancellation {
    std::function<bool()> requested;
    std::function<void()> observed;
};

class Aarch64ImportRegistry;
class Elf64ModuleSet;
class GuestHeap;

enum class Aarch64CallArgumentKind : std::uint8_t {
    kGeneral64,
    kFloat32,
    kFloat64,
    kVector128,
};

struct Aarch64CallArgument {
    Aarch64CallArgumentKind kind = Aarch64CallArgumentKind::kGeneral64;
    std::array<std::uint64_t, 2> bits{};

    static Aarch64CallArgument General64(std::uint64_t value);
    static Aarch64CallArgument Float32Bits(std::uint32_t value);
    static Aarch64CallArgument Float64Bits(std::uint64_t value);
    static Aarch64CallArgument Vector128Bits(
        std::array<std::uint64_t, 2> value);
};

struct Aarch64CallResult {
    std::uint64_t x0 = 0;
    std::array<std::uint64_t, 2> v0{};
};

enum class Aarch64GuestCallResultState : std::uint8_t {
    kNotRun,
    kSucceeded,
    kFailed,
};

enum class Aarch64CallTelemetryMode : std::uint8_t {
    kOff,
    kEnvelope,
    kFull,
    kSvcCategory,
};

enum class Aarch64SvcCategory : std::uint8_t {
    kConditionWait,
    kMutexRwlockBlocking,
    kFutexWait,
    kPthreadOnceJoin,
    kQsortBsearch,
    kAllocationFree,
    kJniGles,
    kOther,
    kCount,
};

struct Aarch64SvcCategoryTelemetry {
    std::uint64_t invocations = 0;
    std::uint64_t wall_time_ns = 0;
};

struct Aarch64SvcImportTelemetry {
    std::string name;
    std::uint64_t invocations = 0;
    std::uint64_t wall_time_ns = 0;
    std::uint64_t max_wall_time_ns = 0;
    std::uint64_t over_1ms_invocations = 0;
    std::uint64_t over_1ms_wall_time_ns = 0;
    std::uint64_t successful_nanosleep_requests = 0;
    std::uint64_t nanosleep_requested_total_ns = 0;
    std::uint64_t nanosleep_requested_max_ns = 0;
    bool nanosleep_requested_accumulation_valid = true;
};

struct Aarch64GuestPcTelemetry {
    std::uint64_t program_counter = 0;
    std::uint64_t link_register = 0;
    std::uint64_t parent_link_register = 0;
    std::uint64_t register_0_min = 0;
    std::uint64_t register_0_max = 0;
    std::uint64_t register_1_min = 0;
    std::uint64_t register_1_max = 0;
    std::uint64_t register_2_min = 0;
    std::uint64_t register_2_max = 0;
    std::uint64_t register_3_min = 0;
    std::uint64_t register_3_max = 0;
    std::uint64_t register_8_min = 0;
    std::uint64_t register_8_max = 0;
    std::uint64_t register_10_min = 0;
    std::uint64_t register_10_max = 0;
    std::uint64_t register_11_min = 0;
    std::uint64_t register_11_max = 0;
    std::uint64_t register_19_min = 0;
    std::uint64_t register_19_max = 0;
    std::uint64_t register_22_min = 0;
    std::uint64_t register_22_max = 0;
    std::uint64_t register_24_min = 0;
    std::uint64_t register_24_max = 0;
    std::uint64_t samples = 0;
    std::uint64_t error = 0;
};

class Aarch64SvcImportTelemetryAccumulator {
public:
    bool Prepare(std::span<const std::string_view> import_names) noexcept;
    void Accumulate(std::uint32_t immediate,
                    std::uint64_t wall_time_ns) noexcept;
    void AccumulateSuccessfulNanosleepRequest(
        std::uint32_t immediate, std::uint64_t requested_ns) noexcept;
    void AccumulateSuccessfulNanosleepRequests(
        std::uint32_t immediate, std::uint64_t request_count,
        std::uint64_t requested_total_ns,
        std::uint64_t requested_max_ns) noexcept;
    bool empty() const noexcept { return size() == 0; }
    std::size_t size() const noexcept;
    const Aarch64SvcImportTelemetry& operator[](
        std::size_t active_index) const noexcept;
    std::span<const Aarch64SvcImportTelemetry> entries() const noexcept;

private:
    struct Storage;
    std::shared_ptr<Storage> storage_;
};

struct Aarch64GuestCallTelemetry {
    bool enabled = false;
    Aarch64CallTelemetryMode mode = Aarch64CallTelemetryMode::kOff;
    bool thread_cpu_time_available = false;
    Aarch64GuestCallResultState result_state =
        Aarch64GuestCallResultState::kNotRun;
    std::uint64_t completion_sequence = 0;
    std::uint64_t halt_reason = 0;
    std::uint64_t wall_time_ns = 0;
    std::uint64_t thread_cpu_time_100ns = 0;
    std::uint64_t translated_ticks = 0;
    std::uint64_t jit_run_invocations = 0;
    std::uint64_t memory_read_code_invocations = 0;
    std::uint64_t scalar_read_invocations = 0;
    std::uint64_t vector_read_invocations = 0;
    // These are exact callback invocations, not guest stores: Dynarmic's
    // pinned-memory emitter may perform accesses without invoking callbacks.
    std::uint64_t write_invocations = 0;
    std::uint64_t exclusive_write_invocations = 0;
    std::uint64_t svc_invocations = 0;
    std::uint64_t svc_wall_time_ns = 0;
    std::array<std::uint64_t, 10> clock_gettime_clock_ids{};
    std::uint64_t clock_gettime_unknown_clock_ids = 0;
    std::array<Aarch64SvcCategoryTelemetry,
               static_cast<std::size_t>(Aarch64SvcCategory::kCount)>
        svc_categories{};
    Aarch64SvcImportTelemetryAccumulator svc_imports;
    std::uint64_t cntpct_invocations = 0;
    std::uint64_t heap_read_invocations = 0;
    std::uint64_t module_read_invocations = 0;
    std::uint64_t heap_write_invocations = 0;
    std::uint64_t module_write_invocations = 0;
    std::uint64_t heap_read_cache_hits = 0;
    std::uint64_t heap_read_cache_validations = 0;
    std::uint64_t heap_write_cache_hits = 0;
    std::uint64_t heap_write_cache_validations = 0;
    std::uint64_t memory_callback_timing_samples = 0;
    std::uint64_t memory_callback_sample_wall_time_ns = 0;
    std::uint64_t inter_slice_callback_invocations = 0;
    std::uint64_t inter_slice_callback_wall_time_ns = 0;
    std::uint64_t heap_metadata_generation_advances = 0;
    std::uint64_t guest_pc_samples = 0;
    std::shared_ptr<std::array<Aarch64GuestPcTelemetry, 64>>
        guest_pc_histogram;
};

void AccumulateAarch64SvcImportTelemetry(
    Aarch64GuestCallTelemetry* telemetry, std::uint32_t immediate,
    std::uint64_t wall_time_ns) noexcept;
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
    std::uint64_t register_24) noexcept;

enum class Aarch64MemoryCaptureAccess : std::uint8_t {
    kNone,
    kRead,
    kWrite,
};

struct Aarch64MemoryCaptureConfig {
    Aarch64MemoryCaptureAccess access = Aarch64MemoryCaptureAccess::kNone;
    std::uint64_t watch_generation = 0;
    std::uint64_t occurrence = 1;
    std::uint64_t breakpoint_address = 0;
};

struct Aarch64MemoryAccessCapture {
    Aarch64MemoryCaptureAccess access = Aarch64MemoryCaptureAccess::kNone;
    std::uint64_t watch_generation = 0;
    std::uint64_t occurrence = 0;
    std::uint64_t address = 0;
    std::size_t size = 0;
    std::array<std::uint64_t, 2> value{};
    std::uint64_t program_counter = 0;
    std::array<std::uint64_t, 31> registers{};
    std::uint64_t stack_pointer = 0;

    bool triggered() const {
        return access != Aarch64MemoryCaptureAccess::kNone;
    }
};

class Aarch64ExclusiveMonitor {
public:
    explicit Aarch64ExclusiveMonitor(std::size_t processor_capacity = 64);
    ~Aarch64ExclusiveMonitor();
    Aarch64ExclusiveMonitor(const Aarch64ExclusiveMonitor&) = delete;
    Aarch64ExclusiveMonitor& operator=(const Aarch64ExclusiveMonitor&) = delete;

private:
    friend class Aarch64GuestSession;
    class Impl;
    std::unique_ptr<Impl> impl_;
};

class Aarch64GuestSession {
public:
    static std::unique_ptr<Aarch64GuestSession> Create(
        Elf64ModuleSet* modules, const Aarch64ImportRegistry* imports,
        GuestHeap* heap, std::uint64_t thread_pointer,
        std::size_t stack_size = 1024U * 1024U,
        std::uint64_t instruction_budget = 100000000ULL,
        std::string* error = nullptr,
        Aarch64MemoryCaptureConfig memory_capture = {},
        std::shared_ptr<WorkerExecutionTelemetry> execution_telemetry = {},
        Aarch64GuestProcessExitCancellation process_exit_cancellation = {});
    static std::unique_ptr<Aarch64GuestSession> Create(
        Aarch64ExclusiveMonitor* exclusive_monitor,
        Elf64ModuleSet* modules, const Aarch64ImportRegistry* imports,
        GuestHeap* heap, std::uint64_t thread_pointer,
        std::size_t stack_size = 1024U * 1024U,
        std::uint64_t instruction_budget = 100000000ULL,
        std::string* error = nullptr,
        Aarch64MemoryCaptureConfig memory_capture = {},
        std::shared_ptr<WorkerExecutionTelemetry> execution_telemetry = {},
        Aarch64GuestProcessExitCancellation process_exit_cancellation = {});

    ~Aarch64GuestSession();
    Aarch64GuestSession(const Aarch64GuestSession&) = delete;
    Aarch64GuestSession& operator=(const Aarch64GuestSession&) = delete;

    bool Call(std::uint64_t function,
              const std::array<std::uint64_t, 8>& arguments,
              std::uint64_t* result, std::string* error);
    bool CallEntry(std::uint64_t function,
                   const std::vector<Aarch64CallArgument>& arguments,
                   Aarch64CallResult* result, std::string* error);
    bool SetInstructionBudgetForNextCall(std::uint64_t instruction_budget,
                                         std::string* error);
    // Opt-in only. The callback runs on its configuring/calling thread after
    // a bounded dispatcher slice; the current call then resumes unchanged.
    // When renew_total_budget_after_slice is true, the configured total
    // budget is enforced per bounded slice for long-lived cooperative worker
    // calls. False preserves one finite total budget for the whole call.
    bool SetOwnerThreadInterSliceCallback(
        std::uint64_t slice_instruction_budget,
        Aarch64OwnerThreadInterSliceCallback callback,
        std::string* error,
        bool renew_total_budget_after_slice = false);
    // Overrides one subsequent dispatcher slice. This is valid only from the
    // configured owner-thread callback while execution is already paused.
    bool SetOwnerThreadInterSliceBudgetForNextSlice(
        std::uint64_t slice_instruction_budget, std::string* error);
    void ClearOwnerThreadInterSliceCallback();
    // Captures one guest PC sample only while the configured owner-thread
    // inter-slice callback is active and the JIT is already paused.
    bool CaptureGuestPcTelemetrySample(
        Aarch64GuestCallTelemetry* telemetry, std::string* error) const;
    // Opt-in only. The exact instruction is not executed: the owner-thread
    // callback receives the paused state and the same guest call resumes from
    // the validated state it returns. Clearing restores ordinary execution.
    bool SetOwnerThreadExecutionBreakpoint(
        std::uint64_t guest_address,
        Aarch64OwnerThreadExecutionBreakpointCallback callback,
        std::string* error);
    void ClearOwnerThreadExecutionBreakpoint();
    // Adds an independently removable exact-address callback. All callbacks
    // belong to the configuring thread and run only while that thread's guest
    // call is paused.
    bool AddOwnerThreadExecutionBreakpoint(
        std::uint64_t guest_address,
        Aarch64OwnerThreadExecutionBreakpointCallback callback,
        std::string* error);
    void RemoveOwnerThreadExecutionBreakpoint(std::uint64_t guest_address);
    void SetCallTelemetryEnabled(bool enabled);
    void SetCallTelemetryMode(Aarch64CallTelemetryMode mode);

    // Asks a currently-running guest call to halt at the next dynarmic
    // boundary; safe to call from a different host thread for diagnostics.
    void RequestHalt();
    std::uint64_t register_value(std::size_t index) const;
    std::uint64_t program_counter() const;
    std::uint64_t instructions_executed() const;
    std::uint64_t stack_base() const;
    std::uint64_t stack_top() const;
    std::uint64_t stack_pointer() const;
    std::uint64_t thread_pointer() const;
    Aarch64MemoryAccessCapture memory_access_capture() const;
    Aarch64GuestCallTelemetry last_call_telemetry() const;

private:
    class Impl;
    explicit Aarch64GuestSession(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

}  // namespace isomr
