// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// --check-language-switch: the language chosen in the settings over the main
// menu, and over a skirmish's in-game menu, from Simplified Chinese, the
// language the run starts in, to English, German and back to Chinese. After
// each change, with the settings closed by OK, the screen under them shows
// what it shows opened again in the language, and back in Chinese what it
// showed as the run started in it.

#include "engine_settings_match_host.hpp"
#include "engine_settings_menu_host.hpp"

#include "oa/app/runtime.hpp"
#include "oa/platform/preferences.hpp"
#include "oa/ui/engine_settings.hpp"
#include "oa/ui/engine_settings/dialog.hpp"
#include "oa/ui/hud/resource_bar.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>

namespace oa::app {

namespace {

namespace settings = oa::ui::engine_settings;

/// The language the run starts in, which the check's preferences file chooses.
constexpr std::string_view kStartTag = "zh-Hans";
/// The languages the settings choose in turn; the last is the one the run
/// starts in.
constexpr std::array<std::string_view, 3> kChosenTags{"en", "de", kStartTag};
/// The most steps the bars' numbers take to come to rest.
constexpr int kMostReadoutSteps = 10000;
/// A point of the main menu's picture over none of its buttons, where the
/// pointer rests.
constexpr oa::ui::display_layout::Point kRestingPointer{4, 240};

/// Stops the check with a reason.
///
/// @param what what went wrong
[[noreturn]] void fail(const std::string& what) {
    throw std::runtime_error("language switch check: " + what);
}

/// Counts the pixels in which two frames differ.
///
/// @param first one frame
/// @param second the other frame
/// @return the differing pixels; every pixel and one more when the sizes differ
std::size_t differing_pixels(const renderer::Surface& first, const renderer::Surface& second) {
    if (first.width != second.width || first.height != second.height ||
        first.rgb.size() != second.rgb.size())
        return static_cast<std::size_t>(first.width) * first.height + 1U;
    std::size_t differing = 0;
    for (std::size_t at = 0; at + 2 < first.rgb.size(); at += 3)
        if (first.rgb[at] != second.rgb[at] || first.rgb[at + 1] != second.rgb[at + 1] ||
            first.rgb[at + 2] != second.rgb[at + 2])
            ++differing;
    return differing;
}

/// Returns the path of one step's frame: <stem>-<step>.ppm beside --snapshot.
///
/// @param snapshot the --snapshot path
/// @param step the step's name
/// @return the step's path
fs::path step_snapshot(const fs::path& snapshot, std::string_view step) {
    return snapshot.parent_path() / (snapshot.stem().string() + '-' + std::string(step) + ".ppm");
}

} // namespace

void Runtime::check_language_switch() {
    if (sdl_.renderer == nullptr || sdl_.window == nullptr)
        fail("needs the SDL presenter");
    // The check saves settings, so it never runs over the player's own file.
    if (!options_.preferences_file)
        fail("needs --preferences-file");
    if (shown_language().tag != kStartTag)
        fail(
            "the run shows " + std::string(shown_language().tag) + ", not " +
            std::string(kStartTag) + ": its preferences file chooses the language"
        );
    if (language_pack_folders().empty())
        fail("no language pack beside the game holds " + std::string(kStartTag));

    // However the check ends, its preferences file chooses the language the
    // run started in again, for the next run.
    struct StartLanguageKept {
        Runtime& runtime;

        ~StartLanguageKept() {
            namespace preferences = oa::platform::preferences;
            try {
                auto values = preferences::load(runtime.preference_path_);
                values[std::string(settings::key::language)] = std::string(kStartTag);
                preferences::save(runtime.preference_path_, values);
            } catch (const std::exception& error) {
                std::cerr << "language switch check: the preferences file keeps the last "
                             "language chosen: "
                          << error.what() << '\n';
            }
        }
    } start_language_kept{*this};

    // The extensions' overlays are set aside while the check holds the main
    // menu to the engine's own drawing, and put back when it ends.
    set_extension_overlays_aside(true);

    struct PutBack {
        Runtime& runtime;

        ~PutBack() { runtime.set_extension_overlays_aside(false); }
    } put_back{*this};

    const auto snapshot = [this](std::string_view step, const renderer::Surface& frame) {
        if (!options_.snapshot.empty())
            write_ppm(step_snapshot(options_.snapshot, step), frame);
    };
    // Chooses a language in the open settings, as a click on its list does,
    // and closes them with OK.
    const auto choose = [this](std::string_view tag, auto&& close) {
        auto* dialog = engine_settings_dialog();
        if (dialog == nullptr)
            fail("the settings did not open");
        dialog->chosen.language = std::string(tag);
        std::ignore = take_engine_settings_action(settings::DialogAction::changed);
        close(settings::DialogAction::accepted);
        if (engine_settings_dialog() != nullptr)
            fail("OK did not close the settings");
        if (shown_language().tag != tag)
            fail(
                "the settings chose " + std::string(tag) + ", but the run shows " +
                std::string(shown_language().tag)
            );
    };
    // Requires a frame shown after a change to be the one the screen shows
    // opened again, and to differ from the frame of the language before.
    const auto expect_shown = [](std::string_view screen,
                                 std::string_view tag,
                                 const renderer::Surface& switched,
                                 const renderer::Surface& reopened,
                                 const renderer::Surface& before) {
        if (const auto differing = differing_pixels(switched, reopened); differing != 0)
            fail(
                "after the change to " + std::string(tag) + " " + std::string(screen) + " shows " +
                std::to_string(differing) + " pixels other than it shows opened again in it"
            );
        if (differing_pixels(switched, before) == 0)
            fail(std::string(screen) + " looks the same in " + std::string(tag) + " as before it");
    };

    // The main menu: each frame drawn from sparks started afresh, without
    // the pointer.
    const auto menu_frame = [this] {
        menu_sparks_ = {};
        return frame_without_cursor();
    };
    load(Screen::main_menu);
    if (screen_ != Screen::main_menu)
        fail("the main menu did not open");
    send_check_pointer(SDL_EVENT_MOUSE_MOTION, kRestingPointer, 0);
    const auto menu_started = menu_frame();
    snapshot("menu-" + std::string(kStartTag) + "-start", menu_started);
    auto menu_before = menu_started;
    for (const auto tag : kChosenTags) {
        open_engine_settings_from_menu();
        choose(tag, [this](settings::DialogAction action) {
            EngineSettingsMenuHost::take_action(*this, action, 0);
        });
        const auto switched = menu_frame();
        snapshot("menu-" + std::string(tag), switched);
        load(Screen::main_menu);
        const auto reopened = menu_frame();
        snapshot("menu-" + std::string(tag) + "-opened-again", reopened);
        expect_shown("the main menu", tag, switched, reopened, menu_before);
        menu_before = switched;
    }
    if (const auto differing = differing_pixels(menu_before, menu_started); differing != 0)
        fail(
            "back in " + std::string(kStartTag) + " the main menu shows " +
            std::to_string(differing) + " pixels other than it showed as the run started"
        );
    std::cout << "language switch check: the main menu shows each language as it opens in it\n";

    // A skirmish's in-game menu, the pointer at the window's far corner.
    start_benchmark_skirmish();
    apply_output_mode();
    show_match_pause_menu();
    if (!ingame_menu_column_shown())
        fail("the in-game menu did not open");
    const auto match_frame = [this] {
        send_check_pointer(
            SDL_EVENT_MOUSE_MOTION, {match_layout_.width - 1, match_layout_.height - 1}, 0
        );
        // The bars' numbers ease toward the stores a step each frame drawn:
        // they are brought to rest first, so that frames differ only where
        // the language does.
        auto& game = match_->state().game;
        auto& readout = game.resource_readout;
        for (int step = 0; step < kMostReadoutSteps; ++step) {
            const float metal = readout.metal;
            const float energy = readout.energy;
            oa::ui::hud::update_resource_readout(
                readout, game.players[match_view_player()], game.tick
            );
            if (readout.metal == metal && readout.energy == energy)
                break;
        }
        render_match_surface();
        renderer::Surface frame;
        compose_match_frame(frame);
        return frame;
    };
    const auto match_started = match_frame();
    snapshot("match-" + std::string(kStartTag) + "-start", match_started);
    auto match_before = match_started;
    for (const auto tag : kChosenTags) {
        open_engine_settings_in_match();
        choose(tag, [this](settings::DialogAction action) {
            std::ignore = EngineSettingsMatchHost::take_action(*this, action);
        });
        if (!ingame_menu_column_shown())
            fail("the in-game menu closed with the settings");
        const auto switched = match_frame();
        snapshot("match-" + std::string(tag), switched);
        // The game resumed, and the in-game menu opened again over it with
        // the pictures and panels kept for later loaded afresh, as a run
        // started in the language shows it.
        match_titles_ = {};
        talk_layout_.reset();
        match_talk_ = {};
        resume_match_pause();
        show_match_pause_menu();
        const auto reopened = match_frame();
        snapshot("match-" + std::string(tag) + "-opened-again", reopened);
        expect_shown("the in-game menu", tag, switched, reopened, match_before);
        match_before = switched;
    }
    if (const auto differing = differing_pixels(match_before, match_started); differing != 0)
        fail(
            "back in " + std::string(kStartTag) + " the in-game menu shows " +
            std::to_string(differing) + " pixels other than it showed as the skirmish started"
        );
    std::cout << "language switch check: the in-game menu shows each language as it opens in "
                 "it\n";
}

} // namespace oa::app
