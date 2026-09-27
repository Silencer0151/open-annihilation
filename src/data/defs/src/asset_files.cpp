// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/data/defs/asset_files.hpp"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <string>

namespace oa::data::defs {
namespace {

std::string host_path(const char* path) {
    std::string out(path);
    for (char& c : out)
        if (c == '\\')
            c = '/';
    return out;
}

const AssetStore* store_of(void* context) {
    return static_cast<const AssetStore*>(context);
}

bool read(void* context, const char* path, uint8_t** data, uint32_t* size, bool* from_archive) {
    try {
        AssetData asset = store_of(context)->read(host_path(path));
        if (asset.bytes.size() > formats::tdf::max_input_bytes)
            return false;
        auto* copy =
            static_cast<uint8_t*>(std::malloc(asset.bytes.empty() ? 1 : asset.bytes.size()));
        if (copy == nullptr)
            return false;
        if (!asset.bytes.empty())
            std::memcpy(copy, asset.bytes.data(), asset.bytes.size());
        *data = copy;
        *size = static_cast<uint32_t>(asset.bytes.size());
        *from_archive = asset.archived;
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

void release(void*, uint8_t* data) {
    std::free(data);
}

bool exists(void* context, const char* path) {
    try {
        (void)store_of(context)->read(host_path(path));
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

void list(
    void* context,
    const char* directory,
    const char* extension,
    void (*visit)(void* user, const char* name),
    void* user
) {
    try {
        const auto names = store_of(context)->list_effective_in_mount_order(
            host_path(directory), std::string(".") + extension
        );
        for (const auto& name : names) {
            const auto slash = name.rfind('/');
            visit(user, name.c_str() + (slash == std::string::npos ? 0 : slash + 1));
        }
    } catch (const std::exception&) {
    }
}

} // namespace

Files asset_store_files(const AssetStore* store) noexcept {
    return Files{const_cast<AssetStore*>(store), read, release, exists, list};
}

} // namespace oa::data::defs
