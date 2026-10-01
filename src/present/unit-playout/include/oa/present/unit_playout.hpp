// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// Smooth playout of the units of players this machine does not simulate.
//
// Those units (mirrored units) move only when their owner's unit-state
// record is applied, and the records arrive in bursts: a mirrored army stands
// still for a few ticks and then jumps. The playout draws each such unit on
// the path the simulation actually gave it, a few of its owner's ticks
// behind the newest one applied, on a clock per owner that runs evenly and
// adapts its delay to how the owner's records arrive. It is presentation
// only: it reads the simulation's records after each tick and never writes
// them, and targeting, sight and hits keep using the simulated places.

#include "oa/core/world.h"

#include <array>
#include <cstddef>
#include <optional>
#include <stdint.h>
#include <vector>

namespace oa::present::unit_playout {

/// A whole tick in the 16-bit fractions of frame times, clocks and rates.
inline constexpr uint32_t whole_tick = 1u << 16;

/// The delay, in owner ticks, an owner's clock starts with behind its newest
/// record, and the least it aims to keep behind the freshest over the first
/// delay_window_ticks: 200 milliseconds, the interval at which 3.1c sends
/// unit states by default.
inline constexpr int32_t start_delay_ticks = 6;
/// The least an owner's clock aims to run behind the freshest record of the
/// window, in owner ticks.
inline constexpr int32_t min_delay_ticks = 2;
/// The furthest an owner's clock aims to run behind the newest record, in owner ticks.
inline constexpr int32_t max_delay_ticks = 12;
/// The owner ticks of places an owner's clock aims to keep in hand at the
/// stalest moment of the window, just before the longest wait for records
/// ended: when records arrive evenly, it runs that wait and this margin
/// behind the freshest.
inline constexpr int32_t delay_margin_ticks = 2;
/// How far back, in ticks, the arrivals of an owner's records are measured: three seconds.
inline constexpr uint32_t delay_window_ticks = 90;
/// The fewest ticks between the oldest and the newest arrival in the window
/// over which an owner's pace is measured; over fewer the owner counts as
/// running one owner tick a tick.
inline constexpr uint32_t pace_span_ticks = 30;
/// A clock further than this many owner ticks behind the newest record jumps to its delay.
inline constexpr int32_t snap_behind_ticks = 30;
/// An owner's newest record further than this many owner ticks before the
/// newest seen starts the owner's clock afresh, as a new match does; a
/// record less far back arrived late and moves no clock.
inline constexpr int32_t restart_behind_ticks = 30;
/// The slowest an owner's clock runs: three quarters of a tick each tick.
inline constexpr uint32_t min_rate = whole_tick * 3 / 4;
/// The fastest an owner's clock runs: one and a half ticks each tick.
inline constexpr uint32_t max_rate = whole_tick * 3 / 2;
/// The most an owner's rate changes from one tick to the next: a sixteenth.
inline constexpr uint32_t rate_step = whole_tick / 16;
/// The ticks over which the rate closes the gap between a clock and where it
/// aims to be: a clock 4 ticks behind runs a quarter of a tick a tick faster
/// than its owner's pace.
inline constexpr int32_t settle_ticks = 16;
/// A clock less than this many owner ticks behind the newest record runs at min_rate at once.
inline constexpr int32_t low_water_ticks = 1;
/// A unit whose move between two of its places is longer than its top speed
/// allows by more than this many steps of that speed jumps there instead of
/// moving.
inline constexpr int32_t jump_speed_steps = 2;
/// A unit whose move between two of its places is longer than its top speed
/// allows by more than this many pixels jumps there, however fast it is.
inline constexpr int32_t jump_pixels = 48;
/// The places of one unit the playout keeps; enough for snap_behind_ticks
/// of moves and stops.
inline constexpr std::size_t places_per_unit = 48;
/// The arrivals of one owner's records the playout keeps; more than the
/// delay window holds, at most one arrival each tick.
inline constexpr std::size_t arrivals_per_owner = 128;
/// The most ticks one observation runs a clock on, when ticks went unobserved.
inline constexpr uint32_t max_elapsed_ticks = 64;
/// The clock values kept of the last ticks observed, so that a frame can show
/// any moment of the last clock_history_ticks - 1 ticks: more than the five
/// ticks one frame's clock step runs at most.
inline constexpr std::size_t clock_history_ticks = 8;

/// The moment a frame shows: a tick of the match and a part of a tick after it.
///
/// A moment from clock_history_ticks - 1 ticks before the tick last observed
/// up to it shows the owners' clocks as they ran from one tick observed to
/// the next; a moment after it, up to a tick, shows them running on at their
/// rates. frame_time_between() places a frame drawn between two ticks.
struct FrameTime {
    uint32_t tick{};     ///< a tick of the match, as Game.tick counts them
    uint32_t fraction{}; ///< part of a tick after `tick`, 0 to whole_tick
};

/// Returns the moment of a frame drawn part of the way from one tick to a later one.
///
/// @param tick_before the tick the frame starts from
/// @param tick the later tick, which the frame shows at a whole fraction
/// @param fraction the part of the way, 0 to whole_tick
/// @return tick_before and the part of the ticks between; `tick` itself when
///     it is not after tick_before (the difference wraps at 32 bits) or is
///     clock_history_ticks or more on
[[nodiscard]] constexpr FrameTime
frame_time_between(uint32_t tick_before, uint32_t tick, uint32_t fraction) noexcept {
    const uint32_t ticks = tick - tick_before;
    if (ticks == 0 || ticks >= clock_history_ticks)
        return {tick, 0};
    const uint64_t part = uint64_t{ticks} * (fraction < whole_tick ? fraction : whole_tick);
    return {
        tick_before + static_cast<uint32_t>(part / whole_tick),
        static_cast<uint32_t>(part % whole_tick)
    };
}

/// Where and how a unit is drawn.
struct UnitPose {
    FixedVec3 position{}; ///< 16.16 map pixels
    oa_angle heading{};
    int16_t pitch{};
    int16_t bank{};
};

/// The state of one owner's playout clock, as the last observation left it.
struct OwnerPlayout {
    bool followed{};       ///< the owner's units are played out
    int32_t newest_tick{}; ///< the owner tick of the newest record applied (Player.last_sim_tick)
    int64_t clock{};       ///< the owner tick the clock shows, with 16 bits of fraction
    uint32_t rate{};       ///< owner ticks the clock runs each tick, with 16 bits of fraction
    uint32_t pace{}; ///< owner ticks the owner's records advance each tick, 16 bits of fraction
    /// Owner ticks the clock aims to run behind the freshest record, carried
    /// on at the owner's pace to the tick observed; 16 bits of fraction.
    int64_t target_delay{};
    int64_t delay{};  ///< owner ticks it runs behind it now, with 16 bits of fraction
    uint32_t jumps{}; ///< times the clock jumped to its delay since it started
};

/// What the playout reads beside the World.
struct Hooks {
    void* context{};
    /// Returns a count for a unit slot that changes each time a unit is made
    /// in it, which tells a unit made in a slot freed during the same tick
    /// from the unit before. Null tells them apart only by type and owner.
    uint32_t (*slot_generation)(void* context, uint32_t slot) noexcept {};
};

/// The playout of every mirrored unit of one match.
///
/// observe() reads the match after each of its ticks; unit_pose() then
/// tells where a frame around that tick draws a unit. Memory is bounded: a
/// fixed ring of places for each unit slot, allocated when the first mirrored
/// player is seen, and fixed rings of arrivals and clock values per player.
/// The same observations and frame times give the same poses on every
/// machine: the clocks and the places are integers.
class Playout {
  public:

    /// Forgets every clock and place, as for a new match, and frees the places' memory.
    void reset() noexcept;

    /// Records the places of the units of the players this machine does not
    /// simulate and runs their owners' clocks on to the match's tick.
    ///
    /// Call it after each tick of the match, once the tick's records are
    /// applied. Calling it again at the same tick records the units' changes,
    /// and records applied since, without running the clocks. A tick earlier
    /// than the last one observed, a different World or a different unit
    /// table starts afresh, as reset() does, and so does an owner's newest
    /// record further back than restart_behind_ticks for that owner. A player
    /// is followed while it is in use and neither local nor a computer player
    /// (the complement of locally simulated owners in
    /// oa::sim::simulation_state::locally_simulated). Reads the world only;
    /// when the places' memory cannot be had, no unit is played out.
    ///
    /// @param world the match's world after the tick
    /// @param hooks what it reads beside the world
    void observe(const oa::World& world, const Hooks& hooks = {}) noexcept;

    /// Tells where a frame draws a mirrored unit.
    ///
    /// The unit is drawn at its owner's clock on the path of its recorded
    /// places, interpolated between the two around the clock and never past
    /// the newest. It jumps, rather than moving, into a place it reached by
    /// a jump: as it was created, loaded or unloaded, or corrected further
    /// than its top speed allows. A unit that dies leaves its slot empty at
    /// once, and an empty slot gives nothing.
    ///
    /// @param slot the unit's slot in World.units
    /// @param time the moment the frame shows
    /// @return the pose; nothing for a slot the playout does not follow,
    ///         which the frame draws as the simulation has it
    [[nodiscard]] std::optional<UnitPose> unit_pose(uint32_t slot, FrameTime time) const noexcept;

    /// Tells where a player's clock stands.
    ///
    /// @param player the player's index, 0 to OA_PLAYER_COUNT - 1
    /// @return the clock; all zero for a player not followed
    [[nodiscard]] OwnerPlayout owner(uint32_t player) const noexcept;

    /// Tells the tick the last observation saw.
    ///
    /// @return Game.tick at the last observe(), 0 before the first
    [[nodiscard]] uint32_t observed_tick() const noexcept { return observed_tick_; }

  private:

    /// One place of a unit, at one of its owner's ticks.
    struct Place {
        int32_t owner_tick{};
        UnitPose pose{};
        uint8_t flags{}; ///< place_* bits
    };

    /// The recorded places of one unit slot.
    struct Track {
        std::array<Place, places_per_unit> places{};
        uint32_t first{};      ///< index in `places` of the oldest place
        uint32_t count{};      ///< places held
        uint32_t generation{}; ///< Hooks::slot_generation of the unit the places belong to
        uint16_t type_index{}; ///< Unit.type_index the places belong to
        uint8_t owner{};       ///< the owning player's index
        bool active{};         ///< the slot holds a followed unit
    };

    /// One arrival of an owner's records.
    struct Arrival {
        uint32_t tick{};      ///< the match's tick it was observed at
        int32_t owner_tick{}; ///< the owner tick of the newest record it brought
    };

    /// An owner's clock as an observation left it.
    struct ClockSample {
        uint32_t tick{}; ///< the match's tick observed
        int64_t clock{}; ///< owner ticks, 16 bits of fraction
    };

    /// The playout clock of one owner.
    struct Clock {
        bool started{};
        int32_t newest_tick{};     ///< Player.last_sim_tick at this observation
        int32_t tick_before{};     ///< newest_tick at the observation of an earlier tick
        int64_t clock{};           ///< owner ticks at this observation, 16 bits of fraction
        uint32_t rate{whole_tick}; ///< owner ticks each tick from this observation on
        uint32_t pace{whole_tick}; ///< owner ticks the records advance each tick, over the window
        int64_t target_delay{int64_t{start_delay_ticks} * whole_tick}; ///< 16 bits of fraction
        /// The freshest arrival's line: its owner tick less pace times the
        /// tick it arrived at, 16 bits of fraction.
        int64_t freshest{};
        uint32_t started_tick{}; ///< the match's tick the clock started at
        uint32_t jumps{};
        std::array<Arrival, arrivals_per_owner> arrivals{};
        uint32_t first_arrival{}; ///< index in `arrivals` of the oldest
        uint32_t arrival_count{};
        /// The clock at the last ticks observed, oldest first, ending with this observation.
        std::array<ClockSample, clock_history_ticks> history{};
        uint32_t first_sample{}; ///< index in `history` of the oldest
        uint32_t sample_count{};
    };

    /// Starts an owner's clock afresh at its newest record.
    ///
    /// @param[out] clock the owner's clock
    /// @param newest_tick Player.last_sim_tick
    /// @param tick the match's tick
    static void start_clock(Clock& clock, int32_t newest_tick, uint32_t tick) noexcept;

    /// Forgets an owner clock's earlier values: frames before this
    /// observation show the clock as it is now.
    ///
    /// @param[in,out] clock the owner's clock
    /// @param tick the match's tick
    static void restart_history(Clock& clock, uint32_t tick) noexcept;

    /// Runs an owner's clock on to this observation and takes its newest record.
    ///
    /// @param[in,out] clock the owner's clock
    /// @param newest_tick Player.last_sim_tick
    /// @param tick the match's tick
    /// @param elapsed ticks since the last observation; 0 at the same tick
    static void
    run_clock(Clock& clock, int32_t newest_tick, uint32_t tick, uint32_t elapsed) noexcept;

    /// Chooses the pace, the delay and the rate an owner's clock runs at
    /// until the next observation.
    ///
    /// @param[in,out] clock the owner's clock
    /// @param tick the match's tick
    static void choose_rate(Clock& clock, uint32_t tick) noexcept;

    /// Returns the owner tick a clock shows at a frame's moment.
    ///
    /// @param clock the owner's clock
    /// @param time the frame's moment
    /// @return owner ticks with 16 bits of fraction, never past the newest record
    [[nodiscard]] int64_t clock_at(const Clock& clock, FrameTime time) const noexcept;

    /// Records the places of one followed player's units.
    ///
    /// @param world the match's world
    /// @param hooks what it reads beside the world
    /// @param player the player's index
    /// @param restarted the player's clock started afresh at this observation
    void record_units(
        const oa::World& world, const Hooks& hooks, uint8_t player, bool restarted
    ) noexcept;

    /// Records one unit's place at its owner's newest tick.
    ///
    /// @param world the match's world
    /// @param unit the unit
    /// @param generation Hooks::slot_generation of its slot; 0 without the hook
    /// @param[in,out] track the unit slot's places
    /// @param clock the owner's clock
    /// @param player the owner's index
    /// @param restarted the owner's clock started afresh at this observation
    static void record_unit(
        const oa::World& world,
        const oa::Unit& unit,
        uint32_t generation,
        Track& track,
        const Clock& clock,
        uint8_t player,
        bool restarted
    ) noexcept;

    /// Stops following a player and forgets its units' places.
    ///
    /// @param player the player's index
    void forget_player(uint8_t player) noexcept;

    std::array<Clock, OA_PLAYER_COUNT> clocks_{};
    std::vector<Track> tracks_{};
    const oa::World* world_{}; ///< the World observed; compared, never read through
    uint32_t slot_count_{};    ///< World.unit_slot_count at the last observation
    uint32_t observed_tick_{};
    bool observed_{};
};

} // namespace oa::present::unit_playout
