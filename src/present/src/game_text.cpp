// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/present/game_text.hpp"

#include "oa/present/blit.hpp"

#include <algorithm>
#include <limits>
#include <unordered_map>

namespace oa::present {

namespace {

/// The last Unicode code point.
constexpr char32_t last_code_point = 0x10FFFF;
/// The surrogate code points, which UTF-8 never encodes.
constexpr char32_t first_surrogate = 0xD800;
constexpr char32_t last_surrogate = 0xDFFF;
/// The smallest code point of a two-, three- and four-byte sequence.
constexpr char32_t smallest_two_byte = 0x80;
constexpr char32_t smallest_three_byte = 0x800;
constexpr char32_t smallest_four_byte = 0x10000;
/// Payload bits of a continuation byte.
constexpr uint32_t continuation_bits = 6;
/// The longest UTF-8 sequence.
constexpr std::size_t longest_sequence = 4;
/// The largest coverage, fully covered.
constexpr uint32_t full_coverage = 255;
/// The first byte above ASCII.
constexpr uint8_t first_high_byte = 0x80;
/// The first byte a game font draws: the space.
constexpr char32_t first_drawn_character = 0x20;
/// The byte of the game's code page that stands in for a character it lacks.
constexpr char missing_character = '?';
/// Bytes of one palette entry: red, green, blue and one more.
constexpr std::size_t palette_entry_bytes = 4;

/// The characters of the game's code page at 0x80-0x9F; the five it leaves
/// undefined stand for the control characters of the same value.
constexpr std::array<char32_t, 32> code_page_marks{
    0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160,
    0x2039, 0x0152, 0x008D, 0x017D, 0x008F, 0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022,
    0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178,
};

/// The GUI fonts' letters at 0x80-0xAF, which are the DOS code page's, but
/// for 0xA6, which holds Á, and 0xAD, which repeats ¿; 0 marks a slot no
/// character is read from.
constexpr std::array<char32_t, 48> gui_font_dos_letters{
    0x00C7, 0x00FC, 0x00E9, 0x00E2, 0x00E4, 0x00E0, 0x00E5, 0x00E7, 0x00EA, 0x00EB, 0x00E8, 0x00EF,
    0x00EE, 0x00EC, 0x00C4, 0x00C5, 0x00C9, 0x00E6, 0x00C6, 0x00F4, 0x00F6, 0x00F2, 0x00FB, 0x00F9,
    0x00FF, 0x00D6, 0x00DC, 0x00A2, 0x00A3, 0x00A5, 0x20A7, 0x0192, 0x00E1, 0x00ED, 0x00F3, 0x00FA,
    0x00F1, 0x00D1, 0x00C1, 0x00BA, 0x00BF, 0x2310, 0x00AC, 0x00BD, 0x00BC, 0,      0x00AB, 0x00BB,
};
/// The GUI fonts' slots of the game's code page: the degree sign, then from
/// the inverted question mark to the u with circumflex.
constexpr uint8_t gui_font_degree = 0xB0;
constexpr uint8_t gui_font_first_letter = 0xBF;
constexpr uint8_t gui_font_last_letter = 0xFC;
/// The FNT fonts' slots of the game's code page, Latin-1 from the inverted
/// exclamation mark to the thorn.
constexpr uint8_t fnt_font_first_letter = 0xA1;
constexpr uint8_t fnt_font_last_letter = 0xFE;
/// The marks of the game's code page the FNT fonts hold at 0x80-0x9F.
constexpr std::array<uint8_t, 9> fnt_font_marks{
    0x86, 0x89, 0x8B, 0x91, 0x92, 0x98, 0x9B, 0x9C, 0x9F
};

GameTextHooks installed_hooks{};

/// Returns whether a byte continues a UTF-8 sequence: 10xxxxxx.
constexpr bool is_continuation(uint8_t byte) noexcept {
    return (byte & 0xC0U) == 0x80U;
}

/// The colour of a palette entry.
std::array<uint8_t, 3> entry_color(std::span<const uint8_t> palette, uint8_t index) noexcept {
    const std::size_t at = static_cast<std::size_t>(index) * palette_entry_bytes;
    if (at + 2 >= palette.size())
        return {};
    return {palette[at], palette[at + 1], palette[at + 2]};
}

/// Tells whether a colour is darker than another, by the sum of its channels.
bool darker(std::array<uint8_t, 3> color, std::array<uint8_t, 3> than) noexcept {
    return color[0] + color[1] + color[2] < than[0] + than[1] + than[2];
}

} // namespace

TextCharacter utf8_sequence(std::string_view text) noexcept {
    if (text.empty())
        return {};
    const auto lead = static_cast<uint8_t>(text[0]);
    std::size_t length = 0;
    char32_t code_point = 0;
    char32_t smallest = 0;
    if ((lead & 0xE0U) == 0xC0U) {
        length = 2;
        code_point = lead & 0x1FU;
        smallest = smallest_two_byte;
    } else if ((lead & 0xF0U) == 0xE0U) {
        length = 3;
        code_point = lead & 0x0FU;
        smallest = smallest_three_byte;
    } else if ((lead & 0xF8U) == 0xF0U) {
        length = longest_sequence;
        code_point = lead & 0x07U;
        smallest = smallest_four_byte;
    } else {
        return {};
    }
    if (text.size() < length)
        return {};
    for (std::size_t index = 1; index < length; ++index) {
        const auto byte = static_cast<uint8_t>(text[index]);
        if (!is_continuation(byte))
            return {};
        code_point = (code_point << continuation_bits) | (byte & 0x3FU);
    }
    if (code_point < smallest || code_point > last_code_point ||
        (code_point >= first_surrogate && code_point <= last_surrogate))
        return {};
    return {length, code_point, true};
}

char32_t code_page_character(uint8_t byte) noexcept {
    if (byte >= first_high_byte && byte < first_high_byte + code_page_marks.size())
        return code_page_marks[byte - first_high_byte];
    return byte;
}

std::optional<uint8_t> code_page_byte(char32_t character) noexcept {
    if (character < first_high_byte ||
        (character >= first_high_byte + code_page_marks.size() && character <= 0xFF))
        return static_cast<uint8_t>(character);
    for (std::size_t index = 0; index < code_page_marks.size(); ++index)
        if (code_page_marks[index] == character)
            return static_cast<uint8_t>(first_high_byte + index);
    return std::nullopt;
}

void append_utf8(std::string& text, char32_t character) {
    const auto push = [&text](uint32_t value) { text.push_back(static_cast<char>(value)); };
    const auto value = static_cast<uint32_t>(character);
    if (value < smallest_two_byte) {
        push(value);
    } else if (value < smallest_three_byte) {
        push(0xC0U | (value >> 6));
        push(0x80U | (value & 0x3FU));
    } else if (value < smallest_four_byte) {
        if (value >= first_surrogate && value <= last_surrogate)
            return;
        push(0xE0U | (value >> 12));
        push(0x80U | ((value >> 6) & 0x3FU));
        push(0x80U | (value & 0x3FU));
    } else if (value <= last_code_point) {
        push(0xF0U | (value >> 18));
        push(0x80U | ((value >> 12) & 0x3FU));
        push(0x80U | ((value >> 6) & 0x3FU));
        push(0x80U | (value & 0x3FU));
    }
}

std::string decode_game_text(std::string_view bytes, bool utf8) {
    std::string text;
    text.reserve(bytes.size());
    for (std::size_t at = 0; at < bytes.size();) {
        if (utf8) {
            if (const auto sequence = utf8_sequence(bytes.substr(at)); sequence.bytes != 0) {
                text.append(bytes.substr(at, sequence.bytes));
                at += sequence.bytes;
                continue;
            }
        }
        append_utf8(text, code_page_character(static_cast<uint8_t>(bytes[at])));
        ++at;
    }
    return text;
}

std::string encode_game_text(std::string_view text, bool utf8) {
    if (utf8)
        return std::string(text);
    std::string bytes;
    bytes.reserve(text.size());
    for (std::size_t at = 0; at < text.size();) {
        const auto byte = static_cast<uint8_t>(text[at]);
        if (byte < first_high_byte) {
            bytes.push_back(static_cast<char>(byte));
            ++at;
            continue;
        }
        const auto sequence = utf8_sequence(text.substr(at));
        if (sequence.bytes == 0) {
            bytes.push_back(missing_character);
            ++at;
            continue;
        }
        const auto held = code_page_byte(sequence.code_point);
        bytes.push_back(held ? static_cast<char>(*held) : missing_character);
        at += sequence.bytes;
    }
    return bytes;
}

std::size_t last_character_start(std::string_view text) noexcept {
    if (text.empty())
        return 0;
    // The nearest start of a sequence that runs to the end of the text is
    // the last character.
    for (std::size_t back = 2; back <= std::min(longest_sequence, text.size()); ++back) {
        const std::size_t start = text.size() - back;
        if (utf8_sequence(text.substr(start)).bytes == back)
            return start;
    }
    return text.size() - 1;
}

FontCharacters FontCharacters::gui_font(const HasGlyph& has_glyph) {
    FontCharacters font;
    const auto add = [&font](char32_t character, uint8_t byte) {
        if (std::none_of(font.extra_.begin(), font.extra_.end(), [&](const auto& entry) {
                return entry.first == character;
            }))
            font.extra_.emplace_back(character, byte);
    };
    if (has_glyph(gui_font_degree))
        add(gui_font_degree, gui_font_degree);
    for (uint32_t byte = gui_font_first_letter; byte <= gui_font_last_letter; ++byte)
        if (has_glyph(static_cast<uint8_t>(byte)))
            add(byte, static_cast<uint8_t>(byte));
    for (std::size_t index = 0; index < gui_font_dos_letters.size(); ++index) {
        const auto byte = static_cast<uint8_t>(first_high_byte + index);
        if (gui_font_dos_letters[index] != 0 && has_glyph(byte))
            add(gui_font_dos_letters[index], byte);
    }
    std::sort(font.extra_.begin(), font.extra_.end());
    return font;
}

FontCharacters FontCharacters::fnt_font(const HasGlyph& has_glyph) {
    FontCharacters font;
    for (uint32_t byte = fnt_font_first_letter; byte <= fnt_font_last_letter; ++byte)
        if (has_glyph(static_cast<uint8_t>(byte)))
            font.extra_.emplace_back(byte, static_cast<uint8_t>(byte));
    for (const uint8_t byte : fnt_font_marks)
        if (has_glyph(byte))
            font.extra_.emplace_back(code_page_character(byte), byte);
    std::sort(font.extra_.begin(), font.extra_.end());
    return font;
}

std::optional<uint8_t> FontCharacters::byte_for(char32_t character) const noexcept {
    if (character >= first_drawn_character && character < first_high_byte)
        return static_cast<uint8_t>(character);
    const auto found = std::lower_bound(
        extra_.begin(),
        extra_.end(),
        character,
        [](const std::pair<char32_t, uint8_t>& entry, char32_t value) {
            return entry.first < value;
        }
    );
    if (found == extra_.end() || found->first != character)
        return std::nullopt;
    return found->second;
}

bool same_glyph(const Sprite* glyph, const Sprite* other) {
    if (glyph == nullptr || other == nullptr)
        return false;
    if (glyph->width != other->width || glyph->height != other->height ||
        glyph->origin_x != other->origin_x || glyph->origin_y != other->origin_y)
        return false;
    // Each is drawn over two fills, so its transparent pixels show too.
    for (const uint8_t fill : {uint8_t{0x00}, uint8_t{0xFF}}) {
        SurfaceBuffer drawn[2]{
            create_surface(glyph->width, glyph->height), create_surface(other->width, other->height)
        };
        std::fill(drawn[0].pixels.begin(), drawn[0].pixels.end(), fill);
        std::fill(drawn[1].pixels.begin(), drawn[1].pixels.end(), fill);
        draw_sprite(&drawn[0].surface, glyph, glyph->origin_x, glyph->origin_y);
        draw_sprite(&drawn[1].surface, other, other->origin_x, other->origin_y);
        if (drawn[0].pixels != drawn[1].pixels)
            return false;
    }
    return true;
}

std::vector<TextRun> split_text(std::string_view text, const FontCharacters& font) {
    std::vector<TextRun> runs;
    const auto take = [&runs](bool modern) -> std::string& {
        if (runs.empty() || runs.back().modern != modern)
            runs.push_back({modern, {}, game_font_text_size});
        return runs.back().text;
    };
    for (std::size_t at = 0; at < text.size();) {
        const auto sequence = utf8_sequence(text.substr(at));
        const std::size_t length = sequence.bytes != 0 ? sequence.bytes : 1;
        const char32_t character = sequence.bytes != 0
                                       ? sequence.code_point
                                       : code_page_character(static_cast<uint8_t>(text[at]));
        if (const auto byte = font.byte_for(character))
            take(false).push_back(static_cast<char>(*byte));
        else
            take(true).append(text.substr(at, length));
        at += length;
    }
    return runs;
}

void set_game_text_hooks(const GameTextHooks& hooks) noexcept {
    installed_hooks = hooks;
}

const GameTextHooks& game_text_hooks() noexcept {
    return installed_hooks;
}

TextSettings game_text_settings() {
    if (installed_hooks.settings == nullptr)
        return {};
    return installed_hooks.settings(installed_hooks.context);
}

int32_t game_text_size() {
    const TextSettings settings = game_text_settings();
    return settings.style.modern_fonts ? held_text_size(settings.style.size) : game_font_text_size;
}

int32_t face_pixel_size(int32_t game_font_pixels, int32_t scale, int32_t size) noexcept {
    return std::max(
        sized_length(std::max(game_font_pixels, 0), held_text_size(size)) * std::max(scale, 1),
        least_text_pixel_size
    );
}

int32_t text_border(int32_t scale, int32_t size) noexcept {
    // A half rounds down, so that text a half larger keeps the border of
    // the size below.
    const int32_t scaled = std::max(scale, 1) * held_text_size(size);
    return std::max((scaled + game_font_text_size / 2 - 1) / game_font_text_size, 1);
}

TextLayers build_text_layers(const TextMask& mask, const TextStyle& style, int32_t thickness) {
    thickness = std::max(thickness, 1);
    const int32_t ring = style.outline ? thickness : 0;
    const int32_t drop = style.shadow ? thickness : 0;
    TextLayers layers;
    layers.width = std::max(mask.width, 0) + 2 * ring + drop;
    layers.height = std::max(mask.height, 0) + 2 * ring + drop;
    layers.baseline = mask.baseline + ring;
    layers.pen = mask.origin + ring;
    layers.advance = mask.advance + (ring != 0 || drop != 0 ? thickness : 0);
    layers.background = style.background;
    const auto count =
        static_cast<std::size_t>(layers.width) * static_cast<std::size_t>(layers.height);
    layers.fill.assign(count, 0);
    layers.outline.assign(count, 0);
    layers.shadow.assign(count, 0);
    const auto at = [&layers](int32_t column, int32_t row) {
        return static_cast<std::size_t>(row) * static_cast<std::size_t>(layers.width) +
               static_cast<std::size_t>(column);
    };
    for (int32_t row = 0; row < mask.height; ++row)
        for (int32_t column = 0; column < mask.width; ++column) {
            const auto source =
                static_cast<std::size_t>(row) * static_cast<std::size_t>(mask.width) +
                static_cast<std::size_t>(column);
            if (source < mask.alpha.size())
                layers.fill[at(column + ring, row + ring)] = mask.alpha[source];
        }
    // The outline: every pixel within the thickness of a letter's pixel.
    std::vector<uint8_t> ink(layers.fill.size(), 0);
    for (int32_t row = 0; row < layers.height; ++row)
        for (int32_t column = 0; column < layers.width; ++column) {
            if (layers.fill[at(column, row)] == 0)
                continue;
            ink[at(column, row)] = 1;
            for (int32_t dy = -ring; dy <= ring; ++dy)
                for (int32_t dx = -ring; dx <= ring; ++dx) {
                    const int32_t x = column + dx;
                    const int32_t y = row + dy;
                    if (x < 0 || y < 0 || x >= layers.width || y >= layers.height)
                        continue;
                    if (layers.fill[at(x, y)] == 0)
                        layers.outline[at(x, y)] = 1;
                }
        }
    for (std::size_t index = 0; index < ink.size(); ++index)
        ink[index] = static_cast<uint8_t>(ink[index] | layers.outline[index]);
    if (drop != 0)
        for (int32_t row = 0; row + drop < layers.height; ++row)
            for (int32_t column = 0; column + drop < layers.width; ++column)
                if (ink[at(column, row)] != 0)
                    layers.shadow[at(column + drop, row + drop)] = 1;
    // The box spans the rows anything is drawn on.
    if (layers.background) {
        int32_t first = -1;
        int32_t last = -1;
        for (int32_t row = 0; row < layers.height; ++row)
            for (int32_t column = 0; column < layers.width; ++column) {
                const auto index = at(column, row);
                if (ink[index] != 0 || layers.shadow[index] != 0) {
                    first = first < 0 ? row : first;
                    last = row;
                    break;
                }
            }
        layers.background = first >= 0;
        layers.background_top = first;
        layers.background_bottom = last;
    }
    return layers;
}

std::optional<TextLayers> modern_text(
    std::string_view text, TextFace face, int32_t scale, int32_t size, bool allow_background
) {
    if (installed_hooks.draw == nullptr || text.empty())
        return std::nullopt;
    scale = std::max(scale, 1);
    size = held_text_size(size);
    const auto mask = installed_hooks.draw(installed_hooks.context, text, face, scale, size);
    if (!mask)
        return std::nullopt;
    TextStyle style = game_text_settings().style;
    style.background = style.background && allow_background;
    return build_text_layers(*mask, style, text_border(scale, size));
}

namespace {

/// One character of a line the modern fonts drew: its bytes and where the
/// pen stands after it.
struct DrawnCharacter {
    std::size_t offset{}; ///< its first byte
    std::size_t bytes{};  ///< its bytes
    int32_t end{};        ///< the pen's column after it, from the line's start
    bool space{};         ///< it is a space, where a row may break
};

/// Draws a line once and lists its characters with the pen after each.
///
/// @param text UTF-8 text
/// @param face the game font it stands in for
/// @param scale screen pixels to a game pixel
/// @param size the text size, in percent
/// @return the characters; none when the modern fonts cannot draw the text
std::vector<DrawnCharacter>
drawn_characters(std::string_view text, TextFace face, int32_t scale, int32_t size) {
    std::vector<DrawnCharacter> characters;
    if (installed_hooks.draw == nullptr || text.empty())
        return characters;
    const auto mask = installed_hooks.draw(
        installed_hooks.context, text, face, std::max(scale, 1), held_text_size(size)
    );
    if (!mask)
        return characters;
    int32_t pen = 0;
    for (std::size_t at = 0; at < text.size();) {
        const auto sequence = utf8_sequence(text.substr(at));
        const std::size_t length = sequence.bytes != 0 ? sequence.bytes : 1;
        // A character the mask has no pen for takes no room.
        if (characters.size() < mask->character_ends.size())
            pen = mask->character_ends[characters.size()];
        characters.push_back({at, length, pen, text[at] == ' '});
        at += length;
    }
    return characters;
}

/// The columns the borders a style asks for add right of a line's pen.
///
/// @param scale screen pixels to a game pixel
/// @param size the text size, in percent
/// @return the outline's and the shadow's columns
int32_t border_reach(int32_t scale, int32_t size) {
    const TextStyle style = game_text_settings().style;
    const int32_t border = text_border(scale, size);
    return (style.outline ? border : 0) + (style.shadow ? border : 0);
}

} // namespace

std::size_t
modern_text_fit(std::string_view text, TextFace face, int32_t scale, int32_t size, int32_t width) {
    std::size_t fitted = 0;
    for (const DrawnCharacter& character : drawn_characters(text, face, scale, size)) {
        if (character.end > width)
            break;
        fitted = character.offset + character.bytes;
    }
    return fitted;
}

std::size_t
modern_text_tail(std::string_view text, TextFace face, int32_t scale, int32_t size, int32_t width) {
    const auto characters = drawn_characters(text, face, scale, size);
    if (characters.empty())
        return 0;
    // The tail from a character takes the pen's columns from the end of the
    // one before it to the line's end.
    const int32_t end = characters.back().end;
    for (std::size_t index = 0; index < characters.size(); ++index) {
        const int32_t start = index == 0 ? 0 : characters[index - 1].end;
        if (end - start <= width)
            return characters[index].offset;
    }
    return characters.back().offset;
}

std::vector<TextRowSpan>
modern_text_rows(std::string_view text, TextFace face, int32_t scale, int32_t size, int32_t width) {
    std::vector<TextRowSpan> rows;
    if (text.empty())
        return rows;
    const auto characters = drawn_characters(text, face, scale, size);
    if (characters.empty()) {
        rows.push_back({0, text.size()});
        return rows;
    }
    const int32_t room = width - border_reach(scale, size);
    const auto row_of = [&](std::size_t first, std::size_t end) {
        // The spaces a row was broken at stay out of it.
        while (end > first + 1 && characters[end - 1].space)
            --end;
        const std::size_t offset = characters[first].offset;
        return TextRowSpan{offset, characters[end - 1].offset + characters[end - 1].bytes - offset};
    };
    std::size_t first = 0;
    while (first < characters.size()) {
        const int32_t start = first == 0 ? 0 : characters[first - 1].end;
        std::size_t index = first;
        std::size_t last_space = characters.size();
        for (; index < characters.size(); ++index) {
            if (index > first && characters[index].end - start > room)
                break;
            if (characters[index].space)
                last_space = index;
        }
        if (index == characters.size()) {
            rows.push_back(row_of(first, index));
            break;
        }
        // The row breaks at a space that does not fit, else at its last
        // space, else, in a word wider than the row, before the character
        // that does not fit.
        const bool at_space = !characters[index].space && last_space > first && last_space < index;
        const std::size_t end = at_space ? last_space : index;
        rows.push_back(row_of(first, end));
        first = end;
        while (first < characters.size() && characters[first].space)
            ++first;
    }
    return rows;
}

TextCanvas indexed_canvas(Surface& surface, std::span<const uint8_t> palette) {
    TextCanvas canvas;
    canvas.pixels = surface.pixels;
    canvas.width = surface.width;
    canvas.height = surface.height;
    canvas.pitch = surface.pitch;
    canvas.indexed = true;
    canvas.clip_left = std::max(surface.clip.x1, 0);
    canvas.clip_top = std::max(surface.clip.y1, 0);
    canvas.clip_right = std::min(surface.clip.x2, surface.width - 1);
    canvas.clip_bottom = std::min(surface.clip.y2, surface.height - 1);
    canvas.palette = palette;
    return canvas;
}

TextCanvas rgb_canvas(
    std::span<uint8_t> rgb, int32_t width, int32_t height, std::span<const uint8_t> palette
) {
    TextCanvas canvas;
    canvas.pixels = rgb.data();
    canvas.width = width;
    canvas.height = height;
    canvas.pitch = static_cast<std::ptrdiff_t>(width) * 3;
    canvas.clip_right = width - 1;
    canvas.clip_bottom = height - 1;
    canvas.palette = palette;
    if (rgb.size() < static_cast<std::size_t>(std::max(width, 0)) *
                         static_cast<std::size_t>(std::max(height, 0)) * 3U)
        canvas.clip_right = canvas.clip_bottom = -1;
    return canvas;
}

void lay_text(
    TextCanvas& canvas,
    const TextLayers& layers,
    int32_t x,
    int32_t baseline_y,
    std::array<uint8_t, 3> color
) {
    if (canvas.pixels == nullptr || layers.width <= 0 || layers.height <= 0)
        return;
    if (canvas.indexed && canvas.palette.size() < palette_entry_bytes)
        return;
    const int32_t left = x - layers.pen;
    const int32_t top = baseline_y - layers.baseline;
    // The few colours a line makes are reduced once each.
    std::unordered_map<uint32_t, uint8_t> reduced;
    const auto reduce = [&](std::array<uint8_t, 3> mixed) -> uint8_t {
        const auto key = (uint32_t{mixed[0]} << 16) | (uint32_t{mixed[1]} << 8) | mixed[2];
        auto found = reduced.find(key);
        if (found == reduced.end())
            found =
                reduced
                    .emplace(key, nearest_palette_index(canvas.palette, palette_entry_bytes, mixed))
                    .first;
        return found->second;
    };
    // Pixels the picture under an RGB canvas shows through, and the runs of
    // them each layer leaves to the canvas's world_run.
    const bool see_through = !canvas.indexed && canvas.see_through.has_value();
    std::array<std::vector<TextWorldRun>, 3> world_runs;
    const auto leave = [&](TextWorldLayer layer, int32_t px, int32_t py, uint8_t coverage) {
        auto& runs = world_runs[static_cast<std::size_t>(layer)];
        if (!runs.empty()) {
            auto& last = runs.back();
            if (last.y == py && last.x + last.width == px && last.coverage == coverage) {
                ++last.width;
                return;
            }
        }
        const std::array<uint8_t, 3> run_color =
            layer == TextWorldLayer::letter ? color : std::array<uint8_t, 3>{};
        runs.push_back({layer, px, py, 1, run_color, coverage});
    };
    for (int32_t row = 0; row < layers.height; ++row) {
        const int32_t py = top + row;
        if (py < canvas.clip_top || py > canvas.clip_bottom)
            continue;
        const bool boxed =
            layers.background && row >= layers.background_top && row <= layers.background_bottom;
        uint8_t* line = canvas.pixels + static_cast<std::ptrdiff_t>(py) * canvas.pitch;
        for (int32_t column = 0; column < layers.width; ++column) {
            const int32_t px = left + column;
            if (px < canvas.clip_left || px > canvas.clip_right)
                continue;
            const auto at = static_cast<std::size_t>(row) * static_cast<std::size_t>(layers.width) +
                            static_cast<std::size_t>(column);
            uint8_t coverage = layers.fill[at];
            if (!boxed && coverage == 0 && layers.outline[at] == 0 && layers.shadow[at] == 0)
                continue;
            uint8_t* pixel =
                canvas.indexed ? line + px : line + static_cast<std::ptrdiff_t>(px) * 3;
            std::array<uint8_t, 3> shown =
                canvas.indexed ? entry_color(canvas.palette, *pixel)
                               : std::array<uint8_t, 3>{pixel[0], pixel[1], pixel[2]};
            if (!boxed && see_through && shown == *canvas.see_through) {
                // What the picture is here is not known: a letter covering
                // the pixel whole is laid, and every other layer is left to
                // the picture's drawing.
                if (coverage < full_coverage) {
                    if (layers.shadow[at] != 0)
                        leave(TextWorldLayer::shadow, px, py, 0);
                    if (layers.outline[at] != 0)
                        leave(TextWorldLayer::outline, px, py, 0);
                    if (coverage != 0)
                        leave(TextWorldLayer::letter, px, py, coverage);
                    continue;
                }
                shown = color;
                coverage = 0;
            } else {
                if (boxed)
                    shown = text_shade_color;
                if (layers.shadow[at] != 0)
                    shown = blend_color(shown, text_shade_color, text_shadow_alpha);
                if (layers.outline[at] != 0)
                    shown = darker(shown, text_outline_color) ? text_dark_outline_color
                                                              : text_outline_color;
            }
            if (coverage != 0)
                shown = blend_color(shown, color, coverage);
            if (canvas.indexed) {
                *pixel = reduce(shown);
                continue;
            }
            if (!canvas.palette.empty())
                shown = entry_color(canvas.palette, reduce(shown));
            pixel[0] = shown[0];
            pixel[1] = shown[1];
            pixel[2] = shown[2];
        }
    }
    if (canvas.world_run != nullptr)
        for (const auto& runs : world_runs)
            for (const TextWorldRun& run : runs)
                canvas.world_run(canvas.world_user, run);
}

std::array<uint8_t, 3>
blend_color(std::array<uint8_t, 3> under, std::array<uint8_t, 3> over, uint8_t alpha) noexcept {
    std::array<uint8_t, 3> mixed{};
    for (std::size_t channel = 0; channel < mixed.size(); ++channel) {
        const auto weighted =
            static_cast<int32_t>(under[channel]) * static_cast<int32_t>(full_coverage - alpha) +
            static_cast<int32_t>(over[channel]) * static_cast<int32_t>(alpha);
        mixed[channel] = static_cast<uint8_t>(
            (weighted + static_cast<int32_t>(full_coverage / 2)) /
            static_cast<int32_t>(full_coverage)
        );
    }
    return mixed;
}

uint8_t nearest_palette_index(
    std::span<const uint8_t> palette, std::size_t entry_bytes, std::array<uint8_t, 3> color
) noexcept {
    if (entry_bytes < 3)
        return 0;
    const std::size_t entries = std::min<std::size_t>(palette.size() / entry_bytes, 256);
    uint8_t best = 0;
    int32_t best_distance = std::numeric_limits<int32_t>::max();
    for (std::size_t entry = 0; entry < entries; ++entry) {
        int32_t distance = 0;
        for (std::size_t channel = 0; channel < color.size(); ++channel) {
            const int32_t delta = static_cast<int32_t>(palette[entry * entry_bytes + channel]) -
                                  static_cast<int32_t>(color[channel]);
            distance += delta * delta;
        }
        if (distance < best_distance) {
            best_distance = distance;
            best = static_cast<uint8_t>(entry);
        }
    }
    return best;
}

} // namespace oa::present
