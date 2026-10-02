// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The texture limit the render policy corrects from what the probe found,
// for each kind of report; the line the game logs once its renderer is
// made, and the adapter the "+stats" overlay names: a hardware adapter
// named, one that could not be named, one not read, SDL's software
// renderer with no limit, and a limit the device's own cuts; whether
// SDL_RENDER_DRIVER's value lets the start read the adapter; then SDL's
// software renderer on the dummy video driver, reported as the game
// reports it.
#include "graphics_report.hpp"

#include "oa/test/check.hpp"

#include <SDL3/SDL.h>

#include <cstdint>
#include <cstdio>
#include <string>

namespace {

using oa::app::corrected_texture_limit;
using oa::app::graphics_log_line;
using oa::app::stats_adapter_name;
using oa::platform::render_probe::AdapterFacts;
using oa::platform::render_probe::AdapterState;

/// Returns facts as the probe fills them for a renderer.
///
/// @param renderer SDL's name for the render driver
/// @param video SDL's name for the video driver
/// @param state what is known of the adapter
/// @param adapter the adapter's name
/// @param reported the texture limit SDL reports, texels; 0 for none
/// @param device the device's own texture limit, texels; 0 where not read
/// @return the facts
AdapterFacts facts_of(
    const char* renderer,
    const char* video,
    AdapterState state,
    const char* adapter,
    int64_t reported,
    uint32_t device = 0
) {
    AdapterFacts facts{};
    facts.renderer = renderer;
    facts.video_driver = video;
    facts.adapter_state = state;
    facts.adapter = adapter;
    facts.reported_texture_limit = reported;
    facts.device_texture_limit = device;
    return facts;
}

/// Returns the corrected texture limit of a renderer whose adapter was read.
///
/// @param renderer SDL's name for the render driver
/// @param reported the texture limit SDL reports, texels; 0 or less for none
/// @param device the device's own texture limit, texels; 0 where not read
/// @return corrected_texture_limit's answer
uint32_t limit_of(const char* renderer, int64_t reported, uint32_t device) {
    return corrected_texture_limit(
        facts_of(renderer, "x11", AdapterState::read, "card", reported, device)
    );
}

/// SDL's software renderer reports no limit, and a report of 0 or less is
/// none; a driver that reports the device's own limit keeps its report,
/// held at 32 bits; vulkan and gpu, which report a fixed 16384, take the
/// device's own limit where it was read, never above the report, and
/// otherwise the report at most 8192, as the render policy decides.
void test_corrected_texture_limit() {
    constexpr int64_t kFixedReport = 16384;
    constexpr uint32_t kUnreadCap = 8192;
    OA_CHECK(limit_of("software", 0, 0) == 0);
    OA_CHECK(limit_of("opengl", 0, 0) == 0);
    OA_CHECK(limit_of("opengl", -1, 0) == 0);
    OA_CHECK(limit_of("direct3d", 2048, 0) == 2048);
    OA_CHECK(limit_of("metal", kFixedReport, 0) == kFixedReport);
    OA_CHECK(limit_of("direct3d11", kFixedReport, 4096) == kFixedReport);
    OA_CHECK(limit_of("opengl", int64_t{1} << 40, 0) == UINT32_MAX);
    OA_CHECK(limit_of("vulkan", kFixedReport, 4096) == 4096);
    OA_CHECK(limit_of("vulkan", kFixedReport, 32768) == kFixedReport);
    OA_CHECK(limit_of("vulkan", kFixedReport, 0) == kUnreadCap);
    OA_CHECK(limit_of("gpu", kFixedReport, 0) == kUnreadCap);
    OA_CHECK(limit_of("gpu", kFixedReport, 2048) == 2048);
    OA_CHECK(limit_of("gpu", 4096, 0) == 4096);
    OA_CHECK(limit_of("gpu", 0, 2048) == 2048);
    OA_CHECK(limit_of("gpu", 0, 0) == kUnreadCap);
    // The adapter not read, as under SDL_RENDER_DRIVER: no device limit.
    OA_CHECK(
        corrected_texture_limit(
            facts_of("vulkan", "x11", AdapterState::skipped, "", kFixedReport)
        ) == kUnreadCap
    );
}

/// The line names the driver, the video driver, the adapter as known, the
/// texture limit and the standard tier.
void test_log_line() {
    constexpr uint32_t kMetalLimit = 16384;
    constexpr uint32_t kOldCardLimit = 2048;
    OA_CHECK(
        graphics_log_line(
            facts_of("metal", "cocoa", AdapterState::read, "Apple M2", kMetalLimit)
        ) == "open-annihilation: graphics: metal on cocoa (Apple M2), textures up to 16384; "
             "standard tier: the processor draws everything"
    );
    OA_CHECK(
        graphics_log_line(facts_of(
            "opengl", "x11", AdapterState::read, "llvmpipe (LLVM 15.0.7, 256 bits)", kMetalLimit
        )) == "open-annihilation: graphics: opengl on x11 (llvmpipe (LLVM 15.0.7, 256 bits)), "
              "textures up "
              "to 16384; standard tier: the processor draws everything"
    );
    OA_CHECK(
        graphics_log_line(
            facts_of("direct3d12", "windows", AdapterState::unknown, "", kOldCardLimit)
        ) == "open-annihilation: graphics: direct3d12 on windows (unknown adapter), textures up to "
             "2048; "
             "standard tier: the processor draws everything"
    );
    OA_CHECK(
        graphics_log_line(
            facts_of("direct3d", "windows", AdapterState::skipped, "", kOldCardLimit)
        ) ==
        "open-annihilation: graphics: direct3d on windows, textures up to 2048; standard tier: the "
        "processor draws everything"
    );
    OA_CHECK(
        graphics_log_line(facts_of("software", "dummy", AdapterState::none, "", 0)) ==
        "open-annihilation: graphics: software on dummy, textures of any size; standard tier: the "
        "processor draws everything"
    );
    // The device's own limit cuts vulkan's fixed report.
    OA_CHECK(
        graphics_log_line(facts_of(
            "vulkan", "x11", AdapterState::read, "AMD Radeon RX 580", kMetalLimit, kOldCardLimit
        )) == "open-annihilation: graphics: vulkan on x11 (AMD Radeon RX 580), textures up to "
              "2048; standard tier: the processor draws everything"
    );
}

/// The "+stats" overlay names a read adapter, says an unnamed one is
/// unknown, and names none where none was read.
void test_stats_adapter() {
    OA_CHECK(
        stats_adapter_name(facts_of("metal", "cocoa", AdapterState::read, "Apple M2", 0)) ==
        "Apple M2"
    );
    OA_CHECK(
        stats_adapter_name(facts_of("gpu", "x11", AdapterState::unknown, "", 0)) ==
        "unknown adapter"
    );
    OA_CHECK(stats_adapter_name(facts_of("opengl", "x11", AdapterState::skipped, "", 0)).empty());
    OA_CHECK(stats_adapter_name(facts_of("software", "x11", AdapterState::none, "", 0)).empty());
}

/// The adapter is read where SDL_RENDER_DRIVER names no driver, unset or
/// empty, and never where it names one, known to SDL or not.
void test_adapter_read_for() {
    using oa::app::adapter_read_for;
    using oa::platform::render_probe::AdapterRead;
    OA_CHECK(adapter_read_for({}) == AdapterRead::read);
    OA_CHECK(adapter_read_for("") == AdapterRead::read);
    OA_CHECK(adapter_read_for("software") == AdapterRead::skip);
    OA_CHECK(adapter_read_for("opengl") == AdapterRead::skip);
    OA_CHECK(adapter_read_for("bogus") == AdapterRead::skip);
}

/// SDL's software renderer on the dummy video driver, reported as the game
/// reports it with SDL_RENDER_DRIVER empty and naming it. Having no
/// adapter, it reads the same either way; test_adapter_read_for shows what
/// the hint decides.
void test_report_software_renderer() {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        OA_CHECK(false);
        return;
    }
    constexpr int kWindowWidth = 64;
    constexpr int kWindowHeight = 48;
    SDL_Window* window = SDL_CreateWindow("graphics report test", kWindowWidth, kWindowHeight, 0);
    OA_CHECK(window != nullptr);
    SDL_Renderer* renderer = window != nullptr ? SDL_CreateRenderer(window, "software") : nullptr;
    OA_CHECK(renderer != nullptr);
    if (renderer != nullptr) {
        const std::string expected = std::string("open-annihilation: graphics: software on ") +
                                     SDL_GetCurrentVideoDriver() +
                                     ", textures of any size; standard tier: the processor draws "
                                     "everything";
        OA_CHECK(SDL_SetHint(SDL_HINT_RENDER_DRIVER, ""));
        const AdapterFacts walked = oa::app::report_game_renderer(renderer);
        OA_CHECK(walked.adapter_state == AdapterState::none);
        OA_CHECK(graphics_log_line(walked) == expected);
        OA_CHECK(SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software"));
        const AdapterFacts named = oa::app::report_game_renderer(renderer);
        OA_CHECK(named.adapter_state == AdapterState::none);
        OA_CHECK(graphics_log_line(named) == expected);
        SDL_DestroyRenderer(renderer);
    }
    if (window != nullptr)
        SDL_DestroyWindow(window);
    SDL_Quit();
}

} // namespace

int main() {
    test_corrected_texture_limit();
    test_log_line();
    test_stats_adapter();
    test_adapter_read_for();
    test_report_software_renderer();
    return oa::test::check_exit_status();
}
