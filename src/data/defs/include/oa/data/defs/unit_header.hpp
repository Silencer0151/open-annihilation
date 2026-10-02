// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The unit-table header pass: identity, costs, content hashes and the
// availability verdict each FBI gives its record before the full load.
#pragma once

#include "oa/core/game_state.h"
#include "oa/core/unit_def.h"
#include "oa/data/defs/files.hpp"
#include "oa/formats/tdf.hpp"

#include <cstdint>

namespace oa::data::defs {

// The WEAPONS\*.TDF documents whose section hashes feed UnitDef.weapon_checksum.
struct WeaponTdfSet {
    formats::tdf::Document* documents{};
    uint32_t count{};    // documents loaded
    uint32_t capacity{}; // files listed
};

/// Empties a weapon document set without freeing anything.
///
/// @param[out] set set to reset; its previous storage is not released
void weapon_tdf_set_init(WeaponTdfSet* set) noexcept;

/// Loads every WEAPONS\*.TDF into a document set.
///
/// The set is freed first. Files are listed in VFS order (at most 4096) and
/// each is read through the variant directory when it has the file; a file
/// that fails to load, or is loose while `archive_only` is set, gives its
/// slot to the next file.
///
/// @param files file boundary
/// @param variant game-data variant suffix; null or empty for none
/// @param archive_only true to skip files that do not come from an archive
/// @param[in,out] set set to refill
/// @return false when the listing or an allocation fails; an empty directory succeeds
bool load_weapon_tdf_set(
    const Files* files, const char* variant, bool archive_only, WeaponTdfSet* set
) noexcept;

/// Frees every document of a weapon set and empties it.
///
/// @param[in,out] set set to release
void weapon_tdf_set_free(WeaponTdfSet* set) noexcept;

/// Returns the body hash of the first section with a name across a weapon set.
///
/// @param set loaded weapon set; its documents' cursors are moved
/// @param name weapon section name; may be null
/// @return the section's body hash; 0 for a null or empty name or when no
///     document has the section
[[nodiscard]] uint32_t weapon_tdf_hash(const WeaponTdfSet* set, const char* name) noexcept;

// Checks the running build and install make of each unit.
struct UnitHeaderSources {
    const char* language{}; // localized-key prefix ("" for none)
    const WeaponTdfSet* weapons{};
    int8_t build_major{}; // the running build's major version, 3 in 3.1c
    int8_t build_minor{}; // its minor version, 1 in 3.1c
    bool archive_only{};  // loose unit files are refused
    bool disc_mismatch{}; // the game disc failed its check
};

// The Copyright value a unit needs, any year: the 60-character notice the
// game's own units carry, with the year as four zero digits, and a
// terminating NUL.
inline constexpr char unit_copyright_chars[] = {
    0x43, 0x6f, 0x70, 0x79, 0x72, 0x69, 0x67, 0x68, 0x74, 0x20, 0x30, 0x30, 0x30, 0x30, 0x20, 0x48,
    0x75, 0x6d, 0x6f, 0x6e, 0x67, 0x6f, 0x75, 0x73, 0x20, 0x45, 0x6e, 0x74, 0x65, 0x72, 0x74, 0x61,
    0x69, 0x6e, 0x6d, 0x65, 0x6e, 0x74, 0x2e, 0x20, 0x41, 0x6c, 0x6c, 0x20, 0x72, 0x69, 0x67, 0x68,
    0x74, 0x73, 0x20, 0x72, 0x65, 0x73, 0x65, 0x72, 0x76, 0x65, 0x64, 0x2e, 0x00
};
inline constexpr const char* unit_copyright = unit_copyright_chars;
inline constexpr const char* unit_copyright_default = "(no Copyright line)";

/// Reads one units\*.FBI into a record's header fields and sets or clears its catalog bit.
///
/// Stores the FBI byte hash, the localized name, unitname, side, ai_weight,
/// ai_limit, objectname (unitname when missing), the build costs, the
/// norestrict and wacky bits, and XORs the five weapon section hashes into
/// weapon_checksum. The unit is available when its Version (major.minor, the
/// minor digit from the first decimal) is not above the running build; a
/// loose file under `archive_only`, a failed disc check or a Copyright
/// other than unit_copyright (year ignored) clears the bit and marks the unit
/// refused. player_limit becomes -1. 3.1c also let a units\<name>.OVR bank
/// override the byte hash; that bank is not read here.
///
/// @param files file boundary
/// @param path FBI file path
/// @param[in,out] unit record to fill; untouched on failure
/// @param sources language, weapon set, running build and disc checks
/// @param[out] refused set to true when the archive, disc or copyright check
///     rejected the unit; never cleared
/// @return false when the file is missing, empty or unparsable, or has no
///     UNITINFO section
bool load_unit_header(
    const Files* files,
    const char* path,
    UnitDef& unit,
    const UnitHeaderSources& sources,
    bool* refused
) noexcept;

// The whole units\*.FBI header pass that fills Game.unit_defs.
struct UnitHeaderLoader {
    void* context{};
    void (*load)(void* context, Game* game) = nullptr;
};

/// Reloads the unit header table while none is loaded or a full unit load
/// has marked it stale, then clears the mark.
///
/// @param[in,out] game game whose unit_defs, unit_def_count and unit_defs_stale are used
/// @param loader header pass to run; a null callback only clears the mark
void reload_unit_headers(Game* game, const UnitHeaderLoader& loader) noexcept;

} // namespace oa::data::defs
