#include "native_opensles.h"

#include "bionic_threads.h"
#include "guest_address_space.h"
#include "guest_heap.h"

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <limits>
#include <string_view>
#include <thread>
#include <utility>

namespace isomr {
namespace {

void SetError(std::string* error, std::string message) {
    if (error != nullptr) *error = std::move(message);
}

constexpr std::string_view kObjectRealize =
    "__isomr_opensl_object_realize";
constexpr std::string_view kObjectGetInterface =
    "__isomr_opensl_object_get_interface";
constexpr std::string_view kObjectDestroy =
    "__isomr_opensl_object_destroy";
constexpr std::string_view kEngineCreateAudioPlayer =
    "__isomr_opensl_engine_create_audio_player";
constexpr std::string_view kEngineCreateOutputMix =
    "__isomr_opensl_engine_create_output_mix";
constexpr std::string_view kBufferQueueEnqueue =
    "__isomr_opensl_buffer_queue_enqueue";
constexpr std::string_view kBufferQueueClear =
    "__isomr_opensl_buffer_queue_clear";
constexpr std::string_view kBufferQueueGetState =
    "__isomr_opensl_buffer_queue_get_state";
constexpr std::string_view kBufferQueueRegisterCallback =
    "__isomr_opensl_buffer_queue_register_callback";
constexpr std::string_view kPlaySetState =
    "__isomr_opensl_play_set_state";
constexpr std::string_view kPlayGetState =
    "__isomr_opensl_play_get_state";
constexpr std::string_view kPlayGetPosition =
    "__isomr_opensl_play_get_position";
constexpr std::string_view kConfigurationSet =
    "__isomr_opensl_configuration_set";
constexpr std::string_view kConfigurationGet =
    "__isomr_opensl_configuration_get";

constexpr std::uint32_t kPlayStateStopped = 1;
constexpr std::uint32_t kPlayStatePaused = 2;
constexpr std::uint32_t kPlayStatePlaying = 3;
constexpr std::uint32_t kDataLocatorOutputMix = 4;
constexpr std::uint32_t kDataFormatPcm = 2;
constexpr std::uint32_t kAndroidDataFormatPcmEx = 4;
constexpr std::uint32_t kDataLocatorAndroidSimpleBufferQueue = 0x800007bdU;
constexpr std::size_t kMaximumInterfaces = 16;
constexpr std::size_t kMaximumQueueBuffers = 64;
constexpr std::string_view kPerformanceModeKey = "androidPerformanceMode";
constexpr std::string_view kStreamTypeKey = "androidPlaybackStreamType";

struct IidDefinition {
    std::string_view name;
    std::uint64_t discriminator;
};

constexpr std::array<IidDefinition, 8> kIids{{
    {"SL_IID_ENGINE", 1},
    {"SL_IID_OUTPUTMIX", 2},
    {"SL_IID_PLAY", 3},
    {"SL_IID_RECORD", 4},
    {"SL_IID_BUFFERQUEUE", 5},
    {"SL_IID_ANDROIDSIMPLEBUFFERQUEUE", 6},
    {"SL_IID_ANDROIDCONFIGURATION", 7},
    {"SL_IID_VOLUME", 8},
}};

struct SlDataPointerPair {
    std::uint64_t locator = 0;
    std::uint64_t format = 0;
};

struct SlOutputMixLocator {
    std::uint32_t locator_type = 0;
    std::uint32_t padding = 0;
    std::uint64_t output_mix = 0;
};

struct PlayerConfiguration {
    std::uint64_t output_mix = 0;
    std::uint32_t queue_capacity = 0;
    std::uint32_t sample_rate_hz = 0;
    std::uint32_t bytes_per_frame = 0;
    bool queue_requested = false;
    bool configuration_requested = false;
};

}  // namespace

struct NativeOpenSl::Player {
    struct Buffer {
        std::uint64_t sequence = 0;
        std::uint64_t frames = 0;
        std::chrono::nanoseconds duration{};
    };

    std::uint32_t queue_capacity = 0;
    std::uint32_t sample_rate_hz = 0;
    std::uint32_t bytes_per_frame = 0;
    bool queue_requested = false;
    bool configuration_requested = false;
    std::uint64_t play_handle = 0;
    std::uint64_t queue_handle = 0;
    std::uint64_t configuration_handle = 0;
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<Buffer> queue;
    std::thread worker;
    std::uint64_t next_sequence = 1;
    std::uint64_t callback = 0;
    std::uint64_t callback_context = 0;
    std::uint64_t completed_frames = 0;
    std::uint32_t play_state = kPlayStateStopped;
    std::uint32_t queue_index = 0;
    std::uint32_t performance_mode = 1;
    std::uint32_t stream_type = 3;
    std::string worker_error;
    bool stop = false;
};

NativeOpenSl::NativeOpenSl(GuestHeap* heap,
                           GuestAddressSpace* address_space,
                           NativeOpenSlMode mode, BionicThreads* threads)
    : heap_(heap), address_space_(address_space), mode_(mode),
      threads_(threads) {}

NativeOpenSl::~NativeOpenSl() noexcept {
    if (!shutdown_) (void)Shutdown(nullptr);
}

bool NativeOpenSl::RegisterHandler(
    Aarch64ImportRegistry* registry, std::string name,
    bool (NativeOpenSl::*handler)(Aarch64ImportRegistry::Registers*,
                                  std::string*),
    std::string* error) {
    return registry->Register(
        std::move(name),
        [this, handler](Aarch64ImportRegistry::Registers* registers,
                        std::string* dispatch_error) {
            return (this->*handler)(registers, dispatch_error);
        },
        error);
}

bool NativeOpenSl::RegisterIids(Aarch64ImportRegistry* registry,
                                std::string* error) {
    for (const IidDefinition& definition : kIids) {
        const std::array<std::uint64_t, 2> token{
            0x49534f4d524f534cULL, definition.discriminator};
        const std::uint64_t token_address =
            heap_->AllocatePinned(sizeof(token));
        if (token_address == 0) {
            SetError(error, "native OpenSL IID token allocation failed");
            return false;
        }
        iid_allocations_.push_back(token_address);
        if (!heap_->Write(token_address, token.data(), sizeof(token))) {
            SetError(error, "native OpenSL IID token write failed");
            return false;
        }
        const std::uint64_t symbol_address =
            heap_->AllocatePinned(sizeof(token_address));
        if (symbol_address == 0) {
            SetError(error, "native OpenSL IID symbol allocation failed");
            return false;
        }
        iid_allocations_.push_back(symbol_address);
        if (!heap_->Write(symbol_address, &token_address,
                          sizeof(token_address)) ||
            !registry->RegisterDataAddress(std::string(definition.name),
                                           symbol_address, error))
            return false;
        if (definition.name == "SL_IID_ENGINE") engine_iid_ = token_address;
        if (definition.name == "SL_IID_PLAY") play_iid_ = token_address;
        if (definition.name == "SL_IID_BUFFERQUEUE")
            buffer_queue_iid_ = token_address;
        if (definition.name == "SL_IID_ANDROIDSIMPLEBUFFERQUEUE")
            android_simple_buffer_queue_iid_ = token_address;
        if (definition.name == "SL_IID_ANDROIDCONFIGURATION")
            configuration_iid_ = token_address;
    }
    return true;
}

bool NativeOpenSl::RegisterImports(Aarch64ImportRegistry* registry,
                                   std::string* error) {
    if (mode_ == NativeOpenSlMode::kDisabled) return true;
    if (heap_ == nullptr || address_space_ == nullptr || threads_ == nullptr ||
        registry == nullptr || registry_ != nullptr || shutdown_) {
        SetError(error, "native OpenSL timed-discard configuration is invalid");
        return false;
    }
    if (!RegisterIids(registry, error) ||
        !RegisterHandler(registry, "slCreateEngine",
                         &NativeOpenSl::SlCreateEngine, error) ||
        !RegisterHandler(registry, std::string(kObjectRealize),
                         &NativeOpenSl::ObjectRealize, error) ||
        !RegisterHandler(registry, std::string(kObjectGetInterface),
                         &NativeOpenSl::ObjectGetInterface, error) ||
        !RegisterHandler(registry, std::string(kObjectDestroy),
                         &NativeOpenSl::ObjectDestroy, error) ||
        !RegisterHandler(registry, std::string(kEngineCreateAudioPlayer),
                         &NativeOpenSl::EngineCreateAudioPlayer, error) ||
        !RegisterHandler(registry, std::string(kEngineCreateOutputMix),
                         &NativeOpenSl::EngineCreateOutputMix, error) ||
        !RegisterHandler(registry, std::string(kBufferQueueEnqueue),
                         &NativeOpenSl::BufferQueueEnqueue, error) ||
        !RegisterHandler(registry, std::string(kBufferQueueClear),
                         &NativeOpenSl::BufferQueueClear, error) ||
        !RegisterHandler(registry, std::string(kBufferQueueGetState),
                         &NativeOpenSl::BufferQueueGetState, error) ||
        !RegisterHandler(registry, std::string(kBufferQueueRegisterCallback),
                         &NativeOpenSl::BufferQueueRegisterCallback, error) ||
        !RegisterHandler(registry, std::string(kPlaySetState),
                         &NativeOpenSl::PlaySetState, error) ||
        !RegisterHandler(registry, std::string(kPlayGetState),
                         &NativeOpenSl::PlayGetState, error) ||
        !RegisterHandler(registry, std::string(kPlayGetPosition),
                         &NativeOpenSl::PlayGetPosition, error) ||
        !RegisterHandler(registry, std::string(kConfigurationSet),
                         &NativeOpenSl::ConfigurationSet, error) ||
        !RegisterHandler(registry, std::string(kConfigurationGet),
                         &NativeOpenSl::ConfigurationGet, error)) {
        (void)ReleaseIids(nullptr);
        return false;
    }
    registry_ = registry;
    return true;
}

NativeOpenSl::Object* NativeOpenSl::FindInterfaceOwnerLocked(
    std::uint64_t handle) {
    const auto owner = interface_owners_.find(handle);
    if (owner == interface_owners_.end()) return nullptr;
    const auto direct = objects_.find(owner->second);
    return direct == objects_.end() ? nullptr : &direct->second;
}

std::uint64_t NativeOpenSl::FindObjectInterfaceLocked(
    const Object& object, std::uint64_t iid) const {
    InterfaceKind wanted = InterfaceKind::kEngine;
    if (object.kind == ObjectKind::kEngine && iid == engine_iid_) {
        wanted = InterfaceKind::kEngine;
    } else if (object.kind == ObjectKind::kAudioPlayer && iid == play_iid_) {
        wanted = InterfaceKind::kPlay;
    } else if (object.kind == ObjectKind::kAudioPlayer &&
               (iid == buffer_queue_iid_ ||
                iid == android_simple_buffer_queue_iid_)) {
        if (!object.player || !object.player->queue_requested) return 0;
        wanted = InterfaceKind::kBufferQueue;
    } else if (object.kind == ObjectKind::kAudioPlayer &&
               iid == configuration_iid_) {
        if (!object.player || !object.player->configuration_requested)
            return 0;
        wanted = InterfaceKind::kConfiguration;
    } else {
        return 0;
    }
    for (const Interface& interface : object.interfaces) {
        if (interface.kind == wanted && interface.table)
            return interface.table->object_address();
    }
    return 0;
}

void NativeOpenSl::RemoveObjectInterfacesLocked(const Object& object) {
    for (const Interface& interface : object.interfaces) {
        if (interface.table)
            interface_owners_.erase(interface.table->object_address());
    }
}

void NativeOpenSl::RetireObjectLocked(Object& object) {
    // Keep every function-table allocation alive: the engine may hold
    // SLObjectItf/interface pointers across Destroy and dereference them
    // (reading the table's heap_ member) long after the object is gone.
    std::fprintf(stderr, "ISOMR_OPENSL_RETIRE kind=%d ifaces=%zu\n",
                 static_cast<int>(object.kind), object.interfaces.size());
    std::fflush(stderr);
    RemoveObjectInterfacesLocked(object);
    if (object.object_table)
        retired_tables_.push_back(std::move(object.object_table));
    for (Interface& interface : object.interfaces) {
        if (interface.table)
            retired_tables_.push_back(std::move(interface.table));
    }
}

bool NativeOpenSl::CreateObjectLocked(
    ObjectKind kind, bool realized, std::shared_ptr<Player> player,
    const Aarch64ImportRegistry& registry, std::uint64_t* address,
    std::string* error) {
    if (address == nullptr) {
        SetError(error, "native OpenSL object output is null");
        return false;
    }
    auto object_table = std::make_unique<GuestFunctionTable>(heap_, 10, error);
    if (!object_table->valid()) return false;
    for (std::size_t slot = 0; slot < 10; ++slot) {
        bool assigned = true;
        if (slot == 0)
            assigned = object_table->AssignSlot(slot, kObjectRealize,
                                                registry, error);
        else if (slot == 3)
            assigned = object_table->AssignSlot(slot, kObjectGetInterface,
                                                registry, error);
        else if (slot == 6)
            assigned = object_table->AssignSlot(slot, kObjectDestroy,
                                                registry, error);
        else
            assigned = object_table->AssignNullSlot(slot, error);
        if (!assigned) return false;
    }
    if (!object_table->Seal(error)) return false;

    Object new_object;
    new_object.kind = kind;
    new_object.realized = realized;
    new_object.object_table = std::move(object_table);
    new_object.player = std::move(player);

    const auto add_interface =
        [&](InterfaceKind interface_kind, std::size_t slots,
            std::initializer_list<std::pair<std::size_t, std::string_view>>
                handlers) {
            auto table =
                std::make_unique<GuestFunctionTable>(heap_, slots, error);
            if (!table->valid()) return false;
            for (std::size_t slot = 0; slot < slots; ++slot) {
                std::string_view handler;
                for (const auto& entry : handlers) {
                    if (entry.first == slot) {
                        handler = entry.second;
                        break;
                    }
                }
                const bool assigned =
                    handler.empty()
                        ? table->AssignNullSlot(slot, error)
                        : table->AssignSlot(slot, handler, registry, error);
                if (!assigned) return false;
            }
            if (!table->Seal(error)) return false;
            new_object.interfaces.push_back(
                Interface{interface_kind, std::move(table)});
            return true;
        };

    if (kind == ObjectKind::kEngine &&
        !add_interface(InterfaceKind::kEngine, 15,
                       {{2, kEngineCreateAudioPlayer},
                        {7, kEngineCreateOutputMix}}))
        return false;
    if (kind == ObjectKind::kAudioPlayer) {
        if (!new_object.player ||
            !add_interface(InterfaceKind::kPlay, 12,
                           {{0, kPlaySetState},
                            {1, kPlayGetState},
                            {3, kPlayGetPosition}}) ||
            (new_object.player->queue_requested &&
             !add_interface(InterfaceKind::kBufferQueue, 4,
                            {{0, kBufferQueueEnqueue},
                             {1, kBufferQueueClear},
                             {2, kBufferQueueGetState},
                             {3, kBufferQueueRegisterCallback}})) ||
            (new_object.player->configuration_requested &&
             !add_interface(InterfaceKind::kConfiguration, 4,
                            {{0, kConfigurationSet},
                             {1, kConfigurationGet}})))
            return false;
    }

    const std::uint64_t object_handle =
        new_object.object_table->object_address();
    if (objects_.contains(object_handle)) {
        SetError(error, "native OpenSL object handle collision");
        return false;
    }
    for (const Interface& interface : new_object.interfaces) {
        if (interface_owners_.contains(interface.table->object_address())) {
            SetError(error, "native OpenSL interface handle collision");
            return false;
        }
    }
    if (new_object.player) {
        for (const Interface& interface : new_object.interfaces) {
            const std::uint64_t handle = interface.table->object_address();
            if (interface.kind == InterfaceKind::kPlay)
                new_object.player->play_handle = handle;
            else if (interface.kind == InterfaceKind::kBufferQueue)
                new_object.player->queue_handle = handle;
            else if (interface.kind == InterfaceKind::kConfiguration)
                new_object.player->configuration_handle = handle;
        }
    }
    const auto [inserted, ok] =
        objects_.emplace(object_handle, std::move(new_object));
    if (!ok) {
        SetError(error, "native OpenSL object insertion failed");
        return false;
    }
    for (const Interface& interface : inserted->second.interfaces)
        interface_owners_.emplace(interface.table->object_address(),
                                  object_handle);
    *address = object_handle;
    return true;
}

bool NativeOpenSl::WriteResultPointer(std::uint64_t output,
                                      std::uint64_t value,
                                      SlResult* result) const {
    if (result == nullptr) return false;
    if (output == 0 || !address_space_->Write(output, &value, sizeof(value))) {
        *result = kSlResultParameterInvalid;
        return false;
    }
    return true;
}

bool NativeOpenSl::SlCreateEngine(Aarch64ImportRegistry::Registers* r,
                                  std::string*) {
    SlResult result = kSlResultSuccess;
    if (r == nullptr || !WriteResultPointer((*r)[0], 0, &result) ||
        (*r)[1] != 0 || (*r)[2] != 0 || (*r)[3] != 0 || (*r)[4] != 0 ||
        (*r)[5] != 0) {
        if (r != nullptr)
            (*r)[0] = result == kSlResultSuccess
                          ? kSlResultParameterInvalid
                          : result;
        return true;
    }
    std::scoped_lock lock(mutex_);
    std::uint64_t object = 0;
    std::string ignored;
    if (shutdown_ || registry_ == nullptr ||
        !CreateObjectLocked(ObjectKind::kEngine, false, nullptr, *registry_,
                            &object, &ignored)) {
        (*r)[0] = kSlResultMemoryFailure;
        return true;
    }
    if (!address_space_->Write((*r)[0], &object, sizeof(object))) {
        const auto found = objects_.find(object);
        if (found != objects_.end()) {
            RemoveObjectInterfacesLocked(found->second);
            objects_.erase(found);
        }
        (*r)[0] = kSlResultParameterInvalid;
        return true;
    }
    (*r)[0] = kSlResultSuccess;
    return true;
}

bool NativeOpenSl::ObjectRealize(Aarch64ImportRegistry::Registers* r,
                                 std::string*) {
    std::scoped_lock lock(mutex_);
    const auto found =
        r == nullptr ? objects_.end() : objects_.find((*r)[0]);
    if (found == objects_.end() || shutdown_ || (*r)[1] > 1) {
        if (r != nullptr) (*r)[0] = kSlResultParameterInvalid;
        return true;
    }
    found->second.realized = true;
    (*r)[0] = kSlResultSuccess;
    return true;
}

bool NativeOpenSl::ObjectGetInterface(
    Aarch64ImportRegistry::Registers* r, std::string*) {
    SlResult result = kSlResultSuccess;
    if (r == nullptr || !WriteResultPointer((*r)[2], 0, &result)) {
        if (r != nullptr) (*r)[0] = result;
        return true;
    }
    std::scoped_lock lock(mutex_);
    const auto found = objects_.find((*r)[0]);
    if (found == objects_.end() || shutdown_) {
        (*r)[0] = kSlResultParameterInvalid;
        return true;
    }
    const std::uint64_t interface_handle =
        FindObjectInterfaceLocked(found->second, (*r)[1]);
    // Implicit player interfaces (Play, buffer queues, configuration) are
    // obtainable before Realize per the OpenSL ES specification; the engine
    // binds them between CreateAudioPlayer and Object::Realize.
    const bool implicit_before_realize =
        found->second.kind == ObjectKind::kAudioPlayer &&
        ((*r)[1] == play_iid_ || (*r)[1] == buffer_queue_iid_ ||
         (*r)[1] == android_simple_buffer_queue_iid_ ||
         (*r)[1] == configuration_iid_);
    {
        static std::size_t getiface_logs = 0;
        if (getiface_logs < 20) {
            ++getiface_logs;
            std::uint64_t handle_target = 0;
            bool handle_read = false;
            if (interface_handle != 0 && heap_ != nullptr)
                handle_read = heap_->Read(interface_handle, &handle_target,
                                          sizeof(handle_target));
            std::fprintf(stderr,
                         "ISOMR_OPENSL_GETIFACE obj=%llx iid=%llx out=%llx "
                         "handle=%llx *handle=%llx handle_rd=%d realized=%d "
                         "implicit=%d kind=%d queue_req=%d cfg_req=%d\n",
                         static_cast<unsigned long long>((*r)[0]),
                         static_cast<unsigned long long>((*r)[1]),
                         static_cast<unsigned long long>((*r)[2]),
                         static_cast<unsigned long long>(interface_handle),
                         static_cast<unsigned long long>(handle_target),
                         handle_read ? 1 : 0,
                         found->second.realized ? 1 : 0,
                         implicit_before_realize ? 1 : 0,
                         static_cast<int>(found->second.kind),
                         (found->second.player &&
                          found->second.player->queue_requested)
                             ? 1 : 0,
                         (found->second.player &&
                          found->second.player->configuration_requested)
                             ? 1 : 0);
            std::fflush(stderr);
        }
    }
    if (!found->second.realized && !implicit_before_realize) {
        (*r)[0] = kSlResultPreconditionsViolated;
    } else if (interface_handle == 0) {
        (*r)[0] = kSlResultFeatureUnsupported;
    } else if (!address_space_->Write((*r)[2], &interface_handle,
                                      sizeof(interface_handle))) {
        (*r)[0] = kSlResultParameterInvalid;
    } else {
        (*r)[0] = kSlResultSuccess;
    }
    return true;
}

void NativeOpenSl::StopAndJoinPlayer(
    const std::shared_ptr<Player>& player) noexcept {
    if (!player) return;
    {
        const std::lock_guard lock(player->mutex);
        player->stop = true;
        player->queue.clear();
    }
    player->changed.notify_all();
    if (player->worker.joinable()) player->worker.join();
}

bool NativeOpenSl::ObjectDestroy(Aarch64ImportRegistry::Registers* r,
                                 std::string* error) {
    if (r == nullptr) {
        SetError(error, "native OpenSL Destroy registers are null");
        return false;
    }
    std::fprintf(stderr, "ISOMR_OPENSL_DESTROY obj=%llx\n",
                 static_cast<unsigned long long>((*r)[0]));
    std::fflush(stderr);
    std::shared_ptr<Player> player;
    {
        std::scoped_lock lock(mutex_);
        const auto found = objects_.find((*r)[0]);
        if (shutdown_ || found == objects_.end()) {
            SetError(error, "native OpenSL Destroy object is invalid");
            return false;
        }
        player = found->second.player;
    }
    StopAndJoinPlayer(player);
    {
        std::scoped_lock lock(mutex_);
        const auto found = objects_.find((*r)[0]);
        if (found == objects_.end()) {
            SetError(error, "native OpenSL Destroy object disappeared");
            return false;
        }
        RetireObjectLocked(found->second);
        objects_.erase(found);
    }
    // SLObjectItf::Destroy has a void ABI; leave x0 unchanged.
    return true;
}

bool NativeOpenSl::EngineCreateOutputMix(
    Aarch64ImportRegistry::Registers* r, std::string*) {
    SlResult result = kSlResultSuccess;
    if (r == nullptr || !WriteResultPointer((*r)[1], 0, &result)) {
        if (r != nullptr) (*r)[0] = result;
        return true;
    }
    std::scoped_lock lock(mutex_);
    Object* engine = FindInterfaceOwnerLocked((*r)[0]);
    if (engine == nullptr || engine->kind != ObjectKind::kEngine || shutdown_) {
        (*r)[0] = kSlResultParameterInvalid;
        return true;
    }
    if (!engine->realized) {
        (*r)[0] = kSlResultPreconditionsViolated;
        return true;
    }
    if ((*r)[2] != 0 || (*r)[3] != 0 || (*r)[4] != 0) {
        (*r)[0] = kSlResultFeatureUnsupported;
        return true;
    }
    std::uint64_t output_mix = 0;
    std::string ignored;
    if (!CreateObjectLocked(ObjectKind::kOutputMix, false, nullptr,
                            *registry_, &output_mix, &ignored)) {
        (*r)[0] = kSlResultMemoryFailure;
        return true;
    }
    if (!address_space_->Write((*r)[1], &output_mix, sizeof(output_mix))) {
        objects_.erase(output_mix);
        (*r)[0] = kSlResultParameterInvalid;
        return true;
    }
    (*r)[0] = kSlResultSuccess;
    return true;
}

bool NativeOpenSl::EngineCreateAudioPlayer(
    Aarch64ImportRegistry::Registers* r, std::string*) {
    SlResult result = kSlResultSuccess;
    if (r == nullptr || !WriteResultPointer((*r)[1], 0, &result)) {
        if (r != nullptr) (*r)[0] = result;
        return true;
    }
    {
        std::scoped_lock lock(mutex_);
        Object* engine = FindInterfaceOwnerLocked((*r)[0]);
        if (engine == nullptr || engine->kind != ObjectKind::kEngine ||
            shutdown_) {
            (*r)[0] = kSlResultParameterInvalid;
        std::fprintf(stderr, "ISOMR_OPENSL_CREATE reject=1\n"); return true;
        }
        if (!engine->realized) {
            (*r)[0] = kSlResultPreconditionsViolated;
        std::fprintf(stderr, "ISOMR_OPENSL_CREATE reject=2\n"); return true;
        }
    }

    PlayerConfiguration config;
    SlDataPointerPair source;
    SlDataPointerPair sink;
    std::array<std::uint32_t, 8> format{};
    std::array<std::uint32_t, 2> queue_locator{};
    SlOutputMixLocator output_locator;
    if ((*r)[2] == 0 || (*r)[3] == 0 ||
        !address_space_->Read((*r)[2], &source, sizeof(source)) ||
        !address_space_->Read((*r)[3], &sink, sizeof(sink)) ||
        source.locator == 0 || source.format == 0 || sink.locator == 0 ||
        !address_space_->Read(source.locator, queue_locator.data(),
                              sizeof(queue_locator)) ||
        !address_space_->Read(source.format, format.data(),
                              7 * sizeof(std::uint32_t)) ||
        !address_space_->Read(sink.locator, &output_locator,
                              sizeof(output_locator))) {
        (*r)[0] = kSlResultParameterInvalid;
        std::fprintf(stderr, "ISOMR_OPENSL_CREATE reject=3\n"); return true;
    }
    // PvZ passes a compact PCM descriptor whose first word is the channel
    // count (no SL formatType); the standard SLDataFormat_PCM layout is
    // still accepted when it is recognized.
    // The two layouts agree here: prefer format[1] as the channel count
    // when it is plausible (SL layout), otherwise fall back to format[0]
    // (compact layout whose first word is the channel count).
    const std::uint64_t channels =
        (format[1] >= 1 && format[1] <= 30) ? format[1] : format[0];
    if (format[2] < 8000000U || format[2] > 384000000U ||
        format[2] % 1000U != 0) {
        // The engine's configured output rate can be uninitialized in the
        // compat runtime; fall back to 44.1 kHz instead of rejecting the
        // player outright.
        format[2] = 44100000U;
    }
    if (queue_locator[0] != kDataLocatorAndroidSimpleBufferQueue ||
        queue_locator[1] == 0 || queue_locator[1] > kMaximumQueueBuffers ||
        channels == 0 || channels > 30 ||
        format[3] == 0 || format[3] > format[4] ||
        (format[4] != 8 && format[4] != 16 && format[4] != 24 &&
         format[4] != 32) ||
        (format[6] != 1 && format[6] != 2) ||
        output_locator.locator_type != kDataLocatorOutputMix ||
        output_locator.output_mix == 0 || sink.format != 0) {
        (*r)[0] = kSlResultParameterInvalid;
        std::fprintf(stderr, "ISOMR_OPENSL_CREATE reject=4\n"); return true;
    }
    if (format[0] == kAndroidDataFormatPcmEx &&
        (!address_space_->Read(source.format, format.data(), sizeof(format)) ||
         format[7] < 1 || format[7] > 3)) {
        (*r)[0] = kSlResultParameterInvalid;
        std::fprintf(stderr, "ISOMR_OPENSL_CREATE reject=5\n"); return true;
    }
    const std::uint64_t bytes_per_frame =
        channels * (format[4] / 8U);
    if (bytes_per_frame == 0 ||
        bytes_per_frame > std::numeric_limits<std::uint32_t>::max()) {
        (*r)[0] = kSlResultParameterInvalid;
        std::fprintf(stderr, "ISOMR_OPENSL_CREATE reject=6\n"); return true;
    }
    config.output_mix = output_locator.output_mix;
    config.queue_capacity = queue_locator[1];
    config.sample_rate_hz = format[2] / 1000U;
    config.bytes_per_frame = static_cast<std::uint32_t>(bytes_per_frame);

    const std::uint64_t interface_count = (*r)[4];
    if (interface_count > kMaximumInterfaces ||
        (interface_count != 0 && ((*r)[5] == 0 || (*r)[6] == 0))) {
        (*r)[0] = kSlResultParameterInvalid;
        std::fprintf(stderr, "ISOMR_OPENSL_CREATE reject=7\n"); return true;
    }
    for (std::uint64_t index = 0; index < interface_count; ++index) {
        std::uint64_t iid = 0;
        std::uint32_t required = 0;
        if (!address_space_->Read((*r)[5] + index * sizeof(iid), &iid,
                                  sizeof(iid)) ||
            !address_space_->Read((*r)[6] + index * sizeof(required),
                                  &required, sizeof(required)) ||
            required > 1) {
            (*r)[0] = kSlResultParameterInvalid;
        std::fprintf(stderr, "ISOMR_OPENSL_CREATE reject=8\n"); return true;
        }
        {
            static std::size_t iid_logs = 0;
            if (iid_logs < 24) {
                ++iid_logs;
                std::fprintf(stderr,
                             "ISOMR_OPENSL_IID index=%llu iid=%llx req=%u "
                             "bq=%llx asbq=%llx\n",
                             static_cast<unsigned long long>(index),
                             static_cast<unsigned long long>(iid),
                             required,
                             static_cast<unsigned long long>(buffer_queue_iid_),
                             static_cast<unsigned long long>(
                                 android_simple_buffer_queue_iid_));
                std::fflush(stderr);
            }
        }
        if (iid == buffer_queue_iid_ ||
            iid == android_simple_buffer_queue_iid_) {
            config.queue_requested = true;
        } else if (iid == configuration_iid_) {
            config.configuration_requested = true;
        } else if (iid == play_iid_ || iid == engine_iid_) {
            // Satisfied implicitly by the player/engine object tables.
        } else if (required != 0) {
            (*r)[0] = kSlResultFeatureUnsupported;
        std::fprintf(stderr, "ISOMR_OPENSL_CREATE reject=9\n"); return true;
        }
    }
    std::fprintf(stderr,
                 "ISOMR_OPENSL_PLAYER icount=%llu queue_req=%d cfg_req=%d "
                 "qloc0=%x qcap=%llu ch=%llu rate=%u bpf=%llu\n",
                 static_cast<unsigned long long>(interface_count),
                 config.queue_requested ? 1 : 0,
                 config.configuration_requested ? 1 : 0,
                 queue_locator[0],
                 static_cast<unsigned long long>(config.queue_capacity),
                 static_cast<unsigned long long>(channels),
                 static_cast<unsigned>(config.sample_rate_hz),
                 static_cast<unsigned long long>(config.bytes_per_frame));
    std::fflush(stderr);

    std::shared_ptr<Player> player;
    try {
        player = std::make_shared<Player>();
    } catch (...) {
        (*r)[0] = kSlResultMemoryFailure;
        std::fprintf(stderr, "ISOMR_OPENSL_CREATE reject=10\n"); return true;
    }
    player->queue_capacity = config.queue_capacity;
    player->sample_rate_hz = config.sample_rate_hz;
    player->bytes_per_frame = config.bytes_per_frame;
    player->queue_requested = config.queue_requested;
    player->configuration_requested = config.configuration_requested;
    std::uint64_t player_object = 0;
    {
        std::scoped_lock lock(mutex_);
        Object* engine = FindInterfaceOwnerLocked((*r)[0]);
        const auto mix = objects_.find(config.output_mix);
        std::string ignored;
        if (engine == nullptr || engine->kind != ObjectKind::kEngine ||
            !engine->realized || shutdown_) {
            (*r)[0] = kSlResultParameterInvalid;
        std::fprintf(stderr, "ISOMR_OPENSL_CREATE reject=11\n"); return true;
        }
        if (mix == objects_.end() ||
            mix->second.kind != ObjectKind::kOutputMix ||
            !mix->second.realized) {
            (*r)[0] = kSlResultPreconditionsViolated;
        std::fprintf(stderr, "ISOMR_OPENSL_CREATE reject=12\n"); return true;
        }
        if (!CreateObjectLocked(ObjectKind::kAudioPlayer, false, player,
                                *registry_, &player_object, &ignored)) {
            (*r)[0] = kSlResultMemoryFailure;
        std::fprintf(stderr, "ISOMR_OPENSL_CREATE reject=13\n"); return true;
        }
    }
    try {
        player->worker =
            std::thread(&NativeOpenSl::PlayerWorkerMain, this, player);
    } catch (...) {
        std::scoped_lock lock(mutex_);
        const auto found = objects_.find(player_object);
        if (found != objects_.end()) {
            RemoveObjectInterfacesLocked(found->second);
            objects_.erase(found);
        }
        (*r)[0] = kSlResultMemoryFailure;
        std::fprintf(stderr, "ISOMR_OPENSL_CREATE reject=14\n"); return true;
    }
    if (!address_space_->Write((*r)[1], &player_object,
                               sizeof(player_object))) {
        StopAndJoinPlayer(player);
        std::scoped_lock lock(mutex_);
        const auto found = objects_.find(player_object);
        if (found != objects_.end()) {
            RemoveObjectInterfacesLocked(found->second);
            objects_.erase(found);
        }
        (*r)[0] = kSlResultParameterInvalid;
        std::fprintf(stderr, "ISOMR_OPENSL_CREATE reject=15\n"); return true;
    }
    (*r)[0] = kSlResultSuccess;
        std::fprintf(stderr, "ISOMR_OPENSL_CREATE reject=16\n"); return true;
}

std::shared_ptr<NativeOpenSl::Player> NativeOpenSl::FindPlayerForInterface(
    std::uint64_t handle, InterfaceKind kind, bool require_realized,
    SlResult* result, bool* realized) {
    if (result == nullptr) return {};
    std::scoped_lock lock(mutex_);
    Object* object = FindInterfaceOwnerLocked(handle);
    if (object == nullptr || object->kind != ObjectKind::kAudioPlayer ||
        !object->player || shutdown_) {
        *result = kSlResultParameterInvalid;
        return {};
    }
    bool kind_matches = false;
    for (const Interface& interface : object->interfaces) {
        if (interface.kind == kind && interface.table &&
            interface.table->object_address() == handle) {
            kind_matches = true;
            break;
        }
    }
    if (!kind_matches) {
        *result = kSlResultParameterInvalid;
        return {};
    }
    if (realized != nullptr) *realized = object->realized;
    if (require_realized && !object->realized) {
        *result = kSlResultPreconditionsViolated;
        return {};
    }
    *result = kSlResultSuccess;
    return object->player;
}

bool NativeOpenSl::BufferQueueEnqueue(
    Aarch64ImportRegistry::Registers* r, std::string*) {
    if (r == nullptr) return false;
    SlResult result = kSlResultSuccess;
    const std::shared_ptr<Player> player = FindPlayerForInterface(
        (*r)[0], InterfaceKind::kBufferQueue, true, &result);
    if ((*r)[2] > std::numeric_limits<std::uint32_t>::max()) {
        (*r)[0] = player ? kSlResultParameterInvalid : result;
        return true;
    }
    const std::uint32_t size = static_cast<std::uint32_t>((*r)[2]);
    std::uint8_t probe = 0;
    if (!player || (*r)[1] == 0 || size == 0 ||
        (*r)[1] > std::numeric_limits<std::uint64_t>::max() - (size - 1) ||
        !address_space_->Read((*r)[1], &probe, sizeof(probe)) ||
        !address_space_->Read((*r)[1] + size - 1, &probe, sizeof(probe)) ||
        size % player->bytes_per_frame != 0) {
        (*r)[0] = player ? kSlResultParameterInvalid : result;
        return true;
    }
    const std::uint64_t frames = size / player->bytes_per_frame;
    const std::uint64_t duration_ns =
        frames * 1000000000ULL / player->sample_rate_hz;
    {
        const std::lock_guard lock(player->mutex);
        if (player->stop || !player->worker_error.empty()) {
            (*r)[0] = kSlResultInternalError;
            return true;
        }
        if (player->queue.size() >= player->queue_capacity) {
            (*r)[0] = kSlResultBufferInsufficient;
            return true;
        }
        player->queue.push_back(Player::Buffer{
            player->next_sequence++, frames,
            std::chrono::nanoseconds(duration_ns == 0 ? 1 : duration_ns)});
    }
    player->changed.notify_all();
    (*r)[0] = kSlResultSuccess;
    return true;
}

bool NativeOpenSl::BufferQueueClear(
    Aarch64ImportRegistry::Registers* r, std::string*) {
    if (r == nullptr) return false;
    SlResult result = kSlResultSuccess;
    const std::shared_ptr<Player> player = FindPlayerForInterface(
        (*r)[0], InterfaceKind::kBufferQueue, true, &result);
    if (!player) {
        (*r)[0] = result;
        return true;
    }
    {
        const std::lock_guard lock(player->mutex);
        player->queue.clear();
        ++player->next_sequence;
    }
    player->changed.notify_all();
    (*r)[0] = kSlResultSuccess;
    return true;
}

bool NativeOpenSl::BufferQueueGetState(
    Aarch64ImportRegistry::Registers* r, std::string*) {
    if (r == nullptr) return false;
    SlResult result = kSlResultSuccess;
    const std::shared_ptr<Player> player = FindPlayerForInterface(
        (*r)[0], InterfaceKind::kBufferQueue, true, &result);
    if (!player || (*r)[1] == 0) {
        (*r)[0] = player ? kSlResultParameterInvalid : result;
        return true;
    }
    std::array<std::uint32_t, 2> state{};
    {
        const std::lock_guard lock(player->mutex);
        state[0] = static_cast<std::uint32_t>(player->queue.size());
        state[1] = player->queue_index;
    }
    (*r)[0] = address_space_->Write((*r)[1], state.data(), sizeof(state))
                  ? kSlResultSuccess
                  : kSlResultParameterInvalid;
    return true;
}

bool NativeOpenSl::BufferQueueRegisterCallback(
    Aarch64ImportRegistry::Registers* r, std::string*) {
    if (r == nullptr) return false;
    SlResult result = kSlResultSuccess;
    const std::shared_ptr<Player> player = FindPlayerForInterface(
        (*r)[0], InterfaceKind::kBufferQueue, true, &result);
    if (!player) {
        (*r)[0] = result;
        return true;
    }
    {
        const std::lock_guard lock(player->mutex);
        player->callback = (*r)[1];
        player->callback_context = (*r)[2];
    }
    player->changed.notify_all();
    (*r)[0] = kSlResultSuccess;
    return true;
}

bool NativeOpenSl::PlaySetState(Aarch64ImportRegistry::Registers* r,
                                std::string*) {
    if (r == nullptr) return false;
    SlResult result = kSlResultSuccess;
    const std::shared_ptr<Player> player = FindPlayerForInterface(
        (*r)[0], InterfaceKind::kPlay, true, &result);
    const std::uint32_t state = static_cast<std::uint32_t>((*r)[1]);
    if (!player || state < kPlayStateStopped || state > kPlayStatePlaying) {
        (*r)[0] = player ? kSlResultParameterInvalid : result;
        return true;
    }
    {
        const std::lock_guard lock(player->mutex);
        if (player->stop || !player->worker_error.empty()) {
            (*r)[0] = kSlResultInternalError;
            return true;
        }
        player->play_state = state;
        if (state == kPlayStateStopped) player->completed_frames = 0;
    }
    player->changed.notify_all();
    (*r)[0] = kSlResultSuccess;
    return true;
}

bool NativeOpenSl::PlayGetState(Aarch64ImportRegistry::Registers* r,
                                std::string*) {
    if (r == nullptr) return false;
    SlResult result = kSlResultSuccess;
    const std::shared_ptr<Player> player = FindPlayerForInterface(
        (*r)[0], InterfaceKind::kPlay, true, &result);
    std::uint32_t state = 0;
    if (!player || (*r)[1] == 0) {
        (*r)[0] = player ? kSlResultParameterInvalid : result;
        return true;
    }
    {
        const std::lock_guard lock(player->mutex);
        state = player->play_state;
    }
    (*r)[0] = address_space_->Write((*r)[1], &state, sizeof(state))
                  ? kSlResultSuccess
                  : kSlResultParameterInvalid;
    return true;
}

bool NativeOpenSl::PlayGetPosition(Aarch64ImportRegistry::Registers* r,
                                   std::string*) {
    if (r == nullptr) return false;
    SlResult result = kSlResultSuccess;
    const std::shared_ptr<Player> player = FindPlayerForInterface(
        (*r)[0], InterfaceKind::kPlay, true, &result);
    std::uint32_t position = 0;
    if (!player || (*r)[1] == 0) {
        (*r)[0] = player ? kSlResultParameterInvalid : result;
        return true;
    }
    {
        const std::lock_guard lock(player->mutex);
        position = static_cast<std::uint32_t>(
            player->completed_frames * 1000ULL / player->sample_rate_hz);
    }
    (*r)[0] = address_space_->Write((*r)[1], &position, sizeof(position))
                  ? kSlResultSuccess
                  : kSlResultParameterInvalid;
    return true;
}

bool NativeOpenSl::ReadGuestKey(std::uint64_t address,
                                std::string* key) const {
    if (address == 0 || key == nullptr) return false;
    std::uint64_t length = 0;
    std::string ignored;
    if (!address_space_->BoundedStringLength(address, 64, &length, &ignored))
        return false;
    key->resize(static_cast<std::size_t>(length));
    return length == 0 ||
           address_space_->Read(address, key->data(), key->size());
}

bool NativeOpenSl::ConfigurationSet(
    Aarch64ImportRegistry::Registers* r, std::string*) {
    if (r == nullptr) return false;
    SlResult result = kSlResultSuccess;
    bool realized = false;
    const std::shared_ptr<Player> player = FindPlayerForInterface(
        (*r)[0], InterfaceKind::kConfiguration, false, &result, &realized);
    std::string key;
    std::uint32_t value = 0;
    if (!player || realized || (*r)[2] == 0 || (*r)[3] != sizeof(value) ||
        !ReadGuestKey((*r)[1], &key) ||
        !address_space_->Read((*r)[2], &value, sizeof(value))) {
        (*r)[0] = !player ? result
                          : realized ? kSlResultPreconditionsViolated
                                     : kSlResultParameterInvalid;
        return true;
    }
    {
        const std::lock_guard lock(player->mutex);
        if (key == kPerformanceModeKey && value <= 3)
            player->performance_mode = value;
        else if (key == kStreamTypeKey && value <= 5)
            player->stream_type = value;
        else {
            (*r)[0] = key == kPerformanceModeKey || key == kStreamTypeKey
                          ? kSlResultParameterInvalid
                          : kSlResultFeatureUnsupported;
            return true;
        }
    }
    (*r)[0] = kSlResultSuccess;
    return true;
}

bool NativeOpenSl::ConfigurationGet(
    Aarch64ImportRegistry::Registers* r, std::string*) {
    if (r == nullptr) return false;
    SlResult result = kSlResultSuccess;
    const std::shared_ptr<Player> player = FindPlayerForInterface(
        (*r)[0], InterfaceKind::kConfiguration, false, &result);
    std::string key;
    std::uint32_t available = 0;
    if (!player || (*r)[2] == 0 || !ReadGuestKey((*r)[1], &key) ||
        !address_space_->Read((*r)[2], &available, sizeof(available))) {
        (*r)[0] = player ? kSlResultParameterInvalid : result;
        return true;
    }
    std::uint32_t value = 0;
    {
        const std::lock_guard lock(player->mutex);
        if (key == kPerformanceModeKey)
            value = player->performance_mode;
        else if (key == kStreamTypeKey)
            value = player->stream_type;
        else {
            (*r)[0] = kSlResultFeatureUnsupported;
            return true;
        }
    }
    const std::uint32_t required = sizeof(value);
    if (!address_space_->Write((*r)[2], &required, sizeof(required))) {
        (*r)[0] = kSlResultParameterInvalid;
    } else if (available < required) {
        (*r)[0] = kSlResultBufferInsufficient;
    } else if ((*r)[3] == 0 ||
               !address_space_->Write((*r)[3], &value, sizeof(value))) {
        (*r)[0] = kSlResultParameterInvalid;
    } else {
        (*r)[0] = kSlResultSuccess;
    }
    return true;
}

void NativeOpenSl::PlayerWorkerMain(
    const std::shared_ptr<Player>& player) noexcept {
    std::string worker_error;
    const bool ok = threads_ != nullptr && threads_->RunManagedGuestLoop(
        [player](const BionicThreads::GuestCall& executor,
                 std::string* error) {
            for (;;) {
                std::uint64_t callback = 0;
                std::uint64_t callback_context = 0;
                std::uint64_t queue_handle = 0;
                Player::Buffer buffer;
                {
                    std::unique_lock lock(player->mutex);
                    player->changed.wait(lock, [&] {
                        return player->stop ||
                               (player->play_state == kPlayStatePlaying &&
                                !player->queue.empty());
                    });
                    if (player->stop) return true;
                    buffer = player->queue.front();
                    const bool interrupted = player->changed.wait_for(
                        lock, buffer.duration, [&] {
                            return player->stop ||
                                   player->play_state != kPlayStatePlaying ||
                                   player->queue.empty() ||
                                   player->queue.front().sequence !=
                                       buffer.sequence;
                        });
                    if (player->stop) return true;
                    if (interrupted) continue;
                    player->queue.pop_front();
                    ++player->queue_index;
                    player->completed_frames += buffer.frames;
                    callback = player->callback;
                    callback_context = player->callback_context;
                    queue_handle = player->queue_handle;
                }
                if (callback != 0) {
                    std::array<std::uint64_t, 8> arguments{};
                    arguments[0] = queue_handle;
                    arguments[1] = callback_context;
                    std::uint64_t ignored_result = 0;
                    if (!executor(callback, arguments, &ignored_result,
                                  error))
                        return false;
                }
            }
        },
        &worker_error);
    if (!ok) {
        {
            const std::lock_guard lock(player->mutex);
            player->worker_error = worker_error.empty()
                                       ? "managed guest audio callback failed"
                                       : std::move(worker_error);
            player->stop = true;
        }
        player->changed.notify_all();
    }
}

std::size_t NativeOpenSl::object_count() const {
    std::scoped_lock lock(mutex_);
    return objects_.size();
}

bool NativeOpenSl::ReleaseIids(std::string* error) {
    while (!iid_allocations_.empty()) {
        const std::uint64_t allocation = iid_allocations_.back();
        if (!heap_->ReleasePinned(allocation, error)) return false;
        iid_allocations_.pop_back();
    }
    engine_iid_ = 0;
    play_iid_ = 0;
    buffer_queue_iid_ = 0;
    android_simple_buffer_queue_iid_ = 0;
    configuration_iid_ = 0;
    return true;
}

bool NativeOpenSl::Shutdown(std::string* error) {
    std::fprintf(stderr, "ISOMR_OPENSL_SHUTDOWN objects=%zu\n",
                 objects_.size());
    std::fflush(stderr);
    std::vector<std::shared_ptr<Player>> players;
    {
        std::scoped_lock lock(mutex_);
        if (shutdown_) {
            SetError(error, "native OpenSL timed-discard is already shut down");
            return false;
        }
        for (const auto& entry : objects_) {
            if (entry.second.player) players.push_back(entry.second.player);
        }
    }
    for (const std::shared_ptr<Player>& player : players)
        StopAndJoinPlayer(player);
    std::scoped_lock lock(mutex_);
    interface_owners_.clear();
    objects_.clear();
    // Retired object/interface tables were kept alive so stale guest
    // SLObjectItf pointers stay valid mid-session; the process boundary is
    // where they are finally released back to the heap.
    retired_tables_.clear();
    if (!ReleaseIids(error)) return false;
    registry_ = nullptr;
    shutdown_ = true;
    return true;
}

}  // namespace isomr
