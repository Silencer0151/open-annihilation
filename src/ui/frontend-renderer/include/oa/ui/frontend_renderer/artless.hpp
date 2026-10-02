// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Drawing without the game's art, on an RGB surface: flat fills and blends,
// one-pixel bevels, outlines and lines, one-colour text in a game font and
// one-bit marks. The Open Annihilation settings dialog and its buttons are
// drawn this way. Everything is given in source pixels (the game's 640x480
// screen) and drawn through a Placement, which puts source pixel (0, 0) at
// a surface pixel and draws each source pixel as a whole-number block, so
// a caller draws at the size it shows. Every primitive clips to the surface,
// and to the placement's clip when it has one, and leaves a surface whose
// pixels do not fill its size as it is.
#pragma once

#include "oa/formats/fnt.hpp"
#include "oa/formats/hpi.hpp"
#include "oa/ui/frontend_renderer.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

namespace oa::ui::frontend_renderer {

/// A colour: red, green and blue.
using Rgb = std::array<uint8_t, 3>;

/// A rectangle in source pixels.
struct SourceRect {
    int32_t x{};      ///< left column
    int32_t y{};      ///< top row
    int32_t width{};  ///< columns; 0 or less is empty
    int32_t height{}; ///< rows; 0 or less is empty
};

/// Where source pixels land on a surface, and how large, and the source
/// pixels drawing may touch.
struct Placement {
    int32_t x{};      ///< surface column of source column 0
    int32_t y{};      ///< surface row of source row 0
    int32_t scale{1}; ///< surface pixels across each source pixel; below 1 draws nothing
    /// In source pixels: nothing is drawn outside it. Empty, the default,
    /// clips to the surface alone.
    SourceRect clip{};
};

/// A one-bit picture: `bits` holds width x height bytes, row by row, nonzero
/// where the mark is drawn.
struct Mark {
    int32_t width{};                 ///< columns
    int32_t height{};                ///< rows
    std::span<const uint8_t> bits{}; ///< width x height bytes
};

/// The OA mark in thin letters: "OA" five pixels high, its strokes one pixel
/// wide, 9 by 5 pixels. It suits a box of about 13 to 24 source pixels.
extern const Mark oa_mark_thin;

/// The OA mark in bold letters: "OA" seven pixels high, its upright strokes
/// two pixels wide, 13 by 7 pixels. It suits a box of about 20 source pixels
/// or more.
extern const Mark oa_mark_bold;

/// Fills a rectangle with a colour.
///
/// @param[in,out] surface the surface
/// @param placement where source pixels land
/// @param rect the rectangle, in source pixels
/// @param color the colour
void fill_source_rect(
    Surface& surface, const Placement& placement, const SourceRect& rect, Rgb color
) noexcept;

/// Blends a colour over a rectangle, as blend_rect blends it.
///
/// @param[in,out] surface the surface
/// @param placement where source pixels land
/// @param rect the rectangle, in source pixels
/// @param color the colour
/// @param opacity the colour's share of each pixel, in 256ths (blend_opaque paints it)
void blend_source_rect(
    Surface& surface,
    const Placement& placement,
    const SourceRect& rect,
    Rgb color,
    uint32_t opacity
) noexcept;

/// Draws a one-pixel raised edge round the inside of a rectangle: light along
/// the top and left, dark along the bottom and right.
///
/// The dark edges run the rectangle's whole width and height, so the top
/// right and bottom left corners are dark; the light ones stop short of them.
/// A rectangle one pixel wide or high is dark throughout.
///
/// @param[in,out] surface the surface
/// @param placement where source pixels land
/// @param rect the rectangle, in source pixels; its inside is left as it is
/// @param light the top and left edges' colour
/// @param dark the bottom and right edges' colour
void draw_bevel(
    Surface& surface, const Placement& placement, const SourceRect& rect, Rgb light, Rgb dark
) noexcept;

/// Draws a one-pixel outline round the inside of a rectangle. A hairline is
/// a fill one pixel high or wide (fill_source_rect).
///
/// @param[in,out] surface the surface
/// @param placement where source pixels land
/// @param rect the rectangle, in source pixels; its inside is left as it is
/// @param color the outline's colour
void draw_outline(
    Surface& surface, const Placement& placement, const SourceRect& rect, Rgb color
) noexcept;

/// The share of the text colour each palette index of a font's glyph pixels
/// draws, in 256ths: 0 draws nothing and blend_opaque the colour itself.
using GlyphInk = std::array<uint16_t, oa::palette_color_count>;

/// A font readied for text in one colour: its glyphs, and how much of the
/// colour each of their pixels draws.
struct TextFont {
    oa::formats::fnt::Font font{}; ///< the glyphs
    GlyphInk ink{};                ///< by the palette index a glyph pixel holds
};

/// Readies a font for text in one colour, shaded as its glyphs are.
///
/// A colour's brightness is 299 red + 587 green + 114 blue. The colour that
/// most of the glyphs' edge pixels hold (a pixel beside an empty one or the
/// glyph's border) rings the letters: it and every darker colour draw
/// nothing. The brightest colour the glyphs hold draws the text colour, and
/// each colour between draws the share of it that its brightness lies
/// between the two, rounded to the nearest 256th. A font whose ring is its
/// brightest colour, such as a one-bit FNT font, draws every glyph pixel in
/// the colour. So the game's shaded and outlined GUI fonts keep their
/// shading and lose their dark outline.
///
/// @param font the font
/// @param palette the palette its glyph pixels index, four bytes a colour
/// @return the font and its ink
[[nodiscard]] TextFont text_font(oa::formats::fnt::Font font, const oa::PaletteBytes& palette);

/// Draws text in one colour: only the glyphs' own pixels are written, each
/// in the colour, whatever colours the font's glyphs hold. It suits one-bit
/// fonts; a shaded or outlined font draws better as a TextFont.
///
/// Bytes below 0x20 and bytes without a glyph are skipped, and a space
/// advances without drawing, as oa::formats::fnt::raster_text draws them.
///
/// @param[in,out] surface the surface
/// @param placement where source pixels land
/// @param font the font
/// @param text the text, one byte a glyph
/// @param x the pen column, in source pixels
/// @param y the pen row, in source pixels, as raster_text takes it
/// @param color the colour
/// @return the pen column after the text, in source pixels
int32_t draw_text(
    Surface& surface,
    const Placement& placement,
    const oa::formats::fnt::Font& font,
    std::string_view text,
    int32_t x,
    int32_t y,
    Rgb color
);

/// Draws text in one colour, each glyph pixel blended over the surface by
/// the share of the colour the font's ink gives its palette index.
///
/// Bytes below 0x20 and bytes without a glyph are skipped, and a space
/// advances without drawing, as oa::formats::fnt::raster_text draws them.
///
/// @param[in,out] surface the surface
/// @param placement where source pixels land
/// @param font the font and its ink
/// @param text the text, one byte a glyph
/// @param x the pen column, in source pixels
/// @param y the pen row, in source pixels, as raster_text takes it
/// @param color the colour
/// @return the pen column after the text, in source pixels
int32_t draw_text(
    Surface& surface,
    const Placement& placement,
    const TextFont& font,
    std::string_view text,
    int32_t x,
    int32_t y,
    Rgb color
);

/// Returns the width of a text in a font.
///
/// @param font the font
/// @param text the text, one byte a glyph
/// @return source pixels, as oa::formats::fnt::measure_text measures them
[[nodiscard]] int32_t
text_width(const oa::formats::fnt::Font& font, std::string_view text) noexcept;

/// Returns the width of a text in a readied font.
///
/// @param font the font
/// @param text the text, one byte a glyph
/// @return source pixels, as oa::formats::fnt::measure_text measures them
[[nodiscard]] int32_t text_width(const TextFont& font, std::string_view text) noexcept;

/// Draws a one-bit mark in one colour: only the mark's set pixels are written.
///
/// A mark whose bits are fewer than its width times its height draws nothing.
///
/// @param[in,out] surface the surface
/// @param placement where source pixels land
/// @param mark the mark
/// @param x the mark's left column, in source pixels
/// @param y the mark's top row, in source pixels
/// @param color the colour
void draw_mark(
    Surface& surface, const Placement& placement, const Mark& mark, int32_t x, int32_t y, Rgb color
) noexcept;

} // namespace oa::ui::frontend_renderer
