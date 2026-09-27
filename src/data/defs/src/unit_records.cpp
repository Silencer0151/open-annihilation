// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/data/defs/unit_records.hpp"

#include "oa/formats/tdf.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace oa::data::defs {
namespace {

constexpr std::size_t assigned_prefix = offsetof(UnitDef, abilities);

uint32_t load_abilities(const UnitDef* record) noexcept {
    uint32_t value;
    std::memcpy(
        &value, reinterpret_cast<const unsigned char*>(record) + assigned_prefix, sizeof value
    );
    return value;
}

void store_abilities(UnitDef* record, uint32_t value) noexcept {
    std::memcpy(reinterpret_cast<unsigned char*>(record) + assigned_prefix, &value, sizeof value);
}

uint32_t load_flags(const UnitDef* record) noexcept {
    uint32_t value;
    std::memcpy(
        &value,
        reinterpret_cast<const unsigned char*>(record) + offsetof(UnitDef, flags),
        sizeof value
    );
    return value;
}

/// Picks the median of three records by value.
///
/// @param a first candidate
/// @param b second candidate
/// @param c third candidate
/// @param less strict ordering of two records
/// @return a copy of the median record
UnitDef
median_of_three(const UnitDef& a, const UnitDef& b, const UnitDef& c, UnitDefLess less) noexcept {
    if (less(&a, &b)) {
        if (less(&b, &c))
            return b;
        return less(&a, &c) ? c : a;
    }
    if (less(&a, &c))
        return a;
    return less(&b, &c) ? c : b;
}

/// Partitions a range around a pivot value (Hoare scheme).
///
/// @param[in,out] first start of the range
/// @param last end of the range (exclusive)
/// @param pivot pivot value, held outside the range
/// @param less strict ordering of two records
/// @return the cut: records before it do not order after the pivot, records
///     from it on do not order before it
UnitDef* partition(UnitDef* first, UnitDef* last, const UnitDef& pivot, UnitDefLess less) noexcept {
    for (;;) {
        while (less(first, &pivot))
            ++first;
        --last;
        while (less(&pivot, last))
            --last;
        if (!(first < last))
            return first;
        const UnitDef held = *first;
        unit_def_assign(first, last);
        unit_def_assign(last, &held);
        ++first;
    }
}

/// Quicksorts a range until every unsorted run is at most unit_def_sort_run records.
///
/// Recurses into the smaller side of each cut and loops on the larger.
///
/// @param[in,out] first start of the range
/// @param last end of the range (exclusive)
/// @param less strict ordering of two records
void quicksort_loop(UnitDef* first, UnitDef* last, UnitDefLess less) noexcept {
    while (last - first > static_cast<std::ptrdiff_t>(unit_def_sort_run)) {
        const UnitDef pivot = median_of_three(*first, first[(last - first) / 2], last[-1], less);
        UnitDef* cut = partition(first, last, pivot, less);
        if (cut - first < last - cut) {
            quicksort_loop(first, cut, less);
            first = cut;
        } else {
            quicksort_loop(cut, last, less);
            last = cut;
        }
    }
}

/// Inserts a value by shifting larger records up one slot until it fits.
///
/// There is no lower-bound check: a record not above `value` must precede `last`.
///
/// @param[in,out] last slot the value comes from; records before it are sorted
/// @param value record to insert, held by value
/// @param less strict ordering of two records
void unguarded_insert(UnitDef* last, const UnitDef value, UnitDefLess less) noexcept {
    UnitDef* next = last - 1;
    while (less(&value, next)) {
        unit_def_assign(last, next);
        last = next;
        --next;
    }
    unit_def_assign(last, &value);
}

/// Insertion-sorts a range, checking the lower bound.
///
/// @param[in,out] first start of the range
/// @param last end of the range (exclusive)
/// @param less strict ordering of two records
void insertion_sort(UnitDef* first, UnitDef* last, UnitDefLess less) noexcept {
    if (first == last)
        return;
    for (UnitDef* at = first + 1; at != last; ++at) {
        const UnitDef value = *at;
        if (less(&value, first)) {
            for (UnitDef* move = at; move != first; --move)
                unit_def_assign(move, move - 1);
            unit_def_assign(first, &value);
        } else {
            unguarded_insert(at, value, less);
        }
    }
}

/// Finds the first record whose unit name does not sort before a name.
///
/// @param first start of a range sorted by case-insensitive unit name
/// @param last end of the range (exclusive)
/// @param name unit name to place
/// @return the first record not below `name`, or `last`
const UnitDef*
unit_defs_lower_bound(const UnitDef* first, const UnitDef* last, const char* name) noexcept {
    std::ptrdiff_t length = last - first;
    while (length > 0) {
        const std::ptrdiff_t half = length / 2;
        const UnitDef* middle = first + half;
        if (formats::tdf::compare_nocase(middle->unit_name, name) < 0) {
            first = middle + 1;
            length -= half + 1;
        } else {
            length = half;
        }
    }
    return first;
}

} // namespace

void unit_def_assign(UnitDef* target, const UnitDef* source) noexcept {
    const uint32_t kept = load_abilities(target) & ~unit_def_abilities_assigned_mask;
    const uint32_t copied = load_abilities(source) & unit_def_abilities_assigned_mask;
    std::memmove(target, source, assigned_prefix);
    store_abilities(target, kept | copied);
}

bool unit_def_is_unavailable(const UnitDef* record) noexcept {
    return (load_flags(record) & unit_def_flag_available) == 0;
}

bool unit_def_name_less(const UnitDef* left, const UnitDef* right) noexcept {
    return formats::tdf::compare_nocase(left->unit_name, right->unit_name) < 0;
}

UnitDef* unit_defs_remove_if(UnitDef* first, UnitDef* last, UnitDefPredicate remove) noexcept {
    while (first != last && !remove(first))
        ++first;
    if (first == last)
        return first;
    for (UnitDef* at = first + 1; at != last; ++at) {
        if (!remove(at)) {
            unit_def_assign(first, at);
            ++first;
        }
    }
    return first;
}

void unit_defs_sort(UnitDef* first, UnitDef* last, UnitDefLess less) noexcept {
    if (last - first <= static_cast<std::ptrdiff_t>(unit_def_sort_run)) {
        insertion_sort(first, last, less);
        return;
    }
    quicksort_loop(first, last, less);
    UnitDef* run_end = first + unit_def_sort_run;
    insertion_sort(first, run_end, less);
    for (UnitDef* at = run_end; at != last; ++at)
        unguarded_insert(at, *at, less);
}

uint32_t unit_defs_finalize_catalog(UnitDef* table, uint32_t count) noexcept {
    if (count == 0)
        return 0;
    UnitDef* end = unit_defs_remove_if(table + 1, table + count, unit_def_is_unavailable);
    const auto kept = static_cast<uint32_t>(end - table);
    unit_defs_sort(table + 1, end, unit_def_name_less);
    for (uint32_t index = 0; index < kept; ++index)
        table[index].type_id = static_cast<uint16_t>(index);
    return kept;
}

const UnitDef* unit_defs_find(const UnitDef* table, uint32_t count, const char* name) noexcept {
    if (count <= 1)
        return nullptr;
    const UnitDef* last = table + count;
    const UnitDef* first = unit_defs_lower_bound(table + 1, last, name);
    if (first == last || formats::tdf::compare_nocase(name, first->unit_name) != 0)
        return nullptr;
    return first;
}

uint16_t unit_defs_type_id(const UnitDef* table, uint32_t count, const char* name) noexcept {
    const UnitDef* record = unit_defs_find(table, count, name);
    return record != nullptr ? record->type_id : 0;
}

} // namespace oa::data::defs
