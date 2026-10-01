// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/ui/frontend_renderer/gadget_draw.hpp"

#include "oa/present/blit.hpp"
#include "oa/present/raster.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace oa::ui::frontend_renderer {
namespace {

void line(oa::Surface* target, int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint8_t color) {
    present::draw_clipped_line(target, x0, y0, x1, y1, color);
}

void bevel(oa::Surface* target, const oa::Rect32& r, uint8_t top_left, uint8_t bottom_right) {
    line(target, r.x1, r.y1, r.x2, r.y1, top_left);
    line(target, r.x1, r.y1 + 1, r.x2 - 1, r.y1 + 1, top_left);
    line(target, r.x1, r.y1, r.x1, r.y2, top_left);
    line(target, r.x1 + 1, r.y1, r.x1 + 1, r.y2 - 1, top_left);
    line(target, r.x2, r.y1 + 1, r.x2, r.y2, bottom_right);
    line(target, r.x2 - 1, r.y1 + 2, r.x2 - 1, r.y2, bottom_right);
    line(target, r.x1 + 1, r.y2, r.x2, r.y2, bottom_right);
    line(target, r.x1 + 2, r.y2 - 1, r.x2, r.y2 - 1, bottom_right);
}

void frame(oa::Surface* target, const oa::Rect32& r, uint8_t top_left, uint8_t bottom_right) {
    line(target, r.x1, r.y1, r.x2, r.y1, top_left);
    line(target, r.x1, r.y1, r.x1, r.y2, top_left);
    line(target, r.x2, r.y1 + 1, r.x2, r.y2, bottom_right);
    line(target, r.x1 + 1, r.y2, r.x2, r.y2, bottom_right);
}

} // namespace

void draw_bevel_raised(
    oa::Surface* target, const oa::Rect32& rect, uint8_t top_left, uint8_t bottom_right
) {
    bevel(target, rect, top_left, bottom_right);
}

void draw_bevel_sunken(
    oa::Surface* target, const oa::Rect32& rect, uint8_t bottom_right, uint8_t top_left
) {
    bevel(target, rect, top_left, bottom_right);
}

void fill_box_raised(
    oa::Surface* target,
    const oa::Rect32& rect,
    uint8_t top_left,
    uint8_t bottom_right,
    uint8_t fill
) {
    present::fill_clipped_rect(target, rect, fill);
    draw_bevel_raised(target, rect, top_left, bottom_right);
}

void fill_box_sunken(
    oa::Surface* target,
    const oa::Rect32& rect,
    uint8_t bottom_right,
    uint8_t top_left,
    uint8_t fill
) {
    present::fill_clipped_rect(target, rect, fill);
    draw_bevel_sunken(target, rect, bottom_right, top_left);
}

void fill_frame_raised(
    oa::Surface* target,
    const oa::Rect32& rect,
    uint8_t top_left,
    uint8_t bottom_right,
    uint8_t fill
) {
    present::fill_clipped_rect(target, rect, fill);
    frame(target, rect, top_left, bottom_right);
}

void fill_frame_sunken(
    oa::Surface* target,
    const oa::Rect32& rect,
    uint8_t bottom_right,
    uint8_t top_left,
    uint8_t fill
) {
    present::fill_clipped_rect(target, rect, fill);
    frame(target, rect, top_left, bottom_right);
}

std::array<FocusRing, focus_ring_count>
focus_rings(const ui::gui_layout::GadgetRect& rect) noexcept {
    std::array<FocusRing, focus_ring_count> rings{};
    oa::Rect32 outline{rect.left, rect.top, rect.right, rect.bottom};
    int32_t level = present::light_level_brightest;
    for (std::size_t ring = 0; ring < focus_ring_count; ++ring) {
        --outline.x1;
        --outline.y1;
        ++outline.x2;
        ++outline.y2;
        rings[ring] = {outline, level};
        level += -3 - static_cast<int32_t>(ring);
    }
    return rings;
}

void draw_focus_outline(oa::Surface* target, const ui::gui_layout::GadgetRect& rect) {
    for (const auto& ring : focus_rings(rect))
        present::light_rect_edges(target, ring.rect, ring.level);
}

std::size_t quick_key_offset(std::string_view caption, char key) noexcept {
    return key == '\0' ? std::string_view::npos : caption.find(key);
}

void draw_value_marker(
    oa::Surface* target,
    const ui::gui_layout::GadgetRect& rect,
    uint32_t attributes,
    uint8_t color,
    uint8_t flags
) {
    if ((flags & 1) == 0)
        return;
    auto x1 = rect.right;
    auto y1 = rect.bottom;
    if ((attributes & 1) != 0)
        y1 = rect.top;
    else if ((attributes & 2) != 0)
        x1 = rect.left;
    else if ((attributes & 4) == 0)
        return;
    line(target, rect.left, rect.top, x1, y1, color);
}

oa::present::GafStatus load_gui_font(std::span<const uint8_t> file, oa::present::GafSprites& font) {
    constexpr int32_t height_glyph = 'I';
    const oa::present::GafStatus status = present::relocate_gaf(file, font);
    if (status != oa::present::GafStatus::ok || font.sequences.empty())
        return status;
    const present::GafSequence& glyphs = font.sequences.front();
    const oa::Sprite* tall = present::gaf_frame(&glyphs, height_glyph);
    const auto lift = static_cast<int16_t>(tall != nullptr ? tall->height : 0);
    for (int32_t index = 0; index < glyphs.frame_count; ++index)
        if (oa::Sprite* glyph = present::gaf_frame(&glyphs, index); glyph != nullptr)
            glyph->origin_y = static_cast<int16_t>(glyph->origin_y - lift);
    return status;
}

void draw_gadget_text(
    oa::Surface* target,
    const oa::present::GafSprites* font,
    const char* text,
    int32_t x,
    int32_t y,
    int32_t max_width,
    int32_t light_level
) {
    constexpr unsigned char first_glyph = 0x20;
    constexpr unsigned char space = 0x20;
    if (font == nullptr) {
        present::draw_text(target, text, x, y, present::text_width_unbounded);
        return;
    }
    const present::GafSequence* glyphs =
        font->sequences.empty() ? nullptr : &font->sequences.front();
    for (const auto* p = reinterpret_cast<const unsigned char*>(text); *p != 0; ++p) {
        if (*p < first_glyph)
            continue;
        const oa::Sprite* glyph = present::gaf_frame(glyphs, *p);
        if (glyph == nullptr)
            continue;
        if (max_width != gadget_text_unbounded && glyph->width > max_width)
            return;
        if (*p != space) {
            if (light_level == 0)
                present::draw_sprite(target, glyph, x, y);
            else
                present::draw_sprite_lit(target, glyph, x, y, light_level);
        }
        if (max_width != gadget_text_unbounded) {
            max_width -= glyph->width;
            if (max_width < 0)
                return;
        }
        x += glyph->width;
    }
}

} // namespace oa::ui::frontend_renderer
