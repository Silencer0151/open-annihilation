// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// What the Full tier's presentation holds between frames (runtime_full.cpp):
// the card's executor on the game's renderer, the match's terrain atlas,
// built as the match loads, and the pages it was uploaded as, the target a
// zoom between whole numbers is drawn through, the stages of the scene
// builder switched on with the sprite pages and the card's pages that hold
// them, the overlays of what the processor drew over the terrain and over
// the stages, and what the last Full frame drew, which the render tiers
// check reads.
#pragma once

#include "oa/app/runtime.hpp"

#include "full_terrain.hpp"
#include "oa/app/card/executor.hpp"
#include "oa/present/gpu_world/sprite_pages.hpp"
#include "runtime_full.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace oa::app {

/// A call of the card's own that failed, or the Full function test that
/// did, which drops Full to Basic for the rest of the run (Runtime::drop_full).
class FullCardError : public std::runtime_error {
  public:

    using std::runtime_error::runtime_error;
};

/// The atlas levels the Full tier uploads and draws from: 0 and 1, since
/// the game's zoom never goes below one half.
inline constexpr uint8_t full_terrain_levels = 2;

/// The target a zoom above 1 that is not whole is drawn through, where the
/// renderer lacks the pixel-art sampling mode, is sized to a multiple of
/// this, so that it holds a whole number of map pixels at every
/// whole-number zoom up to the largest (2, 3 and 4 divide it).
inline constexpr uint32_t full_target_grain = 12;

/// Side of an ordinary sprite page, in texels, and of the largest page a
/// frame too big for one may have of its own.
inline constexpr uint32_t full_sprite_page_side = 1024;
inline constexpr uint32_t full_largest_sprite_page_side = 2048;
/// Texel memory the sprite pages may hold: four ordinary pages. A
/// placeholder until the performance pass sets the budget.
inline constexpr std::size_t full_sprite_page_memory = std::size_t{16} * 1024 * 1024;

/// The card's page that holds a page of the sprite pages.
struct FullCardPage {
    card::PageHandle handle{}; ///< none for a page not made
    uint32_t size{};           ///< texels a side it was made with
    /// The sprite page's revision whose texels it holds; 0 for none
    /// uploaded yet, which uploads the whole page.
    uint64_t revision{};
};

struct Runtime::FullPresentation {
    /// What a terrain atlas was built from: the map by the storage and size
    /// of its tile grid and its tile pixels, and the grid's extent. One map
    /// record holds every map of the run in turn, so the record's address
    /// tells one map from the next no better than the storage its vectors
    /// hold, which each new map takes afresh; every member zero names no
    /// map.
    struct AtlasSource {
        const uint16_t* tile_indices{};
        std::size_t tile_index_count{};
        const uint8_t* tile_pixels{};
        std::size_t tile_pixel_count{};
        uint32_t tile_width{};
        uint32_t tile_height{};

        friend bool operator==(const AtlasSource&, const AtlasSource&) = default;

        /// Returns what identifies a map's terrain.
        ///
        /// @param map the map
        /// @return the identity
        [[nodiscard]] static AtlasSource of(const oa::formats::tnt::Map& map) noexcept {
            return {
                map.tile_indices.data(),
                map.tile_indices.size(),
                map.tile_palette_indices.data(),
                map.tile_palette_indices.size(),
                map.tile_width,
                map.tile_height
            };
        }
    };

    bool on{};               ///< the tier decided for the frame is Full (set_full_presentation)
    card::Executor executor; ///< open on the renderer while Full holds pages
    bool function_tested{};  ///< the Full function test ran on this executor and passed

    // The match's terrain atlas and what it was built from. Once the pages
    // are filled the atlas keeps its grid, its slots and its pages' sizes
    // and levels, which the builder reads, and not their texels, which the
    // card holds: read it with tile_rect, never with read_terrain_view.
    oa::present::gpu_world::TerrainAtlas atlas;
    AtlasSource atlas_source{}; ///< the map it was built from; every member zero for none
    oa::PaletteBytes atlas_palette{};
    bool atlas_gamma{}; ///< built through the display gamma table
    std::array<uint8_t, 256> atlas_gamma_table{};
    uint32_t atlas_page_edge{};
    std::vector<card::PageHandle> pages; ///< the executor's page of each atlas page, in order
    uint64_t page_bytes{};               ///< texel bytes uploaded to the pages
    uint64_t atlas_build_ns{};           ///< the time the atlas took to build
    uint64_t page_upload_ns{};           ///< the time the pages took to make and fill
    /// The pages were made as the match loaded (make_full_match_pages),
    /// before its first frame, not at a frame.
    bool pages_from_load{};

    // The target a zoom between whole numbers is drawn through.
    card::TargetHandle target{};
    uint32_t target_width{};  ///< pixels across; 0 for none
    uint32_t target_height{}; ///< pixels down
    /// The target could not be made on this renderer, so such a zoom draws
    /// the terrain LINEAR straight to the window.
    bool target_refused{};

    card::CardFrame frame; ///< the terrain's frame being built, its memory kept between frames

    // The stages of the scene builder beside the terrain (full_stages.hpp).
    uint8_t stages{}; ///< the stages switched on, among those this build draws
    /// The last match frame's world layer leaves the card's kinds undrawn,
    /// and the base below was kept from it.
    bool frame_drawn{};
    bool stages_logged{};   ///< the stages' first frame has been logged
    bool overflow_logged{}; ///< the pages overflowing a frame has been logged
    /// The sprite pages, with the match's palette and gray table.
    oa::present::gpu_world::SpritePages sprite_pages{oa::present::gpu_world::Limits{
        full_sprite_page_side, full_largest_sprite_page_side, full_sprite_page_memory
    }};
    /// The palette generation of the sprite pages the gray table was built for.
    uint64_t gray_generation{};
    std::vector<FullCardPage> card_pages; ///< the card's pages, by the sprite pages' index
    card::CardFrame stage_frame;          ///< the stages' batches, run after the first overlay
    /// The world layer after the fog's gray and before the fog's black and
    /// the painters, without the card's kinds (capture_full_base).
    renderer::Surface base;
    /// What the fog's black and the painters changed over the base, as
    /// ARGB8888 words, and its texture: the second overlay.
    TiledTexture painted_texture;
    std::vector<uint8_t> painted;
    std::vector<uint8_t> painted_opaque_bands;
    std::vector<uint8_t> painted_uploaded_bands;
    bool painted_uploaded{};
    full::SpriteStageResult sprites{}; ///< what the sprite stage did on the last frame built
    uint64_t stage_ns{}; ///< the time the stages took to build, upload and run, last frame

    /// Sets the sprite pages' palette and gray table to the match's.
    ///
    /// @param palette_bytes the match's palette, 4 bytes a colour
    /// @param gamma the display gamma
    void ensure_sprite_palette(const oa::PaletteBytes& palette_bytes, float gamma);

    /// Returns the card's page for a sprite page, making it when the page
    /// is new or has a new size.
    ///
    /// Throws FullCardError when the card cannot make it.
    ///
    /// @param page index into the sprite pages
    /// @return the handle; none for a page the sprite pages do not hold
    [[nodiscard]] card::PageHandle card_page(uint32_t page);

    /// Returns the card's page for a sprite page (card_page), as the sprite
    /// stage asks for it (full::SpritePageHooks::card_page).
    ///
    /// @param context the presentation
    /// @param page index into the sprite pages
    /// @return the handle
    [[nodiscard]] static card::PageHandle card_page_hook(void* context, uint32_t page);

    /// Uploads what the sprite pages changed since the card's pages last
    /// took their texels: a page made this frame whole, another its dirty
    /// rectangle; a page released is destroyed.
    ///
    /// Throws FullCardError when a page cannot be filled.
    void upload_sprite_pages();

    /// Destroys the card's pages of the sprite pages.
    void destroy_sprite_card_pages() noexcept;

    // The overlay of what the processor drew over its terrain base.
    TiledTexture overlay_texture;
    std::vector<uint8_t> overlay; ///< ARGB8888 words, the battlefield's size
    std::vector<uint8_t> opaque_bands;
    std::vector<uint8_t> uploaded_bands;
    bool overlay_uploaded{};
    ScaledWorldCounts counts; ///< what the overlay's texture was made of

    // What the last Full match frame drew, for the checks and the statistics.
    bool drawn{}; ///< the last match frame presented drew its terrain on the card
    float drawn_zoom{};
    full_terrain::TerrainDrawPlan plan{};
    uint32_t drawn_quads{};
    uint32_t drawn_batches{};
    bool drawn_through_target{};
    uint64_t build_ns{};   ///< the time the frame took to build
    uint64_t execute_ns{}; ///< the time the card's call took
    uint64_t overlay_ns{}; ///< the time the overlay took to convert and upload
    uint64_t frames{};     ///< match frames the Full branch presented
};

} // namespace oa::app
