// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Painting for the touch controls' layer: blending with straight alpha,
// rectangles clipped to the canvas and the clip, rounded corners left clear,
// discs, the radial menu's wedges, every icon inside its box, coverage maps
// in their colour, and lines of text from the bundled fonts when they lie
// beside the test.
#include "touch_paint.hpp"
#include "oa/test/check.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <numbers>
#include <span>

namespace {

namespace paint = oa::app::touch_paint;
namespace text_font = oa::platform::text_font;

constexpr paint::Rgba red{200, 0, 0, 255};
constexpr paint::Rgba blue{0, 0, 255, 255};
constexpr paint::Rgba green{156, 204, 60, 255};

/// Returns whether two channel values lie within a tolerance of each other.
///
/// @param a a value
/// @param b another value
/// @param tolerance the largest difference allowed
/// @return true when they are that close
bool near(int a, int b, int tolerance = 1) {
    return std::abs(a - b) <= tolerance;
}

/// Checks that a rectangle partly off the canvas paints only its part on it.
void rectangles_are_clipped_to_the_canvas() {
    auto canvas = paint::make_canvas(4, 3);
    OA_CHECK(canvas.rgba.size() == 4U * 3U * 4U);
    paint::Painter painter(canvas);
    painter.fill_rect({-1, 1, 3, 5}, paint::Rgba{10, 20, 30, 255});
    OA_CHECK(paint::pixel_at(canvas, 0, 1) == (paint::Rgba{10, 20, 30, 255}));
    OA_CHECK(paint::pixel_at(canvas, 1, 2).r == 10);
    OA_CHECK(paint::pixel_at(canvas, 2, 1).a == 0);
    OA_CHECK(paint::pixel_at(canvas, 0, 0).a == 0);
    OA_CHECK(painter.painted() == (paint::Box{0, 1, 2, 2}));
}

/// Checks the clip: nothing outside it changes, and the painted box stays inside it.
void painting_keeps_inside_the_clip() {
    auto canvas = paint::make_canvas(20, 20);
    paint::Painter painter(canvas);
    painter.set_clip({5, 5, 6, 4});
    OA_CHECK(painter.clip() == (paint::Box{5, 5, 6, 4}));
    painter.fill_rect({0, 0, 20, 20}, red);
    painter.fill_circle({10.0F, 10.0F}, 9.0F, blue);
    for (int y = 0; y < 20; ++y)
        for (int x = 0; x < 20; ++x) {
            const bool inside = x >= 5 && x < 11 && y >= 5 && y < 9;
            OA_CHECK((paint::pixel_at(canvas, x, y).a != 0) == inside);
        }
    OA_CHECK(painter.painted() == (paint::Box{5, 5, 6, 4}));
    painter.set_clip({-10, 15, 100, 100});
    OA_CHECK(painter.clip() == (paint::Box{0, 15, 20, 5}));
    painter.reset_clip();
    OA_CHECK(painter.clip() == (paint::Box{0, 0, 20, 20}));
    painter.clear_box({5, 5, 3, 4});
    OA_CHECK(paint::pixel_at(canvas, 6, 6).a == 0);
    OA_CHECK(paint::pixel_at(canvas, 9, 6).a != 0);
}

/// Checks straight-alpha blending over an opaque colour and over a clear pixel.
void colours_blend_by_their_opacity() {
    auto canvas = paint::make_canvas(2, 1);
    paint::Painter painter(canvas);
    painter.fill_rect({0, 0, 1, 1}, red);
    painter.fill_rect({0, 0, 2, 1}, paint::Rgba{0, 0, 255, 128});
    const auto over_red = paint::pixel_at(canvas, 0, 0);
    // 128/255 of blue over red: red keeps 127/255 of 200.
    OA_CHECK(near(over_red.r, 100));
    OA_CHECK(over_red.g == 0);
    OA_CHECK(near(over_red.b, 128));
    OA_CHECK(over_red.a == 255);
    // Over a clear pixel the colour stays itself at its own opacity.
    const auto over_clear = paint::pixel_at(canvas, 1, 0);
    OA_CHECK(over_clear == (paint::Rgba{0, 0, 255, 128}));
    // Two half-opaque layers make three quarters.
    painter.fill_rect({1, 0, 1, 1}, paint::Rgba{0, 0, 255, 128});
    OA_CHECK(near(paint::pixel_at(canvas, 1, 0).a, 192));
    OA_CHECK(paint::pixel_at(canvas, 1, 0).b == 255);
    // An opacity factor scales the colour's own.
    OA_CHECK(paint::with_opacity(paint::Rgba{1, 2, 3, 200}, 0.5F).a == 100);
    OA_CHECK(paint::with_opacity(paint::Rgba{1, 2, 3, 200}, 2.0F).a == 200);
    OA_CHECK(paint::with_opacity(paint::Rgba{1, 2, 3, 200}, -1.0F).a == 0);
    // Partial coverage blends a share of the colour.
    std::array<uint8_t, 4> pixel{0, 0, 0, 255};
    paint::blend_pixel(pixel.data(), paint::Rgba{255, 255, 255, 255}, 51);
    OA_CHECK(near(pixel[0], 51));
    OA_CHECK(pixel[3] == 255);
}

/// Checks boxes: overlap, union and emptiness.
void boxes_meet_and_join() {
    OA_CHECK(paint::intersect({0, 0, 10, 10}, {5, 5, 10, 10}) == (paint::Box{5, 5, 5, 5}));
    OA_CHECK(paint::intersect({0, 0, 5, 5}, {5, 5, 5, 5}).empty());
    OA_CHECK(paint::unite({0, 0, 2, 2}, {8, 4, 2, 2}) == (paint::Box{0, 0, 10, 6}));
    OA_CHECK(paint::unite({}, {3, 3, 1, 1}) == (paint::Box{3, 3, 1, 1}));
    OA_CHECK(paint::unite({3, 3, 1, 1}, {}) == (paint::Box{3, 3, 1, 1}));
}

/// Checks that a rounded rectangle leaves its corners clear and fills its edges' middles.
void rounded_corners_stay_clear() {
    auto canvas = paint::make_canvas(30, 30);
    paint::Painter painter(canvas);
    painter.fill_rounded_rect({2.0F, 2.0F, 24.0F, 20.0F}, 6.0F, green);
    OA_CHECK(paint::pixel_at(canvas, 2, 2).a == 0);
    OA_CHECK(paint::pixel_at(canvas, 25, 2).a == 0);
    OA_CHECK(paint::pixel_at(canvas, 2, 21).a == 0);
    OA_CHECK(paint::pixel_at(canvas, 25, 21).a == 0);
    OA_CHECK(paint::pixel_at(canvas, 14, 2) == green);
    OA_CHECK(paint::pixel_at(canvas, 2, 12) == green);
    OA_CHECK(paint::pixel_at(canvas, 14, 12) == green);
    OA_CHECK(paint::pixel_at(canvas, 14, 22).a == 0);
    OA_CHECK(paint::pixel_at(canvas, 1, 12).a == 0);
    // The corner's edge is smoothed: a pixel the arc crosses is partly covered.
    bool partial = false;
    for (int i = 2; i < 8; ++i) {
        const auto a = paint::pixel_at(canvas, i, i).a;
        partial = partial || (a > 0 && a < 255);
    }
    OA_CHECK(partial);
    OA_CHECK(painter.painted() == (paint::Box{2, 2, 24, 20}));

    // The outline keeps its inside clear.
    auto outlined = paint::make_canvas(30, 30);
    paint::Painter edge(outlined);
    edge.outline_rounded_rect({2.0F, 2.0F, 24.0F, 20.0F}, 6.0F, 2.0F, red);
    OA_CHECK(paint::pixel_at(outlined, 14, 2) == red);
    OA_CHECK(paint::pixel_at(outlined, 14, 3) == red);
    OA_CHECK(paint::pixel_at(outlined, 14, 12).a == 0);
    OA_CHECK(paint::pixel_at(outlined, 2, 2).a == 0);

    // A whole-pixel outline.
    auto boxed = paint::make_canvas(10, 10);
    paint::Painter frame(boxed);
    frame.outline_rect({1, 1, 8, 8}, 1, red);
    OA_CHECK(paint::pixel_at(boxed, 1, 1) == red);
    OA_CHECK(paint::pixel_at(boxed, 8, 5) == red);
    OA_CHECK(paint::pixel_at(boxed, 5, 5).a == 0);
}

/// Checks discs and rings.
void circles_fill_and_ring() {
    auto canvas = paint::make_canvas(40, 40);
    paint::Painter painter(canvas);
    painter.fill_circle({20.0F, 20.0F}, 10.0F, blue);
    OA_CHECK(paint::pixel_at(canvas, 20, 20) == blue);
    OA_CHECK(paint::pixel_at(canvas, 20, 11) == blue);
    OA_CHECK(paint::pixel_at(canvas, 20, 8).a == 0);
    OA_CHECK(paint::pixel_at(canvas, 12, 12).a == 0);
    auto ringed = paint::make_canvas(40, 40);
    paint::Painter ring(ringed);
    ring.outline_circle({20.0F, 20.0F}, 10.0F, 2.0F, blue);
    OA_CHECK(paint::pixel_at(ringed, 20, 20).a == 0);
    OA_CHECK(paint::pixel_at(ringed, 20, 9).a != 0);
    OA_CHECK(paint::pixel_at(ringed, 30, 20).a != 0);
}

/// Checks the radial menu's wedges: inside the ring and the angle only.
void sectors_cover_their_wedge() {
    constexpr float pi = std::numbers::pi_v<float>;
    auto canvas = paint::make_canvas(100, 100);
    paint::Painter painter(canvas);
    const paint::Spot centre{50.0F, 50.0F};
    // The wedge at the top: 30 degrees wide, between radii 15 and 40.
    painter.fill_sector(centre, 15.0F, 40.0F, 0.0F, pi / 12.0F, green);
    OA_CHECK(paint::pixel_at(canvas, 50, 22) == green); // inside, straight up
    OA_CHECK(paint::pixel_at(canvas, 50, 45).a == 0);   // inside the hub
    OA_CHECK(paint::pixel_at(canvas, 50, 5).a == 0);    // beyond the ring
    OA_CHECK(paint::pixel_at(canvas, 75, 25).a == 0);   // beside the wedge
    OA_CHECK(paint::pixel_at(canvas, 50, 78).a == 0);   // the other way
    // Three o'clock: a quarter turn clockwise from up.
    painter.fill_sector(centre, 15.0F, 40.0F, pi / 2.0F, pi / 12.0F, red);
    OA_CHECK(paint::pixel_at(canvas, 78, 50) == red);
    OA_CHECK(paint::pixel_at(canvas, 22, 50).a == 0);
    // The outline of a wedge leaves its middle clear.
    auto edged = paint::make_canvas(100, 100);
    paint::Painter edge(edged);
    edge.outline_sector(centre, 15.0F, 40.0F, pi, pi / 12.0F, 2.0F, blue);
    OA_CHECK(paint::pixel_at(edged, 50, 78).a == 0);
    OA_CHECK(paint::pixel_at(edged, 50, 89).a != 0); // near the outer edge
    // An arc wider than a half turn leaves its gap clear.
    auto arced = paint::make_canvas(60, 60);
    paint::Painter arc(arced);
    arc.stroke_arc({30.0F, 30.0F}, 20.0F, pi, pi * 0.75F, 3.0F, red);
    OA_CHECK(paint::pixel_at(arced, 30, 10).a == 0); // the gap at the top
    OA_CHECK(paint::pixel_at(arced, 30, 49).a != 0); // the bottom
    OA_CHECK(paint::pixel_at(arced, 10, 30).a != 0); // the left
    OA_CHECK(paint::pixel_at(arced, 30, 30).a == 0); // the centre
}

/// Checks strokes and polygons.
void strokes_and_polygons_fill_their_shape() {
    auto canvas = paint::make_canvas(40, 40);
    paint::Painter painter(canvas);
    painter.stroke_line({5.0F, 20.0F}, {35.0F, 20.0F}, 4.0F, red);
    OA_CHECK(paint::pixel_at(canvas, 20, 20) == red);
    OA_CHECK(paint::pixel_at(canvas, 20, 25).a == 0);
    OA_CHECK(paint::pixel_at(canvas, 38, 20).a == 0);
    const std::array<paint::Spot, 3> triangle{{{5.0F, 5.0F}, {35.0F, 5.0F}, {5.0F, 35.0F}}};
    auto shaped = paint::make_canvas(40, 40);
    paint::Painter polygon(shaped);
    polygon.fill_polygon(triangle, blue);
    OA_CHECK(paint::pixel_at(shaped, 10, 10) == blue);
    OA_CHECK(paint::pixel_at(shaped, 30, 30).a == 0);
    polygon.fill_polygon(std::span<const paint::Spot>(triangle.data(), 2), red);
    OA_CHECK(paint::pixel_at(shaped, 10, 10) == blue);
}

/// Checks that every icon paints something and nothing outside its box.
void icons_stay_inside_their_boxes() {
    for (std::size_t index = 1; index < paint::icon_count; ++index) {
        const auto icon = static_cast<paint::Icon>(index);
        auto canvas = paint::make_canvas(64, 64);
        paint::Painter painter(canvas);
        const paint::Area box{20.0F, 16.0F, 24.0F, 32.0F}; // the icon is 24 a side, centred
        painter.draw_icon(icon, box, paint::Rgba{255, 255, 255, 255});
        int painted = 0;
        bool outside = false;
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 64; ++x) {
                if (paint::pixel_at(canvas, x, y).a == 0)
                    continue;
                ++painted;
                // A pixel of smoothing beyond the edge is allowed.
                outside = outside || x < 19 || x > 44 || y < 19 || y > 44;
            }
        if (painted < 12 || outside)
            std::fprintf(
                stderr,
                "icon %zu: %d pixels painted%s\n",
                index,
                painted,
                outside ? ", some outside its box" : ""
            );
        OA_CHECK(painted >= 12);
        OA_CHECK(!outside);
        // The painted box tells the same.
        const auto box_painted = painter.painted();
        OA_CHECK(!box_painted.empty());
        OA_CHECK(box_painted.x >= 19 && box_painted.y >= 19);
        OA_CHECK(
            box_painted.x + box_painted.width <= 46 && box_painted.y + box_painted.height <= 46
        );
    }
    auto canvas = paint::make_canvas(8, 8);
    paint::Painter painter(canvas);
    painter.draw_icon(paint::Icon::none, {0.0F, 0.0F, 8.0F, 8.0F}, red);
    painter.draw_icon(paint::Icon::plus, {0.0F, 0.0F, 0.0F, 8.0F}, red);
    OA_CHECK(painter.painted().empty());
}

/// Checks that a coverage map paints its colour at each byte's share.
void coverage_paints_in_its_colour() {
    auto canvas = paint::make_canvas(6, 3);
    paint::Painter painter(canvas);
    painter.fill_rect({0, 0, 6, 3}, paint::Rgba{0, 0, 0, 255});
    const std::array<uint8_t, 6> alpha{255, 128, 0, 255, 0, 64};
    painter.forget_painted();
    painter.draw_coverage(alpha, 3, 2, 4, 1, green); // its right column is cut off
    OA_CHECK(paint::pixel_at(canvas, 4, 1) == green);
    const auto half = paint::pixel_at(canvas, 5, 1);
    OA_CHECK(near(half.r, 156 * 128 / 255));
    OA_CHECK(near(half.g, 204 * 128 / 255));
    OA_CHECK(half.a == 255);
    OA_CHECK(paint::pixel_at(canvas, 4, 2) == green);
    OA_CHECK(paint::pixel_at(canvas, 5, 2) == (paint::Rgba{0, 0, 0, 255}));
    OA_CHECK(painter.painted() == (paint::Box{4, 1, 2, 2}));
    // Over a clear canvas the coverage becomes the opacity.
    auto clear = paint::make_canvas(3, 1);
    paint::Painter over_clear(clear);
    over_clear.draw_coverage(std::span<const uint8_t>(alpha.data(), 3), 3, 1, 0, 0, green);
    OA_CHECK(paint::pixel_at(clear, 1, 0).a == 128);
    OA_CHECK(paint::pixel_at(clear, 1, 0).g == 204);
    OA_CHECK(paint::pixel_at(clear, 2, 0).a == 0);
    // A map shorter than its size says paints nothing.
    over_clear.forget_painted();
    over_clear.draw_coverage(std::span<const uint8_t>(alpha.data(), 2), 3, 1, 0, 0, red);
    OA_CHECK(over_clear.painted().empty());
}

/// Checks text from the bundled fonts, when they lie beside the test: the
/// line is painted in its colour, fits where it is told, and long text is
/// shortened and wrapped.
void text_paints_in_its_colour() {
    auto fonts = text_font::FontStack::open(text_font::bundled_font_directory());
    if (!fonts) {
        std::printf("app-touch-paint: no fonts beside the test, so FreeType text was not drawn\n");
        return;
    }
    const auto line = paint::draw_line(*fonts, "QUEUE", 20, true);
    OA_CHECK(line.drawn);
    auto canvas = paint::make_canvas(120, 40);
    paint::Painter painter(canvas);
    const auto box = paint::paint_line(painter, line, 10, 28, green);
    OA_CHECK(!box.empty());
    OA_CHECK(box.y + line.coverage.baseline == 28);
    int full = 0;
    for (int y = 0; y < canvas.height; ++y)
        for (int x = 0; x < canvas.width; ++x) {
            const auto pixel = paint::pixel_at(canvas, x, y);
            if (pixel.a == 0)
                continue;
            // Every touched pixel is the text's colour, its coverage in the opacity.
            OA_CHECK(pixel.r == green.r && pixel.g == green.g && pixel.b == green.b);
            OA_CHECK(x >= box.x && x < box.x + box.width && y >= box.y && y < box.y + box.height);
            full += pixel.a == 255 ? 1 : 0;
        }
    OA_CHECK(full > 20);
    const int width = paint::text_width(*fonts, "QUEUE", 20, true);
    OA_CHECK(width > 40 && width < 110);
    OA_CHECK(paint::text_width(*fonts, "", 20, true) == 0);
    const auto metrics = paint::line_metrics(*fonts, 20, true);
    OA_CHECK(metrics.ascent > 10 && metrics.descent > 0);

    const auto fitted = paint::fit_text(*fonts, "SELF-DESTRUCT HOLD", 20, true, 80);
    OA_CHECK(paint::text_width(*fonts, fitted, 20, true) <= 80);
    OA_CHECK(fitted.size() >= 3 && fitted.substr(fitted.size() - 3) == "\xE2\x80\xA6");
    OA_CHECK(paint::fit_text(*fonts, "STOP", 20, true, 500) == "STOP");
    OA_CHECK(paint::fit_text(*fonts, "STOP", 20, true, 1).empty());
    const auto lines =
        paint::wrap_text(*fonts, "Hold to self-destruct the selected units", 16, true, 120);
    OA_CHECK(lines.size() >= 2);
    for (const auto& wrapped : lines)
        OA_CHECK(paint::text_width(*fonts, wrapped, 16, true) <= 120);
    OA_CHECK(paint::wrap_text(*fonts, "", 16, true, 120).empty());
}

} // namespace

int main() {
    rectangles_are_clipped_to_the_canvas();
    painting_keeps_inside_the_clip();
    colours_blend_by_their_opacity();
    boxes_meet_and_join();
    rounded_corners_stay_clear();
    circles_fill_and_ring();
    sectors_cover_their_wedge();
    strokes_and_polygons_fill_their_shape();
    icons_stay_inside_their_boxes();
    coverage_paints_in_its_colour();
    text_paints_in_its_colour();
    return oa::test::check_exit_status();
}
