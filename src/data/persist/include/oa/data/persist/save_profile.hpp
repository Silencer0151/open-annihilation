// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The ModProfile account of a savegame written while a mod's profile is
// played: which profile the save belongs to, and the state the profile's
// rules keep outside 3.1c's records, one blob a table. A save written
// without a profile has no such account, so its bytes are 3.1c's.
#pragma once

#include "oa/data/persist/hapibank.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace oa::data::persist {

// Account and field names of the ModProfile account.
namespace profile_key {
inline constexpr const char* account = "ModProfile";
inline constexpr const char* id = "Id";
inline constexpr const char* version = "Version";
inline constexpr const char* catalogue = "Catalogue";
inline constexpr const char* sim_hash = "Sim Hash";   // lower-case hexadecimal
inline constexpr const char* full_hash = "Full Hash"; // lower-case hexadecimal
} // namespace profile_key

/// The profile a save was written under.
struct SavedProfile {
    std::string id{};        ///< the profile's id
    std::string version{};   ///< the mod's version
    int32_t catalogue{};     ///< the registry catalogue the profile was resolved with
    std::string sim_hash{};  ///< the sim hash, 64 lower-case hexadecimal digits
    std::string full_hash{}; ///< the full hash, 64 lower-case hexadecimal digits
};

/// One table of rule state as a save keeps it: a blob of the ModProfile
/// account named after the table.
struct SavedRuleState {
    std::string_view name{};          ///< the table's name
    std::span<const uint8_t> bytes{}; ///< its bytes
};

/// Writes the ModProfile account: the profile's fields, then one blob per table.
///
/// @param[in,out] bank the save being written; the account is left open
/// @param profile the profile played
/// @param tables the rule state, in the match's order
/// @return false when a field or a blob cannot be written
bool save_write_profile(
    Bank* bank, const SavedProfile& profile, std::span<const SavedRuleState> tables
);

/// Reads the ModProfile account.
///
/// @param[in,out] bank a save read whole; the account is left open when present
/// @return the profile, or nullopt for a save written without one
[[nodiscard]] std::optional<SavedProfile> save_read_profile(Bank* bank);

/// Reads one table's bytes from the ModProfile account.
///
/// @param[in,out] bank a save with the ModProfile account open
/// @param name the table's name
/// @return its bytes, or nullopt when the save holds no blob of that name
[[nodiscard]] std::optional<std::vector<uint8_t>>
save_read_rule_state(Bank* bank, const char* name);

} // namespace oa::data::persist
