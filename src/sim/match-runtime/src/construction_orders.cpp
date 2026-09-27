// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/match_runtime/construction_orders.hpp"
#include <bit>
#include <cstdint>

namespace oa::sim::match_runtime {
namespace {
int16_t arithmetic_shift20(uint32_t value) {
    const auto high = value >> 20;
    const auto bits = (value & 0x80000000u) ? high | 0xfffff000u : high;
    return std::bit_cast<int16_t>(static_cast<uint16_t>(bits));
}

uint32_t as_unsigned(int32_t value) {
    return std::bit_cast<uint32_t>(value);
}
} // namespace

void snap_build_position(
    sim::ground_orders::Point& destination, int16_t footprint_x, int16_t footprint_z
) {
    const auto fx = static_cast<int32_t>(footprint_x);
    const auto fz = static_cast<int32_t>(footprint_z);
    const auto cell_x = arithmetic_shift20(
        as_unsigned(destination[0]) - static_cast<uint32_t>(fx) * 0x80000u + 0x80000u
    );
    const auto cell_z = arithmetic_shift20(
        as_unsigned(destination[2]) - static_cast<uint32_t>(fz) * 0x80000u + 0x80000u
    );
    destination[0] = (fx + static_cast<int32_t>(cell_x) * 2) * 0x80000;
    destination[2] = (fz + static_cast<int32_t>(cell_z) * 2) * 0x80000;
}

} // namespace oa::sim::match_runtime
