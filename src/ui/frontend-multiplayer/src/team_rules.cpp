// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Setup and team rules over a plain view of the player slots.
#include "oa/ui/frontend_multiplayer/team_rules.hpp"

#include <algorithm>
#include <cstring>

namespace oa::ui::frontend_multiplayer::team_rules {

namespace {

/// Every slot in slot order.
constexpr std::array<int32_t, slot_count> slot_order{0, 1, 2, 3, 4, 5, 6, 7, 8, 9};

/// A placement position no slot holds; the second pass replaces it.
constexpr int32_t position_taken = static_cast<int32_t>(slot_count);

/// Lowers an ASCII letter.
///
/// @param c the character
/// @return its lower-case form, or c
constexpr unsigned char lowered(unsigned char c) noexcept {
    return c >= 'A' && c <= 'Z' ? static_cast<unsigned char>(c + ('a' - 'A')) : c;
}

/// Compares two player names without ASCII case.
///
/// @param a a name, NUL-terminated within its array
/// @param b another
/// @return true when they are equal
bool same_name(const std::array<char, 30>& a, const std::array<char, 30>& b) noexcept {
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto x = lowered(static_cast<unsigned char>(a[i]));
        const auto y = lowered(static_cast<unsigned char>(b[i]));
        if (x != y)
            return false;
        if (x == '\0')
            return true;
    }
    return true;
}

/// Finds the first seated slot of a name, without ASCII case.
///
/// @param slots the slots
/// @param name the name
/// @return its index, or -1 when no seated slot has it
int32_t slot_named(const TeamSlots& slots, const std::array<char, 30>& name) noexcept {
    for (std::size_t i = 0; i < slot_count; ++i)
        if (slots[i].in_use && same_name(slots[i].name, name))
            return static_cast<int32_t>(i);
    return -1;
}

/// An alliance step.
///
/// @param from the player setting its alliance
/// @param to the other player
/// @param allied whether it allies
/// @return the step
TeamStep alliance_step(std::size_t from, std::size_t to, bool allied) noexcept {
    return {
        TeamStep::Kind::alliance,
        static_cast<uint8_t>(from),
        static_cast<uint8_t>(to),
        static_cast<uint8_t>(allied ? 1 : 0)
    };
}

/// The team of each counted player from its linked alliances: a player and
/// every counted player it allies, and theirs in turn, share a team.
///
/// @param slots the slots
/// @param from the slot the group grows from
/// @param team the team number given to the group
/// @param[in,out] seen the slots already given a team
/// @param[in,out] teams the team of each slot
void link_allies(
    const TeamSlots& slots,
    std::size_t from,
    int32_t team,
    std::array<bool, slot_count>& seen,
    std::array<int32_t, slot_count>& teams
) noexcept {
    seen[from] = true;
    teams[from] = team;
    for (std::size_t other = 0; other < slot_count; ++other)
        if (counted(slots[other]) && slots[from].alliance[other] != 0 && !seen[other])
            link_allies(slots, other, team, seen, teams);
}

/// The size of each team of `teams`, by team number - 1.
///
/// @param teams the team of each slot, 0 for none
/// @return the sizes
std::array<int32_t, slot_count> team_sizes(const std::array<int32_t, slot_count>& teams) noexcept {
    std::array<int32_t, slot_count> sizes{};
    for (const int32_t team : teams)
        if (team >= 1 && team <= static_cast<int32_t>(slot_count))
            ++sizes[static_cast<std::size_t>(team - 1)];
    return sizes;
}

/// Places the computer players this machine runs that are alone on their
/// team after every other slot of an order, keeping both groups' order.
///
/// @param slots the slots
/// @param[in,out] order slot indices; entries past the last slot are dropped and the tail is -1
/// @param teams the team of each slot
/// @param sizes the size of each team by team number - 1
/// @return the number of players moved
int32_t solitary_computers_last(
    const TeamSlots& slots,
    std::array<int32_t, slot_count>& order,
    const std::array<int32_t, slot_count>& teams,
    const std::array<int32_t, slot_count>& sizes
) noexcept {
    std::array<int32_t, slot_count> kept{};
    std::array<int32_t, slot_count> moved{};
    int32_t kept_count = 0;
    int32_t moved_count = 0;
    for (const int32_t index : order) {
        if (index < 0 || index >= static_cast<int32_t>(slot_count))
            continue;
        const int32_t team = teams[static_cast<std::size_t>(index)];
        const int32_t size = team >= 1 && team <= static_cast<int32_t>(slot_count)
                                 ? sizes[static_cast<std::size_t>(team - 1)]
                                 : 0;
        if (slots[static_cast<std::size_t>(index)].status == status_computer && size == 1)
            moved[static_cast<std::size_t>(moved_count++)] = index;
        else
            kept[static_cast<std::size_t>(kept_count++)] = index;
    }
    order.fill(-1);
    int32_t next = 0;
    for (int32_t i = 0; i < kept_count; ++i)
        order[static_cast<std::size_t>(next++)] = kept[static_cast<std::size_t>(i)];
    for (int32_t i = 0; i < moved_count; ++i)
        order[static_cast<std::size_t>(next++)] = moved[static_cast<std::size_t>(i)];
    return moved_count;
}

/// Positions players one after another.
///
/// @param slots the slots
/// @param random whether the order is shuffled
/// @param neutral_units whether computer players go after the others
/// @param shuffle the draws
/// @return the positions
StartPositions sequential_positions(
    const TeamSlots& slots, bool random, bool neutral_units, const ShuffleRandom& shuffle
) noexcept {
    StartPositions positions{};
    positions.fill(-1);
    auto order = slot_order;
    if (random)
        shuffle_slots(order, shuffle);
    if (neutral_units) {
        // Every player counts as a team of its own here.
        std::array<int32_t, slot_count> teams{};
        std::array<int32_t, slot_count> sizes{};
        for (std::size_t i = 0; i < slot_count; ++i) {
            teams[i] = static_cast<int32_t>(i + 1);
            sizes[i] = 1;
        }
        (void)solitary_computers_last(slots, order, teams, sizes);
    }
    int32_t next = 0;
    for (const int32_t index : order)
        if (index >= 0 && counted(slots[static_cast<std::size_t>(index)]))
            positions[static_cast<std::size_t>(index)] = next++;
    return positions;
}

/// Positions team-mates every n-th position apart.
///
/// @param slots the slots
/// @param teams the team of each slot, 0 for none
/// @param random whether the order is shuffled
/// @param neutral_units whether lone computer players go after the others
/// @param shuffle the draws
/// @return the positions
StartPositions team_positions(
    const TeamSlots& slots,
    const std::array<int32_t, slot_count>& teams,
    bool random,
    bool neutral_units,
    const ShuffleRandom& shuffle
) noexcept {
    StartPositions positions{};
    positions.fill(-1);
    const auto sizes = team_sizes(teams);
    int32_t stride = 0;
    for (const int32_t size : sizes)
        if (size != 0)
            ++stride;
    auto order = slot_order;
    if (random)
        shuffle_slots(order, shuffle);
    if (neutral_units && solitary_computers_last(slots, order, teams, sizes) > 0 && stride > 2)
        --stride;
    // Team t's next position, by t - 1.
    std::array<int32_t, slot_count> next = slot_order;
    std::array<bool, slot_count> used{};
    for (const int32_t index : order) {
        if (index < 0)
            continue;
        const int32_t team = teams[static_cast<std::size_t>(index)];
        if (team <= 0)
            continue;
        auto& position = positions[static_cast<std::size_t>(index)];
        const int32_t wanted = next[static_cast<std::size_t>(team - 1)];
        if (wanted >= 0 && wanted < static_cast<int32_t>(slot_count) &&
            used[static_cast<std::size_t>(wanted)]) {
            position = position_taken;
            continue;
        }
        position = wanted;
        next[static_cast<std::size_t>(team - 1)] += stride;
        if (wanted >= 0 && wanted < static_cast<int32_t>(slot_count))
            used[static_cast<std::size_t>(wanted)] = true;
    }
    // A position taken twice or past the last slot becomes the lowest free one.
    for (const int32_t index : order) {
        if (index < 0 || teams[static_cast<std::size_t>(index)] <= 0)
            continue;
        auto& position = positions[static_cast<std::size_t>(index)];
        if (position < static_cast<int32_t>(slot_count))
            continue;
        for (std::size_t free = 0; free < slot_count; ++free)
            if (!used[free]) {
                position = static_cast<int32_t>(free);
                used[free] = true;
                break;
            }
        position %= static_cast<int32_t>(slot_count);
    }
    return positions;
}

} // namespace

void TeamSteps::push(const TeamStep& step) noexcept {
    if (count < items.size())
        items[count++] = step;
}

bool counted(const TeamSlot& slot) noexcept {
    return slot.in_use && !slot.watcher;
}

bool runs_here(const TeamSlot& slot) noexcept {
    return slot.status == status_local || slot.status == status_computer;
}

TeamNumberResult receive_team_number(
    const TeamSlots& slots, uint8_t sender, uint8_t value, bool keeps_alliances_bit
) noexcept {
    TeamNumberResult result;
    const uint8_t team =
        keeps_alliances_bit ? static_cast<uint8_t>(value & team_number_bits) : value;
    if (team >= team_values || sender >= slot_count)
        return result;
    result.store = true;
    result.team = static_cast<int8_t>(team);
    if (keeps_alliances_bit && (value & team_keeps_alliances) != 0)
        return result;
    const auto& from = slots[sender];
    for (std::size_t other = 0; other < slot_count; ++other) {
        const auto& player = slots[other];
        if (!player.in_use || other == sender || player.watcher)
            continue;
        if (team == static_cast<uint8_t>(no_team) && player.team >= no_team)
            continue;
        if (!runs_here(player))
            continue;
        const bool same = static_cast<int32_t>(player.team) == static_cast<int32_t>(team);
        if (same != (player.alliance[sender] != 0))
            result.steps.push(alliance_step(other, sender, same));
        if (same != (from.alliance[other] != 0))
            result.steps.push(alliance_step(sender, other, same));
    }
    return result;
}

bool alliance_requested(const TeamSlot& first, uint32_t both_sides) noexcept {
    return both_sides == alliance_request && runs_here(first);
}

TeamSteps resend_alliances(const TeamSlots& slots, uint8_t player, AllianceResend resend) noexcept {
    TeamSteps steps;
    if (player >= slot_count || !slots[player].in_use)
        return steps;
    const auto& from = slots[player];
    for (std::size_t other = 0; other < slot_count; ++other) {
        if (other == player || !slots[other].in_use)
            continue;
        bool allied = from.alliance[other] != 0;
        if (resend == AllianceResend::from_teams && from.team != no_team &&
            slots[other].team != no_team)
            allied = from.team == slots[other].team;
        steps.push(alliance_step(player, other, allied));
    }
    return steps;
}

int32_t dealt_team_count(std::string_view argument) noexcept {
    if (argument.empty())
        return fewest_dealt_teams;
    std::size_t at = 0;
    while (at < argument.size() && (argument[at] == ' ' || argument[at] == '\t'))
        ++at;
    bool negative = false;
    if (at < argument.size() && (argument[at] == '-' || argument[at] == '+'))
        negative = argument[at++] == '-';
    int64_t number = 0;
    while (at < argument.size() && argument[at] >= '0' && argument[at] <= '9' && number < 1000) {
        number = number * 10 + (argument[at] - '0');
        ++at;
    }
    if (negative)
        number = -number;
    return static_cast<int32_t>(std::clamp<int64_t>(number, fewest_dealt_teams, most_dealt_teams));
}

void shuffle_slots(std::array<int32_t, slot_count>& order, const ShuffleRandom& random) noexcept {
    if (random.below == nullptr)
        return;
    for (std::size_t i = 1; i < order.size(); ++i) {
        const uint32_t j = random.below(random.context, static_cast<uint32_t>(i + 1));
        if (j <= i)
            std::swap(order[i], order[j]);
    }
}

TeamSteps deal_teams(
    const TeamSlots& slots, const std::array<int32_t, slot_count>& order, int32_t teams
) noexcept {
    TeamSteps steps;
    if (teams <= 0)
        return steps;
    for (std::size_t a = 0; a < slot_count; ++a) {
        if (!counted(slots[a]))
            continue;
        for (std::size_t b = a + 1; b < slot_count; ++b)
            if (counted(slots[b]) && (slots[a].alliance[b] != 0 || slots[b].alliance[a] != 0)) {
                steps.push(alliance_step(a, b, false));
                steps.push(alliance_step(b, a, false));
            }
    }
    // The named players, each standing for the first seated slot of its name.
    std::array<int32_t, slot_count> named{};
    named.fill(-1);
    int32_t count = 0;
    for (const int32_t index : order)
        if (index >= 0 && index < static_cast<int32_t>(slot_count) &&
            counted(slots[static_cast<std::size_t>(index)]))
            named[static_cast<std::size_t>(count++)] =
                slot_named(slots, slots[static_cast<std::size_t>(index)].name);
    for (int32_t k = 0; k < count; ++k) {
        const int32_t index = named[static_cast<std::size_t>(k)];
        if (index < 0 || !counted(slots[static_cast<std::size_t>(index)]))
            continue;
        steps.push(
            {TeamStep::Kind::team, static_cast<uint8_t>(index), 0, static_cast<uint8_t>(k % teams)}
        );
    }
    for (int32_t k = 0; k < count; ++k) {
        const int32_t index = named[static_cast<std::size_t>(k)];
        if (index < 0 || !counted(slots[static_cast<std::size_t>(index)]))
            continue;
        for (int32_t m = 0; m < count; ++m) {
            const int32_t other = named[static_cast<std::size_t>(m)];
            if (other < 0 || other == index || !counted(slots[static_cast<std::size_t>(other)]))
                continue;
            steps.push(alliance_step(
                static_cast<std::size_t>(index),
                static_cast<std::size_t>(other),
                m % teams == k % teams
            ));
        }
    }
    return steps;
}

TeamSteps
alliances_by_position(const std::array<int32_t, slot_count>& positions, int32_t teams) noexcept {
    TeamSteps steps;
    if (teams <= 0)
        return steps;
    for (std::size_t from = 0; from < slot_count; ++from) {
        if (positions[from] < 0)
            continue;
        for (std::size_t to = 0; to < slot_count; ++to)
            if (to != from && positions[to] >= 0)
                steps.push(
                    alliance_step(from, to, positions[to] % teams == positions[from] % teams)
                );
    }
    return steps;
}

StartPositions team_start_positions(
    const TeamSlots& slots, bool random, bool neutral_units, const ShuffleRandom& shuffle
) noexcept {
    std::array<int32_t, slot_count> teams{};
    bool all_teamed = true;
    for (const auto& slot : slots)
        if (counted(slot) && static_cast<uint8_t>(slot.team) >= static_cast<uint8_t>(no_team))
            all_teamed = false;
    if (all_teamed) {
        for (std::size_t i = 0; i < slot_count; ++i)
            teams[i] = counted(slots[i]) ? slots[i].team + 1 : 0;
    } else {
        std::array<bool, slot_count> seen{};
        int32_t next_team = 1;
        for (std::size_t i = 0; i < slot_count; ++i)
            if (counted(slots[i]) && !seen[i])
                link_allies(slots, i, next_team++, seen, teams);
    }
    const auto sizes = team_sizes(teams);
    const bool together = *std::max_element(sizes.begin(), sizes.end()) > 1;
    StartPositions positions = together
                                   ? team_positions(slots, teams, random, neutral_units, shuffle)
                                   : sequential_positions(slots, random, neutral_units, shuffle);
    if (!neutral_units)
        return positions;
    int32_t human = -1;
    int32_t computer = -1;
    for (std::size_t i = 0; i < slot_count; ++i) {
        if (!counted(slots[i]))
            continue;
        const auto at = static_cast<int32_t>(i);
        if (slots[i].setup_state == setup_human) {
            if (human < 0 || positions[i] > positions[static_cast<std::size_t>(human)])
                human = at;
        } else if (slots[i].setup_state == setup_computer) {
            if (computer < 0 || positions[i] > positions[static_cast<std::size_t>(computer)])
                computer = at;
        }
    }
    if (human >= 0 && computer >= 0 &&
        positions[static_cast<std::size_t>(computer)] < positions[static_cast<std::size_t>(human)])
        std::swap(
            positions[static_cast<std::size_t>(computer)],
            positions[static_cast<std::size_t>(human)]
        );
    return positions;
}

void format_computer_name(
    char (&out)[computer_name_bytes + 1],
    std::string_view format,
    const char* local_name,
    int32_t slot
) noexcept {
    std::memset(out, 0, sizeof out);
    std::size_t length = 0;
    const auto append = [&](char c) {
        if (length < computer_name_bytes)
            out[length++] = c;
    };
    for (std::size_t i = 0; i < format.size(); ++i) {
        if (format[i] != '%' || i + 1 >= format.size()) {
            append(format[i]);
            continue;
        }
        const char kind = format[++i];
        if (kind == 's') {
            for (const char* c = local_name; c != nullptr && *c != '\0'; ++c)
                append(*c);
        } else if (kind == 'd') {
            char digits[12];
            int32_t value = slot;
            std::size_t count = 0;
            const bool negative = value < 0;
            uint32_t magnitude =
                negative ? 0U - static_cast<uint32_t>(value) : static_cast<uint32_t>(value);
            do {
                digits[count++] = static_cast<char>('0' + magnitude % 10U);
                magnitude /= 10U;
            } while (magnitude != 0U);
            if (negative)
                append('-');
            while (count > 0)
                append(digits[--count]);
        } else {
            append(kind);
        }
    }
}

} // namespace oa::ui::frontend_multiplayer::team_rules
