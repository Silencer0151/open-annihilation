// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// One whole file from the game's files, for an extension: the runtime's
// store, loose files ahead of every archive.
#include "game_file.hpp"

#include "oa/app/extension.hpp"
#include "oa/app/runtime.hpp"

#include <vector>

namespace oa::app {

bool read_game_file(const Runtime& runtime, const char* path, std::vector<uint8_t>& bytes) {
    return read_stored_game_file(runtime.assets_, path, bytes);
}

} // namespace oa::app
