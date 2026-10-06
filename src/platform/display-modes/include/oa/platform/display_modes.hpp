// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The screen sizes a display offers: the full-screen modes it reports, each
// size once, sorted as the game's resolution lists are, from 640x480 up; for
// a window, the sizes that fit the desktop. The rules work on a report of the
// display, so that a test can hand them any monitor's modes; SDL fills the
// report for the game (oa/platform/display_modes/sdl.hpp).
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace oa::platform::display_modes {

/// A screen size in the window system's own units, the units a window's size
/// is given in: pixels on Windows and X11, points on macOS and Wayland,
/// where a high-density display shows each point with several pixels.
struct Size {
    int32_t width{};  ///< units across
    int32_t height{}; ///< units down

    friend bool operator==(const Size&, const Size&) = default;
};

/// One full-screen mode a display reports.
struct ReportedMode {
    Size size{};
    /// Pixels to a unit of size along each axis: 2 for a Retina display's
    /// scaled modes, 1 for most others.
    float pixel_density{1.0F};
    float refresh_rate{}; ///< frames a second; 0 when not known
};

/// What a display reports of itself.
struct DisplayReport {
    /// Its full-screen modes, in the order the system lists them; the same
    /// size may come several times, at other refresh rates, colour depths or
    /// pixel densities.
    std::vector<ReportedMode> modes;
    /// The desktop's mode; a size of 0 by 0 when it is not known.
    ReportedMode desktop{};
};

/// How a size is shown: the whole screen in full screen, or a window on the
/// desktop.
enum class Use : uint8_t {
    full_screen, ///< the screen switches to the size
    window,      ///< a window of the size; it fits the desktop
};

/// The smallest size offered: the game's own screen.
inline constexpr Size smallest_size{640, 480};
/// The most sizes a list offers; a display that reports more keeps its
/// largest.
inline constexpr std::size_t most_sizes = 100;
/// The sizes offered when a display reports no mode of smallest_size or
/// larger.
inline constexpr std::array<Size, 5> fallback_sizes{{
    {640, 480},
    {800, 600},
    {1024, 768},
    {1280, 1024},
    {1600, 1200},
}};

/// Tells whether one size comes before another in a list: the narrower
/// first, and of two as wide, the shorter.
///
/// @param first a size
/// @param second another size
/// @return true when `first` is listed before `second`
[[nodiscard]] constexpr bool listed_before(Size first, Size second) noexcept {
    return first.width < second.width ||
           (first.width == second.width && first.height < second.height);
}

/// Returns the sizes a display offers, each once, the narrower first and of
/// two as wide the shorter.
///
/// Every reported mode at least smallest_size.width wide and minimum_height
/// tall is offered, whatever its refresh rate, colour depth or pixel
/// density; for a window, only those no wider and no taller than a known
/// desktop. A display that reports more than most_sizes sizes offers its
/// largest. When nothing is left, fallback_sizes is offered, without the
/// sizes shorter than minimum_height and, of the others, those larger than a
/// known desktop; the smallest of them always stays.
///
/// @param report what the display reports
/// @param use full screen, or a window
/// @param minimum_height the shortest size offered, in units: 480, or a
///     mod's taller floor
/// @return the sizes, at most most_sizes of them; never empty
[[nodiscard]] std::vector<Size>
offered_sizes(const DisplayReport& report, Use use, int32_t minimum_height = smallest_size.height);

/// Tells whether a display can show a size, as a stored screen size is
/// checked at start: a size it reports a full-screen mode of; for a window
/// also any size that fits a known desktop, or any at all when the desktop
/// is not known. A display that reports no mode of smallest_size or larger
/// cannot be judged and shows every size.
///
/// @param report what the display reports
/// @param size the size
/// @param use full screen, or a window
/// @return true when the display can show the size
[[nodiscard]] bool can_show(const DisplayReport& report, Size size, Use use);

/// Returns the full-screen mode a display switches to for a size: one of
/// exactly that size, preferring the desktop's pixel density, then the
/// desktop's refresh rate, then the highest refresh rate.
///
/// @param report what the display reports
/// @param size the size
/// @return the mode's index in DisplayReport::modes; nothing when no mode
///     has the size
[[nodiscard]] std::optional<std::size_t> mode_for(const DisplayReport& report, Size size);

/// Returns the offered size nearest a size from below: the size itself when
/// it is offered, else the last one listed before it, else the first.
///
/// @param offered the sizes, as offered_sizes lists them
/// @param size the size
/// @return its index in `offered`; nothing when `offered` is empty
[[nodiscard]] std::optional<std::size_t> nearest_offered(std::span<const Size> offered, Size size);

/// Returns a size as the game writes it, its width and height in decimal
/// joined by an "x", as "1920x1080".
///
/// @param size the size
/// @return the text
[[nodiscard]] std::string size_text(Size size);

/// Reads a display report from text, as a check names a made-up monitor:
/// modes separated by commas, each "WIDTHxHEIGHT", optionally followed by
/// "@RATE" (frames a second) and "/DENSITY" (pixels to a unit), as
/// "1512x982@120/2". The first mode listed is also the desktop's. "none"
/// reads as a display that reports nothing, its desktop unknown.
///
/// @param text the text
/// @return the report; nothing when the text is not of that form
[[nodiscard]] std::optional<DisplayReport> report_from_text(std::string_view text);

} // namespace oa::platform::display_modes
