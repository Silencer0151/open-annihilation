// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// A test extension that fills every hook of oa-game's extension table with
// a recorder. Each call is counted, under the hook's name and, for a hook
// that takes one, the enumerator it was given; every answer is the one a
// null hook stands for, so the game behaves as it does without an
// extension. When oa-game exits, the counts go to the file --record-hooks
// names, one "<hook>[ <enumerator>] <count>" line each in name order.
//
// The frontend's Game block, which frontend_game must return, is the
// recorder's own zeroed block, as the engine's own would be, and
// --check-multiplayer-menu, which check_multiplayer_menu takes over, only
// says that it ran. The recorder also adds one member to Runtime
// (recorder_runtime_members.hpp) and calls it through RuntimeExtension, the
// friend, as the extension table's startup hook runs.
//
// --record-quit STATUS reaches the screen services an extension keeps: an
// overlay with nothing to draw keeps the host and services its create is
// given, and on the fifth frame the recorder stops the sounds, plays BGM on
// the alternate route and asks for a frontend pass, and on the tenth ends
// the run through quit with STATUS. Without the option none of this runs.
#include "oa/app/runtime.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <string_view>

namespace oa::app {

uint32_t Runtime::count_recorder_member_call() {
    return ++recorder_member_calls_;
}

namespace {

// The version of the extension table's contract the recorder follows, and
// the hooks the table holds after its context at that version. A change to
// the table raises OA_EXTENSION_API_VERSION (extension.hpp); both follow it.
constexpr uint32_t kExtensionApiVersionRecorded = 7;
constexpr std::size_t kHookCount = 38;
static_assert(
    extension_api_version == kExtensionApiVersionRecorded,
    "the extension table's contract changed: record every hook here, "
    "then raise kExtensionApiVersionRecorded"
);
static_assert(
    sizeof(Extension) == sizeof(void*) * (1 + kHookCount),
    "the extension table's hooks changed: record every one of them here "
    "and raise OA_EXTENSION_API_VERSION"
);

// The option that names the file the counts go to.
constexpr std::string_view kRecordOption = "--record-hooks";
// The option that ends the run through ScreenServices::quit with a status.
constexpr std::string_view kQuitOption = "--record-quit";
// The after_pump frames --record-quit waits before the sound and frontend
// services, and before quit.
constexpr uint64_t kServicesFrame = 5;
constexpr uint64_t kQuitFrame = 10;

struct Recorder {
    std::string path{};                       // --record-hooks FILE; empty writes nothing
    std::map<std::string, uint64_t> counts{}; // by "<hook>[ <enumerator>]"
    oa::Game frontend_game{};
    bool quit{};                      // --record-quit was given
    int quit_status{};                // its STATUS
    uint64_t after_pump_frames{};     // frames seen at FrameStage::after_pump
    void* host{};                     // ScreenContext::host, kept from the overlay
    const ScreenServices* services{}; // ScreenContext::services, kept likewise
};

/// Returns the recorder of this process.
///
/// @return the recorder, which lives until the process exits
Recorder& recorder() {
    static Recorder state;
    return state;
}

/// Counts one call of a hook.
///
/// @param hook the hook's name
/// @param detail the enumerator it was given; null for none
void record(const char* hook, const char* detail = nullptr) {
    std::string key = hook;
    if (detail != nullptr)
        key += std::string(" ") + detail;
    ++recorder().counts[key];
}

/// Writes the counts to the file --record-hooks named; nothing without one.
void write_record() {
    const auto& state = recorder();
    if (state.path.empty())
        return;
    std::FILE* file = std::fopen(state.path.c_str(), "w");
    if (file == nullptr) {
        std::fprintf(stderr, "recorder: cannot write %s\n", state.path.c_str());
        return;
    }
    for (const auto& [key, count] : state.counts)
        std::fprintf(file, "%s %llu\n", key.c_str(), static_cast<unsigned long long>(count));
    std::fclose(file);
}

/// Returns the name of an ExtensionText.
///
/// @param which the text
/// @return its enumerator's name
const char* text_name(ExtensionText which) {
    switch (which) {
    case ExtensionText::usage_checks:
        return "usage_checks";
    case ExtensionText::usage_runs:
        return "usage_runs";
    case ExtensionText::usage_switches:
        return "usage_switches";
    case ExtensionText::usage_note:
        return "usage_note";
    case ExtensionText::register_switch:
        return "register_switch";
    }
    return "unknown";
}

/// Returns the name of a RunPhase.
///
/// @param phase the phase
/// @return its enumerator's name
const char* phase_name(RunPhase phase) {
    switch (phase) {
    case RunPhase::start:
        return "start";
    case RunPhase::headless_first:
        return "headless_first";
    case RunPhase::headless:
        return "headless";
    }
    return "unknown";
}

/// Returns the name of a FrameStage.
///
/// @param stage the stage
/// @return its enumerator's name
const char* stage_name(FrameStage stage) {
    switch (stage) {
    case FrameStage::pump:
        return "pump";
    case FrameStage::after_pump:
        return "after_pump";
    }
    return "unknown";
}

/// Returns the name of a MatchEvent.
///
/// @param event the event
/// @return its enumerator's name
const char* event_name(MatchEvent event) {
    switch (event) {
    case MatchEvent::finished:
        return "finished";
    case MatchEvent::torn_down:
        return "torn_down";
    case MatchEvent::left:
        return "left";
    case MatchEvent::results_reported:
        return "results_reported";
    case MatchEvent::results_released:
        return "results_released";
    case MatchEvent::watching_kept:
        return "watching_kept";
    }
    return "unknown";
}

/// Takes --record-hooks FILE and --record-quit STATUS and no other option (Extension::take_option).
///
/// @param context Extension::context (unused)
/// @param name the option as given
/// @param values takes the arguments after it
/// @param[out] effects option_effect bits; left 0
/// @return true for --record-hooks and --record-quit
bool take_option(
    void* /*context*/, const char* name, const OptionValues& values, uint32_t& /*effects*/
) {
    record("take_option");
    if (std::string_view(name) == kRecordOption) {
        recorder().path = values.next(values.arguments);
        return true;
    }
    if (std::string_view(name) == kQuitOption) {
        recorder().quit = true;
        recorder().quit_status = std::atoi(values.next(values.arguments));
        return true;
    }
    return false;
}

/// Counts the check of the options (Extension::check_options).
///
/// @param context Extension::context (unused)
void check_options(void* /*context*/) {
    record("check_options");
}

/// Refuses the reserved game switches (Extension::switch_handler).
///
/// @param context Extension::context (unused)
/// @return null
const oa::app::command_line::SwitchHandler* switch_handler(void* /*context*/) {
    record("switch_handler");
    return nullptr;
}

/// Keeps the engine's wording (Extension::text).
///
/// @param context Extension::context (unused)
/// @param which the text asked for
/// @return null
const char* text(void* /*context*/, ExtensionText which) {
    record("text", text_name(which));
    return nullptr;
}

/// Keeps the host and services an overlay's create is given.
///
/// @param ctx the overlay's context
void keep_services(ScreenContext* ctx, void* /*state*/) {
    recorder().host = ctx->host;
    recorder().services = ctx->services;
}

/// Counts the registration of the screens; with --record-quit, registers an
/// overlay that only keeps the services (Extension::register_screens).
///
/// @param context Extension::context (unused)
/// @param[in,out] registry the runtime's registry; left as it is without
///        --record-quit
void register_screens(void* /*context*/, ScreenRegistry* registry) {
    record("register_screens");
    if (!recorder().quit)
        return;
    OverlayDesc overlay{};
    overlay.name = "recorder services";
    overlay.screen = kScreenAny;
    overlay.create = keep_services;
    (void)overlay_register(registry, &overlay);
}

/// Uses the kept screen services on the frames --record-quit names.
void use_services() {
    auto& state = recorder();
    if (!state.quit || state.services == nullptr)
        return;
    ++state.after_pump_frames;
    if (state.after_pump_frames == kServicesFrame) {
        state.services->stop_sounds(state.host);
        state.services->play_sound_alternate(state.host, "BGM");
        state.services->run_frontend(state.host);
        std::printf("recorder: stop_sounds, play_sound_alternate and run_frontend\n");
    } else if (state.after_pump_frames == kQuitFrame) {
        std::printf("recorder: quit %d\n", state.quit_status);
        std::fflush(stdout);
        state.services->quit(state.host, nullptr, state.quit_status);
    }
}

/// Leaves the frontend's launch values to the preferences (Extension::frontend_entry).
///
/// @param context Extension::context (unused)
/// @param[out] entry the values; left null
void frontend_entry(void* /*context*/, FrontendEntry& /*entry*/) {
    record("frontend_entry");
}

/// Leaves every frontend state to the engine (Extension::frontend_states).
///
/// @param context Extension::context (unused)
/// @param[out] handler the runtime's handler; left empty
void frontend_states(void* /*context*/, oa::ui::frontend_state::StateHandler& /*handler*/) {
    record("frontend_states");
}

/// Leaves MULTI to the engine's message box (Extension::select_multiplayer).
///
/// @param context Extension::context (unused)
/// @param[in,out] runtime the running app; left as it is
/// @return unavailable
MultiplayerSelection select_multiplayer(void* /*context*/, Runtime& /*runtime*/) {
    record("select_multiplayer");
    return MultiplayerSelection::unavailable;
}

/// Returns the recorder's frontend Game block (Extension::frontend_game).
///
/// @param context Extension::context (unused)
/// @return the block, zeroed as the engine's own starts
oa::Game* frontend_game(void* /*context*/) {
    record("frontend_game");
    return &recorder().frontend_game;
}

/// Tells that no launcher started the game (Extension::launched_by_service).
///
/// @param context Extension::context (unused)
/// @return false
bool launched_by_service(void* /*context*/) {
    record("launched_by_service");
    return false;
}

/// Leaves a new match's Game block to the engine (Extension::match_game).
///
/// @param context Extension::context (unused)
/// @param[in,out] game the new match's Game block; left as it is
void match_game(void* /*context*/, oa::Game& /*game*/) {
    record("match_game");
}

/// Shows no disconnect text (Extension::disconnect_text).
///
/// @param context Extension::context (unused)
/// @param reason the local player's reject reason (unused)
/// @return null
const char* disconnect_text(void* /*context*/, uint8_t /*reason*/) {
    record("disconnect_text");
    return nullptr;
}

/// Names no launcher (Extension::service_label).
///
/// @param context Extension::context (unused)
/// @return null
const char* service_label(void* /*context*/) {
    record("service_label");
    return nullptr;
}

} // namespace

// The recorder's hooks that take the runtime.
struct RuntimeExtension {
    /// Counts the start and calls the recorder's Runtime member (Extension::startup).
    ///
    /// @param context Extension::context (unused)
    /// @param[in,out] runtime the runtime being built
    static void startup(void* /*context*/, Runtime& runtime) {
        record("startup");
        const uint32_t calls = runtime.count_recorder_member_call();
        if (calls == runtime.recorder_member_calls_)
            record("runtime_member");
    }

    /// Counts the end of the start (Extension::ready).
    ///
    /// @param context Extension::context (unused)
    /// @param[in,out] runtime the runtime being built; left as it is
    static void ready(void* /*context*/, Runtime& /*runtime*/) { record("ready"); }

    /// Runs nothing of its own (Extension::run_mode).
    ///
    /// @param context Extension::context (unused)
    /// @param[in,out] runtime the running app; left as it is
    /// @param phase the point of Runtime::run
    /// @param[out] exit_code oa-game's exit status; left as it is
    /// @return false
    static bool
    run_mode(void* /*context*/, Runtime& /*runtime*/, RunPhase phase, int& /*exit_code*/) {
        record("run_mode", phase_name(phase));
        return false;
    }

    /// Leaves the first scene to the menu music (Extension::start_scene).
    ///
    /// @param context Extension::context (unused)
    /// @param[in,out] runtime the running app; left as it is
    /// @return false
    static bool start_scene(void* /*context*/, Runtime& /*runtime*/) {
        record("start_scene");
        return false;
    }

    /// Counts the end of the main loop (Extension::shutdown).
    ///
    /// @param context Extension::context (unused)
    /// @param[in,out] runtime the running app; left as it is
    static void shutdown(void* /*context*/, Runtime& /*runtime*/) { record("shutdown"); }

    /// Says that --check-multiplayer-menu ran, then runs the engine's own check,
    /// whose click on MULTI reaches select_multiplayer (Extension::check_multiplayer_menu).
    ///
    /// @param context Extension::context (unused)
    /// @param[in,out] runtime the running app, whose main menu the check drives
    static void check_multiplayer_menu(void* /*context*/, Runtime& runtime) {
        record("check_multiplayer_menu");
        std::printf("recorder: --check-multiplayer-menu\n");
        runtime.check_multiplayer_unavailable();
    }

    /// Reports no session of its own (Extension::state).
    ///
    /// @param context Extension::context (unused)
    /// @param runtime the running app (unused)
    /// @return 0
    static uint32_t state(void* /*context*/, const Runtime& /*runtime*/) {
        record("state");
        return 0;
    }

    /// Counts a frame stage (Extension::frame).
    ///
    /// @param context Extension::context (unused)
    /// @param[in,out] runtime the running app; left as it is
    /// @param stage the point of the frame
    static void frame(void* /*context*/, Runtime& /*runtime*/, FrameStage stage) {
        record("frame", stage_name(stage));
        if (stage == FrameStage::after_pump)
            use_services();
    }

    /// Leaves the simulation step to the engine (Extension::simulation_step).
    ///
    /// @param context Extension::context (unused)
    /// @param[in,out] runtime the running app; left as it is
    /// @return false
    static bool simulation_step(void* /*context*/, Runtime& /*runtime*/) {
        record("simulation_step");
        return false;
    }

    /// Lets the finished match move on (Extension::outcome_ready).
    ///
    /// @param context Extension::context (unused)
    /// @param[in,out] runtime the running app; left as it is
    /// @return true
    static bool outcome_ready(void* /*context*/, Runtime& /*runtime*/) {
        record("outcome_ready");
        return true;
    }

    /// Counts a match event (Extension::match_event).
    ///
    /// @param context Extension::context (unused)
    /// @param[in,out] runtime the running app; left as it is
    /// @param event what happened
    static void match_event(void* /*context*/, Runtime& /*runtime*/, MatchEvent event) {
        record("match_event", event_name(event));
    }

    /// Leaves a console gift to the engine (Extension::give_resources).
    ///
    /// @param context Extension::context (unused)
    /// @param[in,out] runtime the running app; left as it is
    /// @param from the giving player's index (unused)
    /// @param to the receiving player's index (unused)
    /// @param amount the amount given (unused)
    /// @param metal true for metal, false for energy (unused)
    /// @return false
    static bool give_resources(
        void* /*context*/,
        Runtime& /*runtime*/,
        uint8_t /*from*/,
        uint8_t /*to*/,
        float /*amount*/,
        bool /*metal*/
    ) {
        record("give_resources");
        return false;
    }

    /// Leaves the message log's hooks to the engine (Extension::message_hooks).
    ///
    /// @param context Extension::context (unused)
    /// @param[in,out] runtime the running app; left as it is
    /// @param[in,out] hooks the hooks being built; left as the engine filled them
    static void message_hooks(
        void* /*context*/, Runtime& /*runtime*/, oa::sim::messages::Hooks& /*hooks*/
    ) {
        record("message_hooks");
    }

    /// Leaves the announcement to the engine (Extension::player_gone).
    ///
    /// @param context Extension::context (unused)
    /// @param[in,out] runtime the running app; left as it is
    /// @param[in,out] world the match world; left as it is
    /// @param player the player who lost its last unit (unused)
    /// @return false
    static bool player_gone(
        void* /*context*/, Runtime& /*runtime*/, oa::World& /*world*/, const oa::Player& /*player*/
    ) {
        record("player_gone");
        return false;
    }

    /// Leaves the console's host to the engine (Extension::console_host).
    ///
    /// @param context Extension::context (unused)
    /// @param[in,out] runtime the running app; left as it is
    /// @param[in,out] host the console's host; left as the engine filled it
    static void console_host(
        void* /*context*/, Runtime& /*runtime*/, oa::ui::console::ConsoleHost& /*host*/
    ) {
        record("console_host");
    }

    /// Types no console line of its own (Extension::check_console).
    ///
    /// @param context Extension::context (unused)
    /// @param[in,out] runtime the running app; left as it is
    /// @param enter_line types one console line (unused)
    /// @param user the first argument of enter_line (unused)
    static void check_console(
        void* /*context*/,
        Runtime& /*runtime*/,
        void (* /*enter_line*/)(void* user, const char* line),
        void* /*user*/
    ) {
        record("check_console");
    }

    /// Draws nothing over the loading screen (Extension::draw_loading).
    ///
    /// @param context Extension::context (unused)
    /// @param[in,out] runtime the running app; left as it is
    /// @param[in,out] target the locked display surface; left as it is
    /// @param font the GUI font, or null (unused)
    static void draw_loading(
        void* /*context*/,
        Runtime& /*runtime*/,
        oa::Surface& /*target*/,
        const oa::present::GafSprites* /*font*/
    ) {
        record("draw_loading");
    }

    /// Draws nothing in the match HUD (Extension::draw_match_hud).
    ///
    /// @param context Extension::context (unused)
    /// @param[in,out] runtime the running app; left as it is
    static void draw_match_hud(void* /*context*/, Runtime& /*runtime*/) {
        record("draw_match_hud");
    }

    /// Draws nothing over the battlefield (Extension::draw_match_overlay).
    ///
    /// @param context Extension::context (unused)
    /// @param[in,out] runtime the running app; left as it is
    /// @param overlay the battlefield and its painter (unused)
    static void draw_match_overlay(
        void* /*context*/, Runtime& /*runtime*/, const MatchOverlay& /*overlay*/
    ) {
        record("draw_match_overlay");
    }

    /// Keeps the pause on this machine (Extension::pause_changed).
    ///
    /// @param context Extension::context (unused)
    /// @param[in,out] runtime the running app; left as it is
    /// @param paused the pause bit after the flip
    static void pause_changed(void* /*context*/, Runtime& /*runtime*/, bool paused) {
        record("pause_changed", paused ? "on" : "off");
    }

    /// Counts a change of the loading screen's rows (Extension::load_progress).
    ///
    /// @param context Extension::context (unused)
    /// @param[in,out] runtime the running app; left as it is
    /// @param rows the loading screen's rows (unused)
    /// @param row_count how many rows `rows` holds (unused)
    static void load_progress(
        void* /*context*/, Runtime& /*runtime*/, const uint8_t* /*rows*/, size_t /*row_count*/
    ) {
        record("load_progress");
    }

    /// Leaves the team panels' host empty (Extension::team_panel_host).
    ///
    /// @param context Extension::context (unused)
    /// @param[in,out] runtime the running app; left as it is
    /// @param[in,out] host the panels' host; left with every entry null
    static void team_panel_host(
        void* /*context*/, Runtime& /*runtime*/, oa::ui::hud::TeamPanelHost& /*host*/
    ) {
        record("team_panel_host");
    }

    /// Leaves a close request to the engine (Extension::close_requested).
    ///
    /// @param context Extension::context (unused)
    /// @param[in,out] runtime the running app; left as it is
    /// @return false
    static bool close_requested(void* /*context*/, Runtime& /*runtime*/) {
        record("close_requested");
        return false;
    }

    /// Keeps a speed the local player set on this machine (Extension::speed_changed).
    ///
    /// @param context Extension::context (unused)
    /// @param[in,out] runtime the running app; left as it is
    /// @param speed the match's game speed now (unused)
    static void speed_changed(void* /*context*/, Runtime& /*runtime*/, uint16_t /*speed*/) {
        record("speed_changed");
    }

    /// Counts an application mode the frontend set (Extension::app_mode_set).
    ///
    /// @param context Extension::context (unused)
    /// @param[in,out] runtime the running app; left as it is
    /// @param mode the mode set (unused)
    static void app_mode_set(void* /*context*/, Runtime& /*runtime*/, int32_t /*mode*/) {
        record("app_mode_set");
    }
};

} // namespace oa::app

void oa_extensions_init(oa::app::Extension* table) {
    using namespace oa::app;
    *table = {};
    table->context = &recorder();
    table->take_option = take_option;
    table->check_options = check_options;
    table->switch_handler = switch_handler;
    table->text = text;
    table->startup = RuntimeExtension::startup;
    table->register_screens = register_screens;
    table->ready = RuntimeExtension::ready;
    table->frontend_entry = frontend_entry;
    table->frontend_states = frontend_states;
    table->run_mode = RuntimeExtension::run_mode;
    table->start_scene = RuntimeExtension::start_scene;
    table->shutdown = RuntimeExtension::shutdown;
    table->select_multiplayer = select_multiplayer;
    table->frontend_game = frontend_game;
    table->launched_by_service = launched_by_service;
    table->check_multiplayer_menu = RuntimeExtension::check_multiplayer_menu;
    table->state = RuntimeExtension::state;
    table->frame = RuntimeExtension::frame;
    table->simulation_step = RuntimeExtension::simulation_step;
    table->outcome_ready = RuntimeExtension::outcome_ready;
    table->match_game = match_game;
    table->match_event = RuntimeExtension::match_event;
    table->disconnect_text = disconnect_text;
    table->give_resources = RuntimeExtension::give_resources;
    table->message_hooks = RuntimeExtension::message_hooks;
    table->player_gone = RuntimeExtension::player_gone;
    table->console_host = RuntimeExtension::console_host;
    table->check_console = RuntimeExtension::check_console;
    table->draw_loading = RuntimeExtension::draw_loading;
    table->draw_match_hud = RuntimeExtension::draw_match_hud;
    table->draw_match_overlay = RuntimeExtension::draw_match_overlay;
    table->pause_changed = RuntimeExtension::pause_changed;
    table->load_progress = RuntimeExtension::load_progress;
    table->team_panel_host = RuntimeExtension::team_panel_host;
    table->close_requested = RuntimeExtension::close_requested;
    table->service_label = service_label;
    table->speed_changed = RuntimeExtension::speed_changed;
    table->app_mode_set = RuntimeExtension::app_mode_set;
    // A hook left unset here would fall back to the engine's behaviour
    // unrecorded: stop before anything runs.
    uintptr_t words[1 + kHookCount]{};
    std::memcpy(words, table, sizeof words);
    for (std::size_t hook = 1; hook <= kHookCount; ++hook) {
        if (words[hook] == 0) {
            std::fprintf(stderr, "recorder: hook %zu of the extension table is not set\n", hook);
            std::exit(1);
        }
    }
    std::atexit(write_record);
}
