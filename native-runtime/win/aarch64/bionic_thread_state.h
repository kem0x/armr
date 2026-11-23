#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace isomr {

class Aarch64ImportRegistry;
class GuestAddressSpace;
class GuestHeap;

class BionicThreadState {
public:
    static constexpr std::size_t kStorageSize = 64;
    static constexpr std::size_t kStackGuardOffset = 5 * sizeof(std::uint64_t);

    explicit BionicThreadState(GuestHeap* heap) : heap_(heap) {}

    // Runs after a thread's storage is released from the attachment table so
    // owners of per-thread caches (executors, sessions) can drop entries that
    // pin resources keyed by the detached storage address.
    using DetachObserver = std::function<void(std::uint64_t storage)>;

    bool AttachCurrentThread(std::string* error);
    bool AttachCurrentThreadAt(std::uint64_t storage, std::string* error);
    bool DetachCurrentThread(std::string* error);
    bool DetachCurrentThread(bool release_storage, std::string* error);
    void SetDetachObserver(DetachObserver observer);
    std::optional<std::uint64_t> CurrentErrnoAddress() const;
    std::optional<std::uint32_t> CurrentGuestThreadId() const;
    bool SetGuestThreadNice(std::uint32_t guest_thread_id,
                            int android_nice, std::string* error);
    std::optional<int> GuestThreadNice(
        std::uint32_t guest_thread_id) const;
    bool SetGuestThreadAffinity(std::uint32_t guest_thread_id,
                                std::uintptr_t affinity_mask,
                                std::uintptr_t* previous_mask,
                                std::string* error);
    std::optional<std::uintptr_t> GuestThreadAffinity(
        std::uint32_t guest_thread_id) const;
    bool IsAttachedStorage(std::uint64_t storage) const;
    bool SetCurrentErrno(std::int32_t value, std::string* error);
    bool ReadCurrentErrno(std::int32_t* value, std::string* error) const;
    std::optional<std::uint64_t> stack_guard() const;
    int SetCurrentThreadName(std::string_view name, std::string* error);
    int SetCurrentJavaThreadName(std::string_view name, std::string* error);
    std::optional<std::string> CurrentThreadName() const;

private:
    int SetCurrentThreadNameWithLimit(std::string_view name,
                                      std::size_t maximum_length,
                                      std::string* error);
    bool EnsureStackGuardLocked(std::string* error);
    bool InitializeStorageLocked(std::uint64_t storage, std::string* error);

    GuestHeap* heap_ = nullptr;
    mutable std::mutex mutex_;
    std::unordered_map<std::uint32_t, std::uint64_t> errno_addresses_;
    std::unordered_map<std::uint32_t, std::uint32_t> guest_thread_ids_;
    std::unordered_map<std::uint32_t, int> guest_thread_nice_;
    struct ThreadNameState {
        std::string guest_name;
        std::wstring original_description;
    };
    std::unordered_map<std::uint32_t, ThreadNameState> thread_names_;
    std::uint32_t next_guest_thread_id_ = 1;
    std::uint64_t stack_guard_ = 0;
    DetachObserver detach_observer_;
};

bool RegisterBionicErrnoImport(Aarch64ImportRegistry* imports,
                               BionicThreadState* threads,
                               std::string* error);
bool RegisterBionicThreadIdentityImports(Aarch64ImportRegistry* imports,
                                         GuestAddressSpace* memory,
                                         BionicThreadState* threads,
                                         std::string* error);

}  // namespace isomr
