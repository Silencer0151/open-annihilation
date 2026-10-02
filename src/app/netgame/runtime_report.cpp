// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The match report of a network game. Network play keeps no report of its
// own: it tells an extension, through the report hooks of its API
// (extension_api::Hooks), when the report starts, what happens to the
// match and when the report ends. The report rides with the network
// session and is used once the end-of-game screen's state exists.
#include "oa/app/runtime.hpp"
#include "network_play.hpp"
#include "launch_binding.hpp"
#include "net_state.hpp"

#include "oa/app/netgame/extension_api.hpp"

#include <cstddef>
#include <cstring>

namespace oa::app {

namespace {

namespace api = oa::app::netgame::extension_api;

// Bytes of the game name the match's Game keeps in session_description.
constexpr std::size_t kSessionNameBytes = 0x11;

} // namespace

oa::World* NetworkPlay::reporter_world() {
    if (runtime_.match_)
        return &runtime_.match_->state();
    return runtime_.endgame_world();
}

void NetworkPlay::start_reporter(oa::World& world, const oa::Game* lobby_game) {
    (void)runtime_.endgame_state();
    auto& state = *net_;
    if (lobby_game != nullptr && lobby_game != &world.game) {
        std::memcpy(
            world.game.provider_guid, lobby_game->provider_guid, sizeof world.game.provider_guid
        );
        auto* name = reinterpret_cast<char*>(world.game.session_description);
        const char* source = lobby_game->game_name;
        std::size_t length = 0;
        while (length + 1 < kSessionNameBytes && source[length] != '\0') {
            name[length] = source[length];
            ++length;
        }
        name[length] = '\0';
    }
    const api::Hooks& hooks = extension_hooks();
    if (hooks.report_start != nullptr)
        hooks.report_start(hooks.context, world, runtime_.game_options());
    state.report_started = true;
    report_game_event(api::report_event::battleroom_opened);
    report_game_event(api::report_event::game_launched);
}

void NetworkPlay::report_game_event(int32_t event) {
    if (!runtime_.endgame_ || !net_ || !net_->report_started)
        return;
    const api::Hooks& hooks = extension_hooks();
    if (hooks.report_event == nullptr)
        return;
    if (oa::World* world = reporter_world())
        hooks.report_event(hooks.context, *world, event);
}

void NetworkPlay::report_chat_line(const char* line) {
    reported_chat_ = line != nullptr ? line : "";
    if (!runtime_.endgame_ || !net_ || !net_->report_started)
        return;
    const api::Hooks& hooks = extension_hooks();
    if (hooks.report_chat == nullptr)
        return;
    if (oa::World* world = reporter_world())
        hooks.report_chat(hooks.context, *world, line);
}

void NetworkPlay::step_reporter_frame() {
    if (!runtime_.endgame_ || !net_ || !runtime_.match_ || !net_->report_started)
        return;
    const api::Hooks& hooks = extension_hooks();
    if (hooks.report_step != nullptr)
        hooks.report_step(hooks.context, runtime_.match_->state());
}

void NetworkPlay::close_reporter() {
    if (!runtime_.endgame_ || !net_ || !net_->report_started)
        return;
    report_game_event(api::report_event::session_closed);
    net_->report_started = false;
    const api::Hooks& hooks = extension_hooks();
    if (hooks.report_close != nullptr)
        hooks.report_close(hooks.context);
}

void NetworkPlay::release_match_report() {
    if (!runtime_.endgame_ || !net_)
        return;
    const api::Hooks& hooks = extension_hooks();
    if (hooks.report_release != nullptr)
        hooks.report_release(hooks.context);
}

} // namespace oa::app
