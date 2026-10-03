// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// What the Full tier's presentation holds between frames (runtime_full.cpp):
// the card's executor on the game's renderer, the match's terrain atlas,
// built as the match loads, and the pages it was uploaded as, the target a
// zoom between whole numbers is drawn through, the overlay of what the
// processor drew over the terrain, and what the last Full frame drew, which
// the render tiers check reads.
#pragma once

#include "oa/app/runtime.hpp"

#include "full_terrain.hpp"
#include "oa/app/card/executor.hpp"

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

    card::CardFrame frame; ///< the frame being built, its memory kept between frames

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
