// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/weapon_execution/retaliation.hpp"

#include "oa/core/player.h"
#include "oa/core/unit_def.h"
#include "oa/core/weapon_def.h"

#include <cstdio>
#include <cstdlib>

using namespace oa;
using namespace oa::sim::weapon_execution;

namespace {
int failures = 0;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x);                           \
            ++failures;                                                                            \
        }                                                                                          \
    } while (false)

constexpr int32_t fx(int32_t whole) {
    return static_cast<int32_t>(static_cast<uint32_t>(whole) << 16);
}

constexpr oa_ref32 gun_def = 1;
constexpr oa_ref32 dgun_def = 2;
constexpr oa_ref32 excluded_category = 0x99; // the category ref the hook reports as holding type 2
constexpr uint32_t victim_slot = 1;
constexpr uint32_t attacker_slot = 2;
constexpr uint32_t bystander_slot = 3;

struct Recorder {
    bool accept_attack = false;
    int attack_requests = 0;
    int random_draws = 0;
    bool has_head = false; // the victim's head order
    uint32_t head = 0;
    uint32_t chase_head = 0; // head flags an accepted attack order installs
    int alerts = 0;
    uint32_t alert_tick = 0;
    bool alert_clears_orders = false;
};

bool category_contains(void*, oa_ref32 mask, uint16_t type_index) {
    return mask == excluded_category && type_index == 2;
}

bool queue_attack(void* context, Unit&, Unit&) {
    auto& r = *static_cast<Recorder*>(context);
    ++r.attack_requests;
    if (r.accept_attack && r.chase_head != 0) {
        r.has_head = true;
        r.head = r.chase_head;
    }
    return r.accept_attack;
}

bool head_order_flags(void* context, const Unit&, uint32_t* flags) {
    auto& r = *static_cast<Recorder*>(context);
    *flags = r.head;
    return r.has_head;
}

void computer_alert(void* context, Unit&, uint32_t tick) {
    auto& r = *static_cast<Recorder*>(context);
    ++r.alerts;
    r.alert_tick = tick;
    if (r.alert_clears_orders)
        r.has_head = false;
}

uint32_t fixed_random(void* context, uint32_t) {
    ++static_cast<Recorder*>(context)->random_draws;
    return 7;
}

RetaliationHooks hooks_for(Recorder& r) {
    return {&r, category_contains, head_order_flags, queue_attack, fixed_random, computer_alert};
}

// Player 0 (local) owns the armed victim at the origin with a 100-unit gun in
// slot 0 set to return fire; player 1 owns the attacker 50 units away and a
// bystander 30 units away.
World* make_world() {
    World* w = world_create();
    WorldCapacity cap{8, 4, 0};
    if (w == nullptr || !world_alloc_tables(w, &cap))
        std::abort();
    for (uint32_t index = 0; index < 2; ++index) {
        Player& player = w->game.players[index];
        player.in_use = 1;
        player.status = OA_PLAYER_STATUS_LOCAL;
        player.index = static_cast<uint8_t>(index);
    }
    WeaponDef& gun = w->game.weapon_defs[gun_def - 1];
    gun.flags = OA_WEAPON_FLAG_LINE_OF_SIGHT;
    gun.range = 100;
    WeaponDef& dgun = w->game.weapon_defs[dgun_def - 1];
    dgun.flags = OA_WEAPON_FLAG_LINE_OF_SIGHT | OA_WEAPON_FLAG_COMMAND_FIRE;
    dgun.range = 100;
    for (uint32_t type = 1; type <= 2; ++type) {
        UnitDef& def = w->unit_defs[type];
        def.flags = OA_UNIT_DEF_FLAG_HAS_WEAPONS;
        def.model_height = fx(10);
    }
    auto place = [&](uint32_t slot, uint8_t owner, uint16_t type, int32_t x) {
        Unit& unit = w->units[slot];
        unit.owner_index = owner;
        unit.owner = owner + 1u;
        unit.def = type + 1u;
        unit.type_index = type;
        unit.id = static_cast<uint16_t>(slot);
        unit.position = {fx(x), 0, 0};
        unit.flags = OA_UNIT_FLAG_LIVE;
        unit.last_attacker_owner = 10;
    };
    place(victim_slot, 0, 1, 0);
    place(attacker_slot, 1, 2, 50);
    place(bystander_slot, 1, 1, 30);
    Unit& victim = w->units[victim_slot];
    victim.last_attacker_owner = 0; // a fresh unit's 10 already counts as foreign
    victim.flags |= 1u << OA_UNIT_FLAG_FIRE_ORDER_SHIFT;
    victim.weapons[0].def = gun_def;
    victim.weapons[0].flags = OA_UNIT_WEAPON_ENABLED | OA_UNIT_WEAPON_RETALIATE;
    victim.weapons[0].target_b = OA_UNIT_TARGET_IS_UNIT;
    victim.events = 0xffff;
    w->game.tick = 1000;
    return w;
}

void slot_helpers() {
    World* w = make_world();
    Unit& victim = w->units[victim_slot];
    Unit& attacker = w->units[attacker_slot];
    CHECK(slot_target_unit(*w, victim, 0) == nullptr);
    aim_slot_at_unit(victim, attacker, 0);
    CHECK(victim.weapons[0].target_a == 2 && victim.weapons[0].target_b == OA_UNIT_TARGET_IS_UNIT);
    CHECK(victim.events == 0x83ff);
    CHECK(slot_target_unit(*w, victim, 0) == &attacker);
    victim.weapons[0].target_b = 40; // a ground target keeps its X in target_a
    CHECK(slot_target_unit(*w, victim, 0) == nullptr);
    CHECK(slot_target_unit(*w, victim, 3) == nullptr);

    CHECK(slot_reaches_unit(*w, victim, attacker, 0));
    attacker.position.x = fx(101);
    CHECK(!slot_reaches_unit(*w, victim, attacker, 0));
    CHECK(!slot_reaches_unit(*w, victim, attacker, 1));
    world_destroy(w);
}

// Aiming at a point keeps its integral X/Z and moves a Z of -0x8000 off the
// unit-target marker; the reach test checks range, then the shooter's model top
// against the sea, and never the point's height.
void ground_point_helpers() {
    World* w = make_world();
    Unit& victim = w->units[victim_slot];
    aim_slot_at_point(victim, {fx(40) + 0x8000, fx(7), fx(-25) + 1}, 0);
    CHECK(victim.weapons[0].target_a == 40 && victim.weapons[0].target_b == -25);
    CHECK(victim.events == 0x83ff);
    aim_slot_at_point(victim, {fx(1), 0, fx(-0x8000)}, 0);
    CHECK(victim.weapons[0].target_a == 1 && victim.weapons[0].target_b == -0x7fff);
    aim_slot_at_point(victim, {fx(2), 0, fx(3)}, 3);
    CHECK(victim.weapons[0].target_a == 1);

    CHECK(slot_reaches_point(*w, victim, {fx(100), fx(500), 0}, 0));
    CHECK(slot_reaches_point(*w, victim, {fx(60), 0, fx(80)}, 0));
    CHECK(!slot_reaches_point(*w, victim, {fx(101), 0, 0}, 0));
    CHECK(!slot_reaches_point(*w, victim, {fx(10), 0, 0}, 1));
    w->game.sea_level = 10; // the model top (10) is not above the sea
    CHECK(!slot_reaches_point(*w, victim, {fx(10), 0, 0}, 0));
    world_destroy(w);
}

void chase_and_retarget() {
    World* w = make_world();
    Unit& victim = w->units[victim_slot];
    Unit& attacker = w->units[attacker_slot];
    Recorder r;
    const auto hooks = hooks_for(r);

    Unit typeless = attacker;
    typeless.type_index = 0;
    auto result = retaliate(*w, victim, &typeless, hooks);
    CHECK(!result.chased && result.retargeted_slots == 0 && r.attack_requests == 0);
    CHECK(!result.attack_notice && !result.computer_alert);
    result = retaliate(*w, victim, nullptr, hooks);
    CHECK(!result.chased && result.retargeted_slots == 0);

    r.accept_attack = true;
    result = retaliate(*w, victim, &attacker, hooks);
    CHECK(result.chased && r.attack_requests == 1 && result.retargeted_slots == 0);
    CHECK(victim.weapons[0].target_a == 0);

    r.accept_attack = false;
    result = retaliate(*w, victim, &attacker, hooks);
    CHECK(!result.chased && r.attack_requests == 2 && result.retargeted_slots == 1);
    CHECK(victim.weapons[0].target_a == 2 && victim.weapons[0].target_b == OA_UNIT_TARGET_IS_UNIT);
    CHECK(victim.events == 0x83ff);

    // A head order that does not yield skips the attack order but still re-aims.
    victim.weapons[0].target_a = 0;
    r.has_head = true;
    r.head = 0;
    result = retaliate(*w, victim, &attacker, hooks);
    CHECK(r.attack_requests == 2 && result.retargeted_slots == 1);
    r.head = order_standby_flag;
    victim.weapons[0].target_a = 0;
    (void)retaliate(*w, victim, &attacker, hooks);
    CHECK(r.attack_requests == 3);
    r.has_head = false;
    r.head = 0;

    // nochasecategory blocks the order; the bad-target category blocks it too.
    UnitDef& def = w->unit_defs[1];
    def.no_chase_category = excluded_category;
    victim.weapons[0].target_a = 0;
    result = retaliate(*w, victim, &attacker, hooks);
    CHECK(r.attack_requests == 3 && result.retargeted_slots == 1);
    def.no_chase_category = 0;
    def.primary_bad_target_category = excluded_category;
    victim.weapons[0].target_a = 0;
    result = retaliate(*w, victim, &attacker, hooks);
    CHECK(r.attack_requests == 3 && result.retargeted_slots == 1);
    def.primary_bad_target_category = 0;

    // Out of range of slot 0: neither the order nor the re-aim.
    attacker.position.x = fx(150);
    victim.weapons[0].target_a = 0;
    result = retaliate(*w, victim, &attacker, hooks);
    CHECK(
        r.attack_requests == 3 && result.retargeted_slots == 0 && victim.weapons[0].target_a == 0
    );
    attacker.position.x = fx(50);
    world_destroy(w);
}

void slot_rules() {
    World* w = make_world();
    Unit& victim = w->units[victim_slot];
    Unit& attacker = w->units[attacker_slot];
    Unit& bystander = w->units[bystander_slot];
    Recorder r;
    const auto hooks = hooks_for(r);

    // Hold fire: no re-aim.
    victim.flags &= ~OA_UNIT_FLAG_FIRE_ORDER_MASK;
    CHECK(retaliate(*w, victim, &attacker, hooks).retargeted_slots == 0);
    victim.flags |= 2u << OA_UNIT_FLAG_FIRE_ORDER_SHIFT;

    // A reachable, acceptable current target is kept.
    aim_slot_at_unit(victim, bystander, 0);
    CHECK(retaliate(*w, victim, &attacker, hooks).retargeted_slots == 0);
    CHECK(victim.weapons[0].target_a == 3);
    bystander.position.x = fx(200);
    CHECK(retaliate(*w, victim, &attacker, hooks).retargeted_slots == 1);
    CHECK(victim.weapons[0].target_a == 2);
    bystander.position.x = fx(30);
    bystander.type_index = 2;
    w->unit_defs[1].primary_bad_target_category = excluded_category;
    aim_slot_at_unit(victim, bystander, 0);
    // The attacker (type 2) is a bad target for the order, but the slot loop
    // only checks the category of the current target.
    CHECK(retaliate(*w, victim, &attacker, hooks).retargeted_slots == 1);
    w->unit_defs[1].primary_bad_target_category = 0;

    // Disabled, untracked and commandfire slots stay.
    victim.weapons[0].target_a = 0;
    victim.weapons[0].flags = OA_UNIT_WEAPON_RETALIATE;
    CHECK(retaliate(*w, victim, &attacker, hooks).retargeted_slots == 0);
    victim.weapons[0].flags = OA_UNIT_WEAPON_ENABLED;
    CHECK(retaliate(*w, victim, &attacker, hooks).retargeted_slots == 0);
    victim.weapons[0].flags = OA_UNIT_WEAPON_ENABLED | OA_UNIT_WEAPON_RETALIATE;
    victim.weapons[0].def = dgun_def;
    CHECK(retaliate(*w, victim, &attacker, hooks).retargeted_slots == 0);
    victim.weapons[0].def = gun_def;

    // Three slots each decide on their own.
    victim.weapons[2] = victim.weapons[0];
    victim.weapons[2].target_a = 0;
    victim.weapons[0].target_a = 0;
    CHECK(retaliate(*w, victim, &attacker, hooks).retargeted_slots == 5);

    // Allies, unfinished units, mirrored owners and unarmed types do not react.
    victim.weapons[0].target_a = 0;
    victim.weapons[2].target_a = 0;
    w->game.players[0].alliance[1] = 1;
    CHECK(retaliate(*w, victim, &attacker, hooks).retargeted_slots == 0);
    w->game.players[0].alliance[1] = 0;
    victim.build_remaining = 0.5F;
    CHECK(retaliate(*w, victim, &attacker, hooks).retargeted_slots == 0);
    victim.build_remaining = 0.0F;
    w->game.players[0].status = OA_PLAYER_STATUS_MIRRORED;
    CHECK(retaliate(*w, victim, &attacker, hooks).retargeted_slots == 0);
    w->game.players[0].status = OA_PLAYER_STATUS_LOCAL;
    w->unit_defs[1].flags = 0;
    CHECK(retaliate(*w, victim, &attacker, hooks).retargeted_slots == 0);
    w->unit_defs[1].flags = OA_UNIT_DEF_FLAG_KAMIKAZE;
    CHECK(retaliate(*w, victim, &attacker, hooks).retargeted_slots == 5);
    world_destroy(w);
}

void alerts_and_notice() {
    World* w = make_world();
    Unit& victim = w->units[victim_slot];
    Unit& attacker = w->units[attacker_slot];
    Recorder r;
    const auto hooks = hooks_for(r);

    auto result = retaliate(*w, victim, &attacker, hooks);
    CHECK(!result.computer_alert && r.random_draws == 0);
    w->unit_defs[1].abilities = OA_UNIT_DEF_ABILITY_CAN_CAPTURE;
    result = retaliate(*w, victim, &attacker, hooks);
    CHECK(!result.computer_alert);
    w->game.players[0].status = OA_PLAYER_STATUS_COMPUTER;
    victim.weapons[0].target_a = 0;
    result = retaliate(*w, victim, &attacker, hooks);
    CHECK(result.computer_alert && result.computer_alert_tick == 1037 && r.random_draws == 1);
    CHECK(r.alerts == 1 && r.alert_tick == 1037);
    CHECK(result.retargeted_slots == 1);

    // The alert clears the orders before the head order is read, so a busy
    // head no longer blocks the attack order.
    r.has_head = true;
    r.head = 0;
    r.alert_clears_orders = true;
    r.accept_attack = true;
    const int requests = r.attack_requests;
    result = retaliate(*w, victim, &attacker, hooks);
    CHECK(r.alerts == 2 && result.chased && r.attack_requests == requests + 1);
    r.alert_clears_orders = false;
    r.accept_attack = false;
    w->game.players[0].status = OA_PLAYER_STATUS_LOCAL;
    w->unit_defs[1].abilities = 0;

    // The notice needs a foreign last attacker or weapon damage, and no muting order.
    CHECK(!result.attack_notice);
    victim.last_attacker_owner = 1;
    CHECK(retaliate(*w, victim, &attacker, hooks).attack_notice);
    r.has_head = true;
    r.head = order_mutes_attack_notice_flag;
    CHECK(!retaliate(*w, victim, &attacker, hooks).attack_notice);
    r.has_head = false;
    r.head = 0;
    // An accepted attack order carrying the muting bit becomes the head before
    // the notice reads it.
    r.accept_attack = true;
    r.chase_head = 0x280;
    victim.weapons[0].target_a = 0;
    result = retaliate(*w, victim, &attacker, hooks);
    CHECK(result.chased && !result.attack_notice);
    r.accept_attack = false;
    r.chase_head = 0;
    r.has_head = false;
    r.head = 0;
    victim.last_attacker_owner = 0;
    CHECK(!retaliate(*w, victim, &attacker, hooks).attack_notice);
    victim.damage_kind = 1;
    CHECK(retaliate(*w, victim, &attacker, hooks).attack_notice);
    CHECK(retaliate(*w, victim, nullptr, hooks).attack_notice);
    world_destroy(w);
}
} // namespace

int main() {
    slot_helpers();
    ground_point_helpers();
    chase_and_retarget();
    slot_rules();
    alerts_and_notice();
    if (failures != 0) {
        std::fprintf(stderr, "%d failures\n", failures);
        return 1;
    }
    std::puts("retaliation tests passed");
    return 0;
}
