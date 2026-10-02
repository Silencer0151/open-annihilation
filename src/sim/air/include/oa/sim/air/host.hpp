// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// World queries the air goals and the air driver need from the simulation
// that owns them: the spatial buckets, query points and terrain the World
// does not hold. The aircraft missions themselves are run by the match's
// order tick, which hands goals to the driver. A null query behaves as
// documented on each member.

#include "oa/core/world.h"

#include <cstdint>

namespace oa::sim::air {

struct AirHost {
    void* context{};
    World* world{};

    // Terrain height byte of the unit's spatial bucket. Null: 0.
    uint8_t (*bucket_height)(void* context, const Unit* unit){};
    // Whether the unit sits in the bucket of units outside the map. Null:
    // false.
    bool (*outside_map)(void* context, const Unit* unit){};
    // World position of one of the unit's query points; -1 is its origin.
    // Null: the unit position.
    FixedVec3 (*query_point)(void* context, const Unit* unit, int16_t index){};
    // Interpolated terrain height at X/Z, -1 off the map interior. Null: -1.
    int32_t (*terrain_height)(void* context, oa_fixed x, oa_fixed z){};
};

} // namespace oa::sim::air
