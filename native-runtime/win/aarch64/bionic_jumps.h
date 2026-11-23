#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace isomr {

class Aarch64ImportRegistry;
class BionicThreadState;
class GuestAddressSpace;

class BionicJumps {
public:
    // Android arm64 exposes jmp_buf as 32 64-bit words. The encoded record
    // below deliberately occupies the full bounded guest allocation.
    static constexpr std::size_t kJmpBufStorageSize = 32 * sizeof(std::uint64_t);

    BionicJumps(GuestAddressSpace* memory, BionicThreadState* threads)
        : memory_(memory), threads_(threads) {}

    bool RegisterImports(Aarch64ImportRegistry* imports, std::string* error);
    bool Shutdown(std::string* error);

private:
    GuestAddressSpace* memory_ = nullptr;
    BionicThreadState* threads_ = nullptr;
    bool shutdown_ = false;
};

}  // namespace isomr
