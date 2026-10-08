// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Text input with the field's place given to the system, so that an
// on-screen keyboard (Steam's in Game Mode, the system's on a desktop)
// opens clear of it, and an input method's candidates stand beside it.
#include "oa/app/runtime.hpp"
#include "text_input_area.hpp"

#include "oa/present/typed_text.hpp"
#include <SDL3/SDL.h>

namespace oa::app {

namespace {

/// Hands a field's place to the window. context is the SDL_Window.
///
/// @param context the window
/// @param area the area in the window's coordinates; null clears it
/// @return false when the system rejects the area
bool tell_text_input_area(void* context, const TextInputArea* area) {
    auto* window = static_cast<SDL_Window*>(context);
    if (area == nullptr)
        return SDL_SetTextInputArea(window, nullptr, 0);
    const SDL_Rect rect{area->x, area->y, area->width, area->height};
    return SDL_SetTextInputArea(window, &rect, area->cursor);
}

} // namespace

void set_focused_text_field(Runtime& runtime, const TextField* field) {
    if (runtime.sdl_.window == nullptr)
        return;
    TextInputAreaTarget target;
    target.context = runtime.sdl_.window;
    target.set_area = tell_text_input_area;
    static_cast<void>(set_text_input_area_with(field, runtime.sdl_.renderer, target));
}

void Runtime::start_text_input(std::optional<oa::ui::display_layout::Rect> field) {
    // A field opening starts with no composition of the one before.
    text_composition_.clear();
    if (sdl_.window == nullptr)
        return;
    // The field's place first, so that a keyboard opening with the input
    // already knows where not to stand. The game keeps no caret inside a
    // typed line, so the caret is the field's start. Without a field, the
    // system places the keyboard as it would.
    if (field && field->width > 0 && field->height > 0) {
        const TextField placed{field->x, field->y, field->width, field->height, 0};
        set_focused_text_field(*this, &placed);
    } else {
        set_focused_text_field(*this, nullptr);
    }
    SDL_StartTextInput(sdl_.window);
}

void Runtime::stop_text_input() {
    text_composition_.clear();
    if (sdl_.window != nullptr)
        SDL_StopTextInput(sdl_.window);
}

bool Runtime::take_composition_event(const SDL_Event& event) {
    if (event.type == SDL_EVENT_TEXT_EDITING)
        text_composition_ = oa::present::typed_characters(
            event.edit.text != nullptr ? event.edit.text : "", oa::present::TypedCharacters::text
        );
    else if (event.type == SDL_EVENT_TEXT_INPUT)
        text_composition_.clear();
    return event.type == SDL_EVENT_KEY_DOWN && !text_composition_.empty() &&
           event.key.key != SDLK_ESCAPE;
}

} // namespace oa::app
