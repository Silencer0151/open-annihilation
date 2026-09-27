// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/map_runtime.hpp"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <map>
#include <string_view>

namespace {
void require(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

oa::formats::gaf::Archive masks(std::string name = "ViSmAsK") {
    oa::formats::gaf::Frame frame;
    frame.width = 3;
    frame.height = 2;
    frame.origin_x = -1;
    frame.origin_y = 4;
    frame.transparency_index = 9;
    frame.pixels = {9, 1, 9, 2, 3, 4};
    frame.coverage.assign(frame.pixels.size(), 1);
    oa::formats::gaf::Archive archive;
    archive.sequences.push_back({std::move(name), 0, 0, {std::move(frame)}});
    return archive;
}

oa::formats::tnt::Map current_map() {
    oa::formats::tnt::Map map;
    map.version = oa::formats::tnt::Version::total_annihilation;
    map.attribute_width = 4;
    map.attribute_height = 3;
    map.attributes.resize(12);
    for (auto& attribute : map.attributes)
        attribute.feature = 0xffff;
    map.features.resize(2);
    return map;
}

class FeatureAssets final : public oa::sim::map_runtime::FeatureAssetReader {
  public:

    std::vector<std::string> paths;
    std::map<std::string, std::string, std::less<>> files;

    oa::data::unit_definitions::Result<std::vector<std::string>> list_effective_recursive(
        std::string_view directory, std::string_view extension
    ) const override {
        require(directory == "features" && extension == ".tdf", "feature recursive query");
        return {paths, {}};
    }

    oa::data::unit_definitions::Result<std::string> read(std::string_view path) const override {
        const auto found = files.find(path);
        if (found == files.end())
            return {{}, {oa::data::unit_definitions::ErrorCode::io, 0, "missing test asset"}};
        return {found->second, {}};
    }
};
} // namespace

int main() {
    using namespace oa;
    auto map = current_map();
    map.features[0].name = "Tree";
    map.features[1].name = "Rock";
    FeatureAssets feature_assets;
    feature_assets.paths = {"features/first.tdf", "features/second.tdf"};
    feature_assets.files[feature_assets.paths[0]] =
        "[TREE]{footprintx=+2junk; footprintz=3; metal=-1; indestructible=3; blocking=3; "
        "object=tree; featuredead=treedead;}"
        "[rock]{footprintx=9; footprintz=9; metal=1; indestructible=0;}";
    feature_assets.files[feature_assets.paths[1]] =
        "[ROCK]{footprintx=4; footprintz=5; metal=65538; indestructible=2;}";
    const auto resolved = sim::map_runtime::resolve_feature_terrain(map, feature_assets);
    require(resolved.ok() && resolved.value->size() == 2, "feature TDF resolution");
    require(
        (*resolved.value)[0].footprint_x == 2 && (*resolved.value)[0].footprint_z == 3 &&
            (*resolved.value)[0].metal == 65535.0F && (*resolved.value)[0].metal_overlay,
        "feature loader integer, narrowing, and indestructible projection"
    );
    require(
        (*resolved.value)[0].blocking && (*resolved.value)[0].requires_model_instance,
        "feature blocking and object-mode projection"
    );
    require(
        (*resolved.value)[1].footprint_x == 9 && !(*resolved.value)[1].metal_overlay,
        "first matching feature section wins search order"
    );
    require(
        (*resolved.value)[0].object == "tree" && (*resolved.value)[1].seqname.empty(),
        "object-mode features keep the 3DO stem and skip empty seqname"
    );
    feature_assets.files[feature_assets.paths[1]] =
        "[SPRITE]{footprintx=1; footprintz=1; metal=0; indestructible=0; animating=1; "
        "filename=trees; seqname=arm_tree01;}";
    map.features[1].name = "Sprite";
    const auto sprites = sim::map_runtime::resolve_feature_terrain(map, feature_assets);
    require(sprites.ok() && (*sprites.value)[1].filename == "trees", "GAF filename from TDF");
    require((*sprites.value)[1].seqname == "arm_tree01", "GAF seqname from TDF");
    require((*sprites.value)[1].animating, "animating=1 from TDF");
    require(!(*sprites.value)[1].requires_model_instance, "seqname features are not 3DO slots");
    require(!(*sprites.value)[0].no_draw_under_gray, "features draw under gray by default");
    feature_assets.files[feature_assets.paths[1]] =
        "[SPRITE]{footprintx=1; footprintz=1; nodrawundergray=1;}"
        "[dragonsteeth_core]{footprintx=1; footprintz=1;}";
    map.features[0].name = "DragonsTeeth_Core";
    const auto under_gray = sim::map_runtime::resolve_feature_terrain(map, feature_assets);
    require(
        under_gray.ok() && (*under_gray.value)[0].no_draw_under_gray,
        "wall families are forced nodrawundergray"
    );
    require((*under_gray.value)[1].no_draw_under_gray, "nodrawundergray=1 from TDF");
    uint16_t frame = 0, remaining = 4;
    const uint16_t durs[] = {4, 4};
    require(
        sim::map_runtime::step_feature_animation(frame, remaining, true, durs) && remaining == 3,
        "step_feature_animation decrements remaining when >= 2"
    );
    remaining = 1;
    require(
        sim::map_runtime::step_feature_animation(frame, remaining, true, durs) && frame == 1 &&
            remaining == 4,
        "step_feature_animation advances frame when remaining < 2"
    );
    map.attributes[5].feature = 1;
    map.attributes[5].height = 7;
    map.attributes[6].height = 20;
    const sim::map_runtime::FeatureTerrain definitions[] = {
        {},
        {2, 2, 19.75F, true, true, false, false, {}, {}, {}, {}},
    };
    const auto archive = masks();
    const auto result = sim::map_runtime::prepare(map, 7, definitions, archive);
    require(result.ok(), "valid map preparation");
    const auto& prepared = *result.value;
    require(prepared.terrain_values.size() == 12, "terrain cell count");
    require(
        prepared.collision_plots.size() == 12 && prepared.collision_plots[5].low_height == 0 &&
            prepared.collision_plots[5].high_height == 20,
        "plot four-corner height extrema"
    );
    require(
        prepared.collision_plots[5].blocking_feature &&
            prepared.collision_plots[10].blocking_feature,
        "blocking feature footprint projection"
    );
    require(
        prepared.collision_plots[5].metal_feature &&
            prepared.collision_plots[5].indestructible_feature &&
            !prepared.collision_plots[5].geo_feature,
        "metal overlay feature flags from TDF metal/indestructible"
    );
    require(
        prepared.collision_plots[0].metal == 7 && prepared.collision_plots[5].metal == 19,
        "plot metal starts at SurfaceMetal and feature overlay replaces it"
    );
    for (std::size_t i = 0; i < prepared.terrain_values.size(); ++i) {
        const bool overlay = i == 5 || i == 6 || i == 9 || i == 10;
        require(
            prepared.terrain_values[i].movement_cost == (overlay ? 19 : 7),
            "base and feature metal projection"
        );
    }
    require(
        prepared.sight_width == 2 && prepared.sight_height == 1,
        "normal LOS uses half-resolution dimensions"
    );
    require(prepared.sight_masks.size() == 1, "vismask sequence frames");
    const auto& mask = prepared.sight_masks.front();
    require(
        mask.width == 3 && mask.height == 2 && mask.offset_x == -1 && mask.offset_z == 4,
        "vismask frame geometry"
    );
    require(
        mask.transparent == 9 && mask.pixels == std::vector<uint8_t>({9, 1, 9, 2, 3, 4}),
        "vismask transparency and pixels"
    );

    map.attributes[11].feature = 1;
    const auto clipped = sim::map_runtime::prepare(map, -1, definitions, archive);
    require(clipped.ok(), "edge footprint placement is handled");
    require(
        clipped.value->terrain_values[11].movement_cost == 0,
        "out-of-bounds feature placement is rejected before metal overlay"
    );
    require(clipped.value->terrain_values[0].movement_cost == 0, "negative setting begins at zero");

    auto legacy = map;
    legacy.version = formats::tnt::Version::legacy_1020;
    legacy.attributes[0].feature = formats::tnt::value::legacy_feature_sentinel_floor;
    legacy.attributes[0].legacy_metal = 43;
    legacy.attributes[5].legacy_metal = 87;
    const auto legacy_result = sim::map_runtime::prepare(legacy, 222, definitions, archive);
    require(
        legacy_result.ok() && legacy_result.value->terrain_values[0].movement_cost == 43,
        "legacy copies the attribute's extra byte and ignores sentinel feature"
    );
    require(
        legacy_result.value->terrain_values[5].movement_cost == 19,
        "legacy feature metal overlay replaces the attribute's extra byte"
    );

    auto absent = masks("not-vismask");
    require(
        sim::map_runtime::prepare(map, 0, definitions, absent).error->code ==
            sim::map_runtime::ErrorCode::missing_sight_sequence,
        "missing sequence is explicit"
    );
    auto compressed = masks();
    compressed.sequences[0].frames[0].compressed = true;
    require(
        sim::map_runtime::prepare(map, 0, definitions, compressed).error->code ==
            sim::map_runtime::ErrorCode::unsupported_sight_frame,
        "nonlinear vismask frame is rejected"
    );

    auto mismatch = map;
    mismatch.attributes.pop_back();
    require(
        sim::map_runtime::prepare(mismatch, 0, definitions, archive).error->code ==
            sim::map_runtime::ErrorCode::dimension_mismatch,
        "dimension mismatch is rejected"
    );
    require(
        sim::map_runtime::prepare(
            map, 0, std::span<const sim::map_runtime::FeatureTerrain>{}, archive
        )
                .error->code == sim::map_runtime::ErrorCode::feature_table_mismatch,
        "short feature metadata is rejected"
    );

    auto negative_definitions =
        std::vector<sim::map_runtime::FeatureTerrain>(definitions, definitions + 2);
    negative_definitions[1].footprint_x = -1;
    const auto negative = sim::map_runtime::prepare(map, 3, negative_definitions, archive);
    require(
        negative.ok() && negative.value->terrain_values[5].movement_cost == 3,
        "negative signed footprint executes no paint loop"
    );

    auto numeric_definitions =
        std::vector<sim::map_runtime::FeatureTerrain>(definitions, definitions + 2);
    for (const auto metal :
         {0x1p31F, -0x1p31F, 0x1p63F, -0x1p63F, std::numeric_limits<float>::infinity()}) {
        numeric_definitions[1].metal = metal;
        const auto numeric = sim::map_runtime::prepare(map, 3, numeric_definitions, archive);
        require(
            numeric.ok() && numeric.value->terrain_values[5].movement_cost == 0,
            "signed-64 conversion and INT64_MIN keep the low byte without UB"
        );
    }
    numeric_definitions[1].metal = -19.75F;
    const auto negative_metal = sim::map_runtime::prepare(map, 3, numeric_definitions, archive);
    require(
        negative_metal.ok() && negative_metal.value->terrain_values[5].movement_cost == 237,
        "negative metal truncates toward zero and retains low byte"
    );
    numeric_definitions[1].metal = std::numeric_limits<float>::quiet_NaN();
    const auto unordered_metal = sim::map_runtime::prepare(map, 3, numeric_definitions, archive);
    require(
        unordered_metal.ok() && unordered_metal.value->terrain_values[5].movement_cost == 3,
        "unordered floating comparison skips NaN feature overlay"
    );

    auto missing_feature = map;
    missing_feature.features[1].name = "absent";
    require(
        sim::map_runtime::resolve_feature_terrain(missing_feature, feature_assets).error->code ==
            sim::map_runtime::ErrorCode::missing_feature_definition,
        "missing feature definition is explicit"
    );

    auto sentinel = current_map();
    sentinel.attributes[3].feature = 0xfffcU;
    const auto sentinel_result = sim::map_runtime::prepare(sentinel, 0, definitions, archive);
    require(
        sentinel_result.ok() && sentinel_result.value->collision_plots[3].blocking_feature,
        "current-format runtime 0xfffc marker blocks occupancy"
    );
    auto ordered = current_map();
    ordered.attributes[0].feature = 1;
    ordered.attributes[1].feature = 0xfffcU;
    const auto ordered_result = sim::map_runtime::prepare(ordered, 4, definitions, archive);
    require(
        ordered_result.ok() && ordered_result.value->terrain_values[0].movement_cost == 4 &&
            ordered_result.value->collision_plots[1].blocking_feature,
        "special-marker first pass rejects an overlapping ordinary feature"
    );

    require(prepared.collision_plots[5].feature_word == 1, "feature_word_at origin word");
    require(
        prepared.collision_plots[10].feature_word == 0xfffe &&
            prepared.collision_plots[10].feature_back_x == 1 &&
            prepared.collision_plots[10].feature_back_z == 1,
        "feature_word_at continuation stores the footprint offset"
    );
    require(
        sim::map_runtime::feature_word_at(prepared.collision_plots, 5, map.attribute_width) == 1,
        "feature_word_at returns a word at or below 0xfffa"
    );
    require(
        sim::map_runtime::feature_word_at(prepared.collision_plots, 6, map.attribute_width) == 1 &&
            sim::map_runtime::feature_word_at(prepared.collision_plots, 9, map.attribute_width) ==
                1 &&
            sim::map_runtime::feature_word_at(prepared.collision_plots, 10, map.attribute_width) ==
                1,
        "feature_word_at follows one 0xfffe backlink by z * width + x"
    );
    require(
        sim::map_runtime::feature_word_at(prepared.collision_plots, 0, map.attribute_width) ==
            0xffff,
        "feature_word_at empty plot stays 0xffff"
    );
    require(
        sim::map_runtime::feature_word_at(
            sentinel_result.value->collision_plots, 3, sentinel.attribute_width
        ) == 0xffff,
        "feature_word_at turns a non-continuation sentinel into 0xffff"
    );

    sim::map_runtime::CollisionPlot linked[8]{};
    linked[0].feature_word = 42;
    linked[1].feature_word = 0xfffe;
    linked[2].feature_word = 0xfffa;
    linked[3].feature_word = 0xfffd;
    linked[6].feature_word = 0xfffe;
    linked[6].feature_back_x = 2;
    linked[6].feature_back_z = 1;
    require(
        sim::map_runtime::feature_word_at(linked, 2, 4) == 0xfffa, "feature_word_at keeps 0xfffa"
    );
    require(
        sim::map_runtime::feature_word_at(linked, 3, 4) == 0xffff, "feature_word_at rejects 0xfffd"
    );
    require(
        sim::map_runtime::feature_word_at(linked, 6, 4) == 42, "feature_word_at z*width+x origin"
    );
    linked[0].feature_word = 0xfffc;
    linked[1].feature_back_x = 1;
    require(
        sim::map_runtime::feature_word_at(linked, 1, 4) == 0xfffc,
        "feature_word_at returns a redirected sentinel without retesting it"
    );
    linked[1].feature_word = 0xfffe;
    linked[1].feature_back_x = 0;
    linked[1].feature_back_z = 0;
    require(
        sim::map_runtime::feature_word_at(linked, 1, 4) == 0xfffe,
        "feature_word_at zero offset returns the continuation itself"
    );
    linked[6].feature_back_x = 9;
    require(
        sim::map_runtime::feature_word_at(linked, 6, 4) == 0xffff,
        "feature_word_at origin before the span is empty"
    );
    require(
        sim::map_runtime::feature_word_at(linked, 9, 4) == 0xffff,
        "feature_word_at missing plot is empty"
    );

    int16_t cell_x = 0x1111;
    int16_t cell_z = 0x2222;
    int16_t footprint_x = 0x3333;
    int16_t footprint_z = 0x4444;
    const auto unchanged = [&](std::string_view message) {
        require(
            cell_x == 0x1111 && cell_z == 0x2222 && footprint_x == 0x3333 && footprint_z == 0x4444,
            message
        );
    };
    sim::map_runtime::FeatureTerrain table[6]{};
    table[1].footprint_x = 2;
    table[1].footprint_z = 2;
    table[3].footprint_x = 9;
    table[3].footprint_z = 8;
    table[5].footprint_x = 4;
    table[5].footprint_z = -3;
    require(
        sim::map_runtime::feature_at_position(
            2 << 20,
            2 << 20,
            prepared.collision_plots,
            static_cast<int32_t>(map.attribute_width),
            static_cast<int32_t>(map.attribute_height),
            definitions,
            &cell_x,
            &cell_z,
            &footprint_x,
            &footprint_z
        ) == 1 &&
            cell_x == 1 && cell_z == 1 && footprint_x == 2 && footprint_z == 2,
        "feature_at_position follows one placed 0xfffe link to the feature footprint"
    );
    cell_x = 0x1111;
    cell_z = 0x2222;
    footprint_x = 0x3333;
    footprint_z = 0x4444;
    require(
        sim::map_runtime::feature_at_position(
            (1 << 20) + 0xfffff,
            (1 << 20) + 0xfffff,
            prepared.collision_plots,
            static_cast<int32_t>(map.attribute_width),
            static_cast<int32_t>(map.attribute_height),
            definitions,
            &cell_x,
            &cell_z,
            &footprint_x,
            &footprint_z
        ) == 1 &&
            cell_x == 1 && cell_z == 1,
        "feature_at_position keeps the cell for a 16.16 position still inside it"
    );

    sim::map_runtime::CollisionPlot grid[8]{};
    grid[0].feature_word = 5;
    grid[0].feature_footprint_x = 1;
    grid[1].feature_word = 0xfffe;
    grid[1].feature_back_x = 1;
    grid[2].feature_word = 3;
    grid[3].feature_word = 0xfffd;
    grid[4].feature_word = 0xfffe;
    grid[4].feature_back_z = 1;
    grid[6].feature_word = 0xfffe;
    grid[6].feature_back_x = 2;
    grid[6].feature_back_z = 1;
    grid[7].feature_word = 0xfffa;
    cell_x = 0x1111;
    cell_z = 0x2222;
    footprint_x = 0x3333;
    footprint_z = 0x4444;
    require(
        sim::map_runtime::feature_at_position(
            1 << 20, 0, grid, 4, 2, table, &cell_x, &cell_z, &footprint_x, &footprint_z
        ) == 5 &&
            cell_x == 0 && cell_z == 0 && footprint_x == 4 && footprint_z == -3,
        "feature_at_position subtracts feature_back_x from x and reads the feature table"
    );
    require(
        sim::map_runtime::feature_at_position(
            0, 1 << 20, grid, 4, 2, table, &cell_x, &cell_z, &footprint_x, &footprint_z
        ) == 5 &&
            cell_x == 0 && cell_z == 0,
        "feature_at_position subtracts feature_back_z from z"
    );
    require(
        sim::map_runtime::feature_at_position(
            2 << 20, 1 << 20, grid, 4, 2, table, &cell_x, &cell_z, &footprint_x, &footprint_z
        ) == 5 &&
            cell_x == 0 && cell_z == 0,
        "feature_at_position applies both footprint offsets once"
    );
    require(
        sim::map_runtime::feature_at_position(
            2 << 20, 0, grid, 4, 2, table, &cell_x, &cell_z, &footprint_x, &footprint_z
        ) == 3 &&
            cell_x == 2 && cell_z == 0 && footprint_x == 9 && footprint_z == 8,
        "feature_at_position returns a direct word below 0xfffb"
    );
    cell_x = 0x1111;
    cell_z = 0x2222;
    footprint_x = 0x3333;
    footprint_z = 0x4444;
    require(
        sim::map_runtime::feature_at_position(
            3 << 20, 0, grid, 4, 2, table, &cell_x, &cell_z, &footprint_x, &footprint_z
        ) == 0xffff,
        "feature_at_position rejects 0xfffd"
    );
    unchanged("feature_at_position writes nothing for a non-continuation sentinel");
    require(
        sim::map_runtime::feature_at_position(
            0, 0, grid, 4, 2, table, nullptr, nullptr, nullptr, nullptr
        ) == 5,
        "feature_at_position null outs still return the word"
    );
    require(
        sim::map_runtime::feature_at_position(
            3 << 20, 1 << 20, grid, 4, 2, table, &cell_x, &cell_z, &footprint_x, &footprint_z
        ) == 0xfffa &&
            cell_x == 3 && cell_z == 1 && footprint_x == 0x3333 && footprint_z == 0x4444,
        "feature_at_position keeps 0xfffa and skips a missing feature footprint"
    );
    cell_x = 0x1111;
    cell_z = 0x2222;
    footprint_x = 0x3333;
    footprint_z = 0x4444;
    grid[0].feature_word = 0xfffc;
    require(
        sim::map_runtime::feature_at_position(
            1 << 20, 0, grid, 4, 2, table, &cell_x, &cell_z, &footprint_x, &footprint_z
        ) == 0xffff,
        "feature_at_position retests a redirected sentinel"
    );
    unchanged("feature_at_position writes nothing when the origin word is not below 0xfffb");
    grid[1].feature_back_x = 0;
    grid[1].feature_back_z = 0;
    grid[1].feature_word = 0xfffe;
    require(
        sim::map_runtime::feature_at_position(
            1 << 20, 0, grid, 4, 2, table, &cell_x, &cell_z, &footprint_x, &footprint_z
        ) == 0xffff,
        "feature_at_position does not return a zero-offset continuation"
    );
    unchanged("feature_at_position writes nothing for a continuation that stays 0xfffe");
    grid[0].feature_word = 0xfffe;
    grid[0].feature_back_x = 1;
    require(
        sim::map_runtime::feature_at_position(
            0, 0, grid, 4, 2, table, &cell_x, &cell_z, &footprint_x, &footprint_z
        ) == 0xffff,
        "feature_at_position origin before the map is empty"
    );
    unchanged("feature_at_position writes nothing when the second lookup has no plot");
    require(
        sim::map_runtime::feature_at_position(
            -1, 0, grid, 4, 2, table, &cell_x, &cell_z, &footprint_x, &footprint_z
        ) == 0xffff &&
            sim::map_runtime::feature_at_position(
                4 << 20, 0, grid, 4, 2, table, &cell_x, &cell_z, &footprint_x, &footprint_z
            ) == 0xffff &&
            sim::map_runtime::feature_at_position(
                0, 2 << 20, grid, 4, 2, table, &cell_x, &cell_z, &footprint_x, &footprint_z
            ) == 0xffff &&
            sim::map_runtime::feature_at_position(
                std::numeric_limits<int32_t>::min(),
                std::numeric_limits<int32_t>::max(),
                grid,
                4,
                2,
                table,
                &cell_x,
                &cell_z,
                &footprint_x,
                &footprint_z
            ) == 0xffff,
        "feature_at_position rejects cells outside the map after the arithmetic shift"
    );
    unchanged("feature_at_position writes nothing for an off-map position");
    require(
        sim::map_runtime::feature_at_position(
            1 << 20,
            0,
            std::span(grid, 1),
            4,
            2,
            table,
            &cell_x,
            &cell_z,
            &footprint_x,
            &footprint_z
        ) == 0xffff,
        "feature_at_position missing plot is empty"
    );
    unchanged("feature_at_position writes nothing when the plot span has no cell");

    struct HeightProbe {
        int32_t x = 0;
        int32_t z = 0;
        int32_t result = 0;
        int calls = 0;
    };

    static HeightProbe probe;
    const auto sample = [](int32_t x, int32_t z) noexcept {
        probe.x = x;
        probe.z = z;
        ++probe.calls;
        return probe.result;
    };
    probe = {};
    probe.result = 7;
    const auto center = sim::map_runtime::feature_center(1, 2, 2, 4, sample);
    require(
        center[0] == 0x200000 && center[1] == 0x70000 && center[2] == 0x400000,
        "feature_center centers footprint (2,4) on cell (1,2) and shifts height 7"
    );
    require(
        probe.calls == 1 && probe.x == center[0] && probe.z == center[2],
        "feature_center passes X and Z to sample_height and does not pass Y"
    );
    probe.result = -1;
    const auto negative_center = sim::map_runtime::feature_center(0, 0, -1, -3, sample);
    require(
        negative_center[0] == -524288 && negative_center[1] == -65536 &&
            negative_center[2] == -1572864,
        "feature_center sign-extends a negative footprint and height -1"
    );
    probe.result = 0x12345;
    const auto shifted = sim::map_runtime::feature_center(0, 0, 0, 0, sample);
    require(
        shifted[0] == 0 && shifted[2] == 0 && shifted[1] == static_cast<int32_t>(0x23450000u),
        "feature_center shifts the height sample left 16 in 32 bits"
    );
    const auto wrapped = sim::map_runtime::feature_center(32767, -32768, 32767, -32768, sample);
    require(
        wrapped[0] == -1572864 && wrapped[2] == 0,
        "feature_center wraps the 32-bit left shift of 19"
    );
    const auto edge = sim::map_runtime::feature_center(-32768, 32767, 1, 1, sample);
    require(
        edge[0] == 524288 && edge[2] == -524288,
        "feature_center int16 extremes still use a 32-bit shift"
    );
    require(probe.calls == 5, "feature_center calls sample_height once per point");
}
