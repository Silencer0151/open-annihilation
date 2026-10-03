// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Rule keys: unit-file and weapon-file keys that 3.1c does not read and a
// mod profile binds to a rule meaning (its data-keys block). Each key is read
// only while a profile binds it, under the name the profile gives it, matched
// without case like every TDF key; an unbound key is ignored, as in 3.1c. What
// a key holds goes into the unit type's or weapon's own rules record
// (match_rules::UnitTypeRules, WeaponTypeRules), which the match keeps and
// the rules that use it read.
#pragma once

#include "oa/data/defs/files.hpp"
#include "oa/data/match_rules.hpp"
#include "oa/formats/tdf.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace oa::data::defs {

/// The highest kill count a veterancy threshold may name.
inline constexpr uint32_t highest_veterancy_threshold = 65535;
/// The highest kills-per-step a veterancy accuracy rate may name.
inline constexpr uint32_t highest_veterancy_accuracy_rate = 65535;

/// The unit-file keys a profile binds, by meaning: each is the key's name in
/// the unit files, or null or empty when nothing is bound to the meaning.
struct UnitDataKeys {
    const char* veterancy_thresholds{};    ///< kill counts per veterancy level
    const char* veterancy_accuracy_rate{}; ///< kills per step of spread reduction
    const char* build_facings{};           ///< facings a building may be placed in

    /// Tells whether any key is bound.
    ///
    /// @return true when at least one name is set
    [[nodiscard]] bool any() const noexcept;
};

/// The weapon-file keys a profile binds, by meaning (see UnitDataKeys).
struct WeaponDataKeys {
    const char* not_to_air{};        ///< airborne targets refused
    const char* surface_fire{};      ///< water weapons fire at the surface
    const char* not_to_underwater{}; ///< submerged targets refused by water weapons
    const char* no_map_alert{};      ///< ownerless zero-damage shots stay quiet

    /// Tells whether any key is bound.
    ///
    /// @return true when at least one name is set
    [[nodiscard]] bool any() const noexcept;
};

/// The most characters one preview key keeps; a longer value is cut.
inline constexpr size_t preview_text_capacity = 256;

/// The build-cursor preview keys of one unit type, as their files give them.
/// They change only what the player sees while placing a building.
struct UnitPreviewKeys {
    /// Pieces the preview draws, as the file lists them; empty for all.
    std::array<char, preview_text_capacity> pieces{};
    /// Pieces drawn in each facing: south, east, north, west.
    std::array<std::array<char, preview_text_capacity>, 4> pieces_by_facing{};
    /// A model drawn in place of the unit's own; empty for none.
    std::array<char, preview_text_capacity> object{};
    bool face_opponent{}; ///< the preview turns toward the nearest enemy start
};

/// The unit-file preview keys a profile binds (see UnitDataKeys). The
/// by-facing name is a stem: the keys read are the stem followed by S, E, N
/// and W.
struct UnitPreviewDataKeys {
    const char* pieces{};
    const char* pieces_by_facing{};
    const char* object{};
    const char* face_opponent{};

    /// Tells whether any key is bound.
    ///
    /// @return true when at least one name is set
    [[nodiscard]] bool any() const noexcept;
};

/// Why a bound key's value could not be used.
enum class RuleKeyProblem : uint8_t {
    none,
    not_a_number,    ///< a list item or value is not a whole number
    too_many_values, ///< more thresholds than match_rules::max_veterancy_thresholds
    out_of_range,    ///< a number past its highest value
    not_ascending,   ///< a threshold below the one before it
    unknown_facing,  ///< a facing letter other than S, E, N or W, or none at all
};

/// Describes a problem in words.
///
/// @param problem the problem
/// @return a short lower-case phrase; "no problem" for none
[[nodiscard]] const char* rule_key_problem_text(RuleKeyProblem problem) noexcept;

/// The keys of one file whose values could not be used. Each is treated as
/// missing, so the profile's default applies to the type.
struct RuleKeyIssues {
    /// The problem with each unit key, by meaning: thresholds, accuracy rate, facings.
    RuleKeyProblem veterancy_thresholds{};
    RuleKeyProblem veterancy_accuracy_rate{};
    RuleKeyProblem build_facings{};

    /// Tells whether any key had a problem.
    ///
    /// @return true when one is not none
    [[nodiscard]] bool any() const noexcept;
};

/// Reads a unit type's bound rule keys from its UNITINFO block.
///
/// VeterancyThresholds-style lists are whole numbers separated by spaces,
/// tabs or commas; an empty list is a missing key. Facings are letters from
/// S, E, N and W in either case; a letter written twice counts once. A value
/// that cannot be used leaves its field as for a missing key and is reported.
///
/// @param unit_info the file's UNITINFO block, or null
/// @param keys the names the profile binds
/// @param[out] data the type's record, reset first
/// @return the keys that could not be used
RuleKeyIssues read_unit_rule_keys(
    const formats::tdf::Block* unit_info, const UnitDataKeys& keys, match_rules::UnitTypeRules& data
) noexcept;

/// Reads a weapon's bound rule keys from its section, replacing every field
/// of the record: each is the low bit of the key's integer, false when
/// missing.
///
/// @param section the weapon's section
/// @param keys the names the profile binds
/// @param[out] data the weapon's record
void read_weapon_rule_keys(
    const formats::tdf::Block* section,
    const WeaponDataKeys& keys,
    match_rules::WeaponTypeRules& data
) noexcept;

/// Reads a unit type's bound preview keys from its UNITINFO block.
///
/// @param unit_info the file's UNITINFO block, or null
/// @param keys the names the profile binds
/// @param[out] data the type's preview keys, reset first
void read_unit_preview_keys(
    const formats::tdf::Block* unit_info, const UnitPreviewDataKeys& keys, UnitPreviewKeys& data
) noexcept;

/// Reads a unit file and its bound rule and preview keys.
///
/// @param files file boundary
/// @param path the FBI file's path
/// @param keys the rule keys the profile binds
/// @param preview_keys the preview keys the profile binds
/// @param[out] data the type's rule record, reset first
/// @param[out] preview the type's preview keys, reset first; may be null
/// @param[out] issues the rule keys that could not be used; may be null
/// @return false when the file is missing, malformed or has no UNITINFO block
bool load_unit_rule_keys(
    const Files* files,
    const char* path,
    const UnitDataKeys& keys,
    const UnitPreviewDataKeys& preview_keys,
    match_rules::UnitTypeRules& data,
    UnitPreviewKeys* preview,
    RuleKeyIssues* issues
) noexcept;

} // namespace oa::data::defs
