#pragma once

#include "guest_jni_backend.h"

#include <jni.h>
#include <windows.h>

#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace isomr {

class DesktopJvm final : public GuestJniBackend {
public:
    DesktopJvm() = default;
    ~DesktopJvm() override;
    DesktopJvm(const DesktopJvm&) = delete;
    DesktopJvm& operator=(const DesktopJvm&) = delete;

    bool Start(const std::wstring& jvm_path, const std::string& class_path,
               std::string* error);
    bool Shutdown(std::string* error);

    bool AttachCurrentThread(std::string* error) override;
    bool DetachCurrentThread(std::string* error) override;
    std::uint32_t GetVersion() override;
    bool FindClass(std::string_view name, Handle* result,
                   std::string* error) override;
    bool GetMethodId(Handle clazz, std::string_view name,
                     std::string_view signature, bool is_static,
                     Handle* result, std::string* error) override;
    bool CallObjectMethod(Handle receiver, Handle method, bool is_static,
                          Handle* result, std::string* error) override;
    bool CallVoidMethod(Handle receiver, Handle method,
                        std::string* error) override;
    bool GetObjectClass(Handle object, Handle* result,
                        std::string* error) override;
    bool NewGlobalRef(Handle source, Handle* result,
                      std::string* error) override;
    bool DeleteLocalRef(Handle reference, std::string* error) override;
    bool DeleteGlobalRef(Handle reference, std::string* error) override;
    bool DeleteMethod(Handle method, std::string* error) override;
    bool IsSameObject(Handle left, Handle right, bool* result,
                      std::string* error) override;
    bool ExceptionCheck(bool* result, std::string* error) override;
    bool ExceptionClear(std::string* error) override;

    std::size_t reference_count() const override;
    std::size_t method_count() const override;
    std::size_t attached_thread_count() const override;

private:
    struct Reference {
        jobject value = nullptr;
        bool global = false;
        std::thread::id owner;
    };
    struct Method {
        jmethodID value = nullptr;
        bool is_static = false;
    };

    bool CurrentEnvironment(JNIEnv** environment, std::string* error) const;
    Handle AddReferenceLocked(jobject value, bool global);
    const Reference* FindReferenceLocked(Handle handle) const;
    const Method* FindMethodLocked(Handle handle) const;

    using CreateJavaVm = jint(JNICALL*)(JavaVM**, void**, void*);

    mutable std::mutex mutex_;
    HMODULE module_ = nullptr;
    JavaVM* vm_ = nullptr;
    std::thread::id creator_thread_;
    std::unordered_set<std::thread::id> backend_attached_threads_;
    std::unordered_map<Handle, Reference> references_;
    std::unordered_map<Handle, Method> methods_;
    Handle next_handle_ = 1;
    bool started_ = false;
    bool shutdown_ = false;
};

}  // namespace isomr
