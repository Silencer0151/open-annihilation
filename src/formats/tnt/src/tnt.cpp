// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/formats/tnt.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <utility>

namespace oa::formats::tnt {
namespace {

[[nodiscard]] bool fits(std::size_t at, std::size_t count, std::size_t size) noexcept {
    return at <= size && count <= size - at;
}

[[nodiscard]] uint16_t le16(std::span<const uint8_t> bytes, std::size_t at) noexcept {
    return static_cast<uint16_t>(bytes[at]) |
           static_cast<uint16_t>(static_cast<uint16_t>(bytes[at + 1]) << 8U);
}

[[nodiscard]] uint32_t le32(std::span<const uint8_t> bytes, std::size_t at) noexcept {
    return static_cast<uint32_t>(bytes[at]) | (static_cast<uint32_t>(bytes[at + 1]) << 8U) |
           (static_cast<uint32_t>(bytes[at + 2]) << 16U) |
           (static_cast<uint32_t>(bytes[at + 3]) << 24U);
}

[[nodiscard]] ParseResult failure(ErrorCode code, std::size_t at, std::string message) {
    return {std::nullopt, Error{code, at, std::move(message)}};
}

[[nodiscard]] bool multiply(std::size_t left, std::size_t right, std::size_t& result) noexcept {
    if (left != 0 && right > std::numeric_limits<std::size_t>::max() / left)
        return false;
    result = left * right;
    return true;
}

/// Decodes the 64-byte file header; the caller has checked that it fits.
[[nodiscard]] Header read_header(std::span<const uint8_t> bytes) noexcept {
    Header header;
    header.id_version = le32(bytes, offsetof(Header, id_version));
    header.width = le32(bytes, offsetof(Header, width));
    header.height = le32(bytes, offsetof(Header, height));
    header.tile_map_offset = le32(bytes, offsetof(Header, tile_map_offset));
    header.attribute_offset = le32(bytes, offsetof(Header, attribute_offset));
    header.tile_pixels_offset = le32(bytes, offsetof(Header, tile_pixels_offset));
    header.tile_count = le32(bytes, offsetof(Header, tile_count));
    header.feature_count = le32(bytes, offsetof(Header, feature_count));
    header.feature_offset = le32(bytes, offsetof(Header, feature_offset));
    header.sea_level = le32(bytes, offsetof(Header, sea_level));
    header.minimap_offset = le32(bytes, offsetof(Header, minimap_offset));
    header.minimap_presence_flags = le32(bytes, offsetof(Header, minimap_presence_flags));
    header.reserved_after_presence_flags =
        le32(bytes, offsetof(Header, reserved_after_presence_flags));
    header.reserved_before_legacy_minimap =
        le32(bytes, offsetof(Header, reserved_before_legacy_minimap));
    header.legacy_minimap_offset = le32(bytes, offsetof(Header, legacy_minimap_offset));
    header.legacy_presence_flags = le32(bytes, offsetof(Header, legacy_presence_flags));
    return header;
}

[[nodiscard]] TileAttr read_tile_attr(std::span<const uint8_t> bytes, std::size_t at) noexcept {
    TileAttr record;
    record.height = bytes[at + offsetof(TileAttr, height)];
    record.feature = le16(bytes, at + offsetof(TileAttr, feature));
    record.padding = bytes[at + offsetof(TileAttr, padding)];
    return record;
}

/// Decodes the height, feature and metal of the version 0x1020 cell at `at`.
[[nodiscard]] LegacyTileAttr
read_legacy_tile_attr(std::span<const uint8_t> bytes, std::size_t at) noexcept {
    LegacyTileAttr record;
    record.height = bytes[at + offsetof(LegacyTileAttr, height)];
    record.feature = bytes[at + offsetof(LegacyTileAttr, feature)];
    record.metal = bytes[at + offsetof(LegacyTileAttr, metal)];
    return record;
}

} // namespace

ParseResult parse(std::span<const uint8_t> bytes) {
    if (bytes.size() > limit::input_bytes)
        return failure(ErrorCode::input_limit, 0, "TNT input exceeds 256 MiB limit");
    if (!fits(0, layout::header_bytes, bytes.size()))
        return failure(ErrorCode::truncated, 0, "truncated TNT header");

    Map map;
    map.header = read_header(bytes);
    const auto raw_version = map.header.id_version;
    if (raw_version == static_cast<uint32_t>(Version::total_annihilation)) {
        map.version = Version::total_annihilation;
    } else if (raw_version == static_cast<uint32_t>(Version::legacy_1020)) {
        map.version = Version::legacy_1020;
    } else {
        return failure(ErrorCode::unsupported_version, 0, "unsupported TNT version");
    }
    map.attribute_width = map.header.width;
    map.attribute_height = map.header.height;
    if (map.attribute_width > limit::attribute_dimension ||
        map.attribute_height > limit::attribute_dimension) {
        return failure(
            ErrorCode::dimension_limit,
            offsetof(Header, width),
            "TNT attribute dimensions exceed limit"
        );
    }
    std::size_t attribute_count = 0;
    if (!multiply(map.attribute_width, map.attribute_height, attribute_count) ||
        attribute_count > limit::attribute_cells) {
        return failure(
            ErrorCode::dimension_limit,
            offsetof(Header, width),
            "TNT attribute cell count exceeds limit"
        );
    }
    map.tile_width = map.attribute_width / layout::attribute_cells_per_tile_edge;
    map.tile_height = map.attribute_height / layout::attribute_cells_per_tile_edge;
    std::size_t tile_map_count = 0;
    if (!multiply(map.tile_width, map.tile_height, tile_map_count)) {
        return failure(
            ErrorCode::dimension_limit, offsetof(Header, width), "TNT tile grid overflows"
        );
    }
    map.sea_level = map.header.sea_level;
    map.tile_count = map.header.tile_count;
    const auto feature_count = static_cast<std::size_t>(map.header.feature_count);
    if (map.tile_count > limit::tiles || feature_count > limit::features) {
        return failure(
            ErrorCode::count_limit,
            offsetof(Header, tile_count),
            "TNT tile or feature count exceeds limit"
        );
    }

    const auto tile_map_at = static_cast<std::size_t>(map.header.tile_map_offset);
    std::size_t tile_map_bytes = 0;
    if (!multiply(tile_map_count, sizeof(uint16_t), tile_map_bytes) ||
        !fits(tile_map_at, tile_map_bytes, bytes.size())) {
        return failure(
            ErrorCode::offset_out_of_range, tile_map_at, "TNT tile map is outside input"
        );
    }
    map.tile_indices.resize(tile_map_count);
    for (std::size_t index = 0; index < tile_map_count; ++index) {
        map.tile_indices[index] = le16(bytes, tile_map_at + index * sizeof(uint16_t));
        if (map.tile_indices[index] >= map.tile_count) {
            return failure(
                ErrorCode::invalid_tile_index,
                tile_map_at + index * sizeof(uint16_t),
                "TNT tile map references a missing tile"
            );
        }
    }

    const auto attribute_at = static_cast<std::size_t>(map.header.attribute_offset);
    const auto attribute_stride = map.version == Version::total_annihilation
                                      ? layout::current_attribute_bytes
                                      : layout::legacy_attribute_bytes;
    std::size_t attribute_bytes = 0;
    if (!multiply(attribute_count, attribute_stride, attribute_bytes) ||
        !fits(attribute_at, attribute_bytes, bytes.size())) {
        return failure(
            ErrorCode::offset_out_of_range, attribute_at, "TNT attributes are outside input"
        );
    }
    map.attributes.resize(attribute_count);
    for (std::size_t index = 0; index < attribute_count; ++index) {
        const auto record_at = attribute_at + index * attribute_stride;
        auto& attribute = map.attributes[index];
        if (map.version == Version::total_annihilation) {
            const auto record = read_tile_attr(bytes, record_at);
            attribute.height = record.height;
            attribute.feature = record.feature;
            attribute.padding = record.padding;
        } else {
            const auto record = read_legacy_tile_attr(bytes, record_at);
            attribute.height = record.height;
            attribute.feature = record.feature;
            attribute.legacy_metal = record.metal;
        }
    }

    const auto tile_pixels_at = static_cast<std::size_t>(map.header.tile_pixels_offset);
    std::size_t tile_pixels_bytes = 0;
    if (!multiply(map.tile_count, layout::tile_bytes, tile_pixels_bytes) ||
        !fits(tile_pixels_at, tile_pixels_bytes, bytes.size())) {
        return failure(
            ErrorCode::offset_out_of_range, tile_pixels_at, "TNT tile pixels are outside input"
        );
    }
    map.tile_palette_indices.assign(
        bytes.begin() + static_cast<std::ptrdiff_t>(tile_pixels_at),
        bytes.begin() + static_cast<std::ptrdiff_t>(tile_pixels_at + tile_pixels_bytes)
    );

    const auto feature_at = static_cast<std::size_t>(map.header.feature_offset);
    std::size_t feature_bytes = 0;
    if (!multiply(feature_count, layout::feature_record_bytes, feature_bytes) ||
        !fits(feature_at, feature_bytes, bytes.size())) {
        return failure(
            ErrorCode::offset_out_of_range, feature_at, "TNT feature records are outside input"
        );
    }
    map.features.resize(feature_count);
    for (std::size_t index = 0; index < feature_count; ++index) {
        const auto record_at = feature_at + index * layout::feature_record_bytes;
        auto& feature = map.features[index];
        std::copy_n(
            bytes.begin() + static_cast<std::ptrdiff_t>(record_at),
            static_cast<std::ptrdiff_t>(layout::feature_record_bytes),
            feature.raw.begin()
        );
        feature.stored_index = le32(bytes, record_at + offsetof(FeatureDiskRecord, stored_index));
        const auto name_at = record_at + offsetof(FeatureDiskRecord, name);
        const auto name_end = std::find(
            bytes.begin() + static_cast<std::ptrdiff_t>(name_at),
            bytes.begin() + static_cast<std::ptrdiff_t>(name_at + layout::feature_name_bytes),
            uint8_t{0}
        );
        feature.name.assign(bytes.begin() + static_cast<std::ptrdiff_t>(name_at), name_end);
    }

    const auto current = map.version == Version::total_annihilation;
    map.minimap_presence_flags =
        current ? map.header.minimap_presence_flags : map.header.legacy_presence_flags;
    if ((map.minimap_presence_flags & layout::minimap_present_flag) != 0) {
        const auto minimap_at = static_cast<std::size_t>(
            current ? map.header.minimap_offset : map.header.legacy_minimap_offset
        );
        if (!fits(minimap_at, layout::minimap_header_bytes, bytes.size())) {
            return failure(
                ErrorCode::offset_out_of_range, minimap_at, "TNT minimap header is outside input"
            );
        }
        Minimap minimap;
        minimap.width = le32(bytes, minimap_at + offsetof(MinimapHeader, width));
        minimap.height = le32(bytes, minimap_at + offsetof(MinimapHeader, height));
        if (minimap.width > limit::minimap_dimension || minimap.height > limit::minimap_dimension) {
            return failure(
                ErrorCode::dimension_limit, minimap_at, "TNT minimap dimensions exceed limit"
            );
        }
        std::size_t minimap_pixels = 0;
        if (!multiply(minimap.width, minimap.height, minimap_pixels) ||
            !fits(minimap_at + layout::minimap_header_bytes, minimap_pixels, bytes.size())) {
            return failure(
                ErrorCode::offset_out_of_range, minimap_at, "TNT minimap pixels are outside input"
            );
        }
        minimap.palette_indices.assign(
            bytes.begin() + static_cast<std::ptrdiff_t>(minimap_at + layout::minimap_header_bytes),
            bytes.begin() + static_cast<std::ptrdiff_t>(
                                minimap_at + layout::minimap_header_bytes + minimap_pixels
                            )
        );
        map.minimap = std::move(minimap);
    }
    return {std::move(map), std::nullopt};
}

RenderResult render_palette_indices(const Map& map) {
    const auto width = static_cast<std::size_t>(map.tile_width) * layout::tile_edge_pixels;
    const auto height = static_cast<std::size_t>(map.tile_height) * layout::tile_edge_pixels;
    std::size_t pixel_count = 0;
    if (!multiply(width, height, pixel_count) || pixel_count > limit::rendered_pixels ||
        width > std::numeric_limits<uint32_t>::max() ||
        height > std::numeric_limits<uint32_t>::max()) {
        return {
            std::nullopt,
            Error{ErrorCode::render_limit, 0, "expanded TNT terrain exceeds render limit"}
        };
    }
    const auto tile_count = static_cast<std::size_t>(map.tile_width) * map.tile_height;
    if (map.tile_indices.size() != tile_count ||
        map.tile_palette_indices.size() !=
            static_cast<std::size_t>(map.tile_count) * layout::tile_bytes) {
        return {
            std::nullopt,
            Error{ErrorCode::count_limit, 0, "TNT model arrays do not match dimensions"}
        };
    }
    TerrainIndices output{
        static_cast<uint32_t>(width),
        static_cast<uint32_t>(height),
        std::vector<uint8_t>(pixel_count)
    };
    for (std::size_t tile_y = 0; tile_y < map.tile_height; ++tile_y) {
        for (std::size_t tile_x = 0; tile_x < map.tile_width; ++tile_x) {
            const auto tile_index = map.tile_indices[tile_y * map.tile_width + tile_x];
            if (tile_index >= map.tile_count) {
                return {
                    std::nullopt,
                    Error{ErrorCode::invalid_tile_index, 0, "TNT model references a missing tile"}
                };
            }
            const auto source_at = static_cast<std::size_t>(tile_index) * layout::tile_bytes;
            for (std::size_t row = 0; row < layout::tile_edge_pixels; ++row) {
                const auto destination_at = (tile_y * layout::tile_edge_pixels + row) * width +
                                            tile_x * layout::tile_edge_pixels;
                std::copy_n(
                    map.tile_palette_indices.begin() +
                        static_cast<std::ptrdiff_t>(source_at + row * layout::tile_edge_pixels),
                    static_cast<std::ptrdiff_t>(layout::tile_edge_pixels),
                    output.palette_indices.begin() + static_cast<std::ptrdiff_t>(destination_at)
                );
            }
        }
    }
    return {std::move(output), std::nullopt};
}

std::vector<FeaturePlacement> feature_placements(const Map& map) {
    std::vector<FeaturePlacement> placements;
    if (map.attribute_width == 0 ||
        map.attributes.size() !=
            static_cast<std::size_t>(map.attribute_width) * map.attribute_height) {
        return placements;
    }
    for (std::size_t index = 0; index < map.attributes.size(); ++index) {
        const auto feature = map.attributes[index].feature;
        const auto sentinel_floor = map.version == Version::total_annihilation
                                        ? value::current_feature_sentinel_floor
                                        : value::legacy_feature_sentinel_floor;
        if (feature >= sentinel_floor || feature >= map.features.size())
            continue;
        placements.push_back(
            {static_cast<uint32_t>(index % map.attribute_width),
             static_cast<uint32_t>(index / map.attribute_width),
             feature}
        );
    }
    return placements;
}

int32_t map_file_memory_class(int32_t file_bytes) noexcept {
    if (file_bytes < file_memory::class_16_below)
        return file_memory::class_16;
    if (file_bytes < file_memory::class_24_below)
        return file_memory::class_24;
    if (file_bytes < file_memory::class_32_below)
        return file_memory::class_32;
    if (file_bytes < file_memory::class_48_below)
        return file_memory::class_48;
    // At least class_128_at gives 128, else 64: (flag - 1) & upper_mask, plus upper_bias.
    const auto at_least_12mib = file_bytes >= file_memory::class_128_at ? 1 : 0;
    return ((at_least_12mib - 1) & file_memory::upper_mask) + file_memory::upper_bias;
}

bool load_radar_picture(
    const MapFileReader& file, RadarPicture& picture, int32_t* map_width, int32_t* map_height
) {
    picture = {};
    uint8_t bytes[layout::header_bytes];
    if (file.read == nullptr || !file.read(file.context, 0, bytes, sizeof bytes))
        return false;
    const Header header = read_header(bytes);
    bool loaded = false;
    uint8_t extent[layout::minimap_header_bytes];
    if ((header.minimap_presence_flags & layout::minimap_present_flag) != 0 &&
        file.read(file.context, header.minimap_offset, extent, sizeof extent)) {
        const MinimapHeader size{
            le32(extent, offsetof(MinimapHeader, width)),
            le32(extent, offsetof(MinimapHeader, height))
        };
        if (size.width <= limit::minimap_dimension && size.height <= limit::minimap_dimension) {
            picture.width = static_cast<int32_t>(size.width);
            picture.height = static_cast<int32_t>(size.height);
            picture.pixels.resize(static_cast<std::size_t>(size.width) * size.height);
            loaded = picture.pixels.empty() ||
                     file.read(
                         file.context,
                         header.minimap_offset + static_cast<uint32_t>(sizeof extent),
                         picture.pixels.data(),
                         static_cast<uint32_t>(picture.pixels.size())
                     );
            if (!loaded)
                picture = {};
        }
    }
    if (map_width != nullptr && map_height != nullptr) {
        *map_width = static_cast<int32_t>(header.width);
        *map_height = static_cast<int32_t>(header.height);
    }
    return loaded;
}

} // namespace oa::formats::tnt
