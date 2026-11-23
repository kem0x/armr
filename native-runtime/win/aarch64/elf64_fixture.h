#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace isomr::test {

constexpr std::uint64_t kFixtureEntry = 0x1000;
constexpr std::uint64_t kFixtureImportThunk = 0x1100;
constexpr std::uint64_t kFixtureRelativeSlot = 0x3000;
constexpr std::uint64_t kFixtureImportSlot = 0x3008;
constexpr std::uint64_t kFixtureInitArray = 0x3020;
constexpr std::uint64_t kFixtureBssProbe = 0x3200;
constexpr std::uint64_t kFixtureInitializer = 0x1200;
constexpr std::uint64_t kFixtureHeapTerminal = kFixtureEntry + 24;
constexpr std::uint64_t kFixtureThreadTerminal = kFixtureEntry + 16;
constexpr std::uint64_t kFixtureThreadDestructor = kFixtureEntry + 32;
constexpr std::uint64_t kFixtureThreadLoop = kFixtureEntry + 48;
constexpr std::uint64_t kFixtureThreadImport = kFixtureEntry + 64;
constexpr std::uint64_t kFixtureThreadCompare = kFixtureEntry + 96;
constexpr std::uint64_t kFixtureThreadBoundedLoop = kFixtureEntry + 112;
constexpr std::uint64_t kFixtureThreadUnresolvedSvc = kFixtureEntry + 128;
constexpr std::uint64_t kFixtureMemoryReadWrite = kFixtureEntry + 128;
constexpr std::uint64_t kFixtureMemoryReadSample = kFixtureEntry + 160;
constexpr std::uint64_t kFixtureEntryAbiFloat = 0x1080;
constexpr std::uint64_t kFixtureEntryAbiMixed = 0x1100;
constexpr std::uint64_t kFixtureEntryAbiInterleaved = 0x1180;
constexpr std::uint64_t kFixtureEntryAbiVector = 0x1200;
constexpr std::uint64_t kFixtureEntryAbiStack = 0x1280;
constexpr std::uint64_t kFixtureEntryAbiUnbalanced = 0x1300;
constexpr std::uint64_t kFixtureEntryAbiZero = 0x1400;
constexpr std::size_t kFixtureRelaInformationFileOffset = 0x808;

std::vector<std::uint8_t> MakeElf64Fixture();
std::vector<std::uint8_t> MakeElf64GnuHashFixture();
std::vector<std::uint8_t> MakeElf64MalformedGnuHashFixture();
std::vector<std::uint8_t> MakeElf64TlsExportFixture();
std::vector<std::uint8_t> MakeElf64AbsoluteExportFixture();
std::vector<std::uint8_t> MakeElf64EndMarkerExportFixture();
std::vector<std::uint8_t> MakeElf64HeapFixture();
std::vector<std::uint8_t> MakeElf64MemoryOperationFixture();
std::vector<std::uint8_t> MakeElf64TimeFixture();
std::vector<std::uint8_t> MakeElf64ThreadIdentityFixture();
std::vector<std::uint8_t> MakeElf64ThreadStartFixture();
std::vector<std::uint8_t> MakeElf64JniOnLoadFixture();
std::vector<std::uint8_t> MakeElf64ExclusiveMemoryFixture();
std::vector<std::uint8_t> MakeElf64EntryAbiFixture();
std::vector<std::uint8_t> MakeElf64RelrFixture();
std::vector<std::uint8_t> MakeElf64ProviderFixture();
std::vector<std::uint8_t> MakeElf64ConsumerFixture();
std::vector<std::uint8_t> MakeElf64UnsafeDependencyFixture();
std::vector<std::uint8_t> MakeElf64UnknownDependencyFixture();
std::vector<std::uint8_t> MakeElf64CycleAFixture();
std::vector<std::uint8_t> MakeElf64CycleBFixture();

}  // namespace isomr::test
