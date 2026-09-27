// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "check.hpp"

#include "oa/ui/hud/ingame_menu.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace oa;
using namespace oa::ui::hud;

namespace {

struct Panel {
    std::vector<std::string> names;
    std::vector<int32_t> values;
    std::vector<int32_t> states;

    Panel() {
        char name[32];
        for (int i = 0; i < OA_PLAYER_COUNT; ++i) {
            std::snprintf(name, sizeof name, "PLAYER%d", i);
            names.emplace_back(name);
            std::snprintf(name, sizeof name, "LIVEPLYR%d", i);
            names.emplace_back(name);
        }
        values.assign(names.size(), 7);
        states.assign(names.size(), -1);
    }

    static int32_t find(void* user, const char* name) {
        auto& self = *static_cast<Panel*>(user);
        for (std::size_t i = 0; i < self.names.size(); ++i)
            if (self.names[i] == name)
                return static_cast<int32_t>(i);
        return -1;
    }

    PanelControls controls() {
        return {
            this,
            find,
            nullptr,
            [](void* user, int32_t index, int32_t value) {
                static_cast<Panel*>(user)->values[static_cast<std::size_t>(index)] = value;
            },
            nullptr,
            [](void* user, int32_t index, int32_t state) {
                static_cast<Panel*>(user)->states[static_cast<std::size_t>(index)] = state;
            }
        };
    }

    int32_t live(int player) const { return states[static_cast<std::size_t>(player * 2 + 1)]; }
};

void test_chat_targets() {
    auto game = std::make_unique<Game>();
    game->local_player_index = 0;
    game->players[0].status = OA_PLAYER_STATUS_LOCAL;
    game->players[1].status = OA_PLAYER_STATUS_COMPUTER;
    game->players[2].status = OA_PLAYER_STATUS_MIRRORED;
    game->players[3].status = OA_PLAYER_STATUS_CLOSED;
    game->players[0].alliance[2] = 1;

    Panel panel;
    refresh_chat_targets(*game, panel.controls());
    CHECK(panel.values[0] == 0 && panel.values[2] == 0);
    CHECK(panel.live(0) == -1 && panel.live(3) == -1 && panel.live(4) == -1);
    CHECK(panel.live(1) == 1 && panel.live(2) == 1);

    game->chat_mode = static_cast<uint8_t>(ChatSendMode::allies);
    refresh_chat_targets(*game, panel.controls());
    CHECK(panel.live(1) == 0 && panel.live(2) == 1);
    game->chat_mode = static_cast<uint8_t>(ChatSendMode::enemies);
    refresh_chat_targets(*game, panel.controls());
    CHECK(panel.live(1) == 1 && panel.live(2) == 0);
    game->chat_mode = static_cast<uint8_t>(ChatSendMode::chosen);
    game->chat_targets[2] = 1;
    refresh_chat_targets(*game, panel.controls());
    CHECK(panel.live(1) == 0 && panel.live(2) == 1);
    CHECK(chat_send_mode(*game) == ChatSendMode::chosen && chat_chosen_target(*game, 2));
}

void test_menu_clicks() {
    std::vector<std::string> sounds;
    HudEvents events{
        &sounds,
        [](void* user, const char* name) {
            static_cast<std::vector<std::string>*>(user)->emplace_back(name);
        },
        nullptr
    };
    uint8_t gui = 0xff;
    uint16_t frame = 0;
    CHECK(ingame_menu_click("OPTIONS", gui, frame, events) == IngameMenuAction::options);
    CHECK((frame & kFrameIngameOptions) != 0 && sounds.back() == "BigButton");
    CHECK(ingame_menu_click("SHARE", gui, frame, events) == IngameMenuAction::share);
    CHECK(ingame_menu_click("CONTROL", gui, frame, events) == IngameMenuAction::control);
    CHECK(ingame_menu_click("ALLIES", gui, frame, events) == IngameMenuAction::allies);
    const auto count = sounds.size();
    CHECK(ingame_menu_click("CANCEL", gui, frame, events) == IngameMenuAction::cancel);
    CHECK(ingame_menu_click("OTHER", gui, frame, events) == IngameMenuAction::none);
    CHECK(sounds.size() == count);
    CHECK(ingame_menu_click(nullptr, gui, frame, events) == IngameMenuAction::closed);
    CHECK(gui == 0x1f);
}

} // namespace

int main() {
    test_chat_targets();
    test_menu_clicks();
    return 0;
}
