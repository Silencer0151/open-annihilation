// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Mission tag table: registering the built-in tag blocks gives 3.1c's sorted
// table, and the sort handles runs above the insertion threshold.
#include "oa/data/mission_types.hpp"
#include "oa/ui/console/unit_tags.hpp"

#include <cstdio>
#include <cstring>
#include <string_view>

namespace {

namespace console = oa::ui::console;

int g_failures = 0;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond);          \
            ++g_failures;                                                                          \
        }                                                                                          \
    } while (0)

console::MissionTagTable g_table;

// The registered order equals 3.1c's mission table (oa::data::mission_types::registered_names).
void test_static_registration_order() {
    g_table = {};
    CHECK(console::mission_tags_register_static(&g_table));
    const auto expected = oa::data::mission_types::registered_names();
    CHECK(g_table.count == expected.size());
    for (size_t i = 0; i < expected.size() && i < g_table.count; ++i) {
        if (std::string_view(g_table.records[i].name) != expected[i]) {
            std::fprintf(
                stderr,
                "index %zu: %s != %.*s\n",
                i,
                g_table.records[i].name,
                static_cast<int>(expected[i].size()),
                expected[i].data()
            );
            ++g_failures;
        }
    }
    CHECK(std::strcmp(g_table.records[0].status_text, "Ready") == 0);
}

void test_lookup() {
    for (size_t i = 1; i < g_table.count; ++i) {
        const auto index = console::mission_tags_find(&g_table, g_table.records[i].name);
        CHECK(index == i);
        CHECK(index == oa::data::mission_types::index_for_name(g_table.records[i].name));
    }
    CHECK(
        console::mission_tags_find(&g_table, "selfdestruct") ==
        oa::data::mission_types::index_for_name("SelfDestruct")
    );
    CHECK(console::mission_tags_find(&g_table, "NoSuchMission") == 0);
    const auto* self_destruct =
        console::mission_tags_entry(&g_table, console::mission_tags_find(&g_table, "SELFDESTRUCT"));
    CHECK(self_destruct != nullptr);
    if (self_destruct != nullptr)
        CHECK(
            (self_destruct->attributes[console::kMissionSecondaryAttribute] &
             console::kMissionAttributeSecondaryQueue) != 0
        );
    CHECK(console::mission_tags_entry(&g_table, 200) == nullptr);
}

// Reverse-ordered and interleaved inputs of every length sort correctly.
void test_sort_shapes() {
    static char names[80][8];
    console::MissionTagRecord records[80];
    for (int length = 0; length <= 80; ++length) {
        for (int shape = 0; shape < 3; ++shape) {
            for (int i = 0; i < length; ++i) {
                const int key = shape == 0 ? length - i : shape == 1 ? (i * 37) % 80 : i % 5;
                std::snprintf(names[i], sizeof names[i], "k%03d", key);
                records[i] = {};
                records[i].name = names[i];
            }
            console::mission_tags_sort(records, records + length);
            for (int i = 1; i < length; ++i)
                CHECK(!console::mission_tag_less(records[i], records[i - 1]));
        }
    }
}

void test_capacity() {
    console::MissionTagTable table{};
    console::MissionTagRecord record{};
    record.name = "x";
    for (uint32_t i = 0; i < console::kMissionTagCapacity; ++i)
        CHECK(console::mission_tags_insert(&table, &record, 1));
    CHECK(!console::mission_tags_insert(&table, &record, 1));
    CHECK(table.count == console::kMissionTagCapacity);
}

} // namespace

int main() {
    test_static_registration_order();
    test_lookup();
    test_sort_shapes();
    test_capacity();
    if (g_failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    std::puts("unit tags: all checks passed");
    return 0;
}
