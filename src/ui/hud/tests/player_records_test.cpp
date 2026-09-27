// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "check.hpp"
#include "fixtures.hpp"

#include "oa/ui/hud/chat_panel.hpp"
#include "oa/ui/hud/player_records.hpp"

#include <cstring>

using namespace oa;
using namespace oa::ui::hud;

namespace {

constexpr int32_t kMemory64Mb = 0x4000000;

bool only_self(const uint8_t (&row)[11], uint8_t self) {
    for (uint8_t i = 0; i < 11; ++i)
        if (row[i] != (i == self ? 1 : 0))
            return false;
    return true;
}

// A slot reset over a dirty record: alliance rows cleared but for itself,
// the slot index everywhere, team 5, the ready bit (0x20) cleared and the
// memory reported as bytes / 0x100000 + 1.
void slot_reset() {
    hud_test::TestWorld w;
    Player& p = w.player(3);
    auto* info = world_player_info(w.world, &p);
    std::memset(p.alliance, 1, sizeof p.alliance);
    std::memset(p.allied_by, 1, sizeof p.allied_by);
    std::memset(p.economy_requested, 1, sizeof p.economy_requested);
    std::memset(p.economy_processed, 1, sizeof p.economy_processed);
    std::memset(p.economy_answered, 1, sizeof p.economy_answered);
    p.controller = 7;
    p.team = 2;
    p.reject_reason = 4;
    p.machine_group = 3;
    p.player_id = 0x1234;
    info->options = OA_SETUP_OPTION_READY | OA_SETUP_OPTION_WATCHER;
    info->side = 1;

    init_player_slot(*w.world, 3, OA_PLAYER_STATUS_COMPUTER, kSessionSkirmish, kMemory64Mb);
    CHECK(only_self(p.alliance, 3) && only_self(p.allied_by, 3));
    for (uint8_t i = 0; i < 11; ++i)
        CHECK(
            p.economy_requested[i] == 0 && p.economy_processed[i] == 0 && p.economy_answered[i] == 0
        );
    CHECK(p.status == OA_PLAYER_STATUS_COMPUTER && info->state == OA_PLAYER_STATUS_COMPUTER);
    CHECK(p.controller == 0 && p.in_use == 1 && p.player_id == 3);
    CHECK(p.index == 3 && p.board_row == 3 && p.start_position == 3);
    CHECK(p.team == OA_PLAYER_NO_TEAM && p.reject_reason == 0 && p.machine_group == 0);
    CHECK(info->options == OA_SETUP_OPTION_WATCHER);
    CHECK(info->memory_mb == 65);
    CHECK(std::strcmp(p.name, "Core") == 0 && std::strcmp(p.second_name, "Core") == 0);

    // Side 0 names a computer "Arm"; a human is "Player".
    info->side = 0;
    init_player_slot(*w.world, 3, OA_PLAYER_STATUS_COMPUTER, kSessionCampaign, kMemory64Mb);
    CHECK(std::strcmp(p.name, "Arm") == 0);
    init_player_slot(*w.world, 3, OA_PLAYER_STATUS_LOCAL, kSessionSkirmish, kMemory64Mb);
    CHECK(std::strcmp(p.name, "Player") == 0 && info->state == OA_PLAYER_STATUS_LOCAL);

    // A status for another player's machine leaves the info state and
    // memory alone; its name is only copied.
    std::snprintf(p.name, sizeof p.name, "%s", "joiner");
    info->memory_mb = 9;
    init_player_slot(*w.world, 3, OA_PLAYER_STATUS_MIRRORED, kSessionSkirmish, kMemory64Mb);
    CHECK(p.status == OA_PLAYER_STATUS_MIRRORED && info->state == OA_PLAYER_STATUS_LOCAL);
    CHECK(info->memory_mb == 9);
    CHECK(std::strcmp(p.name, "joiner") == 0 && std::strcmp(p.second_name, "joiner") == 0);

    // Outside a campaign or skirmish neither name changes; the memory is a
    // signed quotient.
    std::snprintf(p.second_name, sizeof p.second_name, "%s", "kept");
    init_player_slot(*w.world, 3, OA_PLAYER_STATUS_LOCAL, kSessionMultiplayer, -0x200000);
    CHECK(std::strcmp(p.name, "joiner") == 0 && std::strcmp(p.second_name, "kept") == 0);
    CHECK(info->memory_mb == 0xffff);
}

// Roster rows as the ally search reads them: their controller and alliance.
constexpr SkirmishSlot kRoster[] = {
    {OA_PLAYER_STATUS_LOCAL, 0, 0, 1000, 1000, 4},
    {OA_PLAYER_STATUS_COMPUTER, 1, 0, 1000, 1000, 6},
    {OA_PLAYER_STATUS_FREE, 1, 0, 1000, 1000, 7},
    {OA_PLAYER_STATUS_COMPUTER, 0, kNoRosterAlliance, 1000, 1000, 2},
    {OA_PLAYER_STATUS_COMPUTER, 1, kNoRosterAlliance, 1000, 1000, 3},
    {OA_PLAYER_STATUS_LOCAL, 1, 1, 1000, 1000, 5},
};
constexpr int32_t kRosterCount = 6;

void roster_allies() {
    CHECK(next_roster_ally(kRoster, kRosterCount, 0, 0) == 0);
    CHECK(next_roster_ally(kRoster, kRosterCount, 0, 1) == 1);
    // A disabled entry of the alliance, the no-alliance entries and another
    // alliance never match.
    CHECK(next_roster_ally(kRoster, kRosterCount, 0, 2) == -1);
    // No alliance: only the entry itself.
    CHECK(next_roster_ally(kRoster, kRosterCount, 3, 0) == 3);
    CHECK(next_roster_ally(kRoster, kRosterCount, 3, 4) == -1);
    CHECK(next_roster_ally(kRoster, kRosterCount, 0, kRosterCount) == -1);
}

// Seating from the roster: every row resets its record; seated rows take side,
// colour and allies, the last human is the local and viewing player.
void roster_seating() {
    hud_test::TestWorld w;
    for (uint8_t i = 0; i < OA_PLAYER_COUNT; ++i)
        world_player_info(w.world, &w.player(i))->color = 0xff;
    init_player_slots_from_roster(*w.world, kRoster, kRosterCount, kSessionSkirmish, kMemory64Mb);
    const auto info = [&](uint8_t i) { return world_player_info(w.world, &w.player(i)); };
    CHECK(
        w.player(0).status == OA_PLAYER_STATUS_LOCAL && info(0)->color == 4 && info(0)->side == 0
    );
    CHECK(
        w.player(1).status == OA_PLAYER_STATUS_COMPUTER && info(1)->color == 6 && info(1)->side == 1
    );
    CHECK(
        w.player(0).alliance[0] == 1 && w.player(0).alliance[1] == 1 && w.player(0).alliance[2] == 0
    );
    CHECK(w.player(1).alliance[0] == 1 && w.player(1).alliance[1] == 1);
    CHECK(
        std::strcmp(w.player(0).name, "Player") == 0 && std::strcmp(w.player(1).name, "Core") == 0
    );
    CHECK(std::strcmp(w.player(3).name, "Arm") == 0);
    // The disabled row's record is reset free and keeps its colour.
    CHECK(
        w.player(2).status == OA_PLAYER_STATUS_FREE && w.player(2).in_use == 1 &&
        info(2)->color == 0xff
    );
    CHECK(only_self(w.player(2).alliance, 2));
    CHECK(only_self(w.player(3).alliance, 3) && only_self(w.player(4).alliance, 4));
    CHECK(only_self(w.player(5).alliance, 5));
    CHECK(w.game().local_player_index == 5 && w.game().viewpoint_player == 5);
    // Records past the roster are left alone.
    CHECK(w.player(6).in_use == 0 && w.player(6).index == 10);
}

} // namespace

int main() {
    slot_reset();
    roster_allies();
    roster_seating();
    std::puts("player records tests passed");
    return 0;
}
