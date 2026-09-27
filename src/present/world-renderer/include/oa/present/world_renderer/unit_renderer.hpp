// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "oa/sim/model_runtime/instance.hpp"
#include "oa/present/world_renderer.hpp"
#include "oa/formats/hpi.hpp"
#include "oa/formats/gaf.hpp"
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace oa::present::world_renderer {
namespace game_viewport {
inline constexpr int32_t origin_x = 128;
inline constexpr int32_t origin_y = 32;
inline constexpr std::size_t maximum_polygon_vertices = 22;
inline constexpr int32_t maximum_safe_origin = 1'000'000;
} // namespace game_viewport

struct RasterClip {
    int32_t x = 0;
    int32_t y = 0;
    uint32_t width = 0;
    uint32_t height = 0;
};

struct UnitProjection {
    formats::objects3d::FixedVector3 world_position{};
    int32_t camera_pixel_x{}, camera_pixel_y{};
    int32_t destination_origin_x = game_viewport::origin_x;
    int32_t destination_origin_y = game_viewport::origin_y;
    // Empty preserves the full-surface raster scope used by standalone tools.
    std::optional<RasterClip> raster_clip;
    float scale = 1.0F;

    UnitProjection() = default;

    /// Builds a projection for one unit.
    ///
    /// @param position signed 16.16 world position of the unit
    /// @param camera_x camera map-pixel X
    /// @param camera_y camera map-pixel Y
    /// @param origin_x screen X of the battlefield origin
    /// @param origin_y screen Y of the battlefield origin
    /// @param clip raster clip, or nullopt for the whole surface
    /// @param projection_scale screen pixels per map pixel
    UnitProjection(
        formats::objects3d::FixedVector3 position,
        int32_t camera_x,
        int32_t camera_y,
        int32_t origin_x = game_viewport::origin_x,
        int32_t origin_y = game_viewport::origin_y,
        std::optional<RasterClip> clip = std::nullopt,
        float projection_scale = 1.0F
    )
        : world_position(position), camera_pixel_x(camera_x), camera_pixel_y(camera_y),
          destination_origin_x(origin_x), destination_origin_y(origin_y), raster_clip(clip),
          scale(projection_scale) {}
};

/// Builds a unit projection from the battlefield viewport.
///
/// The raster clip is the battlefield rectangle one pixel short on the right and
/// bottom, as the game's rasterizers treat the inclusive surface limits as
/// exclusive span ends.
///
/// @param viewport battlefield rectangle, camera and scale
/// @param world_position signed 16.16 world position of the unit
/// @return the projection
[[nodiscard]] UnitProjection unit_projection_for_viewport(
    const BattlefieldViewport& viewport, formats::objects3d::FixedVector3 world_position
) noexcept;

struct UnitRenderStats {
    std::size_t colored_primitives{}, textured_primitives{};
    std::size_t textured_primitives_deferred{}, hidden_pieces{};
};

struct UnitRenderResult {
    std::optional<UnitRenderStats> stats;
    std::optional<Error> error;

    /// Returns whether the result holds render statistics.
    [[nodiscard]] bool ok() const noexcept { return stats.has_value(); }
};

enum class MaterialFrameMode : uint8_t {
    fixed,
    animated,
    owner_team,
};

// Runtime inputs of the material frame choice. The cursor belongs to the
// resolved primitive material; it is not a clock from which the renderer
// invents a frame. owner_team_frame is the owner's team colour
// (PlayerSetupInfo.color).
struct MaterialFrameState {
    uint16_t cursor = 0;
    bool force_first_frame = false;
    std::optional<uint8_t> owner_team_frame;
};

struct PrimitiveMaterialCursor {
    std::size_t object_index = 0;
    std::size_t primitive_index = 0;
    uint16_t cursor = 0;
};

struct UnitMaterialState {
    bool force_first_frame = false;
    std::optional<uint8_t> owner_team_frame;
    // Unfinished units (Unit.build_remaining != 0) stroke primitives with the animated
    // 0xa0..0xaf nano palette instead of filled ColorIndex.
    bool wireframe = false;
    uint8_t wireframe_index = 0xa0;
    uint8_t wireframe_index_b = 0xa0;
    float build_remaining = 0.0F;
    std::optional<uint8_t> selection_outline;
    // Vertex depth is Y_hi + 0x32, plus 0x4b for a digger
    // (OA_UNIT_DEF_FLAG_DIGGER in UnitDef.flags). The rasterizer z-tests the
    // interpolated high byte.
    int32_t depth_extra = 0;
    // The model loader creates the cursor in each loaded object primitive.
    // Model data is shared by its instances, so callers keep and advance this
    // per-model state rather than deriving a frame from render time.
    std::vector<PrimitiveMaterialCursor> primitive_cursors;
    // Gouraud vertex shading applies when Shading (Game.graphics_flags bit
    // 0x20) is on and the unit is a building (OA_UNIT_FLAG_BUILDING in
    // Unit.flags); mobiles use flat projection.
    bool gouraud_shading = false;
    // Shadows (Game.graphics_flags bit 4) stamp a 3DO silhouette onto terrain.
    bool cast_shadow = false;
    // Ground Y as 16.16 from the terrain height sample, used instead of unit Y for placement.
    int32_t ground_height = 0;

    /// Returns the material cursor of one object primitive.
    ///
    /// @param object_index model object
    /// @param primitive_index primitive of that object
    /// @return the cursor's frame, 0 when none is kept, as a newly loaded primitive starts
    [[nodiscard]] uint16_t
    cursor_for(std::size_t object_index, std::size_t primitive_index) const noexcept;
};

struct TextureMaterial {
    MaterialFrameMode mode = MaterialFrameMode::fixed;
    // Unsupported GAF frames retain their slot as null so indices continue to
    // match the game's sequence table.
    std::vector<std::optional<formats::gaf::RenderedFrame>> frames;
};

struct TextureCatalog {
    std::unordered_map<std::string, TextureMaterial> materials;
};

/// Selects the texture frame a primitive draws with.
///
/// Team selection has priority, force-first affects animated materials only,
/// and fixed materials always use frame zero.
///
/// @param material the primitive's texture material
/// @param state cursor, force-first flag and owner team byte
/// @return the frame, or null for an unavailable or out-of-range slot or a missing team byte
[[nodiscard]] const formats::gaf::RenderedFrame*
select_material_frame(const TextureMaterial& material, const MaterialFrameState& state) noexcept;

/// Loads every textures/*.gaf sequence as a texture material, first name wins.
///
/// logos.gaf is left out of the normal archive list and consulted only as a
/// fallback, and its ten-frame sequences select the owner's team frame.
/// Sequences of two or more frames animate.
///
/// @param assets asset store to list and read textures/ through
/// @return the catalog
[[nodiscard]] TextureCatalog load_texture_catalog(AssetStore& assets);

/// Draws a transformed 3DO instance's solid and textured primitives.
///
/// Visible pieces are drawn in reverse order through the orthographic
/// projection; the instance must already have passed through
/// Instance::rebuild_transforms.
///
/// @param[in,out] destination RGB surface
/// @param instance the transformed instance
/// @param palette the game palette
/// @param projection unit position, camera, origin, clip and scale
/// @param textures texture materials, or null to count textured primitives as deferred
/// @param material_state cursors, team byte, wireframe, shading and shadow options
/// @return primitive counts, or an output-limit or malformed-model error
[[nodiscard]] UnitRenderResult render_colored_instance(
    Surface& destination,
    const sim::model_runtime::Instance& instance,
    const PaletteBytes& palette,
    const UnitProjection& projection,
    const TextureCatalog* textures = nullptr,
    const UnitMaterialState& material_state = {}
);

/// Starts a frame's shadow pass.
///
/// The shadow remap is applied once per destination pixel; call this at the
/// start of a frame before any cast_shadow draws so overlapping silhouettes do
/// not stack.
///
/// @param dest_width destination width in pixels
/// @param dest_height destination height in pixels
void begin_shadow_pass(uint32_t dest_width, uint32_t dest_height);

} // namespace oa::present::world_renderer
