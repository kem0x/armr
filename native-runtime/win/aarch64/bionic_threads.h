#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "worker_execution_telemetry.h"

namespace isomr {

class Aarch64ImportRegistry;
class BionicThreadState;
class BionicTls;
class GuestAddressSpace;
class GuestHeap;

class BionicThreads {
public:
    enum class WorkerCpuSamplingMode {
        kDisabled,
        kEnabled,
    };
    enum class WorkerLifecycleState {
        kActive,
        kCompleted,
    };
    enum class WorkerExecutionSamplingMode { kDisabled, kEnabled };
    enum class QsortOperationTelemetryMode {
        kDisabled,
        kAggregateOnly,
        kOperationBoundary,
    };
    enum class QsortAlgorithm { kHeapSort, kBionicPartition };
    enum class WorkerPcGroupState { kUnavailable, kAvailable, kOverflow };
    enum class WorkerHostResidency {
        kUnavailable,
        kGeneratedPrivateExecutable,
        kArmrMainImage,
        kOtherImage,
        kNonExecutable,
        kSampleFailed,
    };
    struct WorkerExecutionRecord {
        std::uint64_t ordinal = 0;
        std::uint64_t start_group = 0;
        WorkerExecutionPhase phase = WorkerExecutionPhase::kUnavailable;
        WorkerPcGroupState pc_group_state = WorkerPcGroupState::kUnavailable;
        std::uint64_t pc_group = 0;
        std::uint64_t sample_sequence = 0;
        WorkerHostResidency host_residency =
            WorkerHostResidency::kUnavailable;
    };
    struct WorkerExecutionSnapshot {
        std::vector<WorkerExecutionRecord> records;
    };
    static constexpr std::size_t kMaxWorkerPcGroups = 64;
    struct WorkerCpuRecord {
        std::uint64_t ordinal = 0;
        std::uint64_t start_group = 0;
        WorkerLifecycleState lifecycle = WorkerLifecycleState::kActive;
        bool cpu_available = false;
        std::uint64_t cpu_100ns = 0;
    };
    struct WorkerCpuSnapshot {
        std::size_t total = 0;
        std::size_t sampled = 0;
        std::size_t dropped = 0;
        std::vector<WorkerCpuRecord> records;
    };
    static constexpr std::size_t kMaxWorkerCpuSnapshotRecords = 32;
    struct QsortOperationSnapshot {
        std::uint64_t started = 0;
        std::uint64_t completed = 0;
        std::uint64_t failed = 0;
        std::uint64_t comparisons = 0;
        std::uint64_t swaps = 0;
        std::uint64_t wall_ns = 0;
        std::uint64_t executor_setup_wall_ns = 0;
        std::uint64_t comparator_wall_ns = 0;
        std::uint64_t swap_wall_ns = 0;
    };

    using GuestCall = std::function<bool(
        std::uint64_t function,
        const std::array<std::uint64_t, 8>& arguments,
        std::uint64_t* result, std::string* error)>;
    struct GuestExecutorContext {
        GuestExecutorContext() = default;
        GuestExecutorContext(std::uint64_t handle) : thread_handle(handle) {}
        std::uint64_t thread_handle = 0;
        std::uint64_t ordinal = 0;
        std::uint64_t start_group = 0;
        std::uint64_t execution_slice_ticks = 0;
        bool worker_execution = false;
        std::shared_ptr<WorkerExecutionTelemetry> execution_telemetry;
        std::function<bool()> process_exit_cancellation_requested;
        std::function<void()> process_exit_cancellation_observed;
        operator std::uint64_t() const noexcept { return thread_handle; }
    };
    using GuestExecutorFactory =
        std::function<GuestCall(GuestExecutorContext context,
                                std::string* error)>;
    using ThreadHook = std::function<bool(std::string* error)>;
    using ManagedGuestLoop =
        std::function<bool(const GuestCall& executor, std::string* error)>;
    using ProcessExitWakeBlockedCalls = std::function<std::size_t()>;
    struct ProcessExitWorkerCleanup {
        std::size_t cancellation_requests = 0;
        std::size_t cancellation_observed = 0;
        std::size_t blocked_calls_woken = 0;
        std::size_t workers_reaped = 0;
    };

    BionicThreads(GuestHeap* heap, GuestAddressSpace* address_space,
                  BionicThreadState* thread_state, BionicTls* tls,
                  GuestExecutorFactory executor_factory,
                  ThreadHook attach_hook = {},
                  ThreadHook detach_hook = {});
    ~BionicThreads();

    BionicThreads(const BionicThreads&) = delete;
    BionicThreads& operator=(const BionicThreads&) = delete;

    int Create(std::uint64_t output_address, std::uint64_t attributes,
               std::uint64_t start_routine, std::uint64_t argument);
    int Join(std::uint64_t thread, std::uint64_t result_address);
    int Detach(std::uint64_t thread);
    bool Once(std::uint64_t once_address, std::uint64_t routine,
              int* result, std::string* error);
    bool AttrInit(std::uint64_t attributes_address, int* result,
                  std::string* error);
    bool AttrDestroy(std::uint64_t attributes_address, int* result,
                     std::string* error);
    bool AttrGetNp(std::uint64_t thread, std::uint64_t attributes_address,
                   int* result, std::string* error);
    bool AttrGetSched(std::uint64_t attributes_address,
                      std::uint64_t output_address, int* result,
                      std::string* error);
    bool AttrSetSchedPolicy(std::uint64_t attributes_address, int policy,
                            int* result, std::string* error);
    bool AttrSetSchedParam(std::uint64_t attributes_address,
                           std::uint64_t param_address, int* result,
                           std::string* error);
    bool AttrGetStack(std::uint64_t attributes_address,
                      std::uint64_t stack_base_address,
                      std::uint64_t stack_size_address, int* result,
                      std::string* error);
    bool AttrSetDetachState(std::uint64_t attributes_address, int state,
                            int* result, std::string* error);
    bool AttrSetStackSize(std::uint64_t attributes_address,
                          std::uint64_t stack_size, int* result,
                          std::string* error);
    bool BinarySearch(std::uint64_t key, std::uint64_t base,
                      std::uint64_t count, std::uint64_t element_size,
                      std::uint64_t comparator, std::uint64_t* result,
                      std::string* error);
    bool Sort(std::uint64_t base, std::uint64_t count,
              std::uint64_t element_size, std::uint64_t comparator,
              std::string* error);
    // Runs a host-owned callback loop on the calling host thread while giving
    // it one persistent guest executor and the same Bionic TLS/JNI attachment
    // envelope as pthread workers. The loop owner is responsible for joining
    // this host thread before BionicThreads is destroyed.
    bool RunManagedGuestLoop(ManagedGuestLoop loop, std::string* error);

    std::size_t thread_count() const;
    std::size_t active_count() const;
    // Process-exit only. Cooperatively cancels guest executors at a safe host
    // boundary, wakes blocked imports, and joins/reaps completed joinable
    // workers. Ordinary pthread cancellation semantics are unchanged.
    bool CancelAndReapWorkersForProcessExit(
        ProcessExitWakeBlockedCalls wake_blocked_calls,
        std::chrono::milliseconds timeout,
        ProcessExitWorkerCleanup* cleanup, std::string* error);
    // Guest-initiated process exit (_exit/_Exit): flags every thread's
    // session cancellation so each run loop halts at its next boundary.
    // The caller decides how to unwind; the import itself does not wait.
    void RequestProcessExit(int exit_code) noexcept;
    bool process_exit_requested() const noexcept;
    int process_exit_code() const noexcept;
    bool CheckCompletedWorkerFailure(std::string* error) const;
    // Pops the first completed-worker failure message so the caller can
    // decide to continue the session (reaping the dead worker) instead
    // of failing the whole guest. Returns true if a message was popped.
    bool AcknowledgeCompletedWorkerFailure(std::string* error);
    std::string DescribeThreads() const;
    bool ConfigureWorkerCpuSampling(WorkerCpuSamplingMode mode,
                                    std::string* error);
    bool TakeWorkerCpuSnapshot(WorkerCpuSnapshot* snapshot) const;
    bool ConfigureWorkerExecutionSampling(
        WorkerExecutionSamplingMode mode,
        const std::vector<std::uint64_t>& ordinals,
        std::uint64_t slice_ticks, std::string* error);
    bool TakeWorkerExecutionSnapshot(WorkerExecutionSnapshot* snapshot) const;
    // Explicit local-process-only escape hatch for resolving one record's
    // exact guest PC. The ordinary snapshot remains address-free.
    bool ResolveWorkerExecutionGuestPcLocalOnly(
        const WorkerExecutionRecord& record, std::uint64_t* guest_pc) const;
    bool ConfigureQsortOperationTelemetry(
        QsortOperationTelemetryMode mode, std::string* error);
    bool TakeQsortOperationSnapshot(QsortOperationSnapshot* snapshot) const;
    bool ConfigureQsortAlgorithm(QsortAlgorithm algorithm,
                                 std::string* error);

private:
    struct ThreadRecord;
    struct SharedState;

    static void WorkerMain(const std::shared_ptr<SharedState>& shared,
                           const std::shared_ptr<ThreadRecord>& record);
    static void CleanupRecord(const std::shared_ptr<SharedState>& shared,
                              const std::shared_ptr<ThreadRecord>& record);

    std::shared_ptr<SharedState> shared_;
};

bool RegisterBionicThreadLifecycleImports(Aarch64ImportRegistry* imports,
                                          BionicThreads* threads,
                                          std::string* error);

}  // namespace isomr
