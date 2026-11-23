#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace isomr {

enum class WorkerExecutionPhase : std::uint32_t {
    kUnavailable,
    kCallEntry,
    kBeforeRun,
    kAfterRun,
    kInterSlice,
    kCallSuccess,
    kCallFailure,
    kDestroyed,
};

struct WorkerExecutionTelemetrySnapshot {
    WorkerExecutionPhase phase = WorkerExecutionPhase::kUnavailable;
    bool pc_valid = false;
    std::uint64_t raw_pc = 0;
    std::uint64_t sample_sequence = 0;
};

// Single-writer/multi-reader telemetry. All fields are atomic and all accesses
// are sequentially consistent: an accepted equal, even sequence therefore
// brackets one coherent publication in the single global SC order.
class WorkerExecutionTelemetry {
public:
    void Publish(WorkerExecutionPhase phase, bool pc_valid,
                 std::uint64_t raw_pc) noexcept {
        sequence_.fetch_add(1, std::memory_order_seq_cst);
        phase_.store(static_cast<std::uint32_t>(phase),
                     std::memory_order_seq_cst);
        pc_valid_.store(pc_valid, std::memory_order_seq_cst);
        raw_pc_.store(pc_valid ? raw_pc : 0, std::memory_order_seq_cst);
        sample_sequence_.fetch_add(1, std::memory_order_seq_cst);
        sequence_.fetch_add(1, std::memory_order_seq_cst);
    }

    bool TakeSnapshot(WorkerExecutionTelemetrySnapshot* snapshot) const
        noexcept {
        if (snapshot == nullptr) return false;
        constexpr std::size_t kMaximumRetries = 64;
        for (std::size_t retry = 0; retry < kMaximumRetries; ++retry) {
            const std::uint64_t before =
                sequence_.load(std::memory_order_seq_cst);
            if ((before & 1U) != 0) continue;
            WorkerExecutionTelemetrySnapshot candidate;
            candidate.phase = static_cast<WorkerExecutionPhase>(
                phase_.load(std::memory_order_seq_cst));
            candidate.pc_valid = pc_valid_.load(std::memory_order_seq_cst);
            candidate.raw_pc = raw_pc_.load(std::memory_order_seq_cst);
            candidate.sample_sequence =
                sample_sequence_.load(std::memory_order_seq_cst);
            const std::uint64_t after =
                sequence_.load(std::memory_order_seq_cst);
            if (before == after && (after & 1U) == 0) {
                *snapshot = candidate;
                return true;
            }
        }
        return false;
    }

private:
    std::atomic<std::uint64_t> sequence_{0};
    std::atomic<std::uint32_t> phase_{
        static_cast<std::uint32_t>(WorkerExecutionPhase::kUnavailable)};
    std::atomic<bool> pc_valid_{false};
    std::atomic<std::uint64_t> raw_pc_{0};
    std::atomic<std::uint64_t> sample_sequence_{0};
};

}  // namespace isomr
