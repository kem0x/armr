#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <endpointvolume.h>
#include <mmdeviceapi.h>
#include <objbase.h>

#include "guest_jni.h"

#include "bionic_files.h"
#include "bionic_thread_state.h"
#include "guest_address_space.h"
#include "guest_function_table.h"
#include "guest_heap.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <cmath>
#include <fstream>
#include <cstdlib>
#include <cstdio>
#include <functional>
#include <limits>
#include <new>
#include <sstream>
#include <string_view>
#include <atomic>
#include <thread>
#include <utility>
#include <vector>

namespace isomr {
namespace {

constexpr std::string_view kGetVersion = "__isomr_jni_get_version";
constexpr std::string_view kFindClass = "__isomr_jni_find_class";
constexpr std::string_view kFromReflectedMethod =
    "__isomr_jni_from_reflected_method";
constexpr std::string_view kToReflectedMethod =
    "__isomr_jni_to_reflected_method";
constexpr std::string_view kGetStaticMethodId =
    "__isomr_jni_get_static_method_id";
constexpr std::string_view kGetStaticFieldId =
    "__isomr_jni_get_static_field_id";
constexpr std::string_view kGetStaticObjectField =
    "__isomr_jni_get_static_object_field";
constexpr std::string_view kGetStaticIntField =
    "__isomr_jni_get_static_int_field";
constexpr std::string_view kGetMethodId = "__isomr_jni_get_method_id";
constexpr std::string_view kGetFieldId = "__isomr_jni_get_field_id";
constexpr std::string_view kGetObjectField =
    "__isomr_jni_get_object_field";
constexpr std::string_view kGetIntField = "__isomr_jni_get_int_field";
constexpr std::string_view kGetLongField = "__isomr_jni_get_long_field";
constexpr std::string_view kGetFloatField = "__isomr_jni_get_float_field";
constexpr std::string_view kGetDoubleField = "__isomr_jni_get_double_field";
constexpr std::string_view kNewObject = "__isomr_jni_new_object";
constexpr std::string_view kNewObjectV = "__isomr_jni_new_object_v";
constexpr std::string_view kNewObjectA = "__isomr_jni_new_object_a";
constexpr std::string_view kNewString = "__isomr_jni_new_string";
constexpr std::string_view kGetStringLength =
    "__isomr_jni_get_string_length";
constexpr std::string_view kGetStringChars = "__isomr_jni_get_string_chars";
constexpr std::string_view kReleaseStringChars =
    "__isomr_jni_release_string_chars";
constexpr std::string_view kNewStringUtf = "__isomr_jni_new_string_utf";
constexpr std::string_view kGetStringUtfLength =
    "__isomr_jni_get_string_utf_length";
constexpr std::string_view kGetStringUtfChars =
    "__isomr_jni_get_string_utf_chars";
constexpr std::string_view kReleaseStringUtfChars =
    "__isomr_jni_release_string_utf_chars";
constexpr std::string_view kGetArrayLength =
    "__isomr_jni_get_array_length";
constexpr std::string_view kNewObjectArray =
    "__isomr_jni_new_object_array";
constexpr std::string_view kGetObjectArrayElement =
    "__isomr_jni_get_object_array_element";
constexpr std::string_view kSetObjectArrayElement =
    "__isomr_jni_set_object_array_element";
constexpr std::string_view kNewByteArray = "__isomr_jni_new_byte_array";
constexpr std::string_view kNewIntArray = "__isomr_jni_new_int_array";
constexpr std::string_view kNewLongArray = "__isomr_jni_new_long_array";
constexpr std::string_view kGetPrimitiveArrayCritical =
    "__isomr_jni_get_primitive_array_critical";
constexpr std::string_view kReleasePrimitiveArrayCritical =
    "__isomr_jni_release_primitive_array_critical";
constexpr std::string_view kGetByteArrayElements =
    "__isomr_jni_get_byte_array_elements";
constexpr std::string_view kGetIntArrayElements =
    "__isomr_jni_get_int_array_elements";
constexpr std::string_view kGetLongArrayElements =
    "__isomr_jni_get_long_array_elements";
constexpr std::string_view kReleaseByteArrayElements =
    "__isomr_jni_release_byte_array_elements";
constexpr std::string_view kReleaseIntArrayElements =
    "__isomr_jni_release_int_array_elements";
constexpr std::string_view kReleaseLongArrayElements =
    "__isomr_jni_release_long_array_elements";
constexpr std::string_view kGetByteArrayRegion =
    "__isomr_jni_get_byte_array_region";
constexpr std::string_view kGetIntArrayRegion =
    "__isomr_jni_get_int_array_region";
constexpr std::string_view kGetLongArrayRegion =
    "__isomr_jni_get_long_array_region";
constexpr std::string_view kSetByteArrayRegion =
    "__isomr_jni_set_byte_array_region";
constexpr std::string_view kSetIntArrayRegion =
    "__isomr_jni_set_int_array_region";
constexpr std::string_view kSetLongArrayRegion =
    "__isomr_jni_set_long_array_region";
constexpr std::string_view kCallObjectMethod =
    "__isomr_jni_call_object_method";
constexpr std::string_view kCallObjectMethodV =
    "__isomr_jni_call_object_method_v";
constexpr std::string_view kCallObjectMethodA =
    "__isomr_jni_call_object_method_a";
constexpr std::string_view kCallBooleanMethod =
    "__isomr_jni_call_boolean_method";
constexpr std::string_view kCallBooleanMethodV =
    "__isomr_jni_call_boolean_method_v";
constexpr std::string_view kCallBooleanMethodA =
    "__isomr_jni_call_boolean_method_a";
constexpr std::string_view kCallByteMethod =
    "__isomr_jni_call_byte_method";
constexpr std::string_view kCallByteMethodA =
    "__isomr_jni_call_byte_method_a";
constexpr std::string_view kCallCharMethod =
    "__isomr_jni_call_char_method";
constexpr std::string_view kCallCharMethodA =
    "__isomr_jni_call_char_method_a";
constexpr std::string_view kCallShortMethod =
    "__isomr_jni_call_short_method";
constexpr std::string_view kCallShortMethodA =
    "__isomr_jni_call_short_method_a";
constexpr std::string_view kCallIntMethod =
    "__isomr_jni_call_int_method";
constexpr std::string_view kCallIntMethodV =
    "__isomr_jni_call_int_method_v";
constexpr std::string_view kCallIntMethodA =
    "__isomr_jni_call_int_method_a";
constexpr std::string_view kCallLongMethod =
    "__isomr_jni_call_long_method";
constexpr std::string_view kCallLongMethodV =
    "__isomr_jni_call_long_method_v";
constexpr std::string_view kCallLongMethodA =
    "__isomr_jni_call_long_method_a";
constexpr std::string_view kCallFloatMethod =
    "__isomr_jni_call_float_method";
constexpr std::string_view kCallFloatMethodV =
    "__isomr_jni_call_float_method_v";
constexpr std::string_view kCallFloatMethodA =
    "__isomr_jni_call_float_method_a";
constexpr std::string_view kCallDoubleMethod =
    "__isomr_jni_call_double_method";
constexpr std::string_view kCallDoubleMethodV =
    "__isomr_jni_call_double_method_v";
constexpr std::string_view kCallDoubleMethodA =
    "__isomr_jni_call_double_method_a";
constexpr std::string_view kCallVoidMethod =
    "__isomr_jni_call_void_method";
constexpr std::string_view kCallVoidMethodV =
    "__isomr_jni_call_void_method_v";
constexpr std::string_view kCallVoidMethodA =
    "__isomr_jni_call_void_method_a";
constexpr std::string_view kDeleteLocalRef = "__isomr_jni_delete_local_ref";
constexpr std::string_view kIsSameObject = "__isomr_jni_is_same_object";
constexpr std::string_view kNewLocalRef = "__isomr_jni_new_local_ref";
constexpr std::string_view kGetObjectClass = "__isomr_jni_get_object_class";
constexpr std::string_view kIsInstanceOf = "__isomr_jni_is_instance_of";
constexpr std::string_view kNewGlobalRef = "__isomr_jni_new_global_ref";
constexpr std::string_view kDeleteGlobalRef = "__isomr_jni_delete_global_ref";
constexpr std::string_view kNewWeakGlobalRef =
    "__isomr_jni_new_weak_global_ref";
constexpr std::string_view kDeleteWeakGlobalRef =
    "__isomr_jni_delete_weak_global_ref";
constexpr std::string_view kCallStaticObjectMethod =
    "__isomr_jni_call_static_object_method";
constexpr std::string_view kCallStaticObjectMethodV =
    "__isomr_jni_call_static_object_method_v";
constexpr std::string_view kCallStaticObjectMethodA =
    "__isomr_jni_call_static_object_method_a";
constexpr std::string_view kCallStaticBooleanMethod =
    "__isomr_jni_call_static_boolean_method";
constexpr std::string_view kCallStaticBooleanMethodV =
    "__isomr_jni_call_static_boolean_method_v";
constexpr std::string_view kCallStaticBooleanMethodA =
    "__isomr_jni_call_static_boolean_method_a";
// Synthetic touchscreen device id reported by InputDevice.getDeviceIds so
// Unity registers a touch source and consumes injected MotionEvents.
constexpr std::uint64_t kArmrTouchDeviceId = 1;
constexpr std::string_view kCallStaticByteMethodA =
    "__isomr_jni_call_static_byte_method_a";
constexpr std::string_view kCallStaticCharMethodA =
    "__isomr_jni_call_static_char_method_a";
constexpr std::string_view kCallStaticShortMethodA =
    "__isomr_jni_call_static_short_method_a";
constexpr std::string_view kCallStaticIntMethod =
    "__isomr_jni_call_static_int_method";
constexpr std::string_view kCallStaticIntMethodV =
    "__isomr_jni_call_static_int_method_v";
constexpr std::string_view kCallStaticIntMethodA =
    "__isomr_jni_call_static_int_method_a";
constexpr std::string_view kCallStaticLongMethod =
    "__isomr_jni_call_static_long_method";
constexpr std::string_view kCallStaticLongMethodV =
    "__isomr_jni_call_static_long_method_v";
constexpr std::string_view kCallStaticLongMethodA =
    "__isomr_jni_call_static_long_method_a";
constexpr std::string_view kCallStaticVoidMethod =
    "__isomr_jni_call_static_void_method";
constexpr std::string_view kCallStaticVoidMethodV =
    "__isomr_jni_call_static_void_method_v";
constexpr std::string_view kCallStaticVoidMethodA =
    "__isomr_jni_call_static_void_method_a";
constexpr std::string_view kThrowNew =
    "__isomr_jni_throw_new";
constexpr std::string_view kThrowThrowable =
    "__isomr_jni_throw";
constexpr std::string_view kExceptionOccurred =
    "__isomr_jni_exception_occurred";
constexpr std::string_view kExceptionClear =
    "__isomr_jni_exception_clear";
constexpr std::string_view kFatalError = "__isomr_jni_fatal_error";
constexpr std::string_view kPushLocalFrame =
    "__isomr_jni_push_local_frame";
constexpr std::string_view kPopLocalFrame =
    "__isomr_jni_pop_local_frame";
constexpr std::string_view kRegisterNatives =
    "__isomr_jni_register_natives";
constexpr std::string_view kUnregisterNatives =
    "__isomr_jni_unregister_natives";
constexpr std::string_view kGetJavaVm = "__isomr_jni_get_java_vm";
constexpr std::string_view kExceptionCheck =
    "__isomr_jni_exception_check";
constexpr std::string_view kVmDestroy = "__isomr_jvm_destroy";
constexpr std::string_view kVmAttach = "__isomr_jvm_attach";
constexpr std::string_view kVmDetach = "__isomr_jvm_detach";
constexpr std::string_view kVmGetEnv = "__isomr_jvm_get_env";

std::optional<float> NativeWindowsOutputVolume() {
    const HRESULT initialization =
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool uninitialize = SUCCEEDED(initialization);
    if (FAILED(initialization) && initialization != RPC_E_CHANGED_MODE)
        return std::nullopt;

    IMMDeviceEnumerator* enumerator = nullptr;
    IMMDevice* device = nullptr;
    IAudioEndpointVolume* endpoint = nullptr;
    float volume = 0.0f;
    HRESULT result = CoCreateInstance(
        __uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER,
        __uuidof(IMMDeviceEnumerator),
        reinterpret_cast<void**>(&enumerator));
    if (SUCCEEDED(result))
        result = enumerator->GetDefaultAudioEndpoint(
            eRender, eMultimedia, &device);
    if (SUCCEEDED(result))
        result = device->Activate(
            __uuidof(IAudioEndpointVolume), CLSCTX_INPROC_SERVER, nullptr,
            reinterpret_cast<void**>(&endpoint));
    if (SUCCEEDED(result))
        result = endpoint->GetMasterVolumeLevelScalar(&volume);
    if (endpoint != nullptr) endpoint->Release();
    if (device != nullptr) device->Release();
    if (enumerator != nullptr) enumerator->Release();
    if (uninitialize) CoUninitialize();
    if (FAILED(result) || !std::isfinite(volume)) return std::nullopt;
    return std::clamp(volume, 0.0f, 1.0f);
}
constexpr std::string_view kVmAttachDaemon =
    "__isomr_jvm_attach_daemon";

constexpr std::int32_t kJniOk = 0;
constexpr std::int32_t kJniError = -1;
constexpr std::int32_t kJniDetached = -2;
constexpr std::int32_t kJniVersionError = -3;
constexpr std::uint32_t kJniVersion11 = 0x00010001U;
constexpr std::uint32_t kJniVersion12 = 0x00010002U;
constexpr std::uint32_t kJniVersion14 = 0x00010004U;
constexpr std::uint32_t kJniVersion16 = 0x00010006U;
constexpr std::uint32_t kCompatibilityObjectMagic = 0x4b4c4a4fU;

// JNI reference-op census: [0]=NewGlobalRef, [1]=DeleteGlobalRef,
// [2]=DeleteLocalRef. Bounded periodic totals identify churn-bound
// phases (deserialization loops that marshal object refs per record).
std::atomic<std::uint64_t> ref_op_counts[3];
constexpr std::size_t kCompatibilityObjectSize = 0x40;
constexpr std::size_t kMaximumByteArraySize = 64U * 1024U * 1024U;
constexpr std::size_t kMaximumObjectArrayLength = 1024U * 1024U;
constexpr std::size_t kMaximumRegisteredNativeMethods = 4096;

std::chrono::steady_clock::time_point UiTaskReadyAt(
    std::uint64_t delay_milliseconds) {
    const auto now = std::chrono::steady_clock::now();
    const auto maximum_delay = std::chrono::duration_cast<
        std::chrono::milliseconds>(
        std::chrono::steady_clock::time_point::max() - now);
    const auto bounded_delay = static_cast<std::chrono::milliseconds::rep>(
        std::min<std::uint64_t>(
            delay_milliseconds,
            static_cast<std::uint64_t>(maximum_delay.count())));
    return now + std::chrono::milliseconds(bounded_delay);
}

struct GuestJniNativeMethod {
    std::uint64_t name;
    std::uint64_t signature;
    std::uint64_t function;
};
static_assert(sizeof(GuestJniNativeMethod) == 24);

enum class CompatibilityReferenceKind {
    kClass,
    kObject,
    kString,
};

void SetError(std::string* error, std::string message) {
    if (error != nullptr) *error = std::move(message);
}

std::string NormalizeCompatibilityType(std::string_view type) {
    if (type.size() >= 2 && type.front() == 'L' && type.back() == ';')
        return std::string(type.substr(1, type.size() - 2));
    return std::string(type);
}

// Reference .type fields are stored inconsistently: NewStringUTF keeps the
// descriptor form "Ljava/lang/String;" while objects created through the
// compatibility path keep the normalized "java/lang/String". Normalize before
// comparing a stored reference type so either representation matches.
bool IsStringTypeName(std::string_view type) {
    return NormalizeCompatibilityType(type) == "java/lang/String";
}

// JNI receiver types are slashed ('Lpkg/Type;' or 'pkg/Type') while Nimble
// declares methods under dotted class names; compare with slash/dot and
// enclosing-L tolerance.
bool ReceiverClassMatches(std::string_view receiver,
                          std::string_view declaring) {
    const auto strip = [](std::string_view t) {
        return t.size() >= 2 && t.front() == 'L' && t.back() == ';'
                   ? t.substr(1, t.size() - 2)
                   : t;
    };
    const std::string_view r = strip(receiver);
    const std::string_view d = strip(declaring);
    if (r == d) return true;
    if (r.size() != d.size()) return false;
    for (std::size_t i = 0; i < r.size(); ++i) {
        const char a = r[i];
        const char b = d[i];
        if (a == b) continue;
        if ((a == '/' && b == '.') || (a == '.' && b == '/')) continue;
        return false;
    }
    return true;
}

bool IsCompatibilityAssignable(std::string_view source,
                               std::string_view target) {
    const std::string normalized_source = NormalizeCompatibilityType(source);
    const std::string normalized_target = NormalizeCompatibilityType(target);
    if (normalized_source == normalized_target) return true;
    if (normalized_target == "java/lang/Object") return true;
    if (normalized_source == "android/app/Activity" &&
        normalized_target == "android/content/Context")
        return true;
    if ((normalized_source == "android/view/MotionEvent" ||
         normalized_source == "android/view/KeyEvent") &&
        normalized_target == "android/view/InputEvent")
        return true;
    if (normalized_source.starts_with('[') &&
        (normalized_target == "java/lang/Cloneable" ||
         normalized_target == "java/io/Serializable"))
        return true;
    return false;
}

bool IsSafeRelativeAssetPath(std::string_view path) {
    constexpr std::size_t kMaximumAssetPath = 32767;
    if (path.empty() || path.size() > kMaximumAssetPath ||
        path.front() == '/' || path.find('\\') != std::string_view::npos ||
        path.find(':') != std::string_view::npos)
        return false;
    std::size_t start = 0;
    while (start < path.size()) {
        const std::size_t end = path.find('/', start);
        const std::string_view component = path.substr(
            start, end == std::string_view::npos ? path.size() - start
                                                  : end - start);
        if (component.empty() || component == "." || component == "..")
            return false;
        if (end == std::string_view::npos) return true;
        start = end + 1;
    }
    return false;
}

std::uint64_t AllocateCompatibilityReference(
    GuestHeap* heap, CompatibilityReferenceKind kind, std::string_view type,
    std::string_view value, std::string* error) {
    if (heap == nullptr) {
        SetError(error, "compatibility reference heap is invalid");
        return 0;
    }
    const std::string normalized_type = NormalizeCompatibilityType(type);
    const std::string_view object_class =
        kind == CompatibilityReferenceKind::kClass
            ? std::string_view("java/lang/Class")
            : kind == CompatibilityReferenceKind::kString
                  ? std::string_view("java/lang/String")
                  : std::string_view(normalized_type);
    const std::string_view payload =
        kind == CompatibilityReferenceKind::kClass
            ? std::string_view(normalized_type)
            : kind == CompatibilityReferenceKind::kString ? value
                                                          : std::string_view{};
    const bool has_payload = kind != CompatibilityReferenceKind::kObject;
    const std::size_t total = kCompatibilityObjectSize + object_class.size() +
                              1 + (has_payload ? payload.size() + 1 : 0);
    const std::uint64_t allocation = heap->Callocate(1, total);
    if (allocation == 0) {
        SetError(error, "compatibility reference allocation failed");
        return 0;
    }
    const std::uint64_t class_address =
        allocation + kCompatibilityObjectSize;
    const std::uint64_t payload_address =
        has_payload ? class_address + object_class.size() + 1 : allocation;
    std::array<std::uint8_t, kCompatibilityObjectSize> object{};
    constexpr std::uint32_t kPinned = 1;
    std::memcpy(object.data(), &kCompatibilityObjectMagic,
                sizeof(kCompatibilityObjectMagic));
    std::memcpy(object.data() + 4, &kPinned, sizeof(kPinned));
    std::memcpy(object.data() + 8, &class_address, sizeof(class_address));
    std::memcpy(object.data() + 0x10, &payload_address,
                sizeof(payload_address));
    std::memcpy(object.data() + 0x38, &allocation, sizeof(allocation));
    const char terminator = '\0';
    const bool written =
        heap->Write(allocation, object.data(), object.size()) &&
        heap->Write(class_address, object_class.data(), object_class.size()) &&
        heap->Write(class_address + object_class.size(), &terminator, 1) &&
        (!has_payload ||
         (heap->Write(payload_address, payload.data(), payload.size()) &&
          heap->Write(payload_address + payload.size(), &terminator, 1)));
    if (written) return allocation;
    std::string ignored;
    (void)heap->Release(allocation, &ignored);
    SetError(error, "compatibility reference initialization failed");
    return 0;
}

std::uint64_t AllocateJniReference(
    GuestHeap* heap, bool compatibility, CompatibilityReferenceKind kind,
    std::string_view type, std::string_view value, std::string* error) {
    if (compatibility)
        return AllocateCompatibilityReference(heap, kind, type, value, error);
    if (heap == nullptr) {
        SetError(error, "guest JNI reference heap is invalid");
        return 0;
    }
    const std::uint64_t allocation = heap->Callocate(1, 8);
    if (allocation == 0)
        SetError(error, "guest JNI reference allocation failed");
    return allocation;
}

std::uint64_t JniResult(std::int32_t value) {
    return static_cast<std::uint32_t>(value);
}

bool IsSupportedVersion(std::uint64_t value) {
    const std::uint32_t version = static_cast<std::uint32_t>(value);
    return version == kJniVersion11 || version == kJniVersion12 ||
           version == kJniVersion14 || version == kJniVersion16;
}

bool IsImplementedEnvironmentSlot(std::size_t slot) {
    return slot == GuestJni::kGetVersionSlot ||
           slot == GuestJni::kFindClassSlot ||
           slot == GuestJni::kFromReflectedMethodSlot ||
           slot == GuestJni::kToReflectedMethodSlot ||
           slot == GuestJni::kGetStaticMethodIdSlot ||
           slot == GuestJni::kGetStaticFieldIdSlot ||
           slot == GuestJni::kGetStaticObjectFieldSlot ||
           slot == GuestJni::kGetStaticIntFieldSlot ||
           slot == GuestJni::kCallStaticObjectMethodSlot ||
           slot == GuestJni::kCallStaticObjectMethodVSlot ||
           slot == GuestJni::kCallStaticObjectMethodASlot ||
           slot == GuestJni::kCallStaticBooleanMethodSlot ||
           slot == GuestJni::kCallStaticBooleanMethodVSlot ||
           slot == GuestJni::kCallStaticBooleanMethodASlot ||
           slot == GuestJni::kCallStaticByteMethodSlot ||
           slot == GuestJni::kCallStaticByteMethodVSlot ||
           slot == GuestJni::kCallStaticByteMethodASlot ||
           slot == GuestJni::kCallStaticCharMethodSlot ||
           slot == GuestJni::kCallStaticCharMethodVSlot ||
           slot == GuestJni::kCallStaticCharMethodASlot ||
           slot == GuestJni::kCallStaticShortMethodSlot ||
           slot == GuestJni::kCallStaticShortMethodVSlot ||
           slot == GuestJni::kCallStaticShortMethodASlot ||
           slot == GuestJni::kCallStaticIntMethodSlot ||
           slot == GuestJni::kCallStaticIntMethodVSlot ||
           slot == GuestJni::kCallStaticIntMethodASlot ||
           slot == GuestJni::kCallStaticLongMethodSlot ||
           slot == GuestJni::kCallStaticLongMethodVSlot ||
           slot == GuestJni::kCallStaticLongMethodASlot ||
           slot == GuestJni::kCallStaticVoidMethodSlot ||
           slot == GuestJni::kCallStaticVoidMethodVSlot ||
           slot == GuestJni::kCallStaticVoidMethodASlot ||
           slot == GuestJni::kGetMethodIdSlot ||
           slot == GuestJni::kGetFieldIdSlot ||
           slot == GuestJni::kGetObjectFieldSlot ||
           slot == GuestJni::kGetIntFieldSlot ||
           slot == GuestJni::kGetLongFieldSlot ||
           slot == GuestJni::kGetFloatFieldSlot ||
           slot == GuestJni::kGetDoubleFieldSlot ||
           slot == GuestJni::kNewObjectSlot ||
           slot == GuestJni::kNewObjectVSlot ||
           slot == GuestJni::kNewObjectASlot ||
           slot == GuestJni::kNewStringSlot ||
           slot == GuestJni::kGetStringLengthSlot ||
           slot == GuestJni::kGetStringCharsSlot ||
           slot == GuestJni::kReleaseStringCharsSlot ||
           slot == GuestJni::kNewStringUtfSlot ||
           slot == GuestJni::kGetStringUtfLengthSlot ||
           slot == GuestJni::kGetStringUtfCharsSlot ||
           slot == GuestJni::kReleaseStringUtfCharsSlot ||
           slot == GuestJni::kGetArrayLengthSlot ||
           slot == GuestJni::kNewObjectArraySlot ||
           slot == GuestJni::kGetObjectArrayElementSlot ||
           slot == GuestJni::kSetObjectArrayElementSlot ||
           slot == GuestJni::kNewByteArraySlot ||
           slot == GuestJni::kNewIntArraySlot ||
           slot == GuestJni::kNewLongArraySlot ||
           slot == GuestJni::kGetPrimitiveArrayCriticalSlot ||
           slot == GuestJni::kReleasePrimitiveArrayCriticalSlot ||
           slot == GuestJni::kGetByteArrayElementsSlot ||
           slot == GuestJni::kGetIntArrayElementsSlot ||
           slot == GuestJni::kGetLongArrayElementsSlot ||
           slot == GuestJni::kReleaseByteArrayElementsSlot ||
           slot == GuestJni::kReleaseIntArrayElementsSlot ||
           slot == GuestJni::kReleaseLongArrayElementsSlot ||
           slot == GuestJni::kGetByteArrayRegionSlot ||
           slot == GuestJni::kGetIntArrayRegionSlot ||
           slot == GuestJni::kGetLongArrayRegionSlot ||
           slot == GuestJni::kSetByteArrayRegionSlot ||
           slot == GuestJni::kSetIntArrayRegionSlot ||
           slot == GuestJni::kSetLongArrayRegionSlot ||
           slot == GuestJni::kCallObjectMethodSlot ||
           slot == GuestJni::kCallObjectMethodVSlot ||
           slot == GuestJni::kCallObjectMethodASlot ||
           slot == GuestJni::kCallBooleanMethodSlot ||
           slot == GuestJni::kCallBooleanMethodVSlot ||
           slot == GuestJni::kCallBooleanMethodASlot ||
           slot == GuestJni::kCallByteMethodSlot ||
           slot == GuestJni::kCallByteMethodVSlot ||
           slot == GuestJni::kCallByteMethodASlot ||
           slot == GuestJni::kCallCharMethodSlot ||
           slot == GuestJni::kCallCharMethodVSlot ||
           slot == GuestJni::kCallCharMethodASlot ||
           slot == GuestJni::kCallShortMethodSlot ||
           slot == GuestJni::kCallShortMethodVSlot ||
           slot == GuestJni::kCallShortMethodASlot ||
           slot == GuestJni::kCallIntMethodSlot ||
           slot == GuestJni::kCallIntMethodVSlot ||
           slot == GuestJni::kCallIntMethodASlot ||
           slot == GuestJni::kCallLongMethodSlot ||
           slot == GuestJni::kCallLongMethodVSlot ||
           slot == GuestJni::kCallLongMethodASlot ||
           slot == GuestJni::kCallFloatMethodSlot ||
           slot == GuestJni::kCallFloatMethodVSlot ||
           slot == GuestJni::kCallFloatMethodASlot ||
           slot == GuestJni::kCallDoubleMethodSlot ||
           slot == GuestJni::kCallDoubleMethodVSlot ||
           slot == GuestJni::kCallDoubleMethodASlot ||
           slot == GuestJni::kCallVoidMethodSlot ||
           slot == GuestJni::kCallVoidMethodVSlot ||
           slot == GuestJni::kCallVoidMethodASlot ||
           slot == GuestJni::kDeleteLocalRefSlot ||
           slot == GuestJni::kIsSameObjectSlot ||
           slot == GuestJni::kNewLocalRefSlot ||
           slot == GuestJni::kGetObjectClassSlot ||
           slot == GuestJni::kIsInstanceOfSlot ||
           slot == GuestJni::kNewGlobalRefSlot ||
           slot == GuestJni::kDeleteGlobalRefSlot ||
           slot == GuestJni::kNewWeakGlobalRefSlot ||
           slot == GuestJni::kDeleteWeakGlobalRefSlot ||
           slot == GuestJni::kExceptionOccurredSlot ||
           slot == GuestJni::kExceptionClearSlot ||
           slot == GuestJni::kThrowSlot ||
           slot == GuestJni::kThrowNewSlot ||
           slot == GuestJni::kFatalErrorSlot ||
           slot == GuestJni::kPushLocalFrameSlot ||
           slot == GuestJni::kPopLocalFrameSlot ||
           slot == GuestJni::kRegisterNativesSlot ||
           slot == GuestJni::kUnregisterNativesSlot ||
           slot == GuestJni::kGetJavaVmSlot ||
           slot == GuestJni::kExceptionCheckSlot;
}

std::string UnsupportedEnvironmentName(std::size_t slot) {
    return "__isomr_jni_unsupported_" + std::to_string(slot);
}

// ISOMR_JNI_TRACE=1 logs each JNI env call to stderr so a protected loader's
// boot sequence (FindClass/GetMethodID/RegisterNatives/...) is visible.
bool JniTraceEnabled() {
    static const bool enabled = [] {
        wchar_t flag[8] = {};
        return GetEnvironmentVariableW(L"ISOMR_JNI_TRACE", flag, 8) > 0 &&
               flag[0] != L'0';
    }();
    return enabled;
}

bool ReadGuestText(GuestAddressSpace* memory, std::uint64_t address,
                   std::string_view kind, std::string* text,
                   std::string* error) {
    std::uint64_t length = 0;
    if (memory == nullptr || text == nullptr || address == 0 ||
        !memory->BoundedStringLength(address, 1025, &length, error) ||
        length == 0 || length > 1024) {
        SetError(error, "guest JNI " + std::string(kind) +
                            " is invalid or too long");
        return false;
    }
    text->assign(static_cast<std::size_t>(length), '\0');
    if (memory->Read(address, text->data(), text->size())) return true;
    SetError(error, "guest JNI " + std::string(kind) +
                        " could not be read");
    return false;
}

bool ReadGuestStringValue(GuestAddressSpace* memory, std::uint64_t address,
                          std::string* value, std::string* error) {
    std::uint64_t length = 0;
    if (memory == nullptr || value == nullptr || address == 0 ||
        !memory->BoundedStringLength(address, 4097, &length, error) ||
        length > 4096) {
        SetError(error,
                 "guest JNI NewStringUTF value is invalid or too long");
        return false;
    }
    value->assign(static_cast<std::size_t>(length), '\0');
    if (length == 0 ||
        memory->Read(address, value->data(), value->size()))
        return true;
    SetError(error, "guest JNI NewStringUTF value could not be read");
    return false;
}

bool DecodeModifiedUtf8(std::string_view input,
                        std::vector<std::uint16_t>* output,
                        std::string* error) {
    if (output == nullptr) {
        SetError(error, "guest JNI UTF-16 output is invalid");
        return false;
    }
    output->clear();
    for (std::size_t cursor = 0; cursor < input.size();) {
        const std::uint8_t first =
            static_cast<std::uint8_t>(input[cursor++]);
        std::uint32_t code_point = 0;
        std::size_t continuation_count = 0;
        if (first >= 0x01U && first <= 0x7fU) {
            code_point = first;
        } else if ((first & 0xe0U) == 0xc0U) {
            code_point = first & 0x1fU;
            continuation_count = 1;
        } else if ((first & 0xf0U) == 0xe0U) {
            code_point = first & 0x0fU;
            continuation_count = 2;
        } else if ((first & 0xf8U) == 0xf0U) {
            code_point = first & 0x07U;
            continuation_count = 3;
        } else {
            SetError(error, "guest JNI string contains malformed modified UTF-8");
            return false;
        }
        if (continuation_count != 0) {
            if (input.size() - cursor < continuation_count) {
                SetError(error,
                         "guest JNI string contains truncated modified UTF-8");
                return false;
            }
            for (std::size_t index = 0; index < continuation_count; ++index) {
                const std::uint8_t next =
                    static_cast<std::uint8_t>(input[cursor++]);
                if ((next & 0xc0U) != 0x80U) {
                    SetError(error,
                             "guest JNI string contains malformed modified UTF-8");
                    return false;
                }
                code_point = (code_point << 6U) | (next & 0x3fU);
            }
            const bool modified_null =
                continuation_count == 1 && first == 0xc0U &&
                code_point == 0;
            if ((!modified_null && continuation_count == 1 &&
                 code_point < 0x80U) ||
                (continuation_count == 2 && code_point < 0x800U) ||
                (continuation_count == 3 && code_point < 0x10000U)) {
                SetError(error,
                         "guest JNI string contains overlong modified UTF-8");
                return false;
            }
        }
        if (code_point > 0x10ffffU) {
            SetError(error,
                     "guest JNI string contains an invalid Unicode value");
            return false;
        }
        if (code_point <= 0xffffU) {
            output->push_back(static_cast<std::uint16_t>(code_point));
        } else {
            code_point -= 0x10000U;
            output->push_back(static_cast<std::uint16_t>(
                0xd800U + (code_point >> 10U)));
            output->push_back(static_cast<std::uint16_t>(
                0xdc00U + (code_point & 0x3ffU)));
        }
        if (output->size() > 4096) {
            SetError(error, "guest JNI UTF-16 string is too long");
            return false;
        }
    }
    return true;
}

std::string EncodeModifiedUtf8(
    const std::vector<std::uint16_t>& characters) {
    std::string output;
    output.reserve(characters.size() * 3U);
    for (const std::uint16_t character : characters) {
        if (character == 0) {
            output.push_back(static_cast<char>(0xc0));
            output.push_back(static_cast<char>(0x80));
        } else if (character <= 0x7fU) {
            output.push_back(static_cast<char>(character));
        } else if (character <= 0x7ffU) {
            output.push_back(static_cast<char>(0xc0U | (character >> 6U)));
            output.push_back(
                static_cast<char>(0x80U | (character & 0x3fU)));
        } else {
            output.push_back(static_cast<char>(0xe0U | (character >> 12U)));
            output.push_back(static_cast<char>(
                0x80U | ((character >> 6U) & 0x3fU)));
            output.push_back(
                static_cast<char>(0x80U | (character & 0x3fU)));
        }
    }
    return output;
}

bool IsBoundedMetadata(std::string_view value, std::size_t maximum) {
    return !value.empty() && value.size() <= maximum &&
           value.find('\0') == std::string_view::npos;
}

std::string ReturnType(std::string_view signature) {
    const std::size_t close = signature.find(')');
    return close != std::string_view::npos && close + 1 < signature.size()
               ? std::string(signature.substr(close + 1))
               : std::string();
}

bool IsFieldSignature(std::string_view signature) {
    if (signature.size() == 1 &&
        std::string_view("ZBCSIJFD").find(signature.front()) !=
            std::string_view::npos)
        return true;
    if (signature.size() >= 3 && signature.front() == 'L' &&
        signature.back() == ';')
        return true;
    return signature.size() >= 2 && signature.front() == '[';
}

bool ConsumeJniType(std::string_view signature, std::size_t* cursor,
                    bool allow_void) {
    if (cursor == nullptr || *cursor >= signature.size()) return false;
    const char type = signature[*cursor];
    if (type == 'V') {
        ++*cursor;
        return allow_void;
    }
    if (std::string_view("ZBCSIJFD").find(type) != std::string_view::npos) {
        ++*cursor;
        return true;
    }
    if (type == '[') {
        do {
            ++*cursor;
        } while (*cursor < signature.size() && signature[*cursor] == '[');
        return ConsumeJniType(signature, cursor, false);
    }
    if (type != 'L') return false;
    const std::size_t first = ++*cursor;
    const std::size_t end = signature.find(';', first);
    if (end == std::string_view::npos || end == first) return false;
    *cursor = end + 1;
    return true;
}

bool IsMethodSignature(std::string_view signature) {
    if (signature.size() < 3 || signature.front() != '(') return false;
    std::size_t cursor = 1;
    while (cursor < signature.size() && signature[cursor] != ')') {
        if (!ConsumeJniType(signature, &cursor, false)) return false;
    }
    if (cursor >= signature.size() || signature[cursor] != ')') return false;
    ++cursor;
    return ConsumeJniType(signature, &cursor, true) &&
           cursor == signature.size();
}

struct Aarch64VaList {
    std::uint64_t stack;
    std::uint64_t general_top;
    std::uint64_t vector_top;
    std::int32_t general_offset;
    std::int32_t vector_offset;
};
static_assert(sizeof(Aarch64VaList) == 32);

bool ReadGuestVaList(GuestAddressSpace* memory, std::uint64_t list_address,
                     Aarch64VaList* list, std::string* error) {
    if (memory == nullptr || list == nullptr || list_address == 0 ||
        !memory->Read(list_address, list, sizeof(*list))) {
        SetError(error, "guest JNI could not read the AArch64 va_list");
        return false;
    }
    if (list->general_offset < -64 || list->general_offset > 0 ||
        (list->general_offset % 8) != 0 ||
        list->vector_offset < -128 || list->vector_offset > 0 ||
        (list->vector_offset % 16) != 0) {
        SetError(error, "guest JNI AArch64 va_list is malformed");
        return false;
    }
    return true;
}

bool ReadGuestGeneralArgument(GuestAddressSpace* memory,
                              std::uint64_t list_address,
                              std::uint64_t* value,
                              std::string* error,
                              std::size_t argument_index = 0) {
    Aarch64VaList list{};
    if (value == nullptr ||
        !ReadGuestVaList(memory, list_address, &list, error))
        return false;
    std::uint64_t source = 0;
    const std::size_t saved_arguments =
        list.general_offset < 0
            ? static_cast<std::size_t>(-list.general_offset) / 8
            : 0;
    if (argument_index < saved_arguments) {
        const std::uint64_t distance = static_cast<std::uint64_t>(
            -list.general_offset -
            static_cast<std::int32_t>(argument_index * 8));
        if (list.general_top < distance) {
            SetError(error,
                     "guest JNI AArch64 general save area underflowed");
            return false;
        }
        source = list.general_top - distance;
    } else {
        const std::size_t stack_index = argument_index - saved_arguments;
        if (list.stack > std::numeric_limits<std::uint64_t>::max() - 7U) {
            SetError(error, "guest JNI AArch64 argument stack overflowed");
            return false;
        }
        source = (list.stack + 7U) & ~7ULL;
        if (stack_index >
            (std::numeric_limits<std::uint64_t>::max() - source) / 8U) {
            SetError(error, "guest JNI AArch64 argument stack overflowed");
            return false;
        }
        source += stack_index * 8U;
    }
    if (source == 0 || !memory->Read(source, value, sizeof(*value))) {
        SetError(error,
                 "guest JNI could not read an AArch64 general argument");
        return false;
    }
    return true;
}

}  // namespace

GuestJni::GuestJni(GuestHeap* heap, GuestAddressSpace* address_space,
                   BionicThreadState* thread_state, BionicFileSystem* files,
                   GuestJniBackend* backend)
    : heap_(heap), address_space_(address_space),
      thread_state_(thread_state), files_(files), backend_(backend) {}

GuestJni::~GuestJni() = default;

bool GuestJni::ConfigureCompatibility(GuestJniCompatibilityConfig config,
                                      std::string* error) {
    const std::lock_guard lock(mutex_);
    if (initialization_attempted_ || initialized_) {
        SetError(error,
                 "guest JNI compatibility must be configured before initialization");
        return false;
    }
    const bool has_package_name = !config.package_name.empty();
    const bool has_package_code_path = !config.package_code_path.empty();
    const bool has_package_version = !config.package_version.empty();
    const bool has_any_package =
        has_package_name || has_package_code_path || has_package_version;
    const bool has_complete_package =
        has_package_name && has_package_code_path && has_package_version;
    if ((has_any_package && !has_complete_package) ||
        (has_complete_package &&
         (!IsBoundedMetadata(config.package_name, 1024) ||
          !IsBoundedMetadata(config.package_code_path, 4096) ||
          !IsBoundedMetadata(config.package_version, 1024))) ||
        (!config.splash_removal_class.empty() &&
         !IsBoundedMetadata(config.splash_removal_class, 1024))) {
        SetError(error,
                 "guest JNI compatibility configuration is incomplete or invalid");
        return false;
    }
    compatibility_ = std::move(config);
    return true;
}

GuestJni::PendingExceptionState& GuestJni::exception_pending() {
    thread_local PendingExceptionState state;
    return state;
}

bool GuestJni::RegisterImports(Aarch64ImportRegistry* imports,
                               std::string* error) {
    if (registration_attempted_) {
        SetError(error, "guest JNI import registration already attempted");
        return false;
    }
    registration_attempted_ = true;
    if (imports == nullptr || heap_ == nullptr || address_space_ == nullptr ||
        thread_state_ == nullptr || files_ == nullptr) {
        SetError(error, "guest JNI configuration is invalid");
        return false;
    }

    const auto get_version =
        [this](Aarch64ImportRegistry::Registers* registers,
               std::string* call_error) {
            if (registers == nullptr ||
                !IsCurrentEnvironment((*registers)[0])) {
                SetError(call_error, "guest JNI environment is invalid");
                return false;
            }
            if (backend_ != nullptr && backend_->GetVersion() == 0) {
                SetError(call_error, "desktop JVM version is unavailable");
                return false;
            }
            // Keep the Android-facing JNI contract stable even when HotSpot
            // supports a newer desktop JNI revision.
            (*registers)[0] = kJniVersion16;
            return true;
        };
    const auto find_class =
        [this](Aarch64ImportRegistry::Registers* registers,
               std::string* call_error) {
            if (registers == nullptr ||
                !IsCurrentEnvironment((*registers)[0]) ||
                (*registers)[1] == 0) {
                SetError(call_error,
                         "guest JNI FindClass arguments are invalid");
                return false;
            }
            std::string name;
            if (!ReadGuestText(address_space_, (*registers)[1],
                               "class name", &name, call_error))
                return false;
            if (JniTraceEnabled())
                std::fprintf(stderr, "ISOMR_JNI_TRACE FindClass name=%s\n",
                             name.c_str());
            GuestJniBackend::Handle backend_reference = 0;
            if (backend_ != nullptr &&
                !backend_->FindClass(name, &backend_reference, call_error))
                return false;
            if (backend_ != nullptr && backend_reference == 0) {
                (*registers)[0] = 0;
                return true;
            }
            const std::uint64_t reference = AllocateJniReference(
                heap_, backend_ == nullptr, CompatibilityReferenceKind::kClass,
                name, {}, call_error);
            if (reference == 0) {
                if (backend_reference != 0) {
                    std::string ignored;
                    (void)backend_->DeleteLocalRef(backend_reference,
                                                   &ignored);
                }
                SetError(call_error,
                         "guest JNI class reference allocation failed");
                return false;
            }
            const std::lock_guard lock(mutex_);
            class_references_.emplace(
                reference,
                ClassReference{reference, std::move(name),
                               backend_reference});
            TrackLocalReferenceLocked(reference);
            (*registers)[0] = reference;
            return true;
        };
    const auto register_natives =
        [this](Aarch64ImportRegistry::Registers* registers,
               std::string* call_error) {
            if (registers == nullptr ||
                !IsCurrentEnvironment((*registers)[0])) {
                SetError(call_error,
                         "guest JNI RegisterNatives environment is invalid");
                return false;
            }
            std::string class_type;
            {
                const std::lock_guard lock(mutex_);
                if (backend_ != nullptr) {
                    SetError(call_error,
                             "guest JNI RegisterNatives cannot expose AArch64 "
                             "targets to the desktop JVM");
                    return false;
                }
                const std::optional<std::string_view> type =
                    ClassTypeLocked((*registers)[1]);
                if (!type || !IsClassReferenceLocked((*registers)[1])) {
                    SetError(call_error,
                             "guest JNI RegisterNatives class is invalid");
                    return false;
                }
                class_type.assign(*type);
            }

            const std::int32_t count = static_cast<std::int32_t>(
                static_cast<std::uint32_t>((*registers)[3]));
            if (count < 0 ||
                static_cast<std::uint32_t>(count) >
                    kMaximumRegisteredNativeMethods) {
                (*registers)[0] = JniResult(kJniError);
                return true;
            }
            if (count == 0) {
                (*registers)[0] = JniResult(kJniOk);
                return true;
            }
            if ((*registers)[2] == 0) {
                SetError(call_error,
                         "guest JNI RegisterNatives method array is null");
                return false;
            }

            std::vector<RegisteredNativeMethod> staged;
            try {
                staged.reserve(static_cast<std::size_t>(count));
            } catch (const std::bad_alloc&) {
                SetError(call_error,
                         "guest JNI RegisterNatives staging allocation failed");
                return false;
            }
            for (std::int32_t index = 0; index < count; ++index) {
                const std::size_t offset =
                    static_cast<std::size_t>(index) *
                    sizeof(GuestJniNativeMethod);
                if ((*registers)[2] >
                    std::numeric_limits<std::uint64_t>::max() - offset) {
                    SetError(call_error,
                             "guest JNI RegisterNatives method array overflowed");
                    return false;
                }
                GuestJniNativeMethod method{};
                if (!address_space_->Read((*registers)[2] + offset, &method,
                                          sizeof(method))) {
                    SetError(call_error,
                             "guest JNI RegisterNatives method array is unreadable");
                    return false;
                }
                std::string name;
                std::string signature;
                if (!ReadGuestText(address_space_, method.name,
                                   "native method name", &name, call_error) ||
                    !ReadGuestText(address_space_, method.signature,
                                   "native method signature", &signature,
                                   call_error))
                    return false;
                if (method.function == 0 || !IsMethodSignature(signature)) {
                    (*registers)[0] = JniResult(kJniError);
                    return true;
                }
                const bool duplicate = std::any_of(
                    staged.begin(), staged.end(),
                    [&](const RegisteredNativeMethod& candidate) {
                        return candidate.name == name &&
                               candidate.signature == signature;
                    });
                if (duplicate) {
                    (*registers)[0] = JniResult(kJniError);
                    return true;
                }
                staged.push_back(RegisteredNativeMethod{
                    {}, class_type, std::move(name), std::move(signature),
                    method.function});
            }

            const std::lock_guard lock(mutex_);
            const std::optional<std::string_view> current_type =
                ClassTypeLocked((*registers)[1]);
            if (!current_type || *current_type != class_type ||
                !IsClassReferenceLocked((*registers)[1])) {
                SetError(call_error,
                         "guest JNI RegisterNatives class became invalid");
                return false;
            }
            std::size_t additions = 0;
            for (const RegisteredNativeMethod& method : staged) {
                const auto existing = std::find_if(
                    registered_native_methods_.begin(),
                    registered_native_methods_.end(),
                    [&](const RegisteredNativeMethod& candidate) {
                        return candidate.class_type == method.class_type &&
                               candidate.name == method.name &&
                               candidate.signature == method.signature;
                    });
                if (existing == registered_native_methods_.end()) ++additions;
            }
            if (additions > kMaximumRegisteredNativeMethods -
                                registered_native_methods_.size()) {
                (*registers)[0] = JniResult(kJniError);
                return true;
            }
            std::vector<RegisteredNativeMethod> updated;
            try {
                updated = registered_native_methods_;
                updated.reserve(updated.size() + additions);
            } catch (const std::bad_alloc&) {
                SetError(call_error,
                         "guest JNI RegisterNatives registry allocation failed");
                return false;
            }
            std::uint64_t next_id = next_registered_native_id_;
            for (RegisteredNativeMethod& method : staged) {
                const auto existing = std::find_if(
                    updated.begin(), updated.end(),
                    [&](const RegisteredNativeMethod& candidate) {
                        return candidate.class_type == method.class_type &&
                               candidate.name == method.name &&
                               candidate.signature == method.signature;
                    });
                if (existing != updated.end()) {
                    if (existing->handle.generation ==
                        std::numeric_limits<std::uint64_t>::max()) {
                        (*registers)[0] = JniResult(kJniError);
                        return true;
                    }
                    existing->function = method.function;
                    ++existing->handle.generation;
                    continue;
                }
                if (next_id == 0 ||
                    next_id == std::numeric_limits<std::uint64_t>::max()) {
                    (*registers)[0] = JniResult(kJniError);
                    return true;
                }
                method.handle = RegisteredNativeHandle{next_id++, 1};
                updated.push_back(std::move(method));
            }
            registered_native_methods_.swap(updated);
            next_registered_native_id_ = next_id;
            (*registers)[0] = JniResult(kJniOk);
            return true;
        };
    const auto from_reflected_method =
        [this](Aarch64ImportRegistry::Registers* registers,
               std::string* call_error) {
            if (registers == nullptr ||
                !IsCurrentEnvironment((*registers)[0])) {
                SetError(call_error,
                         "guest JNI FromReflectedMethod environment is invalid");
                return false;
            }
            const std::lock_guard lock(mutex_);
            // A null Method object yields a null jmethodID on ART rather than
            // a crash; il2cpp reflection caching probes call this on stub
            // Method refs.
            if ((*registers)[1] == 0) {
                (*registers)[0] = 0;
                return true;
            }
            std::uint64_t method_reference = 0;
            bool is_field = false;
            const auto local =
                FindOwnedLocalObjectReferenceLocked((*registers)[1]);
            const auto global =
                global_object_references_.find((*registers)[1]);
            const std::string_view reflected_type =
                local != object_references_.end()
                    ? local->second.type
                    : (global != global_object_references_.end()
                           ? global->second.type
                           : std::string_view{});
            const std::string_view normalized =
                NormalizeCompatibilityType(reflected_type);
            if (normalized == "java/lang/reflect/Method" ||
                normalized == "java/lang/reflect/Constructor") {
                method_reference =
                    local != object_references_.end()
                        ? local->second.method_reference
                        : global->second.method_reference;
            } else if (normalized == "java/lang/reflect/Field") {
                is_field = true;
                method_reference =
                    local != object_references_.end()
                        ? local->second.method_reference
                        : global->second.method_reference;
            }
            const bool valid =
                method_reference != 0 &&
                (is_field ? field_references_.contains(method_reference)
                          : method_references_.contains(method_reference));
            if (!valid) {
                char detail[160];
                std::snprintf(
                    detail, sizeof(detail),
                    "guest JNI FromReflectedMethod object is invalid "
                    "ref=0x%llx type=%.*s method-ref=0x%llx",
                    static_cast<unsigned long long>((*registers)[1]),
                    static_cast<int>(normalized.size()), normalized.data(),
                    static_cast<unsigned long long>(method_reference));
                SetError(call_error, detail);
                return false;
            }
            (*registers)[0] = method_reference;
            return true;
        };
    const auto to_reflected_method =
        [this](Aarch64ImportRegistry::Registers* registers,
               std::string* call_error) {
            if (registers == nullptr ||
                !IsCurrentEnvironment((*registers)[0])) {
                SetError(call_error,
                         "guest JNI ToReflectedMethod environment is invalid");
                return false;
            }
            const std::lock_guard lock(mutex_);
            // Null jmethodID input yields a null Method object on ART.
            if ((*registers)[2] == 0) {
                (*registers)[0] = 0;
                return true;
            }
            const std::optional<std::string_view> class_type =
                ClassTypeLocked((*registers)[1]);
            const auto method = method_references_.find((*registers)[2]);
            const bool requested_static = (*registers)[3] != 0;
            if (!class_type || !IsClassReferenceLocked((*registers)[1]) ||
                method == method_references_.end() ||
                method->second.class_type != *class_type ||
                method->second.is_static != requested_static) {
                SetError(call_error,
                         "guest JNI ToReflectedMethod arguments are invalid");
                return false;
            }
            constexpr std::string_view kMethodType =
                "Ljava/lang/reflect/Method;";
            const std::uint64_t reference = AllocateCompatibilityReference(
                heap_, CompatibilityReferenceKind::kObject, kMethodType, {},
                call_error);
            if (reference == 0) return false;
            object_references_.emplace(
                reference,
                ObjectReference{reference, (*registers)[2],
                                std::string(kMethodType), {}, 0});
            TrackLocalReferenceLocked(reference);
            (*registers)[0] = reference;
            return true;
        };
    const auto unregister_natives =
        [this](Aarch64ImportRegistry::Registers* registers,
               std::string* call_error) {
            if (registers == nullptr ||
                !IsCurrentEnvironment((*registers)[0])) {
                SetError(call_error,
                         "guest JNI UnregisterNatives environment is invalid");
                return false;
            }
            const std::lock_guard lock(mutex_);
            if (backend_ != nullptr) {
                SetError(call_error,
                         "guest JNI UnregisterNatives cannot manage AArch64 "
                         "targets through the desktop JVM");
                return false;
            }
            const std::optional<std::string_view> class_type =
                ClassTypeLocked((*registers)[1]);
            if (!class_type || !IsClassReferenceLocked((*registers)[1])) {
                SetError(call_error,
                         "guest JNI UnregisterNatives class is invalid");
                return false;
            }
            std::erase_if(
                registered_native_methods_,
                [&](const RegisteredNativeMethod& method) {
                    return method.class_type == *class_type;
                });
            (*registers)[0] = JniResult(kJniOk);
            return true;
        };
    const auto get_static_method_id =
        [this](Aarch64ImportRegistry::Registers* registers,
               std::string* call_error) {
            if (registers == nullptr ||
                !IsCurrentEnvironment((*registers)[0])) {
                SetError(call_error,
                         "guest JNI GetStaticMethodID environment is invalid");
                return false;
            }
            std::string name;
            std::string signature;
            if (!ReadGuestText(address_space_, (*registers)[2],
                               "method name", &name, call_error) ||
                !ReadGuestText(address_space_, (*registers)[3],
                               "method signature", &signature,
                               call_error))
                return false;
            const std::lock_guard lock(mutex_);
            if (!IsClassReferenceLocked((*registers)[1]) &&
                !RecoverClassReferenceLocked((*registers)[1])) {
                char detail[192];
                std::snprintf(detail, sizeof(detail),
                              "guest JNI GetStaticMethodID class is invalid "
                              "class=0x%llx name=%s sig=%s",
                              static_cast<unsigned long long>(
                                  (*registers)[1]),
                              name.c_str(), signature.c_str());
                SetError(call_error, detail);
                return false;
            }
            const std::optional<std::uint64_t> class_identity =
                ReferenceIdentityLocked((*registers)[1]);
            const std::optional<std::string_view> class_type =
                ClassTypeLocked((*registers)[1]);
            if (!class_identity || !class_type) return false;
            // Stable jmethodID per declaring class/name/signature — see the
            // matching dedup in GetMethodID for the JNIBridge proxy dispatch.
            for (const auto& [candidate, method] : method_references_) {
                if (method.is_static && method.name == name &&
                    method.signature == signature &&
                    ReceiverClassMatches(method.class_type, *class_type)) {
                    (*registers)[0] = candidate;
                    return true;
                }
            }
            GuestJniBackend::Handle backend_method = 0;
            if (backend_ != nullptr) {
                const std::optional<GuestJniBackend::Handle> backend_class =
                    BackendReferenceLocked((*registers)[1]);
                if (!backend_class ||
                    !backend_->GetMethodId(*backend_class, name, signature,
                                           true, &backend_method,
                                           call_error))
                    return false;
                if (backend_method == 0) {
                    (*registers)[0] = 0;
                    return true;
                }
            }
            const std::uint64_t reference = heap_->Callocate(1, 8);
            if (reference == 0) {
                if (backend_method != 0) {
                    std::string ignored;
                    (void)backend_->DeleteMethod(backend_method, &ignored);
                }
                SetError(call_error,
                         "guest JNI method reference allocation failed");
                return false;
            }
            method_references_.emplace(
                reference,
                MethodReference{*class_identity, std::string(*class_type),
                                std::move(name),
                                std::move(signature), true,
                                backend_method});
            (*registers)[0] = reference;
            return true;
        };
    const auto get_static_field_id =
        [this](Aarch64ImportRegistry::Registers* registers,
               std::string* call_error) {
            if (registers == nullptr ||
                !IsCurrentEnvironment((*registers)[0])) {
                SetError(call_error,
                         "guest JNI GetStaticFieldID environment is invalid");
                return false;
            }
            if (backend_ != nullptr) {
                SetError(call_error,
                         "guest JNI GetStaticFieldID is unavailable with an active backend");
                return false;
            }
            std::string name;
            std::string signature;
            if (!ReadGuestText(address_space_, (*registers)[2],
                               "field name", &name, call_error) ||
                !ReadGuestText(address_space_, (*registers)[3],
                               "field signature", &signature,
                               call_error) ||
                !IsFieldSignature(signature)) {
                if (call_error == nullptr || call_error->empty())
                    SetError(call_error,
                             "guest JNI GetStaticFieldID signature is invalid");
                return false;
            }
            const std::lock_guard lock(mutex_);
            std::optional<std::uint64_t> class_identity =
                ReferenceIdentityLocked((*registers)[1]);
            std::optional<std::string_view> class_type =
                ClassTypeLocked((*registers)[1]);
            if ((!class_identity || !class_type ||
                 !IsClassReferenceLocked((*registers)[1])) &&
                !RecoverClassReferenceLocked((*registers)[1])) {
                char detail[192];
                std::snprintf(detail, sizeof(detail),
                              "guest JNI GetStaticFieldID class is invalid "
                              "class=0x%llx name=%s sig=%s",
                              static_cast<unsigned long long>(
                                  (*registers)[1]),
                              name.c_str(), signature.c_str());
                SetError(call_error, detail);
                return false;
            }
            if (!class_identity)
                class_identity = ReferenceIdentityLocked((*registers)[1]);
            if (!class_type) class_type = ClassTypeLocked((*registers)[1]);
            if (!class_identity || !class_type) return false;
            const std::uint64_t reference = heap_->Callocate(1, 8);
            if (reference == 0) {
                SetError(call_error,
                         "guest JNI field reference allocation failed");
                return false;
            }
            field_references_.emplace(
                reference,
                FieldReference{*class_identity, std::string(*class_type),
                               std::move(name), std::move(signature), true});
            (*registers)[0] = reference;
            return true;
        };
    const auto get_static_int_field =
        [this](Aarch64ImportRegistry::Registers* registers,
               std::string* call_error) {
            if (registers == nullptr ||
                !IsCurrentEnvironment((*registers)[0])) {
                SetError(call_error,
                         "guest JNI GetStaticIntField environment is invalid");
                return false;
            }
            if (backend_ != nullptr) {
                SetError(call_error,
                         "guest JNI GetStaticIntField is unavailable with an active backend");
                return false;
            }
            const std::lock_guard lock(mutex_);
            if (!IsClassReferenceLocked((*registers)[1]) &&
                !RecoverClassReferenceLocked((*registers)[1])) {
                SetError(call_error,
                         "guest JNI GetStaticIntField class is invalid");
                return false;
            }
            const auto field = field_references_.find((*registers)[2]);
            // ART does not compare the jclass argument against the field's
            // declaring class — the jfieldID already encodes it.
            if (field == field_references_.end() ||
                !field->second.is_static) {
                SetError(call_error,
                         "guest JNI GetStaticIntField provenance is invalid");
                return false;
            }
            if (field->second.class_type ==
                    "android/os/Build$VERSION" &&
                field->second.signature == "I") {
                // android/os/Build$VERSION ints. SDK_INT is the platform API
                // level; PREVIEW_SDK_INT/BASE_OS-derived codes are 0 on a
                // stable (non-preview) release.
                static const std::unordered_map<
                    std::string_view, std::uint64_t> kVersionInts = {
                    {"SDK_INT", 35},
                    {"PREVIEW_SDK_INT", 0}};
                const auto version =
                    kVersionInts.find(field->second.name);
                if (version != kVersionInts.end()) {
                    (*registers)[0] = version->second;
                    return true;
                }
            }
            if (field->second.class_type == "android/content/Context" &&
                field->second.name == "MODE_PRIVATE" &&
                field->second.signature == "I") {
                (*registers)[0] = 0;
                return true;
            }
            if (field->second.class_type == "android/view/View" &&
                field->second.signature == "I") {
                // android.view.View system-UI visibility flags (AOSP values).
                static const std::unordered_map<
                    std::string_view, std::uint64_t> kViewFlags = {
                    {"SYSTEM_UI_FLAG_LOW_PROFILE", 0x1},
                    {"SYSTEM_UI_FLAG_HIDE_NAVIGATION", 0x2},
                    {"SYSTEM_UI_FLAG_FULLSCREEN", 0x4},
                    {"SYSTEM_UI_FLAG_LAYOUT_STABLE", 0x100},
                    {"SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION", 0x200},
                    {"SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN", 0x400},
                    {"SYSTEM_UI_FLAG_IMMERSIVE", 0x800},
                    {"SYSTEM_UI_FLAG_IMMERSIVE_STICKY", 0x1000},
                    {"SYSTEM_UI_FLAG_VISIBLE", 0x0},
                    {"SYSTEM_UI_LAYOUT_FLAGS", 0x600},
                    {"KEEP_SCREEN_ON", 0x80},
                    {"STATUS_BAR_VISIBLE", 0x0},
                    {"STATUS_BAR_HIDDEN", 0x1},
                    {"FOCUSABLE", 0x1},
                    {"NOT_FOCUSABLE", 0x8},
                    {"OVERSCROLL_MODE", 0x1}};
                const auto flag = kViewFlags.find(field->second.name);
                if (flag != kViewFlags.end()) {
                    (*registers)[0] = flag->second;
                    return true;
                }
            }
            if (field->second.signature == "I") {
                // Android framework int constants Unity/SDK code reads. These
                // are the real AOSP values, keyed by slashed class name.
                static const std::unordered_map<
                    std::string_view,
                    std::unordered_map<std::string_view, std::uint64_t>>
                    kStaticInts = {
                        {"android/content/pm/ActivityInfo",
                         {{"SCREEN_ORIENTATION_LANDSCAPE", 0},
                          {"SCREEN_ORIENTATION_PORTRAIT", 1},
                          {"SCREEN_ORIENTATION_USER", 2},
                          {"SCREEN_ORIENTATION_BEHIND", 3},
                          {"SCREEN_ORIENTATION_SENSOR", 4},
                          {"SCREEN_ORIENTATION_NOSENSOR", 5},
                          {"SCREEN_ORIENTATION_SENSOR_LANDSCAPE", 6},
                          {"SCREEN_ORIENTATION_SENSOR_PORTRAIT", 7},
                          {"SCREEN_ORIENTATION_REVERSE_LANDSCAPE", 8},
                          {"SCREEN_ORIENTATION_REVERSE_PORTRAIT", 9},
                          {"SCREEN_ORIENTATION_FULL_SENSOR", 10},
                          {"SCREEN_ORIENTATION_USER_LANDSCAPE", 11},
                          {"SCREEN_ORIENTATION_USER_PORTRAIT", 12},
                          {"SCREEN_ORIENTATION_FULL_USER", 13},
                          {"SCREEN_ORIENTATION_LOCKED", 14},
                          {"SCREEN_ORIENTATION_UNSPECIFIED",
                           0xffffffffull}}},
                        {"android/view/WindowManager$LayoutParams",
                         {{"FLAG_ALLOW_LOCK_WHILE_SCREEN_ON", 0x1},
                          {"FLAG_DIM_BEHIND", 0x2},
                          {"FLAG_BLUR_BEHIND", 0x4},
                          {"FLAG_NOT_FOCUSABLE", 0x8},
                          {"FLAG_NOT_TOUCHABLE", 0x10},
                          {"FLAG_NOT_TOUCH_MODAL", 0x20},
                          {"FLAG_TOUCHABLE_WHEN_WAKING", 0x40},
                          {"FLAG_KEEP_SCREEN_ON", 0x80},
                          {"FLAG_LAYOUT_IN_SCREEN", 0x100},
                          {"FLAG_LAYOUT_NO_LIMITS", 0x200},
                          {"FLAG_FULLSCREEN", 0x400},
                          {"FLAG_FORCE_NOT_FULLSCREEN", 0x800},
                          {"FLAG_DITHER", 0x1000},
                          {"FLAG_SECURE", 0x2000},
                          {"FLAG_SCALED", 0x4000},
                          {"FLAG_IGNORE_CHEEK_PRESSES", 0x8000},
                          {"FLAG_LAYOUT_INSET_DECOR", 0x10000},
                          {"FLAG_ALT_FOCUSABLE_IM", 0x20000},
                          {"FLAG_WATCH_OUTSIDE_TOUCH", 0x40000},
                          {"FLAG_SHOW_WHEN_LOCKED", 0x80000},
                          {"FLAG_SHOW_WALLPAPER", 0x100000},
                          {"FLAG_TURN_SCREEN_ON", 0x200000},
                          {"FLAG_DISMISS_KEYGUARD", 0x400000},
                          {"FLAG_SPLIT_TOUCH", 0x800000},
                          {"FLAG_HARDWARE_ACCELERATED", 0x1000000},
                          {"FLAG_LAYOUT_IN_OVERSCAN", 0x2000000},
                          {"FLAG_TRANSLUCENT_STATUS", 0x4000000},
                          {"FLAG_TRANSLUCENT_NAVIGATION", 0x8000000},
                          {"FLAG_LOCAL_FOCUS_MODE", 0x10000000},
                          {"FLAG_SLIPPERY", 0x20000000},
                          {"FLAG_LAYOUT_ATTACHED_IN_DECOR", 0x40000000},
                          {"FLAG_DRAWS_SYSTEM_BAR_BACKGROUNDS",
                           0x80000000ull}}},
                        {"android/content/res/Configuration",
                         {{"ORIENTATION_UNDEFINED", 0},
                          {"ORIENTATION_PORTRAIT", 1},
                          {"ORIENTATION_LANDSCAPE", 2},
                          {"ORIENTATION_SQUARE", 3},
                          {"SCREENLAYOUT_SIZE_UNDEFINED", 0},
                          {"SCREENLAYOUT_SIZE_SMALL", 1},
                          {"SCREENLAYOUT_SIZE_NORMAL", 2},
                          {"SCREENLAYOUT_SIZE_LARGE", 3},
                          {"SCREENLAYOUT_SIZE_XLARGE", 4},
                          {"SCREENLAYOUT_LONG_NO", 0x10},
                          {"SCREENLAYOUT_LONG_YES", 0x20},
                          {"SCREENLAYOUT_ROUND_NO", 0x100},
                          {"SCREENLAYOUT_ROUND_YES", 0x200},
                          {"UI_MODE_NIGHT_NO", 0x10},
                          {"UI_MODE_NIGHT_YES", 0x20}}},
                        {"android/view/Surface",
                         {{"ROTATION_0", 0},
                          {"ROTATION_90", 1},
                          {"ROTATION_180", 2},
                          {"ROTATION_270", 3}}},
                        {"android/view/Window",
                         {{"FEATURE_NO_TITLE", 7},
                          {"FEATURE_PROGRESS", 2},
                          {"FEATURE_OPTIONS_PANEL", 0},
                          {"FEATURE_CONTEXT_MENU", 6}}},
                        {"android/media/AudioManager",
                         {{"STREAM_VOICE_CALL", 0},
                          {"STREAM_SYSTEM", 1},
                          {"STREAM_RING", 2},
                          {"STREAM_MUSIC", 3},
                          {"STREAM_ALARM", 4},
                          {"STREAM_NOTIFICATION", 5},
                          {"STREAM_DTMF", 8},
                          {"NUM_STREAMS", 5},
                          {"MODE_INVALID", 0xfffffffeull},
                          {"MODE_CURRENT", 0xffffffffull},
                          {"MODE_NORMAL", 0},
                          {"MODE_RINGTONE", 1},
                          {"MODE_IN_CALL", 2},
                          {"MODE_IN_COMMUNICATION", 3},
                          {"MODE_CALL_SCREENING", 4},
                          {"GET_DEVICES_OUTPUTS", 0x1},
                          {"GET_DEVICES_INPUTS", 0x2},
                          {"GET_DEVICES_ALL", 0x3},
                          {"ADJUST_LOWER", 0xffffffffull},
                          {"ADJUST_SAME", 0},
                          {"ADJUST_RAISE", 1},
                          {"ADJUST_MUTE", 0xffffff9cull},
                          {"ADJUST_UNMUTE", 0xffffffa0ull},
                          {"ADJUST_TOGGLE_MUTE", 0xffffff9dull},
                          {"AUDIOFOCUS_GAIN", 1},
                          {"AUDIOFOCUS_LOSS", 0xffffffffull},
                          {"AUDIOFOCUS_LOSS_TRANSIENT", 0xfffffffeull},
                          {"AUDIOFOCUS_LOSS_TRANSIENT_CAN_DUCK",
                           0xfffffffdull},
                          {"AUDIOFOCUS_GAIN_TRANSIENT", 2},
                          {"AUDIOFOCUS_GAIN_TRANSIENT_MAY_DUCK", 3},
                          {"AUDIOFOCUS_GAIN_TRANSIENT_EXCLUSIVE", 4},
                          {"AUDIOFOCUS_REQUEST_FAILED", 0},
                          {"AUDIOFOCUS_REQUEST_GRANTED", 1},
                          {"AUDIOFOCUS_REQUEST_DELAYED", 2},
                          {"FLAG_SHOW_UI", 1},
                          {"FLAG_ALLOW_RINGER_MODES", 2},
                          {"FLAG_PLAY_SOUND", 4},
                          {"FLAG_REMOVE_SOUND_AND_VIBRATE", 8},
                          {"FLAG_VIBRATE", 16},
                          {"FLAG_FIXED_VOLUME", 32},
                          {"FLAG_BLUETOOTH_ABS_VOLUME", 64},
                          {"FLAG_SHOW_SILENT_HINT", 128},
                          {"FLAG_HDMI_SYSTEM_AUDIO_VOLUME", 256},
                          {"FLAG_ACTIVE_MEDIA_ONLY", 512},
                          {"FLAG_SHOW_UI_WARNINGS", 1024},
                          {"FLAG_SHOW_VIBRATE_HINT", 2048},
                          {"RINGER_MODE_SILENT", 0},
                          {"RINGER_MODE_VIBRATE", 1},
                          {"RINGER_MODE_NORMAL", 2},
                          {"VIBRATE_TYPE_NOTIFICATION", 1},
                          {"VIBRATE_TYPE_RINGER", 0},
                          {"VIBRATE_SETTING_ON", 1},
                          {"VIBRATE_SETTING_OFF", 0},
                          {"VIBRATE_SETTING_ONLY_SILENT", 2},
                          {"FX_KEY_CLICK", 0},
                          {"FX_FOCUS_NAVIGATION_UP", 1},
                          {"FX_FOCUS_NAVIGATION_DOWN", 2},
                          {"FX_FOCUS_NAVIGATION_LEFT", 3},
                          {"FX_FOCUS_NAVIGATION_RIGHT", 4},
                          {"FX_KEYPRESS_STANDARD", 5},
                          {"FX_KEYPRESS_SPACEBAR", 6},
                          {"FX_KEYPRESS_DELETE", 7},
                          {"FX_KEYPRESS_RETURN", 8}}},
                        {"android/media/AudioDeviceInfo",
                         {{"TYPE_UNKNOWN", 0xffffffffull},
                          {"TYPE_BUILTIN_EARPIECE", 1},
                          {"TYPE_BUILTIN_SPEAKER", 2},
                          {"TYPE_WIRED_HEADPHONES", 3},
                          {"TYPE_WIRED_HEADSET", 4},
                          {"TYPE_LINE_ANALOG", 5},
                          {"TYPE_LINE_DIGITAL", 6},
                          {"TYPE_BLUETOOTH_SCO", 7},
                          {"TYPE_BLUETOOTH_A2DP", 8},
                          {"TYPE_HDMI", 9},
                          {"TYPE_HDMI_ARC", 10},
                          {"TYPE_USB_DEVICE", 11},
                          {"TYPE_USB_ACCESSORY", 12},
                          {"TYPE_DOCK", 13},
                          {"TYPE_FIXED_LATENCY", 14},
                          {"TYPE_EXPORT", 15},
                          {"TYPE_REMOTE_SUBMIX", 16},
                          {"TYPE_BLE_HEADSET", 17},
                          {"TYPE_BLE_SPEAKER", 18},
                          {"TYPE_EARPHONE", 21},
                          {"TYPE_BLE_BROADCAST", 22},
                          {"FLAG_SYSTEM", 1},
                          {"FLAG_VOICE", 2},
                          {"FLAG_SONIFICATION", 4},
                          {"FLAG_ACCESSIBILITY", 8},
                          {"FLAG_NOTIFICATION", 16}}},
                        {"android/content/pm/PackageManager",
                         {{"PERMISSION_GRANTED", 0},
                          {"PERMISSION_DENIED", 0xffffffffull},
                          {"GET_META_DATA", 0x80},
                          {"GET_SHARED_LIBRARY_FILES", 0x400},
                          {"GET_PROVIDERS", 0x8},
                          {"GET_RECEIVERS", 0x2},
                          {"GET_SERVICES", 0x4},
                          {"GET_ACTIVITIES", 0x1},
                          {"GET_PERMISSIONS", 0x1000},
                          {"SIGNATURE_MATCH", 0},
                          {"SIGNATURE_NEITHER_SIGNED", 1},
                          {"SIGNATURE_FIRST_NOT_SIGNED", 0xffffffffull},
                          {"SIGNATURE_NO_MATCH", 0xfffffffeull},
                          {"SIGNATURE_UNKNOWN_PACKAGE", 0xfffffffdull}}},
                        {"android/media/MediaRouter",
                         {{"ROUTE_TYPE_LIVE_AUDIO", 1},
                          {"ROUTE_TYPE_LIVE_VIDEO", 2},
                          {"ROUTE_TYPE_USER", 4},
                          {"ROUTE_TYPE_REMOTE_DISPLAY", 8},
                          {"CALLBACK_FLAG_PASSIVE_DISCOVERY", 8},
                          {"CALLBACK_FLAG_REQUEST_DISCOVERY", 1},
                          {"CALLBACK_FLAG_FORCE_DISCOVERY", 2},
                          {"CALLBACK_FLAG_PERFORM_ACTIVE_SCAN", 1},
                          {"CALLBACK_FLAG_UNFILTERED_EVENTS", 2}}}};
                const auto class_entry =
                    kStaticInts.find(field->second.class_type);
                if (class_entry != kStaticInts.end()) {
                    const auto constant =
                        class_entry->second.find(field->second.name);
                    if (constant != class_entry->second.end()) {
                        (*registers)[0] = constant->second;
                        return true;
                    }
                }
            }
            {
                SetError(call_error,
                         "guest JNI GetStaticIntField " +
                             field->second.class_type + "." +
                             field->second.name + " signature " +
                             field->second.signature +
                             " is not supported");
                return false;
            }
        };
    const auto get_field_id =
        [this](Aarch64ImportRegistry::Registers* registers,
               std::string* call_error) {
            if (registers == nullptr ||
                !IsCurrentEnvironment((*registers)[0])) {
                SetError(call_error,
                         "guest JNI GetFieldID environment is invalid");
                return false;
            }
            if (backend_ != nullptr) {
                SetError(call_error,
                         "guest JNI GetFieldID is unavailable with an active backend");
                return false;
            }
            std::string name;
            std::string signature;
            if (!ReadGuestText(address_space_, (*registers)[2],
                               "field name", &name, call_error) ||
                !ReadGuestText(address_space_, (*registers)[3],
                               "field signature", &signature,
                               call_error) ||
                !IsFieldSignature(signature)) {
                if (call_error == nullptr || call_error->empty())
                    SetError(call_error,
                             "guest JNI GetFieldID signature is invalid");
                return false;
            }
            const std::lock_guard lock(mutex_);
            std::optional<std::uint64_t> class_identity =
                ReferenceIdentityLocked((*registers)[1]);
            std::optional<std::string_view> class_type =
                ClassTypeLocked((*registers)[1]);
            if ((!class_identity || !class_type ||
                 !IsClassReferenceLocked((*registers)[1])) &&
                !RecoverClassReferenceLocked((*registers)[1])) {
                char detail[192];
                std::snprintf(detail, sizeof(detail),
                              "guest JNI GetFieldID class is invalid "
                              "class=0x%llx name=%s sig=%s",
                              static_cast<unsigned long long>(
                                  (*registers)[1]),
                              name.c_str(), signature.c_str());
                SetError(call_error, detail);
                return false;
            }
            if (!class_identity)
                class_identity =
                    ReferenceIdentityLocked((*registers)[1]);
            if (!class_type) class_type = ClassTypeLocked((*registers)[1]);
            if (!class_identity || !class_type) return false;
            const std::uint64_t reference = heap_->Callocate(1, 8);
            if (reference == 0) {
                SetError(call_error,
                         "guest JNI field reference allocation failed");
                return false;
            }
            field_references_.emplace(
                reference,
                FieldReference{*class_identity, std::string(*class_type),
                               std::move(name), std::move(signature), false});
            (*registers)[0] = reference;
            return true;
        };
    const auto get_object_field =
        [this](Aarch64ImportRegistry::Registers* registers,
               std::string* call_error) {
            if (registers == nullptr ||
                !IsCurrentEnvironment((*registers)[0])) {
                SetError(call_error,
                         "guest JNI GetObjectField environment is invalid");
                return false;
            }
            if (backend_ != nullptr) {
                SetError(call_error,
                         "guest JNI GetObjectField is unavailable with an active backend");
                return false;
            }
            const std::lock_guard lock(mutex_);
            const std::optional<std::string_view> object_type =
                ObjectTypeLocked((*registers)[1]);
            const auto field = field_references_.find((*registers)[2]);
            // ART does not verify that a field's declaring class matches the
            // receiver — the jfieldID already encodes the field. Compat
            // objects carry best-effort types (reflection paths surface as
            // java/lang/Object), so an opaque receiver passes unchecked,
            // while a concrete one must be assignable to the declaring
            // class.
            if (!object_type || field == field_references_.end() ||
                field->second.is_static ||
                (NormalizeCompatibilityType(*object_type) !=
                     "java/lang/Object" &&
                 !IsCompatibilityAssignable(*object_type,
                                            field->second.class_type))) {
                char detail[224];
                std::snprintf(
                    detail, sizeof(detail),
                    "guest JNI GetObjectField provenance is invalid "
                    "obj=0x%llx objtype=%.*s field=%s.%s%s",
                    static_cast<unsigned long long>((*registers)[1]),
                    static_cast<int>(object_type ? object_type->size() : 0),
                    object_type ? object_type->data() : "",
                    field != field_references_.end()
                        ? field->second.class_type.c_str() : "?",
                    field != field_references_.end()
                        ? field->second.name.c_str() : "?",
                    field != field_references_.end()
                        ? field->second.signature.c_str() : "?");
                SetError(call_error, detail);
                return false;
            }
            if (field->second.class_type ==
                    "android/content/pm/ApplicationInfo" &&
                field->second.name == "splitPublicSourceDirs" &&
                field->second.signature == "[Ljava/lang/String;") {
                // The native profile stages one base APK tree and therefore
                // has no Android split APK public-source directories.
                (*registers)[0] = 0;
                return true;
            }
            if (field->second.class_type ==
                    "android/content/pm/PackageInfo" &&
                field->second.name == "versionName" &&
                field->second.signature == "Ljava/lang/String;") {
                if (compatibility_.package_version.empty()) {
                    SetError(call_error,
                             "guest JNI package version compatibility is not configured");
                    return false;
                }
                const std::uint64_t reference =
                    AllocateCompatibilityReference(
                        heap_, CompatibilityReferenceKind::kString,
                        "Ljava/lang/String;",
                        compatibility_.package_version,
                        call_error);
                if (reference == 0) return false;
                object_references_.emplace(
                    reference,
                    ObjectReference{reference, (*registers)[2],
                                    "Ljava/lang/String;",
                                    compatibility_.package_version, 0});
                TrackLocalReferenceLocked(reference);
                (*registers)[0] = reference;
                return true;
            }
            // Unknown object field: hand back a compat value typed by the
            // field signature — empty String for Ljava/lang/String;, a fresh
            // object otherwise — rather than aborting the caller.
            const std::string& field_type = field->second.signature;
            const CompatibilityReferenceKind field_kind =
                field_type == "Ljava/lang/String;"
                    ? CompatibilityReferenceKind::kString
                    : CompatibilityReferenceKind::kObject;
            const std::uint64_t reference = AllocateCompatibilityReference(
                heap_, field_kind, field_type, {}, call_error);
            if (reference == 0) return false;
            object_references_.emplace(
                reference,
                ObjectReference{reference, (*registers)[2], field_type, {},
                                0});
            TrackLocalReferenceLocked(reference);
            if (call_error != nullptr)
                *call_error = "trace:jni-getobjectfield-generic-" +
                              field->second.name;
            (*registers)[0] = reference;
            return true;
        };
    const auto get_int_field =
        [this](Aarch64ImportRegistry::Registers* registers,
               std::string* call_error) {
            if (registers == nullptr ||
                !IsCurrentEnvironment((*registers)[0])) {
                SetError(call_error,
                         "guest JNI GetIntField environment is invalid");
                return false;
            }
            if (backend_ != nullptr) {
                SetError(call_error,
                         "guest JNI GetIntField is unavailable with an active backend");
                return false;
            }
            const std::lock_guard lock(mutex_);
            const std::optional<std::string_view> object_type =
                ObjectTypeLocked((*registers)[1]);
            const auto field = field_references_.find((*registers)[2]);
            // ART does not verify field/receiver provenance — see
            // GetObjectField. An opaque java/lang/Object receiver passes
            // unchecked; a concrete one must be assignable to the
            // declaring class.
            if (!object_type || field == field_references_.end() ||
                field->second.is_static ||
                (NormalizeCompatibilityType(*object_type) !=
                     "java/lang/Object" &&
                 !IsCompatibilityAssignable(*object_type,
                                            field->second.class_type))) {
                SetError(call_error,
                         "guest JNI GetIntField provenance is invalid");
                return false;
            }
            if (field->second.class_type ==
                    "android/content/pm/PackageInfo" &&
                field->second.name == "versionCode" &&
                field->second.signature == "I") {
                // The native profile carries a version name but no Android
                // package-manager version code and stages no OBB payload.
                (*registers)[0] = 0;
                return true;
            }
            if (field->second.class_type == "android/util/DisplayMetrics" &&
                field->second.signature == "I") {
                // DisplayMetrics integer members Unity reads when sizing the
                // render surface. The native window is fixed at the profile's
                // logical size; density is the mdpi baseline.
                constexpr std::array<
                    std::pair<std::string_view, std::int32_t>, 6>
                    kMetrics{{
                        {"widthPixels", 1280},
                        {"heightPixels", 720},
                        {"densityDpi", 160},
                        {"noncompatWidthPixels", 1280},
                        {"noncompatHeightPixels", 720},
                        {"noncompatDensityDpi", 160},
                    }};
                for (const auto& [name, metric] : kMetrics) {
                    if (field->second.name == name) {
                        (*registers)[0] =
                            static_cast<std::uint32_t>(metric);
                        return true;
                    }
                }
            }
            if (call_error != nullptr)
                *call_error = "trace:jni-getfield-default-" +
                              field->second.name;
            (*registers)[0] = 0;
            return true;
        };
    const auto get_long_field =
        [this](Aarch64ImportRegistry::Registers* registers,
               std::string* call_error) {
            if (registers == nullptr ||
                !IsCurrentEnvironment((*registers)[0])) {
                SetError(call_error,
                         "guest JNI GetLongField environment is invalid");
                return false;
            }
            if (backend_ != nullptr) {
                SetError(call_error,
                         "guest JNI GetLongField is unavailable with an active backend");
                return false;
            }
            const std::lock_guard lock(mutex_);
            const std::optional<std::string_view> object_type =
                ObjectTypeLocked((*registers)[1]);
            const auto field = field_references_.find((*registers)[2]);
            // ART does not verify field/receiver provenance — see
            // GetObjectField. An opaque java/lang/Object receiver passes
            // unchecked; a concrete one must be assignable to the
            // declaring class.
            if (!object_type || field == field_references_.end() ||
                field->second.is_static ||
                (NormalizeCompatibilityType(*object_type) !=
                     "java/lang/Object" &&
                 !IsCompatibilityAssignable(*object_type,
                                            field->second.class_type))) {
                SetError(call_error,
                         "guest JNI GetLongField provenance is invalid");
                return false;
            }
            if (call_error != nullptr)
                *call_error = "trace:jni-getfield-default-" +
                              field->second.name;
            (*registers)[0] = 0;
            return true;
        };
    const auto get_float_field =
        [this](Aarch64ImportRegistry::Registers* registers,
               std::string* call_error) {
            if (registers == nullptr ||
                !IsCurrentEnvironment((*registers)[0])) {
                SetError(call_error,
                         "guest JNI GetFloatField environment is invalid");
                return false;
            }
            if (backend_ != nullptr) {
                SetError(call_error,
                         "guest JNI GetFloatField is unavailable with an active backend");
                return false;
            }
            const std::lock_guard lock(mutex_);
            const std::optional<std::string_view> object_type =
                ObjectTypeLocked((*registers)[1]);
            const auto field = field_references_.find((*registers)[2]);
            // ART does not verify field/receiver provenance — see
            // GetObjectField. An opaque java/lang/Object receiver passes
            // unchecked; a concrete one must be assignable to the
            // declaring class.
            if (!object_type || field == field_references_.end() ||
                field->second.is_static ||
                (NormalizeCompatibilityType(*object_type) !=
                     "java/lang/Object" &&
                 !IsCompatibilityAssignable(*object_type,
                                            field->second.class_type))) {
                SetError(call_error,
                         "guest JNI GetFloatField provenance is invalid");
                return false;
            }
            if (field->second.class_type == "android/util/DisplayMetrics" &&
                field->second.signature == "F") {
                // DisplayMetrics float members: mdpi baseline density and
                // density-independent scale/X-Y DPI.
                constexpr std::array<
                    std::pair<std::string_view, float>, 5>
                    kMetrics{{
                        {"density", 1.0f},
                        {"scaledDensity", 1.0f},
                        {"xdpi", 160.0f},
                        {"ydpi", 160.0f},
                        {"noncompatScaledDensity", 1.0f},
                    }};
                for (const auto& [name, metric] : kMetrics) {
                    if (field->second.name == name) {
                        registers->SetFloatResult(metric);
                        return true;
                    }
                }
            }
            if (call_error != nullptr)
                *call_error = "trace:jni-getfield-default-" +
                              field->second.name;
            registers->SetFloatResult(0.0f);
            return true;
        };
    const auto get_double_field =
        [this](Aarch64ImportRegistry::Registers* registers,
               std::string* call_error) {
            if (registers == nullptr ||
                !IsCurrentEnvironment((*registers)[0])) {
                SetError(call_error,
                         "guest JNI GetDoubleField environment is invalid");
                return false;
            }
            if (backend_ != nullptr) {
                SetError(call_error,
                         "guest JNI GetDoubleField is unavailable with an active backend");
                return false;
            }
            const std::lock_guard lock(mutex_);
            const std::optional<std::string_view> object_type =
                ObjectTypeLocked((*registers)[1]);
            const auto field = field_references_.find((*registers)[2]);
            // ART does not verify field/receiver provenance — see
            // GetObjectField. An opaque java/lang/Object receiver passes
            // unchecked; a concrete one must be assignable to the
            // declaring class.
            if (!object_type || field == field_references_.end() ||
                field->second.is_static ||
                (NormalizeCompatibilityType(*object_type) !=
                     "java/lang/Object" &&
                 !IsCompatibilityAssignable(*object_type,
                                            field->second.class_type))) {
                SetError(call_error,
                         "guest JNI GetDoubleField provenance is invalid");
                return false;
            }
            if (call_error != nullptr)
                *call_error = "trace:jni-getfield-default-" +
                              field->second.name;
            registers->SetDoubleResult(0.0);
            return true;
        };
    const auto get_static_object_field =
        [this](Aarch64ImportRegistry::Registers* registers,
               std::string* call_error) {
            if (registers == nullptr ||
                !IsCurrentEnvironment((*registers)[0])) {
                SetError(call_error,
                         "guest JNI GetStaticObjectField environment is invalid");
                return false;
            }
            if (backend_ != nullptr) {
                SetError(call_error,
                         "guest JNI GetStaticObjectField is unavailable with an active backend");
                return false;
            }
            const std::lock_guard lock(mutex_);
            if (!IsClassReferenceLocked((*registers)[1]) &&
                !RecoverClassReferenceLocked((*registers)[1])) {
                SetError(call_error,
                         "guest JNI GetStaticObjectField class is invalid");
                return false;
            }
            const std::optional<std::string_view> class_type =
                ClassTypeLocked((*registers)[1]);
            const auto field = field_references_.find((*registers)[2]);
            // ART does not compare the jclass argument against the field's
            // declaring class — the jfieldID already encodes it. Require only
            // a valid class reference and a static field.
            if (!class_type || field == field_references_.end() ||
                !field->second.is_static) {
                SetError(call_error,
                         "guest JNI GetStaticObjectField provenance is invalid");
                return false;
            }
            std::string_view value;
            std::string service_token_scratch;
            if (*class_type == "android/content/Context" &&
                field->second.name == "AUDIO_SERVICE" &&
                field->second.signature == "Ljava/lang/String;") {
                value = "audio";
            } else if (*class_type == "android/content/Context" &&
                       field->second.name == "LOCATION_SERVICE" &&
                       field->second.signature == "Ljava/lang/String;") {
                value = "location";
            } else if (*class_type == "android/os/Environment" &&
                       field->second.name == "MEDIA_MOUNTED" &&
                       field->second.signature == "Ljava/lang/String;") {
                value = "mounted";
            } else if (value.empty() &&
                       *class_type == "android/content/Intent" &&
                       field->second.signature == "Ljava/lang/String;" &&
                       field->second.name.starts_with("ACTION_")) {
                // Intent.ACTION_* constants are the action name verbatim
                // under android.intent.action.* (BATTERY_CHANGED ->
                // "android.intent.action.BATTERY_CHANGED").
                service_token_scratch =
                    "android.intent.action." +
                    std::string(field->second.name.substr(7));
                value = service_token_scratch;
            } else if (*class_type == "android/os/Build" &&
                       field->second.signature == "Ljava/lang/String;") {
                constexpr std::array<
                    std::pair<std::string_view, std::string_view>, 17>
                    kBuildStrings{{
                        {"MANUFACTURER", "Google"},
                        {"BRAND", "google"},
                        {"MODEL", "Pixel 6"},
                        {"DEVICE", "oriole"},
                        {"PRODUCT", "oriole"},
                        {"HARDWARE", "oriole"},
                        {"BOARD", "oriole"},
                        {"BOOTLOADER", "slider"},
                        {"RADIO", "unknown"},
                        {"SERIAL", "unknown"},
                        {"HOST", "android-build"},
                        {"USER", "android-build"},
                        {"ID", "AP3A.240805.005"},
                        {"DISPLAY", "AP3A.240805.005"},
                        {"TYPE", "user"},
                        {"TAGS", "release-keys"},
                        {"FINGERPRINT",
                         "google/oriole/oriole:15/AP3A.240805.005/12345678:"
                         "user/release-keys"},
                    }};
                for (const auto& [name, string_value] : kBuildStrings) {
                    if (field->second.name == name) {
                        value = string_value;
                        break;
                    }
                }
            } else if (*class_type == "android/os/Build$VERSION" &&
                       field->second.signature == "Ljava/lang/String;") {
                constexpr std::array<
                    std::pair<std::string_view, std::string_view>, 3>
                    kVersionStrings{{
                        {"RELEASE", "15"},
                        {"INCREMENTAL", "AP3A.240805.005"},
                        {"CODENAME", "REL"},
                    }};
                for (const auto& [name, string_value] : kVersionStrings) {
                    if (field->second.name == name) {
                        value = string_value;
                        break;
                    }
                }
            } else if (*class_type == "android/media/AudioManager" &&
                       field->second.name ==
                           "PROPERTY_OUTPUT_SAMPLE_RATE" &&
                       field->second.signature == "Ljava/lang/String;") {
                value = "android.media.property.OUTPUT_SAMPLE_RATE";
            } else if (*class_type == "android/media/AudioManager" &&
                       field->second.name ==
                           "PROPERTY_OUTPUT_FRAMES_PER_BUFFER" &&
                       field->second.signature == "Ljava/lang/String;") {
                value =
                    "android.media.property.OUTPUT_FRAMES_PER_BUFFER";
            } else if (*class_type == "android/content/pm/PackageManager" &&
                       field->second.signature == "Ljava/lang/String;") {
                // FEATURE_* constants resolve to the canonical
                // android.hardware/software feature-name strings.
                constexpr std::array<
                    std::pair<std::string_view, std::string_view>, 40>
                    kFeatureStrings{{
                        {"FEATURE_AUDIO_LOW_LATENCY",
                         "android.hardware.audio.low_latency"},
                        {"FEATURE_AUDIO_OUTPUT",
                         "android.hardware.audio.output"},
                        {"FEATURE_AUDIO_PRO", "android.hardware.audio.pro"},
                        {"FEATURE_MICROPHONE",
                         "android.hardware.microphone"},
                        {"FEATURE_BLUETOOTH", "android.hardware.bluetooth"},
                        {"FEATURE_BLUETOOTH_LE",
                         "android.hardware.bluetooth_le"},
                        {"FEATURE_CAMERA", "android.hardware.camera"},
                        {"FEATURE_CAMERA_AUTOFOCUS",
                         "android.hardware.camera.autofocus"},
                        {"FEATURE_CAMERA_FLASH",
                         "android.hardware.camera.flash"},
                        {"FEATURE_CAMERA_FRONT",
                         "android.hardware.camera.front"},
                        {"FEATURE_FAKETOUCH",
                         "android.hardware.faketouch"},
                        {"FEATURE_GAMEPAD", "android.hardware.gamepad"},
                        {"FEATURE_LEANBACK", "android.software.leanback"},
                        {"FEATURE_LOCATION", "android.hardware.location"},
                        {"FEATURE_LOCATION_GPS",
                         "android.hardware.location.gps"},
                        {"FEATURE_LOCATION_NETWORK",
                         "android.hardware.location.network"},
                        {"FEATURE_NFC", "android.hardware.nfc"},
                        {"FEATURE_OPENGLES_ES20",
                         "android.hardware.opengles.aep"},
                        {"FEATURE_SCREEN_LANDSCAPE",
                         "android.hardware.screen.landscape"},
                        {"FEATURE_SCREEN_PORTRAIT",
                         "android.hardware.screen.portrait"},
                        {"FEATURE_SENSOR_ACCELEROMETER",
                         "android.hardware.sensor.accelerometer"},
                        {"FEATURE_SENSOR_COMPASS",
                         "android.hardware.sensor.compass"},
                        {"FEATURE_SENSOR_GYROSCOPE",
                         "android.hardware.sensor.gyroscope"},
                        {"FEATURE_SENSOR_LIGHT",
                         "android.hardware.sensor.light"},
                        {"FEATURE_SENSOR_PROXIMITY",
                         "android.hardware.sensor.proximity"},
                        {"FEATURE_TOUCHSCREEN",
                         "android.hardware.touchscreen"},
                        {"FEATURE_TOUCHSCREEN_MULTITOUCH",
                         "android.hardware.touchscreen.multitouch"},
                        {"FEATURE_TOUCHSCREEN_MULTITOUCH_DISTINCT",
                         "android.hardware.touchscreen.multitouch.distinct"},
                        {"FEATURE_TOUCHSCREEN_MULTITOUCH_JAZZHANDS",
                         "android.hardware.touchscreen.multitouch.jazzhand"},
                        {"FEATURE_VULKAN_HARDWARE_LEVEL",
                         "android.hardware.vulkan.level"},
                        {"FEATURE_VULKAN_HARDWARE_VERSION",
                         "android.hardware.vulkan.version"},
                        {"FEATURE_VULKAN_HARDWARE_COMPUTE",
                         "android.hardware.vulkan.compute"},
                        {"FEATURE_WIFI", "android.hardware.wifi"},
                        {"FEATURE_WIFI_AWARE", "android.hardware.wifi.aware"},
                        {"FEATURE_WIFI_DIRECT",
                         "android.hardware.wifi.direct"},
                        {"FEATURE_TELEPHONY", "android.hardware.telephony"},
                        {"FEATURE_USB_ACCESSORY",
                         "android.hardware.usb.accessory"},
                        {"FEATURE_USB_HOST", "android.hardware.usb.host"},
                        {"FEATURE_WEBVIEW", "android.software.webview"},
                        {"FEATURE_LOW_RAM", "android.hardware.ram.low"},
                    }};
                for (const auto& [name, feature_value] : kFeatureStrings) {
                    if (field->second.name == name) {
                        value = feature_value;
                        break;
                    }
                }
            } else if (ReceiverClassMatches(*class_type,
                                            "com/ea/nimble/Error") &&
                       field->second.name == "ERROR_DOMAIN" &&
                       field->second.signature == "Ljava/lang/String;") {
                // Nimble error-domain label; the engine only strings it
                // into error reports, and the compat runtime reports no
                // Nimble errors, so the exact spelling is cosmetic.
                value = "com.ea.nimble";
            }
            if (value.empty() &&
                *class_type == "android/content/Context" &&
                field->second.signature == "Ljava/lang/String;") {
                // Context.*_SERVICE constants resolve to the lower-cased
                // service token. Most strip the _SERVICE suffix verbatim;
                // a handful use a non-obvious token.
                constexpr std::array<
                    std::pair<std::string_view, std::string_view>, 12>
                    kServiceTokens{{
                        {"TELEPHONY_SERVICE", "phone"},
                        {"WIFI_P2P_SERVICE", "wifip2p"},
                        {"WIFI_AWARE_SERVICE", "wifiaware"},
                        {"UI_MODE_SERVICE", "uimode"},
                        {"TEXT_SERVICES_MANAGER_SERVICE", "textservices"},
                        {"NETWORK_STATS_SERVICE", "netstats"},
                        {"JOB_SCHEDULER_SERVICE", "jobscheduler"},
                        {"MEDIA_PROJECTION_SERVICE", "media_projection"},
                        {"MEDIA_ROUTER_SERVICE", "media_router"},
                        {"FINGERPRINT_SERVICE", "fingerprint"},
                        {"HARDWARE_PROPERTIES_SERVICE",
                         "hardware_properties"},
                        {"SYSTEM_HEALTH_SERVICE", "systemhealth"},
                    }};
                bool matched = false;
                for (const auto& [name, token] : kServiceTokens) {
                    if (field->second.name == name) {
                        value = token;
                        matched = true;
                        break;
                    }
                }
                if (!matched) {
                    constexpr std::string_view kSuffix = "_SERVICE";
                    std::string field_name(field->second.name);
                    if (field_name.size() > kSuffix.size() &&
                        field_name.compare(field_name.size() - kSuffix.size(),
                                           kSuffix.size(), kSuffix) == 0) {
                        field_name.resize(field_name.size() - kSuffix.size());
                        std::string token;
                        token.reserve(field_name.size());
                        for (char c : field_name)
                            token += static_cast<char>(
                                std::tolower(static_cast<unsigned char>(c)));
                        service_token_scratch = std::move(token);
                        value = service_token_scratch;
                    }
                }
            }
            if (backend_ == nullptr &&
                field->second.is_static &&
                field->second.signature == "Lcom/ea/nimble/Persistence$Storage;") {
                // Persistence$Storage constants (DOCUMENT/CACHE/...) are
                // identity-only enum values; each field name gets its own
                // compat object so separate constants stay distinct.
                const std::string enum_type =
                    field->second.signature;
                const std::uint64_t reference =
                    AllocateCompatibilityReference(
                        heap_, CompatibilityReferenceKind::kObject,
                        enum_type, std::string(field->second.name),
                        call_error);
                if (reference == 0) {
                    SetError(call_error,
                             "guest JNI Persistence$Storage allocation "
                             "failed");
                    return false;
                }
                object_references_.emplace(
                    reference,
                    ObjectReference{reference, 0, enum_type,
                                    std::string(field->second.name), 0});
                TrackLocalReferenceLocked(reference);
                (*registers)[0] = reference;
                return true;
            }
            if (backend_ == nullptr &&
                NormalizeCompatibilityType(*class_type) ==
                    "com/ea/game/pvzfree_row/PvZActivity" &&
                field->second.name == "mInstance" &&
                field->second.signature ==
                    "Lcom/ea/game/pvzfree_row/PvZActivity;") {
                // The activity singleton: hand back a compat typed object so
                // engine code can call its instance methods.
                const std::uint64_t reference = AllocateCompatibilityReference(
                    heap_, CompatibilityReferenceKind::kObject,
                    "Lcom/ea/game/pvzfree_row/PvZActivity;", {}, call_error);
                if (reference == 0) {
                    SetError(call_error,
                             "guest JNI mInstance allocation failed");
                    return false;
                }
                object_references_.emplace(
                    reference,
                    ObjectReference{reference, 0,
                                    "Lcom/ea/game/pvzfree_row/PvZActivity;",
                                    {}, 0});
                TrackLocalReferenceLocked(reference);
                (*registers)[0] = reference;
                return true;
            }
            if (value.empty() &&
                field->second.signature == "Ljava/lang/String;" &&
                class_type->starts_with("android/provider/Settings$")) {
                // Settings.Global/System/Secure public String constants hold
                // the provider key name, which is the lower-cased field name
                // (DEVICE_NAME -> "device_name", ANDROID_ID -> "android_id").
                std::string token;
                token.reserve(field->second.name.size());
                for (char c : field->second.name)
                    token += static_cast<char>(
                        std::tolower(static_cast<unsigned char>(c)));
                service_token_scratch = std::move(token);
                value = service_token_scratch;
            }
            if (backend_ == nullptr &&
                NormalizeCompatibilityType(*class_type) ==
                    "com/unity3d/player/UnityPlayer" &&
                field->second.name == "currentActivity") {
                // UnityPlayer.currentActivity is the running Activity. The
                // profile creates the Activity compat object at initJni time;
                // return a fresh local alias to that registered object rather
                // than a fresh dummy so its identity stays consistent across
                // the engine's uses.
                std::uint64_t activity_identity = 0;
                std::uint64_t activity_method = 0;
                std::string_view activity_type;
                for (const auto& [reference, object] : object_references_) {
                    static_cast<void>(reference);
                    if (NormalizeCompatibilityType(object.type) ==
                        "android/app/Activity") {
                        activity_identity = object.identity;
                        activity_method = object.method_reference;
                        activity_type = object.type;
                        break;
                    }
                }
                if (activity_type.empty()) {
                    for (const auto& [reference, object] :
                         global_object_references_) {
                        static_cast<void>(reference);
                        if (NormalizeCompatibilityType(object.type) ==
                            "android/app/Activity") {
                            activity_identity = object.identity;
                            activity_method = object.method_reference;
                            activity_type = object.type;
                            break;
                        }
                    }
                }
                if (activity_type.empty()) {
                    SetError(call_error,
                             "guest JNI UnityPlayer.currentActivity has no "
                             "registered Activity");
                    return false;
                }
                const std::uint64_t activity_reference =
                    AllocateCompatibilityReference(
                        heap_, CompatibilityReferenceKind::kObject,
                        activity_type, {}, call_error);
                if (activity_reference == 0) {
                    SetError(call_error,
                             "guest JNI local activity reference allocation "
                             "failed");
                    return false;
                }
                object_references_.emplace(
                    activity_reference,
                    ObjectReference{activity_identity, activity_method,
                                    std::string(activity_type), {}, 0});
                TrackLocalReferenceLocked(activity_reference);
                (*registers)[0] = activity_reference;
                return true;
            }
            if (value.empty() &&
                NormalizeCompatibilityType(*class_type) ==
                    "com/adjust/sdk/LogLevel") {
                // Adjust SDK LogLevel enum constants (VERBOSE/DEBUG/...)
                // are read via a generic Ljava/lang/Object; jfieldID. Serve a
                // compat object of the concrete enum type so getClass and
                // method dispatch observe LogLevel rather than Object.
                const std::uint64_t level_reference =
                    AllocateCompatibilityReference(
                        heap_, CompatibilityReferenceKind::kObject,
                        "Lcom/adjust/sdk/LogLevel;", {}, call_error);
                if (level_reference == 0) {
                    SetError(call_error,
                             "guest JNI LogLevel reference allocation failed");
                    return false;
                }
                object_references_.emplace(
                    level_reference,
                    ObjectReference{0, 0, "Lcom/adjust/sdk/LogLevel;", {}, 0});
                TrackLocalReferenceLocked(level_reference);
                (*registers)[0] = level_reference;
                return true;
            }
            if (value.empty()) {
                SetError(call_error,
                         "guest JNI GetStaticObjectField " +
                             std::string(*class_type) + "." +
                             field->second.name + " signature " +
                             field->second.signature +
                             " is not supported");
                return false;
            }
            const std::uint64_t reference = AllocateCompatibilityReference(
                heap_, CompatibilityReferenceKind::kString,
                "Ljava/lang/String;", value, call_error);
            if (reference == 0) {
                SetError(call_error,
                         "guest JNI static object reference allocation failed");
                return false;
            }
            object_references_.emplace(
                reference,
                ObjectReference{reference, 0, "Ljava/lang/String;",
                                std::string(value), 0});
            TrackLocalReferenceLocked(reference);
            (*registers)[0] = reference;
            return true;
        };
    const auto get_method_id =
        [this](Aarch64ImportRegistry::Registers* registers,
               std::string* call_error) {
            if (registers == nullptr ||
                !IsCurrentEnvironment((*registers)[0])) {
                SetError(call_error,
                         "guest JNI GetMethodID environment is invalid");
                return false;
            }
            std::string name;
            std::string signature;
            if (!ReadGuestText(address_space_, (*registers)[2],
                               "method name", &name, call_error) ||
                !ReadGuestText(address_space_, (*registers)[3],
                               "method signature", &signature,
                               call_error))
                return false;
            const std::lock_guard lock(mutex_);
            if (!IsClassReferenceLocked((*registers)[1]) &&
                !RecoverClassReferenceLocked((*registers)[1])) {
                char detail[192];
                std::snprintf(detail, sizeof(detail),
                              "guest JNI GetMethodID class is invalid "
                              "class=0x%llx name=%s sig=%s",
                              static_cast<unsigned long long>(
                                  (*registers)[1]),
                              name.c_str(), signature.c_str());
                SetError(call_error, detail);
                return false;
            }
            const std::optional<std::uint64_t> class_identity =
                ReferenceIdentityLocked((*registers)[1]);
            const std::optional<std::string_view> class_type =
                ClassTypeLocked((*registers)[1]);
            if (!class_identity || !class_type) return false;
            if (JniTraceEnabled())
                std::fprintf(stderr,
                    "ISOMR_CTR_GETMETHOD class=%s name=%s sig=%s\n",
                    std::string(*class_type).c_str(), name.c_str(),
                    signature.c_str());
            // Stable jmethodID: a method id is unique per declaring class,
            // name, and signature. Unity's JNIBridge lazily resolves each
            // interface method via GetMethodID while the reflected Method
            // handed to JNIBridge.invoke carries an id minted separately by
            // CreateCompatibilityReflectedMethodReference; without dedup the
            // proxy dispatcher's mid comparison never matches and the call
            // throws AbstractMethodError.
            for (const auto& [candidate, method] : method_references_) {
                if (!method.is_static && method.name == name &&
                    method.signature == signature &&
                    ReceiverClassMatches(method.class_type, *class_type)) {
                    (*registers)[0] = candidate;
                    return true;
                }
            }
            GuestJniBackend::Handle backend_method = 0;
            if (backend_ != nullptr) {
                const std::optional<GuestJniBackend::Handle> backend_class =
                    BackendReferenceLocked((*registers)[1]);
                if (!backend_class ||
                    !backend_->GetMethodId(*backend_class, name, signature,
                                           false, &backend_method,
                                           call_error))
                    return false;
                if (backend_method == 0) {
                    (*registers)[0] = 0;
                    return true;
                }
            }
            const std::uint64_t reference = heap_->Callocate(1, 8);
            if (reference == 0) {
                if (backend_method != 0) {
                    std::string ignored;
                    (void)backend_->DeleteMethod(backend_method, &ignored);
                }
                SetError(call_error,
                         "guest JNI method reference allocation failed");
                return false;
            }
            RecordHttpObservationEventLocked(
                HttpObservationEventKind::kMethodLookup, *class_type,
                name, signature);
            method_references_.emplace(
                reference,
                MethodReference{*class_identity, std::string(*class_type),
                                std::move(name),
                                std::move(signature), false,
                                backend_method});
            (*registers)[0] = reference;
            return true;
        };
    const auto exception_clear =
        [this](Aarch64ImportRegistry::Registers* registers,
               std::string* call_error) {
            if (registers == nullptr ||
                !IsCurrentEnvironment((*registers)[0])) {
                SetError(call_error, "guest JNI environment is invalid");
                return false;
            }
            if (backend_ == nullptr) {
                const std::lock_guard lock(mutex_);
                exception_pending().pending = false;
                exception_pending().type.clear();
                exception_pending().message.clear();
                exception_pending().reference = 0;
                MirrorPendingExceptionLocked();
            }
            return backend_ == nullptr || backend_->ExceptionClear(call_error);
        };
    const auto throw_new =
        [this](Aarch64ImportRegistry::Registers* registers,
               std::string* call_error) {
            if (registers == nullptr ||
                !IsCurrentEnvironment((*registers)[0])) {
                SetError(call_error, "guest JNI environment is invalid");
                return false;
            }
            if (backend_ != nullptr) {
                SetError(call_error,
                         "guest JNI ThrowNew is unavailable with an active backend");
                return false;
            }
            std::string message;
            if ((*registers)[2] != 0 &&
                !ReadGuestText(address_space_, (*registers)[2], "throw-new",
                               &message, call_error)) {
                return false;
            }
            const std::lock_guard lock(mutex_);
            exception_pending().type = "Ljava/lang/Throwable;";
            exception_pending().message = std::move(message);
            exception_pending().reference = 0;
            exception_pending().pending = true;
            MirrorPendingExceptionLocked();
            std::fprintf(stdout,
                "ISOMR_CTR_THROWNEW type=%s message=%s\n",
                exception_pending().type.c_str(),
                exception_pending().message.c_str());
            (*registers)[0] = 0;
            return true;
        };
    const auto throw_throwable =
        [this](Aarch64ImportRegistry::Registers* registers,
               std::string* call_error) {
            if (registers == nullptr ||
                !IsCurrentEnvironment((*registers)[0])) {
                SetError(call_error, "guest JNI environment is invalid");
                return false;
            }
            if (backend_ != nullptr) {
                SetError(call_error,
                         "guest JNI Throw is unavailable with an active backend");
                return false;
            }
            const std::optional<std::string_view> throwable_type =
                ObjectTypeLocked((*registers)[1]);
            const std::lock_guard lock(mutex_);
            exception_pending().type = std::string(
                throwable_type.value_or("Ljava/lang/Throwable;"));
            exception_pending().message.clear();
            exception_pending().reference = (*registers)[1];
            exception_pending().pending = true;
            MirrorPendingExceptionLocked();
            (*registers)[0] = 0;
            return true;
        };
    const auto exception_occurred =
        [this](Aarch64ImportRegistry::Registers* registers,
               std::string* call_error) {
            if (registers == nullptr ||
                !IsCurrentEnvironment((*registers)[0])) {
                SetError(call_error,
                         "guest JNI ExceptionOccurred environment is invalid");
                return false;
            }
            if (backend_ != nullptr) {
                SetError(call_error,
                         "guest JNI ExceptionOccurred is unavailable with an active backend");
                return false;
            }
            {
                const std::lock_guard lock(mutex_);
                if (exception_pending().pending &&
                    exception_pending().reference == 0) {
                    const std::uint64_t reference =
                        AllocateCompatibilityReference(
                            heap_, CompatibilityReferenceKind::kObject,
                            exception_pending().type,
                            exception_pending().message, call_error);
                    if (reference == 0) return false;
                    object_references_.emplace(
                        reference,
                        ObjectReference{reference, (*registers)[2],
                                        exception_pending().type,
                                        exception_pending().message, 0});
                    TrackLocalReferenceLocked(reference);
                    exception_pending().reference = reference;
                }
                (*registers)[0] = exception_pending().reference;
                if (exception_pending().reference != 0)
                    std::fprintf(stdout,
                        "ISOMR_CTR_EXC_OCCURRED ref=0x%llx type=%s\n",
                        (unsigned long long)exception_pending().reference,
                        exception_pending().type.c_str());
            }
            return true;
        };
    const auto push_local_frame =
        [this](Aarch64ImportRegistry::Registers* registers,
               std::string* call_error) {
            if (registers == nullptr ||
                !IsCurrentEnvironment((*registers)[0])) {
                SetError(call_error,
                         "guest JNI PushLocalFrame environment is invalid");
                return false;
            }
            constexpr std::int32_t kMaximumCapacity = 65536;
            constexpr std::size_t kMaximumDepth = 256;
            const std::int32_t capacity = static_cast<std::int32_t>(
                static_cast<std::uint32_t>((*registers)[1]));
            const std::optional<std::uint64_t> thread =
                thread_state_ != nullptr
                    ? thread_state_->CurrentErrnoAddress()
                    : std::nullopt;
            if (!thread) {
                SetError(call_error,
                         "guest JNI PushLocalFrame thread is not attached");
                return false;
            }
            if (capacity < 0 || capacity > kMaximumCapacity) {
                (*registers)[0] = JniResult(kJniError);
                return true;
            }
            const std::lock_guard lock(mutex_);
            auto& frames = local_reference_frames_[*thread];
            if (frames.size() >= kMaximumDepth) {
                (*registers)[0] = JniResult(kJniError);
                return true;
            }
            try {
                LocalReferenceFrame frame;
                frame.requested_capacity =
                    static_cast<std::size_t>(capacity);
                frame.created_references.reserve(
                    static_cast<std::size_t>(capacity));
                frames.push_back(std::move(frame));
            } catch (const std::bad_alloc&) {
                (*registers)[0] = JniResult(kJniError);
                return true;
            }
            (*registers)[0] = JniResult(kJniOk);
            return true;
        };
    const auto delete_local_ref =
        [this](Aarch64ImportRegistry::Registers* registers,
               std::string* call_error) {
            ref_op_counts[2].fetch_add(1, std::memory_order_relaxed);
            if (registers == nullptr ||
                !IsCurrentEnvironment((*registers)[0])) {
                SetError(call_error,
                         "guest JNI DeleteLocalRef environment is invalid");
                return false;
            }
            return DeleteLocalReference((*registers)[1], call_error);
        };
    const auto new_global_ref =
        [this](Aarch64ImportRegistry::Registers* registers,
               std::string* call_error) {
            ref_op_counts[0].fetch_add(1, std::memory_order_relaxed);
            if (registers == nullptr ||
                !IsCurrentEnvironment((*registers)[0])) {
                SetError(call_error,
                         "guest JNI NewGlobalRef environment is invalid");
                return false;
            }
            const std::uint64_t source = (*registers)[1];
            if (source == 0) {
                (*registers)[0] = 0;
                return true;
            }
            const std::lock_guard lock(mutex_);
            const std::optional<std::uint64_t> identity =
                ReferenceIdentityLocked(source);
            if (!identity) {
                SetError(call_error,
                         "guest JNI NewGlobalRef reference is invalid");
                return false;
            }
            const auto local_class = class_references_.find(source);
            const auto global_class = global_class_references_.find(source);
            const auto local_object = object_references_.find(source);
            const auto global_object = global_object_references_.find(source);
            const bool is_class = local_class != class_references_.end() ||
                                  global_class != global_class_references_.end();
            if (!is_class && local_object == object_references_.end() &&
                global_object == global_object_references_.end()) {
                SetError(call_error,
                         "guest JNI NewGlobalRef object provenance is invalid");
                return false;
            }
            const std::string type =
                is_class
                    ? (local_class != class_references_.end()
                           ? local_class->second.type
                           : global_class->second.type)
                    : (local_object != object_references_.end()
                           ? local_object->second.type
                           : global_object->second.type);
            const std::string string_value =
                is_class
                    ? std::string{}
                    : (local_object != object_references_.end()
                           ? local_object->second.string_value
                           : global_object->second.string_value);
            const CompatibilityReferenceKind kind =
                is_class
                    ? CompatibilityReferenceKind::kClass
                    : IsStringTypeName(type)
                          ? CompatibilityReferenceKind::kString
                          : CompatibilityReferenceKind::kObject;
            const std::uint64_t reference = AllocateJniReference(
                heap_, backend_ == nullptr, kind, type, string_value,
                call_error);
            if (reference == 0) {
                SetError(call_error,
                         "guest JNI global reference allocation failed");
                return false;
            }
            GuestJniBackend::Handle backend_reference = 0;
            if (backend_ != nullptr) {
                const std::optional<GuestJniBackend::Handle> backend_source =
                    BackendReferenceLocked(source);
                if (!backend_source ||
                    !backend_->NewGlobalRef(*backend_source,
                                            &backend_reference, call_error)) {
                    std::string ignored;
                    (void)heap_->Release(reference, &ignored);
                    return false;
                }
                if (backend_reference == 0) {
                    std::string ignored;
                    (void)heap_->Release(reference, &ignored);
                    (*registers)[0] = 0;
                    return true;
                }
            }
            if (is_class) {
                global_class_references_.emplace(
                    reference,
                    GlobalClassReference{*identity, type,
                                         backend_reference});
            } else {
                const std::uint64_t method =
                    local_object != object_references_.end()
                        ? local_object->second.method_reference
                        : global_object->second.method_reference;
                global_object_references_.emplace(
                    reference,
                    GlobalObjectReference{*identity, method, type,
                                          string_value,
                                          backend_reference});
            }
            (*registers)[0] = reference;
            return true;
        };
    const auto delete_global_ref =
        [this](Aarch64ImportRegistry::Registers* registers,
               std::string* call_error) {
            const uint64_t dele =
                ref_op_counts[1].fetch_add(1, std::memory_order_relaxed) + 1;
            const uint64_t total =
                dele + ref_op_counts[0].load(std::memory_order_relaxed) +
                ref_op_counts[2].load(std::memory_order_relaxed);
            if (total % 200000 == 0) {
                std::fprintf(stderr,
                             "ISOMR_REF_OPS n=%llu new=%llu dele=%llu "
                             "delloc=%llu\n",
                             static_cast<unsigned long long>(total),
                             static_cast<unsigned long long>(
                                 ref_op_counts[0].load()),
                             static_cast<unsigned long long>(
                                 ref_op_counts[1].load()),
                             static_cast<unsigned long long>(
                                 ref_op_counts[2].load()));
                std::fflush(stderr);
            }
            if (registers == nullptr ||
                !IsCurrentEnvironment((*registers)[0])) {
                SetError(call_error,
                         "guest JNI DeleteGlobalRef environment is invalid");
                return false;
            }
            const std::uint64_t reference = (*registers)[1];
            if (reference == 0) return true;
            const std::lock_guard lock(mutex_);
            const auto object = global_object_references_.find(reference);
            if (object != global_object_references_.end()) {
                const std::uint64_t identity = object->second.identity;
                if (backend_ != nullptr &&
                    !backend_->DeleteGlobalRef(object->second.backend,
                                               call_error))
                    return false;
                if (!heap_->Release(reference, call_error)) return false;
                global_object_references_.erase(object);
                // The liveness scan decides whether attached resources can
                // be reclaimed; when the identity has no attachments there
                // is nothing to reclaim, so skip the O(refs) walk entirely.
                // Unity init deletes globals at a high rate and the scans
                // otherwise serialize all JNI traffic behind mutex_.
                if (!IdentityHasAttachmentsLocked(identity)) return true;
                bool identity_is_live = false;
                for (const auto& [candidate, value] : object_references_) {
                    static_cast<void>(candidate);
                    identity_is_live = value.identity == identity;
                    if (identity_is_live) break;
                }
                if (!identity_is_live) {
                    for (const auto& [candidate, value] :
                         global_object_references_) {
                        static_cast<void>(candidate);
                        identity_is_live = value.identity == identity;
                        if (identity_is_live) break;
                    }
                }
                if (!identity_is_live &&
                    !EraseIdentityAttachmentsLocked(identity, call_error))
                    return false;
                return true;
            }
            const auto class_reference =
                global_class_references_.find(reference);
            if (class_reference != global_class_references_.end()) {
                if (backend_ != nullptr &&
                    !backend_->DeleteGlobalRef(class_reference->second.backend,
                                               call_error))
                    return false;
                if (!heap_->Release(reference, call_error)) return false;
                global_class_references_.erase(class_reference);
                return true;
            }
            SetError(call_error,
                     "guest JNI DeleteGlobalRef reference is invalid");
            return false;
        };
    const auto is_same_object =
        [this](Aarch64ImportRegistry::Registers* registers,
               std::string* call_error) {
            if (registers == nullptr ||
                !IsCurrentEnvironment((*registers)[0])) {
                SetError(call_error,
                         "guest JNI IsSameObject environment is invalid");
                return false;
            }
            const std::uint64_t left = (*registers)[1];
            const std::uint64_t right = (*registers)[2];
            const std::lock_guard lock(mutex_);
            const std::optional<std::uint64_t> left_identity =
                ReferenceIdentityLocked(left);
            const std::optional<std::uint64_t> right_identity =
                ReferenceIdentityLocked(right);
            if (!left_identity || !right_identity) {
                SetError(call_error,
                         "guest JNI IsSameObject reference is invalid");
                return false;
            }
            if (backend_ != nullptr) {
                const std::optional<GuestJniBackend::Handle> left_backend =
                    BackendReferenceLocked(left);
                const std::optional<GuestJniBackend::Handle> right_backend =
                    BackendReferenceLocked(right);
                bool same = false;
                if (!left_backend || !right_backend ||
                    !backend_->IsSameObject(*left_backend, *right_backend,
                                            &same, call_error))
                    return false;
                (*registers)[0] = same ? 1 : 0;
            } else {
                (*registers)[0] = *left_identity == *right_identity ? 1 : 0;
            }
            if (call_error != nullptr) {
                const auto describe_reference = [this](std::uint64_t value) {
                    if (value == 0) return std::string("null");
                    if (class_references_.contains(value))
                        return std::string("local-class");
                    if (global_class_references_.contains(value))
                        return std::string("global-class");
                    const auto local_object = object_references_.find(value);
                    if (local_object != object_references_.end())
                        return std::string(
                            IsStringTypeName(local_object->second.type)
                                ? "local-string"
                                : "local-object");
                    const auto global_object =
                        global_object_references_.find(value);
                    if (global_object != global_object_references_.end())
                        return std::string(
                            IsStringTypeName(global_object->second.type)
                                ? "global-string"
                                : "global-object");
                    return std::string("unknown");
                };
                *call_error =
                    "trace:jni-same-left-" +
                    describe_reference(left) +
                    "-right-" +
                    describe_reference(right) + "-handle-" +
                    (left == right ? "same" : "different") + "-result-" +
                    ((*registers)[0] == 0 ? "false" : "true");
            }
            return true;
        };
    const auto new_local_ref =
        [this](Aarch64ImportRegistry::Registers* registers,
               std::string* call_error) {
            if (registers == nullptr ||
                !IsCurrentEnvironment((*registers)[0])) {
                SetError(call_error,
                         "guest JNI NewLocalRef environment is invalid");
                return false;
            }
            const std::uint64_t source = (*registers)[1];
            if (source == 0) {
                (*registers)[0] = 0;
                return true;
            }
            if (backend_ != nullptr) {
                SetError(call_error,
                         "guest JNI NewLocalRef is unavailable with an active backend");
                return false;
            }
            const std::lock_guard lock(mutex_);
            const std::optional<std::uint64_t> identity =
                ReferenceIdentityLocked(source);
            if (!identity) {
                SetError(call_error,
                         "guest JNI NewLocalRef source is invalid");
                return false;
            }
            const auto local_class = class_references_.find(source);
            const auto global_class = global_class_references_.find(source);
            if (local_class != class_references_.end() ||
                global_class != global_class_references_.end()) {
                const std::string& type =
                    local_class != class_references_.end()
                        ? local_class->second.type
                        : global_class->second.type;
                const std::uint64_t reference = AllocateCompatibilityReference(
                    heap_, CompatibilityReferenceKind::kClass, type, {},
                    call_error);
                if (reference == 0) {
                    SetError(call_error,
                             "guest JNI local class reference allocation failed");
                    return false;
                }
                class_references_.emplace(
                    reference, ClassReference{*identity, type, 0});
                TrackLocalReferenceLocked(reference);
                (*registers)[0] = reference;
                return true;
            }
            const auto local_object = object_references_.find(source);
            const auto global_object =
                global_object_references_.find(source);
            if (local_object == object_references_.end() &&
                global_object == global_object_references_.end()) {
                SetError(call_error,
                         "guest JNI NewLocalRef provenance is invalid");
                return false;
            }
            const std::uint64_t method =
                local_object != object_references_.end()
                    ? local_object->second.method_reference
                    : global_object->second.method_reference;
            const std::string& type =
                local_object != object_references_.end()
                    ? local_object->second.type
                    : global_object->second.type;
            const std::string& string_value =
                local_object != object_references_.end()
                    ? local_object->second.string_value
                    : global_object->second.string_value;
            const CompatibilityReferenceKind kind =
                IsStringTypeName(type)
                    ? CompatibilityReferenceKind::kString
                    : CompatibilityReferenceKind::kObject;
            const std::uint64_t reference = AllocateCompatibilityReference(
                heap_, kind, type, string_value, call_error);
            if (reference == 0) {
                SetError(call_error,
                         "guest JNI local object reference allocation failed");
                return false;
            }
            object_references_.emplace(
                reference,
                ObjectReference{*identity, method, type, string_value, 0});
            TrackLocalReferenceLocked(reference);
            (*registers)[0] = reference;
            return true;
        };
    const auto get_object_class =
        [this](Aarch64ImportRegistry::Registers* registers,
               std::string* call_error) {
            if (registers == nullptr ||
                !IsCurrentEnvironment((*registers)[0])) {
                SetError(call_error,
                         "guest JNI GetObjectClass environment is invalid");
                return false;
            }
            const std::lock_guard lock(mutex_);
            std::string type;
            const std::optional<std::string_view> object_type =
                ObjectTypeLocked((*registers)[1]);
            if (object_type) {
                type.assign(*object_type);
            } else if (IsClassReferenceLocked((*registers)[1])) {
                type = "Ljava/lang/Class;";
            } else {
                // Untracked object (raw heap block used as thiz by the
                // surface natives): return a generic Object class ref.
                const std::uint64_t generic_class =
                    AllocateCompatibilityReference(
                        heap_, CompatibilityReferenceKind::kClass,
                        "Ljava/lang/Object;", {}, call_error);
                if (generic_class == 0) return false;
                class_references_.emplace(
                    generic_class,
                    ClassReference{generic_class, "Ljava/lang/Object;", 0});
                TrackLocalReferenceLocked(generic_class);
                (*registers)[0] = generic_class;
                return true;
            }
            GuestJniBackend::Handle backend_reference = 0;
            if (backend_ != nullptr) {
                const std::optional<GuestJniBackend::Handle> backend_object =
                    BackendReferenceLocked((*registers)[1]);
                if (!backend_object ||
                    !backend_->GetObjectClass(*backend_object,
                                              &backend_reference,
                                              call_error))
                    return false;
                if (backend_reference == 0) {
                    (*registers)[0] = 0;
                    return true;
                }
            }
            const std::uint64_t reference = AllocateJniReference(
                heap_, backend_ == nullptr, CompatibilityReferenceKind::kClass,
                type, {}, call_error);
            if (reference == 0) {
                if (backend_reference != 0) {
                    std::string ignored;
                    (void)backend_->DeleteLocalRef(backend_reference,
                                                   &ignored);
                }
                SetError(call_error,
                         "guest JNI class reference allocation failed");
                return false;
            }
            class_references_.emplace(
                reference,
                ClassReference{reference, std::move(type),
                               backend_reference});
            TrackLocalReferenceLocked(reference);
            (*registers)[0] = reference;
            return true;
        };
    const auto new_object_v =
        [this](Aarch64ImportRegistry::Registers* registers,
               std::string* call_error) {
            if (registers == nullptr ||
                !IsCurrentEnvironment((*registers)[0])) {
                SetError(call_error,
                         "guest JNI NewObjectV environment is invalid");
                return false;
            }
            if (backend_ != nullptr) {
                SetError(call_error,
                         "guest JNI NewObjectV is unavailable with an active backend");
                return false;
            }
            if ((*registers)[3] == 0) {
                SetError(call_error,
                         "guest JNI NewObjectV va_list is invalid");
                return false;
            }
            std::array<std::uint8_t, 32> arguments{};
            if (!address_space_->Read((*registers)[3], arguments.data(),
                                      arguments.size())) {
                SetError(call_error,
                         "guest JNI NewObjectV va_list is invalid");
                return false;
            }
            const std::lock_guard lock(mutex_);
            const auto local_class =
                FindOwnedLocalClassReferenceLocked((*registers)[1]);
            const auto global_class =
                global_class_references_.find((*registers)[1]);
            if (local_class == class_references_.end() &&
                global_class == global_class_references_.end()) {
                SetError(call_error,
                         "guest JNI NewObjectV class is invalid");
                return false;
            }
            const std::optional<std::uint64_t> class_identity =
                ReferenceIdentityLocked((*registers)[1]);
            const auto method = method_references_.find((*registers)[2]);
            // A jmethodID is shared across jclass instances of the same
            // class, so match on class_type, not the per-instance identity.
            const std::optional<std::string_view> call_class_type =
                ClassTypeLocked((*registers)[1]);
            const bool class_matches =
                method != method_references_.end() && class_identity &&
                (method->second.class_identity == *class_identity ||
                 (call_class_type &&
                  ReceiverClassMatches(method->second.class_type,
                                       *call_class_type)));
            if (!class_identity || method == method_references_.end() ||
                !class_matches ||
                method->second.is_static ||
                method->second.name != "<init>" ||
                !method->second.signature.starts_with('(') ||
                ReturnType(method->second.signature) != "V") {
                char detail[320];
                std::snprintf(
                    detail, sizeof(detail),
                    "guest JNI NewObjectV constructor is invalid "
                    "class=0x%llx mid=0x%llx class_id=%d method_found=%d "
                    "class_matches=%d is_static=%d name=%s sig=%s "
                    "method_class=%s call_class=%s",
                    static_cast<unsigned long long>((*registers)[1]),
                    static_cast<unsigned long long>((*registers)[2]),
                    class_identity ? 1 : 0,
                    method != method_references_.end() ? 1 : 0,
                    class_matches ? 1 : 0,
                    method != method_references_.end() &&
                            method->second.is_static
                        ? 1
                        : 0,
                    method != method_references_.end()
                        ? method->second.name.c_str()
                        : "?",
                    method != method_references_.end()
                        ? method->second.signature.c_str()
                        : "?",
                    method != method_references_.end()
                        ? method->second.class_type.c_str()
                        : "?",
                    call_class_type ? std::string(*call_class_type).c_str()
                                    : "?");
                SetError(call_error, detail);
                return false;
            }
            const std::string& type =
                local_class != class_references_.end()
                    ? local_class->second.type
                    : global_class->second.type;
            std::array<std::uint64_t, 4> observed_arguments{};
            if (method->second.class_type ==
                    "com/king/http/HttpManager" &&
                method->second.signature == "(JJIZ)V") {
                for (std::size_t index = 0;
                     index < observed_arguments.size(); ++index) {
                    std::string ignored;
                    if (!ReadGuestGeneralArgument(
                            address_space_, (*registers)[3],
                            &observed_arguments[index], &ignored, index)) {
                        observed_arguments = {};
                        break;
                    }
                }
            }
            RecordHttpObservationEventLocked(
                HttpObservationEventKind::kConstructor,
                method->second.class_type, method->second.name,
                method->second.signature, observed_arguments);
            std::uint64_t scanner_stream_identity = 0;
            if (NormalizeCompatibilityType(method->second.class_type) ==
                    "java/util/Scanner" &&
                (method->second.signature ==
                     "(Ljava/io/InputStream;)V" ||
                 method->second.signature ==
                     "(Ljava/io/InputStream;Ljava/lang/String;)V")) {
                std::uint64_t stream_reference = 0;
                if (!ReadGuestGeneralArgument(address_space_,
                                              (*registers)[3],
                                              &stream_reference,
                                              call_error))
                    return false;
                const std::optional<std::uint64_t> identity =
                    ReferenceIdentityLocked(stream_reference);
                if (!identity || !input_streams_.contains(*identity)) {
                    SetError(call_error,
                             "guest JNI Scanner constructor InputStream is invalid");
                    return false;
                }
                scanner_stream_identity = *identity;
                if (method->second.signature ==
                    "(Ljava/io/InputStream;Ljava/lang/String;)V") {
                    std::uint64_t charset_reference = 0;
                    if (!ReadGuestGeneralArgument(address_space_,
                                                  (*registers)[3],
                                                  &charset_reference,
                                                  call_error, 1))
                        return false;
                    const std::optional<std::string_view> charset_type =
                        ObjectTypeLocked(charset_reference);
                    if (!charset_type ||
                        !IsStringTypeName(*charset_type)) {
                        SetError(call_error,
                                 "guest JNI Scanner constructor charset is invalid");
                        return false;
                    }
                }
            }
            const std::uint64_t reference = AllocateCompatibilityReference(
                heap_, CompatibilityReferenceKind::kObject, type, {},
                call_error);
            if (reference == 0) {
                SetError(call_error,
                         "guest JNI object reference allocation failed");
                return false;
            }
            object_references_.emplace(
                reference,
                ObjectReference{reference, (*registers)[2], type, {}, 0});
            if (NormalizeCompatibilityType(type) == "android/os/Handler" &&
                method->second.signature.find(
                    "Landroid/os/Handler$Callback;") !=
                    std::string::npos) {
                // Unity's SwappyDisplayManager binds a HandlerThread looper
                // Handler with a JNIBridge proxy implementing
                // Handler.Callback; remember it so obtainMessage/sendToTarget
                // can deliver the wakeup to the peer.
                const std::string& sig = method->second.signature;
                std::size_t position = 0;
                std::size_t callback_index = 0;
                bool found_callback = false;
                const std::size_t open = sig.find('(');
                for (std::size_t at = open + 1; at < sig.size() &&
                                               sig[at] != ')'; ++position) {
                    if (sig.compare(
                            at, std::string("Landroid/os/Handler$Callback;")
                                    .size(),
                            "Landroid/os/Handler$Callback;") == 0) {
                        callback_index = position;
                        found_callback = true;
                        break;
                    }
                    // Advance past this argument's type descriptor; every
                    // branch must move `at` forward or the walk spins.
                    while (at < sig.size() && sig[at] == '[') ++at;
                    if (at < sig.size() && sig[at] == 'L') {
                        const std::size_t end = sig.find(';', at);
                        at = end == std::string::npos ? sig.size() : end + 1;
                    } else {
                        ++at;
                    }
                }
                if (found_callback) {
                    std::uint64_t callback = 0;
                    std::string ignored;
                    if (ReadGuestGeneralArgument(address_space_,
                                                 (*registers)[3], &callback,
                                                 &ignored, callback_index) &&
                        callback != 0) {
                        const auto identity =
                            ReferenceIdentityLocked(callback);
                        const auto proxy =
                            identity
                                ? interface_proxies_.find(*identity)
                                : interface_proxies_.end();
                        if (proxy != interface_proxies_.end()) {
                            handler_callbacks_.emplace(
                                reference, proxy->second.native_handle);
                            if (call_error != nullptr)
                                *call_error =
                                    "trace:jni-handler-bound";
                        } else if (call_error != nullptr)
                            *call_error =
                                "trace:jni-handler-callback-not-proxy";
                    }
                }
            }
            if (NormalizeCompatibilityType(type) ==
                    "com/google/androidgamesdk/ChoreographerCallback" &&
                method->second.signature == "(J)V") {
                // Swappy constructs its frame callback with the native
                // pacer handle; remember it so the launcher can deliver
                // nOnChoreographer(cookie, frameTimeNanos) vsync ticks.
                std::uint64_t cookie = 0;
                std::string ignored;
                if (ReadGuestGeneralArgument(address_space_, (*registers)[3],
                                             &cookie, &ignored, 0) &&
                    cookie != 0) {
                    choreographer_callbacks_.push_back({reference, cookie});
                }
            }
            if (scanner_stream_identity != 0) {
                scanners_.emplace(
                    reference,
                    ScannerStorage{scanner_stream_identity, {}, false});
            }
            if (NormalizeCompatibilityType(type) ==
                "android/app/AlertDialog$Builder") {
                alert_dialog_builders_.emplace(
                    reference, AlertDialogBuilderStorage{});
            }
            TrackLocalReferenceLocked(reference);
            (*registers)[0] = reference;
            return true;
        };
    const auto new_string_utf =
        [this](Aarch64ImportRegistry::Registers* registers,
               std::string* call_error) {
            if (registers == nullptr ||
                !IsCurrentEnvironment((*registers)[0])) {
                SetError(call_error,
                         "guest JNI NewStringUTF environment is invalid");
                return false;
            }
            if (backend_ != nullptr) {
                SetError(call_error,
                         "guest JNI NewStringUTF is unavailable with an active backend");
                return false;
            }
            std::string value;
            if (!ReadGuestStringValue(address_space_, (*registers)[1],
                                      &value, call_error))
                return false;
            std::uint64_t reference = 0;
            if (!CreateCompatibilityStringReference(value, &reference,
                                                    call_error))
                return false;
            (*registers)[0] = reference;
            return true;
        };
    const auto is_instance_of =
        [this](Aarch64ImportRegistry::Registers* registers,
               std::string* call_error) {
            if (registers == nullptr ||
                !IsCurrentEnvironment((*registers)[0])) {
                SetError(call_error,
                         "guest JNI IsInstanceOf environment is invalid");
                return false;
            }
            const std::lock_guard lock(mutex_);
            if (backend_ != nullptr) {
                SetError(call_error,
                         "guest JNI IsInstanceOf is unavailable with an active backend");
                return false;
            }
            const auto local_target =
                FindOwnedLocalClassReferenceLocked((*registers)[2]);
            const auto global_target =
                global_class_references_.find((*registers)[2]);
            if (local_target == class_references_.end() &&
                global_target == global_class_references_.end()) {
                SetError(call_error,
                         "guest JNI IsInstanceOf class is invalid");
                return false;
            }
            if ((*registers)[1] == 0) {
                (*registers)[0] = 1;
                return true;
            }
            std::string_view source_type;
            const std::optional<std::string_view> object_type =
                ObjectTypeLocked((*registers)[1]);
            if (object_type) {
                source_type = *object_type;
            } else if (IsClassReferenceLocked((*registers)[1])) {
                source_type = "java/lang/Class";
            } else {
                SetError(call_error,
                         "guest JNI IsInstanceOf object is invalid");
                return false;
            }
            const std::string& target_type =
                local_target != class_references_.end()
                    ? local_target->second.type
                    : global_target->second.type;
            (*registers)[0] =
                IsCompatibilityAssignable(source_type, target_type) ? 1 : 0;
            return true;
        };
    const auto get_string_utf_length =
        [this](Aarch64ImportRegistry::Registers* registers,
               std::string* call_error) {
            if (registers == nullptr ||
                !IsCurrentEnvironment((*registers)[0])) {
                SetError(call_error,
                         "guest JNI GetStringUTFLength environment is invalid");
                return false;
            }
            if (backend_ != nullptr) {
                SetError(call_error,
                         "guest JNI GetStringUTFLength is unavailable with an active backend");
                return false;
            }
            const std::lock_guard lock(mutex_);
            const auto local_string =
                FindOwnedLocalObjectReferenceLocked((*registers)[1]);
            const auto global_string =
                global_object_references_.find((*registers)[1]);
            if ((local_string == object_references_.end() &&
                 global_string == global_object_references_.end()) ||
                (local_string != object_references_.end()
                     ? local_string->second.type
                     : global_string->second.type) !=
                    "Ljava/lang/String;") {
                SetError(call_error,
                         "guest JNI GetStringUTFLength string is invalid");
                return false;
            }
            const std::string& value =
                local_string != object_references_.end()
                    ? local_string->second.string_value
                    : global_string->second.string_value;
            std::vector<std::uint16_t> characters;
            if (!DecodeModifiedUtf8(value, &characters, call_error))
                return false;
            (*registers)[0] = EncodeModifiedUtf8(characters).size();
            return true;
        };
    const auto get_string_utf_chars =
        [this](Aarch64ImportRegistry::Registers* registers,
               std::string* call_error) {
            if (registers == nullptr ||
                !IsCurrentEnvironment((*registers)[0])) {
                SetError(call_error,
                         "guest JNI GetStringUTFChars environment is invalid");
                return false;
            }
            if (backend_ != nullptr) {
                SetError(call_error,
                         "guest JNI GetStringUTFChars is unavailable with an active backend");
                return false;
            }
            const std::lock_guard lock(mutex_);
            const bool null_string_recovery =
                (*registers)[1] == 0 && backend_ == nullptr;
            const std::optional<std::uint64_t> identity =
                ReferenceIdentityLocked((*registers)[1]);
            const auto local_string =
                FindOwnedLocalObjectReferenceLocked((*registers)[1]);
            const auto global_string =
                global_object_references_.find((*registers)[1]);
            if (!identity ||
                (local_string == object_references_.end() &&
                 global_string == global_object_references_.end()) ||
                (local_string != object_references_.end()
                     ? local_string->second.type
                     : global_string->second.type) !=
                    "Ljava/lang/String;") {
                if (null_string_recovery) {
                    std::fprintf(stdout,
                        "ISOMR_CTR_NULL_STRING_RECOVERY value=%s\n",
                        exception_pending().message.c_str());
                    std::string encoded = exception_pending().message;
                    encoded.push_back('\0');
                    const std::uint64_t allocation =
                        heap_->Callocate(encoded.size(), 1);
                    if (allocation == 0 ||
                        !address_space_->Write(allocation, encoded.data(),
                                               encoded.size())) {
                        if (allocation != 0) {
                            std::string ignored;
                            (void)heap_->Release(allocation, &ignored);
                        }
                        SetError(call_error,
                                 "guest JNI GetStringUTFChars allocation failed");
                        return false;
                    }
                    if ((*registers)[2] != 0) {
                        const std::uint8_t is_copy = 1;
                        (void)address_space_->Write((*registers)[2], &is_copy,
                                                    sizeof(is_copy));
                    }
                    string_utf_character_buffers_.emplace(allocation, 0);
                    (*registers)[0] = allocation;
                    return true;
                }
                SetError(call_error,
                         "guest JNI GetStringUTFChars string is invalid ref=0x" +
                         ([](std::uint64_t v) { char b[24]; snprintf(b, sizeof(b), "%llx", (unsigned long long)v); return std::string(b); })((*registers)[1]));
                return false;
            }
            const std::string& value =
                local_string != object_references_.end()
                    ? local_string->second.string_value
                    : global_string->second.string_value;
            std::vector<std::uint16_t> characters;
            if (!DecodeModifiedUtf8(value, &characters, call_error))
                return false;
            std::string encoded = EncodeModifiedUtf8(characters);
            encoded.push_back('\0');
            const std::uint64_t allocation =
                heap_->Callocate(encoded.size(), 1);
            if (allocation == 0 ||
                !address_space_->Write(allocation, encoded.data(),
                                       encoded.size())) {
                if (allocation != 0) {
                    std::string ignored;
                    (void)heap_->Release(allocation, &ignored);
                }
                SetError(call_error,
                         "guest JNI GetStringUTFChars allocation failed");
                return false;
            }
            if ((*registers)[2] != 0) {
                const std::uint8_t is_copy = 1;
                if (!address_space_->Write((*registers)[2], &is_copy,
                                           sizeof(is_copy))) {
                    std::string ignored;
                    (void)heap_->Release(allocation, &ignored);
                    SetError(call_error,
                             "guest JNI GetStringUTFChars isCopy output is invalid");
                    return false;
                }
            }
            string_utf_character_buffers_.emplace(allocation, *identity);
            std::fprintf(stdout, "ISOMR_CTR_GETSTRUTF value=%s\n",
                         value.c_str());
            if (value.empty()) {
                std::fprintf(stderr,
                             "ISOMR_GETSTRUTF_EMPTY lr=%llx\n",
                             static_cast<unsigned long long>(
                                 registers->callee_saved_general.back()));
                std::fflush(stderr);
            }
            (*registers)[0] = allocation;
            return true;
        };
    const auto release_string_utf_chars =
        [this](Aarch64ImportRegistry::Registers* registers,
               std::string* call_error) {
            if (registers == nullptr ||
                !IsCurrentEnvironment((*registers)[0])) {
                SetError(call_error,
                         "guest JNI ReleaseStringUTFChars environment is invalid");
                return false;
            }
            if (backend_ != nullptr) {
                SetError(call_error,
                         "guest JNI ReleaseStringUTFChars is unavailable with an active backend");
                return false;
            }
            const std::lock_guard lock(mutex_);
            const std::optional<std::uint64_t> identity =
                ReferenceIdentityLocked((*registers)[1]);
            const auto buffer =
                string_utf_character_buffers_.find((*registers)[2]);
            if (buffer == string_utf_character_buffers_.end() ||
                (!identity && buffer->second != 0) ||
                (identity && buffer->second != *identity &&
                 buffer->second != 0)) {
                SetError(call_error,
                         "guest JNI ReleaseStringUTFChars ownership is invalid");
                return false;
            }
            if (!heap_->Release(buffer->first, call_error)) return false;
            string_utf_character_buffers_.erase(buffer);
            return true;
        };
    const auto get_string_chars =
        [this](Aarch64ImportRegistry::Registers* registers,
               std::string* call_error) {
            if (registers == nullptr ||
                !IsCurrentEnvironment((*registers)[0])) {
                SetError(call_error,
                         "guest JNI GetStringChars environment is invalid");
                return false;
            }
            if (backend_ != nullptr) {
                SetError(call_error,
                         "guest JNI GetStringChars is unavailable with an active backend");
                return false;
            }
            const std::lock_guard lock(mutex_);
            const std::optional<std::uint64_t> identity =
                ReferenceIdentityLocked((*registers)[1]);
            const auto local_string =
                object_references_.find((*registers)[1]);
            const auto global_string =
                global_object_references_.find((*registers)[1]);
            if (!identity ||
                (local_string == object_references_.end() &&
                 global_string == global_object_references_.end()) ||
                (local_string != object_references_.end()
                     ? local_string->second.type
                     : global_string->second.type) !=
                    "Ljava/lang/String;") {
                SetError(call_error,
                         "guest JNI GetStringChars string is invalid");
                return false;
            }
            const std::string& value =
                local_string != object_references_.end()
                    ? local_string->second.string_value
                    : global_string->second.string_value;
            std::vector<std::uint16_t> characters;
            if (!DecodeModifiedUtf8(value, &characters, call_error))
                return false;
            characters.push_back(0);
            const std::uint64_t allocation =
                heap_->Callocate(characters.size(), sizeof(std::uint16_t));
            if (allocation == 0 ||
                !address_space_->Write(allocation, characters.data(),
                                       characters.size() *
                                           sizeof(std::uint16_t))) {
                if (allocation != 0) {
                    std::string ignored;
                    (void)heap_->Release(allocation, &ignored);
                }
                SetError(call_error,
                         "guest JNI GetStringChars allocation failed");
                return false;
            }
            if ((*registers)[2] != 0) {
                const std::uint8_t is_copy = 1;
                if (!address_space_->Write((*registers)[2], &is_copy,
                                           sizeof(is_copy))) {
                    std::string ignored;
                    (void)heap_->Release(allocation, &ignored);
                    SetError(call_error,
                             "guest JNI GetStringChars isCopy output is invalid");
                    return false;
                }
            }
            string_character_buffers_.emplace(allocation, *identity);
            (*registers)[0] = allocation;
            return true;
        };
    const auto new_string =
        [this](Aarch64ImportRegistry::Registers* registers,
               std::string* call_error) {
            if (registers == nullptr ||
                !IsCurrentEnvironment((*registers)[0])) {
                SetError(call_error,
                         "guest JNI NewString environment is invalid");
                return false;
            }
            if (backend_ != nullptr) {
                SetError(call_error,
                         "guest JNI NewString is unavailable with an active backend");
                return false;
            }
            const std::uint64_t length = (*registers)[2];
            if (length > 4096 || (length != 0 && (*registers)[1] == 0)) {
                SetError(call_error,
                         "guest JNI NewString input is invalid or too long");
                return false;
            }
            std::vector<std::uint16_t> characters(
                static_cast<std::size_t>(length));
            if (length != 0 &&
                !address_space_->Read((*registers)[1], characters.data(),
                                      characters.size() *
                                          sizeof(std::uint16_t))) {
                SetError(call_error,
                         "guest JNI NewString characters could not be read");
                return false;
            }
            const std::string value = EncodeModifiedUtf8(characters);
            std::uint64_t reference = 0;
            if (!CreateCompatibilityStringReference(value, &reference,
                                                    call_error))
                return false;
            (*registers)[0] = reference;
            return true;
        };
    const auto get_string_length =
        [this](Aarch64ImportRegistry::Registers* registers,
               std::string* call_error) {
            if (registers == nullptr ||
                !IsCurrentEnvironment((*registers)[0])) {
                SetError(call_error,
                         "guest JNI GetStringLength environment is invalid");
                return false;
            }
            if (backend_ != nullptr) {
                SetError(call_error,
                         "guest JNI GetStringLength is unavailable with an active backend");
                return false;
            }
            const std::lock_guard lock(mutex_);
            const auto local_string =
                FindOwnedLocalObjectReferenceLocked((*registers)[1]);
            const auto global_string =
                global_object_references_.find((*registers)[1]);
            if ((local_string == object_references_.end() &&
                 global_string == global_object_references_.end()) ||
                (local_string != object_references_.end()
                     ? local_string->second.type
                     : global_string->second.type) !=
                    "Ljava/lang/String;") {
                const char* resolved_type =
                    local_string != object_references_.end()
                        ? local_string->second.type.c_str()
                        : (global_string != global_object_references_.end()
                               ? global_string->second.type.c_str()
                               : "<none>");
                std::fprintf(stderr,
                             "ISOMR_JNI_GETSTRLEN_BAD ref=%llx type=%s\n",
                             (unsigned long long)(*registers)[1],
                             resolved_type);
                std::fflush(stderr);
                SetError(call_error,
                         "guest JNI GetStringLength string is invalid");
                return false;
            }
            const std::string& value =
                local_string != object_references_.end()
                    ? local_string->second.string_value
                    : global_string->second.string_value;
            std::vector<std::uint16_t> characters;
            if (!DecodeModifiedUtf8(value, &characters, call_error))
                return false;
            (*registers)[0] = characters.size();
            return true;
        };
    const auto release_string_chars =
        [this](Aarch64ImportRegistry::Registers* registers,
               std::string* call_error) {
            if (registers == nullptr ||
                !IsCurrentEnvironment((*registers)[0])) {
                SetError(call_error,
                         "guest JNI ReleaseStringChars environment is invalid");
                return false;
            }
            if (backend_ != nullptr) {
                SetError(call_error,
                         "guest JNI ReleaseStringChars is unavailable with an active backend");
                return false;
            }
            const std::lock_guard lock(mutex_);
            const std::optional<std::uint64_t> identity =
                ReferenceIdentityLocked((*registers)[1]);
            const auto buffer =
                string_character_buffers_.find((*registers)[2]);
            if (!identity || buffer == string_character_buffers_.end() ||
                buffer->second != *identity) {
                SetError(call_error,
                         "guest JNI ReleaseStringChars ownership is invalid");
                return false;
            }
            if (!heap_->Release(buffer->first, call_error)) return false;
            string_character_buffers_.erase(buffer);
            return true;
        };
    const auto get_array_length =
        [this](Aarch64ImportRegistry::Registers* registers,
               std::string* call_error) {
            if (registers == nullptr ||
                !IsCurrentEnvironment((*registers)[0])) {
                SetError(call_error,
                         "guest JNI GetArrayLength environment is invalid");
                return false;
            }
            if (backend_ != nullptr) {
                SetError(call_error,
                         "guest JNI GetArrayLength is unavailable with an active backend");
                return false;
            }
            const std::lock_guard lock(mutex_);
            const std::optional<std::uint64_t> identity =
                ReferenceIdentityLocked((*registers)[1]);
            const std::optional<std::string_view> type =
                ObjectTypeLocked((*registers)[1]);
            const auto array = identity ? byte_arrays_.find(*identity)
                                        : byte_arrays_.end();
            const auto int_array = identity ? int_arrays_.find(*identity)
                                            : int_arrays_.end();
            const auto long_array = identity ? long_arrays_.find(*identity)
                                             : long_arrays_.end();
            const auto object_array = identity
                                          ? object_arrays_.find(*identity)
                                          : object_arrays_.end();
            if (identity && type && *type == "[B" &&
                array != byte_arrays_.end()) {
                (*registers)[0] = array->second.size();
                return true;
            }
            if (identity && type && *type == "[I" &&
                int_array != int_arrays_.end()) {
                (*registers)[0] = int_array->second.size();
                return true;
            }
            if (identity && type && *type == "[J" &&
                long_array != long_arrays_.end()) {
                (*registers)[0] = long_array->second.size();
                return true;
            }
            if (identity && type && object_array != object_arrays_.end() &&
                object_array->second.type == *type) {
                (*registers)[0] = object_array->second.elements.size();
                return true;
            }
            {
                std::ostringstream detail;
                detail << "guest JNI GetArrayLength array is invalid"
                       << " reference=0x" << std::hex << (*registers)[1]
                       << " identity=";
                if (identity)
                    detail << "0x" << *identity;
                else
                    detail << "none";
                detail << " type=";
                if (type)
                    detail << *type;
                else
                    detail << "none";
                detail << " reference-kind=";
                if (object_references_.contains((*registers)[1]))
                    detail << "local-object";
                else if (global_object_references_.contains((*registers)[1]))
                    detail << "global-object";
                else if (class_references_.contains((*registers)[1]))
                    detail << "local-class";
                else if (global_class_references_.contains((*registers)[1]))
                    detail << "global-class";
                else if ((*registers)[1] == 0)
                    detail << "null";
                else
                    detail << "unknown";
                detail << " byte-storage="
                       << (array != byte_arrays_.end() ? "present" : "absent");
                detail << " int-storage="
                       << (int_array != int_arrays_.end() ? "present"
                                                          : "absent");
                detail << " long-storage="
                       << (long_array != long_arrays_.end() ? "present"
                                                            : "absent");
                detail << " object-storage="
                       << (object_array != object_arrays_.end() ? "present"
                                                                : "absent");
                std::uint64_t method_reference = 0;
                const auto local_object =
                    object_references_.find((*registers)[1]);
                if (local_object != object_references_.end())
                    method_reference = local_object->second.method_reference;
                else {
                    const auto global_object =
                        global_object_references_.find((*registers)[1]);
                    if (global_object != global_object_references_.end())
                        method_reference =
                            global_object->second.method_reference;
                }
                if (method_reference != 0) {
                    detail << " method-reference=0x" << method_reference;
                    const auto method = method_references_.find(method_reference);
                    if (method != method_references_.end())
                        detail << " created-by=" << method->second.class_type
                               << "." << method->second.name
                               << method->second.signature;
                }
                SetError(call_error, detail.str());
                return false;
            }
        };
    const auto snapshot_object_array_element_locked =
        [this](std::uint64_t reference,
               std::optional<ObjectArrayElement>* snapshot,
               std::string* java_type, std::string* call_error) {
            if (snapshot == nullptr || java_type == nullptr) return false;
            if (reference == 0) {
                snapshot->reset();
                java_type->clear();
                return true;
            }
            const std::optional<std::uint64_t> identity =
                ReferenceIdentityLocked(reference);
            if (!identity) {
                SetError(call_error,
                         "guest JNI object-array element reference is invalid");
                return false;
            }
            const auto local_class =
                FindOwnedLocalClassReferenceLocked(reference);
            const auto global_class = global_class_references_.find(reference);
            if (local_class != class_references_.end() ||
                global_class != global_class_references_.end()) {
                const std::string& type =
                    local_class != class_references_.end()
                        ? local_class->second.type
                        : global_class->second.type;
                *snapshot = ObjectArrayElement{true, *identity, 0, type, {}};
                *java_type = "Ljava/lang/Class;";
                return true;
            }
            const auto local_object =
                FindOwnedLocalObjectReferenceLocked(reference);
            const auto global_object = global_object_references_.find(reference);
            if (local_object == object_references_.end() &&
                global_object == global_object_references_.end()) {
                SetError(call_error,
                         "guest JNI object-array element provenance is invalid");
                return false;
            }
            const bool local = local_object != object_references_.end();
            const std::uint64_t method =
                local ? local_object->second.method_reference
                      : global_object->second.method_reference;
            const std::string& type =
                local ? local_object->second.type : global_object->second.type;
            const std::string& value = local ? local_object->second.string_value
                                             : global_object->second.string_value;
            *snapshot =
                ObjectArrayElement{false, *identity, method, type, value};
            *java_type = type;
            return true;
        };
    const auto new_object_array =
        [this, snapshot_object_array_element_locked](
            Aarch64ImportRegistry::Registers* registers,
            std::string* call_error) {
            if (registers == nullptr ||
                !IsCurrentEnvironment((*registers)[0])) {
                SetError(call_error,
                         "guest JNI NewObjectArray environment is invalid");
                return false;
            }
            if (backend_ != nullptr) {
                SetError(call_error,
                         "guest JNI NewObjectArray is unavailable with an active backend");
                return false;
            }
            const std::int32_t signed_length = static_cast<std::int32_t>(
                static_cast<std::uint32_t>((*registers)[1]));
            if (signed_length < 0 ||
                static_cast<std::uint32_t>(signed_length) >
                    kMaximumObjectArrayLength) {
                SetError(call_error,
                         "guest JNI NewObjectArray length is invalid or too large");
                return false;
            }
            const std::lock_guard lock(mutex_);
            const std::optional<std::string_view> class_type =
                ClassTypeLocked((*registers)[2]);
            if (!class_type || !IsClassReferenceLocked((*registers)[2])) {
                SetError(call_error,
                         "guest JNI NewObjectArray element class is invalid");
                return false;
            }
            const std::string element_type =
                class_type->starts_with('[')
                    ? std::string(*class_type)
                    : "L" + std::string(*class_type) + ";";
            std::optional<ObjectArrayElement> initial;
            std::string initial_type;
            if (!snapshot_object_array_element_locked(
                    (*registers)[3], &initial, &initial_type, call_error))
                return false;
            if (initial && element_type != "Ljava/lang/Object;" &&
                initial_type != element_type) {
                SetError(call_error,
                         "guest JNI NewObjectArray initial element type is incompatible");
                return false;
            }
            std::vector<std::optional<ObjectArrayElement>> elements;
            try {
                elements.assign(static_cast<std::size_t>(signed_length),
                                initial);
            } catch (const std::bad_alloc&) {
                SetError(call_error,
                         "guest JNI NewObjectArray storage allocation failed");
                return false;
            }
            const std::string array_type = "[" + element_type;
            const std::uint64_t reference = AllocateCompatibilityReference(
                heap_, CompatibilityReferenceKind::kObject, array_type, {},
                call_error);
            if (reference == 0) return false;
            object_references_.emplace(
                reference,
                ObjectReference{reference, 0, array_type, {}, 0});
            TrackLocalReferenceLocked(reference);
            object_arrays_.emplace(
                reference,
                ObjectArrayStorage{array_type, element_type,
                                   std::move(elements)});
            (*registers)[0] = reference;
            return true;
        };
    const auto get_object_array_element =
        [this](Aarch64ImportRegistry::Registers* registers,
               std::string* call_error) {
            if (registers == nullptr ||
                !IsCurrentEnvironment((*registers)[0])) {
                SetError(call_error,
                         "guest JNI GetObjectArrayElement environment is invalid");
                return false;
            }
            if (backend_ != nullptr) {
                SetError(call_error,
                         "guest JNI GetObjectArrayElement is unavailable with an active backend");
                return false;
            }
            const std::int32_t index = static_cast<std::int32_t>(
                static_cast<std::uint32_t>((*registers)[2]));
            const std::lock_guard lock(mutex_);
            const std::optional<std::uint64_t> identity =
                ReferenceIdentityLocked((*registers)[1]);
            const auto array = identity ? object_arrays_.find(*identity)
                                        : object_arrays_.end();
            if (!identity || array == object_arrays_.end() || index < 0 ||
                static_cast<std::size_t>(index) >=
                    array->second.elements.size()) {
                SetError(call_error,
                         "guest JNI GetObjectArrayElement array or index is invalid");
                return false;
            }
            const std::optional<ObjectArrayElement>& element =
                array->second.elements[static_cast<std::size_t>(index)];
            if (!element) {
                (*registers)[0] = 0;
                return true;
            }
            const CompatibilityReferenceKind kind =
                element->is_class
                    ? CompatibilityReferenceKind::kClass
                    : IsStringTypeName(element->type)
                          ? CompatibilityReferenceKind::kString
                          : CompatibilityReferenceKind::kObject;
            const std::uint64_t reference = AllocateCompatibilityReference(
                heap_, kind, element->type, element->string_value,
                call_error);
            if (reference == 0) return false;
            if (element->is_class) {
                class_references_.emplace(
                    reference,
                    ClassReference{element->identity, element->type, 0});
            } else {
                object_references_.emplace(
                    reference,
                    ObjectReference{element->identity,
                                    element->method_reference, element->type,
                                    element->string_value, 0});
            }
            TrackLocalReferenceLocked(reference);
            (*registers)[0] = reference;
            return true;
        };
    const auto set_object_array_element =
        [this, snapshot_object_array_element_locked](
            Aarch64ImportRegistry::Registers* registers,
            std::string* call_error) {
            if (registers == nullptr ||
                !IsCurrentEnvironment((*registers)[0])) {
                SetError(call_error,
                         "guest JNI SetObjectArrayElement environment is invalid");
                return false;
            }
            if (backend_ != nullptr) {
                SetError(call_error,
                         "guest JNI SetObjectArrayElement is unavailable with an active backend");
                return false;
            }
            const std::int32_t index = static_cast<std::int32_t>(
                static_cast<std::uint32_t>((*registers)[2]));
            const std::lock_guard lock(mutex_);
            const std::optional<std::uint64_t> identity =
                ReferenceIdentityLocked((*registers)[1]);
            const auto array = identity ? object_arrays_.find(*identity)
                                        : object_arrays_.end();
            if (!identity || array == object_arrays_.end() || index < 0 ||
                static_cast<std::size_t>(index) >=
                    array->second.elements.size()) {
                SetError(call_error,
                         "guest JNI SetObjectArrayElement array or index is invalid");
                return false;
            }
            std::optional<ObjectArrayElement> element;
            std::string element_type;
            if (!snapshot_object_array_element_locked(
                    (*registers)[3], &element, &element_type, call_error))
                return false;
            if (element &&
                array->second.element_type != "Ljava/lang/Object;" &&
                element_type != array->second.element_type) {
                SetError(call_error,
                         "guest JNI SetObjectArrayElement value type is incompatible");
                return false;
            }
            array->second.elements[static_cast<std::size_t>(index)] =
                std::move(element);
            (*registers)[0] = 0;
            return true;
        };
    const auto new_byte_array =
        [this](Aarch64ImportRegistry::Registers* registers,
               std::string* call_error) {
            if (registers == nullptr ||
                !IsCurrentEnvironment((*registers)[0])) {
                SetError(call_error,
                         "guest JNI NewByteArray environment is invalid");
                return false;
            }
            if (backend_ != nullptr) {
                SetError(call_error,
                         "guest JNI NewByteArray is unavailable with an active backend");
                return false;
            }
            const std::int32_t signed_length = static_cast<std::int32_t>(
                static_cast<std::uint32_t>((*registers)[1]));
            if (signed_length < 0 ||
                static_cast<std::uint32_t>(signed_length) >
                    kMaximumByteArraySize) {
                SetError(call_error,
                         "guest JNI NewByteArray length is invalid or too large");
                return false;
            }
            std::vector<std::uint8_t> bytes;
            try {
                bytes.assign(static_cast<std::size_t>(signed_length), 0);
            } catch (const std::bad_alloc&) {
                SetError(call_error,
                         "guest JNI NewByteArray storage allocation failed");
                return false;
            }
            const std::uint64_t reference = AllocateCompatibilityReference(
                heap_, CompatibilityReferenceKind::kObject, "[B", {},
                call_error);
            if (reference == 0) return false;
            const std::lock_guard lock(mutex_);
            object_references_.emplace(
                reference,
                ObjectReference{reference, 0, "[B", {}, 0});
            TrackLocalReferenceLocked(reference);
            byte_arrays_.emplace(reference, std::move(bytes));
            (*registers)[0] = reference;
            return true;
        };
    const auto new_int_array =
        [this](Aarch64ImportRegistry::Registers* registers,
               std::string* call_error) {
            if (registers == nullptr ||
                !IsCurrentEnvironment((*registers)[0])) {
                SetError(call_error,
                         "guest JNI NewIntArray environment is invalid");
                return false;
            }
            const std::int32_t signed_length = static_cast<std::int32_t>(
                static_cast<std::uint32_t>((*registers)[1]));
            if (signed_length < 0 ||
                static_cast<std::uint32_t>(signed_length) >
                    kMaximumByteArraySize / 4) {
                SetError(call_error,
                         "guest JNI NewIntArray length is invalid or too large");
                return false;
            }
            std::vector<std::uint32_t> elements;
            try {
                elements.assign(static_cast<std::size_t>(signed_length), 0);
            } catch (const std::bad_alloc&) {
                SetError(call_error,
                         "guest JNI NewIntArray storage allocation failed");
                return false;
            }
            const std::uint64_t reference = AllocateCompatibilityReference(
                heap_, CompatibilityReferenceKind::kObject, "[I", {},
                call_error);
            if (reference == 0) return false;
            const std::lock_guard lock(mutex_);
            object_references_.emplace(
                reference,
                ObjectReference{reference, 0, "[I", {}, 0});
            TrackLocalReferenceLocked(reference);
            int_arrays_.emplace(reference, std::move(elements));
            (*registers)[0] = reference;
            return true;
        };
    const auto new_long_array =
        [this](Aarch64ImportRegistry::Registers* registers,
               std::string* call_error) {
            const std::int32_t signed_length = static_cast<std::int32_t>(
                static_cast<std::uint32_t>((*registers)[1]));
            if (signed_length < 0 ||
                static_cast<std::uint32_t>(signed_length) >
                    kMaximumByteArraySize / 8) {
                SetError(call_error,
                         "guest JNI NewLongArray length is invalid");
                return false;
            }
            std::vector<std::uint64_t> elements;
            try {
                elements.assign(static_cast<std::size_t>(signed_length), 0);
            } catch (const std::bad_alloc&) {
                SetError(call_error,
                         "guest JNI NewLongArray storage allocation failed");
                return false;
            }
            const std::uint64_t reference = AllocateCompatibilityReference(
                heap_, CompatibilityReferenceKind::kObject, "[J", {},
                call_error);
            if (reference == 0) return false;
            const std::lock_guard lock(mutex_);
            object_references_.emplace(
                reference,
                ObjectReference{reference, 0, "[J", {}, 0});
            TrackLocalReferenceLocked(reference);
            long_arrays_.emplace(reference, std::move(elements));
            (*registers)[0] = reference;
            return true;
        };
    const auto get_byte_array_elements =
        [this](Aarch64ImportRegistry::Registers* registers,
               std::string* call_error) {
            if (registers == nullptr ||
                !IsCurrentEnvironment((*registers)[0])) {
                SetError(call_error,
                         "guest JNI GetByteArrayElements environment is invalid");
                return false;
            }
            if (backend_ != nullptr) {
                SetError(call_error,
                         "guest JNI GetByteArrayElements is unavailable with an active backend");
                return false;
            }
            const std::lock_guard lock(mutex_);
            const std::optional<std::uint64_t> identity =
                ReferenceIdentityLocked((*registers)[1]);
            const std::optional<std::string_view> type =
                ObjectTypeLocked((*registers)[1]);
            const auto array = identity ? byte_arrays_.find(*identity)
                                        : byte_arrays_.end();
            if (!identity || !type || *type != "[B" ||
                array == byte_arrays_.end()) {
                SetError(call_error,
                         "guest JNI GetByteArrayElements byte array is invalid");
                return false;
            }
            const std::uint64_t buffer =
                heap_->Callocate(std::max<std::size_t>(array->second.size(), 1),
                                 1);
            if (buffer == 0 ||
                (!array->second.empty() &&
                 !address_space_->Write(buffer, array->second.data(),
                                        array->second.size()))) {
                if (buffer != 0) {
                    std::string ignored;
                    (void)heap_->Release(buffer, &ignored);
                }
                SetError(call_error,
                         "guest JNI GetByteArrayElements buffer allocation failed");
                return false;
            }
            if ((*registers)[2] != 0) {
                const std::uint8_t is_copy = 1;
                if (!address_space_->Write((*registers)[2], &is_copy,
                                           sizeof(is_copy))) {
                    std::string ignored;
                    (void)heap_->Release(buffer, &ignored);
                    SetError(call_error,
                             "guest JNI GetByteArrayElements isCopy output is invalid");
                    return false;
                }
            }
            byte_array_buffers_.emplace(
                buffer, ByteArrayBuffer{*identity, array->second.size()});
            (*registers)[0] = buffer;
            return true;
        };
    const auto release_byte_array_elements =
        [this](Aarch64ImportRegistry::Registers* registers,
               std::string* call_error) {
            if (registers == nullptr ||
                !IsCurrentEnvironment((*registers)[0])) {
                SetError(call_error,
                         "guest JNI ReleaseByteArrayElements environment is invalid");
                return false;
            }
            if (backend_ != nullptr) {
                SetError(call_error,
                         "guest JNI ReleaseByteArrayElements is unavailable with an active backend");
                return false;
            }
            const std::int32_t mode = static_cast<std::int32_t>(
                static_cast<std::uint32_t>((*registers)[3]));
            const std::lock_guard lock(mutex_);
            const std::optional<std::uint64_t> identity =
                ReferenceIdentityLocked((*registers)[1]);
            const std::optional<std::string_view> type =
                ObjectTypeLocked((*registers)[1]);
            const auto buffer = byte_array_buffers_.find((*registers)[2]);
            const auto array = identity ? byte_arrays_.find(*identity)
                                        : byte_arrays_.end();
            if (!identity || !type || *type != "[B" ||
                buffer == byte_array_buffers_.end() ||
                buffer->second.identity != *identity ||
                array == byte_arrays_.end() ||
                buffer->second.length != array->second.size() ||
                (mode != 0 && mode != 1 && mode != 2)) {
                SetError(call_error,
                         "guest JNI ReleaseByteArrayElements ownership or mode is invalid");
                return false;
            }
            if (mode != 2 && !array->second.empty() &&
                !address_space_->Read(buffer->first, array->second.data(),
                                      array->second.size())) {
                SetError(call_error,
                         "guest JNI ReleaseByteArrayElements buffer is unreadable");
                return false;
            }
            if (mode != 1) {
                if (!heap_->Release(buffer->first, call_error)) return false;
                byte_array_buffers_.erase(buffer);
            }
            (*registers)[0] = 0;
            return true;
        };
    const auto byte_array_region =
        [this](bool write_array,
               Aarch64ImportRegistry::Registers* registers,
               std::string* call_error) {
            if (registers == nullptr ||
                !IsCurrentEnvironment((*registers)[0])) {
                SetError(call_error,
                         "guest JNI byte-array region environment is invalid");
                return false;
            }
            if (backend_ != nullptr) {
                SetError(call_error,
                         "guest JNI byte-array regions are unavailable with an active backend");
                return false;
            }
            const std::int32_t signed_start = static_cast<std::int32_t>(
                static_cast<std::uint32_t>((*registers)[2]));
            const std::int32_t signed_length = static_cast<std::int32_t>(
                static_cast<std::uint32_t>((*registers)[3]));
            const std::lock_guard lock(mutex_);
            const std::optional<std::uint64_t> identity =
                ReferenceIdentityLocked((*registers)[1]);
            const std::optional<std::string_view> type =
                ObjectTypeLocked((*registers)[1]);
            const auto array = identity ? byte_arrays_.find(*identity)
                                        : byte_arrays_.end();
            if (!identity || !type || *type != "[B" ||
                array == byte_arrays_.end() || signed_start < 0 ||
                signed_length < 0) {
                SetError(call_error,
                         "guest JNI byte-array region or range is invalid");
                return false;
            }
            const std::size_t start =
                static_cast<std::size_t>(signed_start);
            const std::size_t length =
                static_cast<std::size_t>(signed_length);
            if (start > array->second.size() ||
                length > array->second.size() - start ||
                (length != 0 && (*registers)[4] == 0)) {
                SetError(call_error,
                         "guest JNI byte-array region exceeds its bounds");
                return false;
            }
            if (length != 0 &&
                !(write_array
                      ? address_space_->Read((*registers)[4],
                                             array->second.data() + start,
                                             length)
                      : address_space_->Write((*registers)[4],
                                              array->second.data() + start,
                                              length))) {
                SetError(call_error,
                         "guest JNI byte-array region buffer is invalid");
                return false;
            }
            (*registers)[0] = 0;
            return true;
        };
    const auto get_byte_array_region =
        [byte_array_region](Aarch64ImportRegistry::Registers* registers,
                            std::string* call_error) {
            return byte_array_region(false, registers, call_error);
        };
    const auto set_byte_array_region =
        [byte_array_region](Aarch64ImportRegistry::Registers* registers,
                            std::string* call_error) {
            return byte_array_region(true, registers, call_error);
        };
    // Get/Release< Int|Long >ArrayElements and Get/Set< Int|Long >ArrayRegion:
    // element-width variants of the byte-array handlers backed by
    // int_arrays_ (jint, "[I") and long_arrays_ (jlong, "[J").
    const auto get_wide_array_elements =
        [this](auto* arrays, auto* buffers, const char* type_tag,
               const char* op_name,
               Aarch64ImportRegistry::Registers* registers,
               std::string* call_error) {
            using Element =
                typename std::decay_t<decltype(arrays->begin()->second)>::
                    value_type;
            if (registers == nullptr ||
                !IsCurrentEnvironment((*registers)[0])) {
                SetError(call_error, std::string("guest JNI ") + op_name +
                                         " environment is invalid");
                return false;
            }
            if (backend_ != nullptr) {
                SetError(call_error,
                         std::string("guest JNI ") + op_name +
                             " is unavailable with an active backend");
                return false;
            }
            const std::lock_guard lock(mutex_);
            const std::optional<std::uint64_t> identity =
                ReferenceIdentityLocked((*registers)[1]);
            const std::optional<std::string_view> type =
                ObjectTypeLocked((*registers)[1]);
            const auto array =
                identity ? arrays->find(*identity) : arrays->end();
            if (!identity || !type || *type != type_tag ||
                array == arrays->end()) {
                SetError(call_error, std::string("guest JNI ") + op_name +
                                         " array is invalid");
                return false;
            }
            const std::uint64_t buffer = heap_->Callocate(
                std::max<std::size_t>(array->second.size(), 1),
                sizeof(Element));
            if (buffer == 0 ||
                (!array->second.empty() &&
                 !address_space_->Write(buffer, array->second.data(),
                                        array->second.size() *
                                            sizeof(Element)))) {
                if (buffer != 0) {
                    std::string ignored;
                    (void)heap_->Release(buffer, &ignored);
                }
                SetError(call_error, std::string("guest JNI ") + op_name +
                                         " buffer allocation failed");
                return false;
            }
            if ((*registers)[2] != 0) {
                const std::uint8_t is_copy = 1;
                if (!address_space_->Write((*registers)[2], &is_copy,
                                           sizeof(is_copy))) {
                    std::string ignored;
                    (void)heap_->Release(buffer, &ignored);
                    SetError(call_error, std::string("guest JNI ") + op_name +
                                             " isCopy output is invalid");
                    return false;
                }
            }
            buffers->emplace(buffer,
                             ByteArrayBuffer{*identity, array->second.size()});
            (*registers)[0] = buffer;
            return true;
        };
    const auto release_wide_array_elements =
        [this](auto* arrays, auto* buffers, const char* type_tag,
               const char* op_name,
               Aarch64ImportRegistry::Registers* registers,
               std::string* call_error) {
            using Element =
                typename std::decay_t<decltype(arrays->begin()->second)>::
                    value_type;
            if (registers == nullptr ||
                !IsCurrentEnvironment((*registers)[0])) {
                SetError(call_error, std::string("guest JNI ") + op_name +
                                         " environment is invalid");
                return false;
            }
            if (backend_ != nullptr) {
                SetError(call_error,
                         std::string("guest JNI ") + op_name +
                             " is unavailable with an active backend");
                return false;
            }
            const std::int32_t mode = static_cast<std::int32_t>(
                static_cast<std::uint32_t>((*registers)[3]));
            const std::lock_guard lock(mutex_);
            const std::optional<std::uint64_t> identity =
                ReferenceIdentityLocked((*registers)[1]);
            const std::optional<std::string_view> type =
                ObjectTypeLocked((*registers)[1]);
            const auto buffer = buffers->find((*registers)[2]);
            const auto array =
                identity ? arrays->find(*identity) : arrays->end();
            if (!identity || !type || *type != type_tag ||
                buffer == buffers->end() ||
                buffer->second.identity != *identity ||
                array == arrays->end() ||
                buffer->second.length != array->second.size() ||
                (mode != 0 && mode != 1 && mode != 2)) {
                SetError(call_error,
                         std::string("guest JNI ") + op_name +
                             " ownership or mode is invalid");
                return false;
            }
            if (mode != 2 && !array->second.empty() &&
                !address_space_->Read(buffer->first, array->second.data(),
                                      array->second.size() *
                                          sizeof(Element))) {
                SetError(call_error, std::string("guest JNI ") + op_name +
                                         " buffer is unreadable");
                return false;
            }
            if (mode != 1) {
                if (!heap_->Release(buffer->first, call_error)) return false;
                buffers->erase(buffer);
            }
            (*registers)[0] = 0;
            return true;
        };
    const auto wide_array_region =
        [this](auto* arrays, const char* type_tag, const char* op_name,
               bool write_array,
               Aarch64ImportRegistry::Registers* registers,
               std::string* call_error) {
            using Element =
                typename std::decay_t<decltype(arrays->begin()->second)>::
                    value_type;
            if (registers == nullptr ||
                !IsCurrentEnvironment((*registers)[0])) {
                SetError(call_error, std::string("guest JNI ") + op_name +
                                         " environment is invalid");
                return false;
            }
            if (backend_ != nullptr) {
                SetError(call_error,
                         std::string("guest JNI ") + op_name +
                             " is unavailable with an active backend");
                return false;
            }
            const std::int32_t signed_start = static_cast<std::int32_t>(
                static_cast<std::uint32_t>((*registers)[2]));
            const std::int32_t signed_length = static_cast<std::int32_t>(
                static_cast<std::uint32_t>((*registers)[3]));
            const std::lock_guard lock(mutex_);
            const std::optional<std::uint64_t> identity =
                ReferenceIdentityLocked((*registers)[1]);
            const std::optional<std::string_view> type =
                ObjectTypeLocked((*registers)[1]);
            const auto array =
                identity ? arrays->find(*identity) : arrays->end();
            if (!identity || !type || *type != type_tag ||
                array == arrays->end() || signed_start < 0 ||
                signed_length < 0) {
                SetError(call_error, std::string("guest JNI ") + op_name +
                                         " region or range is invalid");
                return false;
            }
            const std::size_t start =
                static_cast<std::size_t>(signed_start);
            const std::size_t length =
                static_cast<std::size_t>(signed_length);
            if (start > array->second.size() ||
                length > array->second.size() - start ||
                (length != 0 && (*registers)[4] == 0)) {
                SetError(call_error, std::string("guest JNI ") + op_name +
                                         " region exceeds its bounds");
                return false;
            }
            if (length != 0 &&
                !(write_array
                      ? address_space_->Read(
                            (*registers)[4],
                            array->second.data() + start,
                            length * sizeof(Element))
                      : address_space_->Write(
                            (*registers)[4],
                            array->second.data() + start,
                            length * sizeof(Element)))) {
                SetError(call_error, std::string("guest JNI ") + op_name +
                                         " region buffer is invalid");
                return false;
            }
            (*registers)[0] = 0;
            return true;
        };
    const auto get_int_array_elements =
        [this, get_wide_array_elements](
            Aarch64ImportRegistry::Registers* registers,
            std::string* call_error) {
            return get_wide_array_elements(&int_arrays_, &int_array_buffers_,
                                           "[I", "GetIntArrayElements",
                                           registers, call_error);
        };
    const auto release_int_array_elements =
        [this, release_wide_array_elements](
            Aarch64ImportRegistry::Registers* registers,
            std::string* call_error) {
            return release_wide_array_elements(
                &int_arrays_, &int_array_buffers_, "[I",
                "ReleaseIntArrayElements", registers, call_error);
        };
    const auto get_long_array_elements =
        [this, get_wide_array_elements](
            Aarch64ImportRegistry::Registers* registers,
            std::string* call_error) {
            return get_wide_array_elements(&long_arrays_, &long_array_buffers_,
                                           "[J", "GetLongArrayElements",
                                           registers, call_error);
        };
    const auto release_long_array_elements =
        [this, release_wide_array_elements](
            Aarch64ImportRegistry::Registers* registers,
            std::string* call_error) {
            return release_wide_array_elements(
                &long_arrays_, &long_array_buffers_, "[J",
                "ReleaseLongArrayElements", registers, call_error);
        };
    const auto get_int_array_region =
        [this, wide_array_region](Aarch64ImportRegistry::Registers* registers,
                                  std::string* call_error) {
            return wide_array_region(&int_arrays_, "[I", "GetIntArrayRegion",
                                     false, registers, call_error);
        };
    const auto set_int_array_region =
        [this, wide_array_region](Aarch64ImportRegistry::Registers* registers,
                                  std::string* call_error) {
            return wide_array_region(&int_arrays_, "[I", "SetIntArrayRegion",
                                     true, registers, call_error);
        };
    const auto get_long_array_region =
        [this, wide_array_region](Aarch64ImportRegistry::Registers* registers,
                                  std::string* call_error) {
            return wide_array_region(&long_arrays_, "[J",
                                     "GetLongArrayRegion", false, registers,
                                     call_error);
        };
    const auto set_long_array_region =
        [this, wide_array_region](Aarch64ImportRegistry::Registers* registers,
                                  std::string* call_error) {
            return wide_array_region(&long_arrays_, "[J",
                                     "SetLongArrayRegion", true, registers,
                                     call_error);
        };
    const auto call_object_method_v =
        [this](Aarch64ImportRegistry::Registers* registers,
               std::string* call_error) {
            if (registers == nullptr ||
                !IsCurrentEnvironment((*registers)[0]) ||
                (*registers)[3] == 0) {
                SetError(call_error,
                         "guest JNI CallObjectMethodV arguments are invalid");
                return false;
            }
            std::array<std::uint8_t, 32> arguments{};
            if (!address_space_->Read((*registers)[3], arguments.data(),
                                      arguments.size())) {
                SetError(call_error,
                         "guest JNI CallObjectMethodV va_list is invalid");
                return false;
            }
            const std::lock_guard lock(mutex_);
            const auto method = method_references_.find((*registers)[2]);
            if (method == method_references_.end() ||
                method->second.is_static) {
                if (backend_ == nullptr) {
                    // With no Java backend an object-method call on an
                    // unregistered or static method id has no target to
                    // reach. Return a live synthetic String reference rather
                    // than aborting the frame — the transition path calls
                    // GetStringUTFChars on the result, and a real (registered)
                    // handle lets follow-up calls resolve to further stubs
                    // where a bare null would re-crash on the next chained
                    // dispatch.
                    const auto recv_type = ObjectTypeLocked((*registers)[1]);
                    std::fprintf(stderr,
                                 "ISOMR_JNI_OBJECT_STUB mid=%llx found=%d "
                                 "static=%d recv=%llx rtype=%s\n",
                                 (unsigned long long)(*registers)[2],
                                 method != method_references_.end() ? 1 : 0,
                                 method != method_references_.end()
                                     ? (method->second.is_static ? 1 : 0)
                                     : 0,
                                 (unsigned long long)(*registers)[1],
                                 recv_type
                                     ? std::string(*recv_type).c_str()
                                     : "<none>");
                    std::fflush(stderr);
                    const std::uint64_t reference = AllocateJniReference(
                        heap_, true, CompatibilityReferenceKind::kString,
                        "Ljava/lang/String;", {}, call_error);
                    if (reference == 0) {
                        (*registers)[0] = 0;
                        return true;
                    }
                    object_references_.emplace(
                        reference,
                        ObjectReference{reference, (*registers)[2],
                                        "Ljava/lang/String;", {}, 0});
                    TrackLocalReferenceLocked(reference);
                    (*registers)[0] = reference;
                    return true;
                }
                SetError(call_error,
                         "guest JNI CallObjectMethodV method is invalid");
                return false;
            }
            // JNI defines jclass as an object-reference subtype. Unity's
            // initJni calls java/lang/Class.getClassLoader() on the jclass
            // returned by GetObjectClass, so rejecting class handles here
            // violates the JNI receiver contract.
            // GetActivity()Landroid/app/Activity; — the engine calls this
            // on the fake_activity heap block, which isn't tracked. Return
            // the receiver itself as the Activity reference.
            if (method->second.name == "GetActivity" &&
                method->second.signature == "()Landroid/app/Activity;") {
                (*registers)[0] = (*registers)[1];
                return true;
            }
            {
                const std::uint64_t receiver =
                    ReferenceIdentityLocked((*registers)[1])
                        .value_or((*registers)[1]);
                if (input_devices_.find(receiver) != input_devices_.end()) {
                    const std::string_view dev_name = method->second.name;
                    if (dev_name == "getName" ||
                        dev_name == "getDescriptor") {
                        std::uint64_t string_reference = 0;
                        if (!CreateCompatibilityStringReferenceLocked(
                                "ARMR Touchscreen", &string_reference,
                                call_error))
                            return false;
                        (*registers)[0] = string_reference;
                        return true;
                    }
                    if (dev_name == "getMotionRange") {
                        // MotionRange for the queried axis; report the full
                        // touch extent so Unity's axis scan reads a real span.
                        std::uint64_t axis = 0;
                        ReadGuestGeneralArgument(address_space_,
                                                 (*registers)[3], &axis,
                                                 call_error, 0);
                        const std::uint64_t reference =
                            AllocateCompatibilityReference(
                                heap_, CompatibilityReferenceKind::kObject,
                                "android/view/InputDevice$MotionRange", {},
                                call_error);
                        if (reference == 0) return false;
                        object_references_.emplace(
                            reference,
                            ObjectReference{
                                reference, (*registers)[2],
                                "android/view/InputDevice$MotionRange", {},
                                0});
                        input_ranges_.emplace(
                            reference,
                            InputRangeStorage{
                                static_cast<int>(axis), 0.0f,
                                axis == 1 ? 720.0f : 1280.0f});
                        TrackLocalReferenceLocked(reference);
                        (*registers)[0] = reference;
                        return true;
                    }
                    // hasKeys/getKeyCharacterMap and friends fall through to
                    // the generic object stub (a live reference keeps chained
                    // reads from crashing).
                }
            }
            const bool object_to_string_call =
                method->second.name == "toString" &&
                method->second.signature == "()Ljava/lang/String;" &&
                (method->second.class_type == "java/lang/Object" ||
                 method->second.class_type == "Ljava/lang/Object;");
            // A compat object that is still registered is a real receiver no
            // matter which attached thread issues the call; the per-thread
            // local-ref ownership table only governs cleanup timing. Unity's
            // main/render split and StringBuilder traffic hand objects across
            // JNI threads, so a registered object must dispatch even when the
            // calling thread did not create the ref.
            const bool known_object_receiver =
                object_references_.contains((*registers)[1]) ||
                global_object_references_.contains((*registers)[1]);
            if (!known_object_receiver &&
                !IsClassReferenceLocked((*registers)[1]) &&
                !object_to_string_call) {
                SetError(call_error,
                         "guest JNI CallObjectMethodV object is invalid for " +
                             method->second.class_type + "." +
                             method->second.name +
                             method->second.signature);
                return false;
            }
            const std::string& signature = method->second.signature;
            RecordHttpObservationEventLocked(
                HttpObservationEventKind::kObjectCall,
                method->second.class_type, method->second.name,
                method->second.signature);
            if (call_error != nullptr) {
                *call_error =
                    "trace:jni-call-object-" + method->second.class_type +
                    "." + method->second.name + signature;
            }
            // SexyAppFrameworkActivity methods called on the untracked
            // fake_activity heap block: serve known queries and return
            // the raw heap block for self-referencing calls.
            if (backend_ == nullptr) {
                if (method->second.name == "GetActivity" &&
                    signature == "()Landroid/app/Activity;") {
                    (*registers)[0] = (*registers)[1];
                    return true;
                }
                if (method->second.name == "StartTracing" ||
                    method->second.name == "StopTracing") {
                    (*registers)[0] = 0;
                    return true;
                }
                // Activity self: return the receiver for any unknown
                // Activity-returning method.
                if (signature == "()Lcom/popcap/SexyAppFramework/SexyAppFrameworkActivity;") {
                    (*registers)[0] = (*registers)[1];
                    return true;
                }
            }
            const std::size_t close = signature.find(')');
            const std::string normalized_method_class =
                NormalizeCompatibilityType(method->second.class_type);
            if (backend_ == nullptr &&
                normalized_method_class ==
                    "csdk/glucentralservices/util/AndroidPlatform" &&
                method->second.name == "getFromStringStore" &&
                method->second.signature ==
                    "(Ljava/lang/String;)Ljava/lang/String;") {
                // Resolve the lookup from the string store kept by
                // setToStringStore; a miss returns an empty string so the
                // engine treats it as a default value.
                std::uint64_t key_reference = 0;
                if (!ReadGuestGeneralArgument(address_space_,
                                              (*registers)[3],
                                              &key_reference, call_error)) {
                    return false;
                }
                std::string key;
                const auto local_key =
                    FindOwnedLocalObjectReferenceLocked(key_reference);
                const auto global_key =
                    global_object_references_.find(key_reference);
                if (local_key != object_references_.end() &&
                    IsStringTypeName(local_key->second.type)) {
                    key = local_key->second.string_value;
                } else if (global_key != global_object_references_.end() &&
                           IsStringTypeName(global_key->second.type)) {
                    key = global_key->second.string_value;
                }
                const auto stored = compat_string_store_.find(key);
                const std::string value = stored != compat_string_store_.end()
                                              ? stored->second
                                              : std::string();
                static std::atomic<int> miss_log{0};
                if (stored == compat_string_store_.end() &&
                    miss_log.fetch_add(1) < 24) {
                    std::fprintf(stderr,
                                 "ISOMR_STRING_STORE miss key=%s\n",
                                 key.c_str());
                    std::fflush(stderr);
                }
                const std::uint64_t reference = AllocateCompatibilityReference(
                    heap_, CompatibilityReferenceKind::kString,
                    "Ljava/lang/String;", value, call_error);
                if (reference == 0) {
                    SetError(call_error,
                             "guest getFromStringStore allocation failed");
                    return false;
                }
                object_references_.emplace(
                    reference,
                    ObjectReference{reference, 0, "Ljava/lang/String;", value,
                                    0});
                TrackLocalReferenceLocked(reference);
                (*registers)[0] = reference;
                return true;
            }
            if (backend_ == nullptr &&
                normalized_method_class == "java/lang/reflect/Method" &&
                (method->second.name == "getName" ||
                 method->second.name == "getDeclaringClass" ||
                 method->second.name == "toString")) {
                const auto local_reflected =
                    object_references_.find((*registers)[1]);
                const auto global_reflected =
                    global_object_references_.find((*registers)[1]);
                const std::uint64_t reflected_method_id =
                    local_reflected != object_references_.end() &&
                            NormalizeCompatibilityType(local_reflected->second.type) == "java/lang/reflect/Method"
                        ? local_reflected->second.method_reference
                        : global_reflected !=
                                      global_object_references_.end() &&
                                  NormalizeCompatibilityType(global_reflected->second.type) == "java/lang/reflect/Method"
                              ? global_reflected->second.method_reference
                              : 0;
                const auto reflected_method =
                    method_references_.find(reflected_method_id);
                if (reflected_method == method_references_.end()) {
                    SetError(call_error,
                             "guest JNI reflected Method receiver is invalid");
                    return false;
                }
                if (method->second.name == "getName" &&
                    signature == "()Ljava/lang/String;") {
                    const std::uint64_t reference =
                        AllocateCompatibilityReference(
                            heap_, CompatibilityReferenceKind::kString,
                            "Ljava/lang/String;",
                            reflected_method->second.name, call_error);
                    if (reference == 0) return false;
                    object_references_.emplace(
                        reference,
                        ObjectReference{reference, (*registers)[2],
                                        "Ljava/lang/String;",
                                        reflected_method->second.name, 0});
                    TrackLocalReferenceLocked(reference);
                    (*registers)[0] = reference;
                    return true;
                }

                if (method->second.name == "toString" &&
                    signature == "()Ljava/lang/String;" &&
                    reflected_method->second.class_type ==
                        "java/lang/Runnable" &&
                    reflected_method->second.name == "run" &&
                    reflected_method->second.signature == "()V") {
                    constexpr std::string_view kDescription =
                        "public abstract void java.lang.Runnable.run()";
                    const std::uint64_t reference =
                        AllocateCompatibilityReference(
                            heap_, CompatibilityReferenceKind::kString,
                            "Ljava/lang/String;", kDescription, call_error);
                    if (reference == 0) return false;
                    object_references_.emplace(
                        reference,
                        ObjectReference{reference, (*registers)[2],
                                        "Ljava/lang/String;",
                                        std::string(kDescription), 0});
                    TrackLocalReferenceLocked(reference);
                    (*registers)[0] = reference;
                    return true;
                }
                if (method->second.name == "getDeclaringClass" &&
                    signature == "()Ljava/lang/Class;") {
                    const std::uint64_t reference =
                        AllocateCompatibilityReference(
                            heap_, CompatibilityReferenceKind::kClass,
                            reflected_method->second.class_type, {},
                            call_error);
                    if (reference == 0) return false;
                    class_references_.emplace(
                        reference,
                        ClassReference{
                            reflected_method->second.class_identity,
                            reflected_method->second.class_type, 0});
                    TrackLocalReferenceLocked(reference);
                    (*registers)[0] = reference;
                    return true;
                }
                SetError(call_error,
                         "guest JNI reflected Method accessor signature is unsupported");
                return false;
            }
            if (backend_ == nullptr &&
                normalized_method_class == "android/content/Context" &&
                method->second.name == "getFilesDir" &&
                signature == "()Ljava/io/File;") {
                const std::optional<std::string_view> receiver_type =
                    ObjectTypeLocked((*registers)[1]);
                if (!receiver_type ||
                    !IsCompatibilityAssignable(*receiver_type,
                                               "android/content/Context")) {
                    SetError(call_error,
                             "guest JNI Context.getFilesDir receiver is invalid");
                    return false;
                }
                const std::string path =
                    "/data/data/" + compatibility_.package_name + "/files";
                const std::uint64_t reference =
                    AllocateCompatibilityReference(
                        heap_, CompatibilityReferenceKind::kObject,
                        "Ljava/io/File;", path, call_error);
                if (reference == 0) return false;
                object_references_.emplace(
                    reference,
                    ObjectReference{reference, (*registers)[2],
                                    "Ljava/io/File;", path, 0});
                TrackLocalReferenceLocked(reference);
                (*registers)[0] = reference;
                return true;
            }
            if (backend_ == nullptr &&
                normalized_method_class == "android/content/Context" &&
                method->second.name == "getExternalFilesDir" &&
                signature == "(Ljava/lang/String;)Ljava/io/File;") {
                const std::optional<std::string_view> receiver_type =
                    ObjectTypeLocked((*registers)[1]);
                if (!receiver_type ||
                    !IsCompatibilityAssignable(*receiver_type,
                                               "android/content/Context")) {
                    SetError(call_error,
                             "guest JNI Context.getExternalFilesDir receiver is invalid");
                    return false;
                }
                std::uint64_t directory_type = 0;
                if (!ReadGuestGeneralArgument(address_space_,
                                              (*registers)[3],
                                              &directory_type, call_error))
                    return false;
                if (directory_type != 0) {
                    const std::optional<std::string_view> argument_type =
                        ObjectTypeLocked(directory_type);
                    if (!argument_type ||
                        !IsStringTypeName(*argument_type)) {
                        SetError(call_error,
                                 "guest JNI Context.getExternalFilesDir type is invalid");
                        return false;
                    }
                }
                const std::string path =
                    "/data/Android/data/" + compatibility_.package_name +
                    "/files";
                const std::uint64_t reference =
                    AllocateCompatibilityReference(
                        heap_, CompatibilityReferenceKind::kObject,
                        "Ljava/io/File;", path, call_error);
                if (reference == 0) return false;
                object_references_.emplace(
                    reference,
                    ObjectReference{reference, (*registers)[2],
                                    "Ljava/io/File;", path, 0});
                TrackLocalReferenceLocked(reference);
                (*registers)[0] = reference;
                return true;
            }
            if (backend_ == nullptr &&
                normalized_method_class == "android/content/Context" &&
                (method->second.name == "getCacheDir" ||
                 method->second.name == "getExternalCacheDir") &&
                signature == "()Ljava/io/File;") {
                const std::optional<std::string_view> receiver_type =
                    ObjectTypeLocked((*registers)[1]);
                if (!receiver_type ||
                    !IsCompatibilityAssignable(*receiver_type,
                                               "android/content/Context")) {
                    SetError(call_error,
                             "guest JNI Context.getCacheDir receiver is invalid");
                    return false;
                }
                const std::string path =
                    "/data/data/" + compatibility_.package_name + "/cache";
                const std::uint64_t reference =
                    AllocateCompatibilityReference(
                        heap_, CompatibilityReferenceKind::kObject,
                        "Ljava/io/File;", path, call_error);
                if (reference == 0) return false;
                object_references_.emplace(
                    reference,
                    ObjectReference{reference, (*registers)[2],
                                    "Ljava/io/File;", path, 0});
                TrackLocalReferenceLocked(reference);
                (*registers)[0] = reference;
                return true;
            }
            if (backend_ == nullptr &&
                normalized_method_class == "android/content/Context" &&
                method->second.name == "getObbDirs" &&
                signature == "()[Ljava/io/File;") {
                const std::optional<std::string_view> receiver_type =
                    ObjectTypeLocked((*registers)[1]);
                if (!receiver_type ||
                    !IsCompatibilityAssignable(*receiver_type,
                                               "android/content/Context")) {
                    SetError(call_error,
                             "guest JNI Context.getObbDirs receiver is invalid");
                    return false;
                }
                constexpr std::string_view kArrayType =
                    "[Ljava/io/File;";
                const std::uint64_t reference =
                    AllocateCompatibilityReference(
                        heap_, CompatibilityReferenceKind::kObject,
                        kArrayType, {}, call_error);
                if (reference == 0) return false;
                object_references_.emplace(
                    reference,
                    ObjectReference{reference, (*registers)[2],
                                    std::string(kArrayType), {}, 0});
                object_arrays_.emplace(
                    reference,
                    ObjectArrayStorage{std::string(kArrayType),
                                       "Ljava/io/File;", {}});
                TrackLocalReferenceLocked(reference);
                (*registers)[0] = reference;
                return true;
            }
            if (backend_ == nullptr &&
                method->second.name == "Resources_GetUserDataFolder" &&
                method->second.signature == "()Ljava/lang/String;") {
                std::fprintf(stderr, "ISOMR_FOLDER_SERVE userData\n");
                std::fflush(stderr);
                const std::uint64_t reference =
                    AllocateCompatibilityReference(
                        heap_, CompatibilityReferenceKind::kString,
                        "Ljava/lang/String;", "/data/data", call_error);
                if (reference == 0) return false;
                object_references_.emplace(
                    reference,
                    ObjectReference{reference, (*registers)[2],
                                    "Ljava/lang/String;", "/data/data", 0});
                TrackLocalReferenceLocked(reference);
                (*registers)[0] = reference;
                return true;
            }
            if (backend_ == nullptr &&
                method->second.name == "Resources_GetCacheDataFolder" &&
                method->second.signature == "()Ljava/lang/String;") {
                const std::uint64_t reference =
                    AllocateCompatibilityReference(
                        heap_, CompatibilityReferenceKind::kString,
                        "Ljava/lang/String;", "/data/cache", call_error);
                if (reference == 0) return false;
                object_references_.emplace(
                    reference,
                    ObjectReference{reference, (*registers)[2],
                                    "Ljava/lang/String;", "/data/cache", 0});
                TrackLocalReferenceLocked(reference);
                (*registers)[0] = reference;
                return true;
            }
            if (backend_ == nullptr &&
                method->second.name == "Resources_GetAppSupportDataFolder" &&
                method->second.signature == "()Ljava/lang/String;") {
                const std::uint64_t reference =
                    AllocateCompatibilityReference(
                        heap_, CompatibilityReferenceKind::kString,
                        "Ljava/lang/String;", "/data/appsupport", call_error);
                if (reference == 0) return false;
                object_references_.emplace(
                    reference,
                    ObjectReference{reference, (*registers)[2],
                                    "Ljava/lang/String;",
                                    "/data/appsupport", 0});
                TrackLocalReferenceLocked(reference);
                (*registers)[0] = reference;
                return true;
            }
            if (backend_ == nullptr &&
                method->second.name == "Resources_GetAssetFileInfo" &&
                method->second.signature ==
                    "(Ljava/lang/String;[J)Ljava/lang/String;") {
                // Asset metadata query: the compat runtime cannot produce
                // PvZ2 RSB metadata, so report an empty info string; the
                // bounded log records which asset the engine wants.
                std::array<std::uint8_t, 32> va{};
                std::uint64_t name_ref = 0;
                std::uint64_t array_ref = 0;
                if ((*registers)[3] != 0 &&
                    address_space_->Read((*registers)[3], va.data(),
                                         va.size())) {
                    std::memcpy(&name_ref, va.data(), sizeof(name_ref));
                    std::memcpy(&array_ref, va.data() + 8,
                                sizeof(array_ref));
                    std::string requested;
                    bool tracked = false;
                    const auto found = object_references_.find(name_ref);
                    if (found != object_references_.end()) {
                        tracked = true;
                        requested = found->second.string_value;
                    }
                    std::string raw_dump;
                    {
                        // Dump the header and, if +0x10 holds a payload
                        // pointer, the string it names (compat string
                        // object layout).
                        std::array<std::uint8_t, 0x40> raw{};
                        if (address_space_->Read(name_ref, raw.data(),
                                                 raw.size())) {
                            char hex[4];
                            for (std::uint8_t byte : raw) {
                                snprintf(hex, sizeof(hex), "%02x ", byte);
                                raw_dump += hex;
                            }
                            std::uint64_t payload = 0;
                            std::memcpy(&payload, raw.data() + 0x10,
                                        sizeof(payload));
                            if (payload > 0x700000000000ULL &&
                                payload < 0x720000000000ULL) {
                                std::array<char, 96> text{};
                                if (address_space_->Read(
                                        payload, text.data(), text.size())) {
                                    requested = std::string(
                                        text.data(),
                                        strnlen(text.data(), text.size()));
                                }
                            }
                        }
                    }
                    const auto qword_at = [&va](std::size_t index) {
                        std::uint64_t value = 0;
                        for (std::size_t byte = 0; byte < 8; ++byte)
                            value |= static_cast<std::uint64_t>(
                                         va[index * 8 + byte])
                                     << (8 * byte);
                        return value;
                    };
                    std::string extra_dump;
                    for (std::size_t qindex = 0; qindex < 3; ++qindex) {
                        const std::uint64_t obj = qword_at(qindex);
                        if (obj < 0x700000000000ULL) continue;
                        std::array<std::uint8_t, 0x18> obj_bytes{};
                        if (!address_space_->Read(obj, obj_bytes.data(),
                                                  obj_bytes.size()))
                            continue;
                        std::uint64_t payload = 0;
                        std::memcpy(&payload, obj_bytes.data() + 0x10,
                                    sizeof(payload));
                        char header[32];
                        snprintf(header, sizeof(header), " obj%zu@%llx:",
                                 qindex, (unsigned long long)obj);
                        extra_dump += header;
                        char hex2[3];
                        for (std::uint8_t byte : obj_bytes) {
                            snprintf(hex2, sizeof(hex2), "%02x", byte);
                            extra_dump += hex2;
                        }
                        if (payload > 0x700000000000ULL) {
                            std::array<char, 48> text{};
                            if (address_space_->Read(payload, text.data(),
                                                     text.size())) {
                                extra_dump += " payload=";
                                extra_dump += std::string(
                                    text.data(),
                                    strnlen(text.data(), text.size()));
                            }
                        }
                    }
                    static std::size_t asset_info_logs = 0;
                    if (asset_info_logs < 20) {
                        ++asset_info_logs;
                        const auto qword = [&va](std::size_t index) {
                            std::uint64_t value = 0;
                            for (std::size_t byte = 0; byte < 8; ++byte)
                                value |= static_cast<std::uint64_t>(
                                             va[index * 8 + byte])
                                         << (8 * byte);
                            return value;
                        };
                        std::fprintf(stderr,
                                     "ISOMR_ASSET_INFO_QUERY path=%s "
                                     "ref=%llx tracked=%d raw=%s "
                                     "q=[%llx %llx %llx %llx]%s\n",
                                     requested.c_str(),
                                     static_cast<unsigned long long>(name_ref),
                                     tracked ? 1 : 0, raw_dump.c_str(),
                                     static_cast<unsigned long long>(qword(0)),
                                     static_cast<unsigned long long>(qword(1)),
                                     static_cast<unsigned long long>(qword(2)),
                                     static_cast<unsigned long long>(qword(3)),
                                     extra_dump.c_str());
                        std::fflush(stderr);
                    }
                }
                // Fill the [J out-array with the RSB asset size pair and
                // return a non-empty info string so the boot load pipeline
                // proceeds past the metadata query.
                const auto longs = long_arrays_.find(array_ref);
                if (longs != long_arrays_.end()) {
                    if (longs->second.size() > 0) longs->second[0] = 34713600ULL;
                    if (longs->second.size() > 1) longs->second[1] = 0ULL;
                }
                std::string info_value = "main.rsb";
                const std::uint64_t reference =
                    AllocateCompatibilityReference(
                        heap_, CompatibilityReferenceKind::kString,
                        "Ljava/lang/String;", info_value, call_error);
                if (reference == 0) return false;
                object_references_.emplace(
                    reference,
                    ObjectReference{reference, (*registers)[2],
                                    "Ljava/lang/String;", info_value, 0});
                TrackLocalReferenceLocked(reference);
                (*registers)[0] = reference;
                return true;
            }
            if (backend_ == nullptr &&
                normalized_method_class == "java/lang/Object" &&
                method->second.name == "toString" &&
                signature == "()Ljava/lang/String;") {
                const auto local_obj =
                    object_references_.find((*registers)[1]);
                const auto global_obj =
                    global_object_references_.find((*registers)[1]);
                std::string type;
                std::string value;
                if (local_obj != object_references_.end()) {
                    type = local_obj->second.type;
                    value = local_obj->second.string_value;
                } else if (global_obj != global_object_references_.end()) {
                    type = global_obj->second.type;
                    value = global_obj->second.string_value;
                } else {
                    // Java defines toString on every object; an untracked
                    // receiver still gets a stable generic description.
                    char address_text[32];
                    snprintf(address_text, sizeof(address_text), "@0x%llx",
                             (unsigned long long)(*registers)[1]);
                    type = "java/lang/Object";
                    value = address_text;
                }
                std::string description = type;
                if (!value.empty()) description += ": " + value;
                const std::uint64_t reference =
                    AllocateCompatibilityReference(
                        heap_, CompatibilityReferenceKind::kString,
                        "Ljava/lang/String;", description, call_error);
                if (reference == 0) return false;
                object_references_.emplace(
                    reference,
                    ObjectReference{reference, (*registers)[2],
                                    "Ljava/lang/String;", description, 0});
                TrackLocalReferenceLocked(reference);
                (*registers)[0] = reference;
                return true;
            }
            if (backend_ == nullptr &&
                normalized_method_class.ends_with("Throwable") &&
                (method->second.name == "getMessage" ||
                 method->second.name == "getLocalizedMessage" ||
                 method->second.name == "toString") &&
                method->second.signature == "()Ljava/lang/String;") {
                const auto pending_local =
                    object_references_.find((*registers)[1]);
                const auto pending_global =
                    global_object_references_.find((*registers)[1]);
                const std::string pending_type =
                    pending_local != object_references_.end()
                        ? pending_local->second.type
                        : pending_global != global_object_references_.end()
                              ? pending_global->second.type
                              : std::string();
                const std::string pending_value =
                    pending_local != object_references_.end()
                        ? pending_local->second.string_value
                        : pending_global != global_object_references_.end()
                              ? pending_global->second.string_value
                              : std::string();
                if (pending_type.empty()) {
                    SetError(call_error,
                             "guest JNI Throwable receiver is invalid");
                    return false;
                }
                std::string value =
                    method->second.name == "toString"
                        ? (pending_value.empty()
                               ? pending_type
                               : pending_type + ": " + pending_value)
                        : pending_value;
                std::fprintf(stdout,
                    "ISOMR_CTR_THROWABLE_QUERY method=%s value=%s\n",
                    method->second.name.c_str(), value.c_str());
                const std::uint64_t reference =
                    AllocateCompatibilityReference(
                        heap_, CompatibilityReferenceKind::kString,
                        "Ljava/lang/String;", value, call_error);
                if (reference == 0) return false;
                object_references_.emplace(
                    reference,
                    ObjectReference{reference, (*registers)[2],
                                    "Ljava/lang/String;", value, 0});
                TrackLocalReferenceLocked(reference);
                (*registers)[0] = reference;
                return true;
            }
            if (backend_ == nullptr &&
                normalized_method_class == "android/content/Context" &&
                method->second.name == "getObbDir" &&
                signature == "()Ljava/io/File;") {
                const std::optional<std::string_view> receiver_type =
                    ObjectTypeLocked((*registers)[1]);
                if (!receiver_type ||
                    !IsCompatibilityAssignable(*receiver_type,
                                               "android/content/Context")) {
                    SetError(call_error,
                             "guest JNI Context.getObbDir receiver is invalid");
                    return false;
                }
                constexpr std::string_view kObbPath = "/data/obb";
                const std::uint64_t reference =
                    AllocateCompatibilityReference(
                        heap_, CompatibilityReferenceKind::kObject,
                        "Ljava/io/File;", kObbPath, call_error);
                if (reference == 0) return false;
                object_references_.emplace(
                    reference,
                    ObjectReference{reference, (*registers)[2],
                                    "Ljava/io/File;",
                                    std::string(kObbPath), 0});
                TrackLocalReferenceLocked(reference);
                (*registers)[0] = reference;
                return true;
            }
            if (backend_ == nullptr &&
                normalized_method_class == "android/content/Context" &&
                (method->second.name == "getPackageName" ||
                 method->second.name == "getPackageCodePath") &&
                signature == "()Ljava/lang/String;") {
                const std::optional<std::string_view> receiver_type =
                    ObjectTypeLocked((*registers)[1]);
                if (!receiver_type ||
                    !IsCompatibilityAssignable(*receiver_type,
                                               "android/content/Context")) {
                    SetError(call_error,
                             "guest JNI Context package query receiver is invalid");
                    return false;
                }
                if (compatibility_.package_name.empty() ||
                    compatibility_.package_code_path.empty()) {
                    SetError(call_error,
                             "guest JNI package compatibility is not configured");
                    return false;
                }
                const std::string_view value =
                    method->second.name == "getPackageName"
                        ? std::string_view(compatibility_.package_name)
                        : std::string_view(
                              compatibility_.package_code_path);
                const std::uint64_t reference =
                    AllocateCompatibilityReference(
                        heap_, CompatibilityReferenceKind::kString,
                        "Ljava/lang/String;", value, call_error);
                if (reference == 0) return false;
                object_references_.emplace(
                    reference,
                    ObjectReference{reference, (*registers)[2],
                                    "Ljava/lang/String;",
                                    std::string(value), 0});
                TrackLocalReferenceLocked(reference);
                (*registers)[0] = reference;
                return true;
            }
            if (backend_ == nullptr &&
                normalized_method_class ==
                    "android/content/pm/PackageManager" &&
                method->second.name == "getPackageInfo" &&
                signature ==
                    "(Ljava/lang/String;I)Landroid/content/pm/PackageInfo;") {
                const std::optional<std::string_view> receiver_type =
                    ObjectTypeLocked((*registers)[1]);
                if (!receiver_type ||
                    NormalizeCompatibilityType(*receiver_type) !=
                        "android/content/pm/PackageManager") {
                    SetError(call_error,
                             "guest JNI PackageManager.getPackageInfo receiver is invalid");
                    return false;
                }
                std::uint64_t package_reference = 0;
                std::uint64_t flags = 0;
                if (!ReadGuestGeneralArgument(address_space_,
                                              (*registers)[3],
                                              &package_reference,
                                              call_error) ||
                    !ReadGuestGeneralArgument(address_space_,
                                              (*registers)[3], &flags,
                                              call_error, 1))
                    return false;
                static_cast<void>(flags);
                const auto local_package =
                    FindOwnedLocalObjectReferenceLocked(package_reference);
                const auto global_package =
                    global_object_references_.find(package_reference);
                const std::string* package_name =
                    local_package != object_references_.end() &&
                            IsStringTypeName(local_package->second.type)
                        ? &local_package->second.string_value
                        : global_package !=
                                      global_object_references_.end() &&
                                  IsStringTypeName(global_package->second.type)
                              ? &global_package->second.string_value
                              : nullptr;
                if (compatibility_.package_name.empty() ||
                    package_name == nullptr ||
                    *package_name != compatibility_.package_name) {
                    SetError(call_error,
                             "guest JNI PackageManager.getPackageInfo package is invalid");
                    return false;
                }
                constexpr std::string_view kPackageInfoType =
                    "Landroid/content/pm/PackageInfo;";
                const std::uint64_t reference =
                    AllocateCompatibilityReference(
                        heap_, CompatibilityReferenceKind::kObject,
                        kPackageInfoType, {}, call_error);
                if (reference == 0) return false;
                object_references_.emplace(
                    reference,
                    ObjectReference{reference, (*registers)[2],
                                    std::string(kPackageInfoType), {}, 0});
                TrackLocalReferenceLocked(reference);
                (*registers)[0] = reference;
                return true;
            }
            // getSharedPreferences may be declared on a subclass
            // (Activity/ContextWrapper) or with an Object return type when
            // invoked through Unity's reflection path — the receiver
            // assignability check below is the real gate.
            if (backend_ == nullptr &&
                method->second.name == "getSharedPreferences" &&
                signature.starts_with("(Ljava/lang/String;I)L")) {
                const std::optional<std::string_view> receiver_type =
                    ObjectTypeLocked((*registers)[1]);
                if (!receiver_type ||
                    !IsCompatibilityAssignable(*receiver_type,
                                               "android/content/Context")) {
                    SetError(call_error,
                             "guest JNI Context.getSharedPreferences receiver is invalid");
                    return false;
                }
                std::uint64_t name_reference = 0;
                std::uint64_t mode = 0;
                if (!ReadGuestGeneralArgument(address_space_,
                                              (*registers)[3],
                                              &name_reference,
                                              call_error) ||
                    !ReadGuestGeneralArgument(address_space_,
                                              (*registers)[3], &mode,
                                              call_error, 1))
                    return false;
                const auto local_name =
                    FindOwnedLocalObjectReferenceLocked(name_reference);
                const auto global_name =
                    global_object_references_.find(name_reference);
                const std::string* name =
                    local_name != object_references_.end() &&
                            IsStringTypeName(local_name->second.type)
                        ? &local_name->second.string_value
                        : global_name !=
                                      global_object_references_.end() &&
                                  IsStringTypeName(global_name->second.type)
                              ? &global_name->second.string_value
                              : nullptr;
                if (name == nullptr || name->empty() || mode != 0) {
                    SetError(call_error,
                             "guest JNI Context.getSharedPreferences name or mode is invalid");
                    return false;
                }
                constexpr std::string_view kPreferencesType =
                    "Landroid/content/SharedPreferences;";
                const std::uint64_t reference =
                    AllocateCompatibilityReference(
                        heap_, CompatibilityReferenceKind::kObject,
                        kPreferencesType, {}, call_error);
                if (reference == 0) return false;
                object_references_.emplace(
                    reference,
                    ObjectReference{reference, (*registers)[2],
                                    std::string(kPreferencesType), *name,
                                    0});
                TrackLocalReferenceLocked(reference);
                (*registers)[0] = reference;
                return true;
            }
            // Context.registerReceiver returns the first matching sticky
            // broadcast or null. A live empty Intent is strictly safer than
            // null: callers that chain getAction()/get*Extra on the result
            // see empty defaults instead of a NullPointerException, and
            // callers that null-check still behave (an empty Intent is
            // indistinguishable from a sticky broadcast with no extras).
            if (backend_ == nullptr &&
                method->second.name == "registerReceiver" &&
                ReturnType(signature) == "Landroid/content/Intent;") {
                const std::optional<std::string_view> receiver_type =
                    ObjectTypeLocked((*registers)[1]);
                if (receiver_type &&
                    !IsCompatibilityAssignable(*receiver_type,
                                               "android/content/Context") &&
                    !IsCompatibilityAssignable(*receiver_type,
                                               "android/app/Activity") &&
                    !IsCompatibilityAssignable(*receiver_type,
                                               "java/lang/Object")) {
                    SetError(call_error,
                             "guest JNI Context.registerReceiver receiver is invalid");
                    return false;
                }
                std::uint64_t receiver_arg = 0;
                std::uint64_t filter_arg = 0;
                if (ReadGuestGeneralArgument(address_space_,
                                             (*registers)[3], &receiver_arg,
                                             call_error, 0) &&
                    ReadGuestGeneralArgument(address_space_,
                                             (*registers)[3], &filter_arg,
                                             call_error, 1)) {
                    const std::optional<std::string_view> arg_type =
                        ObjectTypeLocked(receiver_arg);
                    const std::optional<std::uint64_t> arg_identity =
                        ReferenceIdentityLocked(receiver_arg);
                    const auto arg_proxy =
                        arg_identity
                            ? interface_proxies_.find(*arg_identity)
                            : interface_proxies_.end();
                    const std::optional<std::string_view> filter_type =
                        ObjectTypeLocked(filter_arg);
                    std::string actions;
                    const auto local_filter =
                        object_references_.find(filter_arg);
                    if (local_filter != object_references_.end())
                        actions = local_filter->second.string_value;
                    std::fprintf(stderr,
                                 "ISOMR_REGISTER_RECEIVER recv=%s proxy=%d filter=%s actions='%s'\n",
                                 arg_type ? std::string(*arg_type).c_str()
                                          : "<null>",
                                 arg_proxy != interface_proxies_.end() ? 1 : 0,
                                 filter_type ? std::string(*filter_type).c_str()
                                             : "<null>",
                                 actions.c_str());
                    std::fflush(stderr);
                }
                const std::uint64_t reference = AllocateCompatibilityReference(
                    heap_, CompatibilityReferenceKind::kObject,
                    "Landroid/content/Intent;", {}, call_error);
                if (reference == 0) return false;
                object_references_.emplace(
                    reference,
                    ObjectReference{reference, (*registers)[2],
                                    "Landroid/content/Intent;", {}, 0});
                TrackLocalReferenceLocked(reference);
                (*registers)[0] = reference;
                return true;
            }
            if (backend_ == nullptr &&
                normalized_method_class ==
                    "android/content/SharedPreferences" &&
                method->second.name == "edit" &&
                (signature ==
                     "()Landroid/content/SharedPreferences$Editor;" ||
                 signature == "()Ljava/lang/Object;")) {
                const auto local_preferences =
                    object_references_.find((*registers)[1]);
                const auto global_preferences =
                    global_object_references_.find((*registers)[1]);
                const std::string* preference_name =
                    local_preferences != object_references_.end() &&
                            NormalizeCompatibilityType(local_preferences->second.type) == "android/content/SharedPreferences"
                        ? &local_preferences->second.string_value
                        : global_preferences !=
                                      global_object_references_.end() &&
                                  NormalizeCompatibilityType(global_preferences->second.type) == "android/content/SharedPreferences"
                              ? &global_preferences->second.string_value
                              : nullptr;
                if (preference_name == nullptr || preference_name->empty()) {
                    SetError(call_error,
                             "guest JNI SharedPreferences.edit receiver is invalid");
                    return false;
                }
                constexpr std::string_view kEditorType =
                    "Landroid/content/SharedPreferences$Editor;";
                const std::uint64_t reference =
                    AllocateCompatibilityReference(
                        heap_, CompatibilityReferenceKind::kObject,
                        kEditorType, {}, call_error);
                if (reference == 0) return false;
                object_references_.emplace(
                    reference,
                    ObjectReference{reference, (*registers)[2],
                                    std::string(kEditorType),
                                    *preference_name, 0});
                shared_preferences_editors_.emplace(
                    reference,
                    SharedPreferencesEditorStorage{*preference_name, {}});
                TrackLocalReferenceLocked(reference);
                (*registers)[0] = reference;
                return true;
            }
            // SharedPreferences.getString(key, default): an absent key
            // returns the caller's default reference verbatim — returning
            // null where the caller passed a non-null default produces a
            // managed NullPointerException on the first use.
            if (backend_ == nullptr &&
                normalized_method_class ==
                    "android/content/SharedPreferences" &&
                method->second.name == "getString" &&
                signature ==
                    "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;") {
                const std::string* preference_name = nullptr;
                const auto local_preferences =
                    object_references_.find((*registers)[1]);
                const auto global_preferences =
                    global_object_references_.find((*registers)[1]);
                if (local_preferences != object_references_.end() &&
                    NormalizeCompatibilityType(
                        local_preferences->second.type) ==
                        "android/content/SharedPreferences")
                    preference_name = &local_preferences->second.string_value;
                else if (global_preferences !=
                             global_object_references_.end() &&
                         NormalizeCompatibilityType(
                             global_preferences->second.type) ==
                             "android/content/SharedPreferences")
                    preference_name =
                        &global_preferences->second.string_value;
                std::uint64_t key_reference = 0;
                std::uint64_t default_reference = 0;
                if (!ReadGuestGeneralArgument(address_space_,
                                              (*registers)[3],
                                              &key_reference, call_error, 0) ||
                    !ReadGuestGeneralArgument(address_space_,
                                              (*registers)[3],
                                              &default_reference, call_error,
                                              1))
                    return false;
                const auto copy_string =
                    [this](std::uint64_t ref) -> const std::string* {
                    const auto local =
                        FindOwnedLocalObjectReferenceLocked(ref);
                    if (local != object_references_.end() &&
                        IsStringTypeName(local->second.type))
                        return &local->second.string_value;
                    const auto global = global_object_references_.find(ref);
                    if (global != global_object_references_.end() &&
                        IsStringTypeName(global->second.type))
                        return &global->second.string_value;
                    return nullptr;
                };
                const std::string* key = copy_string(key_reference);
                if (preference_name == nullptr || key == nullptr) {
                    SetError(call_error,
                             "guest JNI SharedPreferences.getString arguments are invalid");
                    return false;
                }
                const auto preferences =
                    shared_preferences_strings_.find(*preference_name);
                if (preferences != shared_preferences_strings_.end()) {
                    const auto stored = preferences->second.find(*key);
                    if (stored != preferences->second.end()) {
                        const std::uint64_t stored_reference =
                            AllocateCompatibilityReference(
                                heap_, CompatibilityReferenceKind::kObject,
                                "Ljava/lang/String;", stored->second,
                                call_error);
                        if (stored_reference == 0) return false;
                        object_references_.emplace(
                            stored_reference,
                            ObjectReference{stored_reference, (*registers)[2],
                                            "Ljava/lang/String;",
                                            stored->second, 0});
                        TrackLocalReferenceLocked(stored_reference);
                        (*registers)[0] = stored_reference;
                        return true;
                    }
                }
                if (call_error != nullptr)
                    *call_error = "trace:jni-getstring-default-" + *key;
                (*registers)[0] = default_reference;
                return true;
            }
            // AdvertisingIdClient.Info.getId: a stable all-zero advertising
            // id — the value real devices report when the ad id is absent or
            // reset, so callers tolerate it.
            if (backend_ == nullptr &&
                method->second.name == "getId" &&
                signature == "()Ljava/lang/String;") {
                const auto local_info =
                    object_references_.find((*registers)[1]);
                const auto global_info =
                    global_object_references_.find((*registers)[1]);
                const bool is_ad_info =
                    (local_info != object_references_.end() &&
                     NormalizeCompatibilityType(local_info->second.type)
                             .find("AdvertisingIdClient$Info") !=
                         std::string::npos) ||
                    (global_info != global_object_references_.end() &&
                     NormalizeCompatibilityType(global_info->second.type)
                             .find("AdvertisingIdClient$Info") !=
                         std::string::npos);
                if (is_ad_info) {
                    const std::uint64_t reference =
                        AllocateCompatibilityReference(
                            heap_, CompatibilityReferenceKind::kString,
                            "Ljava/lang/String;",
                            "00000000-0000-0000-0000-000000000000",
                            call_error);
                    if (reference == 0) return false;
                    object_references_.emplace(
                        reference,
                        ObjectReference{reference, (*registers)[2],
                                        "Ljava/lang/String;",
                                        "00000000-0000-0000-0000-000000000000",
                                        0});
                    TrackLocalReferenceLocked(reference);
                    (*registers)[0] = reference;
                    return true;
                }
            }
            if (backend_ == nullptr &&
                normalized_method_class ==
                    "android/content/SharedPreferences$Editor" &&
                method->second.name == "putInt" &&
                signature ==
                    "(Ljava/lang/String;I)Landroid/content/SharedPreferences$Editor;") {
                const std::optional<std::uint64_t> editor_identity =
                    ReferenceIdentityLocked((*registers)[1]);
                auto editor = editor_identity
                                  ? shared_preferences_editors_.find(
                                        *editor_identity)
                                  : shared_preferences_editors_.end();
                std::uint64_t key_reference = 0;
                std::uint64_t value = 0;
                if (!editor_identity ||
                    editor == shared_preferences_editors_.end() ||
                    !ReadGuestGeneralArgument(address_space_,
                                              (*registers)[3],
                                              &key_reference,
                                              call_error) ||
                    !ReadGuestGeneralArgument(address_space_,
                                              (*registers)[3], &value,
                                              call_error, 1)) {
                    if (call_error == nullptr || call_error->empty())
                        SetError(call_error,
                                 "guest JNI SharedPreferences.Editor.putInt state is invalid");
                    return false;
                }
                const auto local_key =
                    FindOwnedLocalObjectReferenceLocked(key_reference);
                const auto global_key =
                    global_object_references_.find(key_reference);
                const std::string* key =
                    local_key != object_references_.end() &&
                            IsStringTypeName(local_key->second.type)
                        ? &local_key->second.string_value
                        : global_key != global_object_references_.end() &&
                                  IsStringTypeName(global_key->second.type)
                              ? &global_key->second.string_value
                              : nullptr;
                if (key == nullptr) {
                    SetError(call_error,
                             "guest JNI SharedPreferences.Editor.putInt key is invalid");
                    return false;
                }
                editor->second.pending_ints[*key] =
                    static_cast<std::int32_t>(
                        static_cast<std::uint32_t>(value));
                // Android's Editor mutators return this Editor for chaining.
                (*registers)[0] = (*registers)[1];
                return true;
            }
            // SharedPreferences.Editor.putString — matches on the receiver
            // being a tracked editor rather than the declaring class, so the
            // reflection-style call site declaring the method on
            // Ljava/lang/Object; is handled identically.
            if (backend_ == nullptr &&
                method->second.name == "putString" &&
                (signature ==
                     "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/Object;" ||
                 signature ==
                     "(Ljava/lang/String;Ljava/lang/String;)"
                     "Landroid/content/SharedPreferences$Editor;")) {
                const std::optional<std::uint64_t> editor_identity =
                    ReferenceIdentityLocked((*registers)[1]);
                auto editor = editor_identity
                                  ? shared_preferences_editors_.find(
                                        *editor_identity)
                                  : shared_preferences_editors_.end();
                if (editor == shared_preferences_editors_.end()) {
                    if (call_error != nullptr)
                        *call_error = "trace:jni-call-object-stub-" +
                                      method->second.class_type + "." +
                                      method->second.name +
                                      method->second.signature;
                    (*registers)[0] = 0;
                    return true;
                }
                std::uint64_t key_reference = 0;
                std::uint64_t value_reference = 0;
                if (!ReadGuestGeneralArgument(address_space_,
                                              (*registers)[3],
                                              &key_reference, call_error, 0) ||
                    !ReadGuestGeneralArgument(address_space_,
                                              (*registers)[3],
                                              &value_reference, call_error, 1))
                    return false;
                const auto copy_string =
                    [this](std::uint64_t ref) -> const std::string* {
                    const auto local =
                        FindOwnedLocalObjectReferenceLocked(ref);
                    if (local != object_references_.end() &&
                        IsStringTypeName(local->second.type))
                        return &local->second.string_value;
                    const auto global = global_object_references_.find(ref);
                    if (global != global_object_references_.end() &&
                        IsStringTypeName(global->second.type))
                        return &global->second.string_value;
                    return nullptr;
                };
                const std::string* key = copy_string(key_reference);
                const std::string* value = copy_string(value_reference);
                if (key == nullptr || value == nullptr) {
                    SetError(call_error,
                             "guest JNI SharedPreferences.Editor.putString arguments are invalid");
                    return false;
                }
                editor->second.pending_strings[*key] = *value;
                if (call_error != nullptr)
                    *call_error = "trace:jni-putstring-" + *key;
                (*registers)[0] = (*registers)[1];
                return true;
            }
            if (backend_ == nullptr &&
                normalized_method_class == "java/lang/ClassLoader" &&
                method->second.name == "findLibrary" &&
                signature ==
                    "(Ljava/lang/String;)Ljava/lang/String;") {
                const std::optional<std::string_view> receiver_type =
                    ObjectTypeLocked((*registers)[1]);
                if (!receiver_type ||
                    NormalizeCompatibilityType(*receiver_type) !=
                        "java/lang/ClassLoader") {
                    SetError(call_error,
                             "guest JNI ClassLoader.findLibrary receiver is invalid");
                    return false;
                }
                std::uint64_t name_reference = 0;
                if (!ReadGuestGeneralArgument(address_space_,
                                              (*registers)[3],
                                              &name_reference,
                                              call_error))
                    return false;
                const auto local_name =
                    FindOwnedLocalObjectReferenceLocked(name_reference);
                const auto global_name =
                    global_object_references_.find(name_reference);
                const std::string* name =
                    local_name != object_references_.end() &&
                            IsStringTypeName(local_name->second.type)
                        ? &local_name->second.string_value
                        : global_name != global_object_references_.end() &&
                                  IsStringTypeName(global_name->second.type)
                              ? &global_name->second.string_value
                              : nullptr;
                if (name == nullptr || name->empty()) {
                    SetError(call_error,
                             "guest JNI ClassLoader.findLibrary name is invalid");
                    return false;
                }
                if (*name != "main" && *name != "unity" &&
                    *name != "il2cpp") {
                    // Java ClassLoader.findLibrary returns null when its
                    // search path has no matching native module.
                    (*registers)[0] = 0;
                    return true;
                }
                const std::string library_name = "lib" + *name + ".so";
                const std::uint64_t reference =
                    AllocateCompatibilityReference(
                        heap_, CompatibilityReferenceKind::kString,
                        "Ljava/lang/String;", library_name, call_error);
                if (reference == 0) return false;
                object_references_.emplace(
                    reference,
                    ObjectReference{reference, (*registers)[2],
                                    "Ljava/lang/String;", library_name,
                                    0});
                TrackLocalReferenceLocked(reference);
                (*registers)[0] = reference;
                return true;
            }
            if (backend_ == nullptr &&
                normalized_method_class ==
                    "android/app/AlertDialog$Builder" &&
                (method->second.name == "setTitle" ||
                 method->second.name == "setMessage") &&
                signature ==
                    "(Ljava/lang/CharSequence;)Landroid/app/AlertDialog$Builder;") {
                const std::optional<std::uint64_t> builder_identity =
                    ReferenceIdentityLocked((*registers)[1]);
                auto builder =
                    builder_identity
                        ? alert_dialog_builders_.find(*builder_identity)
                        : alert_dialog_builders_.end();
                std::uint64_t title_reference = 0;
                if (!builder_identity ||
                    builder == alert_dialog_builders_.end() ||
                    !ReadGuestGeneralArgument(address_space_,
                                              (*registers)[3],
                                              &title_reference,
                                              call_error)) {
                    if (call_error == nullptr || call_error->empty())
                        SetError(call_error,
                                 "guest JNI AlertDialog.Builder text setter state is invalid");
                    return false;
                }
                const auto local_title =
                    FindOwnedLocalObjectReferenceLocked(title_reference);
                const auto global_title =
                    global_object_references_.find(title_reference);
                const std::string* title =
                    local_title != object_references_.end() &&
                            IsStringTypeName(local_title->second.type)
                        ? &local_title->second.string_value
                        : global_title != global_object_references_.end() &&
                                  IsStringTypeName(global_title->second.type)
                              ? &global_title->second.string_value
                              : nullptr;
                if (title == nullptr) {
                    SetError(call_error,
                             "guest JNI AlertDialog.Builder text value is not a copied String");
                    return false;
                }
                if (method->second.name == "setTitle")
                    builder->second.title = *title;
                else
                    builder->second.message = *title;
                (*registers)[0] = (*registers)[1];
                return true;
            }
            if (backend_ == nullptr &&
                normalized_method_class ==
                    "android/app/AlertDialog$Builder" &&
                method->second.name != "create" &&
                method->second.name != "show") {
                // Generic Builder chain setter: validate the receiver is a
                // tracked builder and return it so guest chaining continues.
                const std::optional<std::uint64_t> builder_identity =
                    ReferenceIdentityLocked((*registers)[1]);
                if (!builder_identity ||
                    alert_dialog_builders_.find(*builder_identity) ==
                        alert_dialog_builders_.end()) {
                    SetError(call_error,
                             "guest JNI AlertDialog.Builder receiver is invalid");
                    return false;
                }
                (*registers)[0] = (*registers)[1];
                return true;
            }
            if (backend_ == nullptr &&
                normalized_method_class ==
                    "android/app/AlertDialog$Builder" &&
                (method->second.name == "create" ||
                 method->second.name == "show") &&
                ReturnType(signature) == "Landroid/app/AlertDialog;") {
                const std::optional<std::uint64_t> builder_identity =
                    ReferenceIdentityLocked((*registers)[1]);
                auto builder =
                    builder_identity
                        ? alert_dialog_builders_.find(*builder_identity)
                        : alert_dialog_builders_.end();
                if (!builder_identity ||
                    builder == alert_dialog_builders_.end()) {
                    SetError(call_error,
                             "guest JNI AlertDialog.Builder receiver is invalid");
                    return false;
                }
                const std::uint64_t reference =
                    AllocateCompatibilityReference(
                        heap_, CompatibilityReferenceKind::kObject,
                        "Landroid/app/AlertDialog;",
                        builder->second.title + "|" + builder->second.message,
                        call_error);
                if (reference == 0) return false;
                object_references_.emplace(
                    reference,
                    ObjectReference{reference, (*registers)[2],
                                    "Landroid/app/AlertDialog;",
                                    builder->second.title + "|" +
                                        builder->second.message,
                                    0});
                TrackLocalReferenceLocked(reference);
                (*registers)[0] = reference;
                std::fprintf(stderr,
                             "ISOMR_CTR_DIALOG show title='%s' message='%s'\n",
                             builder->second.title.c_str(),
                             builder->second.message.c_str());
                std::fflush(stderr);
                return true;
            }
            if (backend_ == nullptr &&
                normalized_method_class == "java/io/File" &&
                (method->second.name == "getPath" ||
                 method->second.name == "getAbsolutePath" ||
                 method->second.name == "getCanonicalPath") &&
                signature == "()Ljava/lang/String;") {
                const auto local_file =
                    object_references_.find((*registers)[1]);
                const auto global_file =
                    global_object_references_.find((*registers)[1]);
                const std::string* path =
                    local_file != object_references_.end() &&
                            NormalizeCompatibilityType(local_file->second.type) == "java/io/File"
                        ? &local_file->second.string_value
                        : global_file !=
                                      global_object_references_.end() &&
                                  NormalizeCompatibilityType(global_file->second.type) == "java/io/File"
                              ? &global_file->second.string_value
                              : nullptr;
                if (path == nullptr || path->empty()) {
                    SetError(call_error,
                             "guest JNI File path receiver is invalid");
                    return false;
                }
                const std::uint64_t reference =
                    AllocateCompatibilityReference(
                        heap_, CompatibilityReferenceKind::kString,
                        "Ljava/lang/String;", *path, call_error);
                if (reference == 0) return false;
                object_references_.emplace(
                    reference,
                    ObjectReference{reference, (*registers)[2],
                                    "Ljava/lang/String;", *path, 0});
                TrackLocalReferenceLocked(reference);
                (*registers)[0] = reference;
                return true;
            }
            if (backend_ == nullptr &&
                normalized_method_class == "java/io/File" &&
                (method->second.name == "getParent" ||
                 method->second.name == "getParentFile")) {
                const std::string_view expected =
                    method->second.name == "getParent"
                        ? "()Ljava/lang/String;"
                        : "()Ljava/io/File;";
                if (signature != expected) {
                    SetError(call_error,
                             "guest JNI File parent signature is invalid");
                    return false;
                }
                const auto local_file =
                    object_references_.find((*registers)[1]);
                const auto global_file =
                    global_object_references_.find((*registers)[1]);
                const std::string* path =
                    local_file != object_references_.end() &&
                            NormalizeCompatibilityType(
                                local_file->second.type) == "java/io/File"
                        ? &local_file->second.string_value
                        : global_file !=
                                      global_object_references_.end() &&
                                  NormalizeCompatibilityType(
                                      global_file->second.type) ==
                                      "java/io/File"
                              ? &global_file->second.string_value
                              : nullptr;
                if (path == nullptr || path->empty() ||
                    (*path)[0] != '/') {
                    (*registers)[0] = 0;
                    return true;
                }
                std::string trimmed(*path);
                while (trimmed.size() > 1 && trimmed.back() == '/')
                    trimmed.pop_back();
                const std::size_t separator = trimmed.find_last_of('/');
                std::string parent =
                    separator == std::string::npos ? std::string()
                    : separator == 0 ? std::string("/")
                                     : trimmed.substr(0, separator);
                if (parent.empty()) {
                    (*registers)[0] = 0;
                    return true;
                }
                const bool as_file =
                    method->second.name == "getParentFile";
                const std::uint64_t reference =
                    AllocateCompatibilityReference(
                        heap_,
                        as_file ? CompatibilityReferenceKind::kObject
                                : CompatibilityReferenceKind::kString,
                        as_file ? "Ljava/io/File;" : "Ljava/lang/String;",
                        parent, call_error);
                if (reference == 0) return false;
                object_references_.emplace(
                    reference,
                    ObjectReference{reference, (*registers)[2],
                                    as_file ? "Ljava/io/File;"
                                            : "Ljava/lang/String;",
                                    parent, 0});
                TrackLocalReferenceLocked(reference);
                (*registers)[0] = reference;
                return true;
            }
            if (backend_ == nullptr &&
                normalized_method_class == "java/util/Scanner" &&
                method->second.name == "useDelimiter" &&
                signature ==
                    "(Ljava/lang/String;)Ljava/util/Scanner;") {
                const std::optional<std::uint64_t> scanner_identity =
                    ReferenceIdentityLocked((*registers)[1]);
                const auto scanner = scanner_identity
                                         ? scanners_.find(*scanner_identity)
                                         : scanners_.end();
                std::uint64_t delimiter_reference = 0;
                if (!scanner_identity || scanner == scanners_.end()) {
                    const auto local_receiver =
                        object_references_.find((*registers)[1]);
                    const auto global_receiver =
                        global_object_references_.find((*registers)[1]);
                    const std::uint64_t constructor_reference =
                        local_receiver != object_references_.end()
                            ? local_receiver->second.method_reference
                            : global_receiver !=
                                      global_object_references_.end()
                                  ? global_receiver->second.method_reference
                                  : 0;
                    const auto constructor =
                        method_references_.find(constructor_reference);
                    SetError(
                        call_error,
                        "guest JNI Scanner.useDelimiter receiver is not linked to its InputStream constructor=" +
                            (constructor != method_references_.end()
                                 ? constructor->second.signature
                                 : std::string("<unknown>")));
                    return false;
                }
                if (!ReadGuestGeneralArgument(address_space_,
                                              (*registers)[3],
                                              &delimiter_reference,
                                              call_error))
                    return false;
                const auto local_delimiter =
                    FindOwnedLocalObjectReferenceLocked(delimiter_reference);
                const auto global_delimiter =
                    global_object_references_.find(delimiter_reference);
                const std::string* delimiter =
                    local_delimiter != object_references_.end() &&
                            IsStringTypeName(local_delimiter->second.type)
                        ? &local_delimiter->second.string_value
                        : global_delimiter !=
                                      global_object_references_.end() &&
                                  IsStringTypeName(global_delimiter->second.type)
                              ? &global_delimiter->second.string_value
                              : nullptr;
                if (delimiter == nullptr ||
                    (*delimiter != "\\A" && *delimiter != "\\Z" &&
                     *delimiter != "\\z")) {
                    SetError(call_error,
                             "guest JNI Scanner.useDelimiter pattern is unsupported");
                    return false;
                }
                scanner->second.delimiter = *delimiter;
                const std::uint64_t reference =
                    AllocateCompatibilityReference(
                        heap_, CompatibilityReferenceKind::kObject,
                        "Ljava/util/Scanner;", {}, call_error);
                if (reference == 0) return false;
                object_references_.emplace(
                    reference,
                    ObjectReference{*scanner_identity, (*registers)[2],
                                    "Ljava/util/Scanner;", {}, 0});
                TrackLocalReferenceLocked(reference);
                (*registers)[0] = reference;
                return true;
            }
            if (backend_ == nullptr &&
                normalized_method_class == "java/util/Scanner" &&
                method->second.name == "next" &&
                signature == "()Ljava/lang/String;") {
                const std::optional<std::uint64_t> scanner_identity =
                    ReferenceIdentityLocked((*registers)[1]);
                const auto scanner = scanner_identity
                                         ? scanners_.find(*scanner_identity)
                                         : scanners_.end();
                const auto stream =
                    scanner != scanners_.end()
                        ? input_streams_.find(
                              scanner->second.input_stream_identity)
                        : input_streams_.end();
                if (!scanner_identity || scanner == scanners_.end() ||
                    stream == input_streams_.end() ||
                    stream->second.descriptor < 0 ||
                    scanner->second.consumed ||
                    scanner->second.delimiter.empty()) {
                    SetError(call_error,
                             "guest JNI Scanner.next state is invalid");
                    return false;
                }
                const std::int64_t current = files_->Lseek64(
                    stream->second.descriptor, 0, 1);
                const std::int64_t end = files_->Lseek64(
                    stream->second.descriptor, 0, 2);
                if (current < 0 || end < current ||
                    files_->Lseek64(stream->second.descriptor, current, 0) !=
                        current ||
                    end - current > 16 * 1024 * 1024) {
                    SetError(call_error,
                             "guest JNI Scanner.next asset size is invalid");
                    return false;
                }
                const std::size_t remaining =
                    static_cast<std::size_t>(end - current);
                const std::uint64_t buffer =
                    heap_->Allocate(remaining == 0 ? 1 : remaining);
                if (buffer == 0) {
                    SetError(call_error,
                             "guest JNI Scanner.next staging allocation failed");
                    return false;
                }
                std::size_t total = 0;
                while (total < remaining) {
                    const std::int64_t read = files_->Read(
                        stream->second.descriptor, buffer + total,
                        remaining - total);
                    if (read <= 0) break;
                    total += static_cast<std::size_t>(read);
                }
                std::string value(total, '\0');
                const bool copied =
                    total == 0 || heap_->Read(buffer, value.data(), total);
                std::string ignored;
                const bool released = heap_->Release(buffer, &ignored);
                if (!released || !copied || total != remaining) {
                    SetError(call_error,
                             "guest JNI Scanner.next asset read failed");
                    return false;
                }
                const std::uint64_t reference =
                    AllocateCompatibilityReference(
                        heap_, CompatibilityReferenceKind::kString,
                        "Ljava/lang/String;", value, call_error);
                if (reference == 0) return false;
                object_references_.emplace(
                    reference,
                    ObjectReference{reference, (*registers)[2],
                                    "Ljava/lang/String;", std::move(value),
                                    0});
                TrackLocalReferenceLocked(reference);
                scanner->second.consumed = true;
                (*registers)[0] = reference;
                return true;
            }
            if (backend_ == nullptr &&
                method->second.class_type ==
                    "android/content/res/AssetManager" &&
                method->second.name == "open" &&
                signature ==
                    "(Ljava/lang/String;)Ljava/io/InputStream;") {
                const std::optional<std::string_view> receiver_type =
                    ObjectTypeLocked((*registers)[1]);
                if (!receiver_type ||
                    NormalizeCompatibilityType(*receiver_type) != "android/content/res/AssetManager") {
                    SetError(call_error,
                             "guest JNI AssetManager.open receiver is invalid");
                    return false;
                }
                std::uint64_t name_reference = 0;
                if (!ReadGuestGeneralArgument(address_space_,
                                              (*registers)[3],
                                              &name_reference, call_error))
                    return false;
                const auto local_name =
                    FindOwnedLocalObjectReferenceLocked(name_reference);
                const auto global_name =
                    global_object_references_.find(name_reference);
                const std::string* name =
                    local_name != object_references_.end() &&
                            IsStringTypeName(local_name->second.type)
                        ? &local_name->second.string_value
                        : global_name != global_object_references_.end() &&
                                  IsStringTypeName(global_name->second.type)
                              ? &global_name->second.string_value
                              : nullptr;
                if (name == nullptr) {
                    SetError(call_error,
                             "guest JNI AssetManager.open path is invalid");
                    return false;
                }
                // EA's appbundle:/ scheme surfaces absolute guest paths
                // (e.g. /app/assets/main.0...obb); resolve those through
                // the /app mount instead of demanding relative names.
                std::string opened_path;
                if (name->starts_with("/app/")) {
                    opened_path = *name;
                } else if (!IsSafeRelativeAssetPath(*name)) {
                    SetError(call_error,
                             "guest JNI AssetManager.open path is invalid");
                    return false;
                }
                const std::string path =
                    opened_path.empty() ? "/app/assets/" + *name : opened_path;
                const std::uint64_t path_address =
                    heap_->Allocate(path.size() + 1);
                std::string ignored;
                if (path_address == 0 ||
                    !heap_->Write(path_address, path.c_str(),
                                  path.size() + 1)) {
                    if (path_address != 0)
                        (void)heap_->Release(path_address, &ignored);
                    SetError(call_error,
                             "guest JNI AssetManager.open path staging failed");
                    return false;
                }
                const int descriptor = files_->Open(path_address, 0, 0);
                if (!heap_->Release(path_address, &ignored)) {
                    if (descriptor >= 0) (void)files_->Close(descriptor);
                    SetError(call_error,
                             "guest JNI AssetManager.open path cleanup failed");
                    return false;
                }
                if (descriptor < 0) {
                    SetError(call_error,
                             "guest JNI AssetManager.open mounted asset is unavailable: " +
                                 *name);
                    return false;
                }
                constexpr std::string_view kInputStreamType =
                    "Ljava/io/InputStream;";
                const std::uint64_t reference = AllocateJniReference(
                    heap_, true, CompatibilityReferenceKind::kObject,
                    kInputStreamType, {}, call_error);
                if (reference == 0) {
                    (void)files_->Close(descriptor);
                    return false;
                }
                object_references_.emplace(
                    reference,
                    ObjectReference{reference, (*registers)[2],
                                    std::string(kInputStreamType), {}, 0});
                input_streams_.emplace(
                    reference, InputStreamStorage{descriptor});
                TrackLocalReferenceLocked(reference);
                (*registers)[0] = reference;
                return true;
            }
            constexpr std::string_view kLoadClassSignature =
                "(Ljava/lang/String;)Ljava/lang/Class;";
            if (method->second.name == "loadClass" &&
                signature == kLoadClassSignature) {
                if (backend_ != nullptr) {
                    SetError(call_error,
                             "guest JNI CallObjectMethodV loadClass is unavailable with an active backend");
                    return false;
                }
                const std::optional<std::string_view> receiver_type =
                    ObjectTypeLocked((*registers)[1]);
                if (!receiver_type ||
                    NormalizeCompatibilityType(*receiver_type) != "java/lang/ClassLoader") {
                    SetError(call_error,
                             "guest JNI CallObjectMethodV loadClass receiver is invalid");
                    return false;
                }
                std::uint64_t argument = 0;
                if (!ReadGuestGeneralArgument(address_space_,
                                              (*registers)[3], &argument,
                                              call_error))
                    return false;
                const auto local_string =
                    FindOwnedLocalObjectReferenceLocked(argument);
                const auto global_string =
                    global_object_references_.find(argument);
                if ((local_string == object_references_.end() &&
                     global_string == global_object_references_.end()) ||
                    (local_string != object_references_.end()
                         ? local_string->second.type
                         : global_string->second.type) !=
                        "Ljava/lang/String;") {
                    SetError(call_error,
                             "guest JNI CallObjectMethodV loadClass argument is not a string reference");
                    return false;
                }
                const std::string& class_name =
                    local_string != object_references_.end()
                        ? local_string->second.string_value
                        : global_string->second.string_value;
                if (!IsBoundedMetadata(class_name, 1024)) {
                    SetError(call_error,
                             "guest JNI CallObjectMethodV loadClass name is invalid");
                    return false;
                }
                const std::uint64_t reference =
                    AllocateCompatibilityReference(
                        heap_, CompatibilityReferenceKind::kClass,
                        class_name, {}, call_error);
                if (reference == 0) {
                    SetError(call_error,
                             "guest JNI class reference allocation failed");
                    return false;
                }
                class_references_.emplace(
                    reference, ClassReference{reference, class_name, 0});
                TrackLocalReferenceLocked(reference);
                (*registers)[0] = reference;
                return true;
            }
            constexpr std::string_view kIntentStringArraySignature =
                "(Ljava/lang/String;)[Ljava/lang/String;";
            if (method->second.name == "getIntentStringArrayExtra" &&
                signature == kIntentStringArraySignature) {
                if (backend_ != nullptr) {
                    SetError(call_error,
                             "guest JNI CallObjectMethodV intent string array is unavailable with an active backend");
                    return false;
                }
                std::uint64_t argument = 0;
                if (!ReadGuestGeneralArgument(address_space_,
                                              (*registers)[3], &argument,
                                              call_error))
                    return false;
                const auto local_string =
                    FindOwnedLocalObjectReferenceLocked(argument);
                const auto global_string =
                    global_object_references_.find(argument);
                if ((local_string == object_references_.end() &&
                     global_string == global_object_references_.end()) ||
                    (local_string != object_references_.end()
                         ? local_string->second.type
                         : global_string->second.type) !=
                        "Ljava/lang/String;") {
                    SetError(call_error,
                             "guest JNI CallObjectMethodV intent key is not a string reference");
                    return false;
                }
                (*registers)[0] = 0;
                return true;
            }
            constexpr std::string_view kGetSystemServiceSignature =
                "(Ljava/lang/String;)Ljava/lang/Object;";
            if (method->second.name == "getSystemService" &&
                signature == kGetSystemServiceSignature) {
                if (backend_ != nullptr) {
                    SetError(call_error,
                             "guest JNI CallObjectMethodV getSystemService is unavailable with an active backend");
                    return false;
                }
                const std::optional<std::string_view> receiver_type =
                    ObjectTypeLocked((*registers)[1]);
                if (!receiver_type ||
                    !receiver_type->starts_with('L') ||
                    !receiver_type->ends_with("Activity;")) {
                    SetError(call_error,
                             "guest JNI CallObjectMethodV getSystemService receiver is invalid");
                    return false;
                }
                std::uint64_t argument = 0;
                if (!ReadGuestGeneralArgument(address_space_,
                                              (*registers)[3], &argument,
                                              call_error))
                    return false;
                const auto local_string =
                    FindOwnedLocalObjectReferenceLocked(argument);
                const auto global_string =
                    global_object_references_.find(argument);
                const std::string* service =
                    local_string != object_references_.end() &&
                            IsStringTypeName(local_string->second.type)
                        ? &local_string->second.string_value
                        : global_string !=
                                      global_object_references_.end() &&
                                  IsStringTypeName(global_string->second.type)
                              ? &global_string->second.string_value
                              : nullptr;
                if (service == nullptr) {
                    SetError(call_error,
                             "guest JNI CallObjectMethodV getSystemService argument is not a string reference");
                    return false;
                }
                if (*service == "location") {
                    // No native Windows location manager is connected. The
                    // Java compatibility facade exposes service absence
                    // instead of fabricating a device location.
                    (*registers)[0] = 0;
                    return true;
                }
                // System-service stubs: return a typed object so follow-up
                // method calls land on the fail-open method dispatchers
                // instead of a null receiver. Services absent on a desktop
                // (vibrator, sensors, telephony) still yield a stub object —
                // the guest's own capability checks (hasVibrator(),
                // getDefaultSensor(), ...) report the device absent.
                static const std::unordered_map<std::string_view,
                                                std::string_view>
                    kSystemServices = {
                        {"audio", "Landroid/media/AudioManager;"},
                        {"vibrator", "Landroid/os/Vibrator;"},
                        {"window", "Landroid/view/WindowManager;"},
                        {"activity", "Landroid/app/ActivityManager;"},
                        {"connectivity", "Landroid/net/ConnectivityManager;"},
                        {"input_method",
                         "Landroid/view/inputmethod/InputMethodManager;"},
                        {"power", "Landroid/os/PowerManager;"},
                        {"alarm", "Landroid/app/AlarmManager;"},
                        {"notification", "Landroid/app/NotificationManager;"},
                        {"sensor", "Landroid/hardware/SensorManager;"},
                        {"storage",
                         "Landroid/os/storage/StorageManager;"},
                        {"telephony", "Landroid/telephony/TelephonyManager;"},
                        {"keyguard", "Landroid/app/KeyguardManager;"},
                        {"uimode", "Landroid/app/UiModeManager;"},
                        {"layout_inflater", "Landroid/view/LayoutInflater;"},
                        {"clipboard", "Landroid/content/ClipboardManager;"},
                        {"accessibility",
                         "Landroid/view/accessibility/AccessibilityManager;"},
                        {"display", "Landroid/hardware/display/DisplayManager;"},
                        {"download", "Landroid/app/DownloadManager;"},
                        {"search", "Landroid/app/SearchManager;"},
                        {"wifi", "Landroid/net/wifi/WifiManager;"},
                        {"battery", "Landroid/os/BatteryManager;"},
                        {"nfc", "Landroid/nfc/NfcManager;"},
                        {"device_policy",
                         "Landroid/app/admin/DevicePolicyManager;"},
                        {"usagestats", "Landroid/app/usage/UsageStatsManager;"},
                        {"appops", "Landroid/app/AppOpsManager;"},
                        {"account", "Landroid/accounts/AccountManager;"},
                        {"jobscheduler", "Landroid/app/job/JobScheduler;"},
                        {"media_session",
                         "Landroid/media/session/MediaSessionManager;"},
                        {"media_router",
                         "Landroid/media/MediaRouter;"},
                    };
                const auto service_type = kSystemServices.find(*service);
                if (service_type == kSystemServices.end()) {
                    // Unknown service: report absence rather than aborting
                    // the frame on a name the compat table does not know.
                    (*registers)[0] = 0;
                    return true;
                }
                const std::uint64_t reference =
                    AllocateCompatibilityReference(
                        heap_, CompatibilityReferenceKind::kObject,
                        service_type->second, {}, call_error);
                if (reference == 0) {
                    SetError(call_error,
                             "guest JNI system service reference allocation failed");
                    return false;
                }
                object_references_.emplace(
                    reference,
                    ObjectReference{reference, (*registers)[2],
                                    std::string(service_type->second), {},
                                    0});
                TrackLocalReferenceLocked(reference);
                (*registers)[0] = reference;
                return true;
            }
            constexpr std::string_view kGetAudioPropertySignature =
                "(Ljava/lang/String;)Ljava/lang/String;";
            if (method->second.name == "getProperty" &&
                signature == kGetAudioPropertySignature) {
                if (backend_ != nullptr) {
                    SetError(call_error,
                             "guest JNI CallObjectMethodV getProperty is unavailable with an active backend");
                    return false;
                }
                const std::optional<std::string_view> receiver_type =
                    ObjectTypeLocked((*registers)[1]);
                if (!receiver_type ||
                    NormalizeCompatibilityType(*receiver_type) != "android/media/AudioManager") {
                    SetError(call_error,
                             "guest JNI CallObjectMethodV getProperty receiver is invalid");
                    return false;
                }
                std::uint64_t argument = 0;
                if (!ReadGuestGeneralArgument(address_space_,
                                              (*registers)[3], &argument,
                                              call_error))
                    return false;
                const auto local_string =
                    FindOwnedLocalObjectReferenceLocked(argument);
                const auto global_string =
                    global_object_references_.find(argument);
                const std::string* key =
                    local_string != object_references_.end()
                        ? &local_string->second.string_value
                        : global_string != global_object_references_.end()
                              ? &global_string->second.string_value
                              : nullptr;
                if ((local_string == object_references_.end() &&
                     global_string == global_object_references_.end()) ||
                    (local_string != object_references_.end()
                         ? local_string->second.type
                         : global_string->second.type) !=
                        "Ljava/lang/String;" ||
                    key == nullptr ||
                    (*key != "android.media.property.OUTPUT_SAMPLE_RATE" &&
                     *key !=
                         "android.media.property.OUTPUT_FRAMES_PER_BUFFER")) {
                    SetError(call_error,
                             "guest JNI CallObjectMethodV getProperty argument is not a supported output-audio key");
                    return false;
                }
                const std::string_view value =
                    *key == "android.media.property.OUTPUT_SAMPLE_RATE"
                        ? std::string_view("48000")
                        : std::string_view("256");
                const std::uint64_t reference =
                    AllocateCompatibilityReference(
                        heap_, CompatibilityReferenceKind::kString,
                        "Ljava/lang/String;", value, call_error);
                if (reference == 0) {
                    SetError(call_error,
                             "guest JNI audio property string allocation failed");
                    return false;
                }
                object_references_.emplace(
                    reference,
                    ObjectReference{reference, (*registers)[2],
                                    "Ljava/lang/String;", std::string(value),
                                    0});
                TrackLocalReferenceLocked(reference);
                (*registers)[0] = reference;
                return true;
            }
            if (backend_ == nullptr &&
                method->second.name == "getDevices" &&
                signature == "(I)[Landroid/media/AudioDeviceInfo;") {
                // Unity's Android audio layer polls the output-device
                // inventory every frame and keeps re-running device-change
                // handling while it answers null/empty; the flask loader
                // spins forever in that retry loop. Serve one stable
                // built-in sink so discovery completes and stays done.
                const std::optional<std::string_view> receiver_type =
                    ObjectTypeLocked((*registers)[1]);
                if (!receiver_type ||
                    (*receiver_type != "android/media/AudioManager" &&
                     NormalizeCompatibilityType(*receiver_type) !=
                         "android/media/AudioManager")) {
                    SetError(call_error,
                             "guest JNI CallObjectMethodV getDevices receiver is invalid");
                    return false;
                }
                const std::uint64_t info_reference =
                    AllocateCompatibilityReference(
                        heap_, CompatibilityReferenceKind::kObject,
                        "android/media/AudioDeviceInfo", {}, call_error);
                if (info_reference == 0) return false;
                object_references_.emplace(
                    info_reference,
                    ObjectReference{info_reference, 0,
                                    "android/media/AudioDeviceInfo", {}, 0});
                TrackLocalReferenceLocked(info_reference);
                std::vector<std::optional<ObjectArrayElement>> elements;
                elements.push_back(ObjectArrayElement{
                    false, info_reference, 0,
                    "Landroid/media/AudioDeviceInfo;", {}});
                const std::uint64_t array_reference =
                    AllocateCompatibilityReference(
                        heap_, CompatibilityReferenceKind::kObject,
                        "[Landroid/media/AudioDeviceInfo;", {}, call_error);
                if (array_reference == 0) return false;
                object_references_.emplace(
                    array_reference,
                    ObjectReference{array_reference, 0,
                                    "[Landroid/media/AudioDeviceInfo;", {},
                                    0});
                TrackLocalReferenceLocked(array_reference);
                object_arrays_.emplace(
                    array_reference,
                    ObjectArrayStorage{"[Landroid/media/AudioDeviceInfo;",
                                       "Landroid/media/AudioDeviceInfo;",
                                       std::move(elements)});
                (*registers)[0] = array_reference;
                return true;
            }
            if (backend_ == nullptr &&
                method->second.class_type == "com/king/http/HttpManager" &&
                method->second.name == "createTask" &&
                signature ==
                    "(JILjava/lang/String;IZLcom/king/http/HttpHeaders;[B)Lcom/king/http/HttpTask;") {
                const std::optional<std::string_view> receiver_type =
                    ObjectTypeLocked((*registers)[1]);
                if (!receiver_type ||
                    (*receiver_type != "com/king/http/HttpManager" &&
                     NormalizeCompatibilityType(*receiver_type) != "com/king/http/HttpManager")) {
                    SetError(call_error,
                             "guest JNI CallObjectMethodV HttpManager.createTask "
                             "receiver is invalid");
                    return false;
                }
                std::array<std::uint64_t, 7> task_arguments{};
                for (std::size_t index = 0; index < task_arguments.size();
                     ++index) {
                    if (!ReadGuestGeneralArgument(
                            address_space_, (*registers)[3],
                            &task_arguments[index], call_error, index))
                        return false;
                }
                const auto local_url =
                    FindOwnedLocalObjectReferenceLocked(task_arguments[2]);
                const auto global_url =
                    global_object_references_.find(task_arguments[2]);
                const std::string* url =
                    local_url != object_references_.end() &&
                            IsStringTypeName(local_url->second.type)
                        ? &local_url->second.string_value
                        : global_url != global_object_references_.end() &&
                                  IsStringTypeName(global_url->second.type)
                              ? &global_url->second.string_value
                              : nullptr;
                if (url == nullptr) {
                    SetError(call_error,
                             "guest JNI CallObjectMethodV HttpManager.createTask "
                             "URL is not a string reference");
                    return false;
                }
                if (task_arguments[4] > 1) {
                    SetError(call_error,
                             "guest JNI CallObjectMethodV HttpManager.createTask "
                             "boolean argument is invalid");
                    return false;
                }
                std::uint64_t headers_identity = 0;
                if (task_arguments[5] != 0) {
                    const std::optional<std::string_view> headers_type =
                        ObjectTypeLocked(task_arguments[5]);
                    const std::optional<std::uint64_t> identity =
                        ReferenceIdentityLocked(task_arguments[5]);
                    if (!identity || !headers_type ||
                        (*headers_type != "com/king/http/HttpHeaders" &&
                         NormalizeCompatibilityType(*headers_type) != "com/king/http/HttpHeaders")) {
                        SetError(call_error,
                                 "guest JNI CallObjectMethodV HttpManager.createTask "
                                 "headers object is invalid");
                        return false;
                    }
                    headers_identity = *identity;
                }
                const std::vector<std::uint8_t>* body = nullptr;
                if (task_arguments[6] != 0) {
                    const std::optional<std::string_view> body_type =
                        ObjectTypeLocked(task_arguments[6]);
                    const std::optional<std::uint64_t> identity =
                        ReferenceIdentityLocked(task_arguments[6]);
                    const auto bytes = identity
                                           ? byte_arrays_.find(*identity)
                                           : byte_arrays_.end();
                    if (!identity || !body_type || *body_type != "[B" ||
                        bytes == byte_arrays_.end()) {
                        SetError(call_error,
                                 "guest JNI CallObjectMethodV HttpManager.createTask "
                                 "body is not a byte-array reference");
                        return false;
                    }
                    body = &bytes->second;
                }
                const std::string return_type =
                    "Lcom/king/http/HttpTask;";
                const std::uint64_t reference = AllocateCompatibilityReference(
                    heap_, CompatibilityReferenceKind::kObject, return_type,
                    {}, call_error);
                if (reference == 0) return false;
                object_references_.emplace(
                    reference,
                    ObjectReference{reference, (*registers)[2], return_type,
                                    {}, 0});
                TrackLocalReferenceLocked(reference);
                http_tasks_.emplace(
                    reference,
                    HttpTaskState{
                        task_arguments[0],
                        static_cast<std::int32_t>(
                            static_cast<std::uint32_t>(task_arguments[1])),
                        *url,
                        static_cast<std::int32_t>(
                            static_cast<std::uint32_t>(task_arguments[3])),
                        task_arguments[4] != 0, headers_identity,
                        body != nullptr ? *body
                                        : std::vector<std::uint8_t>{}});
                RecordHttpLifecycleEventLocked(
                    HttpLifecycleEventKind::kCreateTask,
                    task_arguments[0]);
                (*registers)[0] = reference;
                return true;
            }
            if (backend_ == nullptr &&
                method->second.class_type ==
                    "com/king/usdk/cloudstorage/CloudStorage" &&
                method->second.name == "Load" &&
                signature ==
                    "(Ljava/lang/String;)Ljava/lang/String;") {
                const std::optional<std::string_view> receiver_type =
                    ObjectTypeLocked((*registers)[1]);
                if (!receiver_type ||
                    (*receiver_type !=
                         "com/king/usdk/cloudstorage/CloudStorage" &&
                     NormalizeCompatibilityType(*receiver_type) != "com/king/usdk/cloudstorage/CloudStorage")) {
                    SetError(call_error,
                             "guest JNI CallObjectMethodV CloudStorage.Load "
                             "receiver is invalid");
                    return false;
                }
                std::uint64_t key_reference = 0;
                if (!ReadGuestGeneralArgument(address_space_,
                                              (*registers)[3],
                                              &key_reference, call_error))
                    return false;
                const auto local_key =
                    FindOwnedLocalObjectReferenceLocked(key_reference);
                const auto global_key =
                    global_object_references_.find(key_reference);
                if ((local_key == object_references_.end() &&
                     global_key == global_object_references_.end()) ||
                    (local_key != object_references_.end()
                         ? local_key->second.type
                         : global_key->second.type) !=
                        "Ljava/lang/String;") {
                    SetError(call_error,
                             "guest JNI CallObjectMethodV CloudStorage.Load "
                             "key is not a string reference");
                    return false;
                }
                // A fresh native profile has no cloud data. The retained
                // compatibility contract returns null and performs no I/O.
                (*registers)[0] = 0;
                return true;
            }
            if (backend_ == nullptr &&
                method->second.name.starts_with("create") &&
                method->second.signature.starts_with(
                    "(Ljava/lang/String;Ljava/util/Map;)L") &&
                method->second.signature.back() == ';') {
                // The Glu analytics factory returns a plugin object whose
                // event calls are fire-and-forget; hand back a compat typed
                // object of the advertised analytics type.
                const std::string analytics_type =
                    method->second.signature.substr(
                        method->second.signature.rfind(')') + 1);
                const std::uint64_t reference = AllocateCompatibilityReference(
                    heap_, CompatibilityReferenceKind::kObject, analytics_type,
                    {}, call_error);
                if (reference == 0) {
                    SetError(call_error,
                             "guest createAnalytics allocation failed");
                    return false;
                }
                object_references_.emplace(
                    reference,
                    ObjectReference{reference, 0, analytics_type, {}, 0});
                TrackLocalReferenceLocked(reference);
                (*registers)[0] = reference;
                return true;
            }
            if (backend_ == nullptr &&
                method->second.name == "readProperties" &&
                method->second.signature ==
                    "(Ljava/lang/String;[Ljava/lang/String;)"
                    "[Ljava/lang/String;") {
                // No property store exists in the compat runtime; an empty
                // String[] yields no overrides and the engine keeps its
                // built-in defaults.
                // mutex_ is already held by the enclosing dispatch lambda.
                const std::uint64_t reference = AllocateCompatibilityReference(
                    heap_, CompatibilityReferenceKind::kObject,
                    "[Ljava/lang/String;", {}, call_error);
                if (reference == 0) {
                    SetError(call_error,
                             "guest readProperties array allocation failed");
                    return false;
                }
                object_references_.emplace(
                    reference,
                    ObjectReference{reference, 0, "[Ljava/lang/String;", {},
                                    0});
                TrackLocalReferenceLocked(reference);
                object_arrays_.emplace(
                    reference,
                    ObjectArrayStorage{"[Ljava/lang/String;",
                                       "Ljava/lang/String;", {}});
                (*registers)[0] = reference;
                return true;
            }
            if (backend_ == nullptr &&
                (method->second.name == "longValue" ||
                 method->second.name == "intValue") &&
                (signature == "()J" || signature == "()I")) {
                // Unbox a compat Long/Integer: the decimal value lives in
                // the reference's string storage (see Long.valueOf).
                // mutex_ is already held by the enclosing dispatch lambda.
                const auto local_boxed =
                    FindOwnedLocalObjectReferenceLocked((*registers)[1]);
                const auto global_boxed =
                    global_object_references_.find((*registers)[1]);
                const std::string* decimal =
                    local_boxed != object_references_.end() &&
                            !local_boxed->second.string_value.empty()
                        ? &local_boxed->second.string_value
                        : global_boxed != global_object_references_.end() &&
                                  !global_boxed->second.string_value.empty()
                              ? &global_boxed->second.string_value
                              : nullptr;
                if (decimal == nullptr) {
                    SetError(call_error,
                             "guest JNI unbox receiver has no stored value");
                    return false;
                }
                const std::int64_t value = std::strtoll(
                    decimal->c_str(), nullptr, 10);
                (*registers)[0] = signature == "()J"
                                      ? static_cast<std::uint64_t>(value)
                                      : static_cast<std::uint64_t>(
                                            static_cast<std::int32_t>(value));
                return true;
            }
            if (backend_ == nullptr &&
                method->second.name == "toString" &&
                signature == "()Ljava/lang/String;") {
                // Boxed Long/Integer toString: mirror of the unbox path.
                // Only a Long/Integer receiver is served here; other object
                // types (StringBuilder, Throwable, ...) fall through to the
                // later toString handlers rather than hard-failing.
                const std::optional<std::string_view> boxed_type =
                    ObjectTypeLocked((*registers)[1]);
                const bool is_boxed =
                    boxed_type &&
                    (ReceiverClassMatches(*boxed_type, "java/lang/Long") ||
                     ReceiverClassMatches(*boxed_type, "java/lang/Integer"));
                if (!is_boxed) {
                    // Not a boxed number — let later toString handlers try.
                } else {
                const auto local_boxed =
                    FindOwnedLocalObjectReferenceLocked((*registers)[1]);
                const auto global_boxed =
                    global_object_references_.find((*registers)[1]);
                std::string decimal = "0";
                if (local_boxed != object_references_.end() &&
                    !local_boxed->second.string_value.empty())
                    decimal = local_boxed->second.string_value;
                else if (global_boxed != global_object_references_.end() &&
                         !global_boxed->second.string_value.empty())
                    decimal = global_boxed->second.string_value;
                std::uint64_t string_reference = 0;
                if (!CreateCompatibilityStringReferenceLocked(
                        decimal, &string_reference, call_error))
                    return false;
                (*registers)[0] = string_reference;
                return true;
                }
            }
            if (backend_ == nullptr &&
                (normalized_method_class == "java/lang/StringBuilder" ||
                 normalized_method_class == "java/lang/StringBuffer" ||
                 normalized_method_class ==
                     "java/lang/AbstractStringBuilder")) {
                // java.lang StringBuilder/StringBuffer share the
                // AbstractStringBuilder mutable-content model: the receiver
                // accumulates appended text and toString() hands it back.
                // Read/write the registered object's string_value directly —
                // the receiver is a real compat object even when the call
                // arrives on a different attached thread than the creator.
                const auto local_builder =
                    object_references_.find((*registers)[1]);
                const auto global_builder =
                    global_object_references_.find((*registers)[1]);
                auto* builder_value =
                    local_builder != object_references_.end()
                        ? &local_builder->second.string_value
                        : global_builder != global_object_references_.end()
                              ? &global_builder->second.string_value
                              : nullptr;
                if (method->second.name == "toString" &&
                    signature == "()Ljava/lang/String;") {
                    std::uint64_t string_reference = 0;
                    if (!CreateCompatibilityStringReferenceLocked(
                            builder_value != nullptr ? *builder_value
                                                     : std::string(),
                            &string_reference, call_error))
                        return false;
                    (*registers)[0] = string_reference;
                    return true;
                }
                if (method->second.name == "append") {
                    std::string piece;
                    const char first =
                        signature.size() > 1 ? signature[1] : '\0';
                    if (first == 'L' || first == '[') {
                        std::uint64_t arg_reference = 0;
                        if (!ReadGuestGeneralArgument(
                                address_space_, (*registers)[3],
                                &arg_reference, call_error, 0))
                            return false;
                        if (arg_reference == 0) {
                            piece = "null";
                        } else {
                            const auto local_arg =
                                object_references_.find(arg_reference);
                            const auto global_arg =
                                global_object_references_.find(arg_reference);
                            const std::string* arg_value =
                                local_arg != object_references_.end()
                                    ? &local_arg->second.string_value
                                    : global_arg !=
                                              global_object_references_.end()
                                          ? &global_arg->second.string_value
                                          : nullptr;
                            if (arg_value != nullptr &&
                                !arg_value->empty()) {
                                piece = *arg_value;
                            } else {
                                // An object without captured text appends its
                                // type identity, matching Java's default
                                // Object.toString shape closely enough for a
                                // builder that only feeds logging.
                                const std::string arg_type =
                                    local_arg != object_references_.end()
                                        ? local_arg->second.type
                                        : global_arg !=
                                                  global_object_references_
                                                      .end()
                                              ? global_arg->second.type
                                              : "Ljava/lang/Object;";
                                char address_text[32];
                                snprintf(address_text, sizeof(address_text),
                                         "@0x%llx",
                                         (unsigned long long)arg_reference);
                                piece = arg_type + address_text;
                            }
                        }
                    } else {
                        std::uint64_t raw = 0;
                        if (!ReadGuestGeneralArgument(
                                address_space_, (*registers)[3], &raw,
                                call_error, 0))
                            return false;
                        switch (first) {
                            case 'I':
                                piece = std::to_string(
                                    static_cast<std::int32_t>(raw));
                                break;
                            case 'J':
                                piece = std::to_string(
                                    static_cast<std::int64_t>(raw));
                                break;
                            case 'Z':
                                piece = raw != 0 ? "true" : "false";
                                break;
                            case 'C':
                                piece.assign(
                                    1, static_cast<char>(raw & 0xffff));
                                break;
                            default:
                                break;
                        }
                    }
                    if (builder_value != nullptr) *builder_value += piece;
                    // append(x) returns the same builder instance.
                    (*registers)[0] = (*registers)[1];
                    return true;
                }
            }
            if (backend_ == nullptr &&
                (method->second.name == "put" ||
                 method->second.name == "get" ||
                 method->second.name == "remove") &&
                (signature ==
                     "(Ljava/lang/Object;Ljava/lang/Object;)"
                     "Ljava/lang/Object;" ||
                 signature == "(Ljava/lang/Object;)Ljava/lang/Object;")) {
                // java.util.Map traffic on compat-created maps (bundles,
                // intent extras): a fresh map answers put with the absent
                // previous value (null) and get/remove with null.
                (*registers)[0] = 0;
                return true;
            }
            if (backend_ == nullptr &&
                (method->second.name == "size" ||
                 method->second.name == "isEmpty") &&
                (signature == "()I" || signature == "()Z")) {
                // Compat-created collections hold nothing.
                (*registers)[0] =
                    method->second.name == "size" ? 0 : 1;
                return true;
            }
            if (backend_ == nullptr &&
                method->second.name == "readSharedProperty" &&
                signature == "(Ljava/lang/String;)Ljava/lang/String;") {
                // Consent flow property lookup; serve from the string store
                // the engine filled via setToStringStore, empty otherwise.
                // mutex_ is already held by the enclosing dispatch lambda.
                std::uint64_t key_reference = 0;
                if (!ReadGuestGeneralArgument(address_space_,
                                              (*registers)[3],
                                              &key_reference, call_error, 0))
                    return false;
                const auto local_key =
                    FindOwnedLocalObjectReferenceLocked(key_reference);
                const auto global_key =
                    global_object_references_.find(key_reference);
                const std::string* key =
                    local_key != object_references_.end() &&
                            IsStringTypeName(local_key->second.type)
                        ? &local_key->second.string_value
                        : global_key != global_object_references_.end() &&
                                  IsStringTypeName(global_key->second.type)
                              ? &global_key->second.string_value
                              : nullptr;
                if (key == nullptr) {
                    SetError(call_error,
                             "guest readSharedProperty key is invalid");
                    return false;
                }
                const auto stored = compat_string_store_.find(*key);
                const std::string value =
                    stored != compat_string_store_.end() ? stored->second
                                                         : std::string();
                std::uint64_t string_reference = 0;
                if (!CreateCompatibilityStringReferenceLocked(
                        value, &string_reference, call_error))
                    return false;
                (*registers)[0] = string_reference;
                return true;
            }
            if (backend_ == nullptr &&
                method->second.name == "readAssetAsString" &&
                signature == "(Ljava/lang/String;)Ljava/lang/String;") {
                // Glu consent/config step: after the first onTimerEvent
                // fires, the platform reads its config asset as one
                // string. Serve it from the /app assets mount; an
                // unreadable asset yields an empty string so the caller
                // keeps its defaults instead of failing the session.
                std::uint64_t name_reference = 0;
                if (!ReadGuestGeneralArgument(address_space_,
                                              (*registers)[3],
                                              &name_reference, call_error, 0))
                    return false;
                const auto local_name =
                    FindOwnedLocalObjectReferenceLocked(name_reference);
                const auto global_name =
                    global_object_references_.find(name_reference);
                const std::string* name =
                    local_name != object_references_.end() &&
                            IsStringTypeName(local_name->second.type)
                        ? &local_name->second.string_value
                        : global_name != global_object_references_.end() &&
                                  IsStringTypeName(global_name->second.type)
                              ? &global_name->second.string_value
                              : nullptr;
                if (name == nullptr) {
                    SetError(call_error,
                             "guest JNI readAssetAsString path is invalid");
                    return false;
                }
                std::string value;
                if (name->starts_with("/app/") ||
                    IsSafeRelativeAssetPath(*name)) {
                    const std::string path = name->starts_with("/app/")
                                                 ? *name
                                                 : "/app/assets/" + *name;
                    const std::uint64_t path_address =
                        heap_->Allocate(path.size() + 1);
                    std::string ignored;
                    if (path_address != 0 &&
                        heap_->Write(path_address, path.c_str(),
                                     path.size() + 1)) {
                        const int descriptor =
                            files_->Open(path_address, 0, 0);
                        if (descriptor >= 0) {
                            const std::int64_t end =
                                files_->Lseek64(descriptor, 0, 2);
                            if (0 <= end &&
                                end <= 16 * 1024 * 1024) {
                                const std::size_t size =
                                    static_cast<std::size_t>(end);
                                const std::uint64_t buffer = heap_->Allocate(
                                    size == 0 ? 1 : size);
                                std::size_t total = 0;
                                while (buffer != 0 && total < size) {
                                    const std::int64_t got = files_->Read(
                                        descriptor, buffer + total,
                                        size - total);
                                    if (got <= 0) break;
                                    total += static_cast<std::size_t>(got);
                                }
                                value.resize(total);
                                if (total != 0 &&
                                    !heap_->Read(buffer, value.data(), total))
                                    value.clear();
                                if (buffer != 0)
                                    (void)heap_->Release(buffer, &ignored);
                            }
                            (void)files_->Close(descriptor);
                        } else {
                            std::fprintf(stderr,
                                         "ISOMR_READ_ASSET missing=%s\n",
                                         name->c_str());
                            std::fflush(stderr);
                        }
                        (void)heap_->Release(path_address, &ignored);
                    }
                }
                std::uint64_t string_reference = 0;
                // Already under mutex_ (the dispatch lambda holds it);
                // the public wrapper would relock and throw.
                if (!CreateCompatibilityStringReferenceLocked(
                        value, &string_reference, call_error))
                    return false;
                (*registers)[0] = string_reference;
                return true;
            }
            if (ApplyCompatOverride("object", method->second.class_type,
                                    method->second.name,
                                    method->second.signature, registers,
                                    call_error))
                return true;
            // A string-valued override satisfies object calls whose
            // advertised return type is java/lang/String (paths, names,
            // identifiers the engine dereferences immediately).
            if (signature.ends_with(")Ljava/lang/String;") &&
                ApplyCompatOverride("string", method->second.class_type,
                                    method->second.name,
                                    method->second.signature, registers,
                                    call_error))
                return true;
            if (backend_ == nullptr &&
                NormalizeCompatibilityType(method->second.class_type) ==
                    "android/os/Handler" &&
                method->second.name == "obtainMessage" &&
                method->second.signature ==
                    "(I)Landroid/os/Message;") {
                // Unity posts its native task-queue wakeup as
                // handler.obtainMessage(0).sendToTarget(). Fabricate a real
                // Message reference and bind the handler's Callback proxy
                // (captured at construction) so sendToTarget can enqueue the
                // peer handle for the UI drain to run.
                const std::uint64_t reference = AllocateJniReference(
                    heap_, true, CompatibilityReferenceKind::kObject,
                    "Landroid/os/Message;", {}, call_error);
                if (reference == 0) {
                    SetError(call_error,
                             "guest JNI obtainMessage reference allocation failed");
                    return false;
                }
                object_references_.emplace(
                    reference,
                    ObjectReference{reference, (*registers)[2],
                                    "Landroid/os/Message;", {}, 0});
                TrackLocalReferenceLocked(reference);
                const std::optional<std::uint64_t> handler_identity =
                    ReferenceIdentityLocked((*registers)[1]);
                const auto callback =
                    handler_identity
                        ? handler_callbacks_.find(*handler_identity)
                        : handler_callbacks_.end();
                if (callback != handler_callbacks_.end()) {
                    message_callbacks_.emplace(reference,
                                               callback->second);
                    if (call_error != nullptr)
                        *call_error = "trace:jni-message-bound";
                } else if (call_error != nullptr)
                    *call_error =
                        "trace:jni-message-unbound-handler0x" +
                        ([](std::uint64_t v) { char b[24];
                            snprintf(b, sizeof(b), "%llx",
                                     (unsigned long long)v);
                            return std::string(b); })((*registers)[1]);
                (*registers)[0] = reference;
                return true;
            }
            // UMP builder setters are fluent: `new Builder().setX(...)`
            // returns the same Builder for chaining. The generic arg-bearing
            // stub returns null, which makes the follow-on call throw a
            // managed NullReferenceException inside the consent coroutine.
            if (backend_ == nullptr &&
                normalized_method_class.starts_with(
                    "com/google/android/ump/") &&
                normalized_method_class.ends_with("$Builder")) {
                if (method->second.name == "build") {
                    const std::string product_type =
                        ReturnType(method->second.signature);
                    const std::uint64_t reference =
                        AllocateCompatibilityReference(
                            heap_, CompatibilityReferenceKind::kObject,
                            product_type, {}, call_error);
                    if (reference == 0) return false;
                    object_references_.emplace(
                        reference,
                        ObjectReference{reference, (*registers)[2],
                                        product_type, {}, 0});
                    TrackLocalReferenceLocked(reference);
                    (*registers)[0] = reference;
                    return true;
                }
                (*registers)[0] = (*registers)[1];
                return true;
            }
            if (!signature.starts_with("()") || close == std::string::npos ||
                close + 1 >= signature.size() ||
                (signature[close + 1] != 'L' &&
                 signature[close + 1] != '[')) {
                if (backend_ == nullptr) {
                    // No Java backend runs the target; an unimplemented
                    // object method with arguments returns the JNI null
                    // default rather than aborting the caller.
                    if (call_error != nullptr)
                        *call_error =
                            "trace:jni-call-object-stub-" +
                            method->second.class_type + "." +
                            method->second.name + method->second.signature;
                    (*registers)[0] = 0;
                    return true;
                }
                SetError(call_error,
                         "guest JNI CallObjectMethodV method " +
                             method->second.name + " signature " + signature +
                             " is not yet supported");
                return false;
            }
            GuestJniBackend::Handle backend_reference = 0;
            if (backend_ != nullptr) {
                const std::optional<GuestJniBackend::Handle> backend_object =
                    BackendReferenceLocked((*registers)[1]);
                if (!backend_object ||
                    !backend_->CallObjectMethod(*backend_object,
                                                method->second.backend,
                                                false, &backend_reference,
                                                call_error))
                    return false;
                if (backend_reference == 0) {
                    (*registers)[0] = 0;
                    return true;
                }
            }
            const std::string return_type =
                ReturnType(method->second.signature);
            const CompatibilityReferenceKind kind =
                return_type == "Ljava/lang/String;"
                    ? CompatibilityReferenceKind::kString
                    : CompatibilityReferenceKind::kObject;
            const std::uint64_t reference = AllocateJniReference(
                heap_, backend_ == nullptr, kind, return_type, {}, call_error);
            if (reference == 0) {
                if (backend_reference != 0) {
                    std::string ignored;
                    (void)backend_->DeleteLocalRef(backend_reference,
                                                   &ignored);
                }
                SetError(call_error,
                         "guest JNI object reference allocation failed");
                return false;
            }
            object_references_.emplace(
                reference,
                ObjectReference{reference, (*registers)[2], return_type, {},
                                backend_reference});
            EnsureStubArrayStorageLocked(reference, return_type);
            TrackLocalReferenceLocked(reference);
            (*registers)[0] = reference;
            return true;
        };
    const auto call_object_method_a =
        [this, call_object_method_v](
            Aarch64ImportRegistry::Registers* registers,
            std::string* call_error) {
            if (registers == nullptr ||
                !IsCurrentEnvironment((*registers)[0])) {
                SetError(call_error,
                         "guest JNI CallObjectMethodA environment is invalid");
                return false;
            }
            Aarch64VaList synthesized{};
            synthesized.stack = (*registers)[3];
            synthesized.general_top = 0;
            synthesized.vector_top = 0;
            synthesized.general_offset = 0;
            synthesized.vector_offset = 0;
            const std::uint64_t scratch =
                heap_->Callocate(1, sizeof(synthesized));
            if (scratch == 0 ||
                !address_space_->Write(scratch, &synthesized,
                                       sizeof(synthesized))) {
                SetError(call_error,
                         "guest JNI CallObjectMethodA argument frame failed");
                return false;
            }
            Aarch64ImportRegistry::Registers sub = *registers;
            sub[3] = scratch;
            const bool result =
                call_object_method_v(&sub, call_error);
            // The V handler writes the jobject result into registers[0]; the
            // copy above means that landed in sub[0], so propagate it back or
            // the caller reads the untouched env pointer as the return value.
            if (result) (*registers)[0] = sub[0];
            return result;
        };
    const auto call_boolean_method_v =
        [this](Aarch64ImportRegistry::Registers* registers,
               std::string* call_error) {
            if (registers == nullptr ||
                !IsCurrentEnvironment((*registers)[0]) ||
                (*registers)[3] == 0) {
                SetError(call_error,
                         "guest JNI CallBooleanMethodV arguments are invalid");
                return false;
            }
            std::array<std::uint8_t, 32> arguments{};
            if (!address_space_->Read((*registers)[3], arguments.data(),
                                      arguments.size())) {
                SetError(call_error,
                         "guest JNI CallBooleanMethodV va_list is invalid");
                return false;
            }
            const std::lock_guard lock(mutex_);
            {
                // SexyAppFramework config lookups: the compat runtime
                // provisions no config keys, so existence checks report
                // absent and callers fall back to defaults.
                const auto method0 = method_references_.find((*registers)[2]);
                if (method0 != method_references_.end() &&
                    method0->second.name == "Config_ConfigKeyExists" &&
                    method0->second.signature ==
                        "(Ljava/lang/String;)Z") {
                    (*registers)[0] = 0;
                    return true;
                }
            }
            const auto motion_bool =
                motion_events_.find(ReferenceIdentityLocked((*registers)[1])
                                        .value_or((*registers)[1]));
            if (motion_bool != motion_events_.end()) {
                const auto motion_method =
                    method_references_.find((*registers)[2]);
                const std::string_view motion_name =
                    motion_method != method_references_.end()
                        ? std::string_view(motion_method->second.name)
                        : std::string_view();
                std::uint64_t motion_result = 0;
                if (motion_name == "isFromSource") {
                    std::uint64_t source_mask = 0;
                    if (ReadGuestGeneralArgument(address_space_,
                                                 (*registers)[3],
                                                 &source_mask,
                                                 call_error, 0)) {
                        motion_result =
                            (0x1002ULL & source_mask) == source_mask &&
                                    source_mask != 0
                                ? 1
                                : 0;
                    }
                } else if (motion_name == "isTouchEvent" ||
                           motion_name == "isHoverEvent" ||
                           motion_name == "isTargetFromSource") {
                    motion_result = 1;
                }
                (*registers)[0] = motion_result;
                return true;
            }
            {
                const std::uint64_t receiver =
                    ReferenceIdentityLocked((*registers)[1])
                        .value_or((*registers)[1]);
                if (input_devices_.find(receiver) != input_devices_.end()) {
                    const auto dev_method =
                        method_references_.find((*registers)[2]);
                    const std::string_view dev_name =
                        dev_method != method_references_.end()
                            ? std::string_view(dev_method->second.name)
                            : std::string_view();
                    std::uint64_t result = 0;
                    if (dev_name == "isEnabled") {
                        result = 1;
                    } else if (dev_name == "supportsSource") {
                        std::uint64_t mask = 0;
                        if (ReadGuestGeneralArgument(address_space_,
                                                     (*registers)[3], &mask,
                                                     call_error, 0))
                            result =
                                (0x1002ULL & mask) == mask && mask != 0 ? 1
                                                                        : 0;
                    }
                    // isVirtual/isExternal/isFullKeyboard/isGamePad/isClosed
                    // and friends stay 0 (a real, always-on touchscreen).
                    (*registers)[0] = result;
                    return true;
                }
            }
            const auto audio_device_bool =
                method_references_.find((*registers)[2]);
            if (backend_ == nullptr &&
                audio_device_bool != method_references_.end() &&
                NormalizeCompatibilityType(
                    audio_device_bool->second.class_type) ==
                    "android/media/AudioDeviceInfo" &&
                audio_device_bool->second.signature == "()Z") {
                // The getDevices descriptor must present as an output sink:
                // isSink false would return the inventory to "no usable
                // device" and re-enter the flask retry loop.
                // isSource/isDirect stay 0 (output-only, non-direct).
                (*registers)[0] =
                    audio_device_bool->second.name == "isSink" ? 1 : 0;
                return true;
            }
            if ((*registers)[1] == 0) {
                // Null receiver (and typically a null method id): the guest
                // reached a JNI helper whose cached state was never
                // initialized. Real JNI would fault; serve the Java default
                // (false) and keep booting, with the call on record.
                static std::size_t boolean_null_receiver_logs = 0;
                if (boolean_null_receiver_logs < 8) {
                    ++boolean_null_receiver_logs;
                    std::fprintf(
                        stderr,
                        "ISOMR_JNI_BOOL_NULL_RECEIVER lr=%llx served=false\n",
                        static_cast<unsigned long long>(
                            (*registers)[30]));
                    std::fflush(stderr);
                }
                (*registers)[0] = 0;
                return true;
            }
            if (!IsObjectReferenceLocked((*registers)[1])) {
                // Name the call before rejecting: the method id and the
                // receiver kind identify which init path needs a serve.
                static std::size_t boolean_reject_logs = 0;
                if (boolean_reject_logs < 8) {
                    ++boolean_reject_logs;
                    const auto rejected_method =
                        method_references_.find((*registers)[2]);
                    std::fprintf(
                        stderr,
                        "ISOMR_JNI_BOOL_REJECT receiver=%llx class_ref=%d "
                        "global=%d method=%s%s%s\n",
                        static_cast<unsigned long long>((*registers)[1]),
                        IsClassReferenceLocked((*registers)[1]) ? 1 : 0,
                        global_object_references_.contains(
                            (*registers)[1])
                            ? 1
                            : 0,
                        rejected_method != method_references_.end()
                            ? rejected_method->second.class_type.c_str()
                            : "<unknown>",
                        rejected_method != method_references_.end()
                            ? rejected_method->second.name.c_str()
                            : "",
                        rejected_method != method_references_.end()
                            ? rejected_method->second.signature.c_str()
                            : "");
                    std::fflush(stderr);
                }
                SetError(call_error,
                         "guest JNI CallBooleanMethodV object is invalid");
                return false;
            }
            const auto method = method_references_.find((*registers)[2]);
            if (method == method_references_.end() ||
                method->second.is_static) {
                if (backend_ == nullptr) {
                    // No Java backend: a boolean call on an unknown or static
                    // method id returns JNI false instead of aborting.
                    std::fprintf(stderr,
                                 "ISOMR_JNI_BOOL_STUB mid=%llx\n",
                                 (unsigned long long)(*registers)[2]);
                    std::fflush(stderr);
                    (*registers)[0] = 0;
                    return true;
                }
                SetError(call_error,
                         "guest JNI CallBooleanMethodV method is invalid");
                return false;
            }
            const std::optional<std::string_view> object_type =
                ObjectTypeLocked((*registers)[1]);
            RecordHttpObservationEventLocked(
                HttpObservationEventKind::kVoidCall,
                method->second.class_type, method->second.name,
                method->second.signature);
            const auto receiver_matches_declaring_class = [&]() {
                if (!object_type) return false;
                // Nimble stores dotted declaring classes while receivers
                // carry slashed JNI types; compare normalized forms.
                if (ReceiverClassMatches(*object_type,
                                         method->second.class_type))
                    return true;
                if (method->second.class_type.size() >= 2 &&
                    method->second.class_type.front() == 'L' &&
                    method->second.class_type.back() == ';')
                    return *object_type == method->second.class_type;
                return object_type->size() >= 2 &&
                       object_type->front() == 'L' &&
                       object_type->back() == ';' &&
                       object_type->substr(1, object_type->size() - 2) ==
                           method->second.class_type;
            };
            if (!receiver_matches_declaring_class()) {
                SetError(call_error,
                         "guest JNI CallBooleanMethodV receiver " +
                             std::string(object_type.value_or("<unknown>")) +
                             " does not match declaring class " +
                             method->second.class_type);
                return false;
            }
            if (backend_ == nullptr &&
                ReceiverClassMatches(*object_type,
                                     method->second.class_type) &&
                method->second.class_type.find("ISynergyEnvironment") !=
                    std::string_view::npos &&
                method->second.name == "isDataAvailable" &&
                method->second.signature == "()Z") {
                // The Nimble SynergyEnvironment has no downloaded data in
                // the compat runtime.
                (*registers)[0] = 0;
                return true;
            }
            if (backend_ == nullptr &&
                NormalizeCompatibilityType(method->second.class_type) ==
                    "java/lang/String" &&
                method->second.name == "equals" &&
                method->second.signature == "(Ljava/lang/Object;)Z") {
                const auto local_receiver =
                    object_references_.find((*registers)[1]);
                const auto global_receiver =
                    global_object_references_.find((*registers)[1]);
                const std::string* receiver =
                    local_receiver != object_references_.end() &&
                            IsStringTypeName(local_receiver->second.type)
                        ? &local_receiver->second.string_value
                        : global_receiver !=
                                      global_object_references_.end() &&
                                  IsStringTypeName(global_receiver->second.type)
                              ? &global_receiver->second.string_value
                              : nullptr;
                std::uint64_t other_reference = 0;
                if (receiver == nullptr ||
                    !ReadGuestGeneralArgument(address_space_,
                                              (*registers)[3],
                                              &other_reference,
                                              call_error)) {
                    if (call_error == nullptr || call_error->empty())
                        SetError(call_error,
                                 "guest JNI String.equals receiver is invalid");
                    return false;
                }
                if (other_reference == 0) {
                    (*registers)[0] = 0;
                    return true;
                }
                const auto local_other =
                    FindOwnedLocalObjectReferenceLocked(other_reference);
                const auto global_other =
                    global_object_references_.find(other_reference);
                const std::string* other =
                    local_other != object_references_.end() &&
                            IsStringTypeName(local_other->second.type)
                        ? &local_other->second.string_value
                        : global_other !=
                                      global_object_references_.end() &&
                                  IsStringTypeName(global_other->second.type)
                              ? &global_other->second.string_value
                              : nullptr;
                if (other != nullptr) {
                    (*registers)[0] = *receiver == *other ? 1 : 0;
                    return true;
                }
                if (IsObjectReferenceLocked(other_reference) ||
                    IsClassReferenceLocked(other_reference)) {
                    (*registers)[0] = 0;
                    return true;
                }
                SetError(call_error,
                         "guest JNI String.equals argument is invalid");
                return false;
            }
            if (backend_ == nullptr &&
                NormalizeCompatibilityType(method->second.class_type) ==
                    "com/unity3d/player/UnityPlayer" &&
                method->second.name == "initializeGoogleAr" &&
                method->second.signature == "()Z") {
                // Google ARCore is not part of the native Windows profile.
                (*registers)[0] = 0;
                return true;
            }
            if (backend_ == nullptr &&
                NormalizeCompatibilityType(method->second.class_type) ==
                    "com/unity3d/player/PlayAssetDeliveryUnityWrapper" &&
                method->second.name == "playCoreApiMissing" &&
                method->second.signature == "()Z") {
                // Google Play Core is not present in the native Windows
                // profile. Unity uses this signal to stay on local assets.
                (*registers)[0] = 1;
                return true;
            }
            if (backend_ == nullptr &&
                NormalizeCompatibilityType(method->second.class_type) ==
                    "java/util/Scanner" &&
                method->second.name == "hasNext" &&
                method->second.signature == "()Z") {
                const std::optional<std::uint64_t> scanner_identity =
                    ReferenceIdentityLocked((*registers)[1]);
                const auto scanner = scanner_identity
                                         ? scanners_.find(*scanner_identity)
                                         : scanners_.end();
                const auto stream =
                    scanner != scanners_.end()
                        ? input_streams_.find(
                              scanner->second.input_stream_identity)
                        : input_streams_.end();
                if (!scanner_identity || scanner == scanners_.end() ||
                    stream == input_streams_.end() ||
                    stream->second.descriptor < 0) {
                    SetError(call_error,
                             "guest JNI Scanner.hasNext state is invalid");
                    return false;
                }
                const std::int64_t current = files_->Lseek64(
                    stream->second.descriptor, 0, 1);
                const std::int64_t end = files_->Lseek64(
                    stream->second.descriptor, 0, 2);
                if (current < 0 || end < current ||
                    files_->Lseek64(stream->second.descriptor, current, 0) !=
                        current) {
                    SetError(call_error,
                             "guest JNI Scanner.hasNext seek failed");
                    return false;
                }
                (*registers)[0] =
                    !scanner->second.consumed && end > current ? 1 : 0;
                return true;
            }
            if (backend_ == nullptr &&
                NormalizeCompatibilityType(method->second.class_type) ==
                    "java/util/Iterator" &&
                method->second.name == "hasNext" &&
                method->second.signature == "()Z") {
                // SharedPreferences.getAll() represents the native profile's
                // fresh, empty preference store. The generic Map -> Set ->
                // Iterator compatibility chain therefore has no next entry.
                (*registers)[0] = 0;
                return true;
            }
            if (backend_ == nullptr &&
                NormalizeCompatibilityType(method->second.class_type) ==
                    "android/os/Handler" &&
                ((method->second.name == "post" &&
                  method->second.signature ==
                      "(Ljava/lang/Runnable;)Z") ||
                 (method->second.name == "postDelayed" &&
                  method->second.signature ==
                      "(Ljava/lang/Runnable;J)Z"))) {
                std::uint64_t runnable = 0;
                if (!ReadGuestGeneralArgument(address_space_,
                                              (*registers)[3], &runnable,
                                              call_error))
                    return false;
                std::uint64_t delay_milliseconds = 0;
                if (method->second.name == "postDelayed") {
                    std::uint64_t requested_delay = 0;
                    if (!ReadGuestGeneralArgument(
                            address_space_, (*registers)[3],
                            &requested_delay, call_error, 1))
                        return false;
                    const std::int64_t signed_delay =
                        static_cast<std::int64_t>(requested_delay);
                    delay_milliseconds =
                        signed_delay > 0
                            ? static_cast<std::uint64_t>(signed_delay)
                            : 0;
                }
                const std::optional<std::string_view> runnable_type =
                    ObjectTypeLocked(runnable);
                const std::optional<std::uint64_t> runnable_identity =
                    ReferenceIdentityLocked(runnable);
                const auto proxy =
                    runnable_identity
                        ? interface_proxies_.find(*runnable_identity)
                        : interface_proxies_.end();
                const bool implements_runnable =
                    proxy != interface_proxies_.end() &&
                    std::ranges::any_of(
                        proxy->second.interfaces,
                        [](const std::string& interface_type) {
                            return NormalizeCompatibilityType(
                                       interface_type) ==
                                   "java/lang/Runnable";
                        });
                constexpr std::size_t kMaximumPendingUiTasks = 1024;
                if (!runnable_type || !runnable_identity ||
                    !implements_runnable ||
                    pending_ui_tasks_.size() >=
                        kMaximumPendingUiTasks) {
                    std::string interfaces;
                    if (proxy != interface_proxies_.end()) {
                        for (const std::string& interface_type :
                             proxy->second.interfaces) {
                            if (!interfaces.empty()) interfaces += ',';
                            interfaces += interface_type;
                        }
                    }
                    SetError(call_error,
                             "guest JNI Handler.post runnable is invalid or queue is full type=" +
                                 std::string(runnable_type.value_or(
                                     "<unknown>")) +
                                 " identity=" +
                                 std::to_string(
                                     runnable_identity.value_or(0)) +
                                 " proxy=" +
                                 (proxy != interface_proxies_.end()
                                      ? "yes"
                                      : "no") +
                                 " interfaces=" + interfaces +
                                 " queued=" +
                                 std::to_string(
                                     pending_ui_tasks_.size()));
                    return false;
                }
                pending_ui_tasks_.push_back(PendingUiTask{
                    proxy->second.native_handle,
                    UiTaskReadyAt(delay_milliseconds)});
                // Handler posting returns true when the Runnable is accepted
                // into the target queue, not when it has already executed.
                (*registers)[0] = 1;
                return true;
            }
            if (backend_ == nullptr &&
                method->second.class_type == "android/os/Bundle" &&
                method->second.name == "containsKey" &&
                method->second.signature == "(Ljava/lang/String;)Z") {
                std::uint64_t key_reference = 0;
                if (!ReadGuestGeneralArgument(address_space_,
                                              (*registers)[3],
                                              &key_reference, call_error))
                    return false;
                const std::optional<std::string_view> key_type =
                    ObjectTypeLocked(key_reference);
                if (!key_type || !IsStringTypeName(*key_type)) {
                    SetError(call_error,
                             "guest JNI CallBooleanMethodV Bundle.containsKey key is invalid");
                    return false;
                }
                // The native Windows launcher has no Android Intent extras.
                // Represent its compatibility Bundle as empty.
                (*registers)[0] = 0;
                return true;
            }
            if (backend_ == nullptr &&
                method->second.class_type ==
                    "com/king/core/HapticManager" &&
                method->second.name == "hasVibrator" &&
                method->second.signature == "()Z") {
                // Win32 haptics are not connected yet, so capability is
                // reported accurately as absent.
                (*registers)[0] = 0;
                return true;
            }
            if (backend_ == nullptr &&
                method->second.class_type ==
                    "com/king/usdk/popupguard/internal/PopupDialog" &&
                method->second.name == "isWebviewEnabled" &&
                method->second.signature == "()Z") {
                // The Java wrapper returns false when its Activity is absent,
                // the current Android WebView package is absent, or the
                // platform query throws. Native Windows supplies no Android
                // WebView package, so expose that exact fallback result.
                (*registers)[0] = 0;
                return true;
            }
            if (backend_ == nullptr &&
                method->second.class_type ==
                    "com/king/abm/internal/AdsPlatformAbstraction" &&
                method->second.name == "isClassAvailable" &&
                method->second.signature ==
                    "(Ljava/lang/String;)Z") {
                std::uint64_t class_name_reference = 0;
                if (!ReadGuestGeneralArgument(
                        address_space_, (*registers)[3],
                        &class_name_reference, call_error))
                    return false;
                const std::optional<std::string_view> argument_type =
                    ObjectTypeLocked(class_name_reference);
                if (!argument_type ||
                    !IsStringTypeName(*argument_type)) {
                    SetError(call_error,
                             "guest JNI CallBooleanMethodV AdsPlatformAbstraction.isClassAvailable argument is invalid");
                    return false;
                }
                // No Android advertising SDK classes are loaded by the
                // native Windows profile. Report absence without probing a
                // host JVM or fabricating availability.
                (*registers)[0] = 0;
                return true;
            }
            if (backend_ == nullptr &&
                method->second.class_type ==
                    "com/king/abm/internal/AdsPlatformAbstraction" &&
                method->second.name ==
                    "isInternetPermissionGranted" &&
                method->second.signature == "()Z") {
                // The native Windows profile currently exposes no external
                // network transport, so it must not claim Android INTERNET
                // permission or online capability.
                (*registers)[0] = 0;
                return true;
            }
            if (backend_ == nullptr &&
                method->second.class_type ==
                    "com/king/audio/AudioSessionHandler" &&
                method->second.name == "IsUsingDeviceSpeaker" &&
                method->second.signature == "()Z") {
                // A Windows render endpoint is not an Android handset
                // speaker session. Report the mobile-specific capability as
                // absent until native endpoint routing is implemented.
                (*registers)[0] = 0;
                return true;
            }
            const bool advertising_info =
                method->second.class_type ==
                    "com/king/usdk/adinfo/AdvertisingId$AdvertisingInfo" ||
                method->second.class_type ==
                    "Lcom/king/usdk/adinfo/AdvertisingId$AdvertisingInfo;";
            if (backend_ == nullptr && advertising_info &&
                (method->second.name == "isValid" ||
                 method->second.name == "isLimitAdTrackingEnabled") &&
                method->second.signature == "()Z") {
                // The native Windows profile exposes no advertising identity,
                // so there is neither a valid ID nor a tracking state to
                // report.
                (*registers)[0] = 0;
                return true;
            }
            if (backend_ == nullptr &&
                method->second.class_type ==
                    "com/king/usdk/notificationtokenprovider/NotificationTokenProviderModule" &&
                (method->second.name == "isPushNotificationSupported" ||
                 method->second.name == "isPushNotificationsEnabled") &&
                method->second.signature == "()Z") {
                // isPushNotificationSupported returns true only when Google
                // Play Services is available. Native Windows has no Google
                // Play Services, token provider, or push delivery transport,
                // so both support and enabled state are accurately absent.
                (*registers)[0] = 0;
                return true;
            }
            if (backend_ == nullptr &&
                method->second.signature == "()Z" &&
                (method->second.name.rfind("Is", 0) == 0 ||
                 method->second.name.rfind("Has", 0) == 0) &&
                NormalizeCompatibilityType(method->second.class_type)
                        .starts_with("com/ea/blast/") &&
                NormalizeCompatibilityType(method->second.class_type)
                        .ends_with("AndroidDelegate")) {
                // Hardware/state queries on blast delegates (physical
                // keyboard visible, sensors present, ...) are accurately
                // false in the compat runtime.
                (*registers)[0] = 0;
                return true;
            }
            if (backend_ == nullptr &&
                NormalizeCompatibilityType(method->second.class_type) ==
                    "csdk/glucentralservices/util/AndroidPlatform" &&
                method->second.name == "checkPrivateDirectoryExists" &&
                method->second.signature == "(Ljava/lang/String;Z)Z") {
                // The private-directory probe is answered as existing so
                // the engine uses its configured storage paths.
                (*registers)[0] = 1;
                return true;
            }
            if (ApplyCompatOverride("boolean", method->second.class_type,
                                    method->second.name,
                                    method->second.signature, registers,
                                    call_error))
                return true;
            // UMP ConsentInformation.canRequestAds: consent is reported
            // OBTAINED, so ads may be requested. isConsentFormAvailable
            // reports false — no form to show.
            if (backend_ == nullptr &&
                (method->second.name == "canRequestAds" ||
                 method->second.name == "isConsentFormAvailable") &&
                method->second.signature == "()Z") {
                const auto local_consent =
                    object_references_.find((*registers)[1]);
                const auto global_consent =
                    global_object_references_.find((*registers)[1]);
                const bool is_consent =
                    (local_consent != object_references_.end() &&
                     NormalizeCompatibilityType(
                         local_consent->second.type) ==
                         "com/google/android/ump/ConsentInformation") ||
                    (global_consent != global_object_references_.end() &&
                     NormalizeCompatibilityType(
                         global_consent->second.type) ==
                         "com/google/android/ump/ConsentInformation");
                if (is_consent) {
                    (*registers)[0] =
                        method->second.name == "canRequestAds" ? 1 : 0;
                    return true;
                }
            }
            // AdvertisingIdClient.Info.isLimitAdTrackingEnabled: ad tracking
            // is not limited.
            if (backend_ == nullptr &&
                method->second.name == "isLimitAdTrackingEnabled" &&
                method->second.signature == "()Z") {
                (*registers)[0] = 0;
                return true;
            }
            // SharedPreferences.contains(key): consult the committed int and
            // string stores so write-then-check sequences observe their own
            // writes.
            if (backend_ == nullptr &&
                NormalizeCompatibilityType(method->second.class_type) ==
                    "android/content/SharedPreferences" &&
                method->second.name == "contains" &&
                method->second.signature == "(Ljava/lang/String;)Z") {
                const std::string* preference_name = nullptr;
                const auto local_preferences =
                    object_references_.find((*registers)[1]);
                const auto global_preferences =
                    global_object_references_.find((*registers)[1]);
                if (local_preferences != object_references_.end() &&
                    NormalizeCompatibilityType(
                        local_preferences->second.type) ==
                        "android/content/SharedPreferences")
                    preference_name = &local_preferences->second.string_value;
                else if (global_preferences !=
                             global_object_references_.end() &&
                         NormalizeCompatibilityType(
                             global_preferences->second.type) ==
                             "android/content/SharedPreferences")
                    preference_name =
                        &global_preferences->second.string_value;
                std::uint64_t key_reference = 0;
                if (preference_name == nullptr ||
                    !ReadGuestGeneralArgument(address_space_,
                                              (*registers)[3],
                                              &key_reference, call_error, 0))
                    return false;
                const std::string* key = nullptr;
                const auto local_key =
                    FindOwnedLocalObjectReferenceLocked(key_reference);
                if (local_key != object_references_.end() &&
                    IsStringTypeName(local_key->second.type))
                    key = &local_key->second.string_value;
                else {
                    const auto global_key =
                        global_object_references_.find(key_reference);
                    if (global_key != global_object_references_.end() &&
                        IsStringTypeName(global_key->second.type))
                        key = &global_key->second.string_value;
                }
                if (key == nullptr) {
                    SetError(call_error,
                             "guest JNI SharedPreferences.contains key is invalid");
                    return false;
                }
                bool found = false;
                const auto int_prefs =
                    shared_preferences_ints_.find(*preference_name);
                if (int_prefs != shared_preferences_ints_.end())
                    found = int_prefs->second.count(*key) != 0;
                if (!found) {
                    const auto string_prefs =
                        shared_preferences_strings_.find(*preference_name);
                    if (string_prefs != shared_preferences_strings_.end())
                        found = string_prefs->second.count(*key) != 0;
                }
                (*registers)[0] = found ? 1 : 0;
                return true;
            }
            // Intent.getBooleanExtra(name, default) on an Intent with no
            // extras map returns the caller's default, matching the
            // documented contract rather than the JNI zero default.
            if (backend_ == nullptr &&
                NormalizeCompatibilityType(method->second.class_type) ==
                    "android/content/Intent" &&
                method->second.name == "getBooleanExtra" &&
                method->second.signature == "(Ljava/lang/String;Z)Z") {
                std::uint64_t name_ref = 0;
                std::uint64_t default_value = 0;
                if (!ReadGuestGeneralArgument(address_space_,
                                              (*registers)[3], &name_ref,
                                              call_error, 0) ||
                    !ReadGuestGeneralArgument(address_space_,
                                              (*registers)[3],
                                              &default_value, call_error, 1))
                    return false;
                (*registers)[0] = default_value != 0 ? 1 : 0;
                return true;
            }
            if (backend_ == nullptr) {
                // No Java backend runs the target; an unimplemented boolean
                // method returns the JNI zero default (false) rather than
                // aborting the caller on an unimplemented signature.
                if (call_error != nullptr)
                    *call_error =
                        "trace:jni-call-boolean-stub-" +
                        method->second.class_type + "." +
                        method->second.name + method->second.signature;
                (*registers)[0] = 0;
                return true;
            }
            SetError(call_error,
                     "guest JNI CallBooleanMethodV class " +
                         method->second.class_type + " method " +
                         method->second.name + " signature " +
                         method->second.signature +
                         " is not yet supported");
            return false;
        };
    const auto call_long_method_v =
        [this](Aarch64ImportRegistry::Registers* registers,
               std::string* call_error) {
            if (registers == nullptr ||
                !IsCurrentEnvironment((*registers)[0]) ||
                (*registers)[3] == 0) {
                SetError(call_error,
                         "guest JNI CallLongMethodV arguments are invalid");
                return false;
            }
            const std::lock_guard lock(mutex_);
            const auto motion_long =
                motion_events_.find(ReferenceIdentityLocked((*registers)[1])
                                        .value_or((*registers)[1]));
            if (motion_long != motion_events_.end()) {
                const auto motion_method =
                    method_references_.find((*registers)[2]);
                const std::string_view motion_name =
                    motion_method != method_references_.end()
                        ? std::string_view(motion_method->second.name)
                        : std::string_view();
                std::int64_t motion_result = 0;
                if (motion_name == "getEventTime" ||
                    motion_name == "getDownTime") {
                    motion_result = motion_long->second.event_time_ms;
                }
                (*registers)[0] = static_cast<std::uint64_t>(motion_result);
                return true;
            }
            const auto method = method_references_.find((*registers)[2]);
            static std::size_t long_call_logs = 0;
            if (long_call_logs < 12 && method != method_references_.end()) {
                ++long_call_logs;
                std::fprintf(stderr, "ISOMR_JNI_LONG_CALL cls=%s name=%s%s\n",
                             method->second.class_type.c_str(),
                             method->second.name.c_str(),
                             method->second.signature.c_str());
                std::fflush(stderr);
            }
            if (method != method_references_.end()) {
                const std::string normalized_class =
                    NormalizeCompatibilityType(method->second.class_type);
                // Storage-space queries: java/io/File free/usable/total and
                // android/os/StatFs(V) byte/block accessors. Unity's device
                // init aborts with "Not enough storage space" when these
                // report the small asset-size default, so report a generous
                // free volume.
                if (normalized_class == "java/io/File" ||
                    normalized_class == "android/os/StatFs" ||
                    normalized_class == "android/os/StatFsV") {
                    if (method->second.name == "getBlockSize" ||
                        method->second.name == "getBlockSizeLong") {
                        (*registers)[0] = 4096ULL;
                        return true;
                    }
                    (*registers)[0] = 16ULL * 1024 * 1024 * 1024;
                    return true;
                }
            }
            // Intent.getLongExtra(name, default) on an Intent with no
            // extras map returns the caller's default.
            if (backend_ == nullptr && method != method_references_.end() &&
                NormalizeCompatibilityType(method->second.class_type) ==
                    "android/content/Intent" &&
                method->second.name == "getLongExtra" &&
                method->second.signature == "(Ljava/lang/String;J)J") {
                std::uint64_t name_ref = 0;
                std::uint64_t default_value = 0;
                if (!ReadGuestGeneralArgument(address_space_,
                                              (*registers)[3], &name_ref,
                                              call_error, 0) ||
                    !ReadGuestGeneralArgument(address_space_,
                                              (*registers)[3],
                                              &default_value, call_error, 1))
                    return false;
                (*registers)[0] = default_value;
                return true;
            }
            // Resources_GetAssetFileSize(String)J: report the size of the
            // RSB asset pack (pvz1-data/main.rsb.raw) so the boot load
            // pipeline sees a real file rather than zero.
            (*registers)[0] = 34713600ULL;
            return true;
        };
    const auto call_int_method_v =
        [this](Aarch64ImportRegistry::Registers* registers,
               std::string* call_error) {
            if (registers == nullptr ||
                !IsCurrentEnvironment((*registers)[0]) ||
                (*registers)[3] == 0) {
                SetError(call_error,
                         "guest JNI CallIntMethodV arguments are invalid");
                return false;
            }
            Aarch64VaList arguments{};
            if (!ReadGuestVaList(address_space_, (*registers)[3],
                                 &arguments, call_error))
                return false;
            const std::lock_guard lock(mutex_);
            if (!IsObjectReferenceLocked((*registers)[1])) {
                SetError(call_error,
                         "guest JNI CallIntMethodV object is invalid");
                return false;
            }
            const auto motion_int =
                motion_events_.find(ReferenceIdentityLocked((*registers)[1])
                                        .value_or((*registers)[1]));
            if (motion_int != motion_events_.end()) {
                const auto motion_method =
                    method_references_.find((*registers)[2]);
                const std::string_view motion_name =
                    motion_method != method_references_.end()
                        ? std::string_view(motion_method->second.name)
                        : std::string_view();
                std::int64_t motion_result = 0;
                if (motion_name == "getAction" ||
                    motion_name == "getActionMasked") {
                    motion_result = motion_int->second.action;
                } else if (motion_name == "getDeviceId") {
                    // Route the injected touch to the registered touchscreen
                    // device so Unity's input pipeline does not drop it as
                    // coming from an unknown/unenumerated device.
                    motion_result =
                        static_cast<std::int64_t>(kArmrTouchDeviceId);
                } else if (motion_name == "getPointerCount") {
                    motion_result = 1;
                } else if (motion_name == "getSource") {
                    motion_result = 0x1002;
                } else if (motion_name == "getToolType") {
                    motion_result = 1;
                }
                (*registers)[0] = static_cast<std::uint64_t>(motion_result);
                return true;
            }
            {
                const std::uint64_t receiver =
                    ReferenceIdentityLocked((*registers)[1])
                        .value_or((*registers)[1]);
                const auto input_dev = input_devices_.find(receiver);
                const auto input_range = input_ranges_.find(receiver);
                if (input_dev != input_devices_.end() ||
                    input_range != input_ranges_.end()) {
                    const auto dev_method =
                        method_references_.find((*registers)[2]);
                    const std::string_view dev_name =
                        dev_method != method_references_.end()
                            ? std::string_view(dev_method->second.name)
                            : std::string_view();
                    std::int64_t result = 0;
                    if (input_dev != input_devices_.end()) {
                        if (dev_name == "getSources") {
                            // SOURCE_TOUCHSCREEN so Unity classifies the
                            // device as a touch input source.
                            result = 0x1002;
                        } else if (dev_name == "getId") {
                            result = static_cast<std::int64_t>(
                                input_dev->second);
                        } else if (dev_name == "getVendorId" ||
                                   dev_name == "getProductId" ||
                                   dev_name == "getControllerNumber" ||
                                   dev_name == "getKeyboardType" ||
                                   dev_name == "getPointerCount") {
                            result = 0;
                        }
                    } else {
                        // InputDevice$MotionRange.getAxis reports the axis the
                        // range describes.
                        if (dev_name == "getAxis")
                            result = input_range->second.axis;
                    }
                    (*registers)[0] = static_cast<std::uint64_t>(result);
                    return true;
                }
            }
            const auto method = method_references_.find((*registers)[2]);
            if (method == method_references_.end() ||
                method->second.is_static) {
                if (backend_ == nullptr) {
                    // No Java backend: an int call on an unknown or static
                    // method id returns the JNI zero default instead of
                    // aborting the frame.
                    std::fprintf(stderr,
                                 "ISOMR_JNI_INT_STUB mid=%llx\n",
                                 (unsigned long long)(*registers)[2]);
                    std::fflush(stderr);
                    (*registers)[0] = 0;
                    return true;
                }
                SetError(call_error,
                         "guest JNI CallIntMethodV method is invalid");
                return false;
            }
            const std::optional<std::string_view> receiver_type =
                ObjectTypeLocked((*registers)[1]);
            // Nimble declares interfaces under dotted names while receivers
            // carry slashed JNI types (ILog etc.); compare with the same
            // slash/dot/L; tolerance the void path uses. Subclass receivers
            // (Activity -> Context, etc.) are accepted via the compatibility
            // assignability walk.
            const bool receiver_matches =
                receiver_type &&
                (ReceiverClassMatches(*receiver_type,
                                      method->second.class_type) ||
                 IsCompatibilityAssignable(*receiver_type,
                                           method->second.class_type));
            // java.lang.Enum methods are inherited by every enum
            // constant; the declaring class never equals the concrete
            // receiver type. Answer ordinal() synthetically with the
            // first constant (PvZ1 reads Network$Status.ordinal to pick
            // its connectivity behavior; the first constant drives the
            // offline path).
            if (!receiver_matches &&
                NormalizeCompatibilityType(method->second.class_type) ==
                    "java.lang.Enum" &&
                method->second.name == "ordinal" &&
                method->second.signature == "()I") {
                (*registers)[0] = 0;
                return true;
            }
            if (!receiver_matches) {
                std::fprintf(stderr,
                             "ISOMR_JNI_INT_RECEIVER_MISMATCH cls=%s name=%s sig=%s recv=%s\n",
                             method->second.class_type.c_str(),
                             method->second.name.c_str(),
                             method->second.signature.c_str(),
                             receiver_type ? std::string(*receiver_type).c_str()
                                           : "<none>");
                std::fflush(stderr);
                SetError(call_error,
                         "guest JNI CallIntMethodV receiver does not match declaring class " +
                             method->second.class_type);
                return false;
            }
            if (backend_ == nullptr &&
                ReceiverClassMatches("com/ea/nimble/ILog",
                                     method->second.class_type) &&
                method->second.name == "getThresholdLevel" &&
                method->second.signature == "()I") {
                // Nimble log threshold: report the highest severity so the
                // compat session stays quiet; the engine only gates its own
                // logging volume with this value.
                (*registers)[0] = 5;
                return true;
            }
            if (backend_ == nullptr &&
                NormalizeCompatibilityType(method->second.class_type) ==
                    "android/media/AudioDeviceInfo") {
                // Companion of the AudioManager.getDevices serve: the
                // descriptor must look like a real built-in speaker sink,
                // including a usable sampling rate (a zero default would
                // make the audio engine reject the device and retry the
                // inventory — the flask retry loop this whole path exits).
                const std::string_view device_method = method->second.name;
                if (device_method == "getType" &&
                    method->second.signature == "()I") {
                    (*registers)[0] = 2;  // TYPE_BUILTIN_SPEAKER
                    return true;
                }
                if (device_method == "getMaxSamplingRate" &&
                    method->second.signature == "()I") {
                    (*registers)[0] = 48000;
                    return true;
                }
                if (device_method == "getMinChannelCount" &&
                    method->second.signature == "()I") {
                    (*registers)[0] = 1;
                    return true;
                }
                if (device_method == "getMaxChannelCount" &&
                    method->second.signature == "()I") {
                    (*registers)[0] = 2;
                    return true;
                }
            }
            if (backend_ == nullptr &&
                NormalizeCompatibilityType(method->second.class_type) ==
                    "java/lang/String" &&
                method->second.name == "length" &&
                method->second.signature == "()I") {
                const auto local_string =
                    object_references_.find((*registers)[1]);
                const auto global_string =
                    global_object_references_.find((*registers)[1]);
                const std::string* value =
                    local_string != object_references_.end() &&
                            IsStringTypeName(local_string->second.type)
                        ? &local_string->second.string_value
                        : global_string !=
                                      global_object_references_.end() &&
                                  IsStringTypeName(global_string->second.type)
                              ? &global_string->second.string_value
                              : nullptr;
                std::vector<std::uint16_t> characters;
                if (value == nullptr ||
                    !DecodeModifiedUtf8(*value, &characters,
                                        call_error)) {
                    if (call_error == nullptr || call_error->empty())
                        SetError(call_error,
                                 "guest JNI String.length receiver is invalid");
                    return false;
                }
                (*registers)[0] = characters.size();
                return true;
            }
            if (backend_ == nullptr &&
                NormalizeCompatibilityType(method->second.class_type) ==
                    "android/content/SharedPreferences" &&
                method->second.name == "getInt" &&
                method->second.signature ==
                    "(Ljava/lang/String;I)I") {
                std::uint64_t key_reference = 0;
                std::uint64_t default_value = 0;
                if (!ReadGuestGeneralArgument(address_space_,
                                              (*registers)[3],
                                              &key_reference,
                                              call_error) ||
                    !ReadGuestGeneralArgument(address_space_,
                                              (*registers)[3],
                                              &default_value, call_error,
                                              1))
                    return false;
                const std::optional<std::string_view> key_type =
                    ObjectTypeLocked(key_reference);
                if (!key_type ||
                    NormalizeCompatibilityType(*key_type) !=
                        "java/lang/String") {
                    Aarch64VaList dbg{};
                    std::string dbg_err;
                    const bool have_va = ReadGuestVaList(
                        address_space_, (*registers)[3], &dbg, &dbg_err);
                    std::uint64_t rawva = 0;
                    (void)address_space_->Read((*registers)[3], &rawva, 8);
                    std::fprintf(
                        stderr,
                        "ISOMR_GETINT_KEY key_ref=0x%llx type=%s "
                        "in_obj=%d in_global=%d owned=%d va_ok=%d "
                        "reg3=0x%llx va_stack=0x%llx va_gtop=0x%llx "
                        "va_goff=%d va_voff=%d lr=0x%llx\n",
                        static_cast<unsigned long long>(key_reference),
                        key_type ? std::string(*key_type).c_str() : "null",
                        object_references_.contains(key_reference) ? 1 : 0,
                        global_object_references_.contains(key_reference)
                            ? 1
                            : 0,
                        CurrentThreadOwnsLocalReferenceLocked(key_reference)
                            ? 1
                            : 0,
                        have_va ? 1 : 0,
                        static_cast<unsigned long long>((*registers)[3]),
                        static_cast<unsigned long long>(dbg.stack),
                        static_cast<unsigned long long>(dbg.general_top),
                        dbg.general_offset, dbg.vector_offset,
                        static_cast<unsigned long long>(
                            registers->callee_saved_general[11]));
                    if (have_va) {
                        for (int slot = 0; slot < 8; ++slot) {
                            std::uint64_t slot_value = 0;
                            const std::uint64_t slot_address =
                                dbg.general_top - 64U +
                                static_cast<std::uint64_t>(slot) * 8U;
                            (void)address_space_->Read(slot_address,
                                                       &slot_value, 8);
                            std::fprintf(stderr,
                                         "ISOMR_GETINT_GR gr[%d] "
                                         "@0x%llx=0x%llx\n",
                                         slot,
                                         static_cast<unsigned long long>(
                                             slot_address),
                                         static_cast<unsigned long long>(
                                             slot_value));
                        }
                        for (int slot = 0; slot < 8; ++slot) {
                            std::uint64_t slot_value = 0;
                            const std::uint64_t slot_address =
                                dbg.stack +
                                static_cast<std::uint64_t>(slot) * 8U;
                            (void)address_space_->Read(slot_address,
                                                       &slot_value, 8);
                            std::fprintf(stderr,
                                         "ISOMR_GETINT_ST st[%d] "
                                         "@0x%llx=0x%llx\n",
                                         slot,
                                         static_cast<unsigned long long>(
                                             slot_address),
                                         static_cast<unsigned long long>(
                                             slot_value));
                        }
                        std::uint64_t caller_lr = 0;
                        (void)address_space_->Read(dbg.general_top + 0x70U,
                                                   &caller_lr, 8);
                        std::fprintf(stderr,
                                     "ISOMR_GETINT_CALLER lr=0x%llx\n",
                                     static_cast<unsigned long long>(
                                         caller_lr));
                        // The dispatcher marshals key = **(caller_sp): the
                        // args array lives at the caller's stack pointer,
                        // which equals va_list.__stack. Follow the chain.
                        std::uint64_t args0 = 0;
                        std::uint64_t key_slot = 0;
                        (void)address_space_->Read(dbg.stack, &args0, 8);
                        (void)address_space_->Read(args0, &key_slot, 8);
                        std::fprintf(
                            stderr,
                            "ISOMR_GETINT_CHAIN args0=0x%llx key=0x%llx "
                            "key_type=%s\n",
                            static_cast<unsigned long long>(args0),
                            static_cast<unsigned long long>(key_slot),
                            ObjectTypeLocked(key_slot)
                                ? std::string(*ObjectTypeLocked(key_slot))
                                      .c_str()
                                : "none");
                    }
                    std::fflush(stderr);
                    SetError(call_error,
                             "guest JNI SharedPreferences.getInt key is invalid");
                    return false;
                }
                const auto local_preferences =
                    object_references_.find((*registers)[1]);
                const auto global_preferences =
                    global_object_references_.find((*registers)[1]);
                const std::string* preference_name =
                    local_preferences != object_references_.end()
                        ? &local_preferences->second.string_value
                        : global_preferences !=
                                  global_object_references_.end()
                              ? &global_preferences->second.string_value
                              : nullptr;
                const auto local_key =
                    FindOwnedLocalObjectReferenceLocked(key_reference);
                const auto global_key =
                    global_object_references_.find(key_reference);
                const std::string* key =
                    local_key != object_references_.end()
                        ? &local_key->second.string_value
                        : global_key != global_object_references_.end()
                              ? &global_key->second.string_value
                              : nullptr;
                if (preference_name == nullptr ||
                    preference_name->empty() || key == nullptr) {
                    std::fprintf(
                        stderr,
                        "ISOMR_GETINT_STATE pref_obj=0x%llx pref_name=%s "
                        "key_ref=0x%llx key=%s\n",
                        static_cast<unsigned long long>((*registers)[1]),
                        preference_name ? preference_name->c_str() : "<null>",
                        static_cast<unsigned long long>(key_reference),
                        key ? key->c_str() : "<null>");
                    std::fflush(stderr);
                    SetError(call_error,
                             "guest JNI SharedPreferences.getInt state is invalid");
                    return false;
                }
                // An empty key is a legal SharedPreferences lookup; it never
                // matches a stored entry, so the call returns the default.
                if (!key->empty()) {
                    const auto preferences =
                        shared_preferences_ints_.find(*preference_name);
                    if (preferences != shared_preferences_ints_.end()) {
                        const auto stored = preferences->second.find(*key);
                        if (stored != preferences->second.end()) {
                            (*registers)[0] =
                                static_cast<std::uint32_t>(stored->second);
                            return true;
                        }
                    }
                }
                (*registers)[0] = static_cast<std::uint32_t>(default_value);
                return true;
            }
            if (backend_ == nullptr &&
                NormalizeCompatibilityType(method->second.class_type) ==
                    "android/view/View" &&
                method->second.name == "getSystemUiVisibility" &&
                method->second.signature == "()I") {
                // System-UI visibility setters are accepted no-ops, so the
                // tracked flags read back as zero (plain visible state).
                (*registers)[0] = 0;
                return true;
            }
            if (backend_ == nullptr &&
                NormalizeCompatibilityType(method->second.class_type) ==
                    "com/ea/blast/MainActivity" &&
                (method->second.name == "getDisplayWidth" ||
                 method->second.name == "getDisplayHeight") &&
                method->second.signature == "()I") {
                // The blast renderer sizes its surface from the activity's
                // display metrics. There is no Android display service in
                // the compat runtime; report the probe's declared surface.
                (*registers)[0] =
                    method->second.name == "getDisplayWidth" ? 1280U : 720U;
                return true;
            }
            if (backend_ == nullptr &&
                NormalizeCompatibilityType(method->second.class_type) ==
                    "csdk/glucentralservices/util/AndroidPlatform" &&
                method->second.name == "getCpuCoreCount" &&
                method->second.signature == "()I") {
                // Report the host's hardware thread count, capped for
                // mobile-oriented pacing assumptions.
                const std::uint64_t cores = std::thread::hardware_concurrency();
                (*registers)[0] = cores == 0 ? 4U : (cores > 8U ? 8U : cores);
                return true;
            }
            if (backend_ == nullptr &&
                NormalizeCompatibilityType(method->second.class_type) ==
                    "csdk/glucentralservices/util/AndroidPlatform" &&
                method->second.name == "getDeviceTier" &&
                method->second.signature == "()I") {
                // A desktop GPU outranks every mobile tier; report the
                // highest device tier so the engine picks full quality.
                (*registers)[0] = 3;
                return true;
            }
            if (backend_ == nullptr &&
                NormalizeCompatibilityType(method->second.class_type) ==
                    "com/ea/blast/DisplayAndroidDelegate" &&
                method->second.signature == "()I" &&
                (method->second.name == "GetStdOrientation" ||
                 method->second.name == "GetCurrentWidth" ||
                 method->second.name == "GetCurrentHeight")) {
                // The probe declares a 1280x720 landscape surface, matching
                // Android's ORIENTATION_LANDSCAPE.
                if (method->second.name == "GetStdOrientation") {
                    (*registers)[0] = 2;
                } else if (method->second.name == "GetCurrentWidth") {
                    (*registers)[0] = 1280;
                } else {
                    (*registers)[0] = 720;
                }
                return true;
            }
            if (backend_ == nullptr &&
                NormalizeCompatibilityType(method->second.class_type) ==
                    "com/ea/blast/SystemAndroidDelegate" &&
                method->second.name == "GetApplicationVersionCode" &&
                method->second.signature == "()I") {
                // Derive a store-style version code (major*10000 +
                // minor*100 + patch) from the configured package version.
                unsigned components[3] = {0, 0, 0};
                int index = 0;
                for (const char c : compatibility_.package_version) {
                    if (c == '.') {
                        if (++index > 2) break;
                        continue;
                    }
                    if (c < '0' || c > '9') break;
                    components[index] =
                        components[index] * 10U +
                        static_cast<unsigned>(c - '0');
                }
                (*registers)[0] =
                    components[0] * 10000U + components[1] * 100U +
                    components[2];
                return true;
            }
            if (backend_ == nullptr &&
                NormalizeCompatibilityType(method->second.class_type) ==
                    "com/ea/blast/SystemAndroidDelegate" &&
                method->second.name.rfind("Get", 0) == 0 &&
                method->second.name.find("Count") !=
                    std::string::npos &&
                method->second.signature == "()I") {
                // Hardware/sensor count queries (accelerometer, camera, ...)
                // report zero devices by default, but a compat override may
                // advertise a device the runtime synthesizes — e.g. the
                // touchscreen that carries pointer input into the guest's
                // TouchScreen module (a count of zero there prevents the
                // module from being created at all, dropping all input).
                if (!ApplyCompatOverride("int", method->second.class_type,
                                         method->second.name,
                                         method->second.signature, registers,
                                         call_error))
                    (*registers)[0] = 0;
                return true;
            }
            if (ApplyCompatOverride("int", method->second.class_type,
                                    method->second.name,
                                    method->second.signature, registers,
                                    call_error))
                return true;
            // UMP ConsentInformation status getters on the compat object:
            // consent OBTAINED(3) / privacy-options NOT_REQUIRED(0) so the
            // game's consent coroutine completes instead of hanging.
            if (backend_ == nullptr &&
                (method->second.name == "getConsentStatus" ||
                 method->second.name ==
                     "getPrivacyOptionsRequirementStatus") &&
                method->second.signature == "()I") {
                const auto local_consent =
                    object_references_.find((*registers)[1]);
                const auto global_consent =
                    global_object_references_.find((*registers)[1]);
                const bool is_consent =
                    (local_consent != object_references_.end() &&
                     NormalizeCompatibilityType(
                         local_consent->second.type) ==
                         "com/google/android/ump/ConsentInformation") ||
                    (global_consent != global_object_references_.end() &&
                     NormalizeCompatibilityType(
                         global_consent->second.type) ==
                         "com/google/android/ump/ConsentInformation");
                if (is_consent) {
                    (*registers)[0] =
                        method->second.name == "getConsentStatus" ? 3 : 0;
                    return true;
                }
            }
            // Intent.getIntExtra(name, default) on an Intent with no extras
            // map returns the caller's default, not zero. CTR registers a
            // sticky-broadcast receiver and reads extras off the result;
            // returning 0 instead of the documented default breaks its
            // bootstrap branch.
            if (backend_ == nullptr &&
                NormalizeCompatibilityType(method->second.class_type) ==
                    "android/content/Intent" &&
                method->second.name == "getIntExtra" &&
                method->second.signature == "(Ljava/lang/String;I)I") {
                std::uint64_t name_ref = 0;
                std::uint64_t default_value = 0;
                if (!ReadGuestGeneralArgument(address_space_,
                                              (*registers)[3], &name_ref,
                                              call_error, 0) ||
                    !ReadGuestGeneralArgument(address_space_,
                                              (*registers)[3],
                                              &default_value, call_error, 1))
                    return false;
                const std::string* extra_name = nullptr;
                const auto local_name =
                    FindOwnedLocalObjectReferenceLocked(name_ref);
                if (local_name != object_references_.end() &&
                    IsStringTypeName(local_name->second.type))
                    extra_name = &local_name->second.string_value;
                if (call_error != nullptr)
                    *call_error =
                        "trace:jni-call-int-intent-extra-" +
                        (extra_name ? *extra_name : std::string("?")) +
                        "-def" + std::to_string(default_value);
                (*registers)[0] = static_cast<std::uint32_t>(default_value);
                return true;
            }
            if (backend_ == nullptr &&
                NormalizeCompatibilityType(method->second.class_type) !=
                    "java/io/InputStream") {
                // No Java backend runs the target; an unimplemented int
                // method (other than the InputStream reads handled below)
                // returns the JNI zero default rather than aborting.
                if (call_error != nullptr)
                    *call_error =
                        "trace:jni-call-int-stub-" +
                        method->second.class_type + "." +
                        method->second.name + method->second.signature;
                (*registers)[0] = 0;
                return true;
            }
            if (backend_ != nullptr) {
                SetError(call_error,
                         "guest JNI CallIntMethodV method " +
                             method->second.class_type + "." +
                             method->second.name +
                             method->second.signature +
                             " is not yet supported");
                return false;
            }
            const std::optional<std::uint64_t> stream_identity =
                ReferenceIdentityLocked((*registers)[1]);
            const auto stream = stream_identity
                                    ? input_streams_.find(*stream_identity)
                                    : input_streams_.end();
            if (!stream_identity || stream == input_streams_.end() ||
                stream->second.descriptor < 0) {
                SetError(call_error,
                         "guest JNI InputStream is closed or invalid");
                return false;
            }
            if (method->second.name == "available" &&
                method->second.signature == "()I") {
                const std::int64_t current = files_->Lseek64(
                    stream->second.descriptor, 0, 1);
                const std::int64_t end = files_->Lseek64(
                    stream->second.descriptor, 0, 2);
                if (current < 0 || end < current ||
                    files_->Lseek64(stream->second.descriptor, current, 0) !=
                        current) {
                    SetError(call_error,
                             "guest JNI InputStream.available seek failed");
                    return false;
                }
                const std::int64_t remaining = end - current;
                (*registers)[0] = static_cast<std::uint32_t>(
                    std::min<std::int64_t>(
                        remaining,
                        std::numeric_limits<std::int32_t>::max()));
                return true;
            }
            if (method->second.name == "read" &&
                method->second.signature == "()I") {
                const std::uint64_t buffer = heap_->Allocate(1);
                if (buffer == 0) {
                    SetError(call_error,
                             "guest JNI InputStream.read staging allocation failed");
                    return false;
                }
                const std::int64_t count = files_->Read(
                    stream->second.descriptor, buffer, 1);
                std::uint8_t value = 0;
                const bool copied = count == 1 &&
                                    heap_->Read(buffer, &value, 1);
                std::string ignored;
                const bool released = heap_->Release(buffer, &ignored);
                if (!released || count < 0 || (count == 1 && !copied)) {
                    SetError(call_error,
                             "guest JNI InputStream.read failed");
                    return false;
                }
                (*registers)[0] = count == 0
                                      ? JniResult(-1)
                                      : static_cast<std::uint32_t>(value);
                return true;
            }
            const bool read_whole_array =
                method->second.name == "read" &&
                method->second.signature == "([B)I";
            const bool read_array_region =
                method->second.name == "read" &&
                method->second.signature == "([BII)I";
            if (!read_whole_array && !read_array_region) {
                // This branch only runs with no Java backend; an
                // unimplemented InputStream int method returns the JNI zero
                // default rather than aborting the caller.
                if (call_error != nullptr)
                    *call_error =
                        "trace:jni-call-int-stub-" +
                        method->second.class_type + "." +
                        method->second.name + method->second.signature;
                (*registers)[0] = 0;
                return true;
            }
            std::uint64_t array_reference = 0;
            if (!ReadGuestGeneralArgument(address_space_, (*registers)[3],
                                          &array_reference, call_error))
                return false;
            const std::optional<std::uint64_t> array_identity =
                ReferenceIdentityLocked(array_reference);
            const auto bytes = array_identity
                                   ? byte_arrays_.find(*array_identity)
                                   : byte_arrays_.end();
            if (!array_identity || bytes == byte_arrays_.end()) {
                SetError(call_error,
                         "guest JNI InputStream.read array is invalid");
                return false;
            }
            std::int32_t offset = 0;
            std::int32_t requested =
                static_cast<std::int32_t>(bytes->second.size());
            if (read_array_region) {
                std::uint64_t raw_offset = 0;
                std::uint64_t raw_requested = 0;
                if (!ReadGuestGeneralArgument(address_space_,
                                              (*registers)[3], &raw_offset,
                                              call_error, 1) ||
                    !ReadGuestGeneralArgument(address_space_,
                                              (*registers)[3],
                                              &raw_requested, call_error, 2))
                    return false;
                offset = static_cast<std::int32_t>(
                    static_cast<std::uint32_t>(raw_offset));
                requested = static_cast<std::int32_t>(
                    static_cast<std::uint32_t>(raw_requested));
            }
            if (offset < 0 || requested < 0 ||
                static_cast<std::size_t>(offset) > bytes->second.size() ||
                static_cast<std::size_t>(requested) >
                    bytes->second.size() -
                        static_cast<std::size_t>(offset)) {
                SetError(call_error,
                         "guest JNI InputStream.read range is invalid");
                return false;
            }
            if (requested == 0) {
                (*registers)[0] = 0;
                return true;
            }
            constexpr std::size_t kMaximumRead = 16U * 1024U * 1024U;
            const std::size_t transfer = std::min<std::size_t>(
                static_cast<std::size_t>(requested), kMaximumRead);
            const std::uint64_t buffer = heap_->Allocate(transfer);
            if (buffer == 0) {
                SetError(call_error,
                         "guest JNI InputStream.read staging allocation failed");
                return false;
            }
            const std::int64_t count = files_->Read(
                stream->second.descriptor, buffer, transfer);
            const bool copied =
                count > 0 &&
                heap_->Read(buffer,
                            bytes->second.data() +
                                static_cast<std::size_t>(offset),
                            static_cast<std::size_t>(count));
            std::string ignored;
            const bool released = heap_->Release(buffer, &ignored);
            if (!released || count < 0 || (count > 0 && !copied)) {
                SetError(call_error,
                         "guest JNI InputStream.read failed");
                return false;
            }
            (*registers)[0] =
                count == 0 ? JniResult(-1)
                           : static_cast<std::uint32_t>(count);
            return true;
        };
    const auto pop_local_frame =
        [this](Aarch64ImportRegistry::Registers* registers,
               std::string* call_error) {
            if (registers == nullptr ||
                !IsCurrentEnvironment((*registers)[0])) {
                SetError(call_error,
                         "guest JNI PopLocalFrame environment is invalid");
                return false;
            }
            const std::uint64_t result = (*registers)[1];
            const std::optional<std::uint64_t> thread =
                thread_state_ != nullptr
                    ? thread_state_->CurrentErrnoAddress()
                    : std::nullopt;
            if (!thread) {
                SetError(call_error,
                         "guest JNI PopLocalFrame thread is not attached");
                return false;
            }
            std::vector<std::uint64_t> release;
            {
                const std::lock_guard lock(mutex_);
                auto frames = local_reference_frames_.find(*thread);
                if (frames == local_reference_frames_.end() ||
                    frames->second.empty()) {
                    SetError(call_error,
                             "guest JNI PopLocalFrame has no active frame");
                    return false;
                }
                if (result != 0 &&
                    !ReferenceIdentityLocked(result)) {
                    SetError(call_error,
                             "guest JNI PopLocalFrame result is invalid");
                    return false;
                }
                release = std::move(
                    frames->second.back().created_references);
                frames->second.pop_back();
                const auto preserved =
                    std::find(release.begin(), release.end(), result);
                if (preserved != release.end()) {
                    release.erase(preserved);
                    if (!frames->second.empty())
                        frames->second.back().created_references.push_back(
                            result);
                }
                if (frames->second.empty())
                    local_reference_frames_.erase(frames);
            }
            for (const std::uint64_t reference : release) {
                if (!DeleteLocalReference(reference, call_error))
                    return false;
            }
            (*registers)[0] = result;
            return true;
        };
    const auto call_double_method_v =
        [this](Aarch64ImportRegistry::Registers* registers,
               std::string* call_error) {
            if (registers == nullptr ||
                !IsCurrentEnvironment((*registers)[0])) {
                SetError(call_error,
                         "guest JNI CallDoubleMethodV arguments are invalid");
                return false;
            }
            const std::lock_guard lock(mutex_);
            const auto method = method_references_.find((*registers)[2]);
            std::string described =
                method != method_references_.end()
                    ? method->second.class_type + "." +
                          method->second.name + " signature " +
                          method->second.signature
                    : "<unknown method>";
            if (backend_ == nullptr &&
                NormalizeCompatibilityType(method->second.class_type) ==
                    "csdk/glucentralservices/util/AndroidPlatform" &&
                method->second.name == "getRamAmount" &&
                method->second.signature == "()D") {
                // Report 8 GiB of device RAM; a desktop comfortably covers
                // any mobile-tier memory budget the engine assumes.
                registers->vector[0] = {};
                registers->vector[0][0] =
                    std::bit_cast<std::uint64_t>(8589934592.0);
                return true;
            }
            if (ApplyCompatOverride("double", method->second.class_type,
                                    method->second.name,
                                    method->second.signature, registers,
                                    call_error))
                return true;
            if (backend_ == nullptr) {
                // No Java backend runs the target; an unimplemented double
                // method returns the JNI zero default rather than aborting.
                if (call_error != nullptr)
                    *call_error =
                        "trace:jni-call-double-stub-" +
                        method->second.class_type + "." +
                        method->second.name + method->second.signature;
                registers->vector[0] = {};
                return true;
            }
            SetError(call_error,
                     "guest JNI CallDoubleMethodV " + described +
                         " is not yet supported");
            return false;
        };
    const auto call_float_method_v =
        [this](Aarch64ImportRegistry::Registers* registers,
               std::string* call_error) {
            if (registers == nullptr ||
                !IsCurrentEnvironment((*registers)[0]) ||
                (*registers)[3] == 0) {
                SetError(call_error,
                         "guest JNI CallFloatMethodV arguments are invalid");
                return false;
            }
            Aarch64VaList arguments{};
            if (!ReadGuestVaList(address_space_, (*registers)[3],
                                 &arguments, call_error))
                return false;
            const std::lock_guard lock(mutex_);
            if (!IsObjectReferenceLocked((*registers)[1])) {
                SetError(call_error,
                         "guest JNI CallFloatMethodV object is invalid");
                return false;
            }
            const auto motion_float =
                motion_events_.find(ReferenceIdentityLocked((*registers)[1])
                                        .value_or((*registers)[1]));
            if (motion_float != motion_events_.end()) {
                const auto motion_method =
                    method_references_.find((*registers)[2]);
                const std::string_view motion_name =
                    motion_method != method_references_.end()
                        ? std::string_view(motion_method->second.name)
                        : std::string_view();
                float motion_result = 0.0f;
                if (motion_name == "getX" || motion_name == "getRawX") {
                    motion_result = motion_float->second.x;
                } else if (motion_name == "getY" ||
                           motion_name == "getRawY") {
                    motion_result = motion_float->second.y;
                } else if (motion_name == "getPressure" ||
                           motion_name == "getSize" ||
                           motion_name == "getTouchMajor" ||
                           motion_name == "getToolMajor" ||
                           motion_name == "getXPrecision" ||
                           motion_name == "getYPrecision") {
                    motion_result = 1.0f;
                }
                registers->SetFloatResult(motion_result);
                return true;
            }
            {
                const auto input_range =
                    input_ranges_.find(ReferenceIdentityLocked((*registers)[1])
                                           .value_or((*registers)[1]));
                if (input_range != input_ranges_.end()) {
                    const auto range_method =
                        method_references_.find((*registers)[2]);
                    const std::string_view range_name =
                        range_method != method_references_.end()
                            ? std::string_view(range_method->second.name)
                            : std::string_view();
                    float result = 0.0f;
                    if (range_name == "getMax") {
                        result = input_range->second.max;
                    } else if (range_name == "getRange") {
                        result =
                            input_range->second.max - input_range->second.min;
                    }
                    // getMin/getFlat/getOrientation getFlat stay at the stored
                    // 0; a [0, span] range with no flat dead-zone is correct
                    // for a touchscreen axis.
                    registers->SetFloatResult(result);
                    return true;
                }
            }
            const auto method = method_references_.find((*registers)[2]);
            if (method == method_references_.end() ||
                method->second.is_static) {
                if (backend_ == nullptr) {
                    std::fprintf(stderr,
                                 "ISOMR_JNI_FLOAT_STUB mid=%llx\n",
                                 (unsigned long long)(*registers)[2]);
                    std::fflush(stderr);
                    registers->SetFloatResult(0.0f);
                    return true;
                }
                SetError(call_error,
                         "guest JNI CallFloatMethodV method is invalid");
                return false;
            }
            const std::optional<std::string_view> object_type =
                ObjectTypeLocked((*registers)[1]);
            const bool receiver_matches =
                object_type &&
                ReceiverClassMatches(*object_type,
                                     method->second.class_type);
            if (!receiver_matches) {
                SetError(call_error,
                         "guest JNI CallFloatMethodV receiver " +
                             std::string(object_type.value_or("<unknown>")) +
                             " does not match declaring class " +
                             method->second.class_type);
                return false;
            }
            if (backend_ == nullptr &&
                method->second.class_type ==
                    "com/king/audio/AudioSessionHandler" &&
                method->second.name == "GetHardwareOutputVolume" &&
                method->second.signature == "()F") {
                // Query the current native Windows render endpoint. A host
                // without an endpoint is treated as silent, not as a
                // fabricated full-volume Android device.
                const float volume =
                    NativeWindowsOutputVolume().value_or(0.0f);
                registers->SetFloatResult(volume);
                return true;
            }
            if (backend_ == nullptr &&
                NormalizeCompatibilityType(method->second.class_type) ==
                    "com/ea/blast/DisplayAndroidDelegate" &&
                (method->second.name == "GetDpiX" ||
                 method->second.name == "GetDpiY") &&
                method->second.signature == "()F") {
                // Report the Android mdpi baseline density; the compat
                // runtime has no display metrics service.
                registers->SetFloatResult(160.0f);
                return true;
            }
            if (ApplyCompatOverride("float", method->second.class_type,
                                    method->second.name,
                                    method->second.signature, registers,
                                    call_error))
                return true;
            if (backend_ == nullptr) {
                // No Java backend runs the target; an unimplemented float
                // method returns the JNI zero default rather than aborting.
                if (call_error != nullptr)
                    *call_error =
                        "trace:jni-call-float-stub-" +
                        method->second.class_type + "." +
                        method->second.name + method->second.signature;
                registers->SetFloatResult(0.0f);
                return true;
            }
            SetError(call_error,
                     "guest JNI CallFloatMethodV class " +
                         method->second.class_type + " method " +
                         method->second.name + " signature " +
                         method->second.signature +
                         " is not yet supported");
            return false;
        };
    const auto call_void_method_v =
        [this](Aarch64ImportRegistry::Registers* registers,
               std::string* call_error) {
            if (registers == nullptr ||
                !IsCurrentEnvironment((*registers)[0]) ||
                (*registers)[3] == 0) {
                SetError(call_error,
                         "guest JNI CallVoidMethodV arguments are invalid");
                return false;
            }
            Aarch64VaList arguments{};
            if (!ReadGuestVaList(address_space_, (*registers)[3],
                                 &arguments, call_error))
                return false;
            const std::lock_guard lock(mutex_);
            if ((*registers)[1] == 0 && backend_ == nullptr) {
                // A void call on a null receiver (a method resolved on a stub
                // object that came back null) is a benign no-op when no Java
                // backend runs the target.
                (*registers)[0] = 0;
                return true;
            }
            if (motion_events_.count(ReferenceIdentityLocked((*registers)[1])
                                       .value_or((*registers)[1])) != 0) {
                // MotionEvent.recycle/setLocation/offsetLocation/transform
                // and friends are no-ops on a compatibility event.
                (*registers)[0] = 0;
                return true;
            }
            if (!IsObjectReferenceLocked((*registers)[1])) {
                const auto method_dbg =
                    method_references_.find((*registers)[2]);
                std::fprintf(stderr,
                             "ISOMR_JNI_VOID_OBJ_BAD ref=%llx local=%d global=%d cls=%d gcls=%d mid=%s.%s\n",
                             (unsigned long long)(*registers)[1],
                             (int)object_references_.count((*registers)[1]),
                             (int)global_object_references_.count(
                                 (*registers)[1]),
                             (int)class_references_.count((*registers)[1]),
                             (int)global_class_references_.count(
                                 (*registers)[1]),
                             method_dbg != method_references_.end()
                                 ? method_dbg->second.class_type.c_str()
                                 : "?",
                             method_dbg != method_references_.end()
                                 ? method_dbg->second.name.c_str()
                                 : "?");
                std::fflush(stderr);
                SetError(call_error,
                         "guest JNI CallVoidMethodV object is invalid");
                return false;
            }
            const auto method = method_references_.find((*registers)[2]);
            if (method == method_references_.end() ||
                method->second.is_static) {
                if (backend_ == nullptr) {
                    // No Java backend to reach: a void call on an unknown or
                    // static method id is a benign no-op (ack+drop) rather
                    // than a frame-aborting fault.
                    std::fprintf(stderr,
                                 "ISOMR_JNI_VOID_STUB mid=%llx\n",
                                 (unsigned long long)(*registers)[2]);
                    std::fflush(stderr);
                    (*registers)[0] = 0;
                    return true;
                }
                SetError(call_error,
                         "guest JNI CallVoidMethodV method is invalid");
                return false;
            }
            const std::optional<std::string_view> object_type =
                ObjectTypeLocked((*registers)[1]);
            const auto receiver_matches_declaring_class = [&]() {
                if (!object_type) return false;
                // Nimble stores dotted declaring classes while receivers
                // carry slashed JNI types; compare normalized forms.
                if (ReceiverClassMatches(*object_type,
                                         method->second.class_type))
                    return true;
                if (method->second.class_type.size() >= 2 &&
                    method->second.class_type.front() == 'L' &&
                    method->second.class_type.back() == ';') {
                    return *object_type == method->second.class_type;
                }
                return object_type->size() >= 2 &&
                       object_type->front() == 'L' &&
                       object_type->back() == ';' &&
                       object_type->substr(1, object_type->size() - 2) ==
                           method->second.class_type;
            };
            const auto finish = [&]() {
                if (call_error != nullptr) {
                    *call_error =
                        "trace:jni-call-void-" + method->second.class_type +
                        "." + method->second.name +
                        method->second.signature;
                }
                (*registers)[0] = 0;
                return true;
            };
            if (backend_ == nullptr &&
                receiver_matches_declaring_class() &&
                NormalizeCompatibilityType(method->second.class_type) ==
                    "java/util/Scanner" &&
                method->second.name == "close" &&
                method->second.signature == "()V") {
                const std::optional<std::uint64_t> identity =
                    ReferenceIdentityLocked((*registers)[1]);
                const auto scanner = identity ? scanners_.find(*identity)
                                              : scanners_.end();
                if (!identity || scanner == scanners_.end() ||
                    !CloseInputStreamLocked(
                        scanner->second.input_stream_identity,
                        call_error)) {
                    if (call_error == nullptr || call_error->empty())
                        SetError(call_error,
                                 "guest JNI Scanner.close state is invalid");
                    return false;
                }
                scanner->second.consumed = true;
                return finish();
            }
            if (backend_ == nullptr &&
                receiver_matches_declaring_class() &&
                NormalizeCompatibilityType(method->second.class_type) ==
                    "java/io/InputStream" &&
                method->second.name == "close" &&
                method->second.signature == "()V") {
                const std::optional<std::uint64_t> identity =
                    ReferenceIdentityLocked((*registers)[1]);
                if (!identity ||
                    !CloseInputStreamLocked(*identity, call_error))
                    return false;
                return finish();
            }
            if (backend_ == nullptr &&
                receiver_matches_declaring_class() &&
                NormalizeCompatibilityType(method->second.class_type) ==
                    "android/content/SharedPreferences$Editor" &&
                method->second.name == "apply" &&
                method->second.signature == "()V") {
                const std::optional<std::uint64_t> editor_identity =
                    ReferenceIdentityLocked((*registers)[1]);
                auto editor = editor_identity
                                  ? shared_preferences_editors_.find(
                                        *editor_identity)
                                  : shared_preferences_editors_.end();
                if (!editor_identity ||
                    editor == shared_preferences_editors_.end() ||
                    editor->second.preference_name.empty()) {
                    SetError(call_error,
                             "guest JNI SharedPreferences.Editor.apply state is invalid");
                    return false;
                }
                auto& preferences = shared_preferences_ints_[
                    editor->second.preference_name];
                for (const auto& [key, value] :
                     editor->second.pending_ints)
                    preferences[key] = value;
                editor->second.pending_ints.clear();
                auto& string_preferences = shared_preferences_strings_[
                    editor->second.preference_name];
                for (const auto& [key, value] :
                     editor->second.pending_strings)
                    string_preferences[key] = value;
                editor->second.pending_strings.clear();
                return finish();
            }
            if (backend_ == nullptr &&
                receiver_matches_declaring_class() &&
                method->second.class_type ==
                    "com/king/usdk/popupguard/internal/PopupDialog" &&
                method->second.name == "showNativeDialog" &&
                method->second.signature ==
                    "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;ZJ)V") {
                std::array<std::uint64_t, 5> values{};
                for (std::size_t index = 0; index < values.size(); ++index) {
                    if (!ReadGuestGeneralArgument(address_space_,
                                                  (*registers)[3],
                                                  &values[index], call_error,
                                                  index))
                        return false;
                }
                const auto copied_string = [&](std::uint64_t reference)
                    -> const std::string* {
                    const auto local =
                        FindOwnedLocalObjectReferenceLocked(reference);
                    if (local != object_references_.end() &&
                        IsStringTypeName(local->second.type))
                        return &local->second.string_value;
                    const auto global =
                        global_object_references_.find(reference);
                    if (global != global_object_references_.end() &&
                        IsStringTypeName(global->second.type))
                        return &global->second.string_value;
                    return nullptr;
                };
                const std::string* message = copied_string(values[0]);
                const std::string* title = copied_string(values[1]);
                const std::string* button = copied_string(values[2]);
                if (message == nullptr || title == nullptr ||
                    button == nullptr) {
                    SetError(call_error,
                             "guest JNI CallVoidMethodV PopupDialog text "
                             "arguments are invalid");
                    return false;
                }
                if (values[3] > 1) {
                    SetError(call_error,
                             "guest JNI CallVoidMethodV PopupDialog boolean "
                             "argument is invalid");
                    return false;
                }
                if (!QueuePopupDialogRequestLocked(
                        PopupDialogRequest{0, *message, *title, *button,
                                           values[3] != 0, values[4]},
                        call_error))
                    return false;
                return finish();
            }
            if (backend_ == nullptr &&
                (method->second.class_type == "java/lang/Thread" ||
                 method->second.class_type == "Ljava/lang/Thread;") &&
                method->second.name == "setName" &&
                method->second.signature == "(Ljava/lang/String;)V") {
                if (!receiver_matches_declaring_class()) {
                    SetError(call_error,
                             "guest JNI CallVoidMethodV Thread.setName "
                             "receiver is invalid");
                    return false;
                }
                std::uint64_t name_reference = 0;
                if (!ReadGuestGeneralArgument(address_space_,
                                              (*registers)[3],
                                              &name_reference, call_error))
                    return false;
                const std::string* name = nullptr;
                const auto local_name =
                    FindOwnedLocalObjectReferenceLocked(name_reference);
                if (local_name != object_references_.end() &&
                    IsStringTypeName(local_name->second.type))
                    name = &local_name->second.string_value;
                else {
                    const auto global_name =
                        global_object_references_.find(name_reference);
                    if (global_name != global_object_references_.end() &&
                        IsStringTypeName(global_name->second.type))
                        name = &global_name->second.string_value;
                }
                if (name == nullptr) {
                    SetError(call_error,
                             "guest JNI CallVoidMethodV Thread.setName "
                             "argument is not a string reference");
                    return false;
                }
                if (thread_state_ == nullptr ||
                    thread_state_->SetCurrentJavaThreadName(
                        *name, call_error) != 0) {
                    if (call_error != nullptr && call_error->empty())
                        *call_error =
                            "guest JNI CallVoidMethodV Thread.setName "
                            "native Windows update failed";
                    return false;
                }
                return finish();
            }
            if (backend_ == nullptr &&
                method->second.class_type ==
                    "com/king/core/HapticManager" &&
                method->second.name ==
                    "setHapticsInExternalDisplayEnabled" &&
                method->second.signature == "(Z)V") {
                if (!receiver_matches_declaring_class()) {
                    SetError(call_error,
                             "guest JNI CallVoidMethodV haptics receiver " +
                                 std::string(object_type.value_or("<unknown>")) +
                                 " does not match declaring class " +
                                 method->second.class_type);
                    return false;
                }
                std::uint64_t enabled = 0;
                if (!ReadGuestGeneralArgument(address_space_,
                                              (*registers)[3], &enabled,
                                              call_error))
                    return false;
                if (enabled > 1) {
                    SetError(call_error,
                             "guest JNI CallVoidMethodV haptics argument is not boolean");
                    return false;
                }
                haptics_in_external_display_enabled_ = enabled != 0;
                return finish();
            }
            if (backend_ == nullptr &&
                method->second.class_type ==
                    "com/king/core/HapticManager" &&
                method->second.name == "vibrate" &&
                method->second.signature == "(I)V") {
                if (!receiver_matches_declaring_class()) {
                    SetError(call_error,
                             "guest JNI CallVoidMethodV haptics receiver " +
                                 std::string(object_type.value_or("<unknown>")) +
                                 " does not match declaring class " +
                                 method->second.class_type);
                    return false;
                }
                std::uint64_t duration = 0;
                if (!ReadGuestGeneralArgument(address_space_,
                                              (*registers)[3], &duration,
                                              call_error))
                    return false;
                if (duration > 0xFFFFFFFFULL) {
                    SetError(call_error,
                             "guest JNI CallVoidMethodV vibrate argument is not a jint");
                    return false;
                }
                // Native Windows has no vibrator device; record the bounded
                // request and drop it exactly like an absent Android vibrator.
                ++haptics_vibrate_requests_;
                return finish();
            }
            if (backend_ == nullptr &&
                method->second.class_type == "android/view/View" &&
                method->second.name ==
                    "setOnSystemUiVisibilityChangeListener" &&
                method->second.signature ==
                    "(Landroid/view/View$OnSystemUiVisibilityChangeListener;)V") {
                const std::optional<std::string_view> receiver_view =
                    ObjectTypeLocked((*registers)[1]);
                if (!receiver_view ||
                    receiver_view->find("View") == std::string_view::npos) {
                    SetError(call_error,
                             "guest JNI View receiver is invalid");
                    return false;
                }
                // Native Windows has no system-UI visibility transitions;
                // the listener proxy is accepted and never fired.
                (*registers)[0] = 0;
                return true;
            }
            if (backend_ == nullptr &&
                method->second.class_type == "Landroid/app/Activity;" &&
                method->second.name == "setKeepScreenOn" &&
                method->second.signature == "(Z)V") {
                if (!object_type || !object_type->starts_with('L') ||
                    !object_type->ends_with("Activity;")) {
                    SetError(call_error,
                             "guest JNI CallVoidMethodV keep-screen-on receiver is invalid");
                    return false;
                }
                std::uint64_t enabled = 0;
                if (!ReadGuestGeneralArgument(address_space_,
                                              (*registers)[3], &enabled,
                                              call_error))
                    return false;
                if (enabled > 1) {
                    SetError(call_error,
                             "guest JNI CallVoidMethodV keep-screen-on argument is not boolean");
                    return false;
                }
                keep_screen_on_ = enabled != 0;
                return finish();
            }
            if (backend_ == nullptr &&
                NormalizeCompatibilityType(method->second.class_type) ==
                    "android/app/Activity" &&
                method->second.name == "runOnUiThread" &&
                method->second.signature ==
                    "(Ljava/lang/Runnable;)V") {
                if (!object_type ||
                    !IsCompatibilityAssignable(*object_type,
                                               "android/app/Activity")) {
                    SetError(call_error,
                             "guest JNI Activity.runOnUiThread receiver is invalid");
                    return false;
                }
                std::uint64_t runnable = 0;
                if (!ReadGuestGeneralArgument(address_space_,
                                              (*registers)[3], &runnable,
                                              call_error))
                    return false;
                const std::optional<std::string_view> runnable_type =
                    ObjectTypeLocked(runnable);
                const std::optional<std::uint64_t> runnable_identity =
                    ReferenceIdentityLocked(runnable);
                const auto proxy =
                    runnable_identity
                        ? interface_proxies_.find(*runnable_identity)
                        : interface_proxies_.end();
                const bool implements_runnable =
                    proxy != interface_proxies_.end() &&
                    std::ranges::any_of(
                        proxy->second.interfaces,
                        [](const std::string& interface_type) {
                            return NormalizeCompatibilityType(
                                       interface_type) ==
                                   "java/lang/Runnable";
                        });
                constexpr std::size_t kMaximumPendingUiTasks = 1024;
                if (!runnable_type || !runnable_identity ||
                    !implements_runnable ||
                    pending_ui_tasks_.size() >=
                        kMaximumPendingUiTasks) {
                    SetError(call_error,
                             "guest JNI Activity.runOnUiThread runnable is invalid or queue is full");
                    return false;
                }
                // There is no Win32 UI dispatcher in the entry probe yet.
                // Preserve the JNIBridge callback handle for the launcher to
                // drain instead of claiming that the Runnable already ran.
                pending_ui_tasks_.push_back(PendingUiTask{
                    proxy->second.native_handle, UiTaskReadyAt(0)});
                return finish();
            }
            if (backend_ == nullptr &&
                method->second.class_type == "Landroid/app/Activity;" &&
                method->second.name == "onStartupLoadingFinished" &&
                method->second.signature == "()V") {
                if (!object_type || !object_type->starts_with('L') ||
                    !object_type->ends_with("Activity;")) {
                    SetError(call_error,
                             "guest JNI CallVoidMethodV startup-loading-finished receiver is invalid");
                    return false;
                }
                // This is the guest's observed lifecycle notification. Keep
                // the state available to the native host without painting or
                // hiding any host-composited content on the guest's behalf.
                startup_loading_finished_ = true;
                return finish();
            }
            if (backend_ == nullptr &&
                method->second.class_type == "Landroid/app/Activity;" &&
                method->second.name == "initializeInputMapProvider" &&
                method->second.signature == "()V") {
                if (!object_type || !object_type->starts_with('L') ||
                    !object_type->ends_with("Activity;")) {
                    SetError(call_error,
                             "guest JNI CallVoidMethodV input-map receiver is invalid");
                    return false;
                }
                input_map_provider_initialized_ = true;
                return finish();
            }
            if (backend_ == nullptr &&
                method->second.class_type ==
                    "com/king/core/AppNotRespondingTaskProcessor" &&
                method->second.name == "scheduleTaskOnUiThread" &&
                method->second.signature == "()V") {
                if (!receiver_matches_declaring_class()) {
                    SetError(
                        call_error,
                        "guest JNI CallVoidMethodV AppNotRespondingTaskProcessor receiver is invalid");
                    return false;
                }
                // The native host has no Android UI looper. Preserve the
                // validated request so a Win32 main-thread scheduler can
                // consume it without claiming that the task already ran.
                app_not_responding_ui_task_requested_ = true;
                return finish();
            }
            if (backend_ == nullptr &&
                method->second.class_type ==
                    "com/king/core/AppNotRespondingTaskProcessor" &&
                method->second.name == "setActive" &&
                method->second.signature == "(Z)V") {
                if (!receiver_matches_declaring_class()) {
                    SetError(
                        call_error,
                        "guest JNI CallVoidMethodV AppNotRespondingTaskProcessor.setActive receiver is invalid");
                    return false;
                }
                std::uint64_t active = 0;
                if (!ReadGuestGeneralArgument(address_space_,
                                              (*registers)[3], &active,
                                              call_error))
                    return false;
                if (active > 1) {
                    SetError(
                        call_error,
                        "guest JNI CallVoidMethodV AppNotRespondingTaskProcessor.setActive argument is not boolean");
                    return false;
                }
                // This is local task-processor lifecycle state. It neither
                // creates an Android UI looper nor delivers guest work
                // reentrantly from the native Windows host.
                app_not_responding_task_processor_active_ = active != 0;
                return finish();
            }
            if (backend_ == nullptr &&
                method->second.class_type ==
                    "com/king/abm/internal/AdsPlatformAbstraction" &&
                method->second.name == "setActivity" &&
                method->second.signature ==
                    "(Landroid/app/Activity;)V") {
                if (!receiver_matches_declaring_class()) {
                    SetError(call_error,
                             "guest JNI CallVoidMethodV AdsPlatformAbstraction.setActivity receiver is invalid");
                    return false;
                }
                std::uint64_t activity = 0;
                if (!ReadGuestGeneralArgument(address_space_,
                                              (*registers)[3], &activity,
                                              call_error))
                    return false;
                if (activity == 0) {
                    ads_platform_activity_identity_.reset();
                    return finish();
                }
                const std::optional<std::string_view> activity_type =
                    ObjectTypeLocked(activity);
                const std::optional<std::uint64_t> activity_identity =
                    ReferenceIdentityLocked(activity);
                if (!activity_type || !activity_identity ||
                    !activity_type->starts_with('L') ||
                    !activity_type->ends_with("Activity;")) {
                    SetError(call_error,
                             "guest JNI CallVoidMethodV AdsPlatformAbstraction.setActivity argument is invalid");
                    return false;
                }
                ads_platform_activity_identity_ = *activity_identity;
                return finish();
            }
            if (backend_ == nullptr &&
                method->second.class_type ==
                    "com/king/abm/internal/AdsPlatformAbstraction" &&
                method->second.name == "initNetworkListener" &&
                method->second.signature == "(J)V") {
                if (!receiver_matches_declaring_class()) {
                    SetError(call_error,
                             "guest JNI CallVoidMethodV AdsPlatformAbstraction.initNetworkListener receiver is invalid");
                    return false;
                }
                std::uint64_t native_pointer = 0;
                if (!ReadGuestGeneralArgument(address_space_,
                                              (*registers)[3],
                                              &native_pointer, call_error))
                    return false;
                if (native_pointer == 0) {
                    SetError(call_error,
                             "guest JNI CallVoidMethodV AdsPlatformAbstraction.initNetworkListener pointer is invalid");
                    return false;
                }
                // Preserve the guest correlation value for a future local
                // Win32 network-change adapter. Do not contact an external
                // service or fabricate a network callback here.
                ads_network_listener_native_pointer_ = native_pointer;
                return finish();
            }
            if (backend_ == nullptr &&
                method->second.class_type ==
                    "com/king/abm/internal/AdsPlatformAbstraction" &&
                method->second.name == "uninit" &&
                method->second.signature == "()V") {
                if (!receiver_matches_declaring_class()) {
                    SetError(call_error,
                             "guest JNI CallVoidMethodV AdsPlatformAbstraction.uninit receiver is invalid");
                    return false;
                }
                // Native Windows owns no Android Ads SDK lifecycle. Clear
                // the two locally recorded adapter values without inventing
                // a network listener or SDK callback.
                ads_platform_activity_identity_.reset();
                ads_network_listener_native_pointer_.reset();
                return finish();
            }
            if (backend_ == nullptr &&
                method->second.class_type ==
                    "com/king/abm/internal/Http" &&
                method->second.name == "resetHttpInstance" &&
                method->second.signature == "()V") {
                if (!receiver_matches_declaring_class()) {
                    SetError(call_error,
                             "guest JNI CallVoidMethodV Ads Http.resetHttpInstance receiver is invalid");
                    return false;
                }
                // The ABM Android HTTP singleton has no native-Windows
                // counterpart. Record its validated reset separately from
                // the local com/king/http/HttpManager task model.
                ads_http_reset_requested_ = true;
                return finish();
            }
            if (backend_ == nullptr &&
                method->second.class_type ==
                    "com/king/google/billing/GoogleBillingLibGlue" &&
                method->second.name == "connect" &&
                method->second.signature == "()V") {
                if (!receiver_matches_declaring_class()) {
                    SetError(call_error,
                             "guest JNI CallVoidMethodV GoogleBillingLibGlue.connect receiver is invalid");
                    return false;
                }
                // There is no Google Play Billing service on native Windows.
                // Record the request without claiming a connection or
                // fabricating purchase callbacks.
                google_billing_connect_requested_ = true;
                return finish();
            }
            if (backend_ == nullptr &&
                method->second.class_type ==
                    "com/king/google/billing/GoogleBillingLibGlue" &&
                method->second.name == "disconnect" &&
                method->second.signature == "()V") {
                if (!receiver_matches_declaring_class()) {
                    SetError(call_error,
                             "guest JNI CallVoidMethodV GoogleBillingLibGlue.disconnect receiver is invalid");
                    return false;
                }
                // Native Windows has no Google Play Billing connection to
                // tear down. Clear the locally recorded connect request so
                // Candy can complete its real destruction sequence without
                // fabricating a billing service or callback.
                google_billing_connect_requested_ = false;
                return finish();
            }
            if (backend_ == nullptr &&
                method->second.class_type ==
                    "com/king/usdk/facebook/sdk/FacebookSdkWrapper" &&
                method->second.name == "tearDown" &&
                method->second.signature == "(J)V") {
                if (!receiver_matches_declaring_class()) {
                    SetError(call_error,
                             "guest JNI CallVoidMethodV FacebookSdkWrapper.tearDown receiver is invalid");
                    return false;
                }
                std::uint64_t native_pointer = 0;
                if (!ReadGuestGeneralArgument(address_space_,
                                              (*registers)[3],
                                              &native_pointer, call_error))
                    return false;
                // Native Windows has no Facebook SDK transport to tear down.
                // Candy legitimately passes zero when no SDK instance was
                // created. Record the validated lifecycle request without
                // fabricating an SDK instance or callback.
                (void)native_pointer;
                facebook_sdk_teardown_requested_ = true;
                return finish();
            }
            if (backend_ == nullptr &&
                method->second.class_type ==
                    "com/king/usdk/braze/BrazeAndroidModule" &&
                method->second.name == "configure" &&
                method->second.signature ==
                    "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;ILjava/lang/String;Ljava/lang/String;ZIZI)V") {
                if (!receiver_matches_declaring_class()) {
                    SetError(call_error,
                             "guest JNI CallVoidMethodV Braze.configure receiver is invalid");
                    return false;
                }
                std::array<std::uint64_t, 10> values{};
                for (std::size_t index = 0; index < values.size(); ++index) {
                    if (!ReadGuestGeneralArgument(address_space_,
                                                  (*registers)[3],
                                                  &values[index], call_error,
                                                  index))
                        return false;
                }
                const auto is_string_reference =
                    [this](std::uint64_t reference) {
                    const auto local =
                        FindOwnedLocalObjectReferenceLocked(reference);
                    if (local != object_references_.end() &&
                        IsStringTypeName(local->second.type))
                        return true;
                    const auto global =
                        global_object_references_.find(reference);
                    return global != global_object_references_.end() &&
                           IsStringTypeName(global->second.type);
                };
                BrazeConfiguration configuration{};
                const std::array<std::size_t, 5> string_indices{
                    0, 1, 2, 4, 5};
                for (std::size_t index = 0; index < string_indices.size();
                     ++index) {
                    const std::uint64_t reference =
                        values[string_indices[index]];
                    configuration.string_present[index] = reference != 0;
                    if (reference != 0 && !is_string_reference(reference)) {
                        SetError(call_error,
                                 "guest JNI CallVoidMethodV Braze.configure string argument is invalid");
                        return false;
                    }
                }
                const std::uint32_t location_enabled =
                    static_cast<std::uint32_t>(values[6]);
                const std::uint32_t logging_enabled =
                    static_cast<std::uint32_t>(values[8]);
                if (location_enabled > 1 || logging_enabled > 1) {
                    SetError(call_error,
                             "guest JNI CallVoidMethodV Braze.configure boolean argument is invalid");
                    return false;
                }
                configuration.session_timeout = static_cast<std::int32_t>(
                    static_cast<std::uint32_t>(values[3]));
                configuration.location_enabled = location_enabled != 0;
                configuration.trigger_interval = static_cast<std::int32_t>(
                    static_cast<std::uint32_t>(values[7]));
                configuration.logging_enabled = logging_enabled != 0;
                configuration.log_level = static_cast<std::int32_t>(
                    static_cast<std::uint32_t>(values[9]));
                // Preserve non-secret request metadata for diagnostics.
                // Native Windows intentionally has no external Braze
                // transport, so this does not contact or enable a service.
                braze_configuration_ = std::move(configuration);
                return finish();
            }
            if (backend_ == nullptr &&
                method->second.class_type ==
                    "com/king/usdk/braze/BrazeAndroidModule" &&
                method->second.name == "disconnectFromNativeSide" &&
                method->second.signature == "()V") {
                if (!receiver_matches_declaring_class()) {
                    SetError(call_error,
                             "guest JNI CallVoidMethodV Braze.disconnectFromNativeSide receiver is invalid");
                    return false;
                }
                // No external Braze service is connected on native Windows.
                // Clear only the validated local adapter configuration so
                // Candy's explicit destruction sequence remains idempotent
                // at the guest-process boundary.
                braze_configuration_.reset();
                return finish();
            }
            if (backend_ == nullptr &&
                method->second.class_type ==
                    "com/king/usdk/notificationtokenprovider/NotificationTokenProviderModule" &&
                method->second.name == "disconnectFromNativeSide" &&
                method->second.signature == "()V") {
                if (!receiver_matches_declaring_class()) {
                    SetError(call_error,
                             "guest JNI CallVoidMethodV NotificationTokenProviderModule.disconnectFromNativeSide receiver is invalid");
                    return false;
                }
                // Native Windows has no Google Play token provider or push
                // transport. Record the exact validated lifecycle boundary
                // without fabricating a token, service, or callback.
                notification_token_provider_disconnected_ = true;
                return finish();
            }
            const bool is_push_notification_module =
                method->second.class_type ==
                "com/king/usdk/braze/pushnotification/NotificationModule";
            const bool is_local_notification_module =
                method->second.class_type ==
                "com/king/usdk/localnotification/LocalNotificationModule";
            if (backend_ == nullptr &&
                (is_push_notification_module ||
                 is_local_notification_module) &&
                method->second.name == "cancelAllNotifications" &&
                method->second.signature == "()V") {
                if (!receiver_matches_declaring_class()) {
                    SetError(call_error,
                             "guest JNI CallVoidMethodV NotificationModule.cancelAllNotifications receiver is invalid");
                    return false;
                }
                // There is no native Windows push-notification transport.
                // Record the cancellation request without claiming a remote
                // service or fabricating a callback.
                if (is_push_notification_module)
                    notification_cancel_all_requested_ = true;
                else
                    local_notification_cancel_all_requested_ = true;
                return finish();
            }
            if (backend_ == nullptr && is_push_notification_module &&
                method->second.name == "disconnectFromNativeSide" &&
                method->second.signature == "()V") {
                if (!receiver_matches_declaring_class()) {
                    SetError(call_error,
                             "guest JNI CallVoidMethodV NotificationModule.disconnectFromNativeSide receiver is invalid");
                    return false;
                }
                // Native Windows owns no Braze push-notification transport.
                // Clear only the local cancellation state recorded above.
                notification_cancel_all_requested_ = false;
                return finish();
            }
            if (backend_ == nullptr && is_local_notification_module &&
                method->second.name == "disconnectFromNativeSide" &&
                method->second.signature == "()V") {
                if (!receiver_matches_declaring_class()) {
                    SetError(call_error,
                             "guest JNI CallVoidMethodV LocalNotificationModule.disconnectFromNativeSide receiver is invalid");
                    return false;
                }
                // Native Windows owns no Android local-notification adapter.
                // Clear only the validated local cancellation and schedule
                // metadata without delivering or cancelling a host alert.
                local_notification_cancel_all_requested_ = false;
                local_notification_schedule_.reset();
                return finish();
            }
            if (backend_ == nullptr &&
                method->second.class_type ==
                    "com/king/usdk/localnotification/LocalNotificationModule" &&
                method->second.name == "scheduleLocalNotification" &&
                method->second.signature ==
                    "(IJLjava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;I)V") {
                if (!receiver_matches_declaring_class()) {
                    SetError(call_error,
                             "guest JNI CallVoidMethodV LocalNotificationModule.scheduleLocalNotification receiver is invalid");
                    return false;
                }
                std::array<std::uint64_t, 10> values{};
                for (std::size_t index = 0; index < values.size(); ++index) {
                    if (!ReadGuestGeneralArgument(address_space_,
                                                  (*registers)[3],
                                                  &values[index], call_error,
                                                  index))
                        return false;
                }
                const auto is_string_reference =
                    [this](std::uint64_t reference) {
                    const auto local =
                        FindOwnedLocalObjectReferenceLocked(reference);
                    if (local != object_references_.end() &&
                        IsStringTypeName(local->second.type))
                        return true;
                    const auto global =
                        global_object_references_.find(reference);
                    return global != global_object_references_.end() &&
                           IsStringTypeName(global->second.type);
                };
                LocalNotificationSchedule schedule{};
                schedule.first_integer = static_cast<std::int32_t>(
                    static_cast<std::uint32_t>(values[0]));
                schedule.long_value = static_cast<std::int64_t>(values[1]);
                for (std::size_t index = 0;
                     index < schedule.string_present.size(); ++index) {
                    const std::uint64_t reference = values[index + 2];
                    schedule.string_present[index] = reference != 0;
                    if (reference != 0 && !is_string_reference(reference)) {
                        SetError(call_error,
                                 "guest JNI CallVoidMethodV LocalNotificationModule.scheduleLocalNotification string argument is invalid");
                        return false;
                    }
                }
                schedule.final_integer = static_cast<std::int32_t>(
                    static_cast<std::uint32_t>(values[9]));
                // Preserve only non-sensitive request metadata. No Windows
                // notification is claimed until a real local adapter exists.
                local_notification_schedule_ = schedule;
                return finish();
            }
            if (backend_ == nullptr &&
                method->second.class_type ==
                    "com/king/usdk/cloudstorage/CloudStorage" &&
                method->second.name == "ClearKey" &&
                method->second.signature == "(Ljava/lang/String;)V") {
                if (!receiver_matches_declaring_class()) {
                    SetError(call_error,
                             "guest JNI CallVoidMethodV CloudStorage.ClearKey "
                             "receiver is invalid");
                    return false;
                }
                std::uint64_t key_reference = 0;
                if (!ReadGuestGeneralArgument(address_space_,
                                              (*registers)[3],
                                              &key_reference, call_error))
                    return false;
                const auto local_key =
                    FindOwnedLocalObjectReferenceLocked(key_reference);
                const auto global_key =
                    global_object_references_.find(key_reference);
                if ((local_key == object_references_.end() &&
                     global_key == global_object_references_.end()) ||
                    (local_key != object_references_.end()
                         ? local_key->second.type
                         : global_key->second.type) !=
                        "Ljava/lang/String;") {
                    SetError(call_error,
                             "guest JNI CallVoidMethodV CloudStorage.ClearKey "
                             "key is not a string reference");
                    return false;
                }
                return finish();
            }
            if (backend_ == nullptr &&
                method->second.class_type == "com/king/http/HttpManager" &&
                method->second.name == "addHeader" &&
                method->second.signature ==
                    "(Lcom/king/http/HttpHeaders;Ljava/lang/String;Ljava/lang/String;)V") {
                if (!receiver_matches_declaring_class()) {
                    SetError(call_error,
                             "guest JNI CallVoidMethodV HttpManager.addHeader "
                             "receiver is invalid");
                    return false;
                }
                std::array<std::uint64_t, 3> argument_references{};
                for (std::size_t index = 0;
                     index < argument_references.size(); ++index) {
                    if (!ReadGuestGeneralArgument(
                            address_space_, (*registers)[3],
                            &argument_references[index], call_error, index))
                        return false;
                }
                const std::optional<std::string_view> headers_type =
                    ObjectTypeLocked(argument_references[0]);
                const std::optional<std::uint64_t> headers_identity =
                    ReferenceIdentityLocked(argument_references[0]);
                if (!headers_identity || !headers_type ||
                    (*headers_type != "com/king/http/HttpHeaders" &&
                     NormalizeCompatibilityType(*headers_type) != "com/king/http/HttpHeaders")) {
                    SetError(call_error,
                             "guest JNI CallVoidMethodV HttpManager.addHeader "
                             "headers object is invalid");
                    return false;
                }
                const auto string_value = [this](std::uint64_t reference)
                    -> const std::string* {
                    const auto local =
                        FindOwnedLocalObjectReferenceLocked(reference);
                    if (local != object_references_.end() &&
                        IsStringTypeName(local->second.type))
                        return &local->second.string_value;
                    const auto global =
                        global_object_references_.find(reference);
                    if (global != global_object_references_.end() &&
                        IsStringTypeName(global->second.type))
                        return &global->second.string_value;
                    return nullptr;
                };
                const std::string* name = string_value(argument_references[1]);
                const std::string* value =
                    string_value(argument_references[2]);
                if (name == nullptr || value == nullptr) {
                    SetError(call_error,
                             "guest JNI CallVoidMethodV HttpManager.addHeader "
                             "name or value is not a string reference");
                    return false;
                }
                // Preserve the Java facade's local request state. Native
                // Windows networking remains disconnected, so this performs
                // no network I/O and does not fabricate a response.
                http_headers_.push_back(
                    HttpHeaderEntry{*headers_identity, *name, *value});
                return finish();
            }
            if (backend_ == nullptr &&
                method->second.class_type == "com/king/http/HttpManager" &&
                method->second.name == "executeTask" &&
                method->second.signature ==
                    "(Lcom/king/http/HttpTask;)V") {
                if (!receiver_matches_declaring_class()) {
                    SetError(call_error,
                             "guest JNI CallVoidMethodV HttpManager.executeTask "
                             "receiver is invalid");
                    return false;
                }
                std::uint64_t task_reference = 0;
                if (!ReadGuestGeneralArgument(address_space_,
                                              (*registers)[3],
                                              &task_reference, call_error))
                    return false;
                const std::optional<std::string_view> task_type =
                    ObjectTypeLocked(task_reference);
                const std::optional<std::uint64_t> task_identity =
                    ReferenceIdentityLocked(task_reference);
                const auto task = task_identity
                                      ? http_tasks_.find(*task_identity)
                                      : http_tasks_.end();
                if (!task_identity || !task_type ||
                    NormalizeCompatibilityType(*task_type) != "com/king/http/HttpTask" ||
                    task == http_tasks_.end()) {
                    SetError(call_error,
                             "guest JNI CallVoidMethodV HttpManager.executeTask "
                             "task object is invalid");
                    return false;
                }
                // Record the lifecycle transition without contacting a
                // network service or fabricating a completion callback.
                task->second.execution_requested = true;
                RecordHttpLifecycleEventLocked(
                    HttpLifecycleEventKind::kExecuteTask,
                    task->second.request_id);
                return finish();
            }
            if (backend_ == nullptr &&
                method->second.class_type == "com/king/http/HttpManager" &&
                method->second.name == "cancelTask" &&
                method->second.signature == "(J)V") {
                if (!receiver_matches_declaring_class()) {
                    SetError(call_error,
                             "guest JNI CallVoidMethodV HttpManager.cancelTask "
                             "receiver is invalid");
                    return false;
                }
                std::uint64_t request_id = 0;
                if (!ReadGuestGeneralArgument(address_space_,
                                              (*registers)[3], &request_id,
                                              call_error))
                    return false;
                // Record the guest's cancellation request locally. There is
                // no Windows network operation or completion callback to
                // cancel until a native transport is implemented.
                last_http_cancel_request_id_ = request_id;
                RecordHttpLifecycleEventLocked(
                    HttpLifecycleEventKind::kCancelTask, request_id);
                return finish();
            }
            if (backend_ == nullptr &&
                method->second.class_type == "com/king/http/HttpManager" &&
                method->second.name == "shutDown" &&
                method->second.signature == "()V") {
                if (!receiver_matches_declaring_class()) {
                    SetError(call_error,
                             "guest JNI CallVoidMethodV HttpManager.shutDown "
                             "receiver is invalid");
                    return false;
                }
                // The compatibility facade owns no native-Windows network
                // transport. Discard its local request state without
                // fabricating completion callbacks or contacting a service.
                http_headers_.clear();
                http_tasks_.clear();
                last_http_cancel_request_id_.reset();
                return finish();
            }
            if (backend_ == nullptr &&
                method->second.class_type ==
                    "com/king/device/DeviceImpl" &&
                method->second.signature == "()V" &&
                (method->second.name == "start" ||
                 method->second.name == "stop" ||
                 method->second.name == "getScreenInfo")) {
                if (!receiver_matches_declaring_class()) {
                    SetError(call_error,
                             "guest JNI CallVoidMethodV DeviceImpl receiver " +
                                 std::string(
                                     object_type.value_or("<unknown>")) +
                                 " does not match declaring class " +
                                 method->second.class_type);
                    return false;
                }
                if (method->second.name == "start")
                    device_impl_started_ = true;
                else if (method->second.name == "stop")
                    device_impl_started_ = false;
                else
                    device_screen_info_requested_ = true;
                return finish();
            }
            if (backend_ == nullptr &&
                !compatibility_.splash_removal_class.empty() &&
                method->second.class_type ==
                    compatibility_.splash_removal_class &&
                method->second.name == "removeSplashScreen" &&
                method->second.signature == "()V") {
                if (!receiver_matches_declaring_class()) {
                    SetError(call_error,
                             "guest JNI CallVoidMethodV configured splash-removal receiver is invalid");
                    return false;
                }
                // The native probe has no host-composited splash overlay.
                // Track the guest's ready transition so a Win32 launcher can
                // remove one without presenting diagnostic pixels as content.
                platform_splash_visible_ = false;
                return finish();
            }
            if (backend_ == nullptr &&
                method->second.class_type == "com.ea.nimble.ILog" &&
                method->second.name == "writeWithTitle" &&
                method->second.signature ==
                    "(ILjava/lang/String;Ljava/lang/String;[Ljava/lang/Object;)V") {
                // Nimble's C++ components forward every engine log line into
                // the Java logging service. The compat runtime has no Java
                // log service; accept the line and continue. Engine progress
                // evidence still streams through __android_log_print.
                // Decode the va_list (priority, tag, message, args) and echo
                // the message so the Nimble channel doubles as the engine's
                // boot log — it names whatever the startup is waiting on.
                // AAPCS64 va_list is a struct: __stack, __gr_top, __vr_top,
                // __gr_offs, __vr_offs. Integer/pointer varargs live at
                // __gr_top+__gr_offs while __gr_offs is negative, else at
                // __stack.
                {
                    static std::size_t nimble_log_echoes = 0;
                    if (nimble_log_echoes < 400) {
                        ++nimble_log_echoes;
                        std::array<std::uint64_t, 4> list_header{};
                        if ((*registers)[3] != 0 &&
                            address_space_->Read((*registers)[3],
                                                 list_header.data(),
                                                 sizeof(list_header))) {
                            const std::uint64_t stack_base = list_header[0];
                            const std::int32_t gr_offs = static_cast<
                                std::int32_t>(list_header[3]);
                            std::array<std::uint64_t, 4> log_args{};
                            const std::uint64_t source =
                                gr_offs < 0
                                    ? list_header[1] +
                                          static_cast<std::uint64_t>(
                                              gr_offs)
                                    : stack_base;
                            const bool decoded =
                                address_space_->Read(source, log_args.data(),
                                                     sizeof(log_args));
                            if (decoded) {
                                const auto read_string =
                                    [&](std::uint64_t ref,
                                        std::string* out) {
                                        const auto found =
                                            object_references_.find(ref);
                                        if (found == object_references_.end()) {
                                            const auto global_found =
                                                global_object_references_.find(
                                                    ref);
                                            if (global_found ==
                                                global_object_references_
                                                    .end())
                                                return false;
                                            *out = global_found->second
                                                       .string_value;
                                            return true;
                                        }
                                        *out = found->second.string_value;
                                        return true;
                                    };
                                std::string tag;
                                std::string message;
                                read_string(log_args[1], &tag);
                                read_string(log_args[2], &message);
                                std::fprintf(stderr,
                                             "ISOMR_NIMBLE_LOG tag=%s %s\n",
                                             tag.c_str(), message.c_str());
                                std::fflush(stderr);
                            }
                        }
                    }
                }
                return finish();
            }
            if (backend_ == nullptr &&
                method->second.signature == "()V" &&
                ((method->second.name == "OnLifeCycleFocusGained" ||
                  method->second.name == "OnLifeCycleFocusLost") &&
                 NormalizeCompatibilityType(method->second.class_type)
                     .starts_with("com/ea/blast/"))) {
                // Blast delegates deliver activity focus transitions to
                // their handlers. Without device sensors there is no
                // orientation state to update; accept the notification.
                return finish();
            }
            if (backend_ == nullptr &&
                NormalizeCompatibilityType(method->second.class_type) ==
                    "android/view/Choreographer" &&
                method->second.name == "postFrameCallback" &&
                method->second.signature ==
                    "(Landroid/view/Choreographer$FrameCallback;)V") {
                // Real Choreographer queues the FrameCallback to fire once on
                // the next vsync then drops it. Preserve the JNIBridge proxy
                // handle so the launcher's vsync pump delivers doFrame(J)V
                // exactly once per post — consuming the callback stops the
                // guest's re-post loop instead of stubbing it away.
                std::uint64_t callback = 0;
                if (!ReadGuestGeneralArgument(address_space_,
                                              (*registers)[3], &callback,
                                              call_error))
                    return false;
                const std::optional<std::uint64_t> callback_identity =
                    ReferenceIdentityLocked(callback);
                const auto proxy =
                    callback_identity
                        ? interface_proxies_.find(*callback_identity)
                        : interface_proxies_.end();
                constexpr std::size_t kMaximumPendingFrameCallbacks = 4096;
                if (proxy != interface_proxies_.end() &&
                    pending_frame_callbacks_.size() <
                        kMaximumPendingFrameCallbacks) {
                    pending_frame_callbacks_.push_back(
                        {callback, proxy->second.native_handle});
                }
                return finish();
            }
            std::fprintf(stderr, "ISOMR_VOIDV candidate class=%s name=%s sig=%s\n",
                         method->second.class_type.c_str(),
                         method->second.name.c_str(),
                         method->second.signature.c_str());
            if (backend_ == nullptr &&
                NormalizeCompatibilityType(method->second.class_type) ==
                    "csdk/glucentralservices/util/AndroidPlatform" &&
                method->second.name == "setToStringStore" &&
                method->second.signature ==
                    "(Ljava/lang/String;Ljava/lang/String;)V") {
                // The engine registers named values with the Java platform
                // service (storage paths and similar); keep them so the
                // matching getFromStringStore lookups resolve.
                std::uint64_t key_reference = 0;
                std::uint64_t value_reference = 0;
                if (!ReadGuestGeneralArgument(address_space_,
                                              (*registers)[3],
                                              &key_reference, call_error) ||
                    !ReadGuestGeneralArgument(address_space_,
                                              (*registers)[3],
                                              &value_reference, call_error,
                                              1)) {
                    return false;
                }
                const auto resolve_string = [&](std::uint64_t reference,
                                                std::string* out) {
                    const auto local =
                        FindOwnedLocalObjectReferenceLocked(reference);
                    if (local != object_references_.end() &&
                        IsStringTypeName(local->second.type)) {
                        *out = local->second.string_value;
                        return true;
                    }
                    const auto global =
                        global_object_references_.find(reference);
                    if (global != global_object_references_.end() &&
                        IsStringTypeName(global->second.type)) {
                        *out = global->second.string_value;
                        return true;
                    }
                    return false;
                };
                std::string key;
                std::string value;
                if (key_reference == 0 || value_reference == 0 ||
                    !resolve_string(key_reference, &key) ||
                    !resolve_string(value_reference, &value)) {
                    return finish();
                }
                compat_string_store_[key] = value;
                std::fprintf(stderr,
                             "ISOMR_STRING_STORE set key=%s value=%s\n",
                             key.c_str(), value.c_str());
                std::fflush(stderr);
                return finish();
            }
            if (backend_ == nullptr &&
                NormalizeCompatibilityType(method->second.class_type) ==
                    "csdk/gluads/GluAdsNativeBridge" &&
                method->second.name == "setAdvertisingListener" &&
                method->second.signature == "()V") {
                // The compat runtime presents no ads surface, so there is
                // no advertising listener to attach.
                return finish();
            }
            if (backend_ == nullptr &&
                object_type &&
                ReceiverClassMatches(std::string(*object_type),
                                     "com/ea/nimble/tracking/ITracking")) {
                // Nimble telemetry surfaces (ITracking) report nowhere in
                // the compat runtime; every tracking call is acknowledged
                // and dropped.
                return finish();
            }
            if (backend_ == nullptr &&
                (NormalizeCompatibilityType(method->second.class_type)
                         .rfind("com/glu/plugins/gluanalytics/", 0) == 0 ||
                 (object_type &&
                  NormalizeCompatibilityType(std::string(*object_type))
                          .rfind("com/glu/plugins/gluanalytics/", 0) == 0))) {
                // The createAnalytics factory hands back compat dummy plugin
                // objects whose reporting methods are fire-and-forget. The
                // compat runtime has no Glu analytics transport, so every
                // call on the package is acknowledged and dropped rather
                // than aborting the caller on an unimplemented signature.
                return finish();
            }
            if (backend_ == nullptr &&
                method->second.name == "setValue" &&
                method->second.signature ==
                    "(Ljava/lang/String;Ljava/io/Serializable;)V") {
                // The compat Persistence dummy stores nothing; writes are
                // acknowledged and dropped.
                return finish();
            }
            if (backend_ == nullptr &&
                NormalizeCompatibilityType(method->second.class_type) ==
                    "csdk/glucentralservices/util/AndroidPlatform" &&
                method->second.name == "writeProperties" &&
                method->second.signature ==
                    "(Ljava/lang/String;[Ljava/lang/String;)V") {
                // Property persistence is the write side of the handled
                // readProperties; the compat runtime keeps nothing, and
                // the consent flow only checks that the call succeeds.
                return finish();
            }
            if (backend_ == nullptr &&
                NormalizeCompatibilityType(method->second.class_type) ==
                    "csdk/glucentralservices/util/AndroidPlatform" &&
                method->second.name == "requestCMPConsentInfo" &&
                method->second.signature ==
                    "(JLandroid/app/Activity;)V") {
                // CMP consent request: the game passes a native callback
                // pointer (jlong) that Java's side hands back through the
                // onRequestCMPConsentInfoComplete export, which
                // dereferences it directly. Capture the handle for the
                // launcher loop to complete immediately with
                // consent-known=1; acking silently would park the flow.
                std::uint64_t handle = 0;
                if (!ReadGuestGeneralArgument(address_space_,
                                              (*registers)[3], &handle,
                                              call_error, 0))
                    return false;
                pending_consent_completions_.emplace_back(false, handle);
                std::fprintf(stderr,
                             "ISOMR_CMP_REQUEST captured handle=%llx\n",
                             static_cast<unsigned long long>(handle));
                std::fflush(stderr);
                return finish();
            }
            if (backend_ == nullptr &&
                NormalizeCompatibilityType(method->second.class_type) ==
                    "csdk/glucentralservices/util/AndroidPlatform" &&
                method->second.name == "showCMPConsentForm" &&
                method->second.signature ==
                    "(JLandroid/app/Activity;ZZ)V") {
                // CMP consent-form display: same native-callback-pointer
                // pattern as requestCMPConsentInfo; complete it with
                // onShowCMPConsentFormComplete from the launcher loop.
                std::uint64_t handle = 0;
                if (!ReadGuestGeneralArgument(address_space_,
                                              (*registers)[3], &handle,
                                              call_error, 0))
                    return false;
                pending_consent_completions_.emplace_back(true, handle);
                std::fprintf(stderr,
                             "ISOMR_CMP_FORM captured handle=%llx\n",
                             static_cast<unsigned long long>(handle));
                std::fflush(stderr);
                return finish();
            }
            if (backend_ == nullptr &&
                NormalizeCompatibilityType(method->second.class_type) ==
                    "csdk/glucentralservices/util/AndroidPlatform" &&
                method->second.name == "registerApplicationEvent" &&
                method->second.signature == "()V") {
                // Application-event registration targets the Glu analytics
                // service, which the compat runtime does not run.
                return finish();
            }
            if (backend_ == nullptr &&
                method->second.name == "scheduleEvent" &&
                method->second.signature == "(JJ)V") {
                // The game asks Java to schedule a timer callback
                // (AndroidPlatform.scheduleEvent(delay, id)) and blocks
                // progress on the matching onTimerEvent callback. The
                // compat runtime has no Java handler thread, so capture
                // the event for the launcher loop to fire back into the
                // guest export instead of acknowledging it silently.
                // Match on method identity alone and log the declaring
                // class so an unexpected source stays visible; the
                // receiver is the host activity object rather than an
                // AndroidPlatform instance.
                std::array<std::uint64_t, 2> values{};
                for (std::size_t index = 0; index < values.size(); ++index) {
                    if (!ReadGuestGeneralArgument(address_space_,
                                                  (*registers)[3],
                                                  &values[index], call_error,
                                                  index))
                        return false;
                }
                pending_scheduled_events_.push_back(values);
                std::fprintf(stderr,
                             "ISOMR_SCHEDULE_EVENT captured class=%s "
                             "a0=%llu a1=%llu\n",
                             method->second.class_type.c_str(),
                             static_cast<unsigned long long>(values[0]),
                             static_cast<unsigned long long>(values[1]));
                std::fflush(stderr);
                return finish();
            }
            if (backend_ == nullptr &&
                NormalizeCompatibilityType(method->second.class_type) ==
                    "com/ea/blast/NotificationAndroid" &&
                method->second.name == "VerifyUrlLaunch" &&
                method->second.signature == "()V") {
                // The compat runtime launches no URLs; the verification
                // callback has nothing to reject.
                return finish();
            }
            if (backend_ == nullptr &&
                NormalizeCompatibilityType(method->second.class_type) ==
                    "com/ea/blast/NotificationAndroid" &&
                method->second.name == "RegisterApplicationForNotifications" &&
                method->second.signature ==
                    "(Ljava/lang/String;)V") {
                // There is no Google Play services or notification transport
                // on native Windows, so application registration records
                // nothing (consistent with the token-provider handlers).
                return finish();
            }
            if (backend_ == nullptr &&
                NormalizeCompatibilityType(method->second.class_type) ==
                    "com/ea/blast/NotificationAndroid" &&
                (method->second.name ==
                     "NotifyPendingStartupPushNotifications" ||
                 method->second.name ==
                     "NotifyPendingStartupLocalNotifications") &&
                method->second.signature == "(JJ)V") {
                // No push or local notification transport exists to have
                // queued startup notifications; the delivery walk has
                // nothing to deliver.
                return finish();
            }
            if (backend_ == nullptr &&
                NormalizeCompatibilityType(method->second.class_type) ==
                    "com/ea/blast/ViewAndroidDelegate" &&
                method->second.name == "SetViewFrame" &&
                method->second.signature ==
                    "(ILjava/lang/Object;IIIIZ)V") {
                // Blast asks the Android view hierarchy to place a native
                // surface view. The compat runtime renders through the host
                // window surface directly; there is no view hierarchy to
                // position, so acknowledge the frame assignment.
                return finish();
            }
            if (backend_ == nullptr &&
                method->second.signature == "(Z)V" &&
                (method->second.name == "SetEnabled" ||
                 method->second.name == "UserSetVisible") &&
                NormalizeCompatibilityType(method->second.class_type)
                    .starts_with("com/ea/blast/") &&
                NormalizeCompatibilityType(method->second.class_type)
                    .ends_with("AndroidDelegate")) {
                // Delegate enable/disable switches toggle hardware-backed
                // polling the compat runtime does not perform; the virtual
                // keyboard has no soft-input surface to show or hide.
                return finish();
            }
            if (backend_ == nullptr &&
                method->second.signature == "(I)V" &&
                method->second.name == "SetStdOrientation" &&
                NormalizeCompatibilityType(method->second.class_type) ==
                    "com/ea/blast/DisplayAndroidDelegate") {
                // The engine echoes the reported orientation back; there is
                // no display service to notify.
                return finish();
            }
            if (backend_ == nullptr &&
                method->second.name == "StartTracing" &&
                method->second.signature == "(Ljava/lang/String;)V") {
                // Profiling trace start: no-op in the compat runtime.
                return finish();
            }
            if (backend_ == nullptr &&
                method->second.name == "StopTracing" &&
                method->second.signature == "()V") {
                return finish();
            }
            if (backend_ == nullptr &&
                method->second.name == "Graphics_GetScreenSizeInPixels" &&
                method->second.signature == "([I)V") {
                // The engine asks the surface view to fill an int array
                // with the pixel size; the compat surface is 1280x720.
                std::array<std::uint8_t, 16> va{};
                if ((*registers)[3] != 0 &&
                    address_space_->Read((*registers)[3], va.data(),
                                         va.size())) {
                    std::uint64_t array_ref = 0;
                    std::memcpy(&array_ref, va.data(), sizeof(array_ref));
                    const auto found = int_arrays_.find(array_ref);
                    if (found != int_arrays_.end() &&
                        found->second.size() >= 2) {
                        found->second[0] = 1280;
                        found->second[1] = 720;
                    }
                }
                (*registers)[0] = 0;
                return true;
            }
            if (backend_ == nullptr &&
                method->second.signature == "()V" &&
                (method->second.name == "OnLifeCycleFocusGained" ||
                 method->second.name == "OnLifeCycleFocusLost") &&
                NormalizeCompatibilityType(method->second.class_type)
                    .starts_with("com/ea/blast/")) {
                return finish();
            }
            if (backend_ == nullptr &&
                (method->second.name == "SetEnabled" ||
                 method->second.name == "UserSetVisible") &&
                method->second.signature == "(Z)V" &&
                NormalizeCompatibilityType(method->second.class_type)
                    .starts_with("com/ea/blast/") &&
                NormalizeCompatibilityType(method->second.class_type)
                    .ends_with("AndroidDelegate")) {
                return finish();
            }
            if (backend_ == nullptr &&
                NormalizeCompatibilityType(method->second.class_type) ==
                    "com/ea/blast/DisplayAndroidDelegate" &&
                method->second.name == "SetStdOrientation" &&
                method->second.signature == "(I)V") {
                return finish();
            }
            if (backend_ == nullptr &&
                NormalizeCompatibilityType(method->second.class_type) ==
                    "com/ea/blast/NotificationAndroid" &&
                method->second.name == "VerifyUrlLaunch" &&
                method->second.signature == "()V") {
                return finish();
            }
            if (backend_ == nullptr &&
                NormalizeCompatibilityType(method->second.class_type) ==
                    "com/ea/blast/NotificationAndroid" &&
                method->second.name == "RegisterApplicationForNotifications" &&
                method->second.signature ==
                    "(Ljava/lang/String;)V") {
                return finish();
            }
            if (backend_ == nullptr &&
                NormalizeCompatibilityType(method->second.class_type) ==
                    "com/ea/blast/NotificationAndroid" &&
                (method->second.name ==
                     "NotifyPendingStartupPushNotifications" ||
                 method->second.name ==
                     "NotifyPendingStartupLocalNotifications") &&
                method->second.signature == "(JJ)V") {
                return finish();
            }
            if (backend_ == nullptr &&
                NormalizeCompatibilityType(method->second.class_type) ==
                    "com/ea/blast/ViewAndroidDelegate" &&
                method->second.name == "SetViewFrame" &&
                method->second.signature ==
                    "(ILjava/lang/Object;IIIIZ)V") {
                return finish();
            }
            if (backend_ == nullptr &&
                NormalizeCompatibilityType(method->second.class_type) ==
                    "csdk/glucentralservices/util/AndroidPlatform" &&
                method->second.name == "setToStringStore" &&
                method->second.signature ==
                    "(Ljava/lang/String;Ljava/lang/String;)V") {
                return finish();
            }
            if (backend_ == nullptr &&
                NormalizeCompatibilityType(method->second.class_type) ==
                    "android/hardware/display/DisplayManager" &&
                method->second.name == "registerDisplayListener" &&
                method->second.signature ==
                    "(Landroid/hardware/display/DisplayManager$DisplayListener;"
                    "Landroid/os/Handler;)V") {
                // Real Android fires onDisplayAdded for existing displays as
                // soon as the listener registers. Swappy's
                // SwappyDisplayManager relies on that callback to learn the
                // refresh period; without it the frame pacer keeps a zero
                // interval and its deadline catch-up loop spins forever.
                // Capture the listener proxy so the launcher can deliver the
                // refresh-period notification.
                std::uint64_t listener = 0;
                if (ReadGuestGeneralArgument(address_space_, (*registers)[3],
                                             &listener, call_error)) {
                    const std::optional<std::uint64_t> listener_identity =
                        ReferenceIdentityLocked(listener);
                    const auto proxy =
                        listener_identity
                            ? interface_proxies_.find(*listener_identity)
                            : interface_proxies_.end();
                    if (proxy != interface_proxies_.end()) {
                        display_listeners_.push_back(
                            {listener, proxy->second.native_handle});
                    }
                }
                (*registers)[0] = 0;
                return true;
            }
            if (backend_ == nullptr &&
                method->second.name == "requestConsentInfoUpdate") {
                // UMP consent: consent is already OBTAINED, so the update
                // succeeds immediately — deliver
                // ConsentInformation$OnConsentInfoUpdateSuccessListener
                // .onConsentInfoUpdateSuccess() through the UI-task pump.
                // Args: (activity, params, successListener, failureListener).
                std::uint64_t success_listener = 0;
                if (ReadGuestGeneralArgument(address_space_, (*registers)[3],
                                             &success_listener, call_error,
                                             2)) {
                    const std::optional<std::uint64_t> listener_identity =
                        ReferenceIdentityLocked(success_listener);
                    const auto proxy =
                        listener_identity
                            ? interface_proxies_.find(*listener_identity)
                            : interface_proxies_.end();
                    if (proxy != interface_proxies_.end() &&
                        pending_ui_tasks_.size() < 1024) {
                        PendingUiTask task{};
                        task.native_handle = proxy->second.native_handle;
                        task.ready_at = UiTaskReadyAt(0);
                        task.dispatch_class =
                            "com/google/android/ump/"
                            "ConsentInformation$"
                            "OnConsentInfoUpdateSuccessListener";
                        task.dispatch_name = "onConsentInfoUpdateSuccess";
                        task.dispatch_signature = "()V";
                        task.argument_reference = 0;
                        pending_ui_tasks_.push_back(std::move(task));
                        if (call_error != nullptr)
                            *call_error =
                                "trace:jni-consent-update-queued";
                    }
                }
                (*registers)[0] = 0;
                return true;
            }
            if (backend_ == nullptr &&
                NormalizeCompatibilityType(method->second.class_type) ==
                    "android/os/Message" &&
                method->second.name == "sendToTarget" &&
                method->second.signature == "()V") {
                // Unity's SwappyDisplayManager wakes its handler thread with
                // handler.obtainMessage(0).sendToTarget(); the message exists
                // only to schedule the Callback's handleMessage, which runs
                // the queued native task that releases the render thread.
                const std::optional<std::uint64_t> identity =
                    ReferenceIdentityLocked((*registers)[1]);
                const auto callback =
                    identity ? message_callbacks_.find(*identity)
                             : message_callbacks_.end();
                if (callback != message_callbacks_.end() &&
                    pending_ui_tasks_.size() < 1024) {
                    PendingUiTask task{};
                    task.native_handle = callback->second;
                    task.ready_at = UiTaskReadyAt(0);
                    task.dispatch_class = "android/os/Handler$Callback";
                    task.dispatch_name = "handleMessage";
                    task.dispatch_signature =
                        "(Landroid/os/Message;)Z";
                    task.argument_reference = (*registers)[1];
                    pending_ui_tasks_.push_back(std::move(task));
                    if (call_error != nullptr)
                        *call_error = "trace:jni-message-queued";
                } else if (call_error != nullptr)
                    *call_error =
                        "trace:jni-message-miss-recv0x" +
                        ([](std::uint64_t v) { char b[24];
                            snprintf(b, sizeof(b), "%llx",
                                     (unsigned long long)v);
                            return std::string(b); })((*registers)[1]);
                (*registers)[0] = 0;
                return true;
            }
            // IntentFilter.addAction records each action on the filter's
            // compat storage so registerReceiver can see which broadcasts
            // the guest subscribed to.
            if (backend_ == nullptr &&
                NormalizeCompatibilityType(method->second.class_type) ==
                    "android/content/IntentFilter" &&
                method->second.name == "addAction" &&
                method->second.signature == "(Ljava/lang/String;)V") {
                std::uint64_t action_reference = 0;
                if (!ReadGuestGeneralArgument(address_space_,
                                              (*registers)[3],
                                              &action_reference, call_error))
                    return false;
                const std::string* action = nullptr;
                const auto local_action =
                    FindOwnedLocalObjectReferenceLocked(action_reference);
                if (local_action != object_references_.end() &&
                    IsStringTypeName(local_action->second.type))
                    action = &local_action->second.string_value;
                else {
                    const auto global_action =
                        global_object_references_.find(action_reference);
                    if (global_action != global_object_references_.end() &&
                        IsStringTypeName(global_action->second.type))
                        action = &global_action->second.string_value;
                }
                const auto local_filter =
                    object_references_.find((*registers)[1]);
                if (local_filter != object_references_.end() &&
                    action != nullptr) {
                    if (!local_filter->second.string_value.empty())
                        local_filter->second.string_value += '|';
                    local_filter->second.string_value += *action;
                }
                std::fprintf(stderr, "ISOMR_INTENTFILTER_ACTION '%s'\n",
                             action ? action->c_str() : "<unresolved>");
                std::fflush(stderr);
                return finish();
            }
            if (ApplyCompatOverride("void", method->second.class_type,
                                    method->second.name,
                                    method->second.signature, registers,
                                    call_error))
                return true;
            if (backend_ == nullptr) {
                // No Java backend runs the target, so a registered but
                // unimplemented void method is a benign no-op (acknowledged
                // and dropped, logged as a stub note) rather than aborting
                // the caller on an unimplemented signature.
                if (call_error != nullptr)
                    *call_error =
                        "trace:jni-call-void-stub-" + method->second.class_type +
                        "." + method->second.name + method->second.signature;
                (*registers)[0] = 0;
                return true;
            }
            if (method->second.signature != "()V") {
                SetError(call_error,
                         "guest JNI CallVoidMethodV class " +
                             method->second.class_type + " method " +
                             method->second.name + " signature " +
                             method->second.signature +
                             " is not supported");
                return false;
            }
            if (!receiver_matches_declaring_class()) {
                SetError(call_error,
                         "guest JNI CallVoidMethodV receiver " +
                             std::string(object_type.value_or("<unknown>")) +
                             " does not match declaring class " +
                             method->second.class_type);
                return false;
            }
            const std::optional<GuestJniBackend::Handle> backend_object =
                BackendReferenceLocked((*registers)[1]);
            if (!backend_object || *backend_object == 0 ||
                method->second.backend == 0 ||
                !backend_->CallVoidMethod(*backend_object,
                                          method->second.backend,
                                          call_error))
                return false;
            return finish();
        };
    const auto call_static_object_method_v =
        [this](Aarch64ImportRegistry::Registers* registers,
               std::string* call_error) {
            if (registers == nullptr ||
                !IsCurrentEnvironment((*registers)[0]) ||
                (*registers)[3] == 0) {
                std::fprintf(stderr,
                             "ISOMR_JNI_SOBJECT_V_ARGBAD tid=%u this=%p ts=%p errno=%llx regs_null=%d env=%llx env_ok=%d cur_env=%llx argp=%llx\n",
                             (unsigned)GetCurrentThreadId(),
                             (void*)this,
                             (void*)thread_state_,
                             (unsigned long long)(
                                 thread_state_ != nullptr
                                     ? thread_state_->CurrentErrnoAddress()
                                           .value_or(0)
                                     : 0),
                             registers == nullptr ? 1 : 0,
                             (unsigned long long)(registers != nullptr
                                                      ? (*registers)[0]
                                                      : 0),
                             (registers != nullptr &&
                              IsCurrentEnvironment((*registers)[0]))
                                 ? 1
                                 : 0,
                             (unsigned long long)CurrentEnvironmentAddress()
                                 .value_or(0),
                             (unsigned long long)(registers != nullptr
                                                      ? (*registers)[3]
                                                      : 0));
                std::fflush(stderr);
                SetError(call_error,
                         "guest JNI CallStaticObjectMethodV arguments are invalid");
                return false;
            }
            std::array<std::uint8_t, 32> arguments{};
            if (!address_space_->Read((*registers)[3], arguments.data(),
                                      arguments.size())) {
                SetError(call_error,
                         "guest JNI CallStaticObjectMethodV va_list is invalid");
                return false;
            }
            const std::lock_guard lock(mutex_);
            if (!IsClassReferenceLocked((*registers)[1])) {
                SetError(call_error,
                         "guest JNI CallStaticObjectMethodV class is invalid");
                return false;
            }
            const auto method = method_references_.find((*registers)[2]);
            const std::optional<std::uint64_t> class_identity =
                ReferenceIdentityLocked((*registers)[1]);
            // A jclass resolved via a fresh FindClass/local ref has a new
            // reference identity even though it denotes the same class. A
            // static method belongs to a class type, so accept the call when
            // the passed class's type matches the registered class_type even
            // if the identity tokens differ.
            const std::optional<std::string_view> call_class_type =
                ClassTypeLocked((*registers)[1]);
            const bool class_matches =
                method != method_references_.end() && class_identity &&
                (method->second.class_identity == *class_identity ||
                 (call_class_type &&
                  ReceiverClassMatches(method->second.class_type,
                                       *call_class_type)));
            if (method == method_references_.end() || !class_matches ||
                !method->second.is_static) {
                if (backend_ == nullptr) {
                    // No Java backend: a static object call that cannot be
                    // dispatched returns a live synthetic Object handle so a
                    // chained follow-up call resolves instead of re-faulting.
                    std::fprintf(stderr,
                                 "ISOMR_JNI_SOBJECT_STUB mid=%llx found=%d cls_id=%llx want_cls=%llx static=%d m=%s.%s%s\n",
                                 (unsigned long long)(*registers)[2],
                                 method != method_references_.end() ? 1 : 0,
                                 (unsigned long long)class_identity.value_or(0),
                                 (unsigned long long)(
                                     method != method_references_.end()
                                         ? method->second.class_identity
                                         : 0),
                                 method != method_references_.end() &&
                                         method->second.is_static
                                     ? 1
                                     : 0,
                                 method != method_references_.end()
                                     ? method->second.class_type.c_str()
                                     : "?",
                                 method != method_references_.end()
                                     ? method->second.name.c_str()
                                     : "?",
                                 method != method_references_.end()
                                     ? method->second.signature.c_str()
                                     : "?");
                    std::fflush(stderr);
                    const std::uint64_t reference = AllocateJniReference(
                        heap_, true, CompatibilityReferenceKind::kObject,
                        "Ljava/lang/Object;", {}, call_error);
                    if (reference == 0) {
                        (*registers)[0] = 0;
                        return true;
                    }
                    object_references_.emplace(
                        reference,
                        ObjectReference{reference, (*registers)[2],
                                        "Ljava/lang/Object;", {}, 0});
                    TrackLocalReferenceLocked(reference);
                    (*registers)[0] = reference;
                    return true;
                }
                SetError(call_error,
                         "guest JNI CallStaticObjectMethodV method is invalid");
                return false;
            }
            const std::string& signature = method->second.signature;
            const std::optional<std::string_view> class_type =
                ClassTypeLocked((*registers)[1]);
            if (call_error != nullptr) {
                *call_error =
                    "trace:jni-call-static-object-" +
                    method->second.class_type + "." + method->second.name +
                    signature;
            }
            if (backend_ == nullptr &&
                method->second.class_type ==
                    "com.ea.nimble.bridge.NimbleCppComponentRegistrar" &&
                method->second.name == "getComponentId" &&
                signature == "(Lcom/ea/nimble/Component;)Ljava/lang/String;") {
                // The registrar asks Java for each C++ component's id during
                // setup. The Component jobject is a compat object created by
                // the engine; derive a stable id from the object identity so
                // every component registers under a unique (if synthetic)
                // name and the setup walk completes.
                std::array<std::uint64_t, 4> setup_args{};
                if (!address_space_->Read((*registers)[3], setup_args.data(),
                                          sizeof(setup_args))) {
                    SetError(call_error,
                             "guest JNI getComponentId va_list is invalid");
                    return false;
                }
                const std::uint64_t component_ref = setup_args[0];
                std::string component_type;
                const auto found = object_references_.find(component_ref);
                if (found != object_references_.end())
                    component_type = found->second.type;
                char id_text[96];
                snprintf(id_text, sizeof(id_text), "cpp-%s-%llx",
                         component_type.c_str(),
                         static_cast<unsigned long long>(component_ref));
                std::fprintf(stderr, "ISOMR_NIMBLE_COMPONENT id=%s\n",
                             id_text);
                std::fflush(stderr);
                const std::uint64_t reference = AllocateCompatibilityReference(
                    heap_, CompatibilityReferenceKind::kString,
                    "Ljava/lang/String;", std::string(id_text), call_error);
                if (reference == 0) return false;
                object_references_.emplace(
                    reference,
                    ObjectReference{reference, (*registers)[2],
                                    "Ljava/lang/String;",
                                    std::string(id_text), 0});
                TrackLocalReferenceLocked(reference);
                (*registers)[0] = reference;
                return true;
            }
            if (backend_ == nullptr &&
                method->second.class_type ==
                    "com/unity3d/player/ReflectionHelper" &&
                method->second.name == "getFieldID" &&
                signature ==
                    "(Ljava/lang/Class;Ljava/lang/String;Ljava/lang/String;Z)"
                    "Ljava/lang/reflect/Field;") {
                // ReflectionHelper.getFieldID(Class,name,sig,isStatic)
                // reflects a declared field on the Java side. There is no
                // Java backend, so mint a FieldReference for the requested
                // (class,name,sig) and hand back a Field object carrying that
                // token; FromReflectedField then resolves it to a jfieldID.
                const auto read_string_arg =
                    [&](std::size_t index, std::string* out) {
                        std::uint64_t arg = 0;
                        if (!ReadGuestGeneralArgument(address_space_,
                                                      (*registers)[3], &arg,
                                                      call_error, index))
                            return false;
                        if (arg != 0) {
                            const auto local =
                                FindOwnedLocalObjectReferenceLocked(arg);
                            const auto global =
                                global_object_references_.find(arg);
                            if (local != object_references_.end())
                                *out = local->second.string_value;
                            else if (global != global_object_references_.end())
                                *out = global->second.string_value;
                        }
                        return true;
                    };
                std::uint64_t class_arg = 0;
                std::uint64_t static_arg = 0;
                std::string field_name;
                std::string field_signature;
                if (!ReadGuestGeneralArgument(address_space_, (*registers)[3],
                                              &class_arg, call_error, 0) ||
                    !read_string_arg(1, &field_name) ||
                    !read_string_arg(2, &field_signature) ||
                    !ReadGuestGeneralArgument(address_space_, (*registers)[3],
                                              &static_arg, call_error, 3))
                    return false;
                const std::optional<std::uint64_t> target_identity =
                    ReferenceIdentityLocked(class_arg);
                const std::optional<std::string_view> target_type =
                    ClassTypeLocked(class_arg);
                if (!target_identity || !target_type ||
                    !IsFieldSignature(field_signature)) {
                    SetError(call_error,
                             "guest JNI getFieldID class is invalid");
                    return false;
                }
                const std::uint64_t field_reference = heap_->Callocate(1, 8);
                if (field_reference == 0) {
                    SetError(call_error,
                             "guest JNI field reference allocation failed");
                    return false;
                }
                field_references_.emplace(
                    field_reference,
                    FieldReference{*target_identity,
                                   std::string(*target_type),
                                   field_name, field_signature,
                                   static_arg != 0});
                const std::uint64_t reference = AllocateCompatibilityReference(
                    heap_, CompatibilityReferenceKind::kObject,
                    "Ljava/lang/reflect/Field;", field_signature, call_error);
                if (reference == 0) return false;
                object_references_.emplace(
                    reference,
                    ObjectReference{reference, field_reference,
                                    "Ljava/lang/reflect/Field;",
                                    field_signature, 0});
                TrackLocalReferenceLocked(reference);
                (*registers)[0] = reference;
                return true;
            }
            if (backend_ == nullptr &&
                method->second.class_type ==
                    "com/unity3d/player/ReflectionHelper" &&
                method->second.name == "getFieldSignature" &&
                signature ==
                    "(Ljava/lang/reflect/Field;)Ljava/lang/String;") {
                // getFieldSignature(Field) returns the field's declared type
                // signature; the compat Field object stores it in
                // string_value.
                std::uint64_t field_ref = 0;
                if (!ReadGuestGeneralArgument(address_space_,
                                              (*registers)[3], &field_ref,
                                              call_error, 0))
                    return false;
                std::string field_signature;
                const auto local =
                    FindOwnedLocalObjectReferenceLocked(field_ref);
                const auto global =
                    global_object_references_.find(field_ref);
                if (local != object_references_.end())
                    field_signature = local->second.string_value;
                else if (global != global_object_references_.end())
                    field_signature = global->second.string_value;
                const std::uint64_t reference = AllocateCompatibilityReference(
                    heap_, CompatibilityReferenceKind::kString,
                    "Ljava/lang/String;", field_signature, call_error);
                if (reference == 0) return false;
                object_references_.emplace(
                    reference,
                    ObjectReference{reference, (*registers)[2],
                                    "Ljava/lang/String;", field_signature, 0});
                TrackLocalReferenceLocked(reference);
                (*registers)[0] = reference;
                return true;
            }
            if (backend_ == nullptr &&
                method->second.class_type ==
                    "com/unity3d/player/ReflectionHelper" &&
                method->second.name == "getConstructorID" &&
                signature ==
                    "(Ljava/lang/Class;Ljava/lang/String;)Ljava/lang/reflect/Constructor;") {
                // Mirror of getMethodID for <init>: mint a MethodReference
                // carrying the declared signature and hand back a Constructor
                // object so FromReflectedMethod resolves it to a jmethodID.
                std::uint64_t class_arg = 0;
                std::uint64_t signature_arg = 0;
                if (!ReadGuestGeneralArgument(address_space_, (*registers)[3],
                                              &class_arg, call_error, 0) ||
                    !ReadGuestGeneralArgument(address_space_, (*registers)[3],
                                              &signature_arg, call_error, 1))
                    return false;
                std::string ctor_signature;
                const auto local_signature =
                    FindOwnedLocalObjectReferenceLocked(signature_arg);
                const auto global_signature =
                    global_object_references_.find(signature_arg);
                if (local_signature != object_references_.end())
                    ctor_signature = local_signature->second.string_value;
                else if (global_signature != global_object_references_.end())
                    ctor_signature = global_signature->second.string_value;
                const std::optional<std::uint64_t> target_identity =
                    ReferenceIdentityLocked(class_arg);
                const std::optional<std::string_view> target_type =
                    ClassTypeLocked(class_arg);
                if (!target_identity || !target_type) {
                    SetError(call_error,
                             "guest JNI getConstructorID class is invalid");
                    return false;
                }
                const std::uint64_t method_reference = heap_->Callocate(1, 8);
                if (method_reference == 0) {
                    SetError(call_error,
                             "guest JNI method reference allocation failed");
                    return false;
                }
                method_references_.emplace(
                    method_reference,
                    MethodReference{*target_identity,
                                    std::string(*target_type), "<init>",
                                    ctor_signature, false, 0});
                const std::uint64_t reference = AllocateCompatibilityReference(
                    heap_, CompatibilityReferenceKind::kObject,
                    "Ljava/lang/reflect/Constructor;", ctor_signature,
                    call_error);
                if (reference == 0) return false;
                object_references_.emplace(
                    reference,
                    ObjectReference{reference, method_reference,
                                    "Ljava/lang/reflect/Constructor;",
                                    ctor_signature, 0});
                TrackLocalReferenceLocked(reference);
                (*registers)[0] = reference;
                return true;
            }
            if (backend_ == nullptr &&
                method->second.class_type ==
                    "com/unity3d/player/ReflectionHelper" &&
                (method->second.name == "getMethodID" ||
                 method->second.name == "getStaticMethodID") &&
                signature.starts_with("(Ljava/lang/Class;Ljava/lang/String;"
                                      "Ljava/lang/String;") &&
                signature.ends_with(")Ljava/lang/reflect/Method;")) {
                // ReflectionHelper.getMethodID/getStaticMethodID reflect a
                // declared method. Mint a MethodReference for (class,name,sig)
                // and hand back a Method object carrying that token so
                // FromReflectedMethod resolves it to a jmethodID.
                const auto read_string_arg =
                    [&](std::size_t index, std::string* out) {
                        std::uint64_t arg = 0;
                        if (!ReadGuestGeneralArgument(address_space_,
                                                      (*registers)[3], &arg,
                                                      call_error, index))
                            return false;
                        if (arg != 0) {
                            const auto local =
                                FindOwnedLocalObjectReferenceLocked(arg);
                            const auto global =
                                global_object_references_.find(arg);
                            if (local != object_references_.end())
                                *out = local->second.string_value;
                            else if (global != global_object_references_.end())
                                *out = global->second.string_value;
                        }
                        return true;
                    };
                std::uint64_t class_arg = 0;
                std::uint64_t static_arg = 0;
                std::string method_name;
                std::string method_signature;
                if (!ReadGuestGeneralArgument(address_space_, (*registers)[3],
                                              &class_arg, call_error, 0) ||
                    !read_string_arg(1, &method_name) ||
                    !read_string_arg(2, &method_signature))
                    return false;
                const bool explicit_static =
                    method->second.name == "getStaticMethodID";
                if (!explicit_static &&
                    !ReadGuestGeneralArgument(address_space_, (*registers)[3],
                                              &static_arg, call_error, 3))
                    return false;
                const std::optional<std::uint64_t> target_identity =
                    ReferenceIdentityLocked(class_arg);
                const std::optional<std::string_view> target_type =
                    ClassTypeLocked(class_arg);
                if (!target_identity || !target_type) {
                    SetError(call_error,
                             "guest JNI getMethodID class is invalid");
                    return false;
                }
                // Bounded diagnostic: log each unique resolved (class,name,
                // sig) once so the event stream shows which methods managed
                // code looks up — SDK callback names in particular.
                {
                    static std::unordered_map<std::string, bool> seen_methods;
                    std::string key = std::string(*target_type) + "." +
                                      method_name + method_signature;
                    if (seen_methods.emplace(key, true).second &&
                        call_error != nullptr)
                        *call_error = "trace:jni-reflect-mid-" + key;
                }
                const std::uint64_t method_reference = heap_->Callocate(1, 8);
                if (method_reference == 0) {
                    SetError(call_error,
                             "guest JNI method reference allocation failed");
                    return false;
                }
                method_references_.emplace(
                    method_reference,
                    MethodReference{*target_identity,
                                    std::string(*target_type), method_name,
                                    method_signature,
                                    explicit_static || static_arg != 0, 0});
                const std::uint64_t reference = AllocateCompatibilityReference(
                    heap_, CompatibilityReferenceKind::kObject,
                    "Ljava/lang/reflect/Method;", method_signature,
                    call_error);
                if (reference == 0) return false;
                object_references_.emplace(
                    reference,
                    ObjectReference{reference, method_reference,
                                    "Ljava/lang/reflect/Method;",
                                    method_signature, 0});
                TrackLocalReferenceLocked(reference);
                (*registers)[0] = reference;
                return true;
            }
            if (backend_ == nullptr &&
                class_type &&
                ReceiverClassMatches(*class_type,
                                     "com/ea/nimble/PersistenceService") &&
                method->second.name == "getPersistenceForNimbleComponent" &&
                signature.starts_with("(Ljava/lang/String;")) {
                // Persistence handles: the compat runtime persists nothing,
                // so every component gets a distinct typed dummy handle.
                const std::uint64_t reference = AllocateCompatibilityReference(
                    heap_, CompatibilityReferenceKind::kObject,
                    "Lcom/ea/nimble/Persistence;", {}, call_error);
                if (reference == 0) return false;
                object_references_.emplace(
                    reference,
                    ObjectReference{reference, 0, "Lcom/ea/nimble/Persistence;",
                                    std::string(method->second.name), 0});
                TrackLocalReferenceLocked(reference);
                (*registers)[0] = reference;
                return true;
            }
            if (backend_ == nullptr &&
                ReceiverClassMatches(*class_type, "java/lang/Long") &&
                method->second.name == "valueOf" &&
                signature == "(J)Ljava/lang/Long;") {
                // Boxed Long: keep the value in the reference's string
                // storage so Long.longValue/toString can read it back.
                std::uint64_t boxed = 0;
                if (!ReadGuestGeneralArgument(address_space_,
                                              (*registers)[3], &boxed,
                                              call_error, 0))
                    return false;
                const std::string decimal = std::to_string(
                    static_cast<std::int64_t>(boxed));
                const std::uint64_t reference = AllocateCompatibilityReference(
                    heap_, CompatibilityReferenceKind::kObject,
                    "Ljava/lang/Long;", decimal, call_error);
                if (reference == 0) return false;
                object_references_.emplace(
                    reference,
                    ObjectReference{reference, 0, "Ljava/lang/Long;", decimal,
                                    0});
                TrackLocalReferenceLocked(reference);
                (*registers)[0] = reference;
                return true;
            }
            if (backend_ == nullptr &&
                NormalizeCompatibilityType(method->second.class_type) ==
                    "android/net/Uri" &&
                method->second.name == "encode" &&
                signature.starts_with("(Ljava/lang/String;") &&
                signature.ends_with(")Ljava/lang/String;")) {
                // Uri.encode percent-encodes a string. Unity routes the
                // SharedPreferences key through it, so returning null here
                // produces a null key downstream. Read the source string and
                // re-emit it with Android's unreserved set left verbatim.
                std::uint64_t source_reference = 0;
                std::uint64_t allow_reference = 0;
                const bool two_args =
                    signature == "(Ljava/lang/String;Ljava/lang/String;)"
                                 "Ljava/lang/String;";
                if (!ReadGuestGeneralArgument(address_space_,
                                              (*registers)[3],
                                              &source_reference, call_error) ||
                    (two_args &&
                     !ReadGuestGeneralArgument(address_space_,
                                               (*registers)[3],
                                               &allow_reference, call_error,
                                               1)))
                    return false;
                const auto resolve_string =
                    [this](std::uint64_t ref) -> const std::string* {
                    const auto is_string = [](const std::string& type) {
                        return NormalizeCompatibilityType(type) ==
                               "java/lang/String";
                    };
                    const auto local = object_references_.find(ref);
                    if (local != object_references_.end() &&
                        is_string(local->second.type))
                        return &local->second.string_value;
                    const auto global = global_object_references_.find(ref);
                    if (global != global_object_references_.end() &&
                        is_string(global->second.type))
                        return &global->second.string_value;
                    return nullptr;
                };
                const std::string* source = resolve_string(source_reference);
                const std::optional<std::string_view> source_type =
                    ObjectTypeLocked(source_reference);
                std::fprintf(
                    stderr,
                    "ISOMR_URI_ENCODE src_ref=0x%llx resolved=%d type=%s "
                    "src='%s'\n",
                    static_cast<unsigned long long>(source_reference),
                    source != nullptr ? 1 : 0,
                    source_type ? std::string(*source_type).c_str() : "none",
                    source ? source->c_str() : "<null>");
                std::fflush(stderr);
                if (source == nullptr) {
                    (*registers)[0] = 0;
                    return true;
                }
                const std::string* allow =
                    two_args ? resolve_string(allow_reference) : nullptr;
                static constexpr char kHex[] = "0123456789ABCDEF";
                std::string encoded;
                encoded.reserve(source->size());
                for (const unsigned char c : *source) {
                    const bool unreserved =
                        (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                        (c >= '0' && c <= '9') ||
                        (c == '_' || c == '-' || c == '!' || c == '.' ||
                         c == '~' || c == '\'' || c == '(' || c == ')' ||
                         c == '*');
                    const bool allowed =
                        unreserved ||
                        (allow != nullptr &&
                         allow->find(static_cast<char>(c)) !=
                             std::string::npos);
                    if (allowed) {
                        encoded.push_back(static_cast<char>(c));
                    } else {
                        encoded.push_back('%');
                        encoded.push_back(kHex[c >> 4]);
                        encoded.push_back(kHex[c & 0xf]);
                    }
                }
                const std::uint64_t reference = AllocateCompatibilityReference(
                    heap_, CompatibilityReferenceKind::kString,
                    "Ljava/lang/String;", encoded, call_error);
                if (reference == 0) return false;
                object_references_.emplace(
                    reference,
                    ObjectReference{reference, (*registers)[2],
                                    "Ljava/lang/String;", encoded, 0});
                TrackLocalReferenceLocked(reference);
                (*registers)[0] = reference;
                return true;
            }
            if (class_type == "bitter/jnibridge/JNIBridge" &&
                method->second.name == "newInterfaceProxy" &&
                signature == "(J[Ljava/lang/Class;)Ljava/lang/Object;") {
                if (backend_ != nullptr) {
                    SetError(call_error,
                             "guest JNI JNIBridge proxy creation is unavailable with an active backend");
                    return false;
                }
                std::uint64_t native_handle = 0;
                std::uint64_t interfaces_reference = 0;
                if (!ReadGuestGeneralArgument(address_space_,
                                              (*registers)[3],
                                              &native_handle, call_error) ||
                    !ReadGuestGeneralArgument(address_space_,
                                              (*registers)[3],
                                              &interfaces_reference,
                                              call_error, 1))
                    return false;
                const std::optional<std::uint64_t> array_identity =
                    ReferenceIdentityLocked(interfaces_reference);
                const auto array = array_identity
                                       ? object_arrays_.find(*array_identity)
                                       : object_arrays_.end();
                if (native_handle == 0 || !array_identity ||
                    array == object_arrays_.end() ||
                    array->second.type != "[Ljava/lang/Class;" ||
                    array->second.elements.empty()) {
                    SetError(call_error,
                             "guest JNI JNIBridge proxy arguments are invalid");
                    return false;
                }
                std::vector<std::string> interfaces;
                try {
                    interfaces.reserve(array->second.elements.size());
                    for (const std::optional<ObjectArrayElement>& element :
                         array->second.elements) {
                        if (!element || !element->is_class ||
                            element->type.empty()) {
                            SetError(call_error,
                                     "guest JNI JNIBridge interface array contains an invalid class");
                            return false;
                        }
                        interfaces.push_back(element->type);
                    }
                } catch (const std::bad_alloc&) {
                    SetError(call_error,
                             "guest JNI JNIBridge interface storage allocation failed");
                    return false;
                }
                const std::string proxy_type =
                    interfaces.front().starts_with('[')
                        ? interfaces.front()
                        : "L" + interfaces.front() + ";";
                const std::uint64_t reference =
                    AllocateCompatibilityReference(
                        heap_, CompatibilityReferenceKind::kObject,
                        proxy_type, {}, call_error);
                if (reference == 0) return false;
                object_references_.emplace(
                    reference,
                    ObjectReference{reference, (*registers)[2], proxy_type,
                                    {}, 0});
                TrackLocalReferenceLocked(reference);
                interface_proxies_.emplace(
                    reference,
                    InterfaceProxyStorage{native_handle,
                                          std::move(interfaces)});
                (*registers)[0] = reference;
                return true;
            }
            if (backend_ == nullptr &&
                class_type == "android/os/Environment" &&
                method->second.name == "getExternalStorageState" &&
                signature == "()Ljava/lang/String;") {
                constexpr std::string_view kMounted = "mounted";
                const std::uint64_t reference =
                    AllocateCompatibilityReference(
                        heap_, CompatibilityReferenceKind::kString,
                        "Ljava/lang/String;", kMounted, call_error);
                if (reference == 0) return false;
                object_references_.emplace(
                    reference,
                    ObjectReference{reference, (*registers)[2],
                                    "Ljava/lang/String;",
                                    std::string(kMounted), 0});
                TrackLocalReferenceLocked(reference);
                (*registers)[0] = reference;
                return true;
            }
            if (backend_ == nullptr &&
                class_type ==
                    "com/unity3d/player/PlayAssetDeliveryUnityWrapper" &&
                method->second.name == "init" &&
                signature ==
                    "(Landroid/content/Context;)Lcom/unity3d/player/PlayAssetDeliveryUnityWrapper;") {
                std::uint64_t context = 0;
                if (!ReadGuestGeneralArgument(address_space_,
                                              (*registers)[3], &context,
                                              call_error))
                    return false;
                const std::optional<std::string_view> context_type =
                    ObjectTypeLocked(context);
                if (!context_type ||
                    !IsCompatibilityAssignable(*context_type,
                                               "android/content/Context")) {
                    SetError(call_error,
                             "guest JNI PlayAssetDeliveryUnityWrapper.init context is invalid");
                    return false;
                }
                constexpr std::string_view kWrapperType =
                    "Lcom/unity3d/player/PlayAssetDeliveryUnityWrapper;";
                const std::uint64_t reference =
                    AllocateCompatibilityReference(
                        heap_, CompatibilityReferenceKind::kObject,
                        kWrapperType, {}, call_error);
                if (reference == 0) return false;
                object_references_.emplace(
                    reference,
                    ObjectReference{reference, (*registers)[2],
                                    std::string(kWrapperType), {}, 0});
                TrackLocalReferenceLocked(reference);
                (*registers)[0] = reference;
                return true;
            }
            if (class_type ==
                    "com/king/abm/internal/DefaultFileSystem" &&
                method->second.name == "getDefaultAppDirectory" &&
                signature ==
                    "(Landroid/app/Activity;)Ljava/lang/String;") {
                if (backend_ != nullptr) {
                    SetError(call_error,
                             "guest JNI CallStaticObjectMethodV default app directory query is unavailable with an active backend");
                    return false;
                }
                std::uint64_t activity = 0;
                if (!ReadGuestGeneralArgument(address_space_,
                                              (*registers)[3], &activity,
                                              call_error))
                    return false;
                const std::optional<std::string_view> activity_type =
                    ObjectTypeLocked(activity);
                if (!activity_type ||
                    !activity_type->ends_with("Activity;")) {
                    SetError(call_error,
                             "guest JNI CallStaticObjectMethodV default app directory activity is invalid");
                    return false;
                }
                constexpr std::string_view kDefaultAppDirectory = "/data";
                const std::uint64_t reference =
                    AllocateCompatibilityReference(
                        heap_, CompatibilityReferenceKind::kString,
                        "Ljava/lang/String;", kDefaultAppDirectory,
                        call_error);
                if (reference == 0) {
                    SetError(call_error,
                             "guest JNI default app directory string allocation failed");
                    return false;
                }
                object_references_.emplace(
                    reference,
                    ObjectReference{reference, (*registers)[2],
                                    "Ljava/lang/String;",
                                    std::string(kDefaultAppDirectory), 0});
                TrackLocalReferenceLocked(reference);
                (*registers)[0] = reference;
                return true;
            }
            if (class_type == "com/king/core/AndroidCallstackUtils" &&
                method->second.name == "getPackageVersionName" &&
                signature ==
                    "(Landroid/content/Context;)Ljava/lang/String;") {
                if (backend_ != nullptr) {
                    SetError(call_error,
                             "guest JNI CallStaticObjectMethodV package version query is unavailable with an active backend");
                    return false;
                }
                std::uint64_t context = 0;
                if (!ReadGuestGeneralArgument(address_space_,
                                              (*registers)[3], &context,
                                              call_error))
                    return false;
                const std::optional<std::string_view> context_type =
                    ObjectTypeLocked(context);
                if (!context_type ||
                    (NormalizeCompatibilityType(*context_type) != "android/content/Context" &&
                     !context_type->ends_with("Activity;"))) {
                    SetError(call_error,
                             "guest JNI CallStaticObjectMethodV package version context is invalid");
                    return false;
                }
                if (compatibility_.package_version.empty()) {
                    SetError(call_error,
                             "guest JNI package version compatibility is not configured");
                    return false;
                }
                const std::uint64_t reference =
                    AllocateCompatibilityReference(
                        heap_, CompatibilityReferenceKind::kString,
                        "Ljava/lang/String;",
                        compatibility_.package_version, call_error);
                if (reference == 0) {
                    SetError(call_error,
                             "guest JNI package version string allocation failed");
                    return false;
                }
                object_references_.emplace(
                    reference,
                    ObjectReference{reference, (*registers)[2],
                                    "Ljava/lang/String;",
                                    compatibility_.package_version, 0});
                TrackLocalReferenceLocked(reference);
                (*registers)[0] = reference;
                return true;
            }
            if (class_type == "com/king/core/AndroidCallstackUtils" &&
                method->second.name == "getThreadsCallstacks" &&
                signature == "()[Ljava/lang/String;") {
                if (backend_ != nullptr) {
                    SetError(call_error,
                             "guest JNI CallStaticObjectMethodV thread-callstacks query is unavailable with an active backend");
                    return false;
                }
                constexpr std::string_view kArrayType =
                    "[Ljava/lang/String;";
                const std::uint64_t reference =
                    AllocateCompatibilityReference(
                        heap_, CompatibilityReferenceKind::kObject,
                        kArrayType, {}, call_error);
                if (reference == 0) return false;
                object_references_.emplace(
                    reference,
                    ObjectReference{reference, (*registers)[2],
                                    std::string(kArrayType), {}, 0});
                TrackLocalReferenceLocked(reference);
                // The native host has no Android Java thread runtime to
                // enumerate. Preserve the JNI array contract while reporting
                // no fabricated Java call stacks.
                object_arrays_.emplace(
                    reference,
                    ObjectArrayStorage{std::string(kArrayType),
                                       "Ljava/lang/String;", {}});
                (*registers)[0] = reference;
                return true;
            }
            if (backend_ == nullptr &&
                class_type ==
                    "com/king/usdk/popupwebview/internal/PopupFactory" &&
                method->second.name == "initFactory" &&
                signature ==
                    "(Landroid/app/Activity;)Lcom/king/usdk/popupwebview/internal/PopupFactory;") {
                std::uint64_t activity = 0;
                if (!ReadGuestGeneralArgument(address_space_,
                                              (*registers)[3], &activity,
                                              call_error))
                    return false;
                const std::optional<std::string_view> activity_type =
                    ObjectTypeLocked(activity);
                if (!activity_type ||
                    !activity_type->ends_with("Activity;")) {
                    SetError(call_error,
                             "guest JNI CallStaticObjectMethodV PopupFactory "
                             "activity is invalid");
                    return false;
                }
                const std::string return_type = ReturnType(signature);
                const std::uint64_t reference =
                    AllocateCompatibilityReference(
                        heap_, CompatibilityReferenceKind::kObject,
                        return_type, {}, call_error);
                if (reference == 0) {
                    SetError(call_error,
                             "guest JNI PopupFactory object allocation "
                             "failed");
                    return false;
                }
                object_references_.emplace(
                    reference,
                    ObjectReference{reference, (*registers)[2],
                                    return_type, {}, 0});
                TrackLocalReferenceLocked(reference);
                (*registers)[0] = reference;
                return true;
            }
            if (backend_ == nullptr &&
                class_type ==
                    "com/king/usdk/braze/BrazeAndroidModule$ForcedIGPOrientation" &&
                method->second.name == "valueOf" &&
                signature ==
                    "(Ljava/lang/String;)Lcom/king/usdk/braze/BrazeAndroidModule$ForcedIGPOrientation;") {
                std::uint64_t name_reference = 0;
                if (!ReadGuestGeneralArgument(address_space_,
                                              (*registers)[3],
                                              &name_reference, call_error))
                    return false;
                const std::optional<std::string_view> name_type =
                    ObjectTypeLocked(name_reference);
                std::optional<std::string_view> enum_name;
                const auto local_name =
                    FindOwnedLocalObjectReferenceLocked(name_reference);
                if (local_name != object_references_.end())
                    enum_name = local_name->second.string_value;
                else {
                    const auto global_name =
                        global_object_references_.find(name_reference);
                    if (global_name != global_object_references_.end())
                        enum_name = global_name->second.string_value;
                }
                if (!name_type || !IsStringTypeName(*name_type) ||
                    !enum_name ||
                    (*enum_name != "NONE" && *enum_name != "PORTRAIT" &&
                     *enum_name != "LANDSCAPE")) {
                    SetError(call_error,
                             "guest JNI CallStaticObjectMethodV Braze orientation name is invalid");
                    return false;
                }
                const std::string return_type =
                    ReturnType(method->second.signature);
                const std::uint64_t reference =
                    AllocateCompatibilityReference(
                        heap_, CompatibilityReferenceKind::kObject,
                        return_type, *enum_name, call_error);
                if (reference == 0) {
                    SetError(call_error,
                             "guest JNI Braze orientation object allocation failed");
                    return false;
                }
                object_references_.emplace(
                    reference,
                    ObjectReference{reference, (*registers)[2], return_type,
                                    std::string(*enum_name), 0});
                TrackLocalReferenceLocked(reference);
                (*registers)[0] = reference;
                return true;
            }
            const std::size_t close = signature.find(')');
            if (backend_ == nullptr &&
                class_type &&
                NormalizeCompatibilityType(*class_type) ==
                    "org/cocos2dx/lib/Cocos2dxDownloader" &&
                method->second.name == "createDownloader" &&
                signature ==
                    "(IILjava/lang/String;I)"
                    "Lorg/cocos2dx/lib/Cocos2dxDownloader;") {
                // The compat runtime has no HTTP download transport; hand
                // back a compat downloader handle. No tasks will be enqueued
                // through it, so no progress/finish callbacks can fire.
                const std::uint64_t reference = AllocateCompatibilityReference(
                    heap_, CompatibilityReferenceKind::kObject,
                    "Lorg/cocos2dx/lib/Cocos2dxDownloader;", {}, call_error);
                if (reference == 0) {
                    SetError(call_error,
                             "guest downloader allocation failed");
                    return false;
                }
                object_references_.emplace(
                    reference,
                    ObjectReference{reference, 0,
                                    "Lorg/cocos2dx/lib/Cocos2dxDownloader;",
                                    {}, 0});
                TrackLocalReferenceLocked(reference);
                (*registers)[0] = reference;
                return true;
            }
            if (backend_ == nullptr &&
                class_type &&
                NormalizeCompatibilityType(*class_type) ==
                    "com/unity3d/player/ReflectionHelper" &&
                method->second.name == "newProxyInstance" &&
                signature ==
                    "(Lcom/unity3d/player/UnityPlayer;JLjava/lang/Class;)"
                    "Ljava/lang/Object;") {
                // Unity's ReflectionHelper proxy factory is a single-interface
                // variant of JNIBridge.newInterfaceProxy: argument 1 is the
                // native invocation handle and argument 2 is the interface
                // Class. Returning null leaves the managed caller holding a
                // dead proxy (observed as a NullReferenceException in CTR's
                // BroadcastReceiver registration path).
                std::uint64_t native_handle = 0;
                std::uint64_t interface_reference = 0;
                if (!ReadGuestGeneralArgument(address_space_, (*registers)[3],
                                              &native_handle, call_error, 1) ||
                    !ReadGuestGeneralArgument(address_space_, (*registers)[3],
                                              &interface_reference, call_error,
                                              2)) {
                    SetError(call_error,
                             "guest JNI ReflectionHelper proxy arguments are unreadable");
                    return false;
                }
                const std::optional<std::string_view> interface_type =
                    ClassTypeLocked(interface_reference);
                std::string interface_name;
                if (interface_type) interface_name.assign(*interface_type);
                if (native_handle == 0 || interface_name.empty()) {
                    std::fprintf(stderr,
                                 "ISOMR_PROXY_MISS handle=%llx ifref=%llx "
                                 "iface=%s\n",
                                 (unsigned long long)native_handle,
                                 (unsigned long long)interface_reference,
                                 interface_name.c_str());
                    if (call_error != nullptr)
                        *call_error =
                            "trace:jni-proxy-miss-" + std::to_string(
                                (unsigned long long)interface_reference);
                    (*registers)[0] = 0;
                    return true;
                }
                if (!interface_name.starts_with('[') &&
                    !interface_name.starts_with('L'))
                    interface_name = "L" + interface_name + ";";
                const std::uint64_t reference = AllocateCompatibilityReference(
                    heap_, CompatibilityReferenceKind::kObject, interface_name,
                    {}, call_error);
                if (reference == 0) return false;
                object_references_.emplace(
                    reference,
                    ObjectReference{reference, (*registers)[2], interface_name,
                                    {}, 0});
                TrackLocalReferenceLocked(reference);
                interface_proxies_.emplace(
                    reference,
                    InterfaceProxyStorage{native_handle, {interface_name}});
                (*registers)[0] = reference;
                return true;
            }
            if (backend_ == nullptr &&
                NormalizeCompatibilityType(method->second.class_type) ==
                    "com/google/android/ump/UserMessagingPlatform" &&
                method->second.name == "getConsentInformation" &&
                (signature.ends_with(
                     ")Lcom/google/android/ump/ConsentInformation;") ||
                 signature.ends_with(")Ljava/lang/Object;"))) {
                // Google UMP consent flow: returning null makes the game
                // dereference ConsentInformation and die inside its consent
                // coroutine. Hand back a compat object whose status getters
                // report consent already obtained.
                const std::uint64_t reference = AllocateCompatibilityReference(
                    heap_, CompatibilityReferenceKind::kObject,
                    "Lcom/google/android/ump/ConsentInformation;", {},
                    call_error);
                if (reference == 0) return false;
                object_references_.emplace(
                    reference,
                    ObjectReference{reference, (*registers)[2],
                                    "Lcom/google/android/ump/ConsentInformation;",
                                    {}, 0});
                TrackLocalReferenceLocked(reference);
                (*registers)[0] = reference;
                return true;
            }
            if (backend_ == nullptr &&
                NormalizeCompatibilityType(method->second.class_type) ==
                    "com/google/android/gms/ads/identifier/"
                    "AdvertisingIdClient" &&
                method->second.name == "getAdvertisingIdInfo") {
                // GMS advertising-id lookup: returning null makes the caller
                // dereference Info.getId() and die. On devices without Play
                // services this call throws IOException; games broadly catch
                // that, but a non-null Info keeps the managed path alive.
                const std::uint64_t reference = AllocateCompatibilityReference(
                    heap_, CompatibilityReferenceKind::kObject,
                    "Lcom/google/android/gms/ads/identifier/"
                    "AdvertisingIdClient$Info;",
                    {}, call_error);
                if (reference == 0) return false;
                object_references_.emplace(
                    reference,
                    ObjectReference{
                        reference, (*registers)[2],
                        "Lcom/google/android/gms/ads/identifier/"
                        "AdvertisingIdClient$Info;",
                        {}, 0});
                TrackLocalReferenceLocked(reference);
                (*registers)[0] = reference;
                return true;
            }
            if (backend_ == nullptr &&
                method->second.class_type == "android/view/MotionEvent" &&
                method->second.name == "obtain") {
                // Unity's nativeInjectEvent copies an injected event with
                // MotionEvent.obtain(src) before queueing it for the input
                // system. The generic arg-bearing stub returns null, so the
                // copied touch is dropped and the splash's tap gate never
                // opens. Clone the stored action/coords into a fresh event so
                // the delivered copy reads back the same touch.
                std::uint64_t source = 0;
                MotionEventStorage storage{};
                if (signature.starts_with("(Landroid/view/MotionEvent;") &&
                    ReadGuestGeneralArgument(address_space_,
                                             (*registers)[3], &source,
                                             call_error, 0)) {
                    const auto found = motion_events_.find(
                        ReferenceIdentityLocked(source).value_or(source));
                    if (found != motion_events_.end()) storage = found->second;
                }
                const std::uint64_t reference = AllocateCompatibilityReference(
                    heap_, CompatibilityReferenceKind::kObject,
                    "android/view/MotionEvent", {}, call_error);
                if (reference == 0) return false;
                object_references_.emplace(
                    reference,
                    ObjectReference{reference, (*registers)[2],
                                    "android/view/MotionEvent", {}, 0});
                motion_events_.emplace(reference, storage);
                TrackLocalReferenceLocked(reference);
                (*registers)[0] = reference;
                return true;
            }
            if (backend_ == nullptr &&
                method->second.class_type == "android/view/InputDevice" &&
                method->second.name == "getDeviceIds" &&
                signature == "()[I") {
                // Unity polls InputDevice.getDeviceIds every frame to discover
                // the touch source. The generic stub returned an empty array,
                // so no touchscreen registered and injected MotionEvents were
                // dropped unread (the splash's tap gate stayed shut). Report a
                // single synthetic touchscreen device id. The lock is already
                // held here, so populate the array inline (the public helper
                // would relock and deadlock).
                const std::uint64_t reference = AllocateCompatibilityReference(
                    heap_, CompatibilityReferenceKind::kObject, "[I", {},
                    call_error);
                if (reference == 0) {
                    SetError(call_error,
                             "compatibility int array allocation failed");
                    return false;
                }
                object_references_.emplace(
                    reference,
                    ObjectReference{reference, 0, "[I", {}, 0});
                int_arrays_.emplace(reference,
                                    std::vector<std::uint32_t>{
                                        static_cast<std::uint32_t>(
                                            kArmrTouchDeviceId)});
                TrackLocalReferenceLocked(reference);
                (*registers)[0] = reference;
                return true;
            }
            if (backend_ == nullptr &&
                method->second.class_type == "android/view/InputDevice" &&
                method->second.name == "getDevice") {
                // InputDevice.getDevice(id): hand back a touchscreen object so
                // the caller's getSources()/isVirtual()/getMotionRange() reads
                // classify it as a real touch device.
                std::uint64_t device_id = kArmrTouchDeviceId;
                ReadGuestGeneralArgument(address_space_, (*registers)[3],
                                         &device_id, call_error, 0);
                const std::uint64_t reference = AllocateCompatibilityReference(
                    heap_, CompatibilityReferenceKind::kObject,
                    "android/view/InputDevice", {}, call_error);
                if (reference == 0) return false;
                object_references_.emplace(
                    reference,
                    ObjectReference{reference, (*registers)[2],
                                    "android/view/InputDevice", {}, 0});
                input_devices_.emplace(reference, device_id);
                TrackLocalReferenceLocked(reference);
                (*registers)[0] = reference;
                return true;
            }
            if (ApplyCompatOverride("static-object",
                                    method->second.class_type,
                                    method->second.name,
                                    method->second.signature, registers,
                                    call_error))
                return true;
            // A string-valued override satisfies static object calls whose
            // advertised return type is java/lang/String (paths, names,
            // identifiers the engine dereferences immediately).
            if (signature.ends_with(")Ljava/lang/String;") &&
                ApplyCompatOverride("static-string",
                                    method->second.class_type,
                                    method->second.name,
                                    method->second.signature, registers,
                                    call_error))
                return true;
            if (!signature.starts_with("()") || close == std::string::npos ||
                close + 1 >= signature.size() ||
                (signature[close + 1] != 'L' &&
                 signature[close + 1] != '[')) {
                if (backend_ == nullptr) {
                    // No Java backend runs the target; an unimplemented
                    // static object method with arguments returns the JNI
                    // null default rather than aborting the caller.
                    if (call_error != nullptr)
                        *call_error =
                            "trace:jni-call-static-object-stub-" +
                            method->second.class_type + "." +
                            method->second.name + method->second.signature;
                    (*registers)[0] = 0;
                    return true;
                }
                SetError(call_error,
                         "guest JNI CallStaticObjectMethodV class " +
                             std::string(class_type.value_or("<unknown>")) +
                             " method " + method->second.name +
                             " signature " + signature +
                             " is not yet supported");
                return false;
            }
            GuestJniBackend::Handle backend_reference = 0;
            if (backend_ != nullptr) {
                const std::optional<GuestJniBackend::Handle> backend_class =
                    BackendReferenceLocked((*registers)[1]);
                if (!backend_class ||
                    !backend_->CallObjectMethod(*backend_class,
                                                method->second.backend,
                                                true, &backend_reference,
                                                call_error))
                    return false;
                if (backend_reference == 0) {
                    (*registers)[0] = 0;
                    return true;
                }
            }
            const std::string return_type =
                ReturnType(method->second.signature);
            const CompatibilityReferenceKind kind =
                return_type == "Ljava/lang/String;"
                    ? CompatibilityReferenceKind::kString
                    : CompatibilityReferenceKind::kObject;
            const std::uint64_t reference = AllocateJniReference(
                heap_, backend_ == nullptr, kind, return_type, {}, call_error);
            if (reference == 0) {
                if (backend_reference != 0) {
                    std::string ignored;
                    (void)backend_->DeleteLocalRef(backend_reference,
                                                   &ignored);
                }
                SetError(call_error,
                         "guest JNI object reference allocation failed");
                return false;
            }
            object_references_.emplace(
                reference,
                ObjectReference{reference, (*registers)[2], return_type, {},
                                backend_reference});
            EnsureStubArrayStorageLocked(reference, return_type);
            TrackLocalReferenceLocked(reference);
            (*registers)[0] = reference;
            return true;
        };
    const auto call_static_long_method_v =
        [this](Aarch64ImportRegistry::Registers* registers,
               std::string* call_error) {
            if (registers == nullptr ||
                !IsCurrentEnvironment((*registers)[0]) ||
                (*registers)[3] == 0) {
                SetError(call_error,
                         "guest JNI CallStaticLongMethodV arguments are invalid");
                return false;
            }
            std::array<std::uint8_t, 32> arguments{};
            if (!address_space_->Read((*registers)[3], arguments.data(),
                                      arguments.size())) {
                SetError(call_error,
                         "guest JNI CallStaticLongMethodV va_list is invalid");
                return false;
            }
            const std::lock_guard lock(mutex_);
            const auto method_pre = method_references_.find((*registers)[2]);
            if (method_pre != method_references_.end() &&
                method_pre->second.name == "getHasCrashReport" &&
                method_pre->second.signature == "()Z") {
                // No crash report exists in the compat runtime; the boot
                // checks this before proceeding to the surface pipeline.
                (*registers)[0] = 0;
                return true;
            }
            const std::optional<std::uint64_t> class_identity =
                ReferenceIdentityLocked((*registers)[1]);
            const auto method = method_references_.find((*registers)[2]);
            // A static method belongs to a class type, not a jclass
            // instance — a fresh FindClass of the same class carries a new
            // identity, so match on class_type like CallStaticObjectMethodV.
            const std::optional<std::string_view> call_class_type =
                ClassTypeLocked((*registers)[1]);
            const bool class_matches =
                method != method_references_.end() && class_identity &&
                (method->second.class_identity == *class_identity ||
                 (call_class_type &&
                  ReceiverClassMatches(method->second.class_type,
                                       *call_class_type)));
            if (!class_identity || !IsClassReferenceLocked((*registers)[1]) ||
                method == method_references_.end() || !class_matches ||
                !method->second.is_static) {
                if (backend_ == nullptr) {
                    std::fprintf(stderr,
                                 "ISOMR_JNI_SLONG_STUB mid=%llx\n",
                                 (unsigned long long)(*registers)[2]);
                    std::fflush(stderr);
                    (*registers)[0] = 0;
                    return true;
                }
                SetError(call_error,
                         "guest JNI CallStaticLongMethodV provenance is invalid");
                return false;
            }
            const std::optional<std::string_view> class_type =
                ClassTypeLocked((*registers)[1]);
            if (class_type ==
                    "com/king/usdk/adinfo/AppInstallTimeProvider" &&
                (method->second.name == "getInstallTimeMsts" ||
                 method->second.name == "getUpdateTimeMsts") &&
                method->second.signature == "(Landroid/app/Activity;)J") {
                if (backend_ != nullptr) {
                    SetError(call_error,
                             "guest JNI CallStaticLongMethodV app install "
                             "time query is unavailable with an active backend");
                    return false;
                }
                std::uint64_t activity = 0;
                if (!ReadGuestGeneralArgument(address_space_,
                                              (*registers)[3], &activity,
                                              call_error))
                    return false;
                const std::optional<std::string_view> activity_type =
                    ObjectTypeLocked(activity);
                if (!activity_type ||
                    !activity_type->ends_with("Activity;")) {
                    SetError(call_error,
                             "guest JNI CallStaticLongMethodV app install "
                             "time activity is invalid");
                    return false;
                }
                // There is no Android package manager timestamp on the native
                // Windows profile. Zero means unavailable and does not invent
                // user-install metadata.
                (*registers)[0] = 0;
                return true;
            }
            if (class_type == "android/os/Debug" &&
                method->second.name == "getPss" &&
                method->second.signature == "()J") {
                if (backend_ != nullptr) {
                    SetError(call_error,
                             "guest JNI CallStaticLongMethodV Debug "
                             "getPss query is unavailable with an active "
                             "backend");
                    return false;
                }
                // The Windows host does not implement Android PSS accounting.
                // Zero is the nonnegative kilobyte contract without inventing
                // a memory measurement.
                (*registers)[0] = 0;
                return true;
            }
            // Real wall/monotonic time for the Java-side clocks the game
            // engine and Choreographer read. Without these every time-based
            // transition (splash timeouts, Handler delays, animation deltas)
            // sees elapsed time frozen at zero and never fires.
            if (method->second.signature == "()J") {
                const std::string_view clock_name = method->second.name;
                std::int64_t clock_result = -1;
                if (class_type == "java/lang/System" &&
                    clock_name == "nanoTime") {
                    clock_result = std::chrono::duration_cast<
                        std::chrono::nanoseconds>(
                            std::chrono::steady_clock::now()
                                .time_since_epoch())
                        .count();
                } else if (class_type == "java/lang/System" &&
                           clock_name == "currentTimeMillis") {
                    clock_result = std::chrono::duration_cast<
                        std::chrono::milliseconds>(
                            std::chrono::system_clock::now()
                                .time_since_epoch())
                        .count();
                } else if (class_type == "android/os/SystemClock" &&
                           (clock_name == "uptimeMillis" ||
                            clock_name == "elapsedRealtime" ||
                            clock_name == "elapsedRealtimeNanos" ||
                            clock_name == "currentThreadTimeMillis" ||
                            clock_name == "elapsedRealtimeCpu")) {
                    const auto steady_now =
                        std::chrono::steady_clock::now().time_since_epoch();
                    clock_result =
                        (clock_name == "elapsedRealtimeNanos")
                            ? std::chrono::duration_cast<
                                  std::chrono::nanoseconds>(steady_now).count()
                            : std::chrono::duration_cast<
                                  std::chrono::milliseconds>(steady_now)
                                  .count();
                }
                if (clock_result >= 0) {
                    (*registers)[0] =
                        static_cast<std::uint64_t>(clock_result);
                    return true;
                }
            }
            if (ApplyCompatOverride("static-long",
                                    method->second.class_type,
                                    method->second.name,
                                    method->second.signature, registers,
                                    call_error))
                return true;
            if (backend_ == nullptr) {
                if (call_error != nullptr)
                    *call_error = "trace:jni-call-static-long-stub-" +
                                  method->second.class_type + "." +
                                  method->second.name + method->second.signature;
                (*registers)[0] = 0;
                return true;
            }
            SetError(call_error,
                     "guest JNI CallStaticLongMethodV class " +
                         std::string(class_type.value_or("<unknown>")) +
                         " method " + method->second.name + " signature " +
                         method->second.signature +
                         " is not yet supported");
            return false;
        };
    const auto call_static_int_method_v =
        [this](Aarch64ImportRegistry::Registers* registers,
               std::string* call_error) {
            if (registers == nullptr ||
                !IsCurrentEnvironment((*registers)[0]) ||
                (*registers)[3] == 0) {
                SetError(call_error,
                         "guest JNI CallStaticIntMethodV arguments are invalid");
                return false;
            }
            std::array<std::uint8_t, 32> arguments{};
            if (!address_space_->Read((*registers)[3], arguments.data(),
                                      arguments.size())) {
                SetError(call_error,
                         "guest JNI CallStaticIntMethodV va_list is invalid");
                return false;
            }
            const std::lock_guard lock(mutex_);
            const std::optional<std::uint64_t> class_identity =
                ReferenceIdentityLocked((*registers)[1]);
            const auto method = method_references_.find((*registers)[2]);
            // A static method belongs to a class type, not a jclass
            // instance — a fresh FindClass of the same class carries a new
            // identity, so match on class_type like CallStaticObjectMethodV.
            const std::optional<std::string_view> call_class_type =
                ClassTypeLocked((*registers)[1]);
            const bool class_matches =
                method != method_references_.end() && class_identity &&
                (method->second.class_identity == *class_identity ||
                 (call_class_type &&
                  ReceiverClassMatches(method->second.class_type,
                                       *call_class_type)));
            if (!class_identity || !IsClassReferenceLocked((*registers)[1]) ||
                method == method_references_.end() || !class_matches ||
                !method->second.is_static) {
                if (backend_ == nullptr) {
                    std::fprintf(stderr,
                                 "ISOMR_JNI_SINT_STUB mid=%llx\n",
                                 (unsigned long long)(*registers)[2]);
                    std::fflush(stderr);
                    (*registers)[0] = 0;
                    return true;
                }
                SetError(call_error,
                         "guest JNI CallStaticIntMethodV provenance is invalid");
                return false;
            }
            const std::optional<std::string_view> class_type =
                ClassTypeLocked((*registers)[1]);
            if (backend_ == nullptr &&
                class_type &&
                NormalizeCompatibilityType(*class_type) ==
                    "com/ea/EAMIO/StorageDirectory" &&
                method->second.name == "GetPrimaryExternalStorageState" &&
                method->second.signature == "()I") {
                // Report the primary external storage as mounted (state 1)
                // so the engine uses the configured /data storage root.
                (*registers)[0] = 1;
                return true;
            }
            if (backend_ == nullptr &&
                class_type &&
                NormalizeCompatibilityType(*class_type) ==
                    "csdk/glucentralservices/util/AndroidPlatform" &&
                method->second.name == "getDeviceTier" &&
                method->second.signature == "()I") {
                // A desktop GPU outranks every mobile tier; report the
                // highest device tier so the engine picks full quality.
                (*registers)[0] = 3;
                return true;
            }
            if (backend_ == nullptr &&
                class_type &&
                NormalizeCompatibilityType(*class_type) ==
                    "com/ea/game/pvzfree_row/PvZActivity" &&
                method->second.is_static &&
                (method->second.name == "getSafeInsetLeft" ||
                 method->second.name == "getSafeInsetTop" ||
                 method->second.name == "getSafeInsetRight" ||
                 method->second.name == "getSafeInsetBottom") &&
                method->second.signature == "()I") {
                // The compat window has no cutout insets; report zero.
                (*registers)[0] = 0;
                return true;
            }
            if (class_type == "com/king/device/DeviceImpl" &&
                method->second.name == "getNetworkTypeCached" &&
                method->second.signature == "()I") {
                if (backend_ != nullptr) {
                    SetError(call_error,
                             "guest JNI CallStaticIntMethodV DeviceImpl network query is unavailable with an active backend");
                    return false;
                }
                // This is the profile's cached native connectivity category,
                // not a claim that an Android network service is present.
                // The historical compatibility contract uses category 1.
                (*registers)[0] = 1;
                return true;
            }
            if (ApplyCompatOverride("static-int",
                                    method->second.class_type,
                                    method->second.name,
                                    method->second.signature, registers,
                                    call_error))
                return true;
            if (backend_ == nullptr) {
                if (call_error != nullptr)
                    *call_error = "trace:jni-call-static-int-stub-" +
                                  method->second.class_type + "." +
                                  method->second.name + method->second.signature;
                (*registers)[0] = 0;
                return true;
            }
            SetError(call_error,
                     "guest JNI CallStaticIntMethodV class " +
                         std::string(class_type.value_or("<unknown>")) +
                         " method " + method->second.name + " signature " +
                         method->second.signature +
                         " is not yet supported");
            return false;
        };
    const auto call_static_void_method_v =
        [this](Aarch64ImportRegistry::Registers* registers,
               std::string* call_error) {
            if (registers == nullptr ||
                !IsCurrentEnvironment((*registers)[0]) ||
                (*registers)[3] == 0) {
                SetError(call_error,
                         "guest JNI CallStaticVoidMethodV arguments are invalid");
                return false;
            }
            std::array<std::uint8_t, 32> arguments{};
            if (!address_space_->Read((*registers)[3], arguments.data(),
                                      arguments.size())) {
                SetError(call_error,
                         "guest JNI CallStaticVoidMethodV va_list is invalid");
                return false;
            }
            const std::lock_guard lock(mutex_);
            if (!IsClassReferenceLocked((*registers)[1])) {
                SetError(call_error,
                         "guest JNI CallStaticVoidMethodV class is invalid");
                return false;
            }
            const auto method = method_references_.find((*registers)[2]);
            const std::optional<std::uint64_t> class_identity =
                ReferenceIdentityLocked((*registers)[1]);
            // A static method belongs to a class type, not a jclass
            // instance — a fresh FindClass of the same class carries a new
            // identity, so match on class_type like CallStaticObjectMethodV.
            const std::optional<std::string_view> call_class_type =
                ClassTypeLocked((*registers)[1]);
            const bool class_matches =
                method != method_references_.end() && class_identity &&
                (method->second.class_identity == *class_identity ||
                 (call_class_type &&
                  ReceiverClassMatches(method->second.class_type,
                                       *call_class_type)));
            if (method == method_references_.end() || !class_identity ||
                !class_matches || !method->second.is_static) {
                if (backend_ == nullptr) {
                    std::fprintf(stderr,
                                 "ISOMR_JNI_SVOID_STUB mid=%llx\n",
                                 (unsigned long long)(*registers)[2]);
                    std::fflush(stderr);
                    (*registers)[0] = 0;
                    return true;
                }
                SetError(call_error,
                         "guest JNI CallStaticVoidMethodV method is invalid");
                return false;
            }
            const std::optional<std::string_view> class_type =
                ClassTypeLocked((*registers)[1]);
            if (backend_ == nullptr &&
                class_type &&
                NormalizeCompatibilityType(*class_type) ==
                    "com/ea/game/pvzfree_row/PvZActivity" &&
                method->second.is_static &&
                method->second.signature.ends_with(")V")) {
                // The compat PvZActivity is an inert handle: static UI
                // flags (SetImpressumRequired and friends) have no surface
                // to reach, so acknowledge and drop.
                return true;
            }
            if (backend_ == nullptr &&
                class_type &&
                ReceiverClassMatches(*class_type,
                                     "com/ea/nimble/Utility") &&
                method->second.name == "sendBroadcastSerializable" &&
                method->second.signature ==
                    "(Ljava/lang/String;Ljava/util/Map;)V") {
                // Nimble broadcasts target Android's broadcast transport,
                // which the compat runtime does not run; acknowledge and
                // drop (no receiver will observe the missing broadcast).
                return true;
            }
            if (backend_ == nullptr &&
                class_type &&
                NormalizeCompatibilityType(*class_type) ==
                    "com/ea/EAThread/EAThread" &&
                method->second.name == "setCurrentThreadName" &&
                method->second.signature == "(Ljava/lang/String;)V") {
                // Thread naming is diagnostic-only on device; the compat
                // runtime has no OS thread-name surface to update.
                return true;
            }
            if (backend_ == nullptr &&
                class_type &&
                NormalizeCompatibilityType(*class_type) ==
                    "com/ea/game/pvzfree_row/AppShortcutUtil" &&
                (method->second.name == "CreateShortcut" ||
                 method->second.name == "ApplyShortcuts") &&
                method->second.signature.ends_with("V")) {
                // Desktop shortcuts do not exist in the compat runtime; the
                // request is acknowledged and dropped.
                return true;
            }
            if (backend_ == nullptr &&
                class_type &&
                NormalizeCompatibilityType(*class_type) ==
                    "org/cocos2dx/lib/Cocos2dxDownloader" &&
                method->second.name == "createTask" &&
                method->second.signature.ends_with("V")) {
                // The compat runtime has no HTTP transport; download tasks
                // are acknowledged and dropped (no progress/finish events).
                return true;
            }
            if (backend_ == nullptr &&
                class_type &&
                NormalizeCompatibilityType(*class_type) ==
                    "com/ea/game/pvzfree_row/CrashlyticsBridge" &&
                method->second.signature.ends_with("V")) {
                // Crashlytics telemetry has no native-Windows transport;
                // keys, logs, and user data are acknowledged and dropped.
                return true;
            }
            if (backend_ == nullptr &&
                class_type &&
                NormalizeCompatibilityType(*class_type) ==
                    "com.ea.nimble.Utility" &&
                method->second.name == "registerReceiver" &&
                method->second.signature ==
                    "(Ljava/lang/String;Landroid/content/BroadcastReceiver;)V") {
                // Nimble registers broadcast receivers for connectivity and
                // battery events; the compat runtime dispatches none.
                return true;
            }
            if (backend_ == nullptr &&
                class_type &&
                NormalizeCompatibilityType(*class_type) ==
                    "com/ea/EAAudioCore/AndroidEAAudioCore" &&
                method->second.name == "Startup" &&
                method->second.signature == "()V") {
                // The Java audio bridge has no native-Windows transport; the
                // OpenSL ES shim already discards output. Audio startup is
                // acknowledged so the engine proceeds.
                return true;
            }
            if (class_type == "android/os/Process" &&
                method->second.name == "setThreadPriority" &&
                method->second.signature == "(II)V") {
                if (backend_ != nullptr) {
                    SetError(call_error,
                             "guest JNI CallStaticVoidMethodV Android Process thread-priority update is unavailable with an active backend");
                    return false;
                }
                std::uint64_t thread_argument = 0;
                std::uint64_t priority_argument = 0;
                if (!ReadGuestGeneralArgument(address_space_,
                                              (*registers)[3],
                                              &thread_argument, call_error) ||
                    !ReadGuestGeneralArgument(address_space_,
                                              (*registers)[3],
                                              &priority_argument,
                                              call_error, 1))
                    return false;
                const std::int32_t thread_id =
                    static_cast<std::int32_t>(
                        static_cast<std::uint32_t>(thread_argument));
                const std::int32_t android_priority =
                    static_cast<std::int32_t>(
                        static_cast<std::uint32_t>(priority_argument));
                if (thread_id <= 0 || thread_state_ == nullptr) {
                    SetError(call_error,
                             "guest JNI Android Process thread priority target " +
                                 std::to_string(thread_id) +
                                 " is invalid or unavailable (priority " +
                                 std::to_string(android_priority) + ")");
                    return false;
                }
                if (android_priority < -20 || android_priority > 19) {
                    SetError(call_error,
                             "guest JNI Android Process thread priority is outside the Android nice range");
                    return false;
                }
                std::string priority_error;
                if (!thread_state_->SetGuestThreadNice(
                        static_cast<std::uint32_t>(thread_id),
                        android_priority, &priority_error)) {
                    SetError(call_error,
                             "guest JNI Android Process thread priority update failed: " +
                                 priority_error);
                    return false;
                }
                (*registers)[0] = 0;
                return true;
            }
            if (class_type == "com/king/core/UncaughtExceptionWriter" &&
                method->second.name == "enable" &&
                method->second.signature == "()V") {
                if (backend_ != nullptr) {
                    SetError(call_error,
                             "guest JNI CallStaticVoidMethodV UncaughtExceptionWriter enable is unavailable with an active backend");
                    return false;
                }
                (*registers)[0] = 0;
                return true;
            }
            if (class_type == "com/king/core/GameLib" &&
                (method->second.name ==
                     "updateAllowedScreenOrientations" ||
                 method->second.name ==
                     "initAllowedScreenOrientations") &&
                method->second.signature == "(ZZZZ)V") {
                if (backend_ != nullptr) {
                    SetError(call_error,
                             "guest JNI CallStaticVoidMethodV GameLib orientation update is unavailable with an active backend");
                    return false;
                }
                std::array<std::uint8_t, 4> orientations{};
                for (std::size_t index = 0; index < orientations.size();
                     ++index) {
                    std::uint64_t argument = 0;
                    if (!ReadGuestGeneralArgument(
                            address_space_, (*registers)[3], &argument,
                            call_error, index))
                        return false;
                    if (argument > 1) {
                        SetError(call_error,
                                 "guest JNI CallStaticVoidMethodV GameLib orientation argument is not boolean");
                        return false;
                    }
                    orientations[index] =
                        static_cast<std::uint8_t>(argument);
                }
                allowed_screen_orientations_ = orientations;
                (*registers)[0] = 0;
                return true;
            }
            if (class_type == "com/king/core/GameLib" &&
                method->second.name == "setupPlatform" &&
                method->second.signature == "(IIIII)V") {
                if (backend_ != nullptr) {
                    SetError(call_error,
                             "guest JNI CallStaticVoidMethodV GameLib platform setup is unavailable with an active backend");
                    return false;
                }
                std::array<std::int32_t, 5> setup{};
                for (std::size_t index = 0; index < setup.size(); ++index) {
                    std::uint64_t argument = 0;
                    if (!ReadGuestGeneralArgument(
                            address_space_, (*registers)[3], &argument,
                            call_error, index))
                        return false;
                    setup[index] = static_cast<std::int32_t>(
                        static_cast<std::uint32_t>(argument));
                }
                platform_setup_ = setup;
                (*registers)[0] = 0;
                return true;
            }
            if (class_type ==
                    "com/ff/basesdk/activity/bridge/FFBaseSdkActivityBridge" &&
                method->second.name == "init" &&
                method->second.signature == "()V") {
                if (backend_ != nullptr) {
                    SetError(call_error,
                             "guest JNI CallStaticVoidMethodV FFBaseSdkActivityBridge init is unavailable with an active backend");
                    return false;
                }
                ff_base_sdk_initialized_ = true;
                (*registers)[0] = 0;
                return true;
            }
            if (class_type == "com/king/usdk/adinfo/AdvertisingId" &&
                method->second.name == "registerNativePtr" &&
                method->second.signature == "(J)V") {
                if (backend_ != nullptr) {
                    SetError(call_error,
                             "guest JNI CallStaticVoidMethodV AdvertisingId "
                             "registration is unavailable with an active "
                             "backend");
                    return false;
                }
                std::uint64_t native_pointer = 0;
                if (!ReadGuestGeneralArgument(address_space_,
                                              (*registers)[3],
                                              &native_pointer, call_error))
                    return false;
                advertising_id_native_pointer_ = native_pointer;
                (*registers)[0] = 0;
                return true;
            }
            if (class_type == "com/king/usdk/adinfo/AdvertisingId" &&
                method->second.name == "unregisterNativePtr" &&
                method->second.signature == "()V") {
                if (backend_ != nullptr) {
                    SetError(call_error,
                             "guest JNI CallStaticVoidMethodV AdvertisingId "
                             "unregistration is unavailable with an active "
                             "backend");
                    return false;
                }
                advertising_id_native_pointer_.reset();
                (*registers)[0] = 0;
                return true;
            }
            if (class_type == "com/king/usdk/adinfo/AdvertisingId" &&
                (method->second.name == "requestAdvertisingInfo" ||
                 method->second.name == "requestAppSetId") &&
                method->second.signature == "(Landroid/app/Activity;)V") {
                if (backend_ != nullptr) {
                    SetError(call_error,
                             "guest JNI CallStaticVoidMethodV AdvertisingId "
                             "request is unavailable with an active backend");
                    return false;
                }
                std::uint64_t activity = 0;
                if (!ReadGuestGeneralArgument(address_space_,
                                              (*registers)[3], &activity,
                                              call_error))
                    return false;
                const std::optional<std::string_view> activity_type =
                    ObjectTypeLocked(activity);
                if (!activity_type || !activity_type->ends_with("Activity;")) {
                    SetError(call_error,
                             "guest JNI CallStaticVoidMethodV AdvertisingId "
                             "activity is invalid");
                    return false;
                }
                advertising_info_requested_ = true;
                (*registers)[0] = 0;
                return true;
            }
            if (class_type == "com/king/usdk/kdid/AnalyticsReceiver" &&
                method->second.name == "getValue" &&
                method->second.signature ==
                    "(Ljava/lang/String;Landroid/app/Activity;)V") {
                if (backend_ != nullptr) {
                    SetError(call_error,
                             "guest JNI CallStaticVoidMethodV "
                             "AnalyticsReceiver request is unavailable with "
                             "an active backend");
                    return false;
                }
                std::uint64_t key = 0;
                std::uint64_t activity = 0;
                if (!ReadGuestGeneralArgument(address_space_,
                                              (*registers)[3], &key,
                                              call_error) ||
                    !ReadGuestGeneralArgument(address_space_,
                                              (*registers)[3], &activity,
                                              call_error, 1))
                    return false;
                const auto local_key =
                    FindOwnedLocalObjectReferenceLocked(key);
                const auto global_key = global_object_references_.find(key);
                if ((local_key == object_references_.end() &&
                     global_key == global_object_references_.end()) ||
                    (local_key != object_references_.end()
                         ? local_key->second.type
                         : global_key->second.type) !=
                        "Ljava/lang/String;") {
                    SetError(call_error,
                             "guest JNI CallStaticVoidMethodV "
                             "AnalyticsReceiver key is not a string "
                             "reference");
                    return false;
                }
                const std::optional<std::string_view> activity_type =
                    ObjectTypeLocked(activity);
                if (!activity_type ||
                    !activity_type->ends_with("Activity;")) {
                    SetError(call_error,
                             "guest JNI CallStaticVoidMethodV "
                             "AnalyticsReceiver activity is invalid");
                    return false;
                }
                // The native Windows profile deliberately performs no
                // external analytics I/O. Retain the validated local request
                // so the host can provide a response bridge without exposing
                // guest references or pretending that a value exists.
                const std::string key_value =
                    local_key != object_references_.end()
                        ? local_key->second.string_value
                        : global_key->second.string_value;
                if (!QueueAnalyticsValueRequestLocked(key_value,
                                                      call_error))
                    return false;
                analytics_value_key_ = key_value;
                (*registers)[0] = 0;
                return true;
            }
            if (class_type ==
                    "com/king/usdk/privacyagecompliance/UsdkAgeSignals" &&
                method->second.name == "request" &&
                method->second.signature ==
                    "(Landroid/content/Context;JJJ)V") {
                if (backend_ != nullptr) {
                    SetError(call_error,
                             "guest JNI CallStaticVoidMethodV Age Signals "
                             "request is unavailable with an active backend");
                    return false;
                }
                std::array<std::uint64_t, 4> values{};
                for (std::size_t index = 0; index < values.size(); ++index) {
                    if (!ReadGuestGeneralArgument(address_space_,
                                                  (*registers)[3],
                                                  &values[index], call_error,
                                                  index))
                        return false;
                }
                const std::optional<std::string_view> context_type =
                    ObjectTypeLocked(values[0]);
                if (!context_type ||
                    (!context_type->ends_with("Context;") &&
                     !context_type->ends_with("Activity;"))) {
                    SetError(call_error,
                             "guest JNI CallStaticVoidMethodV Age Signals "
                             "context is invalid");
                    return false;
                }
                // The Java wrapper forwards the first and third long values
                // to both native completions; the middle long selects a local
                // debug scenario. Preserve all three until the owner-thread
                // bridge supplies the native-Windows offline completion.
                if (!QueueAgeSignalsRequestLocked(values[1], values[2],
                                                  values[3], call_error))
                    return false;
                (*registers)[0] = 0;
                return true;
            }
            if (class_type ==
                    "com/king/firebaseanalytics/FirebaseAnalyticsManager" &&
                method->second.name == "init" &&
                method->second.signature ==
                    "(Landroid/app/Activity;JJZ)V") {
                if (backend_ != nullptr) {
                    SetError(call_error,
                             "guest JNI CallStaticVoidMethodV Firebase "
                             "Analytics initialization is unavailable with "
                             "an active backend");
                    return false;
                }
                std::array<std::uint64_t, 4> values{};
                for (std::size_t index = 0; index < values.size(); ++index) {
                    if (!ReadGuestGeneralArgument(address_space_,
                                                  (*registers)[3],
                                                  &values[index], call_error,
                                                  index))
                        return false;
                }
                const std::optional<std::string_view> activity_type =
                    ObjectTypeLocked(values[0]);
                if (!activity_type ||
                    !activity_type->ends_with("Activity;")) {
                    SetError(call_error,
                             "guest JNI CallStaticVoidMethodV Firebase "
                             "Analytics activity is invalid");
                    return false;
                }
                if (values[3] > 1) {
                    SetError(call_error,
                             "guest JNI CallStaticVoidMethodV Firebase "
                             "Analytics initialization argument is not "
                             "boolean");
                    return false;
                }
                // Firebase is intentionally not connected to an external
                // service. Preserve the validated guest configuration for a
                // local host adapter while keeping initialization side-effect
                // free.
                firebase_analytics_configuration_ =
                    std::array<std::uint64_t, 3>{values[1], values[2],
                                                  values[3]};
                (*registers)[0] = 0;
                return true;
            }
            if (class_type ==
                    "com/king/firebaseanalytics/FirebaseAnalyticsManager" &&
                method->second.name == "setCollectionEnabled" &&
                method->second.signature == "(Z)V") {
                if (backend_ != nullptr) {
                    SetError(call_error,
                             "guest JNI CallStaticVoidMethodV Firebase "
                             "Analytics collection update is unavailable "
                             "with an active backend");
                    return false;
                }
                std::uint64_t enabled = 0;
                if (!ReadGuestGeneralArgument(address_space_,
                                              (*registers)[3], &enabled,
                                              call_error))
                    return false;
                if (enabled > 1) {
                    SetError(call_error,
                             "guest JNI CallStaticVoidMethodV Firebase "
                             "Analytics collection argument is not boolean");
                    return false;
                }
                firebase_analytics_collection_enabled_ = enabled != 0;
                (*registers)[0] = 0;
                return true;
            }
            if (class_type ==
                    "com/king/firebaseanalytics/FirebaseAnalyticsManager" &&
                method->second.name == "disconnectFromNativeSide" &&
                method->second.signature == "()V") {
                if (backend_ != nullptr) {
                    SetError(call_error,
                             "guest JNI CallStaticVoidMethodV Firebase Analytics disconnect is unavailable with an active backend");
                    return false;
                }
                // No external Firebase service is connected on native
                // Windows. Clear only the local adapter configuration.
                firebase_analytics_configuration_.reset();
                firebase_analytics_collection_enabled_.reset();
                (*registers)[0] = 0;
                return true;
            }
            if (class_type == "com/king/usdk/adinfo/ReferrerProvider" &&
                method->second.name == "startFetch" &&
                method->second.signature == "(Landroid/app/Activity;J)V") {
                if (backend_ != nullptr) {
                    SetError(call_error,
                             "guest JNI CallStaticVoidMethodV referrer "
                             "request is unavailable with an active backend");
                    return false;
                }
                std::uint64_t activity = 0;
                std::uint64_t native_pointer = 0;
                if (!ReadGuestGeneralArgument(address_space_,
                                              (*registers)[3], &activity,
                                              call_error) ||
                    !ReadGuestGeneralArgument(address_space_,
                                              (*registers)[3],
                                              &native_pointer, call_error, 1))
                    return false;
                const std::optional<std::string_view> activity_type =
                    ObjectTypeLocked(activity);
                if (!activity_type ||
                    !activity_type->ends_with("Activity;")) {
                    SetError(call_error,
                             "guest JNI CallStaticVoidMethodV referrer "
                             "activity is invalid");
                    return false;
                }
                // Native Windows does not contact the Play referrer service.
                // Keep the validated correlation value available to a local
                // adapter without claiming that a response was delivered.
                referrer_fetch_native_pointer_ = native_pointer;
                (*registers)[0] = 0;
                return true;
            }
            if (class_type == "com/king/usdk/adinfo/ReferrerProvider" &&
                method->second.name == "clearUserData" &&
                method->second.signature == "()V") {
                if (backend_ != nullptr) {
                    SetError(call_error,
                             "guest JNI CallStaticVoidMethodV referrer "
                             "clear is unavailable with an active backend");
                    return false;
                }
                // No Play referrer service exists on native Windows. Clear
                // the validated local correlation value without claiming an
                // external deletion or synthesizing a callback.
                referrer_fetch_native_pointer_.reset();
                (*registers)[0] = 0;
                return true;
            }
            if (class_type == "com/king/lifecycletracking/WebViewHelper" &&
                method->second.name == "getUserAgent" &&
                method->second.signature == "(Landroid/app/Activity;J)V") {
                if (backend_ != nullptr) {
                    SetError(call_error,
                             "guest JNI CallStaticVoidMethodV WebView user-agent request is unavailable with an active backend");
                    return false;
                }
                std::uint64_t activity = 0;
                std::uint64_t native_pointer = 0;
                if (!ReadGuestGeneralArgument(address_space_,
                                              (*registers)[3], &activity,
                                              call_error) ||
                    !ReadGuestGeneralArgument(address_space_,
                                              (*registers)[3],
                                              &native_pointer, call_error, 1))
                    return false;
                const std::optional<std::string_view> activity_type =
                    ObjectTypeLocked(activity);
                if (!activity_type || !activity_type->ends_with("Activity;")) {
                    SetError(call_error,
                             "guest JNI CallStaticVoidMethodV WebView user-agent activity is invalid");
                    return false;
                }
                // Record the validated correlation value for a future native
                // Windows WebView adapter. Do not fabricate a Java callback or
                // claim that an Android WebView supplied a user agent.
                web_view_user_agent_native_pointer_ = native_pointer;
                (*registers)[0] = 0;
                return true;
            }
            if (class_type == "com/king/core/UncaughtExceptionWriter" &&
                method->second.name == "setBuildId" &&
                method->second.signature == "(Ljava/lang/String;)V") {
                if (backend_ != nullptr) {
                    SetError(call_error,
                             "guest JNI CallStaticVoidMethodV UncaughtExceptionWriter setBuildId is unavailable with an active backend");
                    return false;
                }
                std::uint64_t argument = 0;
                if (!ReadGuestGeneralArgument(address_space_,
                                              (*registers)[3], &argument,
                                              call_error))
                    return false;
                const auto local_string =
                    FindOwnedLocalObjectReferenceLocked(argument);
                const auto global_string =
                    global_object_references_.find(argument);
                if ((local_string == object_references_.end() &&
                     global_string == global_object_references_.end()) ||
                    (local_string != object_references_.end()
                         ? local_string->second.type
                         : global_string->second.type) !=
                        "Ljava/lang/String;") {
                    SetError(call_error,
                             "guest JNI CallStaticVoidMethodV build id is not a string reference");
                    return false;
                }
                uncaught_exception_build_id_ =
                    local_string != object_references_.end()
                        ? local_string->second.string_value
                        : global_string->second.string_value;
                (*registers)[0] = 0;
                return true;
            }
            if (backend_ == nullptr &&
                method->second.class_type ==
                    "com.ea.nimble.bridge.NimbleCppComponentRegistrar" &&
                method->second.name == "register" &&
                method->second.signature == "(Ljava/lang/String;)V") {
                // Nimble's native components self-register with the Java
                // bridge; there is no Java registry to update in the
                // compatibility runtime, so accept and drop the request.
                (*registers)[0] = 0;
                return true;
            }
            if (backend_ == nullptr &&
                NormalizeCompatibilityType(method->second.class_type) ==
                    "com/popcap/pcsp/NetworkerJNI" &&
                method->second.name == "HttpGet" &&
                method->second.signature == "(Ljava/lang/String;J)V") {
                // The game's Draper/IronSource content request asks Java to
                // perform an HTTP GET and complete through
                // Java_com_popcap_pcsp_NetworkerJNI_callback(env, cls, J
                // context, int status, String body). The J context is the
                // guest's heap-allocated std::function<void(int,string)>.
                // No HTTP transport exists, so capture the context for the
                // launcher loop to complete with a failure status — the
                // caller's completion still runs and the app finishes
                // loading instead of parking on the pending request.
                std::uint64_t context = 0;
                if (!ReadGuestGeneralArgument(address_space_,
                                              (*registers)[3], &context,
                                              call_error, 1))
                    return false;
                pending_networker_completions_.push_back(context);
                std::fprintf(stderr,
                             "ISOMR_NETWORKER_HTTPGET captured context=%llx\n",
                             static_cast<unsigned long long>(context));
                std::fflush(stderr);
                (*registers)[0] = 0;
                return true;
            }
            if (backend_ == nullptr &&
                NormalizeCompatibilityType(method->second.class_type) ==
                    "com/applovin/mediation/unity/MaxUnityPlugin" &&
                method->second.name == "setBackgroundCallback") {
                // MAX Unity plugin: capture the BackgroundCallback proxy so
                // initializeSdk can deliver OnSdkInitializedEvent.
                std::uint64_t callback = 0;
                if (ReadGuestGeneralArgument(address_space_, (*registers)[3],
                                             &callback, call_error, 0)) {
                    const std::optional<std::uint64_t> identity =
                        ReferenceIdentityLocked(callback);
                    const auto proxy =
                        identity ? interface_proxies_.find(*identity)
                                 : interface_proxies_.end();
                    if (proxy != interface_proxies_.end())
                        max_background_callback_handle_ =
                            proxy->second.native_handle;
                }
                (*registers)[0] = 0;
                return true;
            }
            if (backend_ == nullptr &&
                NormalizeCompatibilityType(method->second.class_type) ==
                    "com/applovin/mediation/unity/MaxUnityPlugin" &&
                method->second.name == "initializeSdk") {
                // The managed side added its OnSdkInitializedEvent listener
                // before this call; on a real device the AppLovin SDK posts
                // the event to the background callback once init completes.
                // Deliver it immediately so the boot sequence proceeds.
                if (max_background_callback_handle_ != 0 &&
                    pending_ui_tasks_.size() < 1024) {
                    const std::string event_json =
                        "{\"name\":\"OnSdkInitializedEvent\"}";
                    const std::uint64_t event_reference =
                        AllocateCompatibilityReference(
                            heap_, CompatibilityReferenceKind::kString,
                            "Ljava/lang/String;", event_json, call_error);
                    if (event_reference != 0) {
                        object_references_.emplace(
                            event_reference,
                            ObjectReference{event_reference, (*registers)[2],
                                            "Ljava/lang/String;", event_json,
                                            0});
                        TrackLocalReferenceLocked(event_reference);
                        PendingUiTask task{};
                        task.native_handle = max_background_callback_handle_;
                        task.ready_at = UiTaskReadyAt(0);
                        task.dispatch_class =
                            "com/applovin/mediation/unity/"
                            "MaxUnityAdManager$BackgroundCallback";
                        task.dispatch_name = "forwardUnityEvent";
                        task.dispatch_signature = "(Ljava/lang/String;)V";
                        task.argument_reference = event_reference;
                        pending_ui_tasks_.push_back(std::move(task));
                        if (call_error != nullptr)
                            *call_error = "trace:jni-maxinit-queued";
                    }
                }
                (*registers)[0] = 0;
                return true;
            }
            if (backend_ == nullptr &&
                NormalizeCompatibilityType(method->second.class_type) ==
                    "com/ironsource/adqualitysdk/sdk/unity/"
                    "IronSourceAdQuality" &&
                method->second.name == "setUnityISAdQualityInitListener") {
                std::uint64_t listener = 0;
                if (ReadGuestGeneralArgument(address_space_, (*registers)[3],
                                             &listener, call_error, 0)) {
                    const std::optional<std::uint64_t> identity =
                        ReferenceIdentityLocked(listener);
                    const auto proxy =
                        identity ? interface_proxies_.find(*identity)
                                 : interface_proxies_.end();
                    if (proxy != interface_proxies_.end())
                        adquality_init_listener_handle_ =
                            proxy->second.native_handle;
                }
                (*registers)[0] = 0;
                return true;
            }
            if (backend_ == nullptr &&
                NormalizeCompatibilityType(method->second.class_type) ==
                    "com/ironsource/adqualitysdk/sdk/unity/"
                    "IronSourceAdQuality" &&
                method->second.name == "initialize") {
                // AdQuality init completes asynchronously on a real device;
                // the Unity listener's adQualitySdkInitSuccess releases the
                // game's SDK-wait gate.
                if (adquality_init_listener_handle_ != 0 &&
                    pending_ui_tasks_.size() < 1024) {
                    PendingUiTask task{};
                    task.native_handle = adquality_init_listener_handle_;
                    task.ready_at = UiTaskReadyAt(0);
                    task.dispatch_class =
                        "com/ironsource/adqualitysdk/sdk/unity/"
                        "UnityISAdQualityInitListener";
                    task.dispatch_name = "adQualitySdkInitSuccess";
                    task.dispatch_signature = "()V";
                    task.argument_reference = 0;
                    pending_ui_tasks_.push_back(std::move(task));
                    if (call_error != nullptr)
                        *call_error = "trace:jni-adquality-init-queued";
                }
                (*registers)[0] = 0;
                return true;
            }
            if (backend_ == nullptr &&
                NormalizeCompatibilityType(method->second.class_type) ==
                    "com/adjust/sdk/Adjust" &&
                method->second.name == "getAdid") {
                // Adjust SDK ad-id fetch: on Android the listener's
                // onAdidRead(adid) fires asynchronously. Deliver the zero id
                // through the UI-task pump so SDK init completes instead of
                // parking on a callback that never arrives.
                std::uint64_t listener = 0;
                if (ReadGuestGeneralArgument(address_space_, (*registers)[3],
                                             &listener, call_error, 0)) {
                    const std::optional<std::uint64_t> listener_identity =
                        ReferenceIdentityLocked(listener);
                    const auto proxy =
                        listener_identity
                            ? interface_proxies_.find(*listener_identity)
                            : interface_proxies_.end();
                    if (proxy != interface_proxies_.end() &&
                        pending_ui_tasks_.size() < 1024) {
                        const std::uint64_t adid_reference =
                            AllocateCompatibilityReference(
                                heap_, CompatibilityReferenceKind::kString,
                                "Ljava/lang/String;",
                                "00000000-0000-0000-0000-000000000000",
                                call_error);
                        if (adid_reference != 0) {
                            object_references_.emplace(
                                adid_reference,
                                ObjectReference{
                                    adid_reference, (*registers)[2],
                                    "Ljava/lang/String;",
                                    "00000000-0000-0000-0000-000000000000",
                                    0});
                            TrackLocalReferenceLocked(adid_reference);
                            PendingUiTask task{};
                            task.native_handle =
                                proxy->second.native_handle;
                            task.ready_at = UiTaskReadyAt(0);
                            task.dispatch_class =
                                "com/adjust/sdk/"
                                "OnAdidReadListener";
                            task.dispatch_name = "onAdidRead";
                            task.dispatch_signature =
                                "(Ljava/lang/String;)V";
                            task.argument_reference = adid_reference;
                            pending_ui_tasks_.push_back(std::move(task));
                            if (call_error != nullptr)
                                *call_error = "trace:jni-adid-queued";
                        }
                    }
                }
                (*registers)[0] = 0;
                return true;
            }
            if (backend_ == nullptr &&
                NormalizeCompatibilityType(method->second.class_type) ==
                    "com/zeptolab/ctr/CtrAndroidCloudReader" &&
                method->second.name == "startReading" &&
                method->second.signature == "(Ljava/lang/String;)V") {
                // On device this posts a UI runnable that calls
                // PlayGames.getGamesSignInClient; the APK ships no Play
                // Games metadata, so that call throws and the catch path
                // delivers
                // UnitySendMessage(gameObject, "OnCloudDataAvailable", "").
                // Queue that delivery so the managed cloud-wait completes.
                std::uint64_t name_reference = 0;
                if (!ReadGuestGeneralArgument(address_space_,
                                              (*registers)[3],
                                              &name_reference, call_error, 0))
                    return false;
                const std::string* name = nullptr;
                const auto local_name =
                    FindOwnedLocalObjectReferenceLocked(name_reference);
                if (local_name != object_references_.end() &&
                    IsStringTypeName(local_name->second.type))
                    name = &local_name->second.string_value;
                else {
                    const auto global_name =
                        global_object_references_.find(name_reference);
                    if (global_name != global_object_references_.end() &&
                        IsStringTypeName(global_name->second.type))
                        name = &global_name->second.string_value;
                }
                if (name != nullptr &&
                    pending_unity_messages_.size() < 64) {
                    pending_unity_messages_.push_back(
                        PendingUnityMessage{*name, "OnCloudDataAvailable",
                                          ""});
                    if (call_error != nullptr)
                        *call_error =
                            "trace:jni-unitymsg-queued-" + *name;
                }
                (*registers)[0] = 0;
                return true;
            }
            if (ApplyCompatOverride("static-void",
                                    method->second.class_type,
                                    method->second.name,
                                    method->second.signature, registers,
                                    call_error))
                return true;
            if (backend_ == nullptr) {
                // No Java backend runs the target; a registered but
                // unimplemented static void method is a benign no-op
                // (acknowledged and dropped, logged as a stub note) rather
                // than aborting the caller on an unimplemented signature.
                if (call_error != nullptr)
                    *call_error =
                        "trace:jni-call-static-void-stub-" +
                        method->second.class_type + "." +
                        method->second.name + method->second.signature;
                (*registers)[0] = 0;
                return true;
            }
            SetError(call_error,
                     "guest JNI CallStaticVoidMethodV class " +
                         std::string(class_type.value_or("<unknown>")) +
                         " method " +
                         method->second.name + " signature " +
                         method->second.signature +
                         " is not yet supported");
            return false;
        };
    const auto call_static_void_method_a =
        [this, call_static_void_method_v](
            Aarch64ImportRegistry::Registers* registers,
            std::string* call_error) {
            if (registers == nullptr ||
                !IsCurrentEnvironment((*registers)[0])) {
                SetError(call_error,
                         "guest JNI CallStaticVoidMethodA environment is invalid");
                return false;
            }
            Aarch64VaList synthesized{};
            synthesized.stack = (*registers)[3];
            synthesized.general_top = 0;
            synthesized.vector_top = 0;
            synthesized.general_offset = 0;
            synthesized.vector_offset = 0;
            const std::uint64_t scratch =
                heap_->Callocate(1, sizeof(synthesized));
            if (scratch == 0 ||
                !address_space_->Write(scratch, &synthesized,
                                       sizeof(synthesized))) {
                SetError(call_error,
                         "guest JNI CallStaticVoidMethodA argument frame failed");
                return false;
            }
            Aarch64ImportRegistry::Registers sub = *registers;
            sub[3] = scratch;
            const bool result =
                call_static_void_method_v(&sub, call_error);
            if (result) (*registers)[0] = sub[0];
            return result;
        };
    // CallStaticObjectMethodA passes a jvalue[] array instead of an AArch64
    // va_list. Synthesize a va_list whose overflow stack points at the jvalue
    // array (general_offset == 0 selects stack reads), then reuse the shared
    // CallStaticObjectMethodV logic.
    const auto call_static_object_method_a =
        [this, call_static_object_method_v](
            Aarch64ImportRegistry::Registers* registers,
            std::string* call_error) {
            if (registers == nullptr ||
                !IsCurrentEnvironment((*registers)[0])) {
                SetError(call_error,
                         "guest JNI CallStaticObjectMethodA environment is invalid");
                return false;
            }
            Aarch64VaList synthesized{};
            synthesized.stack = (*registers)[3];
            synthesized.general_top = 0;
            synthesized.vector_top = 0;
            synthesized.general_offset = 0;
            synthesized.vector_offset = 0;
            const std::uint64_t scratch =
                heap_->Callocate(1, sizeof(synthesized));
            if (scratch == 0 ||
                !address_space_->Write(scratch, &synthesized,
                                       sizeof(synthesized))) {
                SetError(call_error,
                         "guest JNI CallStaticObjectMethodA argument frame failed");
                return false;
            }
            Aarch64ImportRegistry::Registers sub = *registers;
            sub[3] = scratch;
            const bool result =
                call_static_object_method_v(&sub, call_error);
            // The V handler writes the jobject result into registers[0]; the
            // copy above means that landed in sub[0], so propagate it back or
            // the caller reads the untouched env pointer as the return value.
            if (result) (*registers)[0] = sub[0];
            if (!result) {
                const int env_ok =
                    IsCurrentEnvironment((*registers)[0]) ? 1 : 0;
                const std::uint64_t cur_env_dbg =
                    CurrentEnvironmentAddress().value_or(0);
                const std::lock_guard lock(mutex_);
                const auto method_dbg =
                    method_references_.find((*registers)[2]);
                std::fprintf(stderr,
                             "ISOMR_JNI_SOBJECT_A_FAIL tid=%u this=%p ts=%p env=%llx env_ok=%d cur_env=%llx cls=%llx mid=%llx jargs=%llx scratch=%llx m=%s.%s\n",
                             (unsigned)GetCurrentThreadId(),
                             (void*)this,
                             (void*)thread_state_,
                             (unsigned long long)(*registers)[0],
                             env_ok,
                             (unsigned long long)cur_env_dbg,
                             (unsigned long long)(*registers)[1],
                             (unsigned long long)(*registers)[2],
                             (unsigned long long)(*registers)[3],
                             (unsigned long long)scratch,
                             method_dbg != method_references_.end()
                                 ? method_dbg->second.class_type.c_str()
                                 : "?",
                             method_dbg != method_references_.end()
                                 ? method_dbg->second.name.c_str()
                                 : "?");
                std::fflush(stderr);
            }
            return result;
        };
    const auto call_static_boolean_method_v =
        [this](Aarch64ImportRegistry::Registers* registers,
               std::string* call_error) {
            if (registers == nullptr ||
                !IsCurrentEnvironment((*registers)[0]) ||
                (*registers)[3] == 0) {
                SetError(call_error,
                         "guest JNI CallStaticBooleanMethodV arguments are invalid");
                return false;
            }
            std::array<std::uint8_t, 32> arguments{};
            if (!address_space_->Read((*registers)[3], arguments.data(),
                                      arguments.size())) {
                SetError(call_error,
                         "guest JNI CallStaticBooleanMethodV va_list is invalid");
                return false;
            }
            const std::lock_guard lock(mutex_);
            const std::optional<std::uint64_t> class_identity =
                ReferenceIdentityLocked((*registers)[1]);
            const auto method = method_references_.find((*registers)[2]);
            // A jclass re-resolved through a fresh local ref carries a new
            // identity token for the same class; fall back to the class type
            // so a re-resolved jclass still dispatches its static methods.
            const std::optional<std::string_view> provenance_type =
                ClassTypeLocked((*registers)[1]);
            const bool class_matches =
                method != method_references_.end() && class_identity &&
                (method->second.class_identity == *class_identity ||
                 (provenance_type &&
                  method->second.class_type == *provenance_type));
            if (!class_identity || !IsClassReferenceLocked((*registers)[1]) ||
                !class_matches || !method->second.is_static) {
                if (backend_ == nullptr) {
                    std::fprintf(stderr,
                                 "ISOMR_JNI_SBOOL_STUB mid=%llx\n",
                                 (unsigned long long)(*registers)[2]);
                    std::fflush(stderr);
                    (*registers)[0] = 0;
                    return true;
                }
                SetError(call_error,
                         "guest JNI CallStaticBooleanMethodV provenance is invalid");
                return false;
            }
            const std::optional<std::string_view> class_type =
                ClassTypeLocked((*registers)[1]);
            if (backend_ == nullptr &&
                class_type &&
                NormalizeCompatibilityType(*class_type) ==
                    "com/ea/game/pvzfree_row/NetworkMonitorBridge" &&
                (method->second.signature == "()Z" ||
                 method->second.signature ==
                     "(Landroid/content/Context;)Z") &&
                (method->second.name == "initialize" ||
                 method->second.name == "isOnline" ||
                 method->second.name == "isConnected" ||
                 method->second.name == "isNetworkAvailable")) {
                // The compat runtime has no Android connectivity service and
                // no HTTP transport for the remote-config fetch. Reporting
                // "online" sends the engine down the async download path and
                // it parks in the TitleScreen wait state (+0x138 gate) forever
                // because no NetworkConnectionCallback ever completes. PvZ is
                // fully playable offline, so report "offline" and let it fall
                // back to the bundled FIRRemoteConfigDefaults.json.
                (*registers)[0] = 0;
                return true;
            }
            if (class_type == "com/king/core/Device" &&
                method->second.name == "isTablet" &&
                method->second.signature == "()Z") {
                if (backend_ != nullptr) {
                    SetError(call_error,
                             "guest JNI CallStaticBooleanMethodV Device "
                             "tablet query is unavailable with an active "
                             "backend");
                    return false;
                }
                // The native Win32 host is a desktop window, not an Android
                // tablet form factor.
                (*registers)[0] = 0;
                return true;
            }
            if (backend_ == nullptr &&
                class_type &&
                NormalizeCompatibilityType(*class_type) ==
                    "com/ea/game/pvzfree_row/PvZActivity" &&
                method->second.is_static &&
                (method->second.name == "getSafeInsetLeft" ||
                 method->second.name == "getSafeInsetTop" ||
                 method->second.name == "getSafeInsetRight" ||
                 method->second.name == "getSafeInsetBottom") &&
                method->second.signature == "()I") {
                // The compat window has no cutout insets; report zero.
                (*registers)[0] = 0;
                return true;
            }
            if (class_type == "com/king/device/DeviceImpl" &&
                method->second.name == "isInternetReachableCached" &&
                method->second.signature == "()Z") {
                if (backend_ != nullptr) {
                    SetError(call_error,
                             "guest JNI CallStaticBooleanMethodV DeviceImpl "
                             "network query is unavailable with an active "
                             "backend");
                    return false;
                }
                // No native Windows network backend is wired into this JNI
                // facade yet. Report the cached offline state rather than
                // advertising connectivity that the host cannot provide.
                (*registers)[0] = 0;
                return true;
            }
            if (class_type == "com/king/network/Network" &&
                method->second.name == "isInternetReachable" &&
                method->second.signature == "()Z") {
                if (backend_ != nullptr) {
                    SetError(call_error,
                             "guest JNI CallStaticBooleanMethodV Network "
                             "reachability query is unavailable with an "
                             "active backend");
                    return false;
                }
                // The facade has no native Windows network transport. Keep
                // the guest on its offline path until one is implemented.
                (*registers)[0] = 0;
                return true;
            }
            if (class_type ==
                    "com/king/usdk/popupwebview/internal/PopupFactory" &&
                method->second.name == "isSupportedByOs" &&
                method->second.signature ==
                    "(Landroid/app/Activity;)Z") {
                if (backend_ != nullptr) {
                    SetError(call_error,
                             "guest JNI CallStaticBooleanMethodV "
                             "PopupFactory support query is unavailable "
                             "with an active backend");
                    return false;
                }
                std::uint64_t activity = 0;
                if (!ReadGuestGeneralArgument(address_space_,
                                              (*registers)[3], &activity,
                                              call_error))
                    return false;
                const std::optional<std::string_view> activity_type =
                    ObjectTypeLocked(activity);
                if (!activity_type ||
                    !activity_type->ends_with("Activity;")) {
                    SetError(call_error,
                             "guest JNI CallStaticBooleanMethodV "
                             "PopupFactory activity is invalid");
                    return false;
                }
                // This host does not provide Android WebView. Report the
                // absence so the guest selects its own supported fallback.
                (*registers)[0] = 0;
                return true;
            }
            if (class_type == "com/king/core/GameLib" &&
                method->second.name == "isInMultiWindowMode" &&
                method->second.signature == "()Z") {
                if (backend_ != nullptr) {
                    SetError(call_error,
                             "guest JNI CallStaticBooleanMethodV GameLib multi-window query is unavailable with an active backend");
                    return false;
                }
                // The native Win32 host owns one ordinary top-level window;
                // Android multi-window mode does not apply to this profile.
                (*registers)[0] = 0;
                return true;
            }
            if (class_type ==
                    "com/king/usdk/identitygpgs/GooglePlayServices" &&
                method->second.name == "isAvailable" &&
                method->second.signature == "()Z") {
                if (backend_ != nullptr) {
                    SetError(call_error,
                             "guest JNI CallStaticBooleanMethodV Google Play Services query is unavailable with an active backend");
                    return false;
                }
                // The native Windows profile does not host Android Google
                // Play Services. Report absence so the guest can select its
                // supported fallback instead of advertising a fake service.
                (*registers)[0] = 0;
                return true;
            }
            if (class_type == "com/king/usdk/adinfo/AdvertisingId" &&
                method->second.name == "isAdvertisingInfoReceived" &&
                method->second.signature == "()Z") {
                if (backend_ != nullptr) {
                    SetError(call_error,
                             "guest JNI CallStaticBooleanMethodV "
                             "AdvertisingId query is unavailable with an "
                             "active backend");
                    return false;
                }
                (*registers)[0] = 1;
                return true;
            }
            if (ApplyCompatOverride("static-boolean",
                                    method->second.class_type,
                                    method->second.name,
                                    method->second.signature, registers,
                                    call_error))
                return true;
            if (backend_ == nullptr) {
                if (call_error != nullptr)
                    *call_error = "trace:jni-call-static-boolean-stub-" +
                                  method->second.class_type + "." +
                                  method->second.name + method->second.signature;
                (*registers)[0] = 0;
                return true;
            }
            SetError(call_error,
                     "guest JNI CallStaticBooleanMethodV class " +
                         std::string(class_type.value_or("<unknown>")) +
                         " method " + method->second.name + " signature " +
                         method->second.signature +
                         " is not yet supported");
            return false;
        };
    // CallStaticBooleanMethodA passes a jvalue[] array instead of an AArch64
    // va_list. Synthesize a va_list whose overflow stack points at the jvalue
    // array (general_offset == 0 selects stack reads), reuse the shared V
    // logic, then copy the boolean result back to registers[0].
    const auto call_static_boolean_method_a =
        [this, call_static_boolean_method_v](
            Aarch64ImportRegistry::Registers* registers,
            std::string* call_error) {
            if (registers == nullptr ||
                !IsCurrentEnvironment((*registers)[0])) {
                SetError(call_error,
                         "guest JNI CallStaticBooleanMethodA environment is invalid");
                return false;
            }
            Aarch64VaList synthesized{};
            synthesized.stack = (*registers)[3];
            synthesized.general_top = 0;
            synthesized.vector_top = 0;
            synthesized.general_offset = 0;
            synthesized.vector_offset = 0;
            const std::uint64_t scratch =
                heap_->Callocate(1, sizeof(synthesized));
            if (scratch == 0 ||
                !address_space_->Write(scratch, &synthesized,
                                       sizeof(synthesized))) {
                SetError(call_error,
                         "guest JNI CallStaticBooleanMethodA argument frame failed");
                return false;
            }
            Aarch64ImportRegistry::Registers sub = *registers;
            sub[3] = scratch;
            const bool result =
                call_static_boolean_method_v(&sub, call_error);
            if (result) (*registers)[0] = sub[0];
            return result;
        };
    // CallXxxMethodA adapters: registers[3] is a guest jvalue[] array, not a
    // va_list. Synthesize a va_list whose overflow stack aliases the array
    // (general_offset == 0 selects stack reads) and reuse the V logic.
    const auto adapt_to_a =
        [this](const auto& v_handler, std::string_view label) {
            return [this, v_handler, label](
                       Aarch64ImportRegistry::Registers* registers,
                       std::string* call_error) {
                if (registers == nullptr ||
                    !IsCurrentEnvironment((*registers)[0])) {
                    SetError(call_error, "guest JNI " + std::string(label) +
                                             " environment is invalid");
                    return false;
                }
                Aarch64VaList synthesized{};
                synthesized.stack = (*registers)[3];
                const std::uint64_t scratch =
                    heap_->Callocate(1, sizeof(synthesized));
                if (scratch == 0 ||
                    !address_space_->Write(scratch, &synthesized,
                                           sizeof(synthesized))) {
                    SetError(call_error, "guest JNI " + std::string(label) +
                                             " argument frame failed");
                    return false;
                }
                Aarch64ImportRegistry::Registers sub = *registers;
                sub[3] = scratch;
                const bool result = v_handler(&sub, call_error);
                // Float/double returns land in vector[0]; integer and
                // reference results land in general[0]. Propagate both.
                if (result) {
                    (*registers)[0] = sub[0];
                    registers->vector[0] = sub.vector[0];
                }
                return result;
            };
        };
    // Direct variadic CallXxxMethod adapters: trailing args arrive in x3-x7
    // (general) and v0-v7 (vector), with overflow on the caller stack.
    // Materialize both register tails into fake AAPCS64 save areas and point
    // a synthesized va_list at them.
    const auto adapt_to_direct =
        [this](const auto& v_handler, std::string_view label) {
            return [this, v_handler, label](
                       Aarch64ImportRegistry::Registers* registers,
                       std::string* call_error) {
                if (registers == nullptr ||
                    !IsCurrentEnvironment((*registers)[0])) {
                    SetError(call_error, "guest JNI " + std::string(label) +
                                             " environment is invalid");
                    return false;
                }
                const std::array<std::uint64_t, 5> saved{
                    (*registers)[3], (*registers)[4], (*registers)[5],
                    (*registers)[6], (*registers)[7]};
                const std::uint64_t area =
                    heap_->Callocate(1, sizeof(saved));
                if (area == 0 ||
                    !address_space_->Write(area, saved.data(),
                                           sizeof(saved))) {
                    SetError(call_error, "guest JNI " + std::string(label) +
                                             " argument save area failed");
                    return false;
                }
                const std::uint64_t vector_area =
                    heap_->Callocate(1, sizeof(registers->vector));
                if (vector_area == 0 ||
                    !address_space_->Write(vector_area,
                                           registers->vector.data(),
                                           sizeof(registers->vector))) {
                    SetError(call_error, "guest JNI " + std::string(label) +
                                             " vector save area failed");
                    return false;
                }
                Aarch64VaList synthesized{};
                synthesized.stack = registers->stack_pointer;
                synthesized.general_top = area + sizeof(saved);
                synthesized.vector_top =
                    vector_area + sizeof(registers->vector);
                synthesized.general_offset =
                    -static_cast<std::int32_t>(sizeof(saved));
                synthesized.vector_offset =
                    -static_cast<std::int32_t>(sizeof(registers->vector));
                const std::uint64_t scratch =
                    heap_->Callocate(1, sizeof(synthesized));
                if (scratch == 0 ||
                    !address_space_->Write(scratch, &synthesized,
                                           sizeof(synthesized))) {
                    SetError(call_error, "guest JNI " + std::string(label) +
                                             " argument frame failed");
                    return false;
                }
                Aarch64ImportRegistry::Registers sub = *registers;
                sub[3] = scratch;
                const bool result = v_handler(&sub, call_error);
                if (result) {
                    (*registers)[0] = sub[0];
                    registers->vector[0] = sub.vector[0];
                }
                return result;
            };
        };
    const auto call_object_method =
        adapt_to_direct(call_object_method_v, "CallObjectMethod");
    const auto call_boolean_method =
        adapt_to_direct(call_boolean_method_v, "CallBooleanMethod");
    const auto call_boolean_method_a =
        adapt_to_a(call_boolean_method_v, "CallBooleanMethodA");
    // Byte/Char/Short calls reuse the int path: jbyte/jchar/jshort results
    // are delivered through the same general register the int handler fills.
    const auto call_byte_method =
        adapt_to_direct(call_int_method_v, "CallByteMethod");
    const auto call_byte_method_a =
        adapt_to_a(call_int_method_v, "CallByteMethodA");
    const auto call_char_method =
        adapt_to_direct(call_int_method_v, "CallCharMethod");
    const auto call_char_method_a =
        adapt_to_a(call_int_method_v, "CallCharMethodA");
    const auto call_short_method =
        adapt_to_direct(call_int_method_v, "CallShortMethod");
    const auto call_short_method_a =
        adapt_to_a(call_int_method_v, "CallShortMethodA");
    const auto call_int_method =
        adapt_to_direct(call_int_method_v, "CallIntMethod");
    const auto call_int_method_a =
        adapt_to_a(call_int_method_v, "CallIntMethodA");
    const auto call_long_method =
        adapt_to_direct(call_long_method_v, "CallLongMethod");
    const auto call_long_method_a =
        adapt_to_a(call_long_method_v, "CallLongMethodA");
    const auto call_float_method =
        adapt_to_direct(call_float_method_v, "CallFloatMethod");
    const auto call_float_method_a =
        adapt_to_a(call_float_method_v, "CallFloatMethodA");
    const auto call_double_method =
        adapt_to_direct(call_double_method_v, "CallDoubleMethod");
    const auto call_double_method_a =
        adapt_to_a(call_double_method_v, "CallDoubleMethodA");
    const auto call_void_method =
        adapt_to_direct(call_void_method_v, "CallVoidMethod");
    const auto call_void_method_a =
        adapt_to_a(call_void_method_v, "CallVoidMethodA");
    const auto call_static_object_method =
        adapt_to_direct(call_static_object_method_v, "CallStaticObjectMethod");
    const auto call_static_boolean_method = adapt_to_direct(
        call_static_boolean_method_v, "CallStaticBooleanMethod");
    const auto call_static_byte_method_a =
        adapt_to_a(call_static_int_method_v, "CallStaticByteMethodA");
    const auto call_static_char_method_a =
        adapt_to_a(call_static_int_method_v, "CallStaticCharMethodA");
    const auto call_static_short_method_a =
        adapt_to_a(call_static_int_method_v, "CallStaticShortMethodA");
    const auto call_static_int_method =
        adapt_to_direct(call_static_int_method_v, "CallStaticIntMethod");
    const auto call_static_int_method_a =
        adapt_to_a(call_static_int_method_v, "CallStaticIntMethodA");
    const auto call_static_long_method =
        adapt_to_direct(call_static_long_method_v, "CallStaticLongMethod");
    const auto call_static_long_method_a =
        adapt_to_a(call_static_long_method_v, "CallStaticLongMethodA");
    const auto call_static_void_method =
        adapt_to_direct(call_static_void_method_v, "CallStaticVoidMethod");
    const auto fatal_error =
        [this](Aarch64ImportRegistry::Registers* registers,
               std::string* call_error) {
            if (registers == nullptr ||
                !IsCurrentEnvironment((*registers)[0])) {
                SetError(call_error,
                         "guest JNI FatalError environment is invalid");
                return false;
            }
            std::string message;
            if (!ReadGuestText(address_space_, (*registers)[1],
                               "FatalError message", &message,
                               call_error))
                return false;
            SetError(call_error,
                     "guest JNI FatalError: " + std::move(message));
            return false;
        };
    const auto get_java_vm =
        [this](Aarch64ImportRegistry::Registers* registers,
               std::string* call_error) {
            if (registers == nullptr ||
                !IsCurrentEnvironment((*registers)[0]) ||
                !WriteGuestPointer((*registers)[1], vm_address(),
                                   call_error))
                return false;
            (*registers)[0] = JniResult(kJniOk);
            return true;
        };
    const auto exception_check =
        [this](Aarch64ImportRegistry::Registers* registers,
               std::string* call_error) {
            if (registers == nullptr ||
                !IsCurrentEnvironment((*registers)[0])) {
                SetError(call_error, "guest JNI environment is invalid");
                return false;
            }
            bool pending = false;
            if (backend_ != nullptr) {
                if (!backend_->ExceptionCheck(&pending, call_error))
                    return false;
            } else {
                const std::lock_guard lock(mutex_);
                pending = exception_pending().pending;
            }
            (*registers)[0] = pending ? 1 : 0;
            return true;
        };
    const auto vm_destroy =
        [this](Aarch64ImportRegistry::Registers* registers,
               std::string* call_error) {
            if (registers == nullptr || !IsVmAddress((*registers)[0])) {
                SetError(call_error, "guest JavaVM is invalid");
                return false;
            }
            (*registers)[0] = JniResult(kJniError);
            return true;
        };
    const auto vm_attach =
        [this](Aarch64ImportRegistry::Registers* registers,
               std::string* call_error) {
            if (registers == nullptr || !IsVmAddress((*registers)[0])) {
                SetError(call_error, "guest JavaVM is invalid");
                return false;
            }
            if (!AttachCurrentThread(call_error)) return false;
            const std::optional<std::uint64_t> environment =
                CurrentEnvironmentAddress();
            if (!environment ||
                !WriteGuestPointer((*registers)[1], *environment,
                                   call_error))
                return false;
            (*registers)[0] = JniResult(kJniOk);
            return true;
        };
    const auto vm_detach =
        [this](Aarch64ImportRegistry::Registers* registers,
               std::string* call_error) {
            if (registers == nullptr || !IsVmAddress((*registers)[0])) {
                SetError(call_error, "guest JavaVM is invalid");
                return false;
            }
            if (!CurrentEnvironmentAddress()) {
                (*registers)[0] = JniResult(kJniDetached);
                return true;
            }
            if (!DetachCurrentThread(call_error)) return false;
            (*registers)[0] = JniResult(kJniOk);
            return true;
        };
    const auto vm_get_env =
        [this](Aarch64ImportRegistry::Registers* registers,
               std::string* call_error) {
            if (registers == nullptr || !IsVmAddress((*registers)[0])) {
                SetError(call_error, "guest JavaVM is invalid");
                return false;
            }
            if (!IsSupportedVersion((*registers)[2])) {
                if (!WriteGuestPointer((*registers)[1], 0, call_error))
                    return false;
                (*registers)[0] = JniResult(kJniVersionError);
                return true;
            }
            const std::optional<std::uint64_t> environment =
                CurrentEnvironmentAddress();
            if (!WriteGuestPointer((*registers)[1], environment.value_or(0),
                                   call_error))
                return false;
            (*registers)[0] = JniResult(environment ? kJniOk : kJniDetached);
            return true;
        };

    if (!imports->Register(std::string(kGetVersion), get_version, error) ||
        !imports->Register(std::string(kFindClass), find_class, error) ||
        !imports->Register(std::string(kFromReflectedMethod),
                           from_reflected_method, error) ||
        !imports->Register(std::string(kToReflectedMethod),
                           to_reflected_method, error) ||
        !imports->Register(std::string(kGetStaticMethodId),
                           get_static_method_id, error) ||
        !imports->Register(std::string(kGetStaticFieldId),
                           get_static_field_id, error) ||
        !imports->Register(std::string(kGetStaticObjectField),
                           get_static_object_field, error) ||
        !imports->Register(std::string(kGetStaticIntField),
                           get_static_int_field, error) ||
        !imports->Register(std::string(kGetMethodId), get_method_id,
                           error) ||
        !imports->Register(std::string(kGetFieldId), get_field_id,
                           error) ||
        !imports->Register(std::string(kGetObjectField), get_object_field,
                           error) ||
        !imports->Register(std::string(kGetIntField), get_int_field,
                           error) ||
        !imports->Register(std::string(kGetLongField), get_long_field,
                           error) ||
        !imports->Register(std::string(kGetFloatField), get_float_field,
                           error) ||
        !imports->Register(std::string(kGetDoubleField), get_double_field,
                           error) ||
        !imports->Register(std::string(kNewObject),
                           adapt_to_direct(new_object_v, "NewObject"),
                           error) ||
        !imports->Register(std::string(kNewObjectV), new_object_v,
                           error) ||
        !imports->Register(std::string(kNewObjectA),
                           adapt_to_a(new_object_v, "NewObjectA"), error) ||
        !imports->Register(std::string(kNewString), new_string, error) ||
        !imports->Register(std::string(kGetStringLength), get_string_length,
                           error) ||
        !imports->Register(std::string(kGetStringChars), get_string_chars,
                           error) ||
        !imports->Register(std::string(kReleaseStringChars),
                           release_string_chars, error) ||
        !imports->Register(std::string(kNewStringUtf), new_string_utf,
                            error) ||
        !imports->Register(std::string(kGetStringUtfLength),
                           get_string_utf_length, error) ||
        !imports->Register(std::string(kGetStringUtfChars),
                           get_string_utf_chars, error) ||
        !imports->Register(std::string(kReleaseStringUtfChars),
                           release_string_utf_chars, error) ||
        !imports->Register(std::string(kGetArrayLength), get_array_length,
                           error) ||
        !imports->Register(std::string(kNewObjectArray), new_object_array,
                           error) ||
        !imports->Register(std::string(kGetObjectArrayElement),
                           get_object_array_element, error) ||
        !imports->Register(std::string(kSetObjectArrayElement),
                           set_object_array_element, error) ||
        !imports->Register(std::string(kNewByteArray), new_byte_array,
                           error) ||
        !imports->Register(std::string(kNewIntArray), new_int_array,
                           error) ||
        !imports->Register(std::string(kNewLongArray), new_long_array,
                           error) ||
        !imports->Register(
            std::string(kGetPrimitiveArrayCritical),
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string* call_error) {
                (void)call_error;
                const std::uint64_t array_ref = (*registers)[1];
                const std::uint64_t bytes = heap_->Callocate(1, 0x100000);
                if (bytes == 0) {
                    (*registers)[0] = 0;
                    return true;
                }
                const std::lock_guard lock(mutex_);
                const auto ints = int_arrays_.find(array_ref);
                if (ints != int_arrays_.end()) {
                    std::vector<std::uint8_t> raw;
                    raw.reserve(ints->second.size() * 4);
                    for (std::uint32_t element : ints->second) {
                        for (int byte = 0; byte < 4; ++byte)
                            raw.push_back(static_cast<std::uint8_t>(
                                element >> (8 * byte)));
                    }
                    address_space_->Write(bytes, raw.data(), raw.size());
                } else {
                    const auto byts = byte_arrays_.find(array_ref);
                    if (byts != byte_arrays_.end())
                        address_space_->Write(bytes, byts->second.data(),
                                              byts->second.size());
                }
                if ((*registers)[2] != 0) {
                    const std::uint32_t one = 1;
                    (void)address_space_->Write((*registers)[2], &one,
                                                sizeof(one));
                }
                (*registers)[0] = bytes;
                return true;
            },
            error) ||
        !imports->Register(
            std::string(kReleasePrimitiveArrayCritical),
            [](Aarch64ImportRegistry::Registers* registers,
               std::string*) {
                // Read-only critical access: no write-back needed.
                (*registers)[0] = 0;
                return true;
            },
            error) ||
        !imports->Register(std::string(kGetByteArrayElements),
                           get_byte_array_elements, error) ||
        !imports->Register(std::string(kGetIntArrayElements),
                           get_int_array_elements, error) ||
        !imports->Register(std::string(kGetLongArrayElements),
                           get_long_array_elements, error) ||
        !imports->Register(std::string(kReleaseByteArrayElements),
                           release_byte_array_elements, error) ||
        !imports->Register(std::string(kReleaseIntArrayElements),
                           release_int_array_elements, error) ||
        !imports->Register(std::string(kReleaseLongArrayElements),
                           release_long_array_elements, error) ||
        !imports->Register(std::string(kGetByteArrayRegion),
                           get_byte_array_region, error) ||
        !imports->Register(std::string(kGetIntArrayRegion),
                           get_int_array_region, error) ||
        !imports->Register(std::string(kGetLongArrayRegion),
                           get_long_array_region, error) ||
        !imports->Register(std::string(kSetByteArrayRegion),
                           set_byte_array_region, error) ||
        !imports->Register(std::string(kSetIntArrayRegion),
                           set_int_array_region, error) ||
        !imports->Register(std::string(kSetLongArrayRegion),
                           set_long_array_region, error) ||
        !imports->Register(std::string(kCallObjectMethodV),
                           call_object_method_v, error) ||
        !imports->Register(std::string(kCallObjectMethodA),
                           call_object_method_a, error) ||
        !imports->Register(std::string(kCallObjectMethod),
                           call_object_method, error) ||
        !imports->Register(std::string(kCallBooleanMethod),
                           call_boolean_method, error) ||
        !imports->Register(std::string(kCallBooleanMethodV),
                           call_boolean_method_v, error) ||
        !imports->Register(std::string(kCallBooleanMethodA),
                           call_boolean_method_a, error) ||
        !imports->Register(std::string(kCallByteMethod),
                           call_byte_method, error) ||
        !imports->Register(std::string(kCallByteMethodA),
                           call_byte_method_a, error) ||
        !imports->Register(std::string(kCallCharMethod),
                           call_char_method, error) ||
        !imports->Register(std::string(kCallCharMethodA),
                           call_char_method_a, error) ||
        !imports->Register(std::string(kCallShortMethod),
                           call_short_method, error) ||
        !imports->Register(std::string(kCallShortMethodA),
                           call_short_method_a, error) ||
        !imports->Register(std::string(kCallIntMethod),
                           call_int_method, error) ||
        !imports->Register(std::string(kCallIntMethodV),
                           call_int_method_v, error) ||
        !imports->Register(std::string(kCallIntMethodA),
                           call_int_method_a, error) ||
        !imports->Register(std::string(kCallLongMethod),
                           call_long_method, error) ||
        !imports->Register(std::string(kCallLongMethodV),
                           call_long_method_v, error) ||
        !imports->Register(std::string(kCallLongMethodA),
                           call_long_method_a, error) ||
        !imports->Register(std::string(kCallFloatMethod),
                           call_float_method, error) ||
        !imports->Register(std::string(kCallFloatMethodV),
                           call_float_method_v, error) ||
        !imports->Register(std::string(kCallFloatMethodA),
                           call_float_method_a, error) ||
        !imports->Register(std::string(kCallDoubleMethod),
                           call_double_method, error) ||
        !imports->Register(std::string(kCallDoubleMethodV),
                           call_double_method_v, error) ||
        !imports->Register(std::string(kCallDoubleMethodA),
                           call_double_method_a, error) ||
        !imports->Register(std::string(kCallVoidMethod),
                           call_void_method, error) ||
        !imports->Register(std::string(kCallVoidMethodV),
                           call_void_method_v, error) ||
        !imports->Register(std::string(kCallVoidMethodA),
                           call_void_method_a, error) ||
        !imports->Register(std::string(kDeleteLocalRef), delete_local_ref,
                           error) ||
        !imports->Register(std::string(kIsSameObject), is_same_object,
                           error) ||
        !imports->Register(std::string(kNewLocalRef), new_local_ref,
                           error) ||
        !imports->Register(std::string(kGetObjectClass), get_object_class,
                           error) ||
        !imports->Register(std::string(kIsInstanceOf), is_instance_of,
                           error) ||
        !imports->Register(std::string(kNewGlobalRef), new_global_ref,
                           error) ||
        !imports->Register(std::string(kDeleteGlobalRef), delete_global_ref,
                           error) ||
        // A weak global ref is a global ref here: the compat runtime runs no
        // guest GC that could clear it, so the strong tables model it.
        !imports->Register(std::string(kNewWeakGlobalRef), new_global_ref,
                           error) ||
        !imports->Register(std::string(kDeleteWeakGlobalRef),
                           delete_global_ref, error) ||
        !imports->Register(std::string(kCallStaticObjectMethod),
                           call_static_object_method, error) ||
        !imports->Register(std::string(kCallStaticObjectMethodV),
                           call_static_object_method_v, error) ||
        !imports->Register(std::string(kCallStaticObjectMethodA),
                           call_static_object_method_a, error) ||
        !imports->Register(std::string(kCallStaticBooleanMethod),
                           call_static_boolean_method, error) ||
        !imports->Register(std::string(kCallStaticBooleanMethodV),
                           call_static_boolean_method_v, error) ||
        !imports->Register(std::string(kCallStaticBooleanMethodA),
                           call_static_boolean_method_a, error) ||
        !imports->Register(std::string(kCallStaticByteMethodA),
                           call_static_byte_method_a, error) ||
        !imports->Register(std::string(kCallStaticCharMethodA),
                           call_static_char_method_a, error) ||
        !imports->Register(std::string(kCallStaticShortMethodA),
                           call_static_short_method_a, error) ||
        !imports->Register(std::string(kCallStaticIntMethod),
                           call_static_int_method, error) ||
        !imports->Register(std::string(kCallStaticIntMethodV),
                           call_static_int_method_v, error) ||
        !imports->Register(std::string(kCallStaticIntMethodA),
                           call_static_int_method_a, error) ||
        !imports->Register(std::string(kCallStaticLongMethod),
                           call_static_long_method, error) ||
        !imports->Register(std::string(kCallStaticLongMethodV),
                           call_static_long_method_v, error) ||
        !imports->Register(std::string(kCallStaticLongMethodA),
                           call_static_long_method_a, error) ||
        !imports->Register(std::string(kCallStaticVoidMethod),
                           call_static_void_method, error) ||
        !imports->Register(std::string(kCallStaticVoidMethodV),
                           call_static_void_method_v, error) ||
        !imports->Register(std::string(kCallStaticVoidMethodA),
                           call_static_void_method_a, error) ||
        !imports->Register(std::string(kExceptionOccurred),
                           exception_occurred, error) ||
        !imports->Register(std::string(kExceptionClear), exception_clear,
                           error) ||
        !imports->Register(std::string(kThrowNew), throw_new, error) ||
        !imports->Register(std::string(kThrowThrowable), throw_throwable,
                           error) ||
        !imports->Register(std::string(kFatalError), fatal_error, error) ||
        !imports->Register(std::string(kPushLocalFrame), push_local_frame,
                           error) ||
        !imports->Register(std::string(kPopLocalFrame), pop_local_frame,
                           error) ||
        !imports->Register(std::string(kRegisterNatives), register_natives,
                           error) ||
        !imports->Register(std::string(kUnregisterNatives),
                           unregister_natives, error) ||
        !imports->Register(std::string(kGetJavaVm), get_java_vm, error) ||
        !((backend_ == nullptr &&
           [] {
               // Opt-in: measurements show CTR's load phases run FASTER
               // through the serialized host dispatch (SVC yields pace the
               // JNI-mutex handoff), so the inline stub stays off unless
               // ARMR_INLINE_EXC=1 is set for experiments.
               wchar_t flag[4] = {};
               return GetEnvironmentVariableW(L"ARMR_INLINE_EXC", flag, 4) >
                          0 &&
                      flag[0] != L'0';
           }())
              ? imports->RegisterInlineStub(
                    std::string(kExceptionCheck),
                    0x39402000U /* ldrb w0, [x0, #8] = env exception flag */,
                    0xd65f03c0U /* ret */, error)
              : imports->Register(std::string(kExceptionCheck),
                                  exception_check, error)) ||
        !imports->Register(std::string(kVmDestroy), vm_destroy, error) ||
        !imports->Register(std::string(kVmAttach), vm_attach, error) ||
        !imports->Register(std::string(kVmDetach), vm_detach, error) ||
        !imports->Register(std::string(kVmGetEnv), vm_get_env, error) ||
        !imports->Register(std::string(kVmAttachDaemon), vm_attach, error))
        return false;
    for (std::size_t slot = 4; slot < kEnvironmentSlotCount; ++slot) {
        if (IsImplementedEnvironmentSlot(slot)) continue;
        const std::string name = UnsupportedEnvironmentName(slot);
        if (!imports->Register(
                name,
                [this, slot](Aarch64ImportRegistry::Registers* registers,
                       std::string* call_error) {
                    if (registers != nullptr && slot == 14) {
                        std::string message = "<null>";
                        std::string message_error;
                        if ((*registers)[2] != 0 &&
                            !ReadGuestStringValue(
                                address_space_, (*registers)[2], &message,
                                &message_error)) {
                            const std::lock_guard lock(mutex_);
                            const std::optional<std::string_view> message_type =
                                ObjectTypeLocked((*registers)[2]);
                            std::ostringstream detail;
                            detail << "guest JNI ThrowNew message is unreadable address=0x"
                                   << std::hex << (*registers)[2] << std::dec
                                   << " object-type="
                                   << message_type.value_or("<none>")
                                   << " reason=" << message_error
                                   << " range="
                                   << heap_->DescribeRange((*registers)[2], 1);
                            std::array<std::uint8_t, 16> raw{};
                            if (heap_->Read((*registers)[2], raw.data(),
                                            raw.size())) {
                                detail << " bytes=" << std::hex;
                                for (const std::uint8_t value : raw) {
                                    detail.width(2);
                                    detail.fill('0');
                                    detail << static_cast<unsigned>(value);
                                }
                                detail << std::dec;
                            }
                            SetError(call_error, detail.str());
                            return false;
                        }
                        const std::lock_guard lock(mutex_);
                        const std::optional<std::string_view> exception_type =
                            ClassTypeLocked((*registers)[1]);
                        SetError(call_error,
                                 "guest JNI ThrowNew is not implemented class=" +
                                     std::string(exception_type.value_or(
                                         "<invalid>")) +
                                     " message=" + message);
                        return false;
                    }
                    if (registers != nullptr && slot >= 114 && slot <= 143) {
                        const std::lock_guard lock(mutex_);
                        const auto method =
                            method_references_.find((*registers)[2]);
                        const std::optional<std::string_view> class_type =
                            ClassTypeLocked((*registers)[1]);
                        if (method != method_references_.end()) {
                            SetError(call_error,
                                     "guest JNI environment slot " +
                                         std::to_string(slot) +
                                         " for class " +
                                         std::string(class_type.value_or(
                                             "<unknown>")) +
                                         " method " + method->second.name +
                                         " signature " +
                                         method->second.signature +
                                         " is not implemented");
                            return false;
                        }
                    }
                    if (registers != nullptr && slot >= 145 && slot <= 153) {
                        const std::lock_guard lock(mutex_);
                        const auto field =
                            field_references_.find((*registers)[2]);
                        if (field != field_references_.end()) {
                            SetError(call_error,
                                     "guest JNI environment slot " +
                                         std::to_string(slot) +
                                         " for static field " +
                                         field->second.name + " signature " +
                                         field->second.signature +
                                         " is not implemented");
                            return false;
                        }
                    }
                    if (registers != nullptr && slot >= 95 && slot <= 103) {
                        const std::lock_guard lock(mutex_);
                        const auto field =
                            field_references_.find((*registers)[2]);
                        if (field != field_references_.end()) {
                            SetError(call_error,
                                     "guest JNI environment slot " +
                                         std::to_string(slot) +
                                         " for instance field " +
                                         field->second.name + " signature " +
                                         field->second.signature +
                                         " is not implemented");
                            return false;
                        }
                    }
                    SetError(call_error,
                             "guest JNI environment slot " +
                                 std::to_string(slot) +
                                 " is not implemented");
                    return false;
                },
                error))
            return false;
    }
    return true;
}

bool GuestJni::Initialize(const Aarch64ImportRegistry& imports,
                          std::string* error) {
    if (initialization_attempted_) {
        SetError(error, initialized_ ? "guest JNI is already initialized"
                                     : "guest JNI initialization failed");
        return false;
    }
    initialization_attempted_ = true;
    if (!registration_attempted_ || !imports.sealed()) {
        SetError(error, "guest JNI imports are not sealed");
        return false;
    }
    std::unique_ptr<GuestFunctionTable> vm;
    try {
        vm = std::make_unique<GuestFunctionTable>(heap_, kVmSlotCount,
                                                  error);
    } catch (const std::bad_alloc&) {
        SetError(error, "guest JavaVM table allocation failed");
        return false;
    }
    if (!vm->valid()) return false;
    for (std::size_t slot = 0; slot < 3; ++slot) {
        if (!vm->AssignNullSlot(slot, error)) return false;
    }
    constexpr std::array<std::string_view, 5> handlers{
        kVmDestroy, kVmAttach, kVmDetach, kVmGetEnv, kVmAttachDaemon};
    for (std::size_t index = 0; index < handlers.size(); ++index) {
        if (!vm->AssignSlot(index + 3, handlers[index], imports, error))
            return false;
    }
    if (!vm->Seal(error)) return false;
    imports_ = &imports;
    vm_ = std::move(vm);
    initialized_ = true;
    return true;
}

void GuestJni::TrackLocalReferenceLocked(std::uint64_t reference) {
    if (reference == 0 || thread_state_ == nullptr) return;
    const std::optional<std::uint64_t> thread =
        thread_state_->CurrentErrnoAddress();
    if (!thread) return;
    if (!environments_.contains(*thread)) return;
    deleted_class_references_.erase(reference);
    local_reference_owners_[reference] = *thread;
    const auto frames = local_reference_frames_.find(*thread);
    if (frames == local_reference_frames_.end() || frames->second.empty())
        return;
    frames->second.back().created_references.push_back(reference);
}

void GuestJni::ForgetLocalReferenceLocked(std::uint64_t reference) {
    local_reference_owners_.erase(reference);
    for (auto& [thread, frames] : local_reference_frames_) {
        static_cast<void>(thread);
        for (LocalReferenceFrame& frame : frames) {
            std::erase(frame.created_references, reference);
        }
    }
}

void GuestJni::EnsureStubArrayStorageLocked(std::uint64_t reference,
                                            const std::string& return_type) {
    if (return_type == "[B") {
        byte_arrays_.emplace(reference, std::vector<std::uint8_t>{});
    } else if (return_type == "[I") {
        int_arrays_.emplace(reference, std::vector<std::uint32_t>{});
    } else if (return_type == "[J") {
        long_arrays_.emplace(reference, std::vector<std::uint64_t>{});
    } else if (return_type.starts_with("[L") ||
               return_type.starts_with("[[")) {
        object_arrays_.emplace(
            reference,
            ObjectArrayStorage{return_type,
                               return_type.substr(1),
                               {}});
    }
}

bool GuestJni::CurrentThreadOwnsLocalReferenceLocked(
    std::uint64_t reference) const {
    const auto owner = local_reference_owners_.find(reference);
    if (owner == local_reference_owners_.end() || thread_state_ == nullptr)
        return false;
    const std::optional<std::uint64_t> current =
        thread_state_->CurrentErrnoAddress();
    return current && owner->second == *current &&
           environments_.contains(*current);
}

bool GuestJni::DeleteLocalReferenceLocked(std::uint64_t reference,
                                          std::string* error) {
    const auto object = object_references_.find(reference);
    if (object != object_references_.end()) {
        const std::uint64_t identity = object->second.identity;
        if (backend_ != nullptr &&
            !backend_->DeleteLocalRef(object->second.backend, error))
            return false;
        // Compat objects are pinned heap blocks the guest may still hold by
        // raw address (il2cpp field writes, retained builder chains). ART's
        // IndirectRefTable delete only drops the ref entry — the object
        // itself is GC-managed. Releasing the block here turns benign
        // stale writes into invalid-range faults, so drop the map entry but
        // leave the storage mapped; the block is reclaimed at Shutdown.
        if (backend_ != nullptr && !heap_->Release(reference, error))
            return false;
        if (backend_ == nullptr)
            retained_reference_blocks_.insert(reference);
        object_references_.erase(object);
        ForgetLocalReferenceLocked(reference);
        if (!IdentityHasAttachmentsLocked(identity)) return true;
        bool identity_is_live = false;
        for (const auto& [candidate, value] : object_references_) {
            static_cast<void>(candidate);
            identity_is_live = value.identity == identity;
            if (identity_is_live) break;
        }
        if (!identity_is_live) {
            for (const auto& [candidate, value] :
                 global_object_references_) {
                static_cast<void>(candidate);
                identity_is_live = value.identity == identity;
                if (identity_is_live) break;
            }
        }
        if (!identity_is_live &&
            !EraseIdentityAttachmentsLocked(identity, error))
            return false;
        return true;
    }
    const auto class_reference = class_references_.find(reference);
    if (class_reference != class_references_.end()) {
        if (backend_ != nullptr &&
            !backend_->DeleteLocalRef(class_reference->second.backend,
                                      error))
            return false;
        if (backend_ != nullptr && !heap_->Release(reference, error))
            return false;
        if (backend_ == nullptr)
            retained_reference_blocks_.insert(reference);
        deleted_class_references_.insert(reference);
        class_references_.erase(class_reference);
        ForgetLocalReferenceLocked(reference);
        return true;
    }
    SetError(error, "guest JNI DeleteLocalRef reference is invalid");
    return false;
}

bool GuestJni::DeleteLocalReference(std::uint64_t reference,
                                    std::string* error) {
    if (reference == 0) return true;
    const std::lock_guard lock(mutex_);
    // Unity's main/render split and StringBuilder traffic hand local refs
    // across attached JNI threads; a still-registered ref may be dropped by
    // whichever thread releases it, not only its creator. il2cpp reflection
    // cleanup also double-deletes (e.g. a reflected Method object is dropped
    // both as a call result and as a cached arg), which ART's
    // IndirectRefTable tolerates as a no-op. Match that: an untracked ref is
    // a traced success, not a hard failure.
    if (!local_reference_owners_.contains(reference) &&
        !object_references_.contains(reference) &&
        !class_references_.contains(reference)) {
        if (deleted_class_references_.contains(reference)) {
            SetError(error,
                     "guest JNI DeleteLocalRef reference is invalid");
            return false;
        }
        if (error != nullptr) {
            char detail[96];
            std::snprintf(detail, sizeof(detail),
                          "trace:jni-dellocalref-stale-0x%llx",
                          static_cast<unsigned long long>(reference));
            *error = detail;
        }
        return true;
    }
    return DeleteLocalReferenceLocked(reference, error);
}

bool GuestJni::BuildEnvironmentLocked(std::uint64_t thread_handle,
                                      std::string* error) {
    if (!initialized_ || imports_ == nullptr || thread_handle == 0) {
        SetError(error, "guest JNI environment configuration is invalid");
        return false;
    }
    std::unique_ptr<GuestFunctionTable> environment;
    try {
        environment = std::make_unique<GuestFunctionTable>(
            heap_, kEnvironmentSlotCount, error);
    } catch (const std::bad_alloc&) {
        SetError(error, "guest JNI environment table allocation failed");
        return false;
    }
    if (!environment->valid()) return false;
    for (std::size_t slot = 0; slot < 4; ++slot) {
        if (!environment->AssignNullSlot(slot, error)) return false;
    }
    for (std::size_t slot = 4; slot < kEnvironmentSlotCount; ++slot) {
        if (!IsImplementedEnvironmentSlot(slot)) {
            const std::string handler = UnsupportedEnvironmentName(slot);
            if (!environment->AssignSlot(slot, handler, *imports_, error))
                return false;
        } else {
            std::string_view handler = kGetVersion;
            if (slot == kFindClassSlot)
                handler = kFindClass;
            else if (slot == kFromReflectedMethodSlot)
                handler = kFromReflectedMethod;
            else if (slot == kToReflectedMethodSlot)
                handler = kToReflectedMethod;
            else if (slot == kGetStaticMethodIdSlot)
                handler = kGetStaticMethodId;
            else if (slot == kGetStaticFieldIdSlot)
                handler = kGetStaticFieldId;
            else if (slot == kGetStaticObjectFieldSlot)
                handler = kGetStaticObjectField;
            else if (slot == kGetStaticIntFieldSlot)
                handler = kGetStaticIntField;
            else if (slot == kGetMethodIdSlot)
                handler = kGetMethodId;
            else if (slot == kGetFieldIdSlot)
                handler = kGetFieldId;
            else if (slot == kGetObjectFieldSlot)
                handler = kGetObjectField;
            else if (slot == kGetIntFieldSlot)
                handler = kGetIntField;
            else if (slot == kGetLongFieldSlot)
                handler = kGetLongField;
            else if (slot == kGetFloatFieldSlot)
                handler = kGetFloatField;
            else if (slot == kGetDoubleFieldSlot)
                handler = kGetDoubleField;
            else if (slot == kNewObjectSlot)
                handler = kNewObject;
            else if (slot == kNewObjectVSlot)
                handler = kNewObjectV;
            else if (slot == kNewObjectASlot)
                handler = kNewObjectA;
            else if (slot == kNewStringSlot)
                handler = kNewString;
            else if (slot == kGetStringLengthSlot)
                handler = kGetStringLength;
            else if (slot == kGetStringCharsSlot)
                handler = kGetStringChars;
            else if (slot == kReleaseStringCharsSlot)
                handler = kReleaseStringChars;
            else if (slot == kNewStringUtfSlot)
                handler = kNewStringUtf;
            else if (slot == kGetStringUtfLengthSlot)
                handler = kGetStringUtfLength;
            else if (slot == kGetStringUtfCharsSlot)
                handler = kGetStringUtfChars;
            else if (slot == kReleaseStringUtfCharsSlot)
                handler = kReleaseStringUtfChars;
            else if (slot == kGetArrayLengthSlot)
                handler = kGetArrayLength;
            else if (slot == kNewObjectArraySlot)
                handler = kNewObjectArray;
            else if (slot == kGetObjectArrayElementSlot)
                handler = kGetObjectArrayElement;
            else if (slot == kSetObjectArrayElementSlot)
                handler = kSetObjectArrayElement;
            else if (slot == kNewByteArraySlot)
                handler = kNewByteArray;
            else if (slot == kNewIntArraySlot)
                handler = kNewIntArray;
            else if (slot == kNewLongArraySlot)
                handler = kNewLongArray;
            else if (slot == kGetPrimitiveArrayCriticalSlot)
                handler = kGetPrimitiveArrayCritical;
            else if (slot == kReleasePrimitiveArrayCriticalSlot)
                handler = kReleasePrimitiveArrayCritical;
            else if (slot == kGetByteArrayElementsSlot)
                handler = kGetByteArrayElements;
            else if (slot == kGetIntArrayElementsSlot)
                handler = kGetIntArrayElements;
            else if (slot == kGetLongArrayElementsSlot)
                handler = kGetLongArrayElements;
            else if (slot == kReleaseByteArrayElementsSlot)
                handler = kReleaseByteArrayElements;
            else if (slot == kReleaseIntArrayElementsSlot)
                handler = kReleaseIntArrayElements;
            else if (slot == kReleaseLongArrayElementsSlot)
                handler = kReleaseLongArrayElements;
            else if (slot == kGetByteArrayRegionSlot)
                handler = kGetByteArrayRegion;
            else if (slot == kGetIntArrayRegionSlot)
                handler = kGetIntArrayRegion;
            else if (slot == kGetLongArrayRegionSlot)
                handler = kGetLongArrayRegion;
            else if (slot == kSetByteArrayRegionSlot)
                handler = kSetByteArrayRegion;
            else if (slot == kSetIntArrayRegionSlot)
                handler = kSetIntArrayRegion;
            else if (slot == kSetLongArrayRegionSlot)
                handler = kSetLongArrayRegion;
            else if (slot == kCallObjectMethodSlot)
                handler = kCallObjectMethod;
            else if (slot == kCallObjectMethodVSlot)
                handler = kCallObjectMethodV;
            else if (slot == kCallObjectMethodASlot)
                handler = kCallObjectMethodA;
            else if (slot == kCallBooleanMethodSlot)
                handler = kCallBooleanMethod;
            else if (slot == kCallBooleanMethodVSlot)
                handler = kCallBooleanMethodV;
            else if (slot == kCallBooleanMethodASlot)
                handler = kCallBooleanMethodA;
            else if (slot == kCallByteMethodSlot)
                handler = kCallByteMethod;
            else if (slot == kCallByteMethodVSlot)
                handler = kCallIntMethodV;
            else if (slot == kCallByteMethodASlot)
                handler = kCallByteMethodA;
            else if (slot == kCallCharMethodSlot)
                handler = kCallCharMethod;
            else if (slot == kCallCharMethodVSlot)
                handler = kCallIntMethodV;
            else if (slot == kCallCharMethodASlot)
                handler = kCallCharMethodA;
            else if (slot == kCallShortMethodSlot)
                handler = kCallShortMethod;
            else if (slot == kCallShortMethodVSlot)
                handler = kCallIntMethodV;
            else if (slot == kCallShortMethodASlot)
                handler = kCallShortMethodA;
            else if (slot == kCallIntMethodSlot)
                handler = kCallIntMethod;
            else if (slot == kCallIntMethodVSlot)
                handler = kCallIntMethodV;
            else if (slot == kCallIntMethodASlot)
                handler = kCallIntMethodA;
            else if (slot == kCallLongMethodSlot)
                handler = kCallLongMethod;
            else if (slot == kCallLongMethodVSlot)
                handler = kCallLongMethodV;
            else if (slot == kCallLongMethodASlot)
                handler = kCallLongMethodA;
            else if (slot == kCallFloatMethodSlot)
                handler = kCallFloatMethod;
            else if (slot == kCallFloatMethodVSlot)
                handler = kCallFloatMethodV;
            else if (slot == kCallFloatMethodASlot)
                handler = kCallFloatMethodA;
            else if (slot == kCallDoubleMethodSlot)
                handler = kCallDoubleMethod;
            else if (slot == kCallDoubleMethodVSlot)
                handler = kCallDoubleMethodV;
            else if (slot == kCallDoubleMethodASlot)
                handler = kCallDoubleMethodA;
            else if (slot == kCallVoidMethodSlot)
                handler = kCallVoidMethod;
            else if (slot == kCallVoidMethodVSlot)
                handler = kCallVoidMethodV;
            else if (slot == kCallVoidMethodASlot)
                handler = kCallVoidMethodA;
            else if (slot == kDeleteLocalRefSlot)
                handler = kDeleteLocalRef;
            else if (slot == kIsSameObjectSlot)
                handler = kIsSameObject;
            else if (slot == kNewLocalRefSlot)
                handler = kNewLocalRef;
            else if (slot == kGetObjectClassSlot)
                handler = kGetObjectClass;
            else if (slot == kIsInstanceOfSlot)
                handler = kIsInstanceOf;
            else if (slot == kNewGlobalRefSlot)
                handler = kNewGlobalRef;
            else if (slot == kDeleteGlobalRefSlot)
                handler = kDeleteGlobalRef;
            else if (slot == kNewWeakGlobalRefSlot)
                handler = kNewWeakGlobalRef;
            else if (slot == kDeleteWeakGlobalRefSlot)
                handler = kDeleteWeakGlobalRef;
            else if (slot == kCallStaticObjectMethodSlot)
                handler = kCallStaticObjectMethod;
            else if (slot == kCallStaticObjectMethodVSlot)
                handler = kCallStaticObjectMethodV;
            else if (slot == kCallStaticObjectMethodASlot)
                handler = kCallStaticObjectMethodA;
            else if (slot == kCallStaticBooleanMethodSlot)
                handler = kCallStaticBooleanMethod;
            else if (slot == kCallStaticBooleanMethodVSlot)
                handler = kCallStaticBooleanMethodV;
            else if (slot == kCallStaticBooleanMethodASlot)
                handler = kCallStaticBooleanMethodA;
            else if (slot == kCallStaticByteMethodSlot)
                handler = kCallStaticIntMethod;
            else if (slot == kCallStaticByteMethodVSlot)
                handler = kCallStaticIntMethodV;
            else if (slot == kCallStaticByteMethodASlot)
                handler = kCallStaticByteMethodA;
            else if (slot == kCallStaticCharMethodSlot)
                handler = kCallStaticIntMethod;
            else if (slot == kCallStaticCharMethodVSlot)
                handler = kCallStaticIntMethodV;
            else if (slot == kCallStaticCharMethodASlot)
                handler = kCallStaticCharMethodA;
            else if (slot == kCallStaticShortMethodSlot)
                handler = kCallStaticIntMethod;
            else if (slot == kCallStaticShortMethodVSlot)
                handler = kCallStaticIntMethodV;
            else if (slot == kCallStaticShortMethodASlot)
                handler = kCallStaticShortMethodA;
            else if (slot == kCallStaticIntMethodSlot)
                handler = kCallStaticIntMethod;
            else if (slot == kCallStaticIntMethodVSlot)
                handler = kCallStaticIntMethodV;
            else if (slot == kCallStaticIntMethodASlot)
                handler = kCallStaticIntMethodA;
            else if (slot == kCallStaticLongMethodSlot)
                handler = kCallStaticLongMethod;
            else if (slot == kCallStaticLongMethodVSlot)
                handler = kCallStaticLongMethodV;
            else if (slot == kCallStaticLongMethodASlot)
                handler = kCallStaticLongMethodA;
            else if (slot == kCallStaticVoidMethodSlot)
                handler = kCallStaticVoidMethod;
            else if (slot == kCallStaticVoidMethodVSlot)
                handler = kCallStaticVoidMethodV;
            else if (slot == kCallStaticVoidMethodASlot)
                handler = kCallStaticVoidMethodA;
            else if (slot == kExceptionOccurredSlot)
                handler = kExceptionOccurred;
            else if (slot == kExceptionClearSlot)
                handler = kExceptionClear;
            else if (slot == kThrowSlot)
                handler = kThrowThrowable;
            else if (slot == kThrowNewSlot)
                handler = kThrowNew;
            else if (slot == kFatalErrorSlot)
                handler = kFatalError;
            else if (slot == kPushLocalFrameSlot)
                handler = kPushLocalFrame;
            else if (slot == kPopLocalFrameSlot)
                handler = kPopLocalFrame;
            else if (slot == kRegisterNativesSlot)
                handler = kRegisterNatives;
            else if (slot == kUnregisterNativesSlot)
                handler = kUnregisterNatives;
            else if (slot == kGetJavaVmSlot)
                handler = kGetJavaVm;
            else if (slot == kExceptionCheckSlot)
                handler = kExceptionCheck;
            if (!environment->AssignSlot(slot, handler, *imports_, error))
                return false;
        }
    }
    if (!environment->Seal(error)) return false;
    environments_.emplace(thread_handle, std::move(environment));
    return true;
}

bool GuestJni::AttachCurrentThread(std::string* error) {
    const std::optional<std::uint64_t> handle =
        thread_state_ != nullptr ? thread_state_->CurrentErrnoAddress()
                                 : std::nullopt;
    if (!handle) {
        SetError(error, "guest JNI thread is not attached to the runtime");
        return false;
    }
    const std::lock_guard lock(mutex_);
    if (!initialized_) {
        SetError(error, "guest JNI is not initialized");
        return false;
    }
    if (environments_.contains(*handle)) return true;
    if (backend_ != nullptr && !backend_->AttachCurrentThread(error))
        return false;
    if (BuildEnvironmentLocked(*handle, error)) return true;
    if (backend_ != nullptr) {
        std::string ignored;
        (void)backend_->DetachCurrentThread(&ignored);
    }
    return false;
}

bool GuestJni::DetachCurrentThread(std::string* error) {
    const std::optional<std::uint64_t> handle =
        thread_state_ != nullptr ? thread_state_->CurrentErrnoAddress()
                                 : std::nullopt;
    if (!handle) {
        SetError(error, "guest JNI thread is not attached to the runtime");
        return false;
    }
    const std::lock_guard lock(mutex_);
    if (!initialized_) {
        SetError(error, "guest JNI is not initialized");
        return false;
    }
    if (!environments_.contains(*handle)) {
        SetError(error, "guest JNI environment is not attached");
        return false;
    }
    std::vector<std::uint64_t> owned_references;
    owned_references.reserve(local_reference_owners_.size());
    for (const auto& [reference, owner] : local_reference_owners_) {
        if (owner == *handle) owned_references.push_back(reference);
    }
    for (const std::uint64_t reference : owned_references) {
        if (!DeleteLocalReferenceLocked(reference, error)) return false;
    }
    local_reference_frames_.erase(*handle);
    if (backend_ != nullptr && !backend_->DetachCurrentThread(error))
        return false;
    environments_.erase(*handle);
    return true;
}

std::size_t GuestJni::LoadCompatOverrides(const std::string& path,
                                          std::string* error) {
    std::ifstream file(path);
    if (!file) return 0;  // absence is fine: policy stays fail-closed
    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line.front() == '#') continue;
        CompatOverride entry;
        std::size_t start = 0;
        for (int field = 0; field < 4; ++field) {
            const std::size_t bar = line.find('|', start);
            if (bar == std::string::npos) {
                SetError(error, "compat override line is malformed: " + line);
                return 0;
            }
            const std::string piece = line.substr(start, bar - start);
            switch (field) {
                case 0: entry.kind = piece; break;
                case 1: entry.class_type = piece; break;
                case 2: entry.name = piece; break;
                case 3: entry.signature = piece; break;
            }
            start = bar + 1;
        }
        entry.value = line.substr(start);
        compat_overrides_.push_back(std::move(entry));
    }
    compat_overrides_loaded_ = true;
    std::fprintf(stdout, "ISOMR_COMPAT_OVERRIDES count=%zu path=%s\n",
                 compat_overrides_.size(), path.c_str());
    return compat_overrides_.size();
}

static bool CompatFieldMatches(const std::string& pattern,
                               const std::string& value) {
    if (pattern.empty() || pattern == "*") return true;
    if (!pattern.empty() && pattern.back() == '*')
        return value.starts_with(
            pattern.substr(0, pattern.size() - 1));
    const auto strip = [](const std::string& t) {
        return t.size() >= 2 && t.front() == 'L' && t.back() == ';'
                   ? t.substr(1, t.size() - 2)
                   : t;
    };
    std::string p = strip(pattern);
    std::string v = strip(value);
    for (auto& c : p)
        if (c == '.') c = '/';
    for (auto& c : v)
        if (c == '.') c = '/';
    return p == v;
}

bool GuestJni::ApplyCompatOverride(
    const char* kind, const std::string& class_type,
    const std::string& name, const std::string& signature, void* registers_v,
    std::string* call_error) {
    auto* registers =
        static_cast<Aarch64ImportRegistry::Registers*>(registers_v);
    // The table stores plain kinds ("string", "object"); static call
    // handlers query the "static-" variant, so compare canonical kinds.
    const auto canonical_kind = [](std::string_view k) {
        const std::string_view prefix = "static-";
        return k.starts_with(prefix) ? k.substr(prefix.size()) : k;
    };
    const std::string queried_kind = std::string(canonical_kind(kind));
    for (const auto& entry : compat_overrides_) {
        if (canonical_kind(entry.kind) != queried_kind) continue;
        if (!CompatFieldMatches(entry.class_type, class_type)) continue;
        if (!CompatFieldMatches(entry.name, name)) continue;
        if (!CompatFieldMatches(entry.signature, signature)) continue;
        const std::string& value = entry.value;
        if (kind == std::string_view("void") ||
            kind == std::string_view("static-void")) {
            (*registers)[0] = 0;
            return true;
        }
        if (kind == std::string_view("boolean") ||
            kind == std::string_view("static-boolean")) {
            (*registers)[0] =
                (value == "true" || value == "1") ? 1U : 0U;
            return true;
        }
        if (kind == std::string_view("int") ||
            kind == std::string_view("static-int") ||
            kind == std::string_view("long") ||
            kind == std::string_view("static-long")) {
            (*registers)[0] =
                static_cast<std::uint64_t>(std::strtoull(value.c_str(),
                                                         nullptr, 0));
            return true;
        }
        if (kind == std::string_view("float")) {
            registers->SetFloatResult(std::strtof(value.c_str(), nullptr));
            return true;
        }
        if (kind == std::string_view("double")) {
            (*registers).vector[0] = {};
            (*registers).vector[0][0] = std::bit_cast<std::uint64_t>(
                std::strtod(value.c_str(), nullptr));
            return true;
        }
        if (kind == std::string_view("string") ||
            kind == std::string_view("static-string")) {
            std::fprintf(stderr,
                         "ISOMR_COMPAT_STRING class=%s name=%s value=%s\n",
                         class_type.c_str(), name.c_str(), value.c_str());
            const std::uint64_t reference = AllocateCompatibilityReference(
                heap_, CompatibilityReferenceKind::kString,
                "Ljava/lang/String;", value, call_error);
            if (reference == 0) return false;
            object_references_.emplace(
                reference,
                ObjectReference{reference, 0, "Ljava/lang/String;", value, 0});
            TrackLocalReferenceLocked(reference);
            (*registers)[0] = reference;
            return true;
        }
        if (kind == std::string_view("object") ||
            kind == std::string_view("static-object")) {
            // A value starting with '[' requests an empty array of that
            // element type instead of a plain typed object.
            const bool is_array = !value.empty() && value.front() == '[';
            const std::string array_type =
                is_array ? value : "[L" + value + ";";
            const std::uint64_t reference = AllocateCompatibilityReference(
                heap_, CompatibilityReferenceKind::kObject, array_type, {},
                call_error);
            if (reference == 0) return false;
            object_references_.emplace(
                reference,
                ObjectReference{reference, 0, array_type, {}, 0});
            TrackLocalReferenceLocked(reference);
            if (is_array)
                object_arrays_.emplace(
                    reference,
                    ObjectArrayStorage{array_type, value, {}});
            (*registers)[0] = reference;
            return true;
        }
        if (kind == std::string_view("array") ||
            kind == std::string_view("static-array")) {
            const std::string array_type = "[L" + value + ";";
            const std::uint64_t reference = AllocateCompatibilityReference(
                heap_, CompatibilityReferenceKind::kObject, array_type, {},
                call_error);
            if (reference == 0) return false;
            object_references_.emplace(
                reference,
                ObjectReference{reference, 0, array_type, {}, 0});
            TrackLocalReferenceLocked(reference);
            object_arrays_.emplace(
                reference,
                ObjectArrayStorage{array_type, "L" + value + ";", {}});
            (*registers)[0] = reference;
            return true;
        }
        SetError(call_error,
                 std::string("compat override kind is unknown: ") + kind);
        return false;
    }
    // No flat-file override matched: consult the per-game script (mutex_ is
    // held, so the handler only uses the context's locked ops).
    if (scripted_jni_handler_) {
        ScriptedJniContext context{};
        context.kind = kind;
        context.class_type = class_type;
        context.name = name;
        context.signature = signature;
        context.receiver = (*registers)[1];
        context.method_ref = (*registers)[2];
        context.args_source = (*registers)[3];
        context.registers_v = registers;
        context.call_error = call_error;
        context.jni = this;
        if (scripted_jni_handler_(context)) return true;
        if (call_error != nullptr && !call_error->empty()) return false;
    }
    return false;
}

bool GuestJni::CreateCompatibilityClassReference(
    std::string_view type, std::uint64_t* reference, std::string* error) {
    if (reference != nullptr) *reference = 0;
    const std::lock_guard lock(mutex_);
    if (backend_ != nullptr) {
        SetError(error,
                 "compatibility class references are unavailable with a desktop JVM backend");
        return false;
    }
    const std::optional<std::uint64_t> thread =
        thread_state_ != nullptr ? thread_state_->CurrentErrnoAddress()
                                 : std::nullopt;
    if (!initialized_ || !thread || !environments_.contains(*thread) ||
        reference == nullptr ||
        !IsBoundedMetadata(type, 1024)) {
        SetError(error, "compatibility class reference type is invalid");
        return false;
    }
    const std::uint64_t allocation = AllocateCompatibilityReference(
        heap_, CompatibilityReferenceKind::kClass, type, {}, error);
    if (allocation == 0) {
        SetError(error, "compatibility class reference allocation failed");
        return false;
    }
    std::uint64_t identity = allocation;
    const auto local_same_type = std::find_if(
        class_references_.begin(), class_references_.end(),
        [&](const auto& candidate) { return candidate.second.type == type; });
    if (local_same_type != class_references_.end()) {
        identity = local_same_type->second.identity;
    } else {
        const auto global_same_type = std::find_if(
            global_class_references_.begin(),
            global_class_references_.end(), [&](const auto& candidate) {
                return candidate.second.type == type;
            });
        if (global_same_type != global_class_references_.end())
            identity = global_same_type->second.identity;
    }
    class_references_.emplace(
        allocation, ClassReference{identity, std::string(type), 0});
    TrackLocalReferenceLocked(allocation);
    *reference = allocation;
    return true;
}

bool GuestJni::CreateCompatibilityTypedObjectReference(
    std::string_view type, std::uint64_t* reference, std::string* error) {
    if (reference != nullptr) *reference = 0;
    const std::lock_guard lock(mutex_);
    if (backend_ != nullptr) {
        SetError(error,
                 "compatibility object references are unavailable with a desktop JVM backend");
        return false;
    }
    const std::optional<std::uint64_t> thread =
        thread_state_ != nullptr ? thread_state_->CurrentErrnoAddress()
                                 : std::nullopt;
    if (!initialized_ || !thread || !environments_.contains(*thread) ||
        reference == nullptr ||
        !IsBoundedMetadata(type, 1024)) {
        SetError(error, "compatibility object reference type is invalid");
        return false;
    }
    const std::uint64_t allocation = AllocateCompatibilityReference(
        heap_, CompatibilityReferenceKind::kObject, type, {}, error);
    if (allocation == 0) {
        SetError(error, "compatibility object reference allocation failed");
        return false;
    }
    object_references_.emplace(
        allocation,
        ObjectReference{allocation, 0, std::string(type), {}, 0});
    TrackLocalReferenceLocked(allocation);
    *reference = allocation;
    return true;
}

bool GuestJni::CreateCompatibilityTypedObjectReferenceLocked(
    std::string_view type, std::uint64_t* reference, std::string* error) {
    if (reference != nullptr) *reference = 0;
    if (backend_ != nullptr) {
        SetError(error,
                 "compatibility object references are unavailable with a desktop JVM backend");
        return false;
    }
    const std::optional<std::uint64_t> thread =
        thread_state_ != nullptr ? thread_state_->CurrentErrnoAddress()
                                 : std::nullopt;
    if (!initialized_ || !thread || !environments_.contains(*thread) ||
        reference == nullptr || !IsBoundedMetadata(type, 1024)) {
        SetError(error, "compatibility object reference type is invalid");
        return false;
    }
    const std::uint64_t allocation = AllocateCompatibilityReference(
        heap_, CompatibilityReferenceKind::kObject, type, {}, error);
    if (allocation == 0) {
        SetError(error, "compatibility object reference allocation failed");
        return false;
    }
    object_references_.emplace(
        allocation,
        ObjectReference{allocation, 0, std::string(type), {}, 0});
    TrackLocalReferenceLocked(allocation);
    *reference = allocation;
    return true;
}

// ---- ScriptedJniContext (locked-context helpers for game scripts) --------

void GuestJni::set_scripted_jni_handler(
    std::function<bool(ScriptedJniContext&)> handler) {
    scripted_jni_handler_ = std::move(handler);
}

bool ScriptedJniContext::Arg(std::size_t index, std::uint64_t* value,
                             std::string* error) const {
    if (jni == nullptr || args_source == 0) {
        SetError(error, "scripted jni call has no argument source");
        return false;
    }
    return ReadGuestGeneralArgument(jni->address_space_, args_source, value,
                                    error, index);
}

void ScriptedJniContext::SetResult(std::uint64_t value) {
    auto* registers =
        static_cast<Aarch64ImportRegistry::Registers*>(registers_v);
    (*registers)[0] = value;
}

void ScriptedJniContext::SetFloatResult(float value) {
    auto* registers =
        static_cast<Aarch64ImportRegistry::Registers*>(registers_v);
    registers->SetFloatResult(value);
}

void ScriptedJniContext::SetDoubleResult(double value) {
    auto* registers =
        static_cast<Aarch64ImportRegistry::Registers*>(registers_v);
    registers->vector[0] = {};
    registers->vector[0][0] = std::bit_cast<std::uint64_t>(value);
}

bool ScriptedJniContext::CreateString(std::string_view value,
                                      std::uint64_t* reference,
                                      std::string* error) {
    return jni->CreateCompatibilityStringReferenceLocked(value, reference,
                                                         error);
}

bool ScriptedJniContext::CreateObject(std::string_view type,
                                      std::uint64_t* reference,
                                      std::string* error) {
    return jni->CreateCompatibilityTypedObjectReferenceLocked(type, reference,
                                                              error);
}

bool ScriptedJniContext::ReadString(std::uint64_t reference,
                                    std::string* value,
                                    std::string* error) const {
    const auto local = jni->object_references_.find(reference);
    if (local != jni->object_references_.end()) {
        *value = local->second.string_value;
        return true;
    }
    const auto global = jni->global_object_references_.find(reference);
    if (global != jni->global_object_references_.end()) {
        *value = global->second.string_value;
        return true;
    }
    SetError(error, "scripted jni string read: reference is untracked");
    return false;
}

bool ScriptedJniContext::WriteObjectString(std::uint64_t reference,
                                           std::string_view value,
                                           std::string* error) {
    const auto local = jni->object_references_.find(reference);
    if (local != jni->object_references_.end()) {
        local->second.string_value = value;
        return true;
    }
    const auto global = jni->global_object_references_.find(reference);
    if (global != jni->global_object_references_.end()) {
        global->second.string_value = value;
        return true;
    }
    SetError(error, "scripted jni string write: reference is untracked");
    return false;
}

std::optional<std::string> ScriptedJniContext::ObjectType(
    std::uint64_t reference) const {
    const auto local = jni->object_references_.find(reference);
    if (local != jni->object_references_.end()) return local->second.type;
    const auto global = jni->global_object_references_.find(reference);
    if (global != jni->global_object_references_.end())
        return global->second.type;
    const auto local_class = jni->class_references_.find(reference);
    if (local_class != jni->class_references_.end())
        return local_class->second.type;
    const auto global_class = jni->global_class_references_.find(reference);
    if (global_class != jni->global_class_references_.end())
        return global_class->second.type;
    return std::nullopt;
}

bool GuestJni::CreateCompatibilityStringReferenceLocked(
    std::string_view value, std::uint64_t* reference, std::string* error) {
    if (reference != nullptr) *reference = 0;
    if (backend_ != nullptr) {
        SetError(error,
                 "compatibility string references are unavailable with a desktop JVM backend");
        return false;
    }
    const std::optional<std::uint64_t> thread =
        thread_state_ != nullptr ? thread_state_->CurrentErrnoAddress()
                                 : std::nullopt;
    if (!initialized_ || !thread || !environments_.contains(*thread) ||
        reference == nullptr || value.size() > 4096 ||
        value.find('\0') != std::string_view::npos) {
        SetError(error, "compatibility string reference value is invalid");
        return false;
    }
    const std::uint64_t allocation = AllocateCompatibilityReference(
        heap_, CompatibilityReferenceKind::kString, "Ljava/lang/String;",
        value, error);
    if (allocation == 0) {
        SetError(error, "compatibility string reference allocation failed");
        return false;
    }
    object_references_.emplace(
        allocation,
        ObjectReference{allocation, 0, "Ljava/lang/String;",
                        std::string(value), 0});
    TrackLocalReferenceLocked(allocation);
    *reference = allocation;
    return true;
}

bool GuestJni::CreateCompatibilityStringReference(
    std::string_view value, std::uint64_t* reference, std::string* error) {
    const std::lock_guard lock(mutex_);
    return CreateCompatibilityStringReferenceLocked(value, reference, error);
}

bool GuestJni::CreateCompatibilityReflectedMethodReference(
    std::uint64_t declaring_class_reference, std::string_view name,
    std::string_view signature, std::uint64_t* reference,
    std::string* error) {
    if (reference != nullptr) *reference = 0;
    const std::lock_guard lock(mutex_);
    const std::optional<std::uint64_t> class_identity =
        ReferenceIdentityLocked(declaring_class_reference);
    const std::optional<std::string_view> class_type =
        ClassTypeLocked(declaring_class_reference);
    const std::optional<std::uint64_t> thread =
        thread_state_ != nullptr ? thread_state_->CurrentErrnoAddress()
                                 : std::nullopt;
    if (backend_ != nullptr || !initialized_ || !thread ||
        !environments_.contains(*thread) || reference == nullptr ||
        !class_identity || !class_type ||
        !IsClassReferenceLocked(declaring_class_reference) ||
        !IsBoundedMetadata(name, 1024) || !IsMethodSignature(signature)) {
        SetError(error,
                 "compatibility reflected method arguments are invalid");
        return false;
    }
    std::uint64_t method_reference = 0;
    for (const auto& [candidate, method] : method_references_) {
        if (!method.is_static && method.name == name &&
            method.signature == signature &&
            ReceiverClassMatches(method.class_type, *class_type)) {
            method_reference = candidate;
            break;
        }
    }
    bool created_method = false;
    if (method_reference == 0) {
        method_reference = heap_->Callocate(1, sizeof(std::uint64_t));
        if (method_reference == 0) {
            SetError(error,
                     "compatibility reflected method id allocation failed");
            return false;
        }
        method_references_.emplace(
            method_reference,
            MethodReference{*class_identity, std::string(*class_type),
                            std::string(name), std::string(signature), false,
                            0});
        created_method = true;
    }
    constexpr std::string_view kMethodType =
        "Ljava/lang/reflect/Method;";
    const std::uint64_t allocation = AllocateCompatibilityReference(
        heap_, CompatibilityReferenceKind::kObject, kMethodType, {}, error);
    if (allocation == 0) {
        if (created_method) {
            method_references_.erase(method_reference);
            std::string ignored;
            (void)heap_->Release(method_reference, &ignored);
        }
        SetError(error,
                 "compatibility reflected method object allocation failed");
        return false;
    }
    object_references_.emplace(
        allocation,
        ObjectReference{allocation, method_reference,
                        std::string(kMethodType), {}, 0});
    TrackLocalReferenceLocked(allocation);
    *reference = allocation;
    return true;
}

bool GuestJni::CreateCompatibilityEmptyObjectArrayReference(
    std::uint64_t* reference, std::string* error) {
    if (reference != nullptr) *reference = 0;
    const std::lock_guard lock(mutex_);
    const std::optional<std::uint64_t> thread =
        thread_state_ != nullptr ? thread_state_->CurrentErrnoAddress()
                                 : std::nullopt;
    if (backend_ != nullptr || !initialized_ || !thread ||
        !environments_.contains(*thread) || reference == nullptr) {
        SetError(error,
                 "compatibility empty object array arguments are invalid");
        return false;
    }
    constexpr std::string_view kArrayType = "[Ljava/lang/Object;";
    constexpr std::string_view kElementType = "Ljava/lang/Object;";
    const std::uint64_t allocation = AllocateCompatibilityReference(
        heap_, CompatibilityReferenceKind::kObject, kArrayType, {}, error);
    if (allocation == 0) {
        SetError(error,
                 "compatibility empty object array allocation failed");
        return false;
    }
    object_references_.emplace(
        allocation,
        ObjectReference{allocation, 0, std::string(kArrayType), {}, 0});
    object_arrays_.emplace(
        allocation,
        ObjectArrayStorage{std::string(kArrayType),
                           std::string(kElementType), {}});
    TrackLocalReferenceLocked(allocation);
    *reference = allocation;
    return true;
}

bool GuestJni::CreateCompatibilitySingleObjectArrayReference(
    std::uint64_t element, std::uint64_t* reference, std::string* error) {
    if (reference != nullptr) *reference = 0;
    const std::lock_guard lock(mutex_);
    const std::optional<std::uint64_t> thread =
        thread_state_ != nullptr ? thread_state_->CurrentErrnoAddress()
                                 : std::nullopt;
    if (backend_ != nullptr || !initialized_ || !thread ||
        !environments_.contains(*thread) || reference == nullptr) {
        SetError(error,
                 "compatibility object array arguments are invalid");
        return false;
    }
    constexpr std::string_view kArrayType = "[Ljava/lang/Object;";
    constexpr std::string_view kElementType = "Ljava/lang/Object;";
    const std::uint64_t allocation = AllocateCompatibilityReference(
        heap_, CompatibilityReferenceKind::kObject, kArrayType, {}, error);
    if (allocation == 0) {
        SetError(error,
                 "compatibility object array allocation failed");
        return false;
    }
    object_references_.emplace(
        allocation,
        ObjectReference{allocation, 0, std::string(kArrayType), {}, 0});
    std::optional<ObjectArrayElement> snapshot;
    const std::optional<std::uint64_t> element_identity =
        ReferenceIdentityLocked(element);
    if (element != 0 && element_identity) {
        const auto local_object =
            FindOwnedLocalObjectReferenceLocked(element);
        const auto global_object =
            global_object_references_.find(element);
        const bool local = local_object != object_references_.end();
        if (local || global_object != global_object_references_.end()) {
            const std::uint64_t method =
                local ? local_object->second.method_reference
                      : global_object->second.method_reference;
            const std::string& type =
                local ? local_object->second.type
                      : global_object->second.type;
            const std::string& value =
                local ? local_object->second.string_value
                      : global_object->second.string_value;
            snapshot = ObjectArrayElement{false, *element_identity, method,
                                          type, value};
        }
    }
    object_arrays_.emplace(
        allocation,
        ObjectArrayStorage{std::string(kArrayType),
                           std::string(kElementType), {snapshot}});
    TrackLocalReferenceLocked(allocation);
    *reference = allocation;
    return true;
}

bool GuestJni::CreateCompatibilityLongReference(
    std::int64_t value, std::uint64_t* reference, std::string* error) {
    if (reference != nullptr) *reference = 0;
    const std::lock_guard lock(mutex_);
    const std::optional<std::uint64_t> thread =
        thread_state_ != nullptr ? thread_state_->CurrentErrnoAddress()
                                 : std::nullopt;
    if (backend_ != nullptr || !initialized_ || !thread ||
        !environments_.contains(*thread) || reference == nullptr) {
        SetError(error, "compatibility Long arguments are invalid");
        return false;
    }
    // Mirrors the Long.valueOf dispatch: the decimal value lives in the
    // reference's string storage so longValue()/toString() read it back.
    const std::string decimal = std::to_string(value);
    const std::uint64_t allocation = AllocateCompatibilityReference(
        heap_, CompatibilityReferenceKind::kObject, "Ljava/lang/Long;",
        decimal, error);
    if (allocation == 0) {
        SetError(error, "compatibility Long allocation failed");
        return false;
    }
    object_references_.emplace(
        allocation,
        ObjectReference{allocation, 0, "Ljava/lang/Long;", decimal, 0});
    TrackLocalReferenceLocked(allocation);
    *reference = allocation;
    return true;
}

bool GuestJni::CreateCompatibilityLongArrayReference(
    std::vector<std::uint64_t> elements, std::uint64_t* reference,
    std::string* error) {
    if (reference != nullptr) *reference = 0;
    const std::uint64_t allocation = AllocateCompatibilityReference(
        heap_, CompatibilityReferenceKind::kObject, "[J", {}, error);
    if (allocation == 0) {
        SetError(error, "compatibility long array allocation failed");
        return false;
    }
    const std::lock_guard lock(mutex_);
    object_references_.emplace(
        allocation, ObjectReference{allocation, 0, "[J", {}, 0});
    long_arrays_.emplace(allocation, std::move(elements));
    TrackLocalReferenceLocked(allocation);
    *reference = allocation;
    return true;
}

bool GuestJni::CreateCompatibilityIntArrayReference(
    std::vector<std::uint32_t> elements, std::uint64_t* reference,
    std::string* error) {
    if (reference != nullptr) *reference = 0;
    const std::uint64_t allocation = AllocateCompatibilityReference(
        heap_, CompatibilityReferenceKind::kObject, "[I", {}, error);
    if (allocation == 0) {
        SetError(error, "compatibility int array allocation failed");
        return false;
    }
    const std::lock_guard lock(mutex_);
    object_references_.emplace(
        allocation, ObjectReference{allocation, 0, "[I", {}, 0});
    int_arrays_.emplace(allocation, std::move(elements));
    TrackLocalReferenceLocked(allocation);
    *reference = allocation;
    return true;
}

bool GuestJni::CreateCompatibilityByteArrayReference(
    std::vector<std::uint8_t> elements, std::uint64_t* reference,
    std::string* error) {
    if (reference != nullptr) *reference = 0;
    const std::uint64_t allocation = AllocateCompatibilityReference(
        heap_, CompatibilityReferenceKind::kObject, "[B", {}, error);
    if (allocation == 0) {
        SetError(error, "compatibility byte array allocation failed");
        return false;
    }
    const std::lock_guard lock(mutex_);
    object_references_.emplace(
        allocation, ObjectReference{allocation, 0, "[B", {}, 0});
    byte_arrays_.emplace(allocation, std::move(elements));
    TrackLocalReferenceLocked(allocation);
    *reference = allocation;
    return true;
}

bool GuestJni::CreateCompatibilityMotionEventReference(
    int action, float x, float y, std::int64_t event_time_ms,
    std::uint64_t* reference, std::string* error) {
    if (reference != nullptr) *reference = 0;
    const std::uint64_t allocation = AllocateCompatibilityReference(
        heap_, CompatibilityReferenceKind::kObject,
        "android/view/MotionEvent", {}, error);
    if (allocation == 0) {
        SetError(error, "compatibility MotionEvent allocation failed");
        return false;
    }
    const std::lock_guard lock(mutex_);
    object_references_.emplace(
        allocation,
        ObjectReference{allocation, 0, "android/view/MotionEvent", {}, 0});
    motion_events_.emplace(
        allocation, MotionEventStorage{action, x, y, event_time_ms});
    TrackLocalReferenceLocked(allocation);
    *reference = allocation;
    return true;
}

bool GuestJni::ReleaseCompatibilityLocalReference(
    std::uint64_t reference, std::string* error) {
    {
        const std::lock_guard lock(mutex_);
        if (backend_ != nullptr) {
            SetError(error,
                     "compatibility local reference release is unavailable with a desktop JVM backend");
            return false;
        }
        if (!initialized_ || reference == 0) {
            SetError(error, "compatibility local reference is invalid");
            return false;
        }
    }
    return DeleteLocalReference(reference, error);
}

bool GuestJni::CloseInputStreamLocked(std::uint64_t identity,
                                      std::string* error) {
    const auto stream = input_streams_.find(identity);
    if (stream == input_streams_.end()) return true;
    if (stream->second.descriptor >= 0 &&
        files_->Close(stream->second.descriptor) != 0) {
        SetError(error, "guest JNI InputStream descriptor close failed");
        return false;
    }
    input_streams_.erase(stream);
    return true;
}

bool GuestJni::IdentityHasAttachmentsLocked(std::uint64_t identity) const {
    return input_streams_.contains(identity) ||
           byte_arrays_.contains(identity) ||
           int_arrays_.contains(identity) ||
           long_arrays_.contains(identity) ||
           object_arrays_.contains(identity) ||
           http_tasks_.contains(identity) ||
           interface_proxies_.contains(identity) ||
           scanners_.contains(identity) ||
           shared_preferences_editors_.contains(identity) ||
           alert_dialog_builders_.contains(identity) ||
           handler_callbacks_.contains(identity) ||
           message_callbacks_.contains(identity) ||
           std::any_of(choreographer_callbacks_.begin(),
                       choreographer_callbacks_.end(),
                       [identity](const auto& entry) {
                           return entry.first == identity;
                       }) ||
           std::any_of(display_listeners_.begin(), display_listeners_.end(),
                       [identity](const auto& entry) {
                           return entry.first == identity;
                       });
}

bool GuestJni::EraseIdentityAttachmentsLocked(std::uint64_t identity,
                                              std::string* error) {
    if (!CloseInputStreamLocked(identity, error)) return false;
    byte_arrays_.erase(identity);
    int_arrays_.erase(identity);
    long_arrays_.erase(identity);
    object_arrays_.erase(identity);
    http_tasks_.erase(identity);
    interface_proxies_.erase(identity);
    scanners_.erase(identity);
    shared_preferences_editors_.erase(identity);
    alert_dialog_builders_.erase(identity);
    handler_callbacks_.erase(identity);
    message_callbacks_.erase(identity);
    std::erase_if(choreographer_callbacks_,
                  [identity](const auto& entry) {
                      return entry.first == identity;
                  });
    std::erase_if(display_listeners_, [identity](const auto& entry) {
        return entry.first == identity;
    });
    return true;
}

bool GuestJni::Shutdown(std::string* error) {
    const std::lock_guard lock(mutex_);
    if (!initialized_) {
        SetError(error, "guest JNI is not initialized");
        return false;
    }
    if (!environments_.empty()) {
        SetError(error, "guest JNI still has attached environments");
        return false;
    }
    for (const auto& [buffer, metadata] : byte_array_buffers_) {
        static_cast<void>(metadata);
        if (!heap_->Release(buffer, error)) return false;
    }
    byte_array_buffers_.clear();
    for (const auto& [buffer, metadata] : int_array_buffers_) {
        static_cast<void>(metadata);
        if (!heap_->Release(buffer, error)) return false;
    }
    int_array_buffers_.clear();
    for (const auto& [buffer, metadata] : long_array_buffers_) {
        static_cast<void>(metadata);
        if (!heap_->Release(buffer, error)) return false;
    }
    long_array_buffers_.clear();
    for (const auto& [buffer, identity] :
         string_utf_character_buffers_) {
        static_cast<void>(identity);
        if (!heap_->Release(buffer, error)) return false;
    }
    string_utf_character_buffers_.clear();
    for (const auto& [buffer, identity] : string_character_buffers_) {
        static_cast<void>(identity);
        if (!heap_->Release(buffer, error)) return false;
    }
    string_character_buffers_.clear();
    byte_arrays_.clear();
    int_arrays_.clear();
    long_arrays_.clear();
    object_arrays_.clear();
    interface_proxies_.clear();
    pending_ui_tasks_.clear();
    handler_callbacks_.clear();
    message_callbacks_.clear();
    scanners_.clear();
    shared_preferences_editors_.clear();
    shared_preferences_ints_.clear();
    alert_dialog_builders_.clear();
    while (!input_streams_.empty()) {
        if (!CloseInputStreamLocked(input_streams_.begin()->first, error))
            return false;
    }
    local_reference_frames_.clear();
    local_reference_owners_.clear();
    registered_native_methods_.clear();
    next_registered_native_id_ = 1;
    for (const auto& [reference, value] : global_object_references_) {
        if (backend_ != nullptr &&
            !backend_->DeleteGlobalRef(value.backend, error))
            return false;
        if (!heap_->Release(reference, error)) return false;
    }
    global_object_references_.clear();
    for (const auto& [reference, value] : global_class_references_) {
        if (backend_ != nullptr &&
            !backend_->DeleteGlobalRef(value.backend, error))
            return false;
        if (!heap_->Release(reference, error)) return false;
    }
    global_class_references_.clear();
    for (const auto& [reference, object] : object_references_) {
        if (backend_ != nullptr &&
            !backend_->DeleteLocalRef(object.backend, error))
            return false;
        if (!heap_->Release(reference, error)) return false;
    }
    object_references_.clear();
    for (const auto& [reference, field] : field_references_) {
        static_cast<void>(field);
        if (!heap_->Release(reference, error)) return false;
    }
    field_references_.clear();
    for (const auto& [reference, method] : method_references_) {
        if (backend_ != nullptr &&
            !backend_->DeleteMethod(method.backend, error))
            return false;
        if (!heap_->Release(reference, error)) return false;
    }
    method_references_.clear();
    for (const auto& [reference, class_reference] : class_references_) {
        if (backend_ != nullptr &&
            !backend_->DeleteLocalRef(class_reference.backend, error))
            return false;
        if (!heap_->Release(reference, error)) return false;
    }
    class_references_.clear();
    for (const std::uint64_t block : retained_reference_blocks_) {
        if (!heap_->Release(block, error)) return false;
    }
    retained_reference_blocks_.clear();
    deleted_class_references_.clear();
    uncaught_exception_build_id_.clear();
    allowed_screen_orientations_.reset();
    platform_setup_.reset();
    advertising_id_native_pointer_.reset();
    advertising_info_requested_ = false;
    analytics_value_key_.reset();
    pending_analytics_value_requests_.clear();
    analytics_value_request_sequence_ = 0;
    pending_age_signals_requests_.clear();
    age_signals_request_sequence_ = 0;
    pending_popup_dialog_requests_.clear();
    popup_dialog_request_sequence_ = 0;
    firebase_analytics_configuration_.reset();
    firebase_analytics_collection_enabled_.reset();
    referrer_fetch_native_pointer_.reset();
    web_view_user_agent_native_pointer_.reset();
    http_headers_.clear();
    http_tasks_.clear();
    last_http_cancel_request_id_.reset();
    http_lifecycle_events_.fill({});
    http_lifecycle_event_count_ = 0;
    http_lifecycle_event_next_ = 0;
    http_lifecycle_event_sequence_ = 0;
    http_observation_events_.fill({});
    http_observation_event_count_ = 0;
    http_observation_event_next_ = 0;
    http_observation_event_sequence_ = 0;
    http_observation_owner_thread_hash_ = 0;
    device_impl_started_ = false;
    device_screen_info_requested_ = false;
    platform_splash_visible_.reset();
    haptics_in_external_display_enabled_.reset();
    haptics_vibrate_requests_ = 0;
    exception_pending().pending = false;
    exception_pending().type.clear();
    exception_pending().message.clear();
    exception_pending().reference = 0;
    keep_screen_on_.reset();
    startup_loading_finished_ = false;
    input_map_provider_initialized_ = false;
    app_not_responding_ui_task_requested_ = false;
    app_not_responding_task_processor_active_ = false;
    ads_platform_activity_identity_.reset();
    ads_network_listener_native_pointer_.reset();
    google_billing_connect_requested_ = false;
    facebook_sdk_teardown_requested_ = false;
    ads_http_reset_requested_ = false;
    braze_configuration_.reset();
    notification_token_provider_disconnected_ = false;
    notification_cancel_all_requested_ = false;
    local_notification_cancel_all_requested_ = false;
    local_notification_schedule_.reset();
    ff_base_sdk_initialized_ = false;
    vm_.reset();
    imports_ = nullptr;
    initialized_ = false;
    return true;
}

std::uint64_t GuestJni::vm_address() const {
    const std::lock_guard lock(mutex_);
    return vm_ != nullptr ? vm_->object_address() : 0;
}

std::size_t GuestJni::registered_native_count() const {
    const std::lock_guard lock(mutex_);
    return registered_native_methods_.size();
}

std::vector<GuestJni::RegisteredNativeDescriptor>
GuestJni::RegisteredNativesForClass(std::string_view class_type) const {
    const std::lock_guard lock(mutex_);
    std::vector<RegisteredNativeDescriptor> descriptors;
    for (const RegisteredNativeMethod& method : registered_native_methods_) {
        if (method.class_type != class_type) continue;
        descriptors.push_back(RegisteredNativeDescriptor{
            method.class_type, method.name, method.signature});
    }
    return descriptors;
}

std::vector<GuestJni::RegisteredNativeDescriptor>
GuestJni::AllRegisteredNatives() const {
    const std::lock_guard lock(mutex_);
    std::vector<RegisteredNativeDescriptor> descriptors;
    descriptors.reserve(registered_native_methods_.size());
    for (const RegisteredNativeMethod& method : registered_native_methods_) {
        descriptors.push_back(RegisteredNativeDescriptor{
            method.class_type, method.name, method.signature});
    }
    return descriptors;
}

std::optional<GuestJni::RegisteredNativeHandle>
GuestJni::FindRegisteredNative(std::string_view class_type,
                               std::string_view name,
                               std::string_view signature) const {
    const std::lock_guard lock(mutex_);
    const auto found = std::find_if(
        registered_native_methods_.begin(), registered_native_methods_.end(),
        [&](const RegisteredNativeMethod& method) {
            return method.class_type == class_type && method.name == name &&
                   method.signature == signature;
        });
    return found != registered_native_methods_.end()
               ? std::optional<RegisteredNativeHandle>(found->handle)
               : std::nullopt;
}

std::optional<std::uint64_t> GuestJni::FindRegisteredNativeFunction(
    std::string_view class_type, std::string_view name,
    std::string_view signature) const {
    const auto handle =
        FindRegisteredNative(class_type, name, signature);
    if (!handle) return std::nullopt;
    std::uint64_t function = 0;
    if (!ResolveRegisteredNativeTarget(*handle, &function, nullptr))
        return std::nullopt;
    return function;
}

bool GuestJni::ResolveRegisteredNativeTarget(RegisteredNativeHandle handle,
                                             std::uint64_t* function,
                                             std::string* error) const {
    const std::lock_guard lock(mutex_);
    const auto found = std::find_if(
        registered_native_methods_.begin(), registered_native_methods_.end(),
        [&](const RegisteredNativeMethod& method) {
            return method.handle == handle;
        });
    if (function == nullptr || handle.id == 0 || handle.generation == 0 ||
        found == registered_native_methods_.end()) {
        SetError(error,
                 "registered guest JNI native handle is stale or invalid");
        return false;
    }
    *function = found->function;
    return true;
}

std::optional<std::uint64_t> GuestJni::CurrentEnvironmentAddress() const {
    const std::optional<std::uint64_t> handle =
        thread_state_ != nullptr ? thread_state_->CurrentErrnoAddress()
                                 : std::nullopt;
    if (!handle) return std::nullopt;
    const std::lock_guard lock(mutex_);
    const auto found = environments_.find(*handle);
    return found != environments_.end()
               ? std::optional<std::uint64_t>(
                     found->second->object_address())
               : std::nullopt;
}

void GuestJni::MirrorPendingExceptionLocked() {
    // Caller must hold mutex_. Publishes the thread_local pending flag into
    // the calling thread's env-block spare slot for the inline
    // ExceptionCheck cell. A thread with no env yet (or an unattached host
    // thread) simply has nothing to mirror.
    if (backend_ != nullptr || thread_state_ == nullptr) return;
    const std::optional<std::uint64_t> handle =
        thread_state_->CurrentErrnoAddress();
    if (!handle) return;
    const auto found = environments_.find(*handle);
    if (found == environments_.end() || found->second == nullptr) return;
    found->second->SetExceptionFlagByte(exception_pending().pending ? 1 : 0);
}

std::size_t GuestJni::environment_count() const {
    const std::lock_guard lock(mutex_);
    return environments_.size();
}

std::size_t GuestJni::class_reference_count() const {
    const std::lock_guard lock(mutex_);
    return class_references_.size();
}

std::size_t GuestJni::method_reference_count() const {
    const std::lock_guard lock(mutex_);
    return method_references_.size();
}

std::size_t GuestJni::field_reference_count() const {
    const std::lock_guard lock(mutex_);
    return field_references_.size();
}

std::size_t GuestJni::object_reference_count() const {
    const std::lock_guard lock(mutex_);
    return object_references_.size();
}

std::size_t GuestJni::global_reference_count() const {
    const std::lock_guard lock(mutex_);
    return global_class_references_.size() +
           global_object_references_.size();
}

std::size_t GuestJni::string_character_buffer_count() const {
    const std::lock_guard lock(mutex_);
    return string_character_buffers_.size();
}

std::size_t GuestJni::string_utf_character_buffer_count() const {
    const std::lock_guard lock(mutex_);
    return string_utf_character_buffers_.size();
}

std::size_t GuestJni::byte_array_count() const {
    const std::lock_guard lock(mutex_);
    return byte_arrays_.size();
}

std::size_t GuestJni::byte_array_buffer_count() const {
    const std::lock_guard lock(mutex_);
    return byte_array_buffers_.size();
}

std::size_t GuestJni::object_array_count() const {
    const std::lock_guard lock(mutex_);
    return object_arrays_.size();
}

std::string GuestJni::uncaught_exception_build_id() const {
    const std::lock_guard lock(mutex_);
    return uncaught_exception_build_id_;
}

std::optional<std::array<std::uint8_t, 4>>
GuestJni::allowed_screen_orientations() const {
    const std::lock_guard lock(mutex_);
    return allowed_screen_orientations_;
}

std::optional<std::array<std::int32_t, 5>>
GuestJni::platform_setup() const {
    const std::lock_guard lock(mutex_);
    return platform_setup_;
}

std::optional<std::uint64_t>
GuestJni::advertising_id_native_pointer() const {
    const std::lock_guard lock(mutex_);
    return advertising_id_native_pointer_;
}

bool GuestJni::advertising_info_requested() const {
    const std::lock_guard lock(mutex_);
    return advertising_info_requested_;
}

std::optional<std::string> GuestJni::analytics_value_key() const {
    const std::lock_guard lock(mutex_);
    return analytics_value_key_;
}

std::size_t GuestJni::pending_analytics_value_request_count() const {
    const std::lock_guard lock(mutex_);
    return pending_analytics_value_requests_.size();
}

std::optional<GuestJni::AnalyticsValueRequest>
GuestJni::NextPendingAnalyticsValueRequest() const {
    const std::lock_guard lock(mutex_);
    if (pending_analytics_value_requests_.empty()) return std::nullopt;
    return pending_analytics_value_requests_.front();
}

bool GuestJni::AcknowledgeAnalyticsValueRequest(
    std::uint64_t sequence, std::string* error) {
    const std::lock_guard lock(mutex_);
    if (pending_analytics_value_requests_.empty() ||
        pending_analytics_value_requests_.front().sequence != sequence) {
        SetError(error,
                 "guest JNI AnalyticsReceiver request acknowledgement "
                 "is out of order");
        return false;
    }
    pending_analytics_value_requests_.erase(
        pending_analytics_value_requests_.begin());
    return true;
}

std::vector<GuestJni::AnalyticsValueRequest>
GuestJni::TakePendingAnalyticsValueRequests() {
    const std::lock_guard lock(mutex_);
    std::vector<AnalyticsValueRequest> requests;
    requests.swap(pending_analytics_value_requests_);
    return requests;
}

std::size_t GuestJni::pending_age_signals_request_count() const {
    const std::lock_guard lock(mutex_);
    return pending_age_signals_requests_.size();
}

std::optional<GuestJni::AgeSignalsRequest>
GuestJni::NextPendingAgeSignalsRequest() const {
    const std::lock_guard lock(mutex_);
    if (pending_age_signals_requests_.empty()) return std::nullopt;
    return pending_age_signals_requests_.front();
}

bool GuestJni::AcknowledgeAgeSignalsRequest(
    std::uint64_t sequence, std::string* error) {
    const std::lock_guard lock(mutex_);
    if (pending_age_signals_requests_.empty() ||
        pending_age_signals_requests_.front().sequence != sequence) {
        SetError(error,
                 "guest JNI Age Signals request acknowledgement is out of "
                 "order");
        return false;
    }
    pending_age_signals_requests_.erase(
        pending_age_signals_requests_.begin());
    return true;
}

std::vector<GuestJni::AgeSignalsRequest>
GuestJni::TakePendingAgeSignalsRequests() {
    const std::lock_guard lock(mutex_);
    std::vector<AgeSignalsRequest> requests;
    requests.swap(pending_age_signals_requests_);
    return requests;
}

std::size_t GuestJni::pending_popup_dialog_request_count() const {
    const std::lock_guard lock(mutex_);
    return pending_popup_dialog_requests_.size();
}

std::optional<GuestJni::PopupDialogRequest>
GuestJni::NextPendingPopupDialogRequest() const {
    const std::lock_guard lock(mutex_);
    if (pending_popup_dialog_requests_.empty()) return std::nullopt;
    return pending_popup_dialog_requests_.front();
}

bool GuestJni::AcknowledgePopupDialogRequest(
    std::uint64_t sequence, std::string* error) {
    const std::lock_guard lock(mutex_);
    if (pending_popup_dialog_requests_.empty() ||
        pending_popup_dialog_requests_.front().sequence != sequence) {
        SetError(error,
                 "guest JNI PopupDialog request acknowledgement is out of "
                 "order");
        return false;
    }
    pending_popup_dialog_requests_.erase(
        pending_popup_dialog_requests_.begin());
    return true;
}

std::vector<GuestJni::PopupDialogRequest>
GuestJni::TakePendingPopupDialogRequests() {
    const std::lock_guard lock(mutex_);
    std::vector<PopupDialogRequest> requests;
    requests.swap(pending_popup_dialog_requests_);
    return requests;
}

std::optional<std::array<std::uint64_t, 3>>
GuestJni::firebase_analytics_configuration() const {
    const std::lock_guard lock(mutex_);
    return firebase_analytics_configuration_;
}

std::optional<bool> GuestJni::firebase_analytics_collection_enabled() const {
    const std::lock_guard lock(mutex_);
    return firebase_analytics_collection_enabled_;
}

std::optional<std::uint64_t> GuestJni::referrer_fetch_native_pointer() const {
    const std::lock_guard lock(mutex_);
    return referrer_fetch_native_pointer_;
}

std::optional<std::uint64_t>
GuestJni::web_view_user_agent_native_pointer() const {
    const std::lock_guard lock(mutex_);
    return web_view_user_agent_native_pointer_;
}

std::size_t GuestJni::http_header_count() const {
    const std::lock_guard lock(mutex_);
    return http_headers_.size();
}

std::optional<std::array<std::string, 2>>
GuestJni::last_http_header() const {
    const std::lock_guard lock(mutex_);
    if (http_headers_.empty()) return std::nullopt;
    return std::array<std::string, 2>{http_headers_.back().name,
                                      http_headers_.back().value};
}

std::size_t GuestJni::http_task_count() const {
    const std::lock_guard lock(mutex_);
    return http_tasks_.size();
}

std::optional<std::array<std::uint64_t, 5>>
GuestJni::http_task_configuration(std::uint64_t reference) const {
    const std::lock_guard lock(mutex_);
    const std::optional<std::uint64_t> identity =
        ReferenceIdentityLocked(reference);
    if (!identity) return std::nullopt;
    const auto task = http_tasks_.find(*identity);
    if (task == http_tasks_.end()) return std::nullopt;
    return std::array<std::uint64_t, 5>{
        task->second.request_id,
        static_cast<std::uint32_t>(task->second.method),
        static_cast<std::uint32_t>(task->second.option),
        task->second.flag ? 1ULL : 0ULL,
        task->second.body.size()};
}

std::optional<std::string> GuestJni::http_task_url(
    std::uint64_t reference) const {
    const std::lock_guard lock(mutex_);
    const std::optional<std::uint64_t> identity =
        ReferenceIdentityLocked(reference);
    if (!identity) return std::nullopt;
    const auto task = http_tasks_.find(*identity);
    return task != http_tasks_.end()
               ? std::optional<std::string>(task->second.url)
               : std::nullopt;
}

std::optional<bool> GuestJni::http_task_execution_requested(
    std::uint64_t reference) const {
    const std::lock_guard lock(mutex_);
    const std::optional<std::uint64_t> identity =
        ReferenceIdentityLocked(reference);
    if (!identity) return std::nullopt;
    const auto task = http_tasks_.find(*identity);
    return task != http_tasks_.end()
               ? std::optional<bool>(task->second.execution_requested)
               : std::nullopt;
}

std::optional<std::uint64_t> GuestJni::last_http_cancel_request_id() const {
    const std::lock_guard lock(mutex_);
    return last_http_cancel_request_id_;
}

void GuestJni::ResetHttpLifecycleEvents() {
    const std::lock_guard lock(mutex_);
    http_lifecycle_events_.fill({});
    http_lifecycle_event_count_ = 0;
    http_lifecycle_event_next_ = 0;
    http_lifecycle_event_sequence_ = 0;
}

std::vector<GuestJni::HttpLifecycleEvent>
GuestJni::http_lifecycle_events() const {
    const std::lock_guard lock(mutex_);
    std::vector<HttpLifecycleEvent> events;
    events.reserve(http_lifecycle_event_count_);
    const std::size_t first =
        (http_lifecycle_event_next_ + kHttpLifecycleEventCapacity -
         http_lifecycle_event_count_) %
        kHttpLifecycleEventCapacity;
    for (std::size_t offset = 0; offset < http_lifecycle_event_count_;
         ++offset) {
        events.push_back(http_lifecycle_events_[
            (first + offset) % kHttpLifecycleEventCapacity]);
    }
    return events;
}

void GuestJni::RecordHttpLifecycleEventLocked(
    HttpLifecycleEventKind kind, std::uint64_t request_id) {
    http_lifecycle_events_[http_lifecycle_event_next_] = HttpLifecycleEvent{
        kind, request_id, ++http_lifecycle_event_sequence_,
        static_cast<std::uint64_t>(
            std::hash<std::thread::id>{}(std::this_thread::get_id()))};
    http_lifecycle_event_next_ =
        (http_lifecycle_event_next_ + 1) % kHttpLifecycleEventCapacity;
    if (http_lifecycle_event_count_ < kHttpLifecycleEventCapacity)
        ++http_lifecycle_event_count_;
}

void GuestJni::ResetHttpObservationEvents() {
    const std::lock_guard lock(mutex_);
    http_observation_events_.fill({});
    http_observation_event_count_ = 0;
    http_observation_event_next_ = 0;
    http_observation_event_sequence_ = 0;
    http_observation_owner_thread_hash_ =
        static_cast<std::uint64_t>(
            std::hash<std::thread::id>{}(std::this_thread::get_id()));
}

std::vector<GuestJni::HttpObservationEvent>
GuestJni::http_observation_events() const {
    const std::lock_guard lock(mutex_);
    std::vector<HttpObservationEvent> events;
    events.reserve(http_observation_event_count_);
    const std::size_t first =
        (http_observation_event_next_ + kHttpObservationEventCapacity -
         http_observation_event_count_) %
        kHttpObservationEventCapacity;
    for (std::size_t offset = 0;
         offset < http_observation_event_count_; ++offset) {
        events.push_back(http_observation_events_[
            (first + offset) % kHttpObservationEventCapacity]);
    }
    return events;
}

void GuestJni::RecordHttpObservationEventLocked(
    HttpObservationEventKind kind, std::string_view class_name,
    std::string_view method_name, std::string_view signature,
    std::array<std::uint64_t, 4> arguments) {
    if (!class_name.starts_with("com/king/http/") &&
        !class_name.starts_with("Lcom/king/http/"))
        return;
    const std::uint64_t thread_hash =
        static_cast<std::uint64_t>(
            std::hash<std::thread::id>{}(std::this_thread::get_id()));
    http_observation_events_[http_observation_event_next_] =
        HttpObservationEvent{kind,
                             ++http_observation_event_sequence_,
                             thread_hash,
                             thread_hash ==
                                 http_observation_owner_thread_hash_,
                             std::string(class_name),
                             std::string(method_name),
                             std::string(signature),
                             arguments};
    http_observation_event_next_ =
        (http_observation_event_next_ + 1) %
        kHttpObservationEventCapacity;
    if (http_observation_event_count_ < kHttpObservationEventCapacity)
        ++http_observation_event_count_;
}

bool GuestJni::QueueAnalyticsValueRequestLocked(std::string key,
                                                 std::string* error) {
    constexpr std::size_t kMaximumAnalyticsKeySize = 4096;
    if (key.size() > kMaximumAnalyticsKeySize ||
        key.find('\0') != std::string::npos) {
        SetError(error,
                 "guest JNI AnalyticsReceiver key exceeds the copied-key contract");
        return false;
    }
    if (pending_analytics_value_requests_.size() >=
        kAnalyticsValueRequestCapacity) {
        SetError(error,
                 "guest JNI AnalyticsReceiver request queue is full");
        return false;
    }
    if (analytics_value_request_sequence_ ==
        std::numeric_limits<std::uint64_t>::max()) {
        SetError(error,
                 "guest JNI AnalyticsReceiver request sequence is exhausted");
        return false;
    }
    pending_analytics_value_requests_.push_back(
        AnalyticsValueRequest{++analytics_value_request_sequence_,
                              std::move(key)});
    return true;
}

bool GuestJni::QueueAgeSignalsRequestLocked(
    std::uint64_t first_callback_handle, std::uint64_t debug_scenario,
    std::uint64_t second_callback_handle, std::string* error) {
    if (pending_age_signals_requests_.size() >=
        kAgeSignalsRequestCapacity) {
        SetError(error, "guest JNI Age Signals request queue is full");
        return false;
    }
    if (age_signals_request_sequence_ ==
        std::numeric_limits<std::uint64_t>::max()) {
        SetError(error, "guest JNI Age Signals request sequence is exhausted");
        return false;
    }
    pending_age_signals_requests_.push_back(
        AgeSignalsRequest{++age_signals_request_sequence_,
                          first_callback_handle, debug_scenario,
                          second_callback_handle});
    return true;
}

bool GuestJni::QueuePopupDialogRequestLocked(PopupDialogRequest request,
                                              std::string* error) {
    constexpr std::size_t kMaximumPopupDialogStringSize = 64 * 1024;
    if (request.message.size() > kMaximumPopupDialogStringSize ||
        request.title.size() > kMaximumPopupDialogStringSize ||
        request.button.size() > kMaximumPopupDialogStringSize ||
        request.message.find('\0') != std::string::npos ||
        request.title.find('\0') != std::string::npos ||
        request.button.find('\0') != std::string::npos) {
        SetError(error,
                 "guest JNI PopupDialog text exceeds the copied-text contract");
        return false;
    }
    if (pending_popup_dialog_requests_.size() >=
        kPopupDialogRequestCapacity) {
        SetError(error, "guest JNI PopupDialog request queue is full");
        return false;
    }
    if (popup_dialog_request_sequence_ ==
        std::numeric_limits<std::uint64_t>::max()) {
        SetError(error, "guest JNI PopupDialog request sequence is exhausted");
        return false;
    }
    request.sequence = ++popup_dialog_request_sequence_;
    pending_popup_dialog_requests_.push_back(std::move(request));
    return true;
}

bool GuestJni::device_impl_started() const {
    const std::lock_guard lock(mutex_);
    return device_impl_started_;
}

bool GuestJni::device_screen_info_requested() const {
    const std::lock_guard lock(mutex_);
    return device_screen_info_requested_;
}

std::optional<bool> GuestJni::platform_splash_visible() const {
    const std::lock_guard lock(mutex_);
    return platform_splash_visible_;
}

std::optional<bool> GuestJni::haptics_in_external_display_enabled() const {
    const std::lock_guard lock(mutex_);
    return haptics_in_external_display_enabled_;
}

std::uint64_t GuestJni::haptics_vibrate_request_count() const {
    const std::lock_guard lock(mutex_);
    return haptics_vibrate_requests_;
}

bool GuestJni::WriteGuestMemory(std::uint64_t address, const void* source,
                                std::size_t size) {
    if (address_space_ == nullptr) return false;
    return address_space_->Write(address, source, size);
}

void GuestJni::ClearPendingException() {
    const std::lock_guard lock(mutex_);
    exception_pending().pending = false;
    exception_pending().type.clear();
    exception_pending().message.clear();
    exception_pending().reference = 0;
    MirrorPendingExceptionLocked();
}

std::optional<bool> GuestJni::keep_screen_on() const {
    const std::lock_guard lock(mutex_);
    return keep_screen_on_;
}

bool GuestJni::startup_loading_finished() const {
    const std::lock_guard lock(mutex_);
    return startup_loading_finished_;
}

bool GuestJni::input_map_provider_initialized() const {
    const std::lock_guard lock(mutex_);
    return input_map_provider_initialized_;
}

bool GuestJni::app_not_responding_ui_task_requested() const {
    const std::lock_guard lock(mutex_);
    return app_not_responding_ui_task_requested_;
}

bool GuestJni::app_not_responding_task_processor_active() const {
    const std::lock_guard lock(mutex_);
    return app_not_responding_task_processor_active_;
}

std::size_t GuestJni::pending_ui_task_count() const {
    const std::lock_guard lock(mutex_);
    return pending_ui_tasks_.size();
}

std::vector<std::uint64_t> GuestJni::TakeReadyPendingUiTaskHandles() {
    std::vector<std::uint64_t> handles;
    for (const PendingUiTaskInfo& task : TakeReadyPendingUiTasks())
        handles.push_back(task.native_handle);
    return handles;
}

std::vector<GuestJni::PendingUiTaskInfo>
GuestJni::TakeReadyPendingUiTasks() {
    const std::lock_guard lock(mutex_);
    const auto now = std::chrono::steady_clock::now();
    std::vector<PendingUiTaskInfo> tasks;
    tasks.reserve(pending_ui_tasks_.size());
    auto task = pending_ui_tasks_.begin();
    while (task != pending_ui_tasks_.end()) {
        if (task->ready_at <= now) {
            tasks.push_back(PendingUiTaskInfo{
                task->native_handle, task->dispatch_class,
                task->dispatch_name, task->dispatch_signature,
                task->argument_reference});
            task = pending_ui_tasks_.erase(task);
        } else {
            ++task;
        }
    }
    return tasks;
}

std::vector<std::pair<std::uint64_t, std::uint64_t>>
GuestJni::TakePendingDisplayListeners() {
    const std::lock_guard lock(mutex_);
    auto listeners = std::move(display_listeners_);
    display_listeners_.clear();
    return listeners;
}

std::optional<std::pair<std::uint64_t, std::uint64_t>>
GuestJni::FindInterfaceProxy(std::string_view interface_type) {
    const std::lock_guard lock(mutex_);
    const std::string wanted = NormalizeCompatibilityType(interface_type);
    for (const auto& [reference, proxy] : interface_proxies_) {
        for (const std::string& iface : proxy.interfaces) {
            if (NormalizeCompatibilityType(iface) == wanted)
                return std::pair{reference, proxy.native_handle};
        }
    }
    return std::nullopt;
}

std::string GuestJni::DescribeInterfaceProxies() const {
    const std::lock_guard lock(mutex_);
    std::string result;
    for (const auto& [reference, proxy] : interface_proxies_) {
        if (!result.empty()) result.push_back('|');
        result += "ref=" + std::to_string(reference) + " handle=" +
                  std::to_string(proxy.native_handle) + " if=";
        bool first = true;
        for (const std::string& iface : proxy.interfaces) {
            if (!first) result.push_back(',');
            result += iface;
            first = false;
        }
    }
    return result;
}

std::vector<std::pair<std::uint64_t, std::uint64_t>>
GuestJni::TakeChoreographerCallbacks() {
    const std::lock_guard lock(mutex_);
    auto callbacks = std::move(choreographer_callbacks_);
    choreographer_callbacks_.clear();
    return callbacks;
}

std::vector<std::pair<std::uint64_t, std::uint64_t>>
GuestJni::TakePendingFrameCallbacks() {
    const std::lock_guard lock(mutex_);
    auto callbacks = std::move(pending_frame_callbacks_);
    pending_frame_callbacks_.clear();
    return callbacks;
}

std::optional<std::uint64_t>
GuestJni::next_pending_ui_task_delay_milliseconds() const {
    const std::lock_guard lock(mutex_);
    if (pending_ui_tasks_.empty()) return std::nullopt;
    const auto next = std::min_element(
        pending_ui_tasks_.begin(), pending_ui_tasks_.end(),
        [](const PendingUiTask& left, const PendingUiTask& right) {
            return left.ready_at < right.ready_at;
        });
    const auto now = std::chrono::steady_clock::now();
    if (next->ready_at <= now) return 0;
    const auto delay = std::chrono::ceil<std::chrono::milliseconds>(
        next->ready_at - now);
    return static_cast<std::uint64_t>(delay.count());
}

std::optional<std::uint64_t>
GuestJni::ads_platform_activity_identity() const {
    const std::lock_guard lock(mutex_);
    return ads_platform_activity_identity_;
}

std::optional<std::uint64_t>
GuestJni::ads_network_listener_native_pointer() const {
    const std::lock_guard lock(mutex_);
    return ads_network_listener_native_pointer_;
}

bool GuestJni::google_billing_connect_requested() const {
    const std::lock_guard lock(mutex_);
    return google_billing_connect_requested_;
}

bool GuestJni::facebook_sdk_teardown_requested() const {
    const std::lock_guard lock(mutex_);
    return facebook_sdk_teardown_requested_;
}

bool GuestJni::ads_http_reset_requested() const {
    const std::lock_guard lock(mutex_);
    return ads_http_reset_requested_;
}

std::optional<GuestJni::BrazeConfiguration>
GuestJni::braze_configuration() const {
    const std::lock_guard lock(mutex_);
    return braze_configuration_;
}

bool GuestJni::notification_token_provider_disconnected() const {
    const std::lock_guard lock(mutex_);
    return notification_token_provider_disconnected_;
}

bool GuestJni::notification_cancel_all_requested() const {
    const std::lock_guard lock(mutex_);
    return notification_cancel_all_requested_;
}

bool GuestJni::local_notification_cancel_all_requested() const {
    const std::lock_guard lock(mutex_);
    return local_notification_cancel_all_requested_;
}

std::optional<GuestJni::LocalNotificationSchedule>
GuestJni::local_notification_schedule() const {
    const std::lock_guard lock(mutex_);
    return local_notification_schedule_;
}

bool GuestJni::ff_base_sdk_initialized() const {
    const std::lock_guard lock(mutex_);
    return ff_base_sdk_initialized_;
}

bool GuestJni::initialized() const {
    const std::lock_guard lock(mutex_);
    return initialized_;
}

auto GuestJni::FindOwnedLocalClassReferenceLocked(
    std::uint64_t reference) const
    -> std::unordered_map<std::uint64_t, ClassReference>::const_iterator {
    const auto found = class_references_.find(reference);
    return found != class_references_.end() &&
                   CurrentThreadOwnsLocalReferenceLocked(reference)
               ? found
               : class_references_.end();
}

auto GuestJni::FindOwnedLocalObjectReferenceLocked(
    std::uint64_t reference) const
    -> std::unordered_map<std::uint64_t, ObjectReference>::const_iterator {
    const auto found = object_references_.find(reference);
    return found != object_references_.end() &&
                   CurrentThreadOwnsLocalReferenceLocked(reference)
               ? found
               : object_references_.end();
}

bool GuestJni::IsClassReferenceLocked(std::uint64_t reference) const {
    return (class_references_.contains(reference) &&
            CurrentThreadOwnsLocalReferenceLocked(reference)) ||
           global_class_references_.contains(reference);
}

bool GuestJni::RecoverClassReferenceLocked(std::uint64_t reference) {
    if (reference == 0 || heap_ == nullptr) return false;
    std::array<std::uint8_t, 0x40> header{};
    if (!heap_->Read(reference, header.data(), header.size()))
        return false;
    std::uint32_t magic = 0;
    std::memcpy(&magic, header.data(), sizeof(magic));
    if (magic != kCompatibilityObjectMagic) return false;
    std::uint64_t class_address = 0;
    std::uint64_t payload_address = 0;
    std::memcpy(&class_address, header.data() + 8, sizeof(class_address));
    std::memcpy(&payload_address, header.data() + 0x10,
                sizeof(payload_address));
    const auto read_cstr = [this](std::uint64_t address,
                                  std::string* out) -> bool {
        out->clear();
        for (std::size_t i = 0; i < 256; i += 64) {
            std::array<char, 64> chunk{};
            if (!heap_->Read(address + i, chunk.data(), chunk.size()))
                return false;
            for (char c : chunk) {
                if (c == '\0') return true;
                out->push_back(c);
            }
        }
        return false;
    };
    std::string object_class;
    std::string represented_type;
    if (!read_cstr(class_address, &object_class) ||
        object_class != "java/lang/Class" ||
        !read_cstr(payload_address, &represented_type) ||
        represented_type.empty() || represented_type.front() == 'L')
        return false;
    const std::string type = "L" + represented_type + ";";
    class_references_.emplace(reference,
                              ClassReference{reference, type, 0});
    TrackLocalReferenceLocked(reference);
    return true;
}

bool GuestJni::IsObjectReferenceLocked(std::uint64_t reference) const {
    return (object_references_.contains(reference) &&
            CurrentThreadOwnsLocalReferenceLocked(reference)) ||
           global_object_references_.contains(reference);
}

std::optional<std::uint64_t> GuestJni::ReferenceIdentityLocked(
    std::uint64_t reference) const {
    if (reference == 0) return 0;
    const auto local_class = class_references_.find(reference);
    if (local_class != class_references_.end() &&
        CurrentThreadOwnsLocalReferenceLocked(reference))
        return local_class->second.identity;
    const auto local_object = object_references_.find(reference);
    if (local_object != object_references_.end() &&
        CurrentThreadOwnsLocalReferenceLocked(reference))
        return local_object->second.identity;
    const auto global_class = global_class_references_.find(reference);
    if (global_class != global_class_references_.end())
        return global_class->second.identity;
    const auto global_object = global_object_references_.find(reference);
    if (global_object != global_object_references_.end())
        return global_object->second.identity;
    return std::nullopt;
}

std::optional<std::string_view> GuestJni::ObjectTypeLocked(
    std::uint64_t reference) const {
    const auto local = object_references_.find(reference);
    if (local != object_references_.end() &&
        CurrentThreadOwnsLocalReferenceLocked(reference))
        return local->second.type;
    const auto global = global_object_references_.find(reference);
    if (global != global_object_references_.end()) return global->second.type;
    return std::nullopt;
}

std::optional<std::string_view> GuestJni::ClassTypeLocked(
    std::uint64_t reference) const {
    const auto local = class_references_.find(reference);
    if (local != class_references_.end() &&
        CurrentThreadOwnsLocalReferenceLocked(reference))
        return local->second.type;
    const auto global = global_class_references_.find(reference);
    if (global != global_class_references_.end()) return global->second.type;
    return std::nullopt;
}

std::optional<GuestJniBackend::Handle> GuestJni::BackendReferenceLocked(
    std::uint64_t reference) const {
    if (reference == 0) return 0;
    const auto local_class = class_references_.find(reference);
    if (local_class != class_references_.end() &&
        CurrentThreadOwnsLocalReferenceLocked(reference))
        return local_class->second.backend;
    const auto local_object = object_references_.find(reference);
    if (local_object != object_references_.end() &&
        CurrentThreadOwnsLocalReferenceLocked(reference))
        return local_object->second.backend;
    const auto global_class = global_class_references_.find(reference);
    if (global_class != global_class_references_.end())
        return global_class->second.backend;
    const auto global_object = global_object_references_.find(reference);
    if (global_object != global_object_references_.end())
        return global_object->second.backend;
    return std::nullopt;
}

bool GuestJni::IsVmAddress(std::uint64_t address) const {
    return address != 0 && address == vm_address();
}

bool GuestJni::IsCurrentEnvironment(std::uint64_t address) const {
    const std::optional<std::uint64_t> current =
        CurrentEnvironmentAddress();
    return current && address == *current;
}

bool GuestJni::IsCurrentEnvironmentReference(std::uint64_t address) const {
    return IsCurrentEnvironment(address);
}

bool GuestJni::IsObjectReferenceOfType(std::uint64_t reference,
                                       std::string_view type) const {
    const std::lock_guard lock(mutex_);
    const std::optional<std::string_view> actual = ObjectTypeLocked(reference);
    return actual && *actual == type;
}

bool GuestJni::WriteGuestPointer(std::uint64_t output,
                                 std::uint64_t value,
                                 std::string* error) const {
    if (output == 0 || address_space_ == nullptr ||
        !address_space_->Write(output, &value, sizeof(value))) {
        SetError(error, "guest JNI output pointer is invalid");
        return false;
    }
    return true;
}

}  // namespace isomr
