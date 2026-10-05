// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace oa::formats::tnt {

enum class Version : uint32_t {
    legacy_1020 = 0x00001020U,
    total_annihilation = 0x00002000U,
};

// 64-byte on-disk header. Version 0x2000 keeps the minimap in minimap_offset
// and minimap_presence_flags, version 0x1020 in the last two words. The two
// words between them are zero in every shipped map; they are kept in Header
// but not used. TA: Kingdoms maps (version 0x4000) use minimap_presence_flags
// as an offset; the game does not accept them.
struct Header {
    uint32_t id_version = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t tile_map_offset = 0;
    uint32_t attribute_offset = 0;
    uint32_t tile_pixels_offset = 0;
    uint32_t tile_count = 0;
    uint32_t feature_count = 0;
    uint32_t feature_offset = 0;
    uint32_t sea_level = 0;
    uint32_t minimap_offset = 0;
    uint32_t minimap_presence_flags = 0;
    uint32_t reserved_after_presence_flags = 0;
    uint32_t reserved_before_legacy_minimap = 0;
    uint32_t legacy_minimap_offset = 0;
    uint32_t legacy_presence_flags = 0;
};

static_assert(sizeof(Header) == 64);
static_assert(offsetof(Header, id_version) == 0x00);
static_assert(offsetof(Header, width) == 0x04);
static_assert(offsetof(Header, height) == 0x08);
static_assert(offsetof(Header, tile_map_offset) == 0x0c);
static_assert(offsetof(Header, attribute_offset) == 0x10);
static_assert(offsetof(Header, tile_pixels_offset) == 0x14);
static_assert(offsetof(Header, tile_count) == 0x18);
static_assert(offsetof(Header, feature_count) == 0x1c);
static_assert(offsetof(Header, feature_offset) == 0x20);
static_assert(offsetof(Header, sea_level) == 0x24);
static_assert(offsetof(Header, minimap_offset) == 0x28);
static_assert(offsetof(Header, minimap_presence_flags) == 0x2c);
static_assert(offsetof(Header, reserved_after_presence_flags) == 0x30);
static_assert(offsetof(Header, reserved_before_legacy_minimap) == 0x34);
static_assert(offsetof(Header, legacy_minimap_offset) == 0x38);
static_assert(offsetof(Header, legacy_presence_flags) == 0x3c);

// 0x2000 attribute cell, one per 16px cell (four per 32px tile): height,
// little-endian feature, pad.
#pragma pack(push, 1)

struct TileAttr {
    uint8_t height = 0;
    uint16_t feature = 0;
    uint8_t padding = 0;
};

// 0x1020 cell: height at byte 0, an 8-bit feature at byte 2 and the cell's
// metal at byte 6. The other bytes keep the stride at 8 and are not read.
struct LegacyTileAttr {
    uint8_t height = 0;
    uint8_t reserved_after_height = 0;
    uint8_t feature = 0;
    uint8_t reserved_after_feature = 0;
    uint16_t reserved_before_metal = 0;
    // The cell's starting metal in its map plot, which version 0x2000 maps
    // take from the SurfaceMetal setting instead.
    uint8_t metal = 0;
    uint8_t reserved_after_metal = 0;
};

#pragma pack(pop)

static_assert(sizeof(TileAttr) == 4);
static_assert(offsetof(TileAttr, height) == 0);
static_assert(offsetof(TileAttr, feature) == 1);
static_assert(offsetof(TileAttr, padding) == 3);
static_assert(sizeof(LegacyTileAttr) == 8);
static_assert(offsetof(LegacyTileAttr, height) == 0);
static_assert(offsetof(LegacyTileAttr, reserved_after_height) == 1);
static_assert(offsetof(LegacyTileAttr, feature) == 2);
static_assert(offsetof(LegacyTileAttr, reserved_after_feature) == 3);
static_assert(offsetof(LegacyTileAttr, reserved_before_metal) == 4);
static_assert(offsetof(LegacyTileAttr, metal) == 6);
static_assert(offsetof(LegacyTileAttr, reserved_after_metal) == 7);

struct MinimapHeader {
    uint32_t width = 0;
    uint32_t height = 0;
};

static_assert(sizeof(MinimapHeader) == 8);
static_assert(offsetof(MinimapHeader, height) == 4);

// Feature records are 132 bytes apart; the name at record offset 4 names the
// feature definition. The leading word matches the table ordinal in every map
// the game ships; loading ignores it.
struct FeatureDiskRecord {
    uint32_t stored_index = 0;
    std::array<uint8_t, 128> name{};
};

static_assert(sizeof(FeatureDiskRecord) == 0x84);
static_assert(offsetof(FeatureDiskRecord, name) == 4);
static_assert(sizeof(FeatureDiskRecord::name) == 0x80);

namespace layout {
inline constexpr std::size_t header_words = sizeof(Header) / sizeof(uint32_t);
inline constexpr std::size_t header_bytes = sizeof(Header);
inline constexpr std::size_t attribute_cell_pixels = 16;
inline constexpr std::size_t tile_edge_pixels = 32;
inline constexpr uint32_t attribute_cells_per_tile_edge =
    static_cast<uint32_t>(tile_edge_pixels / attribute_cell_pixels);
inline constexpr std::size_t tile_bytes = tile_edge_pixels * tile_edge_pixels;
inline constexpr std::size_t current_attribute_bytes = sizeof(TileAttr);
inline constexpr std::size_t legacy_attribute_bytes = sizeof(LegacyTileAttr);
inline constexpr std::size_t feature_record_bytes = sizeof(FeatureDiskRecord);
inline constexpr std::size_t feature_name_offset = offsetof(FeatureDiskRecord, name);
inline constexpr std::size_t feature_name_bytes = sizeof(FeatureDiskRecord::name);
inline constexpr std::size_t minimap_header_bytes = sizeof(MinimapHeader);
inline constexpr uint32_t minimap_present_flag = 1U;
} // namespace layout

static_assert(layout::header_words == 16);
static_assert(layout::tile_bytes == 1024);
static_assert(layout::attribute_cells_per_tile_edge == 2U);

namespace value {
inline constexpr uint16_t current_feature_sentinel_floor = 0xfffbU;
inline constexpr uint16_t legacy_feature_sentinel_floor = 0x00fcU;
} // namespace value

namespace limit {
inline constexpr std::size_t input_bytes = 256U * 1024U * 1024U;
inline constexpr uint32_t attribute_dimension = 4096;
inline constexpr std::size_t attribute_cells = 16U * 1024U * 1024U;
inline constexpr std::size_t tiles = 65536;
inline constexpr std::size_t features = 65536;
inline constexpr uint32_t minimap_dimension = 1024;
inline constexpr std::size_t rendered_pixels = 512U * 1024U * 1024U;
} // namespace limit

// Signed TNT length compares, applied in order. file_bytes below
// class_16_below is class 16, then below class_24_below is class 24, and so on.
// class_16_below is the game's exact constant, not a rounded mebibyte. Class 64
// and class 128 come from the upper mask, not another compare.
namespace file_memory {
inline constexpr int32_t class_16_below = 0x3e6666;
inline constexpr int32_t class_24_below = 0x600000;
inline constexpr int32_t class_32_below = 0x800000;
inline constexpr int32_t class_48_below = 0xa00000;
inline constexpr int32_t class_128_at = 0xc00000;
inline constexpr int32_t class_16 = 0x10;
inline constexpr int32_t class_24 = 0x18;
inline constexpr int32_t class_32 = 0x20;
inline constexpr int32_t class_48 = 0x30;
inline constexpr int32_t upper_mask = static_cast<int32_t>(0xffffffc0);
inline constexpr int32_t upper_bias = 0x80;
} // namespace file_memory

struct Attribute {
    uint8_t height = 0;
    uint16_t feature = 0xffffU;
    uint8_t padding = 0;
    // LegacyTileAttr::metal of a version 0x1020 map; zero for version 0x2000.
    uint8_t legacy_metal = 0;
};

struct FeatureRecord {
    // Records are 0x84 bytes apart; the name at record offset 4 names the
    // feature definition. The leading word agrees with the table ordinal in
    // every map the game ships, but loading ignores it; it is retained without
    // assigning it a meaning.
    uint32_t stored_index = 0;
    std::string name;
    std::array<uint8_t, layout::feature_record_bytes> raw{};
};

struct FeaturePlacement {
    uint32_t attribute_x = 0;
    uint32_t attribute_y = 0;
    uint16_t feature = 0;
};

struct Minimap {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> palette_indices;
};

struct Map {
    Version version = Version::total_annihilation;
    Header header{};
    // Width/height are 16-pixel attribute cells. The tile grid uses half each.
    uint32_t attribute_width = 0;
    uint32_t attribute_height = 0;
    uint32_t tile_width = 0;
    uint32_t tile_height = 0;
    uint32_t sea_level = 0;
    uint32_t minimap_presence_flags = 0;
    std::vector<uint16_t> tile_indices;
    std::vector<Attribute> attributes;
    uint32_t tile_count = 0;
    // Contiguous tile_count*1024 row-major palette indices.
    std::vector<uint8_t> tile_palette_indices;
    std::vector<FeatureRecord> features;
    std::optional<Minimap> minimap;
};

enum class ErrorCode {
    none,
    input_limit,
    truncated,
    unsupported_version,
    dimension_limit,
    count_limit,
    offset_out_of_range,
    invalid_tile_index,
    render_limit,
};

struct Error {
    ErrorCode code = ErrorCode::none;
    std::size_t offset = 0;
    std::string message;
};

struct ParseResult {
    std::optional<Map> map;
    std::optional<Error> error;

    /// Returns whether a map was parsed.
    [[nodiscard]] bool ok() const noexcept { return map.has_value(); }
};

struct TerrainIndices {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> palette_indices;
};

struct RenderResult {
    std::optional<TerrainIndices> terrain;
    std::optional<Error> error;

    /// Returns whether terrain was rendered.
    [[nodiscard]] bool ok() const noexcept { return terrain.has_value(); }
};

/// Decodes a TNT map file.
///
/// The version picks the 0x2000 or 0x1020 header layout (the minimap offset
/// and presence words differ); then the tile map, cell attributes, tile
/// pixels, features and minimap are read with bounds and limit checks.
///
/// @param bytes the whole file
/// @return the map, or the first error and its offset; any other version is
///         unsupported_version, which stops the game with a fatal error
[[nodiscard]] ParseResult parse(std::span<const uint8_t> bytes);

/// Expands the 32x32 tile mosaic into one palette-index image.
///
/// @param map parsed map
/// @return tile_width*32 by tile_height*32 row-major palette indices, or
///         invalid_tile_index for a tile outside the tile set (the game does
///         not check), or render_limit when too large
[[nodiscard]] RenderResult render_palette_indices(const Map& map);

/// Lists the attribute cells whose feature value names an entry in Map::features.
///
/// @param map parsed map
/// @return placements in row-major cell order; sentinel values (including
///         0xffff and the 0xfffc void marker) are omitted
[[nodiscard]] std::vector<FeaturePlacement> feature_placements(const Map& map);

/// Classifies a TNT file length into the memory buckets the game uses.
///
/// The length is the map's TNT file size in bytes, taken as signed. This is
/// not the OTA memory literal.
///
/// @param file_bytes TNT length in bytes, or negative when the file was not found
/// @return 16, 24, 32, 48, 64 or 128; a negative length, including the
///         0xFFFFFFFF miss, stays in the 16 bucket
[[nodiscard]] int32_t map_file_memory_class(int32_t file_bytes) noexcept;

// The radar picture a map summary shows: the map file's stored minimap
// (the RADARPIC block).
struct RadarPicture {
    int32_t width = 0;
    int32_t height = 0;
    std::vector<uint8_t> pixels;
};

// Byte reads from an open map file.
struct MapFileReader {
    void* context{};
    // Copies `size` bytes at `offset`; false when fewer are there.
    bool (*read)(void* context, uint32_t offset, void* out, uint32_t size) = nullptr;
};

/// Reads a map file's header and, when its minimap flag is set, the minimap.
///
/// Minimaps larger than limit::minimap_dimension on a side are refused, and
/// so is one whose pixels, after its 8-byte header, would end past the
/// 32-bit offsets a map file is read at.
///
/// @param file reader over the open map file
/// @param[out] picture the minimap's size and pixels; empty when none loaded
/// @param[out] map_width header width, written whenever the header reads and
///        both size pointers are given
/// @param[out] map_height header height, written under the same condition
/// @return true when a picture loaded
bool load_radar_picture(
    const MapFileReader& file, RadarPicture& picture, int32_t* map_width, int32_t* map_height
);

} // namespace oa::formats::tnt
