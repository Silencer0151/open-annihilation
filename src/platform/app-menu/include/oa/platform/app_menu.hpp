// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The macOS application menu's Settings… item, which opens the Open
// Annihilation settings with the key equivalent Cmd+,. Elsewhere there is no
// such menu, and every function here does nothing.
#pragma once

namespace oa::platform::app_menu {

/// What choosing the Settings… item does.
struct SettingsItemHooks {
    void* context{};
    /// Asks the game to open its settings; called on the thread that runs
    /// the application's event loop when the item is chosen from the menu.
    /// Not called when the item is chosen with Cmd+,: that key reaches the
    /// game as a key press first. Null greys the item.
    void (*open)(void* context){};
};

/// Tells whether this platform has an application menu with a Settings… item.
///
/// @return true on macOS
[[nodiscard]] bool settings_item_supported() noexcept;

/// Puts the Settings… item in the application menu, in place of the
/// Preferences… item the windowing library adds (or after "About" when the
/// menu has none), with the key equivalent Cmd+,. Call it once the
/// application's menu exists; without one only the hooks are kept, and a
/// later call replaces them.
///
/// @param hooks what choosing the item does; kept until the next call
void install_settings_item(const SettingsItemHooks& hooks);

/// Enables or greys the Settings… item; the state also holds for an item
/// installed later.
///
/// @param enabled true where the settings can open: the main menu and a match
void enable_settings_item(bool enabled);

} // namespace oa::platform::app_menu
