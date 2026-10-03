// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The setup and team rules over plain slot views: team records, alliance
// requests, resent alliances, team deals, start positions and computer
// player names.
#include "oa/ui/frontend_multiplayer/team_rules.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace tr = oa::ui::frontend_multiplayer::team_rules;

namespace {

int failures = 0;

void expect(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAILED: %s\n", what);
        ++failures;
    }
}

/// A seated slot.
///
/// @param status Player.status
/// @param team the team, tr::no_team for none
/// @param setup_state human or computer
/// @param name the player's name
/// @return the slot
tr::TeamSlot seat(uint8_t status, int8_t team, uint8_t setup_state, const char* name) {
    tr::TeamSlot slot;
    slot.in_use = true;
    slot.status = status;
    slot.team = team;
    slot.setup_state = setup_state;
    std::snprintf(slot.name.data(), slot.name.size(), "%s", name);
    return slot;
}

/// Tells whether the steps hold an alliance step.
bool has_alliance(const tr::TeamSteps& steps, uint8_t from, uint8_t to, uint8_t value) {
    for (uint16_t i = 0; i < steps.count; ++i) {
        const auto& step = steps.items[i];
        if (step.kind == tr::TeamStep::Kind::alliance && step.from == from && step.to == to &&
            step.value == value)
            return true;
    }
    return false;
}

/// Counts the steps of a kind.
int count_kind(const tr::TeamSteps& steps, tr::TeamStep::Kind kind) {
    int count = 0;
    for (uint16_t i = 0; i < steps.count; ++i)
        count += steps.items[i].kind == kind ? 1 : 0;
    return count;
}

void test_team_number_reaction() {
    tr::TeamSlots slots{};
    slots[0] = seat(tr::status_local, 1, tr::setup_human, "Host");
    slots[1] = seat(tr::status_remote, tr::no_team, tr::setup_human, "Guest");
    slots[2] = seat(tr::status_computer, 2, tr::setup_computer, "AI:Host");
    slots[3] = seat(tr::status_remote, 1, tr::setup_human, "Far");
    slots[0].alliance[1] = 0;
    slots[1].alliance[2] = 1;

    // The guest joins team 1: the host allies it, the computer player (team 2)
    // drops the guest's alliance with it; a remote player is not acted for.
    auto result = tr::receive_team_number(slots, 1, 1, true);
    expect(result.store && result.team == 1, "team stored");
    expect(has_alliance(result.steps, 0, 1, 1), "same team: local player allies the sender");
    expect(has_alliance(result.steps, 1, 0, 1), "same team: the sender is asked to ally back");
    expect(!has_alliance(result.steps, 2, 1, 0), "computer already unallied: no step");
    expect(has_alliance(result.steps, 1, 2, 0), "other team: the sender's alliance is undone");
    expect(
        !has_alliance(result.steps, 3, 1, 1), "a remote player's alliance is left to its machine"
    );

    // Bit 7 sets the team and leaves every alliance.
    result = tr::receive_team_number(slots, 1, 0x81, true);
    expect(result.store && result.team == 1 && result.steps.count == 0, "bit 7 keeps alliances");
    // Without the bit-7 reading the byte is a team of 6 or more and ignored.
    result = tr::receive_team_number(slots, 1, 0x81, false);
    expect(!result.store && result.steps.count == 0, "without the flag 0x81 is ignored");
    result = tr::receive_team_number(slots, 1, 6, true);
    expect(!result.store, "team 6 is ignored");

    // Joining no team leaves players that are on no team as they are.
    slots[0].team = tr::no_team;
    slots[0].alliance[1] = 1;
    slots[1].alliance[0] = 1;
    result = tr::receive_team_number(slots, 1, 5, true);
    expect(result.store && result.team == tr::no_team, "no team stored");
    expect(!has_alliance(result.steps, 0, 1, 0), "a player on no team keeps its alliance");
    expect(
        has_alliance(result.steps, 2, 1, 0) || !has_alliance(result.steps, 2, 1, 1),
        "a teamed player is unallied from a player on no team"
    );

    // A watcher is never acted for.
    slots[0].team = 1;
    slots[0].alliance[1] = 0;
    slots[0].watcher = true;
    result = tr::receive_team_number(slots, 1, 1, true);
    expect(!has_alliance(result.steps, 0, 1, 1), "a watcher is not allied");
}

void test_alliance_requests() {
    const auto local = seat(tr::status_local, 0, tr::setup_human, "A");
    const auto computer = seat(tr::status_computer, 0, tr::setup_computer, "B");
    const auto remote = seat(tr::status_remote, 0, tr::setup_human, "C");
    expect(tr::alliance_requested(local, tr::alliance_request), "a request for a local player");
    expect(
        tr::alliance_requested(computer, tr::alliance_request), "a request for a computer player"
    );
    expect(!tr::alliance_requested(remote, tr::alliance_request), "a remote player's is not ours");
    expect(!tr::alliance_requested(local, 1), "both-sides 1 is 3.1c's record");
    expect(!tr::alliance_requested(local, 0), "both-sides 0 is 3.1c's record");
}

void test_resend() {
    tr::TeamSlots slots{};
    slots[0] = seat(tr::status_local, 0, tr::setup_human, "A");
    slots[1] = seat(tr::status_remote, 0, tr::setup_human, "B");
    slots[2] = seat(tr::status_remote, 1, tr::setup_human, "C");
    slots[3] = seat(tr::status_remote, tr::no_team, tr::setup_human, "D");
    slots[0].alliance[2] = 1;
    slots[0].alliance[3] = 1;
    const auto stored = tr::resend_alliances(slots, 0, tr::AllianceResend::stored);
    expect(stored.count == 3, "every other seated slot");
    expect(
        has_alliance(stored, 0, 1, 0) && has_alliance(stored, 0, 2, 1) &&
            has_alliance(stored, 0, 3, 1),
        "stored alliances go out as they stand"
    );
    const auto teams = tr::resend_alliances(slots, 0, tr::AllianceResend::from_teams);
    expect(has_alliance(teams, 0, 1, 1), "same team allies");
    expect(has_alliance(teams, 0, 2, 0), "other team unallies");
    expect(has_alliance(teams, 0, 3, 1), "a player on no team keeps its alliance");
}

void test_dealt_team_count() {
    expect(tr::dealt_team_count("") == 2, "no argument deals two teams");
    expect(tr::dealt_team_count("3") == 3, "three teams");
    expect(tr::dealt_team_count("9") == 5, "at most five");
    expect(tr::dealt_team_count("1") == 2, "at least two");
    expect(tr::dealt_team_count("x") == 2, "no number deals two");
    expect(tr::dealt_team_count("4 more") == 4, "the leading number counts");
}

void test_deal_teams() {
    tr::TeamSlots slots{};
    slots[0] = seat(tr::status_local, tr::no_team, tr::setup_human, "Host");
    slots[1] = seat(tr::status_remote, tr::no_team, tr::setup_human, "Guest");
    slots[2] = seat(tr::status_computer, tr::no_team, tr::setup_computer, "AI:Host 2");
    slots[3] = seat(tr::status_computer, tr::no_team, tr::setup_computer, "AI:Host 3");
    slots[0].alliance[1] = 1;
    const std::array<int32_t, tr::slot_count> order{0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    const auto steps = tr::deal_teams(slots, order, 2);
    expect(
        steps.items[0].kind == tr::TeamStep::Kind::alliance && steps.items[0].from == 0 &&
            steps.items[0].to == 1 && steps.items[0].value == 0,
        "existing alliances are undone first"
    );
    expect(count_kind(steps, tr::TeamStep::Kind::team) == 4, "every counted player takes a team");
    bool teams_right = true;
    for (uint16_t i = 0; i < steps.count; ++i) {
        const auto& step = steps.items[i];
        if (step.kind == tr::TeamStep::Kind::team)
            teams_right = teams_right && step.value == step.from % 2;
    }
    expect(teams_right, "the k-th named player takes team k mod 2");
    expect(
        has_alliance(steps, 0, 2, 1) && has_alliance(steps, 0, 1, 0) &&
            has_alliance(steps, 1, 3, 1),
        "team-mates ally, the rest unally"
    );

    // Two players of one name both stand for the first of it.
    slots[3] = seat(tr::status_computer, tr::no_team, tr::setup_computer, "ai:host 2");
    const auto same = tr::deal_teams(slots, order, 2);
    int teams_of_two = 0;
    for (uint16_t i = 0; i < same.count; ++i)
        if (same.items[i].kind == tr::TeamStep::Kind::team && same.items[i].from == 2)
            ++teams_of_two;
    expect(teams_of_two == 2, "a repeated name, without case, stands for its first slot");
}

uint32_t first_draw(void*, uint32_t) {
    return 0;
}

void test_shuffle() {
    std::array<int32_t, tr::slot_count> order{0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    tr::shuffle_slots(order, {});
    expect(order[0] == 0 && order[9] == 9, "no draws leave the order");
    tr::shuffle_slots(order, {nullptr, first_draw});
    // Each index swaps with the first: 1,2,...,9,0.
    expect(order[0] == 9 && order[1] == 0 && order[9] == 8, "each index swaps with its draw");
}

void test_start_positions() {
    const tr::ShuffleRandom none{};
    tr::TeamSlots slots{};
    // Teams from team numbers: 0,1,0,1 -> team 1 at 0,2 and team 2 at 1,3.
    slots[0] = seat(tr::status_local, 0, tr::setup_human, "A");
    slots[1] = seat(tr::status_remote, 0, tr::setup_human, "B");
    slots[2] = seat(tr::status_remote, 1, tr::setup_human, "C");
    slots[3] = seat(tr::status_remote, 1, tr::setup_human, "D");
    auto positions = tr::team_start_positions(slots, false, false, none);
    expect(
        positions[0] == 0 && positions[1] == 2 && positions[2] == 1 && positions[3] == 3,
        "team-mates every second position"
    );
    expect(positions[4] == -1, "an empty slot takes none");

    // A player on no team: teams come from alliances.
    slots[3].team = tr::no_team;
    slots[0].alliance[3] = 1;
    positions = tr::team_start_positions(slots, false, false, none);
    // Groups: {0,3}, {1}, {2} -> three teams, team 1 at 0 then 3.
    expect(
        positions[0] == 0 && positions[3] == 3 && positions[1] == 1 && positions[2] == 2,
        "allied players form a team"
    );

    // No team of two: one after another.
    slots[0].alliance[3] = 0;
    positions = tr::team_start_positions(slots, false, false, none);
    expect(
        positions[0] == 0 && positions[1] == 1 && positions[2] == 2 && positions[3] == 3,
        "solitary players in slot order"
    );

    // A watcher takes no position.
    slots[1].watcher = true;
    positions = tr::team_start_positions(slots, false, false, none);
    expect(positions[1] == -1 && positions[2] == 1 && positions[3] == 2, "a watcher takes none");
    slots[1].watcher = false;

    // A position past the last slot moves to the lowest free one.
    tr::TeamSlots crowd{};
    for (std::size_t i = 0; i < 6; ++i)
        crowd[i] = seat(tr::status_remote, 4, tr::setup_human, "P");
    crowd[6] = seat(tr::status_local, 0, tr::setup_human, "Q");
    positions = tr::team_start_positions(crowd, false, false, none);
    // Two teams (0 and 4): team 5 starts at 4 and steps by 2: 4,6,8,(10)->free,...
    expect(positions[0] == 4 && positions[1] == 6 && positions[2] == 8, "team 5 from position 4");
    expect(positions[6] == 0, "team 1 from position 0");
    expect(
        positions[3] == 1 && positions[4] == 2 && positions[5] == 3,
        "past the last slot: the lowest free positions in turn"
    );
}

void test_neutral_maps() {
    const tr::ShuffleRandom none{};
    tr::TeamSlots slots{};
    slots[0] = seat(tr::status_computer, tr::no_team, tr::setup_computer, "AI");
    slots[1] = seat(tr::status_local, tr::no_team, tr::setup_human, "Host");
    slots[2] = seat(tr::status_remote, tr::no_team, tr::setup_human, "Guest");
    auto positions = tr::team_start_positions(slots, false, true, none);
    expect(
        positions[1] == 0 && positions[2] == 1 && positions[0] == 2,
        "the computer player this machine runs goes last"
    );
    positions = tr::team_start_positions(slots, false, false, none);
    expect(positions[0] == 0, "without neutral units slot order stands");

    // A remote computer player is not moved by the sort but by the final swap.
    slots[0].status = tr::status_remote;
    positions = tr::team_start_positions(slots, false, true, none);
    expect(
        positions[0] == 2 && positions[2] == 0,
        "the highest computer player swaps with the highest human"
    );
}

void test_alliances_by_position() {
    std::array<int32_t, tr::slot_count> positions{};
    positions.fill(-1);
    positions[0] = 0;
    positions[1] = 3;
    positions[4] = 2;
    const auto steps = tr::alliances_by_position(positions, 2);
    expect(steps.count == 6, "every placed pair both ways");
    expect(has_alliance(steps, 0, 4, 1) && has_alliance(steps, 4, 0, 1), "even positions ally");
    expect(has_alliance(steps, 0, 1, 0) && has_alliance(steps, 1, 4, 0), "odd and even unally");
    const auto three = tr::alliances_by_position(positions, 3);
    expect(has_alliance(three, 0, 1, 1), "positions 0 and 3 share a team of three");
}

void test_computer_names() {
    char name[tr::computer_name_bytes + 1];
    tr::format_computer_name(name, "AI:%s %d", "Steve", 3);
    expect(std::string(name) == "AI:Steve 3", "name and slot");
    tr::format_computer_name(name, "AI:%s", "Steve", 3);
    expect(std::string(name) == "AI:Steve", "the 3.1c form");
    tr::format_computer_name(name, "AI:%s %d", "AVeryLongPlayerName", 9);
    expect(std::string(name) == "AI:AVeryLongPlay", "cut to 16 bytes");
    tr::format_computer_name(name, "Bot %s-%d!", "X", 0);
    expect(std::string(name) == "Bot X-0!", "other text is copied");
}

} // namespace

int main() {
    test_team_number_reaction();
    test_alliance_requests();
    test_resend();
    test_dealt_team_count();
    test_deal_teams();
    test_shuffle();
    test_start_positions();
    test_neutral_maps();
    test_alliances_by_position();
    test_computer_names();
    if (failures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    std::puts("team rules tests passed");
    return 0;
}
