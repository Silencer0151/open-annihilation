// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/ui/gui_layout/gui_gadget.hpp"

#include <algorithm>
#include <cstdint>

namespace oa::ui::gui_layout {
namespace {

// strncmp(a, b, count) == 0 where `a` is record storage and `b` ends at its size.
[[nodiscard]] bool bounded_equal(
    const GadgetRecord& record, std::size_t offset, std::string_view b, std::size_t count
) {
    for (std::size_t index = 0; index < count; ++index) {
        const char a_char = offset + index < kGadgetRecordBytes
                                ? static_cast<char>(record.bytes[offset + index])
                                : '\0';
        const char b_char = index < b.size() ? b[index] : '\0';
        if (a_char != b_char)
            return false;
        if (a_char == '\0')
            return true;
    }
    return true;
}

// Records visited by the `1 < last + 1` scans, bounded by the storage size.
[[nodiscard]] int32_t scan_end(std::span<const GadgetRecord> table) {
    const auto last = static_cast<int32_t>(gadget_last_index(table)) + 1;
    return std::min(last, static_cast<int32_t>(table.size()));
}

/// Copies the references whose record slots lie within the first `bytes` bytes.
void copy_refs_within(GadgetRefs& target, const GadgetRefs& source, std::size_t bytes) {
    const auto covers = [bytes](std::size_t offset) { return offset + 4 <= bytes; };
    if (covers(field::button_sprite))
        target.sprite = source.sprite;
    if (covers(field::hot_callback))
        target.hot_callback = source.hot_callback;
    if (covers(field::image))
        target.image = source.image;
    if (covers(field::list_lines)) {
        target.lines = source.lines;
        target.lines_size = source.lines_size;
    }
    if (covers(field::list_images))
        target.images = source.images;
    if (covers(field::list_changed))
        target.list_changed = source.list_changed;
    if (covers(field::list_item_flags)) {
        target.item_flags = source.item_flags;
        target.font = source.font;
    }
    if (covers(field::scroll_value))
        target.scroll_value = source.scroll_value;
}

[[nodiscard]] GadgetRecord* next_append_slot(std::span<GadgetRecord> table) {
    if (table.empty())
        return nullptr;
    const auto count = record_i16(table[0], field::record_count);
    // Refused at exactly 200; the storage bound also keeps index 200
    // (one past the allocation) from being written.
    if (count == static_cast<int16_t>(kGadgetCapacity) || count < 0 ||
        static_cast<std::size_t>(count) + 1 >= table.size()) {
        return nullptr;
    }
    const auto index = static_cast<int16_t>(count + 1);
    set_record_i16(table[0], field::record_count, index);
    return &table[static_cast<std::size_t>(index)];
}

bool append_copy(
    std::span<GadgetRecord> table, const GadgetRecord& source, std::size_t bytes, uint8_t type
) {
    auto* record = next_append_slot(table);
    if (record == nullptr)
        return false;
    std::copy_n(source.bytes.begin(), bytes, record->bytes.begin());
    copy_refs_within(record->refs, source.refs, bytes);
    record->bytes[field::type] = type;
    return true;
}

} // namespace

std::string_view record_string(const GadgetRecord& record, std::size_t offset) {
    if (offset >= kGadgetRecordBytes)
        return {};
    const auto* begin = record_chars(record, offset);
    const auto available = kGadgetRecordBytes - offset;
    const auto* end = std::find(begin, begin + available, '\0');
    return {begin, static_cast<std::size_t>(end - begin)};
}

void set_record_string(
    GadgetRecord& record, std::size_t offset, std::string_view value, std::size_t capacity
) {
    if (offset >= kGadgetRecordBytes)
        return;
    capacity = std::min(capacity, kGadgetRecordBytes - offset);
    const auto length = std::min(value.size(), capacity);
    std::copy_n(value.begin(), length, record.bytes.begin() + static_cast<std::ptrdiff_t>(offset));
    std::fill(
        record.bytes.begin() + static_cast<std::ptrdiff_t>(offset + length),
        record.bytes.begin() + static_cast<std::ptrdiff_t>(offset + capacity),
        uint8_t{0}
    );
}

void copy_record_cstring(GadgetRecord& record, std::size_t offset, std::string_view value) {
    if (offset >= kGadgetRecordBytes)
        return;
    const auto length = std::min(value.size(), kGadgetRecordBytes - offset - 1);
    std::copy_n(value.begin(), length, record.bytes.begin() + static_cast<std::ptrdiff_t>(offset));
    record.bytes[offset + length] = 0;
}

void load_gadget_common(GadgetRecord& record, const CommonFields& common) {
    record.bytes[field::type] = static_cast<uint8_t>(common.type);
    record.bytes[field::group] = common.association;
    set_record_string(record, field::name, common.name, field::name_bytes);
    set_record_i16(record, field::x, common.x);
    set_record_i16(record, field::y, common.y);
    set_record_i16(record, field::width, common.width);
    set_record_i16(record, field::height, common.height);
    set_record_i32(record, field::attributes, common.attributes);
    set_record_u32(record, field::color_foreground, common.foreground_color);
    set_record_u32(record, field::color_background, common.background_color);
    record.bytes[field::texture] = static_cast<uint8_t>(common.texture_number);
    record.bytes[field::font] = static_cast<uint8_t>(common.font_number);
    record.bytes[field::active] = static_cast<uint8_t>(common.active);
    record.bytes[field::common_attributes] = static_cast<uint8_t>(common.common_attributes);
    std::fill_n(record.bytes.begin() + field::help, field::help_bytes, uint8_t{0});
    set_record_string(record, field::help, common.runtime_help, field::help_bytes - 1);
    record.bytes[field::gaf_file] =
        static_cast<uint8_t>((record.bytes[field::gaf_file] & ~1U) | (common.gaf_file ? 1U : 0U));
}

void load_panel_header(GadgetRecord& record, const PanelFields& panel) {
    set_record_i16(record, field::record_count, panel.declared_total_gadgets);
    set_record_string(record, field::panel_name, panel.panel, field::name_bytes);
    set_record_string(
        record, field::enter_default, panel.carriage_return_default, field::name_bytes
    );
    set_record_string(record, field::escape_default, panel.escape_default, field::name_bytes);
    set_record_string(record, field::default_focus, panel.default_focus, field::name_bytes);
    record.bytes[field::version_major] = static_cast<uint8_t>(panel.version.major);
    record.bytes[field::version_minor] = static_cast<uint8_t>(panel.version.minor);
    record.bytes[field::version_revision] = static_cast<uint8_t>(panel.version.revision);
}

void load_button(GadgetRecord& record, const ButtonFields& button) {
    set_record_i16(record, field::button_status, button.status);
    set_record_string(record, field::text, button.text, field::text_bytes);
    record.bytes[field::button_quick_key] = static_cast<uint8_t>(button.quick_key);
    record.bytes[field::button_flags] = static_cast<uint8_t>(
        (record.bytes[field::button_flags] & ~1U) | (button.grayed_out ? 1U : 0U)
    );
    record.bytes[field::button_stages] = static_cast<uint8_t>(button.stages);
}

void load_list_box(GadgetRecord& record, const ListBoxFields& list) {
    set_record_u32(record, field::list_changed, 0);
    set_record_u32(record, field::list_item_flags, 0);
    record.refs.list_changed = nullptr;
    record.refs.item_flags = nullptr;
    record.refs.font = nullptr;
    set_record_i16(record, field::list_item_height, list.item_height);
}

void load_text_box(GadgetRecord& record, const TextBoxFields& text_box) {
    set_record_i16(record, field::text_max_chars, text_box.max_characters);
    copy_record_cstring(record, field::text, text_box.text);
}

void load_scroll_bar(GadgetRecord& record, const ScrollBarFields& scroll) {
    set_record_i16(record, field::scroll_range, scroll.range);
    set_record_i32(record, field::scroll_thickness, scroll.thickness);
    set_record_i16(record, field::scroll_knob, scroll.knob_position);
    set_record_i16(record, field::scroll_knob_size, scroll.knob_size);
    set_record_u32(record, field::scroll_callback, 0);
    record.refs.scroll_changed = nullptr;
    copy_record_cstring(record, field::text, scroll.text);
}

void load_label(GadgetRecord& record, const LabelFields& label) {
    record.bytes[field::label_quick_key] = 0;
    record.bytes[field::label_link] = 0;
    std::fill_n(record.bytes.begin() + field::text, field::text_bytes, uint8_t{0});
    set_record_string(record, field::text, label.text, field::label_text_bytes);
    set_record_string(record, field::label_link, label.link, field::label_link_bytes);
}

void load_hot_surface(GadgetRecord& record, const HotSurfaceFields& hot) {
    const auto flags = record_u32(record, field::hot_flags);
    set_record_u32(record, field::hot_flags, (flags & ~1U) | (hot.hot ? 1U : 0U));
}

std::size_t load_gadget_records(const Layout& layout, std::span<GadgetRecord> records) {
    const auto count = std::min(layout.gadgets.size(), records.size());
    for (std::size_t index = 0; index < count; ++index) {
        const auto& gadget = layout.gadgets[index];
        auto& record = records[index];
        load_gadget_common(record, gadget.common);
        if (const auto* panel = std::get_if<PanelFields>(&gadget.fields)) {
            load_panel_header(record, *panel);
        } else if (const auto* button = std::get_if<ButtonFields>(&gadget.fields)) {
            load_button(record, *button);
        } else if (const auto* list = std::get_if<ListBoxFields>(&gadget.fields)) {
            load_list_box(record, *list);
        } else if (const auto* text_box = std::get_if<TextBoxFields>(&gadget.fields)) {
            load_text_box(record, *text_box);
        } else if (const auto* scroll = std::get_if<ScrollBarFields>(&gadget.fields)) {
            load_scroll_bar(record, *scroll);
        } else if (const auto* label = std::get_if<LabelFields>(&gadget.fields)) {
            load_label(record, *label);
        } else if (const auto* hot = std::get_if<HotSurfaceFields>(&gadget.fields)) {
            load_hot_surface(record, *hot);
        } else if (const auto* file = std::get_if<FileResourceFields>(&gadget.fields)) {
            set_record_string(record, field::filename, file->filename, field::filename_bytes - 1);
        } else if (const auto* null_resource = std::get_if<NullResourceFields>(&gadget.fields)) {
            set_record_i32(record, field::nuttin, null_resource->nuttin);
        }
    }
    if (count != 0)
        set_record_i16(records[0], field::record_count, static_cast<int16_t>(count - 1));
    return count;
}

int32_t find_gadget(std::span<const GadgetRecord> table, std::string_view name) {
    const auto end = scan_end(table);
    for (int32_t index = 1; index < end; ++index) {
        if (bounded_equal(
                table[static_cast<std::size_t>(index)], field::name, name, field::name_bytes
            ))
            return index;
    }
    return kNoGadget;
}

int32_t find_gadget_containing(std::span<const GadgetRecord> table, std::string_view fragment) {
    const auto end = scan_end(table);
    for (int32_t index = 1; index < end; ++index) {
        const auto name = record_string(table[static_cast<std::size_t>(index)], field::name);
        if (name.find(fragment) != std::string_view::npos)
            return index;
    }
    return kNoGadget;
}

void copy_gadget_name(std::span<const GadgetRecord> table, int32_t index, char* output) {
    const auto& record = table[static_cast<std::size_t>(index)];
    std::size_t length = 0;
    while (length < field::name_bytes && record.bytes[field::name + length] != 0) {
        output[length] = static_cast<char>(record.bytes[field::name + length]);
        ++length;
    }
    std::fill(output + length, output + field::name_bytes + 1, '\0');
}

GadgetRecord* find_gadget_record(std::span<GadgetRecord> table, std::string_view name) {
    const auto index = find_gadget(table, name);
    return index == kNoGadget ? nullptr : &table[static_cast<std::size_t>(index)];
}

GadgetRecord*
require_gadget(std::span<GadgetRecord> table, std::string_view name, FatalReporter report_fatal) {
    auto* record = find_gadget_record(table, name);
    if (record == nullptr && report_fatal != nullptr)
        report_fatal("Error in GUI layout");
    return record;
}

bool gadget_name_matches(
    std::span<const GadgetRecord> table, int32_t index, std::string_view name
) {
    if (index == kNoGadget)
        return false;
    return bounded_equal(
        table[static_cast<std::size_t>(index)], field::name, name, field::name_bytes
    );
}

GadgetRect panel_relative_rect(const GadgetRecord& record) {
    GadgetRect rect;
    if (gadget_type_of(record) != gadget_type::panel) {
        rect.left = record_i16(record, field::x);
        rect.top = record_i16(record, field::y);
    }
    rect.right = record_i16(record, field::width) - 1 + rect.left;
    rect.bottom = record_i16(record, field::height) - 1 + rect.top;
    return rect;
}

GadgetRect screen_rect(std::span<const GadgetRecord> table, std::size_t index) {
    const auto& record = table[index];
    GadgetRect rect;
    rect.left = record_i16(record, field::x);
    rect.top = record_i16(record, field::y);
    if (gadget_type_of(record) != gadget_type::panel) {
        rect.left += record_i16(table[0], field::x);
        rect.top += record_i16(table[0], field::y);
    }
    rect.right = record_i16(record, field::width) - 1 + rect.left;
    rect.bottom = record_i16(record, field::height) - 1 + rect.top;
    return rect;
}

void scroll_bar_rects(const GadgetRecord& record, GadgetRect& outer, GadgetRect& knob) {
    outer.left = record_i16(record, field::x);
    outer.top = record_i16(record, field::y);
    outer.right = record_i16(record, field::width) + outer.left;
    outer.bottom = record_i16(record, field::height) + outer.top;
    const auto position = static_cast<int32_t>(record_i16(record, field::scroll_knob));
    const auto size = static_cast<int32_t>(record_i16(record, field::scroll_knob_size));
    if ((record.bytes[field::attributes] & attribute::horizontal) == 0) {
        knob.left = outer.left + 1;
        knob.top = position + 2 + outer.top;
        knob.right = record_i16(record, field::width) - 2 + knob.left;
        knob.bottom = size + knob.top;
    } else {
        knob.left = position + 1 + outer.left;
        knob.top = outer.top + 1;
        knob.right = size + knob.left;
        knob.bottom = record_i16(record, field::height) - 2 + knob.top;
    }
}

int32_t find_group_member(std::span<const GadgetRecord> table, int32_t index, uint8_t type) {
    const auto group = table[static_cast<std::size_t>(index)].bytes[field::group];
    const auto end = scan_end(table);
    for (int32_t candidate = 1; candidate < end; ++candidate) {
        const auto& record = table[static_cast<std::size_t>(candidate)];
        if (record.bytes[field::type] == type && record.bytes[field::group] == group)
            return candidate;
    }
    return 0;
}

int32_t find_list_skin_record(std::span<const GadgetRecord>, int32_t) {
    return 0;
}

int32_t add_gadget(std::span<GadgetRecord> table, uint8_t type) {
    if (table.empty())
        return kNoGadget;
    const auto next = static_cast<int16_t>(record_i16(table[0], field::record_count) + 1);
    if (next < 0 || static_cast<std::size_t>(next) >= table.size())
        return kNoGadget;
    set_record_i16(table[0], field::record_count, next);
    auto& record = table[static_cast<std::size_t>(next)];
    record = GadgetRecord{};
    record.bytes[field::type] = type;
    record.bytes[field::active] = 1;
    return next;
}

int32_t add_label(
    std::span<GadgetRecord> table,
    std::string_view name,
    std::string_view text,
    int16_t x,
    int16_t y,
    int32_t width,
    uint32_t attributes
) {
    if (table.empty())
        return kNoGadget;
    const auto next = static_cast<int16_t>(record_i16(table[0], field::record_count) + 1);
    if (next < 0 || static_cast<std::size_t>(next) >= table.size())
        return kNoGadget;
    set_record_i16(table[0], field::record_count, next);
    auto& record = table[static_cast<std::size_t>(next)];
    set_record_i16(record, field::y, y);
    record.bytes[field::type] = gadget_type::label;
    set_record_i16(record, field::x, x);
    if (width == -1) {
        set_record_i16(
            record, field::width, static_cast<int16_t>(record_i16(table[0], field::width) - x - 5)
        );
    } else {
        set_record_i16(record, field::width, static_cast<int16_t>(width));
    }
    record.bytes[field::group] = 0;
    set_record_i16(record, field::height, 0xF);
    set_record_u32(record, field::color_foreground, 0xF);
    set_record_u32(record, field::attributes, attributes);
    set_record_u32(record, field::color_background, 0);
    record.bytes[field::texture] = 0;
    record.bytes[field::font] = 0;
    record.bytes[field::active] = 1;
    record.bytes[field::common_attributes] = 0;
    set_record_string(record, field::name, name, field::name_bytes);
    set_record_string(record, field::text, text, field::label_text_bytes);
    record.bytes[field::text + field::label_text_bytes] = 0; // ends a full-length caption
    return next;
}

bool append_button_copy(std::span<GadgetRecord> table, const GadgetRecord& source) {
    return append_copy(table, source, 0x13E, gadget_type::button);
}

void reset_hot_surface(GadgetRecord& record) {
    set_record_u32(record, field::hot_callback, 0);
    set_record_u32(record, field::hot_sequence, 0);
    set_record_u32(record, field::hot_image, 0);
    set_record_u16(record, field::hot_frame, 0);
    record.refs.hot_callback = nullptr;
    record.refs.lines = nullptr;
    record.refs.lines_size = 0;
    record.refs.images = nullptr;
    record.refs.hot_sequence = nullptr;
    record.refs.hot_image = nullptr;
}

bool append_hot_surface_copy(std::span<GadgetRecord> table, const GadgetRecord& source) {
    if (!append_copy(table, source, 0xCC, gadget_type::hot_surface))
        return false;
    reset_hot_surface(table[static_cast<std::size_t>(record_i16(table[0], field::record_count))]);
    return true;
}

bool append_progress_copy(std::span<GadgetRecord> table, const GadgetRecord& source) {
    return append_copy(table, source, 0xD6, gadget_type::progress);
}

} // namespace oa::ui::gui_layout
