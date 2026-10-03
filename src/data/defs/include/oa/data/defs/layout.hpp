// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Where the game reads its data: the names of the seven directories a mod
// may rename, the extension of unit definition files, the section of a
// mission's map schema that places units, the build version unit
// definitions are checked against, and the two side names the game itself
// uses. Every use of these names reads
// them here, so a mod that renames a directory replaces the base one at
// every site and never falls back to it.
//
// The layout is set once, before any game data is read, and stays the same
// for the rest of the process; without a mod it is the base game's.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace oa::data::defs {

/// A directory of game data a mod may rename.
enum class DataDirectory : uint8_t {
    units,    ///< unit definitions (FBI)
    weapons,  ///< weapon definitions (TDF)
    gamedata, ///< sidedata, moveinfo, sound, LOS, help, translate and version tables
    ai,       ///< computer players' profiles
    guis,     ///< GUI layouts
    unitpics, ///< build pictures
    download  ///< build-menu entries (MENUENTRY TDF)
};

/// Number of DataDirectory values.
inline constexpr std::size_t data_directory_count = 7;

/// The names the game reads its data by.
struct DataLayout {
    /// Each DataDirectory's name, by its number; the base game's spellings.
    std::array<std::string, data_directory_count> directories{
        "units", "Weapons", "gamedata", "ai", "guis", "unitpics", "download"
    };
    /// Extension of unit definition files, without the dot.
    std::string unit_extension{"FBI"};
    /// Section of a mission's map schema whose entries place units.
    std::string map_units_section{"units"};
    /// The build version, major then minor, that a unit definition's Version
    /// key is checked against: a unit of a later version is unavailable. It is
    /// the game's network version, 3.1 for the base game.
    std::array<int8_t, 2> build_version{3, 1};
    /// The names of the two side slots the game itself uses: computer
    /// players' names, elimination messages and the side buttons' gadgets.
    /// SIDEDATA keeps its own names for everything else.
    std::array<std::string, 2> side_names{"Arm", "Core"};

    /// Compares every field.
    bool operator==(const DataLayout&) const = default;
};

/// Returns the layout in use.
///
/// @return the layout set by use_data_layout(); the base game's before
[[nodiscard]] const DataLayout& data_layout() noexcept;

/// Sets the layout every later read uses.
///
/// Call it before any game data is read and while no other thread reads
/// the layout; a test restores the base layout the same way.
///
/// @param layout the layout
void use_data_layout(const DataLayout& layout);

/// Returns the name a directory is read by.
///
/// @param directory the directory
/// @return its name in the layout in use, NUL-terminated
[[nodiscard]] const char* directory_name(DataDirectory directory) noexcept;

/// Joins a directory and a name below it.
///
/// @param directory the directory
/// @param name a path below it, '/'-separated
/// @return "<directory>/<name>"
[[nodiscard]] std::string data_path(DataDirectory directory, std::string_view name);

/// Joins the GUI directory and a layout's file name.
///
/// @param file the layout, such as "mainmenu.gui"
/// @return "<guis>/<file>"
[[nodiscard]] std::string gui_path(std::string_view file);

/// Returns the extension of unit definition files.
///
/// @return the extension without the dot, NUL-terminated, such as "FBI"
[[nodiscard]] const char* unit_extension() noexcept;

/// Returns the extension of unit definition files with its dot.
///
/// @return ".<extension>", such as ".FBI"
[[nodiscard]] std::string unit_file_suffix();

/// Returns the name of a side slot the game itself uses.
///
/// @param side 0 or 1; any other slot is named as 1
/// @return the name, NUL-terminated
[[nodiscard]] const char* side_name(uint8_t side) noexcept;

/// Returns the map schema section that places a mission's units.
///
/// @return the section's name, NUL-terminated
[[nodiscard]] const char* map_units_section() noexcept;

} // namespace oa::data::defs
