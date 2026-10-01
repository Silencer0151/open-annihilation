// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// --check-engine-settings, the match's part: the OA button under Resume, the
// dialog beside the darkened column, the pause and the locks.

#include "engine_settings_match_host.hpp"

#include "oa/app/runtime.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace oa::app {

namespace {

namespace fs = std::filesystem;
namespace settings = oa::ui::engine_settings;
namespace layout = oa::ui::display_layout;
namespace artless = oa::ui::frontend_renderer;

/// How far round the pointer the software cursor may draw, in window pixels.
constexpr int kCursorReach = 64;
/// How long the check lets a paused game's clock run, in milliseconds a frame.
constexpr uint32_t kPausedFrameMs = 40;
/// How many frames the check lets a paused game's clock run.
constexpr int kPausedFrames = 3;

/// The modifier of the shortcut that opens the settings.
#ifdef SDL_PLATFORM_MACOS
constexpr SDL_Keymod kShortcutModifier = SDL_KMOD_GUI;
#else
constexpr SDL_Keymod kShortcutModifier = SDL_KMOD_CTRL;
#endif

/// An ultrawide window: 21:9.
constexpr int kUltrawideWidth = 2560;
/// An ultrawide window's height.
constexpr int kUltrawideHeight = 1080;

/// The sections whose rows a game locks: AI & Pathfinding and Gameplay.
constexpr std::array<settings::Page, 2> kLockedPages{
    settings::Page::path_search, settings::Page::gameplay
};

/// Returns the name a section's snapshots carry.
///
/// @param page AI & Pathfinding or Gameplay
/// @return a short name
std::string_view locked_page_slug(settings::Page page) {
    return page == settings::Page::path_search ? "path" : "gameplay";
}

/// Tells whether the dialog's layout shows a text.
///
/// @param parts the dialog's layout
/// @param text the text
/// @return true when a part draws exactly that text
bool shows_text(const std::vector<settings::LayoutPart>& parts, std::string_view text) {
    return std::any_of(parts.begin(), parts.end(), [text](const settings::LayoutPart& part) {
        return part.text == text;
    });
}

/// Returns the path of one step's snapshot: <stem>-<step>.ppm beside --snapshot.
///
/// @param snapshot the --snapshot path
/// @param step the step's name
/// @return the step's snapshot path
fs::path step_snapshot(const fs::path& snapshot, std::string_view step) {
    auto stem = snapshot;
    stem.replace_extension();
    return fs::path(stem.string() + "-" + std::string(step) + ".ppm");
}

} // namespace

void Runtime::check_engine_settings_in_match() {
    using Host = EngineSettingsMatchHost;
    const auto require = [](bool ok, const std::string& what) {
        if (!ok)
            throw std::runtime_error("engine settings check: " + what);
    };
    const fs::path report_directory = "local/reports";
    fs::create_directories(report_directory);
    require(engine_settings_fonts() != nullptr, "the dialog's fonts are missing");
    const Extension saved_extension = extension_;
    bool running = true;
    const auto send_key = [&](SDL_Keycode key, SDL_Keymod modifiers, bool down, bool repeat) {
        SDL_Event event{};
        event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
        event.key.windowID = sdl_.window != nullptr ? SDL_GetWindowID(sdl_.window) : 0;
        event.key.key = key;
        event.key.mod = modifiers;
        event.key.down = down;
        event.key.repeat = repeat;
        dispatch_event(event, running);
        require(running, "a key ended the run");
    };
    const auto tap_key = [&](SDL_Keycode key, SDL_Keymod modifiers) {
        send_key(key, modifiers, true, false);
        send_key(key, modifiers, false, false);
    };
    const auto composed = [this] {
        renderer::Surface frame;
        compose_match_frame(frame);
        return frame;
    };
    const auto shown = [this](const renderer::Surface& source) {
        auto copy = source;
        apply_gamma_rgb(copy.rgb.data(), copy.rgb.size() / 3U, 3);
        return copy;
    };
    // Each window pixel of `rect` shows the source pixel it lands on.
    const auto drawn_at = [&](const renderer::Surface& frame,
                              const renderer::Surface& source,
                              const layout::Rect& rect,
                              const std::string& what) {
        const auto expected = shown(source);
        std::size_t differing = 0;
        for (int row = 0; row < rect.height; ++row)
            for (int column = 0; column < rect.width; ++column) {
                const auto source_offset =
                    (static_cast<std::size_t>(row * static_cast<int>(source.height) / rect.height) *
                         source.width +
                     static_cast<std::size_t>(
                         column * static_cast<int>(source.width) / rect.width
                     )) *
                    3U;
                const auto frame_offset = (static_cast<std::size_t>(rect.y + row) * frame.width +
                                           static_cast<std::size_t>(rect.x + column)) *
                                          3U;
                for (std::size_t channel = 0; channel < 3; ++channel)
                    if (frame.rgb[frame_offset + channel] !=
                        expected.rgb[source_offset + channel]) {
                        ++differing;
                        break;
                    }
            }
        require(differing == 0, what + ": " + std::to_string(differing) + " pixels differ");
    };
    const auto button_face = [](settings::ButtonLook look, bool darkened, const auto& fonts) {
        renderer::Surface face;
        face.width = static_cast<uint32_t>(settings::ingame_button_side);
        face.height = face.width;
        face.rgb.assign(static_cast<std::size_t>(face.width) * face.height * 3U, 0);
        settings::draw_oa_button(face, {0, 0, 1}, settings::ingame_button_side, look, fonts);
        if (darkened)
            renderer::blend_source_rect(
                face,
                {0, 0, 1},
                {0, 0, settings::ingame_button_side, settings::ingame_button_side},
                settings::backdrop_color,
                settings::ingame_backdrop_opacity
            );
        return face;
    };
    const auto dialog_face = [](const settings::Dialog& dialog, const auto& fonts) {
        renderer::Surface face;
        face.width = static_cast<uint32_t>(settings::dialog_width);
        face.height = static_cast<uint32_t>(settings::dialog_height);
        face.rgb.assign(static_cast<std::size_t>(face.width) * face.height * 3U, 0);
        settings::draw_dialog(face, {0, 0, 1}, dialog, fonts);
        return face;
    };
    const auto centre = [](const layout::Rect& rect) {
        return layout::Point{rect.x + rect.width / 2, rect.y + rect.height / 2};
    };

    // Clicks the middle of a part of the dialog that shows beside the column.
    const auto click_dialog = [&](const artless::SourceRect& part) {
        const auto at = Host::dialog_rect(match_layout_);
        const layout::Point point{
            at.x + (part.x + part.width / 2) * at.width / settings::dialog_width,
            at.y + (part.y + part.height / 2) * at.height / settings::dialog_height
        };
        send_check_pointer(SDL_EVENT_MOUSE_MOTION, point, 0);
        send_check_pointer(SDL_EVENT_MOUSE_BUTTON_DOWN, point, SDL_BUTTON_LEFT);
        send_check_pointer(SDL_EVENT_MOUSE_BUTTON_UP, point, SDL_BUTTON_LEFT);
    };
    // Shows a section of the open dialog through its entry in the list.
    const auto show_page = [&](settings::Page page, const std::string& on) {
        auto* dialog = engine_settings_dialog();
        require(dialog != nullptr, "the dialog closed" + on);
        for (const auto& part : settings::dialog_layout(*dialog))
            if (part.control == settings::page_control(page)) {
                click_dialog(part.rect);
                break;
            }
        require(dialog->page == page, "a click on a section's entry did not show it" + on);
    };
    // Writes the composed frame as a step's snapshot when --snapshot names one.
    const auto snapshot = [&](std::string_view step, const std::string& size) {
        if (options_.snapshot.empty())
            return;
        send_check_pointer(
            SDL_EVENT_MOUSE_MOTION, {match_layout_.width - 1, match_layout_.height - 1}, 0
        );
        render();
        write_ppm(step_snapshot(options_.snapshot, std::string(step) + '-' + size), composed());
    };
    // Each locked row says why, and neither a press on its slider nor its
    // keys change it.
    const auto expect_locks = [&](std::string_view lock_text,
                                  std::string_view step,
                                  const std::string& size,
                                  const std::string& on) {
        // Each section says why its row is locked, shown before a key has
        // shown the keyboard focus.
        for (const auto page : kLockedPages) {
            show_page(page, on);
            require(
                shows_text(settings::dialog_layout(*engine_settings_dialog()), lock_text),
                std::string(locked_page_slug(page)) + " does not say \"" + std::string(lock_text) +
                    '"' + on
            );
            snapshot(std::string(step) + '-' + std::string(locked_page_slug(page)), size);
        }
        for (const auto page : kLockedPages) {
            show_page(page, on);
            const auto kept = engine_settings();
            // The slider's place, as the row shows it unlocked.
            auto unlocked = *engine_settings_dialog();
            unlocked.locks = {};
            for (const auto& part : settings::dialog_layout(unlocked))
                if (part.control == settings::first_row_control && part.text.empty()) {
                    click_dialog(
                        {part.rect.x + part.rect.width - 2, part.rect.y, 1, part.rect.height}
                    );
                    click_dialog({part.rect.x + 1, part.rect.y, 1, part.rect.height});
                }
            for (const auto key : {SDLK_DOWN, SDLK_RIGHT, SDLK_RIGHT, SDLK_LEFT})
                tap_key(key, SDL_KMOD_NONE);
            require(
                engine_settings_dialog() != nullptr && engine_settings() == kept,
                "a locked row changed under a press or a key" + on
            );
        }
    };

    for (const auto& [width, height] :
         {std::pair{640, 480},
          std::pair{1280, 720},
          std::pair{kDefaultWindowWidth, kDefaultWindowHeight},
          std::pair{kUltrawideWidth, kUltrawideHeight}}) {
        const std::string size = std::to_string(width) + 'x' + std::to_string(height);
        const std::string on = " on the " + size + " window";
        if (match_)
            leave_match();
        load(Screen::main_menu);
        if (!SDL_SetWindowSize(sdl_.window, width, height) || !SDL_SyncWindow(sdl_.window))
            throw std::runtime_error(std::string("SDL_SetWindowSize: ") + SDL_GetError());
        start_benchmark_skirmish();
        require(
            match_layout_.width == width && match_layout_.height == height,
            "the match canvas is not the window's size" + on
        );
        const auto& fonts = *engine_settings_fonts();
        const auto button = Host::button_rect(match_layout_);
        const auto dialog_at = Host::dialog_rect(match_layout_);
        require(
            button.y + button.height <= match_layout_.hud_height && button.x >= 0 &&
                button.x + button.width <= match_layout_.left,
            "the OA button leaves the side column" + on
        );
        require(
            dialog_at.x >= match_layout_.left && dialog_at.y >= 0 &&
                dialog_at.x + dialog_at.width <= width && dialog_at.y + dialog_at.height <= height,
            "the dialog leaves the area right of the column" + on
        );

        // In play the column and the button are hidden, and a press on the
        // button's place opens nothing.
        render();
        require(!Host::button_shown(*this), "the OA button shows in play" + on);
        send_check_pointer(SDL_EVENT_MOUSE_MOTION, centre(button), 0);
        send_check_pointer(SDL_EVENT_MOUSE_BUTTON_DOWN, centre(button), SDL_BUTTON_LEFT);
        send_check_pointer(SDL_EVENT_MOUSE_BUTTON_UP, centre(button), SDL_BUTTON_LEFT);
        require(engine_settings_dialog() == nullptr, "a press in play opened the dialog" + on);

        // The in-game menu shows the button under Resume.
        show_match_pause_menu();
        require(ingame_menu_column_shown(), "the in-game menu's column does not show" + on);
        send_check_pointer(SDL_EVENT_MOUSE_MOTION, {width - 1, height - 1}, 0);
        render();
        const auto menu = composed();
        write_ppm(
            report_directory / ("native-engine-settings-menu-" + std::to_string(width) + ".ppm"),
            menu
        );
        if (!options_.snapshot.empty())
            write_ppm(step_snapshot(options_.snapshot, "match-menu-" + size), menu);
        drawn_at(
            menu,
            button_face(settings::ButtonLook::idle, false, fonts),
            button,
            "the OA button" + on
        );
        send_check_pointer(SDL_EVENT_MOUSE_MOTION, centre(button), 0);
        render();
        drawn_at(
            composed(),
            button_face(settings::ButtonLook::hovered, false, fonts),
            button,
            "the hovered OA button" + on
        );
        send_check_pointer(SDL_EVENT_MOUSE_BUTTON_DOWN, centre(button), SDL_BUTTON_LEFT);
        require(
            engine_settings_dialog() == nullptr, "a press opened the dialog before its release"
        );
        render();
        drawn_at(
            composed(),
            button_face(settings::ButtonLook::pressed, false, fonts),
            button,
            "the pressed OA button" + on
        );

        // Its release opens the dialog beside the darkened column; a game
        // played alone stays paused.
        const uint32_t tick = match_timing_.tick;
        send_check_pointer(SDL_EVENT_MOUSE_BUTTON_UP, centre(button), SDL_BUTTON_LEFT);
        auto* dialog = engine_settings_dialog();
        require(dialog != nullptr, "a click on the OA button did not open the dialog" + on);
        require(
            match_paused_ && ingame_menu_column_shown(),
            "the in-game menu did not stay under the dialog" + on
        );
        require(
            dialog->locks.path_search == settings::Lock::in_game &&
                dialog->locks.unit_limit == settings::Lock::in_game && !dialog->locks.shared_game,
            "a game played alone does not lock Pathfinding cycles and Unit limit" + on
        );
        for (int frame = 0; frame < kPausedFrames; ++frame) {
            SDL_Delay(kPausedFrameMs);
            idle_tick();
        }
        require(
            match_timing_.tick == tick && engine_settings_dialog() != nullptr,
            "the game ran on under the dialog" + on
        );
        send_check_pointer(SDL_EVENT_MOUSE_MOTION, {width - 1, height - 1}, 0);
        render();
        const auto open = composed();
        write_ppm(
            report_directory / ("native-engine-settings-match-" + std::to_string(width) + ".ppm"),
            open
        );
        drawn_at(open, dialog_face(*dialog, fonts), dialog_at, "the dialog" + on);
        drawn_at(
            open,
            button_face(settings::ButtonLook::idle, true, fonts),
            button,
            "the darkened OA button" + on
        );
        // A point in the column, above the button, is darkened.
        const layout::Point column_point{button.x, button.y - button.height};
        const auto column_offset = (static_cast<std::size_t>(column_point.y) * open.width +
                                    static_cast<std::size_t>(column_point.x)) *
                                   3U;
        renderer::Surface backdrop;
        backdrop.width = 1;
        backdrop.height = 1;
        backdrop.rgb = {
            settings::backdrop_color[0], settings::backdrop_color[1], settings::backdrop_color[2]
        };
        backdrop = shown(backdrop);
        const unsigned alpha = (settings::ingame_backdrop_opacity * 255U + 128U) / 256U;
        for (std::size_t channel = 0; channel < 3; ++channel) {
            const unsigned under = menu.rgb[column_offset + channel];
            require(
                open.rgb[column_offset + channel] ==
                    (backdrop.rgb[channel] * alpha + under * (255U - alpha)) / 255U,
                "the in-game menu's column is not darkened under the dialog" + on
            );
        }
        // The presented frame is the composed one.
        renderer::Surface presented;
        capture_frame_ = &presented;
        render();
        capture_frame_ = nullptr;
        const auto again = composed();
        require(
            presented.width == again.width && presented.height == again.height,
            "the presented frame is not the composed frame's size" + on
        );
        std::size_t differing = 0;
        const auto near_cursor = [this](int x, int y) {
            const auto near = [x, y](float cursor_x, float cursor_y) {
                return std::abs(x - static_cast<int>(cursor_x)) < kCursorReach &&
                       std::abs(y - static_cast<int>(cursor_y)) < kCursorReach;
            };
            return near(pointer_x_, pointer_y_) || near(match_pointer_x_, match_pointer_y_);
        };
        for (int y = 0; y < static_cast<int>(again.height); ++y)
            for (int x = 0; x < static_cast<int>(again.width); ++x) {
                if (near_cursor(x, y))
                    continue;
                const auto offset =
                    (static_cast<std::size_t>(y) * again.width + static_cast<std::size_t>(x)) * 3U;
                for (std::size_t channel = 0; channel < 3; ++channel)
                    if (presented.rgb[offset + channel] != again.rgb[offset + channel]) {
                        ++differing;
                        break;
                    }
            }
        if (differing != 0) {
            write_ppm(report_directory / "native-engine-settings-composed.ppm", again);
            write_ppm(report_directory / "native-engine-settings-presented.ppm", presented);
        }
        require(
            differing == 0,
            std::to_string(differing) + " presented pixels differ from the composed frame" + on
        );
        expect_locks("Locked during a game", "match-dialog-alone", size, on);
        require(
            !shows_text(
                settings::dialog_layout(*engine_settings_dialog()), "Shared game - still running"
            ),
            "a game played alone says it is shared" + on
        );

        // A press beside the dialog does nothing; Escape is Cancel and puts
        // the opened settings back, and back to the in-game menu; while it is
        // held the menu does not see it.
        send_check_pointer(SDL_EVENT_MOUSE_BUTTON_DOWN, column_point, SDL_BUTTON_LEFT);
        send_check_pointer(SDL_EVENT_MOUSE_BUTTON_UP, column_point, SDL_BUTTON_LEFT);
        require(
            engine_settings_dialog() != nullptr && ingame_menu_column_shown(),
            "a press beside the dialog reached the in-game menu" + on
        );
        const auto opened = engine_settings_dialog()->opened;
        engine_settings_dialog()->chosen.wheel_zoom = !opened.wheel_zoom;
        (void)take_engine_settings_action(settings::DialogAction::changed);
        send_key(SDLK_ESCAPE, SDL_KMOD_NONE, true, false);
        require(engine_settings_dialog() == nullptr, "Escape did not close the dialog" + on);
        require(
            engine_settings().wheel_zoom == opened.wheel_zoom,
            "Escape did not put the opened settings back" + on
        );
        require(ingame_menu_column_shown(), "Escape did not go back to the in-game menu" + on);
        send_key(SDLK_ESCAPE, SDL_KMOD_NONE, true, true);
        require(
            ingame_menu_column_shown() && match_paused_,
            "a held Escape reached the in-game menu" + on
        );
        send_key(SDLK_ESCAPE, SDL_KMOD_NONE, false, false);

        // The shortcut opens it from the menu, and Enter is OK.
        send_key(SDLK_COMMA, kShortcutModifier, true, false);
        require(engine_settings_dialog() != nullptr, "the shortcut did not open the dialog" + on);
        send_key(SDLK_COMMA, kShortcutModifier, false, false);
        tap_key(SDLK_RETURN, SDL_KMOD_NONE);
        require(
            engine_settings_dialog() == nullptr && ingame_menu_column_shown(),
            "Enter did not close the dialog back to the in-game menu" + on
        );

        // From play the comma alone opens nothing; the shortcut opens the
        // in-game menu with the dialog over it, and pauses.
        resume_match_pause();
        require(!match_paused_, "the in-game menu did not resume" + on);
        tap_key(SDLK_COMMA, SDL_KMOD_NONE);
        require(engine_settings_dialog() == nullptr, "the comma alone opened the dialog" + on);
        send_key(SDLK_COMMA, kShortcutModifier, true, false);
        send_key(SDLK_COMMA, kShortcutModifier, false, false);
        require(
            engine_settings_dialog() != nullptr && ingame_menu_column_shown() && match_paused_,
            "the shortcut in play did not open the dialog over the paused in-game menu" + on
        );
        tap_key(SDLK_ESCAPE, SDL_KMOD_NONE);
        require(engine_settings_dialog() == nullptr, "Escape did not close the dialog" + on);

        // The dialog closes when the menu no longer shows under it.
        send_key(SDLK_COMMA, kShortcutModifier, true, false);
        send_key(SDLK_COMMA, kShortcutModifier, false, false);
        require(engine_settings_dialog() != nullptr, "the shortcut did not open the dialog" + on);
        resume_match_pause();
        tick_screen_packages();
        require(
            engine_settings_dialog() == nullptr,
            "the dialog stayed open without the in-game menu" + on
        );

        // A shared game runs on under the dialog, which says so, and the host
        // sets Pathfinding cycles and Unit limit.
        extension_.state = [](void*, const Runtime&) -> uint32_t {
            return extension_state::multiplayer | extension_state::shared_match;
        };
        show_match_pause_menu();
        send_key(SDLK_COMMA, kShortcutModifier, true, false);
        send_key(SDLK_COMMA, kShortcutModifier, false, false);
        dialog = engine_settings_dialog();
        require(dialog != nullptr, "the shortcut did not open the dialog in a shared game" + on);
        require(
            dialog->locks.path_search == settings::Lock::set_by_host &&
                dialog->locks.unit_limit == settings::Lock::set_by_host &&
                dialog->locks.shared_game,
            "a shared game does not leave Pathfinding cycles and Unit limit to the host" + on
        );
        require(match_clock_steps(), "a shared game stopped under the dialog" + on);
        require(
            shows_text(settings::dialog_layout(*dialog), "Shared game - still running"),
            "the dialog does not say the shared game is still running" + on
        );
        expect_locks("Set by the host", "match-dialog-shared", size, on);
        tap_key(SDLK_ESCAPE, SDL_KMOD_NONE);
        require(engine_settings_dialog() == nullptr, "Escape did not close the dialog" + on);
        extension_ = saved_extension;
        resume_match_pause();
        std::cout << "engine settings check: the in-game menu's OA button at " << button.x << ','
                  << button.y << " and the dialog at " << dialog_at.x << ',' << dialog_at.y << on
                  << '\n';
    }
    leave_match();
    load(Screen::main_menu);
    if (!SDL_SetWindowSize(sdl_.window, kDefaultWindowWidth, kDefaultWindowHeight) ||
        !SDL_SyncWindow(sdl_.window))
        throw std::runtime_error(std::string("SDL_SetWindowSize: ") + SDL_GetError());
    std::cout << "engine settings check: the in-game menu\n";
}

} // namespace oa::app
