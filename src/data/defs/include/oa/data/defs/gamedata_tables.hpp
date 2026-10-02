// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Line-of-sight ray tables from GAMEDATA\LOS.TDF.
#pragma once

#include "oa/data/defs/files.hpp"
#include "oa/formats/tdf.hpp"

#include <cstdint>

namespace oa::data::defs {

// Receives the tables in file order. resize comes first with TABLEINFO's
// numtables; table then gets each TABLE<index + 1> the file has, with its
// numlines (never negative). A false return stops the load.
struct GamedataTableSink {
    void* context{};
    bool (*resize)(void* context, int16_t table_count) = nullptr;
    bool (*table)(
        void* context, int16_t index, const formats::tdf::Block* section, int16_t line_count
    ) = nullptr;
};

/// Loads GAMEDATA/LOS.TDF into a sink, preferring the variant directory.
///
/// TABLEINFO's numtables sizes the sink (negative counts as 0), then each
/// TABLE<n> is handed over in order. A file without TABLEINFO loads no tables.
///
/// @param files file boundary
/// @param variant game-data variant suffix; null or empty for none
/// @param sink receiver of the table count and each table
/// @return false when the file is missing or a sink call fails
bool load_gamedata_tables(
    const Files* files, const char* variant, const GamedataTableSink& sink
) noexcept;

/// Hands one TABLE<index + 1> section of a loaded document to a sink.
///
/// @param[in,out] document parsed LOS.TDF; its cursor is moved
/// @param index zero-based table index
/// @param sink receiver of the table
/// @return the sink's result, or true when the file lacks the section; a
///     negative numlines is passed to the sink as 0
bool load_gamedata_table_lines(
    formats::tdf::Document* document, int16_t index, const GamedataTableSink& sink
) noexcept;

} // namespace oa::data::defs
