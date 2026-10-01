// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The playout of mirrored units (unit_playout.hpp).
#include "oa/present/unit_playout.hpp"

#include <algorithm>
#include <exception>
#include <limits>

namespace oa::present::unit_playout {
namespace {

/// A whole tick, as a signed count of fractions.
constexpr int64_t whole = whole_tick;

/// Bits below the binary point of a part of a tick.
constexpr uint32_t fraction_bits = 16;

/// The shortest way part_of() measures in coarser parts.
constexpr int64_t longest_exact_span = int64_t{1} << 30;

/// Bit of Place::flags: the unit jumped into this place; nothing is drawn between it and the one before.
constexpr uint8_t place_jump = 0x01;
/// Bit of Place::flags: the unit is carried (Unit.attach_parent is set).
constexpr uint8_t place_carried = 0x02;

/// Returns an owner tick as a clock value.
///
/// @param owner_tick an owner tick
/// @return the tick with 16 bits of fraction
constexpr int64_t clock_of(int64_t owner_tick) noexcept {
    return owner_tick * whole;
}

/// Returns the absolute difference of two 16.16 coordinates.
///
/// @param from one coordinate
/// @param to the other
/// @return |to - from|, without wrapping
int64_t distance(int32_t from, int32_t to) noexcept {
    const int64_t step = int64_t{to} - int64_t{from};
    return step < 0 ? -step : step;
}

/// Returns a value part of the way along a step, the part given as a fraction.
///
/// @param step the whole step, within 32 bits either way
/// @param done the part of the way gone, 0 to `span`
/// @param span the whole way, above 0
/// @return step * done / span, rounded toward zero; a span of 2^30 or more
///     is measured in coarser parts, so that the product stays within 64 bits
int64_t part_of(int64_t step, int64_t done, int64_t span) noexcept {
    while (span >= longest_exact_span) {
        span >>= 1;
        done >>= 1;
    }
    return step * done / span;
}

/// Returns a 16.16 coordinate part of the way to another.
///
/// @param from the coordinate at the earlier place
/// @param to the coordinate at the later place
/// @param done the part of the way gone, 0 to `span`
/// @param span the whole way, above 0
/// @return from + (to - from) * done / span, rounded toward `from`
int32_t blend_coordinate(int32_t from, int32_t to, int64_t done, int64_t span) noexcept {
    return static_cast<int32_t>(int64_t{from} + part_of(int64_t{to} - int64_t{from}, done, span));
}

/// Returns an angle part of the way to another, the short way round.
///
/// @param from the angle at the earlier place, 65536 to a turn
/// @param to the angle at the later place
/// @param done the part of the way gone, 0 to `span`
/// @param span the whole way, above 0
/// @return the angle between, wrapped to 16 bits
uint16_t blend_angle(uint16_t from, uint16_t to, int64_t done, int64_t span) noexcept {
    const auto turn = static_cast<int16_t>(static_cast<uint16_t>(to - from));
    return static_cast<uint16_t>(from + static_cast<uint16_t>(part_of(turn, done, span)));
}

/// Returns the pose part of the way from one place to the next.
///
/// @param from the pose at the earlier place
/// @param to the pose at the later place
/// @param done the part of the way gone, 0 to `span`
/// @param span the whole way, above 0
/// @return the pose between
UnitPose blend_pose(const UnitPose& from, const UnitPose& to, int64_t done, int64_t span) noexcept {
    UnitPose pose{};
    pose.position.x = blend_coordinate(from.position.x, to.position.x, done, span);
    pose.position.y = blend_coordinate(from.position.y, to.position.y, done, span);
    pose.position.z = blend_coordinate(from.position.z, to.position.z, done, span);
    pose.heading = blend_angle(from.heading, to.heading, done, span);
    pose.pitch = static_cast<int16_t>(
        blend_angle(static_cast<uint16_t>(from.pitch), static_cast<uint16_t>(to.pitch), done, span)
    );
    pose.bank = static_cast<int16_t>(
        blend_angle(static_cast<uint16_t>(from.bank), static_cast<uint16_t>(to.bank), done, span)
    );
    return pose;
}

/// Tells whether two poses are the same.
///
/// @param a a pose
/// @param b another
/// @return true when every coordinate and angle matches
bool same_pose(const UnitPose& a, const UnitPose& b) noexcept {
    return a.position.x == b.position.x && a.position.y == b.position.y &&
           a.position.z == b.position.z && a.heading == b.heading && a.pitch == b.pitch &&
           a.bank == b.bank;
}

/// Returns a unit's pose as the simulation has it.
///
/// @param unit the unit
/// @return its place and orientation
UnitPose pose_of(const oa::Unit& unit) noexcept {
    UnitPose pose{};
    pose.position = unit.position;
    pose.heading = unit.heading;
    pose.pitch = unit.pitch;
    pose.bank = unit.bank;
    return pose;
}

/// Tells whether the simulation runs a player's units on this machine.
///
/// @param player the player
/// @return true for a player in use whose units the playout does not follow:
///         a local or a computer player
bool simulated_here(const oa::Player& player) noexcept {
    return player.status == OA_PLAYER_STATUS_LOCAL || player.status == OA_PLAYER_STATUS_COMPUTER;
}

/// Tells whether the playout follows a player's units.
///
/// @param player the player
/// @return true for a player in use that this machine does not simulate
bool followed(const oa::Player& player) noexcept {
    return player.in_use != 0 && !simulated_here(player);
}

} // namespace

void Playout::reset() noexcept {
    clocks_ = {};
    tracks_ = std::vector<Track>{};
    world_ = nullptr;
    slot_count_ = 0;
    observed_tick_ = 0;
    observed_ = false;
}

void Playout::observe(const oa::World& world, const Hooks& hooks) noexcept {
    const uint32_t tick = world.game.tick;
    if (observed_ &&
        (world_ != &world || slot_count_ != world.unit_slot_count || tick < observed_tick_))
        reset();
    const uint32_t elapsed = observed_ ? std::min(tick - observed_tick_, max_elapsed_ticks) : 0;
    world_ = &world;
    slot_count_ = world.unit_slot_count;
    observed_tick_ = tick;
    observed_ = true;
    for (uint8_t index = 0; index < OA_PLAYER_COUNT; ++index) {
        const oa::Player& player = world.game.players[index];
        Clock& clock = clocks_[index];
        if (!followed(player)) {
            if (clock.started)
                forget_player(index);
            continue;
        }
        if (tracks_.size() != world.unit_slot_count) {
            // Without the memory for the places no unit is played out; the
            // next observation tries again.
            try {
                tracks_.assign(world.unit_slot_count, Track{});
            } catch (const std::exception&) {
                tracks_ = std::vector<Track>{};
            }
        }
        const bool restarted =
            !clock.started ||
            int64_t{player.last_sim_tick} + restart_behind_ticks < int64_t{clock.newest_tick};
        if (restarted)
            start_clock(clock, player.last_sim_tick, tick);
        else
            run_clock(clock, player.last_sim_tick, tick, elapsed);
        if (tracks_.size() == world.unit_slot_count)
            record_units(world, hooks, index, restarted);
    }
}

void Playout::start_clock(Clock& clock, int32_t newest_tick, uint32_t tick) noexcept {
    clock = Clock{};
    clock.started = true;
    clock.newest_tick = newest_tick;
    clock.tick_before = newest_tick;
    clock.clock = clock_of(int64_t{newest_tick} - start_delay_ticks);
    clock.freshest = clock_of(newest_tick) - clock.pace * int64_t{tick};
    clock.started_tick = tick;
    restart_history(clock, tick);
}

void Playout::restart_history(Clock& clock, uint32_t tick) noexcept {
    clock.first_sample = 0;
    clock.sample_count = 1;
    clock.history[0] = {tick, clock.clock};
}

void Playout::run_clock(
    Clock& clock, int32_t newest_tick, uint32_t tick, uint32_t elapsed
) noexcept {
    if (elapsed != 0) {
        clock.tick_before = clock.newest_tick;
        // The clock runs on as the frames since the last observation showed
        // it: at its rate, and no further than the newest record they had.
        clock.clock =
            std::min(clock.clock + int64_t{clock.rate} * elapsed, clock_of(clock.newest_tick));
    }
    if (newest_tick > clock.newest_tick) {
        clock.newest_tick = newest_tick;
        const uint32_t last = (clock.first_arrival + clock.arrival_count + arrivals_per_owner - 1) %
                              arrivals_per_owner;
        if (clock.arrival_count != 0 && clock.arrivals[last].tick == tick)
            clock.arrivals[last].owner_tick = newest_tick;
        else if (clock.arrival_count < arrivals_per_owner) {
            clock.arrivals[(clock.first_arrival + clock.arrival_count) % arrivals_per_owner] = {
                tick, newest_tick
            };
            ++clock.arrival_count;
        } else {
            clock.arrivals[clock.first_arrival] = {tick, newest_tick};
            clock.first_arrival = (clock.first_arrival + 1) % arrivals_per_owner;
        }
    }
    // A second observation of the same tick runs no clock: frames already
    // drawn up to it keep their clock.
    if (elapsed == 0)
        return;
    const uint32_t jumps = clock.jumps;
    choose_rate(clock, tick);
    if (clock.jumps != jumps) {
        restart_history(clock, tick);
        return;
    }
    if (clock.sample_count < clock_history_ticks) {
        clock.history[(clock.first_sample + clock.sample_count) % clock_history_ticks] = {
            tick, clock.clock
        };
        ++clock.sample_count;
    } else {
        clock.history[clock.first_sample] = {tick, clock.clock};
        clock.first_sample = (clock.first_sample + 1) % clock_history_ticks;
    }
}

void Playout::choose_rate(Clock& clock, uint32_t tick) noexcept {
    // The arrivals older than the window go, all but the newest.
    while (clock.arrival_count > 1 &&
           tick - clock.arrivals[clock.first_arrival].tick > delay_window_ticks) {
        clock.first_arrival = (clock.first_arrival + 1) % arrivals_per_owner;
        --clock.arrival_count;
    }
    const auto arrival_at = [&clock](uint32_t i) -> const Arrival& {
        return clock.arrivals[(clock.first_arrival + i) % arrivals_per_owner];
    };
    // The owner's pace: the owner ticks its records advance each tick, the
    // slope of the least-squares line through the window's arrivals (their
    // ticks and owner ticks, counted from the newest). An owner whose game
    // runs slower than this machine's advances less than a tick a tick.
    int64_t pace = whole;
    if (clock.arrival_count > 1) {
        const Arrival& latest = arrival_at(clock.arrival_count - 1);
        if (int64_t{latest.tick} - int64_t{arrival_at(0).tick} >= int64_t{pace_span_ticks}) {
            const int64_t count = clock.arrival_count;
            int64_t sum_ticks = 0;
            int64_t sum_owner_ticks = 0;
            int64_t sum_ticks_squared = 0;
            int64_t sum_products = 0;
            for (uint32_t i = 0; i < clock.arrival_count; ++i) {
                const Arrival& arrival = arrival_at(i);
                const int64_t at = int64_t{arrival.tick} - int64_t{latest.tick};
                const int64_t owner_tick = int64_t{arrival.owner_tick} - int64_t{latest.owner_tick};
                sum_ticks += at;
                sum_owner_ticks += owner_tick;
                sum_ticks_squared += at * at;
                sum_products += at * owner_tick;
            }
            const int64_t spread = count * sum_ticks_squared - sum_ticks * sum_ticks;
            // The slope is bounded to twice a tick a tick before it is
            // scaled, which keeps the product within 64 bits.
            const int64_t slope = std::clamp<int64_t>(
                count * sum_products - sum_ticks * sum_owner_ticks, 0, 2 * spread
            );
            if (spread > 0)
                pace = std::clamp<int64_t>(clock_of(slope) / spread, min_rate, max_rate);
        }
    }
    // Each moment of the window on a line at the owner's pace: the owner
    // tick of its newest record less pace times the match's tick. The line
    // is furthest up as a record arrives (the freshest) and furthest down
    // just before the next one arrives (the stalest), the wait still open
    // included. Records arriving evenly at the owner's pace put the two one
    // wait apart.
    const auto line = [pace](int64_t owner_tick, uint32_t at) {
        return clock_of(owner_tick) - pace * int64_t{at};
    };
    int64_t freshest = line(clock.newest_tick, clock.started_tick);
    int64_t stalest = line(clock.newest_tick, tick);
    if (clock.arrival_count != 0) {
        freshest = std::numeric_limits<int64_t>::min();
        int32_t owner_tick_before = 0;
        for (uint32_t i = 0; i < clock.arrival_count; ++i) {
            const Arrival& arrival = arrival_at(i);
            freshest = std::max(freshest, line(arrival.owner_tick, arrival.tick));
            if (i != 0)
                stalest = std::min(stalest, line(owner_tick_before, arrival.tick));
            owner_tick_before = arrival.owner_tick;
        }
    }
    // The clock aims to run at the owner's pace a margin below the stalest
    // moment, and at least min_delay_ticks below the freshest
    // (start_delay_ticks over the first window); it keeps that buffer
    // through the longest wait the window saw. It never aims further than
    // max_delay_ticks behind the newest record, nor past it.
    int64_t aim =
        std::min(stalest - clock_of(delay_margin_ticks), freshest - clock_of(min_delay_ticks));
    if (tick - clock.started_tick < delay_window_ticks)
        aim = std::min(aim, freshest - clock_of(start_delay_ticks));
    const int64_t newest = clock_of(clock.newest_tick);
    const int64_t wanted_clock =
        std::clamp(pace * int64_t{tick} + aim, newest - clock_of(max_delay_ticks), newest);
    clock.pace = static_cast<uint32_t>(pace);
    clock.freshest = freshest;
    clock.target_delay = pace * int64_t{tick} + freshest - wanted_clock;

    if (newest - clock.clock > clock_of(snap_behind_ticks)) {
        // Too far behind to catch up: the clock jumps to its delay.
        clock.clock = wanted_clock;
        clock.rate = clock.pace;
        ++clock.jumps;
        return;
    }
    // The owner's pace, and a part of the gap to the wanted clock.
    int64_t rate =
        std::clamp<int64_t>(pace + (wanted_clock - clock.clock) / settle_ticks, min_rate, max_rate);
    rate = std::clamp<int64_t>(
        rate, int64_t{clock.rate} - int64_t{rate_step}, int64_t{clock.rate} + int64_t{rate_step}
    );
    // A short buffer slows the units down at once rather than stopping them.
    if (newest - clock.clock < clock_of(low_water_ticks))
        rate = std::min<int64_t>(rate, min_rate);
    clock.rate = static_cast<uint32_t>(rate);
}

int64_t Playout::clock_at(const Clock& clock, FrameTime time) const noexcept {
    const int64_t moment = int64_t{time.tick} * whole + std::min<int64_t>(time.fraction, whole);
    const int64_t observed = int64_t{observed_tick_} * whole;
    if (moment >= observed) {
        // After the tick observed the clock runs on at its rate, for at
        // most a tick, and no further than the newest record.
        const int64_t since = std::min(moment - observed, whole);
        return std::min(
            clock.clock + ((int64_t{clock.rate} * since) >> fraction_bits),
            clock_of(clock.newest_tick)
        );
    }
    // Before it, the clock as it ran from one tick observed to the next.
    const auto sample_at = [&clock](uint32_t i) -> const ClockSample& {
        return clock.history[(clock.first_sample + i) % clock_history_ticks];
    };
    for (uint32_t i = clock.sample_count - 1; i > 0; --i) {
        const ClockSample& later = sample_at(i);
        const ClockSample& earlier = sample_at(i - 1);
        const int64_t start = int64_t{earlier.tick} * whole;
        if (moment < start)
            continue;
        const int64_t span = (int64_t{later.tick} - int64_t{earlier.tick}) * whole;
        return earlier.clock + (later.clock - earlier.clock) * (moment - start) / span;
    }
    return sample_at(0).clock;
}

void Playout::record_units(
    const oa::World& world, const Hooks& hooks, uint8_t player, bool restarted
) noexcept {
    const oa::Player& owner = world.game.players[player];
    // The player's inclusive range of unit slots, as world_player_units reads it.
    const oa::Unit* first = oa::world_unit(&world, owner.first_unit);
    const oa::Unit* last = oa::world_unit(&world, owner.last_unit);
    if (first == nullptr || last == nullptr || last < first)
        return;
    const uint32_t first_slot = oa::world_unit_slot(&world, first);
    const uint32_t end = std::min<uint32_t>(
        oa::world_unit_slot(&world, last) + 1, static_cast<uint32_t>(tracks_.size())
    );
    for (uint32_t slot = first_slot; slot < end; ++slot) {
        const oa::Unit& unit = world.units[slot];
        const uint32_t generation = unit.type_index != 0 && hooks.slot_generation != nullptr
                                        ? hooks.slot_generation(hooks.context, slot)
                                        : 0;
        record_unit(world, unit, generation, tracks_[slot], clocks_[player], player, restarted);
    }
}

void Playout::record_unit(
    const oa::World& world,
    const oa::Unit& unit,
    uint32_t generation,
    Track& track,
    const Clock& clock,
    uint8_t player,
    bool restarted
) noexcept {
    if (unit.type_index == 0) {
        // The unit died, or the slot was never filled: nothing is drawn.
        track.active = false;
        track.count = 0;
        return;
    }
    const auto place_at = [&track](uint32_t i) -> Place& {
        return track.places[(track.first + i) % places_per_unit];
    };
    const auto push = [&track, &place_at](const Place& place) {
        if (track.count == places_per_unit) {
            track.first = (track.first + 1) % places_per_unit;
            --track.count;
        }
        place_at(track.count) = place;
        ++track.count;
    };
    Place now{};
    now.owner_tick = clock.newest_tick;
    now.pose = pose_of(unit);
    now.flags = unit.attach_parent != 0 ? place_carried : 0;
    if (restarted || !track.active || track.count == 0 || track.type_index != unit.type_index ||
        track.owner != player || track.generation != generation) {
        // A unit new to the slot appears where it was created.
        track.first = 0;
        track.count = 0;
        track.active = true;
        track.generation = generation;
        track.type_index = unit.type_index;
        track.owner = player;
        now.flags |= place_jump;
        push(now);
        return;
    }
    // The unit's top speed, in 16.16 pixels a tick; a carried unit goes as
    // fast as the unit carrying it.
    const oa::Unit* carrier =
        unit.attach_parent != 0 ? oa::world_unit(&world, unit.attach_parent) : nullptr;
    const oa::UnitDef* def = oa::world_unit_def_of(&world, carrier != nullptr ? carrier : &unit);
    const int64_t speed = def != nullptr ? std::max<int64_t>(def->max_velocity, 0) : 0;
    // A move between two places is a jump when it is longer than the unit
    // can go in the owner ticks between them by more than jump_speed_steps
    // steps of its top speed or jump_pixels pixels, whichever is less, or
    // when the unit was carried at one and not at the other.
    const int64_t allowance =
        std::min<int64_t>(int64_t{jump_speed_steps} * speed, clock_of(jump_pixels));
    const auto jumps = [speed, allowance](const Place& from, const Place& to) {
        if (((from.flags ^ to.flags) & place_carried) != 0)
            return true;
        const int64_t ticks = std::max<int64_t>(int64_t{to.owner_tick} - from.owner_tick, 1);
        const int64_t step = std::max(
            {distance(from.pose.position.x, to.pose.position.x),
             distance(from.pose.position.y, to.pose.position.y),
             distance(from.pose.position.z, to.pose.position.z)}
        );
        return step - speed * ticks > allowance;
    };
    Place& newest = place_at(track.count - 1);
    if (same_pose(newest.pose, now.pose) && ((newest.flags ^ now.flags) & place_carried) == 0)
        return;
    if (newest.owner_tick == now.owner_tick) {
        // Changed again within one owner tick: the newest place takes the
        // change.
        now.flags |= newest.flags & place_jump;
        if (track.count > 1 && jumps(place_at(track.count - 2), now))
            now.flags |= place_jump;
        newest = now;
        return;
    }
    if (newest.owner_tick < clock.tick_before && clock.tick_before < now.owner_tick) {
        // The unit stood where it was through the owner tick the last
        // observation of an earlier tick saw, and moved since.
        Place stood = newest;
        stood.owner_tick = clock.tick_before;
        stood.flags &= static_cast<uint8_t>(~place_jump);
        push(stood);
    }
    if (jumps(place_at(track.count - 1), now))
        now.flags |= place_jump;
    push(now);
}

void Playout::forget_player(uint8_t player) noexcept {
    clocks_[player] = Clock{};
    for (auto& track : tracks_)
        if (track.active && track.owner == player) {
            track.active = false;
            track.count = 0;
        }
}

std::optional<UnitPose> Playout::unit_pose(uint32_t slot, FrameTime time) const noexcept {
    if (slot >= tracks_.size())
        return std::nullopt;
    const Track& track = tracks_[slot];
    if (!track.active || track.count == 0)
        return std::nullopt;
    const auto place_at = [&track](uint32_t i) -> const Place& {
        return track.places[(track.first + i) % places_per_unit];
    };
    const Place& newest = place_at(track.count - 1);
    const Clock& clock = clocks_[track.owner];
    if (!clock.started)
        return newest.pose;
    const int64_t now = clock_at(clock, time);
    if (now >= clock_of(newest.owner_tick))
        return newest.pose;
    for (uint32_t i = track.count - 1; i > 0; --i) {
        const Place& after = place_at(i);
        const Place& before = place_at(i - 1);
        const int64_t start = clock_of(before.owner_tick);
        if (now < start)
            continue;
        if ((after.flags & place_jump) != 0)
            return before.pose;
        // The part of the way: the clock past the earlier place, over the
        // owner ticks between the two.
        const int64_t span =
            clock_of(std::max<int64_t>(int64_t{after.owner_tick} - before.owner_tick, 1));
        return blend_pose(before.pose, after.pose, now - start, span);
    }
    // The clock has not reached the oldest place held: the unit is shown
    // there, where it was created or where the places held begin.
    return place_at(0).pose;
}

OwnerPlayout Playout::owner(uint32_t player) const noexcept {
    OwnerPlayout state{};
    if (player >= OA_PLAYER_COUNT || !clocks_[player].started)
        return state;
    const Clock& clock = clocks_[player];
    state.followed = true;
    state.newest_tick = clock.newest_tick;
    state.clock = clock.clock;
    state.rate = clock.rate;
    state.pace = clock.pace;
    state.target_delay = clock.target_delay;
    state.delay = int64_t{clock.pace} * int64_t{observed_tick_} + clock.freshest - clock.clock;
    state.jumps = clock.jumps;
    return state;
}

} // namespace oa::present::unit_playout
