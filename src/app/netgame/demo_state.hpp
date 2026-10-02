// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The .tad playback a Runtime keeps while it replays a recording
// (Runtime::demo_): the session runtime_demo.cpp drives, which the replay
// hooks the extension hands the engine read.
#pragma once

#include "oa/app/runtime.hpp"

#include "oa/session/demo.hpp"

namespace oa::app {

struct Runtime::DemoState : oa::session::demo::DemoSession {};

} // namespace oa::app
