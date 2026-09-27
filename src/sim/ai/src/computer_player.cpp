// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/ai.hpp"

#include "computer_internal.hpp"
#include "oa/base/game_math.hpp"
#include "oa/sim/unit_movement/movement.hpp"
#include "oa/sim/simulation_state.hpp"

#include <cstdlib>
#include <cstring>

namespace oa::sim::ai {
namespace {

constexpr uint32_t strike_interval_ticks = 300;
constexpr uint32_t rally_interval_ticks = 150;
constexpr uint32_t construction_interval_ticks = 90;
constexpr uint32_t structures_interval_ticks = 30;
constexpr uint32_t air_raid_base_ticks = 30;
constexpr uint32_t air_raid_jitter_ticks = 900;
constexpr int32_t air_raid_group_size = 5;
constexpr uint32_t siege_base_ticks = 30;
constexpr uint32_t siege_jitter_ticks = 150;
constexpr int32_t commander_factory_quota = 5;
constexpr int32_t builder_patrol_radius = 0x1400000;   // 320 world units
constexpr int32_t builder_nudge_radius = 0x100000;     // 16 world units
constexpr int32_t commander_patrol_radius = 0x2800000; // 640 world units
constexpr uint8_t order_preserve_busy = 0x08;          // bit of Order::preserve_flags
constexpr uint8_t order_queue_idle = 0x40;             // bit of OrderState::command_flags

int32_t word(oa_fixed value) noexcept {
    return static_cast<int16_t>(static_cast<uint32_t>(value) >> 16);
}

int32_t wrap_add(int32_t a, int32_t b) noexcept {
    return static_cast<int32_t>(static_cast<uint32_t>(a) + static_cast<uint32_t>(b));
}

int32_t wrap_sub(int32_t a, int32_t b) noexcept {
    return static_cast<int32_t>(static_cast<uint32_t>(a) - static_cast<uint32_t>(b));
}

int32_t wrap_mul(int32_t a, int32_t b) noexcept {
    return static_cast<int32_t>(static_cast<uint32_t>(a) * static_cast<uint32_t>(b));
}

oa::Unit* unit_at(const ComputerHost& host, uint16_t slot) noexcept {
    return oa::world_unit_at(host.world, slot);
}

uint32_t tick_of(const ComputerHost& host) noexcept {
    return host.world->game.tick;
}

/// Returns the number of members of one of the player's squads.
///
/// @param host squad membership and world
/// @param ai the player's controller
/// @param squad squad to count
/// @return its member count
uint32_t squad_size(const ComputerHost& host, const ComputerPlayer& ai, Squad squad) noexcept {
    return host.squad_size(host.context, ai.player, squad);
}

/// Tests whether one of the player's squads has no members.
///
/// @param host squad membership and world
/// @param ai the player's controller
/// @param squad squad to test
/// @return true when empty
bool squad_empty(const ComputerHost& host, const ComputerPlayer& ai, Squad squad) noexcept {
    return squad_size(host, ai, squad) == 0;
}

/// Returns the base position the player's knowledge settled on.
///
/// @param ai the player's controller
/// @return 16.16 world position
oa::FixedVec3 base_position(const ComputerPlayer& ai) noexcept {
    return ai.knowledge.base_position;
}

/// Returns the player's finished units that have a build list.
///
/// @param ai the player's controller
/// @return the count from the last knowledge rebuild
int32_t builder_count(const ComputerPlayer& ai) noexcept {
    return ai.knowledge.builder_count;
}

uint16_t
squad_member(const ComputerHost& host, const ComputerPlayer& ai, Squad squad, uint32_t i) noexcept {
    return host.squad_member(host.context, ai.player, squad, i);
}

bool unit_active(const oa::Unit& unit) noexcept {
    return (unit.flags & OA_UNIT_FLAG_LIVE) != 0 && (unit.flags & OA_UNIT_FLAG_DEATH_PENDING) == 0;
}

/// Averages a squad's positions from the signed high words, back in 16.16.
///
/// @param host squad membership and world
/// @param ai the player's controller
/// @param squad squad to average
/// @param[out] out receives the centroid; unchanged for an empty squad
/// @return false for an empty squad
bool squad_centroid(
    const ComputerHost& host, const ComputerPlayer& ai, Squad squad, oa::FixedVec3* out
) noexcept {
    const auto count = squad_size(host, ai, squad);
    if (count == 0)
        return false;
    int32_t x = 0, y = 0, z = 0;
    for (uint32_t i = 0; i < count; ++i) {
        const auto* unit = unit_at(host, squad_member(host, ai, squad, i));
        if (unit == nullptr)
            continue;
        x += word(unit->position.x);
        y += word(unit->position.y);
        z += word(unit->position.z);
    }
    const auto n = static_cast<int32_t>(count);
    out->x = wrap_mul(x / n, 0x10000);
    out->y = wrap_mul(y / n, 0x10000);
    out->z = wrap_mul(z / n, 0x10000);
    return true;
}

/// Finds the player's home: the centroid of the armed structures, else the structures, else the builders.
///
/// @param host squad membership and world
/// @param ai the player's controller
/// @param[out] out receives the home position
/// @return false when all three squads are empty
bool home_centroid(
    const ComputerHost& host, const ComputerPlayer& ai, oa::FixedVec3* out
) noexcept {
    return squad_centroid(host, ai, Squad::armed_structures, out) ||
           squad_centroid(host, ai, Squad::structures, out) ||
           squad_centroid(host, ai, Squad::builders, out);
}

/// Finds the nearest live unit of any player the controller is not allied with.
///
/// @param host alliances and world
/// @param ai the player's controller
/// @param at reference point, 16.16 world coordinates
/// @return the unit, or null
oa::Unit* nearest_enemy(
    const ComputerHost& host, const ComputerPlayer& ai, const oa::FixedVec3& at
) noexcept {
    uint8_t relation[OA_PLAYER_COUNT + 1]{};
    for (uint8_t i = 0; i < OA_PLAYER_COUNT; ++i)
        relation[i] = host.allied(host.context, ai.player, i) ? 1 : 0;
    relation[OA_PLAYER_COUNT] = 1;
    return sim::simulation_state::nearest_candidate_unit(*host.world, relation, at.x, at.z);
}

enum class Command : uint8_t { move, patrol, attack };

/// Orders every unit of the player in a squad, in unit-slot order.
///
/// @param host order calls and world
/// @param ai the player's controller
/// @param squad squad to order
/// @param command move, patrol or attack
/// @param queue true to queue a move or patrol behind the current orders
/// @param target unit to attack (attack only)
/// @param at destination (move and patrol)
void order_squad(
    const ComputerHost& host,
    const ComputerPlayer& ai,
    Squad squad,
    Command command,
    bool queue,
    uint16_t target,
    const oa::FixedVec3* at
) noexcept {
    const auto& player = host.world->game.players[ai.player];
    uint32_t count = 0;
    auto* units = oa::world_player_units(host.world, &player, &count);
    if (units == nullptr)
        return;
    for (uint32_t i = 0; i < count; ++i) {
        const auto& unit = units[i];
        if (unit.type_index == 0 || unit.squad != static_cast<int32_t>(squad))
            continue;
        const auto slot = static_cast<uint16_t>(oa::world_unit_slot(host.world, &unit));
        switch (command) {
        case Command::move:
            (void)host.order_move(host.context, slot, at, queue);
            break;
        case Command::patrol:
            (void)host.order_patrol(host.context, slot, at, queue);
            break;
        case Command::attack:
            (void)host.order_attack(host.context, slot, target);
            break;
        }
    }
}

/// Pulls members of a source squad within the per-member radius into the task's squad.
///
/// Seeds an empty task squad with the source's first member, then sheds the farthest
/// stragglers back to the source until the squad fits the radius, and last recruits
/// every source member inside it.
///
/// @param host squad membership and world
/// @param ai the player's controller
/// @param task task whose squad recruits
/// @param source squad recruited from; the task's own squad does nothing
/// @param radius squared world units allowed per member
void recruit_squad(
    const ComputerHost& host,
    ComputerPlayer& ai,
    const ComputerTask& task,
    Squad source,
    int32_t radius
) noexcept {
    if (source == task.squad)
        return;
    if (squad_size(host, ai, task.squad) == 0) {
        if (squad_size(host, ai, source) == 0)
            return;
        host.set_squad(host.context, squad_member(host, ai, source, 0), task.squad);
    }
    int32_t sum_x = 0, sum_z = 0;
    for (uint32_t i = 0; i < squad_size(host, ai, task.squad); ++i) {
        const auto* unit = unit_at(host, squad_member(host, ai, task.squad, i));
        sum_x += word(unit->position.x);
        sum_z += word(unit->position.z);
    }
    int32_t center_x = 0, center_z = 0;
    for (;;) {
        const auto count = static_cast<int32_t>(squad_size(host, ai, task.squad));
        center_x = sum_x / count;
        center_z = sum_z / count;
        if (count == 1)
            break;
        int32_t farthest = 0;
        uint16_t straggler = 0;
        for (int32_t i = 0; i < count; ++i) {
            const auto slot = squad_member(host, ai, task.squad, static_cast<uint32_t>(i));
            const auto* unit = unit_at(host, slot);
            const auto dx = word(unit->position.x) - center_x;
            const auto dz = word(unit->position.z) - center_z;
            const auto distance = wrap_add(wrap_mul(dz, dz), wrap_mul(dx, dx));
            if (farthest < distance) {
                farthest = distance;
                straggler = slot;
            }
        }
        if (farthest < wrap_mul(count, radius))
            break;
        const auto* unit = unit_at(host, straggler);
        host.set_squad(host.context, straggler, source);
        sum_x -= word(unit->position.x);
        sum_z -= word(unit->position.z);
    }
    uint16_t joining[max_recruits];
    uint32_t joining_count = 0;
    const auto own = static_cast<int32_t>(squad_size(host, ai, task.squad));
    const auto candidates = squad_size(host, ai, source);
    for (uint32_t i = 0; i < candidates && joining_count < max_recruits; ++i) {
        const auto slot = squad_member(host, ai, source, i);
        const auto* unit = unit_at(host, slot);
        const auto dx = word(unit->position.x) - center_x;
        const auto dz = word(unit->position.z) - center_z;
        if (wrap_add(wrap_mul(dz, dz), wrap_mul(dx, dx)) < wrap_mul(own, radius))
            joining[joining_count++] = slot;
    }
    for (uint32_t i = 0; i < joining_count; ++i)
        host.set_squad(host.context, joining[i], task.squad);
}

/// Runs a strike group task.
///
/// Recruits, then rallies at home until big enough, then attacks the nearest enemy
/// unit. Runs again in 300 ticks.
///
/// @param host squads, orders and world
/// @param ai the player's controller
/// @param[in,out] task strike task; its attacking flag and next tick are updated
void run_strike(const ComputerHost& host, ComputerPlayer& ai, ComputerTask& task) noexcept {
    task.next_tick = tick_of(host) + strike_interval_ticks;
    recruit_squad(host, ai, task, task.source_squad, task.merge_radius);
    if (squad_empty(host, ai, task.squad))
        return;
    const auto count = static_cast<int32_t>(squad_size(host, ai, task.squad));
    bool attack = false;
    if (count > task.min_size)
        attack = task.attacking != 0 || count >= task.launch_size;
    if (!attack) {
        oa::FixedVec3 home{};
        if (home_centroid(host, ai, &home)) {
            task.attacking = 0;
            order_squad(host, ai, task.squad, Command::move, false, 0, &home);
            return;
        }
    }
    task.attacking = 1;
    oa::FixedVec3 center{};
    (void)squad_centroid(host, ai, task.squad, &center);
    const auto* target = nearest_enemy(host, ai, center);
    if (target == nullptr)
        return;
    const auto target_slot = static_cast<uint16_t>(oa::world_unit_slot(host.world, target));
    order_squad(host, ai, task.squad, Command::attack, false, target_slot, nullptr);
}

/// Runs a waiting squad's task: move to the strike group when both have members.
///
/// Runs again in 150 ticks.
///
/// @param host squads, orders and world
/// @param ai the player's controller
/// @param[in,out] task rally task
void run_rally(const ComputerHost& host, ComputerPlayer& ai, ComputerTask& task) noexcept {
    task.next_tick = tick_of(host) + rally_interval_ticks;
    if (squad_empty(host, ai, task.squad) || squad_empty(host, ai, task.source_squad))
        return;
    oa::FixedVec3 center{};
    if (squad_centroid(host, ai, task.source_squad, &center))
        order_squad(host, ai, task.squad, Command::move, false, 0, &center);
}

/// Runs the aircraft task.
///
/// Small wings scout around the base (or patrol to the nearest enemy before a base
/// exists); wings of five or more patrol to a random map edge. Runs again in 30 plus a
/// random 0..899 ticks.
///
/// @param host squads, orders, random stream and world
/// @param ai the player's controller
/// @param[in,out] task air raid task
void run_air_raid(const ComputerHost& host, ComputerPlayer& ai, ComputerTask& task) noexcept {
    task.next_tick =
        tick_of(host) + host.random(host.context, air_raid_jitter_ticks) + air_raid_base_ticks;
    const auto world_x = host.map_cells_x << 4;
    const auto world_z = host.map_cells_z << 4;
    if (static_cast<int32_t>(squad_size(host, ai, task.squad)) < air_raid_group_size) {
        const auto base = base_position(ai);
        if ((static_cast<uint32_t>(base.x) >> 16 | static_cast<uint32_t>(base.z) >> 16) == 0) {
            oa::FixedVec3 center{};
            (void)squad_centroid(host, ai, task.squad, &center);
            const auto* target = nearest_enemy(host, ai, center);
            if (target != nullptr)
                order_squad(host, ai, task.squad, Command::patrol, true, 0, &target->position);
            return;
        }
        const auto waypoints = static_cast<int32_t>(host.random(host.context, 2)) + 2;
        const auto span_x = world_x / 8;
        const auto span_z = world_z / 8;
        const auto half_x = span_x / 2;
        const auto half_z = span_z / 2;
        for (int32_t i = 0; i < waypoints; ++i) {
            oa::FixedVec3 point{};
            const auto rx =
                static_cast<int32_t>(host.random(host.context, static_cast<uint32_t>(span_x)));
            point.x = wrap_add(wrap_mul(wrap_add(rx, wrap_mul(half_x, 0xffff)), 0x10000), base.x);
            point.y = base.y;
            const auto rz =
                static_cast<int32_t>(host.random(host.context, static_cast<uint32_t>(span_z)));
            point.z = wrap_add(wrap_mul(wrap_add(rz, wrap_mul(half_z, 0xffff)), 0x10000), base.z);
            order_squad(
                host, ai, task.squad, i == 0 ? Command::move : Command::patrol, i != 0, 0, &point
            );
        }
        return;
    }
    oa::FixedVec3 edge{};
    if (host.random(host.context, 2) != 0) {
        edge.x = wrap_mul(
            static_cast<int32_t>(host.random(host.context, static_cast<uint32_t>(world_x))), 0x10000
        );
        edge.z =
            host.random(host.context, 2) != 0 ? 0 : wrap_mul(wrap_add(world_z, 0xffff), 0x10000);
    } else {
        edge.x =
            host.random(host.context, 2) != 0 ? 0 : wrap_mul(wrap_add(world_x, 0xffff), 0x10000);
        edge.z = wrap_mul(
            static_cast<int32_t>(host.random(host.context, static_cast<uint32_t>(world_z))), 0x10000
        );
    }
    order_squad(host, ai, task.squad, Command::patrol, false, 0, &edge);
}

/// Runs the siege squad timer.
///
/// The sort never assigns squad 9, so only the reschedule (and its random draw) is
/// reachable.
///
/// @param host random stream and world
/// @param ai the player's controller
/// @param[in,out] task siege task
void run_siege(const ComputerHost& host, ComputerPlayer& ai, ComputerTask& task) noexcept {
    task.next_tick =
        tick_of(host) + host.random(host.context, siege_jitter_ticks) + siege_base_ticks;
    (void)squad_size(host, ai, task.squad);
}

/// Runs the structures task: metal makers follow the energy surplus; idle factories queue one pick.
///
/// Runs again in 30 ticks.
///
/// @param state computer players and their type table
/// @param host squads, orders, random stream and world
/// @param ai the player's controller
/// @param[in,out] task structures task
void run_structures(
    ComputerPlayers* state, const ComputerHost& host, ComputerPlayer& ai, ComputerTask& task
) noexcept {
    task.next_tick = tick_of(host) + structures_interval_ticks;
    const auto& player = host.world->game.players[ai.player];
    for (uint32_t i = 0; i < squad_size(host, ai, task.squad); ++i) {
        const auto slot = squad_member(host, ai, task.squad, i);
        const auto* unit = unit_at(host, slot);
        if (unit == nullptr || (unit->flags & OA_UNIT_FLAG_BUILDING) == 0 || !unit_active(*unit))
            continue;
        const auto* type = computer_type(state, unit->type_index);
        if (type == nullptr)
            continue;
        if (type->makes_metal != 0) {
            bool on = false;
            if (player.energy > player.metal + player.metal) {
                if (!(player.energy_produced - player.energy_requested > 0.0F))
                    continue;
                if (host.random(host.context, 5) == 0)
                    continue;
                on = true;
            }
            host.set_active(host.context, slot, on);
            continue;
        }
        if (type->build_count == 0)
            continue;
        uint8_t preserve = 0, queue = 0;
        if (host.primary_order(host.context, slot, &preserve, &queue))
            continue;
        const auto pick = computer_pick_build(state, host, ai.player, slot);
        if (pick != 0)
            (void)host.order_factory(host.context, slot, pick, 1);
    }
}

/// Runs the builders' task.
///
/// Each builder places a weighted pick near the base; idle ones patrol home.
/// Capturing builders (commanders) build only while the player has fewer than five
/// build-capable units and otherwise circle the base. Runs again in 90 ticks.
///
/// @param state computer players and their type table
/// @param host squads, orders, placement queries, random stream and world
/// @param ai the player's controller
/// @param[in,out] task construction task
void run_construction(
    ComputerPlayers* state, const ComputerHost& host, ComputerPlayer& ai, ComputerTask& task
) noexcept {
    task.next_tick = tick_of(host) + construction_interval_ticks;
    const auto base = base_position(ai);
    for (uint32_t i = 0; i < squad_size(host, ai, task.squad); ++i) {
        const auto slot = squad_member(host, ai, task.squad, i);
        const auto* unit = unit_at(host, slot);
        const auto* type = unit != nullptr ? computer_type(state, unit->type_index) : nullptr;
        if (type == nullptr || type->build_count == 0)
            continue;
        const bool captures = (type->abilities & OA_UNIT_DEF_ABILITY_CAN_CAPTURE) != 0;
        if (captures && (builder_count(ai) >= commander_factory_quota ||
                         tick_of(host) < ai.commander_build_tick))
            continue;
        uint8_t preserve = 0, queue = 0;
        if (host.primary_order(host.context, slot, &preserve, &queue) &&
            (preserve & order_preserve_busy) != 0)
            continue;
        const auto pick = computer_pick_build(state, host, ai.player, slot);
        if (pick == 0)
            continue;
        oa::FixedVec3 site{};
        bool placed = computer_place_build(state, host, ai, unit->position, pick, &site);
        if (captures) {
            const auto reach =
                wrap_mul((host.map_cells_x * 16 - 32 + host.map_cells_z * 16 - 128) / 3, 0x10000);
            const auto length = base::game_math::truncated_length(
                wrap_sub(site.x, base.x), 0, wrap_sub(site.z, base.z)
            );
            if (length > reach)
                placed = false;
        }
        if (placed)
            (void)host.order_build(host.context, slot, pick, &site);
    }
    for (uint32_t i = 0; i < squad_size(host, ai, task.squad); ++i) {
        const auto slot = squad_member(host, ai, task.squad, i);
        const auto* unit = unit_at(host, slot);
        const auto* type = unit != nullptr ? computer_type(state, unit->type_index) : nullptr;
        if (type == nullptr)
            continue;
        uint8_t preserve = 0, queue = 0;
        if (host.primary_order(host.context, slot, &preserve, &queue) &&
            (queue & order_queue_idle) == 0)
            continue;
        const bool captures = (type->abilities & OA_UNIT_DEF_ABILITY_CAN_CAPTURE) != 0;
        if (captures && builder_count(ai) < commander_factory_quota)
            continue;
        oa::FixedVec3 destination = base;
        if (captures) {
            int32_t offset[3] = {
                wrap_sub(base.x, unit->position.x), 0, wrap_sub(base.z, unit->position.z)
            };
            if (base::game_math::truncated_length(offset[0], offset[1], offset[2]) >
                commander_patrol_radius) {
                const auto angle = static_cast<uint16_t>(host.random(host.context, 0x10000));
                offset[0] = -sim::unit_movement::sine_scaled(angle, commander_patrol_radius);
                offset[1] = 0;
                offset[2] = -sim::unit_movement::cosine_scaled(angle, commander_patrol_radius);
            }
            oa::FixedVec3 circle{
                wrap_add(base.x, offset[0]),
                wrap_add(unit->position.y, offset[1]),
                wrap_add(base.z, offset[2])
            };
            (void)host.order_move(host.context, slot, &circle, false);
            oa::FixedVec3 home{base.x, unit->position.y, base.z};
            (void)host.order_patrol(host.context, slot, &home, true);
            continue;
        }
        int32_t offset[3] = {
            wrap_sub(base.x, unit->position.x),
            wrap_sub(base.y, unit->position.y),
            wrap_sub(base.z, unit->position.z)
        };
        const auto length = base::game_math::truncated_length(offset[0], offset[1], offset[2]);
        if (length < builder_patrol_radius) {
            if (length < builder_nudge_radius) {
                const auto angle = static_cast<uint16_t>(host.random(host.context, 0x10000));
                offset[0] = -sim::unit_movement::sine_scaled(angle, builder_patrol_radius);
                offset[1] = 0;
                offset[2] = -sim::unit_movement::cosine_scaled(angle, builder_patrol_radius);
            } else {
                const auto factor = static_cast<int32_t>(
                    (static_cast<int64_t>(0x140) << 32) / static_cast<int64_t>(length)
                );
                base::game_math::scale_vector_fixed(offset, offset, factor);
            }
            destination = {
                wrap_add(unit->position.x, offset[0]),
                wrap_add(unit->position.y, offset[1]),
                wrap_add(unit->position.z, offset[2])
            };
        }
        (void)host.order_patrol(host.context, slot, &destination, false);
    }
}

/// Sorts the player's unsorted units into squads and sets their standing orders.
///
/// Fire at will, and roam, or manoeuvre for capturing units.
///
/// @param state computer players and their type table
/// @param host squad assignment and world
/// @param ai the player's controller
void sort_squads(ComputerPlayers* state, const ComputerHost& host, ComputerPlayer& ai) noexcept {
    const auto& player = host.world->game.players[ai.player];
    uint32_t count = 0;
    auto* units = oa::world_player_units(host.world, &player, &count);
    if (units == nullptr)
        return;
    for (uint32_t i = 0; i < count; ++i) {
        auto& unit = units[i];
        if ((unit.flags & OA_UNIT_FLAG_SELECTABLE) == 0)
            continue;
        const auto* type = computer_type(state, unit.type_index);
        if (type == nullptr)
            continue;
        const auto move_order = (type->abilities & OA_UNIT_DEF_ABILITY_CAN_CAPTURE) != 0
                                    ? standing_move_manoeuvre
                                    : standing_move_roam;
        unit.flags = (unit.flags & ~OA_UNIT_FLAG_MOVE_ORDER_MASK) |
                     (move_order << OA_UNIT_FLAG_MOVE_ORDER_SHIFT);
        unit.flags = (unit.flags & ~OA_UNIT_FLAG_FIRE_ORDER_MASK) |
                     (standing_fire_at_will << OA_UNIT_FLAG_FIRE_ORDER_SHIFT);
        if (unit.squad != 0)
            continue;
        const auto squad = computer_sort_squad(unit, *type);
        if (squad != Squad::none)
            host.set_squad(
                host.context, static_cast<uint16_t>(oa::world_unit_slot(host.world, &unit)), squad
            );
    }
}

void run_task(
    ComputerPlayers* state, const ComputerHost& host, ComputerPlayer& ai, ComputerTask& task
) noexcept {
    switch (task.kind) {
    case TaskKind::structures:
        run_structures(state, host, ai, task);
        break;
    case TaskKind::strike:
        run_strike(host, ai, task);
        break;
    case TaskKind::rally:
        run_rally(host, ai, task);
        break;
    case TaskKind::construction:
        run_construction(state, host, ai, task);
        break;
    case TaskKind::air_raid:
        run_air_raid(host, ai, task);
        break;
    case TaskKind::siege:
        run_siege(host, ai, task);
        break;
    case TaskKind::none:
    case TaskKind::idle:
        break;
    }
}

} // namespace

Squad computer_sort_squad(const oa::Unit& unit, const ComputerType& type) noexcept {
    const bool armed = (unit.flags & OA_UNIT_FLAG_HAS_WEAPONS) != 0;
    if ((unit.flags & OA_UNIT_FLAG_BUILDING) != 0)
        return armed ? Squad::armed_structures : Squad::structures;
    if ((type.flags & OA_UNIT_DEF_FLAG_BUILDER) != 0)
        return Squad::builders;
    if ((type.flags & OA_UNIT_DEF_FLAG_CAN_FLY) != 0)
        return Squad::aircraft;
    if (type.min_water_depth > 0)
        return Squad::navy;
    return armed ? Squad::land_army : Squad::none;
}

void computer_player_create(ComputerPlayer& ai, uint8_t player) noexcept {
    ai.present = 1;
    ai.player = player;
    ai.sort_countdown = static_cast<int32_t>(sort_interval_ticks);
    ai.commander_build_tick = 0;
    for (uint32_t i = 0; i < squad_count; ++i) {
        auto& task = ai.tasks[i];
        task = {};
        task.squad = static_cast<Squad>(i);
    }
    ai.tasks[1].kind = TaskKind::structures;
    ai.tasks[2] = {TaskKind::strike, Squad::land_strike, 0, 3, 6, 20000, Squad::land_army, 0};
    ai.tasks[3] = {TaskKind::rally, Squad::land_army, 0, 0, 0, 0, Squad::land_strike, 0};
    ai.tasks[4].kind = TaskKind::construction;
    ai.tasks[5].kind = TaskKind::idle;
    ai.tasks[6] = {TaskKind::strike, Squad::naval_strike, 0, 3, 6, 50000, Squad::navy, 0};
    ai.tasks[7] = {TaskKind::rally, Squad::navy, 0, 0, 0, 0, Squad::naval_strike, 0};
    ai.tasks[8].kind = TaskKind::air_raid;
    ai.tasks[9].kind = TaskKind::siege;
}

void computer_player_tick_orders(
    ComputerPlayers* state, const ComputerHost& host, uint8_t player
) noexcept {
    if (state == nullptr || host.world == nullptr || player >= OA_PLAYER_COUNT ||
        !state->players[player].present)
        return;
    auto& ai = state->players[player];
    if (--ai.sort_countdown < 1) {
        ai.sort_countdown = static_cast<int32_t>(sort_interval_ticks);
        sort_squads(state, host, ai);
    }
    for (auto& task : ai.tasks) {
        if (task.kind != TaskKind::none && task.next_tick <= tick_of(host))
            run_task(state, host, ai, task);
    }
}

} // namespace oa::sim::ai
