#include "bionic_signals.h"

#include "bionic_thread_state.h"
#include "guest_address_space.h"
#include "import_registry.h"

#include <limits>
#include <optional>
#include <utility>

namespace isomr {
namespace {

constexpr std::int32_t kSignalStackDisabled = 2;
constexpr std::int32_t kNoSuchProcess = 3;
constexpr std::int32_t kBadAddress = 14;
constexpr std::int32_t kInvalidArgument = 22;
constexpr std::int32_t kNotSupported = 95;

void SetError(std::string* error, std::string message) {
    if (error != nullptr) *error = std::move(message);
}

BionicSignalStack DisabledStack() {
    BionicSignalStack stack{};
    stack.flags = kSignalStackDisabled;
    return stack;
}

}  // namespace

void BionicSignals::SetErrno(std::int32_t value) {
    if (threads_ == nullptr) return;
    std::string ignored;
    (void)threads_->SetCurrentErrno(value, &ignored);
}

int BionicSignals::AlternateStack(std::uint64_t requested_address,
                                  std::uint64_t previous_address) {
    const std::optional<std::uint32_t> thread =
        threads_->CurrentGuestThreadId();
    if (!thread) {
        SetErrno(kInvalidArgument);
        return -1;
    }
    BionicSignalStack requested{};
    if (requested_address != 0) {
        if (!memory_->Read(requested_address, &requested,
                           sizeof(requested))) {
            SetErrno(kBadAddress);
            return -1;
        }
        if ((requested.flags != 0 &&
             requested.flags != kSignalStackDisabled) ||
            requested.padding != 0) {
            SetErrno(kInvalidArgument);
            return -1;
        }
        if (requested.flags == 0) {
            std::uint8_t probe = 0;
            if (requested.stack_pointer == 0 || requested.size == 0 ||
                requested.stack_pointer >
                    std::numeric_limits<std::uint64_t>::max() -
                        (requested.size - 1) ||
                !memory_->Read(requested.stack_pointer, &probe,
                               sizeof(probe)) ||
                !memory_->Read(requested.stack_pointer + requested.size - 1,
                               &probe, sizeof(probe))) {
                SetErrno(kBadAddress);
                return -1;
            }
        }
    }
    const std::lock_guard lock(mutex_);
    const auto found = alternate_stacks_.find(*thread);
    const BionicSignalStack previous =
        found != alternate_stacks_.end() ? found->second : DisabledStack();
    if (previous_address != 0 &&
        !memory_->Write(previous_address, &previous, sizeof(previous))) {
        SetErrno(kBadAddress);
        return -1;
    }
    if (requested_address != 0) {
        if (requested.flags == kSignalStackDisabled)
            alternate_stacks_.erase(*thread);
        else
            alternate_stacks_[*thread] = requested;
    }
    return 0;
}

int BionicSignals::EmptySet(std::uint64_t set_address) {
    constexpr std::uint64_t kEmptySignalSet = 0;
    if (!memory_->Write(set_address, &kEmptySignalSet,
                        sizeof(kEmptySignalSet))) {
        SetErrno(kBadAddress);
        return -1;
    }
    return 0;
}

int BionicSignals::FillSet(std::uint64_t set_address) {
    // Android arm64's LP64 sigset_t covers 64 signals in one 64-bit word.
    constexpr std::uint64_t kFullSignalSet = UINT64_MAX;
    if (!memory_->Write(set_address, &kFullSignalSet,
                        sizeof(kFullSignalSet))) {
        SetErrno(kBadAddress);
        return -1;
    }
    return 0;
}

int BionicSignals::AddSet(std::uint64_t set_address, std::int32_t signal) {
    if (signal < 1 || signal > 64) {
        SetErrno(kInvalidArgument);
        return -1;
    }
    std::uint64_t set = 0;
    if (!memory_->Read(set_address, &set, sizeof(set))) {
        SetErrno(kBadAddress);
        return -1;
    }
    set |= std::uint64_t{1} << (signal - 1);
    if (!memory_->Write(set_address, &set, sizeof(set))) {
        SetErrno(kBadAddress);
        return -1;
    }
    return 0;
}

int BionicSignals::DeleteSet(std::uint64_t set_address, std::int32_t signal) {
    if (signal < 1 || signal > 64) {
        SetErrno(kInvalidArgument);
        return -1;
    }
    std::uint64_t set = 0;
    if (!memory_->Read(set_address, &set, sizeof(set))) {
        SetErrno(kBadAddress);
        return -1;
    }
    set &= ~(std::uint64_t{1} << (signal - 1));
    if (!memory_->Write(set_address, &set, sizeof(set))) {
        SetErrno(kBadAddress);
        return -1;
    }
    return 0;
}

int BionicSignals::IsMember(std::uint64_t set_address, std::int32_t signal) {
    if (signal < 1 || signal > 64) {
        SetErrno(kInvalidArgument);
        return -1;
    }
    std::uint64_t set = 0;
    if (!memory_->Read(set_address, &set, sizeof(set))) {
        SetErrno(kBadAddress);
        return -1;
    }
    return (set & (std::uint64_t{1} << (signal - 1))) != 0 ? 1 : 0;
}

int BionicSignals::ProcessMask(std::int32_t how, std::uint64_t set_address,
                               std::uint64_t previous_address) {
    // SIG_BLOCK=0, SIG_UNBLOCK=1, SIG_SETMASK=2 (bionic values).
    if (how < 0 || how > 2) {
        SetErrno(kInvalidArgument);
        return -1;
    }
    std::uint64_t set = 0;
    if (set_address != 0 &&
        !memory_->Read(set_address, &set, sizeof(set))) {
        SetErrno(kBadAddress);
        return -1;
    }
    const std::uint32_t key =
        threads_->CurrentGuestThreadId().value_or(0);
    const std::lock_guard lock(mutex_);
    const std::uint64_t previous = blocked_signals_[key];
    if (previous_address != 0 &&
        !memory_->Write(previous_address, &previous, sizeof(previous))) {
        SetErrno(kBadAddress);
        return -1;
    }
    if (set_address != 0) {
        if (how == 0)
            blocked_signals_[key] = previous | set;
        else if (how == 1)
            blocked_signals_[key] = previous & ~set;
        else
            blocked_signals_[key] = set;
    }
    return 0;
}

int BionicSignals::ThreadKill(std::uint64_t thread,
                              std::int32_t signal) const {
    constexpr std::int32_t kMaximumSignal = 64;
    if (signal < 0 || signal > kMaximumSignal) return kInvalidArgument;
    if (threads_ == nullptr || !threads_->IsAttachedStorage(thread))
        return kNoSuchProcess;
    if (signal == 0) return 0;
    // Bionic signal disposition is recorded, but native guest-signal
    // delivery and interruption are not implemented. Do not claim delivery.
    return kNotSupported;
}

int BionicSignals::Action(std::int32_t signal,
                          std::uint64_t requested_address,
                          std::uint64_t previous_address) {
    constexpr std::int32_t kMaximumSignal = 64;
    constexpr std::int32_t kKillSignal = 9;
    constexpr std::int32_t kStopSignal = 19;
    if (signal < 1 || signal > kMaximumSignal ||
        (requested_address != 0 &&
         (signal == kKillSignal || signal == kStopSignal))) {
        SetErrno(kInvalidArgument);
        return -1;
    }
    BionicSignalAction requested{};
    if (requested_address != 0 &&
        !memory_->Read(requested_address, &requested, sizeof(requested))) {
        SetErrno(kBadAddress);
        return -1;
    }
    const std::lock_guard lock(mutex_);
    BionicSignalAction previous{};
    const auto found = actions_.find(signal);
    if (found != actions_.end()) previous = found->second;
    if (previous_address != 0 &&
        !memory_->Write(previous_address, &previous, sizeof(previous))) {
        SetErrno(kBadAddress);
        return -1;
    }
    if (requested_address != 0) actions_[signal] = requested;
    return 0;
}

bool BionicSignals::RegisterImports(Aarch64ImportRegistry* imports,
                                    std::string* error) {
    if (imports == nullptr || memory_ == nullptr || threads_ == nullptr) {
        SetError(error, "bionic signal import dependencies are null");
        return false;
    }
    return imports->Register(
               "sigaltstack",
               [this](Aarch64ImportRegistry::Registers* registers,
                      std::string*) {
                   (*registers)[0] = static_cast<std::uint64_t>(
                       AlternateStack((*registers)[0], (*registers)[1]));
                   return true;
               },
               error) &&
           imports->Register(
               "sigemptyset",
               [this](Aarch64ImportRegistry::Registers* registers,
                      std::string*) {
                   (*registers)[0] = static_cast<std::uint64_t>(
                       EmptySet((*registers)[0]));
                   return true;
               },
               error) &&
           imports->Register(
               "sigfillset",
               [this](Aarch64ImportRegistry::Registers* registers,
                      std::string*) {
                   (*registers)[0] = static_cast<std::uint64_t>(
                       FillSet((*registers)[0]));
                   return true;
               },
               error) &&
           imports->Register(
               "sigaddset",
               [this](Aarch64ImportRegistry::Registers* registers,
                      std::string*) {
                   (*registers)[0] = static_cast<std::uint64_t>(AddSet(
                       (*registers)[0],
                       static_cast<std::int32_t>((*registers)[1])));
                   return true;
               },
               error) &&
           imports->Register(
               "sigdelset",
               [this](Aarch64ImportRegistry::Registers* registers,
                      std::string*) {
                   (*registers)[0] = static_cast<std::uint64_t>(DeleteSet(
                       (*registers)[0],
                       static_cast<std::int32_t>((*registers)[1])));
                   return true;
               },
               error) &&
           imports->Register(
               "sigismember",
               [this](Aarch64ImportRegistry::Registers* registers,
                      std::string*) {
                   (*registers)[0] = static_cast<std::uint64_t>(IsMember(
                       (*registers)[0],
                       static_cast<std::int32_t>((*registers)[1])));
                   return true;
               },
               error) &&
           imports->Register(
               "pthread_sigmask",
               [this](Aarch64ImportRegistry::Registers* registers,
                      std::string*) {
                   (*registers)[0] = static_cast<std::uint64_t>(ProcessMask(
                       static_cast<std::int32_t>((*registers)[0]),
                       (*registers)[1], (*registers)[2]));
                   return true;
               },
               error) &&
           imports->Register(
               "sigprocmask",
               [this](Aarch64ImportRegistry::Registers* registers,
                      std::string*) {
                   (*registers)[0] = static_cast<std::uint64_t>(ProcessMask(
                       static_cast<std::int32_t>((*registers)[0]),
                       (*registers)[1], (*registers)[2]));
                   return true;
               },
               error) &&
           imports->Register(
               "pthread_kill",
               [this](Aarch64ImportRegistry::Registers* registers,
                      std::string*) {
                   (*registers)[0] = static_cast<std::uint64_t>(ThreadKill(
                       (*registers)[0],
                       static_cast<std::int32_t>((*registers)[1])));
                   return true;
               },
               error) &&
           imports->Register(
               "sigaction",
               [this](Aarch64ImportRegistry::Registers* registers,
                      std::string*) {
                   (*registers)[0] = static_cast<std::uint64_t>(Action(
                       static_cast<std::int32_t>((*registers)[0]),
                       (*registers)[1], (*registers)[2]));
                   return true;
               },
               error);
}

bool BionicSignals::Shutdown(std::string* error) {
    (void)error;
    const std::lock_guard lock(mutex_);
    alternate_stacks_.clear();
    actions_.clear();
    return true;
}

std::size_t BionicSignals::alternate_stack_count() const {
    const std::lock_guard lock(mutex_);
    return alternate_stacks_.size();
}

std::size_t BionicSignals::action_count() const {
    const std::lock_guard lock(mutex_);
    return actions_.size();
}

}  // namespace isomr
