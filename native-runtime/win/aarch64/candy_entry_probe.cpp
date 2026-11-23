#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <GLES2/gl2.h>

#include "aarch64_profile_runtime.h"
#include "bionic_runtime.h"
#include "candy_entry_bridge.h"
#include "candy_webp_host.h"
#include "candy_vorbis_host.h"

#include <array>
#include <chrono>
#include <cerrno>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace {

const std::unordered_set<std::string> kAndroidHostDependencies{
    "libandroid.so", "libc.so",         "libdl.so",
    "libEGL.so",     "libGLESv2.so",   "liblog.so",
    "libm.so",       "libmediandk.so", "libOpenSLES.so",
    "libz.so"};

const char* HttpLifecycleKindName(
    isomr::GuestJni::HttpLifecycleEventKind kind) {
    switch (kind) {
        case isomr::GuestJni::HttpLifecycleEventKind::kCreateTask:
            return "create";
        case isomr::GuestJni::HttpLifecycleEventKind::kExecuteTask:
            return "execute";
        case isomr::GuestJni::HttpLifecycleEventKind::kCancelTask:
            return "cancel";
    }
    return "unknown";
}

const char* HttpObservationKindName(
    isomr::GuestJni::HttpObservationEventKind kind) {
    switch (kind) {
        case isomr::GuestJni::HttpObservationEventKind::kMethodLookup:
            return "lookup";
        case isomr::GuestJni::HttpObservationEventKind::kConstructor:
            return "constructor";
        case isomr::GuestJni::HttpObservationEventKind::kObjectCall:
            return "object-call";
        case isomr::GuestJni::HttpObservationEventKind::kVoidCall:
            return "void-call";
    }
    return "unknown";
}

void ReportFailure(const char* stage, const std::string& error) {
    std::fprintf(stderr,
                 "ISOMR_CANDY_ENTRY_PROBE stage=%s result=fail error=%s\n",
                 stage, error.c_str());
}

bool ParseUint64Argument(const wchar_t* text, std::uint64_t* value) {
    if (text == nullptr || text[0] == L'\0' || value == nullptr) return false;
    wchar_t* end = nullptr;
    errno = 0;
    const unsigned long long parsed = std::wcstoull(text, &end, 10);
    if (errno != 0 || end == text || end == nullptr || *end != L'\0')
        return false;
    *value = static_cast<std::uint64_t>(parsed);
    return true;
}

bool ParseSizeArgument(const wchar_t* text, std::size_t* value) {
    std::uint64_t parsed = 0;
    if (value == nullptr || !ParseUint64Argument(text, &parsed) ||
        parsed > (std::numeric_limits<std::size_t>::max)())
        return false;
    *value = static_cast<std::size_t>(parsed);
    return true;
}

bool ParseStrictNonzeroUint64Argument(std::wstring_view text,
                                      std::uint64_t* value) {
    if (text.empty() || value == nullptr) return false;
    for (const wchar_t character : text) {
        if (character < L'0' || character > L'9') return false;
    }
    const std::wstring owned(text);
    std::uint64_t parsed = 0;
    if (!ParseUint64Argument(owned.c_str(), &parsed) || parsed == 0)
        return false;
    *value = parsed;
    return true;
}

bool ParseCaptureArgument(std::wstring_view text, std::uint64_t* generation,
                          std::uint64_t* occurrence) {
    if (generation == nullptr || occurrence == nullptr || text.empty())
        return false;
    const std::size_t separator = text.find(L':');
    const std::wstring generation_text(text.substr(0, separator));
    const std::wstring occurrence_text =
        separator == std::wstring_view::npos
            ? std::wstring(L"1")
            : std::wstring(text.substr(separator + 1));
    return ParseUint64Argument(generation_text.c_str(), generation) &&
           ParseUint64Argument(occurrence_text.c_str(), occurrence) &&
           *generation != 0 && *occurrence != 0;
}

bool ParseAddressArgument(std::wstring_view text, std::uint64_t* value) {
    if (text.empty() || value == nullptr) return false;
    const std::wstring owned(text);
    wchar_t* end = nullptr;
    errno = 0;
    const unsigned long long parsed = std::wcstoull(owned.c_str(), &end, 0);
    if (errno != 0 || end == owned.c_str() || end == nullptr ||
        *end != L'\0' || parsed == 0)
        return false;
    *value = static_cast<std::uint64_t>(parsed);
    return true;
}

std::string FormatWorkerCpuWatchdogLine(
    std::uint32_t elapsed,
    const isomr::BionicThreads::WorkerCpuSnapshot& snapshot,
    std::unordered_map<std::uint64_t, std::uint64_t>* previous_cpu) {
    std::ostringstream line;
    line << "ISOMR_CANDY_WORKER_CPU elapsed=" << elapsed
         << " total=" << snapshot.total
         << " sampled=" << snapshot.sampled
         << " dropped=" << snapshot.dropped << " records=";
    if (snapshot.records.empty()) line << "none";
    bool first = true;
    for (const auto& record : snapshot.records) {
        if (!first) line << ';';
        first = false;
        line << "ordinal=" << record.ordinal
             << ",start-group=" << record.start_group << ",lifecycle="
             << (record.lifecycle ==
                         isomr::BionicThreads::WorkerLifecycleState::kActive
                     ? "active"
                     : "completed")
             << ",cpu=";
        if (!record.cpu_available || previous_cpu == nullptr) {
            line << "unavailable,cumulative-100ns=unavailable,"
                    "delta-100ns=unavailable";
            continue;
        }
        const auto previous = previous_cpu->find(record.ordinal);
        if (previous != previous_cpu->end() &&
            record.cpu_100ns < previous->second) {
            line << "unavailable,cumulative-100ns=" << record.cpu_100ns
                 << ",delta-100ns=unavailable";
            continue;
        }
        const std::uint64_t delta =
            previous == previous_cpu->end()
                ? record.cpu_100ns
                : record.cpu_100ns - previous->second;
        (*previous_cpu)[record.ordinal] = record.cpu_100ns;
        line << "available,cumulative-100ns=" << record.cpu_100ns
             << ",delta-100ns=" << delta;
    }
    return line.str();
}

void PrintWorkerCpuSamplingMarker(bool enabled) {
    std::printf(
        "ISOMR_WORKER_CPU_SAMPLING config=%s cadence=watchdog-5s "
        "limit=32 start-group=first-ordinal-equality address-free=1\n",
        enabled ? "enabled" : "disabled");
}

void PrintWorkerCpuFormatSelfTest() {
    isomr::BionicThreads::WorkerCpuSnapshot snapshot;
    snapshot.total = 2;
    snapshot.sampled = 2;
    snapshot.records = {
        {7, 3, isomr::BionicThreads::WorkerLifecycleState::kActive, true,
         250},
        {9, 9, isomr::BionicThreads::WorkerLifecycleState::kCompleted, false,
         0}};
    std::unordered_map<std::uint64_t, std::uint64_t> previous{{7, 100}};
    std::puts(FormatWorkerCpuWatchdogLine(5, snapshot, &previous).c_str());
}

const char* WorkerExecutionPhaseName(isomr::WorkerExecutionPhase phase) {
    switch (phase) {
        case isomr::WorkerExecutionPhase::kCallEntry: return "call-entry";
        case isomr::WorkerExecutionPhase::kBeforeRun: return "before-run";
        case isomr::WorkerExecutionPhase::kAfterRun: return "after-run";
        case isomr::WorkerExecutionPhase::kInterSlice: return "inter-slice";
        case isomr::WorkerExecutionPhase::kCallSuccess: return "call-success";
        case isomr::WorkerExecutionPhase::kCallFailure: return "call-failure";
        case isomr::WorkerExecutionPhase::kDestroyed: return "destroyed";
        case isomr::WorkerExecutionPhase::kUnavailable: return "unavailable";
    }
    return "unavailable";
}

const char* WorkerHostResidencyName(
    isomr::BionicThreads::WorkerHostResidency residency) {
    using Residency = isomr::BionicThreads::WorkerHostResidency;
    switch (residency) {
        case Residency::kGeneratedPrivateExecutable:
            return "generated-private-executable";
        case Residency::kArmrMainImage: return "armr-main-image";
        case Residency::kOtherImage: return "other-image";
        case Residency::kNonExecutable: return "non-executable";
        case Residency::kSampleFailed: return "sample-failed";
        case Residency::kUnavailable: return "unavailable";
    }
    return "unavailable";
}

void PrintCandyVorbisHostMarker(bool enabled) {
    std::printf(
        "ISOMR_CANDY_VORBIS_HOST config=%s default=off "
        "profile=candy-crush-1.333.2.1 substitutions=10 "
        "source=pinned-memory-or-aasset-provider-bounded-copy decoder=host-libvorbisfile "
        "audio=real-pcm addresses=none\n",
        enabled ? "enabled" : "disabled");
}

void PrintCandyWebP103HostMarker(bool enabled) {
    std::printf(
        "ISOMR_CANDY_WEBP_103_HOST config=%s default=off "
        "profile=candy-crush-1.333.2.1 substitutions=1 "
        "decoder=libwebp-1.0.3 exact-version=0x010003 addresses=none\n",
        enabled ? "enabled" : "disabled");
}

void PrintCandyBulkZeroStreamMarker(bool enabled) {
    std::printf(
        "ISOMR_CANDY_BULK_ZERO_STREAM config=%s default=off profile=candy-crush-1.333.2.1 signatures=exact-loop-and-ostream-write mode=owner-thread-paused\n",
        enabled ? "enabled" : "disabled");
}

void PrintCandyRapidJsonHostMirrorMarker(bool enabled) {
    std::printf(
        "ISOMR_CANDY_RAPIDJSON_HOST_MIRROR config=%s default=off profile=candy-crush-1.333.2.1 parser-value-version=1.1.0 parser-value-revision=f54b0e47a08782a6131cc3d60f94d038fa6e0a51 allocator-layout=legacy-five-field scope=caller-0x4fe403c-minimum-4096 mode=owner-thread-paused addresses=none\n",
        enabled ? "enabled" : "disabled");
}

void PrintCandyVorbisHostSnapshot(
    std::uint32_t elapsed,
    const std::shared_ptr<isomr::CandyVorbisHostMirror>& mirror,
    const char* phase) {
    if (!mirror) return;
    const isomr::CandyVorbisHostSnapshot snapshot = mirror->snapshot();
    std::printf(
        "ISOMR_CANDY_VORBIS_HOST_RESULT phase=%s elapsed=%u "
        "test-callbacks=%llu test-open=%llu streams=%llu "
        "time-total=%llu pcm-total=%llu pcm-seek=%llu pcm-tell=%llu "
        "info=%llu read=%llu clear=%llu read-bytes=%llu "
        "refusals=%llu active=%zu addresses=none\n",
        phase, elapsed,
        static_cast<unsigned long long>(snapshot.test_callbacks),
        static_cast<unsigned long long>(snapshot.test_open),
        static_cast<unsigned long long>(snapshot.streams),
        static_cast<unsigned long long>(snapshot.time_total),
        static_cast<unsigned long long>(snapshot.pcm_total),
        static_cast<unsigned long long>(snapshot.pcm_seek),
        static_cast<unsigned long long>(snapshot.pcm_tell),
        static_cast<unsigned long long>(snapshot.info),
        static_cast<unsigned long long>(snapshot.read),
        static_cast<unsigned long long>(snapshot.clear),
        static_cast<unsigned long long>(snapshot.read_bytes),
        static_cast<unsigned long long>(snapshot.refusals), snapshot.active);
}

void PrintCandyWebP103HostSnapshot(
    std::uint32_t elapsed,
    const std::shared_ptr<isomr::CandyWebP103Host>& host,
    const char* phase) {
    if (!host) return;
    const isomr::CandyWebP103HostSnapshot snapshot = host->snapshot();
    std::printf(
        "ISOMR_CANDY_WEBP_103_HOST_RESULT phase=%s elapsed=%u "
        "attempts=%llu successes=%llu decoder-failures=%llu "
        "refusals=%llu input-bytes=%llu output-bytes=%llu "
        "addresses=none\n",
        phase, elapsed,
        static_cast<unsigned long long>(snapshot.attempts),
        static_cast<unsigned long long>(snapshot.successes),
        static_cast<unsigned long long>(snapshot.decoder_failures),
        static_cast<unsigned long long>(snapshot.refusals),
        static_cast<unsigned long long>(snapshot.input_bytes),
        static_cast<unsigned long long>(snapshot.output_bytes));
}

std::string FormatHexOffset(std::uint64_t offset) {
    char value[19]{};
    const int length = std::snprintf(
        value, sizeof(value), "0x%llx",
        static_cast<unsigned long long>(offset));
    return length > 0 && static_cast<std::size_t>(length) < sizeof(value)
               ? std::string(value, static_cast<std::size_t>(length))
               : std::string("-");
}

std::string SanitizeModuleToken(std::string_view value) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string result;
    for (const unsigned char byte : value) {
        const bool ascii_alphanumeric =
            (byte >= 'a' && byte <= 'z') ||
            (byte >= 'A' && byte <= 'Z') ||
            (byte >= '0' && byte <= '9');
        if (ascii_alphanumeric || byte == '_' || byte == '.' || byte == '-') {
            result.push_back(static_cast<char>(byte));
        } else {
            result.push_back('%');
            result.push_back(kHex[byte >> 4]);
            result.push_back(kHex[byte & 15U]);
        }
    }
    return result.empty() ? std::string("-") : result;
}

using WorkerExecutionModuleResolver = std::function<std::optional<
    isomr::Aarch64ProfileRuntime::ResolvedGuestCodeAddress>(
    const isomr::BionicThreads::WorkerExecutionRecord&)>;

std::string FormatWorkerExecutionModuleRegionsLine(
    std::uint32_t elapsed,
    const isomr::BionicThreads::WorkerExecutionSnapshot& snapshot,
    const WorkerExecutionModuleResolver& resolver) {
    constexpr std::uint64_t kRegionSize = 4ULL * 1024ULL;
    std::ostringstream line;
    line << "ISOMR_CANDY_WORKER_EXECUTION_MODULE_REGIONS elapsed=" << elapsed
         << " region-size=" << kRegionSize << " records=";
    if (snapshot.records.empty()) line << "none";
    bool first = true;
    for (const auto& record : snapshot.records) {
        if (!first) line << ';';
        first = false;
        line << "ordinal=" << record.ordinal << ",pc-group=";
        if (record.pc_group_state ==
            isomr::BionicThreads::WorkerPcGroupState::kAvailable) {
            line << record.pc_group << ",pc=";
            const auto resolved = resolver ? resolver(record) : std::nullopt;
            if (resolved) {
                const std::uint64_t aligned_offset =
                    resolved->offset & ~(kRegionSize - 1ULL);
                const std::string module =
                    SanitizeModuleToken(resolved->module);
                line << module << '+' << FormatHexOffset(resolved->offset)
                     << ",region=" << module << '+'
                     << FormatHexOffset(aligned_offset);
            } else {
                line << "unresolved,region=unresolved";
            }
        } else if (record.pc_group_state ==
                   isomr::BionicThreads::WorkerPcGroupState::kOverflow) {
            line << "overflow,pc=unavailable,region=unavailable";
        } else {
            line << "unavailable,pc=unavailable,region=unavailable";
        }
    }
    return line.str();
}

std::string FormatWorkerExecutionWatchdogLine(
    std::uint32_t elapsed,
    const isomr::BionicThreads::WorkerExecutionSnapshot& snapshot) {
    std::ostringstream line;
    line << "ISOMR_CANDY_WORKER_EXECUTION elapsed=" << elapsed
         << " records=";
    if (snapshot.records.empty()) line << "none";
    bool first = true;
    for (const auto& record : snapshot.records) {
        if (!first) line << ';';
        first = false;
        line << "ordinal=" << record.ordinal
             << ",start-group=" << record.start_group
             << ",phase=" << WorkerExecutionPhaseName(record.phase)
             << ",pc-group=";
        if (record.pc_group_state ==
            isomr::BionicThreads::WorkerPcGroupState::kAvailable)
            line << record.pc_group;
        else if (record.pc_group_state ==
                 isomr::BionicThreads::WorkerPcGroupState::kOverflow)
            line << "overflow";
        else
            line << "unavailable";
        line << ",sample-sequence=" << record.sample_sequence
             << ",host-residency="
             << WorkerHostResidencyName(record.host_residency);
    }
    return line.str();
}

void PrintWorkerExecutionSamplingMarker(bool enabled,
                                        std::uint64_t slice_ticks) {
    if (slice_ticks == 0) {
        std::printf(
            "ISOMR_WORKER_EXECUTION_SAMPLING config=%s cadence=watchdog-5s "
            "selector=ordinals:0,1 boundaries=existing-only "
            "pc-group=exact-process-local host-residency=one-win32-control-sample "
            "address-free=1\n",
            enabled ? "enabled" : "disabled");
        return;
    }
    std::printf(
        "ISOMR_WORKER_EXECUTION_SAMPLING config=enabled cadence=watchdog-5s "
        "selector=ordinals:0,1 boundaries=inter-slice slice-ticks=%llu "
        "callback=owner-thread-noop pc-group=exact-process-local "
        "host-residency=one-win32-control-sample "
        "address-free=1\n",
        static_cast<unsigned long long>(slice_ticks));
}

void PrintWorkerExecutionModuleRegionsMarker() {
    std::puts(
        "ISOMR_WORKER_EXECUTION_MODULE_REGIONS config=enabled "
        "cadence=watchdog-5s selector=ordinals:0,1 region-size=4096 "
        "fields=pc,region addresses=module-relative-local-only");
}

void PrintWorkerExecutionFormatSelfTest() {
    isomr::BionicThreads::WorkerExecutionSnapshot snapshot;
    snapshot.records = {
        {0, 0, isomr::WorkerExecutionPhase::kBeforeRun,
         isomr::BionicThreads::WorkerPcGroupState::kAvailable, 4, 7,
         isomr::BionicThreads::WorkerHostResidency::
             kGeneratedPrivateExecutable},
        {1, 0, isomr::WorkerExecutionPhase::kDestroyed,
         isomr::BionicThreads::WorkerPcGroupState::kUnavailable, 0, 9,
         isomr::BionicThreads::WorkerHostResidency::kUnavailable}};
    std::puts(FormatWorkerExecutionWatchdogLine(5, snapshot).c_str());
}

void PrintWorkerExecutionModuleRegionsFormatSelfTest() {
    isomr::BionicThreads::WorkerExecutionSnapshot snapshot;
    snapshot.records = {
        {0, 0, isomr::WorkerExecutionPhase::kBeforeRun,
         isomr::BionicThreads::WorkerPcGroupState::kAvailable, 4, 7,
         isomr::BionicThreads::WorkerHostResidency::
             kGeneratedPrivateExecutable},
        {1, 0, isomr::WorkerExecutionPhase::kDestroyed,
         isomr::BionicThreads::WorkerPcGroupState::kUnavailable, 0, 9,
         isomr::BionicThreads::WorkerHostResidency::kUnavailable}};
    std::puts(FormatWorkerExecutionModuleRegionsLine(
                  5, snapshot,
                  [](const isomr::BionicThreads::WorkerExecutionRecord& record)
                      -> std::optional<isomr::Aarch64ProfileRuntime::
                                           ResolvedGuestCodeAddress> {
                      if (record.pc_group != 4) return std::nullopt;
                      return isomr::Aarch64ProfileRuntime::
                          ResolvedGuestCodeAddress{"lib test.so", 0x1ffff};
                  })
                  .c_str());
}

enum class NanosleepOptionResult {
    kNotRecognized,
    kAccepted,
    kRejected,
};

NanosleepOptionResult ParseNanosleepBackendOption(
    std::wstring_view option, isomr::BionicNanosleepBackend* backend,
    bool* configured, const char** parse_error) {
    constexpr std::wstring_view kPrefix = L"nanosleep-backend=";
    if (!option.starts_with(kPrefix))
        return NanosleepOptionResult::kNotRecognized;
    if (backend == nullptr || configured == nullptr || parse_error == nullptr)
        return NanosleepOptionResult::kRejected;
    if (*configured) {
        *parse_error = "duplicate nanosleep backend";
        return NanosleepOptionResult::kRejected;
    }
    const std::wstring_view value = option.substr(kPrefix.size());
    if (value == L"sleep-for") {
        *backend = isomr::BionicNanosleepBackend::kSleepFor;
    } else if (value == L"high-resolution-waitable") {
        *backend =
            isomr::BionicNanosleepBackend::kHighResolutionWaitable;
    } else {
        *parse_error = "invalid nanosleep backend";
        return NanosleepOptionResult::kRejected;
    }
    *configured = true;
    return NanosleepOptionResult::kAccepted;
}

int RunNanosleepConfigSelfTest(
    isomr::BionicNanosleepBackend backend,
    bool nanosleep_callsite_telemetry_enabled = false,
    isomr::NativeOpenSlMode opensl_mode =
        isomr::NativeOpenSlMode::kDisabled) {
    constexpr std::uint64_t kHeapBase = 0x710000000000ULL;
    constexpr std::uint64_t kImportBase = 0x680000000000ULL;
    isomr::BionicRuntime runtime(
        kHeapBase, 16U * 1024U * 1024U, kImportBase, {}, {}, {}, 4096,
        nullptr, 0, 0, 0, false, {}, backend, {},
        nanosleep_callsite_telemetry_enabled, opensl_mode);
    std::string error;
    if (!runtime.Initialize(&error)) {
        std::fprintf(stderr,
                     "ISOMR_CANDY_NANOSLEEP_CONFIG_SELF_TEST=refused error=%s\n",
                     error.c_str());
        return 1;
    }
    if (!runtime.Shutdown(&error)) {
        std::fprintf(stderr,
                     "ISOMR_CANDY_NANOSLEEP_CONFIG_SELF_TEST=shutdown-fail error=%s\n",
                     error.c_str());
        return 1;
    }
    std::puts("ISOMR_CANDY_NANOSLEEP_CONFIG_SELF_TEST=pass");
    return 0;
}

int RunNanosleepConfigSelfTest(int argc, wchar_t** argv) {
    isomr::BionicNanosleepBackend backend =
        isomr::BionicNanosleepBackend::kSleepFor;
    bool configured = false;
    for (int index = 2; index < argc; ++index) {
        const std::wstring_view option =
            argv[index] != nullptr ? argv[index] : L"";
        const char* parse_error = "invalid nanosleep backend";
        const NanosleepOptionResult parsed = ParseNanosleepBackendOption(
            option, &backend, &configured, &parse_error);
        if (parsed != NanosleepOptionResult::kAccepted) {
            std::fprintf(stderr, "%s\n",
                         parsed == NanosleepOptionResult::kNotRecognized
                             ? "invalid nanosleep backend"
                             : parse_error);
            return 2;
        }
    }
    return RunNanosleepConfigSelfTest(backend);
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

bool ReleaseIfPresent(isomr::GuestJni* jni, std::uint64_t reference,
                      std::string* first_error) {
    if (reference == 0) return true;
    std::string error;
    if (jni->ReleaseCompatibilityLocalReference(reference, &error))
        return true;
    if (first_error->empty()) *first_error = std::move(error);
    return false;
}

bool Cleanup(isomr::Aarch64ProfileRuntime* runtime,
             const isomr::CandyEntryHandles& handles,
             std::string* error, bool guest_process_exit = false) {
    std::string first_error;
    isomr::GuestJni* jni = &runtime->bionic()->jni();
    bool ok = ReleaseIfPresent(jni, handles.asset_manager, &first_error);
    ok = ReleaseIfPresent(jni, handles.version_string, &first_error) && ok;
    ok = ReleaseIfPresent(jni, handles.native_application, &first_error) && ok;
    ok = ReleaseIfPresent(jni, handles.activity, &first_error) && ok;
    ok = ReleaseIfPresent(jni, handles.platform_proxy_class, &first_error) && ok;
    const std::size_t pending_finalizers =
        guest_process_exit ? runtime->bionic()->at_exit().size() : 0;
    const std::size_t pending_dynamic_handles =
        guest_process_exit
            ? runtime->bionic()->dynamic_loader().handle_count()
            : 0;
    std::string shutdown_error;
    if (!runtime->Shutdown(
            &shutdown_error,
            guest_process_exit
                ? isomr::Aarch64ProfileShutdownMode::kGuestProcessExit
                : isomr::Aarch64ProfileShutdownMode::kRunGuestFinalizers)) {
        ok = false;
        if (first_error.empty()) first_error = std::move(shutdown_error);
    } else if (guest_process_exit) {
        std::printf(
            "ISOMR_CANDY_PROCESS_EXIT finalizers=discarded count=%zu "
            "dl-handles=discarded count=%zu "
            "reason=explicit-destroy-process-boundary\n",
            pending_finalizers, pending_dynamic_handles);
    }
    if (!ok && error != nullptr) *error = std::move(first_error);
    return ok;
}

bool HasMeaningfulGuestEvidence(
    std::size_t completed_calls, bool step_keep_running,
    std::size_t draw_calls, std::size_t presents,
    std::size_t lit_presents, bool lit_pixel_observed,
    std::size_t pointer_events) {
    constexpr std::size_t kMandatoryCallsBeforeFirstStep = 7;
    return completed_calls > kMandatoryCallsBeforeFirstStep &&
           step_keep_running && draw_calls != 0 && presents != 0 &&
           lit_presents != 0 && lit_pixel_observed &&
           pointer_events != 0;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    (void)std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::puts(
        "ISOMR_GUEST_HEAP_ACCESS config=shared-lock-generation-span-cache");
    std::puts(
        "ISOMR_GUEST_HEAP_METADATA config=offset-map-augmented-free-avl-first-fit-v2");
    if (argc == 2 && argv != nullptr && argv[1] != nullptr &&
        std::wstring_view(argv[1]) == L"--self-test-evidence") {
        const bool pass =
            HasMeaningfulGuestEvidence(8, true, 1, 1, 1, true, 1);
        const bool rejects_missing_evidence =
            !HasMeaningfulGuestEvidence(7, true, 1, 1, 1, true, 1) &&
            !HasMeaningfulGuestEvidence(8, false, 1, 1, 1, true, 1) &&
            !HasMeaningfulGuestEvidence(8, true, 0, 1, 1, true, 1) &&
            !HasMeaningfulGuestEvidence(8, true, 1, 0, 1, true, 1) &&
            !HasMeaningfulGuestEvidence(8, true, 1, 1, 0, true, 1) &&
            !HasMeaningfulGuestEvidence(8, true, 1, 1, 1, false, 1) &&
            !HasMeaningfulGuestEvidence(8, true, 1, 1, 1, true, 0);
        if (!pass || !rejects_missing_evidence) {
            std::fputs(
                "ISOMR_CANDY_ENTRY_EVIDENCE_SELF_TEST=fail\n", stderr);
            return 1;
        }
        std::puts("ISOMR_CANDY_ENTRY_EVIDENCE_SELF_TEST=pass");
        return 0;
    }
    if (argc >= 2 && argv != nullptr && argv[1] != nullptr &&
        std::wstring_view(argv[1]) == L"--self-test-nanosleep-config")
        return RunNanosleepConfigSelfTest(argc, argv);
    if ((argc != 4 && argc < 7) || argv == nullptr ||
        argv[1] == nullptr ||
        argv[2] == nullptr || argv[3] == nullptr) {
        std::fputs(
            "usage: ArmrCandyEntryProbe <root-library> <payload-root> <data-root> [watch-allocation-size watch-range-offset watch-range-size [call-telemetry|call-envelope-telemetry|call-svc-category-telemetry|nanosleep-callsite-telemetry|qsort-operation-telemetry|step-boundary-telemetry|qsort-algorithm=heap-sort|qsort-algorithm=bionic-partition|worker-cpu-sampling|worker-execution-sampling=ordinals:0,1|worker-execution-slice-ticks=<count>|worker-execution-module-regions|candy-vorbis-host-mirror|candy-webp-1.0.3-host=<absolute-dll>|candy-bulk-zero-stream|candy-rapidjson-host-mirror|opensl-timed-discard|direct-heap-page-table|present-pacing=<fps>|screen-size=<w>x<h>|import-journal|instruction-budget=<count>|post-analytics-instruction-budget=<count>|heap-capacity=<bytes>|nanosleep-backend=sleep-for|nanosleep-backend=high-resolution-waitable|analytics-no-value=null|analytics-no-value=empty|acknowledge-popup-without-ui-for-testing|close-after-popup-completion-for-testing|capture-read=<generation>[:occurrence>|capture-write=<generation>[:occurrence>|breakpoint=<address>]...]]\n",
            stderr);
        return 2;
    }
    isomr::Aarch64ProfileRuntimeConfig config;
    config.instruction_budget = 500000000ULL;
    config.file_mounts = {{"/app", argv[2]}, {"/data", argv[3], true}};
    config.guest_current_directory = "/data";
    config.host_dependencies = kAndroidHostDependencies;
    config.fail_closed_unimplemented_imports = true;
    // Same relocation mechanism as the CTR entry probe: ARMR_GUEST_HEAP_BASE
    // moves the guest arena into the direct page-table window so JIT
    // loads/stores hit host memory directly. The direct-heap-page-table flag
    // below stays an explicit way to request the same relocation.
    {
        wchar_t heap_base_text[32] = {};
        if (GetEnvironmentVariableW(L"ARMR_GUEST_HEAP_BASE", heap_base_text,
                                    32) > 0) {
            config.heap_base =
                std::wcstoull(heap_base_text, nullptr, 0);
            std::printf("ISOMR_DIRECT_HEAP base=0x%llx\n",
                        static_cast<unsigned long long>(config.heap_base));
        }
    }
    config.jni_compatibility.package_name =
        "com.king.candycrushsaga";
    config.jni_compatibility.package_code_path = "/app";
    config.jni_compatibility.package_version = "1.333.2.1";
    config.jni_compatibility.splash_removal_class =
        "com/king/candycrushsaga/PlatformProxy";
    isomr::CandyAnalyticsNoValueMode analytics_no_value_mode =
        isomr::CandyAnalyticsNoValueMode::kNull;
    bool analytics_no_value_configured = false;
    isomr::CandyPopupDialogMode popup_dialog_mode =
        isomr::CandyPopupDialogMode::kSystemTaskDialog;
    bool popup_dialog_mode_configured = false;
    bool close_after_popup_completion_for_testing = false;
    bool nanosleep_backend_configured = false;
    bool nanosleep_callsite_telemetry_configured = false;
    bool qsort_operation_telemetry_configured = false;
    bool qsort_operation_telemetry_enabled = false;
    bool step_boundary_telemetry_configured = false;
    bool step_boundary_telemetry_enabled = false;
    bool qsort_algorithm_configured = false;
    isomr::BionicThreads::QsortAlgorithm qsort_algorithm =
        isomr::BionicThreads::QsortAlgorithm::kHeapSort;
    bool worker_cpu_sampling_configured = false;
    bool worker_cpu_sampling_enabled = false;
    bool worker_execution_sampling_configured = false;
    bool worker_execution_sampling_enabled = false;
    bool worker_execution_module_regions_configured = false;
    bool worker_execution_module_regions_enabled = false;
    bool vorbis_host_mirror_configured = false;
    bool vorbis_host_mirror_enabled = false;
    bool webp_103_host_configured = false;
    std::wstring webp_103_host_library;
    bool bulk_zero_stream_configured = false;
    bool bulk_zero_stream_enabled = false;
bool screen_size_set = false;
    int screen_size_width = 0;
    int screen_size_height = 0;
    bool rapidjson_host_mirror_configured = false;
    bool rapidjson_host_mirror_enabled = false;
    bool opensl_timed_discard_configured = false;
    bool worker_execution_slice_ticks_configured = false;
    std::uint64_t worker_execution_slice_ticks = 0;
    bool config_self_test = false;
    isomr::Aarch64CallTelemetryMode call_telemetry_mode =
        isomr::Aarch64CallTelemetryMode::kOff;
    std::uint64_t post_analytics_instruction_budget = 0;
    if (argc >= 7 &&
        (!ParseSizeArgument(argv[4], &config.heap_watch_allocation_size) ||
         !ParseSizeArgument(argv[5], &config.heap_watch_range_offset) ||
         !ParseSizeArgument(argv[6], &config.heap_watch_range_size))) {
        std::fputs("invalid heap watch configuration\n", stderr);
        return 2;
    }
    if (argc >= 7)
        std::printf(
            "ISOMR_HEAP_WATCH config=allocation-size%zu range-offset%zu range-size%zu\n",
            config.heap_watch_allocation_size,
            config.heap_watch_range_offset, config.heap_watch_range_size);
    for (int index = 7; index < argc; ++index) {
        constexpr std::wstring_view kInstructionBudgetPrefix =
            L"instruction-budget=";
        constexpr std::wstring_view kPostAnalyticsInstructionBudgetPrefix =
            L"post-analytics-instruction-budget=";
        constexpr std::wstring_view kHeapCapacityPrefix = L"heap-capacity=";
        constexpr std::wstring_view kReadPrefix = L"capture-read=";
        constexpr std::wstring_view kWritePrefix = L"capture-write=";
        constexpr std::wstring_view kAnalyticsNoValuePrefix =
            L"analytics-no-value=";
        constexpr std::wstring_view kQsortAlgorithmPrefix =
            L"qsort-algorithm=";
        constexpr std::wstring_view kWebP103HostPrefix =
            L"candy-webp-1.0.3-host=";
        const std::wstring_view option =
            argv[index] != nullptr ? argv[index] : L"";
        const char* nanosleep_parse_error = "invalid nanosleep backend";
        const NanosleepOptionResult nanosleep_option =
            ParseNanosleepBackendOption(
                option, &config.nanosleep_backend,
                &nanosleep_backend_configured, &nanosleep_parse_error);
        if (nanosleep_option == NanosleepOptionResult::kRejected) {
            std::fprintf(stderr, "%s\n", nanosleep_parse_error);
            return 2;
        }
        if (nanosleep_option == NanosleepOptionResult::kAccepted) continue;
        if (option == L"nanosleep-callsite-telemetry") {
            if (nanosleep_callsite_telemetry_configured) {
                std::fputs(
                    "duplicate nanosleep callsite telemetry mode\n",
                    stderr);
                return 2;
            }
            nanosleep_callsite_telemetry_configured = true;
            config.nanosleep_callsite_telemetry_enabled = true;
            continue;
        }
        if (option == L"worker-cpu-sampling") {
            if (worker_cpu_sampling_configured) {
                std::fputs("duplicate worker CPU sampling mode\n", stderr);
                return 2;
            }
            worker_cpu_sampling_configured = true;
            worker_cpu_sampling_enabled = true;
            continue;
        }
        if (option == L"opensl-timed-discard") {
            if (opensl_timed_discard_configured) {
                std::fputs("duplicate OpenSL timed-discard mode\n", stderr);
                return 2;
            }
            opensl_timed_discard_configured = true;
            config.opensl_mode =
                isomr::NativeOpenSlMode::kTimedDiscard;
            continue;
        }
        constexpr std::wstring_view kWorkerExecutionPrefix =
            L"worker-execution-sampling=";
        if (option.starts_with(kWorkerExecutionPrefix)) {
            if (worker_execution_sampling_configured) {
                std::fputs("duplicate worker execution sampling mode\n", stderr);
                return 2;
            }
            worker_execution_sampling_configured = true;
            if (option.substr(kWorkerExecutionPrefix.size()) !=
                L"ordinals:0,1") {
                std::fputs("invalid worker execution sampling selector\n", stderr);
                return 2;
            }
            worker_execution_sampling_enabled = true;
            continue;
        }
        constexpr std::wstring_view kWorkerExecutionSlicePrefix =
            L"worker-execution-slice-ticks=";
        if (option.starts_with(kWorkerExecutionSlicePrefix)) {
            if (worker_execution_slice_ticks_configured) {
                std::fputs("duplicate worker execution slice ticks\n", stderr);
                return 2;
            }
            worker_execution_slice_ticks_configured = true;
            if (!ParseStrictNonzeroUint64Argument(
                    option.substr(kWorkerExecutionSlicePrefix.size()),
                    &worker_execution_slice_ticks)) {
                std::fputs("invalid worker execution slice ticks\n", stderr);
                return 2;
            }
            continue;
        }
        if (option == L"config-self-test") {
            if (config_self_test) {
                std::fputs("duplicate config self-test mode\n", stderr);
                return 2;
            }
            config_self_test = true;
            continue;
        }
        if (option == L"worker-execution-module-regions") {
            if (worker_execution_module_regions_configured) {
                std::fputs(
                    "duplicate worker execution module regions mode\n",
                    stderr);
                return 2;
            }
            worker_execution_module_regions_configured = true;
            worker_execution_module_regions_enabled = true;
            continue;
        }
        if (option == L"candy-vorbis-host-mirror") {
            if (vorbis_host_mirror_configured) {
                std::fputs("duplicate Candy Vorbis host mirror mode\n",
                           stderr);
                return 2;
            }
            vorbis_host_mirror_configured = true;
            vorbis_host_mirror_enabled = true;
            continue;
        }
        if (option.starts_with(kWebP103HostPrefix)) {
            if (webp_103_host_configured ||
                option.size() == kWebP103HostPrefix.size()) {
                std::fputs("invalid or duplicate Candy WebP 1.0.3 host mode\n",
                           stderr);
                return 2;
            }
            webp_103_host_configured = true;
            webp_103_host_library =
                std::wstring(option.substr(kWebP103HostPrefix.size()));
            continue;
        }
        if (option == L"candy-bulk-zero-stream") {
            if (bulk_zero_stream_configured) {
                std::fputs("duplicate Candy bulk zero-stream mode\n",
                           stderr);
                return 2;
            }
            bulk_zero_stream_configured = true;
            bulk_zero_stream_enabled = true;
            continue;
        }
        if (option == L"candy-rapidjson-host-mirror") {
            if (rapidjson_host_mirror_configured) {
                std::fputs("duplicate Candy RapidJSON host mirror mode\n",
                           stderr);
                return 2;
            }
            rapidjson_host_mirror_configured = true;
            rapidjson_host_mirror_enabled = true;
            continue;
        }
        if (option == L"acknowledge-popup-without-ui-for-testing") {
            if (popup_dialog_mode_configured) {
                std::fputs("duplicate popup dialog mode\n", stderr);
                return 2;
            }
            popup_dialog_mode_configured = true;
            popup_dialog_mode = isomr::CandyPopupDialogMode::
                kAcknowledgeWithoutUiForTesting;
            continue;
        }
        if (option == L"close-after-popup-completion-for-testing") {
            if (close_after_popup_completion_for_testing) {
                std::fputs("duplicate test popup close mode\n", stderr);
                return 2;
            }
            close_after_popup_completion_for_testing = true;
            continue;
        }
        if (option == L"qsort-operation-telemetry") {
            if (qsort_operation_telemetry_configured) {
                std::fputs(
                    "duplicate qsort operation telemetry mode\n", stderr);
                return 2;
            }
            qsort_operation_telemetry_configured = true;
            qsort_operation_telemetry_enabled = true;
            continue;
        }
        if (option == L"step-boundary-telemetry") {
            if (step_boundary_telemetry_configured) {
                std::fputs(
                    "duplicate step boundary telemetry mode\n", stderr);
                return 2;
            }
            step_boundary_telemetry_configured = true;
            step_boundary_telemetry_enabled = true;
            continue;
        }
        if (option.starts_with(kQsortAlgorithmPrefix)) {
            if (qsort_algorithm_configured) {
                std::fputs("duplicate qsort algorithm mode\n", stderr);
                return 2;
            }
            const std::wstring_view algorithm =
                option.substr(kQsortAlgorithmPrefix.size());
            if (algorithm == L"heap-sort") {
                qsort_algorithm =
                    isomr::BionicThreads::QsortAlgorithm::kHeapSort;
            } else if (algorithm == L"bionic-partition") {
                qsort_algorithm = isomr::BionicThreads::QsortAlgorithm::
                    kBionicPartition;
            } else {
                std::fputs("invalid qsort algorithm mode\n", stderr);
                return 2;
            }
            qsort_algorithm_configured = true;
            continue;
        }
        if (option.starts_with(kAnalyticsNoValuePrefix)) {
            if (analytics_no_value_configured) {
                std::fputs("duplicate analytics no-value mode\n", stderr);
                return 2;
            }
            const std::wstring_view mode =
                option.substr(kAnalyticsNoValuePrefix.size());
            if (mode == L"null") {
                analytics_no_value_mode =
                    isomr::CandyAnalyticsNoValueMode::kNull;
            } else if (mode == L"empty") {
                analytics_no_value_mode =
                    isomr::CandyAnalyticsNoValueMode::kEmptyString;
            } else {
                std::fputs("invalid analytics no-value mode\n", stderr);
                return 2;
            }
            analytics_no_value_configured = true;
            continue;
        }
        if (option.starts_with(kPostAnalyticsInstructionBudgetPrefix)) {
            const std::wstring budget_text(option.substr(
                kPostAnalyticsInstructionBudgetPrefix.size()));
            if (post_analytics_instruction_budget != 0 ||
                !ParseUint64Argument(budget_text.c_str(),
                                     &post_analytics_instruction_budget) ||
                post_analytics_instruction_budget == 0) {
                std::fputs(
                    "invalid post-analytics instruction budget\n", stderr);
                return 2;
            }
            std::printf(
                "ISOMR_CANDY_INSTRUCTION_BUDGET config=post-analytics-%llu\n",
                static_cast<unsigned long long>(
                    post_analytics_instruction_budget));
            continue;
        }
        if (option.starts_with(kInstructionBudgetPrefix)) {
            const std::wstring budget_text(
                option.substr(kInstructionBudgetPrefix.size()));
            std::uint64_t budget = 0;
            if (!ParseUint64Argument(budget_text.c_str(), &budget) ||
                budget == 0) {
                std::fputs("invalid instruction budget\n", stderr);
                return 2;
            }
            config.instruction_budget = budget;
            std::printf("ISOMR_INSTRUCTION_BUDGET config=%llu\n",
                        static_cast<unsigned long long>(budget));
            continue;
        }
        if (option.starts_with(kHeapCapacityPrefix)) {
            constexpr std::size_t kMinimumHeapCapacity =
                16U * 1024U * 1024U;
            constexpr std::size_t kMaximumHeapCapacity =
                1024U * 1024U * 1024U;
            const std::wstring capacity_text(
                option.substr(kHeapCapacityPrefix.size()));
            std::size_t capacity = 0;
            if (!ParseSizeArgument(capacity_text.c_str(), &capacity) ||
                capacity < kMinimumHeapCapacity ||
                capacity > kMaximumHeapCapacity) {
                std::fputs("invalid heap capacity\n", stderr);
                return 2;
            }
            config.heap_capacity = capacity;
            std::printf("ISOMR_HEAP_CAPACITY config=%zu\n", capacity);
            continue;
        }
        if (option == L"direct-heap-page-table") {
            // Relocates the guest arena into the direct page-table address
            // space so Dynarmic can emit inline guest loads/stores for arena
            // pages. Checked callbacks keep handling every other region.
            config.heap_base = 0x40000000ULL;
            std::printf("ISOMR_DIRECT_HEAP_PAGE_TABLE config=heap-base=0x40000000\n");
            continue;
        }
        constexpr std::wstring_view kPresentPacingPrefix = L"present-pacing=";
        if (option.starts_with(kPresentPacingPrefix)) {
            const std::wstring fps_text(
                option.substr(kPresentPacingPrefix.size()));
            const wchar_t* fps_begin = fps_text.c_str();
            wchar_t* fps_end = nullptr;
            const double fps = std::wcstod(fps_begin, &fps_end);
            if (fps_end == fps_begin || *fps_end != L'\0' ||
                fps < 10.0 || fps > 360.0) {
                std::fputs("invalid present pacing\n", stderr);
                return 2;
            }
            isomr::NativeGles::SetProcessPresentPacingFps(fps);
            std::printf("ISOMR_PRESENT_PACING config=%.3f\n", fps);
            continue;
        }
        constexpr std::wstring_view kScreenSizeArgPrefix = L"screen-size=";
        if (option.starts_with(kScreenSizeArgPrefix)) {
            const std::wstring size_text(
                option.substr(kScreenSizeArgPrefix.size()));
            const std::size_t separator = size_text.find(L'x');
            int width = 0;
            int height = 0;
            if (separator != std::wstring::npos) {
                try {
                    width = std::stoi(size_text.substr(0, separator));
                    height = std::stoi(size_text.substr(separator + 1));
                } catch (const std::exception&) {
                    width = 0;
                    height = 0;
                }
            }
            if (width < 240 || width > 2160 || height < 240 ||
                height > 2160) {
                std::fputs("invalid screen size\n", stderr);
                return 2;
            }
            isomr::NativeGles::SetProcessInitialSurfaceSize(width, height);
            screen_size_set = true;
            screen_size_width = width;
            screen_size_height = height;
            std::printf("ISOMR_SCREEN_SIZE config=%dx%d\n", width, height);
            continue;
        }
        constexpr std::wstring_view kBreakpointPrefix = L"breakpoint=";
        if (option.starts_with(kBreakpointPrefix) &&
            config.memory_capture.breakpoint_address == 0 &&
            ParseAddressArgument(option.substr(kBreakpointPrefix.size()),
                                 &config.memory_capture.breakpoint_address)) {
            std::printf(
                "ISOMR_EXECUTION_BREAKPOINT config=address0x%llx\n",
                static_cast<unsigned long long>(
                    config.memory_capture.breakpoint_address));
            continue;
        }
        if (option == L"import-journal" &&
            !config.import_journal_enabled) {
            config.import_journal_enabled = true;
            std::puts("ISOMR_IMPORT_JOURNAL config=enabled capacity=64");
            continue;
        }
        if (option == L"call-telemetry" ||
            option == L"call-envelope-telemetry" ||
            option == L"call-svc-category-telemetry") {
            if (call_telemetry_mode !=
                isomr::Aarch64CallTelemetryMode::kOff) {
                std::fputs("duplicate or incompatible call telemetry mode\n",
                           stderr);
                return 2;
            }
            if (option == L"call-telemetry") {
                call_telemetry_mode =
                    isomr::Aarch64CallTelemetryMode::kFull;
            } else if (option == L"call-envelope-telemetry") {
                call_telemetry_mode =
                    isomr::Aarch64CallTelemetryMode::kEnvelope;
            } else {
                call_telemetry_mode =
                    isomr::Aarch64CallTelemetryMode::kSvcCategory;
            }
            continue;
        }
        const bool capture_read = option.starts_with(kReadPrefix);
        const bool capture_write = option.starts_with(kWritePrefix);
        const std::size_t prefix_size = capture_read
                                            ? kReadPrefix.size()
                                            : capture_write
                                                  ? kWritePrefix.size()
                                                  : 0;
        std::uint64_t generation = 0;
        std::uint64_t occurrence = 0;
        if (prefix_size == 0 || config.memory_capture.watch_generation != 0 ||
            !ParseCaptureArgument(option.substr(prefix_size), &generation,
                                  &occurrence)) {
            std::fputs("invalid diagnostic option\n", stderr);
            return 2;
        }
        config.memory_capture.access =
            capture_read ? isomr::Aarch64MemoryCaptureAccess::kRead
                         : isomr::Aarch64MemoryCaptureAccess::kWrite;
        config.memory_capture.watch_generation = generation;
        config.memory_capture.occurrence = occurrence;
        std::printf("ISOMR_MEMORY_CAPTURE config=access-%s generation%llu occurrence%llu\n",
                    capture_read ? "read" : "write",
                    static_cast<unsigned long long>(generation),
                    static_cast<unsigned long long>(occurrence));
    }
    std::printf(
        "ISOMR_BIONIC_QSORT_TELEMETRY config=%s scope=operation-boundary fields=count,element-size,comparisons,swaps,wall-ns,executor-setup-wall-ns,comparator-wall-ns,swap-wall-ns,residual-wall-ns addresses=none\n",
        qsort_operation_telemetry_enabled ? "enabled" : "disabled");
    std::printf(
        "ISOMR_CANDY_STEP_TELEMETRY config=%s scope=guest-step-call,frame-cadence,queued-pointer-latency step-fields=wall-ns,thread-cpu-available,thread-cpu-100ns,qsort-started,qsort-completed,qsort-failed,qsort-comparisons,qsort-swaps,qsort-wall-ns,qsort-executor-setup-wall-ns,qsort-comparator-wall-ns,qsort-swap-wall-ns,qsort-residual-wall-ns,qsort-open-end frame-fields=iteration-complete,frame-wall-ns,guest-draws,host-presents,lit-presents,pointer-events pointer-fields=sequence,action,queue-wait-ns,guest-call-ns,total-ns addresses=none\n",
        step_boundary_telemetry_enabled ? "enabled" : "disabled");
    std::printf(
        "ISOMR_CANDY_LONG_CALL_PC config=%s trigger=step-wall-at-least-250000000ns boundary=deterministic-xorshift-jitter-184464-through-315535-mean-249999.5 cadence=first-then-deterministic-xorshift-1-in-8 fields=step,wall-threshold-ns,total-samples,retained,pc,lr,parent-lr,x0-min,x0-max,x1-min,x1-max,count,error,region-size,region,count,region-error addresses=module-relative-and-register-values-local-only\n",
        step_boundary_telemetry_enabled ? "enabled" : "disabled");
    if (qsort_algorithm ==
        isomr::BionicThreads::QsortAlgorithm::kBionicPartition) {
        std::puts(
            "ISOMR_BIONIC_QSORT_ALGORITHM config=bionic-partition pivot=median-three-nine small=insertion equal=three-way");
    } else {
        std::puts(
            "ISOMR_BIONIC_QSORT_ALGORITHM config=heap-sort pivot=none small=none equal=none");
    }
    std::printf(
        "ISOMR_NATIVE_OPENSL config=%s scope=timed-discard audio-player=wall-clock-paced sink=none callback=managed-thread address-free=1\n",
        config.opensl_mode == isomr::NativeOpenSlMode::kTimedDiscard
            ? "enabled"
            : "disabled");
    std::printf("ISOMR_CANDY_ANALYTICS config=no-value-%s\n",
                analytics_no_value_mode ==
                        isomr::CandyAnalyticsNoValueMode::kNull
                    ? "null"
                    : "empty");
    std::printf(
        "ISOMR_CANDY_POPUP_DIALOG config=%s\n",
        popup_dialog_mode ==
                isomr::CandyPopupDialogMode::kSystemTaskDialog
            ? "system-task-dialog"
            : "test-acknowledge-without-ui");
    std::printf(
        "ISOMR_CANDY_POPUP_CLOSE config=%s\n",
        close_after_popup_completion_for_testing
            ? "test-after-completion"
            : "disabled");
    if (worker_execution_sampling_enabled &&
        !worker_cpu_sampling_enabled) {
        std::fputs("worker execution sampling requires worker CPU sampling\n",
                   stderr);
        return 2;
    }
    if (close_after_popup_completion_for_testing &&
        popup_dialog_mode !=
            isomr::CandyPopupDialogMode::kAcknowledgeWithoutUiForTesting) {
        std::fputs(
            "test popup close requires no-UI popup acknowledgement\n",
            stderr);
        return 2;
    }
    if (worker_execution_slice_ticks != 0 &&
        (!worker_cpu_sampling_enabled ||
         !worker_execution_sampling_enabled)) {
        std::fputs(
            "worker execution slice ticks require worker CPU and execution sampling\n",
            stderr);
        return 2;
    }
    if (worker_execution_module_regions_enabled &&
        (!worker_cpu_sampling_enabled ||
         !worker_execution_sampling_enabled ||
         worker_execution_slice_ticks == 0)) {
        std::fputs(
            "worker execution module regions require worker CPU, execution, and inter-slice sampling\n",
            stderr);
        return 2;
    }
    std::shared_ptr<isomr::CandyVorbisHostMirror> vorbis_host_mirror;
    std::string vorbis_configuration_error;
    if (vorbis_host_mirror_enabled &&
        !isomr::ConfigureCandyVorbisHostMirror(
            &config, &vorbis_host_mirror, &vorbis_configuration_error)) {
        std::fprintf(stderr, "Candy Vorbis host mirror configuration failed: %s\n",
                     vorbis_configuration_error.c_str());
        return 2;
    }
    PrintCandyVorbisHostMarker(vorbis_host_mirror_enabled);
    std::shared_ptr<isomr::CandyWebP103Host> webp_103_host;
    std::string webp_103_configuration_error;
    if (webp_103_host_configured &&
        !isomr::ConfigureCandyWebP103Host(
            &config, &webp_103_host, webp_103_host_library,
            &webp_103_configuration_error)) {
        std::fprintf(stderr,
                     "Candy WebP 1.0.3 host configuration failed: %s\n",
                     webp_103_configuration_error.c_str());
        return 2;
    }
    PrintCandyWebP103HostMarker(webp_103_host != nullptr);
    PrintCandyBulkZeroStreamMarker(bulk_zero_stream_enabled);
    PrintCandyRapidJsonHostMirrorMarker(rapidjson_host_mirror_enabled);
    if (config_self_test) {
        PrintWorkerCpuSamplingMarker(worker_cpu_sampling_enabled);
        PrintWorkerExecutionSamplingMarker(worker_execution_sampling_enabled,
                                           worker_execution_slice_ticks);
        const char* telemetry_name = "off";
        if (call_telemetry_mode == isomr::Aarch64CallTelemetryMode::kFull)
            telemetry_name = "full";
        else if (call_telemetry_mode ==
                 isomr::Aarch64CallTelemetryMode::kEnvelope)
            telemetry_name = "envelope";
        else if (call_telemetry_mode ==
                 isomr::Aarch64CallTelemetryMode::kSvcCategory)
            telemetry_name = "svc-category";
        std::printf(
            "ISOMR_CANDY_ENTRY_CONFIG_SELF_TEST options=import-journal-%s telemetry-%s nanosleep-callsite-%s worker-cpu-%s capture-%s\n",
            config.import_journal_enabled ? "enabled" : "disabled",
            telemetry_name,
            config.nanosleep_callsite_telemetry_enabled ? "enabled"
                                                        : "disabled",
            worker_cpu_sampling_enabled ? "enabled" : "disabled",
            config.memory_capture.watch_generation != 0 ? "enabled"
                                                         : "disabled");
        if (worker_cpu_sampling_enabled) PrintWorkerCpuFormatSelfTest();
        if (worker_execution_sampling_enabled)
            PrintWorkerExecutionFormatSelfTest();
        if (worker_execution_module_regions_enabled) {
            PrintWorkerExecutionModuleRegionsMarker();
            PrintWorkerExecutionModuleRegionsFormatSelfTest();
        }
        const int result =
            RunNanosleepConfigSelfTest(
                config.nanosleep_backend,
                config.nanosleep_callsite_telemetry_enabled,
                config.opensl_mode);
        if (result == 0)
            std::puts("ISOMR_CANDY_ENTRY_CONFIG_SELF_TEST=pass");
        return result;
    }
    {
        wchar_t heap_fidelity_text[8] = {};
        if (GetEnvironmentVariableW(L"ISOMR_HEAP_FIDELITY",
                                    heap_fidelity_text, 8) > 0 &&
            heap_fidelity_text[0] == L'0') {
            isomr::GuestHeap::SetDeviceAllocatorFidelityForTesting(false);
            std::printf("ISOMR_HEAP_FIDELITY device-allocator-fidelity=off\n");
        }
    }
    isomr::Aarch64ProfileRuntime runtime(std::move(config));
    std::string error;
    if (!runtime.Load(argv[1], &error)) {
        ReportFailure("load", error);
        return 3;
    }
    if (worker_cpu_sampling_enabled &&
        !runtime.bionic()->threads().ConfigureWorkerCpuSampling(
            isomr::BionicThreads::WorkerCpuSamplingMode::kEnabled,
            &error)) {
        ReportFailure("worker-cpu-sampling", error);
        return 3;
    }
    if ((qsort_operation_telemetry_enabled ||
         step_boundary_telemetry_enabled) &&
        !runtime.bionic()->threads().ConfigureQsortOperationTelemetry(
            qsort_operation_telemetry_enabled
                ? isomr::BionicThreads::QsortOperationTelemetryMode::
                      kOperationBoundary
                : isomr::BionicThreads::QsortOperationTelemetryMode::
                      kAggregateOnly,
            &error)) {
        ReportFailure(
            qsort_operation_telemetry_enabled
                ? "qsort-operation-telemetry"
                : "step-boundary-telemetry",
            error);
        return 3;
    }
    if (!runtime.bionic()->threads().ConfigureQsortAlgorithm(
            qsort_algorithm, &error)) {
        ReportFailure("qsort-algorithm", error);
        return 3;
    }
    if (worker_execution_sampling_enabled &&
        !runtime.bionic()->threads().ConfigureWorkerExecutionSampling(
            isomr::BionicThreads::WorkerExecutionSamplingMode::kEnabled,
            {0, 1}, worker_execution_slice_ticks, &error)) {
        ReportFailure("worker-execution-sampling", error);
        return 3;
    }
    PrintWorkerCpuSamplingMarker(worker_cpu_sampling_enabled);
    PrintWorkerExecutionSamplingMarker(worker_execution_sampling_enabled,
                                       worker_execution_slice_ticks);
    if (worker_execution_module_regions_enabled)
        PrintWorkerExecutionModuleRegionsMarker();
    std::printf(
        "ISOMR_CANDY_ENTRY_PROBE stage=load result=pass modules=%zu imports=%zu initializers=%zu\n",
        runtime.module_count(), runtime.import_count(),
        runtime.initializer_count());
    constexpr std::array<std::string_view, 3> kStandardFailureSymbols{
        "_ZNSt6__ndk125__throw_bad_function_callEv",
        "_ZNSt3__125__throw_bad_function_callEv",
        "_ZNSt6__ndk122__libcpp_verbose_abortEPKcz"};
    for (const std::string_view symbol : kStandardFailureSymbols) {
        const std::optional<std::uint64_t> address = runtime.FindExport(symbol);
        if (address)
            std::printf(
                "ISOMR_CANDY_ENTRY_PROBE standard-symbol=%.*s address=0x%llx\n",
                static_cast<int>(symbol.size()), symbol.data(),
                static_cast<unsigned long long>(*address));
    }
    if (!runtime.RunInitializers(&error)) {
        ReportFailure("initializers", error);
        std::string cleanup_error;
        if (!runtime.Shutdown(&cleanup_error))
            ReportFailure("cleanup", cleanup_error);
        return 4;
    }
    std::puts("ISOMR_CANDY_ENTRY_PROBE stage=initializers result=pass");
    // Diagnostic: trap the sqlite3_open result inside usdk's
    // LocalDatabaseManager open path (game library + 0x42c8c74, the
    // instruction right after `blr x8` returns the open result in w0).
    // Dumps w0 (sqlite result), the db file object handle/error, and the
    // decoded db path so the cold-WAL null-store failure is attributable.
    {
        wchar_t sqlite_bp[8] = {};
        const DWORD sqlite_bp_len = GetEnvironmentVariableW(
            L"ISOMR_SQLITE_OPEN_BP", sqlite_bp, 8);
        if (sqlite_bp_len != 0 && sqlite_bp[0] == L'1') {
            constexpr std::uint64_t kSqliteOpenResultPc = 0x40042c8cacULL;
            std::string bp_error;
            if (!runtime.AddMainOwnerThreadExecutionBreakpoint(
                    kSqliteOpenResultPc,
                    [&runtime](isomr::Aarch64PausedExecutionState* state,
                               std::string*) {
                        const std::uint64_t fileobj = state->registers[19];
                        const isomr::GuestAddressSpace& mem =
                            runtime.bionic()->address_space();
                        std::uint8_t hdr[0x48] = {};
                        std::uint64_t handle = 0;
                        char path[128] = {};
                        if (fileobj != 0 &&
                            mem.Read(fileobj, hdr, sizeof(hdr))) {
                            std::memcpy(&handle, hdr + 0x38, 8);
                            const std::uint8_t tag = hdr[0x20];
                            if (tag & 1) {
                                std::uint64_t ptr = 0, size = 0;
                                std::memcpy(&ptr, hdr + 0x30, 8);
                                std::memcpy(&size, hdr + 0x28, 8);
                                const std::size_t n =
                                    size < sizeof(path) - 1
                                        ? static_cast<std::size_t>(size)
                                        : sizeof(path) - 1;
                                if (ptr != 0 && n != 0)
                                    mem.Read(ptr, path, n);
                            } else {
                                const std::size_t n =
                                    static_cast<std::size_t>(tag >> 1);
                                std::memcpy(path, hdr + 0x21,
                                            n < sizeof(path) - 1 ? n
                                                                 : sizeof(path) -
                                                                       1);
                            }
                        }
                        std::fprintf(
                            stderr,
                            "ISOMR_SQLITE_ERR sqlite_rc=%lld w0=%lld "
                            "fileobj=%llx handle=%llx path=%s\n",
                            static_cast<long long>(state->registers[20]),
                            static_cast<long long>(state->registers[0]),
                            static_cast<unsigned long long>(fileobj),
                            static_cast<unsigned long long>(handle),
                            path);
                        std::fflush(stderr);
                        return true;
                    },
                    &bp_error)) {
                std::fprintf(stderr,
                             "ISOMR_SQLITE_OPEN breakpoint failed: %s\n",
                             bp_error.c_str());
            } else {
                std::fprintf(stderr,
                             "ISOMR_SQLITE_OPEN breakpoint armed at 0x%llx\n",
                             (unsigned long long)kSqliteOpenResultPc);
            }
        }
    }
    std::uint64_t version = 0;
    if (!runtime.CallJniOnLoad(&version, &error)) {
        ReportFailure("jni-on-load", error);
        std::string cleanup_error;
        if (!runtime.Shutdown(&cleanup_error))
            ReportFailure("cleanup", cleanup_error);
        return 5;
    }
    std::printf(
        "ISOMR_CANDY_ENTRY_PROBE stage=jni-on-load result=pass version=0x%08llx\n",
        static_cast<unsigned long long>(version));

    isomr::CandyEntryHandles handles{};
    if (!CreateHandles(&runtime, &handles, &error)) {
        ReportFailure("references", error);
        std::string cleanup_error;
        if (!Cleanup(&runtime, handles, &cleanup_error))
            ReportFailure("cleanup", cleanup_error);
        return 6;
    }
    std::puts("ISOMR_CANDY_ENTRY_PROBE stage=references result=pass");
    if (call_telemetry_mode != isomr::Aarch64CallTelemetryMode::kOff) {
        if (!runtime.SetMainCallTelemetryMode(call_telemetry_mode, &error)) {
            ReportFailure("telemetry", error);
            std::string cleanup_error;
            if (!Cleanup(&runtime, handles, &cleanup_error))
                ReportFailure("cleanup", cleanup_error);
            return 6;
        }
        if (call_telemetry_mode ==
            isomr::Aarch64CallTelemetryMode::kEnvelope) {
            std::puts("ISOMR_AARCH64_CALL_ENVELOPE config=enabled scope=main-bridge svc-timing=off memory-counters=off");
        } else if (call_telemetry_mode ==
                   isomr::Aarch64CallTelemetryMode::kSvcCategory) {
            std::puts("ISOMR_AARCH64_CALL_SVC_CATEGORY config=enabled scope=main-bridge address-free=1 per-svc-lines=off memory-counters=off import-journal=unchanged");
        } else {
            std::puts("ISOMR_AARCH64_CALL_TELEMETRY config=enabled");
            std::puts(
                "ISOMR_AARCH64_CALL_GUEST_PC config=inter-slice-space-saving64 scope=main-bridge fields=pc,lr,parent-lr,x0-range,x1-range,x2-range,x3-range,x8-range,x10-range,x11-range,x19-range,x22-range,x24-range addresses=guest-local-only host-addresses=none");
        }
    }
    isomr::CandyEntryOptions options;
    options.step_count = 600;
    options.run_until_close = true;
    options.show_window = true;
    options.present_each_step = true;
    if (screen_size_set) {
        options.width = screen_size_width;
        options.height = screen_size_height;
    }
    options.step_boundary_telemetry = step_boundary_telemetry_enabled;
    options.bulk_zero_stream = bulk_zero_stream_enabled;
    options.rapidjson_host_mirror = rapidjson_host_mirror_enabled;
    options.analytics_no_value_mode = analytics_no_value_mode;
    options.popup_dialog_mode = popup_dialog_mode;
    options.request_close_after_popup_completion_for_testing =
        close_after_popup_completion_for_testing;
    options.post_analytics_instruction_budget =
        post_analytics_instruction_budget;
    isomr::CandyEntryResult result{};
    std::mutex watchdog_mutex;
    std::condition_variable watchdog_changed;
    bool bridge_finished = false;
    runtime.bionic()->jni().ResetHttpLifecycleEvents();
    runtime.bionic()->jni().ResetHttpObservationEvents();
    std::thread watchdog([&] {
        std::unique_lock lock(watchdog_mutex);
        std::uint64_t last_http_sequence = 0;
        std::uint64_t last_http_observation_sequence = 0;
        std::unique_ptr<std::unordered_map<std::uint64_t, std::uint64_t>>
            previous_worker_cpu;
        if (worker_cpu_sampling_enabled) {
            previous_worker_cpu = std::make_unique<
                std::unordered_map<std::uint64_t, std::uint64_t>>();
        }
        for (std::uint32_t elapsed = 5; !bridge_finished; elapsed += 5) {
            if (watchdog_changed.wait_for(
                    lock, std::chrono::seconds(5),
                    [&] { return bridge_finished; }))
                break;
            lock.unlock();
            const bool detailed_watchdog =
                config.import_journal_enabled ||
                worker_cpu_sampling_enabled ||
                worker_execution_sampling_enabled ||
                vorbis_host_mirror != nullptr || webp_103_host != nullptr;
            if (!detailed_watchdog) {
                std::printf(
                    "ISOMR_CANDY_WATCHDOG elapsed=%u diagnostics=disabled\n",
                    elapsed);
                lock.lock();
                continue;
            }
            const std::string journal = config.import_journal_enabled
                                            ? runtime.bionic()
                                                  ->imports()
                                                  .RecentDispatchJournal(8)
                                            : std::string{};
            std::printf(
                "ISOMR_CANDY_WATCHDOG elapsed=%u threads=%zu active=%zu "
                "heap-allocated=%zu heap-capacity=%zu allocations=%zu "
                "journal=%s\n",
                elapsed, runtime.bionic()->threads().thread_count(),
                runtime.bionic()->threads().active_count(),
                runtime.bionic()->heap().allocated_bytes(),
                runtime.bionic()->heap().capacity(),
                runtime.bionic()->heap().allocation_count(),
                journal.empty() ? "disabled" : journal.c_str());
            if (worker_cpu_sampling_enabled) {
                isomr::BionicThreads::WorkerCpuSnapshot cpu_snapshot;
                if (runtime.bionic()->threads().TakeWorkerCpuSnapshot(
                        &cpu_snapshot)) {
                    std::puts(FormatWorkerCpuWatchdogLine(
                                  elapsed, cpu_snapshot,
                                  previous_worker_cpu.get())
                                  .c_str());
                } else {
                    std::printf(
                        "ISOMR_CANDY_WORKER_CPU elapsed=%u "
                        "total=unavailable sampled=unavailable "
                        "dropped=unavailable records=cpu=unavailable\n",
                        elapsed);
                }
            }
            if (worker_execution_sampling_enabled) {
                isomr::BionicThreads::WorkerExecutionSnapshot snapshot;
                if (runtime.bionic()->threads().TakeWorkerExecutionSnapshot(
                        &snapshot)) {
                    std::puts(FormatWorkerExecutionWatchdogLine(
                                  elapsed, snapshot).c_str());
                    if (worker_execution_module_regions_enabled) {
                        const auto resolver =
                            [&runtime](const isomr::BionicThreads::
                                           WorkerExecutionRecord& record)
                            -> std::optional<isomr::Aarch64ProfileRuntime::
                                                 ResolvedGuestCodeAddress> {
                            std::uint64_t guest_pc = 0;
                            if (!runtime.bionic()
                                     ->threads()
                                     .ResolveWorkerExecutionGuestPcLocalOnly(
                                         record, &guest_pc))
                                return std::nullopt;
                            return runtime.ResolveGuestCodeAddress(guest_pc);
                        };
                        std::puts(FormatWorkerExecutionModuleRegionsLine(
                                      elapsed, snapshot, resolver)
                                      .c_str());
                    }
                }
            }
            PrintCandyVorbisHostSnapshot(
                elapsed, vorbis_host_mirror, "watchdog");
            PrintCandyWebP103HostSnapshot(
                elapsed, webp_103_host, "watchdog");
            const auto http_events =
                runtime.bionic()->jni().http_lifecycle_events();
            for (const auto& event : http_events) {
                if (event.sequence <= last_http_sequence) continue;
                std::printf(
                    "ISOMR_CANDY_HTTP_EVENT kind=%s request=%llu "
                    "sequence=%llu thread=0x%llx\n",
                    HttpLifecycleKindName(event.kind),
                    static_cast<unsigned long long>(event.request_id),
                    static_cast<unsigned long long>(event.sequence),
                    static_cast<unsigned long long>(event.host_thread_hash));
                last_http_sequence = event.sequence;
            }
            const auto http_observations =
                runtime.bionic()->jni().http_observation_events();
            for (const auto& event : http_observations) {
                if (event.sequence <= last_http_observation_sequence)
                    continue;
                std::printf(
                    "ISOMR_CANDY_HTTP_OBSERVATION kind=%s sequence=%llu "
                    "thread=0x%llx owner=%u class=%s method=%s "
                    "signature=%s arguments=0x%llx,0x%llx,0x%llx,0x%llx\n",
                    HttpObservationKindName(event.kind),
                    static_cast<unsigned long long>(event.sequence),
                    static_cast<unsigned long long>(
                        event.host_thread_hash),
                    event.owner_thread ? 1U : 0U,
                    event.class_name.c_str(), event.method_name.c_str(),
                    event.signature.c_str(),
                    static_cast<unsigned long long>(event.arguments[0]),
                    static_cast<unsigned long long>(event.arguments[1]),
                    static_cast<unsigned long long>(event.arguments[2]),
                    static_cast<unsigned long long>(event.arguments[3]));
                last_http_observation_sequence = event.sequence;
            }
            if (!worker_cpu_sampling_enabled && elapsed % 30 == 0) {
                const std::string workers =
                    runtime.bionic()->threads().DescribeThreads();
                std::printf("ISOMR_CANDY_WORKERS elapsed=%u %s\n", elapsed,
                            workers.c_str());
            }
            lock.lock();
        }
    });
    const bool bridge_ok = isomr::CandyEntryBridge::Run(
        &runtime, handles, options, &result, &error);
    {
        const std::lock_guard lock(watchdog_mutex);
        bridge_finished = true;
    }
    watchdog_changed.notify_all();
    watchdog.join();
    PrintCandyVorbisHostSnapshot(0, vorbis_host_mirror, "final");
    PrintCandyWebP103HostSnapshot(0, webp_103_host, "final");
    if (!bridge_ok) {
        if (runtime.bionic()->threads().process_exit_requested()) {
            std::printf(
                "ISOMR_CANDY_PROCESS_EXIT stage=entry result=guest-exit "
                "guest-code=%d calls=%zu mid-call\n",
                runtime.bionic()->threads().process_exit_code(),
                result.completed_calls);
            std::string cleanup_error;
            if (!Cleanup(&runtime, handles, &cleanup_error, true)) {
                ReportFailure("cleanup", cleanup_error);
                return 7;
            }
            std::puts("ISOMR_CANDY_ENTRY_PROBE stage=cleanup result=pass");
            std::fflush(nullptr);
            ExitProcess(0);
        }
        ReportFailure("entry", error);
        std::string cleanup_error;
        if (!Cleanup(&runtime, handles, &cleanup_error))
            ReportFailure("cleanup", cleanup_error);
        return 7;
    }
    if (result.termination == isomr::CandyEntryTermination::kGuestStopped) {
        error =
            "CandyEntryBridge step/Java_com_king_core_NativeApplication_step: returned stop-requested";
        ReportFailure("entry", error);
        std::string cleanup_error;
        if (!Cleanup(&runtime, handles, &cleanup_error))
            ReportFailure("cleanup", cleanup_error);
        return 7;
    }
    if (result.termination ==
        isomr::CandyEntryTermination::kGuestProcessExit) {
        const isomr::BionicThreads& guest_threads =
            runtime.bionic()->threads();
        std::printf(
            "ISOMR_CANDY_PROCESS_EXIT stage=entry result=guest-exit "
            "guest-code=%d calls=%zu pointer-events=%zu\n",
            guest_threads.process_exit_code(), result.completed_calls,
            result.pointer_event_count);
        std::string cleanup_error;
        if (!Cleanup(&runtime, handles, &cleanup_error, true)) {
            ReportFailure("cleanup", cleanup_error);
            return 7;
        }
        std::puts("ISOMR_CANDY_ENTRY_PROBE stage=cleanup result=pass");
        std::fflush(nullptr);
        // A worker abandoned by guest _exit can hold CRT/DLL locks that hang
        // a normal return-path exit; teardown is complete, so end directly.
        ExitProcess(0);
    }
    if (result.termination != isomr::CandyEntryTermination::kWindowClosed) {
        error = "CandyEntryBridge interactive run ended without a Win32 close request";
        ReportFailure("entry", error);
        std::string cleanup_error;
        if (!Cleanup(&runtime, handles, &cleanup_error))
            ReportFailure("cleanup", cleanup_error);
        return 7;
    }
    const isomr::NativeGles& gles = runtime.bionic()->gles();
    if (!HasMeaningfulGuestEvidence(
            result.completed_calls, result.step_keep_running,
            gles.draw_call_count(), gles.present_count(),
            gles.lit_present_count(), gles.lit_pixel_observed(),
            result.pointer_event_count)) {
        error =
            "interactive session closed without meaningful guest "
            "step/draw/lit-presentation/input evidence";
        ReportFailure("entry-evidence", error);
        std::string cleanup_error;
        if (!Cleanup(&runtime, handles, &cleanup_error, true)) {
            ReportFailure("cleanup", cleanup_error);
        } else {
            std::puts("ISOMR_CANDY_ENTRY_PROBE stage=cleanup result=pass");
        }
        return 7;
    }
    std::printf(
        "ISOMR_CANDY_ENTRY_PROBE stage=entry result=pass calls=%zu "
        "termination=window-close pointer-events=%zu "
        "draws=%zu presents=%zu lit-presents=%zu lit=%s "
        "framebuffer=%u sampled=%zu lit-pixels=%zu "
        "alpha-pixels=%zu max-rgb=%u\n",
        result.completed_calls, result.pointer_event_count,
        gles.draw_call_count(), gles.present_count(),
        gles.lit_present_count(),
        gles.lit_pixel_observed() ? "yes" : "no",
        gles.bound_framebuffer(), gles.last_sampled_pixel_count(),
        gles.last_lit_pixel_count(),
        gles.last_nonzero_alpha_pixel_count(), gles.last_max_rgb());
    GLint current_program = 0;
    GLint viewport[4]{};
    GLint scissor_box[4]{};
    GLint active_texture = 0;
    GLint bound_texture = 0;
    GLboolean color_mask[4]{};
    GLboolean depth_mask = GL_FALSE;
    glGetIntegerv(GL_CURRENT_PROGRAM, &current_program);
    glGetIntegerv(GL_VIEWPORT, viewport);
    glGetIntegerv(GL_SCISSOR_BOX, scissor_box);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &active_texture);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &bound_texture);
    glGetBooleanv(GL_COLOR_WRITEMASK, color_mask);
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depth_mask);
    const GLboolean blend = glIsEnabled(GL_BLEND);
    const GLboolean depth_test = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean cull_face = glIsEnabled(GL_CULL_FACE);
    const GLboolean scissor_test = glIsEnabled(GL_SCISSOR_TEST);
    const GLenum diagnostic_error = glGetError();
    std::printf(
        "ISOMR_CANDY_DRAW_STATE program=%d viewport=%d,%d,%d,%d "
        "scissor=%d,%d,%d,%d color-mask=%u,%u,%u,%u depth-mask=%u "
        "blend=%u depth-test=%u cull=%u scissor-test=%u "
        "active-texture=%d texture2d=%d gl-error=%u\n",
        current_program, viewport[0], viewport[1], viewport[2], viewport[3],
        scissor_box[0], scissor_box[1], scissor_box[2], scissor_box[3],
        static_cast<unsigned int>(color_mask[0]),
        static_cast<unsigned int>(color_mask[1]),
        static_cast<unsigned int>(color_mask[2]),
        static_cast<unsigned int>(color_mask[3]),
        static_cast<unsigned int>(depth_mask),
        static_cast<unsigned int>(blend),
        static_cast<unsigned int>(depth_test),
        static_cast<unsigned int>(cull_face),
        static_cast<unsigned int>(scissor_test), active_texture, bound_texture,
        static_cast<unsigned int>(diagnostic_error));
    if (!Cleanup(&runtime, handles, &error, true)) {
        ReportFailure("cleanup", error);
        return 8;
    }
    std::puts("ISOMR_CANDY_ENTRY_PROBE stage=cleanup result=pass");
    std::puts("ISOMR_CANDY_ENTRY_PROBE=pass");
    return 0;
}
