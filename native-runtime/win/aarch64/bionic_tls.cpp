#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "bionic_tls.h"

#include "bionic_thread_state.h"
#include "guest_address_space.h"
#include "import_registry.h"

#include <algorithm>
#include <cstdio>
#include <limits>
#include <utility>

namespace isomr {
namespace {

constexpr int kSuccess = 0;
constexpr int kTryAgain = 11;
constexpr int kBadAddress = 14;
constexpr int kInvalid = 22;

void SetError(std::string* error, std::string message) {
    if (error != nullptr) *error = std::move(message);
}

}  // namespace

BionicTls::BionicTls(BionicThreadState* threads,
                     GuestAddressSpace* address_space,
                     std::size_t maximum_keys)
    : threads_(threads), address_space_(address_space),
      maximum_keys_(std::min(
          maximum_keys,
          static_cast<std::size_t>(
              std::numeric_limits<std::uint32_t>::max()))) {
    wchar_t flag[8] = {};
    tls_trace_ = GetEnvironmentVariableW(L"ISOMR_TLS_TRACE", flag, 8) > 0 &&
                 flag[0] != L'0';
    keys_.reserve(maximum_keys_);
}

std::optional<std::uint64_t> BionicTls::CurrentThreadHandle() const {
    if (threads_ == nullptr) return std::nullopt;
    return threads_->CurrentErrnoAddress();
}

int BionicTls::KeyCreate(std::uint64_t key_address,
                         std::uint64_t destructor) {
    if (address_space_ == nullptr) return kBadAddress;
    const std::lock_guard lock(mutex_);
    std::size_t key = 0;
    while (key < keys_.size() && keys_[key].active) ++key;
    if (key == maximum_keys_) return kTryAgain;
    if (key > std::numeric_limits<std::uint32_t>::max()) return kTryAgain;
    const std::uint32_t guest_key = static_cast<std::uint32_t>(key);
    if (!address_space_->Write(key_address, &guest_key, sizeof(guest_key)))
        return kBadAddress;
    if (key == keys_.size()) keys_.push_back({});
    keys_[key] = {destructor, true};
    return kSuccess;
}

int BionicTls::KeyDelete(std::uint32_t key) {
    const std::lock_guard lock(mutex_);
    if (key >= keys_.size() || !keys_[key].active) return kInvalid;
    keys_[key] = {};
    for (auto& thread : thread_values_) thread.second.erase(key);
    for (auto iterator = thread_values_.begin();
         iterator != thread_values_.end();) {
        if (iterator->second.empty()) {
            iterator = thread_values_.erase(iterator);
        } else {
            ++iterator;
        }
    }
    return kSuccess;
}

std::uint64_t BionicTls::GetSpecific(std::uint32_t key) const {
    const std::optional<std::uint64_t> handle = CurrentThreadHandle();
    if (!handle) return 0;
    const std::lock_guard lock(mutex_);
    if (key >= keys_.size() || !keys_[key].active) return 0;
    const auto thread = thread_values_.find(*handle);
    const std::uint64_t value =
        thread == thread_values_.end()
            ? 0
            : ([&]() -> std::uint64_t {
                  const auto found = thread->second.find(key);
                  return found == thread->second.end() ? 0 : found->second;
              })();
    // Default-off diagnostic: ISOMR_TLS_TRACE reports which thread handles
    // observe a null for keys that are set elsewhere — the signature of an
    // unattached guest thread (e.g. il2cpp Thread::Current()).
    if (tls_trace_ && value == 0 && thread_values_.size() > 1) {
        bool set_elsewhere = false;
        for (const auto& [other_handle, values] : thread_values_) {
            if (other_handle == *handle) continue;
            const auto found = values.find(key);
            if (found != values.end() && found->second != 0) {
                set_elsewhere = true;
                break;
            }
        }
        if (set_elsewhere)
            std::fprintf(stderr,
                         "ISOMR_TLS_TRACE getspecific-null key=%u "
                         "handle=0x%llx set-elsewhere\n",
                         key, static_cast<unsigned long long>(*handle));
    }
    return value;
}

int BionicTls::SetSpecific(std::uint32_t key, std::uint64_t value) {
    const std::optional<std::uint64_t> handle = CurrentThreadHandle();
    if (tls_trace_ && handle)
        std::fprintf(stderr,
                     "ISOMR_TLS_TRACE setspecific key=%u handle=0x%llx "
                     "value=0x%llx\n",
                     key, static_cast<unsigned long long>(*handle),
                     static_cast<unsigned long long>(value));
    if (!handle) return kInvalid;
    const std::lock_guard lock(mutex_);
    if (key >= keys_.size() || !keys_[key].active) return kInvalid;
    if (value == 0) {
        const auto thread = thread_values_.find(*handle);
        if (thread != thread_values_.end()) {
            thread->second.erase(key);
            if (thread->second.empty()) thread_values_.erase(thread);
        }
        return kSuccess;
    }
    thread_values_[*handle][key] = value;
    return kSuccess;
}

std::vector<BionicTlsDestructorCall>
BionicTls::DrainCurrentDestructorPass() {
    std::vector<BionicTlsDestructorCall> calls;
    const std::optional<std::uint64_t> handle = CurrentThreadHandle();
    if (!handle) return calls;
    const std::lock_guard lock(mutex_);
    const auto pending = pending_destructor_calls_.find(*handle);
    if (pending != pending_destructor_calls_.end()) {
        return {pending->second.begin(), pending->second.end()};
    }
    const auto thread = thread_values_.find(*handle);
    if (thread == thread_values_.end()) return calls;
    for (auto& value : thread->second) {
        const std::uint32_t key = value.first;
        if (value.second == 0 || key >= keys_.size() ||
            !keys_[key].active || keys_[key].destructor == 0)
            continue;
        calls.push_back({keys_[key].destructor, value.second});
        value.second = 0;
    }
    for (auto iterator = thread->second.begin();
         iterator != thread->second.end();) {
        if (iterator->second == 0) {
            iterator = thread->second.erase(iterator);
        } else {
            ++iterator;
        }
    }
    if (thread->second.empty()) thread_values_.erase(thread);
    std::sort(calls.begin(), calls.end(),
              [](const BionicTlsDestructorCall& left,
                 const BionicTlsDestructorCall& right) {
                  if (left.function != right.function)
                      return left.function < right.function;
                  return left.value < right.value;
              });
    if (!calls.empty()) {
        pending_destructor_calls_.emplace(
            *handle,
            std::deque<BionicTlsDestructorCall>(calls.begin(), calls.end()));
    }
    return calls;
}

std::size_t BionicTls::DiscardCurrentThreadDestructors() {
    const std::optional<std::uint64_t> handle = CurrentThreadHandle();
    if (!handle) return 0;
    const std::lock_guard lock(mutex_);
    std::size_t discarded = 0;
    const auto pending = pending_destructor_calls_.find(*handle);
    if (pending != pending_destructor_calls_.end()) {
        discarded += pending->second.size();
        pending_destructor_calls_.erase(pending);
    }
    const auto thread = thread_values_.find(*handle);
    if (thread != thread_values_.end()) {
        for (const auto& value : thread->second) {
            const std::uint32_t key = value.first;
            if (value.second != 0 && key < keys_.size() &&
                keys_[key].active && keys_[key].destructor != 0)
                ++discarded;
        }
        thread_values_.erase(thread);
    }
    return discarded;
}

bool BionicTls::CompleteCurrentDestructorCall() {
    const std::optional<std::uint64_t> handle = CurrentThreadHandle();
    if (!handle) return false;
    const std::lock_guard lock(mutex_);
    const auto pending = pending_destructor_calls_.find(*handle);
    if (pending == pending_destructor_calls_.end() ||
        pending->second.empty())
        return false;
    pending->second.pop_front();
    if (pending->second.empty()) pending_destructor_calls_.erase(pending);
    return true;
}

bool BionicTls::ClearCurrentThreadValues(std::string* error) {
    const std::optional<std::uint64_t> handle = CurrentThreadHandle();
    if (!handle) {
        SetError(error, "current thread is not attached to bionic state");
        return false;
    }
    const std::lock_guard lock(mutex_);
    thread_values_.erase(*handle);
    pending_destructor_calls_.erase(*handle);
    return true;
}

std::size_t BionicTls::current_thread_destructor_count() const {
    const std::optional<std::uint64_t> handle = CurrentThreadHandle();
    if (!handle) return 0;
    const std::lock_guard lock(mutex_);
    std::size_t count = 0;
    const auto pending = pending_destructor_calls_.find(*handle);
    if (pending != pending_destructor_calls_.end())
        count = pending->second.size();
    const auto thread = thread_values_.find(*handle);
    if (thread == thread_values_.end()) return count;
    for (const auto& value : thread->second) {
        if (value.second != 0 && value.first < keys_.size() &&
            keys_[value.first].active &&
            keys_[value.first].destructor != 0)
            ++count;
    }
    return count;
}

std::size_t BionicTls::key_count() const {
    const std::lock_guard lock(mutex_);
    return static_cast<std::size_t>(std::count_if(
        keys_.begin(), keys_.end(),
        [](const KeyState& key) { return key.active; }));
}

std::size_t BionicTls::thread_value_count() const {
    const std::lock_guard lock(mutex_);
    std::size_t count = 0;
    for (const auto& thread : thread_values_) count += thread.second.size();
    return count;
}

bool RegisterBionicTlsImports(Aarch64ImportRegistry* imports, BionicTls* tls,
                              std::string* error) {
    if (imports == nullptr || tls == nullptr) {
        SetError(error, "bionic TLS import dependencies are null");
        return false;
    }
    if (!imports->Register(
            "pthread_key_create",
            [tls](Aarch64ImportRegistry::Registers* registers, std::string*) {
                (*registers)[0] = static_cast<std::uint64_t>(
                    tls->KeyCreate((*registers)[0], (*registers)[1]));
                return true;
            },
            error) ||
        !imports->Register(
            "pthread_key_delete",
            [tls](Aarch64ImportRegistry::Registers* registers, std::string*) {
                (*registers)[0] = static_cast<std::uint64_t>(
                    tls->KeyDelete(static_cast<std::uint32_t>(
                        (*registers)[0])));
                return true;
            },
            error) ||
        !imports->Register(
            "pthread_getspecific",
            [tls](Aarch64ImportRegistry::Registers* registers, std::string*) {
                (*registers)[0] = tls->GetSpecific(
                    static_cast<std::uint32_t>((*registers)[0]));
                return true;
            },
            error))
        return false;
    return imports->Register(
        "pthread_setspecific",
        [tls](Aarch64ImportRegistry::Registers* registers, std::string*) {
            (*registers)[0] = static_cast<std::uint64_t>(
                tls->SetSpecific(static_cast<std::uint32_t>((*registers)[0]),
                                 (*registers)[1]));
            return true;
        },
        error);
}

}  // namespace isomr
