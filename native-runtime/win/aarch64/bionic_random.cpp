#include "bionic_random.h"

#include "import_registry.h"

namespace isomr {

bool BionicRandom::RegisterImports(Aarch64ImportRegistry* registry,
                                   std::string* error) {
    if (registry == nullptr) return false;
    if (!registry->Register(
            "srand",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string*) {
                const std::lock_guard lock(mutex_);
                state_ = static_cast<std::uint32_t>((*registers)[0]);
                (*registers)[0] = 0;
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "rand",
            [this](Aarch64ImportRegistry::Registers* registers, std::string*) {
                const std::lock_guard lock(mutex_);
                state_ = state_ * 1103515245U + 12345U;
                (*registers)[0] = (state_ >> 1) & 0x7fffffffU;
                return true;
            },
            error))
        return false;
    // srand48(seed): X = (seed << 16) | 0x330E.
    if (!registry->Register(
            "srand48",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string*) {
                const std::lock_guard lock(mutex_);
                state48_ = (static_cast<std::uint64_t>(
                                static_cast<std::uint32_t>((*registers)[0]))
                            << 16) |
                           0x330e;
                (*registers)[0] = 0;
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "lrand48",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string*) {
                const std::lock_guard lock(mutex_);
                Next48();
                (*registers)[0] =
                    static_cast<std::uint64_t>(state48_ >> 17) & 0x7fffffffULL;
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "mrand48",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string*) {
                const std::lock_guard lock(mutex_);
                Next48();
                (*registers)[0] = static_cast<std::uint64_t>(
                    static_cast<std::int64_t>(state48_) >> 16);
                return true;
            },
            error))
        return false;
    // drand48(): X / 2^48 as a double in [0,1); the result lands in the
    // vector result slot (d0) on this import ABI.
    return registry->Register(
        "drand48",
        [this](Aarch64ImportRegistry::Registers* registers, std::string*) {
            const std::lock_guard lock(mutex_);
            Next48();
            const double value =
                static_cast<double>(state48_) * (1.0 / 281474976710656.0);
            registers->SetDoubleResult(value);
            return true;
        },
        error);
}

void BionicRandom::Next48() {
    state48_ = (state48_ * 0x5deece66dULL + 0xbULL) & 0xffffffffffffULL;
}

}  // namespace isomr
