// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The macOS application menu's Settings… item (oa/platform/app_menu.hpp):
// installed once SDL's menu exists, enabled where the settings can open, and
// its choice posted as an SDL event the application loop takes
// (Runtime::take_engine_settings_request).

#include "oa/app/runtime.hpp"
#include "oa/platform/app_menu.hpp"

#include <SDL3/SDL.h>

#include <cstdint>
#include <iostream>

namespace oa::app {

namespace {

/// The event type the item posts. The menu outlives the Runtime that
/// installed it, so its action reads this rather than a Runtime; 0 posts
/// nothing.
uint32_t posted_event_type{};

/// Posts the Settings… item's request to the application loop.
void post_engine_settings_request(void*) {
    if (posted_event_type == 0)
        return;
    SDL_Event event{};
    event.type = posted_event_type;
    if (!SDL_PushEvent(&event))
        std::cerr << "Settings could not be opened: " << SDL_GetError() << '\n';
}

} // namespace

void Runtime::install_engine_settings_menu_item() {
    // The event type is registered on every platform, so a check can post
    // the item's request where the menu does not exist.
    if (engine_settings_menu_event_ == 0)
        engine_settings_menu_event_ = SDL_RegisterEvents(1);
    if (!oa::platform::app_menu::settings_item_supported())
        return;
    posted_event_type = engine_settings_menu_event_;
    oa::platform::app_menu::SettingsItemHooks hooks{};
    if (engine_settings_menu_event_ != 0)
        hooks.open = post_engine_settings_request;
    oa::platform::app_menu::install_settings_item(hooks);
}

void Runtime::sync_engine_settings_menu_item() {
    const bool enabled = screen_ == Screen::main_menu || screen_ == Screen::match;
    if (engine_settings_menu_enabled_ == enabled)
        return;
    engine_settings_menu_enabled_ = enabled;
    oa::platform::app_menu::enable_settings_item(enabled);
}

bool Runtime::is_engine_settings_menu_event(const SDL_Event& event) const noexcept {
    return engine_settings_menu_event_ != 0 && event.type == engine_settings_menu_event_;
}

} // namespace oa::app
