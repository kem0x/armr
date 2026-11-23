#include "bionic_mappings.h"

#include "bionic_files.h"
#include "bionic_thread_state.h"
#include "guest_heap.h"
#include "import_registry.h"

#include <algorithm>
#include <cstdio>
#include <iterator>
#include <limits>
#include <utility>

namespace isomr {
namespace {

constexpr std::uint64_t kPageSize = 4096;
constexpr std::uint64_t kProtRead = 0x1;
constexpr std::uint64_t kProtWrite = 0x2;
constexpr std::uint64_t kProtReadWrite = 0x3;
constexpr std::uint64_t kProtExec = 0x4;
constexpr std::uint64_t kProtReadWriteExec = 0x7;
constexpr std::uint64_t kMapShared = 0x1;
constexpr std::uint64_t kMapPrivateAnonymous = 0x22;
constexpr std::uint64_t kMapGrowDown = 0x100;
constexpr std::uint64_t kMapStack = 0x20000;
constexpr std::uint64_t kMapPrivateAnonymousStack =
    kMapPrivateAnonymous | kMapGrowDown | kMapStack;
constexpr std::uint64_t kMadvNormal = 0;
constexpr std::uint64_t kMadvDontNeed = 4;
constexpr std::int32_t kIo = 5;
constexpr std::int32_t kOutOfMemory = 12;
constexpr std::int32_t kInvalidArgument = 22;

void SetError(std::string* error, std::string message) {
    if (error != nullptr) *error = std::move(message);
}

bool RoundToPage(std::uint64_t length, std::uint64_t* rounded) {
    if (rounded == nullptr || length == 0 ||
        length > std::numeric_limits<std::uint64_t>::max() -
                     (kPageSize - 1))
        return false;
    *rounded = (length + kPageSize - 1) & ~(kPageSize - 1);
    return true;
}

bool ReturnFailure(BionicThreadState* threads, std::int32_t guest_errno,
                   Aarch64ImportRegistry::Registers* registers,
                   std::string* error) {
    if (!threads->SetCurrentErrno(guest_errno, error)) return false;
    (*registers)[0] = std::numeric_limits<std::uint64_t>::max();
    return true;
}

}  // namespace

bool BionicMappings::RegisterImports(Aarch64ImportRegistry* imports,
                                     std::string* error) {
    if (imports == nullptr || heap_ == nullptr || threads_ == nullptr ||
        files_ == nullptr) {
        SetError(error, "bionic mapping import dependencies are null");
        return false;
    }
    if (!imports->Register(
            "mmap",
            [this](Aarch64ImportRegistry::Registers* registers,
                   std::string* dispatch_error) {
                std::uint64_t length = 0;
                const std::uint64_t requested_length = (*registers)[1];
                const std::uint64_t protection = (*registers)[2];
                const std::uint64_t flags = (*registers)[3];
                const std::uint64_t raw_descriptor = (*registers)[4];
                const std::uint64_t offset = (*registers)[5];
                const bool rounded = RoundToPage(requested_length, &length);
                // A nonzero address with MAP_FIXED absent is only a placement
                // hint; the Boehm GC passes the prior heap end to grow
                // contiguously, but honors whatever address is returned.
                const bool anonymous =
                    rounded && protection <= kProtReadWriteExec &&
                    (flags == kMapPrivateAnonymous ||
                     flags == kMapPrivateAnonymousStack) &&
                    offset == 0;
                const bool shared_file =
                    (*registers)[0] == 0 && rounded &&
                    (protection == kProtRead ||
                     protection == kProtReadWrite) &&
                    flags == kMapShared &&
                    raw_descriptor <=
                        static_cast<std::uint64_t>(
                            std::numeric_limits<int>::max()) &&
                    offset <= static_cast<std::uint64_t>(
                                  std::numeric_limits<std::int64_t>::max()) &&
                    (offset & (kPageSize - 1)) == 0;
                if (!anonymous && !shared_file) {
                    std::fprintf(
                        stderr,
                        "ISOMR_BIONIC_MMAP result=reject address=0x%llx "
                        "length=%llu protection=0x%llx flags=0x%llx "
                        "descriptor=%lld offset=%llu\n",
                        static_cast<unsigned long long>((*registers)[0]),
                        static_cast<unsigned long long>((*registers)[1]),
                        static_cast<unsigned long long>((*registers)[2]),
                        static_cast<unsigned long long>((*registers)[3]),
                        static_cast<long long>((*registers)[4]),
                        static_cast<unsigned long long>((*registers)[5]));
                    return ReturnFailure(threads_, kInvalidArgument,
                                         registers, dispatch_error);
                }
                // On AArch64 an executable page is always readable (the core
                // must read it to fetch), so PROT_EXEC implies read access
                // for the host backing store even when PROT_READ is clear.
                const bool readable =
                    (protection & (kProtRead | kProtExec)) != 0;
                const bool writable = (protection & kProtWrite) != 0;
                // Anonymous maps carry a non-MAP_FIXED placement hint in
                // registers[0]; Boehm's GC_expand_hp/GC_scratch_alloc pass the
                // prior heap end there to grow the scratch arena contiguously.
                const std::uint64_t address = heap_->ReserveMappingAligned(
                    length, kPageSize,
                    shared_file ? true : readable,
                    shared_file ? true : writable, dispatch_error,
                    anonymous ? (*registers)[0] : 0);
                if (address == 0) {
                    std::fprintf(
                        stderr,
                        "ISOMR_BIONIC_MMAP result=fail stage=reserve "
                        "length=%llu protection=0x%llx flags=0x%llx "
                        "descriptor=%lld offset=%llu caller=0x%llx\n",
                        static_cast<unsigned long long>((*registers)[1]),
                        static_cast<unsigned long long>((*registers)[2]),
                        static_cast<unsigned long long>((*registers)[3]),
                        static_cast<long long>((*registers)[4]),
                        static_cast<unsigned long long>((*registers)[5]),
                        static_cast<unsigned long long>(
                            registers->callee_saved_general[11]));
                    return ReturnFailure(threads_, kOutOfMemory, registers,
                                         dispatch_error);
                }
                const int descriptor =
                    shared_file ? static_cast<int>(raw_descriptor) : -1;
                const BionicFileSystem::FileReference file =
                    shared_file ? files_->RetainFile(descriptor)
                                : BionicFileSystem::FileReference{};
                const bool shared_writeback_capable =
                    shared_file && files_->IsWritable(file);
                if (shared_file) {
                    const std::int64_t read = files_->Pread64(
                        file, address, requested_length,
                        static_cast<std::int64_t>(offset));
                    if (read < 0) {
                        std::fprintf(
                            stderr,
                            "ISOMR_BIONIC_MMAP result=fail stage=pread "
                            "address=0x%llx length=%llu wanted=%llu "
                            "read=%lld offset=%llu descriptor=%lld "
                            "caller=0x%llx\n",
                            static_cast<unsigned long long>(address),
                            static_cast<unsigned long long>(length),
                            static_cast<unsigned long long>(
                                requested_length),
                            static_cast<long long>(read),
                            static_cast<unsigned long long>(offset),
                            static_cast<long long>((*registers)[4]),
                            static_cast<unsigned long long>(
                                registers->callee_saved_general[11]));
                        if (!heap_->ReleaseMappingRange(
                                address, length, dispatch_error))
                            return false;
                        (*registers)[0] =
                            std::numeric_limits<std::uint64_t>::max();
                        return true;
                    }
                    // A read short of requested_length means the file is
                    // smaller than the page-rounded map length; Linux permits
                    // this — the unread tail stays zero-committed and only an
                    // access past the EOF page boundary would fault. il2cpp
                    // maps global-metadata.dat with a page-rounded length that
                    // exceeds the file, so a short read is not an error.
                }
                if (shared_file &&
                    !heap_->SetProtection(address, length, readable,
                                          writable, dispatch_error)) {
                    if (!heap_->ReleaseMappingRange(
                            address, length, dispatch_error))
                        return false;
                    return false;
                }
                {
                    const std::lock_guard lock(mutex_);
                    mappings_.emplace(address,
                                      Mapping{length, protection, file,
                                              static_cast<std::int64_t>(offset),
                                              requested_length,
                                              shared_writeback_capable,
                                              shared_writeback_capable &&
                                                  (protection & kProtWrite) !=
                                                      0});
                }
                if ((protection & kProtExec) != 0)
                    heap_->SetExecutable(address, length, true);
                std::fprintf(
                    stderr,
                    "ISOMR_BIONIC_MMAP result=pass address=0x%llx "
                    "length=%llu protection=0x%llx flags=0x%llx "
                    "descriptor=%lld offset=%llu caller=0x%llx\n",
                    static_cast<unsigned long long>(address),
                    static_cast<unsigned long long>((*registers)[1]),
                    static_cast<unsigned long long>((*registers)[2]),
                    static_cast<unsigned long long>((*registers)[3]),
                    static_cast<long long>((*registers)[4]),
                    static_cast<unsigned long long>((*registers)[5]),
                    static_cast<unsigned long long>(
                        registers->callee_saved_general[11]));
                (*registers)[0] = address;
                return true;
            },
            error))
        return false;
    if (!imports->Register(
        "munmap",
        [this](Aarch64ImportRegistry::Registers* registers,
               std::string* dispatch_error) {
            std::uint64_t length = 0;
            if (((*registers)[0] & (kPageSize - 1)) != 0 ||
                !RoundToPage((*registers)[1], &length))
                return ReturnFailure(threads_, kInvalidArgument, registers,
                                     dispatch_error);
            const std::uint64_t address = (*registers)[0];
            if (address > std::numeric_limits<std::uint64_t>::max() - length)
                return ReturnFailure(threads_, kInvalidArgument, registers,
                                     dispatch_error);
            const std::uint64_t end = address + length;
            const std::lock_guard lock(mutex_);
            SplitAtLocked(address);
            SplitAtLocked(end);
            auto found = mappings_.lower_bound(address);
            while (found != mappings_.end() && found->first < end) {
                if (found->second.shared_writeback &&
                    found->second.file_length != 0) {
                    const std::int64_t written = files_->Pwrite64(
                        found->second.file, found->first,
                        found->second.file_length,
                        found->second.file_offset);
                    if (written != static_cast<std::int64_t>(
                                       found->second.file_length)) {
                        if (written < 0) {
                            (*registers)[0] =
                                std::numeric_limits<std::uint64_t>::max();
                            return true;
                        }
                        return ReturnFailure(threads_, kIo, registers,
                                             dispatch_error);
                    }
                }
                ++found;
            }
            found = mappings_.lower_bound(address);
            while (found != mappings_.end() && found->first < end) {
                const std::uint64_t release_address = found->first;
                const std::uint64_t release_length = found->second.length;
                if (!heap_->ReleaseMappingRange(
                        release_address, release_length, dispatch_error))
                    return false;
                found = mappings_.erase(found);
            }
            std::fprintf(
                stderr,
                "ISOMR_BIONIC_MUNMAP result=pass address=0x%llx length=%llu caller=0x%llx\n",
                static_cast<unsigned long long>(address),
                static_cast<unsigned long long>((*registers)[1]),
                static_cast<unsigned long long>(
                    registers->callee_saved_general[11]));
            (*registers)[0] = 0;
            return true;
        },
        error))
        return false;
    if (!imports->Register(
        "mprotect",
        [this](Aarch64ImportRegistry::Registers* registers,
               std::string* dispatch_error) {
            std::uint64_t length = 0;
            if (((*registers)[0] & (kPageSize - 1)) != 0 ||
                !RoundToPage((*registers)[1], &length) ||
                (*registers)[2] > kProtReadWriteExec)
                return ReturnFailure(threads_, kInvalidArgument, registers,
                                     dispatch_error);
            const std::uint64_t address = (*registers)[0];
            if (address > std::numeric_limits<std::uint64_t>::max() - length)
                return ReturnFailure(threads_, kInvalidArgument, registers,
                                     dispatch_error);
            const std::uint64_t end = address + length;
            const std::lock_guard lock(mutex_);
            if (!CoversRangeLocked(address, length))
                return ReturnFailure(threads_, kInvalidArgument, registers,
                                     dispatch_error);
            const bool readable =
                ((*registers)[2] & (kProtRead | kProtExec)) != 0;
            const bool writable = ((*registers)[2] & kProtWrite) != 0;
            if (!heap_->SetProtection(address, length, readable, writable,
                                      dispatch_error))
                return false;
            heap_->SetExecutable(address, length,
                                 ((*registers)[2] & kProtExec) != 0);
            SplitAtLocked(address);
            SplitAtLocked(end);
            for (auto found = mappings_.lower_bound(address);
                 found != mappings_.end() && found->first < end; ++found) {
                found->second.protection = (*registers)[2];
                found->second.shared_writeback =
                    found->second.shared_writeback_capable && writable;
            }
            MergeAdjacentLocked();
            std::fprintf(
                stderr,
                "ISOMR_BIONIC_MPROTECT result=pass address=0x%llx length=%llu protection=0x%llx caller=0x%llx\n",
                static_cast<unsigned long long>(address),
                static_cast<unsigned long long>((*registers)[1]),
                static_cast<unsigned long long>((*registers)[2]),
                static_cast<unsigned long long>(
                    registers->callee_saved_general[11]));
            (*registers)[0] = 0;
            return true;
        },
        error))
        return false;
    return imports->Register(
        "madvise",
        [this](Aarch64ImportRegistry::Registers* registers,
               std::string* dispatch_error) {
            std::uint64_t length = 0;
            const std::uint64_t address = (*registers)[0];
            const std::uint64_t advice = (*registers)[2];
            if ((address & (kPageSize - 1)) != 0 ||
                !RoundToPage((*registers)[1], &length) ||
                address > std::numeric_limits<std::uint64_t>::max() - length ||
                (advice != kMadvNormal && advice != kMadvDontNeed))
                return ReturnFailure(threads_, kInvalidArgument, registers,
                                     dispatch_error);
            {
                const std::lock_guard lock(mutex_);
                if (!CoversRangeLocked(address, length))
                    return ReturnFailure(threads_, kInvalidArgument,
                                         registers, dispatch_error);
                if (advice == kMadvDontNeed) {
                    const std::uint64_t end = address + length;
                    auto mapping = mappings_.lower_bound(address);
                    if (mapping != mappings_.begin()) {
                        auto previous = mapping;
                        --previous;
                        if (previous->first + previous->second.length >
                            address)
                            mapping = previous;
                    }
                    for (; mapping != mappings_.end() &&
                           mapping->first < end; ++mapping) {
                        if (mapping->second.file)
                            return ReturnFailure(
                                threads_, kInvalidArgument,
                                registers, dispatch_error);
                    }
                    if (!heap_->DiscardMappingRange(address, length,
                                                     dispatch_error))
                        return false;
                }
            }
            std::fprintf(
                stderr,
                "ISOMR_BIONIC_MADVISE result=pass address=0x%llx length=%llu advice=%llu caller=0x%llx\n",
                static_cast<unsigned long long>(address),
                static_cast<unsigned long long>((*registers)[1]),
                static_cast<unsigned long long>(advice),
                static_cast<unsigned long long>(
                    registers->callee_saved_general[11]));
            (*registers)[0] = 0;
            return true;
        },
        error);
}

bool BionicMappings::Shutdown(std::string* error) {
    const std::lock_guard lock(mutex_);
    std::vector<BionicFileSystem::FileReference> retained_files;
    retained_files.reserve(mappings_.size());
    for (const auto& [address, mapping] : mappings_) {
        (void)address;
        if (mapping.file) retained_files.push_back(mapping.file);
    }
    BionicFileSystem::ProcessExitLockCleanup lock_cleanup;
    if (!files_->ReleaseLocksForProcessExit(retained_files, &lock_cleanup,
                                             error))
        return false;
    if (lock_cleanup.locks != 0) {
        std::fprintf(stderr,
                     "ISOMR_BIONIC_PROCESS_EXIT_FILE_LOCKS records=%zu "
                     "released=%zu\n",
                     lock_cleanup.records, lock_cleanup.locks);
    }
    for (const auto& [address, mapping] : mappings_) {
        if (mapping.shared_writeback) {
            std::string failure_stage;
            const std::int64_t written = files_->Pwrite64(
                mapping.file, address, mapping.file_length,
                mapping.file_offset, &failure_stage);
            if (written !=
                static_cast<std::int64_t>(mapping.file_length)) {
                std::fprintf(
                    stderr,
                    "ISOMR_BIONIC_MAPPING_WRITEBACK result=fail "
                    "length=%llu file-length=%llu protection=0x%llx "
                    "capable=%u file-writable=%u guest-readable=%u "
                    "written=%lld stage=%s\n",
                    static_cast<unsigned long long>(mapping.length),
                    static_cast<unsigned long long>(mapping.file_length),
                    static_cast<unsigned long long>(mapping.protection),
                    mapping.shared_writeback_capable ? 1U : 0U,
                    files_->IsWritable(mapping.file) ? 1U : 0U,
                    heap_->IsReadableRange(
                        address,
                        static_cast<std::size_t>(mapping.file_length))
                        ? 1U
                        : 0U,
                    static_cast<long long>(written),
                    failure_stage.empty() ? "short-write"
                                          : failure_stage.c_str());
                SetError(error, "shared file mapping writeback failed");
                return false;
            }
        }
        if (!heap_->ReleaseMappingRange(address, mapping.length, error))
            return false;
    }
    mappings_.clear();
    return true;
}

std::size_t BionicMappings::mapping_count() const {
    const std::lock_guard lock(mutex_);
    return mappings_.size();
}

bool BionicMappings::CoversRangeLocked(std::uint64_t address,
                                       std::uint64_t length) const {
    if (length == 0 ||
        address > std::numeric_limits<std::uint64_t>::max() - length)
        return false;
    const std::uint64_t end = address + length;
    std::uint64_t cursor = address;
    auto mapping = mappings_.upper_bound(address);
    if (mapping != mappings_.begin()) --mapping;
    while (cursor < end) {
        if (mapping == mappings_.end() || mapping->first > cursor ||
            mapping->first + mapping->second.length <= cursor)
            return false;
        cursor = std::min(end,
                          mapping->first + mapping->second.length);
        ++mapping;
    }
    return true;
}

void BionicMappings::SplitAtLocked(std::uint64_t address) {
    auto found = mappings_.upper_bound(address);
    if (found == mappings_.begin()) return;
    --found;
    const std::uint64_t start = found->first;
    const std::uint64_t end = start + found->second.length;
    if (address <= start || address >= end) return;
    Mapping right = found->second;
    const std::uint64_t left_length = address - start;
    right.length = end - address;
    if (right.file) {
        right.file_offset += static_cast<std::int64_t>(left_length);
        right.file_length = found->second.file_length > left_length
            ? found->second.file_length - left_length
            : 0;
        found->second.file_length =
            std::min(found->second.file_length, left_length);
    }
    found->second.length = left_length;
    mappings_.emplace(address, right);
}

void BionicMappings::MergeAdjacentLocked() {
    auto found = mappings_.begin();
    while (found != mappings_.end()) {
        auto next = std::next(found);
        if (next == mappings_.end()) break;
        const bool adjacent =
            found->first + found->second.length == next->first;
        const bool compatible =
            found->second.protection == next->second.protection &&
            found->second.file == next->second.file &&
            found->second.shared_writeback_capable ==
                next->second.shared_writeback_capable &&
            found->second.shared_writeback ==
                next->second.shared_writeback &&
            (!found->second.file ||
             found->second.file_offset +
                     static_cast<std::int64_t>(found->second.length) ==
                 next->second.file_offset);
        if (adjacent && compatible) {
            found->second.length += next->second.length;
            found->second.file_length += next->second.file_length;
            mappings_.erase(next);
        } else {
            found = next;
        }
    }
}

}  // namespace isomr
