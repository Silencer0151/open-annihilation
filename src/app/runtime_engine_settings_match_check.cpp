// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// --check-engine-settings, the match's part: the OA button under Resume, the
// dialog beside the darkened column, the pause and the locks.

#include "check_host_input.hpp"
#include "engine_settings_match_host.hpp"
#include "engine_settings_state.hpp"
#include "engine_settings_tall_section.hpp"

#include "oa/app/runtime.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
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

/// The rows of a side-column page taller than the game's own 480, as a
/// mod's build page may be: it narrows the side column on a window whose
/// height decides the chrome's scale.
constexpr int kTallPageRows = 540;

/// Windows the OA button is placed on without a game: 4:3, 16:10 (and the
/// same at two pixels a point) and 16:9.
constexpr std::array<std::pair<int, int>, 6> kPlacedWindows{{
    {1024, 768},
    {1440, 900},
    {1512, 982},
    {2880, 1800},
    {3024, 1964},
    {1920, 1080},
}};

/// The section whose rows a game locks: Common Tweaks.
constexpr std::array<settings::Page, 1> kLockedPages{settings::Page::common_tweaks};

/// Common Tweaks' rows a game locks: the unit limit and the pathfinding cycles.
constexpr std::array<int32_t, 2> kLockedRows{
    settings::first_row_control + 1, settings::first_row_control + 2
};

/// How Mods' note during a game starts; the note may take two lines.
constexpr std::string_view kModInGameNote = "Locked during a game.";

/// The keys that move the focus, each pressed in turn round every control
/// and back: Tab, Shift+Tab, Down and Up.
constexpr std::array<std::pair<SDL_Keycode, SDL_Keymod>, 4> kFocusKeys{{
    {SDLK_TAB, SDL_KMOD_NONE},
    {SDLK_TAB, SDL_KMOD_SHIFT},
    {SDLK_DOWN, SDL_KMOD_NONE},
    {SDLK_UP, SDL_KMOD_NONE},
}};

/// Returns the name a section's snapshots carry.
///
/// @param page Common Tweaks
/// @return a short name
std::string_view locked_page_slug(settings::Page page) {
    return page == settings::Page::common_tweaks ? "tweaks" : "page";
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
    using MatchHost = EngineSettingsMatchHost;
    const auto require = [](bool ok, const std::string& what) {
        if (!ok)
            throw std::runtime_error("engine settings check: " + what);
    };
    const fs::path report_directory = "local/reports";
    fs::create_directories(report_directory);
    require(engine_settings_fonts() != nullptr, "the dialog's fonts are missing");

    // On a phone with touch controls the dialog and the OA button fit the
    // safe area: an 852x393-point landscape phone at three pixels a point,
    // its sides 59 points in and its bottom 21.
    {
        const layout::Insets insets{177, 0, 177, 63};
        const auto phone = layout::make_phone_layout(2556, 1179, 3.0, insets);
        const auto safe = MatchHost::safe_area(phone);
        require(
            safe.x == 177 && safe.y == 0 && safe.width == 2202 && safe.height == 1116,
            "the phone's safe area is not the canvas less its insets"
        );
        const auto inside_safe = [&safe](const layout::Rect& rect) {
            return rect.width > 0 && rect.height > 0 && rect.x >= safe.x && rect.y >= safe.y &&
                   rect.x + rect.width <= safe.x + safe.width &&
                   rect.y + rect.height <= safe.y + safe.height;
        };
        const auto dialog_at = MatchHost::dialog_rect(phone, true);
        // min(2202 / 480, 1116 / 324): the safe area's height decides.
        const double scale = 1116.0 / settings::dialog_height;
        require(
            inside_safe(dialog_at) && dialog_at.height == 1116 &&
                dialog_at.width == static_cast<int>(std::lround(settings::dialog_width * scale)) &&
                std::abs(2 * dialog_at.x + dialog_at.width - (2 * safe.x + safe.width)) <= 1,
            "the dialog does not fit the phone's safe area, centred"
        );
        const auto point = MatchHost::dialog_point(
            phone,
            static_cast<float>(dialog_at.x + dialog_at.width - 1),
            static_cast<float>(dialog_at.y + dialog_at.height - 1),
            true
        );
        require(
            point.x == settings::dialog_width - 1 && point.y == settings::dialog_height - 1,
            "the fitted dialog's corner does not map to its last source pixel"
        );
        const auto button = MatchHost::button_rect(phone, true);
        require(inside_safe(button), "the OA button leaves the phone's safe area");
        const auto unfitted = MatchHost::dialog_rect(phone);
        require(
            unfitted.width == settings::dialog_width && !inside_safe(MatchHost::button_rect(phone)),
            "without fitting, the dialog does not keep the side column's scale"
        );
    }

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
    const auto icon = engine_settings_icon();
    require(renderer::picture_drawable(icon), "the in-game OA button has no icon to show");
    const auto button_face = [&](settings::ButtonLook look, bool darkened, const auto& fonts) {
        return MatchHost::button_face(match_layout_, look, darkened, fonts, icon);
    };
    const auto dialog_face = [&icon](const settings::Dialog& dialog, const auto& fonts) {
        renderer::Surface face;
        face.width = static_cast<uint32_t>(settings::dialog_width);
        face.height = static_cast<uint32_t>(settings::dialog_height);
        face.rgb.assign(static_cast<std::size_t>(face.width) * face.height * 3U, 0);
        settings::draw_dialog(face, {0, 0, 1}, dialog, fonts, icon);
        return face;
    };
    const auto centre = [](const layout::Rect& rect) {
        return layout::Point{rect.x + rect.width / 2, rect.y + rect.height / 2};
    };

    // Clicks the middle of a part of the dialog that shows beside the column.
    const auto click_dialog = [&](const artless::SourceRect& part) {
        const auto at = MatchHost::dialog_rect(match_layout_);
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
        // The Game files section belongs to the main menu's dialog alone.
        require(!dialog->game_files, "the in-game dialog lists Game files" + on);
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
                if (std::find(kLockedRows.begin(), kLockedRows.end(), part.control) !=
                        kLockedRows.end() &&
                    part.text.empty()) {
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

    // Mods during a game keeps the mod played and says that the main menu
    // chooses the mod: no press on a row and no key asks the Switch Mod
    // question, and OPEN MODS FOLDER is inert.
    const auto expect_mod_locked = [&](const std::string& on) {
        show_page(settings::Page::mods, on);
        const auto kept = engine_settings();
        auto* dialog = engine_settings_dialog();
        const auto parts = settings::dialog_layout(*dialog);
        require(
            dialog->locks.mod == settings::Lock::in_game &&
                std::any_of(
                    parts.begin(),
                    parts.end(),
                    [](const settings::LayoutPart& part) {
                        return part.text.starts_with(kModInGameNote);
                    }
                ),
            "Mods is not locked during a game with its note" + on
        );
        // No mod plays in the check, whatever the setting holds.
        const auto rows = settings::mod_rows(*dialog);
        require(
            !rows.empty() && rows.front().playing && rows.front().offered == settings::no_mod_row,
            "Mods does not show the mod played first" + on
        );
        // A press on every row and on OPEN MODS FOLDER, where they are
        // unlocked.
        auto unlocked = *dialog;
        unlocked.locks = {};
        for (const auto& part : settings::dialog_layout(unlocked))
            if (part.control >= settings::first_row_control)
                click_dialog(part.rect);
        // The keys move the focus round every control and back, never onto
        // a row; on Mods' entry, where the focus comes back to, the arrows
        // and Space change nothing.
        const auto entry = settings::page_control(settings::Page::mods);
        require(
            engine_settings_dialog()->focused == entry,
            "a click on Mods' entry did not focus it" + on
        );
        for (const auto& [key, modifiers] : kFocusKeys) {
            for (int press = 0; press < 12; ++press) {
                tap_key(key, modifiers);
                require(
                    engine_settings_dialog() != nullptr &&
                        engine_settings_dialog()->focused < settings::first_row_control,
                    "a key moved the focus onto a locked row of Mods" + on
                );
            }
        }
        require(
            engine_settings_dialog()->focused == entry,
            "the keys did not bring the focus back to Mods' entry" + on
        );
        for (const auto key : {SDLK_RIGHT, SDLK_LEFT, SDLK_SPACE, SDLK_Y})
            tap_key(key, SDL_KMOD_NONE);
        dialog = engine_settings_dialog();
        require(
            dialog != nullptr && dialog->switch_question == settings::no_question &&
                !soft_restart_requested() && engine_settings() == kept,
            "a press or a key asked to switch the mod or changed a setting during a game" + on
        );
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
        const auto button = MatchHost::button_rect(match_layout_);
        const auto dialog_at = MatchHost::dialog_rect(match_layout_);
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
        require(!MatchHost::button_shown(*this), "the OA button shows in play" + on);
        send_check_pointer(SDL_EVENT_MOUSE_MOTION, centre(button), 0);
        send_check_pointer(SDL_EVENT_MOUSE_BUTTON_DOWN, centre(button), SDL_BUTTON_LEFT);
        send_check_pointer(SDL_EVENT_MOUSE_BUTTON_UP, centre(button), SDL_BUTTON_LEFT);
        require(engine_settings_dialog() == nullptr, "a press in play opened the dialog" + on);

        // The in-game menu shows the button under Resume.
        show_match_pause_menu();
        require(ingame_menu_column_shown(), "the in-game menu's column does not show" + on);
        // The button lies in the menu's panel, at the same place in it on
        // every window: this window, and the others with the game's pages and
        // with a page that narrows the side column.
        const auto& panel_record = match_hud_->layout.gadgets.front().common;
        const layout::Rect menu_panel{
            panel_record.x, panel_record.y, panel_record.width, panel_record.height
        };
        const auto check_in_menu = [&](const layout::MatchLayout& match, const std::string& where) {
            const auto panel = layout::source_rect_to_canvas(
                match, menu_panel.x, menu_panel.y, menu_panel.width, menu_panel.height
            );
            const auto placed = MatchHost::button_rect(match);
            const double scale = match.column_narrowed() ? match.column_scale : match.scale;
            const auto at = [scale](int source) {
                return static_cast<int>(std::lround(static_cast<double>(source) * scale));
            };
            require(
                placed.x >= panel.x && placed.y >= panel.y &&
                    placed.x + placed.width <= panel.x + panel.width &&
                    placed.y + placed.height <= panel.y + panel.height,
                "the OA button leaves the in-game menu" + where
            );
            require(
                std::abs(placed.x - panel.x - at(MatchHost::button_source_x - menu_panel.x)) <= 1 &&
                    std::abs(placed.y - panel.y - at(MatchHost::button_source_y - menu_panel.y)) <=
                        1,
                "the OA button is not under Resume" + where
            );
        };
        check_in_menu(match_layout_, on);
        for (const auto& [placed_width, placed_height] : kPlacedWindows) {
            const auto plain = layout::make_match_layout(placed_width, placed_height);
            const std::string window =
                " on a " + std::to_string(placed_width) + 'x' + std::to_string(placed_height);
            check_in_menu(plain, window + " window");
            check_in_menu(
                layout::fit_side_column(plain, kTallPageRows),
                window + " window whose side column a taller page narrows"
            );
        }
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
        expect_mod_locked(on);
        require(
            !shows_text(
                settings::dialog_layout(*engine_settings_dialog()), "Shared game - still running"
            ),
            "a game played alone says it is shared" + on
        );
        // Graphics at its end in a game played alone: Vertical sync and
        // Explosion flash among its last rows, and neither Hardware
        // acceleration nor Vertical sync locked by the game.
        show_page(settings::Page::graphics, on);
        tap_key(SDLK_END, SDL_KMOD_NONE);
        {
            const auto* graphics = engine_settings_dialog();
            require(
                graphics != nullptr &&
                    graphics->scroll[static_cast<std::size_t>(settings::Page::graphics)] == 257,
                "End did not scroll Graphics to its end" + on
            );
            const auto parts = settings::dialog_layout(*graphics);
            require(
                shows_text(parts, "Vertical sync") && shows_text(parts, "Explosion flash"),
                "Graphics at its end does not show its last rows" + on
            );
            require(
                graphics->locks.hardware_acceleration != settings::Lock::in_game &&
                    graphics->locks.vertical_sync != settings::Lock::in_game,
                "a game played alone locks Hardware acceleration or Vertical sync" + on
            );
        }
        // No snapshot here: a frame drawn while the game is paused would move
        // the later shared game's clock, and with it the snapshots taken there.
        tap_key(SDLK_HOME, SDL_KMOD_NONE);

        // The wheel over the dialog reaches it and not the battlefield: given
        // a section taller than its view, a notch scrolls it, the zoom stays
        // and the dialog is drawn again; Home scrolls it back.
        {
            namespace tall = engine_settings_check;

            // The dialog shows its own sections again however the block
            // ends, and the next frame draws them.
            struct OwnSections {
                Runtime& runtime;
                uint64_t& revision;

                ~OwnSections() {
                    if (auto* shown = runtime.engine_settings_dialog()) {
                        tall::show_own_sections(*shown);
                        ++revision;
                    }
                }
            } own_sections{*this, engine_settings_match_host().revision};

            tall::show_tall_section(*engine_settings_dialog());
            const auto offset = [this] {
                const auto* open = engine_settings_dialog();
                return open->scroll[static_cast<std::size_t>(open->page)];
            };
            const float zoom = match_zoom_target_;
            const uint64_t revision = engine_settings_match_host().revision;
            const auto over = centre(dialog_at);
            SDL_Event wheel =
                check_host_input::wheel_event(sdl_.renderer, sdl_.window, over.x, over.y, -1.0F);
            dispatch_event(wheel, running);
            require(running, "the wheel in the dialog ended the run" + on);
            require(
                offset() == tall::kWheelStepPixels,
                "a notch of the wheel did not scroll the dialog" + on
            );
            require(
                match_zoom_target_ == zoom, "the wheel over the dialog zoomed the battlefield" + on
            );
            require(
                engine_settings_match_host().revision != revision,
                "a scroll did not draw the dialog again" + on
            );
            tap_key(SDLK_HOME, SDL_KMOD_NONE);
            require(offset() == 0, "Home did not scroll the dialog back to its top" + on);
        }

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
        // sets Pathfinding cycles and Unit limit. The match turns shared
        // here rather than at its loading screen, so the tier is told so
        // as a shared game's bootstrap tells it.
        extension_.state = [](void*, const Runtime&) -> uint32_t {
            return extension_state::multiplayer | extension_state::shared_match;
        };
        begin_render_tier_match(render_policy::MatchKind::shared_game);
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
        show_page(settings::Page::mods, on);
        require(
            engine_settings_dialog()->locks.mod == settings::Lock::in_game &&
                std::ranges::any_of(
                    settings::dialog_layout(*engine_settings_dialog()),
                    [](const settings::LayoutPart& part) {
                        return part.text.starts_with(kModInGameNote);
                    }
                ),
            "a shared game does not lock Mods with its note" + on
        );
        // Graphics in a shared game, a page down to show both rows: Vertical
        // sync is locked during the game, its value set before the game kept
        // in effect; Hardware acceleration can still be set, and Basic or
        // Full waits for the game to end, so the processor keeps drawing.
        // Escape then puts Off back.
        show_page(settings::Page::graphics, on);
        tap_key(SDLK_PAGEDOWN, SDL_KMOD_NONE);
        dialog = engine_settings_dialog();
        require(
            dialog->locks.vertical_sync ==
                (options_.force_capable ? settings::Lock::in_game : settings::Lock::unavailable),
            "a shared game does not lock Vertical sync" + on
        );
        require(
            dialog->locks.hardware_acceleration == settings::Lock::none ||
                dialog->locks.hardware_acceleration == settings::Lock::unavailable,
            "a shared game locks Hardware acceleration" + on
        );
        require(
            shows_text(settings::dialog_layout(*dialog), "Locked during a game") ||
                !options_.force_capable,
            "Vertical sync does not say it is locked during the game" + on
        );
        if (dialog->locks.hardware_acceleration == settings::Lock::none) {
            // Each level of the strip, Full then Basic, waits for the game's
            // end and says which level then takes effect.
            const auto choose = [&](std::string_view caption,
                                    settings::HardwareAcceleration level,
                                    std::string_view waiting) {
                // The layout outlives the loop: segment points into it.
                const auto strip_layout = settings::dialog_layout(*dialog);
                const settings::LayoutPart* segment = nullptr;
                for (const auto& part : strip_layout)
                    if (part.control == settings::first_row_control + 3 && part.text == caption)
                        segment = &part;
                require(
                    segment != nullptr,
                    "Hardware acceleration's " + std::string(caption) + " does not show" + on
                );
                const auto rect = segment->rect;
                click_dialog(rect);
                tick_screen_packages();
                dialog = engine_settings_dialog();
                require(
                    dialog != nullptr && engine_settings().hardware_acceleration == level,
                    "Hardware acceleration was not set to " + std::string(caption) +
                        " in a shared game" + on
                );
                require(
                    dialog->acceleration.state == settings::AccelerationState::waiting_for_game_end,
                    "Hardware acceleration " + std::string(caption) +
                        " in a shared game does not wait for its end" + on
                );
                require(
                    shows_text(settings::dialog_layout(*dialog), waiting),
                    "the status does not say " + std::string(caption) + " waits for the next game" +
                        on
                );
            };
            choose(
                "Full",
                settings::HardwareAcceleration::full,
                "Off for this game: in a shared game, Full"
            );
            choose(
                "Basic",
                settings::HardwareAcceleration::basic,
                "Off for this game: in a shared game, Basic"
            );
            snapshot("match-dialog-shared-graphics", size);
        }
        tap_key(SDLK_ESCAPE, SDL_KMOD_NONE);
        require(
            engine_settings().hardware_acceleration == settings::HardwareAcceleration::off,
            "Escape did not put Hardware acceleration back Off" + on
        );
        require(engine_settings_dialog() == nullptr, "Escape did not close the dialog" + on);
        extension_ = saved_extension;
        begin_render_tier_match(render_policy::MatchKind::none);
        resume_match_pause();
        std::cout << "engine settings check: the in-game menu's OA button at " << button.x << ','
                  << button.y << " and the dialog at " << dialog_at.x << ',' << dialog_at.y << on
                  << '\n';
    }
    leave_match();
    load(Screen::main_menu);
    // Back on the main menu Mods is unlocked.
    send_key(SDLK_COMMA, kShortcutModifier, true, false);
    send_key(SDLK_COMMA, kShortcutModifier, false, false);
    {
        const auto* dialog = engine_settings_dialog();
        require(
            dialog != nullptr && dialog->locks.mod == settings::Lock::none &&
                engine_settings_locks().mod == settings::Lock::none,
            "Mods is locked on the main menu after a game"
        );
    }
    tap_key(SDLK_ESCAPE, SDL_KMOD_NONE);
    require(engine_settings_dialog() == nullptr, "Escape did not close the main menu's dialog");
    if (!SDL_SetWindowSize(sdl_.window, kDefaultWindowWidth, kDefaultWindowHeight) ||
        !SDL_SyncWindow(sdl_.window))
        throw std::runtime_error(std::string("SDL_SetWindowSize: ") + SDL_GetError());
    std::cout << "engine settings check: the in-game menu\n";
}

} // namespace oa::app
