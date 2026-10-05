// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "oa/base/bytes.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace oa::formats::objects3d {

// 3DO positions are signed 16.16 values. Keeping the stored integer avoids
// losing the exact coordinates used by the simulation and piece transforms.
inline constexpr int32_t kThreeDoUnitsPerWorldUnit = 65'536;
inline constexpr uint32_t kNoObject = 0xffff'ffffU;

struct FixedVector3 {
    int32_t x{};
    int32_t y{};
    int32_t z{};
};

struct Primitive {
    int32_t color_index{};
    std::vector<uint16_t> vertex_indices;
    std::string texture_name;
    // The two words after the texture name offset, kept as read and not
    // interpreted; shipped models hold zero or varied values in them.
    int32_t word_after_texture_name{};
    int32_t word_before_is_colored{};
    int32_t is_colored{};

    /// Returns whether the model file marks the primitive visible.
    ///
    /// Textured primitives and explicitly colored primitives are visible. The
    /// game's later texture lookup and fallback decisions are not included.
    ///
    /// @return true for a textured or colored primitive
    [[nodiscard]] bool source_visible() const noexcept {
        return !texture_name.empty() || is_colored != 0;
    }
};

struct Object {
    int32_t version_signature{};
    // Preserved verbatim. The game only gives this field special meaning
    // where usable; shipped child pieces include both -1 and stale values.
    int32_t selection_primitive{-1};
    FixedVector3 offset_from_parent{};
    std::string name;
    std::vector<FixedVector3> vertices;
    std::vector<Primitive> primitives;
    uint32_t parent{kNoObject};
    uint32_t first_child{kNoObject};
    uint32_t next_sibling{kNoObject};
};

struct Model {
    // Preorder, with the root at index zero. Indices remain stable for COB piece
    // binding and renderer-owned per-piece transforms.
    std::vector<Object> objects;
};

/// Parses a 3DO model file into its object tree.
///
/// Decoding is little-endian with bounded offsets and counts; the two
/// primitive words after the texture name offset are kept as read and not
/// interpreted. A model holds at most 65,536 objects, and its links are
/// followed without recursion, so neither a long sibling list nor a deep
/// child chain can exhaust the stack. Every primitive copies its own vertex
/// indices and texture name, even where records share them, so a model
/// copies at most 1,048,576 vertex indices and 1 MiB of names in all.
///
/// @param bytes the whole file
/// @return the objects in preorder, root first, with stored (not negated)
///         coordinates; or the first error at its file offset: a truncated
///         record, array or name, an out-of-range vertex index, a name over
///         4,095 bytes, more than 65,536 objects, more vertex indices or
///         name bytes than a model may copy, an object reached twice or a
///         cycle
[[nodiscard]] base::bytes::Decoded<Model> load_3do(std::span<const std::byte> bytes);

/// Returns a model's height, the value of UnitDef.model_height.
///
/// Walks sibling and child links from the root, taking vertex Y plus piece Y
/// offsets. Its high 16 bits, which some code reads alone, are part of the
/// same value, not a separate property. A model load_3do returned always
/// succeeds.
///
/// @param model loaded model
/// @return the highest Y in 16.16 fixed point, never below zero; or, with
///         the object index as its offset, an out-of-range link or a cycle
/// @quirk Each child level clamps at zero and sums with signed 32-bit wrap,
///        as 3.1c does.
[[nodiscard]] base::bytes::Decoded<int32_t> maximum_height_fixed(const Model& model);

// UNITINFO collision/visibility bounds are a combination of FBI footprint and
// loaded-model height; 3.1c does not use 3DO X/Z vertex extrema here.
struct UnitTypeBounds {
    int32_t bounds_min_x{};
    int32_t bounds_min_y{};
    int32_t bounds_min_z{};
    int32_t bounds_max_x{};
    int32_t model_height{};
    int32_t bounds_max_z{};
    int32_t size_x{};
    int32_t size_y{};
    int32_t size_z{};
};

/// Derives the UNITINFO bounds a unit type is given at load.
///
/// X and Z span the footprint at 0x100000 fixed units per footprint cell,
/// centred on the unit; Y runs from zero to maximum_height_fixed. Targeting
/// visibility samples exactly this box.
///
/// @param model the unit's loaded model
/// @param footprint_x FBI footprintx, in footprint cells
/// @param footprint_z FBI footprintz, in footprint cells
/// @return minima, maxima and extents in 16.16 fixed point, with 32-bit wrap;
///         or maximum_height_fixed's error
[[nodiscard]] base::bytes::Decoded<UnitTypeBounds>
derive_unit_type_bounds(const Model& model, int16_t footprint_x, int16_t footprint_z);

struct ObjectBounds {
    FixedVector3 minimum{};
    FixedVector3 maximum{};
};

/// Returns the box spanned by one object's own vertices plus its offset.
///
/// Coordinates are in the loaded orientation (X and Z negated). The selection
/// box and the pointer pick both stop at the object, so its children and
/// siblings are not visited.
///
/// @param object model object measured
/// @return the box in 16.16 fixed point; every bound starts at zero, and an
///         object of two vertices or fewer leaves it there
[[nodiscard]] ObjectBounds object_bounds(const Object& object) noexcept;

// Bitmap measured before a unit's shadow is drawn.
// Width, height and origins are full int32 values; the game keeps the low 16
// bits of each as the shadow bitmap's size and origin.
struct ShadowBitmapExtent {
    int32_t width{};
    int32_t height{};
    int32_t origin_x{};
    int32_t origin_y{};
};

/// Measures the bitmap a unit's shadow is drawn into.
///
/// Walks the flat piece array from last to first. A piece contributes only
/// when its visible bit is set, which the game does exactly when the object
/// has at least three vertices. The measured points are the initial local
/// vertices with X and Z negated: piece offsets are not added and later
/// script overwrites of the point list are not seen. Per vertex,
/// yq = (int16)(Y >> 16) >> 2, sx = (int16)(X >> 16) + yq and
/// sy = (int16)((-Z) >> 16) - yq. Extents start at 0, so the projected origin
/// stays inside the range.
///
/// @param model loaded model
/// @return size max - (min - 2) + 2 and origin -(min - 2) on each axis, in pixels
[[nodiscard]] ShadowBitmapExtent shadow_bitmap_extent(const Model& model);

struct RenderPrimitive {
    uint32_t object_index{};
    uint32_t primitive_index{};
    int32_t color_index{};
    int32_t is_colored{};
    std::string_view texture_name;
    std::vector<FixedVector3> vertices;
    bool is_selection_primitive{};
};

/// Flattens a model into world-space primitives for a renderer.
///
/// A convenience adapter, not the game's renderer: it accumulates piece
/// offsets and negates X and Z, as loading a model does in 3.1c. It
/// keeps primitive order and reports selection geometry instead of dropping
/// it.
///
/// @param model loaded model
/// @return every primitive with its vertices in model space, 16.16 fixed
///         point; or, with the object index as its offset, an invalid parent
///         link or vertex index, or a coordinate that overflows int32
[[nodiscard]] base::bytes::Decoded<std::vector<RenderPrimitive>>
flatten_for_render(const Model& model);

} // namespace oa::formats::objects3d
