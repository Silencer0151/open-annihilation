// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The sizes offered for made-up monitors, as SDL would report them: a 4K
// monitor on Windows, a Retina Mac's built-in display, a 1280x1024 monitor
// of Windows XP's time, an ultrawide, a portrait monitor, one that reports
// more sizes than a list holds and one that reports nothing; for full
// screen and for a window. Then the mode each size switches to, the sizes a
// start keeps, the nearest offered size and the reports a check names in
// text.

#include "oa/platform/display_modes.hpp"
#include "oa/test/check.hpp"

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <vector>

namespace {

namespace dm = oa::platform::display_modes;
using dm::DisplayReport;
using dm::ReportedMode;
using dm::Size;
using dm::Use;

/// Returns a report of modes at one density and rate each, the desktop's
/// mode given apart.
///
/// @param desktop the desktop's mode
/// @param modes the modes, in the order the system lists them
/// @return the report
DisplayReport report_of(ReportedMode desktop, std::initializer_list<ReportedMode> modes) {
    DisplayReport report;
    report.desktop = desktop;
    report.modes.assign(modes.begin(), modes.end());
    return report;
}

/// Returns a mode at density 1.
///
/// @param width units across
/// @param height units down
/// @param rate frames a second
/// @return the mode
ReportedMode mode(int32_t width, int32_t height, float rate = 60.0F) {
    return ReportedMode{{width, height}, 1.0F, rate};
}

/// Returns a mode at a pixel density.
///
/// @param width units across
/// @param height units down
/// @param density pixels to a unit
/// @param rate frames a second
/// @return the mode
ReportedMode dense(int32_t width, int32_t height, float density, float rate) {
    return ReportedMode{{width, height}, density, rate};
}

/// A 4K monitor on Windows: every size at 60 and 30 frames a second, as
/// SDL lists them, the largest first, with the television sizes and the odd
/// 1176x664 a graphics driver adds; the same size at 16 and 32 bits a pixel
/// comes as two modes of the same size and rate.
DisplayReport four_k_monitor() {
    return report_of(mode(3840, 2160), {mode(3840, 2160), mode(3840, 2160, 30), mode(2560, 1600),
                                        mode(2560, 1440), mode(2560, 1440, 30), mode(1920, 1200),
                                        mode(1920, 1080), mode(1920, 1080, 30), mode(1920, 1080),
                                        mode(1680, 1050), mode(1600, 1200),     mode(1600, 1024),
                                        mode(1600, 900),  mode(1440, 900),      mode(1366, 768),
                                        mode(1360, 768),  mode(1280, 1024),     mode(1280, 960),
                                        mode(1280, 800),  mode(1280, 768),      mode(1280, 720),
                                        mode(1176, 664),  mode(1024, 768),      mode(1024, 768),
                                        mode(800, 600),   mode(720, 576, 50),   mode(720, 480),
                                        mode(640, 480),   mode(640, 480, 30)});
}

/// The 4K monitor's sizes, each once, in the order they are offered.
const std::vector<Size> four_k_sizes{
    {640, 480},   {720, 480},   {720, 576},   {800, 600},   {1024, 768},  {1176, 664},
    {1280, 720},  {1280, 768},  {1280, 800},  {1280, 960},  {1280, 1024}, {1360, 768},
    {1366, 768},  {1440, 900},  {1600, 900},  {1600, 1024}, {1600, 1200}, {1680, 1050},
    {1920, 1080}, {1920, 1200}, {2560, 1440}, {2560, 1600}, {3840, 2160},
};

/// A 14-inch Retina MacBook Pro's display, in points: its scaled sizes at
/// density 2, the desktop's among them, and the low-density duplicates and
/// the panel's own 3024x1964 at density 1, which SDL also lists.
DisplayReport retina_mac() {
    return report_of(
        dense(1512, 982, 2.0F, 120.0F),
        {dense(3024, 1964, 1.0F, 120.0F),
         dense(1800, 1169, 2.0F, 120.0F),
         dense(1512, 982, 2.0F, 120.0F),
         dense(1512, 982, 1.0F, 120.0F),
         dense(1352, 878, 2.0F, 120.0F),
         dense(1147, 745, 2.0F, 120.0F),
         dense(1024, 665, 2.0F, 120.0F),
         dense(800, 520, 1.0F, 120.0F),
         dense(640, 414, 1.0F, 120.0F)}
    );
}

/// A 1280x1024 monitor of Windows XP's time, with the 720x400 text mode.
DisplayReport xp_monitor() {
    return report_of(
        mode(1280, 1024, 60.0F),
        {mode(1280, 1024, 75.0F),
         mode(1280, 1024, 60.0F),
         mode(1280, 960),
         mode(1280, 768),
         mode(1280, 720),
         mode(1152, 864, 75.0F),
         mode(1024, 768, 75.0F),
         mode(1024, 768, 60.0F),
         mode(800, 600, 75.0F),
         mode(800, 600, 60.0F),
         mode(720, 400, 70.0F),
         mode(640, 480, 75.0F),
         mode(640, 480, 60.0F)}
    );
}

/// A 3440x1440 ultrawide whose desktop runs at 100 frames a second.
DisplayReport ultrawide() {
    return report_of(
        mode(3440, 1440, 100.0F),
        {mode(3440, 1440, 100.0F),
         mode(3440, 1440, 60.0F),
         mode(2560, 1440),
         mode(2560, 1080),
         mode(1920, 1080),
         mode(1680, 1050),
         mode(1600, 900),
         mode(1280, 1024),
         mode(1024, 768),
         mode(800, 600),
         mode(640, 480)}
    );
}

void test_four_k_monitor() {
    const DisplayReport report = four_k_monitor();
    OA_CHECK(dm::offered_sizes(report, Use::full_screen) == four_k_sizes);
    // The desktop is the monitor's largest size, so a window may take each.
    OA_CHECK(dm::offered_sizes(report, Use::window) == four_k_sizes);
    // Each size switches to its mode at the desktop's rate, never the 30.
    OA_CHECK(dm::mode_for(report, {3840, 2160}) == std::optional<std::size_t>{0});
    OA_CHECK(dm::mode_for(report, {2560, 1440}) == std::optional<std::size_t>{3});
    OA_CHECK(dm::mode_for(report, {1920, 1080}) == std::optional<std::size_t>{6});
    OA_CHECK(!dm::mode_for(report, {1920, 1440}));
    // Desktop set lower than the monitor: a window keeps to the desktop,
    // full screen still offers every size.
    DisplayReport lower = report;
    lower.desktop = mode(2560, 1440);
    const auto window = dm::offered_sizes(lower, Use::window);
    OA_CHECK(window.size() == four_k_sizes.size() - 2);
    OA_CHECK(window.back() == (Size{2560, 1440}));
    OA_CHECK(dm::offered_sizes(lower, Use::full_screen) == four_k_sizes);
}

void test_retina_mac() {
    const DisplayReport report = retina_mac();
    const std::vector<Size> full{
        {800, 520}, {1024, 665}, {1147, 745}, {1352, 878}, {1512, 982}, {1800, 1169}, {3024, 1964}
    };
    // 640x414 is shorter than the game's screen; 1512x982 comes once.
    OA_CHECK(dm::offered_sizes(report, Use::full_screen) == full);
    const std::vector<Size> window{{800, 520}, {1024, 665}, {1147, 745}, {1352, 878}, {1512, 982}};
    OA_CHECK(dm::offered_sizes(report, Use::window) == window);
    // The desktop's size switches to its own density-2 mode, not the
    // low-density duplicate listed after it; a size SDL lists only at
    // density 2 switches to that one, the panel's own size to density 1.
    OA_CHECK(dm::mode_for(report, {1512, 982}) == std::optional<std::size_t>{2});
    OA_CHECK(dm::mode_for(report, {1800, 1169}) == std::optional<std::size_t>{1});
    OA_CHECK(dm::mode_for(report, {3024, 1964}) == std::optional<std::size_t>{0});
}

void test_xp_monitor() {
    const DisplayReport report = xp_monitor();
    const std::vector<Size> sizes{
        {640, 480},
        {800, 600},
        {1024, 768},
        {1152, 864},
        {1280, 720},
        {1280, 768},
        {1280, 960},
        {1280, 1024},
    };
    OA_CHECK(dm::offered_sizes(report, Use::full_screen) == sizes);
    OA_CHECK(dm::offered_sizes(report, Use::window) == sizes);
    // The desktop runs at 60: its rate wins over the higher 75.
    OA_CHECK(dm::mode_for(report, {1280, 1024}) == std::optional<std::size_t>{1});
    OA_CHECK(dm::mode_for(report, {1024, 768}) == std::optional<std::size_t>{7});
    // Without a desktop rate the highest rate wins.
    DisplayReport no_rate = report;
    no_rate.desktop.refresh_rate = 0.0F;
    OA_CHECK(dm::mode_for(no_rate, {1280, 1024}) == std::optional<std::size_t>{0});
    // A stored 1600x1200 is beyond this monitor; a window of a size that is
    // no mode still fits the desktop, which full screen cannot show.
    OA_CHECK(!dm::can_show(report, {1600, 1200}, Use::full_screen));
    OA_CHECK(!dm::can_show(report, {1600, 1200}, Use::window));
    OA_CHECK(dm::can_show(report, {1024, 768}, Use::full_screen));
    OA_CHECK(dm::can_show(report, {1100, 700}, Use::window));
    OA_CHECK(!dm::can_show(report, {1100, 700}, Use::full_screen));
}

void test_ultrawide() {
    const DisplayReport report = ultrawide();
    const std::vector<Size> sizes{
        {640, 480},
        {800, 600},
        {1024, 768},
        {1280, 1024},
        {1600, 900},
        {1680, 1050},
        {1920, 1080},
        {2560, 1080},
        {2560, 1440},
        {3440, 1440},
    };
    OA_CHECK(dm::offered_sizes(report, Use::full_screen) == sizes);
    OA_CHECK(dm::offered_sizes(report, Use::window) == sizes);
    OA_CHECK(dm::mode_for(report, {3440, 1440}) == std::optional<std::size_t>{0});
    DisplayReport at_sixty = report;
    at_sixty.desktop.refresh_rate = 60.0F;
    OA_CHECK(dm::mode_for(at_sixty, {3440, 1440}) == std::optional<std::size_t>{1});
}

void test_portrait_monitor() {
    const DisplayReport report = report_of(
        mode(1080, 1920),
        {mode(1080, 1920), mode(900, 1600), mode(768, 1024), mode(600, 800), mode(480, 640)}
    );
    // Sizes narrower than the game's screen are left out.
    const std::vector<Size> sizes{{768, 1024}, {900, 1600}, {1080, 1920}};
    OA_CHECK(dm::offered_sizes(report, Use::full_screen) == sizes);
}

void test_reports_nothing() {
    // No modes and a 1920x1080 desktop: the fixed sizes that fit it.
    const DisplayReport desktop_only = report_of(mode(1920, 1080), {});
    const std::vector<Size> fitting{{640, 480}, {800, 600}, {1024, 768}, {1280, 1024}};
    OA_CHECK(dm::offered_sizes(desktop_only, Use::full_screen) == fitting);
    OA_CHECK(dm::offered_sizes(desktop_only, Use::window) == fitting);
    // Nothing known at all: every fixed size.
    const DisplayReport unknown{};
    const std::vector<Size> every(dm::fallback_sizes.begin(), dm::fallback_sizes.end());
    OA_CHECK(dm::offered_sizes(unknown, Use::full_screen) == every);
    // A desktop smaller than the game's screen still offers 640x480.
    const DisplayReport tiny = report_of(mode(600, 400), {});
    OA_CHECK(dm::offered_sizes(tiny, Use::window) == (std::vector<Size>{{640, 480}}));
    // A phone's modes are all shorter than 480: as good as none.
    const DisplayReport phone =
        report_of(dense(852, 393, 3.0F, 120.0F), {dense(852, 393, 3.0F, 120.0F)});
    OA_CHECK(dm::offered_sizes(phone, Use::full_screen) == (std::vector<Size>{{640, 480}}));
    // A display that cannot be judged shows every stored size.
    OA_CHECK(dm::can_show(unknown, {2560, 1440}, Use::full_screen));
    OA_CHECK(dm::can_show(phone, {1280, 1024}, Use::window));
}

void test_minimum_height() {
    // A mod's display rules keep only sizes of 768 rows or more.
    const auto tall = dm::offered_sizes(four_k_monitor(), Use::full_screen, 768);
    OA_CHECK(!tall.empty() && tall.front() == (Size{1024, 768}));
    for (const Size size : tall)
        OA_CHECK(size.height >= 768);
    OA_CHECK(tall.size() == 17);
    const DisplayReport desktop_only = report_of(mode(1920, 1080), {});
    OA_CHECK(
        dm::offered_sizes(desktop_only, Use::window, 768) ==
        (std::vector<Size>{{1024, 768}, {1280, 1024}})
    );
    // The smallest tall size stays on a desktop too small for it.
    const DisplayReport small_desktop = report_of(mode(800, 600), {});
    OA_CHECK(
        dm::offered_sizes(small_desktop, Use::window, 768) == (std::vector<Size>{{1024, 768}})
    );
}

void test_more_sizes_than_a_list_holds() {
    DisplayReport report;
    report.desktop = mode(640 + 8 * 149, 480 + 4 * 149);
    for (int32_t index = 149; index >= 0; --index)
        report.modes.push_back(mode(640 + 8 * index, 480 + 4 * index));
    const auto sizes = dm::offered_sizes(report, Use::full_screen);
    OA_CHECK(sizes.size() == dm::most_sizes);
    OA_CHECK(sizes.front() == (Size{640 + 8 * 50, 480 + 4 * 50}));
    OA_CHECK(sizes.back() == report.desktop.size);
}

void test_nearest_offered() {
    const std::vector<Size> sizes{{640, 480}, {800, 600}, {1024, 768}, {1280, 1024}};
    OA_CHECK(dm::nearest_offered(sizes, {1024, 768}) == std::optional<std::size_t>{2});
    OA_CHECK(dm::nearest_offered(sizes, {1100, 700}) == std::optional<std::size_t>{2});
    OA_CHECK(dm::nearest_offered(sizes, {3840, 2160}) == std::optional<std::size_t>{3});
    OA_CHECK(dm::nearest_offered(sizes, {600, 400}) == std::optional<std::size_t>{0});
    OA_CHECK(!dm::nearest_offered({}, {640, 480}));
}

void test_text() {
    OA_CHECK(dm::size_text({3840, 2160}) == "3840x2160");
    const auto report = dm::report_from_text("3840x2160@60,2560x1440@59.94,1512x982@120/2");
    OA_CHECK(report.has_value());
    if (report) {
        OA_CHECK(report->modes.size() == 3);
        OA_CHECK(report->desktop.size == (Size{3840, 2160}));
        OA_CHECK(report->desktop.refresh_rate == 60.0F);
        OA_CHECK(report->modes[1].refresh_rate > 59.93F && report->modes[1].refresh_rate < 59.95F);
        OA_CHECK(report->modes[2].pixel_density == 2.0F);
        OA_CHECK(report->modes[2].size == (Size{1512, 982}));
    }
    const auto nothing = dm::report_from_text("none");
    OA_CHECK(nothing.has_value() && nothing->modes.empty() && nothing->desktop.size == Size{});
    for (const char* bad :
         {"",
          "3840X2160",
          "3840x",
          "x2160",
          "0x0",
          "3840x2160@",
          "3840x2160@0",
          "3840x2160/0",
          "03840x2160",
          "3840x2160@60.1234",
          "70000x480",
          "3840x2160,,",
          " 3840x2160",
          "3840x2160@-60",
          "3840x2160@60/2/2"})
        OA_CHECK(!dm::report_from_text(bad));
}

} // namespace

int main() {
    test_four_k_monitor();
    test_retina_mac();
    test_xp_monitor();
    test_ultrawide();
    test_portrait_monitor();
    test_reports_nothing();
    test_minimum_height();
    test_more_sizes_than_a_list_holds();
    test_nearest_offered();
    test_text();
    return oa::test::check_exit_status();
}
