// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The observer pulse in the running match: while BigBrother runs, every 90
// ticks of the unit sweep move the selection to the viewpoint player's next
// selectable unit, unless shift is held.
#include "combat_fixture.hpp"

#include <cstdint>
#include <iostream>

namespace {

using namespace combat_fixture;

constexpr uint8_t periodic_observer = 0x02; // Game.periodic_flags
constexpr int16_t pulse_ticks = 90;

struct Keyboard {
    bool shift = false;
    int cleared = 0;
};

bool selected(const sim::unit_spawn::Slot& slot) {
    return (slot.record.flags & OA_UNIT_FLAG_SELECTED) != 0;
}

void pulse_moves_the_selection() {
    Fixture f;
    Keyboard keys;
    f.match->observer = {
        &keys,
        [](void* context) { return static_cast<Keyboard*>(context)->shift; },
        [](void* context) { ++static_cast<Keyboard*>(context)->cleared; },
    };
    auto& first = f.spawn(0, 64, 64);
    auto& second = f.spawn(0, 96, 64);
    CHECK(first.unit_index < second.unit_index);
    auto& game = f.match->state().game;
    game.viewpoint_player = 0;
    game.periodic_flags = static_cast<uint8_t>(game.periodic_flags | periodic_observer);
    game.periodic_countdown = 1;
    // Nothing selected: the first selectable unit takes the selection.
    f.run(1);
    CHECK(selected(first) && !selected(second) && keys.cleared == 0);
    CHECK(game.periodic_countdown == pulse_ticks);
    f.run(pulse_ticks - 1);
    CHECK(selected(first) && game.periodic_countdown == 1);
    // The next pulse drops the selection and moves on.
    f.run(1);
    CHECK(!selected(first) && selected(second) && keys.cleared == 1);
    // The last unit wraps to the first.
    game.periodic_countdown = 1;
    f.run(1);
    CHECK(selected(first) && !selected(second) && keys.cleared == 2);
    // Shift held: no countdown and no pulse.
    keys.shift = true;
    game.periodic_countdown = 1;
    f.run(1);
    CHECK(selected(first) && game.periodic_countdown == 1 && keys.cleared == 2);
    std::cout << "observer pulse moves the selection passed\n";
}

} // namespace

int main() {
    try {
        pulse_moves_the_selection();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
