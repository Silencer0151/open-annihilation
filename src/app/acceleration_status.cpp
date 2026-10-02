// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/app/acceleration_status.hpp"

#include "oa/app/render_policy.hpp"

namespace oa::app {

namespace settings = oa::ui::engine_settings;

bool enough_memory_for_acceleration(uint64_t physical_memory) noexcept {
    return physical_memory >= render_policy::smallest_accelerated_memory;
}

AccelerationReport report_acceleration(const AccelerationFacts& facts) noexcept {
    using settings::AccelerationState;
    AccelerationReport report{};
    const bool flag_on = facts.flag.value_or(false);
    // The environment's driver keeps the frames on the processor unless a
    // flag or a check lifts it.
    const bool environment = facts.environment_driver && !flag_on && !facts.force_capable;
    const bool memory = enough_memory_for_acceleration(facts.physical_memory);
    // A renderer nothing has looked at yet is not ruled out; SDL's software
    // renderer always is. A function test that drew wrongly rules it out
    // even where --force-capable lifts the rest.
    const bool capable = !facts.function_test_failed &&
                         (facts.force_capable ||
                          (facts.renderer_capable.value_or(true) && !facts.software_renderer));
    // SDL's software renderer has no graphics card at all, whatever it lacks.
    const bool lacks_feature = facts.lacks_feature && !facts.software_renderer;
    // A driver that failed in this run explains the renderer it left, which
    // switching Off then On may try again, so it locks nothing.
    report.acceleration_unavailable = environment || !memory || (!capable && !facts.driver_failed);
    report.vertical_sync_unavailable = (facts.software_renderer && !facts.force_capable) ||
                                       facts.vertical_sync_resets_device ||
                                       facts.vertical_sync_refused;
    report.status.reach = facts.reach;
    // Under 2 GiB the processor draws whatever the setting or the flags say,
    // and the status says why first.
    if (!memory)
        report.status.state = AccelerationState::needs_memory;
    else if (facts.flag == false)
        report.status.state = AccelerationState::off_by_command_line;
    else if (!facts.asked)
        report.status.state = AccelerationState::off_by_setting;
    else if (environment)
        report.status.state = AccelerationState::environment_driver;
    else if (
        capable && (facts.shared_game || facts.replay) && !facts.tier_accelerated &&
        !facts.driver_failed && !facts.memory_dropped && !facts.slow_frames_dropped
    )
        report.status.state = AccelerationState::waiting_for_game_end;
    else if (facts.driver_failed && !facts.tier_accelerated)
        report.status.state = AccelerationState::driver_failed;
    else if (facts.memory_dropped && !facts.tier_accelerated)
        report.status.state = AccelerationState::too_little_memory;
    else if (facts.slow_frames_dropped && !facts.tier_accelerated)
        report.status.state = AccelerationState::slow_frames;
    else if (!capable)
        report.status.state =
            lacks_feature ? AccelerationState::lacks_feature : AccelerationState::no_usable_card;
    else if (facts.tier_accelerated && facts.slow_frames_stepped)
        report.status.state = AccelerationState::in_use_less_smoothing;
    else if (facts.tier_accelerated)
        report.status.state =
            facts.no_smoothing ? AccelerationState::in_use_no_smoothing : AccelerationState::in_use;
    else
        report.status.state = AccelerationState::next_start;
    // The wait says whether it is for a replay.
    report.status.replay =
        report.status.state == AccelerationState::waiting_for_game_end && facts.replay;
    return report;
}

AccelerationFacts tier_acceleration_facts(
    const render_policy::TierInputs& inputs,
    const render_policy::LadderState& rung,
    bool tier_accelerated
) noexcept {
    using render_policy::AccelerationFlag;
    using render_policy::Capability;
    AccelerationFacts facts{};
    if (inputs.flag != AccelerationFlag::none)
        facts.flag = inputs.flag == AccelerationFlag::on;
    facts.asked = facts.flag.value_or(inputs.setting_on);
    facts.force_capable = inputs.force_capable;
    facts.environment_driver = inputs.render_driver_named || inputs.virtual_video_driver;
    facts.physical_memory = inputs.memory;
    const bool test_failed = inputs.function_test == render_policy::FunctionTest::failed;
    if (inputs.renderer)
        facts.renderer_capable = inputs.capability == Capability::capable && !test_failed;
    facts.function_test_failed = test_failed;
    facts.software_renderer = inputs.capability == Capability::software_renderer;
    facts.lacks_feature = inputs.capability == Capability::small_texture_limit || test_failed;
    facts.driver_failed = inputs.drop == render_policy::Drop::driver_failure;
    facts.memory_dropped = inputs.drop == render_policy::Drop::memory;
    facts.slow_frames_dropped = inputs.drop == render_policy::Drop::slow_frames;
    facts.shared_game = inputs.match.kind == render_policy::MatchKind::shared_game;
    facts.replay = inputs.match.kind == render_policy::MatchKind::replay;
    facts.tier_accelerated = tier_accelerated;
    facts.no_smoothing = rung.budget == render_policy::SceneBudget::none;
    facts.reach = acceleration_reach(rung);
    return facts;
}

settings::AccelerationReach acceleration_reach(const render_policy::LadderState& rung) noexcept {
    using settings::AccelerationReach;
    const bool smoothed = rung.budget != render_policy::SceneBudget::none;
    if (!rung.filtered_chrome)
        return smoothed ? AccelerationReach::nearest_zoomed_out : AccelerationReach::nearest_none;
    if (smoothed)
        return AccelerationReach::zoomed_out;
    return rung.magnify ? AccelerationReach::zoomed_in : AccelerationReach::menus;
}

} // namespace oa::app
