// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The accelerated tier's watch while it runs: the memory guard, which
// samples the system's memory about once a second and drops the tier for
// the rest of the run when memory runs short, and refuses a buffer of the
// tier's own that would leave too little free, which lowers the tier's rung
// to one without that buffer. Full goes first: the memory guard drops Full
// before Basic and judges Basic afresh, and a refused page or target drops
// Full. Nothing lowers a rung for slow frames: the player's settings hold,
// however slowly the machine draws them. Each step is logged once, and
// none ever rises again within the run. Nothing of it exists until the tier
// first switches on.
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
    }
    auto& watch = *run.watch;
    if (!watch.moved)
        watch.rung = render_tier_rung();
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
    if (policy::observe_memory(guard, system_memory(run.forced_memory), now) !=
        policy::MemoryGuardAction::drop_acceleration)
        return;
    // Full goes first, freeing its pages and targets; the guard then judges
    // Basic on the memory left, and drops it when memory stays short.
    if (full_presentation()) {
        drop_full(memory_drop_reason(guard.tripped), policy::FullDrop::memory);
        policy::retry_memory_guard(guard);
        return;
    }
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
    const policy::LadderState before = accelerated_.rung;
    const policy::LadderState lowered = policy::rung_without(before, buffer);
    if (lowered != before)
        lower_accelerated_rung(lowered, refused_buffer_cause);
    // A page or target of Full's refused drops Full for the run.
    if (before.full && !lowered.full)
        drop_full(std::string(refused_buffer_cause), policy::FullDrop::memory);
    return false;
}

bool Runtime::accelerated_buffer_fits(uint64_t bytes) {
    if (!render_run_ || !render_run_->watch)
        return true;
    auto& run = *render_run_;
    if (!frame_pacer_.started && !run.forced_memory)
        return true;
    return policy::memory_guard_allows(run.watch->memory, system_memory(run.forced_memory), bytes);
}

void Runtime::lower_accelerated_rung(const policy::LadderState& rung, std::string_view cause) {
    if (!render_run_ || !render_run_->watch)
        return;
    auto& watch = *render_run_->watch;
    auto& state = accelerated_;
    const policy::LadderState before = state.rung;
    watch.rung = rung;
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

} // namespace oa::app
