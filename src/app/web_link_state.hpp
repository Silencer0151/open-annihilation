// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The web links of a run (Runtime::WebLinkState): the hooks that open web
// addresses, the browser in a watched run, else a record of the requests,
// which the notice check reads. runtime_notices.cpp chooses and uses them.
#pragma once

#include "oa/app/runtime.hpp"
#include "oa/app/web_link.hpp"

#include <string>
#include <vector>

namespace oa::app {

struct Runtime::WebLinkState {
    WebLinkHooks hooks{};
    // The addresses asked for while the hooks keep a record; the hooks
    // point at it, so it stays where the state is.
    std::vector<std::string> requests;
};

} // namespace oa::app
