// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// What the Mods page of the settings shows of one mod folder: the title,
// version and description its oamod.yaml gives, read without resolving the
// profile or opening an archive, and its badge, oamod.png, decoded to RGBA.
// A folder whose files are missing, damaged or hostile still gives a row:
// nothing here throws or stops on bad input.
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace oa::app {

/// The name of a mod's badge in the root of its folder, beside its
/// oamod.yaml, matched without case.
inline constexpr std::string_view mod_badge_name = "oamod.png";

/// The side of a mod's badge as read, in pixels: a larger badge is scaled down to it.
inline constexpr uint32_t mod_badge_side = 64;

/// The largest badge file read, in bytes; a larger one gives no badge.
inline constexpr uintmax_t max_mod_badge_bytes = uintmax_t{256} * 1024;

/// The largest width and height of a badge read, in pixels; a larger one gives no badge.
inline constexpr uint32_t max_mod_badge_dimension = 256;

/// The bytes of one badge pixel: red, green, blue and alpha, in that order.
inline constexpr size_t mod_badge_pixel_bytes = 4;

/// The version a row shows for a folder without an oamod.yaml, or whose
/// oamod.yaml gives none that can be read.
inline constexpr std::string_view mod_summary_no_version = "N/A";

/// The description a row shows for a folder without an oamod.yaml.
inline constexpr std::string_view mod_summary_no_profile = "No oamod.yaml present";

/// The description a row shows for an oamod.yaml that cannot be read: a
/// file that breaks the profile grammar, is too large, or has a second
/// spelling differing only in case.
inline constexpr std::string_view mod_summary_unreadable_profile = "The oamod.yaml cannot be read";

/// The description a row shows for a folder that cannot be listed.
inline constexpr std::string_view mod_summary_unreadable_folder = "The folder cannot be read";

/// A mod's badge, decoded.
struct ModBadge {
    uint32_t width{};  ///< pixels, at most mod_badge_side; 0 for no badge
    uint32_t height{}; ///< pixels, at most mod_badge_side; 0 for no badge
    /// RGBA, 8 bits a sample, mod_badge_pixel_bytes a pixel, top row first,
    /// alpha not premultiplied; empty for no badge.
    std::vector<uint8_t> pixels{};
};

/// One row of the Mods page.
///
/// Every text is one line of UTF-8: a line break, tab or other control
/// character the file holds is shown as a space.
struct ModSummary {
    /// The profile's name; the folder's name when the folder has no
    /// oamod.yaml, or its oamod.yaml gives no name that can be read.
    std::string title{};
    /// The profile's version; mod_summary_no_version when the folder has no
    /// oamod.yaml, or its oamod.yaml gives no version that can be read.
    std::string version{};
    /// The profile's description, empty when it has none;
    /// mod_summary_no_profile without an oamod.yaml, and
    /// mod_summary_unreadable_profile or mod_summary_unreadable_folder when
    /// it cannot be read.
    std::string description{};
    /// The folder holds an oamod.yaml, readable or not.
    bool has_profile{};
    /// The folder's oamod.png, decoded; empty when it is missing or cannot be used.
    ModBadge badge{};
};

/// Reads what the Mods page shows of a mod folder.
///
/// Finds the folder's oamod.yaml as find_mod_profile does, matched without
/// case, and takes its top-level name, version and description, the scalars
/// only: the profile is neither resolved nor checked, and no archive is
/// opened. A string or number gives its text as written. Then reads the
/// badge, as read_mod_badge does, from the oamod.png in the folder's root,
/// matched without case, when that is a regular file of at most
/// max_mod_badge_bytes; two spellings differing only in case give no badge.
///
/// @param folder the mod folder
/// @return the row; never fails
[[nodiscard]] ModSummary read_mod_summary(const std::filesystem::path& folder);

/// Decodes a mod's badge from a PNG file.
///
/// Reads every standard colour type and bit depth, interlaced or not, into
/// RGBA: grey and palette images opaque (transparency given by a tRNS chunk
/// is not kept), samples of fewer than 8 bits scaled to the full range, and
/// 16-bit samples cut to their high byte. A badge wider or taller than
/// mod_badge_side is scaled down to fit within it, keeping its proportions,
/// each pixel the average of the source pixels it covers weighted by their
/// alpha.
///
/// @param png the file's bytes
/// @return the badge; empty when the file is over max_mod_badge_bytes, is not
///         a PNG, is damaged or ends early, or is wider or taller than
///         max_mod_badge_dimension
[[nodiscard]] ModBadge read_mod_badge(std::span<const uint8_t> png);

} // namespace oa::app
