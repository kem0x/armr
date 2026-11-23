#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace isomr::test {

constexpr std::uint64_t kJumpFixtureEntry = 0x1000;
constexpr std::uint64_t kJumpFixtureSetjmpResume = 0x1080;
constexpr std::uint64_t kJumpFixtureSetjmpSlot = 0x3000;
constexpr std::uint64_t kJumpFixtureLongjmpSlot = 0x3008;

struct JumpFixtureResult {
    std::uint64_t setjmp_result;
    std::array<std::uint64_t, 13> general;  // x18-x30
    std::uint64_t stack_pointer;
    std::array<std::uint64_t, 8> vector_low;
};

std::vector<std::uint8_t> MakeBionicJumpsFixture();

}  // namespace isomr::test
