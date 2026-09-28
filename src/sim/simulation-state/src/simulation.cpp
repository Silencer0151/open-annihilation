// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/simulation_state.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace oa::sim::simulation_state {
namespace {
constexpr uint32_t timer_event = 1, weapon_event = 0x10000;
constexpr uint8_t secondary_flag = 4, detached_flag = 1, retry_flag = 0x80;
constexpr uint32_t height_dirty = OA_UNIT_FLAG_POSITION_DIRTY,
                   can_hover_type = OA_UNIT_DEF_FLAG_CAN_HOVER;
constexpr uint8_t live_multiplayer_game = 1, periodic_enabled = 2;

void spend(std::size_t& budget) {
    if (budget == 0)
        throw std::runtime_error(
            "order execution budget exhausted; unresolved handler failed to yield"
        );
    --budget;
}

void wait(oa::World& w, Order& o, Host& h, uint32_t range) {
    const auto random = h.random_bounded(range);
    o.wait_events |= timer_event;
    o.wake_tick = w.game.tick + random + 30u;
}

const oa::UnitDef& type(oa::World& w, const oa::Unit& u) {
    const auto* def = oa::world_unit_def_of(&w, &u);
    if (!def)
        throw std::invalid_argument("unit has no type");
    return *def;
}

// UnitDef.water_line is read as an unsigned byte.
uint32_t waterline(const oa::UnitDef& t) noexcept {
    return static_cast<uint8_t>(t.water_line);
}

uint32_t position_word(oa::oa_fixed value) noexcept {
    return static_cast<uint32_t>(value);
}
} // namespace

uint8_t lowest_unused_player_mark(const oa::World& w) noexcept {
    for (int32_t mark = 1; mark <= 10; ++mark) {
        bool found = false;
        for (const auto& player : w.game.players)
            if (player_active(player) && static_cast<int32_t>(player.machine_group) == mark)
                found = true;
        if (!found)
            return static_cast<uint8_t>(mark);
    }
    return 0;
}

oa::Unit* nearest_candidate_unit(
    oa::World& w, std::span<const uint8_t> relation, int32_t x, int32_t z
) noexcept {
    oa::Unit* best = nullptr;
    auto best_metric = static_cast<int32_t>(0x7fffffff);
    for (uint8_t index = 0; index < OA_PLAYER_COUNT; ++index) {
        const auto& player = w.game.players[index];
        if (!player_slot_active(index, player) || relation[player.index] != 0)
            continue;
        uint32_t count = 0;
        auto* units = oa::world_player_units(&w, &player, &count);
        for (uint32_t i = 0; i < count; ++i) {
            auto& unit = units[i];
            const auto flags = unit.flags;
            if ((flags & OA_UNIT_FLAG_LIVE) == 0 || (flags & OA_UNIT_FLAG_OCCUPANCY_MASK) == 2 ||
                (flags & OA_UNIT_FLAG_NOT_SELECTABLE) != 0 ||
                (unit.state_flags & OA_UNIT_STATE_CLOAKED) != 0)
                continue;
            const auto dx =
                static_cast<int32_t>(static_cast<uint32_t>(x) - position_word(unit.position.x));
            const auto dz =
                static_cast<int32_t>(static_cast<uint32_t>(z) - position_word(unit.position.z));
            const auto x_high = static_cast<uint32_t>(
                static_cast<uint64_t>(static_cast<int64_t>(dx) * static_cast<int64_t>(dx)) >> 32
            );
            const auto z_high = static_cast<uint32_t>(
                static_cast<uint64_t>(static_cast<int64_t>(dz) * static_cast<int64_t>(dz)) >> 32
            );
            const auto metric = static_cast<int32_t>(x_high + z_high);
            if (metric < best_metric) {
                best = &unit;
                best_metric = metric;
            }
        }
    }
    return best;
}

bool locally_simulated(const oa::World& w, const oa::Unit& u) noexcept {
    const auto* owner = oa::world_unit_owner(&w, &u);
    return owner && owner->in_use && (owner->status == 1 || owner->status == 2);
}

bool unit_selectable(const oa::World& w, const oa::Unit& u) {
    const auto unfinished = u.build_remaining;
    // The float compare accepts unordered as well as equal.
    if (!(u.flags & OA_UNIT_FLAG_SELECTABLE) || (unfinished != 0 && !std::isnan(unfinished)) ||
        u.capture_cooldown)
        return false;
    const auto parent = oa::oa_unit_slot_from_ref(u.attach_parent);
    if (parent >= w.unit_slot_count)
        throw std::out_of_range("selection parent outside pool");
    return !parent || (w.units[parent].flags & OA_UNIT_FLAG_AIR_BASE) != 0;
}

void remove_order(OrderQueue& q, oa::Unit& u, Order& o, Host& h) {
    Order* original = q.primary;
    Order** link = (o.flags & secondary_flag) ? &q.secondary : &q.primary;
    std::size_t budget = default_order_budget;
    while (*link && *link != &o) {
        spend(budget);
        link = &(*link)->next;
    }
    if (!*link)
        return;
    *link = o.next;
    if (&o != original)
        o.flags |= detached_flag;
    h.destroy_order(u, o);
}

void rotate_primary(OrderQueue& q, Order& o) {
    Order** link = &q.primary;
    std::size_t budget = default_order_budget;
    while (*link != &o) {
        spend(budget);
        if (!*link)
            throw std::invalid_argument("rotated order is not in primary queue");
        link = &(*link)->next;
    }
    *link = o.next;
    while (*link) {
        spend(budget);
        link = &(*link)->next;
    }
    *link = &o;
    o.next = nullptr;
}

void clear_orders(OrderQueue& q, oa::Unit& u, bool all, Host& h) {
    Order* original = q.primary;
    Order** link = &q.primary;
    std::size_t budget = default_order_budget;
    while (*link) {
        spend(budget);
        Order& o = **link;
        if (!all && (o.preserve_flags & 4))
            link = &o.next;
        else {
            *link = o.next;
            if (&o != original)
                o.flags |= detached_flag;
            h.destroy_order(u, o);
        }
    }
    if (all)
        while (q.secondary) {
            spend(budget);
            remove_order(q, u, *q.secondary, h);
        }
}

void primary_orders(oa::World& w, OrderQueue& q, oa::Unit& u, Host& h, std::size_t budget) {
    for (;;) {
        spend(budget);
        Order* o = q.primary;
        if (!o) {
            if (locally_simulated(w, u) && type(w, u).default_mission_type != 0)
                h.queue_default_mission(w, u);
            return;
        }
        if (o->wake_tick <= w.game.tick) {
            o->wake_tick = 0xffffffffu;
            o->raised_events |= timer_event;
        }
        const auto events = (static_cast<uint32_t>(u.events) | o->raised_events) & o->wait_events;
        if (o->wait_events && !events)
            return;
        u.events = static_cast<uint16_t>(u.events & ~events);
        o->wait_events = 0;
        o->raised_events &= ~events;
        if (events & weapon_event)
            for (uint32_t slot = 0; slot < 3; ++slot)
                h.clear_weapon_target(u, slot);
        const auto result = h.dispatch_mission(w, u, *o, events);
        switch (result) {
        case 0:
            o->phase = 0;
            break;
        case 1:
            ++o->phase;
            break;
        case 2:
        case 4:
            break;
        case 3:
            wait(w, *o, h, 15);
            break;
        case 5:
        case 8:
            remove_order(q, u, *o, h);
            break;
        case 6:
            rotate_primary(q, *o);
            break;
        case 9:
            o->flags |= retry_flag;
            if (o->next)
                remove_order(q, u, *o, h);
            else {
                o->phase = 0;
                wait(w, *o, h, 30);
            }
            break;
        default:
            clear_orders(q, u, true, h);
            return;
        }
    }
}

void secondary_orders(oa::World& w, OrderQueue& q, oa::Unit& u, Host& h, std::size_t budget) {
    for (;;) {
        Order* o = q.secondary;
        while (o && o->wait_events && o->wake_tick > w.game.tick) {
            spend(budget);
            o = o->next;
        }
        if (!o)
            return;
        spend(budget);
        o->wait_events = 0;
        const auto result = h.dispatch_mission(w, u, *o, 0);
        switch (result) {
        case 0:
            o->phase = 0;
            break;
        case 1:
            ++o->phase;
            break;
        case 2:
        case 4:
            break;
        case 3:
            wait(w, *o, h, 15);
            break;
        case 6:
        case 7:
            remove_order(q, u, *o, h);
            return;
        default:
            remove_order(q, u, *o, h);
            break;
        }
    }
}

void update_height(oa::World& w, oa::Unit& u, Host& h) {
    const auto flags = u.flags;
    const auto& t = type(w, u);
    if (!(flags & height_dirty) && !(t.flags & can_hover_type))
        return;
    u.flags &= ~height_dirty;
    if (!u.movement || (flags & OA_UNIT_FLAG_OCCUPANCY_MASK) != 1)
        return;
    if (t.flags & OA_UNIT_DEF_FLAG_UPRIGHT) {
        int32_t height;
        if (!(t.flags & can_hover_type))
            height = h.terrain_height_under(u);
        else {
            height = static_cast<int32_t>(w.game.sea_level) - static_cast<int32_t>(waterline(t));
            if (height < h.terrain_height_under(u))
                height = h.terrain_height_under(u);
        }
        u.position.y = static_cast<oa::oa_fixed>(static_cast<uint32_t>(height) << 16);
    } else if (t.flags & OA_UNIT_DEF_FLAG_FLOATER) {
        u.position.y = static_cast<oa::oa_fixed>((waterline(t) * 0xffffu + w.game.sea_level) << 16);
    } else
        h.settle_on_ground(u);
}

void update_unit(oa::World& w, OrderQueue& q, oa::Unit& u, Host& h) {
    if (u.script)
        h.tick_script(u, 1);
    if (u.damage_countdown)
        --u.damage_countdown;
    if (u.capture_cooldown)
        --u.capture_cooldown;
    if ((u.flags & OA_UNIT_FLAG_SELECTED) && !unit_selectable(w, u))
        u.flags &= ~OA_UNIT_FLAG_SELECTED;
    if (w.game.tick % 30 == 0) {
        const auto maximum = type(w, u).max_damage;
        if (!maximum)
            throw std::domain_error("health percentage divisor is zero");
        // The division treats the sign-extended health*100 as an unsigned word.
        const auto quotient = static_cast<uint32_t>(static_cast<int32_t>(u.health) * 100) / maximum;
        const auto percent = std::clamp(std::bit_cast<int32_t>(quotient), 0, 100);
        u.previous_health_percent = u.health_percent;
        u.health_percent = static_cast<uint8_t>(percent);
    }
    if (locally_simulated(w, u)) {
        if (w.environment_enabled && w.environment_damage && w.game.tick % 30 == 0 &&
            std::bit_cast<int16_t>(static_cast<uint16_t>(position_word(u.position.y) >> 16)) <=
                w.game.sea_level &&
            !(type(w, u).flags & can_hover_type))
            h.apply_scaled_damage(u, w.environment_damage, 11);
        if (type(w, u).heal_time &&
            static_cast<uint32_t>(static_cast<int32_t>(u.health)) < type(w, u).max_damage &&
            (w.game.tick & 7) == 0)
            h.regenerate_health(u);
        primary_orders(w, q, u, h);
        secondary_orders(w, q, u, h);
        if (u.movement) {
            h.movement_tick(u);
            update_height(w, u, h);
        }
    }
    if (u.flags & OA_UNIT_FLAG_DEATH_PENDING)
        h.kill_unit(u, u.damage_kind);
}

void update_units(oa::World& w, std::span<OrderQueue> orders, Host& h) {
    if (orders.size() != w.unit_slot_count)
        throw std::invalid_argument("order queues do not cover the unit pool");
    w.game.active_unit_count = 0;
    for (auto& p : w.game.players) {
        if (!player_active(p))
            continue;
        uint32_t count = 0;
        auto* units = oa::world_player_units(&w, &p, &count);
        for (uint32_t i = 0; i < count; ++i) {
            auto& u = units[i];
            if (u.type_index) {
                ++w.game.active_unit_count;
                h.update_wind_generator(u);
                if (p.in_use && (p.status == 1 || p.status == 2))
                    h.tick_weapon_aim(u);
                update_unit(w, orders[oa::world_unit_slot(&w, &u)], u, h);
            }
        }
        if ((w.game.session_flags & live_multiplayer_game) && p.in_use &&
            (p.status == 1 || p.status == 2))
            h.local_player_ticked(p);
    }
    if ((w.game.periodic_flags & periodic_enabled) && !h.is_key_down(0xf9)) {
        w.game.periodic_countdown = std::bit_cast<int16_t>(
            static_cast<uint16_t>(static_cast<uint16_t>(w.game.periodic_countdown) - 1u)
        );
        if (w.game.periodic_countdown < 1) {
            w.game.periodic_countdown = 90;
            h.select_next_viewpoint_unit();
            h.follow_next_selected(0);
        }
    }
}
} // namespace oa::sim::simulation_state
