// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Campaign/map file services over the mounted asset store.
#pragma once

#include "oa/data/campaign/campaign_file.hpp"

namespace oa {
class AssetStore;
}

namespace oa::data::campaign {

/// Returns file services reading through the mounted asset store.
///
/// Directory listings enumerate in find order (loose files, then each archive) with
/// stored capitalisation.
///
/// @param assets mounted asset store; must outlive the services
/// @return the services
[[nodiscard]] CampaignFiles campaign_asset_files(const oa::AssetStore& assets) noexcept;

} // namespace oa::data::campaign
