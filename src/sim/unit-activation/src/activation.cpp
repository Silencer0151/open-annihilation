// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/unit_activation.hpp"

#include <cstdint>

namespace oa::sim::unit_activation {
void change(uint8_t& flags, const uint16_t& unit_index, uint8_t mask, bool enabled, Host& host) {
    const auto before = flags;
    flags = enabled ? static_cast<uint8_t>(flags | mask) : static_cast<uint8_t>(flags & ~mask);
    if (flags == before)
        return;
    const auto added = static_cast<uint8_t>(flags & ~before);
    const auto removed = static_cast<uint8_t>(before & ~flags);
    if (added & active_mask) {
        host.script("Activate");
        host.sound(Sound::activate);
    }
    if (removed & active_mask) {
        host.script("Deactivate");
        host.sound(Sound::deactivate);
    }
    if (added & building_mask)
        host.script("StartBuilding");
    if (removed & building_mask)
        host.script("StopBuilding");
    if (added & cloaked_mask) {
        host.sound(Sound::cloak);
        host.notify_attachments(cloak_notification);
    }
    if (removed & cloaked_mask)
        host.sound(Sound::decloak);
    host.refresh_selected_unit();
    if (host.owner_simulates_here())
        host.flags_changed(unit_index, flags);
}

void mark_owned_selection(
    std::span<ListedUnit> units, std::span<const uint16_t> selected, uint8_t local_player
) {
    for (const auto id : selected) {
        auto& unit = units[id];
        if (unit.owner == local_player)
            unit.flags = (unit.flags & owned_selection_keep) | owned_selection_bit;
    }
}

void clear_cycle_marks(std::span<ListedUnit> units) {
    for (auto& unit : units)
        unit.flags &= cycle_marks_keep;
}
} // namespace oa::sim::unit_activation
