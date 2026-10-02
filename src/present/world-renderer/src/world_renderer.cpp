// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/present/world_renderer.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

namespace oa::present::world_renderer {
namespace {

constexpr uint64_t maximum_output_pixels = 64ULL * 1024ULL * 1024ULL;

/// 16.16 fixed-point one: a step of one destination pixel.
constexpr uint32_t fixed_one = 65536U;

[[nodiscard]] RenderResult failure(ErrorCode code, std::string message) {
    return {std::nullopt, Error{code, std::move(message)}};
}

/// What fill_scaled_viewport samples and where it writes, shared by its bands.
struct ScaledFill {
    const formats::tnt::Map* map{};
    const uint8_t* lut{}; ///< three bytes, red, green and blue, for each palette index
    uint32_t source_x{};
    uint32_t source_y{};
    uint32_t dest_width{};
    uint32_t scale_fp{}; ///< 16.16 screen pixels per map pixel, at least 1
    uint64_t terrain_width{};
    uint64_t terrain_height{};
    uint8_t* dest_rgb{};
    uint32_t dest_stride_pixels{};
};

/// Fills destination rows [first_row, end_row) of a scaled terrain sample.
///
/// Row r samples map row source_y + floor(r * 65536 / scale_fp), the row the
/// fill reaches stepping 65536 a row from row 0.
///
/// @param fill the sample and its destination
/// @param first_row first destination row
/// @param end_row destination row after the last
/// @return false at the first missing tile, where the band stops
bool fill_scaled_rows(const ScaledFill& fill, uint32_t first_row, uint32_t end_row) noexcept {
    constexpr uint32_t tile_edge = formats::tnt::layout::tile_edge_pixels;
    const formats::tnt::Map& map = *fill.map;
    const uint16_t* const grid = map.tile_indices.data();
    const uint8_t* const tiles = map.tile_palette_indices.data();
    const uint32_t scale_fp = fill.scale_fp;
    const uint64_t advanced = static_cast<uint64_t>(first_row) * fixed_one;
    uint32_t map_y = fill.source_y + static_cast<uint32_t>(advanced / scale_fp);
    auto y_frac = static_cast<uint32_t>(advanced % scale_fp);
    for (uint32_t destination_y = first_row; destination_y < end_row; ++destination_y) {
        auto* out =
            fill.dest_rgb + static_cast<std::size_t>(destination_y) * fill.dest_stride_pixels * 3U;
        const auto within_tile_y = static_cast<std::size_t>(map_y % tile_edge);
        const auto row_tiles = static_cast<std::size_t>(map_y / tile_edge) * map.tile_width;
        if (map_y >= fill.terrain_height) {
            std::memset(out, 0, static_cast<std::size_t>(fill.dest_width) * 3U);
        } else if (scale_fp == fixed_one) {
            // 1:1 sampling copies whole tile rows through the palette table.
            uint64_t map_x = fill.source_x;
            uint32_t remaining = fill.dest_width;
            while (remaining != 0) {
                if (map_x >= fill.terrain_width) {
                    std::memset(out, 0, static_cast<std::size_t>(remaining) * 3U);
                    break;
                }
                const auto within_tile_x = static_cast<uint32_t>(map_x % tile_edge);
                const auto tile_index = grid[row_tiles + map_x / tile_edge];
                if (tile_index >= map.tile_count)
                    return false;
                const auto run = std::min(remaining, tile_edge - within_tile_x);
                const auto* source =
                    tiles +
                    static_cast<std::size_t>(tile_index) * formats::tnt::layout::tile_bytes +
                    within_tile_y * tile_edge + within_tile_x;
                for (uint32_t pixel = 0; pixel < run; ++pixel, out += 3)
                    std::memcpy(out, &fill.lut[static_cast<std::size_t>(source[pixel]) * 3U], 3U);
                map_x += run;
                remaining -= run;
            }
        } else {
            uint32_t map_x = fill.source_x;
            uint32_t x_frac = 0;
            const uint8_t* rgb = nullptr;
            static constexpr uint8_t black[3] = {0, 0, 0};
            for (uint32_t destination_x = 0; destination_x < fill.dest_width; ++destination_x) {
                if (rgb == nullptr) {
                    if (map_x >= fill.terrain_width) {
                        rgb = black;
                    } else {
                        const auto tile_index = grid[row_tiles + map_x / tile_edge];
                        if (tile_index >= map.tile_count)
                            return false;
                        const auto tile_pixel = static_cast<std::size_t>(tile_index) *
                                                    formats::tnt::layout::tile_bytes +
                                                within_tile_y * tile_edge + map_x % tile_edge;
                        rgb = &fill.lut[static_cast<std::size_t>(tiles[tile_pixel]) * 3U];
                    }
                }
                std::memcpy(out, rgb, 3U);
                out += 3;
                x_frac += fixed_one;
                while (x_frac >= scale_fp) {
                    x_frac -= scale_fp;
                    ++map_x;
                    rgb = nullptr;
                }
            }
        }
        y_frac += fixed_one;
        while (y_frac >= scale_fp) {
            y_frac -= scale_fp;
            ++map_y;
        }
    }
    return true;
}

} // namespace

RenderResult render_viewport(
    const formats::tnt::Map& map, const PaletteBytes& game_palette, const Viewport& viewport
) {
    const auto expected_tile_indices = static_cast<uint64_t>(map.tile_width) * map.tile_height;
    const auto expected_tile_bytes =
        static_cast<uint64_t>(map.tile_count) * formats::tnt::layout::tile_bytes;
    if (expected_tile_indices != map.tile_indices.size() ||
        expected_tile_bytes != map.tile_palette_indices.size()) {
        return failure(
            ErrorCode::invalid_map_model, "TNT arrays do not match the declared tile grid"
        );
    }

    const uint64_t terrain_width =
        static_cast<uint64_t>(map.tile_width) * formats::tnt::layout::tile_edge_pixels;
    const uint64_t terrain_height =
        static_cast<uint64_t>(map.tile_height) * formats::tnt::layout::tile_edge_pixels;
    const uint64_t crop_right = static_cast<uint64_t>(viewport.source_x) + viewport.width;
    const uint64_t crop_bottom = static_cast<uint64_t>(viewport.source_y) + viewport.height;
    if (crop_right > terrain_width || crop_bottom > terrain_height) {
        return failure(
            ErrorCode::viewport_out_of_bounds, "terrain viewport extends beyond the TNT tile mosaic"
        );
    }
    if (viewport.width == 0 || viewport.height == 0) {
        return {Surface{viewport.width, viewport.height, {}}, std::nullopt};
    }
    const uint64_t pixel_count = static_cast<uint64_t>(viewport.width) * viewport.height;
    if (pixel_count > maximum_output_pixels ||
        pixel_count > std::numeric_limits<std::size_t>::max() / 3U) {
        return failure(
            ErrorCode::output_limit, "terrain viewport exceeds the 64 Mi-pixel output limit"
        );
    }

    Surface output{
        viewport.width,
        viewport.height,
        std::vector<uint8_t>(static_cast<std::size_t>(pixel_count) * 3U)
    };
    constexpr uint32_t tile_edge = formats::tnt::layout::tile_edge_pixels;
    for (uint32_t destination_y = 0; destination_y < viewport.height; ++destination_y) {
        const uint64_t source_y = static_cast<uint64_t>(viewport.source_y) + destination_y;
        const auto tile_y = static_cast<std::size_t>(source_y / tile_edge);
        const auto within_tile_y = static_cast<std::size_t>(source_y % tile_edge);
        for (uint32_t destination_x = 0; destination_x < viewport.width; ++destination_x) {
            const uint64_t source_x = static_cast<uint64_t>(viewport.source_x) + destination_x;
            const auto tile_x = static_cast<std::size_t>(source_x / tile_edge);
            const auto within_tile_x = static_cast<std::size_t>(source_x % tile_edge);
            const auto tile_grid_offset =
                static_cast<std::size_t>(tile_y) * map.tile_width + tile_x;
            const auto tile_index = map.tile_indices[tile_grid_offset];
            if (tile_index >= map.tile_count) {
                return failure(
                    ErrorCode::invalid_map_model, "TNT tile grid references a missing tile"
                );
            }
            const auto tile_pixel =
                static_cast<std::size_t>(tile_index) * formats::tnt::layout::tile_bytes +
                within_tile_y * tile_edge + within_tile_x;
            const auto palette_index = map.tile_palette_indices[tile_pixel];
            const auto palette_offset =
                static_cast<std::size_t>(palette_index) * palette_entry_bytes;
            const auto output_offset =
                (static_cast<std::size_t>(destination_y) * viewport.width + destination_x) * 3U;
            output.rgb[output_offset] = game_palette[palette_offset];
            output.rgb[output_offset + 1] = game_palette[palette_offset + 1];
            output.rgb[output_offset + 2] = game_palette[palette_offset + 2];
        }
    }
    return {std::move(output), std::nullopt};
}

std::optional<Error> fill_scaled_viewport(
    const formats::tnt::Map& map,
    const PaletteBytes& game_palette,
    uint32_t source_x,
    uint32_t source_y,
    uint32_t dest_width,
    uint32_t dest_height,
    float scale,
    uint8_t* dest_rgb,
    uint32_t dest_stride_pixels,
    platform::job_pool::Pool* pool
) {
    if (scale <= 0.0F)
        scale = 1.0F;
    if (dest_width == 0 || dest_height == 0)
        return std::nullopt;
    if (dest_rgb == nullptr || dest_stride_pixels < dest_width)
        return Error{ErrorCode::output_limit, "scaled viewport destination is empty"};
    const auto expected_tile_indices = static_cast<uint64_t>(map.tile_width) * map.tile_height;
    const auto expected_tile_bytes =
        static_cast<uint64_t>(map.tile_count) * formats::tnt::layout::tile_bytes;
    if (expected_tile_indices != map.tile_indices.size() ||
        expected_tile_bytes != map.tile_palette_indices.size()) {
        return Error{
            ErrorCode::invalid_map_model, "TNT arrays do not match the declared tile grid"
        };
    }
    auto scale_fp = static_cast<uint32_t>(std::lround(static_cast<double>(scale) * 65536.0));
    if (scale_fp == 0)
        scale_fp = 1;
    std::array<uint8_t, 256U * 3U> lut{};
    for (std::size_t index = 0; index < 256U; ++index)
        std::memcpy(&lut[index * 3U], &game_palette[index * palette_entry_bytes], 3U);
    const ScaledFill fill{
        &map,
        lut.data(),
        source_x,
        source_y,
        dest_width,
        scale_fp,
        static_cast<uint64_t>(map.tile_width) * formats::tnt::layout::tile_edge_pixels,
        static_cast<uint64_t>(map.tile_height) * formats::tnt::layout::tile_edge_pixels,
        dest_rgb,
        dest_stride_pixels
    };
    std::atomic<bool> missing_tile{false};
    platform::job_pool::run_bands(
        pool,
        platform::job_pool::bands_of_rows(dest_height, terrain_band_rows),
        [&](uint32_t band) {
            if (missing_tile.load(std::memory_order_relaxed))
                return;
            const uint32_t first_row = band * terrain_band_rows;
            const uint32_t end_row = std::min(dest_height, first_row + terrain_band_rows);
            if (!fill_scaled_rows(fill, first_row, end_row))
                missing_tile.store(true, std::memory_order_relaxed);
        }
    );
    if (missing_tile.load(std::memory_order_relaxed))
        return Error{ErrorCode::invalid_map_model, "TNT tile grid references a missing tile"};
    return std::nullopt;
}

RenderResult render_scaled_viewport(
    const formats::tnt::Map& map,
    const PaletteBytes& game_palette,
    uint32_t source_x,
    uint32_t source_y,
    uint32_t dest_width,
    uint32_t dest_height,
    float scale
) {
    if (scale <= 0.0F)
        scale = 1.0F;
    if (dest_width == 0 || dest_height == 0)
        return {Surface{dest_width, dest_height, {}}, std::nullopt};
    const uint64_t pixel_count = static_cast<uint64_t>(dest_width) * dest_height;
    if (pixel_count > maximum_output_pixels ||
        pixel_count > std::numeric_limits<std::size_t>::max() / 3U) {
        return failure(
            ErrorCode::output_limit, "terrain viewport exceeds the 64 Mi-pixel output limit"
        );
    }
    Surface output{
        dest_width, dest_height, std::vector<uint8_t>(static_cast<std::size_t>(pixel_count) * 3U)
    };
    if (auto error = fill_scaled_viewport(
            map,
            game_palette,
            source_x,
            source_y,
            dest_width,
            dest_height,
            scale,
            output.rgb.data(),
            dest_width
        ))
        return {std::nullopt, std::move(error)};
    return {std::move(output), std::nullopt};
}

std::optional<BattlefieldViewport> game_battlefield_viewport(
    uint32_t source_x, uint32_t source_y, uint32_t surface_width, uint32_t surface_height
) noexcept {
    constexpr uint32_t left = 128;
    constexpr uint32_t top = 32;
    constexpr uint32_t bottom_panel_height = 32;
    if (surface_width < left || surface_height < top + bottom_panel_height)
        return std::nullopt;
    return BattlefieldViewport{
        source_x,
        source_y,
        static_cast<int32_t>(left),
        static_cast<int32_t>(top),
        surface_width - left,
        surface_height - top - bottom_panel_height,
        surface_width,
        surface_height
    };
}

ScreenPoint map_pixel_to_screen(const BattlefieldViewport& viewport, MapPixel map_pixel) noexcept {
    const auto dx = static_cast<int64_t>(map_pixel.x) - static_cast<int64_t>(viewport.source_x);
    const auto dy = static_cast<int64_t>(map_pixel.y) - static_cast<int64_t>(viewport.source_y);
    // At the whole scale a map pixel is a screen pixel: the offsets are kept as they are.
    const bool whole = viewport.scale == 0.0F || viewport.scale == 1.0F;
    const auto scale = whole ? 1.0 : static_cast<double>(viewport.scale);
    const auto x =
        static_cast<int64_t>(viewport.destination_x) +
        (whole ? dx : static_cast<int64_t>(std::llround(static_cast<double>(dx) * scale)));
    const auto y =
        static_cast<int64_t>(viewport.destination_y) +
        (whole ? dy : static_cast<int64_t>(std::llround(static_cast<double>(dy) * scale)));
    return {
        static_cast<int32_t>(std::clamp<int64_t>(
            x, std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::max()
        )),
        static_cast<int32_t>(std::clamp<int64_t>(
            y, std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::max()
        ))
    };
}

std::optional<MapPixel>
screen_to_map_pixel(const BattlefieldViewport& viewport, ScreenPoint screen) noexcept {
    const auto relative_x = static_cast<int64_t>(screen.x) - viewport.destination_x;
    const auto relative_y = static_cast<int64_t>(screen.y) - viewport.destination_y;
    if (relative_x < 0 || relative_y < 0 || relative_x >= viewport.width ||
        relative_y >= viewport.height)
        return std::nullopt;
    const auto scale = viewport.scale == 0.0F ? 1.0 : static_cast<double>(viewport.scale);
    const auto map_x = static_cast<uint64_t>(viewport.source_x) +
                       static_cast<uint64_t>(std::llround(static_cast<double>(relative_x) / scale));
    const auto map_y = static_cast<uint64_t>(viewport.source_y) +
                       static_cast<uint64_t>(std::llround(static_cast<double>(relative_y) / scale));
    if (map_x > std::numeric_limits<uint32_t>::max() ||
        map_y > std::numeric_limits<uint32_t>::max())
        return std::nullopt;
    return MapPixel{static_cast<uint32_t>(map_x), static_cast<uint32_t>(map_y)};
}

int32_t screen_span(const BattlefieldViewport& viewport, int32_t map_pixels) noexcept {
    const auto scale = viewport.scale == 0.0F ? 1.0 : static_cast<double>(viewport.scale);
    const auto span = static_cast<int64_t>(std::llround(static_cast<double>(map_pixels) * scale));
    return static_cast<int32_t>(std::clamp<int64_t>(span, 1, std::numeric_limits<int32_t>::max()));
}

RenderResult render_battlefield_viewport(
    const formats::tnt::Map& map, const PaletteBytes& palette, const BattlefieldViewport& viewport
) {
    const auto right = static_cast<int64_t>(viewport.destination_x) + viewport.width;
    const auto bottom = static_cast<int64_t>(viewport.destination_y) + viewport.height;
    if (viewport.destination_x < 0 || viewport.destination_y < 0 ||
        right > viewport.surface_width || bottom > viewport.surface_height)
        return failure(
            ErrorCode::viewport_out_of_bounds,
            "battlefield destination extends beyond the output surface"
        );
    const auto pixels = static_cast<uint64_t>(viewport.surface_width) * viewport.surface_height;
    if (pixels > maximum_output_pixels || pixels > std::numeric_limits<std::size_t>::max() / 3U)
        return failure(
            ErrorCode::output_limit, "battlefield surface exceeds the 64 Mi-pixel output limit"
        );
    auto crop = render_viewport(
        map, palette, {viewport.source_x, viewport.source_y, viewport.width, viewport.height}
    );
    if (!crop.ok())
        return crop;
    Surface output{
        viewport.surface_width,
        viewport.surface_height,
        std::vector<uint8_t>(static_cast<std::size_t>(pixels) * 3U)
    };
    if (viewport.width == 0 || viewport.height == 0)
        return {std::move(output), std::nullopt};
    for (uint32_t y = 0; y < viewport.height; ++y) {
        const auto source = static_cast<std::size_t>(y) * viewport.width * 3U;
        const auto destination =
            (static_cast<std::size_t>(viewport.destination_y + static_cast<int32_t>(y)) *
                 output.width +
             static_cast<std::size_t>(viewport.destination_x)) *
            3U;
        std::copy_n(
            crop.surface->rgb.begin() + static_cast<std::ptrdiff_t>(source),
            static_cast<std::size_t>(viewport.width) * 3U,
            output.rgb.begin() + static_cast<std::ptrdiff_t>(destination)
        );
    }
    return {std::move(output), std::nullopt};
}

bool shared_radar_contact(const RadarShare& share) noexcept {
    // A zero alliance byte, or a clear ShareRadar bit, hides the contact.
    if (share.alliance_byte == 0 || (share.share_flags & share_flag_share_radar) == 0)
        return false;
    return true;
}

} // namespace oa::present::world_renderer
