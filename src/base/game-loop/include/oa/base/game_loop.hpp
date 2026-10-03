// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>

namespace oa::base::game_loop {
// Names describe each field's role in the loop. Where a value stands for a
// field of the canonical Game or Player record, its comment names that field.
struct PlayerTiming {
    bool present{};      // Player.in_use
    uint8_t status{};    // Player.status; only a mirrored player (status 3) can lag
    uint16_t eligible{}; // Player.unit_count; a player with no units cannot lag
    uint32_t tick{};     // Player.last_sim_tick, compared as signed 32 bits
};
enum class Precision : unsigned { significand_53 = 53, significand_64 = 64 };

struct Timing {
    // Working precision of the timing arithmetic, not a Game field; 3.1c's results
    // use a 53-bit significand.
    Precision precision = Precision::significand_53;
    uint32_t previous_clock{};              // Game.last_frame_time
    int32_t pending_steps{};                // Game.pending_ticks
    uint32_t elapsed_bits{};                // Game.frame_elapsed, converted as signed 32 bits
    float remainder{};                      // Game.tick_remainder
    uint32_t tick{};                        // Game.tick
    uint16_t requested_rate{};              // Game.requested_speed
    uint16_t actual_rate{};                 // Game.current_speed
    int16_t adaptation{};                   // Game.speed_adaptation
    uint16_t flags{};                       // Game.sim_run_flags; bit 0 pauses stepping
    std::array<PlayerTiming, 10> players{}; // Game.players
    // The player whose simulation trails furthest; empty when no player lags.
    std::optional<uint8_t> slowest_player;
    uint32_t lag_bits{}; // ticks the slowest player trails Game.tick, wrapping at 32 bits
};

struct State {
    Timing timing;
    uint8_t session_flags{};              // Game.session_flags; bit 0 chooses the live path
    uint16_t frame_flags{};               // Game.frame_flags
    uint8_t gui_flags{};                  // Game.gui_flags
    uint16_t panel_unit_id{};             // Game.panel_unit_id
    uint16_t deferred_flags{};            // Game.load_flags; bit 2 defers the slot ordering
    uint8_t local_player_index{};         // Game.local_player_index
    bool multiplayer_active{};            // a multiplayer game is running
    uint32_t paused_deadline{};           // signed comparison
    int32_t capture_enabled{};            // Game.capture_enabled
    int32_t capture_rate{};               // Game.capture_rate, a divisor
    uint32_t next_capture_tick{};         // Game.next_capture_tick, unsigned comparison
    std::array<char, 256> capture_path{}; // Game.capture_path
};
// The subsystem calls of the loop that take no explicit argument. Their
// implementation is a mandatory host responsibility. No default callback silently
// succeeds; calls may update State before subsequent branches.
enum class Step : uint32_t {
    process_player_commands = 1,
    tick_unit_movement,
    tick_projectiles,
    tick_explosion_pieces,
    tick_players,
    tick_features,
    step_spans,
    schedule_random_event,
    step_meteor_strike,
    tick_camera,
    flush_fx_lists,
    tick_blink_counter,
    noop,
    advance_event_index,
    always_true,
    dispatch_debug_hotkeys,
    update_talk_scroll,
    collect_visible_units,
    update_order_panel
};

class Host {
  public:

    virtual ~Host() = default;
    /// Reads the game clock.
    ///
    /// @param state loop state
    /// @return the clock in its own units (scaled_clock converts milliseconds to them)
    virtual uint32_t current_tick(State& state) = 0;
    /// Runs one argument-free subsystem call of the loop.
    ///
    /// @param which the call to run
    /// @param[in,out] state loop state; the call may change it before later branches
    virtual void step(Step which, State& state) = 0;
    /// Starts this frame's profiling sample window.
    ///
    /// @param state loop state
    virtual void begin_sample_window(State& state) = 0;
    /// Adds the time since the last mark to a profiling bucket.
    ///
    /// @param state loop state
    /// @param bucket profiling bucket 0..8
    virtual void accumulate_timing(State& state, uint8_t bucket) = 0;
    /// Shares the local player's resources with allies after a live tick.
    ///
    /// @param state loop state
    /// @param player local player index
    virtual void share_resources(State& state, uint8_t player) = 0;
    /// Sends the queued simulation messages to every other player.
    ///
    /// @param state loop state
    /// @param force_flush ? nonzero sends even when a channel's send timer has not
    ///        yet come due; the loop passes 0
    virtual void send_all_channels(State& state, int32_t force_flush) = 0;
    /// Expires timed area coverage after a batch of ticks.
    ///
    /// @param state loop state
    /// @param force true from the loop
    /// @quirk 3.1c always forces the expiry after a batch of ticks, so the loop
    ///        always passes true.
    virtual void expire_area_coverage(State& state, bool force) = 0;
    /// Carries out a deferred player slot ordering (deferred_flags bit 2).
    ///
    /// @param state loop state
    /// @return true when done, which clears the deferred bit
    virtual bool assign_slot_order(State& state) = 0;
    /// Returns the identifier of the primary player slot for the pause notice.
    ///
    /// @param state loop state
    /// @return slot identifier passed on to dispatch_message
    virtual int32_t primary_slot_player_id(State& state) = 0;
    /// Dispatches the notice a paused live game sends every 60 clock units.
    ///
    /// @param state loop state
    /// @param sender_id ? the player the notice comes from; the loop passes the
    ///        primary slot's identifier
    /// @param target_id ? the player the notice goes to, 0 for every player; the
    ///        loop passes 0
    virtual void dispatch_message(State& state, int32_t sender_id, int32_t target_id) = 0;
    /// Draws the in-game HUD overlay.
    ///
    /// @param state loop state
    /// @param show_extended_hud ? nonzero draws the extended overlay; the loop passes 1
    /// @param present_frame ? nonzero presents the frame once drawn; the loop passes 1
    virtual void
    draw_hud_overlay(State& state, int32_t show_extended_hud, int32_t present_frame) = 0;
    /// Saves the current frame for movie capture.
    ///
    /// @param state loop state
    /// @param path capture directory from State::capture_path
    /// @param prefix file name prefix, "FRAM"
    virtual void capture_frame(State& state, std::string_view path, std::string_view prefix) = 0;
};

/// What stopped a step of the loop; none when it ran to the end.
enum class LoopError : uint8_t {
    none,                      ///< the step ran to the end
    nonfinite_remainder,       ///< Timing::remainder is an infinity or a NaN
    unsupported_precision,     ///< Timing::precision is neither 53 nor 64 significand bits
    accumulation_out_of_range, ///< the accumulated steps leave the signed 64-bit range
    negative_pending_steps,    ///< Timing::pending_steps is below zero
    unterminated_capture_path, ///< State::capture_path holds no terminating NUL
    zero_capture_rate,         ///< State::capture_rate is zero while capturing
};

/// Says what a loop error means, for messages.
///
/// @param error the error
/// @return static text; "none" for LoopError::none
[[nodiscard]] const char* loop_error_text(LoopError error) noexcept;

/// Converts elapsed clock units into pending simulation steps and adapts the actual rate.
///
/// Scales the clock delta by a tenth of the actual rate, slowed when another
/// player's simulation lags 900 ticks or more, adds the carried fraction, and keeps
/// the whole part as pending steps: none while paused, at most five. Too many
/// overloaded frames lower the actual rate by one; enough spare frames raise it back
/// towards the requested rate.
///
/// A non-finite remainder stops it before it changes anything. An unsupported
/// precision stops it once it has read the clock and set the rate flag, and
/// accumulated steps outside the signed 64-bit range once it has also measured
/// the lag; both leave the pending steps, the remainder and the rate as they were.
///
/// @param[in,out] timing clock, rate, lag and pending-step state
/// @param now current clock, in current_tick units rather than milliseconds
/// @return none; nonfinite_remainder, unsupported_precision or
///         accumulation_out_of_range when stopped
/// @quirk The expression runs with the significand Timing::precision sets; the whole part is
///        truncated through 64 bits and only its low 32 bits kept.
[[nodiscard]] LoopError update_timing(Timing& timing, uint32_t now) noexcept;

/// The lag guard of a network game (network.lag-guard): while no remote player
/// has been heard for a while, the game steps at most once per period.
struct LagGuard {
    uint32_t last_step_ms{}; ///< wall clock of the last step let through while closed
    bool closed{};           ///< the remote players have been silent for the period
    uint32_t closed_at_ms{}; ///< wall clock the guard closed at
};

/// What lag_guard_step decided for one step.
enum class LagGuardStep : uint8_t {
    run,     ///< the step runs
    held,    ///< the step is held: the guard is closed and stepped within the period
    closing, ///< the guard closes now; the step runs, the first of the gap
    opening, ///< the guard opens again now; the step runs
};

/// Decides whether one simulation step runs under the lag guard.
///
/// The guard closes once the remote players have been silent for gap_ms and
/// opens as soon as one is heard again. While closed, a step runs once
/// gap_ms have passed since the last one it let through.
///
/// @param[in,out] guard the guard
/// @param gap_ms the period in milliseconds; 0 turns the guard off
/// @param now_ms wall clock in milliseconds
/// @param silent_ms milliseconds since any remote player was last heard
/// @return what happens to the step
[[nodiscard]] LagGuardStep
lag_guard_step(LagGuard& guard, uint32_t gap_ms, uint32_t now_ms, uint32_t silent_ms) noexcept;
/// Runs the pending simulation steps through the fixed per-tick subsystem order.
///
/// Each step advances the tick counter and runs, in order: player commands (live
/// only), unit movement, projectiles, explosion pieces, players, features, spans,
/// random events, meteor strikes, the camera, the effect lists and the blink
/// counter, then shares resources and sends queued messages when live with a
/// multiplayer link. A fixed trailer follows every batch, even an empty one.
///
/// A negative pending count stops it before any call, instead of running
/// billions of wrapped iterations.
///
/// @param[in,out] state loop state; the pending count is read once and not consumed
/// @param host subsystem calls
/// @param live true when multiplayer commands are processed each tick
/// @return none; negative_pending_steps when stopped
[[nodiscard]] LoopError run_ticks(State& state, Host& host, bool live);
/// Runs one frame of the running-game mode: timing, ticks, debug hotkeys and presentation.
///
/// An error from the timing or the ticks stops the frame where it arises.
/// While capturing, an unterminated capture path stops it before the frame is
/// saved, and a zero capture rate once it has been saved.
///
/// @param[in,out] state loop state
/// @param host subsystem calls
/// @return none; the error of update_timing or run_ticks,
///         unterminated_capture_path or zero_capture_rate when stopped
[[nodiscard]] LoopError run_frame(State& state, Host& host);
/// Normal game speed, the value a multiplayer session restarts at.
inline constexpr uint16_t normal_game_speed = 10;

/// Resets the timing as a session or match starts.
///
/// @param[in,out] timing its tick remainder restarts at zero
/// @param multiplayer true to also set the requested and actual speed to the normal speed
void reset_mode_timing(Timing& timing, bool multiplayer) noexcept;

/// Converts milliseconds to game clock units.
///
/// @param milliseconds duration in milliseconds
/// @param scale clock units per second
/// @return `milliseconds * scale / 1000`
/// @quirk The product wraps at 32 bits before the division.
uint32_t scaled_clock(uint32_t milliseconds, uint32_t scale) noexcept;

/// The largest reading scaled_clock gives. Its product keeps 32 bits, so a
/// clock read through it runs from 0 through this many units and then turns
/// over to 0: about every 39.8 hours at 30 units a second, and again
/// whenever the milliseconds themselves turn over at 2^32.
inline constexpr uint32_t scaled_clock_turn = 4'294'967;

/// Tells whether a scaled_clock reading lies before another.
///
/// A reading lies before another when it is the smaller by less than half
/// a turn of the clock; one smaller by more has turned over to 0 since the
/// other was read, and lies after it, as does a larger reading however far.
/// The other may be a reading plus a wait, past scaled_clock_turn.
///
/// @param reading the reading
/// @param other the reading, or reading plus a wait, it is compared with
/// @return true when `reading` lies before `other`
[[nodiscard]] bool scaled_clock_before(uint32_t reading, uint32_t other) noexcept;

/// Returns the clock units from an earlier scaled_clock reading to a later one.
///
/// A later reading below the earlier one was read after the clock turned
/// over to 0. Readings a whole turn or more apart count less than a turn.
///
/// @param later the later reading
/// @param earlier the earlier reading, at most scaled_clock_turn
/// @return the units between them, below scaled_clock_turn
[[nodiscard]] uint32_t scaled_clock_elapsed(uint32_t later, uint32_t earlier) noexcept;

// Per-mode tick handlers set_mode installs; the main loop runs them.
enum class ModeCallback : uint32_t {
    enter_frontend = 1,
    reset_to_frontend,
    frontend,
    idle_stats,
    skirmish_setup,
    game_load,
    game,
    end_game
};
// Close-request handlers set_mode installs as the idle callback.
enum class ExitHandler : uint32_t { leave_game = 1, confirm_exit, exit_to_windows_prompt };

struct ModeState {
    int32_t mode{};                       // Game.mode
    std::optional<ModeCallback> callback; // Game.mode_callback
};

// The state of a screen package's window that set_mode reads.
struct ModeEnvironment {
    bool present{}; // the window exists
    uint8_t enabled{};
    uint8_t resource_open{}; // ? cleared when the frontend resource shuts down
    // Nonzero from the launch of a battle through the window until the
    // return to the main menu.
    uint8_t game_launched{};
};

class ModeHost {
  public:

    virtual ~ModeHost() = default;
    /// Installs the handler the window system calls on a close request.
    ///
    /// @param handler close-request handler
    /// @param handler_context ? value handed back to the handler when it runs;
    ///        set_mode passes 0
    virtual void set_idle_callback(ExitHandler handler, int32_t handler_context) = 0;
};

/// Selects the application mode's tick handler and installs the matching close handler.
///
/// The mode is stored before the handler is installed. The exit-to-Windows prompt
/// is used while the screen package's window is present, enabled and has its
/// resource open, and no battle it launched is running; otherwise the game mode
/// (6) asks to confirm and every other mode leaves the game.
///
/// @param[in,out] state stored mode and tick handler; modes outside 0..7 get none
/// @param mode application mode 0..7
/// @param environment state of the screen package's window
/// @param host installs the close handler
void set_mode(ModeState& state, int32_t mode, const ModeEnvironment& environment, ModeHost& host);
} // namespace oa::base::game_loop
