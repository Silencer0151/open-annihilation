// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/ui/frontend/resource_palette.hpp"
#include "test_support.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace oa::ui::frontend::test {
namespace {

struct Bitmaps {
    std::vector<std::string> loaded; // paths, in load order
    std::vector<oa_ref32> freed;
    std::vector<oa_ref32> backdrops;
    std::vector<uint8_t> applied; // first byte of each applied palette
    std::string fatal;
    std::string variant_present; // a path the variant lookup finds
    bool panel_open = true;
    bool fail = false;
    oa_ref32 next = 1;
};

int32_t variant_size(void* context, const char* path) {
    return static_cast<Bitmaps*>(context)->variant_present == path ? 10 : -1;
}

ResourceHost make_host(Bitmaps& bitmaps, data::campaign::CampaignFiles& files) {
    files = {};
    files.context = &bitmaps;
    files.size = variant_size;
    files.language = "german";
    ResourceHost host{};
    host.context = &bitmaps;
    host.load_bitmap = [](void* context, const char* path, uint8_t* palette) -> oa_ref32 {
        auto& state = *static_cast<Bitmaps*>(context);
        state.loaded.emplace_back(path);
        if (state.fail)
            return 0;
        palette[0] = static_cast<uint8_t>(state.next);
        return state.next++;
    };
    host.free_bitmap = [](void* context, oa_ref32 bitmap) {
        static_cast<Bitmaps*>(context)->freed.push_back(bitmap);
    };
    host.fatal = [](void* context, const char* path) {
        static_cast<Bitmaps*>(context)->fatal = path;
    };
    host.panel_open = [](void* context) { return static_cast<Bitmaps*>(context)->panel_open; };
    host.set_backdrop = [](void* context, oa_ref32 bitmap) {
        static_cast<Bitmaps*>(context)->backdrops.push_back(bitmap);
    };
    host.apply_palette = [](void* context, const uint8_t* palette) {
        static_cast<Bitmaps*>(context)->applied.push_back(palette[0]);
    };
    host.files = &files;
    return host;
}

std::string slot_name(const ResourceCache& cache, int32_t index) {
    return cache.slots[index].name;
}

OA_TEST(resource_palette_promotes_hits_to_the_front) {
    Bitmaps bitmaps;
    data::campaign::CampaignFiles files{};
    const auto host = make_host(bitmaps, files);
    auto game = std::make_unique<Game>();
    ResourceCache cache{};
    resource_cache_init(&cache);
    for (const char* name : {"singlebg", "skirmsetup4x", "options4x"})
        OA_CHECK(load_resource_palette(&cache, game.get(), host, name, false, false, false) == 1);
    OA_CHECK(
        slot_name(cache, 0) == "options4x" && slot_name(cache, 1) == "skirmsetup4x" &&
        slot_name(cache, 2) == "singlebg"
    );
    OA_CHECK(bitmaps.loaded.size() == 3 && bitmaps.loaded[0] == "bitmaps/singlebg.PCX");

    OA_CHECK(load_resource_palette(&cache, game.get(), host, "singlebg", false, true, false) == 1);
    OA_CHECK(bitmaps.loaded.size() == 3);
    OA_CHECK(
        slot_name(cache, 0) == "singlebg" && slot_name(cache, 1) == "options4x" &&
        slot_name(cache, 2) == "skirmsetup4x"
    );
    OA_CHECK(bitmaps.backdrops.back() == 1 && bitmaps.applied.back() == 1);
    OA_CHECK(std::string(game->background_name) == "singlebg");
    // The names compare case-sensitively.
    OA_CHECK(load_resource_palette(&cache, game.get(), host, "SINGLEBG", false, false, false) == 1);
    OA_CHECK(bitmaps.loaded.size() == 4 && slot_name(cache, 1) == "singlebg");
    resource_cache_free(&cache, host);
}

OA_TEST(resource_palette_frees_the_eleventh_back) {
    Bitmaps bitmaps;
    data::campaign::CampaignFiles files{};
    const auto host = make_host(bitmaps, files);
    auto game = std::make_unique<Game>();
    ResourceCache cache{};
    resource_cache_init(&cache);
    for (int index = 0; index < kResourceCacheSlots; ++index) {
        const std::string name = "bg" + std::to_string(index);
        (void)load_resource_palette(&cache, game.get(), host, name.c_str(), false, false, false);
    }
    OA_CHECK(bitmaps.freed.empty());
    (void)load_resource_palette(&cache, game.get(), host, "bg10", false, false, false);
    OA_CHECK(bitmaps.freed.size() == 1 && bitmaps.freed[0] == 1);
    OA_CHECK(slot_name(cache, 0) == "bg10" && slot_name(cache, 9) == "bg1");

    // In a match the load is used but not cached.
    game->mode = 6;
    (void)load_resource_palette(&cache, game.get(), host, "ingame", false, false, false);
    OA_CHECK(slot_name(cache, 0) == "bg10" && bitmaps.backdrops.back() == 12);
    resource_cache_free(&cache, host);
    OA_CHECK(bitmaps.freed.size() == 11);
}

OA_TEST(resource_palette_without_a_panel_or_a_name) {
    Bitmaps bitmaps;
    data::campaign::CampaignFiles files{};
    const auto host = make_host(bitmaps, files);
    auto game = std::make_unique<Game>();
    ResourceCache cache{};
    resource_cache_init(&cache);
    bitmaps.panel_open = false;
    OA_CHECK(
        load_resource_palette(&cache, game.get(), host, "loadgame2bg", false, true, false) == 1
    );
    OA_CHECK(game->pending_background == 1 && bitmaps.backdrops.empty() && bitmaps.applied.empty());

    bitmaps.panel_open = true;
    OA_CHECK(load_resource_palette(&cache, game.get(), host, nullptr, false, false, false) == 1);
    OA_CHECK(bitmaps.backdrops.back() == 0 && game->background_name[0] == '\0');
    // Deferred: cached, but nothing shown.
    OA_CHECK(load_resource_palette(&cache, game.get(), host, "outcome0", false, true, true) == 0);
    OA_CHECK(bitmaps.backdrops.size() == 1 && slot_name(cache, 0) == "outcome0");

    // The language directory is used when the bitmap is there; a failed load
    // is fatal with the path it tried.
    bitmaps.variant_present = "bitmaps-german/newcampaign4.PCX";
    (void)load_resource_palette(&cache, game.get(), host, "newcampaign4", false, false, false);
    OA_CHECK(bitmaps.loaded.back() == "bitmaps-german/newcampaign4.PCX");
    bitmaps.fail = true;
    OA_CHECK(load_resource_palette(&cache, game.get(), host, "missing", false, false, false) == 0);
    OA_CHECK(bitmaps.fatal == "bitmaps/missing.PCX");
    resource_cache_free(&cache, host);
}

} // namespace
} // namespace oa::ui::frontend::test
