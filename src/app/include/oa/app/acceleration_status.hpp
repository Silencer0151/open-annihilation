// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// What the settings dialog says of the renderer: the Hardware acceleration
// row's status and whether nothing in the game could help the run, and
// whether Vertical sync is out of reach, from what the run knows of its
// renderer, its machine and the match. The graphics card scales the frames
// only with a renderer able to and 2 GiB of memory; every other run draws as
// the game always has.
#pragma once

#include "oa/ui/engine_settings/dialog.hpp"

#include <cstdint>
#include <optional>

namespace oa::app {

/// What the run knows of whether the graphics card could scale its frames:
/// the facts the Hardware acceleration row's status and the renderer's
/// locks follow from.
struct AccelerationFacts {
    /// Acceleration is asked for: either flag decides, else the Hardware
    /// acceleration setting in effect (hardware_acceleration_asked).
    bool asked{};
    /// --hardware-acceleration (true) or --no-hardware-acceleration (false);
    /// empty for neither.
    std::optional<bool> flag{};
    /// --force-capable: the renderer counts as able, and neither the
    /// environment's driver nor SDL's software renderer locks a row.
    bool force_capable{};
    /// SDL_RENDER_DRIVER names a render driver, or the video driver draws no
    /// window (dummy or offscreen).
    bool environment_driver{};
    uint64_t physical_memory{}; ///< bytes; 0 when the system does not say
    /// Whether the renderer was found able to scale and compose the frames;
    /// empty until something has looked at it.
    std::optional<bool> renderer_capable{};
    /// SDL's own software renderer presents the frames: never able, whatever
    /// else is known.
    bool software_renderer{};
    /// Each change of the wait for the display resets the renderer's device,
    /// and the game does not yet recover a device such a reset leaves lost.
    bool vertical_sync_resets_device{};
    /// The renderer refused, in this run, to wait for the display.
    bool vertical_sync_refused{};
    bool shared_game{};      ///< a match played with other machines is under way
    bool replay{};           ///< a recorded game is being played back
    bool tier_accelerated{}; ///< the graphics card scales the frames now
    /// What the graphics card does on this machine while it is in use.
    oa::ui::engine_settings::AccelerationReach reach{
        oa::ui::engine_settings::AccelerationReach::menus
    };
};

/// The Hardware acceleration row's status and the renderer's locks, as the
/// settings dialog shows them.
struct AccelerationReport {
    oa::ui::engine_settings::AccelerationStatus status{}; ///< the row's two status lines
    /// Nothing in the game could have the graphics card scale this run's
    /// frames (GameState::acceleration_unavailable).
    bool acceleration_unavailable{};
    /// The renderer cannot wait for the display
    /// (GameState::vertical_sync_unavailable).
    bool vertical_sync_unavailable{};

    friend bool operator==(const AccelerationReport&, const AccelerationReport&) = default;
};

/// Tells whether a machine has 2 GiB, the memory the graphics card's
/// scaling needs: the render policy's threshold,
/// render_policy::smallest_accelerated_memory, 1.75 GiB as the system
/// reports it, so that a machine sold with 2 GB counts. Memory the system
/// does not report counts as less.
///
/// @param physical_memory bytes; 0 when the system does not say
/// @return true at render_policy::smallest_accelerated_memory or more
[[nodiscard]] bool enough_memory_for_acceleration(uint64_t physical_memory) noexcept;

/// Reports what the settings dialog says of the renderer.
///
/// The status is the first that applies: under 2 GiB, whatever the setting
/// or the flags; Off by --no-hardware-acceleration; Off by the setting; then,
/// with acceleration asked for, an environment that names a driver (unless
/// --hardware-acceleration or --force-capable); a shared game or a replay,
/// where a renderer not found unable waits for the match to end; a renderer
/// found unable, SDL's software renderer among them (unless --force-capable);
/// in use while the graphics card scales the frames; and otherwise from the
/// next start, which a renderer not yet looked at shows. Acceleration is out
/// of reach under 2 GiB, on the environment's driver (unless
/// --hardware-acceleration or --force-capable), or on a renderer found
/// unable (unless --force-capable); a renderer not yet looked at leaves it
/// within reach. Vertical sync is out of reach on SDL's software renderer
/// (unless --force-capable), on a renderer whose device each change resets,
/// and once the renderer refused it.
///
/// @param facts what the run knows
/// @return the status and the locks' facts
[[nodiscard]] AccelerationReport report_acceleration(const AccelerationFacts& facts) noexcept;

} // namespace oa::app
