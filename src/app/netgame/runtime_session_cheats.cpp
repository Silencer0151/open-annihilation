// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The console's cheat gate in a network game, against the loopback peer: the
// host's CHEATING option on both machines, the Game Settings sheet's Cheat
// Codes row, and where an accepted cheat's line goes.
#include "oa/app/runtime.hpp"
#include "network_play.hpp"

#include "oa/sim/scenario/commander_rules.hpp"
#include "oa/sim/match_runtime.hpp"
#include "oa/ui/console/console.hpp"
#include "oa/ui/frontend/ingame_menu.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace oa::app {

namespace {

constexpr int kSessionWaitSteps = 200;

/// Fails the console cheat check unless a condition holds.
///
/// @param ok Condition that must hold.
/// @param what Failure text; thrown as std::runtime_error "console cheat check: <what>".
void require(bool ok, const std::string& what) {
    if (!ok)
        throw std::runtime_error("console cheat check: " + what);
}

/// Returns the "<name> " that starts a chat line the local player sends.
///
/// @param world Match world whose local player speaks.
/// @return The prefix, with the player's second name.
std::string speaker_prefix(const oa::World& world) {
    const auto& name = world.game.players[world.game.local_player_index].second_name;
    std::string prefix = "<";
    prefix.append(name, strnlen(name, sizeof name));
    prefix += "> ";
    return prefix;
}

/// Tests whether a message log holds a line exactly.
///
/// @param lines Message log lines.
/// @param text Line looked for.
/// @return Whether the line is logged.
bool logged(const std::vector<std::string>& lines, std::string_view text) {
    return std::find(lines.begin(), lines.end(), text) != lines.end();
}

/// Returns the value row that follows a label row of the Game Settings sheet.
///
/// @param sheet Game Settings sheet.
/// @param label Label row text, such as "Cheat Codes:".
/// @return The value row's text, or null when the label is absent or last.
const char*
settings_value(const oa::ui::frontend::GameSettingsSheet& sheet, std::string_view label) {
    for (std::size_t i = 0; i + 1 < sheet.count; ++i)
        if (label == sheet.entries[i].text.data())
            return sheet.entries[i + 1].text.data();
    return nullptr;
}

} // namespace

void NetworkPlay::check_console_session_cheats(
    Runtime& peer, const std::function<void(bool)>& step
) {
    require(
        runtime_.match_session_kind() == oa::data::campaign::SessionKind::multiplayer &&
            peer.match_session_kind() == oa::data::campaign::SessionKind::multiplayer,
        "the loopback is not a multiplayer session"
    );
    oa::World& world = runtime_.match_->state();
    oa::Game& game = world.game;
    const auto host = sim::scenario::host_player_index(world);
    const auto* host_info =
        host != OA_PLAYER_COUNT ? oa::world_player_info(&world, &game.players[host]) : nullptr;
    require(host_info != nullptr, "no host is seated");
    const bool allowed = (host_info->options & OA_SETUP_OPTION_CHEATS_ALLOWED) != 0;
    require(
        runtime_.session_cheats_allowed_ == allowed && peer.session_cheats_allowed_ == allowed,
        "a machine's cheat flag is not the host's CHEATING option"
    );
    for (Runtime* side : {&runtime_, &peer}) {
        oa::ui::frontend::GameSettingsSheet sheet;
        oa::ui::frontend::ingame_build_game_settings(side->game_settings_view(), sheet);
        const char* shown = settings_value(sheet, "Cheat Codes:");
        require(
            shown != nullptr && std::string_view(shown) == (allowed ? "Allowed" : "Disallowed") &&
                settings_value(sheet, "Difficulty:") == nullptr,
            "the Game Settings sheet does not show the host's CHEATING option"
        );
    }
    const auto wait = [&](const auto& done, const char* what) {
        for (int i = 0; i < kSessionWaitSteps && !done(); ++i)
            step(true);
        require(done(), what);
    };
    const auto prefix = speaker_prefix(world);
    const std::string atm = prefix + "+atm";
    require(reported_chat_ != prefix + "+clock", "an option command's echo was reported");
    // Each console adds its ATM amount: 1000, or a mod profile's.
    const auto atm_amount = [](const Runtime& side) {
        const auto* profile = side.mod_profile();
        return oa::ui::console::console_atm_amount(
            profile != nullptr ? profile->rules : oa::data::match_rules::MatchRules{}
        );
    };
    const auto viewer = game.viewpoint_player;
    const float metal = game.players[viewer].metal;
    runtime_.enter_console_check_line("+atm");
    require(
        game.players[viewer].metal == metal + (allowed ? atm_amount(runtime_) : 0.0F),
        allowed ? "+atm did not run with cheats allowed" : "+atm ran with cheats disallowed"
    );
    require(reported_chat_ == atm, "the +atm line was not reported");
    wait(
        [&] { return logged(peer.match_message_lines(), atm); },
        "the +atm line did not reach the peer"
    );

    if (peer.local_player_watches()) {
        bool running = true;
        SDL_Event enter{};
        enter.type = SDL_EVENT_KEY_DOWN;
        enter.key.key = SDLK_RETURN;
        enter.key.scancode = SDL_SCANCODE_RETURN;
        peer.handle_sdl_event(enter, running);
        require(!peer.chat_composing_, "Enter opened the watcher's chat line");
    } else {
        oa::Game& peer_game = peer.match_->state().game;
        const auto peer_viewer = peer_game.viewpoint_player;
        const float peer_metal = peer_game.players[peer_viewer].metal;
        const std::string peer_atm = speaker_prefix(peer.match_->state()) + "+atm";
        peer.enter_console_check_line("+atm");
        require(
            peer_game.players[peer_viewer].metal ==
                peer_metal + (allowed ? atm_amount(peer) : 0.0F),
            "the peer's +atm did not follow the host's CHEATING option"
        );
        wait(
            [&] { return logged(runtime_.match_message_lines(), peer_atm); },
            "the peer's +atm line did not reach this machine"
        );
    }
    std::cout << "console cheat check: both machines take the host's CHEATING option ("
              << (allowed ? "allowed" : "disallowed") << "), +atm "
              << (allowed ? "ran" : "was refused") << " and its line reached the peer and the "
              << "match report\n";
}

} // namespace oa::app
