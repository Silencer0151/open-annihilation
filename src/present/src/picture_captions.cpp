// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Captions over the player's own pictures: a pictures.tdf section's keys
// read, and each caption painted over its spot of an 8-bit picture.
#include "oa/present/picture_captions.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <limits>
#include <utility>

namespace oa::present {

namespace {

/// Bytes of one palette entry: red, green, blue and one unused.
constexpr std::size_t palette_entry_bytes = 4;
/// Entries of a palette.
constexpr std::size_t palette_entries = 256;
/// Numbers of an area: x, y, width and height.
constexpr std::size_t area_numbers = 4;
/// What separates a field's parts.
constexpr char part_separator = '|';
/// The darkest an old word's light pixels may be: an area with nothing
/// lighter holds no word to paint out.
constexpr int32_t least_word_luminance = 48;
/// The share of a picture's pixels, in hundredths, that must be
/// transparent for it to be read as words on nothing (a title, whose
/// letters leave a sixth or so of it clear) rather than a face with words
/// on it, which is drawn whole.
constexpr std::size_t transparent_picture_percent = 5;
/// The outline's width round each covered pixel.
constexpr int32_t outline_pixels = 1;
/// The least colourfulness (the spread of an entry's red, green and blue)
/// of an old word's deep pixels. A word drawn in a gradient, as the top
/// bar's METAL is, light along one edge and deep blue along the other,
/// holds such pixels joined to its light ones; the grey face round it
/// does not.
constexpr int32_t least_word_chroma = 60;
/// The steps by column and by row through a row's face pixels that pick
/// the one a pixel by an old word takes: primes, so that neighbours take
/// pixels apart.
constexpr std::size_t face_pick_column = 7;
constexpr std::size_t face_pick_row = 13;

/// Returns text in lower case, ASCII letters only, '\\' read as '/'.
std::string folded(std::string_view text) {
    std::string result(text);
    for (char& letter : result) {
        if (letter >= 'A' && letter <= 'Z')
            letter = static_cast<char>(letter - 'A' + 'a');
        else if (letter == '\\')
            letter = '/';
    }
    return result;
}

/// Splits a field at part_separator, keeping empty parts.
std::vector<std::string_view> parts(std::string_view field) {
    std::vector<std::string_view> result;
    for (;;) {
        const auto separator = field.find(part_separator);
        result.push_back(field.substr(0, separator));
        if (separator == std::string_view::npos)
            return result;
        field.remove_prefix(separator + 1);
    }
}

/// Trims spaces and tabs from both ends.
std::string_view trimmed(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
        text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t'))
        text.remove_suffix(1);
    return text;
}

/// Reads "x,y,width,height"; empty for anything else, or a size not above 0.
std::optional<CaptionArea> read_area(std::string_view text) {
    std::array<int32_t, area_numbers> numbers{};
    std::size_t count = 0;
    text = trimmed(text);
    while (count < area_numbers) {
        const auto comma = text.find(',');
        const auto number = trimmed(text.substr(0, comma));
        int32_t value = 0;
        const auto [end, failure] =
            std::from_chars(number.data(), number.data() + number.size(), value);
        if (failure != std::errc{} || end != number.data() + number.size() || number.empty())
            return std::nullopt;
        numbers[count++] = value;
        if (comma == std::string_view::npos)
            break;
        text.remove_prefix(comma + 1);
    }
    if (count != area_numbers || numbers[2] <= 0 || numbers[3] <= 0)
        return std::nullopt;
    return CaptionArea{numbers[0], numbers[1], numbers[2], numbers[3]};
}

/// Reads an align value; centre for anything it does not know.
CaptionAlign read_align(std::string_view text) {
    const std::string word = folded(trimmed(text));
    if (word == "left")
        return CaptionAlign::left;
    if (word == "right")
        return CaptionAlign::right;
    return CaptionAlign::centre;
}

/// A palette entry's luminance, 0..255.
int32_t luminance(std::span<const uint8_t> palette, uint8_t index) {
    const std::size_t at = static_cast<std::size_t>(index) * palette_entry_bytes;
    constexpr int32_t red_weight = 299;
    constexpr int32_t green_weight = 587;
    constexpr int32_t blue_weight = 114;
    constexpr int32_t weight_total = 1000;
    return (red_weight * palette[at] + green_weight * palette[at + 1] +
            blue_weight * palette[at + 2]) /
           weight_total;
}

/// A palette entry's colourfulness: the spread of its red, green and blue,
/// 0..255.
int32_t chroma(std::span<const uint8_t> palette, uint8_t index) {
    const std::size_t at = static_cast<std::size_t>(index) * palette_entry_bytes;
    const auto [low, high] = std::minmax({palette[at], palette[at + 1], palette[at + 2]});
    return high - low;
}

/// The luminance that best splits a set of luminances in two (the split
/// with the greatest variance between the two parts).
int32_t splitting_luminance(const std::array<uint32_t, palette_entries>& counts) {
    double total = 0;
    double weighted_total = 0;
    for (std::size_t level = 0; level < counts.size(); ++level) {
        total += counts[level];
        weighted_total += static_cast<double>(level) * counts[level];
    }
    double below = 0;
    double weighted_below = 0;
    double best = -1;
    int32_t split = 0;
    for (std::size_t level = 0; level < counts.size(); ++level) {
        below += counts[level];
        weighted_below += static_cast<double>(level) * counts[level];
        const double above = total - below;
        if (below == 0 || above == 0)
            continue;
        const double mean_below = weighted_below / below;
        const double mean_above = (weighted_total - weighted_below) / above;
        const double between =
            below * above * (mean_below - mean_above) * (mean_below - mean_above);
        if (between > best) {
            best = between;
            split = static_cast<int32_t>(level);
        }
    }
    return split;
}

/// The commonest index of a count, skipping `avoid`; empty when none counted.
std::optional<uint8_t>
commonest(const std::array<uint32_t, palette_entries>& counts, std::optional<uint8_t> avoid) {
    std::optional<uint8_t> best;
    for (std::size_t index = 0; index < counts.size(); ++index) {
        if (counts[index] == 0 || (avoid && *avoid == index))
            continue;
        if (!best || counts[index] > counts[*best])
            best = static_cast<uint8_t>(index);
    }
    return best;
}

/// The darkest palette entry, skipping `avoid`.
uint8_t darkest(std::span<const uint8_t> palette, std::optional<uint8_t> avoid) {
    uint8_t best = 0;
    int32_t best_luminance = std::numeric_limits<int32_t>::max();
    for (std::size_t index = 0; index < palette_entries; ++index) {
        if (avoid && *avoid == index)
            continue;
        const int32_t level = luminance(palette, static_cast<uint8_t>(index));
        if (level < best_luminance) {
            best_luminance = level;
            best = static_cast<uint8_t>(index);
        }
    }
    return best;
}

/// Tells whether a pixel is drawn.
bool drawn(const IndexedPicture& picture, std::size_t at) {
    return picture.coverage.empty() || picture.coverage[at] != 0;
}

/// Tells whether enough of a picture is transparent for it to be words on
/// nothing (transparent_picture_percent).
bool mostly_words_on_nothing(const IndexedPicture& picture) {
    if (picture.coverage.empty())
        return false;
    const auto clear = static_cast<std::size_t>(
        std::count(picture.coverage.begin(), picture.coverage.end(), uint8_t{0})
    );
    constexpr std::size_t whole = 100;
    return clear * whole >= picture.coverage.size() * transparent_picture_percent;
}

/// The default spot of a picture: inset by default_caption_border, or the
/// box of the drawn pixels of one mostly transparent.
std::optional<CaptionArea> default_area(const IndexedPicture& picture) {
    if (mostly_words_on_nothing(picture)) {
        int32_t left = picture.width;
        int32_t top = picture.height;
        int32_t right = -1;
        int32_t bottom = -1;
        for (int32_t y = 0; y < picture.height; ++y)
            for (int32_t x = 0; x < picture.width; ++x)
                if (drawn(picture, static_cast<std::size_t>(y) * picture.width + x)) {
                    left = std::min(left, x);
                    top = std::min(top, y);
                    right = std::max(right, x);
                    bottom = std::max(bottom, y);
                }
        if (right < left)
            return std::nullopt;
        return CaptionArea{left, top, right - left + 1, bottom - top + 1};
    }
    const int32_t border = picture.width > 2 * default_caption_border + 1 &&
                                   picture.height > 2 * default_caption_border + 1
                               ? default_caption_border
                               : 0;
    return CaptionArea{border, border, picture.width - 2 * border, picture.height - 2 * border};
}

/// An area clipped to the picture; empty when nothing of it is left.
std::optional<CaptionArea> clipped(const CaptionArea& area, const IndexedPicture& picture) {
    const int32_t left = std::max(area.x, 0);
    const int32_t top = std::max(area.y, 0);
    const int32_t right = std::min(area.x + area.width, picture.width);
    const int32_t bottom = std::min(area.y + area.height, picture.height);
    if (right <= left || bottom <= top)
        return std::nullopt;
    return CaptionArea{left, top, right - left, bottom - top};
}

/// The covered box of a drawn line.
struct InkBox {
    int32_t left{};
    int32_t top{};
    int32_t width{};
    int32_t height{};
};

/// Finds the box of a line's covered pixels; empty for a line with none.
std::optional<InkBox> ink_box(const CaptionLine& line) {
    int32_t left = line.width;
    int32_t top = line.height;
    int32_t right = -1;
    int32_t bottom = -1;
    for (int32_t y = 0; y < line.height; ++y)
        for (int32_t x = 0; x < line.width; ++x)
            if (line.alpha[static_cast<std::size_t>(y) * line.width + x] != 0) {
                left = std::min(left, x);
                top = std::min(top, y);
                right = std::max(right, x);
                bottom = std::max(bottom, y);
            }
    if (right < left)
        return std::nullopt;
    return InkBox{left, top, right - left + 1, bottom - top + 1};
}

/// Draws a caption's line at the largest size whose covered pixels, with
/// their outline, fit an area; at the smallest size when none fits.
std::optional<std::pair<CaptionLine, InkBox>>
fitted_line(std::string_view text, const CaptionArea& area, const CaptionDraw& draw) {
    const int32_t outline = 2 * outline_pixels;
    int32_t size =
        std::clamp(area.height + outline, smallest_caption_pixel_size, largest_caption_pixel_size);
    std::optional<std::pair<CaptionLine, InkBox>> smallest;
    for (; size >= smallest_caption_pixel_size; --size) {
        auto line = draw(text, size);
        if (!line || line->width <= 0 || line->height <= 0 ||
            line->alpha.size() !=
                static_cast<std::size_t>(line->width) * static_cast<std::size_t>(line->height))
            continue;
        const auto box = ink_box(*line);
        if (!box)
            return std::nullopt;
        if (box->width + outline <= area.width && box->height + outline <= area.height)
            return std::pair{std::move(*line), *box};
        smallest = std::pair{std::move(*line), *box};
    }
    return smallest;
}

/// Tells whether a picture's planes and a palette are whole.
bool holds(const IndexedPicture& picture, std::span<const uint8_t> palette) {
    const std::size_t pixel_count = static_cast<std::size_t>(std::max(picture.width, 0)) *
                                    static_cast<std::size_t>(std::max(picture.height, 0));
    return pixel_count != 0 && picture.pixels.size() == pixel_count &&
           (picture.coverage.empty() || picture.coverage.size() == pixel_count) &&
           palette.size() >= palette_entries * palette_entry_bytes;
}

/// Calls `visit` with the place of each drawn pixel of an area inside a
/// picture.
template <typename Visit>
void for_each_drawn(const IndexedPicture& picture, const CaptionArea& area, Visit visit) {
    for (int32_t y = area.y; y < area.y + area.height; ++y)
        for (int32_t x = area.x; x < area.x + area.width; ++x) {
            const std::size_t at = static_cast<std::size_t>(y) * picture.width + x;
            if (drawn(picture, at))
                visit(at);
        }
}

/// Counts the luminances of an area's drawn pixels.
std::array<uint32_t, palette_entries> luminance_counts(
    const IndexedPicture& picture, std::span<const uint8_t> palette, const CaptionArea& area
) {
    std::array<uint32_t, palette_entries> levels{};
    for_each_drawn(picture, area, [&](std::size_t at) {
        ++levels[static_cast<std::size_t>(luminance(palette, picture.pixels[at]))];
    });
    return levels;
}

} // namespace

std::optional<CaptionArea> PictureCaption::area(std::size_t index) const {
    if (areas.empty())
        return std::nullopt;
    if (areas.size() == 1)
        return areas.front();
    return index < areas.size() ? areas[index] : std::nullopt;
}

std::string picture_name(std::string_view file, std::string_view sequence) {
    std::string name = folded(file);
    if (const auto slash = name.find_last_of('/'); slash != std::string::npos)
        name.erase(0, slash + 1);
    if (!sequence.empty())
        name += "/" + folded(sequence);
    return name;
}

PictureCaption read_picture_caption(const CaptionKeys& keys) {
    PictureCaption caption;
    const auto text = keys.find("text");
    if (text == keys.end() || text->second.empty())
        return caption;
    for (const auto part : parts(text->second))
        caption.texts.emplace_back(trimmed(part));
    if (const auto areas = keys.find("area"); areas != keys.end() && !areas->second.empty())
        for (const auto part : parts(areas->second))
            caption.areas.push_back(read_area(part));
    if (const auto align = keys.find("align"); align != keys.end())
        caption.align = read_align(align->second);
    return caption;
}

std::optional<uint8_t> word_index(
    const IndexedPicture& picture, std::span<const uint8_t> palette, const CaptionArea& area
) {
    if (!holds(picture, palette))
        return std::nullopt;
    const auto clipped_area = clipped(area, picture);
    if (!clipped_area)
        return std::nullopt;
    const auto levels = luminance_counts(picture, palette, *clipped_area);
    const int32_t split = std::max(splitting_luminance(levels), least_word_luminance);
    std::array<uint32_t, palette_entries> word_indices{};
    bool darker = false;
    for_each_drawn(picture, *clipped_area, [&](std::size_t at) {
        const uint8_t index = picture.pixels[at];
        if (luminance(palette, index) > split)
            ++word_indices[index];
        else
            darker = true;
    });
    // Words are lighter than something round them.
    if (!darker)
        return std::nullopt;
    return commonest(word_indices, picture.transparent_index);
}

uint8_t outline_index(const IndexedPicture& picture, std::span<const uint8_t> palette) {
    return darkest(palette, picture.transparent_index);
}

std::optional<CaptionArea>
caption_area(const IndexedPicture& picture, std::optional<CaptionArea> area) {
    if (!area)
        area = default_area(picture);
    if (!area)
        return std::nullopt;
    return clipped(*area, picture);
}

bool draw_caption(
    IndexedPicture picture,
    std::string_view text,
    const CaptionArea& area,
    CaptionAlign align,
    CaptionInk ink,
    const CaptionDraw& draw
) {
    const std::size_t pixel_count = static_cast<std::size_t>(std::max(picture.width, 0)) *
                                    static_cast<std::size_t>(std::max(picture.height, 0));
    if (text.empty() || !draw || pixel_count == 0 || picture.pixels.size() != pixel_count ||
        (!picture.coverage.empty() && picture.coverage.size() != pixel_count))
        return false;
    const auto spot = clipped(area, picture);
    if (!spot)
        return false;
    const auto line = fitted_line(text, *spot, draw);
    if (!line)
        return false;
    const auto& [drawn_line, box] = *line;
    const int32_t outline = 2 * outline_pixels;
    const int32_t free_columns = spot->width - (box.width + outline);
    const int32_t left = spot->x + outline_pixels +
                         (align == CaptionAlign::left    ? 0
                          : align == CaptionAlign::right ? std::max(free_columns, 0)
                                                         : free_columns / 2) -
                         box.left;
    const int32_t top =
        spot->y + outline_pixels + (spot->height - (box.height + outline)) / 2 - box.top;
    const auto covered = [&drawn_line](int32_t x, int32_t y) {
        return x >= 0 && y >= 0 && x < drawn_line.width && y < drawn_line.height &&
               drawn_line.alpha[static_cast<std::size_t>(y) * drawn_line.width + x] != 0;
    };
    const auto paint = [&](int32_t x, int32_t y, uint8_t index) {
        if (x < spot->x || y < spot->y || x >= spot->x + spot->width || y >= spot->y + spot->height)
            return;
        const std::size_t at = static_cast<std::size_t>(y) * picture.width + x;
        picture.pixels[at] = index;
        if (!picture.coverage.empty())
            picture.coverage[at] = 1;
    };
    for (int32_t y = -outline_pixels; y < drawn_line.height + outline_pixels; ++y)
        for (int32_t x = -outline_pixels; x < drawn_line.width + outline_pixels; ++x) {
            if (covered(x, y)) {
                paint(left + x, top + y, ink.text);
                continue;
            }
            bool beside = false;
            for (int32_t dy = -outline_pixels; dy <= outline_pixels && !beside; ++dy)
                for (int32_t dx = -outline_pixels; dx <= outline_pixels && !beside; ++dx)
                    beside = covered(x + dx, y + dy);
            if (beside)
                paint(left + x, top + y, ink.outline);
        }
    return true;
}

bool caption_picture(
    IndexedPicture picture,
    std::span<const uint8_t> palette,
    std::string_view text,
    std::optional<CaptionArea> area,
    CaptionAlign align,
    const CaptionDraw& draw
) {
    if (text.empty() || !draw || !holds(picture, palette))
        return false;
    area = caption_area(picture, area);
    if (!area)
        return false;
    const auto at = [&picture](int32_t x, int32_t y) {
        return static_cast<std::size_t>(y) * static_cast<std::size_t>(picture.width) +
               static_cast<std::size_t>(x);
    };

    // The area's light pixels are the old words, and so are the colourful
    // pixels joined to them, the deep part of a word drawn in a gradient;
    // the rest is its face.
    const auto levels = luminance_counts(picture, palette, *area);
    const int32_t split = std::max(splitting_luminance(levels), least_word_luminance);
    std::array<uint32_t, palette_entries> word_indices{};
    std::array<uint32_t, palette_entries> face_indices{};
    std::vector<uint8_t> word(picture.pixels.size(), 0);
    std::vector<std::size_t> joining;
    for_each_drawn(picture, *area, [&](std::size_t pixel) {
        const uint8_t index = picture.pixels[pixel];
        if (luminance(palette, index) > split) {
            ++word_indices[index];
            word[pixel] = 1;
            joining.push_back(pixel);
        }
    });
    const auto inside = [&area](int32_t x, int32_t y) {
        return x >= area->x && x < area->x + area->width && y >= area->y &&
               y < area->y + area->height;
    };
    while (!joining.empty()) {
        const std::size_t pixel = joining.back();
        joining.pop_back();
        const auto x = static_cast<int32_t>(pixel % static_cast<std::size_t>(picture.width));
        const auto y = static_cast<int32_t>(pixel / static_cast<std::size_t>(picture.width));
        for (int32_t dy = -1; dy <= 1; ++dy)
            for (int32_t dx = -1; dx <= 1; ++dx) {
                if (!inside(x + dx, y + dy))
                    continue;
                const std::size_t next = at(x + dx, y + dy);
                if (word[next] == 0 && drawn(picture, next) &&
                    chroma(palette, picture.pixels[next]) >= least_word_chroma) {
                    word[next] = 1;
                    joining.push_back(next);
                }
            }
    }
    for_each_drawn(picture, *area, [&](std::size_t pixel) {
        if (word[pixel] == 0)
            ++face_indices[picture.pixels[pixel]];
    });
    CaptionInk ink;
    ink.outline = outline_index(picture, palette);
    ink.text =
        commonest(word_indices, picture.transparent_index)
            .value_or(commonest(face_indices, picture.transparent_index).value_or(ink.outline));
    if (!fitted_line(text, *area, draw))
        return false;

    // Paint the old words out.
    if (mostly_words_on_nothing(picture)) {
        for (int32_t y = area->y; y < area->y + area->height; ++y)
            for (int32_t x = area->x; x < area->x + area->width; ++x) {
                picture.coverage[at(x, y)] = 0;
                if (picture.transparent_index)
                    picture.pixels[at(x, y)] = *picture.transparent_index;
            }
    } else if (const auto face = commonest(face_indices, picture.transparent_index)) {
        // Each pixel by an old word takes one of the face's pixels in its
        // own row, so that a face drawn with a grain, as the top bar's is,
        // keeps it there; a row with no face left takes the face's
        // commonest index.
        std::vector<uint8_t> row_face;
        std::vector<uint8_t> near_word(static_cast<std::size_t>(area->width), 0);
        for (int32_t y = area->y; y < area->y + area->height; ++y) {
            row_face.clear();
            for (int32_t x = area->x; x < area->x + area->width; ++x) {
                bool near = false;
                for (int32_t dy = -outline_pixels; dy <= outline_pixels && !near; ++dy)
                    for (int32_t dx = -outline_pixels; dx <= outline_pixels && !near; ++dx)
                        near = inside(x + dx, y + dy) && word[at(x + dx, y + dy)] != 0;
                near_word[static_cast<std::size_t>(x - area->x)] = near ? 1 : 0;
                const uint8_t index = picture.pixels[at(x, y)];
                if (!near && drawn(picture, at(x, y)) && index != picture.transparent_index)
                    row_face.push_back(index);
            }
            for (int32_t x = area->x; x < area->x + area->width; ++x) {
                if (near_word[static_cast<std::size_t>(x - area->x)] == 0 ||
                    !drawn(picture, at(x, y)))
                    continue;
                // Spread the row's face pixels over the gap in an order no
                // row repeats, rather than in stripes.
                const auto pick = static_cast<std::size_t>(x) * face_pick_column +
                                  static_cast<std::size_t>(y) * face_pick_row;
                picture.pixels[at(x, y)] =
                    row_face.empty() ? *face : row_face[pick % row_face.size()];
            }
        }
    }
    return draw_caption(picture, text, *area, align, ink, draw);
}

std::optional<std::size_t>
frame_caption(const PictureCaption& captions, std::size_t frame, std::size_t frames) {
    if (captions.texts.empty() || frame >= frames)
        return std::nullopt;
    if (captions.texts.size() == 1)
        return 0;
    if (frame < captions.texts.size())
        return frame;
    return std::nullopt;
}

std::size_t caption_frames(
    std::span<IndexedPicture> frames,
    std::span<const uint8_t> palette,
    const PictureCaption& captions,
    const CaptionDraw& draw
) {
    std::size_t drawn_captions = 0;
    if (frames.size() == 1) {
        // Each caption is a spot of the one frame.
        for (std::size_t index = 0; index < captions.texts.size(); ++index)
            if (caption_picture(
                    frames.front(),
                    palette,
                    captions.texts[index],
                    captions.area(index),
                    captions.align,
                    draw
                ))
                ++drawn_captions;
        return drawn_captions;
    }
    for (std::size_t frame = 0; frame < frames.size(); ++frame) {
        const auto index = frame_caption(captions, frame, frames.size());
        if (index && caption_picture(
                         frames[frame],
                         palette,
                         captions.texts[*index],
                         captions.area(*index),
                         captions.align,
                         draw
                     ))
            ++drawn_captions;
    }
    return drawn_captions;
}

} // namespace oa::present
