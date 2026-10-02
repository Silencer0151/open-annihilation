// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Side identity and HUD layout from GAMEDATA\SIDEDATA.TDF.
#pragma once

#include "oa/core/side.h"
#include "oa/data/defs/files.hpp"
#include "oa/formats/tdf.hpp"

#include <cstddef>
#include <cstdint>

namespace oa::data::defs {

inline constexpr std::size_t side_error_capacity = 256;

struct SideTable {
    Side sides[OA_SIDE_COUNT];
    uint32_t count{};
    char error[side_error_capacity]{}; // set when a required rectangle is missing
};

// Resolves a font name to a handle stored in Side.font; may be null.
struct SideFontResolver {
    void* context{};
    oa_ref32 (*font)(void* context, const char* name) = nullptr;
};

/// Returns the slot index a side record carries in Side.side_index.
///
/// @param side side record
/// @return index of the side in its table
[[nodiscard]] uint32_t side_index(const Side* side) noexcept;

/// Reads x1/y1/x2/y2 of a named sub-section of the side section at the document cursor.
///
/// The cursor is restored afterwards. Missing keys read as 0.
///
/// @param[in,out] sidedata parsed SIDEDATA.TDF with the cursor on a SIDEn section
/// @param[out] rect rectangle to fill; untouched when the sub-section is missing
/// @param section sub-section name, e.g. "LOGO"
/// @param side_name side name used in the error message
/// @param[out] error receives "Section [<section>] is missing from
///     GAMEDATA/SIDEDATA.TDF for the <name> side" when the sub-section is
///     missing; may be null
/// @param error_capacity size of `error` in bytes
/// @return true when the sub-section exists
bool side_load_rect(
    formats::tdf::Document* sidedata,
    Rect32* rect,
    const char* section,
    const char* side_name,
    char* error,
    std::size_t error_capacity
) noexcept;

/// Loads SIDE0, SIDE1, ... until one is missing or the table is full.
///
/// Each side reads name, nameprefix, commander, font (through `fonts`),
/// energycolor, metalcolor, its HUD rectangles in the game's read order and
/// RELOAD1..RELOAD3. The table is zeroed first and every visited slot gets its index.
///
/// @param[in,out] sidedata parsed SIDEDATA.TDF; its cursor is moved
/// @param[out] table table to fill
/// @param fonts font resolver; null, or a null callback, leaves Side.font 0
/// @return false when a side lacks one of its HUD rectangles (fatal in 3.1c);
///     table->error names the first missing one and count is not set
bool side_table_load(
    formats::tdf::Document* sidedata, SideTable* table, const SideFontResolver* fonts
) noexcept;

/// Loads GAMEDATA/SIDEDATA.TDF, preferring the variant directory.
///
/// A missing or unparsable file still runs side_table_load on the empty
/// document, leaving zero sides, as 3.1c does.
///
/// @param files file boundary
/// @param[out] table table to fill
/// @param variant game-data variant suffix; null or empty for none
/// @param fonts font resolver; may be null
/// @return true when the file loaded and every side was complete
bool load_side_data(
    const Files* files, SideTable* table, const char* variant, const SideFontResolver* fonts
) noexcept;

} // namespace oa::data::defs
