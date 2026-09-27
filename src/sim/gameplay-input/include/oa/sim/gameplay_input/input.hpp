// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "oa/formats/objects3d.hpp"
#include "oa/sim/model_runtime/instance.hpp"
#include "oa/sim/unit_movement/terrain.hpp"
#include <cstdint>
#include <span>
#include <vector>

namespace oa::sim::gameplay_input {
struct ScreenPoint {
    int32_t x{}, y{};
};

struct Camera {
    int32_t pixel_x{}, pixel_z{};
};

struct PickUnit {
    uint16_t id{};
    formats::objects3d::FixedVector3 position{};
    sim::model_runtime::RotationWords rotation{};
    const formats::objects3d::Model* model{};
};

/// Projects a model-local point of a unit onto the screen.
///
/// @param local point in the unit's model space, 16.16
/// @param world unit position, 16.16 world coordinates
/// @param camera camera position, whole pixels
/// @return screen pixel: x from world X, y from world Z less half the height, offset by
///         the battlefield origin (+128, +32)
/// @quirk Every add and subtract wraps at 32 bits before the high words are taken.
[[nodiscard]] ScreenPoint project(
    const formats::objects3d::FixedVector3& local,
    const formats::objects3d::FixedVector3& world,
    const Camera& camera
) noexcept;
/// Tests whether a point lies strictly inside every edge of a clockwise screen polygon.
///
/// @param polygon corners in clockwise screen order; fewer than three contain nothing
/// @param point screen pixel to test
/// @return true when the point is strictly inside
/// @quirk The edge products wrap at 32 bits and are compared, not subtracted.
[[nodiscard]] bool
inside_clockwise(std::span<const ScreenPoint> polygon, ScreenPoint point) noexcept;
/// Tests whether the pointer is over a unit's root-object box.
///
/// The four min-Y corners of the root object's box (formats::objects3d::object_bounds) are
/// rotated by the unit's angles, projected, and tested as a clockwise polygon.
///
/// Throws std::invalid_argument for a unit without a root model.
///
/// @param unit unit with its model, position and rotation
/// @param camera camera position, whole pixels
/// @param point pointer position, screen pixels
/// @return true when the pointer is inside the projected box base
[[nodiscard]] bool hits_root_bounds(const PickUnit& unit, const Camera& camera, ScreenPoint point);
/// Lists the units whose root-object box is under the pointer.
///
/// Throws std::invalid_argument for a unit without a root model.
///
/// @param units candidate units in pick order
/// @param camera camera position, whole pixels
/// @param point pointer position, screen pixels
/// @return ids of the units hit, in the order given
[[nodiscard]] std::vector<uint16_t>
hit_candidates(std::span<const PickUnit> units, const Camera& camera, ScreenPoint point);
/// Finds the terrain point seen at a screen-plane position.
///
/// Takes the map column and searches ground rows from eight cell rows beyond the
/// point towards it for the first whose projection (row less half the surface height,
/// never below sea level) reaches `projected_z`, then interpolates between that row and
/// the next. The inputs are the camera-adjusted pointer position, or a campaign
/// MoveUnitToRadius point.
///
/// Throws std::invalid_argument for a non-positive map size and std::domain_error when
/// the interpolation divisor is zero.
///
/// @param terrain height and sea level queries
/// @param map_x camera-adjusted map column, whole units; clamped to the map
/// @param projected_z camera-adjusted screen-plane row, whole units; clamped to the map
/// @param map_width map width, whole units
/// @param map_height map height, whole units
/// @return the terrain point, 16.16 world coordinates
/// @quirk The interpolation numerator wraps at 32 bits, as the game computes it.
[[nodiscard]] formats::objects3d::FixedVector3 terrain_intersection(
    const sim::unit_movement::Terrain& terrain,
    int32_t map_x,
    int32_t projected_z,
    int32_t map_width,
    int32_t map_height
);

// Command bytes that never take the unit under the pointer as their target. 5 is
// UNLOAD. 10 is the command that resolves to STOP. 0x0e is the build cursor a
// structure button on the build panel arms.
inline constexpr uint8_t unload_command = 5;
inline constexpr uint8_t stop_command = 10;
inline constexpr uint8_t build_command = 0x0e;

/// Tests whether an armed command takes the unit under the pointer (Game.cursor_unit_id) as its target.
///
/// @param command armed command byte
/// @return false only for unload, stop and build
[[nodiscard]] bool command_binds_cursor_unit(uint8_t command) noexcept;

} // namespace oa::sim::gameplay_input
