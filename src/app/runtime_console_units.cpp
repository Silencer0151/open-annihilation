// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The console commands that reach the units and the path search: "ShootAll",
// "Assign" and "Search", checked through the chat line on a running match.
#include "oa/app/runtime.hpp"
#include "oa/app/match_console.hpp"

#include "oa/sim/match_runtime/attack_orders.hpp"
#include "oa/sim/match_runtime.hpp"
#include "oa/sim/match_runtime/mission_unit_binding.hpp"
#include "oa/sim/ground_orders/search_worker.hpp"
#include "oa/sim/mission_units.hpp"
#include "oa/ui/console/console.hpp"
#include "oa/ui/console/game_fields.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace oa::app {

namespace {

namespace console = oa::ui::console;
namespace runtime = oa::sim::match_runtime;

// The navigation check moves the commander down and the display check put a
// solar collector to its right; these two go above and to the left.
constexpr int32_t kSolarAboveCommander = 96 << 16;
constexpr int32_t kSolarLeftOfCommander = 96 << 16;
constexpr uint32_t kFireAtWill = 2;

uint32_t fire_order(const oa::Unit& unit) {
    return (unit.flags & OA_UNIT_FLAG_FIRE_ORDER_MASK) >> OA_UNIT_FLAG_FIRE_ORDER_SHIFT;
}

void set_fire_order(oa::Unit& unit, uint32_t order) {
    unit.flags = (unit.flags & ~OA_UNIT_FLAG_FIRE_ORDER_MASK) |
                 ((order << OA_UNIT_FLAG_FIRE_ORDER_SHIFT) & OA_UNIT_FLAG_FIRE_ORDER_MASK);
}

} // namespace

void Runtime::check_console_unit_commands(const std::function<void(const char*)>& enter_line) {
    oa::World& world = match_->state();
    oa::Game& game = world.game;
    const auto require = [](bool ok, const std::string& what) {
        if (!ok)
            throw std::runtime_error("console unit check: " + what);
    };
    const auto tick = [&] {
        ++match_timing_.tick;
        match_->simulation().tick = match_timing_.tick;
        match_->tick();
    };
    const uint8_t local = game.local_player_index;
    uint8_t other = OA_PLAYER_COUNT;
    for (uint8_t i = OA_PLAYER_COUNT; i-- > 0;)
        if (i != local && game.players[i].status == OA_PLAYER_STATUS_COMPUTER)
            other = i;
    const auto def_of = [&](const oa::Unit& unit) { return oa::world_unit_def_of(&world, &unit); };
    oa::sim::unit_spawn::Slot* commander = nullptr;
    for (auto& slot : match_->world().slots)
        if (commander == nullptr && slot.unit != nullptr && slot.record.type_index != 0 &&
            slot.record.owner_index == local && (slot.record.flags & OA_UNIT_FLAG_LIVE) != 0 &&
            def_of(slot.record) != nullptr &&
            (def_of(slot.record)->abilities & OA_UNIT_DEF_ABILITY_FIRE_STAND_ORDERS) != 0)
            commander = &slot;
    const auto solar = oa::sim::unit_spawn::find_type_index(spawn_type_names_, "ARMSOLAR");
    require(
        other != OA_PLAYER_COUNT && commander != nullptr && solar != 0 &&
            console_->host.create_unit != nullptr,
        "needs a computer player, a local unit with fire orders and the solar collector type"
    );
    // Places a solar collector and returns its slot id.
    const auto place_solar = [&](uint8_t owner, const FixedVec3& position) -> uint16_t {
        std::vector<uint16_t> before;
        for (const auto& slot : match_->world().slots)
            if (slot.unit != nullptr && slot.record.type_index == solar)
                before.push_back(slot.unit_index);
        console_->host.create_unit(this, owner, solar, &position);
        for (const auto& slot : match_->world().slots)
            if (slot.unit != nullptr && slot.record.type_index == solar &&
                slot.record.owner_index == owner &&
                std::find(before.begin(), before.end(), slot.unit_index) == before.end())
                return slot.unit_index;
        return 0;
    };
    const auto at = commander->record.position;
    const auto enemy = place_solar(other, {at.x, at.y, at.z - kSolarAboveCommander});
    require(enemy != 0, "no solar collector placed for the computer player");
    const auto* enemy_def = def_of(match_->world().slots[enemy].record);
    require(
        enemy_def != nullptr && (enemy_def->flags & OA_UNIT_DEF_FLAG_SHOOT_ME) == 0,
        "the solar collector has ShootMe"
    );

    // ShootAll.
    const auto saved_fire = fire_order(commander->record);
    set_fire_order(commander->record, kFireAtWill);
    for (int i = 0; i < 30; ++i)
        tick();
    const auto shoot_all = [&] {
        return (console::console_flags(game) & console::console_flag::shoot_all) != 0;
    };
    const auto picked = [&] {
        const auto* target = match_->find_automatic_target(*commander->unit);
        return target != nullptr ? target->record.id : uint16_t{0};
    };
    require(!shoot_all(), "ShootAll is on before the check");
    require(picked() == 0, "the commander picks a target without ShootMe before +shootall");
    enter_line("+shootall");
    require(shoot_all(), "+shootall did not set the ShootAll bit of Game.console_flags");
    require(
        picked() == enemy,
        "with +shootall the commander picks " + std::to_string(picked()) +
            ", not the computer player's solar collector " + std::to_string(enemy)
    );
    enter_line("+shootall");
    require(!shoot_all() && picked() == 0, "a second +shootall did not turn it off");

    // Assign.
    const auto own = place_solar(local, {at.x - kSolarLeftOfCommander, at.y, at.z});
    require(own != 0, "no solar collector placed for the local player");
    // The unit's queued Standing_FireOrder, kind 0 when it has none.
    const auto fire_order_record = [&](uint16_t id) {
        std::array<runtime::Match::OrderRecordView, 8> records{};
        const auto count = match_->queue_records(id, false, records.data(), records.size());
        for (std::size_t i = 0; i < count; ++i)
            if (records[i].kind == runtime::standing_fire_order_kind)
                return records[i];
        return runtime::Match::OrderRecordView{};
    };
    const auto flags = console::console_flags(game);
    const auto developer = [&](bool on) {
        console::set_console_flags(
            game,
            static_cast<uint16_t>(
                on ? flags | console::console_flag::developer
                   : flags & ~console::console_flag::developer
            )
        );
    };
    developer(false);
    const auto selected = selected_local_ids();
    clear_local_selection();
    adopt_selection(commander->unit_index);
    adopt_selection(own);
    enter_line("+assign Standing_FireOrder 2");
    require(
        fire_order_record(commander->unit_index).kind == 0, "+assign ran without the passphrase"
    );
    developer(true);
    enter_line("+assign Standing_FireOrder 2");
    const auto order = fire_order_record(commander->unit_index);
    require(
        order.kind == runtime::standing_fire_order_kind && order.parameter_1 == 0 &&
            order.parameter_2 == 2,
        "+assign gave the commander kind " + std::to_string(order.kind) + " words " +
            std::to_string(order.parameter_1) + "," + std::to_string(order.parameter_2)
    );
    require(
        fire_order_record(own).kind == 0, "+assign gave a fire order to a type without fire orders"
    );
    for (int i = 0; i < 30 && fire_order(commander->record) != 0; ++i)
        tick();
    require(
        fire_order(commander->record) == 0,
        "the assigned fire order left fire order " + std::to_string(fire_order(commander->record))
    );
    clear_local_selection();
    for (const auto id : selected)
        adopt_selection(id);
    set_fire_order(commander->record, saved_fire);
    // The two collectors go through the kill sweep (outcome 8) so
    // that the later checks see the scene they expect.
    oa::sim::match_runtime::MissionUnitBinding binding{*match_, {}};
    const auto hooks = oa::sim::match_runtime::mission_unit_hooks(binding);
    for (const auto id : {enemy, own})
        hooks.kill(
            hooks.context,
            match_->world().slots[id].record,
            oa::sim::mission_units::kill_outcome_removed
        );
    tick();
    require(
        (match_->world().slots[enemy].record.flags & OA_UNIT_FLAG_LIVE) == 0 &&
            (match_->world().slots[own].record.flags & OA_UNIT_FLAG_LIVE) == 0,
        "the placed solar collectors were not removed"
    );

    // Search.
    auto& jobs = match_->path_search_jobs();
    require(
        jobs.tick_credit == oa::sim::ground_orders::search_tick_credit &&
            jobs.base_heuristic == oa::sim::ground_orders::search_heuristic_scale,
        "the path search does not start at 1333 nodes and weight 1.5"
    );
    constexpr int32_t kWeight2_5 = 0x28000; // 2.5 * 65536
    developer(false);
    enter_line("+search 500 2.5");
    require(
        jobs.tick_credit == oa::sim::ground_orders::search_tick_credit &&
            jobs.base_heuristic == oa::sim::ground_orders::search_heuristic_scale,
        "+search ran without the passphrase"
    );
    developer(true);
    enter_line("+search 500");
    require(
        jobs.tick_credit == 500 &&
            jobs.base_heuristic == oa::sim::ground_orders::search_heuristic_scale,
        "+search 500 did not set only the node credit"
    );
    enter_line("+search 0 2.5");
    require(
        jobs.tick_credit == 500 && jobs.base_heuristic == kWeight2_5,
        "+search 0 2.5 did not set only the weight"
    );
    enter_line("+search 1333 1.5");
    require(
        jobs.tick_credit == oa::sim::ground_orders::search_tick_credit &&
            jobs.base_heuristic == oa::sim::ground_orders::search_heuristic_scale,
        "+search 1333 1.5 did not restore the defaults"
    );
    console::set_console_flags(game, flags);
    std::cout << "console unit check: +shootall let the commander pick a solar collector, "
                 "+assign gave the selection a standing fire order, +search set the path "
                 "search's node credit and weight\n";
}

} // namespace oa::app
