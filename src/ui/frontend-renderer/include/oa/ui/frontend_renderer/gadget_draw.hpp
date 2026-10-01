// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// 8-bit bevel, frame and marker primitives of the GUI gadget engine, drawn on
// the canonical presentation surface. A null target draws on the locked
// display surface, as the game does.

#include "oa/ui/gui_layout/gui_gadget.hpp"
#include "oa/present/gaf_sprites.hpp"
#include "oa/present/surface.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace oa::ui::frontend_renderer {

/// Draws a two-pixel raised bevel.
///
/// @param[in,out] target Surface to draw on; null draws on the locked display.
/// @param rect Inclusive rectangle.
/// @param top_left Palette index of the top and left edges.
/// @param bottom_right Palette index of the bottom and right edges.
void draw_bevel_raised(
    oa::Surface* target, const oa::Rect32& rect, uint8_t top_left, uint8_t bottom_right
);

/// Draws a two-pixel sunken bevel: the raised bevel with the colour arguments swapped.
///
/// @param[in,out] target Surface to draw on; null draws on the locked display.
/// @param rect Inclusive rectangle.
/// @param bottom_right Palette index of the bottom and right edges.
/// @param top_left Palette index of the top and left edges.
void draw_bevel_sunken(
    oa::Surface* target, const oa::Rect32& rect, uint8_t bottom_right, uint8_t top_left
);

/// Fills a rectangle and draws a two-pixel raised bevel over it.
///
/// @param[in,out] target Surface to draw on; null draws on the locked display.
/// @param rect Inclusive rectangle.
/// @param top_left Palette index of the top and left edges.
/// @param bottom_right Palette index of the bottom and right edges.
/// @param fill Palette index of the interior.
void fill_box_raised(
    oa::Surface* target,
    const oa::Rect32& rect,
    uint8_t top_left,
    uint8_t bottom_right,
    uint8_t fill
);

/// Fills a rectangle and draws a two-pixel sunken bevel over it.
///
/// @param[in,out] target Surface to draw on; null draws on the locked display.
/// @param rect Inclusive rectangle.
/// @param bottom_right Palette index of the bottom and right edges.
/// @param top_left Palette index of the top and left edges.
/// @param fill Palette index of the interior.
void fill_box_sunken(
    oa::Surface* target,
    const oa::Rect32& rect,
    uint8_t bottom_right,
    uint8_t top_left,
    uint8_t fill
);

/// Fills a rectangle and draws a one-pixel raised frame over it.
///
/// @param[in,out] target Surface to draw on; null draws on the locked display.
/// @param rect Inclusive rectangle.
/// @param top_left Palette index of the top and left edges.
/// @param bottom_right Palette index of the bottom and right edges.
/// @param fill Palette index of the interior.
void fill_frame_raised(
    oa::Surface* target,
    const oa::Rect32& rect,
    uint8_t top_left,
    uint8_t bottom_right,
    uint8_t fill
);

/// Fills a rectangle and draws a one-pixel sunken frame over it.
///
/// @param[in,out] target Surface to draw on; null draws on the locked display.
/// @param rect Inclusive rectangle.
/// @param bottom_right Palette index of the bottom and right edges.
/// @param top_left Palette index of the top and left edges.
/// @param fill Palette index of the interior.
void fill_frame_sunken(
    oa::Surface* target,
    const oa::Rect32& rect,
    uint8_t bottom_right,
    uint8_t top_left,
    uint8_t fill
);

/// Number of rectangles the focus marker's outline lights round a record.
inline constexpr std::size_t focus_ring_count = 6;

/// One rectangle of the focus marker's outline and the light level its
/// edges are lit through.
struct FocusRing {
    oa::Rect32 rect{}; ///< inclusive; each ring one pixel wider on every side
    int32_t level = 0; ///< light table row
};

/// Returns the rectangles the focus marker's outline lights round a record.
///
/// The first ring lies one pixel outside the record and each one after it a
/// pixel further out; they are lit through light levels 31, 28, 24, 19, 13
/// and 6, brightest nearest the record. Each ring's four edges are lit one
/// after another (top, right, bottom, left), so its corners are lit twice.
///
/// @param rect Record rectangle, inclusive.
/// @return The rings, innermost first.
[[nodiscard]] std::array<FocusRing, focus_ring_count>
focus_rings(const ui::gui_layout::GadgetRect& rect) noexcept;

/// Draws the outline part of the focus marker around a record.
///
/// Lights the focus_rings() of the record. The state half is
/// ui::gui_input::mark_text_box_focus().
///
/// @param[in,out] target Surface to draw on; null draws on the locked display.
/// @param rect Record rectangle, inclusive.
void draw_focus_outline(oa::Surface* target, const ui::gui_layout::GadgetRect& rect);

/// Returns where a centred caption underlines its quick key.
///
/// A caption drawn centred on its button, unless the button is grayed,
/// underlines the first character that is the quick key itself, in the same
/// case; a caption without one and a key of 0 underline nothing.
///
/// @param caption The caption as drawn.
/// @param key The button's quick key.
/// @return The offset of the underlined character, or std::string_view::npos.
[[nodiscard]] std::size_t quick_key_offset(std::string_view caption, char key) noexcept;

/// Draws one edge line of a value marker.
///
/// Draws the top edge (attributes & 1), the left edge (& 2) or the full
/// diagonal extent (& 4).
///
/// @param[in,out] target Surface to draw on; null draws on the locked display.
/// @param rect Record rectangle, inclusive.
/// @param attributes Record attribute word selecting the edge.
/// @param color Remapped colorf palette index.
/// @param flags Nothing is drawn unless bit 0 is set.
void draw_value_marker(
    oa::Surface* target,
    const ui::gui_layout::GadgetRect& rect,
    uint32_t attributes,
    uint8_t color,
    uint8_t flags
);

/// Loads a GUI font from its GAF file.
///
/// Relocates the GAF and moves every glyph's hotspot up by the height of
/// glyph 'I', so text hangs from the pen instead of sitting on it; without an
/// 'I' glyph the hotspots are left unchanged. The file comes from the caller
/// instead of the context's font directory, and the caller keeps the font
/// instead of the context's slots.
///
/// @param file Whole GAF file.
/// @param[out] font Relocated glyph sequences.
/// @return The relocation status.
[[nodiscard]] oa::present::GafStatus
load_gui_font(std::span<const uint8_t> file, oa::present::GafSprites& font);

// draw_gadget_text width that never stops the text.
inline constexpr int32_t gadget_text_unbounded = -1;

/// Draws text glyph by glyph from a GAF font, or through the active FNT font when there is none.
///
/// Frame c of the font's first sequence is the glyph of byte c. Bytes below
/// 0x20 and missing glyphs are skipped and a space advances without drawing.
///
/// @param[in,out] target Surface to draw on; null draws on the locked display.
/// @param font The GUI context's active GAF font (GadgetPanel::active_gaf_font);
///        null uses the FNT font.
/// @param text Text to draw.
/// @param x Pen x in target pixels.
/// @param y Pen y in target pixels.
/// @param max_width Width limit in pixels; other than gadget_text_unbounded,
///        drawing stops before a glyph wider than what is left.
/// @param light_level Nonzero draws glyphs through that light-table row.
void draw_gadget_text(
    oa::Surface* target,
    const oa::present::GafSprites* font,
    const char* text,
    int32_t x,
    int32_t y,
    int32_t max_width,
    int32_t light_level
);

} // namespace oa::ui::frontend_renderer
