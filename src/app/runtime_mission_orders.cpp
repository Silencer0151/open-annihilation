// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Orders a headless campaign run gives the local player's units, so that a
// mission plays towards its victory conditions without a player.
#include "oa/app/runtime.hpp"
#include "oa/core/map_plot.h"
#include "oa/data/mission_types.hpp"
#include "oa/sim/gameplay_input/input.hpp"
#include "oa/sim/match_runtime.hpp"
#include "oa/ui/hud/order_panel.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace oa::app {
namespace {

namespace scenario = oa::sim::scenario;

// World positions are 16.16 fixed-point pixels; map cells are 16 pixels
// square, so a position shifted right by kCellShift is its cell.
constexpr int32_t kFixedShift = 16;
constexpr int32_t kFixedOne = 1 << kFixedShift;
constexpr int32_t kCellShift = 20;
static_assert(1 << (kCellShift - kFixedShift) == OA_MAP_CELL_PIXELS);
// Rings of map cells searched around a builder for a structure's site.
constexpr int32_t kSiteNearest = 3;
constexpr int32_t kSiteFarthest = 24;
// Cells between the sites tried along a ring.
constexpr int32_t kSiteStep = 2;
// Units of each armed type a factory with nothing queued is given to build.
constexpr int32_t kFactoryBatch = 2;
// Armed mobile units the local player gathers before they attack, when it
// has a builder or a factory to add to them.
constexpr std::size_t kAttackGroup = 8;

/// Returns the squared map distance between two units, whole world units.
[[nodiscard]] int64_t squared_distance(const oa::Unit& from, const oa::Unit& to) {
    const auto dx =
        static_cast<int64_t>(from.position.x >> kFixedShift) - (to.position.x >> kFixedShift);
    const auto dz =
        static_cast<int64_t>(from.position.z >> kFixedShift) - (to.position.z >> kFixedShift);
    return dx * dx + dz * dz;
}

/// Tells whether a type is a structure that makes energy: a negative energy
/// use, as solar collectors have, counts as making it.
[[nodiscard]] bool makes_energy(const oa::UnitDef& def) {
    return def.bm_code == 0 && (def.energy_make > 0.0F || def.energy_use < 0.0F ||
                                def.wind_generator > 0.0F || def.tidal_generator > 0.0F);
}

/// Tells whether a type is a structure that extracts metal.
[[nodiscard]] bool extracts_metal(const oa::UnitDef& def) {
    return def.bm_code == 0 && def.extracts_metal > 0.0F;
}

/// Tells whether a type is a structure that builds units.
[[nodiscard]] bool builds_units(const oa::UnitDef& def) {
    return def.bm_code == 0 && (def.flags & OA_UNIT_DEF_FLAG_BUILDER) != 0;
}

// One kind of structure a mobile builder puts up, and how many the player
// should own.
struct BaseStructure {
    bool (*wanted)(const oa::UnitDef&){};
    std::size_t count{};
};

// What a mobile builder puts up, in order: the first kind the player owns
// fewer of than its count.
constexpr std::array<BaseStructure, 5> kBaseStructures{
    {{makes_energy, 1},
     {extracts_metal, 1},
     {builds_units, 1},
     {makes_energy, 3},
     {extracts_metal, 3}}
};

} // namespace

std::optional<MissionMoveGoal> Runtime::mission_move_goal() {
    if (!match_ || !selected_tnt_)
        return std::nullopt;
    for (const auto& entry : match_->scenario_controller().victory) {
        if (!entry || entry->kind != scenario::Kind::move_unit_to_radius)
            continue;
        MissionMoveGoal goal;
        goal.type_name = entry->type_name;
        const auto& point = entry->point;
        if (point.y == scenario::unplaced_point_height) {
            // The condition's point is a screen-plane position until the game
            // first places it on the terrain.
            const auto& game = match_->state().game;
            const oa::sim::unit_movement::Terrain terrain(*selected_tnt_);
            const auto placed = oa::sim::gameplay_input::terrain_intersection(
                terrain,
                point.x,
                point.z,
                static_cast<int32_t>(game.map_width_world),
                static_cast<int32_t>(game.map_height_world)
            );
            goal.point = oa::sim::ground_orders::Point{placed.x, placed.y, placed.z};
        } else {
            goal.point = oa::sim::ground_orders::Point{point.x, point.y, point.z};
        }
        return goal;
    }
    return std::nullopt;
}

std::vector<uint16_t> Runtime::offered_build_types(uint16_t builder) {
    std::vector<uint16_t> offered;
    if (!match_)
        return offered;
    const auto previous = std::exchange(selected_match_unit_, builder);
    const auto pages = builder_gui_page_count();
    for (int page = 1; page <= pages; ++page) {
        show_match_build_page(page);
        if (!match_hud_ || match_build_page_ != page)
            continue;
        for (const auto& gadget : match_hud_->layout.gadgets) {
            const auto* button = std::get_if<oa::ui::gui_layout::ButtonFields>(&gadget.fields);
            if (button == nullptr || button->grayed_out ||
                (static_cast<uint8_t>(gadget.common.common_attributes) &
                 oa::ui::hud::kCommonUnitButton) == 0)
                continue;
            const auto type =
                oa::sim::unit_spawn::find_type_index(spawn_type_names_, gadget.common.name);
            if (type != 0 && std::find(offered.begin(), offered.end(), type) == offered.end())
                offered.push_back(type);
        }
    }
    selected_match_unit_ = previous;
    apply_match_hud_for_selection();
    return offered;
}

bool Runtime::order_structure_near(uint16_t builder, uint16_t type) {
    auto& world = match_->state();
    const auto& slots = match_->world().slots;
    if (type == 0 || type >= spawn_types_.size() || builder >= slots.size() ||
        slots[builder].unit == nullptr)
        return false;
    const auto& unit = slots[builder].record;
    const auto cell_x = static_cast<int32_t>(unit.position.x >> kCellShift);
    const auto cell_z = static_cast<int32_t>(unit.position.z >> kCellShift);
    const auto fx = std::max<int32_t>(spawn_types_[type].footprint_x, 1);
    const auto fz = std::max<int32_t>(spawn_types_[type].footprint_z, 1);
    // An extractor goes where its footprint holds the most metal; anything
    // else takes the first clear site.
    const bool extractor = extracts_metal(world.unit_defs[type]);
    const auto metal_under = [&](int32_t x, int32_t z) {
        int32_t metal = 0;
        for (int32_t row = 0; row < fz; ++row)
            for (int32_t column = 0; column < fx; ++column)
                if (const auto* plot = oa::world_plot(&world, x + column, z + row))
                    metal += plot->metal;
        return metal;
    };
    std::optional<oa::sim::ground_orders::Point> best;
    int32_t best_metal = -1;
    for (int32_t ring = kSiteNearest; ring < kSiteFarthest && (extractor || !best); ++ring)
        for (int32_t dz = -ring; dz <= ring; dz += kSiteStep)
            for (int32_t dx = -ring; dx <= ring; dx += kSiteStep) {
                if (dx != -ring && dx != ring && dz != -ring && dz != ring)
                    continue;
                const auto x = cell_x + dx;
                const auto z = cell_z + dz;
                const auto height = match_->building_site(type, x, z, 0, match_local_player_);
                if (!height)
                    continue;
                const auto metal = extractor ? metal_under(x, z) : 0;
                if (best && metal <= best_metal)
                    continue;
                // The footprint's centre, at the site's height.
                best = oa::sim::ground_orders::Point{
                    (x * OA_MAP_CELL_PIXELS + fx * OA_MAP_CELL_PIXELS / 2) * kFixedOne,
                    static_cast<int32_t>(static_cast<uint32_t>(*height) << kFixedShift),
                    (z * OA_MAP_CELL_PIXELS + fz * OA_MAP_CELL_PIXELS / 2) * kFixedOne
                };
                best_metal = metal;
            }
    if (!best)
        return false;
    match_->issue_mobile_build(builder, type, *best, false);
    return true;
}

void Runtime::give_mission_orders() {
    if (!match_ || !selected_tnt_)
        return;
    auto& world = match_->state();
    const auto& slots = match_->world().slots;
    const auto local = match_local_player_;
    const auto& alliance = world.game.players[local].alliance;
    const auto standby = oa::data::mission_types::index_for_name("Standby");
    const auto chase = oa::data::mission_types::index_for_name("Attack_Chase");

    // A unit's first order: its kind (Standby when it has none) and target.
    struct FirstOrder {
        bool seen{};
        uint8_t kind{};
        uint16_t target{};
    };

    const auto first_order = [&](uint16_t unit) {
        FirstOrder first;
        match_->visit_saved_orders(
            unit,
            [](void* walk,
               const oa::data::persist::SavedOrder* order,
               const oa::data::persist::SavedGoal*) {
                auto& state = *static_cast<FirstOrder*>(walk);
                if (!state.seen) {
                    state.seen = true;
                    state.kind = order->kind;
                    state.target = order->target_id;
                }
            },
            &first
        );
        if (!first.seen)
            first.kind = standby;
        return first;
    };
    const auto idle = [&](uint16_t unit) { return first_order(unit).kind == standby; };
    const auto name_of = [&](const oa::Unit& unit) -> std::string_view {
        return unit.type_index < spawn_type_names_.size()
                   ? std::string_view(spawn_type_names_[unit.type_index])
                   : std::string_view{};
    };
    const auto live = [](const oa::sim::unit_spawn::Slot& slot) {
        return slot.unit != nullptr && slot.record.type_index != 0 &&
               (slot.record.flags & OA_UNIT_FLAG_LIVE) != 0;
    };
    const auto enemy = [&](const oa::Unit& unit) {
        return unit.owner_index != local &&
               (unit.owner_index >= sizeof alliance || alliance[unit.owner_index] == 0);
    };

    // The victory the orders work towards: a point some units must reach, a
    // type to destroy, or every enemy unit.
    const auto move_goal = mission_move_goal();
    std::optional<oa::sim::ground_orders::Point> goal;
    std::string goal_type;
    if (move_goal) {
        goal = move_goal->point;
        goal_type = move_goal->type_name;
    }
    std::string destroy_type;
    for (const auto& entry : match_->scenario_controller().victory) {
        if (!entry)
            continue;
        switch (entry->kind) {
        case scenario::Kind::kill_unit_type:
        case scenario::Kind::kill_all_of_type:
        case scenario::Kind::capture_unit_type:
            if (destroy_type.empty())
                destroy_type = entry->type_name;
            break;
        default:
            break;
        }
    }

    const auto nearest_enemy = [&](const oa::Unit& from) -> uint16_t {
        uint16_t best = 0;
        auto best_distance = std::numeric_limits<int64_t>::max();
        bool best_named = false;
        for (const auto& slot : slots) {
            if (!live(slot) || !enemy(slot.record))
                continue;
            const bool named =
                !destroy_type.empty() && tdf_names_equal(name_of(slot.record), destroy_type);
            const auto distance = squared_distance(from, slot.record);
            if ((named && !best_named) || (named == best_named && distance < best_distance)) {
                best = slot.unit_index;
                best_distance = distance;
                best_named = named;
            }
        }
        return best;
    };
    const auto owned = [&](bool (*wanted)(const oa::UnitDef&)) {
        return static_cast<std::size_t>(
            std::count_if(slots.begin(), slots.end(), [&](const auto& slot) {
                if (!live(slot) || slot.record.owner_index != local)
                    return false;
                const auto* def = oa::world_unit_def_of(&world, &slot.record);
                return def != nullptr && wanted(*def);
            })
        );
    };

    // The armed mobile units that are not builders: they attack together once
    // enough have gathered, or at once when no builder can add to them.
    std::vector<uint16_t> fighters;
    std::size_t movers = 0;
    std::size_t builds = 0;
    std::size_t builders = 0;
    std::size_t factories = 0;
    for (const auto& slot : slots) {
        if (!live(slot) || slot.record.owner_index != local || slot.record.build_remaining != 0.0F)
            continue;
        const auto unit = slot.unit_index;
        const auto* def = oa::world_unit_def_of(&world, &slot.record);
        if (def == nullptr)
            continue;
        const bool builder = (def->flags & OA_UNIT_DEF_FLAG_BUILDER) != 0;
        const bool armed = (def->flags & OA_UNIT_DEF_FLAG_HAS_WEAPONS) != 0;
        const bool mobile = def->bm_code != 0 && match_->takes_move_order(unit);
        try {
            if (builder && !mobile) {
                // A factory with nothing queued builds a batch of each armed
                // unit its build pages offer.
                if (!idle(unit))
                    continue;
                bool queued = false;
                for (const auto type : offered_build_types(unit)) {
                    const auto& offered = world.unit_defs[type];
                    if (offered.bm_code == 0 || (offered.flags & OA_UNIT_DEF_FLAG_HAS_WEAPONS) == 0)
                        continue;
                    match_->queue_factory_build(unit, type, kFactoryBatch);
                    queued = true;
                }
                factories += queued ? 1 : 0;
                continue;
            }
            if (builder && mobile) {
                ++builders;
                // An idle mobile builder puts up the first base structure the
                // player lacks that its build pages offer.
                if (!idle(unit))
                    continue;
                const auto offered = offered_build_types(unit);
                for (const auto& structure : kBaseStructures) {
                    if (owned(structure.wanted) >= structure.count)
                        continue;
                    const auto type = std::find_if(offered.begin(), offered.end(), [&](uint16_t t) {
                        return structure.wanted(world.unit_defs[t]);
                    });
                    if (type != offered.end() && order_structure_near(unit, *type)) {
                        ++builds;
                        break;
                    }
                }
                continue;
            }
            if (!mobile || !armed)
                continue;
            if (goal && (goal_type.empty() || tdf_names_equal(name_of(slot.record), goal_type))) {
                match_->issue_ground_move(unit, *goal, false);
                ++movers;
                continue;
            }
            fighters.push_back(unit);
        } catch (const std::exception& error) {
            std::printf("mission orders: unit %u: %s\n", static_cast<unsigned>(unit), error.what());
        }
    }
    // Once some fighters chase the type the victory names, the fighters that
    // gather join them.
    const bool attacking =
        !destroy_type.empty() && std::any_of(fighters.begin(), fighters.end(), [&](uint16_t unit) {
            const auto order = first_order(unit);
            return order.kind == chase && order.target != 0 && order.target < slots.size() &&
                   live(slots[order.target]) &&
                   tdf_names_equal(name_of(slots[order.target].record), destroy_type);
        });
    std::size_t attackers = 0;
    if ((builders == 0 && owned(builds_units) == 0) || attacking || fighters.size() >= kAttackGroup)
        for (const auto unit : fighters)
            try {
                if (const auto target = nearest_enemy(slots[unit].record); target != 0)
                    attackers += match_->issue_attack(unit, target, true) ? 1 : 0;
            } catch (const std::exception& error) {
                std::printf(
                    "mission orders: unit %u: %s\n", static_cast<unsigned>(unit), error.what()
                );
            }
    std::printf(
        "mission orders: tick %u: %zu to the goal, %zu of %zu fighters attacking, %zu builds, "
        "%zu factories queued\n",
        static_cast<unsigned>(match_timing_.tick),
        movers,
        attackers,
        fighters.size(),
        builds,
        factories
    );
    std::fflush(stdout);
}

} // namespace oa::app
