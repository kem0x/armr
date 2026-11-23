#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "elf64_loader.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <memory>
#include <sstream>
#include <utility>

namespace isomr {
namespace {

constexpr std::uint16_t kElfTypeSharedObject = 3;
constexpr std::uint16_t kElfMachineAarch64 = 183;
constexpr std::uint32_t kElfVersionCurrent = 1;
constexpr std::uint32_t kProgramLoad = 1;
constexpr std::uint32_t kProgramDynamic = 2;
constexpr std::int64_t kDynamicNull = 0;
constexpr std::int64_t kDynamicNeeded = 1;
constexpr std::int64_t kDynamicHash = 4;
constexpr std::int64_t kDynamicGnuHash = 0x6ffffef5;
constexpr std::int64_t kDynamicStringTable = 5;
constexpr std::int64_t kDynamicSymbolTable = 6;
constexpr std::int64_t kDynamicRela = 7;
constexpr std::int64_t kDynamicRelaSize = 8;
constexpr std::int64_t kDynamicRelaEntry = 9;
constexpr std::int64_t kDynamicStringSize = 10;
constexpr std::int64_t kDynamicSymbolEntry = 11;
constexpr std::int64_t kDynamicPltRelaSize = 2;
constexpr std::int64_t kDynamicPltRelType = 20;
constexpr std::int64_t kDynamicJumpRelocations = 23;
constexpr std::int64_t kDynamicInitArray = 25;
constexpr std::int64_t kDynamicInitArraySize = 27;
constexpr std::uint64_t kDynamicRelaType = 7;
// Packed relative relocations (ELF standard DT_RELR): a bitmap stream of
// addresses that only need the load bias added — the dominant relative-reloc
// form in modern (lld-linked) Android libraries.
constexpr std::int64_t kDynamicRelr = 0x6fffe000;
constexpr std::int64_t kDynamicRelrSize = 0x6fffe001;
constexpr std::int64_t kDynamicRelrEntry = 0x6fffe003;
// Android APS2 packed relocations (DT_ANDROID_RELA): an LEB128-encoded group
// stream carrying symbol + addend relocs. Present alongside DT_RELR on the
// same libraries.
constexpr std::int64_t kDynamicAndroidRela = 0x60000011;
constexpr std::int64_t kDynamicAndroidRelaSize = 0x60000012;
constexpr std::uint32_t kRelocationAbsolute64 = 257;
constexpr std::uint32_t kRelocationGlobalData = 1025;
constexpr std::uint32_t kRelocationJumpSlot = 1026;
constexpr std::uint32_t kRelocationRelative = 1027;
constexpr std::uint8_t kSymbolBindingGlobal = 1;
constexpr std::uint8_t kSymbolBindingWeak = 2;
constexpr std::uint8_t kSymbolTypeTls = 6;
constexpr std::uint16_t kSectionIndexAbsolute = 0xfff1;
constexpr std::uint16_t kSectionIndexCommon = 0xfff2;
constexpr std::uint64_t kMaximumFileSize = 1ULL << 30;
constexpr std::uint64_t kMaximumImageSpan = 2ULL << 30;
constexpr std::size_t kMaximumDynamicSymbols = 1U << 20;

#pragma pack(push, 1)
struct Elf64Header {
    std::uint8_t identification[16];
    std::uint16_t type;
    std::uint16_t machine;
    std::uint32_t version;
    std::uint64_t entry;
    std::uint64_t program_header_offset;
    std::uint64_t section_header_offset;
    std::uint32_t flags;
    std::uint16_t header_size;
    std::uint16_t program_header_entry_size;
    std::uint16_t program_header_count;
    std::uint16_t section_header_entry_size;
    std::uint16_t section_header_count;
    std::uint16_t section_name_index;
};

struct Elf64Dynamic {
    std::int64_t tag;
    std::uint64_t value;
};

struct Elf64Symbol {
    std::uint32_t name;
    std::uint8_t information;
    std::uint8_t other;
    std::uint16_t section_index;
    std::uint64_t value;
    std::uint64_t size;
};

struct Elf64Rela {
    std::uint64_t offset;
    std::uint64_t information;
    std::int64_t addend;
};
#pragma pack(pop)

static_assert(sizeof(Elf64Header) == 64);
static_assert(sizeof(Elf64Dynamic) == 16);
static_assert(sizeof(Elf64Symbol) == 24);
static_assert(sizeof(Elf64Rela) == 24);

bool AddOverflows(std::uint64_t left, std::uint64_t right,
                  std::uint64_t* result) {
    if (right > std::numeric_limits<std::uint64_t>::max() - left) return true;
    *result = left + right;
    return false;
}

bool MultiplyOverflows(std::uint64_t left, std::uint64_t right,
                       std::uint64_t* result) {
    if (left != 0 && right > std::numeric_limits<std::uint64_t>::max() / left)
        return true;
    *result = left * right;
    return false;
}

bool AddSigned(std::uint64_t base, std::int64_t addend,
               std::uint64_t* result) {
    if (addend >= 0)
        return !AddOverflows(base, static_cast<std::uint64_t>(addend), result);
    const std::uint64_t magnitude =
        static_cast<std::uint64_t>(-(addend + 1)) + 1;
    if (magnitude > base) return false;
    *result = base - magnitude;
    return true;
}

template <typename T>
bool ReadObject(const std::vector<std::uint8_t>& bytes, std::uint64_t offset,
                T* value) {
    if (offset > bytes.size() || sizeof(T) > bytes.size() - offset) return false;
    std::memcpy(value, bytes.data() + static_cast<std::size_t>(offset), sizeof(T));
    return true;
}

std::string WindowsError(const char* operation) {
    std::ostringstream message;
    message << operation << " failed with Windows error " << GetLastError();
    return message.str();
}

bool ReadWindowsFile(const std::wstring& path, std::vector<std::uint8_t>* bytes,
                     std::string* error) {
    HANDLE raw = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (raw == INVALID_HANDLE_VALUE) {
        *error = WindowsError("CreateFileW");
        return false;
    }
    const auto close_handle = [](void* handle) {
        CloseHandle(static_cast<HANDLE>(handle));
    };
    std::unique_ptr<void, decltype(close_handle)> file(raw, close_handle);

    LARGE_INTEGER size{};
    if (!GetFileSizeEx(raw, &size)) {
        *error = WindowsError("GetFileSizeEx");
        return false;
    }
    if (size.QuadPart < static_cast<LONGLONG>(sizeof(Elf64Header)) ||
        static_cast<std::uint64_t>(size.QuadPart) > kMaximumFileSize) {
        *error = "ELF file size is outside the supported range";
        return false;
    }

    bytes->resize(static_cast<std::size_t>(size.QuadPart));
    std::size_t cursor = 0;
    while (cursor < bytes->size()) {
        const DWORD request = static_cast<DWORD>(std::min<std::size_t>(
            bytes->size() - cursor, std::numeric_limits<DWORD>::max()));
        DWORD received = 0;
        if (!ReadFile(raw, bytes->data() + cursor, request, &received, nullptr)) {
            *error = WindowsError("ReadFile");
            return false;
        }
        if (received == 0) {
            *error = "ReadFile reached an unexpected end of file";
            return false;
        }
        cursor += received;
    }
    return true;
}

}  // namespace

bool Elf64Image::RangeOffset(std::uint64_t guest_address, std::size_t size,
                             std::uint32_t required_flags,
                             std::size_t* offset) const {
    if (guest_address < guest_base_ || size > memory_.size()) return false;
    const std::uint64_t relative = guest_address - guest_base_;
    if (relative > memory_.size() - size) return false;

    for (const Elf64Segment& segment : segments_) {
        const std::uint64_t segment_offset = segment.virtual_address - low_vaddr_;
        std::uint64_t segment_start = 0;
        if (AddOverflows(guest_base_, segment_offset, &segment_start)) continue;
        std::uint64_t segment_end = 0;
        std::uint64_t range_end = 0;
        if (AddOverflows(segment_start, segment.memory_size, &segment_end) ||
            AddOverflows(guest_address, size, &range_end))
            continue;
        if (guest_address >= segment_start && range_end <= segment_end &&
            (segment.flags & required_flags) == required_flags) {
            *offset = static_cast<std::size_t>(relative);
            return true;
        }
    }
    return false;
}

std::optional<std::uint64_t> Elf64Image::GuestAddress(
    std::uint64_t virtual_address) const {
    if (virtual_address < low_vaddr_) return std::nullopt;
    const std::uint64_t offset = virtual_address - low_vaddr_;
    if (offset >= memory_.size()) return std::nullopt;
    std::uint64_t address = 0;
    if (AddOverflows(guest_base_, offset, &address)) return std::nullopt;
    std::size_t ignored = 0;
    if (!RangeOffset(address, 1, 0, &ignored)) return std::nullopt;
    return address;
}

std::optional<std::uint64_t> Elf64Image::FindExport(
    std::string_view name) const {
    const auto found = exports_.find(std::string(name));
    if (found == exports_.end()) return std::nullopt;
    return found->second;
}

bool Elf64Image::Read(std::uint64_t guest_address, void* destination,
                      std::size_t size) const {
    std::size_t offset = 0;
    if (!RangeOffset(guest_address, size, kElfSegmentRead, &offset)) return false;
    std::memcpy(destination, memory_.data() + offset, size);
    return true;
}

bool Elf64Image::ReadCode(std::uint64_t guest_address, void* destination,
                          std::size_t size) const {
    std::size_t offset = 0;
    if (!RangeOffset(guest_address, size, kElfSegmentExecute, &offset))
        return false;
    std::memcpy(destination, memory_.data() + offset, size);
    return true;
}

bool Elf64Image::PatchCode(
    std::uint64_t guest_address,
    const std::array<std::uint32_t, 2>& expected,
    const std::array<std::uint32_t, 2>& replacement,
    std::string* error) {
    std::size_t offset = 0;
    if (!RangeOffset(guest_address, sizeof(expected), kElfSegmentExecute,
                     &offset)) {
        if (error != nullptr)
            *error = "ELF code patch target is not an executable range";
        return false;
    }
    if (std::memcmp(memory_.data() + offset, expected.data(),
                    sizeof(expected)) != 0) {
        if (error != nullptr)
            *error = "ELF code patch signature mismatch";
        return false;
    }
    std::memcpy(memory_.data() + offset, replacement.data(),
                sizeof(replacement));
    return true;
}

bool Elf64Image::IsWritableRange(std::uint64_t guest_address,
                                 std::size_t size) const {
    std::size_t offset = 0;
    return RangeOffset(guest_address, size, kElfSegmentWrite, &offset);
}

bool Elf64Image::Write(std::uint64_t guest_address, const void* source,
                       std::size_t size) {
    std::size_t offset = 0;
    if (!RangeOffset(guest_address, size, kElfSegmentWrite, &offset)) return false;
    std::memcpy(memory_.data() + offset, source, size);
    return true;
}

class Elf64LoaderImpl {
public:
    Elf64LoaderImpl(const std::vector<std::uint8_t>& file,
                    std::uint64_t guest_base,
                    const Elf64ImportResolver& resolver,
                    const Elf64DependencyPreparer& prepare)
        : file_(file), guest_base_(guest_base), resolver_(resolver),
          prepare_(prepare) {}

    Elf64LoadResult Load() {
        if (!ReadObject(file_, 0, &header_)) return Fail("truncated ELF header");
        if (std::memcmp(header_.identification, "\x7f" "ELF", 4) != 0 ||
            header_.identification[4] != 2 || header_.identification[5] != 1 ||
            header_.identification[6] != kElfVersionCurrent)
            return Fail("file is not a little-endian ELF64 image");
        if (header_.type != kElfTypeSharedObject)
            return Fail("ELF image is not ET_DYN");
        if (header_.machine != kElfMachineAarch64)
            return Fail("ELF image is not AArch64");
        if (header_.version != kElfVersionCurrent ||
            header_.header_size != sizeof(Elf64Header) ||
            header_.program_header_entry_size != sizeof(Elf64ProgramHeader) ||
            header_.program_header_count == 0)
            return Fail("ELF header fields are invalid");

        std::uint64_t program_bytes = 0;
        std::uint64_t program_end = 0;
        if (MultiplyOverflows(header_.program_header_count,
                              sizeof(Elf64ProgramHeader), &program_bytes) ||
            AddOverflows(header_.program_header_offset, program_bytes,
                         &program_end) ||
            program_end > file_.size())
            return Fail("ELF program-header table is out of bounds");

        std::vector<Elf64ProgramHeader> programs(header_.program_header_count);
        std::uint64_t low = std::numeric_limits<std::uint64_t>::max();
        std::uint64_t high = 0;
        for (std::size_t index = 0; index < programs.size(); ++index) {
            const std::uint64_t offset =
                header_.program_header_offset + index * sizeof(Elf64ProgramHeader);
            if (!ReadObject(file_, offset, &programs[index]))
                return Fail("ELF program header is truncated");
            const Elf64ProgramHeader& program = programs[index];
            if (program.type != kProgramLoad) continue;
            if (program.file_size > program.memory_size)
                return Fail("PT_LOAD file size exceeds memory size");
            std::uint64_t file_end = 0;
            std::uint64_t memory_end = 0;
            if (AddOverflows(program.offset, program.file_size, &file_end) ||
                file_end > file_.size() ||
                AddOverflows(program.virtual_address, program.memory_size,
                             &memory_end))
                return Fail("PT_LOAD range is out of bounds");
            if (program.alignment > 1) {
                const bool power_of_two =
                    (program.alignment & (program.alignment - 1)) == 0;
                if (!power_of_two ||
                    program.offset % program.alignment !=
                        program.virtual_address % program.alignment)
                    return Fail("PT_LOAD alignment is invalid");
            }
            low = std::min(low, program.virtual_address);
            high = std::max(high, memory_end);
        }
        if (low == std::numeric_limits<std::uint64_t>::max() || high <= low)
            return Fail("ELF image has no non-empty PT_LOAD range");
        if (high - low > kMaximumImageSpan)
            return Fail("ELF mapped span exceeds the supported limit");
        if (guest_base_ < low || (guest_base_ & 0xfffU) != 0)
            return Fail("guest mapping base is invalid");

        image_.guest_base_ = guest_base_;
        image_.low_vaddr_ = low;
        image_.memory_.assign(static_cast<std::size_t>(high - low), 0);
        image_.program_headers_ = programs;
        for (const Elf64ProgramHeader& program : programs) {
            if (program.type != kProgramLoad || program.memory_size == 0) continue;
            const std::size_t mapped_offset =
                static_cast<std::size_t>(program.virtual_address - low);
            std::memcpy(image_.memory_.data() + mapped_offset,
                        file_.data() + static_cast<std::size_t>(program.offset),
                        static_cast<std::size_t>(program.file_size));
            image_.segments_.push_back({program.virtual_address,
                                        program.memory_size, program.flags});
        }
        if (!ValidateSegmentOverlap()) return Fail(error_);

        const Elf64ProgramHeader* dynamic = nullptr;
        for (const Elf64ProgramHeader& program : programs) {
            if (program.type == kProgramDynamic) {
                if (dynamic != nullptr) return Fail("multiple PT_DYNAMIC tables");
                dynamic = &program;
            }
        }
        if (dynamic == nullptr || dynamic->file_size < sizeof(Elf64Dynamic))
            return Fail("ELF image has no usable PT_DYNAMIC table");
        if (!ParseDynamic(*dynamic)) return Fail(error_);
        if (!image_.needed_.empty()) {
            if (!prepare_)
                return Fail("ELF image has DT_NEEDED entries but no dependency loader");
            std::string dependency_error;
            if (!prepare_(image_.needed_, &dependency_error))
                return Fail(dependency_error.empty()
                                ? "ELF dependency preparation failed"
                                : std::move(dependency_error));
        }
        if (!ApplyRelocations(dynamic_.rela, dynamic_.rela_size) ||
            !ApplyRelocations(dynamic_.jump_relocations,
                              dynamic_.jump_relocation_size))
            return Fail(error_);
        // Packed forms must run before CollectInitializers: the init array's
        // function pointers are relocated through RELR/Android-packed relocs
        // on modern (lld-linked) libraries, not through classic DT_RELA.
        if (!ApplyRelr() || !ApplyAndroidPackedRelocations())
            return Fail(error_);
        if (!CollectInitializers()) return Fail(error_);

        const auto entry = image_.GuestAddress(header_.entry);
        std::size_t entry_offset = 0;
        // Shared libraries commonly have e_entry == 0: initialization runs
        // through initializers instead, so only validate a real entry point.
        if (header_.entry != 0) {
            if (!entry || !image_.RangeOffset(*entry, sizeof(std::uint32_t),
                                              kElfSegmentExecute,
                                              &entry_offset))
                return Fail("ELF entry point is not executable");
            image_.entry_point_ = *entry;
        }
        return {std::move(image_), {}};
    }

private:
    struct DynamicInfo {
        std::uint64_t hash = 0;
        std::uint64_t gnu_hash = 0;
        std::uint64_t string_table = 0;
        std::uint64_t string_size = 0;
        std::uint64_t symbol_table = 0;
        std::uint64_t symbol_entry_size = 0;
        std::uint64_t rela = 0;
        std::uint64_t rela_size = 0;
        std::uint64_t rela_entry_size = 0;
        std::uint64_t jump_relocations = 0;
        std::uint64_t jump_relocation_size = 0;
        std::uint64_t plt_relocation_type = 0;
        std::uint64_t relr = 0;
        std::uint64_t relr_size = 0;
        std::uint64_t relr_entry_size = 0;
        std::uint64_t android_rela = 0;
        std::uint64_t android_rela_size = 0;
        std::uint64_t init_array = 0;
        std::uint64_t init_array_size = 0;
        std::size_t symbol_count = 0;
        std::vector<std::uint64_t> needed;
    };

    Elf64LoadResult Fail(std::string message) {
        return {std::nullopt, std::move(message)};
    }

    bool SetError(std::string message) {
        error_ = std::move(message);
        return false;
    }

    bool ValidateSegmentOverlap() {
        for (std::size_t left = 0; left < image_.segments_.size(); ++left) {
            const Elf64Segment& a = image_.segments_[left];
            for (std::size_t right = left + 1;
                 right < image_.segments_.size(); ++right) {
                const Elf64Segment& b = image_.segments_[right];
                const std::uint64_t a_end = a.virtual_address + a.memory_size;
                const std::uint64_t b_end = b.virtual_address + b.memory_size;
                if (a.virtual_address < b_end && b.virtual_address < a_end)
                    return SetError("overlapping PT_LOAD ranges are unsupported");
            }
        }
        return true;
    }

    bool VirtualOffset(std::uint64_t virtual_address, std::size_t size,
                       std::uint32_t required_flags, std::size_t* offset) {
        const auto guest = image_.GuestAddress(virtual_address);
        if (!guest ||
            !image_.RangeOffset(*guest, size, required_flags, offset))
            return false;
        return true;
    }

    template <typename T>
    bool ReadVirtual(std::uint64_t virtual_address, T* value) {
        std::size_t offset = 0;
        if (!VirtualOffset(virtual_address, sizeof(T), kElfSegmentRead, &offset))
            return false;
        std::memcpy(value, image_.memory_.data() + offset, sizeof(T));
        return true;
    }

    bool ReadSysvSymbolCount() {
        std::array<std::uint32_t, 2> hash_header{};
        std::size_t hash_offset = 0;
        if (!VirtualOffset(dynamic_.hash, sizeof(hash_header),
                           kElfSegmentRead, &hash_offset))
            return SetError("DT_HASH header is outside readable memory");
        std::memcpy(hash_header.data(), image_.memory_.data() + hash_offset,
                    sizeof(hash_header));
        const std::uint64_t hash_words =
            2ULL + hash_header[0] + hash_header[1];
        std::uint64_t hash_bytes = 0;
        if (MultiplyOverflows(hash_words, sizeof(std::uint32_t), &hash_bytes) ||
            hash_bytes > std::numeric_limits<std::size_t>::max() ||
            !VirtualOffset(dynamic_.hash, static_cast<std::size_t>(hash_bytes),
                           kElfSegmentRead, &hash_offset))
            return SetError("DT_HASH table is out of bounds");
        if (hash_header[1] == 0)
            return SetError("DT_HASH reports an empty symbol table");
        if (hash_header[1] > kMaximumDynamicSymbols)
            return SetError("DT_HASH exceeds the supported symbol limit");
        dynamic_.symbol_count = hash_header[1];
        return true;
    }

    bool ReadGnuSymbolCount() {
        std::array<std::uint32_t, 4> header{};
        if (!ReadVirtual(dynamic_.gnu_hash, &header))
            return SetError("DT_GNU_HASH header is outside readable memory");
        const std::uint64_t bucket_count = header[0];
        const std::uint64_t symbol_offset = header[1];
        const std::uint64_t bloom_count = header[2];
        if (bucket_count == 0 || bloom_count == 0)
            return SetError("DT_GNU_HASH header is invalid");
        if (symbol_offset == 0 || symbol_offset > kMaximumDynamicSymbols)
            return SetError(
                "DT_GNU_HASH symbol offset exceeds the supported limit");

        std::uint64_t bloom_bytes = 0;
        std::uint64_t bucket_bytes = 0;
        std::uint64_t bloom_address = 0;
        std::uint64_t bucket_address = 0;
        std::uint64_t chain_address = 0;
        if (MultiplyOverflows(bloom_count, sizeof(std::uint64_t),
                              &bloom_bytes) ||
            MultiplyOverflows(bucket_count, sizeof(std::uint32_t),
                              &bucket_bytes) ||
            AddOverflows(dynamic_.gnu_hash, sizeof(header), &bloom_address) ||
            AddOverflows(bloom_address, bloom_bytes, &bucket_address) ||
            AddOverflows(bucket_address, bucket_bytes, &chain_address) ||
            bloom_bytes > std::numeric_limits<std::size_t>::max() ||
            bucket_bytes > std::numeric_limits<std::size_t>::max())
            return SetError("DT_GNU_HASH table size overflows");

        std::size_t ignored = 0;
        if (!VirtualOffset(bloom_address,
                           static_cast<std::size_t>(bloom_bytes),
                           kElfSegmentRead, &ignored) ||
            !VirtualOffset(bucket_address,
                           static_cast<std::size_t>(bucket_bytes),
                           kElfSegmentRead, &ignored))
            return SetError("DT_GNU_HASH table is out of bounds");

        std::uint32_t highest_bucket = 0;
        for (std::uint64_t index = 0; index < bucket_count; ++index) {
            std::uint32_t bucket = 0;
            if (!ReadVirtual(bucket_address +
                                 index * sizeof(std::uint32_t),
                             &bucket))
                return SetError("DT_GNU_HASH bucket is unreadable");
            if (bucket != 0 && bucket < symbol_offset)
                return SetError("DT_GNU_HASH bucket precedes symbol offset");
            if (bucket > kMaximumDynamicSymbols)
                return SetError(
                    "DT_GNU_HASH bucket exceeds the supported symbol limit");
            highest_bucket = std::max(highest_bucket, bucket);
        }

        if (highest_bucket == 0) {
            dynamic_.symbol_count = static_cast<std::size_t>(symbol_offset);
            return true;
        }

        const std::uint64_t first_chain = highest_bucket - symbol_offset;
        for (std::uint64_t symbol = highest_bucket;
             symbol < kMaximumDynamicSymbols; ++symbol) {
            const std::uint64_t chain_index =
                first_chain + (symbol - highest_bucket);
            std::uint64_t chain_offset = 0;
            std::uint64_t chain_entry_address = 0;
            if (MultiplyOverflows(chain_index, sizeof(std::uint32_t),
                                  &chain_offset) ||
                AddOverflows(chain_address, chain_offset,
                             &chain_entry_address))
                return SetError("DT_GNU_HASH chain address overflows");
            std::uint32_t chain = 0;
            if (!ReadVirtual(chain_entry_address, &chain))
                return SetError("DT_GNU_HASH chain is unterminated");
            if ((chain & 1U) != 0) {
                dynamic_.symbol_count = static_cast<std::size_t>(symbol + 1);
                return true;
            }
        }
        return SetError("DT_GNU_HASH chain exceeds the supported symbol limit");
    }

    bool ParseDynamic(const Elf64ProgramHeader& program) {
        if (program.file_size > program.memory_size ||
            program.file_size % sizeof(Elf64Dynamic) != 0)
            return SetError("PT_DYNAMIC size is invalid");
        const std::size_t count =
            static_cast<std::size_t>(program.file_size / sizeof(Elf64Dynamic));
        bool terminated = false;
        for (std::size_t index = 0; index < count; ++index) {
            Elf64Dynamic entry{};
            if (!ReadVirtual(program.virtual_address +
                                 index * sizeof(Elf64Dynamic),
                             &entry))
                return SetError("PT_DYNAMIC entry is outside readable memory");
            if (entry.tag == kDynamicNull) {
                terminated = true;
                break;
            }
            switch (entry.tag) {
                case kDynamicHash: dynamic_.hash = entry.value; break;
                case kDynamicGnuHash:
                    dynamic_.gnu_hash = entry.value;
                    break;
                case kDynamicNeeded:
                    dynamic_.needed.push_back(entry.value);
                    break;
                case kDynamicStringTable:
                    dynamic_.string_table = entry.value;
                    break;
                case kDynamicStringSize:
                    dynamic_.string_size = entry.value;
                    break;
                case kDynamicSymbolTable:
                    dynamic_.symbol_table = entry.value;
                    break;
                case kDynamicSymbolEntry:
                    dynamic_.symbol_entry_size = entry.value;
                    break;
                case kDynamicRela: dynamic_.rela = entry.value; break;
                case kDynamicRelaSize: dynamic_.rela_size = entry.value; break;
                case kDynamicRelaEntry:
                    dynamic_.rela_entry_size = entry.value;
                    break;
                case kDynamicJumpRelocations:
                    dynamic_.jump_relocations = entry.value;
                    break;
                case kDynamicPltRelaSize:
                    dynamic_.jump_relocation_size = entry.value;
                    break;
                case kDynamicPltRelType:
                    dynamic_.plt_relocation_type = entry.value;
                    break;
                case kDynamicRelr: dynamic_.relr = entry.value; break;
                case kDynamicRelrSize: dynamic_.relr_size = entry.value; break;
                case kDynamicRelrEntry:
                    dynamic_.relr_entry_size = entry.value;
                    break;
                case kDynamicAndroidRela:
                    dynamic_.android_rela = entry.value;
                    break;
                case kDynamicAndroidRelaSize:
                    dynamic_.android_rela_size = entry.value;
                    break;
                case kDynamicInitArray:
                    dynamic_.init_array = entry.value;
                    break;
                case kDynamicInitArraySize:
                    dynamic_.init_array_size = entry.value;
                    break;
                default: break;
            }
        }
        if (!terminated) return SetError("PT_DYNAMIC is not terminated");
        if ((dynamic_.hash == 0 && dynamic_.gnu_hash == 0) ||
            dynamic_.string_table == 0 ||
            dynamic_.string_size == 0 || dynamic_.symbol_table == 0 ||
            dynamic_.symbol_entry_size != sizeof(Elf64Symbol))
            return SetError("required dynamic symbol metadata is missing");
        // DT_RELAENT defaults to sizeof(Elf64Rela) when omitted; reject only
        // when it is present with a nonstandard entry size.
        if ((dynamic_.rela_size != 0 ||
             dynamic_.jump_relocation_size != 0) &&
            dynamic_.rela_entry_size != 0 &&
            dynamic_.rela_entry_size != sizeof(Elf64Rela))
            return SetError("DT_RELAENT is invalid");
        if (dynamic_.jump_relocation_size != 0 &&
            dynamic_.plt_relocation_type != kDynamicRelaType)
            return SetError("PLT relocations are not RELA entries");
        // DT_RELRENT defaults to sizeof(uint64_t) when omitted.
        if (dynamic_.relr_size != 0 &&
            dynamic_.relr_entry_size != 0 &&
            dynamic_.relr_entry_size != sizeof(std::uint64_t))
            return SetError("DT_RELRENT is invalid");

        if (dynamic_.hash != 0) {
            if (!ReadSysvSymbolCount()) return false;
        } else if (!ReadGnuSymbolCount()) {
            return false;
        }

        std::size_t ignored = 0;
        if (!VirtualOffset(dynamic_.string_table,
                           static_cast<std::size_t>(dynamic_.string_size),
                           kElfSegmentRead, &ignored))
            return SetError("DT_STRTAB is outside readable memory");
        std::uint64_t symbol_bytes = 0;
        if (MultiplyOverflows(dynamic_.symbol_count, sizeof(Elf64Symbol),
                              &symbol_bytes) ||
            symbol_bytes > std::numeric_limits<std::size_t>::max() ||
            !VirtualOffset(dynamic_.symbol_table,
                           static_cast<std::size_t>(symbol_bytes),
                           kElfSegmentRead, &ignored))
            return SetError("DT_SYMTAB is outside readable memory");

        for (const std::uint64_t needed_offset : dynamic_.needed) {
            std::string_view name;
            if (!DynamicString(needed_offset, &name)) return false;
            if (name.empty()) return SetError("DT_NEEDED entry has an empty name");
            image_.needed_.emplace_back(name);
        }
        for (std::size_t index = 0; index < dynamic_.symbol_count; ++index) {
            Elf64Symbol symbol{};
            if (!ReadVirtual(dynamic_.symbol_table +
                                 static_cast<std::uint64_t>(index) *
                                     sizeof(Elf64Symbol),
                             &symbol))
                return SetError("dynamic export symbol is unreadable");
            if (symbol.section_index == 0 || symbol.name == 0) continue;
            const std::uint8_t binding = symbol.information >> 4;
            const std::uint8_t type = symbol.information & 0x0fU;
            const std::uint8_t visibility = symbol.other & 3U;
            if ((binding != kSymbolBindingGlobal &&
                 binding != kSymbolBindingWeak) ||
                visibility == 1 || visibility == 2 ||
                type == kSymbolTypeTls)
                continue;
            std::string_view name;
            if (!SymbolName(symbol, &name)) return false;
            std::uint64_t address = 0;
            if (!DefinedSymbolValue(symbol, &address))
                return SetError("dynamic export value is out of bounds");
            image_.exports_.emplace(std::string(name), address);
        }
        return true;
    }

    bool CollectInitializers() {
        if (dynamic_.init_array_size == 0) return true;
        if (dynamic_.init_array == 0 ||
            dynamic_.init_array_size % sizeof(std::uint64_t) != 0 ||
            dynamic_.init_array_size >
                std::numeric_limits<std::size_t>::max())
            return SetError("DT_INIT_ARRAY metadata is invalid");
        std::size_t offset = 0;
        if (!VirtualOffset(dynamic_.init_array,
                           static_cast<std::size_t>(dynamic_.init_array_size),
                           kElfSegmentRead, &offset))
            return SetError("DT_INIT_ARRAY is outside readable memory");
        const std::size_t count = static_cast<std::size_t>(
            dynamic_.init_array_size / sizeof(std::uint64_t));
        for (std::size_t index = 0; index < count; ++index) {
            std::uint64_t function = 0;
            std::memcpy(&function,
                        image_.memory_.data() + offset +
                            index * sizeof(function),
                        sizeof(function));
            // The dynamic linker skips both 0 and (Elf_Addr)-1 sentinel
            // entries left in .init_array by the toolchain; only relocated
            // function pointers are real initializers.
            if (function == 0 ||
                function == std::numeric_limits<std::uint64_t>::max())
                continue;
            std::size_t code_offset = 0;
            if (!image_.RangeOffset(function, sizeof(std::uint32_t),
                                    kElfSegmentExecute, &code_offset))
                return SetError("DT_INIT_ARRAY target is not executable");
            image_.initializers_.push_back(function);
        }
        return true;
    }

    bool DynamicString(std::uint64_t string_offset, std::string_view* value) {
        if (string_offset >= dynamic_.string_size)
            return SetError("dynamic string offset is out of bounds");
        std::uint64_t name_vaddr = 0;
        if (AddOverflows(dynamic_.string_table, string_offset, &name_vaddr))
            return SetError("dynamic string address overflowed");
        const std::size_t maximum =
            static_cast<std::size_t>(dynamic_.string_size - string_offset);
        std::size_t offset = 0;
        if (!VirtualOffset(name_vaddr, maximum, kElfSegmentRead, &offset))
            return SetError("dynamic string is outside readable memory");
        const char* begin =
            reinterpret_cast<const char*>(image_.memory_.data() + offset);
        const void* terminator = std::memchr(begin, '\0', maximum);
        if (terminator == nullptr)
            return SetError("dynamic string is not terminated");
        *value = std::string_view(
            begin, static_cast<const char*>(terminator) - begin);
        return true;
    }

    bool SymbolName(const Elf64Symbol& symbol, std::string_view* name) {
        return DynamicString(symbol.name, name);
    }

    bool DefinedSymbolValue(const Elf64Symbol& symbol,
                            std::uint64_t* value) const {
        if (value == nullptr || symbol.section_index == 0 ||
            symbol.section_index == kSectionIndexCommon)
            return false;
        if (symbol.section_index == kSectionIndexAbsolute) {
            *value = symbol.value;
            return true;
        }
        if (symbol.section_index >= 0xff00) return false;
        const std::optional<std::uint64_t> address =
            image_.GuestAddress(symbol.value);
        if (address) {
            *value = *address;
            return true;
        }
        if (symbol.size != 0 || symbol.value < image_.low_vaddr_)
            return false;
        bool segment_end = false;
        for (const Elf64Segment& segment : image_.segments_) {
            if (segment.virtual_address <=
                    std::numeric_limits<std::uint64_t>::max() -
                        segment.memory_size &&
                symbol.value ==
                    segment.virtual_address + segment.memory_size) {
                segment_end = true;
                break;
            }
        }
        const std::uint64_t offset = symbol.value - image_.low_vaddr_;
        if (!segment_end || offset > image_.memory_.size() ||
            AddOverflows(image_.guest_base_, offset, value))
            return false;
        return true;
    }

    bool ResolveSymbol(std::uint32_t index, std::uint64_t* value) {
        if (index >= dynamic_.symbol_count)
            return SetError("relocation symbol index is out of bounds");
        Elf64Symbol symbol{};
        if (!ReadVirtual(dynamic_.symbol_table +
                             static_cast<std::uint64_t>(index) *
                                 sizeof(Elf64Symbol),
                         &symbol))
            return SetError("relocation symbol is unreadable");
        std::string_view name;
        if (!SymbolName(symbol, &name)) return false;
        if (symbol.section_index != 0) {
            if ((symbol.information & 0x0fU) == kSymbolTypeTls)
                return SetError("TLS relocation requires a TLS runtime model");
            if (!DefinedSymbolValue(symbol, value))
                return SetError("defined symbol value is out of bounds");
            return true;
        }
        const std::uint8_t binding = symbol.information >> 4;
        if (binding != kSymbolBindingGlobal &&
            binding != kSymbolBindingWeak)
            return SetError("undefined relocation symbol is not global or weak");
        if (name.empty()) return SetError("imported symbol has no name");
        // Match the Android linker: weak undefined symbols still search
        // the host-provided globals (for example cocos2d's
        // glGenVertexArraysOESEXT function-pointer exports); only an
        // unfound weak symbol resolves to zero.
        if (resolver_) {
            const std::optional<std::uint64_t> resolved = resolver_(name);
            if (resolved && *resolved != 0) {
                *value = *resolved;
                ++image_.relocations_.imports_bound;
                return true;
            }
        }
        if (binding == kSymbolBindingWeak) {
            // A weak undefined import that no provider resolves binds to 0.
            // Protected games probe optional symbols this way; logging the
            // name identifies which absent import the guest may later call.
            std::fprintf(
                stderr,
                "ISOMR_WEAK_IMPORT_NULL name=%.*s (bound to 0)\n",
                static_cast<int>(name.size()), name.data());
            *value = 0;
            return true;
        }
        return SetError("unresolved strong import: " + std::string(name));
    }

    // RELR: a packed bitmap of slot vaddrs whose stored pointer only needs
    // the load bias added. Even entries are a slot vaddr; odd entries are a
    // 63-bit bitmap covering the slots after the last even entry.
    bool ApplyRelrSlot(std::uint64_t virtual_address) {
        std::size_t offset = 0;
        if (!VirtualOffset(virtual_address, sizeof(std::uint64_t),
                           kElfSegmentWrite, &offset))
            return SetError("DT_RELR target is outside writable memory");
        std::uint64_t value = 0;
        std::memcpy(&value, image_.memory_.data() + offset, sizeof(value));
        std::uint64_t relocated = 0;
        if (!AddSigned(value,
                       static_cast<std::int64_t>(image_.load_bias()),
                       &relocated))
            return SetError("DT_RELR target value overflowed");
        std::memcpy(image_.memory_.data() + offset, &relocated,
                    sizeof(relocated));
        ++image_.relocations_.relative;
        return true;
    }

    bool ApplyRelr() {
        if (dynamic_.relr_size == 0) return true;
        if (dynamic_.relr == 0 ||
            dynamic_.relr_size % sizeof(std::uint64_t) != 0 ||
            dynamic_.relr_size > std::numeric_limits<std::size_t>::max())
            return SetError("DT_RELR metadata is invalid");
        std::size_t table_offset = 0;
        if (!VirtualOffset(dynamic_.relr,
                           static_cast<std::size_t>(dynamic_.relr_size),
                           kElfSegmentRead, &table_offset))
            return SetError("DT_RELR is outside readable memory");
        const std::size_t count = static_cast<std::size_t>(
            dynamic_.relr_size / sizeof(std::uint64_t));
        std::uint64_t base = 0;
        for (std::size_t index = 0; index < count; ++index) {
            std::uint64_t entry = 0;
            std::memcpy(&entry, image_.memory_.data() + table_offset +
                                index * sizeof(entry), sizeof(entry));
            if ((entry & 1) == 0) {
                if (!ApplyRelrSlot(entry)) return false;
                base = entry + sizeof(std::uint64_t);
            } else {
                for (std::uint64_t bit = 0; bit < 63; ++bit) {
                    if (((entry >> (bit + 1)) & 1) &&
                        !ApplyRelrSlot(base + bit * sizeof(std::uint64_t)))
                        return false;
                }
                base += 63 * sizeof(std::uint64_t);
            }
        }
        return true;
    }

    // SLEB128 reader over the in-memory packed-relocation stream. Advances
    // *cursor; returns false on truncation/overflow.
    bool ReadSleb128(const std::uint8_t* data, std::size_t size,
                     std::size_t* cursor, std::uint64_t* value) {
        std::uint64_t result = 0;
        std::uint32_t shift = 0;
        std::size_t at = *cursor;
        std::uint8_t byte = 0;
        do {
            if (at >= size || shift >= 64) return false;
            byte = data[at++];
            result |= static_cast<std::uint64_t>(byte & 0x7f) << shift;
            shift += 7;
        } while (byte & 0x80);
        // Sign-extend when the final byte's bit 6 is set (SLEB128 is signed).
        if (shift < 64 && (byte & 0x40)) result |= ~0ULL << shift;
        *cursor = at;
        *value = result;
        return true;
    }

    // APS2 packed RELA (DT_ANDROID_RELA): "APS2" magic, then SLEB128
    // relocation_count and group_size, then groups of relocations sharing
    // r_info / r_offset_delta / r_addend according to group flags.
    bool ApplyAndroidPackedRelocations() {
        if (dynamic_.android_rela_size == 0) return true;
        if (dynamic_.android_rela == 0 ||
            dynamic_.android_rela_size >
                std::numeric_limits<std::size_t>::max())
            return SetError("DT_ANDROID_RELA metadata is invalid");
        const std::size_t size =
            static_cast<std::size_t>(dynamic_.android_rela_size);
        std::size_t table_offset = 0;
        if (!VirtualOffset(dynamic_.android_rela, size, kElfSegmentRead,
                           &table_offset))
            return SetError("DT_ANDROID_RELA is outside readable memory");
        const std::uint8_t* data = image_.memory_.data() + table_offset;
        if (size < 4 || std::memcmp(data, "APS2", 4) != 0)
            return SetError("DT_ANDROID_RELA has no APS2 signature");
        std::size_t cursor = 4;
        std::uint64_t relocation_count = 0, r_offset = 0, r_addend = 0;
        // Header: relocation_count then the initial r_offset. Each group then
        // carries its own relocation count and flag word.
        if (!ReadSleb128(data, size, &cursor, &relocation_count) ||
            !ReadSleb128(data, size, &cursor, &r_offset))
            return SetError("DT_ANDROID_RELA header is invalid");

        // RelocationGroupFlags (bionic elf.h).
        constexpr std::uint64_t kGroupedByInfo = 1;
        constexpr std::uint64_t kGroupedByOffsetDelta = 2;
        constexpr std::uint64_t kGroupedByAddend = 4;
        constexpr std::uint64_t kGroupHasAddend = 8;

        while (relocation_count > 0) {
            std::uint64_t group_count = 0, group_flags = 0,
                          group_r_info = 0, group_offset_delta = 0,
                          group_addend_delta = 0;
            if (!ReadSleb128(data, size, &cursor, &group_count))
                return SetError("DT_ANDROID_RELA group count truncated");
            if (group_count > relocation_count)
                return SetError("DT_ANDROID_RELA group is unexpectedly large");
            relocation_count -= group_count;
            if (!ReadSleb128(data, size, &cursor, &group_flags))
                return SetError("DT_ANDROID_RELA group flags truncated");
            if ((group_flags & kGroupedByInfo) &&
                !ReadSleb128(data, size, &cursor, &group_r_info))
                return SetError("DT_ANDROID_RELA group r_info truncated");
            if ((group_flags & kGroupedByOffsetDelta) &&
                !ReadSleb128(data, size, &cursor, &group_offset_delta))
                return SetError(
                    "DT_ANDROID_RELA group offset-delta truncated");
            if ((group_flags & kGroupedByAddend) &&
                (group_flags & kGroupHasAddend) &&
                !ReadSleb128(data, size, &cursor, &group_addend_delta))
                return SetError(
                    "DT_ANDROID_RELA group addend-delta truncated");
            for (std::uint64_t i = 0; i < group_count; ++i) {
                std::uint64_t offset_delta = group_offset_delta;
                if (!(group_flags & kGroupedByOffsetDelta) &&
                    !ReadSleb128(data, size, &cursor, &offset_delta))
                    return SetError("DT_ANDROID_RELA r_offset truncated");
                std::uint64_t info = group_r_info;
                if (!(group_flags & kGroupedByInfo) &&
                    !ReadSleb128(data, size, &cursor, &info))
                    return SetError("DT_ANDROID_RELA r_info truncated");
                if (group_flags & kGroupHasAddend) {
                    std::uint64_t addend_delta = group_addend_delta;
                    if (!(group_flags & kGroupedByAddend) &&
                        !ReadSleb128(data, size, &cursor, &addend_delta))
                        return SetError("DT_ANDROID_RELA r_addend truncated");
                    if (!AddSigned(r_addend,
                                   static_cast<std::int64_t>(addend_delta),
                                   &r_addend))
                        return SetError(
                            "DT_ANDROID_RELA r_addend underflowed");
                }
                if (!AddSigned(r_offset,
                               static_cast<std::int64_t>(offset_delta),
                               &r_offset))
                    return SetError("DT_ANDROID_RELA r_offset underflowed");

                std::size_t target_offset = 0;
                if (!VirtualOffset(r_offset, sizeof(std::uint64_t),
                                   kElfSegmentWrite, &target_offset))
                    return SetError(
                        "DT_ANDROID_RELA target is outside writable memory");
                const std::uint32_t type =
                    static_cast<std::uint32_t>(info);
                const std::uint32_t symbol =
                    static_cast<std::uint32_t>(info >> 32);
                std::uint64_t value = 0;
                if (type == kRelocationRelative) {
                    if (symbol != 0 ||
                        !AddSigned(image_.load_bias(),
                                   static_cast<std::int64_t>(r_addend),
                                   &value))
                        return SetError("R_AARCH64_RELATIVE value is invalid");
                    ++image_.relocations_.relative;
                } else if (type == kRelocationAbsolute64 ||
                           type == kRelocationGlobalData ||
                           type == kRelocationJumpSlot) {
                    std::uint64_t symbol_value = 0;
                    if (!ResolveSymbol(symbol, &symbol_value) ||
                        !AddSigned(symbol_value,
                                   static_cast<std::int64_t>(r_addend),
                                   &value))
                        return error_.empty()
                                   ? SetError("symbol relocation value "
                                              "overflowed")
                                   : false;
                    if (type == kRelocationAbsolute64)
                        ++image_.relocations_.absolute;
                    else if (type == kRelocationGlobalData)
                        ++image_.relocations_.global_data;
                    else
                        ++image_.relocations_.jump_slots;
                } else {
                    return SetError("unsupported AArch64 relocation type " +
                                    std::to_string(type));
                }
                std::memcpy(image_.memory_.data() + target_offset, &value,
                            sizeof(value));
            }
        }
        return true;
    }

    bool ApplyRelocations(std::uint64_t table, std::uint64_t size) {
        if (size == 0) return true;
        if (table == 0 || size % sizeof(Elf64Rela) != 0 ||
            size > std::numeric_limits<std::size_t>::max())
            return SetError("RELA table metadata is invalid");
        std::size_t table_offset = 0;
        if (!VirtualOffset(table, static_cast<std::size_t>(size),
                           kElfSegmentRead, &table_offset))
            return SetError("RELA table is outside readable memory");
        const std::size_t count = static_cast<std::size_t>(
            size / sizeof(Elf64Rela));
        for (std::size_t index = 0; index < count; ++index) {
            Elf64Rela relocation{};
            std::memcpy(&relocation,
                        image_.memory_.data() + table_offset +
                            index * sizeof(Elf64Rela),
                        sizeof(relocation));
            std::size_t target_offset = 0;
            if (!VirtualOffset(relocation.offset, sizeof(std::uint64_t),
                               kElfSegmentWrite, &target_offset))
                return SetError("relocation target is outside writable memory");
            const std::uint32_t type =
                static_cast<std::uint32_t>(relocation.information);
            const std::uint32_t symbol =
                static_cast<std::uint32_t>(relocation.information >> 32);
            std::uint64_t value = 0;
            if (type == kRelocationRelative) {
                if (symbol != 0 ||
                    !AddSigned(image_.load_bias(), relocation.addend, &value))
                    return SetError("R_AARCH64_RELATIVE value is invalid");
                ++image_.relocations_.relative;
            } else if (type == kRelocationAbsolute64 ||
                       type == kRelocationGlobalData ||
                       type == kRelocationJumpSlot) {
                std::uint64_t symbol_value = 0;
                if (!ResolveSymbol(symbol, &symbol_value) ||
                    !AddSigned(symbol_value, relocation.addend, &value))
                    return error_.empty()
                               ? SetError("symbol relocation value overflowed")
                               : false;
                if (type == kRelocationAbsolute64)
                    ++image_.relocations_.absolute;
                else if (type == kRelocationGlobalData)
                    ++image_.relocations_.global_data;
                else
                    ++image_.relocations_.jump_slots;
            } else {
                return SetError("unsupported AArch64 relocation type " +
                                std::to_string(type));
            }
            std::memcpy(image_.memory_.data() + target_offset, &value,
                        sizeof(value));
        }
        return true;
    }

    const std::vector<std::uint8_t>& file_;
    std::uint64_t guest_base_;
    const Elf64ImportResolver& resolver_;
    const Elf64DependencyPreparer& prepare_;
    Elf64Header header_{};
    Elf64Image image_;
    DynamicInfo dynamic_;
    std::string error_;
};

Elf64LoadResult LoadElf64File(const std::wstring& path,
                              std::uint64_t guest_base,
                              const Elf64ImportResolver& resolver,
                              const Elf64DependencyPreparer& prepare) {
    std::vector<std::uint8_t> bytes;
    std::string error;
    if (!ReadWindowsFile(path, &bytes, &error))
        return {std::nullopt, std::move(error)};
    return Elf64LoaderImpl(bytes, guest_base, resolver, prepare).Load();
}

}  // namespace isomr
