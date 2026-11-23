#pragma once

#include "guest_function_table.h"
#include "import_registry.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace isomr {

class BionicThreads;
class GuestAddressSpace;
class GuestHeap;

enum class NativeOpenSlMode : std::uint8_t {
    kDisabled,
    // Oboe-compatible, wall-clock-paced discard output. It exercises the
    // guest audio callback without publishing samples to a Windows sink.
    kTimedDiscard,
};

using SlResult = std::uint32_t;
inline constexpr SlResult kSlResultSuccess = 0;
inline constexpr SlResult kSlResultPreconditionsViolated = 1;
inline constexpr SlResult kSlResultParameterInvalid = 2;
inline constexpr SlResult kSlResultMemoryFailure = 3;
inline constexpr SlResult kSlResultBufferInsufficient = 7;
inline constexpr SlResult kSlResultFeatureUnsupported = 12;
inline constexpr SlResult kSlResultInternalError = 13;

class NativeOpenSl {
public:
    NativeOpenSl(GuestHeap* heap, GuestAddressSpace* address_space,
                 NativeOpenSlMode mode, BionicThreads* threads = nullptr);
    ~NativeOpenSl() noexcept;

    bool RegisterImports(Aarch64ImportRegistry* registry,
                         std::string* error);
    bool Shutdown(std::string* error);

    NativeOpenSlMode mode() const { return mode_; }
    std::size_t object_count() const;

private:
    enum class ObjectKind : std::uint8_t {
        kEngine,
        kOutputMix,
        kAudioPlayer,
    };
    enum class InterfaceKind : std::uint8_t {
        kEngine,
        kPlay,
        kBufferQueue,
        kConfiguration,
    };
    struct Player;
    struct Interface {
        InterfaceKind kind = InterfaceKind::kEngine;
        std::unique_ptr<GuestFunctionTable> table;
    };
    struct Object {
        ObjectKind kind = ObjectKind::kEngine;
        bool realized = false;
        std::unique_ptr<GuestFunctionTable> object_table;
        std::vector<Interface> interfaces;
        std::shared_ptr<Player> player;
    };

    Object* FindInterfaceOwnerLocked(std::uint64_t handle);
    std::uint64_t FindObjectInterfaceLocked(const Object& object,
                                            std::uint64_t iid) const;
    void RemoveObjectInterfacesLocked(const Object& object);
    void RetireObjectLocked(Object& object);
    bool CreateObjectLocked(ObjectKind kind, bool realized,
                            std::shared_ptr<Player> player,
                            const Aarch64ImportRegistry& registry,
                            std::uint64_t* address, std::string* error);
    bool RegisterHandler(Aarch64ImportRegistry* registry, std::string name,
                         bool (NativeOpenSl::*handler)(
                             Aarch64ImportRegistry::Registers*,
                             std::string*),
                         std::string* error);
    bool RegisterIids(Aarch64ImportRegistry* registry, std::string* error);
    bool ReleaseIids(std::string* error);
    bool SlCreateEngine(Aarch64ImportRegistry::Registers*, std::string*);
    bool ObjectRealize(Aarch64ImportRegistry::Registers*, std::string*);
    bool ObjectGetInterface(Aarch64ImportRegistry::Registers*, std::string*);
    bool ObjectDestroy(Aarch64ImportRegistry::Registers*, std::string*);
    bool EngineCreateOutputMix(Aarch64ImportRegistry::Registers*,
                               std::string*);
    bool EngineCreateAudioPlayer(Aarch64ImportRegistry::Registers*,
                                 std::string*);
    bool BufferQueueEnqueue(Aarch64ImportRegistry::Registers*, std::string*);
    bool BufferQueueClear(Aarch64ImportRegistry::Registers*, std::string*);
    bool BufferQueueGetState(Aarch64ImportRegistry::Registers*, std::string*);
    bool BufferQueueRegisterCallback(Aarch64ImportRegistry::Registers*,
                                     std::string*);
    bool PlaySetState(Aarch64ImportRegistry::Registers*, std::string*);
    bool PlayGetState(Aarch64ImportRegistry::Registers*, std::string*);
    bool PlayGetPosition(Aarch64ImportRegistry::Registers*, std::string*);
    bool ConfigurationSet(Aarch64ImportRegistry::Registers*, std::string*);
    bool ConfigurationGet(Aarch64ImportRegistry::Registers*, std::string*);
    bool WriteResultPointer(std::uint64_t output, std::uint64_t value,
                            SlResult* result) const;
    std::shared_ptr<Player> FindPlayerForInterface(
        std::uint64_t handle, InterfaceKind kind, bool require_realized,
        SlResult* result, bool* realized = nullptr);
    bool ReadGuestKey(std::uint64_t address, std::string* key) const;
    void PlayerWorkerMain(const std::shared_ptr<Player>& player) noexcept;
    void StopAndJoinPlayer(const std::shared_ptr<Player>& player) noexcept;

    GuestHeap* heap_ = nullptr;
    GuestAddressSpace* address_space_ = nullptr;
    NativeOpenSlMode mode_ = NativeOpenSlMode::kDisabled;
    BionicThreads* threads_ = nullptr;
    Aarch64ImportRegistry* registry_ = nullptr;
    std::uint64_t engine_iid_ = 0;
    std::uint64_t play_iid_ = 0;
    std::uint64_t buffer_queue_iid_ = 0;
    std::uint64_t android_simple_buffer_queue_iid_ = 0;
    std::uint64_t configuration_iid_ = 0;
    mutable std::mutex mutex_;
    std::unordered_map<std::uint64_t, Object> objects_;
    std::unordered_map<std::uint64_t, std::uint64_t> interface_owners_;
    std::vector<std::uint64_t> iid_allocations_;
    // Destroyed SL objects keep their function tables alive here: engines
    // legitimately hold SLObjectItf/interface pointers across Destroy and
    // dereference them through the object's heap table, so freeing the
    // tables turns any late touch into a host-side use-after-free.
    std::vector<std::unique_ptr<GuestFunctionTable>> retired_tables_;
    bool shutdown_ = false;
};

}  // namespace isomr
