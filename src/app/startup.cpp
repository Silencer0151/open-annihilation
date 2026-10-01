// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Command-line parsing for oa-game.
#include "oa/app/app.hpp"
#include "oa/app/extension.hpp"
#include "oa/app/game_directory.hpp"
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace oa::app {
namespace {

constexpr std::size_t kMaximumRunFrames = 10'000'000;

[[nodiscard]] uint32_t parse_seed(std::string_view text) {
    uint32_t value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (text.empty() || result.ec != std::errc{} || result.ptr != text.data() + text.size())
        throw std::runtime_error("--seed expects an integer from 0 through 4294967295");
    return value;
}

// The arguments after a long option, as OptionValues hands them out.
struct ArgumentCursor {
    int argc{};
    char** argv{};
    int* index{};
    std::string_view name;
};

const char* next_value(void* arguments) {
    auto& cursor = *static_cast<ArgumentCursor*>(arguments);
    if (++*cursor.index >= cursor.argc || std::string_view(cursor.argv[*cursor.index]).empty())
        throw std::runtime_error(std::string(cursor.name) + " requires a value");
    return cursor.argv[*cursor.index];
}

// The extension's text for `which`, or `fallback` when it has none.
const char* extension_text(const Extension& extension, ExtensionText which, const char* fallback) {
    const char* text =
        extension.text != nullptr ? extension.text(extension.context, which) : nullptr;
    return text != nullptr ? text : fallback;
}

/// Returns the frames a second a --max-fps or --frame-rate value names.
///
/// Throws std::runtime_error with `expected` unless the whole text is a
/// decimal integer from `least` through kHighestFrameRate, or 0 when
/// `zero_allowed`.
///
/// @param text the option's value
/// @param least the lowest rate above 0 the option takes
/// @param zero_allowed the option takes 0 as well
/// @param expected the message that says what the option takes
/// @return the rate
[[nodiscard]] uint32_t
parse_frame_rate(std::string_view text, uint32_t least, bool zero_allowed, const char* expected) {
    uint32_t value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    const bool in_range =
        (value >= least && value <= kHighestFrameRate) || (zero_allowed && value == 0);
    if (text.empty() || result.ec != std::errc{} || result.ptr != text.data() + text.size() ||
        !in_range)
        throw std::runtime_error(expected);
    return value;
}

/// Returns the showcase a --showcase value names.
///
/// Throws std::runtime_error naming the showcases for any other value.
///
/// @param text the option's value
/// @return the showcase
[[nodiscard]] Showcase parse_showcase(std::string_view text) {
    if (text == "arm-first-mission")
        return Showcase::arm_first_mission;
    throw std::runtime_error("--showcase knows one showcase: arm-first-mission");
}

/// Returns the chunks a --chunks value names: A-B, both included, or A alone.
///
/// Throws std::runtime_error for any other value.
///
/// @param text the option's value
/// @return the first and last chunk, counted from 0
[[nodiscard]] std::pair<uint32_t, uint32_t> parse_chunks(std::string_view text) {
    const auto number = [&](std::string_view part) {
        uint32_t value = 0;
        const auto result = std::from_chars(part.data(), part.data() + part.size(), value);
        if (part.empty() || result.ec != std::errc{} || result.ptr != part.data() + part.size())
            throw std::runtime_error(
                "--chunks expects A-B or A: chunk numbers from 0, the first not after the last"
            );
        return value;
    };
    const auto separator = text.find('-');
    const uint32_t first = number(text.substr(0, separator));
    const uint32_t last =
        separator == std::string_view::npos ? first : number(text.substr(separator + 1));
    if (last < first)
        throw std::runtime_error(
            "--chunks expects A-B or A: chunk numbers from 0, the first not after the last"
        );
    return {first, last};
}

/// Checks the director's options against the others, and has a director
/// run headless: --generate-script and --render-script run on their own,
/// on the fixed clock and seed, so that a script's analysis and its render
/// replay the recording alike.
///
/// Throws std::runtime_error naming the options that cannot be used
/// together.
///
/// @param[in,out] options the parsed options; a director run gets
///        headless_check and skip_intro
void check_director_options(Options& options) {
    const bool generate = !options.generate_script.empty();
    const bool render = !options.render_script.empty();
    if (!generate && !render) {
        if (!options.director_output.empty())
            throw std::runtime_error("--output needs --generate-script or --render-script");
        if (options.director_chunks)
            throw std::runtime_error("--chunks needs --render-script");
        return;
    }
    if (generate && render)
        throw std::runtime_error("--generate-script and --render-script cannot be used together");
    if (generate && options.director_chunks)
        throw std::runtime_error("--chunks needs --render-script");
    if (render && options.window_resolution)
        throw std::runtime_error(
            "--render-script takes the frame size from the script, not from --resolution"
        );
    const char* run = generate ? "--generate-script" : "--render-script";
    // Options of other runs, and those that would change the match, its
    // seed or its camera.
    const std::pair<bool, const char*> refused[] = {
        {options.seed.has_value(), "--seed"},
        {!options.capture_video.empty(), "--capture-video"},
        {options.showcase != Showcase::none, "--showcase"},
        {options.benchmark_frames.has_value(), "--benchmark"},
        {options.frame_limit.has_value(), "--frames"},
        {!options.snapshot.empty(), "--snapshot"},
        {options.match_ticks.has_value(), "--match-ticks"},
        {options.frame_rate.has_value(), "--frame-rate"},
        {!options.campaign.empty() || options.campaign_mission.has_value(), "--campaign"},
        {!options.load_file.empty(), "--load"},
        {options.save_after.has_value(), "--save-after"},
        {!options.save_file.empty(), "--save-file"},
        {options.camera.has_value(), "--camera"},
        {options.match_zoom != kDefaultBattlefieldZoom, "--zoom"},
        {options.combat_units != 0, "--combat"},
        {options.reclaim_check, "--reclaim-check"},
        {options.give_orders, "--give-orders"},
        {options.check_navigation, "--check-navigation"},
        {options.check_match_dialogs, "--check-match-dialogs"},
        {options.check_load_save, "--check-load-save"},
        {options.check_frontend_controls, "--check-frontend-controls"},
        {options.check_scroll_bars, "--check-scroll-bars"},
        {options.check_briefing_narration, "--check-briefing-narration"},
        {options.check_match_layers, "--check-match-layers"},
        {options.check_match_orders, "--check-match-orders"},
        {options.check_factory_orders, "--check-factory-orders"},
        {options.check_download_builds, "--check-download-builds"},
        {options.check_kill_board, "--check-kill-board"},
        {options.check_patrol_reclaim, "--check-patrol-reclaim"},
        {options.check_reclaim_cursor, "--check-reclaim-cursor"},
        {options.check_pointer_interfaces, "--check-pointer-interfaces"},
        {options.check_multiplayer_menu, "--check-multiplayer-menu"},
        {options.check_director_view, "--check-director-view"},
        {options.check_director_render, "--check-director-render"},
        {options.check_interpolation, "--check-interpolation"},
    };
    for (const auto& [given, name] : refused)
        if (given)
            throw std::runtime_error(std::string(run) + " cannot be used with " + name);
    options.headless_check = true;
    options.skip_intro = true;
}

} // namespace

[[nodiscard]] std::size_t parse_count(std::string_view text) {
    std::size_t value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (text.empty() || result.ec != std::errc{} || result.ptr != text.data() + text.size() ||
        value > kMaximumRunFrames)
        throw std::runtime_error("frame counts must be integers from 0 through 10,000,000");
    return value;
}

[[nodiscard]] Options parse_options(int argc, char** argv, const Extension& extension) {
    Options result;
    std::string joined_line;
    uint32_t extension_effects = 0;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        auto value = [&](std::string_view name) -> std::string_view {
            ArgumentCursor cursor{argc, argv, &index, name};
            return next_value(&cursor);
        };
        if (argument == "--game-dir")
            result.game_dir = path_from_utf8(value(argument));
        else if (argument == "--choose-game-dir")
            result.choose_game_dir = true;
        else if (argument == "--archive")
            result.archives.emplace_back(value(argument));
        else if (argument == "--snapshot")
            result.snapshot = value(argument);
        else if (argument == "--preferences-file")
            result.preferences_file = fs::path(value(argument));
        else if (argument == "--data-dir")
            result.data_dir = path_from_utf8(value(argument));
        else if (argument == "--frames")
            result.frame_limit = parse_count(value(argument));
        else if (argument == "--benchmark")
            result.benchmark_frames = parse_count(value(argument));
        else if (argument == "--match-ticks")
            result.match_ticks = parse_count(value(argument));
        else if (argument == "--max-fps")
            result.max_frames_per_second = parse_frame_rate(
                value(argument),
                kLowestMaxFramesPerSecond,
                true,
                "--max-fps expects 0 for no limit, or frames a second from 40 through 1000"
            );
        else if (argument == "--frame-rate")
            result.frame_rate = parse_frame_rate(
                value(argument),
                1,
                false,
                "--frame-rate expects frames a second from 1 through 1000"
            );
        else if (argument == "--frame-log")
            result.frame_log = path_from_utf8(value(argument));
        else if (argument == "--scroll-camera")
            result.scroll_camera = true;
        else if (argument == "--march")
            result.march = true;
        else if (argument == "--follow")
            result.follow = true;
        else if (argument == "--campaign")
            result.campaign = value(argument);
        else if (argument == "--mission")
            result.campaign_mission = parse_count(value(argument));
        else if (argument == "--past-outcome")
            result.campaign_past_outcome = true;
        else if (argument == "--restart-at")
            result.campaign_restart_tick = parse_count(value(argument));
        else if (argument == "--combat")
            result.combat_units = parse_count(value(argument));
        else if (argument == "--save-after")
            result.save_after = parse_count(value(argument));
        else if (argument == "--save-file")
            result.save_file = value(argument);
        else if (argument == "--load")
            result.load_file = value(argument);
        else if (argument == "--give-orders")
            result.give_orders = true;
        else if (argument == "--zoom") {
            const auto text = std::string(value(argument));
            char* end = nullptr;
            result.match_zoom = std::strtof(text.c_str(), &end);
            if (end == text.c_str() || *end != '\0' || !(result.match_zoom > 0.0F))
                throw std::runtime_error("--zoom expects a positive number");
        } else if (argument == "--reclaim-check")
            result.reclaim_check = true;
        else if (argument == "--resolution") {
            const auto text = std::string(value(argument));
            const auto separator = text.find('x');
            if (separator == std::string::npos)
                throw std::runtime_error("--resolution expects WIDTHxHEIGHT");
            result.match_width = static_cast<int>(parse_count(text.substr(0, separator)));
            result.match_height = static_cast<int>(parse_count(text.substr(separator + 1)));
            if (result.match_width <= 0 || result.match_height <= 0)
                throw std::runtime_error("--resolution expects a width and height above 0");
            result.window_resolution = true;
        } else if (argument == "--camera") {
            const auto text = std::string(value(argument));
            const auto separator = text.find(',');
            if (separator == std::string::npos)
                throw std::runtime_error("--camera expects X,Z");
            result.camera = {
                static_cast<int>(parse_count(text.substr(0, separator))),
                static_cast<int>(parse_count(text.substr(separator + 1)))
            };
        } else if (argument == "--skip-intro")
            result.skip_intro = true;
        else if (argument == "--headless-check")
            result.headless_check = true;
        else if (argument == "--mute")
            result.mute = true;
        else if (argument == "--check-navigation")
            result.check_navigation = true;
        else if (argument == "--check-match-dialogs")
            result.check_match_dialogs = true;
        else if (argument == "--check-load-save")
            result.check_load_save = true;
        else if (argument == "--check-frontend-controls")
            result.check_frontend_controls = true;
        else if (argument == "--check-scroll-bars")
            result.check_scroll_bars = true;
        else if (argument == "--check-briefing-narration")
            result.check_briefing_narration = true;
        else if (argument == "--check-match-layers")
            result.check_match_layers = true;
        else if (argument == "--check-match-orders")
            result.check_match_orders = true;
        else if (argument == "--check-factory-orders")
            result.check_factory_orders = true;
        else if (argument == "--check-download-builds")
            result.check_download_builds = true;
        else if (argument == "--check-kill-board")
            result.check_kill_board = true;
        else if (argument == "--check-patrol-reclaim")
            result.check_patrol_reclaim = true;
        else if (argument == "--check-reclaim-cursor")
            result.check_reclaim_cursor = true;
        else if (argument == "--check-pointer-interfaces")
            result.check_pointer_interfaces = true;
        else if (argument == "--check-multiplayer-menu")
            result.check_multiplayer_menu = true;
        else if (argument == "--check-director-view")
            result.check_director_view = true;
        else if (argument == "--check-director-render")
            result.check_director_render = true;
        else if (argument == "--check-interpolation")
            result.check_interpolation = true;
        else if (argument == "--generate-script")
            result.generate_script = path_from_utf8(value(argument));
        else if (argument == "--render-script")
            result.render_script = path_from_utf8(value(argument));
        else if (argument == "--output")
            result.director_output = path_from_utf8(value(argument));
        else if (argument == "--chunks")
            result.director_chunks = parse_chunks(value(argument));
        else if (argument == "--trace-input")
            result.trace_input = true;
        else if (argument == "--debug-order-lines")
            result.debug_order_lines = true;
        else if (argument == "--trace-digest")
            result.trace_digest = value(argument);
        else if (argument == "--trace-units")
            result.trace_units = value(argument);
        else if (argument == "--seed")
            result.seed = parse_seed(value(argument));
        else if (argument == "--capture-video")
            result.capture_video = path_from_utf8(value(argument));
        else if (argument == "--showcase")
            result.showcase = parse_showcase(value(argument));
        // A bare -h stays the help flag, so the game's "-h NAME" (name as
        // the next argument) must be written -hNAME here.
        else if (argument == "--help" || argument == "-h") {
            std::cout << "usage: open-annihilation [--game-dir PATH | --choose-game-dir] "
                         "[--archive PATH]... "
                         "[--skip-intro] [--frames N] [--headless-check] "
                         "[--snapshot PATH.ppm] [--preferences-file PATH] [--data-dir PATH] "
                         "[--mute] "
                         "[--check-navigation] [--check-match-dialogs] [--check-match-layers] "
                         "[--check-match-orders] [--check-factory-orders] "
                         "[--check-download-builds] [--check-kill-board] "
                         "[--check-patrol-reclaim] [--check-reclaim-cursor] "
                         "[--check-pointer-interfaces] "
                         "[--check-multiplayer-menu] "
                         "[--check-load-save] [--check-frontend-controls] "
                         "[--check-scroll-bars] "
                         "[--check-briefing-narration] [--check-director-view] "
                         "[--check-director-render] [--check-interpolation] "
                         "[--trace-input] "
                      << extension_text(extension, ExtensionText::usage_checks, "")
                      << "[--debug-order-lines] "
                         "[--max-fps N] "
                         "[--benchmark FRAMES] [--match-ticks N "
                         "[--frame-rate FPS [--frame-log FILE] [--scroll-camera] [--march] "
                         "[--follow]]] "
                         "[--campaign NAME --mission N [--past-outcome] [--restart-at TICK]] "
                         "[--resolution WxH] "
                         "[--zoom FACTOR] [--combat UNITS] [--reclaim-check] [--camera X,Z] "
                      << extension_text(extension, ExtensionText::usage_runs, "")
                      << "[--save-after TICK] "
                         "[--save-file PATH.sav] [--load PATH.sav] [--give-orders] [--seed N] "
                         "[--trace-digest FILE] [--trace-units FILE] "
                         "[--capture-video PATH.mp4] [--showcase arm-first-mission] "
                         "[--generate-script RECORDING [--output PATH.oascript|PATH.oamovie] "
                         "[--resolution WxH]] "
                         "[--render-script PATH.oascript|PATH.oamovie [--output DIR] "
                         "[--chunks A-B]] "
                         "[game switches such as "
                      << extension_text(extension, ExtensionText::usage_switches, "")
                      << "-d -s] "
                         "[LANGUAGE]\n"
                         "--generate-script plans a director script from a recording an "
                         "extension of this build replays;\n"
                         "--render-script renders a director script, or a bundle of one and "
                         "its recording, to video (docs/director.md).\n"
                      << extension_text(
                             extension,
                             ExtensionText::usage_note,
                             "This release has no multiplayer; --check-multiplayer-menu checks "
                             "the message box MULTI opens instead.\n"
                         );
            std::exit(0);
        } else if (argument.starts_with("--")) {
            ArgumentCursor cursor{argc, argv, &index, argument};
            const OptionValues values{&cursor, next_value};
            uint32_t effects = 0;
            if (extension.take_option == nullptr ||
                !extension.take_option(extension.context, argv[index], values, effects))
                throw std::runtime_error("unknown option: " + std::string(argument));
            if ((effects & option_effect::headless_check) != 0)
                result.headless_check = true;
            if ((effects & option_effect::skip_intro) != 0)
                result.skip_intro = true;
            extension_effects |= effects;
        } else {
            if (!joined_line.empty())
                joined_line += ' ';
            joined_line += argument;
        }
    }
    const oa::app::command_line::SwitchHandler* switches =
        extension.switch_handler != nullptr ? extension.switch_handler(extension.context) : nullptr;
    switch (oa::app::command_line::parse(joined_line.c_str(), result.launch, switches)) {
    case oa::app::command_line::Status::run:
        break;
    case oa::app::command_line::Status::register_application:
        throw std::runtime_error(extension_text(
            extension,
            ExtensionText::register_switch,
            "-r registers the game for multiplayer, which this release does not include"
        ));
    case oa::app::command_line::Status::line_too_long:
        throw std::runtime_error("the game switches exceed the command-line limit");
    case oa::app::command_line::Status::argument_too_long:
        throw std::runtime_error("the language argument is too long");
    case oa::app::command_line::Status::unavailable_switch:
        throw std::runtime_error(
            std::string("-") + result.launch.unavailable_switch + " is not handled by this build"
        );
    }
    if (result.campaign_mission.has_value() != !result.campaign.empty())
        throw std::runtime_error("--campaign and --mission are used together");
    if (result.campaign_restart_tick && !result.campaign_mission)
        throw std::runtime_error("--restart-at needs --campaign and --mission");
    if (!result.trace_units.empty() && result.trace_digest.empty())
        throw std::runtime_error("--trace-units needs --trace-digest");
    if (result.frame_rate && (!result.match_ticks || result.campaign_mission || result.save_after ||
                              !result.load_file.empty()))
        throw std::runtime_error("--frame-rate draws a headless skirmish of --match-ticks ticks");
    if (!result.frame_rate &&
        (!result.frame_log.empty() || result.scroll_camera || result.march || result.follow))
        throw std::runtime_error(
            "--frame-log, --scroll-camera, --march and --follow need --frame-rate"
        );
    if (extension.check_options != nullptr)
        extension.check_options(extension.context);
    if (const char* env = std::getenv("OA_DEBUG_ORDER_LINES"); env != nullptr && env[0] != '\0')
        result.debug_order_lines = true;
    // The director view check runs headless, where SDL is never started.
    if (result.check_director_view || result.check_director_render || result.check_interpolation) {
        result.headless_check = true;
        result.skip_intro = true;
    }
    check_director_options(result);
    result.fixed_clock =
        result.headless_check || result.check_match_layers || result.check_match_dialogs ||
        result.check_load_save || result.check_frontend_controls || result.check_scroll_bars ||
        result.check_match_orders || result.check_factory_orders || result.check_download_builds ||
        result.check_kill_board || result.check_patrol_reclaim || result.check_reclaim_cursor ||
        result.check_pointer_interfaces || result.check_director_view ||
        result.check_director_render || result.check_interpolation;
    // A capture and a showcase need the application's own loop and window,
    // which checks and benchmarks do not run.
    const bool check_run = result.fixed_clock || result.check_navigation ||
                           result.check_multiplayer_menu || result.check_briefing_narration ||
                           result.benchmark_frames;
    if (!result.capture_video.empty() && check_run)
        throw std::runtime_error(
            "--capture-video captures the game or a --showcase, not a check or benchmark"
        );
    if (result.showcase != Showcase::none && check_run)
        throw std::runtime_error("--showcase plays in a window; it is not a check or benchmark");
    // A showcase plays its mission from the same random state on every run.
    if (result.showcase != Showcase::none && !result.seed)
        result.seed = kFixedRandomSeed;
    result.unattended = result.fixed_clock || result.check_navigation ||
                        result.check_multiplayer_menu || result.check_briefing_narration ||
                        result.benchmark_frames || result.frame_limit || !result.snapshot.empty() ||
                        result.showcase != Showcase::none;
    if ((extension_effects & option_effect::unattended) != 0)
        result.unattended = true;
#ifdef _WIN32
    // On Windows a player's run starts full screen; -d, with any suffix, keeps
    // a window, and so do unattended runs and video captures.
    result.start_full_screen =
        result.launch.display_option == 0 && !result.unattended && result.capture_video.empty();
#endif
    if (result.choose_game_dir && !result.game_dir.empty())
        throw std::runtime_error("--choose-game-dir and --game-dir cannot be used together");
    if (result.choose_game_dir && result.unattended)
        throw std::runtime_error(
            "--choose-game-dir opens a dialog, which headless, check, benchmark, --frames and "
            "--snapshot runs never show"
        );
    return result;
}

} // namespace oa::app
