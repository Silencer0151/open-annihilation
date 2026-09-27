// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/app/asset_files.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <string>
#include <vector>

namespace oa::app {
namespace {

const oa::AssetStore& store(void* context) noexcept {
    return *static_cast<const oa::AssetStore*>(context);
}

bool read_file(
    void* context, const char* path, uint8_t** data, uint32_t* size, bool* from_archive
) {
    try {
        const auto asset = store(context).read(path);
        const std::size_t bytes = std::max<std::size_t>(asset.bytes.size(), 1);
        auto* copy = static_cast<uint8_t*>(std::malloc(bytes));
        if (copy == nullptr)
            return false;
        std::memcpy(copy, asset.bytes.data(), asset.bytes.size());
        *data = copy;
        *size = static_cast<uint32_t>(asset.bytes.size());
        *from_archive = asset.archived;
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

void release_file(void*, uint8_t* data) {
    std::free(data);
}

bool file_exists(void* context, const char* path) {
    try {
        oa::ResourceFile* file = store(context).open(path);
        if (file == nullptr)
            return false;
        oa::AssetStore::close(file);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

// The store lists '/'-separated directories and returns full resource paths.
void list_files(
    void* context,
    const char* directory,
    const char* extension,
    void (*visit)(void* user, const char* name),
    void* user
) {
    std::string folder(directory);
    std::replace(folder.begin(), folder.end(), '\\', '/');
    std::vector<std::string> paths;
    try {
        paths = store(context).list_effective_in_mount_order(folder, std::string(".") + extension);
    } catch (const std::exception&) {
        return;
    }
    for (const auto& path : paths) {
        const auto slash = path.find_last_of('/');
        visit(user, path.c_str() + (slash == std::string::npos ? 0 : slash + 1));
    }
}

} // namespace

oa::data::defs::Files asset_files(const oa::AssetStore& assets) noexcept {
    return {const_cast<oa::AssetStore*>(&assets), read_file, release_file, file_exists, list_files};
}

} // namespace oa::app
