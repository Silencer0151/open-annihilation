// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/scenario/state.hpp"
#include <algorithm>
#include <bit>
#include <cstdint>
#include <stdexcept>

namespace oa::sim::scenario {
namespace {
constexpr uint32_t ticks_per_second = 30;
constexpr unsigned world_to_tile_shift = 4;
constexpr unsigned fixed_point_fraction_bits = 16;
constexpr std::array<Descriptor, kind_count> descriptions{{
    {Kind::kill_enemy_commander, Group::victory, "KillEnemyCommander"},
    {Kind::destroy_all_units, Group::victory, "DestroyAllUnits"},
    {Kind::kill_all_mobile_units, Group::victory, "KillAllMobileUnits"},
    {Kind::build_unit_type, Group::victory, "BuildUnitType"},
    {Kind::capture_unit_type, Group::victory, "CaptureUnitType"},
    {Kind::kill_all_of_type, Group::victory, "KillAllOfType"},
    {Kind::kill_unit_type, Group::victory, "KillUnitType"},
    {Kind::move_unit_to_radius, Group::victory, "MoveUnitToRadius"},
    {Kind::unit_type_passes_x, Group::victory, "UnitTypePassesX"},
    {Kind::unit_type_passes_z, Group::victory, "UnitTypePassesZ"},
    {Kind::victory_timer, Group::victory, "VictoryTimerRunsOut"},
    {Kind::commander_killed, Group::defeat, "CommanderKilled"},
    {Kind::all_units_killed, Group::defeat, "AllUnitsKilled"},
    {Kind::all_units_killed_of_type, Group::defeat, "AllUnitsKilledOfType"},
    {Kind::unit_type_killed, Group::defeat, "UnitTypeKilled"},
    {Kind::death_timer, Group::defeat, "DeathTimerRunsOut"},
    {Kind::any_unit_passes_x, Group::defeat, "AnyUnitPassesX"},
    {Kind::any_unit_passes_z, Group::defeat, "AnyUnitPassesZ"},
}};

/// Tells whether every descriptor sits at its kind's index, which descriptor() relies on.
///
/// @return true when the table is in Kind order
constexpr bool descriptions_in_kind_order() {
    for (std::size_t i = 0; i < descriptions.size(); ++i)
        if (static_cast<std::size_t>(descriptions[i].kind) != i)
            return false;
    return true;
}

static_assert(descriptions_in_kind_order());

/// Stores a condition's unit type name.
///
/// Throws std::invalid_argument for a name that does not fit the 32-byte field with its NUL.
///
/// @param[in,out] c condition whose name is set
/// @param text the name
void set_type_name(Condition& c, std::string_view text) {
    if (text.size() >= type_name_capacity)
        throw std::invalid_argument("scenario unit type exceeds the 32-byte name field");
    std::copy(text.begin(), text.end(), c.type_name);
    c.type_name[text.size()] = '\0';
}

/// Builds a condition of a kind with every field at its initial value.
///
/// @param kind condition kind
/// @return the condition
Condition base(Kind kind) {
    Condition c;
    c.kind = kind;
    return c;
}

/// Builds a condition that names a unit type and holds nothing else yet.
///
/// Throws std::invalid_argument for a name that does not fit the 32-byte field.
///
/// @param kind condition kind
/// @param type unit type name
/// @return the condition
Condition named_condition(Kind kind, std::string_view type) {
    auto c = base(kind);
    set_type_name(c, type);
    return c;
}

/// Adds a condition to the end of its kind's group.
///
/// Throws std::invalid_argument when the group is full.
///
/// @param[in,out] controller controller being filled
/// @param condition condition to add
void append(Controller& controller, Condition condition) {
    const bool victory = descriptor(condition.kind).group == Group::victory;
    auto& count = victory ? controller.victory_count : controller.defeat_count;
    auto& array = victory ? controller.victory : controller.defeat;
    if (count < 0 || count >= static_cast<int32_t>(array.size()))
        throw std::invalid_argument("scenario handler array capacity exceeded");
    array[static_cast<std::size_t>(count)] = std::move(condition);
    ++count;
}

/// Tells whether a character is an ASCII letter.
///
/// @param c character
/// @return true for a to z and A to Z
bool letters(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

/// Tells whether a unit type name is ANYTYPE, in any case.
///
/// @param s unit type name
/// @return true for ANYTYPE
bool any_type(std::string_view s) {
    constexpr std::string_view any = "anytype";
    if (s.size() != any.size())
        return false;
    for (std::size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c + 32);
        if (c != any[i])
            return false;
    }
    return true;
}

struct Parsed {
    std::string type;
    std::array<int32_t, 3> values{};
};

/// Reads condition text: a unit type of letters, then comma-separated integers.
///
/// Each integer reads as scanf's %i does: optional leading space and sign, then octal
/// after a 0, hexadecimal after 0x, else decimal, up to the first other character.
///
/// Throws std::invalid_argument for text without a type, a comma or an integer, or for
/// an integer outside the signed 32-bit range.
///
/// @param s condition text
/// @param count integers to read, at most 3
/// @return the type and the integers
Parsed parse(std::string_view s, std::size_t count) {
    Parsed result;
    std::size_t at = 0;
    while (at < s.size() && letters(s[at]))
        ++at;
    if (at == 0)
        throw std::invalid_argument("scenario condition requires alphabetic unit type");
    result.type = s.substr(0, at);
    for (std::size_t i = 0; i < count; ++i) {
        if (at >= s.size() || s[at++] != ',')
            throw std::invalid_argument("scenario condition missing comma");
        while (at < s.size() && (s[at] == ' ' || s[at] == '\t' || s[at] == '\n' || s[at] == '\r' ||
                                 s[at] == '\f' || s[at] == '\v'))
            ++at;
        bool negative = false;
        if (at < s.size() && (s[at] == '-' || s[at] == '+'))
            negative = s[at++] == '-';
        unsigned radix = 10;
        if (at < s.size() && s[at] == '0') {
            radix = 8;
            if (at + 1 < s.size() && (s[at + 1] == 'x' || s[at + 1] == 'X')) {
                radix = 16;
                at += 2;
            }
        }
        const auto begin = at;
        uint64_t value = 0;
        while (at < s.size()) {
            const unsigned char ch = static_cast<unsigned char>(s[at]);
            unsigned digit = ch >= '0' && ch <= '9'   ? ch - '0'
                             : ch >= 'a' && ch <= 'f' ? ch - 'a' + 10
                             : ch >= 'A' && ch <= 'F' ? ch - 'A' + 10
                                                      : 99;
            if (digit >= radix)
                break;
            value = value * radix + digit;
            if (value > (negative ? 0x80000000ULL : 0x7fffffffULL))
                throw std::invalid_argument("scenario integer outside signed 32-bit range");
            ++at;
        }
        if (at == begin)
            throw std::invalid_argument("scenario condition missing integer");
        result.values[i] = std::bit_cast<int32_t>(
            negative ? 0U - static_cast<uint32_t>(value) : static_cast<uint32_t>(value)
        );
    }
    return result;
}

/// Builds the condition a GlobalHeader key registers.
///
/// Throws std::invalid_argument for malformed condition text.
///
/// @param kind condition kind
/// @param text the key's text, for the kinds that read one
/// @param number the key's integer, for the timer and AnyUnitPasses kinds
/// @return the condition
Condition make(Kind kind, std::optional<std::string> text, int32_t number = 0) {
    switch (kind) {
    case Kind::build_unit_type:
        return build_unit_type_condition(*text);
    case Kind::kill_all_of_type:
        return kill_all_of_type_condition(*text);
    case Kind::all_units_killed_of_type:
        return all_units_killed_of_type_condition(*text);
    case Kind::capture_unit_type:
        return named_condition(kind, *text);
    case Kind::kill_unit_type:
    case Kind::unit_type_killed: {
        const auto p = parse(*text, 1);
        auto c = named_condition(kind, p.type);
        c.kills_left = p.values[0];
        return c;
    }
    case Kind::move_unit_to_radius: {
        const auto p = parse(*text, 3);
        return move_unit_to_radius_condition(p.type, p.values[0], p.values[1], p.values[2]);
    }
    case Kind::unit_type_passes_x:
    case Kind::unit_type_passes_z: {
        const auto p = parse(*text, 1);
        return unit_type_passes_condition(kind, p.type, p.values[0]);
    }
    case Kind::victory_timer:
    case Kind::death_timer: {
        auto c = base(kind);
        c.deadline = static_cast<uint32_t>(number) * ticks_per_second;
        return c;
    }
    case Kind::any_unit_passes_x:
    case Kind::any_unit_passes_z: {
        auto c = base(kind);
        c.line = number >> world_to_tile_shift;
        return c;
    }
    default:
        return base(kind);
    }
}
} // namespace

const Descriptor& descriptor(Kind kind) {
    const auto i = static_cast<std::size_t>(kind);
    if (i >= descriptions.size())
        throw std::invalid_argument("unknown scenario condition kind");
    return descriptions[i];
}

Condition build_unit_type_condition(std::string_view type) {
    return named_condition(Kind::build_unit_type, type);
}

Condition
move_unit_to_radius_condition(std::string_view type, int32_t x, int32_t z, int32_t radius) {
    auto c = named_condition(Kind::move_unit_to_radius, any_type(type) ? std::string_view{} : type);
    c.point = {x, unplaced_point_height, z};
    c.radius = static_cast<int32_t>(static_cast<uint32_t>(radius) << fixed_point_fraction_bits);
    return c;
}

Condition unit_type_passes_condition(Kind kind, std::string_view type, int32_t line) {
    if (kind != Kind::unit_type_passes_x && kind != Kind::unit_type_passes_z)
        throw std::invalid_argument("not a UnitTypePasses condition");
    auto c = named_condition(kind, any_type(type) ? std::string_view{} : type);
    c.line = line >> world_to_tile_shift;
    return c;
}

Condition kill_all_of_type_condition(std::string_view type) {
    return named_condition(Kind::kill_all_of_type, type);
}

Condition all_units_killed_of_type_condition(std::string_view type) {
    return named_condition(Kind::all_units_killed_of_type, type);
}

void construct(Controller& c) {
    c = Controller{};
}

void destroy(Controller& c) {
    for (auto& condition : c.victory)
        condition.reset();
    for (auto& condition : c.defeat)
        condition.reset();
    c.victory_count = 0;
    c.defeat_count = 0;
    c.registration_complete = false;
}

void disable(Controller& c) {
    c.enabled = 0;
}

void register_conditions(Controller& c, DefinitionHost& h) {
    c.registration_complete = false;
    for (const auto& d : descriptions) {
        const auto kind = d.kind;
        switch (kind) {
        case Kind::build_unit_type:
        case Kind::capture_unit_type:
        case Kind::kill_all_of_type:
        case Kind::kill_unit_type:
        case Kind::move_unit_to_radius:
        case Kind::unit_type_passes_x:
        case Kind::unit_type_passes_z:
        case Kind::all_units_killed_of_type:
        case Kind::unit_type_killed: {
            auto text = h.text(d.key);
            if (text) {
                *text = text->substr(0, 255);
                if (text->find('\0') != std::string::npos)
                    throw std::invalid_argument("embedded NUL in scenario definition");
                append(c, make(kind, text));
            }
            break;
        }
        case Kind::victory_timer:
        case Kind::death_timer: {
            const auto value = h.integer(d.key, 0);
            if (value > 0)
                append(c, make(kind, {}, value));
            break;
        }
        case Kind::any_unit_passes_x:
        case Kind::any_unit_passes_z: {
            const auto value = h.integer(d.key, -1);
            if (value >= 0)
                append(c, make(kind, {}, value));
            break;
        }
        default:
            if (h.integer(d.key, 0) != 0)
                append(c, make(kind, {}));
            break;
        }
    }
    if (c.victory_count == 0)
        append(c, make(Kind::destroy_all_units, {}));
    if (c.defeat_count == 0)
        append(c, make(Kind::all_units_killed, {}));
    c.registration_complete = true;
}

void check_condition_count(int32_t count) {
    if (count < 0 || count > static_cast<int32_t>(handler_capacity))
        throw std::invalid_argument("invalid scenario handler count");
}

void notify_unit_created(Controller& c, UnitHandle) {
    visit_conditions(c, [](Condition&) {});
}
} // namespace oa::sim::scenario
