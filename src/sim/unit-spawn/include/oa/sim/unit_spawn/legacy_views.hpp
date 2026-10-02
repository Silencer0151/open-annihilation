// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Transitional reference views over the canonical oa::World records.
//
// The per-module unit, player and world copies have been removed; these types
// keep their old member names for code that has not moved to oa::Unit,
// oa::Player and oa::World yet (including src/app). Each view holds references
// into the canonical records or a native side table and owns no state, so a
// write through a view is a write to the World. Views are bound once by
// LegacyViews and must not outlive it. Delete a member once nothing reads it.
//
// The canonical records are packed, so some of their fields sit at addresses
// their types do not align to. A member over such a field is a Field (or
// Words3), which reads and writes the field's bytes; the members over fields
// their types align to stay plain references.
#pragma once

#include "oa/sim/unit_spawn/spawn.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

namespace oa::sim::unit_spawn::legacy {
/// Reinterprets a canonical integer field as the same-sized type of the other signedness.
///
/// Only for a field at an address its type aligns to; bind a Field otherwise.
///
/// @param field canonical field
/// @return the field seen as `To`
template <class To, class From>
To& as(From& field) noexcept {
    static_assert(sizeof(To) == sizeof(From));
    return reinterpret_cast<To&>(field);
}

// Legacy `T&` member backed by a canonical field at an address `T` does not
// align to. It reads and writes the field's bytes, so it never binds a `T&` to
// the field. Like a reference it writes through a const view, and assigning
// one Field to another copies the value. It converts to `T` but cannot be
// copied: a local that keeps the value is declared as `T`, not `auto`.
template <class T>
class Field {
    static_assert(std::is_trivially_copyable_v<T>);

  public:

    /// Binds the member to a field of a canonical record.
    ///
    /// @param record canonical record
    /// @param member the record's field, the size of `T` (its signedness may differ)
    template <class Record, class Member>
    Field(Record& record, Member Record::* member) noexcept
        : bytes_(reinterpret_cast<unsigned char*>(&(record.*member))) {
        static_assert(sizeof(Member) == sizeof(T));
    }

    Field(const Field&) = delete;
    Field(Field&&) = default;

    /// Returns the field's value.
    ///
    /// @return the value
    operator T() const noexcept {
        T value{};
        std::memcpy(&value, bytes_, sizeof(T));
        return value;
    }

    /// Stores a value in the field.
    ///
    /// @param value value stored
    /// @return this member
    const Field& operator=(T value) const noexcept {
        std::memcpy(bytes_, &value, sizeof(T));
        return *this;
    }

    /// Stores another member's value in the field.
    ///
    /// @param other member whose value is stored
    /// @return this member
    const Field& operator=(const Field& other) const noexcept {
        return *this = static_cast<T>(other);
    }

    // A swap moves a member into a temporary that still names the same bytes,
    // so moving one member into another does not compile.
    const Field& operator=(Field&&) const = delete;

    /// Adds to the field, as `+=` on a `T` does.
    ///
    /// @param operand value added
    /// @return this member
    template <class U>
    const Field& operator+=(U operand) const noexcept {
        T value = *this;
        value += operand;
        return *this = value;
    }

    /// Subtracts from the field, as `-=` on a `T` does.
    ///
    /// @param operand value subtracted
    /// @return this member
    template <class U>
    const Field& operator-=(U operand) const noexcept {
        T value = *this;
        value -= operand;
        return *this = value;
    }

    /// Multiplies the field, as `*=` on a `T` does.
    ///
    /// @param operand factor
    /// @return this member
    template <class U>
    const Field& operator*=(U operand) const noexcept {
        T value = *this;
        value *= operand;
        return *this = value;
    }

    /// Divides the field, as `/=` on a `T` does.
    ///
    /// @param operand divisor
    /// @return this member
    template <class U>
    const Field& operator/=(U operand) const noexcept {
        T value = *this;
        value /= operand;
        return *this = value;
    }

    /// Sets bits of the field, as `|=` on a `T` does.
    ///
    /// @param operand bits set
    /// @return this member
    template <class U>
    const Field& operator|=(U operand) const noexcept {
        T value = *this;
        value |= operand;
        return *this = value;
    }

    /// Keeps bits of the field, as `&=` on a `T` does.
    ///
    /// @param operand bits kept
    /// @return this member
    template <class U>
    const Field& operator&=(U operand) const noexcept {
        T value = *this;
        value &= operand;
        return *this = value;
    }

    /// Flips bits of the field, as `^=` on a `T` does.
    ///
    /// @param operand bits flipped
    /// @return this member
    template <class U>
    const Field& operator^=(U operand) const noexcept {
        T value = *this;
        value ^= operand;
        return *this = value;
    }

    /// Adds one to the field, as `++` on a `T` does.
    ///
    /// @return this member
    const Field& operator++() const noexcept {
        T value = *this;
        ++value;
        return *this = value;
    }

    /// Subtracts one from the field, as `--` on a `T` does.
    ///
    /// @return this member
    const Field& operator--() const noexcept {
        T value = *this;
        --value;
        return *this = value;
    }

    /// Adds one to the field, as `++` on a `T` does.
    ///
    /// @return the value before the increment
    T operator++(int) const noexcept {
        T value = *this;
        T incremented = value;
        ++incremented;
        *this = incremented;
        return value;
    }

    /// Subtracts one from the field, as `--` on a `T` does.
    ///
    /// @return the value before the decrement
    T operator--(int) const noexcept {
        T value = *this;
        T decremented = value;
        --decremented;
        *this = decremented;
        return value;
    }

    /// The member has no address that points at a `T`.
    void operator&() const = delete;

  private:

    unsigned char* bytes_{};
};

// Legacy `std::array<uint32_t, 3>&` member backed by a canonical FixedVec3 at
// an address uint32_t does not align to. Each component is the bit pattern of
// its 16.16 value, read and written through a Field.
class Words3 {
  public:

    /// Binds the member to a canonical vector.
    ///
    /// @param vector canonical vector
    explicit Words3(oa::FixedVec3& vector) noexcept
        : components_{
              {Field<uint32_t>(vector, &oa::FixedVec3::x),
               Field<uint32_t>(vector, &oa::FixedVec3::y),
               Field<uint32_t>(vector, &oa::FixedVec3::z)}
          } {}

    Words3(const Words3&) = delete;
    Words3(Words3&&) = default;

    /// Returns a component.
    ///
    /// @param index 0 for x, 1 for y, 2 for z
    /// @return the component
    const Field<uint32_t>& operator[](std::size_t index) const noexcept {
        return components_[index];
    }

    /// Returns the three components' values.
    ///
    /// @return x, y and z
    operator std::array<uint32_t, 3>() const noexcept {
        return {components_[0], components_[1], components_[2]};
    }

    /// Stores three components.
    ///
    /// @param words x, y and z
    /// @return this member
    const Words3& operator=(const std::array<uint32_t, 3>& words) const noexcept {
        for (std::size_t i = 0; i < components_.size(); ++i)
            components_[i] = words[i];
        return *this;
    }

    /// Stores another member's components.
    ///
    /// @param other member whose components are stored
    /// @return this member
    const Words3& operator=(const Words3& other) const noexcept {
        return *this = static_cast<std::array<uint32_t, 3>>(other);
    }

    // A swap moves a member into a temporary that still names the same bytes,
    // so moving one member into another does not compile.
    const Words3& operator=(Words3&&) const = delete;

    /// Tests whether the components hold the given values.
    ///
    /// @param words x, y and z
    /// @return true when all three are equal
    bool operator==(const std::array<uint32_t, 3>& words) const noexcept {
        return static_cast<std::array<uint32_t, 3>>(*this) == words;
    }

  private:

    std::array<Field<uint32_t>, 3> components_;
};

// Span of views with the bounds-checked at() the replaced vectors offered.
template <class T>
class Span : public std::span<T> {
  public:

    using std::span<T>::span;

    /// Wraps a span of views.
    ///
    /// @param other views to wrap
    Span(std::span<T> other) noexcept : std::span<T>(other) {}

    /// Returns the view at an index, bounds-checked like the replaced vectors.
    ///
    /// An index past the span gives the view at index 0, the reserved slot 0
    /// of a unit pool, which holds no unit; the span holds at least that view.
    ///
    /// @param index position in the span
    /// @return the view
    T& at(std::size_t index) const noexcept {
        return index < this->size() ? (*this)[index] : this->front();
    }
};

// Pointer-valued legacy member backed by an oa_ref32 into a table of views.
template <class T>
class RefField {
  public:

    /// Binds a pointer-valued member to a canonical reference and its table.
    ///
    /// @param ref canonical oa_ref32 field (0 null, else index + 1)
    /// @param table views the reference indexes
    RefField(Field<oa_ref32> ref, std::span<T> table) noexcept
        : ref_(std::move(ref)), table_(table) {}

    RefField(const RefField&) = delete;
    RefField(RefField&&) = default;

    /// Points the reference at a view of its table, or clears it.
    ///
    /// A view outside the table, which the reference cannot hold, clears it too.
    ///
    /// @param target view in the table, or null
    /// @return this field
    RefField& operator=(T* target) noexcept {
        if (target && target >= table_.data() && target < table_.data() + table_.size())
            ref_ = static_cast<oa_ref32>(target - table_.data()) + 1u;
        else
            ref_ = 0;
        return *this;
    }

    /// Returns the referenced view, or null.
    operator T*() const noexcept { return ref_ ? &table_[ref_ - 1u] : nullptr; }

    /// Returns the referenced view.
    T* operator->() const noexcept { return *this; }

  private:

    Field<oa_ref32> ref_;
    std::span<T> table_;
};

// Legacy `UnitType*` member backed by Unit.def (an index into the type table).
class TypeField {
  public:

    /// Binds a `UnitType*` member to Unit.def and the runtime type table.
    ///
    /// @param ref the unit's def reference
    /// @param types runtime types, indexed like World.unit_defs
    TypeField(Field<oa_ref32> ref, std::span<sim::unit_spawn::Type> types) noexcept
        : ref_(std::move(ref)), types_(types) {}

    TypeField(const TypeField&) = delete;
    TypeField(TypeField&&) = default;

    /// Points Unit.def at the runtime type owning `target`, or clears it.
    ///
    /// A type outside the table, which Unit.def cannot hold, clears it too.
    ///
    /// @param target simulation fields of a runtime type, or null
    /// @return this field
    TypeField& operator=(sim::simulation_state::UnitType* target);
    /// Returns the simulation fields of the referenced type.
    ///
    /// @return the fields, or null for no type or an index past the table
    operator sim::simulation_state::UnitType*() const noexcept;

    /// Returns the simulation fields of the referenced type.
    sim::simulation_state::UnitType* operator->() const noexcept { return *this; }

  private:

    Field<oa_ref32> ref_;
    std::span<sim::unit_spawn::Type> types_;
};

// Legacy `bool` member backed by one bit of a canonical byte.
class BitField {
  public:

    /// Binds a `bool` member to bits of a canonical byte.
    ///
    /// @param byte canonical byte
    /// @param mask bits the member stands for
    BitField(uint8_t& byte, uint8_t mask) noexcept : byte_(byte), mask_(mask) {}

    BitField(const BitField&) = delete;
    BitField(BitField&&) = default;

    /// Sets or clears the bits.
    ///
    /// @param value true sets, false clears
    /// @return this field
    BitField& operator=(bool value) noexcept {
        byte_ = static_cast<uint8_t>(value ? byte_ | mask_ : byte_ & ~mask_);
        return *this;
    }

    /// Returns whether any of the bits is set.
    operator bool() const noexcept { return (byte_ & mask_) != 0; }

  private:

    uint8_t& byte_;
    uint8_t mask_;
};
} // namespace oa::sim::unit_spawn::legacy

namespace oa::sim::simulation_state {
struct Player;

// View of oa::Unit under the old simulation-state member names.
struct Unit {
    /// Binds the view to a unit record and its side tables.
    ///
    /// @param record canonical unit
    /// @param orders the unit's order lists
    /// @param types runtime types, for the type member
    /// @param players player views, for the owner member
    Unit(
        oa::Unit& record,
        OrderQueue& orders,
        std::span<sim::unit_spawn::Type> types,
        std::span<Player> players
    ) noexcept;
    Unit(const Unit&) = delete;
    Unit(Unit&&) = default;

    oa::Unit& record;
    OrderQueue& orders;
    uint32_t& object_present;                                  // Unit.movement
    Order*& primary;                                           // OrderQueue.primary
    Order*& secondary;                                         // OrderQueue.secondary
    sim::unit_spawn::legacy::Words3 position;                  // Unit.position as 16.16 words
    sim::unit_spawn::legacy::TypeField type;                   // Unit.def
    sim::unit_spawn::legacy::RefField<Player> owner;           // Unit.owner
    sim::unit_spawn::legacy::Field<uint32_t> script_present;   // Unit.script
    int16_t& type_index;                                       // Unit.type_index
    int32_t& squad;                                            // Unit.squad
    uint16_t& events;                                          // Unit.events
    uint8_t& damage_kind;                                      // Unit.damage_kind
    uint8_t& health_percent;                                   // Unit.health_percent
    uint8_t& previous_health_percent;                          // Unit.previous_health_percent
    uint8_t& damage_countdown;                                 // Unit.damage_countdown
    sim::unit_spawn::legacy::Field<uint32_t> capture_cooldown; // Unit.capture_cooldown
    int16_t& health;                                           // Unit.health
    uint8_t& state_flags;                                      // Unit.state_flags
    uint32_t& flags;                                           // Unit.flags
};

// View of oa::Player under the old simulation-state member names.
struct Player {
    /// Binds the view to a player record; the units span is set when the pool is bound.
    ///
    /// @param record canonical player
    explicit Player(oa::Player& record) noexcept;
    Player(const Player&) = delete;
    Player(Player&&) = default;

    oa::Player& record;
    sim::unit_spawn::legacy::Field<uint32_t> present;      // Player.in_use
    sim::unit_spawn::legacy::Field<int32_t> machine_group; // Player.machine_group
    uint8_t& status;                                       // Player.status
    uint8_t& index;                                        // Player.index
    std::span<Unit> units; // inclusive first_unit..last_unit, bound with the pool
};

// View of oa::World (Game fields) under the old simulation-state member names.
struct World {
    /// Binds the view to a world's game fields and its ten players.
    ///
    /// @param record canonical world
    explicit World(oa::World& record) noexcept;
    World(const World&) = delete;

    oa::World& record;
    std::array<Player, 10> players;
    sim::unit_spawn::legacy::Field<uint32_t> tick;              // Game.tick
    sim::unit_spawn::legacy::Field<uint32_t> active_units;      // Game.active_unit_count
    uint8_t& sea_level;                                         // Game.sea_level
    sim::unit_spawn::legacy::BitField run_flag;                 // Game.session_flags bit 0
    sim::unit_spawn::legacy::BitField periodic_flag;            // Game.periodic_flags bit 1
    sim::unit_spawn::legacy::Field<int16_t> periodic_countdown; // Game.periodic_countdown
    uint32_t& environment_enabled;                              // World.environment_enabled
    int32_t& environment_damage;                                // World.environment_damage
};

// View forms of the canonical order and ownership functions.

/// Tests whether a player view's record is live (player_active on the record).
///
/// @param player player view
/// @return true when active
[[nodiscard]] inline bool player_active(const Player& player) noexcept {
    return player_active(player.record);
}

/// Tests whether a player slot below ten holds a live record.
///
/// @param index player slot
/// @param player the slot's view
/// @return true for a slot below 10 whose record is active
[[nodiscard]] inline bool player_slot_active(uint8_t index, const Player& player) noexcept {
    return player_slot_active(index, player.record);
}

/// Tests whether a unit's owner is present and simulated here (status 1 or 2).
///
/// @param unit unit view
/// @return true for a local or computer owner
[[nodiscard]] inline bool locally_simulated(const Unit& unit) noexcept {
    const Player* owner = unit.owner;
    return owner && owner->present && (owner->status == 1 || owner->status == 2);
}

/// Unlinks an order from a unit's queue and hands it to the host to destroy.
///
/// @param unit unit view
/// @param order order to remove
/// @param host destroys the order
/// @return as sim::simulation_state::remove_order
inline StepFault remove_order(Unit& unit, Order& order, Host& host) {
    return remove_order(unit.orders, unit.record, order, host);
}

/// Destroys a unit's orders (see sim::simulation_state::clear_orders).
///
/// @param unit unit view
/// @param all true for every order, false for the primary orders without preserve bit 2
/// @param host destroys the orders
/// @return as sim::simulation_state::clear_orders
inline StepFault clear_orders(Unit& unit, bool all, Host& host) {
    return clear_orders(unit.orders, unit.record, all, host);
}

/// Finds a unit's first order of a kind.
///
/// @param unit unit view
/// @param kind mission kind
/// @return the order, or null
[[nodiscard]] inline Order* find_order(const Unit& unit, uint8_t kind) noexcept {
    return find_order(unit.orders, kind);
}
} // namespace oa::sim::simulation_state

namespace oa::sim::unit_spawn {
// View of oa::Unit under the old spawn-slot member names, plus the slot's native
// asset handles.
struct Slot {
    /// Binds the view to a unit view and the slot's asset handles.
    ///
    /// @param unit unit view of the slot
    /// @param assets the slot's native asset handles
    Slot(sim::simulation_state::Unit& unit, SlotAssets& assets) noexcept;
    Slot(const Slot&) = delete;
    Slot(Slot&&) = default;

    oa::Unit& record;
    sim::simulation_state::Unit* unit;
    uint16_t& unit_index;                  // Unit.id
    uint32_t& squad;                       // Unit.squad
    uint32_t& flags2;                      // Unit.flags2
    uint32_t& cleared_on_unfinished_spawn; // Unit.cleared_on_unfinished_spawn
    const float& build_remaining;          // Unit.build_remaining; read by src/app
    uint32_t& decloak_until_tick;          // Unit.decloak_until_tick
    uint32_t& last_attacker_id;            // Unit.last_attacker_id
    uint16_t& bank;                        // Unit.bank
    uint16_t& yaw;                         // Unit.heading
    uint16_t& pitch;                       // Unit.pitch
    uint16_t& sight_center_x;              // Unit.sight_center_x
    uint16_t& sight_center_z;              // Unit.sight_center_z
    uint16_t& bob_phase;                   // Unit.bob_phase
    uint16_t& veteran_level;               // Unit.veteran_level
    uint8_t& state_flags;                  // Unit.state_flags
    uint8_t& build_flags;                  // Unit.build_flags
    uint8_t& last_attacker_owner;          // Unit.last_attacker_owner
    uint8_t& sight_band;                   // Unit.sight_band
    uint8_t& attach_piece;                 // Unit.attach_piece
    uint8_t& owner_index;                  // Unit.owner_index
    std::array<bool, 3>& weapon_slots_initialized;
    AssetHandle& script_instance;
    AssetHandle& model_instance;
    AssetHandle& movement_object;
};

// View of oa::Player under the old spawn player-range member names.
struct PlayerRange {
    /// Binds the view to a player record and its setup record.
    ///
    /// @param world world the player belongs to (unused)
    /// @param record canonical player
    /// @param setup the player's setup record
    PlayerRange(oa::World& world, oa::Player& record, PlayerSetupState& setup) noexcept;
    PlayerRange(const PlayerRange&) = delete;
    PlayerRange(PlayerRange&&) = default;

    oa::Player& record;
    std::size_t first{}, count{};           // bound with the pool from first_unit/last_unit
    legacy::Field<uint16_t> current_count;  // Player.unit_count
    legacy::Field<uint32_t> total_created;  // Player.units_created
    uint8_t& setup_side;                    // PlayerSetupState.side
    uint8_t& setup_color;                   // PlayerSetupState.color
    uint8_t& resource_flags;                // Player.resource_flags
    legacy::Field<float> energy;            // Player.energy
    legacy::Field<float> metal;             // Player.metal
    legacy::Field<float> energy_produced;   // Player.energy_produced
    legacy::Field<float> energy_consumed;   // Player.energy_requested
    legacy::Field<float> metal_produced;    // Player.metal_produced
    legacy::Field<float> metal_consumed;    // Player.metal_requested
    legacy::Field<float> energy_cap;        // Player.energy_storage
    legacy::Field<float> metal_cap;         // Player.metal_storage
    legacy::Field<double> energy_harvested; // Player.energy_produced_total
    legacy::Field<double> metal_harvested;  // Player.metal_produced_total
    legacy::Field<double> cumulative_energy_consumed; // Player.energy_requested_total
    legacy::Field<double> cumulative_metal_consumed;  // Player.metal_requested_total
    legacy::Field<double> cumulative_energy_wasted;   // Player.energy_wasted_total
    legacy::Field<double> cumulative_metal_wasted;    // Player.metal_wasted_total
    legacy::Field<float> shared_energy;               // Player.shared_energy_storage
    legacy::Field<float> shared_metal;                // Player.shared_metal_storage
};

// View of oa::World (Game fields) under the old spawn-world member names.
struct World {
    /// Binds the view to a world, its simulation view and its native tables.
    ///
    /// @param record canonical world
    /// @param simulation simulation-state view of the same world
    /// @param types runtime types
    /// @param setups setup records of the ten players
    World(
        oa::World& record,
        sim::simulation_state::World& simulation,
        std::span<Type> types,
        std::span<PlayerSetupState> setups
    );
    World(const World&) = delete;

    oa::World& record;
    sim::simulation_state::World* simulation;
    sim::unit_spawn::legacy::Span<Slot> slots; // bound by LegacyViews
    std::span<Type> types;
    std::array<PlayerRange, 10> players;
    legacy::Field<uint16_t> cycle_unit_id; // Game.cycle_unit_id
    uint16_t& per_player_limit;            // Game.units_per_player
    legacy::Field<uint16_t> total_slots;   // Game.unit_slot_count
    uint8_t& viewpoint_player;             // Game.viewpoint_player
};

// Owns the views of one World. The World, its unit table and every side table
// must stay at stable addresses for the lifetime of the views; rebind after the
// unit pool is initialised so player ranges reflect it.
class LegacyViews {
  public:

    /// Builds the views of every unit slot and player of a world.
    ///
    /// Only the slots every side table covers are viewed; with fewer than ten
    /// setups, the player views read spare setup records the views own.
    ///
    /// @param world canonical world; must stay at a stable address
    /// @param types runtime types
    /// @param assets asset handles, one per unit slot
    /// @param setups setup records of the ten players
    /// @param orders order lists, one per unit slot
    LegacyViews(
        oa::World& world,
        std::span<Type> types,
        std::span<SlotAssets> assets,
        std::span<PlayerSetupState> setups,
        std::span<sim::simulation_state::OrderQueue> orders
    );
    LegacyViews(const LegacyViews&) = delete;
    LegacyViews& operator=(const LegacyViews&) = delete;

    /// Rebinds every player's unit range from its first_unit/last_unit references.
    void bind_player_ranges();

    /// Returns the simulation-state world view.
    sim::simulation_state::World& simulation() noexcept { return simulation_; }

    /// Returns the spawn world view.
    World& world() noexcept { return world_; }

    /// Returns the spawn world view.
    const World& world() const noexcept { return world_; }

    /// Returns the unit views, one per slot.
    std::span<sim::simulation_state::Unit> units() noexcept { return units_; }

    /// Returns the slot views, one per slot.
    std::span<Slot> slots() noexcept { return slots_; }

    /// Returns the slot view of a unit record.
    ///
    /// @param record unit in the viewed world
    /// @return its slot view; for a unit outside the viewed pool, the view of
    ///         reserved slot 0, which the pool always holds
    Slot& slot(const oa::Unit& record) noexcept;

    /// Returns the unit view of a unit record.
    sim::simulation_state::Unit& unit(const oa::Unit& record) { return *slot(record).unit; }

  private:

    oa::World& record_;
    sim::simulation_state::World simulation_;
    std::vector<sim::simulation_state::Unit> units_;
    std::vector<Slot> slots_;
    // What the player views read when the views are given fewer than ten setups.
    std::array<PlayerSetupState, OA_PLAYER_COUNT> spare_setups_{};
    World world_;
};

// A self-contained World with views over it, for tests and tools that built the
// removed per-module records directly. With no types a reserved type 0 and one
// further type are supplied. Call load_types() after editing types().
class LegacyWorld {
  public:

    /// Creates a world with its tables and views.
    ///
    /// @param unit_slots unit pool size
    /// @param type_count runtime types, including reserved type 0
    explicit LegacyWorld(std::size_t unit_slots, std::size_t type_count = 2);
    LegacyWorld(const LegacyWorld&) = delete;
    LegacyWorld& operator=(const LegacyWorld&) = delete;

    /// Returns the canonical world.
    oa::World& state() noexcept { return *world_; }

    /// Returns the views over the world.
    LegacyViews& views() noexcept { return *views_; }

    /// Returns the simulation-state world view.
    sim::simulation_state::World& simulation() noexcept { return views_->simulation(); }

    /// Returns the spawn world view.
    World& world() noexcept { return views_->world(); }

    /// Returns the runtime types; call load_types after editing them.
    std::span<Type> types() noexcept { return types_; }

    /// Returns the unit view of a slot.
    sim::simulation_state::Unit& unit(std::size_t slot) { return views_->units()[slot]; }

    /// Returns the slot view of a slot.
    Slot& slot(std::size_t slot) { return views_->slots()[slot]; }

    /// Returns a player view.
    ///
    /// Throws std::out_of_range for an index past the ten players.
    ///
    /// @param index player index
    /// @return the view
    sim::simulation_state::Player& player(std::size_t index) {
        return views_->simulation().players.at(index);
    }

    /// Returns the native tables for unit creation.
    Tables& tables() noexcept { return tables_; }

    /// Copies every runtime type into its canonical UnitDef.
    void load_types();
    /// Gives a player an inclusive slot range and rebinds the views.
    ///
    /// An index past the ten players changes nothing.
    ///
    /// @param index player index
    /// @param first first slot
    /// @param count number of slots; 0 clears the range
    void range(std::size_t index, std::size_t first, std::size_t count);

  private:

    std::unique_ptr<oa::World> world_;
    std::vector<oa::Unit> units_;
    std::vector<oa::UnitDef> defs_;
    std::vector<Type> types_;
    std::vector<SlotAssets> assets_;
    std::array<PlayerSetupState, 10> setups_{};
    std::vector<sim::simulation_state::OrderQueue> orders_;
    Tables tables_;
    std::unique_ptr<LegacyViews> views_;
};
} // namespace oa::sim::unit_spawn
