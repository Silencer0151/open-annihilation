// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Package-private helpers shared by the gadget draws.
#pragma once

#include "oa/ui/gadget_render.hpp"
#include "oa/present/blit.hpp"
#include "oa/present/display.hpp"
#include "oa/present/raster.hpp"

#include <cstddef>
#include <cstdint>

namespace oa::ui::gadget_render::detail {

namespace layout = oa::ui::gui_layout;
namespace field = oa::ui::gui_layout::field;
namespace gadget_type = oa::ui::gui_layout::gadget_type;
namespace attribute = oa::ui::gui_layout::attribute;

using layout::GadgetRecord;

// Rows of a list line table end with NUL or LF.
inline constexpr char kLineFeed = '\n';
// Colour-coded list rows start with '&'; "&G" marks a header row.
inline constexpr char kRowCode = '&';
inline constexpr char kHeaderCode = 'G';
inline constexpr int32_t kRowCodeBytes = 2;
// Glyph whose height sets a GAF font's line height.
inline constexpr int32_t kReferenceGlyph = 'I';
inline constexpr int32_t kLineGap = 2;
// The smallest 'I' of a GAF font whose missing characters are drawn in the
// modern message face: hattfont12's; smaller fonts take the status face.
inline constexpr int32_t kMessageFaceHeight = 12;
inline constexpr int32_t kFirstPrintable = 0x20;
inline constexpr int32_t kNoLimit = -1;
inline constexpr const char* kGafExtension = "GAF";

/// Returns a record of the panel's top owner.
///
/// @param[in,out] panel the panel
/// @param index the record index, within the owner's table
/// @return the record
[[nodiscard]] inline GadgetRecord& record_at(GadgetPanel& panel, int32_t index) {
    return panel.owner->table.records[static_cast<size_t>(index)];
}

/// Returns the root record of the panel's top owner.
///
/// @param[in,out] panel the panel
/// @return the root record
[[nodiscard]] inline GadgetRecord& root_of(GadgetPanel& panel) {
    return panel.owner->table.records[0];
}

/// Returns the surface the panel's face is drawn on.
///
/// @param[in,out] panel the panel
/// @return the root record's surface, or null before the first draw makes it
[[nodiscard]] inline Surface* face_of(GadgetPanel& panel) {
    return static_cast<Surface*>(root_of(panel).refs.surface);
}

/// Reads a signed 16-bit field of a record.
///
/// @param record the record
/// @param offset the field's byte position (a gui_layout::field constant)
/// @return the field's value
[[nodiscard]] inline int16_t i16(const GadgetRecord& record, size_t offset) {
    return layout::record_i16(record, offset);
}

/// Reads a signed 32-bit field of a record.
///
/// @param record the record
/// @param offset the field's byte position (a gui_layout::field constant)
/// @return the field's value
[[nodiscard]] inline int32_t i32(const GadgetRecord& record, size_t offset) {
    return layout::record_i32(record, offset);
}

/// Returns a record's attribute bits.
///
/// @param record the record
/// @return the gui_layout::attribute bits
[[nodiscard]] inline uint32_t attributes(const GadgetRecord& record) {
    return layout::gadget_attributes(record);
}

/// Converts a gadget rectangle into a raster rectangle with the same edges.
///
/// @param rect the gadget rectangle
/// @return the rectangle
[[nodiscard]] inline Rect32 to_rect(const layout::GadgetRect& rect) {
    return {rect.left, rect.top, rect.right, rect.bottom};
}

/// Returns a record's rectangle relative to the panel's root.
///
/// @param record the record
/// @return the rectangle
[[nodiscard]] inline Rect32 record_rect(const GadgetRecord& record) {
    return to_rect(layout::panel_relative_rect(record));
}

/// Returns a colour map entry of the panel.
///
/// @param panel the panel
/// @param slot the entry (a color_slot constant)
/// @return the palette index, or 0 for a slot past the map
[[nodiscard]] inline uint8_t mapped_color(const GadgetPanel& panel, uint32_t slot) {
    return slot < panel.colors.size() ? panel.colors[slot] : 0;
}

/// Returns the rows from a GAF font's pen down to its baseline.
///
/// @param renderer the renderer whose art table reads the font
/// @param glyphs the font's glyph sequence, or null
/// @return the height of its 'I' glyph, zero without one
[[nodiscard]] int32_t font_baseline(const GadgetRenderer& renderer, const void* glyphs);

/// Measures a text in an explicit GAF font, or in the display's FNT font.
///
/// A GAF text is as wide as the frames of its characters' glyphs; a character
/// without a glyph adds nothing. With game-text hooks installed, text with a
/// byte from 0x80 up is measured as draw_text draws it: a run of characters
/// the font lacks is as wide as the modern fonts draw it.
///
/// @param renderer the renderer whose art table reads the font
/// @param gaf_font the GAF font, or null for the display's active FNT font
/// @param text the text, or null
/// @return the width in pixels; 0 for a null text
[[nodiscard]] int32_t
measure_with_font(const GadgetRenderer& renderer, const void* gaf_font, const char* text);

/// Returns the line height of an explicit GAF font, or of the display's FNT font.
///
/// A GAF line is as high as the font's 'I' glyph plus two pixels; a font
/// without that glyph counts it as zero high.
///
/// @param renderer the renderer whose art table reads the font
/// @param gaf_font the GAF font, or null for the display's active FNT font
/// @return the height in pixels
[[nodiscard]] int32_t line_height_with_font(const GadgetRenderer& renderer, const void* gaf_font);

/// Returns the frame of one image-list row.
///
/// An image list holds either a table of frames (attribute::image_records) or
/// a table the art table reads frames from.
///
/// @param renderer the renderer whose art table reads the images
/// @param list the image-list record
/// @param item the row
/// @return the frame, or null for a list without images or an art table that cannot read them
[[nodiscard]] const Sprite*
list_image(const GadgetRenderer& renderer, const GadgetRecord& list, int32_t item);

/// Returns one frame of an art sequence through the renderer's art table.
///
/// @param renderer the renderer whose art table reads the sequence
/// @param sequence the sequence, or null
/// @param index the frame index
/// @return the frame, or null for a null sequence or an art table that cannot read frames
[[nodiscard]] Sprite*
art_frame(const GadgetRenderer& renderer, const void* sequence, int32_t index);

/// Returns how many frames an art sequence holds.
///
/// @param renderer the renderer whose art table reads the sequence
/// @param sequence the sequence, or null
/// @return the frame count; 0 for a null sequence or an art table that cannot count
[[nodiscard]] int32_t art_frame_count(const GadgetRenderer& renderer, const void* sequence);

/// Finds a named sequence in an art file.
///
/// @param renderer the renderer whose art table searches the file
/// @param file the art file, or null
/// @param name the sequence name
/// @return the sequence, or null when it or the file is missing
[[nodiscard]] const void*
art_find(const GadgetRenderer& renderer, const void* file, const char* name);

/// Returns the glyph sequence of the panel's active GAF font.
///
/// @param renderer the renderer whose art table reads the font
/// @param panel the panel
/// @return the glyphs, or null when the panel draws with the FNT font
[[nodiscard]] const void* active_glyphs(const GadgetRenderer& renderer, const GadgetPanel& panel);

/// Moves every frame's origin of a sequence to (0,0), as the first draw does with the art it binds.
///
/// @param renderer the renderer whose art table reads the sequence
/// @param sequence the sequence, or null for none
void clear_origins(const GadgetRenderer& renderer, const void* sequence);

/// Fills a rectangle clipped to a surface or to the display, the fill every gadget draw uses.
///
/// Without a target the display surface is locked for the fill and unlocked
/// after it; nothing is drawn when it cannot be locked.
///
/// @param target the surface, or null for the display
/// @param rect the rectangle
/// @param color the palette index
void fill_clipped(Surface* target, const Rect32& rect, uint8_t color);

/// Draws a text in the display's FNT font through the renderer's text hook, in the renderer's text colour.
///
/// Nothing is drawn without a text hook.
///
/// @param[in,out] renderer the renderer
/// @param target the surface, or null for the display
/// @param text the text
/// @param x left edge in pixels
/// @param y top edge in pixels
/// @param max_width the width the text is cut to, or kNoLimit
void fnt_text(
    GadgetRenderer& renderer,
    Surface* target,
    const char* text,
    int32_t x,
    int32_t y,
    int32_t max_width
);

/// Finds the whole-panel draw's record surfaces for an owner, or takes a free slot for it.
///
/// @param[in,out] renderer the renderer holding the slots
/// @param owner the record owner
/// @param create whether to take a free slot when the owner has none
/// @return the owner's slot, or null when it has none and none is taken
[[nodiscard]] PanelSurfaces*
panel_slot(GadgetRenderer& renderer, const GadgetOwner* owner, bool create);

} // namespace oa::ui::gadget_render::detail
