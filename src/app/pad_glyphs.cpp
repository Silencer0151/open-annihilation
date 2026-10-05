// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The gamepad's button glyphs (pad_glyphs.hpp): every glyph is the
// project's own drawing, made of the touch painter's discs, rounded
// rectangles and strokes, with letters from the bundled fonts. Sizes are
// shares of the glyph's height, so a glyph reads the same at any size.
#include "pad_glyphs.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <string_view>

namespace oa::app::pad_glyphs {

namespace {

namespace pad = oa::ui::pad_controls;
namespace paint = touch_paint;

/// A pill's width at a height: this share, and this share again for each letter it shows.
constexpr float pill_base_share = 0.55f;
/// What each letter adds to a pill's width, as a share of its height.
constexpr float pill_letter_share = 0.42f;
/// The narrowest pill, as a share of its height: one letter still reads as a pill.
constexpr float pill_least_share = 1.25f;
/// A disc's letter, as a share of the glyph's height.
constexpr float disc_letter_share = 0.62f;
/// A pill's label, as a share of the glyph's height.
constexpr float pill_letter_size_share = 0.52f;
/// The width of the marks' strokes, as a share of the glyph's height.
constexpr float stroke_share = 0.09f;
/// The gap either side of a chord's plus, as a share of the glyphs' height.
constexpr float chord_gap_share = 0.15f;
/// A chord's plus, as a share of the glyphs' height.
constexpr float chord_plus_share = 0.42f;
/// The cap height of the bundled fonts, as a share of the pixel size: centres capitals on a row.
constexpr float cap_share = 0.73f;
/// The smallest letters drawn, in pixels per em.
constexpr int least_letter_px = 6;
/// The D-pad's arms that are not lit, at this opacity.
constexpr float unlit_arm_opacity = 0.3f;

/// Returns a box's centre.
///
/// @param box the box
/// @return its centre
paint::Spot centre_of(paint::Area box) noexcept {
    return {box.x + box.width * 0.5f, box.y + box.height * 0.5f};
}

/// Paints letters centred on a point, shortened to a width.
///
/// @param painter the painter
/// @param fonts the fonts; null paints nothing
/// @param text the letters
/// @param centre where their middle goes
/// @param size_px their size, pixels per em
/// @param max_width the widest they may be, pixels
/// @param colour their colour
void paint_letters(
    paint::Painter& painter,
    oa::platform::text_font::FontStack* fonts,
    std::string_view text,
    paint::Spot centre,
    int size_px,
    float max_width,
    paint::Rgba colour
) {
    if (fonts == nullptr || text.empty() || max_width <= 0.0f)
        return;
    const int size = std::max(least_letter_px, size_px);
    const std::string fitted =
        paint::fit_text(*fonts, text, size, true, static_cast<int>(std::lround(max_width)));
    if (fitted.empty())
        return;
    const auto line = paint::draw_line(*fonts, fitted, size, true);
    if (!line.drawn)
        return;
    const float width = static_cast<float>(line.coverage.advance);
    const int pen = static_cast<int>(std::lround(centre.x - width * 0.5f));
    const int baseline =
        static_cast<int>(std::lround(centre.y + cap_share * static_cast<float>(size) * 0.5f));
    paint::paint_line(painter, line, pen, baseline, colour);
}

/// Paints a plus centred on a point.
///
/// @param painter the painter
/// @param centre where its middle goes
/// @param arm half its span, pixels
/// @param width its strokes' width, pixels
/// @param colour its colour
void paint_plus(
    paint::Painter& painter, paint::Spot centre, float arm, float width, paint::Rgba colour
) {
    painter.stroke_line({centre.x - arm, centre.y}, {centre.x + arm, centre.y}, width, colour);
    painter.stroke_line({centre.x, centre.y - arm}, {centre.x, centre.y + arm}, width, colour);
}

/// Paints a D-pad on a disc: four arms round a middle, one lit.
///
/// @param painter the painter
/// @param box the glyph's square
/// @param direction the lit arm: 0 up, 1 right, 2 down, 3 left
/// @param ink the arms' colour, the lit one whole and the others faint
/// @param fill the disc's colour
void paint_dpad(
    paint::Painter& painter, paint::Area box, uint8_t direction, paint::Rgba ink, paint::Rgba fill
) {
    const float h = box.height;
    const float arm = h * 0.3f;    // from the middle to an arm's end
    const float thick = h * 0.24f; // an arm's width
    const paint::Spot middle = centre_of(box);
    const float radius = thick * 0.25f;
    painter.fill_circle(middle, h * 0.5f - 0.5f, fill);
    const paint::Rgba unlit = paint::with_opacity(ink, unlit_arm_opacity);
    // Each arm from the middle outward: up, right, down, left.
    const std::array<paint::Area, 4> arms{{
        {middle.x - thick * 0.5f, middle.y - arm - thick * 0.5f, thick, arm + thick * 0.25f},
        {middle.x - thick * 0.25f, middle.y - thick * 0.5f, arm + thick * 0.75f, thick},
        {middle.x - thick * 0.5f, middle.y - thick * 0.25f, thick, arm + thick * 0.75f},
        {middle.x - arm - thick * 0.5f, middle.y - thick * 0.5f, arm + thick * 0.25f, thick},
    }};
    for (std::size_t index = 0; index < arms.size(); ++index)
        painter.fill_rounded_rect(arms[index], radius, index == direction ? ink : unlit);
    painter.fill_rounded_rect(
        {middle.x - thick * 0.5f, middle.y - thick * 0.5f, thick, thick}, 0.0f, unlit
    );
    if (direction < arms.size())
        painter.fill_rounded_rect(arms[direction], radius, ink);
}

} // namespace

float spec_width(const pad::GlyphSpec& spec, float height) noexcept {
    if (height <= 0.0f)
        return 0.0f;
    if (spec.shape != pad::GlyphShape::pill)
        return height;
    const float letters = static_cast<float>(spec.text.size());
    return height * std::max(pill_least_share, pill_base_share + pill_letter_share * letters);
}

void draw_spec(
    paint::Painter& painter,
    oa::platform::text_font::FontStack* fonts,
    const pad::GlyphSpec& spec,
    paint::Area box,
    paint::Rgba ink,
    paint::Rgba fill
) {
    const float h = box.height;
    if (h <= 0.0f || box.width <= 0.0f)
        return;
    const float width = std::min(spec_width(spec, h), box.width);
    const paint::Area glyph{box.x + (box.width - width) * 0.5f, box.y, width, h};
    const paint::Spot middle = centre_of(glyph);
    const float side = std::min(width, h);
    const float radius = side * 0.5f;
    const float stroke = std::max(1.0f, h * stroke_share);
    const auto disc = [&] { painter.fill_circle(middle, radius - 0.5f, fill); };
    switch (spec.shape) {
    case pad::GlyphShape::letter_circle:
        disc();
        paint_letters(
            painter,
            fonts,
            spec.text,
            middle,
            static_cast<int>(std::lround(h * disc_letter_share)),
            side * 0.8f,
            ink
        );
        return;
    case pad::GlyphShape::pill:
        painter.fill_rounded_rect(glyph, h * 0.5f, fill);
        paint_letters(
            painter,
            fonts,
            spec.text,
            middle,
            static_cast<int>(std::lround(h * pill_letter_size_share)),
            width - h * 0.4f,
            ink
        );
        return;
    case pad::GlyphShape::cross: {
        disc();
        const float reach = side * 0.22f;
        painter.stroke_line(
            {middle.x - reach, middle.y - reach}, {middle.x + reach, middle.y + reach}, stroke, ink
        );
        painter.stroke_line(
            {middle.x - reach, middle.y + reach}, {middle.x + reach, middle.y - reach}, stroke, ink
        );
        return;
    }
    case pad::GlyphShape::circle:
        disc();
        painter.outline_circle(middle, side * 0.23f, stroke, ink);
        return;
    case pad::GlyphShape::square: {
        disc();
        const float half = side * 0.21f;
        painter.outline_rounded_rect(
            {middle.x - half, middle.y - half, half * 2.0f, half * 2.0f}, stroke * 0.5f, stroke, ink
        );
        return;
    }
    case pad::GlyphShape::triangle: {
        disc();
        const float reach = side * 0.26f;
        const paint::Spot top{middle.x, middle.y - reach};
        const paint::Spot right{middle.x + reach * 0.9f, middle.y + reach * 0.62f};
        const paint::Spot left{middle.x - reach * 0.9f, middle.y + reach * 0.62f};
        painter.stroke_line(top, right, stroke, ink);
        painter.stroke_line(right, left, stroke, ink);
        painter.stroke_line(left, top, stroke, ink);
        return;
    }
    case pad::GlyphShape::dpad:
        paint_dpad(
            painter, {middle.x - side * 0.5f, glyph.y, side, side}, spec.direction, ink, fill
        );
        return;
    case pad::GlyphShape::trackpad: {
        // A rounded square, its dot toward the pad's side; pressed, a ring round the dot.
        const float outer = side * 0.88f;
        const paint::Area pad_area{middle.x - outer * 0.5f, middle.y - outer * 0.5f, outer, outer};
        painter.fill_rounded_rect(pad_area, outer * 0.24f, fill);
        const float shift = (spec.side == pad::Side::left ? -1.0f : 1.0f) * outer * 0.14f;
        const paint::Spot dot{middle.x + shift, middle.y};
        painter.fill_circle(dot, side * 0.1f, ink);
        if (spec.pressed)
            painter.outline_circle(dot, side * 0.22f, stroke * 0.8f, ink);
        return;
    }
    case pad::GlyphShape::stick: {
        // A ring with the stick's side in its middle; a click, a second ring inside.
        disc();
        painter.outline_circle(middle, side * 0.36f, stroke, ink);
        if (spec.pressed)
            painter.outline_circle(middle, side * 0.2f, stroke * 0.8f, ink);
        if (fonts != nullptr && !spec.pressed)
            paint_letters(
                painter,
                fonts,
                spec.side == pad::Side::left ? std::string_view{"L"} : std::string_view{"R"},
                middle,
                static_cast<int>(std::lround(h * 0.36f)),
                side * 0.5f,
                ink
            );
        else if (!spec.pressed)
            painter.fill_circle(middle, side * 0.08f, ink);
        return;
    }
    case pad::GlyphShape::view: {
        disc();
        if (!spec.text.empty()) {
            // Nintendo's minus.
            painter.stroke_line(
                {middle.x - side * 0.22f, middle.y},
                {middle.x + side * 0.22f, middle.y},
                stroke,
                ink
            );
            return;
        }
        // Two overlapping squares.
        const float square = side * 0.3f;
        const float offset = side * 0.08f;
        painter.outline_rounded_rect(
            {middle.x - square * 0.5f - offset, middle.y - square * 0.5f - offset, square, square},
            stroke * 0.4f,
            stroke * 0.8f,
            ink
        );
        painter.fill_rounded_rect(
            {middle.x - square * 0.5f + offset, middle.y - square * 0.5f + offset, square, square},
            stroke * 0.4f,
            fill
        );
        painter.outline_rounded_rect(
            {middle.x - square * 0.5f + offset, middle.y - square * 0.5f + offset, square, square},
            stroke * 0.4f,
            stroke * 0.8f,
            ink
        );
        return;
    }
    case pad::GlyphShape::menu: {
        disc();
        if (!spec.text.empty()) {
            // Nintendo's plus.
            paint_plus(painter, middle, side * 0.22f, stroke, ink);
            return;
        }
        // Three bars.
        const float bar = side * 0.22f;
        for (const float row : {-0.16f, 0.0f, 0.16f})
            painter.stroke_line(
                {middle.x - bar, middle.y + side * row},
                {middle.x + bar, middle.y + side * row},
                stroke * 0.8f,
                ink
            );
        return;
    }
    }
}

float glyph_width(pad::PadButton button, pad::GlyphStyle style, float height) noexcept {
    if (button == pad::PadButton::none)
        return 0.0f;
    return spec_width(pad::glyph_spec(button, style), height);
}

void draw_glyph(
    paint::Painter& painter,
    oa::platform::text_font::FontStack* fonts,
    pad::PadButton button,
    pad::GlyphStyle style,
    paint::Area box,
    paint::Rgba ink,
    paint::Rgba fill
) {
    if (button == pad::PadButton::none)
        return;
    draw_spec(painter, fonts, pad::glyph_spec(button, style), box, ink, fill);
}

float chord_width(const pad::Chord& chord, pad::GlyphStyle style, float height) noexcept {
    const float button = glyph_width(chord.button, style, height);
    if (chord.held == pad::PadButton::none)
        return button;
    return glyph_width(chord.held, style, height) +
           height * (2.0f * chord_gap_share + chord_plus_share) + button;
}

void draw_chord(
    paint::Painter& painter,
    oa::platform::text_font::FontStack* fonts,
    const pad::Chord& chord,
    pad::GlyphStyle style,
    paint::Area box,
    paint::Rgba ink,
    paint::Rgba fill
) {
    const float h = box.height;
    if (h <= 0.0f)
        return;
    float x = box.x;
    if (chord.held != pad::PadButton::none) {
        const float held = glyph_width(chord.held, style, h);
        draw_glyph(painter, fonts, chord.held, style, {x, box.y, held, h}, ink, fill);
        x += held + h * chord_gap_share;
        const float plus = h * chord_plus_share;
        paint_plus(
            painter,
            {x + plus * 0.5f, box.y + h * 0.5f},
            plus * 0.4f,
            std::max(1.0f, h * stroke_share),
            fill
        );
        x += plus + h * chord_gap_share;
    }
    draw_glyph(
        painter,
        fonts,
        chord.button,
        style,
        {x, box.y, glyph_width(chord.button, style, h), h},
        ink,
        fill
    );
}

} // namespace oa::app::pad_glyphs
