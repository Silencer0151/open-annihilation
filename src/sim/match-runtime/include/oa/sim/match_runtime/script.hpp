// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "oa/sim/script_vm.hpp"
#include "oa/formats/cob.hpp"
#include "oa/sim/unit_spawn/spawn.hpp"
#include <cstdint>
#include <memory>
#include <string_view>

namespace oa::sim::match_runtime {
class SharedRandom {
  public:

    /// Seeds the shared stream every unit script and simulation draw consumes.
    ///
    /// @param seed Match seed; it is scrambled with 0x66E29572 and forced odd.
    explicit SharedRandom(uint32_t seed) noexcept : state_((seed ^ 0x66e29572u) | 1u) {}

    /// Advances the shared stream and returns a value below `bound`.
    ///
    /// @param bound Exclusive upper limit of the result.
    /// @return A value in [0, bound), or 0 without advancing the stream when
    ///     the bound read as signed is below 2.
    /// @quirk The bound is tested as a signed value, so bounds of 0x80000000
    ///     and above return 0; the step itself uses unsigned multiply and
    ///     division, as 3.1c does.
    uint32_t bounded(uint32_t bound) noexcept;

    /// Returns the current stream state, for saving or comparing.
    uint32_t state() const noexcept { return state_; }

    /// Replaces the stream state with a saved one.
    void restore(uint32_t state) noexcept { state_ = state; }

  private:

    uint32_t state_;
};

// COB scheduler ownership and the named-call wrappers. Host is a real
// model/world adapter, never an internal successful no-op implementation.
class ScriptInstance {
  public:

    /// Builds the COB interpreter for a program against a model/world host.
    ///
    /// @param program Loaded COB program; must not be null.
    /// @param host Model and world adapter the interpreter calls for piece
    ///     and unit-value operations; must outlive the instance.
    /// @param clock_scale SLEEP multiplier supplied by the owning
    ///     application (delay = scale * ms / 1000); not derived from the
    ///     simulation frequency.
    ScriptInstance(
        std::shared_ptr<const formats::cob::CobProgram> program,
        sim::script_vm::Host& host,
        int32_t clock_scale
    );
    /// Starts the first script with this exact name without writing any
    /// locals.
    ///
    /// The started context keeps whatever its locals held. Create uses this
    /// form.
    ///
    /// @param name Script name; the lookup is case-sensitive.
    /// @param immediate Runs a full scheduler step with elapsed zero right
    ///     after the start.
    /// @param callback Receives the script's RETURN value when it finishes;
    ///     never called when the start fails.
    /// @return False when no script has the name or no context is free.
    bool call_no_arguments(
        std::string_view name, bool immediate, sim::script_vm::ReturnCallback callback = {}
    );
    /// Starts the first script with this exact name with up to four
    /// arguments; omitted trailing locals are zeroed.
    ///
    /// @param name Script name; the lookup is case-sensitive.
    /// @param arguments At most four argument values; more throws.
    /// @param immediate Runs a full scheduler step with elapsed zero right
    ///     after the start.
    /// @param callback Receives the script's RETURN value when it finishes,
    ///     or zero when the name is missing or no context is free.
    /// @return False when no script has the name or no context is free.
    bool call(
        std::string_view name,
        std::span<const int32_t> arguments,
        bool immediate,
        sim::script_vm::ReturnCallback callback = {}
    );
    /// Starts the first script with this exact name with all four locals
    /// written and only the first `count` of them passed as arguments; the
    /// others stay readable as locals.
    ///
    /// @param name Script name; the lookup is case-sensitive.
    /// @param locals Values of the four locals.
    /// @param count Number of locals that are arguments, 0..4; more throws.
    /// @param immediate Runs a full scheduler step with elapsed zero right
    ///     after the start.
    /// @param callback Receives the script's RETURN value when it finishes,
    ///     or zero when the name is missing or no context is free.
    /// @return False when no script has the name or no context is free.
    bool call_with_locals(
        std::string_view name,
        const std::array<int32_t, 4>& locals,
        std::size_t count,
        bool immediate,
        sim::script_vm::ReturnCallback callback = {}
    );
    /// Runs a query script synchronously and copies its first four local
    /// slots back into `arguments`.
    ///
    /// Only the new context runs, with elapsed zero; a query that sleeps
    /// stays scheduled. A RETURN operand is not the query result.
    ///
    /// @param name Script name; the lookup is case-sensitive.
    /// @param[in,out] arguments Initial locals on entry, local slots 0..3 on
    ///     return; left unchanged when the query does not start.
    /// @return False when no script has the name or no context is free.
    bool query(std::string_view name, std::array<int32_t, 4>& arguments);
    /// Finds the first script with this exact name, as a start looks it up.
    ///
    /// @param name Script name; the lookup is case-sensitive.
    /// @return The script's COB index, or -1 when the program has none.
    [[nodiscard]] int32_t find(std::string_view name) const noexcept;
    /// Steps every context in ascending order, then integrates piece motion.
    ///
    /// @param elapsed Clock time since the last step, in the units the clock
    ///     scale converts SLEEP milliseconds into.
    void tick(uint32_t elapsed);

    /// Returns the interpreter.
    sim::script_vm::Vm& vm() noexcept { return vm_; }

    /// Returns the loaded COB program.
    const formats::cob::CobProgram& program() const noexcept { return *program_; }

  private:

    std::shared_ptr<const formats::cob::CobProgram> program_;
    sim::script_vm::Vm vm_;
};

// Services the unit-value GET/SET handlers need outside the unit record.
struct UnitValueHost {
    virtual ~UnitValueHost() = default;
    /// Returns the world position of a script piece of the unit.
    ///
    /// @param slot Unit whose piece is asked for.
    /// @param piece COB piece index.
    /// @return Signed 16.16 x, y, z bit patterns.
    virtual std::array<uint32_t, 3>
    piece_world_position(sim::unit_spawn::Slot& slot, uint32_t piece) = 0;
    /// Returns the 16-bit heading of a horizontal vector.
    ///
    /// @param x Signed x component as a bit pattern.
    /// @param z Signed z component as a bit pattern.
    /// @return Heading in 65536ths of a turn.
    virtual uint16_t direction_to(uint32_t x, uint32_t z) = 0;
    /// Returns the horizontal length of a vector, truncated to an integer.
    ///
    /// @param x Signed x component as a bit pattern.
    /// @param z Signed z component as a bit pattern.
    /// @return Length of (x, z).
    virtual uint32_t distance(uint32_t x, uint32_t z) = 0;
    /// Returns the integer terrain height at a map position.
    ///
    /// @param x Signed 16.16 map x as a bit pattern.
    /// @param z Signed 16.16 map z as a bit pattern.
    /// @return Terrain height in whole units.
    virtual int32_t sample_terrain_height(uint32_t x, uint32_t z) = 0;
    /// Sets or clears a state flag with its side effects.
    ///
    /// @param slot Unit whose flag changes.
    /// @param mask 1 for activation, 2 for armored.
    /// @param enabled True sets the flag, false clears it.
    virtual void set_activation(sim::unit_spawn::Slot& slot, uint8_t mask, bool enabled) = 0;
    /// Opens or closes a factory yard.
    ///
    /// @param slot Factory unit.
    /// @param value Nonzero opens the yard, zero closes it.
    virtual void set_yard_open(sim::unit_spawn::Slot& slot, int32_t value) = 0;
};

/// Reads a COB unit value (GET) for a unit.
///
/// @param world Canonical world the value is read from.
/// @param slot Unit running the script; must be bound to a unit.
/// @param pool Unit slot pool (unused by the lookup).
/// @param selector Unit-value selector; an unknown one reads 0.
/// @param second First extra operand of the selector.
/// @param third Second extra operand of the selector.
/// @param host Piece, direction, distance and terrain services.
/// @return The selector's value, 0 for an unknown selector.
int32_t get_unit_value(
    oa::World& world,
    sim::unit_spawn::Slot& slot,
    std::span<sim::unit_spawn::Slot> pool,
    int32_t selector,
    int32_t second,
    int32_t third,
    UnitValueHost& host
);
/// Writes a COB unit value (SET) for a unit, with the selector's flag
/// changes; every SET raises the unit's script-state-changed event.
///
/// @param slot Unit running the script; must be bound to a unit.
/// @param selector Unit-value selector.
/// @param value New value.
/// @param host Activation and yard services the selectors reach.
void set_unit_value(
    sim::unit_spawn::Slot& slot, int32_t selector, int32_t value, UnitValueHost& host
);
} // namespace oa::sim::match_runtime
