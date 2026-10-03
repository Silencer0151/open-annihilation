// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Smooth panning, for --check-render-tiers: the view the accelerated tier
// draws between map pixels, read back as a slow scroll moves it at zoom 2.5
// and 0.5, against the same scroll in the standard tier, and as a second
// axis joins the scroll; the pointer picking the unit drawn under it, and
// finding the game view up to the battlefield's edge, when the view lies
// between map pixels; and the point drawn under a zoom's anchor held there.
#include "oa/app/runtime.hpp"

#include "engine_settings_state.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace oa::app {

namespace {

/// Frames each scroll is read back over.
constexpr int scroll_frames = 24;
/// Frames at a scroll's start left out of its steadiness: scrolling toward
/// the map's start, the view waits there for the camera's first step.
constexpr int scroll_start_frames = 2;

/// The zoom the card magnifies the slow scroll at, and the screen pixels
/// each frame scrolls there: 0.23 of a map pixel, so the camera steps a
/// whole map pixel, 2.5 screen pixels, every fourth or fifth frame, and the
/// view never lands back on a whole map pixel within the run.
constexpr float magnified_scroll_zoom = 2.5F;
constexpr double magnified_scroll_step = 0.575;

/// Frames the slow scroll at zoom 2.5 moves along one axis before the other
/// joins it, as holding a second arrow key does, and frames both move after.
/// Two frames in, the carry both axes step on leaves the joining axis three
/// frames to reach its camera's first step, a whole map pixel on: under a
/// screen pixel each. Joining later leaves fewer frames for that map pixel.
constexpr int frames_before_joining = 2;
constexpr int joined_frames = 16;

/// The zoom the area pass reduces the slow scroll at, and the screen pixels
/// each frame scrolls there: half a map pixel, so the camera steps every
/// second frame by a whole map pixel, half a screen pixel.
constexpr float reduced_scroll_zoom = 0.5F;
constexpr double reduced_scroll_step = 0.25;

/// The most a frame of the accelerated tier may move from the one before,
/// in screen pixels: one pixel, which SDL's software renderer, placing the
/// card's draws at whole pixels, moves by.
constexpr double most_frame_motion = 1.0;
/// The most a zoomed-out frame's measured motion may differ from another's:
/// the camera alone moves by none or half a pixel by turns, and the area
/// pass's frames, each the scene averaged a quarter of a pixel further on,
/// are measured within an eighth of a pixel of one another.
constexpr double most_reduced_spread = 0.25;
/// The least the standard tier's largest frame-to-frame jump at zoom 2.5
/// is: whole map pixels, two screen pixels or more.
constexpr double least_standard_jump = 2.0;
/// How near, in map pixels, a frame's drawn view must be to where the
/// scroll's exact travel puts it: what double arithmetic leaves.
constexpr double view_tolerance = 1.0e-9;
/// The most the motion over a run may differ from the scroll's, in screen
/// pixels; scrolling toward the map's start the view waits for the
/// camera's first step, a map pixel more.
constexpr double most_run_stray = 1.0;

/// The zoom the pick is checked at, and how far past the camera's map pixel
/// the view is moved for it: three screen pixels.
constexpr float pick_zoom = 4.0F;
constexpr double pick_offset = 0.75;
/// Screen pixels either side of the unit's drawn place the pointer looks for it in.
constexpr int pick_reach = 96;
/// The most the view at the pick may be drawn from where it lies, in screen
/// pixels: SDL's software renderer places the card's draw at a whole pixel.
constexpr double most_drawn_stray = 0.5;
/// How far past the camera's map pixel the view is moved for a zoom about
/// an anchor, on each axis the map leaves room on.
constexpr std::array<double, 2> anchor_offsets{0.3, 0.9};
/// How many screen pixels past the start of a map pixel the wheel's anchor
/// is put, at zoom 4: a quarter of a map pixel, so that the camera, rounding
/// about the anchor, stays where it is, as it always has there.
constexpr int wheel_anchor_past = 1;
/// The wheel's turn toward a nearer zoom, which at zoom 4, the nearest,
/// leaves the zoom where it is.
constexpr float wheel_nearer = 1.0F;
/// Points of the game's screen the mapping back to the canvas is tried at,
/// along a line this many columns and rows apart.
constexpr int mapped_points = 40;
constexpr int mapped_columns_apart = 3;
constexpr int mapped_rows_apart = 2;

/// Whole screen pixels a frame-to-frame shift is searched over either way,
/// and the fractions of a pixel a shift is found to.
constexpr int frame_shift_reach = 5;
constexpr int shift_steps_per_pixel = 16;
/// Rows of the battlefield between those a shift is measured on.
constexpr int shift_row_step = 3;
/// The cursor's region, either side of the pointer, left out of the comparisons.
constexpr int cursor_reach = 64;
/// How much of a pixel's brightness each channel gives, as the eye weighs them.
constexpr double red_brightness = 0.299;
constexpr double green_brightness = 0.587;
constexpr double blue_brightness = 0.114;

/// A rectangle of a frame, in its pixels.
struct Region {
    int x{};
    int y{};
    int w{};
    int h{};
};

/// A frame's brightness, the channels weighted as the eye weighs them.
struct Brightness {
    int width{};
    int height{};
    std::vector<double> levels; ///< from 0 to 255, row after row

    /// Returns one pixel's brightness.
    ///
    /// @param x column, within the frame
    /// @param y row, within the frame
    /// @return from 0 to 255
    [[nodiscard]] double at(int x, int y) const {
        return levels
            [static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
             static_cast<std::size_t>(x)];
    }
};

/// Returns a frame's brightness.
///
/// @param frame the frame
/// @return its brightness
Brightness brightness_of(const renderer::Surface& frame) {
    Brightness brightness{
        static_cast<int>(frame.width),
        static_cast<int>(frame.height),
        std::vector<double>(std::size_t{frame.width} * frame.height)
    };
    for (std::size_t pixel = 0; pixel < brightness.levels.size(); ++pixel)
        brightness.levels[pixel] = red_brightness * frame.rgb[pixel * 3U] +
                                   green_brightness * frame.rgb[pixel * 3U + 1U] +
                                   blue_brightness * frame.rgb[pixel * 3U + 2U];
    return brightness;
}

/// Returns how far apart two frames match at a shift: the mean difference
/// of brightness between the later frame and the earlier, read that many
/// pixels to the right and between its pixels.
///
/// @param before the earlier frame
/// @param after the later frame, the same size
/// @param region where they are compared, far enough inside both for the shift
/// @param left_out a region not compared, such as the cursor's
/// @param shift screen pixels
/// @return the mean difference
double mismatch(
    const Brightness& before,
    const Brightness& after,
    const Region& region,
    const Region& left_out,
    double shift
) {
    const int whole = static_cast<int>(std::floor(shift));
    const double part = shift - whole;
    double error = 0.0;
    std::size_t count = 0;
    for (int y = region.y; y < region.y + region.h; y += shift_row_step)
        for (int x = region.x; x < region.x + region.w; ++x) {
            if (x >= left_out.x && x < left_out.x + left_out.w && y >= left_out.y &&
                y < left_out.y + left_out.h)
                continue;
            const double earlier =
                (1.0 - part) * before.at(x + whole, y) + part * before.at(x + whole + 1, y);
            error += std::abs(after.at(x, y) - earlier);
            ++count;
        }
    return error / static_cast<double>(std::max<std::size_t>(count, 1));
}

/// Returns how far a frame's picture moved left from an earlier frame's, in
/// screen pixels: the whole shift at which the earlier frame best matches
/// the later, then the sixteenth of a pixel within a pixel of it.
///
/// @param before the earlier frame
/// @param after the later frame, the same size
/// @param region where they are compared, at least `reach` + 2 pixels inside both
/// @param left_out a region not compared, such as the cursor's
/// @param reach whole pixels searched either way
/// @return the shift, positive when the picture moved left
double horizontal_shift(
    const Brightness& before,
    const Brightness& after,
    const Region& region,
    const Region& left_out,
    int reach
) {
    double best = 0.0;
    double best_error = std::numeric_limits<double>::max();
    for (int whole = -reach; whole <= reach; ++whole) {
        const double error = mismatch(before, after, region, left_out, whole);
        if (error < best_error) {
            best_error = error;
            best = whole;
        }
    }
    const double centre = best;
    for (int step = -shift_steps_per_pixel; step <= shift_steps_per_pixel; ++step) {
        const double shift = centre + static_cast<double>(step) / shift_steps_per_pixel;
        const double error = mismatch(before, after, region, left_out, shift);
        if (error < best_error) {
            best_error = error;
            best = shift;
        }
    }
    return best;
}

/// What a scroll's frames did.
struct ScrollRun {
    bool ran{};                  ///< the map left room for the scroll
    int32_t way{};               ///< 1 toward the map's end, -1 toward its start
    std::vector<double> motions; ///< screen pixels each frame moved from the one before
    double largest{};            ///< the largest motion's size
    double spread{};             ///< the motions' range, the scroll's start left out
    double total{};              ///< screen pixels the last frame moved from the first
    int32_t camera_steps{};      ///< whole map pixels the camera moved
    double travel{};             ///< screen pixels the scroll moved the view in all
    /// The most the drawn view's motion in a frame differed from the
    /// scroll's, in map pixels.
    double view_stray{};
    /// A frame drew the view back against the scroll, or further than it.
    bool view_turned{};
};

/// Returns a run's motions as text, for the log.
///
/// @param run the run
/// @return each motion to two places
std::string motions_text(const ScrollRun& run) {
    std::ostringstream text;
    text << std::fixed << std::setprecision(2);
    for (std::size_t index = 0; index < run.motions.size(); ++index)
        text << (index != 0 ? " " : "") << run.motions[index];
    return text.str();
}

} // namespace

void Runtime::check_smooth_panning(
    const std::function<void(bool)>& switch_tier,
    const std::function<void(float)>& at_zoom,
    uint16_t unit
) {
    const auto fail = [](const std::string& what) {
        throw std::runtime_error("render tiers check: smooth panning: " + what);
    };
    if (!match_ || !selected_tnt_)
        fail("needs a match");
    const auto presented = [&]() {
        renderer::Surface frame;
        capture_frame_ = &frame;
        render();
        capture_frame_ = nullptr;
        return brightness_of(frame);
    };
    const auto rest_pointer = [&]() {
        // The cursor waits in the blank corner right of the bottom bar.
        update_pointer(
            static_cast<float>(match_layout_.width - 1),
            static_cast<float>(match_layout_.height - 1)
        );
    };
    rest_pointer();
    const Region cursor{
        static_cast<int>(match_pointer_x_) - cursor_reach,
        static_cast<int>(match_pointer_y_) - cursor_reach,
        2 * cursor_reach,
        2 * cursor_reach
    };
    // The battlefield, less a margin the shifts searched and the card's
    // draw before the edge stay inside.
    const auto measured = [&](float zoom, int reach) {
        const int margin = static_cast<int>(std::ceil(2.0 * zoom)) + reach + 4;
        return Region{
            match_layout_.left + margin,
            match_layout_.top + margin,
            match_layout_.battlefield_width() - 2 * margin,
            match_layout_.battlefield_height() - 2 * margin
        };
    };
    const auto map_width = shown_map_size()[0];

    // A scroll from the unit, toward whichever side of the map has room for
    // it, one frame's step after another, each frame read back.
    const auto scroll = [&](float zoom, double step) {
        at_zoom(zoom);
        scroll_zoom_carry_ = 0.0;
        ScrollRun run;
        const double screen_travel = step * (scroll_frames + 1);
        const auto map_travel =
            static_cast<int32_t>(std::ceil(screen_travel / static_cast<double>(zoom))) + 2;
        const int32_t camera_first = view_camera()[0];
        if (camera_first + visible_map_width() + map_travel <= map_width)
            run.way = 1;
        else if (camera_first - map_travel >= 0)
            run.way = -1;
        else
            return run;
        run.ran = true;
        // A frame at the zoom first, which settles how the view is drawn.
        std::ignore = presented();
        // The scroll's first frame moves the view off the camera's map
        // pixel, where the card draws one more column at the same scale:
        // SDL's software renderer, which places draws at whole pixels,
        // rounds that wider picture's width its own way, so the frames are
        // compared from the first already moved.
        scroll_match_view(run.way, 0, step);
        const auto first = presented();
        auto before = first;
        // Where each frame drew the view, in map pixels.
        const auto drawn_view = [&]() {
            return static_cast<double>(view_camera()[0]) + accelerated_.frame_offset.x;
        };
        double view = drawn_view();
        const double travel = step / static_cast<double>(zoom);
        for (int frame = 0; frame < scroll_frames; ++frame) {
            scroll_match_view(run.way, 0, step);
            auto after = presented();
            const double view_moved = run.way * (drawn_view() - view);
            view = drawn_view();
            run.view_stray = std::max(run.view_stray, std::abs(view_moved - travel));
            run.view_turned = run.view_turned || view_moved < -view_tolerance ||
                              view_moved > travel + view_tolerance;
            // The picture moves against the view: left for a view moving right.
            const double motion =
                run.way *
                horizontal_shift(
                    before, after, measured(zoom, frame_shift_reach), cursor, frame_shift_reach
                );
            run.motions.push_back(motion);
            run.largest = std::max(run.largest, std::abs(motion));
            before = std::move(after);
        }
        const auto reach = static_cast<int>(std::ceil(screen_travel)) + frame_shift_reach;
        run.total = run.way * horizontal_shift(first, before, measured(zoom, reach), cursor, reach);
        const auto [low, high] =
            std::minmax_element(run.motions.begin() + scroll_start_frames, run.motions.end());
        run.spread = *high - *low;
        run.camera_steps = run.way * (view_camera()[0] - camera_first);
        run.travel = step * (scroll_frames + 1);
        return run;
    };
    const auto report = [](const char* what, float zoom, double step, const ScrollRun& run) {
        std::cout << "render tiers check: a scroll of " << step << " screen pixels a frame at zoom "
                  << zoom << ", " << what << ": largest " << run.largest << ", spread "
                  << run.spread << ", over the run " << run.total << " of " << step * scroll_frames
                  << ", the camera " << run.camera_steps << " map pixels, the drawn view within "
                  << run.view_stray << " map pixels of the scroll's place; " << motions_text(run)
                  << '\n';
    };
    // Each frame draws the view the scroll's exact travel on, toward the
    // map's end; toward its start, never back and never further.
    const auto view_follows = [](const ScrollRun& run) {
        return !run.view_turned && (run.way < 0 || run.view_stray <= view_tolerance);
    };
    // The camera steps the whole map pixels the scroll's exact travel holds.
    const auto camera_keeps_rate = [](const ScrollRun& run, float zoom) {
        return run.camera_steps ==
               static_cast<int32_t>(std::floor(run.travel / static_cast<double>(zoom)));
    };
    // The view moves the scroll's exact travel over the frames compared.
    const auto view_keeps_rate = [](const ScrollRun& run, float zoom, double step) {
        const double stray = most_run_stray + (run.way < 0 ? static_cast<double>(zoom) : 0.0);
        return std::abs(run.total - step * scroll_frames) <= stray;
    };

    // At zoom 2.5 the card magnifies the scene: the accelerated tier's view
    // moves at most a pixel a frame at the scroll's rate, while the camera
    // steps whole map pixels as it always has; the standard tier's jumps them.
    {
        switch_tier(true);
        const auto run = scroll(magnified_scroll_zoom, magnified_scroll_step);
        if (!run.ran) {
            std::cout << "render tiers check: the map leaves no room to scroll at zoom 2.5\n";
        } else {
            if (accelerated_.frame.method != SceneMethod::magnify || !accelerated_.magnified)
                fail("zoom 2.5 was not magnified by the card");
            report("accelerated", magnified_scroll_zoom, magnified_scroll_step, run);
            if (run.largest > most_frame_motion)
                fail("a frame at zoom 2.5 moved " + std::to_string(run.largest) + " screen pixels");
            if (!view_follows(run))
                fail("the view drawn at zoom 2.5 did not follow the scroll's exact place");
            if (!view_keeps_rate(run, magnified_scroll_zoom, magnified_scroll_step))
                fail("the view at zoom 2.5 did not keep the scroll's rate");
            if (!camera_keeps_rate(run, magnified_scroll_zoom))
                fail("the camera at zoom 2.5 did not step whole map pixels at the scroll's rate");
            switch_tier(false);
            const auto standard = scroll(magnified_scroll_zoom, magnified_scroll_step);
            report("standard", magnified_scroll_zoom, magnified_scroll_step, standard);
            if (standard.largest < least_standard_jump)
                fail("the standard tier's scroll at zoom 2.5 did not step whole map pixels");
            if (!camera_keeps_rate(standard, magnified_scroll_zoom))
                fail("the standard tier's camera at zoom 2.5 did not keep the scroll's rate");
            switch_tier(true);
        }
    }
    // At zoom 2.5, a second axis joining the slow scroll, down the map: both
    // axes step on one carry, so the joining axis's view catches up with it
    // before that axis's camera first steps, and from then on is at the
    // scroll's exact place. Each frame it moves forward by at most a pixel,
    // where the camera alone would jump it at that step.
    {
        at_zoom(magnified_scroll_zoom);
        scroll_zoom_carry_ = 0.0;
        const auto map_height = static_cast<int32_t>(selected_tnt_->tile_height * 32U);
        const double travel = magnified_scroll_step / static_cast<double>(magnified_scroll_zoom);
        const auto room =
            static_cast<int32_t>(std::ceil(travel * (frames_before_joining + joined_frames))) + 2;
        const auto start = view_camera();
        int32_t way = 0;
        if (start[0] + visible_map_width() + room <= map_width)
            way = 1;
        else if (start[0] - room >= 0)
            way = -1;
        if (way == 0 || start[1] + visible_map_height() + room > map_height) {
            std::cout
                << "render tiers check: the map leaves no room to join a scroll at zoom 2.5\n";
        } else {
            std::ignore = presented();
            for (int frame = 0; frame < frames_before_joining; ++frame) {
                scroll_match_view(way, 0, magnified_scroll_step);
                std::ignore = presented();
            }
            // Where each frame drew the view's row, in map pixels.
            const auto drawn_row = [&]() {
                return static_cast<double>(view_camera()[1]) + accelerated_.frame_offset.y;
            };
            const auto joined_at = view_camera();
            double row = drawn_row();
            ScrollRun run;
            bool stepped = false;
            for (int frame = 0; frame < joined_frames; ++frame) {
                scroll_match_view(way, 1, magnified_scroll_step);
                std::ignore = presented();
                const double moved = drawn_row() - row;
                row = drawn_row();
                const double motion = moved * static_cast<double>(magnified_scroll_zoom);
                run.motions.push_back(motion);
                run.largest = std::max(run.largest, std::abs(motion));
                run.view_turned = run.view_turned || moved < -view_tolerance;
                if (stepped)
                    run.view_stray = std::max(run.view_stray, std::abs(moved - travel));
                stepped = stepped || view_camera()[1] != joined_at[1];
            }
            std::cout << "render tiers check: a second axis joining the scroll at zoom 2.5 after "
                      << frames_before_joining << " frames: largest " << run.largest
                      << ", then within " << run.view_stray << " map pixels of the scroll's place; "
                      << motions_text(run) << '\n';
            if (run.largest > most_frame_motion || run.view_turned)
                fail(
                    "a second axis joining the scroll at zoom 2.5 moved " +
                    std::to_string(run.largest) + " screen pixels in a frame"
                );
            if (!stepped || run.view_stray > view_tolerance)
                fail("the joining axis did not follow the scroll's place from its camera's step");
            // The camera steps as it always has: both axes on the one carry.
            const auto end = view_camera();
            if (end[1] - joined_at[1] != way * (end[0] - joined_at[0]))
                fail("the cameras of a scroll on both axes did not step together");
        }
    }
    // At zoom 0.5 the area pass reduces the scene: every frame moves by the
    // scroll's own step, a quarter of a pixel, where the camera alone moves
    // by none or half a pixel by turns.
    {
        const auto run = scroll(reduced_scroll_zoom, reduced_scroll_step);
        if (!run.ran) {
            std::cout << "render tiers check: the map leaves no room to scroll at zoom 0.5\n";
        } else {
            if (accelerated_.frame.method != SceneMethod::area)
                fail("zoom 0.5 was not reduced by the area pass");
            report("accelerated", reduced_scroll_zoom, reduced_scroll_step, run);
            if (run.largest > most_frame_motion)
                fail("a frame at zoom 0.5 moved " + std::to_string(run.largest) + " screen pixels");
            if (!view_follows(run))
                fail("the view drawn at zoom 0.5 did not follow the scroll's exact place");
            if (run.spread > most_reduced_spread)
                fail("the frames at zoom 0.5 did not move evenly");
            if (!view_keeps_rate(run, reduced_scroll_zoom, reduced_scroll_step))
                fail("the view at zoom 0.5 did not keep the scroll's rate");
            if (!camera_keeps_rate(run, reduced_scroll_zoom))
                fail("the camera at zoom 0.5 did not step whole map pixels at the scroll's rate");
            switch_tier(false);
            const auto standard = scroll(reduced_scroll_zoom, reduced_scroll_step);
            report("standard", reduced_scroll_zoom, reduced_scroll_step, standard);
            switch_tier(true);
        }
    }

    // At zoom 4, a view three quarters of a map pixel past the camera's is
    // drawn three screen pixels further left, and the pointer finds the
    // unit three pixels further left with it.
    {
        at_zoom(pick_zoom);
        scroll_zoom_carry_ = 0.0;
        // The unit the camera is centred on, unless it has left the match
        // in the ticks the checks before ran, as a campaign mission's may:
        // then the first of the local player's still in it, centred on.
        uint16_t picked = unit;
        if (match_->world().slots[unit].record.type_index == 0) {
            picked = 0;
            for (const auto& slot : match_->world().slots)
                if (slot.unit_index != 0 && slot.unit != nullptr && slot.record.type_index != 0 &&
                    slot.owner_index == match_local_player_) {
                    picked = slot.unit_index;
                    break;
                }
            if (picked == 0)
                fail("the match has no local unit left to pick");
            center_camera_on_unit(picked);
        }
        const auto before = presented();
        if (accelerated_.frame.method != SceneMethod::magnify)
            fail("zoom 4 was not magnified by the card");
        const auto* placed = match_->world().slots[picked].unit;
        if (placed == nullptr)
            fail("the unit to pick is gone");
        const auto camera = view_camera();
        const auto drawn = project_match_point(
            live_viewport(static_cast<uint32_t>(camera[0]), static_cast<uint32_t>(camera[1])),
            placed->position
        );
        const auto first_hit = [&]() {
            for (int x = drawn.x - pick_reach; x <= drawn.x + pick_reach; ++x) {
                update_pointer(static_cast<float>(x), static_cast<float>(drawn.y));
                if (hovered_match_unit_ == picked)
                    return x;
            }
            return std::numeric_limits<int>::min();
        };
        const int hit_on_pixel = first_hit();
        if (hit_on_pixel == std::numeric_limits<int>::min())
            fail("the pointer found the unit nowhere near where it is drawn");
        rest_pointer();
        // The battlefield's columns and rows, along those through its
        // middle, over which the pointer finds the game view
        // (Game.battlefield_rect), where hover and picking look.
        const auto over_game_view = [&]() {
            bind_match_view();
            const auto rect = match_->state().game.battlefield_rect;
            const int middle_x = match_layout_.left + match_layout_.battlefield_width() / 2;
            const int middle_y = match_layout_.top + match_layout_.battlefield_height() / 2;
            std::array<int, 2> found{};
            for (int x = match_layout_.left;
                 x < match_layout_.left + match_layout_.battlefield_width();
                 ++x)
                if (game_screen_point(static_cast<float>(x), static_cast<float>(middle_y)).x <=
                    rect.x2)
                    ++found[0];
            for (int y = match_layout_.top;
                 y < match_layout_.top + match_layout_.battlefield_height();
                 ++y)
                if (game_screen_point(static_cast<float>(middle_x), static_cast<float>(y)).y <=
                    rect.y2)
                    ++found[1];
            return found;
        };
        const auto on_pixel_view = over_game_view();
        scroll_match_view(1, 0, pick_offset * static_cast<double>(pick_zoom));
        if (view_camera() != camera || view_offset().x != pick_offset)
            fail(
                "a scroll of three quarters of a map pixel did not move the view between map "
                "pixels"
            );
        const auto after = presented();
        if (accelerated_.frame_offset.x != pick_offset)
            fail("the frame did not draw the view between map pixels");
        const double drawn_shift = horizontal_shift(
            before, after, measured(pick_zoom, frame_shift_reach), cursor, frame_shift_reach
        );
        const int hit_between = first_hit();
        const int picked_shift = hit_on_pixel - hit_between;
        std::cout << "render tiers check: at zoom 4 a view 0.75 map pixels on is drawn "
                  << drawn_shift << " screen pixels left, and the pointer first finds the unit "
                  << picked_shift << " pixels left\n";
        const double expected = pick_offset * static_cast<double>(pick_zoom);
        if (std::abs(drawn_shift - expected) > most_drawn_stray)
            fail("the view between map pixels was not drawn where it lies");
        if (picked_shift != static_cast<int>(expected))
            fail("the pointer does not pick the unit where it is drawn");
        // Up to the battlefield's right and bottom edges, the pointer finds
        // the game view as far as it does on the camera's map pixel.
        if (over_game_view() != on_pixel_view)
            fail("the pointer does not find the game view up to the battlefield's edge");
        // Mouse-look warps the pointer to a point of the game's screen and
        // reads it back: the two mappings stay each other's inverse, from
        // the first map pixel the view draws whole.
        for (int point = 1; point <= mapped_points; ++point) {
            const int32_t x = oa::ui::display_layout::kSourceLeft + mapped_columns_apart * point;
            const int32_t y = oa::ui::display_layout::kSourceTop + mapped_rows_apart * point;
            const auto canvas = game_screen_canvas(x, y);
            const auto back =
                game_screen_point(static_cast<float>(canvas.x), static_cast<float>(canvas.y));
            if (back.x != x || back.y != y)
                fail("the game's screen and the canvas do not map back to each other");
        }
        rest_pointer();
    }
    // At zoom 4, the nearest, with the view between map pixels: the wheel,
    // which leaves the zoom there, and the menu's ease about the
    // battlefield's centre to the zoom it is at hold the point drawn under
    // their anchor where it is drawn, the camera staying where its rounding
    // about the anchor puts it, as it always has.
    for (const double offset : anchor_offsets) {
        for (const bool wheel : {true, false}) {
            at_zoom(pick_zoom);
            scroll_zoom_carry_ = 0.0;
            std::ignore = presented();
            const auto camera = view_camera();
            const int way_down = most_view_offsets(camera[0], camera[1]).y >= offset ? 1 : 0;
            scroll_match_view(1, way_down, offset * static_cast<double>(pick_zoom));
            if (view_camera() != camera || view_offset().x != offset)
                fail("a scroll of under a map pixel did not move the view between map pixels");
            std::ignore = presented();
            const int whole = static_cast<int>(pick_zoom);
            const std::array<int, 2> anchor =
                wheel ? std::array<int, 2>{
                            whole * (match_layout_.battlefield_width() / (2 * whole)) +
                                wheel_anchor_past,
                            whole * (match_layout_.battlefield_height() / (2 * whole)) +
                                wheel_anchor_past
                        }
                      : std::array<int, 2>{
                            match_layout_.battlefield_width() / 2,
                            match_layout_.battlefield_height() / 2
                        };
            // The point drawn under the anchor, in map pixels.
            const auto drawn_point = [&]() {
                const auto at = view_camera();
                return std::array<double, 2>{
                    static_cast<double>(at[0]) + accelerated_.frame_offset.x +
                        static_cast<double>(anchor[0]) / static_cast<double>(pick_zoom),
                    static_cast<double>(at[1]) + accelerated_.frame_offset.y +
                        static_cast<double>(anchor[1]) / static_cast<double>(pick_zoom)
                };
            };
            const auto before = drawn_point();
            if (wheel)
                handle_match_zoom(
                    wheel_nearer,
                    static_cast<float>(match_layout_.left + anchor[0]),
                    static_cast<float>(match_layout_.top + anchor[1])
                );
            else
                EngineSettingsState::ease_zoom_about_centre(*this, pick_zoom);
            step_match_zoom();
            std::ignore = presented();
            if (match_zoom() != pick_zoom)
                fail("a zoom to the zoom it was at changed the zoom");
            const auto after = drawn_point();
            const auto moved_to = view_camera();
            const char* what = wheel ? "the wheel" : "the ease about the centre";
            std::cout << "render tiers check: at zoom 4 with the view " << offset
                      << " map pixels on, " << what << " moved the point under its anchor "
                      << (after[0] - before[0]) * static_cast<double>(pick_zoom) << ", "
                      << (after[1] - before[1]) * static_cast<double>(pick_zoom)
                      << " screen pixels\n";
            for (std::size_t axis = 0; axis < 2; ++axis) {
                // At the battlefield's centre the camera's own rounding may
                // step it, as the standard tier's does there; the wheel's
                // anchor is put where it does not.
                if (moved_to[axis] != camera[axis]) {
                    if (wheel)
                        fail(std::string(what) + " at the nearest zoom moved the camera");
                    continue;
                }
                if (std::abs(after[axis] - before[axis]) * static_cast<double>(pick_zoom) >
                    most_frame_motion)
                    fail(std::string(what) + " moved the point drawn under its anchor");
            }
        }
    }
    rest_pointer();
    std::cout << "render tiers check: the accelerated tier's view moves at most a pixel a "
                 "frame between map pixels, the pointer picks what is drawn, and a zoom "
                 "holds what is drawn under its anchor\n";
}

} // namespace oa::app
