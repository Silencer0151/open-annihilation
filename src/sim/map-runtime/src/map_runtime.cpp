// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/map_runtime.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <limits>
#include <cerrno>
#include <cstdlib>
#include <string_view>
#include <utility>
#include <cstdint>

namespace oa::sim::map_runtime {
namespace {
bool equal(std::string_view a, std::string_view b) {
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    return true;
}

Result failure(ErrorCode code, std::string message) {
    return {std::nullopt, Error{code, std::move(message)}};
}

FeatureResult feature_failure(ErrorCode code, std::string message) {
    return {std::nullopt, Error{code, std::move(message)}};
}

int32_t
integer(const data::unit_definitions::TdfSection& section, std::string_view key, bool& valid) {
    const auto* source = section.find(key);
    if (!source || source->empty())
        return 0;
    const char* begin = source->c_str();
    char* end = nullptr;
    errno = 0;
    const auto parsed = std::strtol(begin, &end, 10);
    if (end == begin || errno == ERANGE || parsed < std::numeric_limits<int32_t>::min() ||
        parsed > std::numeric_limits<int32_t>::max()) {
        valid = false;
        return 0;
    }
    return static_cast<int32_t>(parsed);
}

uint8_t metal_byte(float value) {
    // The game truncates to signed 64 bits; overflow and nonfinite values produce
    // INT64_MIN, whose low byte is zero.
    constexpr double signed_64_limit = 0x1p63;
    if (!std::isfinite(value) || static_cast<double>(value) >= signed_64_limit ||
        static_cast<double>(value) < -signed_64_limit)
        return 0;
    return static_cast<uint8_t>(static_cast<int64_t>(value));
}

// The wall features that are nodrawundergray whatever their TDF says.
bool forced_no_draw_under_gray(std::string_view name) {
    for (const auto wall :
         {"DragonsTeeth", "DragonsTeeth_Core", "Fortification", "Fortification_Core"})
        if (equal(name, wall))
            return true;
    return false;
}

bool parse_feature_section(
    const data::unit_definitions::TdfSection& found, std::string_view name, FeatureTerrain& out
) {
    bool valid = true;
    const auto x = integer(found, "footprintx", valid), z = integer(found, "footprintz", valid);
    const auto metal = integer(found, "metal", valid),
               indestructible = integer(found, "indestructible", valid);
    const auto blocking = integer(found, "blocking", valid);
    const auto reclaimable = integer(found, "reclaimable", valid);
    const auto* object = found.find("object");
    const auto* filename = found.find("filename");
    const auto* seqname = found.find("seqname");
    const auto geothermal = integer(found, "geothermal", valid);
    const auto animating = integer(found, "animating", valid);
    const auto height = integer(found, "height", valid);
    const auto no_draw_under_gray = integer(found, "nodrawundergray", valid);
    if (!valid)
        return false;
    out = {
        static_cast<int16_t>(x),
        static_cast<int16_t>(z),
        static_cast<float>(static_cast<uint32_t>(metal) & 0xffffU),
        (static_cast<uint32_t>(indestructible) & 1U) != 0,
        (static_cast<uint32_t>(blocking) & 1U) != 0,
        (static_cast<uint32_t>(reclaimable) & 1U) != 0,
        object != nullptr && !object->empty(),
        object ? *object : std::string{},
        filename ? *filename : std::string{},
        seqname ? *seqname : std::string{},
        (static_cast<uint32_t>(geothermal) & 1U) != 0,
        (static_cast<uint32_t>(animating) & 1U) != 0,
        static_cast<uint8_t>(height),
        (static_cast<uint32_t>(no_draw_under_gray) & 1U) != 0 || forced_no_draw_under_gray(name)
    };
    return true;
}

/// Paints each metal-overlay feature's metal over the plot metal of its footprint.
///
/// Walks the plots row by row; the metal is truncated to a byte and footprint
/// cells off the map are skipped.
///
/// @param[in,out] out prepared map whose terrain values and collision plots are painted
/// @param map parsed TNT giving the attribute dimensions
/// @param features resolved feature terrain indexed by feature word
/// @quirk NaN metal is skipped, as in 3.1c.
void paint_feature_metal(
    PreparedMap& out, const formats::tnt::Map& map, std::span<const FeatureTerrain> features
) {
    for (std::size_t index = 0; index < out.collision_plots.size(); ++index) {
        const auto feature_index = out.collision_plots[index].feature_word;
        if (feature_index >= formats::tnt::value::current_feature_sentinel_floor ||
            feature_index >= features.size())
            continue;
        const auto& feature = features[feature_index];
        if (feature.metal == 0.0F || std::isnan(feature.metal) || !feature.metal_overlay)
            continue;
        const auto origin_x = static_cast<int32_t>(index % map.attribute_width);
        const auto origin_z = static_cast<int32_t>(index / map.attribute_width);
        const auto value = metal_byte(feature.metal);
        for (int32_t z = 0; z < feature.footprint_z; ++z)
            for (int32_t x = 0; x < feature.footprint_x; ++x) {
                const auto px = origin_x + x, pz = origin_z + z;
                if (px < 0 || pz < 0 || px >= static_cast<int32_t>(map.attribute_width) ||
                    pz >= static_cast<int32_t>(map.attribute_height))
                    continue;
                const auto cell = static_cast<std::size_t>(pz) * map.attribute_width +
                                  static_cast<std::size_t>(px);
                out.terrain_values[cell].movement_cost = value;
                if (cell < out.collision_plots.size())
                    out.collision_plots[cell].metal = value;
            }
    }
}

} // namespace

data::unit_definitions::Result<std::vector<data::unit_definitions::TdfDocument>>
load_feature_documents(const FeatureAssetReader& assets) {
    auto paths = assets.list_effective_recursive("features", ".tdf");
    if (!paths)
        return {{}, {data::unit_definitions::ErrorCode::io, 0, paths.error.message}};
    std::vector<data::unit_definitions::TdfDocument> documents;
    documents.reserve(paths.value.size());
    for (const auto& path : paths.value) {
        auto source = assets.read(path);
        if (!source)
            return {{}, {data::unit_definitions::ErrorCode::io, 0, source.error.message}};
        auto parsed = data::unit_definitions::parse_tdf(source.value);
        if (!parsed)
            return {{}, parsed.error};
        documents.push_back(std::move(parsed.value));
    }
    return {std::move(documents), {}};
}

FeatureResult
resolve_feature_terrain(const formats::tnt::Map& map, const FeatureAssetReader& assets) {
    auto documents = load_feature_documents(assets);
    if (!documents)
        return feature_failure(ErrorCode::asset_error, documents.error.message);
    return resolve_feature_terrain(map, documents.value);
}

FeatureResult resolve_feature_terrain(
    const formats::tnt::Map& map, std::span<const data::unit_definitions::TdfDocument> documents
) {
    std::vector<FeatureTerrain> result;
    result.reserve(map.features.size());
    for (const auto& feature : map.features) {
        const data::unit_definitions::TdfSection* found = nullptr;
        for (const auto& document : documents) {
            for (const auto& section : document.sections)
                if (equal(section.name, feature.name)) {
                    found = &section;
                    break;
                }
            if (found)
                break;
        }
        if (!found)
            return feature_failure(
                ErrorCode::missing_feature_definition,
                "feature definition is missing: " + feature.name
            );
        FeatureTerrain terrain;
        if (!parse_feature_section(*found, feature.name, terrain))
            return feature_failure(
                ErrorCode::invalid_feature_number,
                "feature has an invalid numeric field: " + feature.name
            );
        result.push_back(std::move(terrain));
    }
    return {std::move(result), std::nullopt};
}

FeatureCatalogResult load_feature_catalog(const FeatureAssetReader& assets) {
    auto documents = load_feature_documents(assets);
    if (!documents)
        return {std::nullopt, Error{ErrorCode::asset_error, documents.error.message}};
    return load_feature_catalog(documents.value);
}

FeatureCatalogResult
load_feature_catalog(std::span<const data::unit_definitions::TdfDocument> documents) {
    std::vector<NamedFeature> catalog;
    for (const auto& document : documents) {
        for (const auto& section : document.sections) {
            bool exists = false;
            for (const auto& prior : catalog)
                if (equal(prior.name, section.name)) {
                    exists = true;
                    break;
                }
            if (exists)
                continue;
            FeatureTerrain terrain;
            if (!parse_feature_section(section, section.name, terrain))
                return {
                    std::nullopt,
                    Error{
                        ErrorCode::invalid_feature_number,
                        "feature has an invalid numeric field: " + section.name
                    }
                };
            catalog.push_back({section.name, std::move(terrain)});
        }
    }
    return {std::move(catalog), std::nullopt};
}

Result prepare(
    const formats::tnt::Map& map,
    int32_t configured_map_metal,
    std::span<const FeatureTerrain> features,
    const formats::gaf::Archive& vismasks
) {
    const auto cells = static_cast<std::size_t>(map.attribute_width) * map.attribute_height;
    if (map.attributes.size() != cells)
        return failure(ErrorCode::dimension_mismatch, "TNT attributes do not match dimensions");
    if (features.size() < map.features.size())
        return failure(
            ErrorCode::feature_table_mismatch,
            "runtime feature table is shorter than TNT feature table"
        );
    PreparedMap out;
    out.terrain_values.resize(cells);
    out.collision_plots.resize(cells);
    for (uint32_t z = 0; z < map.attribute_height; ++z)
        for (uint32_t x = 0; x < map.attribute_width; ++x) {
            auto low = map.attributes[static_cast<std::size_t>(z) * map.attribute_width + x].height,
                 high = low;
            const auto include = [&](uint32_t px, uint32_t pz) {
                const auto h =
                    map.attributes[static_cast<std::size_t>(pz) * map.attribute_width + px].height;
                low = std::min(low, h);
                high = std::max(high, h);
            };
            if (x + 1 < map.attribute_width)
                include(x + 1, z);
            if (z + 1 < map.attribute_height)
                include(x, z + 1);
            if (x + 1 < map.attribute_width && z + 1 < map.attribute_height)
                include(x + 1, z + 1);
            auto& plot = out.collision_plots[static_cast<std::size_t>(z) * map.attribute_width + x];
            plot.low_height = low;
            plot.high_height = high;
        }

    struct RuntimeFeaturePlot {
        uint16_t feature = 0xffffU;
        uint8_t back_x{}, back_z{};
        bool model_instance{};
    };

    std::vector<RuntimeFeaturePlot> runtime_features(cells);
    std::size_t model_instances = 0;
    const auto remove_feature = [&](std::size_t at) {
        auto origin = at;
        const auto record = runtime_features[at];
        if (record.feature == 0xfffeU)
            origin -= static_cast<std::size_t>(record.back_z) * map.attribute_width + record.back_x;
        const auto existing = runtime_features[origin].feature;
        if (existing >= features.size() || features[existing].metal_overlay)
            return false;
        const auto& old = features[existing];
        const auto ox = origin % map.attribute_width, oz = origin / map.attribute_width;
        if (runtime_features[origin].model_instance)
            --model_instances;
        runtime_features[origin] = {};
        for (int32_t row = 0; row < old.footprint_z; ++row)
            for (int32_t column = 0; column < old.footprint_x; ++column) {
                const auto px = static_cast<int32_t>(ox) + column,
                           pz = static_cast<int32_t>(oz) + row;
                if (px >= 0 && pz >= 0 && px < static_cast<int32_t>(map.attribute_width) &&
                    pz < static_cast<int32_t>(map.attribute_height)) {
                    auto& covered =
                        runtime_features[static_cast<std::size_t>(pz) * map.attribute_width + px];
                    if (covered.feature == 0xfffeU)
                        covered = {};
                }
            }
        return true;
    };
    // Features are placed in row-major order: successful placement,
    // replacement, backlinks, and the explicit 0xFFFC plot marker.
    if (map.version == formats::tnt::Version::total_annihilation)
        for (std::size_t at = 0; at < cells; ++at)
            if (map.attributes[at].feature == 0xfffcU)
                runtime_features[at].feature = 0xfffcU;
    for (std::size_t origin = 0; origin < cells; ++origin) {
        const auto requested = map.attributes[origin].feature;
        if (map.version == formats::tnt::Version::total_annihilation && requested == 0xfffcU)
            continue;
        if (requested >= features.size())
            continue;
        const auto& feature = features[requested];
        const auto ox = static_cast<int32_t>(origin % map.attribute_width),
                   oz = static_cast<int32_t>(origin / map.attribute_width);
        if (ox + feature.footprint_x > static_cast<int32_t>(map.attribute_width) ||
            oz + feature.footprint_z > static_cast<int32_t>(map.attribute_height))
            continue;
        bool accepted = true;
        for (int32_t row = 0; accepted && row < feature.footprint_z; ++row)
            for (int32_t column = 0; column < feature.footprint_x; ++column) {
                const auto at = static_cast<std::size_t>(oz + row) * map.attribute_width +
                                static_cast<std::size_t>(ox + column);
                if (runtime_features[at].feature != 0xffffU && !remove_feature(at)) {
                    accepted = false;
                    break;
                }
            }
        if (!accepted)
            continue;
        if (feature.requires_model_instance && model_instances >= 2048U)
            continue;
        runtime_features[origin].feature = requested;
        runtime_features[origin].model_instance = feature.requires_model_instance;
        if (feature.requires_model_instance)
            ++model_instances;
        if (!feature.object.empty())
            out.placed_features.push_back(
                {feature.object,
                 {},
                 {},
                 ox,
                 oz,
                 feature.footprint_x,
                 feature.footprint_z,
                 map.attributes[origin].height,
                 feature.reclaimable,
                 feature.metal,
                 feature.animating}
            );
        else if (!feature.filename.empty() && !feature.seqname.empty())
            out.placed_features.push_back(
                {{},
                 feature.filename,
                 feature.seqname,
                 ox,
                 oz,
                 feature.footprint_x,
                 feature.footprint_z,
                 map.attributes[origin].height,
                 feature.reclaimable,
                 feature.metal,
                 feature.animating}
            );
        for (int32_t row = 0; row < feature.footprint_z; ++row)
            for (int32_t column = 0; column < feature.footprint_x; ++column)
                if (row != 0 || column != 0) {
                    const auto at = static_cast<std::size_t>(oz + row) * map.attribute_width +
                                    static_cast<std::size_t>(ox + column);
                    runtime_features[at] = {
                        0xfffeU, static_cast<uint8_t>(column), static_cast<uint8_t>(row)
                    };
                }
    }
    for (std::size_t at = 0; at < cells; ++at) {
        const auto record = runtime_features[at];
        out.collision_plots[at].feature_word = record.feature;
        out.collision_plots[at].feature_back_x = record.back_x;
        out.collision_plots[at].feature_back_z = record.back_z;
        if (record.feature == 0xffffU)
            continue;
        if (record.feature >= 0xfffbU && record.feature != 0xfffeU) {
            out.collision_plots[at].blocking_feature = true;
            continue;
        }
        auto origin = at;
        if (record.feature == 0xfffeU)
            origin -= static_cast<std::size_t>(record.back_z) * map.attribute_width + record.back_x;
        const auto feature = runtime_features[origin].feature;
        if (feature < features.size()) {
            out.collision_plots[at].feature_height = features[feature].height;
            out.collision_plots[at].feature_footprint_x = features[feature].footprint_x;
            out.collision_plots[at].feature_footprint_z = features[feature].footprint_z;
            out.collision_plots[at].blocking_feature = features[feature].blocking;
            out.collision_plots[at].metal_feature = features[feature].metal != 0.0F;
            out.collision_plots[at].geo_feature = features[feature].geothermal;
            out.collision_plots[at].indestructible_feature = features[feature].metal_overlay;
        }
    }
    // The configured map metal (scenario SurfaceMetal) applies only to the
    // current 0x2000 format and only when nonnegative. A legacy map starts each
    // plot's metal at its attribute's extra byte instead.
    const auto base =
        map.version == formats::tnt::Version::total_annihilation && configured_map_metal >= 0
            ? static_cast<uint8_t>(configured_map_metal)
            : uint8_t{};
    for (std::size_t index = 0; index < cells; ++index) {
        out.terrain_values[index].movement_cost = map.version == formats::tnt::Version::legacy_1020
                                                      ? map.attributes[index].legacy_metal
                                                      : base;
        // The plot's metal overlay starts there; features paint over it next.
        if (index < out.collision_plots.size())
            out.collision_plots[index].metal = out.terrain_values[index].movement_cost;
    }
    paint_feature_metal(out, map, features);
    out.sight_width = static_cast<int32_t>(map.attribute_width / 2U);
    out.sight_height = static_cast<int32_t>(map.attribute_height / 2U);
    const formats::gaf::Sequence* sequence = nullptr;
    for (const auto& candidate : vismasks.sequences)
        if (equal(candidate.name, sight_mask_sequence)) {
            sequence = &candidate;
            break;
        }
    if (!sequence)
        return failure(ErrorCode::missing_sight_sequence, "VISMASKS.GAF has no vismask sequence");
    if (sequence->frames.empty())
        return failure(ErrorCode::empty_sight_sequence, "vismask sequence has no frames");
    out.sight_masks.reserve(sequence->frames.size());
    for (const auto& frame : sequence->frames) {
        // Line of sight reads each frame's pixels as one linear byte array. The
        // shipped VISMASKS frames are all simple and uncompressed; do not invent a
        // normalized representation for another GAF frame form here.
        const auto pixels = static_cast<std::size_t>(frame.width) * frame.height;
        if (frame.compressed || frame.layer_count != 0 || !frame.layers.empty() ||
            frame.special_render_flag != 0 || frame.pixels.size() != pixels)
            return failure(
                ErrorCode::unsupported_sight_frame, "vismask frame is not simple linear pixel data"
            );
        out.sight_masks.push_back(
            {frame.width,
             frame.height,
             frame.origin_x,
             frame.origin_y,
             frame.transparency_index,
             frame.pixels}
        );
    }
    return {std::move(out), std::nullopt};
}

uint16_t feature_word_at(
    std::span<const CollisionPlot> plots, std::size_t index, uint32_t map_width
) noexcept {
    constexpr uint16_t empty = 0xffffU;
    constexpr uint16_t continuation = 0xfffeU;
    if (index >= plots.size())
        return empty;
    const auto word = plots[index].feature_word;
    if (word < formats::tnt::value::current_feature_sentinel_floor)
        return word;
    if (word != continuation)
        return empty;
    const auto steps = static_cast<uint64_t>(plots[index].feature_back_z) * map_width +
                       plots[index].feature_back_x;
    if (steps > index)
        return empty;
    return plots[index - static_cast<std::size_t>(steps)].feature_word;
}

uint16_t feature_at_position(
    int32_t world_x,
    int32_t world_z,
    std::span<const CollisionPlot> plots,
    int32_t map_width,
    int32_t map_height,
    std::span<const FeatureTerrain> features,
    int16_t* cell_x,
    int16_t* cell_z,
    int16_t* footprint_x,
    int16_t* footprint_z
) noexcept {
    constexpr uint16_t empty = 0xffffU;
    constexpr uint16_t continuation = 0xfffeU;
    const auto plot_at = [&](int16_t x_cell, int16_t z_cell) -> std::optional<std::size_t> {
        const auto x = static_cast<int32_t>(x_cell);
        const auto z = static_cast<int32_t>(z_cell);
        if (x < 0 || z < 0 || x >= map_width || z >= map_height)
            return std::nullopt;
        const auto index =
            static_cast<uint64_t>(z) * static_cast<uint64_t>(map_width) + static_cast<uint64_t>(x);
        if (index >= plots.size())
            return std::nullopt;
        return static_cast<std::size_t>(index);
    };
    auto origin_x = static_cast<int16_t>(world_x >> 20);
    auto origin_z = static_cast<int16_t>(world_z >> 20);
    auto index = plot_at(origin_x, origin_z);
    if (!index)
        return empty;
    if (plots[*index].feature_word == continuation) {
        origin_z =
            static_cast<int16_t>(origin_z - static_cast<uint16_t>(plots[*index].feature_back_z));
        origin_x =
            static_cast<int16_t>(origin_x - static_cast<uint16_t>(plots[*index].feature_back_x));
        index = plot_at(origin_x, origin_z);
        if (!index)
            return empty;
    }
    const auto word = plots[*index].feature_word;
    if (word >= formats::tnt::value::current_feature_sentinel_floor)
        return empty;
    if (cell_x != nullptr)
        *cell_x = origin_x;
    if (cell_z != nullptr)
        *cell_z = origin_z;
    if (word < features.size()) {
        if (footprint_x != nullptr)
            *footprint_x = features[word].footprint_x;
        if (footprint_z != nullptr)
            *footprint_z = features[word].footprint_z;
    }
    return word;
}

std::array<int32_t, 3> feature_center(
    int16_t cell_x,
    int16_t cell_z,
    int16_t footprint_x,
    int16_t footprint_z,
    int32_t (*sample_height)(int32_t world_x, int32_t world_z) noexcept
) noexcept {
    const auto axis = [](int16_t footprint, int16_t cell) noexcept {
        const auto sum = static_cast<int32_t>(footprint) + static_cast<int32_t>(cell) * 2;
        return static_cast<int32_t>(static_cast<uint32_t>(sum) << 19);
    };
    const auto x = axis(footprint_x, cell_x);
    const auto z = axis(footprint_z, cell_z);
    const auto y = static_cast<int32_t>(static_cast<uint32_t>(sample_height(x, z)) << 16);
    return {x, y, z};
}

bool step_feature_animation(
    uint16_t& frame, uint16_t& remaining, bool loop, std::span<const uint16_t> durations
) {
    if (durations.empty())
        return false;
    if (remaining < 2) {
        ++frame;
        if (frame >= durations.size()) {
            if (!loop) {
                frame = static_cast<uint16_t>(durations.size() - 1);
                remaining = 0;
                return false;
            }
            frame = 0;
        }
        remaining = durations[frame];
        return true;
    }
    --remaining;
    return true;
}
} // namespace oa::sim::map_runtime
