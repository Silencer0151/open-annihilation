// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The bottom bar's unit panel over a small World: the cursor unit's name,
// damage bar, logo, rates, kills, head order status and second unit; the
// unidentified line; the build button's cost line; the feature line; and the
// unit info panel's subject.
#include "oa/ui/hud/unit_panel.hpp"

#include "oa/ui/hud/chat_panel.hpp"

#include "check.hpp"
#include "fixtures.hpp"

#include <cstring>
#include <vector>

using namespace oa;
using namespace oa::ui::hud;

namespace {

// CampaignFile.kind of a skirmish.
constexpr int32_t kSessionSkirmish = 2;

// Mission indices in name order (kMissionOverlays).
constexpr uint8_t kMissionMobileBuild = 25;
constexpr uint8_t kMissionMoveGround = 26;
constexpr uint8_t kMissionBuildWeapon = 13;

constexpr uint16_t kArmlab = 1;
constexpr uint16_t kCommander = 2;
constexpr uint16_t kSilo = 3;
constexpr uint16_t kTank = 4;

/// Queues and sight the panel reads.
struct Panel {
    hud_test::TestWorld world;
    std::vector<OrderOverlay> primary[8];
    std::vector<OrderOverlay> secondary[8];
    bool sees_enemy = true;
    uint16_t hidden_unit = 0;

    Panel() {
        world.add_player(0, 1);
        world.add_player(1, 2);
        world.give_range(0, 1, 3);
        world.give_range(1, 4, 7);
        world.game().viewpoint_player = 0;
        world.game().local_player_index = 0;
        auto name = [&](uint16_t type, const char* unit_name, const char* shown) {
            auto& def = world.world->unit_defs[type];
            std::snprintf(def.unit_name, sizeof def.unit_name, "%s", unit_name);
            std::snprintf(def.name, sizeof def.name, "%s", shown);
            def.max_damage = 1000;
        };
        name(kArmlab, "ARMLAB", "Kbot Lab");
        name(kCommander, "ARMCOM", "Commander");
        name(kSilo, "ARMSILO", "Nuclear Missile Silo");
        name(kTank, "ARMSTUMP", "Stumpy");
        auto& lab = world.world->unit_defs[kArmlab];
        lab.build_cost_metal = 605.9F;
        lab.build_cost_energy = 1130.2F;
        std::snprintf(lab.description, sizeof lab.description, "%s", "Produces Kbots");
        world.world->unit_defs[kCommander].flags = OA_UNIT_DEF_FLAG_HIDE_DAMAGE;
    }

    OverlayContext overlay() {
        OverlayContext context{};
        context.world = world.world;
        context.missions = kMissionOverlays;
        context.sink.user = this;
        context.sink.orders = [](void* user, const Unit& unit, bool second) -> OrderOverlay* {
            auto& self = *static_cast<Panel*>(user);
            auto& queue = second ? self.secondary[unit.id] : self.primary[unit.id];
            for (size_t index = 0; index < queue.size(); ++index)
                queue[index].next = index + 1 < queue.size() ? &queue[index + 1] : nullptr;
            return queue.empty() ? nullptr : queue.data();
        };
        return context;
    }

    UnitPanelHooks hooks() {
        UnitPanelHooks h{};
        h.context = this;
        h.can_see = [](void* context, const Player& viewer, const Unit& unit) {
            auto& self = *static_cast<Panel*>(context);
            if (unit.id == self.hidden_unit)
                return false;
            return unit.owner_index == viewer.index || self.sees_enemy;
        };
        return h;
    }

    UnitPanelSnapshot
    shown(uint16_t unit, bool debug_keys = false, int32_t session_kind = kSessionSkirmish) {
        return unit_panel_snapshot(
            *world.world, unit, debug_keys, session_kind, overlay(), hooks()
        );
    }

    OrderOverlay& order(uint16_t unit, uint8_t mission, uint16_t target = 0) {
        auto& o = primary[unit].emplace_back();
        o.mission = mission;
        o.unit = &world.unit(unit);
        o.target = target != 0 ? &world.unit(target) : nullptr;
        return o;
    }
};

void test_idle_and_orders() {
    Panel p;
    auto& builder = p.world.spawn(1, kCommander);
    builder.health = 500;
    auto panel = p.shown(1);
    CHECK(panel.unit == 1 && !panel.unidentified);
    CHECK(std::strcmp(panel.name, "Commander") == 0);
    CHECK(panel.show_damage && panel.show_rates && panel.logo_player == 0);
    CHECK(std::strcmp(panel.mission_text, "Ready") == 0);
    CHECK(panel.second == PanelSecondUnit::none && !panel.show_kills);

    // A constructor building a structure: "Nanolathing" and the structure.
    p.world.spawn(2, kArmlab);
    p.order(1, kMissionMobileBuild, 2);
    panel = p.shown(1);
    CHECK(std::strcmp(panel.mission_text, "Nanolathing") == 0);
    CHECK(panel.second == PanelSecondUnit::target && panel.second_unit == 2);
    CHECK(panel.second_damage);
    CHECK(head_order_target(p.overlay(), builder) == 2);

    // A hidden target is not shown.
    p.hidden_unit = 2;
    panel = p.shown(1);
    CHECK(panel.second == PanelSecondUnit::none && panel.second_unit == 0);
    p.hidden_unit = 0;

    // A move with no target.
    p.primary[1].clear();
    p.order(1, kMissionMoveGround);
    panel = p.shown(1);
    CHECK(std::strcmp(panel.mission_text, "Moving") == 0 && panel.second == PanelSecondUnit::none);
    CHECK(std::strcmp(head_order_status_text(p.overlay(), nullptr), "Ready") == 0);
}

void test_enemy_units() {
    Panel p;
    p.world.spawn(1, kTank);
    auto& enemy = p.world.spawn(4, kCommander);
    enemy.flags |= OA_UNIT_FLAG_HAS_WEAPONS;
    enemy.veteran_level = 6;
    p.order(4, kMissionMobileBuild, 1);
    auto panel = p.shown(4);
    CHECK(std::strcmp(panel.name, "Commander") == 0 && panel.logo_player == 1);
    // An enemy commander's type hides its damage.
    CHECK(!panel.show_damage);
    CHECK(!panel.show_rates && !panel.show_kills && panel.mission_text[0] == '\0');
    CHECK(panel.second == PanelSecondUnit::none);
    // The debug keys show an enemy's rates, kills and status, but no second unit.
    panel = p.shown(4, true);
    CHECK(panel.show_rates && panel.show_kills);
    CHECK(std::strcmp(panel.mission_text, "Nanolathing") == 0);
    CHECK(panel.second == PanelSecondUnit::none);
    // An enemy whose type shows damage shows its bar.
    p.world.spawn(5, kTank);
    CHECK(p.shown(5).show_damage);
}

// In a multiplayer game a commander, and a type that shows its player's
// name, are named by their owner; other units and other games keep the type's
// name.
void test_owner_name() {
    Panel p;
    p.world.world->unit_defs[kCommander].abilities = OA_UNIT_DEF_ABILITY_COMMANDER;
    p.world.world->unit_defs[kSilo].abilities = OA_UNIT_DEF_ABILITY_SHOW_PLAYER_NAME;
    p.world.spawn(1, kCommander);
    p.world.spawn(2, kSilo);
    p.world.spawn(3, kTank);
    p.world.spawn(4, kCommander);
    CHECK(std::strcmp(p.shown(1, false, kSessionMultiplayer).name, "Player 0") == 0);
    CHECK(std::strcmp(p.shown(2, false, kSessionMultiplayer).name, "Player 0") == 0);
    CHECK(std::strcmp(p.shown(4, false, kSessionMultiplayer).name, "Player 1") == 0);
    CHECK(std::strcmp(p.shown(3, false, kSessionMultiplayer).name, "Stumpy") == 0);
    CHECK(std::strcmp(p.shown(1).name, "Commander") == 0);
    CHECK(std::strcmp(p.shown(2).name, "Nuclear Missile Silo") == 0);
    // An unseen commander stays unidentified.
    p.sees_enemy = false;
    CHECK(p.shown(4, false, kSessionMultiplayer).unidentified);
}

void test_unidentified() {
    Panel p;
    auto& enemy = p.world.spawn(4, kTank);
    p.sees_enemy = false;
    auto panel = p.shown(4);
    CHECK(panel.unit == 4 && panel.unidentified);
    CHECK(std::strcmp(panel.name, "R: Unidentified object") == 0);
    CHECK(!panel.show_damage && !panel.show_rates);
    enemy.flags |= OA_UNIT_FLAG_VIEWPOINT_OWNED;
    CHECK(std::strcmp(p.shown(4).name, "S: Unidentified object") == 0);
}

void test_kills_and_nothing() {
    Panel p;
    auto& unit = p.world.spawn(1, kTank);
    unit.veteran_level = 3;
    CHECK(!p.shown(1).show_kills); // unarmed
    unit.flags |= OA_UNIT_FLAG_HAS_WEAPONS;
    auto panel = p.shown(1);
    CHECK(panel.show_kills && std::strcmp(panel.kills, "3 kills") == 0);
    unit.veteran_level = 0;
    CHECK(!p.shown(1).show_kills);
    unit.veteran_level = 5;
    CHECK(std::strcmp(p.shown(1).kills, "5 kills - Veteran") == 0);
    // Only the unit under the cursor is shown: a selection alone shows nothing.
    unit.flags |= OA_UNIT_FLAG_SELECTED;
    CHECK(p.shown(0).unit == 0 && p.shown(0).name[0] == '\0');
    CHECK(p.shown(9).unit == 0);
}

void test_stockpile() {
    Panel p;
    auto& silo = p.world.spawn(1, kSilo);
    p.world.game().weapon_defs[4].reload_time = 200;
    silo.weapons[0].def = oa_ref_from_index(4);
    auto& build = p.secondary[1].emplace_back();
    build.mission = kMissionBuildWeapon;
    build.unit = &silo;
    build.state = kOrderStockpileBuild;
    build.parameter = 0;
    build.progress = 50;
    auto panel = p.shown(1);
    CHECK(panel.second == PanelSecondUnit::stockpile && panel.stockpile_percent == 25);
    CHECK(std::strcmp(panel_stockpile_label(nullptr, nullptr), "Weapon") == 0);
    // Another player's stockpile is not shown.
    auto& enemy_silo = p.world.spawn(4, kSilo);
    enemy_silo.weapons[0].def = oa_ref_from_index(4);
    p.secondary[4] = p.secondary[1];
    p.secondary[4][0].unit = &enemy_silo;
    CHECK(p.shown(4, true).second == PanelSecondUnit::none);
}

void test_build_button() {
    Panel p;
    char line[kPanelLineBytes];
    const char* description = nullptr;
    CHECK(build_button_readout(*p.world.world, "ARMLAB", line, sizeof line, &description));
    CHECK(std::strcmp(line, "Kbot Lab  M:605 E:1130") == 0);
    CHECK(description != nullptr && std::strcmp(description, "Produces Kbots") == 0);
    CHECK(build_button_readout(*p.world.world, "armlab", line, sizeof line, &description));
    CHECK(!build_button_readout(*p.world.world, "ARMNOTHING", line, sizeof line, &description));
    CHECK(description == nullptr);
    std::snprintf(
        p.world.world->unit_defs[5].unit_name,
        sizeof p.world.world->unit_defs[5].unit_name,
        "%s",
        "CORBUILD"
    );
    CHECK(!build_button_readout(*p.world.world, "CORBUILD", line, sizeof line, &description));
}

void test_feature() {
    Panel p;
    auto& game = p.world.game();
    game.feature_def_count = 4;
    auto& rock = p.world.world->feature_defs[2];
    std::snprintf(rock.name, sizeof rock.name, "%s", "Rock1");
    std::snprintf(rock.description, sizeof rock.description, "%s", "Rock");
    rock.metal = 30.0F;
    rock.energy = 5.0F;
    char line[kPanelLineBytes];
    game.cursor_feature = 0xffff;
    CHECK(!feature_readout(*p.world.world, false, nullptr, nullptr, line, sizeof line));
    game.cursor_feature = 2;
    CHECK(feature_readout(*p.world.world, false, nullptr, nullptr, line, sizeof line));
    CHECK(std::strcmp(line, "Rock  M:30 E:5") == 0);
    CHECK(feature_readout(*p.world.world, true, nullptr, nullptr, line, sizeof line));
    CHECK(std::strcmp(line, "Rock1  M:30 E:5") == 0);
    rock.energy = 0.0F;
    CHECK(feature_readout(*p.world.world, false, nullptr, nullptr, line, sizeof line));
    CHECK(std::strcmp(line, "Rock  M:30") == 0);
    rock.flags = OA_FEATURE_FLAG_INDESTRUCTIBLE;
    CHECK(feature_readout(*p.world.world, false, nullptr, nullptr, line, sizeof line));
    CHECK(std::strcmp(line, "Rock") == 0);
    rock.flags = OA_FEATURE_FLAG_NO_DISPLAY_INFO;
    CHECK(!feature_readout(*p.world.world, false, nullptr, nullptr, line, sizeof line));
    CHECK(feature_readout(*p.world.world, true, nullptr, nullptr, line, sizeof line));
}

// F1's subject: the build button under the pointer before the cursor unit.
void test_unit_info_subject() {
    Panel p;
    p.world.spawn(1, kTank);
    p.world.game().cursor_unit_id = 1;
    const auto type_for_name = [](void* user, const char* name) -> uint16_t {
        auto& self = *static_cast<Panel*>(user);
        for (uint32_t type = 1; type < self.world.world->unit_def_count; ++type)
            if (std::strcmp(self.world.world->unit_defs[type].unit_name, name) == 0)
                return static_cast<uint16_t>(type);
        return 0;
    };
    const auto can_see = [](void* context, const Player& viewer, const Unit& unit) {
        return static_cast<Panel*>(context)->hooks().can_see(context, viewer, unit);
    };
    CHECK(unit_info_subject(*p.world.world, "ARMLAB", type_for_name, can_see, &p) == kArmlab);
    CHECK(unit_info_subject(*p.world.world, nullptr, type_for_name, can_see, &p) == kTank);
    // A gadget that names no unit shows nothing, not the cursor unit.
    CHECK(unit_info_subject(*p.world.world, "ARMATTACK", type_for_name, can_see, &p) == 0);
    p.hidden_unit = 1;
    CHECK(unit_info_subject(*p.world.world, nullptr, type_for_name, can_see, &p) == 0);
    p.world.game().cursor_unit_id = 0;
    CHECK(unit_info_subject(*p.world.world, nullptr, type_for_name, can_see, &p) == 0);
}

} // namespace

int main() {
    test_idle_and_orders();
    test_enemy_units();
    test_owner_name();
    test_unidentified();
    test_kills_and_nothing();
    test_stockpile();
    test_build_button();
    test_feature();
    test_unit_info_subject();
    std::puts("ui-hud-unit-panel-test: ok");
    return 0;
}
