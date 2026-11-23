#include "ctr_entry_bridge.h"

#include "aarch64_profile_runtime.h"
#include "bionic_runtime.h"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace isomr {
namespace {

constexpr std::string_view kNativeLoaderClass =
    "com/unity3d/player/NativeLoader";
constexpr std::string_view kLoadName = "load";
constexpr std::string_view kLoadSignature = "(Ljava/lang/String;)Z";
constexpr std::string_view kUnloadName = "unload";
constexpr std::string_view kUnloadSignature = "()Z";
constexpr std::string_view kUnityPlayerClass =
    "com/unity3d/player/UnityPlayer";
constexpr std::string_view kInitJniName = "initJni";
constexpr std::string_view kInitJniSignature =
    "(Landroid/content/Context;)V";
constexpr std::string_view kNativeResumeName = "nativeResume";
constexpr std::string_view kNativeResumeSignature = "()V";
constexpr std::string_view kNativeRenderName = "nativeRender";
constexpr std::string_view kNativeRenderSignature = "()Z";
constexpr std::string_view kJniBridgeClass =
    "bitter/jnibridge/JNIBridge";
constexpr std::string_view kJniBridgeInvokeName = "invoke";
constexpr std::string_view kJniBridgeInvokeSignature =
    "(JLjava/lang/Class;Ljava/lang/reflect/Method;[Ljava/lang/Object;)Ljava/lang/Object;";

void SetError(std::string* error, std::string message) {
    if (error != nullptr) *error = std::move(message);
}

bool CallBooleanNative(Aarch64ProfileRuntime* runtime,
                       std::string_view class_type,
                       std::string_view name,
                       std::string_view signature,
                       const std::vector<Aarch64CallArgument>& arguments,
                       bool* value, std::string* error) {
    Aarch64CallResult call_result{};
    std::string call_error;
    if (!runtime->CallRegisteredNativeOnAttachedThread(
            class_type, name, signature, arguments, &call_result,
            &call_error)) {
        SetError(error, "CtrEntryBridge " + std::string(class_type) + "." +
                            std::string(name) + ": " + call_error);
        return false;
    }
    if (call_result.x0 > 1) {
        SetError(error, "CtrEntryBridge " + std::string(class_type) + "." +
                            std::string(name) +
                            " returned a non-boolean JNI value");
        return false;
    }
    *value = call_result.x0 != 0;
    return true;
}

}  // namespace

bool CtrEntryBridge::LoadUnity(Aarch64ProfileRuntime* runtime,
                               std::string_view guest_library_directory,
                               CtrEntryResult* result,
                               std::string* error) {
    if (result != nullptr) *result = {};
    if (runtime == nullptr || runtime->bionic() == nullptr ||
        result == nullptr || guest_library_directory.empty() ||
        guest_library_directory.size() > 1024) {
        SetError(error, "CtrEntryBridge load arguments are invalid");
        return false;
    }
    GuestJni& jni = runtime->bionic()->jni();
    const std::optional<std::uint64_t> environment =
        jni.CurrentEnvironmentAddress();
    if (!environment) {
        SetError(error, "CtrEntryBridge has no current guest JNI environment");
        return false;
    }
    if (!jni.FindRegisteredNative(kNativeLoaderClass, kLoadName,
                                  kLoadSignature) ||
        !jni.FindRegisteredNative(kNativeLoaderClass, kUnloadName,
                                  kUnloadSignature)) {
        SetError(error,
                 "CtrEntryBridge NativeLoader natives are not registered");
        return false;
    }

    std::uint64_t loader_class = 0;
    std::uint64_t directory = 0;
    const auto cleanup = [&] {
        std::string ignored;
        if (directory != 0)
            (void)jni.ReleaseCompatibilityLocalReference(directory,
                                                         &ignored);
        if (loader_class != 0)
            (void)jni.ReleaseCompatibilityLocalReference(loader_class,
                                                         &ignored);
    };
    if (!jni.CreateCompatibilityClassReference(kNativeLoaderClass,
                                               &loader_class, error) ||
        !jni.CreateCompatibilityStringReference(guest_library_directory,
                                                &directory, error)) {
        cleanup();
        return false;
    }
    bool loaded = false;
    const bool called = CallBooleanNative(
        runtime, kNativeLoaderClass, kLoadName, kLoadSignature,
        {Aarch64CallArgument::General64(*environment),
         Aarch64CallArgument::General64(loader_class),
         Aarch64CallArgument::General64(directory)},
        &loaded, error);
    cleanup();
    if (!called) return false;
    result->unity_loaded = loaded;
    result->module_count = runtime->module_count();
    result->registered_native_count = jni.registered_native_count();
    if (!loaded) {
        SetError(error, "CtrEntryBridge NativeLoader.load returned false");
        return false;
    }
    return true;
}

bool CtrEntryBridge::UnloadUnity(Aarch64ProfileRuntime* runtime,
                                 std::string* error) {
    if (runtime == nullptr || runtime->bionic() == nullptr) {
        SetError(error, "CtrEntryBridge unload arguments are invalid");
        return false;
    }
    GuestJni& jni = runtime->bionic()->jni();
    const std::optional<std::uint64_t> environment =
        jni.CurrentEnvironmentAddress();
    if (!environment) {
        SetError(error, "CtrEntryBridge has no current guest JNI environment");
        return false;
    }
    std::uint64_t loader_class = 0;
    if (!jni.CreateCompatibilityClassReference(kNativeLoaderClass,
                                               &loader_class, error))
        return false;
    bool unloaded = false;
    const bool called = CallBooleanNative(
        runtime, kNativeLoaderClass, kUnloadName, kUnloadSignature,
        {Aarch64CallArgument::General64(*environment),
         Aarch64CallArgument::General64(loader_class)},
        &unloaded, error);
    std::string cleanup_error;
    const bool released = jni.ReleaseCompatibilityLocalReference(
        loader_class, &cleanup_error);
    if (!called) return false;
    if (!released) {
        SetError(error, std::move(cleanup_error));
        return false;
    }
    if (!unloaded) {
        SetError(error, "CtrEntryBridge NativeLoader.unload returned false");
        return false;
    }
    return true;
}

bool CtrEntryBridge::RenderUnityOnce(Aarch64ProfileRuntime* runtime,
                                     bool* rendered,
                                     std::string* error) {
    if (runtime == nullptr || runtime->bionic() == nullptr ||
        rendered == nullptr) {
        SetError(error, "CtrEntryBridge render arguments are invalid");
        return false;
    }
    *rendered = false;
    GuestJni& jni = runtime->bionic()->jni();
    const std::optional<std::uint64_t> environment =
        jni.CurrentEnvironmentAddress();
    if (!environment ||
        !jni.FindRegisteredNative(kUnityPlayerClass, kInitJniName,
                                  kInitJniSignature) ||
        !jni.FindRegisteredNative(kUnityPlayerClass, kNativeResumeName,
                                  kNativeResumeSignature) ||
        !jni.FindRegisteredNative(kUnityPlayerClass, kNativeRenderName,
                                  kNativeRenderSignature)) {
        SetError(error,
                 "CtrEntryBridge UnityPlayer lifecycle natives are unavailable");
        return false;
    }
    std::uint64_t unity_player = 0;
    std::uint64_t context = 0;
    const auto cleanup = [&] {
        std::string ignored;
        if (context != 0)
            (void)jni.ReleaseCompatibilityLocalReference(context, &ignored);
        if (unity_player != 0)
            (void)jni.ReleaseCompatibilityLocalReference(unity_player,
                                                         &ignored);
    };
    if (!jni.CreateCompatibilityTypedObjectReference(
            "Lcom/unity3d/player/UnityPlayer;", &unity_player, error) ||
        !jni.CreateCompatibilityTypedObjectReference(
            "Landroid/app/Activity;", &context, error)) {
        cleanup();
        return false;
    }
    Aarch64CallResult init_result{};
    std::string init_error;
    if (!runtime->CallRegisteredNativeOnAttachedThread(
            kUnityPlayerClass, kInitJniName, kInitJniSignature,
            {Aarch64CallArgument::General64(*environment),
             Aarch64CallArgument::General64(unity_player),
             Aarch64CallArgument::General64(context)},
            &init_result, &init_error)) {
        cleanup();
        SetError(error, "CtrEntryBridge " + std::string(kUnityPlayerClass) +
                            "." + std::string(kInitJniName) + ": " +
                            init_error);
        return false;
    }
    Aarch64CallResult resume_result{};
    std::string resume_error;
    if (!runtime->CallRegisteredNativeOnAttachedThread(
            kUnityPlayerClass, kNativeResumeName, kNativeResumeSignature,
            {Aarch64CallArgument::General64(*environment),
             Aarch64CallArgument::General64(unity_player)},
            &resume_result, &resume_error)) {
        cleanup();
        SetError(error, "CtrEntryBridge " + std::string(kUnityPlayerClass) +
                            "." + std::string(kNativeResumeName) + ": " +
                            resume_error);
        return false;
    }
    {
        std::string surface_error;
        Aarch64CallResult surface_call_result{};
        if (!runtime->CallRegisteredNativeOnAttachedThread(
                kUnityPlayerClass, "nativeSendSurfaceChangedEvent", "()V",
                {Aarch64CallArgument::General64(*environment),
                 Aarch64CallArgument::General64(unity_player)},
                &surface_call_result, &surface_error)) {
            cleanup();
            SetError(error, "CtrEntryBridge nativeSendSurfaceChangedEvent: " +
                                surface_error);
            return false;
        }
    }
    const bool called = CallBooleanNative(
        runtime, kUnityPlayerClass, kNativeRenderName,
        kNativeRenderSignature,
        {Aarch64CallArgument::General64(*environment),
         Aarch64CallArgument::General64(unity_player)},
        rendered, error);
    cleanup();
    if (!called) return false;
    return true;
}

bool CtrEntryBridge::RenderUnityFrame(Aarch64ProfileRuntime* runtime,
                                      bool* rendered, std::string* error) {
    if (runtime == nullptr || runtime->bionic() == nullptr ||
        rendered == nullptr) {
        SetError(error, "CtrEntryBridge render arguments are invalid");
        return false;
    }
    *rendered = false;
    GuestJni& jni = runtime->bionic()->jni();
    const std::optional<std::uint64_t> environment =
        jni.CurrentEnvironmentAddress();
    if (!environment ||
        !jni.FindRegisteredNative(kUnityPlayerClass, kNativeRenderName,
                                  kNativeRenderSignature)) {
        SetError(error,
                 "CtrEntryBridge UnityPlayer lifecycle natives are unavailable");
        return false;
    }
    std::uint64_t unity_player = 0;
    const auto cleanup = [&] {
        std::string ignored;
        if (unity_player != 0)
            (void)jni.ReleaseCompatibilityLocalReference(unity_player,
                                                         &ignored);
    };
    if (!jni.CreateCompatibilityTypedObjectReference(
            "Lcom/unity3d/player/UnityPlayer;", &unity_player, error)) {
        return false;
    }
    const bool called = CallBooleanNative(
        runtime, kUnityPlayerClass, kNativeRenderName,
        kNativeRenderSignature,
        {Aarch64CallArgument::General64(*environment),
         Aarch64CallArgument::General64(unity_player)},
        rendered, error);
    cleanup();
    if (!called) return false;
    return true;
}

bool CtrEntryBridge::InvokeRunnable(Aarch64ProfileRuntime* runtime,
                                    std::uint64_t native_handle,
                                    std::string* error) {
    if (runtime == nullptr || runtime->bionic() == nullptr ||
        native_handle == 0) {
        SetError(error, "CtrEntryBridge Runnable arguments are invalid");
        return false;
    }
    GuestJni& jni = runtime->bionic()->jni();
    const std::optional<std::uint64_t> environment =
        jni.CurrentEnvironmentAddress();
    if (!environment ||
        !jni.FindRegisteredNative(kJniBridgeClass, kJniBridgeInvokeName,
                                  kJniBridgeInvokeSignature)) {
        SetError(error,
                 "CtrEntryBridge JNIBridge.invoke is unavailable on the attached thread");
        return false;
    }

    std::uint64_t bridge_class = 0;
    std::uint64_t runnable_class = 0;
    std::uint64_t reflected_method = 0;
    std::uint64_t arguments = 0;
    const auto cleanup = [&] {
        std::string ignored;
        if (arguments != 0)
            (void)jni.ReleaseCompatibilityLocalReference(arguments,
                                                         &ignored);
        if (reflected_method != 0)
            (void)jni.ReleaseCompatibilityLocalReference(reflected_method,
                                                         &ignored);
        if (runnable_class != 0)
            (void)jni.ReleaseCompatibilityLocalReference(runnable_class,
                                                         &ignored);
        if (bridge_class != 0)
            (void)jni.ReleaseCompatibilityLocalReference(bridge_class,
                                                         &ignored);
    };
    if (!jni.CreateCompatibilityClassReference(kJniBridgeClass,
                                               &bridge_class, error) ||
        !jni.CreateCompatibilityClassReference("java/lang/Runnable",
                                               &runnable_class, error) ||
        !jni.CreateCompatibilityReflectedMethodReference(
            runnable_class, "run", "()V", &reflected_method, error) ||
        !jni.CreateCompatibilityEmptyObjectArrayReference(&arguments,
                                                          error)) {
        cleanup();
        return false;
    }

    Aarch64CallResult call_result{};
    const bool called = runtime->CallRegisteredNativeOnAttachedThread(
        kJniBridgeClass, kJniBridgeInvokeName, kJniBridgeInvokeSignature,
        {Aarch64CallArgument::General64(*environment),
         Aarch64CallArgument::General64(bridge_class),
         Aarch64CallArgument::General64(native_handle),
         Aarch64CallArgument::General64(runnable_class),
         Aarch64CallArgument::General64(reflected_method),
         Aarch64CallArgument::General64(arguments)},
        &call_result, error);
    // Unity's dispatcher probes interface methods by throwing
    // NoSuchMethodError and expects the Java proxy layer to catch it.
    // Swallow any pending exception exactly like that proxy catch block.
    if (jni.has_pending_thrown_exception())
        jni.ClearPendingException();
    cleanup();
    return called;
}

}  // namespace isomr
