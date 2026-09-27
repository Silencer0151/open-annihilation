// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/ui/frontend/resource_palette.hpp"

#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace oa::ui::frontend {
namespace {

// The app mode whose loads stay out of the cache.
constexpr int32_t kMatchMode = 6;
constexpr std::size_t kPathBytes = 0x100;

/// Copies a background name, cut to fit its buffer with the terminator.
///
/// @param[out] out a slot's name or Game.background_name
/// @param capacity bytes of out
/// @param name NUL-terminated name
void copy_name(char* out, std::size_t capacity, const char* name) noexcept {
    std::strncpy(out, name, capacity - 1);
    out[capacity - 1] = '\0';
}

} // namespace

void resource_cache_init(ResourceCache* cache) noexcept {
    std::memset(cache, 0, sizeof *cache);
}

void resource_cache_free(ResourceCache* cache, const ResourceHost& host) noexcept {
    for (auto& slot : cache->slots) {
        if (slot.surface != 0 && host.free_bitmap != nullptr)
            host.free_bitmap(host.context, slot.surface);
        std::free(slot.palette);
    }
    resource_cache_init(cache);
}

oa_ref32 load_named_bitmap(const ResourceHost& host, const char* name, uint8_t* palette) {
    char path[kPathBytes];
    data::campaign::build_variant_path(
        host.files, path, sizeof path, kResourceBitmapDirectory, name, kResourceBitmapExtension
    );
    const oa_ref32 bitmap =
        host.load_bitmap != nullptr ? host.load_bitmap(host.context, path, palette) : 0;
    if (bitmap == 0 && host.fatal != nullptr)
        host.fatal(host.context, path);
    return bitmap;
}

int32_t load_resource_palette(
    ResourceCache* cache,
    Game* game,
    const ResourceHost& host,
    const char* name,
    bool redraw,
    bool apply,
    bool defer
) {
    uint8_t* palette = nullptr;
    oa_ref32 surface = 0;
    if (redraw && host.draw_current_frame != nullptr)
        host.draw_current_frame(host.context);
    if (name != nullptr) {
        int32_t found = -1;
        for (int32_t index = 0; index < kResourceCacheSlots; ++index)
            if (std::strcmp(cache->slots[index].name, name) == 0) {
                found = index;
                break;
            }
        if (found >= 0) {
            const ResourceSlot hit = cache->slots[found];
            surface = hit.surface;
            palette = hit.palette;
            for (int32_t index = found; index > 0; --index)
                cache->slots[index] = cache->slots[index - 1];
            cache->slots[0] = hit;
        }
        if (surface == 0) {
            palette = static_cast<uint8_t*>(std::calloc(1, kResourcePaletteBytes));
            surface = load_named_bitmap(host, name, palette);
            if (game->mode != kMatchMode) {
                ResourceSlot& last = cache->slots[kResourceCacheSlots - 1];
                if (last.surface != 0) {
                    if (host.free_bitmap != nullptr)
                        host.free_bitmap(host.context, last.surface);
                    std::free(last.palette);
                }
                for (int32_t index = kResourceCacheSlots - 1; index > 0; --index)
                    cache->slots[index] = cache->slots[index - 1];
                cache->slots[0].surface = surface;
                cache->slots[0].palette = palette;
                copy_name(cache->slots[0].name, kResourceNameBytes, name);
            }
        }
    }
    if (defer)
        return 0;
    if (host.panel_open == nullptr || !host.panel_open(host.context)) {
        game->pending_background = surface;
        if (name != nullptr)
            copy_name(game->background_name, sizeof game->background_name, name);
    } else {
        if (host.set_backdrop != nullptr)
            host.set_backdrop(host.context, surface);
        if (apply && palette != nullptr && host.apply_palette != nullptr)
            host.apply_palette(host.context, palette);
    }
    if (surface == 0 && name != nullptr)
        return 0;
    if (name != nullptr)
        copy_name(game->background_name, sizeof game->background_name, name);
    else
        game->background_name[0] = '\0';
    return 1;
}

} // namespace oa::ui::frontend
