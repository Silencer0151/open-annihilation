// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/formats/fnt.hpp"
#include "oa/formats/gaf.hpp"
#include <algorithm>
#include <bit>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>

namespace oa::formats::fnt {
namespace {
constexpr std::size_t fnt_header_bytes = file_header_bytes;
constexpr std::size_t fnt_data_start = file_preamble_bytes;

struct FileHeader {
    uint16_t height = 0;
    uint16_t word_after_height = 0; // kept as read; nothing reads it
    std::array<uint16_t, limit::glyph_count> offsets{};
};

constexpr uint8_t first_printable = 0x20;
constexpr uint8_t height_reference = 0x49;

uint16_t le16(std::span<const uint8_t> b, std::size_t o) {
    if (o > b.size() || b.size() - o < 2)
        throw std::runtime_error("truncated FNT uint16");
    return static_cast<uint16_t>(b[o]) |
           static_cast<uint16_t>(static_cast<uint16_t>(b[o + 1]) << 8U);
}

void validate(std::span<const uint8_t> b, std::string_view kind) {
    if (b.size() > limit::input_bytes)
        throw std::runtime_error(std::string(kind) + " font exceeds the 4 MiB safety limit");
}
} // namespace

Font parse_fnt(std::span<const uint8_t> b) {
    validate(b, "FNT");
    if (b.size() < fnt_data_start)
        throw std::runtime_error("FNT is shorter than its 516-byte header");
    FileHeader disk;
    disk.height = le16(b, 0);
    disk.word_after_height = le16(b, 2);
    for (std::size_t c = 0; c < limit::glyph_count; ++c)
        disk.offsets[c] = le16(b, fnt_header_bytes + c * sizeof(uint16_t));
    Font out;
    out.nominal_height = disk.height;
    out.word_after_height = disk.word_after_height;
    if (out.nominal_height == 0 || out.nominal_height > limit::glyph_height)
        throw std::runtime_error("FNT glyph height is outside 1..128");
    for (std::size_t c = 0; c < limit::glyph_count; ++c) {
        const std::size_t offset = disk.offsets[c];
        if (offset == 0)
            continue;
        if (offset < fnt_data_start || offset >= b.size())
            throw std::runtime_error("FNT glyph offset lies outside glyph data");
        const auto width = b[offset];
        if (width == 0 || width > limit::glyph_width)
            throw std::runtime_error("FNT glyph width is outside 1..128");
        const std::size_t count = static_cast<std::size_t>(width) * out.nominal_height;
        const std::size_t packed = (count + 7U) / 8U;
        if (offset + 1 > b.size() || packed > b.size() - offset - 1)
            throw std::runtime_error("truncated FNT glyph bitmap");
        Glyph glyph;
        glyph.width = width;
        glyph.height = out.nominal_height;
        // FNT is a monochrome mask. Index 255 is the explicit palette-raster
        // foreground; callers can remap it to the current GUI palette.
        glyph.pixels.assign(count, foreground_index);
        glyph.coverage.resize(count);
        for (std::size_t bit = 0; bit < count; ++bit)
            glyph.coverage[bit] =
                static_cast<uint8_t>((b[offset + 1 + bit / 8U] >> (7U - bit % 8U)) & 1U);
        out.glyphs[c] = std::move(glyph);
    }
    return out;
}

Font parse_gaf(std::span<const uint8_t> b) {
    validate(b, "GAF");
    const auto parsed = formats::gaf::parse(b);
    if (!parsed.ok())
        throw std::runtime_error("cannot parse GAF font: " + parsed.error->message);
    if (parsed.archive->sequences.empty())
        throw std::runtime_error("GAF font contains no sequence");
    const auto& frames = parsed.archive->sequences.front().frames;
    if (frames.size() > limit::glyph_count)
        throw std::runtime_error("GAF font has more than 256 glyph frames");
    Font out;
    for (std::size_t c = 0; c < frames.size(); ++c) {
        const auto rendered = formats::gaf::render_normal(frames[c]);
        if (!rendered.ok())
            throw std::runtime_error(
                "cannot render GAF font glyph " + std::to_string(c) + ": " + rendered.error->message
            );
        const auto& f = *rendered.frame;
        if (f.width > limit::glyph_width || f.height > limit::glyph_height)
            throw std::runtime_error("GAF font glyph dimensions exceed 128 pixels");
        out.glyphs[c] = Glyph{f.width, f.height, f.origin_x, f.origin_y, f.pixels, f.coverage};
    }
    if (out.glyphs[height_reference]) {
        out.nominal_height = out.glyphs[height_reference]->height;
        // The game normalizes every GUI-font frame after loading: it reads
        // frame 0x49's height, then subtracts that height from each frame's
        // signed origin_y field. Raw hattfont12 has origin_y=11,height=12;
        // the active GUI font therefore draws with origin_y=-1, placing its
        // bitmap one pixel below the centred pen coordinate the GUI passes.
        for (auto& glyph : out.glyphs) {
            if (!glyph)
                continue;
            const auto wrapped =
                static_cast<uint16_t>(static_cast<int32_t>(glyph->origin_y) - out.nominal_height);
            glyph->origin_y = std::bit_cast<int16_t>(wrapped);
        }
    }
    return out;
}

Font load_fnt(AssetStore& a, std::string_view p) {
    return parse_fnt(a.read(p).bytes);
}

Font load_gaf(AssetStore& a, std::string_view p) {
    return parse_gaf(a.read(p).bytes);
}

namespace {
constexpr std::string_view disk_font_extension = "FNT";
// Longest font path the game accepts, in bytes with the terminator.
constexpr std::size_t disk_font_path_bytes = 0x100;

/// Builds directory\name with its last dotted suffix replaced by ".FNT".
std::string disk_font_path(std::string_view directory, std::string_view name) {
    std::string path;
    path.reserve(directory.size() + name.size() + disk_font_extension.size() + 2);
    path.append(directory);
    path.push_back('\\');
    path.append(name);
    // The suffix dropped starts at the path's last dot, wherever it is. A dot
    // in the language directory counts: "fonts-en.gb\\smlfont" becomes
    // "fonts-en.FNT".
    if (const auto dot = path.rfind('.'); dot != std::string::npos)
        path.resize(dot);
    path.push_back('.');
    path.append(disk_font_extension);
    if (path.size() >= disk_font_path_bytes)
        throw std::runtime_error("font path exceeds the 256-byte font path limit");
    return path;
}

struct LocatedFont {
    bool found = false;
    std::vector<uint8_t> bytes;
};

LocatedFont read_opened_font(AssetStore& assets, const std::string& path) {
    try {
        auto data = assets.read(path);
        return {true, std::move(data.bytes)};
    } catch (const std::runtime_error& error) {
        // The game treats a failed open as "absent". Other failures stay fatal.
        if (std::string_view(error.what()).starts_with("asset not found:"))
            return {};
        throw;
    }
}

Font font_from_opened_file(const LocatedFont& located, const std::string& path) {
    // An empty or missing font file is fatal; no other path is tried.
    if (!located.found || located.bytes.empty())
        throw std::runtime_error("missing font file: " + path);
    return parse_fnt(located.bytes);
}
} // namespace

Font load_named_fnt(AssetStore& assets, std::string_view name, std::string_view language) {
    // The language comes from the game's language setting. The alternate path
    // is fonts-<language>\<name>, kept only when it opens.
    if (!language.empty()) {
        const auto alternate = disk_font_path(std::string("fonts-") + std::string(language), name);
        const auto located = read_opened_font(assets, alternate);
        if (located.found)
            return font_from_opened_file(located, alternate);
    }
    const auto fallback = disk_font_path("fonts", name);
    return font_from_opened_file(read_opened_font(assets, fallback), fallback);
}

uint32_t measure_text(const Font& f, std::string_view text) noexcept {
    uint32_t width = 0;
    for (const unsigned char c : text) {
        if (c < first_printable || !f.glyphs[c])
            continue;
        const auto add = f.glyphs[c]->width;
        width = add > std::numeric_limits<uint32_t>::max() - width
                    ? std::numeric_limits<uint32_t>::max()
                    : width + add;
    }
    return width;
}

uint16_t line_height(const Font& f) noexcept {
    const auto& g = f.glyphs[height_reference];
    const auto height = g ? g->height : f.nominal_height;
    return static_cast<uint16_t>(height + 2U);
}

int32_t
raster_text(IndexedSurface dst, const Font& f, std::string_view text, int32_t x, int32_t y) {
    if (dst.stride < dst.width || (dst.height != 0 && dst.stride > dst.pixels.size() / dst.height))
        throw std::runtime_error("indexed destination has inconsistent bounds");
    if (!dst.coverage.empty() && (dst.height != 0 && dst.stride > dst.coverage.size() / dst.height))
        throw std::runtime_error("indexed destination coverage has inconsistent bounds");
    for (const unsigned char c : text) {
        if (c < first_printable || !f.glyphs[c])
            continue;
        const auto& g = *f.glyphs[c];
        const auto count = static_cast<std::size_t>(g.width) * g.height;
        if (g.pixels.size() != count || g.coverage.size() != count)
            throw std::runtime_error("font glyph has inconsistent buffers");
        if (c != first_printable) {
            const auto left = static_cast<int64_t>(x) - g.origin_x;
            const auto top = static_cast<int64_t>(y) - g.origin_y;
            for (uint32_t row = 0; row < g.height; ++row)
                for (uint32_t col = 0; col < g.width; ++col) {
                    const auto source = static_cast<std::size_t>(row) * g.width + col;
                    const auto dx = left + col;
                    const auto dy = top + row;
                    if (!g.coverage[source] || dx < 0 || dy < 0 ||
                        dx >= static_cast<int32_t>(dst.width) ||
                        dy >= static_cast<int32_t>(dst.height))
                        continue;
                    const auto target =
                        static_cast<std::size_t>(dy) * dst.stride + static_cast<std::size_t>(dx);
                    dst.pixels[target] = g.pixels[source];
                    if (!dst.coverage.empty())
                        dst.coverage[target] = 1;
                }
        }
        const auto next = static_cast<int64_t>(x) + g.width;
        x = static_cast<int32_t>(std::clamp<int64_t>(
            next, std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::max()
        ));
    }
    return x;
}
} // namespace oa::formats::fnt
