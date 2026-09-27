// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/ui/frontend_state/game_entry.hpp"
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace oa::ui::frontend_state;
namespace entry = oa::ui::frontend_state::game_entry;

namespace {
void require(bool value, const char* reason) {
    if (!value)
        throw std::runtime_error(reason);
}

struct Host final : entry::SinglePlayerHost, entry::SkirmishHost {
    State& state;
    std::array<uint32_t, 6> buttons{};
    std::array<uint32_t, 2> discs{1, 1};
    std::vector<std::string> trace;
    std::vector<entry::Message> messages;
    int32_t selected = 1, capacity = 10;

    explicit Host(State& value) : state(value) {}

    uint32_t button_result(entry::MenuHandle m, entry::Button b) override {
        require(m.value == 123, "menu identity");
        trace.emplace_back(entry::resource_name(b));
        return buttons[static_cast<std::size_t>(b)];
    }

    void play_sound(entry::Sound sound, uint32_t arg) override {
        require(arg == 0, "sound arg");
        trace.emplace_back("sound:" + std::string(entry::resource_name(sound)));
    }

    void select_cursor_animation(uint32_t index) override {
        require(index == 20, "cursor index");
        trace.emplace_back("cursor");
    }

    uint32_t find_disc(entry::Disc disc) override {
        trace.emplace_back("disc:" + std::to_string(static_cast<unsigned>(disc)));
        return discs[static_cast<std::size_t>(disc)];
    }

    void refresh_disc_archives() override { trace.emplace_back("refresh"); }

    std::string translate(entry::Message message) override {
        messages.push_back(message);
        trace.emplace_back("translate");
        return "translated";
    }

    void
    show_frontend_message(std::string_view text, int32_t width, int32_t a, int32_t b) override {
        require(text == "translated" && a == 1 && b == 1, "message arguments");
        trace.emplace_back("message:" + std::to_string(width));
    }

    void clear_event_selection(entry::MenuHandle m) override {
        require(m.value == 123, "clear identity");
        trace.emplace_back("clear:event");
    }

    void clear_frontend_selection() override { trace.emplace_back("clear:frontend"); }

    void open_load_game() override { trace.emplace_back("load"); }

    void open_options() override { trace.emplace_back("options"); }

    int32_t select_map(std::string_view name) override {
        require(name == "map", "map identity");
        require(state.player_count == 2, "provisional player count");
        trace.emplace_back("map");
        return selected;
    }

    int32_t map_player_capacity() override {
        trace.emplace_back("capacity");
        return capacity;
    }

    void apply_skirmish_players() override {
        require(state.player_count >= 2, "final count before setup");
        trace.emplace_back("apply");
    }

    void save_preferences() override {
        require(
            state.mission_results[24] == 0x55 && state.mission_results[25] == 0,
            "pattern before save"
        );
        trace.emplace_back("save");
    }
};

void expect(const Host& host, std::initializer_list<std::string> wanted) {
    require(host.trace == std::vector<std::string>(wanted), "trace mismatch");
}

entry::SkirmishSettings settings() {
    entry::SkirmishSettings value;
    value.slot_count = 2;
    value.map_name = "map";
    value.slots[0].controller = entry::controller::human;
    value.slots[0].alliance = 0;
    value.slots[1].controller = entry::controller::computer;
    value.slots[1].alliance = 1;
    return value;
}

void single() {
    {
        State state;
        Host host(state);
        entry::handle_single_player_event(state, {{123}, -1}, host);
        expect(host, {});
    }
    for (std::size_t button = 0; button < 6; ++button) {
        State state;
        Host host(state);
        host.buttons[button] = 0x100;
        entry::handle_single_player_event(state, {{123}, 0}, host);
        require(
            host.trace[button] == entry::resource_name(static_cast<entry::Button>(button)),
            "short circuit button tests"
        );
        const std::array<uint8_t, 6> signals{10, 11, 0, 0, 3, 14};
        require(state.pending_signal == signals[button], "single signal");
    }
    {
        State state;
        Host host(state);
        host.buttons.fill(1);
        entry::handle_single_player_event(state, {{123}, 0}, host);
        expect(host, {"NewCamp", "disc:0", "refresh", "sound:BigButton", "cursor"});
    }
    {
        State state;
        Host host(state);
        host.buttons[2] = 1;
        entry::handle_single_player_event(state, {{123}, 0}, host);
        expect(
            host,
            {"NewCamp", "Skirmish", "LoadGame", "sound:BigButton", "cursor", "load", "clear:event"}
        );
    }
    {
        State state;
        Host host(state);
        host.buttons[3] = 1;
        entry::handle_single_player_event(state, {{123}, 0}, host);
        expect(
            host,
            {"NewCamp",
             "Skirmish",
             "LoadGame",
             "Options",
             "sound:options",
             "clear:event",
             "cursor",
             "options"}
        );
    }
    for (auto button : {0U, 1U, 5U}) {
        State state;
        Host host(state);
        host.buttons[button] = 1;
        host.discs.fill(0x100);
        entry::handle_single_player_event(state, {{123}, 0}, host);
        require(
            state.pending_signal == 0 && host.trace.back() == "clear:frontend", "disc low-byte gate"
        );
        require(
            host.messages ==
                std::vector<entry::Message>{
                    button == 1 ? entry::Message::multiplayer_disc : entry::Message::campaign_disc
                },
            "disc message"
        );
    }
    {
        State state;
        Host host(state);
        entry::handle_single_player_event(state, {{123}, 0}, host);
        require(host.trace.back() == "clear:event", "no match cleanup");
    }
}

void skirmish() {
    {
        State state;
        Host host(state);
        auto value = settings();
        require(entry::start_selected_skirmish(state, value, {123}, host), "valid start");
        expect(
            host,
            {"sound:BigButton", "disc:1", "refresh", "map", "capacity", "apply", "save", "cursor"}
        );
        require(state.pending_signal == 2 && state.player_count == 2, "start state");
    }
    {
        State state;
        Host host(state);
        auto value = settings();
        host.discs[1] = 0x100;
        require(
            entry::start_selected_skirmish(state, value, {123}, host), "missing disc continues"
        );
        expect(
            host,
            {"sound:BigButton",
             "disc:1",
             "translate",
             "message:200",
             "clear:frontend",
             "refresh",
             "map",
             "capacity",
             "apply",
             "save",
             "cursor"}
        );
    }
    for (int failure = 0; failure < 4; ++failure) {
        State state;
        Host host(state);
        auto value = settings();
        if (failure == 0)
            host.selected = 0;
        if (failure == 1)
            value.slots[0].controller = entry::controller::disabled;
        if (failure == 2)
            host.capacity = -1;
        if (failure == 3)
            value.slots[1].alliance = 0;
        require(!entry::start_selected_skirmish(state, value, {123}, host), "invalid start");
        require(
            host.messages == std::vector<entry::Message>{static_cast<entry::Message>(
                                 static_cast<int>(entry::Message::missing_terrain) + failure
                             )},
            "validation priority"
        );
        require(state.pending_signal == 0 && host.trace.back() == "clear:event", "failure cleanup");
    }
    {
        State state;
        Host host(state);
        auto value = settings();
        value.slot_count = 3;
        value.slots[2].controller = entry::controller::human;
        value.slots[2].alliance = 5;
        host.capacity = 2;
        require(
            entry::start_selected_skirmish(state, value, {123}, host) && state.player_count == 3,
            "capacity uses computers plus one before final count"
        );
    }
    auto value = settings();
    value.slots[0].alliance = 5;
    value.slots[1].alliance = 5;
    require(!entry::all_enabled_players_allied(value), "unassigned alliances");
    value.slots[1].alliance = 2;
    require(!entry::all_enabled_players_allied(value), "unassigned player differs");
    value.slots[0].controller = 0;
    require(entry::all_enabled_players_allied(value), "disabled players excluded");
    value.slot_count = 0;
    require(
        entry::human_count(value) == 0 && !entry::all_enabled_players_allied(value), "empty slots"
    );
    for (auto count : {-1, 12}) {
        value.slot_count = count;
        bool rejected = false;
        try {
            (void)entry::computer_count(value);
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        require(rejected, "bounded slots");
    }
}

// The reset writes 25 bytes of 0x55 to Game.mission_results and a zero to
// its last byte.
void mission_results() {
    State state;
    state.mission_results.fill('W');
    entry::reset_mission_results(state);
    for (std::size_t index = 0; index < 25; ++index)
        require(state.mission_results[index] == 0x55, "unplayed mission");
    require(state.mission_results[25] == 0, "terminated results");
}
} // namespace

int main() {
    try {
        single();
        skirmish();
        mission_results();
        std::cout << "game entry tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
