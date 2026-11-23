#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "aarch64_guest_session.h"

namespace isomr {

class Aarch64ProfileRuntime;
struct NanosleepCallsiteTelemetrySnapshot;

std::string FormatSvcImportSummary(
    const Aarch64GuestCallTelemetry& telemetry);
std::string FormatClockCallSummary(
    std::size_t ordinal, std::string_view stage, bool succeeded,
    const Aarch64GuestCallTelemetry& telemetry);
std::string FormatGuestPcSummary(
    const Aarch64GuestCallTelemetry& telemetry);
std::string FormatNanosleepCallSummary(
    std::size_t ordinal, std::string_view stage, bool succeeded,
    const Aarch64GuestCallTelemetry& telemetry);
std::string FormatNanosleepCallsiteSummaryForTesting(
    Aarch64ProfileRuntime* runtime, std::size_t ordinal,
    std::string_view stage, bool succeeded,
    const NanosleepCallsiteTelemetrySnapshot& snapshot);

struct CandyEntryHandles {
    std::uint64_t environment = 0;
    std::uint64_t platform_proxy_class = 0;
    std::uint64_t activity = 0;
    std::uint64_t native_application = 0;
    std::uint64_t version_string = 0;
    std::uint64_t asset_manager = 0;
};

enum class CandyAnalyticsNoValueMode : std::uint8_t {
    kNull,
    kEmptyString,
};

enum class CandyPopupDialogMode : std::uint8_t {
    kSystemTaskDialog,
    kAcknowledgeWithoutUiForTesting,
};

struct CandyEntryOptions {
    std::int32_t width = 1280;
    std::int32_t height = 720;
    std::int32_t rotation = 0;
    float density = 1.0F;
    bool fallback = false;
    float display_scale = 1.0F;
    float step_seconds = 1.0F / 60.0F;
    std::uint32_t step_count = 1;
    bool run_until_close = false;
    bool show_window = false;
    bool present_each_step = false;
    bool step_boundary_telemetry = false;
    bool bulk_zero_stream = false;
    bool rapidjson_host_mirror = false;
    std::uint64_t post_analytics_instruction_budget = 0;
    CandyAnalyticsNoValueMode analytics_no_value_mode =
        CandyAnalyticsNoValueMode::kNull;
    CandyPopupDialogMode popup_dialog_mode =
        CandyPopupDialogMode::kSystemTaskDialog;
    bool request_close_after_popup_completion_for_testing = false;
};

enum class CandyEntryStage : std::uint8_t {
    kNone,
    kCreateNativeInstance,
    kCreateNativeApplication,
    kSystemEventStart,
    kSystemEventResume,
    kSystemEventPause,
    kSystemEventStop,
    kSystemEventTerminate,
    kInitialize,
    kDisplayScale,
    kScreenSize,
    kStep,
    kTouch,
    kAnalyticsCompletion,
    kAgeSignalsCompletion,
    kPopupDialogCompletion,
    kSetAppShuttingDown,
    kDestroy,
};

enum class CandyEntryTermination : std::uint8_t {
    kNone,
    kStepLimit,
    kWindowClosed,
    kGuestStopped,
    kGuestProcessExit,
};

struct CandyEntryResult {
    std::uint64_t native_instance = 0;
    bool step_keep_running = false;
    CandyEntryStage completed_stage = CandyEntryStage::kNone;
    CandyEntryTermination termination = CandyEntryTermination::kNone;
    std::size_t completed_calls = 0;
    std::size_t bridge_call_count = 0;
    std::size_t owner_message_pump_count = 0;
    std::size_t pointer_event_count = 0;
    std::size_t analytics_completion_count = 0;
    std::size_t age_signals_completion_count = 0;
    std::size_t popup_dialog_completion_count = 0;
};

class CandyEntryBridge {
public:
    static bool Run(Aarch64ProfileRuntime* runtime,
                    const CandyEntryHandles& handles,
                    const CandyEntryOptions& options,
                    CandyEntryResult* result, std::string* error);
};

}  // namespace isomr
