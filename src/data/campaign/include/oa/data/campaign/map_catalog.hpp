// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Multiplayer map catalogue: the eligible-map name list the skirmish and
// multiplayer map pickers show.
#pragma once

#include "oa/data/campaign/campaign_file.hpp"

#include <cstdint>

namespace oa::data::campaign {

// Cursor animations shown around the map scan.
inline constexpr uint32_t kMapScanBusyCursor = 20;
inline constexpr uint32_t kMapScanIdleCursor = 19;

// The packed eligible-map list and its bookkeeping.
struct MapList {
    char* names{};      // NUL-separated names followed by a closing NUL
    int32_t complete{}; // built by a full scan, so callers always get copies
    int32_t bytes{};    // block size including the closing NUL
    int32_t count{};
};

// Caller hook for the cursor animation shown around the scan. In 3.1c the
// multiplayer messages that arrive during a full scan are also handled after
// each file; the scan here does not handle them.
struct MapScanHost {
    void* context{};
    void (*set_cursor)(void* context, uint32_t animation){};
};

/// Builds or hands out the list of maps with a multiplayer schema.
///
/// The first call scans Maps/*.ota and caches the maps' localised names; every call then
/// hands the list to `out`. A map that several archives hold is listed once, as the copy
/// it is read from.
///
/// @param[in,out] list cached list and bookkeeping
/// @param files file services
/// @param host cursor animation hook
/// @param[out] out receives a copy, or the cached block itself when `take` is set and the
///        list is partial; the caller frees it with std::free
/// @param first_only stop the scan at the first eligible map
/// @param take hand over the cached block instead of a copy when the list is partial
/// @return the number of names
int32_t map_build_multiplayer_list(
    MapList& list,
    const CampaignFiles& files,
    const MapScanHost& host,
    char** out,
    bool first_only,
    bool take
);

/// Frees the cached list block and clears its bookkeeping.
///
/// @param[in,out] list cached list
/// @param map_context map context object to free as well; may be null
void map_clear_list_cache(MapList& list, CampaignFile* map_context) noexcept;

} // namespace oa::data::campaign
