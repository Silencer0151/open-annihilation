// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/data/defs/categories.hpp"

#include "oa/data/defs/unit_records.hpp"
#include "oa/formats/tdf.hpp"

#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace oa::data::defs {
namespace {

constexpr uint32_t max_categories = 65536;
constexpr const char* all_category = "ALL";

[[nodiscard]] bool is_c_space(unsigned char c) noexcept {
    return c == ' ' || (c >= '\t' && c <= '\r');
}

/// Finds the first entry whose name is not below a name, by halving the remaining span.
///
/// @param registry registry whose entries are sorted by case-insensitive name
/// @param name name to place
/// @return index of the first entry not below `name`; `registry->count` when none is
uint32_t lower_bound_name(const CategoryRegistry* registry, const char* name) noexcept {
    uint32_t first = 0;
    uint32_t length = registry->count;
    while (length > 0) {
        const uint32_t half = length / 2;
        if (formats::tdf::compare_nocase(registry->entries[first + half].name, name) < 0) {
            first += half + 1;
            length -= half + 1;
        } else {
            length = half;
        }
    }
    return first;
}

} // namespace

void category_mask_set(CategoryMask* mask, uint16_t type_id) noexcept {
    if ((type_id >> 5) >= category_mask_words)
        return; // past the 64-byte mask
    mask->words[type_id >> 5] |= 1u << (type_id & 31u);
}

void category_mask_or(CategoryMask* mask, const CategoryMask* other) noexcept {
    for (uint32_t word = 0; word < category_mask_words; ++word)
        mask->words[word] |= other->words[word];
}

bool category_mask_contains(const CategoryMask* mask, uint16_t type_id) noexcept {
    if ((type_id >> 5) >= category_mask_words)
        return false;
    return (mask->words[type_id >> 5] >> (type_id & 31u) & 1u) != 0;
}

void category_registry_init(CategoryRegistry* registry) noexcept {
    registry->entries = nullptr;
    registry->masks = nullptr;
    registry->count = 0;
    registry->capacity = 0;
}

void category_registry_clear(CategoryRegistry* registry) noexcept {
    for (uint32_t index = 0; index < registry->count; ++index)
        std::free(registry->entries[index].name);
    std::free(registry->entries);
    std::free(registry->masks);
    category_registry_init(registry);
}

oa_ref32 category_registry_ref(CategoryRegistry* registry, const char* name) noexcept {
    const uint32_t position = lower_bound_name(registry, name);
    if (position != registry->count &&
        formats::tdf::compare_nocase(registry->entries[position].name, name) == 0)
        return registry->entries[position].mask + 1u;
    if (registry->count == registry->capacity) {
        if (registry->capacity >= max_categories)
            return 0;
        const uint32_t grown = registry->capacity == 0 ? 64u : registry->capacity * 2u;
        auto* entries =
            static_cast<Category*>(std::realloc(registry->entries, sizeof(Category) * grown));
        if (entries == nullptr)
            return 0;
        registry->entries = entries;
        auto* masks =
            static_cast<CategoryMask*>(std::realloc(registry->masks, sizeof(CategoryMask) * grown));
        if (masks == nullptr)
            return 0;
        registry->masks = masks;
        registry->capacity = grown;
    }
    const std::size_t length = std::strlen(name);
    auto* copy = static_cast<char*>(std::malloc(length + 1));
    if (copy == nullptr)
        return 0;
    std::memcpy(copy, name, length + 1);
    Category* slot = &registry->entries[position];
    std::memmove(slot + 1, slot, sizeof(Category) * (registry->count - position));
    slot->name = copy;
    slot->mask = registry->count;
    std::memset(static_cast<void*>(&registry->masks[registry->count]), 0, sizeof(CategoryMask));
    ++registry->count;
    return slot->mask + 1u;
}

CategoryMask* category_registry_mask(CategoryRegistry* registry, oa_ref32 ref) noexcept {
    return ref != 0 && ref <= registry->count ? &registry->masks[ref - 1u] : nullptr;
}

const CategoryMask*
category_registry_mask(const CategoryRegistry* registry, oa_ref32 ref) noexcept {
    return ref != 0 && ref <= registry->count ? &registry->masks[ref - 1u] : nullptr;
}

CategoryMask* category_registry_find_or_add(CategoryRegistry* registry, const char* name) noexcept {
    return category_registry_mask(registry, category_registry_ref(registry, name));
}

const CategoryMask*
category_registry_find(const CategoryRegistry* registry, const char* name) noexcept {
    const uint32_t position = lower_bound_name(registry, name);
    if (position != registry->count &&
        formats::tdf::compare_nocase(registry->entries[position].name, name) == 0)
        return &registry->masks[registry->entries[position].mask];
    return nullptr;
}

bool register_unit_categories(
    CategoryRegistry* registry, const UnitDef* unit, const char* categories
) noexcept {
    const auto* at = reinterpret_cast<const unsigned char*>(categories);
    for (;;) {
        while (is_c_space(*at))
            ++at;
        if (*at == '\0')
            break;
        char token[category_token_capacity];
        std::size_t length = 0;
        while (*at != '\0' && !is_c_space(*at)) {
            if (length + 1 < sizeof token)
                token[length++] = static_cast<char>(*at);
            ++at;
        }
        token[length] = '\0';
        CategoryMask* mask = category_registry_find_or_add(registry, token);
        if (mask == nullptr)
            return false;
        category_mask_set(mask, unit->type_id);
    }
    CategoryMask* all = category_registry_find_or_add(registry, all_category);
    if (all == nullptr)
        return false;
    category_mask_set(all, unit->type_id);
    return true;
}

bool resolve_type_or_category(
    CategoryMask* target,
    CategoryRegistry* registry,
    const UnitDef* table,
    uint32_t count,
    const char* name
) noexcept {
    const uint16_t type_id = unit_defs_type_id(table, count, name);
    if (type_id != 0) {
        category_mask_set(target, type_id);
        return true;
    }
    const CategoryMask* category = category_registry_find_or_add(registry, name);
    if (category != nullptr)
        category_mask_or(target, category);
    return false;
}

} // namespace oa::data::defs
