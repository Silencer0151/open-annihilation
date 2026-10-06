// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// --check-engine-settings, the dialog's part: the dialog on the main menu
// driven through the SDL presenter's pointer and keys (every section, each
// setting changed and in effect at once, Vertical sync read back from the
// renderer, Menu scaling's whole steps in the window's presentation at once,
// Native pixel density kept for the next start, Explosion flash's Reduced in
// effect for the next frame drawn, OK, Cancel and Restore
// defaults and the preferences they
// leave, Developer Mode's overrides in effect and kept), and the main menu
// with its OA button and the dialog as the window shows them at several
// sizes, the Graphics section at its top and its end, and the Developer
// section with an area of Developer Mode's list open, off and on.

#include "check_host_input.hpp"
#include "engine_settings_menu_host.hpp"
#include "engine_settings_state.hpp"
#include "engine_settings_tall_section.hpp"

#include "oa/app/acceleration_status.hpp"
#include "oa/app/game_directory.hpp"
#include "oa/app/game_files_hooks.hpp"
#include "oa/app/mod_profile_loader.hpp"
#include "oa/app/runtime.hpp"
#include "oa/data/mod_profile/overrides.hpp"
#include "oa/platform/preferences.hpp"
#include "oa/ui/engine_settings/dialog.hpp"
#include "oa/ui/frontend_renderer/artless.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
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

/// The profile of the mod folder the check picks: one that changes nothing.
constexpr std::string_view kPickedProfile = "oamod: 1\n"
                                            "id: picked-check\n"
                                            "name: Picked folder check\n"
                                            "version: \"1.0\"\n"
                                            "requires: {base: ta-3.1c, catalogue: 1}\n"
                                            "author: {name: unknown}\n"
                                            "packaging: {revision: 1, date: 2026-10-04, "
                                            "packager: Open Annihilation}\n";

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
    case settings::Page::mods:
        return "mods";
    case settings::Page::controls:
        return "controls";
    case settings::Page::common_tweaks:
        return "tweaks";
    case settings::Page::graphics:
        return "graphics";
    case settings::Page::language:
        return "language";
    case settings::Page::touch:
        return "touch";
    case settings::Page::controller:
        return "controller";
    case settings::Page::developer:
        return "developer";
    case settings::Page::game_files:
        return "game-files";
    case settings::Page::mod_keys:
    case settings::Page::mod_patrol:
    case settings::Page::mod_guard:
    case settings::Page::mod_tools:
    case settings::Page::mod_chat:
        return "mod";
    }
    return "page";
}

/// The dialog's sections, in the order its list shows them.
constexpr std::array<settings::Page, 6> kPages{
    settings::Page::mods,
    settings::Page::controls,
    settings::Page::common_tweaks,
    settings::Page::language,
    settings::Page::graphics,
    settings::Page::developer,
};

/// The first area of Developer Mode's list, which the check opens.
constexpr std::string_view kFirstArea = "AI";
/// The area of the display (view-scope) hacks, as the list names it.
constexpr std::string_view kDisplayArea = "Interface";
/// A rule (sim-scope) hack of the first area, ai.attack-wave-size, as the
/// list names it; the check turns it on.
constexpr std::string_view kRuleHack = "Attack Wave Size";
/// The first area's first hack, which the snapshots show open and on.
constexpr std::string_view kShownHack = kRuleHack;
/// A display (view-scope) hack, ui.whiteboard, as the list names it; the
/// check turns it on.
constexpr std::string_view kDisplayHack = "Whiteboard";

/// Returns Show Active Only's label for a count of hacks that are on.
///
/// @param active the hacks that are on
/// @return "Show Active Only (X/Y)" with every standard hack as Y
std::string active_only_label(std::size_t active) {
    return "Show Active Only (" + std::to_string(active) + "/" +
           std::to_string(oa::data::mod_profile::standard_hacks().size()) + ")";
}

/// Finds the part of a hack's switch, Off or On, on the hack's own line.
///
/// @param parts the dialog's layout
/// @param hack the hack's title, as its header shows it
/// @param caption "OFF" or "ON"
/// @return the switch's half; nullptr when the hack does not show
const settings::LayoutPart* hack_switch(
    const std::vector<settings::LayoutPart>& parts, std::string_view hack, std::string_view caption
) {
    const settings::LayoutPart* header = nullptr;
    for (const auto& part : parts)
        if (part.text == hack)
            header = &part;
    if (header == nullptr)
        return nullptr;
    // The switch's halves lie one row under the header's label line's top.
    for (const auto& part : parts)
        if (part.text == caption && part.rect.y == header->rect.y + 1 &&
            part.rect.x > header->rect.x + header->rect.width)
            return &part;
    return nullptr;
}

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
    case settings::Setting::developer_mode:
        return "Enable Developer Mode";
    case settings::Setting::frame_stats:
        return "Show performance statistics";
    case settings::Setting::hardware_acceleration:
        return "Hardware acceleration";
    case settings::Setting::vertical_sync:
        return "Vertical sync";
    case settings::Setting::menu_scaling:
        return "Menu scaling";
    case settings::Setting::native_density:
        return "Native pixel density";
    case settings::Setting::explosion_flash:
        return "Explosion flash";
    case settings::Setting::modern_fonts:
        return "Use modern fonts for game text";
    case settings::Setting::text_outline:
        return "Font outline";
    case settings::Setting::text_shadow:
        return "Font shadow";
    case settings::Setting::text_background:
        return "Game text background";
    case settings::Setting::unicode_chat:
        return "Enable Unicode Multiplayer Chat";
    case settings::Setting::text_size:
        return "Text size";
    case settings::Setting::language:
        return "Language";
    case settings::Setting::mod:
        // Mods' rows draw their texts inside the row; the button under them
        // is what the layout names.
        return "OPEN MODS FOLDER";
    case settings::Setting::user_folder:
        return "Your files";
    default:
        return {};
    }
}

/// Tells whether the renderer waits for the display, as SDL reports it.
///
/// @param renderer the renderer
/// @return true when SDL_GetRenderVSync reads 1
bool renderer_waits(SDL_Renderer* renderer) {
    int vsync = 0;
    return SDL_GetRenderVSync(renderer, &vsync) && vsync == 1;
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

/// A layout made for the call would be gone before its part is read.
const settings::LayoutPart* find_part(
    std::vector<settings::LayoutPart>&& parts, int32_t control, std::string_view text
) = delete;

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
    const auto* menu_dialog = open("from the main menu");
    // The main menu's dialog lists Game files only where the platform brings
    // game files in; this game lists it exactly then.
    const bool game_files = game_files_import_offered(game_files_hooks());
    require(
        menu_dialog->game_files == game_files &&
            shows_text(settings::dialog_layout(*menu_dialog), "Game files") == game_files,
        game_files ? "the main menu's dialog does not list Game files"
                   : "the main menu's dialog lists Game files where no game files are brought in"
    );
    auto before = frame();
    for (const auto page : kPages) {
        const auto name = std::string(page_slug(page));
        click(settings::page_control(page), {}, name + "'s entry in the list");
        auto* dialog = engine_settings_dialog();
        require(dialog->page == page, "a click on " + name + "'s entry did not show it");
        auto parts = settings::dialog_layout(*dialog);
        // A section taller than its view shows the rest of its rows at its end.
        if (dialog->scroll[static_cast<std::size_t>(page)] == 0) {
            tap(SDLK_END);
            const auto at_end = settings::dialog_layout(*dialog);
            parts.insert(parts.end(), at_end.begin(), at_end.end());
            tap(SDLK_HOME);
        }
        const auto rows = settings::page_settings(page);
        for (std::size_t row = 0; row < rows.size(); ++row)
            require(
                shows_text(parts, label_of(rows[row])),
                name + " does not show " + std::string(label_of(rows[row]))
            );
        // The list of sections names Language, as its row does: only the
        // open section's own parts count.
        std::erase_if(parts, [](const settings::LayoutPart& part) {
            return part.control >= settings::first_page_control &&
                   part.control < settings::restore_control;
        });
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
    click(settings::page_control(settings::Page::common_tweaks), {}, "Common Tweaks' entry");
    focus(settings::first_row_control + 2, "Pathfinding cycles");
    tap(SDLK_RIGHT);
    chosen.path_search_nodes = kChosenPathNodes;
    expect("Pathfinding cycles at 2x");
    require(
        shows_text(settings::dialog_layout(*engine_settings_dialog()), "2x"),
        "Pathfinding cycles does not show 2x"
    );

    click(settings::page_control(settings::Page::controls), {}, "Controls' entry");
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

    click(settings::page_control(settings::Page::common_tweaks), {}, "Common Tweaks' entry");
    focus(settings::first_row_control + 1, "Unit limit");
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
    // Screen size offers Desktop, then every size the display offers (the
    // made-up monitor's with --display-modes), the narrower first; Right
    // steps to the largest and stops there. It applies from the next start.
    {
        const auto offered = EngineSettingsState::offered_screen_sizes(*this);
        std::vector<settings::ScreenSize> stops{settings::desktop_screen_size};
        stops.insert(stops.end(), offered.begin(), offered.end());
        require(
            engine_settings_dialog()->offered_screen_sizes == stops,
            "Screen size does not offer Desktop and the display's sizes"
        );
        focus(settings::first_row_control + 2, "Screen size");
        for (std::size_t step = 0; step <= offered.size(); ++step)
            tap(SDLK_RIGHT);
        chosen.screen_size = offered.back();
        expect("Screen size at the display's largest");
        const std::string largest =
            std::to_string(offered.back().width) + " x " + std::to_string(offered.back().height);
        require(
            shows_text(settings::dialog_layout(*engine_settings_dialog()), largest),
            "Screen size at its last stop does not show " + largest
        );
        std::cout << "engine settings check: Screen size offers Desktop and " << offered.size()
                  << " sizes, up to " << largest << '\n';
    }
    // Hardware acceleration, Off with a named preferences file, says the
    // processor draws, or, under 2 GiB, that the machine needs more memory;
    // it is never switched here, so that every frame the check compares is
    // drawn as without the setting. It is locked under 2 GiB, and on SDL's
    // software renderer or the environment's driver unless --force-capable
    // lifts them; a renderer nothing has looked at leaves it unlocked.
    auto* dialog = engine_settings_dialog();
    const auto facts = acceleration_facts();
    const bool memory = enough_memory_for_acceleration(facts.physical_memory);
    require(
        dialog->acceleration.state == (memory ? settings::AccelerationState::off_by_setting
                                              : settings::AccelerationState::needs_memory),
        "Hardware acceleration's status does not say it is Off, or why not under 2 GiB"
    );
    const bool ruled_out =
        !memory || (!facts.force_capable && (facts.software_renderer || facts.environment_driver));
    require(
        dialog->locks.hardware_acceleration ==
            (ruled_out ? settings::Lock::unavailable : settings::Lock::none),
        "Hardware acceleration is not locked as the renderer and the memory give it"
    );
    // Vertical sync: Down to it scrolls Graphics to show it whole, and Right
    // turns it On, in effect at once: the renderer waits for the display. On
    // SDL's software renderer it is out of reach unless --force-capable lifts
    // it, as the command that registers this check does.
    const bool vertical_sync = dialog->locks.vertical_sync == settings::Lock::none;
    require(
        vertical_sync == options_.force_capable,
        "Vertical sync is within reach otherwise than as --force-capable gives it"
    );
    if (vertical_sync) {
        require(!renderer_waits(sdl_.renderer), "the renderer waits for the display at Off");
        focus(settings::first_row_control + 4, "Vertical sync");
        require(
            dialog->scroll[static_cast<std::size_t>(settings::Page::graphics)] == 72,
            "the focus on Vertical sync did not scroll Graphics to show it"
        );
        require(
            shows_text(
                settings::dialog_layout(*dialog),
                memory ? "Off: the processor draws and scales the view."
                       : "Not in use: it needs at least 2 GB of memory."
            ),
            "Graphics at its end does not show Hardware acceleration's status"
        );
        tap(SDLK_RIGHT);
        chosen.vertical_sync = true;
        expect("Vertical sync On");
        require(renderer_waits(sdl_.renderer), "Vertical sync On did not reach the renderer");
    } else {
        require(
            dialog->locks.vertical_sync == settings::Lock::unavailable,
            "Vertical sync is locked other than on the software renderer"
        );
    }
    // Menu scaling: Whole steps sets the window's presentation at once, in
    // whole steps where the window holds the menu, else letterboxed.
    const auto presentation = [this] {
        int width = 0;
        int height = 0;
        SDL_RendererLogicalPresentation mode = SDL_LOGICAL_PRESENTATION_DISABLED;
        require(
            SDL_GetRenderLogicalPresentation(sdl_.renderer, &width, &height, &mode),
            "the renderer has no logical presentation"
        );
        return mode;
    };
    const auto held_whole = [this] {
        int width = 0;
        int height = 0;
        require(
            SDL_GetRenderOutputSize(sdl_.renderer, &width, &height), "the renderer has no size"
        );
        return render_policy::frame_fit(
                   render_policy::MenuScaling::whole_steps,
                   width,
                   height,
                   kCanvasWidth,
                   kCanvasHeight
               ) == render_policy::FrameFit::whole_steps;
    };
    require(
        presentation() == SDL_LOGICAL_PRESENTATION_LETTERBOX,
        "Sharp does not letterbox the main menu"
    );
    focus(settings::first_row_control + 5, "Menu scaling");
    click(settings::first_row_control + 5, "Whole steps", "Menu scaling's Whole steps");
    chosen.menu_scaling = settings::MenuScaling::whole_steps;
    expect("Menu scaling Whole steps");
    require(
        presentation() == (held_whole() ? SDL_LOGICAL_PRESENTATION_INTEGER_SCALE
                                        : SDL_LOGICAL_PRESENTATION_LETTERBOX),
        "Menu scaling Whole steps did not set the window's presentation at once"
    );
    // Native pixel density: On is kept for the next start, and the window
    // keeps the density it opened at.
    const bool dense = native_density_window();
    focus(settings::first_row_control + 6, "Native pixel density");
    click(settings::first_row_control + 6, "ON", "Native pixel density's On");
    chosen.native_density = true;
    expect("Native pixel density On");
    require(
        native_density_window() == dense &&
            shows_text(settings::dialog_layout(*dialog), "Applies from the next start."),
        "Native pixel density On changed the open window, or does not say when it applies"
    );
    // Explosion flash: Reduced is the level the next frame draws.
    focus(settings::first_row_control + 7, "Explosion flash");
    click(settings::first_row_control + 7, "Reduced", "Explosion flash's Reduced");
    chosen.explosion_flash = settings::ExplosionFlash::reduced;
    expect("Explosion flash Reduced");
    require(
        explosion_flash() == settings::ExplosionFlash::reduced,
        "Explosion flash Reduced is not the level the frames draw"
    );

    // The text drawing reads the Language section at once. Text size
    // waits for the modern fonts, Off with a named preferences file: locked,
    // it says so; with them On, Right raises it a stop.
    click(settings::page_control(settings::Page::language), {}, "Language's entry");
    require(
        !chosen.modern_fonts &&
            shows_text(settings::dialog_layout(*engine_settings_dialog()), "Needs modern fonts"),
        "Text size is not locked while the modern fonts are Off"
    );
    // The Language drop-down: a click on its field opens its list, a click
    // on an item chooses the language at once, and Left steps it back.
    require(
        chosen.language == "en" && game_language() != nullptr &&
            std::string_view(game_language()) == "English",
        "a named preferences file does not play in English"
    );
    click(settings::first_row_control, "English", "Language's field");
    require(
        engine_settings_dialog()->open_list == settings::first_row_control,
        "a click on Language's field did not open its list"
    );
    click(settings::no_control, "Deutsch", "Language's Deutsch");
    chosen.language = "de";
    expect("Language Deutsch");
    require(
        shown_language().tag == "de" && game_language() != nullptr &&
            std::string_view(game_language()) == "German" &&
            engine_settings_dialog()->open_list == settings::no_control,
        "Language Deutsch did not put German in effect at once"
    );
    focus(settings::first_row_control, "Language");
    tap(SDLK_LEFT);
    chosen.language = "en";
    expect("Language English");
    require(shown_language().tag == "en", "Language English did not put English back");
    click(settings::first_row_control + 1, "ON", "Use modern fonts for game text's On");
    chosen.modern_fonts = true;
    expect("Use modern fonts for game text On");
    require(
        !shows_text(settings::dialog_layout(*engine_settings_dialog()), "Needs modern fonts"),
        "Text size stayed locked with the modern fonts On"
    );
    focus(settings::first_row_control + 2, "Text size");
    tap(SDLK_RIGHT);
    chosen.text_size = settings::default_text_size + settings::text_size_step;
    expect("Text size a stop up");
    require(
        text_style().size == chosen.text_size &&
            shows_text(settings::dialog_layout(*engine_settings_dialog()), "90%"),
        "Text size a stop up did not reach the text style"
    );
    // Font shadow lies under the view's edge: the focus brings it in.
    focus(settings::first_row_control + 4, "Font shadow");
    tap(SDLK_LEFT);
    chosen.text_shadow = false;
    expect("Font shadow Off");
    require(
        text_style() == settings::text_style(chosen) && !text_style().shadow &&
            text_style().outline,
        "Font shadow Off did not reach the text style"
    );

    click(settings::page_control(settings::Page::developer), {}, "Developer's entry");
    click(settings::first_row_control + 1, "ON", "Show performance statistics' On");
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
    std::map<std::string, std::string> expected_keys{
        {std::string(settings::key::path_search_nodes), std::to_string(kChosenPathNodes)},
        {std::string(settings::key::wheel_zoom), "0"},
        {std::string(settings::key::escape_opens_menu), "1"},
        {std::string(settings::key::unit_limit), std::to_string(kChosenUnitLimit)},
        {std::string(settings::key::max_frame_rate), std::to_string(kChosenFrameRate)},
        {std::string(settings::key::anti_aliasing),
         std::to_string(static_cast<int>(kChosenAntiAliasing))},
        {std::string(settings::key::frame_stats), "1"},
        {std::string(settings::key::modern_fonts), "1"},
        {std::string(settings::key::text_shadow), "0"},
        {std::string(settings::key::text_size), std::to_string(chosen.text_size)},
        {std::string(settings::key::screen_size), settings::screen_size_text(chosen.screen_size)},
    };
    if (vertical_sync)
        expected_keys.emplace(std::string(settings::key::vertical_sync), "1");
    expected_keys.emplace(std::string(settings::key::menu_scaling), "whole-steps");
    expected_keys.emplace(std::string(settings::key::native_density), "1");
    expected_keys.emplace(std::string(settings::key::explosion_flash), "reduced");
    const auto saved = oa::platform::preferences::load(preference_path_);
    require(engine_keys(saved) == expected_keys, "OK did not save exactly the settings changed");
    require(saved_general_number("SwitchAlt") == 1, "OK did not save SwitchAlt");
    dialog = open("again");
    require(
        dialog->opened == chosen && dialog->page == settings::Page::developer,
        "the dialog did not open again on the settings kept and the last section"
    );

    // Cancel (its button) puts back what the dialog opened with and saves nothing.
    click(settings::first_row_control + 1, "OFF", "Show performance statistics' Off");
    click(settings::page_control(settings::Page::controls), {}, "Controls' entry");
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
    open("for Restore defaults");
    click(settings::restore_control, "RESTORE DEFAULTS", "Restore defaults");
    require(
        engine_settings_dialog() != nullptr && engine_settings() == defaults &&
            !frame_stats_shown_ &&
            unit_supersampling_ == oa::present::model::UnitSupersampling::off,
        "Restore defaults did not reset every setting at once"
    );
    require(!renderer_waits(sdl_.renderer), "Restore defaults did not stop the renderer waiting");
    require(
        presentation() == SDL_LOGICAL_PRESENTATION_LETTERBOX,
        "Restore defaults did not letterbox the main menu again"
    );
    tap(SDLK_ESCAPE);
    require(
        engine_settings_dialog() == nullptr && engine_settings() == chosen,
        "Escape after Restore defaults did not put the settings back"
    );
    require(
        renderer_waits(sdl_.renderer) == vertical_sync,
        "Escape after Restore defaults did not put Vertical sync back"
    );
    open("for Restore defaults and OK");
    click(settings::restore_control, "RESTORE DEFAULTS", "Restore defaults");
    tap(SDLK_RETURN);
    require(
        engine_settings_dialog() == nullptr && engine_settings() == defaults,
        "Enter after Restore defaults did not keep the defaults"
    );
    const auto restored = oa::platform::preferences::load(preference_path_);
    require(engine_keys(restored).empty(), "Restore defaults and OK left settings in the file");
    require(!renderer_waits(sdl_.renderer), "Restore defaults and OK left the renderer waiting");
    require(saved_general_number("SwitchAlt") == 0, "Restore defaults did not save SwitchAlt off");

    // Developer Mode: off, its list shows the profile and takes no change;
    // on, an override of a rule (sim-scope) hack plays at once with no
    // match running and one of a display (view-scope) hack shows at once;
    // OK keeps them under the base game's id, Restore profile values clears
    // them, and Off plays the profile as it ships.
    {
        namespace profiles = oa::data::mod_profile;
        const std::string overrides_key =
            std::string(settings::key::hack_overrides) + std::string(profiles::base_game_id);
        const std::string mode_key{settings::key::developer_mode};
        const auto parts_now = [&] { return settings::dialog_layout(*engine_settings_dialog()); };
        // Clicks the middle of a part with a text, whatever its control.
        const auto click_text = [&](std::string_view text, std::string_view what) {
            const auto parts = parts_now();
            const settings::LayoutPart* found = nullptr;
            for (const auto& part : parts)
                if (part.text == text)
                    found = &part;
            require(found != nullptr, "Developer Mode shows no " + std::string(what));
            click_at(found->rect.x + found->rect.width / 2, found->rect.y + found->rect.height / 2);
        };
        const auto click_hack = [&](std::string_view hack, std::string_view caption) {
            const auto parts = parts_now();
            const auto* half = hack_switch(parts, hack, caption);
            require(half != nullptr, "Developer Mode shows no switch of " + std::string(hack));
            click_at(half->rect.x + half->rect.width / 2, half->rect.y + half->rect.height / 2);
        };
        // Scrolls the list a page at a time until it shows a text.
        const auto reveal = [&](std::string_view text) {
            tap(SDLK_HOME);
            for (int32_t pages = 0; pages < 32; ++pages) {
                if (shows_text(parts_now(), text))
                    return true;
                tap(SDLK_PAGEDOWN);
            }
            return shows_text(parts_now(), text);
        };
        open("for Developer Mode");
        click(settings::page_control(settings::Page::developer), {}, "Developer's entry");
        dialog = engine_settings_dialog();
        require(dialog->page == settings::Page::developer, "Developer did not show");
        auto parts = parts_now();
        require(
            shows_text(parts, "Enable Developer Mode") &&
                shows_text(parts, "Show performance statistics") &&
                shows_text(parts, active_only_label(0)),
            "Developer does not show its switches with no hack on"
        );
        require(!shows_text(parts, kRuleHack), "an area of Developer Mode starts open");
        click_text(kFirstArea, "first area");
        require(reveal(kRuleHack), "a click on an area did not open it");
        click_hack(kRuleHack, "ON");
        require(
            engine_settings_dialog()->chosen.hack_overrides.empty() && mod_profile() == nullptr,
            "a hack's switch took a change while Developer Mode was off"
        );
        click(settings::developer_mode_control, "ON", "Enable Developer Mode's On");
        require(developer_mode(), "Enable Developer Mode did not turn it on at once");
        click_hack(kRuleHack, "ON");
        const auto* rules = mod_profile();
        require(
            rules != nullptr && rules->rules.ai.attack_wave_size.enabled &&
                shows_text(parts_now(), active_only_label(1)),
            "a rule hack turned on did not play at once with no match running"
        );
        require(reveal(kDisplayArea), "Developer Mode does not list the display area");
        click_text(kDisplayArea, "display area");
        require(reveal(kDisplayHack), "the display area did not open");
        click_hack(kDisplayHack, "ON");
        require(
            ui_rules().whiteboard.enabled &&
                engine_settings_dialog()->chosen.hack_overrides.size() == 2,
            "a display hack turned on did not show at once"
        );
        click(settings::ok_control, "OK", "OK");
        const auto kept = oa::platform::preferences::load(preference_path_);
        require(
            kept.contains(mode_key) && kept.at(mode_key) == "1" && kept.contains(overrides_key) &&
                profiles::read_overrides(kept.at(overrides_key)).size() == 2,
            "OK did not keep Developer Mode and its overrides under the base game's id"
        );
        dialog = open("for Restore profile values");
        require(dialog->page == settings::Page::developer, "the dialog did not open on Developer");
        click(
            settings::restore_profile_control, "RESTORE PROFILE VALUES", "Restore profile values"
        );
        require(
            engine_settings_dialog()->chosen.hack_overrides.empty() && mod_profile() == nullptr &&
                !ui_rules().whiteboard.enabled,
            "Restore profile values did not put the profile's values back at once"
        );
        click(settings::developer_mode_control, "OFF", "Enable Developer Mode's Off");
        require(!developer_mode(), "Enable Developer Mode did not turn it off at once");
        click(settings::ok_control, "OK", "OK");
        const auto cleared = oa::platform::preferences::load(preference_path_);
        require(
            !cleared.contains(overrides_key) && cleared.contains(mode_key) &&
                cleared.at(mode_key) == "0",
            "Restore profile values and Off did not leave the overrides out of the file"
        );
        // Restore defaults and OK leave no key of the settings behind, and the
        // list opens closed again.
        open("after Developer Mode");
        click(settings::restore_control, "RESTORE DEFAULTS", "Restore defaults");
        tap(SDLK_RETURN);
        require(
            engine_keys(oa::platform::preferences::load(preference_path_)).empty() &&
                engine_settings() == defaults,
            "Restore defaults and OK after Developer Mode left settings in the file"
        );
        engine_settings_state().last_developer_list.reset();
        engine_settings_state().last_page = settings::Page::common_tweaks;
    }

    // With touch controls the dialog lists Touch between Graphics and
    // Developer, with its five rows at their defaults; a finger's press near
    // a switch takes it, and a mouse press there does nothing.
    {
        // The touch controls go back off however the block ends.
        struct TouchControlsOn {
            Runtime& runtime;
            bool before;

            ~TouchControlsOn() {
                runtime.options_.touch_controls = before;
                runtime.engine_settings_state().finger_pointer = false;
                runtime.engine_settings_state().last_page = settings::Page::common_tweaks;
            }
        } touch_on{*this, options_.touch_controls};

        options_.touch_controls = true;
        // A finger's click as the touch controls send it: on the picture's
        // pixels, from no window, from the touch mouse.
        const auto finger = [this](SDL_EventType type, layout::Point at) {
            SDL_Event event{};
            event.type = type;
            if (type == SDL_EVENT_MOUSE_MOTION) {
                event.motion.which = SDL_TOUCH_MOUSEID;
                event.motion.x = static_cast<float>(at.x);
                event.motion.y = static_cast<float>(at.y);
            } else {
                event.button.which = SDL_TOUCH_MOUSEID;
                event.button.button = SDL_BUTTON_LEFT;
                event.button.down = type == SDL_EVENT_MOUSE_BUTTON_DOWN;
                event.button.clicks = 1;
                event.button.x = static_cast<float>(at.x);
                event.button.y = static_cast<float>(at.y);
            }
            bool running = true;
            dispatch_event(event, running);
            require(running, "a finger in the dialog ended the run");
        };
        const auto finger_click = [&](layout::Point at) {
            finger(SDL_EVENT_MOUSE_MOTION, at);
            finger(SDL_EVENT_MOUSE_BUTTON_DOWN, at);
            finger(SDL_EVENT_MOUSE_BUTTON_UP, at);
        };
        dialog = open("with touch controls");
        require(dialog->touch, "the dialog opened with touch controls does not list Touch");
        auto parts = settings::dialog_layout(*dialog);
        const auto* graphics_entry =
            find_part(parts, settings::page_control(settings::Page::graphics), {});
        const auto* touch_entry =
            find_part(parts, settings::page_control(settings::Page::touch), {});
        const auto* developer_entry =
            find_part(parts, settings::page_control(settings::Page::developer), {});
        require(
            graphics_entry != nullptr && touch_entry != nullptr && developer_entry != nullptr &&
                touch_entry->text == "Touch" && graphics_entry->rect.y < touch_entry->rect.y &&
                touch_entry->rect.y < developer_entry->rect.y,
            "Touch is not listed between Graphics and Developer"
        );
        click(settings::page_control(settings::Page::touch), {}, "Touch's entry");
        require(engine_settings_dialog()->page == settings::Page::touch, "Touch did not show");
        parts = settings::dialog_layout(*engine_settings_dialog());
        for (const std::string_view text :
             {"One-finger drag",
              "Automatic",
              "Hold delay",
              "350 ms",
              "QUEUE and ADD",
              "Stay on",
              "Haptics"})
            require(shows_text(parts, text), "Touch does not show " + std::string(text));
        // Haptics, the fourth row, is On; a press left of its switch, two
        // thirds of the touch controls' reach from its Off half, takes it.
        const auto* haptics_off = find_part(parts, settings::first_row_control + 3, "OFF");
        require(haptics_off != nullptr && engine_settings().touch_haptics, "Haptics is not On");
        const int32_t reach = EngineSettingsState::finger_reach(*this, 1.0);
        const layout::Point beside{
            placement.x + haptics_off->rect.x - 1 - reach * 2 / 3,
            placement.y + haptics_off->rect.y + haptics_off->rect.height / 2
        };
        click_at(beside.x - placement.x, beside.y - placement.y);
        require(engine_settings().touch_haptics, "a mouse press beside Haptics' switch took it");
        finger_click({beside.x - reach, beside.y});
        require(engine_settings().touch_haptics, "a finger's press out of reach took Haptics");
        finger_click(beside);
        require(
            !engine_settings().touch_haptics,
            "a finger's press beside Haptics' switch did not take it"
        );
        tap(SDLK_ESCAPE);
        require(
            engine_settings_dialog() == nullptr && engine_settings() == defaults,
            "Escape did not put Haptics back"
        );
    }
    // Mods: the mod played first, marked PLAYING, then the mods by title as
    // their oamod.yaml names them, a folder without one by its name, and a
    // folder an earlier version's Pick Folder... stored while it is the mod
    // stored and still a folder. A click on another row asks the Switch Mod
    // question, which CANCEL puts away with nothing changed. --base-game
    // locks the page and says so; SWITCH from the picked folder played to No
    // Mod forgets the folder, as a start does a picked folder that is not
    // the mod stored; a stored mod folder that is gone reads as No Mod,
    // played, and OK replaces it.
    {
        auto& state = engine_settings_state();
        const fs::path base = fs::absolute(preference_path_).parent_path();
        const fs::path empty_folder = (base / "picked-empty").lexically_normal();
        const fs::path mod_folder = (base / "picked-mod").lexically_normal();
        fs::create_directories(empty_folder);
        fs::create_directories(mod_folder);
        {
            std::ofstream profile(mod_folder / "oamod.yaml", std::ios::binary);
            profile << kPickedProfile;
        }
        const std::string picked = path_to_utf8(mod_folder);
        const std::string empty = path_to_utf8(empty_folder);
        const std::string mod_key{settings::key::mod_directory};
        const std::string picked_key{settings::key::picked_mod_directory};
        preference_values_[mod_key] = picked;
        preference_values_[picked_key] = picked;
        list_offered_mods();
        const auto parts_now = [&] { return settings::dialog_layout(*engine_settings_dialog()); };
        open("for Mods");
        click(settings::page_control(settings::Page::mods), {}, "Mods' entry");
        // Out of a game the main menu chooses the mod: the page is not locked.
        require(
            engine_settings_dialog()->locks.mod == settings::Lock::none &&
                !shows_text(
                    parts_now(), "Locked during a game. Choose the mod from the main menu."
                ),
            "Mods is locked on the main menu"
        );
        {
            const auto listed = parts_now();
            const auto* listing = engine_settings_dialog();
            const auto rows = settings::mod_rows(*listing);
            const auto titled = std::find(
                listing->mod_names.begin(), listing->mod_names.end(), "Picked folder check"
            );
            require(
                !rows.empty() && rows.front().playing &&
                    rows.front().offered == settings::no_mod_row &&
                    titled != listing->mod_names.end() &&
                    listing->mod_folders[static_cast<std::size_t>(
                        std::distance(listing->mod_names.begin(), titled)
                    )] == picked &&
                    shows_text(listed, "OPEN MODS FOLDER"),
                "Mods does not list No Mod played first and the stored picked folder by its title"
            );
        }
        const auto row_of = [&](const std::string& folder) {
            const auto* dialog_now = engine_settings_dialog();
            const auto rows = settings::mod_rows(*dialog_now);
            for (std::size_t index = 0; index < rows.size(); ++index)
                if (rows[index].offered >= 0 &&
                    dialog_now->mod_folders[static_cast<std::size_t>(rows[index].offered)] ==
                        folder)
                    return settings::first_row_control + static_cast<int32_t>(index);
            return settings::no_control;
        };
        const int32_t picked_row = row_of(picked);
        require(picked_row != settings::no_control, "Mods does not list the stored picked folder");
        click(picked_row, {}, "the picked folder's row");
        {
            const auto asked = parts_now();
            require(
                engine_settings_dialog()->switch_question != settings::no_question &&
                    shows_text(asked, "SWITCH MOD") &&
                    find_part(asked, settings::question_yes_control, "SWITCH") != nullptr &&
                    find_part(asked, settings::question_no_control, "CANCEL") != nullptr,
                "a click on another mod's row did not ask the Switch Mod question"
            );
        }
        if (!options_.snapshot.empty())
            write_ppm(step_snapshot(options_.snapshot, "menu-dialog-mod-question"), frame());
        tap(SDLK_ESCAPE);
        require(
            engine_settings_dialog() != nullptr &&
                engine_settings_dialog()->switch_question == settings::no_question &&
                engine_settings_dialog()->chosen.mod_folder.empty() &&
                engine_settings().mod_folder.empty(),
            "Escape on the question did not put it away and keep the Mod as it was"
        );
        click(picked_row, {}, "the picked folder's row");
        click(settings::question_no_control, "CANCEL", "the question's CANCEL");
        require(
            engine_settings_dialog()->switch_question == settings::no_question &&
                engine_settings_dialog()->chosen.mod_folder.empty(),
            "CANCEL changed the Mod"
        );
        click(settings::cancel_control, "CANCEL", "Cancel");
        // --base-game sets the Mod setting aside for the run: the page keeps
        // the stored choice under its lock and says so.
        options_.base_game = true;
        open("with --base-game");
        click(settings::page_control(settings::Page::mods), {}, "Mods' entry");
        {
            const auto locked_parts = parts_now();
            require(
                engine_settings_locks().mod == settings::Lock::command_line &&
                    shows_text(locked_parts, "The command line chose this run's mod."),
                "--base-game does not lock Mods and say so"
            );
        }
        if (!options_.snapshot.empty())
            write_ppm(step_snapshot(options_.snapshot, "menu-dialog-mod-locked"), frame());
        // A locked row answers to no control: a press on its place and the
        // keys ask nothing.
        {
            auto* locked = engine_settings_dialog();
            auto unlocked = *locked;
            unlocked.locks = {};
            const int32_t control = row_of(picked);
            for (const auto& part : settings::dialog_layout(unlocked))
                if (part.control == control) {
                    const int32_t x = part.rect.x + part.rect.width / 2;
                    const int32_t y = part.rect.y + part.rect.height / 2;
                    std::ignore = settings::dialog_pointer_down(*locked, x, y);
                    std::ignore = settings::dialog_pointer_up(*locked, x, y);
                }
            for (const auto key : {SDLK_DOWN, SDLK_SPACE, SDLK_Y})
                tap(key);
        }
        require(
            engine_settings_dialog() != nullptr &&
                engine_settings_dialog()->switch_question == settings::no_question,
            "a locked row asked the Switch Mod question under --base-game"
        );
        click(settings::cancel_control, "CANCEL", "Cancel");
        options_.base_game = false;
        // SWITCH from the picked folder played to No Mod erases both keys,
        // and the next start, which plays No Mod, no longer lists the folder.
        {
            const auto lists_picked = [&] {
                return std::find(state.mod_folders.begin(), state.mod_folders.end(), picked) !=
                       state.mod_folders.end();
            };
            const auto game_folders = options_.game_folders;
            const fs::path game_folder =
                game_folders.empty() ? options_.game_dir : game_folders.back();
            options_.game_folders = {mod_folder, game_folder};
            load_engine_settings();
            require(
                state.playing_mod_folder == picked && engine_settings().mod_folder == picked &&
                    preference_values_.contains(picked_key),
                "the picked folder stored does not play"
            );
            open("over the picked folder played");
            click(settings::page_control(settings::Page::mods), {}, "Mods' entry");
            int32_t no_mod_control = settings::no_control;
            const auto rows = settings::mod_rows(*engine_settings_dialog());
            for (std::size_t index = 0; index < rows.size(); ++index)
                if (rows[index].offered == settings::no_mod_row)
                    no_mod_control = settings::first_row_control + static_cast<int32_t>(index);
            click(no_mod_control, {}, "No Mod's row");
            click(settings::question_yes_control, "SWITCH", "the question's SWITCH");
            const auto switched = oa::platform::preferences::load(preference_path_);
            require(
                soft_restart_requested() && engine_settings_dialog() == nullptr &&
                    !switched.contains(mod_key) && !switched.contains(picked_key),
                "SWITCH away from the picked folder did not forget it"
            );
            soft_restart_requested_ = false;
            exit_requested_ = false;
            options_.game_folders = game_folders;
            load_engine_settings();
            require(!lists_picked(), "Mods lists the picked folder after a switch away from it");
            // A picked folder's key that names a folder other than the mod
            // stored is erased at start, and the folder is not listed.
            preference_values_[picked_key] = picked;
            load_engine_settings();
            require(
                !preference_values_.contains(picked_key) && !lists_picked(),
                "a start kept a picked folder that is not the mod stored"
            );
        }
        // A stored mod folder that is gone, which the start dropped: No Mod
        // shows as played, and OK replaces the stored folder.
        const std::string gone = path_to_utf8((base / "gone-mod").lexically_normal());
        preference_values_[mod_key] = gone;
        oa::platform::preferences::save(preference_path_, preference_values_);
        load_engine_settings();
        require(
            engine_settings().mod_folder.empty() && state.dropped_mod_folder == gone,
            "a gone mod folder stored did not read as No Mod"
        );
        open("over a gone mod folder");
        click(settings::page_control(settings::Page::mods), {}, "Mods' entry");
        {
            const auto rows = settings::mod_rows(*engine_settings_dialog());
            require(
                !rows.empty() && rows.front().playing &&
                    rows.front().offered == settings::no_mod_row,
                "Mods over a gone mod folder does not show No Mod as played"
            );
        }
        click(settings::ok_control, "OK", "OK");
        const auto saved_mod = oa::platform::preferences::load(preference_path_);
        require(
            !saved_mod.contains(mod_key) && state.dropped_mod_folder.empty(),
            "OK over a gone mod folder did not erase it"
        );
        // A start that plays the folder without a profile layers it over
        // the game folder with no profile, by 3.1c's own rules, and keeps
        // Developer Mode's overrides under the folder's own id.
        {
            const auto game_folders = options_.game_folders;
            const fs::path game_folder =
                game_folders.empty() ? options_.game_dir : game_folders.back();
            options_.game_folders = {empty_folder, game_folder};
            load_engine_settings();
            require(
                mod_profile() == nullptr && state.profile_id == folder_overrides_id(empty_folder) &&
                    state.profile_id != oa::data::mod_profile::base_game_id &&
                    state.playing_mod_folder == empty,
                "a mod folder without a profile does not play by 3.1c's rules under its own "
                "overrides' id"
            );
            options_.game_folders = game_folders;
            load_engine_settings();
        }
        // The settings as the check found them.
        preference_values_.erase(picked_key);
        oa::platform::preferences::save(preference_path_, preference_values_);
        state.current.picked_mod_folder.clear();
        state.last_page = settings::Page::common_tweaks;
        std::error_code ignored;
        fs::remove_all(empty_folder, ignored);
        fs::remove_all(mod_folder, ignored);
    }
    rest();
    host.latched_key = 0;
    fake_frontend_tick_ = previous_tick;
    std::cout << "engine settings check: every section, each setting through the dialog's "
                 "pointer and keys, OK, Cancel and Restore defaults, the keys they save, "
                 "Developer Mode's overrides in effect and kept, and the Touch section with a "
                 "finger's press taking the nearest control\n";
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
            if (page == settings::Page::developer && width == kWindowSizes.front().first) {
                // Developer with the first area of Developer Mode's list
                // open, off and then on, and with a rule hack of it open and
                // on, as the window shows them; then closed and off again as
                // it was.
                // Clicks the middle of a part, in the picture's pixels.
                const auto click_part = [&](const settings::LayoutPart* part,
                                            std::string_view what) {
                    require(part != nullptr, "Developer Mode shows no " + std::string(what) + on);
                    click(
                        {placement.x + part->rect.x + part->rect.width / 2,
                         placement.y + part->rect.y + part->rect.height / 2}
                    );
                };
                // Clicks the part that shows a text.
                const auto click_text = [&](std::string_view text) {
                    const auto parts = settings::dialog_layout(*engine_settings_dialog());
                    const settings::LayoutPart* found = nullptr;
                    for (const auto& part : parts)
                        if (part.text == text)
                            found = &part;
                    click_part(found, text);
                };
                const auto shows_as_picture = [&](std::string_view what) {
                    point(SDL_EVENT_MOUSE_MOTION, kRestingPointer);
                    present(presented, picture);
                    const auto differing = letterbox_differences(
                        presented, picture, area, window_width, kRestingPointer
                    );
                    require(
                        differing == 0,
                        "the window does not show Developer Mode " + std::string(what) + on + ": " +
                            std::to_string(differing) + " pixels differ"
                    );
                };
                click_text(kFirstArea);
                shows_as_picture("with an area open");
                snapshot("menu-dialog-developer-area-" + size);
                {
                    const auto area_parts = settings::dialog_layout(*engine_settings_dialog());
                    click_part(
                        find_part(area_parts, settings::developer_mode_control, "ON"),
                        "Enable Developer Mode's On"
                    );
                }
                shows_as_picture("on");
                snapshot("menu-dialog-developer-on-" + size);
                click_text(kShownHack);
                {
                    const auto hack_parts = settings::dialog_layout(*engine_settings_dialog());
                    click_part(hack_switch(hack_parts, kShownHack, "ON"), "rule hack's switch");
                }
                shows_as_picture("with a rule hack open and on");
                snapshot("menu-dialog-developer-hack-" + size);
                click_text(kShownHack);
                click_text(kFirstArea);
            }
            if (page != settings::Page::graphics)
                continue;
            // Graphics scrolls: at its end too, as the window shows the picture.
            tap(SDLK_END);
            require(
                dialog->scroll[static_cast<std::size_t>(page)] == 257,
                "End did not scroll Graphics to its end" + on
            );
            present(presented, picture);
            const auto at_end =
                letterbox_differences(presented, picture, area, window_width, kRestingPointer);
            require(
                at_end == 0,
                "the window does not show Graphics at its end" + on + ": " +
                    std::to_string(at_end) + " pixels differ"
            );
            snapshot("menu-dialog-graphics-end-" + size);
            tap(SDLK_HOME);
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
