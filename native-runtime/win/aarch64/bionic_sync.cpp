#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <atomic>
#include <mutex>
#include <thread>
#include <unordered_set>

#include "bionic_sync.h"

#include "bionic_thread_state.h"
#include "guest_address_space.h"
#include "import_registry.h"

#include <algorithm>
#include <cstdio>
#include <functional>
#include <limits>
#include <sstream>
#include <utility>
#include <vector>

namespace isomr {
namespace {

constexpr int kSuccess = 0;
constexpr int kPermissionDenied = 1;
constexpr int kTryAgain = 11;
constexpr int kBadAddress = 14;
constexpr int kBusy = 16;
constexpr int kInvalid = 22;
constexpr int kDeadlock = 35;
constexpr int kTimedOut = 110;
constexpr std::uint32_t kUnlocked = 0;
constexpr std::uint32_t kLocked = 1;
constexpr std::uint32_t kDestroyed = 0xdeadc04dU;
constexpr std::uint32_t kMutexAttribute = 0x49534d41U;
constexpr std::uint32_t kConditionAttribute = 0x49534341U;
constexpr int kMutexNormal = 0;
constexpr int kMutexRecursive = 1;
constexpr int kMutexErrorCheck = 2;

void SetError(std::string* error, std::string message) {
    if (error != nullptr) *error = std::move(message);
}

// Default-off diagnostic: ISOMR_SYNC_TRACE=1 enables the per-wait/per-signal
// stdout lines, which are otherwise hot contended writes during il2cpp
// job-scheduler futex/condition traffic.
bool SyncTraceEnabled() {
    static const bool enabled = [] {
        wchar_t flag[8] = {};
        return GetEnvironmentVariableW(L"ISOMR_SYNC_TRACE", flag, 8) > 0 &&
               flag[0] != L'0';
    }();
    return enabled;
}

// Default-off compat knob: ISOMR_SYNC_POLL_MS=N caps how long an untimed
// condition wait and a bounded futex wait may sleep before returning. Unity's
// JobQueue can complete a group via a lock-free counter store followed by a
// cond broadcast; a joiner that is mid-registration (between its guest-side
// predicate check and landing in the waiter list) misses that broadcast and
// parks for the full ~35-minute timeout. A short cap makes the wait poll: a
// lost wake self-heals on the next predicate re-check, while a satisfied wait
// returns normally the moment it is signaled. 0 disables the cap.
std::atomic<std::uint32_t> g_sync_poll_ms{[] {
    wchar_t flag[16] = {};
    const DWORD n = GetEnvironmentVariableW(L"ISOMR_SYNC_POLL_MS", flag, 16);
    if (n == 0) return 0u;
    wchar_t* end = nullptr;
    const unsigned long v = std::wcstoul(flag, &end, 10);
    return (end != flag && v > 0 && v < 60000)
               ? static_cast<std::uint32_t>(v)
               : 0u;
}()};

}  // namespace

std::uint32_t SyncPollMilliseconds() {
    return g_sync_poll_ms.load(std::memory_order_relaxed);
}

// Runtime override used by the game script's armr.set_sync_poll(). The poll
// cap poisons the boot catalog phase (thousands of legitimate long waits each
// pay a re-wake, costing 4-5 cores) but self-heals the load-phase lost wakes,
// so the script arms it only once the boot milestones have passed. Setting it
// nonzero also activates armr.sync_sweep(), which no-ops while the cap is
// zero.
void SetSyncPollMilliseconds(std::uint32_t ms) {
    const std::uint32_t value = (ms > 0 && ms < 60000) ? ms : 0;
    g_sync_poll_ms.store(value, std::memory_order_relaxed);
    std::fprintf(stderr, "ISOMR_SYNC_POLL set=%u\n", value);
    std::fflush(stderr);
}

bool BionicSync::ReadWord(std::uint64_t address, std::uint32_t* value) const {
    return address_space_ != nullptr && value != nullptr &&
           (address & (alignof(std::uint32_t) - 1)) == 0 &&
           address_space_->Read(address, value, sizeof(*value));
}

bool BionicSync::WriteWord(std::uint64_t address, std::uint32_t value) {
    return address_space_ != nullptr &&
           address_space_->Write(address, &value, sizeof(value));
}

std::shared_ptr<BionicSync::MutexState> BionicSync::FindOrCreateMutex(
    std::uint64_t address, int* status) {
    if (status == nullptr) return nullptr;
    *status = kInvalid;
    std::uint32_t word = 0;
    if (!ReadWord(address, &word)) {
        static std::size_t invalid_mutex_logs = 0;
        if (invalid_mutex_logs < 16) {
            ++invalid_mutex_logs;
            std::fprintf(stderr,
                         "ISOMR_MUTEX_INVALID address=%llx readable=0\n",
                         static_cast<unsigned long long>(address));
            std::fflush(stderr);
        }
        return nullptr;
    }

    const std::lock_guard lock(registry_mutex_);
    const auto found = mutexes_.find(address);
    if (found != mutexes_.end()) {
        *status = kSuccess;
        return found->second;
    }
    if (word != kUnlocked && word != kLocked) {
        // A readable word that is neither unlocked nor locked. Our own
        // lifecycle sentinels (destroyed mutex storage, attribute cells)
        // keep their rejection contract — adoption is only for foreign
        // content the guest never ran through this shim.
        if (word == kDestroyed || word == kMutexAttribute ||
            word == kConditionAttribute) {
            return nullptr;
        }
        // Real bionic pthread_mutex_lock CAS's the state field
        // unconditionally — it never validates prior content — so a mutex
        // whose storage was published through a path our shim never saw
        // (foreign or lazily initialized object storage) still locks.
        // Adopt it: normalize the guest word and register a fresh state
        // instead of answering EINVAL, which libc++ escalates to an
        // uncaught system_error.
        static std::size_t adopted_mutex_logs = 0;
        if (adopted_mutex_logs < 32) {
            ++adopted_mutex_logs;
            std::fprintf(stderr,
                         "ISOMR_MUTEX_ADOPT address=%llx word=%x\n",
                         static_cast<unsigned long long>(address), word);
            std::fflush(stderr);
        }
        if (!WriteWord(address, kUnlocked)) return nullptr;
        word = kUnlocked;
    }
    if (word != kUnlocked) return nullptr;
    std::shared_ptr<MutexState> state = std::make_shared<MutexState>();
    mutexes_.emplace(address, state);
    *status = kSuccess;
    return state;
}

std::shared_ptr<BionicSync::ConditionState> BionicSync::FindOrCreateCondition(
    std::uint64_t address, int* status) {
    if (status == nullptr) return nullptr;
    *status = kInvalid;
    std::uint32_t word = 0;
    if (!ReadWord(address, &word) || word != kUnlocked) return nullptr;

    const std::lock_guard lock(registry_mutex_);
    const auto found = conditions_.find(address);
    if (found != conditions_.end()) {
        *status = kSuccess;
        return found->second;
    }
    std::shared_ptr<ConditionState> state =
        std::make_shared<ConditionState>();
    conditions_.emplace(address, state);
    *status = kSuccess;
    return state;
}

std::shared_ptr<BionicSync::ReadWriteLockState>
BionicSync::FindOrCreateReadWriteLock(std::uint64_t address, int* status) {
    if (status == nullptr) return nullptr;
    *status = kInvalid;
    std::uint32_t word = 0;
    if (!ReadWord(address, &word) ||
        (word != kUnlocked && word != kLocked))
        return nullptr;

    const std::lock_guard lock(registry_mutex_);
    const auto found = read_write_locks_.find(address);
    if (found != read_write_locks_.end()) {
        *status = kSuccess;
        return found->second;
    }
    if (word != kUnlocked) return nullptr;
    std::shared_ptr<ReadWriteLockState> state =
        std::make_shared<ReadWriteLockState>();
    read_write_locks_.emplace(address, state);
    *status = kSuccess;
    return state;
}

std::shared_ptr<BionicSync::FutexState> BionicSync::FindOrCreateFutex(
    std::uint64_t address) {
    const std::lock_guard lock(registry_mutex_);
    const auto found = futexes_.find(address);
    if (found != futexes_.end()) return found->second;
    std::shared_ptr<FutexState> state = std::make_shared<FutexState>();
    futexes_.emplace(address, state);
    return state;
}

int BionicSync::MutexInit(std::uint64_t address,
                          std::uint64_t attributes) {
    std::uint32_t word = 0;
    if (!ReadWord(address, &word)) return kInvalid;

    const std::lock_guard lock(registry_mutex_);
    int type = kMutexNormal;
    if (attributes != 0) {
        const auto found = mutex_attributes_.find(attributes);
        if (found == mutex_attributes_.end()) return kInvalid;
        type = found->second;
    }
    if (mutexes_.contains(address)) return kBusy;
    if (!WriteWord(address, kUnlocked)) return kInvalid;
    std::shared_ptr<MutexState> state = std::make_shared<MutexState>();
    state->type = type;
    mutexes_.emplace(address, std::move(state));
    return kSuccess;
}

int BionicSync::MutexAttributeInit(std::uint64_t address) {
    std::uint32_t word = 0;
    if (!ReadWord(address, &word)) return kInvalid;
    const std::lock_guard lock(registry_mutex_);
    if (mutex_attributes_.contains(address)) return kBusy;
    if (!WriteWord(address, kMutexAttribute)) return kInvalid;
    mutex_attributes_.emplace(address, kMutexNormal);
    return kSuccess;
}

int BionicSync::MutexAttributeDestroy(std::uint64_t address) {
    const std::lock_guard lock(registry_mutex_);
    const auto found = mutex_attributes_.find(address);
    if (found == mutex_attributes_.end()) return kInvalid;
    if (!WriteWord(address, kDestroyed)) return kInvalid;
    mutex_attributes_.erase(found);
    return kSuccess;
}

int BionicSync::MutexAttributeSetType(std::uint64_t address,
                                      std::uint64_t type) {
    if (type > static_cast<std::uint64_t>(kMutexErrorCheck)) return kInvalid;
    const std::lock_guard lock(registry_mutex_);
    const auto found = mutex_attributes_.find(address);
    if (found == mutex_attributes_.end()) return kInvalid;
    found->second = static_cast<int>(type);
    return kSuccess;
}

int BionicSync::MutexAttributeSetShared(std::uint64_t address,
                                        std::uint64_t pshared) {
    if (pshared > 1) return kInvalid;
    const std::lock_guard lock(registry_mutex_);
    const auto found = mutex_attributes_.find(address);
    if (found == mutex_attributes_.end()) return kInvalid;
    // Process-shared mutexes behave identically here: the compatibility
    // runtime keeps all guest contexts in one host process.
    return kSuccess;
}

int BionicSync::ConditionAttributeInit(std::uint64_t address) {
    std::uint32_t word = 0;
    if (!ReadWord(address, &word)) return kInvalid;
    const std::lock_guard lock(registry_mutex_);
    if (condition_attributes_.contains(address)) return kBusy;
    if (!WriteWord(address, kConditionAttribute)) return kInvalid;
    condition_attributes_.emplace(address, 0);
    return kSuccess;
}

int BionicSync::ConditionAttributeDestroy(std::uint64_t address) {
    const std::lock_guard lock(registry_mutex_);
    const auto found = condition_attributes_.find(address);
    if (found == condition_attributes_.end()) return kInvalid;
    if (!WriteWord(address, kDestroyed)) return kInvalid;
    condition_attributes_.erase(found);
    return kSuccess;
}

int BionicSync::ConditionAttributeSetClock(std::uint64_t address,
                                           std::uint64_t clock_id) {
    if (clock_id > 1) return kInvalid;
    const std::lock_guard lock(registry_mutex_);
    const auto found = condition_attributes_.find(address);
    if (found == condition_attributes_.end()) return kInvalid;
    found->second = static_cast<int>(clock_id);
    return kSuccess;
}

int BionicSync::MutexDestroy(std::uint64_t address) {
    int status = kSuccess;
    const std::shared_ptr<MutexState> state =
        FindOrCreateMutex(address, &status);
    if (!state) return status;

    std::unique_lock state_lock(state->mutex);
    if (state->destroyed) return kInvalid;
    if (state->locked || state->waiters != 0) return kBusy;
    if (!WriteWord(address, kDestroyed)) return kInvalid;
    state->destroyed = true;
    state_lock.unlock();

    const std::lock_guard registry_lock(registry_mutex_);
    const auto found = mutexes_.find(address);
    if (found != mutexes_.end() && found->second == state)
        mutexes_.erase(found);
    return kSuccess;
}

int BionicSync::LockState(std::uint64_t address,
                          const std::shared_ptr<MutexState>& state,
                          bool try_only) {
    std::unique_lock lock(state->mutex);
    if (state->destroyed) return kInvalid;
    if (state->locked && state->owner == std::this_thread::get_id()) {
        if (state->type == kMutexRecursive) {
            if (state->recursion == std::numeric_limits<std::size_t>::max())
                return kTryAgain;
            ++state->recursion;
            return kSuccess;
        }
        return try_only ? kBusy : kDeadlock;
    }
    if (try_only && state->locked) return kBusy;
    if (!try_only) {
        ++state->waiters;
        state->available.wait(lock,
                              [&] { return !state->locked || state->destroyed; });
        --state->waiters;
        if (state->destroyed) return kInvalid;
    }
    state->locked = true;
    state->recursion = 1;
    state->owner = std::this_thread::get_id();
    if (!WriteWord(address, kLocked)) {
        state->owner = {};
        state->locked = false;
        state->recursion = 0;
        lock.unlock();
        state->available.notify_one();
        return kInvalid;
    }
    return kSuccess;
}

int BionicSync::MutexLock(std::uint64_t address) {
    int status = kSuccess;
    const std::shared_ptr<MutexState> state =
        FindOrCreateMutex(address, &status);
    if (!state) {
        // Phantom null-page mutex: Unity's Swappy path locks a mutex inside
        // a lazily-published singleton that was never built on a device-less
        // display. std::mutex::lock throws system_error on any nonzero
        // return, which aborts the whole render call; answering success for
        // an unmapped low address lets the guarded section read zeros and
        // early-out exactly as the "no callback" path does on-device.
        if (address < 0x10000ULL) {
            static std::size_t phantom_logs = 0;
            if (phantom_logs < 12) {
                ++phantom_logs;
                std::fprintf(stderr,
                             "ISOMR_MUTEX_PHANTOM lock address=0x%llx\n",
                             static_cast<unsigned long long>(address));
                std::fflush(stderr);
            }
            return kSuccess;
        }
        return status;
    }
    return LockState(address, state, false);
}

int BionicSync::MutexTryLock(std::uint64_t address) {
    int status = kSuccess;
    const std::shared_ptr<MutexState> state =
        FindOrCreateMutex(address, &status);
    if (!state) return address < 0x10000ULL ? kSuccess : status;
    return LockState(address, state, true);
}

int BionicSync::MutexUnlock(std::uint64_t address) {
    int status = kSuccess;
    const std::shared_ptr<MutexState> state =
        FindOrCreateMutex(address, &status);
    if (!state) return address < 0x10000ULL ? kSuccess : status;

    std::unique_lock lock(state->mutex);
    if (state->destroyed) return kInvalid;
    if (!state->locked || state->owner != std::this_thread::get_id())
        return kPermissionDenied;
    if (state->type == kMutexRecursive && state->recursion > 1) {
        --state->recursion;
        return kSuccess;
    }
    if (!WriteWord(address, kUnlocked)) return kInvalid;
    state->owner = {};
    state->locked = false;
    state->recursion = 0;
    lock.unlock();
    state->available.notify_one();
    return kSuccess;
}

int BionicSync::ReadWriteLockInit(std::uint64_t address,
                                  std::uint64_t attributes) {
    if (attributes != 0) return kInvalid;
    std::uint32_t word = 0;
    if (!ReadWord(address, &word)) return kInvalid;
    const std::lock_guard lock(registry_mutex_);
    if (read_write_locks_.contains(address)) return kBusy;
    if (!WriteWord(address, kUnlocked)) return kInvalid;
    read_write_locks_.emplace(
        address, std::make_shared<ReadWriteLockState>());
    return kSuccess;
}

int BionicSync::ReadWriteLockDestroy(std::uint64_t address) {
    int status = kSuccess;
    const std::shared_ptr<ReadWriteLockState> state =
        FindOrCreateReadWriteLock(address, &status);
    if (!state) return status;

    std::unique_lock state_lock(state->mutex);
    if (state->destroyed) return kInvalid;
    if (state->writer_locked || state->readers != 0 ||
        state->waiting_readers != 0 || state->waiting_writers != 0)
        return kBusy;
    if (!WriteWord(address, kDestroyed)) return kInvalid;
    state->destroyed = true;
    state_lock.unlock();

    const std::lock_guard registry_lock(registry_mutex_);
    const auto found = read_write_locks_.find(address);
    if (found != read_write_locks_.end() && found->second == state)
        read_write_locks_.erase(found);
    return kSuccess;
}

int BionicSync::ReadLockState(
    std::uint64_t address,
    const std::shared_ptr<ReadWriteLockState>& state, bool try_only) {
    std::unique_lock lock(state->mutex);
    if (state->destroyed) return kInvalid;
    const std::thread::id caller = std::this_thread::get_id();
    if (state->writer_locked && state->writer == caller)
        return try_only ? kBusy : kDeadlock;
    const auto available = [&] {
        return state->destroyed ||
               (!state->writer_locked && state->waiting_writers == 0);
    };
    if (try_only && !available()) return kBusy;
    if (!try_only && !available()) {
        ++state->waiting_readers;
        state->available.wait(lock, available);
        --state->waiting_readers;
        if (state->destroyed) return kInvalid;
    }
    if (state->readers == std::numeric_limits<std::size_t>::max())
        return kTryAgain;
    std::size_t& depth = state->readers_by_thread[caller];
    if (depth == std::numeric_limits<std::size_t>::max())
        return kTryAgain;
    ++state->readers;
    ++depth;
    if (!WriteWord(address, kLocked)) {
        --state->readers;
        if (--depth == 0) state->readers_by_thread.erase(caller);
        return kInvalid;
    }
    return kSuccess;
}

int BionicSync::WriteLockState(
    std::uint64_t address,
    const std::shared_ptr<ReadWriteLockState>& state, bool try_only) {
    std::unique_lock lock(state->mutex);
    if (state->destroyed) return kInvalid;
    const std::thread::id caller = std::this_thread::get_id();
    if ((state->writer_locked && state->writer == caller) ||
        state->readers_by_thread.contains(caller))
        return try_only ? kBusy : kDeadlock;
    const auto available = [&] {
        return state->destroyed ||
               (!state->writer_locked && state->readers == 0);
    };
    if (try_only && !available()) return kBusy;
    if (!try_only && !available()) {
        ++state->waiting_writers;
        state->available.wait(lock, available);
        --state->waiting_writers;
        if (state->destroyed) return kInvalid;
    }
    state->writer_locked = true;
    state->writer = caller;
    if (!WriteWord(address, kLocked)) {
        state->writer_locked = false;
        state->writer = {};
        lock.unlock();
        state->available.notify_all();
        return kInvalid;
    }
    return kSuccess;
}

int BionicSync::ReadWriteLockRead(std::uint64_t address) {
    int status = kSuccess;
    const std::shared_ptr<ReadWriteLockState> state =
        FindOrCreateReadWriteLock(address, &status);
    return state ? ReadLockState(address, state, false) : status;
}

int BionicSync::ReadWriteLockTryRead(std::uint64_t address) {
    int status = kSuccess;
    const std::shared_ptr<ReadWriteLockState> state =
        FindOrCreateReadWriteLock(address, &status);
    return state ? ReadLockState(address, state, true) : status;
}

int BionicSync::ReadWriteLockWrite(std::uint64_t address) {
    int status = kSuccess;
    const std::shared_ptr<ReadWriteLockState> state =
        FindOrCreateReadWriteLock(address, &status);
    return state ? WriteLockState(address, state, false) : status;
}

int BionicSync::ReadWriteLockTryWrite(std::uint64_t address) {
    int status = kSuccess;
    const std::shared_ptr<ReadWriteLockState> state =
        FindOrCreateReadWriteLock(address, &status);
    return state ? WriteLockState(address, state, true) : status;
}

int BionicSync::ReadWriteLockUnlock(std::uint64_t address) {
    int status = kSuccess;
    const std::shared_ptr<ReadWriteLockState> state =
        FindOrCreateReadWriteLock(address, &status);
    if (!state) return status;

    std::unique_lock lock(state->mutex);
    if (state->destroyed) return kInvalid;
    const std::thread::id caller = std::this_thread::get_id();
    if (state->writer_locked) {
        if (state->writer != caller) return kPermissionDenied;
        if (!WriteWord(address, kUnlocked)) return kInvalid;
        state->writer_locked = false;
        state->writer = {};
    } else {
        const auto found = state->readers_by_thread.find(caller);
        if (found == state->readers_by_thread.end() ||
            found->second == 0 || state->readers == 0)
            return kPermissionDenied;
        const bool last_reader = state->readers == 1;
        if (!WriteWord(address, last_reader ? kUnlocked : kLocked))
            return kInvalid;
        --state->readers;
        if (--found->second == 0) state->readers_by_thread.erase(found);
    }
    lock.unlock();
    state->available.notify_all();
    return kSuccess;
}

int BionicSync::ConditionInit(std::uint64_t address,
                              std::uint64_t attributes) {
    std::uint32_t word = 0;
    if (!ReadWord(address, &word)) return kInvalid;

    const std::lock_guard lock(registry_mutex_);
    int clock_id = 0;
    if (attributes != 0) {
        const auto found = condition_attributes_.find(attributes);
        if (found == condition_attributes_.end()) return kInvalid;
        clock_id = found->second;
    }
    if (conditions_.contains(address)) return kBusy;
    if (!WriteWord(address, kUnlocked)) return kInvalid;
    std::shared_ptr<ConditionState> state =
        std::make_shared<ConditionState>();
    state->clock_id = clock_id;
    conditions_.emplace(address, std::move(state));
    return kSuccess;
}

int BionicSync::ConditionDestroy(std::uint64_t address) {
    int status = kSuccess;
    const std::shared_ptr<ConditionState> state =
        FindOrCreateCondition(address, &status);
    if (!state) return status;

    std::unique_lock state_lock(state->mutex);
    if (state->destroyed) return kInvalid;
    if (!state->waiters.empty()) return kBusy;
    if (!WriteWord(address, kDestroyed)) return kInvalid;
    state->destroyed = true;
    state_lock.unlock();

    const std::lock_guard registry_lock(registry_mutex_);
    const auto found = conditions_.find(address);
    if (found != conditions_.end() && found->second == state)
        conditions_.erase(found);
    return kSuccess;
}

int BionicSync::WaitState(
    std::uint64_t condition_address, std::uint64_t mutex_address,
    const std::chrono::system_clock::time_point* deadline) {
    int condition_status = kSuccess;
    const std::shared_ptr<ConditionState> condition =
        FindOrCreateCondition(condition_address, &condition_status);
    if (!condition) return condition_status;
    int mutex_status = kSuccess;
    const std::shared_ptr<MutexState> mutex =
        FindOrCreateMutex(mutex_address, &mutex_status);
    if (!mutex) return mutex_status;

    std::unique_lock condition_lock(condition->mutex);
    {
        static std::mutex report_mutex;
        static std::unordered_set<std::uint64_t> reported_threads;
        std::lock_guard report_lock(report_mutex);
        if (SyncTraceEnabled() &&
            reported_threads.insert(condition_address).second)
            std::fprintf(stdout,
                "ISOMR_CTR_BLOCK thread-wait cond=0x%llx mutex=0x%llx tid=%u\n",
                (unsigned long long)condition_address,
                (unsigned long long)mutex_address,
                static_cast<unsigned>(
                    std::hash<std::thread::id>{}(std::this_thread::get_id())));
    }
    if (condition->destroyed) return kInvalid;
    std::unique_lock mutex_lock(mutex->mutex);
    if (mutex->destroyed) return kInvalid;
    if (!mutex->locked || mutex->owner != std::this_thread::get_id())
        return kPermissionDenied;
    if (mutex->recursion != 1) return kInvalid;

    const std::shared_ptr<ConditionState::Waiter> waiter =
        std::make_shared<ConditionState::Waiter>();
    condition->waiters.push_back(waiter);
    if (condition->pending_signals > 0) {
        // A signal arrived before this waiter registered: consume it here
        // so the predicate-protected wait cannot miss its wakeup.
        condition->pending_signals--;
        waiter->notified = true;
    }
    if (!WriteWord(mutex_address, kUnlocked)) {
        condition->waiters.pop_back();
        return kInvalid;
    }
    mutex->owner = {};
    mutex->locked = false;
    mutex->recursion = 0;
    mutex_lock.unlock();
    mutex->available.notify_one();

    int result = kSuccess;
    const auto ready = [&] { return waiter->notified || condition->destroyed; };
    if (deadline == nullptr) {
        const std::uint32_t poll = SyncPollMilliseconds();
        if (poll != 0) {
            // A poll expiry with !ready is returned as a normal wakeup so the
            // guest re-checks its predicate and re-waits; this self-heals a
            // lost broadcast where the predicate was already satisfied.
            condition->changed.wait_for(
                condition_lock, std::chrono::milliseconds(poll), ready);
        } else {
            condition->changed.wait(condition_lock, ready);
        }
    } else if (SyncPollMilliseconds() != 0) {
        // Deadline waits must poll too: a lost broadcast inside a
        // long-deadline wait would otherwise wedge the wait (and the
        // render call owning it) until its full deadline. Clamped
        // re-waits keep the guest-visible deadline honored while the
        // predicate gets re-checked every poll slice.
        while (!ready() && std::chrono::system_clock::now() < *deadline) {
            const auto slice =
                std::min(std::chrono::system_clock::now() +
                             std::chrono::milliseconds(
                                 SyncPollMilliseconds()),
                         *deadline);
            condition->changed.wait_until(condition_lock, slice, ready);
        }
        if (!ready()) result = kTimedOut;
    } else if (!condition->changed.wait_until(condition_lock, *deadline,
                                               ready)) {
        result = kTimedOut;
    }
    const auto found = std::find(condition->waiters.begin(),
                                 condition->waiters.end(), waiter);
    if (found != condition->waiters.end()) condition->waiters.erase(found);
    if (condition->destroyed) result = kInvalid;
    condition_lock.unlock();

    const int lock_result = LockState(mutex_address, mutex, false);
    return lock_result == kSuccess ? result : lock_result;
}

int BionicSync::ConditionWait(std::uint64_t condition_address,
                              std::uint64_t mutex_address) {
    return WaitState(condition_address, mutex_address, nullptr);
}

int BionicSync::ConditionTimedWait(std::uint64_t condition_address,
                                   std::uint64_t mutex_address,
                                   std::uint64_t absolute_time_address) {
    BionicTimespec absolute_time{};
    if (address_space_ == nullptr ||
        !address_space_->Read(absolute_time_address, &absolute_time,
                              sizeof(absolute_time)) ||
        absolute_time.seconds < 0 || absolute_time.nanoseconds < 0 ||
        absolute_time.nanoseconds >= 1000000000LL)
        return kInvalid;

    int condition_status = kSuccess;
    const std::shared_ptr<ConditionState> condition =
        FindOrCreateCondition(condition_address, &condition_status);
    if (!condition) return condition_status;
    int clock_id = 0;
    {
        const std::lock_guard lock(condition->mutex);
        if (condition->destroyed) return kInvalid;
        clock_id = condition->clock_id;
    }

    using Clock = std::chrono::system_clock;
    if (clock_id == 1) {
        if (static_cast<std::uint64_t>(absolute_time.seconds) >
            std::numeric_limits<std::uint64_t>::max() / 1000000000ULL)
            return kInvalid;
        const std::uint64_t absolute_nanoseconds =
            static_cast<std::uint64_t>(absolute_time.seconds) *
                1000000000ULL +
            static_cast<std::uint64_t>(absolute_time.nanoseconds);
        ULONGLONG unbiased_ticks = 0;
        QueryUnbiasedInterruptTimePrecise(&unbiased_ticks);
        if (unbiased_ticks >
            std::numeric_limits<std::uint64_t>::max() / 100ULL)
            return kInvalid;
        const std::uint64_t now_nanoseconds = unbiased_ticks * 100ULL;
        const std::uint64_t remaining_nanoseconds =
            absolute_nanoseconds > now_nanoseconds
                ? absolute_nanoseconds - now_nanoseconds
                : 0;
        if (remaining_nanoseconds >
            static_cast<std::uint64_t>(
                std::chrono::nanoseconds::max().count()))
            return kInvalid;
        const Clock::duration remaining =
            std::chrono::duration_cast<Clock::duration>(
                std::chrono::nanoseconds(remaining_nanoseconds));
        const Clock::time_point now = Clock::now();
        if (remaining > Clock::time_point::max() - now) return kInvalid;
        const Clock::time_point deadline = now + remaining;
        return WaitState(condition_address, mutex_address, &deadline);
    }
    if (clock_id != 0) return kInvalid;
    const std::int64_t maximum_seconds =
        std::chrono::duration_cast<std::chrono::seconds>(
            Clock::time_point::max().time_since_epoch())
            .count();
    if (absolute_time.seconds > maximum_seconds) return kInvalid;
    const Clock::duration seconds_duration =
        std::chrono::duration_cast<Clock::duration>(
            std::chrono::seconds(absolute_time.seconds));
    const Clock::duration nanoseconds_duration =
        std::chrono::duration_cast<Clock::duration>(
            std::chrono::nanoseconds(absolute_time.nanoseconds));
    if (nanoseconds_duration > Clock::time_point::max().time_since_epoch() -
                                   seconds_duration)
        return kInvalid;
    const Clock::duration deadline_duration =
        seconds_duration + nanoseconds_duration;
    const Clock::time_point deadline(deadline_duration);
    return WaitState(condition_address, mutex_address, &deadline);
}

int BionicSync::ConditionSignal(std::uint64_t address) {
    if (SyncTraceEnabled())
        std::fprintf(stdout,
                     "ISOMR_CTR_SIGNAL cond=0x%llx tid=%u\n",
                     (unsigned long long)address,
                     static_cast<unsigned>(
                         std::hash<std::thread::id>{}(std::this_thread::get_id())));
    int status = kSuccess;
    const std::shared_ptr<ConditionState> state =
        FindOrCreateCondition(address, &status);
    if (!state) return address < 0x10000ULL ? kSuccess : status;
    std::unique_lock lock(state->mutex);
    if (state->destroyed) return kInvalid;
    const auto waiter = std::find_if(
        state->waiters.begin(), state->waiters.end(),
        [](const std::shared_ptr<ConditionState::Waiter>& candidate) {
            return !candidate->notified;
        });
    if (waiter != state->waiters.end()) {
        (*waiter)->notified = true;
    } else {
        state->pending_signals++;
    }
    lock.unlock();
    state->changed.notify_all();
    return kSuccess;
}

int BionicSync::ConditionBroadcast(std::uint64_t address) {
    int status = kSuccess;
    const std::shared_ptr<ConditionState> state =
        FindOrCreateCondition(address, &status);
    if (!state) return address < 0x10000ULL ? kSuccess : status;
    std::unique_lock lock(state->mutex);
    if (state->destroyed) return kInvalid;
    if (SyncTraceEnabled()) {
        std::fprintf(stdout,
                     "ISOMR_CTR_BROADCAST cond=0x%llx waiters=%zu tid=%u\n",
                     (unsigned long long)address, state->waiters.size(),
                     static_cast<unsigned>(
                         std::hash<std::thread::id>{}(std::this_thread::get_id())));
        std::fflush(stdout);
    }
    for (const std::shared_ptr<ConditionState::Waiter>& waiter :
         state->waiters)
        waiter->notified = true;
    lock.unlock();
    state->changed.notify_all();
    return kSuccess;
}

std::size_t BionicSync::WakeAllConditionWaitersForLifecycleShutdown() {
    std::vector<std::shared_ptr<ConditionState>> states;
    {
        const std::lock_guard lock(registry_mutex_);
        states.reserve(conditions_.size());
        for (const auto& [address, state] : conditions_) {
            (void)address;
            states.push_back(state);
        }
    }
    std::size_t woken = 0;
    for (const std::shared_ptr<ConditionState>& state : states) {
        std::unique_lock lock(state->mutex);
        if (state->destroyed) continue;
        for (const std::shared_ptr<ConditionState::Waiter>& waiter :
             state->waiters) {
            if (waiter->notified) continue;
            waiter->notified = true;
            ++woken;
        }
        lock.unlock();
        state->changed.notify_all();
    }
    return woken;
}

std::size_t BionicSync::WakeAllWaiters() {
    // Opt-in: the sweep only runs while the sync-poll knob is set.
    if (SyncPollMilliseconds() == 0) return 0;
    // Snapshot the state objects under the registry lock, then signal each
    // outside it so registry_mutex_ is never held across a state mutex.
    std::vector<std::shared_ptr<FutexState>> futexes;
    std::vector<std::shared_ptr<ConditionState>> conditions;
    std::vector<std::shared_ptr<MutexState>> mutexes;
    std::vector<std::shared_ptr<ReadWriteLockState>> rwlocks;
    {
        const std::lock_guard lock(registry_mutex_);
        futexes.reserve(futexes_.size());
        for (const auto& [address, state] : futexes_) {
            (void)address;
            futexes.push_back(state);
        }
        conditions.reserve(conditions_.size());
        for (const auto& [address, state] : conditions_) {
            (void)address;
            conditions.push_back(state);
        }
        mutexes.reserve(mutexes_.size());
        for (const auto& [address, state] : mutexes_) {
            (void)address;
            mutexes.push_back(state);
        }
        rwlocks.reserve(read_write_locks_.size());
        for (const auto& [address, state] : read_write_locks_) {
            (void)address;
            rwlocks.push_back(state);
        }
    }
    std::size_t woken = 0;
    for (const std::shared_ptr<FutexState>& state : futexes) {
        std::unique_lock lock(state->mutex);
        for (const std::shared_ptr<FutexState::Waiter>& waiter :
             state->waiters) {
            if (!waiter->notified) {
                waiter->notified = true;
                ++woken;
            }
        }
        lock.unlock();
        state->changed.notify_all();
    }
    for (const std::shared_ptr<ConditionState>& state : conditions) {
        std::unique_lock lock(state->mutex);
        if (state->destroyed) continue;
        for (const std::shared_ptr<ConditionState::Waiter>& waiter :
             state->waiters) {
            if (!waiter->notified) {
                waiter->notified = true;
                ++woken;
            }
        }
        lock.unlock();
        state->changed.notify_all();
    }
    for (const std::shared_ptr<MutexState>& state : mutexes) {
        std::unique_lock lock(state->mutex);
        lock.unlock();
        state->available.notify_all();
    }
    for (const std::shared_ptr<ReadWriteLockState>& state : rwlocks) {
        std::unique_lock lock(state->mutex);
        lock.unlock();
        state->available.notify_all();
    }
    return woken;
}

std::string BionicSync::DescribeLocks() const {
    struct MutexSnap {
        std::uint64_t address;
        std::shared_ptr<MutexState> state;
    };
    struct RwSnap {
        std::uint64_t address;
        std::shared_ptr<ReadWriteLockState> state;
    };
    std::vector<MutexSnap> mutexes;
    std::vector<RwSnap> rwlocks;
    {
        const std::lock_guard lock(registry_mutex_);
        for (const auto& [address, state] : mutexes_)
            mutexes.push_back({address, state});
        for (const auto& [address, state] : read_write_locks_)
            rwlocks.push_back({address, state});
    }
    std::ostringstream out;
    std::size_t contended = 0;
    for (const MutexSnap& snap : mutexes) {
        std::unique_lock lock(snap.state->mutex);
        if (!snap.state->locked || snap.state->waiters == 0) continue;
        out << " m=0x" << std::hex << snap.address
            << " owner=" << std::hash<std::thread::id>{}(snap.state->owner)
            << " waiters=" << std::dec << snap.state->waiters
            << " rec=" << snap.state->recursion;
        ++contended;
    }
    for (const RwSnap& snap : rwlocks) {
        std::unique_lock lock(snap.state->mutex);
        if (!snap.state->writer_locked ||
            (snap.state->waiting_writers == 0 &&
             snap.state->waiting_readers == 0))
            continue;
        out << " rw=0x" << std::hex << snap.address
            << " writer=" << std::hash<std::thread::id>{}(snap.state->writer)
            << " wwait=" << std::dec << snap.state->waiting_writers
            << " rwait=" << snap.state->waiting_readers;
        ++contended;
    }
    if (contended == 0) return "none";
    return out.str();
}

int BionicSync::FutexWaitPrivate(
    std::uint64_t address, std::uint32_t expected,
    std::uint64_t relative_timeout_address) {
    if ((address & (alignof(std::uint32_t) - 1)) != 0) return kInvalid;

    BionicTimespec timeout{};
    if (relative_timeout_address != 0) {
        if (address_space_ == nullptr ||
            !address_space_->Read(relative_timeout_address, &timeout,
                                  sizeof(timeout)))
            return kBadAddress;
        if (timeout.seconds < 0 || timeout.nanoseconds < 0 ||
            timeout.nanoseconds >= 1000000000LL)
            return kInvalid;
    }

    std::uint32_t observed = 0;
    if (!ReadWord(address, &observed)) return kBadAddress;
    const std::shared_ptr<FutexState> state = FindOrCreateFutex(address);
    std::unique_lock lock(state->mutex);
    // Re-read while serialized with waiter publication and wake. This keeps
    // the compare-and-sleep transition atomic from the futex registry's view.
    if (!ReadWord(address, &observed)) return kBadAddress;
    if (observed != expected) return kTryAgain;

    const std::shared_ptr<FutexState::Waiter> waiter =
        std::make_shared<FutexState::Waiter>();
    state->waiters.push_back(waiter);
    const auto ready = [&] { return waiter->notified; };
    int result = kSuccess;
    if (SyncTraceEnabled())
        std::fprintf(stdout,
            "ISOMR_CTR_FUTEX_WAIT addr=0x%llx expected=%u timeout=%s "
            "to=%llds%09lldns thread=%u\n",
            (unsigned long long)address, expected,
            relative_timeout_address == 0 ? "none" : "bounded",
            static_cast<long long>(
                relative_timeout_address == 0 ? -1 : timeout.seconds),
            static_cast<long long>(
                relative_timeout_address == 0 ? -1 : timeout.nanoseconds),
            static_cast<unsigned>(std::hash<std::thread::id>{}(std::this_thread::get_id())));
    if (relative_timeout_address == 0) {
        const std::uint32_t poll = SyncPollMilliseconds();
        if (poll != 0) {
            // A poll expiry with !ready returns as a normal wakeup so the
            // guest re-reads the futex word, sees it unchanged, and re-waits;
            // this self-heals a lost FUTEX_WAKE where the word was already
            // updated but the wake landed between the predicate check and
            // waiter registration.
            state->changed.wait_for(lock, std::chrono::milliseconds(poll),
                                    ready);
        } else {
            state->changed.wait(lock, ready);
        }
    } else {
        constexpr std::int64_t kNanosecondsPerSecond = 1000000000LL;
        if (timeout.seconds >
            (std::chrono::nanoseconds::max().count() -
             timeout.nanoseconds) /
                kNanosecondsPerSecond) {
            state->waiters.pop_back();
            return kInvalid;
        }
        std::chrono::nanoseconds duration(
            timeout.seconds * kNanosecondsPerSecond + timeout.nanoseconds);
        const std::uint32_t poll = SyncPollMilliseconds();
        if (poll != 0 && duration > std::chrono::milliseconds(poll))
            duration = std::chrono::milliseconds(poll);
        if (!state->changed.wait_for(lock, duration, ready))
            result = kTimedOut;
    }
    const auto found = std::find(state->waiters.begin(),
                                 state->waiters.end(), waiter);
    if (found != state->waiters.end()) state->waiters.erase(found);
    if (SyncTraceEnabled())
        std::fprintf(stdout,
            "ISOMR_CTR_FUTEX_WAKE_RETURN addr=0x%llx result=%d thread=%u\n",
            (unsigned long long)address, result,
            static_cast<unsigned>(std::hash<std::thread::id>{}(std::this_thread::get_id())));
    return result;
}

int BionicSync::FutexWakePrivate(std::uint64_t address,
                                 std::uint32_t maximum,
                                 std::uint32_t* woken) {
    if (woken == nullptr) return kInvalid;
    if (SyncTraceEnabled())
        std::fprintf(stdout,
            "ISOMR_CTR_FUTEX_WAKE addr=0x%llx maximum=%u thread=%u\n",
            (unsigned long long)address, maximum,
            static_cast<unsigned>(std::hash<std::thread::id>{}(std::this_thread::get_id())));
    *woken = 0;
    if ((address & (alignof(std::uint32_t) - 1)) != 0) return kInvalid;
    std::uint32_t ignored = 0;
    if (!ReadWord(address, &ignored)) return kBadAddress;
    const std::shared_ptr<FutexState> state = FindOrCreateFutex(address);
    std::unique_lock lock(state->mutex);
    for (const std::shared_ptr<FutexState::Waiter>& waiter : state->waiters) {
        if (*woken == maximum) break;
        if (waiter->notified) continue;
        waiter->notified = true;
        ++*woken;
    }
    lock.unlock();
    if (*woken != 0) state->changed.notify_all();
    return kSuccess;
}

int BionicSync::SemaphoreInit(std::uint64_t address, std::uint32_t value) {
    if (address == 0) return kInvalid;
    const std::lock_guard registry_lock(registry_mutex_);
    auto& state = semaphores_[address];
    const std::lock_guard value_lock(state.mutex);
    state.value = value;
    state.initialized = true;
    state.cv.notify_all();
    return kSuccess;
}

int BionicSync::SemaphoreWait(std::uint64_t address) {
    std::unique_lock registry_lock(registry_mutex_);
    const auto state_it = semaphores_.find(address);
    if (state_it == semaphores_.end() || !state_it->second.initialized)
        return kInvalid;
    auto& state = state_it->second;
    registry_lock.unlock();
    std::unique_lock value_lock(state.mutex);
    state.cv.wait(value_lock, [&] { return state.value > 0; });
    --state.value;
    return kSuccess;
}

int BionicSync::SemaphoreTimedWait(std::uint64_t address,
                                   std::uint64_t timeout_address,
                                   int* timed_out, std::string* error) {
    // bionic sem_timedwait takes an absolute CLOCK_REALTIME deadline in a
    // timespec {tv_sec, tv_nsec}.
    std::int64_t deadline_seconds = 0;
    std::int64_t deadline_nanoseconds = 0;
    if (timed_out == nullptr || timeout_address == 0 ||
        !address_space_->Read(timeout_address, &deadline_seconds,
                              sizeof(deadline_seconds)) ||
        !address_space_->Read(timeout_address + 8, &deadline_nanoseconds,
                              sizeof(deadline_nanoseconds))) {
        SetError(error, "sem_timedwait timeout is unreadable");
        return -1;
    }
    const std::chrono::nanoseconds deadline(
        deadline_seconds * 1'000'000'000LL + deadline_nanoseconds);
    const std::chrono::nanoseconds now =
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch());
    const std::chrono::nanoseconds relative =
        deadline > now ? deadline - now : std::chrono::nanoseconds(0);
    std::unique_lock registry_lock(registry_mutex_);
    const auto state_it = semaphores_.find(address);
    if (state_it == semaphores_.end() || !state_it->second.initialized) {
        registry_lock.unlock();
        SetError(error, "sem_timedwait semaphore is invalid");
        return -1;
    }
    auto& state = state_it->second;
    registry_lock.unlock();
    std::unique_lock value_lock(state.mutex);
    const bool woken =
        state.cv.wait_for(value_lock, relative,
                          [&] { return state.value > 0; });
    if (!woken) {
        *timed_out = 1;
        return kTryAgain;
    }
    --state.value;
    *timed_out = 0;
    return kSuccess;
}

int BionicSync::SemaphoreTryWait(std::uint64_t address) {
    const std::lock_guard registry_lock(registry_mutex_);
    const auto state_it = semaphores_.find(address);
    if (state_it == semaphores_.end() || !state_it->second.initialized)
        return kInvalid;
    std::lock_guard value_lock(state_it->second.mutex);
    if (state_it->second.value == 0) return kTryAgain;
    --state_it->second.value;
    return kSuccess;
}

int BionicSync::SemaphorePost(std::uint64_t address) {
    const std::lock_guard registry_lock(registry_mutex_);
    auto state_it = semaphores_.find(address);
    if (state_it == semaphores_.end()) {
        state_it =
            semaphores_.try_emplace(address).first;
        state_it->second.initialized = true;
    }
    {
        const std::lock_guard value_lock(state_it->second.mutex);
        ++state_it->second.value;
    }
    state_it->second.cv.notify_all();
    return kSuccess;
}

int BionicSync::SemaphoreDestroy(std::uint64_t address) {
    const std::lock_guard registry_lock(registry_mutex_);
    semaphores_.erase(address);
    return kSuccess;
}

bool BionicSync::Shutdown(std::string* error) {
    const std::lock_guard registry_lock(registry_mutex_);
    for (const auto& [address, state] : futexes_) {
        const std::lock_guard state_lock(state->mutex);
        if (!state->waiters.empty()) {
            SetError(error, "futex waiters remain at guest address " +
                                std::to_string(address));
            return false;
        }
    }
    futexes_.clear();
    return true;
}

std::size_t BionicSync::mutex_count() const {
    const std::lock_guard lock(registry_mutex_);
    return mutexes_.size();
}

std::size_t BionicSync::mutex_attribute_count() const {
    const std::lock_guard lock(registry_mutex_);
    return mutex_attributes_.size();
}

std::size_t BionicSync::condition_attribute_count() const {
    const std::lock_guard lock(registry_mutex_);
    return condition_attributes_.size();
}

std::size_t BionicSync::condition_count() const {
    const std::lock_guard lock(registry_mutex_);
    return conditions_.size();
}

std::size_t BionicSync::read_write_lock_count() const {
    const std::lock_guard lock(registry_mutex_);
    return read_write_locks_.size();
}

std::size_t BionicSync::futex_waiter_count() const {
    const std::lock_guard registry_lock(registry_mutex_);
    std::size_t count = 0;
    for (const auto& [address, state] : futexes_) {
        (void)address;
        const std::lock_guard state_lock(state->mutex);
        count += state->waiters.size();
    }
    return count;
}

std::string BionicSync::DescribeFutexWaiters() const {
    struct Snap {
        std::uint64_t address;
        std::shared_ptr<FutexState> state;
    };
    std::vector<Snap> futexes;
    {
        const std::lock_guard registry_lock(registry_mutex_);
        for (const auto& [address, state] : futexes_)
            futexes.push_back({address, state});
    }
    std::ostringstream out;
    std::size_t reported = 0;
    for (const Snap& snap : futexes) {
        std::size_t waiters = 0;
        std::size_t notified = 0;
        {
            const std::lock_guard state_lock(snap.state->mutex);
            waiters = snap.state->waiters.size();
            for (const auto& waiter : snap.state->waiters)
                if (waiter->notified) ++notified;
        }
        if (waiters == 0) continue;
        std::uint32_t word = 0;
        const bool readable = ReadWord(snap.address, &word);
        out << " f=0x" << std::hex << snap.address << std::dec
            << " w=" << waiters << " n=" << notified << " *f=";
        if (readable)
            out << "0x" << std::hex << word << std::dec;
        else
            out << "unreadable";
        ++reported;
    }
    if (reported == 0) return "none";
    return out.str();
}

std::string BionicSync::DescribeRange(std::uint64_t address,
                                      std::size_t size) const {
    return address_space_ != nullptr
               ? address_space_->DescribeRange(address, size)
               : std::string("address-space-unavailable");
}

bool RegisterBionicSyncImports(Aarch64ImportRegistry* imports,
                               BionicSync* sync,
                               BionicThreadState* threads,
                               std::string* error) {
    if (imports == nullptr || sync == nullptr) {
        SetError(error, "bionic synchronization import dependencies are null");
        return false;
    }
    const auto register_binary = [&](const char* name, auto operation) {
        return imports->Register(
            name,
            [sync, operation, name](
                Aarch64ImportRegistry::Registers* registers,
                std::string* detail) {
                const std::uint64_t address = (*registers)[0];
                const std::uint64_t status = static_cast<std::uint64_t>(
                    (sync->*operation)(address, (*registers)[1]));
                (*registers)[0] = status;
                if (detail != nullptr)
                    *detail = "trace:sync-" + std::string(name) + "-" +
                              sync->DescribeRange(address,
                                                  sizeof(std::uint32_t)) +
                              "-status" + std::to_string(status);
                return true;
            },
            error);
    };
    const auto register_unary = [&](const char* name, auto operation) {
        return imports->Register(
            name,
            [sync, operation, name](
                Aarch64ImportRegistry::Registers* registers,
                std::string* detail) {
                const std::uint64_t address = (*registers)[0];
                const std::uint64_t status = static_cast<std::uint64_t>(
                    (sync->*operation)(address));
                (*registers)[0] = status;
                if (status == static_cast<std::uint64_t>(EINVAL)) {
                    static std::unordered_set<std::uint64_t> reported;
                    static std::mutex report_mutex;
                    const std::uint64_t caller =
                        registers->callee_saved_general.size() > 11
                            ? registers->callee_saved_general[11]
                            : 0;
                    std::lock_guard report_lock(report_mutex);
                    if (reported.insert(caller).second) {
                        // The caller's LR is the lock-thunk return; the real
                        // call site is one frame up. Stack-scan for module
                        // code pointers so the fault reports its owner.
                        std::string stack_words;
                        if (sync->address_space() != nullptr &&
                            registers->stack_pointer != 0) {
                            char word_text[32];
                            for (std::size_t i = 0; i < 24; ++i) {
                                std::uint64_t word = 0;
                                if (!sync->address_space()->Read(
                                        registers->stack_pointer + i * 8,
                                        &word, sizeof(word)))
                                    break;
                                std::snprintf(word_text, sizeof(word_text),
                                              "%s%llx", i ? "," : "",
                                              (unsigned long long)word);
                                stack_words += word_text;
                            }
                        }
                        std::fprintf(stderr,
                                     "ISOMR_SYNC_EINVAL op=%s mutex=0x%llx "
                                     "caller=0x%llx sp=0x%llx words=[%s]\n",
                                     name, (unsigned long long)address,
                                     (unsigned long long)caller,
                                     (unsigned long long)
                                         registers->stack_pointer,
                                     stack_words.c_str());
                        std::fflush(stderr);
                    }
                }
                if (detail != nullptr)
                    *detail = "trace:sync-" + std::string(name) + "-" +
                              sync->DescribeRange(address,
                                                  sizeof(std::uint32_t)) +
                              "-status" + std::to_string(status);
                return true;
            },
            error);
    };

    if (!register_binary("pthread_mutex_init", &BionicSync::MutexInit) ||
        !register_unary("pthread_mutexattr_init",
                        &BionicSync::MutexAttributeInit) ||
        !register_unary("pthread_mutexattr_destroy",
                        &BionicSync::MutexAttributeDestroy) ||
        !register_binary("pthread_mutexattr_settype",
                         &BionicSync::MutexAttributeSetType) ||
        !register_binary("pthread_mutexattr_setpshared",
                         &BionicSync::MutexAttributeSetShared) ||
        !register_unary("pthread_condattr_init",
                        &BionicSync::ConditionAttributeInit) ||
        !register_unary("pthread_condattr_destroy",
                        &BionicSync::ConditionAttributeDestroy) ||
        !register_binary("pthread_condattr_setclock",
                         &BionicSync::ConditionAttributeSetClock) ||
        !register_unary("pthread_mutex_destroy", &BionicSync::MutexDestroy) ||
        !register_unary("pthread_mutex_lock", &BionicSync::MutexLock) ||
        !register_unary("pthread_mutex_trylock", &BionicSync::MutexTryLock) ||
        !register_unary("pthread_mutex_unlock", &BionicSync::MutexUnlock) ||
        !register_binary("pthread_rwlock_init",
                         &BionicSync::ReadWriteLockInit) ||
        !register_unary("pthread_rwlock_destroy",
                        &BionicSync::ReadWriteLockDestroy) ||
        !register_unary("pthread_rwlock_rdlock",
                        &BionicSync::ReadWriteLockRead) ||
        !register_unary("pthread_rwlock_tryrdlock",
                        &BionicSync::ReadWriteLockTryRead) ||
        !register_unary("pthread_rwlock_wrlock",
                        &BionicSync::ReadWriteLockWrite) ||
        !register_unary("pthread_rwlock_trywrlock",
                        &BionicSync::ReadWriteLockTryWrite) ||
        !register_unary("pthread_rwlock_unlock",
                        &BionicSync::ReadWriteLockUnlock) ||
        !register_binary("pthread_cond_init", &BionicSync::ConditionInit) ||
        !register_unary("pthread_cond_destroy",
                        &BionicSync::ConditionDestroy) ||
        !imports->Register(
            "pthread_cond_wait",
            [sync](Aarch64ImportRegistry::Registers* registers,
                   std::string* detail) {
                const std::uint64_t address = (*registers)[0];
                const std::uint64_t mutex = (*registers)[1];
                // x30 (LR) holds the caller's return address; logging it on
                // entry lets us map an unsignaled wait back to the blocking
                // guest function even when the wait never returns. Dedup by
                // caller so each distinct wait site logs once.
                const std::uint64_t caller = registers->callee_saved_general[11];
                {
                    static std::mutex report_mutex;
                    static std::unordered_set<std::uint64_t> reported;
                    std::lock_guard report_lock(report_mutex);
                    if (reported.insert(caller).second) {
                        std::fprintf(stderr,
                                     "ISOMR_CTR_COND_WAIT_ENTER cond=0x%llx "
                                     "mutex=0x%llx caller=0x%llx tid=%u\n",
                                     (unsigned long long)address,
                                     (unsigned long long)mutex,
                                     (unsigned long long)caller,
                                     static_cast<unsigned>(
                                         std::hash<std::thread::id>{}(
                                             std::this_thread::get_id())));
                        std::fflush(stderr);
                    }
                }
                const std::uint64_t status = static_cast<std::uint64_t>(
                    sync->ConditionWait(address, mutex));
                (*registers)[0] = status;
                {
                    static std::mutex wake_mutex;
                    static std::unordered_set<std::uint64_t> woken;
                    std::lock_guard wake_lock(wake_mutex);
                    if (woken.insert(caller).second) {
                        std::fprintf(stderr,
                                     "ISOMR_CTR_COND_WAKE cond=0x%llx "
                                     "caller=0x%llx status=%llu tid=%u\n",
                                     (unsigned long long)address,
                                     (unsigned long long)caller,
                                     (unsigned long long)status,
                                     static_cast<unsigned>(
                                         std::hash<std::thread::id>{}(
                                             std::this_thread::get_id())));
                        std::fflush(stderr);
                    }
                }
                if (detail != nullptr)
                    *detail = "trace:sync-pthread_cond_wait-caller" +
                              std::to_string(caller);
                return true;
            },
            error) ||
        !register_unary("pthread_cond_signal", &BionicSync::ConditionSignal) ||
        !imports->Register(
            "sem_init",
            [sync](Aarch64ImportRegistry::Registers* registers,
                   std::string*) {
                if (sync->SemaphoreInit((*registers)[0],
                                        static_cast<std::uint32_t>(
                                            (*registers)[2])) != kSuccess)
                    return false;
                (*registers)[0] = 0;
                return true;
            },
            error) ||
        !imports->Register(
            "sem_wait",
            [sync](Aarch64ImportRegistry::Registers* registers,
                   std::string*) {
                if (sync->SemaphoreWait((*registers)[0]) != 0)
                    return false;
                (*registers)[0] = 0;
                return true;
            },
            error) ||
        !imports->Register(
            "sem_timedwait",
            [sync, threads](Aarch64ImportRegistry::Registers* registers,
                            std::string* dispatch_error) {
                int timed_out = 0;
                const int status = sync->SemaphoreTimedWait(
                    (*registers)[0], (*registers)[1], &timed_out,
                    dispatch_error);
                if (status != kSuccess && status != kTryAgain)
                    return false;
                if (status == kTryAgain) {
                    // A deadline expiry is a normal timeout, not a dispatch
                    // failure: bionic returns -1 with errno ETIMEDOUT. The
                    // errno write is best-effort because a dispatched guest
                    // thread may have no registered errno slot.
                    timed_out = 1;
                    std::string errno_error;
                    if (threads != nullptr)
                        threads->SetCurrentErrno(kTimedOut, &errno_error);
                }
                (*registers)[0] = timed_out ? static_cast<std::uint64_t>(-1)
                                            : 0;
                return true;
            },
            error) ||
        !imports->Register(
            "sem_trywait",
            [sync](Aarch64ImportRegistry::Registers* registers,
                   std::string*) {
                const int result = sync->SemaphoreTryWait((*registers)[0]);
                if (result == 3) {
                    (*registers)[0] = static_cast<std::uint64_t>(-1);
                    return true;
                }
                if (result != 0) return false;
                (*registers)[0] = 0;
                return true;
            },
            error) ||
        !imports->Register(
            "sem_post",
            [sync](Aarch64ImportRegistry::Registers* registers,
                   std::string*) {
                if (sync->SemaphorePost((*registers)[0]) != 0)
                    return false;
                (*registers)[0] = 0;
                return true;
            },
            error) ||
        !imports->Register(
            "sem_destroy",
            [sync](Aarch64ImportRegistry::Registers* registers,
                   std::string*) {
                if (sync->SemaphoreDestroy((*registers)[0]) != 0)
                    return false;
                (*registers)[0] = 0;
                return true;
            },
            error) ||

        !register_unary("pthread_cond_broadcast",
                        &BionicSync::ConditionBroadcast))
        return false;
    return imports->Register(
        "pthread_cond_timedwait",
        [sync](Aarch64ImportRegistry::Registers* registers, std::string*) {
            (*registers)[0] = static_cast<std::uint64_t>(
                sync->ConditionTimedWait((*registers)[0], (*registers)[1],
                                         (*registers)[2]));
            return true;
        },
        error);
}

}  // namespace isomr
