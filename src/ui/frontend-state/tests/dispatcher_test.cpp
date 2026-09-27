// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/ui/frontend_state/app_modes.hpp"
#include "oa/ui/frontend_state/dispatcher.hpp"
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <map>
#include <string>
#include <vector>
using namespace oa::ui::frontend_state;

namespace {
void require(bool value, const char* message) {
    if (!value) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

// Labels the fake host records for its own calls; distinct from Step and Query.
enum class HostCall : uint32_t {
    open_new_game_panel = 0x304,
    play_movie = 0x305,
    select_map_list = 0x307,
    set_app_mode = 0x308,
    set_cursor = 0x309,
    set_cursor_visible = 0x30a,
    set_endgame_state = 0x30b,
    shut_down = 0x30e,
};

constexpr uint32_t label(Step step) {
    return static_cast<uint32_t>(step);
}

constexpr uint32_t label(Query query) {
    return static_cast<uint32_t>(query);
}

constexpr uint32_t label(HostCall call) {
    return static_cast<uint32_t>(call);
}

struct Event {
    uint32_t call;
    int32_t a{}, b{};
    uint8_t state{}, signal{};
};

struct RecordingHost final : Host {
    std::vector<Event> events;
    std::vector<std::string> resources;
    std::map<uint32_t, uint32_t> results;
    std::function<void(uint32_t, State&)> mutate;

    /// Records one host call and runs the mutation hook on it.
    ///
    /// @param call Step, Query or HostCall label of the call.
    /// @param[in,out] s Dispatcher state, which the hook may change.
    /// @param a First scalar argument.
    /// @param b Second scalar argument.
    void record(uint32_t call, State& s, int32_t a = 0, int32_t b = 0) {
        events.push_back({call, a, b, s.state, s.signal});
        if (mutate)
            mutate(call, s);
    }

    void step(Step f, State& s) override { record(static_cast<uint32_t>(f), s); }

    uint32_t query(Query f, State& s) override {
        const auto call = static_cast<uint32_t>(f);
        record(call, s);
        return results[call];
    }

    void play_movie(State& s, std::string_view resource) override {
        resources.emplace_back(resource);
        record(label(HostCall::play_movie), s);
    }

#define ONE(name, call)                                                                            \
    void name(State& s, int32_t a) override {                                                      \
        record(call, s, a);                                                                        \
    }
    ONE(set_cursor_visible, label(HostCall::set_cursor_visible))
    ONE(select_map_list, label(HostCall::select_map_list))
    ONE(open_new_game_panel, label(HostCall::open_new_game_panel))
    ONE(set_app_mode, label(HostCall::set_app_mode))
    ONE(set_cursor, label(HostCall::set_cursor))
    ONE(set_endgame_state, label(HostCall::set_endgame_state))
#undef ONE

    void shut_down(State& s) override { record(label(HostCall::shut_down), s); }
};

// An extension that runs one state itself: it pumps a frame there and names
// `mode` as that state's handover.
struct OwnState {
    uint8_t state{};
    int32_t mode{};
    std::vector<uint8_t> offered; // signals of the states offered, in order
};

StateHandler own_state(OwnState& own) {
    StateHandler handler;
    handler.context = &own;
    handler.run = [](void* context, State& s, Host& h) {
        auto& o = *static_cast<OwnState*>(context);
        o.offered.push_back(s.signal);
        if (s.state != o.state)
            return false;
        h.step(Step::present_frame, s);
        return true;
    };
    handler.handover_mode = [](void* context, const State& s) {
        const auto& o = *static_cast<const OwnState*>(context);
        return s.state == o.state ? o.mode : 0;
    };
    return handler;
}

State at(uint8_t state, uint8_t signal) {
    State s;
    s.state = state;
    s.signal = s.pending_signal = signal;
    return s;
}

/// Checks the labels of the host calls recorded so far, in order.
///
/// @param h Recording host.
/// @param expected Labels the calls must have.
void trace(const RecordingHost& h, std::initializer_list<uint32_t> expected) {
    std::vector<uint32_t> actual;
    for (const auto& e : h.events)
        actual.push_back(e.call);
    require(actual == std::vector<uint32_t>(expected), "callback trace differs");
}

void pair(const State& s, uint8_t state, uint8_t signal) {
    require(
        s.state == state && s.signal == signal && s.pending_signal == signal,
        "state/signal pair differs"
    );
}
} // namespace

int main() {
    { // Initial intro route and its following2.zrb resource.
        auto s = at(0, 0);
        s.video_context_flags = 2;
        s.play_intro_movie = 1;
        RecordingHost h;
        dispatch(s, h);
        pair(s, 1, 0);
        require(s.play_intro_movie == 0, "intro latch cleared");
        trace(
            h,
            {label(Step::get_video_context),
             label(HostCall::set_cursor_visible),
             label(HostCall::play_movie),
             label(Step::check_state_checksum),
             label(Step::check_state_checksum),
             label(Step::save_preferences)}
        );
        dispatch(s, h);
        pair(s, 2, 0);
        require(h.resources == std::vector<std::string>{"1.zrb", "2.zrb"}, "intro resources");
    }
    for (auto skip : {0, 1}) {
        auto s = at(0, 0);
        s.video_context_flags = 2;
        s.skip_intro = skip;
        RecordingHost h;
        dispatch(s, h);
        pair(s, 2, 0);
        require(h.resources.size() == static_cast<std::size_t>(1 - skip), "intro skip flag");
    }
    { // A callback changes the display's mode flags before the intro reads them.
        auto s = at(0, 0);
        RecordingHost h;
        h.mutate = [](auto a, State& v) {
            if (a == label(HostCall::set_cursor_visible))
                v.video_context_flags = 2;
        };
        dispatch(s, h);
        require(h.resources.size() == 1, "display mode flags read after callback");
    }
    { // Main-menu signal5: single-player path; then loading selection11.
        auto s = at(2, 5);
        s.session_flags = 0x8000;
        RecordingHost h;
        dispatch(s, h);
        pair(s, 8, 0);
        require(s.session_flags == 0x8008, "single-player flag preserves high byte");
        trace(
            h,
            {label(Step::pop_input_event),
             label(Step::check_state_checksum),
             label(Step::check_state_checksum),
             label(Step::shut_down_resource)}
        );
        dispatch(s, h);
        pair(s, 8, 1);
        s.pending_signal = 11;
        dispatch(s, h);
        pair(s, 10, 0);
        require(
            h.events[h.events.size() - 3].call == label(HostCall::select_map_list) &&
                h.events[h.events.size() - 3].a == 2,
            "load selection2"
        );
    }
    { // Main-menu entry: the main-menu map list and panel, then the cursor.
        auto s = at(2, 0);
        RecordingHost h;
        dispatch(s, h);
        pair(s, 2, 0);
        trace(
            h,
            {label(Step::pop_input_event),
             label(HostCall::select_map_list),
             label(Step::setup_main_menu),
             label(Step::return_to_main_menu),
             label(HostCall::set_cursor_visible)}
        );
        require(h.events[1].a == 0 && h.events.back().a == 1, "main-menu list and visible cursor");
    }
    { // Without an extension, MULTI's signal leaves the main menu as it is.
        auto s = at(2, 6);
        s.session_flags = 0x8008;
        RecordingHost h;
        dispatch(s, h);
        pair(s, 2, 6);
        require(s.session_flags == 0x8008, "flags untouched");
        trace(h, {label(Step::pop_input_event)});
    }
    { // An extension's handler sees the applied pending signal first; declining leaves the state to the engine.
        OwnState own{16, 0, {}};
        const auto extension = own_state(own);
        auto s = at(16, 0);
        s.pending_signal = 1;
        RecordingHost h;
        dispatch(s, h, extension);
        pair(s, 16, 1);
        trace(h, {label(Step::check_state_checksum), label(Step::present_frame)});
        require(own.offered == std::vector<uint8_t>{1}, "handler offered the applied signal");
        s = at(2, 0);
        h.events.clear();
        dispatch(s, h, extension);
        trace(
            h,
            {label(Step::pop_input_event),
             label(HostCall::select_map_list),
             label(Step::setup_main_menu),
             label(Step::return_to_main_menu),
             label(HostCall::set_cursor_visible)}
        );
    }
    for (auto state : {3, 4, 5}) {
        auto s = at(static_cast<uint8_t>(state), 1);
        s.session_flags = 0xffff;
        RecordingHost h;
        dispatch(s, h);
        pair(s, 2, 0);
        std::vector<std::string> expected =
            state == 3 ? std::vector<std::string>{"5.zrb"}
                       : std::vector<std::string>{state == 4 ? "3.zrb" : "4.zrb", "5.zrb"};
        require(h.resources == expected, "exit resources");
        if (state != 3)
            require(
                s.session_flags == 0xfffb &&
                    h.events.back().call == label(HostCall::set_app_mode) && h.events.back().a == 2,
                "exit mode2"
            );
    }
    {
        auto s = at(2, 8);
        RecordingHost h;
        dispatch(s, h);
        trace(
            h,
            {label(Step::pop_input_event),
             label(Step::draw_current_frame),
             label(Step::shut_down_resource),
             label(HostCall::shut_down)}
        );
    }
    { // Signal synchronization captures the pending byte before integrity callback.
        auto s = at(7, 0);
        s.pending_signal = 1;
        RecordingHost h;
        h.mutate = [](auto a, State& v) {
            if (a == label(Step::check_state_checksum))
                v.pending_signal = 9;
        };
        dispatch(s, h);
        pair(s, 7, 1);
        trace(
            h,
            {label(Step::check_state_checksum),
             label(Step::pop_input_event),
             label(Step::present_frame)}
        );
    }
    for (auto state : {6, 16, 17, 18, 19, 20, 21, 22, 255}) {
        for (auto signal : {0, 1, 3, 254}) {
            auto s = at(static_cast<uint8_t>(state), static_cast<uint8_t>(signal));
            RecordingHost h;
            dispatch(s, h);
            pair(s, static_cast<uint8_t>(state), static_cast<uint8_t>(signal));
            require(h.events.empty(), "unknown/default state only synchronizes");
        }
    }
    for (auto signal : {10, 14}) {
        auto s = at(8, static_cast<uint8_t>(signal));
        RecordingHost h;
        dispatch(s, h);
        pair(s, 9, 1);
        trace(
            h,
            {label(Step::pop_input_event),
             label(HostCall::open_new_game_panel),
             label(Step::check_state_checksum),
             label(Step::check_state_checksum),
             label(Step::check_state_checksum)}
        );
    }
    for (auto signal : {15, 16}) {
        auto s = at(9, static_cast<uint8_t>(signal));
        RecordingHost h;
        dispatch(s, h);
        pair(s, signal == 15 ? 12 : 13, 0);
        require(h.events[1].a == 1, "state9 selection1");
    }
    for (auto state : {12, 13}) {
        auto s = at(static_cast<uint8_t>(state), 3);
        RecordingHost h;
        dispatch(s, h);
        pair(s, 9, 1);
        require(h.events[1].a == state - 12, "state12/13 resource branch");
    }
    {
        auto s = at(14, 3);
        RecordingHost h;
        dispatch(s, h);
        trace(
            h,
            {label(Step::pop_input_event),
             label(Step::get_cursor_context),
             label(Step::enter_end_mission),
             label(HostCall::set_app_mode),
             label(HostCall::set_endgame_state),
             label(Step::draw_cursor)}
        );
        require(h.events[3].a == 7 && h.events[4].a == 7, "mode7 route");
    }
    {
        auto s = at(15, 3);
        RecordingHost h;
        dispatch(s, h);
        pair(s, 8, 0);
        require(
            h.events[1].call == label(HostCall::set_app_mode) && h.events[1].a == 2, "mode2 route"
        );
    }
    {
        auto s = at(11, 3);
        RecordingHost h;
        dispatch(s, h);
        pair(s, 8, 0);
        trace(h, {label(Step::check_state_checksum), label(Step::check_state_checksum)});
    }
    { // State reset helpers.
        auto s = at(9, 4);
        RecordingHost h;
        set_frontend_signal(s, h, 11);
        pair(s, 9, 11);
        trace(h, {label(Step::check_state_checksum)});
        RecordingHost g;
        reset_to_main_menu(s, g);
        pair(s, 2, 0);
        trace(
            g,
            {label(Step::check_state_checksum),
             label(Step::check_state_checksum),
             label(Step::load_default_palette)}
        );
    }
    { // Mode 0 leaves a game for the frontend.
        auto s = at(18, 1);
        s.session_flags = 0x0f;
        s.outcome_flags = 0xffff;
        RecordingHost h;
        enter_frontend_mode(s, h);
        require(s.session_flags == 0x02 && s.outcome_flags == 0xffeb, "mode 0 flags");
        trace(
            h,
            {label(HostCall::set_cursor),
             label(Step::draw_cursor),
             label(HostCall::select_map_list),
             label(Step::reset_key_queue),
             label(HostCall::set_app_mode)}
        );
        require(h.events.front().a == 0x13 && h.events.back().a == 2, "mode 0 arguments");
        pair(s, 18, 1);
    }
    { // Mode 1 also returns the dispatcher to the main menu.
        auto s = at(18, 1);
        s.session_flags = 0x0f;
        RecordingHost h;
        reset_to_frontend_mode(s, h);
        pair(s, 2, 0);
        trace(
            h,
            {label(Step::check_state_checksum),
             label(Step::check_state_checksum),
             label(Step::load_default_palette),
             label(HostCall::select_map_list),
             label(Step::reset_key_queue),
             label(Step::enable_panel_keyboard),
             label(HostCall::set_app_mode)}
        );
    }
    { // Mode 2 hands a loaded skirmish to mode 4.
        auto s = at(7, 0);
        s.session_flags = flags::loading;
        RecordingHost h;
        h.results[label(Query::map_list_object_state)] = 1;
        tick_frontend_mode(s, h);
        std::vector<uint32_t> modes;
        for (const auto& e : h.events)
            if (e.call == label(HostCall::set_app_mode))
                modes.push_back(static_cast<uint32_t>(e.a));
        require(modes == std::vector<uint32_t>{4}, "mode 2 -> 4");
        require(
            h.events.front().call == label(Step::reload_unit_overrides),
            "unit overrides reload first"
        );
        require(h.events.back().call == label(Step::draw_cursor), "cursor drawn last");
    }
    { // Mode 2 stays while the frontend is idle, and moves to the mode an extension's state hands over to.
        auto s = at(7, 0);
        RecordingHost h;
        tick_frontend_mode(s, h);
        for (const auto& e : h.events)
            require(e.call != label(HostCall::set_app_mode), "idle frontend keeps mode 2");
        OwnState own{16, 3, {}};
        auto t = at(16, 1);
        RecordingHost g;
        tick_frontend_mode(t, g, own_state(own));
        trace(
            g,
            {label(Step::reload_unit_overrides),
             label(Step::present_frame),
             label(Step::get_cursor_context),
             label(HostCall::set_app_mode),
             label(Step::get_cursor_context),
             label(Step::draw_panel_gadgets),
             label(Step::draw_cursor)}
        );
        require(g.events[3].a == 3, "extension state -> its handover mode");
        auto u = at(7, 0);
        u.session_flags = flags::loading;
        RecordingHost k;
        k.results[label(Query::map_list_object_state)] = 1;
        OwnState other{7, 3, {}};
        tick_frontend_mode(u, k, own_state(other));
        std::vector<uint32_t> modes;
        for (const auto& e : k.events)
            if (e.call == label(HostCall::set_app_mode))
                modes.push_back(static_cast<uint32_t>(e.a));
        require(modes == std::vector<uint32_t>{4}, "a ready map list comes before the handover");
    }
    std::cout << "frontend dispatcher trace cases passed\n";
}
