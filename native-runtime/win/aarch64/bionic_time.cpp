#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#include <windows.h>

#include "bionic_time.h"

#include "bionic_thread_state.h"
#include "guest_address_space.h"
#include "guest_heap.h"
#include "import_registry.h"

#include <cstdint>
#include <chrono>
#include <ctime>
#include <cstdio>
#include <limits>
#include <thread>
#include <utility>

namespace isomr {
namespace {

enum class BionicTimespecValidation : std::uint8_t {
    kValid,
    kInvalid,
    kOverflow,
};

BionicTimespecValidation BionicTimespecToNanoseconds(
    const BionicTimespec& value, std::uint64_t* result) noexcept {
    if (result == nullptr || value.seconds < 0 || value.nanoseconds < 0 ||
        value.nanoseconds >= 1000000000LL)
        return BionicTimespecValidation::kInvalid;
    constexpr std::int64_t kNanosecondsPerSecond = 1000000000LL;
    const std::int64_t maximum = std::chrono::nanoseconds::max().count();
    if (value.seconds > maximum / kNanosecondsPerSecond ||
        value.nanoseconds >
            maximum - value.seconds * kNanosecondsPerSecond)
        return BionicTimespecValidation::kOverflow;
    *result = static_cast<std::uint64_t>(
        value.seconds * kNanosecondsPerSecond + value.nanoseconds);
    return BionicTimespecValidation::kValid;
}

constexpr std::uint64_t kTicksPerSecond = 10000000ULL;
constexpr std::uint64_t kNanosecondsPerTick = 100ULL;
constexpr std::uint64_t kWindowsToUnixEpochTicks = 116444736000000000ULL;
constexpr std::uint64_t kFailure = UINT64_MAX;
constexpr std::int32_t kErrnoFault = 14;
constexpr std::int32_t kErrnoInvalid = 22;
constexpr std::int32_t kErrnoOverflow = 75;

BionicWaitableTimerApi NativeWaitableTimerApi() {
    BionicWaitableTimerApi api;
    api.create_high_resolution = []() {
        HANDLE handle = CreateWaitableTimerExW(
            nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
            TIMER_MODIFY_STATE | SYNCHRONIZE);
        return BionicWaitableTimerCreateResult{
            handle, handle == nullptr ? GetLastError() : ERROR_SUCCESS};
    };
    api.set_relative_100ns = [](void* handle, std::int64_t due_time) {
        LARGE_INTEGER due{};
        due.QuadPart = due_time;
        const BOOL succeeded = SetWaitableTimerEx(
            static_cast<HANDLE>(handle), &due, 0, nullptr, nullptr, nullptr, 0);
        return BionicWaitableTimerResult{
            succeeded != FALSE,
            succeeded == FALSE ? GetLastError() : ERROR_SUCCESS};
    };
    api.wait = [](void* handle) {
        const DWORD result =
            WaitForSingleObject(static_cast<HANDLE>(handle), INFINITE);
        return BionicWaitableTimerResult{
            result == WAIT_OBJECT_0,
            result == WAIT_FAILED ? GetLastError() : result};
    };
    api.close = [](void* handle) {
        const BOOL succeeded = CloseHandle(static_cast<HANDLE>(handle));
        return BionicWaitableTimerResult{
            succeeded != FALSE,
            succeeded == FALSE ? GetLastError() : ERROR_SUCCESS};
    };
    return api;
}

class LocalWaitableTimer final {
public:
    LocalWaitableTimer(const BionicWaitableTimerApi* api,
                       BionicWaitableTimerApi::Handle handle) noexcept
        : api_(api), handle_(handle) {}
    ~LocalWaitableTimer() noexcept {
        if (handle_ != nullptr && api_ != nullptr && api_->close) {
            try {
                (void)api_->close(handle_);
            } catch (...) {
            }
        }
    }
    LocalWaitableTimer(LocalWaitableTimer&& other) noexcept
        : api_(std::exchange(other.api_, nullptr)),
          handle_(std::exchange(other.handle_, nullptr)) {}
    LocalWaitableTimer& operator=(LocalWaitableTimer&&) = delete;
    LocalWaitableTimer(const LocalWaitableTimer&) = delete;
    LocalWaitableTimer& operator=(const LocalWaitableTimer&) = delete;

    BionicWaitableTimerResult Close() noexcept {
        BionicWaitableTimerApi::Handle handle =
            std::exchange(handle_, nullptr);
        if (handle == nullptr || api_ == nullptr || !api_->close)
            return {false, ERROR_INVALID_FUNCTION};
        try {
            return api_->close(handle);
        } catch (...) {
            return {false, ERROR_UNHANDLED_EXCEPTION};
        }
    }

private:
    const BionicWaitableTimerApi* api_ = nullptr;
    BionicWaitableTimerApi::Handle handle_ = nullptr;
};

void SetError(std::string* error, std::string message) {
    if (error != nullptr) *error = std::move(message);
}

std::string NativeError(std::string operation, std::uint32_t error) {
    return std::move(operation) + " failed with native error " +
           std::to_string(error);
}

bool CloseAfterFailure(LocalWaitableTimer* timer, std::string* error,
                       std::string failure) {
    const BionicWaitableTimerResult closed = timer->Close();
    if (!closed.succeeded) {
        failure += "; close failed with native error " +
                   std::to_string(closed.native_error);
    }
    SetError(error, std::move(failure));
    return false;
}

bool HighResolutionNanosleep(const BionicWaitableTimerApi& api,
                             std::uint64_t nanoseconds,
                             std::string* error) {
    const BionicWaitableTimerCreateResult created =
        api.create_high_resolution();
    if (created.handle == nullptr) {
        SetError(error, NativeError(
                            "bionic nanosleep high-resolution timer create",
                            created.native_error));
        return false;
    }
    LocalWaitableTimer timer(&api, created.handle);
    const std::uint64_t ticks = nanoseconds / kNanosecondsPerTick +
                                (nanoseconds % kNanosecondsPerTick != 0);
    const std::int64_t relative_due_time =
        -static_cast<std::int64_t>(ticks);
    const BionicWaitableTimerResult set =
        api.set_relative_100ns(created.handle, relative_due_time);
    if (!set.succeeded) {
        return CloseAfterFailure(
            &timer, error,
            NativeError("bionic nanosleep high-resolution timer set",
                        set.native_error));
    }
    const BionicWaitableTimerResult waited = api.wait(created.handle);
    if (!waited.succeeded) {
        return CloseAfterFailure(
            &timer, error,
            NativeError("bionic nanosleep high-resolution timer wait",
                        waited.native_error));
    }
    // Close is part of the operation, before remainder clearing and success
    // telemetry. A close failure therefore leaves both unpublished and the
    // guest remainder unchanged; the guard has relinquished the handle so the
    // failed CloseHandle attempt is never repeated.
    const BionicWaitableTimerResult closed = timer.Close();
    if (!closed.succeeded) {
        SetError(error,
                 NativeError("bionic nanosleep high-resolution timer close",
                             closed.native_error));
        return false;
    }
    return true;
}

std::uint64_t FileTimeValue(const FILETIME& value) {
    ULARGE_INTEGER converted{};
    converted.LowPart = value.dwLowDateTime;
    converted.HighPart = value.dwHighDateTime;
    return converted.QuadPart;
}

BionicTimespec TicksToTimespec(std::uint64_t ticks) {
    return {
        static_cast<std::int64_t>(ticks / kTicksPerSecond),
        static_cast<std::int64_t>((ticks % kTicksPerSecond) *
                                  kNanosecondsPerTick),
    };
}

bool Realtime(BionicTimespec* result) {
    FILETIME value{};
    GetSystemTimePreciseAsFileTime(&value);
    const std::uint64_t ticks = FileTimeValue(value);
    if (ticks < kWindowsToUnixEpochTicks) return false;
    *result = TicksToTimespec(ticks - kWindowsToUnixEpochTicks);
    return true;
}

bool CpuTime(bool thread, BionicTimespec* result) {
    FILETIME creation{};
    FILETIME exit{};
    FILETIME kernel{};
    FILETIME user{};
    const BOOL ok = thread
                        ? GetThreadTimes(GetCurrentThread(), &creation, &exit,
                                         &kernel, &user)
                        : GetProcessTimes(GetCurrentProcess(), &creation, &exit,
                                          &kernel, &user);
    if (ok == FALSE) return false;
    const std::uint64_t kernel_ticks = FileTimeValue(kernel);
    const std::uint64_t user_ticks = FileTimeValue(user);
    if (kernel_ticks > UINT64_MAX - user_ticks) return false;
    *result = TicksToTimespec(kernel_ticks + user_ticks);
    return true;
}

bool ClockTime(std::uint64_t clock_id, BionicTimespec* result) {
    if (result == nullptr) return false;
    switch (clock_id) {
    case 0:  // CLOCK_REALTIME
    case 5:  // CLOCK_REALTIME_COARSE
    case 8:  // CLOCK_REALTIME_ALARM
        return Realtime(result);
    case 1:  // CLOCK_MONOTONIC
    case 4:  // CLOCK_MONOTONIC_RAW
    case 6: {  // CLOCK_MONOTONIC_COARSE
        ULONGLONG ticks = 0;
        QueryUnbiasedInterruptTimePrecise(&ticks);
        *result = TicksToTimespec(ticks);
        return true;
    }
    case 2:  // CLOCK_PROCESS_CPUTIME_ID
        return CpuTime(false, result);
    case 3:  // CLOCK_THREAD_CPUTIME_ID
        return CpuTime(true, result);
    case 7:  // CLOCK_BOOTTIME
    case 9: {  // CLOCK_BOOTTIME_ALARM
        ULONGLONG ticks = 0;
        QueryInterruptTimePrecise(&ticks);
        *result = TicksToTimespec(ticks);
        return true;
    }
    default:
        return false;
    }
}

bool ClockResolution(std::uint64_t clock_id, BionicTimespec* result) {
    BionicTimespec probe{};
    if (!ClockTime(clock_id, &probe)) return false;
    *result = {0, 1000};
    return true;
}

bool WriteTimespec(GuestAddressSpace* memory, std::uint64_t address,
                   const BionicTimespec& value, std::string* error) {
    if (address == 0 || !memory->Write(address, &value, sizeof(value))) {
        if (error != nullptr) *error = "bionic timespec output is invalid";
        return false;
    }
    return true;
}

bool ReturnTimeError(BionicThreadState* threads, std::int32_t guest_errno,
                     Aarch64ImportRegistry::Registers* registers,
                     std::string* error, std::string message) {
    SetError(error, std::move(message));
    if (!threads->SetCurrentErrno(guest_errno, error)) return false;
    (*registers)[0] = kFailure;
    return true;
}

bool UnixTimeToFileTime(std::int64_t seconds, FILETIME* result) {
    if (result == nullptr) return false;
    std::uint64_t ticks = 0;
    if (seconds >= 0) {
        const std::uint64_t positive = static_cast<std::uint64_t>(seconds);
        if (positive >
            (std::numeric_limits<std::uint64_t>::max() -
             kWindowsToUnixEpochTicks) /
                kTicksPerSecond)
            return false;
        ticks = kWindowsToUnixEpochTicks + positive * kTicksPerSecond;
    } else {
        const std::uint64_t magnitude =
            static_cast<std::uint64_t>(-(seconds + 1)) + 1;
        if (magnitude > kWindowsToUnixEpochTicks / kTicksPerSecond)
            return false;
        ticks = kWindowsToUnixEpochTicks - magnitude * kTicksPerSecond;
    }
    ULARGE_INTEGER converted{};
    converted.QuadPart = ticks;
    result->dwLowDateTime = converted.LowPart;
    result->dwHighDateTime = converted.HighPart;
    return true;
}

bool IsLeapYear(std::uint16_t year) {
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

std::int32_t YearDay(const SYSTEMTIME& value) {
    constexpr std::int32_t starts[] =
        {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    std::int32_t result = starts[value.wMonth - 1] + value.wDay - 1;
    if (value.wMonth > 2 && IsLeapYear(value.wYear)) ++result;
    return result;
}

bool LocalSystemTime(std::int64_t seconds, SYSTEMTIME* local,
                     std::int64_t* utc_offset, std::int32_t* daylight) {
    FILETIME utc_file{};
    SYSTEMTIME utc{};
    if (local == nullptr || utc_offset == nullptr || daylight == nullptr ||
        !UnixTimeToFileTime(seconds, &utc_file) ||
        FileTimeToSystemTime(&utc_file, &utc) == FALSE ||
        SystemTimeToTzSpecificLocalTimeEx(nullptr, &utc, local) == FALSE)
        return false;

    FILETIME local_as_utc{};
    if (SystemTimeToFileTime(local, &local_as_utc) == FALSE) return false;
    const std::uint64_t utc_ticks = FileTimeValue(utc_file);
    const std::uint64_t local_ticks = FileTimeValue(local_as_utc);
    const std::int64_t offset_ticks =
        local_ticks >= utc_ticks
            ? static_cast<std::int64_t>(local_ticks - utc_ticks)
            : -static_cast<std::int64_t>(utc_ticks - local_ticks);
    *utc_offset = offset_ticks / static_cast<std::int64_t>(kTicksPerSecond);

    DYNAMIC_TIME_ZONE_INFORMATION dynamic{};
    TIME_ZONE_INFORMATION zone{};
    if (GetDynamicTimeZoneInformation(&dynamic) == TIME_ZONE_ID_INVALID ||
        GetTimeZoneInformationForYear(local->wYear, &dynamic, &zone) == FALSE)
        return false;
    const std::int64_t standard_offset =
        -60LL * static_cast<std::int64_t>(zone.Bias + zone.StandardBias);
    const std::int64_t daylight_offset =
        -60LL * static_cast<std::int64_t>(zone.Bias + zone.DaylightBias);
    if (zone.DaylightDate.wMonth != 0 && *utc_offset == daylight_offset &&
        daylight_offset != standard_offset) {
        *daylight = 1;
    } else if (*utc_offset == standard_offset ||
               zone.DaylightDate.wMonth == 0) {
        *daylight = 0;
    } else {
        *daylight = -1;
    }
    return true;
}

bool LocalTimeToUnix(BionicTm* value, std::int64_t* seconds) {
    if (value == nullptr || seconds == nullptr) return false;
    std::tm host{};
    host.tm_sec = value->seconds;
    host.tm_min = value->minutes;
    host.tm_hour = value->hours;
    host.tm_mday = value->month_day;
    host.tm_mon = value->month;
    host.tm_year = value->year;
    host.tm_wday = value->week_day;
    host.tm_yday = value->year_day;
    host.tm_isdst = value->daylight_saving;
    const __time64_t converted = _mktime64(&host);
    if (converted == static_cast<__time64_t>(-1)) return false;

    SYSTEMTIME local{};
    std::int64_t utc_offset = 0;
    std::int32_t daylight = 0;
    if (!LocalSystemTime(static_cast<std::int64_t>(converted), &local,
                         &utc_offset, &daylight))
        return false;
    *value = BionicTm{
        local.wSecond,
        local.wMinute,
        local.wHour,
        local.wDay,
        static_cast<std::int32_t>(local.wMonth) - 1,
        static_cast<std::int32_t>(local.wYear) - 1900,
        local.wDayOfWeek,
        YearDay(local),
        daylight,
        0,
        utc_offset,
        0,
    };
    *seconds = static_cast<std::int64_t>(converted);
    return true;
}

// strptime support: read a NUL-terminated guest C string.
bool ReadGuestCString(GuestAddressSpace& memory, std::uint64_t address,
                      std::string* text) {
    if (address == 0 || text == nullptr) return false;
    text->clear();
    for (std::size_t index = 0; index < 4096; ++index) {
        char value = 0;
        if (!memory.Read(address + index, &value, sizeof(value))) return false;
        if (value == '\0') return true;
        text->push_back(value);
    }
    return false;
}

// Case-insensitive prefix match of a full month/day name; returns the
// consumed character count, or 0 when nothing matches.
std::size_t MatchName(const char* text, const char* const* names,
                      std::size_t count, std::size_t* index_out) {
    for (std::size_t candidate = 0; candidate < count; ++candidate) {
        const std::string_view name = names[candidate];
        std::size_t position = 0;
        while (position < name.size() && text[position] != '\0' &&
               std::tolower(static_cast<unsigned char>(text[position])) ==
                   std::tolower(static_cast<unsigned char>(name[position])))
            ++position;
        if (position == name.size()) {
            if (index_out != nullptr) *index_out = candidate;
            return position;
        }
    }
    return 0;
}

// Minimal strptime over the directives Nimble's server-date parsing uses
// (numeric dates, two- and four-digit years, month/weekday names, wall
// clock fields, and literal/whitespace separators).
const char* HostStrptime(const char* text, const char* format,
                         std::tm* out) {
    constexpr const char* kWeekdays[] = {
        "sunday", "monday", "tuesday", "wednesday", "thursday", "friday",
        "saturday"};
    constexpr const char* kMonths[] = {"january", "february", "march",
                                       "april",   "may",      "june",
                                       "july",    "august",   "september",
                                       "october", "november", "december"};
    const char* s = text;
    const char* f = format;
    while (*f != '\0') {
        if (std::isspace(static_cast<unsigned char>(*f))) {
            while (std::isspace(static_cast<unsigned char>(*s))) ++s;
            ++f;
            continue;
        }
        if (*f != '%') {
            if (*s != *f) return nullptr;
            ++s;
            ++f;
            continue;
        }
        ++f;
        const char directive = *f++;
        if (directive == '%') {
            if (*s != '%') return nullptr;
            ++s;
            continue;
        }
        if (directive == 'n' || directive == 't') {
            while (std::isspace(static_cast<unsigned char>(*s))) ++s;
            continue;
        }
        if (directive == 'a' || directive == 'A') {
            std::size_t index = 0;
            const std::size_t consumed =
                MatchName(s, kWeekdays, 7, &index);
            if (consumed == 0) return nullptr;
            out->tm_wday = static_cast<int>(index);
            s += consumed;
            continue;
        }
        if (directive == 'b' || directive == 'B' || directive == 'h') {
            std::size_t index = 0;
            const std::size_t consumed = MatchName(s, kMonths, 12, &index);
            if (consumed == 0) return nullptr;
            out->tm_mon = static_cast<int>(index);
            s += consumed;
            continue;
        }
        if (directive == 'p') {
            if ((s[0] | 0x20) == 'p') {
                out->tm_hour = out->tm_hour % 12 + 12;
                s += 2;
            } else if ((s[0] | 0x20) == 'a') {
                out->tm_hour %= 12;
                s += 2;
            } else {
                return nullptr;
            }
            continue;
        }
        if (directive == 'Z') {
            while (std::isalpha(static_cast<unsigned char>(*s))) ++s;
            continue;
        }
        if (directive == 'F') {
            const char* after = HostStrptime(s, "%Y-%m-%d", out);
            if (after == nullptr) return nullptr;
            s = after;
            continue;
        }
        if (directive == 'T') {
            const char* after = HostStrptime(s, "%H:%M:%S", out);
            if (after == nullptr) return nullptr;
            s = after;
            continue;
        }
        int maximum_digits = 2;
        int* target = nullptr;
        int base_offset = 0;
        switch (directive) {
        case 'Y': maximum_digits = 4; target = &out->tm_year; break;
        case 'y':
            maximum_digits = 2;
            target = &out->tm_year;
            base_offset = 100;  // years since 1900; 00-68 -> 2000s
            break;
        case 'm': target = &out->tm_mon; base_offset = -1; break;
        case 'd': case 'e': target = &out->tm_mday; break;
        case 'H': target = &out->tm_hour; break;
        case 'I': target = &out->tm_hour; break;
        case 'M': target = &out->tm_min; break;
        case 'S': target = &out->tm_sec; break;
        case 'j': maximum_digits = 3; target = &out->tm_yday; base_offset = -1; break;
        case 'w': target = &out->tm_wday; break;
        default: return nullptr;
        }
        int parsed = 0;
        int digits = 0;
        while (digits < maximum_digits &&
               std::isdigit(static_cast<unsigned char>(*s))) {
            parsed = parsed * 10 + (*s - '0');
            ++s;
            ++digits;
        }
        if (digits == 0) return nullptr;
        if (directive == 'y' && parsed < 69) parsed += 100;
        *target = parsed + base_offset;
    }
    return s;
}

}  // namespace

bool BionicTime::SleepForNanoseconds(std::uint64_t nanoseconds,
                                     std::string* error) {
    // ARMR_NANOSLEEP_CAP_NS clamps every guest-requested sleep to a fixed
    // ceiling in nanoseconds. Unity/il2cpp pace background work and lock
    // backoff with usleep/nanosleep; under emulation those sleeps dominate
    // wall-clock (the CTR deserialize runs ~99% wait-bound at ~1 MIPS
    // effective). A sleep is a minimum-delay hint — returning early is always
    // legal — so clamping turns idle pacing into fast retries without
    // changing correctness. 0/unset leaves sleeps untouched.
    static const std::uint64_t cap_ns = [] {
        wchar_t flag[24] = {};
        if (GetEnvironmentVariableW(L"ARMR_NANOSLEEP_CAP_NS", flag, 24) == 0) {
            return std::uint64_t{0};
        }
        const long long v = _wtoi64(flag);
        return v > 0 ? static_cast<std::uint64_t>(v) : std::uint64_t{0};
    }();
    if (cap_ns != 0 && nanoseconds > cap_ns) {
        nanoseconds = cap_ns;
    }
    const auto duration = std::chrono::nanoseconds(nanoseconds);
    if (nanosleep_test_backend_) {
        nanosleep_test_backend_(duration);
    } else if (nanosleep_backend_ == BionicNanosleepBackend::kSleepFor) {
        std::this_thread::sleep_for(duration);
    } else if (nanoseconds != 0 &&
               !HighResolutionNanosleep(waitable_timer_api_, nanoseconds,
                                        error)) {
        return false;
    }
    return true;
}

bool BionicTime::RegisterImports(Aarch64ImportRegistry* imports,
                                 std::string* error) {
    if (imports == nullptr || heap_ == nullptr || memory_ == nullptr ||
        threads_ == nullptr) {
        if (error != nullptr) *error = "bionic time dependencies are null";
        return false;
    }
    GuestAddressSpace* const memory = memory_;
    BionicThreadState* const threads = threads_;
    if (!imports->Register(
            "clock_gettime",
            [memory, threads](Aarch64ImportRegistry::Registers* registers,
                              std::string* handler_error) {
                BionicTimespec value{};
                if (!ClockTime((*registers)[0], &value)) {
                    if (!threads->SetCurrentErrno(kErrnoInvalid,
                                                  handler_error))
                        return false;
                    (*registers)[0] = kFailure;
                    return true;
                }
                if (!WriteTimespec(memory, (*registers)[1], value,
                                   handler_error)) {
                    (void)threads->SetCurrentErrno(kErrnoFault,
                                                   handler_error);
                    return false;
                }
                (*registers)[0] = 0;
                return true;
            },
            error))
        return false;
    if (!imports->Register(
            "clock_getres",
            [memory, threads](Aarch64ImportRegistry::Registers* registers,
                              std::string* handler_error) {
                BionicTimespec value{};
                if (!ClockResolution((*registers)[0], &value)) {
                    if (!threads->SetCurrentErrno(kErrnoInvalid,
                                                  handler_error))
                        return false;
                    (*registers)[0] = kFailure;
                    return true;
                }
                if ((*registers)[1] != 0 &&
                    !WriteTimespec(memory, (*registers)[1], value,
                                   handler_error)) {
                    (void)threads->SetCurrentErrno(kErrnoFault,
                                                   handler_error);
                    return false;
                }
                (*registers)[0] = 0;
                return true;
            },
            error))
        return false;
    if (!imports->Register(
            "clock",
            [](Aarch64ImportRegistry::Registers* registers, std::string*) {
                // clock() reports process CPU time in CLOCKS_PER_SEC units;
                // bionic's CLOCKS_PER_SEC is 1e6, so return kernel+user
                // 100ns ticks converted to microseconds.
                FILETIME created{}, exited{}, kernel{}, user{};
                if (!GetProcessTimes(GetCurrentProcess(), &created, &exited,
                                     &kernel, &user)) {
                    (*registers)[0] = kFailure;
                    return true;
                }
                const std::uint64_t ticks =
                    ((static_cast<std::uint64_t>(kernel.dwHighDateTime) << 32) |
                     kernel.dwLowDateTime) +
                    ((static_cast<std::uint64_t>(user.dwHighDateTime) << 32) |
                     user.dwLowDateTime);
                (*registers)[0] = ticks / 10;
                return true;
            },
            error))
        return false;
    if (!imports->Register(
        "gettimeofday",
        [memory, threads](Aarch64ImportRegistry::Registers* registers,
                          std::string* handler_error) {
            BionicTimespec now{};
            if (!Realtime(&now)) return false;
            const BionicTimeval value{
                now.seconds,
                now.nanoseconds / 1000,
            };
            if ((*registers)[0] != 0 &&
                !memory->Write((*registers)[0], &value, sizeof(value))) {
                if (handler_error != nullptr)
                    *handler_error = "bionic timeval output is invalid";
                (void)threads->SetCurrentErrno(kErrnoFault, handler_error);
                return false;
            }
            if ((*registers)[1] != 0) {
                constexpr std::uint64_t timezone = 0;
                if (!memory->Write((*registers)[1], &timezone,
                                   sizeof(timezone))) {
                    if (handler_error != nullptr)
                        *handler_error = "bionic timezone output is invalid";
                    (void)threads->SetCurrentErrno(kErrnoFault,
                                                   handler_error);
                    return false;
                }
            }
            (*registers)[0] = 0;
            return true;
        },
        error))
        return false;
    if (!imports->Register(
            "time",
            [memory, threads](Aarch64ImportRegistry::Registers* registers,
                              std::string* handler_error) {
                BionicTimespec now{};
                if (!Realtime(&now)) return false;
                if ((*registers)[0] != 0 &&
                    !memory->Write((*registers)[0], &now.seconds,
                                   sizeof(now.seconds))) {
                    if (handler_error != nullptr)
                        *handler_error = "bionic time output is invalid";
                    (void)threads->SetCurrentErrno(kErrnoFault,
                                                   handler_error);
                    return false;
                }
                (*registers)[0] = static_cast<std::uint64_t>(now.seconds);
                return true;
            },
            error))
        return false;
    if (!imports->Register(
            "strptime",
            [memory, threads](Aarch64ImportRegistry::Registers* registers,
                              std::string* handler_error) {
                std::string text;
                std::string format;
                if (!ReadGuestCString(*memory, (*registers)[0], &text) ||
                    !ReadGuestCString(*memory, (*registers)[1], &format)) {
                    (*registers)[0] = 0;
                    return true;
                }
                BionicTm parsed{};
                if ((*registers)[2] != 0 &&
                    !memory->Read((*registers)[2], &parsed,
                                  sizeof(parsed))) {
                    (*registers)[0] = 0;
                    return true;
                }
                std::tm host{};
                host.tm_sec = parsed.seconds;
                host.tm_min = parsed.minutes;
                host.tm_hour = parsed.hours;
                host.tm_mday = parsed.month_day;
                host.tm_mon = parsed.month;
                host.tm_year = parsed.year;
                host.tm_wday = parsed.week_day;
                host.tm_yday = parsed.year_day;
                host.tm_isdst = parsed.daylight_saving;
                const char* consumed = HostStrptime(text.c_str(),
                                                    format.c_str(), &host);
                if (consumed == nullptr) {
                    (*registers)[0] = 0;
                    return true;
                }
                parsed.seconds = host.tm_sec;
                parsed.minutes = host.tm_min;
                parsed.hours = host.tm_hour;
                parsed.month_day = host.tm_mday;
                parsed.month = host.tm_mon;
                parsed.year = host.tm_year;
                parsed.week_day = host.tm_wday;
                parsed.year_day = host.tm_yday;
                parsed.daylight_saving = host.tm_isdst;
                if ((*registers)[2] != 0 &&
                    !memory->Write((*registers)[2], &parsed,
                                   sizeof(parsed))) {
                    (void)threads->SetCurrentErrno(kErrnoFault,
                                                   handler_error);
                    return false;
                }
                (*registers)[0] =
                    (*registers)[0] + static_cast<std::uint64_t>(
                                         consumed - text.c_str());
                return true;
            },
            error))
        return false;
    // strftime/strftime_l: host strftime in the C locale; the locale_t
    // argument of the _l variant is ignored because every served locale uses
    // the default categories.
    const auto strftime_handler =
        [memory](Aarch64ImportRegistry::Registers* registers,
                 std::string* handler_error) {
                const std::uint64_t output = (*registers)[0];
                const std::uint64_t maximum = (*registers)[1];
                std::string format;
                if (output == 0 || maximum == 0 ||
                    !ReadGuestCString(*memory, (*registers)[2], &format)) {
                    (*registers)[0] = 0;
                    return true;
                }
                BionicTm value{};
                if ((*registers)[3] == 0 ||
                    !memory->Read((*registers)[3], &value, sizeof(value))) {
                    (*registers)[0] = 0;
                    return true;
                }
                std::tm host{};
                host.tm_sec = value.seconds;
                host.tm_min = value.minutes;
                host.tm_hour = value.hours;
                host.tm_mday = value.month_day;
                host.tm_mon = value.month;
                host.tm_year = value.year;
                host.tm_wday = value.week_day;
                host.tm_yday = value.year_day;
                host.tm_isdst = value.daylight_saving;
                std::string formatted(256, '\0');
                const std::size_t produced =
                    std::strftime(formatted.data(), formatted.size(),
                                  format.c_str(), &host);
                if (produced == 0 || produced >= maximum) {
                    (*registers)[0] = 0;
                    return true;
                }
                formatted.resize(produced);
                if (!memory->Write(output, formatted.data(),
                                   produced + 1)) {
                    if (handler_error != nullptr)
                        *handler_error = "bionic strftime output is invalid";
                    return false;
                }
                (*registers)[0] = produced;
                return true;
            };
    if (!imports->Register("strftime", strftime_handler, error) ||
        !imports->Register("strftime_l", strftime_handler, error))
        return false;
    if (!imports->Register(
        "localtime",
        [this](Aarch64ImportRegistry::Registers* registers,
               std::string* handler_error) {
            std::uint64_t result = 0;
            if (!Localtime((*registers)[0], &result, handler_error))
                return false;
            (*registers)[0] = result;
            return true;
        },
        error))
        return false;
    if (!imports->Register(
        "gmtime",
        [this](Aarch64ImportRegistry::Registers* registers,
               std::string* handler_error) {
            std::uint64_t result = 0;
            if (!Gmtime((*registers)[0], &result, handler_error))
                return false;
            (*registers)[0] = result;
            return true;
        },
        error))
        return false;
    if (!imports->Register(
            "mktime",
            [memory, threads](Aarch64ImportRegistry::Registers* registers,
                              std::string* handler_error) {
                BionicTm value{};
                if ((*registers)[0] == 0 ||
                    !memory->Read((*registers)[0], &value,
                                  sizeof(value))) {
                    return ReturnTimeError(
                        threads, kErrnoFault, registers, handler_error,
                        "bionic mktime input is invalid");
                }
                std::int64_t seconds = 0;
                if (!LocalTimeToUnix(&value, &seconds)) {
                    return ReturnTimeError(
                        threads, kErrnoOverflow, registers, handler_error,
                        "bionic mktime time is outside the native Windows range");
                }
                if (!memory->Write((*registers)[0], &value,
                                   sizeof(value))) {
                    return ReturnTimeError(
                        threads, kErrnoFault, registers, handler_error,
                        "bionic mktime normalized output is invalid");
                }
                (*registers)[0] = static_cast<std::uint64_t>(seconds);
                return true;
            },
            error))
        return false;
    if (!imports->Register(
        "difftime",
        [](Aarch64ImportRegistry::Registers* registers,
           std::string*) {
            const double end =
                static_cast<double>(static_cast<std::int64_t>((*registers)[0]));
            const double begin =
                static_cast<double>(static_cast<std::int64_t>((*registers)[1]));
            registers->SetDoubleResult(end - begin);
            return true;
        },
        error))
        return false;
    if (!imports->Register(
        "nanosleep",
        [this, memory, threads](
            Aarch64ImportRegistry::Registers* registers,
            std::string* handler_error) {
            BionicTimespec requested{};
            if ((*registers)[0] == 0 ||
                !memory->Read((*registers)[0], &requested,
                              sizeof(requested))) {
                return ReturnTimeError(
                    threads, kErrnoFault, registers, handler_error,
                    "bionic nanosleep request is invalid");
            }
            std::uint64_t requested_nanoseconds = 0;
            const BionicTimespecValidation validation =
                BionicTimespecToNanoseconds(requested,
                                            &requested_nanoseconds);
            if (validation == BionicTimespecValidation::kInvalid) {
                return ReturnTimeError(
                    threads, kErrnoInvalid, registers, handler_error,
                    "bionic nanosleep duration is invalid");
            }
            const std::uint64_t remainder = (*registers)[1];
            if (remainder != 0 &&
                !memory->IsWritableRange(remainder,
                                         sizeof(BionicTimespec))) {
                return ReturnTimeError(
                    threads, kErrnoFault, registers, handler_error,
                    "bionic nanosleep remainder is invalid");
            }
            if (validation == BionicTimespecValidation::kOverflow) {
                return ReturnTimeError(
                    threads, kErrnoOverflow, registers, handler_error,
                    "bionic nanosleep duration exceeds the host bound");
            }
            // Boehm's GC generic-lock backoff reaches nanosleep with an
            // escalating power-of-two delay (up to ~16.7ms) while spinning on
            // GC_allocate_ml (caller guest-lr 0x420269b6c4 in libil2cpp).
            // During CTR's deserialize many threads contend that lock and the
            // real sleep leaves it idle in multi-ms bubbles between brief
            // holds; yielding the host thread lets a waiter reacquire the
            // instant the holder frees it. Opt-in via ARMR_CTR_YIELD_BACKOFF.
            static const bool yield_boehm_backoff = [] {
                wchar_t buf[8] = {};
                return GetEnvironmentVariableW(L"ARMR_CTR_YIELD_BACKOFF",
                                               buf, 8) > 0 &&
                       buf[0] != L'0';
            }();
            if (yield_boehm_backoff &&
                registers->callee_saved_general.back() == 0x420269b6c4ULL) {
                // Cap the backoff at ~200us (from the ~16.7ms max) so a waiter
                // retries and reacquires the freed lock quickly instead of
                // leaving it idle through the full escalation window. A real
                // (short) sleep keeps the host CPU free; the normal path below
                // handles the capped duration and remainder/completion.
                if (requested_nanoseconds > 200000ULL) {
                    requested_nanoseconds = 200000ULL;
                }
            }
            thread_local std::uint64_t nanosleep_calls = 0;
            ++nanosleep_calls;
            if (!nanosleep_callsite_telemetry_enabled_ &&
                (requested.seconds != 0 ||
                requested.nanoseconds >= 100000000LL ||
                nanosleep_calls <= 3 || nanosleep_calls % 1000 == 0)) {
                std::printf(
                    "ISOMR_NANOSLEEP call=%llu request-seconds=%lld "
                    "request-nanoseconds=%lld guest-lr=0x%llx\n",
                    static_cast<unsigned long long>(nanosleep_calls),
                    static_cast<long long>(requested.seconds),
                    static_cast<long long>(requested.nanoseconds),
                    static_cast<unsigned long long>(
                        registers->callee_saved_general.back()));
            }
            if (nanosleep_calls % 4096 == 0 || nanosleep_calls == 20000) {
                // Read-only frame-chain walk from x29: identifies which
                // guest loop is polling in nanosleep without any guest
                // breakpoints.
                std::uint64_t frame_pointer =
                    registers->callee_saved_general[10];
                std::string chain;
                for (int depth = 0; depth < 8 && frame_pointer != 0;
                     ++depth) {
                    std::array<std::uint64_t, 2> record{};
                    if (!memory->Read(frame_pointer, record.data(),
                                      sizeof(record)))
                        break;
                    if (!chain.empty()) chain.push_back(',');
                    chain += "0x" + [&] {
                        std::array<char, 24> text{};
                        std::snprintf(text.data(), text.size(), "%llx",
                                      static_cast<unsigned long long>(
                                          record[1]));
                        return std::string(text.data());
                    }();
                    if (record[0] <= frame_pointer) break;
                    frame_pointer = record[0];
                }
                std::printf("ISOMR_NANOSLEEP_CHAIN %s\n", chain.c_str());
                std::fflush(stdout);
                if (nanosleep_calls % 12288 == 0) {
                    // Live inflate progress: read zstream total_out from the
                    // spawned decompression driver (deterministic address).
                    // total_out at zstream+0x28. Growing => real progress.
                    constexpr std::uint64_t kDriver = 0x710001142ff0ULL;
                    std::uint64_t zstream = 0;
                    if (memory->Read(kDriver + 0xf8, &zstream, 8) &&
                        zstream != 0) {
                        std::uint64_t total_out = 0;
                        std::uint64_t total_in = 0;
                        memory->Read(zstream + 0x28, &total_out, 8);
                        memory->Read(zstream + 0x10, &total_in, 8);
                        std::printf(
                            "ISOMR_INFLATE_PROGRESS total_in=%llu total_out=%llu (of 211b000)\n",
                            static_cast<unsigned long long>(total_in),
                            static_cast<unsigned long long>(total_out));
                        std::fflush(stdout);
                    }
                }
                if (nanosleep_calls == 20000) {
                    // The ForceLoadGroup poll frame: its x29 sits two frames
                    // above nanosleep (nanosleep -> ThreadSleep ->
                    // ForceLoadGroup). The manager this was spilled at
                    // x29-0x48; dump its queue state (read-only).
                    std::uint64_t fp = registers->callee_saved_general[10];
                    for (int depth = 0; depth < 4 && fp != 0; ++depth) {
                        std::array<std::uint64_t, 2> record{};
                        if (!memory->Read(fp, record.data(), sizeof(record)))
                            break;
                        if (record[1] == 0x4001eb1bd8ULL) {
                            const std::uint64_t force_frame = record[0];
                            std::uint64_t manager = 0;
                            memory->Read(force_frame - 0x48, &manager, 8);
                            if (manager != 0) {
                                std::uint64_t list_head[2] = {};
                                std::uint64_t vec_b[3] = {};
                                std::uint64_t vec_c[3] = {};
                                memory->Read(manager + 0x98, list_head, 16);
                                memory->Read(manager + 0xb0, vec_b, 24);
                                memory->Read(manager + 0xc8, vec_c, 24);
                                const bool list_empty =
                                    list_head[0] == list_head[1] ||
                                    list_head[0] == 0;
                                std::uint64_t pool_object = 0;
                                if (vec_b[1] - vec_b[0] >= 8) {
                                    memory->Read(vec_b[0], &pool_object, 8);
                                }
                                std::uint64_t pool_vptr = 0;
                                if (pool_object != 0) {
                                    memory->Read(pool_object, &pool_vptr, 8);
                                }
                                // Task + driver decompress-buffer state.
                                std::uint64_t task_driver = 0;
                                std::uint8_t task_flag50 = 0;
                                std::uint64_t task_buf40 = 0;
                                if (pool_object != 0) {
                                    memory->Read(pool_object + 0x18,
                                                 &task_driver, 8);
                                    memory->Read(pool_object + 0x50,
                                                 &task_flag50, 1);
                                    memory->Read(pool_object + 0x40,
                                                 &task_buf40, 8);
                                }
                                std::uint64_t res_vector[3] = {};
                                std::uint8_t driver_active = 0;
                                if (task_driver != 0) {
                                    memory->Read(task_driver + 0x180,
                                                 res_vector, 24);
                                    memory->Read(task_driver + 0xc,
                                                 &driver_active, 1);
                                }
                                std::uint64_t first_resource = 0;
                                std::uint64_t first_vptr = 0;
                                if (res_vector[1] - res_vector[0] >= 8) {
                                    memory->Read(res_vector[0],
                                                 &first_resource, 8);
                                    if (first_resource != 0) {
                                        memory->Read(first_resource,
                                                     &first_vptr, 8);
                                    }
                                }
                                std::fprintf(
                                    stderr,
                                    "ISOMR_RSM_LIVE manager=%llx listempty=%d vecb=%lld vecc=%lld pool=%llx poolvptr=%llx\\n",
                                    static_cast<unsigned long long>(manager),
                                    list_empty ? 1 : 0,
                                    static_cast<long long>(vec_b[1] -
                                                           vec_b[0]),
                                    static_cast<long long>(vec_c[1] -
                                                           vec_c[0]),
                                    static_cast<unsigned long long>(
                                        pool_object),
                                    static_cast<unsigned long long>(
                                        pool_vptr));
for (int slot = 0x10; slot <= 0xb0; slot += 8) {
                                    std::uint64_t slot_addr =
                                        0x4002e7bda8ULL +
                                        static_cast<std::uint64_t>(slot);
                                    std::uint64_t fn = 0;
                                    memory->Read(slot_addr, &fn, 8);
                                    std::fprintf(stderr,
                                                 "ISOMR_DRV_SLOT +%x = %llx\n",
                                                 slot,
                                                 static_cast<unsigned long long>(fn));
                                }
                                for (int r = 0; r < 4; ++r) {
                                    std::uint64_t res = 0;
                                    memory->Read(res_vector[0] + static_cast<std::uint64_t>(r) * 8, &res, 8);
                                    if (res == 0) continue;
                                    std::uint64_t rv = 0;
                                    std::uint32_t rc = 0;
                                    memory->Read(res, &rv, 8);
                                    memory->Read(res + 8, &rc, 4);
                                    std::fprintf(stderr, "ISOMR_RES[%d] ptr=%llx vptr=%llx refcount=%u\n", r, static_cast<unsigned long long>(res), static_cast<unsigned long long>(rv), rc);
                                }
std::uint64_t task_in_res = 0;
                                std::uint64_t task_out_res = 0;
                                memory->Read(pool_object + 0x30, &task_in_res, 8);
                                memory->Read(pool_object + 0x38, &task_out_res, 8);
                                std::uint64_t in_size = 0;
                                std::uint64_t out_size = 0;
                                if (task_in_res != 0) memory->Read(task_in_res + 0x30, &in_size, 8);
                                if (task_out_res != 0) memory->Read(task_out_res + 0x30, &out_size, 8);
                                std::fprintf(stderr, "ISOMR_TASK_BUFS inres=%llx insize=%llu outres=%llx outsize=%llu\n",
                                    static_cast<unsigned long long>(task_in_res),
                                    static_cast<unsigned long long>(in_size),
                                    static_cast<unsigned long long>(task_out_res),
                                    static_cast<unsigned long long>(out_size));
                                                                                                std::fprintf(
                                    stderr,
                                    "ISOMR_TASK_STATE driver=%llx flag50=%02x buf40=%llx active=%02x resvec=%lld firstres=%llx resvptr=%llx\\n",
                                    static_cast<unsigned long long>(
                                        task_driver),
                                    task_flag50,
                                    static_cast<unsigned long long>(
                                        task_buf40),
                                    driver_active,
                                    static_cast<long long>(res_vector[1] -
                                                           res_vector[0]),
                                    static_cast<unsigned long long>(
                                        first_resource),
                                    static_cast<unsigned long long>(
                                        first_vptr));
                                std::fflush(stderr);
                            }
                            break;
                        }
                        if (record[0] <= fp) break;
                        fp = record[0];
                    }
                }

            }
            NanosleepAdmission admission{};
            if (requested_nanoseconds == 500000ULL) {
                admission = AdmitNanosleep(
                    registers->callee_saved_general.back(),
                    registers->callee_saved_general[10]);
            }
            NanosleepCompletionGuard completion(this, admission);
            if (!SleepForNanoseconds(requested_nanoseconds,
                                     handler_error)) {
                return false;
            }
            if (remainder != 0) {
                constexpr BionicTimespec kNoRemainingTime{0, 0};
                if (!memory->Write(remainder, &kNoRemainingTime,
                                   sizeof(kNoRemainingTime))) {
                    return ReturnTimeError(
                        threads, kErrnoFault, registers, handler_error,
                        "bionic nanosleep remainder became invalid");
                }
            }
            (*registers)[0] = 0;
            registers->nanosleep_requested_duration_diagnostic.Publish(
                requested_nanoseconds);
            completion.Succeed();
            return true;
        },
        error))
        return false;
    return imports->Register(
        "usleep",
        [this](Aarch64ImportRegistry::Registers* registers,
               std::string* handler_error) {
            constexpr std::uint64_t kNanosecondsPerMicrosecond = 1000;
            const std::uint64_t microseconds =
                static_cast<std::uint32_t>((*registers)[0]);
            if (!SleepForNanoseconds(
                    microseconds * kNanosecondsPerMicrosecond,
                    handler_error))
                return false;
            (*registers)[0] = 0;
            return true;
        },
        error);
}

bool BionicTime::AccumulateNanosleepCallsiteTelemetryForTesting(
    NanosleepCallsiteTelemetryBucket* target,
    std::uint64_t requested_ns, std::uint64_t handler_body_ns,
    NanosleepCallsiteTelemetryOverflow* overflow) noexcept {
    if (target == nullptr || overflow == nullptr) return false;
    constexpr std::uint64_t kMaximum =
        std::numeric_limits<std::uint64_t>::max();
    bool valid = true;
    if (target->success_count == kMaximum) {
        overflow->count = true;
        valid = false;
    }
    if (requested_ns > kMaximum - target->requested_total_ns) {
        overflow->requested_total = true;
        valid = false;
    }
    if (handler_body_ns > kMaximum - target->handler_body_total_ns) {
        overflow->handler_body_total = true;
        valid = false;
    }
    if (!valid) return false;
    ++target->success_count;
    target->requested_total_ns += requested_ns;
    target->requested_max_ns =
        std::max(target->requested_max_ns, requested_ns);
    target->handler_body_total_ns += handler_body_ns;
    target->handler_body_max_ns =
        std::max(target->handler_body_max_ns, handler_body_ns);
    return true;
}

NanosleepCallsiteTelemetryToken
BionicTime::BeginNanosleepCallsiteTelemetry() noexcept {
    if (!nanosleep_callsite_telemetry_enabled_) return {};
    try {
        const std::lock_guard lock(nanosleep_callsite_mutex_);
        if (nanosleep_callsite_accepting_ ||
            nanosleep_callsite_in_flight_ != 0 ||
            nanosleep_callsite_generation_ ==
                std::numeric_limits<std::uint64_t>::max())
            return {};
        ++nanosleep_callsite_generation_;
        nanosleep_callsite_snapshot_ = {};
        nanosleep_callsite_snapshot_.enabled = true;
        nanosleep_callsite_snapshot_.generation =
            nanosleep_callsite_generation_;
        nanosleep_callsite_accepting_ = true;
        return {nanosleep_callsite_generation_, true};
    } catch (...) {
        return {};
    }
}

NanosleepCallsiteTelemetrySnapshot
BionicTime::EndNanosleepCallsiteTelemetry(
    NanosleepCallsiteTelemetryToken token) noexcept {
    if (!token.active || !nanosleep_callsite_telemetry_enabled_) return {};
    try {
        std::unique_lock lock(nanosleep_callsite_mutex_);
        if (token.generation != nanosleep_callsite_generation_ ||
            !nanosleep_callsite_accepting_)
            return {};
        nanosleep_callsite_accepting_ = false;
        nanosleep_callsite_condition_.wait(
            lock, [this]() { return nanosleep_callsite_in_flight_ == 0; });
        nanosleep_callsite_snapshot_.complete =
            !nanosleep_callsite_snapshot_.overflow.table &&
            !nanosleep_callsite_snapshot_.overflow.admission &&
            !nanosleep_callsite_snapshot_.overflow.count &&
            !nanosleep_callsite_snapshot_.overflow.requested_total &&
            !nanosleep_callsite_snapshot_.overflow.handler_body_total;
        return nanosleep_callsite_snapshot_;
    } catch (...) {
        return {};
    }
}

BionicTime::NanosleepAdmission BionicTime::AdmitNanosleep(
    std::uint64_t guest_link_register,
    std::uint64_t guest_frame_pointer) noexcept {
    if (!nanosleep_callsite_telemetry_enabled_) return {};
    NanosleepAdmission admission{};
    try {
        const std::optional<std::uint32_t> logical_thread_id =
            threads_ == nullptr ? std::nullopt
                                : threads_->CurrentGuestThreadId();
        admission.started = nanosleep_telemetry_clock_
                                ? nanosleep_telemetry_clock_()
                                : std::chrono::steady_clock::now();
        {
            const std::lock_guard lock(nanosleep_callsite_mutex_);
            if (!nanosleep_callsite_accepting_) return {};
            if (!logical_thread_id ||
                nanosleep_callsite_in_flight_ ==
                    std::numeric_limits<std::size_t>::max()) {
                nanosleep_callsite_snapshot_.overflow.admission = true;
                return {};
            }
            ++nanosleep_callsite_in_flight_;
            admission.generation = nanosleep_callsite_generation_;
            admission.guest_link_register = guest_link_register;
            admission.logical_thread_id = *logical_thread_id;
            admission.active = true;
        }
        struct GuestFrameRecord {
            std::uint64_t previous_frame_pointer = 0;
            std::uint64_t saved_link_register = 0;
        };
        static_assert(sizeof(GuestFrameRecord) == 16);
        GuestFrameRecord frame{};
        if (guest_frame_pointer != 0 &&
            (guest_frame_pointer & 15U) == 0 && heap_ != nullptr &&
            memory_ != nullptr &&
            heap_->IsReadableRange(guest_frame_pointer, sizeof(frame))) {
            nanosleep_frame_read_count_for_testing_.fetch_add(
                1, std::memory_order_relaxed);
            if (memory_->Read(guest_frame_pointer, &frame, sizeof(frame)) &&
                frame.saved_link_register != 0) {
                admission.parent_guest_link_register =
                    frame.saved_link_register;
                admission.parent_valid = true;
            }
        }
        return admission;
    } catch (...) {
        if (admission.active) CompleteNanosleep(admission, false);
        return {};
    }
}

void BionicTime::CompleteNanosleep(NanosleepAdmission admission,
                                   bool succeeded) noexcept {
    if (!admission.active) return;
    std::uint64_t elapsed_ns = 0;
    try {
        const auto finished = nanosleep_telemetry_clock_
                                  ? nanosleep_telemetry_clock_()
                                  : std::chrono::steady_clock::now();
        const auto elapsed = finished - admission.started;
        const auto count = std::chrono::duration_cast<
            std::chrono::nanoseconds>(elapsed).count();
        if (count > 0) elapsed_ns = static_cast<std::uint64_t>(count);
    } catch (...) {
        succeeded = false;
    }
    try {
        const std::lock_guard lock(nanosleep_callsite_mutex_);
        if (admission.generation == nanosleep_callsite_generation_ &&
            succeeded) {
            const bool aggregate_ok =
                AccumulateNanosleepCallsiteTelemetryForTesting(
                    &nanosleep_callsite_snapshot_.aggregate, 500000ULL,
                    elapsed_ns, &nanosleep_callsite_snapshot_.overflow);
            NanosleepCallsiteTelemetryBucket* target = nullptr;
            for (std::size_t index = 0;
                 index < nanosleep_callsite_snapshot_.retained_count;
                 ++index) {
                NanosleepCallsiteTelemetryBucket& entry =
                    nanosleep_callsite_snapshot_.retained[index];
                if (entry.guest_link_register ==
                        admission.guest_link_register &&
                    entry.parent_valid == admission.parent_valid &&
                    (!entry.parent_valid ||
                     entry.parent_guest_link_register ==
                         admission.parent_guest_link_register) &&
                    entry.logical_thread_id ==
                        admission.logical_thread_id) {
                    target = &entry;
                    break;
                }
            }
            if (target == nullptr &&
                nanosleep_callsite_snapshot_.retained_count <
                    nanosleep_callsite_snapshot_.retained.size()) {
                target = &nanosleep_callsite_snapshot_.retained[
                    nanosleep_callsite_snapshot_.retained_count++];
                target->guest_link_register =
                    admission.guest_link_register;
                target->parent_guest_link_register =
                    admission.parent_guest_link_register;
                target->parent_valid = admission.parent_valid;
                target->logical_thread_id =
                    admission.logical_thread_id;
            }
            if (target == nullptr) {
                nanosleep_callsite_snapshot_.overflow.table = true;
                target = &nanosleep_callsite_snapshot_.residual;
            }
            if (aggregate_ok) {
                (void)AccumulateNanosleepCallsiteTelemetryForTesting(
                    target, 500000ULL, elapsed_ns,
                    &nanosleep_callsite_snapshot_.overflow);
            }
        }
        if (nanosleep_callsite_in_flight_ != 0)
            --nanosleep_callsite_in_flight_;
        if (!nanosleep_callsite_accepting_ &&
            nanosleep_callsite_in_flight_ == 0)
            nanosleep_callsite_condition_.notify_all();
    } catch (...) {
        // Diagnostic completion is failure-isolated. The production mutex
        // implementation is non-throwing; this guard prevents propagation.
    }
}

bool BionicTime::ValidateConfiguration(std::string* error) {
    if (nanosleep_callsite_telemetry_enabled_) {
        std::puts(
            "ISOMR_NANOSLEEP_CALLSITE_CONFIG caller=module-relative parent=validated-one-frame-x29-record host-addresses=none top=8 per-request-lines=off");
    }
    if (nanosleep_backend_ == BionicNanosleepBackend::kSleepFor) {
        std::puts(
            "ISOMR_NANOSLEEP_BACKEND requested=sleep-for effective=sleep-for");
        return true;
    }
    const bool any_api =
        static_cast<bool>(waitable_timer_api_.create_high_resolution) ||
        static_cast<bool>(waitable_timer_api_.set_relative_100ns) ||
        static_cast<bool>(waitable_timer_api_.wait) ||
        static_cast<bool>(waitable_timer_api_.close);
    if (!any_api) waitable_timer_api_ = NativeWaitableTimerApi();
    if (!waitable_timer_api_.create_high_resolution ||
        !waitable_timer_api_.set_relative_100ns ||
        !waitable_timer_api_.wait || !waitable_timer_api_.close) {
        std::puts(
            "ISOMR_NANOSLEEP_BACKEND requested=high-resolution-waitable effective=unsupported");
        SetError(error, "high-resolution waitable timer API is incomplete");
        return false;
    }
    const BionicWaitableTimerCreateResult created =
        waitable_timer_api_.create_high_resolution();
    if (created.handle == nullptr) {
        std::puts(
            "ISOMR_NANOSLEEP_BACKEND requested=high-resolution-waitable effective=unsupported");
        SetError(error, NativeError(
                            "high-resolution waitable timer capability probe",
                            created.native_error));
        return false;
    }
    LocalWaitableTimer timer(&waitable_timer_api_, created.handle);
    const BionicWaitableTimerResult closed = timer.Close();
    if (!closed.succeeded) {
        std::puts(
            "ISOMR_NANOSLEEP_BACKEND requested=high-resolution-waitable effective=unsupported");
        SetError(error,
                 NativeError("high-resolution waitable timer probe close",
                             closed.native_error));
        return false;
    }
    std::puts(
        "ISOMR_NANOSLEEP_BACKEND requested=high-resolution-waitable effective=high-resolution-waitable");
    return true;
}

bool BionicTime::AttachCurrentThread(std::string* error) {
    if (heap_ == nullptr || threads_ == nullptr || shutdown_) {
        SetError(error, "bionic time is unavailable");
        return false;
    }
    const std::optional<std::uint32_t> id =
        threads_->CurrentGuestThreadId();
    if (!id) {
        SetError(error, "current thread is not attached to bionic time");
        return false;
    }
    const std::lock_guard lock(mutex_);
    if (results_.contains(*id)) return true;
    const std::uint64_t address = heap_->Callocate(1, sizeof(BionicTm));
    if (address == 0) {
        SetError(error, "guest localtime result allocation failed");
        return false;
    }
    results_.emplace(*id, address);
    return true;
}

bool BionicTime::DetachCurrentThread(std::string* error) {
    if (heap_ == nullptr || threads_ == nullptr || shutdown_) {
        SetError(error, "bionic time is unavailable");
        return false;
    }
    const std::optional<std::uint32_t> id =
        threads_->CurrentGuestThreadId();
    if (!id) {
        SetError(error, "current thread is not attached to bionic time");
        return false;
    }
    const std::lock_guard lock(mutex_);
    const auto found = results_.find(*id);
    if (found == results_.end()) {
        SetError(error, "current thread has no guest localtime result");
        return false;
    }
    if (!heap_->Release(found->second, error)) return false;
    results_.erase(found);
    return true;
}

bool BionicTime::Localtime(std::uint64_t input, std::uint64_t* result,
                           std::string* error) {
    if (result == nullptr) {
        SetError(error, "bionic localtime result pointer is null");
        return false;
    }
    *result = 0;
    std::int64_t seconds = 0;
    if (input == 0 || !memory_->Read(input, &seconds, sizeof(seconds))) {
        return threads_->SetCurrentErrno(kErrnoFault, error);
    }
    SYSTEMTIME local{};
    std::int64_t utc_offset = 0;
    std::int32_t daylight = 0;
    if (!LocalSystemTime(seconds, &local, &utc_offset, &daylight))
        return threads_->SetCurrentErrno(kErrnoOverflow, error);

    const std::optional<std::uint32_t> id =
        threads_->CurrentGuestThreadId();
    if (!id) {
        SetError(error, "current thread is not attached to bionic time");
        return false;
    }
    const BionicTm value{
        local.wSecond,
        local.wMinute,
        local.wHour,
        local.wDay,
        static_cast<std::int32_t>(local.wMonth) - 1,
        static_cast<std::int32_t>(local.wYear) - 1900,
        local.wDayOfWeek,
        YearDay(local),
        daylight,
        0,
        utc_offset,
        0,
    };
    const std::lock_guard lock(mutex_);
    const auto found = results_.find(*id);
    if (found == results_.end()) {
        SetError(error, "current thread has no guest localtime result");
        return false;
    }
    if (!memory_->Write(found->second, &value, sizeof(value))) {
        (void)threads_->SetCurrentErrno(kErrnoFault, error);
        SetError(error, "guest localtime result storage is invalid");
        return false;
    }
    *result = found->second;
    return true;
}

bool BionicTime::Gmtime(std::uint64_t input, std::uint64_t* result,
                        std::string* error) {
    if (result == nullptr) {
        SetError(error, "bionic gmtime result pointer is null");
        return false;
    }
    *result = 0;
    std::int64_t seconds = 0;
    if (input == 0 || !memory_->Read(input, &seconds, sizeof(seconds))) {
        return threads_->SetCurrentErrno(kErrnoFault, error);
    }
    FILETIME utc_file{};
    SYSTEMTIME utc{};
    if (!UnixTimeToFileTime(seconds, &utc_file) ||
        FileTimeToSystemTime(&utc_file, &utc) == FALSE)
        return threads_->SetCurrentErrno(kErrnoOverflow, error);

    const std::optional<std::uint32_t> id =
        threads_->CurrentGuestThreadId();
    if (!id) {
        SetError(error, "current thread is not attached to bionic time");
        return false;
    }
    const BionicTm value{
        utc.wSecond,
        utc.wMinute,
        utc.wHour,
        utc.wDay,
        static_cast<std::int32_t>(utc.wMonth) - 1,
        static_cast<std::int32_t>(utc.wYear) - 1900,
        utc.wDayOfWeek,
        YearDay(utc),
        0,
        0,
        0,
        0,
    };
    const std::lock_guard lock(mutex_);
    const auto found = results_.find(*id);
    if (found == results_.end()) {
        SetError(error, "current thread has no guest gmtime result");
        return false;
    }
    if (!memory_->Write(found->second, &value, sizeof(value))) {
        (void)threads_->SetCurrentErrno(kErrnoFault, error);
        SetError(error, "guest gmtime result storage is invalid");
        return false;
    }
    *result = found->second;
    return true;
}

std::size_t BionicTime::result_count() const {
    const std::lock_guard lock(mutex_);
    return results_.size();
}

bool BionicTime::Shutdown(std::string* error) {
    const std::lock_guard lock(mutex_);
    if (shutdown_) {
        SetError(error, "bionic time is already shut down");
        return false;
    }
    if (!results_.empty()) {
        SetError(error, "bionic time has attached result storage");
        return false;
    }
    shutdown_ = true;
    return true;
}

}  // namespace isomr
