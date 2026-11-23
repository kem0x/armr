#include "bionic_log.h"

#include "bionic_format.h"
#include "guest_address_space.h"
#include "guest_heap.h"
#include "import_registry.h"

#include <cstdio>
#include <cstdint>
#include <array>
#include <limits>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string_view>
#include <utility>

namespace isomr {
namespace {

constexpr std::size_t kMaximumTag = 256;
constexpr std::uint64_t kMaximumMessage = 1024U * 1024U;

void SetError(std::string* error, std::string message) {
    if (error != nullptr) *error = std::move(message);
}

bool ReadTag(GuestAddressSpace* memory, std::uint64_t address,
             std::string* tag, std::string* error) {
    if (memory == nullptr || tag == nullptr || address == 0) {
        SetError(error, "__android_log_print tag is invalid");
        return false;
    }
    std::uint64_t length = 0;
    if (!memory->BoundedStringLength(address, kMaximumTag + 1, &length,
                                     error) ||
        length > kMaximumTag ||
        length > std::numeric_limits<std::size_t>::max()) {
        SetError(error, "__android_log_print tag exceeds its bound");
        return false;
    }
    tag->resize(static_cast<std::size_t>(length));
    if (tag->empty() || memory->Read(address, tag->data(), tag->size()))
        return true;
    SetError(error, "__android_log_print tag is unreadable");
    return false;
}

bool ReadMessage(GuestAddressSpace* memory, std::uint64_t address,
                 std::string* message, std::string* error) {
    if (memory == nullptr || message == nullptr || address == 0) {
        SetError(error, "__android_log_write message is invalid");
        return false;
    }
    std::uint64_t length = 0;
    if (!memory->BoundedStringLength(address, kMaximumMessage,
                                     &length, error) ||
        length >= kMaximumMessage ||
        length > std::numeric_limits<std::size_t>::max()) {
        SetError(error, "__android_log_write message exceeds its bound");
        return false;
    }
    message->resize(static_cast<std::size_t>(length));
    if (message->empty() ||
        memory->Read(address, message->data(), message->size()))
        return true;
    SetError(error, "__android_log_write message is unreadable");
    return false;
}

}  // namespace

BionicLog::BionicLog(GuestHeap* heap, GuestAddressSpace* memory)
    : heap_(heap), memory_(memory) {}

bool BionicLog::RegisterImports(Aarch64ImportRegistry* imports,
                                std::string* error) {
    if (registration_attempted_ || imports == nullptr || heap_ == nullptr ||
        memory_ == nullptr) {
        SetError(error, "bionic log configuration is invalid");
        return false;
    }
    registration_attempted_ = true;
    imports_ = imports;
    return imports->Register(
               "__android_log_print",
               [this](Aarch64ImportRegistry::Registers* registers,
                      std::string* dispatch_error) {
                   return Print(registers, dispatch_error);
               },
               error) &&
           imports->Register(
               "__android_log_vprint",
               [this](Aarch64ImportRegistry::Registers* registers,
                      std::string* dispatch_error) {
                   return VPrint(registers, dispatch_error);
               },
               error) &&
           imports->Register(
               "__android_log_write",
               [this](Aarch64ImportRegistry::Registers* registers,
                      std::string* dispatch_error) {
                   return Write(registers, dispatch_error);
               },
               error) &&
           imports->Register(
               "android_set_abort_message",
               [this](Aarch64ImportRegistry::Registers* registers,
                      std::string* dispatch_error) {
                   return SetAbortMessage(registers, dispatch_error);
               },
               error) &&
           imports->Register(
               "openlog",
               [this](Aarch64ImportRegistry::Registers* registers,
                      std::string* dispatch_error) {
                   // openlog(ident, option, facility): keep the ident so
                   // syslog output carries the caller's tag.
                   if (registers == nullptr) {
                       SetError(dispatch_error,
                                "openlog arguments are invalid");
                       return false;
                   }
                   std::string ident;
                   if ((*registers)[0] != 0 &&
                       !ReadTag(memory_, (*registers)[0], &ident,
                                dispatch_error))
                       return false;
                   {
                       const std::lock_guard lock(mutex_);
                       syslog_ident_ = std::move(ident);
                   }
                   return true;
               },
               error) &&
           imports->Register(
               "closelog",
               [](Aarch64ImportRegistry::Registers*, std::string*) {
                   return true;
               },
               error) &&
           imports->Register(
               "setlogmask",
               [this](Aarch64ImportRegistry::Registers* registers,
                      std::string* dispatch_error) {
                   if (registers == nullptr) {
                       SetError(dispatch_error,
                                "setlogmask arguments are invalid");
                       return false;
                   }
                   const std::uint64_t previous =
                       std::exchange(syslog_mask_, (*registers)[0]);
                   (*registers)[0] = previous;
                   return true;
               },
               error) &&
           imports->Register(
               "syslog",
               [this](Aarch64ImportRegistry::Registers* registers,
                      std::string* dispatch_error) {
                   return Syslog(registers, dispatch_error);
               },
               error) &&
           imports->Register(
               "vsyslog",
               [this](Aarch64ImportRegistry::Registers* registers,
                      std::string* dispatch_error) {
                   return VSyslog(registers, dispatch_error);
               },
               error) &&
           imports->Register(
               "__syslog_chk",
               [this](Aarch64ImportRegistry::Registers* registers,
                      std::string* dispatch_error) {
                   // __syslog_chk(priority, flags, fmt, ...): the fortified
                   // variant; format args begin at general[3].
                   return SyslogChk(registers, dispatch_error);
               },
               error);
}

void BionicLog::MaybeDumpStack(
    Aarch64ImportRegistry::Registers* registers, char level,
    const std::string& tag, const std::string& message) {
    // ISOMR_LOG_STACK=1: for exception/Unity error lines, scan the guest
    // stack for module-range words (return addresses) so the managed
    // caller chain is recoverable offline.
    static const bool log_stack = [] {
        wchar_t flag[8]{};
        return GetEnvironmentVariableW(L"ISOMR_LOG_STACK", flag, 8) > 0 &&
               flag[0] == L'1';
    }();
    static std::size_t stack_dump_count = 0;
    if (!log_stack || stack_dump_count >= 12 ||
        (message.find("Exception") == std::string::npos &&
         !(level == 'E' && tag == "Unity")))
        return;
    ++stack_dump_count;
    std::fprintf(stderr, "ISOMR_LOG_STACK lr=%llx sp=%llx",
                 static_cast<unsigned long long>(
                     registers->callee_saved_general[11]),
                 static_cast<unsigned long long>(
                     registers->stack_pointer));
    std::array<std::uint64_t, 256> window{};
    if (memory_ != nullptr &&
        memory_->Read(registers->stack_pointer, window.data(),
                      sizeof(window))) {
        std::size_t found = 0;
        for (std::uint64_t word : window) {
            if (found >= 16) break;
            if (word < 0x4000000000ULL || word > 0x4300000000ULL ||
                (word & 3U) != 0)
                continue;
            std::fprintf(stderr, " <%llx>",
                         static_cast<unsigned long long>(word));
            ++found;
        }
    }
    std::fputc('\n', stderr);
    // The per-thread JNI note ring still names the calls the throwing
    // thread made just before the exception — usually including the
    // null-returning call whose result was dereferenced.
    if (imports_ != nullptr) {
        const std::string notes = imports_->RecentJniNotesForCurrentThread(32);
        if (!notes.empty()) {
            std::fprintf(stderr, "ISOMR_LOG_JNITAIL %s\n", notes.c_str());
        }
    }
}

bool BionicLog::Write(Aarch64ImportRegistry::Registers* registers,
                      std::string* error) {
    if (registers == nullptr || (*registers)[0] > 8 ||
        (*registers)[2] == 0) {
        SetError(error, "__android_log_write arguments are invalid");
        return false;
    }
    std::string tag;
    std::string message;
    if (!ReadTag(memory_, (*registers)[1], &tag, error) ||
        !ReadMessage(memory_, (*registers)[2], &message, error)) {
        (*registers)[0] = static_cast<std::uint32_t>(-1);
        return false;
    }
    static constexpr char kLevels[] = "??VDIWEF?";
    const char level = kLevels[static_cast<std::size_t>((*registers)[0])];
    {
        const std::lock_guard lock(mutex_);
        std::fprintf(stderr, "%c/", level);
        if (!tag.empty())
            std::fwrite(tag.data(), 1, tag.size(), stderr);
        std::fputs(": ", stderr);
        if (!message.empty())
            std::fwrite(message.data(), 1, message.size(), stderr);
        std::fputc('\n', stderr);
        MaybeDumpStack(registers, level, tag, message);
        std::fflush(stderr);
        ++call_count_;
    }
    // liblog's non-formatting write API reports one successfully emitted
    // log record rather than the message byte count.
    (*registers)[0] = 1;
    return true;
}

bool BionicLog::Print(Aarch64ImportRegistry::Registers* registers,
                      std::string* error) {
    if (registers == nullptr || (*registers)[0] > 8 ||
        (*registers)[2] == 0) {
        SetError(error, "__android_log_print arguments are invalid");
        return false;
    }
    std::string tag;
    std::string message;
    if (!ReadTag(memory_, (*registers)[1], &tag, error) ||
        !FormatBionicVariadicCall(heap_, memory_, *registers, 3,
                                  (*registers)[2], &message, error)) {
        (*registers)[0] = static_cast<std::uint32_t>(-1);
        return false;
    }
    static constexpr char kLevels[] = "??VDIWEF?";
    const char level = kLevels[static_cast<std::size_t>((*registers)[0])];
    {
        const std::lock_guard lock(mutex_);
        std::fprintf(stderr, "%c/", level);
        if (!tag.empty())
            std::fwrite(tag.data(), 1, tag.size(), stderr);
        std::fputs(": ", stderr);
        if (!message.empty())
            std::fwrite(message.data(), 1, message.size(), stderr);
        std::fputc('\n', stderr);
        MaybeDumpStack(registers, level, tag, message);
        std::fflush(stderr);
        ++call_count_;
    }
    (*registers)[0] = message.size();
    return true;
}

bool BionicLog::VPrint(Aarch64ImportRegistry::Registers* registers,
                       std::string* error) {
    if (registers == nullptr || (*registers)[0] > 8 ||
        (*registers)[2] == 0 || (*registers)[3] == 0) {
        SetError(error, "__android_log_vprint arguments are invalid");
        return false;
    }
    const std::size_t priority = static_cast<std::size_t>((*registers)[0]);
    std::string tag;
    std::string message;
    if (!ReadTag(memory_, (*registers)[1], &tag, error) ||
        !FormatBionicVaList(memory_, (*registers)[2], (*registers)[3],
                            &message, error)) {
        (*registers)[0] = static_cast<std::uint32_t>(-1);
        return false;
    }
    static constexpr char kLevels[] = "??VDIWEF?";
    const char level = kLevels[priority];
    {
        const std::lock_guard lock(mutex_);
        std::fprintf(stderr, "%c/", level);
        if (!tag.empty())
            std::fwrite(tag.data(), 1, tag.size(), stderr);
        std::fputs(": ", stderr);
        if (!message.empty())
            std::fwrite(message.data(), 1, message.size(), stderr);
        std::fputc('\n', stderr);
        MaybeDumpStack(registers, level, tag, message);
        std::fflush(stderr);
        ++call_count_;
    }
    (*registers)[0] = message.size();
    return true;
}

bool BionicLog::SetAbortMessage(
    Aarch64ImportRegistry::Registers* registers, std::string* error) {
    if (registers == nullptr) {
        SetError(error, "android_set_abort_message arguments are invalid");
        return false;
    }
    // A null message clears the pending abort text; both are void returns.
    std::string message;
    if ((*registers)[0] != 0 &&
        !ReadMessage(memory_, (*registers)[0], &message, error))
        return false;
    {
        const std::lock_guard lock(mutex_);
        std::fputs("F/abort_message: ", stderr);
        if (!message.empty())
            std::fwrite(message.data(), 1, message.size(), stderr);
        std::fputc('\n', stderr);
        std::fflush(stderr);
        ++call_count_;
    }
    (*registers)[0] = 0;
    return true;
}

bool BionicLog::Syslog(Aarch64ImportRegistry::Registers* registers,
                       std::string* error) {
    if (registers == nullptr) {
        SetError(error, "syslog arguments are invalid");
        return false;
    }
    std::string message;
    if (!FormatBionicVariadicCall(heap_, memory_, *registers, 2,
                                  (*registers)[1], &message, error))
        return false;
    static constexpr char kLevels[] = "EACEWNID";
    const std::size_t level =
        static_cast<std::size_t>((*registers)[0] & 7);
    const std::lock_guard lock(mutex_);
    std::fprintf(stderr, "%c/", kLevels[level]);
    if (!syslog_ident_.empty()) {
        std::fwrite(syslog_ident_.data(), 1, syslog_ident_.size(), stderr);
        std::fputs(": ", stderr);
    }
    if (!message.empty())
        std::fwrite(message.data(), 1, message.size(), stderr);
    std::fputc('\n', stderr);
    std::fflush(stderr);
    ++call_count_;
    return true;
}

bool BionicLog::VSyslog(Aarch64ImportRegistry::Registers* registers,
                        std::string* error) {
    if (registers == nullptr) {
        SetError(error, "vsyslog arguments are invalid");
        return false;
    }
    std::string message;
    if (!FormatBionicVaList(memory_, (*registers)[1], (*registers)[2],
                            &message, error))
        return false;
    static constexpr char kLevels[] = "EACEWNID";
    const std::size_t level =
        static_cast<std::size_t>((*registers)[0] & 7);
    const std::lock_guard lock(mutex_);
    std::fprintf(stderr, "%c/", kLevels[level]);
    if (!syslog_ident_.empty()) {
        std::fwrite(syslog_ident_.data(), 1, syslog_ident_.size(), stderr);
        std::fputs(": ", stderr);
    }
    if (!message.empty())
        std::fwrite(message.data(), 1, message.size(), stderr);
    std::fputc('\n', stderr);
    std::fflush(stderr);
    ++call_count_;
    return true;
}

bool BionicLog::SyslogChk(Aarch64ImportRegistry::Registers* registers,
                          std::string* error) {
    if (registers == nullptr) {
        SetError(error, "__syslog_chk arguments are invalid");
        return false;
    }
    std::string message;
    if (!FormatBionicVariadicCall(heap_, memory_, *registers, 3,
                                  (*registers)[2], &message, error))
        return false;
    static constexpr char kLevels[] = "EACEWNID";
    const std::size_t level =
        static_cast<std::size_t>((*registers)[0] & 7);
    const std::lock_guard lock(mutex_);
    std::fprintf(stderr, "%c/", kLevels[level]);
    if (!syslog_ident_.empty()) {
        std::fwrite(syslog_ident_.data(), 1, syslog_ident_.size(), stderr);
        std::fputs(": ", stderr);
    }
    if (!message.empty())
        std::fwrite(message.data(), 1, message.size(), stderr);
    std::fputc('\n', stderr);
    std::fflush(stderr);
    ++call_count_;
    return true;
}

std::size_t BionicLog::call_count() const {
    const std::lock_guard lock(mutex_);
    return call_count_;
}

}  // namespace isomr
