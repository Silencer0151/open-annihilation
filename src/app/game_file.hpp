// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The read read_game_file performs on the runtime's store. Not part of the
// extension table: the call an extension makes is read_game_file.
#pragma once

#include "oa/formats/hpi.hpp"

#include <exception>
#include <vector>

namespace oa::app {

/// Reads one whole file from `assets`, as read_game_file does.
///
/// A loose file wins over an archive. A missing file returns false, and
/// this does not throw.
///
/// @param assets the game's files
/// @param path the file, with '\\' or '/' between its parts; null is refused
/// @param[out] bytes the file's bytes; empty when the file is missing
/// @return true when the file was read
inline bool
read_stored_game_file(const oa::AssetStore& assets, const char* path, std::vector<uint8_t>& bytes) {
    bytes.clear();
    if (path == nullptr || path[0] == '\0')
        return false;
    try {
        bytes = assets.read(path).bytes;
        return true;
    } catch (const std::exception&) {
        bytes.clear();
        return false;
    }
}

} // namespace oa::app
