// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The ModProfile account: what a save written under a mod's profile records
// and reads back through a bank image, and that a save without one reads as
// none.

#include "check.hpp"

#include "oa/data/persist/save_profile.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

using namespace oa::data::persist;

namespace {

/// Serializes a bank and reads the image into a fresh bank.
///
/// @param bank the bank written
/// @param[out] read the bank read back; reset first
/// @return true when the image wrote and read
bool round_trip(const Bank& bank, Bank& read) {
    ByteImage image{};
    const bool written = bank_write_image(&bank, "Total Annihilation Saved Game", true, &image);
    bool ok = written && bank_read_image(&read, image.data, image.size, nullptr, nullptr, nullptr);
    byte_image_free(&image);
    return ok;
}

void profile_round_trips() {
    Bank bank;
    bank_init(&bank);
    CHECK(bank_reset(&bank));
    bank_open_account(&bank, "Summary");
    bank_set_int(&bank, "Game Time", 30);
    const SavedProfile profile{"a-mod", "1.2", 1, std::string(64, 'a'), std::string(64, 'b')};
    const std::array<uint8_t, 3> reuse{1, 2, 3};
    const std::array<SavedRuleState, 2> tables{{{"reuse", reuse}, {"empty", {}}}};
    CHECK(save_write_profile(&bank, profile, tables));

    Bank read;
    bank_init(&read);
    CHECK(round_trip(bank, read));
    const auto saved = save_read_profile(&read);
    CHECK(saved.has_value());
    if (saved) {
        CHECK(saved->id == "a-mod" && saved->version == "1.2" && saved->catalogue == 1);
        CHECK(saved->sim_hash == profile.sim_hash && saved->full_hash == profile.full_hash);
    }
    const auto bytes = save_read_rule_state(&read, "reuse");
    CHECK(bytes.has_value() && *bytes == std::vector<uint8_t>(reuse.begin(), reuse.end()));
    // A table of no bytes leaves no blob, and reads as none: its state stays as built.
    CHECK(!save_read_rule_state(&read, "empty").has_value());
    CHECK(!save_read_rule_state(&read, "facing").has_value());
    bank_destroy(&read);
    bank_destroy(&bank);
}

void save_without_profile_has_none() {
    Bank bank;
    bank_init(&bank);
    CHECK(bank_reset(&bank));
    bank_open_account(&bank, "Summary");
    bank_set_int(&bank, "Game Time", 30);
    Bank read;
    bank_init(&read);
    CHECK(round_trip(bank, read));
    CHECK(!save_read_profile(&read).has_value());
    bank_destroy(&read);
    bank_destroy(&bank);
}

} // namespace

int main() {
    profile_round_trips();
    save_without_profile_has_none();
    return test::finish("persist-profile");
}
