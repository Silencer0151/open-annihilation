// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The mission tag table: every unit mission kind (Stop, Move_Ground,
// VTOL_Patrol, ...) registered from static blocks and kept sorted by name, so
// a mission's index is its position in case-insensitive name order. The
// debug overlay and the self-destruct hotkey look missions up by name.
#pragma once

#include <cstddef>
#include <cstdint>

namespace oa::ui::console {

// One mission kind as the tag table registers it: the status text shown while
// it runs, the word after it, its attribute bytes and its name. The mission's
// handlers are left out: nothing here runs a mission.
struct MissionTagRecord {
    const char* status_text{}; // shown while the mission runs
    // Set per mission in the static tables; the engine never reads it.
    uint32_t word_after_status_text{};
    // attributes[1] to [4] hold the mission's descriptor word, little-endian,
    // as match_runtime's mission_descriptor_table lists it.
    uint8_t attributes[5]{};
    const char* name{};
};

/// The status text of the building missions (MobileBuild, HelpBuild,
/// BuildingBuild, BuildWeapon and their aircraft forms).
inline constexpr const char* kNanolathingStatus = "Nanolathing";
/// The status text of the Paralyze mission.
inline constexpr const char* kParalyzedStatus = "Paralyzed";

// Bit of attributes[3]: the mission runs on the secondary order queue.
inline constexpr uint8_t kMissionAttributeSecondaryQueue = 0x04;
inline constexpr size_t kMissionSecondaryAttribute = 3;

// Mission indices are bytes.
inline constexpr uint32_t kMissionTagCapacity = 256;

struct MissionTagTable {
    MissionTagRecord records[kMissionTagCapacity];
    uint32_t count{};
};

/// Orders two records by name, ignoring ASCII case.
///
/// @param a First record.
/// @param b Second record.
/// @return Whether a's name sorts before b's.
[[nodiscard]] bool mission_tag_less(const MissionTagRecord& a, const MissionTagRecord& b) noexcept;

/// Tests whether a record's name sorts before a key, ignoring ASCII case (the lookup's order).
///
/// @param record Record whose name is compared.
/// @param name Key being looked up.
/// @return Whether the record's name sorts before `name`.
[[nodiscard]] bool mission_tag_name_less(const MissionTagRecord& record, const char* name) noexcept;

/// Sorts records by name with mission_tag_less.
///
/// A median-of-three quicksort leaves runs of at most 16 records, which a final
/// insertion sort orders; ranges of 16 or fewer are insertion-sorted directly.
/// The sort is not stable.
///
/// @param[in,out] first First record of the range.
/// @param last One past the last record.
void mission_tags_sort(MissionTagRecord* first, MissionTagRecord* last) noexcept;

/// Appends a block of records to the table and re-sorts the whole table by name.
///
/// @param[in,out] table Table the records join.
/// @param block Records to append.
/// @param count Number of records in `block`.
/// @return False, with the table unchanged, if the block does not fit in
///         kMissionTagCapacity.
bool mission_tags_insert(
    MissionTagTable* table, const MissionTagRecord* block, uint32_t count
) noexcept;

/// Finds the first record whose name is not below the key (binary search).
///
/// @param first First record of a range sorted by name.
/// @param last One past the last record.
/// @param name Key, compared ignoring ASCII case.
/// @return The first record whose name does not sort before `name`, or `last`.
[[nodiscard]] const MissionTagRecord* mission_tags_lower_bound(
    const MissionTagRecord* first, const MissionTagRecord* last, const char* name
) noexcept;

/// Looks a mission up by name.
///
/// @param table Sorted table.
/// @param name Mission name, compared ignoring ASCII case.
/// @return Index of the record named exactly, or 0 (the unnamed Ready record)
///         when absent.
[[nodiscard]] uint8_t mission_tags_find(const MissionTagTable* table, const char* name) noexcept;

/// Returns the record at a mission index.
///
/// @param table Sorted table.
/// @param index Mission index.
/// @return The record, or null when `index` is not below the table's count.
[[nodiscard]] const MissionTagRecord*
mission_tags_entry(const MissionTagTable* table, uint8_t index) noexcept;

/// Registers the ground-unit mission block (Standby, Move_Ground, Patrol, ...).
///
/// @param[in,out] table Table the block joins.
/// @return False if the block does not fit.
bool register_ground_missions(MissionTagTable* table) noexcept;

/// Registers the aircraft mission block (VTOL_Standby, AirStrike, ...).
///
/// @param[in,out] table Table the block joins.
/// @return False if the block does not fit.
bool register_air_missions(MissionTagTable* table) noexcept;

/// Registers the four static blocks in 3.1c's startup order: Ready, ground, air, then orders and unit states.
///
/// Every block is attempted even after one fails.
///
/// @param[in,out] table Table the blocks join; an empty table ends with the
///                      registered mission table in name order.
/// @return False if any block did not fit.
bool mission_tags_register_static(MissionTagTable* table) noexcept;

} // namespace oa::ui::console
