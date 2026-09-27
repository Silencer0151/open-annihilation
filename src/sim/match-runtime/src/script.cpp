// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/match_runtime/script.hpp"
#include "oa/sim/unit_script.hpp"
#include <bit>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <cstdint>

namespace oa::sim::match_runtime {
uint32_t SharedRandom::bounded(uint32_t bound) noexcept {
    if (std::bit_cast<int32_t>(bound) < 2)
        return 0;
    state_ = state_ * 0x41a7u - (state_ / 0x1f31du) * 0x7fffffffu;
    if (std::bit_cast<int32_t>(state_) < 1)
        state_ += 0x7fffffffu;
    return state_ % bound;
}

namespace {
sim::script_vm::Program
make_program(const std::shared_ptr<const formats::cob::CobProgram>& program) {
    if (!program)
        throw std::invalid_argument("script instance has no COB program");
    return {
        program->code,
        program->entry_points,
        program->header.static_variable_count,
        program->piece_names.size()
    };
}

sim::simulation_state::Unit& state(sim::unit_spawn::Slot& slot) {
    if (!slot.unit)
        throw std::invalid_argument("unit value slot is unbound");
    return *slot.unit;
}
} // namespace

ScriptInstance::ScriptInstance(
    std::shared_ptr<const formats::cob::CobProgram> program,
    sim::script_vm::Host& host,
    int32_t scale
)
    : program_(std::move(program)), vm_(make_program(program_), host, scale) {
}

bool ScriptInstance::call_no_arguments(
    std::string_view name, bool immediate, sim::script_vm::ReturnCallback callback
) {
    std::optional<uint32_t> index;
    for (std::size_t i = 0; i < program_->scripts.size(); ++i)
        if (program_->scripts[i].name == name) {
            index = static_cast<uint32_t>(i);
            break;
        }
    if (!index)
        return false;
    const auto result = vm_.start(*index, {}, std::move(callback));
    if (!result.ok()) {
        if (result.error->code == sim::script_vm::ErrorCode::no_free_context)
            return false;
        throw std::runtime_error("script start: " + result.error->message);
    }
    if (immediate)
        tick(0);
    return true;
}

bool ScriptInstance::call(
    std::string_view name,
    std::span<const int32_t> args,
    bool immediate,
    sim::script_vm::ReturnCallback callback
) {
    if (args.size() > 4)
        throw std::invalid_argument("a named script call accepts at most four arguments");
    std::array<int32_t, 4> locals{};
    std::copy(args.begin(), args.end(), locals.begin());
    return call_with_locals(name, locals, args.size(), immediate, std::move(callback));
}

bool ScriptInstance::call_with_locals(
    std::string_view name,
    const std::array<int32_t, 4>& locals,
    std::size_t count,
    bool immediate,
    sim::script_vm::ReturnCallback callback
) {
    if (count > locals.size())
        throw std::invalid_argument("a named script call accepts at most four arguments");
    std::optional<uint32_t> index;
    for (std::size_t i = 0; i < program_->scripts.size(); ++i)
        if (program_->scripts[i].name == name) {
            index = static_cast<uint32_t>(i);
            break;
        }
    if (!index) {
        if (callback)
            callback(0);
        return false;
    }
    // Keep a copy because a supplied callback is invoked with zero
    // when allocation fails. Vm::start owns the successful callback afterwards.
    const auto failed_callback = callback;
    const auto result = vm_.start_parameterized(*index, locals, count, std::move(callback));
    if (!result.ok()) {
        if (result.error->code == sim::script_vm::ErrorCode::no_free_context) {
            if (failed_callback)
                failed_callback(0);
            return false;
        }
        throw std::runtime_error("script start: " + result.error->message);
    }
    if (immediate)
        tick(0);
    return true;
}

int32_t ScriptInstance::find(std::string_view name) const noexcept {
    for (std::size_t i = 0; i < program_->scripts.size(); ++i)
        if (program_->scripts[i].name == name)
            return static_cast<int32_t>(i);
    return -1;
}

bool ScriptInstance::query(std::string_view name, std::array<int32_t, 4>& args) {
    std::optional<uint32_t> index;
    for (std::size_t i = 0; i < program_->scripts.size(); ++i)
        if (program_->scripts[i].name == name) {
            index = static_cast<uint32_t>(i);
            break;
        }
    if (!index)
        return false;
    const auto started = vm_.start(*index, args);
    if (!started.ok()) {
        if (started.error->code == sim::script_vm::ErrorCode::no_free_context)
            return false;
        throw std::runtime_error("script query start: " + started.error->message);
    }
    const auto result = vm_.tick_context(started.context, 0);
    if (!result.ok())
        throw std::runtime_error("script query: " + result.error->message);
    const auto context = vm_.context(started.context);
    for (std::size_t i = 0; i < args.size(); ++i)
        args[i] = context.slots[i];
    return true;
}

void ScriptInstance::tick(uint32_t elapsed) {
    const auto result = vm_.tick(elapsed);
    if (!result.ok())
        throw std::runtime_error("script tick: " + result.error->message);
}

namespace {
// Adapts the match's UnitValueHost to the canonical GET/SET services.
struct ValueBridge {
    UnitValueHost& host;
    sim::unit_spawn::Slot& slot;
};

oa::FixedVec3 bridge_piece_world(void* context, oa::World*, oa::Unit*, uint32_t piece) {
    auto& bridge = *static_cast<ValueBridge*>(context);
    const auto at = bridge.host.piece_world_position(bridge.slot, piece);
    return {
        std::bit_cast<int32_t>(at[0]), std::bit_cast<int32_t>(at[1]), std::bit_cast<int32_t>(at[2])
    };
}

uint16_t bridge_direction(void* context, int32_t dx, int32_t dz) {
    return static_cast<ValueBridge*>(context)->host.direction_to(
        std::bit_cast<uint32_t>(dx), std::bit_cast<uint32_t>(dz)
    );
}

uint32_t bridge_distance(void* context, int32_t dx, int32_t dz) {
    return static_cast<ValueBridge*>(context)->host.distance(
        std::bit_cast<uint32_t>(dx), std::bit_cast<uint32_t>(dz)
    );
}

int32_t bridge_ground_height(void* context, oa::World*, int32_t x, int32_t z) {
    return static_cast<ValueBridge*>(context)->host.sample_terrain_height(
        std::bit_cast<uint32_t>(x), std::bit_cast<uint32_t>(z)
    );
}

void bridge_state_flag(void* context, oa::World*, oa::Unit*, uint8_t mask, bool enabled) {
    auto& bridge = *static_cast<ValueBridge*>(context);
    bridge.host.set_activation(bridge.slot, mask, enabled);
}

void bridge_yard(void* context, oa::World*, oa::Unit*, int32_t open) {
    auto& bridge = *static_cast<ValueBridge*>(context);
    bridge.host.set_yard_open(bridge.slot, open);
}

oa::sim::unit_script::UnitValueServices services(ValueBridge& bridge) {
    return {
        &bridge,
        bridge_piece_world,
        bridge_direction,
        bridge_distance,
        bridge_ground_height,
        bridge_state_flag,
        bridge_yard
    };
}
} // namespace

int32_t get_unit_value(
    oa::World& world,
    sim::unit_spawn::Slot& slot,
    std::span<sim::unit_spawn::Slot>,
    int32_t selector,
    int32_t second,
    int32_t third,
    UnitValueHost& host
) {
    state(slot);
    ValueBridge bridge{host, slot};
    return oa::sim::unit_script::unit_script_get_value(
        &world, &slot.record, selector, second, third, services(bridge)
    );
}

void set_unit_value(
    sim::unit_spawn::Slot& slot, int32_t selector, int32_t value, UnitValueHost& host
) {
    state(slot);
    ValueBridge bridge{host, slot};
    oa::sim::unit_script::unit_script_set_value(
        nullptr, &slot.record, selector, value, services(bridge)
    );
}
} // namespace oa::sim::match_runtime
