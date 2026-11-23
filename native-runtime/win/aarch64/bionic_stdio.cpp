#include "bionic_stdio.h"

#include "bionic_files.h"
#include "bionic_format.h"
#include "bionic_thread_state.h"
#include "guest_address_space.h"
#include "guest_heap.h"
#include "import_registry.h"

#include <limits>
#include <utility>
#include <vector>

namespace isomr {
namespace {

constexpr std::uint64_t kBionicFileSize = 152;
constexpr std::int32_t kBadDescriptor = 9;
constexpr std::int32_t kOutOfMemory = 12;
constexpr std::int32_t kBadAddress = 14;
constexpr std::int32_t kInvalidArgument = 22;
constexpr std::int32_t kOverflow = 75;

void SetError(std::string* error, std::string message) {
    if (error != nullptr) *error = std::move(message);
}

}  // namespace

void BionicStdio::SetErrno(std::int32_t value) {
    if (threads_ == nullptr) return;
    std::string ignored;
    (void)threads_->SetCurrentErrno(value, &ignored);
}

bool BionicStdio::ReadMode(std::uint64_t address, std::string* mode) {
    if (mode == nullptr || memory_ == nullptr) {
        SetErrno(kBadAddress);
        return false;
    }
    std::uint64_t length = 0;
    std::string ignored;
    if (!memory_->BoundedStringLength(address, 16, &length, &ignored) ||
        length == 0 || length == 16) {
        SetErrno(kInvalidArgument);
        return false;
    }
    mode->resize(static_cast<std::size_t>(length));
    if (!memory_->Read(address, mode->data(), mode->size())) {
        SetErrno(kBadAddress);
        return false;
    }
    return true;
}

std::string BionicStdio::ClassifyPath(std::uint64_t address) const {
    constexpr std::uint64_t kMaximumPath = 4096;
    std::uint64_t length = 0;
    std::string ignored;
    if (memory_ == nullptr || address == 0 ||
        !memory_->BoundedStringLength(address, kMaximumPath + 1, &length,
                                      &ignored) ||
        length > kMaximumPath)
        return "invalid";
    std::string path(static_cast<std::size_t>(length), '\0');
    if (length != 0 &&
        !memory_->Read(address, path.data(), path.size()))
        return "invalid";
    if (path == "/proc/self/maps") return "proc-self-maps";
    return std::string(path.starts_with('/') ? "absolute-len"
                                             : "relative-len") +
           std::to_string(path.size());
}

std::uint64_t BionicStdio::Open(std::uint64_t path, std::uint64_t mode) {
    std::string text;
    if (!ReadMode(mode, &text)) return 0;
    // Bionic accepts a leading [rwa], an optional '+' for update mode, and
    // trailing flag letters ('e' = O_CLOEXEC, 'x' = O_EXCL, 'b', 'm', 's',
    // 't', 'n', 'c'). Unity's cpuinfo provider opens "/proc/cpuinfo" with
    // "re" — rejecting it leaves the CPU-topology singleton empty, which
    // cascades into JobWorkerCount = -1 and a wedged job queue.
    std::uint64_t open_flags = 0;
    switch (text[0]) {
        case 'r': break;
        case 'w': open_flags = 1 | 0x40 | 0x200; break;   // WRONLY|CREAT|TRUNC
        case 'a': open_flags = 1 | 0x40 | 0x400; break;   // WRONLY|CREAT|APPEND
        default:
            SetErrno(kInvalidArgument);
            return 0;
    }
    bool update = false;
    for (std::size_t i = 1; i < text.size(); ++i) {
        switch (text[i]) {
            case '+': update = true; break;
            case 'b': case 'e': case 'm': case 's': case 't': case 'x':
            case 'n': case 'c':
                break;
            default:
                SetErrno(kInvalidArgument);
                return 0;
        }
    }
    if (update) open_flags = (open_flags & ~3ULL) | 2;    // O_RDWR
    const int descriptor = files_->Open(path, open_flags, 0666);
    if (descriptor < 0) return 0;
    const std::uint64_t stream = heap_->Callocate(1, kBionicFileSize);
    if (stream == 0) {
        (void)files_->Close(descriptor);
        SetErrno(kOutOfMemory);
        return 0;
    }
    const std::lock_guard lock(mutex_);
    streams_.emplace(stream, descriptor);
    return stream;
}

int BionicStdio::Close(std::uint64_t stream, std::string* error) {
    const std::lock_guard lock(mutex_);
    const auto found = streams_.find(stream);
    if (found == streams_.end()) {
        SetErrno(kBadDescriptor);
        return -1;
    }
    if (files_->Close(found->second) != 0) return -1;
    if (!heap_->Release(found->first, error)) return -1;
    streams_.erase(found);
    return 0;
}

std::uint64_t BionicStdio::Read(std::uint64_t output,
                                std::uint64_t element_size,
                                std::uint64_t element_count,
                                std::uint64_t stream) {
    if (element_size == 0 || element_count == 0) return 0;
    if (element_count >
        std::numeric_limits<std::uint64_t>::max() / element_size) {
        SetErrno(kOverflow);
        return 0;
    }
    int descriptor = -1;
    {
        const std::lock_guard lock(mutex_);
        const auto found = streams_.find(stream);
        if (found == streams_.end()) {
            SetErrno(kBadDescriptor);
            return 0;
        }
        descriptor = found->second;
    }
    const std::int64_t bytes =
        files_->Read(descriptor, output, element_size * element_count);
    if (bytes < 0) return 0;
    return static_cast<std::uint64_t>(bytes) / element_size;
}

int BionicStdio::Seek(std::uint64_t stream, std::int64_t offset,
                      int origin) {
    int descriptor = -1;
    {
        const std::lock_guard lock(mutex_);
        const auto found = streams_.find(stream);
        if (found == streams_.end()) {
            SetErrno(kBadDescriptor);
            return -1;
        }
        descriptor = found->second;
    }
    return files_->Lseek64(descriptor, offset, origin) < 0 ? -1 : 0;
}

std::int64_t BionicStdio::Tell(std::uint64_t stream) {
    int descriptor = -1;
    {
        const std::lock_guard lock(mutex_);
        const auto found = streams_.find(stream);
        if (found == streams_.end()) {
            SetErrno(kBadDescriptor);
            return -1;
        }
        descriptor = found->second;
    }
    return files_->Lseek64(descriptor, 0, 1);
}

bool BionicStdio::ScanfStream(
    std::uint64_t stream, std::uint64_t format_address,
    const Aarch64ImportRegistry::Registers& registers, std::int64_t* result,
    std::string* error) {
    if (result == nullptr) {
        SetError(error, "guest fscanf result output is not configured");
        return false;
    }
    int descriptor = -1;
    {
        const std::lock_guard lock(mutex_);
        const auto found = streams_.find(stream);
        if (found == streams_.end()) {
            // Not a stream fopen produced (stdin/stdout/stderr or a foreign
            // FILE*): there is nothing to scan, so report end-of-input.
            *result = -1;
            return true;
        }
        descriptor = found->second;
    }
    constexpr std::uint64_t kMaximumFormat = 4096;
    std::uint64_t format_length = 0;
    std::string ignored;
    if (memory_ == nullptr || format_address == 0 ||
        !memory_->BoundedStringLength(format_address, kMaximumFormat + 1,
                                      &format_length, &ignored) ||
        format_length == 0 || format_length > kMaximumFormat) {
        SetErrno(kInvalidArgument);
        SetError(error, "guest fscanf format is invalid");
        return false;
    }
    std::string format(static_cast<std::size_t>(format_length), '\0');
    if (!memory_->Read(format_address, format.data(), format.size())) {
        SetErrno(kBadAddress);
        SetError(error, "guest fscanf format is not readable");
        return false;
    }
    // Snapshot the descriptor position, pull the remaining content into a
    // guest scratch buffer, then copy it to a host string for the scanner.
    const std::int64_t start = files_->Lseek64(descriptor, 0, 1);
    if (start < 0) {
        SetErrno(kInvalidArgument);
        *result = -1;
        return true;
    }
    constexpr std::uint64_t kScanBuffer = 256 * 1024;
    const std::uint64_t scratch = heap_->Callocate(1, kScanBuffer);
    if (scratch == 0) {
        SetErrno(kOutOfMemory);
        *result = -1;
        return true;
    }
    const std::int64_t bytes = files_->Read(descriptor, scratch, kScanBuffer);
    std::string input;
    if (bytes > 0) {
        input.resize(static_cast<std::size_t>(bytes));
        if (!memory_->Read(scratch, input.data(), input.size())) {
            std::string release_error;
            (void)heap_->Release(scratch, &release_error);
            SetErrno(kBadAddress);
            SetError(error, "guest fscanf input is not readable");
            return false;
        }
    }
    std::string release_error;
    (void)heap_->Release(scratch, &release_error);
    std::uint64_t raw_result = 0;
    std::size_t consumed = 0;
    if (!ScanBionicBuffer(memory_, input, format, registers, &consumed,
                          &raw_result, error)) {
        return false;
    }
    // The scanner read to end-of-buffer but only consumed `consumed` bytes;
    // rewind the descriptor so a later read sees the unconsumed tail.
    (void)files_->Lseek64(descriptor, start + static_cast<std::int64_t>(consumed),
                          0);
    *result = static_cast<std::int64_t>(raw_result);
    return true;
}

bool BionicStdio::IsModeledStream(std::uint64_t stream) const {
    const std::lock_guard lock(mutex_);
    return streams_.contains(stream);
}

bool BionicStdio::RegisterImports(Aarch64ImportRegistry* registry,
                                  std::string* error) {
    if (registry == nullptr || heap_ == nullptr || memory_ == nullptr ||
        files_ == nullptr || threads_ == nullptr) {
        SetError(error, "bionic stdio imports have null dependencies");
        return false;
    }
    // stdin/stdout/stderr are FILE* variables on Android, and libPVZ2
    // dereferences them (GOT slot -> variable -> FILE struct). Model both
    // levels: three variable slots holding FILE-struct pointers, and a
    // bionic-style __sF[3] FILE array the variables point into.
    constexpr std::uint64_t kStreamVarBytes = 8;
    heap_->SetAllocDebugTag("stdio-sf");
    const std::uint64_t vars_base =
        heap_->Callocate(1, 3 * kStreamVarBytes + 3 * kBionicFileSize);
    heap_->SetAllocDebugTag(nullptr);
    stream_vars_base_ = vars_base;
    if (vars_base == 0) {
        SetError(error, "bionic stdio stream allocation failed");
        return false;
    }
    const std::uint64_t stdin_var = vars_base;
    const std::uint64_t stdout_var = vars_base + kStreamVarBytes;
    const std::uint64_t stderr_var = vars_base + 2 * kStreamVarBytes;
    const std::uint64_t stdin_file = vars_base + 3 * kStreamVarBytes;
    const std::uint64_t stdout_file = stdin_file + kBionicFileSize;
    const std::uint64_t stderr_file = stdout_file + kBionicFileSize;
    const auto write_var = [&](std::uint64_t var, std::uint64_t file) {
        return heap_->Write(var, &file, sizeof(file));
    };
    if (!write_var(stdin_var, stdin_file) ||
        !write_var(stdout_var, stdout_file) ||
        !write_var(stderr_var, stderr_file)) {
        SetError(error, "bionic stdio stream variable write failed");
        return false;
    }
    if (!registry->RegisterDataAddress("__sF", stdin_file, error) ||
        !registry->RegisterDataAddress("stdin", stdin_var, error) ||
        !registry->RegisterDataAddress("stdout", stdout_var, error) ||
        !registry->RegisterDataAddress("stderr", stderr_var, error))
        return false;
    if (!registry->Register(
            "fflush",
            [this, registry](Aarch64ImportRegistry::Registers* registers,
                             std::string* dispatch_error) {
                const std::uint64_t stream = (*registers)[0];
                const auto sf_token = registry->Resolve("__sF");
                const auto stdin_token = registry->Resolve("stdin");
                const auto stdout_token = registry->Resolve("stdout");
                const auto stderr_token = registry->Resolve("stderr");
                if (stream == 0 || IsModeledStream(stream) ||
                    (sf_token && stream == *sf_token) ||
                    (sf_token && stream == *sf_token + kBionicFileSize) ||
                    (sf_token && stream == *sf_token + 2U * kBionicFileSize) ||
                    (stdin_token && stream == *stdin_token) ||
                    (stdout_token && stream == *stdout_token) ||
                    (stderr_token && stream == *stderr_token)) {
                    (*registers)[0] = 0;
                    return true;
                }
                SetError(dispatch_error,
                         "guest fflush stream token is invalid: stream=" +
                             std::to_string(stream));
                return false;
            },
            error) ||
        !registry->Register(
            "vfprintf",
            [this, registry](Aarch64ImportRegistry::Registers* registers,
                             std::string*) {
                // vfprintf(stream, fmt, ap): the engine's console/debug
                // logging runs through here (the Draper/Networker completion
                // path prints its result this way). Format via the shared
                // AArch64 va_list formatter and echo to the host console so
                // the message survives; a modeled std stream is the expected
                // target and any other stream is a benign discard. A format
                // the formatter cannot render is discarded rather than made
                // fatal — logging must not abort a live call.
                const std::uint64_t stream = (*registers)[0];
                std::string output;
                if (!FormatBionicVaList(memory_, (*registers)[1],
                                        (*registers)[2], &output, nullptr)) {
                    output.clear();
                }
                const auto sf_token = registry->Resolve("__sF");
                const bool is_sf_range =
                    sf_token && stream >= *sf_token &&
                    stream < *sf_token + 3 * kBionicFileSize;
                if (!output.empty() &&
                    (IsModeledStream(stream) || is_sf_range)) {
                    std::fwrite(output.data(), 1, output.size(), stderr);
                    std::fflush(stderr);
                }
                (*registers)[0] = output.size();
                return true;
            },
            error) ||
        !registry->Register(
            "fprintf",
            [this, registry](Aarch64ImportRegistry::Registers* registers,
                             std::string*) {
                // fprintf(stream, fmt, ...): the variadic twin of vfprintf.
                // PvZ's resource/boot logging reaches this once the host
                // inflate path completes loading; format via the shared
                // register/stack variadic formatter and echo to the host
                // console. Unrenderable formats are discarded, never fatal.
                const std::uint64_t stream = (*registers)[0];
                std::string output;
                if (!FormatBionicVariadicCall(heap_, memory_, *registers, 2,
                                              (*registers)[1], &output,
                                              nullptr)) {
                    output.clear();
                }
                const auto sf_token = registry->Resolve("__sF");
                const bool is_sf_range =
                    sf_token && stream >= *sf_token &&
                    stream < *sf_token + 3 * kBionicFileSize;
                if (!output.empty() &&
                    (IsModeledStream(stream) || is_sf_range)) {
                    std::fwrite(output.data(), 1, output.size(), stderr);
                    std::fflush(stderr);
                }
                (*registers)[0] = output.size();
                return true;
            },
            error) ||
        !registry->Register(
            "fputc",
            [this, registry](Aarch64ImportRegistry::Registers* registers,
                             std::string*) {
                const std::uint64_t stream = (*registers)[1];
                const auto sf_token = registry->Resolve("__sF");
                const bool is_sf_range =
                    sf_token && stream >= *sf_token &&
                    stream < *sf_token + 3 * kBionicFileSize;
                const int ch = static_cast<int>((*registers)[0] & 0xff);
                if (IsModeledStream(stream) || is_sf_range) {
                    std::fputc(ch, stderr);
                    std::fflush(stderr);
                }
                (*registers)[0] = static_cast<std::uint64_t>(ch);
                return true;
            },
            error) ||
        !registry->Register(
            "fputs",
            [this, registry](Aarch64ImportRegistry::Registers* registers,
                             std::string*) {
                const std::uint64_t stream = (*registers)[1];
                const auto sf_token = registry->Resolve("__sF");
                const bool is_sf_range =
                    sf_token && stream >= *sf_token &&
                    stream < *sf_token + 3 * kBionicFileSize;
                if (IsModeledStream(stream) || is_sf_range) {
                    std::uint64_t length = 0;
                    std::string ignored;
                    if (memory_->BoundedStringLength((*registers)[0],
                                                     1 << 20, &length,
                                                     &ignored) &&
                        length != 0) {
                        std::string text(length, '\0');
                        if (memory_->Read((*registers)[0], text.data(),
                                          length)) {
                            std::fwrite(text.data(), 1, length, stderr);
                            std::fflush(stderr);
                        }
                    }
                }
                (*registers)[0] = 1;
                return true;
            },
            error) ||
        !registry->Register(
            "fwrite",
            [this, registry](Aarch64ImportRegistry::Registers* registers,
                             std::string* dispatch_error) {
                const std::uint64_t size = (*registers)[1];
                const std::uint64_t count = (*registers)[2];
                if (size == 0 || count == 0) {
                    (*registers)[0] = 0;
                    return true;
                }
                // Writes to the modeled std streams (and to any stream the
                // stdio layer tracks) are accepted and discarded: the
                // engine's logging path runs through here during boot.
                const std::uint64_t stream = (*registers)[3];
                const auto sf_token = registry->Resolve("__sF");
                const bool is_sf_range =
                    sf_token && stream >= *sf_token &&
                    stream < *sf_token + 3 * kBionicFileSize;
                if (IsModeledStream(stream) || is_sf_range) {
                    (*registers)[0] = size * count;
                    return true;
                }
                SetError(dispatch_error,
                         "non-empty guest fwrite requires a writable FILE stream");
                return false;
            },
            error) ||
        !registry->Register(
            "fopen",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string* dispatch_error) {
                const std::string path_category =
                    ClassifyPath((*registers)[0]);
                (*registers)[0] = Open((*registers)[0], (*registers)[1]);
                if (dispatch_error != nullptr)
                    *dispatch_error = (*registers)[0] == 0
                        ? "trace:stdio-open-miss-" + path_category
                        : "trace:stdio-open-hit-" + path_category;
                return true;
            },
            error) ||
        !registry->Register(
            "fclose",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string* handler_error) {
                (*registers)[0] = static_cast<std::uint64_t>(
                    Close((*registers)[0], handler_error));
                return true;
            },
            error) ||
        !registry->Register(
            "fread",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string*) {
                (*registers)[0] = Read((*registers)[0], (*registers)[1],
                                       (*registers)[2], (*registers)[3]);
                return true;
            },
            error) ||
        !registry->Register(
            "fseek",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string*) {
                (*registers)[0] = static_cast<std::uint64_t>(Seek(
                    (*registers)[0],
                    static_cast<std::int64_t>((*registers)[1]),
                    static_cast<int>((*registers)[2])));
                return true;
            },
            error) ||
        !registry->Register(
            "fscanf",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string* dispatch_error) {
                std::int64_t result = -1;
                if (!ScanfStream((*registers)[0], (*registers)[1], *registers,
                                 &result, dispatch_error))
                    return false;
                (*registers)[0] = static_cast<std::uint64_t>(result);
                return true;
            },
            error) ||
        !registry->Register(
            "__isoc99_fscanf",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string* dispatch_error) {
                std::int64_t result = -1;
                if (!ScanfStream((*registers)[0], (*registers)[1], *registers,
                                 &result, dispatch_error))
                    return false;
                (*registers)[0] = static_cast<std::uint64_t>(result);
                return true;
            },
            error))
        return false;

    const auto seek_stream = [this](std::uint64_t stream, std::int64_t offset,
                                    int origin) {
        return static_cast<std::uint64_t>(Seek(stream, offset, origin));
    };
    const auto read_char = [this](std::uint64_t stream) -> std::uint64_t {
        const std::uint64_t cell = heap_->Callocate(1, 8);
        if (cell == 0) return std::numeric_limits<std::uint64_t>::max();
        std::uint8_t byte = 0;
        bool valid = Read(cell, 1, 1, stream) == 1 &&
                     memory_->Read(cell, &byte, 1);
        (void)heap_->Release(cell, nullptr);
        return valid ? static_cast<std::uint64_t>(byte)
                     : std::numeric_limits<std::uint64_t>::max();
    };

    if (!registry->Register(
            "ftell",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string*) {
                (*registers)[0] =
                    static_cast<std::uint64_t>(Tell((*registers)[0]));
                return true;
            },
            error) ||
        !registry->Register(
            "fseeko",
            [seek_stream](Aarch64ImportRegistry::Registers* registers,
                          std::string*) {
                (*registers)[0] =
                    seek_stream((*registers)[0],
                                static_cast<std::int64_t>((*registers)[1]),
                                static_cast<int>((*registers)[2]));
                return true;
            },
            error) ||
        !registry->Register(
            "fseeko64",
            [seek_stream](Aarch64ImportRegistry::Registers* registers,
                          std::string*) {
                (*registers)[0] =
                    seek_stream((*registers)[0],
                                static_cast<std::int64_t>((*registers)[1]),
                                static_cast<int>((*registers)[2]));
                return true;
            },
            error) ||
        !registry->Register(
            "ftello",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string*) {
                (*registers)[0] =
                    static_cast<std::uint64_t>(Tell((*registers)[0]));
                return true;
            },
            error) ||
        !registry->Register(
            "ftello64",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string*) {
                (*registers)[0] =
                    static_cast<std::uint64_t>(Tell((*registers)[0]));
                return true;
            },
            error) ||
        !registry->Register(
            "rewind",
            [seek_stream](Aarch64ImportRegistry::Registers* registers,
                          std::string*) {
                (void)seek_stream((*registers)[0], 0, 0);
                (*registers)[0] = 0;
                return true;
            },
            error) ||
        !registry->Register(
            "fgetpos",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string*) {
                const std::int64_t position = Tell((*registers)[0]);
                if (position < 0 || (*registers)[1] == 0 ||
                    !memory_->Write((*registers)[1], &position,
                                    sizeof(position))) {
                    (*registers)[0] = ~0ULL;
                    return true;
                }
                (*registers)[0] = 0;
                return true;
            },
            error) ||
        !registry->Register(
            "fsetpos",
            [seek_stream, this](Aarch64ImportRegistry::Registers* registers,
                                std::string*) {
                std::int64_t position = 0;
                if ((*registers)[1] == 0 ||
                    !memory_->Read((*registers)[1], &position,
                                   sizeof(position))) {
                    (*registers)[0] = ~0ULL;
                    return true;
                }
                (*registers)[0] =
                    seek_stream((*registers)[0], position, 0);
                return true;
            },
            error))
        return false;

    const auto constant_zero = [](Aarch64ImportRegistry::Registers* registers,
                                  std::string*) {
        (*registers)[0] = 0;
        return true;
    };
    for (const char* name :
         {"feof", "feof_unlocked", "ferror", "ferror_unlocked", "clearerr",
          "clearerr_unlocked", "funlockfile", "setbuf", "setbuffer",
          "setlinebuf", "setvbuf", "flockfile", "ftrylockfile",
          "__fsetlocking"}) {
        if (!registry->Register(name, constant_zero, error)) return false;
    }
    for (const char* name :
         {"fgetc", "getc", "fgetc_unlocked", "getc_unlocked"}) {
        if (!registry->Register(
                name,
                [read_char](Aarch64ImportRegistry::Registers* registers,
                            std::string*) {
                    (*registers)[0] = read_char((*registers)[0]);
                    return true;
                },
                error))
            return false;
    }
    for (const char* name : {"ungetc", "ungetc_unlocked"}) {
        if (!registry->Register(
                name,
                [seek_stream](Aarch64ImportRegistry::Registers* registers,
                              std::string*) {
                    if ((*registers)[0] ==
                            std::numeric_limits<std::uint64_t>::max() ||
                        seek_stream((*registers)[1], -1, 1) != 0) {
                        (*registers)[0] =
                            std::numeric_limits<std::uint64_t>::max();
                        return true;
                    }
                    (*registers)[0] =
                        static_cast<std::uint64_t>((*registers)[0]);
                    return true;
                },
                error))
            return false;
    }
    const auto fgets_body = [this](std::uint64_t buffer, std::uint64_t count,
                                   std::uint64_t stream) -> std::uint64_t {
        if (buffer == 0 || count == 0) return 0;
        if (count == 1) {
            const std::uint8_t only_terminator = 0;
            return memory_->Write(buffer, &only_terminator, 1) ? buffer : 0;
        }
        const std::uint64_t cell = heap_->Callocate(1, count);
        if (cell == 0) return 0;
        const std::uint64_t got = Read(cell, 1, count - 1, stream);
        if (got == 0) {
            (void)heap_->Release(cell, nullptr);
            return 0;
        }
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(got));
        if (!memory_->Read(cell, bytes.data(), bytes.size())) {
            (void)heap_->Release(cell, nullptr);
            return 0;
        }
        (void)heap_->Release(cell, nullptr);
        std::uint64_t keep = got;
        for (std::uint64_t i = 0; i < got; ++i) {
            if (bytes[i] == '\n') {
                keep = i + 1;
                break;
            }
        }
        if (got > keep) (void)Seek(stream, -(std::int64_t)(got - keep), 1);
        if (!memory_->Write(buffer, bytes.data(), static_cast<std::size_t>(keep))) {
            return 0;
        }
        const std::uint8_t terminator = 0;
        (void)memory_->Write(buffer + keep, &terminator, 1);
        return buffer;
    };
    if (!registry->Register(
            "fgets",
            [fgets_body](Aarch64ImportRegistry::Registers* registers,
                         std::string*) {
                (*registers)[0] = fgets_body((*registers)[0], (*registers)[1],
                                             (*registers)[2]);
                return true;
            },
            error) ||
        !registry->Register(
            "fgets_unlocked",
            [fgets_body](Aarch64ImportRegistry::Registers* registers,
                         std::string*) {
                (*registers)[0] = fgets_body((*registers)[0], (*registers)[1],
                                             (*registers)[2]);
                return true;
            },
            error))
        return false;
    for (const char* name : {"fileno", "fileno_unlocked"}) {
        if (!registry->Register(
                name,
                [this](Aarch64ImportRegistry::Registers* registers,
                       std::string*) {
                    const std::lock_guard lock(mutex_);
                    const auto found = streams_.find((*registers)[0]);
                    (*registers)[0] =
                        found == streams_.end()
                            ? std::numeric_limits<std::uint64_t>::max()
                            : static_cast<std::uint64_t>(found->second);
                    return true;
                },
                error))
            return false;
    }
    if (!registry->Register(
            "fdopen",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string*) {
                const int descriptor = static_cast<int>((*registers)[0]);
                const std::uint64_t stream =
                    heap_->Callocate(1, kBionicFileSize);
                if (stream == 0) {
                    SetErrno(kOutOfMemory);
                    (*registers)[0] = 0;
                    return true;
                }
                {
                    const std::lock_guard lock(mutex_);
                    streams_.emplace(stream, descriptor);
                }
                (*registers)[0] = stream;
                return true;
            },
            error))
        return false;
    for (const char* name : {"putc", "putc_unlocked", "fputc_unlocked"}) {
        if (!registry->Register(
                name,
                [this](Aarch64ImportRegistry::Registers* registers,
                       std::string*) {
                    const std::uint64_t character = (*registers)[0];
                    const std::uint64_t stream = (*registers)[1];
                    const std::uint8_t byte =
                        static_cast<std::uint8_t>(character);
                    int descriptor = -1;
                    {
                        const std::lock_guard lock(mutex_);
                        const auto found = streams_.find(stream);
                        if (found != streams_.end()) {
                            descriptor = found->second;
                        }
                    }
                    bool ok = false;
                    const std::uint64_t cell =
                        descriptor < 0 ? 0 : heap_->Callocate(1, 8);
                    if (cell != 0) {
                        ok = memory_->Write(cell, &byte, 1) &&
                             files_->Write(descriptor, cell, 1) == 1;
                        (void)heap_->Release(cell, nullptr);
                    }
                    if (!ok) {
                        (*registers)[0] =
                            std::numeric_limits<std::uint64_t>::max();
                        return true;
                    }
                    (*registers)[0] = character & 0xff;
                    return true;
                },
                error))
            return false;
    }
    return true;
}

bool BionicStdio::Shutdown(std::string* error) {
    const std::lock_guard lock(mutex_);
    for (const auto& [stream, descriptor] : streams_) {
        if (files_->Close(descriptor) != 0) {
            SetError(error, "guest stdio descriptor cleanup failed");
            return false;
        }
        if (!heap_->Release(stream, error)) return false;
    }
    streams_.clear();
    if (stream_vars_base_ != 0) {
        if (!heap_->Release(stream_vars_base_, error)) return false;
        stream_vars_base_ = 0;
    }
    return true;
}

std::size_t BionicStdio::stream_count() const {
    const std::lock_guard lock(mutex_);
    return streams_.size();
}

}  // namespace isomr
