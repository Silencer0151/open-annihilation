// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/app/render_policy.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace oa::app::render_policy {

namespace {} // namespace

// ---------------------------------------------------------------------------
// The renderer's facts and its capability

uint32_t texture_limit(const DriverTraits& driver, uint32_t reported, uint32_t device) noexcept {
    switch (driver.texture_limit_source) {
    case TextureLimitSource::reported:
        return reported;
    case TextureLimitSource::fixed_report:
        if (device != 0)
            return reported == unlimited_texture_size ? device : std::min(reported, device);
        if (reported == unlimited_texture_size)
            return unconfirmed_texture_size_cap;
        return std::min(reported, unconfirmed_texture_size_cap);
    }
    return reported;
}

Capability assess_renderer(
    const RendererFacts& renderer, bool legacy_windows, bool accept_virtual_adapter
) noexcept {
    if (renderer.driver.software)
        return Capability::software_renderer;
    if (legacy_windows && !renderer.driver.capable_before_vista)
        return Capability::before_vista_driver;
    if (renderer.max_texture_size != unlimited_texture_size &&
        renderer.max_texture_size < smallest_capable_texture_size)
        return Capability::small_texture_limit;
    if (renderer.under_wine)
        return Capability::under_wine;
    if (renderer.software_rasteriser)
        return Capability::software_rasteriser;
    if (renderer.virtual_adapter && !accept_virtual_adapter)
        return Capability::virtual_adapter;
    if (!renderer.adapter_known && renderer.driver.adapter_required)
        return Capability::unknown_adapter;
    return Capability::capable;
}

// ---------------------------------------------------------------------------
// Choosing the tier

void begin_match(
    SharedMatchGate& gate, MatchKind kind, bool accelerated_now, bool full_now
) noexcept {
    gate.kind = kind;
    gate.accelerated = kind != MatchKind::none && accelerated_now;
    gate.full = gate.accelerated && full_now;
}

void note_match_frame(SharedMatchGate& gate, RenderTier tier, bool device_lost) noexcept {
    if (gate.kind == MatchKind::none)
        return;
    if (tier == RenderTier::standard && !device_lost)
        gate.accelerated = false;
    // A lost device's frames keep Full waiting for its reset, as they keep
    // Basic; any other tier below Full keeps Full away until the match ends.
    if (tier != RenderTier::full && !device_lost)
        gate.full = false;
}

void stop_until_match_end(SharedMatchGate& gate) noexcept {
    if (gate.kind != MatchKind::none) {
        gate.accelerated = false;
        gate.full = false;
    }
}

void end_match(SharedMatchGate& gate) noexcept {
    gate = SharedMatchGate{};
}

bool records_on_disk(bool players_own_profile, bool render_driver_named) noexcept {
    return players_own_profile && !render_driver_named;
}

TierDecision decide_render_tier(const TierInputs& inputs) noexcept {
    const auto standard = [](TierReason reason) {
        return TierDecision{RenderTier::standard, reason};
    };
    const bool flag_on = flag_asks_for_card(inputs.flag);
    const bool on_disk = records_on_disk(inputs.players_own_profile, inputs.render_driver_named);
    if (!inputs.renderer)
        return standard(TierReason::no_renderer);
    if (inputs.director_frame)
        return standard(TierReason::director_frame);
    // Memory that is not reported counts as less, and no flag lifts it.
    if (inputs.memory < smallest_accelerated_memory)
        return standard(TierReason::memory);
    if (inputs.flag == AccelerationFlag::off)
        return standard(TierReason::flag_off);
    if ((inputs.render_driver_named || inputs.virtual_video_driver) && !flag_on &&
        !inputs.force_capable)
        return standard(TierReason::environment);
    // Basic and Full both ask for the accelerated tier; which of the two
    // runs is decided once every condition holds.
    const HardwareAcceleration asked = acceleration_asked(inputs.flag, inputs.setting);
    if (asked == HardwareAcceleration::off)
        return standard(TierReason::setting_off);
    if (inputs.capability != Capability::capable && !inputs.force_capable)
        return standard(TierReason::not_capable);
    if (inputs.function_test == FunctionTest::failed)
        return standard(TierReason::function_test_failed);
    if (inputs.records_unreadable_after_unclean_start && on_disk)
        return standard(TierReason::records_unreadable);
    if (inputs.accelerated_unusable_record && !flag_on)
        return standard(TierReason::accelerated_unusable);
    if (inputs.drop != Drop::none)
        return standard(TierReason::dropped);
    if (inputs.device_lost)
        return standard(TierReason::device_lost);
    if (inputs.match.kind != MatchKind::none && !inputs.match.accelerated)
        return standard(TierReason::waiting_for_match_end);
    if (inputs.function_test == FunctionTest::trial_unwritten && on_disk)
        return standard(TierReason::trial_unwritten);
    if (inputs.function_test != FunctionTest::passed)
        return standard(TierReason::function_test_due);
    if (asked != HardwareAcceleration::full)
        return TierDecision{
            RenderTier::accelerated, TierReason::accelerated, FullReason::not_asked
        };
    const auto basic = [](FullReason reason) {
        return TierDecision{RenderTier::accelerated, TierReason::accelerated, reason};
    };
    if (inputs.full_unusable_record && inputs.flag != AccelerationFlag::full)
        return basic(FullReason::unusable_record);
    if (inputs.full_drop != FullDrop::none)
        return basic(FullReason::dropped);
    if (inputs.match.kind != MatchKind::none && !inputs.match.full)
        return basic(FullReason::waiting_for_match_end);
    return TierDecision{RenderTier::full, TierReason::accelerated, FullReason::full};
}

bool function_test_may_run(const TierInputs& inputs) noexcept {
    return decide_render_tier(inputs).reason == TierReason::function_test_due;
}

// ---------------------------------------------------------------------------
// Acting on the tier

bool windowless_video_driver(std::string_view video_driver) noexcept {
    return std::any_of(
        windowless_video_drivers.begin(),
        windowless_video_drivers.end(),
        [video_driver](std::string_view windowless) {
            return video_driver.size() == windowless.size() &&
                   std::equal(
                       video_driver.begin(),
                       video_driver.end(),
                       windowless.begin(),
                       [](char letter, char lower) {
                           return (letter >= 'A' && letter <= 'Z'
                                       ? static_cast<char>(letter - 'A' + 'a')
                                       : letter) == lower;
                       }
                   );
        }
    );
}

AccelerationFlag acceleration_flag(std::optional<HardwareAcceleration> flag) noexcept {
    if (!flag)
        return AccelerationFlag::none;
    switch (*flag) {
    case HardwareAcceleration::off:
        return AccelerationFlag::off;
    case HardwareAcceleration::basic:
        return AccelerationFlag::basic;
    case HardwareAcceleration::full:
        return AccelerationFlag::full;
    }
    return AccelerationFlag::none;
}

HardwareAcceleration
acceleration_asked(AccelerationFlag flag, HardwareAcceleration setting) noexcept {
    switch (flag) {
    case AccelerationFlag::none:
        return setting;
    case AccelerationFlag::off:
        return HardwareAcceleration::off;
    case AccelerationFlag::basic:
        return HardwareAcceleration::basic;
    case AccelerationFlag::full:
        return HardwareAcceleration::full;
    }
    return setting;
}

bool flag_asks_for_card(AccelerationFlag flag) noexcept {
    return flag == AccelerationFlag::basic || flag == AccelerationFlag::full;
}

TierAction tier_action(const TierDecision& decision, bool presentation_on) noexcept {
    if (decision.reason == TierReason::function_test_due)
        return TierAction::run_function_test;
    const bool accelerated = card_tier(decision.tier);
    if (accelerated && !presentation_on)
        return TierAction::switch_on;
    if (!accelerated && presentation_on)
        return TierAction::switch_off;
    return TierAction::none;
}

void forget_failures(TierInputs& inputs) noexcept {
    if (inputs.function_test == FunctionTest::failed ||
        inputs.function_test == FunctionTest::trial_unwritten)
        inputs.function_test = FunctionTest::not_run;
    if (inputs.drop != Drop::memory)
        inputs.drop = Drop::none;
    if (inputs.full_drop != FullDrop::memory)
        inputs.full_drop = FullDrop::none;
}

TierStep step_tier(TierInputs& inputs, bool presentation_on, const FunctionTestHooks& test) {
    TierStep step;
    step.decision = decide_render_tier(inputs);
    if (step.decision.reason == TierReason::function_test_due && test.run != nullptr) {
        inputs.function_test = test.run(test.context);
        step.decision = decide_render_tier(inputs);
    }
    note_match_frame(
        inputs.match, step.decision.tier, step.decision.reason == TierReason::device_lost
    );
    step.action = tier_action(step.decision, presentation_on);
    // A test still due, with nothing that ran it, leaves the frame standard.
    if (step.action == TierAction::run_function_test)
        step.action = presentation_on ? TierAction::switch_off : TierAction::none;
    return step;
}

// ---------------------------------------------------------------------------
// Creating the renderer

namespace {

/// Tells whether a list of names holds one.
///
/// @param names the list
/// @param name the name
/// @return true when name is among names
bool contains(std::span<const std::string_view> names, std::string_view name) noexcept {
    return std::find(names.begin(), names.end(), name) != names.end();
}

/// Returns an ASCII letter in lower case.
///
/// @param letter a character
/// @return the letter in lower case; any other character unchanged
char lower_ascii(char letter) noexcept {
    return letter >= 'A' && letter <= 'Z' ? static_cast<char>(letter - 'A' + 'a') : letter;
}

/// Tells whether two driver names are the same in any letter case, as SDL
/// matches the names SDL_RENDER_DRIVER gives.
///
/// @param first one name
/// @param second the other
/// @return true when they differ at most in the case of ASCII letters
bool same_driver_name(std::string_view first, std::string_view second) noexcept {
    return first.size() == second.size() &&
           std::equal(first.begin(), first.end(), second.begin(), [](char left, char right) {
               return lower_ascii(left) == lower_ascii(right);
           });
}

/// Tells whether a driver name is SDL's software renderer's, in any letter
/// case.
///
/// @param driver the name
/// @return true for software
bool is_software(std::string_view driver) noexcept {
    return same_driver_name(driver, software_driver);
}

/// Returns the order a walk goes through.
///
/// @param walk the walk
/// @param inputs the drivers
/// @return SDL_RENDER_DRIVER's list for an environment rebuild, else SDL's order
std::span<const std::string_view>
walk_order(const CreationWalk& walk, const CreationInputs& inputs) noexcept {
    return walk.kind == WalkKind::environment_rebuild ? inputs.environment_order : inputs.sdl_order;
}

/// Tells whether a walk skips a driver because of a record.
///
/// @param walk the walk
/// @param inputs the drivers and the records
/// @param driver the driver
/// @return true for a recorded hardware driver while the records are read
bool skipped_by_record(
    const CreationWalk& walk, const CreationInputs& inputs, std::string_view driver
) noexcept {
    if (walk.records_ignored || is_software(driver))
        return false;
    if (walk.kind == WalkKind::environment_start || walk.kind == WalkKind::environment_rebuild)
        return false;
    return contains(inputs.failed_drivers, driver);
}

/// Returns the drivers SDL's software renderer may present through: SDL's
/// order less software, the drivers the walk skips by record and the
/// rebuild's failed driver.
///
/// @param walk the walk
/// @param inputs the drivers and the records
/// @return the framebuffer hint's list, or its first name alone before SDL
///     3.4; empty when no driver is left
std::string trusted_driver_list(const CreationWalk& walk, const CreationInputs& inputs) {
    std::string list;
    for (const std::string_view driver : inputs.sdl_order) {
        if (is_software(driver) || same_driver_name(driver, walk.failed_driver) ||
            skipped_by_record(walk, inputs, driver))
            continue;
        if (!list.empty()) {
            if (!inputs.hint_takes_list)
                break;
            list += ',';
        }
        list += driver;
    }
    return list;
}

/// Starts a walk over again from the top of SDL's order with the records
/// ignored.
///
/// @param[in,out] walk the walk
void begin_second_walk(CreationWalk& walk) noexcept {
    walk.position = 0;
    walk.records_ignored = true;
    walk.hardware_missed = !walk.failed_driver.empty() && !is_software(walk.failed_driver);
    walk.skipped_by_record = false;
    walk.software_appended = false;
    walk.attempted = false;
    walk.attempt_was_hardware = false;
}

/// Tells whether a walk that found nothing able to present walks again.
///
/// @param walk the walk
/// @return true for a first walk of a start that skipped a driver by a
///     record, and for the first walk of every rebuild outside
///     SDL_RENDER_DRIVER
bool walks_again(const CreationWalk& walk) noexcept {
    if (walk.records_ignored)
        return false;
    if (walk.kind == WalkKind::rebuild)
        return true;
    return walk.kind == WalkKind::start && walk.skipped_by_record;
}

} // namespace

CreationWalk start_creation(const CreationInputs& inputs) noexcept {
    CreationWalk walk;
    walk.kind = inputs.render_driver_named ? WalkKind::environment_start : WalkKind::start;
    return walk;
}

CreationWalk start_rebuild(const CreationInputs& inputs, std::string_view failed_driver) noexcept {
    CreationWalk walk;
    walk.kind = inputs.render_driver_named ? WalkKind::environment_rebuild : WalkKind::rebuild;
    walk.failed_driver = failed_driver;
    walk.hardware_missed = !is_software(failed_driver);
    const std::span<const std::string_view> order = walk_order(walk, inputs);
    const auto failed =
        std::find_if(order.begin(), order.end(), [failed_driver](std::string_view driver) {
            return same_driver_name(driver, failed_driver);
        });
    walk.position = failed == order.end() ? 0 : static_cast<size_t>(failed - order.begin()) + 1;
    return walk;
}

Attempt next_attempt(CreationWalk& walk, const CreationInputs& inputs) {
    if (walk.attempted && walk.attempt_was_hardware)
        walk.hardware_missed = true;
    walk.attempted = false;
    walk.attempt_was_hardware = false;
    if (walk.finished)
        return Attempt{};
    if (walk.kind == WalkKind::environment_start) {
        if (walk.position != 0) {
            walk.finished = true;
            return Attempt{};
        }
        walk.position = 1;
        walk.attempted = true;
        Attempt attempt;
        attempt.kind = AttemptKind::sdl_choice;
        return attempt;
    }
    for (;;) {
        const std::span<const std::string_view> order = walk_order(walk, inputs);
        std::string_view driver;
        bool found = false;
        while (walk.position < order.size()) {
            const std::string_view candidate = order[walk.position++];
            if (walk.kind == WalkKind::environment_rebuild) {
                // The tester's list may name the failed driver again, or
                // name it in another letter case than SDL's.
                if (same_driver_name(candidate, walk.failed_driver))
                    continue;
                if (is_software(candidate))
                    walk.software_appended = true;
            }
            if (skipped_by_record(walk, inputs, candidate)) {
                walk.skipped_by_record = true;
                walk.hardware_missed = true;
                continue;
            }
            driver = candidate;
            found = true;
            break;
        }
        if (!found && walk.kind == WalkKind::environment_rebuild && !walk.software_appended) {
            walk.software_appended = true;
            driver = software_driver;
            found = true;
        }
        if (!found) {
            if (walks_again(walk)) {
                begin_second_walk(walk);
                continue;
            }
            walk.finished = true;
            return Attempt{};
        }
        Attempt attempt;
        attempt.kind = AttemptKind::driver;
        attempt.driver = driver;
        attempt.records_ignored = walk.records_ignored;
        if (is_software(driver) && walk.hardware_missed) {
            if (inputs.native_window_framebuffer) {
                attempt.set_framebuffer_hint = true;
                attempt.framebuffer_hint = std::string(framebuffer_hint_window);
            } else {
                std::string list = trusted_driver_list(walk, inputs);
                if (!list.empty()) {
                    attempt.set_framebuffer_hint = true;
                    attempt.framebuffer_hint = std::move(list);
                } else if (walks_again(walk)) {
                    // Nothing is left for software to present through.
                    begin_second_walk(walk);
                    continue;
                }
                // Otherwise the hint stays unset: SDL's own choice.
            }
        }
        walk.attempted = true;
        walk.attempt_was_hardware = !is_software(driver);
        return attempt;
    }
}

// ---------------------------------------------------------------------------
// Present stalls

StallAction
note_present(StallWatch& watch, uint64_t steady_ns, uint64_t present_ns, RenderTier tier) noexcept {
    if (present_ns <= stall_present_ns)
        return StallAction::none;
    // Forget the stalls that fell out of the window. Fewer than
    // stalls_that_act are ever kept: the one that makes them act empties
    // the watch.
    uint32_t kept = 0;
    for (uint32_t index = 0; index < watch.stalls; ++index) {
        const uint64_t time = watch.stall_times_ns[index];
        if (steady_ns >= time && steady_ns - time < stall_window_ns)
            watch.stall_times_ns[kept++] = time;
    }
    watch.stalls = kept;
    watch.stall_times_ns[watch.stalls++] = steady_ns;
    if (watch.stalls < stalls_that_act)
        return StallAction::none;
    watch.stalls = 0;
    if (card_tier(tier))
        return StallAction::drop;
    if (watch.logged)
        return StallAction::none;
    watch.logged = true;
    return StallAction::log;
}

// ---------------------------------------------------------------------------
// Device resets

bool note_device_reset(ResetWatch& watch, uint64_t now_ms) noexcept {
    // Forget the resets that fell out of the window. Fewer than
    // resets_that_rebuild are ever kept: the one that makes a rebuild
    // empties the watch.
    uint32_t kept = 0;
    for (uint32_t index = 0; index < watch.resets; ++index) {
        const uint64_t time = watch.reset_times_ms[index];
        if (now_ms >= time && now_ms - time < reset_window_ms)
            watch.reset_times_ms[kept++] = time;
    }
    watch.resets = kept;
    watch.reset_times_ms[watch.resets++] = now_ms;
    if (watch.resets < resets_that_rebuild)
        return false;
    watch.resets = 0;
    return true;
}

// ---------------------------------------------------------------------------
// Texture formats of the layers

LayerFormats layer_formats(bool software, bool rgb565_window, bool render_driver_named) noexcept {
    LayerFormats formats;
    if (software) {
        formats.opaque = rgb565_window ? LayerFormat::rgb565 : LayerFormat::xrgb8888;
        return formats;
    }
    if (render_driver_named)
        return formats;
    formats.opaque = LayerFormat::argb8888;
    formats.loading = LayerFormat::argb8888;
    formats.front_end = LayerFormat::argb8888;
    return formats;
}

// ---------------------------------------------------------------------------
// The step-down ladder

namespace {

/// Tells whether the card's magnification is in use: by the magnified
/// world or by the filtered chrome.
///
/// @param state the rung
/// @return true when lowering the card's magnification changes something
bool card_in_use(const LadderState& state) noexcept {
    return state.magnify || state.filtered_chrome;
}

/// Returns the card's magnification one rung down.
///
/// @param card the card's magnification
/// @return PIXELART and the quarter budget fall to plain LINEAR, the whole
///     budget to the quarter
CardFilter lower_card(CardFilter card) noexcept {
    switch (card) {
    case CardFilter::pixelart:
    case CardFilter::prescale_quarter:
        return CardFilter::linear;
    case CardFilter::prescale_full:
        return CardFilter::prescale_quarter;
    case CardFilter::linear:
        return CardFilter::linear;
    }
    return CardFilter::linear;
}

/// Returns the card's magnification one rung up, no higher than a ceiling.
///
/// @param card the card's magnification
/// @param ceiling the highest allowed
/// @return the next rung toward the ceiling, or card at the ceiling
CardFilter raise_card(CardFilter card, CardFilter ceiling) noexcept {
    if (card == ceiling)
        return card;
    if (ceiling == CardFilter::pixelart)
        return CardFilter::pixelart;
    if (card == CardFilter::linear)
        return CardFilter::prescale_quarter;
    return ceiling;
}

} // namespace

SceneBudget start_budget(const StartInputs& machine) noexcept {
    if (machine.processors <= budget_none_most_processors || machine.other_arm ||
        machine.legacy_windows || machine.run_class == ClassTesting::untested)
        return SceneBudget::none;
    if (machine.processors == budget_reduced_processors ||
        machine.run_class == ClassTesting::tested)
        return SceneBudget::reduced;
    return SceneBudget::full;
}

LadderState start_rung(const StartInputs& machine) noexcept {
    LadderState state;
    state.full = false;
    state.budget = start_budget(machine);
    state.method = ZoomOutMethod::area;
    state.blend_allowed = machine.blend_available && !machine.driver.blend_excluded &&
                          machine.memory > most_memory_without_blend;
    state.magnify = !(machine.legacy_windows && !magnify_measured_before_vista) &&
                    !(state.budget == SceneBudget::none && !magnify_measured_at_budget_none);
    state.filtered_chrome = true;
    if (machine.pixelart)
        state.card = CardFilter::pixelart;
    else if (machine.light_machine || machine.raspberry_pi)
        state.card = CardFilter::prescale_quarter;
    else
        state.card = CardFilter::prescale_full;
    state.standard = false;
    return state;
}

LadderState start_ceiling(const StartInputs& machine) noexcept {
    LadderState ceiling = start_rung(machine);
    // A start at the magnify-off rung rises no higher.
    if (ceiling.magnify)
        ceiling.budget = SceneBudget::full;
    ceiling.method = ZoomOutMethod::area;
    return ceiling;
}

LadderState step_up(const LadderState& state, const LadderState& ceiling) noexcept {
    LadderState raised = state;
    raised.standard = false;
    if (card_in_use(raised) && raised.card < ceiling.card) {
        raised.card = raise_card(raised.card, ceiling.card);
        return raised;
    }
    if (!raised.filtered_chrome && ceiling.filtered_chrome) {
        raised.filtered_chrome = true;
        return raised;
    }
    if (!raised.magnify && ceiling.magnify) {
        raised.magnify = true;
        return raised;
    }
    if (raised.budget < ceiling.budget) {
        raised.budget = static_cast<SceneBudget>(static_cast<uint8_t>(raised.budget) + 1);
        return raised;
    }
    if (raised.method == ZoomOutMethod::blend) {
        raised.method = ZoomOutMethod::area;
        return raised;
    }
    return raised;
}

LadderState resume_rung(
    const StartInputs& machine, const LadderState& remembered, uint32_t median_percent
) noexcept {
    const LadderState ceiling = start_ceiling(machine);
    LadderState state = remembered;
    state.standard = false;
    state.blend_allowed = ceiling.blend_allowed;
    if (!state.blend_allowed)
        state.method = ZoomOutMethod::area;
    state.budget = std::min(state.budget, ceiling.budget);
    state.magnify = state.magnify && ceiling.magnify;
    state.filtered_chrome = state.filtered_chrome && ceiling.filtered_chrome;
    if (ceiling.card == CardFilter::pixelart)
        state.card = state.card == CardFilter::pixelart ? CardFilter::pixelart : CardFilter::linear;
    else if (state.card == CardFilter::pixelart || state.card > ceiling.card)
        state.card = ceiling.card;
    if (median_percent < headroom_percent)
        state = step_up(state, ceiling);
    return state;
}

bool steady_frame(const FrameSample& sample) noexcept {
    return !sample.idle && sample.window_active && !sample.settling && !sample.match_warming;
}

LadderState rung_without(const LadderState& state, AcceleratedBuffer buffer) noexcept {
    LadderState lowered = state;
    switch (buffer) {
    case AcceleratedBuffer::scene:
        lowered.magnify = false;
        break;
    case AcceleratedBuffer::prescale:
        if (lowered.card == CardFilter::prescale_full ||
            lowered.card == CardFilter::prescale_quarter)
            lowered.card = lower_card(lowered.card);
        break;
    case AcceleratedBuffer::card_pages:
    case AcceleratedBuffer::card_targets:
        lowered.full = false;
        break;
    }
    return lowered;
}

std::string_view describe_step(const LadderState& before, const LadderState& after) noexcept {
    if (after.standard && !before.standard)
        return "the processor draws everything for the rest of the run";
    if (before.full && !after.full)
        return "the graphics card no longer draws the battlefield";
    if (before.budget != SceneBudget::none && after.budget == SceneBudget::none && before.magnify &&
        !after.magnify)
        return "the zoomed-out view is no longer smoothed and the graphics card no longer "
               "magnifies the battlefield";
    if (before.method != after.method)
        return "the graphics card blends the zoomed-out view";
    if (before.budget != after.budget)
        return after.budget == SceneBudget::none ? "the zoomed-out view is no longer smoothed"
                                                 : "the zoomed-out view is smoothed less";
    if (before.magnify != after.magnify)
        return "the graphics card no longer magnifies the battlefield";
    if (before.filtered_chrome != after.filtered_chrome)
        return "the interface is scaled without filtering";
    if (before.card != after.card)
        return after.card == CardFilter::linear
                   ? "the graphics card magnifies by plain linear filtering"
                   : "the graphics card magnifies within a quarter of its budget";
    return "nothing changed";
}

// ---------------------------------------------------------------------------
// Native pixel density

DensityDecision decide_native_density(const DensityInputs& inputs) noexcept {
    const auto window_system = [](DensityReason reason) { return DensityDecision{false, reason}; };
    // Memory that is not reported counts as less, and no flag lifts it.
    if (inputs.memory < smallest_accelerated_memory)
        return window_system(DensityReason::memory);
    if (inputs.flag == AccelerationFlag::off)
        return window_system(DensityReason::flag_off);
    if (inputs.platform_native)
        return DensityDecision{true, DensityReason::platform};
    if (inputs.asked)
        return DensityDecision{true, DensityReason::asked};
    if (inputs.chosen)
        return DensityDecision{true, DensityReason::chosen};
    if (inputs.render_driver_named || inputs.virtual_video_driver)
        return window_system(DensityReason::environment);
    if (inputs.unattended)
        return window_system(DensityReason::unattended);
    if (inputs.capture)
        return window_system(DensityReason::capture);
    if (acceleration_asked(inputs.flag, inputs.setting) == HardwareAcceleration::off)
        return window_system(DensityReason::setting_off);
    if (!inputs.class_measured)
        return window_system(DensityReason::class_unmeasured);
    if (inputs.budget == SceneBudget::none)
        return window_system(DensityReason::budget_none);
    // A rung at or below the magnify-off rung has magnify off.
    if (inputs.remembered && (!inputs.remembered->magnify || inputs.remembered->standard))
        return window_system(DensityReason::remembered_rung);
    if (!inputs.record)
        return window_system(DensityReason::no_record);
    return DensityDecision{true, DensityReason::native};
}

// ---------------------------------------------------------------------------
// Chrome filtering and the prescale budget

uint64_t prescale_budget(CardFilter card) noexcept {
    switch (card) {
    case CardFilter::prescale_full:
        return prescale_budget_pixels;
    case CardFilter::prescale_quarter:
        return prescale_budget_pixels / 4;
    case CardFilter::linear:
    case CardFilter::pixelart:
        return 0;
    }
    return 0;
}

uint32_t prescale_factor(
    const PrescaleBudget& budget, uint32_t width, uint32_t height, double scale
) noexcept {
    if (width == 0 || height == 0 || !(scale > 0.0))
        return 1;
    const double rounded_up = std::ceil(scale);
    uint32_t factor =
        rounded_up >= double(UINT16_MAX) ? UINT16_MAX : static_cast<uint32_t>(rounded_up);
    const uint64_t left = budget.charged >= budget.limit ? 0 : budget.limit - budget.charged;
    const uint64_t source = uint64_t{width} * height;
    while (factor > 1 && source * factor * factor > left)
        --factor;
    return std::max<uint32_t>(factor, 1);
}

bool charge_prescale(PrescaleBudget& budget, uint64_t pixels) noexcept {
    if (budget.charged > budget.limit || pixels > budget.limit - budget.charged)
        return false;
    budget.charged += pixels;
    return true;
}

void release_prescale(PrescaleBudget& budget, uint64_t pixels) noexcept {
    budget.charged = pixels > budget.charged ? 0 : budget.charged - pixels;
}

ScaleFilter chrome_filter(const LadderState& state, double scale) noexcept {
    if (state.standard || !state.filtered_chrome || std::floor(scale) == scale)
        return ScaleFilter::nearest;
    switch (state.card) {
    case CardFilter::pixelart:
        return ScaleFilter::pixelart;
    case CardFilter::prescale_full:
    case CardFilter::prescale_quarter:
        return ScaleFilter::sharp_bilinear;
    case CardFilter::linear:
        return ScaleFilter::linear;
    }
    return ScaleFilter::nearest;
}

FrameFit frame_fit(
    MenuScaling scaling,
    int32_t window_width,
    int32_t window_height,
    int32_t frame_width,
    int32_t frame_height
) noexcept {
    if (scaling != MenuScaling::whole_steps || frame_width <= 0 || frame_height <= 0 ||
        window_width < frame_width || window_height < frame_height)
        return FrameFit::letterbox;
    return FrameFit::whole_steps;
}

bool frame_wants_pixelart(MenuScaling scaling, double scale) noexcept {
    return scaling != MenuScaling::unfiltered && scale > 0.0 && std::floor(scale) != scale;
}

ScaleFilter frame_filter(
    MenuScaling scaling, const LadderState* accelerated, bool pixelart, double scale
) noexcept {
    if (!frame_wants_pixelart(scaling, scale))
        return ScaleFilter::nearest;
    if (accelerated != nullptr)
        return chrome_filter(*accelerated, scale);
    return pixelart ? ScaleFilter::pixelart : ScaleFilter::nearest;
}

ScaleFilter world_filter(const LadderState& state, double zoom) noexcept {
    LadderState magnified = state;
    magnified.filtered_chrome = true;
    return chrome_filter(magnified, zoom);
}

// ---------------------------------------------------------------------------
// The Full tier's anti-aliasing: the supersample factor and the budget S

uint32_t supersample_factor(oa::ui::engine_settings::AntiAliasing level) noexcept {
    using oa::ui::engine_settings::AntiAliasing;
    switch (level) {
    case AntiAliasing::off:
        return 1;
    case AntiAliasing::x2:
        return 2;
    case AntiAliasing::x4:
        return 4;
    case AntiAliasing::x8:
        return 8;
    case AntiAliasing::x16:
        return largest_supersample_factor;
    }
    return 1;
}

uint64_t supersample_target_pixels(uint32_t width, uint32_t height, uint32_t factor) noexcept {
    const uint64_t texture_width = uint64_t{width} * factor;
    const uint64_t texture_height = uint64_t{height} * factor;
    uint64_t pixels = texture_width * texture_height;
    // The halves: one for the two-level reduction at a factor up to 4, and
    // one fewer than the doublings above it.
    uint32_t doublings = 0;
    while ((1U << doublings) < factor)
        ++doublings;
    const uint32_t halves = std::max<uint32_t>(1, doublings > 1 ? doublings - 1 : 0);
    for (uint32_t level = 1; level <= halves; ++level)
        pixels += (texture_width >> level) * (texture_height >> level);
    return pixels;
}

uint32_t fit_supersample_factor(
    uint32_t asked, uint32_t width, uint32_t height, uint64_t budget, uint32_t texture_limit
) noexcept {
    if (width == 0 || height == 0)
        return 1;
    // A render target takes a power of two: a factor between is read as the
    // one below it.
    uint32_t factor = 1;
    while (factor * 2 <= std::min(asked, largest_supersample_factor))
        factor *= 2;
    while (factor > 1) {
        const bool within_limit =
            texture_limit == unlimited_texture_size || (uint64_t{width} * factor <= texture_limit &&
                                                        uint64_t{height} * factor <= texture_limit);
        if (within_limit && supersample_target_pixels(width, height, factor) <= budget)
            return factor;
        factor /= 2;
    }
    return 1;
}

// ---------------------------------------------------------------------------
// Tiled textures

namespace {

/// Returns the number of tiles along one axis.
///
/// @param length the texture's length in texels
/// @param tile_size the largest tile, gutters included
/// @return one tile within tile_size; otherwise enough tiles of tile_size
///     texels with a gutter toward each neighbour
uint32_t tiles_along(uint32_t length, uint32_t tile_size) noexcept {
    if (length <= tile_size)
        return 1;
    // The two outer tiles hold tile_size - gutter texels, the inner ones
    // tile_size - 2 * gutter.
    const uint32_t inner = tile_size - 2 * tile_gutter;
    const uint32_t beyond_outer = length - 2 * tile_gutter;
    return (beyond_outer + inner - 1) / inner;
}

/// One axis of a tile.
struct AxisSpan {
    uint32_t texture_start{};
    uint32_t texture_length{};
    uint32_t content_start{};
    uint32_t content_length{};
};

/// Returns a tile's span along one axis.
///
/// @param length the texture's length in texels
/// @param tile_size the largest tile, gutters included
/// @param tiles the tiles along the axis
/// @param index the tile's place along it
/// @return its texture and content spans
AxisSpan tile_span(uint32_t length, uint32_t tile_size, uint32_t tiles, uint32_t index) noexcept {
    AxisSpan span;
    if (tiles <= 1) {
        span.texture_length = length;
        span.content_length = length;
        return span;
    }
    const uint32_t inner = tile_size - 2 * tile_gutter;
    const uint32_t first_content = tile_size - tile_gutter;
    span.content_start = index == 0 ? 0 : first_content + (index - 1) * inner;
    const uint32_t content_end =
        index + 1 == tiles
            ? length
            : std::min(length, span.content_start + (index == 0 ? first_content : inner));
    span.content_length = content_end - span.content_start;
    span.texture_start = index == 0 ? 0 : span.content_start - tile_gutter;
    const uint32_t texture_end = index + 1 == tiles ? length : content_end + tile_gutter;
    span.texture_length = texture_end - span.texture_start;
    return span;
}

} // namespace

TileGrid plan_tiles(uint32_t width, uint32_t height, uint32_t limit) noexcept {
    TileGrid grid;
    grid.width = width;
    grid.height = height;
    if (width == 0 || height == 0)
        return grid;
    if (limit == unlimited_texture_size || (width <= limit && height <= limit)) {
        grid.columns = 1;
        grid.rows = 1;
        return grid;
    }
    const uint32_t tile_size = std::min(limit, largest_tile_size);
    if (tile_size < smallest_tile_size)
        return grid;
    grid.tile_size = tile_size;
    grid.columns = tiles_along(width, tile_size);
    grid.rows = tiles_along(height, tile_size);
    return grid;
}

Tile tile_at(const TileGrid& grid, uint32_t column, uint32_t row) noexcept {
    Tile tile;
    if (column >= grid.columns || row >= grid.rows)
        return tile;
    const uint32_t tile_size =
        grid.tile_size == 0 ? std::max(grid.width, grid.height) : grid.tile_size;
    const AxisSpan across = tile_span(grid.width, tile_size, grid.columns, column);
    const AxisSpan down = tile_span(grid.height, tile_size, grid.rows, row);
    tile.texture = TexelRect{
        across.texture_start, down.texture_start, across.texture_length, down.texture_length
    };
    tile.content = TexelRect{
        across.content_start, down.content_start, across.content_length, down.content_length
    };
    tile.source = TexelRect{
        across.content_start - across.texture_start,
        down.content_start - down.texture_start,
        across.content_length,
        down.content_length
    };
    return tile;
}

} // namespace oa::app::render_policy
