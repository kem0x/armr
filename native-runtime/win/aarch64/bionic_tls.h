#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace isomr {

class Aarch64ImportRegistry;
class BionicThreadState;
class GuestAddressSpace;

struct BionicTlsDestructorCall {
    std::uint64_t function = 0;
    std::uint64_t value = 0;
};

class BionicTls {
public:
    BionicTls(BionicThreadState* threads, GuestAddressSpace* address_space,
              std::size_t maximum_keys = 128);

    int KeyCreate(std::uint64_t key_address, std::uint64_t destructor);
    int KeyDelete(std::uint32_t key);
    std::uint64_t GetSpecific(std::uint32_t key) const;
    int SetSpecific(std::uint32_t key, std::uint64_t value);

    std::vector<BionicTlsDestructorCall> DrainCurrentDestructorPass();
    // Process-exit only: drops the current thread's queued destructor calls
    // and values without running them, matching _exit semantics.
    std::size_t DiscardCurrentThreadDestructors();
    bool CompleteCurrentDestructorCall();
    bool ClearCurrentThreadValues(std::string* error);
    std::size_t current_thread_destructor_count() const;
    std::size_t key_count() const;
    std::size_t thread_value_count() const;

private:
    struct KeyState {
        std::uint64_t destructor = 0;
        bool active = false;
    };

    std::optional<std::uint64_t> CurrentThreadHandle() const;

    BionicThreadState* threads_ = nullptr;
    GuestAddressSpace* address_space_ = nullptr;
    std::size_t maximum_keys_ = 0;
    bool tls_trace_ = false;
    mutable std::mutex mutex_;
    std::vector<KeyState> keys_;
    std::unordered_map<std::uint64_t,
                       std::unordered_map<std::uint32_t, std::uint64_t>>
        thread_values_;
    std::unordered_map<std::uint64_t,
                       std::deque<BionicTlsDestructorCall>>
        pending_destructor_calls_;
};

bool RegisterBionicTlsImports(Aarch64ImportRegistry* imports, BionicTls* tls,
                              std::string* error);

}  // namespace isomr
