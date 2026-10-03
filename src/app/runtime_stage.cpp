// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The parts of a --stage file that play out over its match: lines timed for
// later ticks, units placed by map pixel, the groups the stage's units join
// and the orders it gives them. apply_stage (runtime_benchmark.cpp) reads the
// file and carries out its lines through run_stage_direction; the match's
// steps run the timed lines (run_due_stage_lines).
#include "oa/app/runtime.hpp"
#include "stage_state.hpp"

#include "oa/core/unit.h"
#include "oa/sim/ground_orders/goals.hpp"
#include "oa/sim/match_runtime.hpp"
#include "oa/sim/unit_spawn/spawn.hpp"
#include "oa/sim/unit_spawn/spawn_runtime.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace oa::app {
namespace {

/// The shift from whole map pixels to 16.16 world units.
constexpr uint32_t kStageFixedShift = 16;

/// A facing a place line names, and the quarter turns from south it stands
/// for.
struct FacingName {
    std::string_view name{};
    uint8_t facing{};
};

/// The facings a place line may name.
constexpr std::array<FacingName, 4> kFacingNames{
    {{"south", oa::sim::unit_spawn::facing_south},
     {"east", oa::sim::unit_spawn::facing_east},
     {"north", oa::sim::unit_spawn::facing_north},
     {"west", oa::sim::unit_spawn::facing_west}}
};

/// Returns a map pixel as a 16.16 world point on the ground.
///
/// @param match the running match, whose terrain gives the height
/// @param x map pixels across
/// @param z map-image row
/// @return the point
[[nodiscard]] oa::sim::ground_orders::Point
ground_point(oa::sim::match_runtime::Match& match, int32_t x, int32_t z) {
    const auto fixed_x = static_cast<uint32_t>(x) << kStageFixedShift;
    const auto fixed_z = static_cast<uint32_t>(z) << kStageFixedShift;
    return {
        static_cast<int32_t>(fixed_x),
        static_cast<int32_t>(
            static_cast<uint32_t>(match.map_height(fixed_x, fixed_z)) << kStageFixedShift
        ),
        static_cast<int32_t>(fixed_z)
    };
}

} // namespace

void Runtime::destroy_stage_state(StageState* state) noexcept {
    delete state;
}

void Runtime::begin_stage_state() {
    stage_.reset(new StageState{});
    for (const auto& slot : match_->world().slots)
        if (slot.unit != nullptr && slot.record.type_index != 0)
            stage_->origins.try_emplace(
                slot.record.owner_index,
                static_cast<int32_t>(slot.unit->position[0] >> kStageFixedShift),
                static_cast<int32_t>(slot.unit->position[2] >> kStageFixedShift)
            );
}

bool Runtime::take_stage_tick(
    std::istream& line, const std::string& where, const StageLine& source, std::string& action
) {
    int64_t tick = -1;
    if (!(line >> tick) || tick < 0 || tick > std::numeric_limits<uint32_t>::max())
        throw std::runtime_error(where + ": at takes TICK ACTION");
    if (tick > match_timing_.tick) {
        // Kept after every line of the same tick or an earlier one, so that
        // a tick's lines run in the order the file gives them.
        auto& timed = stage_->timed;
        const auto later = std::upper_bound(
            timed.begin() + static_cast<std::ptrdiff_t>(stage_->next_timed),
            timed.end(),
            tick,
            [](int64_t value, const StageState::TimedLine& entry) { return value < entry.tick; }
        );
        timed.insert(later, {static_cast<uint32_t>(tick), source});
        return false;
    }
    if (!(line >> action))
        throw std::runtime_error(where + ": at takes TICK ACTION");
    return true;
}

void Runtime::run_due_stage_lines() {
    if (!stage_ || !match_)
        return;
    std::vector<StageLine> due;
    const auto& timed = stage_->timed;
    while (stage_->next_timed < timed.size() &&
           timed[stage_->next_timed].tick <= match_timing_.tick)
        due.push_back(timed[stage_->next_timed++].line);
    if (due.empty())
        return;
    std::printf("stage: tick %u\n", match_timing_.tick);
    run_stage_lines(due);
}

void Runtime::join_stage_group(uint16_t unit) {
    stage_->last_unit = unit;
    // A unit in a slot an earlier member held is not that member.
    for (auto& [name, members] : stage_->groups)
        std::erase_if(members, [unit](const StageState::Member& member) {
            return member.unit == unit;
        });
    if (stage_->group.empty())
        return;
    const auto& record = match_->world().slots[unit].record;
    stage_->groups[stage_->group].push_back({unit, record.owner_index, record.type_index});
}

bool Runtime::run_stage_direction(
    const std::string& action, std::istream& line, const std::string& where
) {
    // The live units of a group, in the order they joined; throws for a
    // group no unit joined.
    const auto live_members = [&](const std::string& name) {
        const auto found = stage_->groups.find(name);
        if (found == stage_->groups.end())
            throw std::runtime_error(where + ": no unit joined the group " + name);
        std::vector<uint16_t> units;
        const auto& slots = match_->world().slots;
        for (const auto& member : found->second) {
            if (member.unit >= slots.size())
                continue;
            const auto& slot = slots[member.unit];
            if (slot.unit != nullptr && slot.record.type_index == member.type &&
                slot.record.owner_index == member.owner &&
                oa::unit_is_live_target(slot.record.flags))
                units.push_back(member.unit);
        }
        return units;
    };
    if (action == "group") {
        std::string name;
        line >> name;
        stage_->group = name;
        std::printf("stage: group %s\n", name.empty() ? "(none)" : name.c_str());
        return true;
    }
    if (action == "place") {
        int32_t player = -1;
        std::string name;
        int32_t x = 0;
        int32_t z = 0;
        if (!(line >> player >> name >> x >> z))
            throw std::runtime_error(where + ": place takes PLAYER TYPE X Z [FACING]");
        uint8_t facing = oa::sim::unit_spawn::facing_south;
        if (std::string word; line >> word) {
            const auto named = std::find_if(
                kFacingNames.begin(), kFacingNames.end(), [&word](const FacingName& entry) {
                    return entry.name == word;
                }
            );
            if (named == kFacingNames.end())
                throw std::runtime_error(
                    where + ": a place line faces south, east, north or west, not " + word
                );
            facing = named->facing;
        }
        const auto type = oa::sim::unit_spawn::find_type_index(spawn_type_names_, name);
        if (type == 0)
            throw std::runtime_error(where + ": the game has no type " + name);
        if (player < 0 || player >= OA_PLAYER_COUNT ||
            match_->state().game.players[player].in_use == 0)
            throw std::runtime_error(
                where + ": player " + std::to_string(player) + " is not playing"
            );
        const auto& game = match_->state().game;
        if (x < 0 || z < 0 || x >= game.map_pixel_width || z >= game.map_pixel_height)
            throw std::runtime_error(
                where + ": " + std::to_string(x) + "," + std::to_string(z) + " is off the map"
            );
        const auto point = ground_point(*match_, x, z);
        oa::sim::unit_spawn::Request request;
        request.player = static_cast<uint8_t>(player);
        request.type = type;
        request.finished = true;
        request.state = kGroundOccupancyState;
        request.position = {
            static_cast<uint32_t>(point[0]),
            static_cast<uint32_t>(point[1]),
            static_cast<uint32_t>(point[2])
        };
        request.facing = facing;
        const auto* placed = match_->create(request);
        if (placed == nullptr || placed->unit == nullptr)
            throw std::runtime_error(where + ": " + name + " could not be placed");
        join_stage_group(placed->unit_index);
        std::printf(
            "stage: unit %u %s of player %d at %d,%d\n",
            static_cast<unsigned>(placed->unit_index),
            name.c_str(),
            player,
            x,
            z
        );
        return true;
    }
    if (action == "move" || action == "patrol") {
        std::string group;
        int32_t x = 0;
        int32_t z = 0;
        if (!(line >> group >> x >> z))
            throw std::runtime_error(where + ": " + action + " takes GROUP X Z");
        const auto point = ground_point(*match_, x, z);
        std::size_t ordered = 0;
        for (const auto unit : live_members(group)) {
            if (!match_->takes_move_order(unit))
                continue;
            if (action == "move")
                match_->issue_ground_move(unit, point, false);
            else
                match_->issue_patrol(unit, point, false);
            ++ordered;
        }
        std::printf(
            "stage: %s %s: %zu units to %d,%d\n", action.c_str(), group.c_str(), ordered, x, z
        );
        return true;
    }
    if (action == "attack") {
        std::string group;
        std::string targets;
        if (!(line >> group >> targets))
            throw std::runtime_error(where + ": attack takes GROUP TARGETS");
        const auto candidates = live_members(targets);
        const auto& slots = match_->world().slots;
        std::size_t ordered = 0;
        for (const auto unit : live_members(group)) {
            // The nearest live unit of the targets; the first of them joined
            // when two are as near.
            const auto& from = slots[unit].unit->position;
            uint16_t nearest = 0;
            int64_t nearest_distance = 0;
            for (const auto candidate : candidates) {
                const auto& to = slots[candidate].unit->position;
                const int64_t dx = (static_cast<int32_t>(to[0]) >> kStageFixedShift) -
                                   (static_cast<int32_t>(from[0]) >> kStageFixedShift);
                const int64_t dz = (static_cast<int32_t>(to[2]) >> kStageFixedShift) -
                                   (static_cast<int32_t>(from[2]) >> kStageFixedShift);
                const int64_t distance = dx * dx + dz * dz;
                if (nearest == 0 || distance < nearest_distance) {
                    nearest = candidate;
                    nearest_distance = distance;
                }
            }
            if (nearest != 0 && match_->issue_attack_command(unit, nearest, false, nullptr))
                ++ordered;
        }
        std::printf("stage: attack %s: %zu units on %s\n", group.c_str(), ordered, targets.c_str());
        return true;
    }
    if (action == "activate" || action == "deactivate") {
        std::string group;
        if (!(line >> group))
            throw std::runtime_error(where + ": " + action + " takes GROUP");
        // The ON/OFF button's order, given to each unit as to a selection: a
        // type that cannot be switched on and off takes it and stays as it is.
        const char* tag = action == "activate" ? "ACTIVATE" : "DEACTIVATE";
        std::size_t ordered = 0;
        for (const auto unit : live_members(group))
            if (give_state_order(unit, tag, 0))
                ++ordered;
        std::printf("stage: %s %s: %zu units\n", action.c_str(), group.c_str(), ordered);
        return true;
    }
    return false;
}

} // namespace oa::app
