// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The playout of mirrored units over synthetic arrivals of an owner's
// records: steady, in bursts, stalled and fast; and the places it jumps to.
#include "oa/present/unit_playout.hpp"
#include "oa/sim/simulation_state.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <limits>
#include <optional>
#include <vector>

using namespace oa;
using namespace oa::present::unit_playout;

namespace {
int failures = 0;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x);                           \
            ++failures;                                                                            \
        }                                                                                          \
    } while (false)

/// Checks a condition inside a loop, reporting only the first failure of the loop.
#define CHECK_ONCE(flag, x)                                                                        \
    do {                                                                                           \
        if (!(flag) && !(x)) {                                                                     \
            std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x);                           \
            ++failures;                                                                            \
            (flag) = true;                                                                         \
        }                                                                                          \
    } while (false)

constexpr int64_t whole = whole_tick;

// Unit types: 1 a kbot that goes 2 pixels a tick, 2 a structure.
enum : uint16_t { kbot = 1, structure, type_count };

constexpr int32_t kbot_speed = 2 << 16;

// Player 0 is local and owns slots 1..3; player 1 is mirrored and owns 4..9.
constexpr uint8_t local_player = 0;
constexpr uint8_t mirrored_player = 1;
constexpr uint32_t local_slot = 1;
constexpr uint32_t first_mirrored_slot = 4;
constexpr uint32_t last_mirrored_slot = 9;
constexpr uint32_t slot_count = 10;

// The mirrored kbot walks along x at 1.5 pixels an owner tick from x = 1000.
constexpr uint32_t walker = 4;
constexpr int64_t walk_start = int64_t{1000} << 16;
constexpr int64_t walk_step = 3 << 15;

/// Frames drawn each tick: 120 frames a second.
constexpr uint32_t frames_per_tick = 4;

/// Returns the walker's x at an owner tick, 16.16.
///
/// @param owner_tick the owner tick
/// @return its x
int32_t walk_x(int64_t owner_tick) {
    return static_cast<int32_t>(walk_start + walk_step * owner_tick);
}

/// Returns a whole number of pixels as 16.16.
///
/// @param pixels the pixels
/// @return the 16.16 value
constexpr int32_t fixed(int32_t pixels) {
    return static_cast<int32_t>(static_cast<uint32_t>(pixels) << 16);
}

/// A world with a local and a mirrored player.
struct Scene {
    World* world{};

    /// Builds the world: the two players, their slots and the unit types.
    Scene() {
        world = world_create();
        WorldCapacity capacity{slot_count, type_count, 0};
        if (world == nullptr || !world_alloc_tables(world, &capacity))
            std::abort();
        world->unit_defs[kbot].max_velocity = kbot_speed;
        const auto own = [&](uint8_t index, uint8_t status, uint32_t first, uint32_t last) {
            Player& p = world->game.players[index];
            p.in_use = 1;
            p.index = index;
            p.status = status;
            p.first_unit = oa_unit_ref_from_slot(first);
            p.last_unit = oa_unit_ref_from_slot(last);
            for (uint32_t slot = first; slot <= last; ++slot) {
                world->units[slot].owner = oa_ref_from_index(index);
                world->units[slot].owner_index = index;
                world->units[slot].id = static_cast<uint16_t>(slot);
            }
        };
        own(local_player, OA_PLAYER_STATUS_LOCAL, local_slot, first_mirrored_slot - 1);
        own(mirrored_player, OA_PLAYER_STATUS_MIRRORED, first_mirrored_slot, last_mirrored_slot);
    }

    ~Scene() { world_destroy(world); }

    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;

    /// Returns the unit in a slot.
    ///
    /// @param slot the slot
    /// @return its record
    Unit& unit(uint32_t slot) { return world->units[slot]; }

    /// Puts a live unit of a type in a slot, 50 pixels up.
    ///
    /// @param slot the slot
    /// @param type the unit type
    /// @param x its x, 16.16
    /// @param z its z, 16.16
    void spawn(uint32_t slot, uint16_t type, int32_t x, int32_t z) {
        Unit& u = unit(slot);
        u.type_index = type;
        u.def = oa_ref_from_index(type);
        u.position = {x, fixed(50), z};
        u.flags = OA_UNIT_FLAG_LIVE;
    }
};

/// The owner tick of the newest record applied by a tick of the match.
using Arrivals = std::function<int32_t(uint32_t tick)>;

/// Records applied the tick they are made: the fastest a sender gets.
int32_t fast(uint32_t tick) {
    return static_cast<int32_t>(tick);
}

/// Records applied two ticks after they are made, one each tick.
int32_t steady(uint32_t tick) {
    return tick < 2 ? 0 : static_cast<int32_t>(tick) - 2;
}

/// Records sent every six ticks and applied together, every fourth send a
/// tick late: the owner stands still five or six ticks, then jumps six.
int32_t sent_every_six(uint32_t tick) {
    int32_t newest = 0;
    for (int32_t send = 1;; ++send) {
        const int64_t arrives = int64_t{send} * 6 + 1 + (send % 4 == 3 ? 1 : 0);
        if (arrives > tick)
            return newest;
        newest = send * 6;
    }
}

/// 3.1c's receive queue at its worst: the owner stands still five ticks and
/// jumps seven, then stands still five ticks and jumps five.
int32_t queue_bursts(uint32_t tick) {
    const uint32_t cycle = tick / 12;
    const uint32_t within = tick % 12;
    return static_cast<int32_t>(cycle * 12 + (within < 6 ? 0 : 7));
}

/// A queue that falls behind and catches up: for ten ticks a record every
/// other tick, then the backlog at once and one the tick after. No wait is
/// longer than two ticks, but the newest record falls six behind.
int32_t trickle_then_flush(uint32_t tick) {
    const uint32_t cycle = tick / 12;
    const uint32_t within = tick % 12;
    return static_cast<int32_t>(cycle * 12 + (within < 10 ? within / 2 : within));
}

/// An owner whose game runs at nine tenths of this machine's pace, its
/// records applied the tick they are made.
int32_t slow_owner(uint32_t tick) {
    return static_cast<int32_t>(uint64_t{tick} * 9 / 10);
}

/// Returns the arrivals of an owner whose game runs at a part of this
/// machine's pace, its records sent every six ticks and applied together.
///
/// @param numerator the owner's pace, over `denominator`
/// @param denominator the whole pace
/// @return the arrivals
Arrivals slow_bursts(uint32_t numerator, uint32_t denominator) {
    return [numerator, denominator](uint32_t tick) {
        return static_cast<int32_t>(uint64_t{tick / 6 * 6} * numerator / denominator);
    };
}

/// Records applied two ticks after they are made, with a stall of a number
/// of ticks from tick 150, whose records are then applied at once.
Arrivals stalled(uint32_t stall) {
    return
        [stall](uint32_t tick) { return steady(tick >= 150 && tick < 150 + stall ? 149 : tick); };
}

/// One frame of a run: the walker's pose and the playout's state.
struct Frame {
    uint32_t tick{};
    uint32_t fraction{};
    std::optional<UnitPose> pose{};
    int32_t newest_x{}; ///< the walker's x as the simulation has it
    OwnerPlayout owner{};
};

/// How the frames of a run are placed.
struct Framing {
    /// The frames show the tick before to the current one, rather than the
    /// current tick on.
    bool before_tick{};
    /// Ticks each frame's clock step runs; the frames of a step show the
    /// whole batch, through frame_time_between.
    uint32_t batch{1};
    /// Each tick is observed twice, as a match step that runs no tick does.
    bool observe_twice{};
};

/// Plays the walker out over a run of ticks.
///
/// @param arrivals the owner tick applied by each tick
/// @param ticks the ticks to run
/// @param framing how the frames are placed
/// @param change called at each tick before the playout observes it
/// @return the frames, frames_per_tick each tick
std::vector<Frame>
run(const Arrivals& arrivals,
    uint32_t ticks,
    Framing framing = {},
    const std::function<void(Scene&, uint32_t)>& change = {}) {
    Scene scene;
    Playout playout;
    scene.spawn(walker, kbot, walk_x(0), fixed(500));
    scene.spawn(local_slot, kbot, fixed(10), fixed(10));
    std::vector<Frame> frames;
    for (uint32_t tick = 1; tick <= ticks; ++tick) {
        scene.world->game.tick = tick;
        const int32_t newest = arrivals(tick);
        scene.world->game.players[mirrored_player].last_sim_tick = newest;
        scene.unit(walker).position.x = walk_x(newest);
        scene.unit(walker).heading = 0x4000;
        if (change)
            change(scene, tick);
        playout.observe(*scene.world);
        if (framing.observe_twice)
            playout.observe(*scene.world);
        if (tick % framing.batch != 0)
            continue;
        for (uint32_t k = 0; k < frames_per_tick * framing.batch; ++k) {
            const auto fraction =
                static_cast<uint32_t>(k * whole / (frames_per_tick * framing.batch));
            const FrameTime time = framing.batch > 1
                                       ? frame_time_between(tick - framing.batch, tick, fraction)
                                   : framing.before_tick ? FrameTime{tick - 1, fraction}
                                                         : FrameTime{tick, fraction};
            Frame frame{};
            frame.tick = time.tick;
            frame.fraction = time.fraction;
            frame.pose = playout.unit_pose(walker, time);
            frame.newest_x = scene.unit(walker).position.x;
            frame.owner = playout.owner(mirrored_player);
            frames.push_back(frame);
        }
    }
    return frames;
}

/// Returns the share of frames from a tick on that show the walker where
/// the frame before did.
///
/// @param frames the run
/// @param from the first tick counted
/// @return frames standing, over frames counted
double standing_share(const std::vector<Frame>& frames, uint32_t from) {
    uint32_t counted = 0;
    uint32_t standing = 0;
    for (std::size_t i = 1; i < frames.size(); ++i) {
        if (frames[i].tick < from || !frames[i].pose || !frames[i - 1].pose)
            continue;
        ++counted;
        if (frames[i].pose->position.x == frames[i - 1].pose->position.x)
            ++standing;
    }
    return counted == 0 ? 0.0 : static_cast<double>(standing) / counted;
}

/// Checks that the walker moves evenly from a tick on: never back, never
/// standing, every step between three quarters and one and a half of its
/// pace, and each step close to the one before.
///
/// @param frames the run
/// @param from the first tick checked
/// @param name the arrivals, for the report
void check_even(const std::vector<Frame>& frames, uint32_t from, const char* name) {
    const int64_t pace = walk_step / frames_per_tick;
    int64_t previous_step = -1;
    bool reported = false;
    int64_t smallest = std::numeric_limits<int64_t>::max();
    int64_t largest = 0;
    int64_t widest_change = 0;
    for (std::size_t i = 1; i < frames.size(); ++i) {
        if (frames[i].tick < from)
            continue;
        CHECK_ONCE(reported, frames[i].pose.has_value());
        if (!frames[i].pose || !frames[i - 1].pose)
            continue;
        const int64_t step = int64_t{frames[i].pose->position.x} - frames[i - 1].pose->position.x;
        smallest = std::min(smallest, step);
        largest = std::max(largest, step);
        if (previous_step >= 0)
            widest_change = std::max(
                widest_change, step > previous_step ? step - previous_step : previous_step - step
            );
        previous_step = step;
    }
    // A sixteenth of a pace a tick, and a unit of rounding.
    const int64_t allowed_change = pace / 16 + 2;
    if (smallest < pace * 3 / 4 - 2 || largest > pace * 3 / 2 + 2 ||
        widest_change > allowed_change) {
        std::fprintf(
            stderr,
            "%s: steps %lld..%lld (pace %lld), widest change %lld (allowed %lld)\n",
            name,
            static_cast<long long>(smallest),
            static_cast<long long>(largest),
            static_cast<long long>(pace),
            static_cast<long long>(widest_change),
            static_cast<long long>(allowed_change)
        );
        ++failures;
    }
}

/// Checks that no frame shows the walker past its newest place or off its path.
///
/// @param frames the run
void check_never_ahead(const std::vector<Frame>& frames) {
    bool reported = false;
    for (const Frame& frame : frames) {
        if (!frame.pose)
            continue;
        CHECK_ONCE(reported, frame.pose->position.x <= frame.newest_x);
        CHECK_ONCE(reported, frame.pose->position.x >= walk_x(0));
        CHECK_ONCE(reported, frame.pose->position.z == fixed(500));
        CHECK_ONCE(reported, frame.pose->heading == 0x4000);
    }
}

/// Checks that the clock's delay behind the freshest record settles on its target.
///
/// @param frames the run
/// @param from the first tick checked
/// @param target the delay expected, in owner ticks
/// @param tolerance how far the delay and its target may stray, 16.16 owner ticks
void check_delay(
    const std::vector<Frame>& frames, uint32_t from, int32_t target, int64_t tolerance = whole / 4
) {
    int64_t lowest_aim = std::numeric_limits<int64_t>::max();
    int64_t highest_aim = std::numeric_limits<int64_t>::min();
    int64_t widest_off = 0;
    for (const Frame& frame : frames) {
        if (frame.tick < from || frame.fraction != 0)
            continue;
        lowest_aim = std::min(lowest_aim, frame.owner.target_delay);
        highest_aim = std::max(highest_aim, frame.owner.target_delay);
        const int64_t off = frame.owner.delay - frame.owner.target_delay;
        widest_off = std::max(widest_off, off < 0 ? -off : off);
    }
    const int64_t expected = int64_t{target} * whole;
    if (lowest_aim < expected - tolerance || highest_aim > expected + tolerance ||
        widest_off > tolerance) {
        std::fprintf(
            stderr,
            "delay target %d: aims %.3f..%.3f, delay off by up to %.3f\n",
            target,
            static_cast<double>(lowest_aim) / whole,
            static_cast<double>(highest_aim) / whole,
            static_cast<double>(widest_off) / whole
        );
        ++failures;
    }
}

void test_fast_sender() {
    const auto frames = run(fast, 400);
    check_never_ahead(frames);
    // A record each tick: a wait of one tick, and the margin.
    check_delay(frames, 300, 1 + delay_margin_ticks);
    check_even(frames, 200, "fast");
    // The clock starts at the start delay.
    CHECK(frames.front().owner.delay == int64_t{start_delay_ticks} * whole);
}

void test_steady() {
    const auto frames = run(steady, 400);
    check_never_ahead(frames);
    // A record each tick: a wait of one tick, and the margin.
    check_delay(frames, 300, 1 + delay_margin_ticks);
    check_even(frames, 100, "steady");
}

void test_sent_every_six() {
    const auto frames = run(sent_every_six, 600);
    check_never_ahead(frames);
    // The longest wait is seven ticks; and the margin.
    check_delay(frames, 400, 7 + delay_margin_ticks);
    check_even(frames, 100, "sent every six");
    // The simulation's own walker stands still most frames.
    uint32_t standing = 0;
    for (std::size_t i = 1; i < frames.size(); ++i)
        if (frames[i].tick >= 100 && frames[i].newest_x == frames[i - 1].newest_x)
            ++standing;
    CHECK(standing > frames.size() / 2);
}

void test_queue_bursts() {
    const auto frames = run(queue_bursts, 600);
    check_never_ahead(frames);
    // The newest record falls seven ticks behind the freshest: six of the
    // wait, and one more for the five records that came short; and the
    // margin.
    check_delay(frames, 400, 7 + delay_margin_ticks);
    check_even(frames, 100, "queue bursts");
}

void test_trickle_then_flush() {
    // The delay covers how far the newest record falls behind, not only the
    // longest wait.
    const auto frames = run(trickle_then_flush, 600);
    check_never_ahead(frames);
    // Its uneven arrivals tilt the owner's measured pace a little from one
    // tick to the next, and the delay with it.
    check_delay(frames, 400, 6 + delay_margin_ticks, whole * 3 / 8);
    check_even(frames, 100, "trickle then flush");
}

void test_frames_before_the_tick() {
    // Frames drawn from the tick before to the current one move as evenly.
    const auto frames = run(queue_bursts, 600, {.before_tick = true});
    check_never_ahead(frames);
    check_even(frames, 100, "queue bursts, frames before the tick");
}

void test_long_stall() {
    // A stall of 21 ticks outlasts the longest delay: the walker slows, may
    // stand, and then catches up no faster than one and a half times its
    // pace, never jumping.
    const auto frames = run(stalled(21), 500);
    check_never_ahead(frames);
    const int64_t pace = walk_step / frames_per_tick;
    bool reported = false;
    for (std::size_t i = 1; i < frames.size(); ++i) {
        if (!frames[i].pose || !frames[i - 1].pose)
            continue;
        const int64_t step = int64_t{frames[i].pose->position.x} - frames[i - 1].pose->position.x;
        CHECK_ONCE(reported, step >= 0 && step <= pace * 3 / 2 + 2);
    }
    CHECK(frames.back().owner.jumps == 0);
    // Once the stall leaves the window the delay settles again.
    check_delay(frames, 400, 1 + delay_margin_ticks);
    check_even(frames, 400, "after a stall");
}

void test_stall_past_the_snap() {
    // A stall of 40 ticks leaves the clock further behind than it catches
    // up: it jumps to its delay once.
    const auto frames = run(stalled(40), 400);
    check_never_ahead(frames);
    CHECK(frames.back().owner.jumps == 1);
    const int64_t pace = walk_step / frames_per_tick;
    int64_t largest = 0;
    for (std::size_t i = 1; i < frames.size(); ++i)
        if (frames[i].pose && frames[i - 1].pose)
            largest = std::max<int64_t>(
                largest, int64_t{frames[i].pose->position.x} - frames[i - 1].pose->position.x
            );
    CHECK(largest > pace * 8);
}

void test_determinism() {
    const auto a = run(sent_every_six, 300);
    const auto b = run(sent_every_six, 300);
    CHECK(a.size() == b.size());
    bool reported = false;
    for (std::size_t i = 0; i < a.size() && i < b.size(); ++i) {
        CHECK_ONCE(reported, a[i].pose.has_value() == b[i].pose.has_value());
        if (a[i].pose && b[i].pose) {
            CHECK_ONCE(reported, a[i].pose->position.x == b[i].pose->position.x);
            CHECK_ONCE(reported, a[i].pose->heading == b[i].pose->heading);
        }
        CHECK_ONCE(reported, a[i].owner.clock == b[i].owner.clock);
        CHECK_ONCE(reported, a[i].owner.rate == b[i].owner.rate);
    }
}

void test_local_units_are_not_followed() {
    Scene scene;
    Playout playout;
    scene.spawn(local_slot, kbot, fixed(10), fixed(10));
    scene.spawn(walker, kbot, walk_x(0), fixed(500));
    scene.world->game.tick = 1;
    playout.observe(*scene.world);
    CHECK(!playout.unit_pose(local_slot, {1, 0}).has_value());
    CHECK(playout.unit_pose(walker, {1, 0}).has_value());
    CHECK(!playout.unit_pose(5, {1, 0}).has_value()); // an empty slot
    CHECK(!playout.unit_pose(slot_count, {1, 0}).has_value());
    CHECK(!playout.owner(local_player).followed);
    CHECK(playout.owner(mirrored_player).followed);
    // A player this machine starts to simulate is no longer followed.
    scene.world->game.players[mirrored_player].status = OA_PLAYER_STATUS_COMPUTER;
    scene.world->game.tick = 2;
    playout.observe(*scene.world);
    CHECK(!playout.unit_pose(walker, {2, 0}).has_value());
    CHECK(!playout.owner(mirrored_player).followed);
}

void test_followed_players_match_the_simulation() {
    // The playout follows exactly the in-use players whose units the
    // simulation does not run here.
    Scene scene;
    scene.spawn(walker, kbot, walk_x(0), fixed(500));
    for (uint8_t status = OA_PLAYER_STATUS_FREE; status <= OA_PLAYER_STATUS_CLOSED; ++status) {
        scene.world->game.players[mirrored_player].status = status;
        Playout playout;
        playout.observe(*scene.world);
        CHECK(
            playout.unit_pose(walker, {0, 0}).has_value() ==
            !oa::sim::simulation_state::locally_simulated(*scene.world, scene.unit(walker))
        );
    }
}

/// Returns the frames whose x lies strictly between two values.
///
/// @param frames the run
/// @param low the lower value, 16.16
/// @param high the higher value
/// @return how many frames show x between them
uint32_t frames_between(const std::vector<Frame>& frames, int32_t low, int32_t high) {
    uint32_t count = 0;
    for (const Frame& frame : frames)
        if (frame.pose && frame.pose->position.x > low && frame.pose->position.x < high)
            ++count;
    return count;
}

void test_correction_jumps() {
    // At owner tick 200 a full record moves the walker 100 pixels on: it
    // jumps there when its clock reaches that tick, and no frame shows it
    // between.
    constexpr int32_t correction = fixed(100);
    const auto frames = run(steady, 300, {}, [](Scene& scene, uint32_t tick) {
        if (steady(tick) >= 200)
            scene.unit(walker).position.x += correction;
    });
    CHECK(frames_between(frames, walk_x(200), walk_x(199) + correction) == 0);
    bool shown_before = false;
    bool shown_after = false;
    for (const Frame& frame : frames) {
        if (!frame.pose)
            continue;
        if (frame.pose->position.x <= walk_x(199))
            shown_before = true;
        if (frame.pose->position.x >= walk_x(200) + correction)
            shown_after = true;
    }
    CHECK(shown_before && shown_after);
    // The jump is shown when the clock reaches it, not as it arrives: the
    // frame the record arrives still shows the walker behind.
    for (const Frame& frame : frames)
        if (frame.tick == 202 && frame.fraction == 0 && frame.pose)
            CHECK(frame.pose->position.x < walk_x(200));
}

void test_correction_after_a_burst() {
    // Six owner ticks applied in one tick carry the walker six steps and a
    // full record's correction of 10 pixels: more than two steps of its top
    // speed beyond what it can walk in six ticks, so it jumps, however many
    // ticks the burst held.
    constexpr int32_t correction = fixed(10);
    const auto frames = run(sent_every_six, 300, {}, [](Scene& scene, uint32_t tick) {
        if (sent_every_six(tick) >= 120)
            scene.unit(walker).position.x += correction;
    });
    CHECK(frames_between(frames, walk_x(114) + 1, walk_x(120) + correction) == 0);
    // A correction of 3 pixels in the same burst is less than two steps
    // beyond its walk: it is drawn as part of the move.
    const auto small = run(sent_every_six, 300, {}, [](Scene& scene, uint32_t tick) {
        if (sent_every_six(tick) >= 120)
            scene.unit(walker).position.x += fixed(3);
    });
    CHECK(frames_between(small, walk_x(114) + 1, walk_x(120) + fixed(3)) > 0);
}

void test_small_correction_is_smoothed() {
    // A correction shorter than a step of the walker's top speed is drawn
    // as part of its move.
    constexpr int32_t correction = fixed(1);
    const auto frames = run(steady, 300, {}, [](Scene& scene, uint32_t tick) {
        if (steady(tick) >= 200)
            scene.unit(walker).position.x += correction;
    });
    CHECK(frames_between(frames, walk_x(199), walk_x(200) + correction) > 0);
}

void test_creation() {
    // A unit created at tick 100 stands where it was created until its
    // owner's clock reaches the tick, then walks on.
    constexpr uint32_t newcomer = 5;
    Scene scene;
    Playout playout;
    scene.spawn(walker, kbot, walk_x(0), fixed(500));
    bool reported = false;
    bool walked = false;
    for (uint32_t tick = 1; tick <= 200; ++tick) {
        scene.world->game.tick = tick;
        const int32_t newest = steady(tick);
        scene.world->game.players[mirrored_player].last_sim_tick = newest;
        if (tick == 100)
            scene.spawn(newcomer, kbot, fixed(300), fixed(300));
        if (tick > 100)
            scene.unit(newcomer).position.z = fixed(300) + (newest - steady(100)) * fixed(1);
        playout.observe(*scene.world);
        const auto pose = playout.unit_pose(newcomer, {tick, 0});
        if (tick < 100) {
            CHECK_ONCE(reported, !pose.has_value());
            continue;
        }
        CHECK_ONCE(reported, pose.has_value());
        if (!pose)
            continue;
        CHECK_ONCE(reported, pose->position.x == fixed(300));
        CHECK_ONCE(reported, pose->position.z >= fixed(300));
        CHECK_ONCE(reported, pose->position.z <= scene.unit(newcomer).position.z);
        const int64_t clock = playout.owner(mirrored_player).clock;
        if (clock <= int64_t{steady(100)} * whole)
            CHECK_ONCE(reported, pose->position.z == fixed(300));
        else
            walked = true;
    }
    CHECK(walked);
}

void test_death() {
    // A mirrored unit dies within a tick and leaves its slot empty: from that
    // tick on the slot is not drawn, though its clock was behind.
    Scene scene;
    Playout playout;
    scene.spawn(walker, kbot, walk_x(0), fixed(500));
    for (uint32_t tick = 1; tick <= 60; ++tick) {
        scene.world->game.tick = tick;
        const int32_t newest = steady(tick);
        scene.world->game.players[mirrored_player].last_sim_tick = newest;
        scene.unit(walker).position.x = walk_x(newest);
        if (tick == 50)
            scene.unit(walker).type_index = 0;
        playout.observe(*scene.world);
        const auto pose = playout.unit_pose(walker, {tick, whole / 2});
        if (tick < 50) {
            if (tick >= 10)
                CHECK(pose.has_value() && pose->position.x < scene.unit(walker).position.x);
        } else {
            CHECK(!pose.has_value());
        }
    }
}

void test_slot_reused_within_a_tick() {
    // A unit of the same type and owner made in a slot freed during the same
    // tick is told from the one before by the slot's generation: it stands
    // where it was made rather than sliding there along the dead unit's path.
    Scene scene;
    Playout playout;
    uint32_t generation = 1;
    const Hooks hooks{&generation, [](void* context, uint32_t) noexcept {
                          return *static_cast<uint32_t*>(context);
                      }};
    scene.spawn(walker, kbot, walk_x(0), fixed(500));
    bool reported = false;
    for (uint32_t tick = 1; tick <= 120; ++tick) {
        scene.world->game.tick = tick;
        const int32_t newest = steady(tick);
        scene.world->game.players[mirrored_player].last_sim_tick = newest;
        if (tick < 60) {
            scene.unit(walker).position.x = walk_x(newest);
        } else if (tick == 60) {
            // Died and remade 30 pixels from where the first unit was.
            ++generation;
            scene.unit(walker).position.x = walk_x(newest) + fixed(30);
        }
        playout.observe(*scene.world, hooks);
        if (tick < 60)
            continue;
        for (uint32_t k = 0; k < frames_per_tick; ++k) {
            const auto pose = playout.unit_pose(walker, {tick, k * whole_tick / frames_per_tick});
            CHECK_ONCE(reported, pose && pose->position.x == walk_x(steady(60)) + fixed(30));
        }
    }
}

void test_slow_owner() {
    // An owner whose game runs slower than this machine's: the clock runs at
    // the owner's pace, so the walker keeps moving rather than catching up
    // with its newest place and standing.
    const auto even = run(slow_owner, 800);
    check_never_ahead(even);
    CHECK(standing_share(even, 200) == 0.0);
    check_even(even, 200, "slow owner");
    CHECK(even.back().owner.pace > whole_tick * 89 / 100);
    CHECK(even.back().owner.pace < whole_tick * 91 / 100);
    // Sent every six ticks, at nine tenths and at four fifths of the pace.
    for (const uint32_t numerator : {9u, 8u}) {
        const auto bursts = run(slow_bursts(numerator, 10), 800);
        check_never_ahead(bursts);
        CHECK(standing_share(bursts, 200) == 0.0);
        check_even(bursts, 200, numerator == 9 ? "slow bursts, 0.9" : "slow bursts, 0.8");
    }
}

void test_frames_over_a_batch() {
    // A frame's clock step that runs three ticks at once shows the frames
    // between the first and the last of them on the clocks as they ran: the
    // walker moves on evenly, never standing and then jumping.
    const auto frames = run(queue_bursts, 600, {.batch = 3});
    check_never_ahead(frames);
    CHECK(standing_share(frames, 100) == 0.0);
    check_even(frames, 100, "frames over a batch of three ticks");
}

void test_same_tick_observed_twice() {
    // A second observation of the same tick, as a match step that runs no
    // tick gives, changes no frame.
    const auto once = run(queue_bursts, 300, {.before_tick = true});
    const auto twice = run(queue_bursts, 300, {.before_tick = true, .observe_twice = true});
    CHECK(once.size() == twice.size());
    bool reported = false;
    for (std::size_t i = 0; i < once.size() && i < twice.size(); ++i) {
        CHECK_ONCE(reported, once[i].pose.has_value() == twice[i].pose.has_value());
        if (once[i].pose && twice[i].pose)
            CHECK_ONCE(reported, once[i].pose->position.x == twice[i].pose->position.x);
    }
}

void test_frame_time_between() {
    CHECK(frame_time_between(10, 13, 0).tick == 10);
    CHECK(frame_time_between(10, 13, 0).fraction == 0);
    const FrameTime half = frame_time_between(10, 13, whole_tick / 2);
    CHECK(half.tick == 11 && half.fraction == whole_tick / 2);
    const FrameTime whole_way = frame_time_between(10, 13, whole_tick);
    CHECK(whole_way.tick == 13 && whole_way.fraction == 0);
    // The same tick, an earlier one, or one too far on: the later tick.
    CHECK(frame_time_between(13, 13, whole_tick / 2).tick == 13);
    CHECK(frame_time_between(14, 13, whole_tick / 2).tick == 13);
    CHECK(
        frame_time_between(13 - static_cast<uint32_t>(clock_history_ticks), 13, whole_tick / 2)
            .tick == 13
    );
    CHECK(
        frame_time_between(13 - static_cast<uint32_t>(clock_history_ticks), 13, whole_tick / 2)
            .fraction == 0
    );
    CHECK(
        frame_time_between(13 - static_cast<uint32_t>(clock_history_ticks - 1), 13, 0).tick ==
        13 - clock_history_ticks + 1
    );
    // Across the 32-bit wrap of the match's ticks.
    CHECK(frame_time_between(0xffffffffu, 1, whole_tick / 2).tick == 0);
}

void test_observe_is_noexcept() {
    // A failure in the playout costs no tick of the match.
    Scene scene;
    Playout playout;
    static_assert(noexcept(playout.observe(*scene.world)));
    static_assert(noexcept(playout.reset()));
}

void test_transport() {
    // At owner tick 100 the walker is loaded into a transport 40 pixels to
    // its side, rides with it, and at owner tick 140 is set down 40 pixels
    // further: both changes are jumps, shown when the clock reaches them.
    const auto carried = [](int32_t owner_tick) { return owner_tick >= 100 && owner_tick < 140; };
    const auto frames = run(steady, 260, {}, [&](Scene& scene, uint32_t tick) {
        const int32_t newest = steady(tick);
        Unit& u = scene.unit(walker);
        if (carried(newest)) {
            u.attach_parent = oa_unit_ref_from_slot(6);
            u.position.z = fixed(540);
        } else {
            u.attach_parent = 0;
            u.position.z = newest >= 140 ? fixed(580) : fixed(500);
        }
    });
    bool reported = false;
    bool loaded = false;
    bool unloaded = false;
    for (const Frame& frame : frames) {
        if (!frame.pose)
            continue;
        const int32_t z = frame.pose->position.z;
        CHECK_ONCE(reported, z == fixed(500) || z == fixed(540) || z == fixed(580));
        if (z == fixed(540)) {
            loaded = true;
            // Carried, it rides along: it is drawn past where it was loaded.
            CHECK_ONCE(reported, frame.pose->position.x >= walk_x(100));
        }
        if (z == fixed(580)) {
            unloaded = true;
            CHECK_ONCE(reported, frame.pose->position.x >= walk_x(140));
        }
        if (z == fixed(500))
            CHECK_ONCE(reported, frame.pose->position.x < walk_x(100));
    }
    CHECK(loaded && unloaded);
}

void test_structure_moves_are_jumps() {
    // A structure cannot move: any change of its place is a jump.
    Scene scene;
    Playout playout;
    constexpr uint32_t building = 6;
    scene.spawn(building, structure, fixed(700), fixed(700));
    bool reported = false;
    for (uint32_t tick = 1; tick <= 80; ++tick) {
        scene.world->game.tick = tick;
        scene.world->game.players[mirrored_player].last_sim_tick = steady(tick);
        if (steady(tick) == 40)
            scene.unit(building).position.x = fixed(704);
        playout.observe(*scene.world);
        for (uint32_t k = 0; k < frames_per_tick; ++k) {
            const auto pose = playout.unit_pose(building, {tick, k * whole_tick / frames_per_tick});
            CHECK_ONCE(
                reported, pose && (pose->position.x == fixed(700) || pose->position.x == fixed(704))
            );
        }
    }
}

void test_heading_turns_the_short_way() {
    // A turn across north (0xfff0 to 0x0010) is drawn through north, not
    // round the other way.
    Scene scene;
    Playout playout;
    scene.spawn(walker, kbot, walk_x(0), fixed(500));
    bool reported = false;
    for (uint32_t tick = 1; tick <= 100; ++tick) {
        scene.world->game.tick = tick;
        const int32_t newest = fast(tick);
        scene.world->game.players[mirrored_player].last_sim_tick = newest;
        scene.unit(walker).heading = static_cast<uint16_t>(newest < 60 ? 0xfff0 : 0x0010);
        playout.observe(*scene.world);
        for (uint32_t k = 0; k < frames_per_tick; ++k) {
            const auto pose = playout.unit_pose(walker, {tick, k * whole_tick / frames_per_tick});
            CHECK_ONCE(reported, pose && (pose->heading >= 0xfff0 || pose->heading <= 0x0010));
        }
    }
}

void test_restart() {
    // A tick earlier than the last observed starts afresh: the places of the
    // match before are gone.
    Scene scene;
    Playout playout;
    scene.spawn(walker, kbot, walk_x(0), fixed(500));
    for (uint32_t tick = 1; tick <= 50; ++tick) {
        scene.world->game.tick = tick;
        scene.world->game.players[mirrored_player].last_sim_tick = steady(tick);
        scene.unit(walker).position.x = walk_x(steady(tick));
        playout.observe(*scene.world);
    }
    scene.world->game.tick = 3;
    scene.world->game.players[mirrored_player].last_sim_tick = 1;
    scene.unit(walker).position.x = fixed(20);
    playout.observe(*scene.world);
    const auto pose = playout.unit_pose(walker, {3, 0});
    CHECK(pose.has_value() && pose->position.x == fixed(20));
    CHECK(playout.observed_tick() == 3);
    CHECK(playout.owner(mirrored_player).newest_tick == 1);
    playout.reset();
    CHECK(!playout.unit_pose(walker, {3, 0}).has_value());
    CHECK(!playout.owner(mirrored_player).followed);
}

void test_late_record() {
    // A record three owner ticks older than the newest, applied late, steps
    // the walker on but moves no clock and starts nothing afresh: the walker
    // goes on along its path, never back and never jumping.
    const auto frames = run(steady, 400, {}, [](Scene& scene, uint32_t tick) {
        if (tick == 300)
            scene.world->game.players[mirrored_player].last_sim_tick -= 3;
    });
    check_never_ahead(frames);
    CHECK(frames.back().owner.jumps == 0);
    const int64_t pace = walk_step / frames_per_tick;
    bool reported = false;
    for (std::size_t i = 1; i < frames.size(); ++i) {
        if (frames[i].tick < 100 || !frames[i].pose || !frames[i - 1].pose)
            continue;
        const int64_t step = int64_t{frames[i].pose->position.x} - frames[i - 1].pose->position.x;
        CHECK_ONCE(reported, step > 0 && step <= pace * 5 / 2);
    }
    check_even(frames, 340, "after a late record");
}

void test_bounded_places() {
    // A unit that moves every tick for far longer than its ring holds keeps
    // only the newest places, and is still drawn on its path.
    const auto frames = run(fast, 1000);
    check_never_ahead(frames);
    check_even(frames, 900, "fast, long");
}

} // namespace

int main() {
    test_fast_sender();
    test_steady();
    test_sent_every_six();
    test_queue_bursts();
    test_trickle_then_flush();
    test_frames_before_the_tick();
    test_long_stall();
    test_stall_past_the_snap();
    test_determinism();
    test_local_units_are_not_followed();
    test_followed_players_match_the_simulation();
    test_correction_jumps();
    test_small_correction_is_smoothed();
    test_creation();
    test_death();
    test_slot_reused_within_a_tick();
    test_slow_owner();
    test_frames_over_a_batch();
    test_same_tick_observed_twice();
    test_frame_time_between();
    test_observe_is_noexcept();
    test_correction_after_a_burst();
    test_transport();
    test_structure_moves_are_jumps();
    test_heading_turns_the_short_way();
    test_restart();
    test_late_record();
    test_bounded_places();
    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::puts("unit playout: all checks passed");
    return 0;
}
