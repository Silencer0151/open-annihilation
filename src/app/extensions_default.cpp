// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The extension oa-game links when nothing else extends it: every hook null,
// so the game runs without multiplayer.
#include "oa/app/extension.hpp"

void oa_extensions_init(oa::app::Extension* table) {
    *table = {};
}
