// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Unit category registry: named 512-bit masks of unit type ids.
#pragma once

#include "oa/core/unit_def.h"

#include <cstdint>

namespace oa::data::defs {

inline constexpr uint32_t category_mask_words = 16; // 64-byte masks
inline constexpr uint32_t category_token_capacity = 256;

struct CategoryMask {
    uint32_t words[category_mask_words];
};

struct Category {
    char* name;
    uint32_t mask; // index into CategoryRegistry.masks
};

// Entries sorted by case-insensitive name. Masks stay in creation order so a
// mask reference (index + 1, as UnitDef stores it) survives later inserts.
struct CategoryRegistry {
    Category* entries;
    CategoryMask* masks;
    uint32_t count; // entries and masks
    uint32_t capacity;
};

/// Sets a unit type's bit in a category mask.
///
/// Type ids of 512 and above do not fit the 64-byte mask and are ignored.
///
/// @param[in,out] mask mask to add the type to
/// @param type_id sorted catalog index of the unit type
void category_mask_set(CategoryMask* mask, uint16_t type_id) noexcept;

/// ORs every bit of one category mask into another.
///
/// @param[in,out] mask mask that receives the bits
/// @param other mask whose bits are added
void category_mask_or(CategoryMask* mask, const CategoryMask* other) noexcept;

/// Tests whether a category mask holds a unit type.
///
/// @param mask mask to test
/// @param type_id sorted catalog index of the unit type
/// @return true when the type's bit is set; false for type ids of 512 and above
[[nodiscard]] bool category_mask_contains(const CategoryMask* mask, uint16_t type_id) noexcept;

/// Empties a registry without freeing anything.
///
/// @param[out] registry registry to reset; its previous storage is not released
void category_registry_init(CategoryRegistry* registry) noexcept;

/// Frees every category name and the entry and mask arrays, then empties the registry.
///
/// @param[in,out] registry registry to clear; left as category_registry_init leaves it
void category_registry_clear(CategoryRegistry* registry) noexcept;

/// Returns the reference of a named category mask, creating an empty one.
///
/// Names match case-insensitively. A new entry is inserted in name order and
/// its zeroed mask appended after the existing ones, so earlier references
/// stay valid; the registry grows from 64 entries by doubling, up to 65536.
///
/// @param[in,out] registry registry to search and extend
/// @param name category name; copied when a new entry is made
/// @return mask index + 1, as UnitDef stores category references; 0 only when
///     the registry is full or an allocation fails
oa_ref32 category_registry_ref(CategoryRegistry* registry, const char* name) noexcept;

/// Resolves a category reference to its mask.
///
/// @param registry registry that issued the reference
/// @param ref mask index + 1 from category_registry_ref
/// @return the mask, or null for 0 and out-of-range references
[[nodiscard]] CategoryMask*
category_registry_mask(CategoryRegistry* registry, oa_ref32 ref) noexcept;

/// Resolves a category reference to its mask (read-only).
///
/// @param registry registry that issued the reference
/// @param ref mask index + 1 from category_registry_ref
/// @return the mask, or null for 0 and out-of-range references
[[nodiscard]] const CategoryMask*
category_registry_mask(const CategoryRegistry* registry, oa_ref32 ref) noexcept;

/// Returns the named category mask, creating an empty one.
///
/// @param[in,out] registry registry to search and extend
/// @param name category name, matched case-insensitively
/// @return the mask; null only when the registry is full or an allocation fails
CategoryMask* category_registry_find_or_add(CategoryRegistry* registry, const char* name) noexcept;

/// Looks up a named category mask without creating one.
///
/// @param registry registry to search
/// @param name category name, matched case-insensitively
/// @return the mask, or null when no category has that name
[[nodiscard]] const CategoryMask*
category_registry_find(const CategoryRegistry* registry, const char* name) noexcept;

/// Adds a unit's type id to each whitespace-separated category it names and to "ALL".
///
/// Tokens are split as sscanf(" %s %n") splits them; categories that do not
/// exist yet are created. Tokens longer than 255 characters are truncated.
///
/// @param[in,out] registry registry that receives the unit
/// @param unit unit whose type_id is added
/// @param categories the unit's CATEGORY text
/// @return false when a category cannot be created
bool register_unit_categories(
    CategoryRegistry* registry, const UnitDef* unit, const char* categories
) noexcept;

/// ORs a unit type's own bit, or else the named category's mask, into a target mask.
///
/// @param[in,out] target mask that receives the bits
/// @param[in,out] registry category registry; a name that is neither a unit
///     nor a known category gains an empty category
/// @param table sorted unit table (slot 0 reserved)
/// @param count number of slots in `table`, including slot 0
/// @param name unit name or category name
/// @return true when the name was a unit
bool resolve_type_or_category(
    CategoryMask* target,
    CategoryRegistry* registry,
    const UnitDef* table,
    uint32_t count,
    const char* name
) noexcept;

} // namespace oa::data::defs
