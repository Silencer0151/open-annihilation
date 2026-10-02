// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// What the game tells the player about the renderer it draws with: the line
// it logs at start, naming the render driver, the adapter, the texture
// limit and the tier frames are drawn in, and the names the "+stats"
// overlay's renderer row shows. Every frame is drawn in the standard tier,
// where the processor draws everything.
#pragma once

#include "oa/platform/render_probe.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace oa::app {

/// What every line the game logs about its graphics begins with.
inline constexpr std::string_view graphics_log_prefix = "open-annihilation: graphics: ";
/// The name of the tier every frame is drawn in.
inline constexpr std::string_view standard_tier_name = "standard";
/// What the start-up line says the standard tier does.
inline constexpr std::string_view standard_tier_text = "the processor draws everything";
/// What the start-up line and the "+stats" overlay say of an adapter that
/// was read but could not be named.
inline constexpr std::string_view unknown_adapter_name = "unknown adapter";

/// Returns the texture limit a renderer really has, as the render policy
/// corrects it (render_policy::texture_limit) from what the probe found: a
/// report of 0 or less means no limit, and one past 32 bits is held at the
/// largest 32-bit value; on a driver that reports a fixed limit
/// (render_probe::reports_fixed_texture_limit) the device's own limit
/// where the probe read it, otherwise the report at most 8192.
///
/// @param facts what the probe found
/// @return the limit in texels; 0 for none
[[nodiscard]] uint32_t
corrected_texture_limit(const oa::platform::render_probe::AdapterFacts& facts) noexcept;

/// Returns the line the game logs once its renderer is made: the render
/// driver on the video driver, the adapter in brackets where it was read,
/// the largest texture side (corrected_texture_limit), and the tier with
/// what it does, as in
/// "open-annihilation: graphics: metal on cocoa (Apple M2), textures up to
/// 16384; standard tier: the processor draws everything". An adapter that
/// could not be named reads "(unknown adapter)"; one not read, as under
/// SDL_RENDER_DRIVER, and SDL's software renderer, which has none, leave
/// the brackets out. No limit reads "textures of any size".
///
/// @param facts what the probe found
/// @return the line, without its line break
[[nodiscard]] std::string graphics_log_line(const oa::platform::render_probe::AdapterFacts& facts);

/// Returns the adapter the "+stats" overlay's renderer row names: its name
/// where it was read, unknown_adapter_name where it could not be named, and
/// nothing where it was not read or the renderer has none.
///
/// @param facts what the probe found
/// @return the name; empty for none
[[nodiscard]] std::string stats_adapter_name(const oa::platform::render_probe::AdapterFacts& facts);

/// Says whether the start reads the adapter, from the render driver
/// SDL_RENDER_DRIVER names: it does where none is named, and not where one
/// is, which keeps the start the player asked for to SDL's own properties
/// alone.
///
/// @param named_driver the hint's value; empty where it is unset or empty
/// @return AdapterRead::read for none named, else AdapterRead::skip
[[nodiscard]] oa::platform::render_probe::AdapterRead
adapter_read_for(std::string_view named_driver) noexcept;

/// Describes the renderer the game just made and logs graphics_log_line on
/// standard output, reading the adapter as adapter_read_for says for
/// SDL_RENDER_DRIVER.
///
/// @param renderer the renderer
/// @return what the probe found
oa::platform::render_probe::AdapterFacts report_game_renderer(SDL_Renderer* renderer);

} // namespace oa::app
