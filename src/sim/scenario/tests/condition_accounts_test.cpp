// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The savegame accounts of the 18 scenario condition kinds, pinned: each
// registered condition, given distinctive satisfied, celebrated and counter
// values, saves under the account name, field names and values listed below,
// the bank image of all 18 accounts matches its pinned size and 64-bit
// FNV-1a, and loading the image into freshly registered conditions restores
// every saved value.
#include "oa/sim/scenario/condition_persist.hpp"
#include "oa/data/persist/hapibank.hpp"
#include "oa/sim/scenario/state.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>

using namespace oa::sim::scenario;
namespace persist = oa::data::persist;

namespace {

// Map kind of the single-player map list, the only one that saves conditions.
constexpr int32_t campaign_map_kind = 1;
// Any other map kind; its saves hold no conditions.
constexpr int32_t non_campaign_map_kind = 0;
// The values each condition is given before the save: satisfied and
// celebrated step from these by the kind's index; the counters are fixed.
constexpr int32_t first_satisfied = 101;
constexpr int32_t first_celebrated = 201;
constexpr int32_t mobile_units_left = 77;
constexpr int32_t kill_unit_type_left = 31;
constexpr int32_t unit_type_killed_left = 32;
// A value no saved field holds, given to conditions a load must leave alone.
constexpr int32_t unsaved_value = -5;

constexpr uint64_t fnv_basis = 0xcbf29ce484222325ull;
constexpr uint64_t fnv_prime = 0x100000001b3ull;

struct ExpectedField {
    const char* name{};
    int32_t value{};
};

struct ExpectedAccount {
    Kind kind{};
    const char* name{};
    std::array<ExpectedField, 3> fields{};
    size_t field_count{};
};

constexpr std::array<ExpectedAccount, kind_count> expected_accounts{{
    {Kind::kill_enemy_commander,
     "VictoryCondition_KillEnemyCommander",
     {{{"Satisfied", 101}, {"Celebrated", 201}}},
     2},
    {Kind::destroy_all_units,
     "VictoryCondition_DestroyAllUnits",
     {{{"Satisfied", 102}, {"Celebrated", 202}}},
     2},
    {Kind::kill_all_mobile_units,
     "VictoryCondition_KillAllMobileUnits",
     {{{"NumUnits", 77}, {"Satisfied", 103}, {"Celebrated", 203}}},
     3},
    {Kind::build_unit_type,
     "VictoryCondition_BuildUnitType",
     {{{"Satisfied", 104}, {"Celebrated", 204}}},
     2},
    {Kind::capture_unit_type,
     "VictoryCondition_CaptureUnitType",
     {{{"Satisfied", 105}, {"Celebrated", 205}}},
     2},
    {Kind::kill_all_of_type,
     "VictoryCondition_KillAllOfType",
     {{{"Satisfied", 106}, {"Celebrated", 206}}},
     2},
    {Kind::kill_unit_type,
     "VictoryCondition_KillUnitType",
     {{{"NumLeftToKill", 31}, {"Satisfied", 107}, {"Celebrated", 207}}},
     3},
    {Kind::move_unit_to_radius,
     "VictoryCondition_MoveUnitToRadius",
     {{{"Satisfied", 108}, {"Celebrated", 208}}},
     2},
    {Kind::unit_type_passes_x,
     "VictoryCondition_UnitTypePassesX",
     {{{"Satisfied", 109}, {"Celebrated", 209}}},
     2},
    {Kind::unit_type_passes_z,
     "VictoryCondition_UnitTypePassesZ",
     {{{"Satisfied", 110}, {"Celebrated", 210}}},
     2},
    {Kind::victory_timer,
     "VictoryCondition_VictoryTimerRunsOut",
     {{{"Satisfied", 111}, {"Celebrated", 211}}},
     2},
    {Kind::commander_killed,
     "DefeatCondition_CommanderKilled",
     {{{"Satisfied", 112}, {"Celebrated", 212}}},
     2},
    {Kind::all_units_killed,
     "DefeatCondition_AllUnitsKilled",
     {{{"Satisfied", 113}, {"Celebrated", 213}}},
     2},
    {Kind::all_units_killed_of_type,
     "DefeatCondition_AllUnitsKilledOfType",
     {{{"Satisfied", 114}, {"Celebrated", 214}}},
     2},
    {Kind::unit_type_killed,
     "DefeatCondition_UnitTypeKilled",
     {{{"NumLeftToKill", 32}, {"Satisfied", 115}, {"Celebrated", 215}}},
     3},
    {Kind::death_timer,
     "DefeatCondition_DeathTimerRunsOut",
     {{{"Satisfied", 116}, {"Celebrated", 216}}},
     2},
    {Kind::any_unit_passes_x,
     "DefeatCondition_AnyUnitPassesX",
     {{{"Satisfied", 117}, {"Celebrated", 217}}},
     2},
    {Kind::any_unit_passes_z,
     "DefeatCondition_AnyUnitPassesZ",
     {{{"Satisfied", 118}, {"Celebrated", 218}}},
     2},
}};

struct PinnedBytes {
    uint64_t size{};
    uint64_t hash{}; // 64-bit FNV-1a
};

// Packing leaves accounts this small as they are, so both images agree.
constexpr PinnedBytes pinned_packed_image{1276, 0xdf6ac0bfff69dbddull};
constexpr PinnedBytes pinned_plain_image{1276, 0xdf6ac0bfff69dbddull};

int failures = 0;

/// Records a failed check.
///
/// @param held whether the check held
/// @param what description printed when it did not
void check(bool held, const std::string& what) {
    if (!held) {
        std::cerr << "FAIL: " << what << '\n';
        ++failures;
    }
}

// A GlobalHeader naming every condition kind once.
struct EveryCondition final : DefinitionHost {
    std::map<std::string, int32_t, std::less<>> integers{
        {"KillEnemyCommander", 1},
        {"DestroyAllUnits", 1},
        {"KillAllMobileUnits", 1},
        {"CommanderKilled", 1},
        {"AllUnitsKilled", 1},
        {"VictoryTimerRunsOut", 4},
        {"DeathTimerRunsOut", 5},
        {"AnyUnitPassesX", 48},
        {"AnyUnitPassesZ", 64},
    };
    std::map<std::string, std::string, std::less<>> strings{
        {"BuildUnitType", "armcom"},
        {"CaptureUnitType", "armcom"},
        {"KillAllOfType", "armcom"},
        {"AllUnitsKilledOfType", "armcom"},
        {"KillUnitType", "armcom,48"},
        {"UnitTypeKilled", "armcom,48"},
        {"UnitTypePassesX", "armcom,48"},
        {"UnitTypePassesZ", "armcom,48"},
        {"MoveUnitToRadius", "ANYTYPE,16,32,64"},
    };

    /// Returns a key's integer.
    ///
    /// @param key key name
    /// @param fallback value for a key not listed
    /// @return the value
    int32_t integer(std::string_view key, int32_t fallback) override {
        const auto found = integers.find(key);
        return found == integers.end() ? fallback : found->second;
    }

    /// Returns a key's text.
    ///
    /// @param key key name
    /// @return the text, or nullopt for a key not listed
    std::optional<std::string> text(std::string_view key) override {
        const auto found = strings.find(key);
        if (found == strings.end())
            return std::nullopt;
        return found->second;
    }
};

struct ScopedBank {
    persist::Bank bank{};

    ScopedBank() { persist::bank_init(&bank); }

    ~ScopedBank() { persist::bank_destroy(&bank); }

    ScopedBank(const ScopedBank&) = delete;
    ScopedBank& operator=(const ScopedBank&) = delete;
};

struct ScopedImage {
    persist::ByteImage image{};
    ScopedImage() = default;

    ~ScopedImage() { persist::byte_image_free(&image); }

    ScopedImage(const ScopedImage&) = delete;
    ScopedImage& operator=(const ScopedImage&) = delete;
};

/// Returns every registered condition of a controller, victories first.
///
/// @param[in,out] controller registered controller
/// @return pointers to its conditions, in dispatch order
std::array<Condition*, kind_count> conditions_of(Controller& controller) {
    std::array<Condition*, kind_count> all{};
    size_t next = 0;
    for (int32_t i = 0; i < controller.victory_count && next < kind_count; ++i)
        all[next++] = &*controller.victory[static_cast<size_t>(i)];
    for (int32_t i = 0; i < controller.defeat_count && next < kind_count; ++i)
        all[next++] = &*controller.defeat[static_cast<size_t>(i)];
    return all;
}

/// Returns the counter field a kind saves, if it saves one.
///
/// @param kind condition kind
/// @return the field, or null
int32_t Condition::* counter_field(Kind kind) {
    if (kind == Kind::kill_all_mobile_units)
        return &Condition::units_counted;
    if (kind == Kind::kill_unit_type || kind == Kind::unit_type_killed)
        return &Condition::kills_left;
    return nullptr;
}

/// Gives each condition its distinctive values.
///
/// @param[in,out] controller registered controller with all 18 kinds
void give_distinctive_values(Controller& controller) {
    for (Condition* condition : conditions_of(controller)) {
        const auto index = static_cast<int32_t>(condition->kind);
        condition->satisfied = first_satisfied + index;
        condition->celebrated = first_celebrated + index;
        if (condition->kind == Kind::kill_all_mobile_units)
            condition->units_counted = mobile_units_left;
        if (condition->kind == Kind::kill_unit_type)
            condition->kills_left = kill_unit_type_left;
        if (condition->kind == Kind::unit_type_killed)
            condition->kills_left = unit_type_killed_left;
    }
}

/// Registers all 18 kinds from the synthetic GlobalHeader.
///
/// @param[out] controller controller to construct and fill
void register_every_kind(Controller& controller) {
    construct(controller);
    EveryCondition header;
    register_conditions(controller, header);
    const auto registered = controller.victory_count + controller.defeat_count;
    check(registered == static_cast<int32_t>(kind_count), "every kind registers");
}

/// Returns the 64-bit FNV-1a of bytes.
///
/// @param bytes bytes to hash
/// @return the hash
uint64_t fnv1a(std::span<const uint8_t> bytes) {
    uint64_t hash = fnv_basis;
    for (const uint8_t byte : bytes) {
        hash ^= byte;
        hash *= fnv_prime;
    }
    return hash;
}

/// Checks an image against a pinned size and hash, printing the values found when they differ.
///
/// @param what name printed
/// @param image image produced
/// @param expected pinned size and hash
void matches_pinned(
    const std::string& what, const persist::ByteImage& image, const PinnedBytes& expected
) {
    const PinnedBytes found{image.size, fnv1a(std::span<const uint8_t>(image.data, image.size))};
    char text[96];
    std::snprintf(
        text,
        sizeof text,
        "{%llu, 0x%016llxull}",
        static_cast<unsigned long long>(found.size),
        static_cast<unsigned long long>(found.hash)
    );
    check(found.size == expected.size && found.hash == expected.hash, what + " is " + text);
}

/// Checks that each kind's account holds exactly its expected fields, in order.
///
/// @param[in,out] bank bank the conditions were saved to
void accounts_match(persist::Bank* bank) {
    check(bank->accounts->count == static_cast<int32_t>(kind_count), "one account per kind");
    for (const auto& expected : expected_accounts) {
        const std::string name = expected.name;
        const bool named = std::strcmp(condition_record_name(expected.kind), expected.name) == 0;
        check(named, name + " is the kind's account name");
        const bool saved_account = persist::bank_open_account(bank, expected.name);
        check(saved_account, name + " was saved");
        if (!saved_account)
            continue;
        const persist::BankAccount& account = bank->accounts->items[bank->accounts->open];
        check(account.field_count == static_cast<int32_t>(expected.field_count), name + " fields");
        check(account.blob_count == 0, name + " has no blobs");
        const auto saved = static_cast<size_t>(account.field_count);
        for (size_t i = 0; i < std::min(expected.field_count, saved); ++i) {
            const persist::BankField& field = account.fields[i];
            const ExpectedField& want = expected.fields[i];
            const std::string where = name + " field " + std::to_string(i);
            check(std::strcmp(field.name, want.name) == 0, where + " is " + want.name);
            check(
                field.type == persist::BankFieldType::integer && field.integer == want.value,
                where + " holds " + std::to_string(field.integer)
            );
        }
    }
}

/// Tells whether a restored condition field equals the saved one.
///
/// @param saved condition that was saved
/// @param loaded condition restored from the save
/// @param field the field compared
/// @return whether they agree
bool same_field(const Condition& saved, const Condition& loaded, int32_t Condition::* field) {
    return saved.*field == loaded.*field;
}

/// Saves all 18 kinds, checks the accounts and images, and loads them back.
void accounts_round_trip() {
    Controller saved;
    register_every_kind(saved);
    give_distinctive_values(saved);
    ScopedBank bank;
    check(persist::bank_reset(&bank.bank), "bank resets");
    save_conditions(saved, &bank.bank, campaign_map_kind);
    accounts_match(&bank.bank);

    const char* description = persist::savegame_description;
    ScopedImage plain;
    check(persist::bank_write_image(&bank.bank, description, false, &plain.image), "plain image");
    matches_pinned("plain image", plain.image, pinned_plain_image);
    ScopedImage packed;
    check(persist::bank_write_image(&bank.bank, description, true, &packed.image), "packed image");
    matches_pinned("packed image", packed.image, pinned_packed_image);

    ScopedBank back;
    persist::BankError error{};
    const bool read = persist::bank_read_image(
        &back.bank, packed.image.data, packed.image.size, description, nullptr, &error
    );
    check(read, std::string("packed image reads back: ") + error.message);
    Controller loaded;
    register_every_kind(loaded);
    load_conditions(loaded, &back.bank, campaign_map_kind);
    const auto before = conditions_of(saved);
    const auto after = conditions_of(loaded);
    for (size_t i = 0; i < kind_count; ++i) {
        const std::string name = condition_record_name(before[i]->kind);
        check(after[i]->kind == before[i]->kind, name + " registers in the same order");
        check(
            same_field(*before[i], *after[i], &Condition::satisfied), name + " satisfied restored"
        );
        check(
            same_field(*before[i], *after[i], &Condition::celebrated), name + " celebrated restored"
        );
        if (const auto counter = counter_field(before[i]->kind))
            check(same_field(*before[i], *after[i], counter), name + " counter restored");
    }
    destroy(saved);
    destroy(loaded);
}

/// Checks that a load for a map other than a campaign's leaves every condition as it is,
/// though the bank holds all 18 accounts.
void other_maps_load_nothing() {
    Controller saved;
    register_every_kind(saved);
    give_distinctive_values(saved);
    ScopedBank bank;
    check(persist::bank_reset(&bank.bank), "bank resets");
    save_conditions(saved, &bank.bank, campaign_map_kind);

    Controller untouched;
    register_every_kind(untouched);
    for (Condition* condition : conditions_of(untouched)) {
        condition->satisfied = unsaved_value;
        condition->celebrated = unsaved_value;
        condition->units_counted = unsaved_value;
        condition->kills_left = unsaved_value;
    }
    Controller before = untouched;
    load_conditions(untouched, &bank.bank, non_campaign_map_kind);
    const auto kept = conditions_of(before);
    const auto after = conditions_of(untouched);
    for (size_t i = 0; i < kind_count; ++i)
        check(
            *after[i] == *kept[i],
            std::string(condition_record_name(kept[i]->kind)) + " left alone off a campaign map"
        );
    // The same bank does load on a campaign map, so the check above could fail.
    load_conditions(untouched, &bank.bank, campaign_map_kind);
    check(after[0]->satisfied == first_satisfied, "the bank loads on a campaign map");
    destroy(saved);
    destroy(before);
    destroy(untouched);
}

} // namespace

int main() {
    try {
        accounts_round_trip();
        other_maps_load_nothing();
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
    if (failures != 0)
        return 1;
    std::cout << "condition accounts passed\n";
    return 0;
}
