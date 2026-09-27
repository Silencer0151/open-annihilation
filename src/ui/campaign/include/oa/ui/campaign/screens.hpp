// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Screen-registry glue for the campaign package (ids 0x400-0x4FF).
#pragma once

#include "oa/ui/campaign/endgame.hpp"

#include <cstdint>

namespace oa::formats::fnt {
struct Font;
}

namespace oa::formats::gaf {
struct Sequence;
}

namespace oa::ui::campaign {

inline constexpr uint16_t kScoreOverlayScreen = 14; // app::Screen::campaign_end
inline constexpr uint16_t kFirstCampaignScreen = 0x400;
inline constexpr uint16_t kLastCampaignScreen = 0x4ff;

// An 8-bit picture drawn through the end screen's fade palette.
struct EndgamePicture {
    const uint8_t* pixels{};
    uint32_t width{};
    uint32_t height{};
};

// What the end-of-game overlay runs and draws: the finished game's world,
// session record and screen state, the screen's services, the fonts,
// 0x400-byte palette, light table and 32xlogos sequence (player colour
// swatches) the screen is drawn with. The glamour picture, once the outcome
// step loads it, covers the screen while it fades in.
struct EndgameView {
    World* world{};
    oa::data::campaign::CampaignFile* campaign{};
    EndgameScreen* screen{};
    const EndgameHost* host{};
    const oa::formats::fnt::Font* font{}; // GUI font slot 0: "Click to continue."
    // GUI font slot 1 (hattfont11): the score rows' names and values; null
    // draws no text in the rows.
    const oa::formats::fnt::Font* label_font{};
    const uint8_t* palette{};
    // 32 rows of 256 palette entries (palettes/palette.lht); the names are
    // drawn through row kScoreNameLight. Null draws them in the font's own
    // colours.
    const uint8_t* light_table{};
    const oa::formats::gaf::Sequence* player_logos{};
    const EndgamePicture* glamour{};
};

/// Runs the end-of-game screen (endgame_tick) over a finished game.
///
/// The overlay takes over the host's input and continue-label services, and
/// keeps its own copy of the label font lit through the names' light-table
/// row. The app keeps everything the view points to alive until it
/// publishes an empty view.
///
/// @param view Finished game, screen state, services and drawing resources;
///             without a world, screen or host the overlay is cleared.
void publish_endgame(const EndgameView& view);

} // namespace oa::ui::campaign
