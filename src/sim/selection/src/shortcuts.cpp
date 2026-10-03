// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/selection/shortcuts.hpp"

namespace oa::sim::selection {
namespace {

/// Unit flags of a unit a shortcut may select: selected-able, with the selection bit apart.
constexpr uint32_t selected_and_selectable = OA_UNIT_FLAG_SELECTED | OA_UNIT_FLAG_SELECTABLE;
/// Unit.previous_health_percent at or below which Ctrl+B passes a unit over.
constexpr uint8_t lowest_skipped_health_byte = 1;

/// Tells whether a unit is finished: no build left, as an exact 0.0.
bool finished(const Unit& unit) noexcept {
    return unit.build_remaining == 0.0F;
}

/// Tells whether a type is a commander for the shortcut sets: it shows its
/// player's name and hides its damage.
bool commander_type(const UnitDef& def) noexcept {
    return (def.abilities & OA_UNIT_DEF_ABILITY_SHOW_PLAYER_NAME) != 0 &&
           (def.flags & OA_UNIT_DEF_FLAG_HIDE_DAMAGE) != 0;
}

/// Sets a type's bit in a mask.
void add_type(const TypeMask& mask, uint16_t type_id) noexcept {
    const uint32_t word = type_id >> 5;
    if (word < mask.word_count)
        mask.words[word] |= 1u << (type_id & 31u);
}

/// Copies a category's types into a mask, leaving out those `keep` refuses.
template <typename Keep>
uint32_t copy_category(
    const World& world, const TypeMask* category, const TypeMask& out, Keep&& keep
) noexcept {
    uint32_t found = 0;
    for (uint32_t index = 0; index < world.unit_def_count; ++index) {
        const UnitDef& def = world.unit_defs[index];
        if (!data::defs::category_mask_contains(category, def.type_id))
            continue;
        ++found;
        if (keep(def))
            add_type(out, def.type_id);
    }
    return found;
}

/// Fills a builder set when its category holds no type: the builders that
/// are no air base and no commander, mobile or not as `mobile` asks.
void fill_builders(const World& world, const TypeMask& out, bool mobile) noexcept {
    for (uint32_t index = 0; index < world.unit_def_count; ++index) {
        const UnitDef& def = world.unit_defs[index];
        if ((def.flags & (OA_UNIT_DEF_FLAG_BUILDER | OA_UNIT_DEF_FLAG_IS_AIRBASE)) !=
            OA_UNIT_DEF_FLAG_BUILDER)
            continue;
        if ((def.bm_code != 0) != mobile || commander_type(def))
            continue;
        add_type(out, def.type_id);
    }
}

Player* local_player(World& world) noexcept {
    return world_player(&world, world.game.local_player_index);
}

/// The local player's index as an economy record names its owner.
bool owned_by_local(const World& world, const Unit& unit) noexcept {
    const Player* owner = world_player_ref(const_cast<World*>(&world), unit.economy.player);
    return owner != nullptr && owner->index == world.game.local_player_index;
}

/// Deselects the local player's finished selectable units and empties the order panel.
void deselect_local(World& world) noexcept {
    if (const Player* player = local_player(world)) {
        uint32_t count = 0;
        Unit* first = world_player_units(&world, player, &count);
        for (uint32_t index = 0; index < count; ++index) {
            Unit& unit = first[index];
            if (unit.type_index != 0 && (unit.flags & OA_UNIT_FLAG_SELECTABLE) != 0 &&
                finished(unit))
                unit.flags &= ~OA_UNIT_FLAG_SELECTED;
        }
    }
    world.game.panel_unit_id = 0;
}

/// Refreshes the order panel and drops the armed command, as every shortcut ends.
void finish(World& world, const Hooks& hooks) {
    world.game.frame_flags =
        static_cast<uint16_t>(world.game.frame_flags | frame_flag_selection_changed);
    world.game.panel_unit_id = 0;
    if (hooks.reset_command != nullptr)
        hooks.reset_command(hooks.context);
}

/// Selects the on-screen units `wanted` takes among the local player's finished selectable ones.
template <typename Wanted>
uint32_t select_on_screen(World& world, const VisibleLists& lists, Wanted&& wanted) noexcept {
    uint32_t selected = 0;
    const int32_t listed = world.game.hot_unit_count;
    for (int32_t index = 0; index < listed && static_cast<uint32_t>(index) < lists.unit_capacity;
         ++index) {
        Unit* unit = world_unit_at(&world, lists.units[index]);
        if (unit == nullptr || (unit->flags & OA_UNIT_FLAG_SELECTABLE) == 0 || !finished(*unit) ||
            !wanted(*unit) || !owned_by_local(world, *unit))
            continue;
        unit->flags |= OA_UNIT_FLAG_SELECTED;
        ++selected;
    }
    return selected;
}

int32_t head_mission(const ShortcutHooks& shortcuts, const Unit& unit) {
    return shortcuts.head_mission != nullptr ? shortcuts.head_mission(shortcuts.context, unit) : -1;
}

/// Walks the local player's range for the next unit `idle` takes past the
/// cycle's place, wrapping once; see cycle_idle_constructor.
template <typename Idle>
Unit* cycle_idle(World& world, int32_t& place, const Hooks& hooks, Idle&& idle) {
    const Player* player = local_player(world);
    deselect_local(world);
    Unit* picked = nullptr;
    if (player != nullptr) {
        uint32_t range = 0;
        Unit* first = world_player_units(&world, player, &range);
        const auto last = static_cast<int32_t>(static_cast<int16_t>(player->unit_count));
        if (place > last)
            place = 0;
        for (;;) {
            for (int32_t index = place; index <= last && picked == nullptr; ++index) {
                if (first == nullptr || static_cast<uint32_t>(index) >= range)
                    break;
                Unit& unit = first[index];
                if (index > place && idle(unit))
                    picked = &unit;
                if (picked != nullptr)
                    place = index;
            }
            if (picked != nullptr || place == 0)
                break;
            place = 0;
        }
    }
    if (picked != nullptr) {
        picked->flags |= OA_UNIT_FLAG_SELECTED;
        if (hooks.center_camera != nullptr)
            hooks.center_camera(hooks.context, picked->position, true);
    }
    finish(world, hooks);
    return picked;
}

} // namespace

void build_shortcut_sets(
    const World& world,
    data::defs::CategoryRegistry& categories,
    uint32_t type_bits,
    ShortcutSets& sets
) noexcept {
    sets.mobile_combat = data::defs::category_mask_over(sets.mobile_combat_words, type_bits);
    sets.constructors = data::defs::category_mask_over(sets.constructor_words, type_bits);
    sets.factories = data::defs::category_mask_over(sets.factory_words, type_bits);
    const TypeMask* combat =
        data::defs::category_registry_find_or_add(&categories, mobile_combat_category);
    copy_category(world, combat, sets.mobile_combat, [](const UnitDef& def) {
        return (def.flags & OA_UNIT_DEF_FLAG_CAN_FLY) == 0;
    });
    const auto any = [](const UnitDef&) { return true; };
    const TypeMask* constructors =
        data::defs::category_registry_find_or_add(&categories, constructor_category);
    if (copy_category(world, constructors, sets.constructors, any) == 0)
        fill_builders(world, sets.constructors, true);
    const TypeMask* factories =
        data::defs::category_registry_find_or_add(&categories, factory_category);
    if (copy_category(world, factories, sets.factories, any) == 0)
        fill_builders(world, sets.factories, false);
}

uint32_t select_selected_types_on_screen(
    World& world, const VisibleLists& lists, uint32_t type_bits, const Hooks& hooks
) {
    data::defs::CategoryMaskStorage storage;
    const TypeMask selected_types = data::defs::category_mask_over(storage, type_bits);
    if (const Player* player = local_player(world)) {
        uint32_t count = 0;
        const Unit* first = world_player_units(&world, player, &count);
        for (uint32_t index = 0; index < count; ++index)
            if ((first[index].flags & OA_UNIT_FLAG_SELECTED) != 0)
                add_type(selected_types, first[index].type_index);
    }
    deselect_local(world);
    const uint32_t selected = select_on_screen(world, lists, [&](const Unit& unit) {
        return data::defs::category_mask_contains(&selected_types, unit.type_index);
    });
    finish(world, hooks);
    return selected;
}

uint32_t select_mobile_combat_on_screen(
    World& world, const VisibleLists& lists, const ShortcutSets& sets, const Hooks& hooks
) {
    deselect_local(world);
    const uint32_t selected = select_on_screen(world, lists, [&](const Unit& unit) {
        return data::defs::category_mask_contains(&sets.mobile_combat, unit.type_index);
    });
    finish(world, hooks);
    return selected;
}

Unit* cycle_idle_constructor(
    World& world,
    const ShortcutSets& sets,
    IdleCycle& cycle,
    const Hooks& hooks,
    const ShortcutHooks& shortcuts
) {
    return cycle_idle(world, cycle.constructor, hooks, [&](const Unit& unit) {
        if (unit.previous_health_percent <= lowest_skipped_health_byte || unit.movement == 0 ||
            !data::defs::category_mask_contains(&sets.constructors, unit.type_index))
            return false;
        const int32_t mission = head_mission(shortcuts, unit);
        return mission < 0 || mission == idle_standby_mission ||
               mission == idle_air_standby_mission;
    });
}

Unit* cycle_idle_factory(
    World& world,
    const ShortcutSets& sets,
    IdleCycle& cycle,
    const Hooks& hooks,
    const ShortcutHooks& shortcuts
) {
    return cycle_idle(world, cycle.factory, hooks, [&](const Unit& unit) {
        if ((unit.flags & OA_UNIT_FLAG_SELECTABLE) == 0 || !finished(unit) ||
            unit.economy.player == 0 ||
            !data::defs::category_mask_contains(&sets.factories, unit.type_index))
            return false;
        return head_mission(shortcuts, unit) != factory_busy_mission;
    });
}

uint32_t filter_box_selection(World& world, const ShortcutSets& sets, DragFilter filter) noexcept {
    const TypeMask* kept = nullptr;
    switch (filter) {
    case DragFilter::mobile_combat:
        kept = &sets.mobile_combat;
        break;
    case DragFilter::constructors:
        kept = &sets.constructors;
        break;
    case DragFilter::factories:
        kept = &sets.factories;
        break;
    case DragFilter::none:
        return 0;
    }
    uint32_t left = 0;
    const Player* player = local_player(world);
    if (player == nullptr)
        return 0;
    uint32_t count = 0;
    Unit* first = world_player_units(&world, player, &count);
    for (uint32_t index = 0; index < count; ++index) {
        Unit& unit = first[index];
        if ((unit.flags & selected_and_selectable) != selected_and_selectable || !finished(unit) ||
            unit.economy.player == 0)
            continue;
        if (data::defs::category_mask_contains(kept, unit.type_index)) {
            ++left;
        } else {
            unit.flags &= ~OA_UNIT_FLAG_SELECTED;
        }
    }
    return left;
}

} // namespace oa::sim::selection
