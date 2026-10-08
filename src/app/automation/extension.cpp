// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The automation endpoint's extension, filled by
// oa_extension_init_automation. Without --fark every hook returns at once:
// no socket is opened and no file written. With it, the first runtime's
// start refuses the engine options that run no main loop, opens the
// endpoint and writes its address and token; the endpoint then serves its
// client from the frame hook of every runtime the process builds, so that
// a client stays connected through a switch of the mod, and sends its
// events from the frame hook, from load_progress while a match loads and
// no frame runs, and from match_event as a match is torn down. It reads the
// game through the check host, the automation host and the multiplayer
// screens' lobby and never changes the simulation: its hooks that could
// take over a simulation step, resources, a departing player or a close
// request are left null, and so is the hook that tells the session's kind.
#include "endpoint.hpp"
#include "options.hpp"
#include "serve_reports.hpp"

#include "oa/app/app.hpp"
#include "oa/app/check_host.hpp"
#include "oa/app/extension.hpp"
#include "oa/app/game_directory.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>

// The version of the engine's extension table these hooks follow. A build
// against a table of another version stops here until the engine's change
// has been read and the hooks follow it. Version 14 adds functions an
// extension calls rather than hooks (extension.hpp lists them).
// These hooks call none of them.
constexpr uint32_t kExtensionApiVersionFollowed = 14;
static_assert(
    oa::app::extension_api_version == kExtensionApiVersionFollowed,
    "the engine's extension table changed: follow its change, then raise "
    "kExtensionApiVersionFollowed"
);

namespace oa::app::automation {
namespace {

// What the command line asked of the endpoint, and the endpoint, for the
// whole process.
struct AutomationContext {
    EndpointOptions options{};
    Endpoint endpoint;
};

/// Returns the process's automation context.
///
/// @return the one context, made on first use
AutomationContext& automation_context() {
    static AutomationContext context;
    return context;
}

/// Returns the file the endpoint's address and token go to: --fark-file's,
/// else automation.json in the folder of the preferences file.
///
/// @param options what the command line asked of the endpoint
/// @param run the run's options
/// @return the file
std::filesystem::path endpoint_file(const EndpointOptions& options, const oa::app::Options& run) {
    if (options.file)
        return *options.file;
    return preference_file(run.preferences_file).parent_path() /
           std::string(default_endpoint_file_name);
}

/// Opens the endpoint as the first runtime starts (Extension::startup).
///
/// A run whose options run no main loop, or own it, is refused with an
/// error the engine raises as its own.
///
/// @param runtime the runtime being built
void startup(void* /*context*/, Runtime& runtime) {
    auto& automation = automation_context();
    if (!automation.options.enabled || automation.endpoint.listening())
        return;
    const auto& run = runtime_options(runtime);
    if (const std::string_view conflict = conflicting_option(run); !conflict.empty())
        throw std::runtime_error(
            "--fark cannot be used with " + std::string(conflict) +
            ": the automation endpoint is served from the game's main loop"
        );
    automation.endpoint.open(automation.options.listen, endpoint_file(automation.options, run));
}

/// Serves the endpoint at each stage of a frame (Extension::frame).
///
/// @param[in,out] runtime the running game
/// @param stage the stage of the frame
void frame(void* /*context*/, Runtime& runtime, FrameStage stage) {
    auto& automation = automation_context();
    if (automation.options.enabled)
        automation.endpoint.serve(runtime, stage);
}

/// Sends the events of a match's loading, while no frame runs (Extension::load_progress).
///
/// @param[in,out] runtime the running game
/// @param rows the loading screen's rows, each 0 to 100 percent
/// @param row_count how many rows `rows` holds
void load_progress(void* /*context*/, Runtime& runtime, const uint8_t* rows, size_t row_count) {
    auto& automation = automation_context();
    if (!automation.options.enabled || rows == nullptr)
        return;
    const std::span<const uint8_t> progress(rows, row_count);
    automation.endpoint.serve_between_frames(runtime, [progress](Endpoint& endpoint) {
        send_loading_events(endpoint, progress);
    });
}

/// Sends the end of the running match as it is torn down (Extension::match_event).
///
/// @param[in,out] runtime the running game
/// @param event what happened to the match
void match_event(void* /*context*/, Runtime& runtime, MatchEvent event) {
    auto& automation = automation_context();
    if (automation.options.enabled && event == MatchEvent::torn_down)
        automation.endpoint.serve_between_frames(runtime, send_match_gone);
}

} // namespace
} // namespace oa::app::automation

/// Fills the automation endpoint's extension table.
///
/// @param[in,out] table the table, zeroed
void oa_extension_init_automation(oa::app::Extension* table) {
    auto& context = oa::app::automation::automation_context();
    oa::app::automation::fill_option_hooks(*table, context.options);
    table->startup = oa::app::automation::startup;
    table->frame = oa::app::automation::frame;
    table->load_progress = oa::app::automation::load_progress;
    table->match_event = oa::app::automation::match_event;
}
