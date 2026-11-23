#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace isomr {

class Aarch64ImportRegistry;
class BionicThreadState;
class GuestAddressSpace;
class GuestHeap;

struct BionicFileMount {
    std::string guest_prefix;
    std::wstring host_root;
    bool writable = false;
};

struct BionicStat64 {
    std::uint64_t device = 0;
    std::uint64_t inode = 0;
    std::uint32_t mode = 0;
    std::uint32_t link_count = 0;
    std::uint32_t user_id = 0;
    std::uint32_t group_id = 0;
    std::uint64_t special_device = 0;
    std::uint64_t padding1 = 0;
    std::int64_t size = 0;
    std::int32_t block_size = 0;
    std::int32_t padding2 = 0;
    std::int64_t block_count = 0;
    std::int64_t access_seconds = 0;
    std::uint64_t access_nanoseconds = 0;
    std::int64_t modification_seconds = 0;
    std::uint64_t modification_nanoseconds = 0;
    std::int64_t change_seconds = 0;
    std::uint64_t change_nanoseconds = 0;
    std::uint32_t unused4 = 0;
    std::uint32_t unused5 = 0;
};

static_assert(sizeof(BionicStat64) == 128);
static_assert(offsetof(BionicStat64, mode) == 16);
static_assert(offsetof(BionicStat64, size) == 48);
static_assert(offsetof(BionicStat64, block_count) == 64);
static_assert(offsetof(BionicStat64, modification_seconds) == 88);
static_assert(offsetof(BionicStat64, unused4) == 120);

struct BionicStatFs64 {
    std::uint64_t file_system_type = 0;
    std::uint64_t block_size = 0;
    std::uint64_t block_count = 0;
    std::uint64_t free_block_count = 0;
    std::uint64_t available_block_count = 0;
    std::uint64_t file_count = 0;
    std::uint64_t free_file_count = 0;
    std::int32_t file_system_id[2]{};
    std::uint64_t maximum_name_length = 0;
    std::uint64_t fragment_size = 0;
    std::uint64_t flags = 0;
    std::uint64_t spare[4]{};
};

static_assert(sizeof(BionicStatFs64) == 120);
static_assert(offsetof(BionicStatFs64, block_count) == 16);
static_assert(offsetof(BionicStatFs64, file_system_id) == 56);
static_assert(offsetof(BionicStatFs64, maximum_name_length) == 64);
static_assert(offsetof(BionicStatFs64, flags) == 80);
static_assert(offsetof(BionicStatFs64, spare) == 88);

// Bionic `struct statvfs` on AArch64. Unity's resource installer resolves
// `statvfs` via dlsym to check free space; returning -1 there makes it abort
// with "Not enough storage space to install required resources."
struct BionicStatVfs {
    std::uint64_t block_size = 0;        // f_bsize
    std::uint64_t fragment_size = 0;     // f_frsize
    std::uint64_t block_count = 0;       // f_blocks
    std::uint64_t free_block_count = 0;  // f_bfree
    std::uint64_t avail_block_count = 0; // f_bavail
    std::uint64_t file_count = 0;        // f_files
    std::uint64_t free_file_count = 0;   // f_ffree
    std::uint64_t avail_file_count = 0;  // f_favail
    std::uint64_t file_system_id = 0;    // f_fsid
    std::uint64_t flags = 0;             // f_flag (ST_RDONLY/ST_NOSUID/...)
    std::uint64_t maximum_name_length = 0;  // f_namemax
    std::int32_t spare[6]{};             // __f_spare (int[6])
};

static_assert(sizeof(BionicStatVfs) == 112);
static_assert(offsetof(BionicStatVfs, block_count) == 16);
static_assert(offsetof(BionicStatVfs, avail_block_count) == 32);
static_assert(offsetof(BionicStatVfs, file_system_id) == 64);
static_assert(offsetof(BionicStatVfs, flags) == 72);
static_assert(offsetof(BionicStatVfs, maximum_name_length) == 80);
static_assert(offsetof(BionicStatVfs, spare) == 88);

struct BionicFlock {
    std::int16_t type = 0;
    std::int16_t origin = 0;
    std::uint32_t padding = 0;
    std::int64_t start = 0;
    std::int64_t length = 0;
    std::int32_t process_id = 0;
    std::uint32_t padding2 = 0;
};

static_assert(sizeof(BionicFlock) == 32);
static_assert(offsetof(BionicFlock, type) == 0);
static_assert(offsetof(BionicFlock, origin) == 2);
static_assert(offsetof(BionicFlock, start) == 8);
static_assert(offsetof(BionicFlock, length) == 16);
static_assert(offsetof(BionicFlock, process_id) == 24);

struct BionicDirent64 {
    std::uint64_t inode = 0;
    std::int64_t offset = 0;
    std::uint16_t record_length = 0;
    std::uint8_t type = 0;
    char name[256]{};
};

static_assert(sizeof(BionicDirent64) == 280);
static_assert(offsetof(BionicDirent64, inode) == 0);
static_assert(offsetof(BionicDirent64, offset) == 8);
static_assert(offsetof(BionicDirent64, record_length) == 16);
static_assert(offsetof(BionicDirent64, type) == 18);
static_assert(offsetof(BionicDirent64, name) == 19);

class BionicFileSystem {
public:
    BionicFileSystem(GuestHeap* heap,
                     GuestAddressSpace* address_space,
                     BionicThreadState* thread_state,
                     std::vector<BionicFileMount> mounts,
                     std::string current_directory = {},
                     std::size_t maximum_transfer = 16U * 1024U * 1024U,
                     std::size_t maximum_path = 32767);
    ~BionicFileSystem();

    BionicFileSystem(const BionicFileSystem&) = delete;
    BionicFileSystem& operator=(const BionicFileSystem&) = delete;

    class FileReference {
    public:
        FileReference() = default;

        explicit operator bool() const { return record_ != nullptr; }
        bool operator==(const FileReference&) const = default;

    private:
        friend class BionicFileSystem;
        explicit FileReference(std::shared_ptr<void> record)
            : record_(std::move(record)) {}

        std::shared_ptr<void> record_;
    };

    struct ProcessExitLockCleanup {
        std::size_t records = 0;
        std::size_t locks = 0;
    };

    FileReference RetainFile(int descriptor) const;
    // Completed content-traced guest reads: milestone #1 is catalog.json,
    // #2 onward are asset bundles. The game script watches this to arm the
    // lost-wake poll cap exactly once the boot catalog phase is over,
    // instead of guessing from drain-tick counts (which vary 30x by run).
    std::uint64_t ServedContentCount() const {
        return served_content_count_.load(std::memory_order_relaxed);
    }
    bool IsWritable(const FileReference& file) const;
    bool ReleaseLocksForProcessExit(
        const std::vector<FileReference>& retained_files,
        ProcessExitLockCleanup* cleanup, std::string* error);
    int Open(std::uint64_t path_address, std::uint64_t flags,
             std::uint64_t mode);
    int Close(int descriptor);
    int Dup(int descriptor);
    int Dup2(int descriptor, int target);
    int Dup3(int descriptor, int target, std::uint64_t flags);
    std::int64_t Read(int descriptor, std::uint64_t output_address,
                      std::uint64_t size);
    std::int64_t Pread64(int descriptor, std::uint64_t output_address,
                         std::uint64_t size, std::int64_t offset);
    std::int64_t Pread64(const FileReference& file,
                         std::uint64_t output_address,
                         std::uint64_t size, std::int64_t offset);
    std::int64_t Write(int descriptor, std::uint64_t input_address,
                       std::uint64_t size);
    std::int64_t Pwrite64(int descriptor, std::uint64_t input_address,
                          std::uint64_t size, std::int64_t offset);
    std::int64_t Pwrite64(const FileReference& file,
                          std::uint64_t input_address,
                          std::uint64_t size, std::int64_t offset);
    std::int64_t Pwrite64(const FileReference& file,
                          std::uint64_t input_address,
                          std::uint64_t size, std::int64_t offset,
                          std::string* failure_stage);
    int Truncate(int descriptor, std::int64_t size);
    int Sync(int descriptor);
    std::int64_t Lseek64(int descriptor, std::int64_t offset, int origin);
    int FileControl(int descriptor, int command,
                    std::uint64_t argument_address);
    int Access(std::uint64_t path_address, int mode);
    int Stat(std::uint64_t path_address, std::uint64_t output_address);
    int Lstat(std::uint64_t path_address, std::uint64_t output_address);
    int Fstat(int descriptor, std::uint64_t output_address);
    int StatFileSystem(std::uint64_t path_address,
                       std::uint64_t output_address);
    int StatVfs(std::uint64_t path_address, std::uint64_t output_address);
    int FstatVfs(int descriptor, std::uint64_t output_address);
    int MakeDirectory(std::uint64_t path_address, std::uint64_t mode);
    int Chmod(std::uint64_t path_address, std::uint64_t mode);
    int Fchmod(int descriptor, std::uint64_t mode);
    int Fchmodat(int dir_fd, std::uint64_t path_address, std::uint64_t mode,
                 std::uint64_t flags);
    int Futimens(int descriptor, std::uint64_t times_address);
    int Utimensat(int dir_fd, std::uint64_t path_address,
                  std::uint64_t times_address, std::uint64_t flags);
    int Remove(std::uint64_t path_address);
    int Unlink(std::uint64_t path_address);    int Pipe(std::uint64_t output_address);
    int Rename(std::uint64_t old_address, std::uint64_t new_address);
    int Link(std::uint64_t old_address, std::uint64_t new_address);
    int Symlink(std::uint64_t target_address, std::uint64_t link_address);
    std::uint64_t OpenDirectory(std::uint64_t path_address);
    std::uint64_t ReadDirectory(std::uint64_t directory);
    int CloseDirectory(std::uint64_t directory);
    std::uint64_t GetCurrentDirectory(std::uint64_t output_address,
                                      std::uint64_t size);
    std::int64_t ReadLink(std::uint64_t path_address,
                          std::uint64_t output_address, std::uint64_t size);
    std::int64_t ReadLinkAt(int dir_fd, std::uint64_t path_address,
                            std::uint64_t output_address, std::uint64_t size);
    std::string TraceGuestPath(std::uint64_t address) const;
    // Sets guest errno and returns -1; used by syscall stubs the runtime
    // deliberately does not serve (the socket family, which must stay
    // offline in the native route).
    int Deny(int error);

    // Bounded diagnostic: resolves a descriptor to the guest path it was
    // opened with and prints an fd-scoped op result, so sqlite's
    // read/write/lock sequence surfaces the exact failing call.
    void TraceFdOp(const char* operation, int descriptor,
                   const std::string& detail, std::int64_t result);

    std::size_t mount_count() const { return mounts_.size(); }
    bool configuration_valid() const { return configuration_valid_; }
    std::size_t open_file_count() const;
    std::size_t open_directory_count() const;

private:
    struct Mount {
        std::string guest_prefix;
        std::wstring host_root;
        std::wstring final_root;
        bool writable = false;
    };

    struct FileRecord {
        enum class Kind {
            HostFile,
            SystemRandom,
            VirtualText,
        };

        struct LockRecord {
            std::uint64_t start = 0;
            std::uint64_t length = 0;
            std::int16_t type = 0;
        };

        ~FileRecord();
        Kind kind = Kind::HostFile;
        void* handle = nullptr;
        bool readable = false;
        bool writable = false;
        bool append = false;
        // The descriptor does not own the host handle (stdin/stdout/stderr
        // borrow GetStdHandle results) so the destructor must not close it.
        bool borrowed = false;
        bool obb_diagnostic = false;
        // Bounds the OBB read trace so a streaming expansion file does not
        // emit a line per read for the whole session.
        unsigned obb_read_logged = 0;
        // Bounded diagnostic: for content-critical payloads (catalog.json,
        // *.bundle) accumulate a hash over the exact bytes served so a
        // corrupt read path is distinguishable from a managed-decode bug.
        bool content_trace = false;
        std::string content_path;
        // Bounded diagnostic: the guest path the descriptor was opened with,
        // so fd-scoped op tracing can name the file without a second lookup.
        std::string trace_path;
        std::uint64_t served_bytes = 0;
        std::uint64_t served_hash = 1469598103934665603ULL;
        std::vector<LockRecord> locks;
        std::mutex mutex;
        // VirtualText records serve read-only pseudo-file content (for
        // example /proc/cpuinfo) from memory rather than a host handle.
        std::string content;
        std::size_t read_position = 0;
    };

    struct DirectoryEntry {
        std::uint64_t inode = 0;
        std::uint8_t type = 0;
        std::string name;
    };

    struct DirectoryRecord {
        ~DirectoryRecord();
        void* handle = nullptr;
        std::uint64_t handle_address = 0;
        std::uint64_t entry_address = 0;
        std::vector<DirectoryEntry> entries;
        std::size_t next_entry = 0;
        std::mutex mutex;
    };

    bool ReadGuestPath(std::uint64_t address, std::string* path);
    bool ResolvePath(std::uint64_t address, std::wstring* host_path,
                     std::wstring* final_root,
                     bool* writable = nullptr,
                     bool* mount_root = nullptr);
    bool ValidateOpenedPath(void* handle, const std::wstring& final_root);
    bool ValidateOutput(std::uint64_t address, std::size_t size);
    bool ReadStat(void* handle, BionicStat64* output);
    int WriteStatValue(const BionicStat64& value,
                       std::uint64_t output_address);
    int WriteStat(void* handle, std::uint64_t output_address);
    int WriteSystemRandomStat(std::uint64_t output_address);
    int WriteVirtualTextStat(const FileRecord& file,
                             std::uint64_t output_address);
    // Returns in-memory content for a read-only pseudo file when the guest
    // path names one (for example /proc/cpuinfo or a /sys cpu node), or
    // std::nullopt when the path is not virtualized.
    static std::optional<std::string> VirtualFileContent(
        const std::string& guest_path);
    int InsertFile(std::shared_ptr<FileRecord> file);
    std::shared_ptr<FileRecord> Resolve(
        const FileReference& reference) const;
    std::shared_ptr<FileRecord> Find(int descriptor) const;
    std::int64_t ReadLocked(const std::shared_ptr<FileRecord>& file,
                            std::uint64_t output_address, std::size_t size);
    std::int64_t WriteLocked(const std::shared_ptr<FileRecord>& file,
                             std::uint64_t input_address, std::size_t size,
                             std::string* failure_stage = nullptr);
    int RemovePath(std::uint64_t path_address, bool allow_directory);
    void SetErrno(std::int32_t value);
    // Shared Win32 volume query backing both `statfs`/`statvfs`. Fills the
    // block counts, name limit, fs id and read-only flag from the volume that
    // owns `opened_final_path`.
    struct VolumeSpace {
        std::uint64_t block_size = 0;
        std::uint64_t block_count = 0;
        std::uint64_t free_block_count = 0;
        std::uint64_t available_block_count = 0;
        std::uint64_t file_count = 0;
        std::uint64_t free_file_count = 0;
        std::int32_t file_system_id = 0;
        std::uint64_t maximum_name_length = 0;
        std::uint32_t file_system_type = 0;
        bool read_only = false;
    };
    bool QueryVolumeSpace(const std::wstring& opened_final_path,
                          bool mount_writable, VolumeSpace* output);
    // Bounded diagnostic: prints the guest path and resolved host path when a
    // path-resolved filesystem op fails to find the backing file, so asset
    // and il2cpp path schemes surface their real mapping targets.
    void LogFileMiss(const char* operation, const std::string& guest_path,
                     const std::wstring& host_path);
    // Bounded diagnostic: prints a filesystem op's guest path so the resource
    // install/scan sequences surface their real targets.
    void LogFileOp(const char* operation, const std::string& guest_path);

    GuestHeap* heap_ = nullptr;
    GuestAddressSpace* address_space_ = nullptr;
    BionicThreadState* thread_state_ = nullptr;
    std::size_t maximum_transfer_ = 0;
    std::size_t maximum_path_ = 0;
    std::vector<Mount> mounts_;
    std::string current_directory_;
    bool configuration_valid_ = true;
    mutable std::mutex files_mutex_;
    std::unordered_map<int, std::shared_ptr<FileRecord>> files_;
    int next_descriptor_ = 3;
    std::atomic<std::uint64_t> served_content_count_{0};
    mutable std::mutex directories_mutex_;
    std::unordered_map<std::uint64_t, std::shared_ptr<DirectoryRecord>>
        directories_;
};

bool RegisterBionicFileImports(Aarch64ImportRegistry* imports,
                               BionicFileSystem* files,
                               std::string* error);

}  // namespace isomr
