// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The Mods page's rows over scratch folders: a profile's name, version and
// description read without resolving it, a folder without a profile, with a
// damaged one or one that cannot be listed; and badges of every colour type,
// scaled down to the badge's side, and refused when damaged, cut short, too
// large or not a file.

#include "oa/app/mod_summary.hpp"
#include "oa/formats/oamod.hpp"
#include "oa/formats/png.hpp"
#include "oa/test/check.hpp"
#include "oa/test/scratch_directory.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

namespace fs = std::filesystem;
namespace png = oa::formats::png;
using namespace oa::app;

/// A profile of a made-up mod with every field the row shows.
constexpr std::string_view full_profile = "oamod: 1\n"
                                          "id: glacier-front\n"
                                          "name: Glacier Front\n"
                                          "version: \"3.2\"\n"
                                          "description: \"Cold maps and slow, heavy armies.\"\n"
                                          "requires: {base: ta-3.1c, catalogue: 1}\n"
                                          "author: {name: unknown}\n"
                                          "packaging: {revision: 1, date: 2026-10-04, "
                                          "packager: Open Annihilation}\n";

/// The offset of an IHDR chunk's width in a PNG file: the signature, the
/// chunk's length and its type.
constexpr size_t ihdr_width_offset = 16;

/// Writes a file.
///
/// @param path the file
/// @param bytes its contents
void write(const fs::path& path, std::string_view bytes) {
    std::ofstream out{path, std::ios::binary};
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

/// Writes a file.
///
/// @param path the file
/// @param bytes its contents
void write(const fs::path& path, const std::vector<uint8_t>& bytes) {
    write(path, std::string_view{reinterpret_cast<const char*>(bytes.data()), bytes.size()});
}

/// Encodes an image as a PNG file.
///
/// @param header the image's header
/// @param rows its packed rows, png::row_bytes(header, {}) bytes each
/// @param palette its palette, for a palette image
/// @return the file's bytes
std::vector<uint8_t> encoded(
    const png::Header& header,
    const std::vector<uint8_t>& rows,
    std::span<const png::Rgb> palette = {}
) {
    std::vector<uint8_t> file;
    OA_CHECK(png::write(png::Image{header, palette, rows}, &file));
    return file;
}

/// Makes an 8-bit RGBA image whose pixels follow a rule.
///
/// @param side its width and height
/// @param colour gives the red, green, blue and alpha of each pixel
/// @return the PNG file
template <typename Colour>
std::vector<uint8_t> rgba_image(uint32_t side, Colour colour) {
    const png::Header header{side, side, 8, png::ColorType::rgb_alpha, png::Interlace::none};
    std::vector<uint8_t> rows;
    for (uint32_t y = 0; y < side; ++y)
        for (uint32_t x = 0; x < side; ++x)
            for (const uint8_t sample : colour(x, y))
                rows.push_back(sample);
    return encoded(header, rows);
}

/// Returns a pixel of a badge.
///
/// @param badge the badge
/// @param x the column
/// @param y the row
/// @return its red, green, blue and alpha
std::array<uint8_t, 4> pixel_at(const ModBadge& badge, uint32_t x, uint32_t y) {
    const size_t at = (size_t{y} * badge.width + x) * mod_badge_pixel_bytes;
    return {badge.pixels[at], badge.pixels[at + 1], badge.pixels[at + 2], badge.pixels[at + 3]};
}

/// Tells whether a badge is empty: no size and no pixels.
///
/// @param badge the badge
/// @return true when it is
bool empty(const ModBadge& badge) {
    return badge.width == 0 && badge.height == 0 && badge.pixels.empty();
}

/// Tells whether a badge's size and pixels agree.
///
/// @param badge the badge
/// @return true when it is empty or whole and within the badge's side
bool well_formed(const ModBadge& badge) {
    if (empty(badge))
        return true;
    return badge.width >= 1 && badge.height >= 1 && badge.width <= mod_badge_side &&
           badge.height <= mod_badge_side &&
           badge.pixels.size() == size_t{badge.width} * badge.height * mod_badge_pixel_bytes;
}

void a_profile_gives_its_name_version_and_description(const fs::path& scratch) {
    const fs::path folder = scratch / "glacier";
    fs::create_directories(folder);
    write(folder / "oamod.yaml", full_profile);
    write(folder / "oamod.png", rgba_image(64, [](uint32_t x, uint32_t y) {
              return std::array<uint8_t, 4>{
                  static_cast<uint8_t>(x * 4), static_cast<uint8_t>(y * 4), 7, 200
              };
          }));
    const ModSummary summary = read_mod_summary(folder);
    OA_CHECK(summary.title == "Glacier Front");
    OA_CHECK(summary.version == "3.2");
    OA_CHECK(summary.description == "Cold maps and slow, heavy armies.");
    OA_CHECK(summary.has_profile);
    OA_CHECK(summary.badge.width == 64 && summary.badge.height == 64);
    OA_CHECK(well_formed(summary.badge));
    if (summary.badge.width == 64) {
        OA_CHECK((pixel_at(summary.badge, 0, 0) == std::array<uint8_t, 4>{0, 0, 7, 200}));
        OA_CHECK((pixel_at(summary.badge, 63, 10) == std::array<uint8_t, 4>{252, 40, 7, 200}));
    }

    // The profile is not resolved: a profile the resolver would refuse still
    // gives its row, and a version written as a number gives its text.
    const fs::path loose = scratch / "loose";
    fs::create_directories(loose);
    write(
        loose / "OAMOD.YAML",
        "name: Loose Rules\nversion: 2.10\nhacks: {no.such-hack: true}\n"
        "description: \"First line\\nsecond\\tline\"\n"
    );
    const ModSummary refused = read_mod_summary(loose);
    OA_CHECK(refused.title == "Loose Rules");
    OA_CHECK(refused.version == "2.10");
    OA_CHECK(refused.description == "First line second line");
    OA_CHECK(refused.has_profile);
    OA_CHECK(empty(refused.badge));
}

void a_profile_without_a_description_or_name(const fs::path& scratch) {
    const fs::path folder = scratch / "plain";
    fs::create_directories(folder);
    write(folder / "oamod.yaml", "oamod: 1\nid: plain\nname: Plain Mod\nversion: \"1.0\"\n");
    const ModSummary summary = read_mod_summary(folder);
    OA_CHECK(summary.title == "Plain Mod" && summary.version == "1.0");
    OA_CHECK(summary.description.empty() && summary.has_profile);

    // Without a name or version, the folder's name and N/A.
    const fs::path nameless = scratch / "Nameless Folder";
    fs::create_directories(nameless);
    write(nameless / "oamod.yaml", "oamod: 1\nname: [not, a, name]\ndescription: 12\n");
    const ModSummary unnamed = read_mod_summary(nameless / "");
    OA_CHECK(unnamed.title == "Nameless Folder");
    OA_CHECK(unnamed.version == mod_summary_no_version);
    OA_CHECK(unnamed.description == "12");
    OA_CHECK(unnamed.has_profile);
}

void a_folder_without_a_profile(const fs::path& scratch) {
    const fs::path folder = scratch / "Extra Maps";
    fs::create_directories(folder);
    write(folder / "readme.txt", "maps");
    // A badge is read whether or not a profile sits beside it.
    write(folder / "OaMod.Png", rgba_image(8, [](uint32_t, uint32_t) {
              return std::array<uint8_t, 4>{1, 2, 3, 255};
          }));
    const ModSummary summary = read_mod_summary(folder);
    OA_CHECK(summary.title == "Extra Maps");
    OA_CHECK(summary.version == "N/A");
    OA_CHECK(summary.description == "No oamod.yaml present");
    OA_CHECK(!summary.has_profile);
    OA_CHECK(summary.badge.width == 8 && summary.badge.height == 8 && well_formed(summary.badge));

    // A folder that cannot be listed still gives a row.
    const ModSummary missing = read_mod_summary(scratch / "Gone");
    OA_CHECK(missing.title == "Gone" && missing.version == "N/A");
    OA_CHECK(missing.description == mod_summary_unreadable_folder);
    OA_CHECK(!missing.has_profile && empty(missing.badge));
}

void a_damaged_profile(const fs::path& scratch) {
    for (const std::string_view text :
         {std::string_view{"name: [unclosed\n"},
          std::string_view{"name: A\nname: B\n"},
          std::string_view{"\xEF\xBB\xBFname: Marked\n"},
          std::string_view{"- a list\n"},
          std::string_view{"name: \xFF\xFE\n"},
          std::string_view{""}}) {
        const fs::path folder = scratch / "damaged";
        fs::create_directories(folder);
        write(folder / "oamod.yaml", text);
        const ModSummary summary = read_mod_summary(folder);
        OA_CHECK(summary.title == "damaged");
        OA_CHECK(summary.version == "N/A");
        OA_CHECK(summary.description == mod_summary_unreadable_profile);
        OA_CHECK(summary.has_profile);
        fs::remove_all(folder);
    }

    // Larger than any profile.
    const fs::path large = scratch / "large";
    fs::create_directories(large);
    write(large / "oamod.yaml", std::string(oa::formats::oamod::max_input_bytes + 10, '#'));
    const ModSummary too_large = read_mod_summary(large);
    OA_CHECK(too_large.description == mod_summary_unreadable_profile && too_large.has_profile);

    // A folder named like the profile is no profile file.
    const fs::path directory = scratch / "directory";
    fs::create_directories(directory / "oamod.yaml");
    const ModSummary not_a_file = read_mod_summary(directory);
    OA_CHECK(not_a_file.description == mod_summary_unreadable_profile && not_a_file.has_profile);
}

void a_large_badge_is_scaled_down() {
    // 128 by 128 RGB, red on the left and blue on the right, scales to 64 by 64.
    const png::Header header{128, 128, 8, png::ColorType::rgb, png::Interlace::none};
    std::vector<uint8_t> rows;
    for (uint32_t y = 0; y < 128; ++y)
        for (uint32_t x = 0; x < 128; ++x) {
            rows.push_back(x < 64 ? 255 : 0);
            rows.push_back(0);
            rows.push_back(x < 64 ? 0 : 255);
        }
    const ModBadge badge = read_mod_badge(encoded(header, rows));
    OA_CHECK(badge.width == mod_badge_side && badge.height == mod_badge_side);
    OA_CHECK(well_formed(badge));
    if (badge.width == mod_badge_side) {
        OA_CHECK((pixel_at(badge, 0, 0) == std::array<uint8_t, 4>{255, 0, 0, 255}));
        OA_CHECK((pixel_at(badge, 31, 63) == std::array<uint8_t, 4>{255, 0, 0, 255}));
        OA_CHECK((pixel_at(badge, 32, 0) == std::array<uint8_t, 4>{0, 0, 255, 255}));
    }

    // 200 by 100 keeps its proportions; transparent pixels lend no colour.
    const png::Header wide{200, 100, 8, png::ColorType::rgb_alpha, png::Interlace::none};
    std::vector<uint8_t> wide_rows;
    for (uint32_t y = 0; y < 100; ++y)
        for (uint32_t x = 0; x < 200; ++x) {
            const bool opaque = (x + y) % 2 == 0;
            wide_rows.push_back(opaque ? 10 : 250);
            wide_rows.push_back(opaque ? 20 : 250);
            wide_rows.push_back(opaque ? 30 : 250);
            wide_rows.push_back(opaque ? 255 : 0);
        }
    const ModBadge halved = read_mod_badge(encoded(wide, wide_rows));
    OA_CHECK(halved.width == 64 && halved.height == 32 && well_formed(halved));
    if (halved.width == 64) {
        const auto pixel = pixel_at(halved, 20, 20);
        OA_CHECK(pixel[0] == 10 && pixel[1] == 20 && pixel[2] == 30);
        OA_CHECK(pixel[3] > 64 && pixel[3] < 192);
    }

    // The largest badge read, 256 by 256, interlaced.
    const png::Header largest{256, 256, 8, png::ColorType::grey, png::Interlace::adam7};
    const ModBadge interlaced =
        read_mod_badge(encoded(largest, std::vector<uint8_t>(size_t{256} * 256, 90)));
    OA_CHECK(interlaced.width == 64 && interlaced.height == 64 && well_formed(interlaced));
    if (interlaced.width == 64)
        OA_CHECK((pixel_at(interlaced, 40, 50) == std::array<uint8_t, 4>{90, 90, 90, 255}));
}

void every_colour_type_reads_as_rgba() {
    // A 4-bit palette badge: two pixels a byte, the high nibble first.
    const std::array<png::Rgb, 3> palette{
        png::Rgb{200, 0, 0}, png::Rgb{0, 150, 0}, png::Rgb{1, 2, 3}
    };
    const png::Header indexed{4, 2, 4, png::ColorType::palette, png::Interlace::none};
    const std::vector<uint8_t> indices{0x01, 0x20, 0x22, 0x10};
    const ModBadge paletted = read_mod_badge(encoded(indexed, indices, palette));
    OA_CHECK(paletted.width == 4 && paletted.height == 2 && well_formed(paletted));
    if (paletted.width == 4) {
        OA_CHECK((pixel_at(paletted, 0, 0) == std::array<uint8_t, 4>{200, 0, 0, 255}));
        OA_CHECK((pixel_at(paletted, 1, 0) == std::array<uint8_t, 4>{0, 150, 0, 255}));
        OA_CHECK((pixel_at(paletted, 2, 0) == std::array<uint8_t, 4>{1, 2, 3, 255}));
        OA_CHECK((pixel_at(paletted, 2, 1) == std::array<uint8_t, 4>{0, 150, 0, 255}));
    }

    // 1-bit grey scales to 0 and 255.
    const png::Header bits{8, 1, 1, png::ColorType::grey, png::Interlace::none};
    const ModBadge grey = read_mod_badge(encoded(bits, {0b10100000}));
    OA_CHECK(grey.width == 8 && grey.height == 1 && well_formed(grey));
    if (grey.width == 8) {
        OA_CHECK((pixel_at(grey, 0, 0) == std::array<uint8_t, 4>{255, 255, 255, 255}));
        OA_CHECK((pixel_at(grey, 1, 0) == std::array<uint8_t, 4>{0, 0, 0, 255}));
    }

    // 16-bit grey with alpha keeps each sample's high byte.
    const png::Header deep{1, 1, 16, png::ColorType::grey_alpha, png::Interlace::none};
    const ModBadge grey_alpha = read_mod_badge(encoded(deep, {0x12, 0x34, 0x80, 0x01}));
    OA_CHECK(grey_alpha.width == 1 && well_formed(grey_alpha));
    if (grey_alpha.width == 1)
        OA_CHECK((pixel_at(grey_alpha, 0, 0) == std::array<uint8_t, 4>{0x12, 0x12, 0x12, 0x80}));

    // 16-bit RGB.
    const png::Header wide_rgb{1, 1, 16, png::ColorType::rgb, png::Interlace::none};
    const ModBadge rgb = read_mod_badge(encoded(wide_rgb, {0xAB, 0, 0xCD, 0, 0xEF, 0}));
    OA_CHECK(rgb.width == 1 && well_formed(rgb));
    if (rgb.width == 1)
        OA_CHECK((pixel_at(rgb, 0, 0) == std::array<uint8_t, 4>{0xAB, 0xCD, 0xEF, 255}));
}

void a_bad_badge_gives_none(const fs::path& scratch) {
    const std::vector<uint8_t> good = rgba_image(16, [](uint32_t x, uint32_t y) {
        return std::array<uint8_t, 4>{
            static_cast<uint8_t>(x * 16), static_cast<uint8_t>(y * 16), 99, 255
        };
    });
    OA_CHECK(!empty(read_mod_badge(good)));

    // Nothing, the signature alone, and text.
    OA_CHECK(empty(read_mod_badge({})));
    OA_CHECK(empty(read_mod_badge(std::span<const uint8_t>{good.data(), png::k_signature_bytes})));
    const std::string_view text = "not a picture at all";
    OA_CHECK(empty(read_mod_badge(
        std::span<const uint8_t>{reinterpret_cast<const uint8_t*>(text.data()), text.size()}
    )));

    // Cut short at every length: no badge, never a fault.
    for (size_t length = 0; length < good.size(); ++length) {
        const ModBadge cut = read_mod_badge(std::span<const uint8_t>{good.data(), length});
        OA_CHECK(well_formed(cut));
    }
    OA_CHECK(
        empty(read_mod_badge(std::span<const uint8_t>{good.data(), good.size() - good.size() / 3}))
    );

    // Every byte after the signature changed in turn: a damaged chunk is
    // refused or read whole, never a fault.
    for (size_t at = png::k_signature_bytes; at < good.size(); ++at) {
        std::vector<uint8_t> damaged = good;
        damaged[at] ^= 0x5A;
        OA_CHECK(well_formed(read_mod_badge(damaged)));
    }

    // A width or height of zero, one past the largest read, and one far
    // past any file's size.
    for (const uint32_t width : {0U, max_mod_badge_dimension + 1, 0x7FFFFFFFU, 0xFFFFFFFFU}) {
        std::vector<uint8_t> sized = good;
        sized[ihdr_width_offset] = static_cast<uint8_t>(width >> 24);
        sized[ihdr_width_offset + 1] = static_cast<uint8_t>(width >> 16);
        sized[ihdr_width_offset + 2] = static_cast<uint8_t>(width >> 8);
        sized[ihdr_width_offset + 3] = static_cast<uint8_t>(width);
        OA_CHECK(empty(read_mod_badge(sized)));
    }

    // Larger than the largest badge read, though a whole PNG.
    const png::Header oversize{257, 1, 8, png::ColorType::grey, png::Interlace::none};
    OA_CHECK(empty(read_mod_badge(encoded(oversize, std::vector<uint8_t>(257, 0)))));

    // A file over the largest size read, whatever it holds.
    std::vector<uint8_t> padded = good;
    padded.resize(static_cast<size_t>(max_mod_badge_bytes) + 1, 0);
    OA_CHECK(empty(read_mod_badge(padded)));

    // In a folder: too large a file, a damaged file, and a folder of that name.
    const fs::path folder = scratch / "badges";
    fs::create_directories(folder);
    write(folder / "oamod.yaml", full_profile);
    write(folder / "oamod.png", padded);
    OA_CHECK(empty(read_mod_summary(folder).badge));
    write(folder / "oamod.png", std::vector<uint8_t>(good.begin(), good.begin() + 40));
    const ModSummary damaged = read_mod_summary(folder);
    OA_CHECK(empty(damaged.badge) && damaged.title == "Glacier Front");
    fs::remove(folder / "oamod.png");
    fs::create_directories(folder / "oamod.png");
    OA_CHECK(empty(read_mod_summary(folder).badge));
    fs::remove_all(folder / "oamod.png");

    // A file system that tells case apart may hold two spellings: neither is used.
    write(folder / "oamod.png", good);
    write(folder / "OAMOD.PNG", good);
    std::error_code ignored;
    const bool case_sensitive =
        !fs::equivalent(folder / "oamod.png", folder / "OAMOD.PNG", ignored);
    OA_CHECK(empty(read_mod_summary(folder).badge) == case_sensitive);
}

} // namespace

int main() {
    const fs::path scratch = oa::test::make_scratch_directory("oa-app-mod-summary");
    a_profile_gives_its_name_version_and_description(scratch);
    a_profile_without_a_description_or_name(scratch);
    a_folder_without_a_profile(scratch);
    a_damaged_profile(scratch);
    a_large_badge_is_scaled_down();
    every_colour_type_reads_as_rgba();
    a_bad_badge_gives_none(scratch);
    std::error_code ignored;
    fs::remove_all(scratch, ignored);
    return oa::test::check_exit_status();
}
