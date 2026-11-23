#include "candy_entry_fixture.h"

#include <array>
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
#pragma pack(pop)

static_assert(sizeof(CandyEntryFixtureState) == 512);

constexpr std::size_t kFileSize = 0x4000;
constexpr std::size_t kDynamic = 0x400;
constexpr std::size_t kStrings = 0x600;
constexpr std::size_t kHash = 0xa00;
constexpr std::size_t kSymbols = 0xb00;
constexpr std::uint64_t kCreateNativeInstanceAddress = 0x1000;
constexpr std::uint64_t kCreateAddress = 0x1100;
constexpr std::uint64_t kSystemEventAddress = 0x1200;
constexpr std::uint64_t kInitializeAddress = 0x1300;
constexpr std::uint64_t kDisplayScaleAddress = 0x1400;
constexpr std::uint64_t kScreenSizeAddress = 0x1500;
constexpr std::uint64_t kStepAddress = 0x1600;
constexpr std::uint64_t kTouchAddress = 0x1700;
constexpr std::uint64_t kAnalyticsAddress = 0x1800;
constexpr std::uint64_t kAgeSignalsAddress = 0x1900;
constexpr std::uint64_t kSetAppShuttingDownAddress = 0x1a00;
constexpr std::uint64_t kDestroyAddress = 0x1b00;

constexpr std::array<std::string_view, 12> kNames{
    "Java_com_king_candycrushsaga_PlatformProxy_createNativeInstance",
    "Java_com_king_core_NativeApplication_create",
    "Java_com_king_core_NativeApplication_onSystemEvent",
    "Java_com_king_core_NativeApplication_init",
    "Java_com_king_core_NativeApplication_updateDisplayResolutionScale",
    "Java_com_king_core_NativeApplication_updateScreenSize",
    "Java_com_king_core_NativeApplication_step",
    "Java_com_king_usdk_kdid_AnalyticsReceiver_HandleRequestResponse",
    "Java_com_king_core_NativeApplication_onTouchEvent",
    "Java_com_king_usdk_privacyagecompliance_UsdkAgeSignals_"
    "nativeOnAgeSignalsError",
    "Java_com_king_core_NativeApplication_setAppShuttingDownFlag",
    "Java_com_king_core_NativeApplication_destroy"};
constexpr std::array<std::uint64_t, 12> kAddresses{
    kCreateNativeInstanceAddress, kCreateAddress, kSystemEventAddress,
    kInitializeAddress, kDisplayScaleAddress, kScreenSizeAddress,
    kStepAddress, kAnalyticsAddress, kTouchAddress, kAgeSignalsAddress,
    kSetAppShuttingDownAddress, kDestroyAddress};

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

std::uint32_t Store(std::uint32_t source, std::size_t byte_offset) {
    return 0xf9000000U |
           (static_cast<std::uint32_t>(byte_offset / 8) << 10) |
           (9U << 5) | source;
}

std::uint32_t Load(std::uint32_t target, std::size_t byte_offset) {
    return 0xf9400000U |
           (static_cast<std::uint32_t>(byte_offset / 8) << 10) |
           (9U << 5) | target;
}

void BeginRecord(std::vector<std::uint8_t>* bytes, std::size_t* offset,
                 std::size_t index_offset) {
    Instruction(bytes, offset, 0xd0000009U);  // adrp x9,state page
    Instruction(bytes, offset, 0x91040129U);  // add x9,x9,#0x100
    Instruction(bytes, offset, Load(10, 0));
    Instruction(bytes, offset, Store(10, index_offset));
    Instruction(bytes, offset, 0x9100054aU);  // add x10,x10,#1
    Instruction(bytes, offset, Store(10, 0));
}

void StoreArguments(std::vector<std::uint8_t>* bytes, std::size_t* offset,
                    std::size_t destination, std::uint32_t count) {
    for (std::uint32_t index = 0; index < count; ++index)
        Instruction(bytes, offset, Store(index, destination + index * 8));
}

}  // namespace

std::vector<std::uint8_t> MakeCandyEntryFixture(
    bool omit_touch_export, std::uint16_t create_loop_count) {
    std::vector<std::uint8_t> bytes(kFileSize, 0);
    const std::size_t export_count = omit_touch_export ? 7 : kNames.size();
    std::vector<char> strings(1, '\0');
    std::vector<std::uint32_t> name_offsets;
    for (std::size_t index = 0; index < export_count; ++index) {
        name_offsets.push_back(static_cast<std::uint32_t>(strings.size()));
        strings.insert(strings.end(), kNames[index].begin(), kNames[index].end());
        strings.push_back('\0');
    }
    const std::array<Dynamic, 6> dynamic{{
        {5, kStrings}, {10, strings.size()}, {6, kSymbols},
        {11, sizeof(Symbol)}, {4, kHash}, {0, 0}}};
    Header header{};
    std::memcpy(header.identification, "\x7f" "ELF", 4);
    header.identification[4] = 2;
    header.identification[5] = 1;
    header.identification[6] = 1;
    header.type = 3;
    header.machine = 183;
    header.version = 1;
    header.entry = kCreateNativeInstanceAddress;
    header.program_header_offset = sizeof(Header);
    header.header_size = sizeof(Header);
    header.program_header_entry_size = sizeof(ProgramHeader);
    header.program_header_count = 3;
    Put(&bytes, 0, header);
    const ProgramHeader text{1, 5, 0, 0, 0, 0x3000, 0x3000, 0x1000};
    const ProgramHeader data{1, 6, 0x3000, 0x3000, 0, 0x1000, 0x1000,
                             0x1000};
    const ProgramHeader dynamic_header{
        2, 4, kDynamic, kDynamic, 0, sizeof(dynamic), sizeof(dynamic), 8};
    Put(&bytes, sizeof(Header), text);
    Put(&bytes, sizeof(Header) + sizeof(ProgramHeader), data);
    Put(&bytes, sizeof(Header) + 2 * sizeof(ProgramHeader), dynamic_header);
    std::memcpy(bytes.data() + kDynamic, dynamic.data(), sizeof(dynamic));
    std::memcpy(bytes.data() + kStrings, strings.data(), strings.size());

    const std::uint32_t bucket_count = 1;
    const std::uint32_t chain_count =
        static_cast<std::uint32_t>(export_count + 1);
    Put(&bytes, kHash, bucket_count);
    Put(&bytes, kHash + 4, chain_count);
    const std::uint32_t first = export_count == 0 ? 0 : 1;
    Put(&bytes, kHash + 8, first);
    for (std::uint32_t index = 0; index < chain_count; ++index) {
        const std::uint32_t next =
            index == 0 || index == export_count ? 0 : index + 1;
        Put(&bytes, kHash + 12 + index * 4, next);
    }
    const Symbol null_symbol{};
    Put(&bytes, kSymbols, null_symbol);
    for (std::size_t index = 0; index < export_count; ++index) {
        const Symbol symbol{name_offsets[index], 0x12, 0, 1,
                            kAddresses[index], 0};
        Put(&bytes, kSymbols + (index + 1) * sizeof(Symbol), symbol);
    }

    std::size_t offset = kCreateNativeInstanceAddress;
    BeginRecord(&bytes, &offset, offsetof(CandyEntryFixtureState, create_index));
    StoreArguments(&bytes, &offset,
                   offsetof(CandyEntryFixtureState, create_arguments), 3);
    Instruction(&bytes, &offset, 0xd2995fc0U);  // movz x0,#0xcafe
    Instruction(&bytes, &offset, 0xf2a24680U);  // movk x0,#0x1234,lsl#16
    Instruction(&bytes, &offset,
                Store(0, offsetof(CandyEntryFixtureState, native_instance)));
    // Cross Candy's production inter-slice budget using repository-owned
    // instructions while preserving x0 and all recorded ABI state.
    Instruction(&bytes, &offset,
                0xd280000dU |
                    (static_cast<std::uint32_t>(create_loop_count) << 5));
    Instruction(&bytes, &offset, 0xd29fffecU);  // movz x12,#65535
    Instruction(&bytes, &offset, 0xf100058cU);  // subs x12,x12,#1
    Instruction(&bytes, &offset, 0x54ffffe1U);  // b.ne -4
    Instruction(&bytes, &offset, 0xf10005adU);  // subs x13,x13,#1
    Instruction(&bytes, &offset, 0x54ffff81U);  // b.ne -16
    Instruction(&bytes, &offset, 0xd65f03c0U);

    offset = kCreateAddress;
    BeginRecord(&bytes, &offset,
                offsetof(CandyEntryFixtureState, application_index));
    StoreArguments(&bytes, &offset,
                   offsetof(CandyEntryFixtureState, application_arguments), 5);
    Instruction(&bytes, &offset, 0xd65f03c0U);

    offset = kSystemEventAddress;
    Instruction(&bytes, &offset, 0xd0000009U);
    Instruction(&bytes, &offset, 0x91040129U);
    Instruction(&bytes, &offset, Load(10, 0));
    Instruction(&bytes, &offset,
                Load(11, offsetof(CandyEntryFixtureState, event_index_code)));
    for (int index = 0; index < 4; ++index)
        Instruction(&bytes, &offset, 0x8b0b016bU);
    Instruction(&bytes, &offset, 0x8b0a016bU);
    Instruction(&bytes, &offset,
                Store(11, offsetof(CandyEntryFixtureState, event_index_code)));
    Instruction(&bytes, &offset,
                Load(11, offsetof(CandyEntryFixtureState, event_action_code)));
    for (int index = 0; index < 3; ++index)
        Instruction(&bytes, &offset, 0x8b0b016bU);
    Instruction(&bytes, &offset, 0x8b02016bU);
    Instruction(&bytes, &offset,
                Store(11, offsetof(CandyEntryFixtureState, event_action_code)));
    Instruction(&bytes, &offset,
                Load(11, offsetof(CandyEntryFixtureState, event_count)));
    Instruction(&bytes, &offset, 0x9100056bU);
    Instruction(&bytes, &offset,
                Store(11, offsetof(CandyEntryFixtureState, event_count)));
    Instruction(&bytes, &offset,
                Store(0, offsetof(CandyEntryFixtureState, event_environment)));
    Instruction(&bytes, &offset,
                Store(1, offsetof(CandyEntryFixtureState, event_object)));
    Instruction(&bytes, &offset, 0x9100054aU);
    Instruction(&bytes, &offset, Store(10, 0));
    Instruction(&bytes, &offset, 0xd65f03c0U);

    offset = kInitializeAddress;
    BeginRecord(&bytes, &offset,
                offsetof(CandyEntryFixtureState, initialize_index));
    StoreArguments(&bytes, &offset,
                   offsetof(CandyEntryFixtureState, initialize_arguments), 5);
    Instruction(&bytes, &offset, 0x1e26000bU);  // fmov w11,s0
    Instruction(&bytes, &offset,
                Store(11, offsetof(CandyEntryFixtureState,
                                   initialize_arguments) + 5 * 8));
    Instruction(&bytes, &offset,
                Store(5, offsetof(CandyEntryFixtureState,
                                  initialize_arguments) + 6 * 8));
    Instruction(&bytes, &offset, 0xd65f03c0U);

    offset = kDisplayScaleAddress;
    BeginRecord(&bytes, &offset,
                offsetof(CandyEntryFixtureState, scale_index));
    StoreArguments(&bytes, &offset,
                   offsetof(CandyEntryFixtureState, scale_arguments), 2);
    Instruction(&bytes, &offset, 0x1e26000bU);
    Instruction(&bytes, &offset,
                Store(11, offsetof(CandyEntryFixtureState, scale_arguments) +
                              2 * 8));
    Instruction(&bytes, &offset, 0xd65f03c0U);

    offset = kScreenSizeAddress;
    BeginRecord(&bytes, &offset,
                offsetof(CandyEntryFixtureState, screen_index));
    StoreArguments(&bytes, &offset,
                   offsetof(CandyEntryFixtureState, screen_arguments), 4);
    Instruction(&bytes, &offset, 0xd65f03c0U);

    offset = kStepAddress;
    BeginRecord(&bytes, &offset, offsetof(CandyEntryFixtureState, step_index));
    StoreArguments(&bytes, &offset,
                   offsetof(CandyEntryFixtureState, step_arguments), 2);
    Instruction(&bytes, &offset, 0x1e26000bU);
    Instruction(&bytes, &offset,
                Store(11, offsetof(CandyEntryFixtureState, step_arguments) +
                              2 * 8));
    Instruction(&bytes, &offset, 0xd2800020U);  // movz x0,#1
    Instruction(&bytes, &offset, 0xd65f03c0U);

    offset = kAnalyticsAddress;
    BeginRecord(&bytes, &offset,
                offsetof(CandyEntryFixtureState, analytics_index));
    StoreArguments(&bytes, &offset,
                   offsetof(CandyEntryFixtureState, analytics_arguments), 4);
    Instruction(&bytes, &offset, 0xd65f03c0U);

    offset = kTouchAddress;
    BeginRecord(&bytes, &offset,
                offsetof(CandyEntryFixtureState, touch_index));
    StoreArguments(&bytes, &offset,
                   offsetof(CandyEntryFixtureState, touch_arguments), 6);
    Instruction(&bytes, &offset, 0xd65f03c0U);

    offset = kAgeSignalsAddress;
    BeginRecord(&bytes, &offset,
                offsetof(CandyEntryFixtureState, age_signals_index));
    StoreArguments(&bytes, &offset,
                   offsetof(CandyEntryFixtureState, age_signals_arguments), 6);
    Instruction(&bytes, &offset, 0xd65f03c0U);

    offset = kSetAppShuttingDownAddress;
    BeginRecord(&bytes, &offset,
                offsetof(CandyEntryFixtureState, shutting_down_index));
    StoreArguments(
        &bytes, &offset,
        offsetof(CandyEntryFixtureState, shutting_down_arguments), 2);
    Instruction(&bytes, &offset, 0xd65f03c0U);

    offset = kDestroyAddress;
    BeginRecord(&bytes, &offset,
                offsetof(CandyEntryFixtureState, destroy_index));
    StoreArguments(&bytes, &offset,
                   offsetof(CandyEntryFixtureState, destroy_arguments), 2);
    Instruction(&bytes, &offset, 0xd65f03c0U);

    // Wait until the bridge's first export starts, then fail inside its
    // deliberately multi-slice call.
    offset = kCandyEntryFixtureWorkerFailure;
    Instruction(&bytes, &offset, 0xd280002aU);  // movz x10,#1
    Instruction(&bytes, &offset, 0xf900e40aU);  // str x10,[x0,#456]
    Instruction(&bytes, &offset, 0xf9400009U);  // ldr x9,[x0]
    Instruction(&bytes, &offset, 0xb4ffffe9U);  // cbz x9,-4
    Instruction(&bytes, &offset, 0x00000000U);  // unsupported
    return bytes;
}

}  // namespace isomr::test
