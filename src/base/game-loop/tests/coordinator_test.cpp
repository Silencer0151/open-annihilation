// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/base/game_loop.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
using namespace oa::base::game_loop;

namespace {
// Markers the trace records for host callbacks other than Step calls, which
// record their own enumerator value.
enum Event : uint32_t {
    clock_read = 0x1000,
    sample_window,
    share,
    send_all,
    expire_coverage,
    slot_order,
    slot_id,
    notice,
    hud_overlay,
    frame_capture
};

constexpr uint32_t call(Step step) {
    return static_cast<uint32_t>(step);
}

void require(bool yes, const char* message) {
    if (!yes)
        throw std::runtime_error(message);
}

struct Trace final : Host, ModeHost {
    std::vector<uint32_t> events;
    std::vector<uint32_t> clocks{0};
    std::size_t clock_index{};
    std::vector<uint32_t> observed_ticks;
    ExitHandler input{};
    ModeState* mode_observer{};
    bool mutate_pending{}, clear_multiplayer{}, deferred_result{};

    uint32_t current_tick(State&) override {
        events.push_back(clock_read);
        require(clock_index < clocks.size(), "unexpected clock call");
        return clocks[clock_index++];
    }

    void step(Step value, State& s) override {
        const auto v = static_cast<uint32_t>(value);
        events.push_back(v);
        if (value == Step::tick_unit_movement) {
            observed_ticks.push_back(s.timing.tick);
            if (mutate_pending)
                s.timing.pending_steps = 0;
            if (clear_multiplayer)
                s.multiplayer_active = false;
        }
    }

    void begin_sample_window(State&) override { events.push_back(sample_window); }

    void accumulate_timing(State&, uint8_t n) override { events.push_back(0xf0000000u + n); }

    void share_resources(State&, uint8_t p) override {
        require(p == 9, "local player identity");
        events.push_back(share);
    }

    void send_all_channels(State&, int32_t force_flush) override {
        require(force_flush == 0, "send without forcing a flush");
        events.push_back(send_all);
    }

    void expire_area_coverage(State&, bool force) override {
        require(force, "coverage expiry is forced");
        events.push_back(expire_coverage);
    }

    bool assign_slot_order(State&) override {
        events.push_back(slot_order);
        return deferred_result;
    }

    int32_t primary_slot_player_id(State&) override {
        events.push_back(slot_id);
        return -27;
    }

    void dispatch_message(State&, int32_t sender_id, int32_t target_id) override {
        require(sender_id == -27 && target_id == 0, "notification arguments");
        events.push_back(notice);
    }

    void draw_hud_overlay(State&, int32_t show_extended_hud, int32_t present_frame) override {
        require(show_extended_hud == 1 && present_frame == 1, "present arguments");
        events.push_back(hud_overlay);
    }

    void capture_frame(State&, std::string_view path, std::string_view prefix) override {
        require(path == "movie" && prefix == "FRAM", "capture identity");
        events.push_back(frame_capture);
    }

    void set_idle_callback(ExitHandler cb, int32_t handler_context) override {
        require(handler_context == 0, "close handler installed with context 0");
        require(mode_observer != nullptr, "mode observer");
        require(mode_observer->mode != 1234567, "mode must be stored before install");
        input = cb;
    }
};

Timing timing() {
    Timing t;
    t.actual_rate = 10;
    t.requested_rate = 10;
    return t;
}

void test_timing() {
    auto t = timing();
    t.remainder = 0.25f;
    update_timing(t, 2);
    require(
        t.pending_steps == 2 && t.remainder == 0.25f && t.adaptation == -1,
        "fractional accumulation"
    );
    t = timing();
    t.previous_clock = 0xfffffffau;
    update_timing(t, 4);
    require(t.elapsed_bits == 10 && t.pending_steps == 5, "clock wraps before signed conversion");
    t = timing();
    t.previous_clock = 1;
    update_timing(t, 0);
    require(
        t.elapsed_bits == 0xffffffffu && t.pending_steps == 0, "negative clock delta clamps count"
    );
    t = timing();
    t.flags = 1;
    t.adaptation = 7;
    t.remainder = 0.25f;
    update_timing(t, 1);
    require(
        t.pending_steps == 0 && t.adaptation == 7 && t.remainder == 0.25f,
        "pause still updates fraction but skips adaptation"
    );
    t = timing();
    t.adaptation = 9;
    update_timing(t, 20);
    require(
        t.pending_steps == 5 && t.actual_rate == 10 && t.adaptation == 10,
        "overload threshold inclusive10"
    );
    update_timing(t, 40);
    require(
        t.actual_rate == 9 && t.adaptation == 0 && (t.flags & 4) == 0,
        "overload11 lowers rate, flag uses old rate"
    );
    t = timing();
    t.actual_rate = 9;
    t.adaptation = -99;
    update_timing(t, 0);
    require(
        t.adaptation == -100 && t.actual_rate == 9 && (t.flags & 4) != 0,
        "spare threshold inclusive-100"
    );
    update_timing(t, 0);
    require(t.adaptation == 0 && t.actual_rate == 10, "spare-101 restores rate");
    t = timing();
    t.adaptation = 32767;
    update_timing(t, 20);
    require(t.adaptation == -32768 && t.actual_rate == 10, "signed16 increment wraps");
    t = timing();
    t.tick = 5000;
    t.players[0] = {true, 2, 1, 0};
    t.players[1] = {true, 3, 0, 0};
    t.players[2] = {true, 3, 0x8000, 4000};
    t.players[3] = {true, 3, 1, 4000};
    update_timing(t, 1);
    require(
        t.slowest_player == 2 && t.lag_bits == 1000 && (t.flags & 2) != 0,
        "player eligibility unsigned, first tie wins"
    );
    require(
        t.pending_steps == 0 && t.remainder > 0.962f && t.remainder < 0.964f, "lag slowdown factor"
    );
    t = timing();
    t.tick = 5000;
    t.players[9] = {true, 3, 1, 0};
    update_timing(t, 100);
    require(t.lag_bits == 5000 && t.pending_steps == 1, "lag floor and clamp");
    t = timing();
    t.flags = 2;
    t.tick = 1000;
    t.players[0] = {true, 3, 1, 101};
    update_timing(t, 1);
    require((t.flags & 2) == 0, "lag899 clears throttle flag");
    t = timing();
    t.remainder = std::numeric_limits<float>::quiet_NaN();
    bool rejected = false;
    try {
        update_timing(t, 1);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected, "corrupt remainder rejected");
    require(scaled_clock(0xffffffffu, 1000) == 4294966u, "clock product wraps before divide");
}

void test_ticks() {
    State s;
    s.timing.pending_steps = 1;
    s.multiplayer_active = true;
    s.local_player_index = 9;
    Trace h;
    run_ticks(s, h, true);
    const std::vector<uint32_t> want = {
        call(Step::process_player_commands),
        0xf0000000,
        call(Step::tick_unit_movement),
        0xf0000001,
        call(Step::tick_projectiles),
        0xf0000007,
        call(Step::tick_explosion_pieces),
        0xf0000008,
        call(Step::tick_players),
        0xf0000002,
        call(Step::tick_features),
        call(Step::step_spans),
        call(Step::schedule_random_event),
        call(Step::step_meteor_strike),
        call(Step::tick_camera),
        0xf0000008,
        call(Step::flush_fx_lists),
        0xf0000006,
        call(Step::tick_blink_counter),
        0xf0000008,
        share,
        send_all,
        0xf0000000,
        call(Step::noop),
        call(Step::noop),
        call(Step::noop),
        call(Step::advance_event_index),
        expire_coverage,
        0xf0000008
    };
    require(
        h.events == want && s.timing.tick == 1 && s.timing.pending_steps == 1,
        "live tick exact call ordering and unconsumed count"
    );
    s = {};
    s.timing.pending_steps = 2;
    s.timing.tick = 0xffffffffu;
    Trace mutation;
    mutation.mutate_pending = true;
    run_ticks(s, mutation, false);
    require(mutation.observed_ticks == std::vector<uint32_t>{0, 1}, "count snapshot and tick wrap");
    s = {};
    Trace zero;
    run_ticks(s, zero, false);
    require(
        zero.events == std::vector<uint32_t>(
                           {call(Step::noop),
                            call(Step::noop),
                            call(Step::noop),
                            call(Step::advance_event_index),
                            expire_coverage,
                            0xf0000008}
                       ),
        "zero count still runs trailer"
    );
    s = {};
    s.timing.pending_steps = 1;
    s.multiplayer_active = true;
    Trace clear;
    clear.clear_multiplayer = true;
    run_ticks(s, clear, true);
    require(
        std::find(clear.events.begin(), clear.events.end(), share) == clear.events.end(),
        "multiplayer flag read after callbacks"
    );
}

void test_frames() {
    State s;
    s.session_flags = 1;
    s.timing = timing();
    s.timing.flags = 1;
    s.multiplayer_active = true;
    s.paused_deadline = 50;
    Trace h;
    h.clocks = {100, 101, 102};
    run_frame(s, h);
    require(s.paused_deadline == 162 && h.clock_index == 3, "paused deadline samples clock twice");
    const std::vector<uint32_t> paused = {
        sample_window,
        clock_read,
        send_all,
        call(Step::process_player_commands),
        clock_read,
        clock_read,
        slot_id,
        notice,
        0xf0000000,
        call(Step::dispatch_debug_hotkeys),
        call(Step::update_talk_scroll),
        call(Step::collect_visible_units),
        hud_overlay,
        0xf0000008,
        call(Step::noop),
        call(Step::noop)
    };
    require(h.events == paused, "paused branch call ordering");
    s = {};
    s.frame_flags = 0x11;
    s.panel_unit_id = 19;
    Trace skip;
    run_frame(s, skip);
    require(
        skip.clock_index == 0 && s.panel_unit_id == 0 && s.frame_flags == 0x11,
        "nonlive suspended path skips timing and clears field"
    );
    s = {};
    s.frame_flags = 0x10;
    s.timing.flags = 1;
    Trace resume;
    run_frame(s, resume);
    require(
        s.frame_flags == 0 && resume.events[3] == call(Step::collect_visible_units) &&
            resume.events[4] == call(Step::update_order_panel),
        "resume transition clears flag before callback"
    );
    s = {};
    s.timing.flags = 1;
    s.capture_enabled = 1;
    s.capture_rate = 7;
    s.next_capture_tick = 10;
    s.timing.tick = 10;
    const std::string name = "movie";
    std::copy(name.begin(), name.end(), s.capture_path.begin());
    Trace capture;
    capture.clocks = {123};
    run_frame(s, capture);
    require(
        s.next_capture_tick == 14 && s.timing.previous_clock == 123,
        "capture uses integer30/rate and resets clock"
    );
    s = {};
    s.session_flags = 1;
    s.timing = timing();
    s.deferred_flags = 0x14;
    Trace live;
    live.clocks = {1};
    live.deferred_result = true;
    run_frame(s, live);
    require(
        s.timing.tick == 1 && s.deferred_flags == 0x10, "live deferred bit cleared on success only"
    );
}

void test_modes() {
    ModeState s;
    ModeEnvironment env;
    Trace h;
    h.mode_observer = &s;
    const std::array<ModeCallback, 8> target = {
        ModeCallback::enter_frontend,
        ModeCallback::reset_to_frontend,
        ModeCallback::frontend,
        ModeCallback::idle_stats,
        ModeCallback::skirmish_setup,
        ModeCallback::game_load,
        ModeCallback::game,
        ModeCallback::end_game
    };
    for (int32_t i = 0; i < 8; ++i) {
        s.mode = 1234567;
        set_mode(s, i, env, h);
        require(
            s.callback && *s.callback == target[static_cast<std::size_t>(i)],
            "mode pointer identity"
        );
        require(
            h.input == (i == 6 ? ExitHandler::confirm_exit : ExitHandler::leave_game),
            "leave_game input selection"
        );
    }
    set_mode(s, -1, env, h);
    require(!s.callback && s.mode == -1, "negative mode preserves value, null callback");
    set_mode(s, 8, env, h);
    require(!s.callback, "out of range mode");
    env = {true, 1, 2, 0};
    set_mode(s, 6, env, h);
    require(
        h.input == ExitHandler::exit_to_windows_prompt,
        "exit_to_windows_prompt overrides confirm_exit"
    );
    env.game_launched = 1;
    set_mode(s, 6, env, h);
    require(
        h.input == ExitHandler::confirm_exit,
        "exit_to_windows_prompt disabled while a launched battle runs"
    );
}
} // namespace

// A multiplayer session restarts the requested and actual speed at the normal
// speed, 10; every session clears the tick remainder.
void test_mode_timing_reset() {
    Timing t{};
    t.requested_rate = 17;
    t.actual_rate = 15;
    t.remainder = 0.5f;
    reset_mode_timing(t, false);
    require(
        t.requested_rate == 17 && t.actual_rate == 15 && t.remainder == 0.0f,
        "single player keeps speeds"
    );
    t.remainder = 0.75f;
    reset_mode_timing(t, true);
    require(
        t.requested_rate == 10 && t.actual_rate == 10 && t.remainder == 0.0f,
        "multiplayer normal speed"
    );
}

int main() {
    try {
        test_mode_timing_reset();
        test_timing();
        test_ticks();
        test_frames();
        test_modes();
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "game-loop coordinator cases passed\n";
}
