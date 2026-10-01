// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/present/blit.hpp"

#include "oa/present/display.hpp"
#include "oa/present/rle.hpp"
#include "oa/present/surface.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>

namespace oa::present {

namespace {

uint8_t* pixel_at(const Surface& s, int32_t x, int32_t y) noexcept {
    return s.pixels + static_cast<std::ptrdiff_t>(y) * s.pitch + x;
}

// Runs `draw` on `target`, or on the locked display surface when it is null.
template <typename Draw>
void with_target(Surface* target, Draw draw) noexcept {
    if (target != nullptr) {
        draw(*target);
        return;
    }
    Surface locked{};
    if (lock_display_surface(locked) != 0) {
        draw(locked);
        unlock_display_surface();
    }
}

// Clip-origin copy shared by copy_surface_clipped and copy_surface_keyed.
template <typename Put>
void copy_clipped(Surface& dst, const Surface& src, int32_t x, int32_t y, Put put) noexcept {
    assert(!surface_banded(dst) && "surface copies do not honour a surface's band");
    int32_t dst_width = dst.width;
    int32_t dst_height = dst.height;
    if (x >= dst_width || y >= dst_height) {
        return;
    }
    int32_t width = src.width;
    int32_t height = src.height;
    if (-x >= width || -y >= height) {
        return;
    }
    const uint8_t* in = src.pixels;
    uint8_t* out = dst.pixels;
    if (x < 0) {
        width += x;
        in -= x;
    } else {
        dst_width -= x;
        out += x;
    }
    if (dst_width < width) {
        width = dst_width;
    }
    if (y < 0) {
        height += y;
        in += static_cast<std::ptrdiff_t>(-y) * src.pitch;
    } else {
        dst_height -= y;
        out += static_cast<std::ptrdiff_t>(y) * dst.pitch;
    }
    if (dst_height < height) {
        height = dst_height;
    }
    if (width <= 0 || height <= 0) {
        return;
    }
    for (int32_t row = 0; row < height; ++row, in += src.pitch, out += dst.pitch) {
        for (int32_t i = 0; i < width; ++i) {
            put(out + i, in[i]);
        }
    }
}

template <typename Put>
void copy_region(
    Surface& dst, const Surface& src, const Rect32& src_rect, const Rect32& dst_pos, Put put
) noexcept {
    const int32_t width = src_rect.x2 - src_rect.x1 + 1;
    int32_t rows = src_rect.y2 - src_rect.y1 + 1;
    if (width <= 0 || rows <= 0) {
        return;
    }
    const uint8_t* in = pixel_at(src, src_rect.x1, src_rect.y1);
    uint8_t* out = pixel_at(dst, dst_pos.x1, dst_pos.y1);
    do {
        for (int32_t i = 0; i < width; ++i) {
            put(out + i, in[i]);
        }
        in += src.pitch;
        out += dst.pitch;
    } while (--rows != 0);
}

void put_byte(uint8_t* out, uint8_t value) noexcept {
    *out = value;
}

// Placement shared by the three sprite drawers: the sprite rectangle at
// (x,y) less its origin, trimmed to the target clip. False when nothing shows.
bool place_sprite(
    const Surface& target, const Sprite& sprite, int32_t x, int32_t y, Rect32& src, Rect32& dst
) noexcept {
    src = Rect32{0, 0, sprite.width - 1, sprite.height - 1};
    dst.x1 = x - sprite.origin_x;
    dst.y1 = y - sprite.origin_y;
    dst.x2 = sprite.width - 1 + dst.x1;
    dst.y2 = sprite.height - 1 + dst.y1;
    trim_to_surface(src, dst, target);
    return dst.x1 <= dst.x2 && dst.y1 <= dst.y2 && src.x1 <= src.x2 && src.y1 <= src.y2;
}

Surface raw_sprite_view(const Sprite& sprite) noexcept {
    Surface view{};
    view.width = sprite.width;
    view.height = sprite.height;
    view.pitch = sprite.width;
    view.pixels = static_cast<uint8_t*>(sprite.data);
    return view;
}

const Sprite* child_of(const Sprite& sprite, int32_t index) noexcept {
    return static_cast<const Sprite* const*>(sprite.data)[index];
}

/// Returns a row-RLE sprite's stream through one of its rows.
///
/// The sprite record holds no stream size, so the rows are found through
/// their length words as the sprite's producer laid them out: relocate_gaf
/// checks every row of a GAF, and the encoders write whole rows. The decoders
/// then read each row's commands only inside that row.
///
/// @param sprite row-RLE sprite
/// @param last_row index of the last row a draw decodes
/// @return the stream from its first row to the end of `last_row`
std::span<const uint8_t> sprite_rows(const Sprite& sprite, int32_t last_row) noexcept {
    const auto* stream = static_cast<const uint8_t*>(sprite.data);
    size_t size = 0;
    for (int32_t row = 0; row <= last_row; ++row) {
        const size_t length =
            static_cast<size_t>(stream[size]) | (static_cast<size_t>(stream[size + 1]) << 8);
        size += rle_row_length_bytes + length;
    }
    return {stream, size};
}

} // namespace

void trim_to_clip(Rect32& src, Rect32& dst, const Rect32& clip) noexcept {
    int32_t delta = dst.x1 - clip.x1;
    if (delta < 0) {
        src.x1 -= delta;
        dst.x1 -= delta;
    }
    delta = dst.x2 - clip.x2;
    if (delta > 0) {
        src.x2 -= delta;
        dst.x2 -= delta;
    }
    delta = dst.y1 - clip.y1;
    if (delta < 0) {
        src.y1 -= delta;
        dst.y1 -= delta;
    }
    delta = dst.y2 - clip.y2;
    if (delta > 0) {
        src.y2 -= delta;
        dst.y2 -= delta;
    }
}

void trim_to_surface(Rect32& src, Rect32& dst, const Surface& surface) noexcept {
    trim_to_clip(src, dst, surface.clip);
    const SurfaceRows band = surface_band(surface);
    if (dst.y1 < band.first) {
        src.y1 += band.first - dst.y1;
        dst.y1 = band.first;
    }
    if (dst.y2 >= band.end) {
        src.y2 -= dst.y2 - (band.end - 1);
        dst.y2 = band.end - 1;
    }
}

void copy_surface_clipped(Surface& dst, const Surface& src, int32_t x, int32_t y) noexcept {
    copy_clipped(dst, src, x, y, put_byte);
}

void copy_surface_keyed(
    Surface& dst, const Surface& src, int32_t x, int32_t y, uint8_t key
) noexcept {
    copy_clipped(dst, src, x, y, [key](uint8_t* out, uint8_t value) {
        if (value != key) {
            *out = value;
        }
    });
}

void copy_rect(
    Surface& dst, const Surface& src, const Rect32& src_rect, const Rect32& dst_pos
) noexcept {
    copy_region(dst, src, src_rect, dst_pos, put_byte);
}

void copy_rect_keyed(
    Surface& dst, const Surface& src, const Rect32& src_rect, const Rect32& dst_pos, uint8_t key
) noexcept {
    copy_region(dst, src, src_rect, dst_pos, [key](uint8_t* out, uint8_t value) {
        if (value != key) {
            *out = value;
        }
    });
}

void copy_tile(Surface& dst, int32_t x, int32_t y, const uint8_t* tile) noexcept {
    assert(!surface_banded(dst) && "tile copies do not honour a surface's band");
    uint8_t* out = pixel_at(dst, x, y);
    for (int32_t row = 0; row < tile_size; ++row, out += dst.pitch, tile += tile_size) {
        for (int32_t i = 0; i < tile_size; ++i) {
            out[i] = tile[i];
        }
    }
}

void blit_surface(Surface* dst, const Surface* src, int32_t x, int32_t y) noexcept {
    if (dst != nullptr && src != nullptr) {
        copy_surface_clipped(*dst, *src, x - src->origin_x, y - src->origin_y);
        return;
    }
    if (dst == nullptr && src == nullptr) {
        return;
    }
    Surface locked{};
    if (lock_display_surface(locked) == 0) {
        return;
    }
    if (dst == nullptr) {
        copy_surface_clipped(locked, *src, x - src->origin_x, y - src->origin_y);
    } else {
        copy_surface_clipped(*dst, locked, x, y);
    }
    unlock_display_surface();
}

void blit_surface_keyed(
    Surface* dst, const Surface& src, int32_t x, int32_t y, uint8_t key
) noexcept {
    with_target(dst, [&](Surface& target) {
        copy_surface_keyed(target, src, x - src.origin_x, y - src.origin_y, key);
    });
}

void blit_rect(
    Surface* dst, const Surface& src, const Rect32& src_rect, const Rect32& dst_pos
) noexcept {
    with_target(dst, [&](Surface& target) { copy_rect(target, src, src_rect, dst_pos); });
}

void blit_rect_keyed(
    Surface* dst, const Surface& src, const Rect32& src_rect, const Rect32& dst_pos, uint8_t key
) noexcept {
    with_target(dst, [&](Surface& target) {
        copy_rect_keyed(target, src, src_rect, dst_pos, key);
    });
}

void blit_tile(Surface* dst, int32_t x, int32_t y, const uint8_t* tile) noexcept {
    with_target(dst, [&](Surface& target) { copy_tile(target, x, y, tile); });
}

void draw_sprite(Surface* target, const Sprite* sprite, int32_t x, int32_t y) noexcept {
    with_target(target, [&](Surface& surface) {
        if (sprite == nullptr) {
            return;
        }
        if (sprite->child_count != 0) {
            for (int32_t i = 0; i < sprite->child_count; ++i) {
                const Sprite* child = child_of(*sprite, i);
                if (child->child_draw_mode == 0) {
                    draw_sprite(&surface, child, x, y);
                } else {
                    draw_sprite_blended(&surface, child, x, y);
                }
            }
            return;
        }
        Rect32 src{};
        Rect32 dst{};
        if (!place_sprite(surface, *sprite, x, y, src, dst)) {
            return;
        }
        if (sprite->encoding == OA_SPRITE_RAW) {
            copy_rect_keyed(surface, raw_sprite_view(*sprite), src, dst, sprite->key);
        } else {
            decode_rle_rows(surface.pixels, surface.pitch, dst, sprite_rows(*sprite, src.y2), src);
        }
    });
}

void draw_sprite_blended(Surface* target, const Sprite* sprite, int32_t x, int32_t y) noexcept {
    const DisplayContext* display = display_context();
    if (display == nullptr || (display->flags & display_flag_alpha_table) == 0) {
        return;
    }
    const uint8_t* table = display->alpha_table;
    with_target(target, [&](Surface& surface) {
        if (sprite == nullptr) {
            return;
        }
        if (sprite->child_count != 0) {
            for (int32_t i = 0; i < sprite->child_count; ++i) {
                draw_sprite_blended(&surface, child_of(*sprite, i), x, y);
            }
            return;
        }
        Rect32 src{};
        Rect32 dst{};
        if (!place_sprite(surface, *sprite, x, y, src, dst)) {
            return;
        }
        if (sprite->encoding == OA_SPRITE_RAW) {
            copy_rect_blended(surface, raw_sprite_view(*sprite), src, dst, sprite->key, table);
        } else {
            decode_rle_rows_blended(
                surface.pixels, surface.pitch, dst, sprite_rows(*sprite, src.y2), src, table
            );
        }
    });
}

void draw_sprite_lit(
    Surface* target, const Sprite* sprite, int32_t x, int32_t y, int32_t level
) noexcept {
    const DisplayContext* display = display_context();
    if (display == nullptr || (display->flags & display_flag_light_table) == 0) {
        return;
    }
    const uint8_t* table = display->light_table;
    with_target(target, [&](Surface& surface) {
        if (sprite == nullptr) {
            return;
        }
        if (sprite->child_count != 0) {
            for (int32_t i = 0; i < sprite->child_count; ++i) {
                draw_sprite_blended(&surface, child_of(*sprite, i), x, y);
            }
            return;
        }
        Rect32 src{};
        Rect32 dst{};
        if (!place_sprite(surface, *sprite, x, y, src, dst)) {
            return;
        }
        if (sprite->encoding == OA_SPRITE_RAW) {
            copy_rect_blended(
                surface, raw_sprite_view(*sprite), src, dst, static_cast<uint8_t>(level), table
            );
        } else {
            decode_rle_rows_remapped(
                surface.pixels,
                surface.pitch,
                dst,
                sprite_rows(*sprite, src.y2),
                src,
                table + static_cast<std::ptrdiff_t>(level) * palette_table_row_bytes
            );
        }
    });
}

void draw_sprite_shadow(Surface* target, const Sprite* sprite, int32_t x, int32_t y) noexcept {
    const DisplayContext* display = display_context();
    if (display == nullptr || (display->flags & display_flag_light_table) == 0) {
        return;
    }
    const uint8_t* table = display->light_table;
    with_target(target, [&](Surface& surface) {
        if (sprite == nullptr) {
            return;
        }
        if (sprite->child_count != 0) {
            for (int32_t i = 0; i < sprite->child_count; ++i) {
                draw_sprite_shadow(&surface, child_of(*sprite, i), x, y);
            }
            return;
        }
        Rect32 src{};
        Rect32 dst{};
        if (sprite->data == nullptr || !place_sprite(surface, *sprite, x, y, src, dst)) {
            return;
        }
        if (sprite->encoding == OA_SPRITE_RAW) {
            copy_rect_shaded(surface, raw_sprite_view(*sprite), src, dst, sprite->key, table);
        } else {
            decode_rle_rows_shaded(
                surface.pixels, surface.pitch, dst, sprite_rows(*sprite, src.y2), src, table
            );
        }
    });
}

void draw_sprite_opaque(Surface* target, const Sprite* sprite, int32_t x, int32_t y) noexcept {
    with_target(target, [&](Surface& surface) {
        if (sprite == nullptr) {
            return;
        }
        if (sprite->child_count != 0) {
            for (int32_t i = 0; i < sprite->child_count; ++i) {
                const Sprite* child = child_of(*sprite, i);
                if (child->child_draw_mode == 0) {
                    draw_sprite(&surface, child, x, y);
                } else {
                    draw_sprite_blended(&surface, child, x, y);
                }
            }
            return;
        }
        Rect32 src{};
        Rect32 dst{};
        if (!place_sprite(surface, *sprite, x, y, src, dst)) {
            return;
        }
        if (sprite->encoding == OA_SPRITE_RAW) {
            copy_rect(surface, raw_sprite_view(*sprite), src, dst);
        } else {
            decode_rle_rows(surface.pixels, surface.pitch, dst, sprite_rows(*sprite, src.y2), src);
        }
    });
}

void draw_sprite_gray(Surface* target, const Sprite* sprite, int32_t x, int32_t y) noexcept {
    const DisplayContext* display = display_context();
    if (display == nullptr || (display->flags & display_flag_gray_table) == 0 ||
        sprite == nullptr || sprite->encoding != OA_SPRITE_RAW) {
        return;
    }
    const uint8_t* table = display->gray_table;
    with_target(target, [&](Surface& surface) {
        if (sprite->child_count != 0) {
            for (int32_t i = 0; i < sprite->child_count; ++i) {
                draw_sprite_gray(&surface, child_of(*sprite, i), x, y);
            }
            return;
        }
        Rect32 src{};
        Rect32 dst{};
        if (!place_sprite(surface, *sprite, x, y, src, dst)) {
            return;
        }
        Surface view{};
        surface_from_sprite(view, *sprite);
        remap_under_mask(surface, view, src, dst, sprite->key, table);
    });
}

void erase_sprite_dithered(
    Surface* target, const Sprite* sprite, int32_t x, int32_t y, int32_t phase
) noexcept {
    if (sprite == nullptr || sprite->encoding != OA_SPRITE_RAW) {
        return;
    }
    with_target(target, [&](Surface& surface) {
        if (sprite->child_count != 0) {
            for (int32_t i = 0; i < sprite->child_count; ++i) {
                erase_sprite_dithered(&surface, child_of(*sprite, i), x, y, phase);
            }
            return;
        }
        Rect32 src{};
        Rect32 dst{};
        if (!place_sprite(surface, *sprite, x, y, src, dst)) {
            return;
        }
        const auto* pixels = static_cast<const uint8_t*>(sprite->data);
        int32_t src_row = src.y1;
        for (int32_t row = dst.y1; row <= dst.y2; ++row, ++src_row) {
            uint8_t* out = pixel_at(surface, dst.x1, row);
            const uint8_t* in =
                pixels + static_cast<std::ptrdiff_t>(sprite->width) * src_row + src.x1;
            const uint8_t* last = out + (dst.x2 - dst.x1);
            if (((row + dst.x1 + phase) & 1) != 0) {
                ++out;
                ++in;
            }
            for (; out <= last; out += 2, in += 2) {
                if (*in != sprite->key) {
                    *out = 0;
                }
            }
        }
    });
}

void sprite_from_surface(Sprite& sprite, const Surface& surface) noexcept {
    sprite.width = static_cast<uint16_t>(surface.pitch);
    sprite.height = static_cast<uint16_t>(surface.height);
    sprite.data = surface.pixels;
    sprite.origin_x = surface.origin_x;
    sprite.origin_y = surface.origin_y;
    sprite.key = 0xFF;
    sprite.encoding = OA_SPRITE_RAW;
    sprite.child_count = 0;
    sprite.child_draw_mode = 0;
}

void sprite_from_surface_keyed(Sprite& sprite, const Surface& surface, uint8_t key) noexcept {
    sprite_from_surface(sprite, surface);
    sprite.key = key;
}

uint16_t sprite_width(const Sprite& sprite) noexcept {
    return sprite.width;
}

void clear_sprite(Sprite& sprite, uint8_t color) noexcept {
    if (sprite.encoding != OA_SPRITE_RAW) {
        return;
    }
    const std::size_t size = static_cast<std::size_t>(sprite.width) * sprite.height;
    auto* pixels = static_cast<uint8_t*>(sprite.data);
    for (std::size_t i = 0; i < size; ++i) {
        pixels[i] = color;
    }
    if (sprite.aux != nullptr) {
        auto* aux = static_cast<uint8_t*>(sprite.aux);
        for (std::size_t i = 0; i < size; ++i) {
            aux[i] = 0;
        }
    }
}

void capture_under_sprite(
    const Surface& screen, const Sprite& sprite, int32_t x, int32_t y
) noexcept {
    Surface view{};
    surface_from_sprite(view, sprite);
    const Rect32 dst_pos{0, 0, view.width - 1, view.height - 1};
    const Rect32 src_rect{
        x - sprite.origin_x,
        y - sprite.origin_y,
        view.width - sprite.origin_x - 1 + x,
        view.height - sprite.origin_y - 1 + y
    };
    copy_rect(view, screen, src_rect, dst_pos);
}

void draw_displacement_sprite(Surface* target, Sprite& sprite, int32_t x, int32_t y) noexcept {
    const std::size_t size = static_cast<std::size_t>(sprite.width) * sprite.height;
    auto* background = static_cast<uint8_t*>(sprite.aux);
    const auto* offsets = static_cast<const uint8_t*>(sprite.data);
    if (target != nullptr) {
        Sprite captured = sprite;
        captured.data = sprite.aux;
        capture_under_sprite(*target, captured, x, y);
    }
    uint8_t* out = background + size;
    for (std::size_t i = 0; i < size; ++i) {
        const auto offset =
            static_cast<int16_t>(static_cast<uint16_t>(offsets[i * 2] | (offsets[i * 2 + 1] << 8)));
        out[i] = offset == displacement_transparent
                     ? sprite.key
                     : background[static_cast<std::ptrdiff_t>(i) + offset];
    }
    void* saved = sprite.data;
    sprite.data = out;
    draw_sprite(target, &sprite, x, y);
    sprite.data = saved;
}

void clear_unkeyed_pixels(Sprite& sprite) noexcept {
    auto* pixels = static_cast<uint8_t*>(sprite.data);
    const std::size_t size = static_cast<std::size_t>(sprite.height) * sprite.width;
    for (std::size_t i = 0; i < size; ++i) {
        if (pixels[i] != sprite.key) {
            pixels[i] = 0;
        }
    }
}

void stamp_sprite_mask(const Sprite& src, Sprite& dst, int32_t x, int32_t y) noexcept {
    int32_t src_col = x + (src.origin_x - dst.origin_x);
    int32_t dst_row = (dst.origin_y - src.origin_y) - y;
    int32_t src_row = 0;
    if (dst_row < 0) {
        src_row = -dst_row;
        dst_row = 0;
    }
    int32_t dst_col = -src_col;
    if (dst_col < 0) {
        dst_col = 0;
    } else {
        src_col = 0;
    }
    int32_t width = dst.width;
    if (static_cast<int32_t>(src.width) - src_col <= static_cast<int32_t>(dst.width)) {
        width = src.width - src_col;
    }
    if (width <= 0) {
        return;
    }
    const auto* in_pixels = static_cast<const uint8_t*>(src.data);
    auto* out_pixels = static_cast<uint8_t*>(dst.data);
    for (; src_row < src.height && dst_row < dst.height; ++dst_row, ++src_row) {
        uint8_t* out = out_pixels + static_cast<std::ptrdiff_t>(dst.width) * dst_row + dst_col;
        const uint8_t* in = in_pixels + static_cast<std::ptrdiff_t>(src.width) * src_row + src_col;
        for (int32_t i = 0; i < width; ++i) {
            if (in[i] != src.key) {
                out[i] = dst.key;
            }
        }
    }
}

} // namespace oa::present
