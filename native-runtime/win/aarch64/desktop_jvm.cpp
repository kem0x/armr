#include "desktop_jvm.h"

#include <cstring>
#include <utility>

namespace isomr {
namespace {

void SetError(std::string* error, std::string message) {
    if (error != nullptr) *error = std::move(message);
}

}  // namespace

DesktopJvm::~DesktopJvm() {
    if (started_ && !shutdown_) {
        std::string ignored;
        (void)Shutdown(&ignored);
    }
}

bool DesktopJvm::Start(const std::wstring& jvm_path,
                       const std::string& class_path,
                       std::string* error) {
    const std::lock_guard lock(mutex_);
    if (started_ || jvm_path.empty() || class_path.empty()) {
        SetError(error, started_ ? "desktop JVM is already started"
                                 : "desktop JVM configuration is invalid");
        return false;
    }
    module_ = LoadLibraryW(jvm_path.c_str());
    if (module_ == nullptr) {
        SetError(error, "desktop JVM library could not be loaded");
        return false;
    }
    const FARPROC procedure = GetProcAddress(module_, "JNI_CreateJavaVM");
    CreateJavaVm create = nullptr;
    static_assert(sizeof(create) == sizeof(procedure));
    std::memcpy(&create, &procedure, sizeof(create));
    if (create == nullptr) {
        FreeLibrary(module_);
        module_ = nullptr;
        SetError(error, "desktop JVM JNI_CreateJavaVM export is missing");
        return false;
    }

    std::string class_path_option = "-Djava.class.path=" + class_path;
    JavaVMOption option{};
    option.optionString = class_path_option.data();
    JavaVMInitArgs arguments{};
    arguments.version = JNI_VERSION_1_8;
    arguments.nOptions = 1;
    arguments.options = &option;
    arguments.ignoreUnrecognized = JNI_FALSE;
    JNIEnv* environment = nullptr;
    if (create(&vm_, reinterpret_cast<void**>(&environment), &arguments) !=
            JNI_OK ||
        vm_ == nullptr || environment == nullptr) {
        vm_ = nullptr;
        FreeLibrary(module_);
        module_ = nullptr;
        SetError(error, "desktop JVM creation failed");
        return false;
    }
    creator_thread_ = std::this_thread::get_id();
    started_ = true;
    return true;
}

bool DesktopJvm::Shutdown(std::string* error) {
    JavaVM* vm = nullptr;
    HMODULE module = nullptr;
    {
        const std::lock_guard lock(mutex_);
        if (!started_ || shutdown_) {
            SetError(error, shutdown_ ? "desktop JVM is already shut down"
                                      : "desktop JVM is not started");
            return false;
        }
        if (std::this_thread::get_id() != creator_thread_) {
            SetError(error, "desktop JVM must be shut down by its owner thread");
            return false;
        }
        if (!references_.empty() || !methods_.empty() ||
            !backend_attached_threads_.empty()) {
            SetError(error,
                     "desktop JVM ownership remains: references=" +
                         std::to_string(references_.size()) + " methods=" +
                         std::to_string(methods_.size()) + " threads=" +
                         std::to_string(backend_attached_threads_.size()));
            return false;
        }
        vm = vm_;
        module = module_;
    }
    if (vm->DestroyJavaVM() != JNI_OK) {
        SetError(error, "desktop JVM destruction failed");
        return false;
    }
    {
        const std::lock_guard lock(mutex_);
        vm_ = nullptr;
        module_ = nullptr;
        shutdown_ = true;
    }
    FreeLibrary(module);
    return true;
}

bool DesktopJvm::CurrentEnvironment(JNIEnv** environment,
                                    std::string* error) const {
    if (environment == nullptr || vm_ == nullptr || shutdown_) {
        SetError(error, "desktop JVM is not available");
        return false;
    }
    *environment = nullptr;
    const jint result = vm_->GetEnv(reinterpret_cast<void**>(environment),
                                    JNI_VERSION_1_8);
    if (result == JNI_OK && *environment != nullptr) return true;
    SetError(error, result == JNI_EDETACHED
                        ? "current thread is detached from the desktop JVM"
                        : "desktop JVM environment lookup failed");
    return false;
}

bool DesktopJvm::AttachCurrentThread(std::string* error) {
    const std::lock_guard lock(mutex_);
    if (!started_ || shutdown_ || vm_ == nullptr) {
        SetError(error, "desktop JVM is not started");
        return false;
    }
    JNIEnv* environment = nullptr;
    const jint existing = vm_->GetEnv(reinterpret_cast<void**>(&environment),
                                      JNI_VERSION_1_8);
    if (existing == JNI_OK && environment != nullptr) return true;
    if (existing != JNI_EDETACHED ||
        vm_->AttachCurrentThread(reinterpret_cast<void**>(&environment),
                                 nullptr) != JNI_OK ||
        environment == nullptr) {
        SetError(error, "current thread could not attach to the desktop JVM");
        return false;
    }
    backend_attached_threads_.insert(std::this_thread::get_id());
    return true;
}

bool DesktopJvm::DetachCurrentThread(std::string* error) {
    const std::lock_guard lock(mutex_);
    if (!started_ || shutdown_ || vm_ == nullptr) {
        SetError(error, "desktop JVM is not started");
        return false;
    }
    const std::thread::id current = std::this_thread::get_id();
    for (const auto& [handle, reference] : references_) {
        (void)handle;
        if (!reference.global && reference.owner == current) {
            SetError(error,
                     "desktop JVM thread still owns local references");
            return false;
        }
    }
    const auto attached = backend_attached_threads_.find(current);
    if (attached == backend_attached_threads_.end()) return true;
    if (vm_->DetachCurrentThread() != JNI_OK) {
        SetError(error, "current thread could not detach from the desktop JVM");
        return false;
    }
    backend_attached_threads_.erase(attached);
    return true;
}

std::uint32_t DesktopJvm::GetVersion() {
    const std::lock_guard lock(mutex_);
    JNIEnv* environment = nullptr;
    if (!CurrentEnvironment(&environment, nullptr)) return 0;
    return static_cast<std::uint32_t>(environment->GetVersion());
}

GuestJniBackend::Handle DesktopJvm::AddReferenceLocked(jobject value,
                                                        bool global) {
    if (value == nullptr) return 0;
    const Handle handle = next_handle_++;
    references_.emplace(
        handle, Reference{value, global, std::this_thread::get_id()});
    return handle;
}

const DesktopJvm::Reference* DesktopJvm::FindReferenceLocked(
    Handle handle) const {
    const auto found = references_.find(handle);
    if (found == references_.end() ||
        (!found->second.global &&
         found->second.owner != std::this_thread::get_id()))
        return nullptr;
    return &found->second;
}

const DesktopJvm::Method* DesktopJvm::FindMethodLocked(Handle handle) const {
    const auto found = methods_.find(handle);
    return found != methods_.end() ? &found->second : nullptr;
}

bool DesktopJvm::FindClass(std::string_view name, Handle* result,
                           std::string* error) {
    const std::lock_guard lock(mutex_);
    JNIEnv* environment = nullptr;
    if (result == nullptr || !CurrentEnvironment(&environment, error))
        return false;
    const std::string terminated(name);
    *result = AddReferenceLocked(environment->FindClass(terminated.c_str()),
                                 false);
    return true;
}

bool DesktopJvm::GetMethodId(Handle clazz, std::string_view name,
                             std::string_view signature, bool is_static,
                             Handle* result, std::string* error) {
    const std::lock_guard lock(mutex_);
    JNIEnv* environment = nullptr;
    const Reference* class_reference = FindReferenceLocked(clazz);
    if (result == nullptr || class_reference == nullptr ||
        !CurrentEnvironment(&environment, error)) {
        if (class_reference == nullptr)
            SetError(error, "desktop JVM class token is invalid");
        return false;
    }
    const std::string terminated_name(name);
    const std::string terminated_signature(signature);
    jmethodID value = is_static
                          ? environment->GetStaticMethodID(
                                static_cast<jclass>(class_reference->value),
                                terminated_name.c_str(),
                                terminated_signature.c_str())
                          : environment->GetMethodID(
                                static_cast<jclass>(class_reference->value),
                                terminated_name.c_str(),
                                terminated_signature.c_str());
    if (value == nullptr) {
        *result = 0;
        return true;
    }
    const Handle handle = next_handle_++;
    methods_.emplace(handle, Method{value, is_static});
    *result = handle;
    return true;
}

bool DesktopJvm::CallObjectMethod(Handle receiver, Handle method,
                                  bool is_static, Handle* result,
                                  std::string* error) {
    const std::lock_guard lock(mutex_);
    JNIEnv* environment = nullptr;
    const Reference* receiver_reference = FindReferenceLocked(receiver);
    const Method* method_reference = FindMethodLocked(method);
    if (result == nullptr || receiver_reference == nullptr ||
        method_reference == nullptr || method_reference->is_static != is_static ||
        !CurrentEnvironment(&environment, error)) {
        SetError(error, "desktop JVM object call tokens are invalid");
        return false;
    }
    jobject value = is_static
                        ? environment->CallStaticObjectMethod(
                              static_cast<jclass>(receiver_reference->value),
                              method_reference->value)
                        : environment->CallObjectMethod(
                              receiver_reference->value,
                              method_reference->value);
    *result = AddReferenceLocked(value, false);
    return true;
}

bool DesktopJvm::CallVoidMethod(Handle receiver, Handle method,
                                std::string* error) {
    const std::lock_guard lock(mutex_);
    JNIEnv* environment = nullptr;
    const Reference* receiver_reference = FindReferenceLocked(receiver);
    const Method* method_reference = FindMethodLocked(method);
    if (receiver_reference == nullptr || method_reference == nullptr ||
        method_reference->is_static ||
        !CurrentEnvironment(&environment, error)) {
        SetError(error, "desktop JVM void call tokens are invalid");
        return false;
    }
    environment->CallVoidMethodA(receiver_reference->value,
                                 method_reference->value, nullptr);
    return true;
}

bool DesktopJvm::GetObjectClass(Handle object, Handle* result,
                                std::string* error) {
    const std::lock_guard lock(mutex_);
    JNIEnv* environment = nullptr;
    const Reference* object_reference = FindReferenceLocked(object);
    if (result == nullptr || object_reference == nullptr ||
        !CurrentEnvironment(&environment, error)) {
        SetError(error, "desktop JVM object token is invalid");
        return false;
    }
    *result = AddReferenceLocked(
        environment->GetObjectClass(object_reference->value), false);
    return true;
}

bool DesktopJvm::NewGlobalRef(Handle source, Handle* result,
                              std::string* error) {
    const std::lock_guard lock(mutex_);
    JNIEnv* environment = nullptr;
    const Reference* source_reference = FindReferenceLocked(source);
    if (result == nullptr || source_reference == nullptr ||
        !CurrentEnvironment(&environment, error)) {
        SetError(error, "desktop JVM reference token is invalid");
        return false;
    }
    *result = AddReferenceLocked(
        environment->NewGlobalRef(source_reference->value), true);
    return true;
}

bool DesktopJvm::DeleteLocalRef(Handle reference, std::string* error) {
    const std::lock_guard lock(mutex_);
    JNIEnv* environment = nullptr;
    const auto found = references_.find(reference);
    if (found == references_.end() || found->second.global ||
        found->second.owner != std::this_thread::get_id() ||
        !CurrentEnvironment(&environment, error)) {
        SetError(error, "desktop JVM local reference token is invalid");
        return false;
    }
    environment->DeleteLocalRef(found->second.value);
    references_.erase(found);
    return true;
}

bool DesktopJvm::DeleteGlobalRef(Handle reference, std::string* error) {
    const std::lock_guard lock(mutex_);
    JNIEnv* environment = nullptr;
    const auto found = references_.find(reference);
    if (found == references_.end() || !found->second.global ||
        !CurrentEnvironment(&environment, error)) {
        SetError(error, "desktop JVM global reference token is invalid");
        return false;
    }
    environment->DeleteGlobalRef(found->second.value);
    references_.erase(found);
    return true;
}

bool DesktopJvm::DeleteMethod(Handle method, std::string* error) {
    const std::lock_guard lock(mutex_);
    if (methods_.erase(method) == 1) return true;
    SetError(error, "desktop JVM method token is invalid");
    return false;
}

bool DesktopJvm::IsSameObject(Handle left, Handle right, bool* result,
                              std::string* error) {
    const std::lock_guard lock(mutex_);
    JNIEnv* environment = nullptr;
    const Reference* left_reference =
        left == 0 ? nullptr : FindReferenceLocked(left);
    const Reference* right_reference =
        right == 0 ? nullptr : FindReferenceLocked(right);
    if (result == nullptr || (left != 0 && left_reference == nullptr) ||
        (right != 0 && right_reference == nullptr) ||
        !CurrentEnvironment(&environment, error)) {
        SetError(error, "desktop JVM identity tokens are invalid");
        return false;
    }
    *result = environment->IsSameObject(
                  left_reference != nullptr ? left_reference->value : nullptr,
                  right_reference != nullptr ? right_reference->value : nullptr) ==
              JNI_TRUE;
    return true;
}

bool DesktopJvm::ExceptionCheck(bool* result, std::string* error) {
    const std::lock_guard lock(mutex_);
    JNIEnv* environment = nullptr;
    if (result == nullptr || !CurrentEnvironment(&environment, error))
        return false;
    *result = environment->ExceptionCheck() == JNI_TRUE;
    return true;
}

bool DesktopJvm::ExceptionClear(std::string* error) {
    const std::lock_guard lock(mutex_);
    JNIEnv* environment = nullptr;
    if (!CurrentEnvironment(&environment, error)) return false;
    environment->ExceptionClear();
    return true;
}

std::size_t DesktopJvm::reference_count() const {
    const std::lock_guard lock(mutex_);
    return references_.size();
}

std::size_t DesktopJvm::method_count() const {
    const std::lock_guard lock(mutex_);
    return methods_.size();
}

std::size_t DesktopJvm::attached_thread_count() const {
    const std::lock_guard lock(mutex_);
    return backend_attached_threads_.size();
}

}  // namespace isomr
