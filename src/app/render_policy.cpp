// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/app/render_policy.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace oa::app::render_policy {

namespace {

/// Nanoseconds in a second.
constexpr uint64_t nanoseconds_per_second = 1'000'000'000;
/// Nanoseconds in a microsecond.
constexpr uint64_t nanoseconds_per_microsecond = 1'000;
/// The largest figure a pooled sample holds, in microseconds: a frame longer
/// than about 71 minutes counts as that long.
constexpr uint64_t largest_pooled_us = UINT32_MAX;

/// The places of a pool's ring.
constexpr uint32_t pool_slots = static_cast<uint32_t>(pool_capacity);

/// Converts nanoseconds to whole microseconds for a pool, saturating.
///
/// @param ns a duration in nanoseconds
/// @return the duration in microseconds, at most largest_pooled_us
uint32_t pooled_us(uint64_t ns) noexcept {
    return static_cast<uint32_t>(
        std::min<uint64_t>(ns / nanoseconds_per_microsecond, largest_pooled_us)
    );
}

} // namespace

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

void begin_match(SharedMatchGate& gate, MatchKind kind, bool accelerated_now) noexcept {
    gate.kind = kind;
    gate.accelerated = kind != MatchKind::none && accelerated_now;
}

void note_match_frame(SharedMatchGate& gate, RenderTier tier, bool device_lost) noexcept {
    if (gate.kind != MatchKind::none && tier == RenderTier::standard && !device_lost)
        gate.accelerated = false;
}

void stop_until_match_end(SharedMatchGate& gate) noexcept {
    if (gate.kind != MatchKind::none)
        gate.accelerated = false;
}

void end_match(SharedMatchGate& gate) noexcept {
    gate = SharedMatchGate{};
}

bool first_use_allowed(const SharedMatchGate& gate, bool loading_screen_beginning) noexcept {
    return gate.kind == MatchKind::none || loading_screen_beginning;
}

bool records_on_disk(bool players_own_profile, bool render_driver_named) noexcept {
    return players_own_profile && !render_driver_named;
}

TierDecision decide_render_tier(const TierInputs& inputs) noexcept {
    const auto standard = [](TierReason reason) {
        return TierDecision{RenderTier::standard, reason};
    };
    const bool flag_on = inputs.flag == AccelerationFlag::on;
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
    if (!inputs.setting_on && !flag_on)
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
    return TierDecision{RenderTier::accelerated, TierReason::accelerated};
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

AccelerationFlag acceleration_flag(std::optional<bool> flag) noexcept {
    if (!flag)
        return AccelerationFlag::none;
    return *flag ? AccelerationFlag::on : AccelerationFlag::off;
}

TierAction tier_action(const TierDecision& decision, bool presentation_on) noexcept {
    if (decision.reason == TierReason::function_test_due)
        return TierAction::run_function_test;
    const bool accelerated = decision.tier == RenderTier::accelerated;
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

FunctionTest start_function_test(const TierInputs& inputs) noexcept {
    const bool asked = inputs.flag == AccelerationFlag::on || inputs.force_capable;
    return records_on_disk(inputs.players_own_profile, inputs.render_driver_named) && !asked
               ? FunctionTest::trial_unwritten
               : FunctionTest::not_run;
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
    if (tier == RenderTier::accelerated)
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

/// Steps the rungs that frames at any zoom move: NEAREST chrome, the card's
/// magnification while anything uses it, then the standard tier.
///
/// @param state the rung
/// @return the rung one step down
LadderState step_general(LadderState state) noexcept {
    if (state.filtered_chrome) {
        state.filtered_chrome = false;
        return state;
    }
    if (card_in_use(state) && state.card != CardFilter::linear) {
        state.card = lower_card(state.card);
        return state;
    }
    state.standard = true;
    return state;
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

LadderState step_down(const LadderState& state, FrameKind pool, bool blend_favoured) noexcept {
    LadderState lowered = state;
    if (lowered.standard)
        return lowered;
    switch (pool) {
    case FrameKind::zoomed_out:
        if (lowered.method == ZoomOutMethod::area && lowered.blend_allowed && blend_favoured &&
            lowered.budget != SceneBudget::none) {
            lowered.method = ZoomOutMethod::blend;
            return lowered;
        }
        if (lowered.budget != SceneBudget::none) {
            lowered.budget = static_cast<SceneBudget>(static_cast<uint8_t>(lowered.budget) - 1);
            return lowered;
        }
        return step_general(lowered);
    case FrameKind::zoomed_in:
        if (lowered.magnify) {
            lowered.magnify = false;
            return lowered;
        }
        return step_general(lowered);
    case FrameKind::other:
        return step_general(lowered);
    }
    return lowered;
}

bool steady_frame(const FrameSample& sample) noexcept {
    return !sample.idle && sample.window_active && !sample.settling && !sample.match_warming;
}

namespace {

/// Returns the pool a kind of frame joins.
///
/// @param ladder the step-down
/// @param kind the frame's kind
/// @return its pool
SamplePool& pool_of(ScaleStepDown& ladder, FrameKind kind) noexcept {
    switch (kind) {
    case FrameKind::zoomed_out:
        return ladder.zoomed_out;
    case FrameKind::zoomed_in:
        return ladder.zoomed_in;
    case FrameKind::other:
        return ladder.other;
    }
    return ladder.other;
}

/// Adds a sample to a pool, keeping its latest slow_window_ns and no more
/// than pool_capacity samples.
///
/// @param[in,out] pool the pool
/// @param sample the sample
void add_sample(SamplePool& pool, const PooledSample& sample) noexcept {
    if (pool.count == pool_slots) {
        pool.held_us -= pool.samples[pool.first].interval_us;
        pool.first = (pool.first + 1) % pool_slots;
        --pool.count;
    }
    pool.samples[(pool.first + pool.count) % pool_slots] = sample;
    ++pool.count;
    pool.held_us += sample.interval_us;
    const uint64_t window_us = slow_window_ns / nanoseconds_per_microsecond;
    while (pool.count > 1 && pool.held_us - pool.samples[pool.first].interval_us >= window_us) {
        pool.held_us -= pool.samples[pool.first].interval_us;
        pool.first = (pool.first + 1) % pool_slots;
        --pool.count;
    }
}

/// The figure of a pooled sample a median is taken of.
enum class Figure : uint8_t {
    time,       ///< the frame's time, less its ticks'
    passes,     ///< the tier's own passes
    area_third, ///< 1 when the area pass takes at least a third of the draw, else 0
    present,    ///< the present measure
};

/// Returns the median of a figure over a pool's latest samples.
///
/// @param pool the pool
/// @param span_us the span of the latest samples, by their intervals, in
///     microseconds
/// @param figure the figure
/// @param[out] covered the samples taken span at least span_us
/// @return the median, the upper of the two middle values for an even count;
///     for Figure::area_third 1 when at least half the samples, counted
///     from the upper middle, have the area pass at a third of the draw
uint32_t
pool_median(const SamplePool& pool, uint64_t span_us, Figure figure, bool& covered) noexcept {
    std::array<uint32_t, pool_capacity> values{};
    uint32_t taken = 0;
    uint64_t spanned = 0;
    covered = false;
    for (uint32_t back = 0; back < pool.count; ++back) {
        const PooledSample& sample =
            pool.samples[(pool.first + pool.count - 1 - back) % pool_slots];
        uint32_t value = 0;
        switch (figure) {
        case Figure::time:
            value = sample.time_us;
            break;
        case Figure::passes:
            value = sample.passes_us;
            break;
        case Figure::area_third:
            value = sample.draw_us != 0 &&
                    uint64_t{sample.area_us} * blend_area_parts >= sample.draw_us;
            break;
        case Figure::present:
            value = sample.present_us;
            break;
        }
        values[taken++] = value;
        spanned += sample.interval_us;
        if (spanned >= span_us) {
            covered = true;
            break;
        }
    }
    if (taken == 0)
        return 0;
    const auto middle = values.begin() + taken / 2;
    std::nth_element(values.begin(), middle, values.begin() + taken);
    return *middle;
}

/// Empties a pool.
///
/// @param[out] pool the pool
void empty_pool(SamplePool& pool) noexcept {
    pool.first = 0;
    pool.count = 0;
    pool.held_us = 0;
}

} // namespace

ScaleStepDown start_step_down(const LadderState& state) noexcept {
    ScaleStepDown ladder;
    ladder.state = state;
    return ladder;
}

StepResult feed_step_down(ScaleStepDown& ladder, const FrameSample& sample) noexcept {
    if (ladder.state.standard || !steady_frame(sample))
        return StepResult::none;
    // While the clock runs behind, any time the tier's own passes take is a
    // loss: shed them at once.
    if (sample.clock_behind && sample.passes_ns > 0 &&
        (ladder.state.budget != SceneBudget::none || ladder.state.magnify)) {
        ladder.state.budget = SceneBudget::none;
        ladder.state.magnify = false;
        empty_pool(ladder.zoomed_out);
        empty_pool(ladder.zoomed_in);
        return StepResult::shed;
    }
    const uint32_t frames_per_second =
        std::clamp<uint32_t>(sample.paced_frames_per_second, 1, step_target_frames_per_second);
    const uint64_t target_us =
        nanoseconds_per_second / nanoseconds_per_microsecond / frames_per_second;
    PooledSample pooled;
    pooled.interval_us = pooled_us(sample.interval_ns);
    pooled.time_us =
        pooled_us(sample.interval_ns > sample.tick_ns ? sample.interval_ns - sample.tick_ns : 0);
    pooled.passes_us = pooled_us(sample.passes_ns);
    pooled.area_us = pooled_us(sample.area_ns);
    pooled.draw_us = pooled_us(sample.draw_ns);
    pooled.present_us = pooled_us(sample.present_ns);
    SamplePool& pool = pool_of(ladder, sample.kind);
    add_sample(pool, pooled);

    bool step = false;
    bool covered = false;
    const uint32_t quick_median =
        pool_median(pool, very_slow_window_ns / nanoseconds_per_microsecond, Figure::time, covered);
    if (covered && uint64_t{quick_median} * 100 > target_us * very_slow_percent)
        step = true;
    const bool spaced = !ladder.stepped || sample.now_ns - ladder.last_step_ns >= step_spacing_ns;
    if (!step && spaced) {
        const uint32_t median =
            pool_median(pool, slow_window_ns / nanoseconds_per_microsecond, Figure::time, covered);
        if (covered) {
            if (uint64_t{median} * 100 > target_us * slow_percent) {
                step = true;
            } else if (median > target_us && sample.kind != FrameKind::other) {
                bool passes_covered = false;
                const uint32_t passes = pool_median(
                    pool,
                    slow_window_ns / nanoseconds_per_microsecond,
                    Figure::passes,
                    passes_covered
                );
                if (uint64_t{passes} * 100 > target_us * passes_percent)
                    step = true;
            }
        }
    }
    if (!step)
        return StepResult::none;
    bool blend_favoured = false;
    if (sample.kind == FrameKind::zoomed_out) {
        bool share_covered = false;
        bool present_covered = false;
        const uint32_t area_third = pool_median(
            pool, slow_window_ns / nanoseconds_per_microsecond, Figure::area_third, share_covered
        );
        const uint32_t present = pool_median(
            pool, slow_window_ns / nanoseconds_per_microsecond, Figure::present, present_covered
        );
        blend_favoured =
            area_third != 0 && uint64_t{present} * 100 < target_us * blend_present_percent;
    }
    ladder.state = step_down(ladder.state, sample.kind, blend_favoured);
    ladder.stepped = true;
    ladder.last_step_ns = sample.now_ns;
    empty_pool(pool);
    return ladder.state.standard ? StepResult::standard : StepResult::stepped;
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
    if (inputs.asked)
        return DensityDecision{true, DensityReason::asked};
    if (inputs.render_driver_named || inputs.virtual_video_driver)
        return window_system(DensityReason::environment);
    if (inputs.unattended)
        return window_system(DensityReason::unattended);
    if (inputs.capture)
        return window_system(DensityReason::capture);
    if (!inputs.setting_on && inputs.flag != AccelerationFlag::on)
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
