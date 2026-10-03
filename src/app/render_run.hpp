// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The state a runtime keeps of the renderer it borrows. Each runtime keeps
// its own, never the process: a loopback check's second runtime, which has
// no window, has none.
#pragma once

#include "render_host.hpp"
#include "oa/app/memory_guard.hpp"
#include "oa/app/render_policy.hpp"
#include "oa/app/renderer_records.hpp"
#include "oa/app/runtime.hpp"
#include "oa/platform/memory_status.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace oa::app {

/// What watches the accelerated tier while it runs: the memory guard and
/// the step-down, with the measures of the frames that feed it. Made when
/// the tier first switches on in a run, so that a run that stays on the
/// standard tier holds none of it.
struct AcceleratedWatch {
    render_policy::MemoryGuard memory{};      ///< sampled about once a second
    render_policy::ScaleStepDown step_down{}; ///< fed the steady match frames
    /// The ladder has moved in the run, by slow frames or by a buffer the
    /// memory guard refused: each later switch-on draws at its rung, which
    /// never rises again until Hardware acceleration is switched Off then On
    /// or Restore defaults asks for a fresh try. The game reads and writes
    /// the renderer records but not their scale-level key, which is
    /// reserved for the rung reached, so the rung lasts for this run alone.
    bool moved{};
    /// The step-down has lowered the rung for slow frames since the ladder
    /// last started from the top, so the status says the tier smooths less;
    /// a rung below a buffer the memory guard refused is about memory, and
    /// does not count.
    bool slowed{};
    uint32_t steps{};             ///< steps down the ladder in the run, each logged once
    uint64_t frame_interval_ns{}; ///< between the last two presents
    /// Between the two presents before them: a long interval after another
    /// as long is a crawl, not a wait.
    uint64_t previous_interval_ns{};
    uint64_t frame_ticks_ns{}; ///< what the ticks between them took
    uint64_t frame_draw_ns{};  ///< the draw measure between them
    int64_t ticks_seen_ns{};   ///< the run's ticks' time at the last present
    int64_t draw_seen_ns{};    ///< the run's draw measures at the last present
    /// The clock of the frames --check-renderer-ladder forces: the sum of
    /// their forced intervals.
    uint64_t forced_clock_ns{};
};

/// The renderer a runtime borrows, with what made it, and how it has fared
/// while the game runs.
struct Runtime::RenderRun {
    /// What made the renderer and keeps it: HostDisplay's, which outlives
    /// the runtime.
    RendererHost* host{};
    /// Why the renderer is to be made again; empty for no rebuild. A
    /// rebuild waits for the next frame's render(), never a hook or a
    /// drain of events.
    std::string pending_rebuild{};
    /// The failure the pending rebuild strikes against the driver: a
    /// present error, a lost device or repeated resets; none strikes
    /// nothing.
    renderer_state::Strike pending_failure{};
    /// The accelerated paths the frame being drawn uses, for the stage of a
    /// path's first frames (RendererHost::note_presented_frame).
    PathSet paths_drawn{};
    /// An accelerated path was refused because its trial could not be
    /// written; the AccelerationError that follows drops the tier for it.
    bool path_refused{};
    /// The main menu's notices the run noted rather than showed, as a run
    /// nobody watches does, in the order noted.
    std::vector<renderer_state::PendingNotice> notices_noted{};
    uint32_t notices_shown{}; ///< the main menu's notices shown
    /// Frames in a row the main menu, its own and not a screen package's,
    /// has been drawn.
    uint32_t main_menu_frames{};
    /// The device is lost until the system resets it: nothing that fails
    /// meanwhile is the driver's fault, and nothing is read back.
    bool device_lost{};
    render_policy::ResetWatch resets{};  ///< the device resets of the last minute
    render_policy::StallWatch stalls{};  ///< the present stalls of steady frames
    uint64_t steady_ns{};                ///< the steady frames' clock: the sum of their intervals
    uint64_t last_present_ns{};          ///< when the last frame was presented; 0 for none yet
    uint64_t unsteady_until_ns{};        ///< frames are not steady until then
    uint64_t screen_since_ns{};          ///< when the screen shown last changed
    std::optional<Screen> last_screen{}; ///< the screen of the last frame drawn
    uint64_t presented{};                ///< frames presented in the run
    uint64_t presented_since_rebuild{};  ///< frames presented on this renderer
    bool present_false_logged{};         ///< a refused present has been logged
    uint32_t rebuilds{};                 ///< renderers made again in the run
    std::optional<Screen> rebuilt_on{};  ///< the screen the last rebuild came on
    uint32_t resets_handled{};           ///< device resets whose textures were made again
    std::optional<Screen> reset_on{};    ///< the screen the last device reset was taken on
    uint32_t stall_logs{};               ///< times the stall rule logged
    /// The tier the last frame was decided to be drawn in, and why
    /// (update_render_tier).
    render_policy::TierDecision tier{};
    /// The rung --check-render-tiers switches the accelerated presentation
    /// on at; unset for the rung the machine starts at.
    std::optional<render_policy::LadderState> rung{};
    /// The failure --check-renderer-ladder forces next; unset for none.
    std::optional<RenderFaultPoint> fault{};
    /// The presented frame of the run the failure comes at.
    uint64_t fault_frame{};
    /// The accelerated tier's watch; null until the tier first switches on.
    std::unique_ptr<AcceleratedWatch> watch{};
    /// The interval --check-renderer-ladder forces on each presented frame,
    /// in nanoseconds, which the step-down then takes as a frame the loop
    /// paced, on the forced frames' own clock; unset for the frames' own.
    std::optional<uint64_t> forced_frame_ns{};
    /// The system's memory as --check-renderer-ladder forces the memory
    /// guard to see it, in place of the system's own sample; unset for the
    /// system's.
    std::optional<oa::platform::SystemMemorySample> forced_memory{};
};

} // namespace oa::app
