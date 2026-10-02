// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// What the settings dialog says of the renderer: the Hardware acceleration
// row's status, first rule first, for the machine's memory against the 2 GiB
// threshold, the setting, either flag, the environment's driver, a shared
// game or a replay, the renderer, looked at or not, lacking a feature or
// failed in the run, a drop by the memory guard or for slow frames, and
// --force-capable; in use, at the lowest budget or
// above it, or after the step-down lowered its rung for slow frames; whether nothing could help the run; what the graphics card
// does at each rung; the status the facts the tier is decided from give;
// and whether Vertical sync is out of reach.

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

void a_renderer_that_lacks_a_feature_says_so() {
    auto facts = able();
    facts.renderer_capable = false;
    facts.lacks_feature = true;
    auto report = report_acceleration(facts);
    OA_CHECK(report.status.state == State::lacks_feature);
    OA_CHECK(report.acceleration_unavailable);
    // SDL's software renderer has no graphics card, whatever it lacks.
    facts.software_renderer = true;
    OA_CHECK(report_acceleration(facts).status.state == State::no_usable_card);
    // Off, the environment and memory say so first.
    facts = able();
    facts.renderer_capable = false;
    facts.lacks_feature = true;
    facts.asked = false;
    OA_CHECK(report_acceleration(facts).status.state == State::off_by_setting);
}

void a_driver_that_failed_says_so_until_it_is_in_use_again() {
    auto facts = able();
    facts.driver_failed = true;
    auto report = report_acceleration(facts);
    OA_CHECK(report.status.state == State::driver_failed);
    // The row stays within reach, so that Off then On can try again.
    OA_CHECK(!report.acceleration_unavailable);
    // So it does where the rebuild landed on SDL's software renderer, which
    // the failure explains.
    {
        auto rebuilt = facts;
        rebuilt.software_renderer = true;
        rebuilt.renderer_capable = false;
        report = report_acceleration(rebuilt);
        OA_CHECK(report.status.state == State::driver_failed);
        OA_CHECK(!report.acceleration_unavailable);
        rebuilt.driver_failed = false;
        OA_CHECK(report_acceleration(rebuilt).acceleration_unavailable);
    }
    // In a shared game the failure shows rather than the wait.
    facts.shared_game = true;
    OA_CHECK(report_acceleration(facts).status.state == State::driver_failed);
    // Off says so first; in use again, the failure no longer shows.
    facts.shared_game = false;
    facts.asked = false;
    OA_CHECK(report_acceleration(facts).status.state == State::off_by_setting);
    facts.asked = true;
    facts.tier_accelerated = true;
    OA_CHECK(report_acceleration(facts).status.state == State::in_use);
}

void a_drop_for_memory_or_slow_frames_says_so() {
    auto facts = able();
    facts.memory_dropped = true;
    auto report = report_acceleration(facts);
    OA_CHECK(report.status.state == State::too_little_memory);
    OA_CHECK(!report.acceleration_unavailable);
    // In a shared game the drop shows rather than a wait, since the game's
    // end does not lift it.
    facts.shared_game = true;
    OA_CHECK(report_acceleration(facts).status.state == State::too_little_memory);
    facts = able();
    facts.slow_frames_dropped = true;
    report = report_acceleration(facts);
    OA_CHECK(report.status.state == State::slow_frames);
    OA_CHECK(!report.acceleration_unavailable);
    facts.replay = true;
    OA_CHECK(report_acceleration(facts).status.state == State::slow_frames);
    // Off says so first; in use again after Off then On, the drop no longer
    // shows.
    facts.replay = false;
    facts.asked = false;
    OA_CHECK(report_acceleration(facts).status.state == State::off_by_setting);
    facts.asked = true;
    facts.tier_accelerated = true;
    OA_CHECK(report_acceleration(facts).status.state == State::in_use);
    // From the facts the tier is decided from: the kind of drop.
    namespace policy = oa::app::render_policy;
    policy::TierInputs inputs{};
    inputs.renderer = true;
    inputs.memory = kAmpleMemory;
    inputs.setting_on = true;
    const policy::LadderState rung{};
    inputs.drop = policy::Drop::memory;
    OA_CHECK(
        report_acceleration(oa::app::tier_acceleration_facts(inputs, rung, false)).status.state ==
        State::too_little_memory
    );
    inputs.drop = policy::Drop::slow_frames;
    OA_CHECK(
        report_acceleration(oa::app::tier_acceleration_facts(inputs, rung, false)).status.state ==
        State::slow_frames
    );
}

void in_use_at_the_lowest_budget_says_nothing_smooths() {
    auto facts = able();
    facts.tier_accelerated = true;
    facts.no_smoothing = true;
    OA_CHECK(report_acceleration(facts).status.state == State::in_use_no_smoothing);
    facts.tier_accelerated = false;
    OA_CHECK(report_acceleration(facts).status.state == State::next_start);
}

void in_use_after_slow_frames_says_it_smooths_less() {
    auto facts = able();
    facts.tier_accelerated = true;
    facts.slow_frames_stepped = true;
    facts.reach = Reach::zoomed_in;
    auto report = report_acceleration(facts);
    OA_CHECK(report.status.state == State::in_use_less_smoothing);
    OA_CHECK(report.status.reach == Reach::zoomed_in && !report.acceleration_unavailable);
    // It comes before the lowest budget's line, which a step can reach.
    facts.no_smoothing = true;
    OA_CHECK(report_acceleration(facts).status.state == State::in_use_less_smoothing);
    // A drop for slow frames, the last rung, says so instead.
    facts.tier_accelerated = false;
    facts.slow_frames_dropped = true;
    OA_CHECK(report_acceleration(facts).status.state == State::slow_frames);
    // Off then On starts the ladder again from the top: in use, plainly.
    facts.slow_frames_dropped = false;
    facts.slow_frames_stepped = false;
    facts.no_smoothing = false;
    facts.tier_accelerated = true;
    OA_CHECK(report_acceleration(facts).status.state == State::in_use);
    // Off still says so first.
    facts.slow_frames_stepped = true;
    facts.asked = false;
    OA_CHECK(report_acceleration(facts).status.state == State::off_by_setting);
}

void the_reach_follows_the_rung() {
    using oa::app::acceleration_reach;
    using oa::app::render_policy::LadderState;
    using oa::app::render_policy::SceneBudget;
    LadderState rung{};
    rung.filtered_chrome = true;
    rung.budget = SceneBudget::none;
    rung.magnify = false;
    OA_CHECK(acceleration_reach(rung) == Reach::menus);
    rung.magnify = true;
    OA_CHECK(acceleration_reach(rung) == Reach::zoomed_in);
    rung.budget = SceneBudget::reduced;
    OA_CHECK(acceleration_reach(rung) == Reach::zoomed_out);
    rung.budget = SceneBudget::full;
    OA_CHECK(acceleration_reach(rung) == Reach::zoomed_out);
    // The NEAREST-chrome rung scales nothing.
    rung.filtered_chrome = false;
    OA_CHECK(acceleration_reach(rung) == Reach::nearest_zoomed_out);
    rung.budget = SceneBudget::none;
    OA_CHECK(acceleration_reach(rung) == Reach::nearest_none);
}

void the_tier_facts_give_the_status() {
    using oa::app::tier_acceleration_facts;
    namespace policy = oa::app::render_policy;
    policy::TierInputs inputs{};
    inputs.renderer = true;
    inputs.memory = kAmpleMemory;
    inputs.players_own_profile = true;
    inputs.setting_on = true;
    policy::LadderState rung{};
    rung.filtered_chrome = true;
    rung.magnify = true;
    rung.budget = policy::SceneBudget::reduced;
    // In use, at its reach.
    auto report = report_acceleration(tier_acceleration_facts(inputs, rung, true));
    OA_CHECK(report.status.state == State::in_use);
    OA_CHECK(report.status.reach == Reach::zoomed_out);
    rung.budget = policy::SceneBudget::none;
    report = report_acceleration(tier_acceleration_facts(inputs, rung, true));
    OA_CHECK(report.status.state == State::in_use_no_smoothing);
    OA_CHECK(report.status.reach == Reach::zoomed_in);
    // The flags decide over the setting.
    inputs.flag = policy::AccelerationFlag::off;
    OA_CHECK(
        report_acceleration(tier_acceleration_facts(inputs, rung, false)).status.state ==
        State::off_by_command_line
    );
    inputs.flag = policy::AccelerationFlag::on;
    inputs.setting_on = false;
    OA_CHECK(
        report_acceleration(tier_acceleration_facts(inputs, rung, true)).status.state ==
        State::in_use_no_smoothing
    );
    inputs.flag = policy::AccelerationFlag::none;
    OA_CHECK(
        report_acceleration(tier_acceleration_facts(inputs, rung, false)).status.state ==
        State::off_by_setting
    );
    inputs.setting_on = true;
    // A failed function test lacks a feature and locks the row; a small
    // texture limit too; SDL's software renderer has no usable card.
    inputs.function_test = policy::FunctionTest::failed;
    report = report_acceleration(tier_acceleration_facts(inputs, rung, false));
    OA_CHECK(report.status.state == State::lacks_feature && report.acceleration_unavailable);
    inputs.function_test = policy::FunctionTest::not_run;
    inputs.capability = policy::Capability::small_texture_limit;
    OA_CHECK(
        report_acceleration(tier_acceleration_facts(inputs, rung, false)).status.state ==
        State::lacks_feature
    );
    inputs.capability = policy::Capability::software_renderer;
    report = report_acceleration(tier_acceleration_facts(inputs, rung, false));
    OA_CHECK(report.status.state == State::no_usable_card);
    OA_CHECK(report.vertical_sync_unavailable);
    // --force-capable lifts both the renderer's and the environment's.
    inputs.force_capable = true;
    inputs.virtual_video_driver = true;
    report = report_acceleration(tier_acceleration_facts(inputs, rung, true));
    OA_CHECK(report.status.state == State::in_use_no_smoothing);
    OA_CHECK(!report.acceleration_unavailable && !report.vertical_sync_unavailable);
    // It never lifts a function test that drew wrongly.
    inputs.function_test = policy::FunctionTest::failed;
    report = report_acceleration(tier_acceleration_facts(inputs, rung, false));
    OA_CHECK(report.status.state == State::no_usable_card && report.acceleration_unavailable);
    inputs.function_test = policy::FunctionTest::not_run;
    inputs.force_capable = false;
    OA_CHECK(
        report_acceleration(tier_acceleration_facts(inputs, rung, false)).status.state ==
        State::environment_driver
    );
    inputs.virtual_video_driver = false;
    inputs.capability = policy::Capability::capable;
    // A drop after a driver failure, and a shared game or a replay waited for.
    inputs.drop = policy::Drop::driver_failure;
    report = report_acceleration(tier_acceleration_facts(inputs, rung, false));
    OA_CHECK(report.status.state == State::driver_failed && !report.acceleration_unavailable);
    // A rebuild after the failure that landed on SDL's software renderer,
    // the last of the walk: the failure explains it, so the row stays
    // within reach for Off then On.
    inputs.capability = policy::Capability::software_renderer;
    report = report_acceleration(tier_acceleration_facts(inputs, rung, false));
    OA_CHECK(report.status.state == State::driver_failed);
    OA_CHECK(!report.acceleration_unavailable);
    inputs.drop = policy::Drop::none;
    OA_CHECK(
        report_acceleration(tier_acceleration_facts(inputs, rung, false)).acceleration_unavailable
    );
    inputs.capability = policy::Capability::capable;
    inputs.drop = policy::Drop::driver_failure;
    inputs.drop = policy::Drop::none;
    inputs.match.kind = policy::MatchKind::replay;
    report = report_acceleration(tier_acceleration_facts(inputs, rung, false));
    OA_CHECK(report.status.state == State::waiting_for_game_end && report.status.replay);
    // Headless: no renderer, nothing ruled out by it; under 2 GiB first.
    inputs = {};
    inputs.memory = 0;
    inputs.setting_on = true;
    report = report_acceleration(tier_acceleration_facts(inputs, rung, false));
    OA_CHECK(report.status.state == State::needs_memory && report.acceleration_unavailable);
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
    a_renderer_that_lacks_a_feature_says_so();
    a_driver_that_failed_says_so_until_it_is_in_use_again();
    a_drop_for_memory_or_slow_frames_says_so();
    in_use_at_the_lowest_budget_says_nothing_smooths();
    in_use_after_slow_frames_says_it_smooths_less();
    the_reach_follows_the_rung();
    the_tier_facts_give_the_status();
    vertical_sync_is_out_of_reach_on_the_software_renderer();
    return oa::test::check_exit_status();
}
