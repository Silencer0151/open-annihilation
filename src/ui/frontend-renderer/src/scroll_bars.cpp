// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/ui/frontend_renderer/scroll_bars.hpp"

#include "oa/present/display.hpp"
#include "oa/present/palette_tables.hpp"
#include "oa/present/surface.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <unordered_map>
#include <variant>

namespace oa::ui::frontend_renderer {

namespace {

namespace input = ui::gui_input;
namespace gui = ui::gui_layout;

// A grayed part is shaded at level -0x14, which is row 32 - 0x14 of the
// 32-row shade table.
constexpr std::size_t kShadeRows = 32;
constexpr std::size_t kGrayedShadeRow = kShadeRows - 0x14;
// Palette entries from this one up are shaded from the row before the
// grayed level's.
constexpr std::size_t kFirstEntryShadedFromRowBefore = 0x80;
// A horizontal bar's knob sits this far into the bar from its position, and
// at least its own width and this gap before the bar's last column.
constexpr int32_t kKnobInset = 3;
constexpr int32_t kKnobEndGap = 2;
// A vertical bar's knob is at most the bar's height less this margin long,
// and its last row lies at least this far above the row below the bar.
constexpr int32_t kKnobTravelMargin = 6;
constexpr int32_t kKnobEndMargin = 4;

/// Compares two names without regard to ASCII case.
bool same_name(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size())
        return false;
    for (std::size_t index = 0; index < a.size(); ++index) {
        const auto lower = [](char c) {
            return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
        };
        if (lower(a[index]) != lower(b[index]))
            return false;
    }
    return true;
}

/// Finds a GAF sequence by name, without regard to case.
const formats::gaf::Sequence*
find_sequence(const formats::gaf::Archive& archive, std::string_view name) noexcept {
    for (const auto& sequence : archive.sequences)
        if (same_name(sequence.name, name))
            return &sequence;
    return nullptr;
}

/// Returns a path's file name without its directory and extension.
std::string_view file_stem(std::string_view path) noexcept {
    const auto slash = path.find_last_of("/\\");
    if (slash != std::string_view::npos)
        path.remove_prefix(slash + 1);
    return path.substr(0, path.find('.'));
}

/// Returns the palette entry that holds a colour, or else the nearest one.
uint8_t palette_entry(const PaletteBytes& palette, const uint8_t* rgb) noexcept {
    std::size_t found = 0;
    int32_t nearest = std::numeric_limits<int32_t>::max();
    for (std::size_t index = 0; index < palette_color_count && nearest != 0; ++index) {
        int32_t distance = 0;
        for (std::size_t channel = 0; channel < 3; ++channel) {
            const int32_t difference =
                static_cast<int32_t>(rgb[channel]) -
                static_cast<int32_t>(palette[index * palette_entry_bytes + channel]);
            distance += difference * difference;
        }
        if (distance < nearest) {
            nearest = distance;
            found = index;
        }
    }
    return static_cast<uint8_t>(found);
}

/// Draws a part, grayed and shaded over its rectangle once it is drawn when it is grayed.
template <class Draw>
void draw_part(
    Surface& surface,
    const GrayedPaint& paint,
    bool grayed,
    int32_t left,
    int32_t top,
    int32_t width,
    int32_t height,
    const Draw& draw
) {
    if (!grayed || paint.shade_palette == nullptr) {
        draw(nullptr);
        return;
    }
    auto indices = read_indices(surface, *paint.shade_palette, left, top, width, height);
    draw(&indices);
    gray_and_shade(surface, paint, indices);
}

/// Draws a bar's track and knob from its art.
void draw_track(
    Surface& surface,
    const PaletteBytes& palette,
    const formats::gaf::Sequence& art,
    const input::ScrollBar& bar,
    int32_t x0,
    int32_t y0,
    IndexedRect* indices
) {
    const auto tile = [&](int32_t frame) {
        return formats::gaf::frame_at(&art, bar.art_base + frame);
    };
    const auto width_of = [](const formats::gaf::Frame* frame) {
        return frame != nullptr ? static_cast<int32_t>(frame->width) : 0;
    };
    const auto height_of = [](const formats::gaf::Frame* frame) {
        return frame != nullptr ? static_cast<int32_t>(frame->height) : 0;
    };
    const auto* start = tile(input::kScrollTrackStart);
    const auto* track = tile(input::kScrollTrack);
    const auto* end = tile(input::kScrollTrackEnd);
    const auto* knob_start = tile(input::kScrollKnobStart);
    const int32_t width = bar.rect.width;
    const int32_t height = bar.rect.height;
    if (width < height) {
        int32_t y = y0;
        int32_t x = x0;
        int32_t bottom = height + y0 - 1;
        draw_frame_at(surface, palette, start, x, y, indices);
        y += height_of(start);
        while (height_of(track) > 0 && height_of(track) + y <= bottom) {
            draw_frame_at(surface, palette, track, x, y, indices);
            y += height_of(track);
        }
        bottom = bottom - height_of(end) + 1;
        draw_frame_at(surface, palette, end, x, bottom, indices);
        x += width_of(end) >> 1;
        x -= width_of(knob_start) >> 1;
        int32_t knob_top = bar.knob + y0 + kKnobInset;
        const int32_t size = std::min(height - kKnobTravelMargin, int32_t{bar.knob_size});
        const int32_t knob_bottom = std::min(size + knob_top - 1, height + y0 - kKnobEndMargin);
        knob_top = std::min(knob_top, knob_bottom - size + 1);
        draw_frame_at(surface, palette, knob_start, x, knob_top, indices);
        knob_top += height_of(knob_start);
        const auto* middle = tile(input::kScrollKnobMiddle);
        while (height_of(middle) > 0 && knob_top <= knob_bottom - height_of(middle)) {
            draw_frame_at(surface, palette, middle, x, knob_top, indices);
            knob_top += height_of(middle);
        }
        draw_frame_at(surface, palette, middle, x, knob_bottom - height_of(middle), indices);
        const auto* knob_end = tile(input::kScrollKnobEnd);
        draw_frame_at(
            surface, palette, knob_end, x, knob_bottom - height_of(knob_end) + 1, indices
        );
        return;
    }
    int32_t x = x0;
    int32_t y = y0;
    const int32_t right = width + x0 - 1;
    draw_frame_at(surface, palette, start, x, y, indices);
    x += width_of(start);
    while (width_of(track) > 0 && width_of(track) + x <= right) {
        draw_frame_at(surface, palette, track, x, y, indices);
        x += width_of(track);
    }
    draw_frame_at(surface, palette, end, right - width_of(end) + 1, y, indices);
    y += height_of(end) >> 1;
    y -= height_of(knob_start) >> 1;
    const int32_t knob_x =
        std::min(bar.knob + x0 + kKnobInset, right - width_of(knob_start) - kKnobEndGap);
    draw_frame_at(surface, palette, knob_start, knob_x, y, indices);
}

/// Tells whether a bar is drawn grayed: grayed, or kept from the pointer by its attributes.
bool drawn_grayed(const input::ScrollBar& bar) noexcept {
    return bar.grayed || (bar.attributes & gui::attribute::text_list) != 0;
}

/// Stores a bar's knob, activity, range and knob size in its gadget.
void store(const LayoutScrolls::Bar& entry, gui::Layout& layout) {
    if (entry.gadget >= layout.gadgets.size())
        return;
    auto& gadget = layout.gadgets[entry.gadget];
    gadget.common.active = static_cast<int8_t>(entry.bar.active ? 1 : 0);
    if (auto* fields = std::get_if<gui::ScrollBarFields>(&gadget.fields)) {
        fields->knob_position = entry.bar.knob;
        fields->range = entry.bar.range;
        fields->knob_size = entry.bar.knob_size;
    }
}

/// Returns the first bar that shares a group, or null.
LayoutScrolls::Bar* group_bar(LayoutScrolls& scrolls, uint8_t group) {
    for (auto& entry : scrolls.bars)
        if (entry.bar.group == group)
            return &entry;
    return nullptr;
}

/// Brings a moved bar's lists in line with it and stores its knob.
void bar_moved(LayoutScrolls& scrolls, gui::Layout& layout, int32_t gadget) {
    if (gadget >= 0)
        sync_layout_group(scrolls, layout, static_cast<std::size_t>(gadget));
}

} // namespace

std::vector<input::ScrollFrame> scroll_art_frames(const formats::gaf::Archive& archive) {
    std::vector<input::ScrollFrame> frames;
    if (const auto* sequence = find_sequence(archive, kScrollArtSequence))
        for (const auto& frame : sequence->frames)
            frames.push_back(
                {static_cast<int16_t>(frame.width), static_cast<int16_t>(frame.height)}
            );
    return frames;
}

input::ScrollArtFrames
scroll_art(const formats::gaf::Archive* own, const formats::gaf::Archive& shared) {
    input::ScrollArtFrames art;
    if (own != nullptr)
        art.panel = scroll_art_frames(*own);
    art.shared = scroll_art_frames(shared);
    return art;
}

const formats::gaf::Sequence* scroll_art_sequence(
    const formats::gaf::Archive* own, const formats::gaf::Archive& shared, input::ScrollArt art
) {
    switch (art) {
    case input::ScrollArt::panel:
        return own != nullptr ? find_sequence(*own, kScrollArtSequence) : nullptr;
    case input::ScrollArt::shared:
        return find_sequence(shared, kScrollArtSequence);
    case input::ScrollArt::unbound:
    case input::ScrollArt::none:
        break;
    }
    return nullptr;
}

bool gaf_named_after(std::string_view layout, std::string_view sprites) noexcept {
    const auto own = file_stem(layout);
    return !own.empty() && same_name(own, file_stem(sprites));
}

std::vector<uint8_t> build_gray_table(const PaletteBytes& palette) {
    Palette entries{};
    for (std::size_t index = 0; index < palette_color_count; ++index)
        entries.entries[index] = {
            palette[index * palette_entry_bytes],
            palette[index * palette_entry_bytes + 1],
            palette[index * palette_entry_bytes + 2],
            0
        };
    std::vector<uint8_t> table(static_cast<std::size_t>(present::gray_table_size));
    present::DisplayContext display{};
    display.flags = present::display_flag_gray_table;
    display.gray_table = table.data();
    (void)present::build_gray_table(display, entries);
    return table;
}

GrayedPaint grayed_paint(const ScreenResources& screen, std::vector<uint8_t>& gray_table) {
    GrayedPaint paint;
    paint.palette =
        screen.background.palette.has_value() ? &*screen.background.palette : &screen.gui_palette;
    paint.shade_palette = screen.background.palette.has_value() ? &*screen.background.palette
                          : screen.game_palette.has_value()     ? &*screen.game_palette
                                                                : nullptr;
    if (screen.shade_table.size() != kShadeRows * palette_color_count)
        paint.shade_palette = nullptr;
    if (paint.shade_palette == nullptr)
        return paint;
    if (gray_table.empty())
        gray_table = build_gray_table(
            screen.game_palette.has_value() ? *screen.game_palette : *paint.shade_palette
        );
    paint.gray = gray_table;
    paint.shade = screen.shade_table;
    return paint;
}

IndexedRect read_indices(
    const Surface& surface,
    const PaletteBytes& palette,
    int32_t left,
    int32_t top,
    int32_t width,
    int32_t height
) {
    IndexedRect rect;
    rect.left = std::max(0, left);
    rect.top = std::max(0, top);
    rect.width =
        std::max(0, std::min(left + width, static_cast<int32_t>(surface.width)) - rect.left);
    rect.height =
        std::max(0, std::min(top + height, static_cast<int32_t>(surface.height)) - rect.top);
    rect.pixels.resize(
        static_cast<std::size_t>(rect.width) * static_cast<std::size_t>(rect.height)
    );
    std::unordered_map<uint32_t, uint8_t> entries;
    for (int32_t row = 0; row < rect.height; ++row)
        for (int32_t column = 0; column < rect.width; ++column) {
            const auto* rgb = &surface.rgb
                                   [(static_cast<std::size_t>(rect.top + row) * surface.width +
                                     static_cast<std::size_t>(rect.left + column)) *
                                    3U];
            const uint32_t color =
                static_cast<uint32_t>(rgb[0]) << 16 | static_cast<uint32_t>(rgb[1]) << 8 | rgb[2];
            auto [entry, added] = entries.try_emplace(color, 0);
            if (added)
                entry->second = palette_entry(palette, rgb);
            rect.pixels[static_cast<std::size_t>(row * rect.width + column)] = entry->second;
        }
    return rect;
}

void gray_and_shade(Surface& surface, const GrayedPaint& paint, const IndexedRect& rect) {
    if (paint.shade_palette == nullptr || paint.gray.size() < palette_color_count ||
        paint.shade.size() < kShadeRows * palette_color_count)
        return;
    for (int32_t row = 0; row < rect.height; ++row)
        for (int32_t column = 0; column < rect.width; ++column) {
            std::size_t index = rect.pixels[static_cast<std::size_t>(row * rect.width + column)];
            index = paint.gray[index];
            const auto shade_row =
                index >= kFirstEntryShadedFromRowBefore ? kGrayedShadeRow - 1 : kGrayedShadeRow;
            index = paint.shade[shade_row * palette_color_count + index];
            std::memcpy(
                &surface.rgb
                     [(static_cast<std::size_t>(rect.top + row) * surface.width +
                       static_cast<std::size_t>(rect.left + column)) *
                      3U],
                &(*paint.shade_palette)[index * palette_entry_bytes],
                3
            );
        }
}

void draw_frame_at(
    Surface& surface,
    const PaletteBytes& palette,
    const formats::gaf::Frame* frame,
    int32_t x,
    int32_t y,
    IndexedRect* indices
) {
    if (frame == nullptr)
        return;
    const auto rendered = formats::gaf::render_normal(*frame);
    if (!rendered.ok())
        return;
    const auto& image = *rendered.frame;
    for (int32_t row = 0; row < static_cast<int32_t>(image.height); ++row)
        for (int32_t column = 0; column < static_cast<int32_t>(image.width); ++column) {
            const auto at =
                static_cast<std::size_t>(row) * image.width + static_cast<std::size_t>(column);
            const int32_t px = x + column;
            const int32_t py = y + row;
            if (image.coverage[at] == 0 || px < 0 || py < 0 ||
                px >= static_cast<int32_t>(surface.width) ||
                py >= static_cast<int32_t>(surface.height))
                continue;
            const auto index = image.pixels[at];
            std::memcpy(
                &surface.rgb
                     [(static_cast<std::size_t>(py) * surface.width +
                       static_cast<std::size_t>(px)) *
                      3U],
                &palette[static_cast<std::size_t>(index) * palette_entry_bytes],
                3
            );
            if (indices != nullptr && px >= indices->left && py >= indices->top &&
                px < indices->left + indices->width && py < indices->top + indices->height)
                indices->pixels[static_cast<std::size_t>(
                    (py - indices->top) * indices->width + px - indices->left
                )] = index;
        }
}

void draw_scroll_bar(
    Surface& surface,
    const GrayedPaint& paint,
    const formats::gaf::Sequence* art,
    const input::ScrollBar& bar,
    int32_t offset_x,
    int32_t offset_y,
    input::ScrollPart held
) {
    if (art == nullptr || paint.palette == nullptr || !bar.active ||
        (bar.art != input::ScrollArt::panel && bar.art != input::ScrollArt::shared))
        return;
    const bool grayed = drawn_grayed(bar);
    const int32_t x = bar.rect.x + offset_x;
    const int32_t y = bar.rect.y + offset_y;
    draw_part(
        surface, paint, grayed, x, y, bar.rect.width, bar.rect.height, [&](IndexedRect* indices) {
            draw_track(surface, *paint.palette, *art, bar, x, y, indices);
        }
    );
    for (const bool forward : {false, true}) {
        const auto& arrow = forward ? bar.forward_arrow : bar.back_arrow;
        if (arrow.width <= 0 || arrow.height <= 0)
            continue;
        const auto part =
            forward ? input::ScrollPart::forward_arrow : input::ScrollPart::back_arrow;
        const bool pressed = held == part && !grayed;
        const int32_t frame = bar.art_base +
                              (forward ? input::kScrollForwardArrow : input::kScrollBackArrow) +
                              (pressed ? 1 : 0);
        const int32_t left = arrow.x + offset_x;
        const int32_t top = arrow.y + offset_y;
        draw_part(
            surface,
            paint,
            grayed,
            left,
            top,
            arrow.width,
            arrow.height,
            [&](IndexedRect* indices) {
                draw_frame_at(
                    surface, *paint.palette, formats::gaf::frame_at(art, frame), left, top, indices
                );
            }
        );
    }
}

LayoutScrolls bind_layout_scrolls(
    gui::Layout& layout, const input::ScrollArtFrames& art, int32_t line_height, std::size_t first
) {
    LayoutScrolls scrolls;
    scrolls.line_height = line_height;
    for (std::size_t index = std::max<std::size_t>(first, 1); index < layout.gadgets.size();
         ++index) {
        auto& gadget = layout.gadgets[index];
        if (gadget.common.type == gui::GadgetType::list_box) {
            auto list = input::scroll_list_from_gadget(gadget);
            input::scroll_list_trim(list, line_height);
            gadget.common.height = list.height;
            scrolls.lists.push_back({index, list});
        } else if (gadget.common.type == gui::GadgetType::scroll_bar) {
            auto bar = input::scroll_bar_from_gadget(gadget);
            input::bind_scroll_bar(bar, art);
            gadget.common.x = bar.rect.x;
            gadget.common.y = bar.rect.y;
            gadget.common.width = bar.rect.width;
            gadget.common.height = bar.rect.height;
            scrolls.bars.push_back({index, bar});
        }
    }
    for (auto& entry : scrolls.bars) {
        for (const auto& list : scrolls.lists)
            if (list.list.group == entry.bar.group)
                entry.bar.active = false;
        store(entry, layout);
    }
    return scrolls;
}

void refresh_layout_scrolls(LayoutScrolls& scrolls, const gui::Layout& layout) {
    for (auto& entry : scrolls.bars) {
        if (entry.gadget >= layout.gadgets.size())
            continue;
        const auto& gadget = layout.gadgets[entry.gadget];
        entry.bar.active = gadget.common.active != 0;
        if (const auto* fields = std::get_if<gui::ScrollBarFields>(&gadget.fields)) {
            entry.bar.knob = fields->knob_position;
            entry.bar.grayed = fields->locked;
        }
    }
    for (auto& entry : scrolls.lists)
        if (entry.gadget < layout.gadgets.size())
            entry.list.active = layout.gadgets[entry.gadget].common.active != 0;
}

LayoutScrolls::Bar* find_layout_bar(LayoutScrolls& scrolls, std::size_t gadget) {
    for (auto& entry : scrolls.bars)
        if (entry.gadget == gadget)
            return &entry;
    return nullptr;
}

LayoutScrolls::List* find_layout_list(LayoutScrolls& scrolls, std::size_t gadget) {
    for (auto& entry : scrolls.lists)
        if (entry.gadget == gadget)
            return &entry;
    return nullptr;
}

void fill_layout_list(
    LayoutScrolls& scrolls, gui::Layout& layout, std::size_t gadget, int32_t count
) {
    auto* entry = find_layout_list(scrolls, gadget);
    if (entry == nullptr)
        return;
    if (gadget < layout.gadgets.size())
        entry->list.active = layout.gadgets[gadget].common.active != 0;
    const bool overflow = input::scroll_list_fill(entry->list, count, scrolls.line_height);
    if (!entry->list.active)
        return;
    auto* bar = group_bar(scrolls, entry->list.group);
    if (bar == nullptr)
        return;
    bar->bar.active = overflow;
    // The knob is sized from the group's first list, as in 3.1c.
    if (overflow)
        for (const auto& first : scrolls.lists)
            if (first.list.group == entry->list.group) {
                input::scroll_bar_fit_list(bar->bar, first.list, scrolls.line_height);
                break;
            }
    store(*bar, layout);
}

void select_layout_list_row(
    LayoutScrolls& scrolls, gui::Layout& layout, std::size_t gadget, int32_t row
) {
    auto* entry = find_layout_list(scrolls, gadget);
    if (entry == nullptr)
        return;
    auto* bar = group_bar(scrolls, entry->list.group);
    input::scroll_list_select(
        entry->list, bar != nullptr ? &bar->bar : nullptr, scrolls.line_height, row
    );
    if (bar != nullptr)
        store(*bar, layout);
}

void sync_layout_group(LayoutScrolls& scrolls, gui::Layout& layout, std::size_t gadget) {
    if (auto* bar = find_layout_bar(scrolls, gadget)) {
        for (auto& entry : scrolls.lists)
            if (entry.list.group == bar->bar.group)
                (void)input::scroll_list_follow_bar(entry.list, bar->bar);
        store(*bar, layout);
        return;
    }
    auto* source = find_layout_list(scrolls, gadget);
    if (source == nullptr)
        return;
    const auto list = source->list;
    for (auto& entry : scrolls.lists)
        if (entry.gadget != gadget && entry.list.group == list.group) {
            entry.list.first = list.first;
            entry.list.selection = list.selection;
        }
    for (auto& entry : scrolls.bars)
        if (entry.bar.group == list.group) {
            input::scroll_bar_follow_list(entry.bar, list);
            store(entry, layout);
        }
}

ScrollInput press_layout_scrolls(
    LayoutScrolls& scrolls, gui::Layout& layout, int32_t x, int32_t y, uint32_t tick
) {
    for (auto entry = scrolls.bars.rbegin(); entry != scrolls.bars.rend(); ++entry) {
        if (input::scroll_bar_part(entry->bar, x, y) == input::ScrollPart::none)
            continue;
        const auto id = static_cast<int32_t>(entry->gadget);
        const bool changed = input::scroll_press(entry->bar, scrolls.hold, id, x, y, tick);
        if (changed)
            bar_moved(scrolls, layout, id);
        return {true, changed ? id : -1};
    }
    input::scroll_let_go(scrolls.hold);
    return {};
}

bool move_layout_scrolls(LayoutScrolls& scrolls, int32_t x, int32_t y) {
    input::scroll_move(scrolls.hold, x, y);
    return scrolls.hold.bar != input::kNoScrollBar;
}

ScrollInput
release_layout_scrolls(LayoutScrolls& scrolls, gui::Layout& layout, int32_t x, int32_t y) {
    const auto id = scrolls.hold.bar;
    auto* entry = id >= 0 ? find_layout_bar(scrolls, static_cast<std::size_t>(id)) : nullptr;
    if (entry == nullptr) {
        input::scroll_let_go(scrolls.hold);
        return {};
    }
    const bool changed = input::scroll_release(entry->bar, scrolls.hold, x, y);
    if (changed)
        bar_moved(scrolls, layout, id);
    return {true, changed ? id : -1};
}

int32_t tick_layout_scrolls(LayoutScrolls& scrolls, gui::Layout& layout, uint32_t tick) {
    const auto id = scrolls.hold.bar;
    auto* entry = id >= 0 ? find_layout_bar(scrolls, static_cast<std::size_t>(id)) : nullptr;
    if (entry == nullptr) {
        input::scroll_let_go(scrolls.hold);
        return -1;
    }
    if (!input::scroll_hold_tick(entry->bar, scrolls.hold, tick))
        return -1;
    bar_moved(scrolls, layout, id);
    return id;
}

void draw_layout_scrolls(
    Surface& surface,
    const GrayedPaint& paint,
    const formats::gaf::Archive* own,
    const formats::gaf::Archive& shared,
    const LayoutScrolls& scrolls,
    int32_t offset_x,
    int32_t offset_y
) {
    for (const auto& entry : scrolls.bars) {
        const auto held = scrolls.hold.bar == static_cast<int32_t>(entry.gadget)
                              ? scrolls.hold.part
                              : input::ScrollPart::none;
        draw_scroll_bar(
            surface,
            paint,
            scroll_art_sequence(own, shared, entry.bar.art),
            entry.bar,
            offset_x,
            offset_y,
            held
        );
    }
}

} // namespace oa::ui::frontend_renderer
