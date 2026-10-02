// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The state a runtime keeps of the renderer it borrows. Each runtime keeps
// its own, never the process: a loopback check's second runtime, which has
// no window, has none.
#pragma once

#include "oa/app/render_policy.hpp"
#include "oa/app/runtime.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace oa::app {

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
    /// The failure --check-renderer-ladder forces next; unset for none.
    std::optional<RenderFaultPoint> fault{};
    /// The presented frame of the run the failure comes at.
    uint64_t fault_frame{};
};

} // namespace oa::app
