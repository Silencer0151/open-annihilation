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
    // renderer always is.
    const bool capable =
        facts.force_capable || (facts.renderer_capable.value_or(true) && !facts.software_renderer);
    report.acceleration_unavailable = environment || !memory || !capable;
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
    else if (capable && (facts.shared_game || facts.replay) && !facts.tier_accelerated)
        report.status.state = AccelerationState::waiting_for_game_end;
    else if (!capable)
        report.status.state = AccelerationState::no_usable_card;
    else if (facts.tier_accelerated)
        report.status.state = AccelerationState::in_use;
    else
        report.status.state = AccelerationState::next_start;
    // The wait says whether it is for a replay.
    report.status.replay =
        report.status.state == AccelerationState::waiting_for_game_end && facts.replay;
    return report;
}

} // namespace oa::app
