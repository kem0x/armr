#include "bionic_thread_state.h"
#include "bionic_time.h"
#include "guest_address_space.h"
#include "guest_heap.h"
#include "import_registry.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

int Fail(const char* stage) {
    std::fprintf(stderr, "ISOMR_BIONIC_WAITABLE_TIMER_SMOKE=fail stage=%s\n",
                 stage);
    return 1;
}

enum class Failure {
    kNone,
    kCreate,
    kSet,
    kWait,
    kClose,
};

struct ClosedCall {
    std::uintptr_t handle = 0;
    std::optional<std::int64_t> due_time;
    std::size_t set_count = 0;
    std::size_t wait_count = 0;
    bool close_succeeded = false;
};

class FakeWaitableTimerApi {
public:
    isomr::BionicWaitableTimerApi Api() {
        isomr::BionicWaitableTimerApi api;
        api.create_high_resolution = [this]() { return Create(); };
        api.set_relative_100ns =
            [this](void* handle, std::int64_t due_time) {
                return Set(handle, due_time);
            };
        api.wait = [this](void* handle) { return Wait(handle); };
        api.close = [this](void* handle) { return Close(handle); };
        return api;
    }

    void FailNext(Failure failure) {
        const std::lock_guard lock(mutex_);
        failure_ = failure;
    }

    std::size_t create_count() const {
        const std::lock_guard lock(mutex_);
        return create_count_;
    }
    std::size_t set_count() const {
        const std::lock_guard lock(mutex_);
        return set_count_;
    }
    std::size_t wait_count() const {
        const std::lock_guard lock(mutex_);
        return wait_count_;
    }
    std::size_t close_count() const {
        const std::lock_guard lock(mutex_);
        return close_count_;
    }
    std::vector<ClosedCall> closed_calls() const {
        const std::lock_guard lock(mutex_);
        return closed_calls_;
    }
    bool clean() const {
        const std::lock_guard lock(mutex_);
        if (!active_.empty() || close_count_by_handle_.size() != close_count_)
            return false;
        for (const auto& [handle, count] : close_count_by_handle_) {
            (void)handle;
            if (count != 1) return false;
        }
        return true;
    }

private:
    struct HandleState {
        std::optional<std::int64_t> due_time;
        std::size_t set_count = 0;
        std::size_t wait_count = 0;
    };

    bool Consume(Failure failure) {
        if (failure_ != failure) return false;
        failure_ = Failure::kNone;
        return true;
    }

    isomr::BionicWaitableTimerCreateResult Create() {
        const std::lock_guard lock(mutex_);
        ++create_count_;
        if (Consume(Failure::kCreate)) return {nullptr, 1001};
        const std::uintptr_t identity = next_identity_++;
        active_.emplace(identity, HandleState{});
        return {reinterpret_cast<void*>(identity), 0};
    }

    isomr::BionicWaitableTimerResult Set(void* handle,
                                         std::int64_t due_time) {
        const std::lock_guard lock(mutex_);
        ++set_count_;
        const std::uintptr_t identity =
            reinterpret_cast<std::uintptr_t>(handle);
        auto found = active_.find(identity);
        if (found == active_.end()) return {false, 1002};
        ++found->second.set_count;
        found->second.due_time = due_time;
        if (Consume(Failure::kSet)) return {false, 1003};
        return {true, 0};
    }

    isomr::BionicWaitableTimerResult Wait(void* handle) {
        const std::lock_guard lock(mutex_);
        ++wait_count_;
        const std::uintptr_t identity =
            reinterpret_cast<std::uintptr_t>(handle);
        auto found = active_.find(identity);
        if (found == active_.end() || !found->second.due_time)
            return {false, 1004};
        ++found->second.wait_count;
        if (Consume(Failure::kWait)) return {false, 1005};
        return {true, 0};
    }

    isomr::BionicWaitableTimerResult Close(void* handle) {
        const std::lock_guard lock(mutex_);
        ++close_count_;
        const std::uintptr_t identity =
            reinterpret_cast<std::uintptr_t>(handle);
        ++close_count_by_handle_[identity];
        auto found = active_.find(identity);
        if (found == active_.end()) return {false, 1006};
        const bool fail = Consume(Failure::kClose);
        closed_calls_.push_back(
            {identity, found->second.due_time, found->second.set_count,
             found->second.wait_count, !fail});
        active_.erase(found);
        return {!fail, fail ? 1007U : 0U};
    }

    mutable std::mutex mutex_;
    Failure failure_ = Failure::kNone;
    std::uintptr_t next_identity_ = 1;
    std::size_t create_count_ = 0;
    std::size_t set_count_ = 0;
    std::size_t wait_count_ = 0;
    std::size_t close_count_ = 0;
    std::unordered_map<std::uintptr_t, HandleState> active_;
    std::unordered_map<std::uintptr_t, std::size_t> close_count_by_handle_;
    std::vector<ClosedCall> closed_calls_;
};

struct SleepResult {
    bool dispatched = false;
    bool guest_succeeded = false;
    bool published = false;
    bool remainder_zero = false;
    bool errno_before_read = false;
    bool errno_read = false;
    std::int32_t guest_errno_before = -1;
    std::int32_t guest_errno = -1;
    std::uint64_t request_address = 0;
    std::uint64_t register_result = 0;
    std::string error;
};

class Fixture {
public:
    Fixture()
        : heap_(0x710000000000ULL, 16U * 1024U * 1024U),
          memory_(&heap_), threads_(&heap_),
          time_(&heap_, &memory_, &threads_, {},
                isomr::BionicNanosleepBackend::kHighResolutionWaitable,
                api_.Api()),
          imports_(0x680000000000ULL, 16) {}

    bool Initialize() {
        return time_.ValidateConfiguration(&error_) &&
               threads_.AttachCurrentThread(&error_) &&
               time_.AttachCurrentThread(&error_) &&
               time_.RegisterImports(&imports_, &error_) &&
               imports_.Seal(&error_);
    }

    SleepResult Sleep(const isomr::BionicTimespec& request) {
        constexpr isomr::BionicTimespec kCanary{7, 11};
        SleepResult result;
        const std::uint64_t request_address =
            heap_.Allocate(sizeof(isomr::BionicTimespec));
        const std::uint64_t remainder_address =
            heap_.Allocate(sizeof(isomr::BionicTimespec));
        if (request_address == 0 || remainder_address == 0 ||
            !heap_.Write(request_address, &request, sizeof(request)) ||
            !heap_.Write(remainder_address, &kCanary, sizeof(kCanary))) {
            result.error = "fixture allocation";
            return result;
        }
        isomr::Aarch64ImportRegistry::Registers registers{};
        registers[0] = request_address;
        registers[1] = remainder_address;
        registers.nanosleep_requested_duration_diagnostic.Reset(true);
        std::string errno_error;
        result.errno_before_read = threads_.ReadCurrentErrno(
            &result.guest_errno_before, &errno_error);
        result.dispatched =
            imports_.Dispatch("nanosleep", &registers, &result.error);
        result.request_address = request_address;
        result.register_result = registers[0];
        result.guest_succeeded = result.dispatched && registers[0] == 0;
        result.published =
            registers.nanosleep_requested_duration_diagnostic.result
                .has_value();
        isomr::BionicTimespec remainder{};
        if (heap_.Read(remainder_address, &remainder, sizeof(remainder))) {
            result.remainder_zero =
                remainder.seconds == 0 && remainder.nanoseconds == 0;
        }
        result.errno_read =
            threads_.ReadCurrentErrno(&result.guest_errno, &errno_error);
        if (!result.errno_before_read || !result.errno_read)
            result.error += "; " + errno_error;
        std::string release_error;
        if (!heap_.Release(request_address, &release_error) ||
            !heap_.Release(remainder_address, &release_error)) {
            result.error += "; fixture release failed";
            result.dispatched = false;
        }
        return result;
    }

    bool AttachWorker() {
        std::string error;
        return threads_.AttachCurrentThread(&error) &&
               time_.AttachCurrentThread(&error);
    }
    bool DetachWorker() {
        std::string error;
        return time_.DetachCurrentThread(&error) &&
               threads_.DetachCurrentThread(&error);
    }
    bool Shutdown() {
        return time_.DetachCurrentThread(&error_) &&
               threads_.DetachCurrentThread(&error_) &&
               time_.Shutdown(&error_) && heap_.allocation_count() == 0 &&
               api_.clean();
    }

    FakeWaitableTimerApi& api() { return api_; }

private:
    FakeWaitableTimerApi api_;
    isomr::GuestHeap heap_;
    isomr::GuestAddressSpace memory_;
    isomr::BionicThreadState threads_;
    isomr::BionicTime time_;
    isomr::Aarch64ImportRegistry imports_;
    std::string error_;
};

bool Successful(const SleepResult& result) {
    return result.guest_succeeded && result.published &&
           result.remainder_zero;
}

bool FailedClosed(const SleepResult& result) {
    return !result.guest_succeeded && !result.published &&
           !result.remainder_zero;
}

bool BackendFailedClosed(const SleepResult& result,
                         const char* operation,
                         std::uint32_t native_error) {
    const std::string prefix =
        "AArch64 import nanosleep failed: bionic nanosleep "
        "high-resolution timer " +
        std::string(operation) + " failed with native error " +
        std::to_string(native_error) + " recent=";
    return FailedClosed(result) && !result.dispatched &&
           result.errno_before_read && result.errno_read &&
           result.guest_errno == result.guest_errno_before &&
           result.register_result == result.request_address &&
           result.error.rfind(prefix, 0) == 0 && result.error.size() <= 1024;
}

}  // namespace

int main() {
    Fixture fixture;
    if (!fixture.Initialize()) return Fail("initialize");
    FakeWaitableTimerApi& api = fixture.api();
    if (api.create_count() != 1 || api.set_count() != 0 ||
        api.wait_count() != 0 || api.close_count() != 1)
        return Fail("capability-probe");

    const std::size_t zero_create = api.create_count();
    const std::size_t zero_set = api.set_count();
    const std::size_t zero_wait = api.wait_count();
    const std::size_t zero_close = api.close_count();
    if (!Successful(fixture.Sleep({0, 0})) ||
        api.create_count() != zero_create || api.set_count() != zero_set ||
        api.wait_count() != zero_wait || api.close_count() != zero_close)
        return Fail("zero-no-api");

    struct ConversionCase {
        isomr::BionicTimespec request;
        std::int64_t due_time;
    };
    constexpr ConversionCase kConversions[] = {
        {{0, 1}, -1},
        {{0, 99}, -1},
        {{0, 100}, -1},
        {{0, 101}, -2},
        {{0, 500000}, -5000},
        {{0, 1000000}, -10000},
        {{0, 15000000}, -150000},
        {{9223372036LL, 854775807LL}, -92233720368547759LL},
    };
    for (const ConversionCase& conversion : kConversions) {
        const std::size_t before = api.closed_calls().size();
        if (!Successful(fixture.Sleep(conversion.request)))
            return Fail("conversion-dispatch");
        const std::vector<ClosedCall> calls = api.closed_calls();
        if (calls.size() != before + 1 || !calls.back().due_time ||
            *calls.back().due_time != conversion.due_time ||
            calls.back().set_count != 1 || calls.back().wait_count != 1 ||
            !calls.back().close_succeeded)
            return Fail("conversion-due-time");
    }

    const std::size_t overflow_creates = api.create_count();
    if (!FailedClosed(fixture.Sleep({9223372036LL, 854775808LL})) ||
        api.create_count() != overflow_creates)
        return Fail("overflow-before-conversion");

    std::size_t before_create = api.create_count();
    std::size_t before_set = api.set_count();
    std::size_t before_wait = api.wait_count();
    std::size_t before_close = api.close_count();
    api.FailNext(Failure::kCreate);
    if (!BackendFailedClosed(fixture.Sleep({0, 1}), "create", 1001) ||
        api.create_count() != before_create + 1 ||
        api.set_count() != before_set || api.wait_count() != before_wait ||
        api.close_count() != before_close)
        return Fail("create-failure");

    before_create = api.create_count();
    before_set = api.set_count();
    before_wait = api.wait_count();
    before_close = api.close_count();
    api.FailNext(Failure::kSet);
    if (!BackendFailedClosed(fixture.Sleep({0, 1}), "set", 1003) ||
        api.create_count() != before_create + 1 ||
        api.set_count() != before_set + 1 ||
        api.wait_count() != before_wait ||
        api.close_count() != before_close + 1)
        return Fail("set-failure");

    before_create = api.create_count();
    before_set = api.set_count();
    before_wait = api.wait_count();
    before_close = api.close_count();
    api.FailNext(Failure::kWait);
    if (!BackendFailedClosed(fixture.Sleep({0, 1}), "wait", 1005) ||
        api.create_count() != before_create + 1 ||
        api.set_count() != before_set + 1 ||
        api.wait_count() != before_wait + 1 ||
        api.close_count() != before_close + 1)
        return Fail("wait-failure");

    before_create = api.create_count();
    before_set = api.set_count();
    before_wait = api.wait_count();
    before_close = api.close_count();
    api.FailNext(Failure::kClose);
    if (!BackendFailedClosed(fixture.Sleep({0, 1}), "close", 1007) ||
        api.create_count() != before_create + 1 ||
        api.set_count() != before_set + 1 ||
        api.wait_count() != before_wait + 1 ||
        api.close_count() != before_close + 1)
        return Fail("close-failure");

    std::atomic<bool> concurrent_ok{true};
    const auto worker = [&fixture, &concurrent_ok]() {
        if (!fixture.AttachWorker() ||
            !Successful(fixture.Sleep({0, 500000})) ||
            !fixture.DetachWorker())
            concurrent_ok.store(false, std::memory_order_relaxed);
    };
    std::thread first(worker);
    std::thread second(worker);
    first.join();
    second.join();
    if (!concurrent_ok.load(std::memory_order_relaxed))
        return Fail("concurrent-dispatch");

    const std::vector<ClosedCall> calls = api.closed_calls();
    std::unordered_set<std::uintptr_t> identities;
    for (const ClosedCall& call : calls) identities.insert(call.handle);
    if (identities.size() != calls.size() || !fixture.Shutdown())
        return Fail("close-once-cleanup");

    std::puts(
        "ISOMR_BIONIC_WAITABLE_TIMER create-set-wait-close=success-failures-native-code-bounded guest-failure=dispatch-refused-errno-unchanged close=once-local zero=no-api concurrent=independent conversion=0,1,99,100,101,500000,1000000,15000000,int64-max overflow=int64-max-plus-one-rejected");
    std::puts("ISOMR_BIONIC_WAITABLE_TIMER_SMOKE=pass");
    return 0;
}
