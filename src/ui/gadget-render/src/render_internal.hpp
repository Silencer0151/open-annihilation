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
inline constexpr int32_t kFirstPrintable = 0x20;
inline constexpr int32_t kNoLimit = -1;
inline constexpr const char* kGafExtension = "GAF";

[[nodiscard]] inline GadgetRecord& record_at(GadgetPanel& panel, int32_t index) {
    return panel.owner->table.records[static_cast<size_t>(index)];
}

[[nodiscard]] inline GadgetRecord& root_of(GadgetPanel& panel) {
    return panel.owner->table.records[0];
}

[[nodiscard]] inline Surface* face_of(GadgetPanel& panel) {
    return static_cast<Surface*>(root_of(panel).refs.surface);
}

[[nodiscard]] inline int16_t i16(const GadgetRecord& record, size_t offset) {
    return layout::record_i16(record, offset);
}

[[nodiscard]] inline int32_t i32(const GadgetRecord& record, size_t offset) {
    return layout::record_i32(record, offset);
}

[[nodiscard]] inline uint32_t attributes(const GadgetRecord& record) {
    return layout::gadget_attributes(record);
}

[[nodiscard]] inline Rect32 to_rect(const layout::GadgetRect& rect) {
    return {rect.left, rect.top, rect.right, rect.bottom};
}

[[nodiscard]] inline Rect32 record_rect(const GadgetRecord& record) {
    return to_rect(layout::panel_relative_rect(record));
}

// Colour map entry `slot`; a slot past the map yields 0.
[[nodiscard]] inline uint8_t mapped_color(const GadgetPanel& panel, uint32_t slot) {
    return slot < panel.colors.size() ? panel.colors[slot] : 0;
}

// Text width and line height for an explicit GAF font (null: the display FNT font).
[[nodiscard]] int32_t
measure_with_font(const GadgetRenderer& renderer, const void* gaf_font, const char* text);
[[nodiscard]] int32_t line_height_with_font(const GadgetRenderer& renderer, const void* gaf_font);
// Frame of image-list row `item`.
[[nodiscard]] const Sprite*
list_image(const GadgetRenderer& renderer, const GadgetRecord& list, int32_t item);

[[nodiscard]] Sprite*
art_frame(const GadgetRenderer& renderer, const void* sequence, int32_t index);
[[nodiscard]] int32_t art_frame_count(const GadgetRenderer& renderer, const void* sequence);
[[nodiscard]] const void*
art_find(const GadgetRenderer& renderer, const void* file, const char* name);
// Glyph sequence of the panel's active GAF font.
[[nodiscard]] const void* active_glyphs(const GadgetRenderer& renderer, const GadgetPanel& panel);
// Moves every frame's origin to (0,0), as the first draw does with the art it binds.
void clear_origins(const GadgetRenderer& renderer, const void* sequence);

// Clipped rectangle fill onto a surface or the locked display (the fill every
// gadget draw uses).
void fill_clipped(Surface* target, const Rect32& rect, uint8_t color);
void fnt_text(
    GadgetRenderer& renderer,
    Surface* target,
    const char* text,
    int32_t x,
    int32_t y,
    int32_t max_width
);

// The whole-panel draw's record surfaces, found or made for `owner`.
[[nodiscard]] PanelSurfaces*
panel_slot(GadgetRenderer& renderer, const GadgetOwner* owner, bool create);

} // namespace oa::ui::gadget_render::detail
