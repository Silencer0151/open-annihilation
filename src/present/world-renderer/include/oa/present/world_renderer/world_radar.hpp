// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// Radar (minimap): the terrain picture, its explored-area remap, the final
// image with contact blips and range rings, and its blit with the camera
// rectangle. Surfaces are owned by the caller through RadarSurfaceHost; the
// Game block's surface references are left untouched.

#include "oa/core/world.h"
#include "oa/present/display.hpp"
#include "oa/present/gaf_sprites.hpp"
#include "oa/present/surface.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace oa::present::world_renderer {

inline constexpr int16_t radar_blink_reload = 7;
inline constexpr char radar_final_name[] = "radar composed image";
inline constexpr char radar_mapped_name[] = "radar mapped layer";
inline constexpr char radar_picture_name[] = "radar map picture";  // ? surface name
inline constexpr char radar_temp_name[] = "radar picture scratch"; // ? surface name
// Edge of the square the radar picture is aspect-fitted into.
inline constexpr int32_t radar_picture_size = 0x7e;
inline constexpr int32_t radar_tile_pixels = 0x20;

// Game.ui_colors slots the radar passes draw with.
inline constexpr int32_t ui_color_unexplored = 0;
inline constexpr int32_t ui_color_sensor_range = 10; // radar and sonar rings
inline constexpr int32_t ui_color_jammer_range = 12; // radar and sonar jammer rings
inline constexpr int32_t ui_color_radar_marks = 14;  // shot dots and the view rectangle
inline constexpr int32_t ui_color_interceptor_range = 15;

// Game.console_flags bit that shows every unit on the radar.
inline constexpr uint16_t console_flag_full_radar = OA_CONSOLE_FLAG_FULL_RADAR;
// Game.visibility_flags bits under which only contacts are shown.
inline constexpr uint8_t visibility_flags_radar_limited = 0x03;
// Unit.flags bits that make another player's unit a radar contact.
inline constexpr uint32_t radar_contact_flags =
    OA_UNIT_FLAG_RADAR_CONTACT | OA_UNIT_FLAG_VIEWPOINT_OWNED;
// Projectiles of these weapons show their owner's icon instead of a dot.
inline constexpr uint32_t radar_icon_weapon_flags =
    OA_WEAPON_FLAG_TARGETABLE | OA_WEAPON_FLAG_INTERCEPTOR;
// Interceptor rings are drawn this many map pixels inside the coverage.
inline constexpr int32_t interceptor_ring_inset = 0x200;
inline constexpr int32_t interceptor_ring_segments = 0x20;

// Map data the radar picture samples. `minimap` is the TNT minimap picture
// (Game.minimap); when it is null the picture is rebuilt from the tile map.
struct RadarPictureSource {
    const uint16_t* tile_map = nullptr;   // Game.tile_map, stride map_width / 2
    int32_t tile_count = 0;               // tiles in the map's tile set
    const uint8_t* tile_pixels = nullptr; // the tile set's pixels, 32x32 bytes per tile
    const uint8_t* minimap = nullptr;
    int32_t minimap_stride = 0; // minimap picture width in bytes
    // Supplies the pair-mix (alpha) table the halving pass reads.
    const ::oa::present::DisplayContext* display = nullptr;
};

struct RadarSurfaces {
    ::oa::Surface* final_image = nullptr; // Game.radar_final_surface
    ::oa::Surface* mapped = nullptr;      // Game.radar_mapped_surface
    ::oa::Surface* picture = nullptr; // Game.radar_picture_surface, built by radar_build_picture
};

struct RadarSurfaceHost {
    void* user = nullptr;
    /// Builds `surfaces.picture` from the map.
    ///
    /// @param user RadarSurfaceHost::user
    /// @param[in,out] surfaces radar surfaces; `picture` is set
    void (*build_picture)(void* user, RadarSurfaces& surfaces) = nullptr;
    /// Creates a named off-screen surface.
    ///
    /// @param user RadarSurfaceHost::user
    /// @param name surface name, such as radar_final_name
    /// @param width width in pixels
    /// @param height height in pixels
    /// @return the surface, or null on failure
    ::oa::Surface* (*create_surface)(void* user, const char* name, int32_t width, int32_t height) =
        nullptr;
    /// Frees a surface create_surface made.
    ///
    /// @param user RadarSurfaceHost::user
    /// @param surface surface to free; may be null
    void (*free_surface)(void* user, ::oa::Surface* surface) = nullptr;
};

/// Builds the radar picture, creates the final and mapped surfaces and resets the picture rectangle, blink clock and dirty bit.
///
/// @param[in,out] game game block holding the radar fields
/// @param[in,out] surfaces radar surfaces to fill
/// @param host surface builder and allocator
void radar_init_surfaces(
    Game& game, RadarSurfaces& surfaces, const RadarSurfaceHost& host
) noexcept;

/// Builds the radar terrain picture.
///
/// Aspect-fits the map into the radar square, stores the centring offsets and
/// picture size in the Game block, then builds `surfaces.picture` by halving the
/// minimap, or a temporary full-size tile mosaic when the map carries none,
/// through the display's pair-mix table.
///
/// @param[in,out] game game block receiving the radar size and offsets
/// @param[in,out] surfaces radar surfaces; `picture` is set
/// @param source map picture data and pair-mix table
/// @param host surface allocator
void radar_build_picture(
    Game& game,
    RadarSurfaces& surfaces,
    const RadarPictureSource& source,
    const RadarSurfaceHost& host
) noexcept;

/// Frees the picture, mapped and final surfaces.
///
/// @param[in,out] surfaces radar surfaces; each is cleared
/// @param host surface allocator
void radar_free_surfaces(RadarSurfaces& surfaces, const RadarSurfaceHost& host) noexcept;

// Resolved per-viewer inputs of the mapped-radar pass.
struct RadarMapInputs {
    const uint16_t* sight_grid = nullptr; // Game.sight_grid
    const uint8_t* coverage = nullptr;    // viewer Player.coverage_grid
    const uint8_t* gray_table = nullptr;  // DisplayContext gray table
};

/// Rebuilds the mapped radar image while the dirty bit is set.
///
/// Unexplored cells take the unexplored colour; explored cells outside current
/// coverage are grayed.
///
/// @param[in,out] game game block holding the dirty bit and radar size
/// @param surfaces radar surfaces; `mapped` is written
/// @param inputs the viewer's sight bits, coverage and gray table
/// @quirk Rows are written at width stride, as 3.1c does.
void radar_fill_mapped(
    Game& game, const RadarSurfaces& surfaces, const RadarMapInputs& inputs
) noexcept;

// The FX.GAF sequences the radar draws. A missing sequence or frame draws
// nothing.
struct RadarSprites {
    const ::oa::present::GafSequence* unit_logos = nullptr;  // "radlogo", a frame per player colour
    const ::oa::present::GafSequence* cursor_logo = nullptr; // "radlogohigh"
    const ::oa::present::GafSequence* weapon_logos =
        nullptr; // "nuclogo", a frame per player colour
};

struct RadarContactHost {
    void* user = nullptr;
    /// Tests whether a map point is in the viewpoint player's sight.
    ///
    /// @param user RadarContactHost::user
    /// @param position signed 16.16 world position
    /// @return true when the point is seen
    bool (*point_visible)(void* user, const FixedVec3& position) = nullptr;
};

/// Composes the final radar image.
///
/// Copies the mapped image into the final image, then draws every shown unit's
/// blip (blinking while its damage countdown runs), the marker over the unit
/// under the cursor, the radar, sonar, jammer and interceptor rings of selected
/// units and each projectile as a dot or its owner's icon. Shown units are
/// listed in `hot_units` with their screen position and counted in
/// Game.hot_radar_unit_count; units past the end of `hot_units` are drawn but
/// neither listed nor counted. The redraw bit is set last.
///
/// @param[in,out] world world holding the game block, units and projectiles
/// @param surfaces radar surfaces; `final_image` is written
/// @param sprites radar blip and icon sequences
/// @param host sight test for contacts
/// @param[out] hot_units shown units and their radar positions
void radar_compose_final(
    World& world,
    const RadarSurfaces& surfaces,
    const RadarSprites& sprites,
    const RadarContactHost& host,
    std::span<RadarHotUnit> hot_units
) noexcept;

/// Blits the final image and outlines the camera rectangle while the redraw bit is set, then clears the bit.
///
/// @param[in,out] game game block holding the redraw bit, picture offset and view rectangle
/// @param surfaces radar surfaces
/// @param[in,out] target surface to draw on
void radar_draw(Game& game, const RadarSurfaces& surfaces, ::oa::Surface& target) noexcept;

/// Advances the radar blink clock one step.
///
/// A positive countdown decrements; otherwise it reloads and the blink bit
/// toggles.
///
/// @param[in,out] game game block holding the blink clock and flags
void radar_step_blink(Game& game) noexcept;

} // namespace oa::present::world_renderer
