// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Files boundary backed by the host AssetStore (loose files, then mounted HPIs).
#pragma once

#include "oa/formats/hpi.hpp"
#include "oa/data/defs/files.hpp"

namespace oa::data::defs {

/// Returns a file boundary that reads through a host asset store.
///
/// Paths have '\\' turned into '/'; reads larger than formats::tdf::max_input_bytes
/// and store exceptions fail; listings follow the store's mount order.
///
/// @param store asset store; must outlive the returned boundary
/// @return the boundary, with `store` as its context
[[nodiscard]] Files asset_store_files(const AssetStore* store) noexcept;

} // namespace oa::data::defs
