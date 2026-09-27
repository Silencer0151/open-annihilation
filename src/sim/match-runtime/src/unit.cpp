// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/match_runtime/unit.hpp"
#include "oa/sim/unit_script.hpp"
#include <algorithm>
#include <bit>
#include <cstdint>
#include <stdexcept>

namespace oa::sim::match_runtime {
UnitHost::UnitHost(
    sim::model_runtime::Instance& instance,
    sim::unit_spawn::Slot& slot,
    std::span<sim::unit_spawn::Slot> pool,
    oa::World& state,
    UnitValueHost& values,
    Effects& effects,
    SharedRandom& random
)
    : ModelHost(instance), slot_(slot), pool_(pool), state_(state), values_(values),
      effects_(effects), random_(random) {
    if (slot_.unit_index >= state_.unit_slot_count || state_.unit_slot_count != pool_.size())
        throw std::invalid_argument("unit host attachment table does not match pool");
}

void UnitHost::emit_sfx(uint32_t piece, int32_t effect) {
    effects_.emit_sfx(slot_, piece, effect);
}

void UnitHost::explode(uint32_t piece, int32_t flags) {
    effects_.explode_piece(slot_, piece, flags);
}

int32_t
UnitHost::get_unit_value(int32_t selector, int32_t second, int32_t third, int32_t, int32_t) {
    return sim::match_runtime::get_unit_value(
        state_, slot_, pool_, selector, second, third, values_
    );
}

void UnitHost::set_unit_value(int32_t selector, int32_t value) {
    sim::match_runtime::set_unit_value(slot_, selector, value, values_);
}

void UnitHost::attach_unit(int32_t first, int32_t second, int32_t third) {
    effects_.attach_unit(slot_, first, second, third);
}

void UnitHost::drop_unit(int32_t target) {
    effects_.drop_unit(slot_, target);
}

void UnitHost::ignored_piece_op(uint32_t, uint32_t, uint32_t) {
    // The game's unit script host ignores this piece operation.
}

void UnitHost::dont_shadow(uint32_t) {
    // The game's unit script host ignores DONT-SHADOW.
}

uint32_t UnitHost::is_carrying_unit(uint32_t value) {
    return oa::sim::unit_script::unit_script_is_carrying(&state_, &slot_.record, value);
}

uint32_t UnitHost::carrier_unit_id() {
    return oa::sim::unit_script::unit_script_carrier_id(&state_, &slot_.record);
}

uint32_t UnitHost::random_bounded(uint32_t bound) {
    return random_.bounded(bound);
}

namespace {
sim::model_runtime::Instance
construct_model(const sim::unit_spawn::LoadedType& loaded, sim::unit_spawn::Slot& slot) {
    std::vector<std::string_view> names;
    if (loaded.script)
        for (const auto& name : loaded.script->piece_names)
            names.push_back(name);
    return sim::model_runtime::make_instance(
        loaded.model, reinterpret_cast<uintptr_t>(slot.unit), names
    );
}
} // namespace

UnitInstance::UnitInstance(
    const sim::unit_spawn::LoadedType& loaded,
    sim::unit_spawn::Slot& slot,
    std::span<sim::unit_spawn::Slot> pool,
    oa::World& state,
    UnitValueHost& values,
    Effects& effects,
    SharedRandom& random,
    int32_t scale
)
    : slot_(slot), world_(state), model_(construct_model(loaded, slot)),
      host_(model_, slot, pool, state, values, effects, random) {
    if (!slot.unit)
        throw std::invalid_argument("unit instance bound to null unit");
    if (loaded.script) {
        script_ = std::make_unique<ScriptInstance>(loaded.script, host_, scale);
        // Engine callers reach this interpreter through the World's script table.
        oa::sim::unit_script::unit_script_attach(
            &world_, &slot_.record, &script_->vm(), *loaded.script
        );
    }
}

UnitInstance::~UnitInstance() {
    if (script_)
        oa::sim::unit_script::unit_script_detach(&world_, &slot_.record, &script_->vm());
}

bool UnitInstance::create() {
    return script_ && script_->call_no_arguments("Create", true);
}

uint32_t UnitInstance::query_weapon_piece(uint8_t slot) {
    constexpr std::array names{"QueryPrimary", "QuerySecondary", "QueryTertiary"};
    if (slot >= names.size())
        throw std::out_of_range("weapon query slot outside 0..2");
    if (!script_)
        throw std::runtime_error("weapon query requires COB instance");
    std::array<int32_t, 4> args{};
    script_->query(names[slot], args);
    return std::bit_cast<uint32_t>(args[0]);
}

std::array<uint32_t, 3> UnitInstance::query_weapon_world(uint8_t slot) {
    return piece_world(query_weapon_piece(slot));
}

std::array<uint32_t, 3> UnitInstance::query_nano_world() {
    if (!script_ || !slot_.unit)
        return slot_.unit ? slot_.unit->position : std::array<uint32_t, 3>{};
    std::array<int32_t, 4> args{};
    script_->query("QueryNanoPiece", args);
    return piece_world(std::bit_cast<uint32_t>(args[0]));
}

std::array<uint32_t, 3> UnitInstance::sweet_spot_world() {
    std::array<int32_t, 4> args{};
    if (script_)
        script_->query("SweetSpot", args);
    return piece_box_center(std::bit_cast<uint32_t>(args[0]));
}

std::array<uint32_t, 3> UnitInstance::aim_from_world(uint8_t slot) {
    constexpr std::array names{"AimFromPrimary", "AimFromSecondary", "AimFromTertiary"};
    if (slot >= names.size())
        throw std::out_of_range("weapon aim slot outside 0..2");
    if (!script_)
        throw std::runtime_error("weapon aim requires COB instance");
    std::array<int32_t, 4> args{-1, 0, 0, 0};
    script_->query(names[slot], args);
    return piece_world(args[0] == -1 ? query_weapon_piece(slot) : std::bit_cast<uint32_t>(args[0]));
}

void UnitInstance::set_max_reload_time(int32_t milliseconds) {
    if (script_)
        script_->call("SetMaxReloadTime", std::span(&milliseconds, 1), false);
}

void UnitInstance::tick(uint32_t elapsed) {
    if (script_)
        script_->tick(elapsed);
}

std::array<uint32_t, 3> UnitInstance::piece_box_center(uint32_t piece) const {
    const auto position = slot_.unit->position;
    if (piece >= model_.pieces().size())
        return position;
    int32_t low_x = 0, high_x = 0, low_y = 0, high_y = 0, low_z = 0, high_z = 0;
    for (const auto& vertex : model_.pieces()[piece].transformed_vertices) {
        low_x = std::min(low_x, vertex.x);
        high_x = std::max(high_x, vertex.x);
        low_y = std::min(low_y, vertex.y);
        high_y = std::max(high_y, vertex.y);
        low_z = std::min(low_z, vertex.z);
        high_z = std::max(high_z, vertex.z);
    }
    return {
        position[0] + std::bit_cast<uint32_t>((high_x + low_x) / 2),
        position[1] + std::bit_cast<uint32_t>((high_y + low_y) / 2),
        position[2] + std::bit_cast<uint32_t>((high_z + low_z) / 2)
    };
}

sim::model_runtime::RotationWords UnitInstance::piece_attitude(uint32_t piece) const {
    const auto& rotation = model_.piece_for_script_index(piece).rotation;
    return {
        static_cast<int16_t>(slot_.record.bank + rotation.xy),
        static_cast<int16_t>(slot_.yaw + static_cast<uint16_t>(rotation.xz)),
        static_cast<int16_t>(slot_.record.pitch + rotation.yz)
    };
}

std::array<uint32_t, 3> UnitInstance::piece_world(uint32_t piece) const {
    if (std::bit_cast<int32_t>(piece) < 0 || piece >= model_.pieces().size())
        return slot_.unit->position;
    const auto local = model_.attachment_position(
        piece, {slot_.record.bank, std::bit_cast<int16_t>(slot_.yaw), slot_.record.pitch}
    );
    return {
        slot_.unit->position[0] + static_cast<uint32_t>(local.x),
        slot_.unit->position[1] + static_cast<uint32_t>(local.y),
        slot_.unit->position[2] + static_cast<uint32_t>(local.z)
    };
}
} // namespace oa::sim::match_runtime
