#pragma once

#include "guest_jni_backend.h"
#include "import_registry.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace isomr {

class Aarch64ProfileRuntime;
class Aarch64ImportRegistry;
class BionicFileSystem;
class BionicThreadState;
class GuestAddressSpace;
class GuestFunctionTable;
class GuestHeap;

struct GuestJniCompatibilityConfig {
    std::string package_name;
    std::string package_code_path;
    std::string package_version;
    std::string splash_removal_class;
};

class GuestJni;

// Locked-dispatch context handed to a per-game scripted JNI override. It is
// created inside GuestJni::ApplyCompatOverride while mutex_ is held, so all
// helpers operate on the already-locked tables — never the public locking
// entry points (which would deadlock on the non-recursive mutex).
struct ScriptedJniContext {
    const char* kind = nullptr;
    std::string_view class_type;
    std::string_view name;
    std::string_view signature;
    std::uint64_t receiver = 0;     // registers[1]: jobject or jclass
    std::uint64_t method_ref = 0;   // registers[2]: jmethodID
    std::uint64_t args_source = 0;  // registers[3]: va_list guest address
    void* registers_v = nullptr;    // Aarch64ImportRegistry::Registers*
    std::string* call_error = nullptr;
    GuestJni* jni = nullptr;

    // Reads argument `index` out of the call's va_list (registers[3]).
    bool Arg(std::size_t index, std::uint64_t* value,
             std::string* error) const;
    void SetResult(std::uint64_t value);
    void SetFloatResult(float value);
    void SetDoubleResult(double value);
    // Locked-context reference factories and string accessors.
    bool CreateString(std::string_view value, std::uint64_t* reference,
                      std::string* error);
    bool CreateObject(std::string_view type, std::uint64_t* reference,
                      std::string* error);
    bool ReadString(std::uint64_t reference, std::string* value,
                    std::string* error) const;
    bool WriteObjectString(std::uint64_t reference, std::string_view value,
                           std::string* error);
    std::optional<std::string> ObjectType(std::uint64_t reference) const;
};

// Diagnostic global-JNI lock: a std::mutex-compatible wrapper that records
// the holder and, when a waiter blocks >5s, prints the holder id, hold time
// and contentions — pinning reentrant/long-hold deadlocks without changing
// any lock/unlock site.
class TrackedJniMutex {
  public:
    void lock() {
        const std::uint64_t tid = ThreadId();
        if (!back_.try_lock()) {
            contentions_.fetch_add(1, std::memory_order_relaxed);
            const auto start = std::chrono::steady_clock::now();
            std::uint64_t next_report_ms = 5000;
            for (;;) {
                if (back_.try_lock()) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                const auto waited =
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - start)
                        .count();
                if (waited >= static_cast<long long>(next_report_ms)) {
                    next_report_ms += 2000;
                    const auto held =
                        std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() -
                            std::chrono::steady_clock::time_point(
                                std::chrono::milliseconds(
                                    held_epoch_ms_.load(
                                        std::memory_order_relaxed))))
                            .count();
                    std::fprintf(stderr,
                                 "ISOMR_JNI_LOCK_WAIT self=%llx holder=%llx "
                                 "held_ms=%lld wait_ms=%lld contentions=%llu\n",
                                 static_cast<unsigned long long>(tid),
                                 static_cast<unsigned long long>(
                                     owner_.load(std::memory_order_relaxed)),
                                 static_cast<long long>(held),
                                 static_cast<long long>(waited),
                                 static_cast<unsigned long long>(
                                     contentions_.load()));
                    std::fflush(stderr);
                }
            }
        }
        owner_.store(tid, std::memory_order_relaxed);
        held_epoch_ms_.store(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch())
                .count(),
            std::memory_order_relaxed);
    }
    bool try_lock() {
        if (!back_.try_lock()) return false;
        owner_.store(ThreadId(), std::memory_order_relaxed);
        held_epoch_ms_.store(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch())
                .count(),
            std::memory_order_relaxed);
        return true;
    }
    void unlock() {
        owner_.store(0, std::memory_order_relaxed);
        back_.unlock();
    }

  private:
    static std::uint64_t ThreadId() {
        return std::hash<std::thread::id>{}(std::this_thread::get_id());
    }
    std::mutex back_;
    std::atomic<std::uint64_t> owner_{0};
    std::atomic<std::uint64_t> held_epoch_ms_{0};
    std::atomic<std::uint64_t> contentions_{0};
};

class GuestJni {
public:
    struct RegisteredNativeHandle {
        std::uint64_t id = 0;
        std::uint64_t generation = 0;

        bool operator==(const RegisteredNativeHandle&) const = default;
    };
    struct RegisteredNativeDescriptor {
        std::string class_type;
        std::string name;
        std::string signature;
    };

    enum class HttpLifecycleEventKind : std::uint8_t {
        kCreateTask,
        kExecuteTask,
        kCancelTask,
    };
    struct HttpLifecycleEvent {
        HttpLifecycleEventKind kind{};
        std::uint64_t request_id = 0;
        std::uint64_t sequence = 0;
        std::uint64_t host_thread_hash = 0;
    };
    enum class HttpObservationEventKind : std::uint8_t {
        kMethodLookup,
        kConstructor,
        kObjectCall,
        kVoidCall,
    };
    struct HttpObservationEvent {
        HttpObservationEventKind kind =
            HttpObservationEventKind::kMethodLookup;
        std::uint64_t sequence = 0;
        std::uint64_t host_thread_hash = 0;
        bool owner_thread = false;
        std::string class_name;
        std::string method_name;
        std::string signature;
        std::array<std::uint64_t, 4> arguments{};
    };
    struct AnalyticsValueRequest {
        std::uint64_t sequence = 0;
        std::string key;
    };
    struct AgeSignalsRequest {
        std::uint64_t sequence = 0;
        std::uint64_t first_callback_handle = 0;
        std::uint64_t debug_scenario = 0;
        std::uint64_t second_callback_handle = 0;
    };
    struct PopupDialogRequest {
        std::uint64_t sequence = 0;
        std::string message;
        std::string title;
        std::string button;
        bool noncancelable = false;
        std::uint64_t callback_handle = 0;
    };

    struct BrazeConfiguration {
        std::array<bool, 5> string_present{};
        std::int32_t session_timeout = 0;
        bool location_enabled = false;
        std::int32_t trigger_interval = 0;
        bool logging_enabled = false;
        std::int32_t log_level = 0;
    };
    struct LocalNotificationSchedule {
        std::int32_t first_integer = 0;
        std::int64_t long_value = 0;
        std::array<bool, 7> string_present{};
        std::int32_t final_integer = 0;
    };

    static constexpr std::size_t kEnvironmentSlotCount = 233;
    void MirrorPendingExceptionLocked();
    static constexpr std::size_t kVmSlotCount = 8;
    static constexpr std::size_t kHttpLifecycleEventCapacity = 64;
    static constexpr std::size_t kHttpObservationEventCapacity = 64;
    static constexpr std::size_t kAnalyticsValueRequestCapacity = 64;
    static constexpr std::size_t kAgeSignalsRequestCapacity = 64;
    static constexpr std::size_t kPopupDialogRequestCapacity = 16;
    static constexpr std::size_t kGetVersionSlot = 4;
    static constexpr std::size_t kFindClassSlot = 6;
    static constexpr std::size_t kFromReflectedMethodSlot = 7;
    static constexpr std::size_t kToReflectedMethodSlot = 9;
    static constexpr std::size_t kGetStaticMethodIdSlot = 113;
    static constexpr std::size_t kGetStaticFieldIdSlot = 144;
    static constexpr std::size_t kGetStaticObjectFieldSlot = 145;
    static constexpr std::size_t kGetStaticIntFieldSlot = 150;
    static constexpr std::size_t kCallStaticObjectMethodSlot = 114;
    static constexpr std::size_t kCallStaticObjectMethodVSlot = 115;
    static constexpr std::size_t kCallStaticObjectMethodASlot = 116;
    static constexpr std::size_t kCallStaticBooleanMethodSlot = 117;
    static constexpr std::size_t kCallStaticBooleanMethodVSlot = 118;
    static constexpr std::size_t kCallStaticBooleanMethodASlot = 119;
    static constexpr std::size_t kCallStaticByteMethodSlot = 120;
    static constexpr std::size_t kCallStaticByteMethodVSlot = 121;
    static constexpr std::size_t kCallStaticByteMethodASlot = 122;
    static constexpr std::size_t kCallStaticCharMethodSlot = 123;
    static constexpr std::size_t kCallStaticCharMethodVSlot = 124;
    static constexpr std::size_t kCallStaticCharMethodASlot = 125;
    static constexpr std::size_t kCallStaticShortMethodSlot = 126;
    static constexpr std::size_t kCallStaticShortMethodVSlot = 127;
    static constexpr std::size_t kCallStaticShortMethodASlot = 128;
    static constexpr std::size_t kCallStaticIntMethodSlot = 129;
    static constexpr std::size_t kCallStaticIntMethodVSlot = 130;
    static constexpr std::size_t kCallStaticIntMethodASlot = 131;
    static constexpr std::size_t kCallStaticLongMethodSlot = 132;
    static constexpr std::size_t kCallStaticLongMethodVSlot = 133;
    static constexpr std::size_t kCallStaticLongMethodASlot = 134;
    static constexpr std::size_t kCallStaticVoidMethodSlot = 141;
    static constexpr std::size_t kCallStaticVoidMethodVSlot = 142;
    static constexpr std::size_t kCallStaticVoidMethodASlot = 143;
    static constexpr std::size_t kGetMethodIdSlot = 33;
    static constexpr std::size_t kGetFieldIdSlot = 94;
    static constexpr std::size_t kGetObjectFieldSlot = 95;
    static constexpr std::size_t kGetIntFieldSlot = 100;
    static constexpr std::size_t kGetLongFieldSlot = 101;
    static constexpr std::size_t kGetFloatFieldSlot = 102;
    static constexpr std::size_t kGetDoubleFieldSlot = 103;
    static constexpr std::size_t kNewObjectSlot = 28;
    static constexpr std::size_t kNewObjectVSlot = 29;
    static constexpr std::size_t kNewObjectASlot = 30;
    static constexpr std::size_t kNewStringSlot = 163;
    static constexpr std::size_t kGetStringLengthSlot = 164;
    static constexpr std::size_t kGetStringCharsSlot = 165;
    static constexpr std::size_t kReleaseStringCharsSlot = 166;
    static constexpr std::size_t kNewStringUtfSlot = 167;
    static constexpr std::size_t kGetStringUtfLengthSlot = 168;
    static constexpr std::size_t kGetStringUtfCharsSlot = 169;
    static constexpr std::size_t kReleaseStringUtfCharsSlot = 170;
    static constexpr std::size_t kGetArrayLengthSlot = 171;
    static constexpr std::size_t kNewObjectArraySlot = 172;
    static constexpr std::size_t kGetObjectArrayElementSlot = 173;
    static constexpr std::size_t kSetObjectArrayElementSlot = 174;
    static constexpr std::size_t kNewByteArraySlot = 176;
    static constexpr std::size_t kNewIntArraySlot = 179;
    static constexpr std::size_t kNewLongArraySlot = 180;
    static constexpr std::size_t kGetPrimitiveArrayCriticalSlot = 222;
    static constexpr std::size_t kReleasePrimitiveArrayCriticalSlot = 223;
    static constexpr std::size_t kGetByteArrayElementsSlot = 184;
    static constexpr std::size_t kGetIntArrayElementsSlot = 187;
    static constexpr std::size_t kGetLongArrayElementsSlot = 188;
    static constexpr std::size_t kReleaseByteArrayElementsSlot = 192;
    static constexpr std::size_t kReleaseIntArrayElementsSlot = 195;
    static constexpr std::size_t kReleaseLongArrayElementsSlot = 196;
    static constexpr std::size_t kGetByteArrayRegionSlot = 200;
    static constexpr std::size_t kGetIntArrayRegionSlot = 203;
    static constexpr std::size_t kGetLongArrayRegionSlot = 204;
    static constexpr std::size_t kSetByteArrayRegionSlot = 208;
    static constexpr std::size_t kSetIntArrayRegionSlot = 211;
    static constexpr std::size_t kSetLongArrayRegionSlot = 212;
    static constexpr std::size_t kCallObjectMethodSlot = 34;
    static constexpr std::size_t kCallObjectMethodVSlot = 35;
    static constexpr std::size_t kCallObjectMethodASlot = 36;
    static constexpr std::size_t kCallBooleanMethodSlot = 37;
    static constexpr std::size_t kCallBooleanMethodVSlot = 38;
    static constexpr std::size_t kCallBooleanMethodASlot = 39;
    static constexpr std::size_t kCallByteMethodSlot = 40;
    static constexpr std::size_t kCallByteMethodVSlot = 41;
    static constexpr std::size_t kCallByteMethodASlot = 42;
    static constexpr std::size_t kCallCharMethodSlot = 43;
    static constexpr std::size_t kCallCharMethodVSlot = 44;
    static constexpr std::size_t kCallCharMethodASlot = 45;
    static constexpr std::size_t kCallShortMethodSlot = 46;
    static constexpr std::size_t kCallShortMethodVSlot = 47;
    static constexpr std::size_t kCallShortMethodASlot = 48;
    static constexpr std::size_t kCallIntMethodSlot = 49;
    static constexpr std::size_t kCallIntMethodVSlot = 50;
    static constexpr std::size_t kCallIntMethodASlot = 51;
    static constexpr std::size_t kCallLongMethodSlot = 52;
    static constexpr std::size_t kCallLongMethodVSlot = 53;
    static constexpr std::size_t kCallLongMethodASlot = 54;
    static constexpr std::size_t kCallFloatMethodSlot = 55;
    static constexpr std::size_t kCallFloatMethodVSlot = 56;
    static constexpr std::size_t kCallFloatMethodASlot = 57;
    static constexpr std::size_t kCallDoubleMethodSlot = 58;
    static constexpr std::size_t kCallDoubleMethodVSlot = 59;
    static constexpr std::size_t kCallDoubleMethodASlot = 60;
    static constexpr std::size_t kCallVoidMethodSlot = 61;
    static constexpr std::size_t kCallVoidMethodVSlot = 62;
    static constexpr std::size_t kCallVoidMethodASlot = 63;
    static constexpr std::size_t kDeleteLocalRefSlot = 23;
    static constexpr std::size_t kIsSameObjectSlot = 24;
    static constexpr std::size_t kNewLocalRefSlot = 25;
    static constexpr std::size_t kGetObjectClassSlot = 31;
    static constexpr std::size_t kIsInstanceOfSlot = 32;
    static constexpr std::size_t kNewGlobalRefSlot = 21;
    static constexpr std::size_t kDeleteGlobalRefSlot = 22;
    static constexpr std::size_t kNewWeakGlobalRefSlot = 226;
    static constexpr std::size_t kDeleteWeakGlobalRefSlot = 227;
    static constexpr std::size_t kThrowSlot = 13;
    static constexpr std::size_t kThrowNewSlot = 14;
    static constexpr std::size_t kExceptionOccurredSlot = 15;
    static constexpr std::size_t kExceptionClearSlot = 17;
    static constexpr std::size_t kFatalErrorSlot = 18;
    static constexpr std::size_t kPushLocalFrameSlot = 19;
    static constexpr std::size_t kPopLocalFrameSlot = 20;
    static constexpr std::size_t kRegisterNativesSlot = 215;
    static constexpr std::size_t kUnregisterNativesSlot = 216;
    static constexpr std::size_t kGetJavaVmSlot = 219;
    static constexpr std::size_t kExceptionCheckSlot = 228;
    static constexpr std::size_t kVmAttachSlot = 4;
    static constexpr std::size_t kVmDetachSlot = 5;
    static constexpr std::size_t kVmGetEnvSlot = 6;

    GuestJni(GuestHeap* heap, GuestAddressSpace* address_space,
             BionicThreadState* thread_state, BionicFileSystem* files,
             GuestJniBackend* backend = nullptr);
    ~GuestJni();

    bool ConfigureCompatibility(GuestJniCompatibilityConfig config,
                                std::string* error);
    bool RegisterImports(Aarch64ImportRegistry* imports,
                         std::string* error);
    bool Initialize(const Aarch64ImportRegistry& imports,
                    std::string* error);
    bool AttachCurrentThread(std::string* error);
    bool DetachCurrentThread(std::string* error);
    bool Shutdown(std::string* error);

    // Compatibility-only references are guest-heap allocations backed by
    // owned metadata. They are unavailable when a desktop JVM backend is
    // active and never contain host pointers.
    bool CreateCompatibilityClassReference(std::string_view type,
                                           std::uint64_t* reference,
                                           std::string* error);
    bool CreateCompatibilityTypedObjectReference(std::string_view type,
                                                 std::uint64_t* reference,
                                                 std::string* error);
    // Data-driven compat policy: lines `kind|class|method|sig|value` with
    // empty/'*'-wildcard fields; consulted before the fail-closed rejects.
    std::size_t LoadCompatOverrides(const std::string& path,
                                    std::string* error);
    bool CreateCompatibilityStringReference(std::string_view value,
                                            std::uint64_t* reference,
                                            std::string* error);
    bool CreateCompatibilityReflectedMethodReference(
        std::uint64_t declaring_class_reference, std::string_view name,
        std::string_view signature, std::uint64_t* reference,
        std::string* error);
    bool CreateCompatibilityEmptyObjectArrayReference(
        std::uint64_t* reference, std::string* error);
    // [Ljava/lang/Object; with one element — used to marshal a real
    // argument (e.g. the Message) into JNIBridge.invoke's args array.
    bool CreateCompatibilitySingleObjectArrayReference(
        std::uint64_t element, std::uint64_t* reference,
        std::string* error);
    // Boxed java/lang/Long holding `value` in the reference's string
    // storage — Long.longValue()/toString() unbox it. Needed to marshal a
    // primitive long argument (Choreographer.FrameCallback.doFrame(J)V)
    // through JNIBridge.invoke's Object[] args.
    bool CreateCompatibilityLongReference(std::int64_t value,
                                          std::uint64_t* reference,
                                          std::string* error);
    // Pre-populated [J / [I arrays for host-driven native delivery
    // (SwappyDisplayManager.nSetSupportedRefreshPeriods(J[J[I)V).
    bool CreateCompatibilityLongArrayReference(
        std::vector<std::uint64_t> elements, std::uint64_t* reference,
        std::string* error);
    bool CreateCompatibilityIntArrayReference(
        std::vector<std::uint32_t> elements, std::uint64_t* reference,
        std::string* error);
    bool CreateCompatibilityByteArrayReference(
        std::vector<std::uint8_t> elements, std::uint64_t* reference,
        std::string* error);
    // Mint an android/view/MotionEvent compatibility object carrying the
    // fields Unity's nativeInjectEvent reads (action, coordinates, time).
    bool CreateCompatibilityMotionEventReference(
        int action, float x, float y, std::int64_t event_time_ms,
        std::uint64_t* reference, std::string* error);
    bool ReleaseCompatibilityLocalReference(std::uint64_t reference,
                                            std::string* error);
    // UnitySendMessage deliveries queued by compatibility stubs (e.g.
    // CtrAndroidCloudReader's Play-Games-absent catch path); the script
    // host forwards them through the bound nativeUnitySendMessage.
    struct PendingUnityMessage {
        std::string game_object;
        std::string method;
        std::string data;
    };
    std::vector<PendingUnityMessage> TakePendingUnityMessages() {
        const std::lock_guard lock(mutex_);
        std::vector<PendingUnityMessage> messages;
        messages.swap(pending_unity_messages_);
        return messages;
    }
    // Atomically drain AndroidPlatform.scheduleEvent captures for the
    // launcher loop (which owns guest execution) to answer with the
    // onTimerEvent export.
    std::vector<std::array<std::uint64_t, 2>>
    TakePendingScheduledEvents() {
        const std::lock_guard lock(mutex_);
        std::vector<std::array<std::uint64_t, 2>> events;
        events.swap(pending_scheduled_events_);
        return events;
    }
    std::vector<std::pair<bool, std::uint64_t>>
    TakePendingConsentCompletions() {
        const std::lock_guard lock(mutex_);
        std::vector<std::pair<bool, std::uint64_t>> handles;
        handles.swap(pending_consent_completions_);
        return handles;
    }
    // Atomically drain NetworkerJNI.HttpGet(String,J)V captures for the
    // launcher loop to answer through the Java_*_NetworkerJNI_callback
    // export. The jlong is the guest's heap-allocated std::function the
    // request was supposed to complete.
    std::vector<std::uint64_t> TakePendingNetworkerCompletions() {
        const std::lock_guard lock(mutex_);
        std::vector<std::uint64_t> contexts;
        contexts.swap(pending_networker_completions_);
        return contexts;
    }

    std::uint64_t vm_address() const;
    std::optional<std::uint64_t> CurrentEnvironmentAddress() const;
    std::size_t environment_count() const;
    std::size_t class_reference_count() const;
    std::size_t method_reference_count() const;
    std::size_t field_reference_count() const;
    std::size_t object_reference_count() const;
    std::size_t global_reference_count() const;
    std::size_t string_character_buffer_count() const;
    std::size_t string_utf_character_buffer_count() const;
    std::size_t byte_array_count() const;
    std::size_t byte_array_buffer_count() const;
    std::size_t object_array_count() const;
    std::size_t registered_native_count() const;
    std::vector<RegisteredNativeDescriptor> RegisteredNativesForClass(
        std::string_view class_type) const;
    std::vector<RegisteredNativeDescriptor> AllRegisteredNatives() const;
    std::optional<RegisteredNativeHandle> FindRegisteredNative(
        std::string_view class_type, std::string_view name,
        std::string_view signature) const;
    // Resolves a registered native directly to its guest function address;
    // used by script diagnostics to attribute registered natives to a module
    // RVA.
    std::optional<std::uint64_t> FindRegisteredNativeFunction(
        std::string_view class_type, std::string_view name,
        std::string_view signature) const;
    std::string uncaught_exception_build_id() const;
    std::optional<std::array<std::uint8_t, 4>>
    allowed_screen_orientations() const;
    std::optional<std::array<std::int32_t, 5>> platform_setup() const;
    std::optional<std::uint64_t> advertising_id_native_pointer() const;
    bool advertising_info_requested() const;
    std::optional<std::string> analytics_value_key() const;
    std::size_t pending_analytics_value_request_count() const;
    std::optional<AnalyticsValueRequest>
    NextPendingAnalyticsValueRequest() const;
    bool AcknowledgeAnalyticsValueRequest(std::uint64_t sequence,
                                          std::string* error);
    std::vector<AnalyticsValueRequest>
    TakePendingAnalyticsValueRequests();
    std::size_t pending_age_signals_request_count() const;
    std::optional<AgeSignalsRequest> NextPendingAgeSignalsRequest() const;
    bool AcknowledgeAgeSignalsRequest(std::uint64_t sequence,
                                      std::string* error);
    std::vector<AgeSignalsRequest> TakePendingAgeSignalsRequests();
    std::size_t pending_popup_dialog_request_count() const;
    std::optional<PopupDialogRequest>
    NextPendingPopupDialogRequest() const;
    bool AcknowledgePopupDialogRequest(std::uint64_t sequence,
                                       std::string* error);
    std::vector<PopupDialogRequest> TakePendingPopupDialogRequests();
    std::optional<std::array<std::uint64_t, 3>>
    firebase_analytics_configuration() const;
    std::optional<bool> firebase_analytics_collection_enabled() const;
    std::optional<std::uint64_t> referrer_fetch_native_pointer() const;
    std::optional<std::uint64_t> web_view_user_agent_native_pointer() const;
    std::size_t http_header_count() const;
    std::optional<std::array<std::string, 2>> last_http_header() const;
    std::size_t http_task_count() const;
    std::optional<std::array<std::uint64_t, 5>>
    http_task_configuration(std::uint64_t reference) const;
    std::optional<std::string> http_task_url(
        std::uint64_t reference) const;
    std::optional<bool> http_task_execution_requested(
        std::uint64_t reference) const;
    std::optional<std::uint64_t> last_http_cancel_request_id() const;
    void ResetHttpLifecycleEvents();
    std::vector<HttpLifecycleEvent> http_lifecycle_events() const;
    void ResetHttpObservationEvents();
    std::vector<HttpObservationEvent> http_observation_events() const;
    bool device_impl_started() const;
    bool device_screen_info_requested() const;
    std::optional<bool> platform_splash_visible() const;
    std::optional<bool> haptics_in_external_display_enabled() const;
    std::uint64_t haptics_vibrate_request_count() const;
    bool has_pending_thrown_exception() const {
        return exception_pending().reference != 0;
    }
    // Compatibility dispatch: Unity's JNIBridge protocol leaves a
    // NoSuchMethodError pending after probing an interface proxy method;
    // the Java proxy layer on Android catches and clears it. The host
    // dispatcher plays that role here.
    void ClearPendingException();
    bool WriteGuestMemory(std::uint64_t address, const void* source,
                          std::size_t size);
    std::optional<bool> keep_screen_on() const;
    bool startup_loading_finished() const;
    bool input_map_provider_initialized() const;
    bool app_not_responding_ui_task_requested() const;
    bool app_not_responding_task_processor_active() const;
    std::size_t pending_ui_task_count() const;
    std::vector<std::uint64_t> TakeReadyPendingUiTaskHandles();
    // Ready UI tasks with their dispatch descriptor. A task queued via
    // Handler.post/runOnUiThread carries no method (the peer is invoked as
    // Runnable.run); a Message.sendToTarget task carries the handler
    // callback's handleMessage method and the message reference.
    struct PendingUiTaskInfo {
        std::uint64_t native_handle = 0;
        std::string dispatch_class;
        std::string dispatch_name;
        std::string dispatch_signature;
        std::uint64_t argument_reference = 0;
    };
    std::vector<PendingUiTaskInfo> TakeReadyPendingUiTasks();
    // Each entry pairs the DisplayListener jobject reference with the proxy's
    // native handle (the SwappyDisplayManager cookie), captured when the game
    // calls DisplayManager.registerDisplayListener.
    std::vector<std::pair<std::uint64_t, std::uint64_t>>
    TakePendingDisplayListeners();
    // Finds a JNIBridge proxy implementing the given interface type
    // (e.g. "com/google/androidgamesdk/ChoreographerCallback"); returns
    // {proxy ref, native handle} when one exists.
    std::optional<std::pair<std::uint64_t, std::uint64_t>>
    FindInterfaceProxy(std::string_view interface_type);
    // Diagnostic: all live JNIBridge proxies as
    // "ref=0x.. handle=0x.. if=a,b,c" lines.
    std::string DescribeInterfaceProxies() const;
    // ChoreographerCallback(J)V objects captured at construction: {object
    // ref, native handle}.
    std::vector<std::pair<std::uint64_t, std::uint64_t>>
    TakeChoreographerCallbacks();
    // FrameCallback objects posted via Choreographer.postFrameCallback:
    // {object ref, native handle}. Each entry is consumed once — real
    // Choreographer fires the callback on the next vsync then drops it, so
    // the launcher drains and delivers doFrame(J)V exactly once per post.
    std::vector<std::pair<std::uint64_t, std::uint64_t>>
    TakePendingFrameCallbacks();
    std::optional<std::uint64_t>
    next_pending_ui_task_delay_milliseconds() const;
    std::optional<std::uint64_t> ads_platform_activity_identity() const;
    std::optional<std::uint64_t> ads_network_listener_native_pointer() const;
    bool google_billing_connect_requested() const;
    bool facebook_sdk_teardown_requested() const;
    bool ads_http_reset_requested() const;
    std::optional<BrazeConfiguration> braze_configuration() const;
    bool notification_token_provider_disconnected() const;
    bool notification_cancel_all_requested() const;
    bool local_notification_cancel_all_requested() const;
    std::optional<LocalNotificationSchedule>
    local_notification_schedule() const;
    bool ff_base_sdk_initialized() const;
    bool initialized() const;
    bool IsCurrentEnvironmentReference(std::uint64_t address) const;
    bool IsObjectReferenceOfType(std::uint64_t reference,
                                 std::string_view type) const;

    struct CompatOverride {
        std::string kind;
        std::string class_type;
        std::string name;
        std::string signature;
        std::string value;
    };
    bool ApplyCompatOverride(const char* kind, const std::string& class_type,
                             const std::string& name,
                             const std::string& signature, void* registers,
                             std::string* call_error);
    // Per-game Lua scripts register one dispatcher here; it is consulted at
    // the tail of ApplyCompatOverride while mutex_ is held, so the context
    // only exposes locked-context operations.
    void set_scripted_jni_handler(
        std::function<bool(ScriptedJniContext&)> handler);
    std::vector<CompatOverride> compat_overrides_;
    bool compat_overrides_loaded_ = false;
    // Backing store for AndroidPlatform.setToStringStore/getFromStringStore
    // (the Glu platform's named-value service: storage paths and similar).
    std::unordered_map<std::string, std::string> compat_string_store_;
private:
    friend class Aarch64ProfileRuntime;
    friend struct ScriptedJniContext;
    std::function<bool(ScriptedJniContext&)> scripted_jni_handler_;
    bool CreateCompatibilityTypedObjectReferenceLocked(
        std::string_view type, std::uint64_t* reference, std::string* error);

    bool ResolveRegisteredNativeTarget(RegisteredNativeHandle handle,
                                       std::uint64_t* function,
                                       std::string* error) const;
    bool BuildEnvironmentLocked(std::uint64_t thread_handle,
                                std::string* error);
    bool DeleteLocalReference(std::uint64_t reference,
                              std::string* error);
    bool DeleteLocalReferenceLocked(std::uint64_t reference,
                                    std::string* error);
    void TrackLocalReferenceLocked(std::uint64_t reference);
    void ForgetLocalReferenceLocked(std::uint64_t reference);
    // Back a fabricated array-typed object reference with empty storage in
    // the matching array map so GetArrayLength/array accessors see a real
    // zero-length array instead of a bare reference.
    void EnsureStubArrayStorageLocked(std::uint64_t reference,
                                      const std::string& return_type);
    bool CurrentThreadOwnsLocalReferenceLocked(
        std::uint64_t reference) const;
    // True when an identity owns attachable resources (array storage, stream
    // state, handler bindings). Reference deletion gates its O(refs) liveness
    // scan on this: with nothing attached there is nothing to clean up, so
    // the scan is skipped entirely.
    bool IdentityHasAttachmentsLocked(std::uint64_t identity) const;
    // Drops every identity-keyed attachment map entry. Call only after the
    // liveness scan shows no remaining reference to the identity.
    bool EraseIdentityAttachmentsLocked(std::uint64_t identity,
                                        std::string* error);
    bool IsVmAddress(std::uint64_t address) const;
    bool IsCurrentEnvironment(std::uint64_t address) const;
    bool WriteGuestPointer(std::uint64_t output, std::uint64_t value,
                           std::string* error) const;

    GuestHeap* heap_ = nullptr;
    GuestAddressSpace* address_space_ = nullptr;
    BionicThreadState* thread_state_ = nullptr;
    BionicFileSystem* files_ = nullptr;
    GuestJniBackend* backend_ = nullptr;
    const Aarch64ImportRegistry* imports_ = nullptr;
    std::unique_ptr<GuestFunctionTable> vm_;
    std::unordered_map<std::uint64_t,
                       std::unique_ptr<GuestFunctionTable>>
        environments_;
    struct ClassReference {
        std::uint64_t identity;
        std::string type;
        GuestJniBackend::Handle backend = 0;
    };
    std::unordered_map<std::uint64_t, ClassReference> class_references_;
    // Local class refs already deleted once. DeleteLocalRef tolerates
    // untracked references as a no-op (il2cpp reflection cleanup double-
    // deletes reflected Method objects), but a second delete of a known
    // class ref still refuses — matching the IndirectRefTable contract the
    // smoke suite pins. Class refs are few, so the tombstone stays bounded;
    // a reused heap address is tracked again and never reaches this set.
    std::unordered_set<std::uint64_t> deleted_class_references_;
    // Heap blocks of compat objects/classes whose ref entry was dropped but
    // whose storage stayed mapped (the guest may still hold the raw
    // address). Released at Shutdown so teardown still drains the heap.
    std::unordered_set<std::uint64_t> retained_reference_blocks_;
    struct MethodReference {
        std::uint64_t class_identity;
        std::string class_type;
        std::string name;
        std::string signature;
        bool is_static;
        GuestJniBackend::Handle backend = 0;
    };
    std::unordered_map<std::uint64_t, MethodReference> method_references_;
    struct FieldReference {
        std::uint64_t class_identity;
        std::string class_type;
        std::string name;
        std::string signature;
        bool is_static;
    };
    std::unordered_map<std::uint64_t, FieldReference> field_references_;
    struct ObjectReference {
        std::uint64_t identity;
        std::uint64_t method_reference;
        std::string type;
        std::string string_value;
        GuestJniBackend::Handle backend = 0;
    };
    std::unordered_map<std::uint64_t, ObjectReference> object_references_;
    // AndroidPlatform.scheduleEvent(JJ)V captures: the launcher loop drains
    // these and fires the matching onTimerEvent guest export, replacing the
    // Java handler thread the compat runtime does not run.
    std::vector<std::array<std::uint64_t, 2>> pending_scheduled_events_;
    // AndroidPlatform.requestCMPConsentInfo(JLandroid/app/Activity;)V and
    // showCMPConsentForm(JLandroid/app/Activity;ZZ)V captures: the native
    // callback pointers the launcher loop completes through the matching
    // *Complete export. bool selects the export (false=request, true=form).
    std::vector<std::pair<bool, std::uint64_t>> pending_consent_completions_;
    // NetworkerJNI.HttpGet(Ljava/lang/String;J)V captures: the jlong is the
    // guest's std::function<void(int,string)> the Java side hands back through
    // Java_com_popcap_pcsp_NetworkerJNI_callback. No HTTP transport exists, so
    // the launcher loop completes each capture with a failure status so the
    // caller's completion (ad-load finished) still runs instead of parking.
    std::vector<std::uint64_t> pending_networker_completions_;
    struct LocalReferenceFrame {
        std::size_t requested_capacity = 0;
        std::vector<std::uint64_t> created_references;
    };
    std::unordered_map<std::uint64_t,
                       std::vector<LocalReferenceFrame>>
        local_reference_frames_;
    std::unordered_map<std::uint64_t, std::uint64_t>
        local_reference_owners_;
    struct GlobalClassReference {
        std::uint64_t identity;
        std::string type;
        GuestJniBackend::Handle backend = 0;
    };
    struct GlobalObjectReference {
        std::uint64_t identity;
        std::uint64_t method_reference;
        std::string type;
        std::string string_value;
        GuestJniBackend::Handle backend = 0;
    };
    std::unordered_map<std::uint64_t, GlobalClassReference>
        global_class_references_;
    std::unordered_map<std::uint64_t, GlobalObjectReference>
        global_object_references_;
    std::unordered_map<std::uint64_t, std::uint64_t>
        string_character_buffers_;
    std::unordered_map<std::uint64_t, std::uint64_t>
        string_utf_character_buffers_;
    std::unordered_map<std::uint64_t, std::vector<std::uint8_t>>
        byte_arrays_;
    // Fields carried by MotionEvent compatibility objects so the
    // getAction/getX/getEventTime family answers real values.
    struct MotionEventStorage {
        int action = 0;
        float x = 0.0f;
        float y = 0.0f;
        std::int64_t event_time_ms = 0;
    };
    std::unordered_map<std::uint64_t, MotionEventStorage> motion_events_;
    // Synthetic touchscreen device: Unity polls InputDevice.getDeviceIds and
    // drops injected MotionEvents when no touch source is registered, so the
    // splash's tap gate never opens. Map a device-object ref to its id so the
    // getSources/getId/isVirtual family answers a real touchscreen.
    std::unordered_map<std::uint64_t, std::uint64_t> input_devices_;
    // InputDevice.MotionRange objects handed back by getMotionRange so Unity
    // can read the touch axes' extent.
    struct InputRangeStorage {
        int axis = 0;
        float min = 0.0f;
        float max = 0.0f;
    };
    std::unordered_map<std::uint64_t, InputRangeStorage> input_ranges_;
    std::unordered_map<std::uint64_t, std::vector<std::uint32_t>>
        int_arrays_;
    std::unordered_map<std::uint64_t, std::vector<std::uint64_t>>
        long_arrays_;
    struct ObjectArrayElement {
        bool is_class = false;
        std::uint64_t identity = 0;
        std::uint64_t method_reference = 0;
        std::string type;
        std::string string_value;
    };
    struct ObjectArrayStorage {
        std::string type;
        std::string element_type;
        std::vector<std::optional<ObjectArrayElement>> elements;
    };
    std::unordered_map<std::uint64_t, ObjectArrayStorage> object_arrays_;
    struct InterfaceProxyStorage {
        std::uint64_t native_handle = 0;
        std::vector<std::string> interfaces;
    };
    std::unordered_map<std::uint64_t, InterfaceProxyStorage>
        interface_proxies_;
    struct PendingUiTask {
        std::uint64_t native_handle = 0;
        std::chrono::steady_clock::time_point ready_at{};
        // Optional dispatch override for non-Runnable callbacks (e.g.
        // Handler.Callback.handleMessage). Empty class means Runnable.run.
        std::string dispatch_class;
        std::string dispatch_name;
        std::string dispatch_signature;
        std::uint64_t argument_reference = 0;
    };
    std::vector<PendingUiTask> pending_ui_tasks_;
    // Handler identity -> the JNIBridge native handle of the Handler.Callback
    // proxy passed to its constructor.
    std::unordered_map<std::uint64_t, std::uint64_t> handler_callbacks_;
    // Message identity -> the callback native handle of the Handler that
    // produced it via obtainMessage, so sendToTarget can be delivered.
    std::unordered_map<std::uint64_t, std::uint64_t> message_callbacks_;
    // DisplayListener proxies registered through
    // DisplayManager.registerDisplayListener: {listener ref, native handle}.
    std::vector<std::pair<std::uint64_t, std::uint64_t>> display_listeners_;
    // SDK init-listener native handles captured at registration so the
    // initialize call can deliver its completion callback.
    std::uint64_t max_background_callback_handle_ = 0;
    std::uint64_t adquality_init_listener_handle_ = 0;
    std::vector<PendingUnityMessage> pending_unity_messages_;
    // ChoreographerCallback objects constructed with a (J)V cookie: {object
    // ref, native handle}. Swappy feeds these to
    // ChoreographerCallback.nOnChoreographer(cookie, frameTimeNanos).
    std::vector<std::pair<std::uint64_t, std::uint64_t>>
        choreographer_callbacks_;
    // FrameCallback objects queued by Choreographer.postFrameCallback,
    // consumed once per post: {object ref, native handle}.
    std::vector<std::pair<std::uint64_t, std::uint64_t>>
        pending_frame_callbacks_;
    struct InputStreamStorage {
        int descriptor = -1;
    };
    std::unordered_map<std::uint64_t, InputStreamStorage> input_streams_;
    struct ScannerStorage {
        std::uint64_t input_stream_identity = 0;
        std::string delimiter;
        bool consumed = false;
    };
    std::unordered_map<std::uint64_t, ScannerStorage> scanners_;
    struct SharedPreferencesEditorStorage {
        std::string preference_name;
        std::unordered_map<std::string, std::int32_t> pending_ints;
        std::unordered_map<std::string, std::string> pending_strings;
    };
    std::unordered_map<std::uint64_t, SharedPreferencesEditorStorage>
        shared_preferences_editors_;
    std::unordered_map<
        std::string, std::unordered_map<std::string, std::int32_t>>
        shared_preferences_ints_;
    std::unordered_map<
        std::string, std::unordered_map<std::string, std::string>>
        shared_preferences_strings_;
    struct AlertDialogBuilderStorage {
        std::string title;
        std::string message;
        std::string positive_button;
        bool cancelable = true;
        std::uint64_t positive_callback_handle = 0;
    };
    std::unordered_map<std::uint64_t, AlertDialogBuilderStorage>
        alert_dialog_builders_;
    struct RegisteredNativeMethod {
        RegisteredNativeHandle handle;
        std::string class_type;
        std::string name;
        std::string signature;
        std::uint64_t function = 0;
    };
    std::vector<RegisteredNativeMethod> registered_native_methods_;
    std::uint64_t next_registered_native_id_ = 1;
    struct ByteArrayBuffer {
        std::uint64_t identity;
        std::size_t length;
    };
    std::unordered_map<std::uint64_t, ByteArrayBuffer>
        byte_array_buffers_;
    std::unordered_map<std::uint64_t, ByteArrayBuffer>
        int_array_buffers_;
    std::unordered_map<std::uint64_t, ByteArrayBuffer>
        long_array_buffers_;
    std::string uncaught_exception_build_id_;
    std::optional<std::array<std::uint8_t, 4>>
        allowed_screen_orientations_;
    std::optional<std::array<std::int32_t, 5>> platform_setup_;
    std::optional<std::uint64_t> advertising_id_native_pointer_;
    bool advertising_info_requested_ = false;
    std::optional<std::string> analytics_value_key_;
    std::vector<AnalyticsValueRequest> pending_analytics_value_requests_;
    std::uint64_t analytics_value_request_sequence_ = 0;
    std::vector<AgeSignalsRequest> pending_age_signals_requests_;
    std::uint64_t age_signals_request_sequence_ = 0;
    std::vector<PopupDialogRequest> pending_popup_dialog_requests_;
    std::uint64_t popup_dialog_request_sequence_ = 0;
    std::optional<std::array<std::uint64_t, 3>>
        firebase_analytics_configuration_;
    std::optional<bool> firebase_analytics_collection_enabled_;
    std::optional<std::uint64_t> referrer_fetch_native_pointer_;
    std::optional<std::uint64_t> web_view_user_agent_native_pointer_;
    struct HttpHeaderEntry {
        std::uint64_t headers_identity;
        std::string name;
        std::string value;
    };
    std::vector<HttpHeaderEntry> http_headers_;
    struct HttpTaskState {
        std::uint64_t request_id;
        std::int32_t method;
        std::string url;
        std::int32_t option;
        bool flag;
        std::uint64_t headers_identity;
        std::vector<std::uint8_t> body;
        bool execution_requested = false;
    };
    std::unordered_map<std::uint64_t, HttpTaskState> http_tasks_;
    std::optional<std::uint64_t> last_http_cancel_request_id_;
    std::array<HttpLifecycleEvent, kHttpLifecycleEventCapacity>
        http_lifecycle_events_{};
    std::size_t http_lifecycle_event_count_ = 0;
    std::size_t http_lifecycle_event_next_ = 0;
    std::uint64_t http_lifecycle_event_sequence_ = 0;
    std::array<HttpObservationEvent, kHttpObservationEventCapacity>
        http_observation_events_{};
    std::size_t http_observation_event_count_ = 0;
    std::size_t http_observation_event_next_ = 0;
    std::uint64_t http_observation_event_sequence_ = 0;
    std::uint64_t http_observation_owner_thread_hash_ = 0;
    bool device_impl_started_ = false;
    bool device_screen_info_requested_ = false;
    std::optional<bool> platform_splash_visible_;
    GuestJniCompatibilityConfig compatibility_;
    std::optional<bool> haptics_in_external_display_enabled_;
    std::uint64_t haptics_vibrate_requests_ = 0;
    // JNI pending-exception state is per-thread by specification; kept in
    // thread_local storage so concurrent guest contexts never observe each
    // other's exceptions.
    struct PendingExceptionState {
        bool pending = false;
        std::string type;
        std::string message;
        std::uint64_t reference = 0;
    };
    static PendingExceptionState& exception_pending();
    std::optional<bool> keep_screen_on_;
    bool startup_loading_finished_ = false;
    bool input_map_provider_initialized_ = false;
    bool app_not_responding_ui_task_requested_ = false;
    bool app_not_responding_task_processor_active_ = false;
    std::optional<std::uint64_t> ads_platform_activity_identity_;
    std::optional<std::uint64_t> ads_network_listener_native_pointer_;
    bool google_billing_connect_requested_ = false;
    bool facebook_sdk_teardown_requested_ = false;
    bool ads_http_reset_requested_ = false;
    std::optional<BrazeConfiguration> braze_configuration_;
    bool notification_token_provider_disconnected_ = false;
    bool notification_cancel_all_requested_ = false;
    bool local_notification_cancel_all_requested_ = false;
    std::optional<LocalNotificationSchedule> local_notification_schedule_;
    bool ff_base_sdk_initialized_ = false;
    auto FindOwnedLocalClassReferenceLocked(
        std::uint64_t reference) const
        -> std::unordered_map<std::uint64_t,
                              ClassReference>::const_iterator;
    auto FindOwnedLocalObjectReferenceLocked(
        std::uint64_t reference) const
        -> std::unordered_map<std::uint64_t,
                              ObjectReference>::const_iterator;
    bool IsClassReferenceLocked(std::uint64_t reference) const;
    // Resurrect a stale-but-live compat class ref: il2cpp hands jclass
    // values across threads and caches, so a ref whose map entry was dropped
    // by an earlier delete may still point at a pinned compat class object.
    // Probe the heap block (magic + "java/lang/Class" tag + payload type
    // name) and re-register it under the current thread.
    bool RecoverClassReferenceLocked(std::uint64_t reference);
    bool IsObjectReferenceLocked(std::uint64_t reference) const;
    // CreateCompatibilityStringReference body without taking mutex_; for
    // dispatch handlers that already hold it (a second lock on the same
    // thread throws std::system_error and aborts the session).
    bool CreateCompatibilityStringReferenceLocked(std::string_view value,
                                                  std::uint64_t* reference,
                                                  std::string* error);
    std::optional<std::uint64_t> ReferenceIdentityLocked(
        std::uint64_t reference) const;
    std::optional<std::string_view> ClassTypeLocked(
        std::uint64_t reference) const;
    std::optional<std::string_view> ObjectTypeLocked(
        std::uint64_t reference) const;
    std::optional<GuestJniBackend::Handle> BackendReferenceLocked(
        std::uint64_t reference) const;
    void RecordHttpLifecycleEventLocked(HttpLifecycleEventKind kind,
                                        std::uint64_t request_id);
    void RecordHttpObservationEventLocked(
        HttpObservationEventKind kind, std::string_view class_name,
        std::string_view method_name, std::string_view signature,
        std::array<std::uint64_t, 4> arguments = {});
    bool QueueAnalyticsValueRequestLocked(std::string key,
                                          std::string* error);
    bool QueueAgeSignalsRequestLocked(std::uint64_t first_callback_handle,
                                      std::uint64_t debug_scenario,
                                      std::uint64_t second_callback_handle,
                                      std::string* error);
    bool QueuePopupDialogRequestLocked(PopupDialogRequest request,
                                       std::string* error);
    bool CloseInputStreamLocked(std::uint64_t identity,
                                std::string* error);
    mutable class TrackedJniMutex mutex_;
    bool registration_attempted_ = false;
    bool initialization_attempted_ = false;
    bool initialized_ = false;
};

}  // namespace isomr
