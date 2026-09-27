// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Operations over the packed UnitDef table (slot 0 reserved, types 1..count-1).
#pragma once

#include "oa/core/unit_def.h"

#include <cstdint>

namespace oa::data::defs {

// UnitDef.flags bit that keeps a parsed unit in the catalog; cleared records
// are compacted away before sorting.
inline constexpr uint32_t unit_def_flag_available = 1u << 23;
// Bits of UnitDef.abilities copied by record assignment; the rest stay with the slot.
inline constexpr uint32_t unit_def_abilities_assigned_mask = 0x007fffffu;
inline constexpr uint32_t unit_def_sort_run = 16; // insertion-sort threshold

using UnitDefLess = bool (*)(const UnitDef* left, const UnitDef* right);
using UnitDefPredicate = bool (*)(const UnitDef* record);

/// Assigns one unit record to another member by member.
///
/// Every byte before `abilities` is copied; of the abilities word
/// only the bits in unit_def_abilities_assigned_mask transfer, the others stay
/// with the target. Source and target may overlap.
///
/// @param[in,out] target record to overwrite
/// @param source record to copy
void unit_def_assign(UnitDef* target, const UnitDef* source) noexcept;

/// Tests whether a parsed unit lacks unit_def_flag_available.
///
/// @param record unit record
/// @return true when the record is to be dropped from the catalog
[[nodiscard]] bool unit_def_is_unavailable(const UnitDef* record) noexcept;

/// Orders unit records by unit name, case-insensitively.
///
/// @param left first record
/// @param right second record
/// @return true when `left`'s name sorts before `right`'s
[[nodiscard]] bool unit_def_name_less(const UnitDef* left, const UnitDef* right) noexcept;

/// Compacts the records a predicate does not remove to the front of a range.
///
/// Kept records are moved with unit_def_assign, keeping their order; the
/// slots past the returned end hold stale records.
///
/// @param[in,out] first start of the range
/// @param last end of the range (exclusive)
/// @param remove predicate that is true for records to drop
/// @return the new end of the kept records
UnitDef* unit_defs_remove_if(UnitDef* first, UnitDef* last, UnitDefPredicate remove) noexcept;

/// Sorts a range of unit records.
///
/// Quicksort (median-of-three pivot) runs down to runs of 16 records; a
/// guarded insertion sort then orders the first run and unguarded insertion
/// the rest. Records move with unit_def_assign, so the sort is not stable.
///
/// @param[in,out] first start of the range
/// @param last end of the range (exclusive)
/// @param less strict ordering of two records
void unit_defs_sort(UnitDef* first, UnitDef* last, UnitDefLess less) noexcept;

/// Compacts, sorts and numbers the unit catalog after the unit files are parsed.
///
/// Unavailable records are compacted out of table[1..count), the rest are
/// sorted by unit name and every slot, slot 0 included, gets its index as
/// type_id.
///
/// @param[in,out] table unit table with slot 0 reserved
/// @param count number of slots in `table`, including slot 0
/// @return the new count including slot 0; 0 when `count` is 0
uint32_t unit_defs_finalize_catalog(UnitDef* table, uint32_t count) noexcept;

/// Finds a unit record by name in the sorted catalog.
///
/// @param table sorted unit table with slot 0 reserved
/// @param count number of slots in `table`, including slot 0
/// @param name unit name, matched case-insensitively
/// @return the record, or null when no unit has that name
[[nodiscard]] const UnitDef*
unit_defs_find(const UnitDef* table, uint32_t count, const char* name) noexcept;

/// Finds a unit's type id by name in the sorted catalog.
///
/// @param table sorted unit table with slot 0 reserved
/// @param count number of slots in `table`, including slot 0
/// @param name unit name, matched case-insensitively
/// @return the record's type_id, or 0 when no unit has that name
[[nodiscard]] uint16_t
unit_defs_type_id(const UnitDef* table, uint32_t count, const char* name) noexcept;

} // namespace oa::data::defs
