// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "oa/sim/unit_spawn/spawn.hpp"
#include "oa/data/unit_definitions.hpp"
#include "oa/formats/objects3d.hpp"
#include "oa/formats/cob.hpp"
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace oa::sim::unit_spawn {
// File access is the only boundary here. Null means absent; parse/read failures
// must throw and must not fall back to lower-priority assets.
struct AssetReader {
    virtual ~AssetReader() = default;
    /// Reads a game file.
    ///
    /// @param path path inside the game data, such as objects3d/armcom.3DO
    /// @return the bytes, or nullopt when the file is absent; read and decode
    ///         failures throw
    virtual std::optional<std::vector<uint8_t>> read(std::string_view path) = 0;
};

struct RuntimeBindings {
    std::optional<bool>
        resolved_weapon_present; // OA_UNIT_DEF_FLAG_HAS_WEAPONS: a nondefault weapon resolved
    bool enabled{}; // OA_UNIT_DEF_FLAG_AVAILABLE: availability filtering precedes type loading
    int32_t player_limit{-1};
    uint8_t default_mission{}; // UnitDef.default_mission_type, resolved global mission-table index
    // Required when FBI movement_class is nonempty, because the final
    // footprint comes from that movement class, not the raw FBI dimensions.
    std::optional<std::array<int16_t, 2>> movement_footprint;
};

// A loaded unit type. One that did not load holds a load_error, its names and
// the paths it reached, and no type fields, model or script.
struct LoadedType {
    Type type{};
    std::shared_ptr<const formats::objects3d::Model> model;
    std::shared_ptr<const formats::cob::CobProgram> script;
    std::string unit_name, model_path, script_path, load_error; // load_error: empty once loaded
};

/// Loads a unit type's runtime fields, model and script from the game data.
///
/// Uses the base asset paths objects3d/<object>.3DO and scripts/<unit>.COB; mod-prefixed
/// roots are the reader's business. GUI pages are counted from the nonempty
/// guis/<unit>N.GUI files, page 0 first.
///
/// @param definition the unit type's fields, from its loaded UnitDef
/// @param bindings availability, limit, default mission and resolved footprint
/// @param assets game file reader
/// @return the type and shared ownership of its parsed assets; keep it alive while the
///         type is in use. Without resolved weapon presence, without a footprint for
///         a movement class, for an unbounded asset name, a missing or invalid model,
///         an invalid script or more than 4096 GUI pages, the type did not load and
///         its load_error says why
LoadedType load_runtime_type(
    const data::unit_definitions::UnitDefinition& definition,
    const RuntimeBindings& bindings,
    AssetReader& assets
);
/// Finds a unit type by name, case-insensitively.
///
/// Only the first 65536 names, which have a 16-bit index, are searched.
///
/// @param names type names in world type order, including reserved zero
/// @param name name to find
/// @return the index, or 0 when absent
[[nodiscard]] uint16_t
find_type_index(std::span<const std::string> names, std::string_view name) noexcept;
} // namespace oa::sim::unit_spawn
