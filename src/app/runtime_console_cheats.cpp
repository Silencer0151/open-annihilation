// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The console's cheat gate against the running app: the flag each mission
// start sets, the chat line's command mask, where a cheat's echo goes and
// the Game Settings sheet's Cheat Codes row.
#include "oa/app/runtime.hpp"

#include "oa/data/campaign/campaign_file.hpp"
#include "oa/sim/scenario/commander_rules.hpp"
#include "oa/sim/match_runtime.hpp"
#include "oa/ui/console/console.hpp"
#include "oa/ui/console/game_fields.hpp"
#include "oa/ui/frontend/ingame_menu.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace oa::app {

namespace console = oa::ui::console;

namespace {

constexpr float kAtmMetal = 1000.0F;

void require(bool ok, const std::string& what) {
    if (!ok)
        throw std::runtime_error("console cheat check: " + what);
}

// "<name> " as the chat formatter starts a line the local player sends.
std::string speaker_prefix(const oa::World& world) {
    const auto& name = world.game.players[world.game.local_player_index].second_name;
    return "<" + std::string(name, strnlen(name, sizeof name)) + "> ";
}

bool last_line_is(const std::vector<std::string>& lines, std::string_view text) {
    return !lines.empty() && lines.back() == text;
}

bool logged(const std::vector<std::string>& lines, std::string_view text) {
    return std::find(lines.begin(), lines.end(), text) != lines.end();
}

bool developer(const oa::Game& game) {
    return (console::console_flags(game) & console::console_flag::developer) != 0;
}

} // namespace

void Runtime::check_console_skirmish_cheats(const std::function<void(const char*)>& enter_line) {
    require(
        match_session_kind() == oa::data::campaign::SessionKind::skirmish &&
            session_cheats_allowed_,
        "the skirmish start did not allow cheats"
    );
    oa::World& world = match_->state();
    oa::Game& game = world.game;
    require(!developer(game), "the developer passphrase is already set");
    const console::Console* con = match_console();
    require(
        con != nullptr && console::console_chat_mask(con) ==
                              (console::command_class::option | console::command_class::cheat |
                               console::command_class::private_echo),
        "the skirmish chat line does not run options and cheats"
    );
    const auto prefix = speaker_prefix(world);
    const auto viewer = game.viewpoint_player;
    const float metal = game.players[viewer].metal;
    const auto mode = game.chat_mode;
    enter_line("+atm");
    require(game.players[viewer].metal == metal + kAtmMetal, "+atm did not add 1000 metal");
    require(last_line_is(match_message_lines(), prefix + "+atm"), "+atm was not echoed");
    require(game.chat_mode == mode, "the cheat's echo left its chat mode set");
    enter_line("+xyzzy");
    require(
        last_line_is(match_message_lines(), prefix + "+xyzzy"),
        "an unknown +word was not sent as chat"
    );

    show_match_pause_menu();
    activate_pause_gadget("MISSION");
    require(match_hud_.has_value(), "MISSION left no panel");
    std::vector<std::string> rows;
    for (const auto& gadget : match_hud_->layout.gadgets)
        if (const auto* label = std::get_if<oa::ui::gui_layout::LabelFields>(&gadget.fields))
            rows.push_back(label->text);
    require(
        logged(rows, "Difficulty:") && logged(rows, "Max Units:") && !logged(rows, "Cheat Codes:"),
        "MISSION did not open the skirmish's Game Settings sheet"
    );
    renderer::Surface sheet;
    compose_match_frame(sheet);
    const fs::path report_directory = "local/reports";
    fs::create_directories(report_directory);
    write_ppm(report_directory / "native-game-settings.ppm", sheet);
    activate_pause_gadget("OK");
    require(
        match_paused_ && match_hud_.has_value() &&
            std::any_of(
                match_hud_->layout.gadgets.begin(),
                match_hud_->layout.gadgets.end(),
                [](const auto& gadget) { return gadget.common.name == "MISSION"; }
            ),
        "the sheet's OK did not return to the options panel"
    );
    resume_match_pause();
    std::cout << "console cheat check: a skirmish runs +atm and echoes it to everyone, sends an "
                 "unknown +word as chat and shows its Game Settings without Cheat Codes\n";
}

void Runtime::check_console_campaign_cheats() {
    require(
        match_session_kind() == oa::data::campaign::SessionKind::campaign &&
            !session_cheats_allowed_,
        "the campaign start allowed cheats"
    );
    oa::World& world = match_->state();
    oa::Game& game = world.game;
    require(!developer(game), "the developer passphrase is already set");
    const console::Console* con = match_console();
    require(
        con != nullptr && (console::console_chat_mask(con) & console::command_class::cheat) == 0,
        "the campaign chat line runs cheats"
    );
    const auto prefix = speaker_prefix(world);
    const auto viewer = game.viewpoint_player;
    const float metal = game.players[viewer].metal;
    enter_console_check_line("+atm");
    require(game.players[viewer].metal == metal, "a refused +atm changed the metal");
    require(
        last_line_is(match_message_lines(), prefix + "+atm"), "a refused +atm was not sent as chat"
    );
    const auto clock = console::console_flags(game) & console::console_flag::clock;
    enter_console_check_line("+clock");
    require(
        (console::console_flags(game) & console::console_flag::clock) != clock,
        "+clock did not run in a campaign"
    );
    enter_console_check_line("+clock");
    enter_console_check_line("+Now Film Chris Include Reload Assert");
    require(developer(game), "the passphrase was refused in a campaign");
    enter_console_check_line("+atm");
    require(
        game.players[viewer].metal == metal + kAtmMetal, "+atm did not run after the passphrase"
    );
    enter_console_check_line("+Now");
    require(!developer(game), "+Now did not clear the passphrase");
    enter_console_check_line("+atm");
    require(game.players[viewer].metal == metal + kAtmMetal, "+atm ran after +Now");
    std::cout << "console cheat check: a campaign refuses +atm and sends it as chat, runs "
                 "+clock, and runs +atm after the passphrase until +Now\n";
}

} // namespace oa::app
