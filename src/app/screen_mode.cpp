// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "screen_mode.hpp"

namespace oa::app {

namespace display_modes = oa::platform::display_modes;

FullScreenMethod full_screen_method(
    std::string_view video_driver, bool wayland_session, bool steam_game_mode
) noexcept {
    if (video_driver == "windows")
        return FullScreenMethod::switch_mode;
    if (video_driver == "x11" && !wayland_session && !steam_game_mode)
        return FullScreenMethod::switch_mode;
    return FullScreenMethod::scale_frame;
}

display_modes::Size scaled_frame(const ScreenWindow& window, display_modes::Size applied) noexcept {
    if (!window.full_screen || window.exclusive || applied.width <= 0 || applied.height <= 0 ||
        applied == window.size)
        return {};
    return applied;
}

AppliedScreen
apply_screen_size(const ScreenHooks& hooks, display_modes::Size size, FullScreenMethod method) {
    const ScreenWindow window =
        hooks.window != nullptr ? hooks.window(hooks.context) : ScreenWindow{};
    const auto take_desktop_mode = [&hooks] {
        if (hooks.take_desktop_mode != nullptr)
            hooks.take_desktop_mode(hooks.context);
    };
    // The display's mode of the size where the method switches modes, else
    // the desktop's.
    const auto take_mode = [&] {
        const bool switched = method == FullScreenMethod::switch_mode &&
                              hooks.take_mode != nullptr && hooks.take_mode(hooks.context, size);
        if (!switched)
            take_desktop_mode();
        return switched;
    };
    AppliedScreen applied{};
    if (size.width <= 0 || size.height <= 0) {
        take_desktop_mode();
        return applied;
    }
    applied.screen = size;
    if (window.full_screen) {
        applied.switched = take_mode();
        applied.window_after_full_screen = size;
        return applied;
    }
    if (window.maximised && hooks.restore != nullptr)
        hooks.restore(hooks.context);
    if (hooks.set_window_size != nullptr)
        hooks.set_window_size(hooks.context, size);
    if (hooks.keep_on_display != nullptr)
        hooks.keep_on_display(hooks.context, size);
    applied.switched = take_mode();
    return applied;
}

} // namespace oa::app
