// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Map-placed units of skirmish and multiplayer maps.
#include "oa/sim/mission_units/map_units.hpp"

#include <algorithm>
#include <cstring>

namespace oa::sim::mission_units {
namespace {

/// The shift from a 16.16 world coordinate to its map cell.
constexpr int cell_shift = 20;
/// The shift from a map height to a 16.16 world height.
constexpr int height_shift = 16;
/// Player.status of a computer player this machine runs.
constexpr uint8_t status_computer = 2;
/// Player.status of a human on this machine.
constexpr uint8_t status_local = 1;
/// The bytes of a side's commander name.
constexpr std::size_t commander_name_bytes = sizeof(Side::commander);

/// Tells whether an entry names one of the sides' commanders exactly.
///
/// @param name the entry's Unitname
/// @param sides the sides
/// @return true when it does
bool names_commander(const char* name, std::span<const Side> sides) noexcept {
    for (const Side& side : sides) {
        const std::size_t length = ::strnlen(side.commander, commander_name_bytes);
        if (std::strlen(name) == length && std::memcmp(name, side.commander, length) == 0)
            return true;
    }
    return false;
}

/// Finds the unit type whose UnitName equals a name exactly.
///
/// @param world the world
/// @param name the name
/// @return its type index, or -1
int32_t exact_type(const World& world, const char* name) noexcept {
    if (world.unit_defs == nullptr)
        return -1;
    for (uint32_t type = 0; type < world.unit_def_count; ++type) {
        const auto& def = world.unit_defs[type];
        const std::size_t length = ::strnlen(def.unit_name, sizeof def.unit_name);
        if (std::strlen(name) == length && std::memcmp(name, def.unit_name, length) == 0)
            return static_cast<int32_t>(type);
    }
    return -1;
}

} // namespace

int32_t map_unit_owner(const data::campaign::MissionUnit& entry) noexcept {
    return static_cast<int16_t>(static_cast<uint16_t>(entry.player | (entry.flags << 8)));
}

bool has_neutral_map_units(const data::campaign::MissionUnit* units, int32_t count) noexcept {
    for (int32_t i = 0; units != nullptr && i < count; ++i)
        if (map_unit_owner(units[i]) == neutral_map_owner)
            return true;
    return false;
}

int32_t pick_start_map_units(
    const data::campaign::MissionUnit* units,
    int32_t count,
    int32_t start_position,
    bool neutral,
    std::span<const Side> sides,
    std::span<MapUnitPick> out
) noexcept {
    int32_t written = 0;
    for (int32_t i = 0; units != nullptr && i < count; ++i) {
        const auto& entry = units[i];
        if (entry.creation_countdown > 0)
            continue;
        const int32_t owner = map_unit_owner(entry);
        if (neutral ? owner != neutral_map_owner : owner != start_position + 1)
            continue;
        MapUnitPick pick{i, 0};
        if (entry.unit_name != nullptr && names_commander(entry.unit_name, sides))
            pick.slot_offset = static_cast<uint16_t>(commander_slot_offset + i);
        if (static_cast<std::size_t>(written) < out.size())
            out[static_cast<std::size_t>(written++)] = pick;
    }
    return written;
}

Unit* create_map_unit(
    World& world,
    const data::campaign::MissionUnit& entry,
    uint8_t player,
    uint16_t requested_slot,
    const Hooks& hooks
) noexcept {
    if (entry.unit_name == nullptr || hooks.create_unit == nullptr)
        return nullptr;
    const int32_t type = exact_type(world, entry.unit_name);
    if (type < 0)
        return nullptr;
    FixedVec3 position{entry.x, entry.y, entry.z};
    // The height of the cell under the unit, counting cells row by row: a
    // column past the map's width reads the next row's cell.
    const uint32_t width = static_cast<uint32_t>(world.game.map_width);
    const uint32_t cell = (static_cast<uint32_t>(entry.z) >> cell_shift) * width +
                          (static_cast<uint32_t>(entry.x) >> cell_shift);
    if (world.plots != nullptr && cell < width * static_cast<uint32_t>(world.game.map_height))
        position.y =
            static_cast<int32_t>(static_cast<uint32_t>(world.plots[cell].height) << height_shift);
    return hooks.create_unit(
        hooks.context, player, static_cast<uint16_t>(type), position, true, 1, requested_slot
    );
}

void run_map_unit_scripts(
    const data::campaign::MissionUnit* units,
    int32_t count,
    Unit* const* created,
    ScriptPoint& point,
    const Hooks& hooks
) {
    if (units == nullptr || created == nullptr)
        return;
    const CreatedUnits made{units, created, count};
    for (int32_t i = 0; i < count; ++i) {
        const char* script = units[i].initial_mission;
        if (created[i] != nullptr && script != nullptr && script[0] != '\0')
            run_unit_script(*created[i], script, made, point, hooks);
    }
}

int32_t order_timed_map_units(
    const data::campaign::MissionUnit* units, int32_t count, std::span<int32_t> out
) noexcept {
    int32_t written = 0;
    for (int32_t i = 0; units != nullptr && i < count; ++i) {
        const auto& entry = units[i];
        if (entry.unit_name == nullptr || entry.unit_name[0] == '\0' ||
            entry.creation_countdown <= 0)
            continue;
        if (static_cast<std::size_t>(written) < out.size())
            out[static_cast<std::size_t>(written++)] = i;
    }
    // An insertion sort keeps entries of equal countdown in file order and
    // needs no buffer.
    for (int32_t placed = 1; placed < written; ++placed) {
        const int32_t entry = out[static_cast<std::size_t>(placed)];
        int32_t at = placed;
        for (; at > 0 && units[entry].creation_countdown <
                             units[out[static_cast<std::size_t>(at - 1)]].creation_countdown;
             --at)
            out[static_cast<std::size_t>(at)] = out[static_cast<std::size_t>(at - 1)];
        out[static_cast<std::size_t>(at)] = entry;
    }
    return written;
}

bool timed_map_unit_due(const data::campaign::MissionUnit& entry, uint32_t tick) noexcept {
    const auto seconds = static_cast<uint32_t>(
        static_cast<int32_t>(tick) / static_cast<int32_t>(map_unit_ticks_per_second)
    );
    return static_cast<uint32_t>(entry.creation_countdown) <= seconds;
}

int32_t timed_map_unit_player(
    const data::campaign::MissionUnit& entry,
    const std::array<int32_t, map_unit_players>& player_at_position,
    int32_t neutral_player
) noexcept {
    const int32_t owner = map_unit_owner(entry);
    if (owner == neutral_map_owner)
        return neutral_player;
    if (owner < 1 || owner > static_cast<int32_t>(map_unit_players))
        return -1;
    const int32_t player = player_at_position[static_cast<std::size_t>(owner - 1)];
    if (player < 0 || player >= static_cast<int32_t>(map_unit_players) || player == neutral_player)
        return -1;
    return player;
}

void move_computer_last(
    std::array<int32_t, map_unit_players>& positions,
    const std::array<uint8_t, map_unit_players>& statuses,
    int32_t& placing
) noexcept {
    int32_t human = -1;
    int32_t computer = -1;
    int32_t human_position = -1;
    int32_t computer_position = -1;
    for (std::size_t i = 0; i < map_unit_players; ++i) {
        if (statuses[i] == status_local && positions[i] > human_position) {
            human_position = positions[i];
            human = static_cast<int32_t>(i);
        } else if (statuses[i] == status_computer && positions[i] > computer_position) {
            computer_position = positions[i];
            computer = static_cast<int32_t>(i);
        }
    }
    // Without both a human and a computer player here nothing moves.
    if (human < 0 || computer < 0 || computer_position >= human_position)
        return;
    placing = placing == human_position ? computer_position : human_position;
    std::swap(
        positions[static_cast<std::size_t>(human)], positions[static_cast<std::size_t>(computer)]
    );
}

} // namespace oa::sim::mission_units
