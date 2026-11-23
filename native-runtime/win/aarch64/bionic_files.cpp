#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#include <winternl.h>

#include "bionic_files.h"

#include "bionic_thread_state.h"
#include "guest_address_space.h"
#include "guest_heap.h"
#include "guest_thread_stack.h"
#include "import_registry.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <cwchar>
#include <functional>
#include <limits>
#include <optional>
#include <string_view>
#include <thread>
#include <utility>

namespace isomr {
namespace {

constexpr int kOperationNotPermitted = 1;
constexpr int kNoEntry = 2;
constexpr int kInterrupted = 4;
constexpr int kIo = 5;
constexpr int kBadDescriptor = 9;
constexpr int kTryAgain = 11;
constexpr int kOutOfMemory = 12;
constexpr int kAccessDenied = 13;
constexpr int kBadAddress = 14;
constexpr int kExists = 17;
constexpr int kCrossDevice = 18;
constexpr int kNotDirectory = 20;
constexpr int kIsDirectory = 21;
constexpr int kInvalid = 22;
constexpr int kTooManyFiles = 24;
constexpr int kIllegalSeek = 29;
constexpr int kReadOnlyFileSystem = 30;
constexpr int kRange = 34;
constexpr int kNameTooLong = 36;
constexpr int kNoLocks = 37;
constexpr int kNotEmpty = 39;
constexpr int kTooManySymbolicLinks = 40;
constexpr int kOverflow = 75;
constexpr int kIllegalByteSequence = 84;
constexpr std::uint64_t kOpenAccessMode = 3;
constexpr std::uint64_t kOpenWriteOnly = 1;
constexpr std::uint64_t kOpenReadWrite = 2;
constexpr std::uint64_t kOpenCreate = 0x40;
constexpr std::uint64_t kOpenExclusive = 0x80;
constexpr std::uint64_t kOpenTruncate = 0x200;
constexpr std::uint64_t kOpenAppend = 0x400;
constexpr std::uint64_t kOpenLargeFile = 0x8000;
constexpr std::uint64_t kOpenNoFollow = 0x20000;
constexpr std::uint64_t kOpenCloseOnExec = 0x80000;
constexpr std::uint32_t kRegularMode = 0100000U | 0444U;
constexpr std::uint32_t kCharacterMode = 0020000U | 0444U;
constexpr std::uint32_t kDirectoryMode = 0040000U | 0555U;
constexpr std::uint32_t kSymbolicLinkMode = 0120000U | 0777U;
constexpr std::uint64_t kAllowedOpenFlags =
    kOpenAccessMode | kOpenCreate | kOpenExclusive | kOpenTruncate |
    kOpenAppend | kOpenLargeFile | kOpenNoFollow | kOpenCloseOnExec;
constexpr std::uint64_t kWindowsToUnixEpochTicks =
    116444736000000000ULL;
constexpr int kFileGetLock = 5;
constexpr int kFileSetLock = 6;
constexpr std::int16_t kReadLock = 0;
constexpr std::int16_t kWriteLock = 1;
constexpr std::int16_t kUnlock = 2;
constexpr std::string_view kSystemRandomPath = "/dev/urandom";
constexpr std::uint8_t kDirectoryTypeUnknown = 0;
constexpr std::uint8_t kDirectoryTypeDirectory = 4;
constexpr std::uint8_t kDirectoryTypeRegular = 8;
constexpr std::uint8_t kDirectoryTypeLink = 10;
constexpr std::uint64_t kStatFsReadOnly = 1;
// `ST_RDONLY` in bionic `struct statvfs` f_flag; matches kStatFsReadOnly but
// kept distinct so the statvfs writer reads as its own mount-flag contract.
constexpr std::uint64_t kStatVfsReadOnly = 1;
constexpr std::uint64_t kNtfsSuperMagic = 0x5346544e;
constexpr std::uint64_t kMsdosSuperMagic = 0x4d44;
constexpr std::uint64_t kExfatSuperMagic = 0x2011bab0;

void SetError(std::string* error, std::string message) {
    if (error != nullptr) *error = std::move(message);
}

// Default-off diagnostic: ISOMR_FILE_TRACE=1 enables the per-miss/per-op
// stderr lines, which are otherwise hot writes during asset variant probing.
bool FileTraceEnabled() {
    static const bool enabled = [] {
        wchar_t flag[8] = {};
        return GetEnvironmentVariableW(L"ISOMR_FILE_TRACE", flag, 8) > 0 &&
               flag[0] != L'0';
    }();
    return enabled;
}

// Default-off diagnostic: ISOMR_TRACE_PSEUDOFS=1 logs every /proc and /sys
// open with the virtual-content hit/miss outcome, used to verify Unity's
// CPU-topology enumeration actually reaches the pseudo filesystem.
bool PseudoFsTraceEnabled() {
    static const bool enabled = [] {
        wchar_t flag[8] = {};
        return GetEnvironmentVariableW(L"ISOMR_TRACE_PSEUDOFS", flag, 8) > 0 &&
               flag[0] != L'0';
    }();
    return enabled;
}

std::optional<std::wstring> FullPath(const std::wstring& path) {
    const DWORD required = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
    if (required == 0) return std::nullopt;
    std::vector<wchar_t> buffer(static_cast<std::size_t>(required) + 1);
    const DWORD written = GetFullPathNameW(
        path.c_str(), static_cast<DWORD>(buffer.size()), buffer.data(), nullptr);
    if (written == 0 || written >= buffer.size()) return std::nullopt;
    return std::wstring(buffer.data(), written);
}

std::optional<std::wstring> FinalPath(HANDLE handle) {
    const DWORD required = GetFinalPathNameByHandleW(
        handle, nullptr, 0, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (required == 0) return std::nullopt;
    std::vector<wchar_t> buffer(static_cast<std::size_t>(required) + 1);
    const DWORD written = GetFinalPathNameByHandleW(
        handle, buffer.data(), static_cast<DWORD>(buffer.size()),
        FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (written == 0 || written >= buffer.size()) return std::nullopt;
    std::wstring result(buffer.data(), written);
    while (result.size() > 4 &&
           (result.back() == L'\\' || result.back() == L'/'))
        result.pop_back();
    return result;
}

bool HasPathPrefix(std::wstring_view path, std::wstring_view root) {
    if (path.size() < root.size() ||
        _wcsnicmp(path.data(), root.data(), root.size()) != 0)
        return false;
    return path.size() == root.size() || path[root.size()] == L'\\' ||
           path[root.size()] == L'/';
}

int WindowsErrorToErrno(DWORD error) {
    switch (error) {
        case ERROR_FILE_NOT_FOUND:
        case ERROR_PATH_NOT_FOUND:
        case ERROR_INVALID_NAME:
            return kNoEntry;
        case ERROR_ACCESS_DENIED:
        case ERROR_SHARING_VIOLATION:
            return kAccessDenied;
        case ERROR_ALREADY_EXISTS:
        case ERROR_FILE_EXISTS:
            return kExists;
        case ERROR_TOO_MANY_OPEN_FILES:
            return kTooManyFiles;
        case ERROR_DIR_NOT_EMPTY:
            return kNotEmpty;
        default:
            return kInvalid;
    }
}

HANDLE CreateFileRelativeToDirectory(
    HANDLE parent, std::wstring_view file_name, DWORD desired_access,
    ULONG share_access, ULONG create_disposition, bool no_follow,
    DWORD* error) {
    if (file_name.empty() ||
        file_name.size() >
            (std::numeric_limits<USHORT>::max)() / sizeof(wchar_t)) {
        if (error != nullptr) *error = ERROR_INVALID_NAME;
        return INVALID_HANDLE_VALUE;
    }

    const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll == nullptr) {
        if (error != nullptr) *error = ERROR_CALL_NOT_IMPLEMENTED;
        return INVALID_HANDLE_VALUE;
    }
    const FARPROC create_procedure =
        GetProcAddress(ntdll, "NtCreateFile");
    const FARPROC convert_procedure =
        GetProcAddress(ntdll, "RtlNtStatusToDosError");
    decltype(&NtCreateFile) nt_create_file = nullptr;
    decltype(&RtlNtStatusToDosError) status_to_error = nullptr;
    static_assert(sizeof(nt_create_file) == sizeof(create_procedure));
    static_assert(sizeof(status_to_error) == sizeof(convert_procedure));
    std::memcpy(&nt_create_file, &create_procedure,
                sizeof(nt_create_file));
    std::memcpy(&status_to_error, &convert_procedure,
                sizeof(status_to_error));
    if (nt_create_file == nullptr || status_to_error == nullptr) {
        if (error != nullptr) *error = ERROR_CALL_NOT_IMPLEMENTED;
        return INVALID_HANDLE_VALUE;
    }

    UNICODE_STRING relative_name{};
    relative_name.Length = static_cast<USHORT>(
        file_name.size() * sizeof(wchar_t));
    relative_name.MaximumLength = relative_name.Length;
    relative_name.Buffer = const_cast<PWSTR>(file_name.data());

    OBJECT_ATTRIBUTES attributes{};
    attributes.Length = sizeof(attributes);
    attributes.RootDirectory = parent;
    attributes.ObjectName = &relative_name;
    attributes.Attributes = OBJ_CASE_INSENSITIVE;

    IO_STATUS_BLOCK status_block{};
    HANDLE handle = INVALID_HANDLE_VALUE;
    const NTSTATUS status = nt_create_file(
        &handle,
        static_cast<ACCESS_MASK>(desired_access | SYNCHRONIZE),
        &attributes, &status_block, nullptr, FILE_ATTRIBUTE_NORMAL,
        share_access, create_disposition,
        FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT |
            (no_follow ? FILE_OPEN_REPARSE_POINT : 0),
        nullptr, 0);
    if (!NT_SUCCESS(status)) {
        if (error != nullptr) *error = status_to_error(status);
        return INVALID_HANDLE_VALUE;
    }
    return handle;
}

int WindowsLockErrorToErrno(DWORD error) {
    switch (error) {
        case ERROR_LOCK_VIOLATION:
        case ERROR_IO_PENDING:
            return kTryAgain;
        case ERROR_INVALID_HANDLE:
            return kBadDescriptor;
        case ERROR_ACCESS_DENIED:
            return kAccessDenied;
        case ERROR_NOT_ENOUGH_MEMORY:
        case ERROR_OUTOFMEMORY:
            return kNoLocks;
        case ERROR_OPERATION_ABORTED:
            return kInterrupted;
        case ERROR_INVALID_PARAMETER:
        case ERROR_NOT_LOCKED:
        default:
            return kInvalid;
    }
}

void FileTimeToUnix(const FILETIME& input, std::int64_t* seconds,
                    std::uint64_t* nanoseconds) {
    const std::uint64_t ticks =
        static_cast<std::uint64_t>(input.dwHighDateTime) << 32U |
        input.dwLowDateTime;
    if (ticks < kWindowsToUnixEpochTicks) {
        *seconds = 0;
        *nanoseconds = 0;
        return;
    }
    const std::uint64_t unix_ticks = ticks - kWindowsToUnixEpochTicks;
    *seconds = static_cast<std::int64_t>(unix_ticks / 10000000ULL);
    *nanoseconds = (unix_ticks % 10000000ULL) * 100ULL;
}

}  // namespace

BionicFileSystem::FileRecord::~FileRecord() {
    if (!borrowed && handle != nullptr && handle != INVALID_HANDLE_VALUE)
        CloseHandle(static_cast<HANDLE>(handle));
}

BionicFileSystem::DirectoryRecord::~DirectoryRecord() {
    if (handle != nullptr && handle != INVALID_HANDLE_VALUE)
        CloseHandle(static_cast<HANDLE>(handle));
}

BionicFileSystem::BionicFileSystem(
    GuestHeap* heap, GuestAddressSpace* address_space,
    BionicThreadState* thread_state,
    std::vector<BionicFileMount> mounts, std::string current_directory,
    std::size_t maximum_transfer, std::size_t maximum_path)
    : heap_(heap), address_space_(address_space), thread_state_(thread_state),
      maximum_transfer_(maximum_transfer), maximum_path_(maximum_path) {
    for (BionicFileMount& candidate : mounts) {
        while (candidate.guest_prefix.size() > 1 &&
               candidate.guest_prefix.back() == '/')
            candidate.guest_prefix.pop_back();
        if (candidate.guest_prefix.empty() ||
            candidate.guest_prefix.front() != '/' ||
            candidate.guest_prefix.find('\\') != std::string::npos ||
            candidate.guest_prefix.find("..") != std::string::npos)
            continue;
        const std::optional<std::wstring> full = FullPath(candidate.host_root);
        if (!full) continue;
        HANDLE root = CreateFileW(
            full->c_str(), FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
            OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        if (root == INVALID_HANDLE_VALUE) continue;
        BY_HANDLE_FILE_INFORMATION root_information{};
        if (!GetFileInformationByHandle(root, &root_information) ||
            (root_information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ==
                0) {
            CloseHandle(root);
            continue;
        }
        const std::optional<std::wstring> final = FinalPath(root);
        CloseHandle(root);
        if (!final) continue;
        mounts_.push_back({std::move(candidate.guest_prefix), *full, *final,
                           candidate.writable});
    }
    std::sort(mounts_.begin(), mounts_.end(),
              [](const Mount& left, const Mount& right) {
                  return left.guest_prefix.size() >
                         right.guest_prefix.size();
              });
    while (current_directory.size() > 1 && current_directory.back() == '/')
        current_directory.pop_back();
    if (!current_directory.empty()) {
        const auto configured_mount = std::find_if(
            mounts_.begin(), mounts_.end(),
            [&current_directory](const Mount& mount) {
                return mount.guest_prefix == current_directory;
            });
        configuration_valid_ = configured_mount != mounts_.end();
        if (configuration_valid_)
            current_directory_ = configured_mount->guest_prefix;
    } else {
        const auto app_mount = std::find_if(
            mounts_.begin(), mounts_.end(), [](const Mount& mount) {
                return mount.guest_prefix == "/app";
            });
        if (app_mount != mounts_.end())
            current_directory_ = app_mount->guest_prefix;
        else if (!mounts_.empty())
            current_directory_ = mounts_.front().guest_prefix;
    }
    // Install guest stdin/stdout/stderr (fds 0/1/2) on the host's std handles
    // so guest write(2) reaches real stderr. They are borrowed — closing the
    // descriptor must not close the host process's std handles.
    const auto install_standard = [this](int descriptor, DWORD which,
                                         bool readable, bool writable) {
        HANDLE handle = GetStdHandle(which);
        if (handle == nullptr || handle == INVALID_HANDLE_VALUE) return;
        auto file = std::make_shared<FileRecord>();
        file->kind = FileRecord::Kind::HostFile;
        file->handle = handle;
        file->readable = readable;
        file->writable = writable;
        file->borrowed = true;
        files_.emplace(descriptor, std::move(file));
    };
    install_standard(0, STD_INPUT_HANDLE, true, false);
    install_standard(1, STD_OUTPUT_HANDLE, false, true);
    install_standard(2, STD_ERROR_HANDLE, false, true);
    if (next_descriptor_ < 3) next_descriptor_ = 3;
}

BionicFileSystem::~BionicFileSystem() {
    {
        const std::lock_guard lock(directories_mutex_);
        if (heap_ != nullptr) {
            std::string ignored;
            for (const auto& [address, directory] : directories_) {
                (void)address;
                (void)heap_->Release(directory->handle_address, &ignored);
            }
        }
        directories_.clear();
    }
    {
        const std::lock_guard lock(files_mutex_);
        files_.clear();
    }
}

void BionicFileSystem::SetErrno(std::int32_t value) {
    if (thread_state_ == nullptr) return;
    std::string ignored;
    (void)thread_state_->SetCurrentErrno(value, &ignored);
}

void BionicFileSystem::LogFileMiss(const char* operation,
                                   const std::string& guest_path,
                                   const std::wstring& host_path) {
    if (!FileTraceEnabled()) return;
    std::string narrow;
    narrow.reserve(host_path.size());
    for (const wchar_t c : host_path) {
        narrow.push_back(
            c < 0x80 ? static_cast<char>(c) : '?');
    }
    std::fprintf(stderr, "ISOMR_FILE_MISS op=%s guest=%s host=%s\n",
                 operation != nullptr ? operation : "?", guest_path.c_str(),
                 narrow.c_str());
}

void BionicFileSystem::LogFileOp(const char* operation,
                                 const std::string& guest_path) {
    if (!FileTraceEnabled()) return;
    std::fprintf(stderr, "ISOMR_FILE_OP op=%s path=%s\n",
                 operation != nullptr ? operation : "?", guest_path.c_str());
}

void BionicFileSystem::TraceFdOp(const char* operation, int descriptor,
                                 const std::string& detail,
                                 std::int64_t result) {
    if (!FileTraceEnabled()) return;
    const std::shared_ptr<FileRecord> file = Find(descriptor);
    std::fprintf(stderr, "ISOMR_FDOP op=%s fd=%d tid=%lu path=%s %s -> %lld\n",
                 operation != nullptr ? operation : "?", descriptor,
                 static_cast<unsigned long>(GetCurrentThreadId()),
                 (file && !file->trace_path.empty()) ? file->trace_path.c_str()
                                                     : "?",
                 detail.c_str(), static_cast<long long>(result));
}

bool BionicFileSystem::ReadGuestPath(std::uint64_t address,
                                     std::string* path) {
    if (address_space_ == nullptr || path == nullptr) {
        SetErrno(kBadAddress);
        return false;
    }
    std::uint64_t length = 0;
    std::string ignored;
    if (!address_space_->BoundedStringLength(address, maximum_path_ + 1,
                                             &length, &ignored)) {
        SetErrno(kBadAddress);
        return false;
    }
    if (length == 0 || length > maximum_path_ ||
        length > std::numeric_limits<std::size_t>::max()) {
        SetErrno(length == 0 ? kNoEntry : kNameTooLong);
        return false;
    }
    path->resize(static_cast<std::size_t>(length));
    if (!address_space_->Read(address, path->data(), path->size())) {
        SetErrno(kBadAddress);
        return false;
    }
    return true;
}

bool BionicFileSystem::ResolvePath(std::uint64_t address,
                                   std::wstring* host_path,
                                   std::wstring* final_root,
                                   bool* writable,
                                   bool* mount_root) {
    std::string guest_path;
    if (!ReadGuestPath(address, &guest_path)) return false;
    // ASSET:<name> paths name expansion-file entries. Map them into the
    // data mount; "ASSET:main.pak" is backed by main.rsb.raw (the
    // extracted RSB asset pack) when no literal main.pak exists.
    const std::string kAssetPrefix = "ASSET:";
    if (guest_path.starts_with(kAssetPrefix)) {
        std::string asset_name = guest_path.substr(kAssetPrefix.size());
        while (!asset_name.empty() && asset_name.front() == '/')
            asset_name.erase(0, 1);
        std::string mapped = "/data/" + asset_name;
        if (asset_name == "main.pak") mapped = "/data/main.rsb.raw";
        guest_path = mapped;
    }
    // Unity's il2cpp runtime extracts its managed data (Metadata,
    // Resources) into a writable /il2cpp/ root on first run and reads it back
    // from there. Route it into the writable /data mount — NOT the read-only
    // /app APK tree — so the extract's mkdir/rename/open-for-write succeed.
    if (guest_path == "/il2cpp" ||
        guest_path.rfind("/il2cpp/", 0) == 0) {
        guest_path = "/data/il2cpp" +
                     guest_path.substr(std::string("/il2cpp").size());
    }
    if (guest_path.front() != '/') {
        // Unity opens its engine data files as paths relative to the APK
        // content root (assets/bin/Data/unity_app_guid, globalgamemanagers,
        // data.unity3d, ...). Those live under the read-only /app mount, not
        // the writable /data working directory, so route the assets/ tree to
        // the APK root instead of resolving it against the cwd.
        if (guest_path.rfind("assets/", 0) == 0) {
            guest_path = "/app/" + guest_path;
        } else {
            if (current_directory_.empty()) {
                SetErrno(kNoEntry);
                return false;
            }
            guest_path = current_directory_ + '/' + guest_path;
        }
    }
    const Mount* mount = nullptr;
    for (const Mount& candidate : mounts_) {
        if (guest_path.starts_with(candidate.guest_prefix) &&
            (guest_path.size() == candidate.guest_prefix.size() ||
             candidate.guest_prefix == "/" ||
             guest_path[candidate.guest_prefix.size()] == '/')) {
            mount = &candidate;
            break;
        }
    }
    if (mount == nullptr) {
        SetErrno(kNoEntry);
        return false;
    }
    std::string relative = guest_path.substr(mount->guest_prefix.size());
    while (!relative.empty() && relative.front() == '/') relative.erase(0, 1);
    std::wstring converted;
    std::size_t start = 0;
    while (start < relative.size()) {
        const std::size_t end = relative.find('/', start);
        const std::string_view component(
            relative.data() + start,
            (end == std::string::npos ? relative.size() : end) - start);
        if (component.empty() || component == "." || component == ".." ||
            component.find('\\') != std::string_view::npos ||
            component.find(':') != std::string_view::npos) {
            SetErrno(kAccessDenied);
            return false;
        }
        const int required = MultiByteToWideChar(
            CP_UTF8, MB_ERR_INVALID_CHARS, component.data(),
            static_cast<int>(component.size()), nullptr, 0);
        if (required <= 0) {
            SetErrno(kInvalid);
            return false;
        }
        if (!converted.empty()) converted.push_back(L'\\');
        const std::size_t old_size = converted.size();
        converted.resize(old_size + static_cast<std::size_t>(required));
        if (MultiByteToWideChar(
                CP_UTF8, MB_ERR_INVALID_CHARS, component.data(),
                static_cast<int>(component.size()), converted.data() + old_size,
                required) != required) {
            SetErrno(kInvalid);
            return false;
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    const bool resolved_mount_root = converted.empty();
    *host_path = resolved_mount_root
                     ? mount->host_root
                     : mount->host_root + L"\\" + converted;
    *final_root = mount->final_root;
    if (writable != nullptr) *writable = mount->writable;
    if (mount_root != nullptr) *mount_root = resolved_mount_root;
    return true;
}

bool BionicFileSystem::ValidateOpenedPath(
    void* handle, const std::wstring& final_root) {
    const std::optional<std::wstring> final =
        FinalPath(static_cast<HANDLE>(handle));
    if (!final || !HasPathPrefix(*final, final_root)) {
        SetErrno(kAccessDenied);
        return false;
    }
    return true;
}

std::optional<std::string> BionicFileSystem::VirtualFileContent(
    const std::string& guest_path) {
    // Unity (and the guest loader) probes /proc/cpuinfo plus the
    // /sys/devices/system/cpu topology nodes to decide the worker-thread
    // model. There is no /proc or /sys mount, so serve a small read-only
    // pseudo filesystem from memory that reflects the host processor count.
    const unsigned processors = [] {
        // ISOMR_CPU_COUNT overrides the reported processor count so the
        // guest's threading model can be A/B tested (a single processor may
        // steer Unity toward synchronous rendering).
        wchar_t flag[16] = {};
        if (GetEnvironmentVariableW(L"ISOMR_CPU_COUNT", flag, 16) > 0) {
            const unsigned parsed = static_cast<unsigned>(_wtoi64(flag));
            if (parsed != 0) return parsed;
        }
        const unsigned reported = std::thread::hardware_concurrency();
        return reported == 0 ? 1u : reported;
    }();
    if (guest_path == "/proc/cpuinfo") {
        std::string text;
        for (unsigned index = 0; index < processors; ++index) {
            char block[192];
            const int length = std::snprintf(
                block, sizeof(block),
                "processor\t: %u\n"
                "Features\t: fp asimd evtstrm aes pmull sha1 sha2 crc32\n"
                "CPU implementer\t: 0x41\n"
                "CPU architecture: 8\n"
                "CPU variant\t: 0x0\n"
                "CPU part\t: 0xd03\n"
                "CPU revision\t: 4\n\n",
                index);
            if (length > 0) text.append(block, static_cast<std::size_t>(length));
        }
        text += "Hardware\t: Isomr AArch64\n";
        return text;
    }
    if (guest_path == "/sys/devices/system/cpu/online" ||
        guest_path == "/sys/devices/system/cpu/present" ||
        guest_path == "/sys/devices/system/cpu/possible") {
        return "0-" + std::to_string(processors - 1) + "\n";
    }
    // Per-core topology nodes: /sys/devices/system/cpu/cpu<N>/cpu_capacity and
    // .../cpufreq/cpuinfo_max_freq. A homogeneous little-only topology reports
    // the same capacity and frequency for every core.
    constexpr std::string_view kCpuPrefix = "/sys/devices/system/cpu/cpu";
    if (guest_path.rfind(kCpuPrefix, 0) == 0) {
        const std::size_t rest = kCpuPrefix.size();
        const std::size_t slash = guest_path.find('/', rest);
        const std::string index_text =
            slash == std::string::npos
                ? std::string()
                : guest_path.substr(rest, slash - rest);
        bool numeric = !index_text.empty();
        for (const char c : index_text) {
            if (c < '0' || c > '9') {
                numeric = false;
                break;
            }
        }
        if (numeric && slash != std::string::npos) {
            const std::string suffix = guest_path.substr(slash);
            if (suffix == "/cpu_capacity") return "1024\n";
            if (suffix == "/cpufreq/cpuinfo_max_freq") return "2400000\n";
        }
    }
    if (guest_path == "/proc/self/stat") {
        // The Boehm GC falls back to /proc/self/stat field 28 (startstack)
        // for the calling thread's stack top when pthread_getattr_np does not
        // answer. Emit a well-formed stat line with the running thread's
        // stack top in that field so the fallback also resolves.
        const std::uint64_t stack_top =
            g_guest_thread_stack_base + g_guest_thread_stack_size;
        std::string text = "1 (armr) R";
        for (int field = 4; field < 28; ++field) text += " 0";
        text += " " + std::to_string(stack_top);
        for (int field = 29; field <= 52; ++field) text += " 0";
        text += "\n";
        return text;
    }
    if (guest_path == "/proc/self/maps") {
        // A minimal self map covering the guest address-space region Unity
        // scans for module attribution; the precise ranges are not required
        // for the lookups it performs.
        return "410000000000-420000000000 r-xp 00000000 00:00 0   [guest]\n"
               "700000000000-720000000000 rw-p 00000000 00:00 0   [heap]\n";
    }
    if (guest_path == "/proc/self/auxv") {
        // Protected loaders open /proc/self/auxv directly (raw openat+read) to
        // read the auxiliary vector for anti-tamper checks, bypassing
        // getauxval(). Emit a binary Elf64 auxv matching the scalar values the
        // getauxval import reports (page size, clock ticks, no advertised
        // HWCAP extensions, uid/gid 1000, not setuid). Pointer-bearing entries
        // (AT_PHDR/AT_ENTRY/AT_RANDOM/AT_SYSINFO_EHDR) are omitted — there is
        // no guest main-executable image for them to denote.
        std::string auxv;
        auxv.reserve(19 * 16);
        const auto push = [&auxv](std::uint64_t type, std::uint64_t value) {
            for (int s = 0; s < 8; ++s)
                auxv.push_back(static_cast<char>((type >> (s * 8)) & 0xff));
            for (int s = 0; s < 8; ++s)
                auxv.push_back(static_cast<char>((value >> (s * 8)) & 0xff));
        };
        push(6, 4096);    // AT_PAGESZ
        push(17, 100);    // AT_CLKTCK
        push(8, 0);       // AT_FLAGS
        push(11, 1000);   // AT_UID
        push(12, 1000);   // AT_EUID
        push(13, 1000);   // AT_GID
        push(14, 1000);   // AT_EGID
        push(16, 0);      // AT_HWCAP
        push(26, 0);      // AT_HWCAP2
        push(23, 0);      // AT_SECURE
        push(0, 0);       // AT_NULL
        return auxv;
    }
    return std::nullopt;
}

int BionicFileSystem::Open(std::uint64_t path_address, std::uint64_t flags,
                           std::uint64_t mode) {
    const std::uint64_t access_mode = flags & kOpenAccessMode;
    const bool readable = access_mode != kOpenWriteOnly;
    const bool writable = access_mode == kOpenWriteOnly ||
                          access_mode == kOpenReadWrite;
    const bool create = (flags & kOpenCreate) != 0;
    const bool truncate = (flags & kOpenTruncate) != 0;
    const bool no_follow = (flags & kOpenNoFollow) != 0;
    if ((flags & ~kAllowedOpenFlags) != 0 || access_mode == kOpenAccessMode ||
        (create && (mode & ~0777ULL) != 0) ||
        ((truncate || (flags & kOpenAppend) != 0) && !writable)) {
        SetErrno(kInvalid);
        return -1;
    }
    std::string guest_path;
    if (!ReadGuestPath(path_address, &guest_path)) return -1;
    if (guest_path.find(".obb") != std::string::npos) {
        std::fprintf(stderr, "ISOMR_OBB_OPEN path=%s flags=%llx\n",
                     guest_path.c_str(),
                     static_cast<unsigned long long>(flags));
    }
    if (guest_path == kSystemRandomPath) {
        if (writable || create || truncate ||
            (flags & (kOpenExclusive | kOpenAppend)) != 0) {
            SetErrno(kAccessDenied);
            return -1;
        }
        std::shared_ptr<FileRecord> file = std::make_shared<FileRecord>();
        file->kind = FileRecord::Kind::SystemRandom;
        file->readable = true;
        return InsertFile(std::move(file));
    }
    if (const std::optional<std::string> virtual_content =
            VirtualFileContent(guest_path)) {
        if (writable || create || truncate ||
            (flags & (kOpenExclusive | kOpenAppend)) != 0) {
            SetErrno(kAccessDenied);
            return -1;
        }
        std::shared_ptr<FileRecord> file = std::make_shared<FileRecord>();
        file->kind = FileRecord::Kind::VirtualText;
        file->readable = true;
        file->content = *virtual_content;
        const std::size_t content_bytes = file->content.size();
        const int fd = InsertFile(std::move(file));
        if (PseudoFsTraceEnabled() &&
            (guest_path.rfind("/proc", 0) == 0 ||
             guest_path.rfind("/sys", 0) == 0)) {
            std::fprintf(stderr,
                         "ISOMR_PSEUDOFS open hit path=%s fd=%d bytes=%zu\n",
                         guest_path.c_str(), fd, content_bytes);
            std::fflush(stderr);
        }
        return fd;
    }
    if (PseudoFsTraceEnabled() &&
        (guest_path.rfind("/proc", 0) == 0 ||
         guest_path.rfind("/sys", 0) == 0)) {
        std::fprintf(stderr,
                     "ISOMR_PSEUDOFS open miss path=%s flags=%llx\n",
                     guest_path.c_str(),
                     static_cast<unsigned long long>(flags));
        std::fflush(stderr);
    }
    std::wstring host_path;
    std::wstring final_root;
    bool mount_writable = false;
    bool mount_root = false;
    if (!ResolvePath(path_address, &host_path, &final_root,
                     &mount_writable, &mount_root)) {
        // Failed opens are otherwise silent: PvZ1's RSB boot depends on
        // seeing the exact rejected path.
        std::string failed_path;
        if (ReadGuestPath(path_address, &failed_path)) {
            std::string hex;
            char hex_byte[4];
            for (unsigned char byte : failed_path) {
                std::snprintf(hex_byte, sizeof(hex_byte), "%02x ", byte);
                hex += hex_byte;
            }
            std::fprintf(stderr,
                         "ISOMR_FILE_OPEN_FAIL path=%s flags=%llx hex=[%s]\n",
                         failed_path.c_str(),
                         static_cast<unsigned long long>(flags),
                         hex.c_str());
        } else {
            std::fprintf(stderr,
                         "ISOMR_FILE_OPEN_FAIL path=<unreadable at %llx> flags=%llx\n",
                         static_cast<unsigned long long>(path_address),
                         static_cast<unsigned long long>(flags));
        }
        return -1;
    }
    if (FileTraceEnabled()) {
        LogFileOp("open", guest_path);
    }
    if (mount_root && (writable || create || truncate ||
                       (flags & kOpenAppend) != 0)) {
        SetErrno(kAccessDenied);
        return -1;
    }
    if (writable && !mount_writable) {
        SetErrno(kAccessDenied);
        return -1;
    }
    const DWORD desired_access =
        FILE_READ_ATTRIBUTES | (readable ? GENERIC_READ : 0) |
        (writable ? GENERIC_WRITE : 0);
    constexpr ULONG kShareAccess =
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
    HANDLE handle = INVALID_HANDLE_VALUE;
    if (create) {
        const std::size_t separator = host_path.find_last_of(L"\\/");
        if (separator == std::wstring::npos) {
            SetErrno(kAccessDenied);
            return -1;
        }
        const std::wstring parent_path = host_path.substr(0, separator);
        const std::wstring file_name = host_path.substr(separator + 1);
        HANDLE parent = CreateFileW(
            parent_path.c_str(),
            FILE_READ_ATTRIBUTES | FILE_TRAVERSE,
            kShareAccess, nullptr,
            OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        if (parent == INVALID_HANDLE_VALUE) {
            SetErrno(WindowsErrorToErrno(GetLastError()));
            return -1;
        }
        BY_HANDLE_FILE_INFORMATION parent_information{};
        const bool parent_valid =
            GetFileInformationByHandle(parent, &parent_information) &&
            (parent_information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) !=
                0 &&
            ValidateOpenedPath(parent, final_root);
        if (!parent_valid) {
            CloseHandle(parent);
            return -1;
        }
        DWORD open_error = ERROR_SUCCESS;
        handle = CreateFileRelativeToDirectory(
            parent, file_name, desired_access, kShareAccess,
            (flags & kOpenExclusive) != 0 ? FILE_CREATE : FILE_OPEN_IF,
            no_follow, &open_error);
        CloseHandle(parent);
        if (handle == INVALID_HANDLE_VALUE) {
            SetErrno(WindowsErrorToErrno(open_error));
            return -1;
        }
    } else {
        handle = CreateFileW(
            host_path.c_str(), desired_access, kShareAccess, nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL |
                (no_follow ? FILE_FLAG_OPEN_REPARSE_POINT : 0),
            nullptr);
    }
    if (handle == INVALID_HANDLE_VALUE) {
        const DWORD open_error = GetLastError();
        if (open_error == ERROR_FILE_NOT_FOUND ||
            open_error == ERROR_PATH_NOT_FOUND)
            LogFileMiss("open", guest_path, host_path);
        SetErrno(WindowsErrorToErrno(open_error));
        return -1;
    }
    BY_HANDLE_FILE_INFORMATION information{};
    if (!GetFileInformationByHandle(handle, &information)) {
        const DWORD last_error = GetLastError();
        CloseHandle(handle);
        SetErrno(WindowsErrorToErrno(last_error));
        return -1;
    }
    if ((information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        CloseHandle(handle);
        SetErrno(kAccessDenied);
        return -1;
    }
    if (no_follow &&
        (information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
        CloseHandle(handle);
        SetErrno(kTooManySymbolicLinks);
        return -1;
    }
    if (!ValidateOpenedPath(handle, final_root)) {
        CloseHandle(handle);
        return -1;
    }
    if (truncate) {
        LARGE_INTEGER zero{};
        if (!SetFilePointerEx(handle, zero, nullptr, FILE_BEGIN) ||
            !SetEndOfFile(handle)) {
            const DWORD last_error = GetLastError();
            CloseHandle(handle);
            SetErrno(WindowsErrorToErrno(last_error));
            return -1;
        }
    }
    std::shared_ptr<FileRecord> file = std::make_shared<FileRecord>();
    file->handle = handle;
    file->readable = readable;
    file->writable = writable;
    file->append = (flags & kOpenAppend) != 0;
    file->trace_path = guest_path;
    file->obb_diagnostic = guest_path.find(".obb") != std::string::npos;
    if (guest_path.find("catalog.json") != std::string::npos ||
        (guest_path.size() > 7 &&
         guest_path.compare(guest_path.size() - 7, 7, ".bundle") == 0)) {
        file->content_trace = true;
        file->content_path = guest_path;
    }
    const int fd = InsertFile(std::move(file));
    if (FileTraceEnabled()) {
        std::fprintf(stderr, "ISOMR_FDOP op=open fd=%d tid=%lu path=%s\n", fd,
                     static_cast<unsigned long>(GetCurrentThreadId()),
                     guest_path.c_str());
    }
    return fd;
}

int BionicFileSystem::InsertFile(std::shared_ptr<FileRecord> file) {
    const std::lock_guard lock(files_mutex_);
    if (files_.size() >=
        static_cast<std::size_t>(std::numeric_limits<int>::max() - 3)) {
        SetErrno(kTooManyFiles);
        return -1;
    }
    int descriptor = next_descriptor_;
    while (files_.contains(descriptor)) {
        descriptor = descriptor == std::numeric_limits<int>::max()
                         ? 3
                         : descriptor + 1;
    }
    files_.emplace(descriptor, std::move(file));
    next_descriptor_ = descriptor == std::numeric_limits<int>::max()
                           ? 3
                           : descriptor + 1;
    return descriptor;
}

std::shared_ptr<BionicFileSystem::FileRecord> BionicFileSystem::Resolve(
    const FileReference& reference) const {
    return std::static_pointer_cast<FileRecord>(reference.record_);
}

std::shared_ptr<BionicFileSystem::FileRecord> BionicFileSystem::Find(
    int descriptor) const {
    const std::lock_guard lock(files_mutex_);
    const auto found = files_.find(descriptor);
    return found == files_.end() ? nullptr : found->second;
}

BionicFileSystem::FileReference BionicFileSystem::RetainFile(
    int descriptor) const {
    return FileReference(Find(descriptor));
}

bool BionicFileSystem::IsWritable(const FileReference& reference) const {
    const std::shared_ptr<FileRecord> file = Resolve(reference);
    return file != nullptr && file->writable;
}

bool BionicFileSystem::ReleaseLocksForProcessExit(
    const std::vector<FileReference>& retained_files,
    ProcessExitLockCleanup* cleanup, std::string* error) {
    if (cleanup != nullptr) *cleanup = {};
    std::vector<std::shared_ptr<FileRecord>> records;
    {
        const std::lock_guard lock(files_mutex_);
        records.reserve(files_.size() + retained_files.size());
        for (const auto& [descriptor, file] : files_) {
            (void)descriptor;
            records.push_back(file);
        }
    }
    for (const FileReference& reference : retained_files) {
        const std::shared_ptr<FileRecord> file = Resolve(reference);
        if (file != nullptr) records.push_back(file);
    }
    std::sort(records.begin(), records.end(),
              [](const auto& left, const auto& right) {
                  return std::less<const FileRecord*>{}(left.get(), right.get());
              });
    records.erase(std::unique(records.begin(), records.end()), records.end());
    for (const std::shared_ptr<FileRecord>& file : records) {
        const std::lock_guard lock(file->mutex);
        if (file->locks.empty()) continue;
        if (cleanup != nullptr) ++cleanup->records;
        while (!file->locks.empty()) {
            const FileRecord::LockRecord& record = file->locks.back();
            OVERLAPPED overlapped{};
            overlapped.Offset = static_cast<DWORD>(record.start);
            overlapped.OffsetHigh = static_cast<DWORD>(record.start >> 32U);
            if (!UnlockFileEx(static_cast<HANDLE>(file->handle), 0,
                              static_cast<DWORD>(record.length),
                              static_cast<DWORD>(record.length >> 32U),
                              &overlapped)) {
                SetError(error,
                         "process-exit file lock release failed: win32=" +
                             std::to_string(GetLastError()));
                return false;
            }
            file->locks.pop_back();
            if (cleanup != nullptr) ++cleanup->locks;
        }
    }
    return true;
}

int BionicFileSystem::Close(int descriptor) {
    const std::lock_guard lock(files_mutex_);
    const auto found = files_.find(descriptor);
    if (found == files_.end()) {
        SetErrno(kBadDescriptor);
        return -1;
    }
    if (found->second->content_trace) {
        std::fprintf(stderr,
                     "ISOMR_CONTENT_SUM path=%s served=%llu fnv=%016llx\n",
                     found->second->content_path.c_str(),
                     static_cast<unsigned long long>(
                         found->second->served_bytes),
                     static_cast<unsigned long long>(
                         found->second->served_hash));
        std::fflush(stderr);
        served_content_count_.fetch_add(1, std::memory_order_relaxed);
    }
    files_.erase(found);
    next_descriptor_ = std::min(next_descriptor_, descriptor);
    return 0;
}

int BionicFileSystem::Dup(int descriptor) {
    const std::lock_guard lock(files_mutex_);
    const auto found = files_.find(descriptor);
    if (found == files_.end()) {
        SetErrno(kBadDescriptor);
        return -1;
    }
    int target = next_descriptor_;
    while (files_.contains(target)) {
        target = target == std::numeric_limits<int>::max() ? 3 : target + 1;
    }
    // Sharing the FileRecord gives both descriptors the same handle and file
    // offset, which is exactly dup's shared-file-description semantics.
    files_.emplace(target, found->second);
    next_descriptor_ = target == std::numeric_limits<int>::max() ? 3 : target + 1;
    return target;
}

int BionicFileSystem::Dup2(int descriptor, int target) {
    if (target < 0) {
        SetErrno(kBadDescriptor);
        return -1;
    }
    const std::lock_guard lock(files_mutex_);
    const auto found = files_.find(descriptor);
    if (found == files_.end()) {
        SetErrno(kBadDescriptor);
        return -1;
    }
    if (descriptor == target) return target;  // dup2 no-op keeps the descriptor
    files_.erase(target);  // close the target slot if it was open
    files_.emplace(target, found->second);
    return target;
}

int BionicFileSystem::Dup3(int descriptor, int target, std::uint64_t) {
    if (descriptor == target) {
        SetErrno(kInvalid);  // dup3 rejects identical fds, unlike dup2
        return -1;
    }
    return Dup2(descriptor, target);
}

bool BionicFileSystem::ValidateOutput(std::uint64_t address,
                                      std::size_t size) {
    if (address_space_ == nullptr) {
        SetErrno(kBadAddress);
        return false;
    }
    if (size == 0) return true;
    std::vector<std::uint8_t> existing(size);
    if (!address_space_->Read(address, existing.data(), existing.size()) ||
        !address_space_->Write(address, existing.data(), existing.size())) {
        SetErrno(kBadAddress);
        return false;
    }
    return true;
}

std::int64_t BionicFileSystem::ReadLocked(
    const std::shared_ptr<FileRecord>& file, std::uint64_t output_address,
    std::size_t size) {
    if (!file->readable) {
        SetErrno(kAccessDenied);
        return -1;
    }
    if (!ValidateOutput(output_address, size)) return -1;
    if (size == 0) return 0;
    std::vector<std::uint8_t> buffer(size);
    if (file->kind == FileRecord::Kind::SystemRandom) {
        if (BCryptGenRandom(nullptr, buffer.data(),
                            static_cast<ULONG>(buffer.size()),
                            BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
            SetErrno(kIo);
            return -1;
        }
        if (!address_space_->Write(output_address, buffer.data(),
                                   buffer.size())) {
            SetErrno(kBadAddress);
            return -1;
        }
        return static_cast<std::int64_t>(buffer.size());
    }
    if (file->kind == FileRecord::Kind::VirtualText) {
        const std::size_t remaining =
            file->read_position < file->content.size()
                ? file->content.size() - file->read_position
                : 0;
        const std::size_t moved = std::min(remaining, size);
        if (moved != 0 &&
            !address_space_->Write(output_address,
                                   file->content.data() + file->read_position,
                                   moved)) {
            SetErrno(kBadAddress);
            return -1;
        }
        file->read_position += moved;
        return static_cast<std::int64_t>(moved);
    }
    DWORD read = 0;
    if (!ReadFile(static_cast<HANDLE>(file->handle), buffer.data(),
                  static_cast<DWORD>(buffer.size()), &read, nullptr)) {
        SetErrno(WindowsErrorToErrno(GetLastError()));
        return -1;
    }
    if (file->content_trace) {
        for (DWORD i = 0; i < read; ++i) {
            file->served_hash =
                (file->served_hash ^ buffer[i]) * 1099511628211ULL;
        }
        file->served_bytes += read;
    }
    if (!address_space_->Write(output_address, buffer.data(), read)) {
        SetErrno(kBadAddress);
        return -1;
    }
    return static_cast<std::int64_t>(read);
}

std::int64_t BionicFileSystem::WriteLocked(
    const std::shared_ptr<FileRecord>& file, std::uint64_t input_address,
    std::size_t size, std::string* failure_stage) {
    if (!file->writable) {
        SetError(failure_stage, "file-not-writable");
        SetErrno(kAccessDenied);
        return -1;
    }
    if (size == 0) return 0;
    std::vector<std::uint8_t> buffer(size);
    if (!address_space_->Read(input_address, buffer.data(), buffer.size())) {
        SetError(failure_stage, "guest-range-not-readable");
        SetErrno(kBadAddress);
        return -1;
    }
    // Bounded diagnostic: surface text-looking guest writes (fatal/assert
    // reports reach us via write(), not __android_log) so abort/crash paths
    // expose their message. Limited count and preview keep it quiet.
    {
        static std::atomic<unsigned> guest_write_logged = 0;
        std::size_t printable = 0;
        for (std::uint8_t byte : buffer)
            if ((byte >= 0x20 && byte <= 0x7e) || byte == '\n' || byte == '\r' ||
                byte == '\t')
                ++printable;
        if (size >= 4 && printable * 4 >= size * 3 &&
            guest_write_logged.fetch_add(1) < 64) {
            std::string preview;
            preview.reserve(160);
            for (std::uint8_t byte : buffer) {
                if (preview.size() >= 160) break;
                preview.push_back(
                    (byte >= 0x20 && byte <= 0x7e)
                        ? static_cast<char>(byte)
                        : (byte == '\n' || byte == '\r' || byte == '\t' ? ' '
                                                                        : '.'));
            }
            std::fprintf(stderr, "ISOMR_GUEST_WRITE len=%llu text=%s\n",
                         static_cast<unsigned long long>(size),
                         preview.c_str());
        }
    }
    if (file->append) {
        LARGE_INTEGER zero{};
        if (!SetFilePointerEx(static_cast<HANDLE>(file->handle), zero, nullptr,
                              FILE_END)) {
            const DWORD last_error = GetLastError();
            SetError(failure_stage,
                     "append-seek-win32-" + std::to_string(last_error));
            SetErrno(WindowsErrorToErrno(last_error));
            return -1;
        }
    }
    DWORD written = 0;
    if (!WriteFile(static_cast<HANDLE>(file->handle), buffer.data(),
                   static_cast<DWORD>(buffer.size()), &written, nullptr)) {
        const DWORD last_error = GetLastError();
        SetError(failure_stage,
                 "write-win32-" + std::to_string(last_error));
        SetErrno(WindowsErrorToErrno(last_error));
        return -1;
    }
    return static_cast<std::int64_t>(written);
}

std::int64_t BionicFileSystem::Read(int descriptor,
                                    std::uint64_t output_address,
                                    std::uint64_t size) {
    if (size > maximum_transfer_ || size > MAXDWORD) {
        // Large guest reads (PvZ1 streams the 30 MB main.rsb entry in one
        // read) are satisfied by chunking so the per-call Win32 transfer
        // stays bounded instead of failing the whole read.
        if (size > MAXDWORD) {
            SetErrno(kInvalid);
            return -1;
        }
        const std::shared_ptr<FileRecord> file = Find(descriptor);
        if (!file) {
            SetErrno(kBadDescriptor);
            return -1;
        }
        std::int64_t total = 0;
        std::uint64_t remaining = size;
        std::uint64_t cursor = output_address;
        while (remaining > 0) {
            const std::uint64_t chunk =
                remaining > maximum_transfer_ ? maximum_transfer_ : remaining;
            const std::int64_t moved =
                Read(descriptor, cursor, chunk);
            if (moved <= 0) {
                if (total == 0) return moved < 0 ? -1 : 0;
                break;
            }
            total += moved;
            cursor += static_cast<std::uint64_t>(moved);
            remaining -= static_cast<std::uint64_t>(moved);
            if (static_cast<std::uint64_t>(moved) < chunk) break;
        }
        return total;
    }
    const std::shared_ptr<FileRecord> file = Find(descriptor);
    if (!file) {
        SetErrno(kBadDescriptor);
        return -1;
    }
    const std::lock_guard lock(file->mutex);
    const std::int64_t result =
        ReadLocked(file, output_address, static_cast<std::size_t>(size));
    if (file->obb_diagnostic && file->obb_read_logged < 64) {
        ++file->obb_read_logged;
        std::fprintf(stderr,
                     "ISOMR_OBB_READ read fd=%d size=%llu result=%lld\n",
                     descriptor, static_cast<unsigned long long>(size),
                     static_cast<long long>(result));
        if (result > 0 && size >= 8 && size <= 96) {
            std::string preview(static_cast<std::size_t>(result), '\0');
            if (address_space_ != nullptr &&
                address_space_->Read(output_address, preview.data(),
                                     preview.size())) {
                for (char& c : preview)
                    if (c == '\0') c = ' ';
                std::fprintf(stderr, "ISOMR_OBB_READ data fd=%d %s\n",
                             descriptor, preview.c_str());
            }
        }
    }
    return result;
}

std::int64_t BionicFileSystem::Pread64(int descriptor,
                                       std::uint64_t output_address,
                                       std::uint64_t size,
                                       std::int64_t offset) {
    return Pread64(RetainFile(descriptor), output_address, size, offset);
}

std::int64_t BionicFileSystem::Pread64(
    const FileReference& reference, std::uint64_t output_address,
    std::uint64_t size, std::int64_t offset) {
    if (offset < 0 || size > maximum_transfer_ || size > MAXDWORD) {
        SetErrno(kInvalid);
        return -1;
    }
    const std::shared_ptr<FileRecord> file = Resolve(reference);
    if (!file) {
        SetErrno(kBadDescriptor);
        return -1;
    }
    const std::lock_guard lock(file->mutex);
    if (file->kind == FileRecord::Kind::SystemRandom) {
        SetErrno(kIllegalSeek);
        return -1;
    }
    if (file->kind == FileRecord::Kind::VirtualText) {
        if (static_cast<std::uint64_t>(offset) >= file->content.size())
            return 0;
        const std::size_t available =
            file->content.size() - static_cast<std::size_t>(offset);
        const std::size_t moved =
            std::min(available, static_cast<std::size_t>(size));
        if (moved != 0 &&
            !address_space_->Write(output_address,
                                   file->content.data() + offset, moved)) {
            SetErrno(kBadAddress);
            return -1;
        }
        return static_cast<std::int64_t>(moved);
    }
    LARGE_INTEGER original{};
    LARGE_INTEGER zero{};
    if (!SetFilePointerEx(static_cast<HANDLE>(file->handle), zero, &original,
                          FILE_CURRENT)) {
        SetErrno(WindowsErrorToErrno(GetLastError()));
        return -1;
    }
    LARGE_INTEGER target{};
    target.QuadPart = offset;
    if (!SetFilePointerEx(static_cast<HANDLE>(file->handle), target, nullptr,
                          FILE_BEGIN)) {
        SetErrno(WindowsErrorToErrno(GetLastError()));
        return -1;
    }
    const std::int64_t result =
        ReadLocked(file, output_address, static_cast<std::size_t>(size));
    const BOOL restored = SetFilePointerEx(static_cast<HANDLE>(file->handle),
                                           original, nullptr, FILE_BEGIN);
    if (!restored && result >= 0) {
        SetErrno(WindowsErrorToErrno(GetLastError()));
        return -1;
    }
    if (file->obb_diagnostic && file->obb_read_logged < 64) {
        ++file->obb_read_logged;
        std::fprintf(stderr,
                     "ISOMR_OBB_READ pread offset=%lld size=%llu result=%lld\n",
                     static_cast<long long>(offset),
                     static_cast<unsigned long long>(size),
                     static_cast<long long>(result));
    }
    return result;
}

std::int64_t BionicFileSystem::Write(int descriptor,
                                     std::uint64_t input_address,
                                     std::uint64_t size) {
    if (size > maximum_transfer_ || size > MAXDWORD) {
        SetErrno(kInvalid);
        return -1;
    }
    const std::shared_ptr<FileRecord> file = Find(descriptor);
    if (!file) {
        SetErrno(kBadDescriptor);
        return -1;
    }
    const std::lock_guard lock(file->mutex);
    return WriteLocked(file, input_address, static_cast<std::size_t>(size));
}

std::int64_t BionicFileSystem::Pwrite64(int descriptor,
                                        std::uint64_t input_address,
                                        std::uint64_t size,
                                        std::int64_t offset) {
    return Pwrite64(RetainFile(descriptor), input_address, size, offset);
}

std::int64_t BionicFileSystem::Pwrite64(
    const FileReference& reference, std::uint64_t input_address,
    std::uint64_t size, std::int64_t offset) {
    return Pwrite64(reference, input_address, size, offset, nullptr);
}

std::int64_t BionicFileSystem::Pwrite64(
    const FileReference& reference, std::uint64_t input_address,
    std::uint64_t size, std::int64_t offset, std::string* failure_stage) {
    if (failure_stage != nullptr) failure_stage->clear();
    if (offset < 0 || size > maximum_transfer_ || size > MAXDWORD) {
        SetError(failure_stage, "invalid-range");
        SetErrno(kInvalid);
        return -1;
    }
    const std::shared_ptr<FileRecord> file = Resolve(reference);
    if (!file) {
        SetError(failure_stage, "invalid-reference");
        SetErrno(kBadDescriptor);
        return -1;
    }
    const std::lock_guard lock(file->mutex);
    if (file->kind == FileRecord::Kind::SystemRandom) {
        SetError(failure_stage, "unsupported-file-kind");
        SetErrno(kAccessDenied);
        return -1;
    }
    LARGE_INTEGER original{};
    LARGE_INTEGER zero{};
    if (!SetFilePointerEx(static_cast<HANDLE>(file->handle), zero, &original,
                          FILE_CURRENT)) {
        const DWORD last_error = GetLastError();
        SetError(failure_stage,
                 "query-position-win32-" + std::to_string(last_error));
        SetErrno(WindowsErrorToErrno(last_error));
        return -1;
    }
    LARGE_INTEGER target{};
    target.QuadPart = offset;
    if (!SetFilePointerEx(static_cast<HANDLE>(file->handle), target, nullptr,
                          FILE_BEGIN)) {
        const DWORD last_error = GetLastError();
        SetError(failure_stage,
                 "seek-win32-" + std::to_string(last_error));
        SetErrno(WindowsErrorToErrno(last_error));
        return -1;
    }
    const bool append = file->append;
    file->append = false;
    const std::int64_t result =
        WriteLocked(file, input_address, static_cast<std::size_t>(size),
                    failure_stage);
    file->append = append;
    const BOOL restored = SetFilePointerEx(static_cast<HANDLE>(file->handle),
                                           original, nullptr, FILE_BEGIN);
    if (!restored && result >= 0) {
        const DWORD last_error = GetLastError();
        SetError(failure_stage,
                 "restore-position-win32-" + std::to_string(last_error));
        SetErrno(WindowsErrorToErrno(last_error));
        return -1;
    }
    return result;
}

int BionicFileSystem::Truncate(int descriptor, std::int64_t size) {
    if (size < 0) {
        SetErrno(kInvalid);
        return -1;
    }
    const std::shared_ptr<FileRecord> file = Find(descriptor);
    if (!file) {
        SetErrno(kBadDescriptor);
        return -1;
    }
    const std::lock_guard lock(file->mutex);
    if (!file->writable) {
        SetErrno(kAccessDenied);
        return -1;
    }
    LARGE_INTEGER original{};
    LARGE_INTEGER zero{};
    if (!SetFilePointerEx(static_cast<HANDLE>(file->handle), zero, &original,
                          FILE_CURRENT)) {
        SetErrno(WindowsErrorToErrno(GetLastError()));
        return -1;
    }
    LARGE_INTEGER target{};
    target.QuadPart = size;
    if (!SetFilePointerEx(static_cast<HANDLE>(file->handle), target, nullptr,
                          FILE_BEGIN) ||
        !SetEndOfFile(static_cast<HANDLE>(file->handle))) {
        SetErrno(WindowsErrorToErrno(GetLastError()));
        (void)SetFilePointerEx(static_cast<HANDLE>(file->handle), original,
                               nullptr, FILE_BEGIN);
        return -1;
    }
    if (!SetFilePointerEx(static_cast<HANDLE>(file->handle), original, nullptr,
                          FILE_BEGIN)) {
        SetErrno(WindowsErrorToErrno(GetLastError()));
        return -1;
    }
    return 0;
}

int BionicFileSystem::Sync(int descriptor) {
    const std::shared_ptr<FileRecord> file = Find(descriptor);
    if (!file) {
        SetErrno(kBadDescriptor);
        return -1;
    }
    const std::lock_guard lock(file->mutex);
    if (file->kind == FileRecord::Kind::SystemRandom ||
        file->kind == FileRecord::Kind::VirtualText) {
        SetErrno(kInvalid);
        return -1;
    }
    if (!FlushFileBuffers(static_cast<HANDLE>(file->handle))) {
        SetErrno(WindowsErrorToErrno(GetLastError()));
        return -1;
    }
    return 0;
}

std::int64_t BionicFileSystem::Lseek64(int descriptor, std::int64_t offset,
                                       int origin) {
    if (origin < 0 || origin > 2) {
        SetErrno(kInvalid);
        return -1;
    }
    const std::shared_ptr<FileRecord> file = Find(descriptor);
    if (!file) {
        SetErrno(kBadDescriptor);
        return -1;
    }
    const std::lock_guard lock(file->mutex);
    if (file->kind == FileRecord::Kind::SystemRandom) {
        SetErrno(kIllegalSeek);
        return -1;
    }
    if (file->kind == FileRecord::Kind::VirtualText) {
        std::int64_t base = 0;
        if (origin == 0) base = 0;
        else if (origin == 1)
            base = static_cast<std::int64_t>(file->read_position);
        else base = static_cast<std::int64_t>(file->content.size());
        const std::int64_t target = base + offset;
        if (target < 0) {
            SetErrno(kInvalid);
            return -1;
        }
        file->read_position = static_cast<std::size_t>(target);
        return target;
    }
    LARGE_INTEGER distance{};
    distance.QuadPart = offset;
    LARGE_INTEGER position{};
    const DWORD method = origin == 0 ? FILE_BEGIN
                        : origin == 1 ? FILE_CURRENT
                                      : FILE_END;
    if (!SetFilePointerEx(static_cast<HANDLE>(file->handle), distance,
                          &position, method)) {
        SetErrno(WindowsErrorToErrno(GetLastError()));
        return -1;
    }
    if (file->obb_diagnostic && file->obb_read_logged < 64) {
        ++file->obb_read_logged;
        std::fprintf(stderr,
                     "ISOMR_OBB_READ lseek fd=%d offset=%lld origin=%d result=%lld\n",
                     descriptor, static_cast<long long>(offset), origin,
                     static_cast<long long>(position.QuadPart));
    }
    return position.QuadPart;
}

int BionicFileSystem::FileControl(int descriptor, int command,
                                  std::uint64_t argument_address) {
    if (command != kFileGetLock && command != kFileSetLock) {
        SetErrno(kInvalid);
        return -1;
    }
    BionicFlock flock{};
    if (address_space_ == nullptr ||
        !address_space_->Read(argument_address, &flock, sizeof(flock))) {
        SetErrno(kBadAddress);
        return -1;
    }
    if ((flock.type != kReadLock && flock.type != kWriteLock &&
         flock.type != kUnlock) ||
        flock.origin < 0 || flock.origin > 2) {
        SetErrno(kInvalid);
        return -1;
    }
    if (command == kFileGetLock && flock.type == kUnlock) {
        SetErrno(kInvalid);
        return -1;
    }
    const std::shared_ptr<FileRecord> file = Find(descriptor);
    if (!file) {
        SetErrno(kBadDescriptor);
        return -1;
    }
    const std::lock_guard lock(file->mutex);
    if (file->kind == FileRecord::Kind::SystemRandom ||
        file->kind == FileRecord::Kind::VirtualText) {
        SetErrno(kInvalid);
        return -1;
    }
    if ((flock.type == kReadLock && !file->readable) ||
        (flock.type == kWriteLock && !file->writable)) {
        SetErrno(kBadDescriptor);
        return -1;
    }
    HANDLE handle = static_cast<HANDLE>(file->handle);
    LARGE_INTEGER base{};
    if (flock.origin == 1) {
        LARGE_INTEGER zero{};
        if (!SetFilePointerEx(handle, zero, &base, FILE_CURRENT)) {
            SetErrno(WindowsErrorToErrno(GetLastError()));
            return -1;
        }
    } else if (flock.origin == 2) {
        if (!GetFileSizeEx(handle, &base)) {
            SetErrno(WindowsErrorToErrno(GetLastError()));
            return -1;
        }
    }
    if ((flock.start > 0 &&
         base.QuadPart > std::numeric_limits<std::int64_t>::max() -
                             flock.start) ||
        (flock.start < 0 &&
         base.QuadPart < std::numeric_limits<std::int64_t>::min() -
                             flock.start)) {
        SetErrno(kOverflow);
        return -1;
    }
    std::int64_t start = base.QuadPart + flock.start;
    std::uint64_t length = 0;
    if (flock.length < 0) {
        if (flock.length == std::numeric_limits<std::int64_t>::min() ||
            start < -flock.length) {
            SetErrno(kInvalid);
            return -1;
        }
        start += flock.length;
        length = static_cast<std::uint64_t>(-flock.length);
    } else if (flock.length == 0) {
        if (start < 0) {
            SetErrno(kInvalid);
            return -1;
        }
        length = static_cast<std::uint64_t>(
                     std::numeric_limits<std::int64_t>::max() - start) +
                 1ULL;
    } else {
        length = static_cast<std::uint64_t>(flock.length);
    }
    if (start < 0 || length == 0 ||
        length - 1ULL >
            static_cast<std::uint64_t>(
                std::numeric_limits<std::int64_t>::max() - start)) {
        SetErrno(kInvalid);
        return -1;
    }
    const std::uint64_t unsigned_start = static_cast<std::uint64_t>(start);
    const std::uint64_t range_start = unsigned_start;
    const std::uint64_t range_end = range_start + length;
    if (command == kFileGetLock) {
        OVERLAPPED overlapped{};
        overlapped.Offset = static_cast<DWORD>(range_start);
        overlapped.OffsetHigh = static_cast<DWORD>(range_start >> 32U);
        DWORD flags = LOCKFILE_FAIL_IMMEDIATELY;
        if (flock.type == kWriteLock) flags |= LOCKFILE_EXCLUSIVE_LOCK;
        if (LockFileEx(handle, flags, 0, static_cast<DWORD>(length),
                       static_cast<DWORD>(length >> 32U), &overlapped)) {
            if (!UnlockFileEx(handle, 0, static_cast<DWORD>(length),
                              static_cast<DWORD>(length >> 32U),
                              &overlapped)) {
                SetErrno(WindowsLockErrorToErrno(GetLastError()));
                return -1;
            }
            flock.type = kUnlock;
            flock.process_id = 0;
        } else {
            const int mapped_error = WindowsLockErrorToErrno(GetLastError());
            if (mapped_error != kTryAgain) {
                SetErrno(mapped_error);
                return -1;
            }
            // Win32 does not expose the owner or shared/exclusive mode of a
            // conflicting byte-range lock. Conservatively report a write
            // lock with an unknown owner while preserving the queried range.
            flock.type = kWriteLock;
            flock.process_id = -1;
        }
        if (!address_space_->Write(argument_address, &flock, sizeof(flock))) {
            SetErrno(kBadAddress);
            return -1;
        }
        return 0;
    }
    std::vector<FileRecord::LockRecord> desired_locks;
    try {
        desired_locks.reserve(file->locks.size() + 2);
        for (const FileRecord::LockRecord& existing : file->locks) {
            const std::uint64_t existing_end =
                existing.start + existing.length;
            if (range_start >= existing_end || existing.start >= range_end) {
                desired_locks.push_back(existing);
                continue;
            }
            if (existing.start < range_start) {
                desired_locks.push_back(
                    {existing.start, range_start - existing.start,
                     existing.type});
            }
            if (existing_end > range_end) {
                desired_locks.push_back(
                    {range_end, existing_end - range_end, existing.type});
            }
        }
        if (flock.type != kUnlock) {
            desired_locks.push_back({range_start, length, flock.type});
        }
        std::sort(desired_locks.begin(), desired_locks.end(),
                  [](const FileRecord::LockRecord& left,
                     const FileRecord::LockRecord& right) {
                      return left.start < right.start;
                  });
        std::vector<FileRecord::LockRecord> merged_locks;
        merged_locks.reserve(desired_locks.size());
        for (const FileRecord::LockRecord& candidate : desired_locks) {
            if (!merged_locks.empty() &&
                merged_locks.back().type == candidate.type &&
                merged_locks.back().start + merged_locks.back().length >=
                    candidate.start) {
                const std::uint64_t candidate_end =
                    candidate.start + candidate.length;
                const std::uint64_t merged_end =
                    std::max(merged_locks.back().start +
                                 merged_locks.back().length,
                             candidate_end);
                merged_locks.back().length =
                    merged_end - merged_locks.back().start;
            } else {
                merged_locks.push_back(candidate);
            }
        }
        desired_locks = std::move(merged_locks);
    } catch (...) {
        SetErrno(kNoLocks);
        return -1;
    }
    const auto same_locks = [](const std::vector<FileRecord::LockRecord>& left,
                               const std::vector<FileRecord::LockRecord>& right) {
        if (left.size() != right.size()) return false;
        for (std::size_t index = 0; index < left.size(); ++index) {
            if (left[index].start != right[index].start ||
                left[index].length != right[index].length ||
                left[index].type != right[index].type)
                return false;
        }
        return true;
    };
    if (same_locks(file->locks, desired_locks)) {
        return 0;
    }
    const auto unlock_record = [handle](
                                   const FileRecord::LockRecord& record) {
        OVERLAPPED overlapped{};
        overlapped.Offset = static_cast<DWORD>(record.start);
        overlapped.OffsetHigh = static_cast<DWORD>(record.start >> 32U);
        return UnlockFileEx(handle, 0, static_cast<DWORD>(record.length),
                            static_cast<DWORD>(record.length >> 32U),
                            &overlapped) != FALSE;
    };
    const auto lock_record = [handle](
                                 const FileRecord::LockRecord& record) {
        OVERLAPPED overlapped{};
        overlapped.Offset = static_cast<DWORD>(record.start);
        overlapped.OffsetHigh = static_cast<DWORD>(record.start >> 32U);
        DWORD flags = LOCKFILE_FAIL_IMMEDIATELY;
        if (record.type == kWriteLock) flags |= LOCKFILE_EXCLUSIVE_LOCK;
        return LockFileEx(handle, flags, 0,
                          static_cast<DWORD>(record.length),
                          static_cast<DWORD>(record.length >> 32U),
                          &overlapped) != FALSE;
    };
    std::size_t released_count = 0;
    for (; released_count < file->locks.size(); ++released_count) {
        if (unlock_record(file->locks[released_count])) continue;
        const int mapped_error = WindowsLockErrorToErrno(GetLastError());
        for (std::size_t restore = 0; restore < released_count; ++restore)
            (void)lock_record(file->locks[restore]);
        SetErrno(mapped_error);
        return -1;
    }
    std::size_t acquired_count = 0;
    for (; acquired_count < desired_locks.size(); ++acquired_count) {
        if (lock_record(desired_locks[acquired_count])) continue;
        const int mapped_error = WindowsLockErrorToErrno(GetLastError());
        for (std::size_t release = 0; release < acquired_count; ++release)
            (void)unlock_record(desired_locks[release]);
        bool restored = true;
        for (const FileRecord::LockRecord& previous : file->locks)
            restored = lock_record(previous) && restored;
        const int reported_error = restored ? mapped_error : kInvalid;
        SetErrno(reported_error);
        return -1;
    }
    file->locks = std::move(desired_locks);
    return 0;
}

int BionicFileSystem::Access(std::uint64_t path_address, int mode) {
    if ((mode & ~4) != 0) {
        SetErrno(kAccessDenied);
        return -1;
    }
    std::string guest_path;
    if (!ReadGuestPath(path_address, &guest_path)) return -1;
    if (guest_path == kSystemRandomPath) return 0;
    if (VirtualFileContent(guest_path)) return 0;
    std::wstring host_path;
    std::wstring final_root;
    if (!ResolvePath(path_address, &host_path, &final_root)) return -1;
    HANDLE handle = CreateFileW(
        host_path.c_str(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        const DWORD open_error = GetLastError();
        if (open_error == ERROR_FILE_NOT_FOUND ||
            open_error == ERROR_PATH_NOT_FOUND)
            LogFileMiss("access", guest_path, host_path);
        SetErrno(WindowsErrorToErrno(open_error));
        return -1;
    }
    const bool valid = ValidateOpenedPath(handle, final_root);
    CloseHandle(handle);
    return valid ? 0 : -1;
}

bool BionicFileSystem::ReadStat(void* handle, BionicStat64* output) {
    if (handle == nullptr || handle == INVALID_HANDLE_VALUE || output == nullptr) {
        SetErrno(kInvalid);
        return false;
    }
    BY_HANDLE_FILE_INFORMATION information{};
    if (!GetFileInformationByHandle(static_cast<HANDLE>(handle),
                                    &information)) {
        SetErrno(WindowsErrorToErrno(GetLastError()));
        return false;
    }
    const std::uint64_t unsigned_size =
        static_cast<std::uint64_t>(information.nFileSizeHigh) << 32U |
        information.nFileSizeLow;
    if (unsigned_size >
        static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        SetErrno(kOverflow);
        return false;
    }
    BionicStat64 value{};
    value.device = information.dwVolumeSerialNumber;
    value.inode =
        static_cast<std::uint64_t>(information.nFileIndexHigh) << 32U |
        information.nFileIndexLow;
    value.mode =
        (information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0
            ? kSymbolicLinkMode
        : (information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0
            ? kDirectoryMode
            : kRegularMode;
    value.link_count = information.nNumberOfLinks;
    value.size = static_cast<std::int64_t>(unsigned_size);
    value.block_size = 4096;
    value.block_count = static_cast<std::int64_t>(
        unsigned_size / 512ULL + (unsigned_size % 512ULL != 0 ? 1 : 0));
    FileTimeToUnix(information.ftLastAccessTime, &value.access_seconds,
                   &value.access_nanoseconds);
    FileTimeToUnix(information.ftLastWriteTime, &value.modification_seconds,
                   &value.modification_nanoseconds);
    FileTimeToUnix(information.ftCreationTime, &value.change_seconds,
                   &value.change_nanoseconds);
    *output = value;
    return true;
}

int BionicFileSystem::WriteStat(void* handle,
                                std::uint64_t output_address) {
    BionicStat64 value{};
    if (!ReadStat(handle, &value)) return -1;
    return WriteStatValue(value, output_address);
}

int BionicFileSystem::WriteStatValue(const BionicStat64& value,
                                     std::uint64_t output_address) {
    if (!ValidateOutput(output_address, sizeof(BionicStat64))) return -1;
    if (!address_space_->Write(output_address, &value, sizeof(value))) {
        SetErrno(kBadAddress);
        return -1;
    }
    return 0;
}

int BionicFileSystem::WriteSystemRandomStat(
    std::uint64_t output_address) {
    BionicStat64 value{};
    value.mode = kCharacterMode;
    value.link_count = 1;
    value.block_size = 4096;
    return WriteStatValue(value, output_address);
}

int BionicFileSystem::WriteVirtualTextStat(
    const FileRecord& file, std::uint64_t output_address) {
    BionicStat64 value{};
    value.mode = kRegularMode;
    value.link_count = 1;
    value.block_size = 4096;
    value.size = static_cast<std::int64_t>(file.content.size());
    value.block_count = static_cast<std::int64_t>(
        file.content.size() / 512ULL + (file.content.size() % 512ULL != 0 ? 1 : 0));
    return WriteStatValue(value, output_address);
}

int BionicFileSystem::Stat(std::uint64_t path_address,
                           std::uint64_t output_address) {
    std::string guest_path;
    if (!ReadGuestPath(path_address, &guest_path)) return -1;
    if (guest_path == kSystemRandomPath)
        return WriteSystemRandomStat(output_address);
    if (const std::optional<std::string> virtual_content =
            VirtualFileContent(guest_path)) {
        FileRecord record{};
        record.content = *virtual_content;
        return WriteVirtualTextStat(record, output_address);
    }
    std::wstring host_path;
    std::wstring final_root;
    if (!ResolvePath(path_address, &host_path, &final_root)) return -1;
    HANDLE handle = CreateFileW(
        host_path.c_str(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        const DWORD open_error = GetLastError();
        if (open_error == ERROR_FILE_NOT_FOUND ||
            open_error == ERROR_PATH_NOT_FOUND)
            LogFileMiss("stat", guest_path, host_path);
        SetErrno(WindowsErrorToErrno(open_error));
        return -1;
    }
    if (!ValidateOpenedPath(handle, final_root)) {
        CloseHandle(handle);
        return -1;
    }
    const int result = WriteStat(handle, output_address);
    CloseHandle(handle);
    return result;
}

int BionicFileSystem::Lstat(std::uint64_t path_address,
                            std::uint64_t output_address) {
    std::string guest_path;
    if (!ReadGuestPath(path_address, &guest_path)) return -1;
    if (guest_path == kSystemRandomPath)
        return WriteSystemRandomStat(output_address);
    if (const std::optional<std::string> virtual_content =
            VirtualFileContent(guest_path)) {
        FileRecord record{};
        record.content = *virtual_content;
        return WriteVirtualTextStat(record, output_address);
    }
    std::wstring host_path;
    std::wstring final_root;
    if (!ResolvePath(path_address, &host_path, &final_root)) return -1;
    HANDLE handle = CreateFileW(
        host_path.c_str(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        SetErrno(WindowsErrorToErrno(GetLastError()));
        return -1;
    }
    if (!ValidateOpenedPath(handle, final_root)) {
        CloseHandle(handle);
        return -1;
    }
    const int result = WriteStat(handle, output_address);
    CloseHandle(handle);
    return result;
}

int BionicFileSystem::Fstat(int descriptor,
                            std::uint64_t output_address) {
    const std::shared_ptr<FileRecord> file = Find(descriptor);
    if (!file) {
        SetErrno(kBadDescriptor);
        return -1;
    }
    const std::lock_guard lock(file->mutex);
    if (file->kind == FileRecord::Kind::SystemRandom)
        return WriteSystemRandomStat(output_address);
    if (file->kind == FileRecord::Kind::VirtualText)
        return WriteVirtualTextStat(*file, output_address);
    return WriteStat(file->handle, output_address);
}

bool BionicFileSystem::QueryVolumeSpace(const std::wstring& opened_final_path,
                                        bool mount_writable,
                                        VolumeSpace* output) {
    std::array<wchar_t, MAX_PATH + 1> volume_path{};
    if (!GetVolumePathNameW(opened_final_path.c_str(), volume_path.data(),
                            static_cast<DWORD>(volume_path.size()))) {
        SetErrno(WindowsErrorToErrno(GetLastError()));
        return false;
    }
    DWORD sectors_per_cluster = 0;
    DWORD bytes_per_sector = 0;
    DWORD ignored_free_clusters = 0;
    DWORD ignored_total_clusters = 0;
    if (!GetDiskFreeSpaceW(volume_path.data(), &sectors_per_cluster,
                           &bytes_per_sector, &ignored_free_clusters,
                           &ignored_total_clusters)) {
        SetErrno(WindowsErrorToErrno(GetLastError()));
        return false;
    }
    const std::uint64_t block_size =
        static_cast<std::uint64_t>(sectors_per_cluster) *
        static_cast<std::uint64_t>(bytes_per_sector);
    if (block_size == 0) {
        SetErrno(kInvalid);
        return false;
    }
    ULARGE_INTEGER available_bytes{};
    ULARGE_INTEGER total_bytes{};
    ULARGE_INTEGER free_bytes{};
    if (!GetDiskFreeSpaceExW(volume_path.data(), &available_bytes,
                             &total_bytes, &free_bytes)) {
        SetErrno(WindowsErrorToErrno(GetLastError()));
        return false;
    }
    DWORD volume_serial = 0;
    DWORD maximum_component_length = 0;
    DWORD file_system_flags = 0;
    std::array<wchar_t, 64> file_system_name{};
    if (!GetVolumeInformationW(
            volume_path.data(), nullptr, 0, &volume_serial,
            &maximum_component_length, &file_system_flags,
            file_system_name.data(),
            static_cast<DWORD>(file_system_name.size()))) {
        SetErrno(WindowsErrorToErrno(GetLastError()));
        return false;
    }
    if (_wcsicmp(file_system_name.data(), L"NTFS") == 0)
        output->file_system_type = kNtfsSuperMagic;
    else if (_wcsicmp(file_system_name.data(), L"FAT") == 0 ||
             _wcsicmp(file_system_name.data(), L"FAT32") == 0)
        output->file_system_type = kMsdosSuperMagic;
    else if (_wcsicmp(file_system_name.data(), L"exFAT") == 0)
        output->file_system_type = kExfatSuperMagic;
    output->block_size = block_size;
    output->block_count = total_bytes.QuadPart / block_size;
    output->free_block_count = free_bytes.QuadPart / block_size;
    output->available_block_count = available_bytes.QuadPart / block_size;
    output->file_system_id = static_cast<std::int32_t>(volume_serial);
    output->maximum_name_length = maximum_component_length;
    output->read_only = !mount_writable ||
                        (file_system_flags & FILE_READ_ONLY_VOLUME) != 0;
    return true;
}

int BionicFileSystem::StatFileSystem(std::uint64_t path_address,
                                     std::uint64_t output_address) {
    if (!ValidateOutput(output_address, sizeof(BionicStatFs64))) return -1;
    std::wstring host_path;
    std::wstring final_root;
    bool mount_writable = false;
    if (!ResolvePath(path_address, &host_path, &final_root,
                     &mount_writable))
        return -1;
    HANDLE handle = CreateFileW(
        host_path.c_str(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        SetErrno(WindowsErrorToErrno(GetLastError()));
        return -1;
    }
    const std::optional<std::wstring> opened_path = FinalPath(handle);
    if (!opened_path || !HasPathPrefix(*opened_path, final_root)) {
        SetErrno(kAccessDenied);
        CloseHandle(handle);
        return -1;
    }
    VolumeSpace space{};
    if (!QueryVolumeSpace(*opened_path, mount_writable, &space)) {
        CloseHandle(handle);
        return -1;
    }
    CloseHandle(handle);

    BionicStatFs64 value{};
    value.file_system_type = space.file_system_type;
    value.block_size = space.block_size;
    value.block_count = space.block_count;
    value.free_block_count = space.free_block_count;
    value.available_block_count = space.available_block_count;
    value.file_system_id[0] = space.file_system_id;
    value.maximum_name_length = space.maximum_name_length;
    value.fragment_size = space.block_size;
    if (space.read_only) value.flags |= kStatFsReadOnly;
    if (!address_space_->Write(output_address, &value, sizeof(value))) {
        SetErrno(kBadAddress);
        return -1;
    }
    return 0;
}

int BionicFileSystem::StatVfs(std::uint64_t path_address,
                              std::uint64_t output_address) {
    if (!ValidateOutput(output_address, sizeof(BionicStatVfs))) return -1;
    std::wstring host_path;
    std::wstring final_root;
    bool mount_writable = false;
    if (!ResolvePath(path_address, &host_path, &final_root,
                     &mount_writable))
        return -1;
    HANDLE handle = CreateFileW(
        host_path.c_str(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        SetErrno(WindowsErrorToErrno(GetLastError()));
        return -1;
    }
    const std::optional<std::wstring> opened_path = FinalPath(handle);
    if (!opened_path || !HasPathPrefix(*opened_path, final_root)) {
        SetErrno(kAccessDenied);
        CloseHandle(handle);
        return -1;
    }
    VolumeSpace space{};
    if (!QueryVolumeSpace(*opened_path, mount_writable, &space)) {
        CloseHandle(handle);
        return -1;
    }
    CloseHandle(handle);

    BionicStatVfs value{};
    value.block_size = space.block_size;
    value.fragment_size = space.block_size;
    value.block_count = space.block_count;
    value.free_block_count = space.free_block_count;
    value.avail_block_count = space.available_block_count;
    value.file_count = space.file_count;
    value.free_file_count = space.free_file_count;
    value.avail_file_count = space.free_file_count;
    value.file_system_id = space.file_system_id;
    if (space.read_only) value.flags |= kStatVfsReadOnly;
    value.maximum_name_length = space.maximum_name_length;
    if (!address_space_->Write(output_address, &value, sizeof(value))) {
        SetErrno(kBadAddress);
        return -1;
    }
    return 0;
}

int BionicFileSystem::FstatVfs(int descriptor,
                               std::uint64_t output_address) {
    if (!ValidateOutput(output_address, sizeof(BionicStatVfs))) return -1;
    const std::shared_ptr<FileRecord> file = Find(descriptor);
    if (!file) {
        SetErrno(kBadDescriptor);
        return -1;
    }
    const std::lock_guard lock(file->mutex);
    const std::optional<std::wstring> opened_path = FinalPath(file->handle);
    if (!opened_path) {
        SetErrno(kAccessDenied);
        return -1;
    }
    VolumeSpace space{};
    if (!QueryVolumeSpace(*opened_path, file->writable, &space)) return -1;

    BionicStatVfs value{};
    value.block_size = space.block_size;
    value.fragment_size = space.block_size;
    value.block_count = space.block_count;
    value.free_block_count = space.free_block_count;
    value.avail_block_count = space.available_block_count;
    value.file_count = space.file_count;
    value.free_file_count = space.free_file_count;
    value.avail_file_count = space.free_file_count;
    value.file_system_id = space.file_system_id;
    if (space.read_only) value.flags |= kStatVfsReadOnly;
    value.maximum_name_length = space.maximum_name_length;
    if (!address_space_->Write(output_address, &value, sizeof(value))) {
        SetErrno(kBadAddress);
        return -1;
    }
    return 0;
}

int BionicFileSystem::MakeDirectory(std::uint64_t path_address,
                                    std::uint64_t mode) {
    if ((mode & ~0777ULL) != 0) {
        SetErrno(kInvalid);
        return -1;
    }
    {
        std::string mkdir_guest;
        if (ReadGuestPath(path_address, &mkdir_guest))
            LogFileOp("mkdir", mkdir_guest);
    }
    std::wstring host_path;
    std::wstring final_root;
    bool writable = false;
    bool mount_root = false;
    if (!ResolvePath(path_address, &host_path, &final_root, &writable,
                     &mount_root))
        return -1;
    if (!writable) {
        SetErrno(kAccessDenied);
        return -1;
    }
    if (mount_root) {
        SetErrno(kExists);
        return -1;
    }
    const std::size_t separator = host_path.find_last_of(L"\\/");
    if (separator == std::wstring::npos) {
        SetErrno(kAccessDenied);
        return -1;
    }
    const std::wstring parent_path = host_path.substr(0, separator);
    HANDLE parent = CreateFileW(
        parent_path.c_str(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (parent == INVALID_HANDLE_VALUE) {
        SetErrno(WindowsErrorToErrno(GetLastError()));
        return -1;
    }
    BY_HANDLE_FILE_INFORMATION parent_information{};
    const bool parent_valid =
        GetFileInformationByHandle(parent, &parent_information) &&
        (parent_information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 &&
        ValidateOpenedPath(parent, final_root);
    CloseHandle(parent);
    if (!parent_valid) return -1;
    if (!CreateDirectoryW(host_path.c_str(), nullptr)) {
        SetErrno(WindowsErrorToErrno(GetLastError()));
        return -1;
    }
    HANDLE created = CreateFileW(
        host_path.c_str(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (created == INVALID_HANDLE_VALUE) {
        SetErrno(WindowsErrorToErrno(GetLastError()));
        return -1;
    }
    const bool created_valid = ValidateOpenedPath(created, final_root);
    CloseHandle(created);
    if (!created_valid) return -1;
    return 0;
}

int BionicFileSystem::Chmod(std::uint64_t path_address, std::uint64_t mode) {
    std::wstring host_path;
    std::wstring final_root;
    bool writable = false;
    if (!ResolvePath(path_address, &host_path, &final_root, &writable))
        return -1;
    if (!writable) {
        SetErrno(kReadOnlyFileSystem);
        return -1;
    }
    DWORD attributes = GetFileAttributesW(host_path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        SetErrno(WindowsErrorToErrno(GetLastError()));
        return -1;
    }
    if ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) return 0;
    // Map the POSIX write bits to the Windows read-only attribute; a file
    // with any write bit stays writable, a fully read-only mode sets it.
    if ((mode & 0222) != 0)
        attributes &= ~FILE_ATTRIBUTE_READONLY;
    else
        attributes |= FILE_ATTRIBUTE_READONLY;
    if (!SetFileAttributesW(host_path.c_str(), attributes)) {
        SetErrno(WindowsErrorToErrno(GetLastError()));
        return -1;
    }
    return 0;
}

int BionicFileSystem::Fchmod(int descriptor, std::uint64_t mode) {
    std::shared_ptr<FileRecord> file = Find(descriptor);
    if (file == nullptr || file->handle == INVALID_HANDLE_VALUE) {
        SetErrno(kBadDescriptor);
        return -1;
    }
    BY_HANDLE_FILE_INFORMATION information{};
    if (!GetFileInformationByHandle(file->handle, &information)) {
        SetErrno(WindowsErrorToErrno(GetLastError()));
        return -1;
    }
    if ((information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
        return 0;
    FILE_BASIC_INFO basic{};
    // -1 in each time field leaves the timestamps unchanged; only the
    // FileAttributes member is applied.
    basic.CreationTime.QuadPart = -1;
    basic.LastAccessTime.QuadPart = -1;
    basic.LastWriteTime.QuadPart = -1;
    basic.ChangeTime.QuadPart = -1;
    basic.FileAttributes = information.dwFileAttributes;
    if ((mode & 0222) != 0)
        basic.FileAttributes &= ~FILE_ATTRIBUTE_READONLY;
    else
        basic.FileAttributes |= FILE_ATTRIBUTE_READONLY;
    if (!SetFileInformationByHandle(file->handle, FileBasicInfo, &basic,
                                    sizeof(basic))) {
        SetErrno(WindowsErrorToErrno(GetLastError()));
        return -1;
    }
    return 0;
}

int BionicFileSystem::Fchmodat(int dir_fd, std::uint64_t path_address,
                               std::uint64_t mode, std::uint64_t) {
    // AT_FDCWD (-100) and absolute paths both resolve through ResolvePath; a
    // real directory fd with a relative path falls back to the guest cwd,
    // which is the only base Unity uses for its il2cpp extraction chmods.
    (void)dir_fd;
    return Chmod(path_address, mode);
}

namespace {
constexpr std::int64_t kUtimeNow = 0x3fffffff;
constexpr std::int64_t kUtimeOmit = 0x3ffffffe;
constexpr std::int64_t kUnixEpochToFileTimeSeconds = 11644473600ll;

bool ReadGuestTimespecs(GuestAddressSpace* address_space,
                        std::uint64_t times_address,
                        std::int64_t out_seconds[2],
                        std::int64_t out_nanoseconds[2]) {
    if (times_address == 0) {
        // NULL times means "set both to now"; mark both UTIME_NOW.
        out_seconds[0] = out_seconds[1] = 0;
        out_nanoseconds[0] = out_nanoseconds[1] = kUtimeNow;
        return true;
    }
    // aarch64 timespec: { int64 tv_sec; int64 tv_nsec; } x2.
    std::int64_t raw[4]{};
    if (address_space == nullptr ||
        !address_space->Read(times_address, raw, sizeof(raw)))
        return false;
    out_seconds[0] = raw[0];
    out_nanoseconds[0] = raw[1];
    out_seconds[1] = raw[2];
    out_nanoseconds[1] = raw[3];
    return true;
}

std::int64_t TimespecToFileTime(std::int64_t seconds,
                                std::int64_t nanoseconds,
                                bool* omit) {
    if (nanoseconds == kUtimeOmit) {
        *omit = true;
        return -1;
    }
    *omit = false;
    if (nanoseconds == kUtimeNow) {
        FILETIME now{};
        GetSystemTimePreciseAsFileTime(&now);
        return (static_cast<std::int64_t>(now.dwHighDateTime) << 32) |
               now.dwLowDateTime;
    }
    return (seconds + kUnixEpochToFileTimeSeconds) * 10000000ll +
           nanoseconds / 100;
}

int ApplyGuestTimes(HANDLE handle, GuestAddressSpace* address_space,
                    std::uint64_t times_address) {
    std::int64_t seconds[2];
    std::int64_t nanoseconds[2];
    if (!ReadGuestTimespecs(address_space, times_address, seconds,
                            nanoseconds)) {
        return kBadAddress;
    }
    FILE_BASIC_INFO basic{};
    basic.CreationTime.QuadPart = -1;
    basic.ChangeTime.QuadPart = -1;
    basic.FileAttributes = 0;  // 0 leaves attributes unchanged.
    bool omit_access = false;
    bool omit_write = false;
    basic.LastAccessTime.QuadPart = TimespecToFileTime(
        seconds[0], nanoseconds[0], &omit_access);
    basic.LastWriteTime.QuadPart = TimespecToFileTime(
        seconds[1], nanoseconds[1], &omit_write);
    if (omit_access) basic.LastAccessTime.QuadPart = -1;
    if (omit_write) basic.LastWriteTime.QuadPart = -1;
    if (!SetFileInformationByHandle(handle, FileBasicInfo, &basic,
                                    sizeof(basic))) {
        return WindowsErrorToErrno(GetLastError());
    }
    return 0;
}
}  // namespace

int BionicFileSystem::Futimens(int descriptor, std::uint64_t times_address) {
    std::shared_ptr<FileRecord> file = Find(descriptor);
    if (file == nullptr || file->handle == INVALID_HANDLE_VALUE) {
        SetErrno(kBadDescriptor);
        return -1;
    }
    const int error = ApplyGuestTimes(file->handle, address_space_,
                                      times_address);
    if (error != 0) {
        SetErrno(error);
        return -1;
    }
    return 0;
}

int BionicFileSystem::Utimensat(int dir_fd, std::uint64_t path_address,
                                std::uint64_t times_address,
                                std::uint64_t) {
    (void)dir_fd;
    std::wstring host_path;
    std::wstring final_root;
    bool writable = false;
    if (!ResolvePath(path_address, &host_path, &final_root, &writable))
        return -1;
    if (!writable) {
        SetErrno(kReadOnlyFileSystem);
        return -1;
    }
    HANDLE handle = CreateFileW(
        host_path.c_str(), FILE_WRITE_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        SetErrno(WindowsErrorToErrno(GetLastError()));
        return -1;
    }
    const int error =
        ApplyGuestTimes(handle, address_space_, times_address);
    CloseHandle(handle);
    if (error != 0) {
        SetErrno(error);
        return -1;
    }
    return 0;
}

int BionicFileSystem::Deny(int error) {
    SetErrno(error);
    return -1;
}

int BionicFileSystem::Pipe(std::uint64_t output_address) {
    if (heap_ == nullptr) {
        SetErrno(kBadAddress);
        return -1;
    }
    HANDLE read_handle = nullptr;
    HANDLE write_handle = nullptr;
    if (!CreatePipe(&read_handle, &write_handle, nullptr, 0)) {
        SetErrno(WindowsErrorToErrno(GetLastError()));
        return -1;
    }
    auto read_file = std::make_shared<FileRecord>();
    read_file->kind = FileRecord::Kind::HostFile;
    read_file->handle = read_handle;
    read_file->readable = true;
    read_file->writable = false;
    const int read_descriptor = InsertFile(std::move(read_file));
    if (read_descriptor < 0) {
        CloseHandle(read_handle);
        CloseHandle(write_handle);
        SetErrno(kTooManyFiles);
        return -1;
    }
    auto write_file = std::make_shared<FileRecord>();
    write_file->kind = FileRecord::Kind::HostFile;
    write_file->handle = write_handle;
    write_file->readable = false;
    write_file->writable = true;
    const int write_descriptor = InsertFile(std::move(write_file));
    if (write_descriptor < 0) {
        CloseHandle(write_handle);
        Close(read_descriptor);
        SetErrno(kTooManyFiles);
        return -1;
    }
    // guest int fds[2]: [0] = read end, [1] = write end
    const std::int32_t pair[2] = {read_descriptor, write_descriptor};
    if (!address_space_->Write(output_address, pair, sizeof(pair))) {
        Close(read_descriptor);
        Close(write_descriptor);
        SetErrno(kBadAddress);
        return -1;
    }
    return 0;
}

int BionicFileSystem::Remove(std::uint64_t path_address) {
    return RemovePath(path_address, true);
}

int BionicFileSystem::Unlink(std::uint64_t path_address) {
    return RemovePath(path_address, false);
}

int BionicFileSystem::Rename(std::uint64_t old_address,
                             std::uint64_t new_address) {
    if (heap_ == nullptr) {
        SetErrno(kBadAddress);
        return -1;
    }
    {
        std::string old_guest;
        std::string new_guest;
        if (ReadGuestPath(old_address, &old_guest) &&
            ReadGuestPath(new_address, &new_guest)) {
            std::fprintf(stderr, "ISOMR_FILE_OP op=rename old=%s new=%s\n",
                         old_guest.c_str(), new_guest.c_str());
        }
    }
    std::wstring old_host;
    std::wstring old_root;
    bool old_writable = false;
    if (!ResolvePath(old_address, &old_host, &old_root, &old_writable))
        return -1;
    std::wstring new_host;
    std::wstring new_root;
    bool new_writable = false;
    if (!ResolvePath(new_address, &new_host, &new_root, &new_writable))
        return -1;
    if (old_root != new_root) {
        SetErrno(kAccessDenied);
        return -1;
    }
    // A rename mutates both endpoints; a read-only mount (the APK payload,
    // Unity's virtual /il2cpp managed-data root, pseudo files) must refuse it
    // so the guest sees a truthful read-only filesystem rather than a silent
    // host-side move.
    if (!old_writable || !new_writable) {
        SetErrno(kReadOnlyFileSystem);
        return -1;
    }
    if (!MoveFileExW(old_host.c_str(), new_host.c_str(),
                     MOVEFILE_REPLACE_EXISTING)) {
        SetErrno(WindowsErrorToErrno(GetLastError()));
        return -1;
    }
    return 0;
}

int BionicFileSystem::Link(std::uint64_t old_address,
                           std::uint64_t new_address) {
    if (heap_ == nullptr) {
        SetErrno(kBadAddress);
        return -1;
    }
    {
        std::string old_guest;
        std::string new_guest;
        if (ReadGuestPath(old_address, &old_guest) &&
            ReadGuestPath(new_address, &new_guest)) {
            LogFileOp("link", old_guest + " -> " + new_guest);
        }
    }
    std::wstring old_host;
    std::wstring old_root;
    bool old_writable = false;
    if (!ResolvePath(old_address, &old_host, &old_root, &old_writable))
        return -1;
    std::wstring new_host;
    std::wstring new_root;
    bool new_writable = false;
    if (!ResolvePath(new_address, &new_host, &new_root, &new_writable))
        return -1;
    if (old_root != new_root) {
        SetErrno(kCrossDevice);
        return -1;
    }
    if (!new_writable) {
        SetErrno(kReadOnlyFileSystem);
        return -1;
    }
    if (!CreateHardLinkW(new_host.c_str(), old_host.c_str(), nullptr)) {
        SetErrno(WindowsErrorToErrno(GetLastError()));
        return -1;
    }
    return 0;
}

int BionicFileSystem::Symlink(std::uint64_t target_address,
                              std::uint64_t link_address) {
    if (heap_ == nullptr) {
        SetErrno(kBadAddress);
        return -1;
    }
    std::string target_guest;
    std::string link_guest;
    const bool traced = ReadGuestPath(target_address, &target_guest) &&
                        ReadGuestPath(link_address, &link_guest);
    if (traced) LogFileOp("symlink", target_guest + " -> " + link_guest);
    std::wstring target_host;
    std::wstring target_root;
    if (!ResolvePath(target_address, &target_host, &target_root)) return -1;
    std::wstring link_host;
    std::wstring link_root;
    bool link_writable = false;
    if (!ResolvePath(link_address, &link_host, &link_root, &link_writable))
        return -1;
    if (!link_writable) {
        SetErrno(kReadOnlyFileSystem);
        return -1;
    }
    DWORD attributes = GetFileAttributesW(target_host.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        SetErrno(kNoEntry);
        return -1;
    }
    const DWORD flag = (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0
                           ? SYMBOLIC_LINK_FLAG_DIRECTORY
                           : 0;
    if (CreateSymbolicLinkW(link_host.c_str(), target_host.c_str(),
                            flag | SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE))
        return 0;
    // Unprivileged symlinks are unavailable on some hosts; for a regular
    // file a copy is the closest truthful emulation, while directories
    // cannot be faked cheaply and report EPERM.
    if ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0 ||
        !CopyFileW(target_host.c_str(), link_host.c_str(), TRUE)) {
        SetErrno(WindowsErrorToErrno(GetLastError()));
        return -1;
    }
    return 0;
}

std::uint64_t BionicFileSystem::OpenDirectory(
    std::uint64_t path_address) {
    if (heap_ == nullptr) {
        SetErrno(kBadAddress);
        return 0;
    }
    std::string dir_guest;
    if (ReadGuestPath(path_address, &dir_guest))
        LogFileOp("opendir", dir_guest);
    std::wstring host_path;
    std::wstring final_root;
    if (!ResolvePath(path_address, &host_path, &final_root)) return 0;
    HANDLE handle = CreateFileW(
        host_path.c_str(), FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        SetErrno(WindowsErrorToErrno(GetLastError()));
        return 0;
    }
    BY_HANDLE_FILE_INFORMATION information{};
    if (!GetFileInformationByHandle(handle, &information)) {
        const DWORD host_error = GetLastError();
        CloseHandle(handle);
        SetErrno(WindowsErrorToErrno(host_error));
        return 0;
    }
    if ((information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
        CloseHandle(handle);
        SetErrno(kNotDirectory);
        return 0;
    }
    if (!ValidateOpenedPath(handle, final_root)) {
        CloseHandle(handle);
        return 0;
    }

    std::shared_ptr<DirectoryRecord> directory =
        std::make_shared<DirectoryRecord>();
    directory->handle = handle;
    const std::uint64_t directory_inode =
        static_cast<std::uint64_t>(information.nFileIndexHigh) << 32U |
        information.nFileIndexLow;
    directory->entries.push_back(
        {directory_inode, kDirectoryTypeDirectory, "."});
    directory->entries.push_back({0, kDirectoryTypeDirectory, ".."});

    alignas(FILE_ID_BOTH_DIR_INFO)
        std::array<std::byte, 64U * 1024U> query_buffer{};
    bool restart = true;
    for (;;) {
        const FILE_INFO_BY_HANDLE_CLASS query_class =
            restart ? FileIdBothDirectoryRestartInfo
                    : FileIdBothDirectoryInfo;
        restart = false;
        if (!GetFileInformationByHandleEx(
                handle, query_class, query_buffer.data(),
                static_cast<DWORD>(query_buffer.size()))) {
            const DWORD host_error = GetLastError();
            if (host_error == ERROR_NO_MORE_FILES) break;
            SetErrno(WindowsErrorToErrno(host_error));
            return 0;
        }
        std::size_t record_offset = 0;
        for (;;) {
            if (record_offset + sizeof(FILE_ID_BOTH_DIR_INFO) >
                query_buffer.size()) {
                SetErrno(kIo);
                return 0;
            }
            const auto* entry = reinterpret_cast<const FILE_ID_BOTH_DIR_INFO*>(
                query_buffer.data() + record_offset);
            if ((entry->FileNameLength % sizeof(wchar_t)) != 0 ||
                entry->FileNameLength >
                    query_buffer.size() - record_offset -
                        offsetof(FILE_ID_BOTH_DIR_INFO, FileName)) {
                SetErrno(kIo);
                return 0;
            }
            const int wide_length = static_cast<int>(
                entry->FileNameLength / sizeof(wchar_t));
            std::string name;
            if (wide_length != 0) {
                const int required = WideCharToMultiByte(
                    CP_UTF8, WC_ERR_INVALID_CHARS, entry->FileName,
                    wide_length, nullptr, 0, nullptr, nullptr);
                if (required <= 0) {
                    SetErrno(kIllegalByteSequence);
                    return 0;
                }
                if (required >= static_cast<int>(
                                    sizeof(BionicDirent64{}.name))) {
                    SetErrno(kOverflow);
                    return 0;
                }
                name.resize(static_cast<std::size_t>(required));
                if (WideCharToMultiByte(
                        CP_UTF8, WC_ERR_INVALID_CHARS, entry->FileName,
                        wide_length, name.data(), required, nullptr,
                        nullptr) != required) {
                    SetErrno(kIllegalByteSequence);
                    return 0;
                }
            }
            if (name != "." && name != "..") {
                std::uint8_t type = kDirectoryTypeUnknown;
                if ((entry->FileAttributes &
                     FILE_ATTRIBUTE_REPARSE_POINT) != 0)
                    type = kDirectoryTypeLink;
                else if ((entry->FileAttributes &
                          FILE_ATTRIBUTE_DIRECTORY) != 0)
                    type = kDirectoryTypeDirectory;
                else
                    type = kDirectoryTypeRegular;
                directory->entries.push_back(
                    {static_cast<std::uint64_t>(entry->FileId.QuadPart),
                     type, std::move(name)});
            }
            if (entry->NextEntryOffset == 0) break;
            if (entry->NextEntryOffset >
                query_buffer.size() - record_offset) {
                SetErrno(kIo);
                return 0;
            }
            record_offset += entry->NextEntryOffset;
        }
    }

    std::sort(directory->entries.begin() + 2, directory->entries.end(),
              [](const DirectoryEntry& left, const DirectoryEntry& right) {
                  return left.name < right.name;
              });
    std::string ignored;
    directory->handle_address = heap_->Callocate(
        1, sizeof(std::uint64_t) + sizeof(BionicDirent64));
    if (directory->handle_address == 0) {
        SetErrno(kOutOfMemory);
        return 0;
    }
    directory->entry_address =
        directory->handle_address + sizeof(std::uint64_t);
    {
        const std::lock_guard lock(directories_mutex_);
        directories_.emplace(directory->handle_address, directory);
    }
    return directory->handle_address;
}

std::uint64_t BionicFileSystem::ReadDirectory(std::uint64_t directory) {
    std::shared_ptr<DirectoryRecord> record;
    {
        const std::lock_guard lock(directories_mutex_);
        const auto found = directories_.find(directory);
        if (found == directories_.end()) {
            SetErrno(kBadDescriptor);
            return 0;
        }
        record = found->second;
    }
    const std::lock_guard lock(record->mutex);
    if (record->next_entry >= record->entries.size()) return 0;
    const DirectoryEntry& source = record->entries[record->next_entry];
    BionicDirent64 output{};
    output.inode = source.inode;
    output.offset = 0;
    output.record_length = sizeof(output);
    output.type = source.type;
    std::memcpy(output.name, source.name.data(), source.name.size());
    if (!heap_->Write(record->entry_address, &output, sizeof(output))) {
        SetErrno(kBadAddress);
        return 0;
    }
    ++record->next_entry;
    return record->entry_address;
}

int BionicFileSystem::CloseDirectory(std::uint64_t directory) {
    std::shared_ptr<DirectoryRecord> record;
    {
        const std::lock_guard lock(directories_mutex_);
        const auto found = directories_.find(directory);
        if (found == directories_.end()) {
            SetErrno(kBadDescriptor);
            return -1;
        }
        record = found->second;
        directories_.erase(found);
    }
    std::string ignored;
    const bool handle_released =
        heap_->Release(record->handle_address, &ignored);
    if (!handle_released) {
        SetErrno(kInvalid);
        return -1;
    }
    return 0;
}

int BionicFileSystem::RemovePath(std::uint64_t path_address,
                                bool allow_directory) {
    std::string remove_guest;
    if (ReadGuestPath(path_address, &remove_guest))
        LogFileOp(allow_directory ? "rmdir" : "unlink", remove_guest);
    std::wstring host_path;
    std::wstring final_root;
    bool writable = false;
    bool mount_root = false;
    if (!ResolvePath(path_address, &host_path, &final_root, &writable,
                     &mount_root))
        return -1;
    if (!writable || mount_root) {
        SetErrno(kAccessDenied);
        return -1;
    }
    HANDLE target = CreateFileW(
        host_path.c_str(), DELETE | FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (target == INVALID_HANDLE_VALUE) {
        SetErrno(WindowsErrorToErrno(GetLastError()));
        return -1;
    }
    if (!ValidateOpenedPath(target, final_root)) {
        CloseHandle(target);
        return -1;
    }
    BY_HANDLE_FILE_INFORMATION target_information{};
    if (!GetFileInformationByHandle(target, &target_information)) {
        const DWORD last_error = GetLastError();
        CloseHandle(target);
        SetErrno(WindowsErrorToErrno(last_error));
        return -1;
    }
    if (!allow_directory &&
        (target_information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 &&
        (target_information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ==
            0) {
        CloseHandle(target);
        SetErrno(kIsDirectory);
        return -1;
    }
    FILE_DISPOSITION_INFO disposition{};
    disposition.DeleteFile = TRUE;
    if (!SetFileInformationByHandle(target, FileDispositionInfo,
                                    &disposition, sizeof(disposition))) {
        const DWORD last_error = GetLastError();
        CloseHandle(target);
        SetErrno(WindowsErrorToErrno(last_error));
        return -1;
    }
    CloseHandle(target);
    return 0;
}

std::uint64_t BionicFileSystem::GetCurrentDirectory(
    std::uint64_t output_address, std::uint64_t size) {
    if (output_address == 0) {
        SetErrno(kBadAddress);
        return 0;
    }
    if (current_directory_.empty()) {
        SetErrno(kNoEntry);
        return 0;
    }
    const std::size_t required = current_directory_.size() + 1;
    if (size < required) {
        SetErrno(kRange);
        return 0;
    }
    if (!ValidateOutput(output_address, required)) return 0;
    if (!address_space_->Write(output_address, current_directory_.data(),
                               current_directory_.size()) ||
        !address_space_->Write(output_address + current_directory_.size(),
                               "", 1)) {
        SetErrno(kBadAddress);
        return 0;
    }
    return output_address;
}

std::int64_t BionicFileSystem::ReadLink(std::uint64_t path_address,
                                        std::uint64_t output_address,
                                        std::uint64_t size) {
    std::string guest_path;
    if (!ReadGuestPath(path_address, &guest_path)) return -1;
    if (output_address == 0 || size == 0 ||
        size > std::numeric_limits<std::size_t>::max()) {
        SetErrno(kInvalid);
        return -1;
    }
    std::string target;
    const bool is_proc_exe =
        guest_path == "/proc/self/exe" ||
        guest_path == "/proc/thread-self/exe" ||
        (guest_path.size() > 6 && guest_path.rfind("/proc/", 0) == 0 &&
         guest_path.size() >= 4 &&
         guest_path.compare(guest_path.size() - 4, 4, "/exe") == 0);
    if (is_proc_exe) {
        // The running image's self path. For a Unity/il2cpp payload the
        // meaningful "executable" is the engine library under the app mount;
        // callers derive the native lib dir from it.
        target = (mounts_.empty() ? "/app" : mounts_.front().guest_prefix) +
                 "/lib/arm64-v8a/libmain.so";
    } else {
        std::fprintf(stderr, "ISOMR_READLINK path=%s\n",
                     guest_path.c_str());
        std::fflush(stderr);
        // We model no real symlink layer; a path that resolves to an existing
        // file is not a link (EINVAL), anything else does not exist.
        std::wstring host_path, final_root;
        // ResolvePath takes the guest address; reuse the string by checking
        // existence via a path lookup on the raw guest text.
        if (!ResolvePath(path_address, &host_path, &final_root)) {
            SetErrno(kNoEntry);
            return -1;
        }
        SetErrno(kInvalid);
        return -1;
    }
    const std::size_t written =
        (std::min)(target.size(), static_cast<std::size_t>(size));
    if (written != 0 &&
        !address_space_->Write(output_address, target.data(), written)) {
        SetErrno(kBadAddress);
        return -1;
    }
    return static_cast<std::int64_t>(written);
}

std::int64_t BionicFileSystem::ReadLinkAt(int dir_fd,
                                          std::uint64_t path_address,
                                          std::uint64_t output_address,
                                          std::uint64_t size) {
    // Absolute paths and AT_FDCWD both resolve the same way in this model;
    // a dir-fd-qualified relative path is rare here and still resolves
    // through the same guest path text.
    (void)dir_fd;
    return ReadLink(path_address, output_address, size);
}

std::string BionicFileSystem::TraceGuestPath(std::uint64_t address) const {
    if (address_space_ == nullptr) return "invalid";
    constexpr std::uint64_t kTraceMaximum = 256;
    std::uint64_t length = 0;
    std::string ignored;
    if (!address_space_->BoundedStringLength(address, kTraceMaximum, &length,
                                              &ignored))
        return "invalid";
    std::string path(static_cast<std::size_t>(length), '\0');
    if (!path.empty() &&
        !address_space_->Read(address, path.data(), path.size()))
        return "invalid";
    for (char& character : path) {
        const unsigned char value = static_cast<unsigned char>(character);
        if (value < 0x20 || value >= 0x7f) character = '?';
    }
    return path;
}

std::size_t BionicFileSystem::open_file_count() const {
    const std::lock_guard lock(files_mutex_);
    // Borrowed descriptors (guest stdio mapped onto host handles) are
    // infrastructure, not guest-opened files — exclude them so this stays a
    // leak detector.
    std::size_t count = 0;
    for (const auto& entry : files_)
        if (!entry.second->borrowed) ++count;
    return count;
}

std::size_t BionicFileSystem::open_directory_count() const {
    const std::lock_guard lock(directories_mutex_);
    return directories_.size();
}

bool RegisterBionicFileImports(Aarch64ImportRegistry* imports,
                               BionicFileSystem* files,
                               std::string* error) {
    if (imports == nullptr || files == nullptr) {
        SetError(error, "bionic file import dependencies are null");
        return false;
    }
    const auto register_deny = [&](const char* name) {
        return imports->Register(
            name,
            [files](Aarch64ImportRegistry::Registers* registers,
                    std::string*) {
                (*registers)[0] = static_cast<std::uint64_t>(
                    files->Deny(kOperationNotPermitted));
                return true;
            },
            error);
    };
    const auto register_open = [&](const char* name) {
        return imports->Register(
             name,
             [files](Aarch64ImportRegistry::Registers* registers,
                     std::string* dispatch_error) {
                 const std::string path =
                     files->TraceGuestPath((*registers)[0]);
                 const std::uint64_t flags = (*registers)[1];
                 (*registers)[0] = static_cast<std::uint64_t>(files->Open(
                     (*registers)[0], (*registers)[1], (*registers)[2]));
                 if (dispatch_error != nullptr)
                     *dispatch_error =
                         std::string("trace:file-open-") +
                         ((*registers)[0] == UINT64_MAX ? "fail-path-"
                                                       : "ok-path-") +
                         path + "-flags" + std::to_string(flags);
                 return true;
             },
            error);
    };
    // The socket family stays offline in the native route: DNS/analytics
    // callers get a clean EPERM instead of a thread-killing unimplemented
    // dispatch, and no payload traffic can egress.
    if (!register_deny("socket") || !register_deny("socketpair") ||
        !register_deny("bind") || !register_deny("connect") ||
        !register_deny("listen") || !register_deny("accept") ||
        !register_deny("accept4") || !register_deny("send") ||
        !register_deny("sendto") || !register_deny("sendmsg") ||
        !register_deny("sendmmsg") || !register_deny("recv") ||
        !register_deny("recvfrom") || !register_deny("recvmsg") ||
        !register_deny("recvmmsg") || !register_deny("shutdown") ||
        !register_deny("getsockname") || !register_deny("getpeername") ||
        !register_deny("setsockopt") || !register_deny("getsockopt") ||
        !register_open("open") || !register_open("open64") ||
        !imports->Register(
             "__open_2",
             [files](Aarch64ImportRegistry::Registers* registers,
                     std::string* dispatch_error) {
                 const std::string path =
                     files->TraceGuestPath((*registers)[0]);
                 const std::uint64_t flags = (*registers)[1];
                 (*registers)[0] = static_cast<std::uint64_t>(files->Open(
                     (*registers)[0], (*registers)[1], 0));
                 if (dispatch_error != nullptr)
                     *dispatch_error =
                         std::string("trace:file-open2-") +
                         ((*registers)[0] == UINT64_MAX ? "fail-path-"
                                                       : "ok-path-") +
                         path + "-flags" + std::to_string(flags);
                 return true;
             },
            error) ||
        !imports->Register(
            "mkdir",
            [files](Aarch64ImportRegistry::Registers* registers,
                    std::string*) {
                (*registers)[0] = static_cast<std::uint64_t>(
                    files->MakeDirectory((*registers)[0],
                                         (*registers)[1]));
                return true;
             },
             error) ||
        !imports->Register(
            "remove",
            [files](Aarch64ImportRegistry::Registers* registers,
                    std::string*) {
                (*registers)[0] = static_cast<std::uint64_t>(
                    files->Remove((*registers)[0]));
                return true;
            },
            error) ||
        !imports->Register(
            "unlink",
            [files](Aarch64ImportRegistry::Registers* registers,
                    std::string*) {
                (*registers)[0] = static_cast<std::uint64_t>(
                    files->Unlink((*registers)[0]));
                return true;
            },
            error) ||
        !imports->Register(
            "rmdir",
            [files](Aarch64ImportRegistry::Registers* registers,
                    std::string*) {
                // Directory removal in the compat runtime: the payload's
                // cache/temp cleanup paths call rmdir on directories the
                // virtual filesystem never owned; report ENOENT (the
                // Remove path already covers real mounted entries).
                (*registers)[0] = static_cast<std::uint64_t>(
                    files->Remove((*registers)[0]));
                return true;
            },
            error) ||
        !imports->Register(
            "pipe",
            [files](Aarch64ImportRegistry::Registers* registers,
                    std::string*) {
                (*registers)[0] = static_cast<std::uint64_t>(
                    files->Pipe((*registers)[0]));
                return true;
            },
            error) ||
        !imports->Register(
            "rename",
            [files](Aarch64ImportRegistry::Registers* registers,
                    std::string*) {
                (*registers)[0] = static_cast<std::uint64_t>(
                    files->Rename((*registers)[0], (*registers)[1]));
                return true;
            },
            error) ||
        !imports->Register(
            "link",
            [files](Aarch64ImportRegistry::Registers* registers,
                    std::string*) {
                (*registers)[0] = static_cast<std::uint64_t>(
                    files->Link((*registers)[0], (*registers)[1]));
                return true;
            },
            error) ||
        !imports->Register(
            "symlink",
            [files](Aarch64ImportRegistry::Registers* registers,
                    std::string*) {
                (*registers)[0] = static_cast<std::uint64_t>(
                    files->Symlink((*registers)[0], (*registers)[1]));
                return true;
            },
            error) ||
        !imports->Register(
            "chmod",
            [files](Aarch64ImportRegistry::Registers* registers,
                    std::string*) {
                (*registers)[0] = static_cast<std::uint64_t>(
                    files->Chmod((*registers)[0], (*registers)[1]));
                return true;
            },
            error) ||
        !imports->Register(
            "fchmod",
            [files](Aarch64ImportRegistry::Registers* registers,
                    std::string*) {
                (*registers)[0] = static_cast<std::uint64_t>(files->Fchmod(
                    static_cast<int>((*registers)[0]), (*registers)[1]));
                return true;
            },
            error) ||
        !imports->Register(
            "fchmodat",
            [files](Aarch64ImportRegistry::Registers* registers,
                    std::string*) {
                (*registers)[0] = static_cast<std::uint64_t>(files->Fchmodat(
                    static_cast<int>((*registers)[0]), (*registers)[1],
                    (*registers)[2], (*registers)[3]));
                return true;
            },
            error) ||
        !imports->Register(
            "fchmodat2",
            [files](Aarch64ImportRegistry::Registers* registers,
                    std::string*) {
                (*registers)[0] = static_cast<std::uint64_t>(files->Fchmodat(
                    static_cast<int>((*registers)[0]), (*registers)[1],
                    (*registers)[2], (*registers)[3]));
                return true;
            },
            error) ||
        !imports->Register(
            "futimens",
            [files](Aarch64ImportRegistry::Registers* registers,
                    std::string*) {
                (*registers)[0] = static_cast<std::uint64_t>(
                    files->Futimens(static_cast<int>((*registers)[0]),
                                    (*registers)[1]));
                return true;
            },
            error) ||
        !imports->Register(
            "utimensat",
            [files](Aarch64ImportRegistry::Registers* registers,
                    std::string*) {
                (*registers)[0] = static_cast<std::uint64_t>(
                    files->Utimensat(static_cast<int>((*registers)[0]),
                                     (*registers)[1], (*registers)[2],
                                     (*registers)[3]));
                return true;
            },
            error) ||
        !imports->Register(
            "getcwd",
            [files](Aarch64ImportRegistry::Registers* registers,
                    std::string*) {
                (*registers)[0] = files->GetCurrentDirectory(
                    (*registers)[0], (*registers)[1]);
                return true;
            },
            error) ||
        !imports->Register(
            "read",
            [files](Aarch64ImportRegistry::Registers* registers,
                    std::string*) {
                (*registers)[0] = static_cast<std::uint64_t>(files->Read(
                    static_cast<int>((*registers)[0]), (*registers)[1],
                    (*registers)[2]));
                return true;
            },
            error) ||
        !imports->Register(
            "__read_chk",
            [files](Aarch64ImportRegistry::Registers* registers,
                    std::string*) {
                // Fortified read: __read_chk(fd, buf, count, bufsize).
                // Same semantics as read(); the bufsize check is advisory.
                (*registers)[0] = static_cast<std::uint64_t>(files->Read(
                    static_cast<int>((*registers)[0]), (*registers)[1],
                    (*registers)[2]));
                return true;
            },
            error) ||
        !imports->Register(
            "__read",
            [files](Aarch64ImportRegistry::Registers* registers,
                    std::string*) {
                (*registers)[0] = static_cast<std::uint64_t>(files->Read(
                    static_cast<int>((*registers)[0]), (*registers)[1],
                    (*registers)[2]));
                return true;
            },
            error) ||
        !imports->Register(
            "write",
            [files](Aarch64ImportRegistry::Registers* registers,
                    std::string*) {
                (*registers)[0] = static_cast<std::uint64_t>(files->Write(
                    static_cast<int>((*registers)[0]), (*registers)[1],
                    (*registers)[2]));
                return true;
            },
            error) ||
        !imports->Register(
            "pread",
            [files](Aarch64ImportRegistry::Registers* registers,
                    std::string*) {
                const int fd_ = static_cast<int>((*registers)[0]);
                const std::int64_t r_ = files->Pread64(fd_, (*registers)[1], (*registers)[2],
                    static_cast<std::int64_t>((*registers)[3]));
                files->TraceFdOp("pread", fd_, "n="+std::to_string((*registers)[2])+" off="+std::to_string((long long)(*registers)[3]), r_);
                (*registers)[0] = static_cast<std::uint64_t>(r_);
                return true;
            },
            error) ||
        !imports->Register(
            "pread64",
            [files](Aarch64ImportRegistry::Registers* registers,
                    std::string*) {
                const int fd_ = static_cast<int>((*registers)[0]);
                const std::int64_t r_ = files->Pread64(fd_, (*registers)[1], (*registers)[2],
                    static_cast<std::int64_t>((*registers)[3]));
                files->TraceFdOp("pread64", fd_, "n="+std::to_string((*registers)[2])+" off="+std::to_string((long long)(*registers)[3]), r_);
                (*registers)[0] = static_cast<std::uint64_t>(r_);
                return true;
            },
            error) ||
        !imports->Register(
            "pwrite",
            [files](Aarch64ImportRegistry::Registers* registers,
                    std::string*) {
                const int fd_ = static_cast<int>((*registers)[0]);
                const std::int64_t r_ = files->Pwrite64(fd_, (*registers)[1], (*registers)[2],
                    static_cast<std::int64_t>((*registers)[3]));
                files->TraceFdOp("pwrite", fd_, "n="+std::to_string((*registers)[2])+" off="+std::to_string((long long)(*registers)[3]), r_);
                (*registers)[0] = static_cast<std::uint64_t>(r_);
                return true;
            },
            error) ||
        !imports->Register(
            "ftruncate",
            [files](Aarch64ImportRegistry::Registers* registers,
                    std::string*) {
                const int fd_ = static_cast<int>((*registers)[0]);
                const std::int64_t r_ = files->Truncate(fd_, static_cast<std::int64_t>((*registers)[1]));
                files->TraceFdOp("ftruncate", fd_, "size="+std::to_string((long long)(*registers)[1]), r_);
                (*registers)[0] = static_cast<std::uint64_t>(r_);
                return true;
            },
            error) ||
        !imports->Register(
            "fsync",
            [files](Aarch64ImportRegistry::Registers* registers,
                    std::string*) {
                const int fd_ = static_cast<int>((*registers)[0]);
                const std::int64_t r_ = files->Sync(fd_);
                files->TraceFdOp("fsync", fd_, "", r_);
                (*registers)[0] = static_cast<std::uint64_t>(r_);
                return true;
            },
            error) ||
        !imports->Register(
            "readlink",
            [files](Aarch64ImportRegistry::Registers* registers,
                    std::string*) {
                (*registers)[0] = static_cast<std::uint64_t>(files->ReadLink(
                    (*registers)[0], (*registers)[1], (*registers)[2]));
                return true;
            },
            error) ||
        !imports->Register(
            "readlinkat",
            [files](Aarch64ImportRegistry::Registers* registers,
                    std::string*) {
                (*registers)[0] = static_cast<std::uint64_t>(files->ReadLinkAt(
                    static_cast<int>((*registers)[0]), (*registers)[1],
                    (*registers)[2], (*registers)[3]));
                return true;
            },
            error) ||
        !imports->Register(
            "lseek",
            [files](Aarch64ImportRegistry::Registers* registers,
                    std::string*) {
                (*registers)[0] = static_cast<std::uint64_t>(files->Lseek64(
                    static_cast<int>((*registers)[0]),
                    static_cast<std::int64_t>((*registers)[1]),
                    static_cast<int>((*registers)[2])));
                return true;
            },
            error) ||
        !imports->Register(
            "lseek64",
            [files](Aarch64ImportRegistry::Registers* registers,
                    std::string*) {
                (*registers)[0] = static_cast<std::uint64_t>(files->Lseek64(
                    static_cast<int>((*registers)[0]),
                    static_cast<std::int64_t>((*registers)[1]),
                    static_cast<int>((*registers)[2])));
                return true;
            },
            error) ||
        !imports->Register(
            "fcntl",
            [files](Aarch64ImportRegistry::Registers* registers,
                    std::string*) {
                const int descriptor = static_cast<int>((*registers)[0]);
                const int command = static_cast<int>((*registers)[1]);
                const std::uint64_t argument = (*registers)[2];
                const int result =
                    files->FileControl(descriptor, command, argument);
                files->TraceFdOp("fcntl", descriptor,
                                 "cmd=" + std::to_string(command), result);
                (*registers)[0] = static_cast<std::uint64_t>(result);
                return true;
             },
             error) ||
        !imports->Register(
            "fcntl64",
            [files](Aarch64ImportRegistry::Registers* registers,
                    std::string*) {
                const int descriptor = static_cast<int>((*registers)[0]);
                const int command = static_cast<int>((*registers)[1]);
                const std::uint64_t argument = (*registers)[2];
                const int result =
                    files->FileControl(descriptor, command, argument);
                files->TraceFdOp("fcntl64", descriptor,
                                 "cmd=" + std::to_string(command), result);
                (*registers)[0] = static_cast<std::uint64_t>(result);
                return true;
             },
            error) ||
        !imports->Register(
            "opendir",
            [files](Aarch64ImportRegistry::Registers* registers,
                    std::string* dispatch_error) {
                const std::string path =
                    files->TraceGuestPath((*registers)[0]);
                (*registers)[0] =
                    files->OpenDirectory((*registers)[0]);
                if (dispatch_error != nullptr)
                    *dispatch_error =
                        std::string("trace:directory-open-") +
                        ((*registers)[0] == 0 ? "fail-path-"
                                              : "ok-path-") +
                        path;
                return true;
            },
            error) ||
        !imports->Register(
            "readdir",
            [files](Aarch64ImportRegistry::Registers* registers,
                    std::string*) {
                (*registers)[0] =
                    files->ReadDirectory((*registers)[0]);
                return true;
            },
            error) ||
        !imports->Register(
            "closedir",
            [files](Aarch64ImportRegistry::Registers* registers,
                    std::string*) {
                (*registers)[0] = static_cast<std::uint64_t>(
                    files->CloseDirectory((*registers)[0]));
                return true;
            },
            error) ||
        !imports->Register(
            "close",
            [files](Aarch64ImportRegistry::Registers* registers,
                    std::string*) {
                const int fd_ = static_cast<int>((*registers)[0]);
                const int r_ = files->Close(fd_);
                files->TraceFdOp("close", fd_, "", r_);
                (*registers)[0] = static_cast<std::uint64_t>(r_);
                return true;
            },
            error) ||
        !imports->Register(
            "dup",
            [files](Aarch64ImportRegistry::Registers* registers,
                    std::string*) {
                (*registers)[0] = static_cast<std::uint64_t>(
                    files->Dup(static_cast<int>((*registers)[0])));
                return true;
            },
            error) ||
        !imports->Register(
            "dup2",
            [files](Aarch64ImportRegistry::Registers* registers,
                    std::string*) {
                (*registers)[0] = static_cast<std::uint64_t>(files->Dup2(
                    static_cast<int>((*registers)[0]),
                    static_cast<int>((*registers)[1])));
                return true;
            },
            error) ||
        !imports->Register(
            "dup3",
            [files](Aarch64ImportRegistry::Registers* registers,
                    std::string*) {
                (*registers)[0] = static_cast<std::uint64_t>(files->Dup3(
                    static_cast<int>((*registers)[0]),
                    static_cast<int>((*registers)[1]), (*registers)[2]));
                return true;
            },
            error))
        return false;
    if (!imports->Register(
            "access",
            [files](Aarch64ImportRegistry::Registers* registers,
                    std::string*) {
                (*registers)[0] = static_cast<std::uint64_t>(files->Access(
                    (*registers)[0], static_cast<int>((*registers)[1])));
                return true;
            },
            error))
        return false;
    const auto register_stat = [&](const char* name) {
        return imports->Register(
            name,
            [files](Aarch64ImportRegistry::Registers* registers,
                    std::string* dispatch_error) {
                (*registers)[0] = static_cast<std::uint64_t>(files->Stat(
                    (*registers)[0], (*registers)[1]));
                if (dispatch_error != nullptr)
                    *dispatch_error = (*registers)[0] == 0
                                          ? "trace:file-stat-hit"
                                          : "trace:file-stat-miss";
                return true;
            },
            error);
    };
    const auto register_lstat = [&](const char* name) {
        return imports->Register(
            name,
            [files](Aarch64ImportRegistry::Registers* registers,
                    std::string* dispatch_error) {
                const std::string path =
                    files->TraceGuestPath((*registers)[0]);
                (*registers)[0] = static_cast<std::uint64_t>(files->Lstat(
                    (*registers)[0], (*registers)[1]));
                if (dispatch_error != nullptr)
                    *dispatch_error =
                        std::string("trace:file-lstat-") +
                        ((*registers)[0] == 0 ? "hit-path-" : "miss-path-") +
                        path;
                return true;
            },
            error);
    };
    const auto register_fstat = [&](const char* name) {
        return imports->Register(
            name,
            [files, name](Aarch64ImportRegistry::Registers* registers,
                    std::string*) {
                const int fd_ = static_cast<int>((*registers)[0]);
                const int r_ = files->Fstat(fd_, (*registers)[1]);
                files->TraceFdOp(name, fd_, "", r_);
                (*registers)[0] = static_cast<std::uint64_t>(r_);
                return true;
            },
            error);
    };
    const auto register_statfs = [&](const char* name) {
        return imports->Register(
            name,
            [files](Aarch64ImportRegistry::Registers* registers,
                    std::string* dispatch_error) {
                const std::string path =
                    files->TraceGuestPath((*registers)[0]);
                (*registers)[0] = static_cast<std::uint64_t>(
                    files->StatFileSystem((*registers)[0],
                                          (*registers)[1]));
                if (dispatch_error != nullptr)
                    *dispatch_error =
                        std::string("trace:file-statfs-") +
                        ((*registers)[0] == 0 ? "hit-path-" :
                                                "miss-path-") +
                        path;
                return true;
            },
            error);
    };
    const auto register_statvfs = [&](const char* name) {
        return imports->Register(
            name,
            [files](Aarch64ImportRegistry::Registers* registers,
                    std::string* dispatch_error) {
                const std::string path =
                    files->TraceGuestPath((*registers)[0]);
                (*registers)[0] = static_cast<std::uint64_t>(
                    files->StatVfs((*registers)[0], (*registers)[1]));
                if (dispatch_error != nullptr)
                    *dispatch_error =
                        std::string("trace:file-statvfs-") +
                        ((*registers)[0] == 0 ? "hit-path-" :
                                                "miss-path-") +
                        path;
                return true;
            },
            error);
    };
    const auto register_fstatvfs = [&](const char* name) {
        return imports->Register(
            name,
            [files](Aarch64ImportRegistry::Registers* registers,
                    std::string* dispatch_error) {
                (*registers)[0] = static_cast<std::uint64_t>(
                    files->FstatVfs(static_cast<int>((*registers)[0]),
                                    (*registers)[1]));
                if (dispatch_error != nullptr)
                    *dispatch_error =
                        std::string("trace:file-fstatvfs-") +
                        ((*registers)[0] == 0 ? "hit" : "miss");
                return true;
            },
            error);
    };
    return register_stat("stat") && register_stat("stat64") &&
           register_lstat("lstat") && register_lstat("lstat64") &&
           register_fstat("fstat") && register_fstat("fstat64") &&
           register_statfs("statfs") && register_statfs("statfs64") &&
           register_statvfs("statvfs") && register_statvfs("statvfs64") &&
           register_fstatvfs("fstatvfs") && register_fstatvfs("fstatvfs64");
}

}  // namespace isomr
