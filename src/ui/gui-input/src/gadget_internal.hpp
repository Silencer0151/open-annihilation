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

[[nodiscard]] inline GadgetRecord& record_at(GadgetPanel& panel, int32_t index) {
    return panel.owner->table.records[static_cast<std::size_t>(index)];
}

[[nodiscard]] inline bool valid_index(const GadgetPanel& panel, int32_t index) {
    return panel.owner && index >= 0 &&
           static_cast<std::size_t>(index) < panel.owner->table.records.size();
}

// Exclusive end of the `1 .. last` record scans, bounded by the storage.
[[nodiscard]] inline int32_t scan_end(const GadgetPanel& panel) {
    const auto& records = panel.owner->table.records;
    const auto end =
        static_cast<int32_t>(ui::gui_layout::record_i16(records[0], field::record_count)) + 1;
    return end < static_cast<int32_t>(records.size()) ? end : static_cast<int32_t>(records.size());
}

[[nodiscard]] inline int16_t i16(const GadgetRecord& record, std::size_t offset) {
    return ui::gui_layout::record_i16(record, offset);
}

[[nodiscard]] inline int32_t i32(const GadgetRecord& record, std::size_t offset) {
    return ui::gui_layout::record_i32(record, offset);
}

[[nodiscard]] inline uint32_t attributes(const GadgetRecord& record) {
    return ui::gui_layout::gadget_attributes(record);
}

inline void set_i16(GadgetRecord& record, std::size_t offset, int32_t value) {
    ui::gui_layout::set_record_i16(record, offset, static_cast<int16_t>(value));
}

inline void set_i32(GadgetRecord& record, std::size_t offset, int32_t value) {
    ui::gui_layout::set_record_i32(record, offset, value);
}

[[nodiscard]] inline char* text_of(GadgetRecord& record, std::size_t offset = field::text) {
    return ui::gui_layout::record_chars(record, offset);
}

// strlen bounded by the record end.
[[nodiscard]] inline std::size_t text_length(const GadgetRecord& record, std::size_t offset) {
    return ui::gui_layout::record_string(record, offset).size();
}

inline void owner_redraw(GadgetPanel& panel) {
    if (panel.owner)
        panel.owner->redraw = 1;
}

[[nodiscard]] inline uint32_t current_tick(GadgetPanel& panel) {
    return panel.host.current_tick ? panel.host.current_tick(panel.host.context) : 0;
}

[[nodiscard]] inline int32_t pop_key(GadgetPanel& panel) {
    return panel.host.pop_key ? panel.host.pop_key(panel.host.context) : 0;
}

[[nodiscard]] inline int32_t peek_key(GadgetPanel& panel) {
    return panel.host.peek_key ? panel.host.peek_key(panel.host.context) : 0;
}

inline void clear_keys(GadgetPanel& panel) {
    if (panel.host.clear_keys)
        panel.host.clear_keys(panel.host.context);
}

[[nodiscard]] inline bool key_down(GadgetPanel& panel, int32_t code) {
    return panel.host.is_key_down && panel.host.is_key_down(panel.host.context, code);
}

[[nodiscard]] inline const char* translate(GadgetPanel& panel, const char* text) {
    if (!panel.host.translate)
        return text;
    const char* replacement = panel.host.translate(panel.host.context, text);
    return replacement ? replacement : text;
}

inline void select_font(GadgetPanel& panel, const void* font) {
    if (panel.host.select_font)
        panel.host.select_font(panel.host.context, font);
}

[[nodiscard]] inline int32_t text_width(GadgetPanel& panel, const char* text) {
    if (text == nullptr || !panel.host.text_width)
        return 0;
    return panel.host.text_width(panel.host.context, panel.active_gaf_font, text);
}

[[nodiscard]] inline int32_t line_height(GadgetPanel& panel) {
    return panel.host.line_height
               ? panel.host.line_height(panel.host.context, panel.active_gaf_font)
               : 0;
}

inline void draw(GadgetPanel& panel, GadgetDraw what, int32_t index) {
    if (panel.host.draw)
        panel.host.draw(panel.host.context, panel, what, index);
}

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

[[nodiscard]] inline int32_t signed_byte(uint8_t value) {
    return static_cast<int8_t>(value);
}

// Pointer position relative to the root record's origin.
struct LocalPointer {
    int32_t x;
    int32_t y;
};

[[nodiscard]] inline LocalPointer local_pointer(GadgetPanel& panel) {
    const auto& root = record_at(panel, 0);
    return {panel.pointer.x - i16(root, field::x), panel.pointer.y - i16(root, field::y)};
}

} // namespace oa::ui::gui_input::detail
