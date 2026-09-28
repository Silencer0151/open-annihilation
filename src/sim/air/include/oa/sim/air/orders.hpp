// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// Aircraft mission handlers. The simulation's order tick calls a handler with
// the order's triggering events; the handler advances the order's phase and
// returns an AirStep telling the tick what to do next. Orders are not
// canonical records yet, so a handler works on an AirOrder snapshot the host
// fills from its order record and copies back afterwards. `events` and
// `goal` point into the live record because the air driver writes them
// between ticks.

#include "oa/sim/air/goal.hpp"
#include "oa/sim/air/host.hpp"
#include "oa/core/world.h"

#include <cstdint>

namespace oa::sim::air {

// Handler results, as the order tick interprets them.
enum class AirStep : uint32_t {
    restart = 0,      // phase 0 again
    next = 1,         // phase + 1
    stay = 2,         // keep phase, wait for events
    retry = 3,        // keep phase, wait 15 ticks
    phase_chosen = 4, // keep the phase the handler set itself, as stay
    done = 5,         // remove the order
    rotate = 6,       // move the order behind the others
    fail = 7,         // clear the queue
    finished = 8,     // remove the order (failure speech already given)
};

// Bits of AirOrder.wait_events and the events a handler receives.
inline constexpr uint32_t event_timer = 0x01;
inline constexpr uint32_t event_arrived = 0x20; // goal reached
inline constexpr uint32_t event_path_failed = 0x40;
inline constexpr uint32_t event_goal_replaced = 0x80;
inline constexpr uint32_t event_goal_mask = 0x3e0; // cleared when a goal is installed
inline constexpr uint32_t wait_for_goal = event_arrived | event_path_failed | event_goal_replaced;

// The order is being removed from its queue: a handler that waits on this
// event runs once more with it.
inline constexpr uint32_t event_order_destroyed = 0x02;
// The order's target unit went away and the order's link to it was cleared.
inline constexpr uint32_t event_target_lost = 0x08;
// Weapon event: the order tick re-checks every weapon slot before the
// handler runs; setting state flag 4 raises it.
inline constexpr uint32_t event_weapons = 0x10000; /* ? */

// Bits of AirOrder.flags.
inline constexpr uint32_t order_seek_if_lost = 0x200; // attack: re-seek when the target is gone
inline constexpr uint32_t order_announce = 0x2000;    // speak once when the order starts

// Speech categories (index into the owner speech table).
inline constexpr uint32_t speech_order = 5;
inline constexpr uint32_t speech_order_done = 6;
inline constexpr uint32_t speech_order_failed = 7;

// The order fields the air handlers read and write.
struct AirOrder {
    uint8_t kind{};
    uint8_t phase{};
    uint32_t wait_events{};
    uint32_t wake_tick{};
    Unit* unit{};
    Unit* target{}; // unit link
    FixedVec3 destination{};
    int16_t anchor_x{}; // integer world X
    int16_t anchor_z{}; // integer world Z
    int32_t parameter{};
    int32_t parameter_2{};
    int32_t parameter_3{};
    uint32_t flags{};   // order_* bits
    bool has_next{};    // another order follows
    uint32_t* events{}; // live raised event bits
    AirGoal* goal{};    // live goal storage; kind none when unset
};

/// Wakes the order a number of ticks from now.
///
/// @param[in,out] order the order; its timer event and wake tick are set
/// @param host tick source
/// @param ticks delay in game ticks
void air_order_wait(AirOrder* order, const AirHost& host, uint32_t ticks) noexcept;

/// Speaks the order's announcement once.
///
/// @param[in,out] order the order; its announce flag is cleared
/// @param host owner speech
/// @param text text overriding the order speech category's default, or null
void air_order_announce(AirOrder* order, const AirHost& host, const char* text) noexcept;

/// Replaces the order's goal and hands it to the unit's driver.
///
/// Installing a goal clears the order's pending goal events. A unit without a
/// movement object is left alone.
///
/// @param[in,out] order the order; its goal storage is overwritten
/// @param host driver lookup
/// @param goal goal to copy in, or null to remove it
void air_order_set_goal(AirOrder* order, const AirHost& host, const AirGoal* goal) noexcept;

/// Takes the unit off the ground.
///
/// Enables weapons, drops any carrier and activates the unit; when landed it
/// switches to the air layer and climbs to half cruise altitude over the current
/// position.
///
/// @param[in,out] order the order; the climb goal and its wait events are set
/// @param host weapon, carrier, state and layer services
/// @param extra_events events added to the arrival wait
void air_take_off(AirOrder* order, const AirHost& host, uint32_t extra_events) noexcept;

/// Points an aircraft that left the map back toward the middle.
///
/// The goal is 50 world units along the bearing to the map centre, reached
/// within 128.
///
/// @param[in,out] order the order; the goal and its wait events are set
/// @param host off-map test and driver lookup
/// @return false when the unit is on the map
[[nodiscard]] bool air_return_to_map(AirOrder* order, const AirHost& host) noexcept;

/// Sends the unit to a random active repair pad of its owner nearby.
///
/// Drops the order's goal and queues VTOL_Landing on the pad.
///
/// @param[in,out] order the order
/// @param host pad search, random stream and order queue
/// @return false when no pad is near
[[nodiscard]] bool air_seek_repair_pad(AirOrder* order, const AirHost& host) noexcept;

} // namespace oa::sim::air
