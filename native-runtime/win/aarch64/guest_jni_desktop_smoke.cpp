#include "bionic_runtime.h"
#include "desktop_jvm.h"

#include <array>
#include <atomic>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace {

int Fail(std::string_view stage, const std::string& error = {}) {
    std::fprintf(stderr, "ISOMR_GUEST_JNI_DESKTOP_FAIL stage=%.*s error=%s\n",
                 static_cast<int>(stage.size()), stage.data(), error.c_str());
    return 1;
}

std::string Utf8(const wchar_t* text) {
    if (text == nullptr) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0,
                                         nullptr, nullptr);
    if (size <= 1) return {};
    std::string result(static_cast<std::size_t>(size), '\0');
    (void)WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), size,
                              nullptr, nullptr);
    result.pop_back();
    return result;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc != 3) return Fail("arguments");

    isomr::DesktopJvm jvm;
    std::string error;
    const std::string class_path = Utf8(argv[2]);
    if (!jvm.Start(argv[1], class_path, &error)) return Fail("jvm-start", error);

    constexpr std::uint64_t kHeapBase = 0x730000000000ULL;
    constexpr std::uint64_t kImportBase = 0x690000000000ULL;
    isomr::BionicRuntime runtime(
        kHeapBase, 64U * 1024U, kImportBase,
        [](std::uint64_t, std::string*) -> isomr::BionicThreads::GuestCall {
            return {};
        },
        {}, {}, 4096, &jvm);
    if (!runtime.Initialize(&error) || !runtime.AttachCurrentThread(&error))
        return Fail("runtime-start", error);

    std::uint64_t compatibility_reference = 1;
    if (runtime.jni().CreateCompatibilityClassReference(
            "isomr/Probe", &compatibility_reference, &error) ||
        compatibility_reference != 0 ||
        error.find("desktop JVM backend") == std::string::npos ||
        runtime.jni().CreateCompatibilityTypedObjectReference(
            "Lisomr/Probe;", &compatibility_reference, &error) ||
        compatibility_reference != 0 ||
        runtime.jni().CreateCompatibilityStringReference(
            "probe", &compatibility_reference, &error) ||
        compatibility_reference != 0)
        return Fail("compatibility-refusal", error);

    const std::uint64_t buffer = runtime.heap().Callocate(1, 512);
    const std::optional<std::uint64_t> environment =
        runtime.jni().CurrentEnvironmentAddress();
    const std::uint64_t vm = runtime.jni().vm_address();
    if (buffer == 0 || !environment || vm == 0)
        return Fail("guest-allocation");

    const auto call = [&](std::string_view name, std::uint64_t first = 0,
                          std::uint64_t second = 0,
                          std::uint64_t third = 0,
                          std::uint64_t fourth = 0)
        -> std::optional<std::uint64_t> {
        isomr::Aarch64ImportRegistry::Registers registers{};
        registers[0] = first;
        registers[1] = second;
        registers[2] = third;
        registers[3] = fourth;
        std::string dispatch_error;
        if (!runtime.imports().Dispatch(name, &registers, &dispatch_error)) {
            error = std::move(dispatch_error);
            return std::nullopt;
        }
        return registers[0];
    };
    const auto write_text = [&](std::uint64_t offset, std::string_view text) {
        std::string terminated(text);
        terminated.push_back('\0');
        return runtime.heap().Write(buffer + offset, terminated.data(),
                                    terminated.size());
    };

    constexpr std::array<std::uint8_t, 32> kEmptyVaList{};
    if (!write_text(0, "isomr/Probe") ||
        !write_text(64, "probe") ||
        !write_text(96, "()Lisomr/Probe;") ||
        !runtime.heap().Write(buffer + 160, kEmptyVaList.data(),
                              kEmptyVaList.size()))
        return Fail("guest-input");

    const std::optional<std::uint64_t> version =
        call("__isomr_jni_get_version", *environment);
    const std::optional<std::uint64_t> clazz =
        call("__isomr_jni_find_class", *environment, buffer);
    if (!version || *version != 0x00010006U || !clazz || *clazz == 0)
        return Fail("find-class", error);
    const bool class_pass = true;

    if (call("__isomr_jni_get_static_field_id", *environment, *clazz) ||
        error.find("unavailable with an active backend") ==
            std::string::npos)
        return Fail("static-field-refusal", error);
    if (call("__isomr_jni_get_static_int_field", *environment, *clazz) ||
        error.find("unavailable with an active backend") ==
            std::string::npos)
        return Fail("static-int-field-refusal", error);
    const bool static_field_refusal_pass = true;

    if (call("__isomr_jni_new_local_ref", *environment, *clazz) ||
        error.find("unavailable with an active backend") ==
            std::string::npos)
        return Fail("local-ref-refusal", error);
    const bool local_ref_refusal_pass = true;

    if (call("__isomr_jni_exception_occurred", *environment) ||
        error.find("unavailable with an active backend") ==
            std::string::npos)
        return Fail("exception-object-refusal", error);
    const bool exception_object_refusal_pass = true;

    if (call("__isomr_jni_new_string", *environment, buffer + 224, 1) ||
        error.find("unavailable with an active backend") ==
            std::string::npos ||
        call("__isomr_jni_get_string_length", *environment, *clazz) ||
        error.find("unavailable with an active backend") ==
            std::string::npos ||
        call("__isomr_jni_get_string_chars", *environment, *clazz,
             buffer + 224) ||
        error.find("unavailable with an active backend") ==
            std::string::npos ||
        call("__isomr_jni_release_string_chars", *environment, *clazz,
             buffer + 224) ||
        error.find("unavailable with an active backend") ==
            std::string::npos)
        return Fail("string-chars-refusal", error);
    const bool string_chars_refusal_pass = true;

    if (!write_text(64, "<init>") || !write_text(96, "()V"))
        return Fail("constructor-input");
    const std::optional<std::uint64_t> constructor =
        call("__isomr_jni_get_method_id", *environment, *clazz,
             buffer + 64, buffer + 96);
    if (!constructor || *constructor == 0 ||
        call("__isomr_jni_new_object_v", *environment, *clazz,
             *constructor, buffer + 160) ||
        error.find("unavailable with an active backend") ==
            std::string::npos)
        return Fail("constructor-refusal", error);
    const bool constructor_refusal_pass = true;

    if (!write_text(224, "native-string") ||
        call("__isomr_jni_new_string_utf", *environment, buffer + 224) ||
        error.find("unavailable with an active backend") ==
            std::string::npos)
        return Fail("string-refusal", error);
    const bool string_refusal_pass = true;

    if (!write_text(64, "probe") ||
        !write_text(96, "()Lisomr/Probe;"))
        return Fail("static-input");
    const std::optional<std::uint64_t> static_method =
        call("__isomr_jni_get_static_method_id", *environment, *clazz,
             buffer + 64, buffer + 96);
    const std::optional<std::uint64_t> static_object =
        static_method && *static_method != 0
            ? call("__isomr_jni_call_static_object_method_v", *environment,
                   *clazz, *static_method, buffer + 160)
            : std::nullopt;
    if (!write_text(64, "instanceProbe") ||
        !write_text(96, "()Lisomr/Probe;"))
        return Fail("instance-input");
    const std::optional<std::uint64_t> instance_method =
        call("__isomr_jni_get_method_id", *environment, *clazz,
             buffer + 64, buffer + 96);
    const std::optional<std::uint64_t> instance_object =
        instance_method && *instance_method != 0 && static_object &&
                *static_object != 0
            ? call("__isomr_jni_call_object_method_v", *environment,
                   *static_object, *instance_method, buffer + 160)
            : std::nullopt;
    if (!static_method || *static_method == 0 || !static_object ||
        *static_object == 0 || !instance_method || *instance_method == 0 ||
        !instance_object || *instance_object == 0)
        return Fail("method-calls", error);

    if (!write_text(0, "isomr/Probe") ||
        !write_text(64, "voidProbe") || !write_text(96, "()V"))
        return Fail("void-method-input");
    const std::optional<std::uint64_t> void_class =
        call("__isomr_jni_find_class", *environment, buffer);
    const std::optional<std::uint64_t> void_method =
        void_class && *void_class != 0
            ? call("__isomr_jni_get_method_id", *environment, *void_class,
                   buffer + 64, buffer + 96)
            : std::nullopt;
    const bool void_class_released =
        void_class && *void_class != 0 &&
        call("__isomr_jni_delete_local_ref", *environment,
             *void_class).has_value();
    const std::optional<std::uint64_t> void_result =
        void_method && *void_method != 0
            ? call("__isomr_jni_call_void_method_v", *environment,
                   *static_object, *void_method, buffer + 160)
            : std::nullopt;
    if (!write_text(64, "voidProbeResult") ||
        !write_text(96, "()Lisomr/Probe;"))
        return Fail("void-result-input");
    const std::optional<std::uint64_t> void_result_method =
        call("__isomr_jni_get_method_id", *environment, *clazz,
             buffer + 64, buffer + 96);
    const std::optional<std::uint64_t> void_state =
        void_result_method && *void_result_method != 0
            ? call("__isomr_jni_call_object_method_v", *environment,
                   *static_object, *void_result_method, buffer + 160)
            : std::nullopt;
    if (!void_class_released || !void_method || *void_method == 0 ||
        !void_result ||
        *void_result != 0 || !void_result_method ||
        *void_result_method == 0 || !void_state || *void_state == 0 ||
        call("__isomr_jni_is_same_object", *environment,
             *static_object, *void_state) != 1)
        return Fail("void-method", error);

    if (!write_text(64, "staticVoidProbe") || !write_text(96, "()V"))
        return Fail("void-static-input");
    const std::optional<std::uint64_t> static_void_method =
        call("__isomr_jni_get_static_method_id", *environment, *clazz,
             buffer + 64, buffer + 96);
    if (!write_text(0, "java/lang/Thread") ||
        !write_text(64, "interrupt") || !write_text(96, "()V"))
        return Fail("void-other-class-input");
    const std::optional<std::uint64_t> other_class =
        call("__isomr_jni_find_class", *environment, buffer);
    const std::optional<std::uint64_t> other_void_method =
        other_class && *other_class != 0
            ? call("__isomr_jni_get_method_id", *environment,
                   *other_class, buffer + 64, buffer + 96)
            : std::nullopt;
    std::array<std::uint8_t, 32> malformed_va_list{};
    malformed_va_list[24] = 1;
    if (!runtime.heap().Write(buffer + 192, malformed_va_list.data(),
                              malformed_va_list.size()) ||
        !static_void_method || *static_void_method == 0 || !other_class ||
        *other_class == 0 || !other_void_method ||
        *other_void_method == 0 ||
        call("__isomr_jni_call_void_method_v", 1, *static_object,
             *void_method, buffer + 160) ||
        call("__isomr_jni_call_void_method_v", *environment, 1,
             *void_method, buffer + 160) ||
        call("__isomr_jni_call_void_method_v", *environment,
             *static_object, 1, buffer + 160) ||
        call("__isomr_jni_call_void_method_v", *environment,
             *static_object, *static_void_method, buffer + 160) ||
        call("__isomr_jni_call_void_method_v", *environment,
             *static_object, *instance_method, buffer + 160) ||
        call("__isomr_jni_call_void_method_v", *environment,
             *static_object, *other_void_method, buffer + 160) ||
        call("__isomr_jni_call_void_method_v", *environment,
             *static_object, *void_method, 0) ||
        call("__isomr_jni_call_void_method_v", *environment,
             *static_object, *void_method, 1) ||
        call("__isomr_jni_call_void_method_v", *environment,
             *static_object, *void_method, buffer + 192))
        return Fail("void-method-refusal", error);
    const bool void_method_pass = true;

    const std::optional<std::uint64_t> global_object =
        call("__isomr_jni_new_global_ref", *environment, *instance_object);
    const std::optional<std::uint64_t> object_class =
        global_object && *global_object != 0
            ? call("__isomr_jni_get_object_class", *environment,
                   *global_object)
            : std::nullopt;
    const bool identity_pass =
        global_object && *global_object != 0 && object_class &&
        *object_class != 0 &&
        call("__isomr_jni_is_same_object", *environment, *static_object,
             *instance_object) == 1 &&
        call("__isomr_jni_is_same_object", *environment, *instance_object,
             *global_object) == 1 &&
        call("__isomr_jni_is_same_object", *environment, 0, 0) == 1;
    if (!identity_pass) return Fail("identity", error);

    if (!write_text(64, "missingProbe") ||
        !write_text(96, "()Lisomr/Probe;"))
        return Fail("missing-input");
    const std::optional<std::uint64_t> missing_method =
        call("__isomr_jni_get_method_id", *environment, *clazz,
             buffer + 64, buffer + 96);
    const bool exception_pass =
        missing_method && *missing_method == 0 &&
        call("__isomr_jni_exception_check", *environment) == 1 &&
        call("__isomr_jni_exception_clear", *environment).has_value() &&
        call("__isomr_jni_exception_check", *environment) == 0;
    if (!exception_pass) return Fail("exception", error);

    std::atomic<bool> thread_pass{false};
    std::thread worker([&] {
        std::string thread_error;
        if (!runtime.thread_state().AttachCurrentThread(&thread_error)) return;
        isomr::Aarch64ImportRegistry::Registers attach{};
        attach[0] = vm;
        attach[1] = buffer + 224;
        const bool attached = runtime.imports().Dispatch(
            "__isomr_jvm_attach", &attach, &thread_error);
        std::uint64_t worker_environment = 0;
        const bool environment_written =
            runtime.heap().Read(buffer + 224, &worker_environment,
                                sizeof(worker_environment));
        isomr::Aarch64ImportRegistry::Registers cross_thread_local{};
        cross_thread_local[0] = worker_environment;
        cross_thread_local[1] = *clazz;
        cross_thread_local[2] = buffer + 64;
        cross_thread_local[3] = buffer + 96;
        const bool local_reference_rejected =
            attached && environment_written && worker_environment != 0 &&
            !runtime.imports().Dispatch(
                "__isomr_jni_get_static_method_id", &cross_thread_local,
                &thread_error);
        isomr::Aarch64ImportRegistry::Registers cross_thread_void{};
        cross_thread_void[0] = *environment;
        cross_thread_void[1] = *static_object;
        cross_thread_void[2] = *void_method;
        cross_thread_void[3] = buffer + 160;
        const bool void_method_rejected =
            !runtime.imports().Dispatch(
                "__isomr_jni_call_void_method_v", &cross_thread_void,
                &thread_error);
        cross_thread_void[0] = worker_environment;
        const bool local_void_method_rejected =
            !runtime.imports().Dispatch(
                "__isomr_jni_call_void_method_v", &cross_thread_void,
                &thread_error);
        isomr::Aarch64ImportRegistry::Registers detach{};
        detach[0] = vm;
        const bool detached = attached && runtime.imports().Dispatch(
                                              "__isomr_jvm_detach", &detach,
                                              &thread_error);
        const bool state_detached =
            runtime.thread_state().DetachCurrentThread(&thread_error);
        thread_pass.store(attached && attach[0] == 0 && environment_written &&
                              worker_environment != 0 && detached &&
                              detach[0] == 0 && state_detached &&
                              local_reference_rejected &&
                              void_method_rejected &&
                              local_void_method_rejected,
                          std::memory_order_relaxed);
    });
    worker.join();
    if (!thread_pass.load(std::memory_order_relaxed) ||
        runtime.jni().environment_count() != 1 ||
        jvm.attached_thread_count() != 0)
        return Fail("threads", error);

    const bool deleted =
        call("__isomr_jni_delete_local_ref", *environment,
             *object_class).has_value() &&
        call("__isomr_jni_delete_local_ref", *environment,
             *instance_object).has_value() &&
        call("__isomr_jni_delete_local_ref", *environment,
             *void_state).has_value() &&
        call("__isomr_jni_delete_local_ref", *environment,
             *static_object).has_value() &&
        call("__isomr_jni_delete_local_ref", *environment,
             *other_class).has_value() &&
        call("__isomr_jni_delete_global_ref", *environment,
             *global_object).has_value() &&
        call("__isomr_jni_delete_local_ref", *environment,
             *clazz).has_value();
    if (!deleted || runtime.jni().class_reference_count() != 0 ||
        runtime.jni().object_reference_count() != 0 ||
        runtime.jni().global_reference_count() != 0 ||
        !runtime.heap().Release(buffer, &error) ||
        !runtime.DetachCurrentThread(&error) || !runtime.Shutdown(&error) ||
        runtime.jni().environment_count() != 0 ||
        runtime.jni().class_reference_count() != 0 ||
        runtime.jni().method_reference_count() != 0 ||
        runtime.jni().object_reference_count() != 0 ||
        runtime.jni().global_reference_count() != 0 ||
        runtime.heap().allocation_count() != 0 ||
        jvm.reference_count() != 0 || jvm.method_count() != 0 ||
        jvm.attached_thread_count() != 0 || !jvm.Shutdown(&error))
        return Fail("cleanup", error);

    std::puts("ISOMR_GUEST_JNI_DESKTOP class=pass methods=pass void-method=slot62-backend-pass-refusal-pass static-field=refused static-int-field=refused local-ref=refused constructor=refused string=refused string-utf16=refused string-length-chars=refused identity=pass exception-object=refused exception=pass threads=pass cleanup=pass");
    std::puts("ISOMR_NATIVE_GUEST_JNI_DESKTOP=pass");
    return class_pass && static_field_refusal_pass &&
                   local_ref_refusal_pass && constructor_refusal_pass &&
                   string_refusal_pass &&
                   exception_object_refusal_pass && string_chars_refusal_pass &&
                   void_method_pass
               ? 0
               : 1;
}
