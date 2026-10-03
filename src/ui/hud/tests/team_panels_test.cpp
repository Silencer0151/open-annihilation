// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The in-game team panels of a multiplayer game over a synthetic World: the
// tab menu's buttons, ALLIES.GUI's rows, alliances and allied victory,
// CONTROL.GUI's watching and removals, the removal question, what each
// tells the other players' machines through TeamPanelHost, and when a
// tournament game withholds CONTROL.
#include "check.hpp"
#include "fixtures.hpp"

#include "oa/ui/hud/chat_panel.hpp"
#include "oa/ui/hud/ingame_menu.hpp"
#include "oa/ui/hud/team_panels.hpp"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

using namespace oa;
using namespace oa::ui::hud;

namespace {

/// Records what the panels tell the other players' machines.
struct Machines {
    std::vector<std::string> log;

    TeamPanelHost host() {
        TeamPanelHost h{};
        h.context = this;
        h.alliance_changed = [](void* c, uint8_t from, uint8_t to, uint8_t allied) {
            static_cast<Machines*>(c)->log.push_back(
                "ally " + std::to_string(from) + ">" + std::to_string(to) + "=" +
                std::to_string(allied)
            );
        };
        h.setup_changed = [](void* c) { static_cast<Machines*>(c)->log.emplace_back("setup"); };
        h.remove_player = [](void* c, uint8_t player, uint8_t reason) {
            static_cast<Machines*>(c)->log.push_back(
                "remove " + std::to_string(player) + " " + std::to_string(reason)
            );
        };
        h.game_changed = [](void* c) { static_cast<Machines*>(c)->log.emplace_back("game"); };
        return h;
    }
};

/// A four-player game: the local player 0, a computer player 1 and two
/// players on another machine, 2 and 3; nobody on a team yet.
struct Game4 {
    hud_test::TestWorld world;

    Game4() {
        world.add_player(0, OA_PLAYER_STATUS_LOCAL);
        world.add_player(1, OA_PLAYER_STATUS_COMPUTER);
        world.add_player(2, OA_PLAYER_STATUS_MIRRORED);
        world.add_player(3, OA_PLAYER_STATUS_MIRRORED);
        for (uint8_t index = 0; index < OA_PLAYER_COUNT; ++index) {
            world.player(index).team = OA_PLAYER_NO_TEAM;
            info(index).color = index;
        }
        world.game().local_player_index = 0;
    }

    PlayerSetupInfo& info(uint8_t index) {
        return *world_player_info(world.world, &world.player(index));
    }
};

/// ALLIES.GUI's controls, as the panel file names them.
std::vector<std::string> allies_controls() {
    std::vector<std::string> names{"allies.GUI"};
    for (int row = 0; row < 9; ++row) {
        names.push_back("LOGO" + std::to_string(row));
        names.push_back("PLAYER" + std::to_string(row));
    }
    names.insert(names.end(), {"OK", "TEXT", "VICTORY"});
    for (int row = 0; row < 9; ++row)
        names.emplace_back("ALLYx");
    for (int row = 0; row < 9; ++row)
        names.emplace_back("TEAMICONSx");
    return names;
}

/// CONTROL.GUI's controls, as the panel file names them.
std::vector<std::string> control_controls() {
    std::vector<std::string> names{"control.GUI", "OK"};
    for (int row = 0; row < 9; ++row)
        names.push_back("PLAYER" + std::to_string(row));
    for (int row = 0; row < 9; ++row)
        names.push_back("LOGO" + std::to_string(row));
    names.insert(names.end(), {"REJECTTEXT", "WATCHING", "GAMEOPEN"});
    return names;
}

void test_host_role() {
    Game4 game;
    CHECK(!hosts_multiplayer_game(*game.world.world));
    game.info(2).role = 1;
    CHECK(!hosts_multiplayer_game(*game.world.world));
    game.info(2).role = 0;
    game.info(0).role = 1;
    CHECK(hosts_multiplayer_game(*game.world.world));
    // The first slot with the role decides.
    game.info(0).role = 0;
    game.info(1).role = 1;
    game.info(3).role = 1;
    CHECK(hosts_multiplayer_game(*game.world.world));
}

/// Answers TeamPanelHost::tournament_game with its context's flag, counting the calls.
struct Tournament {
    bool tournament = false;
    int asked = 0;

    TeamPanelHost host() {
        TeamPanelHost h{};
        h.context = this;
        h.tournament_game = [](void* c) {
            auto& self = *static_cast<Tournament*>(c);
            ++self.asked;
            return self.tournament;
        };
        return h;
    }
};

void test_control_offered() {
    Game4 game;
    Tournament answer;
    // A machine that does not host is never offered CONTROL, and the host
    // is not asked.
    CHECK(!control_offered(*game.world.world, TeamPanelHost{}));
    CHECK(!control_offered(*game.world.world, answer.host()));
    CHECK(answer.asked == 0);
    game.info(0).role = 1;
    // The host is offered CONTROL unless the game is a tournament game; a
    // null entry means it is not one.
    CHECK(control_offered(*game.world.world, TeamPanelHost{}));
    CHECK(control_offered(*game.world.world, answer.host()));
    CHECK(answer.asked == 1);
    answer.tournament = true;
    CHECK(!control_offered(*game.world.world, answer.host()));
    CHECK(answer.asked == 2);
}

void test_alliances() {
    Game4 game;
    Machines machines;
    auto& me = game.world.player(0);
    // A computer player takes the alliance on both sides; nothing is told.
    set_alliance(*game.world.world, 0, 1, 1, false, machines.host());
    CHECK(me.alliance[1] == 1 && me.allied_by[1] == 1);
    CHECK(game.world.player(1).alliance[0] == 1 && game.world.player(1).allied_by[0] == 1);
    CHECK(machines.log.empty());
    // A player on another machine hears of it; only our side changes here.
    set_alliance(*game.world.world, 0, 2, 1, false, machines.host());
    CHECK(me.alliance[2] == 1 && me.allied_by[2] == 0);
    CHECK(game.world.player(2).allied_by[0] == 0 && game.world.player(2).alliance[0] == 0);
    CHECK(machines.log == std::vector<std::string>{"ally 0>2=1"});
    // A defeated player on another machine counts as allied back.
    game.info(3).state = 2;
    set_alliance(*game.world.world, 0, 3, 1, false, machines.host());
    CHECK(me.alliance[3] == 1 && me.allied_by[3] == 1);
    CHECK(machines.log.size() == 2 && machines.log[1] == "ally 0>3=1");
    // Another local player learns it is allied with, and allies back only when asked.
    game.world.add_player(4, OA_PLAYER_STATUS_LOCAL);
    set_alliance(*game.world.world, 0, 4, 1, false, machines.host());
    CHECK(me.alliance[4] == 1 && me.allied_by[4] == 0);
    CHECK(game.world.player(4).allied_by[0] == 1 && game.world.player(4).alliance[0] == 0);
    set_alliance(*game.world.world, 0, 4, 1, true, machines.host());
    CHECK(me.allied_by[4] == 1 && game.world.player(4).alliance[0] == 1);
    CHECK(machines.log.size() == 2);
    // Out-of-range players change nothing.
    set_alliance(*game.world.world, 10, 1, 0, false, machines.host());
    CHECK(me.alliance[1] == 1);
}

void test_team_members() {
    Game4 game;
    CHECK(team_member_count(*game.world.world, OA_PLAYER_NO_TEAM) == 0);
    game.world.player(0).team = 1;
    game.world.player(2).team = 1;
    CHECK(team_member_count(*game.world.world, 1) == 2);
    // Once the game loads, a player with no units left no longer counts.
    game.world.game().session_flags = 0x04;
    game.world.player(2).unit_count = 0;
    CHECK(team_member_count(*game.world.world, 1) == 1);
}

void test_tab_menu() {
    Game4 game;
    hud_test::FakePanel panel({"HEADER", "OPTIONS", "SHARE", "ALLIES", "CANCEL", "CONTROL"});
    hud_test::Sounds sounds;
    CHECK(toggle_tab_menu(
        *game.world.world,
        oa::data::campaign::SessionKind::multiplayer,
        true,
        panel.loader(),
        panel.controls(),
        sounds.events()
    ));
    CHECK(panel.loads == std::vector<std::string>{"TABMENU.GUI"});
    CHECK(panel.active["ALLIES"] && panel.active["SHARE"] && panel.active["CONTROL"]);
    CHECK((game.world.game().gui_flags & kGuiFlagsTabMenu) != 0);
    // A second press closes it.
    CHECK(!toggle_tab_menu(
        *game.world.world,
        oa::data::campaign::SessionKind::multiplayer,
        true,
        panel.loader(),
        panel.controls(),
        sounds.events()
    ));
    CHECK(panel.closes == 1 && (game.world.game().gui_flags & kGuiFlagsMenuOpen) == 0);
    // CONTROL needs this machine to host; a watcher gets neither ALLIES nor SHARE.
    CHECK(toggle_tab_menu(
        *game.world.world,
        oa::data::campaign::SessionKind::multiplayer,
        false,
        panel.loader(),
        panel.controls(),
        sounds.events()
    ));
    CHECK(panel.active["ALLIES"] && !panel.active["CONTROL"]);
    game.world.game().gui_flags = 0;
    game.info(0).options = OA_SETUP_OPTION_WATCHER;
    CHECK(toggle_tab_menu(
        *game.world.world,
        oa::data::campaign::SessionKind::multiplayer,
        true,
        panel.loader(),
        panel.controls(),
        sounds.events()
    ));
    CHECK(!panel.active["ALLIES"] && !panel.active["SHARE"] && !panel.active["CONTROL"]);
    CHECK(sounds.played.size() == 4 && sounds.played[0] == "SmallButton");
    // Under the alliance-menu rule the watcher keeps ALLIES; SHARE and
    // CONTROL stay hidden.
    game.world.game().gui_flags = 0;
    CHECK(toggle_tab_menu(
        *game.world.world,
        oa::data::campaign::SessionKind::multiplayer,
        true,
        panel.loader(),
        panel.controls(),
        sounds.events(),
        true
    ));
    CHECK(panel.active["ALLIES"] && !panel.active["SHARE"] && !panel.active["CONTROL"]);
    // Outside multiplayer, 3.1c hides all three; the rule shows ALLIES alone.
    game.info(0).options = 0;
    for (const auto kind :
         {oa::data::campaign::SessionKind::skirmish, oa::data::campaign::SessionKind::campaign}) {
        for (const bool every_game : {false, true}) {
            game.world.game().gui_flags = 0;
            CHECK(toggle_tab_menu(
                *game.world.world,
                kind,
                true,
                panel.loader(),
                panel.controls(),
                sounds.events(),
                every_game
            ));
            CHECK(panel.active["ALLIES"] == every_game);
            CHECK(!panel.active["SHARE"] && !panel.active["CONTROL"]);
        }
    }
}

void test_allies_rows() {
    Game4 game;
    game.world.player(0).team = 2;
    game.world.player(3).team = 2;
    game.world.player(0).alliance[2] = 1;
    game.world.player(0).allied_by[3] = 1;
    // A watcher and a player without a colour get no row.
    game.world.add_player(5, OA_PLAYER_STATUS_MIRRORED);
    game.info(5).options = OA_SETUP_OPTION_WATCHER;
    game.world.add_player(6, OA_PLAYER_STATUS_MIRRORED);
    game.info(6).color = 0xff;
    hud_test::FakePanel panel(allies_controls());
    open_allies_panel(*game.world.world, panel.controls());
    CHECK((game.world.game().frame_flags & kFrameAlliesPanelOpen) != 0);
    // Rows 0..3 are players 0..3, the local player included.
    for (int slot = 0; slot < 4; ++slot) {
        const auto live = "LIVEPLYR" + std::to_string(slot);
        CHECK(panel.index(live) != -1 && panel.active[live]);
        CHECK(panel.texts[live] == "Player " + std::to_string(slot));
        CHECK(panel.values["LOGO" + std::to_string(slot)] == slot);
    }
    CHECK(panel.index("LIVEPLYR5") == -1 && panel.index("LIVEPLYR6") == -1);
    CHECK(!panel.active["PLAYER4"]);
    // Ally toggles show only for players on another machine.
    CHECK(!panel.active["LIVEALLY0"] && !panel.active["LIVEALLY1"]);
    CHECK(panel.active["LIVEALLY2"] && panel.active["LIVEALLY3"]);
    // A team mate's toggle is greyed.
    CHECK(panel.grayed["LIVEALLY3"] && !panel.grayed["LIVEALLY2"]);
    // Bit 0: we allied with them; bit 1: they allied with us.
    CHECK(panel.values["LIVEALLY2"] == 1 && panel.values["LIVEALLY3"] == 2);
    CHECK(panel.values["LIVEALLY1"] == 0);
    // Team icons, numbered by slot before loading: team 2 has two members.
    CHECK(panel.values["TEAMICONS0"] == 4 && panel.values["TEAMICONS3"] == 4);
    CHECK(panel.values["TEAMICONS1"] == 10);
    CHECK(!panel.grayed["TEAMICONS0"] && panel.grayed["TEAMICONS2"]);
    // A team of two greys allied victory.
    CHECK(panel.values["VICTORY"] == 0 && panel.grayed["VICTORY"]);
}

void test_allies_clicks() {
    Game4 game;
    Machines machines;
    hud_test::Sounds sounds;
    hud_test::FakePanel panel(allies_controls());
    open_allies_panel(*game.world.world, panel.controls());
    CHECK(!panel.grayed["VICTORY"]);
    auto result = allies_panel_click(
        *game.world.world,
        "LIVEALLY2",
        panel.controls(),
        sounds.events(),
        machines.host(),
        nullptr,
        nullptr
    );
    CHECK(result.click == TeamPanelClick::none);
    CHECK(std::string(result.announcement) == " allied with Player 2");
    CHECK(game.world.player(0).alliance[2] == 1);
    CHECK(machines.log == std::vector<std::string>{"ally 0>2=1"});
    CHECK(panel.values["LIVEALLY2"] == 1);
    // Breaking the alliance with the computer player tells nobody.
    game.world.player(0).alliance[1] = 1;
    const auto translate = [](void*, const char* text) -> const char* {
        return std::strcmp(text, "broke alliance with") == 0 ? "left" : nullptr;
    };
    result = allies_panel_click(
        *game.world.world,
        "LIVEALLY1",
        panel.controls(),
        sounds.events(),
        machines.host(),
        translate,
        nullptr
    );
    CHECK(std::string(result.announcement) == " left Player 1");
    CHECK(game.world.player(0).alliance[1] == 0 && game.world.player(1).alliance[0] == 0);
    CHECK(machines.log.size() == 1);
    // VICTORY only sounds; OK stores it and tells the others once it changed.
    result = allies_panel_click(
        *game.world.world,
        "VICTORY",
        panel.controls(),
        sounds.events(),
        machines.host(),
        nullptr,
        nullptr
    );
    CHECK(result.click == TeamPanelClick::none && result.announcement[0] == '\0');
    panel.values["VICTORY"] = 1;
    result = allies_panel_click(
        *game.world.world,
        "OK",
        panel.controls(),
        sounds.events(),
        machines.host(),
        nullptr,
        nullptr
    );
    CHECK(result.click == TeamPanelClick::closed);
    CHECK((game.info(0).status & OA_SETUP_STATUS_ALLIED_VICTORY) != 0);
    CHECK(machines.log.size() == 2 && machines.log[1] == "setup");
    CHECK((game.world.game().frame_flags & kFrameAlliesPanelOpen) == 0);
    open_allies_panel(*game.world.world, panel.controls());
    CHECK(panel.values["VICTORY"] == 1);
    (void)allies_panel_click(
        *game.world.world,
        "OK",
        panel.controls(),
        sounds.events(),
        machines.host(),
        nullptr,
        nullptr
    );
    CHECK(machines.log.size() == 2);
    CHECK(sounds.played.size() == 5);
    for (const auto& sound : sounds.played)
        CHECK(sound == "Options");
    // A watcher's allied victory is greyed.
    game.info(0).options = OA_SETUP_OPTION_WATCHER;
    hud_test::FakePanel watched(allies_controls());
    open_allies_panel(*game.world.world, watched.controls());
    CHECK(watched.grayed["VICTORY"]);
    // Closing drops the open flag.
    result = allies_panel_click(
        *game.world.world,
        nullptr,
        watched.controls(),
        sounds.events(),
        machines.host(),
        nullptr,
        nullptr
    );
    CHECK(result.click == TeamPanelClick::closed);
    CHECK((game.world.game().frame_flags & kFrameAlliesPanelOpen) == 0);
}

void test_control_panel() {
    Game4 game;
    Machines machines;
    hud_test::Sounds sounds;
    game.world.add_player(5, OA_PLAYER_STATUS_MIRRORED);
    game.info(5).options = OA_SETUP_OPTION_WATCHER;
    game.info(0).options = OA_SETUP_OPTION_WATCHING_ALLOWED;
    hud_test::FakePanel panel(control_controls());
    CHECK(open_control_panel(*game.world.world, panel.controls()));
    // The local player has no row.
    CHECK(panel.index("LIVEPLYR0") == -1 && panel.index("LIVEPLYR1") != -1);
    CHECK(panel.texts["LIVEPLYR1"] == "Player 1");
    CHECK(panel.values["WATCHING"] == 1 && panel.values["GAMEOPEN"] == 1);
    auto result = control_panel_click(
        *game.world.world, "LIVEPLYR2", panel.controls(), sounds.events(), machines.host()
    );
    CHECK(result.click == TeamPanelClick::confirm_removal && result.player == 2);
    CHECK(machines.log.empty() && sounds.played.empty());
    result = control_panel_click(
        *game.world.world, "WATCHING", panel.controls(), sounds.events(), machines.host()
    );
    CHECK(result.click == TeamPanelClick::none);
    CHECK((game.info(0).options & OA_SETUP_OPTION_WATCHING_ALLOWED) == 0);
    CHECK(panel.values["WATCHING"] == 0);
    CHECK(machines.log == std::vector<std::string>{"setup"});
    // OK republishes and, with watching off, removes the other machine's watcher.
    result = control_panel_click(
        *game.world.world, "OK", panel.controls(), sounds.events(), machines.host()
    );
    CHECK(result.click == TeamPanelClick::closed);
    CHECK((machines.log == std::vector<std::string>{"setup", "game", "remove 5 9"}));
    CHECK(sounds.played == (std::vector<std::string>{"Options", "Options"}));
    // With watching allowed OK removes nobody.
    game.info(0).options = OA_SETUP_OPTION_WATCHING_ALLOWED;
    machines.log.clear();
    (void)control_panel_click(
        *game.world.world, "OK", panel.controls(), sounds.events(), machines.host()
    );
    CHECK(machines.log == std::vector<std::string>{"game"});
    // A watching local player gets no panel.
    game.info(0).options = OA_SETUP_OPTION_WATCHER;
    hud_test::FakePanel watched(control_controls());
    CHECK(!open_control_panel(*game.world.world, watched.controls()));
    CHECK(watched.texts.empty());
}

void test_removal_question() {
    Game4 game;
    Machines machines;
    hud_test::FakePanel question({"HEADER", "CHOICE1", "CHOICE2", "TITLE"});
    const auto translate = [](void*, const char* text) -> const char* {
        return std::strcmp(text, "Reject") == 0 ? "Remove" : nullptr;
    };
    open_removal_question(*game.world.world, 3, question.controls(), translate, nullptr);
    CHECK(question.texts["CHOICE1"] == "Yes" && question.texts["CHOICE2"] == "No");
    CHECK(question.texts["TITLE"] == "Remove Player 3?");
    CHECK(removal_question_click("TITLE", 3, machines.host()).click == TeamPanelClick::none);
    CHECK(removal_question_click("CHOICE2", 3, machines.host()).click == TeamPanelClick::closed);
    CHECK(machines.log.empty());
    CHECK(removal_question_click("CHOICE1", 3, machines.host()).click == TeamPanelClick::closed);
    CHECK(machines.log == std::vector<std::string>{"remove 3 1"});
    // A null host entry removes nobody.
    confirm_player_removal(TeamPanelHost{}, 3);
    CHECK(removal_question_click(nullptr, 3, machines.host()).click == TeamPanelClick::closed);
    CHECK(machines.log.size() == 1);
}

} // namespace

int main() {
    test_host_role();
    test_control_offered();
    test_alliances();
    test_team_members();
    test_tab_menu();
    test_allies_rows();
    test_allies_clicks();
    test_control_panel();
    test_removal_question();
    return 0;
}
