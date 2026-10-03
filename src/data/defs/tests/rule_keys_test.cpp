// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Rule keys: a unit or weapon key a profile binds is read under the name it
// gives, without case; an unbound key is ignored; values that cannot be used
// are reported and read as missing; the weapon loader fills each slot's
// record only while keys are bound.

#include "oa/data/defs/rule_keys.hpp"
#include "oa/data/defs/weapons.hpp"
#include "oa/test/check.hpp"
#include "test_files.hpp"

#include <cstring>
#include <memory>
#include <string>

namespace {

using namespace oa::data::defs;
namespace tdf = oa::formats::tdf;
namespace mr = oa::data::match_rules;

/// Returns the veterancy thresholds a record holds: an empty list without any.
///
/// @param data the record
/// @return its thresholds
mr::FixedList<uint16_t, mr::max_veterancy_thresholds>
listed_thresholds(const mr::UnitTypeRules& data) {
    return data.veterancy_thresholds.value_or(
        mr::FixedList<uint16_t, mr::max_veterancy_thresholds>{}
    );
}

/// A parsed TDF text and its first top-level block.
struct Parsed {
    tdf::OwnedDocument document;
    const tdf::Block* block{};

    explicit Parsed(const std::string& text) {
        const bool parsed = document.parse(text);
        OA_CHECK(parsed);
        block = parsed ? tdf::child_at(document.root(), 0) : nullptr;
    }
};

const UnitDataKeys unit_keys{"VeterancyThresholds", "VeterancyAccuracyBuffRate", "Rotations"};

mr::UnitTypeRules unit_data(const std::string& body, RuleKeyIssues* issues = nullptr) {
    Parsed parsed{"[UNITINFO]\n{\n" + body + "\n}\n"};
    mr::UnitTypeRules data{};
    data.veterancy_thresholds.emplace(); // reset by the reader
    const RuleKeyIssues found = read_unit_rule_keys(parsed.block, unit_keys, data);
    if (issues != nullptr)
        *issues = found;
    return data;
}

void unbound_keys_are_ignored() {
    Parsed parsed{"[UNITINFO]\n{\nVeterancyThresholds=1 2 3;\nRotations=SN;\n}\n"};
    mr::UnitTypeRules data{};
    const RuleKeyIssues issues = read_unit_rule_keys(parsed.block, UnitDataKeys{}, data);
    OA_CHECK(data == mr::UnitTypeRules{});
    OA_CHECK(!issues.any());
    OA_CHECK(!UnitDataKeys{}.any());
    OA_CHECK(unit_keys.any());
    OA_CHECK(!(UnitDataKeys{"", nullptr, ""}.any()));
    OA_CHECK(mr::UnitTypeRules{}.build_facings == mr::build_facing::south);
}

void thresholds_are_ascending_kill_counts() {
    const mr::UnitTypeRules data = unit_data("veterancythresholds=10 20 30 40 50;");
    OA_CHECK(data.veterancy_thresholds.has_value());
    OA_CHECK(listed_thresholds(data).count == 5);
    OA_CHECK(listed_thresholds(data).items[0] == 10 && listed_thresholds(data).items[4] == 50);
    const mr::UnitTypeRules commas = unit_data("VeterancyThresholds=5,5, 7\t65535;");
    OA_CHECK(listed_thresholds(commas).count == 4);
    OA_CHECK(listed_thresholds(commas).items[3] == 65535);

    RuleKeyIssues issues{};
    const mr::UnitTypeRules empty = unit_data("VeterancyThresholds=;", &issues);
    OA_CHECK(!empty.veterancy_thresholds.has_value() && !issues.any());

    std::string thirty_two = "VeterancyThresholds=";
    for (int i = 0; i < 32; ++i)
        thirty_two += std::to_string(i) + " ";
    OA_CHECK(listed_thresholds(unit_data(thirty_two + ";")).count == 32);

    const struct {
        const char* body;
        RuleKeyProblem problem;
    } bad[] = {
        {"VeterancyThresholds=10 5;", RuleKeyProblem::not_ascending},
        {"VeterancyThresholds=10 x;", RuleKeyProblem::not_a_number},
        {"VeterancyThresholds=-5;", RuleKeyProblem::not_a_number},
        {"VeterancyThresholds=10a;", RuleKeyProblem::not_a_number},
        {"VeterancyThresholds=65536;", RuleKeyProblem::out_of_range},
        {"VeterancyThresholds=99999999999;", RuleKeyProblem::out_of_range},
    };

    for (const auto& item : bad) {
        const mr::UnitTypeRules data_bad = unit_data(item.body, &issues);
        OA_CHECK(issues.veterancy_thresholds == item.problem);
        OA_CHECK(!data_bad.veterancy_thresholds.has_value());
        OA_CHECK(issues.any());
    }
    const mr::UnitTypeRules too_many = unit_data(thirty_two + " 40;", &issues);
    OA_CHECK(issues.veterancy_thresholds == RuleKeyProblem::too_many_values);
    OA_CHECK(!too_many.veterancy_thresholds.has_value());
    OA_CHECK(std::strcmp(rule_key_problem_text(RuleKeyProblem::not_ascending), "") != 0);
}

void accuracy_rate_is_a_whole_number() {
    RuleKeyIssues issues{};
    const mr::UnitTypeRules data = unit_data("VeterancyAccuracyBuffRate= 24 ;", &issues);
    OA_CHECK(data.veterancy_accuracy_rate.has_value() && data.veterancy_accuracy_rate == 24);
    OA_CHECK(!issues.any());
    const mr::UnitTypeRules zero = unit_data("veterancyaccuracybuffrate=0;");
    OA_CHECK(zero.veterancy_accuracy_rate.has_value() && zero.veterancy_accuracy_rate == 0);
    const mr::UnitTypeRules bad = unit_data("VeterancyAccuracyBuffRate=12 13;", &issues);
    OA_CHECK(!bad.veterancy_accuracy_rate.has_value());
    OA_CHECK(issues.veterancy_accuracy_rate == RuleKeyProblem::not_a_number);
    unit_data("VeterancyAccuracyBuffRate=70000;", &issues);
    OA_CHECK(issues.veterancy_accuracy_rate == RuleKeyProblem::out_of_range);
    OA_CHECK(!unit_data("Other=1;").veterancy_accuracy_rate.has_value());
}

void facings_are_letters() {
    OA_CHECK(unit_data("").build_facings == mr::build_facing::south);
    OA_CHECK(
        unit_data("Rotations=SENW;").build_facings ==
        (mr::build_facing::south | mr::build_facing::east | mr::build_facing::north |
         mr::build_facing::west)
    );
    OA_CHECK(
        unit_data("rotations=en;").build_facings ==
        (mr::build_facing::east | mr::build_facing::north)
    );
    OA_CHECK(unit_data("Rotations=WW;").build_facings == mr::build_facing::west);
    RuleKeyIssues issues{};
    OA_CHECK(unit_data("Rotations=SX;", &issues).build_facings == mr::build_facing::south);
    OA_CHECK(issues.build_facings == RuleKeyProblem::unknown_facing);
    unit_data("Rotations=;", &issues);
    OA_CHECK(issues.build_facings == RuleKeyProblem::unknown_facing);
}

void bound_names_are_the_profiles() {
    const UnitDataKeys renamed{"Kills", "Aim", "Faces"};
    Parsed parsed{
        "[UNITINFO]\n{\nKills=1 2;\nAim=6;\nFaces=E;\nVeterancyThresholds=9;\nRotations=N;\n}\n"
    };
    mr::UnitTypeRules data{};
    read_unit_rule_keys(parsed.block, renamed, data);
    OA_CHECK(listed_thresholds(data).count == 2 && listed_thresholds(data).items[1] == 2);
    OA_CHECK(data.veterancy_accuracy_rate == 6);
    OA_CHECK(data.build_facings == mr::build_facing::east);
}

void preview_keys_are_kept_as_text() {
    Parsed parsed{
        "[UNITINFO]\n{\nPreviewPieces=base turret;\nPreviewPiecesE=base;\nPreviewPiecesW=arm;\n"
        "PreviewObject3D=ghost;\nPreviewFaceOpponent=1;\n}\n"
    };
    const UnitPreviewDataKeys keys{
        "PreviewPieces", "PreviewPieces", "PreviewObject3D", "PreviewFaceOpponent"
    };
    OA_CHECK(keys.any() && !UnitPreviewDataKeys{}.any());
    UnitPreviewKeys preview{};
    read_unit_preview_keys(parsed.block, keys, preview);
    OA_CHECK(std::strcmp(preview.pieces.data(), "base turret") == 0);
    OA_CHECK(std::strcmp(preview.pieces_by_facing[0].data(), "") == 0);
    OA_CHECK(std::strcmp(preview.pieces_by_facing[1].data(), "base") == 0);
    OA_CHECK(std::strcmp(preview.pieces_by_facing[3].data(), "arm") == 0);
    OA_CHECK(std::strcmp(preview.object.data(), "ghost") == 0);
    OA_CHECK(preview.face_opponent);
    read_unit_preview_keys(parsed.block, UnitPreviewDataKeys{}, preview);
    OA_CHECK(preview.pieces[0] == '\0' && !preview.face_opponent);
}

void unit_files_are_read_through_the_boundary() {
    test::MemoryFiles memory;
    memory.files.push_back(
        {"units\\TANK.FBI", "[UNITINFO]\n{\nUnitName=TANK;\nVeterancyThresholds=4 8 x;\n}\n"}
    );
    const Files files = memory.view();
    mr::UnitTypeRules data{};
    UnitPreviewKeys preview{};
    RuleKeyIssues issues{};
    OA_CHECK(load_unit_rule_keys(
        &files, "units\\TANK.FBI", unit_keys, UnitPreviewDataKeys{}, data, &preview, &issues
    ));
    OA_CHECK(issues.veterancy_thresholds == RuleKeyProblem::not_a_number);
    OA_CHECK(!data.veterancy_thresholds.has_value());
    OA_CHECK(!load_unit_rule_keys(
        &files, "units\\NONE.FBI", unit_keys, UnitPreviewDataKeys{}, data, nullptr, nullptr
    ));
}

void weapon_keys_take_the_low_bit() {
    const std::string text = "[GUN]\n{\nID=7;\nnottoair=1;\nSurfaceFire=3;\nnottounderwater=2;\n"
                             "nomapweaponalert=-1;\n}\n"
                             "[OTHER]\n{\nID=8;\nnottoair=1;\n}\n";
    const WeaponDataKeys keys{"nottoair", "surfacefire", "nottounderwater", "nomapweaponalert"};
    OA_CHECK(keys.any() && !WeaponDataKeys{}.any());

    const auto table = std::make_unique<WeaponTable>();
    weapon_table_init(table.get());
    const WeaponLoadOptions bound{nullptr, nullptr, false, false, &keys};
    OA_CHECK(
        load_weapon_text(table.get(), text.data(), static_cast<uint32_t>(text.size()), &bound)
    );
    OA_CHECK(table->rule_data[7].not_to_air);
    OA_CHECK(table->rule_data[7].surface_fire);
    OA_CHECK(!table->rule_data[7].not_to_underwater);
    OA_CHECK(table->rule_data[7].no_map_alert);
    OA_CHECK(table->rule_data[8] == (mr::WeaponTypeRules{true, false, false, false}));
    OA_CHECK(table->rule_data[9] == mr::WeaponTypeRules{});
    // A later section with the same ID replaces the record.
    const std::string again = "[GUN2]\n{\nID=7;\n}\n";
    OA_CHECK(
        load_weapon_text(table.get(), again.data(), static_cast<uint32_t>(again.size()), &bound)
    );
    OA_CHECK(table->rule_data[7] == mr::WeaponTypeRules{});
    weapon_table_free(table.get());

    // Without bound keys nothing is read, as 3.1c reads nothing.
    const auto plain = std::make_unique<WeaponTable>();
    weapon_table_init(plain.get());
    const WeaponLoadOptions unbound{};
    OA_CHECK(
        load_weapon_text(plain.get(), text.data(), static_cast<uint32_t>(text.size()), &unbound)
    );
    OA_CHECK(plain->rule_data[7] == mr::WeaponTypeRules{});
    OA_CHECK(plain->defs[7].flags == table->defs[7].flags);
    weapon_table_free(plain.get());
}

} // namespace

int main() {
    unbound_keys_are_ignored();
    thresholds_are_ascending_kill_counts();
    accuracy_rate_is_a_whole_number();
    facings_are_letters();
    bound_names_are_the_profiles();
    preview_keys_are_kept_as_text();
    unit_files_are_read_through_the_boundary();
    weapon_keys_take_the_low_bit();
    return oa::test::check_exit_status();
}
