#pragma once

#include <cstdint>
#include <mutex>
#include <string>

namespace isomr {

class Aarch64ImportRegistry;

class BionicRandom {
public:
    bool RegisterImports(Aarch64ImportRegistry* registry,
                         std::string* error);

private:
    std::mutex mutex_;
    std::uint32_t state_ = 1;
    // drand48 family: shared 48-bit LCG state (X_{n+1} = a*X_n + c mod 2^48).
    std::uint64_t state48_ = 0x330e;
    void Next48();
};

}  // namespace isomr
