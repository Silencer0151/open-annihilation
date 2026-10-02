// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/unit_spawn/spawn_runtime.hpp"
#include <bit>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>

namespace oa::sim::unit_spawn {
namespace {
constexpr uint32_t enabled_mask = 0x00800000;
constexpr uint32_t gui_zero_mask = 0x80000000;

std::string base_name(std::string_view source) {
    if (source.empty() || source.size() > 31 ||
        source.find_first_of("/\\:\0", 0, 4) != std::string_view::npos || source == "." ||
        source == "..")
        throw std::invalid_argument("unit asset name is not a bounded basename");
    auto result = std::string(source);
    // The extension is stripped before the asset's own extension is added.
    const auto dot = result.find_last_of('.');
    if (dot != std::string::npos)
        result.resize(dot);
    if (result.empty())
        throw std::invalid_argument("empty unit asset basename");
    return result;
}

char lower(char value) {
    return value >= 'A' && value <= 'Z' ? static_cast<char>(value + ('a' - 'A')) : value;
}

bool equal_name(std::string_view a, std::string_view b) {
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (lower(a[i]) != lower(b[i]))
            return false;
    return true;
}
} // namespace

LoadedType load_runtime_type(
    const data::unit_definitions::UnitDefinition& definition,
    const RuntimeBindings& bindings,
    AssetReader& assets
) {
    if (!bindings.resolved_weapon_present)
        throw std::invalid_argument("unit runtime requires resolved weapon presence");
    const auto fields = data::unit_definitions::project_for_spawn(definition);
    LoadedType result;
    result.unit_name = definition.unit_name;
    auto& t = result.type;
    t.simulation.flags = fields.unit_flags | (bindings.enabled ? enabled_mask : 0u) |
                         (*bindings.resolved_weapon_present ? 0x00010000u : 0u);
    t.simulation.abilities = data::unit_definitions::pack_unit_abilities(definition);
    t.simulation.maximum_health = std::bit_cast<uint32_t>(fields.max_damage);
    t.simulation.heal_time = fields.heal_time;
    t.simulation.waterline_offset = std::bit_cast<uint8_t>(definition.waterline);
    t.simulation.default_mission_type = bindings.default_mission;
    t.footprint_x = fields.footprint_x;
    t.footprint_z = fields.footprint_z;
    if (!definition.movement_class.empty()) {
        if (!bindings.movement_footprint)
            throw std::invalid_argument("unit runtime requires resolved movement-class footprint");
        t.footprint_x = (*bindings.movement_footprint)[0];
        t.footprint_z = (*bindings.movement_footprint)[1];
    }
    t.player_limit = bindings.player_limit;
    t.build_angle = std::bit_cast<uint16_t>(fields.build_angle);
    t.bm_code = std::bit_cast<uint8_t>(fields.bm_code);
    const auto object = base_name(definition.object_name);
    const auto unit = base_name(definition.unit_name);
    result.model_path = "objects3d/" + object + ".3DO";
    const auto model_bytes = assets.read(result.model_path);
    auto model = model_bytes
                     ? formats::objects3d::load_3do(std::as_bytes(std::span(*model_bytes)))
                     : base::bytes::Decoded<formats::objects3d::Model>(base::bytes::DecodeError{
                           base::bytes::DecodeCode::not_found, 0, "required unit model not found"
                       });
    if (!model.ok())
        throw std::runtime_error(model.error.message + (": " + result.model_path));
    result.model = std::make_shared<const formats::objects3d::Model>(std::move(*model.value));
    t.model = reinterpret_cast<AssetHandle>(result.model.get());
    // Page zero is probed first, then consecutive pages starting at one.
    const auto zero_bytes = assets.read("guis/" + unit + "0.GUI");
    const bool page_zero = zero_bytes && !zero_bytes->empty();
    if (page_zero)
        t.simulation.flags |= gui_zero_mask;
    uint32_t page = 1;
    while (true) {
        const auto page_bytes = assets.read("guis/" + unit + std::to_string(page) + ".GUI");
        if (!page_bytes || page_bytes->empty())
            break;
        ++page;
        if (page > 4096)
            throw std::length_error("unit GUI page search limit exceeded");
    }
    t.gui_page_count = static_cast<uint8_t>(page > 1 ? page : (page_zero ? 1 : 0));
    result.script_path = "scripts/" + unit + ".COB";
    if (auto bytes = assets.read(result.script_path)) {
        auto parsed = formats::cob::parse_cob(*bytes);
        if (!parsed)
            throw std::runtime_error(
                "invalid unit script " + result.script_path + ": " + parsed.error
            );
        // The loaded file's hash is recorded for the script checks.
        parsed.program->file_hash =
            formats::tdf::buffer_hash(bytes->data(), static_cast<int32_t>(bytes->size()));
        result.script =
            std::make_shared<const formats::cob::CobProgram>(std::move(*parsed.program));
        t.cob = reinterpret_cast<AssetHandle>(result.script.get());
    }
    return result;
}

uint16_t find_type_index(std::span<const std::string> names, std::string_view name) {
    if (names.size() > 65536)
        throw std::length_error("unit type indices exceed the 16-bit representation");
    for (std::size_t i = 1; i < names.size(); ++i)
        if (equal_name(names[i], name))
            return static_cast<uint16_t>(i);
    return 0;
}
} // namespace oa::sim::unit_spawn
