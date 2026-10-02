// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The multiplayer frontend states run through the frontend dispatcher: the
// steps, queries and session routines each path runs, in order, its state and
// signal pairs, and the fields it changes.
#include "oa/netgame/frontend/multiplayer_states.hpp"

#include <cstdlib>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

using namespace oa::ui::frontend_state;
namespace mp = oa::netgame::frontend;

namespace {
void require(bool value, const char* message) {
    if (!value) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

// Labels the fake host records for its own calls; distinct from Step and Query.
enum class HostCall : uint32_t {
    add_player_slot = 0x300,
    handle_add_player_failure = 0x301,
    handle_game_outcome_event = 0x302,
    init_session_channels = 0x303,
    open_new_game_panel = 0x304,
    play_movie = 0x305,
    resolve_connection_info = 0x306,
    select_map_list = 0x307,
    set_app_mode = 0x308,
    set_cursor = 0x309,
    set_cursor_visible = 0x30a,
    set_endgame_state = 0x30b,
    set_guaranteed_delivery = 0x30c,
    set_player_slot_state = 0x30d,
    shut_down = 0x30e,
    start_player_session = 0x30f,
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
    uint32_t call{};
    int32_t a{}, b{};
    uint8_t state{}, signal{};
};

// Records the dispatcher Host's calls and the session routines' calls in one trace.
struct RecordingHost final : Host {
    State* state{};
    mp::MultiplayerFrontend frontend{};
    std::vector<Event> events;
    std::map<uint32_t, uint32_t> results;

    RecordingHost() {
        auto& host = frontend.host;
        host.context = this;
        host.resolve_connection_info = [](void* c, int32_t value) {
            return self(c).answer(HostCall::resolve_connection_info, value);
        };
        host.set_guaranteed_delivery = [](void* c, int32_t value) {
            self(c).note(label(HostCall::set_guaranteed_delivery), value);
        };
        host.handle_add_player_failure = [](void* c, uint8_t player, int32_t value) {
            return self(c).answer(HostCall::handle_add_player_failure, player, value);
        };
        host.add_player_slot = [](void* c, uint32_t id) {
            self(c).note(label(HostCall::add_player_slot), static_cast<int32_t>(id));
        };
        host.start_player_session = [](void* c, mp::Identifier id, uint8_t player) {
            require(
                id == self(c).frontend.state.session_guid, "session identifier passed by value"
            );
            return self(c).answer(HostCall::start_player_session, player);
        };
        host.set_player_slot_state = [](void* c, uint8_t index, int32_t value) {
            self(c).note(label(HostCall::set_player_slot_state), index, value);
        };
        host.init_session_channels = [](void* c, int32_t first, int32_t second) {
            self(c).note(label(HostCall::init_session_channels), first, second);
        };
        host.handle_game_outcome_event = [](void* c, int32_t first, int32_t second) {
            self(c).note(label(HostCall::handle_game_outcome_event), first, second);
        };
    }

    RecordingHost(const RecordingHost&) = delete;
    RecordingHost& operator=(const RecordingHost&) = delete;

    static RecordingHost& self(void* context) { return *static_cast<RecordingHost*>(context); }

    /// Appends a call, its two arguments and the dispatcher's state and signal to the trace.
    void record(uint32_t call, State& s, int32_t a = 0, int32_t b = 0) {
        events.push_back({call, a, b, s.state, s.signal});
    }

    /// Records a session routine's call against the state the host last ran.
    void note(uint32_t call, int32_t a = 0, int32_t b = 0) { record(call, *state, a, b); }

    uint32_t answer(HostCall call, int32_t a = 0, int32_t b = 0) {
        note(label(call), a, b);
        return results[label(call)];
    }

    void step(Step f, State& s) override { record(static_cast<uint32_t>(f), s); }

    /// Records a query and answers it from the results table (0 when absent).
    uint32_t query(Query f, State& s) override {
        const auto call = static_cast<uint32_t>(f);
        record(call, s);
        return results[call];
    }

    void play_movie(State& s, std::string_view) override { record(label(HostCall::play_movie), s); }

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

    // Steps the dispatcher once with the multiplayer states.
    void run(State& s) {
        state = &s;
        dispatch(s, *this, mp::state_handler(frontend));
    }

    // Ticks the frontend mode once, with the multiplayer states or without them.
    void tick(State& s, bool multiplayer) {
        state = &s;
        tick_frontend_mode(s, *this, multiplayer ? mp::state_handler(frontend) : StateHandler{});
    }
};

/// Tells whether two trace events are equal in every field.
bool same(const Event& left, const Event& right) {
    return left.call == right.call && left.a == right.a && left.b == right.b &&
           left.state == right.state && left.signal == right.signal;
}

bool same(const std::vector<Event>& left, const std::vector<Event>& right) {
    if (left.size() != right.size())
        return false;
    for (std::size_t i = 0; i < left.size(); ++i)
        if (!same(left[i], right[i]))
            return false;
    return true;
}

State at(uint8_t state, uint8_t signal) {
    State s;
    s.state = state;
    s.signal = s.pending_signal = signal;
    return s;
}

/// Requires the host's trace to hold exactly the expected calls, in order.
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
    { // Main-menu entry without a launch: the frontend's own main menu, with the direct-session bit cleared.
        auto s = at(2, 0);
        RecordingHost h;
        h.frontend.state.gui_flags = 0xffff;
        h.run(s);
        pair(s, 2, 0);
        trace(
            h,
            {label(Step::pop_input_event),
             label(mp::query::generate_default_game_name),
             label(mp::query::context_flags),
             label(HostCall::select_map_list),
             label(Step::setup_main_menu),
             label(Step::return_to_main_menu),
             label(HostCall::set_cursor_visible)}
        );
        require(h.frontend.state.gui_flags == 0xffef, "direct session bit cleared");
    }
    { // A connection type opens multiplayer from the main menu at once.
        auto s = at(2, 0);
        RecordingHost h;
        h.frontend.state.connection_type = 1;
        h.run(s);
        pair(s, 2, 6);
        trace(
            h,
            {label(Step::pop_input_event),
             label(mp::query::generate_default_game_name),
             label(HostCall::select_map_list),
             label(Step::setup_main_menu),
             label(Step::check_state_checksum),
             label(HostCall::set_cursor_visible)}
        );
    }
    { // Main-menu signal6: multiplayer path, then the provider routes.
        auto s = at(2, 6);
        s.session_flags = 0x8008;
        RecordingHost h;
        h.run(s);
        pair(s, 16, 0);
        require(s.session_flags == 0x8000, "multiplayer flag clear");
        require(
            h.events[1].call == label(HostCall::select_map_list) && h.events[1].a == 3,
            "multiplayer selection3"
        );
        h.results[label(HostCall::resolve_connection_info)] = 1;
        h.run(s);
        pair(s, 16, 2);
        require(
            h.events.back().call == label(Step::check_state_checksum) &&
                h.events[h.events.size() - 2].call == label(HostCall::resolve_connection_info) &&
                h.events[h.events.size() - 2].a == -1,
            "saved provider resolved"
        );
        h.frontend.state.provider_guid = mp::modem_provider;
        h.run(s);
        pair(s, 21, 1);
        require(h.events.back().call == label(mp::step::setup_modem_connect), "modem route");
    }
    { // The main menu's other signals run as the frontend's own.
        auto s = at(2, 5);
        RecordingHost h;
        h.run(s);
        pair(s, 8, 0);
        trace(
            h,
            {label(Step::pop_input_event),
             label(Step::check_state_checksum),
             label(Step::check_state_checksum),
             label(Step::shut_down_resource)}
        );
        s = at(2, 8);
        h.events.clear();
        h.run(s);
        trace(
            h,
            {label(Step::pop_input_event),
             label(Step::draw_current_frame),
             label(Step::shut_down_resource),
             label(HostCall::shut_down)}
        );
    }
    { // The "-y" request asks for setup_requested on the first main-menu update, once.
        auto s = at(2, 1);
        RecordingHost h;
        h.frontend.state.setup_request = 1;
        h.run(s);
        trace(
            h,
            {label(Step::pop_input_event),
             label(Step::present_frame),
             label(mp::step::setup_requested)}
        );
        require(h.frontend.state.setup_request == 0, "request consumed");
        h.events.clear();
        h.run(s);
        trace(h, {label(Step::pop_input_event), label(Step::present_frame)});
    }
    { // The context_flags and adapter_lookup_needed results are tested in their low byte only.
        auto s = at(2, 0);
        RecordingHost h;
        h.results[label(mp::query::context_flags)] = 0x100;
        h.results[label(mp::query::adapter_lookup_needed)] = 1;
        h.run(s);
        require(
            h.events.back().call == label(HostCall::set_cursor_visible), "low byte query false"
        );
        s = at(2, 0);
        h.events.clear();
        h.results[label(mp::query::context_flags)] = 1;
        h.run(s);
        trace(
            h,
            {label(Step::pop_input_event),
             label(mp::query::generate_default_game_name),
             label(mp::query::context_flags),
             label(mp::query::adapter_lookup_needed),
             label(mp::step::return_after_launch)}
        );
    }
    { // The dispatcher Host answers the queries (the extension registers their
        // handlers): a launched battle takes the return after a launch
        // (step 0x109).
        auto s = at(2, 0);
        RecordingHost h;
        h.results[label(mp::query::context_flags)] = 1;
        h.results[label(mp::query::adapter_lookup_needed)] = 1;
        h.run(s);
        trace(
            h,
            {label(Step::pop_input_event),
             label(mp::query::generate_default_game_name),
             label(mp::query::context_flags),
             label(mp::query::adapter_lookup_needed),
             label(mp::step::return_after_launch)}
        );
        // The launch's connection type, when the hook gives one, keeps the main menu.
        static int32_t type = 1;
        h.frontend.host.connection_type = [](void*) { return type; };
        s = at(2, 0);
        h.events.clear();
        h.run(s);
        pair(s, 2, 6);
        type = 0;
        h.results[label(mp::query::context_flags)] = 0;
        s = at(2, 0);
        h.events.clear();
        h.run(s);
        trace(
            h,
            {label(Step::pop_input_event),
             label(mp::query::generate_default_game_name),
             label(mp::query::context_flags),
             label(HostCall::select_map_list),
             label(Step::setup_main_menu),
             label(Step::return_to_main_menu),
             label(HostCall::set_cursor_visible)}
        );
        // The battle room's back after a launch goes to the main menu in mode 1.
        h.results[label(mp::query::context_flags)] = 1;
        s = at(18, 3);
        h.events.clear();
        h.run(s);
        pair(s, 2, 0);
        require(
            h.events.back().call == label(HostCall::set_app_mode) && h.events.back().a == 1,
            "the launched battle room returns to the main menu"
        );
    }
    { // A return from a launched game asks for application mode 1: the next
        // pass sets it first, once, then runs the main menu's initialize,
        // which takes the return after a launch.
        auto s = at(2, 0);
        RecordingHost h;
        h.frontend.state.pending_app_mode = 1;
        h.results[label(mp::query::context_flags)] = 1;
        h.results[label(mp::query::adapter_lookup_needed)] = 1;
        h.run(s);
        require(
            !h.events.empty() && h.events.front().call == label(HostCall::set_app_mode) &&
                h.events.front().a == 1 && h.events.front().state == 2,
            "the pending application mode is set first"
        );
        require(
            h.events.back().call == label(mp::step::return_after_launch),
            "the main menu's initialize takes the return after a launch"
        );
        require(h.frontend.state.pending_app_mode == 0, "the pending mode is set once");
        s = at(2, 0);
        h.events.clear();
        h.run(s);
        for (const auto& e : h.events)
            require(e.call != label(HostCall::set_app_mode), "no mode on the next pass");
        // A state these states leave to the other frontend states still takes the mode.
        s = at(7, 1);
        h.events.clear();
        h.frontend.state.pending_app_mode = 1;
        h.run(s);
        require(
            !h.events.empty() && h.events.front().call == label(HostCall::set_app_mode),
            "the mode is set before the other frontend state"
        );
    }
    { // A launch waits for the connection selection's initialize to
        // select the multiplayer map list and clear the single-player flag.
        auto s = at(16, 0);
        s.session_flags = flags::single_player;
        RecordingHost h;
        h.frontend.state.launch_pending = 1;
        h.run(s);
        require(
            h.events[1].call == label(HostCall::select_map_list) && h.events[1].a == 3,
            "the multiplayer map list first"
        );
        require(
            (s.session_flags & flags::single_player) == 0 && h.frontend.state.launch_pending == 0,
            "the single-player flag clears once"
        );
        s = at(16, 0);
        h.events.clear();
        h.run(s);
        require(h.events[1].call != label(HostCall::select_map_list), "applied once only");
    }
    { // A lobby launch joins its session directly.
        auto s = at(2, 0);
        RecordingHost h;
        h.results[label(mp::query::generate_default_game_name)] = 1;
        h.run(s);
        pair(s, 17, 18);
        require(
            (h.frontend.state.gui_flags & 16) != 0 && (s.session_flags & 1) != 0,
            "direct session flag"
        );
    }
    for (auto state : {19, 20, 22, 255}) {
        auto s = at(static_cast<uint8_t>(state), 254);
        RecordingHost h;
        h.run(s);
        pair(s, static_cast<uint8_t>(state), 254);
        require(h.events.empty(), "unknown/default state only synchronizes");
    }
    { // A serial session whose connection dialog closed with OK skips the game list and proceeds.
        auto s = at(17, 0);
        RecordingHost h;
        h.frontend.state.provider_guid = mp::serial_provider;
        h.frontend.state.connection_flags = 1;
        h.run(s);
        pair(s, 17, 17);
        trace(
            h,
            {label(Step::pop_input_event),
             label(HostCall::set_guaranteed_delivery),
             label(Step::check_state_checksum),
             label(Step::reset_player_slots),
             label(Step::check_state_checksum)}
        );
    }
    { // Otherwise the modem and serial sessions clear the session identifier before the game list.
        auto s = at(17, 0);
        RecordingHost h;
        h.frontend.state.provider_guid = mp::modem_provider;
        h.frontend.state.session_guid.fill(0x5a);
        h.run(s);
        pair(s, 17, 1);
        require(h.frontend.state.session_guid == mp::Identifier{}, "session identifier cleared");
        require(
            h.events[h.events.size() - 2].call == label(mp::step::setup_game_select) &&
                h.events.back().call == label(mp::step::show_flag_message),
            "game list then flag message"
        );
    }
    { // A watcher join marks the local player's descriptor.
        auto s = at(17, 19);
        s.players[0].descriptor = Descriptor{};
        RecordingHost h;
        h.results[label(HostCall::start_player_session)] = 1;
        h.run(s);
        pair(s, 17, 21);
        require(s.players[0].descriptor->options == 0x40, "descriptor flags");
    }
    { // Player filtering and order use live fields after each host call.
        auto s = at(17, 21);
        s.players[0].descriptor = Descriptor{0xfffe, 0};
        s.players[0].present = true;
        s.players[0].status = 1;
        s.players[0].player_id = 42;
        s.players[2].present = true;
        s.players[2].status = 2;
        s.players[2].player_id = 99;
        RecordingHost h;
        h.frontend.state.session_object_flags = 2;
        h.frontend.state.setup_options = 16;
        h.run(s);
        pair(s, 17, 21);
        trace(
            h,
            {label(Step::pop_input_event),
             label(mp::step::select_mission_language),
             label(HostCall::add_player_slot),
             label(HostCall::add_player_slot)}
        );
        require(
            s.players[0].descriptor->role == 0xffff && s.players[0].machine_flags == 2 &&
                s.session_flags == 4,
            "player flags"
        );
        require(h.events[2].a == 42 && h.events[3].a == 99, "player iteration order");
    }
    { // A join whose local player index is past the ten slots is refused.
        auto s = at(17, 21);
        s.local_player_index = 10;
        RecordingHost h;
        bool refused = false;
        try {
            h.run(s);
        } catch (const std::out_of_range&) {
            refused = true;
        }
        require(refused, "local player index out of range");
    }
    {
        auto s = at(18, 0);
        RecordingHost h;
        h.run(s);
        pair(s, 18, 1);
        trace(
            h,
            {label(mp::step::setup_battleroom),
             label(HostCall::handle_game_outcome_event),
             label(HostCall::handle_game_outcome_event),
             label(Step::check_state_checksum),
             label(mp::step::reset_player_pings)}
        );
        require(h.events[1].a == 1 && h.events[2].a == 2 && h.events[1].b == 3, "loading signals");
    }
    {
        auto s = at(18, 17);
        s.players[3].status = 4;
        RecordingHost h;
        h.run(s);
        trace(
            h, {label(HostCall::set_player_slot_state), label(mp::step::sync_then_free_battleroom)}
        );
        require(h.events[0].a == 3 && s.session_flags == 4, "player status4 route");
    }
    // Leaving the battle room returns a modem or serial game to the
    // connection selection, and a TCP/IP, IPX or other game to the game list.
    for (auto result :
         {mp::transport_kind::modem,
          mp::transport_kind::tcpip,
          mp::transport_kind::ipx,
          mp::transport_kind::serial,
          mp::transport_kind::other,
          0x100u}) {
        auto s = at(18, 3);
        RecordingHost h;
        h.results[label(mp::query::transport_kind)] = result;
        h.run(s);
        pair(
            s,
            result == mp::transport_kind::modem || result == mp::transport_kind::serial ? 16 : 17,
            0
        );
        require(
            h.events[1].call == label(HostCall::init_session_channels) && h.events[1].a == 2 &&
                h.events[1].b == 100,
            "session channels"
        );
    }
    {
        auto s = at(18, 3);
        RecordingHost h;
        h.results[label(mp::query::context_flags)] = 1;
        h.run(s);
        pair(s, 2, 0);
        require(
            h.events.back().call == label(HostCall::set_app_mode) && h.events.back().a == 1,
            "loading mode1"
        );
    }
    { // A direct session leaves with the flush instead of returning to a menu.
        auto s = at(18, 3);
        RecordingHost h;
        h.frontend.state.gui_flags = mp::flags::direct_session;
        h.run(s);
        pair(s, 18, 3);
        require(
            h.events.back().call == label(mp::step::leave_game_with_flush), "direct session flush"
        );
    }
    {
        auto s = at(21, 1);
        RecordingHost h;
        h.frontend.state.connection_flags = 0xffff;
        h.run(s);
        pair(s, 16, 0);
        require(h.frontend.state.connection_flags == 0xfffc, "connection flags clear");
        trace(
            h,
            {label(mp::query::open_service_session),
             label(Step::check_state_checksum),
             label(Step::check_state_checksum),
             label(Step::check_state_checksum)}
        );
    }
    { // A dialog whose session opened goes on to the session setup.
        auto s = at(21, 1);
        RecordingHost h;
        h.frontend.state.connection_flags = 0x0003;
        h.results[label(mp::query::open_service_session)] = 1;
        h.run(s);
        pair(s, 17, 0);
        require(h.frontend.state.connection_flags == 0x0003, "the connection flags stay");
    }
    { // Another provider's session opens from the connection selection: the
        // session setup follows when it opened; when it did not, the answer
        // has already gone back to the connection selection.
        auto s = at(16, 2);
        RecordingHost h;
        h.frontend.state.provider_guid.fill(0x11);
        h.results[label(mp::query::open_service_session)] = 1;
        h.run(s);
        pair(s, 17, 0);
        trace(
            h,
            {label(Step::pop_input_event),
             label(mp::query::open_service_session),
             label(Step::check_state_checksum),
             label(Step::check_state_checksum),
             label(Step::check_state_checksum)}
        );
        s = at(16, 2);
        h.events.clear();
        h.results[label(mp::query::open_service_session)] = 0;
        h.run(s);
        pair(s, 16, 2);
        trace(h, {label(Step::pop_input_event), label(mp::query::open_service_session)});
    }
    { // A join goes on at once while score reporting answers 0, and waits
        // for the reporter, presenting frames, while it answers nonzero.
        for (const uint32_t answer : {0u, 1u}) {
            auto s = at(17, 18);
            s.players[0].descriptor = Descriptor{};
            RecordingHost h;
            h.results[label(HostCall::start_player_session)] = 1;
            h.results[label(mp::query::init_score_reporting)] = answer;
            h.run(s);
            pair(s, 17, answer == 0 ? mp::signal_id::finish_join : mp::signal_id::await_reporting);
        }
        auto s = at(17, mp::signal_id::await_reporting);
        RecordingHost h;
        h.run(s);
        pair(s, 17, mp::signal_id::await_reporting);
        trace(h, {label(Step::pop_input_event), label(Step::present_frame)});
    }
    { // A null session routine answers 0: no provider resolves and no session starts.
        auto s = at(16, 0);
        RecordingHost h;
        h.frontend.host = {};
        h.run(s);
        pair(s, 16, 1);
        s = at(17, 18);
        s.players[0].descriptor = Descriptor{};
        h.run(s);
        pair(s, 17, 0);
    }
    { // The UI reset clears chat mode, the OK connection bit and the error text.
        auto s = at(9, 4);
        RecordingHost h;
        h.state = &s;
        h.frontend.state.chat_mode = 2;
        h.frontend.state.connection_flags = 3;
        h.frontend.state.error_text[0] = 'x';
        mp::reset_frontend_ui(h.frontend, s, h);
        pair(s, 0, 0);
        require(
            h.frontend.state.chat_mode == 0 && h.frontend.state.connection_flags == 2 &&
                h.frontend.state.error_text[0] == 0,
            "ui reset"
        );
        trace(h, {label(Step::check_state_checksum), label(Step::check_state_checksum)});
    }
    { // Mode 2 moves to mode 3 from a loading battle room and from a join that skips the battle room.
        auto s = at(18, 1);
        s.session_flags = flags::loading;
        RecordingHost h;
        h.state = &s;
        h.results[label(Query::map_list_object_state)] = 7;
        tick_frontend_mode(s, h, mp::state_handler(h.frontend));
        bool multiplayer_idle = false;
        for (const auto& e : h.events)
            multiplayer_idle =
                multiplayer_idle || (e.call == label(HostCall::set_app_mode) && e.a == 3);
        require(multiplayer_idle, "loading frontend -> mode 3");
        auto t = at(17, 18);
        RecordingHost g;
        g.frontend.state.setup_options = mp::flags::skip_battleroom;
        require(
            mp::handover_mode(g.frontend, t) == mp::app_mode::multiplayer_idle, "join -> mode 3"
        );
        g.frontend.state.setup_options = 0;
        require(mp::handover_mode(g.frontend, t) == 0, "a join into the battle room stays");
        require(mp::handover_mode(g.frontend, at(18, 1)) == 0, "idle battle room stays");
    }
    // Offline (no launch, no "-n" or "-y", every query answering 0) the
    // multiplayer states leave the frontend as it is without them: its main menu
    // runs the same steps in the same order, with the same state, signal and
    // flags after, apart from the two launch queries it asks at initialize
    // and the MULTI signal the single-player main menu ignores; every other
    // state is the frontend's own, and so is the mode handover.
    for (const uint16_t session_flags : {uint16_t{0}, flags::loading}) {
        for (int state = 0; state <= state_id::briefing_to_single_player; ++state) {
            for (int signal = 0; signal <= 0xff; ++signal) {
                if (state == state_id::main_menu && signal == signal_id::multiplayer)
                    continue;
                auto offline = at(static_cast<uint8_t>(state), static_cast<uint8_t>(signal));
                offline.session_flags = session_flags;
                auto online = offline;
                RecordingHost single, multiplayer;
                single.tick(offline, false);
                multiplayer.tick(online, true);
                auto expected = single.events;
                if (state == state_id::main_menu && signal == signal_id::initialize) {
                    // After reload_unit_overrides and pop_input_event.
                    const auto at_menu = expected.begin() + 2;
                    const Event launch{label(mp::query::generate_default_game_name), 0, 0, 2, 0};
                    const Event launched{label(mp::query::context_flags), 0, 0, 2, 0};
                    expected.insert(at_menu, {launch, launched});
                }
                require(
                    same(multiplayer.events, expected),
                    "offline frontend trace differs from the single-player frontend's"
                );
                require(
                    online.state == offline.state && online.signal == offline.signal &&
                        online.pending_signal == offline.pending_signal &&
                        online.session_flags == offline.session_flags,
                    "offline frontend state differs from the single-player frontend's"
                );
            }
        }
    }
    std::cout << "multiplayer frontend state trace cases passed\n";
}
