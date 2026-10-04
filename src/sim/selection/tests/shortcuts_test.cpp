// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The ui.selection-shortcuts selections over a small World: the shortcut
// sets and their fallbacks, the double-click and Ctrl+S selections of
// on-screen units, the Ctrl+B and Ctrl+F idle cycles with their place,
// wrap and skipped first unit, the drag-box filters and the queue step.
#include "oa/sim/selection/shortcuts.hpp"

#include <cstdio>
#include <cstdlib>
#include <initializer_list>

using namespace oa;
using namespace oa::sim::selection;

namespace {
int failures = 0;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x);                           \
            ++failures;                                                                            \
        }                                                                                          \
    } while (false)

// Types: 1 a tank in CTRL_W, 2 a flyer in CTRL_W, 3 a mobile builder,
// 4 a factory (builder without movement), 5 a commander builder.
constexpr uint16_t kTank = 1;
constexpr uint16_t kFlyer = 2;
constexpr uint16_t kConstructor = 3;
constexpr uint16_t kFactory = 4;
constexpr uint16_t kCommander = 5;
constexpr uint32_t kTypeBits = 64;

struct Recorder {
    int resets = 0;
    int32_t camera_x = -1;
};

Hooks hooks_for(Recorder& recorder) {
    Hooks hooks{};
    hooks.context = &recorder;
    hooks.reset_command = [](void* context) { ++static_cast<Recorder*>(context)->resets; };
    hooks.center_camera = [](void* context, const FixedVec3& position, bool) {
        static_cast<Recorder*>(context)->camera_x = position.x >> 16;
    };
    return hooks;
}

struct Scene {
    World* world = world_create();
    data::defs::CategoryRegistry categories{};
    uint16_t ids[16]{};
    VisibleLists lists{ids, 16, nullptr, 0};
    int32_t missions[16]{};

    Scene() {
        const WorldCapacity capacity{12, 8, 0};
        if (world == nullptr || world_alloc_tables(world, &capacity) == 0)
            std::abort();
        data::defs::category_registry_init(&categories);
        for (uint16_t type = 1; type <= kCommander; ++type)
            world->unit_defs[type].type_id = type;
        world->unit_defs[kFlyer].flags = OA_UNIT_DEF_FLAG_CAN_FLY;
        world->unit_defs[kConstructor].flags = OA_UNIT_DEF_FLAG_BUILDER;
        world->unit_defs[kConstructor].bm_code = 1;
        world->unit_defs[kFactory].flags = OA_UNIT_DEF_FLAG_BUILDER;
        world->unit_defs[kCommander].flags =
            OA_UNIT_DEF_FLAG_BUILDER | OA_UNIT_DEF_FLAG_HIDE_DAMAGE;
        world->unit_defs[kCommander].abilities = OA_UNIT_DEF_ABILITY_SHOW_PLAYER_NAME;
        world->unit_defs[kCommander].bm_code = 1;
        auto* combat = data::defs::category_registry_find_or_add(&categories, "CTRL_W");
        combat->words[0] |= (1u << kTank) | (1u << kFlyer);
        for (uint32_t index = 0; index < 2; ++index) {
            Player& player = world->game.players[index];
            player.index = static_cast<uint8_t>(index);
            player.in_use = 1;
        }
        // Player 0 owns slots 1..8, player 1 slots 9..10.
        world->game.players[0].first_unit = oa_unit_ref_from_slot(1);
        world->game.players[0].last_unit = oa_unit_ref_from_slot(8);
        world->game.players[1].first_unit = oa_unit_ref_from_slot(9);
        world->game.players[1].last_unit = oa_unit_ref_from_slot(10);
        world->game.local_player_index = 0;
        for (auto& mission : missions)
            mission = -1;
    }

    ~Scene() {
        data::defs::category_registry_clear(&categories);
        world_destroy(world);
    }

    Unit& unit(uint32_t slot, uint16_t type, uint8_t owner) {
        Unit& u = world->units[slot];
        u.id = static_cast<uint16_t>(slot);
        u.type_index = type;
        u.def = oa_ref_from_index(type);
        u.owner_index = owner;
        u.owner = oa_ref_from_index(owner);
        u.economy.player = oa_ref_from_index(owner);
        u.flags = OA_UNIT_FLAG_SELECTABLE | OA_UNIT_FLAG_LIVE;
        u.movement = world->unit_defs[type].bm_code != 0 || type == kTank || type == kFlyer ? 1 : 0;
        u.previous_health_percent = 100;
        u.position = {static_cast<int32_t>(slot * 100) << 16, 0, 0};
        return u;
    }

    void on_screen(std::initializer_list<uint16_t> listed) {
        world->game.hot_unit_count = 0;
        for (const uint16_t id : listed)
            ids[world->game.hot_unit_count++] = id;
    }

    // Builds the shortcut sets in place: their masks point into their own
    // words, so a copy would read the words of the sets it was copied from.
    void build_sets(ShortcutSets& sets) {
        build_shortcut_sets(*world, categories, kTypeBits, sets);
    }

    ShortcutHooks shortcut_hooks() {
        ShortcutHooks hooks{};
        hooks.context = this;
        hooks.head_mission = [](void* context, const Unit& unit) {
            return static_cast<Scene*>(context)->missions[unit.id];
        };
        return hooks;
    }

    [[nodiscard]] bool selected(uint32_t slot) const {
        return (world->units[slot].flags & OA_UNIT_FLAG_SELECTED) != 0;
    }
};

void sets_and_fallbacks() {
    Scene scene;
    ShortcutSets sets{};
    scene.build_sets(sets);
    // CTRL_W less the flyer.
    CHECK(data::defs::category_mask_contains(&sets.mobile_combat, kTank));
    CHECK(!data::defs::category_mask_contains(&sets.mobile_combat, kFlyer));
    // No CTRL_B or CTRL_F type: builders by movement, the commander left out.
    CHECK(data::defs::category_mask_contains(&sets.constructors, kConstructor));
    CHECK(!data::defs::category_mask_contains(&sets.constructors, kCommander));
    CHECK(!data::defs::category_mask_contains(&sets.constructors, kFactory));
    CHECK(data::defs::category_mask_contains(&sets.factories, kFactory));
    CHECK(!data::defs::category_mask_contains(&sets.factories, kConstructor));
    // An air base builder is no constructor.
    scene.world->unit_defs[kConstructor].flags |= OA_UNIT_DEF_FLAG_IS_AIRBASE;
    scene.build_sets(sets);
    CHECK(!data::defs::category_mask_contains(&sets.constructors, kConstructor));
    // A CTRL_B type replaces the fallback, whatever it is.
    data::defs::category_registry_find_or_add(&scene.categories, "CTRL_B")->words[0] |= 1u << kTank;
    scene.build_sets(sets);
    CHECK(data::defs::category_mask_contains(&sets.constructors, kTank));
    CHECK(!data::defs::category_mask_contains(&sets.constructors, kConstructor));
}

void double_click() {
    Scene scene;
    Recorder recorder;
    const Hooks hooks = hooks_for(recorder);
    auto& clicked = scene.unit(1, kTank, 0);
    scene.unit(2, kTank, 0);
    scene.unit(3, kTank, 0).build_remaining = 0.5F; // unfinished
    scene.unit(4, kTank, 0);                        // off screen
    scene.unit(5, kConstructor, 0);
    scene.unit(9, kTank, 1); // the enemy's
    clicked.flags |= OA_UNIT_FLAG_SELECTED;
    scene.world->units[4].flags |= OA_UNIT_FLAG_SELECTED; // off screen, dropped
    scene.world->game.panel_unit_id = 1;
    scene.on_screen({1, 2, 3, 5, 9});
    CHECK(select_selected_types_on_screen(*scene.world, scene.lists, kTypeBits, hooks) == 2);
    CHECK(scene.selected(1) && scene.selected(2) && !scene.selected(3) && !scene.selected(4));
    CHECK(!scene.selected(5) && !scene.selected(9));
    CHECK(scene.world->game.panel_unit_id == 0 && recorder.resets == 1);
    CHECK((scene.world->game.frame_flags & frame_flag_selection_changed) != 0);
    // An unfinished selected unit keeps its bit: only finished ones are dropped.
    scene.world->units[3].flags |= OA_UNIT_FLAG_SELECTED;
    scene.world->units[5].flags |= OA_UNIT_FLAG_SELECTED;
    CHECK(select_selected_types_on_screen(*scene.world, scene.lists, kTypeBits, hooks) == 3);
    CHECK(scene.selected(3) && scene.selected(5));
}

void ctrl_s() {
    Scene scene;
    Recorder recorder;
    const Hooks hooks = hooks_for(recorder);
    scene.unit(1, kTank, 0);
    scene.unit(2, kFlyer, 0);
    scene.unit(3, kConstructor, 0).flags |= OA_UNIT_FLAG_SELECTED;
    scene.unit(9, kTank, 1);
    scene.on_screen({1, 2, 3, 9});
    ShortcutSets sets{};
    scene.build_sets(sets);
    CHECK(select_mobile_combat_on_screen(*scene.world, scene.lists, sets, hooks) == 1);
    CHECK(scene.selected(1) && !scene.selected(2) && !scene.selected(3) && !scene.selected(9));
}

void idle_constructors() {
    Scene scene;
    Recorder recorder;
    const Hooks hooks = hooks_for(recorder);
    const auto shortcuts = scene.shortcut_hooks();
    // Range indices 0..4 = slots 1..5; Player.unit_count bounds the walk.
    scene.unit(1, kConstructor, 0); // index 0, never picked
    scene.unit(2, kConstructor, 0); // busy
    scene.unit(3, kConstructor, 0); // idle on Standby
    scene.unit(4, kConstructor, 0); // idle without orders
    scene.unit(5, kConstructor, 0); // past unit_count
    scene.missions[2] = 26;
    scene.missions[3] = idle_standby_mission;
    scene.world->game.players[0].unit_count = 3;
    ShortcutSets sets{};
    scene.build_sets(sets);
    IdleCycle cycle{};
    Unit* picked = cycle_idle_constructor(*scene.world, sets, cycle, hooks, shortcuts);
    CHECK(picked == &scene.world->units[3] && cycle.constructor == 2 && scene.selected(3));
    CHECK(recorder.camera_x == 300 && recorder.resets == 1);
    picked = cycle_idle_constructor(*scene.world, sets, cycle, hooks, shortcuts);
    CHECK(picked == &scene.world->units[4] && cycle.constructor == 3);
    CHECK(!scene.selected(3) && scene.selected(4));
    // Past the last one the walk wraps once to the start, skipping index 0.
    picked = cycle_idle_constructor(*scene.world, sets, cycle, hooks, shortcuts);
    CHECK(picked == &scene.world->units[3] && cycle.constructor == 2);
    // A health byte of 1 or a missing movement object passes a unit over.
    scene.world->units[4].previous_health_percent = 1;
    picked = cycle_idle_constructor(*scene.world, sets, cycle, hooks, shortcuts);
    CHECK(picked == &scene.world->units[3] && cycle.constructor == 2);
    scene.world->units[3].movement = 0;
    picked = cycle_idle_constructor(*scene.world, sets, cycle, hooks, shortcuts);
    CHECK(picked == nullptr && cycle.constructor == 0 && !scene.selected(3));
}

void idle_factories() {
    Scene scene;
    Recorder recorder;
    const Hooks hooks = hooks_for(recorder);
    const auto shortcuts = scene.shortcut_hooks();
    scene.unit(1, kFactory, 0);
    scene.unit(2, kFactory, 0); // building
    scene.unit(3, kFactory, 0).build_remaining = 0.25F;
    scene.unit(4, kFactory, 0); // queued but not building: idle
    scene.missions[2] = factory_busy_mission;
    scene.missions[4] = 26;
    scene.world->game.players[0].unit_count = 4;
    ShortcutSets sets{};
    scene.build_sets(sets);
    IdleCycle cycle{};
    Unit* picked = cycle_idle_factory(*scene.world, sets, cycle, hooks, shortcuts);
    CHECK(picked == &scene.world->units[4] && cycle.factory == 3 && cycle.constructor == 0);
    // Wrapping finds nothing new but the same factory again.
    picked = cycle_idle_factory(*scene.world, sets, cycle, hooks, shortcuts);
    CHECK(picked == &scene.world->units[4] && cycle.factory == 3);
}

void drag_filters() {
    CHECK(drag_filter(false, false, false) == DragFilter::none);
    CHECK(drag_filter(true, true, true) == DragFilter::mobile_combat);
    CHECK(drag_filter(false, true, true) == DragFilter::constructors);
    CHECK(drag_filter(false, false, true) == DragFilter::factories);
    Scene scene;
    scene.unit(1, kTank, 0).flags |= OA_UNIT_FLAG_SELECTED;
    scene.unit(2, kConstructor, 0).flags |= OA_UNIT_FLAG_SELECTED;
    scene.unit(3, kFactory, 0).flags |= OA_UNIT_FLAG_SELECTED;
    auto& unfinished = scene.unit(4, kConstructor, 0);
    unfinished.flags |= OA_UNIT_FLAG_SELECTED;
    unfinished.build_remaining = 0.5F;
    ShortcutSets sets{};
    scene.build_sets(sets);
    CHECK(filter_box_selection(*scene.world, sets, DragFilter::none) == 0);
    CHECK(scene.selected(1) && scene.selected(2) && scene.selected(3));
    CHECK(filter_box_selection(*scene.world, sets, DragFilter::constructors) == 1);
    CHECK(!scene.selected(1) && scene.selected(2) && !scene.selected(3));
    // An unfinished unit is left alone.
    CHECK(scene.selected(4));
}

void queue_step() {
    CHECK(shift_queue_step(false) == 5);
    CHECK(shift_queue_step(true) == 100);
}

} // namespace

int main() {
    sets_and_fallbacks();
    double_click();
    ctrl_s();
    idle_constructors();
    idle_factories();
    drag_filters();
    queue_step();
    if (failures != 0)
        return EXIT_FAILURE;
    std::puts("selection shortcut tests passed");
    return EXIT_SUCCESS;
}
