// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/ui/hud/megamap.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstring>
#include <limits>

namespace oa::ui::hud {
namespace {

constexpr std::size_t kPaletteEntryBytes = 4;
constexpr std::size_t kPaletteEntries = 256;
/// UI colours (Game.ui_colors) of the rings, as the minimap draws them.
constexpr std::size_t kSensorRingColor = 10;
constexpr std::size_t kJammerRingColor = 12;
constexpr std::size_t kInterceptorRingColor = 15;

std::string lower(std::string_view text) {
    std::string out(text);
    for (auto& c : out)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

std::string_view trim(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0)
        text.remove_prefix(1);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0)
        text.remove_suffix(1);
    return text;
}

/// The value of a line, up to its ';' comment, trimmed.
std::string_view value_of(std::string_view text) {
    const auto comment = text.find(';');
    if (comment != std::string_view::npos)
        text = text.substr(0, comment);
    return trim(text);
}

bool read_switch(std::string_view value) {
    return lower(value) == "true" || value == "1";
}

uint8_t read_index(std::string_view value, uint8_t fallback) {
    int32_t number = 0;
    const auto result = std::from_chars(value.data(), value.data() + value.size(), number);
    if (result.ec != std::errc{} || number < 0 || number > 255)
        return fallback;
    return static_cast<uint8_t>(number);
}

/// Squared distance between a palette entry and a colour.
int32_t
distance(std::span<const uint8_t> palette, std::size_t entry, int32_t r, int32_t g, int32_t b) {
    const auto at = entry * kPaletteEntryBytes;
    const int32_t dr = palette[at] - r, dg = palette[at + 1] - g, db = palette[at + 2] - b;
    return dr * dr + dg * dg + db * db;
}

} // namespace

MegamapLayout megamap_layout(
    int32_t view_left,
    int32_t view_top,
    int32_t view_width,
    int32_t view_height,
    int32_t map_width,
    int32_t map_height
) noexcept {
    MegamapLayout layout{};
    if (view_width <= 0 || view_height <= 0 || map_width <= 0 || map_height <= 0)
        return layout;
    layout.map_width = map_width;
    layout.map_height = map_height;
    // The map's aspect kept: the wider side fills the view.
    if (static_cast<int64_t>(map_width) * view_height >=
        static_cast<int64_t>(map_height) * view_width) {
        layout.width = view_width;
        layout.height = std::max<int32_t>(
            1, static_cast<int32_t>(static_cast<int64_t>(map_height) * view_width / map_width)
        );
    } else {
        layout.height = view_height;
        layout.width = std::max<int32_t>(
            1, static_cast<int32_t>(static_cast<int64_t>(map_width) * view_height / map_height)
        );
    }
    layout.left = view_left + (view_width - layout.width) / 2;
    layout.top = view_top + (view_height - layout.height) / 2;
    return layout;
}

std::array<int32_t, 2>
megamap_point(const MegamapLayout& layout, int32_t map_x, int32_t map_z) noexcept {
    if (layout.map_width <= 0 || layout.map_height <= 0)
        return {layout.left, layout.top};
    return {
        layout.left +
            static_cast<int32_t>(static_cast<int64_t>(map_x) * layout.width / layout.map_width),
        layout.top +
            static_cast<int32_t>(static_cast<int64_t>(map_z) * layout.height / layout.map_height),
    };
}

std::optional<std::array<int32_t, 2>>
megamap_map_point(const MegamapLayout& layout, int32_t x, int32_t y) noexcept {
    if (layout.width <= 0 || layout.height <= 0 || x < layout.left || y < layout.top ||
        x >= layout.left + layout.width || y >= layout.top + layout.height)
        return std::nullopt;
    return std::array<int32_t, 2>{
        static_cast<int32_t>(
            static_cast<int64_t>(x - layout.left) * layout.map_width / layout.width
        ),
        static_cast<int32_t>(
            static_cast<int64_t>(y - layout.top) * layout.map_height / layout.height
        ),
    };
}

std::vector<uint8_t> downscale_terrain(
    const TerrainSource& source,
    int32_t map_width,
    int32_t map_height,
    int32_t width,
    int32_t height,
    std::span<const uint8_t> palette,
    bool dither
) {
    std::vector<uint8_t> out;
    if (width <= 0 || height <= 0 || map_width <= 0 || map_height <= 0 || source.pixel == nullptr ||
        palette.size() < kPaletteEntries * kPaletteEntryBytes)
        return out;
    out.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
    for (int32_t y = 0; y < height; ++y) {
        const int32_t z0 = static_cast<int32_t>(static_cast<int64_t>(y) * map_height / height);
        const int32_t z1 = std::max(
            z0 + 1, static_cast<int32_t>(static_cast<int64_t>(y + 1) * map_height / height)
        );
        for (int32_t x = 0; x < width; ++x) {
            const int32_t x0 = static_cast<int32_t>(static_cast<int64_t>(x) * map_width / width);
            const int32_t x1 = std::max(
                x0 + 1, static_cast<int32_t>(static_cast<int64_t>(x + 1) * map_width / width)
            );
            int64_t r = 0, g = 0, b = 0, count = 0;
            for (int32_t z = z0; z < z1; ++z)
                for (int32_t column = x0; column < x1; ++column) {
                    const auto entry =
                        static_cast<std::size_t>(source.pixel(source.user, column, z));
                    r += palette[entry * kPaletteEntryBytes];
                    g += palette[entry * kPaletteEntryBytes + 1];
                    b += palette[entry * kPaletteEntryBytes + 2];
                    ++count;
                }
            const auto mean_r = static_cast<int32_t>(r / count);
            const auto mean_g = static_cast<int32_t>(g / count);
            const auto mean_b = static_cast<int32_t>(b / count);
            // The two nearest entries; the first one found wins a tie.
            std::size_t nearest = 0, second = 0;
            int32_t best = std::numeric_limits<int32_t>::max(), runner = best;
            for (std::size_t entry = 0; entry < kPaletteEntries; ++entry) {
                const int32_t d = distance(palette, entry, mean_r, mean_g, mean_b);
                if (d < best) {
                    runner = best;
                    second = nearest;
                    best = d;
                    nearest = entry;
                } else if (d < runner) {
                    runner = d;
                    second = entry;
                }
            }
            const bool alternate =
                dither && ((x + y) & 1) != 0 && runner != std::numeric_limits<int32_t>::max();
            out[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                static_cast<std::size_t>(x)] = static_cast<uint8_t>(alternate ? second : nearest);
        }
    }
    return out;
}

uint8_t feature_blob_color(const FeatureDef& def) noexcept {
    if (def.metal > 0.0F)
        return kFeatureBlobReclaimable;
    if (std::strncmp(def.name, "Spire", sizeof def.name) == 0)
        return kFeatureBlobSpire;
    return kFeatureBlobOther;
}

IconConfig parse_icon_config(std::string_view text, uint32_t category_limit) {
    IconConfig config{};
    std::string section;
    while (!text.empty()) {
        const auto end = text.find('\n');
        std::string_view line = text.substr(0, end);
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
        line = trim(line);
        if (line.empty() || line.front() == ';')
            continue;
        if (line.front() == '[') {
            const auto close = line.find(']');
            section = lower(
                line.substr(1, close == std::string_view::npos ? line.size() - 1 : close - 1)
            );
            continue;
        }
        const auto equals = line.find('=');
        if (equals == std::string_view::npos)
            continue;
        const std::string key = lower(trim(line.substr(0, equals)));
        const std::string_view value = value_of(line.substr(equals + 1));
        if (section == "option") {
            auto& options = config.options;
            if (key == "fillcolor")
                options.fill_color = read_index(value, options.fill_color);
            else if (key == "transparentcolor")
                options.transparent_color = read_index(value, options.transparent_color);
            else if (key == "selectedcolor")
                options.selected_color = read_index(value, options.selected_color);
            else if (key == "hovercolor")
                options.hover_color = read_index(value, options.hover_color);
            else if (key == "usecirclehover")
                options.circle_hover = read_switch(value);
            else if (key == "usedefaulticon")
                options.default_icons = read_switch(value);
        } else if (section == "icon") {
            if (key == "unknow")
                config.unknown_file = std::string(value);
            else if (key == "nothing")
                config.nothing_file = std::string(value);
            else if (key == "nukeicon")
                config.nuke_file = std::string(value);
            else if (config.lines.size() < category_limit)
                config.lines.push_back({key, std::string(value)});
        }
    }
    return config;
}

IconChoice choose_icon(
    bool visible,
    uint16_t type_id,
    std::span<const data::defs::CategoryMask* const> categories,
    std::size_t& line
) noexcept {
    if (!visible)
        return IconChoice::nothing;
    for (std::size_t index = 0; index < categories.size(); ++index)
        if (categories[index] != nullptr &&
            data::defs::category_mask_contains(categories[index], type_id)) {
            line = index;
            return IconChoice::category;
        }
    return IconChoice::unknown;
}

std::optional<uint8_t> icon_pixel(
    const IconOptions& options, uint8_t pixel, uint8_t dot_color, bool selected, bool hovered
) noexcept {
    if (pixel == options.transparent_color)
        return std::nullopt;
    if (pixel == options.fill_color)
        return dot_color;
    if (pixel == options.selected_color && !selected) {
        if (hovered && !options.circle_hover)
            return options.hover_color;
        return std::nullopt;
    }
    return pixel;
}

uint32_t megamap_rings(
    const World& world,
    const Unit& unit,
    const UnitDef& def,
    const std::array<int32_t, 5>& minimums,
    std::array<MegamapRing, 5>& out
) noexcept {
    const auto& colors = world.game.ui_colors;
    uint32_t count = 0;
    const auto add = [&](int32_t radius, int32_t minimum, std::size_t color) {
        if (radius > 0 && radius >= minimum)
            out[count++] = {radius, colors[color]};
    };
    add(def.radar_distance, minimums[0], kSensorRingColor);
    add(def.sonar_distance, minimums[1], kSensorRingColor);
    add(def.radar_distance_jam, minimums[2], kJammerRingColor);
    add(def.sonar_distance_jam, minimums[3], kJammerRingColor);
    int32_t coverage = 0;
    for (const UnitWeapon& slot : unit.weapons) {
        const WeaponDef* weapon = world_weapon_def(&world, slot.def);
        if (weapon != nullptr && (weapon->flags & OA_WEAPON_FLAG_INTERCEPTOR) != 0) {
            const int32_t weapon_coverage = weapon->coverage;
            coverage = std::max(coverage, weapon_coverage);
        }
    }
    add(coverage, minimums[4], kInterceptorRingColor);
    return count;
}

} // namespace oa::ui::hud
