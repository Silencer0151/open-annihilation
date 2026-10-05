// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Captions over pictures: a picture's keys read, a picture's name, a
// caption drawn when it has text and none without, the old
// words painted out on a face, a word in a gradient painted out whole on a
// face with a grain, and cleared from a title, the captions
// placed over a picture's frames, and a caption drawn in the ink given.

#include "oa/present/picture_captions.hpp"
#include "oa/test/check.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace {

namespace present = oa::present;

/// A grey ramp: index n is (n, n, n).
std::array<uint8_t, 1024> grey_palette() {
    std::array<uint8_t, 1024> palette{};
    for (std::size_t index = 0; index < 256; ++index) {
        palette[index * 4] = static_cast<uint8_t>(index);
        palette[index * 4 + 1] = static_cast<uint8_t>(index);
        palette[index * 4 + 2] = static_cast<uint8_t>(index);
    }
    return palette;
}

constexpr uint8_t face = 100;     ///< a button face's grey
constexpr uint8_t old_word = 220; ///< the light grey of its old words
constexpr uint8_t transparent = 9;

/// Draws any text as a solid block a size wide per byte and a size tall,
/// with an empty row above, as a font's line has room above its letters.
present::CaptionDraw block_font() {
    return [](std::string_view text, int32_t size) -> std::optional<present::CaptionLine> {
        present::CaptionLine line;
        line.width = static_cast<int32_t>(text.size()) * size;
        line.height = size + 1;
        line.alpha.assign(static_cast<std::size_t>(line.width) * line.height, 0);
        std::fill(line.alpha.begin() + line.width, line.alpha.end(), uint8_t{255});
        return line;
    };
}

/// A 40x20 opaque face with an old word, a light bar, across its middle.
struct Face {
    static constexpr int32_t width = 40;
    static constexpr int32_t height = 20;
    std::vector<uint8_t> pixels = std::vector<uint8_t>(width * height, face);

    Face() {
        for (int32_t y = 8; y < 12; ++y)
            for (int32_t x = 10; x < 30; ++x)
                pixels[y * width + x] = old_word;
    }

    present::IndexedPicture picture() { return {width, height, pixels, {}, transparent}; }
};

void reads_a_caption() {
    // A picture's keys, as the language packs give them by lower-case key.
    const auto fire = present::read_picture_caption(
        {{"text", "a|b| c |d"}, {"area", "3,3,79,15"}, {"align", "left"}}
    );
    OA_CHECK((fire.texts == std::vector<std::string>{"a", "b", "c", "d"}));
    OA_CHECK(fire.align == present::CaptionAlign::left);
    // One area serves every caption.
    OA_CHECK(fire.area(3).has_value() && fire.area(3)->width == 79);
    const auto load = present::read_picture_caption({{"text", "t|u"}, {"area", "1,2,3,4|bad"}});
    OA_CHECK(load.area(0).has_value() && load.area(0)->height == 4);
    // An area that does not read is the default spot.
    OA_CHECK(!load.area(1).has_value());
    // Keys without text give no caption.
    OA_CHECK(present::read_picture_caption({{"area", "1,2,3,4"}}).texts.empty());
}

void names_pictures() {
    OA_CHECK(present::picture_name("anims/IGTITLES.GAF", "igpaused") == "igtitles.gaf/igpaused");
    OA_CHECK(present::picture_name("bitmaps\\DSavegame2.pcx") == "dsavegame2.pcx");
}

void draws_a_caption_only_for_a_named_picture() {
    const auto palette = grey_palette();
    // A named picture: the old word is painted out in the face's grey and
    // the caption drawn in its light grey, outlined in the darkest entry.
    Face named;
    std::array<present::IndexedPicture, 1> frames{named.picture()};
    OA_CHECK(
        present::caption_frames(
            frames, palette, present::read_picture_caption({{"text", "A"}}), block_font()
        ) == 1
    );
    const auto count = [](const std::vector<uint8_t>& pixels, uint8_t index) {
        return std::count(pixels.begin(), pixels.end(), index);
    };
    OA_CHECK(count(named.pixels, old_word) > 0);
    OA_CHECK(count(named.pixels, 0) > 0);
    // The caption stays inside the default spot, the bevel untouched.
    for (int32_t x = 0; x < Face::width; ++x)
        OA_CHECK(named.pixels[x] == face);
    // The old bar, wider than the caption, is gone where the caption is not.
    OA_CHECK(named.pixels[9 * Face::width + 11] == face);

    // A caption with no text leaves the picture's pixels as they are.
    Face unnamed;
    const auto before = unnamed.pixels;
    std::array<present::IndexedPicture, 1> unnamed_frames{unnamed.picture()};
    OA_CHECK(
        present::caption_frames(unnamed_frames, palette, present::PictureCaption{}, block_font()) ==
        0
    );
    OA_CHECK(unnamed.pixels == before);
}

void paints_out_a_word_in_a_gradient() {
    auto palette = grey_palette();
    // A deep blue, darker than the face but colourful.
    constexpr uint8_t deep_blue = 30;
    palette[deep_blue * 4] = 20;
    palette[deep_blue * 4 + 1] = 40;
    palette[deep_blue * 4 + 2] = 200;
    // A face with a grain of two greys, and an old word light along its
    // top and deep blue below, its left edge deep blue alone.
    constexpr uint8_t grain = 104;
    constexpr int32_t width = 40;
    constexpr int32_t height = 20;
    std::vector<uint8_t> pixels(width * height);
    for (int32_t y = 0; y < height; ++y)
        for (int32_t x = 0; x < width; ++x)
            pixels[y * width + x] = (x % 2) != 0 ? face : grain;
    for (int32_t y = 6; y < 14; ++y)
        for (int32_t x = 8; x < 32; ++x)
            pixels[y * width + x] = y < 9 && x > 9 ? old_word : deep_blue;
    present::IndexedPicture picture{width, height, pixels, {}, transparent};
    OA_CHECK(
        present::caption_picture(
            picture,
            palette,
            "A",
            present::CaptionArea{4, 3, 32, 14},
            present::CaptionAlign::centre,
            block_font()
        )
    );
    // The whole word is gone, its deep part too.
    OA_CHECK(std::count(pixels.begin(), pixels.end(), deep_blue) == 0);
    // Where it was, beside the caption, the face's grain goes on.
    bool grey_face = false;
    bool grey_grain = false;
    for (int32_t x = 8; x < 13; ++x) {
        grey_face = grey_face || pixels[12 * width + x] == face;
        grey_grain = grey_grain || pixels[12 * width + x] == grain;
    }
    OA_CHECK(grey_face && grey_grain);
}

void clears_a_title() {
    const auto palette = grey_palette();
    constexpr int32_t width = 30;
    constexpr int32_t height = 12;
    std::vector<uint8_t> pixels(width * height, transparent);
    std::vector<uint8_t> coverage(width * height, 0);
    for (int32_t y = 2; y < 10; ++y)
        for (int32_t x = 4; x < 26; ++x) {
            pixels[y * width + x] = (x % 2) != 0 ? old_word : face;
            coverage[y * width + x] = 1;
        }
    present::IndexedPicture title{width, height, pixels, coverage, transparent};
    OA_CHECK(
        present::caption_picture(
            title, palette, "A", std::nullopt, present::CaptionAlign::centre, block_font()
        )
    );
    // The old letters' columns beside the caption are cleared.
    OA_CHECK(coverage[5 * width + 5] == 0);
    OA_CHECK(std::count(coverage.begin(), coverage.end(), uint8_t{1}) > 0);
    OA_CHECK(std::count(pixels.begin(), pixels.end(), old_word) > 0);
}

void places_captions_over_frames() {
    const auto palette = grey_palette();
    std::array<Face, 3> faces{};
    std::array<present::IndexedPicture, 3> frames{
        faces[0].picture(), faces[1].picture(), faces[2].picture()
    };
    const auto before = faces[2].pixels;
    present::PictureCaption two;
    two.texts = {"A", "B"};
    // Caption n on frame n; the third frame keeps its art.
    OA_CHECK(present::caption_frames(frames, palette, two, block_font()) == 2);
    OA_CHECK(faces[2].pixels == before);
    // An empty caption leaves its frame alone.
    std::array<Face, 2> more{};
    std::array<present::IndexedPicture, 2> pair{more[0].picture(), more[1].picture()};
    const auto kept = more[0].pixels;
    present::PictureCaption skipped;
    skipped.texts = {"", "B"};
    OA_CHECK(present::caption_frames(pair, palette, skipped, block_font()) == 1);
    OA_CHECK(more[0].pixels == kept);
}

void draws_in_the_ink_given() {
    const auto palette = grey_palette();
    Face blank;
    // A blank face: the old words are gone already, so only the caption
    // and its outline change it.
    std::fill(blank.pixels.begin(), blank.pixels.end(), face);
    auto picture = blank.picture();
    const auto spot = present::caption_area(picture, std::nullopt);
    OA_CHECK(spot.has_value() && spot->x == present::default_caption_border);
    OA_CHECK(!present::word_index(picture, palette, *spot).has_value());
    OA_CHECK(
        spot.has_value() &&
        present::draw_caption(
            picture, "A", *spot, present::CaptionAlign::centre, {old_word, 1}, block_font()
        )
    );
    OA_CHECK(std::count(blank.pixels.begin(), blank.pixels.end(), old_word) > 0);
    OA_CHECK(std::count(blank.pixels.begin(), blank.pixels.end(), uint8_t{1}) > 0);
    OA_CHECK(present::word_index(picture, palette, *spot) == old_word);
}

} // namespace

int main() {
    reads_a_caption();
    names_pictures();
    draws_a_caption_only_for_a_named_picture();
    paints_out_a_word_in_a_gradient();
    clears_a_title();
    places_captions_over_frames();
    draws_in_the_ink_given();
    return oa::test::check_exit_status();
}
