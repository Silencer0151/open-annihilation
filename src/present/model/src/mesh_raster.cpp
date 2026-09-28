// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Textured quad projection and span sampling for 3DO primitives.
#include "oa/present/model/mesh_raster.hpp"

#include "oa/present/display.hpp"
#include "oa/present/palette_tables.hpp"
#include "oa/present/span_sample.hpp"
#include "oa/present/surface.hpp"

#include <algorithm>
#include <vector>

namespace oa::present::model {
namespace {

constexpr int32_t fixed_one = 0x10000;
constexpr int32_t fixed_ceiling = 0xffff; // edge x starts at x + 0.99998
constexpr int32_t no_vertex_low = 999999;
constexpr int32_t no_vertex_high = -999999;
constexpr int32_t quad_corners = 4;
// Masked row samplers address this much texture from the frame start.
constexpr uint32_t sampler_window = 0x10000;

int32_t wrap_add(int32_t a, int32_t b) noexcept {
    return static_cast<int32_t>(static_cast<uint32_t>(a) + static_cast<uint32_t>(b));
}

int32_t wrap_sub(int32_t a, int32_t b) noexcept {
    return static_cast<int32_t>(static_cast<uint32_t>(a) - static_cast<uint32_t>(b));
}

int32_t wrap_mul(int32_t a, int32_t b) noexcept {
    return static_cast<int32_t>(static_cast<uint32_t>(a) * static_cast<uint32_t>(b));
}

// Signed division truncated toward zero; the divisor is always positive here.
int32_t div_step(int32_t numerator, int32_t divisor) noexcept {
    return numerator / divisor;
}

int32_t to_fixed(int32_t value) noexcept {
    return wrap_mul(value, fixed_one);
}

// Texel at (u, v) of a raw texture, as the unmasked loops address it.
// An offset outside the 64 KiB window the samplers may address reads
// as 0.
uint8_t texel(const Sprite& texture, int32_t u, int32_t v) noexcept {
    const int32_t offset = wrap_add(u >> 16, wrap_mul(v >> 16, texture.width));
    if (offset < 0 || static_cast<uint32_t>(offset) >= sampler_window)
        return 0;
    return static_cast<const uint8_t*>(texture.data)[offset];
}

// The width-dispatched row fill used when no depth plane exists. With
// `repeat_w64` the 128-wide case also runs the 64-wide pass: after the 128-wide
// pass the row is filled again at 64 wide (the shaded sampler does the
// same).
void sample_row_unmasked(
    uint8_t* out,
    const Sprite& texture,
    int32_t count,
    int32_t u,
    int32_t v,
    int32_t du,
    int32_t dv,
    bool repeat_w64
) noexcept {
    const auto* data = static_cast<const uint8_t*>(texture.data);
    const auto ucount = static_cast<uint32_t>(count);
    const auto uu = static_cast<uint32_t>(u);
    const auto uv = static_cast<uint32_t>(v);
    const auto udu = static_cast<uint32_t>(du);
    const auto udv = static_cast<uint32_t>(dv);
    switch (texture.width) {
    case 16:
        present::sample_texture_row_w16(out, data, ucount, uu, uv, udu, udv);
        return;
    case 32:
        present::sample_texture_row_w32(out, data, ucount, uu, uv, udu, udv);
        return;
    case 64:
        present::sample_texture_row_w64(out, data, ucount, uu, uv, udu, udv);
        return;
    case 128:
        present::sample_texture_row_w128(out, data, ucount, uu, uv, udu, udv);
        if (repeat_w64)
            present::sample_texture_row_w64(out, data, ucount, uu, uv, udu, udv);
        return;
    default:
        break;
    }
    for (int32_t i = 0; i < count; ++i) {
        *out++ = texel(texture, u, v);
        u = wrap_add(u, du);
        v = wrap_add(v, dv);
    }
}

// Shared body of the two depth span samplers; `shaded` adds the shade-table remap.
void sample_depth_span(
    int32_t y, MeshSpanRow& row, Sprite& target, const Sprite& texture, bool shaded
) noexcept {
    const uint8_t* shade_table = nullptr;
    if (shaded) {
        const present::DisplayContext* display = present::display_context();
        shade_table = display != nullptr ? display->shade_table : nullptr;
    }
    const int32_t left = row.left;
    const int32_t span = wrap_sub(row.right, left);
    const int32_t du = div_step(wrap_sub(row.u_right, row.u_left), span);
    const int32_t dv = div_step(wrap_sub(row.v_right, row.v_left), span);
    const int32_t dd = div_step(wrap_sub(row.depth_right, row.depth_left), span);
    const int32_t ds = shaded ? div_step(wrap_sub(row.shade_right, row.shade_left), span) : 0;
    if (left < 0) {
        row.u_left = wrap_sub(row.u_left, wrap_mul(left, du));
        row.v_left = wrap_sub(row.v_left, wrap_mul(left, dv));
        row.left = 0;
        row.depth_left = wrap_sub(row.depth_left, wrap_mul(left, dd));
        if (shaded)
            row.shade_left = wrap_sub(row.shade_left, wrap_mul(left, ds));
    }
    if (static_cast<int32_t>(target.width) - 1 < row.right)
        row.right = static_cast<int32_t>(target.width) - 1;
    const int32_t count = wrap_sub(row.right, row.left);
    if (count <= 0)
        return;
    int32_t u = row.u_left;
    int32_t v = row.v_left;
    int32_t depth = row.depth_left;
    int32_t shade = row.shade_left;
    const int32_t offset = wrap_add(wrap_mul(static_cast<int32_t>(target.width), y), row.left);
    uint8_t* out = static_cast<uint8_t*>(target.data) + offset;
    if (target.aux == nullptr) {
        sample_row_unmasked(out, texture, count, u, v, du, dv, true);
        return;
    }
    uint8_t* out_depth = static_cast<uint8_t*>(target.aux) + offset;
    for (int32_t i = 0; i < count; ++i, ++out, ++out_depth) {
        const auto sample = static_cast<uint8_t>(static_cast<uint32_t>(depth) >> 16);
        if (*out_depth <= sample) {
            uint8_t value = texel(texture, u, v);
            if (shaded && shade_table != nullptr) {
                const uint32_t index =
                    static_cast<uint32_t>(wrap_add(value, wrap_mul(shade >> 16, 0x100))) %
                    present::shade_table_size;
                value = shade_table[index];
            }
            *out = value;
            *out_depth = sample;
        }
        u = wrap_add(u, du);
        depth = wrap_add(depth, dd);
        shade = wrap_add(shade, ds);
        v = wrap_add(v, dv);
    }
}

// A projected corner as the edge walkers read it.
struct Corner {
    int32_t x{};
    int32_t y{};
    int32_t depth{};
    int32_t shade{};
};

// Per-thread span table for a quad that reads `rows` rows. The rows read are
// cleared first, so a row no edge reaches is empty. The edge walkers may
// write past them for a twisted quad, so the table is sized beyond them.
std::vector<MeshSpanRow>& span_table(int32_t rows) {
    thread_local std::vector<MeshSpanRow> table;
    const auto needed = static_cast<std::size_t>(rows) * quad_corners + quad_corners;
    if (table.size() < needed)
        table.resize(needed);
    std::fill(table.begin(), table.begin() + static_cast<std::ptrdiff_t>(rows) + 1, MeshSpanRow{});
    return table;
}

void default_texture_points(const Sprite& texture, TexturePoint (&points)[quad_corners]) noexcept {
    const int32_t right = static_cast<int32_t>(texture.width) - 1;
    const int32_t bottom = static_cast<int32_t>(texture.height) - 1;
    points[0] = {0, 0};
    points[1] = {right, 0};
    points[2] = {right, bottom};
    points[3] = {0, bottom};
}

// Edge interpolants of one quad side, all 16.16.
struct EdgeStep {
    int32_t x{};
    int32_t u{};
    int32_t v{};
    int32_t depth{};
    int32_t shade{};
    int32_t dx{};
    int32_t du{};
    int32_t dv{};
    int32_t ddepth{};
    int32_t dshade{};
};

EdgeStep edge_step(
    const Corner& a, const Corner& b, const TexturePoint& ta, const TexturePoint& tb, int32_t dy
) noexcept {
    EdgeStep e{};
    e.dx = div_step(wrap_mul(wrap_sub(b.x, a.x), fixed_one), dy);
    e.x = wrap_add(to_fixed(a.x), fixed_ceiling);
    e.depth = to_fixed(a.depth);
    e.u = to_fixed(ta.u);
    e.v = to_fixed(ta.v);
    e.du = div_step(wrap_sub(to_fixed(tb.u), to_fixed(ta.u)), dy);
    e.dv = div_step(wrap_sub(to_fixed(tb.v), to_fixed(ta.v)), dy);
    e.ddepth = div_step(wrap_sub(to_fixed(b.depth), to_fixed(a.depth)), dy);
    e.shade = to_fixed(a.shade);
    e.dshade = div_step(wrap_sub(to_fixed(b.shade), to_fixed(a.shade)), dy);
    return e;
}

void advance_edge(EdgeStep& e, int32_t rows) noexcept {
    e.x = wrap_sub(e.x, wrap_mul(e.dx, rows));
    e.u = wrap_sub(e.u, wrap_mul(e.du, rows));
    e.v = wrap_sub(e.v, wrap_mul(e.dv, rows));
    e.depth = wrap_sub(e.depth, wrap_mul(e.ddepth, rows));
    e.shade = wrap_sub(e.shade, wrap_mul(e.dshade, rows));
}

// Shared body of the depth projectors. `shaded` selects the shade-carrying
// variant, whose edges also require a positive lower end.
void project_depth_quad(
    Sprite* target,
    const Sprite* texture,
    const Corner (&quad)[quad_corners],
    const TexturePoint* uv,
    bool shaded
) noexcept {
    TexturePoint corners[quad_corners];
    if (uv == nullptr) {
        default_texture_points(*texture, corners);
        uv = corners;
    }
    int32_t top = no_vertex_low;
    int32_t bottom = no_vertex_high;
    int32_t right_x = no_vertex_high;
    int32_t left_x = no_vertex_low;
    int32_t top_index = 0;
    int32_t bottom_index = 0;
    for (int32_t i = 0; i < quad_corners; ++i) {
        const int32_t y = quad[i].y;
        if (y < top) {
            top = y;
            top_index = i;
        }
        if (bottom < y) {
            bottom_index = i;
            bottom = y;
        }
        const int32_t x = quad[i].x;
        if (right_x < x)
            right_x = x;
        if (x < left_x)
            left_x = x;
    }
    const int32_t last_row = static_cast<int32_t>(target->height) - 1;
    if (right_x < 0 || left_x > static_cast<int32_t>(target->width) - 1 || bottom < 0 ||
        top > last_row)
        return;
    if (top < 0)
        top = 0;
    if (last_row < bottom)
        bottom = last_row;
    if (bottom == top)
        return;
    auto& table = span_table(bottom - top);
    // Left side: walk backwards from the top corner to the bottom corner.
    std::size_t cursor = 0;
    int32_t i = top_index;
    do {
        const int32_t j = i - 1 < 0 ? quad_corners - 1 : i - 1;
        const Corner& a = quad[i];
        const Corner& b = quad[j];
        if ((!shaded || 0 < b.y) && a.y < b.y) {
            EdgeStep e = edge_step(a, b, uv[i], uv[j], b.y - a.y);
            int32_t from = a.y;
            if (from < 0) {
                advance_edge(e, from);
                from = 0;
            }
            const int32_t to = std::min(b.y, last_row);
            for (int32_t n = from; n < to; ++n, ++cursor) {
                MeshSpanRow& r = table[cursor];
                r.left = e.x >> 16;
                r.u_left = e.u;
                r.v_left = e.v;
                r.depth_left = e.depth;
                if (shaded)
                    r.shade_left = e.shade;
                e.x = wrap_add(e.x, e.dx);
                e.u = wrap_add(e.u, e.du);
                e.v = wrap_add(e.v, e.dv);
                e.depth = wrap_add(e.depth, e.ddepth);
                e.shade = wrap_add(e.shade, e.dshade);
            }
        }
        i = j;
    } while (i != bottom_index);
    // Right side: walk forwards.
    cursor = 0;
    i = top_index;
    do {
        const int32_t j = (i + 1) & (quad_corners - 1);
        const Corner& a = quad[i];
        const Corner& b = quad[j];
        if ((!shaded || 0 < b.y) && a.y < b.y) {
            EdgeStep e = edge_step(a, b, uv[i], uv[j], b.y - a.y);
            int32_t from = a.y;
            if (from < 0) {
                advance_edge(e, from);
                from = 0;
            }
            const int32_t to = std::min(b.y, last_row);
            for (int32_t n = from; n < to; ++n, ++cursor) {
                MeshSpanRow& r = table[cursor];
                r.right = e.x >> 16;
                r.u_right = e.u;
                r.v_right = e.v;
                r.depth_right = e.depth;
                if (shaded)
                    r.shade_right = e.shade;
                e.x = wrap_add(e.x, e.dx);
                e.u = wrap_add(e.u, e.du);
                e.v = wrap_add(e.v, e.dv);
                e.depth = wrap_add(e.depth, e.ddepth);
                e.shade = wrap_add(e.shade, e.dshade);
            }
        }
        i = j;
    } while (i != bottom_index);
    cursor = 0;
    for (int32_t y = top; y < bottom; ++y, ++cursor) {
        MeshSpanRow& r = table[cursor];
        const int32_t span = wrap_sub(r.right, r.left);
        if (r.right != r.left && span > -1) {
            if (shaded)
                sample_shaded_mesh_span(y, r, *target, *texture);
            else
                sample_mesh_span(y, r, *target, *texture);
        }
    }
}

} // namespace

void sample_mesh_span(int32_t y, MeshSpanRow& row, Sprite& target, const Sprite& texture) noexcept {
    sample_depth_span(y, row, target, texture, false);
}

void sample_shaded_mesh_span(
    int32_t y, MeshSpanRow& row, Sprite& target, const Sprite& texture
) noexcept {
    sample_depth_span(y, row, target, texture, true);
}

void texture_depth_quad(
    Sprite* target, const Sprite* texture, const present::DepthVertex* quad, const TexturePoint* uv
) noexcept {
    if (target == nullptr || texture == nullptr || quad == nullptr)
        return;
    Corner corners[quad_corners];
    for (int32_t i = 0; i < quad_corners; ++i)
        corners[i] = {quad[i].x, quad[i].y, quad[i].depth, 0};
    project_depth_quad(target, texture, corners, uv, false);
}

void texture_shaded_depth_quad(
    Sprite* target, const Sprite* texture, const present::ShadedVertex* quad, const TexturePoint* uv
) noexcept {
    if (target == nullptr || texture == nullptr || quad == nullptr)
        return;
    Corner corners[quad_corners];
    for (int32_t i = 0; i < quad_corners; ++i)
        corners[i] = {quad[i].x, quad[i].y, quad[i].depth, quad[i].shade};
    project_depth_quad(target, texture, corners, uv, true);
}

void sample_texture_span(
    int32_t y, MeshSpanRow& row, Surface& target, const Sprite& texture
) noexcept {
    const int32_t span = wrap_sub(row.right, row.left);
    const int32_t du = div_step(wrap_sub(row.u_right, row.u_left), span);
    const int32_t dv = div_step(wrap_sub(row.v_right, row.v_left), span);
    const Rect32 clip = present::surface_clip(target);
    if (row.left < clip.x1) {
        const int32_t skipped = clip.x1 - row.left;
        row.left = clip.x1;
        row.u_left = wrap_add(row.u_left, wrap_mul(skipped, du));
        row.v_left = wrap_add(row.v_left, wrap_mul(skipped, dv));
    }
    if (clip.x2 < row.right)
        row.right = clip.x2;
    const int32_t count = wrap_sub(row.right, row.left);
    if (count <= 0)
        return;
    uint8_t* out = target.pixels + wrap_add(wrap_mul(target.pitch, y), row.left);
    sample_row_unmasked(out, texture, count, row.u_left, row.v_left, du, dv, false);
}

void texture_quad(
    Surface* target,
    const Sprite* texture,
    const present::PolygonVertex* quad,
    const TexturePoint* uv
) noexcept {
    if (texture == nullptr || quad == nullptr)
        return;
    Surface locked{};
    bool unlock = false;
    if (target == nullptr) {
        if (present::lock_display_surface(locked) == 0)
            return;
        target = &locked;
        unlock = true;
    }
    TexturePoint corners[quad_corners];
    if (uv == nullptr) {
        default_texture_points(*texture, corners);
        uv = corners;
    }
    int32_t top = no_vertex_low;
    int32_t left_x = no_vertex_low;
    int32_t bottom = no_vertex_high;
    int32_t right_x = no_vertex_high;
    int32_t top_index = 0;
    int32_t bottom_index = 0;
    for (int32_t i = 0; i < quad_corners; ++i) {
        const int32_t y = quad[i].y;
        if (y < top) {
            top_index = i;
            top = y;
        }
        if (bottom < y) {
            bottom_index = i;
            bottom = y;
        }
        const int32_t x = quad[i].x;
        if (right_x < x)
            right_x = x;
        if (x < left_x)
            left_x = x;
    }
    const Rect32 clip = present::surface_clip(*target);
    if (clip.x1 <= right_x && left_x <= clip.x2 && clip.y1 <= bottom && top <= clip.y2) {
        if (top < clip.y1)
            top = clip.y1;
        if (clip.y2 < bottom)
            bottom = clip.y2;
        if (bottom != top) {
            auto& table = span_table(bottom - top);
            const auto edges = [&](bool left_side) {
                std::size_t cursor = 0;
                int32_t i = top_index;
                do {
                    const int32_t j =
                        left_side ? (i - 1 < 0 ? quad_corners - 1 : i - 1) : ((i + 1) & 3);
                    const int32_t ya = quad[i].y;
                    const int32_t yb = quad[j].y;
                    if (clip.y1 < yb && ya < yb) {
                        const int32_t dy = yb - ya;
                        const int32_t dx =
                            div_step(wrap_mul(wrap_sub(quad[j].x, quad[i].x), fixed_one), dy);
                        int32_t x = wrap_add(to_fixed(quad[i].x), fixed_ceiling);
                        int32_t v = to_fixed(uv[i].v);
                        int32_t u = to_fixed(uv[i].u);
                        const int32_t du =
                            div_step(wrap_sub(to_fixed(uv[j].u), to_fixed(uv[i].u)), dy);
                        const int32_t dv =
                            div_step(wrap_sub(to_fixed(uv[j].v), to_fixed(uv[i].v)), dy);
                        int32_t from = ya;
                        if (from < clip.y1) {
                            const int32_t skipped = clip.y1 - from;
                            x = wrap_add(x, wrap_mul(skipped, dx));
                            u = wrap_add(u, wrap_mul(skipped, du));
                            v = wrap_add(v, wrap_mul(skipped, dv));
                            from = clip.y1;
                        }
                        const int32_t clip_bottom = clip.y2;
                        const int32_t to = std::min(yb, clip_bottom);
                        for (int32_t n = from; n < to; ++n, ++cursor) {
                            MeshSpanRow& r = table[cursor];
                            if (left_side) {
                                r.left = x >> 16;
                                r.u_left = u;
                                r.v_left = v;
                            } else {
                                r.right = x >> 16;
                                r.u_right = u;
                                r.v_right = v;
                            }
                            x = wrap_add(x, dx);
                            u = wrap_add(u, du);
                            v = wrap_add(v, dv);
                        }
                    }
                    i = j;
                } while (i != bottom_index);
            };
            edges(true);
            edges(false);
            std::size_t cursor = 0;
            for (int32_t y = top; y < bottom; ++y, ++cursor) {
                MeshSpanRow& r = table[cursor];
                if (r.right != r.left && wrap_sub(r.right, r.left) > -1)
                    sample_texture_span(y, r, *target, *texture);
            }
        }
    }
    if (unlock)
        present::unlock_display_surface();
}

} // namespace oa::present::model
