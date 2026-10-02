// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/present/world_renderer/unit_renderer.hpp"
#include "oa/base/game_math.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <numeric>
#include <span>
#include <cctype>
#include <limits>
#include <unordered_set>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <vector>
#include <cstdint>

namespace oa::present::world_renderer {
using base::game_math::truncate_low32;

namespace {

struct Point {
    int32_t x{}, y{}, z{}, shade{};
};

// Gouraud light direction, then *5, truncation and & 0x1f.
constexpr float kShadeLightX = -0.8F;
constexpr float kShadeLightY = 1.0F;
constexpr float kShadeLightZ = 0.25F;
constexpr float kShadeLightScale = 5.0F;
// Brightness step added once per 256-entry shade row.
constexpr double kShadeTableStep = 0.06875;

struct ClipBounds {
    int32_t left{}, top{}, right{}, bottom{};
};

struct DepthBuffer {
    uint32_t width = 0;
    uint32_t height = 0;
    int32_t origin_x = 0;
    int32_t origin_y = 0;
    uint8_t* samples = nullptr;
    std::size_t sample_count = 0;
};

bool write_depth_pixel(
    Surface& surface,
    DepthBuffer& depth,
    int x,
    int y,
    int32_t depth_fixed,
    const std::array<uint8_t, 3>& color,
    const ClipBounds& clip
) {
    if (x < clip.left || y < clip.top || x >= clip.right || y >= clip.bottom)
        return false;
    if (x < 0 || y < 0 || x >= static_cast<int>(surface.width) ||
        y >= static_cast<int>(surface.height))
        return false;
    const auto local_x = x - depth.origin_x;
    const auto local_y = y - depth.origin_y;
    if (local_x < 0 || local_y < 0 || local_x >= static_cast<int>(depth.width) ||
        local_y >= static_cast<int>(depth.height))
        return false;
    const auto zi = static_cast<std::size_t>(local_y) * static_cast<std::size_t>(depth.width) +
                    static_cast<std::size_t>(local_x);
    // Compare the interpolated 16.16 high byte against the depth plane.
    const auto sample = static_cast<uint8_t>(static_cast<uint32_t>(depth_fixed) >> 16U);
    if (zi >= depth.sample_count || depth.samples[zi] > sample)
        return false;
    depth.samples[zi] = sample;
    const auto offset =
        (static_cast<std::size_t>(y) * surface.width + static_cast<std::size_t>(x)) * 3U;
    std::copy(
        color.begin(), color.end(), surface.rgb.begin() + static_cast<std::ptrdiff_t>(offset)
    );
    return true;
}

struct IndexedSprite {
    int32_t width = 0, height = 0, origin_x = 0, origin_y = 0;
    uint8_t transparent = 1;
    std::vector<uint8_t> pixels;
    std::vector<uint8_t> aux;
};

uint8_t nearest_palette_index(const PaletteBytes& palette, int r, int g, int b) {
    uint8_t best = 0;
    auto best_d = std::numeric_limits<int64_t>::max();
    const auto count = palette.size() / oa::palette_entry_bytes;
    for (std::size_t i = 0; i < count && i < 256; ++i) {
        const auto o = i * oa::palette_entry_bytes;
        const auto dr = static_cast<int64_t>(palette[o]) - r;
        const auto dg = static_cast<int64_t>(palette[o + 1]) - g;
        const auto db = static_cast<int64_t>(palette[o + 2]) - b;
        const auto d = dr * dr + dg * dg + db * db;
        if (d < best_d) {
            best_d = d;
            best = static_cast<uint8_t>(i);
        }
    }
    return best;
}

std::array<uint8_t, 32 * 256> make_shade_table(const PaletteBytes& palette) {
    // 32 rows of 256 remaps, brightness += 0.06875 per row, nearest color.
    std::array<uint8_t, 32 * 256> table{};
    for (int row = 0; row < 32; ++row) {
        const auto brightness = static_cast<double>(row) * kShadeTableStep;
        for (int color = 0; color < 256; ++color) {
            const auto o = static_cast<std::size_t>(color) * oa::palette_entry_bytes;
            int r = 0, g = 0, b = 0;
            if (o + 2 < palette.size()) {
                r = truncate_low32(static_cast<double>(palette[o]) * brightness);
                g = truncate_low32(static_cast<double>(palette[o + 1]) * brightness);
                b = truncate_low32(static_cast<double>(palette[o + 2]) * brightness);
                r = std::clamp(r, 0, 255);
                g = std::clamp(g, 0, 255);
                b = std::clamp(b, 0, 255);
            }
            table[static_cast<std::size_t>(row) * 256U + static_cast<std::size_t>(color)] =
                nearest_palette_index(palette, r, g, b);
        }
    }
    return table;
}

std::vector<int32_t> vertex_shades(
    const sim::model_runtime::PieceState& piece, const formats::objects3d::Object& object
) {
    const auto n = piece.transformed_vertices.size();
    std::vector<int32_t> shades(n, 0xf);
    if ((piece.flags & static_cast<uint16_t>(sim::model_runtime::PieceFlag::shaded)) == 0 || n == 0)
        return shades;
    std::vector<float> nx(n), ny(n), nz(n);
    std::vector<int> count(n);
    for (const auto& primitive : object.primitives) {
        if (primitive.vertex_indices.size() < 3)
            continue;
        const auto i0 = primitive.vertex_indices[0];
        const auto i1 = primitive.vertex_indices[1];
        const auto i2 = primitive.vertex_indices[2];
        if (i0 >= n || i1 >= n || i2 >= n)
            continue;
        float fx = 0.0F, fy = 1.0F, fz = 0.0F;
        if (i0 != i1 && i1 != i2 && i2 != i0) {
            const auto& v0 = piece.transformed_vertices[i0];
            const auto& v1 = piece.transformed_vertices[i1];
            const auto& v2 = piece.transformed_vertices[i2];
            // Edge A = v0 - v1, edge B = v2 - v1, then their cross product.
            const auto ax = static_cast<float>(v0.x - v1.x);
            const auto ay = static_cast<float>(v0.y - v1.y);
            const auto az = static_cast<float>(v0.z - v1.z);
            const auto bx = static_cast<float>(v2.x - v1.x);
            const auto by = static_cast<float>(v2.y - v1.y);
            const auto bz = static_cast<float>(v2.z - v1.z);
            fx = ay * bz - az * by;
            fy = az * bx - ax * bz;
            fz = ax * by - ay * bx;
            const auto len = std::sqrt(fx * fx + fy * fy + fz * fz);
            if (len > 0.0F) {
                fx /= len;
                fy /= len;
                fz /= len;
            } else {
                fx = 0.0F;
                fy = 1.0F;
                fz = 0.0F;
            }
        }
        for (const auto index : primitive.vertex_indices) {
            if (index >= n)
                continue;
            nx[index] += fx;
            ny[index] += fy;
            nz[index] += fz;
            ++count[index];
        }
    }
    for (std::size_t i = 0; i < n; ++i) {
        if (count[i] == 0)
            continue;
        auto x = nx[i] / static_cast<float>(count[i]);
        auto y = ny[i] / static_cast<float>(count[i]);
        auto z = nz[i] / static_cast<float>(count[i]);
        const auto len = std::sqrt(x * x + y * y + z * z);
        if (len > 0.0F) {
            x /= len;
            y /= len;
            z /= len;
        }
        const auto dot = x * kShadeLightX + y * kShadeLightY + z * kShadeLightZ;
        shades[i] = truncate_low32(static_cast<double>(dot * kShadeLightScale)) & 0x1f;
    }
    return shades;
}

std::array<uint8_t, 3> shaded_rgb(
    const PaletteBytes& palette,
    const std::array<uint8_t, 32 * 256>& table,
    uint8_t color,
    int32_t shade_fixed
) {
    auto row = static_cast<int>(static_cast<uint32_t>(shade_fixed) >> 16U);
    row = std::clamp(row, 0, 31);
    const auto index = table[static_cast<std::size_t>(row) * 256U + color];
    const auto o = static_cast<std::size_t>(index) * oa::palette_entry_bytes;
    if (o + 2 >= palette.size())
        return {0, 0, 0};
    return {palette[o], palette[o + 1], palette[o + 2]};
}

bool write_indexed_pixel(IndexedSprite& sprite, int x, int y, int32_t depth_fixed, uint8_t index) {
    if (x < 0 || y < 0 || x >= sprite.width || y >= sprite.height)
        return false;
    const auto zi = static_cast<std::size_t>(y) * static_cast<std::size_t>(sprite.width) +
                    static_cast<std::size_t>(x);
    const auto sample = static_cast<uint8_t>(static_cast<uint32_t>(depth_fixed) >> 16U);
    if (zi >= sprite.aux.size() || sprite.aux[zi] > sample)
        return false;
    sprite.aux[zi] = sample;
    sprite.pixels[zi] = index;
    return true;
}

void remap_depth_band(IndexedSprite& sprite, uint8_t threshold, int above, int below, int band) {
    const uint8_t lo = threshold < 4 ? 0 : static_cast<uint8_t>(threshold - 4);
    const auto count = sprite.pixels.size();
    for (std::size_t i = 0; i < count; ++i) {
        if (sprite.pixels[i] == sprite.transparent)
            continue;
        int color = below;
        if (lo <= sprite.aux[i]) {
            color = above;
            if (sprite.aux[i] < threshold)
                color = band;
        }
        if (color == -2)
            sprite.pixels[i] = sprite.transparent;
        else if (color != -1)
            sprite.pixels[i] = static_cast<uint8_t>(color);
    }
}

void apply_build_shimmer(
    IndexedSprite& sprite, float build_remaining, uint8_t wave_a, uint8_t wave_b
) {
    if (sprite.aux.empty() || build_remaining == 0.0F)
        return;
    auto progress = truncate_low32(static_cast<double>(build_remaining) * 255.0);
    int above = -2, below = -2, band = static_cast<int>(wave_a);
    uint8_t threshold = 0;
    if (progress < 0xec) {
        if (progress < 0xc9) {
            if (progress < 0x74) {
                if (progress < 0x1f) {
                    const auto scaled = progress * 0xff;
                    threshold = static_cast<uint8_t>(
                        scaled / 0x1e + ((scaled < 0 && scaled % 0x1e) ? -1 : 0)
                    );
                    remap_depth_band(sprite, threshold, -1, -1, wave_a);
                    return;
                }
                above = wave_a;
                below = -1;
                threshold = static_cast<uint8_t>(((0x1e - progress) * 0xff) / 0x55 - 1);
                band = wave_b;
            } else {
                above = -2;
                below = wave_a;
                threshold = static_cast<uint8_t>(((0x73 - progress) * 0xff) / 0x55 - 1);
                band = wave_b;
            }
        } else {
            threshold = static_cast<uint8_t>(((progress - 200) * 0xff) / 0x23);
            above = -2;
            below = -2;
            band = wave_a;
        }
    } else {
        threshold = static_cast<uint8_t>(((progress - 0xeb) * 0xff) / 0x14);
        above = -2;
        below = -2;
        band = wave_a;
    }
    remap_depth_band(sprite, threshold, above, below, band);
}

void blit_indexed_sprite(
    Surface& destination,
    const IndexedSprite& sprite,
    const PaletteBytes& palette,
    const ClipBounds& clip
) {
    for (int y = 0; y < sprite.height; ++y) {
        for (int x = 0; x < sprite.width; ++x) {
            const auto index =
                sprite.pixels
                    [static_cast<std::size_t>(y) * static_cast<std::size_t>(sprite.width) +
                     static_cast<std::size_t>(x)];
            if (index == sprite.transparent)
                continue;
            const int dx = sprite.origin_x + x;
            const int dy = sprite.origin_y + y;
            if (dx < clip.left || dy < clip.top || dx >= clip.right || dy >= clip.bottom)
                continue;
            if (dx < 0 || dy < 0 || dx >= static_cast<int>(destination.width) ||
                dy >= static_cast<int>(destination.height))
                continue;
            const auto pal = static_cast<std::size_t>(index) * oa::palette_entry_bytes;
            const auto out =
                (static_cast<std::size_t>(dy) * destination.width + static_cast<std::size_t>(dx)) *
                3U;
            destination.rgb[out] = palette[pal];
            destination.rgb[out + 1] = palette[pal + 1];
            destination.rgb[out + 2] = palette[pal + 2];
        }
    }
}

std::string folded(std::string_view value) {
    std::string result(value);
    for (auto& c : result)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return result;
}

int32_t wrap_add(int32_t a, int32_t b) {
    return std::bit_cast<int32_t>(std::bit_cast<uint32_t>(a) + std::bit_cast<uint32_t>(b));
}

int32_t wrap_subtract(int32_t a, int32_t b) {
    return std::bit_cast<int32_t>(std::bit_cast<uint32_t>(a) - std::bit_cast<uint32_t>(b));
}

int32_t wrap_multiply(int32_t a, int32_t b) {
    return std::bit_cast<int32_t>(std::bit_cast<uint32_t>(a) * std::bit_cast<uint32_t>(b));
}

int32_t signed_high_word(int32_t value) {
    return static_cast<int16_t>(std::bit_cast<uint32_t>(value) >> 16U);
}

int32_t camera_fixed(int32_t pixels) {
    return std::bit_cast<int32_t>(std::bit_cast<uint32_t>(pixels) << 16U);
}

Point project(
    const formats::objects3d::FixedVector3& vertex,
    const UnitProjection& projection,
    int32_t depth_extra
) {
    const auto x = wrap_subtract(
        wrap_add(vertex.x, projection.world_position.x), camera_fixed(projection.camera_pixel_x)
    );
    const auto z = wrap_subtract(
        wrap_subtract(projection.world_position.z, camera_fixed(projection.camera_pixel_y)),
        vertex.z
    );
    const auto y = wrap_add(vertex.y, projection.world_position.y);
    const auto y_hi = signed_high_word(y);
    // screen_y = (-Z)_hi - (Y_hi >> 1); depth = Y_hi + 0x32 [+ 0x4b].
    auto sx = signed_high_word(x);
    auto sy = signed_high_word(z) - (y_hi >> 1);
    if (projection.scale != 1.0F && projection.scale != 0.0F) {
        sx = static_cast<int32_t>(std::lround(static_cast<double>(sx) * projection.scale));
        sy = static_cast<int32_t>(std::lround(static_cast<double>(sy) * projection.scale));
    }
    return {
        sx + projection.destination_origin_x,
        sy + projection.destination_origin_y,
        y_hi + 0x32 + depth_extra
    };
}

/// Projects a model vertex onto the ground for the unit's shadow.
///
/// The shadow leans with height: from the shadow's ground point, screen x
/// is X_hi + (Y_hi >> 2) and screen y is (-Z)_hi - (Y_hi >> 2), where _hi
/// is the signed high word of a 16.16 coordinate. That ground point sits 5
/// pixels right of the unit's: its screen x is the position relative to
/// the camera plus 0x85, where the unit's is plus the battlefield's left
/// edge, 0x80.
///
/// @param vertex transformed model vertex, signed 16.16
/// @param projection the unit's world position, camera and destination origin
/// @param ground_height ground height under the unit, 16.16
/// @return the destination pixel, with depth 0
Point project_shadow(
    const formats::objects3d::FixedVector3& vertex,
    const UnitProjection& projection,
    int32_t ground_height
) {
    const auto x_hi = signed_high_word(vertex.x);
    const auto y_hi = signed_high_word(vertex.y);
    const auto z_hi = signed_high_word(wrap_subtract(0, vertex.z));
    auto ox = x_hi + (y_hi >> 2);
    auto oy = z_hi - (y_hi >> 2);
    const auto gx =
        wrap_subtract(projection.world_position.x, camera_fixed(projection.camera_pixel_x));
    const auto gz =
        wrap_subtract(projection.world_position.z, camera_fixed(projection.camera_pixel_y));
    const auto gy_hi = signed_high_word(ground_height);
    auto base_x = signed_high_word(gx) + 5;
    auto base_y = signed_high_word(gz) - (gy_hi >> 1);
    if (projection.scale != 1.0F && projection.scale != 0.0F) {
        ox = static_cast<int32_t>(std::lround(static_cast<double>(ox) * projection.scale));
        oy = static_cast<int32_t>(std::lround(static_cast<double>(oy) * projection.scale));
        base_x = static_cast<int32_t>(std::lround(static_cast<double>(base_x) * projection.scale));
        base_y = static_cast<int32_t>(std::lround(static_cast<double>(base_y) * projection.scale));
    }
    return {
        base_x + ox + projection.destination_origin_x,
        base_y + oy + projection.destination_origin_y,
        0
    };
}

thread_local std::vector<uint8_t> shadow_frame_mask;
thread_local uint32_t shadow_frame_width = 0;
thread_local uint32_t shadow_frame_height = 0;

void darken_shadow_pixel(
    Surface& surface,
    const ClipBounds& clip,
    int x,
    int y,
    uint8_t* coverage,
    int32_t origin_x,
    int32_t origin_y,
    uint32_t width,
    uint32_t height
) {
    if (x < clip.left || y < clip.top || x >= clip.right || y >= clip.bottom)
        return;
    if (x < 0 || y < 0 || x >= static_cast<int>(surface.width) ||
        y >= static_cast<int>(surface.height))
        return;
    const auto lx = x - origin_x;
    const auto ly = y - origin_y;
    if (lx < 0 || ly < 0 || lx >= static_cast<int>(width) || ly >= static_cast<int>(height))
        return;
    auto& cell = coverage[static_cast<std::size_t>(ly) * width + static_cast<std::size_t>(lx)];
    if (cell != 0)
        return;
    cell = 1;
    if (shadow_frame_width != 0 && shadow_frame_height != 0) {
        if (x < 0 || y < 0 || static_cast<uint32_t>(x) >= shadow_frame_width ||
            static_cast<uint32_t>(y) >= shadow_frame_height)
            return;
        auto& seen = shadow_frame_mask
            [static_cast<std::size_t>(y) * shadow_frame_width + static_cast<std::size_t>(x)];
        if (seen != 0)
            return;
        seen = 1;
    }
    auto* pixel = surface.rgb.data() +
                  (static_cast<std::size_t>(y) * surface.width + static_cast<std::size_t>(x)) * 3U;
    // Shadow remap of silhouette 0 onto dest: portable RGB-halve, once per pixel.
    pixel[0] = static_cast<uint8_t>(pixel[0] >> 1);
    pixel[1] = static_cast<uint8_t>(pixel[1] >> 1);
    pixel[2] = static_cast<uint8_t>(pixel[2] >> 1);
}

int32_t edge_slope(const Point&, const Point&);
int32_t initial_fixed_x(int32_t);
thread_local std::vector<uint8_t> depth_scratch;

void fill_shadow_polygon(
    Surface& surface,
    const ClipBounds& clip,
    std::span<const Point> points,
    uint8_t* coverage,
    int32_t origin_x,
    int32_t origin_y,
    uint32_t width,
    uint32_t height
) {
    if (points.size() < 3)
        return;
    std::size_t minimum = 0, maximum = 0;
    for (std::size_t i = 1; i < points.size(); ++i) {
        if (points[i].y < points[minimum].y)
            minimum = i;
        if (points[i].y > points[maximum].y)
            maximum = i;
    }
    const auto top = std::max(clip.top, points[minimum].y);
    const auto bottom = std::min(clip.bottom, points[maximum].y);
    if (bottom <= top)
        return;

    struct Span {
        int32_t left = std::numeric_limits<int32_t>::max();
        int32_t right = std::numeric_limits<int32_t>::min();
    };

    thread_local std::vector<Span> spans;
    spans.assign(static_cast<std::size_t>(bottom - top), {});
    const auto trace = [&](bool forward) {
        std::size_t current = minimum;
        while (current != maximum) {
            const auto next = forward ? (current + 1 == points.size() ? 0 : current + 1)
                                      : (current == 0 ? points.size() - 1 : current - 1);
            const auto& from = points[current];
            const auto& to = points[next];
            if (to.y > from.y) {
                const auto step = edge_slope(from, to);
                auto x = initial_fixed_x(from.x);
                auto y = from.y;
                if (y < top) {
                    x = wrap_add(x, wrap_multiply(top - y, step));
                    y = top;
                }
                for (const auto end = std::min(to.y, bottom); y < end; ++y) {
                    auto& span = spans[static_cast<std::size_t>(y - top)];
                    const auto px = x >> 16;
                    span.left = std::min(span.left, px);
                    span.right = std::max(span.right, px);
                    x = wrap_add(x, step);
                }
            }
            current = next;
        }
    };
    trace(true);
    trace(false);
    for (auto y = top; y < bottom; ++y) {
        const auto& span = spans[static_cast<std::size_t>(y - top)];
        if (span.right <= span.left)
            continue;
        for (auto x = span.left; x < span.right; ++x)
            darken_shadow_pixel(surface, clip, x, y, coverage, origin_x, origin_y, width, height);
    }
}

void stamp_instance_shadow(
    Surface& destination,
    const sim::model_runtime::Instance& instance,
    const UnitProjection& projection,
    int32_t ground_height,
    const ClipBounds& clip
) {
    const auto pieces = instance.pieces();
    const auto& model = instance.model();
    int32_t min_x = std::numeric_limits<int32_t>::max();
    int32_t min_y = std::numeric_limits<int32_t>::max();
    int32_t max_x = std::numeric_limits<int32_t>::min();
    int32_t max_y = std::numeric_limits<int32_t>::min();
    bool any = false;
    for (const auto& piece : pieces) {
        if ((piece.flags & static_cast<uint16_t>(sim::model_runtime::PieceFlag::visible)) == 0)
            continue;
        if (piece.object_index >= model.objects.size())
            continue;
        for (const auto& vertex : piece.transformed_vertices) {
            const auto p = project_shadow(vertex, projection, ground_height);
            min_x = std::min(min_x, p.x);
            min_y = std::min(min_y, p.y);
            max_x = std::max(max_x, p.x);
            max_y = std::max(max_y, p.y);
            any = true;
        }
    }
    if (!any)
        return;
    const auto origin = project_shadow({0, 0, 0}, projection, ground_height);
    const auto scale = projection.scale == 0.0F ? 1.0F : projection.scale;
    const auto extent = std::max(64, static_cast<int>(std::lround(160.0 * scale)));
    min_x = std::max(min_x, origin.x - extent);
    min_y = std::max(min_y, origin.y - extent);
    max_x = std::min(max_x, origin.x + extent);
    max_y = std::min(max_y, origin.y + extent);
    min_x = std::max(min_x - 2, clip.left);
    min_y = std::max(min_y - 2, clip.top);
    max_x = std::min(max_x + 3, clip.right);
    max_y = std::min(max_y + 3, clip.bottom);
    if (max_x <= min_x || max_y <= min_y)
        return;
    const auto width = static_cast<uint32_t>(max_x - min_x);
    const auto height = static_cast<uint32_t>(max_y - min_y);
    const auto count = static_cast<std::size_t>(width) * height;
    if (depth_scratch.size() < count)
        depth_scratch.resize(count);
    std::fill_n(depth_scratch.begin(), count, static_cast<uint8_t>(0));
    for (std::size_t reverse = pieces.size(); reverse != 0; --reverse) {
        const auto& piece = pieces[reverse - 1];
        if ((piece.flags & static_cast<uint16_t>(sim::model_runtime::PieceFlag::visible)) == 0)
            continue;
        if (piece.object_index >= model.objects.size())
            continue;
        const auto& object = model.objects[piece.object_index];
        for (const auto& primitive : object.primitives) {
            if (object.selection_primitive != -1 &&
                &primitive ==
                    &object.primitives[static_cast<std::size_t>(object.selection_primitive)])
                continue;
            std::vector<Point> points;
            points.reserve(primitive.vertex_indices.size());
            bool ok = true;
            for (const auto vertex : primitive.vertex_indices) {
                if (vertex >= piece.transformed_vertices.size()) {
                    ok = false;
                    break;
                }
                points.push_back(
                    project_shadow(piece.transformed_vertices[vertex], projection, ground_height)
                );
            }
            if (!ok || points.size() < 3)
                continue;
            fill_shadow_polygon(
                destination, clip, points, depth_scratch.data(), min_x, min_y, width, height
            );
        }
    }
}

/// Sizes and clears the depth plane for one unit or feature.
///
/// The plane covers only the rectangle the visible pieces project to,
/// widened by 2 pixels up and left and 3 down and right and cut to the
/// clip, and every sample starts at 1. As in 3.1c, each unit or feature
/// has a plane of its own, so one unit's pixels never depth-test against
/// another's; a full-screen plane at 1080p would also clear about 2 MiB
/// per unit or feature.
///
/// @param instance model instance whose transformed vertices are projected
/// @param projection the unit's world position, camera and destination origin
/// @param depth_extra depth added to every projected vertex
/// @param clip destination clip rectangle
/// @param[out] depth the plane, backed by per-thread scratch storage
/// @return false when no piece is visible or the rectangle lies outside the clip
bool prepare_unit_depth(
    const sim::model_runtime::Instance& instance,
    const UnitProjection& projection,
    int32_t depth_extra,
    const ClipBounds& clip,
    DepthBuffer& depth
) {
    int32_t min_x = std::numeric_limits<int32_t>::max();
    int32_t min_y = std::numeric_limits<int32_t>::max();
    int32_t max_x = std::numeric_limits<int32_t>::min();
    int32_t max_y = std::numeric_limits<int32_t>::min();
    bool any = false;
    const auto pieces = instance.pieces();
    const auto& model = instance.model();
    for (const auto& piece : pieces) {
        if ((piece.flags & static_cast<uint16_t>(sim::model_runtime::PieceFlag::visible)) == 0)
            continue;
        if (piece.object_index >= model.objects.size())
            continue;
        for (const auto& vertex : piece.transformed_vertices) {
            const auto p = project(vertex, projection, depth_extra);
            min_x = std::min(min_x, p.x);
            min_y = std::min(min_y, p.y);
            max_x = std::max(max_x, p.x);
            max_y = std::max(max_y, p.y);
            any = true;
        }
    }
    if (!any)
        return false;
    min_x = std::max(min_x - 2, clip.left);
    min_y = std::max(min_y - 2, clip.top);
    max_x = std::min(max_x + 3, clip.right);
    max_y = std::min(max_y + 3, clip.bottom);
    if (max_x <= min_x || max_y <= min_y)
        return false;
    const auto width = static_cast<uint32_t>(max_x - min_x);
    const auto height = static_cast<uint32_t>(max_y - min_y);
    const auto count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    if (depth_scratch.size() < count)
        depth_scratch.resize(count);
    std::fill_n(depth_scratch.begin(), count, static_cast<uint8_t>(1));
    depth.width = width;
    depth.height = height;
    depth.origin_x = min_x;
    depth.origin_y = min_y;
    depth.samples = depth_scratch.data();
    depth.sample_count = count;
    return true;
}

int32_t edge_slope(const Point& from, const Point& to) {
    const auto dx = wrap_subtract(to.x, from.x);
    const auto product = std::bit_cast<int32_t>(std::bit_cast<uint32_t>(dx) * 0x1'0000U);
    return product / (to.y - from.y);
}

int32_t initial_fixed_x(int32_t x) {
    return std::bit_cast<int32_t>(std::bit_cast<uint32_t>(x) * 0x1'0000U + 0xffffU);
}

void fill_polygon(
    Surface& surface,
    DepthBuffer& depth,
    std::span<const Point> points,
    const std::array<uint8_t, 3>& color,
    const ClipBounds& clip,
    const std::array<uint8_t, 32 * 256>* shade_table = nullptr,
    const PaletteBytes* palette = nullptr,
    uint8_t color_index = 0
) {
    if (points.empty() || surface.width == 0 || surface.height == 0)
        return;
    std::size_t minimum = 0, maximum = 0;
    for (std::size_t index = 1; index < points.size(); ++index) {
        if (points[index].y < points[minimum].y)
            minimum = index;
        if (points[index].y > points[maximum].y)
            maximum = index;
    }
    const auto top = std::max(clip.top, points[minimum].y);
    const auto bottom = std::min(clip.bottom, points[maximum].y);
    if (bottom <= top)
        return;

    struct Span {
        int32_t left{}, right{}, left_z{}, right_z{}, left_s{}, right_s{};
    };

    thread_local std::vector<Span> spans;
    spans.assign(static_cast<std::size_t>(bottom - top), {});

    const auto trace = [&](bool forward, bool right_edge) {
        std::size_t current = minimum;
        while (current != maximum) {
            const auto next = forward ? (current + 1 == points.size() ? 0 : current + 1)
                                      : (current == 0 ? points.size() - 1 : current - 1);
            const auto& from = points[current];
            const auto& to = points[next];
            if (to.y > from.y) {
                const auto step = edge_slope(from, to);
                const auto dz = wrap_multiply(to.z - from.z, 0x10000) / (to.y - from.y);
                const auto ds = wrap_multiply(to.shade - from.shade, 0x10000) / (to.y - from.y);
                auto x = initial_fixed_x(from.x);
                auto z = wrap_multiply(from.z, 0x10000);
                auto s = wrap_multiply(from.shade, 0x10000);
                auto y = from.y;
                if (y < top) {
                    const auto d = top - y;
                    x = wrap_add(x, wrap_multiply(d, step));
                    z = wrap_add(z, wrap_multiply(d, dz));
                    s = wrap_add(s, wrap_multiply(d, ds));
                    y = top;
                }
                for (const auto end = std::min(to.y, bottom); y < end; ++y) {
                    auto& span = spans[static_cast<std::size_t>(y - top)];
                    if (right_edge) {
                        span.right = x >> 16;
                        span.right_z = z;
                        span.right_s = s;
                    } else {
                        span.left = x >> 16;
                        span.left_z = z;
                        span.left_s = s;
                    }
                    x = wrap_add(x, step);
                    z = wrap_add(z, dz);
                    s = wrap_add(s, ds);
                }
            }
            current = next;
        }
    };
    trace(false, false);
    trace(true, true);

    for (auto y = top; y < bottom; ++y) {
        auto span = spans[static_cast<std::size_t>(y - top)];
        const auto raw_span = span.right - span.left;
        if (raw_span <= 0)
            continue;
        const auto dz = wrap_subtract(span.right_z, span.left_z) / raw_span;
        const auto ds = wrap_subtract(span.right_s, span.left_s) / raw_span;
        auto z = span.left_z;
        auto s = span.left_s;
        if (clip.left > span.left) {
            z = wrap_add(z, wrap_multiply(clip.left - span.left, dz));
            s = wrap_add(s, wrap_multiply(clip.left - span.left, ds));
        }
        span.left = std::max(span.left, clip.left);
        span.right = std::min(span.right, clip.right);
        auto* row = surface.rgb.data() +
                    static_cast<std::size_t>(y) * static_cast<std::size_t>(surface.width) * 3U;
        const auto local_y = y - depth.origin_y;
        if (local_y < 0 || local_y >= static_cast<int>(depth.height))
            continue;
        auto* depth_row = depth.samples + static_cast<std::size_t>(local_y) * depth.width;
        for (auto x = span.left; x < span.right; ++x, z = wrap_add(z, dz), s = wrap_add(s, ds)) {
            const auto local_x = x - depth.origin_x;
            if (local_x < 0 || local_x >= static_cast<int>(depth.width))
                continue;
            const auto sample = static_cast<uint8_t>(static_cast<uint32_t>(z) >> 16U);
            auto& cell = depth_row[static_cast<std::size_t>(local_x)];
            if (cell > sample)
                continue;
            cell = sample;
            const auto rgb = (shade_table != nullptr && palette != nullptr)
                                 ? shaded_rgb(*palette, *shade_table, color_index, s)
                                 : color;
            auto* pixel = row + static_cast<std::size_t>(x) * 3U;
            pixel[0] = rgb[0];
            pixel[1] = rgb[1];
            pixel[2] = rgb[2];
        }
    }
}

void fill_indexed_polygon(IndexedSprite& sprite, std::span<const Point> points, uint8_t index) {
    if (points.empty() || sprite.width <= 0 || sprite.height <= 0)
        return;
    std::size_t minimum = 0, maximum = 0;
    for (std::size_t i = 1; i < points.size(); ++i) {
        if (points[i].y < points[minimum].y)
            minimum = i;
        if (points[i].y > points[maximum].y)
            maximum = i;
    }
    const auto top = std::max(0, points[minimum].y);
    const auto bottom = std::min(sprite.height, points[maximum].y);
    if (bottom <= top)
        return;

    struct Span {
        int32_t left{}, right{}, left_z{}, right_z{};
    };

    std::vector<Span> spans(static_cast<std::size_t>(bottom - top));
    const auto trace = [&](bool forward, bool right_edge) {
        std::size_t current = minimum;
        while (current != maximum) {
            const auto next = forward ? (current + 1 == points.size() ? 0 : current + 1)
                                      : (current == 0 ? points.size() - 1 : current - 1);
            const auto& from = points[current];
            const auto& to = points[next];
            if (to.y > from.y) {
                const auto step = edge_slope(from, to);
                const auto dz = wrap_multiply(to.z - from.z, 0x10000) / (to.y - from.y);
                auto x = initial_fixed_x(from.x);
                auto z = wrap_multiply(from.z, 0x10000);
                auto y = from.y;
                if (y < top) {
                    const auto d = top - y;
                    x = wrap_add(x, wrap_multiply(d, step));
                    z = wrap_add(z, wrap_multiply(d, dz));
                    y = top;
                }
                for (const auto end = std::min(to.y, bottom); y < end; ++y) {
                    auto& span = spans[static_cast<std::size_t>(y - top)];
                    if (right_edge) {
                        span.right = x >> 16;
                        span.right_z = z;
                    } else {
                        span.left = x >> 16;
                        span.left_z = z;
                    }
                    x = wrap_add(x, step);
                    z = wrap_add(z, dz);
                }
            }
            current = next;
        }
    };
    trace(false, false);
    trace(true, true);
    for (auto y = top; y < bottom; ++y) {
        auto span = spans[static_cast<std::size_t>(y - top)];
        const auto raw = span.right - span.left;
        if (raw <= 0)
            continue;
        const auto dz = wrap_subtract(span.right_z, span.left_z) / raw;
        auto z = span.left_z;
        if (span.left < 0)
            z = wrap_add(z, wrap_multiply(-span.left, dz));
        span.left = std::max(span.left, 0);
        span.right = std::min(span.right, sprite.width);
        for (auto x = span.left; x < span.right; ++x, z = wrap_add(z, dz))
            write_indexed_pixel(sprite, x, y, z, index);
    }
}

void texture_indexed_quad(
    IndexedSprite& sprite,
    const std::array<Point, 4>& points,
    const formats::gaf::RenderedFrame& texture
) {
    if (texture.width == 0 || texture.height == 0)
        return;

    // Sample GAF indices along the same quad used by texture_quad.
    struct Scan {
        int32_t lx{}, rx{}, lu{}, lv{}, ru{}, rv{}, lz{}, rz{};
    };

    auto minimum = std::size_t{0}, maximum = std::size_t{0};
    for (std::size_t i = 1; i < 4; ++i) {
        if (points[i].y < points[minimum].y)
            minimum = i;
        if (points[i].y > points[maximum].y)
            maximum = i;
    }
    const auto top = std::max(0, points[minimum].y);
    const auto bottom = std::min(sprite.height, points[maximum].y);
    if (bottom <= top)
        return;
    std::vector<Scan> scans(static_cast<std::size_t>(bottom - top));
    const std::array<int32_t, 4> us{0, texture.width - 1, texture.width - 1, 0};
    const std::array<int32_t, 4> vs{0, 0, texture.height - 1, texture.height - 1};
    const auto trace = [&](bool forward, bool right) {
        auto current = minimum;
        while (current != maximum) {
            const auto next = forward ? ((current + 1) & 3U) : ((current + 3) & 3U);
            if (points[next].y > points[current].y) {
                const auto dy = points[next].y - points[current].y;
                auto x = initial_fixed_x(points[current].x);
                auto u = wrap_multiply(us[current], 0x10000);
                auto v = wrap_multiply(vs[current], 0x10000);
                auto z = wrap_multiply(points[current].z, 0x10000);
                const auto sx = edge_slope(points[current], points[next]);
                const auto su = wrap_multiply(us[next] - us[current], 0x10000) / dy;
                const auto sv = wrap_multiply(vs[next] - vs[current], 0x10000) / dy;
                const auto sz = wrap_multiply(points[next].z - points[current].z, 0x10000) / dy;
                auto y = points[current].y;
                if (y < top) {
                    const auto d = top - y;
                    x = wrap_add(x, wrap_multiply(d, sx));
                    u = wrap_add(u, wrap_multiply(d, su));
                    v = wrap_add(v, wrap_multiply(d, sv));
                    z = wrap_add(z, wrap_multiply(d, sz));
                    y = top;
                }
                for (const auto end = std::min(points[next].y, bottom); y < end; ++y) {
                    auto& s = scans[static_cast<std::size_t>(y - top)];
                    if (right) {
                        s.rx = x >> 16;
                        s.ru = u;
                        s.rv = v;
                        s.rz = z;
                    } else {
                        s.lx = x >> 16;
                        s.lu = u;
                        s.lv = v;
                        s.lz = z;
                    }
                    x = wrap_add(x, sx);
                    u = wrap_add(u, su);
                    v = wrap_add(v, sv);
                    z = wrap_add(z, sz);
                }
            }
            current = next;
        }
    };
    trace(false, false);
    trace(true, true);
    for (auto y = top; y < bottom; ++y) {
        auto s = scans[static_cast<std::size_t>(y - top)];
        const auto span = s.rx - s.lx;
        if (span <= 0)
            continue;
        const auto du = wrap_subtract(s.ru, s.lu) / span;
        const auto dv = wrap_subtract(s.rv, s.lv) / span;
        const auto dz = wrap_subtract(s.rz, s.lz) / span;
        auto u = s.lu, v = s.lv, z = s.lz;
        auto left = std::max(0, s.lx), right = std::min(s.rx, sprite.width);
        u = wrap_add(u, wrap_multiply(left - s.lx, du));
        v = wrap_add(v, wrap_multiply(left - s.lx, dv));
        z = wrap_add(z, wrap_multiply(left - s.lx, dz));
        for (auto x = left; x < right;
             ++x, u = wrap_add(u, du), v = wrap_add(v, dv), z = wrap_add(z, dz)) {
            const auto tx = std::clamp(u >> 16, 0, static_cast<int>(texture.width) - 1);
            const auto ty = std::clamp(v >> 16, 0, static_cast<int>(texture.height) - 1);
            const auto ti = static_cast<std::size_t>(ty) * texture.width + tx;
            if (ti < texture.coverage.size() && texture.coverage[ti] == 0)
                continue;
            write_indexed_pixel(sprite, x, y, z, texture.pixels[ti]);
        }
    }
}

void stroke_indexed_polygon(IndexedSprite& sprite, std::span<const Point> points, uint8_t index) {
    if (points.size() < 2)
        return;
    for (std::size_t i = 0; i < points.size(); ++i) {
        auto x0 = points[i].x, y0 = points[i].y;
        auto z0 = wrap_multiply(points[i].z, 0x10000);
        const auto& to = points[(i + 1) % points.size()];
        auto x1 = to.x, y1 = to.y;
        int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
        int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
        int error = dx + dy;
        const auto steps = std::max(dx, std::abs(y1 - y0));
        const auto dz = steps == 0 ? 0 : wrap_multiply(to.z - points[i].z, 0x10000) / steps;
        while (true) {
            write_indexed_pixel(sprite, x0, y0, z0, index);
            if (x0 == x1 && y0 == y1)
                break;
            const auto twice = error * 2;
            if (twice >= dy) {
                error += dy;
                x0 += sx;
            }
            if (twice <= dx) {
                error += dx;
                y0 += sy;
            }
            z0 = wrap_add(z0, dz);
        }
    }
}

void texture_quad(
    Surface& surface,
    DepthBuffer& depth,
    const std::array<Point, 4>& points,
    const formats::gaf::RenderedFrame& texture,
    const PaletteBytes& palette,
    const ClipBounds& clip,
    const std::array<uint8_t, 32 * 256>* shade_table = nullptr
) {
    if (texture.width == 0 || texture.height == 0 ||
        texture.pixels.size() != static_cast<std::size_t>(texture.width) * texture.height)
        return;

    struct Scan {
        int32_t lx{}, rx{}, lu{}, lv{}, ru{}, rv{}, lz{}, rz{}, ls{}, rs{};
    };

    auto minimum = std::size_t{0}, maximum = std::size_t{0};
    for (std::size_t i = 1; i < 4; ++i) {
        if (points[i].y < points[minimum].y)
            minimum = i;
        if (points[i].y > points[maximum].y)
            maximum = i;
    }
    const auto top = std::max(clip.top, points[minimum].y);
    const auto bottom = std::min(clip.bottom, points[maximum].y);
    if (bottom <= top)
        return;
    std::vector<Scan> scans(static_cast<std::size_t>(bottom - top));
    const std::array<int32_t, 4> us{0, texture.width - 1, texture.width - 1, 0};
    const std::array<int32_t, 4> vs{0, 0, texture.height - 1, texture.height - 1};
    const auto trace = [&](bool forward, bool right) {
        auto current = minimum;
        while (current != maximum) {
            const auto next = forward ? ((current + 1) & 3U) : ((current + 3) & 3U);
            if (points[next].y > points[current].y) {
                const auto dy = points[next].y - points[current].y;
                auto x = initial_fixed_x(points[current].x);
                auto u = wrap_multiply(us[current], 0x10000);
                auto v = wrap_multiply(vs[current], 0x10000);
                auto z = wrap_multiply(points[current].z, 0x10000);
                auto sh = wrap_multiply(points[current].shade, 0x10000);
                const auto sx = edge_slope(points[current], points[next]);
                const auto su = wrap_multiply(us[next] - us[current], 0x10000) / dy;
                const auto sv = wrap_multiply(vs[next] - vs[current], 0x10000) / dy;
                const auto sz = wrap_multiply(points[next].z - points[current].z, 0x10000) / dy;
                const auto ss =
                    wrap_multiply(points[next].shade - points[current].shade, 0x10000) / dy;
                auto y = points[current].y;
                if (y < top) {
                    const auto d = top - y;
                    x = wrap_add(x, wrap_multiply(d, sx));
                    u = wrap_add(u, wrap_multiply(d, su));
                    v = wrap_add(v, wrap_multiply(d, sv));
                    z = wrap_add(z, wrap_multiply(d, sz));
                    sh = wrap_add(sh, wrap_multiply(d, ss));
                    y = top;
                }
                for (const auto end = std::min(points[next].y, bottom); y < end; ++y) {
                    auto& s = scans[static_cast<std::size_t>(y - top)];
                    if (right) {
                        s.rx = x >> 16;
                        s.ru = u;
                        s.rv = v;
                        s.rz = z;
                        s.rs = sh;
                    } else {
                        s.lx = x >> 16;
                        s.lu = u;
                        s.lv = v;
                        s.lz = z;
                        s.ls = sh;
                    }
                    x = wrap_add(x, sx);
                    u = wrap_add(u, su);
                    v = wrap_add(v, sv);
                    z = wrap_add(z, sz);
                    sh = wrap_add(sh, ss);
                }
            }
            current = next;
        }
    };
    trace(false, false);
    trace(true, true);
    for (auto y = top; y < bottom; ++y) {
        auto s = scans[static_cast<std::size_t>(y - top)];
        const auto span = s.rx - s.lx;
        if (span <= 0)
            continue;
        const auto du = wrap_subtract(s.ru, s.lu) / span;
        const auto dv = wrap_subtract(s.rv, s.lv) / span;
        const auto dz = wrap_subtract(s.rz, s.lz) / span;
        const auto ds = wrap_subtract(s.rs, s.ls) / span;
        auto u = s.lu, v = s.lv, z = s.lz, sh = s.ls;
        const auto left = std::max(clip.left, s.lx), right = std::min(s.rx, clip.right);
        u = wrap_add(u, wrap_multiply(left - s.lx, du));
        v = wrap_add(v, wrap_multiply(left - s.lx, dv));
        z = wrap_add(z, wrap_multiply(left - s.lx, dz));
        sh = wrap_add(sh, wrap_multiply(left - s.lx, ds));
        for (auto x = left; x < right; ++x,
                  u = wrap_add(u, du),
                  v = wrap_add(v, dv),
                  z = wrap_add(z, dz),
                  sh = wrap_add(sh, ds)) {
            const auto tx = std::clamp(u >> 16, 0, static_cast<int>(texture.width) - 1);
            const auto ty = std::clamp(v >> 16, 0, static_cast<int>(texture.height) - 1);
            const auto ti = static_cast<std::size_t>(ty) * texture.width + tx;
            if (ti < texture.coverage.size() && texture.coverage[ti] == 0)
                continue;
            const auto color = texture.pixels[ti];
            const auto rgb = shade_table != nullptr ? shaded_rgb(palette, *shade_table, color, sh)
                                                    : [&]() -> std::array<uint8_t, 3> {
                const auto po = static_cast<std::size_t>(color) * 4;
                return {palette[po], palette[po + 1], palette[po + 2]};
            }();
            write_depth_pixel(surface, depth, x, y, z, rgb, clip);
        }
    }
}

UnitRenderResult failure(ErrorCode code, std::string message) {
    return {std::nullopt, Error{code, std::move(message)}};
}

int32_t average_source_y(
    const formats::objects3d::Object& object, const formats::objects3d::Primitive& primitive
) {
    // The primitive sort runs at 3DO load, averaging the primitive's source
    // vertex Y values. Draw walks that baked order — it does not re-sort by
    // transformed Y each frame.
    if (primitive.vertex_indices.empty())
        return 0;
    int32_t sum = 0;
    for (const auto index : primitive.vertex_indices) {
        if (index >= object.vertices.size())
            return 0;
        sum = wrap_add(sum, object.vertices[index].y);
    }
    return sum / static_cast<int32_t>(primitive.vertex_indices.size());
}

void stroke_polygon(
    Surface& surface,
    DepthBuffer& depth,
    std::span<const Point> points,
    const std::array<uint8_t, 3>& color,
    const ClipBounds& clip
) {
    if (points.size() < 2)
        return;
    for (std::size_t i = 0; i < points.size(); ++i) {
        auto x0 = points[i].x, y0 = points[i].y;
        auto z0 = wrap_multiply(points[i].z, 0x10000);
        const auto& to = points[(i + 1) % points.size()];
        auto x1 = to.x, y1 = to.y;
        int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
        int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
        int error = dx + dy;
        const auto steps = std::max(dx, std::abs(y1 - y0));
        const auto dz = steps == 0 ? 0 : wrap_multiply(to.z - points[i].z, 0x10000) / steps;
        while (true) {
            write_depth_pixel(surface, depth, x0, y0, z0, color, clip);
            if (x0 == x1 && y0 == y1)
                break;
            const auto twice = error * 2;
            if (twice >= dy) {
                error += dy;
                x0 += sx;
            }
            if (twice <= dx) {
                error += dx;
                y0 += sy;
            }
            z0 = wrap_add(z0, dz);
        }
    }
}

} // namespace

void begin_shadow_pass(uint32_t dest_width, uint32_t dest_height) {
    const auto count = static_cast<std::size_t>(dest_width) * dest_height;
    if (shadow_frame_mask.size() < count)
        shadow_frame_mask.resize(count);
    if (count != 0)
        std::memset(shadow_frame_mask.data(), 0, count);
    shadow_frame_width = dest_width;
    shadow_frame_height = dest_height;
}

UnitProjection unit_projection_for_viewport(
    const BattlefieldViewport& viewport, formats::objects3d::FixedVector3 world_position
) noexcept {
    return {
        world_position,
        static_cast<int32_t>(viewport.source_x),
        static_cast<int32_t>(viewport.source_y),
        viewport.destination_x,
        viewport.destination_y,
        RasterClip{
            viewport.destination_x,
            viewport.destination_y,
            viewport.width == 0 ? 0 : viewport.width - 1,
            viewport.height == 0 ? 0 : viewport.height - 1
        },
        viewport.scale
    };
}

TextureCatalog load_texture_catalog(AssetStore& assets) {
    TextureCatalog catalog;
    std::unordered_set<std::string> seen;
    const auto paths = assets.list_effective_in_mount_order("textures", ".gaf");
    const auto load = [&](const std::string& path, bool team_archive) {
        const auto parsed = formats::gaf::parse(assets.read(path).bytes);
        if (!parsed.ok())
            throw std::runtime_error(
                "cannot parse texture archive '" + path + "': " + parsed.error->message
            );
        for (const auto& sequence : parsed.archive->sequences) {
            const auto key = folded(sequence.name);
            if (!seen.emplace(key).second)
                continue;
            TextureMaterial material;
            if (team_archive && sequence.frames.size() == 10)
                material.mode = MaterialFrameMode::owner_team;
            else if (sequence.frames.size() >= 2)
                material.mode = MaterialFrameMode::animated;
            material.frames.reserve(sequence.frames.size());
            for (const auto& source : sequence.frames) {
                // The textured rasterizer consumes unpacked frame pixels. Retain an empty
                // slot for unsupported frames so runtime indices do not move.
                if (source.compressed || !source.layers.empty()) {
                    material.frames.emplace_back(std::nullopt);
                    continue;
                }
                const auto rendered = formats::gaf::render_normal(source);
                if (rendered.ok())
                    material.frames.emplace_back(*rendered.frame);
                else
                    material.frames.emplace_back(std::nullopt);
            }
            catalog.materials.emplace(key, std::move(material));
        }
    };
    for (const auto& path : paths) {
        if (folded(path) != "textures/logos.gaf")
            load(path, false);
    }
    for (const auto& path : paths)
        if (folded(path) == "textures/logos.gaf")
            load(path, true);
    return catalog;
}

const formats::gaf::RenderedFrame*
select_material_frame(const TextureMaterial& material, const MaterialFrameState& state) noexcept {
    std::size_t index = 0;
    if (material.mode == MaterialFrameMode::owner_team) {
        if (!state.owner_team_frame)
            return nullptr;
        index = *state.owner_team_frame;
    } else if (material.mode == MaterialFrameMode::animated && !state.force_first_frame) {
        index = state.cursor;
    }
    if (index >= material.frames.size() || !material.frames[index])
        return nullptr;
    return &*material.frames[index];
}

uint16_t UnitMaterialState::cursor_for(
    std::size_t object_index, std::size_t primitive_index
) const noexcept {
    for (const auto& entry : primitive_cursors) {
        if (entry.object_index == object_index && entry.primitive_index == primitive_index)
            return entry.cursor;
    }
    // A newly loaded primitive's cursor starts at frame zero.
    return 0;
}

UnitRenderResult render_colored_instance(
    Surface& destination,
    const sim::model_runtime::Instance& instance,
    const PaletteBytes& palette,
    const UnitProjection& projection,
    const TextureCatalog* textures,
    const UnitMaterialState& material_state
) {
    if (destination.width > static_cast<uint32_t>(INT32_MAX) ||
        destination.height > static_cast<uint32_t>(INT32_MAX) ||
        std::abs(static_cast<int64_t>(projection.destination_origin_x)) >
            game_viewport::maximum_safe_origin ||
        std::abs(static_cast<int64_t>(projection.destination_origin_y)) >
            game_viewport::maximum_safe_origin) {
        return failure(
            ErrorCode::output_limit, "unit destination dimensions or origin exceed safe bounds"
        );
    }
    const auto pixels = static_cast<uint64_t>(destination.width) * destination.height;
    if (pixels > std::numeric_limits<std::size_t>::max() / 3U ||
        pixels * 3U != destination.rgb.size()) {
        return failure(
            ErrorCode::invalid_map_model, "unit destination surface has inconsistent RGB data"
        );
    }
    UnitRenderStats stats;
    ClipBounds clip{
        0, 0, static_cast<int32_t>(destination.width), static_cast<int32_t>(destination.height)
    };
    if (projection.raster_clip) {
        const auto& requested = *projection.raster_clip;
        const auto right = static_cast<int64_t>(requested.x) + requested.width;
        const auto bottom = static_cast<int64_t>(requested.y) + requested.height;
        clip.left = std::clamp(requested.x, 0, static_cast<int32_t>(destination.width));
        clip.top = std::clamp(requested.y, 0, static_cast<int32_t>(destination.height));
        clip.right = static_cast<int32_t>(std::clamp<int64_t>(right, clip.left, destination.width));
        clip.bottom =
            static_cast<int32_t>(std::clamp<int64_t>(bottom, clip.top, destination.height));
    }
    if (material_state.cast_shadow && !material_state.wireframe)
        stamp_instance_shadow(
            destination, instance, projection, material_state.ground_height, clip
        );
    // The depth plane starts at 1; a pixel is written when the plane's
    // sample is at most the pixel's depth.
    DepthBuffer depth{};
    if (!prepare_unit_depth(instance, projection, material_state.depth_extra, clip, depth))
        return {stats, std::nullopt};
    static std::array<uint8_t, 32 * 256> cached_shade_table{};
    static std::size_t cached_palette_bytes = 0;
    const auto* shade_remap = static_cast<const std::array<uint8_t, 32 * 256>*>(nullptr);
    if (material_state.gouraud_shading) {
        if (cached_palette_bytes != palette.size()) {
            cached_shade_table = make_shade_table(palette);
            cached_palette_bytes = palette.size();
        }
        shade_remap = &cached_shade_table;
    }
    const auto project_vertex = [&](const formats::objects3d::FixedVector3& vertex) {
        return project(vertex, projection, material_state.depth_extra);
    };
    const auto pieces = instance.pieces();
    const auto& model = instance.model();
    if (material_state.wireframe) {
        int32_t min_x = std::numeric_limits<int32_t>::max();
        int32_t min_y = std::numeric_limits<int32_t>::max();
        int32_t max_x = std::numeric_limits<int32_t>::min();
        int32_t max_y = std::numeric_limits<int32_t>::min();
        bool any = false;
        for (std::size_t reverse = pieces.size(); reverse != 0; --reverse) {
            const auto& piece = pieces[reverse - 1];
            if ((piece.flags & static_cast<uint16_t>(sim::model_runtime::PieceFlag::visible)) == 0)
                continue;
            if (piece.object_index >= model.objects.size())
                return failure(
                    ErrorCode::invalid_map_model, "model piece references a missing object"
                );
            if (piece.transformed_vertices.size() !=
                model.objects[piece.object_index].vertices.size())
                return failure(
                    ErrorCode::invalid_map_model, "model transformed vertex count mismatch"
                );
            for (const auto& vertex : piece.transformed_vertices) {
                const auto p = project_vertex(vertex);
                min_x = std::min(min_x, p.x);
                min_y = std::min(min_y, p.y);
                max_x = std::max(max_x, p.x);
                max_y = std::max(max_y, p.y);
                any = true;
            }
        }
        if (!any)
            return {stats, std::nullopt};
        // Sprite bounds: width = max - (min - 2) + 2, origin = -(min - 2).
        IndexedSprite sprite;
        sprite.origin_x = min_x - 2;
        sprite.origin_y = min_y - 2;
        sprite.width = max_x - min_x + 4;
        sprite.height = max_y - min_y + 4;
        if (sprite.width <= 0 || sprite.height <= 0 || sprite.width > 512 || sprite.height > 512)
            return {stats, std::nullopt};
        const auto count =
            static_cast<std::size_t>(sprite.width) * static_cast<std::size_t>(sprite.height);
        sprite.pixels.assign(count, sprite.transparent);
        sprite.aux.assign(count, 0);
        const auto offset_points = [&](std::vector<Point>& points) {
            for (auto& p : points) {
                p.x -= sprite.origin_x;
                p.y -= sprite.origin_y;
            }
        };
        for (std::size_t reverse = pieces.size(); reverse != 0; --reverse) {
            const auto& piece = pieces[reverse - 1];
            if ((piece.flags & static_cast<uint16_t>(sim::model_runtime::PieceFlag::visible)) == 0)
                continue;
            const auto& object = model.objects[piece.object_index];
            std::vector<std::size_t> primitive_order(object.primitives.size());
            std::iota(primitive_order.begin(), primitive_order.end(), 0U);
            const bool has_selection = object.selection_primitive != -1;
            if (has_selection && object.selection_primitive >= 0 &&
                static_cast<std::size_t>(object.selection_primitive) < primitive_order.size())
                std::swap(
                    primitive_order.front(),
                    primitive_order[static_cast<std::size_t>(object.selection_primitive)]
                );
            if (primitive_order.size() > 1) {
                std::stable_sort(
                    primitive_order.begin() + 1,
                    primitive_order.end(),
                    [&](const auto left, const auto right) {
                        return average_source_y(object, object.primitives[left]) <
                               average_source_y(object, object.primitives[right]);
                    }
                );
            }
            const std::size_t first = has_selection ? 1U : 0U;
            for (std::size_t ordered_index = first; ordered_index < primitive_order.size();
                 ++ordered_index) {
                const auto& primitive = object.primitives[primitive_order[ordered_index]];
                std::vector<Point> points;
                points.reserve(primitive.vertex_indices.size());
                bool ok = true;
                for (const auto vertex : primitive.vertex_indices) {
                    if (vertex >= piece.transformed_vertices.size()) {
                        ok = false;
                        break;
                    }
                    points.push_back(project_vertex(piece.transformed_vertices[vertex]));
                }
                if (!ok || points.empty())
                    continue;
                offset_points(points);
                if ((static_cast<uint32_t>(primitive.is_colored) & 0x01U) != 0) {
                    fill_indexed_polygon(
                        sprite, points, static_cast<uint8_t>(primitive.color_index)
                    );
                    ++stats.colored_primitives;
                } else if (!primitive.texture_name.empty() && points.size() == 4) {
                    const formats::gaf::RenderedFrame* found = nullptr;
                    if (textures != nullptr) {
                        const auto it = textures->materials.find(folded(primitive.texture_name));
                        if (it != textures->materials.end())
                            found = select_material_frame(
                                it->second,
                                {material_state.cursor_for(
                                     piece.object_index, primitive_order[ordered_index]
                                 ),
                                 material_state.force_first_frame,
                                 material_state.owner_team_frame}
                            );
                    }
                    if (found != nullptr) {
                        texture_indexed_quad(
                            sprite, {points[0], points[1], points[2], points[3]}, *found
                        );
                        ++stats.textured_primitives;
                    }
                }
            }
        }
        apply_build_shimmer(
            sprite,
            material_state.build_remaining,
            material_state.wireframe_index,
            material_state.wireframe_index_b
        );
        for (std::size_t reverse = pieces.size(); reverse != 0; --reverse) {
            const auto& piece = pieces[reverse - 1];
            if ((piece.flags & static_cast<uint16_t>(sim::model_runtime::PieceFlag::visible)) == 0)
                continue;
            const auto& object = model.objects[piece.object_index];
            const bool has_selection = object.selection_primitive != -1;
            const std::size_t first = has_selection ? 1U : 0U;
            std::vector<std::size_t> primitive_order(object.primitives.size());
            std::iota(primitive_order.begin(), primitive_order.end(), 0U);
            if (has_selection && object.selection_primitive >= 0 &&
                static_cast<std::size_t>(object.selection_primitive) < primitive_order.size())
                std::swap(
                    primitive_order.front(),
                    primitive_order[static_cast<std::size_t>(object.selection_primitive)]
                );
            for (std::size_t ordered_index = first; ordered_index < primitive_order.size();
                 ++ordered_index) {
                const auto& primitive = object.primitives[primitive_order[ordered_index]];
                std::vector<Point> points;
                for (const auto vertex : primitive.vertex_indices) {
                    if (vertex >= piece.transformed_vertices.size()) {
                        points.clear();
                        break;
                    }
                    points.push_back(project_vertex(piece.transformed_vertices[vertex]));
                }
                if (points.size() < 2)
                    continue;
                offset_points(points);
                stroke_indexed_polygon(sprite, points, material_state.wireframe_index_b);
            }
        }
        blit_indexed_sprite(destination, sprite, palette, clip);
        if (material_state.selection_outline) {
            const auto pal = static_cast<std::size_t>(*material_state.selection_outline) *
                             oa::palette_entry_bytes;
            const std::array<uint8_t, 3> rgb{palette[pal], palette[pal + 1], palette[pal + 2]};
            for (std::size_t reverse = pieces.size(); reverse != 0; --reverse) {
                const auto& piece = pieces[reverse - 1];
                if ((piece.flags & static_cast<uint16_t>(sim::model_runtime::PieceFlag::visible)) ==
                    0)
                    continue;
                const auto& object = model.objects[piece.object_index];
                if (object.selection_primitive < 0 ||
                    static_cast<std::size_t>(object.selection_primitive) >=
                        object.primitives.size())
                    continue;
                const auto& primitive =
                    object.primitives[static_cast<std::size_t>(object.selection_primitive)];
                std::vector<Point> points;
                for (const auto vertex : primitive.vertex_indices) {
                    if (vertex >= piece.transformed_vertices.size()) {
                        points.clear();
                        break;
                    }
                    points.push_back(project_vertex(piece.transformed_vertices[vertex]));
                }
                if (points.size() >= 2)
                    stroke_polygon(destination, depth, points, rgb, clip);
            }
        }
        return {stats, std::nullopt};
    }
    for (std::size_t reverse = pieces.size(); reverse != 0; --reverse) {
        const auto& piece = pieces[reverse - 1];
        if ((piece.flags & static_cast<uint16_t>(sim::model_runtime::PieceFlag::visible)) == 0) {
            ++stats.hidden_pieces;
            continue;
        }
        if (piece.object_index >= model.objects.size())
            return failure(ErrorCode::invalid_map_model, "model piece references a missing object");
        const auto& object = model.objects[piece.object_index];
        if (piece.transformed_vertices.size() != object.vertices.size())
            return failure(ErrorCode::invalid_map_model, "model transformed vertex count mismatch");
        std::vector<std::size_t> primitive_order(object.primitives.size());
        std::iota(primitive_order.begin(), primitive_order.end(), 0U);
        const bool has_selection = object.selection_primitive != -1;
        if (has_selection) {
            if (object.selection_primitive < 0 ||
                static_cast<std::size_t>(object.selection_primitive) >= primitive_order.size())
                return failure(
                    ErrorCode::invalid_map_model, "3DO selection primitive is out of range"
                );
            std::swap(
                primitive_order.front(),
                primitive_order[static_cast<std::size_t>(object.selection_primitive)]
            );
        }
        // The load-time sort leaves slot zero in place and bubble-sorts the
        // remaining records by average source vertex Y. It runs at 3DO load,
        // not from transformed vertices each frame.
        if (primitive_order.size() > 1) {
            std::stable_sort(
                primitive_order.begin() + 1,
                primitive_order.end(),
                [&](const auto left, const auto right) {
                    return average_source_y(object, object.primitives[left]) <
                           average_source_y(object, object.primitives[right]);
                }
            );
        }
        const std::size_t first = has_selection ? 1U : 0U;
        const auto shades = material_state.gouraud_shading
                                ? vertex_shades(piece, object)
                                : std::vector<int32_t>(piece.transformed_vertices.size(), 0xf);
        for (std::size_t ordered_index = first; ordered_index < primitive_order.size();
             ++ordered_index) {
            const auto primitive_index = primitive_order[ordered_index];
            const auto& primitive = object.primitives[primitive_index];
            if (primitive.vertex_indices.size() > game_viewport::maximum_polygon_vertices)
                return failure(
                    ErrorCode::output_limit, "3DO polygon exceeds the projection buffer"
                );
            std::vector<Point> points;
            points.reserve(primitive.vertex_indices.size());
            for (const auto vertex : primitive.vertex_indices) {
                if (vertex >= piece.transformed_vertices.size())
                    return failure(
                        ErrorCode::invalid_map_model, "3DO primitive vertex is out of range"
                    );
                auto projected = project_vertex(piece.transformed_vertices[vertex]);
                if (vertex < shades.size())
                    projected.shade = shades[vertex];
                points.push_back(projected);
            }
            if (material_state.wireframe) {
                // Wireframe strokes every primitive as a closed polyline in the
                // 0xa0..0xaf nano palette, textured and colored alike.
                const auto pal = static_cast<std::size_t>(material_state.wireframe_index) *
                                 oa::palette_entry_bytes;
                stroke_polygon(
                    destination,
                    depth,
                    points,
                    {palette[pal], palette[pal + 1], palette[pal + 2]},
                    clip
                );
                if ((static_cast<uint32_t>(primitive.is_colored) & 0x01U) != 0)
                    ++stats.colored_primitives;
                else if (!primitive.texture_name.empty())
                    ++stats.textured_primitives;
                continue;
            }
            constexpr uint32_t solid_color_flag = 0x01U;
            if ((static_cast<uint32_t>(primitive.is_colored) & solid_color_flag) == 0) {
                if (!primitive.texture_name.empty()) {
                    const auto found = textures == nullptr
                                           ? nullptr
                                           : [&]() -> const formats::gaf::RenderedFrame* {
                        const auto it = textures->materials.find(folded(primitive.texture_name));
                        return it == textures->materials.end()
                                   ? nullptr
                                   : select_material_frame(
                                         it->second,
                                         {material_state.cursor_for(
                                              piece.object_index, primitive_index
                                          ),
                                          material_state.force_first_frame,
                                          material_state.owner_team_frame}
                                     );
                    }();
                    if (found != nullptr && points.size() == 4) {
                        std::array<Point, 4> quad{points[0], points[1], points[2], points[3]};
                        texture_quad(destination, depth, quad, *found, palette, clip, shade_remap);
                        ++stats.textured_primitives;
                    } else
                        ++stats.textured_primitives_deferred;
                }
                continue;
            }
            const auto offset =
                static_cast<std::size_t>(static_cast<uint8_t>(primitive.color_index)) *
                oa::palette_entry_bytes;
            fill_polygon(
                destination,
                depth,
                points,
                {palette[offset], palette[offset + 1], palette[offset + 2]},
                clip,
                shade_remap,
                shade_remap != nullptr ? &palette : nullptr,
                static_cast<uint8_t>(primitive.color_index)
            );
            ++stats.colored_primitives;
        }
        if (material_state.selection_outline && has_selection) {
            const auto& primitive = object.primitives[primitive_order.front()];
            std::vector<Point> points;
            points.reserve(primitive.vertex_indices.size());
            bool ok = true;
            for (const auto vertex : primitive.vertex_indices) {
                if (vertex >= piece.transformed_vertices.size()) {
                    ok = false;
                    break;
                }
                points.push_back(project_vertex(piece.transformed_vertices[vertex]));
            }
            if (ok && points.size() >= 2) {
                const auto pal = static_cast<std::size_t>(*material_state.selection_outline) *
                                 oa::palette_entry_bytes;
                stroke_polygon(
                    destination,
                    depth,
                    points,
                    {palette[pal], palette[pal + 1], palette[pal + 2]},
                    clip
                );
            }
        }
    }
    return {stats, std::nullopt};
}

} // namespace oa::present::world_renderer
