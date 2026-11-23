#include "elf64_fixture.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace isomr::test {
namespace {

constexpr std::size_t kFileSize = 0x2100;
constexpr std::uint64_t kDynamicAddress = 0x400;
constexpr std::uint64_t kStringAddress = 0x600;
constexpr std::uint64_t kHashAddress = 0x680;
constexpr std::uint64_t kSymbolAddress = 0x700;
constexpr std::uint64_t kRelaAddress = 0x800;
constexpr std::uint64_t kJumpRelaAddress = 0x818;
// RELR fixture: the data PT_LOAD maps file 0x2000..0x2100 to vaddr
// 0x3000..0x30ff. Three writable slots at 0x3000/0x3008/0x3010, the init
// array at 0x3020, and the RELR table at 0x3040.
constexpr std::uint64_t kRelrDataSlot = 0x3000;
constexpr std::uint64_t kRelrTable = 0x3040;
constexpr std::size_t kRelrDataOffset = 0x2000;
constexpr std::size_t kRelrTableFileOffset = 0x2040;
constexpr std::size_t kFixtureInitArrayFileOffset = 0x2020;

enum class HashStyle { kSysv, kGnu, kMalformedGnu };

#pragma pack(push, 1)
struct Header {
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

struct ProgramHeader {
    std::uint32_t type;
    std::uint32_t flags;
    std::uint64_t offset;
    std::uint64_t virtual_address;
    std::uint64_t physical_address;
    std::uint64_t file_size;
    std::uint64_t memory_size;
    std::uint64_t alignment;
};

struct Dynamic {
    std::int64_t tag;
    std::uint64_t value;
};

struct Symbol {
    std::uint32_t name;
    std::uint8_t information;
    std::uint8_t other;
    std::uint16_t section_index;
    std::uint64_t value;
    std::uint64_t size;
};

struct Rela {
    std::uint64_t offset;
    std::uint64_t information;
    std::int64_t addend;
};
#pragma pack(pop)

static_assert(sizeof(Header) == 64);
static_assert(sizeof(ProgramHeader) == 56);
static_assert(sizeof(Dynamic) == 16);
static_assert(sizeof(Symbol) == 24);
static_assert(sizeof(Rela) == 24);

template <typename T>
void Put(std::vector<std::uint8_t>* bytes, std::size_t offset,
         const T& value) {
    std::memcpy(bytes->data() + offset, &value, sizeof(value));
}

void PutInstruction(std::vector<std::uint8_t>* bytes, std::size_t offset,
                    std::uint32_t instruction) {
    Put(bytes, offset, instruction);
}

std::vector<std::uint8_t> MakeFixture(
    std::string_view symbol_name, bool define_symbol,
    std::optional<std::string_view> dependency, bool initializer,
    HashStyle hash_style = HashStyle::kSysv) {
    std::vector<std::uint8_t> bytes(kFileSize, 0);
    std::vector<char> strings(1, '\0');
    const std::uint32_t symbol_name_offset =
        static_cast<std::uint32_t>(strings.size());
    strings.insert(strings.end(), symbol_name.begin(), symbol_name.end());
    strings.push_back('\0');
    std::uint64_t dependency_name_offset = 0;
    if (dependency) {
        dependency_name_offset = strings.size();
        strings.insert(strings.end(), dependency->begin(), dependency->end());
        strings.push_back('\0');
    }

    std::vector<Dynamic> entries{
        {5, kStringAddress},
        {10, strings.size()},
        {6, kSymbolAddress},
        {11, sizeof(Symbol)},
        {hash_style == HashStyle::kSysv ? 4 : 0x6ffffef5, kHashAddress},
    };
    if (dependency) entries.push_back({1, dependency_name_offset});
    if (!define_symbol || initializer) {
        entries.insert(entries.end(), {
            {7, kRelaAddress},
            {8, sizeof(Rela)},
            {9, sizeof(Rela)},
        });
    }
    if (!define_symbol) {
        entries.insert(entries.end(), {
            {23, kJumpRelaAddress},
            {2, sizeof(Rela)},
            {20, 7},
        });
    }
    if (initializer) {
        entries.push_back({25, kFixtureInitArray});
        entries.push_back({27, sizeof(std::uint64_t)});
    }
    entries.push_back({0, 0});

    Header header{};
    std::memcpy(header.identification, "\x7f" "ELF", 4);
    header.identification[4] = 2;  // ELFCLASS64
    header.identification[5] = 1;  // ELFDATA2LSB
    header.identification[6] = 1;  // EV_CURRENT
    header.type = 3;               // ET_DYN
    header.machine = 183;          // EM_AARCH64
    header.version = 1;
    header.entry = kFixtureEntry;
    header.program_header_offset = sizeof(Header);
    header.header_size = sizeof(Header);
    header.program_header_entry_size = sizeof(ProgramHeader);
    header.program_header_count = 3;
    Put(&bytes, 0, header);

    const ProgramHeader text{
        1, 5, 0, 0, 0, 0x2000, 0x2000, 0x1000};
    const ProgramHeader data{
        1, 6, 0x2000, 0x3000, 0, 0x100, 0x1000, 0x1000};
    const std::uint64_t dynamic_size = entries.size() * sizeof(Dynamic);
    const ProgramHeader dynamic{
        2, 4, kDynamicAddress, kDynamicAddress, 0,
        dynamic_size, dynamic_size, 8};
    Put(&bytes, sizeof(Header), text);
    Put(&bytes, sizeof(Header) + sizeof(ProgramHeader), data);
    Put(&bytes, sizeof(Header) + 2 * sizeof(ProgramHeader), dynamic);

    for (std::size_t index = 0; index < entries.size(); ++index)
        Put(&bytes, kDynamicAddress + index * sizeof(Dynamic), entries[index]);
    std::memcpy(bytes.data() + kStringAddress, strings.data(), strings.size());

    if (hash_style == HashStyle::kSysv) {
        const std::uint32_t hash[] = {1, 2, 1, 0, 0};
        std::memcpy(bytes.data() + kHashAddress, hash, sizeof(hash));
    } else {
        const std::uint32_t gnu_header[] = {1, 1, 1, 0};
        const std::uint64_t bloom = 0;
        const std::uint32_t bucket =
            hash_style == HashStyle::kMalformedGnu ? 0xffffffffU : 1U;
        const std::uint32_t chain = 1;
        std::memcpy(bytes.data() + kHashAddress, gnu_header,
                    sizeof(gnu_header));
        Put(&bytes, kHashAddress + sizeof(gnu_header), bloom);
        Put(&bytes, kHashAddress + sizeof(gnu_header) + sizeof(bloom), bucket);
        Put(&bytes,
            kHashAddress + sizeof(gnu_header) + sizeof(bloom) + sizeof(bucket),
            chain);
    }
    const Symbol null_symbol{};
    const Symbol symbol{
        symbol_name_offset,
        static_cast<std::uint8_t>((1U << 4) | 2U),
        0,
        static_cast<std::uint16_t>(define_symbol ? 1 : 0),
        define_symbol ? kFixtureImportThunk : 0,
        0};
    Put(&bytes, kSymbolAddress, null_symbol);
    Put(&bytes, kSymbolAddress + sizeof(Symbol), symbol);

    if (!define_symbol || initializer) {
        const Rela relative{
            initializer ? kFixtureInitArray : kFixtureRelativeSlot,
            1027,
            initializer ? static_cast<std::int64_t>(kFixtureInitializer)
                        : static_cast<std::int64_t>(0x1010)};
        Put(&bytes, kRelaAddress, relative);
    }
    if (!define_symbol) {
        const Rela jump{kFixtureImportSlot, (1ULL << 32) | 1026, 0};
        Put(&bytes, kJumpRelaAddress, jump);
    }

    // movz x0,#21 ; ldr x16,[x1] ; blr x16 ; hlt #0
    PutInstruction(&bytes, kFixtureEntry, 0xd28002a0U);
    PutInstruction(&bytes, kFixtureEntry + 4, 0xf9400030U);
    PutInstruction(&bytes, kFixtureEntry + 8, 0xd63f0200U);
    PutInstruction(&bytes, kFixtureEntry + 12, 0xd4400000U);
    // Import/export thunk: svc #7 ; ret
    PutInstruction(&bytes, kFixtureImportThunk, 0xd40000e1U);
    PutInstruction(&bytes, kFixtureImportThunk + 4, 0xd65f03c0U);
    // Provider initializer: movz x3,#99 ; ret
    PutInstruction(&bytes, kFixtureInitializer, 0xd2800c63U);
    PutInstruction(&bytes, kFixtureInitializer + 4, 0xd65f03c0U);
    return bytes;
}

}  // namespace

std::vector<std::uint8_t> MakeElf64Fixture() {
    return MakeFixture("isomr_host_double", false, std::nullopt, false);
}

std::vector<std::uint8_t> MakeElf64GnuHashFixture() {
    return MakeFixture("isomr_host_double", false, std::nullopt, false,
                       HashStyle::kGnu);
}

std::vector<std::uint8_t> MakeElf64MalformedGnuHashFixture() {
    return MakeFixture("isomr_host_double", false, std::nullopt, false,
                       HashStyle::kMalformedGnu);
}

std::vector<std::uint8_t> MakeElf64TlsExportFixture() {
    std::vector<std::uint8_t> bytes =
        MakeFixture("isomr_tls_export", true, std::nullopt, false);
    Symbol symbol{};
    std::memcpy(&symbol,
                bytes.data() + kSymbolAddress + sizeof(Symbol),
                sizeof(symbol));
    symbol.information = static_cast<std::uint8_t>((1U << 4) | 6U);
    symbol.value = std::numeric_limits<std::uint64_t>::max();
    Put(&bytes, kSymbolAddress + sizeof(Symbol), symbol);
    return bytes;
}

std::vector<std::uint8_t> MakeElf64AbsoluteExportFixture() {
    std::vector<std::uint8_t> bytes =
        MakeFixture("isomr_absolute_export", true, std::nullopt, false);
    Symbol symbol{};
    std::memcpy(&symbol,
                bytes.data() + kSymbolAddress + sizeof(Symbol),
                sizeof(symbol));
    symbol.information = static_cast<std::uint8_t>(1U << 4);
    symbol.section_index = 0xfff1;
    symbol.value = 42;
    Put(&bytes, kSymbolAddress + sizeof(Symbol), symbol);
    return bytes;
}

std::vector<std::uint8_t> MakeElf64EndMarkerExportFixture() {
    std::vector<std::uint8_t> bytes =
        MakeFixture("isomr_end_marker", true, std::nullopt, false);
    Symbol symbol{};
    std::memcpy(&symbol,
                bytes.data() + kSymbolAddress + sizeof(Symbol),
                sizeof(symbol));
    symbol.information = static_cast<std::uint8_t>(1U << 4);
    symbol.value = 0x4000;
    symbol.size = 0;
    Put(&bytes, kSymbolAddress + sizeof(Symbol), symbol);
    return bytes;
}

std::vector<std::uint8_t> MakeElf64HeapFixture() {
    std::vector<std::uint8_t> bytes =
        MakeFixture("malloc", false, std::nullopt, false);
    // movz x0,#32 ; ldr x16,[x1] ; blr x16 ; movz x2,#42 ;
    // str x2,[x0] ; ldr x0,[x0] ; hlt #0
    PutInstruction(&bytes, kFixtureEntry, 0xd2800400U);
    PutInstruction(&bytes, kFixtureEntry + 4, 0xf9400030U);
    PutInstruction(&bytes, kFixtureEntry + 8, 0xd63f0200U);
    PutInstruction(&bytes, kFixtureEntry + 12, 0xd2800542U);
    PutInstruction(&bytes, kFixtureEntry + 16, 0xf9000002U);
    PutInstruction(&bytes, kFixtureEntry + 20, 0xf9400000U);
    PutInstruction(&bytes, kFixtureHeapTerminal, 0xd4400000U);
    return bytes;
}

std::vector<std::uint8_t> MakeElf64MemoryOperationFixture() {
    std::vector<std::uint8_t> bytes =
        MakeFixture("memset", false, std::nullopt, false);
    // ldr x16,[x3] ; blr x16 ; ldr x0,[x0] ; hlt #0
    PutInstruction(&bytes, kFixtureEntry, 0xf9400070U);
    PutInstruction(&bytes, kFixtureEntry + 4, 0xd63f0200U);
    PutInstruction(&bytes, kFixtureEntry + 8, 0xf9400000U);
    PutInstruction(&bytes, kFixtureEntry + 12, 0xd4400000U);
    return bytes;
}

std::vector<std::uint8_t> MakeElf64TimeFixture() {
    std::vector<std::uint8_t> bytes =
        MakeFixture("clock_gettime", false, std::nullopt, false);
    // ldr x16,[x3] ; blr x16 ; hlt #0
    PutInstruction(&bytes, kFixtureEntry, 0xf9400070U);
    PutInstruction(&bytes, kFixtureEntry + 4, 0xd63f0200U);
    PutInstruction(&bytes, kFixtureEntry + 8, 0xd4400000U);
    return bytes;
}

std::vector<std::uint8_t> MakeElf64ThreadIdentityFixture() {
    std::vector<std::uint8_t> bytes =
        MakeFixture("pthread_self", false, std::nullopt, false);
    // ldr x16,[x3] ; blr x16 ; hlt #0
    PutInstruction(&bytes, kFixtureEntry, 0xf9400070U);
    PutInstruction(&bytes, kFixtureEntry + 4, 0xd63f0200U);
    PutInstruction(&bytes, kFixtureEntry + 8, 0xd4400000U);
    return bytes;
}

std::vector<std::uint8_t> MakeElf64ThreadStartFixture() {
    std::vector<std::uint8_t> bytes =
        MakeFixture("pthread_self", false, std::nullopt, false);
    // mrs x2,tpidr_el0 ; ldr x3,[x2,#40] ; add x0,x0,#1 ; ret ; hlt #0
    PutInstruction(&bytes, kFixtureEntry, 0xd53bd042U);
    PutInstruction(&bytes, kFixtureEntry + 4, 0xf9401443U);
    PutInstruction(&bytes, kFixtureEntry + 8, 0x91000400U);
    PutInstruction(&bytes, kFixtureEntry + 12, 0xd65f03c0U);
    PutInstruction(&bytes, kFixtureThreadTerminal, 0xd4400000U);
    PutInstruction(&bytes, kFixtureThreadDestructor, 0xd53bd042U);
    PutInstruction(&bytes, kFixtureThreadDestructor + 4, 0xf9401443U);
    PutInstruction(&bytes, kFixtureThreadDestructor + 8, 0x91000400U);
    PutInstruction(&bytes, kFixtureThreadDestructor + 12, 0xd65f03c0U);
    PutInstruction(&bytes, kFixtureThreadLoop, 0x14000000U);
    PutInstruction(&bytes, kFixtureThreadImport, 0xa9bf7bfdU);
    PutInstruction(&bytes, kFixtureThreadImport + 4, 0xf9400070U);
    PutInstruction(&bytes, kFixtureThreadImport + 8, 0xd63f0200U);
    PutInstruction(&bytes, kFixtureThreadImport + 12, 0xa8c17bfdU);
    PutInstruction(&bytes, kFixtureThreadImport + 16, 0xd65f03c0U);
    // Comparator: return *(uint32_t*)key - *(uint32_t*)element.
    PutInstruction(&bytes, kFixtureThreadCompare, 0xb9400002U);
    PutInstruction(&bytes, kFixtureThreadCompare + 4, 0xb9400023U);
    PutInstruction(&bytes, kFixtureThreadCompare + 8, 0x4b030040U);
    PutInstruction(&bytes, kFixtureThreadCompare + 12, 0xd65f03c0U);
    // Bounded loop: decrement x0 to zero, preserving execution state across
    // dispatcher runs, then return.
    PutInstruction(&bytes, kFixtureThreadBoundedLoop, 0xf1000400U);
    PutInstruction(&bytes, kFixtureThreadBoundedLoop + 4, 0x54ffffe1U);
    PutInstruction(&bytes, kFixtureThreadBoundedLoop + 8, 0xd65f03c0U);
    // Deliberately unresolved SVC identifier for telemetry residual coverage.
    PutInstruction(&bytes, kFixtureThreadUnresolvedSvc, 0xd41fffe1U);
    PutInstruction(&bytes, kFixtureThreadUnresolvedSvc + 4, 0xd65f03c0U);
    return bytes;
}

std::vector<std::uint8_t> MakeElf64JniOnLoadFixture() {
    std::vector<std::uint8_t> bytes =
        MakeFixture("JNI_OnLoad", true, std::nullopt, false);
    // Profile entry: add x0,x0,#1 ; ret
    PutInstruction(&bytes, kFixtureEntry, 0x91000400U);
    PutInstruction(&bytes, kFixtureEntry + 4, 0xd65f03c0U);

    // JNI_OnLoad(JavaVM* vm, void*):
    //   sub sp,sp,#16
    //   save x30; mov w2,#JNI_VERSION_1_6
    //   mov x1,sp
    //   call vm->GetEnv(vm,&env,version) through slot 6
    //   call env->GetVersion(env) through slot 4
    //   restore x30; add sp,sp,#16 ; ret
    constexpr std::array<std::uint32_t, 14> code{
        0xd10043ffU, 0xf90007feU, 0x528000c2U, 0x72a00022U,
        0x910003e1U, 0xf9400010U, 0xf9401a10U, 0xd63f0200U,
        0xf94003e0U, 0xf9400010U, 0xf9401210U, 0xd63f0200U,
        0xf94007feU, 0x910043ffU};
    for (std::size_t index = 0; index < code.size(); ++index)
        PutInstruction(&bytes, kFixtureImportThunk + index * 4,
                       code[index]);
    PutInstruction(&bytes, kFixtureImportThunk + code.size() * 4,
                   0xd65f03c0U);
    return bytes;
}

std::vector<std::uint8_t> MakeElf64ExclusiveMemoryFixture() {
    std::vector<std::uint8_t> bytes =
        MakeFixture("exclusive_memory", true, std::nullopt, false);
    // ldxp x1,x2,[x3] ; stxp w4,x5,x6,[x3] ; mov x0,x4 ; ret
    PutInstruction(&bytes, kFixtureEntry, 0xc87f0861U);
    PutInstruction(&bytes, kFixtureEntry + 4, 0xc8241865U);
    PutInstruction(&bytes, kFixtureEntry + 8, 0xaa0403e0U);
    PutInstruction(&bytes, kFixtureEntry + 12, 0xd65f03c0U);
    // ldr x0,[x3] ; str x5,[x3] ; ret
    PutInstruction(&bytes, kFixtureMemoryReadWrite, 0xf9400060U);
    PutInstruction(&bytes, kFixtureMemoryReadWrite + 4, 0xf9000065U);
    PutInstruction(&bytes, kFixtureMemoryReadWrite + 8, 0xd65f03c0U);
    // mov x0,#1024 ; ldr x1,[x3] ; subs x0,x0,#1 ; b.ne -8 ; ret
    PutInstruction(&bytes, kFixtureMemoryReadSample, 0xd2808000U);
    PutInstruction(&bytes, kFixtureMemoryReadSample + 4, 0xf9400061U);
    PutInstruction(&bytes, kFixtureMemoryReadSample + 8, 0xf1000400U);
    PutInstruction(&bytes, kFixtureMemoryReadSample + 12, 0x54ffffc1U);
    PutInstruction(&bytes, kFixtureMemoryReadSample + 16, 0xd65f03c0U);
    return bytes;
}

std::vector<std::uint8_t> MakeElf64EntryAbiFixture() {
    std::vector<std::uint8_t> bytes =
        MakeFixture("entry_abi", true, std::nullopt, false);

    // Sum x0-x7 and the two 8-byte stack arguments into x0.
    std::size_t offset = kFixtureEntry;
    for (std::uint32_t index = 1; index < 8; ++index) {
        PutInstruction(&bytes, offset,
                       0x8b000000U | (index << 16));
        offset += 4;
    }
    PutInstruction(&bytes, offset, 0xf94003e8U);  // ldr x8,[sp]
    PutInstruction(&bytes, offset + 4, 0x8b080000U);  // add x0,x0,x8
    PutInstruction(&bytes, offset + 8, 0xf94007e8U);  // ldr x8,[sp,#8]
    PutInstruction(&bytes, offset + 12, 0x8b080000U);
    PutInstruction(&bytes, offset + 16, 0xd65f03c0U);

    // XOR the low 64 bits of d0-d7 and the spilled ninth double, then
    // return those raw bits in d0.
    offset = kFixtureEntryAbiFloat;
    PutInstruction(&bytes, offset, 0x9e660000U);  // fmov x0,d0
    offset += 4;
    for (std::uint32_t index = 1; index < 8; ++index) {
        PutInstruction(&bytes, offset,
                       0x9e660001U | (index << 5));  // fmov x1,dN
        PutInstruction(&bytes, offset + 4, 0xca010000U);  // eor x0,x0,x1
        offset += 8;
    }
    PutInstruction(&bytes, offset, 0xf94003e1U);  // ldr x1,[sp]
    PutInstruction(&bytes, offset + 4, 0xca010000U);
    PutInstruction(&bytes, offset + 8, 0x9e670000U);  // fmov d0,x0
    PutInstruction(&bytes, offset + 12, 0xd65f03c0U);

    // x0 + x1 + x2 + raw s0 bits. This proves that the intervening float
    // consumes v0 without consuming the later general/boolean register.
    PutInstruction(&bytes, kFixtureEntryAbiMixed, 0x8b010000U);
    PutInstruction(&bytes, kFixtureEntryAbiMixed + 4, 0x8b020000U);
    PutInstruction(&bytes, kFixtureEntryAbiMixed + 8, 0x1e260003U);
    PutInstruction(&bytes, kFixtureEntryAbiMixed + 12, 0x8b030000U);
    PutInstruction(&bytes, kFixtureEntryAbiMixed + 16, 0xd65f03c0U);

    // Return stack[0]-stack[8] in x0 and stack[16:32] in v0.
    PutInstruction(&bytes, kFixtureEntryAbiInterleaved, 0xf94003e0U);
    PutInstruction(&bytes, kFixtureEntryAbiInterleaved + 4, 0xf94007e1U);
    PutInstruction(&bytes, kFixtureEntryAbiInterleaved + 8, 0xcb010000U);
    PutInstruction(&bytes, kFixtureEntryAbiInterleaved + 12, 0x3dc007e0U);
    PutInstruction(&bytes, kFixtureEntryAbiInterleaved + 16, 0xd65f03c0U);

    // Return the registered vector's low lane in x0 and the aligned spilled
    // vector in the full v0 result.
    PutInstruction(&bytes, kFixtureEntryAbiVector, 0x9e660000U);
    PutInstruction(&bytes, kFixtureEntryAbiVector + 4, 0x3dc007e0U);
    PutInstruction(&bytes, kFixtureEntryAbiVector + 8, 0xd65f03c0U);

    PutInstruction(&bytes, kFixtureEntryAbiStack, 0x910003e0U);  // mov x0,sp
    PutInstruction(&bytes, kFixtureEntryAbiStack + 4, 0xd65f03c0U);
    PutInstruction(&bytes, kFixtureEntryAbiUnbalanced, 0xd10043ffU);
    PutInstruction(&bytes, kFixtureEntryAbiUnbalanced + 4, 0xd65f03c0U);
    PutInstruction(&bytes, kFixtureEntryAbiZero, 0xd65f03c0U);
    return bytes;
}

// RELR fixture: writable slots and the .init_array entry hold raw virtual
// addresses that only the packed RELR bitmap relocates (no classic RELA).
// This is the layout modern lld-linked libraries use: without DT_RELR the
// loader would see raw vaddrs and reject the init array as not executable.
std::vector<std::uint8_t> MakeElf64RelrFixture() {
    std::vector<std::uint8_t> bytes(kFileSize, 0);
    std::vector<char> strings{'\0'};
    const std::uint32_t name_offset = 1;
    strings.insert(strings.end(), {'r', 'e', 'l', 'r', '_', 'e', 'x', '\0'});

    std::vector<Dynamic> entries{
        {5, kStringAddress},
        {10, strings.size()},
        {6, kSymbolAddress},
        {11, sizeof(Symbol)},
        {4, kHashAddress},
        {25, kFixtureInitArray},            // DT_INIT_ARRAY
        {27, sizeof(std::uint64_t)},        // DT_INIT_ARRAYSZ
        {0x6fffe000, kRelrTable},           // DT_RELR
        {0x6fffe001, 3 * sizeof(std::uint64_t)},  // DT_RELRSZ
        {0, 0},
    };

    Header header{};
    std::memcpy(header.identification, "\x7f" "ELF", 4);
    header.identification[4] = 2;
    header.identification[5] = 1;
    header.identification[6] = 1;
    header.type = 3;
    header.machine = 183;
    header.version = 1;
    header.entry = kFixtureEntry;
    header.program_header_offset = sizeof(Header);
    header.header_size = sizeof(Header);
    header.program_header_entry_size = sizeof(ProgramHeader);
    header.program_header_count = 3;
    Put(&bytes, 0, header);

    const ProgramHeader text{1, 5, 0, 0, 0, 0x2000, 0x2000, 0x1000};
    const ProgramHeader data{1, 6, 0x2000, 0x3000, 0, 0x100, 0x1000, 0x1000};
    const std::uint64_t dynamic_size = entries.size() * sizeof(Dynamic);
    const ProgramHeader dynamic{2, 4, kDynamicAddress, kDynamicAddress, 0,
                                dynamic_size, dynamic_size, 8};
    Put(&bytes, sizeof(Header), text);
    Put(&bytes, sizeof(Header) + sizeof(ProgramHeader), data);
    Put(&bytes, sizeof(Header) + 2 * sizeof(ProgramHeader), dynamic);
    for (std::size_t index = 0; index < entries.size(); ++index)
        Put(&bytes, kDynamicAddress + index * sizeof(Dynamic), entries[index]);
    std::memcpy(bytes.data() + kStringAddress, strings.data(), strings.size());
    const std::uint32_t hash[] = {1, 1, 0, 0, 0};
    std::memcpy(bytes.data() + kHashAddress, hash, sizeof(hash));
    Put(&bytes, kSymbolAddress, Symbol{});
    Put(&bytes, kSymbolAddress + sizeof(Symbol),
        Symbol{name_offset,
               static_cast<std::uint8_t>((1U << 4) | 2U), 0, 1,
               kFixtureImportThunk, 0});

    // Raw vaddrs stored in the writable slots; RELR adds the load bias.
    Put<std::uint64_t>(&bytes, kRelrDataOffset, kFixtureImportThunk);
    Put<std::uint64_t>(&bytes, kRelrDataOffset + 8, kFixtureImportThunk);
    Put<std::uint64_t>(&bytes, kRelrDataOffset + 16, kFixtureImportThunk);
    Put<std::uint64_t>(&bytes, kFixtureInitArrayFileOffset,
                       kFixtureInitializer);
    // RELR: even entry relocates the init array slot; a second even entry
    // relocates the first data slot and bases the bitmap, which covers the
    // next two data slots.
    Put<std::uint64_t>(&bytes, kRelrTableFileOffset, kFixtureInitArray);
    Put<std::uint64_t>(&bytes, kRelrTableFileOffset + 8, kRelrDataSlot);
    Put<std::uint64_t>(&bytes, kRelrTableFileOffset + 16, 0x7);

    // Function the init array points at: movz x3,#99 ; ret
    PutInstruction(&bytes, kFixtureInitializer, 0xd2800c63U);
    PutInstruction(&bytes, kFixtureInitializer + 4, 0xd65f03c0U);
    return bytes;
}

std::vector<std::uint8_t> MakeElf64ProviderFixture() {
    return MakeFixture("isomr_dependency_double", true, std::nullopt, true);
}

std::vector<std::uint8_t> MakeElf64ConsumerFixture() {
    return MakeFixture("isomr_dependency_double", false, "libprovider.so",
                       false);
}

std::vector<std::uint8_t> MakeElf64UnsafeDependencyFixture() {
    return MakeFixture("isomr_dependency_double", false, "..\\libprovider.so",
                       false);
}

std::vector<std::uint8_t> MakeElf64UnknownDependencyFixture() {
    return MakeFixture("isomr_dependency_double", false, "libprovidre.so",
                       false);
}

std::vector<std::uint8_t> MakeElf64CycleAFixture() {
    return MakeFixture("cycle_a", true, "cycle-b.so", false);
}

std::vector<std::uint8_t> MakeElf64CycleBFixture() {
    return MakeFixture("cycle_b", true, "cycle-a.so", false);
}

}  // namespace isomr::test
