#pragma once

#include <atomic>
#include <cstddef>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>

namespace isomr {

enum class BionicNanosleepBackend : std::uint8_t {
    kSleepFor,
    kHighResolutionWaitable,
};

struct BionicWaitableTimerResult {
    bool succeeded = false;
    std::uint32_t native_error = 0;
};

struct BionicWaitableTimerCreateResult {
    void* handle = nullptr;
    std::uint32_t native_error = 0;
};

struct BionicWaitableTimerApi {
    using Handle = void*;
    std::function<BionicWaitableTimerCreateResult()> create_high_resolution;
    std::function<BionicWaitableTimerResult(Handle, std::int64_t)>
        set_relative_100ns;
    std::function<BionicWaitableTimerResult(Handle)> wait;
    std::function<BionicWaitableTimerResult(Handle)> close;
};

class Aarch64ImportRegistry;
class BionicThreadState;
class GuestAddressSpace;
class GuestHeap;

struct BionicTimespec {
    std::int64_t seconds;
    std::int64_t nanoseconds;
};

struct BionicTimeval {
    std::int64_t seconds;
    std::int64_t microseconds;
};

constexpr std::size_t kNanosleepCallsiteTelemetryCapacity = 128;

struct NanosleepCallsiteTelemetryBucket {
    std::uint64_t guest_link_register = 0;
    std::uint64_t parent_guest_link_register = 0;
    std::uint32_t logical_thread_id = 0;
    bool parent_valid = false;
    std::uint64_t success_count = 0;
    std::uint64_t requested_total_ns = 0;
    std::uint64_t requested_max_ns = 0;
    std::uint64_t handler_body_total_ns = 0;
    std::uint64_t handler_body_max_ns = 0;
};

struct NanosleepCallsiteTelemetryOverflow {
    bool table = false;
    bool admission = false;
    bool count = false;
    bool requested_total = false;
    bool handler_body_total = false;
};

struct NanosleepCallsiteTelemetrySnapshot {
    bool enabled = false;
    bool complete = true;
    std::uint64_t generation = 0;
    std::size_t retained_count = 0;
    std::array<NanosleepCallsiteTelemetryBucket,
               kNanosleepCallsiteTelemetryCapacity> retained{};
    NanosleepCallsiteTelemetryBucket residual{};
    NanosleepCallsiteTelemetryBucket aggregate{};
    NanosleepCallsiteTelemetryOverflow overflow{};
};

struct NanosleepCallsiteTelemetryToken {
    std::uint64_t generation = 0;
    bool active = false;
};

static_assert(sizeof(BionicTimespec) == 16);
static_assert(sizeof(BionicTimeval) == 16);

struct BionicTm {
    std::int32_t seconds;
    std::int32_t minutes;
    std::int32_t hours;
    std::int32_t month_day;
    std::int32_t month;
    std::int32_t year;
    std::int32_t week_day;
    std::int32_t year_day;
    std::int32_t daylight_saving;
    std::int32_t padding;
    std::int64_t utc_offset;
    std::uint64_t zone;
};

static_assert(sizeof(BionicTm) == 56);
static_assert(offsetof(BionicTm, utc_offset) == 40);
static_assert(offsetof(BionicTm, zone) == 48);

class BionicTime {
public:
    using NanosleepTestBackend =
        std::function<void(std::chrono::nanoseconds)>;
    using NanosleepTelemetryClock = std::function<
        std::chrono::steady_clock::time_point()>;

    BionicTime(GuestHeap* heap, GuestAddressSpace* memory,
               BionicThreadState* threads,
               NanosleepTestBackend nanosleep_test_backend = {},
               BionicNanosleepBackend nanosleep_backend =
                   BionicNanosleepBackend::kSleepFor,
               BionicWaitableTimerApi waitable_timer_api = {},
               bool nanosleep_callsite_telemetry_enabled = false,
               NanosleepTelemetryClock nanosleep_telemetry_clock = {})
        : heap_(heap), memory_(memory), threads_(threads),
          nanosleep_test_backend_(std::move(nanosleep_test_backend)),
          nanosleep_backend_(nanosleep_backend),
          waitable_timer_api_(std::move(waitable_timer_api)),
          nanosleep_callsite_telemetry_enabled_(
              nanosleep_callsite_telemetry_enabled),
          nanosleep_telemetry_clock_(
              std::move(nanosleep_telemetry_clock)) {}

    bool ValidateConfiguration(std::string* error);
    bool RegisterImports(Aarch64ImportRegistry* imports, std::string* error);
    bool AttachCurrentThread(std::string* error);
    bool DetachCurrentThread(std::string* error);
    bool Shutdown(std::string* error);

    std::size_t result_count() const;
    NanosleepCallsiteTelemetryToken BeginNanosleepCallsiteTelemetry()
        noexcept;
    NanosleepCallsiteTelemetrySnapshot EndNanosleepCallsiteTelemetry(
        NanosleepCallsiteTelemetryToken token) noexcept;
    std::size_t nanosleep_frame_read_count_for_testing() const noexcept {
        return nanosleep_frame_read_count_for_testing_.load(
            std::memory_order_relaxed);
    }

    // Source-only diagnostic contract: module-relative offsets and output
    // remain parent-local and must not be sent to hosted workers. Raw guest/
    // host addresses and per-request records are never output.
    static bool AccumulateNanosleepCallsiteTelemetryForTesting(
        NanosleepCallsiteTelemetryBucket* target,
        std::uint64_t requested_ns, std::uint64_t handler_body_ns,
        NanosleepCallsiteTelemetryOverflow* overflow) noexcept;

private:
    bool Localtime(std::uint64_t input, std::uint64_t* result,
                   std::string* error);
    bool Gmtime(std::uint64_t input, std::uint64_t* result,
                std::string* error);
    bool SleepForNanoseconds(std::uint64_t nanoseconds,
                             std::string* error);
    struct NanosleepAdmission {
        std::uint64_t generation = 0;
        std::uint64_t guest_link_register = 0;
        std::uint64_t parent_guest_link_register = 0;
        std::uint32_t logical_thread_id = 0;
        bool parent_valid = false;
        std::chrono::steady_clock::time_point started{};
        bool active = false;
    };
    class NanosleepCompletionGuard {
    public:
        NanosleepCompletionGuard(BionicTime* owner,
                                 NanosleepAdmission admission) noexcept
            : owner_(owner), admission_(admission) {}
        ~NanosleepCompletionGuard() {
            if (owner_ != nullptr)
                owner_->CompleteNanosleep(admission_, succeeded_);
        }
        NanosleepCompletionGuard(const NanosleepCompletionGuard&) = delete;
        NanosleepCompletionGuard& operator=(
            const NanosleepCompletionGuard&) = delete;
        void Succeed() noexcept { succeeded_ = true; }

    private:
        BionicTime* owner_ = nullptr;
        NanosleepAdmission admission_{};
        bool succeeded_ = false;
    };
    NanosleepAdmission AdmitNanosleep(std::uint64_t guest_link_register,
                                     std::uint64_t guest_frame_pointer)
        noexcept;
    void CompleteNanosleep(NanosleepAdmission admission,
                           bool succeeded) noexcept;

    GuestHeap* heap_ = nullptr;
    GuestAddressSpace* memory_ = nullptr;
    BionicThreadState* threads_ = nullptr;
    // Owned, fixture-only seam. Production leaves this empty and retains the
    // normal std::this_thread::sleep_for path.
    NanosleepTestBackend nanosleep_test_backend_;
    BionicNanosleepBackend nanosleep_backend_ =
        BionicNanosleepBackend::kSleepFor;
    BionicWaitableTimerApi waitable_timer_api_;
    const bool nanosleep_callsite_telemetry_enabled_ = false;
    NanosleepTelemetryClock nanosleep_telemetry_clock_;
    std::atomic<std::size_t> nanosleep_frame_read_count_for_testing_{0};
    mutable std::mutex nanosleep_callsite_mutex_;
    std::condition_variable nanosleep_callsite_condition_;
    std::uint64_t nanosleep_callsite_generation_ = 0;
    std::size_t nanosleep_callsite_in_flight_ = 0;
    bool nanosleep_callsite_accepting_ = false;
    NanosleepCallsiteTelemetrySnapshot nanosleep_callsite_snapshot_{};
    mutable std::mutex mutex_;
    std::unordered_map<std::uint32_t, std::uint64_t> results_;
    bool shutdown_ = false;
};

}  // namespace isomr
