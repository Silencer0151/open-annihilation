// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// What the settings dialog says of the renderer: the Hardware acceleration
// row's status, first rule first, for the machine's memory against the 2 GiB
// threshold, the setting, either flag, the environment's driver, a shared
// game or a replay, the renderer, looked at or not, and --force-capable;
// whether nothing could help the run; and whether Vertical sync is out of
// reach.

#include "oa/app/acceleration_status.hpp"

#include "oa/app/render_policy.hpp"
#include "oa/test/check.hpp"

#include <cstdint>
#include <optional>

namespace {

using oa::app::AccelerationFacts;
using oa::app::report_acceleration;
using oa::app::render_policy::smallest_accelerated_memory;
using State = oa::ui::engine_settings::AccelerationState;
using Reach = oa::ui::engine_settings::AccelerationReach;

/// Memory enough for the graphics card's scaling: 8 GiB.
constexpr uint64_t kAmpleMemory = uint64_t{8} * 1024 * 1024 * 1024;
/// What a machine sold with 2 GB may report once the firmware and the
/// graphics take their share: 1,900 MiB.
constexpr uint64_t kSoldWithTwoGigabytes = uint64_t{1900} * 1024 * 1024;

/// Returns the facts of a run on a machine the graphics card could help:
/// acceleration asked for, a renderer found able and ample memory.
AccelerationFacts able() {
    AccelerationFacts facts{};
    facts.asked = true;
    facts.renderer_capable = true;
    facts.physical_memory = kAmpleMemory;
    return facts;
}

void the_two_gibibyte_threshold_is_the_render_policys() {
    // Memory the system does not report counts as less.
    OA_CHECK(!oa::app::enough_memory_for_acceleration(0));
    // One byte under the render policy's threshold is under 2 GiB; the
    // threshold itself counts.
    OA_CHECK(!oa::app::enough_memory_for_acceleration(smallest_accelerated_memory - 1));
    OA_CHECK(oa::app::enough_memory_for_acceleration(smallest_accelerated_memory));
    // A machine sold with 2 GB, which reports a little under 2 GiB, counts.
    OA_CHECK(oa::app::enough_memory_for_acceleration(kSoldWithTwoGigabytes));
    OA_CHECK(oa::app::enough_memory_for_acceleration(kAmpleMemory));
}

void off_by_the_setting_or_the_flag() {
    auto facts = able();
    facts.asked = false;
    auto report = report_acceleration(facts);
    OA_CHECK(report.status.state == State::off_by_setting);
    OA_CHECK(!report.acceleration_unavailable);
    // --no-hardware-acceleration, which asks for nothing, says so.
    facts.flag = false;
    OA_CHECK(report_acceleration(facts).status.state == State::off_by_command_line);
    // --hardware-acceleration asks for it whatever the setting.
    facts.flag = true;
    facts.asked = true;
    OA_CHECK(report_acceleration(facts).status.state == State::next_start);
    // From 2 GiB, Off shows first, whatever else holds.
    AccelerationFacts bare{};
    bare.physical_memory = kAmpleMemory;
    bare.software_renderer = true;
    OA_CHECK(report_acceleration(bare).status.state == State::off_by_setting);
    OA_CHECK(report_acceleration(bare).acceleration_unavailable);
    bare.flag = false;
    bare.environment_driver = true;
    OA_CHECK(report_acceleration(bare).status.state == State::off_by_command_line);
}

void the_environment_driver_keeps_the_frames_on_the_processor() {
    auto facts = able();
    facts.environment_driver = true;
    auto report = report_acceleration(facts);
    OA_CHECK(report.status.state == State::environment_driver);
    OA_CHECK(report.acceleration_unavailable);
    // --hardware-acceleration lifts it, as --force-capable does.
    facts.flag = true;
    report = report_acceleration(facts);
    OA_CHECK(report.status.state != State::environment_driver);
    OA_CHECK(!report.acceleration_unavailable);
    facts.flag.reset();
    facts.force_capable = true;
    report = report_acceleration(facts);
    OA_CHECK(report.status.state == State::next_start);
    OA_CHECK(!report.acceleration_unavailable);
}

void a_machine_under_two_gibibytes_keeps_the_frames_on_the_processor() {
    for (const uint64_t memory :
         {uint64_t{0}, uint64_t{256} * 1024 * 1024, smallest_accelerated_memory - 1}) {
        auto facts = able();
        facts.physical_memory = memory;
        auto report = report_acceleration(facts);
        OA_CHECK(report.status.state == State::needs_memory);
        OA_CHECK(report.acceleration_unavailable);
        // Neither --hardware-acceleration nor --force-capable lifts it.
        facts.flag = true;
        facts.force_capable = true;
        report = report_acceleration(facts);
        OA_CHECK(report.status.state == State::needs_memory);
        OA_CHECK(report.acceleration_unavailable);
        // It is told whatever the setting, the flags, the environment's
        // driver, the renderer or the game.
        facts.force_capable = false;
        facts.flag = false;
        OA_CHECK(report_acceleration(facts).status.state == State::needs_memory);
        facts.flag.reset();
        facts.asked = false;
        OA_CHECK(report_acceleration(facts).status.state == State::needs_memory);
        facts.asked = true;
        facts.environment_driver = true;
        OA_CHECK(report_acceleration(facts).status.state == State::needs_memory);
        facts.environment_driver = false;
        facts.software_renderer = true;
        OA_CHECK(report_acceleration(facts).status.state == State::needs_memory);
        facts.software_renderer = false;
        facts.shared_game = true;
        report = report_acceleration(facts);
        OA_CHECK(report.status.state == State::needs_memory);
        OA_CHECK(!report.status.replay);
        facts.shared_game = false;
        facts.replay = true;
        report = report_acceleration(facts);
        OA_CHECK(report.status.state == State::needs_memory);
        OA_CHECK(!report.status.replay);
    }
    // At the threshold the other facts decide.
    auto facts = able();
    facts.physical_memory = smallest_accelerated_memory;
    const auto report = report_acceleration(facts);
    OA_CHECK(report.status.state == State::next_start);
    OA_CHECK(!report.acceleration_unavailable);
}

void a_renderer_found_unable_keeps_the_frames_on_the_processor() {
    auto facts = able();
    facts.renderer_capable = false;
    auto report = report_acceleration(facts);
    OA_CHECK(report.status.state == State::no_usable_card);
    OA_CHECK(report.acceleration_unavailable);
    // SDL's software renderer is never able, whatever else is known.
    for (const std::optional<bool> capable : {std::optional<bool>{}, std::optional<bool>{true}}) {
        facts = able();
        facts.renderer_capable = capable;
        facts.software_renderer = true;
        report = report_acceleration(facts);
        OA_CHECK(report.status.state == State::no_usable_card);
        OA_CHECK(report.acceleration_unavailable);
        // --force-capable counts it able.
        facts.force_capable = true;
        report = report_acceleration(facts);
        OA_CHECK(report.status.state == State::next_start);
        OA_CHECK(!report.acceleration_unavailable);
    }
}

void a_renderer_not_yet_looked_at_leaves_the_row_within_reach() {
    auto facts = able();
    facts.renderer_capable.reset();
    auto report = report_acceleration(facts);
    OA_CHECK(report.status.state == State::next_start);
    OA_CHECK(!report.acceleration_unavailable);
    // Off says so, and the row stays within reach.
    facts.asked = false;
    report = report_acceleration(facts);
    OA_CHECK(report.status.state == State::off_by_setting);
    OA_CHECK(!report.acceleration_unavailable);
    // In a shared game, On waits for its end.
    facts.asked = true;
    facts.shared_game = true;
    OA_CHECK(report_acceleration(facts).status.state == State::waiting_for_game_end);
}

void a_shared_game_or_a_replay_waits_for_its_end() {
    auto facts = able();
    facts.shared_game = true;
    auto report = report_acceleration(facts);
    OA_CHECK(report.status.state == State::waiting_for_game_end);
    OA_CHECK(!report.status.replay);
    OA_CHECK(!report.acceleration_unavailable);
    facts.shared_game = false;
    facts.replay = true;
    report = report_acceleration(facts);
    OA_CHECK(report.status.state == State::waiting_for_game_end);
    OA_CHECK(report.status.replay);
    // Off applies at once there.
    facts.asked = false;
    report = report_acceleration(facts);
    OA_CHECK(report.status.state == State::off_by_setting);
    OA_CHECK(!report.status.replay);
    // A renderer that could not help after the game says so instead.
    facts.asked = true;
    facts.renderer_capable = false;
    OA_CHECK(report_acceleration(facts).status.state == State::no_usable_card);
    // A game that began with the graphics card in use keeps it.
    facts = able();
    facts.shared_game = true;
    facts.tier_accelerated = true;
    OA_CHECK(report_acceleration(facts).status.state == State::in_use);
}

void in_use_says_what_it_does_here() {
    auto facts = able();
    facts.tier_accelerated = true;
    facts.reach = Reach::nearest_none;
    const auto report = report_acceleration(facts);
    OA_CHECK(report.status.state == State::in_use);
    OA_CHECK(report.status.reach == Reach::nearest_none);
    OA_CHECK(!report.acceleration_unavailable);
}

void vertical_sync_is_out_of_reach_on_the_software_renderer() {
    auto facts = able();
    OA_CHECK(!report_acceleration(facts).vertical_sync_unavailable);
    facts.software_renderer = true;
    OA_CHECK(report_acceleration(facts).vertical_sync_unavailable);
    facts.force_capable = true;
    OA_CHECK(!report_acceleration(facts).vertical_sync_unavailable);
    // A refusal holds for the run, --force-capable or not.
    facts.vertical_sync_refused = true;
    OA_CHECK(report_acceleration(facts).vertical_sync_unavailable);
    // So does a renderer whose device each change of the wait resets.
    facts = able();
    facts.vertical_sync_resets_device = true;
    OA_CHECK(report_acceleration(facts).vertical_sync_unavailable);
    facts.force_capable = true;
    OA_CHECK(report_acceleration(facts).vertical_sync_unavailable);
    OA_CHECK(!report_acceleration(facts).acceleration_unavailable);
    // Hardware acceleration's facts leave it alone.
    facts = able();
    facts.environment_driver = true;
    facts.physical_memory = 0;
    facts.asked = false;
    OA_CHECK(!report_acceleration(facts).vertical_sync_unavailable);
}

} // namespace

int main() {
    the_two_gibibyte_threshold_is_the_render_policys();
    off_by_the_setting_or_the_flag();
    the_environment_driver_keeps_the_frames_on_the_processor();
    a_machine_under_two_gibibytes_keeps_the_frames_on_the_processor();
    a_renderer_found_unable_keeps_the_frames_on_the_processor();
    a_renderer_not_yet_looked_at_leaves_the_row_within_reach();
    a_shared_game_or_a_replay_waits_for_its_end();
    in_use_says_what_it_does_here();
    vertical_sync_is_out_of_reach_on_the_software_renderer();
    return oa::test::check_exit_status();
}
