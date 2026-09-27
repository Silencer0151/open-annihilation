// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The definition loaders' file boundary over the runtime's asset store.
#pragma once

#include "oa/formats/hpi.hpp"
#include "oa/data/defs/files.hpp"

namespace oa::app {

/// Wraps an asset store as the definition loaders' file interface.
///
/// The files are the store's merged view of loose files and archives.
///
/// @param assets store to read through; must outlive the returned value
/// @return callbacks that read, release, test and list the files of `assets`
[[nodiscard]] oa::data::defs::Files asset_files(const oa::AssetStore& assets) noexcept;

} // namespace oa::app
