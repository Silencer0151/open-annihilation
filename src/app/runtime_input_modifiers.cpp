// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The modifier keys as each use reads them: the keyboard's, a synthetic
// key's pulse, the touch latch that gives Shift to that use and the pad's
// FORCE that gives Ctrl to orders and selection; the pointer key word kept
// in step with a latch; keys pressed for the touch controls; the Cmd
// alternates of a hardware keyboard; the keypad's Enter as Return; haptics, on the platform's device and
// the gamepad in use (docs/touch-controls.md, docs/controllers.md). This is
// the one place the engine reads SDL_GetModState().
#include "oa/app/runtime.hpp"
#include "touch_state.hpp"
#include "oa/app/platform_hooks.hpp"
#include "oa/sim/gameplay_input/input.hpp"
#include "oa/ui/pad_controls.hpp"
#include "oa/ui/touch_hud.hpp"
#include <SDL3/SDL.h>
#include <cstdint>
#include <tuple>

namespace oa::app {
namespace {

/// One Cmd alternate of a hardware keyboard: the key typed with Cmd and the key it stands for.
struct CommandAlternate {
    SDL_Keycode typed{};     ///< the key typed with Cmd
    SDL_Keycode key{};       ///< the key it stands for
    SDL_Scancode scancode{}; ///< that key's scancode
};

/// The Cmd alternates of a keyboard without Escape, F-keys or Pause (a tablet's).
constexpr CommandAlternate kCommandAlternates[] = {
    {SDLK_PERIOD, SDLK_ESCAPE, SDL_SCANCODE_ESCAPE},
    {SDLK_1, SDLK_F1, SDL_SCANCODE_F1},
    {SDLK_2, SDLK_F2, SDL_SCANCODE_F2},
    {SDLK_3, SDLK_F3, SDL_SCANCODE_F3},
    {SDLK_4, SDLK_F4, SDL_SCANCODE_F4},
    {SDLK_P, SDLK_PAUSE, SDL_SCANCODE_PAUSE},
};

/// Returns the gamepad's feel for a haptic moment.
///
/// @param kind the moment
/// @return the feel the pad plays for it
oa::ui::pad_controls::Feel pad_feel_of(Haptic kind) noexcept {
    namespace pc = oa::ui::pad_controls;
    switch (kind) {
    case Haptic::hold_started:
        return pc::Feel::hold_started;
    case Haptic::box_started:
        return pc::Feel::box_started;
    case Haptic::site_refused:
        return pc::Feel::site_refused;
    case Haptic::queue_reduced:
        return pc::Feel::queue_reduced;
    }
    return pc::Feel::hold_started;
}

} // namespace

SDL_Keymod Runtime::input_modifiers(ModifierUse use) const {
    auto modifiers = static_cast<uint32_t>(SDL_GetModState());
    if (const auto* state = touch_state_if_made(); state != nullptr)
        modifiers |= state->dispatch.pulse;
    if (virtual_shift(use))
        modifiers |= SDL_KMOD_LSHIFT;
    // FORCE is the pad's Ctrl for clicks, never for the keys the pad presses.
    if ((use == ModifierUse::order || use == ModifierUse::selection) && pad_force_held())
        modifiers |= SDL_KMOD_LCTRL;
    return static_cast<SDL_Keymod>(modifiers);
}

bool Runtime::virtual_shift(ModifierUse use) const {
    const auto* state = touch_state_if_made();
    if (state == nullptr)
        return false;
    namespace hud = oa::ui::touch_hud;
    const auto& latches = state->hud.latches;
    switch (use) {
    case ModifierUse::keyboard:
        return false;
    case ModifierUse::selection:
        return latches.active(hud::Latch::add);
    case ModifierUse::order:
        return latches.active(hud::Latch::queue);
    case ModifierUse::build_button:
        return latches.active(hud::Latch::times_five);
    }
    return false;
}

void Runtime::refresh_pointer_modifiers() {
    if (screen_ != Screen::match || !match_ || match_paused_ || match_finished_)
        return;
    namespace input = oa::sim::gameplay_input;
    auto& game = match_->state().game;
    const auto modifiers = input_modifiers(ModifierUse::order);
    uint32_t keys =
        game.pointer_state[2] & ~(input::pointer_key_shift | input::pointer_key_control);
    if ((modifiers & SDL_KMOD_SHIFT) != 0)
        keys |= input::pointer_key_shift;
    if ((modifiers & SDL_KMOD_CTRL) != 0)
        keys |= input::pointer_key_control;
    game.pointer_state[2] = keys;
}

void Runtime::press_match_key(SDL_Keycode key, SDL_Keymod mods) {
    SDL_KeyboardEvent event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.timestamp = SDL_GetTicksNS();
    event.key = key;
    event.scancode = SDL_GetScancodeFromKey(key, nullptr);
    event.mod = mods;
    event.down = true;
    event.repeat = false;
    // The pulse holds for the call alone, whatever the key does.
    auto& dispatch = touch_state().dispatch;
    const SDL_Keymod before = dispatch.pulse;
    dispatch.pulse = mods;
    try {
        std::ignore = handle_match_hotkey(event);
    } catch (...) {
        dispatch.pulse = before;
        throw;
    }
    dispatch.pulse = before;
}

void Runtime::remap_command_key(SDL_Event& event) const {
    if (event.type != SDL_EVENT_KEY_DOWN && event.type != SDL_EVENT_KEY_UP)
        return;
    if ((event.key.mod & SDL_KMOD_GUI) == 0)
        return;
    // On a desktop without touch controls Cmd keeps its meaning (macOS's
    // Cmd+digit shows a build page).
    if (!touch_controls_active())
        return;
    for (const auto& alternate : kCommandAlternates) {
        if (event.key.key != alternate.typed)
            continue;
        event.key.key = alternate.key;
        event.key.scancode = alternate.scancode;
        event.key.mod = static_cast<SDL_Keymod>(event.key.mod & ~SDL_KMOD_GUI);
        return;
    }
}

void Runtime::remap_keypad_enter(SDL_Event& event) noexcept {
    if ((event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) &&
        event.key.key == SDLK_KP_ENTER)
        event.key.key = SDLK_RETURN;
}

void Runtime::play_haptic(oa::app::Haptic kind) const {
    // The gamepad in use plays it too, by its own Haptics setting.
    if (pad_used())
        play_pad_feel(pad_feel_of(kind));
    // The Touch setting as the dispatcher last read it; haptics are on
    // by default.
    if (const auto* state = touch_state_if_made();
        state != nullptr && !state->dispatch.settings.haptics)
        return;
    const auto& hooks = oa::app::platform_hooks();
    if (hooks.haptic != nullptr)
        hooks.haptic(hooks.context, kind);
}

} // namespace oa::app
