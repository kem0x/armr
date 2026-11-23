#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace isomr {

// Host JNI objects never cross this interface. GuestJni receives only opaque,
// backend-owned tokens and continues to allocate its own guest-visible handles.
class GuestJniBackend {
public:
    using Handle = std::uint64_t;

    virtual ~GuestJniBackend() = default;

    virtual bool AttachCurrentThread(std::string* error) = 0;
    virtual bool DetachCurrentThread(std::string* error) = 0;
    virtual std::uint32_t GetVersion() = 0;
    virtual bool FindClass(std::string_view name, Handle* result,
                           std::string* error) = 0;
    virtual bool GetMethodId(Handle clazz, std::string_view name,
                             std::string_view signature, bool is_static,
                             Handle* result, std::string* error) = 0;
    virtual bool CallObjectMethod(Handle receiver, Handle method,
                                  bool is_static, Handle* result,
                                  std::string* error) = 0;
    virtual bool CallVoidMethod(Handle receiver, Handle method,
                                std::string* error) = 0;
    virtual bool GetObjectClass(Handle object, Handle* result,
                                std::string* error) = 0;
    virtual bool NewGlobalRef(Handle source, Handle* result,
                              std::string* error) = 0;
    virtual bool DeleteLocalRef(Handle reference, std::string* error) = 0;
    virtual bool DeleteGlobalRef(Handle reference, std::string* error) = 0;
    virtual bool DeleteMethod(Handle method, std::string* error) = 0;
    virtual bool IsSameObject(Handle left, Handle right, bool* result,
                              std::string* error) = 0;
    virtual bool ExceptionCheck(bool* result, std::string* error) = 0;
    virtual bool ExceptionClear(std::string* error) = 0;

    virtual std::size_t reference_count() const = 0;
    virtual std::size_t method_count() const = 0;
    virtual std::size_t attached_thread_count() const = 0;
};

}  // namespace isomr
