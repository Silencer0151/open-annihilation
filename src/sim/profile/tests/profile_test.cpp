// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/profile.hpp"

#include <cstdio>

namespace profile = oa::sim::profile;
using oa::ProfileTimes;

namespace {
int failures = 0;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x);                           \
            ++failures;                                                                            \
        }                                                                                          \
    } while (false)

void test_accumulate() {
    ProfileTimes times{};
    times.sampled_at = 1000;
    profile::accumulate(times, 1007, profile::Category::units);
    profile::accumulate(times, 1010, profile::Category::weapon);
    profile::accumulate(times, 1015, profile::Category::units);
    CHECK(times.pending[1] == 12);
    CHECK(times.pending[7] == 3);
    CHECK(times.sampled_at == 1015);
    // The clock wraps as the tick count does.
    times.sampled_at = 0xfffffffeu;
    profile::accumulate(times, 3, profile::Category::misc);
    CHECK(times.pending[8] == 5);
}

void test_window() {
    ProfileTimes times{};
    times.pending[0] = 4;
    times.pending[2] = 6;
    times.shown[5] = 99;
    profile::begin_window(times, 2000);
    CHECK(times.shown[0] == 4);
    CHECK(times.shown[2] == 6);
    CHECK(times.shown[5] == 0);
    CHECK(times.shown_total == 10);
    CHECK(times.pending[0] == 0 && times.pending[2] == 0);
    CHECK(times.sampled_at == 2000);
    // An idle window still scales the bars by 1, as does a negative sum.
    profile::begin_window(times, 2001);
    CHECK(times.shown_total == 1);
    times.pending[3] = -7;
    profile::begin_window(times, 2002);
    CHECK(times.shown[3] == -7);
    CHECK(times.shown_total == 1);
}

void test_labels() {
    CHECK(profile::category_count == 9);
    CHECK(profile::category_labels[static_cast<int>(profile::Category::render_static)][7] == 'S');
    CHECK(profile::category_labels[static_cast<int>(profile::Category::sfx)][0] == 'S');
    CHECK(profile::category_labels[static_cast<int>(profile::Category::misc)][0] == 'M');
}
} // namespace

int main() {
    test_accumulate();
    test_window();
    test_labels();
    if (failures != 0)
        return 1;
    std::puts("sim-profile: ok");
    return 0;
}
