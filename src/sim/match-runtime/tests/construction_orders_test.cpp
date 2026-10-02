// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/match_runtime/construction_orders.hpp"
#include "oa/test/check.hpp"
using namespace oa;

int main() {
    // Descriptor words must match the sorted mission table.
    using namespace sim::match_runtime;
    OA_CHECK(mission_descriptor_table.size() == mission_kind_count);
    OA_CHECK(mission_descriptor_table[mobile_build_kind] == mobile_build_flags);
    OA_CHECK(mission_descriptor_table[help_build_kind] == help_build_flags);
    OA_CHECK(mission_descriptor_table[patrol_kind] == patrol_flags);
    OA_CHECK(mission_descriptor_table[repair_unit_kind] == repair_unit_flags);
    OA_CHECK(mission_descriptor_table[reclaim_unit_kind] == reclaim_unit_flags);
    OA_CHECK(mission_descriptor_table[capture_kind] == capture_flags);
    OA_CHECK(mission_descriptor_table[ground_pickup_kind] == ground_pickup_flags);
    OA_CHECK(mission_descriptor_table[ground_unload_kind] == ground_unload_flags);
    OA_CHECK(mission_descriptor_table[get_built_kind] == get_built_flags);
    OA_CHECK(mission_descriptor_table[building_build_kind] == building_build_flags);
    OA_CHECK(mission_descriptor_table[follow_ground_kind] == follow_ground_flags);
    auto pad = vtol_get_repaired(false, 0, 10, 100);
    OA_CHECK(pad.result == 8 && pad.announce == 7 && !pad.wait);
    pad = vtol_get_repaired(true, 0, 10, 100);
    OA_CHECK(pad.result == 2 && pad.announce == 0 && pad.wait);
    pad = vtol_get_repaired(true, 0, 100, 100);
    OA_CHECK(pad.result == 1 && !pad.wait);
    pad = vtol_get_repaired(true, 1, 100, 100);
    OA_CHECK(pad.result == 5 && pad.announce == 10);
    pad = vtol_get_repaired(true, 2, 100, 100);
    OA_CHECK(pad.result == 7);
    // Health is sign-extended, then compared unsigned: negative health reads as above maximum.
    pad = vtol_get_repaired(true, 0, -1, 100);
    OA_CHECK(pad.result == 1 && !pad.wait);
    static_assert(blank_mission_step() == 5);

    sim::ground_orders::Point point{0x180000, 0, 0x180000};
    sim::match_runtime::snap_build_position(point, 2, 2);
    OA_CHECK(point[0] == 0x200000 && point[2] == 0x200000);
    return oa::test::check_exit_status();
}
