// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The main menu's OA button and the settings dialog over the darkened main
// menu, as two overlays on the main menu: the button under the extensions'
// overlays and the dialog over them. Also the shortcut and the requests that
// open the settings on whichever screen can show them.

#include "engine_settings_menu_host.hpp"
#include "engine_settings_state.hpp"

#include "oa/app/runtime.hpp"
#include "oa/ui/engine_settings/dialog.hpp"
#include "oa/ui/frontend_dialogs.hpp"
#include "oa/ui/frontend_renderer/artless.hpp"

#include <SDL3/SDL.h>

#include <cmath>
#include <cstdint>
#include <string_view>
#include <tuple>

namespace oa::app {

namespace settings = oa::ui::engine_settings;
namespace artless = oa::ui::frontend_renderer;

namespace {

/// The OA button's gap to the picture's right edge, and to its bottom edge
/// (top edge while an extension's overlay stands over the main menu), in
/// source pixels.
constexpr int32_t kButtonInset = 12;
/// The button overlay's z: under the extensions' overlays, which draw over
/// the button and see input before it.
constexpr int16_t kButtonOverlayZ = -100;
/// The dialog overlay's z: over the extensions' overlays, under the
/// frontend's message boxes.
constexpr int16_t kDialogOverlayZ = 100;
/// The sound the OA button and OK play.
constexpr std::string_view kOpenSound = "Options";
/// The sound Cancel plays.
constexpr std::string_view kCancelSound = "Previous";

/// Tells whether a point lies in a rectangle.
///
/// @param rect the rectangle, in source pixels
/// @param x the point's column
/// @param y the point's row
/// @return true inside it
bool contains(const artless::SourceRect& rect, int32_t x, int32_t y) {
    return x >= rect.x && y >= rect.y && x < rect.x + rect.width && y < rect.y + rect.height;
}

/// Returns the source pixel an input's pointer is over: on the main menu
/// the pointer is in the picture's pixels.
///
/// @param input the input
/// @param[out] x the pixel's column
/// @param[out] y the pixel's row
void pointer_pixel(const ScreenInput& input, int32_t& x, int32_t& y) {
    x = static_cast<int32_t>(std::floor(input.x));
    y = static_cast<int32_t>(std::floor(input.y));
}

} // namespace

void Runtime::destroy_engine_settings_menu_host(EngineSettingsMenuHost* host) noexcept {
    delete host;
}

Runtime::EngineSettingsMenuHost& Runtime::engine_settings_menu_host() {
    if (!engine_settings_menu_)
        engine_settings_menu_.reset(new EngineSettingsMenuHost{});
    return *engine_settings_menu_;
}

bool Runtime::EngineSettingsMenuHost::button_shown(Runtime& runtime) {
    return runtime.screen_ == Screen::main_menu && !runtime.frame_owned_by_package() &&
           runtime.surface_.width >= static_cast<uint32_t>(kCanvasWidth) &&
           runtime.surface_.height >= static_cast<uint32_t>(kCanvasHeight) &&
           runtime.engine_settings_fonts() != nullptr;
}

artless::SourceRect Runtime::EngineSettingsMenuHost::button_rect(const Runtime& runtime) {
    constexpr int32_t side = settings::menu_button_side;
    const int32_t y =
        runtime.main_menu_overlay_ ? kButtonInset : kCanvasHeight - kButtonInset - side;
    return {kCanvasWidth - kButtonInset - side, y, side, side};
}

artless::Placement Runtime::EngineSettingsMenuHost::dialog_placement() {
    return {
        (kCanvasWidth - settings::dialog_width) / 2,
        (kCanvasHeight - settings::dialog_height) / 2,
        1
    };
}

int Runtime::EngineSettingsMenuHost::button_event(ScreenContext* context, void*) {
    auto& runtime = *static_cast<Runtime*>(context->host);
    auto& host = runtime.engine_settings_menu_host();
    const auto& input = *context->input;
    if (host.latched_key != 0 && input.key == host.latched_key) {
        if (input.kind == ScreenInputKind::key_up)
            host.latched_key = 0;
        if (input.kind == ScreenInputKind::key_down || input.kind == ScreenInputKind::key_up)
            return 1;
    }
    if (host.dialog_shown || !button_shown(runtime)) {
        host.button_hovered = false;
        host.button_pressed = false;
        return 0;
    }
    if (input.kind == ScreenInputKind::key_down &&
        engine_settings_shortcut(input.key, input.modifiers)) {
        runtime.open_engine_settings_from_menu();
        return 1;
    }
    int32_t x = 0;
    int32_t y = 0;
    pointer_pixel(input, x, y);
    const bool over = contains(button_rect(runtime), x, y);
    switch (input.kind) {
    case ScreenInputKind::pointer_move:
        host.button_hovered = over;
        return 0;
    case ScreenInputKind::pointer_down:
        host.button_hovered = over;
        if (input.button != SDL_BUTTON_LEFT || !over)
            return 0;
        host.button_pressed = true;
        return 1;
    case ScreenInputKind::pointer_up: {
        host.button_hovered = over;
        if (input.button != SDL_BUTTON_LEFT || !host.button_pressed)
            return 0;
        // A press opens the dialog when it is released over the button;
        // released elsewhere it does nothing.
        host.button_pressed = false;
        if (over)
            runtime.open_engine_settings_from_menu();
        return 1;
    }
    default:
        return 0;
    }
}

void Runtime::EngineSettingsMenuHost::button_draw(ScreenContext* context, void*) {
    auto& runtime = *static_cast<Runtime*>(context->host);
    if (!button_shown(runtime) || context->surface == nullptr)
        return;
    const auto& host = runtime.engine_settings_menu_host();
    auto look = settings::ButtonLook::idle;
    if (!host.dialog_shown && host.button_hovered)
        look = host.button_pressed ? settings::ButtonLook::pressed : settings::ButtonLook::hovered;
    const auto rect = button_rect(runtime);
    settings::draw_oa_button(
        *context->surface,
        {rect.x, rect.y, 1},
        rect.width,
        look,
        *runtime.engine_settings_fonts(),
        runtime.engine_settings_icon()
    );
}

int Runtime::EngineSettingsMenuHost::dialog_event(ScreenContext* context, void*) {
    auto& runtime = *static_cast<Runtime*>(context->host);
    auto& host = runtime.engine_settings_menu_host();
    if (!host.dialog_shown || runtime.screen_ != Screen::main_menu)
        return 0;
    auto* dialog = runtime.engine_settings_dialog();
    if (dialog == nullptr) {
        host.dialog_shown = false;
        return 0;
    }
    const auto& input = *context->input;
    const auto placement = dialog_placement();
    int32_t x = 0;
    int32_t y = 0;
    pointer_pixel(input, x, y);
    x -= placement.x;
    y -= placement.y;
    auto action = settings::DialogAction::none;
    uint32_t key_down = 0;
    switch (input.kind) {
    case ScreenInputKind::pointer_move:
        action = settings::dialog_pointer_move(*dialog, x, y);
        break;
    case ScreenInputKind::pointer_down:
        // A finger's press takes the nearest control within reach; the
        // dialog lies on the picture at its own scale.
        if (input.button == SDL_BUTTON_LEFT)
            action = runtime.engine_settings_state().finger_pointer
                         ? settings::dialog_finger_down(
                               *dialog, x, y, EngineSettingsState::finger_reach(runtime, 1.0)
                           )
                         : settings::dialog_pointer_down(*dialog, x, y);
        break;
    case ScreenInputKind::pointer_up:
        if (input.button == SDL_BUTTON_LEFT)
            action = settings::dialog_pointer_up(*dialog, x, y);
        break;
    case ScreenInputKind::wheel:
        action = settings::dialog_wheel(*dialog, x, y, input.wheel_y);
        break;
    case ScreenInputKind::key_down:
        if (const auto key = engine_settings_dialog_key(input.key, input.modifiers)) {
            action = settings::dialog_key(*dialog, *key);
            key_down = input.key;
        }
        break;
    default:
        break;
    }
    take_action(runtime, action, key_down);
    // The dialog is modal: nothing under it sees any input while it shows.
    return 1;
}

void Runtime::EngineSettingsMenuHost::take_action(
    Runtime& runtime, settings::DialogAction action, uint32_t key_down
) {
    if (action == settings::DialogAction::accepted || action == settings::DialogAction::switch_mod)
        runtime.play_ui_sound(kOpenSound, 0);
    else if (action == settings::DialogAction::cancelled)
        runtime.play_ui_sound(kCancelSound, 0);
    if (!runtime.take_engine_settings_action(action))
        return;
    auto& host = runtime.engine_settings_menu_host();
    host.dialog_shown = false;
    host.button_pressed = false;
    host.latched_key = key_down;
}

void Runtime::EngineSettingsMenuHost::dialog_tick(ScreenContext* context, void*) {
    auto& runtime = *static_cast<Runtime*>(context->host);
    auto& host = runtime.engine_settings_menu_host();
    if (!host.dialog_shown)
        return;
    if (runtime.screen_ == Screen::main_menu) {
        // Hardware acceleration's status follows the renderer while the
        // dialog shows, and Touch shows once a finger turns the touch
        // controls on; the main menu is drawn every frame.
        if (auto* dialog = runtime.engine_settings_dialog()) {
            std::ignore =
                settings::set_acceleration_status(*dialog, runtime.acceleration_report().status);
            std::ignore = settings::set_touch_controls(*dialog, runtime.touch_controls_active());
        }
        return;
    }
    host.dialog_shown = false;
    // Cancel always closes an open dialog.
    std::ignore = runtime.take_engine_settings_action(settings::DialogAction::cancelled);
}

void Runtime::EngineSettingsMenuHost::dialog_draw(ScreenContext* context, void*) {
    auto& runtime = *static_cast<Runtime*>(context->host);
    const auto& host = runtime.engine_settings_menu_host();
    if (!host.dialog_shown || runtime.screen_ != Screen::main_menu || context->surface == nullptr)
        return;
    const auto* dialog = runtime.engine_settings_dialog();
    const auto* fonts = runtime.engine_settings_fonts();
    if (dialog == nullptr || fonts == nullptr)
        return;
    auto& frame = *context->surface;
    artless::blend_source_rect(
        frame,
        {0, 0, 1},
        {0, 0, static_cast<int32_t>(frame.width), static_cast<int32_t>(frame.height)},
        settings::backdrop_color,
        settings::menu_backdrop_opacity
    );
    settings::draw_dialog(
        frame, dialog_placement(), *dialog, *fonts, runtime.engine_settings_icon()
    );
}

void Runtime::register_engine_settings_overlays() {
    OverlayDesc button{};
    button.name = "engine_settings_button";
    button.screen = screen_id(Screen::main_menu);
    button.z = kButtonOverlayZ;
    button.event = EngineSettingsMenuHost::button_event;
    button.draw = EngineSettingsMenuHost::button_draw;
    // A refused overlay is recorded in the registry, and register_screens
    // reports it once every screen and overlay is in.
    overlay_register(&screens_, &button);
    // On every screen, so that its tick closes the dialog when another screen
    // replaces the main menu; it takes input and draws on the main menu only.
    OverlayDesc dialog{};
    dialog.name = "engine_settings_menu_dialog";
    dialog.screen = kScreenAny;
    dialog.z = kDialogOverlayZ;
    dialog.event = EngineSettingsMenuHost::dialog_event;
    dialog.tick = EngineSettingsMenuHost::dialog_tick;
    dialog.draw = EngineSettingsMenuHost::dialog_draw;
    overlay_register(&screens_, &dialog);
}

void Runtime::open_engine_settings_from_menu() {
    // Nothing opens over a message box, over a frame a package owns, or a
    // second time.
    if (screen_ != Screen::main_menu || frame_owned_by_package() ||
        oa::ui::frontend_dialogs::dialog_count() != 0 || engine_settings_dialog() != nullptr ||
        saves_notice_shown() || engine_settings_fonts() == nullptr)
        return;
    // The dialog is drawn and fed from engine_settings_dialog().
    open_engine_settings_dialog();
    auto& host = engine_settings_menu_host();
    host.dialog_shown = true;
    host.button_hovered = false;
    host.button_pressed = false;
    play_ui_sound(kOpenSound, 0);
}

bool Runtime::engine_settings_shortcut(uint32_t key, uint16_t modifiers) noexcept {
#ifdef SDL_PLATFORM_MACOS
    constexpr uint16_t kShortcutModifier = SDL_KMOD_GUI;
#else
    constexpr uint16_t kShortcutModifier = SDL_KMOD_CTRL;
#endif
    return key == SDLK_COMMA && (modifiers & kShortcutModifier) != 0;
}

void Runtime::request_engine_settings() {
    if (screen_ == Screen::main_menu)
        open_engine_settings_from_menu();
    else if (screen_ == Screen::match)
        open_engine_settings_in_match();
}

bool Runtime::take_engine_settings_request(const SDL_Event& event) {
    // Whether the pointer event the screens are about to see is a finger's.
    EngineSettingsState::note_pointer_source(*this, event);
    if (!is_engine_settings_menu_event(event))
        return false;
    request_engine_settings();
    return true;
}

} // namespace oa::app
