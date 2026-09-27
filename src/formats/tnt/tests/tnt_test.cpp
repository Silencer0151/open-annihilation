// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/formats/tnt.hpp"
#include "oa/formats/ota.hpp"

#include <algorithm>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace tnt = oa::formats::tnt;

namespace {

void require(bool condition, std::string_view message) {
    if (!condition)
        throw std::runtime_error(std::string(message));
}

void put16(std::vector<uint8_t>& bytes, std::size_t at, uint16_t value) {
    bytes[at] = static_cast<uint8_t>(value);
    bytes[at + 1] = static_cast<uint8_t>(value >> 8U);
}

void put32(std::vector<uint8_t>& bytes, std::size_t at, uint32_t value) {
    bytes[at] = static_cast<uint8_t>(value);
    bytes[at + 1] = static_cast<uint8_t>(value >> 8U);
    bytes[at + 2] = static_cast<uint8_t>(value >> 16U);
    bytes[at + 3] = static_cast<uint8_t>(value >> 24U);
}

std::vector<uint8_t> current_fixture() {
    constexpr std::size_t tile_map_at = 64;
    constexpr std::size_t attributes_at = 72;
    constexpr std::size_t tiles_at = 136;
    constexpr std::size_t features_at = tiles_at + 2 * tnt::layout::tile_bytes;
    constexpr std::size_t minimap_at = features_at + tnt::layout::feature_record_bytes;
    std::vector<uint8_t> bytes(minimap_at + 10);
    put32(bytes, 0, static_cast<uint32_t>(tnt::Version::total_annihilation));
    put32(bytes, 4, 4);
    put32(bytes, 8, 4);
    put32(bytes, 12, tile_map_at);
    put32(bytes, 16, attributes_at);
    put32(bytes, 20, tiles_at);
    put32(bytes, 24, 2);
    put32(bytes, 28, 1);
    put32(bytes, 32, features_at);
    put32(bytes, 36, 17);
    put32(bytes, 40, minimap_at);
    put32(bytes, 44, 1);
    put16(bytes, tile_map_at, 0);
    put16(bytes, tile_map_at + 2, 1);
    put16(bytes, tile_map_at + 4, 1);
    put16(bytes, tile_map_at + 6, 0);
    for (std::size_t index = 0; index < 16; ++index) {
        const auto at = attributes_at + index * 4;
        bytes[at] = static_cast<uint8_t>(index);
        put16(bytes, at + 1, index == 3 ? 0U : 0xffffU);
        bytes[at + 3] = 0xaa;
    }
    std::fill_n(
        bytes.begin() + static_cast<std::ptrdiff_t>(tiles_at),
        static_cast<std::ptrdiff_t>(tnt::layout::tile_bytes),
        uint8_t{5}
    );
    std::fill_n(
        bytes.begin() + static_cast<std::ptrdiff_t>(tiles_at + tnt::layout::tile_bytes),
        static_cast<std::ptrdiff_t>(tnt::layout::tile_bytes),
        uint8_t{9}
    );
    bytes[features_at + 4] = 0x42;
    bytes[features_at + 5] = 0x43;
    put32(bytes, minimap_at, 2);
    put32(bytes, minimap_at + 4, 2);
    bytes[minimap_at + 8] = 1;
    bytes[minimap_at + 9] = 2;
    bytes.push_back(3);
    bytes.push_back(4);
    return bytes;
}

void current_layout_and_render() {
    const auto result = tnt::parse(current_fixture());
    require(result.ok(), result.error ? result.error->message : "current TNT did not parse");
    const auto& map = *result.map;
    require(
        map.version == tnt::Version::total_annihilation && map.attribute_width == 4 &&
            map.attribute_height == 4 && map.tile_width == 2 && map.tile_height == 2 &&
            map.sea_level == 17,
        "current header mapping disagreed"
    );
    require(
        map.attributes.size() == 16 && map.attributes[3].height == 3 &&
            map.attributes[3].feature == 0 && map.attributes[3].padding == 0xaa,
        "current four-byte attributes disagreed"
    );
    require(
        map.features.size() == 1 && map.features[0].stored_index == 0 &&
            map.features[0].name == "BC" && map.features[0].raw[4] == 0x42,
        "0x84-byte feature record was not retained"
    );
    const auto placements = tnt::feature_placements(map);
    require(
        placements.size() == 1 && placements[0].attribute_x == 3 &&
            placements[0].attribute_y == 0 && placements[0].feature == 0,
        "feature placements did not retain attribute-grid coordinates"
    );
    require(
        map.minimap && map.minimap->width == 2 && map.minimap->height == 2 &&
            map.minimap->palette_indices == std::vector<uint8_t>({1, 2, 3, 4}),
        "flagged minimap did not parse"
    );
    const auto rendered = tnt::render_palette_indices(map);
    require(
        rendered.ok() && rendered.terrain->width == 64 && rendered.terrain->height == 64,
        "tile mosaic dimensions disagreed"
    );
    require(
        rendered.terrain->palette_indices[0] == 5 && rendered.terrain->palette_indices[31] == 5 &&
            rendered.terrain->palette_indices[32] == 9 &&
            rendered.terrain->palette_indices[32U * 64U] == 9 &&
            rendered.terrain->palette_indices.back() == 5,
        "32x32 tile order disagreed"
    );
}

void legacy_attribute_layout() {
    auto bytes = current_fixture();
    put32(bytes, 0, static_cast<uint32_t>(tnt::Version::legacy_1020));
    constexpr std::size_t attributes_at = 72;
    constexpr std::size_t legacy_tiles_at = attributes_at + 16 * 8;
    constexpr std::size_t legacy_features_at = legacy_tiles_at + 2 * tnt::layout::tile_bytes;
    bytes.resize(legacy_features_at + tnt::layout::feature_record_bytes);
    put32(bytes, 20, legacy_tiles_at);
    put32(bytes, 32, legacy_features_at);
    put32(bytes, 60, 0); // legacy minimap flag absent
    for (std::size_t index = 0; index < 16; ++index) {
        const auto at = attributes_at + index * 8;
        bytes[at] = static_cast<uint8_t>(20 + index);
        bytes[at + 2] = static_cast<uint8_t>(index);
        bytes[at + 6] = 0x6a;
    }
    std::fill_n(
        bytes.begin() + static_cast<std::ptrdiff_t>(legacy_tiles_at),
        static_cast<std::ptrdiff_t>(2 * tnt::layout::tile_bytes),
        uint8_t{4}
    );
    const auto result = tnt::parse(bytes);
    require(result.ok(), result.error ? result.error->message : "legacy TNT did not parse");
    require(
        result.map->attributes[5].height == 25 && result.map->attributes[5].feature == 5 &&
            result.map->attributes[5].legacy_metal == 0x6a,
        "0x1020 eight-byte attribute mapping disagreed"
    );
}

void malformed_and_bounded() {
    auto unsupported = current_fixture();
    put32(unsupported, 0, 0x4000);
    const auto unsupported_result = tnt::parse(unsupported);
    require(
        !unsupported_result.ok() &&
            unsupported_result.error->code == tnt::ErrorCode::unsupported_version,
        "unsupported version was accepted"
    );

    auto bad_offset = current_fixture();
    put32(bad_offset, 12, 0xfffffff0U);
    const auto offset_result = tnt::parse(bad_offset);
    require(
        !offset_result.ok() && offset_result.error->code == tnt::ErrorCode::offset_out_of_range,
        "out-of-range tile map was accepted"
    );

    auto bad_index = current_fixture();
    put16(bad_index, 64, 2);
    const auto index_result = tnt::parse(bad_index);
    require(
        !index_result.ok() && index_result.error->code == tnt::ErrorCode::invalid_tile_index,
        "out-of-range tile index was accepted"
    );

    auto huge = current_fixture();
    put32(huge, 4, 65535);
    put32(huge, 8, 65535);
    const auto huge_result = tnt::parse(huge);
    require(
        !huge_result.ok() && huge_result.error->code == tnt::ErrorCode::dimension_limit,
        "pathological dimensions were not rejected before allocation"
    );
}

void map_file_memory_class_thresholds() {
    require(tnt::map_file_memory_class(0) == 16, "memory class empty map file");
    require(tnt::map_file_memory_class(0x3e6665) == 16, "memory class below 0x3e6666");
    require(tnt::map_file_memory_class(0x3e6666) == 24, "memory class at 0x3e6666");
    require(tnt::map_file_memory_class(0x5fffff) == 24, "memory class below 0x600000");
    require(tnt::map_file_memory_class(0x600000) == 32, "memory class at 0x600000");
    require(tnt::map_file_memory_class(0x7fffff) == 32, "memory class below 0x800000");
    require(tnt::map_file_memory_class(0x800000) == 48, "memory class at 0x800000");
    require(tnt::map_file_memory_class(0x9fffff) == 48, "memory class below 0xa00000");
    require(tnt::map_file_memory_class(0xa00000) == 64, "memory class at 0xa00000");
    require(tnt::map_file_memory_class(0xbfffff) == 64, "memory class below 0xc00000");
    require(tnt::map_file_memory_class(0xc00000) == 128, "memory class at 0xc00000");
    require(
        tnt::map_file_memory_class(std::numeric_limits<int32_t>::max()) == 128,
        "memory class largest positive length"
    );
    require(tnt::map_file_memory_class(-1) == 16, "memory class 0xffffffff miss");
    require(
        tnt::map_file_memory_class(std::numeric_limits<int32_t>::min()) == 16,
        "memory class negative length"
    );
}

void ota_metadata_and_starts() {
    constexpr std::string_view fixture = R"(
// OTA/TDF spelling and nesting are case-insensitive, as in the game.
[GlobalHeader] {
 missionname=Crystal Maze; missiondescription=Test map; planet=Crystal;
 memory=32 mb; size=10 x 10;
 numplayers=2, 3, 4, 5, 6;
 [Schema 0] { Type=Network 1; [specials] {
   [special0] { specialwhat=StartPos1; XPos=672; ZPos=256; }
   [special1] { specialwhat=StartPos2; XPos=4736; ZPos=3936; }
 }}
 [Schema 1] { Type=Network 2; [specials] {
   [special0] { specialwhat=startpos4; XPos=752; ZPos=4160; }
   [special1] { specialwhat=StartPos1; XPos=4160; ZPos=608; }
   [special2] { specialwhat=StartPos3; XPos=528; ZPos=2256; }
 }}
 [Schema 2] { Type=Easy; [specials] {
   [special0] { specialwhat=StartPos8; XPos=+10; ZPos=+20; }
   [special1] { specialwhat=StartPos; XPos=30; ZPos=40; }
   [special2] { specialwhat=StartPos-2; XPos=50; ZPos=60; }
   [special3] { specialwhat=StartPos2; XPos=70; ZPos=80; }
 }}
})";
    const auto result = oa::formats::ota::parse(fixture);
    require(result.ok(), result.error ? result.error->message : "OTA did not parse");
    require(
        result.metadata->mission_name == "Crystal Maze" && result.metadata->planet == "Crystal" &&
            result.metadata->memory_requirement == "32 mb" &&
            result.metadata->map_size == "10 x 10" && result.metadata->schemas.size() == 3,
        "OTA GlobalHeader/schema extraction"
    );
    const auto& first = result.metadata->schemas[0].start_positions;
    require(
        first.size() == 2 && first[0].index == 0 && first[0].x == 672 && first[0].z == 256 &&
            first[1].index == 1,
        "StartPos record mapping"
    );
    require(
        oa::formats::ota::select_multiplayer_schema(*result.metadata, 3) ==
            &result.metadata->schemas[1],
        "exact multiplayer schema selection"
    );
    require(
        oa::formats::ota::select_multiplayer_schema(*result.metadata, 7) ==
            &result.metadata->schemas[1],
        "largest fallback schema selection"
    );
    const auto& mixed = result.metadata->schemas[2].start_positions;
    require(
        mixed.size() == 4 && mixed[0].index == 7 && mixed[0].x == 10 && mixed[0].z == 20 &&
            mixed[1].index == 0 && mixed[2].index == 1 && mixed[3].index == 1,
        "explicit suffixes changed the implicit StartPos counter"
    );
    oa::formats::ota::MapMetadata gap;
    gap.schemas.push_back({0, "Network 1", {{0, 1, 2}, {1, 3, 4}}});
    gap.schemas.push_back(
        {2, "Network 4", {{0, 1, 2}, {1, 3, 4}, {2, 5, 6}, {3, 7, 8}, {4, 9, 10}}}
    );
    require(
        oa::formats::ota::select_multiplayer_schema(gap, 5) == &gap.schemas[0],
        "schema enumeration did not stop at the first missing number"
    );
    const auto alias = oa::formats::ota::parse(
        "[GlobalHeader]{[Schema 00]{Type=Network "
        "1;[specials]{[s]{specialwhat=StartPos1;XPos=1;ZPos=2;}}}}"
    );
    require(
        alias.ok() && alias.metadata->schemas.empty(),
        "schema selection accepted a noncanonical Schema 00 alias"
    );
    const auto malformed = oa::formats::ota::parse("[GlobalHeader]{ [Schema 0] {");
    require(
        !malformed.ok() && malformed.error->code == oa::formats::ota::ErrorCode::malformed,
        "unterminated OTA accepted"
    );
}

} // namespace

// The header's minimap flag picks whether the stored picture
// loads; the map extent is reported either way.
void radar_picture_from_header() {
    auto bytes = current_fixture();
    const tnt::MapFileReader reader{
        &bytes, [](void* context, uint32_t offset, void* out, uint32_t size) {
            const auto& file = *static_cast<std::vector<uint8_t>*>(context);
            if (offset > file.size() || size > file.size() - offset)
                return false;
            std::copy_n(file.begin() + offset, size, static_cast<uint8_t*>(out));
            return true;
        }
    };
    tnt::RadarPicture picture;
    int32_t width = 0;
    int32_t height = 0;
    require(tnt::load_radar_picture(reader, picture, &width, &height), "radar picture loads");
    require(
        picture.width == 2 && picture.height == 2 &&
            picture.pixels == std::vector<uint8_t>{1, 2, 3, 4},
        "radar picture pixels"
    );
    require(width == 4 && height == 4, "map extent");
    put32(bytes, 44, 0);
    width = 0;
    require(
        !tnt::load_radar_picture(reader, picture, &width, &height) && picture.pixels.empty() &&
            width == 4,
        "no picture without the flag, extent still reported"
    );
    put32(bytes, 44, 1);
    bytes.resize(bytes.size() - 1);
    require(
        !tnt::load_radar_picture(reader, picture, nullptr, nullptr), "short pixels load nothing"
    );
}

int main() {
    try {
        current_layout_and_render();
        radar_picture_from_header();
        legacy_attribute_layout();
        malformed_and_bounded();
        map_file_memory_class_thresholds();
        ota_metadata_and_starts();
    } catch (const std::exception& error) {
        std::cerr << "map-format test failure: " << error.what() << '\n';
        return 1;
    }
    std::cout << "map-format tests passed\n";
    return 0;
}
