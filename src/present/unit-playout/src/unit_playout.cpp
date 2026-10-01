// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The playout of mirrored units (unit_playout.hpp).
#include "oa/present/unit_playout.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <numbers>

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

/// A whole 16.16 coordinate or speed, as a double.
constexpr double pixel = 65536.0;

/// A whole turn in angle units.
constexpr double turn = 65536.0;

/// How close, in squared map pixels, a unit comes to its next route point
/// before the route moves on to the point after it.
constexpr double route_reach_squared = 26.0;

/// How far, in map pixels, a ground unit steers at a point short of its next
/// route point, along the route's leg, while it is further from it than this.
constexpr double route_lead_pixels = 80.0;

/// The distance, in map pixels, from an aircraft's air point beyond which it
/// turns to face the point.
constexpr double air_face_pixels = 20.0;

/// The least distance, in map pixels, an aircraft's steering toward its air
/// point is measured over.
constexpr double air_steer_pixels = 8.0;

/// Returns a turn wrapped to the short way round.
///
/// @param angle a difference of two angles, in angle units
/// @return the difference, truncated toward zero, from -32768 to 32767
double short_turn(double angle) noexcept {
    const auto units = static_cast<int64_t>(angle);
    return static_cast<double>(static_cast<int16_t>(static_cast<uint16_t>(units & 0xffff)));
}

/// Returns an angle wrapped to one turn.
///
/// @param angle an angle in angle units
/// @return the angle, 0 to below 65536
double wrap_turn(double angle) noexcept {
    const double wrapped = std::fmod(angle, turn);
    return wrapped < 0 ? wrapped + turn : wrapped;
}

/// Returns the map step one pixel along a heading.
///
/// @param heading the heading in angle units
/// @return the x and z steps
std::array<double, 2> heading_step(double heading) noexcept {
    const double radians = heading * 2.0 * std::numbers::pi / turn;
    return {-std::sin(radians), -std::cos(radians)};
}

/// Returns the heading that moves from one point toward another.
///
/// @param x the point's x, map pixels
/// @param z the point's z
/// @param to_x the other point's x
/// @param to_z the other point's z
/// @return the heading in angle units, 0 to below 65536
double heading_toward(double x, double z, double to_x, double to_z) noexcept {
    return wrap_turn(std::atan2(x - to_x, z - to_z) * turn / (2.0 * std::numbers::pi));
}

/// Returns how much of a correction is left part of the way through its fading.
///
/// @param since the match ticks since the correction was taken, 16 bits of fraction
/// @return 1 at the start, 0 from correction_ticks on, easing in and out
double correction_left(int64_t since) noexcept {
    if (since <= 0)
        return 1.0;
    if (since >= correction_ticks)
        return 0.0;
    const double part = static_cast<double>(since) / static_cast<double>(correction_ticks);
    return 1.0 - part * part * (3.0 - 2.0 * part);
}

/// Returns a 16.16 coordinate moved by a part of a correction.
///
/// @param at the coordinate
/// @param correction the correction, 16.16
/// @param left the part of it left
/// @return the coordinate, rounded to the nearest
int32_t corrected(int32_t at, double correction, double left) noexcept {
    return static_cast<int32_t>(
        static_cast<int64_t>(at) + static_cast<int64_t>(std::lround(correction * left))
    );
}

} // namespace

void Playout::reset() noexcept {
    clocks_ = {};
    tracks_ = std::vector<Track>{};
    drawn_ = false;
    drawn_moment_ = 0;
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
    if (observed_ && drawn_) {
        // The units' corrections join where the last frame drawn since the
        // last observation showed them, on the clocks as they were. With no
        // frame drawn since, as over the ticks of a batch, they join where
        // they joined before.
        const FrameTime last_drawn{
            static_cast<uint32_t>(drawn_moment_ >> fraction_bits),
            static_cast<uint32_t>(drawn_moment_ & (whole - 1))
        };
        for (Clock& clock : clocks_)
            if (clock.started) {
                clock.join_clock = clock_at(clock, last_drawn);
                clock.join_moment = drawn_moment_;
            }
    }
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
    drawn_ = false;
}

void Playout::start_clock(Clock& clock, int32_t newest_tick, uint32_t tick) noexcept {
    clock = Clock{};
    clock.started = true;
    clock.newest_tick = newest_tick;
    clock.tick_before = newest_tick;
    clock.newest_before = newest_tick;
    clock.clock = clock_of(newest_tick);
    clock.started_tick = tick;
    clock.join_clock = clock.clock;
    clock.join_moment = clock_of(tick);
    clock.newest[0] = {tick, newest_tick};
    clock.newest_count = 1;
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
    clock.newest_before = clock.newest_tick;
    if (elapsed != 0) {
        clock.tick_before = clock.newest_tick;
        // The clock runs on as the frames since the last observation showed
        // it: at its rate, and no further ahead of the newest record they had
        // than predict_ticks.
        clock.clock = std::min(
            clock.clock + int64_t{clock.rate} * elapsed,
            clock_of(int64_t{clock.newest_tick} + predict_ticks)
        );
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
    // The newest record at this tick, for the middle the clock aims at.
    if (clock.newest_count < arrivals_per_owner) {
        clock.newest[(clock.first_newest + clock.newest_count) % arrivals_per_owner] = {
            tick, clock.newest_tick
        };
        ++clock.newest_count;
    } else {
        clock.newest[clock.first_newest] = {tick, clock.newest_tick};
        clock.first_newest = (clock.first_newest + 1) % arrivals_per_owner;
    }
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
    // The arrivals older than the window go, all but the newest; so do the
    // newest records of ticks older than the window.
    while (clock.arrival_count > 1 &&
           tick - clock.arrivals[clock.first_arrival].tick > delay_window_ticks) {
        clock.first_arrival = (clock.first_arrival + 1) % arrivals_per_owner;
        --clock.arrival_count;
    }
    while (clock.newest_count > 1 &&
           tick - clock.newest[clock.first_newest].tick >= delay_window_ticks) {
        clock.first_newest = (clock.first_newest + 1) % arrivals_per_owner;
        --clock.newest_count;
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
    // The middle of the newest records over the window: the mean, over the
    // ticks observed, of the newest record less pace times the tick, which a
    // line at the owner's pace through the steps of the records runs along.
    // Records that arrive in bursts put it half a wait below the freshest.
    int64_t sum = 0;
    for (uint32_t i = 0; i < clock.newest_count; ++i) {
        const Arrival& sample = clock.newest[(clock.first_newest + i) % arrivals_per_owner];
        sum += clock_of(sample.owner_tick) - pace * int64_t{sample.tick};
    }
    const int64_t middle = sum / std::max<int64_t>(clock.newest_count, 1);
    // The clock aims at the owner's pace aim_behind_middle below the middle,
    // never further than max_delay_ticks behind the newest record nor more
    // than predict_ticks ahead of it.
    const int64_t newest = clock_of(clock.newest_tick);
    const int64_t wanted_clock = std::clamp(
        pace * int64_t{tick} + middle - aim_behind_middle,
        newest - clock_of(max_delay_ticks),
        newest + clock_of(predict_ticks)
    );
    clock.pace = static_cast<uint32_t>(pace);
    clock.target_lead = wanted_clock - newest;

    if (newest - clock.clock > clock_of(snap_behind_ticks)) {
        // Too far behind to catch up: the clock jumps to its aim.
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
    // Near the end of the path ahead the units slow down at once rather than stopping.
    if (newest + clock_of(predict_ticks) - clock.clock < clock_of(low_water_ticks))
        rate = std::min<int64_t>(rate, min_rate);
    clock.rate = static_cast<uint32_t>(rate);
}

int64_t Playout::clock_at(const Clock& clock, FrameTime time) const noexcept {
    const int64_t moment = int64_t{time.tick} * whole + std::min<int64_t>(time.fraction, whole);
    const int64_t observed = int64_t{observed_tick_} * whole;
    if (moment >= observed) {
        // After the tick observed the clock runs on at its rate, for at
        // most a tick, and no further than predict_ticks past the newest record.
        const int64_t since = std::min(moment - observed, whole);
        return std::min(
            clock.clock + ((int64_t{clock.rate} * since) >> fraction_bits),
            clock_of(int64_t{clock.newest_tick} + predict_ticks)
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
    const Clock& clock = clocks_[player];
    const uint32_t first_slot = oa::world_unit_slot(&world, first);
    const uint32_t end = std::min<uint32_t>(
        oa::world_unit_slot(&world, last) + 1, static_cast<uint32_t>(tracks_.size())
    );
    for (uint32_t slot = first_slot; slot < end; ++slot) {
        const oa::Unit& unit = world.units[slot];
        Track& track = tracks_[slot];
        // Where the frames before this observation last drew the unit: on
        // its places and path as they were, with its correction as it had
        // faded by then.
        const bool drawn = !restarted && track.active && track.count != 0;
        UnitPose was{};
        std::array<double, 3> was_correction{};
        if (drawn) {
            was = track_pose(track, clock.newest_before, clock.join_clock);
            const double left = correction_left(clock.join_moment - track.correction_moment);
            for (std::size_t axis = 0; axis < 3; ++axis)
                was_correction[axis] = track.correction[axis] * left;
        }
        const uint32_t generation = unit.type_index != 0 && hooks.slot_generation != nullptr
                                        ? hooks.slot_generation(hooks.context, slot)
                                        : 0;
        const bool fresh = record_unit(world, unit, generation, track, clock, player, restarted);
        if (!track.active)
            continue;
        Motion motion{};
        if (hooks.motion != nullptr)
            hooks.motion(hooks.context, slot, motion);
        plan_ahead(world, unit, motion, clock.newest_tick, track);
        track.correction = {};
        track.correction_moment = clock.join_moment;
        if (fresh || !drawn)
            continue;
        // A place reached by a jump since the newest record before: the unit
        // jumps with it.
        bool jumped = false;
        for (uint32_t i = track.count; i > 0 && !jumped; --i) {
            const Place& place = track.places[(track.first + i - 1) % places_per_unit];
            if (place.owner_tick <= clock.newest_before)
                break;
            jumped = (place.flags & place_jump) != 0;
        }
        if (jumped)
            continue;
        const UnitPose now = track_pose(track, clock.newest_tick, clock.join_clock);
        const std::array<double, 3> correction{
            static_cast<double>(was.position.x) + was_correction[0] -
                static_cast<double>(now.position.x),
            static_cast<double>(was.position.y) + was_correction[1] -
                static_cast<double>(now.position.y),
            static_cast<double>(was.position.z) + was_correction[2] -
                static_cast<double>(now.position.z)
        };
        // A correction longer than snap_pixels across the map is a jump.
        if (std::hypot(correction[0], correction[2]) <= double{snap_pixels} * pixel)
            track.correction = correction;
    }
}

bool Playout::record_unit(
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
        return true;
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
        return true;
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
        return false;
    if (newest.owner_tick == now.owner_tick) {
        // Changed again within one owner tick: the newest place takes the
        // change.
        now.flags |= newest.flags & place_jump;
        if (track.count > 1 && jumps(place_at(track.count - 2), now))
            now.flags |= place_jump;
        newest = now;
        return false;
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
    return false;
}

void Playout::plan_ahead(
    const oa::World& world,
    const oa::Unit& unit,
    const Motion& motion,
    int32_t newest_tick,
    Track& track
) noexcept {
    auto& ahead = track.ahead;
    double x = static_cast<double>(unit.position.x) / pixel;
    double z = static_cast<double>(unit.position.z) / pixel;
    double heading = static_cast<double>(unit.heading);
    ahead[0] = {x, z, heading};
    const auto hold = [&ahead]() {
        for (std::size_t i = 1; i < ahead.size(); ++i)
            ahead[i] = ahead[0];
    };
    const auto glide = [&ahead](double step_x, double step_z) {
        for (std::size_t i = 1; i < ahead.size(); ++i)
            ahead[i] = {ahead[i - 1].x + step_x, ahead[i - 1].z + step_z, ahead[0].heading};
    };
    const oa::UnitDef* def = oa::world_unit_def_of(&world, &unit);
    if (unit.attach_parent != 0 || def == nullptr) {
        // A carried unit goes with its carrier.
        hold();
        return;
    }
    const auto place_at = [&track](uint32_t i) -> const Place& {
        return track.places[(track.first + i) % places_per_unit];
    };
    // Without a movement record to read, or blocked on the ground, the unit
    // goes on at the pace of its last two places, unless it stood since or
    // jumped into the newer, and no faster than its top speed.
    if ((!motion.ground && !motion.air) || (motion.blocked && !motion.air)) {
        if (track.count < 2 || place_at(track.count - 1).owner_tick < newest_tick ||
            (place_at(track.count - 1).flags & place_jump) != 0) {
            hold();
            return;
        }
        const Place& later = place_at(track.count - 1);
        const Place& earlier = place_at(track.count - 2);
        const double ticks = std::max(later.owner_tick - earlier.owner_tick, 1);
        double step_x =
            (static_cast<double>(later.pose.position.x) - earlier.pose.position.x) / pixel / ticks;
        double step_z =
            (static_cast<double>(later.pose.position.z) - earlier.pose.position.z) / pixel / ticks;
        const double top_speed = std::max<double>(def->max_velocity, 0) / pixel;
        const double step = std::hypot(step_x, step_z);
        if (step > top_speed) {
            const double keep = step > 0 ? top_speed / step : 0.0;
            step_x *= keep;
            step_z *= keep;
        }
        glide(step_x, step_z);
        return;
    }
    const double top_speed = static_cast<double>(def->max_velocity) / pixel;
    const double acceleration = static_cast<double>(def->acceleration) / pixel;
    const double brake = static_cast<double>(def->brake_rate) / pixel;
    const double turn_rate = std::max<double>(def->turn_rate, 1);
    if (motion.air) {
        if (motion.layer != motion_layer_air) {
            // Not flying: it goes on as its movement record moves it.
            glide(
                static_cast<double>(motion.velocity[0]) / pixel,
                static_cast<double>(motion.velocity[2]) / pixel
            );
            return;
        }
        // In flight it follows its air point, which moves on as it last
        // moved, or by its seek goal's step.
        double velocity_x = static_cast<double>(motion.velocity[0]) / pixel;
        double velocity_z = static_cast<double>(motion.velocity[2]) / pixel;
        double point_x = static_cast<double>(motion.air_point.x) / pixel;
        double point_z = static_cast<double>(motion.air_point.z) / pixel;
        double point_velocity_x = static_cast<double>(motion.air_velocity.x) / pixel;
        double point_velocity_z = static_cast<double>(motion.air_velocity.z) / pixel;
        if (motion.seek) {
            point_velocity_x = static_cast<double>(motion.seek_step.x) / pixel;
            point_velocity_z = static_cast<double>(motion.seek_step.z) / pixel;
        }
        double facing = static_cast<double>(motion.air_heading);
        const double damping = top_speed > 0 ? acceleration / top_speed : 0.0;
        for (std::size_t i = 1; i < ahead.size(); ++i) {
            point_x += point_velocity_x;
            point_z += point_velocity_z;
            const double distance = std::hypot(x - point_x, z - point_z);
            if (distance > air_face_pixels)
                facing = heading_toward(x, z, point_x, point_z);
            velocity_x *= 1.0 - damping;
            velocity_z *= 1.0 - damping;
            const double level = std::hypot(velocity_x, velocity_z);
            if (level > brake) {
                const double keep = brake / level;
                velocity_x *= keep;
                velocity_z *= keep;
                const auto step = heading_step(heading);
                velocity_x += step[0] * (level - brake);
                velocity_z += step[1] * (level - brake);
            }
            heading = wrap_turn(
                heading + std::clamp(short_turn(facing - heading), -turn_rate, turn_rate)
            );
            const double gain =
                -std::sqrt(2.0 * acceleration / std::max(distance, air_steer_pixels));
            double push_x = (x - point_x) * gain - (velocity_x - point_velocity_x);
            double push_z = (z - point_z) * gain - (velocity_z - point_velocity_z);
            const double push = std::hypot(push_x, push_z);
            if (push > acceleration && push > 0) {
                push_x *= acceleration / push;
                push_z *= acceleration / push;
            }
            velocity_x += push_x;
            velocity_z += push_z;
            x += velocity_x;
            z += velocity_z;
            ahead[i] = {x, z, heading};
        }
        return;
    }
    // On the ground it steers along the route its owner shared, speeding up
    // and braking as the route's turns allow, and slows to a stop without one.
    double speed = static_cast<double>(motion.speed) / pixel;
    const double fastest = std::max(top_speed, speed);
    std::array<std::array<double, 2>, 3> route{};
    const int32_t count = std::min<int32_t>(motion.route_count, 3);
    for (int32_t i = 0; i < 3; ++i) {
        const auto& point = motion.route[static_cast<std::size_t>(std::clamp(i, 0, count - 1))];
        route[static_cast<std::size_t>(i)] = {
            static_cast<double>(point[0]), static_cast<double>(point[1])
        };
    }
    bool routed = count > 1;
    for (std::size_t i = 1; i < ahead.size(); ++i) {
        if (!routed)
            speed = std::max(speed - brake, 0.0);
        else {
            const auto& [from, next, after] = route;
            double toward_x = next[0];
            double toward_z = next[1];
            const double distance = std::hypot(toward_x - x, toward_z - z);
            if (distance > route_lead_pixels) {
                const double leg_x = next[0] - from[0];
                const double leg_z = next[1] - from[1];
                const double leg = std::hypot(leg_x, leg_z);
                if (leg >= 1) {
                    const double back = std::min(leg, distance - route_lead_pixels);
                    toward_x -= leg_x / leg * back;
                    toward_z -= leg_z / leg * back;
                }
            }
            const double wanted = short_turn(heading_toward(x, z, toward_x, toward_z) - heading);
            heading = wrap_turn(heading + std::clamp(wanted, -turn_rate, turn_rate));
            const double turn_distance = speed * std::abs(wanted) / turn_rate;
            const double stop = brake > 0 ? speed * speed / (2.0 * brake) : 0.0;
            double change = -brake;
            const double to_x = toward_x - x;
            const double to_z = toward_z - z;
            const double end_x = after[0] - x;
            const double end_z = after[1] - z;
            if (4.0 * turn_distance * turn_distance < to_x * to_x + to_z * to_z &&
                stop * stop < end_x * end_x + end_z * end_z)
                change = acceleration;
            speed = std::clamp(speed + change, 0.0, fastest);
        }
        const auto step = heading_step(heading);
        x += step[0] * speed;
        z += step[1] * speed;
        if (routed) {
            const auto& next = route[1];
            if ((x - next[0]) * (x - next[0]) + (z - next[1]) * (z - next[1]) <
                route_reach_squared) {
                route = {route[1], route[2], route[2]};
                routed = route[2] != route[1];
            }
        }
        ahead[i] = {x, z, heading};
    }
}

void Playout::forget_player(uint8_t player) noexcept {
    clocks_[player] = Clock{};
    for (auto& track : tracks_)
        if (track.active && track.owner == player) {
            track.active = false;
            track.count = 0;
        }
}

UnitPose Playout::track_pose(const Track& track, int32_t newest_tick, int64_t at) noexcept {
    const auto place_at = [&track](uint32_t i) -> const Place& {
        return track.places[(track.first + i) % places_per_unit];
    };
    const Place& newest = place_at(track.count - 1);
    if (at > clock_of(newest_tick)) {
        // Past the newest record: on the path ahead, part of the way from
        // one owner tick's step to the next.
        const int64_t past = std::min(at - clock_of(newest_tick), clock_of(predict_ticks));
        const auto index = static_cast<std::size_t>(past >> fraction_bits);
        const double part = static_cast<double>(past & (whole - 1)) / static_cast<double>(whole);
        const Step& from = track.ahead[index];
        const Step& to = track.ahead[std::min(index + 1, track.ahead.size() - 1)];
        UnitPose pose = newest.pose;
        pose.position.x =
            static_cast<int32_t>(std::lround((from.x + (to.x - from.x) * part) * pixel));
        pose.position.z =
            static_cast<int32_t>(std::lround((from.z + (to.z - from.z) * part) * pixel));
        pose.heading = static_cast<oa_angle>(
            static_cast<int64_t>(from.heading + short_turn(to.heading - from.heading) * part) &
            0xffff
        );
        return pose;
    }
    if (at >= clock_of(newest.owner_tick))
        return newest.pose;
    for (uint32_t i = track.count - 1; i > 0; --i) {
        const Place& after = place_at(i);
        const Place& before = place_at(i - 1);
        const int64_t start = clock_of(before.owner_tick);
        if (at < start)
            continue;
        if ((after.flags & place_jump) != 0)
            return before.pose;
        // The part of the way: the clock past the earlier place, over the
        // owner ticks between the two.
        const int64_t span =
            clock_of(std::max<int64_t>(int64_t{after.owner_tick} - before.owner_tick, 1));
        return blend_pose(before.pose, after.pose, at - start, span);
    }
    // The clock has not reached the oldest place held: the unit is shown
    // there, where it was created or where the places held begin.
    return place_at(0).pose;
}

std::optional<UnitPose> Playout::unit_pose(uint32_t slot, FrameTime time) const noexcept {
    const int64_t asked = int64_t{time.tick} * whole + std::min<int64_t>(time.fraction, whole);
    drawn_moment_ = drawn_ ? std::max(drawn_moment_, asked) : asked;
    drawn_ = true;
    if (slot >= tracks_.size())
        return std::nullopt;
    const Track& track = tracks_[slot];
    if (!track.active || track.count == 0)
        return std::nullopt;
    const Clock& clock = clocks_[track.owner];
    if (!clock.started)
        return track.places[(track.first + track.count - 1) % places_per_unit].pose;
    UnitPose pose = track_pose(track, clock.newest_tick, clock_at(clock, time));
    // The difference new records made, fading out.
    const int64_t moment = int64_t{time.tick} * whole + std::min<int64_t>(time.fraction, whole);
    const double left = correction_left(moment - track.correction_moment);
    if (left > 0) {
        pose.position.x = corrected(pose.position.x, track.correction[0], left);
        pose.position.y = corrected(pose.position.y, track.correction[1], left);
        pose.position.z = corrected(pose.position.z, track.correction[2], left);
    }
    return pose;
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
    state.target_lead = clock.target_lead;
    state.lead = clock.clock - clock_of(clock.newest_tick);
    state.jumps = clock.jumps;
    return state;
}

} // namespace oa::present::unit_playout
