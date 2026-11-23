#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace isomr {

class Aarch64ImportRegistry;
class BionicThreadState;
class GuestAddressSpace;

// Lost-wake poll cap (see bionic_sync.cpp). Read per wait, so
// SetSyncPollMilliseconds takes effect at the next wait without a restart.
std::uint32_t SyncPollMilliseconds();
void SetSyncPollMilliseconds(std::uint32_t ms);

class BionicSync {
public:
    explicit BionicSync(GuestAddressSpace* address_space)
        : address_space_(address_space) {}

    GuestAddressSpace* address_space() const { return address_space_; }

    int MutexInit(std::uint64_t address, std::uint64_t attributes);
    int MutexDestroy(std::uint64_t address);
    int MutexLock(std::uint64_t address);
    int MutexTryLock(std::uint64_t address);
    int MutexUnlock(std::uint64_t address);

    int ReadWriteLockInit(std::uint64_t address, std::uint64_t attributes);
    int ReadWriteLockDestroy(std::uint64_t address);
    int ReadWriteLockRead(std::uint64_t address);
    int ReadWriteLockTryRead(std::uint64_t address);
    int ReadWriteLockWrite(std::uint64_t address);
    int ReadWriteLockTryWrite(std::uint64_t address);
    int ReadWriteLockUnlock(std::uint64_t address);

    int MutexAttributeInit(std::uint64_t address);
    int MutexAttributeDestroy(std::uint64_t address);
    int MutexAttributeSetType(std::uint64_t address, std::uint64_t type);
    int MutexAttributeSetShared(std::uint64_t address,
                                std::uint64_t pshared);

    int ConditionAttributeInit(std::uint64_t address);
    int ConditionAttributeDestroy(std::uint64_t address);
    int ConditionAttributeSetClock(std::uint64_t address,
                                   std::uint64_t clock_id);

    int ConditionInit(std::uint64_t address, std::uint64_t attributes);
    int ConditionDestroy(std::uint64_t address);
    int ConditionWait(std::uint64_t condition_address,
                      std::uint64_t mutex_address);
    int ConditionTimedWait(std::uint64_t condition_address,
                           std::uint64_t mutex_address,
                           std::uint64_t absolute_time_address);
    int ConditionSignal(std::uint64_t address);
    int SemaphoreInit(std::uint64_t address, std::uint32_t value);
    int SemaphoreWait(std::uint64_t address);
    // timespec at timeout_address is absolute CLOCK_REALTIME; waits until
    // the semaphore is posted or the deadline passes (returns kTryAgain).
    int SemaphoreTimedWait(std::uint64_t address,
                           std::uint64_t timeout_address, int* timed_out,
                           std::string* error);
    int SemaphoreTryWait(std::uint64_t address);
    int SemaphorePost(std::uint64_t address);
    int SemaphoreDestroy(std::uint64_t address);
    int ConditionBroadcast(std::uint64_t address);
    std::size_t WakeAllConditionWaitersForLifecycleShutdown();
    // Opt-in lost-wake sweep: wakes every registered waiter across futex,
    // condition, mutex, and read-write-lock states so each re-checks its
    // predicate. Spurious wakes are legal for all four primitives, so this
    // self-heals a missed signal without masking a real error. Driven by the
    // script drain loop under ISOMR_SYNC_POLL_MS.
    std::size_t WakeAllWaiters();
    // Diagnostic: describe every contended lock — mutexes that are held while
    // at least one thread waits, and read-write locks with a pending writer or
    // contested writer hold. During an all-parked wedge this names the lock
    // whose owner is itself blocked, exposing the deadlock cycle.
    std::string DescribeLocks() const;

    int FutexWaitPrivate(std::uint64_t address, std::uint32_t expected,
                         std::uint64_t relative_timeout_address);
    int FutexWakePrivate(std::uint64_t address, std::uint32_t maximum,
                         std::uint32_t* woken);
    bool Shutdown(std::string* error);

    std::size_t mutex_count() const;
    std::size_t mutex_attribute_count() const;
    std::size_t condition_attribute_count() const;
    std::size_t condition_count() const;
    std::size_t read_write_lock_count() const;
    std::size_t futex_waiter_count() const;
    // Diagnostic: describe every futex that currently has registered waiters —
    // its address, live waiter count, and the word re-read through the same
    // ReadWord path the wait used. Reveals whether a parked futex's word was
    // already advanced past the guest's expected value (a lost wake) or still
    // holds the pre-signal value (a structural stall), and whether the word's
    // page is even still committed under the parked waiter.
    std::string DescribeFutexWaiters() const;
    std::string DescribeRange(std::uint64_t address,
                              std::size_t size) const;

private:
    struct MutexState {
        std::mutex mutex;
        std::condition_variable available;
        std::thread::id owner;
        std::size_t waiters = 0;
        std::size_t recursion = 0;
        int type = 0;
        bool locked = false;
        bool destroyed = false;
    };

    struct ConditionState {
        struct Waiter {
            bool notified = false;
        };

        std::mutex mutex;
        std::condition_variable changed;
        std::vector<std::shared_ptr<Waiter>> waiters;
        // Signals that arrived with no registered waiter. pthread semantics
        // drop those; retaining one pending signal prevents lost-wakeup
        // deadlocks when the guest checks its predicate outside the window
        // our condition-wait registration observes.
        std::uint64_t pending_signals = 0;
        int clock_id = 0;
        bool destroyed = false;
    };

    struct ReadWriteLockState {
        std::mutex mutex;
        std::condition_variable available;
        std::unordered_map<std::thread::id, std::size_t> readers_by_thread;
        std::thread::id writer;
        std::size_t readers = 0;
        std::size_t waiting_readers = 0;
        std::size_t waiting_writers = 0;
        bool writer_locked = false;
        bool destroyed = false;
    };

    struct SemaphoreState {
        std::mutex mutex;
        std::condition_variable cv;
        std::uint64_t value = 0;
        bool initialized = false;
    };
    struct FutexState {
        struct Waiter {
            bool notified = false;
        };

        std::mutex mutex;
        std::condition_variable changed;
        std::vector<std::shared_ptr<Waiter>> waiters;
    };

    struct BionicTimespec {
        std::int64_t seconds;
        std::int64_t nanoseconds;
    };

    std::shared_ptr<MutexState> FindOrCreateMutex(std::uint64_t address,
                                                  int* status);
    std::shared_ptr<ConditionState> FindOrCreateCondition(
        std::uint64_t address, int* status);
    std::shared_ptr<ReadWriteLockState> FindOrCreateReadWriteLock(
        std::uint64_t address, int* status);
    std::shared_ptr<FutexState> FindOrCreateFutex(std::uint64_t address);
    int LockState(std::uint64_t address,
                  const std::shared_ptr<MutexState>& state, bool try_only);
    int WaitState(std::uint64_t condition_address,
                  std::uint64_t mutex_address,
                  const std::chrono::system_clock::time_point* deadline);
    int ReadLockState(std::uint64_t address,
                      const std::shared_ptr<ReadWriteLockState>& state,
                      bool try_only);
    int WriteLockState(std::uint64_t address,
                       const std::shared_ptr<ReadWriteLockState>& state,
                       bool try_only);
    bool ReadWord(std::uint64_t address, std::uint32_t* value) const;
    bool WriteWord(std::uint64_t address, std::uint32_t value);

    GuestAddressSpace* address_space_ = nullptr;
    mutable std::mutex registry_mutex_;
    std::unordered_map<std::uint64_t, std::shared_ptr<MutexState>> mutexes_;
    std::unordered_map<std::uint64_t, int> mutex_attributes_;
    std::unordered_map<std::uint64_t, int> condition_attributes_;
    std::unordered_map<std::uint64_t, std::shared_ptr<ConditionState>>
        conditions_;
    std::unordered_map<std::uint64_t, std::shared_ptr<ReadWriteLockState>>
        read_write_locks_;
    std::unordered_map<std::uint64_t, std::shared_ptr<FutexState>> futexes_;
    std::unordered_map<std::uint64_t, SemaphoreState> semaphores_;
};

bool RegisterBionicSyncImports(Aarch64ImportRegistry* imports,
                               BionicSync* sync,
                               BionicThreadState* threads, std::string* error);

}  // namespace isomr
