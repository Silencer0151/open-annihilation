// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "oa/formats/gaf.hpp"
#include "oa/formats/tnt.hpp"
#include "oa/data/unit_definitions.hpp"
#include "oa/sim/visibility_state.hpp"
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace oa::sim::map_runtime {
inline constexpr std::string_view sight_mask_archive = "anims/vismasks.gaf";
inline constexpr std::string_view sight_mask_sequence = "vismask";

struct FeatureTerrain {
    int16_t footprint_x{}, footprint_z{}; // FeatureDef.footprint_x, FeatureDef.footprint_z
    float metal{};                        // FeatureDef.metal
    bool metal_overlay{};                 // OA_FEATURE_FLAG_INDESTRUCTIBLE
    bool blocking{};                      // OA_FEATURE_FLAG_BLOCKING
    bool reclaimable{};                   // OA_FEATURE_FLAG_RECLAIMABLE
    bool requires_model_instance{};       // OA_FEATURE_FLAG_SPRITE clear: a 3DO object
    std::string object;                   // TDF object= 3DO stem
    std::string filename;                 // TDF filename= GAF in anims/
    std::string seqname;                  // TDF seqname= GAF sequence
    bool geothermal{};                    // TDF geothermal= (OA_FEATURE_FLAG_GEOTHERMAL)
    bool animating{};                     // TDF animating= (OA_FEATURE_FLAG_ANIMATING)
    uint8_t height{};                     // TDF height= (FeatureDef.height)
    // TDF nodrawundergray= (OA_FEATURE_FLAG_NO_DRAW_UNDER_GRAY), forced for the
    // DragonsTeeth and Fortification wall families: drawn only in line of sight.
    bool no_draw_under_gray{};
};

struct PlacedFeature {
    std::string object;
    std::string filename;
    std::string seqname;
    int32_t cell_x{};
    int32_t cell_z{};
    int16_t footprint_x{};
    int16_t footprint_z{};
    uint8_t height{};
    bool reclaimable{};
    float metal{};
    bool animating{};
};

struct CollisionPlot {
    uint8_t high_height{}, low_height{};
    bool blocking_feature{};
    bool metal_feature{};           // TDF metal != 0
    bool geo_feature{};             // TDF geothermal= (building yard test 0x80)
    bool indestructible_feature{};  // TDF indestructible= (building yard test 0x40)
    uint8_t metal{};                // MapPlot.metal, the metal overlay
    uint16_t feature_word = 0xffff; // MapPlot.feature
    // Columns and rows from a continuation cell back to its feature's origin:
    // the high and low bytes of MapPlot.feature_record.
    uint8_t feature_back_x{};
    uint8_t feature_back_z{};
    uint8_t feature_height{}; // FeatureDef.height of the resolved feature
    int16_t feature_footprint_x = 1;
    int16_t feature_footprint_z = 1;
};

struct PreparedMap {
    std::vector<sim::visibility_state::TerrainCell> terrain_values;
    std::vector<CollisionPlot> collision_plots;
    std::vector<sim::visibility_state::SightMask> sight_masks;
    int32_t sight_width{}, sight_height{};
    std::vector<PlacedFeature> placed_features;
};
enum class ErrorCode {
    none,
    dimension_mismatch,
    feature_table_mismatch,
    missing_sight_sequence,
    empty_sight_sequence,
    unsupported_sight_frame,
    asset_error,
    malformed_feature_document,
    missing_feature_definition,
    invalid_feature_number
};

struct Error {
    ErrorCode code{};
    std::string message;
};

struct Result {
    std::optional<PreparedMap> value;
    std::optional<Error> error;

    /// Returns whether the result holds a value.
    [[nodiscard]] bool ok() const noexcept { return value.has_value(); }
};

struct FeatureResult {
    std::optional<std::vector<FeatureTerrain>> value;
    std::optional<Error> error;

    /// Returns whether the result holds a value.
    [[nodiscard]] bool ok() const noexcept { return value.has_value(); }
};

/// Asset access the feature loaders read features/**/*.tdf through.
class FeatureAssetReader {
  public:

    virtual ~FeatureAssetReader() = default;
    /// Lists every effective file under a directory, recursively, in listing order.
    ///
    /// @param directory logical directory, such as "features"
    /// @param extension file extension to keep, such as ".tdf"
    /// @return the logical paths, or an I/O error
    [[nodiscard]] virtual data::unit_definitions::Result<std::vector<std::string>>
    list_effective_recursive(std::string_view directory, std::string_view extension) const = 0;
    /// Reads one logical file.
    ///
    /// @param logical_path path as list_effective_recursive returned it
    /// @return the file's text, or an I/O error
    [[nodiscard]] virtual data::unit_definitions::Result<std::string>
    read(std::string_view logical_path) const = 0;
};

/// Loads and parses every features/**/*.tdf document, in listing order.
///
/// This is the document list feature definition lookups search.
///
/// @param assets asset reader to list and read through
/// @return the parsed documents, or the first listing, read or parse error
[[nodiscard]] data::unit_definitions::Result<std::vector<data::unit_definitions::TdfDocument>>
load_feature_documents(const FeatureAssetReader& assets);

/// Resolves a map's TNT feature names to their terrain fields.
///
/// Loads the feature documents first; see the overload taking documents.
///
/// @param map parsed TNT whose feature names are resolved
/// @param assets asset reader for features/**/*.tdf
/// @return one FeatureTerrain per TNT feature, or an asset or resolution error
[[nodiscard]] FeatureResult
resolve_feature_terrain(const formats::tnt::Map& map, const FeatureAssetReader& assets);
/// Resolves a map's TNT feature names through parsed feature documents.
///
/// Each name is matched case-insensitively against the documents' top-level
/// sections in document order, first match wins, and the fields the metal
/// overlay and plot projection read are parsed from it.
///
/// @param map parsed TNT whose feature names are resolved
/// @param documents feature documents in listing order
/// @return one FeatureTerrain per TNT feature, ErrorCode::missing_feature_definition for an unknown name or ErrorCode::invalid_feature_number for a bad numeric field
[[nodiscard]] FeatureResult resolve_feature_terrain(
    const formats::tnt::Map& map, std::span<const data::unit_definitions::TdfDocument> documents
);

struct NamedFeature {
    std::string name;
    FeatureTerrain terrain;
};

struct FeatureCatalogResult {
    std::optional<std::vector<NamedFeature>> value;
    std::optional<Error> error;

    /// Returns whether the result holds a value.
    [[nodiscard]] bool ok() const noexcept { return value.has_value(); }
};

/// Loads every features/**/*.tdf section as a named feature, first name wins.
///
/// Used by the corpse lookup.
///
/// @param assets asset reader for features/**/*.tdf
/// @return the catalog, or an asset or numeric-field error
[[nodiscard]] FeatureCatalogResult load_feature_catalog(const FeatureAssetReader& assets);
/// Builds the named-feature catalog from parsed feature documents, first name wins.
///
/// @param documents feature documents in listing order
/// @return the catalog, or ErrorCode::invalid_feature_number for a bad numeric field
[[nodiscard]] FeatureCatalogResult
load_feature_catalog(std::span<const data::unit_definitions::TdfDocument> documents);
/// Prepares a parsed map's runtime plots, metal and line-of-sight masks.
///
/// Each plot's high and low height (MapPlot.high_height, MapPlot.low_height)
/// come from its own and its east, south and south-east neighbours' heights;
/// plots of the last row and column use the neighbours that exist, where 3.1c
/// does not compute their heights.
/// Features are placed in row-major order: out-of-map footprints are rejected,
/// removable overlaps are cleared, indestructible overlaps reject the new
/// placement, non-origin cells receive 0xfffe backlinks and model-backed
/// features take one of 2,048 instance slots.
/// Current-format 0xfffc markers are installed first and stay blocking. A plot's
/// metal starts at the configured map metal (current format, when nonnegative)
/// or at its attribute's extra byte (legacy format), then metal-overlay features
/// paint theirs.
/// The normal line-of-sight grid is half the attribute size and its masks come
/// from the vismask sequence.
///
/// @param map parsed TNT
/// @param configured_map_metal the game settings' map metal; negative means zero
/// @param features resolved feature terrain, at least one per TNT feature
/// @param vismasks parsed anims/vismasks.gaf
/// @return the prepared map, or a dimension, feature table or sight-mask error
[[nodiscard]] Result prepare(
    const formats::tnt::Map& map,
    int32_t configured_map_metal,
    std::span<const FeatureTerrain> features,
    const formats::gaf::Archive& vismasks
);

/// Reads a plot's feature word, following a footprint continuation once.
///
/// A word above 0xfffa is the empty word 0xffff unless it is the 0xfffe
/// continuation, which returns the word (z * width + x) plots toward the origin;
/// prepare stores z in feature_back_z and x in feature_back_x.
///
/// @param plots the map's collision plots, row-major
/// @param index row-major plot index
/// @param map_width map width in plots (Game.map_width)
/// @return the feature word, or 0xffff for an index or origin outside `plots`
/// @quirk A redirected word is not tested against 0xfffb.
[[nodiscard]] uint16_t feature_word_at(
    std::span<const CollisionPlot> plots, std::size_t index, uint32_t map_width
) noexcept;

/// Resolves the feature under a signed 16.16 world position.
///
/// X and Z are each shifted right 20 and truncated to int16; a negative cell or
/// one at or past the map width or height has no plot. Plot word 0xfffe
/// subtracts feature_back_z from z and feature_back_x from x and looks up once
/// more. A word below 0xfffb is returned and the origin cell written; the
/// footprint is written only when the word indexes `features`. Any other word,
/// or a missing plot or origin, returns 0xffff and writes nothing.
///
/// @param world_x signed 16.16 world X
/// @param world_z signed 16.16 world Z
/// @param plots the map's collision plots, row-major
/// @param map_width map width in plots
/// @param map_height map height in plots
/// @param features resolved feature terrain indexed by feature word
/// @param[out] cell_x origin cell column; may be null
/// @param[out] cell_z origin cell row; may be null
/// @param[out] footprint_x feature footprint width (FeatureDef.footprint_x); may be null
/// @param[out] footprint_z feature footprint depth (FeatureDef.footprint_z); may be null
/// @return the feature word, or 0xffff for no feature
/// @quirk Unlike feature_word_at, a redirected word is tested again, so a continuation landing on another sentinel yields 0xffff.
[[nodiscard]] uint16_t feature_at_position(
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
) noexcept;

/// Returns the signed 16.16 world position at the centre of a feature footprint.
///
/// Each axis is (footprint + cell * 2) shifted left 19, the 32-bit multiply by
/// 0x80000 with wrap. Y is sample_height(X, Z) shifted left 16.
///
/// @param cell_x origin column, the low int16 of the packed cell word
/// @param cell_z origin row, the high int16 of the packed cell word
/// @param footprint_x signed footprint width (FeatureDef.footprint_x)
/// @param footprint_z signed footprint depth (FeatureDef.footprint_z)
/// @param sample_height terrain height sample (sim::unit_movement::Terrain::height) taking 16.16 X and Z
/// @return {X, Y, Z} in signed 16.16
[[nodiscard]] std::array<int32_t, 3> feature_center(
    int16_t cell_x,
    int16_t cell_z,
    int16_t footprint_x,
    int16_t footprint_z,
    int32_t (*sample_height)(int32_t world_x, int32_t world_z) noexcept
) noexcept;

/// Advances a feature's type-level GAF cursor by one tick.
///
/// The cursor is FeatureDef.animation_cursor. When fewer than two ticks remain
/// the frame advances and its duration is loaded; a looping sequence wraps, any
/// other freezes on its last frame.
///
/// @param[in,out] frame current frame index
/// @param[in,out] remaining ticks left on the current frame
/// @param loop whether the sequence wraps
/// @param durations per-frame durations in animation ticks
/// @return false when the sequence is empty or has frozen on its last frame, otherwise true
bool step_feature_animation(
    uint16_t& frame, uint16_t& remaining, bool loop, std::span<const uint16_t> durations
);
} // namespace oa::sim::map_runtime
