// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The in-game console the runtime keeps for the match it is bound to.
#pragma once

#include "oa/ui/console/console.hpp"

#include <cstdint>

namespace oa::app {

struct MatchConsole {
    oa::ui::console::Console state{};
    oa::ui::console::ConsoleHost host{};
    const oa::World* bound_world = nullptr;
    // Game.profiling as the last console line left it, carried from game
    // to game.
    int32_t profiling = 0;
};

} // namespace oa::app
