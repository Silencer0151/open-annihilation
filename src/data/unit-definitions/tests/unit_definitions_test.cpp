// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Unit types loaded as a match loads them: each FBI into a UnitDef record by
// data::defs::load_unit_def, then the typed fields, runtime metadata and
// target masks taken from that record.
#include "oa/data/unit_definitions.hpp"
#include "oa/test/check.hpp"

#include "oa/data/defs/unit_catalog.hpp"
#include "oa/base/text.hpp"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace oa::data::unit_definitions;
namespace defs = oa::data::defs;

namespace {

/// Loads MOVEINFO text into a movement class table, as the game data loads it.
///
/// @param text the CLASSn sections
/// @return the table
defs::MoveClassTable move_classes(const char* text) {
    defs::MoveClassTable table;
    defs::move_class_table_init(&table);
    oa::formats::tdf::Document document;
    oa::formats::tdf::document_init(&document);
    const bool parsed = oa::formats::tdf::parse_text(
        &document, text, static_cast<uint32_t>(std::strlen(text)), false, nullptr
    );
    OA_CHECK(parsed);
    defs::move_class_table_load(&table, &document);
    oa::formats::tdf::document_free(&document);
    return table;
}

/// The tables FBIs load against: movement classes, two weapons, two sound
/// categories and the unit table with its category registry.
struct Tables {
    defs::MoveClassTable classes = move_classes(R"(
      [CLASS0]{Name=TANK2;FootprintX=2;FootprintZ=2;MaxSlope=20;MaxWaterSlope=10;}
      [CLASS2]{Name=BOAT3;FootprintX=3;FootprintZ=3;MinWaterDepth=5;}
    )");
    std::vector<oa::WeaponDef> weapons = std::vector<oa::WeaponDef>(OA_WEAPON_DEF_COUNT);
    std::vector<defs::SoundCategory> sound_list = std::vector<defs::SoundCategory>(2);
    defs::SoundCategoryTable sounds{};
    defs::UnitDefTables units{};

    Tables() {
        oa::base::text::copy_terminated(weapons[0].key, "NOWEAPON");
        oa::base::text::copy_terminated(weapons[7].key, "LASER");
        oa::base::text::copy_terminated(sound_list[0].name, "ARM_KBOT");
        oa::base::text::copy_terminated(sound_list[1].name, "CORE_TANK");
        sounds = {sound_list.data(), 2};
        defs::unit_def_tables_init(&units);
        const bool allocated = defs::unit_def_tables_allocate(&units, 4);
        OA_CHECK(allocated);
    }

    ~Tables() { defs::unit_def_tables_free(&units); }

    Tables(const Tables&) = delete;
    Tables& operator=(const Tables&) = delete;

    /// Loads FBI text into a type's record, as load_unit_def reads a file.
    ///
    /// @param type_id the record's slot
    /// @param text the FBI
    /// @return the record
    const oa::UnitDef& load(uint16_t type_id, const std::string& text) {
        auto read = [](void* context, const char*, uint8_t** data, uint32_t* size, bool* archived) {
            const auto& fbi = *static_cast<const std::string*>(context);
            *data = static_cast<uint8_t*>(std::malloc(fbi.size() + 1));
            std::memcpy(*data, fbi.data(), fbi.size());
            *size = static_cast<uint32_t>(fbi.size());
            *archived = true;
            return true;
        };
        const defs::Files files{
            const_cast<std::string*>(&text),
            read,
            [](void*, uint8_t* data) { std::free(data); },
            [](void*, const char*) { return true; },
            [](void*, const char*, const char*, void (*)(void*, const char*), void*) {}
        };
        const defs::UnitDefSources sources{
            "", &classes, weapons.data(), &sounds, &units.categories, &units.blocks, nullptr
        };
        auto& record = units.records[type_id];
        record.type_id = type_id;
        const bool loaded = defs::load_unit_def(&files, "units\\test.fbi", record, sources);
        OA_CHECK(loaded);
        return record;
    }

    /// Returns a loaded record's typed fields.
    ///
    /// @param record a record load() filled
    /// @return the fields
    UnitDefinition definition(const oa::UnitDef& record) const {
        return unit_definition_from(record, {&classes, weapons.data(), &sounds, &units.categories});
    }
};

} // namespace

int main() {
    Tables tables;
    const auto& specimen = tables.load(1, R"(
// mixed case is intentional
[UNITINFO] {
 UnitName=ARMTEST; Name=Test Unit; Description=Test;
 MaxVelocity=1.6; BankScale=0.75; BuildCostMetal=1437;
 StandingMoveOrder=7; Builder=3; ObjectName=;
 Category=ARM KBOT LEVEL2; Mystery=preserved;
 Weapon1=laser; Weapon2=NOSUCHGUN; SoundCategory=core_tank;
 [SFX] { select1=foo; }
})");
    auto loaded = tables.definition(specimen);
    OA_CHECK(loaded.unit_name == "ARMTEST" && loaded.display_name == "Test Unit");
    OA_CHECK(loaded.object_name.empty());
    OA_CHECK(loaded.max_velocity_fixed == 104857); // trunc(1.6 * 65536)
    OA_CHECK(loaded.bank_scale_fixed == 49152);
    OA_CHECK(loaded.damage_modifier_fixed == 65536);
    OA_CHECK(loaded.move_rate1_fixed == 209714);
    OA_CHECK(loaded.build_cost_metal == 1437);
    OA_CHECK(loaded.standing_move_order == 3);
    OA_CHECK(loaded.builder);
    OA_CHECK((loaded.categories == std::vector<std::string>{"ARM", "KBOT", "LEVEL2"}));
    // Weapons name the section their slot loaded; an unknown name is weapon
    // 0, which stands in for none.
    OA_CHECK(loaded.weapon1 == "LASER" && loaded.weapon2.empty() && loaded.explode_as.empty());
    OA_CHECK(loaded.sound_category == "CORE_TANK");
    OA_CHECK(loaded.movement_class.empty());
    OA_CHECK(pack_unit_flags(loaded) == (3U | (2U << 2U) | flag_mask(UnitFlag::builder)));
    OA_CHECK(pack_unit_abilities(loaded) == 0x500000u);
    OA_CHECK(project_for_spawn(loaded).max_damage == 0);
    UnitDefinition abilities;
    abilities.on_offable = true;
    abilities.can_attack = true;
    abilities.can_reclamate = true;
    abilities.can_resurrect = true;
    abilities.can_cloak = true;
    abilities.commander = true;
    abilities.self_destruct_countdown = 0;
    OA_CHECK(
        pack_unit_abilities(abilities) ==
        (0x4u | 0x10u | 0x200u | 0x400u | 0x800u | 0x2000u | 0x40000u)
    );

    // A sound category the table lacks is read as a number, so a name gives
    // category 0, as the game reads it.
    auto wrapped = tables.definition(
        tables.load(2, "[UNITINFO]{UnitName=WRAP;MaxVelocity=65536;SoundCategory=none;}")
    );
    OA_CHECK(wrapped.max_velocity_fixed == 0); // low 32 bits of 2^32
    OA_CHECK(wrapped.sound_category == "ARM_KBOT");
    // Without the keys a unit roams and fires at will; ARMCOM.FBI's
    // StandingFireOrder=2 and StandingMoveOrder=0 fire at will and hold position.
    OA_CHECK(wrapped.standing_move_order == 2 && wrapped.standing_fire_order == 2);
    OA_CHECK((pack_unit_flags(wrapped) & 0xfU) == (2U | (2U << 2U)));
    auto commander = tables.definition(
        tables.load(2, "[UNITINFO]{UnitName=CMD;StandingFireOrder=2; StandingMoveOrder=0;}")
    );
    OA_CHECK(commander.standing_move_order == 0 && commander.standing_fire_order == 2);
    OA_CHECK((pack_unit_flags(commander) & 0xfU) == (0U | (2U << 2U)));

    // A value that opens with ';' ends at once, so the rest of the line runs
    // into the next key's name: the canguard below is never read.
    auto scorpion = tables.definition(tables.load(
        3,
        "[UNITINFO]{UnitName=SCORP;\r\n\tItalianDescription=;Scorpione\r\n\tcanguard=1;\r\n"
        "\tcanpatrol=1;}"
    ));
    OA_CHECK(!scorpion.can_guard && scorpion.can_patrol);

    // A building takes its movement class's footprint and slopes; its yard map
    // is compiled one cell per footprint cell.
    const auto& building = tables.load(
        2,
        "[UNITINFO]{UnitName=B;MovementClass=tank2;BMcode=0;YardMap=G O / Cc;"
        "FootprintX=9;FootprintZ=9;MaxSlope=99;Category=VTOL;}"
    );
    OA_CHECK(tables.definition(building).movement_class == "TANK2");
    auto metadata = resolve_runtime_metadata(building, tables.classes, tables.units.blocks);
    OA_CHECK(metadata && metadata.value.movement_class_handle == 0);
    OA_CHECK(metadata.value.footprint_x == 2 && metadata.value.footprint_z == 2);
    OA_CHECK(metadata.value.max_slope == 10 && metadata.value.bad_slope == 10);
    OA_CHECK((metadata.value.yard_cells == std::vector<uint8_t>{0x8f, 0x2b, 0x35, 0x2d}));
    const auto& boat = tables.load(3, "[UNITINFO]{UnitName=S;MovementClass=BOAT3;BMcode=1;}");
    auto boat_metadata = resolve_runtime_metadata(boat, tables.classes, tables.units.blocks);
    OA_CHECK(boat_metadata && boat_metadata.value.movement_class_handle == 2);
    OA_CHECK(boat_metadata.value.min_water_depth == 5 && boat_metadata.value.yard_cells.empty());
    // Without a movement class a type keeps its own limits; each bad slope is
    // half its maximum.
    const auto& walker =
        tables.load(3, "[UNITINFO]{UnitName=W;BMcode=1;FootprintX=1;FootprintZ=1;MaxSlope=30;}");
    auto walker_metadata = resolve_runtime_metadata(walker, tables.classes, tables.units.blocks);
    OA_CHECK(walker_metadata && !walker_metadata.value.movement_class_handle);
    OA_CHECK(walker_metadata.value.max_slope == 30 && walker_metadata.value.bad_slope == 15);

    // Target masks: a primary bad target category and the default "none".
    const auto& hunter =
        tables.load(1, "[UNITINFO]{UnitName=H;wpri_badTargetCategory=VTOL;NoChaseCategory=VTOL;}");
    const auto masks = target_category_masks(hunter, tables.units.categories);
    OA_CHECK(masks.primary_bad.contains(2) && !masks.primary_bad.contains(1));
    OA_CHECK(masks.no_chase.contains(2));
    OA_CHECK(!masks.secondary_bad.contains(2) && !masks.special_bad.contains(2));
    return oa::test::check_exit_status();
}
