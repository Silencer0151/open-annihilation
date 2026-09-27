// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/data/campaign/campaign_assets.hpp"

#include "oa/formats/hpi.hpp"

#include <algorithm>
#include <span>
#include <string>

namespace oa::data::campaign {
namespace {

const oa::AssetStore& store(void* context) {
    return *static_cast<const oa::AssetStore*>(context);
}

int32_t asset_size(void* context, const char* path) {
    auto* file = store(context).open(path);
    if (file == nullptr)
        return -1;
    oa::AssetStore::close(file);
    return static_cast<int32_t>(store(context).file_size(path));
}

int32_t asset_read(void* context, const char* path, char* buffer, uint32_t capacity) {
    const auto size = std::min(store(context).file_size(path), capacity);
    if (size == 0 ||
        !store(context).read_chunk(path, 0, std::span(reinterpret_cast<uint8_t*>(buffer), size)))
        return -1;
    return static_cast<int32_t>(size);
}

void asset_list(
    void* context,
    const char* directory,
    const char* extension,
    void (*visit)(void*, const char*),
    void* visit_context
) {
    const std::string pattern = std::string(directory) + "\\*." + extension;
    for (const auto& entry : store(context).find(pattern))
        if (!entry.directory)
            visit(visit_context, entry.name.c_str());
}

int32_t asset_count(void* context, const char* pattern) {
    return store(context).count_entries(pattern, false);
}

// Archive and loose entries carry no write time.
void asset_find(
    void* context, const char* pattern, void (*visit)(void*, const FindRecord&), void* user
) {
    for (const auto& entry : store(context).find(pattern))
        visit(user, {entry.directory ? kFindDirectory : 0U, 0, entry.size, entry.name.c_str()});
}

} // namespace

CampaignFiles campaign_asset_files(const oa::AssetStore& assets) noexcept {
    return {
        const_cast<oa::AssetStore*>(&assets),
        asset_size,
        asset_read,
        asset_list,
        nullptr,
        nullptr,
        nullptr,
        asset_count,
        asset_find
    };
}

} // namespace oa::data::campaign
