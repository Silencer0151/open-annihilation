// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Registry table behaviour: duplicate rejection, lookups and overlay z order.
#include "oa/ui/frontend_state/dispatcher.hpp"
#include "oa/ui/screen_registry.hpp"
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace oa::app {
void register_builtin_screens(ScreenRegistry*) {
}
} // namespace oa::app

namespace {

int failures = 0;

void expect(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAILED: %s\n", what);
        ++failures;
    }
}

void noop(oa::app::ScreenContext*, void*) {
}

} // namespace

int main() {
    using namespace oa::app;
    static ScreenRegistry registry{};
    ScreenDesc screen{};
    screen.id = kFirstPackageScreen;
    screen.name = "package";
    expect(screen_register(&registry, &screen), "first screen registers");
    expect(!screen_register(&registry, &screen), "duplicate screen id rejected");
    expect(std::strcmp(registry.rejected, "package") == 0, "rejected name recorded");
    expect(screen_find(&registry, kFirstPackageScreen) != nullptr, "screen found");
    expect(screen_find(&registry, kFirstPackageScreen + 1) == nullptr, "unknown screen absent");

    const int16_t order[] = {5, -1, 5, 0};
    for (const auto z : order) {
        OverlayDesc overlay{};
        overlay.screen = kScreenAny;
        overlay.z = z;
        overlay.name = z == 5 && registry.overlay_count == 0 ? "first5" : "other";
        expect(overlay_register(&registry, &overlay), "overlay registers");
    }
    expect(registry.overlay_count == 4, "four overlays");
    expect(registry.overlays[0].z == -1 && registry.overlays[1].z == 0, "ascending z");
    expect(std::strcmp(registry.overlays[2].name, "first5") == 0, "equal z keeps order");

    using oa::ui::frontend_state::Step;
    expect(step_register(&registry, Step::setup_main_menu, noop, nullptr), "step registers");
    expect(!step_register(&registry, Step::setup_main_menu, noop, nullptr), "duplicate step");
    expect(step_find(&registry, Step::setup_main_menu) != nullptr, "step found");
    expect(step_find(&registry, Step::setup_skirmish) == nullptr, "unknown step absent");
    return failures == 0 ? 0 : 1;
}
