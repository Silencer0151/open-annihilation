// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "oa/formats/hpi.hpp"
#include "oa/formats/tnt.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace oa::present::world_renderer {

// Pixel-space terrain viewport of the terrain view. source_x/source_y are the
// camera's map-pixel position (Game.camera_x, Game.camera_y); width/height
// are the destination viewport dimensions (Game.viewport_width,
// Game.viewport_height).
struct Viewport {
    uint32_t source_x = 0;
    uint32_t source_y = 0;
    uint32_t width = 0;
    uint32_t height = 0;
};

// Battlefield clip and camera state. The game initializes the destination at
// (128,32) and draws the TNT pixels there without an additional map
// projection.
struct BattlefieldViewport {
    uint32_t source_x = 0;
    uint32_t source_y = 0;
    int32_t destination_x = 128;
    int32_t destination_y = 32;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t surface_width = 0;
    uint32_t surface_height = 0;
    float scale = 1.0F; // screen pixels per map pixel; 1 is the game's unscaled view
};

struct ScreenPoint {
    int32_t x = 0;
    int32_t y = 0;
};

struct MapPixel {
    uint32_t x = 0;
    uint32_t y = 0;
};

/// Returns the game's battlefield rectangle on a surface.
///
/// The rectangle starts at (128,32), after the 128-pixel command panel and the
/// 32-pixel top bar, and leaves a 32-pixel bottom strip: 512x416 on a 640x480
/// surface.
///
/// @param source_x camera map-pixel X
/// @param source_y camera map-pixel Y
/// @param surface_width presentation surface width in pixels
/// @param surface_height presentation surface height in pixels
/// @return the viewport, or nullopt for a surface too small to hold the panels
[[nodiscard]] std::optional<BattlefieldViewport> game_battlefield_viewport(
    uint32_t source_x, uint32_t source_y, uint32_t surface_width, uint32_t surface_height
) noexcept;

struct Surface {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> rgb;
};

enum class ErrorCode {
    none,
    invalid_map_model,
    viewport_out_of_bounds,
    output_limit,
};

struct Error {
    ErrorCode code = ErrorCode::none;
    std::string message;
};

struct RenderResult {
    std::optional<Surface> surface;
    std::optional<Error> error;

    /// Returns whether the result holds a surface.
    [[nodiscard]] bool ok() const noexcept { return surface.has_value(); }
};

/// Renders a camera crop of the TNT tile mosaic into RGB.
///
/// Gives the pixels the game's terrain view shows, partial edge tiles
/// included, without depending on SDL or a host graphics API.
///
/// @param map parsed TNT
/// @param game_palette the game palette
/// @param viewport crop in map pixels
/// @return the RGB surface, or an invalid-map, out-of-bounds or output-limit (64 Mi-pixel) error
[[nodiscard]] RenderResult render_viewport(
    const formats::tnt::Map& map, const PaletteBytes& game_palette, const Viewport& viewport
);

/// Renders a destination-sized, scaled sample of the TNT mosaic into RGB.
///
/// Destination pixel (x,y) reads map (source_x + x/scale, source_y + y/scale);
/// cost is dest_width*dest_height regardless of zoom, unlike render_viewport of
/// the visible map crop. Map pixels past the mosaic are black.
///
/// @param map parsed TNT
/// @param game_palette the game palette
/// @param source_x map-pixel X of the top-left sample
/// @param source_y map-pixel Y of the top-left sample
/// @param dest_width output width in pixels
/// @param dest_height output height in pixels
/// @param scale screen pixels per map pixel; non-positive means 1
/// @return the RGB surface, or an invalid-map or output-limit error
[[nodiscard]] RenderResult render_scaled_viewport(
    const formats::tnt::Map& map,
    const PaletteBytes& game_palette,
    uint32_t source_x,
    uint32_t source_y,
    uint32_t dest_width,
    uint32_t dest_height,
    float scale
);

/// Writes a scaled sample of the TNT mosaic into caller storage without allocating.
///
/// Samples as render_scaled_viewport does.
///
/// @param map parsed TNT
/// @param game_palette the game palette
/// @param source_x map-pixel X of the top-left sample
/// @param source_y map-pixel Y of the top-left sample
/// @param dest_width output width in pixels
/// @param dest_height output height in pixels
/// @param scale screen pixels per map pixel; non-positive means 1
/// @param[out] dest_rgb RGB rows of dest_stride_pixels pixels
/// @param dest_stride_pixels destination row stride in pixels, at least dest_width
/// @return an error for a missing destination, a malformed map or a missing tile; nullopt on success
[[nodiscard]] std::optional<Error> fill_scaled_viewport(
    const formats::tnt::Map& map,
    const PaletteBytes& game_palette,
    uint32_t source_x,
    uint32_t source_y,
    uint32_t dest_width,
    uint32_t dest_height,
    float scale,
    uint8_t* dest_rgb,
    uint32_t dest_stride_pixels
);

/// Renders the battlefield crop into a full presentation surface.
///
/// The crop lands at the same destination origin unit projection and
/// screen/map conversion use; the rest of the surface is black.
///
/// @param map parsed TNT
/// @param palette the game palette
/// @param viewport battlefield rectangle, camera and surface size
/// @return the RGB surface, or an out-of-bounds, output-limit or render_viewport error
[[nodiscard]] RenderResult render_battlefield_viewport(
    const formats::tnt::Map& map, const PaletteBytes& palette, const BattlefieldViewport& viewport
);

/// Converts a map pixel to a screen point through the battlefield viewport.
///
/// @param viewport battlefield rectangle, camera and scale
/// @param map_pixel map-pixel position
/// @return the screen point, rounded and clamped to int32
[[nodiscard]] ScreenPoint
map_pixel_to_screen(const BattlefieldViewport& viewport, MapPixel map_pixel) noexcept;
/// Converts a screen point inside the battlefield rectangle to a map pixel.
///
/// @param viewport battlefield rectangle, camera and scale
/// @param screen screen point
/// @return the map pixel, or nullopt outside the battlefield rectangle or past 32 bits
[[nodiscard]] std::optional<MapPixel>
screen_to_map_pixel(const BattlefieldViewport& viewport, ScreenPoint screen) noexcept;

/// Returns how many screen pixels a run of map pixels spans at the viewport's scale.
///
/// Something drawn a fixed number of battlefield pixels wide, such as the
/// square of a pixel particle, spans this many screen pixels in a zoomed view.
///
/// @param viewport battlefield viewport giving the scale; a zero scale is 1
/// @param map_pixels run length in map pixels
/// @return the run times the scale, rounded to the nearest pixel and at least 1
[[nodiscard]] int32_t screen_span(const BattlefieldViewport& viewport, int32_t map_pixels) noexcept;

// Radar sharing between the viewing player and a unit's owner (Unit.owner).
// alliance_byte is the owner's Player.alliance entry for the viewer's
// Player.index (written when an alliance changes). share_flags is the owner's
// PlayerSetupInfo.role, reached through Player.info. Bit 0x40 is ShareRadar,
// toggled by the share-radar setting; the other bits are not this test's.
// Zero alliance or a clear ShareRadar bit hides the contact.
struct RadarShare {
    uint8_t alliance_byte = 0;
    uint8_t share_flags = 0;
};

/// Tests whether an allied player's radar contact is shared with the viewer.
///
/// @param share the owner's alliance byte for the viewer and share flags
/// @return true when the alliance byte is nonzero and ShareRadar (0x40) is set
[[nodiscard]] bool shared_radar_contact(const RadarShare& share) noexcept;

} // namespace oa::present::world_renderer
