// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// 3DO models as meshes a graphics card can draw: for every piece of a model,
// the triangles of its primitives in today's draw order, each corner with
// its position in piece space, the 3DO vertex it is, its colour, its texture
// coordinates and the texture they address, and the normal the building
// shade rows are taken from; the piece hierarchy; and the battlefield
// projection written as a matrix. Nothing here draws: the meshes are
// groundwork for a battlefield the card draws, and the processor keeps
// drawing today's picture.
//
// What the card is given per frame stays the processor's: the pieces of a
// unit in their draw order with their visibility and their shaded flag, the
// vertices the model runtime has transformed for them
// (PieceState::transformed_vertices, which a corner names by its 3DO vertex
// index), the texture frame of each animated texture, the owner's team
// colour, the light, and whether the unit draws with a depth plane. The
// sprite pages map a texture's frames to rectangles of an atlas; a mesh
// names each texture by its sequence, as the texture library holds it.

#include "oa/formats/objects3d.hpp"
#include "oa/present/model/model_library.hpp"
#include "oa/present/surface.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace oa::present::gpu_world {

/// One world unit, one map pixel at zoom 1, in the 16.16 fixed point of a
/// model's coordinates.
inline constexpr float fixed_units_per_world_unit = 65536.0F;
/// Marks a primitive that addresses no texture, and a piece with no parent.
inline constexpr uint32_t no_texture = 0xffff'ffffU;
inline constexpr uint32_t no_piece = 0xffff'ffffU;

// Bounds a build keeps; a model past one is refused.
inline constexpr std::size_t max_object_vertices = 65536; ///< a 3DO index is 16 bits
inline constexpr std::size_t max_object_primitives = 65536;
inline constexpr std::size_t max_primitive_corners = 1024;
inline constexpr std::size_t max_mesh_vertices = 1U << 22U;
inline constexpr std::size_t max_mesh_pieces = 65536; ///< MeshVertex::piece is 16 bits

// Bits of MeshVertex::flags and MeshPrimitive::flags.
inline constexpr uint8_t primitive_flag_textured = 0x1; ///< colour comes from the texture
inline constexpr uint8_t primitive_flag_animated = 0x2; ///< the frame is the running one
inline constexpr uint8_t primitive_flag_team = 0x4;     ///< the frame is the owner's team colour

/// The battlefield projection of a model point (x, y, z), in world units of
/// the loaded orientation, to the screen: row-major 3x4, applied to
/// (x, y, z, 1). The rows give screen x, screen y and the depth:
///
///     screen x = x
///     screen y = -z - y / 2
///     depth    = y
///
/// Today's raster takes whole pixels of each term on its own, which
/// pixel_of_model_point gives: floor(x), floor(-z) - floor(y) / 2 and
/// floor(y) plus the draw's depth base (model_depth_base, or with the digger
/// bias). A card that floors the matrix's screen y as one value places a
/// vertex one row higher than today's wherever the fraction of -z is below
/// the fraction of y / 2; flooring -z and y / 2 apart gives today's rows.
inline constexpr std::array<float, 12> model_projection = {
    1.0F,
    0.0F,
    0.0F,
    0.0F, //
    0.0F,
    -0.5F,
    -1.0F,
    0.0F, //
    0.0F,
    1.0F,
    0.0F,
    0.0F
};

/// A point the projection matrix gives: screen x, screen y and depth, in
/// pixels, before any flooring.
struct ProjectedPoint {
    float x{};
    float y{};
    float depth{};
};

/// A projected point placed as today's raster places it: whole pixels, and
/// the depth as the byte the depth plane holds before its wrap.
struct PixelPoint {
    int32_t x{};
    int32_t y{};
    int32_t depth{};
};

/// Applies model_projection to a model point.
///
/// @param x model x in world units
/// @param y model y in world units (the height)
/// @param z model z in world units
/// @return screen x, screen y and depth, in pixels
[[nodiscard]] constexpr ProjectedPoint project_model_point(float x, float y, float z) noexcept {
    return {
        model_projection[0] * x + model_projection[1] * y + model_projection[2] * z +
            model_projection[3],
        model_projection[4] * x + model_projection[5] * y + model_projection[6] * z +
            model_projection[7],
        model_projection[8] * x + model_projection[9] * y + model_projection[10] * z +
            model_projection[11]
    };
}

/// Places a model point as today's raster does at zoom 1.
///
/// Each term is floored on its own: x to floor(x), y to
/// floor(-z) - floor(y) / 2 (the division rounding toward negative
/// infinity), and the depth to `depth_base` plus floor(y) kept to 16 bits.
///
/// @param x model x in world units
/// @param y model y in world units (the height)
/// @param z model z in world units
/// @param depth_base depth of a point at height 0 (model_depth_base, plus
///     digger_depth_bias for a digger)
/// @return the pixel and depth
[[nodiscard]] PixelPoint
pixel_of_model_point(float x, float y, float z, int32_t depth_base) noexcept;

/// Places a transformed vertex of a model instance as today's raster does
/// at zoom 1: the same pixel and depth as the float form, taken from the
/// 16.16 value directly, so that no conversion rounds.
///
/// A builder that draws a unit each frame places each corner from
/// PieceState::transformed_vertices at the corner's ModelMesh::source_vertex
/// through this function, which is where today's images put it.
///
/// @param point model point in signed 16.16, as rebuild_transforms leaves it
/// @param depth_base depth of a point at height 0 (model_depth_base, plus
///     digger_depth_bias for a digger)
/// @return the pixel and depth
[[nodiscard]] PixelPoint
pixel_of_model_point(const formats::objects3d::FixedVector3& point, int32_t depth_base) noexcept;

/// One corner of a triangle: 40 bytes, every field at its natural alignment.
struct MeshVertex {
    float x{}; ///< piece space, world units, the loaded orientation (x and z negated)
    float y{};
    float z{};
    /// The vertex normal the building shade rows are taken from, at rest:
    /// vertex_normals over the piece's vertices in the loaded orientation.
    /// It is the normal today's builder takes for a piece the script has
    /// not turned; for a turned piece today's builder averages the normals
    /// of the transformed vertices, which vertex_normals gives over
    /// PieceState::transformed_vertices.
    float normal_x{};
    float normal_y{};
    float normal_z{};
    /// Where on the texture's frame the corner lies, 0 to 1 along each
    /// axis: the corners of a textured primitive map, in corner order, to
    /// (0, 0), (1, 0), (1, 1) and (0, 1), which are the texels (0, 0),
    /// (width - 1, 0), (width - 1, height - 1) and (0, height - 1) of the
    /// frame shown, whatever its size; today's raster interpolates the
    /// texel coordinates u * (width - 1) and v * (height - 1) and fetches
    /// the texel at their floor. Zero for a colour primitive.
    float u{};
    float v{};
    /// The colour: the palette entry of a colour primitive with the gamma
    /// applied, opaque; white for a textured primitive, whose texture gives
    /// the colour.
    uint8_t red{};
    uint8_t green{};
    uint8_t blue{};
    uint8_t alpha{};
    uint16_t piece{};        ///< index into ModelMesh::pieces
    uint8_t palette_index{}; ///< the palette entry of a colour primitive; 0 for a textured one
    uint8_t flags{};         ///< primitive_flag_* bits of the primitive
};

static_assert(sizeof(MeshVertex) == 40);
static_assert(alignof(MeshVertex) == 4);

/// How a texture's frame is chosen each frame.
enum class TextureKind : uint8_t {
    fixed,    ///< its first frame, always
    animated, ///< the frame the processor's animation cursor shows; a cached image shows frame 0
    team,     ///< the frame numbered by the owner's team colour, 0 to 9
};

/// One texture a mesh addresses: a sequence of the texture library.
///
/// The frame a primitive shows is `sequence->frames[index]`, whose GAF
/// frame is `sequence->sequence->frames[index]`, the index by `kind`: 0 for
/// a fixed texture, the running frame of the prepared primitive's cursor
/// (MeshPrimitive::prepared_index; primitive_texture gives the frame) for
/// an animated one, the owner's team colour for a team one. The pair of
/// the sequence and the index names a frame of the library for as long as
/// the library lives: a consumer that numbers frames for the sprite pages
/// numbers these pairs.
struct MeshTexture {
    std::string name; ///< the sequence name, case-folded, as the texture library keys it
    const model::TextureSequence*
        sequence{}; ///< the library's entry, which the mesh was built from
    uint16_t frame_count{};
    uint16_t width{};  ///< of the first frame, in texels
    uint16_t height{}; ///< of the first frame
    /// The frames are not all the first frame's size; a corner's texel then
    /// depends on the frame shown.
    bool frame_sizes_vary{};
    TextureKind kind{};
    /// The first frame is a raw, layer-free frame, as today's samplers
    /// read; a primitive of a texture that is not draws nothing today.
    bool readable{};
};

/// One run of triangles drawn alike: one primitive of one piece, in
/// ModelMesh::indices from first_index, index_count indices (a multiple of
/// three). The primitive's corners are corner_count consecutive vertices
/// from first_vertex, in the 3DO's corner order, so a consumer that walks
/// whole primitives, as today's raster does, can take them back.
///
/// The triangles are a fan from one corner. A primitive of other than four
/// corners fans from its first. A four-cornered primitive is split along
/// one of its diagonals: today's whole walk interpolates a quad's texels
/// along its edges and then along each row, which two triangles follow
/// exactly only where the quad projects to a parallelogram, so the build
/// places the quad at rest (its piece's offsets, no turn), evaluates that
/// walk at the midpoint of each diagonal, and splits along the diagonal
/// whose midpoint the walk interpolates nearer the mean of its two corners:
/// a fan from corner 0 (corners 0 and 2 joined) or from corner 1 (1 and 3
/// joined), corner 0 on a tie and where the quad cannot be walked at rest
/// (edge-on, or not convex). The split is chosen once; a turned pose may
/// favour the other.
struct MeshPrimitive {
    uint32_t first_index{};
    uint32_t index_count{};
    uint32_t first_vertex{};      ///< its corners in ModelMesh::vertices
    uint32_t texture{no_texture}; ///< index into ModelMesh::textures; no_texture for a colour
    uint32_t source_index{};      ///< index into the 3DO object's primitives
    uint32_t prepared_index{};    ///< index into the prepared object's primitives
    uint16_t corner_count{};
    uint8_t palette_index{}; ///< the palette entry of a colour primitive
    uint8_t flags{};         ///< primitive_flag_* bits
};

/// One piece of a model: a 3DO object with its place in the hierarchy. The
/// pieces are indexed like the model's objects, root first.
struct MeshPiece {
    uint32_t object_index{};
    uint32_t parent{no_piece}; ///< the parent piece; no_piece for the root
    /// The piece's origin from its parent's, in world units of the loaded
    /// orientation (x and z negated), before the script's translation.
    float offset_x{};
    float offset_y{};
    float offset_z{};
    uint32_t first_vertex{}; ///< its corners in ModelMesh::vertices
    uint32_t vertex_count{};
    uint32_t first_primitive{}; ///< its runs in ModelMesh::primitives, in draw order
    uint32_t primitive_count{};
};

/// A model as triangles, with its textures and piece hierarchy.
///
/// Draw order, as today's raster keeps it: the processor hands the pieces
/// of a unit last to first; within a piece the primitives in
/// `primitives` order (the selection primitive is left out); within a
/// primitive the triangles in order. A unit drawn with a depth plane writes
/// a pixel when the stored depth byte is not above the new one, so a later
/// primitive wins a tie; a unit drawn without one writes every pixel in
/// order.
struct ModelMesh {
    std::vector<MeshVertex> vertices;
    /// One per vertex: the corner's 3DO vertex index within its piece's
    /// object, which is its place in PieceState::transformed_vertices.
    std::vector<uint16_t> source_vertex;
    std::vector<uint32_t> indices; ///< three per triangle, into vertices
    std::vector<MeshPrimitive> primitives;
    std::vector<MeshPiece> pieces;
    std::vector<MeshTexture> textures;
    /// Coordinates, of corners and piece offsets, whose 16.16 value is not
    /// a float: those at or beyond 256 world units, which are rounded to
    /// the nearest float.
    uint32_t inexact_positions{};
    /// Textured primitives of other than four corners, which today's raster
    /// draws nothing for; they add no triangles.
    uint32_t dropped_textured_primitives{};
    /// Primitives that have neither a colour nor a texture, which draw
    /// nothing today; they add no triangles.
    uint32_t dropped_invisible_primitives{};
    /// Colour primitives of fewer than three corners, which fill nothing
    /// today; they add no triangles.
    uint32_t dropped_short_primitives{};
};

/// Why a model was refused.
struct MeshBuildError {
    const char* message{}; ///< null when the mesh was built
    uint32_t object{};     ///< the object concerned, or 0
    uint32_t primitive{};  ///< the object's primitive concerned, or 0
};

/// Builds a model's mesh from its loaded model and the primitives prepared
/// for drawing it.
///
/// The piece order, the primitive order and the texture of each primitive
/// are the prepared model's, so the mesh draws what today's raster draws:
/// a primitive whose texture is missing is a colour primitive of
/// missing_texture_color, and a textured primitive of other than four
/// corners adds nothing. Positions are the 3DO's 16.16 coordinates in world
/// units, negated in x and z as the game loads them. Colours are the
/// palette's entries with `gamma` applied as the display applies it (each
/// channel truncated and clamped at 255).
///
/// A model is refused, and `mesh` left empty, when it has no objects, when
/// its hierarchy links are out of range, out of order or do not meet, when
/// the prepared model was prepared for another model or its model has been
/// freed, when the prepared model does not match it, when a vertex index is
/// out of range, or when an object, a primitive or the mesh exceeds its
/// bound.
///
/// @param model loaded 3DO model
/// @param prepared the model's primitives prepared for drawing (prepare_model)
/// @param textures the texture library the primitives were prepared from
/// @param palette game palette
/// @param gamma display gamma; 1 applies the palette unchanged
/// @param[out] mesh the mesh; emptied first
/// @return a null message, or why the model was refused
MeshBuildError build_model_mesh(
    const formats::objects3d::Model& model,
    const model::PreparedModel& prepared,
    const model::TextureLibrary& textures,
    const Palette& palette,
    float gamma,
    ModelMesh& mesh
);

/// Returns the bytes a mesh holds in its arrays.
///
/// @param mesh the mesh
/// @return vertices, source vertices, indices, primitives, pieces and
///     texture names together
[[nodiscard]] std::size_t mesh_bytes(const ModelMesh& mesh) noexcept;

/// A vertex normal, as the building builder averages it.
struct VertexNormal {
    float x{};
    float y{};
    float z{};
};

/// Averages the unit normals of an object's drawn primitives into each of
/// its vertices, as the building builder does before it takes the shade
/// rows.
///
/// A primitive's normal is the unit cross product of the edges from its
/// second corner to its first and to its third, the products summed in
/// double precision and rounded to float; a primitive of fewer than three
/// corners, with a repeated corner among its first three, or with one past
/// `points` keeps the straight-up normal. Each vertex takes the mean, in
/// float, of the normals of the drawn primitives that name it, counted once
/// per naming, the selection primitive left out; a vertex no drawn primitive
/// names is zero. Over the vertices in the loaded orientation it gives
/// MeshVertex's normals; over a piece's transformed vertices it gives the
/// normals today's builder shades a turned piece with.
///
/// @param object 3DO object
/// @param prepared the object's primitives prepared for drawing
/// @param points the object's vertices, one per 3DO vertex, in signed 16.16
/// @param[out] normals one per point
void vertex_normals(
    const formats::objects3d::Object& object,
    const model::PreparedObject& prepared,
    std::span<const formats::objects3d::FixedVector3> points,
    std::vector<VertexNormal>& normals
);

/// Returns the shade row of a vertex, as the building builder takes it.
///
/// The dot product of the light and the normal is summed as z, y then x
/// products in double precision, scaled, truncated toward zero and masked
/// to the 32 rows, so a negative product wraps to the top rows.
///
/// @param normal_x vertex normal, as MeshVertex holds it
/// @param normal_y vertex normal
/// @param normal_z vertex normal
/// @param light light direction, x, y and z
/// @param light_scale scale of the dot product (default_light_scale)
/// @return the row, 0 to 31
[[nodiscard]] int32_t shade_row(
    float normal_x, float normal_y, float normal_z, const float light[3], float light_scale
) noexcept;

} // namespace oa::present::gpu_world
