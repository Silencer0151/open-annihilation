// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The Revision.GPF version check against GAMEDATA\VERSION.TDF.
#pragma once

#include "oa/data/defs/files.hpp"

namespace oa::data::defs {

// The GPFVersion the game expects.
inline constexpr const char* expected_gpf_version = "v3.0";

/// Tests GAMEDATA/VERSION.TDF against expected_gpf_version.
///
/// The game warns the player on a mismatch.
///
/// @param files file boundary
/// @param variant game-data variant suffix; null or empty for none
/// @return true when the file loads and its [Version] GPFVersion is missing or
///     differs case-insensitively; a missing file is not a mismatch
[[nodiscard]] bool revision_gpf_mismatch(const Files* files, const char* variant) noexcept;

} // namespace oa::data::defs
