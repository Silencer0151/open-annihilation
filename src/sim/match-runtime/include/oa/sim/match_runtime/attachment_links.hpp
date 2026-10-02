// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// A unit's attachment links, the carrier, the first unit it carries and the
// next unit carried beside it, as unit slot indices.
#pragma once

#include "oa/core/world.h"

#include <cstdint>

namespace oa::sim::match_runtime {

// Attachment links (Unit.attach_parent, attach_first_child and attach_next)
// as unit slot indices; 0 is none.

/// Returns the unit slot of a unit's carrier (Unit.attach_parent).
///
/// @param unit Unit record.
/// @return The carrier's slot, 0 for none.
inline uint16_t link_parent(const oa::Unit& unit) noexcept {
    return static_cast<uint16_t>(oa::oa_unit_slot_from_ref(unit.attach_parent));
}

/// Returns the unit slot of the first unit a unit carries
/// (Unit.attach_first_child).
///
/// @param unit Unit record.
/// @return The first carried unit's slot, 0 for none.
inline uint16_t link_first_child(const oa::Unit& unit) noexcept {
    return static_cast<uint16_t>(oa::oa_unit_slot_from_ref(unit.attach_first_child));
}

/// Returns the unit slot of the next unit carried with this one
/// (Unit.attach_next).
///
/// @param unit Unit record.
/// @return The next carried unit's slot, 0 for none.
inline uint16_t link_next(const oa::Unit& unit) noexcept {
    return static_cast<uint16_t>(oa::oa_unit_slot_from_ref(unit.attach_next));
}

/// Sets a unit's carrier (Unit.attach_parent).
///
/// @param[in,out] unit Unit record.
/// @param slot The carrier's slot, 0 for none.
inline void set_link_parent(oa::Unit& unit, uint32_t slot) noexcept {
    unit.attach_parent = oa::oa_unit_ref_from_slot(slot);
}

/// Sets the first unit a unit carries (Unit.attach_first_child).
///
/// @param[in,out] unit Unit record.
/// @param slot The first carried unit's slot, 0 for none.
inline void set_link_first_child(oa::Unit& unit, uint32_t slot) noexcept {
    unit.attach_first_child = oa::oa_unit_ref_from_slot(slot);
}

/// Sets the next unit carried with this one (Unit.attach_next).
///
/// @param[in,out] unit Unit record.
/// @param slot The next carried unit's slot, 0 for none.
inline void set_link_next(oa::Unit& unit, uint32_t slot) noexcept {
    unit.attach_next = oa::oa_unit_ref_from_slot(slot);
}

} // namespace oa::sim::match_runtime
