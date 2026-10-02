// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The combined extension table (extension_list.hpp) over two test
// extensions, "base" first in the list and "derived" after it: each rule of
// extension.hpp by which it calls their hooks, the switch handlers chained
// through the command line's parse, and the starts and calls it stops for a
// hook or an entry two extensions fill.
#include "oa/app/extension_list.hpp"

#include "oa/app/command_line.hpp"
#include "oa/sim/messages.hpp"
#include "oa/ui/console/console.hpp"
#include "oa/ui/hud/team_panels.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace oa::app;

int failures = 0;

void expect(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAILED: %s\n", what);
        ++failures;
    }
}

// What a test extension answers, and which of its hooks it fills beyond
// those every test extension fills.
struct Probe {
    const char* name{};
    bool takes{};              // what the first-to-take hooks answer
    const char* option{};      // the one option take_option takes
    uint32_t option_effects{}; // the effects it gives the option
    MultiplayerSelection selection{};
    const char* label{};          // return_label
    const char* disconnect{};     // disconnect_text
    const char* usage{};          // text: usage_checks, usage_runs, usage_switches
    const char* note{};           // text: usage_note and register_switch
    const char* game_name{};      // frontend_entry
    const char* nickname{};       // frontend_entry
    bool keep_password{};         // keep_stored_password
    uint32_t state_bits{};        // state
    bool outcome_ready{true};     // outcome_ready
    const char* switch_letters{}; // the letters its switch handler takes; null for no handler
    command_line::SwitchHandler switches{};
    bool fills_frontend_game{};
    bool fills_frontend_states{};
    bool fills_draw_match_hud{true};
    // Which host entries its host hooks set.
    bool sets_extend{};
    bool sets_player_info_changed{};
    bool sets_sight_shared{};
    bool sets_tournament_game{};
    bool sets_play_sound{};
    bool sets_share_chat{};
    bool fills_open_recording{true};
    bool throws_open_recording{}; // open_recording throws instead of answering
    bool opened_from_zero{};      // open_recording's last call was given replay and info all zero
};

Probe base_probe;
Probe derived_probe;
std::vector<std::string> calls; // "<extension> <hook>", in call order
oa::Game* const kFrontendGame = reinterpret_cast<oa::Game*>(uintptr_t{0x1000});

Probe& probe_of(void* context) {
    return *static_cast<Probe*>(context);
}

void record(void* context, const char* hook) {
    calls.push_back(std::string(probe_of(context).name) + " " + hook);
}

/// Returns a reference of an incomplete type the hooks pass on and never use.
template <typename T>
T& opaque() {
    alignas(64) static unsigned char storage[64]{};
    return *reinterpret_cast<T*>(storage);
}

void post_nothing(void*, const char*, uint8_t, uint8_t) {
}

// Each extension sets entries of its own, which do different things so
// that no linker folds them into one function.
void base_play(void*, const char*) {
    calls.emplace_back("base play");
}

void derived_play(void*, const char*) {
    calls.emplace_back("derived play");
}

void share_nothing(void*, const char*) {
}

void extend_nothing(void*, oa::ui::console::Console*) {
}

void info_nothing(void*) {
}

void base_sight(void*, uint8_t, uint8_t) {
    calls.emplace_back("base sight");
}

void derived_sight(void*, uint8_t, uint8_t) {
    calls.emplace_back("derived sight");
}

bool never(void*) {
    return false;
}

// The replay an extension's open_recording gives, whether or not it takes
// the recording.
bool step_nothing(void*) {
    return false;
}

void status_nothing(void*, RecordingStatus&) {
}

void close_nothing(void*) {
}

// Whether a replay and its information are all zero, as the engine passes them.
bool all_zero(const ReplayHooks& replay, const RecordingInfo& info) {
    return replay.context == nullptr && replay.step == nullptr && replay.status == nullptr &&
           replay.close == nullptr && info.expected_end_tick == 0 && info.duration_ms == 0 &&
           info.viewer_player == 0 && info.player_count == 0 && !info.content_differs;
}

void fill(Extension* table, Probe& probe) {
    table->context = &probe;
    table->take_option =
        [](void* context, const char* name, const OptionValues&, uint32_t& effects) {
            record(context, "take_option");
            const auto& self = probe_of(context);
            if (self.option == nullptr || std::string_view(name) != self.option)
                return false;
            effects |= self.option_effects;
            return true;
        };
    table->check_options = [](void* context) { record(context, "check_options"); };
    table->switch_handler = [](void* context) -> const command_line::SwitchHandler* {
        record(context, "switch_handler");
        auto& self = probe_of(context);
        if (self.switch_letters == nullptr)
            return nullptr;
        self.switches.context = &self;
        self.switches.take =
            [](void* context, char letter, const command_line::SwitchArguments*, uint32_t*) {
                const auto& self = probe_of(context);
                if (std::strchr(self.switch_letters, letter) == nullptr)
                    return 0;
                calls.push_back(std::string(self.name) + " takes " + letter);
                return 1;
            };
        self.switches.reset = [](void* context) { record(context, "reset"); };
        return &self.switches;
    };
    table->text = [](void* context, ExtensionText which) -> const char* {
        record(context, "text");
        const auto& self = probe_of(context);
        if (which == ExtensionText::usage_note || which == ExtensionText::register_switch)
            return self.note;
        return self.usage;
    };
    table->startup = [](void* context, Runtime&) { record(context, "startup"); };
    table->register_screens = [](void* context, ScreenRegistry*) {
        record(context, "register_screens");
    };
    table->ready = [](void* context, Runtime&) { record(context, "ready"); };
    table->frontend_entry = [](void* context, FrontendEntry& entry) {
        record(context, "frontend_entry");
        entry.game_name = probe_of(context).game_name;
        entry.nickname = probe_of(context).nickname;
    };
    if (probe.fills_frontend_states)
        table->frontend_states = [](void* context, oa::ui::frontend_state::StateHandler&) {
            record(context, "frontend_states");
        };
    table->run_mode = [](void* context, Runtime&, RunPhase, int& exit_code) {
        record(context, "run_mode");
        if (probe_of(context).takes)
            exit_code = probe_of(context).name[0];
        return probe_of(context).takes;
    };
    table->start_scene = [](void* context, Runtime&) {
        record(context, "start_scene");
        return probe_of(context).takes;
    };
    table->shutdown = [](void* context, Runtime&) { record(context, "shutdown"); };
    table->release_runtime = [](void* context, Runtime&) { record(context, "release_runtime"); };
    table->select_multiplayer = [](void* context, Runtime&) {
        record(context, "select_multiplayer");
        return probe_of(context).selection;
    };
    if (probe.fills_frontend_game)
        table->frontend_game = [](void* context) {
            record(context, "frontend_game");
            return kFrontendGame;
        };
    table->keep_stored_password = [](void* context) {
        record(context, "keep_stored_password");
        return probe_of(context).keep_password;
    };
    table->check_multiplayer_menu = [](void* context, Runtime&) {
        record(context, "check_multiplayer_menu");
    };
    table->state = [](void* context, const Runtime&) {
        record(context, "state");
        return probe_of(context).state_bits;
    };
    table->frame = [](void* context, Runtime&, FrameStage) { record(context, "frame"); };
    table->simulation_step = [](void* context, Runtime&) {
        record(context, "simulation_step");
        return probe_of(context).takes;
    };
    table->outcome_ready = [](void* context, Runtime&) {
        record(context, "outcome_ready");
        return probe_of(context).outcome_ready;
    };
    table->match_game = [](void* context, oa::Game&) { record(context, "match_game"); };
    table->match_event = [](void* context, Runtime&, MatchEvent) {
        record(context, "match_event");
    };
    table->disconnect_text = [](void* context, uint8_t) {
        record(context, "disconnect_text");
        return probe_of(context).disconnect;
    };
    table->give_resources = [](void* context, Runtime&, uint8_t, uint8_t, float, bool) {
        record(context, "give_resources");
        return probe_of(context).takes;
    };
    table->message_hooks = [](void* context, Runtime&, oa::sim::messages::Hooks& hooks) {
        record(context, "message_hooks");
        if (probe_of(context).sets_play_sound)
            hooks.play_sound = &probe_of(context) == &base_probe ? base_play : derived_play;
        if (probe_of(context).sets_share_chat)
            hooks.share_chat = share_nothing;
    };
    table->player_gone = [](void* context, Runtime&, oa::World&, const oa::Player&) {
        record(context, "player_gone");
        return probe_of(context).takes;
    };
    table->console_host = [](void* context, Runtime&, oa::ui::console::ConsoleHost& host) {
        record(context, "console_host");
        if (probe_of(context).sets_extend) {
            host.extension_context = context;
            host.extend = extend_nothing;
        }
        if (probe_of(context).sets_player_info_changed)
            host.player_info_changed = info_nothing;
    };
    table->check_console =
        [](void* context, Runtime&, void (*)(void* user, const char* line), void*) {
            record(context, "check_console");
        };
    table->draw_loading =
        [](void* context, Runtime&, oa::Surface&, const oa::present::GafSprites*) {
            record(context, "draw_loading");
        };
    if (probe.fills_draw_match_hud)
        table->draw_match_hud = [](void* context, Runtime&) { record(context, "draw_match_hud"); };
    table->draw_match_overlay = [](void* context, Runtime&, const MatchOverlay&) {
        record(context, "draw_match_overlay");
    };
    table->pause_changed = [](void* context, Runtime&, bool) { record(context, "pause_changed"); };
    table->load_progress = [](void* context, Runtime&, const uint8_t*, size_t) {
        record(context, "load_progress");
    };
    table->team_panel_host = [](void* context, Runtime&, oa::ui::hud::TeamPanelHost& host) {
        record(context, "team_panel_host");
        if (probe_of(context).sets_sight_shared)
            host.sight_shared = &probe_of(context) == &base_probe ? base_sight : derived_sight;
        if (probe_of(context).sets_tournament_game)
            host.tournament_game = never;
    };
    table->close_requested = [](void* context, Runtime&) {
        record(context, "close_requested");
        return probe_of(context).takes;
    };
    table->return_label = [](void* context) {
        record(context, "return_label");
        return probe_of(context).label;
    };
    table->speed_changed = [](void* context, Runtime&, uint16_t) {
        record(context, "speed_changed");
    };
    table->app_mode_set = [](void* context, Runtime&, int32_t) { record(context, "app_mode_set"); };
    if (probe.fills_open_recording)
        table->open_recording = [](void* context,
                                   Runtime&,
                                   const RecordingInput& input,
                                   ReplayHooks& replay,
                                   RecordingInfo& info) {
            record(context, "open_recording");
            auto& self = probe_of(context);
            self.opened_from_zero = all_zero(replay, info);
            if (self.throws_open_recording)
                throw std::runtime_error(std::string(self.name) + " cannot replay " + input.name);
            // Written whether or not it takes the recording, so that a
            // declining extension's answer would show if it were passed on.
            replay = {context, step_nothing, status_nothing, close_nothing};
            info.expected_end_tick = static_cast<uint32_t>(self.name[0]);
            info.duration_ms = input.byte_count;
            info.viewer_player = 2;
            info.player_count = 2;
            info.content_differs = true;
            return self.takes;
        };
}

void init_base(Extension* table) {
    fill(table, base_probe);
}

void init_derived(Extension* table) {
    fill(table, derived_probe);
}

const RegisteredExtension kBoth[] = {{"base", init_base}, {"derived", init_derived}};

// Starts every test from probes that take nothing and fill every hook but
// the single-owner ones.
void reset_probes() {
    base_probe = {};
    base_probe.name = "base";
    derived_probe = {};
    derived_probe.name = "derived";
    calls.clear();
}

bool called(const std::vector<std::string>& expected) {
    const bool same = calls == expected;
    calls.clear();
    return same;
}

// The hooks after the context, as words.
std::vector<uintptr_t> hook_words(const Extension& table) {
    std::vector<uintptr_t> words(sizeof table / sizeof(void*));
    std::memcpy(words.data(), &table, sizeof table);
    words.erase(words.begin());
    return words;
}

// The message the list's start stops with, or "" when it starts.
std::string start_refusal() {
    try {
        const ExtensionList list(kBoth);
    } catch (const std::runtime_error& error) {
        return error.what();
    }
    return {};
}

void test_empty_list() {
    const ExtensionList none(std::span<const RegisteredExtension>{});
    for (const auto word : hook_words(none.combined()))
        expect(word == 0, "with no extension every hook is null");
}

void test_null_hooks_stay_null() {
    reset_probes();
    const RegisteredExtension one[] = {{"derived", init_derived}};
    derived_probe.fills_draw_match_hud = false;
    const ExtensionList list(one);
    const auto& table = list.combined();
    expect(table.draw_match_hud == nullptr, "a hook no extension fills stays null");
    expect(
        table.frontend_game == nullptr && table.frontend_states == nullptr, "so do the single ones"
    );
    expect(table.startup != nullptr, "a hook one extension fills is combined");
    derived_probe.fills_open_recording = false;
    const ExtensionList without(one);
    expect(
        without.combined().open_recording == nullptr,
        "no extension fills open_recording: the hook stays null, and no extension replays"
    );
}

void test_every_in_order() {
    reset_probes();
    const ExtensionList list(kBoth);
    const auto& table = list.combined();
    void* context = table.context;
    auto& runtime = opaque<Runtime>();
    table.check_options(context);
    table.startup(context, runtime);
    table.register_screens(context, nullptr);
    table.ready(context, runtime);
    table.check_multiplayer_menu(context, runtime);
    table.frame(context, runtime, FrameStage::pump);
    table.match_game(context, opaque<oa::Game>());
    table.match_event(context, runtime, MatchEvent::finished);
    table.check_console(context, runtime, nullptr, nullptr);
    table.draw_loading(context, runtime, opaque<oa::Surface>(), nullptr);
    table.draw_match_hud(context, runtime);
    table.draw_match_overlay(context, runtime, MatchOverlay{});
    table.pause_changed(context, runtime, true);
    table.load_progress(context, runtime, nullptr, 0);
    table.speed_changed(context, runtime, 10);
    table.app_mode_set(context, runtime, 1);
    std::vector<std::string> expected;
    for (const char* hook :
         {"check_options",
          "startup",
          "register_screens",
          "ready",
          "check_multiplayer_menu",
          "frame",
          "match_game",
          "match_event",
          "check_console",
          "draw_loading",
          "draw_match_hud",
          "draw_match_overlay",
          "pause_changed",
          "load_progress",
          "speed_changed",
          "app_mode_set"}) {
        expected.push_back(std::string("base ") + hook);
        expected.push_back(std::string("derived ") + hook);
    }
    expect(called(expected), "every extension is called, in list order");
    table.shutdown(context, runtime);
    expect(called({"derived shutdown", "base shutdown"}), "shutdown runs in reverse list order");
    table.release_runtime(context, runtime);
    expect(
        called({"derived release_runtime", "base release_runtime"}),
        "release_runtime runs in reverse list order"
    );
}

void test_first_to_take() {
    reset_probes();
    const ExtensionList list(kBoth);
    const auto& table = list.combined();
    void* context = table.context;
    auto& runtime = opaque<Runtime>();
    int exit_code = 0;
    // Neither takes: both are asked, the derived first.
    expect(!table.run_mode(context, runtime, RunPhase::start, exit_code), "nobody runs");
    expect(called({"derived run_mode", "base run_mode"}), "the derived extension is asked first");
    expect(!table.start_scene(context, runtime), "nobody starts a scene");
    expect(!table.simulation_step(context, runtime), "nobody steps");
    expect(!table.give_resources(context, runtime, 0, 1, 5.0F, true), "nobody gives");
    expect(
        !table.player_gone(context, runtime, opaque<oa::World>(), opaque<oa::Player>()), "nobody"
    );
    expect(!table.close_requested(context, runtime), "nobody answers a close");
    calls.clear();
    // The base takes: both are asked.
    base_probe.takes = true;
    expect(table.run_mode(context, runtime, RunPhase::headless, exit_code), "the base runs");
    expect(exit_code == 'b', "with the base's exit status");
    expect(called({"derived run_mode", "base run_mode"}), "after the derived declined");
    // The derived takes: the base is not asked.
    derived_probe.takes = true;
    expect(table.run_mode(context, runtime, RunPhase::headless, exit_code), "the derived runs");
    expect(exit_code == 'd', "with its exit status");
    expect(table.start_scene(context, runtime), "the derived starts the scene");
    expect(table.simulation_step(context, runtime), "the derived steps");
    expect(table.give_resources(context, runtime, 0, 1, 5.0F, true), "the derived gives");
    expect(
        table.player_gone(context, runtime, opaque<oa::World>(), opaque<oa::Player>()), "it tells"
    );
    expect(table.close_requested(context, runtime), "the derived answers a close");
    expect(
        called(
            {"derived run_mode",
             "derived start_scene",
             "derived simulation_step",
             "derived give_resources",
             "derived player_gone",
             "derived close_requested"}
        ),
        "the base is not asked once the derived took it"
    );
}

void test_take_option() {
    reset_probes();
    base_probe.option = "--base";
    base_probe.option_effects = option_effect::headless_check;
    derived_probe.option = "--derived";
    derived_probe.option_effects = option_effect::skip_intro;
    const ExtensionList list(kBoth);
    const auto& table = list.combined();
    const OptionValues values{};
    uint32_t effects = 0;
    expect(
        table.take_option(table.context, "--base", values, effects), "the base takes its option"
    );
    expect(effects == option_effect::headless_check, "with its effects alone");
    expect(called({"derived take_option", "base take_option"}), "after the derived declined");
    effects = 0;
    expect(
        table.take_option(table.context, "--derived", values, effects), "the derived takes its own"
    );
    expect(effects == option_effect::skip_intro, "with its effects");
    expect(called({"derived take_option"}), "and the base is not asked");
    expect(!table.take_option(table.context, "--neither", values, effects), "nobody takes it");
}

void test_first_answers() {
    reset_probes();
    const ExtensionList list(kBoth);
    const auto& table = list.combined();
    void* context = table.context;
    auto& runtime = opaque<Runtime>();

    expect(table.return_label(context) == nullptr, "no label");
    base_probe.label = "Hall";
    derived_probe.label = "";
    expect(std::string_view(table.return_label(context)) == "Hall", "an empty label is none");
    derived_probe.label = "Gate";
    calls.clear();
    expect(
        std::string_view(table.return_label(context)) == "Gate", "the derived label comes first"
    );
    expect(called({"derived return_label"}), "and the base is not asked");

    base_probe.disconnect = "gone";
    expect(std::string_view(table.disconnect_text(context, 3)) == "gone", "the base's text");
    derived_probe.disconnect = "";
    expect(
        std::string_view(table.disconnect_text(context, 3)).empty(), "an empty text is an answer"
    );

    expect(table.select_multiplayer(context, runtime) == MultiplayerSelection::unavailable, "none");
    base_probe.selection = MultiplayerSelection::frontend;
    calls.clear();
    expect(
        table.select_multiplayer(context, runtime) == MultiplayerSelection::frontend, "the base's"
    );
    expect(called({"derived select_multiplayer", "base select_multiplayer"}), "after the derived");
    derived_probe.selection = MultiplayerSelection::taken;
    expect(
        table.select_multiplayer(context, runtime) == MultiplayerSelection::taken, "the derived's"
    );

    FrontendEntry entry{};
    base_probe.game_name = "Room";
    base_probe.nickname = "Bee";
    derived_probe.nickname = "Dee";
    table.frontend_entry(context, entry);
    expect(
        entry.game_name != nullptr && std::string_view(entry.game_name) == "Room" &&
            entry.nickname != nullptr && std::string_view(entry.nickname) == "Dee",
        "each field comes from the first extension that gives it, the derived first"
    );
    derived_probe.nickname = "";
    table.frontend_entry(context, entry);
    expect(std::string_view(entry.nickname) == "Bee", "an empty nickname is none");
    base_probe = {};
    base_probe.name = "base";
    derived_probe.nickname = nullptr;
    table.frontend_entry(context, entry);
    expect(entry.game_name == nullptr && entry.nickname == nullptr, "no field without an answer");
}

void test_texts() {
    reset_probes();
    const ExtensionList list(kBoth);
    const auto& table = list.combined();
    void* context = table.context;
    expect(table.text(context, ExtensionText::usage_checks) == nullptr, "no usage text");
    expect(table.text(context, ExtensionText::usage_note) == nullptr, "no note");
    base_probe.usage = "[--base] ";
    derived_probe.usage = "[--derived] ";
    expect(
        std::string_view(table.text(context, ExtensionText::usage_runs)) == "[--base] [--derived] ",
        "the usage texts are joined in list order"
    );
    base_probe.note = "";
    expect(
        std::string_view(table.text(context, ExtensionText::usage_note)).empty(),
        "an empty note is the base's answer"
    );
    derived_probe.note = "derived's";
    expect(
        std::string_view(table.text(context, ExtensionText::register_switch)) == "derived's",
        "the derived's text comes first"
    );
}

void test_combined_answers() {
    reset_probes();
    const ExtensionList list(kBoth);
    const auto& table = list.combined();
    void* context = table.context;
    auto& runtime = opaque<Runtime>();
    base_probe.state_bits = extension_state::multiplayer;
    derived_probe.state_bits = extension_state::replay;
    expect(
        table.state(context, runtime) == (extension_state::multiplayer | extension_state::replay),
        "the state bits are OR'd"
    );
    derived_probe.keep_password = true;
    calls.clear();
    expect(table.keep_stored_password(context), "one extension keeps the password");
    expect(called({"base keep_stored_password", "derived keep_stored_password"}), "both are asked");
    derived_probe.keep_password = false;
    expect(!table.keep_stored_password(context), "neither keeps it");
    expect(table.outcome_ready(context, runtime), "both are ready");
    base_probe.outcome_ready = false;
    calls.clear();
    expect(!table.outcome_ready(context, runtime), "one holds the outcome");
    expect(called({"base outcome_ready", "derived outcome_ready"}), "every extension is asked");
}

void test_switches() {
    reset_probes();
    {
        const ExtensionList list(kBoth);
        const auto& table = list.combined();
        expect(table.switch_handler(table.context) == nullptr, "no handler when neither has one");
    }
    base_probe.switch_letters = "ny";
    derived_probe.switch_letters = "y";
    const ExtensionList list(kBoth);
    const auto& table = list.combined();
    const auto* handler = table.switch_handler(table.context);
    expect(handler != nullptr, "the handlers are chained");
    calls.clear();
    command_line::Switches switches{};
    expect(
        command_line::parse("-y -n1", switches, handler) == command_line::Status::run,
        "both switches are taken"
    );
    expect(
        called({"base reset", "derived reset", "derived takes y", "base takes n"}),
        "every reset runs, and a letter goes to the derived first"
    );
    expect(
        command_line::parse("-t60", switches, handler) ==
                command_line::Status::unavailable_switch &&
            switches.unavailable_switch == 't',
        "a reserved letter neither takes stops the parse"
    );
}

void test_single_owners() {
    reset_probes();
    base_probe.fills_frontend_game = true;
    derived_probe.fills_frontend_states = true;
    {
        const ExtensionList list(kBoth);
        const auto& table = list.combined();
        expect(table.frontend_game(table.context) == kFrontendGame, "the base's block");
        table.frontend_states(table.context, opaque<oa::ui::frontend_state::StateHandler>());
        expect(called({"base frontend_game", "derived frontend_states"}), "each owner is called");
    }
    derived_probe.fills_frontend_game = true;
    expect(
        start_refusal() ==
            "the extensions base and derived both fill frontend_game, which one extension at most "
            "may fill",
        "a second frontend_game stops the start"
    );
    derived_probe.fills_frontend_game = false;
    base_probe.fills_frontend_states = true;
    expect(
        start_refusal() == "the extensions base and derived both fill frontend_states, which one "
                           "extension at most "
                           "may fill",
        "a second frontend_states stops the start"
    );
}

// The message a host hook stops with, or "" when it returns.
template <typename Call>
std::string call_refusal(Call call) {
    try {
        call();
    } catch (const std::runtime_error& error) {
        return error.what();
    }
    return {};
}

void test_host_entries() {
    reset_probes();
    base_probe.sets_extend = true;
    derived_probe.sets_player_info_changed = true;
    base_probe.sets_sight_shared = true;
    derived_probe.sets_tournament_game = true;
    base_probe.sets_play_sound = true;
    derived_probe.sets_share_chat = true;
    const ExtensionList list(kBoth);
    const auto& table = list.combined();
    void* context = table.context;
    auto& runtime = opaque<Runtime>();

    oa::ui::console::ConsoleHost console{};
    console.post_message = post_nothing;
    const auto fill_console = [&] { table.console_host(context, runtime, console); };
    expect(call_refusal(fill_console).empty(), "entries each extension sets alone");
    expect(
        console.extend == extend_nothing && console.extension_context == &base_probe &&
            console.player_info_changed == info_nothing && console.post_message == post_nothing,
        "the host holds both extensions' entries and the engine's"
    );
    expect(call_refusal(fill_console).empty(), "the next match's host is filled the same way");
    derived_probe.sets_extend = true;
    console = {};
    expect(
        call_refusal(fill_console) ==
            "the extensions base and derived both set extension_context of the console's host, "
            "which one extension at most may set",
        "a second extension that sets the console's extension stops the call"
    );

    oa::ui::hud::TeamPanelHost panels{};
    const auto fill_panels = [&] { table.team_panel_host(context, runtime, panels); };
    expect(call_refusal(fill_panels).empty(), "the panels' entries each extension sets alone");
    derived_probe.sets_sight_shared = true;
    panels = {};
    expect(
        call_refusal(fill_panels) ==
            "the extensions base and derived both set sight_shared of the team panels' host, "
            "which one extension at most may set",
        "a second sender stops the call"
    );

    oa::sim::messages::Hooks hooks{};
    const auto fill_hooks = [&] { table.message_hooks(context, runtime, hooks); };
    expect(call_refusal(fill_hooks).empty(), "the message hooks each extension sets alone");
    derived_probe.sets_play_sound = true;
    hooks = {};
    expect(
        call_refusal(fill_hooks) ==
            "the extensions base and derived both set play_sound of the message log's hooks, "
            "which one extension at most may set",
        "a second extension's play_sound stops the call"
    );
}

// open_recording over both extensions, the derived asked first: neither
// takes the recording, the base or the derived takes it, or the derived
// throws.
void test_open_recording() {
    reset_probes();
    const ExtensionList list(kBoth);
    const auto& table = list.combined();
    void* context = table.context;
    auto& runtime = opaque<Runtime>();
    const uint8_t bytes[] = {1, 2, 3};
    const RecordingInput input{"game.rec", bytes, sizeof bytes, true};
    ReplayHooks replay{};
    RecordingInfo info{};
    const auto open = [&] { return table.open_recording(context, runtime, input, replay, info); };

    expect(!open(), "no extension opens the recording");
    expect(
        called({"derived open_recording", "base open_recording"}),
        "the derived extension is asked first, then the base"
    );
    expect(
        derived_probe.opened_from_zero && base_probe.opened_from_zero,
        "each extension asked is given replay and info all zero"
    );
    expect(
        all_zero(replay, info), "the caller's replay and info stay zero when every one declines"
    );

    base_probe.takes = true;
    expect(open(), "the base opens the recording");
    expect(called({"derived open_recording", "base open_recording"}), "after the derived declined");
    expect(
        base_probe.opened_from_zero, "the base starts from zero after the derived wrote its own"
    );
    expect(
        replay.context == &base_probe && replay.step == step_nothing &&
            replay.status == status_nothing && replay.close == close_nothing &&
            info.expected_end_tick == static_cast<uint32_t>('b') &&
            info.duration_ms == sizeof bytes && info.viewer_player == 2 && info.player_count == 2 &&
            info.content_differs,
        "the caller holds the base's replay and information"
    );

    replay = {};
    info = {};
    derived_probe.takes = true;
    expect(open(), "the derived opens the recording");
    expect(called({"derived open_recording"}), "and the base is not asked");
    expect(
        replay.context == &derived_probe && info.expected_end_tick == static_cast<uint32_t>('d'),
        "the caller holds the derived's replay and information"
    );

    replay = {};
    info = {};
    derived_probe.throws_open_recording = true;
    expect(
        call_refusal(open) == "derived cannot replay game.rec",
        "an exception of the extension asked reaches the caller"
    );
    expect(called({"derived open_recording"}), "and no other extension is asked");
    expect(all_zero(replay, info), "the caller's replay and info stay zero");
}

} // namespace

int main() {
    test_empty_list();
    test_null_hooks_stay_null();
    test_every_in_order();
    test_first_to_take();
    test_take_option();
    test_first_answers();
    test_texts();
    test_combined_answers();
    test_switches();
    test_single_owners();
    test_host_entries();
    test_open_recording();
    if (failures != 0)
        return 1;
    std::puts("extension list: every rule of combining two extensions' hooks");
    return 0;
}
