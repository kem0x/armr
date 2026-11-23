#pragma once

#include <cstdint>
#include <mutex>
#include <string>

namespace isomr {

class Aarch64ImportRegistry;
class BionicFileSystem;
class BionicSync;
class BionicThreads;
class BionicThreadState;
class GuestAddressSpace;
class GuestHeap;

class BionicProcessIdentity {
public:
    explicit BionicProcessIdentity(GuestHeap* heap) : heap_(heap) {}

    bool GetPasswd(std::uint64_t uid, std::uint64_t* result,
                   std::string* error);
    bool Shutdown(std::string* error);

private:
    GuestHeap* heap_ = nullptr;
    std::mutex mutex_;
    std::uint64_t passwd_ = 0;
};

// Seeds a guest-visible environment variable (guest getenv/setenv round-trip
// through g_guest_env). Returns a bionic errno (0 on success). Call before the
// payload runtime initializes so a guest library's own getenv sees the value.
std::uint64_t GuestEnvSetVar(GuestAddressSpace* memory, const std::string& name,
                             const std::string& value, bool overwrite);

bool RegisterBionicProcessImports(Aarch64ImportRegistry* registry,
                                  GuestAddressSpace* memory,
                                  BionicFileSystem* files,
                                  BionicThreadState* threads,
                                  BionicSync* sync,
                                  BionicProcessIdentity* identity,
                                  BionicThreads* thread_manager,
                                  std::string* error);

}  // namespace isomr
