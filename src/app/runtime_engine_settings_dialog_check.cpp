// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// --check-engine-settings, the dialog's part: the dialog on the main menu
// driven through the SDL presenter's pointer and keys (every section, each
// setting changed and in effect at once, OK, Cancel and Restore defaults and
// the preferences they leave), and the main menu with its OA button and the
// dialog as the window shows them at several sizes.

#include "check_host_input.hpp"
#include "engine_settings_menu_host.hpp"
#include "engine_settings_state.hpp"
#include "engine_settings_tall_section.hpp"

#include "oa/app/runtime.hpp"
#include "oa/platform/preferences.hpp"
#include "oa/ui/engine_settings/dialog.hpp"
#include "oa/ui/frontend_renderer/artless.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace oa::app {

namespace {

namespace fs = std::filesystem;
namespace settings = oa::ui::engine_settings;
namespace artless = oa::ui::frontend_renderer;
namespace layout = oa::ui::display_layout;

/// The modifier the settings' shortcut takes on this platform.
#ifdef SDL_PLATFORM_MACOS
constexpr SDL_Keymod kShortcutModifier = SDL_KMOD_GUI;
#else
constexpr SDL_Keymod kShortcutModifier = SDL_KMOD_CTRL;
#endif

/// A point of the main menu's picture over none of its buttons and outside
/// the dialog, where the pointer rests between the check's steps.
constexpr layout::Point kRestingPointer{4, 240};

/// Where the pointer rests while a snapshot is taken: the picture's
/// bottom-right pixel, so that the cursor draws almost wholly outside it.
constexpr layout::Point kSnapshotPointer{kCanvasWidth - 1, kCanvasHeight - 1};

/// The OA button's top left corner on the main menu's picture, in its bottom-right corner.
constexpr layout::Point kMenuButtonCorner{596, 436};

/// How far round the pointer the cursor may draw, in the picture's pixels.
constexpr int32_t kCursorReach = 40;

/// The window sizes the main menu is shown at: the picture's own, two 16:9
/// sizes and an ultrawide one.
constexpr std::array<std::pair<int, int>, 4> kWindowSizes{{
    {640, 480},
    {1280, 720},
    {1920, 1080},
    {2560, 1080},
}};

/// The preferences keys' common start: the keys of the Open Annihilation settings.
constexpr std::string_view kEngineKeyPrefix = "open-annihilation.";

/// The Pathfinding cycles the check chooses with the keys: 2x.
constexpr int32_t kChosenPathNodes = settings::base_path_search_nodes * 2;
/// The unit limit the check chooses with the keys: one stop above the default.
constexpr uint16_t kChosenUnitLimit = settings::default_unit_limit + settings::unit_limit_step;
/// The maximum frame rate the check chooses with the keys: one stop below the highest.
constexpr uint32_t kChosenFrameRate = settings::highest_frame_rate - settings::frame_rate_step;
/// The anti-aliasing level the check picks from the strip.
constexpr settings::AntiAliasing kChosenAntiAliasing = settings::AntiAliasing::x4;

/// Throws when a condition fails.
///
/// @param ok the condition
/// @param what what failed
void require(bool ok, std::string_view what) {
    if (!ok)
        throw std::runtime_error("engine settings check: " + std::string(what));
}

/// Returns the name a section's snapshots carry.
///
/// @param page the section
/// @return a short name
std::string_view page_slug(settings::Page page) {
    switch (page) {
    case settings::Page::path_search:
        return "path";
    case settings::Page::controls:
        return "controls";
    case settings::Page::gameplay:
        return "gameplay";
    case settings::Page::graphics:
        return "graphics";
    case settings::Page::developer:
        return "developer";
    }
    return "page";
}

/// The dialog's sections, in the order its list shows them.
constexpr std::array<settings::Page, 5> kPages{
    settings::Page::path_search,
    settings::Page::controls,
    settings::Page::gameplay,
    settings::Page::graphics,
    settings::Page::developer,
};

/// Returns the label a setting's row shows.
///
/// @param setting the setting
/// @return its label, as the dialog draws it
std::string_view label_of(settings::Setting setting) {
    switch (setting) {
    case settings::Setting::path_search:
        return "Pathfinding cycles";
    case settings::Setting::wheel_zoom:
        return "Mouse wheel zoom";
    case settings::Setting::escape_opens_menu:
        return "Escape opens the game menu";
    case settings::Setting::switch_alt:
        return "Select groups without Alt";
    case settings::Setting::unit_limit:
        return "Unit limit";
    case settings::Setting::max_frame_rate:
        return "Maximum frame rate";
    case settings::Setting::anti_aliasing:
        return "Enhanced anti-aliasing";
    case settings::Setting::screen_size:
        return "Screen size";
    case settings::Setting::frame_stats:
        return "Show performance statistics";
    }
    return {};
}

/// Finds a part of the dialog's layout by its control and text.
///
/// @param parts the dialog's layout
/// @param control the part's control
/// @param text the part's text; empty for the control's first part, whatever its text
/// @return the part; nullptr when the layout has none such
const settings::LayoutPart*
find_part(const std::vector<settings::LayoutPart>& parts, int32_t control, std::string_view text) {
    for (const auto& part : parts)
        if (part.control == control && (text.empty() || part.text == text))
            return &part;
    return nullptr;
}

/// Tells whether the dialog's layout shows a text.
///
/// @param parts the dialog's layout
/// @param text the text
/// @return true when a part draws exactly that text
bool shows_text(const std::vector<settings::LayoutPart>& parts, std::string_view text) {
    for (const auto& part : parts)
        if (part.text == text)
            return true;
    return false;
}

/// Returns the Open Annihilation settings' keys a preferences file holds.
///
/// @param values the file's preferences
/// @return each key under kEngineKeyPrefix with its value
std::map<std::string, std::string> engine_keys(const oa::platform::preferences::Values& values) {
    std::map<std::string, std::string> keys;
    for (const auto& [key, value] : values)
        if (key.starts_with(kEngineKeyPrefix))
            keys.emplace(key, value);
    return keys;
}

/// Tells whether a pixel lies in a rectangle.
///
/// @param rect the rectangle
/// @param x the pixel's column
/// @param y the pixel's row
/// @return true inside it
bool inside(const artless::SourceRect& rect, int32_t x, int32_t y) {
    return x >= rect.x && y >= rect.y && x < rect.x + rect.width && y < rect.y + rect.height;
}

/// Counts the pixels in which two frames of one size differ outside a rectangle.
///
/// @param first one frame
/// @param second the other frame
/// @param left_out the rectangle not compared
/// @return the differing pixels; every pixel when the sizes differ
std::size_t differing_outside(
    const artless::Surface& first,
    const artless::Surface& second,
    const artless::SourceRect& left_out
) {
    if (first.width != second.width || first.height != second.height)
        return static_cast<std::size_t>(first.width) * first.height;
    std::size_t differing = 0;
    for (uint32_t y = 0; y < first.height; ++y)
        for (uint32_t x = 0; x < first.width; ++x) {
            if (inside(left_out, static_cast<int32_t>(x), static_cast<int32_t>(y)))
                continue;
            const std::size_t at = (static_cast<std::size_t>(y) * first.width + x) * 3U;
            if (first.rgb[at] != second.rgb[at] || first.rgb[at + 1] != second.rgb[at + 1] ||
                first.rgb[at + 2] != second.rgb[at + 2])
                ++differing;
        }
    return differing;
}

/// Counts the picture's pixels a letterboxed window frame does not show:
/// each picture pixel is compared with the window pixel at its centre,
/// leaving out the pixels round the pointer, where the cursor draws.
///
/// @param presented the window's frame as presented
/// @param picture the picture, at the display gamma
/// @param area where the picture lands in the window
/// @param window_width the window's width, in pixels
/// @param pointer the pointer, in the picture's pixels
/// @return the pixels that differ; every pixel when the frame misses the area
std::size_t letterbox_differences(
    const artless::Surface& presented,
    const artless::Surface& picture,
    const SDL_FRect& area,
    int window_width,
    layout::Point pointer
) {
    // The read-back holds the whole window, or the letterboxed area alone.
    const bool whole_window = static_cast<int>(presented.width) == window_width;
    const float left = whole_window ? area.x : 0.0F;
    const float top = whole_window ? area.y : 0.0F;
    const artless::SourceRect cursor{
        pointer.x - kCursorReach, pointer.y - kCursorReach, 2 * kCursorReach, 2 * kCursorReach
    };
    std::size_t differing = 0;
    for (uint32_t y = 0; y < picture.height; ++y)
        for (uint32_t x = 0; x < picture.width; ++x) {
            if (inside(cursor, static_cast<int32_t>(x), static_cast<int32_t>(y)))
                continue;
            const auto window_x = static_cast<std::size_t>(
                left + (static_cast<float>(x) + 0.5F) * area.w / static_cast<float>(picture.width)
            );
            const auto window_y = static_cast<std::size_t>(
                top + (static_cast<float>(y) + 0.5F) * area.h / static_cast<float>(picture.height)
            );
            if (window_x >= presented.width || window_y >= presented.height)
                return static_cast<std::size_t>(picture.width) * picture.height;
            const auto* shown = presented.rgb.data() + (window_y * presented.width + window_x) * 3U;
            const auto* expected =
                picture.rgb.data() + (static_cast<std::size_t>(y) * picture.width + x) * 3U;
            if (shown[0] != expected[0] || shown[1] != expected[1] || shown[2] != expected[2])
                ++differing;
        }
    return differing;
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

void Runtime::check_engine_settings_dialog() {
    std::cout << "engine settings check: the dialog's sections and controls\n";
    auto& host = engine_settings_menu_host();
    const auto previous_tick = fake_frontend_tick_;
    fake_frontend_tick_ = 1000U;
    load(Screen::main_menu);
    require(screen_ == Screen::main_menu, "the main menu did not open");
    // The dialog starts from an empty preferences file: every setting at its default.
    preference_values_.clear();
    oa::platform::preferences::save(preference_path_, preference_values_);
    init::load_preferences(state_, skirmish_settings_, preferences_, *this);
    load_engine_settings();
    const auto defaults = settings::default_settings(EngineSettingsState::inputs(*this));
    require(engine_settings() == defaults, "the settings did not start at their defaults");

    const auto placement = EngineSettingsMenuHost::dialog_placement();
    const auto point = [this](SDL_EventType type, layout::Point at) {
        send_check_pointer(type, at, type == SDL_EVENT_MOUSE_MOTION ? 0 : SDL_BUTTON_LEFT);
    };
    const auto rest = [&] { point(SDL_EVENT_MOUSE_MOTION, kRestingPointer); };
    // Clicks a point of the dialog, in its source pixels.
    const auto click_at = [&](int32_t x, int32_t y) {
        const layout::Point at{placement.x + x, placement.y + y};
        point(SDL_EVENT_MOUSE_MOTION, at);
        point(SDL_EVENT_MOUSE_BUTTON_DOWN, at);
        point(SDL_EVENT_MOUSE_BUTTON_UP, at);
    };
    // Clicks the middle of the part a control with a caption draws.
    const auto click = [&](int32_t control, std::string_view text, std::string_view what) {
        auto* dialog = engine_settings_dialog();
        require(dialog != nullptr, "the dialog closed before " + std::string(what));
        const auto parts = settings::dialog_layout(*dialog);
        const auto* part = find_part(parts, control, text);
        require(part != nullptr, "the dialog shows no " + std::string(what));
        click_at(part->rect.x + part->rect.width / 2, part->rect.y + part->rect.height / 2);
    };
    // Sends a key's press and release; false when it ended the run.
    const auto tap = [this](SDL_Keycode code, SDL_Keymod modifiers = SDL_KMOD_NONE) {
        bool running = true;
        for (const bool down : {true, false}) {
            SDL_Event event{};
            event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
            event.key.windowID = SDL_GetWindowID(sdl_.window);
            event.key.key = code;
            event.key.scancode = SDL_GetScancodeFromKey(code, nullptr);
            event.key.mod = modifiers;
            event.key.down = down;
            dispatch_event(event, running);
        }
        require(running, "a key in the dialog ended the run");
    };
    const auto open = [&](std::string_view what) {
        tap(SDLK_COMMA, kShortcutModifier);
        auto* dialog = engine_settings_dialog();
        require(
            dialog != nullptr && host.dialog_shown,
            "the shortcut did not open the dialog " + std::string(what)
        );
        rest();
        return dialog;
    };
    // Moves the keyboard focus onto a control with Down, at most once round
    // the focus order: the section's rows, the footer's buttons and the
    // sections' entries, and one more press to show the focus.
    const auto focus = [&](int32_t control, std::string_view what) {
        const auto order =
            settings::page_settings(engine_settings_dialog()->page).size() +
            static_cast<std::size_t>(settings::ok_control - settings::restore_control + 1) +
            settings::page_count;
        for (std::size_t presses = 0; presses <= order; ++presses) {
            if (engine_settings_dialog()->focused == control)
                return;
            tap(SDLK_DOWN);
        }
        require(false, "Down never reached " + std::string(what));
    };
    const auto sparks = menu_sparks_;
    const auto frame = [&] {
        menu_sparks_ = sparks;
        return frame_without_cursor();
    };
    const artless::SourceRect dialog_area{
        placement.x, placement.y, settings::dialog_width, settings::dialog_height
    };

    // Every section through its entry in the list: its rows, and nothing
    // outside the dialog changes.
    auto* dialog = open("from the main menu");
    auto before = frame();
    for (const auto page : kPages) {
        const auto name = std::string(page_slug(page));
        click(settings::page_control(page), {}, name + "'s entry in the list");
        dialog = engine_settings_dialog();
        require(dialog->page == page, "a click on " + name + "'s entry did not show it");
        const auto parts = settings::dialog_layout(*dialog);
        const auto rows = settings::page_settings(page);
        for (std::size_t row = 0; row < rows.size(); ++row)
            require(
                shows_text(parts, label_of(rows[row])),
                name + " does not show " + std::string(label_of(rows[row]))
            );
        for (const auto other : kPages)
            if (other != page)
                for (const auto setting : settings::page_settings(other))
                    require(
                        !shows_text(parts, label_of(setting)),
                        name + " shows " + std::string(label_of(setting))
                    );
        rest();
        const auto shown = frame();
        require(
            differing_outside(shown, before, dialog_area) == 0,
            "showing " + name + " changed the menu outside the dialog"
        );
        before = shown;
        require(engine_settings() == defaults, "showing a section changed a setting");
    }

    // The wheel and the scroll keys reach the dialog. Given a section taller
    // than its view, a notch towards the player scrolls it 24 pixels, Page
    // Down 200 more and End to its end, Page Up 200 back and Home to its top;
    // a notch away from the player at the top, a notch towards the player at
    // the end, and a notch outside the dialog, do nothing.
    {
        namespace tall = engine_settings_check;

        // The dialog shows its own sections again however the block ends.
        struct OwnSections {
            Runtime& runtime;

            ~OwnSections() {
                if (auto* shown = runtime.engine_settings_dialog())
                    tall::show_own_sections(*shown);
            }
        } own_sections{*this};

        tall::show_tall_section(*engine_settings_dialog());
        const auto turn_wheel = [&](layout::Point at, float notches) {
            SDL_Event wheel =
                check_host_input::wheel_event(sdl_.renderer, sdl_.window, at.x, at.y, notches);
            bool running = true;
            dispatch_event(wheel, running);
            require(running, "the wheel in the dialog ended the run");
        };
        const auto offset = [this] {
            const auto* open = engine_settings_dialog();
            return open->scroll[static_cast<std::size_t>(open->page)];
        };
        // The dialog's middle, over its rows.
        const layout::Point over_rows{
            placement.x + settings::dialog_width / 2, placement.y + settings::dialog_height / 2
        };
        turn_wheel(over_rows, 1.0F);
        require(offset() == 0, "the wheel scrolled the dialog above its top");
        turn_wheel(over_rows, -1.0F);
        require(
            offset() == tall::kWheelStepPixels, "a notch of the wheel did not scroll the dialog"
        );
        turn_wheel(kRestingPointer, -1.0F);
        require(offset() == tall::kWheelStepPixels, "the wheel outside the dialog scrolled it");
        tap(SDLK_PAGEDOWN);
        require(
            offset() == tall::kWheelStepPixels + tall::kPageStepPixels,
            "Page Down did not scroll the dialog"
        );
        tap(SDLK_END);
        const int32_t end = offset();
        require(
            end > tall::kWheelStepPixels + tall::kPageStepPixels, "End did not scroll to the end"
        );
        turn_wheel(over_rows, -1.0F);
        require(offset() == end, "the wheel scrolled the dialog past its end");
        tap(SDLK_PAGEUP);
        require(offset() == end - tall::kPageStepPixels, "Page Up did not scroll the dialog");
        tap(SDLK_HOME);
        require(offset() == 0, "Home did not scroll back to the top");
        require(
            engine_settings_dialog()->focused == settings::no_control &&
                engine_settings() == defaults,
            "scrolling showed the focus or changed a setting"
        );
    }
    rest();

    // Each setting through the pointer or the keys, in effect at once.
    auto chosen = defaults;
    const auto expect = [&](std::string_view what) {
        require(engine_settings_dialog() != nullptr, "the dialog closed on " + std::string(what));
        require(
            engine_settings_dialog()->chosen == chosen && engine_settings() == chosen,
            std::string(what) + " did not take effect at once"
        );
    };
    click(settings::page_control(settings::Page::path_search), {}, "AI & Pathfinding's entry");
    focus(settings::first_row_control, "Pathfinding cycles");
    tap(SDLK_RIGHT);
    chosen.path_search_nodes = kChosenPathNodes;
    expect("Pathfinding cycles at 2x");
    require(
        shows_text(settings::dialog_layout(*engine_settings_dialog()), "2x"),
        "Pathfinding cycles does not show 2x"
    );

    click(settings::page_control(settings::Page::controls), {}, "Controls & Input's entry");
    click(settings::first_row_control, "OFF", "Mouse wheel zoom's Off");
    chosen.wheel_zoom = false;
    expect("Mouse wheel zoom Off");
    click(settings::first_row_control + 1, "ON", "Escape opens the game menu's On");
    chosen.escape_opens_menu = true;
    expect("Escape opens the game menu On");
    click(settings::first_row_control + 2, "ON", "Select groups without Alt's On");
    chosen.switch_alt = true;
    expect("Select groups without Alt On");
    require(
        (preferences_.graphics_flags & init::preference_flags::switch_alt) != 0,
        "Select groups without Alt did not set SwitchAlt"
    );
    // A click on the side a switch already shows changes nothing.
    click(settings::first_row_control + 2, "ON", "Select groups without Alt's On");
    expect("a second click on On");

    click(settings::page_control(settings::Page::gameplay), {}, "Gameplay's entry");
    focus(settings::first_row_control, "Unit limit");
    tap(SDLK_RIGHT);
    chosen.unit_limit = kChosenUnitLimit;
    expect("Unit limit one stop up");
    require(
        frontend_game().max_units_setting == kChosenUnitLimit,
        "Unit limit did not set the next game's"
    );

    click(settings::page_control(settings::Page::graphics), {}, "Graphics' entry");
    focus(settings::first_row_control, "Maximum frame rate");
    tap(SDLK_LEFT);
    chosen.max_frame_rate = kChosenFrameRate;
    expect("Maximum frame rate one stop down");
    if (!options_.max_frames_per_second_given)
        require(
            options_.max_frames_per_second == kChosenFrameRate,
            "Maximum frame rate did not set the frame rate"
        );
    click(settings::first_row_control + 1, "4x", "Enhanced anti-aliasing's 4x");
    chosen.anti_aliasing = kChosenAntiAliasing;
    expect("Enhanced anti-aliasing at 4x");
    require(
        unit_supersampling_ == oa::present::model::UnitSupersampling::x4,
        "Enhanced anti-aliasing at 4x did not draw units finer"
    );

    click(settings::page_control(settings::Page::developer), {}, "Developer's entry");
    click(settings::first_row_control, "ON", "Show performance statistics' On");
    chosen.frame_stats = true;
    expect("Show performance statistics On");
    require(frame_stats_shown_, "Show performance statistics did not show the statistics");

    // OK keeps them and saves exactly the keys that changed, and the dialog
    // opens again on them.
    click(settings::ok_control, "OK", "OK");
    require(
        engine_settings_dialog() == nullptr && !host.dialog_shown, "OK did not close the dialog"
    );
    require(engine_settings() == chosen, "OK did not keep the settings chosen");
    const std::map<std::string, std::string> expected_keys{
        {std::string(settings::key::path_search_nodes), std::to_string(kChosenPathNodes)},
        {std::string(settings::key::wheel_zoom), "0"},
        {std::string(settings::key::escape_opens_menu), "1"},
        {std::string(settings::key::unit_limit), std::to_string(kChosenUnitLimit)},
        {std::string(settings::key::max_frame_rate), std::to_string(kChosenFrameRate)},
        {std::string(settings::key::anti_aliasing),
         std::to_string(static_cast<int>(kChosenAntiAliasing))},
        {std::string(settings::key::frame_stats), "1"},
    };
    const auto saved = oa::platform::preferences::load(preference_path_);
    require(engine_keys(saved) == expected_keys, "OK did not save exactly the settings changed");
    require(saved_general_number("SwitchAlt") == 1, "OK did not save SwitchAlt");
    dialog = open("again");
    require(
        dialog->opened == chosen && dialog->page == settings::Page::developer,
        "the dialog did not open again on the settings kept and the last section"
    );

    // Cancel (its button) puts back what the dialog opened with and saves nothing.
    click(settings::first_row_control, "OFF", "Show performance statistics' Off");
    click(settings::page_control(settings::Page::controls), {}, "Controls & Input's entry");
    click(settings::first_row_control, "ON", "Mouse wheel zoom's On");
    require(
        !engine_settings().frame_stats && engine_settings().wheel_zoom && !frame_stats_shown_,
        "the changes before Cancel did not take effect at once"
    );
    click(settings::cancel_control, "CANCEL", "Cancel");
    require(engine_settings_dialog() == nullptr, "Cancel did not close the dialog");
    require(
        engine_settings() == chosen && frame_stats_shown_,
        "Cancel did not put back the settings the dialog opened with"
    );
    require(
        oa::platform::preferences::load(preference_path_) == saved,
        "Cancel changed the preferences file"
    );

    // Restore defaults resets every setting at once, Cancel undoes it, and
    // OK after it erases every key it reset.
    dialog = open("for Restore defaults");
    click(settings::restore_control, "RESTORE DEFAULTS", "Restore defaults");
    require(
        engine_settings_dialog() != nullptr && engine_settings() == defaults &&
            !frame_stats_shown_ &&
            unit_supersampling_ == oa::present::model::UnitSupersampling::off,
        "Restore defaults did not reset every setting at once"
    );
    tap(SDLK_ESCAPE);
    require(
        engine_settings_dialog() == nullptr && engine_settings() == chosen,
        "Escape after Restore defaults did not put the settings back"
    );
    dialog = open("for Restore defaults and OK");
    click(settings::restore_control, "RESTORE DEFAULTS", "Restore defaults");
    tap(SDLK_RETURN);
    require(
        engine_settings_dialog() == nullptr && engine_settings() == defaults,
        "Enter after Restore defaults did not keep the defaults"
    );
    const auto restored = oa::platform::preferences::load(preference_path_);
    require(engine_keys(restored).empty(), "Restore defaults and OK left settings in the file");
    require(saved_general_number("SwitchAlt") == 0, "Restore defaults did not save SwitchAlt off");
    rest();
    host.latched_key = 0;
    fake_frontend_tick_ = previous_tick;
    std::cout << "engine settings check: every section, each setting through the dialog's "
                 "pointer and keys, OK, Cancel and Restore defaults, and the keys they save\n";
}

void Runtime::check_engine_settings_window_sizes() {
    std::cout << "engine settings check: the main menu as the window shows it\n";
    auto& host = engine_settings_menu_host();
    const auto previous_tick = fake_frontend_tick_;
    fake_frontend_tick_ = 1000U;
    const auto placement = EngineSettingsMenuHost::dialog_placement();
    const auto point = [this](SDL_EventType type, layout::Point at) {
        send_check_pointer(type, at, type == SDL_EVENT_MOUSE_MOTION ? 0 : SDL_BUTTON_LEFT);
    };
    const auto click = [&](layout::Point at) {
        point(SDL_EVENT_MOUSE_MOTION, at);
        point(SDL_EVENT_MOUSE_BUTTON_DOWN, at);
        point(SDL_EVENT_MOUSE_BUTTON_UP, at);
    };
    const auto tap = [this](SDL_Keycode code, SDL_Keymod modifiers = SDL_KMOD_NONE) {
        bool running = true;
        for (const bool down : {true, false}) {
            SDL_Event event{};
            event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
            event.key.windowID = SDL_GetWindowID(sdl_.window);
            event.key.key = code;
            event.key.scancode = SDL_GetScancodeFromKey(code, nullptr);
            event.key.mod = modifiers;
            event.key.down = down;
            dispatch_event(event, running);
        }
        require(running, "a key ended the run");
    };
    // Writes the window's frame as a step's snapshot when --snapshot names
    // one, the pointer moved out of the way.
    const auto snapshot = [&](const std::string& step) {
        if (options_.snapshot.empty())
            return;
        point(SDL_EVENT_MOUSE_MOTION, kSnapshotPointer);
        renderer::Surface presented;
        capture_frame_ = &presented;
        render();
        capture_frame_ = nullptr;
        write_ppm(step_snapshot(options_.snapshot, step), presented);
        point(SDL_EVENT_MOUSE_MOTION, kRestingPointer);
    };
    const auto sparks = menu_sparks_;
    // The window's frame and the picture it should show, drawn from the
    // same sparks, the picture at the display gamma.
    const auto present = [&](renderer::Surface& presented, renderer::Surface& picture) {
        menu_sparks_ = sparks;
        picture = frame_without_cursor();
        apply_gamma_rgb(picture.rgb.data(), picture.rgb.size() / 3U, 3);
        menu_sparks_ = sparks;
        capture_frame_ = &presented;
        render();
        capture_frame_ = nullptr;
    };

    for (const auto& [width, height] : kWindowSizes) {
        const std::string size = std::to_string(width) + 'x' + std::to_string(height);
        const std::string on = " on the " + size + " window";
        if (!SDL_SetWindowSize(sdl_.window, width, height) || !SDL_SyncWindow(sdl_.window))
            throw std::runtime_error(std::string("SDL_SetWindowSize: ") + SDL_GetError());
        load(Screen::main_menu);
        require(screen_ == Screen::main_menu, "the main menu did not open" + on);
        int window_width = 0;
        int window_height = 0;
        SDL_GetWindowSizeInPixels(sdl_.window, &window_width, &window_height);
        require(window_width == width && window_height == height, "the window is not " + size);
        SDL_FRect area{};
        require(
            SDL_GetRenderLogicalPresentationRect(sdl_.renderer, &area) &&
                area.w >= static_cast<float>(kCanvasWidth) &&
                area.h >= static_cast<float>(kCanvasHeight),
            "the main menu is not letterboxed at 640x480 or more" + on
        );
        point(SDL_EVENT_MOUSE_MOTION, kRestingPointer);

        // The main menu with the OA button, as the window shows it.
        renderer::Surface presented;
        renderer::Surface picture;
        present(presented, picture);
        const auto button = EngineSettingsMenuHost::button_rect(*this);
        require(
            button.x == kMenuButtonCorner.x && button.y == kMenuButtonCorner.y &&
                EngineSettingsMenuHost::button_shown(*this),
            "the OA button is not in the picture's bottom-right corner" + on
        );
        const auto differing =
            letterbox_differences(presented, picture, area, window_width, kRestingPointer);
        require(
            differing == 0,
            "the window does not show the main menu with its OA button" + on + ": " +
                std::to_string(differing) + " pixels differ"
        );
        snapshot("menu-" + size);

        // A click on the button where the window shows it opens the dialog;
        // each section shows as the window shows the picture.
        click({button.x + button.width / 2, button.y + button.height / 2});
        auto* dialog = engine_settings_dialog();
        require(
            dialog != nullptr && host.dialog_shown,
            "a click on the OA button did not open the dialog" + on
        );
        for (const auto page : kPages) {
            const auto parts = settings::dialog_layout(*dialog);
            const auto* entry = find_part(parts, settings::page_control(page), {});
            require(entry != nullptr, "the dialog has no entry for a section" + on);
            click(
                {placement.x + entry->rect.x + entry->rect.width / 2,
                 placement.y + entry->rect.y + entry->rect.height / 2}
            );
            require(dialog->page == page, "a click on a section's entry did not show it" + on);
            point(SDL_EVENT_MOUSE_MOTION, kRestingPointer);
            present(presented, picture);
            const auto shown =
                letterbox_differences(presented, picture, area, window_width, kRestingPointer);
            require(
                shown == 0,
                "the window does not show the dialog's " + std::string(page_slug(page)) + on +
                    ": " + std::to_string(shown) + " pixels differ"
            );
            snapshot("menu-dialog-" + std::string(page_slug(page)) + '-' + size);
        }
        tap(SDLK_ESCAPE);
        require(engine_settings_dialog() == nullptr, "Escape did not close the dialog" + on);
        std::cout << "engine settings check: the OA button and the dialog's sections as the "
                  << size << " window shows them, the picture at " << area.x << ',' << area.y << ' '
                  << area.w << 'x' << area.h << '\n';
    }
    if (!SDL_SetWindowSize(sdl_.window, kDefaultWindowWidth, kDefaultWindowHeight) ||
        !SDL_SyncWindow(sdl_.window))
        throw std::runtime_error(std::string("SDL_SetWindowSize: ") + SDL_GetError());
    load(Screen::main_menu);
    host.latched_key = 0;
    fake_frontend_tick_ = previous_tick;
}

} // namespace oa::app
