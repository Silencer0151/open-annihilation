// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Captions drawn over the player's own pictures: the words the game data
// paints into its art (order buttons, the top bar's METAL and ENERGY, the
// PAUSED, VICTORY! and DEFEAT titles, the load and save dialogs' titles),
// written again in a language the art does not show. A language pack's
// pictures.tdf names each picture and gives its captions; the old words in
// a caption's area are painted out in the face's own colour and the
// caption is drawn there in the modern fonts, in the old words' colour,
// with a one-pixel outline, at the largest size that fits the area. The
// pictures stay the player's: the captions are composed in memory when a
// picture is loaded, and a picture the pack does not name is left as it
// is.
//
// pictures.tdf holds one section per picture: a GAF sequence as
// [<file>.gaf/<sequence>] and a bitmap as [<file>.pcx], each without its
// folder and read without regard to the case of its letters. A section
// may also sit inside one named for its file ([commongui.gaf] {
// [ARMFIREORD] { ... } }), and a section with no keys leaves the picture
// as it is:
//
//     [commongui.gaf/ARMFIREORD]
//     {
//         text=<hold fire>|<return fire>|<fire at will>|<fire orders>;
//         area=3,3,108,15;
//         align=left;
//     }
//
// - text: the captions, '|' between them. In a picture of one frame each
//   caption is a spot of that frame; else one caption is drawn on every
//   frame, and several are drawn on the frames in order, caption n on
//   frame n. An empty caption leaves its spot as the art has it.
// - area: optional. x,y,width,height of each caption's spot in the
//   picture's pixels, '|' between them; one area serves every caption. A
//   spot with no area is the picture inset by default_caption_border, or,
//   in a picture with transparent pixels, the box of its drawn ones.
// - align: optional. left, centre (the default) or right.
//
// The language packs read the file (oa::data::languages::PictureCaptions,
// which gives a section's keys by its picture_name); this module reads a
// section's keys and draws its captions.
//
// A GAF frame drawn in layers (a button's blank face, then its words) has
// its upper layers cleared in the caption's spot, so that the face shows
// there, and is then drawn into one picture that takes the caption.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace oa::present {

/// The columns and rows a spot with no area leaves round an opaque picture:
/// the bevel of a button face.
inline constexpr int32_t default_caption_border = 3;

/// The smallest pixel size a caption is drawn at; a caption that does not
/// fit its area at this size is drawn at it and clipped to the area.
inline constexpr int32_t smallest_caption_pixel_size = 6;

/// The largest pixel size a caption is drawn at.
inline constexpr int32_t largest_caption_pixel_size = 64;

/// A rectangle of a picture, in its pixels.
struct CaptionArea {
    int32_t x{};      ///< left column
    int32_t y{};      ///< top row
    int32_t width{};  ///< columns
    int32_t height{}; ///< rows
};

/// Where a caption lies across its area.
enum class CaptionAlign : uint8_t {
    centre, ///< in the middle
    left,   ///< against the area's left edge
    right,  ///< against the area's right edge
};

/// The captions a pack draws over one picture.
struct PictureCaption {
    /// The captions, in UTF-8; an empty one leaves its spot alone.
    std::vector<std::string> texts{};
    /// The area of each caption's spot: none, one for every caption, or
    /// one per caption; an absent one is the default spot.
    std::vector<std::optional<CaptionArea>> areas{};
    /// Where each caption lies across its area.
    CaptionAlign align{CaptionAlign::centre};

    /// Returns the area of a caption's spot.
    ///
    /// @param index the caption
    /// @return its area; empty for the default spot
    [[nodiscard]] std::optional<CaptionArea> area(std::size_t index) const;
};

/// Gives the name a picture is known by in pictures.tdf.
///
/// @param file the picture's file, with or without its folder
///        ("anims/commongui.gaf", "bitmaps\\DLoadgame2.pcx")
/// @param sequence the GAF sequence; empty for a bitmap
/// @return the name, in lower case: "commongui.gaf/armattack",
///         "dloadgame2.pcx"
[[nodiscard]] std::string picture_name(std::string_view file, std::string_view sequence = {});

/// The keys of one picture's section of pictures.tdf, by lower-case key.
using CaptionKeys = std::map<std::string, std::string, std::less<>>;

/// Reads a picture's captions from the keys of its section.
///
/// @param keys the section's keys
/// @return the captions; none when the keys hold no text
[[nodiscard]] PictureCaption read_picture_caption(const CaptionKeys& keys);

/// One line of a caption as the modern fonts draw it.
struct CaptionLine {
    int32_t width{};  ///< columns
    int32_t height{}; ///< rows
    /// width * height bytes, top row first: non-zero where the text covers
    /// a pixel
    std::vector<uint8_t> alpha{};
};

/// Draws a caption's text in one line at a pixel size; empty when it
/// cannot be drawn.
using CaptionDraw =
    std::function<std::optional<CaptionLine>(std::string_view text, int32_t pixel_size)>;

/// An 8-bit picture: one GAF frame or a bitmap.
struct IndexedPicture {
    int32_t width{};  ///< columns
    int32_t height{}; ///< rows
    /// width * height palette indices, top row first
    std::span<uint8_t> pixels{};
    /// width * height bytes, 0 where the picture is transparent; empty for
    /// a picture drawn everywhere
    std::span<uint8_t> coverage{};
    /// The index the picture's file keeps for transparent pixels, which a
    /// caption never paints with; empty for none.
    std::optional<uint8_t> transparent_index{};
};

/// The palette indices a caption is drawn in.
struct CaptionInk {
    uint8_t text{};    ///< the letters
    uint8_t outline{}; ///< the one-pixel outline round them
};

/// Finds the commonest index of the words in a spot of a picture: its
/// drawn pixels lighter than the rest.
///
/// @param picture the picture
/// @param palette 256 entries of 4 bytes, red, green, blue and one unused
/// @param area the spot, clipped to the picture
/// @return the index; empty when the spot holds no words
[[nodiscard]] std::optional<uint8_t> word_index(
    const IndexedPicture& picture, std::span<const uint8_t> palette, const CaptionArea& area
);

/// Returns the darkest palette entry other than a picture's transparent
/// one, which outlines a caption.
///
/// @param picture the picture
/// @param palette 256 entries of 4 bytes, red, green, blue and one unused
/// @return the index
[[nodiscard]] uint8_t
outline_index(const IndexedPicture& picture, std::span<const uint8_t> palette);

/// Returns a caption's spot in a picture: `area` clipped to it, or the
/// default spot.
///
/// @param picture the picture
/// @param area the spot pictures.tdf gives; empty for the default spot
/// @return the spot; empty when nothing of it lies in the picture
[[nodiscard]] std::optional<CaptionArea>
caption_area(const IndexedPicture& picture, std::optional<CaptionArea> area);

/// Draws a caption, with its outline, in a spot of a picture, at the
/// largest size that fits it, leaving what lies under it otherwise as it
/// is.
///
/// @param picture the picture, changed in place
/// @param text the caption, in UTF-8; empty draws nothing
/// @param area its spot, inside the picture
/// @param align where it lies across the spot
/// @param ink the indices it is drawn in
/// @param draw draws the text
/// @return true when the caption was drawn
bool draw_caption(
    IndexedPicture picture,
    std::string_view text,
    const CaptionArea& area,
    CaptionAlign align,
    CaptionInk ink,
    const CaptionDraw& draw
);

/// Draws one caption over a picture.
///
/// In a picture that is mostly opaque the old words are the area's light
/// pixels and the colourful ones joined to them, as a word drawn in a
/// gradient holds; they and the pixel round each take the face's pixels of
/// their own row, or the face's commonest index in a row with none left.
/// In one with transparent pixels the whole area is cleared.
/// The caption takes the old words' commonest light index, and its
/// outline the palette's darkest.
///
/// @param picture the picture, changed in place
/// @param palette 256 entries of 4 bytes, red, green, blue and one unused
/// @param text the caption, in UTF-8; empty draws nothing
/// @param area its spot; empty for the default spot
/// @param align where it lies across the spot
/// @param draw draws the text
/// @return true when the caption was drawn
bool caption_picture(
    IndexedPicture picture,
    std::span<const uint8_t> palette,
    std::string_view text,
    std::optional<CaptionArea> area,
    CaptionAlign align,
    const CaptionDraw& draw
);

/// Tells which caption a picture's frame shows, as pictures.tdf places
/// them (see the header's comment) for a picture of several frames.
///
/// @param captions the picture's captions
/// @param frame the frame
/// @param frames the picture's frames
/// @return the caption's index; empty for none
[[nodiscard]] std::optional<std::size_t>
frame_caption(const PictureCaption& captions, std::size_t frame, std::size_t frames);

/// Draws a picture's captions over its frames, as pictures.tdf places
/// them (see the header's comment).
///
/// @param frames the picture's frames, in order, changed in place
/// @param palette 256 entries of 4 bytes, red, green, blue and one unused
/// @param captions the picture's captions
/// @param draw draws the text
/// @return the captions drawn
std::size_t caption_frames(
    std::span<IndexedPicture> frames,
    std::span<const uint8_t> palette,
    const PictureCaption& captions,
    const CaptionDraw& draw
);

} // namespace oa::present
