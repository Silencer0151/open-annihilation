// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The accelerated tier's watch while it runs: the memory guard, which
// samples the system's memory about once a second and drops the tier for
// the rest of the run when memory runs short, and refuses a buffer of the
// tier's own that would leave too little free; and the step-down, fed the
// steady match frames the loop paces, which lowers the tier one rung at a
// time when frames are slow and, at its last rung, drops it for the rest of
// the run. Each step is logged once, and none ever rises again within the
// run. Nothing of it exists until the tier first switches on.
#include "oa/app/runtime.hpp"

#include "graphics_report.hpp"
#include "oa/app/frame_pacing.hpp"
#include "render_host.hpp"
#include "render_run.hpp"

#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

namespace oa::app {

namespace {

namespace policy = render_policy;

/// What the log says moved the rung when the step-down took a step.
constexpr std::string_view slow_frames_cause = "frames were slow";
/// What the log says moved the rung when the memory guard refused a buffer.
constexpr std::string_view refused_buffer_cause = "too little memory for a new buffer";

/// Returns the system's memory as the memory guard is to see it: the
/// sample a check forces, else the system's own.
///
/// @param forced the sample --check-renderer-ladder forces; empty for none
/// @return the sample; a figure the system does not report keeps its
///     `known` flag false
oa::platform::SystemMemorySample
system_memory(const std::optional<oa::platform::SystemMemorySample>& forced) {
    if (forced)
        return *forced;
    oa::platform::SystemMemorySample sample{};
    std::ignore = oa::platform::sample_system_memory(&sample);
    return sample;
}

/// Returns why the memory guard dropped the tier, for the log.
///
/// @param cause what tripped it
/// @return the reason
std::string memory_drop_reason(policy::MemoryGuardCause cause) {
    switch (cause) {
    case policy::MemoryGuardCause::committed:
        return "too little memory: the game's committed memory passed its threshold";
    case policy::MemoryGuardCause::free_memory:
        return "too little memory: free memory stayed under its threshold";
    case policy::MemoryGuardCause::pressure:
        return "too little memory: the system's memory pressure stayed critical";
    case policy::MemoryGuardCause::hard_faults:
        return "too little memory: hard page faults stayed high";
    case policy::MemoryGuardCause::none:
        break;
    }
    return "too little memory";
}

} // namespace

void Runtime::begin_accelerated_watch() {
    if (!render_run_ || render_run_->host == nullptr)
        return;
    auto& run = *render_run_;
    if (!run.watch) {
        run.watch = std::make_unique<AcceleratedWatch>();
        run.watch->memory = policy::make_memory_guard(run.host->tier_inputs().memory);
        run.watch->ticks_seen_ns = phase_times_.simulation;
        run.watch->draw_seen_ns = phase_times_.compose;
    }
    auto& watch = *run.watch;
    if (!watch.moved) {
        watch.step_down = policy::start_step_down(render_tier_rung());
        watch.slowed = false;
    }
    policy::resume_memory_guard(watch.memory);
}

void Runtime::watch_accelerated_memory() {
    if (!render_run_ || !render_run_->watch || !accelerated_presentation())
        return;
    auto& run = *render_run_;
    if (!frame_pacer_.started && !run.forced_memory)
        return;
    auto& guard = run.watch->memory;
    const uint64_t now = frame_pacing::steady_now_ns();
    if (!policy::memory_guard_sample_due(guard, now))
        return;
    if (policy::observe_memory(guard, system_memory(run.forced_memory), now) ==
        policy::MemoryGuardAction::drop_acceleration)
        drop_acceleration(memory_drop_reason(guard.tripped), policy::Drop::memory);
}

bool Runtime::accelerated_buffer_allowed(policy::AcceleratedBuffer buffer, uint64_t bytes) {
    if (!render_run_ || !render_run_->watch)
        return true;
    auto& run = *render_run_;
    if (!frame_pacer_.started && !run.forced_memory)
        return true;
    if (policy::memory_guard_allows(run.watch->memory, system_memory(run.forced_memory), bytes))
        return true;
    const policy::LadderState lowered = policy::rung_without(accelerated_.rung, buffer);
    if (lowered != accelerated_.rung)
        lower_accelerated_rung(lowered, refused_buffer_cause);
    return false;
}

void Runtime::lower_accelerated_rung(const policy::LadderState& rung, std::string_view cause) {
    if (!render_run_ || !render_run_->watch)
        return;
    auto& watch = *render_run_->watch;
    auto& state = accelerated_;
    const policy::LadderState before = state.rung;
    watch.step_down.state = rung;
    watch.moved = true;
    ++watch.steps;
    state.rung = rung;
    // What the lower rung no longer draws with goes at once; what it still
    // needs at a new size the next frame makes.
    if (before.magnify && !rung.magnify) {
        state.scene.reset();
        state.overlay_texture.reset();
        state.world_prescale.destroy();
        state.base = {};
        state.overlay = {};
        state.opaque_bands = {};
        state.uploaded_bands = {};
        state.overlay_uploaded = false;
        state.magnified = false;
    }
    // The scene a frame drew apart, and the terrain at its size, are held at
    // the largest the rung drew; a rung that draws a smaller scene, or none,
    // makes them again at what it needs.
    if ((before.magnify && !rung.magnify) || rung.budget < before.budget)
        free_accelerated_scene_buffers();
    // NEAREST chrome changes the HUD's and the screens' filter alone; the
    // card's magnification changes the scene's too.
    if (before.filtered_chrome && !rung.filtered_chrome) {
        state.hud_prescale.destroy();
        state.screen_prescale.destroy();
    }
    if (before.card != rung.card) {
        state.world_prescale.destroy();
        state.hud_prescale.destroy();
        state.screen_prescale.destroy();
    }
    std::string line(graphics_log_prefix);
    line += cause;
    line += ": ";
    line += policy::describe_step(before, rung);
    std::cout << line << '\n' << std::flush;
}

void Runtime::feed_render_step_down(uint64_t present_ns) {
    // The tier's own passes are counted afresh for each match frame.
    const uint64_t passes_ns = std::exchange(accelerated_.passes_ns, 0);
    const uint64_t area_ns = std::exchange(accelerated_.area_ns, 0);
    if (!render_run_ || !render_run_->watch)
        return;
    auto& run = *render_run_;
    auto& watch = *run.watch;
    policy::PresentedFrame frame;
    frame.tier = full_presentation()          ? policy::RenderTier::full
                 : accelerated_presentation() ? policy::RenderTier::accelerated
                                              : policy::RenderTier::standard;
    frame.match = screen_ == Screen::match && match_ != nullptr;
    frame.paced = frame_pacer_.started || run.forced_frame_ns.has_value();
    frame.previous_interval_ns = watch.previous_interval_ns;
    if (!policy::card_tier(frame.tier) || run.device_lost)
        return;
    const uint64_t now = frame_pacing::steady_now_ns();
    policy::FrameSample sample = present_frame_facts(now);
    sample.now_ns = now;
    sample.interval_ns = watch.frame_interval_ns;
    // A forced frame stands in for the measured interval alone, on a clock
    // of its own, after frames forced alike: whether it counts stays the
    // steady frames' rule.
    if (run.forced_frame_ns) {
        watch.forced_clock_ns += *run.forced_frame_ns;
        sample.now_ns = watch.forced_clock_ns;
        sample.interval_ns = *run.forced_frame_ns;
        frame.previous_interval_ns = *run.forced_frame_ns;
    }
    sample.tick_ns = watch.frame_ticks_ns;
    sample.draw_ns = watch.frame_draw_ns;
    sample.present_ns = present_ns;
    sample.passes_ns = passes_ns;
    sample.area_ns = area_ns;
    sample.paced_frames_per_second = frame_stats_notes().paced_frames_per_second;
    sample.allowance_ns = frame_pacing::frame_allowance_ns(
        policy::step_target_rate(sample.paced_frames_per_second), frame_pacing::FrameWait::precise
    );
    sample.kind = policy::frame_kind(
        match_zoom(), accelerated_.frame.method == SceneMethod::area, accelerated_.magnified
    );
    sample.clock_behind = match_timing_.actual_rate < match_timing_.requested_rate;
    switch (policy::feed_presented_frame(watch.step_down, frame, sample)) {
    case policy::StepResult::none:
        break;
    case policy::StepResult::stepped:
    case policy::StepResult::shed:
        watch.slowed = true;
        lower_accelerated_rung(watch.step_down.state, slow_frames_cause);
        break;
    case policy::StepResult::standard:
        // The last rung: the drop's own line logs it.
        watch.moved = true;
        ++watch.steps;
        drop_acceleration(std::string(slow_frames_cause), policy::Drop::slow_frames);
        break;
    }
}

} // namespace oa::app
