#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace isomr::test {

constexpr std::uint64_t kCandyEntryFixtureState = 0x3100;
constexpr std::uint64_t kCandyEntryFixtureWorkerFailure = 0x2000;

struct CandyEntryFixtureState {
    std::uint64_t call_count;
    std::uint64_t create_index;
    std::array<std::uint64_t, 3> create_arguments;
    std::uint64_t native_instance;
    std::uint64_t application_index;
    std::array<std::uint64_t, 5> application_arguments;
    std::uint64_t event_index_code;
    std::uint64_t event_action_code;
    std::uint64_t event_count;
    std::uint64_t event_environment;
    std::uint64_t event_object;
    std::uint64_t initialize_index;
    std::array<std::uint64_t, 7> initialize_arguments;
    std::uint64_t scale_index;
    std::array<std::uint64_t, 3> scale_arguments;
    std::uint64_t screen_index;
    std::array<std::uint64_t, 4> screen_arguments;
    std::uint64_t step_index;
    std::array<std::uint64_t, 3> step_arguments;
    std::uint64_t touch_index;
    std::array<std::uint64_t, 6> touch_arguments;
    std::uint64_t analytics_index;
    std::array<std::uint64_t, 4> analytics_arguments;
    std::uint64_t age_signals_index;
    std::array<std::uint64_t, 6> age_signals_arguments;
    std::uint64_t worker_ready;
    std::uint64_t shutting_down_index;
    std::array<std::uint64_t, 2> shutting_down_arguments;
    std::uint64_t destroy_index;
    std::array<std::uint64_t, 2> destroy_arguments;
};

std::vector<std::uint8_t> MakeCandyEntryFixture(
    bool omit_touch_export = false,
    std::uint16_t create_loop_count = 4);

}  // namespace isomr::test
