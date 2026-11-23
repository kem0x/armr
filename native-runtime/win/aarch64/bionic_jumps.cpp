#include "bionic_jumps.h"

#include "bionic_thread_state.h"
#include "guest_address_space.h"
#include "import_registry.h"

#include <array>
#include <cstring>
#include <optional>
#include <utility>

namespace isomr {
namespace {

constexpr std::uint64_t kMagic = 0x49534f4d524a4d50ULL;  // ISOMRJMP
constexpr std::uint64_t kVersion = 1;

struct GuestJumpBuffer {
    std::uint64_t magic;
    std::uint64_t version;
    std::uint64_t owner_thread;
    std::uint64_t checksum;
    std::uint64_t platform_register;
    std::array<std::uint64_t, 12> general;
    std::uint64_t stack_pointer;
    std::array<std::uint64_t, 8> vector_low;
    std::array<std::uint64_t, 6> reserved;
};

static_assert(sizeof(GuestJumpBuffer) == BionicJumps::kJmpBufStorageSize);

void SetError(std::string* error, std::string message) {
    if (error != nullptr) *error = std::move(message);
}

std::uint64_t Checksum(GuestJumpBuffer buffer) {
    buffer.checksum = 0;
    constexpr std::uint64_t kOffset = 1469598103934665603ULL;
    constexpr std::uint64_t kPrime = 1099511628211ULL;
    std::uint64_t value = kOffset;
    const auto* bytes = reinterpret_cast<const unsigned char*>(&buffer);
    for (std::size_t index = 0; index < sizeof(buffer); ++index) {
        value ^= bytes[index];
        value *= kPrime;
    }
    return value;
}

bool CurrentThread(const BionicThreadState* threads, std::uint64_t* id,
                   std::string* error) {
    const std::optional<std::uint32_t> current =
        threads == nullptr ? std::nullopt : threads->CurrentGuestThreadId();
    if (!current) {
        SetError(error, "setjmp/longjmp requires an attached guest thread");
        return false;
    }
    *id = *current;
    return true;
}

}  // namespace

bool BionicJumps::RegisterImports(Aarch64ImportRegistry* imports,
                                  std::string* error) {
    if (imports == nullptr || memory_ == nullptr || threads_ == nullptr ||
        shutdown_) {
        SetError(error, "bionic jump dependencies are invalid");
        return false;
    }
    if (!imports->Register(
            "setjmp",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string* handler_error) {
                std::uint64_t owner = 0;
                if (shutdown_ ||
                    !CurrentThread(threads_, &owner, handler_error)) {
                    if (shutdown_)
                        SetError(handler_error,
                                 "bionic jumps are shut down");
                    return false;
                }
                GuestJumpBuffer buffer{};
                if ((*registers)[0] == 0 ||
                    !memory_->Read((*registers)[0], &buffer,
                                   sizeof(buffer)) ||
                    !memory_->Write((*registers)[0], &buffer,
                                    sizeof(buffer))) {
                    SetError(handler_error,
                             "setjmp buffer is not a writable guest allocation");
                    return false;
                }
                buffer.magic = kMagic;
                buffer.version = kVersion;
                buffer.owner_thread = owner;
                buffer.platform_register = registers->platform_register;
                buffer.general = registers->callee_saved_general;
                buffer.stack_pointer = registers->stack_pointer;
                for (std::size_t index = 0;
                     index < buffer.vector_low.size(); ++index)
                    buffer.vector_low[index] =
                        registers->callee_saved_vector[index][0];
                buffer.checksum = Checksum(buffer);
                if (!memory_->Write((*registers)[0], &buffer,
                                    sizeof(buffer))) {
                    SetError(handler_error, "setjmp buffer write failed");
                    return false;
                }
                (*registers)[0] = 0;
                return true;
            },
            error))
        return false;
    return imports->Register(
        "longjmp",
        [this](Aarch64ImportRegistry::Registers* registers,
               std::string* handler_error) {
            std::uint64_t owner = 0;
            if (shutdown_ ||
                !CurrentThread(threads_, &owner, handler_error)) {
                if (shutdown_)
                    SetError(handler_error, "bionic jumps are shut down");
                return false;
            }
            GuestJumpBuffer buffer{};
            if ((*registers)[0] == 0 ||
                !memory_->Read((*registers)[0], &buffer, sizeof(buffer))) {
                SetError(handler_error,
                         "longjmp buffer is not a readable guest allocation");
                return false;
            }
            if (buffer.magic != kMagic || buffer.version != kVersion ||
                buffer.checksum != Checksum(buffer) ||
                buffer.general[11] == 0 || buffer.stack_pointer == 0 ||
                (buffer.stack_pointer & 15U) != 0) {
                SetError(handler_error, "longjmp buffer is invalid or corrupt");
                return false;
            }
            if (buffer.owner_thread != owner) {
                SetError(handler_error,
                         "longjmp buffer belongs to another guest thread");
                return false;
            }
            const std::uint64_t value = (*registers)[1] == 0
                                            ? 1
                                            : (*registers)[1];
            registers->platform_register = buffer.platform_register;
            registers->callee_saved_general = buffer.general;
            for (std::size_t index = 0;
                 index < buffer.vector_low.size(); ++index)
                registers->callee_saved_vector[index][0] =
                    buffer.vector_low[index];
            (*registers)[0] = value;
            registers->non_local_return.requested = true;
            registers->non_local_return.program_counter = buffer.general[11];
            registers->non_local_return.stack_pointer = buffer.stack_pointer;
            return true;
        },
        error);
}

bool BionicJumps::Shutdown(std::string* error) {
    if (shutdown_) {
        SetError(error, "bionic jumps are already shut down");
        return false;
    }
    shutdown_ = true;
    return true;
}

}  // namespace isomr
