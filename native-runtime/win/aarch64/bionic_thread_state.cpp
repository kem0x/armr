#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>

#include "bionic_thread_state.h"

#include "guest_address_space.h"
#include "guest_heap.h"
#include "import_registry.h"

#include <algorithm>
#include <array>
#include <limits>
#include <utility>

namespace isomr {
namespace {

void SetError(std::string* error, std::string message) {
    if (error != nullptr) *error = std::move(message);
}

std::uint32_t CurrentThreadKey() {
    return static_cast<std::uint32_t>(GetCurrentThreadId());
}

}  // namespace

bool BionicThreadState::EnsureStackGuardLocked(std::string* error) {
    if (stack_guard_ != 0) return true;
    for (int attempt = 0; attempt < 4; ++attempt) {
        std::uint64_t candidate = 0;
        const NTSTATUS status = BCryptGenRandom(
            nullptr, reinterpret_cast<PUCHAR>(&candidate),
            static_cast<ULONG>(sizeof(candidate)),
            BCRYPT_USE_SYSTEM_PREFERRED_RNG);
        if (status < 0) {
            SetError(error, "native stack-guard generation failed");
            return false;
        }
        candidate &= ~0xffULL;
        if (candidate != 0) {
            stack_guard_ = candidate;
            return true;
        }
    }
    SetError(error, "native stack-guard generation returned zero");
    return false;
}

bool BionicThreadState::InitializeStorageLocked(std::uint64_t storage,
                                                std::string* error) {
    if (!EnsureStackGuardLocked(error)) return false;
    const std::int32_t zero = 0;
    if (!heap_->Write(storage, &zero, sizeof(zero)) ||
        !heap_->Write(storage + kStackGuardOffset, &stack_guard_,
                      sizeof(stack_guard_))) {
        SetError(error, "guest thread storage initialization failed");
        return false;
    }
    return true;
}

bool BionicThreadState::AttachCurrentThread(std::string* error) {
    if (heap_ == nullptr) {
        SetError(error, "bionic thread state has no guest heap");
        return false;
    }
    const std::lock_guard lock(mutex_);
    const std::uint32_t key = CurrentThreadKey();
    if (errno_addresses_.contains(key)) return true;
    if (!EnsureStackGuardLocked(error)) return false;
    heap_->SetAllocDebugTag("thread-errno");
    const std::uint64_t address = heap_->Callocate(1, kStorageSize);
    heap_->SetAllocDebugTag(nullptr);
    if (address == 0) {
        SetError(error, "guest thread storage allocation failed");
        return false;
    }
    if (!InitializeStorageLocked(address, error)) {
        std::string ignored;
        (void)heap_->Release(address, &ignored);
        return false;
    }
    errno_addresses_.emplace(key, address);
    guest_thread_ids_.emplace(key, next_guest_thread_id_++);
    guest_thread_nice_.emplace(key, 0);
    return true;
}

bool BionicThreadState::AttachCurrentThreadAt(std::uint64_t storage,
                                              std::string* error) {
    if (heap_ == nullptr) {
        SetError(error, "bionic thread state has no guest heap");
        return false;
    }
    const std::lock_guard lock(mutex_);
    const std::uint32_t key = CurrentThreadKey();
    const auto current = errno_addresses_.find(key);
    if (current != errno_addresses_.end()) {
        if (current->second == storage) return true;
        SetError(error, "current thread already has different guest storage");
        return false;
    }
    for (const auto& attached : errno_addresses_) {
        if (attached.second == storage) {
            SetError(error, "guest thread storage is already attached");
            return false;
        }
    }
    std::array<std::uint8_t, kStorageSize> probe{};
    if (!heap_->Read(storage, probe.data(), probe.size()) ||
        !heap_->Write(storage, probe.data(), probe.size())) {
        SetError(error, "guest thread storage is not a writable allocation");
        return false;
    }
    if (!InitializeStorageLocked(storage, error)) return false;
    errno_addresses_.emplace(key, storage);
    guest_thread_ids_.emplace(key, next_guest_thread_id_++);
    guest_thread_nice_.emplace(key, 0);
    return true;
}

bool BionicThreadState::DetachCurrentThread(std::string* error) {
    return DetachCurrentThread(true, error);
}

bool BionicThreadState::DetachCurrentThread(bool release_storage,
                                            std::string* error) {
    if (heap_ == nullptr) {
        SetError(error, "bionic thread state has no guest heap");
        return false;
    }
    std::unique_lock lock(mutex_);
    const auto found = errno_addresses_.find(CurrentThreadKey());
    if (found == errno_addresses_.end()) {
        SetError(error, "current thread has no guest errno allocation");
        return false;
    }
    const auto named = thread_names_.find(CurrentThreadKey());
    if (named != thread_names_.end() &&
        FAILED(SetThreadDescription(
            GetCurrentThread(), named->second.original_description.c_str()))) {
        SetError(error, "native thread description restoration failed");
        return false;
    }
    if (release_storage && !heap_->Release(found->second, error)) return false;
    const std::uint64_t detached_storage = found->second;
    thread_names_.erase(CurrentThreadKey());
    errno_addresses_.erase(found);
    guest_thread_ids_.erase(CurrentThreadKey());
    guest_thread_nice_.erase(CurrentThreadKey());
    DetachObserver observer = detach_observer_;
    lock.unlock();
    if (observer) observer(detached_storage);
    return true;
}

void BionicThreadState::SetDetachObserver(DetachObserver observer) {
    const std::lock_guard lock(mutex_);
    detach_observer_ = std::move(observer);
}

std::optional<std::uint64_t> BionicThreadState::CurrentErrnoAddress() const {
    const std::lock_guard lock(mutex_);
    const auto found = errno_addresses_.find(CurrentThreadKey());
    if (found == errno_addresses_.end()) return std::nullopt;
    return found->second;
}

std::optional<std::uint32_t> BionicThreadState::CurrentGuestThreadId() const {
    const std::lock_guard lock(mutex_);
    const auto found = guest_thread_ids_.find(CurrentThreadKey());
    if (found == guest_thread_ids_.end()) return std::nullopt;
    return found->second;
}

bool BionicThreadState::SetGuestThreadNice(
    std::uint32_t guest_thread_id, int android_nice,
    std::string* error) {
    if (guest_thread_id == 0 || android_nice < -20 ||
        android_nice > 19) {
        SetError(error, "guest thread priority id is invalid");
        return false;
    }
    int windows_priority = THREAD_PRIORITY_NORMAL;
    if (android_nice <= -8)
        windows_priority = THREAD_PRIORITY_HIGHEST;
    else if (android_nice < 0)
        windows_priority = THREAD_PRIORITY_ABOVE_NORMAL;
    else if (android_nice >= 10)
        windows_priority = THREAD_PRIORITY_LOWEST;
    else if (android_nice > 0)
        windows_priority = THREAD_PRIORITY_BELOW_NORMAL;
    // Keep the attachment lock until SetThreadPriority finishes. A worker
    // cannot detach and let Windows recycle its native thread id while the
    // registered guest id is being resolved.
    const std::lock_guard lock(mutex_);
    const auto found = std::find_if(
        guest_thread_ids_.begin(), guest_thread_ids_.end(),
        [guest_thread_id](const auto& attached) {
            return attached.second == guest_thread_id;
        });
    if (found == guest_thread_ids_.end()) {
        SetError(error, "guest thread priority target is not attached");
        return false;
    }
    HANDLE thread = OpenThread(THREAD_SET_INFORMATION, FALSE, found->first);
    if (thread == nullptr) {
        SetError(error,
                 "native thread priority handle open failed with error " +
                     std::to_string(GetLastError()));
        return false;
    }
    const BOOL updated = SetThreadPriority(thread, windows_priority);
    const DWORD update_error = updated ? ERROR_SUCCESS : GetLastError();
    CloseHandle(thread);
    if (!updated) {
        SetError(error,
                 "native thread priority update failed with error " +
                     std::to_string(update_error));
        return false;
    }
    guest_thread_nice_[found->first] = android_nice;
    return true;
}

std::optional<int> BionicThreadState::GuestThreadNice(
    std::uint32_t guest_thread_id) const {
    if (guest_thread_id == 0) return std::nullopt;
    const std::lock_guard lock(mutex_);
    const auto found = std::find_if(
        guest_thread_ids_.begin(), guest_thread_ids_.end(),
        [guest_thread_id](const auto& attached) {
            return attached.second == guest_thread_id;
        });
    if (found == guest_thread_ids_.end()) return std::nullopt;
    const auto priority = guest_thread_nice_.find(found->first);
    if (priority == guest_thread_nice_.end()) return std::nullopt;
    return priority->second;
}

bool BionicThreadState::SetGuestThreadAffinity(
    std::uint32_t guest_thread_id, std::uintptr_t affinity_mask,
    std::uintptr_t* previous_mask, std::string* error) {
    if (guest_thread_id == 0 || affinity_mask == 0 ||
        previous_mask == nullptr) {
        SetError(error, "guest thread affinity arguments are invalid");
        return false;
    }
    const std::lock_guard lock(mutex_);
    const auto found = std::find_if(
        guest_thread_ids_.begin(), guest_thread_ids_.end(),
        [guest_thread_id](const auto& attached) {
            return attached.second == guest_thread_id;
        });
    if (found == guest_thread_ids_.end()) {
        SetError(error, "guest thread affinity target is not attached");
        return false;
    }
    HANDLE thread = OpenThread(
        THREAD_SET_INFORMATION | THREAD_QUERY_INFORMATION, FALSE,
        found->first);
    if (thread == nullptr) {
        SetError(error,
                 "native thread affinity handle open failed with error " +
                     std::to_string(GetLastError()));
        return false;
    }
    const DWORD_PTR previous = SetThreadAffinityMask(
        thread, static_cast<DWORD_PTR>(affinity_mask));
    const DWORD update_error =
        previous != 0 ? ERROR_SUCCESS : GetLastError();
    CloseHandle(thread);
    if (previous == 0) {
        SetError(error,
                 "native thread affinity update failed with error " +
                     std::to_string(update_error));
        return false;
    }
    *previous_mask = static_cast<std::uintptr_t>(previous);
    return true;
}

std::optional<std::uintptr_t> BionicThreadState::GuestThreadAffinity(
    std::uint32_t guest_thread_id) const {
    if (guest_thread_id == 0) return std::nullopt;
    const std::lock_guard lock(mutex_);
    const auto found = std::find_if(
        guest_thread_ids_.begin(), guest_thread_ids_.end(),
        [guest_thread_id](const auto& attached) {
            return attached.second == guest_thread_id;
        });
    if (found == guest_thread_ids_.end()) return std::nullopt;
    HANDLE thread = OpenThread(THREAD_QUERY_INFORMATION, FALSE,
                               found->first);
    if (thread == nullptr) return std::nullopt;
    GROUP_AFFINITY affinity{};
    const BOOL queried = GetThreadGroupAffinity(thread, &affinity);
    CloseHandle(thread);
    if (!queried) return std::nullopt;
    return static_cast<std::uintptr_t>(affinity.Mask);
}

bool BionicThreadState::IsAttachedStorage(std::uint64_t storage) const {
    if (storage == 0) return false;
    const std::lock_guard lock(mutex_);
    return std::any_of(errno_addresses_.begin(), errno_addresses_.end(),
                       [storage](const auto& attached) {
                           return attached.second == storage;
                       });
}

bool BionicThreadState::SetCurrentErrno(std::int32_t value,
                                        std::string* error) {
    if (heap_ == nullptr) {
        SetError(error, "bionic thread state has no guest heap");
        return false;
    }
    const std::lock_guard lock(mutex_);
    const auto found = errno_addresses_.find(CurrentThreadKey());
    if (found == errno_addresses_.end() ||
        !heap_->Write(found->second, &value, sizeof(value))) {
        SetError(error, "current thread guest errno is unavailable");
        return false;
    }
    return true;
}

bool BionicThreadState::ReadCurrentErrno(std::int32_t* value,
                                         std::string* error) const {
    if (heap_ == nullptr || value == nullptr) {
        SetError(error, "bionic errno read dependencies are invalid");
        return false;
    }
    const std::lock_guard lock(mutex_);
    const auto found = errno_addresses_.find(CurrentThreadKey());
    if (found == errno_addresses_.end() ||
        !heap_->Read(found->second, value, sizeof(*value))) {
        SetError(error, "current thread guest errno is unavailable");
        return false;
    }
    return true;
}

std::optional<std::uint64_t> BionicThreadState::stack_guard() const {
    const std::lock_guard lock(mutex_);
    if (stack_guard_ == 0) return std::nullopt;
    return stack_guard_;
}

int BionicThreadState::SetCurrentThreadName(std::string_view name,
                                             std::string* error) {
    return SetCurrentThreadNameWithLimit(name, 15, error);
}

int BionicThreadState::SetCurrentJavaThreadName(std::string_view name,
                                                 std::string* error) {
    return SetCurrentThreadNameWithLimit(
        name, static_cast<std::size_t>(std::numeric_limits<int>::max()),
        error);
}

int BionicThreadState::SetCurrentThreadNameWithLimit(
    std::string_view name, std::size_t maximum_length,
    std::string* error) {
    constexpr int kIoError = 5;
    constexpr int kInvalid = 22;
    if (name.size() > maximum_length ||
        name.find('\0') != std::string_view::npos)
        return kInvalid;
    std::wstring wide;
    if (!name.empty()) {
        const int required = MultiByteToWideChar(
            CP_UTF8, MB_ERR_INVALID_CHARS, name.data(),
            static_cast<int>(name.size()), nullptr, 0);
        if (required <= 0) return kInvalid;
        wide.resize(static_cast<std::size_t>(required));
        if (MultiByteToWideChar(
                CP_UTF8, MB_ERR_INVALID_CHARS, name.data(),
                static_cast<int>(name.size()), wide.data(), required) !=
            required)
            return kInvalid;
    }

    const std::lock_guard lock(mutex_);
    const std::uint32_t key = CurrentThreadKey();
    if (!errno_addresses_.contains(key)) {
        SetError(error, "current thread is not attached to bionic state");
        return kInvalid;
    }
    auto named = thread_names_.find(key);
    std::wstring original;
    if (named == thread_names_.end()) {
        PWSTR description = nullptr;
        if (FAILED(GetThreadDescription(GetCurrentThread(), &description))) {
            SetError(error, "native thread description query failed");
            return kIoError;
        }
        if (description != nullptr) {
            original = description;
            LocalFree(description);
        }
    }
    if (FAILED(SetThreadDescription(GetCurrentThread(), wide.c_str()))) {
        SetError(error, "native thread description update failed");
        return kIoError;
    }
    if (named == thread_names_.end()) {
        thread_names_.emplace(
            key, ThreadNameState{std::string(name), std::move(original)});
    } else {
        named->second.guest_name.assign(name);
    }
    return 0;
}

std::optional<std::string> BionicThreadState::CurrentThreadName() const {
    const std::lock_guard lock(mutex_);
    const auto found = thread_names_.find(CurrentThreadKey());
    if (found == thread_names_.end()) return std::nullopt;
    return found->second.guest_name;
}

bool RegisterBionicErrnoImport(Aarch64ImportRegistry* imports,
                               BionicThreadState* threads,
                               std::string* error) {
    if (imports == nullptr || threads == nullptr) {
        SetError(error, "bionic errno import dependencies are null");
        return false;
    }
    return imports->Register(
        "__errno",
        [threads](Aarch64ImportRegistry::Registers* registers,
                  std::string* handler_error) {
            const std::optional<std::uint64_t> address =
                threads->CurrentErrnoAddress();
            if (!address) {
                SetError(handler_error,
                         "current thread is not attached to bionic state");
                return false;
            }
            (*registers)[0] = *address;
            return true;
        },
        error);
}

bool RegisterBionicThreadIdentityImports(Aarch64ImportRegistry* imports,
                                         GuestAddressSpace* memory,
                                         BionicThreadState* threads,
                                         std::string* error) {
    if (imports == nullptr || memory == nullptr || threads == nullptr) {
        SetError(error, "bionic thread identity dependencies are null");
        return false;
    }
    if (!imports->Register(
            "pthread_self",
            [threads](Aarch64ImportRegistry::Registers* registers,
                      std::string* handler_error) {
                const std::optional<std::uint64_t> address =
                    threads->CurrentErrnoAddress();
                if (!address) {
                    SetError(handler_error,
                             "current thread is not attached to bionic state");
                    return false;
                }
                (*registers)[0] = *address;
                return true;
            },
            error))
        return false;
    if (!imports->Register(
            "pthread_equal",
            [](Aarch64ImportRegistry::Registers* registers, std::string*) {
                (*registers)[0] =
                    (*registers)[0] == (*registers)[1] ? 1 : 0;
                return true;
            },
            error))
        return false;
    if (!imports->Register(
            "gettid",
            [threads](Aarch64ImportRegistry::Registers* registers,
                      std::string* handler_error) {
                const std::optional<std::uint32_t> id =
                    threads->CurrentGuestThreadId();
                if (!id) {
                    SetError(handler_error,
                             "current thread has no guest thread id");
                    return false;
                }
                (*registers)[0] = *id;
                return true;
            },
            error))
        return false;
    if (!imports->Register(
            "pthread_setname_np",
            [memory, threads](
                Aarch64ImportRegistry::Registers* registers,
                std::string* handler_error) {
                constexpr std::uint64_t kNoSuchThread = 3;
                constexpr std::uint64_t kBadAddress = 14;
                constexpr std::uint64_t kRange = 34;
                const std::optional<std::uint64_t> current =
                    threads->CurrentErrnoAddress();
                if (!current || (*registers)[0] != *current) {
                    (*registers)[0] = kNoSuchThread;
                    return true;
                }
                std::uint64_t length = 0;
                std::string read_error;
                if ((*registers)[1] == 0 ||
                    !memory->BoundedStringLength(
                        (*registers)[1], 16, &length, &read_error)) {
                    (*registers)[0] = kBadAddress;
                    return true;
                }
                if (length == 16) {
                    (*registers)[0] = kRange;
                    return true;
                }
                std::string name(static_cast<std::size_t>(length), '\0');
                if (length != 0 &&
                    !memory->Read((*registers)[1], name.data(), name.size())) {
                    (*registers)[0] = kBadAddress;
                    return true;
                }
                (*registers)[0] = static_cast<std::uint64_t>(
                    threads->SetCurrentThreadName(name, handler_error));
                return true;
            },
            error))
        return false;
    return imports->Register(
        "sched_yield",
        [](Aarch64ImportRegistry::Registers* registers, std::string*) {
            (void)SwitchToThread();
            (*registers)[0] = 0;
            return true;
        },
        error);
}

}  // namespace isomr
