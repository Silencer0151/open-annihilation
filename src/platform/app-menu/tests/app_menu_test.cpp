// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The Settings… item: offered on macOS only, and installing and enabling it
// without a windowing library's menu neither fails nor calls its hook.

#include "oa/platform/app_menu.hpp"

#include <iostream>

namespace {

int failures = 0;

void check(bool condition, const char* expression, const char* file, int line) {
    if (condition)
        return;
    std::cerr << file << ':' << line << ": check failed: " << expression << '\n';
    ++failures;
}

#define CHECK(expression) check((expression), #expression, __FILE__, __LINE__)

namespace app_menu = oa::platform::app_menu;

void offered_on_macos_only() {
#ifdef __APPLE__
    CHECK(app_menu::settings_item_supported());
#else
    CHECK(!app_menu::settings_item_supported());
#endif
}

void installing_without_a_menu_calls_nothing() {
    int opened = 0;
    app_menu::SettingsItemHooks hooks{};
    hooks.context = &opened;
    hooks.open = [](void* context) { ++*static_cast<int*>(context); };
    app_menu::install_settings_item(hooks);
    app_menu::enable_settings_item(true);
    app_menu::enable_settings_item(false);
    CHECK(opened == 0);
}

} // namespace

int main() {
    offered_on_macos_only();
    installing_without_a_menu_calls_nothing();
    if (failures != 0)
        return 1;
    std::cout << "app menu: ok\n";
    return 0;
}
