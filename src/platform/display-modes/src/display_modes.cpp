// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/platform/display_modes.hpp"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace oa::platform::display_modes {

namespace {

/// The largest side a made-up mode may name, in units.
constexpr int32_t largest_named_side = 65535;
/// The highest refresh rate a made-up mode may name, in frames a second.
constexpr float highest_named_rate = 1000.0F;
/// The highest pixel density a made-up mode may name.
constexpr float highest_named_density = 8.0F;
/// The most modes a made-up display may name.
constexpr std::size_t most_named_modes = 4 * most_sizes;

/// Tells whether a size fits within another.
///
/// @param size the size
/// @param bounds the size it must fit
/// @return true when it is no wider and no taller
bool fits(Size size, Size bounds) noexcept {
    return size.width <= bounds.width && size.height <= bounds.height;
}

/// Tells whether a size is known: both sides above zero.
///
/// @param size the size
/// @return true when it is known
bool known(Size size) noexcept {
    return size.width > 0 && size.height > 0;
}

/// Tells whether a size is at least the smallest offered and a minimum
/// height.
///
/// @param size the size
/// @param minimum_height the shortest size allowed, in units
/// @return true when it is large enough
bool large_enough(Size size, int32_t minimum_height) noexcept {
    return size.width >= smallest_size.width &&
           size.height >= std::max(minimum_height, smallest_size.height);
}

/// Reads a whole decimal number with no sign, as a made-up mode writes one.
///
/// @param text the digits
/// @param[out] value the number
/// @return true when the text is digits alone, with no leading zero, and
///     the number fits
bool read_number(std::string_view text, int32_t& value) {
    if (text.empty() || (text.size() > 1 && text.front() == '0'))
        return false;
    const char* end = text.data() + text.size();
    const auto [stop, error] = std::from_chars(text.data(), end, value);
    return error == std::errc{} && stop == end;
}

/// Reads a refresh rate or a pixel density: a whole number, or one with a
/// fraction after a '.'.
///
/// @param text the number
/// @param highest the highest value allowed
/// @param[out] value the number
/// @return true when the text is such a number above 0 and at most `highest`
bool read_fraction(std::string_view text, float highest, float& value) {
    const auto point = text.find('.');
    int32_t whole = 0;
    if (!read_number(text.substr(0, point), whole))
        return false;
    float result = static_cast<float>(whole);
    if (point != std::string_view::npos) {
        const auto digits = text.substr(point + 1);
        if (digits.empty() || digits.size() > 3)
            return false;
        float scale = 1.0F;
        for (const char digit : digits) {
            if (digit < '0' || digit > '9')
                return false;
            scale /= 10.0F;
            result += static_cast<float>(digit - '0') * scale;
        }
    }
    if (!(result > 0.0F) || result > highest)
        return false;
    value = result;
    return true;
}

/// Reads one made-up mode, "WIDTHxHEIGHT[@RATE][/DENSITY]".
///
/// @param text the mode
/// @return the mode; nothing when the text is not of that form
std::optional<ReportedMode> read_mode(std::string_view text) {
    ReportedMode mode{};
    if (const auto slash = text.find('/'); slash != std::string_view::npos) {
        if (!read_fraction(text.substr(slash + 1), highest_named_density, mode.pixel_density))
            return std::nullopt;
        text = text.substr(0, slash);
    }
    if (const auto at = text.find('@'); at != std::string_view::npos) {
        if (!read_fraction(text.substr(at + 1), highest_named_rate, mode.refresh_rate))
            return std::nullopt;
        text = text.substr(0, at);
    }
    const auto cross = text.find('x');
    if (cross == std::string_view::npos || !read_number(text.substr(0, cross), mode.size.width) ||
        !read_number(text.substr(cross + 1), mode.size.height) || mode.size.width <= 0 ||
        mode.size.height <= 0 || mode.size.width > largest_named_side ||
        mode.size.height > largest_named_side)
        return std::nullopt;
    return mode;
}

} // namespace

std::vector<Size> offered_sizes(const DisplayReport& report, Use use, int32_t minimum_height) {
    const Size desktop = report.desktop.size;
    const bool bounded = use == Use::window && known(desktop);
    std::vector<Size> sizes;
    sizes.reserve(report.modes.size());
    for (const ReportedMode& mode : report.modes)
        if (large_enough(mode.size, minimum_height) && (!bounded || fits(mode.size, desktop)))
            sizes.push_back(mode.size);
    std::sort(sizes.begin(), sizes.end(), listed_before);
    sizes.erase(std::unique(sizes.begin(), sizes.end()), sizes.end());
    if (sizes.size() > most_sizes)
        sizes.erase(sizes.begin(), sizes.end() - static_cast<std::ptrdiff_t>(most_sizes));
    if (!sizes.empty())
        return sizes;
    // Nothing useful: the fixed sizes, the smallest that is tall enough
    // always among them.
    for (const Size size : fallback_sizes)
        if (large_enough(size, minimum_height) &&
            (sizes.empty() || !known(desktop) || fits(size, desktop)))
            sizes.push_back(size);
    if (sizes.empty())
        sizes.push_back(fallback_sizes.back());
    return sizes;
}

bool can_show(const DisplayReport& report, Size size, Use use) {
    const bool judged = std::any_of(report.modes.begin(), report.modes.end(), [](const auto& mode) {
        return large_enough(mode.size, smallest_size.height);
    });
    if (!judged)
        return true;
    if (mode_for(report, size))
        return true;
    return use == Use::window && (!known(report.desktop.size) || fits(size, report.desktop.size));
}

std::optional<std::size_t> mode_for(const DisplayReport& report, Size size) {
    std::optional<std::size_t> chosen;
    // Ranks a mode of the size: the desktop's density first, then the
    // desktop's rate, then the higher rate.
    const auto better = [&](const ReportedMode& mode, const ReportedMode& than) {
        const bool density = mode.pixel_density == report.desktop.pixel_density;
        const bool than_density = than.pixel_density == report.desktop.pixel_density;
        if (density != than_density)
            return density;
        const bool rate =
            report.desktop.refresh_rate > 0.0F && mode.refresh_rate == report.desktop.refresh_rate;
        const bool than_rate =
            report.desktop.refresh_rate > 0.0F && than.refresh_rate == report.desktop.refresh_rate;
        if (rate != than_rate)
            return rate;
        return mode.refresh_rate > than.refresh_rate;
    };
    for (std::size_t index = 0; index < report.modes.size(); ++index) {
        const ReportedMode& mode = report.modes[index];
        if (mode.size == size && (!chosen || better(mode, report.modes[*chosen])))
            chosen = index;
    }
    return chosen;
}

std::optional<std::size_t> nearest_offered(std::span<const Size> offered, Size size) {
    if (offered.empty())
        return std::nullopt;
    std::size_t nearest = 0;
    for (std::size_t index = 0; index < offered.size(); ++index) {
        if (offered[index] == size)
            return index;
        if (listed_before(offered[index], size))
            nearest = index;
    }
    return nearest;
}

std::string size_text(Size size) {
    return std::to_string(size.width) + 'x' + std::to_string(size.height);
}

std::optional<DisplayReport> report_from_text(std::string_view text) {
    DisplayReport report;
    if (text == "none")
        return report;
    while (true) {
        const auto comma = text.find(',');
        const auto mode = read_mode(text.substr(0, comma));
        if (!mode || report.modes.size() >= most_named_modes)
            return std::nullopt;
        report.modes.push_back(*mode);
        if (comma == std::string_view::npos)
            break;
        text = text.substr(comma + 1);
    }
    report.desktop = report.modes.front();
    return report;
}

} // namespace oa::platform::display_modes
