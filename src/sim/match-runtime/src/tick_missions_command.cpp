// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The command block of the mission table: orders a unit carries out where it
// stands.
#include "ground_missions.hpp"

namespace oa::sim::match_runtime {

namespace {
namespace command {

constexpr uint32_t be_carried_wait = 10;
constexpr uint32_t queued_move_wait = 0x3c;
constexpr uint32_t get_built_first_wait = 300;
constexpr uint32_t get_built_wait = 0x1e;
constexpr uint32_t get_built_decay_ticks = 0xb;
constexpr uint32_t self_destruct_step_wait = 0x1e;
constexpr uint32_t self_destruct_last_jitter = 0xf;
constexpr uint32_t repair_decloak_hold = 0x96;
constexpr uint32_t factory_blocked_wait = 0xf;
constexpr uint32_t factory_full_wait = 300;
constexpr uint32_t spoken_counts = 6; // count0..count5
constexpr uint8_t stance_mask = 3;
constexpr uint32_t countdown_mask = 0x0fffffffu;
constexpr uint8_t activation_state = 1; // set_activation mask: Activate/Deactivate
constexpr uint8_t factory_building = 8; // set_activation mask: the yard is building
constexpr uint8_t factory_states = activation_state | factory_building;
// Unit.flags bits a factory hands on to what it builds.
constexpr uint32_t inherited_orders = OA_UNIT_FLAG_MOVE_ORDER_MASK | OA_UNIT_FLAG_FIRE_ORDER_MASK;

} // namespace command
} // namespace

uint32_t TickHost::GroundMissions::stop() {
    announce();
    for (uint32_t slot = 0; slot < OA_UNIT_WEAPON_COUNT; ++slot)
        host.match.stop_weapon(s, slot);
    if ((s.record.flags & OA_UNIT_FLAG_OCCUPANCY_MASK) == ground::occupancy_air && can_fly()) {
        const auto here = ground::position_of(s.record);
        push_order_front(
            create_order(sim::ground_orders::vtol_land_if_can_kind, nullptr, &here, 0, 0, 0)
        );
    }
    return ground::mission_done;
}

uint32_t TickHost::GroundMissions::make_selectable() {
    s.record.flags = (s.record.flags & ~OA_UNIT_FLAG_NOT_SELECTABLE) | OA_UNIT_FLAG_SELECTABLE;
    return ground::mission_done;
}

uint32_t TickHost::GroundMissions::wait_for_attack() {
    if (!target())
        return ground::mission_done;
    if (order.phase == 0) {
        order.wait_events = ground::wait_for_attack_events;
        return ground::next_phase;
    }
    return order.phase == 1 ? ground::mission_done : ground::mission_invalid;
}

uint32_t TickHost::GroundMissions::self_destruct() {
    auto& remaining = record.attack.retry;
    auto& last_count = record.extra.tolerance;
    const auto countdown = (def().abilities & OA_UNIT_DEF_ABILITY_SELF_DESTRUCT_MASK) >>
                           OA_UNIT_DEF_ABILITY_SELF_DESTRUCT_SHIFT;
    if ((static_cast<uint32_t>(remaining) & self_destruct_countdown_marker) == 0)
        remaining = static_cast<int32_t>(countdown | self_destruct_countdown_marker);
    if (last_count != 0 || countdown == 0) {
        host.scaled_damage(
            &s, s, self_destruct_damage, static_cast<uint8_t>(DeathKind::self_destruct)
        );
        return ground::mission_done;
    }
    if (events & ground::cancel_event) {
        if (!(s.record.flags & OA_UNIT_FLAG_DEATH_PENDING))
            speak(ground::speech_destruct_cancelled);
        return ground::mission_done;
    }
    const auto step = static_cast<uint32_t>(remaining) & command::countdown_mask;
    if (step == 0)
        last_count = 1;
    else
        remaining = static_cast<int32_t>((step - 1) | self_destruct_countdown_marker);
    // Only the six spoken counts, zero to five, are announced; the higher
    // steps of a longer countdown stay silent.
    if (step < command::spoken_counts)
        speak(ground::speech_countdown_zero - step);
    wait_ticks(
        step == 0 ? random(command::self_destruct_last_jitter) : command::self_destruct_step_wait
    );
    order.wait_events |= ground::cancel_event;
    return ground::next_phase;
}

uint32_t TickHost::GroundMissions::attack_no_move() {
    auto* aimed = target();
    if (!aimed || (events & ground::attack_abort_events))
        return ground::mission_done;
    AttackAdapter weapons(host, s, record);
    switch (order.phase) {
    case 0:
        announce();
        return ground::next_phase;
    case 1:
        weapons.enable_weapon(0);
        weapons.assign_target(*aimed, 0);
        order.wait_events = ground::attack_no_move_wait;
        return ground::next_phase;
    case 2:
        weapons.reset_weapons();
        return ground::retry_mission;
    default:
        return ground::mission_invalid;
    }
}

uint32_t TickHost::GroundMissions::self_repair() {
    auto* pad = target();
    if (!pad) {
        speak(ground::speech_failed);
        return ground::mission_failed;
    }
    switch (order.phase) {
    case 0:
        if (!(def_of(pad->record).flags & OA_UNIT_DEF_FLAG_BUILDER))
            return ground::mission_invalid;
        if (pad->record.build_remaining != 0.0F || !(s.record.state_flags & OA_UNIT_STATE_ACTIVE))
            return ground::mission_failed;
        AttackAdapter(host, s, record).enable_weapon(3);
        return ground::next_phase;
    case 1:
        if (static_cast<uint32_t>(static_cast<int32_t>(s.record.health)) >= def().max_damage)
            return ground::next_phase;
        s.record.decloak_until_tick = tick() + command::repair_decloak_hold;
        (void)host.nano_repair(host.slot(*pad), *s.unit);
        wait_ticks(1);
        order.wait_events |= ground::repair_step_event;
        return ground::keep_waiting;
    case 2:
        speak(ground::speech_repaired);
        return ground::mission_done;
    default:
        return ground::mission_invalid;
    }
}

uint32_t TickHost::GroundMissions::building_build() {
    auto& count = record.construction.remaining;
    ConstructionAdapter yard(host, s, record);
    if (events & ground::cancel_event) {
        if (auto* frame = target()) {
            const auto& frame_def = def_of(frame->record);
            const auto unbuilt = ground::truncate_word(
                (1.0 - static_cast<double>(frame->record.build_remaining)) *
                static_cast<double>(frame_def.build_cost_metal)
            );
            host.match.credit_metal(s.record, static_cast<float>(static_cast<uint32_t>(unbuilt)));
            yard.link_built(*frame);
            host.scaled_damage(
                &s, host.slot(*frame), lethal_damage, static_cast<uint8_t>(DeathKind::cancelled)
            );
        }
        host.match.set_activation(s, command::factory_states, false);
        refresh_panel();
        return ground::mission_done;
    }
    if (events & ground::target_lost_event) {
        speak(ground::speech_failed);
        --count;
        refresh_panel();
        return ground::restart_mission;
    }
    switch (order.phase) {
    case 0:
        set_target(nullptr);
        if (!(s.record.flags & OA_UNIT_FLAG_BUILDING))
            return ground::mission_invalid;
        if (count < 1) {
            host.match.set_activation(s, command::activation_state, false);
            return ground::mission_done;
        }
        yard.set_activation_mask(command::activation_state, true);
        return ground::next_phase;
    case 1:
        return wait_for_build_stance(ground::cancel_event);
    case 2: {
        record.construction.pad_piece = -1;
        (void)yard.query_build_pad(record.extra.destination);
        if (!yard.site_clear()) {
            wait_ticks(command::factory_blocked_wait);
            order.wait_events |= ground::cancel_event;
            return ground::keep_waiting;
        }
        auto* frame = yard.spawn_nanoframe();
        set_target(frame);
        if (!frame) {
            speak(ground::speech_failed);
            wait_ticks(command::factory_full_wait);
            order.wait_events |= ground::cancel_event;
            return ground::keep_waiting;
        }
        speak(ground::speech_build);
        yard.attach_child(*frame, record.construction.pad_piece, 1);
        frame->record.flags = ((frame->record.flags ^ s.record.flags) & command::inherited_orders) ^
                              frame->record.flags;
        yard.issue_get_built(*frame);
        host.match.set_activation(s, command::factory_building, true);
        refresh_panel();
        return ground::next_phase;
    }
    case 3: {
        auto* frame = target();
        if (!frame)
            return ground::mission_invalid;
        if (yard.build_progress(*frame, work_rate(def())))
            host.match.spray_nano(s, *frame, Match::NanoSpray::repair);
        if (frame->record.build_remaining == 0.0F)
            return ground::next_phase;
        wait_ticks(1);
        order.wait_events |= ground::build_abort_events;
        return ground::keep_waiting;
    }
    case 4:
        // The frame's completion already ran the builder link when its last
        // step finished it.
        speak(ground::speech_complete);
        host.match.set_activation(s, command::factory_building, false);
        set_target(nullptr);
        --count;
        refresh_panel();
        return ground::restart_mission;
    default:
        return ground::mission_invalid;
    }
}

uint32_t TickHost::GroundMissions::get_built() {
    if (s.record.build_remaining != 0.0F) {
        switch (order.phase) {
        case 0:
            AttackAdapter(host, s, record).enable_weapon(3);
            wait_ticks(command::get_built_first_wait);
            order.wait_events |= ground::get_built_wake;
            return ground::next_phase;
        case 1:
            wait_ticks(command::get_built_wait);
            order.wait_events |= ground::get_built_wake;
            return ground::next_phase;
        case 2:
            if (events & ground::get_built_wake)
                wait_ticks(command::get_built_wait);
            else if (events & ground::timer_event) {
                wait_ticks(command::get_built_decay_ticks);
                ConstructionAdapter self(host, s, record);
                (void)self.build_progress(
                    *s.unit, self.build_decay_rate(command::get_built_decay_ticks)
                );
            }
            order.wait_events |= ground::get_built_wake;
            return ground::keep_waiting;
        default:
            return ground::mission_invalid;
        }
    }
    refresh_selection();
    if (!has_movement_object())
        return ground::mission_done;
    bool queued = false;
    if (auto* factory = target()) {
        for_each_primary_uncycled(factory->primary, [&](sim::simulation_state::Order* inherited) {
            uint8_t kind = 0;
            if (inherited->kind == qmove_kind)
                kind = move_command_kind();
            else if (inherited->kind == qpatrol_kind)
                kind = patrol_command_kind();
            else
                return;
            if (!kind)
                return;
            const auto point = host.owned(*inherited).extra.destination;
            queue_order(kind, &point);
            queued = true;
        });
        if (sim::simulation_state::unit_active(s.record) &&
            sim::simulation_state::unit_active(factory->record)) {
            s.record.flags =
                ((factory->record.flags ^ s.record.flags) & command::inherited_orders) ^
                s.record.flags;
            const auto* owner = oa::world_unit_owner(&world(), &s.record);
            if (owner && owner->in_use && owner->status == OA_PLAYER_STATUS_LOCAL)
                host.match.assign_squad(s, static_cast<uint32_t>(factory->record.squad));
        }
        if (queued)
            return ground::mission_done;
    }
    queue_order(ground::park_kind, nullptr);
    return ground::mission_done;
}

uint32_t TickHost::GroundMissions::be_carried() {
    if (!s.record.attach_parent)
        return ground::mission_done;
    if (order.phase == 0) {
        AttackAdapter(host, s, record).enable_weapon(3);
        return ground::next_phase;
    }
    if (order.phase == 1) {
        wait_ticks(command::be_carried_wait);
        return ground::keep_waiting;
    }
    return ground::mission_invalid;
}

uint32_t TickHost::GroundMissions::activate() {
    if (def().abilities & OA_UNIT_DEF_ABILITY_ON_OFFABLE)
        host.match.set_activation(s, command::activation_state, true);
    return ground::mission_done;
}

uint32_t TickHost::GroundMissions::deactivate() {
    if (def().abilities & OA_UNIT_DEF_ABILITY_ON_OFFABLE)
        host.match.set_activation(s, command::activation_state, false);
    return ground::mission_done;
}

uint32_t TickHost::GroundMissions::cloak_on() {
    if (def().abilities & OA_UNIT_DEF_ABILITY_CAN_CLOAK)
        s.record.flags |= OA_UNIT_FLAG_CLOAK_RUNNING;
    return ground::mission_done;
}

uint32_t TickHost::GroundMissions::cloak_off() {
    if (def().abilities & OA_UNIT_DEF_ABILITY_CAN_CLOAK)
        s.record.flags &= ~OA_UNIT_FLAG_CLOAK_RUNNING;
    return ground::mission_done;
}

uint32_t TickHost::GroundMissions::standing_move_order() {
    const auto stance = static_cast<uint32_t>(record.extra.tolerance) & command::stance_mask;
    s.record.flags = (stance << OA_UNIT_FLAG_MOVE_ORDER_SHIFT) |
                     (s.record.flags & ~OA_UNIT_FLAG_MOVE_ORDER_MASK);
    return ground::mission_done;
}

uint32_t TickHost::GroundMissions::standing_fire_order() {
    const auto stance = record.extra.tolerance;
    s.record.flags =
        ((static_cast<uint32_t>(stance) & command::stance_mask) << OA_UNIT_FLAG_FIRE_ORDER_SHIFT) |
        (s.record.flags & ~OA_UNIT_FLAG_FIRE_ORDER_MASK);
    if (stance == 0 || stance == 1)
        for (uint32_t slot = 0; slot < OA_UNIT_WEAPON_COUNT; ++slot)
            if (s.record.weapons[slot].flags & OA_UNIT_WEAPON_RETALIATE)
                host.match.stop_weapon(s, slot);
    return ground::mission_done;
}

uint32_t TickHost::GroundMissions::queued_move() {
    wait_ticks(command::queued_move_wait);
    return ground::rotate_mission;
}

} // namespace oa::sim::match_runtime
