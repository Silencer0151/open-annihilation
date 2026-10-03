// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// A stage's state while its match runs (Runtime::StageState): where each
// player's first unit stood when the stage began, the lines timed for later
// ticks, the groups its placements formed and the last unit placed.
// runtime_benchmark.cpp reads the stage file and carries out its lines;
// runtime_stage.cpp keeps this state, runs the timed lines as their ticks
// come and carries out the actions that place units by map pixel, gather
// them into groups and give the groups orders.
#pragma once

#include "oa/app/runtime.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace oa::app {

struct Runtime::StageState {
    /// A unit of a group: its slot, and the owner and type it had when it
    /// joined, so that a later unit in the same slot is not taken for it.
    struct Member {
        uint16_t unit{};
        uint8_t owner{};
        uint16_t type{};
    };

    /// A line that runs on a later tick ("at TICK ACTION").
    struct TimedLine {
        uint32_t tick{}; ///< the match tick the line runs before
        StageLine line{};
    };

    /// Each player's first unit, where it stood when the stage began: map
    /// pixels across and the map-image row, keyed by player.
    std::map<int32_t, std::pair<int32_t, int32_t>> origins{};
    /// The lines timed for later ticks, in tick order and, within a tick, in
    /// file order.
    std::vector<TimedLine> timed{};
    std::size_t next_timed{}; ///< the first line of `timed` not yet run
    /// The groups by name, each member in the order it joined.
    std::map<std::string, std::vector<Member>> groups{};
    std::string group{};  ///< the group the units placed now join; empty for none
    uint16_t last_unit{}; ///< the last unit placed; 0 before any
};

} // namespace oa::app
