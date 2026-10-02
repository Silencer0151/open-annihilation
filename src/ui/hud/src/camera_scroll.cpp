// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/ui/hud/camera_scroll.hpp"

#include <cstdint>

namespace oa::ui::hud {

namespace {

constexpr uint32_t kMillisecondsPerSecond = 1000;

// Equals (ms * kScrollClockHz) / 1000 for every value the product does not
// overflow, without the 32-bit wrap the product form suffers after 40 hours.
uint32_t clock_units(uint32_t milliseconds) noexcept {
    return (milliseconds / kMillisecondsPerSecond) * kScrollClockHz +
           ((milliseconds % kMillisecondsPerSecond) * kScrollClockHz) / kMillisecondsPerSecond;
}

} // namespace

uint32_t scroll_clock_advance(ScrollClock& clock, uint32_t now_ms) noexcept {
    const auto now = clock_units(now_ms);
    const auto elapsed = clock.started ? now - clock.last_units : 0u;
    clock.last_units = now;
    clock.started = true;
    return elapsed;
}

int32_t scroll_step(uint8_t scroll_speed, uint32_t frame_elapsed) noexcept {
    const auto step = static_cast<int32_t>(scroll_speed * frame_elapsed);
    return step > kMaxScrollStep ? kMaxScrollStep : step;
}

ScrollPointer
scroll_pointer_position(const ScrollPointer& pointer, int32_t width, int32_t height) noexcept {
    ScrollPointer out = pointer;
    if (!pointer.captured) {
        const auto x = pointer.desktop_x;
        const auto y = pointer.desktop_y;
        if ((width <= x || height <= y) && x < width + kEdgeCatchPixels &&
            y < height + kEdgeCatchPixels && pointer.focused) {
            out.cursor_x = width <= x ? width - 1 : x;
            out.cursor_y = height <= y ? height - 1 : y;
        }
    } else {
        if (width <= out.cursor_x)
            out.cursor_x = width - 1;
        if (height <= out.cursor_y)
            out.cursor_y = height - 1;
    }
    return out;
}

EdgeScroll edge_scroll(int32_t x, int32_t y, int32_t width, int32_t height, int32_t edge) noexcept {
    if (x < 0 || y < 0 || x >= width || y >= height)
        return {};
    const auto depth = edge < 1 ? 1 : edge;
    EdgeScroll way{};
    if (x < depth)
        way.x = -1;
    else if (x >= width - depth)
        way.x = 1;
    if (y < depth)
        way.y = -1;
    else if (y >= height - depth)
        way.y = 1;
    return way;
}

bool scroll_camera(
    Game& game, const ScrollPointer& pointer, const ScrollKeys& keys, const CameraMover& mover
) {
    const auto step = scroll_step(game.scroll_speed, game.frame_elapsed);
    if (step == 0)
        return false;
    const auto width = static_cast<int32_t>(game.offscreen_width);
    const auto height = static_cast<int32_t>(game.offscreen_height);
    const auto at = scroll_pointer_position(pointer, width, height);
    auto x = static_cast<int32_t>(game.camera_x);
    auto y = static_cast<int32_t>(game.camera_y);
    if ((keys.left && !keys.talk_open) || (at.cursor_x == 0 && at.cursor_y < height))
        x -= step;
    else if ((keys.right && !keys.talk_open) || at.cursor_x == width - 1)
        x += step;
    if ((keys.up && !keys.talk_open) || (at.cursor_y == 0 && at.cursor_x < width))
        y -= step;
    else if ((keys.down && !keys.talk_open) || at.cursor_y == height - 1)
        y += step;
    if (static_cast<int32_t>(game.camera_x) == x && static_cast<int32_t>(game.camera_y) == y)
        return false;
    if (mover.set_position != nullptr)
        mover.set_position(mover.user, x, y);
    game.follow_point_ticks = 0;
    game.follow_unit = 0;
    game.follow_target = 0;
    return true;
}

} // namespace oa::ui::hud
