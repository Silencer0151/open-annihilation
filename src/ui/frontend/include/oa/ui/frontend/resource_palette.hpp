// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Named frontend backgrounds: bitmaps\<name>.pcx with its palette, kept in a
// small cache of the most recently asked for, and handed to the panel on top
// or held for the next one.
#pragma once

#include "oa/data/campaign/campaign_file.hpp"
#include "oa/core/game_state.h"

#include <cstddef>
#include <cstdint>

namespace oa::ui::frontend {

inline constexpr int32_t kResourceCacheSlots = 10;
inline constexpr std::size_t kResourceNameBytes = 0x20;
inline constexpr std::size_t kResourcePaletteBytes = 0x400; // 256 RGB0 entries
inline constexpr const char* kResourceBitmapDirectory = "bitmaps";
inline constexpr const char* kResourceBitmapExtension = "PCX";

// One cache slot.
struct ResourceSlot {
    oa_ref32 surface;              // host bitmap handle, 0 for an empty slot
    uint8_t* palette;              // kResourcePaletteBytes block the slot owns
    char name[kResourceNameBytes]; // background name
};

// Slot 0 is the most recently asked for.
struct ResourceCache {
    ResourceSlot slots[kResourceCacheSlots];
};

struct ResourceHost {
    void* context;
    // Clears and presents the frame before the load; optional.
    void (*draw_current_frame)(void* context);
    // Loads a PCX as a bitmap handle, writing its palette to `palette`; 0 when
    // it cannot be read.
    oa_ref32 (*load_bitmap)(void* context, const char* path, uint8_t* palette);
    void (*free_bitmap)(void* context, oa_ref32 bitmap);
    // The load of a named bitmap failed; does not return.
    void (*fatal)(void* context, const char* path);
    // Whether the GUI has a panel open on top.
    bool (*panel_open)(void* context);
    // Hands the bitmap to the panel on top as its backdrop.
    void (*set_backdrop)(void* context, oa_ref32 bitmap);
    // Makes the palette block the display palette (entries 0..255).
    void (*apply_palette)(void* context, const uint8_t* palette);
    // Language variant lookup of the bitmap path.
    const data::campaign::CampaignFiles* files;
};

/// Empties the cache without freeing anything.
///
/// @param[out] cache Cache whose slots are zeroed.
void resource_cache_init(ResourceCache* cache) noexcept;

/// Frees every slot's bitmap and palette and empties the cache.
///
/// @param[in,out] cache Cache to release.
/// @param host Frees the bitmaps.
void resource_cache_free(ResourceCache* cache, const ResourceHost& host) noexcept;

/// Loads bitmaps\<name>.pcx (its language variant when there is one) into a new bitmap.
///
/// @param host Loads the file and reports a failure.
/// @param name Background name without directory or extension.
/// @param[out] palette kResourcePaletteBytes block receiving the file's palette.
/// @return The bitmap handle; a file that cannot be read is fatal (host.fatal).
oa_ref32 load_named_bitmap(const ResourceHost& host, const char* name, uint8_t* palette);

/// Selects a named frontend background, from the cache or freshly loaded.
///
/// A cache hit moves to the front slot; otherwise the bitmap is loaded and,
/// outside a match (app mode 6), replaces the front slot after the others
/// move back and the last is freed. Unless deferred, the bitmap becomes the
/// top panel's backdrop (its palette applied when asked) or, with no panel
/// open, Game.pending_background; Game.background_name records the name.
///
/// @param[in,out] cache Background cache.
/// @param[in,out] game Game block: mode is read; pending_background and
///                     background_name are written.
/// @param host Loading, backdrop and palette services.
/// @param name Background name; null selects no background.
/// @param redraw Whether to clear and present the frame before loading.
/// @param apply Whether to make the palette the display palette when the bitmap becomes a backdrop.
/// @param defer Whether to only load and cache, leaving the backdrop alone.
/// @return 1 when a bitmap or a null name was set, 0 when deferred or nothing loaded.
/// @quirk A hit on a slot without a bitmap loads again and caches the load a
///        second time; a load during a match is not cached and never freed.
int32_t load_resource_palette(
    ResourceCache* cache,
    Game* game,
    const ResourceHost& host,
    const char* name,
    bool redraw,
    bool apply,
    bool defer
);

} // namespace oa::ui::frontend
