// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// --check-engine-settings: the OA button and the settings dialog on the main
// menu and in a match, through the SDL presenter, and each setting taking
// effect.

#include "engine_settings_menu_host.hpp"
#include "engine_settings_state.hpp"

#include "oa/app/runtime.hpp"
#include "oa/platform/preferences.hpp"
#include "oa/ui/engine_settings/dialog.hpp"
#include "oa/ui/frontend_dialogs.hpp"
#include "oa/ui/frontend_renderer/artless.hpp"

#include <SDL3/SDL.h>

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace oa::app {

namespace settings = oa::ui::engine_settings;
namespace artless = oa::ui::frontend_renderer;

namespace {

/// The modifier the settings' shortcut takes on this platform.
#ifdef SDL_PLATFORM_MACOS
constexpr SDL_Keymod kShortcutModifier = SDL_KMOD_GUI;
#else
constexpr SDL_Keymod kShortcutModifier = SDL_KMOD_CTRL;
#endif

/// A point of the main menu's picture over none of its buttons, where the
/// pointer rests between the check's steps.
constexpr oa::ui::display_layout::Point kRestingPointer{4, 240};

/// Stops the check with a reason unless a condition holds.
///
/// @param condition what must hold
/// @param what what went wrong otherwise
void require(bool condition, std::string_view what) {
    if (!condition)
        throw std::runtime_error("engine settings check: " + std::string(what));
}

/// Tells whether a pixel lies in a rectangle.
///
/// @param rect the rectangle
/// @param x the pixel's column
/// @param y the pixel's row
/// @return true inside it
bool inside(const artless::SourceRect& rect, uint32_t x, uint32_t y) {
    const auto column = static_cast<int32_t>(x);
    const auto row = static_cast<int32_t>(y);
    return column >= rect.x && row >= rect.y && column < rect.x + rect.width &&
           row < rect.y + rect.height;
}

/// Counts the pixels in which two frames of one size differ, leaving out a
/// rectangle.
///
/// @param first one frame
/// @param second the other frame
/// @param left_out the rectangle not compared; empty for none
/// @return the differing pixels; every pixel when the sizes differ
std::size_t differing_pixels(
    const renderer::Surface& first,
    const renderer::Surface& second,
    const artless::SourceRect& left_out = {}
) {
    if (first.width != second.width || first.height != second.height ||
        first.rgb.size() != second.rgb.size())
        return static_cast<std::size_t>(first.width) * first.height + 1U;
    std::size_t differing = 0;
    for (uint32_t y = 0; y < first.height; ++y)
        for (uint32_t x = 0; x < first.width; ++x) {
            if (inside(left_out, x, y))
                continue;
            const auto at = (static_cast<std::size_t>(y) * first.width + x) * 3U;
            if (first.rgb[at] != second.rgb[at] || first.rgb[at + 1] != second.rgb[at + 1] ||
                first.rgb[at + 2] != second.rgb[at + 2])
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
    return snapshot.parent_path() / (snapshot.stem().string() + '-' + std::string(step) + ".ppm");
}

} // namespace

void Runtime::check_engine_settings() {
    if (sdl_.renderer == nullptr || sdl_.window == nullptr)
        throw std::runtime_error("engine settings check: needs the SDL presenter");
    // The check saves settings, so it never runs over the player's own file.
    if (!options_.preferences_file)
        throw std::runtime_error("engine settings check: needs --preferences-file");
    // The preferences file lasts from one run of the check to the next: each
    // run starts from an empty one, with the preferences it gives in effect.
    preference_values_.clear();
    oa::platform::preferences::save(preference_path_, preference_values_);
    init::load_preferences(state_, skirmish_settings_, preferences_, *this);
    load_engine_settings();
    check_unit_limit_preference();
    if (main_menu_overlay_)
        check_engine_settings_under_overlay();
    // An extension's overlay may draw over any part of the main menu and take
    // its input first, so the extensions' overlays are set aside while the
    // check holds the main menu to the engine's own drawing, and put back
    // when it ends.
    set_extension_overlays_aside(true);

    struct PutBack {
        Runtime& runtime;

        ~PutBack() { runtime.set_extension_overlays_aside(false); }
    } put_back{*this};

    check_engine_settings_in_menu();
    check_engine_settings_dialog();
    check_engine_settings_window_sizes();
    check_engine_settings_in_match();
    check_unsaved_unit_limit_ends_with_its_match();
    check_engine_settings_wiring();
    std::cout << "engine settings check: passed\n";
}

void Runtime::check_unit_limit_preference() {
    const auto saved_limits = limits_.units_per_player;
    const auto saved_setting = frontend_game().max_units_setting;
    const auto saved_current = engine_settings_state().current.unit_limit;
    const auto saved_values = preference_values_;

    const auto read_back = [this] {
        const auto values = oa::platform::preferences::load(preference_path_);
        return settings::read_settings(values, EngineSettingsState::inputs(*this), false)
            .unit_limit;
    };
    const auto stored_text = [this] {
        const auto values = oa::platform::preferences::load(preference_path_);
        const auto found = values.find(std::string(settings::key::unit_limit));
        require(found != values.end(), "the unit limit preference was not stored");
        return found->second;
    };

    set_unit_limit(*this, 400, SettingScope::immediate);
    require(read_back() == 400, "a unit limit of 400 was not read back");
    require(stored_text() == "400", "the unit limit preference does not hold 400");
    require(frontend_game().max_units_setting == 400, "the next game did not take the unit limit");
    require(engine_settings().unit_limit == 400, "the unit limit setting did not take 400");

    // A game mode's limit reaches the next game and leaves the player's
    // setting and the preferences file alone.
    set_unit_limit(*this, 250, SettingScope::next_game);
    require(
        frontend_game().max_units_setting == 250, "a game mode's unit limit missed the next game"
    );
    require(
        read_back() == 400 && stored_text() == "400",
        "a game mode's unit limit was written to the preferences"
    );
    require(engine_settings().unit_limit == 400, "a game mode's unit limit changed the setting");
    set_unit_limit(*this, 5, SettingScope::next_game);
    require(
        frontend_game().max_units_setting == settings::lowest_stored_unit_limit &&
            stored_text() == "400",
        "a game mode's low unit limit was not clamped, or was written"
    );

    // A limit for the next start is written, and Settings shows it; the
    // games of this session keep the run's limit.
    set_unit_limit(*this, 400, SettingScope::immediate);
    set_unit_limit(*this, 600, SettingScope::next_restart);
    require(
        read_back() == 600 && stored_text() == "600", "a next-start unit limit was not written"
    );
    require(engine_settings().unit_limit == 600, "Settings does not show the next start's limit");
    require(
        frontend_game().max_units_setting == 400,
        "a next-start unit limit changed the running session's games"
    );

    set_unit_limit(*this, 477, SettingScope::immediate);
    require(
        read_back() == 477 && stored_text() == "477", "a unit limit between the stops was not kept"
    );

    set_unit_limit(*this, 20, SettingScope::immediate);
    require(
        read_back() == settings::lowest_stored_unit_limit, "the lowest unit limit was not kept"
    );
    set_unit_limit(*this, 19, SettingScope::immediate);
    require(
        read_back() == settings::lowest_stored_unit_limit && stored_text() == "20",
        "a low unit limit was not clamped"
    );
    set_unit_limit(*this, -40, SettingScope::immediate);
    require(
        read_back() == settings::lowest_stored_unit_limit, "a negative unit limit was not clamped"
    );
    set_unit_limit(*this, 1500, SettingScope::immediate);
    require(read_back() == settings::highest_unit_limit, "the highest unit limit was not kept");
    set_unit_limit(*this, 1501, SettingScope::immediate);
    require(
        read_back() == settings::highest_unit_limit && stored_text() == "1500",
        "a high unit limit was not clamped"
    );
    set_unit_limit(*this, 100000, SettingScope::immediate);
    require(
        read_back() == settings::highest_unit_limit, "a unit limit past the highest was not clamped"
    );

    limits_.units_per_player.minimum = 100;
    limits_.units_per_player.maximum = 3000;
    set_unit_limit(*this, 50, SettingScope::immediate);
    require(read_back() == 100, "a unit limit under a game's lowest was not clamped");
    set_unit_limit(*this, 2000, SettingScope::immediate);
    require(
        read_back() == 2000 && stored_text() == "2000", "a unit limit a game allows was not kept"
    );
    set_unit_limit(*this, 9000, SettingScope::immediate);
    require(read_back() == 3000, "a unit limit over a game's highest was not clamped");

    limits_.units_per_player = saved_limits;
    preference_values_ = saved_values;
    preferences_dirty_ = true;
    frontend_game().max_units_setting = saved_setting;
    engine_settings_state().current.unit_limit = saved_current;
    require(!EngineSettingsState::flush(*this), "the preferences were not put back");
    const auto restored = oa::platform::preferences::load(preference_path_);
    require(
        restored.find(std::string(settings::key::unit_limit)) == restored.end(),
        "the unit limit preference was left in the file"
    );
    std::cout << "engine settings check: an extension sets the unit limit preference\n";
}

void Runtime::check_unsaved_unit_limit_ends_with_its_match() {
    const uint16_t setting = engine_settings_state().current.unit_limit;
    const uint16_t run_before = frontend_game().max_units_setting;
    const uint16_t game_mode_limit = setting == 250 ? 300 : 250;
    const uint16_t next_limit = setting == 350 ? 400 : 350;
    if (match_)
        leave_match();
    load(Screen::main_menu);
    set_unit_limit(*this, game_mode_limit, SettingScope::next_game);
    require(
        frontend_game().max_units_setting == game_mode_limit,
        "a game mode's unit limit missed the next game"
    );
    start_benchmark_skirmish();
    require(
        match_->state().game.max_units_setting == game_mode_limit,
        "the match did not play at the game mode's unit limit"
    );
    // A game mode that sets another next-game limit during the match keeps it
    // for the next new game. A restart, which keeps its match's limit, is not
    // that game.
    set_unit_limit(*this, next_limit, SettingScope::next_game);
    const uint16_t restart_limit = EngineSettingsState::restart_unit_limit(*this);
    leave_match();
    require(
        frontend_game().max_units_setting == next_limit,
        "the unit limit set during a match for the next one was dropped"
    );
    EngineSettingsState::start_skirmish(*this, restart_limit, false);
    require(
        match_ && match_->state().game.max_units_setting == game_mode_limit,
        "a restart did not keep its match's unit limit"
    );
    leave_match();
    require(
        frontend_game().max_units_setting == next_limit,
        "a restart took the limit set for the next new game"
    );
    load(Screen::main_menu);
    start_benchmark_skirmish();
    require(
        match_->state().game.max_units_setting == next_limit,
        "the next new game did not play at the limit set for it"
    );
    leave_match();
    require(
        frontend_game().max_units_setting == setting,
        "a game mode's next-game unit limit outlived its match"
    );
    require(
        engine_settings().unit_limit == setting, "the game mode's unit limit changed the setting"
    );
    load(Screen::main_menu);
    frontend_game().max_units_setting = run_before;
    std::cout << "engine settings check: a game mode's unit limit ends with its match\n";
}

void Runtime::check_engine_settings_under_overlay() {
    std::cout << "engine settings check: the main menu under an extension's overlay\n";
    const auto previous_tick = fake_frontend_tick_;
    fake_frontend_tick_ = 1000U;
    load(Screen::main_menu);
    require(screen_ == Screen::main_menu, "the main menu did not open");
    require(
        EngineSettingsMenuHost::button_shown(*this),
        "the OA button does not show under an extension's overlay"
    );
    const auto top = EngineSettingsMenuHost::button_rect(*this);
    require(
        top.x == 596 && top.y == 12 && top.width == settings::menu_button_side,
        "under an extension's overlay the OA button is not at 596,12"
    );
    // A click on the button reaches it through the extensions' overlays, and
    // Escape closes the dialog it opens, which stands over them.
    const oa::ui::display_layout::Point centre{top.x + top.width / 2, top.y + top.height / 2};
    send_check_pointer(SDL_EVENT_MOUSE_MOTION, centre, 0);
    send_check_pointer(SDL_EVENT_MOUSE_BUTTON_DOWN, centre, SDL_BUTTON_LEFT);
    send_check_pointer(SDL_EVENT_MOUSE_BUTTON_UP, centre, SDL_BUTTON_LEFT);
    require(
        engine_settings_dialog() != nullptr && screen_ == Screen::main_menu,
        "under an extension's overlay a click on the OA button did not open the dialog"
    );
    for (const auto type : {SDL_EVENT_KEY_DOWN, SDL_EVENT_KEY_UP}) {
        SDL_Event event{};
        event.type = type;
        event.key.windowID = SDL_GetWindowID(sdl_.window);
        event.key.key = SDLK_ESCAPE;
        event.key.scancode = SDL_SCANCODE_ESCAPE;
        event.key.down = type == SDL_EVENT_KEY_DOWN;
        bool running = true;
        dispatch_event(event, running);
        require(running, "Escape in the dialog ended the run");
    }
    require(engine_settings_dialog() == nullptr, "Escape did not close the dialog");
    send_check_pointer(SDL_EVENT_MOUSE_MOTION, kRestingPointer, 0);
    fake_frontend_tick_ = previous_tick;
    std::cout << "engine settings check: under an extension's overlay the OA button at " << top.x
              << ',' << top.y << " opens the dialog\n";
}

void Runtime::check_engine_settings_in_menu() {
    std::cout << "engine settings check: the main menu\n";
    auto& host = engine_settings_menu_host();
    const auto previous_tick = fake_frontend_tick_;
    fake_frontend_tick_ = 1000U;
    load(Screen::main_menu);
    require(screen_ == Screen::main_menu, "the main menu did not open");
    const auto* fonts = engine_settings_fonts();
    require(fonts != nullptr, "the dialog's fonts did not load");
    const auto icon = engine_settings_icon();
    require(artless::picture_drawable(icon), "the OA button and the dialog have no icon to show");
    require(!main_menu_overlay_, "an extension's overlay stands over the main menu");

    const auto point = [this](SDL_EventType type, oa::ui::display_layout::Point at) {
        send_check_pointer(type, at, type == SDL_EVENT_MOUSE_MOTION ? 0 : SDL_BUTTON_LEFT);
    };
    const auto click = [&](oa::ui::display_layout::Point at) {
        point(SDL_EVENT_MOUSE_MOTION, at);
        point(SDL_EVENT_MOUSE_BUTTON_DOWN, at);
        point(SDL_EVENT_MOUSE_BUTTON_UP, at);
    };
    // Sends a key's press or release; false when it ended the run.
    const auto key =
        [this](SDL_EventType type, SDL_Keycode code, SDL_Keymod modifiers, bool repeat) {
            SDL_Event event{};
            event.type = type;
            event.key.windowID = SDL_GetWindowID(sdl_.window);
            event.key.key = code;
            event.key.scancode = SDL_GetScancodeFromKey(code, nullptr);
            event.key.mod = modifiers;
            event.key.down = type == SDL_EVENT_KEY_DOWN;
            event.key.repeat = repeat;
            bool running = true;
            dispatch_event(event, running);
            return running;
        };
    const auto press = [&](SDL_Keycode code, SDL_Keymod modifiers = SDL_KMOD_NONE) {
        return key(SDL_EVENT_KEY_DOWN, code, modifiers, false);
    };
    const auto release = [&](SDL_Keycode code, SDL_Keymod modifiers = SDL_KMOD_NONE) {
        return key(SDL_EVENT_KEY_UP, code, modifiers, false);
    };
    const auto shortcut = [&] {
        require(press(SDLK_COMMA, kShortcutModifier), "the shortcut ended the run");
        require(release(SDLK_COMMA, kShortcutModifier), "the shortcut ended the run");
    };
    const auto centre = [](const artless::SourceRect& rect) {
        return oa::ui::display_layout::Point{rect.x + rect.width / 2, rect.y + rect.height / 2};
    };
    const auto snapshot = [this](std::string_view step, const renderer::Surface& frame) {
        if (!options_.snapshot.empty())
            write_ppm(step_snapshot(options_.snapshot, step), frame);
    };

    // The OA button in the picture's bottom-right corner, and in its
    // top-right corner while an extension's overlay stands over the main
    // menu. Each frame is drawn from the same sparks, so that frames differ
    // only where the button differs; the frame with the button at the top
    // shows the bare menu at the bottom.
    point(SDL_EVENT_MOUSE_MOTION, kRestingPointer);
    const auto bottom = EngineSettingsMenuHost::button_rect(*this);
    require(
        bottom.x == 596 && bottom.y == 436 && bottom.width == settings::menu_button_side &&
            bottom.height == settings::menu_button_side,
        "the OA button is not the 32-pixel square at 596,436"
    );
    main_menu_overlay_ = true;
    const auto top = EngineSettingsMenuHost::button_rect(*this);
    main_menu_overlay_ = false;
    require(
        top.x == 596 && top.y == 12 && top.width == settings::menu_button_side,
        "under an extension's overlay the OA button is not at 596,12"
    );
    const auto sparks = menu_sparks_;
    const auto frame = [&] {
        menu_sparks_ = sparks;
        return frame_without_cursor();
    };
    main_menu_overlay_ = true;
    const auto button_at_top = frame();
    main_menu_overlay_ = false;
    const auto expect_button = [&](settings::ButtonLook look, std::string_view what) {
        auto expected = button_at_top;
        settings::draw_oa_button(
            expected, {bottom.x, bottom.y, 1}, bottom.width, look, *fonts, icon
        );
        require(differing_pixels(frame(), expected, top) == 0, what);
    };
    expect_button(settings::ButtonLook::idle, "the OA button is not drawn at rest at 596,436");
    const auto idle = frame();
    snapshot("engine-settings-menu", idle);
    {
        auto expected = idle;
        settings::draw_oa_button(
            expected, {top.x, top.y, 1}, top.width, settings::ButtonLook::idle, *fonts, icon
        );
        require(
            differing_pixels(button_at_top, expected, bottom) == 0,
            "under an extension's overlay the OA button is not drawn at 596,12"
        );
    }

    // Hovered, held, and a press let go off the button.
    point(SDL_EVENT_MOUSE_MOTION, centre(bottom));
    require(host.button_hovered && !host.button_pressed, "the pointer over the OA button");
    expect_button(settings::ButtonLook::hovered, "the OA button does not look hovered");
    point(SDL_EVENT_MOUSE_BUTTON_DOWN, centre(bottom));
    require(host.button_pressed, "a press on the OA button is not held");
    expect_button(settings::ButtonLook::pressed, "the OA button does not look pressed");
    point(SDL_EVENT_MOUSE_MOTION, kRestingPointer);
    expect_button(settings::ButtonLook::idle, "the OA button held off the pointer looks pressed");
    point(SDL_EVENT_MOUSE_BUTTON_UP, kRestingPointer);
    require(
        engine_settings_dialog() == nullptr && !host.button_pressed,
        "a press let go off the OA button opened the dialog"
    );

    // A click opens the dialog over the darkened main menu.
    click(centre(bottom));
    require(
        engine_settings_dialog() != nullptr && host.dialog_shown && screen_ == Screen::main_menu,
        "a click on the OA button did not open the dialog"
    );
    require(release(SDLK_ESCAPE) && press(SDLK_ESCAPE), "Escape in the dialog ended the run");
    require(engine_settings_dialog() == nullptr, "Escape did not close the dialog");
    require(release(SDLK_ESCAPE), "Escape's release ended the run");

    // The darkened menu and the dialog, drawn from the same sparks as the
    // menu without it.
    point(SDL_EVENT_MOUSE_MOTION, kRestingPointer);
    const auto closed = frame();
    shortcut();
    auto* dialog = engine_settings_dialog();
    require(dialog != nullptr && host.dialog_shown, "the shortcut did not open the dialog");
    const auto placement = EngineSettingsMenuHost::dialog_placement();
    require(
        placement.x == 80 && placement.y == 78 && placement.scale == 1,
        "the dialog is not centred on the main menu at 80,78"
    );
    {
        auto expected = closed;
        artless::blend_source_rect(
            expected,
            {0, 0, 1},
            {0, 0, static_cast<int32_t>(expected.width), static_cast<int32_t>(expected.height)},
            settings::backdrop_color,
            settings::menu_backdrop_opacity
        );
        settings::draw_dialog(expected, placement, *dialog, *fonts, icon);
        const auto shown = frame();
        snapshot("engine-settings-menu-dialog", shown);
        require(
            differing_pixels(shown, expected) == 0,
            "the dialog is not drawn over the darkened main menu"
        );
    }

    // The dialog is modal: neither the main menu's buttons nor the OA button
    // nor a second request reach under it.
    const auto* exit = widget(menu::resource_name(menu::Button::exit));
    require(exit != nullptr, "the main menu has no Exit button");
    click({exit->common.x + exit->common.width / 2, exit->common.y + exit->common.height / 2});
    click(centre(bottom));
    shortcut();
    request_engine_settings();
    require(
        engine_settings_dialog() == dialog && oa::ui::frontend_dialogs::dialog_count() == 0 &&
            screen_ == Screen::main_menu,
        "input reached the main menu under the dialog"
    );

    // Cancel (Escape) puts back the settings the dialog opened with, and its
    // held key never reaches the main menu.
    const auto opened = dialog->opened;
    dialog->chosen.wheel_zoom = !opened.wheel_zoom;
    (void)take_engine_settings_action(settings::DialogAction::changed);
    require(
        engine_settings().wheel_zoom == dialog->chosen.wheel_zoom,
        "a change in the dialog did not take effect at once"
    );
    require(press(SDLK_ESCAPE), "Escape in the dialog ended the run");
    require(
        engine_settings_dialog() == nullptr && !host.dialog_shown && engine_settings() == opened,
        "Escape did not close the dialog with the settings it opened with"
    );
    require(
        key(SDL_EVENT_KEY_DOWN, SDLK_ESCAPE, SDL_KMOD_NONE, true) && press(SDLK_ESCAPE),
        "Escape held after the dialog closed ended the run"
    );
    require(release(SDLK_ESCAPE) && host.latched_key == 0, "Escape's release did not count");
    // On the main menu itself Escape does nothing.
    require(
        press(SDLK_ESCAPE) && release(SDLK_ESCAPE) && !exit_requested_ &&
            screen_ == Screen::main_menu && engine_settings_dialog() == nullptr,
        "Escape on the main menu ended the run or left the menu"
    );

    // OK (Enter) keeps the settings chosen; Enter held does nothing more.
    shortcut();
    dialog = engine_settings_dialog();
    require(dialog != nullptr, "the shortcut did not open the dialog again");
    auto chosen = dialog->opened;
    chosen.wheel_zoom = !chosen.wheel_zoom;
    dialog->chosen = chosen;
    (void)take_engine_settings_action(settings::DialogAction::changed);
    require(press(SDLK_RETURN), "Enter in the dialog ended the run");
    require(
        engine_settings_dialog() == nullptr && engine_settings() == chosen,
        "Enter did not close the dialog with the settings chosen"
    );
    require(
        host.latched_key == static_cast<uint32_t>(SDLK_RETURN) &&
            key(SDL_EVENT_KEY_DOWN, SDLK_RETURN, SDL_KMOD_NONE, true) &&
            engine_settings_dialog() == nullptr,
        "Enter held after the dialog closed reached the main menu"
    );
    require(release(SDLK_RETURN) && host.latched_key == 0, "Enter's release did not count");
    // The settings go back as they were, saved.
    shortcut();
    dialog = engine_settings_dialog();
    require(dialog != nullptr, "the shortcut did not open the dialog a third time");
    dialog->chosen = opened;
    (void)take_engine_settings_action(settings::DialogAction::changed);
    require(press(SDLK_RETURN) && release(SDLK_RETURN), "Enter in the dialog ended the run");
    require(engine_settings() == opened, "the settings did not go back as they were");

    // Under an extension's overlay, the button in the top-right corner opens it.
    main_menu_overlay_ = true;
    click(centre(top));
    const bool opened_at_top = engine_settings_dialog() != nullptr;
    require(press(SDLK_ESCAPE) && release(SDLK_ESCAPE), "Escape in the dialog ended the run");
    main_menu_overlay_ = false;
    require(opened_at_top, "under an extension's overlay the OA button did not open the dialog");
    require(engine_settings_dialog() == nullptr, "Escape did not close the dialog");

    // Nothing opens over a message box or over a frame a package owns.
    show_frontend_message("Settings check", 300, 1, 1);
    require(oa::ui::frontend_dialogs::dialog_count() != 0, "no message box opened");
    shortcut();
    open_engine_settings_from_menu();
    require(engine_settings_dialog() == nullptr, "the dialog opened over a message box");
    oa::ui::frontend_dialogs::close_dialog();
    const auto state = state_.state;
    state_.state = frontend::state_id::pump_only;
    const bool shown_when_owned = EngineSettingsMenuHost::button_shown(*this);
    open_engine_settings_from_menu();
    const bool opened_when_owned = engine_settings_dialog() != nullptr;
    state_.state = state;
    require(
        !shown_when_owned && !opened_when_owned,
        "the OA button or the dialog showed over a frame a package owns"
    );

    // Another screen replacing the main menu closes the dialog as Cancel does.
    shortcut();
    dialog = engine_settings_dialog();
    require(dialog != nullptr, "the shortcut did not open the dialog over the main menu");
    dialog->chosen.wheel_zoom = !opened.wheel_zoom;
    (void)take_engine_settings_action(settings::DialogAction::changed);
    load(Screen::single_player);
    tick_screen_packages();
    require(
        engine_settings_dialog() == nullptr && !host.dialog_shown && engine_settings() == opened,
        "the dialog stayed open off the main menu"
    );
    load(Screen::main_menu);

    // The application menu's request opens it on the main menu.
    request_engine_settings();
    require(engine_settings_dialog() != nullptr, "the request did not open the dialog");
    require(press(SDLK_ESCAPE) && release(SDLK_ESCAPE), "Escape in the dialog ended the run");
    require(engine_settings_dialog() == nullptr, "Escape did not close the dialog");

    // EXIT ends the program. The check then goes on from the menu as it was.
    const auto menu_state = state_;
    exercise_click(menu::resource_name(menu::Button::exit));
    const bool exited = exit_requested_;
    exit_requested_ = false;
    state_ = menu_state;
    require(exited, "EXIT did not end the run");

    fake_frontend_tick_ = previous_tick;
    std::cout << "engine settings check: the OA button at " << bottom.x << ',' << bottom.y << " ("
              << top.x << ',' << top.y << " under an overlay), the dialog at " << placement.x << ','
              << placement.y << " over the darkened main menu\n";
}

} // namespace oa::app
