// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/unit_spawn/spawn_runtime.hpp"
#include <map>
#include <stdexcept>
#include <iostream>
#include <cstdint>
using namespace oa::sim::unit_spawn;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x))                                                                                  \
            throw std::runtime_error(#x);                                                          \
    } while (false)

struct Reader : AssetReader {
    std::map<std::string, std::vector<uint8_t>> files;
    std::vector<std::string> reads;

    std::optional<std::vector<uint8_t>> read(std::string_view path) override {
        reads.emplace_back(path);
        auto it = files.find(std::string(path));
        if (it == files.end())
            return std::nullopt;
        return it->second;
    }
};

void put32(std::vector<uint8_t>& bytes, std::size_t offset, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i)
        bytes.at(offset + i) = static_cast<uint8_t>(value >> (8 * i));
}

int main() {
    Reader reader;
    // One empty root piece, legal parsed geometry rather than an artificial model Host.
    auto& model = reader.files["objects3d/body.3DO"];
    model.resize(57);
    put32(model, 0, 1);
    put32(model, 12, 0xffffffff);
    put32(model, 28, 52);
    model[52] = 'b';
    model[53] = 'a';
    model[54] = 's';
    model[55] = 'e';
    auto& cob = reader.files["scripts/ARMCOM.COB"];
    cob.resize(44);
    put32(cob, 0, 4);
    for (unsigned i = 6; i < 11; ++i)
        put32(cob, i * 4, 44);
    reader.files["guis/ARMCOM0.GUI"] = {1};
    reader.files["guis/ARMCOM1.GUI"] = {1};
    reader.files["guis/ARMCOM2.GUI"] = {1};
    oa::data::unit_definitions::UnitDefinition definition;
    definition.unit_name = "ARMCOM";
    definition.object_name = "body";
    definition.max_damage = 3000;
    definition.heal_time = 7;
    definition.waterline = -5;
    definition.build_angle = -20;
    definition.bm_code = 1;
    definition.makes_metal = 99;
    definition.can_hover = true;
    definition.footprint_x = 2;
    definition.footprint_z = 3;
    RuntimeBindings bind;
    bind.enabled = true;
    bind.resolved_weapon_present = true;
    bind.default_mission = 5;
    auto loaded = load_runtime_type(definition, bind, reader);
    CHECK(loaded.model && loaded.model->objects.size() == 1 && loaded.script);
    // The 44-byte file's hash as the game computes it.
    CHECK(
        loaded.script->file_hash == 0x689228e0u &&
        oa::formats::cob::script_identity(*loaded.script) == 0x689228e0u
    );
    CHECK(
        loaded.type.model == reinterpret_cast<AssetHandle>(loaded.model.get()) &&
        loaded.type.cob == reinterpret_cast<AssetHandle>(loaded.script.get())
    );
    CHECK(
        loaded.type.simulation.maximum_health == 3000 && loaded.type.simulation.heal_time == 7 &&
        loaded.type.simulation.waterline_offset == 251
    );
    CHECK(
        loaded.type.simulation.default_mission_type == 5 && loaded.type.build_angle == 65516 &&
        loaded.type.gui_page_count == 3 && loaded.type.simulation.abilities == 0x500000u
    );
    CHECK((loaded.type.simulation.flags & 0x80811000u) == 0x80811000u);
    CHECK(
        reader.reads == std::vector<std::string>(
                            {"objects3d/body.3DO",
                             "guis/ARMCOM0.GUI",
                             "guis/ARMCOM1.GUI",
                             "guis/ARMCOM2.GUI",
                             "guis/ARMCOM3.GUI",
                             "scripts/ARMCOM.COB"}
                        )
    );
    // A unit with a movement class takes the class's footprint from the
    // loaded MOVEINFO classes, so the caller must resolve it first.
    definition.movement_class = "KBOT";
    bool rejected = false;
    try {
        load_runtime_type(definition, bind, reader);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    CHECK(rejected);
    bind.movement_footprint = std::array<int16_t, 2>{4, 5};
    reader.files.erase("scripts/ARMCOM.COB");
    auto plain = load_runtime_type(definition, bind, reader);
    CHECK(
        !plain.script && !plain.type.cob && plain.type.footprint_x == 4 &&
        plain.type.footprint_z == 5
    );
    reader.files["guis/ARMCOM0.GUI"] = {};
    reader.files["guis/ARMCOM1.GUI"] = {};
    auto no_pages = load_runtime_type(definition, bind, reader);
    CHECK(no_pages.type.gui_page_count == 0 && !(no_pages.type.simulation.flags & 0x80000000u));
    reader.files["scripts/ARMCOM.COB"] = {1, 2};
    rejected = false;
    try {
        load_runtime_type(definition, bind, reader);
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    CHECK(rejected);
    std::array<std::string, 3> names = {"", "ARMCOM", "CORCOM"};
    CHECK(find_type_index(names, "armcom") == 1 && find_type_index(names, "missing") == 0);
    std::cout << "unit runtime tests passed\n";
}
