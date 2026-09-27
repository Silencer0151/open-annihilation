// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace oa::formats::ota {
namespace limit {
inline constexpr std::size_t input_bytes = 4U * 1024U * 1024U;
inline constexpr std::size_t sections = 65536;
inline constexpr std::size_t nesting = 32;
inline constexpr std::size_t value_bytes = 4096;
inline constexpr std::size_t start_positions = 64;
} // namespace limit

// Multiplayer schema selection scans these OTA schema types in order. Section
// and start-position names are matched case-insensitively; the prefix length
// is the suffix start.
inline constexpr std::string_view schema_section_prefix = "Schema ";
inline constexpr std::string_view start_position_prefix = "StartPos";
inline constexpr std::string_view multiplayer_schema_types[] = {
    "Network 1",
    "Network 2",
    "Network 3",
    "Network 4",
};

struct StartPosition {
    int32_t index{}; // StartPos1 is runtime index zero.
    int16_t x{};
    int16_t z{};
};

struct Schema {
    int32_t number{};
    std::string type;
    std::vector<StartPosition> start_positions;
};

struct MapMetadata {
    std::string mission_name;
    std::string mission_description;
    std::string planet;
    std::string memory_requirement;
    std::string map_size;
    std::string permitted_player_counts;
    std::vector<Schema> schemas;
};
enum class ErrorCode {
    none,
    input_limit,
    malformed,
    nesting_limit,
    section_limit,
    value_limit,
    start_position_limit,
    number_out_of_range
};

struct Error {
    ErrorCode code{};
    std::size_t offset{};
    std::string message;
};

struct ParseResult {
    std::optional<MapMetadata> metadata;
    std::optional<Error> error;

    /// Returns whether metadata was parsed.
    [[nodiscard]] bool ok() const noexcept { return metadata.has_value(); }
};

/// Parses an OTA map description.
///
/// Reads the GlobalHeader display fields and every canonically named
/// "Schema N" section with its Type and StartPos records. StartPos suffixes
/// become zero-based indices; a record without a numeric suffix takes the
/// next implicit number. Input, sections, nesting, values and start records
/// are bounded.
///
/// @param text the whole OTA file
/// @return the metadata, or the first error and its offset
[[nodiscard]] ParseResult parse(std::string_view text);
/// Picks the multiplayer schema for a player count.
///
/// Types "Network 1" to "Network 4" are considered in order, each scanning
/// Schema 0, 1, ... until the first missing number. A schema whose
/// start-position count equals player_count is taken; until one is found, a
/// schema with strictly more start positions than the current choice
/// replaces it.
///
/// @param map parsed metadata
/// @param player_count players in the game; 0 takes the last schema with any start positions
/// @return the chosen schema, or null when none has start positions
/// @quirk A later exact match replaces an earlier one.
[[nodiscard]] const Schema* select_multiplayer_schema(const MapMetadata& map, int32_t player_count);
} // namespace oa::formats::ota
