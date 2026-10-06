// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Headless check of the attack command a click and an area attack give.
#include "oa/app/runtime.hpp"
#include "match_fault.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

namespace oa::app {
namespace {

// Distances in whole world units from the local commander.
constexpr int32_t near_enemy_distance = 120;
constexpr int32_t second_enemy_offset = 48;
constexpr int32_t move_beyond_distance = 480;
// Ticks for the commander to get walking, and then to come to rest.
constexpr int walk_ticks = 20;
constexpr int rest_ticks = 240;
// Canvas pixels around the enemies' projected positions that the area box
// takes in.
constexpr int area_margin = 6;

} // namespace

void Runtime::check_attack_command() {
    exercise_click(skirmish::resource_name(skirmish::Button::start));
    if (screen_ != Screen::match || !match_ || !selected_tnt_)
        throw std::runtime_error("attack command check: Start did not enter a match");
    auto& slots = match_->world().slots;
    uint16_t commander = 0;
    for (const auto& slot : slots) {
        if (slot.unit_index == 0 || slot.unit == nullptr || slot.record.type_index == 0 ||
            slot.record.owner_index != match_local_player_)
            continue;
        if (const auto* def = definition_for(slot.unit_index); def != nullptr && def->can_dgun) {
            commander = slot.unit_index;
            break;
        }
    }
    if (commander == 0)
        throw std::runtime_error("attack command check found no local commander");
    const auto fail = [](const std::string& what) {
        throw std::runtime_error("attack command check: " + what);
    };

    // Enemies that hold their fire and their ground, toward the middle of
    // the map.
    const auto enemy_player = static_cast<uint8_t>(match_local_player_ == 0 ? 1 : 0);
    const auto enemy_type = oa::sim::unit_spawn::find_type_index(spawn_type_names_, "CORAK");
    if (enemy_type == 0)
        fail("the game lacks CORAK");
    const auto home_x = static_cast<int32_t>(slots[commander].unit->position[0] >> 16);
    const auto home_z = static_cast<int32_t>(slots[commander].unit->position[2] >> 16);
    const int32_t toward_x =
        home_x < static_cast<int32_t>(selected_tnt_->tile_width * 16U) ? 1 : -1;
    const auto ground_at = [&](int32_t dx, int32_t dz) {
        const auto x = static_cast<uint32_t>(home_x + toward_x * dx) << 16;
        const auto z = static_cast<uint32_t>(home_z + dz) << 16;
        return std::array<uint32_t, 3>{x, static_cast<uint32_t>(match_->map_height(x, z)) << 16, z};
    };
    const auto spawn_enemy = [&](int32_t dx, int32_t dz) {
        oa::sim::unit_spawn::Request request;
        request.player = enemy_player;
        request.type = enemy_type;
        request.finished = true;
        request.state = kGroundOccupancyState;
        request.position = ground_at(dx, dz);
        auto* spawned = match_->create(request);
        if (spawned == nullptr || spawned->unit == nullptr)
            fail("could not spawn an enemy");
        return spawned->unit_index;
    };
    const auto near_enemy = spawn_enemy(near_enemy_distance, 0);
    const auto second_enemy = spawn_enemy(near_enemy_distance, second_enemy_offset);
    const auto tick = [&] {
        ++match_timing_.tick;
        match_->simulation().tick = match_timing_.tick;
        tick_or_raise(*match_);
    };
    tick();
    for (const auto enemy : {near_enemy, second_enemy}) {
        match_->stop_orders(enemy);
        slots[enemy].record.flags &= ~(OA_UNIT_FLAG_MOVE_ORDER_MASK | OA_UNIT_FLAG_FIRE_ORDER_MASK);
    }

    const auto viewport = [&] { return live_viewport(match_camera_x_, match_camera_z_); };
    const auto point_at = [&](uint16_t id) {
        const auto screen = project_match_point(viewport(), slots[id].unit->position);
        const auto x = static_cast<float>(screen.x);
        const auto y = static_cast<float>(screen.y);
        update_pointer(x, y);
        if (hovered_match_unit_ != id)
            fail(
                "the pointer over unit " + std::to_string(id) + " found unit " +
                std::to_string(hovered_match_unit_)
            );
        return std::pair{x, y};
    };
    constexpr auto attack_cursor =
        static_cast<uint8_t>(oa::sim::gameplay_input::OrderCursor::attack);
    const auto* runtime = match_->ground_runtime(commander);
    if (runtime == nullptr)
        fail("the commander has no movement");
    const auto primary_orders = [&] {
        std::array<oa::sim::match_runtime::Match::OrderRecordView, 4> records{};
        const auto count = match_->queue_records(commander, false, records.data(), records.size());
        return std::pair{records, count};
    };
    // Starts the commander walking past the enemies, looking at it.
    const auto walk_past = [&] {
        match_->stop_orders(commander);
        const auto beyond = ground_at(move_beyond_distance, 0);
        (void)match_->issue_ground_move(
            commander,
            {std::bit_cast<int32_t>(beyond[0]),
             std::bit_cast<int32_t>(beyond[1]),
             std::bit_cast<int32_t>(beyond[2])},
            false
        );
        for (int step = 0; step < walk_ticks; ++step)
            tick();
        if (runtime->movement.speed == 0)
            fail("the commander did not set off on its move");
        center_camera_on_unit(commander);
        render_match_surface();
    };

    clear_local_selection();
    match_command_ = MatchCommand::none;
    center_camera_on_unit(commander);
    render_match_surface();
    {
        const auto [x, y] = point_at(commander);
        handle_match_left_click(x, y, 1);
    }
    if (selected_match_unit_ != commander)
        fail("the click did not select the commander");

    // A click on an enemy while the commander walks: the attack takes the
    // place of the move and keeps the ground under the pointer.
    walk_past();
    const std::array<uint32_t, 3> walked = slots[commander].unit->position;
    {
        const auto [x, y] = point_at(near_enemy);
        const auto ground = match_world_point(x, y);
        if (const auto cursor = pick_match_cursor(); cursor != attack_cursor)
            fail("shows cursor " + std::to_string(cursor) + " over the enemy");
        handle_match_left_click(x, y, 1);
        const auto [records, count] = primary_orders();
        if (count != 1 || records[0].kind != oa::sim::match_runtime::attack_chase_kind ||
            records[0].target != near_enemy)
            fail(
                "the click left " + std::to_string(count) + " orders, the first of kind " +
                std::to_string(records[0].kind) + " on unit " + std::to_string(records[0].target) +
                ", not the one attack on unit " + std::to_string(near_enemy)
            );
        if (!ground || records[0].point != *ground)
            fail("the click's attack does not keep the ground under the pointer");
    }
    int rested = 0;
    for (int step = 1; step <= rest_ticks && rested == 0; ++step) {
        tick();
        if (runtime->movement.speed == 0)
            rested = step;
    }
    if (rested == 0)
        fail("the commander walked on after the click");
    const auto moved = std::max(
        std::abs(static_cast<int32_t>(slots[commander].unit->position[0] - walked[0]) >> 16),
        std::abs(static_cast<int32_t>(slots[commander].unit->position[2] - walked[2]) >> 16)
    );

    // An area attack over both enemies: the first attack replaces the move,
    // the second follows it.
    walk_past();
    const auto first = project_match_point(viewport(), slots[near_enemy].unit->position);
    const auto second = project_match_point(viewport(), slots[second_enemy].unit->position);
    area_order_units(
        std::min(first.x, second.x) - area_margin,
        std::min(first.y, second.y) - area_margin,
        std::max(first.x, second.x) + area_margin,
        std::max(first.y, second.y) + area_margin,
        "attack"
    );
    {
        const auto [records, count] = primary_orders();
        const auto attacks = [&](std::size_t i) {
            return records[i].kind == oa::sim::match_runtime::attack_chase_kind &&
                   (records[i].target == near_enemy || records[i].target == second_enemy);
        };
        if (count != 2 || !attacks(0) || !attacks(1) || records[0].target == records[1].target)
            fail(
                "the area attack left " + std::to_string(count) +
                " orders, not an attack on each enemy"
            );
    }

    std::cout << "attack command check: a click on an enemy replaced the commander's move "
                 "and it came to rest "
              << rested << " ticks later, " << moved
              << " units on; an area attack queued both enemies\n";
    clear_local_selection();
    match_command_ = MatchCommand::none;
    return_to_skirmish_menu();
}

} // namespace oa::app
