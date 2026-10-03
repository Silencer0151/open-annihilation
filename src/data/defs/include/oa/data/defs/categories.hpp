// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Unit category registry: named masks of unit type ids, 512 type ids wide
// as in 3.1c unless the registry is made wider before its first category.
#pragma once

#include "oa/core/unit_def.h"
#include "oa/data/limits.hpp"

#include <cstdint>

namespace oa::data::defs {

/// Words of a 3.1c category mask: 512 type ids, 64 bytes.
inline constexpr uint32_t category_mask_words = 16;
/// Words of the widest category mask a registry may keep.
inline constexpr uint32_t max_category_mask_words =
    data::limits::type_words(data::limits::highest_type_bits);
inline constexpr uint32_t category_token_capacity = 256;

/// A category mask: bit n of its words is unit type id n. The words belong to
/// the registry that issued the mask, or to a CategoryMaskStorage; a mask
/// with no words holds no type.
struct CategoryMask {
    uint32_t* words{};     ///< word_count words
    uint32_t word_count{}; ///< words held; type ids from word_count * 32 are left out
};

/// Words for a category mask made outside a registry, as wide as the widest
/// a registry may keep.
struct CategoryMaskStorage {
    uint32_t words[max_category_mask_words]{};
};

/// Returns an empty mask over a storage block, holding a count of type ids.
///
/// @param[in,out] storage the words the mask uses; cleared
/// @param type_bits type ids the mask holds, rounded up to whole words and
///     capped at the storage
/// @return the mask
[[nodiscard]] CategoryMask
category_mask_over(CategoryMaskStorage& storage, uint32_t type_bits) noexcept;

struct Category {
    char* name{};
    uint32_t mask{}; // index into CategoryRegistry.masks
};

// Entries sorted by case-insensitive name. Masks stay in creation order so a
// mask reference (index + 1, as UnitDef stores it) survives later inserts.
struct CategoryRegistry {
    Category* entries{};
    CategoryMask* masks{};  // count masks, over mask_words
    uint32_t* mask_words{}; // words_per_mask words for each mask, in mask order
    uint32_t count{};       // entries and masks
    uint32_t capacity{};
    uint32_t words_per_mask{category_mask_words};
};

/// Sets a unit type's bit in a category mask.
///
/// A type id past the mask's words is ignored, as 3.1c ignores type ids of
/// 512 and above in its 64-byte masks.
///
/// @param[in,out] mask mask to add the type to
/// @param type_id sorted catalog index of the unit type
void category_mask_set(CategoryMask* mask, uint16_t type_id) noexcept;

/// ORs every bit of one category mask into another.
///
/// Bits past the receiving mask's words are dropped.
///
/// @param[in,out] mask mask that receives the bits
/// @param other mask whose bits are added
void category_mask_or(CategoryMask* mask, const CategoryMask* other) noexcept;

/// Tests whether a category mask holds a unit type.
///
/// @param mask mask to test
/// @param type_id sorted catalog index of the unit type
/// @return true when the type's bit is set; false for type ids past the mask's words
[[nodiscard]] bool category_mask_contains(const CategoryMask* mask, uint16_t type_id) noexcept;

/// Empties a registry without freeing anything, keeping its mask width.
///
/// @param[out] registry registry to reset; its previous storage is not released
void category_registry_init(CategoryRegistry* registry) noexcept;

/// Sets how many unit type ids each mask of an empty registry holds.
///
/// @param[in,out] registry registry to size; it must hold no category yet
/// @param type_bits type ids each mask holds (CategoryMasks::types), rounded
///     up to whole words
/// @return false, changing nothing, when the registry already holds a
///     category or type_bits is 0 or past data::limits::highest_type_bits
bool category_registry_set_mask_types(CategoryRegistry* registry, uint32_t type_bits) noexcept;

/// Frees every category name and the entry and mask arrays, then empties the registry.
///
/// @param[in,out] registry registry to clear; left as category_registry_init
///     leaves it, with its mask width
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
