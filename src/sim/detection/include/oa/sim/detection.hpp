// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Radar, sonar and line-of-sight contacts of the viewpoint player: the
// per-unit steps of the contact scan that decides which other players' units
// the viewer sees on radar and which it sees below the waterline. A unit whose
// top is under the sea is drawn and targetable by an enemy only while it is a
// sonar contact.
//
// Each player also keeps sightings of other players' units, rebuilt every 30
// ticks; target searches draw on them, and the contact scan uncloaks a unit
// while an enemy its owner sees is inside its mincloakdistance.
#pragma once

#include "oa/core/world.h"

#include <cstdint>

namespace oa::sim::detection {

// Unit.flags bits the scan owns.
inline constexpr uint32_t radar_contact = OA_UNIT_FLAG_RADAR_CONTACT;
// Core names the bit for its first writer: the scan sets it on the viewer's
// own and radar-sharing units as well as on sonar contacts.
inline constexpr uint32_t sonar_contact = OA_UNIT_FLAG_VIEWPOINT_OWNED;
inline constexpr uint32_t jammed = OA_UNIT_FLAG_JAMMED;
inline constexpr uint32_t contact_bits = radar_contact | sonar_contact | jammed;

// Player-info role bit an owner sets to share its radar with allies.
inline constexpr uint8_t share_radar_role = 0x40;

// Unit.flags bits of the cloak: the Cloak_On order sets `cloak_ordered`, and
// the scan sets `cloak_locked` until its next pass while an enemy is close.
inline constexpr uint32_t cloak_ordered = OA_UNIT_FLAG_CLOAK_RUNNING;
inline constexpr uint32_t cloak_locked = OA_UNIT_FLAG_CLOAK_LOCKED;
// Unit.flags bit that keeps a unit off every player's seen list.
inline constexpr uint32_t unsighted = OA_UNIT_FLAG_NOT_SELECTABLE;

// Ticks between rebuilds of a player's sightings.
inline constexpr uint32_t sighting_period = 30;
// Ticks an enemy inside mincloakdistance holds the cloak off.
inline constexpr uint32_t decloak_hold_ticks = 90;

// Reach and position of one scanning unit, as the contact scan walks it.
struct ScanRecord {
    int32_t radar_range_squared{}; // world units squared
    int32_t sonar_range_squared{};
    FixedVec3 position{};
};

// One player's sightings of other players' units, by unit slot: a seen list
// and a radar list with their counts. The caller owns the list storage,
// `capacity` slots for each list.
struct Sightings {
    uint16_t* seen{};  // enemies in the player's line of sight
    uint16_t* radar{}; // enemies carrying the radar-contact bit
    uint32_t capacity{};
    uint32_t seen_count{};
    uint32_t radar_count{};
    uint32_t refreshed_tick{};
    // The player owns a finished, switched-on targeting facility: target
    // searches that find nothing seen fall back to the radar list.
    uint8_t radar_fallback{};
};

/// Returns the squared horizontal distance between two points as the game sums it.
///
/// @param a first point, 16.16 world coordinates
/// @param b second point, 16.16 world coordinates
/// @return the high 32 bits of each 64-bit square of the 16.16 delta, Z first, added with
///         32-bit wraparound; Y is ignored
int32_t squared_distance_high(const FixedVec3& a, const FixedVec3& b) noexcept;

/// Tests whether an owner shares its radar with a viewer.
///
/// @param world player records
/// @param viewer player looking
/// @param owner player whose radar may be shared
/// @return true when the owner's alliance row names the viewer and the owner's player-info
///         role has the share-radar bit
bool shares_radar(World& world, const Player& viewer, const Player& owner) noexcept;

/// Stamps one unit inside a scanner's walk.
///
/// A live, non-stealth unit of another player is a sonar contact at or below sea level
/// inside the sonar reach, and a radar contact when its top is at or above sea level
/// inside the radar reach.
///
/// @param scan scanning unit's reach and position
/// @param[in,out] unit unit walked over; gains contact bits
/// @param world sea level and unit types
void stamp_contact(const ScanRecord& scan, Unit& unit, const World& world) noexcept;

/// Marks a unit inside a radar jammer's walk: it leaves radar and is marked jammed.
///
/// @param[in,out] unit unit walked over
void jam_radar(Unit& unit) noexcept;

/// Marks a unit inside a sonar jammer's walk: it leaves sonar and is marked jammed.
///
/// @param[in,out] unit unit walked over
void jam_sonar(Unit& unit) noexcept;

/// Tests whether a player's sightings are due for a rebuild.
///
/// @param sightings the player's sightings
/// @param tick current game tick
/// @return true once a full 30-tick period has passed since the last rebuild
bool sightings_due(const Sightings& sightings, uint32_t tick) noexcept;

/// Empties both sighting lists and the radar fallback before a rebuild.
///
/// @param[in,out] sightings the player's sightings
void clear_sightings(Sightings& sightings) noexcept;

/// Tests whether a unit is live, not waiting to die, and finished.
///
/// @param unit unit to test
/// @return true when finished; an unordered (NaN) build fraction counts as finished
bool finished_unit(const Unit& unit) noexcept;

/// Tests whether the sightings rebuild asks whether a player sees a unit.
///
/// @param world player records
/// @param player player whose sightings are rebuilt
/// @param unit unit walked over
/// @return true when the unit is active and its owner is outside the player's alliance row
bool sighting_candidate(const World& world, const Player& player, const Unit& unit) noexcept;

/// Files one unit slot into a player's sightings.
///
/// An active enemy enters the seen list when `seen` holds and it is not unsighted, and
/// the radar list when it is a radar contact. The player's own finished, switched-on
/// targeting facility turns the radar fallback on.
///
/// @param[in,out] sightings the player's sightings
/// @param world unit table and types
/// @param player player whose sightings are rebuilt
/// @param unit unit walked over
/// @param seen result of the player's sight test of the unit
/// @return true for an active, finished unit of the player's own, which the same walk
///         counts into the player's computer knowledge
bool file_sighting(
    Sightings& sightings, const World& world, const Player& player, const Unit& unit, bool seen
) noexcept;

/// Tests whether an active unit on the seen list stands within a distance of a position.
///
/// @param sightings the player's sightings
/// @param world unit table
/// @param position centre, 16.16 world coordinates
/// @param distance radius on the ground plane, world units
/// @return true when one does
bool sighted_within(
    const Sightings& sightings, const World& world, const FixedVec3& position, int16_t distance
) noexcept;

/// Holds a cloaker's cloak off while an enemy is inside its mincloakdistance.
///
/// Upkeep stops for 90 ticks, and the lock holds the cloak off until the next scan
/// clears it.
///
/// @param[in,out] unit cloaking unit
/// @param tick current game tick
void expose_cloaker(Unit& unit, uint32_t tick) noexcept;

} // namespace oa::sim::detection
