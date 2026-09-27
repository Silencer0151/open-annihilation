// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/data/unit_definitions.hpp"

#include <cassert>
#include <cstdint>
#include <map>

using namespace oa::data::unit_definitions;

namespace {
class MemoryAssets final : public CatalogAssetReader {
  public:

    std::vector<std::string> order;
    std::map<std::string, std::string> files;

    Result<std::vector<std::string>>
    list_effective(std::string_view, std::string_view) const override {
        return {order, {}};
    }

    Result<std::string> read(std::string_view path) const override {
        auto it = files.find(std::string(path));
        if (it == files.end())
            return {{}, {ErrorCode::io, 0, "missing"}};
        return {it->second, {}};
    }
};
} // namespace

int main() {
    constexpr std::string_view specimen = R"(
// mixed case is intentional
[UNITINFO] {
 UnitName=ARMTEST; Name=Test Unit; Description=Test;
 MaxVelocity=1.6; BankScale=0.75; BuildCostMetal=1437;
 StandingMoveOrder=7; Builder=3; ObjectName=;
 Category=ARM KBOT LEVEL2; Mystery=preserved;
 [SFX] { select1=foo; }
})";
    auto loaded = load_fbi(specimen, "memory.fbi");
    assert(loaded);
    assert(loaded.value.unit_name == "ARMTEST");
    assert(loaded.value.object_name.empty());
    assert(loaded.value.max_velocity_fixed == 104857); // trunc(1.6 * 65536)
    assert(loaded.value.bank_scale_fixed == 49152);
    assert(loaded.value.damage_modifier_fixed == 65536);
    assert(loaded.value.move_rate1_fixed == 209714);
    assert(loaded.value.standing_move_order == 3);
    assert(loaded.value.builder);
    assert(loaded.value.categories.size() == 3);
    assert(loaded.value.unknown_fields.at("mystery") == "preserved");
    assert(pack_unit_flags(loaded.value) == (3U | (2U << 2U) | flag_mask(UnitFlag::builder)));
    assert(pack_unit_abilities(loaded.value) == 0x500000u);
    UnitDefinition abilities;
    abilities.on_offable = true;
    abilities.can_attack = true;
    abilities.can_reclamate = true;
    abilities.can_resurrect = true;
    abilities.can_cloak = true;
    abilities.commander = true;
    abilities.self_destruct_countdown = 0;
    assert(
        pack_unit_abilities(abilities) ==
        (0x4u | 0x10u | 0x200u | 0x400u | 0x800u | 0x2000u | 0x40000u)
    );
    assert(project_for_spawn(loaded.value).max_damage == 0);

    auto wrapped = load_fbi("[UNITINFO]{MaxVelocity=65536;}");
    assert(wrapped && wrapped.value.max_velocity_fixed == 0); // low 32 bits of 2^32
    // Without the keys a unit roams and fires at will; ARMCOM.FBI's
    // StandingFireOrder=2 and StandingMoveOrder=0 fire at will and hold position.
    assert(wrapped.value.standing_move_order == 2 && wrapped.value.standing_fire_order == 2);
    assert((pack_unit_flags(wrapped.value) & 0xfU) == (2U | (2U << 2U)));
    auto commander = load_fbi("[UNITINFO]{StandingFireOrder=2; StandingMoveOrder=0;}");
    assert(commander && commander.value.standing_move_order == 0);
    assert(commander.value.standing_fire_order == 2);
    assert((pack_unit_flags(commander.value) & 0xfU) == (0U | (2U << 2U)));

    auto malformed = parse_tdf("[UNITINFO] { x=1;");
    assert(!malformed && malformed.error.code == ErrorCode::malformed);
    auto bad_comment = parse_tdf("[UNITINFO]{}/*");
    assert(!bad_comment && bad_comment.error.code == ErrorCode::malformed);

    MemoryAssets assets;
    assets.order = {"units/z.fbi", "units/a.fbi", "units/filtered.fbi"};
    assets.files["units/z.fbi"] = "[UNITINFO]{UnitName=Zulu;Version=3.1;}";
    assets.files["units/a.fbi"] = "[UNITINFO]{UnitName=alpha;Version=3.1;}";
    assets.files["units/filtered.fbi"] = "[UNITINFO]{UnitName=Beta;Version=99;}";
    CatalogOptions catalog_options;
    catalog_options.compatible = [](const UnitDefinition& u) { return u.version < 10.0; };
    auto catalog = load_unit_catalog(assets, catalog_options);
    assert(catalog && catalog.value.entries.size() == 2);
    assert(catalog.value.entries[0].definition.unit_name == "alpha");
    assert(catalog.value.entries[0].type_id == 1);
    assert(catalog.value.entries[1].definition.unit_name == "Zulu");
    assert(catalog.value.entries[1].type_id == 2);
    catalog.value.entries[0].definition.categories = {"ARM", "VTOL"};
    catalog.value.entries[1].definition.categories = {"CORE", "TANK"};
    catalog.value.entries[1].definition.wpri_bad_target_category = "VTOL";
    catalog.value.entries[1].definition.no_chase_category = "ARM";
    auto registry = resolve_unit_categories(catalog.value);
    assert(registry && registry.value.categories.at("vtol").contains(1));
    assert(!registry.value.categories.at("vtol").contains(2));
    assert(registry.value.target_masks[2].primary_bad.contains(1));
    assert(registry.value.target_masks[2].no_chase.contains(1));
    assert(!registry.value.target_masks[1].primary_bad.contains(1)); // default "none"

    auto movement = load_movement_classes(R"(
      [CLASS0]{Name=TANK2;FootprintX=2;FootprintZ=2;MaxSlope=20;MaxWaterSlope=10;}
      [CLASS2]{Name=BOAT3;FootprintX=3;FootprintZ=3;MinWaterDepth=5;}
    )");
    assert(
        movement && movement.value.slots[0] && !movement.value.slots[1] && movement.value.slots[2]
    );
    assert(movement.value.slots[0]->max_slope == 10);
    auto building =
        load_fbi("[UNITINFO]{UnitName=B;MovementClass=TANK2;BMcode=0;YardMap=G O / Cc;} ");
    assert(building);
    auto metadata = resolve_runtime_metadata(building.value, movement.value);
    assert(metadata && metadata.value.movement_class_handle == 0);
    assert(metadata.value.footprint_x == 2 && metadata.value.footprint_z == 2);
    assert((metadata.value.yard_cells == std::vector<uint8_t>{0x8f, 0x2b, 0x35, 0x2d}));

    // Appending targets the builder whose type_id matches UNITMENU.
    // Sorted type ids: Alpha=1, Builder=2, Child=3, Twin=4.
    MemoryAssets download_assets;
    download_assets.order = {
        "units/builder.fbi", "units/alpha.fbi", "units/child.fbi", "units/twin.fbi"
    };
    download_assets.files["units/builder.fbi"] = "[UNITINFO]{UnitName=Builder;}";
    download_assets.files["units/alpha.fbi"] = "[UNITINFO]{UnitName=Alpha;}";
    download_assets.files["units/child.fbi"] = "[UNITINFO]{UnitName=Child;}";
    download_assets.files["units/twin.fbi"] = "[UNITINFO]{UnitName=Twin;}";
    auto download_catalog = load_unit_catalog(download_assets);
    assert(download_catalog && download_catalog.value.entries.size() == 4);
    assert(download_catalog.value.entries[1].definition.unit_name == "Builder");
    assert(download_catalog.value.entries[1].type_id == 2);
    std::vector<std::optional<std::vector<uint16_t>>> build_lists(4);
    build_lists[1] = std::vector<uint16_t>{9}; // existing CANBUILD id
    build_lists[2] = std::vector<uint16_t>{};
    const std::vector<std::vector<DownloadMenuEntry>> menus = {
        {{2, "child"}, {2, "missing"}, {1, "Builder"}, {2, "TWIN"}},
        {{2, "aLpHa"}},
    };
    append_download_build_ids(download_catalog.value, build_lists, menus);
    assert(!build_lists[0]); // no build list: menu index 1 does not allocate one
    assert(build_lists[1] && (*build_lists[1] == std::vector<uint16_t>{9, 3, 4, 1}));
    assert(build_lists[2] && build_lists[2]->empty());
    assert(!build_lists[3]);
    std::vector<uint16_t> full(download_build_id_limit - 1, 4);
    build_lists[1] = full;
    append_download_build_ids(download_catalog.value, build_lists, {{{2, "alpha"}, {2, "child"}}});
    assert(build_lists[1]->size() == download_build_id_limit);
    assert(build_lists[1]->back() == 1);
    append_download_build_ids(download_catalog.value, build_lists, {{{2, "child"}}});
    assert(build_lists[1]->size() == download_build_id_limit);

    // The name lookup returns the first sorted record's type_id, including zero.
    UnitCatalog manual;
    UnitDefinition first_ghost;
    first_ghost.unit_name = "Ghost";
    UnitDefinition later_ghost;
    later_ghost.unit_name = "Ghost";
    UnitDefinition host;
    host.unit_name = "Host";
    manual.entries.push_back({0, "ghost0", first_ghost});
    manual.entries.push_back({5, "ghost1", later_ghost});
    manual.entries.push_back({6, "host", host});
    std::vector<std::optional<std::vector<uint16_t>>> manual_lists(3);
    manual_lists[2] = std::vector<uint16_t>{};
    append_download_build_ids(manual, manual_lists, {{{6, "GHOST"}, {6, "host"}}});
    assert(manual_lists[2] && (*manual_lists[2] == std::vector<uint16_t>{6}));
}
