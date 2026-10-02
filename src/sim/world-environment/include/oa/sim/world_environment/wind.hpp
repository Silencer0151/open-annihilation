// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "oa/core/game_state.h"
#include "oa/core/unit.h"
#include "oa/core/unit_def.h"

#include <cstddef>
#include <cstdint>

namespace oa::sim::world_environment {

// Wind scheduler constants. A new sample comes after
// lcg * cadence_multiplier / lcg_divisor + cadence_bias steps of
// cadence_tick_scale ticks, where lcg is a rand() draw below lcg_divisor. The
// normalized strength is compared with its limit as a double.
inline constexpr int32_t default_wind_strength_divisor = 0x1388; // 5000
inline constexpr uint32_t initial_change_deadline = 0;
inline constexpr uint32_t cadence_multiplier = 10;
inline constexpr uint32_t lcg_divisor = 0x8000;
inline constexpr uint32_t cadence_bias = 5;
inline constexpr uint32_t cadence_tick_scale = 0x1e;
inline constexpr uint32_t direction_span = 0x10000;
inline constexpr unsigned negated_vector_shift = 1; // the vector is negated, then doubled
inline constexpr double normalized_strength_limit = 1.0;
inline constexpr uint32_t wind_sample_changed = 1;   // Game.wind_changed after a new sample
inline constexpr uint32_t wind_sample_unchanged = 0; // Game.wind_changed cleared while waiting

// Sea occupy codes. Layers are the unit's occupancy bits
// (OA_UNIT_FLAG_OCCUPANCY_MASK: 1 ground, 2 air); a unit less than 5 below sea
// level is at the surface.
inline constexpr uint32_t sea_occupy_ground_layer = 1;
inline constexpr uint32_t sea_occupy_air_layer = 2;
inline constexpr int32_t sea_surface_depth_limit = -5;
inline constexpr int32_t sea_occupy_none = 0;
inline constexpr int32_t sea_occupy_surface = 1;
inline constexpr int32_t sea_occupy_waterline = 2;
inline constexpr int32_t sea_occupy_submerged = 3;
inline constexpr int32_t sea_occupy_above = 4;

// Game.wind_vector. The scheduler writes x and z only.
struct WindVector {
    int32_t vector_x{};
    int32_t vector_y{};
    int32_t vector_z{};
};

static_assert(offsetof(WindVector, vector_x) == 0);
static_assert(offsetof(WindVector, vector_y) == 4);
static_assert(offsetof(WindVector, vector_z) == 8);

// Wind fields of the live Game block. Callers supply map limits and the
// simulation tick before invoking the scheduler. The vector base lays x/y/z
// out as Game.wind_vector does; the other fields are not one contiguous image
// of that block.
struct WindState : WindVector {
    uint32_t change_deadline{};  // Game.wind_change_tick
    int32_t strength_divisor{};  // Game.wind_strength_divisor
    uint16_t direction{};        // Game.wind_direction
    int32_t strength{};          // Game.wind_strength
    float normalized_strength{}; // Game.wind_factor
    uint32_t changed{};          // Game.wind_changed
    int32_t minimum_strength{};  // Game.wind_min
    int32_t maximum_strength{};  // Game.wind_max
    uint32_t current_tick{};     // Game.tick
};

/// How a run of the wind scheduler ended.
enum class WindRefresh : uint8_t {
    waiting,               ///< the deadline has not passed; the changed flag is cleared
    changed,               ///< a new sample was drawn
    random_out_of_range,   ///< the rand() stream gave a value above 32767; nothing changed
    zero_strength_divisor, ///< the strength divisor is zero; the sample is drawn, the normalized
                           ///< strength and the changed flag are left as they were
};

class WindRandomHost {
  public:

    virtual ~WindRandomHost() = default;

    /// Draws from the rand() stream, which the scheduler uses for cadence only.
    ///
    /// @return a value in 0..32767
    virtual uint32_t lcg_rand_15() = 0;

    /// Draws from the shared simulation stream, which wind strength and direction consume.
    ///
    /// Keeping this callback separate preserves the multiplayer draw order.
    ///
    /// @param exclusive_limit exclusive upper bound
    /// @return a value in 0..exclusive_limit-1
    virtual uint32_t shared_random(uint32_t exclusive_limit) = 0;
};

/// Runs the wind scheduler for one tick.
///
/// Once the tick passes the change deadline, the deadline advances by
/// (rand * 10 / 32768 + 5) * 30 ticks, strength is drawn between the map limits
/// and, when nonzero, a new direction; the vector is the negated, doubled
/// sine/cosine of direction scaled by strength, and the normalized strength is
/// strength / divisor capped at 1. Otherwise the changed flag is cleared.
///
/// @param[in,out] state wind fields, map limits and current tick
/// @param random rand() and shared random streams
/// @return changed exactly when a new wind sample was emitted, waiting before the deadline,
///         or the error that stopped the run
/// @quirk The deadline test is unsigned and waits on equality; a calm (zero) strength keeps the previous direction.
WindRefresh refresh_wind(WindState& state, WindRandomHost& random);

/// Sets up the wind at game start and draws the first sample.
///
/// Establishes the strength divisor and first deadline, then runs the same
/// scheduler as live ticks.
///
/// @param[in,out] state wind fields, map limits and current tick
/// @param random rand() and shared random streams
/// @return how the first run of the scheduler ended
WindRefresh initialize_wind(WindState& state, WindRandomHost& random);

/// Copies the canonical Game wind fields into a scheduler state.
///
/// @param game game block holding wind_min/max, wind_change_tick .. wind_changed and tick
/// @return the scheduler state
[[nodiscard]] WindState wind_state(const Game& game) noexcept;
/// Writes a scheduler state back to the canonical Game wind fields.
///
/// The map limits and tick are not written.
///
/// @param[out] game game block to update
/// @param state scheduler state
void store_wind_state(Game& game, const WindState& state) noexcept;
/// Runs the wind scheduler for one tick over the canonical Game wind fields.
///
/// A run that ends in an error leaves the Game fields as they were.
///
/// @param[in,out] game game block holding the wind fields and tick
/// @param random rand() and shared random streams
/// @return as refresh_wind over a scheduler state
WindRefresh refresh_wind(Game& game, WindRandomHost& random);
/// Sets up the canonical Game wind fields at game start and draws the first sample.
///
/// A run that ends in an error leaves the Game fields as they were.
///
/// @param[in,out] game game block holding the wind fields and tick
/// @param random rand() and shared random streams
/// @return how the first run of the scheduler ended
WindRefresh initialize_wind(Game& game, WindRandomHost& random);

/// Script hook of the sea occupy update.
struct SeaOccupyHost {
    void* context{};
    /// Runs the unit script's setSFXoccupy with the new code; null runs
    /// nothing, and the code is still kept.
    void (*set_sfx_occupy)(void* context, Unit& unit, int32_t occupy_code){};
};

/// Returns the last occupy code sent to the unit's script (Unit.last_occupy_code).
///
/// @param unit unit to read
/// @return one of the sea_occupy_* codes
[[nodiscard]] int32_t sea_occupy(const Unit& unit) noexcept;

/// Updates the unit's sea occupy code and tells its script when it changes.
///
/// A ground or air unit above sea level is above (4); otherwise the checks run
/// in order and the last match wins: less than 5 below the sea is the surface
/// (1), the waterline at sea level is the waterline (2), and a model top below
/// the sea is submerged (3). Any other layer is none (0).
///
/// @param[in,out] unit unit whose code (Unit.last_occupy_code) is updated
/// @param def the unit's type, for its waterline and model height
/// @param sea_level map sea level in whole world units
/// @param host script hook; called only when the code changes
/// @quirk When no sea band matches the previous code is kept.
void update_sea_occupy(
    Unit& unit, const UnitDef& def, uint8_t sea_level, const SeaOccupyHost& host
);

} // namespace oa::sim::world_environment
