// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/present/world_renderer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

namespace oa::present::world_renderer {
namespace {

constexpr uint64_t maximum_output_pixels = 64ULL * 1024ULL * 1024ULL;

[[nodiscard]] RenderResult failure(ErrorCode code, std::string message) {
    return {std::nullopt, Error{code, std::move(message)}};
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
    uint32_t dest_stride_pixels
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
    const uint64_t terrain_width =
        static_cast<uint64_t>(map.tile_width) * formats::tnt::layout::tile_edge_pixels;
    const uint64_t terrain_height =
        static_cast<uint64_t>(map.tile_height) * formats::tnt::layout::tile_edge_pixels;
    constexpr uint32_t tile_edge = formats::tnt::layout::tile_edge_pixels;
    auto scale_fp = static_cast<uint32_t>(std::lround(static_cast<double>(scale) * 65536.0));
    if (scale_fp == 0)
        scale_fp = 1;
    std::array<uint8_t, 256U * 3U> lut{};
    for (std::size_t index = 0; index < 256U; ++index)
        std::memcpy(&lut[index * 3U], &game_palette[index * palette_entry_bytes], 3U);
    const uint16_t* const grid = map.tile_indices.data();
    const uint8_t* const tiles = map.tile_palette_indices.data();
    const auto missing_tile = [] {
        return Error{ErrorCode::invalid_map_model, "TNT tile grid references a missing tile"};
    };
    uint32_t map_y = source_y;
    uint32_t y_frac = 0;
    for (uint32_t destination_y = 0; destination_y < dest_height; ++destination_y) {
        auto* out = dest_rgb + static_cast<std::size_t>(destination_y) * dest_stride_pixels * 3U;
        const auto within_tile_y = static_cast<std::size_t>(map_y % tile_edge);
        const auto row_tiles = static_cast<std::size_t>(map_y / tile_edge) * map.tile_width;
        if (map_y >= terrain_height) {
            std::memset(out, 0, static_cast<std::size_t>(dest_width) * 3U);
        } else if (scale_fp == 65536u) {
            // 1:1 sampling copies whole tile rows through the palette table.
            uint64_t map_x = source_x;
            uint32_t remaining = dest_width;
            while (remaining != 0) {
                if (map_x >= terrain_width) {
                    std::memset(out, 0, static_cast<std::size_t>(remaining) * 3U);
                    break;
                }
                const auto within_tile_x = static_cast<uint32_t>(map_x % tile_edge);
                const auto tile_index = grid[row_tiles + map_x / tile_edge];
                if (tile_index >= map.tile_count)
                    return missing_tile();
                const auto run = std::min(remaining, tile_edge - within_tile_x);
                const auto* source =
                    tiles +
                    static_cast<std::size_t>(tile_index) * formats::tnt::layout::tile_bytes +
                    within_tile_y * tile_edge + within_tile_x;
                for (uint32_t pixel = 0; pixel < run; ++pixel, out += 3)
                    std::memcpy(out, &lut[static_cast<std::size_t>(source[pixel]) * 3U], 3U);
                map_x += run;
                remaining -= run;
            }
        } else {
            uint32_t map_x = source_x;
            uint32_t x_frac = 0;
            const uint8_t* rgb = nullptr;
            static constexpr uint8_t black[3] = {0, 0, 0};
            for (uint32_t destination_x = 0; destination_x < dest_width; ++destination_x) {
                if (rgb == nullptr) {
                    if (map_x >= terrain_width) {
                        rgb = black;
                    } else {
                        const auto tile_index = grid[row_tiles + map_x / tile_edge];
                        if (tile_index >= map.tile_count)
                            return missing_tile();
                        const auto tile_pixel = static_cast<std::size_t>(tile_index) *
                                                    formats::tnt::layout::tile_bytes +
                                                within_tile_y * tile_edge + map_x % tile_edge;
                        rgb = &lut[static_cast<std::size_t>(tiles[tile_pixel]) * 3U];
                    }
                }
                std::memcpy(out, rgb, 3U);
                out += 3;
                x_frac += 65536u;
                while (x_frac >= scale_fp) {
                    x_frac -= scale_fp;
                    ++map_x;
                    rgb = nullptr;
                }
            }
        }
        y_frac += 65536u;
        while (y_frac >= scale_fp) {
            y_frac -= scale_fp;
            ++map_y;
        }
    }
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
    const auto scale = viewport.scale == 0.0F ? 1.0 : static_cast<double>(viewport.scale);
    const auto x = static_cast<int64_t>(viewport.destination_x) +
                   static_cast<int64_t>(std::llround(static_cast<double>(dx) * scale));
    const auto y = static_cast<int64_t>(viewport.destination_y) +
                   static_cast<int64_t>(std::llround(static_cast<double>(dy) * scale));
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
    // A zero alliance byte, or ShareRadar bit 0x40 clear, hides the contact.
    if (share.alliance_byte == 0 || (share.share_flags & 0x40U) == 0)
        return false;
    return true;
}

} // namespace oa::present::world_renderer
