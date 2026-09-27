// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/unit_script.hpp"

#include "oa/formats/cob.hpp"

#include <cstdlib>
#include <utility>
#include <vector>

namespace oa::sim::unit_script {
namespace {

using F = ScriptFunction;
using enum CallForm;

constexpr ScriptRun later = ScriptRun::deferred;
constexpr ScriptRun now = ScriptRun::immediate;

// Ordered as ScriptFunction. Names are the exact COB function names the game calls.
constexpr EngineCall engine_calls[script_function_count] = {
    {F::create, "Create", bare, 0, now},
    {F::killed, "Killed", query, 2, later}, // severity in, corpse type out
    {F::activate, "Activate", bare, 0, later},
    {F::deactivate, "Deactivate", bare, 0, later},
    {F::start_building, "StartBuilding", arguments, 1, later}, // build heading
    {F::stop_building, "StopBuilding", arguments, 0, later},
    {F::start_moving, "StartMoving", bare, 0, now},
    {F::stop_moving, "StopMoving", bare, 0, now},
    {F::move_rate1, "MoveRate1", bare, 0, now},
    {F::move_rate2, "MoveRate2", bare, 0, now},
    {F::move_rate3, "MoveRate3", bare, 0, now},
    {F::set_speed, "SetSpeed", arguments, 1, later},
    {F::set_direction, "SetDirection", arguments, 1, later},
    {F::set_sfx_occupy, "setSFXoccupy", arguments, 1, now},
    {F::query_build_info, "QueryBuildInfo", query, 1, later},
    {F::query_nano_piece, "QueryNanoPiece", query, 1, later},
    {F::query_primary, "QueryPrimary", query, 1, later},
    {F::query_secondary, "QuerySecondary", query, 1, later},
    {F::query_tertiary, "QueryTertiary", query, 1, later},
    {F::aim_from_primary, "AimFromPrimary", query, 1, later},
    {F::aim_from_secondary, "AimFromSecondary", query, 1, later},
    {F::aim_from_tertiary, "AimFromTertiary", query, 1, later},
    {F::aim_primary, "AimPrimary", arguments, 2, later}, // heading, pitch
    {F::aim_secondary, "AimSecondary", arguments, 2, later},
    {F::aim_tertiary, "AimTertiary", arguments, 2, later},
    {F::fire_primary, "FirePrimary", bare, 0, later},
    {F::fire_secondary, "FireSecondary", bare, 0, later},
    {F::fire_tertiary, "FireTertiary", bare, 0, later},
    {F::sweet_spot, "SweetSpot", query, 1, later},
    {F::hit_by_weapon, "HitByWeapon", arguments, 2, later}, // rock x, rock z
    {F::take_damage, "TakeDamage", arguments, 1, later},
    {F::target_cleared, "TargetCleared", arguments, 1, later},         // weapon slot
    {F::rock_unit, "RockUnit", arguments, 2, later},                   // -x, -z
    {F::set_max_reload_time, "SetMaxReloadTime", arguments, 1, later}, // milliseconds
    {F::transport_pickup, "TransportPickup", arguments, 1, now},       // unit id
    // Stack holds only the unit id, but local 1 is also written (packed drop xz).
    {F::transport_drop, "TransportDrop", arguments, 1, now},
    {F::begin_transport, "BeginTransport", arguments, 1, now},
    {F::end_transport, "EndTransport", bare, 0, later},
    {F::query_transport, "QueryTransport", query, 1, later},
    {F::query_landing_pad, "QueryLandingPad", query, 4, later},
};

constexpr bool table_is_ordered() {
    for (std::size_t i = 0; i < script_function_count; ++i)
        if (static_cast<std::size_t>(engine_calls[i].function) != i)
            return false;
    return true;
}

static_assert(table_is_ordered(), "engine call table must follow ScriptFunction order");

constexpr ScriptFunction query_by_slot[weapon_slot_count] = {
    F::query_primary, F::query_secondary, F::query_tertiary
};
constexpr ScriptFunction aim_from_by_slot[weapon_slot_count] = {
    F::aim_from_primary, F::aim_from_secondary, F::aim_from_tertiary
};
constexpr ScriptFunction aim_by_slot[weapon_slot_count] = {
    F::aim_primary, F::aim_secondary, F::aim_tertiary
};
constexpr ScriptFunction fire_by_slot[weapon_slot_count] = {
    F::fire_primary, F::fire_secondary, F::fire_tertiary
};
constexpr ScriptFunction move_rate_by_rate[4] = {
    F::stop_moving, F::move_rate1, F::move_rate2, F::move_rate3
};

ScriptFunction by_slot(const ScriptFunction (&table)[weapon_slot_count], uint32_t slot) {
    return table[slot < weapon_slot_count ? slot : 0];
}

// World -> side table. A World gains an entry with its first attached script
// and loses it with its last, so a destroyed World never leaves a stale entry.
struct Binding {
    const World* world{};
    UnitScript* scripts{};
    uint32_t slot_count{};
    uint32_t attached{};
};

constexpr std::size_t binding_capacity = 16;
Binding bindings[binding_capacity];

Binding* find_binding(const World* world) {
    for (auto& binding : bindings)
        if (binding.world == world && world != nullptr)
            return &binding;
    return nullptr;
}

uint32_t slot_of(const World* world, const Unit* unit) {
    if (world == nullptr || unit == nullptr || world->units == nullptr || unit < world->units)
        return 0;
    const auto slot = static_cast<uint32_t>(unit - world->units);
    return slot < world->unit_slot_count ? slot : 0;
}

ScriptCallStatus status_of(const std::optional<sim::script_vm::Error>& error, UnitScript& script) {
    if (!error)
        return ScriptCallStatus::started;
    if (error->code == sim::script_vm::ErrorCode::no_free_context)
        return ScriptCallStatus::no_free_context;
    script.last_fault = error->code;
    return ScriptCallStatus::fault;
}

ScriptCallStatus run_now(UnitScript& script) {
    return status_of(script.vm->tick(0).error, script);
}

ScriptCallStatus start_with_arguments(
    UnitScript* script,
    int32_t index,
    ScriptArgs args,
    ScriptRun run,
    sim::script_vm::ReturnCallback callback
) {
    if (script == nullptr)
        return ScriptCallStatus::no_script;
    const auto fail = [&](ScriptCallStatus status) {
        if (callback)
            callback(0);
        return status;
    };
    if (index < 0)
        return fail(ScriptCallStatus::no_function);
    if (args.count > max_arguments)
        args.count = max_arguments;
    const std::array<int32_t, 4> locals{
        args.values[0], args.values[1], args.values[2], args.values[3]
    };
    // The interpreter takes the callback only when a context is claimed.
    auto on_failure = callback;
    const auto started = script->vm->start_parameterized(
        static_cast<uint32_t>(index), locals, args.count, std::move(callback)
    );
    if (!started.ok()) {
        const auto status = status_of(started.error, *script);
        if (status == ScriptCallStatus::no_free_context && on_failure)
            on_failure(0);
        return status;
    }
    return run == ScriptRun::immediate ? run_now(*script) : ScriptCallStatus::started;
}

int32_t index_of(const UnitScript* script, ScriptFunction function) {
    return script->function_index[static_cast<std::size_t>(function)];
}

} // namespace

const EngineCall& engine_call(ScriptFunction function) noexcept {
    const auto index = static_cast<std::size_t>(function);
    return engine_calls[index < script_function_count ? index : 0];
}

ScriptFunction weapon_query_function(uint32_t slot) noexcept {
    return by_slot(query_by_slot, slot);
}

ScriptFunction weapon_aim_from_function(uint32_t slot) noexcept {
    return by_slot(aim_from_by_slot, slot);
}

ScriptFunction weapon_aim_function(uint32_t slot) noexcept {
    return by_slot(aim_by_slot, slot);
}

ScriptFunction weapon_fire_function(uint32_t slot) noexcept {
    return by_slot(fire_by_slot, slot);
}

ScriptFunction move_rate_function(uint32_t rate) noexcept {
    return move_rate_by_rate[rate < 4 ? rate : 0];
}

bool unit_script_attach(
    World* world, const Unit* unit, sim::script_vm::Vm* vm, const formats::cob::CobProgram& program
) {
    const auto slot = slot_of(world, unit);
    if (slot == 0 || vm == nullptr)
        return false;
    auto* binding = find_binding(world);
    if (binding == nullptr) {
        for (auto& candidate : bindings) {
            if (candidate.world != nullptr)
                continue;
            auto* scripts =
                static_cast<UnitScript*>(std::calloc(world->unit_slot_count, sizeof(UnitScript)));
            if (scripts == nullptr)
                return false;
            candidate = {world, scripts, world->unit_slot_count, 0};
            binding = &candidate;
            break;
        }
        if (binding == nullptr)
            return false;
    }
    if (slot >= binding->slot_count)
        return false;
    auto& script = binding->scripts[slot];
    if (script.vm == nullptr)
        ++binding->attached;
    script.vm = vm;
    script.last_fault = sim::script_vm::ErrorCode::none;
    // A COB's name table never changes, so each engine call finds the same
    // index every time; the indices are resolved once here.
    std::vector<const char*> names;
    names.reserve(program.scripts.size());
    for (const auto& entry : program.scripts)
        names.push_back(entry.name.c_str());
    for (std::size_t i = 0; i < script_function_count; ++i)
        script.function_index[i] = sim::script_vm::find_script(names, engine_calls[i].name);
    return true;
}

void unit_script_detach(World* world, const Unit* unit, const sim::script_vm::Vm* vm) {
    auto* binding = find_binding(world);
    const auto slot = slot_of(world, unit);
    if (binding == nullptr || slot == 0 || slot >= binding->slot_count ||
        binding->scripts[slot].vm == nullptr || (vm != nullptr && binding->scripts[slot].vm != vm))
        return;
    binding->scripts[slot] = {};
    if (--binding->attached == 0) {
        std::free(binding->scripts);
        *binding = {};
    }
}

UnitScript* unit_script_of(World* world, const Unit* unit) {
    auto* binding = find_binding(world);
    const auto slot = slot_of(world, unit);
    if (binding == nullptr || slot == 0 || slot >= binding->slot_count)
        return nullptr;
    auto* script = &binding->scripts[slot];
    return script->vm != nullptr ? script : nullptr;
}

ScriptCallStatus unit_script_call(
    World* world,
    Unit* unit,
    ScriptFunction function,
    ScriptArgs args,
    ScriptRun run,
    sim::script_vm::ReturnCallback callback
) {
    auto* script = unit_script_of(world, unit);
    if (script == nullptr)
        return ScriptCallStatus::no_script;
    return start_with_arguments(script, index_of(script, function), args, run, std::move(callback));
}

ScriptCallStatus unit_script_call_index(
    World* world,
    Unit* unit,
    int32_t script_index,
    ScriptArgs args,
    ScriptRun run,
    sim::script_vm::ReturnCallback callback
) {
    auto* script = unit_script_of(world, unit);
    if (script == nullptr)
        return ScriptCallStatus::no_script;
    return start_with_arguments(script, script_index, args, run, std::move(callback));
}

ScriptCallStatus unit_script_call_bare(
    World* world,
    Unit* unit,
    ScriptFunction function,
    ScriptRun run,
    sim::script_vm::ReturnCallback callback
) {
    auto* script = unit_script_of(world, unit);
    if (script == nullptr)
        return ScriptCallStatus::no_script;
    const auto index = index_of(script, function);
    if (index < 0)
        return ScriptCallStatus::no_function;
    const auto started = script->vm->start(static_cast<uint32_t>(index), {}, std::move(callback));
    if (!started.ok())
        return status_of(started.error, *script);
    return run == ScriptRun::immediate ? run_now(*script) : ScriptCallStatus::started;
}

ScriptCallStatus unit_script_query(
    World* world,
    Unit* unit,
    ScriptFunction function,
    int32_t* first,
    int32_t* second,
    int32_t* third,
    int32_t* fourth
) {
    auto* script = unit_script_of(world, unit);
    if (script == nullptr)
        return ScriptCallStatus::no_script;
    const auto index = index_of(script, function);
    if (index < 0)
        return ScriptCallStatus::no_function;
    const auto result =
        script->vm->query(static_cast<uint32_t>(index), {first, second, third, fourth});
    return status_of(result.error, *script);
}

ScriptCallStatus unit_script_tick(World* world, Unit* unit, uint32_t elapsed) {
    auto* script = unit_script_of(world, unit);
    if (script == nullptr)
        return ScriptCallStatus::no_script;
    return status_of(script->vm->tick(elapsed).error, *script);
}

uint32_t unit_script_is_carrying(const World* world, const Unit* unit, uint32_t unit_id) noexcept {
    if (world == nullptr || unit == nullptr)
        return 0;
    // The walk is bounded by the unit table, so a cycle ends it.
    auto remaining = world->unit_slot_count;
    for (const Unit* child = world_unit(world, unit->attach_first_child);
         child != nullptr && remaining != 0;
         child = world_unit(world, child->attach_next), --remaining) {
        if (child->id == unit_id)
            return 1;
    }
    return 0;
}

uint32_t unit_script_carrier_id(const World* world, const Unit* unit) noexcept {
    if (world == nullptr || unit == nullptr)
        return 0;
    const Unit* carrier = world_unit(world, unit->attach_parent);
    return carrier != nullptr ? carrier->id : 0;
}

} // namespace oa::sim::unit_script
