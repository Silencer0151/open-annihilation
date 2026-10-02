// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "oa/ui/gui_input/gadget_panel.hpp"

#include <cstdint>
#include <cstring>

namespace oa::ui::gui_input::detail {

using ui::gui_layout::GadgetRecord;
namespace field = ui::gui_layout::field;
namespace gadget_type = ui::gui_layout::gadget_type;
namespace attribute = ui::gui_layout::attribute;

/// Returns a record of the panel's top owner.
///
/// @param[in,out] panel the panel; must have an owner
/// @param index the record index, within the owner's table
/// @return the record
[[nodiscard]] inline GadgetRecord& record_at(GadgetPanel& panel, int32_t index) {
    return panel.owner->table.records[static_cast<std::size_t>(index)];
}

/// Tells whether an index names a record of the panel's top owner.
///
/// @param panel the panel
/// @param index the record index
/// @return false without an owner or for an index outside its table
[[nodiscard]] inline bool valid_index(const GadgetPanel& panel, int32_t index) {
    return panel.owner && index >= 0 &&
           static_cast<std::size_t>(index) < panel.owner->table.records.size();
}

/// Returns the exclusive end of the `1 .. last` record scans.
///
/// The root's record count bounds the scan, and so does the table's storage.
///
/// @param panel the panel; must have an owner
/// @return one past the last record a scan reads
[[nodiscard]] inline int32_t scan_end(const GadgetPanel& panel) {
    const auto& records = panel.owner->table.records;
    const auto end =
        static_cast<int32_t>(ui::gui_layout::record_i16(records[0], field::record_count)) + 1;
    return end < static_cast<int32_t>(records.size()) ? end : static_cast<int32_t>(records.size());
}

/// Reads a signed 16-bit field of a record.
///
/// @param record the record
/// @param offset the field's byte position (a gui_layout::field constant)
/// @return the field's value
[[nodiscard]] inline int16_t i16(const GadgetRecord& record, std::size_t offset) {
    return ui::gui_layout::record_i16(record, offset);
}

/// Reads a signed 32-bit field of a record.
///
/// @param record the record
/// @param offset the field's byte position (a gui_layout::field constant)
/// @return the field's value
[[nodiscard]] inline int32_t i32(const GadgetRecord& record, std::size_t offset) {
    return ui::gui_layout::record_i32(record, offset);
}

/// Returns a record's attribute bits.
///
/// @param record the record
/// @return the gui_layout::attribute bits
[[nodiscard]] inline uint32_t attributes(const GadgetRecord& record) {
    return ui::gui_layout::gadget_attributes(record);
}

/// Writes a 16-bit field of a record, keeping the low 16 bits of the value.
///
/// @param[in,out] record the record
/// @param offset the field's byte position (a gui_layout::field constant)
/// @param value the value
inline void set_i16(GadgetRecord& record, std::size_t offset, int32_t value) {
    ui::gui_layout::set_record_i16(record, offset, static_cast<int16_t>(value));
}

/// Writes a 32-bit field of a record.
///
/// @param[in,out] record the record
/// @param offset the field's byte position (a gui_layout::field constant)
/// @param value the value
inline void set_i32(GadgetRecord& record, std::size_t offset, int32_t value) {
    ui::gui_layout::set_record_i32(record, offset, value);
}

/// Returns the characters of a record's text field.
///
/// @param[in,out] record the record
/// @param offset the field's byte position; the record's text by default
/// @return the first character, writable in place
[[nodiscard]] inline char* text_of(GadgetRecord& record, std::size_t offset = field::text) {
    return ui::gui_layout::record_chars(record, offset);
}

/// Returns the length of a record's text field, bounded by the record's end.
///
/// @param record the record
/// @param offset the field's byte position
/// @return the characters before the terminating NUL or the record's end
[[nodiscard]] inline std::size_t text_length(const GadgetRecord& record, std::size_t offset) {
    return ui::gui_layout::record_string(record, offset).size();
}

/// Asks for the panel's top owner to be redrawn; a panel without an owner is left alone.
///
/// @param[in,out] panel the panel
inline void owner_redraw(GadgetPanel& panel) {
    if (panel.owner)
        panel.owner->redraw = 1;
}

/// Returns the host's current tick.
///
/// @param panel the panel
/// @return the tick, or 0 without a tick service
[[nodiscard]] inline uint32_t current_tick(GadgetPanel& panel) {
    return panel.host.current_tick ? panel.host.current_tick(panel.host.context) : 0;
}

/// Takes the next key from the host's key queue.
///
/// @param panel the panel
/// @return the key code, or 0 when none is queued or there is no key service
[[nodiscard]] inline int32_t pop_key(GadgetPanel& panel) {
    return panel.host.pop_key ? panel.host.pop_key(panel.host.context) : 0;
}

/// Returns the next key of the host's key queue and leaves it queued.
///
/// @param panel the panel
/// @return the key code, or 0 when none is queued or there is no key service
[[nodiscard]] inline int32_t peek_key(GadgetPanel& panel) {
    return panel.host.peek_key ? panel.host.peek_key(panel.host.context) : 0;
}

/// Empties the host's key queue, when the host has one.
///
/// @param panel the panel
inline void clear_keys(GadgetPanel& panel) {
    if (panel.host.clear_keys)
        panel.host.clear_keys(panel.host.context);
}

/// Tells whether the host reports a key held down.
///
/// @param panel the panel
/// @param code the key code
/// @return false without a key service
[[nodiscard]] inline bool key_down(GadgetPanel& panel, int32_t code) {
    return panel.host.is_key_down && panel.host.is_key_down(panel.host.context, code);
}

/// Translates a text through the host's translation table.
///
/// @param panel the panel
/// @param text the text in the game's wording
/// @return the host's replacement, or the text itself without one
[[nodiscard]] inline const char* translate(GadgetPanel& panel, const char* text) {
    if (!panel.host.translate)
        return text;
    const char* replacement = panel.host.translate(panel.host.context, text);
    return replacement ? replacement : text;
}

/// Makes a FNT font the host's active font, when the host selects fonts.
///
/// @param panel the panel
/// @param font the font
inline void select_font(GadgetPanel& panel, const void* font) {
    if (panel.host.select_font)
        panel.host.select_font(panel.host.context, font);
}

/// Measures a text in the panel's active font through the host.
///
/// @param panel the panel
/// @param text the text, or null
/// @return the width in pixels; 0 for a null text or without a measuring service
[[nodiscard]] inline int32_t text_width(GadgetPanel& panel, const char* text) {
    if (text == nullptr || !panel.host.text_width)
        return 0;
    return panel.host.text_width(panel.host.context, panel.active_gaf_font, text);
}

/// Returns the line height of the panel's active font through the host.
///
/// @param panel the panel
/// @return the height in pixels; 0 without a measuring service
[[nodiscard]] inline int32_t line_height(GadgetPanel& panel) {
    return panel.host.line_height
               ? panel.host.line_height(panel.host.context, panel.active_gaf_font)
               : 0;
}

/// Asks the host to run one gadget draw, when the host draws.
///
/// @param[in,out] panel the panel
/// @param what the draw
/// @param index the record drawn
inline void draw(GadgetPanel& panel, GadgetDraw what, int32_t index) {
    if (panel.host.draw)
        panel.host.draw(panel.host.context, panel, what, index);
}

/// Asks the host for a whole-panel draw.
///
/// @param[in,out] panel the panel
/// @param flags panel_flag bits of the draw
/// @return the draw's success value: 1 when drawn, and 1 without a draw service
inline int32_t draw_panel(GadgetPanel& panel, uint32_t flags) {
    return panel.host.draw_panel ? panel.host.draw_panel(panel.host.context, panel, flags) : 1;
}

/// Lower-cases A-Z only, as in 3.1c; every other value, a negative byte value included, is kept.
[[nodiscard]] inline int32_t lower(int32_t c) {
    return (c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c;
}

/// Upper-cases a-z only, as in 3.1c; every other value, a negative byte value included, is kept.
[[nodiscard]] inline int32_t upper(int32_t c) {
    return (c >= 'a' && c <= 'z') ? c - 'a' + 'A' : c;
}

/// Reads a byte as a signed value.
///
/// @param value the byte
/// @return the value in -128..127
[[nodiscard]] inline int32_t signed_byte(uint8_t value) {
    return static_cast<int8_t>(value);
}

// Pointer position relative to the root record's origin.
struct LocalPointer {
    int32_t x{};
    int32_t y{};
};

/// Returns the pointer position relative to the root record's origin.
///
/// @param[in,out] panel the panel; must have an owner
/// @return the position in pixels
[[nodiscard]] inline LocalPointer local_pointer(GadgetPanel& panel) {
    const auto& root = record_at(panel, 0);
    return {panel.pointer.x - i16(root, field::x), panel.pointer.y - i16(root, field::y)};
}

} // namespace oa::ui::gui_input::detail
