// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The 3DO meshes of the GPU-drawn world: the projection matrix against
// today's placement, the triangles, colours, textures, normals and piece
// hierarchy of synthetic models rasterised from their meshes against today's
// model images at rest and turned, malformed models refused, the memory a
// mesh holds, and with --data every unit model of the installed game
// rasterised from its mesh against today's images, at rest and turned.
#include "oa/present/gpu_world/model_meshes.hpp"

#include "oa/formats/hpi.hpp"
#include "oa/formats/tdf.hpp"
#include "oa/present/display.hpp"
#include "oa/present/model/model_draw.hpp"
#include "oa/present/palette_tables.hpp"
#include "oa/present/surface.hpp"
#include "oa/sim/model_runtime/instance.hpp"
#include "oa/test/check.hpp"
#include "oa/test/game_assets.hpp"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace oa::present::gpu_world;
namespace draw = oa::present::model;
using oa::Sprite;
using oa::formats::objects3d::FixedVector3;
using oa::formats::objects3d::Model;
using oa::formats::objects3d::Object;
using oa::formats::objects3d::Primitive;
using oa::sim::model_runtime::Instance;
using oa::sim::model_runtime::PieceFlag;
using oa::sim::model_runtime::PieceState;
using oa::sim::model_runtime::RotationWords;

constexpr int32_t unit = 0x10000;
constexpr int32_t fixed_one = 0x10000;
constexpr int32_t fixed_ceiling = 0xffff;
constexpr uint32_t sampler_window = 0x10000;
constexpr uint16_t piece_visible = static_cast<uint16_t>(PieceFlag::visible);
constexpr uint16_t piece_shaded = static_cast<uint16_t>(PieceFlag::shaded);
constexpr uint8_t ink = 0x55;
constexpr uint8_t barrel_ink = 0x66;
constexpr uint8_t team_color = 4;
// A unit's root rotation, as the game gives it: the bank (xy), the heading
// (xz) and the pitch (yz), in angle words of 65536 a turn.
constexpr RotationWords heading_only = {0, 0x1800, 0};
constexpr RotationWords banked_and_pitched = {0x0800, 0x3000, 0x0c00};
constexpr RotationWords installed_turn = {0x0300, 0x3000, 0x0200};
// What the installed game's meshes are held to. Walked whole, a mesh's
// primitives give today's image exactly. Walked triangle by triangle, as a
// card draws them, per model and mode: the pixels one walk alone covers stay
// under this share of the pixels drawn, or this many pixels on a small
// model; the pixels both walks draw in colours the other has nowhere within
// a pixel (farther than a texel of phase) stay under this share; and no
// pixel's texture is sampled farther than the pose's largest distance, in
// texels, from the whole walk. Over every model of a mode the textures are
// sampled within the pose's mean distance on average, and at least the
// pose's share of the textured pixels lies within one texel. The bounds at
// rest, where the quad split is chosen, hold the split: a fixed split from
// the first corner, or from the second, breaks each of them.
constexpr double tolerated_coverage = 0.02;
constexpr std::size_t tolerated_coverage_pixels = 4;
constexpr double tolerated_far_share = 0.49;
constexpr double tolerated_largest_texels_at_rest = 64.0;
constexpr double tolerated_largest_texels_turned = 100.0;
constexpr double tolerated_mean_texels_at_rest = 1.25;
constexpr double tolerated_mean_texels_turned = 1.5;
constexpr double least_within_one_texel_at_rest = 0.68;
constexpr double least_within_one_texel_turned = 0.66;
constexpr std::size_t least_installed_models = 100;
// The memory every unit model's mesh together may take, and one model's.
constexpr std::size_t tolerated_total_bytes = 16U << 20U;
constexpr std::size_t tolerated_model_bytes = 512U << 10U;

int32_t wrap_add(int32_t a, int32_t b) noexcept {
    return static_cast<int32_t>(static_cast<uint32_t>(a) + static_cast<uint32_t>(b));
}

int32_t wrap_sub(int32_t a, int32_t b) noexcept {
    return static_cast<int32_t>(static_cast<uint32_t>(a) - static_cast<uint32_t>(b));
}

int32_t wrap_mul(int32_t a, int32_t b) noexcept {
    return static_cast<int32_t>(static_cast<uint32_t>(a) * static_cast<uint32_t>(b));
}

int32_t to_fixed(int32_t value) noexcept {
    return wrap_mul(value, fixed_one);
}

/// Today's whole part of a 16.16 value, as the image builders take it.
int32_t hi(int32_t value) noexcept {
    return static_cast<int16_t>(static_cast<uint32_t>(value) >> 16);
}

float world(int32_t value) noexcept {
    return static_cast<float>(value) / fixed_units_per_world_unit;
}

// ---------------------------------------------------------------------------
// A software rasteriser of a mesh: today's edge walk and span sampling over
// the mesh's triangles, each corner placed from the instance's transformed
// vertex by pixel_of_model_point.

/// One triangle corner as the edge walk reads it: whole pixels, and the
/// texel, depth and shade row as whole numbers.
struct Corner {
    int32_t x{};
    int32_t y{};
    int32_t u{};
    int32_t v{};
    int32_t depth{};
    int32_t shade{};
};

/// One scanline: the edge columns and the 16.16 interpolants at each.
struct Row {
    int32_t left{};
    int32_t right{};
    int32_t u_left{};
    int32_t v_left{};
    int32_t depth_left{};
    int32_t shade_left{};
    int32_t u_right{};
    int32_t v_right{};
    int32_t depth_right{};
    int32_t shade_right{};
};

/// A texel coordinate of a pixel no texture was sampled at.
constexpr int32_t no_texel = INT32_MIN;

/// An 8-bit image with an optional depth plane, as a model image, and the
/// 16.16 texel coordinates each pixel was last sampled at when they are
/// recorded.
struct Image {
    int32_t width{};
    int32_t height{};
    int32_t origin_x{};
    int32_t origin_y{};
    std::vector<uint8_t> pixels;
    std::vector<uint8_t> depth; // empty without a depth plane
    std::vector<int32_t> u;     // empty when not recorded
    std::vector<int32_t> v;
};

/// How a mesh is rasterised: with or without a depth plane, lit or not,
/// each primitive walked whole, as today's quad and polygon fills walk it,
/// or triangle by triangle, as a card draws it, and a lit corner's shade
/// row taken from the mesh's normal or from the normals of the piece's
/// transformed vertices, as today's builder takes a turned piece's.
struct RasterMode {
    bool depth_plane{};
    bool shaded{};
    bool whole_primitives{};
    bool normals_from_mesh{true};
    const uint8_t* shade_table{};
    const float* light{};
    float light_scale{};
    int32_t depth_base{draw::model_depth_base};
    uint8_t team{};
};

/// Walks one edge from `a` down to `b` into successive rows of one side,
/// the rows counted from the clipped top, as today's walkers do.
void scan_edge(
    std::vector<Row>& rows,
    std::size_t& cursor,
    bool right,
    const Corner& a,
    const Corner& b,
    int32_t last_row
) {
    if (b.y <= 0 || a.y >= b.y)
        return;
    const int32_t dy = b.y - a.y;
    const int32_t dx = wrap_mul(wrap_sub(b.x, a.x), fixed_one) / dy;
    int32_t x = wrap_add(to_fixed(a.x), fixed_ceiling);
    int32_t values[4] = {to_fixed(a.u), to_fixed(a.v), to_fixed(a.depth), to_fixed(a.shade)};
    const int32_t ends[4] = {to_fixed(b.u), to_fixed(b.v), to_fixed(b.depth), to_fixed(b.shade)};
    int32_t steps[4] = {};
    for (int i = 0; i < 4; ++i)
        steps[i] = wrap_sub(ends[i], values[i]) / dy;
    int32_t y = a.y;
    if (y < 0) {
        const int32_t skipped = -y;
        x = wrap_add(x, wrap_mul(skipped, dx));
        for (int i = 0; i < 4; ++i)
            values[i] = wrap_add(values[i], wrap_mul(skipped, steps[i]));
        y = 0;
    }
    const int32_t end = std::min(b.y, last_row);
    for (; y < end; ++y, ++cursor) {
        Row& row = rows[cursor];
        if (right) {
            row.right = x >> 16;
            row.u_right = values[0];
            row.v_right = values[1];
            row.depth_right = values[2];
            row.shade_right = values[3];
        } else {
            row.left = x >> 16;
            row.u_left = values[0];
            row.v_left = values[1];
            row.depth_left = values[2];
            row.shade_left = values[3];
        }
        x = wrap_add(x, dx);
        for (int i = 0; i < 4; ++i)
            values[i] = wrap_add(values[i], steps[i]);
    }
}

/// Fetches a texel as today's unmasked loop does: 0 outside the sampler window.
uint8_t texel(const Sprite& texture, int32_t u, int32_t v) noexcept {
    const int32_t offset = wrap_add(u >> 16, wrap_mul(v >> 16, texture.width));
    if (offset < 0 || static_cast<uint32_t>(offset) >= sampler_window)
        return 0;
    return static_cast<const uint8_t*>(texture.data)[offset];
}

/// Samples one span as today's span samplers do: linear steps of the
/// interpolants, the depth test when there is a plane, the shade remap of a
/// colour always and of a texel only with a depth plane.
void sample_span(
    Image& image, int32_t y, Row row, const RasterMode& mode, const Sprite* texture, uint8_t color
) {
    const int32_t span = wrap_sub(row.right, row.left);
    const int32_t du = wrap_sub(row.u_right, row.u_left) / span;
    const int32_t dv = wrap_sub(row.v_right, row.v_left) / span;
    const int32_t dd = wrap_sub(row.depth_right, row.depth_left) / span;
    const int32_t ds = wrap_sub(row.shade_right, row.shade_left) / span;
    if (row.left < 0) {
        row.u_left = wrap_sub(row.u_left, wrap_mul(row.left, du));
        row.v_left = wrap_sub(row.v_left, wrap_mul(row.left, dv));
        row.depth_left = wrap_sub(row.depth_left, wrap_mul(row.left, dd));
        row.shade_left = wrap_sub(row.shade_left, wrap_mul(row.left, ds));
        row.left = 0;
    }
    if (row.right > image.width - 1)
        row.right = image.width - 1;
    const int32_t count = wrap_sub(row.right, row.left);
    if (count <= 0)
        return;
    const bool remap = mode.shaded && (texture == nullptr || mode.depth_plane);
    int32_t u = row.u_left;
    int32_t v = row.v_left;
    int32_t depth = row.depth_left;
    int32_t shade = row.shade_left;
    const std::size_t offset = static_cast<std::size_t>(y) * image.width + row.left;
    for (int32_t i = 0; i < count; ++i) {
        const auto sample = static_cast<uint8_t>(static_cast<uint32_t>(depth) >> 16);
        const std::size_t at = offset + static_cast<std::size_t>(i);
        if (!mode.depth_plane || image.depth[at] <= sample) {
            uint8_t value = texture != nullptr ? texel(*texture, u, v) : color;
            if (remap)
                value = mode.shade_table
                            [static_cast<uint32_t>(wrap_add(value, wrap_mul(shade >> 16, 0x100))) %
                             oa::present::shade_table_size];
            image.pixels[at] = value;
            if (mode.depth_plane)
                image.depth[at] = sample;
            if (!image.u.empty()) {
                image.u[at] = texture != nullptr ? u : no_texel;
                image.v[at] = texture != nullptr ? v : no_texel;
            }
        }
        u = wrap_add(u, du);
        v = wrap_add(v, dv);
        depth = wrap_add(depth, dd);
        shade = wrap_add(shade, ds);
    }
}

/// Rasterises one convex polygon as today's fills walk it: the topmost
/// corner first, the left side walked backwards and the right side
/// forwards, each span excluding its right end. A triangle of a card's mesh
/// and a whole primitive walk alike.
void raster_polygon(
    Image& image,
    std::span<const Corner> corners,
    const RasterMode& mode,
    const Sprite* texture,
    uint8_t color
) {
    const auto count = static_cast<int32_t>(corners.size());
    int32_t top = corners[0].y;
    int32_t bottom = corners[0].y;
    int32_t top_index = 0;
    int32_t bottom_index = 0;
    int32_t left_x = corners[0].x;
    int32_t right_x = corners[0].x;
    for (int32_t i = 1; i < count; ++i) {
        if (corners[i].y < top) {
            top = corners[i].y;
            top_index = i;
        }
        if (bottom < corners[i].y) {
            bottom = corners[i].y;
            bottom_index = i;
        }
        left_x = std::min(left_x, corners[i].x);
        right_x = std::max(right_x, corners[i].x);
    }
    const int32_t last_row = image.height - 1;
    if (right_x < 0 || left_x > image.width - 1 || bottom < 0 || top > last_row)
        return;
    top = std::max(top, 0);
    bottom = std::min(bottom, last_row);
    if (bottom == top)
        return;
    // Sized as today's span table: a twisted polygon's walk writes past its rows.
    std::vector<Row> rows(static_cast<std::size_t>(bottom - top) * corners.size() + corners.size());
    std::size_t cursor = 0;
    int32_t i = top_index;
    do {
        const int32_t j = i - 1 < 0 ? count - 1 : i - 1;
        scan_edge(rows, cursor, false, corners[i], corners[j], last_row);
        i = j;
    } while (i != bottom_index);
    cursor = 0;
    i = top_index;
    do {
        const int32_t j = i + 1 >= count ? 0 : i + 1;
        scan_edge(rows, cursor, true, corners[i], corners[j], last_row);
        i = j;
    } while (i != bottom_index);
    for (int32_t y = top, r = 0; y < bottom; ++y, ++r) {
        const Row& row = rows[static_cast<std::size_t>(r)];
        if (wrap_sub(row.right, row.left) > 0)
            sample_span(image, y, row, mode, texture, color);
    }
}

/// The frame a texture shows for a mesh primitive, from the sequence the
/// mesh names: the first frame, or the team's; null for a frame today's
/// samplers cannot read.
const Sprite* frame_of(const MeshTexture& texture, uint8_t team) {
    if (texture.sequence == nullptr)
        return nullptr;
    const std::size_t index = texture.kind == TextureKind::team ? team : 0;
    if (index >= texture.sequence->frames.size())
        return nullptr;
    const Sprite& frame = texture.sequence->frames[index];
    return frame.data != nullptr ? &frame : nullptr;
}

/// Places one corner of a mesh for the walk from the transformed vertex it
/// names; its texel is the frame corner's, of the frame shown.
Corner place_corner(
    const MeshVertex& vertex,
    const FixedVector3& point,
    const VertexNormal& normal,
    const Image& image,
    const RasterMode& mode,
    const Sprite* texture,
    bool lit
) {
    const PixelPoint pixel = pixel_of_model_point(point, mode.depth_base);
    Corner corner;
    corner.x = pixel.x + image.origin_x;
    corner.y = pixel.y + image.origin_y;
    corner.depth = pixel.depth;
    if (texture != nullptr) {
        corner.u = static_cast<int32_t>(vertex.u * static_cast<float>(texture->width - 1));
        corner.v = static_cast<int32_t>(vertex.v * static_cast<float>(texture->height - 1));
    }
    corner.shade = draw::unlit_shade;
    if (lit)
        corner.shade = shade_row(normal.x, normal.y, normal.z, mode.light, mode.light_scale);
    return corner;
}

/// Rasterises a mesh into an image, the instance's pieces last to first,
/// each corner placed from the piece's transformed vertex of its 3DO
/// index. A primitive is walked whole, over its consecutive corners, or
/// triangle by triangle.
void raster_mesh(
    Image& image,
    const ModelMesh& mesh,
    const Instance& instance,
    const draw::PreparedModel& prepared,
    const RasterMode& mode
) {
    std::vector<Corner> corners;
    std::vector<VertexNormal> normals;
    const auto pieces = instance.pieces();
    for (std::size_t n = pieces.size(); n != 0; --n) {
        const PieceState& state = pieces[n - 1];
        if ((state.flags & piece_visible) == 0)
            continue;
        const MeshPiece& piece = mesh.pieces[state.object_index];
        const bool lit = mode.shaded && (state.flags & piece_shaded) != 0;
        if (lit && !mode.normals_from_mesh)
            vertex_normals(
                instance.model().objects[state.object_index],
                prepared.objects[state.object_index],
                state.transformed_vertices,
                normals
            );
        const auto corner_of = [&](uint32_t v, const Sprite* texture) {
            const MeshVertex& vertex = mesh.vertices[v];
            const uint16_t source = mesh.source_vertex[v];
            // An unlit corner's normal goes unread.
            const VertexNormal normal =
                mode.normals_from_mesh || !lit
                    ? VertexNormal{vertex.normal_x, vertex.normal_y, vertex.normal_z}
                    : normals[source];
            return place_corner(
                vertex, state.transformed_vertices[source], normal, image, mode, texture, lit
            );
        };
        for (uint32_t p = piece.first_primitive; p < piece.first_primitive + piece.primitive_count;
             ++p) {
            const MeshPrimitive& run = mesh.primitives[p];
            const Sprite* texture = nullptr;
            if ((run.flags & primitive_flag_textured) != 0) {
                texture = frame_of(mesh.textures[run.texture], mode.team);
                if (texture == nullptr)
                    continue;
            }
            if (mode.whole_primitives) {
                corners.clear();
                for (uint32_t k = 0; k < run.corner_count; ++k)
                    corners.push_back(corner_of(run.first_vertex + k, texture));
                raster_polygon(image, corners, mode, texture, run.palette_index);
                continue;
            }
            for (uint32_t t = run.first_index; t + 2 < run.first_index + run.index_count; t += 3) {
                corners.clear();
                for (uint32_t k = 0; k < 3; ++k)
                    corners.push_back(corner_of(mesh.indices[t + k], texture));
                raster_polygon(image, corners, mode, texture, run.palette_index);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Today's images, and the comparison.

/// Builds today's image of a model: every visible piece, with or without a
/// depth plane, as a building with the Shading flag or not.
Image todays_image(
    Instance& instance,
    const draw::PreparedModel& prepared,
    bool depth_plane,
    bool shaded,
    uint8_t team
) {
    oa::Unit record{};
    if (depth_plane)
        record.flags2 |= OA_UNIT_FLAG2_Z_BUFFER;
    if (shaded)
        record.flags |= OA_UNIT_FLAG_BUILDING;
    draw::ModelRenderer renderer;
    renderer.graphics_flags = shaded ? draw::graphics_shading : 0;
    renderer.team_colors[0] = team;
    draw::ModelState state;
    const draw::ModelRef ref{&instance, &prepared, &state, &record, nullptr};
    Image image;
    if (!draw::prepare_model_image(renderer, ref, false, draw::pass_all_pieces))
        return image;
    const Sprite& sprite = state.image.sprite;
    image.width = sprite.width;
    image.height = sprite.height;
    image.origin_x = sprite.origin_x;
    image.origin_y = sprite.origin_y;
    const std::size_t size = static_cast<std::size_t>(sprite.width) * sprite.height;
    image.pixels.assign(
        static_cast<const uint8_t*>(sprite.data), static_cast<const uint8_t*>(sprite.data) + size
    );
    if (sprite.aux != nullptr)
        image.depth.assign(
            static_cast<const uint8_t*>(sprite.aux), static_cast<const uint8_t*>(sprite.aux) + size
        );
    return image;
}

/// An empty image of another's frame, keyed, with a zeroed depth plane when asked.
Image blank_like(const Image& frame, bool depth_plane) {
    Image image;
    image.width = frame.width;
    image.height = frame.height;
    image.origin_x = frame.origin_x;
    image.origin_y = frame.origin_y;
    const std::size_t size = static_cast<std::size_t>(frame.width) * frame.height;
    image.pixels.assign(size, draw::image_key);
    if (depth_plane)
        image.depth.assign(size, 0);
    image.u.assign(size, no_texel);
    image.v.assign(size, no_texel);
    return image;
}

/// How two images of one frame compare.
struct Comparison {
    std::size_t drawn{};    ///< pixels either image draws
    std::size_t differ{};   ///< pixels whose colours differ
    std::size_t coverage{}; ///< pixels only one image draws
    /// Pixels both draw in different colours, each colour drawn by the other
    /// image within one pixel: a texel of phase, as a quad's span
    /// interpolation and a triangle's differ by.
    std::size_t phase{};
    std::size_t far{};          ///< pixels both draw in colours the other has nowhere near
    std::size_t depth_differ{}; ///< depth bytes that differ
    std::size_t depth_far{};    ///< depth bytes that differ by more than one
};

/// Tells whether an image draws a colour within one pixel of a point.
bool draws_near(const Image& image, int32_t x, int32_t y, uint8_t colour) {
    for (int32_t dy = -1; dy <= 1; ++dy) {
        for (int32_t dx = -1; dx <= 1; ++dx) {
            const int32_t nx = x + dx;
            const int32_t ny = y + dy;
            if (nx < 0 || ny < 0 || nx >= image.width || ny >= image.height)
                continue;
            if (image.pixels[static_cast<std::size_t>(ny) * image.width + nx] == colour)
                return true;
        }
    }
    return false;
}

Comparison compare(const Image& a, const Image& b) {
    Comparison c;
    if (a.width != b.width || a.height != b.height)
        return c;
    for (int32_t y = 0; y < a.height; ++y) {
        for (int32_t x = 0; x < a.width; ++x) {
            const std::size_t i = static_cast<std::size_t>(y) * a.width + x;
            const uint8_t today = a.pixels[i];
            const uint8_t mine = b.pixels[i];
            const bool today_drawn = today != draw::image_key;
            const bool mine_drawn = mine != draw::image_key;
            if (!today_drawn && !mine_drawn)
                continue;
            ++c.drawn;
            if (!a.depth.empty() && !b.depth.empty() && a.depth[i] != b.depth[i]) {
                ++c.depth_differ;
                if (std::abs(static_cast<int>(a.depth[i]) - static_cast<int>(b.depth[i])) > 1)
                    ++c.depth_far;
            }
            if (today == mine)
                continue;
            ++c.differ;
            if (today_drawn != mine_drawn)
                ++c.coverage;
            else if (draws_near(a, x, y, mine) && draws_near(b, x, y, today))
                ++c.phase;
            else
                ++c.far;
        }
    }
    return c;
}

/// How far apart two walks of one mesh sample their textures.
struct TexelDistance {
    std::size_t pixels{}; ///< pixels both walks textured
    double mean{};        ///< mean of the larger of |du| and |dv|, in texels
    double largest{};
    std::size_t within_one{}; ///< pixels within one texel
};

TexelDistance texel_distance(const Image& a, const Image& b) {
    TexelDistance d;
    double sum = 0.0;
    for (std::size_t i = 0; i < a.u.size() && i < b.u.size(); ++i) {
        if (a.u[i] == no_texel || b.u[i] == no_texel)
            continue;
        const double du = std::fabs(static_cast<double>(a.u[i]) - b.u[i]) / fixed_one;
        const double dv = std::fabs(static_cast<double>(a.v[i]) - b.v[i]) / fixed_one;
        const double apart = std::max(du, dv);
        ++d.pixels;
        sum += apart;
        d.largest = std::max(d.largest, apart);
        if (apart <= 1.0)
            ++d.within_one;
    }
    d.mean = d.pixels == 0 ? 0.0 : sum / static_cast<double>(d.pixels);
    return d;
}

/// Rasterises a mesh in the frame of today's image of the same model.
Image raster_in_frame(
    const Image& frame,
    const ModelMesh& mesh,
    const Instance& instance,
    const draw::PreparedModel& prepared,
    const draw::ModelDisplay& display,
    bool depth_plane,
    bool shaded,
    bool whole_primitives,
    bool normals_from_mesh,
    uint8_t team
) {
    Image mine = blank_like(frame, depth_plane);
    RasterMode mode;
    mode.depth_plane = depth_plane;
    mode.shaded = shaded;
    mode.whole_primitives = whole_primitives;
    mode.normals_from_mesh = normals_from_mesh;
    mode.shade_table = display.shade.data();
    const float light[3] = {draw::default_light_x, draw::default_light_y, draw::default_light_z};
    mode.light = light;
    mode.light_scale = draw::default_light_scale;
    mode.team = team;
    raster_mesh(mine, mesh, instance, prepared, mode);
    return mine;
}

// ---------------------------------------------------------------------------
// Fixtures.

oa::Palette gray_palette() {
    oa::Palette palette{};
    for (int i = 0; i < OA_PALETTE_COLORS; ++i)
        palette.entries[i] = {
            static_cast<uint8_t>(i), static_cast<uint8_t>(i), static_cast<uint8_t>(i), 0
        };
    return palette;
}

Primitive primitive(
    std::vector<uint16_t> indices, std::string texture = {}, int32_t color = 0, bool colored = false
) {
    Primitive p;
    p.vertex_indices = std::move(indices);
    p.texture_name = std::move(texture);
    p.color_index = color;
    p.is_colored = colored ? 1 : 0;
    return p;
}

// A flat 16x16 square at height 0 with one coloured primitive, wound so
// that it faces the camera.
std::shared_ptr<Model> square_model() {
    auto model = std::make_shared<Model>();
    Object object;
    const int32_t h = 8 * unit;
    object.vertices = {{-h, 0, -h}, {h, 0, -h}, {h, 0, h}, {-h, 0, h}};
    object.primitives.push_back(primitive({0, 3, 2, 1}, {}, ink, true));
    model->objects.push_back(object);
    return model;
}

// The square with a child barrel two units up, offset from the base.
std::shared_ptr<Model> turret_model() {
    auto model = square_model();
    Object barrel;
    const int32_t w = 2 * unit;
    const int32_t y = 2 * unit;
    const int32_t near = 2 * unit;
    const int32_t far = 14 * unit;
    barrel.vertices = {{-w, y, near}, {w, y, near}, {w, y, far}, {-w, y, far}};
    barrel.primitives.push_back(primitive({0, 3, 2, 1}, {}, barrel_ink, true));
    barrel.offset_from_parent = {2 * unit, 3 * unit, -4 * unit};
    barrel.parent = 0;
    model->objects[0].first_child = 1;
    model->objects.push_back(barrel);
    return model;
}

// A texture library of raw frames whose texel is (x + 16 * y + frame) & 0xff,
// each padded for the samplers: a fixed 16x16 texture, a 3-frame 8x8
// animation and a 10-frame 4x4 team texture.
struct TextureFixture {
    std::vector<std::unique_ptr<oa::formats::gaf::Sequence>> sequences;
    std::vector<std::unique_ptr<std::vector<uint8_t>>> texels;
    draw::TextureLibrary library;

    void add(const std::string& name, int frames, uint16_t width, uint16_t height, bool team) {
        auto sequence = std::make_unique<oa::formats::gaf::Sequence>();
        sequence->name = name;
        draw::TextureSequence entry;
        for (int i = 0; i < frames; ++i) {
            oa::formats::gaf::Frame frame;
            frame.width = width;
            frame.height = height;
            frame.duration = 1;
            sequence->frames.push_back(frame);
            auto pixels = std::make_unique<std::vector<uint8_t>>(
                sampler_window + static_cast<std::size_t>(width) * height, 0
            );
            for (int y = 0; y < height; ++y)
                for (int x = 0; x < width; ++x)
                    (*pixels)[static_cast<std::size_t>(y * width + x)] =
                        static_cast<uint8_t>((x + 16 * y + i) & 0xff);
            Sprite sprite{};
            sprite.width = width;
            sprite.height = height;
            sprite.data = pixels->data();
            entry.frames.push_back(sprite);
            texels.push_back(std::move(pixels));
        }
        entry.sequence = sequence.get();
        entry.team_archive = team;
        library.sequences.emplace(name, std::move(entry));
        sequences.push_back(std::move(sequence));
    }

    TextureFixture() {
        add("plain", 1, 16, 16, false);
        add("blink", 3, 8, 8, false);
        add("logo", 10, 4, 4, true);
    }
};

/// A model made of one square per texture, each a textured quad over the
/// same corners, plus the primitives today draws nothing for.
std::shared_ptr<Model> textured_model() {
    auto model = std::make_shared<Model>();
    Object object;
    const int32_t h = 8 * unit;
    object.vertices = {{-h, 0, -h}, {h, 0, -h}, {h, 0, h}, {-h, 0, h}, {0, unit, 0}};
    object.primitives = {
        primitive({0, 3, 2, 1}, "Plain"),
        primitive({0, 3, 2, 1}, "BLINK"),
        primitive({0, 3, 2, 1}, "logo"),
        primitive({0, 3, 2}, "plain"),      // a textured triangle: nothing today
        primitive({0, 1}, {}, ink, true),   // two corners: nothing today
        primitive({0, 3, 2, 1}),            // no colour, no texture: nothing today
        primitive({0, 3, 2, 1}, "missing"), // a missing texture: colour 0xd1
    };
    model->objects.push_back(object);
    return model;
}

/// A flat quad at height 0 whose corners land, in order, on the given screen
/// pixels at rest (x to the right, y down), textured with the plain texture.
std::shared_ptr<Model> quad_model(const std::array<std::array<int32_t, 2>, 4>& screen) {
    auto model = std::make_shared<Model>();
    Object object;
    // Screen x is the negated file x and screen y the file z; the primitive
    // takes the vertices in the order the square's does.
    const uint16_t order[4] = {0, 3, 2, 1};
    object.vertices.resize(4);
    for (std::size_t k = 0; k < 4; ++k)
        object.vertices[order[k]] = {-screen[k][0] * unit, 0, screen[k][1] * unit};
    object.primitives.push_back(primitive({0, 3, 2, 1}, "plain"));
    model->objects.push_back(object);
    return model;
}

/// A model, its instance with the transforms built, and its prepared
/// primitives, ready to mesh.
struct Scene {
    std::shared_ptr<const Model> model;
    Instance instance;
    draw::ModelLibrary library;
    const draw::PreparedModel* prepared{};
    draw::ModelDisplay display;
    oa::Palette palette = gray_palette();

    explicit Scene(std::shared_ptr<Model> shape, draw::TextureLibrary textures = {})
        : model(std::move(shape)), instance(oa::sim::model_runtime::make_instance(model)) {
        library.textures = std::move(textures);
        instance.rebuild_transforms();
        prepared = &draw::prepare_model(library, model);
        draw::build_model_display(display, palette);
        oa::present::bind_display(&display.context);
    }

    ~Scene() { oa::present::bind_display(nullptr); }

    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;

    MeshBuildError build(ModelMesh& mesh, float gamma = 1.0F) const {
        return build_model_mesh(*model, *prepared, library.textures, palette, gamma, mesh);
    }

    /// Rasterises the mesh in the frame of today's image in one mode and
    /// compares the two.
    Comparison compare_with_today(
        const ModelMesh& mesh,
        bool depth_plane,
        bool shaded,
        bool whole_primitives,
        bool normals_from_mesh,
        uint8_t team
    ) {
        const Image today = todays_image(instance, *prepared, depth_plane, shaded, team);
        const Image mine = raster_in_frame(
            today,
            mesh,
            instance,
            *prepared,
            display,
            depth_plane,
            shaded,
            whole_primitives,
            normals_from_mesh,
            team
        );
        return compare(today, mine);
    }

    /// Checks the mesh against today's image in every mode and both walks
    /// in the instance's present pose: the whole walk exactly, and the
    /// triangles' pixels exactly, their depth bytes too unless the pose
    /// slopes a quad in depth, where a triangle's truncated steps along the
    /// diagonal may move a byte by one.
    void check_exact(
        const ModelMesh& mesh,
        uint8_t team,
        bool normals_from_mesh = true,
        bool triangle_depth_exact = true
    ) {
        for (const bool depth_plane : {false, true}) {
            for (const bool shaded : {false, true}) {
                for (const bool whole : {true, false}) {
                    const Comparison c = compare_with_today(
                        mesh, depth_plane, shaded, whole, normals_from_mesh, team
                    );
                    OA_CHECK(c.drawn > 0);
                    OA_CHECK(c.differ == 0);
                    OA_CHECK(c.depth_far == 0);
                    const bool depth_exact = whole || triangle_depth_exact;
                    if (depth_exact)
                        OA_CHECK(c.depth_differ == 0);
                    if (c.differ != 0 || c.depth_far != 0 || (depth_exact && c.depth_differ != 0))
                        std::fprintf(
                            stderr,
                            "depth %d shaded %d whole %d: %zu of %zu pixels differ, %zu depths\n",
                            depth_plane ? 1 : 0,
                            shaded ? 1 : 0,
                            whole ? 1 : 0,
                            c.differ,
                            c.drawn,
                            c.depth_differ
                        );
                }
            }
        }
    }
};

// ---------------------------------------------------------------------------
// Cases.

// The matrix projects (x, y, z) to (x, -z - y/2, y); pixel_of_model_point
// places a point as today's hi() arithmetic does, from a float and from the
// 16.16 value, 16.16 values of every sign and half included.
void test_projection() {
    const ProjectedPoint p = project_model_point(3.0F, 4.0F, 5.0F);
    OA_CHECK(p.x == 3.0F && p.y == -7.0F && p.depth == 4.0F);
    const int32_t samples[] = {
        0,
        unit,
        -unit,
        3 * unit + unit / 2,
        -(3 * unit + unit / 2),
        7 * unit + 1,
        -1,
        unit / 4,
        -(unit / 4),
        1000 * unit + 0x8000,
        -(1000 * unit) - 0x8000
    };
    for (const int32_t x : samples) {
        for (const int32_t y : samples) {
            for (const int32_t z : samples) {
                const PixelPoint pixel =
                    pixel_of_model_point(world(x), world(y), world(z), draw::model_depth_base);
                OA_CHECK(pixel.x == hi(x));
                OA_CHECK(pixel.y == hi(wrap_sub(0, z)) - (hi(y) >> 1));
                OA_CHECK(pixel.depth == draw::model_depth_base + hi(y));
                const PixelPoint fixed =
                    pixel_of_model_point(FixedVector3{x, y, z}, draw::model_depth_base);
                OA_CHECK(fixed.x == pixel.x && fixed.y == pixel.y && fixed.depth == pixel.depth);
            }
        }
    }
    // The 16.16 form places a coordinate a float cannot hold where today's
    // arithmetic does.
    const int32_t huge = 300 * unit + 1;
    const PixelPoint far = pixel_of_model_point(FixedVector3{huge, -huge, huge}, 0);
    OA_CHECK(far.x == 300 && far.y == -301 - (-301 >> 1) && far.depth == hi(-huge));
    // Flooring the matrix's screen y whole moves a corner up one row exactly
    // where the fraction of -z is below the fraction of y / 2.
    const PixelPoint apart = pixel_of_model_point(0.0F, 1.0F, -0.25F, 0);
    OA_CHECK(apart.y == 0); // floor(0.25) - floor(1) / 2
    const ProjectedPoint whole = project_model_point(0.0F, 1.0F, -0.25F);
    OA_CHECK(static_cast<int32_t>(std::floor(whole.y)) == -1);
}

// The square: one piece, four corners in the loaded orientation naming their
// 3DO vertices, two triangles from corner 0 (a parallelogram ties), the
// palette colour with the gamma applied, the downward normal of its winding,
// and the same pixels as today's images.
void test_square_mesh() {
    Scene scene(square_model());
    ModelMesh mesh;
    const MeshBuildError error = scene.build(mesh);
    OA_CHECK(error.message == nullptr);
    OA_CHECK(mesh.pieces.size() == 1);
    OA_CHECK(mesh.vertices.size() == 4);
    OA_CHECK(mesh.source_vertex.size() == 4);
    OA_CHECK(mesh.indices.size() == 6);
    OA_CHECK(mesh.primitives.size() == 1);
    OA_CHECK(mesh.textures.empty());
    OA_CHECK(mesh.inexact_positions == 0);
    const std::vector<uint32_t> fan = {0, 1, 2, 0, 2, 3};
    OA_CHECK(mesh.indices == fan);
    const std::vector<uint16_t> sources = {0, 3, 2, 1};
    OA_CHECK(mesh.source_vertex == sources);
    const MeshVertex& first = mesh.vertices[0];
    // Vertex 0 of the file, (-8, 0, -8), lands at (8, 0, 8) once x and z
    // are negated.
    OA_CHECK(first.x == 8.0F && first.y == 0.0F && first.z == 8.0F);
    OA_CHECK(first.red == ink && first.green == ink && first.blue == ink && first.alpha == 255);
    OA_CHECK(first.palette_index == ink && first.flags == 0 && first.piece == 0);
    OA_CHECK(first.normal_x == 0.0F && first.normal_y == -1.0F && first.normal_z == 0.0F);
    const MeshPrimitive& run = mesh.primitives[0];
    OA_CHECK(run.texture == no_texture);
    OA_CHECK(run.index_count == 6 && run.first_index == 0);
    OA_CHECK(run.first_vertex == 0 && run.corner_count == 4);
    OA_CHECK(run.source_index == 0 && run.prepared_index == 0);
    OA_CHECK(run.palette_index == ink);
    OA_CHECK(mesh.pieces[0].parent == no_piece && mesh.pieces[0].vertex_count == 4);
    // The normals the builder averages are the mesh's.
    std::vector<VertexNormal> normals;
    vertex_normals(
        scene.model->objects[0],
        scene.prepared->objects[0],
        scene.instance.pieces()[0].transformed_vertices,
        normals
    );
    OA_CHECK(normals.size() == 4);
    for (const VertexNormal& normal : normals)
        OA_CHECK(normal.x == 0.0F && normal.y == -1.0F && normal.z == 0.0F);
    // The downward normal under the default light gives the wrapped row 27.
    const float light[3] = {draw::default_light_x, draw::default_light_y, draw::default_light_z};
    OA_CHECK(shade_row(0.0F, -1.0F, 0.0F, light, draw::default_light_scale) == 27);
    OA_CHECK(shade_row(0.0F, 1.0F, 0.0F, light, draw::default_light_scale) == 5);
    scene.check_exact(mesh, 0);
    // A gamma of 2 doubles the channels, clamped at 255.
    ModelMesh bright;
    OA_CHECK(scene.build(bright, 2.0F).message == nullptr);
    OA_CHECK(bright.vertices[0].red == 2 * ink && bright.vertices[0].palette_index == ink);
    ModelMesh clamped;
    scene.palette.entries[ink] = {200, 100, 0, 0};
    OA_CHECK(scene.build(clamped, 2.0F).message == nullptr);
    OA_CHECK(clamped.vertices[0].red == 255 && clamped.vertices[0].green == 200);
    OA_CHECK(clamped.vertices[0].blue == 0);
}

// Textured primitives: the texture table with its sequences, the texel
// corners, the flags, the primitives that add nothing, and the same pixels
// as today's with the team's frame.
void test_textured_mesh() {
    TextureFixture textures;
    Scene scene(textured_model(), std::move(textures.library));
    ModelMesh mesh;
    const MeshBuildError error = scene.build(mesh);
    OA_CHECK(error.message == nullptr);
    OA_CHECK(mesh.textures.size() == 3);
    OA_CHECK(mesh.primitives.size() == 4);
    OA_CHECK(mesh.vertices.size() == 16);
    OA_CHECK(mesh.source_vertex.size() == 16);
    OA_CHECK(mesh.dropped_textured_primitives == 1);
    OA_CHECK(mesh.dropped_short_primitives == 1);
    OA_CHECK(mesh.dropped_invisible_primitives == 1);
    const auto texture_named = [&](const std::string& name) -> const MeshTexture* {
        for (const MeshTexture& texture : mesh.textures)
            if (texture.name == name)
                return &texture;
        return nullptr;
    };
    const MeshTexture* plain = texture_named("plain");
    const MeshTexture* blink = texture_named("blink");
    const MeshTexture* logo = texture_named("logo");
    OA_CHECK(plain != nullptr && blink != nullptr && logo != nullptr);
    if (plain == nullptr || blink == nullptr || logo == nullptr)
        return;
    OA_CHECK(plain->kind == TextureKind::fixed && plain->frame_count == 1);
    OA_CHECK(plain->width == 16 && plain->height == 16 && plain->readable);
    OA_CHECK(blink->kind == TextureKind::animated && blink->frame_count == 3);
    OA_CHECK(logo->kind == TextureKind::team && logo->frame_count == 10 && logo->width == 4);
    // Each names the library's own entry, whose GAF sequence names the texture.
    OA_CHECK(plain->sequence == &scene.library.textures.sequences.at("plain"));
    OA_CHECK(blink->sequence == &scene.library.textures.sequences.at("blink"));
    OA_CHECK(logo->sequence == &scene.library.textures.sequences.at("logo"));
    OA_CHECK(logo->sequence != nullptr && logo->sequence->sequence->name == "logo");
    OA_CHECK(logo->sequence != nullptr && logo->sequence->sequence->frames.size() == 10);
    // The primitives keep the prepared order: the three quads, then the
    // missing texture as a colour, each naming its 3DO and prepared primitive.
    OA_CHECK(mesh.primitives[0].texture == static_cast<uint32_t>(plain - mesh.textures.data()));
    OA_CHECK(mesh.primitives[0].flags == primitive_flag_textured);
    OA_CHECK(mesh.primitives[1].flags == (primitive_flag_textured | primitive_flag_animated));
    OA_CHECK(mesh.primitives[2].flags == (primitive_flag_textured | primitive_flag_team));
    OA_CHECK(mesh.primitives[3].texture == no_texture);
    OA_CHECK(mesh.primitives[3].palette_index == draw::missing_texture_color);
    const uint32_t sources[4] = {0, 1, 2, 6};
    for (uint32_t i = 0; i < 4; ++i) {
        OA_CHECK(mesh.primitives[i].source_index == sources[i]);
        OA_CHECK(mesh.primitives[i].prepared_index == sources[i]);
        OA_CHECK(mesh.primitives[i].first_vertex == 4 * i);
        OA_CHECK(mesh.primitives[i].corner_count == 4);
        OA_CHECK(
            scene.prepared->objects[0].primitives[mesh.primitives[i].prepared_index].source_index ==
            sources[i]
        );
    }
    // The corners of the plain quad map to the frame's corners in order.
    const MeshVertex* corners = mesh.vertices.data();
    OA_CHECK(corners[0].u == 0.0F && corners[0].v == 0.0F);
    OA_CHECK(corners[1].u == 1.0F && corners[1].v == 0.0F);
    OA_CHECK(corners[2].u == 1.0F && corners[2].v == 1.0F);
    OA_CHECK(corners[3].u == 0.0F && corners[3].v == 1.0F);
    OA_CHECK(!plain->frame_sizes_vary && !logo->frame_sizes_vary);
    OA_CHECK(corners[0].red == 255 && corners[0].alpha == 255 && corners[0].palette_index == 0);
    OA_CHECK(corners[0].flags == primitive_flag_textured);
    scene.check_exact(mesh, team_color);
    scene.check_exact(mesh, 0);
}

// The turret: the barrel's piece hangs from the base with its offset; the
// corners placed from the processor's transformed vertices give today's
// pixels at rest, and turned: the pieces turned and moved by their words
// under a root rotation of heading alone, where the mesh's normals still
// hold, and under bank and pitch with the barrel tilted, where today's rows
// come from the normals of the transformed vertices.
void test_hierarchy_and_poses() {
    Scene scene(turret_model());
    ModelMesh mesh;
    OA_CHECK(scene.build(mesh).message == nullptr);
    OA_CHECK(mesh.pieces.size() == 2);
    OA_CHECK(mesh.pieces[1].parent == 0 && mesh.pieces[1].object_index == 1);
    OA_CHECK(mesh.pieces[1].offset_x == -2.0F && mesh.pieces[1].offset_y == 3.0F);
    OA_CHECK(mesh.pieces[1].offset_z == 4.0F);
    OA_CHECK(mesh.pieces[1].first_vertex == 4 && mesh.pieces[1].vertex_count == 4);
    OA_CHECK(mesh.pieces[1].first_primitive == 1 && mesh.pieces[1].primitive_count == 1);
    const std::vector<uint16_t> sources = {0, 3, 2, 1, 0, 3, 2, 1};
    OA_CHECK(mesh.source_vertex == sources);
    for (std::size_t v = 0; v < mesh.vertices.size(); ++v)
        OA_CHECK(mesh.vertices[v].piece == (v < 4 ? 0 : 1));
    scene.check_exact(mesh, 0);
    // Turned about the vertical axis: the barrel a quarter turn and moved,
    // the base an eighth, the unit's heading three sixteenths. Every normal
    // stays vertical, so the mesh's normals are today's.
    auto pieces = scene.instance.pieces();
    pieces[1].rotation.xz = 0x4000;
    pieces[0].rotation.xz = 0x2000;
    pieces[1].translation = {unit, 0, -unit};
    scene.instance.rebuild_transforms(heading_only);
    scene.check_exact(mesh, 0);
    // Banked and pitched, the barrel tilted: today's builder averages the
    // normals of the transformed vertices, which lean with the pieces.
    pieces[1].rotation.yz = 0x1000;
    scene.instance.rebuild_transforms(banked_and_pitched);
    std::vector<VertexNormal> tilted;
    vertex_normals(
        scene.model->objects[1], scene.prepared->objects[1], pieces[1].transformed_vertices, tilted
    );
    OA_CHECK(tilted.size() == 4);
    OA_CHECK(tilted[0].y != mesh.vertices[4].normal_y || tilted[0].z != mesh.vertices[4].normal_z);
    scene.check_exact(mesh, 0, false, false);
    // Back at rest, a stale pose draws nothing different from today's.
    for (PieceState& piece : pieces) {
        piece.rotation = {};
        piece.translation = {};
    }
    scene.instance.rebuild_transforms();
    scene.check_exact(mesh, 0);
}

// Two quads today's whole walk interpolates away from both diagonals: a
// wedge, whose first diagonal the walk follows nearer, and a kite, whose
// second it does. The split follows the walk at the diagonals' midpoints,
// which the whole walk's recorded texels show; the whole walk gives today's
// pixels exactly either way, and the chosen triangles come nearer today's
// than the other split would.
void test_quad_diagonal() {
    struct Case {
        const char* name;
        std::array<std::array<int32_t, 2>, 4> screen;
        bool from_second_corner;
    };

    const Case cases[] = {
        {"wedge", {{{0, 0}, {24, 0}, {30, 8}, {4, 20}}}, false},
        {"kite", {{{8, 0}, {20, 4}, {14, 16}, {0, 8}}}, true},
    };
    const std::vector<uint32_t> fan_from_second = {1, 2, 3, 1, 3, 0};
    const std::vector<uint32_t> fan_from_first = {0, 1, 2, 0, 2, 3};
    constexpr double plain_texels = 15.0; // the plain texture's last texel along each axis
    for (const Case& c : cases) {
        TextureFixture textures;
        Scene scene(quad_model(c.screen), std::move(textures.library));
        ModelMesh mesh;
        OA_CHECK(scene.build(mesh).message == nullptr);
        OA_CHECK(mesh.primitives.size() == 1 && mesh.indices.size() == 6);
        OA_CHECK(mesh.indices == (c.from_second_corner ? fan_from_second : fan_from_first));
        // The whole walk's texels at the midpoint of each diagonal, against
        // the diagonal's own midpoint texel: the split joins the nearer.
        const Image today = todays_image(scene.instance, *scene.prepared, false, false, 0);
        const Image whole = raster_in_frame(
            today, mesh, scene.instance, *scene.prepared, scene.display, false, false, true, true, 0
        );
        OA_CHECK(whole.width > 0 && compare(today, whole).differ == 0);
        double deviation[2] = {};
        for (std::size_t d = 0; d < 2; ++d) {
            const int32_t x = (c.screen[d][0] + c.screen[d + 2][0]) / 2 + whole.origin_x;
            const int32_t y = (c.screen[d][1] + c.screen[d + 2][1]) / 2 + whole.origin_y;
            const std::size_t at = static_cast<std::size_t>(y) * whole.width + x;
            OA_CHECK(whole.u[at] != no_texel);
            const double u = whole.u[at] / static_cast<double>(fixed_one) / plain_texels;
            const double v = whole.v[at] / static_cast<double>(fixed_one) / plain_texels;
            deviation[d] = std::max(std::fabs(u - 0.5), std::fabs(v - 0.5));
        }
        OA_CHECK((deviation[1] < deviation[0]) == c.from_second_corner);
        const Comparison chosen = scene.compare_with_today(mesh, false, false, false, true, 0);
        ModelMesh other = mesh;
        other.indices = c.from_second_corner ? fan_from_first : fan_from_second;
        const Comparison swapped = scene.compare_with_today(other, false, false, false, true, 0);
        OA_CHECK(chosen.far <= swapped.far);
        std::printf(
            "%s: the walk strays %.3f and %.3f from the diagonals' midpoints; the chosen split "
            "differs from today's on %zu of %zu pixels (%zu far), the other on %zu (%zu far)\n",
            c.name,
            deviation[0],
            deviation[1],
            chosen.differ,
            chosen.drawn,
            chosen.far,
            swapped.differ,
            swapped.far
        );
    }
}

// Malformed models and prepared models that do not match are refused with
// the fault named.
void test_refuses_malformed() {
    draw::ModelLibrary library;
    const oa::Palette palette = gray_palette();
    ModelMesh mesh;
    {
        const auto empty = std::make_shared<const Model>();
        draw::PreparedModel prepared;
        const MeshBuildError error =
            build_model_mesh(*empty, prepared, library.textures, palette, 1.0F, mesh);
        OA_CHECK(error.message != nullptr && std::strstr(error.message, "no objects") != nullptr);
    }
    {
        // A vertex index past the object's vertices.
        auto model = square_model();
        model->objects[0].primitives.push_back(primitive({0, 1, 9}, {}, ink, true));
        const std::shared_ptr<const Model> handle = model;
        const draw::PreparedModel& prepared = draw::prepare_model(library, handle);
        const MeshBuildError error =
            build_model_mesh(*handle, prepared, library.textures, palette, 1.0F, mesh);
        OA_CHECK(error.message != nullptr && std::strstr(error.message, "vertex") != nullptr);
        OA_CHECK(error.object == 0 && error.primitive == 1);
        OA_CHECK(mesh.vertices.empty() && mesh.pieces.empty());
    }
    {
        // A child that names itself as its parent.
        auto model = turret_model();
        model->objects[1].parent = 1;
        const std::shared_ptr<const Model> handle = model;
        const draw::PreparedModel& prepared = draw::prepare_model(library, handle);
        const MeshBuildError error =
            build_model_mesh(*handle, prepared, library.textures, palette, 1.0F, mesh);
        OA_CHECK(error.message != nullptr && std::strstr(error.message, "hierarchy") != nullptr);
    }
    {
        // A prepared model of another shape.
        const std::shared_ptr<const Model> handle = square_model();
        draw::PreparedModel prepared;
        const MeshBuildError error =
            build_model_mesh(*handle, prepared, library.textures, palette, 1.0F, mesh);
        OA_CHECK(error.message != nullptr && std::strstr(error.message, "prepared") != nullptr);
    }
    {
        // A prepared model of another model of the same shape.
        const std::shared_ptr<const Model> first = square_model();
        const std::shared_ptr<const Model> second = square_model();
        const draw::PreparedModel& prepared_first = draw::prepare_model(library, first);
        const draw::PreparedModel& prepared_second = draw::prepare_model(library, second);
        OA_CHECK(
            build_model_mesh(*first, prepared_first, library.textures, palette, 1.0F, mesh)
                .message == nullptr
        );
        const MeshBuildError error =
            build_model_mesh(*first, prepared_second, library.textures, palette, 1.0F, mesh);
        OA_CHECK(
            error.message != nullptr && std::strstr(error.message, "another model") != nullptr
        );
        OA_CHECK(mesh.vertices.empty());
    }
    {
        // A prepared model whose model has been freed: nothing owns it.
        const std::shared_ptr<const Model> handle = square_model();
        draw::PreparedModel prepared;
        prepared.model = handle.get();
        prepared.objects.resize(1);
        prepared.objects[0].primitives.resize(1);
        const MeshBuildError error =
            build_model_mesh(*handle, prepared, library.textures, palette, 1.0F, mesh);
        OA_CHECK(error.message != nullptr && std::strstr(error.message, "freed") != nullptr);
    }
    {
        // A primitive of more corners than the bound.
        auto model = square_model();
        model->objects[0].primitives.push_back(
            primitive(std::vector<uint16_t>(max_primitive_corners + 1, 0), {}, ink, true)
        );
        const std::shared_ptr<const Model> handle = model;
        const draw::PreparedModel& prepared = draw::prepare_model(library, handle);
        const MeshBuildError error =
            build_model_mesh(*handle, prepared, library.textures, palette, 1.0F, mesh);
        OA_CHECK(error.message != nullptr && std::strstr(error.message, "corners") != nullptr);
    }
}

// The memory a mesh holds: 40 bytes a corner and 2 for its 3DO vertex, 4 an
// index, and the runs, pieces and texture names.
void test_memory() {
    OA_CHECK(sizeof(MeshVertex) == 40);
    Scene scene(square_model());
    ModelMesh mesh;
    OA_CHECK(scene.build(mesh).message == nullptr);
    const std::size_t expected = 4 * (sizeof(MeshVertex) + sizeof(uint16_t)) +
                                 6 * sizeof(uint32_t) + sizeof(MeshPrimitive) + sizeof(MeshPiece);
    OA_CHECK(mesh_bytes(mesh) == expected);
    TextureFixture textures;
    Scene textured(textured_model(), std::move(textures.library));
    ModelMesh with_textures;
    OA_CHECK(textured.build(with_textures).message == nullptr);
    std::size_t names = 0;
    for (const MeshTexture& texture : with_textures.textures)
        names += sizeof(MeshTexture) + texture.name.size();
    OA_CHECK(
        mesh_bytes(with_textures) == 16 * (sizeof(MeshVertex) + sizeof(uint16_t)) +
                                         24 * sizeof(uint32_t) + 4 * sizeof(MeshPrimitive) +
                                         sizeof(MeshPiece) + names
    );
}

// ---------------------------------------------------------------------------
// The installed game.

/// Reads the model name of every unit of the installation: UNITINFO's
/// Objectname, or its UnitName when it has none.
std::vector<std::string> installed_unit_models(const oa::AssetStore& assets) {
    std::set<std::string> names;
    for (const std::string& path : assets.list_effective_in_mount_order("units", ".fbi")) {
        const auto bytes = assets.load_file_contents(path);
        if (!bytes || bytes->empty() || bytes->size() > oa::formats::tdf::max_input_bytes)
            continue;
        oa::formats::tdf::Document document;
        oa::formats::tdf::document_init(&document);
        oa::formats::tdf::ParseError error;
        if (oa::formats::tdf::parse_text(
                &document,
                reinterpret_cast<const char*>(bytes->data()),
                static_cast<uint32_t>(bytes->size()),
                true,
                &error
            )) {
            const auto* info = oa::formats::tdf::find_child(document.root, "UNITINFO");
            const char* name =
                info != nullptr ? oa::formats::tdf::find_value(info, "Objectname") : nullptr;
            if ((name == nullptr || *name == '\0') && info != nullptr)
                name = oa::formats::tdf::find_value(info, "UnitName");
            if (name != nullptr && *name != '\0') {
                std::string folded(name);
                for (char& c : folded)
                    if (c >= 'A' && c <= 'Z')
                        c = static_cast<char>(c - 'A' + 'a');
                names.insert(folded);
            }
        }
        oa::formats::tdf::document_free(&document);
    }
    return {names.begin(), names.end()};
}

/// The worst model of a mode by one measure.
struct Worst {
    std::string name;
    double value{};
    Comparison comparison;
};

/// Keeps the larger of a worst so far and a model's value.
void keep_worst(Worst& worst, const std::string& name, double value, const Comparison& c) {
    if (value > worst.value || worst.name.empty()) {
        worst.name = name;
        worst.value = value;
        worst.comparison = c;
    }
}

struct ModeTotals {
    const char* name{};
    bool depth_plane{};
    bool shaded{};
    std::size_t models{};
    std::size_t exact{};
    std::size_t drawn{};
    std::size_t differ{};
    std::size_t coverage{};
    std::size_t phase{};
    std::size_t far{};
    std::size_t depth_differ{};
    std::size_t over_tolerance{};
    Worst worst_differ; ///< the largest share of pixels differing
    Worst worst_far;    ///< the largest share of pixels farther than a texel of phase
    // The card's walk against the whole walk: texel distances.
    std::size_t textured_pixels{};
    std::size_t within_one_texel{};
    double texel_sum{};
    double texel_largest{};
    std::string texel_largest_model;
};

/// Adds one model's comparison to a mode's totals.
void add_comparison(ModeTotals& totals, const std::string& name, const Comparison& c) {
    ++totals.models;
    totals.drawn += c.drawn;
    totals.differ += c.differ;
    totals.coverage += c.coverage;
    totals.phase += c.phase;
    totals.far += c.far;
    totals.depth_differ += c.depth_differ;
    if (c.differ == 0)
        ++totals.exact;
    const double drawn = c.drawn == 0 ? 1.0 : static_cast<double>(c.drawn);
    keep_worst(totals.worst_differ, name, static_cast<double>(c.differ) / drawn, c);
    keep_worst(totals.worst_far, name, static_cast<double>(c.far) / drawn, c);
}

/// Prints a mode's totals.
void print_totals(const ModeTotals& totals) {
    const auto percent = [&](std::size_t count) {
        return totals.drawn == 0
                   ? 0.0
                   : 100.0 * static_cast<double>(count) / static_cast<double>(totals.drawn);
    };
    std::printf(
        "%s: %zu models, %zu exact; of %zu drawn pixels %zu differ (%.2f%%): %zu coverage "
        "(%.3f%%), %zu a texel of phase (%.2f%%), %zu farther (%.2f%%); %zu depth bytes differ; "
        "worst %s with %.2f%% differing (%zu of %zu), %s with %.2f%% farther (%zu of %zu)\n",
        totals.name,
        totals.models,
        totals.exact,
        totals.drawn,
        totals.differ,
        percent(totals.differ),
        totals.coverage,
        percent(totals.coverage),
        totals.phase,
        percent(totals.phase),
        totals.far,
        percent(totals.far),
        totals.depth_differ,
        totals.worst_differ.name.c_str(),
        totals.worst_differ.value * 100.0,
        totals.worst_differ.comparison.differ,
        totals.worst_differ.comparison.drawn,
        totals.worst_far.name.c_str(),
        totals.worst_far.value * 100.0,
        totals.worst_far.comparison.far,
        totals.worst_far.comparison.drawn
    );
    if (totals.textured_pixels != 0)
        std::printf(
            "    texels: over %zu textured pixels the card's walk samples %.3f texels from the "
            "whole walk on average, %.2f at most (%s); %.2f%% within one texel\n",
            totals.textured_pixels,
            totals.texel_sum / static_cast<double>(totals.textured_pixels),
            totals.texel_largest,
            totals.texel_largest_model.c_str(),
            100.0 * static_cast<double>(totals.within_one_texel) /
                static_cast<double>(totals.textured_pixels)
        );
}

/// One pose every installed model is drawn in.
struct Pose {
    const char* name{};
    RotationWords root_rotation{};
    /// Whether a lit corner's shade row comes from the mesh's normal, which
    /// is today's for a piece the pose leaves unturned, or from the normals
    /// of the transformed vertices, which is today's for any pose.
    bool normals_from_mesh{};
    double largest_texels{};   ///< no model's texture sampled farther apart than this
    double mean_texels{};      ///< a mode's mean texel distance stays within this
    double least_within_one{}; ///< a mode's share of textured pixels within one texel
};

/// The totals of a pose: each mode, walked whole and triangle by triangle.
struct PoseTotals {
    Pose pose;
    ModeTotals whole[4];
    ModeTotals triangles[4];
};

/// Draws one model in one pose, in the four modes and both walks, against
/// today's images.
void compare_installed_model(
    PoseTotals& totals,
    const std::string& name,
    const ModelMesh& mesh,
    Instance& instance,
    const draw::PreparedModel& prepared,
    const draw::ModelDisplay& display
) {
    instance.rebuild_transforms(totals.pose.root_rotation);
    for (int m = 0; m < 4; ++m) {
        ModeTotals& whole = totals.whole[m];
        ModeTotals& triangles = totals.triangles[m];
        const bool depth_plane = whole.depth_plane;
        const bool shaded = whole.shaded;
        const Image today = todays_image(instance, prepared, depth_plane, shaded, team_color);
        const Image as_whole = raster_in_frame(
            today,
            mesh,
            instance,
            prepared,
            display,
            depth_plane,
            shaded,
            true,
            totals.pose.normals_from_mesh,
            team_color
        );
        const Image as_triangles = raster_in_frame(
            today,
            mesh,
            instance,
            prepared,
            display,
            depth_plane,
            shaded,
            false,
            totals.pose.normals_from_mesh,
            team_color
        );
        const Comparison whole_comparison = compare(today, as_whole);
        const Comparison triangle_comparison = compare(today, as_triangles);
        add_comparison(whole, name, whole_comparison);
        add_comparison(triangles, name, triangle_comparison);
        if (whole_comparison.differ != 0 || whole_comparison.depth_differ != 0) {
            ++whole.over_tolerance;
            std::fprintf(
                stderr,
                "%s (%s, %s, whole): %zu of %zu pixels differ, %zu depth bytes\n",
                name.c_str(),
                totals.pose.name,
                whole.name,
                whole_comparison.differ,
                whole_comparison.drawn,
                whole_comparison.depth_differ
            );
        }
        const double drawn =
            triangle_comparison.drawn == 0 ? 1.0 : static_cast<double>(triangle_comparison.drawn);
        const double coverage_share = static_cast<double>(triangle_comparison.coverage) / drawn;
        const double far_share = static_cast<double>(triangle_comparison.far) / drawn;
        const TexelDistance apart = texel_distance(as_whole, as_triangles);
        const bool covers_apart = triangle_comparison.coverage > tolerated_coverage_pixels &&
                                  coverage_share > tolerated_coverage;
        if (covers_apart || far_share > tolerated_far_share ||
            apart.largest > totals.pose.largest_texels) {
            ++triangles.over_tolerance;
            std::fprintf(
                stderr,
                "%s (%s, %s, triangles): of %zu pixels %zu covered by one walk alone, %zu farther "
                "than a texel of phase; texels sampled up to %.2f apart\n",
                name.c_str(),
                totals.pose.name,
                triangles.name,
                triangle_comparison.drawn,
                triangle_comparison.coverage,
                triangle_comparison.far,
                apart.largest
            );
        }
        triangles.textured_pixels += apart.pixels;
        triangles.within_one_texel += apart.within_one;
        triangles.texel_sum += apart.mean * static_cast<double>(apart.pixels);
        if (apart.largest > triangles.texel_largest) {
            triangles.texel_largest = apart.largest;
            triangles.texel_largest_model = name;
        }
    }
}

/// Prints and holds a pose's totals.
void check_pose_totals(const PoseTotals& totals) {
    std::printf("%s, primitives walked whole, as today's fills walk them:\n", totals.pose.name);
    for (const ModeTotals& mode : totals.whole) {
        print_totals(mode);
        OA_CHECK(mode.models >= least_installed_models);
        OA_CHECK(mode.over_tolerance == 0);
    }
    std::printf(
        "%s, primitives walked triangle by triangle, as a card draws them:\n", totals.pose.name
    );
    for (const ModeTotals& mode : totals.triangles) {
        print_totals(mode);
        OA_CHECK(mode.models >= least_installed_models);
        OA_CHECK(mode.over_tolerance == 0);
        OA_CHECK(mode.textured_pixels != 0);
        OA_CHECK(
            mode.texel_sum <= totals.pose.mean_texels * static_cast<double>(mode.textured_pixels)
        );
        OA_CHECK(
            static_cast<double>(mode.within_one_texel) >=
            totals.pose.least_within_one * static_cast<double>(mode.textured_pixels)
        );
    }
}

// Every unit model of the installed game, meshed and rasterised from its
// mesh against today's images in four modes and two poses, at rest and
// turned, each primitive walked whole (the mesh's data against today's) and
// triangle by triangle (what a card draws), and the memory the meshes take.
void test_installed_unit_models(oa::AssetStore& assets) {
    const auto pal = oa::test::read_game_file(assets, "palettes/palette.pal");
    OA_CHECK(!pal.empty());
    const oa::Palette palette = oa::present::palette_from_bytes(pal);
    draw::ModelLibrary library;
    library.textures = draw::load_texture_library(assets);
    draw::ModelDisplay display;
    draw::build_model_display(display, palette);
    oa::present::bind_display(&display.context);
    const std::vector<std::string> names = installed_unit_models(assets);
    std::printf("units: %zu model names\n", names.size());
    PoseTotals poses[2];
    poses[0].pose = {
        "at rest",
        RotationWords{},
        true,
        tolerated_largest_texels_at_rest,
        tolerated_mean_texels_at_rest,
        least_within_one_texel_at_rest
    };
    poses[1].pose = {
        "turned",
        installed_turn,
        false,
        tolerated_largest_texels_turned,
        tolerated_mean_texels_turned,
        least_within_one_texel_turned
    };
    const char* mode_names[4] = {
        "flat image, unlit", "depth image, unlit", "flat image, lit", "depth image, lit"
    };
    for (PoseTotals& pose : poses) {
        for (int m = 0; m < 4; ++m) {
            pose.whole[m].name = pose.triangles[m].name = mode_names[m];
            pose.whole[m].depth_plane = pose.triangles[m].depth_plane = (m & 1) != 0;
            pose.whole[m].shaded = pose.triangles[m].shaded = (m & 2) != 0;
        }
    }
    std::size_t loaded = 0;
    std::size_t missing = 0;
    std::size_t refused_instances = 0;
    std::size_t refused_meshes = 0;
    std::size_t total_bytes = 0;
    std::size_t largest_bytes = 0;
    std::string largest;
    std::size_t total_vertices = 0;
    std::size_t total_triangles = 0;
    std::size_t quads_from_second_corner = 0;
    std::size_t quads = 0;
    std::size_t inexact = 0;
    std::size_t dropped_textured = 0;
    std::size_t dropped_invisible = 0;
    std::size_t dropped_short = 0;
    std::size_t unreadable_textures = 0;
    std::set<std::string> varying_textures;
    std::size_t one_row_corners = 0;
    std::size_t corners = 0;
    std::vector<std::shared_ptr<const Model>> kept;
    for (const std::string& name : names) {
        const auto bytes = assets.load_file_contents("objects3d/" + name + ".3do");
        if (!bytes || bytes->empty()) {
            ++missing;
            continue;
        }
        auto loaded_model = oa::formats::objects3d::load_3do(std::as_bytes(std::span(*bytes)));
        if (!loaded_model.ok()) {
            ++missing;
            continue;
        }
        ++loaded;
        auto model = std::make_shared<const Model>(std::move(*loaded_model.value));
        kept.push_back(model);
        Instance instance = oa::sim::model_runtime::make_instance(model);
        if (instance.pieces().empty()) {
            ++refused_instances;
            continue;
        }
        instance.rebuild_transforms();
        const draw::PreparedModel& prepared = draw::prepare_model(library, model);
        ModelMesh mesh;
        const MeshBuildError error =
            build_model_mesh(*model, prepared, library.textures, palette, 1.0F, mesh);
        if (error.message != nullptr) {
            ++refused_meshes;
            std::fprintf(stderr, "%s: refused: %s\n", name.c_str(), error.message);
            continue;
        }
        OA_CHECK(mesh.pieces.size() == model->objects.size());
        OA_CHECK(mesh.source_vertex.size() == mesh.vertices.size());
        const std::size_t bytes_held = mesh_bytes(mesh);
        total_bytes += bytes_held;
        if (bytes_held > largest_bytes) {
            largest_bytes = bytes_held;
            largest = name;
        }
        total_vertices += mesh.vertices.size();
        total_triangles += mesh.indices.size() / 3;
        inexact += mesh.inexact_positions;
        dropped_textured += mesh.dropped_textured_primitives;
        dropped_invisible += mesh.dropped_invisible_primitives;
        dropped_short += mesh.dropped_short_primitives;
        for (const MeshTexture& texture : mesh.textures) {
            OA_CHECK(texture.sequence == &library.textures.sequences.at(texture.name));
            if (!texture.readable)
                ++unreadable_textures;
            if (texture.frame_sizes_vary)
                varying_textures.insert(texture.name);
        }
        for (const MeshPrimitive& run : mesh.primitives) {
            if (run.corner_count != 4)
                continue;
            ++quads;
            if (mesh.indices[run.first_index] == run.first_vertex + 1)
                ++quads_from_second_corner;
        }
        // Every colour corner carries its palette entry, and every corner
        // names a vertex of its piece.
        for (std::size_t v = 0; v < mesh.vertices.size(); ++v) {
            const MeshVertex& vertex = mesh.vertices[v];
            if ((vertex.flags & primitive_flag_textured) == 0) {
                const oa::PaletteEntry& entry = palette.entries[vertex.palette_index];
                OA_CHECK(
                    vertex.red == entry.r && vertex.green == entry.g && vertex.blue == entry.b
                );
            }
            OA_CHECK(vertex.alpha == 255);
            OA_CHECK(vertex.piece < mesh.pieces.size());
            OA_CHECK(
                mesh.source_vertex[v] <
                model->objects[mesh.pieces[vertex.piece].object_index].vertices.size()
            );
        }
        // Corners whose row the matrix's single floor would move, at rest.
        for (const PieceState& state : instance.pieces()) {
            const MeshPiece& piece = mesh.pieces[state.object_index];
            for (uint32_t v = piece.first_vertex; v < piece.first_vertex + piece.vertex_count;
                 ++v) {
                const FixedVector3& point = state.transformed_vertices[mesh.source_vertex[v]];
                const PixelPoint pixel = pixel_of_model_point(point, 0);
                const ProjectedPoint single =
                    project_model_point(world(point.x), world(point.y), world(point.z));
                ++corners;
                if (static_cast<int32_t>(std::floor(single.y)) != pixel.y)
                    ++one_row_corners;
            }
        }
        for (PoseTotals& pose : poses)
            compare_installed_model(pose, name, mesh, instance, prepared, display);
    }
    oa::present::bind_display(nullptr);
    std::printf(
        "models: %zu loaded, %zu missing or unreadable, %zu refused by the model runtime, %zu "
        "refused by the mesh builder\n",
        loaded,
        missing,
        refused_instances,
        refused_meshes
    );
    std::printf(
        "meshes: %zu corners, %zu triangles, %zu bytes in all, largest %s with %zu bytes; %zu "
        "inexact coordinates; %zu textured primitives of other than four corners, %zu invisible, "
        "%zu short; %zu textures today's samplers cannot read; %zu textures whose frames vary "
        "in size; %zu of %zu quads split from their second corner\n",
        total_vertices,
        total_triangles,
        total_bytes,
        largest.c_str(),
        largest_bytes,
        inexact,
        dropped_textured,
        dropped_invisible,
        dropped_short,
        unreadable_textures,
        varying_textures.size(),
        quads_from_second_corner,
        quads
    );
    std::printf(
        "projection: %zu of %zu corners would move up a row under a single floor of screen y\n",
        one_row_corners,
        corners
    );
    for (const PoseTotals& pose : poses)
        check_pose_totals(pose);
    OA_CHECK(refused_meshes == 0);
    OA_CHECK(inexact == 0);
    OA_CHECK(total_bytes <= tolerated_total_bytes);
    OA_CHECK(largest_bytes <= tolerated_model_bytes);
}

} // namespace

int main(int argc, char** argv) {
    if (oa::test::game_data_requested(argc, argv)) {
        oa::AssetStore assets = oa::test::require_game_assets("the installed unit models");
        test_installed_unit_models(assets);
    } else {
        test_projection();
        test_square_mesh();
        test_textured_mesh();
        test_hierarchy_and_poses();
        test_quad_diagonal();
        test_refuses_malformed();
        test_memory();
    }
    return oa::test::check_exit_status();
}
