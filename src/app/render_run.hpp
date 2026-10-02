// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The state a runtime keeps of the renderer it borrows. Each runtime keeps
// its own, never the process: a loopback check's second runtime, which has
// no window, has none.
#pragma once

#include "oa/app/runtime.hpp"

namespace oa::app {

/// The renderer a runtime borrows, with what made it.
struct Runtime::RenderRun {
    /// What made the renderer and keeps it: HostDisplay's, which outlives
    /// the runtime.
    RendererHost* host{};
};

} // namespace oa::app
