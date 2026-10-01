// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/ui/frontend_state/main_menu.hpp"
#include <array>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace oa::ui::frontend_state;
namespace menu = oa::ui::frontend_state::main_menu;

namespace {
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}

struct TestHost final : menu::Host {
    State& state;
    menu::Environment first{1, {11}}, second{1, {22}};
    menu::Environment* current = &first;
    std::array<uint32_t, 5> buttons{};
    std::array<uint32_t, 2> discs{};
    std::vector<std::string> trace;
    std::vector<std::array<uint32_t, 4>> integrity_states;
    uint32_t load_result{};
    uint8_t app_flags = flags::intro_enabled;
    int16_t shift{};
    bool replace_on_application{}, replace_on_load{}, throw_on_load{};

    explicit TestHost(State& s) : state(s) {}

    menu::Environment& environment() override {
        trace.emplace_back("environment");
        return *current;
    }

    void release_sparks() override { trace.emplace_back("release"); }

    uint32_t button_result(menu::MenuHandle handle, menu::Button b) override {
        require(handle.value == 123, "menu identity");
        trace.emplace_back("button:" + std::string(menu::resource_name(b)));
        return buttons[static_cast<std::size_t>(b)];
    }

    void play_sound(menu::Sound s, uint32_t arg) override {
        require(arg == menu::sound_argument, "sound argument");
        trace.emplace_back("sound:" + std::string(menu::resource_name(s)));
    }

    void select_cursor_animation(uint32_t amount) override {
        require(amount == menu::menu_cursor_animation_index, "cursor animation index");
        trace.emplace_back("cursor");
    }

    void prepare_multiplayer() override { trace.emplace_back("prepare"); }

    std::string resolve_resource(menu::ResourceRequest request) override {
        require(
            request.directory == "maps" && request.name == "multiplay" &&
                request.extension == "tdf",
            "resource request"
        );
        trace.emplace_back("resolve");
        return "resolved/maps/multiplay.tdf";
    }

    menu::DocumentHandle construct_document() override {
        trace.emplace_back("construct");
        return {33};
    }

    uint32_t load_document(menu::DocumentHandle h, std::string_view path) override {
        require(h.value == 33 && path == "resolved/maps/multiplay.tdf", "document identity/path");
        trace.emplace_back("load");
        if (replace_on_load)
            current = &second;
        if (throw_on_load)
            throw std::runtime_error("fixture load exception");
        return load_result;
    }

    void destroy_document(menu::DocumentHandle h) noexcept override {
        if (h.value != 33) {
            std::terminate();
        }
        trace.emplace_back("destroy");
    }

    void reset_after_multiplayer_selection() override {
        require(state.pending_signal == signal_id::multiplayer, "signal before multiplayer reset");
        trace.emplace_back("reset");
    }

    uint8_t application_flags() override {
        trace.emplace_back("application");
        if (replace_on_application)
            current = &second;
        return app_flags;
    }

    uint32_t find_disc(menu::Disc d) override {
        const auto i = static_cast<std::size_t>(d);
        trace.emplace_back("disc:" + std::to_string(i));
        return discs[i];
    }

    int16_t shift_key_state() override {
        trace.emplace_back("shift");
        return shift;
    }

    void drain_input() override { trace.emplace_back("drain"); }

    void check_frontend_integrity() override {
        trace.emplace_back("integrity");
        integrity_states.push_back(
            {state.state, state.signal, state.pending_signal, state.movie_skip}
        );
    }

    void show_message(menu::MessageTarget target, menu::Message m) override {
        trace.emplace_back(
            "message:" + std::to_string(target.value) + ":" + std::to_string(static_cast<int>(m))
        );
    }

    void default_event(menu::MenuHandle h) override {
        require(h.value == 123, "default identity");
        trace.emplace_back("default");
    }
};

void run(TestHost& h, int32_t kind = 0) {
    menu::handle_event(h.state, {{123}, kind}, h);
}

void expect(const TestHost& h, std::initializer_list<const char*> expected) {
    std::vector<std::string> wanted;
    for (const auto* item : expected)
        wanted.emplace_back(item);
    if (h.trace != wanted) {
        for (const auto& item : h.trace)
            std::cerr << item << ' ';
        std::cerr << '\n';
        throw std::runtime_error("call order");
    }
}
} // namespace

int main() {
    try {
        {
            State s;
            TestHost h(s);
            run(h, menu::destroy_event);
            expect(h, {"environment", "release"});
        }
        {
            State s;
            TestHost h(s);
            h.buttons.fill(0x100);
            run(h);
            require(
                s.pending_signal == signal_id::single_player, "full-width button result/priority"
            );
            expect(h, {"environment", "button:SINGLE", "sound:BigButton", "cursor"});
        }
        {
            State s;
            TestHost h(s);
            h.buttons[1] = 1;
            h.load_result = 0x100;
            run(h);
            expect(
                h,
                {"environment",
                 "button:SINGLE",
                 "button:MULTI",
                 "sound:BigButton",
                 "cursor",
                 "prepare",
                 "resolve",
                 "construct",
                 "load",
                 "reset",
                 "destroy"}
            );
        }
        {
            State s;
            TestHost h(s);
            h.buttons[1] = 1;
            h.replace_on_load = true;
            run(h);
            expect(
                h,
                {"environment",
                 "button:SINGLE",
                 "button:MULTI",
                 "sound:BigButton",
                 "cursor",
                 "prepare",
                 "resolve",
                 "construct",
                 "load",
                 "environment",
                 "message:22:0",
                 "destroy"}
            );
        }
        {
            State s;
            TestHost h(s);
            h.buttons[1] = 1;
            h.throw_on_load = true;
            bool caught = false;
            try {
                run(h);
            } catch (const std::runtime_error&) {
                caught = true;
            }
            require(caught, "exception expected");
            require(h.trace.back() == "destroy", "document released when loading throws");
        }
        for (const auto shift :
             {int16_t(-32768), int16_t(-1), int16_t(0), int16_t(1), int16_t(32767)}) {
            State s;
            s.state = state_id::main_menu;
            s.signal = 7;
            s.pending_signal = 8;
            TestHost h(s);
            h.buttons[2] = 0x80000000U;
            h.discs[0] = 0x100;
            h.discs[1] = 0x101;
            h.shift = shift;
            run(h);
            require(
                s.state == state_id::intro_followup && s.signal == 0 && s.pending_signal == 0 &&
                    s.movie_skip == static_cast<unsigned>(shift < 0),
                "intro transition"
            );
            require(
                h.integrity_states ==
                    std::vector<std::array<uint32_t, 4>>{
                        {2, 7, 8, static_cast<unsigned>(shift < 0)},
                        {1, 7, 8, static_cast<unsigned>(shift < 0)}
                    },
                "integrity/write ordering"
            );
            expect(
                h,
                {"environment",
                 "button:SINGLE",
                 "button:MULTI",
                 "button:INTRO",
                 "sound:smlButton",
                 "application",
                 "disc:0",
                 "disc:1",
                 "cursor",
                 "shift",
                 "drain",
                 "integrity",
                 "integrity"}
            );
        }
        for (const auto button : {menu::Button::intro, menu::Button::credits}) {
            State s;
            s.pending_signal = 19;
            TestHost h(s);
            h.buttons[static_cast<std::size_t>(button)] = 1;
            h.app_flags = 0;
            h.replace_on_application = true;
            run(h);
            require(
                h.trace.back() == "message:11:1" && s.pending_signal == 19,
                "capture environment/fullscreen gate"
            );
            h.trace.clear();
            h.current = &h.first;
            h.app_flags = flags::intro_enabled;
            h.discs = {0x100, 0x100};
            run(h);
            require(h.trace.back() == "message:11:2", "low-byte disc failure");
            h.trace.clear();
            h.current = &h.first;
            h.first.messages_enabled = 0;
            run(h);
            require(h.trace.back() == "disc:1", "disabled notification gate");
            h.trace.clear();
            h.current = &h.first;
            h.first.messages_enabled = 1;
            h.first.message_target.value = 0;
            run(h);
            require(h.trace.back() == "disc:1", "null notification gate");
        }
        {
            State s;
            TestHost h(s);
            h.buttons[3] = 1;
            run(h);
            require(s.pending_signal == signal_id::exit_application, "exit signal");
            expect(
                h,
                {"environment",
                 "button:SINGLE",
                 "button:MULTI",
                 "button:INTRO",
                 "button:EXIT",
                 "sound:exit",
                 "cursor"}
            );
        }
        {
            State s;
            TestHost h(s);
            h.buttons[4] = 1;
            h.discs[0] = 1;
            run(h);
            require(s.pending_signal == signal_id::movie_5, "credits signal");
            expect(
                h,
                {"environment",
                 "button:SINGLE",
                 "button:MULTI",
                 "button:INTRO",
                 "button:EXIT",
                 "button:Credits",
                 "sound:smlButton",
                 "application",
                 "disc:0",
                 "cursor"}
            );
        }
        {
            State s;
            TestHost h(s);
            run(h);
            expect(
                h,
                {"environment",
                 "button:SINGLE",
                 "button:MULTI",
                 "button:INTRO",
                 "button:EXIT",
                 "button:Credits",
                 "default"}
            );
        }
        require(
            menu::resource_name(menu::Button::exit) == "EXIT" &&
                menu::resource_name(menu::Sound::exit) == "exit",
            "exit strings"
        );
        std::cout << "main menu traces passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
