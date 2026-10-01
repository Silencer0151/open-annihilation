// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Platforms without an application menu: nothing to put in it.

#include "oa/platform/app_menu.hpp"

namespace oa::platform::app_menu {

bool settings_item_supported() noexcept {
    return false;
}

void install_settings_item(const SettingsItemHooks&) {
}

void enable_settings_item(bool) {
}

} // namespace oa::platform::app_menu
