// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace oa::data::mission_types {
// Index of the empty name, which every unknown name also maps to.
inline constexpr uint8_t unknown_mission = 0;
// Number of unit-order names, the empty name at index 0 included.
inline constexpr std::size_t registered_count = 68;
// mission_block_count counts a leading run of root sections with this prefix.
inline constexpr std::string_view mission_section_prefix = "MISSION";
/// Returns every unit-order name, sorted without regard to ASCII case.
///
/// An order kind is an index into this list; this API does not carry out
/// orders.
///
/// @return the 68 names; index 0 is the empty name
std::span<const std::string_view> registered_names() noexcept;
// Flag of the orders that saved orders leave unnumbered: SelfRepair and
// Standby_Mine. A saved order that stores its kind as a position rather than a
// name counts only the orders without this flag; SavedOrder.mission_byte keeps
// the order's flag byte.
inline constexpr uint8_t unnumbered_order = 0x01;
/// Returns the flag byte of an order kind.
///
/// @param index order kind, an index into registered_names()
/// @return unnumbered_order for Standby_Mine and SelfRepair, else 0; 0 past the list
uint8_t mission_flags(uint8_t index) noexcept;
/// Returns the order kind of a name.
///
/// A lower-bound search of the sorted list, then a match without regard to
/// ASCII case.
///
/// @param name order name; a NUL ends it
/// @return the index, or unknown_mission for a name not in the list
uint8_t index_for_name(std::string_view name) noexcept;
/// Counts a campaign's mission blocks.
///
/// Counts the leading run of root sections MISSION0, MISSION1, ...; each
/// search restarts from the first section, and names compare with ASCII case
/// folding.
///
/// @param campaign_file campaign file name; a NUL ends it
/// @param root_sections names of the root sections of the campaign document
/// @return the number of consecutive MISSIONn sections, or 0 when campaign_file is empty
int32_t mission_block_count(
    std::string_view campaign_file, std::span<const std::string_view> root_sections
) noexcept;
} // namespace oa::data::mission_types
