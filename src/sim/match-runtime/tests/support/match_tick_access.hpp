// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The tests' way into the per-tick work of a match that its public
// interface runs only as part of a whole tick: one order's mission step, a
// unit's death, a player's self-destruct, the end of a build and the world
// queries of the air goals. The match's tick host does the work, as the
// tick does; the tests reach it through this interface instead of the
// match's private headers (target oa-sim-match-runtime-tick-access).
#pragma once

#include "oa/sim/air/host.hpp"
#include "oa/sim/match_runtime.hpp"
#include "oa/sim/match_runtime/attachment_links.hpp"
#include "oa/sim/unit_movement/movement.hpp"

#include <cstdint>
#include <memory>

namespace oa::sim::match_runtime {

/// Runs single pieces of a match's tick, as the match's tick host runs them.
class MatchTickAccess {
  public:

    /// The order flags' bit StartBuilding sets and StopBuilding clears.
    static constexpr uint8_t building_flag = 0x40;

    /// Binds the access to a match.
    ///
    /// @param match The match; it outlives the access.
    explicit MatchTickAccess(Match& match);

    MatchTickAccess(const MatchTickAccess&) = delete;
    MatchTickAccess& operator=(const MatchTickAccess&) = delete;

    /// Releases the tick host.
    ~MatchTickAccess();

    /// Runs the handler the mission table holds for the order's kind, as the tick does.
    ///
    /// @param world Canonical world (unused; the match's own is used).
    /// @param record Unit the order belongs to.
    /// @param[in,out] order Order to step; its phase and wait fields change.
    /// @param events Events that woke the order; tearing the order down
    ///     passes event 2.
    /// @return The handler's step result (see the ground:: result codes).
    uint32_t dispatch_mission(
        oa::World& world, oa::Unit& record, sim::simulation_state::Order& order, uint32_t events
    );

    /// Returns the world queries the air goals and the air driver make, over the match.
    ///
    /// @return The queries; their context is the access's tick host, which
    ///     lives as long as the access.
    sim::air::AirHost air_host();

    /// Runs StopBuilding when the order started building, shares the start
    /// with the other players and clears the order's building bit.
    ///
    /// @param builder Builder.
    /// @param[in,out] order Its order.
    void stop_building(sim::unit_spawn::Slot& builder, sim::simulation_state::Order& order);

    /// Kills a unit as the tick kills one: its Killed script, its wreck, its orders and the
    /// commander rule.
    ///
    /// @param record Dying unit; one no longer live is skipped.
    /// @param kind DeathKind value of the death.
    void kill_unit(oa::Unit& record, uint8_t kind);

    /// Makes every live unit of a player that is not already dying self-destruct.
    ///
    /// @param owner Player index 0..9.
    void destroy_player_units(uint8_t owner);

    /// Returns the rate the tick keeps in a moving unit's OA_UNIT_FLAG_MOVE_RATE_MASK bits.
    ///
    /// @param movement The unit's movement object after the tick.
    /// @param unit The moving unit.
    /// @param def Its type.
    /// @return 0 for a carried unit, a movement object that holds its rate,
    ///     or no speed and no turn; otherwise 1, 2 past moverate1 and 3 past
    ///     moverate2.
    [[nodiscard]] static uint32_t movement_rate(
        const sim::unit_movement::Movement& movement, const oa::Unit& unit, const oa::UnitDef& def
    ) noexcept;

    /// Tells whether an order has started building (StartBuilding ran for it).
    ///
    /// @param order The order.
    /// @return Whether its building bit is set.
    [[nodiscard]] static bool order_building(const sim::simulation_state::Order& order) noexcept;

  private:

    std::unique_ptr<TickHost> host_;
};

} // namespace oa::sim::match_runtime
