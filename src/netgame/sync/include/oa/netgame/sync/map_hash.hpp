// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// The lobby's map check: the content hash of the selected map that the
// players compare before a game starts.

#include "oa/data/campaign/campaign_file.hpp"

#include <cstdint>

namespace oa::netgame::sync {

// TNT header fields the content hash reads.
inline constexpr uint32_t kTntHashVersion = 0x2000;
inline constexpr uint32_t kTntHeaderBytes = 0x40;
inline constexpr uint32_t kTntAttributeBytes = 4; // per 16-pixel cell
inline constexpr uint32_t kTntFeatureRecordBytes = 0x84;

// Content hashes already computed this run, keyed by TNT path.
struct MapHashEntry {
    char path[data::campaign::kCampaignPathBytes]{};
    uint32_t hash{};
};

struct MapHashCache {
    MapHashEntry* entries{};
    uint32_t count{};
    uint32_t capacity{};
};

/// Computes the selected map's content hash for the lobby's map check.
///
/// Hashes the TNT header, cell attributes and feature names, caches the
/// result in the map context and in the run's cache, and combines it with
/// the OTA GlobalHeader hash. Offsets and sizes past the end of the file are
/// rejected rather than hashed.
///
/// @param[in,out] map_context The selected map; its content hash is filled on first use.
/// @param files File access used to read the TNT.
/// @param[in,out] cache Hashes computed this run, keyed by TNT path; grows as needed.
/// @return header hash ^ content hash; 0 when the TNT cannot be read or is not version 0x2000.
uint32_t map_compute_content_hash(
    data::campaign::CampaignFile& map_context,
    const data::campaign::CampaignFiles& files,
    MapHashCache& cache
);

/// Frees the content-hash cache.
///
/// @param[in,out] cache Cache to free; left empty.
void map_destroy_hash_cache(MapHashCache& cache) noexcept;

} // namespace oa::netgame::sync
