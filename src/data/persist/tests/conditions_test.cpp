// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "check.hpp"

#include "oa/sim/scenario/condition_persist.hpp"

#include <cstdint>
#include <cstring>
#include <map>
#include <string>

using namespace oa::sim::scenario;
namespace persist = oa::data::persist;

namespace {

struct Definition final : DefinitionHost {
    std::map<std::string, int32_t> integers;
    std::map<std::string, std::string> strings;

    int32_t integer(std::string_view key, int32_t fallback) override {
        auto found = integers.find(std::string(key));
        return found == integers.end() ? fallback : found->second;
    }

    std::optional<std::string> text(std::string_view key) override {
        auto found = strings.find(std::string(key));
        if (found == strings.end())
            return {};
        return found->second;
    }
};

Definition every_condition() {
    Definition d;
    for (auto key :
         {"KillEnemyCommander",
          "DestroyAllUnits",
          "KillAllMobileUnits",
          "CommanderKilled",
          "AllUnitsKilled"})
        d.integers[key] = 1;
    d.integers["VictoryTimerRunsOut"] = 4;
    d.integers["DeathTimerRunsOut"] = 5;
    d.integers["AnyUnitPassesX"] = 48;
    d.integers["AnyUnitPassesZ"] = 64;
    for (auto key : {"BuildUnitType", "CaptureUnitType", "KillAllOfType", "AllUnitsKilledOfType"})
        d.strings[key] = "armcom";
    for (auto key : {"KillUnitType", "UnitTypeKilled", "UnitTypePassesX", "UnitTypePassesZ"})
        d.strings[key] = "armcom,48";
    d.strings["MoveUnitToRadius"] = "ANYTYPE,16,32,64";
    return d;
}

template <class F>
void each(Controller& c, F f) {
    for (int32_t i = 0; i < c.victory_count; ++i)
        f(*c.victory[static_cast<std::size_t>(i)]);
    for (int32_t i = 0; i < c.defeat_count; ++i)
        f(*c.defeat[static_cast<std::size_t>(i)]);
}

void round_trip_all_kinds() {
    Controller saved;
    construct(saved);
    Definition d = every_condition();
    register_conditions(saved, d);
    CHECK(saved.victory_count + saved.defeat_count == 18);
    int32_t seed = 1;
    each(saved, [&](Condition& c) {
        c.satisfied = seed & 1;
        c.celebrated = (seed >> 1) & 1;
        if (c.kind == Kind::kill_all_mobile_units)
            c.units_counted = 77;
        if (c.kind == Kind::kill_unit_type || c.kind == Kind::unit_type_killed)
            c.kills_left = 11 + seed;
        ++seed;
    });

    persist::Bank bank;
    persist::bank_init(&bank);
    persist::bank_reset(&bank);
    save_conditions(saved, &bank, 1);
    CHECK(bank.accounts->count == 18);
    persist::bank_open_account(&bank, "VictoryCondition_KillAllMobileUnits");
    CHECK(persist::bank_get_int(&bank, "NumUnits", 0) == 77);
    CHECK(std::strcmp(bank.accounts->items[bank.accounts->open].fields[0].name, "NumUnits") == 0);
    persist::bank_open_account(&bank, "DefeatCondition_UnitTypeKilled");
    CHECK(persist::bank_has_field(&bank, "NumLeftToKill"));

    // Through a file image, as a campaign save would.
    persist::ByteImage image{};
    CHECK(persist::bank_write_image(&bank, persist::savegame_description, true, &image));
    persist::Bank back;
    persist::bank_init(&back);
    persist::BankError error{};
    CHECK(persist::bank_read_image(&back, image.data, image.size, nullptr, nullptr, &error));

    Controller loaded;
    construct(loaded);
    register_conditions(loaded, d);
    load_conditions(loaded, &back, 1);
    for (int32_t i = 0; i < saved.victory_count; ++i) {
        const Condition& a = *saved.victory[static_cast<std::size_t>(i)];
        const Condition& b = *loaded.victory[static_cast<std::size_t>(i)];
        CHECK(a.satisfied == b.satisfied);
        CHECK(a.celebrated == b.celebrated);
        if (a.kind == Kind::kill_all_mobile_units)
            CHECK(b.units_counted == 77);
        if (a.kind == Kind::kill_unit_type)
            CHECK(b.kills_left == a.kills_left);
    }
    for (int32_t i = 0; i < saved.defeat_count; ++i) {
        const Condition& a = *saved.defeat[static_cast<std::size_t>(i)];
        const Condition& b = *loaded.defeat[static_cast<std::size_t>(i)];
        CHECK(a.satisfied == b.satisfied);
        CHECK(a.celebrated == b.celebrated);
        if (a.kind == Kind::unit_type_killed)
            CHECK(b.kills_left == a.kills_left);
    }
    persist::byte_image_free(&image);
    persist::bank_destroy(&back);
    persist::bank_destroy(&bank);
}

void non_campaign_maps_do_nothing() {
    Controller c;
    construct(c);
    Definition empty;
    register_conditions(c, empty);
    persist::Bank bank;
    persist::bank_init(&bank);
    persist::bank_reset(&bank);
    save_conditions(c, &bank, 0);
    CHECK(bank.accounts->count == 0);
    save_conditions(c, &bank, 1);
    CHECK(bank.accounts->count == 2); // default DestroyAllUnits + AllUnitsKilled
    CHECK(
        std::strcmp(
            condition_record_name(Kind::all_units_killed), "DefeatCondition_AllUnitsKilled"
        ) == 0
    );
    // Absent state loads as zero.
    persist::Bank empty_bank;
    persist::bank_init(&empty_bank);
    persist::bank_reset(&empty_bank);
    c.victory[0]->satisfied = 1;
    load_conditions(c, &empty_bank, 1);
    CHECK(c.victory[0]->satisfied == 0);
    persist::bank_destroy(&empty_bank);
    persist::bank_destroy(&bank);
}

} // namespace

int main() {
    round_trip_all_kinds();
    non_campaign_maps_do_nothing();
    return oa::data::persist::test::finish("persist-conditions");
}
