#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "aarch64_profile_runtime.h"
#include "bionic_runtime.h"
#include "candy_entry_bridge.h"
#include "candy_entry_fixture.h"
#include "guest_address_space.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

constexpr std::uint64_t kModuleBase = 0x4a00000000ULL;
constexpr std::uint64_t kMissingModuleBase = 0x4b00000000ULL;
constexpr std::uint64_t kEmptyAnalyticsModuleBase = 0x4c00000000ULL;
constexpr std::uint64_t kWorkerFailureModuleBase = 0x4d00000000ULL;
constexpr std::uint64_t kPumpFailureModuleBase = 0x4e00000000ULL;
constexpr std::uint64_t kBatchFailureModuleBase = 0x4f00000000ULL;
constexpr std::uint64_t kInFlightWorkerFailureModuleBase = 0x5000000000ULL;
constexpr std::uint64_t kCloseLifecycleModuleBase = 0x5100000000ULL;

struct GuestVaList {
    std::uint64_t stack;
    std::uint64_t general_top;
    std::uint64_t vector_top;
    std::int32_t general_offset;
    std::int32_t vector_offset;
};

static_assert(sizeof(GuestVaList) == 32);

class TemporaryFixture {
public:
    TemporaryFixture() {
        wchar_t directory[MAX_PATH]{};
        if (GetTempPathW(MAX_PATH, directory) == 0 ||
            GetTempFileNameW(directory, L"ice", 0, path_) == 0)
            path_[0] = L'\0';
    }
    ~TemporaryFixture() {
        if (path_[0] != L'\0') (void)DeleteFileW(path_);
    }
    bool Write(const std::vector<std::uint8_t>& bytes) const {
        if (path_[0] == L'\0') return false;
        FILE* file = nullptr;
        if (_wfopen_s(&file, path_, L"wb") != 0 || file == nullptr)
            return false;
        const bool ok = std::fwrite(bytes.data(), 1, bytes.size(), file) ==
                        bytes.size();
        return std::fclose(file) == 0 && ok;
    }
    const wchar_t* path() const { return path_; }

private:
    wchar_t path_[MAX_PATH]{};
};

bool MakeSecondAnalyticsCallbackFail(
    std::vector<std::uint8_t>* bytes) {
    constexpr std::size_t kAnalyticsAddress = 0x1800;
    constexpr std::size_t kOriginalCallbackSize = 44;
    constexpr std::size_t kPrefixSize = 16;
    constexpr std::size_t kFailureInstructionSize = 4;
    constexpr std::size_t kAnalyticsIndexOffset =
        offsetof(isomr::test::CandyEntryFixtureState, analytics_index);
    static_assert(kAnalyticsIndexOffset % 8 == 0);
    if (bytes == nullptr ||
        bytes->size() <
            kAnalyticsAddress + kPrefixSize + kOriginalCallbackSize +
                kFailureInstructionSize)
        return false;

    std::memmove(bytes->data() + kAnalyticsAddress + kPrefixSize,
                 bytes->data() + kAnalyticsAddress,
                 kOriginalCallbackSize);
    const std::uint32_t load_analytics_index =
        0xf9400000U |
        (static_cast<std::uint32_t>(kAnalyticsIndexOffset / 8) << 10) |
        (9U << 5) | 11U;
    const std::array<std::uint32_t, 4> prefix{
        0xd0000009U,  // adrp x9,state page
        0x91040129U,  // add x9,x9,#0x100
        load_analytics_index,
        0xb500018bU,  // cbnz x11,+48
    };
    std::memcpy(bytes->data() + kAnalyticsAddress, prefix.data(),
                sizeof(prefix));
    const std::uint32_t unsupported_instruction = 0;
    std::memcpy(bytes->data() + kAnalyticsAddress + kPrefixSize +
                    kOriginalCallbackSize,
                &unsupported_instruction,
                sizeof(unsupported_instruction));
    return true;
}

int Fail(std::string_view stage, const std::string& error = {}) {
    std::fprintf(stderr, "ISOMR_CANDY_ENTRY_FIXTURE stage=%.*s result=fail error=%s\n",
                 static_cast<int>(stage.size()), stage.data(), error.c_str());
    return 1;
}

bool PostPointerSequence(std::string* error) {
    HWND window = FindWindowW(L"ArmrNativeGlesWindow", L"ARMR");
    DWORD process = 0;
    if (window == nullptr ||
        GetWindowThreadProcessId(window, &process) == 0 ||
        process != GetCurrentProcessId()) {
        *error = "owned native GLES window was not found";
        return false;
    }
    if (!PostMessageW(window, WM_LBUTTONDOWN, MK_LBUTTON,
                      MAKELPARAM(10, 20)) ||
        !PostMessageW(window, WM_MOUSEMOVE, MK_LBUTTON,
                      MAKELPARAM(300, 200)) ||
        !PostMessageW(window, WM_MOUSEMOVE, MK_LBUTTON,
                      MAKELPARAM(640, 360)) ||
        !PostMessageW(window, WM_LBUTTONUP, 0,
                      MAKELPARAM(1500, 900))) {
        *error = "native GLES pointer messages could not be posted";
        return false;
    }
    return true;
}

bool PostClose(std::string* error) {
    HWND window = FindWindowW(L"ArmrNativeGlesWindow", L"ARMR");
    DWORD process = 0;
    if (window == nullptr ||
        GetWindowThreadProcessId(window, &process) == 0 ||
        process != GetCurrentProcessId()) {
        *error = "owned native GLES window was not found for close";
        return false;
    }
    if (!PostMessageW(window, WM_CLOSE, 0, 0)) {
        *error = "native GLES close message could not be posted";
        return false;
    }
    return true;
}

bool Dispatch(isomr::Aarch64ProfileRuntime* runtime, std::string_view name,
              std::array<std::uint64_t, 8> values, std::uint64_t* result,
              std::string* error) {
    isomr::Aarch64ImportRegistry::Registers registers{};
    registers.general = values;
    if (!runtime->bionic()->imports().Dispatch(name, &registers, error))
        return false;
    if (result != nullptr) *result = registers[0];
    return true;
}

bool QueueAnalyticsValueRequest(isomr::Aarch64ProfileRuntime* runtime,
                                std::uint64_t environment,
                                std::uint64_t activity,
                                std::string_view key,
                                std::string* error) {
    constexpr char kClassName[] =
        "com/king/usdk/kdid/AnalyticsReceiver";
    constexpr char kMethodName[] = "getValue";
    constexpr char kMethodSignature[] =
        "(Ljava/lang/String;Landroid/app/Activity;)V";
    constexpr std::size_t kScratchSize = 384;
    constexpr std::uint64_t kMethodOffset = 64;
    constexpr std::uint64_t kSignatureOffset = 96;
    constexpr std::uint64_t kArgumentsOffset = 192;
    constexpr std::uint64_t kVaListOffset = 256;

    if (runtime == nullptr || error == nullptr) return false;
    auto& heap = runtime->bionic()->heap();
    const std::optional<std::uint64_t> scratch = heap.Allocate(kScratchSize);
    if (!scratch) {
        *error = "analytics request scratch allocation failed";
        return false;
    }
    std::uint64_t key_reference = 0;
    std::uint64_t class_reference = 0;
    auto cleanup = [&] {
        std::string cleanup_error;
        if (class_reference != 0) {
            std::array<std::uint64_t, 8> release_arguments{};
            release_arguments[0] = environment;
            release_arguments[1] = class_reference;
            (void)Dispatch(runtime, "__isomr_jni_delete_local_ref",
                           release_arguments, nullptr, &cleanup_error);
        }
        if (key_reference != 0)
            (void)runtime->bionic()
                ->jni()
                .ReleaseCompatibilityLocalReference(key_reference,
                                                     &cleanup_error);
        (void)heap.Release(*scratch, &cleanup_error);
    };
    if (!heap.Write(*scratch, kClassName, sizeof(kClassName)) ||
        !heap.Write(*scratch + kMethodOffset, kMethodName,
                    sizeof(kMethodName)) ||
        !heap.Write(*scratch + kSignatureOffset, kMethodSignature,
                    sizeof(kMethodSignature)) ||
        !runtime->bionic()->jni().CreateCompatibilityStringReference(
            key, &key_reference, error)) {
        if (error->empty()) *error = "analytics request setup failed";
        cleanup();
        return false;
    }

    std::array<std::uint64_t, 8> arguments{};
    arguments[0] = environment;
    arguments[1] = *scratch;
    if (!Dispatch(runtime, "__isomr_jni_find_class", arguments,
                  &class_reference, error) ||
        class_reference == 0) {
        if (error->empty()) *error = "analytics receiver class is unavailable";
        cleanup();
        return false;
    }
    arguments = {};
    arguments[0] = environment;
    arguments[1] = class_reference;
    arguments[2] = *scratch + kMethodOffset;
    arguments[3] = *scratch + kSignatureOffset;
    std::uint64_t method_reference = 0;
    if (!Dispatch(runtime, "__isomr_jni_get_static_method_id", arguments,
                  &method_reference, error) ||
        method_reference == 0) {
        if (error->empty()) *error = "analytics receiver method is unavailable";
        cleanup();
        return false;
    }

    const std::array<std::uint64_t, 2> values{key_reference, activity};
    const GuestVaList va_list{*scratch + kArgumentsOffset + sizeof(values),
                              *scratch + kArgumentsOffset + sizeof(values),
                              0, -16, 0};
    if (!heap.Write(*scratch + kArgumentsOffset, values.data(),
                    sizeof(values)) ||
        !heap.Write(*scratch + kVaListOffset, &va_list, sizeof(va_list))) {
        *error = "analytics request argument setup failed";
        cleanup();
        return false;
    }
    arguments = {};
    arguments[0] = environment;
    arguments[1] = class_reference;
    arguments[2] = method_reference;
    arguments[3] = *scratch + kVaListOffset;
    if (!Dispatch(runtime, "__isomr_jni_call_static_void_method_v",
                  arguments, nullptr, error)) {
        cleanup();
        return false;
    }
    cleanup();
    return true;
}

bool QueueAgeSignalsRequest(isomr::Aarch64ProfileRuntime* runtime,
                            std::uint64_t environment,
                            std::uint64_t context,
                            std::uint64_t first_callback_handle,
                            std::uint64_t debug_scenario,
                            std::uint64_t second_callback_handle,
                            std::string* error) {
    constexpr char kClassName[] =
        "com/king/usdk/privacyagecompliance/UsdkAgeSignals";
    constexpr char kMethodName[] = "request";
    constexpr char kMethodSignature[] =
        "(Landroid/content/Context;JJJ)V";
    constexpr std::size_t kScratchSize = 384;
    constexpr std::uint64_t kMethodOffset = 64;
    constexpr std::uint64_t kSignatureOffset = 96;
    constexpr std::uint64_t kArgumentsOffset = 192;
    constexpr std::uint64_t kVaListOffset = 256;

    if (runtime == nullptr || error == nullptr) return false;
    auto& heap = runtime->bionic()->heap();
    const std::optional<std::uint64_t> scratch = heap.Allocate(kScratchSize);
    if (!scratch) {
        *error = "Age Signals request scratch allocation failed";
        return false;
    }
    std::uint64_t class_reference = 0;
    auto cleanup = [&] {
        std::string cleanup_error;
        if (class_reference != 0) {
            std::array<std::uint64_t, 8> release_arguments{};
            release_arguments[0] = environment;
            release_arguments[1] = class_reference;
            (void)Dispatch(runtime, "__isomr_jni_delete_local_ref",
                           release_arguments, nullptr, &cleanup_error);
        }
        (void)heap.Release(*scratch, &cleanup_error);
    };
    if (!heap.Write(*scratch, kClassName, sizeof(kClassName)) ||
        !heap.Write(*scratch + kMethodOffset, kMethodName,
                    sizeof(kMethodName)) ||
        !heap.Write(*scratch + kSignatureOffset, kMethodSignature,
                    sizeof(kMethodSignature))) {
        *error = "Age Signals request setup failed";
        cleanup();
        return false;
    }

    std::array<std::uint64_t, 8> arguments{};
    arguments[0] = environment;
    arguments[1] = *scratch;
    if (!Dispatch(runtime, "__isomr_jni_find_class", arguments,
                  &class_reference, error) ||
        class_reference == 0) {
        if (error->empty()) *error = "Age Signals class is unavailable";
        cleanup();
        return false;
    }
    arguments = {};
    arguments[0] = environment;
    arguments[1] = class_reference;
    arguments[2] = *scratch + kMethodOffset;
    arguments[3] = *scratch + kSignatureOffset;
    std::uint64_t method_reference = 0;
    if (!Dispatch(runtime, "__isomr_jni_get_static_method_id", arguments,
                  &method_reference, error) ||
        method_reference == 0) {
        if (error->empty()) *error = "Age Signals method is unavailable";
        cleanup();
        return false;
    }

    const std::array<std::uint64_t, 4> values{
        context, first_callback_handle, debug_scenario,
        second_callback_handle};
    const GuestVaList va_list{*scratch + kArgumentsOffset + sizeof(values),
                              *scratch + kArgumentsOffset + sizeof(values),
                              0, -32, 0};
    if (!heap.Write(*scratch + kArgumentsOffset, values.data(),
                    sizeof(values)) ||
        !heap.Write(*scratch + kVaListOffset, &va_list, sizeof(va_list))) {
        *error = "Age Signals request argument setup failed";
        cleanup();
        return false;
    }
    arguments = {};
    arguments[0] = environment;
    arguments[1] = class_reference;
    arguments[2] = method_reference;
    arguments[3] = *scratch + kVaListOffset;
    if (!Dispatch(runtime, "__isomr_jni_call_static_void_method_v",
                  arguments, nullptr, error)) {
        cleanup();
        return false;
    }
    cleanup();
    return true;
}

bool ReadExactGuestString(isomr::GuestAddressSpace* memory,
                          std::uint64_t address, std::string_view expected) {
    if (memory == nullptr || address == 0) return false;
    std::vector<char> value(expected.size() + 1, '\0');
    return memory->Read(address, value.data(), value.size()) &&
           std::string_view(value.data(), expected.size()) == expected &&
           value.back() == '\0';
}

bool ValidateCompatibilityFacade(isomr::GuestAddressSpace* memory,
                                 std::uint64_t reference,
                                 std::string_view expected_class,
                                 std::string_view expected_payload,
                                 bool payload_is_self,
                                 std::string* error) {
    constexpr std::uint32_t kMagic = 0x4b4c4a4fU;
    std::array<std::uint8_t, 0x40> bytes{};
    if (memory == nullptr || reference == 0 ||
        !memory->Read(reference, bytes.data(), bytes.size())) {
        *error = "compatibility facade header is unreadable";
        return false;
    }
    std::uint32_t magic = 0;
    std::uint32_t pinned = 0;
    std::uint64_t class_address = 0;
    std::uint64_t payload_address = 0;
    std::uint64_t dispatch = 0;
    std::uint64_t self = 0;
    std::memcpy(&magic, bytes.data(), sizeof(magic));
    std::memcpy(&pinned, bytes.data() + 4, sizeof(pinned));
    std::memcpy(&class_address, bytes.data() + 8, sizeof(class_address));
    std::memcpy(&payload_address, bytes.data() + 0x10,
                sizeof(payload_address));
    std::memcpy(&dispatch, bytes.data() + 0x20, sizeof(dispatch));
    std::memcpy(&self, bytes.data() + 0x38, sizeof(self));
    if (magic != kMagic || pinned != 1 || dispatch != 0 ||
        self != reference ||
        !ReadExactGuestString(memory, class_address, expected_class) ||
        (payload_is_self
             ? payload_address != reference
             : !ReadExactGuestString(memory, payload_address,
                                     expected_payload))) {
        *error = "compatibility facade fields do not match";
        return false;
    }
    return true;
}

bool CreateHandles(isomr::Aarch64ProfileRuntime* runtime,
                   isomr::CandyEntryHandles* handles,
                   std::string* error) {
    const auto environment =
        runtime->bionic()->jni().CurrentEnvironmentAddress();
    if (!environment) {
        *error = "current guest JNI environment is unavailable";
        return false;
    }
    handles->environment = *environment;
    isomr::GuestJni& jni = runtime->bionic()->jni();
    return jni.CreateCompatibilityClassReference(
               "com/king/candycrushsaga/PlatformProxy",
               &handles->platform_proxy_class, error) &&
           jni.CreateCompatibilityTypedObjectReference(
               "Lcom/king/candycrushsaga/CandyCrushSagaActivity;",
               &handles->activity, error) &&
           jni.CreateCompatibilityTypedObjectReference(
               "Lcom/king/core/NativeApplication;",
               &handles->native_application, error) &&
           jni.CreateCompatibilityStringReference(
               "1.333.2.1", &handles->version_string, error) &&
           jni.CreateCompatibilityTypedObjectReference(
               "Landroid/content/res/AssetManager;",
               &handles->asset_manager, error);
}

bool ReleaseHandles(isomr::Aarch64ProfileRuntime* runtime,
                    const isomr::CandyEntryHandles& handles,
                    std::string* error) {
    isomr::GuestJni& jni = runtime->bionic()->jni();
    return jni.ReleaseCompatibilityLocalReference(handles.asset_manager,
                                                   error) &&
           jni.ReleaseCompatibilityLocalReference(handles.version_string,
                                                   error) &&
           jni.ReleaseCompatibilityLocalReference(handles.native_application,
                                                   error) &&
           jni.ReleaseCompatibilityLocalReference(handles.activity, error) &&
           jni.ReleaseCompatibilityLocalReference(handles.platform_proxy_class,
                                                   error);
}

isomr::Aarch64ProfileRuntimeConfig Config(std::uint64_t module_base) {
    isomr::Aarch64ProfileRuntimeConfig config;
    config.heap_base = 0x730000000000ULL + (module_base - kModuleBase);
    config.heap_capacity = 256 * 1024;
    config.import_base = 0x6a0000000000ULL + (module_base - kModuleBase);
    config.first_module_base = module_base;
    config.stack_size = 64 * 1024;
    config.instruction_budget = 1000000;
    config.jni_compatibility.package_name =
        "com.king.candycrushsaga";
    config.jni_compatibility.package_code_path = "/app";
    config.jni_compatibility.package_version = "1.333.2.1";
    config.jni_compatibility.splash_removal_class =
        "com/king/candycrushsaga/PlatformProxy";
    return config;
}

}  // namespace

int main() {
    isomr::Aarch64GuestCallTelemetry summary_fixture{};
    constexpr std::array<std::string_view, 9> kSummaryNames{
        "slow import/0x", "alpha", "beta", "rank4", "rank5",
        "rank6",          "rank7", "rank8", "residual"};
    if (!summary_fixture.svc_imports.Prepare(kSummaryNames))
        return Fail("svc-import-summary-prepare");
    const auto add_summary_sample =
        [&](std::uint32_t immediate, std::uint64_t wall_time_ns) {
            ++summary_fixture.svc_invocations;
            summary_fixture.svc_wall_time_ns += wall_time_ns;
            isomr::AccumulateAarch64SvcImportTelemetry(
                &summary_fixture, immediate, wall_time_ns);
        };
    add_summary_sample(1, 1'000'000);
    add_summary_sample(1, 1'000'001);
    add_summary_sample(2, 2'000'000);
    add_summary_sample(3, 2'000'000);
    add_summary_sample(4, 1'900'000);
    add_summary_sample(5, 1'800'000);
    add_summary_sample(6, 1'700'000);
    add_summary_sample(7, 1'600'000);
    add_summary_sample(8, 1'500'000);
    add_summary_sample(9, 1'400'000);
    const std::string summary =
        isomr::FormatSvcImportSummary(summary_fixture);
    const std::string expected_summary =
        " rank1-name=slow%20import%2F0x rank1-count=2 rank1-total-wall-ns=2000001 rank1-max-wall-ns=1000001 rank1-over-1ms-count=1 rank1-over-1ms-wall-ns=1000001"
        " rank2-name=alpha rank2-count=1 rank2-total-wall-ns=2000000 rank2-max-wall-ns=2000000 rank2-over-1ms-count=1 rank2-over-1ms-wall-ns=2000000"
        " rank3-name=beta rank3-count=1 rank3-total-wall-ns=2000000 rank3-max-wall-ns=2000000 rank3-over-1ms-count=1 rank3-over-1ms-wall-ns=2000000"
        " rank4-name=rank4 rank4-count=1 rank4-total-wall-ns=1900000 rank4-max-wall-ns=1900000 rank4-over-1ms-count=1 rank4-over-1ms-wall-ns=1900000"
        " rank5-name=rank5 rank5-count=1 rank5-total-wall-ns=1800000 rank5-max-wall-ns=1800000 rank5-over-1ms-count=1 rank5-over-1ms-wall-ns=1800000"
        " rank6-name=rank6 rank6-count=1 rank6-total-wall-ns=1700000 rank6-max-wall-ns=1700000 rank6-over-1ms-count=1 rank6-over-1ms-wall-ns=1700000"
        " rank7-name=rank7 rank7-count=1 rank7-total-wall-ns=1600000 rank7-max-wall-ns=1600000 rank7-over-1ms-count=1 rank7-over-1ms-wall-ns=1600000"
        " rank8-name=rank8 rank8-count=1 rank8-total-wall-ns=1500000 rank8-max-wall-ns=1500000 rank8-over-1ms-count=1 rank8-over-1ms-wall-ns=1500000"
        " residual-count=1 residual-wall-ns=1400000";
    if (summary != expected_summary)
        return Fail("svc-import-summary-contract");

    isomr::Aarch64GuestCallTelemetry nanosleep_summary_fixture{};
    nanosleep_summary_fixture.enabled = true;
    nanosleep_summary_fixture.mode =
        isomr::Aarch64CallTelemetryMode::kSvcCategory;
    constexpr std::array<std::string_view, 1> kNanosleepSummaryName{
        "nanosleep"};
    if (!nanosleep_summary_fixture.svc_imports.Prepare(
            kNanosleepSummaryName))
        return Fail("nanosleep-summary-prepare");
    nanosleep_summary_fixture.svc_imports.Accumulate(1, 30);
    nanosleep_summary_fixture.svc_imports.Accumulate(1, 50);
    nanosleep_summary_fixture.svc_imports.Accumulate(1, 70);
    nanosleep_summary_fixture.svc_imports.
        AccumulateSuccessfulNanosleepRequest(1, 1'500'001);
    nanosleep_summary_fixture.svc_imports.
        AccumulateSuccessfulNanosleepRequest(1, 15'000'000);
    const std::string expected_nanosleep_summary =
        "ISOMR_AARCH64_CALL_NANOSLEEP ordinal=7 stage=step-2 result=success handler-count=3 handler-total-wall-ns=150 handler-max-wall-ns=70 successful-requested-count=2 requested-total-ns=16500001 requested-max-ns=15000000";
    if (isomr::FormatNanosleepCallSummary(
            7, "step-2", true, nanosleep_summary_fixture) !=
            expected_nanosleep_summary ||
        isomr::FormatNanosleepCallSummary(
            7, "step-2", false, nanosleep_summary_fixture) !=
            "ISOMR_AARCH64_CALL_NANOSLEEP ordinal=7 stage=step-2 result=failure handler-count=3 handler-total-wall-ns=150 handler-max-wall-ns=70 successful-requested-count=2 requested-total-ns=16500001 requested-max-ns=15000000")
        return Fail("nanosleep-summary-contract");
    constexpr std::array<std::string_view, 10> kBelowRankNames{
        "rank1", "rank2", "rank3", "rank4", "rank5",
        "rank6", "rank7", "rank8", "rank9", "nanosleep"};
    isomr::Aarch64GuestCallTelemetry below_rank_fixture{};
    below_rank_fixture.enabled = true;
    below_rank_fixture.mode =
        isomr::Aarch64CallTelemetryMode::kSvcCategory;
    if (!below_rank_fixture.svc_imports.Prepare(kBelowRankNames))
        return Fail("nanosleep-below-rank-prepare");
    for (std::uint32_t immediate = 1; immediate <= 9; ++immediate)
        below_rank_fixture.svc_imports.Accumulate(
            immediate, 1'000 - immediate * 10);
    below_rank_fixture.svc_imports.Accumulate(10, 25);
    below_rank_fixture.svc_imports.
        AccumulateSuccessfulNanosleepRequest(10, 500'000);
    const std::string below_rank_summary =
        isomr::FormatSvcImportSummary(below_rank_fixture);
    if (below_rank_summary.find("-name=nanosleep") !=
            std::string::npos ||
        isomr::FormatNanosleepCallSummary(
            9, "step-4", true, below_rank_fixture) !=
            "ISOMR_AARCH64_CALL_NANOSLEEP ordinal=9 stage=step-4 result=success handler-count=1 handler-total-wall-ns=25 handler-max-wall-ns=25 successful-requested-count=1 requested-total-ns=500000 requested-max-ns=500000")
        return Fail("nanosleep-below-rank-contract");
    constexpr std::array<std::string_view, 12> kClockSummaryNames{
        "rank1",        "rank2",        "rank3",      "rank4",
        "rank5",        "rank6",        "rank7",      "rank8",
        "rank9",        "clock_gettime", "clock_getres", "gettimeofday"};
    isomr::Aarch64GuestCallTelemetry clock_summary_fixture{};
    clock_summary_fixture.enabled = true;
    clock_summary_fixture.mode =
        isomr::Aarch64CallTelemetryMode::kSvcCategory;
    if (!clock_summary_fixture.svc_imports.Prepare(kClockSummaryNames))
        return Fail("clock-summary-prepare");
    for (std::uint32_t immediate = 1; immediate <= 9; ++immediate)
        clock_summary_fixture.svc_imports.Accumulate(
            immediate, 1'000 - immediate * 10);
    clock_summary_fixture.svc_imports.Accumulate(10, 25);
    clock_summary_fixture.svc_imports.Accumulate(10, 35);
    clock_summary_fixture.svc_imports.Accumulate(10, 15);
    clock_summary_fixture.svc_imports.Accumulate(11, 45);
    clock_summary_fixture.svc_imports.Accumulate(12, 55);
    clock_summary_fixture.wall_time_ns = 1'234;
    clock_summary_fixture.clock_gettime_clock_ids[1] = 2;
    clock_summary_fixture.clock_gettime_clock_ids[7] = 1;
    if (isomr::FormatClockCallSummary(
            10, "step-5", true, clock_summary_fixture) !=
        "ISOMR_AARCH64_CALL_CLOCK ordinal=10 stage=step-5 result=success call-wall-ns=1234 clock-gettime-count=3 clock-gettime-wall-ns=75 clock-getres-count=1 clock-getres-wall-ns=45 gettimeofday-count=1 gettimeofday-wall-ns=55 clock-realtime-count=0 clock-monotonic-count=2 clock-process-cpu-count=0 clock-thread-cpu-count=0 clock-boottime-count=1 clock-invalid-count=0")
        return Fail("clock-summary-contract");
    isomr::Aarch64GuestCallTelemetry pc_summary_fixture{};
    pc_summary_fixture.enabled = true;
    pc_summary_fixture.mode = isomr::Aarch64CallTelemetryMode::kFull;
    pc_summary_fixture.guest_pc_histogram = std::make_shared<
        std::array<isomr::Aarch64GuestPcTelemetry, 64>>();
    isomr::AccumulateAarch64GuestPcTelemetry(
        &pc_summary_fixture, 0x4000000100ULL, 0x4000001004ULL,
        0x4000010004ULL, 100, 10, 200, 300, 800, 1'000, 1'100,
        20, 2'200, 2'400);
    isomr::AccumulateAarch64GuestPcTelemetry(
        &pc_summary_fixture, 0x4000000200ULL, 0x4000002004ULL,
        0x4000020004ULL, 50, 7, 70, 80, 600, 700, 800, 9, 900,
        1'000);
    isomr::AccumulateAarch64GuestPcTelemetry(
        &pc_summary_fixture, 0x4000000100ULL, 0x4000001004ULL,
        0x4000010004ULL, 400, 5, 300, 500, 900, 500, 1'200,
        30, 2'100, 2'500);
    if (isomr::FormatGuestPcSummary(pc_summary_fixture) !=
        " total-samples=3 retained=2 rank1-pc=0x4000000100 rank1-lr=0x4000001004 rank1-parent-lr=0x4000010004 rank1-x0-min=100 rank1-x0-max=400 rank1-x1-min=5 rank1-x1-max=10 rank1-x2-min=200 rank1-x2-max=300 rank1-x3-min=300 rank1-x3-max=500 rank1-x8-min=800 rank1-x8-max=900 rank1-x10-min=500 rank1-x10-max=1000 rank1-x11-min=1100 rank1-x11-max=1200 rank1-x19-min=20 rank1-x19-max=30 rank1-x22-min=2100 rank1-x22-max=2200 rank1-x24-min=2400 rank1-x24-max=2500 rank1-count=2 rank1-error=0 rank2-pc=0x4000000200 rank2-lr=0x4000002004 rank2-parent-lr=0x4000020004 rank2-x0-min=50 rank2-x0-max=50 rank2-x1-min=7 rank2-x1-max=7 rank2-x2-min=70 rank2-x2-max=70 rank2-x3-min=80 rank2-x3-max=80 rank2-x8-min=600 rank2-x8-max=600 rank2-x10-min=700 rank2-x10-max=700 rank2-x11-min=800 rank2-x11-max=800 rank2-x19-min=9 rank2-x19-max=9 rank2-x22-min=900 rank2-x22-max=900 rank2-x24-min=1000 rank2-x24-max=1000 rank2-count=1 rank2-error=0")
        return Fail("guest-pc-summary-contract");
    for (const isomr::Aarch64CallTelemetryMode mode : {
             isomr::Aarch64CallTelemetryMode::kOff,
             isomr::Aarch64CallTelemetryMode::kEnvelope,
             isomr::Aarch64CallTelemetryMode::kFull}) {
        nanosleep_summary_fixture.mode = mode;
        if (!isomr::FormatNanosleepCallSummary(
                 7, "step-2", true,
                 nanosleep_summary_fixture).empty())
            return Fail("nanosleep-summary-mode-isolation");
        clock_summary_fixture.mode = mode;
        if (!isomr::FormatClockCallSummary(
                 10, "step-5", true, clock_summary_fixture).empty())
            return Fail("clock-summary-mode-isolation");
    }
    nanosleep_summary_fixture.mode =
        isomr::Aarch64CallTelemetryMode::kSvcCategory;
    isomr::Aarch64GuestCallTelemetry no_successful_nanosleep{};
    no_successful_nanosleep.enabled = true;
    no_successful_nanosleep.mode =
        isomr::Aarch64CallTelemetryMode::kSvcCategory;
    if (!no_successful_nanosleep.svc_imports.Prepare(
            kNanosleepSummaryName))
        return Fail("nanosleep-summary-empty-prepare");
    no_successful_nanosleep.svc_imports.Accumulate(1, 25);
    if (!isomr::FormatNanosleepCallSummary(
             8, "step-3", true, no_successful_nanosleep).empty())
        return Fail("nanosleep-summary-empty");

    static_assert(noexcept(
        nanosleep_summary_fixture.svc_imports.
            AccumulateSuccessfulNanosleepRequest(1, 1)));
    isomr::Aarch64GuestCallTelemetry overflow_fixture{};
    overflow_fixture.enabled = true;
    overflow_fixture.mode =
        isomr::Aarch64CallTelemetryMode::kSvcCategory;
    constexpr std::uint64_t kUint64Maximum =
        (std::numeric_limits<std::uint64_t>::max)();
    if (!overflow_fixture.svc_imports.Prepare(kNanosleepSummaryName))
        return Fail("nanosleep-overflow-count-prepare");
    overflow_fixture.svc_imports.Accumulate(1, 1);
    overflow_fixture.svc_imports.AccumulateSuccessfulNanosleepRequests(
        1, kUint64Maximum - 1, 5, 5);
    overflow_fixture.svc_imports.AccumulateSuccessfulNanosleepRequests(
        1, 1, 6, 6);
    const isomr::Aarch64SvcImportTelemetry& count_maximum =
        overflow_fixture.svc_imports[0];
    if (!count_maximum.nanosleep_requested_accumulation_valid ||
        count_maximum.successful_nanosleep_requests != kUint64Maximum ||
        count_maximum.nanosleep_requested_total_ns != 11 ||
        count_maximum.nanosleep_requested_max_ns != 6)
        return Fail("nanosleep-overflow-count-maximum");
    overflow_fixture.svc_imports.
        AccumulateSuccessfulNanosleepRequest(1, 1);
    if (overflow_fixture.svc_imports[0].
            nanosleep_requested_accumulation_valid ||
        !isomr::FormatNanosleepCallSummary(
             10, "overflow-count", true, overflow_fixture).empty())
        return Fail("nanosleep-overflow-count-suppression");

    if (!overflow_fixture.svc_imports.Prepare(kNanosleepSummaryName))
        return Fail("nanosleep-overflow-total-prepare");
    overflow_fixture.svc_imports.Accumulate(1, 1);
    overflow_fixture.svc_imports.AccumulateSuccessfulNanosleepRequests(
        1, 1, kUint64Maximum - 1, kUint64Maximum - 1);
    overflow_fixture.svc_imports.
        AccumulateSuccessfulNanosleepRequest(1, 1);
    const isomr::Aarch64SvcImportTelemetry& total_maximum =
        overflow_fixture.svc_imports[0];
    if (!total_maximum.nanosleep_requested_accumulation_valid ||
        total_maximum.successful_nanosleep_requests != 2 ||
        total_maximum.nanosleep_requested_total_ns != kUint64Maximum ||
        total_maximum.nanosleep_requested_max_ns !=
            kUint64Maximum - 1)
        return Fail("nanosleep-overflow-total-maximum");
    overflow_fixture.svc_imports.
        AccumulateSuccessfulNanosleepRequest(1, 1);
    if (overflow_fixture.svc_imports[0].
            nanosleep_requested_accumulation_valid ||
        !isomr::FormatNanosleepCallSummary(
             11, "overflow-total", true, overflow_fixture).empty())
        return Fail("nanosleep-overflow-total-suppression");

    if (!overflow_fixture.svc_imports.Prepare(kNanosleepSummaryName))
        return Fail("nanosleep-overflow-reset-prepare");
    overflow_fixture.svc_imports.Accumulate(1, 1);
    overflow_fixture.svc_imports.
        AccumulateSuccessfulNanosleepRequest(1, 7);
    const isomr::Aarch64SvcImportTelemetry& reset_entry =
        overflow_fixture.svc_imports[0];
    if (!reset_entry.nanosleep_requested_accumulation_valid ||
        reset_entry.successful_nanosleep_requests != 1 ||
        reset_entry.nanosleep_requested_total_ns != 7 ||
        reset_entry.nanosleep_requested_max_ns != 7 ||
        isomr::FormatNanosleepCallSummary(
            12, "overflow-reset", true, overflow_fixture).empty())
        return Fail("nanosleep-overflow-reset");

    TemporaryFixture fixture;
    if (!fixture.Write(isomr::test::MakeCandyEntryFixture()))
        return Fail("fixture-write");
    isomr::Aarch64ProfileRuntime runtime(Config(kModuleBase));
    std::string error;
    if (!runtime.Load(fixture.path(), &error) ||
        !runtime.RunInitializers(&error))
        return Fail("runtime-initialize", error);

    const auto telemetry_caller = runtime.FindExport(
        "Java_com_king_core_NativeApplication_step");
    if (!telemetry_caller)
        return Fail("callsite-summary-export");
    isomr::NanosleepCallsiteTelemetrySnapshot callsite_snapshot{};
    callsite_snapshot.enabled = true;
    callsite_snapshot.complete = true;
    callsite_snapshot.generation = 1;
    callsite_snapshot.retained_count = 11;
    for (std::size_t index = 0; index < 10; ++index) {
        auto& bucket = callsite_snapshot.retained[index];
        bucket.guest_link_register = *telemetry_caller;
        bucket.parent_valid = true;
        bucket.parent_guest_link_register =
            index == 1
                ? kModuleBase + isomr::test::kCandyEntryFixtureState
                : *telemetry_caller;
        bucket.logical_thread_id = static_cast<std::uint32_t>(index + 1);
        bucket.success_count = index == 1 ? 10 : 10 - index;
        bucket.requested_total_ns = bucket.success_count * 500000;
        bucket.requested_max_ns = 500000;
        bucket.handler_body_total_ns = bucket.success_count * 100;
        bucket.handler_body_max_ns = 100;
    }
    auto& unresolved_bucket = callsite_snapshot.retained[10];
    unresolved_bucket.guest_link_register =
        std::numeric_limits<std::uint64_t>::max();
    unresolved_bucket.parent_valid = true;
    unresolved_bucket.parent_guest_link_register = *telemetry_caller;
    unresolved_bucket.logical_thread_id = 99;
    unresolved_bucket.success_count = 4;
    unresolved_bucket.requested_total_ns = 2'000'000;
    unresolved_bucket.requested_max_ns = 500000;
    unresolved_bucket.handler_body_total_ns = 400;
    unresolved_bucket.handler_body_max_ns = 100;
    callsite_snapshot.aggregate.success_count = 60;
    callsite_snapshot.aggregate.requested_total_ns = 30'000'000;
    callsite_snapshot.aggregate.requested_max_ns = 500000;
    callsite_snapshot.aggregate.handler_body_total_ns = 6000;
    callsite_snapshot.aggregate.handler_body_max_ns = 100;
    const std::string callsite_summary =
        isomr::FormatNanosleepCallsiteSummaryForTesting(
            &runtime, 17, "fixture-stage", false, callsite_snapshot);
    const std::string callsite_summary_repeat =
        isomr::FormatNanosleepCallsiteSummaryForTesting(
            &runtime, 17, "fixture-stage", false, callsite_snapshot);
    const std::size_t rank1 = callsite_summary.find(
        "rank1-thread=1 rank1-count=10");
    const std::size_t rank2 = callsite_summary.find(
        "rank2-thread=2 rank2-count=10");
    if (callsite_summary != callsite_summary_repeat ||
        !callsite_summary.starts_with(
            "ISOMR_NANOSLEEP_CALLSITE ordinal=17 stage=fixture-stage result=failure complete=1") ||
        rank1 == std::string::npos || rank2 == std::string::npos ||
        rank1 >= rank2 ||
        callsite_summary.find("rank8-count=3") == std::string::npos ||
        callsite_summary.find("unresolved-count=4") ==
            std::string::npos ||
        callsite_summary.find("parent-unresolved-count=10") ==
            std::string::npos ||
        callsite_summary.find("successful-count=60") ==
            std::string::npos ||
        callsite_summary.find("residual-count=7") ==
            std::string::npos ||
        callsite_summary.find("rank1-immediate-offset=0x") ==
            std::string::npos ||
        callsite_summary.find("rank1-parent-offset=0x") ==
            std::string::npos ||
        callsite_summary.find("rank2-parent-unresolved=1") ==
            std::string::npos ||
        callsite_summary.find("rank2-parent-offset=") !=
            std::string::npos ||
        callsite_summary.find("ffffffffffffffff") !=
            std::string::npos ||
        callsite_summary.find("guest-lr") != std::string::npos ||
        callsite_summary.find("parent-guest-lr") != std::string::npos ||
        callsite_summary.find("host-address") != std::string::npos)
        return Fail("callsite-summary-contract");
    std::puts("ISOMR_NANOSLEEP_CALLSITE_FORMAT top=8 deterministic=count-immediate-parent-thread residual=reconciled unresolved=suppressed absolute-addresses=none");

    isomr::CandyEntryHandles handles{};
    if (!CreateHandles(&runtime, &handles, &error))
        return Fail("handle-create", error);
    if (runtime.last_call_telemetry().enabled)
        return Fail("envelope-default-off");
    std::puts("ISOMR_AARCH64_CALL_ENVELOPE_DEFAULT=off");
    std::puts("ISOMR_AARCH64_CALL_SVC_CATEGORY_DEFAULT=off");
    isomr::GuestJni& jni = runtime.bionic()->jni();
    isomr::GuestAddressSpace* memory = &runtime.bionic()->address_space();
    if (!ValidateCompatibilityFacade(
            memory, handles.platform_proxy_class, "java/lang/Class",
            "com/king/candycrushsaga/PlatformProxy", false, &error) ||
        !ValidateCompatibilityFacade(
            memory, handles.activity,
            "com/king/candycrushsaga/CandyCrushSagaActivity", {}, true,
            &error) ||
        !ValidateCompatibilityFacade(
            memory, handles.native_application,
            "com/king/core/NativeApplication", {}, true, &error) ||
        !ValidateCompatibilityFacade(memory, handles.version_string,
                                     "java/lang/String", "1.333.2.1", false,
                                     &error) ||
        !ValidateCompatibilityFacade(memory, handles.asset_manager,
                                     "android/content/res/AssetManager", {},
                                     true, &error))
        return Fail("handle-facade", error);
    std::uint64_t rejected = 0;
    const std::string oversized_string(4097, 'x');
    const bool invalid_creation_rejected =
        !jni.CreateCompatibilityClassReference({}, &rejected, &error) &&
        rejected == 0 &&
        !jni.CreateCompatibilityStringReference(oversized_string, &rejected,
                                                &error) &&
        rejected == 0 &&
        !jni.ReleaseCompatibilityLocalReference(0, &error);
    std::array<std::uint64_t, 8> arguments{};
    arguments[0] = handles.environment;
    arguments[1] = handles.activity;
    std::uint64_t activity_global = 0;
    if (!invalid_creation_rejected ||
        !Dispatch(&runtime, "__isomr_jni_new_global_ref", arguments,
                  &activity_global, &error) ||
        activity_global == 0 || jni.global_reference_count() != 1 ||
        !ValidateCompatibilityFacade(
            memory, activity_global,
            "com/king/candycrushsaga/CandyCrushSagaActivity", {}, true,
            &error))
        return Fail("reference-global", error);
    arguments[1] = handles.activity;
    arguments[2] = activity_global;
    std::uint64_t same = 0;
    if (!Dispatch(&runtime, "__isomr_jni_is_same_object", arguments, &same,
                  &error) ||
        same != 1)
        return Fail("reference-identity", error);
    arguments = {};
    arguments[0] = handles.environment;
    arguments[1] = activity_global;
    std::uint64_t activity_class = 0;
    if (!Dispatch(&runtime, "__isomr_jni_get_object_class", arguments,
                  &activity_class, &error) ||
        activity_class == 0 || jni.class_reference_count() != 2 ||
        !ValidateCompatibilityFacade(
            memory, activity_class, "java/lang/Class",
            "com/king/candycrushsaga/CandyCrushSagaActivity", false,
            &error) ||
        !jni.ReleaseCompatibilityLocalReference(activity_class, &error))
        return Fail("reference-type", error);
    arguments[1] = activity_global;
    if (!Dispatch(&runtime, "__isomr_jni_delete_global_ref", arguments,
                  nullptr, &error) ||
        jni.global_reference_count() != 0)
        return Fail("reference-global-delete", error);

    isomr::CandyEntryOptions invalid_options;
    invalid_options.width = 0;
    invalid_options.show_window = true;
    isomr::CandyEntryResult result{};
    if (isomr::CandyEntryBridge::Run(&runtime, handles, invalid_options,
                                     &result, &error) ||
        result.completed_calls != 0 ||
        runtime.bionic()->gles().window_visible() ||
        error.find("validation") == std::string::npos)
        return Fail("invalid-options", error);
    isomr::CandyEntryOptions invalid_analytics;
    invalid_analytics.show_window = true;
    invalid_analytics.analytics_no_value_mode =
        static_cast<isomr::CandyAnalyticsNoValueMode>(0xff);
    if (isomr::CandyEntryBridge::Run(
            &runtime, handles, invalid_analytics, &result, &error) ||
        result.completed_calls != 0 ||
        runtime.bionic()->gles().window_visible() ||
        error.find("analytics no-value mode") == std::string::npos)
        return Fail("invalid-analytics-mode", error);
    isomr::CandyEntryOptions invalid_popup;
    invalid_popup.show_window = true;
    invalid_popup.popup_dialog_mode =
        static_cast<isomr::CandyPopupDialogMode>(0xff);
    if (isomr::CandyEntryBridge::Run(
            &runtime, handles, invalid_popup, &result, &error) ||
        result.completed_calls != 0 ||
        runtime.bionic()->gles().window_visible() ||
        error.find("popup dialog mode") == std::string::npos)
        return Fail("invalid-popup-mode", error);
    isomr::CandyEntryOptions invalid_popup_close;
    invalid_popup_close.show_window = true;
    invalid_popup_close.run_until_close = true;
    invalid_popup_close.request_close_after_popup_completion_for_testing =
        true;
    error.clear();
    if (isomr::CandyEntryBridge::Run(
            &runtime, handles, invalid_popup_close, &result, &error) ||
        result.completed_calls != 0 || runtime.bionic()->gles().window_visible() ||
        error.find("test popup close requires") == std::string::npos)
        return Fail("test-popup-close-system-dialog-rejected", error);
    invalid_popup_close.popup_dialog_mode =
        isomr::CandyPopupDialogMode::kAcknowledgeWithoutUiForTesting;
    invalid_popup_close.show_window = false;
    error.clear();
    if (isomr::CandyEntryBridge::Run(
            &runtime, handles, invalid_popup_close, &result, &error) ||
        result.completed_calls != 0 || runtime.bionic()->gles().window_visible() ||
        error.find("test popup close requires") == std::string::npos)
        return Fail("test-popup-close-hidden-rejected", error);
    invalid_popup_close.show_window = true;
    invalid_popup_close.run_until_close = false;
    error.clear();
    if (isomr::CandyEntryBridge::Run(
            &runtime, handles, invalid_popup_close, &result, &error) ||
        result.completed_calls != 0 || runtime.bionic()->gles().window_visible() ||
        error.find("test popup close requires") == std::string::npos)
        return Fail("test-popup-close-bounded-rejected", error);
    isomr::CandyEntryOptions invalid_steps;
    invalid_steps.step_count = 0;
    if (isomr::CandyEntryBridge::Run(&runtime, handles, invalid_steps,
                                     &result, &error) ||
        result.completed_calls != 0 ||
        error.find("display/step options") == std::string::npos)
        return Fail("invalid-steps", error);

    isomr::CandyEntryOptions options;
    options.width = 1280;
    options.height = 720;
    options.rotation = 1;
    options.density = 2.5F;
    options.fallback = true;
    options.display_scale = 0.75F;
    options.step_seconds = 0.125F;
    options.step_count = 3;
    options.show_window = true;
    options.step_boundary_telemetry = true;
    constexpr std::string_view kAnalyticsKey = "native-windows-test-key";
    constexpr std::uint64_t kAgeSignalsFirstHandle =
        0x123456789abcdef0ULL;
    constexpr std::uint64_t kAgeSignalsDebugScenario =
        std::numeric_limits<std::uint64_t>::max();
    constexpr std::uint64_t kAgeSignalsSecondHandle =
        0xfedcba9876543210ULL;
    if (!QueueAnalyticsValueRequest(&runtime, handles.environment,
                                    handles.activity, kAnalyticsKey, &error) ||
        jni.pending_analytics_value_request_count() != 1 ||
        !QueueAgeSignalsRequest(
            &runtime, handles.environment, handles.activity,
            kAgeSignalsFirstHandle, kAgeSignalsDebugScenario,
            kAgeSignalsSecondHandle, &error) ||
        jni.pending_age_signals_request_count() != 1 ||
        !PostPointerSequence(&error) ||
        !runtime.SetMainCallTelemetryMode(
            isomr::Aarch64CallTelemetryMode::kEnvelope, &error))
        return Fail("envelope-enable", error);
    std::puts("ISOMR_AARCH64_CALL_ENVELOPE config=enabled scope=main-bridge svc-timing=off memory-counters=off");
    if (!isomr::CandyEntryBridge::Run(&runtime, handles, options, &result,
                                      &error))
        return Fail("bridge", error);

    isomr::test::CandyEntryFixtureState state{};
    if (!runtime.bionic()->address_space().Read(
            kModuleBase + isomr::test::kCandyEntryFixtureState, &state,
            sizeof(state)))
        return Fail("fixture-state-read");
    const std::uint64_t native_instance = 0x1234cafeULL;
    const bool handles_pass =
        state.create_arguments ==
            std::array<std::uint64_t, 3>{handles.environment,
                                         handles.platform_proxy_class,
                                         handles.activity} &&
        state.application_arguments ==
            std::array<std::uint64_t, 5>{handles.environment,
                                         handles.native_application,
                                         native_instance,
                                         handles.version_string,
                                         handles.asset_manager};
    const bool create_pass = result.native_instance == native_instance &&
                             state.native_instance == native_instance &&
                             state.create_index == 0 &&
                             state.application_index == 1;
    const bool lifecycle_pass = state.event_count == 2 &&
                                state.event_index_code == 35 &&
                                state.event_action_code == 33 &&
                                state.event_environment == handles.environment &&
                                state.event_object == handles.native_application;
    const bool display_pass =
        state.initialize_index == 4 && state.scale_index == 5 &&
        state.screen_index == 6 &&
        state.initialize_arguments ==
            std::array<std::uint64_t, 7>{
                handles.environment, handles.native_application, 1280, 720,
                1, std::bit_cast<std::uint32_t>(options.density), 1} &&
        state.scale_arguments ==
            std::array<std::uint64_t, 3>{
                handles.environment, handles.native_application,
                std::bit_cast<std::uint32_t>(options.display_scale)} &&
        state.screen_arguments ==
            std::array<std::uint64_t, 4>{handles.environment,
                                         handles.native_application, 1280,
                                         720};
    const bool step_pass = result.step_keep_running &&
                           result.termination ==
                               isomr::CandyEntryTermination::kStepLimit &&
                           result.completed_stage ==
                               isomr::CandyEntryStage::kStep &&
                            result.completed_calls == 15 &&
                            state.step_index == 14 &&
                           state.step_arguments ==
                               std::array<std::uint64_t, 3>{
                                   handles.environment,
                                   handles.native_application,
                                   std::bit_cast<std::uint32_t>(
                                       options.step_seconds)};
    const bool input_pass = result.owner_message_pump_count > 0 &&
        result.pointer_event_count == 3 &&
        state.touch_index == 9 && state.call_count == 15 &&
        state.touch_arguments ==
            std::array<std::uint64_t, 6>{handles.environment,
                                         handles.native_application, 0, 1,
                                         1279, 719};
    const bool analytics_pass =
        result.analytics_completion_count == 1 &&
        jni.pending_analytics_value_request_count() == 0 &&
        state.analytics_index == 11 &&
        state.analytics_arguments[0] == handles.environment &&
        state.analytics_arguments[1] != 0 &&
        state.analytics_arguments[2] != 0 &&
        state.analytics_arguments[3] == 0;
    const bool age_signals_pass =
        result.age_signals_completion_count == 1 &&
        jni.pending_age_signals_request_count() == 0 &&
        state.age_signals_index == 12 &&
        state.age_signals_arguments[0] == handles.environment &&
        state.age_signals_arguments[1] != 0 &&
        state.age_signals_arguments[2] == kAgeSignalsFirstHandle &&
        state.age_signals_arguments[3] == kAgeSignalsSecondHandle &&
        state.age_signals_arguments[4] ==
            static_cast<std::uint32_t>(-100) &&
        state.age_signals_arguments[5] != 0;
    if (!handles_pass) return Fail("fixture-handles-evidence");
    if (!create_pass) return Fail("fixture-create-evidence");
    if (!lifecycle_pass) return Fail("fixture-lifecycle-evidence");
    if (!display_pass) return Fail("fixture-display-evidence");
    if (!step_pass) return Fail("fixture-step-evidence");
    if (!input_pass) return Fail("fixture-input-evidence");
    if (!analytics_pass) return Fail("fixture-analytics-evidence");
    if (!age_signals_pass) return Fail("fixture-age-signals-evidence");

    if (!ReleaseHandles(&runtime, handles, &error) ||
        jni.class_reference_count() != 0 ||
        jni.object_reference_count() != 0 ||
        jni.global_reference_count() != 0 ||
        !runtime.Shutdown(&error) ||
        runtime.bionic()->heap().allocation_count() != 0) {
        runtime.bionic()->heap().DumpLiveAllocations("candy");
        return Fail("cleanup", error);
    }

    isomr::Aarch64ProfileRuntime close_runtime(
        Config(kCloseLifecycleModuleBase));
    isomr::CandyEntryHandles close_handles{};
    if (!close_runtime.Load(fixture.path(), &error) ||
        !close_runtime.RunInitializers(&error) ||
        !CreateHandles(&close_runtime, &close_handles, &error) ||
        !close_runtime.bionic()->gles().Show(&error) ||
        !PostClose(&error))
        return Fail("close-lifecycle-initialize", error);
    isomr::CandyEntryOptions close_options = options;
    close_options.run_until_close = true;
    close_options.step_boundary_telemetry = false;
    isomr::CandyEntryResult close_result{};
    if (!isomr::CandyEntryBridge::Run(
            &close_runtime, close_handles, close_options, &close_result,
            &error))
        return Fail("close-lifecycle-bridge", error);
    isomr::test::CandyEntryFixtureState close_state{};
    if (!close_runtime.bionic()->address_space().Read(
            kCloseLifecycleModuleBase +
                isomr::test::kCandyEntryFixtureState,
            &close_state, sizeof(close_state)) ||
        close_result.termination !=
            isomr::CandyEntryTermination::kWindowClosed ||
        close_result.completed_stage != isomr::CandyEntryStage::kDestroy ||
        close_result.completed_calls != 12 || close_state.call_count != 12 ||
        close_state.event_count != 5 ||
        close_state.event_index_code != 145289 ||
        close_state.event_action_code != 17053 ||
        close_state.shutting_down_index != 10 ||
        close_state.shutting_down_arguments !=
            std::array<std::uint64_t, 2>{close_handles.environment,
                                         close_handles.native_application} ||
        close_state.destroy_index != 11 ||
        close_state.destroy_arguments !=
            std::array<std::uint64_t, 2>{close_handles.environment,
                                         close_handles.native_application})
        return Fail("close-lifecycle-evidence", error);
    if (!ReleaseHandles(&close_runtime, close_handles, &error) ||
        !close_runtime.Shutdown(&error) ||
        close_runtime.bionic()->heap().allocation_count() != 0)
        return Fail("close-lifecycle-cleanup", error);

    isomr::Aarch64ProfileRuntime empty_runtime(
        Config(kEmptyAnalyticsModuleBase));
    isomr::CandyEntryHandles empty_handles{};
    if (!empty_runtime.Load(fixture.path(), &error) ||
        !empty_runtime.RunInitializers(&error) ||
        !CreateHandles(&empty_runtime, &empty_handles, &error) ||
        !QueueAnalyticsValueRequest(
            &empty_runtime, empty_handles.environment,
            empty_handles.activity, kAnalyticsKey, &error))
        return Fail("empty-analytics-initialize", error);
    isomr::CandyEntryOptions empty_options = options;
    empty_options.step_count = 1;
    empty_options.analytics_no_value_mode =
        isomr::CandyAnalyticsNoValueMode::kEmptyString;
    isomr::CandyEntryResult empty_result{};
    if (!isomr::CandyEntryBridge::Run(
            &empty_runtime, empty_handles, empty_options, &empty_result,
            &error))
        return Fail("empty-analytics-bridge", error);
    isomr::test::CandyEntryFixtureState empty_state{};
    if (!empty_runtime.bionic()->address_space().Read(
            kEmptyAnalyticsModuleBase +
                isomr::test::kCandyEntryFixtureState,
            &empty_state, sizeof(empty_state)) ||
        empty_result.completed_calls != 9 ||
        empty_result.analytics_completion_count != 1 ||
        empty_result.completed_stage !=
            isomr::CandyEntryStage::kAnalyticsCompletion ||
        empty_runtime.bionic()
                ->jni()
                .pending_analytics_value_request_count() != 0 ||
        empty_state.analytics_index != 8 ||
        empty_state.analytics_arguments[0] != empty_handles.environment ||
        empty_state.analytics_arguments[1] == 0 ||
        empty_state.analytics_arguments[2] == 0 ||
        empty_state.analytics_arguments[3] == 0)
        return Fail("empty-analytics-evidence");
    if (!ReleaseHandles(&empty_runtime, empty_handles, &error) ||
        empty_runtime.bionic()->jni().class_reference_count() != 0 ||
        empty_runtime.bionic()->jni().object_reference_count() != 0 ||
        !empty_runtime.Shutdown(&error) ||
        empty_runtime.bionic()->heap().allocation_count() != 0)
        return Fail("empty-analytics-cleanup", error);

    isomr::Aarch64ProfileRuntime worker_failure_runtime(
        Config(kWorkerFailureModuleBase));
    isomr::CandyEntryHandles worker_failure_handles{};
    if (!worker_failure_runtime.Load(fixture.path(), &error) ||
        !worker_failure_runtime.RunInitializers(&error) ||
        !CreateHandles(&worker_failure_runtime, &worker_failure_handles,
                       &error))
        return Fail("worker-failure-initialize", error);
    const std::uint64_t worker_handle_output =
        worker_failure_runtime.bionic()->heap().Callocate(1, 16);
    if (worker_handle_output == 0 ||
        worker_failure_runtime.bionic()->threads().Create(
            worker_handle_output, 0, 1, 0) != 0)
        return Fail("worker-failure-create", error);
    std::uint64_t failed_worker_handle = 0;
    if (!worker_failure_runtime.bionic()->heap().Read(
            worker_handle_output, &failed_worker_handle,
            sizeof(failed_worker_handle)) ||
        failed_worker_handle == 0 ||
        worker_failure_runtime.bionic()->threads().Detach(
            failed_worker_handle) != 0)
        return Fail("worker-failure-detach", error);
    const ULONGLONG worker_failure_deadline = GetTickCount64() + 2000;
    while (worker_failure_runtime.bionic()->threads().active_count() != 0 &&
           GetTickCount64() < worker_failure_deadline)
        SwitchToThread();
    isomr::CandyEntryOptions worker_failure_options = options;
    worker_failure_options.show_window = false;
    worker_failure_options.step_count = 1;
    isomr::CandyEntryResult worker_failure_result{};
    error.clear();
    if (worker_failure_runtime.bionic()->threads().active_count() != 0 ||
        isomr::CandyEntryBridge::Run(
            &worker_failure_runtime, worker_failure_handles,
            worker_failure_options, &worker_failure_result, &error) ||
        worker_failure_result.completed_calls != 0 ||
        error.find("worker failure before guest call") == std::string::npos ||
        error.find("start=0x1") == std::string::npos ||
        !worker_failure_runtime.bionic()->heap().Release(
            worker_handle_output, &error) ||
        !ReleaseHandles(&worker_failure_runtime, worker_failure_handles,
                        &error) ||
        !worker_failure_runtime.Shutdown(&error) ||
        worker_failure_runtime.bionic()->heap().allocation_count() != 0)
        return Fail("worker-failure-surfaced", error);

    TemporaryFixture in_flight_fixture;
    if (!in_flight_fixture.Write(
            isomr::test::MakeCandyEntryFixture(false, 256)))
        return Fail("in-flight-worker-failure-fixture-write");
    isomr::Aarch64ProfileRuntimeConfig in_flight_config =
        Config(kInFlightWorkerFailureModuleBase);
    in_flight_config.instruction_budget = 100000000;
    isomr::Aarch64ProfileRuntime in_flight_runtime(in_flight_config);
    isomr::CandyEntryHandles in_flight_handles{};
    if (!in_flight_runtime.Load(in_flight_fixture.path(), &error) ||
        !in_flight_runtime.RunInitializers(&error) ||
        !CreateHandles(&in_flight_runtime, &in_flight_handles, &error))
        return Fail("in-flight-worker-failure-initialize", error);
    const std::uint64_t in_flight_handle_output =
        in_flight_runtime.bionic()->heap().Callocate(1, 16);
    if (in_flight_handle_output == 0 ||
        in_flight_runtime.bionic()->threads().Create(
            in_flight_handle_output, 0,
            kInFlightWorkerFailureModuleBase +
                isomr::test::kCandyEntryFixtureWorkerFailure,
            kInFlightWorkerFailureModuleBase +
                isomr::test::kCandyEntryFixtureState) != 0)
        return Fail("in-flight-worker-failure-create", error);
    std::uint64_t in_flight_worker_handle = 0;
    if (!in_flight_runtime.bionic()->heap().Read(
            in_flight_handle_output, &in_flight_worker_handle,
            sizeof(in_flight_worker_handle)) ||
        in_flight_worker_handle == 0 ||
        in_flight_runtime.bionic()->threads().Detach(
            in_flight_worker_handle) != 0)
        return Fail("in-flight-worker-failure-detach", error);
    isomr::test::CandyEntryFixtureState in_flight_state{};
    const ULONGLONG in_flight_ready_deadline = GetTickCount64() + 2000;
    while (in_flight_state.worker_ready == 0 &&
           GetTickCount64() < in_flight_ready_deadline) {
        if (!in_flight_runtime.bionic()->address_space().Read(
                kInFlightWorkerFailureModuleBase +
                    isomr::test::kCandyEntryFixtureState,
                &in_flight_state, sizeof(in_flight_state)))
            return Fail("in-flight-worker-failure-ready-read");
        SwitchToThread();
    }
    if (in_flight_state.worker_ready != 1)
        return Fail("in-flight-worker-failure-ready-timeout");
    isomr::CandyEntryOptions in_flight_options = options;
    in_flight_options.step_count = 1;
    isomr::CandyEntryResult in_flight_result{};
    error.clear();
    const ULONGLONG in_flight_started = GetTickCount64();
    const bool in_flight_call = isomr::CandyEntryBridge::Run(
        &in_flight_runtime, in_flight_handles, in_flight_options,
        &in_flight_result, &error);
    const ULONGLONG in_flight_elapsed =
        GetTickCount64() - in_flight_started;
    const std::string in_flight_error = error;
    if (in_flight_call || in_flight_elapsed > 2000 ||
        in_flight_result.bridge_call_count != 1 ||
        in_flight_result.completed_calls != 0 ||
        in_flight_result.owner_message_pump_count == 0 ||
        error.find("guest inter-slice callback failed") ==
            std::string::npos ||
        error.find("CandyEntryBridge in-flight worker failure") ==
            std::string::npos ||
        error.find("failed:") == std::string::npos) {
        error = in_flight_error + " elapsed=" +
                std::to_string(in_flight_elapsed) + " bridge-calls=" +
                std::to_string(in_flight_result.bridge_call_count) +
                " completed=" +
                std::to_string(in_flight_result.completed_calls) +
                " pumps=" +
                std::to_string(in_flight_result.owner_message_pump_count);
        return Fail("in-flight-worker-failure-evidence", error);
    }
    if (!in_flight_runtime.bionic()->heap().Release(
            in_flight_handle_output, &error) ||
        !ReleaseHandles(&in_flight_runtime, in_flight_handles, &error) ||
        !in_flight_runtime.Shutdown(&error) ||
        in_flight_runtime.bionic()->heap().allocation_count() != 0)
        return Fail("in-flight-worker-failure-cleanup", error);

    isomr::Aarch64ProfileRuntime pump_failure_runtime(
        Config(kPumpFailureModuleBase));
    isomr::CandyEntryHandles pump_failure_handles{};
    if (!pump_failure_runtime.Load(fixture.path(), &error) ||
        !pump_failure_runtime.RunInitializers(&error) ||
        !CreateHandles(&pump_failure_runtime, &pump_failure_handles,
                       &error))
        return Fail("pump-failure-initialize", error);
    for (std::size_t sequence = 0; sequence < 86; ++sequence) {
        if (!PostPointerSequence(&error))
            return Fail("pump-failure-post", error);
    }
    isomr::CandyEntryOptions pump_failure_options = options;
    pump_failure_options.step_count = 1;
    isomr::CandyEntryResult pump_failure_result{};
    error.clear();
    const bool pump_call =
        isomr::CandyEntryBridge::Run(
            &pump_failure_runtime, pump_failure_handles,
            pump_failure_options, &pump_failure_result, &error);
    isomr::test::CandyEntryFixtureState pump_failure_state{};
    const bool pump_state_read =
        pump_failure_runtime.bionic()->address_space().Read(
            kPumpFailureModuleBase +
                isomr::test::kCandyEntryFixtureState,
            &pump_failure_state, sizeof(pump_failure_state));
    const bool pump_failure_rejected =
        !pump_call && pump_failure_result.completed_calls == 0 &&
        pump_failure_result.pointer_event_count == 0 &&
        pump_state_read && pump_failure_state.call_count == 1 &&
        pump_failure_state.touch_arguments ==
            std::array<std::uint64_t, 6>{} &&
        error.find("CandyEntryBridge create-native-instance/") !=
            std::string::npos &&
        error.find("guest inter-slice callback failed") != std::string::npos &&
        error.find("pointer event queue overflowed") != std::string::npos;
    if (!pump_failure_rejected ||
        !ReleaseHandles(&pump_failure_runtime, pump_failure_handles,
                        &error) ||
        !pump_failure_runtime.Shutdown(&error) ||
        pump_failure_runtime.bionic()->heap().allocation_count() != 0)
        return Fail("pump-failure-no-partial-input", error);

    std::vector<std::uint8_t> batch_failure_fixture =
        isomr::test::MakeCandyEntryFixture();
    if (!MakeSecondAnalyticsCallbackFail(&batch_failure_fixture) ||
        !fixture.Write(batch_failure_fixture))
        return Fail("batch-failure-fixture-write");
    isomr::Aarch64ProfileRuntimeConfig batch_failure_config =
        Config(kBatchFailureModuleBase);
    batch_failure_config.nanosleep_callsite_telemetry_enabled = true;
    isomr::Aarch64ProfileRuntime batch_failure_runtime(
        std::move(batch_failure_config));
    isomr::CandyEntryHandles batch_failure_handles{};
    constexpr std::string_view kFirstBatchAnalyticsKey =
        "failure-atomic-first";
    constexpr std::string_view kSecondBatchAnalyticsKey =
        "failure-atomic-second";
    if (!batch_failure_runtime.Load(fixture.path(), &error) ||
        !batch_failure_runtime.RunInitializers(&error) ||
        !CreateHandles(&batch_failure_runtime, &batch_failure_handles,
                       &error) ||
        !batch_failure_runtime.SetMainCallTelemetryMode(
            isomr::Aarch64CallTelemetryMode::kSvcCategory, &error) ||
        !QueueAnalyticsValueRequest(
            &batch_failure_runtime, batch_failure_handles.environment,
            batch_failure_handles.activity, kFirstBatchAnalyticsKey,
            &error) ||
        !QueueAnalyticsValueRequest(
            &batch_failure_runtime, batch_failure_handles.environment,
            batch_failure_handles.activity, kSecondBatchAnalyticsKey,
            &error) ||
        batch_failure_runtime.bionic()
                ->jni()
                .pending_analytics_value_request_count() != 2)
        return Fail("batch-failure-initialize", error);
    std::puts("ISOMR_AARCH64_CALL_SVC_CATEGORY config=enabled scope=main-bridge address-free=1 per-svc-lines=off memory-counters=off import-journal=unchanged");
    isomr::CandyEntryOptions batch_failure_options = options;
    batch_failure_options.show_window = false;
    batch_failure_options.step_count = 1;
    isomr::CandyEntryResult batch_failure_result{};
    error.clear();
    const bool batch_failure_call =
        isomr::CandyEntryBridge::Run(
            &batch_failure_runtime, batch_failure_handles,
            batch_failure_options, &batch_failure_result, &error);
    isomr::test::CandyEntryFixtureState batch_failure_state{};
    const bool batch_failure_state_read =
        batch_failure_runtime.bionic()->address_space().Read(
            kBatchFailureModuleBase +
                isomr::test::kCandyEntryFixtureState,
            &batch_failure_state, sizeof(batch_failure_state));
    const std::optional<isomr::GuestJni::AnalyticsValueRequest>
        retained_request =
            batch_failure_runtime.bionic()
                ->jni()
                .NextPendingAnalyticsValueRequest();
    const bool failed_suffix_retained =
        !batch_failure_call &&
        batch_failure_result.completed_calls == 9 &&
        batch_failure_result.analytics_completion_count == 1 &&
        batch_failure_result.completed_stage ==
            isomr::CandyEntryStage::kAnalyticsCompletion &&
        batch_failure_runtime.bionic()
                ->jni()
                .pending_analytics_value_request_count() == 1 &&
        retained_request && retained_request->sequence == 2 &&
        retained_request->key == kSecondBatchAnalyticsKey &&
        batch_failure_state_read &&
        batch_failure_state.call_count == 9 &&
        batch_failure_state.analytics_index == 8 &&
        error.find("analytics-response-2") != std::string::npos;
    const isomr::NanosleepCallsiteTelemetryToken post_failure_token =
        batch_failure_runtime.BeginNanosleepCallsiteTelemetry();
    const isomr::NanosleepCallsiteTelemetrySnapshot post_failure_snapshot =
        batch_failure_runtime.EndNanosleepCallsiteTelemetry(
            post_failure_token);
    if (!post_failure_token.active || !post_failure_snapshot.enabled ||
        !post_failure_snapshot.complete ||
        post_failure_snapshot.aggregate.success_count != 0)
        return Fail("callsite-failed-call-isolation", error);
    isomr::CandyEntryResult batch_retry_result{};
    error.clear();
    const bool batch_retry_call =
        isomr::CandyEntryBridge::Run(
            &batch_failure_runtime, batch_failure_handles,
            batch_failure_options, &batch_retry_result, &error);
    isomr::test::CandyEntryFixtureState batch_retry_state{};
    const bool batch_retry_state_read =
        batch_failure_runtime.bionic()->address_space().Read(
            kBatchFailureModuleBase +
                isomr::test::kCandyEntryFixtureState,
            &batch_retry_state, sizeof(batch_retry_state));
    const bool first_not_replayed =
        !batch_retry_call &&
        batch_retry_result.completed_calls == 8 &&
        batch_retry_result.analytics_completion_count == 0 &&
        batch_failure_runtime.bionic()
                ->jni()
                .pending_analytics_value_request_count() == 1 &&
        batch_retry_state_read &&
        batch_retry_state.call_count == 17 &&
        batch_retry_state.analytics_index == 8 &&
        error.find("analytics-response-2") != std::string::npos;
    if (!failed_suffix_retained || !first_not_replayed ||
        !ReleaseHandles(&batch_failure_runtime, batch_failure_handles,
                        &error) ||
        !batch_failure_runtime.Shutdown(&error) ||
        batch_failure_runtime.bionic()->heap().allocation_count() != 0)
        return Fail("batch-failure-atomic", error);

    if (!fixture.Write(isomr::test::MakeCandyEntryFixture(true)))
        return Fail("missing-fixture-write");
    isomr::Aarch64ProfileRuntime missing_runtime(Config(kMissingModuleBase));
    isomr::CandyEntryHandles missing_handles{};
    if (!missing_runtime.Load(fixture.path(), &error) ||
        !missing_runtime.RunInitializers(&error) ||
        !CreateHandles(&missing_runtime, &missing_handles, &error) ||
        !PostPointerSequence(&error))
        return Fail("missing-runtime-initialize", error);
    isomr::CandyEntryResult missing_result{};
    const bool missing_rejected =
        !isomr::CandyEntryBridge::Run(&missing_runtime, missing_handles,
                                      options, &missing_result, &error) &&
        missing_result.completed_calls == 7 &&
        missing_result.pointer_event_count == 0 &&
        error.find("pointer-down/Java_com_king_core_NativeApplication_onTouchEvent") !=
            std::string::npos &&
        error.find("unavailable") != std::string::npos;
    if (!missing_rejected ||
        !ReleaseHandles(&missing_runtime, missing_handles, &error) ||
        !missing_runtime.Shutdown(&error) ||
        missing_runtime.bionic()->heap().allocation_count() != 0)
        return Fail("missing-export", error);

    std::puts("ISOMR_CANDY_ENTRY_FIXTURE handles=pass create=pass lifecycle=pass close-lifecycle=pause-stop-terminate-flag-destroy display=pass step=pass analytics=no-value-null-and-empty-owner-thread-after-step-failure-atomic age-signals=offline-error--100-owner-thread-after-step input=inter-slice-dispatched-post-call-win32-down-move-up-scaled-clamped-pointer0 worker-failure=first-latched-before-call-and-in-flight-slice-abort-actual-error cleanup=pass");
    std::puts("ISOMR_NATIVE_CANDY_ENTRY=pass");
    return 0;
}
