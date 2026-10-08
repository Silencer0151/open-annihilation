// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The font stack: FreeType reads the fonts from their files through a stream
// of the engine's own, so that a folder whose path is not in the system's
// code page opens on Windows too, and draws each glyph once per size, weight
// and rendering.

#include "oa/platform/text_font.hpp"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_MODULE_H
#include FT_MULTIPLE_MASTERS_H

#include <algorithm>
#include <cstdio>
#if defined(_WIN32)
#include <share.h>
#endif
#include <cstdlib>
#include <list>
#include <unordered_map>
#include <utility>

namespace oa::platform::text_font {

namespace {

/// The design weight of Noto Emoji's weight axis for each Weight.
constexpr std::array<FT_Fixed, 2> emoji_weights{400 * 65536, 700 * 65536};

/// The fonts each weight looks a character up in, in order.
constexpr std::array<Face, 4> bold_chain{
    Face::dejavu_sans_bold,
    Face::dejavu_sans,
    Face::noto_sans_cjk,
    Face::noto_emoji,
};
constexpr std::array<Face, 3> regular_chain{
    Face::dejavu_sans,
    Face::noto_sans_cjk,
    Face::noto_emoji,
};

/// Rounds a 26.6 fixed-point length to whole pixels.
int32_t whole_pixels(FT_Pos length) noexcept {
    return static_cast<int32_t>((length + 32) >> 6);
}

/// Reads a font file for FreeType: seeks when count is 0, else reads up to
/// count bytes at offset.
unsigned long
read_file(FT_Stream stream, unsigned long offset, unsigned char* buffer, unsigned long count) {
    auto* file = static_cast<std::FILE*>(stream->descriptor.pointer);
    if (offset > static_cast<unsigned long>(stream->size) ||
        std::fseek(file, static_cast<long>(offset), SEEK_SET) != 0)
        return count == 0 ? 1 : 0;
    if (count == 0)
        return 0;
    return static_cast<unsigned long>(std::fread(buffer, 1, count, file));
}

/// Closes a font file when FreeType is done with its face.
void close_file(FT_Stream stream) {
    if (stream->descriptor.pointer != nullptr)
        std::fclose(static_cast<std::FILE*>(stream->descriptor.pointer));
    stream->descriptor.pointer = nullptr;
}

/// Opens a file for reading, by its wide path on Windows, letting others read
/// and write it too; null when it cannot be opened.
std::FILE* open_file(const std::filesystem::path& path) {
#if defined(_WIN32)
    return _wfsopen(path.c_str(), L"rb", _SH_DENYNO);
#else
    return std::fopen(path.c_str(), "rb");
#endif
}

/// FreeType's memory, from the C library.
void* allocate(FT_Memory, long size) {
    return std::malloc(static_cast<std::size_t>(size));
}

void release(FT_Memory, void* block) {
    std::free(block);
}

void* reallocate(FT_Memory, long, long new_size, void* block) {
    return std::realloc(block, static_cast<std::size_t>(new_size));
}

/// One drawn glyph.
struct Glyph {
    int32_t left{};    ///< columns from the pen to the bitmap's left edge
    int32_t top{};     ///< rows from the baseline up to the bitmap's top row
    int32_t width{};   ///< bitmap columns
    int32_t rows{};    ///< bitmap rows
    int32_t advance{}; ///< pixels the pen moves, rounded
    std::vector<uint8_t> alpha{};
};

/// A glyph in the store, with its place in the order of use.
struct KeptGlyph {
    Glyph glyph{};
    std::list<uint64_t>::iterator place{};
};

/// One character laid out: its glyph, font and pen.
struct Laid {
    Placement placement{};
    const Glyph* glyph{};
};

} // namespace

/// The library, the open fonts and the glyphs drawn from them.
struct FontStack::Fonts {
    FT_MemoryRec_ memory{};
    FT_Library library{};
    std::array<FT_StreamRec, face_count> streams{};
    std::array<FT_Face, face_count> faces{};
    /// the pixel size each face was last set to, 0 for none
    std::array<int32_t, face_count> sizes{};
    /// the weight Noto Emoji's axis was last set to
    std::optional<Weight> emoji_weight{};
    std::unordered_map<uint64_t, KeptGlyph> glyphs{};
    /// the keys of the kept glyphs, the one used last first
    std::list<uint64_t> order{};
    /// glyphs drawn since the stack opened
    std::size_t drawn_count{};

    Fonts() = default;
    Fonts(const Fonts&) = delete;
    Fonts& operator=(const Fonts&) = delete;

    ~Fonts() {
        for (FT_Face face : faces)
            if (face != nullptr)
                FT_Done_Face(face);
        if (library != nullptr)
            FT_Done_Library(library);
    }

    /// Sets a face to a pixel size, unless it has it.
    bool set_size(Face which, int32_t pixel_size) {
        const auto index = static_cast<std::size_t>(which);
        if (sizes[index] == pixel_size)
            return true;
        if (FT_Set_Pixel_Sizes(faces[index], 0, static_cast<FT_UInt>(pixel_size)) != 0)
            return false;
        sizes[index] = pixel_size;
        return true;
    }

    /// Sets Noto Emoji's weight axis, unless it has the weight; a font
    /// without the axis keeps its one weight.
    void set_emoji_weight(Weight weight) {
        if (emoji_weight == weight)
            return;
        FT_Face face = faces[static_cast<std::size_t>(Face::noto_emoji)];
        FT_MM_Var* axes = nullptr;
        if (FT_HAS_MULTIPLE_MASTERS(face) && FT_Get_MM_Var(face, &axes) == 0) {
            std::vector<FT_Fixed> coordinates(axes->num_axis);
            for (FT_UInt axis = 0; axis < axes->num_axis; ++axis) {
                const FT_Var_Axis& found = axes->axis[axis];
                coordinates[axis] = found.tag == FT_MAKE_TAG('w', 'g', 'h', 't')
                                        ? std::clamp(
                                              emoji_weights[static_cast<std::size_t>(weight)],
                                              found.minimum,
                                              found.maximum
                                          )
                                        : found.def;
            }
            FT_Set_Var_Design_Coordinates(face, axes->num_axis, coordinates.data());
            FT_Done_MM_Var(library, axes);
            // The face's scaled metrics follow the instance: set the size again.
            sizes[static_cast<std::size_t>(Face::noto_emoji)] = 0;
        }
        emoji_weight = weight;
    }

    /// Returns a glyph drawn at a size, drawing it the first time.
    const Glyph* glyph(Face which, FT_UInt index, int32_t pixel_size, const Style& style) {
        const bool emoji = which == Face::noto_emoji;
        const uint64_t key = (uint64_t{index} << 24) | (static_cast<uint64_t>(pixel_size) << 8) |
                             (static_cast<uint64_t>(which) << 2) |
                             (static_cast<uint64_t>(style.rendering) << 1) |
                             (emoji ? static_cast<uint64_t>(style.weight) : 0U);
        if (const auto found = glyphs.find(key); found != glyphs.end()) {
            order.splice(order.begin(), order, found->second.place);
            return &found->second.glyph;
        }
        if (emoji)
            set_emoji_weight(style.weight);
        if (!set_size(which, pixel_size))
            return nullptr;
        FT_Face face = faces[static_cast<std::size_t>(which)];
        const bool mono = style.rendering == Rendering::mono;
        const FT_Int32 load =
            FT_LOAD_NO_BITMAP | (mono ? FT_LOAD_TARGET_MONO : FT_LOAD_TARGET_NORMAL);
        if (FT_Load_Glyph(face, index, load) != 0 ||
            FT_Render_Glyph(face->glyph, mono ? FT_RENDER_MODE_MONO : FT_RENDER_MODE_NORMAL) != 0)
            return nullptr;
        const FT_GlyphSlot slot = face->glyph;
        const FT_Bitmap& bitmap = slot->bitmap;
        Glyph drawn;
        drawn.left = slot->bitmap_left;
        drawn.top = slot->bitmap_top;
        drawn.width = static_cast<int32_t>(bitmap.width);
        drawn.rows = static_cast<int32_t>(bitmap.rows);
        drawn.advance = whole_pixels(slot->advance.x);
        drawn.alpha.assign(
            static_cast<std::size_t>(drawn.width) * static_cast<std::size_t>(drawn.rows), 0
        );
        for (int32_t row = 0; row < drawn.rows; ++row) {
            const unsigned char* line =
                bitmap.buffer + static_cast<std::ptrdiff_t>(row) * bitmap.pitch;
            for (int32_t column = 0; column < drawn.width; ++column) {
                const auto covered = static_cast<uint8_t>(
                    bitmap.pixel_mode == FT_PIXEL_MODE_MONO
                        ? ((line[column >> 3] & (0x80 >> (column & 7))) != 0 ? 255 : 0)
                        : line[column]
                );
                drawn.alpha[static_cast<std::size_t>(row * drawn.width + column)] = covered;
            }
        }
        ++drawn_count;
        order.push_front(key);
        auto& kept = glyphs.emplace(key, KeptGlyph{std::move(drawn), order.begin()}).first->second;
        return &kept.glyph;
    }

    /// Makes room for a line of a number of characters: while the line could
    /// take the store past kept_glyphs, forgets the glyph used longest ago.
    /// Only this forgets glyphs, so the glyphs one line draws stay in the
    /// store while the line is drawn.
    void make_room(std::size_t characters) {
        while (!order.empty() && glyphs.size() + characters > kept_glyphs) {
            glyphs.erase(order.back());
            order.pop_back();
        }
    }

    /// Finds the face and glyph that draw a character, the chain's first
    /// face's missing-glyph box when no face has it.
    std::pair<Face, FT_UInt> lookup(char32_t character, Weight weight) const {
        const auto find = [&](const auto& chain) {
            for (Face which : chain)
                if (const FT_UInt index =
                        FT_Get_Char_Index(faces[static_cast<std::size_t>(which)], character);
                    index != 0)
                    return std::pair{which, index};
            return std::pair{chain.front(), FT_UInt{0}};
        };
        return weight == Weight::bold ? find(bold_chain) : find(regular_chain);
    }
};

std::string freetype_version() {
    return std::to_string(FREETYPE_MAJOR) + "." + std::to_string(FREETYPE_MINOR) + "." +
           std::to_string(FREETYPE_PATCH);
}

std::span<const Face> fallback_chain(Weight weight) noexcept {
    if (weight == Weight::bold)
        return bold_chain;
    return regular_chain;
}

int32_t related_pixel_size(int32_t pixel_size) noexcept {
    return std::max((pixel_size * 6 + 3) / 7, std::min(pixel_size, 12));
}

std::optional<std::vector<char32_t>> decode_utf8(std::string_view text) {
    std::vector<char32_t> characters;
    characters.reserve(text.size());
    for (std::size_t at = 0; at < text.size();) {
        const auto lead = static_cast<uint8_t>(text[at]);
        std::size_t length = 1;
        char32_t value = lead;
        char32_t least = 0;
        if (lead >= 0xF0 && lead <= 0xF4) {
            length = 4;
            value = lead & 0x07U;
            least = 0x10000;
        } else if (lead >= 0xE0 && lead <= 0xEF) {
            length = 3;
            value = lead & 0x0FU;
            least = 0x800;
        } else if (lead >= 0xC2 && lead <= 0xDF) {
            length = 2;
            value = lead & 0x1FU;
            least = 0x80;
        } else if (lead >= 0x80) {
            return std::nullopt;
        }
        if (text.size() - at < length)
            return std::nullopt;
        for (std::size_t next = 1; next < length; ++next) {
            const auto byte = static_cast<uint8_t>(text[at + next]);
            if ((byte & 0xC0U) != 0x80U)
                return std::nullopt;
            value = (value << 6) | (byte & 0x3FU);
        }
        if (value < least || value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF))
            return std::nullopt;
        characters.push_back(value);
        at += length;
    }
    return characters;
}

bool is_invisible(char32_t character) noexcept {
    return character < 0x20 || (character >= 0x7F && character < 0xA0) ||
           (character >= 0x200B && character <= 0x200F) || character == 0x2060 ||
           character == 0xFEFF || (character >= 0xFE00 && character <= 0xFE0F) ||
           (character >= 0xE0100 && character <= 0xE01EF);
}

FontStack::FontStack(std::unique_ptr<Fonts> fonts) noexcept : fonts_(std::move(fonts)) {
}

FontStack::~FontStack() = default;

std::unique_ptr<FontStack> FontStack::open(const std::filesystem::path& directory) {
    auto fonts = std::make_unique<Fonts>();
    fonts->memory.alloc = &allocate;
    fonts->memory.free = &release;
    fonts->memory.realloc = &reallocate;
    // The library is made without FreeType's default properties, which an
    // environment variable could change: every machine draws alike.
    if (FT_New_Library(&fonts->memory, &fonts->library) != 0)
        return nullptr;
    FT_Add_Default_Modules(fonts->library);
    for (std::size_t index = 0; index < face_count; ++index) {
        std::FILE* file = open_file(directory / std::filesystem::path(face_files[index]));
        if (file == nullptr)
            return nullptr;
        FT_StreamRec& stream = fonts->streams[index];
        stream.descriptor.pointer = file;
        stream.read = &read_file;
        stream.close = &close_file;
        if (std::fseek(file, 0, SEEK_END) != 0 || std::ftell(file) <= 0) {
            close_file(&stream);
            return nullptr;
        }
        stream.size = static_cast<unsigned long>(std::ftell(file));
        FT_Open_Args arguments{};
        arguments.flags = FT_OPEN_STREAM;
        arguments.stream = &stream;
        // FreeType closes the stream itself when the face cannot be made.
        if (FT_Open_Face(fonts->library, &arguments, 0, &fonts->faces[index]) != 0)
            return nullptr;
        if (!FT_IS_SCALABLE(fonts->faces[index]))
            return nullptr;
    }
    return std::unique_ptr<FontStack>(new FontStack(std::move(fonts)));
}

std::optional<Face> FontStack::face_for(char32_t character, Weight weight) const {
    if (is_invisible(character))
        return std::nullopt;
    return fonts_->lookup(character, weight).first;
}

namespace {

/// Tells whether a style is one the stack draws.
bool in_range(const Style& style) noexcept {
    return style.pixel_size >= 1 && style.pixel_size <= max_pixel_size &&
           style.letter_spacing >= 0 && style.letter_spacing <= max_letter_spacing &&
           style.least_cjk_pixel_size >= 0 && style.least_cjk_pixel_size <= max_pixel_size;
}

/// Gives the pixel size a face is drawn at in a style.
int32_t face_pixel_size(Face which, const Style& style) noexcept {
    if (which == Face::dejavu_sans_bold || which == Face::dejavu_sans)
        return style.pixel_size;
    const int32_t related = related_pixel_size(style.pixel_size);
    return which == Face::noto_sans_cjk ? std::max(related, style.least_cjk_pixel_size) : related;
}

} // namespace

std::optional<LineMetrics> FontStack::metrics(const Style& style) {
    if (!in_range(style))
        return std::nullopt;
    LineMetrics line;
    for (std::size_t index = 0; index < face_count; ++index) {
        const auto which = static_cast<Face>(index);
        if (which == Face::dejavu_sans_bold && style.weight == Weight::regular)
            continue;
        if (which == Face::noto_emoji)
            fonts_->set_emoji_weight(style.weight);
        if (!fonts_->set_size(which, face_pixel_size(which, style)))
            return std::nullopt;
        const FT_Size_Metrics& size = fonts_->faces[index]->size->metrics;
        line.ascent = std::max(line.ascent, static_cast<int32_t>((size.ascender + 63) >> 6));
        line.descent = std::max(line.descent, static_cast<int32_t>((-size.descender + 63) >> 6));
    }
    return line;
}

std::optional<FaceMetrics> FontStack::face_metrics(Face face, const Style& style) {
    if (!in_range(style))
        return std::nullopt;
    // The emoji face's scaled rows follow its weight, so the weight is set
    // before the size, as metrics does.
    if (face == Face::noto_emoji)
        fonts_->set_emoji_weight(style.weight);
    const int32_t pixels = face_pixel_size(face, style);
    if (!fonts_->set_size(face, pixels))
        return std::nullopt;
    const FT_Size_Metrics& size = fonts_->faces[static_cast<std::size_t>(face)]->size->metrics;
    return FaceMetrics{
        pixels,
        static_cast<int32_t>((size.ascender + 63) >> 6),
        static_cast<int32_t>((-size.descender + 63) >> 6),
    };
}

std::optional<std::vector<Placement>> FontStack::layout(std::string_view text, const Style& style) {
    if (!in_range(style) || text.size() > max_text_bytes)
        return std::nullopt;
    const auto characters = decode_utf8(text);
    if (!characters)
        return std::nullopt;
    fonts_->make_room(characters->size());
    std::vector<Placement> placements;
    int32_t pen = 0;
    for (char32_t character : *characters) {
        if (is_invisible(character))
            continue;
        const auto [which, index] = fonts_->lookup(character, style.weight);
        const Glyph* drawn = fonts_->glyph(which, index, face_pixel_size(which, style), style);
        if (drawn == nullptr)
            return std::nullopt;
        // A mark that does not move the pen gets no spacing either.
        const int32_t advance = drawn->advance + (drawn->advance > 0 ? style.letter_spacing : 0);
        placements.push_back({character, which, pen, advance});
        pen += advance;
    }
    return placements;
}

std::optional<Coverage> FontStack::draw(std::string_view text, const Style& style) {
    const auto placements = layout(text, style);
    const auto line = metrics(style);
    if (!placements || !line)
        return std::nullopt;
    // The glyphs come again from the store, where layout has just put every
    // one of them; nothing empties it before the next line.
    std::vector<Laid> laid;
    laid.reserve(placements->size());
    int32_t left = 0;
    int32_t right = 0;
    int32_t above = line->ascent;
    int32_t below = line->descent;
    for (const Placement& placement : *placements) {
        const auto [which, index] = fonts_->lookup(placement.character, style.weight);
        const Glyph* drawn = fonts_->glyph(which, index, face_pixel_size(which, style), style);
        if (drawn == nullptr)
            return std::nullopt;
        laid.push_back({placement, drawn});
        right = std::max(right, placement.pen + placement.advance);
        if (drawn->width > 0 && drawn->rows > 0) {
            left = std::min(left, placement.pen + drawn->left);
            right = std::max(right, placement.pen + drawn->left + drawn->width);
            above = std::max(above, drawn->top);
            below = std::max(below, drawn->rows - drawn->top);
        }
    }
    Coverage coverage;
    coverage.origin = -left;
    coverage.width = std::min(right - left, max_line_width);
    coverage.baseline = above;
    coverage.height = above + below;
    coverage.advance = laid.empty() ? 0 : laid.back().placement.pen + laid.back().placement.advance;
    coverage.alpha.assign(
        static_cast<std::size_t>(coverage.width) * static_cast<std::size_t>(coverage.height), 0
    );
    for (const Laid& each : laid) {
        const Glyph& drawn = *each.glyph;
        const int32_t x = coverage.origin + each.placement.pen + drawn.left;
        const int32_t y = coverage.baseline - drawn.top;
        for (int32_t row = 0; row < drawn.rows; ++row)
            for (int32_t column = 0; column < drawn.width; ++column) {
                const int32_t px = x + column;
                if (px < 0 || px >= coverage.width)
                    continue;
                auto& pixel =
                    coverage.alpha[static_cast<std::size_t>((y + row) * coverage.width + px)];
                pixel = std::max(
                    pixel, drawn.alpha[static_cast<std::size_t>(row * drawn.width + column)]
                );
            }
    }
    return coverage;
}

std::size_t FontStack::cached_glyphs() const noexcept {
    return fonts_->glyphs.size();
}

std::size_t FontStack::drawn_glyphs() const noexcept {
    return fonts_->drawn_count;
}

} // namespace oa::platform::text_font
