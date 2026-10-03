// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/base/game_loop.hpp"
#include "extended.hpp"
#include <algorithm>
#include <bit>
#include <cmath>

namespace oa::base::game_loop {
namespace {
constexpr uint16_t timing_paused = 1u;
constexpr uint16_t timing_player_lag = 2u;
constexpr uint16_t timing_below_requested_rate = 4u;
constexpr int32_t player_lag_slowdown_threshold = 900;
constexpr int32_t player_lag_limit = 3600;
constexpr int32_t player_lag_minimum_rate_threshold = 3573;
constexpr double player_lag_rate_scale = 0.00037037037037037035;
constexpr double minimum_player_rate_multiplier = 0.01;
constexpr double requested_rate_scale = 0.1;
constexpr int32_t maximum_catchup_steps = 5;
constexpr int16_t overload_adaptation_threshold = 10;
constexpr int16_t recovery_adaptation_threshold = -100;
constexpr uint32_t milliseconds_per_second = 1000u;
// Game.session_flags bit: a live game, whose frames take the multiplayer path.
constexpr uint8_t session_live_game = 0x01u;
// Game.load_flags bit: the player slot ordering is still to be assigned; each
// live frame that runs ticks retries it until the host reports it done.
constexpr uint16_t load_deferred_slot_order = 0x04u;
// Game.frame_flags bit: the in-game options menu is open, which suspends a game
// that is not live.
constexpr uint16_t frame_options_open = 0x01u;
// Game.frame_flags bit: the order panel's build menu is to be redrawn.
constexpr uint16_t frame_redraw_build_menu = 0x10u;
// Game.frame_flags bits that hold the order panel closed.
constexpr uint16_t frame_panel_busy = 0x0865u;
// Game.gui_flags bits that hold the order panel closed.
constexpr uint8_t gui_panel_busy = 0xe0u;

int32_t signed32(uint32_t n) noexcept {
    return std::bit_cast<int32_t>(n);
}

int16_t adjust16(int16_t n, int increment) noexcept {
    return std::bit_cast<int16_t>(static_cast<uint16_t>(
        static_cast<unsigned>(std::bit_cast<uint16_t>(n)) + static_cast<unsigned>(increment)
    ));
}

void mark(State& s, Host& h, uint8_t n) {
    h.accumulate_timing(s, n);
}

void pump3(State& s, Host& h) {
    h.step(Step::noop, s);
    h.step(Step::noop, s);
    h.step(Step::noop, s);
}
} // namespace

uint32_t scaled_clock(uint32_t milliseconds, uint32_t scale) noexcept {
    return (milliseconds * scale) / milliseconds_per_second;
}

bool scaled_clock_before(uint32_t reading, uint32_t other) noexcept {
    // Units from the other to the reading, negative when the reading is the smaller.
    const int32_t lead = signed32(reading - other);
    constexpr auto half_turn = static_cast<int32_t>(scaled_clock_turn / 2);
    return lead < 0 && lead > -half_turn;
}

uint32_t scaled_clock_elapsed(uint32_t later, uint32_t earlier) noexcept {
    if (later >= earlier)
        return later - earlier;
    return later + (scaled_clock_turn - earlier);
}

const char* loop_error_text(LoopError error) noexcept {
    switch (error) {
    case LoopError::none:
        return "none";
    case LoopError::nonfinite_remainder:
        return "the clock's carried fraction is not finite";
    case LoopError::unsupported_precision:
        return "the clock's precision is neither 53 nor 64 significand bits";
    case LoopError::accumulation_out_of_range:
        return "the clock's accumulated steps leave the 64-bit range";
    case LoopError::negative_pending_steps:
        return "the clock's pending steps are below zero";
    case LoopError::unterminated_capture_path:
        return "the capture path is not terminated";
    case LoopError::zero_capture_rate:
        return "the capture rate is zero";
    }
    return "unknown loop error";
}

LoopError update_timing(Timing& s, uint32_t now) noexcept {
    if (!std::isfinite(s.remainder))
        return LoopError::nonfinite_remainder;
    s.elapsed_bits = now - s.previous_clock;
    s.previous_clock = now;
    s.flags = static_cast<uint16_t>(
        (s.flags & ~timing_below_requested_rate) |
        (s.actual_rate < s.requested_rate ? timing_below_requested_rate : 0u)
    );
    // The constants are binary64 values widened to the working precision.
    using detail::Extended;
    if (s.precision != Precision::significand_53 && s.precision != Precision::significand_64)
        return LoopError::unsupported_precision;
    const auto extended = [&](double value) {
        return Extended(value, static_cast<unsigned>(s.precision));
    };
    Extended rate = extended(s.actual_rate) * extended(requested_rate_scale);
    uint32_t minimum = s.tick;
    s.slowest_player.reset();
    for (uint8_t i = 0; i < s.players.size(); ++i) {
        const auto& p = s.players[i];
        if (p.present && p.status == 3 && p.eligible != 0 && signed32(p.tick) < signed32(minimum)) {
            minimum = p.tick;
            s.slowest_player = i;
        }
    }
    s.lag_bits = s.tick - minimum;
    const auto lag = signed32(s.lag_bits);
    if (lag < player_lag_slowdown_threshold) {
        s.flags = static_cast<uint16_t>(s.flags & ~timing_player_lag);
    } else {
        const auto limited = std::min(lag, player_lag_limit);
        Extended multiplier =
            extended(player_lag_limit - limited) * extended(player_lag_rate_scale);
        if (limited >= player_lag_minimum_rate_threshold)
            multiplier = extended(minimum_player_rate_multiplier);
        s.flags = static_cast<uint16_t>(s.flags | timing_player_lag);
        rate = multiplier * rate;
    }
    const Extended elapsed_product = extended(signed32(s.elapsed_bits)) * rate;
    // Rounded to a binary64, then floored (rounded toward negative infinity).
    const std::optional<double> rounded = (elapsed_product + extended(s.remainder)).to_double();
    if (!rounded)
        return LoopError::accumulation_out_of_range;
    const double accumulated = *rounded;
    const double whole = std::floor(accumulated);
    if (!std::isfinite(whole) || whole < -9223372036854775808.0 || whole >= 9223372036854775808.0)
        return LoopError::accumulation_out_of_range;
    // The truncation yields 64 bits and only the low 32 are kept; preserve that wrap.
    const auto integer = static_cast<int64_t>(whole);
    s.pending_steps = signed32(static_cast<uint32_t>(integer));
    // The fraction lies in [0, 1), always inside the binary32 range.
    s.remainder = (extended(accumulated) - extended(whole)).to_float().value_or(0.0f);
    if (s.pending_steps < 0)
        s.pending_steps = 0;
    if ((s.flags & timing_paused) != 0) {
        s.pending_steps = 0;
    } else if (s.pending_steps > maximum_catchup_steps) {
        s.pending_steps = maximum_catchup_steps;
        s.adaptation = adjust16(s.adaptation, 1);
        if (s.adaptation > overload_adaptation_threshold) {
            s.adaptation = 0;
            if (s.actual_rate > 1)
                --s.actual_rate;
        }
    } else {
        s.adaptation = adjust16(s.adaptation, -1);
        if (s.adaptation < recovery_adaptation_threshold) {
            s.adaptation = 0;
            if (s.actual_rate < s.requested_rate)
                ++s.actual_rate;
        }
    }
    return LoopError::none;
}

LagGuardStep
lag_guard_step(LagGuard& guard, uint32_t gap_ms, uint32_t now_ms, uint32_t silent_ms) noexcept {
    if (gap_ms == 0 || silent_ms < gap_ms) {
        if (!guard.closed)
            return LagGuardStep::run;
        guard.closed = false;
        return LagGuardStep::opening;
    }
    if (!guard.closed) {
        guard.closed = true;
        guard.closed_at_ms = now_ms;
        guard.last_step_ms = now_ms;
        return LagGuardStep::closing;
    }
    if (now_ms - guard.last_step_ms < gap_ms)
        return LagGuardStep::held;
    guard.last_step_ms = now_ms;
    return LagGuardStep::run;
}

LoopError run_ticks(State& s, Host& h, bool live) {
    const auto count = s.timing.pending_steps;
    if (count < 0)
        return LoopError::negative_pending_steps;
    for (int32_t i = 0; i < count; ++i) {
        ++s.timing.tick;
        if (live) {
            h.step(Step::process_player_commands, s);
            mark(s, h, 0);
        }
        h.step(Step::tick_unit_movement, s);
        mark(s, h, 1);
        h.step(Step::tick_projectiles, s);
        mark(s, h, 7);
        h.step(Step::tick_explosion_pieces, s);
        mark(s, h, 8);
        h.step(Step::tick_players, s);
        mark(s, h, 2);
        h.step(Step::tick_features, s);
        h.step(Step::step_spans, s);
        h.step(Step::schedule_random_event, s);
        h.step(Step::step_meteor_strike, s);
        h.step(Step::tick_camera, s);
        mark(s, h, 8);
        h.step(Step::flush_fx_lists, s);
        mark(s, h, 6);
        h.step(Step::tick_blink_counter, s);
        mark(s, h, 8);
        if (live && s.multiplayer_active) {
            h.share_resources(s, s.local_player_index);
            h.send_all_channels(s, 0);
            mark(s, h, 0);
        }
    }
    pump3(s, h);
    h.step(Step::advance_event_index, s);
    // 3.1c always forces the expiry here (see Host::expire_area_coverage).
    h.expire_area_coverage(s, true);
    mark(s, h, 8);
    return LoopError::none;
}

void reset_mode_timing(Timing& t, bool multiplayer) noexcept {
    if (multiplayer) {
        t.requested_rate = normal_game_speed;
        t.actual_rate = normal_game_speed;
    }
    t.remainder = 0.0f;
}

LoopError run_frame(State& s, Host& h) {
    h.begin_sample_window(s);
    bool skip_pre = false;
    if ((s.session_flags & session_live_game) != 0) {
        if (const auto error = update_timing(s.timing, h.current_tick(s)); error != LoopError::none)
            return error;
        if (s.timing.pending_steps != 0) {
            if (const auto error = run_ticks(s, h, true); error != LoopError::none)
                return error;
            mark(s, h, 8);
            h.step(Step::always_true, s);
            h.step(Step::always_true, s);
            h.step(Step::always_true, s);
            if ((s.deferred_flags & load_deferred_slot_order) != 0) {
                if (h.assign_slot_order(s))
                    s.deferred_flags =
                        static_cast<uint16_t>(s.deferred_flags & ~load_deferred_slot_order);
                mark(s, h, 0);
            }
            pump3(s, h);
        } else if ((s.timing.flags & timing_paused) != 0) {
            if (s.multiplayer_active)
                h.send_all_channels(s, 0);
            h.step(Step::process_player_commands, s);
            const auto now = h.current_tick(s);
            if (signed32(now) > signed32(s.paused_deadline)) {
                s.paused_deadline = h.current_tick(s) + 60u;
                const auto sender_id = h.primary_slot_player_id(s);
                h.dispatch_message(s, sender_id, 0);
            }
            mark(s, h, 0);
        }
    } else if ((s.frame_flags & frame_options_open) != 0) {
        skip_pre = true;
    } else if ((s.timing.flags & timing_paused) == 0) {
        if (const auto error = update_timing(s.timing, h.current_tick(s)); error != LoopError::none)
            return error;
        if (s.timing.pending_steps != 0) {
            if (const auto error = run_ticks(s, h, false); error != LoopError::none)
                return error;
            mark(s, h, 8);
        }
    }
    if (!skip_pre && (s.frame_flags & frame_options_open) == 0) {
        h.step(Step::dispatch_debug_hotkeys, s);
        h.step(Step::update_talk_scroll, s);
    }
    h.step(Step::collect_visible_units, s);
    const auto flags = s.frame_flags;
    if ((flags & frame_panel_busy) == 0 && (s.gui_flags & gui_panel_busy) == 0) {
        if ((flags & frame_redraw_build_menu) != 0) {
            s.frame_flags = static_cast<uint16_t>(flags & ~frame_redraw_build_menu);
            h.step(Step::update_order_panel, s);
        }
    } else if ((flags & frame_redraw_build_menu) != 0) {
        s.panel_unit_id = 0;
    }
    h.draw_hud_overlay(s, 1, 1);
    mark(s, h, 8);
    h.step(Step::noop, s);
    if (s.capture_enabled > 0 && s.next_capture_tick <= s.timing.tick) {
        const auto end = std::find(s.capture_path.begin(), s.capture_path.end(), '\0');
        if (end == s.capture_path.end())
            return LoopError::unterminated_capture_path;
        h.capture_frame(
            s,
            std::string_view(
                s.capture_path.data(), static_cast<std::size_t>(end - s.capture_path.begin())
            ),
            "FRAM"
        );
        if (s.capture_rate == 0)
            return LoopError::zero_capture_rate;
        s.next_capture_tick += static_cast<uint32_t>(30 / s.capture_rate);
        s.timing.previous_clock = h.current_tick(s);
    }
    h.step(Step::noop, s);
    return LoopError::none;
}

void set_mode(ModeState& s, int32_t mode, const ModeEnvironment& env, ModeHost& host) {
    static constexpr std::array targets{
        ModeCallback::enter_frontend,
        ModeCallback::reset_to_frontend,
        ModeCallback::frontend,
        ModeCallback::idle_stats,
        ModeCallback::skirmish_setup,
        ModeCallback::game_load,
        ModeCallback::game,
        ModeCallback::end_game
    };
    s.mode = mode;
    if (static_cast<uint32_t>(mode) < targets.size())
        s.callback = targets[static_cast<uint32_t>(mode)];
    else
        s.callback.reset();
    ExitHandler input;
    if (env.present && env.enabled != 0 && env.resource_open != 0 && env.game_launched == 0)
        input = ExitHandler::exit_to_windows_prompt;
    else
        input = mode == 6 ? ExitHandler::confirm_exit : ExitHandler::leave_game;
    host.set_idle_callback(input, 0);
}
} // namespace oa::base::game_loop
