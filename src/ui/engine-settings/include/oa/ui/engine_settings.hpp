// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The Open Annihilation settings: their values and defaults, the keys the
// preferences file keeps them under, and which of them a running game
// locks. Every default plays the game as it plays without the settings;
// the dialog that shows them is in oa/ui/engine_settings/dialog.hpp.
#pragma once

#include "oa/platform/preferences.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace oa::ui::engine_settings {

/// The preferences keys of the settings. 3.1c's own SwitchAlt keeps its key
/// in the game's section, which the frontend's preferences read and write.
namespace key {
/// Path nodes a game tick, decimal (EngineSettings::path_search_nodes).
inline constexpr std::string_view path_search_nodes = "open-annihilation.path-search-nodes";
/// 1 or 0 (EngineSettings::wheel_zoom).
inline constexpr std::string_view wheel_zoom = "open-annihilation.wheel-zoom";
/// 1 or 0 (EngineSettings::escape_opens_menu).
inline constexpr std::string_view escape_opens_menu = "open-annihilation.escape-opens-menu";
/// Units per player, decimal (EngineSettings::unit_limit).
inline constexpr std::string_view unit_limit = "open-annihilation.unit-limit";
/// Frames a second, decimal (EngineSettings::max_frame_rate).
inline constexpr std::string_view max_frame_rate = "open-annihilation.max-fps";
/// The level's samples along each axis, 1 for off (EngineSettings::anti_aliasing).
inline constexpr std::string_view anti_aliasing = "open-annihilation.anti-aliasing";
/// 1 or 0 (EngineSettings::frame_stats).
inline constexpr std::string_view frame_stats = "open-annihilation.frame-stats";
/// "desktop", or the width and height in decimal joined by an "x", as
/// "800x600" (EngineSettings::screen_size).
inline constexpr std::string_view screen_size = "open-annihilation.screen-size";
} // namespace key

/// Path nodes the path search may visit in a game tick, all players
/// together, at 1x: the credit every match starts with.
inline constexpr int32_t base_path_search_nodes = 1333;
/// The most the Pathfinding cycles setting multiplies base_path_search_nodes by.
inline constexpr int32_t highest_path_search_multiplier = 8;
/// The most path nodes a game tick the setting allows: 8x.
inline constexpr int32_t highest_path_search_nodes =
    base_path_search_nodes * highest_path_search_multiplier;

/// The lowest unit limit the setting offers, in units per player.
inline constexpr uint16_t lowest_unit_limit = 50;
/// The highest unit limit the setting offers, in units per player.
inline constexpr uint16_t highest_unit_limit = 1500;
/// The unit limit setting's step, in units per player.
inline constexpr uint16_t unit_limit_step = 50;
/// The unit limit without a stored value or an installation's own, in units per player.
inline constexpr uint16_t default_unit_limit = 250;
/// The lowest unit limit the preferences keep, in units per player: the
/// lowest an installation's totala.ini can set, below the setting's stops.
inline constexpr uint16_t lowest_stored_unit_limit = 21;

/// The lowest maximum frame rate the setting offers, in frames a second.
inline constexpr uint32_t lowest_frame_rate = 40;
/// The highest maximum frame rate the setting offers, and its default, in frames a second.
inline constexpr uint32_t highest_frame_rate = 120;
/// The maximum frame rate setting's step, in frames a second.
inline constexpr uint32_t frame_rate_step = 5;
/// The maximum frame rate a Raspberry Pi starts with, in frames a second:
/// what its graphics keep up with at the game's resolutions.
inline constexpr uint32_t raspberry_pi_frame_rate = 60;
/// The maximum frame rate a light machine starts with, in frames a second.
inline constexpr uint32_t light_machine_frame_rate = 60;

/// The size the game's window, or the screen in full screen, is set to, in
/// pixels; zero by zero is the desktop's own size, the game's default.
struct ScreenSize {
    uint16_t width{};  ///< pixels across; 0 with height 0 for the desktop's size
    uint16_t height{}; ///< pixels down

    friend bool operator==(const ScreenSize&, const ScreenSize&) = default;
};

/// The desktop's size, as ScreenSize keeps it.
inline constexpr ScreenSize desktop_screen_size{};

/// The screen sizes the setting offers, in the order the dialog offers them.
inline constexpr std::array<ScreenSize, 5> screen_sizes{{
    desktop_screen_size,
    {640, 480},
    {800, 600},
    {1024, 768},
    {1280, 1024},
}};

/// The screen size a light machine starts with.
inline constexpr ScreenSize light_machine_screen_size{800, 600};
/// The screen size a light machine starts with when its desktop is smaller
/// than light_machine_screen_size.
inline constexpr ScreenSize small_desktop_screen_size{640, 480};

/// The most bytes of an installation's totala.ini the defaults read.
inline constexpr std::size_t installation_ini_limit = std::size_t{64} * 1024;

/// Enhanced anti-aliasing: how many times finer, along each axis, each
/// unit's model is drawn before it is reduced into the frame. The value is
/// that number; off draws units as without the setting.
enum class AntiAliasing : uint8_t {
    off = 1,
    x2 = 2,
    x3 = 3,
    x4 = 4,
    x8 = 8,
    x16 = 16,
};

/// The levels of enhanced anti-aliasing, in the order the dialog offers them.
inline constexpr std::array<AntiAliasing, 6> anti_aliasing_levels{
    AntiAliasing::off,
    AntiAliasing::x2,
    AntiAliasing::x3,
    AntiAliasing::x4,
    AntiAliasing::x8,
    AntiAliasing::x16,
};

/// The settings, as the dialog shows them and the game puts them in effect.
struct EngineSettings {
    /// Path nodes the path search may visit in a game tick, all players
    /// together: base_path_search_nodes times the Pathfinding cycles.
    int32_t path_search_nodes{base_path_search_nodes};
    bool wheel_zoom{true};    ///< the mouse wheel zooms the battlefield
    bool escape_opens_menu{}; ///< Escape with nothing to cancel opens the game menu
    bool switch_alt{};        ///< 3.1c's SwitchAlt: a number key alone selects its group
    uint16_t unit_limit{default_unit_limit};       ///< units per player, from the next game
    uint32_t max_frame_rate{highest_frame_rate};   ///< frames a second
    AntiAliasing anti_aliasing{AntiAliasing::off}; ///< enhanced anti-aliasing of units
    bool frame_stats{}; ///< the frame and tick times over the battlefield (+stats)
    /// The window's size, and the screen's in full screen, from the next start.
    ScreenSize screen_size{desktop_screen_size};

    friend bool operator==(const EngineSettings&, const EngineSettings&) = default;
};

/// What the defaults depend on besides the engine itself.
struct Inputs {
    /// The preferences file is the player's own: --preferences-file did not
    /// name one. With a named file every default is the game's own
    /// behaviour on every platform, and the installation is not read.
    bool players_own_profile{};
    bool macos{}; ///< the game runs on macOS
    /// The installation's totala.ini, at most installation_ini_limit bytes
    /// of it; empty when it has none.
    std::string_view installation_ini{};
    bool raspberry_pi{}; ///< the game runs on a Raspberry Pi
    /// The game runs on a light machine (oa::platform::light_machine): one
    /// processor, no SSE2 or under 512 MiB of memory.
    bool light_machine{};
    /// The desktop's size; zero by zero when it is not known.
    ScreenSize desktop{};
};

/// Returns the settings a player has before changing any.
///
/// Escape opens the game menu by default on macOS with the player's own
/// preferences file; the unit limit is the installation's
/// (installation_unit_limit) with the player's own file, else
/// default_unit_limit. On a Raspberry Pi with the player's own file the
/// maximum frame rate is raspberry_pi_frame_rate and enhanced
/// anti-aliasing is off. On a light machine with the player's own file the
/// maximum frame rate is light_machine_frame_rate, enhanced anti-aliasing is
/// off and the screen size is light_machine_screen_size, or
/// small_desktop_screen_size on a known desktop narrower or shorter than it.
///
/// @param inputs the platform, the preferences file and the installation
/// @return the defaults
[[nodiscard]] EngineSettings default_settings(const Inputs& inputs);

/// Reads the settings from the preferences.
///
/// A key that is absent, or whose value is not a whole decimal number,
/// gives the default; a value out of a setting's range is clamped into it.
/// The ranges: path nodes base_path_search_nodes to
/// highest_path_search_nodes; unit limit lowest_stored_unit_limit to
/// highest_unit_limit; frame rate lowest_frame_rate to highest_frame_rate;
/// anti-aliasing the highest level not above the stored number, off below
/// 2; a switch is on for a number above 0. A value between a setting's
/// stops is kept as stored. The screen size is "desktop" or one of
/// screen_sizes as "WIDTHxHEIGHT"; any other value gives the default.
///
/// @param values the preferences
/// @param inputs the platform, the preferences file and the installation
/// @param switch_alt 3.1c's SwitchAlt as the frontend's preferences hold it
/// @return the settings
[[nodiscard]] EngineSettings read_settings(
    const oa::platform::preferences::Values& values, const Inputs& inputs, bool switch_alt
);

/// Writes the settings a player chose into the preferences.
///
/// For each setting but switch_alt: after Restore defaults (`restored`), a
/// setting at its default has its key erased; otherwise a setting that
/// differs from `opened` has its key written, in decimal, a switch as 1 or
/// 0, the screen size as "desktop" or "WIDTHxHEIGHT". Every other key is
/// left as it is. switch_alt is never written here:
/// 3.1c's SwitchAlt key goes with the frontend's own preferences.
///
/// @param[in,out] values the preferences
/// @param opened the settings when the dialog opened
/// @param chosen the settings the player keeps
/// @param defaults the defaults (default_settings)
/// @param restored Restore defaults was pressed while the dialog was open
void write_settings(
    oa::platform::preferences::Values& values,
    const EngineSettings& opened,
    const EngineSettings& chosen,
    const EngineSettings& defaults,
    bool restored
);

/// Returns the text the preferences keep a screen size as.
///
/// @param size the screen size
/// @return "desktop" for desktop_screen_size, else "WIDTHxHEIGHT" in decimal
[[nodiscard]] std::string screen_size_text(ScreenSize size);

/// Returns the screen size a preferences text names.
///
/// @param text the stored text
/// @return the size, when the text is "desktop" or names one of screen_sizes
///     as "WIDTHxHEIGHT"; nothing otherwise
[[nodiscard]] std::optional<ScreenSize> screen_size_from_text(std::string_view text);

/// Returns the unit limit an installation's totala.ini sets.
///
/// Reads [Preferences] UnitLimit, the section and key matched without
/// regard to case, from the first [Preferences] section; the first
/// UnitLimit line there counts. The value is its leading whole number, 0 when it has none or is negative,
/// clamped to 3.1c's own range of 21 to 500. Lines may end in CR LF or LF;
/// a line starting with ';' is a comment. Only the first
/// installation_ini_limit bytes are read.
///
/// @param ini_text the file's text
/// @return the limit in units per player; nothing when the file sets none
[[nodiscard]] std::optional<uint16_t> installation_unit_limit(std::string_view ini_text);

/// Returns the Pathfinding cycles a path credit shows as.
///
/// @param nodes path nodes a game tick
/// @return nodes over base_path_search_nodes, to the nearest whole number,
///     from 1 to highest_path_search_multiplier
[[nodiscard]] int32_t path_search_multiplier(int32_t nodes) noexcept;

/// Returns the path credit a match plays at.
///
/// @param settings the settings in effect
/// @param shared_or_replay the match is played with other machines or
///     replays a recording
/// @return base_path_search_nodes in a shared game or a replay, else the
///     setting's path_search_nodes
[[nodiscard]] int32_t
match_path_search_nodes(const EngineSettings& settings, bool shared_or_replay) noexcept;

/// Why a setting cannot be changed now.
enum class Lock : uint8_t {
    none,         ///< it can be changed
    in_game,      ///< a game is running; it applies from the next game
    set_by_host,  ///< a shared game or a replay decides it
    command_line, ///< --max-fps decides the frame rate for this run
};

/// The game the dialog opens over.
struct GameState {
    bool in_game{};                      ///< a match is running
    bool shared_game{};                  ///< the match is played with other machines
    bool replay{};                       ///< the match replays a recording
    bool frame_rate_from_command_line{}; ///< --max-fps was given
};

/// What the dialog cannot change, and what its header says of the game.
struct Locks {
    Lock path_search{};    ///< Pathfinding cycles
    Lock unit_limit{};     ///< Unit limit
    Lock max_frame_rate{}; ///< Maximum frame rate
    bool shared_game{};    ///< the header says the shared game is still running
};

/// Returns the locks a game state puts on the settings.
///
/// Pathfinding cycles and Unit limit are locked during any game, as
/// set_by_host in a shared game or a replay; the maximum frame rate is
/// locked while --max-fps decides it.
///
/// @param state the game the dialog opens over
/// @return the locks
[[nodiscard]] Locks settings_locks(const GameState& state) noexcept;

} // namespace oa::ui::engine_settings
