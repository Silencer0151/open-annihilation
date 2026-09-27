// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Mission tag table registration, ordering and lookup.
#include "oa/ui/console/unit_tags.hpp"

#include <cstring>
#include <iterator>

namespace oa::ui::console {
namespace {

// Sort runs at or below this length are left to the final insertion pass.
constexpr ptrdiff_t kInsertionRun = 16;

/// Lowers an ASCII capital letter; other characters are returned unchanged.
///
/// @param c Character.
/// @return The lower-case character.
char lower_ascii(char c) noexcept {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

/// Compares two names, ignoring ASCII case.
///
/// @param a First name.
/// @param b Second name.
/// @return Negative, zero or positive as `a` sorts before, with or after `b`.
int compare_nocase(const char* a, const char* b) noexcept {
    for (;; ++a, ++b) {
        const auto ca = static_cast<unsigned char>(lower_ascii(*a));
        const auto cb = static_cast<unsigned char>(lower_ascii(*b));
        if (ca != cb || ca == 0)
            return static_cast<int>(ca) - static_cast<int>(cb);
    }
}

// The four static registration blocks, each in 3.1c's record order.
// The unnamed Ready record, which sorts first.
const MissionTagRecord kReadyRecords[] = {
    {"Ready", 0x00000000, {0x0f, 0x00, 0x00, 0x00, 0x00}, ""},
};
// Ground-unit missions.
const MissionTagRecord kGroundRecords[] = {
    {"Standby", 0x00000010, {0x0f, 0x00, 0x00, 0x02, 0x00}, "Standby"},
    {"Standby", 0x00000010, {0x0f, 0x00, 0x00, 0x02, 0x01}, "Standby_Mine"},
    {"Moving", 0x00000012, {0x0e, 0x02, 0x04, 0x00, 0x00}, "Move_Ground"},
    {"Guarding", 0x00000012, {0x05, 0x00, 0x02, 0x00, 0x00}, "Follow_Ground"},
    {"Suppressing fire", 0x00000008, {0x01, 0x10, 0x04, 0x00, 0x00}, "Suppress"},
    {"Attacking", 0x00000008, {0x01, 0x80, 0x02, 0x00, 0x00}, "Attack_Chase"},
    {"Attacking", 0x00000008, {0x01, 0x00, 0x06, 0x00, 0x00}, "Attack_Kamikaze"},
    {"Annihilating", 0x00000008, {0x01, 0x80, 0x06, 0x00, 0x00}, "AttackSpecial"},
    {"Parking", 0x00000000, {0x0e, 0x00, 0x00, 0x00, 0x00}, "Park"},
    {"Patrolling", 0x00000012, {0x07, 0x12, 0x04, 0x00, 0x00}, "Patrol"},
    {"Loading", 0x00000008, {0x0c, 0x00, 0x02, 0x00, 0x00}, "Ground_Pickup"},
    {"Unloading", 0x00000008, {0x0d, 0x00, 0x04, 0x00, 0x00}, "Ground_Unload"},
    {"Teleporting", 0x00000008, {0x09, 0x00, 0x06, 0x00, 0x00}, "Teleport"},
    {"Nanolathing", 0x00000013, {0x00, 0x08, 0x05, 0x10, 0x00}, "MobileBuild"},
    {"Nanolathing", 0x00000018, {0x06, 0x08, 0x02, 0x10, 0x00}, "HelpBuild"},
    {"Repair patrol", 0x00000012, {0x07, 0x12, 0x04, 0x00, 0x00}, "RepairPatrol"},
    {"Repairing", 0x00000012, {0x06, 0x00, 0x02, 0x10, 0x00}, "RepairUnit"},
    {"Capturing", 0x00000008, {0x04, 0x00, 0x02, 0x00, 0x00}, "Capture"},
    {"Resurrecting", 0x00000012, {0x0b, 0x00, 0x02, 0x00, 0x00}, "Resurrect"},
    {"Reclaiming", 0x00000012, {0x0b, 0x00, 0x08, 0x10, 0x00}, "Reclaim"},
    {"Reclaiming", 0x00000012, {0x0b, 0x00, 0x02, 0x10, 0x00}, "ReclaimUnit"},
    {"Repairing", 0x00000018, {0x06, 0x00, 0x02, 0x00, 0x00}, "RepairUnitNoMove"},
};
// Aircraft missions.
const MissionTagRecord kAirRecords[] = {
    {"Standby", 0x00000000, {0x0f, 0x00, 0x00, 0x02, 0x00}, "VTOL_Standby"},
    {"Moving", 0x00000002, {0x0e, 0x02, 0x04, 0x00, 0x00}, "VTOL_Move"},
    {"Landing", 0x00000008, {0x0e, 0x00, 0x06, 0x00, 0x00}, "VTOL_Landing"},
    {"Loading", 0x00000008, {0x08, 0x00, 0x02, 0x00, 0x00}, "VTOL_Pickup"},
    {"Unloading", 0x00000008, {0x09, 0x00, 0x04, 0x00, 0x00}, "VTOL_Unload"},
    {"Guarding", 0x00000002, {0x05, 0x00, 0x02, 0x00, 0x00}, "VTOL_Follow"},
    {"Patrolling", 0x00000002, {0x07, 0x12, 0x04, 0x00, 0x00}, "VTOL_Patrol"},
    {"Airstrike", 0x00000008, {0x02, 0x00, 0x06, 0x00, 0x00}, "AirStrike"},
    {"Engaging target", 0x00000008, {0x01, 0x00, 0x02, 0x00, 0x00}, "AirToAir"},
    {"Engaging target", 0x00000008, {0x01, 0x00, 0x02, 0x00, 0x00}, "AirToGround"},
    {"Engaging target", 0x00000008, {0x01, 0x00, 0x02, 0x00, 0x00}, "AirToGroundHover"},
    {"Nanolathing", 0x00000003, {0x00, 0x08, 0x05, 0x10, 0x00}, "VTOL_MobileBuild"},
    {"Nanolathing", 0x00000008, {0x06, 0x08, 0x02, 0x10, 0x00}, "VTOL_HelpBuild"},
    {"Repair patrol", 0x00000002, {0x07, 0x12, 0x04, 0x00, 0x00}, "VTOL_RepairPatrol"},
    {"Repairing", 0x00000002, {0x06, 0x00, 0x02, 0x10, 0x00}, "VTOL_RepairUnit"},
    {"Reclaiming", 0x00000002, {0x0b, 0x00, 0x08, 0x10, 0x00}, "VTOL_Reclaim"},
    {"Reclaiming", 0x00000002, {0x0b, 0x00, 0x02, 0x10, 0x00}, "VTOL_ReclaimUnit"},
    {"Evading", 0x00000000, {0x13, 0x00, 0x00, 0x00, 0x00}, "VTOL_Evade"},
    {"Seeking to attack", 0x00000000, {0x13, 0x00, 0x06, 0x00, 0x00}, "VTOL_SeekAttack"},
    {"Seeking to guard", 0x00000000, {0x13, 0x00, 0x06, 0x00, 0x00}, "VTOL_SeekGuard"},
    {"Under repair", 0x00000000, {0x13, 0x00, 0x02, 0x00, 0x00}, "VTOL_GetRepaired"},
    {"Seeking to land", 0x00000000, {0x13, 0x00, 0x04, 0x00, 0x00}, "VTOL_LandIfCan"},
};
// Orders and unit states shared by every unit.
const MissionTagRecord kCommandRecords[] = {
    {"Stopping", 0x00000000, {0x13, 0x00, 0x00, 0x00, 0x00}, "Stop"},
    {"Attacking", 0x00000008, {0x01, 0x80, 0x02, 0x00, 0x00}, "Attack_NoMove"},
    {"Activate", 0x00000000, {0x13, 0x60, 0x00, 0x01, 0x00}, "Activate"},
    {"Deactivate", 0x00000000, {0x13, 0x60, 0x00, 0x01, 0x00}, "Deactivate"},
    {"Cloaking", 0x00000000, {0x13, 0x60, 0x00, 0x01, 0x00}, "Cloak_On"},
    {"Decloaking", 0x00000000, {0x13, 0x60, 0x00, 0x01, 0x00}, "Cloak_Off"},
    {"Acknowledged", 0x00000000, {0x13, 0x60, 0x00, 0x01, 0x00}, "Standing_MoveOrder"},
    {"Acknowledged", 0x00000000, {0x13, 0x60, 0x00, 0x01, 0x00}, "Standing_FireOrder"},
    {"Nanolathing", 0x00000000, {0x13, 0x0c, 0x01, 0x10, 0x00}, "BuildingBuild"},
    {"Nanolathing", 0x00000000, {0x13, 0x40, 0x01, 0x0c, 0x00}, "BuildWeapon"},
    {"SELF DESTRUCT ENGAGED", 0x00000000, {0x13, 0x40, 0x00, 0x04, 0x00}, "SelfDestruct"},
    {"SELF DESTRUCT ENGAGED", 0x00000000, {0x13, 0x00, 0x00, 0x00, 0x00}, "SelfDestructFG"},
    {"Paralyzed", 0x00000000, {0x13, 0x24, 0x00, 0x00, 0x00}, "Paralyze"},
    {"Under construction", 0x00000000, {0x13, 0x24, 0x02, 0x00, 0x00}, "GetBuilt"},
    {"Being transported", 0x00000000, {0x13, 0x24, 0x00, 0x00, 0x00}, "BeCarried"},
    {"Unit is available", 0x00000000, {0x13, 0x04, 0x00, 0x00, 0x00}, "MakeSelectable"},
    {"Waiting", 0x00000000, {0x13, 0x04, 0x00, 0x00, 0x00}, "Wait"},
    {"Waiting for attack", 0x00000000, {0x13, 0x04, 0x02, 0x00, 0x00}, "WaitForAttack"},
    {"Attacking", 0x00000000, {0x13, 0x04, 0x00, 0x00, 0x00}, "AttackUType"},
    {"Ready", 0x00000000, {0x13, 0x20, 0x00, 0x00, 0x00}, "Guard_NoMove"},
    {"Repairing", 0x00000000, {0x13, 0x04, 0x02, 0x00, 0x01}, "SelfRepair"},
    {"Ready with orders", 0x00000002, {0x0e, 0x00, 0x04, 0x00, 0x00}, "QMove"},
    {"Ready with orders", 0x00000002, {0x07, 0x00, 0x04, 0x00, 0x00}, "QPatrol"},
};

/// Sorts a range by name with an insertion sort.
///
/// A record below the first moves to the front at once; the others move down
/// until an earlier record is not above them.
///
/// @param[in,out] first First record of the range.
/// @param last One past the last record.
void insertion_sort(MissionTagRecord* first, MissionTagRecord* last) noexcept {
    if (first == last)
        return;
    for (MissionTagRecord* next = first + 1; next != last; ++next) {
        const MissionTagRecord value = *next;
        MissionTagRecord* hole = next;
        if (mission_tag_less(value, *first)) {
            std::memmove(first + 1, first, static_cast<size_t>(next - first) * sizeof *first);
            *first = value;
            continue;
        }
        while (mission_tag_less(value, *(hole - 1))) {
            *hole = *(hole - 1);
            --hole;
        }
        *hole = value;
    }
}

/// Insertion-sorts a range whose every record has a smaller-or-equal record before it.
///
/// Past the first run every record has a smaller-or-equal record before it,
/// so the inner loop needs no bound check.
///
/// @param[in,out] first First record to place.
/// @param last One past the last record.
void unguarded_insertion_sort(MissionTagRecord* first, MissionTagRecord* last) noexcept {
    for (MissionTagRecord* next = first; next != last; ++next) {
        const MissionTagRecord value = *next;
        MissionTagRecord* hole = next;
        while (mission_tag_less(value, *(hole - 1))) {
            *hole = *(hole - 1);
            --hole;
        }
        *hole = value;
    }
}

/// Picks the median of three records by name.
///
/// @param a First record.
/// @param b Middle record.
/// @param c Last record.
/// @return The record whose name lies between the other two.
const MissionTagRecord& median_of_three(
    const MissionTagRecord& a, const MissionTagRecord& b, const MissionTagRecord& c
) noexcept {
    if (mission_tag_less(a, b)) {
        if (mission_tag_less(b, c))
            return b;
        return mission_tag_less(a, c) ? c : a;
    }
    if (mission_tag_less(a, c))
        return a;
    return mission_tag_less(b, c) ? c : b;
}

/// Partitions a range around a pivot name.
///
/// @param[in,out] first First record of the range.
/// @param last One past the last record.
/// @param pivot Pivot record (a copy, so swaps do not move it).
/// @return The first record of the upper part; every record before it is not
///         above the pivot and every record from it on is not below.
MissionTagRecord*
partition(MissionTagRecord* first, MissionTagRecord* last, const MissionTagRecord& pivot) noexcept {
    for (;;) {
        while (mission_tag_less(*first, pivot))
            ++first;
        --last;
        while (mission_tag_less(pivot, *last))
            --last;
        if (!(first < last))
            return first;
        const MissionTagRecord swap = *first;
        *first = *last;
        *last = swap;
        ++first;
    }
}

/// Quicksorts a range until every unsorted run is at most kInsertionRun records.
///
/// The pivot is the median of the first, middle and last records. The smaller
/// side is sorted by recursion and the larger one by the loop, which bounds
/// the recursion depth. The runs left are ordered by the insertion pass.
///
/// @param[in,out] first First record of the range.
/// @param last One past the last record.
void quicksort_runs(MissionTagRecord* first, MissionTagRecord* last) noexcept {
    while (last - first > kInsertionRun) {
        const MissionTagRecord pivot =
            median_of_three(*first, first[(last - first) / 2], *(last - 1));
        MissionTagRecord* middle = partition(first, last, pivot);
        if (middle - first < last - middle) {
            quicksort_runs(first, middle);
            first = middle;
        } else {
            quicksort_runs(middle, last);
            last = middle;
        }
    }
}

} // namespace

bool mission_tag_less(const MissionTagRecord& a, const MissionTagRecord& b) noexcept {
    return compare_nocase(a.name, b.name) < 0;
}

bool mission_tag_name_less(const MissionTagRecord& record, const char* name) noexcept {
    return compare_nocase(record.name, name) < 0;
}

void mission_tags_sort(MissionTagRecord* first, MissionTagRecord* last) noexcept {
    if (last - first <= kInsertionRun) {
        insertion_sort(first, last);
        return;
    }
    quicksort_runs(first, last);
    insertion_sort(first, first + kInsertionRun);
    unguarded_insertion_sort(first + kInsertionRun, last);
}

bool mission_tags_insert(
    MissionTagTable* table, const MissionTagRecord* block, uint32_t count
) noexcept {
    if (count > kMissionTagCapacity - table->count)
        return false;
    std::memcpy(table->records + table->count, block, count * sizeof *block);
    table->count += count;
    mission_tags_sort(table->records, table->records + table->count);
    return true;
}

const MissionTagRecord* mission_tags_lower_bound(
    const MissionTagRecord* first, const MissionTagRecord* last, const char* name
) noexcept {
    ptrdiff_t length = last - first;
    while (length > 0) {
        const ptrdiff_t half = length / 2;
        const MissionTagRecord* middle = first + half;
        if (mission_tag_name_less(*middle, name)) {
            first = middle + 1;
            length -= half + 1;
        } else {
            length = half;
        }
    }
    return first;
}

uint8_t mission_tags_find(const MissionTagTable* table, const char* name) noexcept {
    const MissionTagRecord* end = table->records + table->count;
    const MissionTagRecord* found = mission_tags_lower_bound(table->records, end, name);
    if (found == end || compare_nocase(found->name, name) != 0)
        return 0;
    return static_cast<uint8_t>(found - table->records);
}

const MissionTagRecord* mission_tags_entry(const MissionTagTable* table, uint8_t index) noexcept {
    return index < table->count ? &table->records[index] : nullptr;
}

bool register_ground_missions(MissionTagTable* table) noexcept {
    return mission_tags_insert(table, kGroundRecords, std::size(kGroundRecords));
}

bool register_air_missions(MissionTagTable* table) noexcept {
    return mission_tags_insert(table, kAirRecords, std::size(kAirRecords));
}

bool mission_tags_register_static(MissionTagTable* table) noexcept {
    bool ok = mission_tags_insert(table, kReadyRecords, std::size(kReadyRecords));
    ok = register_ground_missions(table) && ok;
    ok = register_air_missions(table) && ok;
    ok = mission_tags_insert(table, kCommandRecords, std::size(kCommandRecords)) && ok;
    return ok;
}

} // namespace oa::ui::console
