// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/data/persist/save_profile.hpp"

#include <string>
#include <tuple>

namespace oa::data::persist {

bool save_write_profile(
    Bank* bank, const SavedProfile& profile, std::span<const SavedRuleState> tables
) {
    std::ignore = bank_open_account(bank, profile_key::account);
    bool written = bank_set_text(bank, profile_key::id, profile.id.c_str()) &&
                   bank_set_text(bank, profile_key::version, profile.version.c_str()) &&
                   bank_set_int(bank, profile_key::catalogue, profile.catalogue) &&
                   bank_set_text(bank, profile_key::sim_hash, profile.sim_hash.c_str()) &&
                   bank_set_text(bank, profile_key::full_hash, profile.full_hash.c_str());
    for (const SavedRuleState& table : tables) {
        const std::string name{table.name};
        if (!written || bank_find_blob_name(bank, name.c_str(), true) < 0)
            return false;
        bank_open_blob_name(bank, name.c_str());
        written =
            table.bytes.empty() ||
            bank_blob_write(bank, table.bytes.data(), static_cast<uint32_t>(table.bytes.size())) ==
                table.bytes.size();
    }
    return written;
}

std::optional<SavedProfile> save_read_profile(Bank* bank) {
    // Opening an absent account creates an empty one, which reads as none.
    if (!bank_open_account(bank, profile_key::account) || !bank_has_field(bank, profile_key::id))
        return std::nullopt;
    SavedProfile profile{};
    profile.id = bank_get_text(bank, profile_key::id, "");
    profile.version = bank_get_text(bank, profile_key::version, "");
    profile.catalogue = bank_get_int(bank, profile_key::catalogue, 0);
    profile.sim_hash = bank_get_text(bank, profile_key::sim_hash, "");
    profile.full_hash = bank_get_text(bank, profile_key::full_hash, "");
    return profile;
}

std::optional<std::vector<uint8_t>> save_read_rule_state(Bank* bank, const char* name) {
    if (bank_find_blob_name(bank, name, false) < 0)
        return std::nullopt;
    bank_open_blob_name(bank, name);
    std::vector<uint8_t> bytes(static_cast<size_t>(bank_blob_size(bank)));
    bank_blob_seek(bank, 0);
    if (bank_blob_read(bank, bytes.data(), static_cast<uint32_t>(bytes.size())) != bytes.size())
        return std::nullopt;
    return bytes;
}

} // namespace oa::data::persist
