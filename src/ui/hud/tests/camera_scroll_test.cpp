// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "check.hpp"
#include "fixtures.hpp"

#include "oa/ui/hud/camera_scroll.hpp"

#include <algorithm>
#include <cstdint>
#include <utility>

using namespace oa;
using namespace oa::ui::hud;

namespace {

struct Mover {
    int32_t x = -1, y = -1;
    int calls = 0;

    CameraMover mover() {
        return {this, [](void* u, int32_t x, int32_t y) {
                    auto* self = static_cast<Mover*>(u);
                    self->x = x;
                    self->y = y;
                    ++self->calls;
                }};
    }
};

ScrollPointer at(int32_t x, int32_t y) {
    return {x, y, true, 0, 0, true};
}

void test_step_and_edges() {
    hud_test::TestWorld match;
    Game& game = match.game();
    game.offscreen_width = 640;
    game.offscreen_height = 480;
    game.camera_x = 1000;
    game.camera_y = 500;
    game.scroll_speed = 10;
    game.frame_elapsed = 3;
    game.follow_unit = 7;
    game.follow_point_ticks = 9;
    Mover mover;

    // Pointer in the middle, no keys: nothing moves.
    CHECK(!scroll_camera(game, at(300, 200), {}, mover.mover()));
    CHECK(mover.calls == 0 && game.follow_unit == 7);

    // Left edge scrolls left by speed * elapsed; the follow is dropped.
    CHECK(scroll_camera(game, at(0, 200), {}, mover.mover()));
    CHECK(mover.x == 970 && mover.y == 500);
    CHECK(game.follow_unit == 0 && game.follow_point_ticks == 0);

    // Bottom-right corner scrolls right and down; step caps at 128.
    game.frame_elapsed = 100;
    CHECK(scroll_camera(game, at(639, 479), {}, mover.mover()));
    CHECK(mover.x == 1000 + kMaxScrollStep && mover.y == 500 + kMaxScrollStep);

    // The left edge does not count below the screen, the top edge does not
    // count right of it.
    CHECK(scroll_pointer_position(at(0, 700), 640, 480).cursor_y == 479);
    game.frame_elapsed = 1;
    CHECK(scroll_camera(game, at(0, 479), {}, mover.mover()));
    CHECK(mover.x == 990 && mover.y == 510);

    // Arrow keys scroll unless the chat line owns them.
    CHECK(scroll_camera(game, at(300, 200), {false, true, true, false, false}, mover.mover()));
    CHECK(mover.x == 1010 && mover.y == 490);
    CHECK(!scroll_camera(game, at(300, 200), {false, true, true, false, true}, mover.mover()));

    // No speed, no scroll.
    game.scroll_speed = 0;
    CHECK(!scroll_camera(game, at(0, 0), {}, mover.mover()));
}

// Pixels the left edge scrolls over one second of real time drawn as frames
// of `frame_ms` (the last frame is cut to end exactly on the second).
int32_t scrolled_in_one_second(uint32_t frame_ms, uint8_t speed) {
    hud_test::TestWorld match;
    Game& game = match.game();
    game.offscreen_width = 640;
    game.offscreen_height = 480;
    game.camera_x = 10000;
    game.camera_y = 500;
    game.scroll_speed = speed;
    const CameraMover mover{&game, [](void* u, int32_t x, int32_t y) {
                                auto* g = static_cast<Game*>(u);
                                g->camera_x = static_cast<uint32_t>(x);
                                g->camera_y = static_cast<uint32_t>(y);
                            }};
    ScrollClock clock{};
    constexpr uint32_t start_ms = 5000;
    (void)scroll_clock_advance(clock, start_ms);
    for (uint32_t now = start_ms; now < start_ms + 1000;) {
        now = std::min(now + frame_ms, start_ms + 1000);
        game.frame_elapsed = scroll_clock_advance(clock, now);
        (void)scroll_camera(game, at(0, 200), {}, mover);
    }
    return 10000 - static_cast<int32_t>(game.camera_x);
}

void test_clock_pacing() {
    ScrollClock clock{};
    CHECK(scroll_clock_advance(clock, 123456) == 0);
    CHECK(scroll_clock_advance(clock, 123456 + 100) == 3);
    CHECK(scroll_clock_advance(clock, 123456 + 100) == 0);
    CHECK(scroll_clock_advance(clock, 123456 + 1100) == 30);
    // Whole units only: a 16 ms frame yields one unit or none.
    CHECK(scroll_clock_advance(clock, 123456 + 1116) == 1);
    CHECK(scroll_clock_advance(clock, 123456 + 1132) == 0);
    CHECK(scroll_clock_advance(clock, 123456 + 1148) == 1);
    // The product form of the conversion would wrap here.
    ScrollClock late{};
    (void)scroll_clock_advance(late, 150'000'000);
    CHECK(scroll_clock_advance(late, 150'001'000) == kScrollClockHz);

    CHECK(scroll_step(10, 3) == 30);
    CHECK(scroll_step(65, 10) == kMaxScrollStep);
    CHECK(scroll_step(0, 10) == 0);
    CHECK(scroll_step(10, 0) == 0);

    // One second at speed 10 covers 300 pixels whether it is drawn as 1000,
    // 143, 63, 31 or 4 frames.
    for (const uint32_t frame_ms : {1u, 7u, 16u, 33u, 250u})
        CHECK(scrolled_in_one_second(frame_ms, 10) == 300);
    // Only the per-frame cap makes a frame rate matter: at 4 frames a second
    // the maximum speed exceeds it.
    CHECK(scrolled_in_one_second(16, 65) == 65 * 30);
    CHECK(scrolled_in_one_second(250, 65) == 4 * kMaxScrollStep);
}

void test_desktop_pointer() {
    // A desktop cursor just past the right edge of a focused window counts
    // as the edge; too far away, or unfocused, it does not.
    ScrollPointer pointer{320, 240, false, 700, 100, true};
    auto at_edge = scroll_pointer_position(pointer, 640, 480);
    CHECK(at_edge.cursor_x == 639 && at_edge.cursor_y == 100);
    pointer.desktop_x = 740;
    CHECK(scroll_pointer_position(pointer, 640, 480).cursor_x == 320);
    pointer.desktop_x = 700;
    pointer.focused = false;
    CHECK(scroll_pointer_position(pointer, 640, 480).cursor_x == 320);
    // Inside the window the game cursor stands.
    pointer = {10, 20, false, 5, 5, true};
    CHECK(scroll_pointer_position(pointer, 640, 480).cursor_x == 10);
}

bool scrolls(const EdgeScroll& way, int32_t x, int32_t y) {
    return way.x == x && way.y == y;
}

void test_edge_scroll() {
    // Each edge of a 640x480 screen scrolls toward itself, the sidebar's and
    // the bars' as much as the battlefield's, and each corner both ways.
    CHECK(scrolls(edge_scroll(0, 240, 640, 480, 1), -1, 0));
    CHECK(scrolls(edge_scroll(639, 240, 640, 480, 1), 1, 0));
    CHECK(scrolls(edge_scroll(320, 0, 640, 480, 1), 0, -1));
    CHECK(scrolls(edge_scroll(320, 479, 640, 480, 1), 0, 1));
    CHECK(scrolls(edge_scroll(0, 0, 640, 480, 1), -1, -1));
    CHECK(scrolls(edge_scroll(639, 0, 640, 480, 1), 1, -1));
    CHECK(scrolls(edge_scroll(0, 479, 640, 480, 1), -1, 1));
    CHECK(scrolls(edge_scroll(639, 479, 640, 480, 1), 1, 1));

    // One pixel inside the edges, and the middle, do not scroll.
    CHECK(scrolls(edge_scroll(1, 240, 640, 480, 1), 0, 0));
    CHECK(scrolls(edge_scroll(638, 240, 640, 480, 1), 0, 0));
    CHECK(scrolls(edge_scroll(320, 1, 640, 480, 1), 0, 0));
    CHECK(scrolls(edge_scroll(320, 478, 640, 480, 1), 0, 0));
    CHECK(scrolls(edge_scroll(1, 1, 640, 480, 1), 0, 0));
    CHECK(scrolls(edge_scroll(320, 240, 640, 480, 1), 0, 0));

    // Edges two pixels deep, as a point of two pixels makes them.
    CHECK(scrolls(edge_scroll(1, 959, 2880, 1800, 2), -1, 0));
    CHECK(scrolls(edge_scroll(2878, 1798, 2880, 1800, 2), 1, 1));
    CHECK(scrolls(edge_scroll(2, 1797, 2880, 1800, 2), 0, 0));
    CHECK(scrolls(edge_scroll(2877, 2, 2880, 1800, 2), 0, 0));

    // An edge less than a pixel deep is one pixel deep.
    CHECK(scrolls(edge_scroll(0, 240, 640, 480, 0), -1, 0));
    CHECK(scrolls(edge_scroll(1, 240, 640, 480, 0), 0, 0));

    // A pointer off the screen does not scroll.
    CHECK(scrolls(edge_scroll(-1, 240, 640, 480, 1), 0, 0));
    CHECK(scrolls(edge_scroll(640, 240, 640, 480, 1), 0, 0));
    CHECK(scrolls(edge_scroll(320, -1, 640, 480, 1), 0, 0));
    CHECK(scrolls(edge_scroll(320, 480, 640, 480, 1), 0, 0));

    // The edges agree with scroll_camera's on the outermost pixel.
    hud_test::TestWorld match;
    Game& game = match.game();
    game.offscreen_width = 640;
    game.offscreen_height = 480;
    game.camera_x = 1000;
    game.camera_y = 500;
    game.scroll_speed = 10;
    game.frame_elapsed = 1;
    for (const auto& [x, y] :
         {std::pair{0, 0},
          std::pair{639, 0},
          std::pair{0, 479},
          std::pair{639, 479},
          std::pair{0, 240},
          std::pair{639, 240},
          std::pair{320, 0},
          std::pair{320, 479},
          std::pair{1, 1},
          std::pair{320, 240}}) {
        Mover mover;
        const auto moved = scroll_camera(game, at(x, y), {}, mover.mover());
        const auto way = edge_scroll(x, y, 640, 480, 1);
        CHECK(moved == (way.x != 0 || way.y != 0));
        if (moved)
            CHECK(mover.x == 1000 + way.x * 10 && mover.y == 500 + way.y * 10);
    }
}

} // namespace

int main() {
    test_step_and_edges();
    test_clock_pacing();
    test_desktop_pointer();
    test_edge_scroll();
    return 0;
}
