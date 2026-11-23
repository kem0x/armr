#include "bionic_jumps_fixture.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>

namespace isomr::test {
namespace {

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

constexpr std::size_t kFileSize = 0x4000;
constexpr std::size_t kDynamic = 0x400;
constexpr std::size_t kStrings = 0x600;
constexpr std::size_t kHash = 0x680;
constexpr std::size_t kSymbols = 0x700;
constexpr std::size_t kRelocations = 0x800;

template <typename T>
void Put(std::vector<std::uint8_t>* bytes, std::size_t offset,
         const T& value) {
    std::memcpy(bytes->data() + offset, &value, sizeof(value));
}

void Instruction(std::vector<std::uint8_t>* bytes, std::size_t* offset,
                 std::uint32_t value) {
    Put(bytes, *offset, value);
    *offset += 4;
}

std::uint32_t MoveImmediate(std::uint32_t target, std::uint16_t value) {
    return 0xd2800000U | (static_cast<std::uint32_t>(value) << 5) | target;
}

std::uint32_t Store(std::uint32_t source, std::size_t byte_offset) {
    return 0xf9000000U |
           (static_cast<std::uint32_t>(byte_offset / 8) << 10) |
           (20U << 5) | source;
}

}  // namespace

std::vector<std::uint8_t> MakeBionicJumpsFixture() {
    std::vector<std::uint8_t> bytes(kFileSize, 0);
    const std::string_view setjmp_name = "setjmp";
    const std::string_view longjmp_name = "longjmp";
    std::vector<char> strings(1, '\0');
    const std::uint32_t setjmp_offset =
        static_cast<std::uint32_t>(strings.size());
    strings.insert(strings.end(), setjmp_name.begin(), setjmp_name.end());
    strings.push_back('\0');
    const std::uint32_t longjmp_offset =
        static_cast<std::uint32_t>(strings.size());
    strings.insert(strings.end(), longjmp_name.begin(), longjmp_name.end());
    strings.push_back('\0');

    const std::array<Dynamic, 10> dynamic{{
        {5, kStrings}, {10, strings.size()}, {6, kSymbols},
        {11, sizeof(Symbol)}, {4, kHash}, {23, kRelocations},
        {2, 2 * sizeof(Rela)}, {20, 7}, {9, sizeof(Rela)}, {0, 0}}};
    Header header{};
    std::memcpy(header.identification, "\x7f" "ELF", 4);
    header.identification[4] = 2;
    header.identification[5] = 1;
    header.identification[6] = 1;
    header.type = 3;
    header.machine = 183;
    header.version = 1;
    header.entry = kJumpFixtureEntry;
    header.program_header_offset = sizeof(Header);
    header.header_size = sizeof(Header);
    header.program_header_entry_size = sizeof(ProgramHeader);
    header.program_header_count = 3;
    Put(&bytes, 0, header);
    const ProgramHeader text{1, 5, 0, 0, 0, 0x3000, 0x3000, 0x1000};
    const ProgramHeader data{1, 6, 0x3000, 0x3000, 0, 0x1000, 0x1000,
                             0x1000};
    const ProgramHeader dynamic_header{2, 4, kDynamic, kDynamic, 0,
                                       sizeof(dynamic), sizeof(dynamic), 8};
    Put(&bytes, sizeof(Header), text);
    Put(&bytes, sizeof(Header) + sizeof(ProgramHeader), data);
    Put(&bytes, sizeof(Header) + 2 * sizeof(ProgramHeader), dynamic_header);
    std::memcpy(bytes.data() + kDynamic, dynamic.data(), sizeof(dynamic));
    std::memcpy(bytes.data() + kStrings, strings.data(), strings.size());

    const std::uint32_t hash[] = {1, 3, 1, 0, 2, 0};
    std::memcpy(bytes.data() + kHash, hash, sizeof(hash));
    const Symbol null_symbol{};
    const Symbol setjmp_symbol{setjmp_offset, 0x12, 0, 0, 0, 0};
    const Symbol longjmp_symbol{longjmp_offset, 0x12, 0, 0, 0, 0};
    Put(&bytes, kSymbols, null_symbol);
    Put(&bytes, kSymbols + sizeof(Symbol), setjmp_symbol);
    Put(&bytes, kSymbols + 2 * sizeof(Symbol), longjmp_symbol);
    const Rela setjmp_relocation{kJumpFixtureSetjmpSlot,
                                 (1ULL << 32) | 1026, 0};
    const Rela longjmp_relocation{kJumpFixtureLongjmpSlot,
                                  (2ULL << 32) | 1026, 0};
    Put(&bytes, kRelocations, setjmp_relocation);
    Put(&bytes, kRelocations + sizeof(Rela), longjmp_relocation);

    std::size_t offset = kJumpFixtureEntry;
    Instruction(&bytes, &offset, 0xa9bf7bfdU);  // save caller x29/x30
    Instruction(&bytes, &offset, 0xaa0003f1U);  // mov x17,x0
    Instruction(&bytes, &offset, MoveImmediate(18, 0x1818));
    Instruction(&bytes, &offset, 0xaa0003f3U);  // mov x19,x0
    Instruction(&bytes, &offset, 0xaa0103f4U);  // mov x20,x1
    for (std::uint32_t reg = 21; reg <= 29; ++reg)
        Instruction(&bytes, &offset,
                    MoveImmediate(reg, static_cast<std::uint16_t>(reg)));
    for (std::uint32_t reg = 8; reg <= 15; ++reg) {
        Instruction(&bytes, &offset,
                    MoveImmediate(9, static_cast<std::uint16_t>(0x80 + reg - 8)));
        Instruction(&bytes, &offset,
                    0x9e670000U | (9U << 5) | reg);  // fmov dN,x9
    }
    Instruction(&bytes, &offset, 0xf9400050U);  // ldr x16,[x2]
    Instruction(&bytes, &offset, 0xd63f0200U);  // blr x16 (setjmp)
    const std::size_t resume_offset = offset;
    const std::size_t branch_offset = offset;
    Instruction(&bytes, &offset, 0);  // cbnz x0,resumed
    for (std::uint32_t reg = 18; reg <= 29; ++reg)
        Instruction(&bytes, &offset, 0xaa1f03e0U | reg);  // mov xN,xzr
    for (std::uint32_t reg = 8; reg <= 15; ++reg)
        Instruction(&bytes, &offset,
                    0x9e670000U | (31U << 5) | reg);  // fmov dN,xzr
    Instruction(&bytes, &offset, 0xd10083ffU);  // sub sp,sp,#32
    Instruction(&bytes, &offset, 0xaa1103e0U);  // mov x0,x17
    Instruction(&bytes, &offset, MoveImmediate(1, 0));
    Instruction(&bytes, &offset, 0xf9400070U);  // ldr x16,[x3]
    Instruction(&bytes, &offset, 0xd63f0200U);  // blr x16 (longjmp)
    Instruction(&bytes, &offset, 0xd4200000U);  // must not return here
    const std::size_t resumed_offset = offset;
    const std::int64_t branch_words =
        static_cast<std::int64_t>(resumed_offset - branch_offset) / 4;
    const std::uint32_t branch =
        0xb5000000U |
        ((static_cast<std::uint32_t>(branch_words) & 0x7ffffU) << 5);
    Put(&bytes, branch_offset, branch);

    Instruction(&bytes, &offset,
                Store(0, offsetof(JumpFixtureResult, setjmp_result)));
    for (std::uint32_t reg = 18; reg <= 30; ++reg)
        Instruction(&bytes, &offset,
                    Store(reg, offsetof(JumpFixtureResult, general) +
                                   (reg - 18) * sizeof(std::uint64_t)));
    Instruction(&bytes, &offset, 0x910003e9U);  // mov x9,sp
    Instruction(&bytes, &offset,
                Store(9, offsetof(JumpFixtureResult, stack_pointer)));
    for (std::uint32_t reg = 8; reg <= 15; ++reg) {
        Instruction(&bytes, &offset,
                    0x9e660000U | (reg << 5) | 9U);  // fmov x9,dN
        Instruction(&bytes, &offset,
                    Store(9, offsetof(JumpFixtureResult, vector_low) +
                                 (reg - 8) * sizeof(std::uint64_t)));
    }
    Instruction(&bytes, &offset, MoveImmediate(0, 42));
    Instruction(&bytes, &offset, 0xa8c17bfdU);  // restore caller x29/x30
    Instruction(&bytes, &offset, 0xd65f03c0U);

    // x30 saved by setjmp is precisely the instruction following its BLR.
    static_assert(sizeof(JumpFixtureResult) == 184);
    (void)resume_offset;
    return bytes;
}

}  // namespace isomr::test
