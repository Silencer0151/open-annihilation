// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The Mods page's row for a mod folder: the oamod.yaml's top-level scalars
// read without resolving the profile, and the oamod.png badge decoded with
// the engine's PNG reader and scaled down to the badge's side.

#include "oa/app/mod_summary.hpp"

#include "oa/app/mod_profile_loader.hpp"
#include "oa/formats/oamod.hpp"
#include "oa/formats/png.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <optional>
#include <system_error>
#include <utility>

namespace oa::app {

namespace {

namespace fs = std::filesystem;
namespace oamod = formats::oamod;
namespace png_format = formats::png;

/// The first byte that is not a C0 control character, and the delete character.
constexpr unsigned char first_printable = 0x20;
constexpr unsigned char delete_character = 0x7F;
/// UTF-8 writes the C1 control characters U+0080 to U+009F as this lead
/// byte followed by 0x80 to 0x9F.
constexpr unsigned char c1_lead = 0xC2;
constexpr unsigned char first_c1_trail = 0x80;
constexpr unsigned char last_c1_trail = 0x9F;
/// UTF-8 writes the line separator U+2028 and the paragraph separator
/// U+2029 as these two bytes followed by 0xA8 and 0xA9.
constexpr unsigned char separator_lead = 0xE2;
constexpr unsigned char separator_middle = 0x80;
constexpr unsigned char line_separator_trail = 0xA8;
constexpr unsigned char paragraph_separator_trail = 0xA9;

/// The largest sample value of 8 bits.
constexpr uint32_t full_sample = 255;
/// The bits of a sample after the transforms the badge is read with.
constexpr uint8_t read_bit_depth = 8;
/// The offsets of a pixel's samples.
constexpr size_t red_sample = 0;
constexpr size_t green_sample = 1;
constexpr size_t blue_sample = 2;
constexpr size_t alpha_sample = 3;

/// Lower-cases the ASCII letters of a file name.
///
/// @param name the name, UTF-8
/// @return the name with A-Z made a-z
std::string folded(std::string name) {
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return name;
}

/// Writes a path as UTF-8.
///
/// @param path the path
/// @return its text
std::string utf8(const fs::path& path) {
    const auto text = path.u8string();
    return {text.begin(), text.end()};
}

/// Makes text one line: every C0 control character, delete, C1 control
/// character, line separator and paragraph separator becomes a space.
///
/// @param text UTF-8 text
/// @return the text on one line
std::string one_line(std::string_view text) {
    std::string line;
    line.reserve(text.size());
    for (size_t at = 0; at < text.size(); ++at) {
        const auto byte = static_cast<unsigned char>(text[at]);
        const auto next = [&](size_t ahead) {
            return at + ahead < text.size() ? static_cast<unsigned char>(text[at + ahead]) : 0;
        };
        if (byte < first_printable || byte == delete_character) {
            line += ' ';
        } else if (byte == c1_lead && next(1) >= first_c1_trail && next(1) <= last_c1_trail) {
            line += ' ';
            at += 1;
        } else if (
            byte == separator_lead && next(1) == separator_middle &&
            (next(2) == line_separator_trail || next(2) == paragraph_separator_trail)
        ) {
            line += ' ';
            at += 2;
        } else {
            line += static_cast<char>(byte);
        }
    }
    return line;
}

/// Returns the text of a top-level string or number of a profile.
///
/// @param root the profile's top-level mapping
/// @param key the key
/// @return its text on one line, or nullopt when it is absent or of another kind
std::optional<std::string> scalar_text(const oamod::Node& root, std::string_view key) {
    const oamod::Node* node = oamod::find_entry(root, key);
    if (node == nullptr ||
        (node->kind != oamod::NodeKind::string && node->kind != oamod::NodeKind::number))
        return std::nullopt;
    return one_line(node->text);
}

/// Reads a regular file of at most a given size.
///
/// @param file the file
/// @param limit the most bytes read
/// @return its bytes, or nullopt when it is not a regular file, is larger,
///         or cannot be read whole
std::optional<std::vector<uint8_t>> read_small_file(const fs::path& file, uintmax_t limit) {
    std::error_code error;
    if (!fs::is_regular_file(file, error) || error)
        return std::nullopt;
    const uintmax_t size = fs::file_size(file, error);
    if (error || size > limit)
        return std::nullopt;
    std::ifstream in{file, std::ios::binary};
    if (!in)
        return std::nullopt;
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (static_cast<uintmax_t>(in.gcount()) != size)
        return std::nullopt;
    return bytes;
}

/// Finds the one entry of a folder with a name, compared without case.
///
/// @param folder the folder
/// @param name the name, lower case
/// @return the entry, or nullopt when there is none, more than one, or the
///         folder cannot be listed
std::optional<fs::path> only_entry_named(const fs::path& folder, std::string_view name) {
    std::optional<fs::path> found;
    size_t count = 0;
    std::error_code error;
    for (fs::directory_iterator entry{folder, error}, end; !error && entry != end;
         entry.increment(error)) {
        if (folded(utf8(entry->path().filename())) == name) {
            found = entry->path();
            ++count;
        }
    }
    if (error || count != 1)
        return std::nullopt;
    return found;
}

/// Notes that the PNG reader reported an error; warnings are left out.
///
/// @param user the bool that records it
void note_error(void* user, const char*) {
    *static_cast<bool*>(user) = true;
}

/// Ignores a warning of the PNG reader.
void ignore_warning(void*, const char*) {
}

/// Scales a sample of fewer than 8 bits to the full 8-bit range.
///
/// @param value the sample
/// @param bit_depth its bits: 1, 2, 4 or 8
/// @return the sample from 0 to 255
uint8_t full_range(uint8_t value, uint8_t bit_depth) {
    if (bit_depth >= read_bit_depth)
        return value;
    const uint32_t largest = (uint32_t{1} << bit_depth) - 1;
    return static_cast<uint8_t>(std::min<uint32_t>(value, largest) * full_sample / largest);
}

/// Converts decoded rows to RGBA.
///
/// @param info what read_info gave: the header and palette
/// @param rows the rows read with strip_16 and unpack, one byte a sample
/// @param row the bytes of one row
/// @return the badge at the image's own size
ModBadge to_rgba(const png_format::Info& info, std::span<const uint8_t> rows, size_t row) {
    const png_format::Header& header = info.header;
    const uint32_t channels = png_format::channel_count(header.color_type);
    ModBadge badge;
    badge.width = header.width;
    badge.height = header.height;
    badge.pixels.resize(size_t{header.width} * header.height * mod_badge_pixel_bytes);
    for (uint32_t y = 0; y < header.height; ++y) {
        for (uint32_t x = 0; x < header.width; ++x) {
            const uint8_t* sample = rows.data() + size_t{y} * row + size_t{x} * channels;
            uint8_t* pixel =
                badge.pixels.data() + (size_t{y} * header.width + x) * mod_badge_pixel_bytes;
            switch (header.color_type) {
            case png_format::ColorType::grey:
            case png_format::ColorType::grey_alpha: {
                const uint8_t grey = full_range(sample[0], header.bit_depth);
                pixel[red_sample] = grey;
                pixel[green_sample] = grey;
                pixel[blue_sample] = grey;
                pixel[alpha_sample] = header.color_type == png_format::ColorType::grey_alpha
                                          ? sample[1]
                                          : static_cast<uint8_t>(full_sample);
                break;
            }
            case png_format::ColorType::palette: {
                // An index past the palette is drawn black.
                png_format::Rgb colour{};
                if (sample[0] < info.palette_entries)
                    colour = info.palette[sample[0]];
                pixel[red_sample] = colour.r;
                pixel[green_sample] = colour.g;
                pixel[blue_sample] = colour.b;
                pixel[alpha_sample] = static_cast<uint8_t>(full_sample);
                break;
            }
            case png_format::ColorType::rgb:
            case png_format::ColorType::rgb_alpha:
                pixel[red_sample] = sample[0];
                pixel[green_sample] = sample[1];
                pixel[blue_sample] = sample[2];
                pixel[alpha_sample] = header.color_type == png_format::ColorType::rgb_alpha
                                          ? sample[3]
                                          : static_cast<uint8_t>(full_sample);
                break;
            }
        }
    }
    return badge;
}

/// Scales a badge down to fit within mod_badge_side, keeping its proportions.
///
/// Each pixel is the average of the source pixels it covers, its colour
/// weighted by their alpha so that transparent pixels lend it no colour.
///
/// @param badge a badge wider or taller than mod_badge_side
/// @return the badge, at most mod_badge_side each way and at least 1
ModBadge scaled_down(const ModBadge& badge) {
    const uint32_t longest = std::max(badge.width, badge.height);
    ModBadge scaled;
    scaled.width = std::max<uint32_t>(
        1, static_cast<uint32_t>((uint64_t{badge.width} * mod_badge_side + longest / 2) / longest)
    );
    scaled.height = std::max<uint32_t>(
        1, static_cast<uint32_t>((uint64_t{badge.height} * mod_badge_side + longest / 2) / longest)
    );
    scaled.pixels.resize(size_t{scaled.width} * scaled.height * mod_badge_pixel_bytes);
    for (uint32_t y = 0; y < scaled.height; ++y) {
        const uint32_t top = static_cast<uint32_t>(uint64_t{y} * badge.height / scaled.height);
        const uint32_t bottom = std::max(
            top + 1, static_cast<uint32_t>(uint64_t{y + 1} * badge.height / scaled.height)
        );
        for (uint32_t x = 0; x < scaled.width; ++x) {
            const uint32_t left = static_cast<uint32_t>(uint64_t{x} * badge.width / scaled.width);
            const uint32_t right = std::max(
                left + 1, static_cast<uint32_t>(uint64_t{x + 1} * badge.width / scaled.width)
            );
            uint64_t red = 0;
            uint64_t green = 0;
            uint64_t blue = 0;
            uint64_t alpha = 0;
            for (uint32_t source_y = top; source_y < bottom; ++source_y) {
                for (uint32_t source_x = left; source_x < right; ++source_x) {
                    const uint8_t* source =
                        badge.pixels.data() +
                        (size_t{source_y} * badge.width + source_x) * mod_badge_pixel_bytes;
                    const uint64_t weight = source[alpha_sample];
                    red += source[red_sample] * weight;
                    green += source[green_sample] * weight;
                    blue += source[blue_sample] * weight;
                    alpha += weight;
                }
            }
            const uint64_t covered = uint64_t{right - left} * (bottom - top);
            uint8_t* pixel =
                scaled.pixels.data() + (size_t{y} * scaled.width + x) * mod_badge_pixel_bytes;
            if (alpha > 0) {
                pixel[red_sample] = static_cast<uint8_t>((red + alpha / 2) / alpha);
                pixel[green_sample] = static_cast<uint8_t>((green + alpha / 2) / alpha);
                pixel[blue_sample] = static_cast<uint8_t>((blue + alpha / 2) / alpha);
            }
            pixel[alpha_sample] = static_cast<uint8_t>((alpha + covered / 2) / covered);
        }
    }
    return scaled;
}

} // namespace

ModBadge read_mod_badge(std::span<const uint8_t> png) {
    if (png.size() > max_mod_badge_bytes || !png_format::has_signature(png))
        return {};
    bool failed = false;
    const png_format::Messages messages{&failed, ignore_warning, note_error};
    png_format::Info info;
    if (!png_format::read_info(png, messages, &info) || failed)
        return {};
    const png_format::Header header = png_format::header_of(info, messages);
    if (header.width == 0 || header.height == 0 || header.width > max_mod_badge_dimension ||
        header.height > max_mod_badge_dimension)
        return {};
    if (header.color_type == png_format::ColorType::palette && !info.has_palette)
        return {};
    const png_format::Transforms transforms{true, true};
    const size_t row = png_format::row_bytes(header, transforms);
    if (row != size_t{header.width} * png_format::channel_count(header.color_type))
        return {};
    std::vector<uint8_t> rows(row * header.height);
    const png_format::Progress progress =
        png_format::read_image(png, info, transforms, messages, rows);
    if (progress == png_format::Progress::none || failed)
        return {};
    ModBadge badge = to_rgba(info, rows, row);
    if (badge.width > mod_badge_side || badge.height > mod_badge_side)
        badge = scaled_down(badge);
    return badge;
}

ModSummary read_mod_summary(const fs::path& folder) {
    ModSummary summary;
    fs::path name = folder.filename();
    if (name.empty())
        name = folder.parent_path().filename();
    summary.title = one_line(utf8(name));
    summary.version = mod_summary_no_version;

    std::error_code listing;
    [[maybe_unused]] const fs::directory_iterator listed{folder, listing};
    if (listing) {
        summary.description = mod_summary_unreadable_folder;
        return summary;
    }
    std::string error;
    const std::optional<fs::path> profile = find_mod_profile(folder, error);
    if (!profile && error.empty()) {
        summary.description = mod_summary_no_profile;
    } else {
        summary.has_profile = true;
        oamod::Node root;
        oamod::ReadError read_error;
        // One byte past the limit is enough for the reader to refuse a file too large.
        const auto bytes =
            profile ? read_small_file(*profile, oamod::max_input_bytes + 1) : std::nullopt;
        if (!bytes || !oamod::read_document(*bytes, root, read_error)) {
            summary.description = mod_summary_unreadable_profile;
        } else {
            if (auto title = scalar_text(root, "name"); title && !title->empty())
                summary.title = std::move(*title);
            if (auto version = scalar_text(root, "version"); version && !version->empty())
                summary.version = std::move(*version);
            if (auto description = scalar_text(root, "description"))
                summary.description = std::move(*description);
        }
    }

    if (const auto badge_file = only_entry_named(folder, mod_badge_name))
        if (const auto bytes = read_small_file(*badge_file, max_mod_badge_bytes))
            summary.badge = read_mod_badge(*bytes);
    return summary;
}

} // namespace oa::app
