// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
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

/// What is wrong with a GlobalHeader condition.
enum class DefinitionError : uint8_t {
    none,                 ///< the conditions are registered
    type_name_too_long,   ///< a unit type name does not fit the 32-byte field with its NUL
    group_full,           ///< the condition's group already holds handler_capacity conditions
    missing_unit_type,    ///< the condition text does not start with a unit type of letters
    missing_comma,        ///< a comma before an integer is missing
    missing_integer,      ///< an integer after a comma is missing
    integer_out_of_range, ///< an integer lies outside the signed 32-bit range
    embedded_nul,         ///< the condition text holds a NUL
};

/// Says what a definition error means.
///
/// @param error the error
/// @return static text for messages
[[nodiscard]] const char* definition_error_text(DefinitionError error) noexcept;

/// Returns the descriptor of a condition kind.
///
/// @param kind condition kind
/// @return its group and GlobalHeader key, or null for a value outside Kind
[[nodiscard]] const Descriptor* descriptor(Kind kind) noexcept;

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
/// A condition whose text is malformed ends the registration there: the
/// conditions before it stay in the controller, which is not marked registered.
///
/// @param[in,out] controller controller to fill
/// @param host reads the selected OTA GlobalHeader
/// @return none, or what is wrong with the first malformed condition
DefinitionError register_conditions(Controller& controller, DefinitionHost& host);
/// Builds a BuildUnitType condition: the type name, unresolved.
///
/// @param type unit type name
/// @return the condition, or nullopt for a name that does not fit the 32-byte field
[[nodiscard]] std::optional<Condition> build_unit_type_condition(std::string_view type);
/// Builds a MoveUnitToRadius condition.
///
/// Stores the type name (none for ANYTYPE), the point with the unplaced height, and
/// the radius as 16.16.
///
/// @param type unit type name, or ANYTYPE
/// @param x point x, world units
/// @param z point z, world units
/// @param radius radius, world units
/// @return the condition, or nullopt for a name that does not fit the 32-byte field
[[nodiscard]] std::optional<Condition>
move_unit_to_radius_condition(std::string_view type, int32_t x, int32_t z, int32_t radius);
/// Builds a UnitTypePassesX or UnitTypePassesZ condition.
///
/// Stores the type name (none for ANYTYPE) and the line in cells (world units >> 4,
/// arithmetic).
///
/// @param kind unit_type_passes_x or unit_type_passes_z
/// @param type unit type name, or ANYTYPE
/// @param line line position, world units
/// @return the condition, or nullopt for another kind or a name that does not
///         fit the 32-byte field
[[nodiscard]] std::optional<Condition>
unit_type_passes_condition(Kind kind, std::string_view type, int32_t line);
/// Builds a KillAllOfType condition: the type name, unresolved.
///
/// @param type unit type name
/// @return the condition, or nullopt for a name that does not fit the 32-byte field
[[nodiscard]] std::optional<Condition> kill_all_of_type_condition(std::string_view type);
/// Builds an AllUnitsKilledOfType condition: the type name, unresolved.
///
/// @param type unit type name
/// @return the condition, or nullopt for a name that does not fit the 32-byte field
[[nodiscard]] std::optional<Condition> all_units_killed_of_type_condition(std::string_view type);
/// Clears Controller.enabled, turning the victory and defeat tests off.
///
/// An empty mission schema and the console Kill command leave the game without an
/// outcome this way.
///
/// @param[in,out] controller controller to disable
void disable(Controller& controller);
/// Tells whether a group's condition count fits its array.
///
/// @param count the count
/// @return true for a count in 0..handler_capacity
[[nodiscard]] bool condition_count_valid(int32_t count) noexcept;

/// Calls a function on every victory condition, then on every defeat condition.
///
/// Each group's count is re-read and checked after every call. A controller
/// whose conditions were never registered visits nothing; a count outside the
/// array or an empty slot within the count ends the visit there.
///
/// @param[in,out] controller registered conditions
/// @param visit called with each condition
/// @return false when the visit ended early or never began
template <typename Visit>
[[nodiscard]] bool visit_conditions(Controller& controller, Visit&& visit) {
    if (!controller.registration_complete)
        return false;
    for (const Group group : {Group::victory, Group::defeat}) {
        const int32_t& count =
            group == Group::victory ? controller.victory_count : controller.defeat_count;
        auto& conditions = group == Group::victory ? controller.victory : controller.defeat;
        if (!condition_count_valid(count))
            return false;
        for (int32_t i = 0; i < count; ++i) {
            std::optional<Condition>& condition = conditions[static_cast<std::size_t>(i)];
            if (!condition)
                return false;
            visit(*condition);
            if (!condition_count_valid(count))
                return false;
        }
    }
    return true;
}

/// Tells every victory then defeat condition that a unit was created.
///
/// @param[in,out] controller registered conditions
/// @param unit unit created
/// @quirk No condition kind reacts to a unit's creation, so nothing changes.
void notify_unit_created(Controller& controller, UnitHandle unit);
} // namespace oa::sim::scenario
