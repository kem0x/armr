#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

namespace isomr {

class Aarch64ImportRegistry;
class BionicThreadState;
class GuestAddressSpace;

struct BionicSignalStack {
    std::uint64_t stack_pointer = 0;
    std::int32_t flags = 0;
    std::uint32_t padding = 0;
    std::uint64_t size = 0;
};

static_assert(sizeof(BionicSignalStack) == 24);

struct BionicSignalAction {
    std::int32_t flags = 0;
    std::uint32_t padding = 0;
    std::uint64_t handler = 0;
    std::uint64_t mask = 0;
    std::uint64_t restorer = 0;
};

static_assert(sizeof(BionicSignalAction) == 32);

class BionicSignals {
public:
    BionicSignals(GuestAddressSpace* memory, BionicThreadState* threads)
        : memory_(memory), threads_(threads) {}

    bool RegisterImports(Aarch64ImportRegistry* imports, std::string* error);
    bool Shutdown(std::string* error);
    std::size_t alternate_stack_count() const;
    std::size_t action_count() const;

private:
    int AlternateStack(std::uint64_t requested_address,
                       std::uint64_t previous_address);
    int EmptySet(std::uint64_t set_address);
    int FillSet(std::uint64_t set_address);
    int AddSet(std::uint64_t set_address, std::int32_t signal);
    int DeleteSet(std::uint64_t set_address, std::int32_t signal);
    int IsMember(std::uint64_t set_address, std::int32_t signal);
    int ProcessMask(std::int32_t how, std::uint64_t set_address,
                    std::uint64_t previous_address);
    int ThreadKill(std::uint64_t thread, std::int32_t signal) const;
    int Action(std::int32_t signal, std::uint64_t requested_address,
               std::uint64_t previous_address);
    void SetErrno(std::int32_t value);

    GuestAddressSpace* memory_ = nullptr;
    BionicThreadState* threads_ = nullptr;
    mutable std::mutex mutex_;
    std::unordered_map<std::uint32_t, BionicSignalStack> alternate_stacks_;
    std::unordered_map<std::int32_t, BionicSignalAction> actions_;
    std::unordered_map<std::uint32_t, std::uint64_t> blocked_signals_;
};

}  // namespace isomr
