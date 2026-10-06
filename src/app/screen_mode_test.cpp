// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// A screen size applied to the game's window at once, followed on a
// made-up window over made-up monitors (a 4K monitor on Windows, a Retina
// Mac's built-in display, a 1280x1024 monitor of Windows XP's time and a
// Wayland desktop): how full screen shows a size on each window system,
// the frame drawn at the size and scaled to the screen, and the steps for a
// window, a maximised window, full screen at a mode of the size or on the
// desktop's mode, a mode the display lacks or refuses, and Desktop.

#include "screen_mode.hpp"

#include "oa/platform/display_modes.hpp"
#include "oa/test/check.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace dm = oa::platform::display_modes;
using oa::app::AppliedScreen;
using oa::app::apply_screen_size;
using oa::app::full_screen_method;
using oa::app::FullScreenMethod;
using oa::app::scaled_frame;
using oa::app::ScreenHooks;
using oa::app::ScreenWindow;
using dm::Size;

/// A made-up window on a made-up monitor, which notes each step asked of it
/// and changes as a window system would.
struct FakeWindow {
    ScreenWindow state{};             ///< the window now
    dm::DisplayReport report{};       ///< the monitor the window is on
    bool refuses_modes{};             ///< the display refuses every mode it lists
    std::optional<Size> mode{};       ///< the mode full screen takes; none for the desktop's
    std::vector<std::string> steps{}; ///< each step asked, in order
};

/// Returns a size as the steps name it.
///
/// @param size the size
/// @return "WIDTHxHEIGHT"
std::string text_of(Size size) {
    return dm::size_text(size);
}

/// Returns the hooks that reach a made-up window.
///
/// @param window the window
/// @return the hooks
ScreenHooks hooks_of(FakeWindow& window) {
    ScreenHooks hooks{};
    hooks.context = &window;
    hooks.window = [](void* context) { return static_cast<FakeWindow*>(context)->state; };
    hooks.restore = [](void* context) {
        auto& fake = *static_cast<FakeWindow*>(context);
        fake.steps.emplace_back("restore");
        fake.state.maximised = false;
    };
    hooks.set_window_size = [](void* context, Size size) {
        auto& fake = *static_cast<FakeWindow*>(context);
        fake.steps.push_back("size " + text_of(size));
        // A maximised window keeps its size, as on Windows and macOS.
        if (!fake.state.maximised && !fake.state.full_screen)
            fake.state.size = size;
    };
    hooks.keep_on_display = [](void* context, Size size) {
        static_cast<FakeWindow*>(context)->steps.push_back("place " + text_of(size));
    };
    hooks.take_mode = [](void* context, Size size) {
        auto& fake = *static_cast<FakeWindow*>(context);
        fake.steps.push_back("mode " + text_of(size));
        if (fake.refuses_modes || !dm::mode_for(fake.report, size))
            return false;
        fake.mode = size;
        // Full screen takes the mode at once, and the window its size.
        if (fake.state.full_screen) {
            fake.state.exclusive = true;
            fake.state.size = size;
        }
        return true;
    };
    hooks.take_desktop_mode = [](void* context) {
        auto& fake = *static_cast<FakeWindow*>(context);
        fake.steps.emplace_back("desktop mode");
        fake.mode.reset();
        if (fake.state.full_screen) {
            fake.state.exclusive = false;
            fake.state.size = fake.report.desktop.size;
        }
    };
    return hooks;
}

/// Returns a made-up monitor from the text a check names it with.
///
/// @param text the modes, the first the desktop's (dm::report_from_text)
/// @return the report
dm::DisplayReport monitor(std::string_view text) {
    const auto report = dm::report_from_text(text);
    OA_CHECK(report.has_value());
    return report.value_or(dm::DisplayReport{});
}

/// A 4K monitor on Windows, at its desktop's mode.
constexpr std::string_view kWindows4k =
    "3840x2160@60,2560x1440@60,1920x1080@60,1600x900@60,1280x720@60,1024x768@60,800x600@60,"
    "640x480@60";
/// A 14-inch Retina MacBook Pro's display, in points.
constexpr std::string_view kRetina =
    "1512x982@120/2,1800x1169@120/2,1352x878@120/2,1147x745@120/2,3024x1964@120/1";
/// A 1280x1024 monitor of Windows XP's time.
constexpr std::string_view kXpMonitor =
    "1280x1024@60,640x480@60,800x600@60,1024x768@60,1152x864@60,1280x960@60";
/// A Wayland desktop: its own mode and the sizes the compositor scales.
constexpr std::string_view kWayland = "2560x1440@165,1920x1080,1600x900,1280x720,1024x768,800x600";

// Each window system's way of showing a size in full screen.
void test_method_per_window_system() {
    OA_CHECK(full_screen_method("windows", false, false) == FullScreenMethod::switch_mode);
    OA_CHECK(full_screen_method("x11", false, false) == FullScreenMethod::switch_mode);
    // X11 within a Wayland session, or in Steam's Game Mode, scales.
    OA_CHECK(full_screen_method("x11", true, false) == FullScreenMethod::scale_frame);
    OA_CHECK(full_screen_method("x11", false, true) == FullScreenMethod::scale_frame);
    for (const std::string_view driver :
         {"cocoa", "wayland", "dummy", "offscreen", "uikit", "android", "KMSDRM", ""})
        OA_CHECK(full_screen_method(driver, false, false) == FullScreenMethod::scale_frame);
}

// The frame is scaled only in full screen on the desktop's mode, at a size
// applied that is not the window's own.
void test_scaled_frame() {
    ScreenWindow window{};
    window.size = {1512, 982};
    OA_CHECK(scaled_frame(window, {1280, 720}) == Size{});
    window.full_screen = true;
    OA_CHECK(scaled_frame(window, {1280, 720}) == (Size{1280, 720}));
    OA_CHECK(scaled_frame(window, {}) == Size{});
    OA_CHECK(scaled_frame(window, {1512, 982}) == Size{});
    window.exclusive = true;
    window.size = {1280, 720};
    OA_CHECK(scaled_frame(window, {1280, 720}) == Size{});
    OA_CHECK(scaled_frame(window, {1024, 768}) == Size{});
}

// A window on Windows takes the size, stays on its display at it, and full
// screen will switch to the mode of the size.
void test_window_switching_modes() {
    FakeWindow window{};
    window.report = monitor(kWindows4k);
    window.state.size = {1024, 768};
    const AppliedScreen applied =
        apply_screen_size(hooks_of(window), {1280, 720}, FullScreenMethod::switch_mode);
    OA_CHECK((
        window.steps == std::vector<std::string>{"size 1280x720", "place 1280x720", "mode 1280x720"}
    ));
    OA_CHECK(window.state.size == (Size{1280, 720}));
    OA_CHECK(window.mode == (Size{1280, 720}));
    OA_CHECK(applied.screen == (Size{1280, 720}));
    OA_CHECK(applied.window_after_full_screen == Size{});
    OA_CHECK(applied.switched);
    // A size the monitor has no mode of keeps the desktop's for full screen.
    window.steps.clear();
    const AppliedScreen odd =
        apply_screen_size(hooks_of(window), {1300, 800}, FullScreenMethod::switch_mode);
    OA_CHECK((
        window.steps ==
        std::vector<std::string>{"size 1300x800", "place 1300x800", "mode 1300x800", "desktop mode"}
    ));
    OA_CHECK(!odd.switched && !window.mode);
    OA_CHECK(odd.screen == (Size{1300, 800}));
}

// A maximised window is brought back to a size of its own first, then sized;
// on a Mac full screen keeps the desktop's mode.
void test_maximised_window_scaling() {
    FakeWindow window{};
    window.report = monitor(kRetina);
    window.state.size = {1512, 945};
    window.state.maximised = true;
    const AppliedScreen applied =
        apply_screen_size(hooks_of(window), {1147, 745}, FullScreenMethod::scale_frame);
    OA_CHECK(
        (window.steps ==
         std::vector<std::string>{"restore", "size 1147x745", "place 1147x745", "desktop mode"})
    );
    OA_CHECK(window.state.size == (Size{1147, 745}) && !window.state.maximised);
    OA_CHECK(!applied.switched && applied.screen == (Size{1147, 745}));
    // Full screen then draws the match at the size, scaled to the screen.
    window.state.full_screen = true;
    window.state.size = window.report.desktop.size;
    OA_CHECK(scaled_frame(window.state, applied.screen) == (Size{1147, 745}));
}

// Full screen on Windows XP's monitor switches to the mode of the size at
// once, and the window takes the size once it leaves full screen.
void test_full_screen_switching_modes() {
    FakeWindow window{};
    window.report = monitor(kXpMonitor);
    window.state.full_screen = true;
    window.state.size = {1280, 1024};
    const AppliedScreen applied =
        apply_screen_size(hooks_of(window), {800, 600}, FullScreenMethod::switch_mode);
    OA_CHECK((window.steps == std::vector<std::string>{"mode 800x600"}));
    OA_CHECK(applied.switched && window.state.exclusive);
    OA_CHECK(window.state.size == (Size{800, 600}));
    OA_CHECK(applied.window_after_full_screen == (Size{800, 600}));
    // At its own mode the match is drawn at the window's size.
    OA_CHECK(scaled_frame(window.state, applied.screen) == Size{});
    // Desktop gives full screen the desktop's mode back.
    window.steps.clear();
    const AppliedScreen desktop =
        apply_screen_size(hooks_of(window), {}, FullScreenMethod::switch_mode);
    OA_CHECK((window.steps == std::vector<std::string>{"desktop mode"}));
    OA_CHECK(!window.state.exclusive && window.state.size == (Size{1280, 1024}));
    OA_CHECK(desktop.screen == Size{} && desktop.window_after_full_screen == Size{});
    OA_CHECK(scaled_frame(window.state, desktop.screen) == Size{});
}

// A mode the display lacks or refuses leaves full screen on the desktop's
// mode, drawn at the size and scaled.
void test_full_screen_mode_missing_or_refused() {
    FakeWindow window{};
    window.report = monitor(kWindows4k);
    window.state.full_screen = true;
    window.state.size = {3840, 2160};
    AppliedScreen applied =
        apply_screen_size(hooks_of(window), {1366, 768}, FullScreenMethod::switch_mode);
    OA_CHECK((window.steps == std::vector<std::string>{"mode 1366x768", "desktop mode"}));
    OA_CHECK(!applied.switched && !window.state.exclusive);
    OA_CHECK(scaled_frame(window.state, applied.screen) == (Size{1366, 768}));
    window.steps.clear();
    window.refuses_modes = true;
    applied = apply_screen_size(hooks_of(window), {1280, 720}, FullScreenMethod::switch_mode);
    OA_CHECK((window.steps == std::vector<std::string>{"mode 1280x720", "desktop mode"}));
    OA_CHECK(scaled_frame(window.state, applied.screen) == (Size{1280, 720}));
    OA_CHECK(applied.window_after_full_screen == (Size{1280, 720}));
}

// Full screen on Wayland never switches: the desktop's mode, the frame
// scaled; Desktop draws at the screen's own size again.
void test_full_screen_scaling() {
    FakeWindow window{};
    window.report = monitor(kWayland);
    window.state.full_screen = true;
    window.state.size = {2560, 1440};
    const AppliedScreen applied =
        apply_screen_size(hooks_of(window), {1280, 720}, FullScreenMethod::scale_frame);
    OA_CHECK((window.steps == std::vector<std::string>{"desktop mode"}));
    OA_CHECK(!applied.switched);
    OA_CHECK(scaled_frame(window.state, applied.screen) == (Size{1280, 720}));
    OA_CHECK(applied.window_after_full_screen == (Size{1280, 720}));
    const AppliedScreen desktop =
        apply_screen_size(hooks_of(window), {}, FullScreenMethod::scale_frame);
    OA_CHECK(scaled_frame(window.state, desktop.screen) == Size{});
}

// Desktop in a window leaves the window's size as it is.
void test_desktop_in_a_window() {
    FakeWindow window{};
    window.report = monitor(kWindows4k);
    window.state.size = {1300, 800};
    window.mode = Size{1280, 720};
    const AppliedScreen applied =
        apply_screen_size(hooks_of(window), {}, FullScreenMethod::switch_mode);
    OA_CHECK((window.steps == std::vector<std::string>{"desktop mode"}));
    OA_CHECK(window.state.size == (Size{1300, 800}) && !window.mode);
    OA_CHECK(applied.screen == Size{} && !applied.switched);
}

// Hooks left null take no step and read a window of no size.
void test_null_hooks() {
    const AppliedScreen applied =
        apply_screen_size(ScreenHooks{}, {1280, 720}, FullScreenMethod::switch_mode);
    OA_CHECK(applied.screen == (Size{1280, 720}));
    OA_CHECK(!applied.switched);
    OA_CHECK(applied.window_after_full_screen == Size{});
}

} // namespace

int main() {
    test_method_per_window_system();
    test_scaled_frame();
    test_window_switching_modes();
    test_maximised_window_scaling();
    test_full_screen_switching_modes();
    test_full_screen_mode_missing_or_refused();
    test_full_screen_scaling();
    test_desktop_in_a_window();
    test_null_hooks();
    return oa::test::check_exit_status();
}
