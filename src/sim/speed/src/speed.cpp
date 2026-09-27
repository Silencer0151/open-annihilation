// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/speed.hpp"

#include <cstdio>

namespace oa::sim::speed {

void format_message(
    char (&out)[message_bytes], int32_t speed, const messages::Hooks& hooks
) noexcept {
    const int32_t offset = speed - normal;
    if (offset == 0) {
        std::snprintf(out, sizeof out, "%s", messages::translate(hooks, text_normal));
        return;
    }
    std::snprintf(
        out,
        sizeof out,
        "%s  %c%d\n",
        messages::translate(hooks, text_prefix),
        offset > 0 ? '+' : ' ',
        offset
    );
}

uint16_t set_speed(World& world, int32_t speed, const messages::Hooks& hooks) {
    Game& game = world.game;
    if (speed > fastest)
        speed = fastest;
    if (speed < slowest)
        speed = slowest;
    if (speed != game.requested_speed) {
        char line[message_bytes];
        format_message(line, speed, hooks);
        messages::post_message(world, line, messages::kind_status, 0, messages::sender_none, hooks);
    }
    game.requested_speed = static_cast<uint16_t>(speed);
    game.current_speed = static_cast<uint16_t>(speed);
    return game.requested_speed;
}

uint16_t raise_speed(World& world, const messages::Hooks& hooks) {
    if (world.game.requested_speed < fastest)
        set_speed(world, world.game.requested_speed + 1, hooks);
    return world.game.requested_speed;
}

uint16_t lower_speed(World& world, const messages::Hooks& hooks) {
    if (world.game.requested_speed > slowest)
        set_speed(world, world.game.requested_speed - 1, hooks);
    return world.game.requested_speed;
}

} // namespace oa::sim::speed
