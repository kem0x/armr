#include "guest_address_space.h"

#include "elf64_modules.h"
#include "guest_heap.h"

#include <algorithm>
#include <array>
#include <limits>
#include <new>
#include <utility>
#include <vector>
#include <cstdio>

namespace isomr {
namespace {

void SetError(std::string* error, std::string message) {
    if (error != nullptr) *error = std::move(message);
}

bool AddAddress(std::uint64_t base, std::size_t offset,
                std::uint64_t* result) {
    if (result == nullptr ||
        base > std::numeric_limits<std::uint64_t>::max() - offset)
        return false;
    *result = base + offset;
    return true;
}

}  // namespace

namespace {
constexpr std::uint64_t kAddressSpaceSelfMagic = 0x49534F4D52535031ULL;
}

GuestAddressSpace::GuestAddressSpace(GuestHeap* heap,
                                     std::size_t maximum_transfer,
                                     std::size_t maximum_string)
    : heap_(heap),
      maximum_transfer_(maximum_transfer),
      maximum_string_(maximum_string),
      self_magic_(kAddressSpaceSelfMagic) {
    std::fprintf(stderr,
                 "ISOMR_ADDRESS_SPACE_CTOR space=%p heap=%p\n",
                 static_cast<const void*>(this),
                 static_cast<const void*>(heap));
    std::fflush(stderr);
}

GuestAddressSpace::~GuestAddressSpace() {
    std::fprintf(stderr,
                 "ISOMR_ADDRESS_SPACE_DTOR space=%p heap=%p\n",
                 static_cast<const void*>(this),
                 static_cast<const void*>(heap_));
    std::fflush(stderr);
}

bool GuestAddressSpace::self_valid() const {
    return self_magic_ == kAddressSpaceSelfMagic;
}

bool GuestAddressSpace::AttachModules(Elf64ModuleSet* modules,
                                      std::string* error) {
    if (modules == nullptr || modules_ != nullptr) {
        SetError(error, "guest module set is null or already attached");
        return false;
    }
    modules_ = modules;
    return true;
}

bool GuestAddressSpace::Read(std::uint64_t address, void* destination,
                             std::size_t size) const {
    if (!self_valid()) {
        static std::size_t self_fail_logs = 0;
        if (self_fail_logs < 6) {
            ++self_fail_logs;
            std::fprintf(stderr,
                         "ISOMR_SPACE_INVALID_SELF space=%p address=%llx\n",
                         static_cast<const void*>(this),
                         static_cast<unsigned long long>(address));
            std::fflush(stderr);
        }
        return false;
    }
    if (heap_ == reinterpret_cast<GuestHeap*>(0xBAADF00DBAADF00DULL)) {
        static std::size_t poison_read_logs = 0;
        if (poison_read_logs < 6) {
            ++poison_read_logs;
            char detail[128];
            snprintf(detail, sizeof(detail),
                     "ISOMR_SPACE_POISON_READ space=%p address=%llx\n",
                     static_cast<const void*>(this),
                     static_cast<unsigned long long>(address));
            std::fputs(detail, stderr);
            std::fflush(stderr);
        }
        return false;
    }
    if (heap_ != nullptr && heap_->ContainsAddress(address)) {
        // Tracked malloc blocks take the audited path; committed mmap
        // pages (bionic mmap allocations) have no block metadata but the
        // guest itself can read them, so the host may too.
        if (heap_->Read(address, destination, size)) return true;
        if (heap_->ReadCommittedUntracked(address, destination, size))
            return true;
        // Diagnostic fallback: Unity's JobQueue/il2cpp arenas are reserved
        // PROT_NONE then committed+mprotect'd on demand, which the guest-side
        // protection/committed bookkeeping can lag. ReadPhys reads whatever
        // host pages are actually committed — only reached after the audited
        // and untracked paths refuse, so it never widens guest-visible reads.
        if (heap_->ReadPhys(address, destination, size)) return true;
        static std::size_t space_read_refusals = 0;
        if (space_read_refusals < 8) {
            ++space_read_refusals;
            char detail[160];
            snprintf(detail, sizeof(detail),
                     "ISOMR_SPACE_READ_REFUSED address=%llx size=%zu\n",
                     static_cast<unsigned long long>(address), size);
            std::fputs(detail, stderr);
            std::fflush(stderr);
        }
        return false;
    }
    return modules_ != nullptr && modules_->Read(address, destination, size);
}

bool GuestAddressSpace::Write(std::uint64_t address, const void* source,
                              std::size_t size) {
    if (heap_ != nullptr && heap_->ContainsAddress(address)) {
        // Tracked malloc blocks take the audited path; committed mmap
        // pages (bionic mmap allocations) have no block metadata but the
        // guest itself can write them, so the host may too.
        if (heap_->Write(address, source, size)) return true;
        return heap_->WriteCommittedUntracked(address, source, size);
    }
    return modules_ != nullptr && modules_->Write(address, source, size);
}

bool GuestAddressSpace::IsWritableRange(std::uint64_t address,
                                        std::size_t size) const {
    if (heap_ != nullptr && heap_->ContainsAddress(address))
        return heap_->IsWritableRange(address, size);
    return modules_ != nullptr && modules_->IsWritableRange(address, size);
}

std::string GuestAddressSpace::DescribeRange(std::uint64_t address,
                                             std::size_t size) const {
    if (heap_ != nullptr && heap_->ContainsAddress(address))
        return heap_->DescribeRange(address, size);
    static std::size_t describe_fallback_logs = 0;
    if (describe_fallback_logs < 6) {
        ++describe_fallback_logs;
        char detail[160];
        snprintf(detail, sizeof(detail),
                 "ISOMR_DESCRIBE_FALLBACK space=%p heap=%llx address=%llx\n",
                 static_cast<const void*>(this),
                 static_cast<unsigned long long>(
                     reinterpret_cast<uintptr_t>(heap_)),
                 static_cast<unsigned long long>(address));
        std::fputs(detail, stderr);
        std::fflush(stderr);
    }
    return "not-guest-heap";
}

bool GuestAddressSpace::CheckedSize(std::uint64_t size, std::size_t* checked,
                                    std::string* error) const {
    if (checked == nullptr || size > maximum_transfer_ ||
        size > std::numeric_limits<std::size_t>::max()) {
        SetError(error, "guest memory operation exceeds its transfer bound");
        return false;
    }
    *checked = static_cast<std::size_t>(size);
    return true;
}

bool GuestAddressSpace::Copy(std::uint64_t destination,
                             std::uint64_t source, std::uint64_t size,
                             std::string* error) {
    std::size_t checked = 0;
    if (!CheckedSize(size, &checked, error)) return false;
    if (checked == 0) return true;
    std::vector<std::uint8_t> buffer;
    try {
        buffer.resize(checked);
    } catch (const std::bad_alloc&) {
        SetError(error, "guest memory copy buffer allocation failed");
        return false;
    }
    const bool src_ok = source != 0 &&
                        Read(source, buffer.data(), buffer.size());
    if (!src_ok) {
        // A null/invalid source (from a compat service with no backing
        // data) produces a zeroed destination, matching what the engine
        // would see from an empty file read.
        std::fprintf(stderr,
                     "ISOMR_COPY_ZEROFILL src=%llx dst=%llx size=%llx\n",
                     static_cast<unsigned long long>(source),
                     static_cast<unsigned long long>(destination),
                     static_cast<unsigned long long>(size));
        std::memset(buffer.data(), 0, buffer.size());
    }
    if (!Write(destination, buffer.data(), buffer.size())) {
        std::fprintf(stderr,
                     "ISOMR_COPY_FAIL src=%llx dst=%llx size=%llx\n",
                     static_cast<unsigned long long>(source),
                     static_cast<unsigned long long>(destination),
                     static_cast<unsigned long long>(size));
        SetError(error, "guest memory copy touches an invalid range");
        return false;
    }
    return true;
}

bool GuestAddressSpace::Fill(std::uint64_t destination, std::uint8_t value,
                             std::uint64_t size, std::string* error) {
    std::size_t checked = 0;
    if (!CheckedSize(size, &checked, error)) return false;
    if (checked == 0) return true;
    std::vector<std::uint8_t> buffer;
    try {
        buffer.resize(checked, value);
    } catch (const std::bad_alloc&) {
        SetError(error, "guest memory fill buffer allocation failed");
        return false;
    }
    if (!Write(destination, buffer.data(), buffer.size())) {
        SetError(error, "guest memory fill touches an invalid range");
        return false;
    }
    return true;
}

bool GuestAddressSpace::Compare(std::uint64_t left, std::uint64_t right,
                                std::uint64_t size, int* result,
                                std::string* error) const {
    std::size_t checked = 0;
    if (result == nullptr || !CheckedSize(size, &checked, error)) return false;
    constexpr std::size_t kChunkSize = 4096;
    std::array<std::uint8_t, kChunkSize> left_chunk{};
    std::array<std::uint8_t, kChunkSize> right_chunk{};
    std::size_t offset = 0;
    while (offset < checked) {
        const std::size_t amount =
            std::min(left_chunk.size(), checked - offset);
        std::uint64_t left_address = 0;
        std::uint64_t right_address = 0;
        if (!AddAddress(left, offset, &left_address) ||
            !AddAddress(right, offset, &right_address) ||
            !Read(left_address, left_chunk.data(), amount) ||
            !Read(right_address, right_chunk.data(), amount)) {
            SetError(error, "guest memory compare touches an invalid range");
            return false;
        }
        for (std::size_t index = 0; index < amount; ++index) {
            if (left_chunk[index] == right_chunk[index]) continue;
            *result = left_chunk[index] < right_chunk[index] ? -1 : 1;
            return true;
        }
        offset += amount;
    }
    *result = 0;
    return true;
}

bool GuestAddressSpace::FindByte(std::uint64_t address, std::uint8_t value,
                                 std::uint64_t size, std::uint64_t* result,
                                 std::string* error) const {
    std::size_t checked = 0;
    if (result == nullptr || !CheckedSize(size, &checked, error)) return false;
    *result = 0;
    constexpr std::size_t kChunkSize = 4096;
    std::array<std::uint8_t, kChunkSize> chunk{};
    std::size_t offset = 0;
    while (offset < checked) {
        const std::size_t amount = std::min(chunk.size(), checked - offset);
        std::uint64_t chunk_address = 0;
        if (!AddAddress(address, offset, &chunk_address) ||
            !Read(chunk_address, chunk.data(), amount)) {
            SetError(error, "guest byte search touches an invalid range");
            return false;
        }
        const auto found =
            std::find(chunk.begin(), chunk.begin() + amount, value);
        if (found != chunk.begin() + amount) {
            const std::size_t index =
                static_cast<std::size_t>(found - chunk.begin());
            if (!AddAddress(chunk_address, index, result)) {
                SetError(error, "guest byte search address overflowed");
                return false;
            }
            return true;
        }
        offset += amount;
    }
    return true;
}

bool GuestAddressSpace::ScanStringLength(std::uint64_t address,
                                          std::uint64_t bound,
                                          std::uint64_t* result) const {
    if (result == nullptr) return false;
    // 64 MiB hard bound so SIZE_MAX / oversized callers measure a real string
    // rather than inheriting the much smaller legacy clamp.
    constexpr std::uint64_t kHardBound = 64ull * 1024 * 1024;
    if (bound > kHardBound) bound = kHardBound;
    *result = 0;
    if (address == 0 || bound == 0) return true;
    constexpr std::size_t kChunkSize = 4096;
    std::array<std::uint8_t, kChunkSize> chunk{};
    std::uint64_t offset = 0;
    while (offset < bound) {
        // Bound each chunk to its containing page so a bulk Read failure
        // cleanly signals a sub-page accessibility edge rather than a
        // mid-chunk gap.
        const std::uint64_t page_remaining =
            4096ull - ((address + offset) & 4095ull);
        std::uint64_t want64 = bound - offset;
        if (want64 > page_remaining) want64 = page_remaining;
        if (want64 > kChunkSize) want64 = kChunkSize;
        const std::size_t want = static_cast<std::size_t>(want64);
        std::uint64_t chunk_address = 0;
        if (!AddAddress(address, static_cast<std::size_t>(offset),
                        &chunk_address)) {
            *result = offset;
            return true;
        }
        if (Read(chunk_address, chunk.data(), want)) {
            const auto found =
                std::find(chunk.begin(), chunk.begin() + want,
                          std::uint8_t{0});
            if (found != chunk.begin() + want) {
                *result = offset +
                          static_cast<std::size_t>(found - chunk.begin());
                return true;
            }
            offset += want;
            continue;
        }
        // Byte-wise fallback at the accessibility edge: locate the readable
        // prefix (or an earlier NUL) so the caller gets the real length.
        for (std::size_t k = 0; k < want; ++k) {
            std::uint8_t value = 0;
            std::uint64_t byte_address = 0;
            if (!AddAddress(chunk_address, k, &byte_address) ||
                !Read(byte_address, &value, sizeof(value))) {
                std::string preview;
                const std::size_t scanned =
                    static_cast<std::size_t>(offset + k);
                for (std::size_t j = 0; j < scanned && j < 64; ++j) {
                    std::uint8_t byte = 0;
                    std::uint64_t prev_address = 0;
                    char text[8];
                    if (AddAddress(address, j, &prev_address) &&
                        Read(prev_address, &byte, sizeof(byte))) {
                        std::snprintf(text, sizeof(text), "%02x ", byte);
                    } else {
                        std::snprintf(text, sizeof(text), "?? ");
                    }
                    preview += text;
                }
                std::fprintf(
                    stderr,
                    "ISOMR_STRLEN_FAIL addr=%llx scanned=%llu bytes=[%s]\n",
                    static_cast<unsigned long long>(address),
                    static_cast<unsigned long long>(offset + k),
                    preview.c_str());
                *result = offset + k;
                // A start address with no readable byte at all is an invalid
                // pointer, not an empty string: report failure so strict
                // callers surface "unreadable" while tolerant callers still
                // get the measured prefix (0) via *result.
                return offset + k != 0;
            }
            if (value == 0) {
                *result = offset + k;
                return true;
            }
        }
        offset += want;
    }
    *result = bound;
    return true;
}

bool GuestAddressSpace::StringLength(std::uint64_t address,
                                     std::uint64_t* result,
                                     std::string* error) const {
    if (result == nullptr || maximum_string_ == 0) {
        SetError(error, "guest string length configuration is invalid");
        return false;
    }
    if (address == 0) {
        SetError(error, "guest string pointer is null");
        return false;
    }
    std::uint64_t length = 0;
    // Tolerant measure: a wild or truncated string yields its readable prefix
    // (possibly 0) rather than a hard import failure. Guests routinely pass
    // garbage pointers to strlen-family imports during init, and real bionic
    // strlen would fault in the guest — not abort the host dispatch. The
    // unreadable-start diagnostic still logs inside ScanStringLength.
    (void)ScanStringLength(address,
                           std::numeric_limits<std::uint64_t>::max(),
                           &length);
    *result = length;
    return true;
}

bool GuestAddressSpace::BoundedStringLength(std::uint64_t address,
                                            std::uint64_t maximum,
                                            std::uint64_t* result,
                                            std::string* error) const {
    if (result == nullptr) {
        SetError(error, "guest bounded string length result is null");
        return false;
    }
    std::uint64_t length = 0;
    if (!ScanStringLength(address, maximum, &length)) {
        SetError(error,
                 "guest bounded string touches an invalid range");
        return false;
    }
    *result = length;
    return true;
}

bool GuestAddressSpace::StringCompare(std::uint64_t left,
                                      std::uint64_t right, int* result,
                                      std::string* error) const {
    if (result == nullptr || maximum_string_ == 0) {
        SetError(error, "guest string compare configuration is invalid");
        return false;
    }
    for (std::size_t index = 0; index < maximum_string_; ++index) {
        std::uint8_t left_value = 0;
        std::uint8_t right_value = 0;
        std::uint64_t left_address = 0;
        std::uint64_t right_address = 0;
        if (!AddAddress(left, index, &left_address) ||
            !AddAddress(right, index, &right_address) ||
            !Read(left_address, &left_value, sizeof(left_value)) ||
            !Read(right_address, &right_value, sizeof(right_value))) {
            SetError(error, "guest string compare touches an invalid range");
            return false;
        }
        if (left_value != right_value) {
            *result = left_value < right_value ? -1 : 1;
            return true;
        }
        if (left_value == 0) {
            *result = 0;
            return true;
        }
    }
    SetError(error, "guest string compare exceeds its length bound");
    return false;
}

bool GuestAddressSpace::BoundedStringCompare(std::uint64_t left,
                                             std::uint64_t right,
                                             std::uint64_t maximum,
                                             int* result,
                                             std::string* error) const {
    if (result == nullptr || maximum > maximum_string_) {
        SetError(error, "guest bounded string compare exceeds its limit");
        return false;
    }
    for (std::size_t index = 0;
         index < static_cast<std::size_t>(maximum); ++index) {
        std::uint8_t left_value = 0;
        std::uint8_t right_value = 0;
        std::uint64_t left_address = 0;
        std::uint64_t right_address = 0;
        if (!AddAddress(left, index, &left_address) ||
            !AddAddress(right, index, &right_address) ||
            !Read(left_address, &left_value, sizeof(left_value)) ||
            !Read(right_address, &right_value, sizeof(right_value))) {
            SetError(error,
                     "guest bounded string compare touches an invalid range");
            return false;
        }
        if (left_value != right_value) {
            *result = left_value < right_value ? -1 : 1;
            return true;
        }
        if (left_value == 0) {
            *result = 0;
            return true;
        }
    }
    *result = 0;
    return true;
}

}  // namespace isomr
