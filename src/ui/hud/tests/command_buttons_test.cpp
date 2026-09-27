// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "check.hpp"

#include "oa/ui/hud/command_buttons.hpp"

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace oa;
using namespace oa::ui::hud;

namespace {

struct Heard {
    std::vector<std::string> sounds;
    std::vector<std::string> orders;
};

HudEvents events_for(Heard& heard) {
    return {
        &heard,
        [](void* user, const char* name) { static_cast<Heard*>(user)->sounds.emplace_back(name); },
        [](void* user, const char* tag, int32_t value) {
            auto& orders = static_cast<Heard*>(user)->orders;
            orders.push_back(std::string(tag) + "=" + std::to_string(value));
        },
    };
}

struct Case {
    const char* name;
    uint8_t lit_order; // Game.pointer_command once the button is lit
    const char* sound;
};

// The ARMGEN.GUI and CORGEN.GUI command buttons, the order each
// arms in Game.pointer_command while lit (1 once turned off) and its
// allsound.tdf entry.
constexpr Case kCases[] = {
    {"ARMMOVE", 2, "immediateorders"},
    {"ARMATTACK", 3, "immediateorders"},
    {"CORBLAST", 4, "immediateorders"},
    {"ARMUNLOAD", 5, "specialorders"},
    {"CORLOAD", 6, "immediateorders"},
    {"ARMDEFEND", 7, "immediateorders"},
    {"CORREPAIR", 8, "specialorders"},
    {"ARMPATROL", 9, "immediateorders"},
    {"ARMRECLAIM", 0x0c, "specialorders"},
    {"CORCAPTURE", 0x0d, "specialorders"},
};

void lit_and_unlit() {
    auto game = std::make_unique<Game>();
    for (const auto& test : kCases) {
        Heard heard;
        game->pointer_command = 0xee;
        game->pointer_flags = 0xff;
        CHECK(order_panel_command(*game, test.name, 1, events_for(heard)));
        CHECK(armed_order_of(*game) == test.lit_order);
        CHECK(game->pointer_flags == 0xf7);
        CHECK(heard.sounds.size() == 1 && heard.sounds[0] == test.sound);
        CHECK(heard.orders.empty());
        // Clicking the lit button turns it off: the default order is armed.
        CHECK(order_panel_command(*game, test.name, 0, events_for(heard)));
        CHECK(armed_order_of(*game) == armed_order::default_order);
        CHECK(heard.sounds.size() == 2 && heard.sounds[1] == test.sound);
    }
}

// STOP arms the default order whatever its status and gives the group STOP.
void stop_gives_stop() {
    auto game = std::make_unique<Game>();
    for (const int16_t status : {0, 1}) {
        Heard heard;
        game->pointer_command = armed_order::attack;
        CHECK(order_panel_command(*game, "ARMSTOP", status, events_for(heard)));
        CHECK(armed_order_of(*game) == armed_order::default_order);
        CHECK(heard.orders.size() == 1 && heard.orders[0] == "STOP=0");
        CHECK(heard.sounds.size() == 1 && heard.sounds[0] == kImmediateOrdersSound);
    }
}

// Other names are left to the build buttons; the match is case sensitive.
void other_names() {
    auto game = std::make_unique<Game>();
    Heard heard;
    game->pointer_command = armed_order::patrol;
    CHECK(!order_panel_command(*game, "ARMSOLAR", 1, events_for(heard)));
    CHECK(!order_panel_command(*game, "armattack", 1, events_for(heard)));
    CHECK(!order_panel_command(*game, nullptr, 1, events_for(heard)));
    CHECK(armed_order_of(*game) == armed_order::patrol && heard.sounds.empty());
}

} // namespace

int main() {
    lit_and_unlit();
    stop_gives_stop();
    other_names();
    return 0;
}
