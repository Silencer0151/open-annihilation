// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The megamap of ui.megamap: the whole map drawn over the battlefield's
// rectangle with player-coloured category icons, sensor rings and the
// main view's rectangle, and the downscaled terrain both it and the
// enhanced minimap draw. Clicks on it give ordinary orders at the map point
// they stand for.
#pragma once

#include "oa/core/world.h"
#include "oa/data/defs/categories.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace oa::ui::hud {

/// Where the megamap shows the map: scaled to fit the view, its aspect
/// kept, centred with bars on the long sides.
struct MegamapLayout {
    int32_t left{};      ///< picture's left column on the canvas
    int32_t top{};       ///< its top row
    int32_t width{};     ///< its width in pixels
    int32_t height{};    ///< its height
    int32_t map_width{}; ///< the map in map pixels
    int32_t map_height{};
};

/// Fits the map into the view.
///
/// @param view_left the view's left column
/// @param view_top its top row
/// @param view_width its width
/// @param view_height its height
/// @param map_width the map's width in map pixels
/// @param map_height its height
/// @return the layout; empty for an empty map or view
[[nodiscard]] MegamapLayout megamap_layout(
    int32_t view_left,
    int32_t view_top,
    int32_t view_width,
    int32_t view_height,
    int32_t map_width,
    int32_t map_height
) noexcept;

/// Places a map point on the megamap.
[[nodiscard]] std::array<int32_t, 2>
megamap_point(const MegamapLayout& layout, int32_t map_x, int32_t map_z) noexcept;

/// Returns the map point a megamap pixel stands for, or none off the picture.
[[nodiscard]] std::optional<std::array<int32_t, 2>>
megamap_map_point(const MegamapLayout& layout, int32_t x, int32_t y) noexcept;

/// Reads one map pixel's palette index.
struct TerrainSource {
    void* user{};
    uint8_t (*pixel)(void* user, int32_t map_x, int32_t map_z){};
};

/// Downscales terrain to a picture: each picture pixel takes the mean
/// colour of the map pixels it covers, then the nearest palette entry or,
/// dithered, alternates in a checkerboard between the two nearest.
///
/// @param source the map's pixels
/// @param map_width map pixels across
/// @param map_height map pixels down
/// @param width picture width
/// @param height picture height
/// @param palette the game palette, four bytes an entry
/// @param dither dither between the two nearest entries
/// @return the picture's palette indices, rows `width` apart
[[nodiscard]] std::vector<uint8_t> downscale_terrain(
    const TerrainSource& source,
    int32_t map_width,
    int32_t map_height,
    int32_t width,
    int32_t height,
    std::span<const uint8_t> palette,
    bool dither
);

/// Palette indices of the megamap's feature blobs (ui.megamap feature-blobs).
inline constexpr uint8_t kFeatureBlobReclaimable = 0xfe;
inline constexpr uint8_t kFeatureBlobSpire = 0xd3;
inline constexpr uint8_t kFeatureBlobOther = 0xa5;

/// Returns a feature's blob colour: features worth metal, the feature
/// named "Spire", and the rest.
[[nodiscard]] uint8_t feature_blob_color(const FeatureDef& def) noexcept;

/// The icon settings of the megamap's icon file ([Option] of iconcfg.ini).
struct IconOptions {
    uint8_t fill_color{0};        ///< replaced by the owner's dot colour
    uint8_t transparent_color{9}; ///< not drawn
    uint8_t selected_color{0x59}; ///< drawn only while the unit is selected
    uint8_t hover_color{0x54};    ///< the selected colour's stand-in under the pointer
    bool circle_hover{};          ///< a ring marks the hovered unit instead
    bool default_icons{true};     ///< the built-in icon set instead of [Icon]
};

/// One [Icon] line: a unit category word (lower case) and its picture file.
struct IconLine {
    std::string category;
    std::string file;
};

/// The megamap's icon file, read.
struct IconConfig {
    IconOptions options{};
    std::vector<IconLine> lines; ///< category lines, in file order
    std::string unknown_file;    ///< "Unknow": a seen unit no line matches
    std::string nothing_file;    ///< "Nothing": a unit out of sight
    std::string nuke_file;       ///< "NukeIcon"
};

/// Reads an icon file's text: its [Option] numbers and switches and its
/// [Icon] lines, keys without case, values up to a ';'.
///
/// @param text the file
/// @param category_limit category lines kept (ui.megamap icon-categories)
/// @return the settings
[[nodiscard]] IconConfig parse_icon_config(std::string_view text, uint32_t category_limit);

/// Which icon a unit takes.
enum class IconChoice : uint8_t {
    category, ///< the first line whose category holds the unit's type
    unknown,  ///< seen, but no line matches
    nothing,  ///< out of sight
};

/// Chooses a unit's icon: out of sight, the "Nothing" icon; else the first
/// category line holding its type, or the "Unknow" icon.
///
/// @param visible the viewer sees the unit
/// @param type_id its type
/// @param categories each line's category mask, in file order; null entries match nothing
/// @param[out] line the matching line's index for IconChoice::category
/// @return the choice
[[nodiscard]] IconChoice choose_icon(
    bool visible,
    uint16_t type_id,
    std::span<const data::defs::CategoryMask* const> categories,
    std::size_t& line
) noexcept;

/// Recolours one icon pixel for a unit: the fill colour takes the owner's
/// dot colour; the selected colour shows while selected, takes the hover
/// colour under the pointer and is transparent otherwise.
///
/// @param options icon settings
/// @param pixel the icon's palette index
/// @param dot_color the owner's dot colour
/// @param selected the unit is selected
/// @param hovered the pointer is on it
/// @return the palette index, or none for a transparent pixel
[[nodiscard]] std::optional<uint8_t> icon_pixel(
    const IconOptions& options, uint8_t pixel, uint8_t dot_color, bool selected, bool hovered
) noexcept;

/// One sensor ring on the megamap.
struct MegamapRing {
    int32_t radius{}; ///< map pixels
    uint8_t color{};
};

/// The sensor and anti-nuke rings a unit shows: radar, sonar, radar jammer,
/// sonar jammer and interceptor coverage, each when its range reaches the
/// ring's minimum (ui.megamap ring-minimums, in that order) and is above 0.
///
/// @param world the unit's weapons and the UI colours
/// @param unit the unit
/// @param def its type
/// @param minimums the five minimums
/// @param[out] out the rings
/// @return how many were written
uint32_t megamap_rings(
    const World& world,
    const Unit& unit,
    const UnitDef& def,
    const std::array<int32_t, 5>& minimums,
    std::array<MegamapRing, 5>& out
) noexcept;

} // namespace oa::ui::hud
