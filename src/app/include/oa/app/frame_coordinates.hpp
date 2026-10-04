// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Where a screen drawn as one frame lies in the window, and how the
// pointer's window coordinates map into that frame. SDL's logical
// presentation centres the frame at half the window's spare pixels, which
// falls half a pixel off where they are odd; it draws the frame from the
// whole pixel at or before that place, while its own mapping of the pointer
// keeps the half. The functions here map through the rectangle the frame is
// drawn in, so that a click lands on the pixel it shows. Without a logical
// presentation, as in a match, they map as SDL does.
#pragma once

#include <SDL3/SDL.h>

#include <cmath>

namespace oa::app {

/// How far, in a frame's own pixels, a point SDL maps into the frame lies
/// from the place the drawn frame shows it at.
struct FrameShift {
    float x{}; ///< the frame's columns to add
    float y{}; ///< the frame's rows to add
};

/// Returns the rectangle a frame is drawn in: SDL's reckoning of it
/// (SDL_GetRenderLogicalPresentationRect) from the whole pixel at or before
/// its corner, at the size SDL reckons, which is whole.
///
/// @param reckoned SDL's rectangle, in output pixels
/// @return the drawn rectangle, in whole output pixels
[[nodiscard]] inline SDL_Rect drawn_frame_rect(const SDL_FRect& reckoned) noexcept {
    return {
        static_cast<int>(std::floor(reckoned.x)),
        static_cast<int>(std::floor(reckoned.y)),
        static_cast<int>(reckoned.w),
        static_cast<int>(reckoned.h)
    };
}

/// Returns the shift that brings a point SDL maps into a frame through its
/// own rectangle onto the drawn frame (drawn_frame_rect).
///
/// @param reckoned SDL's rectangle, in output pixels
/// @param frame_width the frame's width, in its own pixels
/// @param frame_height the frame's height, in its own pixels
/// @return the shift; none for an empty rectangle or frame
[[nodiscard]] inline FrameShift
frame_shift(const SDL_FRect& reckoned, int frame_width, int frame_height) noexcept {
    if (!(reckoned.w > 0.0F) || !(reckoned.h > 0.0F) || frame_width <= 0 || frame_height <= 0)
        return {};
    return {
        (reckoned.x - std::floor(reckoned.x)) * static_cast<float>(frame_width) / reckoned.w,
        (reckoned.y - std::floor(reckoned.y)) * static_cast<float>(frame_height) / reckoned.h
    };
}

/// Returns the shift for the frame a renderer presents now (frame_shift).
///
/// @param renderer the renderer; may be null
/// @return the shift; none without a renderer or a logical presentation
[[nodiscard]] inline FrameShift renderer_frame_shift(SDL_Renderer* renderer) noexcept {
    int width = 0;
    int height = 0;
    SDL_RendererLogicalPresentation mode = SDL_LOGICAL_PRESENTATION_DISABLED;
    SDL_FRect reckoned{};
    if (renderer == nullptr ||
        !SDL_GetRenderLogicalPresentation(renderer, &width, &height, &mode) ||
        mode == SDL_LOGICAL_PRESENTATION_DISABLED ||
        !SDL_GetRenderLogicalPresentationRect(renderer, &reckoned))
        return {};
    return frame_shift(reckoned, width, height);
}

/// Converts an event's window coordinates into the renderer's, as
/// SDL_ConvertEventToRenderCoordinates does, and then onto the frame as it
/// is drawn: each position SDL converted is shifted by
/// renderer_frame_shift. Movements, which no rectangle moves, keep SDL's.
///
/// @param renderer the renderer
/// @param[in,out] event the event
/// @return false when SDL refused to convert it, as its own call says
[[nodiscard]] inline bool
convert_event_to_frame(SDL_Renderer* renderer, SDL_Event& event) noexcept {
    if (!SDL_ConvertEventToRenderCoordinates(renderer, &event))
        return false;
    const FrameShift shift = renderer_frame_shift(renderer);
    if (shift.x == 0.0F && shift.y == 0.0F)
        return true;
    // SDL converts an event of the renderer's window, and a finger's
    // wherever the renderer has a window.
    SDL_Window* const window = SDL_GetRenderWindow(renderer);
    const auto ours = [window](SDL_WindowID id) { return SDL_GetWindowFromID(id) == window; };
    const auto move = [&shift](float& x, float& y) {
        x += shift.x;
        y += shift.y;
    };
    switch (event.type) {
    case SDL_EVENT_MOUSE_MOTION:
        if (ours(event.motion.windowID))
            move(event.motion.x, event.motion.y);
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (ours(event.button.windowID))
            move(event.button.x, event.button.y);
        break;
    case SDL_EVENT_MOUSE_WHEEL:
        if (ours(event.wheel.windowID))
            move(event.wheel.mouse_x, event.wheel.mouse_y);
        break;
    case SDL_EVENT_FINGER_DOWN:
    case SDL_EVENT_FINGER_UP:
    case SDL_EVENT_FINGER_CANCELED:
    case SDL_EVENT_FINGER_MOTION:
        if (window != nullptr)
            move(event.tfinger.x, event.tfinger.y);
        break;
    case SDL_EVENT_PEN_MOTION:
        if (ours(event.pmotion.windowID))
            move(event.pmotion.x, event.pmotion.y);
        break;
    case SDL_EVENT_PEN_DOWN:
    case SDL_EVENT_PEN_UP:
        if (ours(event.ptouch.windowID))
            move(event.ptouch.x, event.ptouch.y);
        break;
    case SDL_EVENT_PEN_BUTTON_DOWN:
    case SDL_EVENT_PEN_BUTTON_UP:
        if (ours(event.pbutton.windowID))
            move(event.pbutton.x, event.pbutton.y);
        break;
    case SDL_EVENT_PEN_AXIS:
        if (ours(event.paxis.windowID))
            move(event.paxis.x, event.paxis.y);
        break;
    case SDL_EVENT_DROP_POSITION:
    case SDL_EVENT_DROP_FILE:
    case SDL_EVENT_DROP_TEXT:
    case SDL_EVENT_DROP_COMPLETE:
        if (ours(event.drop.windowID))
            move(event.drop.x, event.drop.y);
        break;
    default:
        break;
    }
    return true;
}

/// Returns where a point of the renderer's coordinates lies in the window,
/// as SDL_RenderCoordinatesToWindow does, through the frame as it is drawn:
/// the inverse of convert_event_to_frame.
///
/// @param renderer the renderer
/// @param x the point's column, in the renderer's coordinates
/// @param y the point's row, in the renderer's coordinates
/// @param[out] window_x the column in the window, in window coordinates
/// @param[out] window_y the row in the window, in window coordinates
/// @return false when SDL refused, as its own call says
[[nodiscard]] inline bool frame_to_window(
    SDL_Renderer* renderer, float x, float y, float* window_x, float* window_y
) noexcept {
    const FrameShift shift = renderer_frame_shift(renderer);
    return SDL_RenderCoordinatesToWindow(renderer, x - shift.x, y - shift.y, window_x, window_y);
}

} // namespace oa::app
