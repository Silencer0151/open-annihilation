// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// The match's FeatureDef table (Game.feature_defs / feature_def_count) built
// from the features/**/*.tdf documents: the map's own features in TNT order,
// then whatever unit corpses and featuredead/reclamate/burnt links pull in.

#include "oa/core/feature_def.h"
#include "oa/sim/map_runtime.hpp"
#include "oa/formats/tdf.hpp"
#include "oa/formats/tnt.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace oa::sim::map_runtime {

// FeatureDef index meaning "none" (dead_feature and the plot feature words).
inline constexpr uint16_t no_feature_index = 0xffff;
// FeatureDef.animation_file for a sprite feature sharing an earlier entry's GAF.
inline constexpr std::string_view reused_animation_name = "reuse";

// Asset references the definition loader stores into a FeatureDef. A null
// entry stores 0, which the feature code treats as "no sequence/object".
struct FeatureDefHost {
    void* context{};
    // Archive ref for anims/<gaf_name>.gaf.
    oa_ref32 (*load_animation)(void* context, const char* gaf_name){};
    // Sequence ref within a loaded archive; one_shot marks a die/burn/reclaim
    // sequence that must not loop.
    oa_ref32 (*find_sequence)(
        void* context, oa_ref32 animation, const char* sequence, bool one_shot
    ){};
    // Model ref for objects3d/<object_name>.
    oa_ref32 (*load_object)(void* context, const char* object_name){};
    // WeaponDef ref by name, 0 when unknown.
    oa_ref32 (*find_weapon)(void* context, const char* weapon_name){};
    // Frame count, repeat flag and duration of one frame of a sequence, for
    // the looping cursor an animating sprite starts at load time.
    void (*sequence_frame)(
        void* context,
        oa_ref32 sequence,
        uint16_t frame,
        uint16_t* frame_count,
        uint8_t* repeat,
        uint16_t* duration
    ){};
};

struct FeatureDefTable {
    std::vector<FeatureDef> defs;
};

struct FeatureIndexResult {
    uint16_t index = no_feature_index;
    std::optional<Error> error;

    /// Returns whether the lookup or load succeeded.
    [[nodiscard]] bool ok() const noexcept { return !error.has_value(); }
};

/// Finds the first top-level section named `name` across the documents, case-insensitively.
///
/// @param documents feature documents in listing order
/// @param name feature name to look up
/// @return the section, or null when no document has it
[[nodiscard]] const formats::tdf::Block*
find_feature_section(std::span<const formats::tdf::OwnedDocument> documents, std::string_view name);

/// Finds the table index of the definition named `name`, case-insensitively.
///
/// @param defs feature definition table
/// @param name feature name to look up
/// @return the index, or no_feature_index when absent
[[nodiscard]] uint16_t find_feature_index(std::span<const FeatureDef> defs, std::string_view name);

/// Finds the table index of the definition named `name`, case-insensitively.
///
/// @param table feature definition table
/// @param name feature name to look up
/// @return the index, or no_feature_index when absent
[[nodiscard]] inline uint16_t
find_feature_index(const FeatureDefTable& table, std::string_view name) {
    return find_feature_index(std::span<const FeatureDef>(table.defs), name);
}

/// Appends the definition named `name` from its TDF section.
///
/// The record is named after the requested spelling. Its sprite archive,
/// sequences, 3DO object and burn weapon are resolved through `host`; a sprite
/// whose GAF an earlier entry already loaded is recorded as "reuse" and resolves
/// its sequences through that archive. An animating feature starts its looping
/// cursor (animation_cursor) and shadow cursor (shadow_cursor) on frame 0. The
/// dead, burnt and reclamate links are left at no_feature_index for
/// load_feature_links. A name without a section is returned as an error; 3.1c
/// stops the game there. The TDF sparktime, in seconds, is stored in
/// spark_time as ticks: times 30, truncated toward zero, with the low 16 bits
/// kept, so 5 stores 150 and 2.9 stores 87.
///
/// @param[in,out] table table to append to
/// @param documents feature documents in listing order
/// @param name feature name to load
/// @param host asset resolver; null or null entries store 0 refs
/// @return the new index, ErrorCode::missing_feature_definition for an unknown name or ErrorCode::feature_table_mismatch for a full table
[[nodiscard]] FeatureIndexResult load_feature_def(
    FeatureDefTable& table,
    std::span<const formats::tdf::OwnedDocument> documents,
    std::string_view name,
    const FeatureDefHost* host
);

/// Returns the index of an existing definition, loading it when the table lacks it.
///
/// @param[in,out] table table to search and append to
/// @param documents feature documents in listing order
/// @param name feature name
/// @param host asset resolver for a new definition
/// @return the index, or the load_feature_def error
[[nodiscard]] FeatureIndexResult find_or_load_feature(
    FeatureDefTable& table,
    std::span<const formats::tdf::OwnedDocument> documents,
    std::string_view name,
    const FeatureDefHost* host
);

/// Empties the table and loads every TNT feature in map order.
///
/// The map's plot feature words then index the table directly.
///
/// @param[out] table table to rebuild
/// @param map parsed TNT whose feature names are loaded
/// @param documents feature documents in listing order
/// @param host asset resolver
/// @return the first load error, or nullopt
[[nodiscard]] std::optional<Error> init_feature_table(
    FeatureDefTable& table,
    const formats::tnt::Map& map,
    std::span<const formats::tdf::OwnedDocument> documents,
    const FeatureDefHost* host
);

/// Resolves featuredead, featurereclamate and featureburnt for every definition.
///
/// Definitions the table lacks are appended and linked in turn.
///
/// @param[in,out] table table whose links are filled
/// @param documents feature documents in listing order
/// @param host asset resolver for appended definitions
/// @return the first missing section or load error, or nullopt
[[nodiscard]] std::optional<Error> load_feature_links(
    FeatureDefTable& table,
    std::span<const formats::tdf::OwnedDocument> documents,
    const FeatureDefHost* host
);

/// Returns the featureburnt link (FeatureDef.burnt_feature).
///
/// @param def feature definition
/// @return the burnt feature's index, or no_feature_index
[[nodiscard]] uint16_t burnt_feature(const FeatureDef& def) noexcept;
/// Returns the featurereclamate link (FeatureDef.reclamate_feature).
///
/// @param def feature definition
/// @return the reclamate feature's index, or no_feature_index
[[nodiscard]] uint16_t reclamate_feature(const FeatureDef& def) noexcept;

} // namespace oa::sim::map_runtime
