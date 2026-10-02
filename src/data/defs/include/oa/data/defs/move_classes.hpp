// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Movement classes from GAMEDATA\MOVEINFO.TDF.
#pragma once

#include "oa/core/move_class.h"
#include "oa/data/defs/files.hpp"
#include "oa/formats/tdf.hpp"

#include <cstdint>

namespace oa::data::defs {

inline constexpr std::size_t move_class_name_capacity = 100;

// The fixed CLASS0..CLASS31 table. MoveClass.name holds slot+1 for a
// populated slot and 0 for an empty one; the text lives in names[slot].
struct MoveClassTable {
    MoveClass classes[OA_MOVE_CLASS_COUNT];
    char names[OA_MOVE_CLASS_COUNT][move_class_name_capacity]{};
};

/// Resets a movement class to the defaults a CLASSn section is read over.
///
/// Everything is zeroed except the four slope limits (255) and the water
/// depth limits (+10000 maximum, -10000 minimum).
///
/// @param[out] move_class class to reset
void move_class_init_defaults(MoveClass* move_class) noexcept;

/// Reads a CLASSn section's limits over the current values.
///
/// FootPrintX/FootPrintZ default to 0; the depth and slope keys default to
/// the current values, and badslope/badwaterslope to half the maximum just
/// read. Afterwards maxslope is clamped to maxwaterslope, badslope to maxslope
/// and badwaterslope to maxwaterslope. Values are truncated to the record's
/// field widths.
///
/// @param[in,out] move_class class to update; name and grid are left alone
/// @param section CLASSn block of MOVEINFO.TDF
void move_class_load(MoveClass* move_class, const formats::tdf::Block* section) noexcept;

/// Resets all 32 slots to the defaults and marks them empty.
///
/// @param[out] table table to reset
void move_class_table_init(MoveClassTable* table) noexcept;

/// Fills slots from the CLASS0..CLASS31 sections of a parsed MOVEINFO document.
///
/// Each section found stores its name (up to 99 characters), marks the slot
/// populated and reads its limits; missing sections leave their slots as they were.
///
/// @param[in,out] table table to fill, normally fresh from move_class_table_init
/// @param[in,out] moveinfo parsed MOVEINFO.TDF; its cursor is moved
void move_class_table_load(MoveClassTable* table, formats::tdf::Document* moveinfo) noexcept;

/// Resets the table and loads GAMEDATA/MOVEINFO.TDF, preferring the variant directory.
///
/// @param files file boundary
/// @param[out] table table to fill; reset even when loading fails
/// @param variant game-data variant suffix; null or empty for none
/// @param[out] error parse error details when the file is malformed; may be null
/// @return false when the file is missing, empty or fails to parse
[[nodiscard]] bool load_move_classes(
    const Files* files, MoveClassTable* table, const char* variant, formats::tdf::ParseError* error
) noexcept;

/// Finds a populated movement class by name.
///
/// @param table loaded table
/// @param name class name, matched case-insensitively
/// @return slot index of the first match, or -1
[[nodiscard]] int move_class_find(const MoveClassTable* table, const char* name) noexcept;

} // namespace oa::data::defs
