// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Captions over the player's own pictures: the shown language's
// pictures.tdf captions (Runtime::language_pictures) drawn in the bundled
// fonts over the GAF sequences and bitmaps it names as they are loaded
// (oa/present/picture_captions.hpp).

#include "oa/app/runtime.hpp"

#include "oa/platform/text_font.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace oa::app {

namespace {

namespace text_font = oa::platform::text_font;

/// The palette the game's GAF pictures are drawn in.
constexpr std::string_view game_palette_file = "palettes/PALETTE.PAL";
/// Bytes of a palette: 256 entries of red, green, blue and one unused.
constexpr std::size_t palette_bytes = 1024;
/// Bytes of one palette entry.
constexpr std::size_t palette_entry_bytes = 4;
/// Bytes of one RGB pixel.
constexpr std::size_t rgb_pixel_bytes = 3;
/// The most drawn lines kept before the store starts again.
constexpr std::size_t kept_caption_lines = 4096;

/// Tells whether a picture's path lies in one of the game data's folders
/// for a language, whose name joins a folder and the language's word with
/// '-' (bitmaps-German, anims-Chinese).
bool in_language_folder(std::string_view path, std::string_view word) {
    const auto slash = path.find_last_of("/\\");
    if (slash == std::string_view::npos || word.empty())
        return false;
    const std::string_view folder = path.substr(0, slash);
    const auto dash = folder.find_last_of('-');
    if (dash == std::string_view::npos || folder.size() - dash - 1 != word.size())
        return false;
    for (std::size_t index = 0; index < word.size(); ++index) {
        const auto lower = [](char letter) {
            return letter >= 'A' && letter <= 'Z' ? static_cast<char>(letter - 'A' + 'a') : letter;
        };
        if (lower(folder[dash + 1 + index]) != lower(word[index]))
            return false;
    }
    return true;
}

/// Draws a picture's caption over a GAF frame drawn in layers: the upper
/// layers are cleared in the caption's spot so that the lowest (a button's
/// blank face) shows there, the frame is drawn into one picture, and the
/// caption is drawn on it in the old words' colour.
///
/// @param frame the frame, made one picture in place
/// @param palette the game palette
/// @param text the caption
/// @param area its spot; empty for the default spot
/// @param align where it lies across the spot
/// @param draw draws the text
/// @return true when the caption was drawn
bool caption_layered_frame(
    oa::formats::gaf::Frame& frame,
    std::span<const uint8_t> palette,
    std::string_view text,
    std::optional<oa::present::CaptionArea> area,
    oa::present::CaptionAlign align,
    const oa::present::CaptionDraw& draw
) {
    if (text.empty() || frame.width == 0 || frame.height == 0)
        return false;
    // A layered frame is drawn opaque: its spot is the frame inset.
    if (!area) {
        const int32_t border = oa::present::default_caption_border;
        area = oa::present::CaptionArea{
            border, border, frame.width - 2 * border, frame.height - 2 * border
        };
    }
    std::optional<uint8_t> word;
    for (std::size_t index = 1; index < frame.layers.size(); ++index) {
        auto& layer = frame.layers[index];
        const std::size_t pixel_count = static_cast<std::size_t>(layer.width) * layer.height;
        if (!layer.layers.empty() || layer.pixels.size() != pixel_count ||
            layer.coverage.size() != pixel_count)
            continue;
        // The layer lies at the frame's origin less its own.
        const oa::present::CaptionArea spot{
            area->x - (frame.origin_x - layer.origin_x),
            area->y - (frame.origin_y - layer.origin_y),
            area->width,
            area->height
        };
        const oa::present::IndexedPicture picture{
            layer.width, layer.height, layer.pixels, layer.coverage, layer.transparency_index
        };
        if (!word)
            word = oa::present::word_index(picture, palette, spot);
        for (int32_t y = std::max(spot.y, 0);
             y < std::min(spot.y + spot.height, int32_t{layer.height});
             ++y)
            for (int32_t x = std::max(spot.x, 0);
                 x < std::min(spot.x + spot.width, int32_t{layer.width});
                 ++x) {
                const std::size_t at = static_cast<std::size_t>(y) * layer.width + x;
                layer.coverage[at] = 0;
                layer.pixels[at] = layer.transparency_index;
            }
    }
    auto rendered = oa::formats::gaf::render_normal(frame);
    if (!rendered.ok())
        return false;
    frame.pixels = std::move(rendered.frame->pixels);
    frame.coverage = std::move(rendered.frame->coverage);
    frame.layers.clear();
    frame.layer_count = 0;
    frame.compressed = 0;
    const oa::present::IndexedPicture picture{
        frame.width, frame.height, frame.pixels, frame.coverage, frame.transparency_index
    };
    const auto spot = oa::present::caption_area(picture, area);
    if (!spot)
        return false;
    oa::present::CaptionInk ink;
    ink.outline = oa::present::outline_index(picture, palette);
    ink.text =
        word.value_or(oa::present::word_index(picture, palette, *spot).value_or(ink.outline));
    return oa::present::draw_caption(picture, text, *spot, align, ink, draw);
}

} // namespace

struct Runtime::PictureCaptionState {
    /// Guards the fonts and the lines, which one thread at a time uses.
    std::mutex mutex{};
    /// The bundled fonts have been opened, or failed to.
    bool fonts_opened{};
    /// The bundled fonts; null when they do not open.
    std::unique_ptr<text_font::FontStack> fonts{};
    /// The lines drawn so far, by text and pixel size; an empty one could
    /// not be drawn.
    std::map<std::pair<std::string, int32_t>, std::optional<oa::present::CaptionLine>> lines{};
    /// The game palette has been read, or failed to.
    bool palette_read{};
    /// The game palette; empty when it cannot be read.
    std::vector<uint8_t> palette{};
};

void Runtime::destroy_picture_caption_state(PictureCaptionState* state) noexcept {
    delete state;
}

namespace {

/// Draws captions in a state's fonts (Runtime::PictureCaptionState),
/// keeping each line drawn.
template <typename CaptionState>
oa::present::CaptionDraw caption_font(CaptionState& state) {
    return [&state](std::string_view text, int32_t pixel_size) {
        auto key = std::pair{std::string(text), pixel_size};
        if (const auto found = state.lines.find(key); found != state.lines.end())
            return found->second;
        if (state.lines.size() >= kept_caption_lines)
            state.lines.clear();
        std::optional<oa::present::CaptionLine> line;
        text_font::Style style;
        style.pixel_size = pixel_size;
        style.weight = text_font::Weight::bold;
        style.rendering = text_font::Rendering::mono;
        if (state.fonts)
            if (const auto drawn = state.fonts->draw(text, style))
                line = oa::present::CaptionLine{drawn->width, drawn->height, drawn->alpha};
        state.lines.emplace(std::move(key), line);
        return line;
    };
}

} // namespace

void Runtime::caption_gaf_pictures(
    std::string_view file, oa::formats::gaf::Archive& archive, std::size_t first_sequence
) {
    if (in_language_folder(file, shown_language().game_name))
        return;
    const auto& captions = language_pictures();
    if (captions.all().empty())
        return;
    if (!picture_caption_state_)
        picture_caption_state_.reset(new PictureCaptionState{});
    auto& state = *picture_caption_state_;
    const std::lock_guard lock(state.mutex);
    if (!state.palette_read) {
        state.palette_read = true;
        try {
            state.palette = assets_.read(std::string(game_palette_file)).bytes;
        } catch (const std::exception&) {
            state.palette.clear();
        }
    }
    if (state.palette.size() != palette_bytes)
        return;
    for (std::size_t index = first_sequence; index < archive.sequences.size(); ++index) {
        auto& sequence = archive.sequences[index];
        const auto* keys = captions.find(oa::present::picture_name(file, sequence.name));
        if (keys == nullptr)
            continue;
        const auto entry = oa::present::read_picture_caption(*keys);
        if (entry.texts.empty())
            continue;
        if (!state.fonts_opened) {
            state.fonts_opened = true;
            state.fonts = text_font::FontStack::open(text_font::bundled_font_directory());
        }
        if (!state.fonts)
            return;
        const auto draw = caption_font(state);
        auto& frames = sequence.frames;
        for (std::size_t at = 0; at < frames.size(); ++at) {
            auto& frame = frames[at];
            // A picture of one frame takes each caption in its own spot.
            for (std::size_t caption = 0; caption < entry.texts.size(); ++caption) {
                if (frames.size() != 1) {
                    const auto shown = oa::present::frame_caption(entry, at, frames.size());
                    if (!shown || *shown != caption)
                        continue;
                }
                const std::string& text = entry.texts[caption];
                if (!frame.layers.empty()) {
                    caption_layered_frame(
                        frame, state.palette, text, entry.area(caption), entry.align, draw
                    );
                    continue;
                }
                const std::size_t pixel_count =
                    static_cast<std::size_t>(frame.width) * frame.height;
                if (frame.pixels.size() != pixel_count || frame.coverage.size() != pixel_count)
                    continue;
                oa::present::caption_picture(
                    {frame.width,
                     frame.height,
                     frame.pixels,
                     frame.coverage,
                     frame.transparency_index},
                    state.palette,
                    text,
                    entry.area(caption),
                    entry.align,
                    draw
                );
            }
        }
    }
}

void Runtime::caption_bitmap(std::string_view file, oa::Image& image) {
    if (in_language_folder(file, shown_language().game_name))
        return;
    const auto& captions = language_pictures();
    if (captions.all().empty())
        return;
    if (!picture_caption_state_)
        picture_caption_state_.reset(new PictureCaptionState{});
    const auto* keys = captions.find(oa::present::picture_name(file));
    const std::size_t pixel_count = static_cast<std::size_t>(image.width) * image.height;
    if (keys == nullptr || !image.palette || image.indices.size() != pixel_count ||
        image.rgb.size() != pixel_count * rgb_pixel_bytes)
        return;
    auto& state = *picture_caption_state_;
    const std::lock_guard lock(state.mutex);
    if (!state.fonts_opened) {
        state.fonts_opened = true;
        state.fonts = text_font::FontStack::open(text_font::bundled_font_directory());
    }
    if (!state.fonts)
        return;
    std::array<oa::present::IndexedPicture, 1> frames{oa::present::IndexedPicture{
        static_cast<int32_t>(image.width), static_cast<int32_t>(image.height), image.indices
    }};
    const auto entry = oa::present::read_picture_caption(*keys);
    if (oa::present::caption_frames(frames, *image.palette, entry, caption_font(state)) == 0)
        return;
    // The RGB plane shows the indices again through the bitmap's palette.
    const auto& palette = *image.palette;
    for (std::size_t pixel = 0; pixel < pixel_count; ++pixel) {
        const std::size_t entry_at =
            static_cast<std::size_t>(image.indices[pixel]) * palette_entry_bytes;
        std::memcpy(&image.rgb[pixel * rgb_pixel_bytes], &palette[entry_at], rgb_pixel_bytes);
    }
}

} // namespace oa::app
