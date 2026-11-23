#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>

#include "bionic_process.h"

#include "bionic_files.h"
#include "bionic_sync.h"
#include "bionic_thread_state.h"
#include "bionic_threads.h"
#include "guest_address_space.h"
#include "guest_heap.h"
#include "import_registry.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <limits>
#include <map>
#include <mutex>
#include <new>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace isomr {
namespace {

constexpr std::uint64_t kAtPageSize = 6;
constexpr std::uint64_t kAtClockTicks = 17;
constexpr std::uint64_t kAtHardwareCapabilities = 16;
constexpr std::uint64_t kAtSecure = 23;
constexpr std::uint64_t kAtHardwareCapabilities2 = 26;
constexpr std::uint64_t kPageSize = 4096;
constexpr std::uint64_t kClockTicksPerSecond = 100;
constexpr int kNoEntry = 2;
constexpr int kNoSuchProcess = 3;
constexpr int kIoError = 5;
constexpr int kNoMemory = 12;
constexpr int kBadAddress = 14;
constexpr int kInvalidArgument = 22;
constexpr std::uint64_t kBionicScClockTicks = 0x06;
constexpr std::uint64_t kBionicScPageSize = 0x27;
constexpr std::uint64_t kBionicScPageSizeAlias = 0x28;
constexpr std::uint64_t kBionicScPhysicalPages = 0x62;
constexpr std::uint64_t kBionicScNprocessorsConf = 0x60;
constexpr std::uint64_t kBionicScNprocessorsOnln = 0x61;
constexpr std::uint64_t kAarch64Futex = 98;
constexpr std::uint64_t kAarch64Sysinfo = 99;
constexpr std::uint64_t kAarch64SchedSetAffinity = 122;
constexpr std::uint64_t kAarch64SchedGetAffinity = 123;
constexpr std::uint64_t kAarch64GetCpu = 168;
constexpr std::uint64_t kAarch64GetTid = 178;
constexpr std::uint64_t kAarch64ProcessVmReadv = 270;
constexpr std::uint64_t kFutexWaitPrivate = 0x80;
constexpr std::uint64_t kFutexWakePrivate = 0x81;
constexpr std::uint64_t kPrSetName = 15;
constexpr std::uint64_t kPrGetDumpable = 3;
constexpr std::uint64_t kPrSetDumpable = 4;
constexpr std::uint64_t kPrGetKeepcaps = 7;
constexpr std::uint64_t kPrSetKeepcaps = 8;
constexpr std::uint64_t kPrGetTiming = 13;
constexpr std::uint64_t kPrSetTiming = 14;
constexpr std::uint64_t kPrGetName = 16;
constexpr std::uint64_t kPrGetSeccomp = 21;
constexpr std::uint64_t kPrSetSeccomp = 22;
constexpr std::uint64_t kPrGetTsc = 25;
constexpr std::uint64_t kPrSetTsc = 26;
constexpr std::uint64_t kPrGetSecurebits = 27;
constexpr std::uint64_t kPrSetSecurebits = 28;
constexpr std::uint64_t kPrSetTimerslack = 29;
constexpr std::uint64_t kPrGetTimerslack = 30;
constexpr std::uint64_t kPrTaskPerfEventsDisable = 31;
constexpr std::uint64_t kPrTaskPerfEventsEnable = 32;
constexpr std::uint64_t kPrSetMm = 35;
constexpr std::uint64_t kPrSetNoNewPrivs = 38;
constexpr std::uint64_t kPrGetNoNewPrivs = 39;
constexpr std::uint64_t kPrSetThpDisable = 41;
constexpr std::uint64_t kPrGetThpDisable = 42;
constexpr std::uint64_t kPrSetPtracer = 0x59616d61;
constexpr std::uint64_t kPrSetVma = 0x53564d41;
constexpr std::uint64_t kPrSetTaggedAddrCtrl = 0x41835;
constexpr std::uint64_t kPrGetTaggedAddrCtrl = 0x41836;
constexpr std::uint64_t kPriorityProcess = 0;
constexpr std::uint64_t kMaximumEnvironmentName = 4096;
constexpr std::uint64_t kGuestApplicationId = 10000;
constexpr std::uint64_t kMaximumIovecs = 1024;
constexpr std::uint64_t kMaximumProcessVmTransfer = 64U * 1024U * 1024U;

void SetError(std::string* error, std::string message) {
    if (error != nullptr) *error = std::move(message);
}

// Guest-controlled environment. The native product still exposes no host
// process environment, but variables a guest sets for itself (Unity sets
// several during boot) must round-trip through getenv or the guest re-sets /
// diverges on every read. Values live in guest memory so getenv returns a
// stable guest pointer; the map is keyed by name only.
std::mutex g_guest_env_mutex;
std::map<std::string, std::uint64_t> g_guest_env;

std::uint64_t GuestEnvSet(GuestAddressSpace* memory,
                          const std::string& name, const std::string& value,
                          bool overwrite) {
    if (name.empty() || name.find('=') != std::string::npos) return 22;
    GuestHeap* heap = memory->heap();
    if (heap == nullptr) return 12;
    const std::lock_guard<std::mutex> lock(g_guest_env_mutex);
    const auto existing = g_guest_env.find(name);
    if (existing != g_guest_env.end() && !overwrite) return 0;
    const std::uint64_t address = heap->Callocate(1, value.size() + 1);
    if (address == 0) return 12;
    if (!value.empty() &&
        !memory->Write(address, value.data(), value.size())) {
        std::string ignored;
        (void)heap->Release(address, &ignored);
        return 12;
    }
    g_guest_env[name] = address;
    return 0;
}

std::uint64_t GuestEnvReadName(GuestAddressSpace* memory,
                               std::uint64_t address, std::string* name,
                               std::string* error) {
    std::uint64_t length = 0;
    if (address == 0 ||
        !memory->BoundedStringLength(address, kMaximumEnvironmentName + 1,
                                     &length, error) ||
        length > kMaximumEnvironmentName) {
        SetError(error, "environment name is invalid or exceeds its bound");
        return 0;
    }
    name->assign(static_cast<std::size_t>(length), '\0');
    if (length != 0 &&
        !memory->Read(address, name->data(), name->size())) {
        SetError(error, "environment name could not be read");
        return 0;
    }
    return 1;
}

struct BionicIovec {
    std::uint64_t base = 0;
    std::uint64_t length = 0;
};

static_assert(sizeof(BionicIovec) == 16);

struct BionicPasswd {
    std::uint64_t name = 0;
    std::uint64_t password = 0;
    std::uint32_t uid = 0;
    std::uint32_t gid = 0;
    std::uint64_t gecos = 0;
    std::uint64_t directory = 0;
    std::uint64_t shell = 0;
};

static_assert(sizeof(BionicPasswd) == 48);

std::string ErrorMessage(int error_number) {
    switch (error_number) {
        case 1: return "Operation not permitted";
        case 2: return "No such file or directory";
        case 4: return "Interrupted system call";
        case 5: return "I/O error";
        case 9: return "Bad file descriptor";
        case 11: return "Try again";
        case 13: return "Permission denied";
        case 14: return "Bad address";
        case 17: return "File exists";
        case 21: return "Is a directory";
        case 22: return "Invalid argument";
        case 24: return "Too many open files";
        case 34: return "Numerical result out of range";
        case 36: return "File name too long";
        case 37: return "No locks available";
        case 39: return "Directory not empty";
        case 40: return "Too many symbolic links encountered";
        case 75: return "Value too large for defined data type";
        case 110: return "Connection timed out";
        default: return "Unknown error " + std::to_string(error_number);
    }
}

std::uint64_t Fnv1a(std::string_view value) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (const unsigned char byte : value) {
        hash ^= byte;
        hash *= 1099511628211ULL;
    }
    return hash;
}

std::optional<std::uint64_t> PhysicalPageCount() {
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof(status);
    if (!GlobalMemoryStatusEx(&status) || status.ullTotalPhys < kPageSize)
        return std::nullopt;
    return std::min<std::uint64_t>(
        status.ullTotalPhys / kPageSize,
        static_cast<std::uint64_t>(
            std::numeric_limits<std::int64_t>::max()));
}

bool ReturnSyscallError(BionicThreadState* threads, int error_number,
                        Aarch64ImportRegistry::Registers* registers,
                        std::string* error) {
    if (!threads->SetCurrentErrno(error_number, error)) return false;
    (*registers)[0] = UINT64_MAX;
    return true;
}

// Build a guest cpu_set_t for sched_getaffinity. ISOMR_CPU_COUNT overrides
// the reported processor count (matching sysconf/_SC_NPROCESSORS_*,
// /proc/cpuinfo, and /sys/devices/system/cpu/online): when set, the mask has
// only the low N bits populated so Unity's JobQueue fan-out sees the reduced
// count. Otherwise the real host process affinity mask is reported. Returns
// false only when the host affinity query itself fails (not in override mode).
bool GuestAffinityMaskBytes(std::size_t set_size,
                            std::vector<std::uint8_t>* mask) {
    mask->assign(set_size, 0);
    wchar_t cpu_flag[16] = {};
    unsigned override_count = 0;
    if (GetEnvironmentVariableW(L"ISOMR_CPU_COUNT", cpu_flag, 16) > 0)
        override_count = static_cast<unsigned>(_wtoi64(cpu_flag));
    if (override_count != 0) {
        for (std::size_t cpu = 0;
             cpu < override_count && (cpu >> 3) < mask->size(); ++cpu)
            (*mask)[cpu >> 3] |=
                static_cast<std::uint8_t>(1u << (cpu & 7));
        return true;
    }
    DWORD_PTR process_mask = 0;
    DWORD_PTR system_mask = 0;
    if (!GetProcessAffinityMask(GetCurrentProcess(), &process_mask,
                                &system_mask))
        return false;
    for (std::size_t byte = 0;
         byte < mask->size() && byte < sizeof(process_mask); ++byte)
        (*mask)[byte] = static_cast<std::uint8_t>(
            (process_mask >> (8 * byte)) & 0xff);
    return true;
}

std::optional<std::uint32_t> ResolvePriorityTarget(
    BionicThreadState* threads, std::uint64_t who) {
    if (who == 0) return threads->CurrentGuestThreadId();
    if (who > (std::numeric_limits<std::uint32_t>::max)())
        return std::nullopt;
    return static_cast<std::uint32_t>(who);
}

bool ReadIovecs(GuestAddressSpace* memory, std::uint64_t address,
                std::uint64_t count, std::vector<BionicIovec>* result) {
    if (result == nullptr || count > kMaximumIovecs ||
        count > std::numeric_limits<std::size_t>::max() /
                    sizeof(BionicIovec))
        return false;
    result->resize(static_cast<std::size_t>(count));
    return count == 0 ||
           (address != 0 &&
            memory->Read(address, result->data(),
                         result->size() * sizeof(BionicIovec)));
}

std::optional<std::uint64_t> IovecLength(
    const std::vector<BionicIovec>& vectors) {
    std::uint64_t total = 0;
    for (const BionicIovec& vector : vectors) {
        if (vector.length >
            static_cast<std::uint64_t>(
                std::numeric_limits<std::int64_t>::max()) - total)
            return std::nullopt;
        total += vector.length;
    }
    return total;
}

bool ProcessVmReadSelf(GuestAddressSpace* memory, BionicThreadState* threads,
                       Aarch64ImportRegistry::Registers* registers,
                       std::string* error) {
    const std::uint64_t process = (*registers)[1];
    const std::uint64_t local_address = (*registers)[2];
    const std::uint64_t local_count = (*registers)[3];
    const std::uint64_t remote_address = (*registers)[4];
    const std::uint64_t remote_count = (*registers)[5];
    const std::uint64_t flags = (*registers)[6];
    if (process != GetCurrentProcessId())
        return ReturnSyscallError(threads, kNoSuchProcess, registers, error);
    if (flags != 0 || local_count > kMaximumIovecs ||
        remote_count > kMaximumIovecs)
        return ReturnSyscallError(threads, kInvalidArgument, registers,
                                  error);
    std::vector<BionicIovec> local;
    std::vector<BionicIovec> remote;
    try {
        if (!ReadIovecs(memory, local_address, local_count, &local) ||
            !ReadIovecs(memory, remote_address, remote_count, &remote))
            return ReturnSyscallError(threads, kBadAddress, registers, error);
    } catch (const std::bad_alloc&) {
        return ReturnSyscallError(threads, kNoMemory, registers, error);
    }
    const std::optional<std::uint64_t> local_length = IovecLength(local);
    const std::optional<std::uint64_t> remote_length = IovecLength(remote);
    if (!local_length || !remote_length)
        return ReturnSyscallError(threads, kInvalidArgument, registers,
                                  error);
    const std::uint64_t transfer = std::min(*local_length, *remote_length);
    if (transfer == 0) {
        (*registers)[0] = 0;
        return true;
    }
    if (transfer > kMaximumProcessVmTransfer)
        return ReturnSyscallError(threads, kInvalidArgument, registers,
                                  error);

    constexpr std::size_t kChunkSize = 64U * 1024U;
    std::vector<std::uint8_t> buffer;
    try {
        buffer.resize(static_cast<std::size_t>(
            std::min<std::uint64_t>(transfer, kChunkSize)));
    } catch (const std::bad_alloc&) {
        return ReturnSyscallError(threads, kNoMemory, registers, error);
    }
    std::size_t local_index = 0;
    std::size_t remote_index = 0;
    std::uint64_t local_offset = 0;
    std::uint64_t remote_offset = 0;
    std::uint64_t copied = 0;
    while (copied < transfer) {
        while (local_index < local.size() &&
               local_offset == local[local_index].length) {
            ++local_index;
            local_offset = 0;
        }
        while (remote_index < remote.size() &&
               remote_offset == remote[remote_index].length) {
            ++remote_index;
            remote_offset = 0;
        }
        if (local_index == local.size() || remote_index == remote.size())
            break;
        const std::uint64_t amount = std::min<std::uint64_t>(
            {transfer - copied,
             local[local_index].length - local_offset,
             remote[remote_index].length - remote_offset,
             buffer.size()});
        const bool local_overflow =
            local[local_index].base >
            std::numeric_limits<std::uint64_t>::max() - local_offset;
        const bool remote_overflow =
            remote[remote_index].base >
            std::numeric_limits<std::uint64_t>::max() - remote_offset;
        if (amount == 0 || local_overflow || remote_overflow ||
            !memory->Read(remote[remote_index].base + remote_offset,
                          buffer.data(), static_cast<std::size_t>(amount)) ||
            !memory->Write(local[local_index].base + local_offset,
                           buffer.data(), static_cast<std::size_t>(amount))) {
            if (copied == 0)
                return ReturnSyscallError(threads, kBadAddress, registers,
                                          error);
            (*registers)[0] = copied;
            return true;
        }
        copied += amount;
        local_offset += amount;
        remote_offset += amount;
    }
    (*registers)[0] = copied;
    return true;
}

}  // namespace

std::uint64_t GuestEnvSetVar(GuestAddressSpace* memory,
                             const std::string& name,
                             const std::string& value, bool overwrite) {
    return GuestEnvSet(memory, name, value, overwrite);
}

bool BionicProcessIdentity::GetPasswd(std::uint64_t uid,
                                      std::uint64_t* result,
                                      std::string* error) {
    if (result == nullptr || heap_ == nullptr) {
        SetError(error, "bionic passwd identity is not configured");
        return false;
    }
    if (uid != kGuestApplicationId) {
        *result = 0;
        return true;
    }
    const std::lock_guard lock(mutex_);
    if (passwd_ == 0) {
        constexpr char kName[] = "isomr";
        constexpr char kDirectory[] = "/data";
        constexpr char kShell[] = "";
        constexpr std::size_t kNameOffset = sizeof(BionicPasswd);
        constexpr std::size_t kDirectoryOffset =
            kNameOffset + sizeof(kName);
        constexpr std::size_t kShellOffset =
            kDirectoryOffset + sizeof(kDirectory);
        constexpr std::size_t kStorageSize =
            kShellOffset + sizeof(kShell);
        heap_->SetAllocDebugTag("process-passwd");
        const std::uint64_t allocation = heap_->Callocate(1, kStorageSize);
        heap_->SetAllocDebugTag(nullptr);
        if (allocation == 0) {
            SetError(error, "bionic passwd allocation failed");
            return false;
        }
        const BionicPasswd passwd{
            allocation + kNameOffset,
            0,
            static_cast<std::uint32_t>(kGuestApplicationId),
            static_cast<std::uint32_t>(kGuestApplicationId),
            0,
            allocation + kDirectoryOffset,
            allocation + kShellOffset};
        if (!heap_->Write(allocation, &passwd, sizeof(passwd)) ||
            !heap_->Write(allocation + kNameOffset, kName,
                          sizeof(kName)) ||
            !heap_->Write(allocation + kDirectoryOffset, kDirectory,
                          sizeof(kDirectory)) ||
            !heap_->Write(allocation + kShellOffset, kShell,
                          sizeof(kShell))) {
            std::string ignored;
            (void)heap_->Release(allocation, &ignored);
            SetError(error, "bionic passwd storage initialization failed");
            return false;
        }
        passwd_ = allocation;
    }
    *result = passwd_;
    return true;
}

bool BionicProcessIdentity::Shutdown(std::string* error) {
    const std::lock_guard lock(mutex_);
    if (passwd_ == 0) return true;
    if (!heap_->Release(passwd_, error)) return false;
    passwd_ = 0;
    return true;
}

bool RegisterBionicProcessImports(Aarch64ImportRegistry* registry,
                                  GuestAddressSpace* memory,
                                  BionicFileSystem* files,
                                  BionicThreadState* threads,
                                  BionicSync* sync,
                                  BionicProcessIdentity* identity,
                                  BionicThreads* thread_manager,
                                  std::string* error) {
    if (registry == nullptr || memory == nullptr || files == nullptr ||
        threads == nullptr || sync == nullptr || identity == nullptr ||
        thread_manager == nullptr)
        return false;
    const auto register_exit = [&](const char* name) {
        return registry->Register(
            name,
            [thread_manager, memory](Aarch64ImportRegistry::Registers* registers,
                             std::string*) {
                // _exit never returns: flag every thread's session
                // cancellation so each run loop halts at its next boundary
                // and the caller can unwind as a guest process exit.
                {
                    unsigned long long fp = (unsigned long long)(*registers)[29];
                    std::string chain;
                    for (int i = 0; i < 10 && fp != 0; ++i) {
                        unsigned long long prev = 0, ra = 0;
                        if (!memory->Read(fp, &prev, 8) ||
                            !memory->Read(fp + 8, &ra, 8))
                            break;
                        char buf[32];
                        std::snprintf(buf, sizeof buf, " ra=%llx", ra);
                        chain += buf;
                        fp = prev;
                    }
                    fprintf(stderr,
                            "ISOMR_EXIT_DIAG code=%d lr=%llx fp=%llx chain=%s\n",
                            static_cast<int>((*registers)[0]),
                            (unsigned long long)(*registers)[30], fp,
                            chain.c_str());
                }
                thread_manager->RequestProcessExit(
                    static_cast<int>((*registers)[0]));
                return true;
            },
            error);
    };
    if (!register_exit("_exit") || !register_exit("_Exit") ||
        !register_exit("exit") || !register_exit("exit_group") ||
        !register_exit("abort"))
        return false;
    if (!registry->Register(
        "getauxval",
        [threads](Aarch64ImportRegistry::Registers* registers,
                  std::string* dispatch_error) {
            switch ((*registers)[0]) {
                case kAtPageSize:
                    (*registers)[0] = kPageSize;
                    return true;
                case kAtClockTicks:
                    (*registers)[0] = kClockTicksPerSecond;
                    return true;
                case kAtHardwareCapabilities:
                case kAtHardwareCapabilities2:
                case kAtSecure:
                    // Advertise no optional AArch64 extensions until each is
                    // independently covered by the execution gate.
                    (*registers)[0] = 0;
                    return true;
                default:
                    if (!threads->SetCurrentErrno(kNoEntry,
                                                  dispatch_error))
                        return false;
                    (*registers)[0] = 0;
                    return true;
            }
        },
        error))
        return false;
    if (!registry->Register(
            "sysconf",
            [threads](Aarch64ImportRegistry::Registers* registers,
                      std::string* dispatch_error) {
                switch ((*registers)[0]) {
                    case kBionicScClockTicks:
                        (*registers)[0] = kClockTicksPerSecond;
                        return true;
                    case kBionicScPageSize:
                    case kBionicScPageSizeAlias:
                        (*registers)[0] = kPageSize;
                        return true;
                    case kBionicScNprocessorsConf:
                    case kBionicScNprocessorsOnln: {
                        // il2cpp's Environment.ProcessorCount feeds
                        // ConcurrentDictionary's concurrencyLevel, which must
                        // be positive. ISOMR_CPU_COUNT overrides the count so
                        // Unity's JobQueue worker fan-out can be A/B tested
                        // (a low count steers it toward inline execution);
                        // otherwise report the real host CPU count.
                        wchar_t cpu_flag[16] = {};
                        if (GetEnvironmentVariableW(L"ISOMR_CPU_COUNT",
                                                    cpu_flag, 16) > 0) {
                            const unsigned parsed =
                                static_cast<unsigned>(_wtoi64(cpu_flag));
                            if (parsed != 0) {
                                (*registers)[0] = parsed;
                                return true;
                            }
                        }
                        SYSTEM_INFO system_info{};
                        GetNativeSystemInfo(&system_info);
                        (*registers)[0] =
                            system_info.dwNumberOfProcessors != 0
                                ? static_cast<std::uint64_t>(
                                      system_info.dwNumberOfProcessors)
                                : 1;
                        return true;
                    }
                    case kBionicScPhysicalPages: {
                        const std::optional<std::uint64_t> pages =
                            PhysicalPageCount();
                        if (!pages) {
                            if (!threads->SetCurrentErrno(
                                    kIoError, dispatch_error))
                                return false;
                            (*registers)[0] = UINT64_MAX;
                            return true;
                        }
                        (*registers)[0] = *pages;
                        if (dispatch_error != nullptr)
                            *dispatch_error =
                                "trace:sysconf-physical-pages=" +
                                std::to_string(*pages);
                        return true;
                    }
                    default:
                        if (!threads->SetCurrentErrno(kInvalidArgument,
                                                      dispatch_error))
                            return false;
                        (*registers)[0] = UINT64_MAX;
                        return true;
                }
            },
            error))
        return false;
    if (!registry->Register(
            "getpagesize",
            [](Aarch64ImportRegistry::Registers* registers,
               std::string*) {
                (*registers)[0] = kPageSize;
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "prctl",
            [memory, threads](
                Aarch64ImportRegistry::Registers* registers,
                std::string* dispatch_error) {
                const std::uint64_t option = (*registers)[0];
                // PR_SET_NAME stores at most 15 bytes plus terminator and
                // truncates longer names.
                if (option == kPrSetName) {
                    if ((*registers)[1] == 0)
                        return ReturnSyscallError(threads, kBadAddress,
                                                  registers,
                                                  dispatch_error);
                    std::uint64_t length = 0;
                    std::string read_error;
                    if (!memory->BoundedStringLength((*registers)[1], 16,
                                                     &length,
                                                     &read_error))
                        return ReturnSyscallError(threads, kBadAddress,
                                                  registers,
                                                  dispatch_error);
                    length = std::min<std::uint64_t>(length, 15);
                    std::string name(static_cast<std::size_t>(length), '\0');
                    if (length != 0 &&
                        !memory->Read((*registers)[1], name.data(),
                                      name.size()))
                        return ReturnSyscallError(threads, kBadAddress,
                                                  registers,
                                                  dispatch_error);
                    const int result =
                        threads->SetCurrentThreadName(name, dispatch_error);
                    if (result != 0)
                        return ReturnSyscallError(threads, result, registers,
                                                  dispatch_error);
                    (*registers)[0] = 0;
                    return true;
                }
                if (option == kPrGetName) {
                    // Copy the current thread name into the caller buffer
                    // (arg2), 16 bytes, NUL-padded. Unknown names read as
                    // empty, matching a kernel that never saw PR_SET_NAME.
                    std::array<char, 16> name{};
                    if ((*registers)[1] == 0 ||
                        !memory->Write((*registers)[1], name.data(),
                                       name.size()))
                        return ReturnSyscallError(threads, kBadAddress,
                                                  registers,
                                                  dispatch_error);
                    (*registers)[0] = 0;
                    return true;
                }
                // GET options return their value in x0. Report real-kernel
                // defaults: dumpable=1, everything else off/zero.
                switch (option) {
                    case kPrGetDumpable:
                        (*registers)[0] = 1;
                        return true;
                    case kPrGetTimerslack:
                        (*registers)[0] = 50000;
                        return true;
                    case kPrGetKeepcaps:
                    case kPrGetTiming:
                    case kPrGetSeccomp:
                    case kPrGetTsc:
                    case kPrGetSecurebits:
                    case kPrGetNoNewPrivs:
                    case kPrGetThpDisable:
                    case kPrGetTaggedAddrCtrl:
                        (*registers)[0] = 0;
                        return true;
                    default:
                        break;
                }
                // SET options a real kernel accepts: hardening flags,
                // ptracer/vma tagging, no_new_privs, seccomp, timerslack.
                // They are no-ops under emulation but must not report EINVAL
                // — a real kernel accepts them, so rejecting them is a
                // detectable divergence for anti-tamper probes.
                switch (option) {
                    case kPrSetDumpable:
                    case kPrSetKeepcaps:
                    case kPrSetTiming:
                    case kPrSetTsc:
                    case kPrSetSecurebits:
                    case kPrSetTimerslack:
                    case kPrTaskPerfEventsDisable:
                    case kPrTaskPerfEventsEnable:
                    case kPrSetMm:
                    case kPrSetNoNewPrivs:
                    case kPrSetSeccomp:
                    case kPrSetThpDisable:
                    case kPrSetPtracer:
                    case kPrSetVma:
                    case kPrSetTaggedAddrCtrl:
                        (*registers)[0] = 0;
                        return true;
                    default:
                        break;
                }
                return ReturnSyscallError(threads, kInvalidArgument,
                                          registers, dispatch_error);
            },
            error))
        return false;
    if (!registry->Register(
            "getenv",
            [memory](Aarch64ImportRegistry::Registers* registers,
                     std::string* dispatch_error) {
                std::uint64_t length = 0;
                if ((*registers)[0] == 0 ||
                    !memory->BoundedStringLength(
                        (*registers)[0], kMaximumEnvironmentName + 1,
                        &length, dispatch_error) ||
                    length > kMaximumEnvironmentName) {
                    if (dispatch_error != nullptr)
                        *dispatch_error =
                            "getenv name is invalid or exceeds its bound";
                    (*registers)[0] = 0;
                    return false;
                }
                std::string name(static_cast<std::size_t>(length), '\0');
                if (length != 0 &&
                    !memory->Read((*registers)[0], name.data(), name.size())) {
                    if (dispatch_error != nullptr)
                        *dispatch_error = "getenv name could not be read";
                    (*registers)[0] = 0;
                    return false;
                }
                std::string category =
                    name == "HOME"               ? "HOME"
                    : name == "TMPDIR"           ? "TMPDIR"
                    : name == "ANDROID_DATA"     ? "ANDROID_DATA"
                    : name == "ANDROID_ROOT"     ? "ANDROID_ROOT"
                    : name == "EXTERNAL_STORAGE" ? "EXTERNAL_STORAGE"
                    : "other-len" + std::to_string(name.size()) + "-fnv" +
                          std::to_string(Fnv1a(name));
                // The native product exposes no host process environment to
                // guest code, but a variable the guest set for itself must
                // read back so guest code does not diverge. Consult the
                // guest-controlled map first; anything else stays empty.
                {
                    const std::lock_guard<std::mutex> lock(g_guest_env_mutex);
                    const auto found = g_guest_env.find(name);
                    if (found != g_guest_env.end()) {
                        (*registers)[0] = found->second;
                        return true;
                    }
                }
                (*registers)[0] = 0;
                if (dispatch_error != nullptr)
                    *dispatch_error = "trace:getenv=" + category;
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "setenv",
            [memory](Aarch64ImportRegistry::Registers* registers,
                     std::string* dispatch_error) {
                std::string name;
                if (!GuestEnvReadName(memory, (*registers)[0], &name,
                                      dispatch_error)) {
                    (*registers)[0] = std::numeric_limits<std::uint64_t>::max();
                    return true;
                }
                std::string value;
                const std::uint64_t value_address = (*registers)[1];
                if (value_address != 0) {
                    std::uint64_t length = 0;
                    if (!memory->BoundedStringLength(
                            value_address, kMaximumEnvironmentName + 1,
                            &length, dispatch_error) ||
                        length > kMaximumEnvironmentName) {
                        SetError(dispatch_error,
                                 "setenv value is invalid or exceeds its bound");
                        (*registers)[0] =
                            std::numeric_limits<std::uint64_t>::max();
                        return true;
                    }
                    value.assign(static_cast<std::size_t>(length), '\0');
                    if (length != 0 &&
                        !memory->Read(value_address, value.data(),
                                      value.size())) {
                        SetError(dispatch_error,
                                 "setenv value could not be read");
                        (*registers)[0] =
                            std::numeric_limits<std::uint64_t>::max();
                        return true;
                    }
                }
                const std::uint64_t result =
                    GuestEnvSet(memory, name, value, (*registers)[2] != 0);
                (*registers)[0] = result == 0
                    ? 0
                    : std::numeric_limits<std::uint64_t>::max();
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "unsetenv",
            [memory](Aarch64ImportRegistry::Registers* registers,
                     std::string* dispatch_error) {
                std::string name;
                if (!GuestEnvReadName(memory, (*registers)[0], &name,
                                      dispatch_error) ||
                    name.find('=') != std::string::npos) {
                    (*registers)[0] = std::numeric_limits<std::uint64_t>::max();
                    return true;
                }
                const std::lock_guard<std::mutex> lock(g_guest_env_mutex);
                g_guest_env.erase(name);
                (*registers)[0] = 0;
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "putenv",
            [memory](Aarch64ImportRegistry::Registers* registers,
                     std::string* dispatch_error) {
                std::string entry;
                const std::uint64_t entry_address = (*registers)[0];
                std::uint64_t length = 0;
                if (entry_address == 0 ||
                    !memory->BoundedStringLength(
                        entry_address, kMaximumEnvironmentName + 1,
                        &length, dispatch_error) ||
                    length > kMaximumEnvironmentName) {
                    SetError(dispatch_error,
                             "putenv string is invalid or exceeds its bound");
                    (*registers)[0] = std::numeric_limits<std::uint64_t>::max();
                    return true;
                }
                entry.assign(static_cast<std::size_t>(length), '\0');
                if (length != 0 &&
                    !memory->Read(entry_address, entry.data(), entry.size())) {
                    SetError(dispatch_error, "putenv string could not be read");
                    (*registers)[0] = std::numeric_limits<std::uint64_t>::max();
                    return true;
                }
                const std::size_t equals = entry.find('=');
                if (equals == std::string::npos) {
                    const std::lock_guard<std::mutex> lock(g_guest_env_mutex);
                    g_guest_env.erase(entry);
                    (*registers)[0] = 0;
                    return true;
                }
                const std::uint64_t result = GuestEnvSet(
                    memory, entry.substr(0, equals), entry.substr(equals + 1),
                    true);
                (*registers)[0] = result == 0
                    ? 0
                    : std::numeric_limits<std::uint64_t>::max();
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "clearenv",
            [](Aarch64ImportRegistry::Registers* registers, std::string*) {
                const std::lock_guard<std::mutex> lock(g_guest_env_mutex);
                g_guest_env.clear();
                (*registers)[0] = 0;
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "getpid",
            [](Aarch64ImportRegistry::Registers* registers,
               std::string*) {
                (*registers)[0] = GetCurrentProcessId();
                return true;
            },
            error))
        return false;
    // sched_get_priority_min/max: SCHED_OTHER (policy 0) is the only
    // scheduler the compat runtime models; its Linux priority range is a
    // flat 0..0. Other policies report the same flat range rather than
    // failing an information-only query.
    if (!registry->Register(
            "sched_get_priority_min",
            [](Aarch64ImportRegistry::Registers* registers, std::string*) {
                (*registers)[0] = 0;
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "sched_get_priority_max",
            [](Aarch64ImportRegistry::Registers* registers, std::string*) {
                (*registers)[0] = 0;
                return true;
            },
            error))
        return false;
    // sched_getaffinity(pid, cpusetsize, mask*): report the host process
    // affinity mask as the guest cpu_set_t so SystemInfo sees a real CPU
    // count. This is the libc entry point; the raw syscall path is handled
    // separately in the syscall dispatcher.
    if (!registry->Register(
            "sched_getaffinity",
            [memory](Aarch64ImportRegistry::Registers* registers,
                     std::string* /*dispatch_error*/) {
                const std::uint64_t set_size = (*registers)[1];
                const std::uint64_t mask_address = (*registers)[2];
                if (mask_address == 0) {
                    (*registers)[0] =
                        static_cast<std::uint64_t>(-14);  // EFAULT
                    return true;
                }
                std::vector<std::uint8_t> mask;
                if (!GuestAffinityMaskBytes(
                        static_cast<std::size_t>(set_size), &mask)) {
                    (*registers)[0] =
                        static_cast<std::uint64_t>(-5);  // EIO
                    return true;
                }
                if (!memory->Write(mask_address, mask.data(), mask.size())) {
                    (*registers)[0] =
                        static_cast<std::uint64_t>(-14);  // EFAULT
                    return true;
                }
                (*registers)[0] = 0;
                return true;
            },
            error))
        return false;
    // sched_setaffinity(pid, cpusetsize, mask*): guest CPU pinning is a hint we
    // cannot map to host thread affinity through the compat scheduler. Accept
    // it and report success so Unity's worker threads keep running unpinned.
    if (!registry->Register(
            "sched_setaffinity",
            [](Aarch64ImportRegistry::Registers* registers, std::string*) {
                (*registers)[0] = 0;
                return true;
            },
            error))
        return false;
    // uname(buf): report a plausible Android-on-aarch64 kernel identity.
    // struct utsname is six 65-byte fields (sysname, nodename, release,
    // version, machine, domainname); Unity reads release/machine during
    // device-info collection.
    if (!registry->Register(
            "uname",
            [memory](Aarch64ImportRegistry::Registers* registers,
                     std::string* /*dispatch_error*/) {
                const std::uint64_t buffer = (*registers)[0];
                std::array<std::uint8_t, 390> uts{};
                const auto write_field = [&uts](std::size_t offset,
                                                const char* text) {
                    const std::size_t length = std::strlen(text);
                    std::memcpy(uts.data() + offset, text,
                                length < 64 ? length : 64);
                };
                write_field(0, "Linux");
                write_field(65, "localhost");
                write_field(130, "5.10.107-android12-9-00000");
                write_field(195, "#1 SMP PREEMPT");
                write_field(260, "aarch64");
                write_field(325, "(none)");
                if (!memory->Write(buffer, uts.data(), uts.size())) {
                    (*registers)[0] =
                        static_cast<std::uint64_t>(-14);  // EFAULT
                    return true;
                }
                (*registers)[0] = 0;
                return true;
            },
            error))
        return false;
    // gethostname(name, len): report the same nodename uname() does; guest
    // network/device-init paths call it while collecting device info.
    if (!registry->Register(
            "gethostname",
            [memory](Aarch64ImportRegistry::Registers* registers,
                     std::string* /*dispatch_error*/) {
                const std::uint64_t buffer = (*registers)[0];
                const std::uint64_t length = (*registers)[1];
                static constexpr char kHost[] = "localhost";
                const std::size_t write =
                    length > sizeof(kHost) ? sizeof(kHost)
                                           : static_cast<std::size_t>(length);
                if (write != 0 &&
                    !memory->Write(buffer, kHost, write)) {
                    (*registers)[0] =
                        static_cast<std::uint64_t>(-14);  // EFAULT
                    return true;
                }
                (*registers)[0] = 0;
                return true;
            },
            error))
        return false;
    // inet_pton(af, src, dst): parse a dotted-quad IPv4 string into 4 bytes.
    // There is no guest socket layer; returning 0 ("not a valid address")
    // for anything else keeps callers on their non-network path.
    if (!registry->Register(
            "inet_pton",
            [memory](Aarch64ImportRegistry::Registers* registers,
                     std::string* /*dispatch_error*/) {
                constexpr std::uint64_t kAfInet = 2;
                const std::uint64_t source = (*registers)[1];
                const std::uint64_t destination = (*registers)[2];
                if ((*registers)[0] != kAfInet || source == 0 ||
                    destination == 0) {
                    (*registers)[0] = 0;
                    return true;
                }
                char text[64] = {};
                if (!memory->Read(source, text, sizeof(text) - 1)) {
                    (*registers)[0] = 0;
                    return true;
                }
                std::uint8_t bytes[4] = {};
                unsigned part = 0;
                int octet = 0;
                bool valid = true;
                for (const char* c = text; valid; ++c) {
                    if (*c >= '0' && *c <= '9') {
                        octet = octet * 10 + (*c - '0');
                        if (octet > 255) valid = false;
                    } else if (*c == '.' || *c == '\0') {
                        if (part > 3) { valid = false; break; }
                        bytes[part++] = static_cast<std::uint8_t>(octet);
                        octet = 0;
                        if (*c == '\0') break;
                    } else {
                        valid = false;
                    }
                }
                if (!valid || part != 4 ||
                    !memory->Write(destination, bytes, sizeof(bytes))) {
                    (*registers)[0] = 0;
                    return true;
                }
                (*registers)[0] = 1;
                return true;
            },
            error))
        return false;
    const auto register_identity = [&](const char* name) {
        return registry->Register(
            name,
            [](Aarch64ImportRegistry::Registers* registers,
               std::string*) {
                // Windows has no Unix UID/GID. Expose one stable, non-root
                // Android application identity to keep guest ownership and
                // privilege checks coherent without leaking the host SID.
                (*registers)[0] = kGuestApplicationId;
                return true;
            },
            error);
    };
    if (!register_identity("getuid") || !register_identity("geteuid") ||
        !register_identity("getgid") || !register_identity("getegid"))
        return false;
    if (!registry->Register(
            "getpwuid",
            [identity](Aarch64ImportRegistry::Registers* registers,
                       std::string* dispatch_error) {
                std::uint64_t result = 0;
                if (!identity->GetPasswd((*registers)[0], &result,
                                         dispatch_error))
                    return false;
                (*registers)[0] = result;
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "strerror_r",
            [memory](Aarch64ImportRegistry::Registers* registers,
                     std::string* dispatch_error) {
                constexpr std::uint64_t kRangeError = 34;
                const std::string message = ErrorMessage(
                    static_cast<int>((*registers)[0]));
                const std::uint64_t output = (*registers)[1];
                const std::uint64_t capacity = (*registers)[2];
                const std::size_t copied = capacity == 0
                    ? 0
                    : static_cast<std::size_t>(std::min<std::uint64_t>(
                          message.size(), capacity - 1));
                if (capacity != 0 &&
                    (output > std::numeric_limits<std::uint64_t>::max() -
                                  copied ||
                     (!message.empty() && copied != 0 &&
                      !memory->Write(output, message.data(), copied)) ||
                     !memory->Write(output + copied, "", 1))) {
                    if (dispatch_error != nullptr)
                        *dispatch_error =
                            "strerror_r output is outside guest memory";
                    return false;
                }
                (*registers)[0] = message.size() >= capacity
                                      ? kRangeError
                                      : 0;
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "getpriority",
            [threads](Aarch64ImportRegistry::Registers* registers,
                      std::string* dispatch_error) {
                if ((*registers)[0] != kPriorityProcess)
                    return ReturnSyscallError(
                        threads, kInvalidArgument, registers,
                        dispatch_error);
                const std::optional<std::uint32_t> target =
                    ResolvePriorityTarget(threads, (*registers)[1]);
                const std::optional<int> priority =
                    target ? threads->GuestThreadNice(*target)
                           : std::nullopt;
                if (!priority)
                    return ReturnSyscallError(
                        threads, kNoSuchProcess, registers,
                        dispatch_error);
                (*registers)[0] = static_cast<std::uint64_t>(
                    static_cast<std::int64_t>(*priority));
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "setpriority",
            [threads](Aarch64ImportRegistry::Registers* registers,
                      std::string* dispatch_error) {
                if ((*registers)[0] != kPriorityProcess)
                    return ReturnSyscallError(
                        threads, kInvalidArgument, registers,
                        dispatch_error);
                const std::optional<std::uint32_t> target =
                    ResolvePriorityTarget(threads, (*registers)[1]);
                if (!target || !threads->GuestThreadNice(*target))
                    return ReturnSyscallError(
                        threads, kNoSuchProcess, registers,
                        dispatch_error);
                const int priority = static_cast<std::int32_t>(
                    static_cast<std::uint32_t>((*registers)[2]));
                if (priority < -20 || priority > 19)
                    return ReturnSyscallError(
                        threads, kInvalidArgument, registers,
                        dispatch_error);
                std::string priority_error;
                if (!threads->SetGuestThreadNice(
                        *target, priority, &priority_error)) {
                    if (dispatch_error != nullptr)
                        *dispatch_error = std::move(priority_error);
                    return ReturnSyscallError(
                        threads, kIoError, registers, dispatch_error);
                }
                (*registers)[0] = 0;
                return true;
            },
            error))
        return false;
    if (!registry->Register(
            "syscall",
            [memory, files, threads, sync, registry](
                Aarch64ImportRegistry::Registers* registers,
                std::string* dispatch_error) {
            // Raw Linux syscall ABI (real `svc #0` / libc syscall()): after the
            // CallSVC remap, slot 0 carries the syscall number and slots 1-6
            // the arguments. Protected loaders drive their decryptor and
            // anti-tamper probes through raw syscalls to bypass hooked libc,
            // so file / process / time / signal calls route to the same bionic
            // services the named imports use. A real kernel returns -errno on
            // failure, so bionic results (-1 + errno) are converted to -errno.
            const std::uint64_t raw_nr = (*registers)[0];
            // Lightweight histogram of raw syscall numbers: the protected
            // loader can sit in a tight svc loop, and naming which number
            // dominates is the fastest way to see what it is waiting on.
            {
                static std::map<std::uint64_t, std::uint64_t> raw_hist;
                static std::uint64_t raw_total = 0;
                if (++raw_hist[raw_nr] && (++raw_total % 20000) == 0) {
                    std::fprintf(stderr, "ISOMR_RAW_HIST total=%llu:",
                                 (unsigned long long)raw_total);
                    for (const auto& e : raw_hist)
                        std::fprintf(stderr, " %llu=%llu",
                                     (unsigned long long)e.first,
                                     (unsigned long long)e.second);
                    std::fprintf(stderr, "\n");
                    std::fflush(stderr);
                }
            }
            const auto raw_neg = [threads](std::int64_t result) {
                if (result >= 0)
                    return static_cast<std::uint64_t>(result);
                std::int32_t err = 5;  // EIO fallback when errno is unavailable
                std::string ignored;
                if (threads != nullptr)
                    threads->ReadCurrentErrno(&err, &ignored);
                return static_cast<std::uint64_t>(
                    -static_cast<std::int64_t>(err > 0 ? err : 5));
            };
            if (files != nullptr) {
                const std::uint64_t a1 = (*registers)[1], a2 = (*registers)[2],
                                    a3 = (*registers)[3], a4 = (*registers)[4];
                std::int64_t r = 0;
                bool file_handled = true;
                switch (raw_nr) {
                    case 56:  // openat(dirfd,path,flags,mode)
                        r = files->Open(a2, a3, a4);
                        break;
                    case 57:  // close(fd)
                        r = files->Close(static_cast<int>(a1));
                        break;
                    case 62:  // lseek(fd,off,whence)
                        r = files->Lseek64(static_cast<int>(a1),
                                           static_cast<std::int64_t>(a2),
                                           static_cast<int>(a3));
                        break;
                    case 63:  // read(fd,buf,count)
                        r = files->Read(static_cast<int>(a1), a2, a3);
                        break;
                    case 64:  // write(fd,buf,count)
                        r = files->Write(static_cast<int>(a1), a2, a3);
                        break;
                    case 67:  // pread64(fd,buf,count,off)
                        r = files->Pread64(static_cast<int>(a1), a2, a3,
                                           static_cast<std::int64_t>(a4));
                        break;
                    case 68:  // pwrite64(fd,buf,count,off)
                        r = files->Pwrite64(static_cast<int>(a1), a2, a3,
                                            static_cast<std::int64_t>(a4));
                        break;
                    case 46:  // ftruncate(fd,len)
                        r = files->Truncate(static_cast<int>(a1),
                                            static_cast<std::int64_t>(a2));
                        break;
                    case 78:  // readlinkat(dirfd,path,buf,size)
                        r = files->ReadLinkAt(static_cast<int>(a1), a2, a3, a4);
                        break;
                    case 79:  // fstatat(dirfd,path,buf,flags)
                        r = (a4 & 0x1000ULL) != 0
                                ? files->Fstat(static_cast<int>(a1), a3)
                                : files->Stat(a2, a3);
                        break;
                    case 80:  // fstat(fd,buf)
                        r = files->Fstat(static_cast<int>(a1), a2);
                        break;
                    case 34:  // mkdirat(dirfd,path,mode)
                        r = files->MakeDirectory(a2, a3);
                        break;
                    case 35:  // unlinkat(dirfd,path,flags)
                        r = files->Unlink(a2);
                        break;
                    case 48:  // faccessat(dirfd,path,mode)
                        r = files->Access(a2, static_cast<int>(a3));
                        break;
                    case 23:  // dup(fd)
                        r = files->Dup(static_cast<int>(a1));
                        break;
                    case 24:  // dup3(fd,newfd,flags)
                        r = files->Dup3(static_cast<int>(a1),
                                        static_cast<int>(a2), a3);
                        break;
                    default:
                        file_handled = false;
                        break;
                }
                if (file_handled) {
                    (*registers)[0] = raw_neg(r);
                    return true;
                }
            }
            // mmap-family raw syscalls route to the registered BionicMappings
            // imports by name; the arguments shift down one slot to drop the
            // syscall number (mmap args are addr,len,prot,flags,fd,off).
            const char* mapped_name = nullptr;
            switch (raw_nr) {
                case 222: mapped_name = "mmap"; break;
                case 215: mapped_name = "munmap"; break;
                case 226: mapped_name = "mprotect"; break;
                case 233: mapped_name = "madvise"; break;
                default: break;
            }
            if (mapped_name != nullptr) {
                Aarch64ImportRegistry::Registers mapped = *registers;
                for (std::size_t i = 0; i + 1 < mapped.general.size(); ++i)
                    mapped.general[i] = (*registers)[i + 1];
                std::string map_error;
                const bool mapped_ok =
                    registry->Dispatch(mapped_name, &mapped, &map_error);
                (*registers)[0] = mapped.general[0];
                if (!mapped_ok) {
                    if (dispatch_error != nullptr)
                        *dispatch_error = std::move(map_error);
                    return false;
                }
                return true;
            }
            // Process / identity / time / signal raw syscalls. These don't go
            // through BionicFileSystem, so they are handled directly.
            switch (raw_nr) {
                case 96:   // set_tid_address
                case 99:   // set_robust_list
                case 131:  // sigaltstack
                case 133:  // rt_sigsuspend... (134-136 handled below)
                case 134:  // rt_sigaction
                case 135:  // rt_sigprocmask
                case 136:  // rt_sigpending
                case 139:  // rt_sigreturn
                    // Benign no-op successes for the loader's anti-tamper
                    // scaffolding; real signal delivery isn't required to boot.
                    (*registers)[0] = 0;
                    return true;
                case 172:  // getpid
                case 173:  // getppid
                case 174:  // getuid
                case 175:  // geteuid
                case 176:  // getegid
                case 177: {  // getgid
                    const std::uint64_t id =
                        (raw_nr == 172 || raw_nr == 178)
                            ? static_cast<std::uint64_t>(
                                  threads->CurrentGuestThreadId()
                                      .value_or(1))
                            : 1000;
                    (*registers)[0] = id;
                    return true;
                }
                case 160: {  // uname(buf)
                    struct UtsName { char f[6][65]; } uts{};
                    auto cp = [&](int i, const char* s) {
                        const std::size_t n = std::min<std::size_t>(
                            std::strlen(s), 64);
                        std::memcpy(uts.f[i], s, n);
                        uts.f[i][n] = '\0';
                    };
                    cp(0, "Linux"); cp(1, "armr"); cp(2, "5.10.0");
                    cp(3, "#1 SMP"); cp(4, "aarch64"); cp(5, "(none)");
                    (*registers)[0] =
                        memory->Write((*registers)[1], &uts, sizeof(uts))
                            ? 0
                            : raw_neg(-1);
                    return true;
                }
                case 278: {  // getrandom(buf,len,flags)
                    std::uint64_t addr = (*registers)[1];
                    std::uint64_t len = (*registers)[2];
                    static std::size_t gr_samples = 0;
                    if (gr_samples < 48) {
                        ++gr_samples;
                        std::fprintf(stderr,
                                   "ISOMR_GETRANDOM buf=0x%llx len=%llu "
                                   "flags=0x%llx\n",
                                   (unsigned long long)addr,
                                   (unsigned long long)len,
                                   (unsigned long long)(*registers)[3]);
                    }
                    if (len > (1u << 20)) {
                        (*registers)[0] = raw_neg(-1);
                        return true;
                    }
                    std::vector<std::uint8_t> rnd(len);
                    if (len != 0 &&
                        BCryptGenRandom(nullptr, rnd.data(),
                                        static_cast<ULONG>(len),
                                        BCRYPT_USE_SYSTEM_PREFERRED_RNG) !=
                            0) {
                        (*registers)[0] = raw_neg(-1);
                        return true;
                    }
                    (*registers)[0] = memory->Write(addr, rnd.data(), len)
                                          ? len : raw_neg(-1);
                    return true;
                }
                case 113: {  // clock_gettime(clk,timespec)
                    const std::uint64_t ts_addr = (*registers)[2];
                    struct { std::int64_t sec, nsec; } ts{};
                    FILETIME ft; GetSystemTimePreciseAsFileTime(&ft);
                    std::int64_t ns =
                        ((static_cast<std::int64_t>(ft.dwHighDateTime) << 32) |
                         ft.dwLowDateTime - 116444736000000000LL) * 100;
                    ts.sec = ns / 1000000000LL; ts.nsec = ns % 1000000000LL;
                    (*registers)[0] =
                        memory->Write(ts_addr, &ts, sizeof(ts)) ? 0
                                                              : raw_neg(-1);
                    return true;
                }
                case 101: {  // nanosleep(req,rem)
                    struct { std::int64_t sec, nsec; } req{};
                    if (memory->Read((*registers)[1], &req, sizeof(req))) {
                        std::int64_t ms = req.sec * 1000 + req.nsec / 1000000;
                        if (ms < 0) ms = 0;
                        if (ms > 100) ms = 100;  // cap long guest sleeps
                        if (ms > 0) Sleep(static_cast<DWORD>(ms));
                    }
                    (*registers)[0] = 0;
                    return true;
                }
                case 167:  // prctl — accept silently (PR_SET_NAME etc.)
                case 261:  // prlimit64 — report success, leave guest values
                    (*registers)[0] = 0;
                    return true;
                default:
                    break;
            }
            // Numbers the existing named-syscall chain below already handles;
            // don't flag them as unsupported in the diagnostic.
            switch (raw_nr) {
                case kAarch64Futex:
                case kAarch64Sysinfo:
                case kAarch64SchedSetAffinity:
                case kAarch64SchedGetAffinity:
                case kAarch64GetCpu:
                case kAarch64GetTid:
                case kAarch64ProcessVmReadv:
                    break;
                default: {
                    static std::mutex raw_log_mu;
                    const std::lock_guard lock(raw_log_mu);
                    static std::map<std::uint64_t, std::uint64_t> raw_seen;
                    if (raw_seen[raw_nr]++ < 8)
                        std::fprintf(stderr,
                                     "ISOMR_RAW_SYSCALL unsupported nr=%llu "
                                     "a1=0x%llx a2=0x%llx a3=0x%llx "
                                     "a4=0x%llx\n",
                                     (unsigned long long)raw_nr,
                                     (unsigned long long)(*registers)[1],
                                     (unsigned long long)(*registers)[2],
                                     (unsigned long long)(*registers)[3],
                                     (unsigned long long)(*registers)[4]);
                    break;
                }
            }
            if ((*registers)[0] == kAarch64ProcessVmReadv)
                return ProcessVmReadSelf(memory, threads, registers,
                                         dispatch_error);
            if ((*registers)[0] == kAarch64Futex) {
                const std::uint64_t operation = (*registers)[2];
                if (operation == kFutexWaitPrivate) {
                    const std::uint64_t wait_addr = (*registers)[1];
                    const std::uint32_t expected =
                        static_cast<std::uint32_t>((*registers)[3]);
                    const std::uint64_t timeout_addr = (*registers)[4];
                    // ISOMR_ONCE_BREAK=N: when a single-threaded guest blocks
                    // forever on a lock word still at 1 (held by itself — a
                    // recursive once/mutex that can't resolve), write N to the
                    // word and wake. N=2 means "done" (skip init); N=0 means
                    // "free" (the nested call re-acquires, like a recursive
                    // mutex). Diagnostic to identify the divergent init path.
                    static const long once_break_val = [] {
                        wchar_t t[16]{};
                        SetLastError(ERROR_SUCCESS);
                        const DWORD n = GetEnvironmentVariableW(
                            L"ISOMR_ONCE_BREAK", t,
                            static_cast<DWORD>(std::size(t)));
                        if (n == 0 || n >= std::size(t)) return -1L;
                        wchar_t* end = nullptr;
                        const long v = std::wcstol(t, &end, 10);
                        return v;
                    }();
                    if (once_break_val >= 0 && timeout_addr == 0 &&
                        expected == 1) {
                        std::uint32_t word = 0;
                        if (memory->Read(wait_addr, &word, sizeof(word)) &&
                            word == 1) {
                            const std::uint32_t done =
                                static_cast<std::uint32_t>(once_break_val);
                            if (memory->Write(wait_addr, &done,
                                              sizeof(done))) {
                                std::fprintf(
                                    stderr,
                                    "ISOMR_ONCE_DEADLOCK_BREAK addr=0x%llx\n",
                                    static_cast<unsigned long long>(
                                        wait_addr));
                                std::fflush(stderr);
                                (*registers)[0] = 0;
                                return true;
                            }
                        }
                    }
                    const int result = sync->FutexWaitPrivate(
                        wait_addr, expected, timeout_addr);
                    if (result != 0)
                        return ReturnSyscallError(
                            threads, result, registers, dispatch_error);
                    (*registers)[0] = 0;
                    return true;
                }
                if (operation == kFutexWakePrivate) {
                    if ((*registers)[3] >
                        (std::numeric_limits<std::uint32_t>::max)())
                        return ReturnSyscallError(
                            threads, kInvalidArgument, registers,
                            dispatch_error);
                    std::uint32_t woken = 0;
                    const int result = sync->FutexWakePrivate(
                        (*registers)[1],
                        static_cast<std::uint32_t>((*registers)[3]),
                        &woken);
                    if (result != 0)
                        return ReturnSyscallError(
                            threads, result, registers, dispatch_error);
                    (*registers)[0] = woken;
                    return true;
                }
                std::fprintf(stderr,
                             "ISOMR_FUTEX_REJECT op=0x%llx addr=0x%llx "
                             "val=0x%llx timeout=0x%llx\n",
                             static_cast<unsigned long long>((*registers)[2]),
                             static_cast<unsigned long long>((*registers)[1]),
                             static_cast<unsigned long long>((*registers)[3]),
                             static_cast<unsigned long long>((*registers)[4]));
                std::fflush(stderr);
                return ReturnSyscallError(threads, kInvalidArgument,
                                          registers, dispatch_error);
            }
            if ((*registers)[0] == kAarch64SchedSetAffinity) {
                constexpr std::uint64_t kMeasuredCpuSetSize =
                    sizeof(std::uint32_t);
                if ((*registers)[2] != kMeasuredCpuSetSize)
                    return ReturnSyscallError(
                        threads, kInvalidArgument, registers,
                        dispatch_error);
                std::uint32_t requested_mask = 0;
                if ((*registers)[3] == 0 ||
                    !memory->Read((*registers)[3], &requested_mask,
                                  sizeof(requested_mask)))
                    return ReturnSyscallError(
                        threads, kBadAddress, registers,
                        dispatch_error);
                const std::optional<std::uint32_t> target =
                    ResolvePriorityTarget(threads, (*registers)[1]);
                if (!target || !threads->GuestThreadNice(*target))
                    return ReturnSyscallError(
                        threads, kNoSuchProcess, registers,
                        dispatch_error);
                DWORD_PTR process_mask = 0;
                DWORD_PTR system_mask = 0;
                if (!GetProcessAffinityMask(GetCurrentProcess(),
                                            &process_mask,
                                            &system_mask))
                    return ReturnSyscallError(
                        threads, kIoError, registers, dispatch_error);
                const std::uintptr_t effective_mask =
                    static_cast<std::uintptr_t>(requested_mask) &
                    static_cast<std::uintptr_t>(process_mask);
                if (effective_mask == 0)
                    return ReturnSyscallError(
                        threads, kInvalidArgument, registers,
                        dispatch_error);
                std::uintptr_t previous_mask = 0;
                std::string affinity_error;
                if (!threads->SetGuestThreadAffinity(
                        *target, effective_mask, &previous_mask,
                        &affinity_error)) {
                    if (dispatch_error != nullptr)
                        *dispatch_error = std::move(affinity_error);
                    return ReturnSyscallError(
                        threads, kIoError, registers, dispatch_error);
                }
                (*registers)[0] = 0;
                return true;
            }
            if ((*registers)[0] == kAarch64SchedGetAffinity) {
                // sched_getaffinity(pid, cpusetsize, mask*): report the host
                // process affinity mask so SystemInfo sees a real CPU count.
                const std::uint64_t set_size = (*registers)[2];
                const std::uint64_t mask_address = (*registers)[3];
                if (mask_address == 0)
                    return ReturnSyscallError(threads, kBadAddress, registers,
                                              dispatch_error);
                std::vector<std::uint8_t> mask;
                if (!GuestAffinityMaskBytes(
                        static_cast<std::size_t>(set_size), &mask))
                    return ReturnSyscallError(threads, kIoError, registers,
                                              dispatch_error);
                if (!memory->Write(mask_address, mask.data(), mask.size()))
                    return ReturnSyscallError(threads, kBadAddress, registers,
                                              dispatch_error);
                (*registers)[0] = 0;
                return true;
            }
            if ((*registers)[0] == kAarch64Sysinfo) {
                // sysinfo(struct sysinfo*): fill the RAM/uptime fields Unity's
                // SystemInfo reads. Layout (aarch64): uptime@0, loads[3]@8,
                // totalram@32, freeram@40, sharedram@48, bufferram@56,
                // totalswap@64, freeswap@72, procs@80, mem_unit@104.
                const std::uint64_t info_address = (*registers)[1];
                if (info_address == 0)
                    return ReturnSyscallError(threads, kBadAddress, registers,
                                              dispatch_error);
                MEMORYSTATUSEX status{};
                status.dwLength = sizeof(status);
                if (!GlobalMemoryStatusEx(&status))
                    return ReturnSyscallError(threads, kIoError, registers,
                                              dispatch_error);
                std::array<std::uint8_t, 112> info{};
                const auto put64 = [&info](std::size_t offset,
                                           std::uint64_t value) {
                    for (std::size_t byte = 0; byte < 8; ++byte)
                        info[offset + byte] = static_cast<std::uint8_t>(
                            (value >> (8 * byte)) & 0xff);
                };
                put64(0, GetTickCount64() / 1000);        // uptime seconds
                put64(32, status.ullTotalPhys);           // totalram
                put64(40, status.ullAvailPhys);           // freeram
                put64(64, status.ullTotalPageFile);       // totalswap
                put64(72, status.ullAvailPageFile);       // freeswap
                put64(104, 1);                            // mem_unit bytes
                if (!memory->Write(info_address, info.data(), info.size()))
                    return ReturnSyscallError(threads, kBadAddress, registers,
                                              dispatch_error);
                (*registers)[0] = 0;
                return true;
            }
            if ((*registers)[0] == kAarch64GetCpu) {
                // getcpu(unsigned* cpu, unsigned* node): report the current
                // logical processor so CPU topology queries resolve.
                const std::uint64_t cpu_address = (*registers)[1];
                const std::uint64_t node_address = (*registers)[2];
                const std::uint32_t cpu = GetCurrentProcessorNumber();
                const std::uint32_t node = 0;
                if ((cpu_address != 0 &&
                     !memory->Write(cpu_address, &cpu, sizeof(cpu))) ||
                    (node_address != 0 &&
                     !memory->Write(node_address, &node, sizeof(node))))
                    return ReturnSyscallError(threads, kBadAddress, registers,
                                              dispatch_error);
                (*registers)[0] = 0;
                return true;
            }
            if ((*registers)[0] != kAarch64GetTid) {
                if (dispatch_error != nullptr)
                    *dispatch_error = "unsupported raw AArch64 syscall: " +
                                      std::to_string((*registers)[0]);
                return false;
            }
            const std::optional<std::uint32_t> id =
                threads->CurrentGuestThreadId();
            if (!id) {
                if (dispatch_error != nullptr)
                    *dispatch_error =
                        "current thread has no guest thread id";
                return false;
            }
            (*registers)[0] = *id;
            return true;
        },
        error))
        return false;
    // compiler-rt CFI forward-edge hooks (Clang -fsanitize=cfi). These are
    // hardening checks, not functional APIs: __cfi_check returns the call
    // target to jump to, and the __cfi_slowpath* variants are the
    // diagnostic/recover path taken when the inline type check fails. Running
    // them as recover-mode (return normally / hand back the target) lets the
    // guest's CFI-instrumented indirect calls proceed the way they do on a
    // non-CFI build, which is the correct host-side model since the emulator
    // does not reproduce the type-tag tables.
    if (!registry->Register(
            "__cfi_check",
            [](Aarch64ImportRegistry::Registers* registers, std::string*) {
                // Return the checked target (x1) so the indirect call jumps
                // to it: equivalent to the check passing.
                (*registers)[0] = (*registers)[1];
                return true;
            },
            error))
        return false;
    const auto register_cfi_slowpath = [&](const char* name) {
        return registry->Register(
            name,
            [](Aarch64ImportRegistry::Registers* registers, std::string*) {
                // Recover: do not abort; returning skips the failed call.
                (*registers)[0] = 0;
                return true;
            },
            error);
    };
    if (!register_cfi_slowpath("__cfi_slowpath") ||
        !register_cfi_slowpath("__cfi_slowpath_diag"))
        return false;
    return true;
}

}  // namespace isomr
