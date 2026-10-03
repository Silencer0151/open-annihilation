// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The rules a mod profile sets for the simulation, as plain records: one
// record per hack, grouped by the area of the engine that owns them, and the
// unit-script extension tables. The engine's limits are a record of their
// own, oa::data::limits::Limits.
//
// Every field starts at its 3.1c baseline, so a default-constructed
// MatchRules is base 3.1c, and a match given one plays exactly as
// without a profile. Each hack's record says whether the profile turns it on
// (enabled) and holds its parameters, which equal the baselines while it is
// off. The records hold fixed-size data only: no heap, no std::string, so
// simulation code can keep and read them.
//
// The records are generated from the registry (records.inc,
// script_extensions.inc; tools/gen_mod_registry.py); src/data/mod-profile
// fills them from a resolved profile.
//
// Besides the match-wide rules, a profile's data keys give single unit types
// and weapons values of their own (UnitTypeRules, WeaponTypeRules), read from
// their files. MatchRulesView is how simulation code reads all three: one
// small value every module's hooks or host can carry, which answers with
// 3.1c's rules when nothing was given.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <span>
#include <string_view>

namespace oa::data::match_rules {

/// Text of at most Capacity bytes of UTF-8, held in place.
///
/// @tparam Capacity the most bytes it holds
template <size_t Capacity>
struct FixedText {
    std::array<char, Capacity> bytes{}; ///< the text, its unused bytes zero
    uint16_t size{};                    ///< bytes of `bytes` in use

    /// Makes empty text.
    constexpr FixedText() = default;

    /// Makes text from a string literal, cut at Capacity bytes.
    ///
    /// @param text the literal
    template <size_t Length>
    constexpr FixedText(const char (&text)[Length]) noexcept {
        assign(std::string_view{text, Length - 1});
    }

    /// Replaces the text, cut at Capacity bytes.
    ///
    /// @param text the new text
    /// @return false when it was cut
    constexpr bool assign(std::string_view text) noexcept {
        bytes = {};
        size = 0;
        for (const char c : text) {
            if (size == Capacity)
                return false;
            bytes[size++] = c;
        }
        return true;
    }

    /// Views the text.
    ///
    /// @return the bytes in use
    [[nodiscard]] constexpr std::string_view view() const noexcept {
        return std::string_view{bytes.data(), size};
    }

    /// Compares the texts.
    bool operator==(const FixedText&) const = default;
};

/// A list of at most Capacity items, held in place.
///
/// @tparam Item the item type
/// @tparam Capacity the most items it holds
template <class Item, size_t Capacity>
struct FixedList {
    std::array<Item, Capacity> items{}; ///< the items, unused ones value-initialised
    uint16_t count{};                   ///< items in use

    /// Makes an empty list.
    constexpr FixedList() = default;

    /// Makes a list of the items given, cut at Capacity.
    ///
    /// @param list the items
    constexpr FixedList(std::initializer_list<Item> list) noexcept {
        for (const Item& item : list) {
            if (count == Capacity)
                break;
            items[count++] = item;
        }
    }

    /// Appends an item.
    ///
    /// @param item the item
    /// @return false when the list is full
    constexpr bool push(const Item& item) noexcept {
        if (count == Capacity)
            return false;
        items[count++] = item;
        return true;
    }

    /// Compares the lists, unused items included, which stay value-initialised.
    bool operator==(const FixedList&) const = default;
};

/// A set of the values of an enumeration with Count values, 0 to Count - 1.
///
/// @tparam Enum the enumeration
/// @tparam Count its number of values
template <class Enum, size_t Count>
struct EnumSet {
    std::array<bool, Count> members{}; ///< whether each value, by its number, is in the set

    /// Makes an empty set.
    constexpr EnumSet() = default;

    /// Makes a set of the values given.
    ///
    /// @param list the values
    constexpr EnumSet(std::initializer_list<Enum> list) noexcept {
        for (const Enum value : list)
            insert(value);
    }

    /// Adds a value.
    ///
    /// @param value the value
    constexpr void insert(Enum value) noexcept {
        const auto index = static_cast<size_t>(value);
        if (index < Count)
            members[index] = true;
    }

    /// Tells whether a value is in the set.
    ///
    /// @param value the value
    /// @return true when it is
    [[nodiscard]] constexpr bool contains(Enum value) const noexcept {
        const auto index = static_cast<size_t>(value);
        return index < Count && members[index];
    }

    /// Compares the sets.
    bool operator==(const EnumSet&) const = default;
};

/// How unit-script extensions answer reads that 3.1c's add-on left undefined.
enum class ScriptFidelity : uint8_t {
    exact, ///< reproduce every defined quirk; an undefined read gives the empty slot's answer
    safe,  ///< empty or dead slots give 0, and no read goes past the unit table
};

#include "oa/data/match_rules/script_extensions.inc"

/// The most indices a profile mounts in one direction.
inline constexpr size_t max_script_mounts = 32;

/// One mounted extension: the get or set index unit scripts use, and what it reads or writes.
struct ScriptMount {
    uint16_t index{};                                 ///< the script's get or set index
    ScriptExtension extension{ScriptExtension::none}; ///< what is mounted there

    /// Compares the mounts.
    bool operator==(const ScriptMount&) const = default;
};

/// The extensions mounted in one direction, in increasing index order.
///
/// An index with nothing mounted gives 0 on get, as in 3.1c.
struct ScriptExtensionTable {
    std::array<ScriptMount, max_script_mounts> mounts{}; ///< the mounts, in increasing index order
    uint8_t count{};                                     ///< mounts in use

    /// Returns what is mounted at an index.
    ///
    /// @param index a get or set index
    /// @return the extension, or none
    [[nodiscard]] constexpr ScriptExtension find(uint16_t index) const noexcept {
        for (uint8_t at = 0; at < count; ++at) {
            if (mounts[at].index == index)
                return mounts[at].extension;
        }
        return ScriptExtension::none;
    }

    /// Compares the tables.
    bool operator==(const ScriptExtensionTable&) const = default;
};

#include "oa/data/match_rules/records.inc"

/// The rules of base 3.1c: what a match without a profile plays by.
inline constexpr MatchRules baseline_match_rules{};

/// The most kill counts one unit type's veterancy thresholds list.
inline constexpr size_t max_veterancy_thresholds = 32;

/// Facings a building may be placed in, as bits of UnitTypeRules::build_facings.
namespace build_facing {
inline constexpr uint8_t south = 1; ///< the only facing in 3.1c
inline constexpr uint8_t east = 2;
inline constexpr uint8_t north = 4;
inline constexpr uint8_t west = 8;
} // namespace build_facing

/// What one unit type's data keys set: values its FBI file gives under the
/// keys a profile binds (DataKeyBindings), beyond the fields 3.1c reads.
///
/// A default record is a type whose file has none of them, as every type
/// has in 3.1c; the match-wide rule then applies.
struct UnitTypeRules {
    /// Kill counts, ascending, at which the type gains each veterancy level
    /// (veterancy.thresholds); none without the key.
    std::optional<FixedList<uint16_t, max_veterancy_thresholds>> veterancy_thresholds{};
    /// Kills per step of ballistic spread taken off (veterancy.accuracy-rate),
    /// 0 for no accuracy gain; none without the key.
    std::optional<uint16_t> veterancy_accuracy_rate{};
    /// The facings the type may be built in, as build_facing bits
    /// (units.build-facings); south only without the key.
    uint8_t build_facings{build_facing::south};

    /// Compares every field.
    bool operator==(const UnitTypeRules&) const = default;
};

/// What one weapon's data keys set: the low bit of the integer its weapon
/// TDF entry gives under each key a profile binds; false without the key, as
/// every weapon has in 3.1c.
struct WeaponTypeRules {
    bool not_to_air{};        ///< weapons.not-to-air: may not target airborne units
    bool surface_fire{};      ///< weapons.surface-fire: a water weapon also fires above water
    bool not_to_underwater{}; ///< weapons.not-to-underwater: no target at or below sea level
    bool no_map_alert{};      ///< weapons.no-map-alert: no area blast or minimap alert

    /// Compares every field.
    bool operator==(const WeaponTypeRules&) const = default;
};

/// A unit type without data keys: what MatchRulesView answers for a type it has no record of.
inline constexpr UnitTypeRules baseline_unit_type_rules{};

/// A weapon without data keys: what MatchRulesView answers for a weapon it has no record of.
inline constexpr WeaponTypeRules baseline_weapon_rules{};

/// The rules a match plays by, as simulation code reads them: the match-wide
/// records and the unit types' and weapons' own.
///
/// It only points at records that its owner (the match) keeps for as long as
/// it plays; copying it copies the pointers. A default-constructed view, and
/// any record it was not given, answers with 3.1c's rules, so a module whose
/// host leaves it unset plays as 3.1c.
struct MatchRulesView {
    const MatchRules* match{}; ///< the match-wide rules; null for 3.1c's
    /// Each unit type's rules, indexed by unit type index like the match's
    /// types (index 0 reserved); empty when no type has any.
    std::span<const UnitTypeRules> unit_types{};
    /// Each weapon's rules, indexed by its weapon ID; empty when no weapon has any.
    std::span<const WeaponTypeRules> weapons{};

    /// Returns the match-wide rules.
    ///
    /// @return the rules given, or baseline_match_rules
    [[nodiscard]] constexpr const MatchRules& rules() const noexcept {
        return match != nullptr ? *match : baseline_match_rules;
    }

    /// Returns a unit type's own rules.
    ///
    /// @param type_index unit type index
    /// @return its record, or baseline_unit_type_rules past the records given
    [[nodiscard]] constexpr const UnitTypeRules& unit_type(size_t type_index) const noexcept {
        return type_index < unit_types.size() ? unit_types[type_index] : baseline_unit_type_rules;
    }

    /// Returns a weapon's own rules.
    ///
    /// @param weapon_id the weapon's ID
    /// @return its record, or baseline_weapon_rules past the records given
    [[nodiscard]] constexpr const WeaponTypeRules& weapon(size_t weapon_id) const noexcept {
        return weapon_id < weapons.size() ? weapons[weapon_id] : baseline_weapon_rules;
    }
};

} // namespace oa::data::match_rules
