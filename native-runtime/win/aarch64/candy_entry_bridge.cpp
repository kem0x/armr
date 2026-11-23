#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>

#include "candy_entry_bridge.h"

#include "aarch64_profile_runtime.h"
#include "bionic_runtime.h"
#include "bionic_threads.h"
#include "candy_rapidjson_host.h"
#include "native_gles.h"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace isomr {

std::string FormatSvcImportSummary(
    const Aarch64GuestCallTelemetry& telemetry) {
    std::vector<const Aarch64SvcImportTelemetry*> ordered;
    ordered.reserve(telemetry.svc_imports.size());
    for (const Aarch64SvcImportTelemetry& entry :
         telemetry.svc_imports.entries()) {
        if (entry.invocations != 0) ordered.push_back(&entry);
    }
    std::sort(ordered.begin(), ordered.end(), [](const auto* left,
                                                 const auto* right) {
        if (left->wall_time_ns != right->wall_time_ns)
            return left->wall_time_ns > right->wall_time_ns;
        return left->name < right->name;
    });
    const auto sanitize = [](std::string_view name) {
        static constexpr char kHex[] = "0123456789ABCDEF";
        std::string result;
        for (const unsigned char byte : name) {
            const bool ascii_alphanumeric =
                (byte >= 'a' && byte <= 'z') ||
                (byte >= 'A' && byte <= 'Z') ||
                (byte >= '0' && byte <= '9');
            if (ascii_alphanumeric || byte == '_' || byte == '.' ||
                byte == '-') {
                result.push_back(static_cast<char>(byte));
            } else {
                result.push_back('%');
                result.push_back(kHex[byte >> 4]);
                result.push_back(kHex[byte & 15U]);
            }
        }
        return result.empty() ? std::string("-") : result;
    };
    std::string result;
    std::uint64_t ranked_count = 0;
    std::uint64_t ranked_wall = 0;
    const std::size_t rank_count = std::min<std::size_t>(8, ordered.size());
    for (std::size_t index = 0; index < rank_count; ++index) {
        const Aarch64SvcImportTelemetry& entry = *ordered[index];
        const std::string prefix = " rank" + std::to_string(index + 1);
        result += prefix + "-name=" + sanitize(entry.name) +
                  prefix + "-count=" + std::to_string(entry.invocations) +
                  prefix + "-total-wall-ns=" +
                  std::to_string(entry.wall_time_ns) +
                  prefix + "-max-wall-ns=" +
                  std::to_string(entry.max_wall_time_ns) +
                  prefix + "-over-1ms-count=" +
                  std::to_string(entry.over_1ms_invocations) +
                  prefix + "-over-1ms-wall-ns=" +
                  std::to_string(entry.over_1ms_wall_time_ns);
        ranked_count += entry.invocations;
        ranked_wall += entry.wall_time_ns;
    }
    result += " residual-count=" +
              std::to_string(telemetry.svc_invocations - ranked_count) +
              " residual-wall-ns=" +
              std::to_string(telemetry.svc_wall_time_ns - ranked_wall);
    return result;
}

std::string FormatClockCallSummary(
    std::size_t ordinal, std::string_view stage, bool succeeded,
    const Aarch64GuestCallTelemetry& telemetry) {
    if (!telemetry.enabled ||
        telemetry.mode != Aarch64CallTelemetryMode::kSvcCategory)
        return {};
    const auto find = [&](std::string_view name)
        -> const Aarch64SvcImportTelemetry* {
        for (const Aarch64SvcImportTelemetry& entry :
             telemetry.svc_imports.entries()) {
            if (entry.name == name) return &entry;
        }
        return nullptr;
    };
    const auto count = [](const Aarch64SvcImportTelemetry* entry) {
        return entry ? entry->invocations : 0;
    };
    const auto wall = [](const Aarch64SvcImportTelemetry* entry) {
        return entry ? entry->wall_time_ns : 0;
    };
    const Aarch64SvcImportTelemetry* clock_gettime =
        find("clock_gettime");
    const Aarch64SvcImportTelemetry* clock_getres = find("clock_getres");
    const Aarch64SvcImportTelemetry* gettimeofday = find("gettimeofday");
    const auto& clock_ids = telemetry.clock_gettime_clock_ids;
    const std::uint64_t realtime_count =
        clock_ids[0] + clock_ids[5] + clock_ids[8];
    const std::uint64_t monotonic_count =
        clock_ids[1] + clock_ids[4] + clock_ids[6];
    const std::uint64_t boottime_count = clock_ids[7] + clock_ids[9];
    return "ISOMR_AARCH64_CALL_CLOCK ordinal=" +
           std::to_string(ordinal) + " stage=" + std::string(stage) +
           " result=" + (succeeded ? "success" : "failure") +
           " call-wall-ns=" + std::to_string(telemetry.wall_time_ns) +
           " clock-gettime-count=" +
           std::to_string(count(clock_gettime)) +
           " clock-gettime-wall-ns=" +
           std::to_string(wall(clock_gettime)) +
           " clock-getres-count=" + std::to_string(count(clock_getres)) +
           " clock-getres-wall-ns=" + std::to_string(wall(clock_getres)) +
           " gettimeofday-count=" +
           std::to_string(count(gettimeofday)) +
           " gettimeofday-wall-ns=" +
           std::to_string(wall(gettimeofday)) +
           " clock-realtime-count=" + std::to_string(realtime_count) +
           " clock-monotonic-count=" + std::to_string(monotonic_count) +
           " clock-process-cpu-count=" + std::to_string(clock_ids[2]) +
           " clock-thread-cpu-count=" + std::to_string(clock_ids[3]) +
           " clock-boottime-count=" + std::to_string(boottime_count) +
           " clock-invalid-count=" +
           std::to_string(telemetry.clock_gettime_unknown_clock_ids);
}

std::string FormatGuestPcSummary(
    const Aarch64GuestCallTelemetry& telemetry) {
    if (!telemetry.enabled ||
        telemetry.mode != Aarch64CallTelemetryMode::kFull)
        return {};
    std::vector<const Aarch64GuestPcTelemetry*> ordered;
    if (telemetry.guest_pc_histogram) {
        for (const Aarch64GuestPcTelemetry& entry :
             *telemetry.guest_pc_histogram) {
            if (entry.samples != 0) ordered.push_back(&entry);
        }
    }
    std::sort(ordered.begin(), ordered.end(), [](const auto* left,
                                                 const auto* right) {
        if (left->samples != right->samples)
            return left->samples > right->samples;
        return left->program_counter < right->program_counter;
    });
    std::string result =
        " total-samples=" + std::to_string(telemetry.guest_pc_samples) +
        " retained=" + std::to_string(ordered.size());
    const std::size_t rank_count = std::min<std::size_t>(8, ordered.size());
    for (std::size_t index = 0; index < rank_count; ++index) {
        const Aarch64GuestPcTelemetry& entry = *ordered[index];
        const std::string prefix = " rank" + std::to_string(index + 1);
        char address[32]{};
        char link[32]{};
        char parent_link[32]{};
        std::snprintf(address, sizeof(address), "0x%llx",
                      static_cast<unsigned long long>(entry.program_counter));
        std::snprintf(link, sizeof(link), "0x%llx",
                      static_cast<unsigned long long>(entry.link_register));
        std::snprintf(
            parent_link, sizeof(parent_link), "0x%llx",
            static_cast<unsigned long long>(entry.parent_link_register));
        result += prefix + "-pc=" + address +
                  prefix + "-lr=" + link +
                  prefix + "-parent-lr=" + parent_link +
                  prefix + "-x0-min=" +
                      std::to_string(entry.register_0_min) +
                  prefix + "-x0-max=" +
                      std::to_string(entry.register_0_max) +
                  prefix + "-x1-min=" +
                      std::to_string(entry.register_1_min) +
                  prefix + "-x1-max=" +
                      std::to_string(entry.register_1_max) +
                  prefix + "-x2-min=" +
                      std::to_string(entry.register_2_min) +
                  prefix + "-x2-max=" +
                      std::to_string(entry.register_2_max) +
                  prefix + "-x3-min=" +
                      std::to_string(entry.register_3_min) +
                  prefix + "-x3-max=" +
                      std::to_string(entry.register_3_max) +
                  prefix + "-x8-min=" +
                      std::to_string(entry.register_8_min) +
                  prefix + "-x8-max=" +
                      std::to_string(entry.register_8_max) +
                  prefix + "-x10-min=" +
                      std::to_string(entry.register_10_min) +
                  prefix + "-x10-max=" +
                      std::to_string(entry.register_10_max) +
                  prefix + "-x11-min=" +
                      std::to_string(entry.register_11_min) +
                  prefix + "-x11-max=" +
                      std::to_string(entry.register_11_max) +
                  prefix + "-x19-min=" +
                      std::to_string(entry.register_19_min) +
                  prefix + "-x19-max=" +
                      std::to_string(entry.register_19_max) +
                  prefix + "-x22-min=" +
                      std::to_string(entry.register_22_min) +
                  prefix + "-x22-max=" +
                      std::to_string(entry.register_22_max) +
                  prefix + "-x24-min=" +
                      std::to_string(entry.register_24_min) +
                  prefix + "-x24-max=" +
                      std::to_string(entry.register_24_max) +
                  prefix + "-count=" + std::to_string(entry.samples) +
                  prefix + "-error=" + std::to_string(entry.error);
    }
    return result;
}

std::string FormatNanosleepCallSummary(
    std::size_t ordinal, std::string_view stage, bool succeeded,
    const Aarch64GuestCallTelemetry& telemetry) {
    if (!telemetry.enabled ||
        telemetry.mode != Aarch64CallTelemetryMode::kSvcCategory)
        return {};
    for (const Aarch64SvcImportTelemetry& entry :
         telemetry.svc_imports.entries()) {
        if (entry.name != "nanosleep" ||
            entry.successful_nanosleep_requests == 0 ||
            !entry.nanosleep_requested_accumulation_valid)
            continue;
        return "ISOMR_AARCH64_CALL_NANOSLEEP ordinal=" +
               std::to_string(ordinal) + " stage=" +
               std::string(stage) + " result=" +
               (succeeded ? "success" : "failure") + " handler-count=" +
               std::to_string(entry.invocations) +
               " handler-total-wall-ns=" +
               std::to_string(entry.wall_time_ns) +
               " handler-max-wall-ns=" +
               std::to_string(entry.max_wall_time_ns) +
               " successful-requested-count=" +
               std::to_string(entry.successful_nanosleep_requests) +
               " requested-total-ns=" +
               std::to_string(entry.nanosleep_requested_total_ns) +
               " requested-max-ns=" +
               std::to_string(entry.nanosleep_requested_max_ns);
    }
    return {};
}

namespace {

constexpr std::string_view kCreateNativeInstance =
    "Java_com_king_candycrushsaga_PlatformProxy_createNativeInstance";
constexpr std::string_view kCreate =
    "Java_com_king_core_NativeApplication_create";
constexpr std::string_view kSystemEvent =
    "Java_com_king_core_NativeApplication_onSystemEvent";
constexpr std::string_view kSetAppShuttingDown =
    "Java_com_king_core_NativeApplication_setAppShuttingDownFlag";
constexpr std::string_view kDestroy =
    "Java_com_king_core_NativeApplication_destroy";
constexpr std::string_view kInitialize =
    "Java_com_king_core_NativeApplication_init";
constexpr std::string_view kDisplayScale =
    "Java_com_king_core_NativeApplication_updateDisplayResolutionScale";
constexpr std::string_view kScreenSize =
    "Java_com_king_core_NativeApplication_updateScreenSize";
constexpr std::string_view kStep =
    "Java_com_king_core_NativeApplication_step";
constexpr std::string_view kTouch =
    "Java_com_king_core_NativeApplication_onTouchEvent";
constexpr std::string_view kAnalyticsCompletion =
    "Java_com_king_usdk_kdid_AnalyticsReceiver_HandleRequestResponse";
constexpr std::string_view kAgeSignalsErrorCompletion =
    "Java_com_king_usdk_privacyagecompliance_UsdkAgeSignals_"
    "nativeOnAgeSignalsError";
constexpr std::int32_t kAgeSignalsUnavailableError = -100;
constexpr std::string_view kAgeSignalsUnavailableMessage =
    "Google Play Age Signals is unavailable on native Windows";
constexpr std::string_view kPopupDialogDismissed =
    "Java_com_king_usdk_popupguard_internal_PopupDialog_onDialogDismissed";
constexpr std::uint64_t kOwnerMessagePumpSliceInstructionBudget = 250000ULL;
constexpr std::chrono::milliseconds kLongCallPcThreshold{250};
constexpr std::uint64_t kLongCallPcMinimumSliceInstructionBudget =
    kOwnerMessagePumpSliceInstructionBudget - 65536ULL;
constexpr std::uint32_t kLongCallPcSliceInstructionBudgetMask = 0x1ffffU;

std::uint32_t AdvanceLongCallPcSampler(std::uint32_t state) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

class MainInterSliceCallbackScope {
public:
    explicit MainInterSliceCallbackScope(Aarch64ProfileRuntime* runtime)
        : runtime_(runtime) {}
    ~MainInterSliceCallbackScope() {
        if (active_ && runtime_ != nullptr)
            runtime_->ClearMainOwnerThreadInterSliceCallback();
    }
    void Activate() { active_ = true; }

private:
    Aarch64ProfileRuntime* runtime_ = nullptr;
    bool active_ = false;
};

class NanosleepCallsiteCallScope {
public:
    explicit NanosleepCallsiteCallScope(Aarch64ProfileRuntime* runtime)
        : runtime_(runtime),
          token_(runtime == nullptr
                     ? NanosleepCallsiteTelemetryToken{}
                     : runtime->BeginNanosleepCallsiteTelemetry()) {}
    ~NanosleepCallsiteCallScope() {
        if (token_.active && runtime_ != nullptr)
            (void)runtime_->EndNanosleepCallsiteTelemetry(token_);
    }
    NanosleepCallsiteTelemetrySnapshot Close() noexcept {
        if (!token_.active || runtime_ == nullptr) return {};
        const NanosleepCallsiteTelemetrySnapshot snapshot =
            runtime_->EndNanosleepCallsiteTelemetry(token_);
        token_.active = false;
        return snapshot;
    }

private:
    Aarch64ProfileRuntime* runtime_ = nullptr;
    NanosleepCallsiteTelemetryToken token_{};
};

struct ResolvedNanosleepBucket {
    const NanosleepCallsiteTelemetryBucket* bucket = nullptr;
    Aarch64ProfileRuntime::ResolvedGuestCodeAddress caller;
    std::optional<Aarch64ProfileRuntime::ResolvedGuestCodeAddress> parent;
};

void AddResidual(const NanosleepCallsiteTelemetryBucket& source,
                 NanosleepCallsiteTelemetryBucket* target) noexcept {
    if (target == nullptr) return;
    constexpr std::uint64_t kMaximum =
        (std::numeric_limits<std::uint64_t>::max)();
    const auto add = [kMaximum](std::uint64_t value,
                               std::uint64_t* total) {
        *total = value > kMaximum - *total ? kMaximum : *total + value;
    };
    add(source.success_count, &target->success_count);
    add(source.requested_total_ns, &target->requested_total_ns);
    target->requested_max_ns =
        (std::max)(target->requested_max_ns, source.requested_max_ns);
    add(source.handler_body_total_ns, &target->handler_body_total_ns);
    target->handler_body_max_ns = (std::max)(
        target->handler_body_max_ns, source.handler_body_max_ns);
}

std::string HexOffset(std::uint64_t offset) {
    char value[19]{};
    const int length = std::snprintf(
        value, sizeof(value), "0x%llx",
        static_cast<unsigned long long>(offset));
    return length > 0 && static_cast<std::size_t>(length) < sizeof(value)
               ? std::string(value, static_cast<std::size_t>(length))
               : std::string("-");
}

std::string SanitizeTelemetryToken(std::string_view value) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string result;
    for (const unsigned char byte : value) {
        const bool ascii_alphanumeric =
            (byte >= 'a' && byte <= 'z') ||
            (byte >= 'A' && byte <= 'Z') ||
            (byte >= '0' && byte <= '9');
        if (ascii_alphanumeric || byte == '_' || byte == '.' ||
            byte == '-') {
            result.push_back(static_cast<char>(byte));
        } else {
            result.push_back('%');
            result.push_back(kHex[byte >> 4]);
            result.push_back(kHex[byte & 15U]);
        }
    }
    return result.empty() ? std::string("-") : result;
}

std::string FormatResolvedGuestCodeAddress(Aarch64ProfileRuntime* runtime,
                                           std::uint64_t address) {
    if (runtime == nullptr || address == 0) return "-";
    const auto resolved = runtime->ResolveGuestCodeAddress(address);
    if (!resolved) return "unresolved";
    return SanitizeTelemetryToken(resolved->module) + "+" +
           HexOffset(resolved->offset);
}

std::string FormatLongCallPcSummary(
    Aarch64ProfileRuntime* runtime, std::uint64_t step,
    const Aarch64GuestCallTelemetry& telemetry) {
    struct RegionBucket {
        std::string address;
        std::uint64_t samples = 0;
        std::uint64_t error = 0;
    };
    constexpr std::uint64_t kRegionSize = 64U * 1024U;
    std::vector<const Aarch64GuestPcTelemetry*> ordered;
    std::vector<RegionBucket> regions;
    std::uint64_t unresolved_region_samples = 0;
    std::uint64_t unresolved_region_error = 0;
    if (telemetry.guest_pc_histogram) {
        for (const Aarch64GuestPcTelemetry& entry :
             *telemetry.guest_pc_histogram) {
            if (entry.samples == 0) continue;
            ordered.push_back(&entry);
            std::optional<
                Aarch64ProfileRuntime::ResolvedGuestCodeAddress>
                resolved;
            if (runtime != nullptr) {
                resolved = runtime->ResolveGuestCodeAddress(
                    entry.program_counter);
            }
            if (!resolved) {
                unresolved_region_samples += entry.samples;
                unresolved_region_error += entry.error;
                continue;
            }
            const std::uint64_t region_offset =
                resolved->offset & ~(kRegionSize - 1);
            const std::string region_address =
                SanitizeTelemetryToken(resolved->module) + "+" +
                HexOffset(region_offset);
            const auto existing = std::find_if(
                regions.begin(), regions.end(),
                [&](const RegionBucket& region) {
                    return region.address == region_address;
                });
            if (existing != regions.end()) {
                existing->samples += entry.samples;
                existing->error += entry.error;
            } else {
                regions.push_back(
                    {region_address, entry.samples, entry.error});
            }
        }
    }
    std::sort(ordered.begin(), ordered.end(), [](const auto* left,
                                                 const auto* right) {
        if (left->samples != right->samples)
            return left->samples > right->samples;
        return left->program_counter < right->program_counter;
    });
    std::sort(regions.begin(), regions.end(), [](const RegionBucket& left,
                                                 const RegionBucket& right) {
        if (left.samples != right.samples)
            return left.samples > right.samples;
        return left.address < right.address;
    });
    std::string output =
        "ISOMR_CANDY_LONG_CALL_PC step=" + std::to_string(step) +
        " wall-threshold-ns=" +
        std::to_string(std::chrono::duration_cast<std::chrono::nanoseconds>(
                           kLongCallPcThreshold)
                           .count()) +
        " total-samples=" + std::to_string(telemetry.guest_pc_samples) +
        " retained=" + std::to_string(ordered.size()) +
        " region-size=" + std::to_string(kRegionSize) +
        " region-retained=" + std::to_string(regions.size()) +
        " region-unresolved-count=" +
            std::to_string(unresolved_region_samples) +
        " region-unresolved-error=" +
            std::to_string(unresolved_region_error);
    const std::size_t rank_count =
        std::min<std::size_t>(8, ordered.size());
    for (std::size_t index = 0; index < rank_count; ++index) {
        const Aarch64GuestPcTelemetry& entry = *ordered[index];
        const std::string prefix = " rank" + std::to_string(index + 1);
        output += prefix + "-pc=" +
                  FormatResolvedGuestCodeAddress(
                      runtime, entry.program_counter) +
                  prefix + "-lr=" +
                  FormatResolvedGuestCodeAddress(
                      runtime, entry.link_register) +
                  prefix + "-parent-lr=" +
                  FormatResolvedGuestCodeAddress(
                      runtime, entry.parent_link_register) +
                  prefix + "-x0-min=" +
                      std::to_string(entry.register_0_min) +
                  prefix + "-x0-max=" +
                      std::to_string(entry.register_0_max) +
                  prefix + "-x1-min=" +
                      std::to_string(entry.register_1_min) +
                  prefix + "-x1-max=" +
                      std::to_string(entry.register_1_max) +
                  prefix + "-count=" + std::to_string(entry.samples) +
                  prefix + "-error=" + std::to_string(entry.error);
    }
    const std::size_t region_rank_count =
        std::min<std::size_t>(8, regions.size());
    for (std::size_t index = 0; index < region_rank_count; ++index) {
        const RegionBucket& region = regions[index];
        const std::string prefix =
            " region" + std::to_string(index + 1);
        output += prefix + "=" + region.address +
                  prefix + "-count=" +
                      std::to_string(region.samples) +
                  prefix + "-error=" + std::to_string(region.error);
    }
    output += " addresses=module-relative-and-register-values-local-only";
    constexpr std::size_t kMaximumSummarySize = 8192;
    if (output.size() > kMaximumSummarySize)
        throw std::length_error(
            "input-blocked PC summary exceeds bound");
    return output;
}

void ResetGuestPcTelemetry(Aarch64GuestCallTelemetry* telemetry) {
    if (telemetry == nullptr) return;
    telemetry->guest_pc_samples = 0;
    if (telemetry->guest_pc_histogram)
        telemetry->guest_pc_histogram->fill({});
}

std::string FormatNanosleepCallsiteSummary(
    Aarch64ProfileRuntime* runtime, std::size_t ordinal,
    std::string_view stage, bool succeeded,
    const NanosleepCallsiteTelemetrySnapshot& snapshot) {
    if (!snapshot.enabled || runtime == nullptr) return {};
    std::vector<ResolvedNanosleepBucket> resolved;
    resolved.reserve(snapshot.retained_count);
    NanosleepCallsiteTelemetryBucket residual = snapshot.residual;
    std::uint64_t unresolved_count = 0;
    std::uint64_t parent_unresolved_count = 0;
    const std::uint64_t maximum =
        (std::numeric_limits<std::uint64_t>::max)();
    const auto add_count = [maximum](std::uint64_t value,
                                     std::uint64_t* total) {
        *total = value > maximum - *total ? maximum : *total + value;
    };
    for (std::size_t index = 0; index < snapshot.retained_count; ++index) {
        const NanosleepCallsiteTelemetryBucket& bucket =
            snapshot.retained[index];
        std::optional<Aarch64ProfileRuntime::ResolvedGuestCodeAddress>
            parent;
        if (bucket.parent_valid) {
            parent = runtime->ResolveGuestCodeAddress(
                bucket.parent_guest_link_register);
        }
        if (!parent) {
            add_count(bucket.success_count, &parent_unresolved_count);
        }
        const auto caller = runtime->ResolveGuestCodeAddress(
            bucket.guest_link_register);
        if (!caller) {
            AddResidual(bucket, &residual);
            add_count(bucket.success_count, &unresolved_count);
            continue;
        }
        resolved.push_back({&bucket, *caller, std::move(parent)});
    }
    std::sort(resolved.begin(), resolved.end(),
              [](const ResolvedNanosleepBucket& left,
                 const ResolvedNanosleepBucket& right) {
                  if (left.bucket->success_count !=
                      right.bucket->success_count)
                      return left.bucket->success_count >
                             right.bucket->success_count;
                  if (left.caller.module != right.caller.module)
                      return left.caller.module < right.caller.module;
                  if (left.caller.offset != right.caller.offset)
                      return left.caller.offset < right.caller.offset;
                  if (left.parent.has_value() != right.parent.has_value())
                      return left.parent.has_value();
                  if (left.parent &&
                      left.parent->module != right.parent->module)
                      return left.parent->module < right.parent->module;
                  if (left.parent &&
                      left.parent->offset != right.parent->offset)
                      return left.parent->offset < right.parent->offset;
                  return left.bucket->logical_thread_id <
                         right.bucket->logical_thread_id;
              });
    const std::size_t rank_count =
        std::min<std::size_t>(8, resolved.size());
    for (std::size_t index = rank_count; index < resolved.size(); ++index)
        AddResidual(*resolved[index].bucket, &residual);

    std::string output =
        "ISOMR_NANOSLEEP_CALLSITE ordinal=" + std::to_string(ordinal) +
        " stage=" + std::string(stage) + " result=" +
        (succeeded ? "success" : "failure") + " complete=" +
        (snapshot.complete ? "1" : "0") + " table-overflow=" +
        (snapshot.overflow.table ? "1" : "0") + " count-overflow=" +
        (snapshot.overflow.count ? "1" : "0") +
        " admission-failure=" +
        (snapshot.overflow.admission ? "1" : "0") +
        " requested-total-overflow=" +
        (snapshot.overflow.requested_total ? "1" : "0") +
        " handler-body-total-overflow=" +
        (snapshot.overflow.handler_body_total ? "1" : "0") +
        " unresolved-count=" + std::to_string(unresolved_count) +
        " parent-unresolved-count=" +
        std::to_string(parent_unresolved_count) +
        " successful-count=" +
        std::to_string(snapshot.aggregate.success_count) +
        " requested-total-ns=" +
        std::to_string(snapshot.aggregate.requested_total_ns) +
        " requested-max-ns=" +
        std::to_string(snapshot.aggregate.requested_max_ns) +
        " handler-body-total-ns=" +
        std::to_string(snapshot.aggregate.handler_body_total_ns) +
        " handler-body-max-ns=" +
        std::to_string(snapshot.aggregate.handler_body_max_ns);
    for (std::size_t index = 0; index < rank_count; ++index) {
        const ResolvedNanosleepBucket& entry = resolved[index];
        const std::string prefix =
            " rank" + std::to_string(index + 1);
        output += prefix + "-immediate-module=" + entry.caller.module +
                  prefix + "-immediate-offset=" +
                  HexOffset(entry.caller.offset);
        if (entry.parent) {
            output += prefix + "-parent-module=" +
                      entry.parent->module + prefix + "-parent-offset=" +
                      HexOffset(entry.parent->offset);
        } else {
            output += prefix + "-parent-unresolved=1";
        }
        output += prefix +
                  "-thread=" +
                  std::to_string(entry.bucket->logical_thread_id) + prefix +
                  "-count=" +
                  std::to_string(entry.bucket->success_count) + prefix +
                  "-requested-total-ns=" +
                  std::to_string(entry.bucket->requested_total_ns) + prefix +
                  "-requested-max-ns=" +
                  std::to_string(entry.bucket->requested_max_ns) + prefix +
                  "-handler-body-total-ns=" +
                  std::to_string(entry.bucket->handler_body_total_ns) +
                  prefix + "-handler-body-max-ns=" +
                  std::to_string(entry.bucket->handler_body_max_ns);
    }
    output += " residual-count=" +
              std::to_string(residual.success_count) +
              " residual-requested-total-ns=" +
              std::to_string(residual.requested_total_ns) +
              " residual-requested-max-ns=" +
              std::to_string(residual.requested_max_ns) +
              " residual-handler-body-total-ns=" +
              std::to_string(residual.handler_body_total_ns) +
              " residual-handler-body-max-ns=" +
              std::to_string(residual.handler_body_max_ns);
    constexpr std::size_t kMaximumSummarySize = 8192;
    if (output.size() > kMaximumSummarySize)
        throw std::length_error("nanosleep callsite summary exceeds bound");
    return output;
}

void SetError(std::string* error, std::string message) {
    if (error != nullptr) *error = std::move(message);
}

class CandyBulkZeroStreamScope {
public:
    explicit CandyBulkZeroStreamScope(Aarch64ProfileRuntime* runtime)
        : runtime_(runtime) {}

    ~CandyBulkZeroStreamScope() { Close(); }

    bool Configure(std::string* error) {
        constexpr std::uint64_t kLoopSetupOffset = 0x456f964ULL;
        constexpr std::uint64_t kBreakpointDelta = 0x0cULL;
        constexpr std::uint64_t kDecrementDelta = 0x18ULL;
        constexpr std::uint64_t kFirstCallerReturnOffset = 0x456f798ULL;
        constexpr std::uint64_t kSecondCallerReturnOffset = 0x456f7b4ULL;
        constexpr std::uint64_t kThirdCallerReturnOffset = 0x456fae8ULL;
        constexpr std::uint64_t kFourthCallerReturnOffset = 0x456fb04ULL;
        constexpr std::uint64_t kBulkWriterOffset = 0x208a488ULL;
        constexpr std::uint64_t kMaximumChunk = 2ULL * 1024ULL * 1024ULL;
        constexpr std::array<std::uint32_t, 8> kLoopSignature{
            0x390033ffU, 0xa9017ff9U, 0x340000d3U, 0x910043e0U,
            0x910033e1U, 0x940101beU, 0x71000673U, 0x54ffff81U};
        constexpr std::array<std::uint32_t, 8> kBulkWriterSignature{
            0xd101c3ffU, 0xa9027bfdU, 0xa90367faU, 0xa9045ff8U,
            0xa90557f6U, 0xa9064ff4U, 0x910083fdU, 0xd53bd05aU};
        constexpr std::array<std::uint32_t, 2> kFirstCallerReturnSignature{
            0xaa1303e0U, 0xaa1603e1U};
        constexpr std::array<std::uint32_t, 2> kSecondCallerReturnSignature{
            0x2a0003f4U, 0x91056260U};
        constexpr std::array<std::uint32_t, 2> kThirdCallerReturnSignature{
            0x910ae261U, 0xaa1303e0U};
        constexpr std::array<std::uint32_t, 2> kFourthCallerReturnSignature{
            0x910b4261U, 0xaa1403e0U};
        if (runtime_ == nullptr || runtime_->bionic() == nullptr ||
            active_) {
            SetError(error,
                     "Candy bulk zero-stream configuration is invalid");
            return false;
        }
        std::uint64_t loop_setup = 0;
        if (!runtime_->ResolveGuestModuleCodeOffset(
                {}, kLoopSetupOffset, kLoopSignature, &loop_setup, error) ||
            !runtime_->ResolveGuestModuleCodeOffset(
                {}, kBulkWriterOffset, kBulkWriterSignature,
                &bulk_writer_, error) ||
            !runtime_->ResolveGuestModuleCodeOffset(
                {}, kFirstCallerReturnOffset, kFirstCallerReturnSignature,
                &first_parent_, error) ||
            !runtime_->ResolveGuestModuleCodeOffset(
                {}, kSecondCallerReturnOffset, kSecondCallerReturnSignature,
                &second_parent_, error) ||
            !runtime_->ResolveGuestModuleCodeOffset(
                {}, kThirdCallerReturnOffset, kThirdCallerReturnSignature,
                &third_parent_, error) ||
            !runtime_->ResolveGuestModuleCodeOffset(
                {}, kFourthCallerReturnOffset, kFourthCallerReturnSignature,
                &fourth_parent_, error)) {
            return false;
        }
        breakpoint_ = loop_setup + kBreakpointDelta;
        decrement_ = loop_setup + kDecrementDelta;
        first_entry_link_ = loop_setup;
        maximum_chunk_ = kMaximumChunk;
        GuestHeap& heap = runtime_->bionic()->heap();
        zero_range_ = heap.Callocate(1, maximum_chunk_);
        if (zero_range_ == 0) {
            SetError(error,
                     "Candy bulk zero-stream zero-range allocation failed: " +
                         heap.DescribeLastAllocationFailure());
            return false;
        }
        if (!heap.SetProtection(zero_range_, maximum_chunk_, true, false,
                                error)) {
            std::string ignored;
            (void)heap.Release(zero_range_, &ignored);
            zero_range_ = 0;
            return false;
        }
        if (!runtime_->AddMainOwnerThreadExecutionBreakpoint(
                breakpoint_,
                [this](Aarch64PausedExecutionState* state,
                       std::string* callback_error) {
                    return Redirect(state, callback_error);
                },
                error)) {
            std::string ignored;
            (void)heap.Release(zero_range_, &ignored);
            zero_range_ = 0;
            return false;
        }
        active_ = true;
        return true;
    }

private:
    bool Refuse(std::string_view reason, std::string* error) {
        ++refusals_;
        SetError(error,
                 "Candy bulk zero-stream paused-state refusal: " +
                     std::string(reason));
        return false;
    }

    bool Redirect(Aarch64PausedExecutionState* state, std::string* error) {
        if (!active_ || runtime_ == nullptr || runtime_->bionic() == nullptr ||
            state == nullptr || state->program_counter != breakpoint_)
            return Refuse("inactive or wrong breakpoint", error);
        GuestHeap& heap = runtime_->bionic()->heap();
        const std::uint64_t stack_pointer = state->stack_pointer;
        const std::uint64_t frame_pointer = state->registers[29];
        if ((stack_pointer & 15U) != 0 ||
            stack_pointer >
                (std::numeric_limits<std::uint64_t>::max)() - 0x190ULL ||
            frame_pointer != stack_pointer + 0x180ULL)
            return Refuse("stack frame", error);
        std::uint64_t stream = 0;
        std::uint64_t stream_state = 0;
        std::uint64_t parent_link = 0;
        std::uint8_t zero_byte = 1;
        if (!heap.Read(stack_pointer + 0x10ULL, &stream, sizeof(stream)) ||
            !heap.Read(stack_pointer + 0x18ULL, &stream_state,
                       sizeof(stream_state)) ||
            !heap.Read(stack_pointer + 0x0cULL, &zero_byte,
                       sizeof(zero_byte)) ||
            !heap.Read(frame_pointer + sizeof(std::uint64_t), &parent_link,
                       sizeof(parent_link)))
            return Refuse("stack state unreadable", error);
        const std::uint64_t remaining = state->registers[19];
        if (state->registers[22] >
            (std::numeric_limits<std::uint64_t>::max)() - 0x10ULL)
            return Refuse("stream base overflow", error);
        const std::uint64_t expected_stream = state->registers[22] + 0x10ULL;
        const bool link_valid = state->registers[30] == first_entry_link_ ||
                                state->registers[30] == decrement_;
        const bool parent_valid = parent_link == first_parent_ ||
                                  parent_link == second_parent_ ||
                                  parent_link == third_parent_ ||
                                  parent_link == fourth_parent_;
        const bool stream_matches_x25 = stream == state->registers[25];
        const bool stream_matches_x22 = stream == expected_stream;
        const bool stream_readable =
            heap.IsReadableRange(stream, sizeof(std::uint64_t));
        const bool zero_range_readable =
            heap.IsReadableRange(zero_range_, maximum_chunk_);
        const bool zero_range_writable =
            heap.IsWritableRange(zero_range_, maximum_chunk_);
        if (!link_valid || !parent_valid || remaining == 0 ||
            remaining > UINT32_MAX || zero_byte != 0 || stream_state != 0 ||
            !stream_matches_x25 || !stream_matches_x22 || !stream_readable ||
            !zero_range_readable || zero_range_writable) {
            const std::uint64_t module_base = breakpoint_ - 0x456f970ULL;
            const std::uint64_t link_offset =
                state->registers[30] >= module_base
                    ? state->registers[30] - module_base
                    : 0;
            const std::uint64_t parent_offset =
                parent_link >= module_base ? parent_link - module_base : 0;
            std::printf(
                "ISOMR_CANDY_BULK_ZERO_STREAM refusal=%llu remaining=%llu link-offset=0x%llx link-valid=%u parent-offset=0x%llx parent-valid=%u zero-byte=%u stream-state=%llu stream-x25=%u stream-x22=%u stream-readable=%u zero-readable=%u zero-writable=%u\n",
                static_cast<unsigned long long>(refusals_ + 1),
                static_cast<unsigned long long>(remaining),
                static_cast<unsigned long long>(link_offset),
                link_valid ? 1U : 0U,
                static_cast<unsigned long long>(parent_offset),
                parent_valid ? 1U : 0U,
                static_cast<unsigned int>(zero_byte),
                static_cast<unsigned long long>(stream_state),
                stream_matches_x25 ? 1U : 0U,
                stream_matches_x22 ? 1U : 0U,
                stream_readable ? 1U : 0U,
                zero_range_readable ? 1U : 0U,
                zero_range_writable ? 1U : 0U);
            return Refuse("register, caller, or stream contract", error);
        }
        const std::uint64_t chunk =
            (std::min)(remaining, maximum_chunk_);
        state->registers[0] = stream;
        state->registers[1] = zero_range_;
        state->registers[2] = chunk;
        // The retained guest decrement and branch consume the final byte of
        // this chunk and either continue with the next chunk or leave the
        // original loop exactly as before.
        state->registers[19] = remaining - (chunk - 1);
        state->registers[30] = decrement_;
        state->program_counter = bulk_writer_;
        ++actions_;
        bytes_ = chunk > (std::numeric_limits<std::uint64_t>::max)() - bytes_
                     ? (std::numeric_limits<std::uint64_t>::max)()
                     : bytes_ + chunk;
        std::printf(
            "ISOMR_CANDY_BULK_ZERO_STREAM action=%llu chunk=%llu remaining-before=%llu remaining-after=%llu\n",
            static_cast<unsigned long long>(actions_),
            static_cast<unsigned long long>(chunk),
            static_cast<unsigned long long>(remaining),
            static_cast<unsigned long long>(remaining - chunk));
        return true;
    }

    void Close() noexcept {
        if (runtime_ == nullptr) return;
        if (active_)
            runtime_->RemoveMainOwnerThreadExecutionBreakpoint(breakpoint_);
        bool released = true;
        if (zero_range_ != 0 && runtime_->bionic() != nullptr) {
            std::string release_error;
            released = runtime_->bionic()->heap().Release(
                zero_range_, &release_error);
            if (!released)
                std::fprintf(
                    stderr,
                    "ISOMR_CANDY_BULK_ZERO_STREAM cleanup=failed reason=%s\n",
                    release_error.c_str());
        }
        if (active_) {
            std::printf(
                "ISOMR_CANDY_BULK_ZERO_STREAM_RESULT actions=%llu bytes=%llu refusals=%llu cleanup=%s\n",
                static_cast<unsigned long long>(actions_),
                static_cast<unsigned long long>(bytes_),
                static_cast<unsigned long long>(refusals_),
                released ? "pass" : "failure");
        }
        active_ = false;
        zero_range_ = 0;
    }

    Aarch64ProfileRuntime* runtime_ = nullptr;
    std::uint64_t breakpoint_ = 0;
    std::uint64_t decrement_ = 0;
    std::uint64_t bulk_writer_ = 0;
    std::uint64_t first_entry_link_ = 0;
    std::uint64_t first_parent_ = 0;
    std::uint64_t second_parent_ = 0;
    std::uint64_t third_parent_ = 0;
    std::uint64_t fourth_parent_ = 0;
    std::uint64_t zero_range_ = 0;
    std::uint64_t maximum_chunk_ = 0;
    std::uint64_t actions_ = 0;
    std::uint64_t bytes_ = 0;
    std::uint64_t refusals_ = 0;
    bool active_ = false;
};

class CandyRapidJsonHostMirrorScope {
public:
    explicit CandyRapidJsonHostMirrorScope(Aarch64ProfileRuntime* runtime)
        : runtime_(runtime) {}

    ~CandyRapidJsonHostMirrorScope() { Close(); }

    bool Configure(std::string* error) {
        constexpr std::uint64_t kEntryOffset = 0x1f0a754ULL;
        constexpr std::array<std::uint32_t, 8> kEntrySignature{
            0xd10203ffU, 0xa9057bfdU, 0xf90033f5U, 0xa9074ff4U,
            0x910143fdU, 0xd53bd055U, 0x6f00e400U, 0x52802009U};
        if (runtime_ == nullptr || runtime_->bionic() == nullptr || active_) {
            SetError(error,
                     "Candy RapidJSON host mirror configuration is invalid");
            return false;
        }
        if (!runtime_->ResolveGuestModuleCodeOffset(
                {}, kEntryOffset, kEntrySignature, &entry_, error)) {
            return false;
        }
        module_base_ = entry_ - kEntryOffset;
        if (!runtime_->AddMainOwnerThreadExecutionBreakpoint(
                entry_,
                [this](Aarch64PausedExecutionState* state,
                       std::string* callback_error) {
                    return Mirror(state, callback_error);
                },
                error)) {
            return false;
        }
        active_ = true;
        return true;
    }

private:
    static constexpr std::uint64_t kStackFrameSize = 0x80ULL;
    static constexpr std::uint64_t kMinimumInputBytes = 4096ULL;
    static constexpr std::uint64_t kMaximumInputBytes =
        64ULL * 1024ULL * 1024ULL;

    static bool ReadInput(const GuestAddressSpace& memory,
                          std::uint64_t stream, std::string* input) {
        if (input == nullptr) return false;
        input->clear();
        std::uint64_t input_address = 0;
        if (stream == 0 ||
            !memory.Read(stream, &input_address, sizeof(input_address)) ||
            input_address == 0) {
            return false;
        }
        std::array<char, 4096> buffer{};
        std::uint64_t offset = 0;
        while (offset < kMaximumInputBytes) {
            if (input_address >
                (std::numeric_limits<std::uint64_t>::max)() - offset) {
                return false;
            }
            const std::uint64_t address = input_address + offset;
            std::size_t chunk = static_cast<std::size_t>((std::min)(
                kMaximumInputBytes - offset,
                static_cast<std::uint64_t>(buffer.size())));
            while (chunk != 0 &&
                   !memory.Read(address, buffer.data(), chunk)) {
                chunk /= 2;
            }
            if (chunk == 0) return false;
            const auto end = buffer.begin() +
                             static_cast<std::ptrdiff_t>(chunk);
            const auto terminator = std::find(buffer.begin(), end, '\0');
            input->append(buffer.begin(), terminator);
            if (terminator != end) return true;
            offset += chunk;
        }
        return false;
    }

    static void ResumeGuestParser(Aarch64PausedExecutionState* state) {
        state->stack_pointer -= kStackFrameSize;
        state->program_counter += sizeof(std::uint32_t);
    }

    bool Mirror(Aarch64PausedExecutionState* state, std::string* error) {
        if (!active_ || runtime_ == nullptr || runtime_->bionic() == nullptr ||
            state == nullptr || state->program_counter != entry_ ||
            (state->stack_pointer & 15U) != 0 ||
            state->stack_pointer < kStackFrameSize) {
            SetError(error,
                     "Candy RapidJSON host mirror paused state is invalid");
            return false;
        }
        ++calls_;
        const std::uint64_t caller_offset =
            state->registers[30] >= module_base_
                ? state->registers[30] - module_base_
                : 0;
        if (caller_offset != 0x4fe403cULL) {
            ++caller_fallbacks_;
            ResumeGuestParser(state);
            return true;
        }

        std::string input;
        const GuestAddressSpace& memory =
            runtime_->bionic()->address_space();
        if (!ReadInput(memory, state->registers[1], &input)) {
            ++read_fallbacks_;
            ResumeGuestParser(state);
            return true;
        }
        if (input.size() < kMinimumInputBytes) {
            ++small_fallbacks_;
            ResumeGuestParser(state);
            return true;
        }

        CandyRapidJsonHostBuildResult result{};
        std::string mirror_error;
        const auto started = std::chrono::steady_clock::now();
        const bool mirrored = BuildCandyRapidJsonHybridDocument(
            runtime_->bionic()->heap(), state->registers[0], input, &result,
            &mirror_error);
        const std::uint64_t wall_ns = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - started)
                .count());
        if (!mirrored) {
            ++refusals_;
            std::fprintf(
                stderr,
                "ISOMR_CANDY_RAPIDJSON_HOST_MIRROR_REFUSAL reason=%s addresses=none\n",
                mirror_error.c_str());
            ResumeGuestParser(state);
            return true;
        }
        ++substitutions_;
        input_bytes_ = input.size() >
                               (std::numeric_limits<std::uint64_t>::max)() -
                                   input_bytes_
                           ? (std::numeric_limits<std::uint64_t>::max)()
                           : input_bytes_ + input.size();
        nodes_ = result.nodes >
                         (std::numeric_limits<std::uint64_t>::max)() - nodes_
                     ? (std::numeric_limits<std::uint64_t>::max)()
                     : nodes_ + result.nodes;
        pool_bytes_ =
            result.pool_bytes >
                    (std::numeric_limits<std::uint64_t>::max)() - pool_bytes_
                ? (std::numeric_limits<std::uint64_t>::max)()
                : pool_bytes_ + result.pool_bytes;
        wall_ns_ = wall_ns >
                           (std::numeric_limits<std::uint64_t>::max)() -
                               wall_ns_
                       ? (std::numeric_limits<std::uint64_t>::max)()
                       : wall_ns_ + wall_ns;
        std::printf(
            "ISOMR_CANDY_RAPIDJSON_HOST_MIRROR substitution=%llu bytes=%llu nodes=%llu pool-bytes=%llu wall-ns=%llu addresses=none\n",
            static_cast<unsigned long long>(substitutions_),
            static_cast<unsigned long long>(input.size()),
            static_cast<unsigned long long>(result.nodes),
            static_cast<unsigned long long>(result.pool_bytes),
            static_cast<unsigned long long>(wall_ns));
        std::fflush(stdout);

        // GenericDocument::ParseStream returns the document reference already
        // present in x0. Returning directly skips only the guest parser body.
        state->program_counter = state->registers[30];
        return true;
    }

    void Close() noexcept {
        if (runtime_ == nullptr) return;
        if (active_)
            runtime_->RemoveMainOwnerThreadExecutionBreakpoint(entry_);
        if (active_) {
            std::printf(
                "ISOMR_CANDY_RAPIDJSON_HOST_MIRROR_RESULT calls=%llu substitutions=%llu input-bytes=%llu nodes=%llu pool-bytes=%llu wall-ns=%llu caller-fallbacks=%llu small-fallbacks=%llu read-fallbacks=%llu refusals=%llu cleanup=pass addresses=none\n",
                static_cast<unsigned long long>(calls_),
                static_cast<unsigned long long>(substitutions_),
                static_cast<unsigned long long>(input_bytes_),
                static_cast<unsigned long long>(nodes_),
                static_cast<unsigned long long>(pool_bytes_),
                static_cast<unsigned long long>(wall_ns_),
                static_cast<unsigned long long>(caller_fallbacks_),
                static_cast<unsigned long long>(small_fallbacks_),
                static_cast<unsigned long long>(read_fallbacks_),
                static_cast<unsigned long long>(refusals_));
        }
        active_ = false;
    }

    Aarch64ProfileRuntime* runtime_ = nullptr;
    std::uint64_t entry_ = 0;
    std::uint64_t module_base_ = 0;
    std::uint64_t calls_ = 0;
    std::uint64_t substitutions_ = 0;
    std::uint64_t input_bytes_ = 0;
    std::uint64_t nodes_ = 0;
    std::uint64_t pool_bytes_ = 0;
    std::uint64_t wall_ns_ = 0;
    std::uint64_t caller_fallbacks_ = 0;
    std::uint64_t small_fallbacks_ = 0;
    std::uint64_t read_fallbacks_ = 0;
    std::uint64_t refusals_ = 0;
    bool active_ = false;
};

bool ConvertPopupTextToUtf16(std::string_view input, std::wstring* output,
                             std::string* error) {
    if (output == nullptr ||
        input.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)())) {
        SetError(error, "popup dialog UTF-8 input is invalid");
        return false;
    }
    output->clear();
    if (input.empty()) return true;
    const int input_size = static_cast<int>(input.size());
    const int output_size = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, input.data(), input_size, nullptr, 0);
    if (output_size <= 0) {
        SetError(error, "popup dialog text is not valid UTF-8");
        return false;
    }
    output->resize(static_cast<std::size_t>(output_size));
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input.data(),
                            input_size, output->data(), output_size) !=
        output_size) {
        output->clear();
        SetError(error, "popup dialog UTF-8 conversion failed");
        return false;
    }
    return true;
}

bool ShowSystemPopupDialog(
    HWND owner, const GuestJni::PopupDialogRequest& request,
    bool* requested_button_pressed, std::string* error) {
    if (requested_button_pressed == nullptr || owner == nullptr ||
        IsWindow(owner) == FALSE || IsWindowVisible(owner) == FALSE ||
        IsWindowEnabled(owner) == FALSE) {
        SetError(error, "popup dialog owner window is unavailable");
        return false;
    }
    DWORD owner_process = 0;
    const DWORD owner_thread =
        GetWindowThreadProcessId(owner, &owner_process);
    if (owner_thread != GetCurrentThreadId() ||
        owner_process != GetCurrentProcessId()) {
        SetError(error, "popup dialog owner thread is invalid");
        return false;
    }
    if (request.button.empty()) {
        SetError(error, "popup dialog requested button text is empty");
        return false;
    }

    std::wstring message;
    std::wstring title;
    std::wstring button;
    if (!ConvertPopupTextToUtf16(request.message, &message, error) ||
        !ConvertPopupTextToUtf16(request.title, &title, error) ||
        !ConvertPopupTextToUtf16(request.button, &button, error))
        return false;

    constexpr int kRequestedButtonId = 1000;
    const TASKDIALOG_BUTTON dialog_button{kRequestedButtonId,
                                           button.c_str()};
    TASKDIALOGCONFIG dialog{};
    dialog.cbSize = sizeof(dialog);
    dialog.hwndParent = owner;
    dialog.hInstance = GetModuleHandleW(nullptr);
    dialog.dwFlags = TDF_POSITION_RELATIVE_TO_WINDOW | TDF_SIZE_TO_CONTENT;
    if (!request.noncancelable)
        dialog.dwFlags |= TDF_ALLOW_DIALOG_CANCELLATION;
    dialog.pszWindowTitle = title.c_str();
    dialog.pszContent = message.c_str();
    dialog.cButtons = 1;
    dialog.pButtons = &dialog_button;
    dialog.nDefaultButton = kRequestedButtonId;

    const HMODULE common_controls = LoadLibraryW(L"comctl32.dll");
    if (common_controls == nullptr) {
        SetError(error, "popup dialog common controls library is unavailable");
        return false;
    }
    using TaskDialogIndirectFunction = HRESULT(WINAPI*)(
        const TASKDIALOGCONFIG*, int*, int*, BOOL*);
    const auto task_dialog_indirect =
        reinterpret_cast<TaskDialogIndirectFunction>(
            GetProcAddress(common_controls, "TaskDialogIndirect"));
    if (task_dialog_indirect == nullptr) {
        FreeLibrary(common_controls);
        SetError(error,
                 "popup dialog TaskDialogIndirect entry point is unavailable");
        return false;
    }
    int pressed = 0;
    const HRESULT result =
        task_dialog_indirect(&dialog, &pressed, nullptr, nullptr);
    FreeLibrary(common_controls);
    if (FAILED(result)) {
        SetError(error, "popup dialog TaskDialogIndirect failed with HRESULT " +
                            std::to_string(static_cast<std::uint32_t>(result)));
        return false;
    }
    if (pressed == kRequestedButtonId) {
        *requested_button_pressed = true;
        return true;
    }
    if (!request.noncancelable && pressed == IDCANCEL) {
        *requested_button_pressed = false;
        return true;
    }
    SetError(error, "popup dialog returned an invalid dismissal result");
    return false;
}

void PrefixError(std::string* error, std::string_view prefix) {
    if (error == nullptr) return;
    *error = std::string(prefix) + ": " + *error;
}

bool ReadCurrentThreadCpuTime100ns(std::uint64_t* value) {
    if (value == nullptr) return false;
    FILETIME creation{};
    FILETIME exit{};
    FILETIME kernel{};
    FILETIME user{};
    if (GetThreadTimes(GetCurrentThread(), &creation, &exit,
                       &kernel, &user) == 0)
        return false;
    ULARGE_INTEGER kernel_value{};
    kernel_value.LowPart = kernel.dwLowDateTime;
    kernel_value.HighPart = kernel.dwHighDateTime;
    ULARGE_INTEGER user_value{};
    user_value.LowPart = user.dwLowDateTime;
    user_value.HighPart = user.dwHighDateTime;
    if (user_value.QuadPart >
        std::numeric_limits<std::uint64_t>::max() -
            kernel_value.QuadPart)
        return false;
    *value = kernel_value.QuadPart + user_value.QuadPart;
    return true;
}

bool IsFinitePositive(float value, float maximum) {
    return std::isfinite(value) && value > 0.0F && value <= maximum;
}

bool Call(Aarch64ProfileRuntime* runtime, std::string_view stage,
          std::string_view export_name,
          const std::vector<Aarch64CallArgument>& arguments,
          CandyEntryStage completed_stage, CandyEntryResult* result,
          Aarch64CallResult* call_result, std::string* error) {
    std::string worker_error;
    if (!runtime->bionic()->threads().CheckCompletedWorkerFailure(
            &worker_error)) {
        SetError(error, "CandyEntryBridge " + std::string(stage) +
                            " worker failure before guest call: " +
                            worker_error);
        return false;
    }
    const bool report_call = !stage.starts_with("step-");
    if (report_call)
        std::printf("ISOMR_CANDY_ENTRY_CALL stage=%.*s state=begin\n",
                    static_cast<int>(stage.size()), stage.data());
    std::string call_error;
    const std::size_t ordinal = ++result->bridge_call_count;
    const Aarch64GuestCallTelemetry prior_telemetry =
        runtime->last_call_telemetry();
    NanosleepCallsiteCallScope nanosleep_callsite_scope(runtime);
    const bool call_succeeded = runtime->CallExportEntry(
        export_name, arguments, call_result, &call_error);
    const NanosleepCallsiteTelemetrySnapshot nanosleep_callsite_snapshot =
        nanosleep_callsite_scope.Close();
    const Aarch64GuestCallTelemetry telemetry =
        runtime->last_call_telemetry();
    if (telemetry.enabled &&
        telemetry.mode == Aarch64CallTelemetryMode::kEnvelope &&
        telemetry.completion_sequence !=
            prior_telemetry.completion_sequence) {
        std::printf(
            "ISOMR_AARCH64_CALL_ENVELOPE ordinal=%zu stage=%.*s result=%s halt=0x%llx wall-ns=%llu cpu-available=%u cpu-100ns=%llu ticks=%llu runs=%llu code-reads=%llu svc=%llu inter-slice=%llu inter-slice-wall-ns=%llu heap-metadata-advances=%llu\n",
            ordinal, static_cast<int>(stage.size()), stage.data(),
            call_succeeded ? "success" : "failure",
            static_cast<unsigned long long>(telemetry.halt_reason),
            static_cast<unsigned long long>(telemetry.wall_time_ns),
            telemetry.thread_cpu_time_available ? 1U : 0U,
            static_cast<unsigned long long>(telemetry.thread_cpu_time_100ns),
            static_cast<unsigned long long>(telemetry.translated_ticks),
            static_cast<unsigned long long>(telemetry.jit_run_invocations),
            static_cast<unsigned long long>(
                telemetry.memory_read_code_invocations),
            static_cast<unsigned long long>(telemetry.svc_invocations),
            static_cast<unsigned long long>(
                telemetry.inter_slice_callback_invocations),
            static_cast<unsigned long long>(
                telemetry.inter_slice_callback_wall_time_ns),
            static_cast<unsigned long long>(
                telemetry.heap_metadata_generation_advances));
    }
    if (telemetry.enabled &&
        telemetry.mode == Aarch64CallTelemetryMode::kFull &&
        telemetry.completion_sequence !=
            prior_telemetry.completion_sequence) {
        try {
            const std::string pc_summary = FormatGuestPcSummary(telemetry);
            std::printf(
                "ISOMR_AARCH64_CALL_GUEST_PC ordinal=%zu stage=%.*s result=%s%s\n",
                ordinal, static_cast<int>(stage.size()), stage.data(),
                call_succeeded ? "success" : "failure",
                pc_summary.c_str());
        } catch (...) {
            std::printf(
                "ISOMR_AARCH64_CALL_GUEST_PC ordinal=%zu stage=%.*s result=%s total-samples=%llu retained=0\n",
                ordinal, static_cast<int>(stage.size()), stage.data(),
                call_succeeded ? "success" : "failure",
                static_cast<unsigned long long>(telemetry.guest_pc_samples));
        }
    }
    if (nanosleep_callsite_snapshot.enabled) {
        try {
            const std::string summary = FormatNanosleepCallsiteSummary(
                runtime, ordinal, stage, call_succeeded,
                nanosleep_callsite_snapshot);
            if (!summary.empty()) std::printf("%s\n", summary.c_str());
        } catch (...) {
            std::printf(
                "ISOMR_NANOSLEEP_CALLSITE ordinal=%zu stage=%.*s result=%s complete=0 resolution-failure=1 residual-count=%llu residual-requested-total-ns=%llu residual-requested-max-ns=%llu residual-handler-body-total-ns=%llu residual-handler-body-max-ns=%llu\n",
                ordinal, static_cast<int>(stage.size()), stage.data(),
                call_succeeded ? "success" : "failure",
                static_cast<unsigned long long>(
                    nanosleep_callsite_snapshot.aggregate.success_count),
                static_cast<unsigned long long>(
                    nanosleep_callsite_snapshot.aggregate.requested_total_ns),
                static_cast<unsigned long long>(
                    nanosleep_callsite_snapshot.aggregate.requested_max_ns),
                static_cast<unsigned long long>(
                    nanosleep_callsite_snapshot.aggregate.handler_body_total_ns),
                static_cast<unsigned long long>(
                    nanosleep_callsite_snapshot.aggregate.handler_body_max_ns));
        }
    }
    if (telemetry.enabled &&
        telemetry.mode == Aarch64CallTelemetryMode::kSvcCategory &&
        telemetry.completion_sequence !=
            prior_telemetry.completion_sequence) {
        const auto bucket = [&](Aarch64SvcCategory category)
            -> const Aarch64SvcCategoryTelemetry& {
            return telemetry.svc_categories[
                static_cast<std::size_t>(category)];
        };
        const Aarch64SvcCategoryTelemetry& condition =
            bucket(Aarch64SvcCategory::kConditionWait);
        const Aarch64SvcCategoryTelemetry& mutex_rwlock =
            bucket(Aarch64SvcCategory::kMutexRwlockBlocking);
        const Aarch64SvcCategoryTelemetry& futex =
            bucket(Aarch64SvcCategory::kFutexWait);
        const Aarch64SvcCategoryTelemetry& once_join =
            bucket(Aarch64SvcCategory::kPthreadOnceJoin);
        const Aarch64SvcCategoryTelemetry& sort_search =
            bucket(Aarch64SvcCategory::kQsortBsearch);
        const Aarch64SvcCategoryTelemetry& allocation =
            bucket(Aarch64SvcCategory::kAllocationFree);
        const Aarch64SvcCategoryTelemetry& jni_gles =
            bucket(Aarch64SvcCategory::kJniGles);
        const Aarch64SvcCategoryTelemetry& other =
            bucket(Aarch64SvcCategory::kOther);
        std::printf(
            "ISOMR_AARCH64_CALL_SVC_CATEGORY ordinal=%zu stage=%.*s result=%s total-count=%llu total-wall-ns=%llu condition-wait-count=%llu condition-wait-wall-ns=%llu mutex-rwlock-block-count=%llu mutex-rwlock-block-wall-ns=%llu futex-wait-count=%llu futex-wait-wall-ns=%llu pthread-once-join-count=%llu pthread-once-join-wall-ns=%llu qsort-bsearch-count=%llu qsort-bsearch-wall-ns=%llu allocation-free-count=%llu allocation-free-wall-ns=%llu jni-gles-count=%llu jni-gles-wall-ns=%llu other-count=%llu other-wall-ns=%llu\n",
            ordinal, static_cast<int>(stage.size()), stage.data(),
            call_succeeded ? "success" : "failure",
            static_cast<unsigned long long>(telemetry.svc_invocations),
            static_cast<unsigned long long>(telemetry.svc_wall_time_ns),
            static_cast<unsigned long long>(condition.invocations),
            static_cast<unsigned long long>(condition.wall_time_ns),
            static_cast<unsigned long long>(mutex_rwlock.invocations),
            static_cast<unsigned long long>(mutex_rwlock.wall_time_ns),
            static_cast<unsigned long long>(futex.invocations),
            static_cast<unsigned long long>(futex.wall_time_ns),
            static_cast<unsigned long long>(once_join.invocations),
            static_cast<unsigned long long>(once_join.wall_time_ns),
            static_cast<unsigned long long>(sort_search.invocations),
            static_cast<unsigned long long>(sort_search.wall_time_ns),
            static_cast<unsigned long long>(allocation.invocations),
            static_cast<unsigned long long>(allocation.wall_time_ns),
            static_cast<unsigned long long>(jni_gles.invocations),
            static_cast<unsigned long long>(jni_gles.wall_time_ns),
            static_cast<unsigned long long>(other.invocations),
            static_cast<unsigned long long>(other.wall_time_ns));
        try {
            const std::string import_summary =
                FormatSvcImportSummary(telemetry);
            std::printf(
                "ISOMR_AARCH64_CALL_SVC_IMPORT ordinal=%zu stage=%.*s result=%s%s\n",
                ordinal, static_cast<int>(stage.size()), stage.data(),
                call_succeeded ? "success" : "failure",
                import_summary.c_str());
        } catch (...) {
            std::printf(
                "ISOMR_AARCH64_CALL_SVC_IMPORT ordinal=%zu stage=%.*s result=%s residual-count=%llu residual-wall-ns=%llu\n",
                ordinal, static_cast<int>(stage.size()), stage.data(),
                call_succeeded ? "success" : "failure",
                static_cast<unsigned long long>(telemetry.svc_invocations),
                static_cast<unsigned long long>(
                    telemetry.svc_wall_time_ns));
        }
        try {
            const std::string clock_summary = FormatClockCallSummary(
                ordinal, stage, call_succeeded, telemetry);
            if (!clock_summary.empty())
                std::printf("%s\n", clock_summary.c_str());
        } catch (...) {
            // Clock telemetry is diagnostic only. Import-category telemetry
            // above remains authoritative if formatting allocation fails.
        }
        try {
            const std::string nanosleep_summary =
                FormatNanosleepCallSummary(
                    ordinal, stage, call_succeeded, telemetry);
            if (!nanosleep_summary.empty())
                std::printf("%s\n", nanosleep_summary.c_str());
        } catch (...) {
            // Requested-duration telemetry is diagnostic only. Formatting
            // failure must not alter the completed guest call.
        }
    }
    if (!call_succeeded) {
        SetError(error, "CandyEntryBridge " + std::string(stage) + "/" +
                            std::string(export_name) + ": " + call_error);
        return false;
    }
    worker_error.clear();
    if (!runtime->bionic()->threads().CheckCompletedWorkerFailure(
            &worker_error)) {
        SetError(error, "CandyEntryBridge " + std::string(stage) +
                            " worker failure after guest call: " +
                            worker_error);
        return false;
    }
    result->completed_stage = completed_stage;
    ++result->completed_calls;
    if (report_call)
        std::printf("ISOMR_CANDY_ENTRY_CALL stage=%.*s state=pass calls=%zu\n",
                    static_cast<int>(stage.size()), stage.data(),
                    result->completed_calls);
    return true;
}

Aarch64CallArgument G(std::uint64_t value) {
    return Aarch64CallArgument::General64(value);
}

Aarch64CallArgument F(float value) {
    return Aarch64CallArgument::Float32Bits(
        std::bit_cast<std::uint32_t>(value));
}

std::string_view AnalyticsNoValueModeName(
    CandyAnalyticsNoValueMode mode) {
    switch (mode) {
    case CandyAnalyticsNoValueMode::kNull:
        return "null";
    case CandyAnalyticsNoValueMode::kEmptyString:
        return "empty";
    }
    return {};
}

bool DrainAnalyticsValueRequests(
    Aarch64ProfileRuntime* runtime, const CandyEntryHandles& handles,
    CandyAnalyticsNoValueMode mode, CandyEntryResult* result,
    Aarch64CallResult* call_result, std::string* error) {
    GuestJni& jni = runtime->bionic()->jni();
    const std::size_t request_count =
        jni.pending_analytics_value_request_count();
    if (request_count == 0) return true;
    const std::string_view mode_name = AnalyticsNoValueModeName(mode);
    if (mode_name.empty()) {
        SetError(error,
                 "CandyEntryBridge analytics no-value mode is invalid");
        return false;
    }

    std::uint64_t class_reference = 0;
    std::uint64_t empty_value_reference = 0;
    std::string callback_error;
    bool ok = jni.CreateCompatibilityClassReference(
        "com/king/usdk/kdid/AnalyticsReceiver", &class_reference,
        &callback_error);
    if (ok && mode == CandyAnalyticsNoValueMode::kEmptyString) {
        ok = jni.CreateCompatibilityStringReference(
            "", &empty_value_reference, &callback_error);
    }

    for (std::size_t index = 0; index < request_count; ++index) {
        if (!ok) break;
        const std::optional<GuestJni::AnalyticsValueRequest> request =
            jni.NextPendingAnalyticsValueRequest();
        if (!request) {
            SetError(&callback_error,
                     "analytics request queue changed before dispatch");
            ok = false;
            break;
        }
        std::uint64_t key_reference = 0;
        ok = jni.CreateCompatibilityStringReference(
            request->key, &key_reference, &callback_error);
        if (ok) {
            const std::string stage =
                "analytics-response-" +
                std::to_string(request->sequence);
            ok = Call(runtime, stage, kAnalyticsCompletion,
                      {G(handles.environment), G(class_reference),
                       G(key_reference), G(empty_value_reference)},
                      CandyEntryStage::kAnalyticsCompletion, result,
                      call_result, &callback_error);
        }
        if (ok &&
            !jni.AcknowledgeAnalyticsValueRequest(request->sequence,
                                                  &callback_error))
            ok = false;
        if (ok) {
            ++result->analytics_completion_count;
            std::printf(
                "ISOMR_CANDY_ANALYTICS_RESPONSE sequence=%llu mode=%.*s "
                "key-length=%zu result=pass\n",
                static_cast<unsigned long long>(request->sequence),
                static_cast<int>(mode_name.size()), mode_name.data(),
                request->key.size());
        }
        std::string release_error;
        if (key_reference != 0 &&
            !jni.ReleaseCompatibilityLocalReference(key_reference,
                                                    &release_error)) {
            if (ok) callback_error = std::move(release_error);
            ok = false;
        }
    }

    const auto release_batch_reference =
        [&](std::uint64_t reference) {
            if (reference == 0) return;
            std::string release_error;
            if (!jni.ReleaseCompatibilityLocalReference(reference,
                                                        &release_error)) {
                if (ok) callback_error = std::move(release_error);
                ok = false;
            }
        };
    release_batch_reference(empty_value_reference);
    release_batch_reference(class_reference);
    if (!ok) {
        SetError(error,
                 "CandyEntryBridge analytics-completion/" +
                     std::string(kAnalyticsCompletion) + ": " +
                     callback_error);
        return false;
    }
    return true;
}

bool DrainAgeSignalsRequests(Aarch64ProfileRuntime* runtime,
                             const CandyEntryHandles& handles,
                             CandyEntryResult* result,
    Aarch64CallResult* call_result,
    std::string* error) {
    GuestJni& jni = runtime->bionic()->jni();
    const std::size_t request_count =
        jni.pending_age_signals_request_count();
    if (request_count == 0) return true;

    std::uint64_t class_reference = 0;
    std::uint64_t message_reference = 0;
    std::string callback_error;
    bool ok = jni.CreateCompatibilityClassReference(
        "com/king/usdk/privacyagecompliance/UsdkAgeSignals",
        &class_reference, &callback_error);
    if (ok) {
        ok = jni.CreateCompatibilityStringReference(
            kAgeSignalsUnavailableMessage, &message_reference,
            &callback_error);
    }

    for (std::size_t index = 0; index < request_count; ++index) {
        if (!ok) break;
        const std::optional<GuestJni::AgeSignalsRequest> request =
            jni.NextPendingAgeSignalsRequest();
        if (!request) {
            SetError(&callback_error,
                     "Age Signals request queue changed before dispatch");
            ok = false;
            break;
        }
        const std::string stage =
            "age-signals-error-" +
            std::to_string(request->sequence);
        ok = Call(
            runtime, stage, kAgeSignalsErrorCompletion,
            {G(handles.environment), G(class_reference),
             G(request->first_callback_handle),
             G(request->second_callback_handle),
             G(static_cast<std::uint32_t>(kAgeSignalsUnavailableError)),
             G(message_reference)},
            CandyEntryStage::kAgeSignalsCompletion, result, call_result,
            &callback_error);
        if (ok &&
            !jni.AcknowledgeAgeSignalsRequest(request->sequence,
                                              &callback_error))
            ok = false;
        if (ok) {
            ++result->age_signals_completion_count;
            std::printf(
                "ISOMR_CANDY_AGE_SIGNALS_RESPONSE sequence=%llu "
                "debug-scenario=%lld error=%d result=pass\n",
                static_cast<unsigned long long>(request->sequence),
                static_cast<long long>(request->debug_scenario),
                kAgeSignalsUnavailableError);
        }
    }

    const auto release_reference = [&](std::uint64_t reference) {
        if (reference == 0) return;
        std::string release_error;
        if (!jni.ReleaseCompatibilityLocalReference(reference,
                                                    &release_error)) {
            if (ok) callback_error = std::move(release_error);
            ok = false;
        }
    };
    release_reference(message_reference);
    release_reference(class_reference);
    if (!ok) {
        SetError(error,
                 "CandyEntryBridge age-signals-completion/" +
                     std::string(kAgeSignalsErrorCompletion) + ": " +
                     callback_error);
        return false;
    }
    return true;
}

bool DrainPopupDialogRequests(Aarch64ProfileRuntime* runtime,
                              const CandyEntryHandles& handles,
                              const CandyEntryOptions& options,
    CandyEntryResult* result,
    Aarch64CallResult* call_result,
    std::string* error) {
    GuestJni& jni = runtime->bionic()->jni();
    const std::size_t request_count =
        jni.pending_popup_dialog_request_count();
    if (request_count == 0) return true;
    if (options.request_close_after_popup_completion_for_testing &&
        request_count != 1) {
        SetError(error,
                 "CandyEntryBridge test close requires exactly one popup request");
        return false;
    }
    if (options.popup_dialog_mode !=
            CandyPopupDialogMode::kSystemTaskDialog &&
        options.popup_dialog_mode !=
            CandyPopupDialogMode::kAcknowledgeWithoutUiForTesting) {
        SetError(error, "CandyEntryBridge popup dialog mode is invalid");
        return false;
    }
    std::uint64_t dialog_reference = 0;
    std::string callback_error;
    bool ok = jni.CreateCompatibilityTypedObjectReference(
        "Lcom/king/usdk/popupguard/internal/PopupDialog;",
        &dialog_reference, &callback_error);
    for (std::size_t index = 0; index < request_count; ++index) {
        if (!ok) break;
        const std::optional<GuestJni::PopupDialogRequest> request =
            jni.NextPendingPopupDialogRequest();
        if (!request) {
            SetError(&callback_error,
                     "popup dialog request queue changed before dispatch");
            ok = false;
            break;
        }
        const std::string stage =
            "popup-dialog-dismissed-" +
            std::to_string(request->sequence);
        bool requested_button_pressed = true;
        if (options.popup_dialog_mode ==
            CandyPopupDialogMode::kSystemTaskDialog) {
            ok = ShowSystemPopupDialog(
                reinterpret_cast<HWND>(
                    runtime->bionic()->gles().window_handle()),
                *request, &requested_button_pressed, &callback_error);
        }
        if (ok)
            ok = Call(runtime, stage, kPopupDialogDismissed,
                      {G(handles.environment), G(dialog_reference),
                       G(request->callback_handle)},
                      CandyEntryStage::kPopupDialogCompletion, result,
                      call_result, &callback_error);
        if (ok &&
            !jni.AcknowledgePopupDialogRequest(request->sequence,
                                               &callback_error))
            ok = false;
        if (ok) {
            ++result->popup_dialog_completion_count;
            const std::string_view mode =
                options.popup_dialog_mode ==
                        CandyPopupDialogMode::kSystemTaskDialog
                    ? "system-task-dialog"
                    : "test-no-ui-acknowledge";
            std::printf(
                "ISOMR_CANDY_POPUP_DIALOG sequence=%llu mode=%.*s "
                "noncancelable=%u title-length=%zu message-length=%zu "
                "button-length=%zu dismissal=%s result=pass\n",
                static_cast<unsigned long long>(request->sequence),
                static_cast<int>(mode.size()), mode.data(),
                request->noncancelable ? 1U : 0U,
                request->title.size(), request->message.size(),
                request->button.size(),
                options.popup_dialog_mode ==
                        CandyPopupDialogMode::kAcknowledgeWithoutUiForTesting
                    ? "test-no-ui"
                    : requested_button_pressed ? "requested-button"
                                               : "cancel");
        }
    }
    if (dialog_reference != 0) {
        std::string release_error;
        if (!jni.ReleaseCompatibilityLocalReference(dialog_reference,
                                                    &release_error)) {
            if (ok) callback_error = std::move(release_error);
            ok = false;
        }
    }
    if (ok && options.request_close_after_popup_completion_for_testing) {
        ok = runtime->bionic()->gles().RequestClose(&callback_error);
        if (ok)
            std::puts(
                "ISOMR_CANDY_POPUP_CLOSE_REQUEST mode=test-after-completion result=pass");
    }
    if (!ok) {
        SetError(error,
                 "CandyEntryBridge popup-dialog-completion/" +
                     std::string(kPopupDialogDismissed) + ": " +
                     callback_error);
        return false;
    }
    return true;
}

std::string_view PointerActionName(NativePointerAction action) {
    switch (action) {
    case NativePointerAction::kDown:
        return "down";
    case NativePointerAction::kUp:
        return "up";
    case NativePointerAction::kMove:
        return "move";
    case NativePointerAction::kCancel:
        return "cancel";
    }
    return {};
}

bool CallPointerEvent(Aarch64ProfileRuntime* runtime,
                      const CandyEntryHandles& handles,
                      const CandyEntryOptions& options,
                      const NativePointerEvent& event,
                      CandyEntryResult* result,
                      Aarch64CallResult* call_result,
                      std::string* error) {
    const std::string_view action_name = PointerActionName(event.action);
    if (action_name.empty() || event.client_width <= 0 ||
        event.client_height <= 0) {
        SetError(error,
                 "CandyEntryBridge pointer event dimensions/action are invalid");
        return false;
    }
    const std::int64_t client_x = std::clamp<std::int64_t>(
        event.x, 0, static_cast<std::int64_t>(event.client_width) - 1);
    const std::int64_t client_y = std::clamp<std::int64_t>(
        event.y, 0, static_cast<std::int64_t>(event.client_height) - 1);
    const std::int32_t guest_x = static_cast<std::int32_t>(std::min<
        std::int64_t>(options.width - 1,
                      client_x * options.width / event.client_width));
    const std::int32_t guest_y = static_cast<std::int32_t>(std::min<
        std::int64_t>(options.height - 1,
                      client_y * options.height / event.client_height));
    const std::string stage = "pointer-" + std::string(action_name);
    const auto dispatch_started = options.step_boundary_telemetry
        ? std::chrono::steady_clock::now()
        : std::chrono::steady_clock::time_point{};
    if (!Call(runtime, stage, kTouch,
              {G(handles.environment), G(handles.native_application),
               G(0), G(static_cast<std::uint8_t>(event.action)),
               G(static_cast<std::uint32_t>(guest_x)),
               G(static_cast<std::uint32_t>(guest_y))},
              CandyEntryStage::kTouch, result, call_result, error))
        return false;
    const auto dispatch_completed = options.step_boundary_telemetry
        ? std::chrono::steady_clock::now()
        : std::chrono::steady_clock::time_point{};
    if (!runtime->bionic()->gles().AcknowledgePointerEvent(event, error)) {
        PrefixError(error,
                    "CandyEntryBridge pointer-event acknowledgement");
        return false;
    }
    ++result->pointer_event_count;
    if (options.step_boundary_telemetry) {
        const std::uint64_t dispatch_started_ns =
            static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    dispatch_started.time_since_epoch())
                    .count());
        const std::uint64_t dispatch_completed_ns =
            static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    dispatch_completed.time_since_epoch())
                    .count());
        const std::uint64_t queue_wait_ns =
            dispatch_started_ns >= event.enqueued_ns
                ? dispatch_started_ns - event.enqueued_ns
                : 0;
        const std::uint64_t guest_call_ns =
            dispatch_completed_ns >= dispatch_started_ns
                ? dispatch_completed_ns - dispatch_started_ns
                : 0;
        const std::uint64_t total_ns =
            dispatch_completed_ns >= event.enqueued_ns
                ? dispatch_completed_ns - event.enqueued_ns
                : 0;
        std::printf(
            "ISOMR_CANDY_POINTER_LATENCY sequence=%zu action=%.*s "
            "queue-wait-ns=%llu guest-call-ns=%llu total-ns=%llu "
            "addresses=none\n",
            result->pointer_event_count, static_cast<int>(action_name.size()),
            action_name.data(),
            static_cast<unsigned long long>(queue_wait_ns),
            static_cast<unsigned long long>(guest_call_ns),
            static_cast<unsigned long long>(total_ns));
    }
    return true;
}

}  // namespace

std::string FormatNanosleepCallsiteSummaryForTesting(
    Aarch64ProfileRuntime* runtime, std::size_t ordinal,
    std::string_view stage, bool succeeded,
    const NanosleepCallsiteTelemetrySnapshot& snapshot) {
    return FormatNanosleepCallsiteSummary(
        runtime, ordinal, stage, succeeded, snapshot);
}

bool CandyEntryBridge::Run(Aarch64ProfileRuntime* runtime,
                           const CandyEntryHandles& handles,
                           const CandyEntryOptions& options,
                           CandyEntryResult* result, std::string* error) {
    if (result != nullptr) *result = {};
    if (runtime == nullptr || result == nullptr) {
        SetError(error, "CandyEntryBridge validation: runtime/result is invalid");
        return false;
    }
    if (handles.environment == 0 || handles.platform_proxy_class == 0 ||
        handles.activity == 0 || handles.native_application == 0 ||
        handles.version_string == 0 || handles.asset_manager == 0) {
        SetError(error, "CandyEntryBridge validation: guest handle is zero");
        return false;
    }
    if (options.width <= 0 || options.width > 16384 ||
        options.height <= 0 || options.height > 16384 ||
        options.rotation < 0 || options.rotation > 3 ||
        !IsFinitePositive(options.density, 1000.0F) ||
        !IsFinitePositive(options.display_scale, 16.0F) ||
        !IsFinitePositive(options.step_seconds, 1.0F) ||
        options.step_count == 0 || options.step_count > 600) {
        SetError(error, "CandyEntryBridge validation: display/step options are invalid");
        return false;
    }
    if (AnalyticsNoValueModeName(options.analytics_no_value_mode).empty()) {
        SetError(
            error,
            "CandyEntryBridge validation: analytics no-value mode is invalid");
        return false;
    }
    if (options.popup_dialog_mode !=
            CandyPopupDialogMode::kSystemTaskDialog &&
        options.popup_dialog_mode !=
            CandyPopupDialogMode::kAcknowledgeWithoutUiForTesting) {
        SetError(error,
                 "CandyEntryBridge validation: popup dialog mode is invalid");
        return false;
    }
    if (options.request_close_after_popup_completion_for_testing &&
        (options.popup_dialog_mode !=
             CandyPopupDialogMode::kAcknowledgeWithoutUiForTesting ||
         !options.show_window || !options.run_until_close)) {
        SetError(
            error,
            "CandyEntryBridge validation: test popup close requires no-UI acknowledgement, a visible window, and run-until-close");
        return false;
    }
    NativeGles& gles = runtime->bionic()->gles();
    if (options.show_window && !gles.Show(error)) {
        PrefixError(error, "CandyEntryBridge show-window");
        return false;
    }
    CandyBulkZeroStreamScope bulk_zero_stream_scope(runtime);
    if (options.bulk_zero_stream) {
        if (!bulk_zero_stream_scope.Configure(error)) {
            PrefixError(error,
                        "CandyEntryBridge bulk-zero-stream configuration");
            return false;
        }
    }
    CandyRapidJsonHostMirrorScope rapidjson_host_mirror_scope(runtime);
    if (options.rapidjson_host_mirror) {
        if (!rapidjson_host_mirror_scope.Configure(error)) {
            PrefixError(error,
                        "CandyEntryBridge RapidJSON host mirror configuration");
            return false;
        }
    }
    Aarch64GuestCallTelemetry long_call_pc;
    std::optional<std::uint64_t> active_step;
    std::optional<std::chrono::steady_clock::time_point>
        active_step_started_at;
    std::uint64_t long_call_slice_count = 0;
    std::uint32_t long_call_pc_sampler_state = 0x9e3779b9U;
    if (options.step_boundary_telemetry) {
        long_call_pc.enabled = true;
        long_call_pc.mode = Aarch64CallTelemetryMode::kFull;
        long_call_pc.guest_pc_histogram = std::make_shared<
            std::array<Aarch64GuestPcTelemetry, 64>>();
    }
    MainInterSliceCallbackScope inter_slice_callback_scope(runtime);
    if (options.show_window) {
        if (!runtime->SetMainOwnerThreadInterSliceCallback(
                kOwnerMessagePumpSliceInstructionBudget,
                [runtime, &gles, result, &active_step,
                 &active_step_started_at, &long_call_pc,
                 &long_call_slice_count, &long_call_pc_sampler_state](
                    std::string* callback_error) {
                    const auto worker_ok = [runtime, callback_error] {
                        std::string worker_error;
                        if (runtime->bionic()
                                ->threads()
                                .CheckCompletedWorkerFailure(
                                    &worker_error))
                            return true;
                        SetError(
                            callback_error,
                            "CandyEntryBridge in-flight worker failure: " +
                                worker_error);
                        return false;
                    };
                    if (!worker_ok()) return false;
                    if (!gles.PumpOwnerMessages(callback_error)) return false;
                    ++result->owner_message_pump_count;
                    if (active_step && active_step_started_at &&
                        std::chrono::steady_clock::now() -
                                *active_step_started_at >=
                            kLongCallPcThreshold) {
                        ++long_call_slice_count;
                        long_call_pc_sampler_state =
                            AdvanceLongCallPcSampler(
                                long_call_pc_sampler_state);
                        const std::uint64_t next_slice_instruction_budget =
                            kLongCallPcMinimumSliceInstructionBudget +
                            (long_call_pc_sampler_state &
                             kLongCallPcSliceInstructionBudgetMask);
                        if (!runtime
                                 ->SetMainOwnerThreadInterSliceBudgetForNextSlice(
                                     next_slice_instruction_budget,
                                     callback_error))
                            return false;
                        if ((long_call_slice_count == 1 ||
                             ((long_call_pc_sampler_state >> 17U) & 7U) ==
                                 0) &&
                            !runtime->CaptureMainGuestPcTelemetrySample(
                                &long_call_pc, callback_error))
                            return false;
                    }
                    return worker_ok();
                },
                error)) {
            PrefixError(error,
                        "CandyEntryBridge owner-message inter-slice callback");
            return false;
        }
        inter_slice_callback_scope.Activate();
    }
    Aarch64CallResult call_result{};
    if (!Call(runtime, "create-native-instance", kCreateNativeInstance,
              {G(handles.environment), G(handles.platform_proxy_class),
               G(handles.activity)},
              CandyEntryStage::kCreateNativeInstance, result, &call_result,
              error))
        return false;
    result->native_instance = call_result.x0;
    if (result->native_instance == 0) {
        SetError(error,
                 "CandyEntryBridge create-native-instance/" +
                     std::string(kCreateNativeInstance) +
                     ": returned zero native instance");
        return false;
    }
    std::printf("ISOMR_CANDY_ENTRY native-instance=0x%llx\n",
                static_cast<unsigned long long>(result->native_instance));
    if (!Call(runtime, "create-native-application", kCreate,
              {G(handles.environment), G(handles.native_application),
               G(result->native_instance), G(handles.version_string),
               G(handles.asset_manager)},
              CandyEntryStage::kCreateNativeApplication, result,
              &call_result, error) ||
        !Call(runtime, "system-event-start", kSystemEvent,
              {G(handles.environment), G(handles.native_application), G(4)},
              CandyEntryStage::kSystemEventStart, result, &call_result,
              error) ||
        !Call(runtime, "system-event-resume", kSystemEvent,
              {G(handles.environment), G(handles.native_application), G(1)},
              CandyEntryStage::kSystemEventResume, result, &call_result,
              error) ||
        !Call(runtime, "initialize", kInitialize,
              {G(handles.environment), G(handles.native_application),
               G(static_cast<std::uint32_t>(options.width)),
               G(static_cast<std::uint32_t>(options.height)),
               G(static_cast<std::uint32_t>(options.rotation)),
               F(options.density), G(options.fallback ? 1 : 0)},
              CandyEntryStage::kInitialize, result, &call_result, error) ||
        !Call(runtime, "display-scale", kDisplayScale,
              {G(handles.environment), G(handles.native_application),
               F(options.display_scale)},
              CandyEntryStage::kDisplayScale, result, &call_result, error) ||
        !Call(runtime, "screen-size", kScreenSize,
              {G(handles.environment), G(handles.native_application),
               G(static_cast<std::uint32_t>(options.width)),
               G(static_cast<std::uint32_t>(options.height))},
              CandyEntryStage::kScreenSize, result, &call_result, error))
        return false;

    bool post_analytics_budget_armed = false;
    for (std::uint64_t step = 0;
         options.run_until_close || step < options.step_count; ++step) {
        const auto frame_cadence_started = options.step_boundary_telemetry
            ? std::chrono::steady_clock::now()
            : std::chrono::steady_clock::time_point{};
        const std::size_t frame_draws_before =
            options.step_boundary_telemetry ? gles.draw_call_count() : 0;
        const std::size_t frame_presents_before =
            options.step_boundary_telemetry ? gles.present_count() : 0;
        const std::size_t frame_lit_presents_before =
            options.step_boundary_telemetry ? gles.lit_present_count() : 0;
        const std::size_t frame_pointer_events_before =
            options.step_boundary_telemetry ? result->pointer_event_count : 0;
        if (options.show_window) {
            bool close_requested = false;
            std::vector<NativePointerEvent> pointer_events;
            std::string pump_error;
            const bool pump_ok = gles.PumpEvents(
                &close_requested, &pointer_events, &pump_error);
            if (!pump_ok) {
                SetError(error, std::move(pump_error));
                PrefixError(error, "CandyEntryBridge pump-events");
                return false;
            }
            for (const NativePointerEvent& event : pointer_events) {
                if (!CallPointerEvent(runtime, handles, options, event,
                                      result, &call_result, error))
                    return false;
            }
            if (close_requested) {
                result->termination = CandyEntryTermination::kWindowClosed;
                break;
            }
        }
        const bool report_step =
            step < 3 || (step + 1) % 60 == 0 ||
            (result->analytics_completion_count != 0 && step < 360);
        if (report_step)
            std::printf("ISOMR_CANDY_STEP index=%llu state=begin\n",
                        static_cast<unsigned long long>(step));
        BionicThreads::QsortOperationSnapshot qsort_before{};
        std::uint64_t thread_cpu_before = 0;
        const bool thread_cpu_before_available =
            !options.step_boundary_telemetry ||
            ReadCurrentThreadCpuTime100ns(&thread_cpu_before);
        if (options.step_boundary_telemetry &&
            !runtime->bionic()->threads().TakeQsortOperationSnapshot(
                &qsort_before)) {
            SetError(error,
                     "CandyEntryBridge step telemetry snapshot failed");
            return false;
        }
        const auto step_started = options.step_boundary_telemetry
            ? std::chrono::steady_clock::now()
            : std::chrono::steady_clock::time_point{};
        const std::string stage = "step-" + std::to_string(step);
        if (options.step_boundary_telemetry) {
            long_call_slice_count = 0;
            long_call_pc_sampler_state =
                0x9e3779b9U ^ static_cast<std::uint32_t>(step);
            if (long_call_pc_sampler_state == 0)
                long_call_pc_sampler_state = 0x6d2b79f5U;
            active_step = step;
            active_step_started_at = step_started;
        }
        const bool step_ok = Call(
            runtime, stage, kStep,
            {G(handles.environment), G(handles.native_application),
             F(options.step_seconds)},
            CandyEntryStage::kStep, result, &call_result, error);
        active_step.reset();
        active_step_started_at.reset();
        BionicThreads::QsortOperationSnapshot qsort_after{};
        std::uint64_t thread_cpu_after = 0;
        bool thread_cpu_after_available = false;
        std::int64_t step_wall_ns = 0;
        if (options.step_boundary_telemetry) {
            thread_cpu_after_available =
                thread_cpu_before_available &&
                ReadCurrentThreadCpuTime100ns(&thread_cpu_after) &&
                thread_cpu_after >= thread_cpu_before;
            if (!runtime->bionic()->threads().TakeQsortOperationSnapshot(
                    &qsort_after)) {
                SetError(error,
                         "CandyEntryBridge step telemetry snapshot failed");
                return false;
            }
            step_wall_ns =
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() - step_started)
                    .count();
        }
        if (long_call_pc.guest_pc_samples != 0) {
            try {
                const std::string long_call_summary =
                    FormatLongCallPcSummary(runtime, step, long_call_pc);
                std::printf("%s\n", long_call_summary.c_str());
            } catch (...) {
                std::printf(
                    "ISOMR_CANDY_LONG_CALL_PC step=%llu wall-threshold-ns=250000000 total-samples=%llu retained=0 resolution-failure=1 addresses=none\n",
                    static_cast<unsigned long long>(step),
                    static_cast<unsigned long long>(
                        long_call_pc.guest_pc_samples));
            }
            ResetGuestPcTelemetry(&long_call_pc);
        }
        const auto report_step_telemetry =
            [&](const char* step_result, bool iteration_complete) {
            if (!options.step_boundary_telemetry) return;
            const std::uint64_t qsort_wall_ns =
                qsort_after.wall_ns - qsort_before.wall_ns;
            const std::uint64_t qsort_executor_setup_wall_ns =
                qsort_after.executor_setup_wall_ns -
                qsort_before.executor_setup_wall_ns;
            const std::uint64_t qsort_comparator_wall_ns =
                qsort_after.comparator_wall_ns -
                qsort_before.comparator_wall_ns;
            const std::uint64_t qsort_swap_wall_ns =
                qsort_after.swap_wall_ns - qsort_before.swap_wall_ns;
            const std::uint64_t qsort_component_wall_ns =
                qsort_executor_setup_wall_ns + qsort_comparator_wall_ns +
                qsort_swap_wall_ns;
            const std::uint64_t qsort_residual_wall_ns =
                qsort_wall_ns >= qsort_component_wall_ns
                ? qsort_wall_ns - qsort_component_wall_ns
                : 0;
            const auto frame_wall_ns =
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() -
                    frame_cadence_started)
                    .count();
            std::printf(
                "ISOMR_CANDY_STEP_TELEMETRY index=%llu result=%s wall-ns=%lld thread-cpu-available=%u thread-cpu-100ns=%llu qsort-started=%llu qsort-completed=%llu qsort-failed=%llu qsort-comparisons=%llu qsort-swaps=%llu qsort-wall-ns=%llu qsort-executor-setup-wall-ns=%llu qsort-comparator-wall-ns=%llu qsort-swap-wall-ns=%llu qsort-residual-wall-ns=%llu qsort-open-end=%llu iteration-complete=%u frame-wall-ns=%lld guest-draws=%zu host-presents=%zu lit-presents=%zu pointer-events=%zu\n",
                static_cast<unsigned long long>(step),
                step_result,
                static_cast<long long>(step_wall_ns),
                thread_cpu_after_available ? 1U : 0U,
                static_cast<unsigned long long>(
                    thread_cpu_after_available
                        ? thread_cpu_after - thread_cpu_before
                        : 0),
                static_cast<unsigned long long>(
                    qsort_after.started - qsort_before.started),
                static_cast<unsigned long long>(
                    qsort_after.completed - qsort_before.completed),
                static_cast<unsigned long long>(
                    qsort_after.failed - qsort_before.failed),
                static_cast<unsigned long long>(
                    qsort_after.comparisons - qsort_before.comparisons),
                static_cast<unsigned long long>(
                    qsort_after.swaps - qsort_before.swaps),
                static_cast<unsigned long long>(qsort_wall_ns),
                static_cast<unsigned long long>(
                    qsort_executor_setup_wall_ns),
                static_cast<unsigned long long>(qsort_comparator_wall_ns),
                static_cast<unsigned long long>(qsort_swap_wall_ns),
                static_cast<unsigned long long>(qsort_residual_wall_ns),
                static_cast<unsigned long long>(
                    qsort_after.started - qsort_after.completed),
                iteration_complete ? 1U : 0U,
                static_cast<long long>(frame_wall_ns),
                gles.draw_call_count() - frame_draws_before,
                gles.present_count() - frame_presents_before,
                gles.lit_present_count() - frame_lit_presents_before,
                result->pointer_event_count - frame_pointer_events_before);
        };
        if (!step_ok) {
            if (runtime->bionic()->threads().process_exit_requested()) {
                result->termination =
                    CandyEntryTermination::kGuestProcessExit;
                break;
            }
            report_step_telemetry("fail", false);
            return false;
        }
        result->step_keep_running = call_result.x0 != 0;
        if (report_step)
            std::printf(
                "ISOMR_CANDY_STEP index=%llu state=pass keep-running=%u\n",
                static_cast<unsigned long long>(step),
                result->step_keep_running ? 1U : 0U);
        if (!result->step_keep_running) {
            report_step_telemetry("pass", false);
            result->termination = CandyEntryTermination::kGuestStopped;
            break;
        }
        if (!DrainAnalyticsValueRequests(
                runtime, handles, options.analytics_no_value_mode, result,
                &call_result, error))
            return false;
        if (!DrainAgeSignalsRequests(runtime, handles, result, &call_result,
                                    error))
            return false;
        if (!DrainPopupDialogRequests(runtime, handles, options, result,
                                     &call_result, error))
            return false;
        if (!post_analytics_budget_armed &&
            result->analytics_completion_count != 0 &&
            options.post_analytics_instruction_budget != 0) {
            if (!runtime->SetMainInstructionBudgetForNextCall(
                    options.post_analytics_instruction_budget, error)) {
                PrefixError(error,
                            "CandyEntryBridge post-analytics instruction budget");
                return false;
            }
            std::printf(
                "ISOMR_CANDY_INSTRUCTION_BUDGET stage=post-analytics "
                "value=%llu\n",
                static_cast<unsigned long long>(
                    options.post_analytics_instruction_budget));
            post_analytics_budget_armed = true;
        }
        if (options.present_each_step &&
            !gles.Present("guest-step", error)) {
            PrefixError(error, "CandyEntryBridge present");
            return false;
        }
        report_step_telemetry("pass", true);
    }
    if (result->termination == CandyEntryTermination::kNone)
        result->termination = CandyEntryTermination::kStepLimit;
    if (result->termination == CandyEntryTermination::kWindowClosed) {
        if (!Call(runtime, "system-event-pause", kSystemEvent,
                  {G(handles.environment), G(handles.native_application),
                   G(2)},
                  CandyEntryStage::kSystemEventPause, result, &call_result,
                  error) ||
            !Call(runtime, "system-event-stop", kSystemEvent,
                  {G(handles.environment), G(handles.native_application),
                   G(3)},
                  CandyEntryStage::kSystemEventStop, result, &call_result,
                  error) ||
            !Call(runtime, "system-event-terminate", kSystemEvent,
                  {G(handles.environment), G(handles.native_application),
                   G(5)},
                  CandyEntryStage::kSystemEventTerminate, result,
                  &call_result, error) ||
            !Call(runtime, "set-app-shutting-down", kSetAppShuttingDown,
                  {G(handles.environment), G(handles.native_application)},
                  CandyEntryStage::kSetAppShuttingDown, result, &call_result,
                  error) ||
            !Call(runtime, "destroy", kDestroy,
                  {G(handles.environment), G(handles.native_application)},
                  CandyEntryStage::kDestroy, result, &call_result, error))
            return false;
        BionicRuntime* bionic = runtime->bionic();
        constexpr std::chrono::seconds kWorkerShutdownTimeout{5};
        BionicThreads::ProcessExitWorkerCleanup worker_cleanup;
        const bool workers_stopped =
            bionic->threads().CancelAndReapWorkersForProcessExit(
                [bionic] {
                    return bionic->sync()
                        .WakeAllConditionWaitersForLifecycleShutdown();
                },
                kWorkerShutdownTimeout, &worker_cleanup, error);
        std::printf(
            "ISOMR_CANDY_CLOSE_WORKERS cancellation-requested=%zu "
            "cancellation-observed=%zu wake-condition-waiters=%zu "
            "reaped=%zu timeout-ms=%lld stopped=%u threads=%zu active=%zu\n",
            worker_cleanup.cancellation_requests,
            worker_cleanup.cancellation_observed,
            worker_cleanup.blocked_calls_woken,
            worker_cleanup.workers_reaped,
            static_cast<long long>(kWorkerShutdownTimeout.count() * 1000),
            workers_stopped ? 1U : 0U,
            bionic->threads().thread_count(),
            bionic->threads().active_count());
        if (!workers_stopped) {
            if (error != nullptr && error->empty())
                *error = "CandyEntryBridge close worker shutdown failed";
            return false;
        }
    }
    std::string worker_error;
    if (!runtime->bionic()->threads().CheckCompletedWorkerFailure(
            &worker_error)) {
        SetError(error, "CandyEntryBridge final worker failure check: " +
                            worker_error);
        return false;
    }
    return true;
}

}  // namespace isomr
