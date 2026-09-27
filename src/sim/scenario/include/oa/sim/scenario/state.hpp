// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace oa::sim::scenario {
inline constexpr std::size_t handler_capacity = 16, type_name_capacity = 32;
enum class Kind {
    kill_enemy_commander,
    destroy_all_units,
    kill_all_mobile_units,
    build_unit_type,
    capture_unit_type,
    kill_all_of_type,
    kill_unit_type,
    move_unit_to_radius,
    unit_type_passes_x,
    unit_type_passes_z,
    victory_timer,
    commander_killed,
    all_units_killed,
    all_units_killed_of_type,
    unit_type_killed,
    death_timer,
    any_unit_passes_x,
    any_unit_passes_z
};
// One per Kind; the kind tables are indexed by Kind.
inline constexpr std::size_t kind_count = 18;
static_assert(kind_count == static_cast<std::size_t>(Kind::any_unit_passes_z) + 1);
enum class Group { victory, defeat };

struct UnitHandle {
    uintptr_t value{};
};

// MoveUnitToRadius keeps this as its point's height until the query first
// places the point on the terrain.
inline constexpr int32_t unplaced_point_height = 0x12345678;

// A map position in world units.
struct ConditionPoint {
    int32_t x{};
    int32_t y{};
    int32_t z{};

    /// Compares two points coordinate by coordinate.
    ///
    /// @param left one point
    /// @param right the other
    /// @return true when every coordinate matches
    friend bool operator==(const ConditionPoint& left, const ConditionPoint& right) = default;
};

// A registered victory or defeat condition. Every kind keeps the two flags;
// each other field belongs to the kinds its comment names and stays at its
// initial value in the rest. A field that a kind's constructor leaves unset
// keeps its initial value until the kind first writes it; the game never
// reads such a field before writing it.
struct Condition {
    Kind kind{};
    int32_t satisfied{};  // nonzero once met; saved as "Satisfied"
    int32_t celebrated{}; // nonzero once the victory cue has played; saved as "Celebrated"
    // NUL-terminated, empty for any type: BuildUnitType, CaptureUnitType,
    // KillAllOfType, KillUnitType, MoveUnitToRadius, UnitTypePassesX/Z,
    // AllUnitsKilledOfType, UnitTypeKilled.
    char type_name[type_name_capacity]{};
    // Type index of type_name, 0 until resolved: BuildUnitType, KillAllOfType,
    // AllUnitsKilledOfType.
    uint16_t type_index{};
    // Units the last walk counted: KillAllMobileUnits' mobile units (saved as
    // "NumUnits"), KillAllOfType's and AllUnitsKilledOfType's units of the type.
    int32_t units_counted{};
    // Deaths of the type still wanted (KillUnitType, UnitTypeKilled); saved as
    // "NumLeftToKill".
    int32_t kills_left{};
    // Cells: the column (UnitTypePassesX, AnyUnitPassesX) or row (UnitTypePassesZ,
    // AnyUnitPassesZ) a unit must come within two cells of.
    int32_t line{};
    // Match tick the timer runs out at: VictoryTimerRunsOut, DeathTimerRunsOut.
    uint32_t deadline{};
    ConditionPoint point{}; // MoveUnitToRadius; y is unplaced_point_height until placed
    int32_t radius{};       // MoveUnitToRadius, world units in 16.16 fixed point

    /// Compares two conditions field by field.
    ///
    /// @param left one condition
    /// @param right the other
    /// @return true when every field matches
    friend bool operator==(const Condition& left, const Condition& right) = default;
};

struct Descriptor {
    Kind kind{};
    Group group{};
    std::string_view key{}; // the GlobalHeader key that registers the kind
};

/// Returns the descriptor of a condition kind.
///
/// Throws std::invalid_argument for a value outside Kind.
///
/// @param kind condition kind
/// @return its group and GlobalHeader key
const Descriptor& descriptor(Kind kind);

struct Controller {
    std::array<std::optional<Condition>, handler_capacity> victory, defeat;
    int32_t victory_count{}, defeat_count{}; // conditions registered in each array
    uint32_t enabled = 1; // the victory and defeat tests run while set; construction sets it
    bool registration_complete{}; // the engine's guard against use before registration
};

class DefinitionHost {
  public:

    virtual ~DefinitionHost() = default;
    // Reads the selected OTA GlobalHeader; exact numeric parsing lives in the TDF
    // reader. Missing strings differ from present empty strings.

    /// Reads an integer key of the GlobalHeader.
    ///
    /// @param key key name
    /// @param fallback value for a missing key
    /// @return the value
    virtual int32_t integer(std::string_view key, int32_t fallback) = 0;
    /// Reads a text key of the GlobalHeader.
    ///
    /// @param key key name
    /// @return the text, at most 255 characters, or nullopt when the key is missing
    virtual std::optional<std::string> text(std::string_view key) = 0;
};

/// Constructs an empty controller: no victory or defeat conditions, enabled.
///
/// @param[out] controller controller to construct
void construct(Controller& controller);
/// Releases each condition of a controller; a condition runs no cleanup of its own.
///
/// @param[in,out] controller controller to empty
void destroy(Controller& controller);
/// Registers every victory and defeat condition the GlobalHeader names.
///
/// Adds the default pair (DestroyAllUnits victory, AllUnitsKilled defeat) when a group
/// stays empty, and marks the controller registered.
///
/// Throws std::invalid_argument for malformed condition text.
///
/// @param[in,out] controller controller to fill
/// @param host reads the selected OTA GlobalHeader
void register_conditions(Controller& controller, DefinitionHost& host);
/// Builds a BuildUnitType condition: the type name, unresolved.
///
/// Throws std::invalid_argument for a name that does not fit the 32-byte field.
///
/// @param type unit type name
/// @return the condition
Condition build_unit_type_condition(std::string_view type);
/// Builds a MoveUnitToRadius condition.
///
/// Stores the type name (none for ANYTYPE), the point with the unplaced height, and
/// the radius as 16.16.
///
/// Throws std::invalid_argument for a name that does not fit the 32-byte field.
///
/// @param type unit type name, or ANYTYPE
/// @param x point x, world units
/// @param z point z, world units
/// @param radius radius, world units
/// @return the condition
Condition
move_unit_to_radius_condition(std::string_view type, int32_t x, int32_t z, int32_t radius);
/// Builds a UnitTypePassesX or UnitTypePassesZ condition.
///
/// Stores the type name (none for ANYTYPE) and the line in cells (world units >> 4,
/// arithmetic).
///
/// Throws std::invalid_argument for another kind or a name that does not fit the
/// 32-byte field.
///
/// @param kind unit_type_passes_x or unit_type_passes_z
/// @param type unit type name, or ANYTYPE
/// @param line line position, world units
/// @return the condition
Condition unit_type_passes_condition(Kind kind, std::string_view type, int32_t line);
/// Builds a KillAllOfType condition: the type name, unresolved.
///
/// Throws std::invalid_argument for a name that does not fit the 32-byte field.
///
/// @param type unit type name
/// @return the condition
Condition kill_all_of_type_condition(std::string_view type);
/// Builds an AllUnitsKilledOfType condition: the type name, unresolved.
///
/// Throws std::invalid_argument for a name that does not fit the 32-byte field.
///
/// @param type unit type name
/// @return the condition
Condition all_units_killed_of_type_condition(std::string_view type);
/// Clears Controller.enabled, turning the victory and defeat tests off.
///
/// An empty mission schema and the console Kill command leave the game without an
/// outcome this way.
///
/// @param[in,out] controller controller to disable
void disable(Controller& controller);
/// Checks a group's condition count.
///
/// Throws std::invalid_argument for a count outside 0..handler_capacity.
///
/// @param count the count
void check_condition_count(int32_t count);

/// Calls a function on every victory condition, then on every defeat condition.
///
/// Each group's count is re-read and checked after every call.
///
/// Throws std::logic_error for a controller whose conditions were never registered or
/// for a missing condition, and std::invalid_argument for a count outside the arrays.
///
/// @param[in,out] controller registered conditions
/// @param visit called with each condition
template <typename Visit>
void visit_conditions(Controller& controller, Visit&& visit) {
    if (!controller.registration_complete)
        throw std::logic_error("scenario GlobalHeader registration was not completed");
    for (const Group group : {Group::victory, Group::defeat}) {
        const int32_t& count =
            group == Group::victory ? controller.victory_count : controller.defeat_count;
        auto& conditions = group == Group::victory ? controller.victory : controller.defeat;
        check_condition_count(count);
        for (int32_t i = 0; i < count; ++i) {
            std::optional<Condition>& condition = conditions[static_cast<std::size_t>(i)];
            if (!condition)
                throw std::logic_error("missing scenario handler");
            visit(*condition);
            check_condition_count(count);
        }
    }
}

/// Tells every victory then defeat condition that a unit was created.
///
/// Throws std::logic_error for a controller whose conditions were never registered.
///
/// @param[in,out] controller registered conditions
/// @param unit unit created
/// @quirk No condition kind reacts to a unit's creation, so nothing changes.
void notify_unit_created(Controller& controller, UnitHandle unit);
} // namespace oa::sim::scenario
