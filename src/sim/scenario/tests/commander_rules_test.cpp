// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/scenario/commander_rules.hpp"
#include "oa/sim/scenario/outcome.hpp"

#include "oa/core/player_setup.h"
#include "oa/core/player.h"

#include <cstdio>
#include <memory>

using namespace oa;
using namespace oa::sim::scenario;

namespace {
int failures = 0;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x);                           \
            ++failures;                                                                            \
        }                                                                                          \
    } while (false)

constexpr int32_t deathmatch_rules = static_cast<int32_t>(CommanderRule::deathmatch);

// Four players in a game, each on its own board row, each with its player-info record.
struct Game4 {
    std::unique_ptr<World> world = std::make_unique<World>();
    PlayerSetupInfo* infos = world->player_info;

    Game4() {
        for (uint8_t index = 0; index < OA_PLAYER_COUNT; ++index) {
            Player& player = world->game.players[index];
            player.index = index;
            player.board_row = index;
            player.info = oa_ref_from_index(index);
            if (index < 4) {
                player.in_use = 1;
                player.status = index == 0 ? OA_PLAYER_STATUS_LOCAL : OA_PLAYER_STATUS_MIRRORED;
                player.units_created = 5;
                player.unit_count = 3;
                player.alliance[index] = 1;
                player.allied_by[index] = 1;
            } else {
                player.index = 10;
            }
        }
    }

    Player& operator[](uint8_t index) { return world->game.players[index]; }
};

void board_score_follows_the_rule() {
    Game4 g;
    g[1].kills = 7;
    g[1].commanders_killed = 2;
    CHECK(board_score(g.world->game, g[1]) == 7);
    g.world->game.session_rules = deathmatch_rules;
    CHECK(board_score(g.world->game, g[1]) == 2);
}

void kill_board_promotion() {
    Game4 g;
    g[0].kills = 4;
    g[1].kills = 3;
    g[2].kills = 5;
    g[3].kills = 4;
    // Player 3 passes player 1 (lower score) but not player 0 (a tie).
    CHECK(!promote_on_kill_board(*g.world, g[3]));
    CHECK(g[0].board_row == 0 && g[3].board_row == 1 && g[1].board_row == 2 && g[2].board_row == 3);
    g[3].kills = 6;
    CHECK(promote_on_kill_board(*g.world, g[3]));
    CHECK(g[3].board_row == 0 && g[0].board_row == 1 && g[1].board_row == 2 && g[2].board_row == 3);
    CHECK(g[4].board_row == 4);
    // The top row does not move.
    g[3].kills = 9;
    CHECK(!promote_on_kill_board(*g.world, g[3]) && g[3].board_row == 0);

    // A watcher's row is not a target.
    Game4 w;
    w.infos[0].options = OA_SETUP_OPTION_WATCHER;
    w[2].kills = 3;
    CHECK(!promote_on_kill_board(*w.world, w[2]));
    CHECK(w[0].board_row == 0 && w[2].board_row == 1 && w[1].board_row == 2);

    // Deathmatch ranks by commander kills: kills alone do not move a player.
    Game4 d;
    d.world->game.session_rules = deathmatch_rules;
    d[1].kills = 50;
    CHECK(!promote_on_kill_board(*d.world, d[1]) && d[1].board_row == 1);
    d[1].commanders_killed = 1;
    CHECK(promote_on_kill_board(*d.world, d[1]) && d[1].board_row == 0 && d[0].board_row == 1);
}

void multiplayer_victory_rules() {
    Game4 g;
    // Everyone else is out of units.
    for (uint8_t index = 1; index < 4; ++index)
        g[index].unit_count = 0;
    CHECK(multiplayer_victory(*g.world));
    g.world->game.session_rules = deathmatch_rules;
    CHECK(!multiplayer_victory(*g.world));
    g.world->game.session_rules = 0;

    // A player that has not created any unit yet blocks it.
    g[2].units_created = 0;
    CHECK(!multiplayer_victory(*g.world));
    g[2].units_created = 5;

    // A player still in the game must be a mutual allied-victory ally of the
    // local player and allied with everyone left.
    g[1].unit_count = 2;
    CHECK(!multiplayer_victory(*g.world));
    g.infos[0].status = OA_SETUP_STATUS_ALLIED_VICTORY;
    g.infos[1].status = OA_SETUP_STATUS_ALLIED_VICTORY;
    g[0].alliance[1] = 1;
    g[0].allied_by[1] = 1;
    g[1].alliance[0] = 1;
    CHECK(multiplayer_victory(*g.world));
    g[0].allied_by[1] = 0;
    CHECK(!multiplayer_victory(*g.world));
    g[0].allied_by[1] = 1;
    g[1].alliance[0] = 0;
    CHECK(!multiplayer_victory(*g.world));
    g[1].alliance[0] = 1;
    g.infos[1].status = 0;
    CHECK(!multiplayer_victory(*g.world));
    g.infos[1].status = OA_SETUP_STATUS_ALLIED_VICTORY;
    g.world->game.session_rules = deathmatch_rules;
    CHECK(!multiplayer_victory(*g.world));
}

void deathmatch_respawns_instead_of_defeat() {
    OutcomeState state;
    CHECK(
        advance_outcome(state, false, false, true, true, true) == Outcome::ongoing &&
        state.countdown == 4
    );
    for (int i = 0; i < 4; ++i)
        CHECK(advance_outcome(state, false, false, true, true, true) == Outcome::ongoing);
    CHECK(advance_outcome(state, false, false, true, true, true) == Outcome::respawn);
    CHECK(state.countdown == -1 && state.flags == 0);
    // The next defeat sample starts a fresh countdown.
    CHECK(
        advance_outcome(state, false, false, true, true, true) == Outcome::ongoing &&
        state.countdown == 4
    );

    // Victory still ends a deathmatch skirmish; campaigns ignore the rule.
    OutcomeState won;
    for (int i = 0; i < 5; ++i)
        CHECK(advance_outcome(won, false, true, false, true, true) == Outcome::ongoing);
    CHECK(advance_outcome(won, false, true, false, true, true) == Outcome::victory);
    OutcomeState campaign;
    for (int i = 0; i < 5; ++i)
        CHECK(advance_outcome(campaign, true, false, true, true, true) == Outcome::ongoing);
    CHECK(advance_outcome(campaign, true, false, true, true, true) == Outcome::defeat);
}

// A multiplayer player's expired defeat countdown becomes watching
// when nothing rejected it and the host allows watching or one of its
// computer players still plays; deathmatch respawns first.
void defeated_player_may_watch() {
    Game4 g;
    g.infos[1].role = 0x01; // the host
    CHECK(computer_participants(*g.world) == 0 && !defeated_player_watches(*g.world));
    g.infos[1].options = OA_SETUP_OPTION_WATCHING_ALLOWED;
    CHECK(defeated_player_watches(*g.world));
    g[0].reject_reason = 1;
    CHECK(!defeated_player_watches(*g.world));
    g[0].reject_reason = 0;
    g.infos[1].options = 0;
    g[3].status = OA_PLAYER_STATUS_COMPUTER;
    CHECK(computer_participants(*g.world) == 1 && defeated_player_watches(*g.world));
    g[3].unit_count = 0; // built units and lost them all
    CHECK(computer_participants(*g.world) == 0 && !defeated_player_watches(*g.world));

    OutcomeState state;
    for (int i = 0; i < 5; ++i)
        CHECK(advance_outcome(state, false, false, true, true, false, true) == Outcome::ongoing);
    CHECK(advance_outcome(state, false, false, true, true, false, true) == Outcome::watch);
    CHECK(state.countdown == -1 && state.flags == 0);
    OutcomeState deathmatch;
    for (int i = 0; i < 5; ++i)
        CHECK(
            advance_outcome(deathmatch, false, false, true, true, true, true) == Outcome::ongoing
        );
    CHECK(advance_outcome(deathmatch, false, false, true, true, true, true) == Outcome::respawn);
    // A victory is not turned into watching.
    OutcomeState won;
    for (int i = 0; i < 5; ++i)
        CHECK(advance_outcome(won, false, true, false, true, false, true) == Outcome::ongoing);
    CHECK(advance_outcome(won, false, true, false, true, false, true) == Outcome::victory);
}

void abandoned_game_countdown() {
    Game4 g;
    // Player 0 is local; players simulated elsewhere count when seated as humans.
    CHECK(connected_participants(*g.world) == 1);
    g.infos[1].state = OA_PLAYER_STATUS_LOCAL;
    g.infos[2].state = OA_PLAYER_STATUS_LOCAL;
    CHECK(connected_participants(*g.world) == 3);
    g.infos[2].options = OA_SETUP_OPTION_WATCHER;
    g[1].unit_count = 0;
    CHECK(connected_participants(*g.world) == 1);
    g[0].status = OA_PLAYER_STATUS_COMPUTER;
    CHECK(connected_participants(*g.world) == 0);

    OutcomeState state;
    CHECK(advance_abandoned_outcome(state, false, 1) == Outcome::ongoing && state.countdown == -1);
    CHECK(advance_abandoned_outcome(state, false, 0) == Outcome::ongoing && state.countdown == 4);
    for (int i = 0; i < 4; ++i)
        CHECK(advance_abandoned_outcome(state, false, 0) == Outcome::ongoing);
    state.flags = outcome_flag::won;
    CHECK(advance_abandoned_outcome(state, false, 0) == Outcome::defeat);
    CHECK(state.flags == (outcome_flag::finished | outcome_flag::defeat_transition));

    // Deathmatch games never end this way.
    OutcomeState deathmatch_state;
    for (int i = 0; i < 8; ++i)
        CHECK(advance_abandoned_outcome(deathmatch_state, true, 0) == Outcome::ongoing);
    CHECK(deathmatch_state.countdown == -1 && deathmatch_state.flags == 0);
}

// Mission start: on for a campaign and a skirmish, whatever came before, and
// for a multiplayer game bit 0x2000 of the options of the first seated player
// with role bit 0. 3.1c turns it off for a campaign; the engine differs on
// purpose.
void session_cheat_flag() {
    using oa::data::campaign::SessionKind;
    Game4 g;
    g.infos[0].options = OA_SETUP_OPTION_CHEATS_ALLOWED;
    CHECK(session_cheats_allowed(SessionKind::campaign, *g.world, false));
    CHECK(session_cheats_allowed(SessionKind::skirmish, *g.world, false));
    // A single-player game ignores every player's CHEATING option.
    g.infos[0].options = 0;
    CHECK(session_cheats_allowed(SessionKind::campaign, *g.world, false));
    CHECK(session_cheats_allowed(SessionKind::skirmish, *g.world, false));
    g.infos[0].options = OA_SETUP_OPTION_CHEATS_ALLOWED;
    // No host seated: the flag stays on.
    CHECK(session_cheats_allowed(SessionKind::multiplayer, *g.world, false));
    g.infos[2].role = 0x01;
    CHECK(!session_cheats_allowed(SessionKind::multiplayer, *g.world, true));
    g.infos[2].options = OA_SETUP_OPTION_CHEATS_ALLOWED | OA_SETUP_OPTION_WATCHER;
    CHECK(session_cheats_allowed(SessionKind::multiplayer, *g.world, false));
    // A free slot's record is passed over; the next host decides.
    g.infos[1].role = 0x01;
    g[1].status = OA_PLAYER_STATUS_FREE;
    CHECK(session_cheats_allowed(SessionKind::multiplayer, *g.world, false));
    g[1].status = OA_PLAYER_STATUS_MIRRORED;
    CHECK(!session_cheats_allowed(SessionKind::multiplayer, *g.world, true));
    CHECK(session_cheats_allowed(SessionKind::none, *g.world, true));
    CHECK(!session_cheats_allowed(SessionKind::none, *g.world, false));
    // The local player as host is held to its own option like any client.
    g.infos[0].role = 0x01;
    g.infos[0].options = 0;
    CHECK(!session_cheats_allowed(SessionKind::multiplayer, *g.world, true));
}
} // namespace

int main() {
    session_cheat_flag();
    board_score_follows_the_rule();
    kill_board_promotion();
    multiplayer_victory_rules();
    deathmatch_respawns_instead_of_defeat();
    abandoned_game_countdown();
    defeated_player_may_watch();
    if (failures != 0) {
        std::fprintf(stderr, "%d failures\n", failures);
        return 1;
    }
    std::puts("commander rule tests passed");
    return 0;
}
