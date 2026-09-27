// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The marker of the Runtime layout oa-game is built with (runtime.hpp): with
// the members an extension adds through OA_RUNTIME_EXTENSION_MEMBERS, or
// without them. Compiled only into oa-game, as its own sources are.
#include "oa/app/runtime.hpp"

namespace oa::app {

const uint8_t OA_RUNTIME_LAYOUT_MARKER = 0;

} // namespace oa::app
