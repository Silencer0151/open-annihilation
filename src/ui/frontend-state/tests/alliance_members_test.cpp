// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Characterisation of alliance_members, the skirmish alliance head count:
// which slots count, the slot-count bounds, and a seeded sweep whose digest
// pins the counts the frontend computes today.

#include "oa/ui/frontend_state/skirmish_ui.hpp"

#include <cstdint>
#include <cstdio>
#include <stdexcept>

namespace {

namespace entry = oa::ui::frontend_state::game_entry;
namespace ui = oa::ui::frontend_state::skirmish_ui;

int failures = 0;

/// Records a failed check.
///
/// @param line source line of the check
/// @param what text of the failed condition
void report_failure(int line, const char* what) {
    std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, line, what);
    ++failures;
}

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            report_failure(__LINE__, #condition);                                                  \
        }                                                                                          \
    } while (false)

constexpr auto slot_capacity = static_cast<int32_t>(entry::skirmish_slot_capacity);
// A controller value outside the named ones; any non-zero value is enabled.
constexpr int32_t unnamed_controller = 7;

// 32-bit FNV-1a parameters.
constexpr uint32_t fnv_offset_basis = 0x811C9DC5u;
constexpr uint32_t fnv_prime = 0x01000193u;
// Marsaglia's xorshift32 shift triple.
constexpr uint32_t xorshift_left_first = 13;
constexpr uint32_t xorshift_right = 17;
constexpr uint32_t xorshift_left_second = 5;
constexpr uint32_t seed_settings = 0xA4093822u;
constexpr int32_t sweep_settings = 300;
constexpr uint32_t sweep_digest = 0x1A9015ABu;

/// Reports whether alliance_members rejects a settings block.
///
/// @param settings settings to count
/// @return true when std::invalid_argument is thrown
bool rejects(const ui::Settings& settings) {
    try {
        (void)ui::alliance_members(settings, 0);
    } catch (const std::invalid_argument&) {
        return true;
    }
    return false;
}

/// Checks which slots count towards an alliance.
void test_counted_slots() {
    ui::Settings settings;
    CHECK(ui::alliance_members(settings, 0) == 0);

    settings.slot_count = 4;
    settings.slots[0] = {entry::controller::human, 0, 2, 0, 0, 0};
    settings.slots[1] = {entry::controller::computer, 1, 2, 0, 0, 1};
    settings.slots[2] = {entry::controller::disabled, 0, 2, 0, 0, 2};
    settings.slots[3] = {unnamed_controller, 0, 3, 0, 0, 3};
    // Beyond slot_count: never counted.
    settings.slots[4] = {entry::controller::human, 0, 2, 0, 0, 4};
    CHECK(ui::alliance_members(settings, 2) == 2);
    CHECK(ui::alliance_members(settings, 3) == 1);
    CHECK(ui::alliance_members(settings, 0) == 0);
    CHECK(ui::alliance_members(settings, entry::unassigned_alliance) == 0);

    // Alliance values outside 0..5 are matched as stored.
    settings.slots[0].alliance = -1;
    CHECK(ui::alliance_members(settings, -1) == 1);
    CHECK(ui::alliance_members(settings, 2) == 1);

    // Every slot of a full table in one alliance.
    for (auto& slot : settings.slots) {
        slot = {entry::controller::computer, 0, 4, 0, 0, 0};
    }
    settings.slot_count = slot_capacity;
    CHECK(ui::alliance_members(settings, 4) == slot_capacity);

    // A slot count outside 0..capacity is refused.
    settings.slot_count = slot_capacity + 1;
    CHECK(rejects(settings));
    settings.slot_count = -1;
    CHECK(rejects(settings));
    settings.slot_count = 0;
    CHECK(!rejects(settings));
}

/// Advances an xorshift32 generator.
///
/// @param[in,out] state generator state, never 0
/// @return the next value
uint32_t next(uint32_t& state) {
    state ^= state << xorshift_left_first;
    state ^= state >> xorshift_right;
    state ^= state << xorshift_left_second;
    return state;
}

/// Counts every alliance of seeded settings and pins the digest of the counts.
void test_sweep() {
    uint32_t state = seed_settings;
    uint32_t digest = fnv_offset_basis;
    for (int32_t i = 0; i < sweep_settings; ++i) {
        ui::Settings settings;
        settings.slot_count =
            static_cast<int32_t>(next(state) % (entry::skirmish_slot_capacity + 1));
        for (auto& slot : settings.slots) {
            slot.controller = static_cast<int32_t>(next(state) % 4);
            slot.alliance = static_cast<int32_t>(next(state) % 8) - 1;
        }
        for (int32_t alliance = -1; alliance <= ui::alliance_count; ++alliance) {
            digest = (digest ^ static_cast<uint32_t>(ui::alliance_members(settings, alliance))) *
                     fnv_prime;
        }
    }
    if (digest != sweep_digest) {
        std::fprintf(
            stderr,
            "%s:%d: sweep digest is 0x%08X, expected 0x%08X\n",
            __FILE__,
            __LINE__,
            digest,
            sweep_digest
        );
        ++failures;
    }
}

} // namespace

int main() {
    test_counted_slots();
    test_sweep();
    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    return 0;
}
