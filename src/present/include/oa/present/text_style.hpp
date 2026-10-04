// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// How game text is drawn, as the player's Language settings choose
// it. The text drawing reads it each frame; it changes only what is drawn,
// never the simulation, a saved game or what a shared game sends.
#pragma once

#include <cstdint>

namespace oa::present {

/// The text size that draws the modern fonts as large as the game's own
/// fonts, in percent: the size the game's screens and panels are laid out
/// for.
inline constexpr int32_t game_font_text_size = 100;
/// The smallest text size, in percent.
inline constexpr int32_t lowest_text_size = 50;
/// The largest text size, in percent.
inline constexpr int32_t highest_text_size = 300;
/// The text size a player starts with, in percent: a fifth smaller than the
/// game's fonts.
inline constexpr int32_t default_text_size = 80;

/// How game text is drawn. A default TextStyle is the settings' own
/// defaults: modern fonts at default_text_size, with an outline and a
/// shadow, and no background.
struct TextStyle {
    /// Game text is drawn in the modern fonts, which hold the letters of
    /// many languages; off, it is drawn in the game's own 8-bit fonts.
    bool modern_fonts{true};
    /// Each letter of modern text has a dark outline round it.
    bool outline{true};
    /// Modern text casts a dark shadow below and to the right of it.
    bool shadow{true};
    /// Each line of modern text is drawn on a shaded box.
    bool background{};
    /// The size of game text in the modern fonts, in percent of the game
    /// fonts' sizes, lowest_text_size to highest_text_size. The game's own
    /// fonts keep their sizes.
    int32_t size{default_text_size};

    friend bool operator==(const TextStyle&, const TextStyle&) = default;
};

} // namespace oa::present
