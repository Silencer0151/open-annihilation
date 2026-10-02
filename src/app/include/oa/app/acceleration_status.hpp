// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// What the settings dialog says of the renderer: the Hardware acceleration
// row's status and whether nothing in the game could help the run, and
// whether Vertical sync is out of reach, from what the run knows of its
// renderer, its machine and the match. The graphics card scales the frames
// only with a renderer able to and 2 GiB of memory; every other run draws as
// the game always has.
#pragma once

#include "oa/app/render_policy.hpp"
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
    /// The renderer was found unable because it lacks something the
    /// graphics card's scaling needs: a texture limit under 1024, or a
    /// start-up test that drew a known pattern wrongly. Otherwise an unable
    /// renderer has no usable graphics card.
    bool lacks_feature{};
    /// The start-up function test drew a known pattern wrongly: the
    /// renderer is unable, --force-capable or not.
    bool function_test_failed{};
    /// The graphics card stopped scaling the frames for the rest of the run
    /// after a call only it makes failed, or after the renderer failed and
    /// was made again.
    bool driver_failed{};
    /// The memory guard stopped the graphics card scaling the frames for the
    /// rest of the run, which switching the setting Off then On does not
    /// lift.
    bool memory_dropped{};
    /// The step-down stopped the graphics card scaling the frames for the
    /// rest of the run: frames were slow with it at every rung.
    bool slow_frames_dropped{};
    /// The step-down lowered the graphics card's rung for slow frames in
    /// this run, short of the last; a rung lowered because the memory guard
    /// refused a buffer does not count.
    bool slow_frames_stepped{};
    /// Each change of the wait for the display resets the renderer's device,
    /// and the game does not yet recover a device such a reset leaves lost.
    bool vertical_sync_resets_device{};
    /// The renderer refused, in this run, to wait for the display.
    bool vertical_sync_refused{};
    bool shared_game{};      ///< a match played with other machines is under way
    bool replay{};           ///< a recorded game is being played back
    bool tier_accelerated{}; ///< the graphics card scales the frames now
    /// The graphics card started at the lowest budget, where nothing smooths
    /// the zoomed-out view.
    bool no_smoothing{};
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
/// where a renderer not found unable waits for the match to end; a driver
/// that failed in this run; a renderer found unable, SDL's software
/// renderer among them (unless --force-capable), as lacking a feature or
/// as no usable graphics card; in use while the graphics card scales the
/// frames, with less smoothing once the step-down has lowered its rung for
/// slow frames, else with no smoothing where it started at the lowest
/// budget; and
/// otherwise from the next start, which a renderer not yet looked at
/// shows. Acceleration is out of reach under 2 GiB, on the environment's
/// driver (unless --hardware-acceleration or --force-capable), or on a
/// renderer found unable (unless --force-capable, which never lifts a
/// failed function test) where no driver failed in this run; a renderer not
/// yet looked at, and one a failed driver left, leave it within reach.
/// Vertical sync is out of reach on SDL's software renderer (unless
/// --force-capable), on a renderer whose device each change resets, and
/// once the renderer refused it.
///
/// @param facts what the run knows
/// @return the status and the locks' facts
[[nodiscard]] AccelerationReport report_acceleration(const AccelerationFacts& facts) noexcept;

/// Returns what the facts the tier is decided from say of the renderer and
/// the run, as the settings dialog's status reads them: acceleration asked
/// for by the flag, else by the setting; the flag; --force-capable; the
/// environment's driver or a video driver that draws no window; the
/// machine's memory; the renderer, where there is one, able when probe
/// items 1 to 3 found it capable and the function test did not fail, and
/// lacking a feature for a small texture limit or a failed function test;
/// whether the function test failed;
/// SDL's software renderer; a drop after a driver failure; a shared game or
/// a replay the tier waits for; whether the graphics card scales the frames
/// now; and what it does at the rung. Vertical sync's facts and whether the
/// step-down has lowered the rung for slow frames are left for the caller.
///
/// @param inputs the facts the tier is decided from
/// @param rung the rung the accelerated tier draws at
/// @param tier_accelerated the graphics card scales the frames now
/// @return the facts
[[nodiscard]] AccelerationFacts tier_acceleration_facts(
    const render_policy::TierInputs& inputs,
    const render_policy::LadderState& rung,
    bool tier_accelerated
) noexcept;

/// Returns what the graphics card does at a rung of the step-down ladder,
/// which the status's second line says: nothing but the zoomed-out view's
/// smoothing, or nothing at all, with the chrome drawn NEAREST; everything,
/// the zoomed-out view smoothed, above the lowest budget; the interface and
/// the zoomed-in view with the card magnifying; and otherwise the menus and
/// the interface.
///
/// @param rung the rung
/// @return the reach
[[nodiscard]] oa::ui::engine_settings::AccelerationReach
acceleration_reach(const render_policy::LadderState& rung) noexcept;

} // namespace oa::app
