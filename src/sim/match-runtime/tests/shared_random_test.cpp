// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The shared random stream every unit script and simulation draw consumes:
// for three match seeds, the seeded state, the first draws and the state
// after a thousand draws must equal the values pinned below on every
// platform. Savegames and multiplayer games carry this state, so a change
// here breaks both.
#include "oa/sim/match_runtime/script.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <string>

namespace {

using oa::sim::match_runtime::SharedRandom;

constexpr size_t pinned_draws = 16;
constexpr uint32_t long_run_draws = 1000;
// The widest bound a draw accepts: every state the stream reaches is below it.
constexpr uint32_t full_bound = 0x7fffffffu;
constexpr uint32_t die_bound = 6;

struct PinnedStream {
    uint32_t seed{};
    uint32_t initial_state{};                        // state() before any draw
    std::array<uint32_t, pinned_draws> full_draws{}; // bounded(full_bound), in order
    std::array<uint32_t, pinned_draws> die_draws{};  // bounded(die_bound) from a fresh stream
    uint32_t state_after_long_run{};                 // state() after long_run_draws full draws
};

// The third seed scrambles to a state with the top bit set.
constexpr std::array<PinnedStream, 3> pinned{{
    {
        .seed = 1,
        .initial_state = 0x66e29573,
        .full_draws =
            {0x25c1e5ca,
             0x5cd54423,
             0x356c8572,
             0x67ad1ac4,
             0x15b86f09,
             0x7f81c302,
             0x2029f9b4,
             0x23c8a8eb,
             0x4ac9eea7,
             0x0f533c4d,
             0x219beb17,
             0x0367443e,
             0x70b14430,
             0x05f3e51d,
             0x4f46d5f8,
             0x3389b571},
        .die_draws = {4, 3, 0, 0, 1, 0, 4, 5, 5, 1, 5, 2, 0, 5, 4, 5},
        .state_after_long_run = 0x3953c51f,
    },
    {
        .seed = 2,
        .initial_state = 0x66e29571,
        .full_draws =
            {0x25c1627c,
             0x3b28ce41,
             0x73fd2bbe,
             0x7243026f,
             0x0d550004,
             0x47740d72,
             0x141edc04,
             0x75fe94ed,
             0x1ce39020,
             0x200b33b1,
             0x3f6eb9e6,
             0x7e6ecb92,
             0x1bf71f17,
             0x7d1a2d58,
             0x41972c92,
             0x2bef46e2},
        .die_draws = {2, 1, 4, 3, 0, 2, 4, 3, 2, 1, 0, 0, 1, 2, 4, 2},
        .state_after_long_run = 0x7b0f8c02,
    },
    {
        .seed = 0x9e3779b9,
        .initial_state = 0xf8d5eccb,
        .full_draws =
            {0x24ad830e,
             0x7b771ef1,
             0x49909f8a,
             0x35da42c1,
             0x0c54a886,
             0x0a03fbbd,
             0x0b843a6e,
             0x161811aa,
             0x062fbb3b,
             0x27ad21a9,
             0x577cf298,
             0x4c1b0607,
             0x0228dc9a,
             0x48ab0391,
             0x537749dc,
             0x3c923553},
        .die_draws = {0, 3, 4, 5, 2, 3, 2, 2, 5, 3, 2, 5, 2, 1, 0, 3},
        .state_after_long_run = 0x310bac49,
    },
}};

int failures = 0;

/// Records a failed check.
///
/// @param held whether the check held
/// @param what description printed when it did not
void check(bool held, const std::string& what) {
    if (!held) {
        std::cerr << "FAIL: " << what << '\n';
        ++failures;
    }
}

/// Formats a sequence as a braced list of hex constants.
///
/// @param values the sequence
/// @return the text
std::string listing(const std::array<uint32_t, pinned_draws>& values) {
    std::string out = "{";
    char word[16];
    for (size_t i = 0; i < values.size(); ++i) {
        std::snprintf(word, sizeof word, "%s0x%08x", i == 0 ? "" : ", ", values[i]);
        out += word;
    }
    return out + "}";
}

/// Checks one seed's stream against its pinned states and draws.
///
/// @param expected the seed and its pinned values
void stream_matches(const PinnedStream& expected) {
    const std::string seed = std::to_string(expected.seed);
    SharedRandom full(expected.seed);
    check(
        full.state() == expected.initial_state,
        "seed " + seed + " initial state " + std::to_string(full.state())
    );
    std::array<uint32_t, pinned_draws> draws{};
    for (auto& draw : draws)
        draw = full.bounded(full_bound);
    check(draws == expected.full_draws, "seed " + seed + " full draws " + listing(draws));
    for (uint32_t i = static_cast<uint32_t>(pinned_draws); i < long_run_draws; ++i)
        (void)full.bounded(full_bound);
    check(
        full.state() == expected.state_after_long_run,
        "seed " + seed + " state after " + std::to_string(long_run_draws) + " draws " +
            std::to_string(full.state())
    );

    SharedRandom die(expected.seed);
    for (auto& draw : draws)
        draw = die.bounded(die_bound);
    check(draws == expected.die_draws, "seed " + seed + " die draws " + listing(draws));
}

/// Checks that seeding forces the state odd, so seeds 0 and 1 give one stream.
void even_seed_joins_odd() {
    check(SharedRandom(0).state() == SharedRandom(1).state(), "seeds 0 and 1 share a state");
}

/// Checks that bounds below 2, read as signed, return 0 without advancing the stream.
void small_bounds_do_not_draw() {
    SharedRandom random(1);
    const uint32_t before = random.state();
    for (const uint32_t bound : {0u, 1u, 0x80000000u, 0xffffffffu})
        check(random.bounded(bound) == 0, "bound " + std::to_string(bound) + " returns 0");
    check(random.state() == before, "small bounds leave the state");
}

} // namespace

int main() {
    for (const auto& stream : pinned)
        stream_matches(stream);
    even_seed_joins_odd();
    small_bounds_do_not_draw();
    if (failures != 0)
        return 1;
    std::cout << "shared random stream passed\n";
    return 0;
}
