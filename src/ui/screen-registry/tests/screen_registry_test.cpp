// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Registry table behaviour: duplicate rejection, lookups, overlay z order and
// the dispatcher-query table.
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

// Answers a query with the number its state holds.
uint32_t answer(oa::app::ScreenContext*, void* state) {
    return *static_cast<const uint32_t*>(state);
}

// A query label no dispatcher state asks, for filling the table.
oa::ui::frontend_state::Query spare_query(uint32_t index) {
    constexpr uint32_t kFirstSpareQuery = 0x300;
    return static_cast<oa::ui::frontend_state::Query>(kFirstSpareQuery + index);
}

// Queries: binding, lookup, and the null, duplicate and full-table rejections.
void test_queries() {
    using namespace oa::app;
    using oa::ui::frontend_state::Query;
    static ScreenRegistry registry{};
    expect(query_find(&registry, Query::map_list_object_state) == nullptr, "no query bound yet");
    expect(
        !query_register(&registry, Query::map_list_object_state, nullptr, nullptr),
        "a null query handler is rejected"
    );
    expect(
        registry.rejected != nullptr && std::strcmp(registry.rejected, "dispatcher query") == 0,
        "a rejected query records \"dispatcher query\""
    );
    expect(registry.query_count == 0, "a rejected query takes no entry");

    static uint32_t value = 42;
    expect(
        query_register(&registry, Query::map_list_object_state, answer, &value), "query registers"
    );
    const auto* bound = query_find(&registry, Query::map_list_object_state);
    expect(bound != nullptr && bound->run == answer && bound->state == &value, "query found");
    expect(bound != nullptr && bound->run(nullptr, bound->state) == 42, "the handler answers");
    expect(
        !query_register(&registry, Query::map_list_object_state, answer, nullptr),
        "duplicate query rejected"
    );
    expect(query_find(&registry, spare_query(0)) == nullptr, "unknown query absent");

    static ScreenRegistry full{};
    for (uint32_t index = 0; index < kMaxQueries; ++index)
        expect(query_register(&full, spare_query(index), answer, &value), "the table fills");
    expect(full.rejected == nullptr, "a filled table rejected nothing");
    expect(
        !query_register(&full, Query::map_list_object_state, answer, &value),
        "a query past the table's end is rejected"
    );
    expect(
        full.rejected != nullptr && std::strcmp(full.rejected, "dispatcher query") == 0,
        "the full table records \"dispatcher query\""
    );
    expect(full.query_count == kMaxQueries, "the full table keeps its entries");
    expect(query_find(&full, spare_query(kMaxQueries - 1)) != nullptr, "the last entry is found");
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
    test_queries();
    return failures == 0 ? 0 : 1;
}
