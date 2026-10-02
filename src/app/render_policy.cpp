// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/app/render_policy.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <system_error>
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
// Records and strikes

bool same_strike(const Strike& first, const Strike& second) noexcept {
    if (first.kind != second.kind)
        return false;
    switch (first.kind) {
    case StrikeKind::path:
        return first.path == second.path;
    case StrikeKind::present:
    case StrikeKind::call:
        return first.call == second.call;
    default:
        return true;
    }
}

const DriverRecord* find_record(const RendererRecords& records, std::string_view driver) noexcept {
    for (const DriverRecord& record : records.drivers)
        if (record.driver == driver)
            return &record;
    return nullptr;
}

DriverRecord& record_for(RendererRecords& records, std::string_view driver) {
    for (DriverRecord& record : records.drivers)
        if (record.driver == driver)
            return record;
    DriverRecord& added = records.drivers.emplace_back();
    added.driver = std::string(driver);
    added.adapter = records.adapter;
    added.engine_version = records.engine_version;
    return added;
}

std::vector<std::string_view> failed_driver_list(const RendererRecords& records) {
    std::vector<std::string_view> failed;
    for (const DriverRecord& record : records.drivers)
        if (record.failed_driver != RecordedFailure::none && !is_software(record.driver))
            failed.emplace_back(record.driver);
    return failed;
}

namespace {

/// A word of a record's value and what it stands for.
template <typename Value>
struct Word {
    std::string_view text{};
    Value value{};
};

/// The accelerated paths' words.
constexpr std::array<Word<AcceleratedPath>, 3> path_words = {{
    {"magnify", AcceleratedPath::magnify},
    {"prescale", AcceleratedPath::prescale},
    {"blend", AcceleratedPath::blend},
}};

/// The strikes' stages' words.
constexpr std::array<Word<StrikeKind>, 8> strike_words = {{
    {"create", StrikeKind::create},
    {"standard", StrikeKind::standard},
    {"probe", StrikeKind::probe},
    {"path", StrikeKind::path},
    {"present", StrikeKind::present},
    {"call", StrikeKind::call},
    {"lost", StrikeKind::lost},
    {"resets", StrikeKind::resets},
}};

/// The recorded failures' words.
constexpr std::array<Word<RecordedFailure>, 5> failure_words = {{
    {"stopped", RecordedFailure::stopped},
    {"present", RecordedFailure::present},
    {"call", RecordedFailure::call},
    {"lost", RecordedFailure::lost},
    {"resets", RecordedFailure::resets},
}};

/// The sentinel's stages' words.
constexpr std::array<Word<SentinelStage>, 6> sentinel_words = {{
    {"create", SentinelStage::create},
    {"standard", SentinelStage::standard},
    {"probe", SentinelStage::probe},
    {"accelerated", SentinelStage::accelerated},
    {"path", SentinelStage::path},
    {"running", SentinelStage::running},
}};

/// The scene budgets' words, in a remembered rung.
constexpr std::array<Word<SceneBudget>, 3> budget_words = {{
    {"none", SceneBudget::none},
    {"reduced", SceneBudget::reduced},
    {"full", SceneBudget::full},
}};

/// The zoomed-out methods' words, in a remembered rung.
constexpr std::array<Word<ZoomOutMethod>, 2> method_words = {{
    {"area", ZoomOutMethod::area},
    {"blend", ZoomOutMethod::blend},
}};

/// The magnify rung's words, in a remembered rung.
constexpr std::array<Word<bool>, 2> magnify_words = {{
    {"magnify", true},
    {"no-magnify", false},
}};

/// The chrome rung's words, in a remembered rung.
constexpr std::array<Word<bool>, 2> chrome_words = {{
    {"filtered-chrome", true},
    {"nearest-chrome", false},
}};

/// The card's magnifications' words, in a remembered rung.
constexpr std::array<Word<CardFilter>, 4> card_words = {{
    {"linear", CardFilter::linear},
    {"prescale-quarter", CardFilter::prescale_quarter},
    {"prescale-full", CardFilter::prescale_full},
    {"pixelart", CardFilter::pixelart},
}};

/// The parts of a remembered rung's word: budget, method, magnify, chrome
/// and card.
constexpr size_t rung_parts = 5;
/// What separates a remembered rung's parts.
constexpr char rung_separator = '/';
/// The engine version written for records whose version is not known; the
/// next start clears them.
constexpr std::string_view unknown_engine_version = "unknown";
/// The word after a record that the main menu's notice has shown.
constexpr std::string_view told_word = "told";
/// The word before a software sentinel's framebuffer hint list.
constexpr std::string_view via_word = "via";
/// What separates the drivers of a framebuffer hint list.
constexpr char list_separator = ',';
/// Hundredths in a whole: a remembered median is written as a fraction of
/// the target period with two decimals.
constexpr uint32_t hundredths = 100;
/// The decimals of a remembered median.
constexpr size_t median_decimals = 2;
/// The base of decimal numbers.
constexpr uint32_t decimal_base = 10;

/// The key of the adapter the probe last read.
constexpr std::string_view adapter_key = "adapter";
/// The key of the trial.
constexpr std::string_view trial_key = "trial";
/// The key of the native-density record.
constexpr std::string_view native_density_key = "native-density";
/// The start of the key of a driver's strike; the driver's name follows.
constexpr std::string_view strike_prefix = "strike.";
/// The start of the key of a driver's failed-driver record.
constexpr std::string_view failed_driver_prefix = "failed-driver.";
/// The start of the key of a driver's accelerated-unusable record.
constexpr std::string_view accelerated_unusable_prefix = "accelerated-unusable.";
/// The start of the key of a driver's remembered rung.
constexpr std::string_view scale_level_prefix = "scale-level.";

/// Finds what a word stands for.
///
/// @param words the words
/// @param text the word
/// @param[out] value what it stands for, when it is one of them
/// @return true when it is
template <typename Value, size_t count>
bool word_value(
    const std::array<Word<Value>, count>& words, std::string_view text, Value& value
) noexcept {
    for (const Word<Value>& word : words) {
        if (word.text == text) {
            value = word.value;
            return true;
        }
    }
    return false;
}

/// Finds the word for a value.
///
/// @param words the words
/// @param value the value
/// @return its word; empty when it has none
template <typename Value, size_t count>
std::string_view word_text(const std::array<Word<Value>, count>& words, Value value) noexcept {
    for (const Word<Value>& word : words)
        if (word.value == value)
            return word.text;
    return {};
}

/// Tells whether a character separates the words of a value.
///
/// @param character the character
/// @return true for a space or a tab
bool separates_words(char character) noexcept {
    return character == ' ' || character == '\t';
}

/// Splits a value into its words, at runs of spaces and tabs.
///
/// @param value the value
/// @return views of its words, in order
std::vector<std::string_view> words_of(std::string_view value) {
    std::vector<std::string_view> words;
    size_t index = 0;
    while (index < value.size()) {
        while (index < value.size() && separates_words(value[index]))
            ++index;
        const size_t start = index;
        while (index < value.size() && !separates_words(value[index]))
            ++index;
        if (index > start)
            words.push_back(value.substr(start, index - start));
    }
    return words;
}

/// Joins words with single spaces.
///
/// @param words the words
/// @return the words joined
std::string joined(std::span<const std::string_view> words) {
    std::string text;
    for (const std::string_view word : words) {
        if (!text.empty())
            text += ' ';
        text += word;
    }
    return text;
}

/// Returns an adapter's name as records hold it: its words joined with single
/// spaces.
///
/// @param adapter the adapter's name
/// @return the name, or unknown_adapter when it holds no word
std::string normal_adapter(std::string_view adapter) {
    const std::vector<std::string_view> words = words_of(adapter);
    return words.empty() ? std::string(unknown_adapter) : joined(words);
}

/// Tells whether a name can be written as one word of a value or a key.
///
/// @param name the name
/// @return true when it is not empty and holds no space or tab
bool one_word(std::string_view name) noexcept {
    return !name.empty() && std::none_of(name.begin(), name.end(), separates_words);
}

/// Returns an engine version as a value holds it.
///
/// @param version the version
/// @return the version, or unknown_engine_version when it is not one word
std::string_view version_word(std::string_view version) noexcept {
    return one_word(version) ? version : unknown_engine_version;
}

/// Reads a whole decimal number that fills a word.
///
/// @param word the word
/// @param[out] number the number
/// @return false when the word is not a number of that type
template <typename Number>
bool read_number(std::string_view word, Number& number) noexcept {
    const char* const end = word.data() + word.size();
    const std::from_chars_result result = std::from_chars(word.data(), end, number);
    return !word.empty() && result.ec == std::errc{} && result.ptr == end;
}

/// Reads a remembered median: a fraction of the target period, written with
/// a decimal point.
///
/// @param word the word
/// @param[out] percent the fraction in percent, later decimals dropped
/// @return false when the word is not such a fraction
bool read_median(std::string_view word, uint32_t& percent) noexcept {
    const size_t point = word.find('.');
    uint32_t whole = 0;
    if (!read_number(word.substr(0, point), whole) || whole > UINT32_MAX / hundredths - 1)
        return false;
    uint32_t fraction = 0;
    if (point != std::string_view::npos) {
        const std::string_view decimals = word.substr(point + 1);
        if (decimals.empty() || !std::all_of(decimals.begin(), decimals.end(), [](char digit) {
                return digit >= '0' && digit <= '9';
            }))
            return false;
        for (size_t place = 0; place < median_decimals; ++place)
            fraction = fraction * decimal_base +
                       (place < decimals.size() ? static_cast<uint32_t>(decimals[place] - '0') : 0);
    }
    percent = whole * hundredths + fraction;
    return true;
}

/// Writes a remembered median as a fraction of the target period.
///
/// @param percent the median, in percent of the target period
/// @return the fraction with two decimals
std::string median_text(uint32_t percent) {
    const uint32_t fraction = percent % hundredths;
    std::string text = std::to_string(percent / hundredths);
    text += '.';
    if (fraction < decimal_base)
        text += '0';
    text += std::to_string(fraction);
    return text;
}

/// Reads a remembered rung's word.
///
/// @param word the word: budget, method, magnify, chrome and card
/// @param[out] rung the rung
/// @return false when the word is not a rung
bool read_rung(std::string_view word, LadderState& rung) noexcept {
    std::array<std::string_view, rung_parts> parts{};
    size_t count = 0;
    size_t start = 0;
    for (;;) {
        const size_t end = word.find(rung_separator, start);
        if (count == rung_parts)
            return false;
        parts[count++] = word.substr(start, end == std::string_view::npos ? end : end - start);
        if (end == std::string_view::npos)
            break;
        start = end + 1;
    }
    LadderState read;
    if (count != rung_parts || !word_value(budget_words, parts[0], read.budget) ||
        !word_value(method_words, parts[1], read.method) ||
        !word_value(magnify_words, parts[2], read.magnify) ||
        !word_value(chrome_words, parts[3], read.filtered_chrome) ||
        !word_value(card_words, parts[4], read.card))
        return false;
    rung = read;
    return true;
}

/// Writes a remembered rung's word.
///
/// @param rung the rung
/// @return its budget, method, magnify, chrome and card, separated
std::string rung_text(const LadderState& rung) {
    std::string text(word_text(budget_words, rung.budget));
    for (const std::string_view part :
         {word_text(method_words, rung.method),
          word_text(magnify_words, rung.magnify),
          word_text(chrome_words, rung.filtered_chrome),
          word_text(card_words, rung.card)}) {
        text += rung_separator;
        text += part;
    }
    return text;
}

/// The adapter and engine version a driver's value was made under.
struct Made {
    std::string adapter{};
    std::string engine_version{};
};

/// Reads the adapter and engine version a driver's value ends with.
///
/// @param words the value's words after its own fields: the adapter's
///     words, then the version
/// @param[out] made the adapter and version
/// @return false when there are too few words
bool read_made(std::span<const std::string_view> words, Made& made) {
    if (words.size() < 2)
        return false;
    made.adapter = joined(words.first(words.size() - 1));
    made.engine_version = std::string(words.back());
    return true;
}

/// Returns the words a driver's value ends with: its adapter and engine
/// version.
///
/// @param record the driver's record
/// @return a space, the adapter, a space and the version
std::string made_text(const DriverRecord& record) {
    std::string text = " ";
    text += normal_adapter(record.adapter);
    text += ' ';
    text += version_word(record.engine_version);
    return text;
}

/// Finds or adds the record a driver's value belongs to.
///
/// @param[in,out] records the records
/// @param driver the driver
/// @param made the adapter and engine version the value was made under
/// @return the record; nullptr when the driver's values read before were
///     made under another adapter or version
DriverRecord*
record_made_under(RendererRecords& records, std::string_view driver, const Made& made) {
    for (DriverRecord& record : records.drivers) {
        if (record.driver != driver)
            continue;
        if (record.adapter != made.adapter || record.engine_version != made.engine_version)
            return nullptr;
        return &record;
    }
    DriverRecord& added = records.drivers.emplace_back();
    added.driver = std::string(driver);
    added.adapter = made.adapter;
    added.engine_version = made.engine_version;
    return &added;
}

/// Reads a strike's stage, from the start of its value.
///
/// @param words the value's words
/// @param[out] strike the strike
/// @param[out] used the words the stage took
/// @return false when the value does not start with a stage
bool read_strike(std::span<const std::string_view> words, Strike& strike, size_t& used) noexcept {
    Strike read;
    if (words.empty() || !word_value(strike_words, words[0], read.kind))
        return false;
    used = 1;
    switch (read.kind) {
    case StrikeKind::path:
        if (words.size() < 2 || !word_value(path_words, words[1], read.path))
            return false;
        used = 2;
        break;
    case StrikeKind::present:
    case StrikeKind::call:
        if (words.size() < 2 || !read_number(words[1], read.call))
            return false;
        used = 2;
        break;
    default:
        break;
    }
    strike = read;
    return true;
}

/// Writes a strike's stage.
///
/// @param strike the strike, not StrikeKind::none
/// @return its stage's words
std::string strike_text(const Strike& strike) {
    std::string text(word_text(strike_words, strike.kind));
    if (strike.kind == StrikeKind::path) {
        text += ' ';
        text += word_text(path_words, strike.path);
    } else if (strike.kind == StrikeKind::present || strike.kind == StrikeKind::call) {
        text += ' ';
        text += std::to_string(strike.call);
    }
    return text;
}

/// Tells whether a failure is one a failed-driver record holds.
///
/// @param failure the failure
/// @return true for a stop, a present error, a lost device or resets
bool fails_driver(RecordedFailure failure) noexcept {
    return failure == RecordedFailure::stopped || failure == RecordedFailure::present ||
           failure == RecordedFailure::lost || failure == RecordedFailure::resets;
}

/// Tells whether a failure is one an accelerated-unusable record holds.
///
/// @param failure the failure
/// @return true for a stop, a failed call, a lost device or resets
bool makes_unusable(RecordedFailure failure) noexcept {
    return failure == RecordedFailure::stopped || failure == RecordedFailure::call ||
           failure == RecordedFailure::lost || failure == RecordedFailure::resets;
}

/// A failure record as its value holds it.
struct ReadFailure {
    RecordedFailure failure{RecordedFailure::none};
    bool told{};
    Made made{};
};

/// Reads a failed-driver or accelerated-unusable value.
///
/// @param words the value's words
/// @param[out] read the failure, whether it was told and where it was made
/// @return false when the value is not a failure record
bool read_failure(std::span<const std::string_view> words, ReadFailure& read) {
    if (words.empty() || !word_value(failure_words, words[0], read.failure))
        return false;
    std::span<const std::string_view> rest = words.subspan(1);
    // The told mark follows the version, so it is read only after an adapter
    // and a version.
    read.told = rest.size() >= 3 && rest.back() == told_word;
    if (read.told)
        rest = rest.first(rest.size() - 1);
    return read_made(rest, read.made);
}

/// Writes a failed-driver or accelerated-unusable value.
///
/// @param failure the failure
/// @param told the main menu's notice has shown it
/// @param record the driver's record
/// @return the value
std::string failure_text(RecordedFailure failure, bool told, const DriverRecord& record) {
    std::string text(word_text(failure_words, failure));
    text += made_text(record);
    if (told) {
        text += ' ';
        text += told_word;
    }
    return text;
}

/// Reads the trial's value.
///
/// @param words the value's words
/// @param[out] trial the trial
/// @return false when the value is not a trial
bool read_trial(std::span<const std::string_view> words, TrialRecord& trial) {
    TrialRecord read;
    if (words.size() == 2 && words[0] == word_text(strike_words, StrikeKind::probe)) {
        read.stage = StrikeKind::probe;
    } else if (
        words.size() == 3 && words[0] == word_text(strike_words, StrikeKind::path) &&
        word_value(path_words, words[1], read.path)
    ) {
        read.stage = StrikeKind::path;
    } else {
        return false;
    }
    read.driver = std::string(words.back());
    trial = std::move(read);
    return true;
}

/// Tells whether a driver's record holds anything the file keeps.
///
/// @param record the driver's record
/// @return true when it has a strike, a failure record or a remembered rung
bool holds_records(const DriverRecord& record) noexcept {
    return record.strike.kind != StrikeKind::none ||
           record.failed_driver != RecordedFailure::none ||
           record.accelerated_unusable != RecordedFailure::none || record.scale_level.kept;
}

/// Reads one driver's value into the records; a value that cannot be read
/// is ignored.
///
/// @param[in,out] records the records
/// @param prefix the key's prefix, which says what the value holds
/// @param driver the driver the key names
/// @param words the value's words
void read_driver_value(
    RendererRecords& records,
    std::string_view prefix,
    std::string_view driver,
    std::span<const std::string_view> words
) {
    if (prefix == strike_prefix) {
        Strike strike;
        size_t used = 0;
        Made made;
        if (!read_strike(words, strike, used) || !read_made(words.subspan(used), made))
            return;
        if (DriverRecord* record = record_made_under(records, driver, made))
            record->strike = strike;
        return;
    }
    if (prefix == failed_driver_prefix || prefix == accelerated_unusable_prefix) {
        ReadFailure read;
        if (!read_failure(words, read))
            return;
        if (prefix == failed_driver_prefix) {
            if (!fails_driver(read.failure) || is_software(driver))
                return;
            if (DriverRecord* record = record_made_under(records, driver, read.made)) {
                record->failed_driver = read.failure;
                record->failed_driver_told = read.told;
            }
            return;
        }
        if (!makes_unusable(read.failure))
            return;
        if (DriverRecord* record = record_made_under(records, driver, read.made)) {
            record->accelerated_unusable = read.failure;
            record->accelerated_unusable_told = read.told;
        }
        return;
    }
    // scale-level: the rung, the adapter, the version, then the median.
    RememberedRung remembered;
    Made made;
    if (words.size() < 4 || !read_rung(words[0], remembered.rung) ||
        !read_median(words.back(), remembered.median_percent) ||
        !read_made(words.subspan(1, words.size() - 2), made))
        return;
    remembered.kept = true;
    if (DriverRecord* record = record_made_under(records, driver, made))
        record->scale_level = remembered;
}

} // namespace

RendererRecords parse_records(const RecordValues& values) {
    RendererRecords records;
    constexpr std::array<std::string_view, 4> driver_prefixes = {
        strike_prefix, failed_driver_prefix, accelerated_unusable_prefix, scale_level_prefix
    };
    for (const auto& [key, value] : values) {
        const std::vector<std::string_view> words = words_of(value);
        if (key == adapter_key) {
            records.adapter = normal_adapter(value);
            continue;
        }
        if (key == trial_key) {
            (void)read_trial(words, records.trial);
            continue;
        }
        if (key == native_density_key) {
            if (words.size() == 2) {
                records.native_density.driver = std::string(words[0]);
                records.native_density.engine_version = std::string(words[1]);
            }
            continue;
        }
        const std::string_view key_view = key;
        for (const std::string_view prefix : driver_prefixes) {
            if (!key_view.starts_with(prefix))
                continue;
            const std::string_view driver = key_view.substr(prefix.size());
            if (one_word(driver))
                read_driver_value(records, prefix, driver, words);
            break;
        }
    }
    return records;
}

RecordValues format_records(const RendererRecords& records) {
    RecordValues values;
    values[std::string(adapter_key)] = normal_adapter(records.adapter);
    const TrialRecord& trial = records.trial;
    if ((trial.stage == StrikeKind::probe || trial.stage == StrikeKind::path) &&
        one_word(trial.driver)) {
        std::string value(word_text(strike_words, trial.stage));
        if (trial.stage == StrikeKind::path) {
            value += ' ';
            value += word_text(path_words, trial.path);
        }
        value += ' ';
        value += trial.driver;
        values[std::string(trial_key)] = std::move(value);
    }
    if (one_word(records.native_density.driver)) {
        std::string value = records.native_density.driver;
        value += ' ';
        value += version_word(records.native_density.engine_version);
        values[std::string(native_density_key)] = std::move(value);
    }
    for (const DriverRecord& record : records.drivers) {
        if (!one_word(record.driver))
            continue;
        if (record.strike.kind != StrikeKind::none)
            values[std::string(strike_prefix) + record.driver] =
                strike_text(record.strike) + made_text(record);
        if (record.failed_driver != RecordedFailure::none && !is_software(record.driver))
            values[std::string(failed_driver_prefix) + record.driver] =
                failure_text(record.failed_driver, record.failed_driver_told, record);
        if (record.accelerated_unusable != RecordedFailure::none)
            values[std::string(accelerated_unusable_prefix) + record.driver] =
                failure_text(record.accelerated_unusable, record.accelerated_unusable_told, record);
        if (record.scale_level.kept)
            values[std::string(scale_level_prefix) + record.driver] =
                rung_text(record.scale_level.rung) + made_text(record) + ' ' +
                median_text(record.scale_level.median_percent);
    }
    return values;
}

RecordChange clear_on_machine_change(
    RendererRecords& records, std::string_view adapter, std::string_view engine_version
) {
    RecordChange change;
    records.engine_version = std::string(engine_version);
    const std::string read = normal_adapter(adapter);
    const bool adapter_read = read != unknown_adapter;
    const bool adapter_changed = adapter_read && read != normal_adapter(records.adapter);
    if (adapter_changed) {
        records.adapter = read;
        change.changed = true;
    }
    const auto made_elsewhere = [&](const DriverRecord& record) {
        return record.engine_version != engine_version ||
               (adapter_read && normal_adapter(record.adapter) != read);
    };
    for (const DriverRecord& record : records.drivers)
        if (holds_records(record) && made_elsewhere(record))
            change.changed = true;
    std::erase_if(records.drivers, made_elsewhere);
    if (!records.native_density.driver.empty() &&
        (records.native_density.engine_version != engine_version || adapter_changed)) {
        records.native_density = NativeDensityRecord{};
        change.changed = true;
    }
    return change;
}

namespace {

/// Records failed-driver against a driver, never against software.
///
/// @param[in,out] record the driver's record
/// @param failure what failed
/// @param[in,out] change what changed
void record_failed_driver(
    DriverRecord& record, RecordedFailure failure, RecordChange& change
) noexcept {
    if (is_software(record.driver))
        return;
    if (record.failed_driver != failure) {
        change.changed = true;
        change.new_record = true;
        record.failed_driver = failure;
        record.failed_driver_told = false;
    }
}

/// Records accelerated-unusable against a driver.
///
/// @param[in,out] record the driver's record
/// @param failure what failed
/// @param[in,out] change what changed
void record_accelerated_unusable(
    DriverRecord& record, RecordedFailure failure, RecordChange& change
) noexcept {
    if (record.accelerated_unusable != failure) {
        change.changed = true;
        change.new_record = true;
        record.accelerated_unusable = failure;
        record.accelerated_unusable_told = false;
    }
}

/// Sets a driver's strike, as one not made by a failure in this run.
///
/// @param[in,out] record the driver's record
/// @param strike the strike, StrikeKind::none to clear it
/// @param[in,out] change what changed
void set_strike(DriverRecord& record, const Strike& strike, RecordChange& change) noexcept {
    if (same_strike(record.strike, strike))
        return;
    record.strike = strike;
    record.struck_this_run = false;
    change.changed = true;
}

/// Applies one start-up stage's left-over evidence to a driver: a strike,
/// or the record it makes with the same strike before it.
///
/// @param[in,out] record the driver's record
/// @param strike the stage's strike
/// @param at_once the record is made at the first strike
/// @return what changed
RecordChange strike_or_record(DriverRecord& record, const Strike& strike, bool at_once) noexcept {
    RecordChange change;
    const bool repeated = at_once || same_strike(record.strike, strike);
    if (!repeated) {
        set_strike(record, strike, change);
        return change;
    }
    if (strike.kind == StrikeKind::probe || strike.kind == StrikeKind::path)
        record_accelerated_unusable(record, RecordedFailure::stopped, change);
    else
        record_failed_driver(record, RecordedFailure::stopped, change);
    set_strike(record, Strike{}, change);
    return change;
}

} // namespace

RecordChange clear_failures(RendererRecords& records) noexcept {
    RecordChange change;
    for (DriverRecord& record : records.drivers) {
        if (holds_records(record))
            change.changed = true;
        record.strike = Strike{};
        record.struck_this_run = false;
        record.failed_driver = RecordedFailure::none;
        record.accelerated_unusable = RecordedFailure::none;
        record.failed_driver_told = false;
        record.accelerated_unusable_told = false;
        record.scale_level = RememberedRung{};
    }
    return change;
}

RecordChange set_trial(
    RendererRecords& records, StrikeKind stage, AcceleratedPath path, std::string_view driver
) {
    RecordChange change;
    if (stage != StrikeKind::probe && stage != StrikeKind::path)
        return change;
    const AcceleratedPath kept_path = stage == StrikeKind::path ? path : AcceleratedPath::magnify;
    TrialRecord& trial = records.trial;
    if (trial.stage == stage && trial.path == kept_path && trial.driver == driver)
        return change;
    trial.stage = stage;
    trial.path = kept_path;
    trial.driver = std::string(driver);
    change.changed = true;
    return change;
}

RecordChange erase_trial(RendererRecords& records) noexcept {
    RecordChange change;
    if (records.trial.stage == StrikeKind::none)
        return change;
    records.trial = TrialRecord{};
    change.changed = true;
    return change;
}

// ---------------------------------------------------------------------------
// The sentinel and the trial through a run

Leftover parse_sentinel(std::string_view value) {
    Leftover unreadable;
    unreadable.sentinel = SentinelStage::unreadable;
    const std::vector<std::string_view> words = words_of(value);
    Leftover read;
    if (words.empty() || !word_value(sentinel_words, words[0], read.sentinel))
        return unreadable;
    switch (read.sentinel) {
    case SentinelStage::path:
        if (words.size() != 3 || !word_value(path_words, words[1], read.sentinel_path))
            return unreadable;
        break;
    case SentinelStage::running:
        if (words.size() != 2)
            return unreadable;
        break;
    case SentinelStage::create:
    case SentinelStage::standard:
    case SentinelStage::probe:
    case SentinelStage::accelerated:
        if (words.size() == 4 && words[2] == via_word && is_software(words[1])) {
            // The hint's list counts only when it names one driver.
            if (words[3].find(list_separator) == std::string_view::npos)
                read.via_driver = words[3];
            read.sentinel_driver = words[1];
            return read;
        }
        if (words.size() != 2)
            return unreadable;
        break;
    case SentinelStage::none:
    case SentinelStage::unreadable:
        return unreadable;
    }
    read.sentinel_driver = words.back();
    return read;
}

std::string format_sentinel(
    SentinelStage stage, std::string_view driver, AcceleratedPath path, std::string_view via_list
) {
    std::string value(word_text(sentinel_words, stage));
    if (value.empty())
        return value;
    if (stage == SentinelStage::path) {
        value += ' ';
        value += word_text(path_words, path);
    }
    value += ' ';
    value += driver;
    const bool start_stage = stage == SentinelStage::create || stage == SentinelStage::standard ||
                             stage == SentinelStage::probe || stage == SentinelStage::accelerated;
    if (start_stage && is_software(driver) && one_word(via_list)) {
        value += ' ';
        value += via_word;
        value += ' ';
        value += via_list;
    }
    return value;
}

LeftoverOutcome
note_leftover(RendererRecords& records, const Leftover& leftover, CrashEvidence evidence) {
    LeftoverOutcome outcome;
    if (records.trial.stage != StrikeKind::none) {
        // The trial decides, and is spent.
        const TrialRecord trial = records.trial;
        outcome.change = erase_trial(records);
        if (trial.driver.empty())
            return outcome;
        Strike strike;
        strike.kind = trial.stage == StrikeKind::path ? StrikeKind::path : StrikeKind::probe;
        strike.path = trial.path;
        outcome.driver = trial.driver;
        const RecordChange change = strike_or_record(
            record_for(records, trial.driver), strike, evidence == CrashEvidence::first_counts
        );
        outcome.change.changed = outcome.change.changed || change.changed;
        outcome.change.new_record = change.new_record;
        return outcome;
    }
    switch (leftover.sentinel) {
    case SentinelStage::create:
    case SentinelStage::standard: {
        std::string_view driver = leftover.sentinel_driver;
        if (is_software(driver))
            driver = leftover.via_driver;
        if (driver.empty() || is_software(driver))
            return outcome;
        Strike strike;
        strike.kind =
            leftover.sentinel == SentinelStage::create ? StrikeKind::create : StrikeKind::standard;
        outcome.driver = std::string(driver);
        outcome.change = strike_or_record(record_for(records, driver), strike, false);
        return outcome;
    }
    case SentinelStage::running:
    case SentinelStage::unreadable:
        outcome.log_unclean_exit = true;
        return outcome;
    case SentinelStage::none:
    case SentinelStage::probe:
    case SentinelStage::accelerated:
    case SentinelStage::path:
        // The stages a trial covers are struck only through their trial.
        return outcome;
    }
    return outcome;
}

RecordChange note_start_passed(DriverRecord& record, bool function_test_ran) noexcept {
    RecordChange change;
    const StrikeKind kind = record.strike.kind;
    if (kind == StrikeKind::create || kind == StrikeKind::standard ||
        (kind == StrikeKind::probe && function_test_ran))
        set_strike(record, Strike{}, change);
    return change;
}

RecordChange note_path_passed(DriverRecord& record, AcceleratedPath path) noexcept {
    RecordChange change;
    if (record.strike.kind == StrikeKind::path && record.strike.path == path)
        set_strike(record, Strike{}, change);
    return change;
}

RecordChange note_clean_run(DriverRecord& record) noexcept {
    RecordChange change;
    const StrikeKind kind = record.strike.kind;
    if (record.struck_this_run)
        return change;
    if (kind == StrikeKind::present || kind == StrikeKind::call || kind == StrikeKind::lost ||
        kind == StrikeKind::resets)
        set_strike(record, Strike{}, change);
    return change;
}

RecordChange note_running_failure(
    DriverRecord& record,
    const Strike& failure,
    const DriverTraits& driver,
    bool render_driver_named
) noexcept {
    RecordChange change;
    if (render_driver_named || driver.loses_device_in_normal_use)
        return change;
    // Only a strike that an earlier run made counts as the run before.
    const bool repeated = !record.struck_this_run && same_strike(record.strike, failure);
    const auto strike = [&record, &failure, &change] {
        set_strike(record, failure, change);
        record.struck_this_run = true;
    };
    switch (failure.kind) {
    case StrikeKind::present:
        if (repeated && !driver.software) {
            record_failed_driver(record, RecordedFailure::present, change);
            set_strike(record, Strike{}, change);
        } else {
            strike();
        }
        return change;
    case StrikeKind::call:
        if (repeated) {
            record_accelerated_unusable(record, RecordedFailure::call, change);
            set_strike(record, Strike{}, change);
        } else {
            strike();
        }
        return change;
    case StrikeKind::lost:
    case StrikeKind::resets: {
        const RecordedFailure recorded =
            failure.kind == StrikeKind::lost ? RecordedFailure::lost : RecordedFailure::resets;
        record_accelerated_unusable(record, recorded, change);
        if (repeated && !driver.software) {
            record_failed_driver(record, recorded, change);
            set_strike(record, Strike{}, change);
        } else {
            strike();
        }
        return change;
    }
    default:
        return change;
    }
}

bool start_stage_passed(uint32_t frames, uint64_t elapsed_ns) noexcept {
    return frames >= start_stage_frames && elapsed_ns >= start_stage_ns;
}

SentinelLife start_sentinel_life(bool render_driver_named) noexcept {
    SentinelLife life;
    life.kept = !render_driver_named;
    return life;
}

namespace {

/// Returns a write that sets the sentinel alone.
///
/// @param stage the sentinel's new stage
/// @param path the path, for SentinelStage::path
/// @return the write
SentinelWrite sentinel_at(SentinelStage stage, AcceleratedPath path) noexcept {
    SentinelWrite write;
    write.set_sentinel = true;
    write.sentinel = stage;
    write.sentinel_path = path;
    return write;
}

} // namespace

SentinelWrite sentinel_step(SentinelLife& life, LifeEvent event, AcceleratedPath path) noexcept {
    if (!life.kept)
        return SentinelWrite{};
    switch (event) {
    case LifeEvent::creating:
        life.stage = SentinelStage::create;
        return sentinel_at(SentinelStage::create, AcceleratedPath::magnify);
    case LifeEvent::probing:
        life.stage = SentinelStage::standard;
        return sentinel_at(SentinelStage::standard, AcceleratedPath::magnify);
    case LifeEvent::function_test: {
        SentinelWrite write = sentinel_at(SentinelStage::probe, AcceleratedPath::magnify);
        write.write_trial = true;
        write.trial_stage = StrikeKind::probe;
        life.before_trial = life.stage;
        life.stage = SentinelStage::probe;
        life.trial = true;
        return write;
    }
    case LifeEvent::function_test_done:
        if (life.stage != SentinelStage::probe)
            return SentinelWrite{};
        life.stage = SentinelStage::standard;
        return sentinel_at(SentinelStage::standard, AcceleratedPath::magnify);
    case LifeEvent::first_accelerated_frame:
        if (life.stage != SentinelStage::standard)
            return SentinelWrite{};
        life.stage = SentinelStage::accelerated;
        return sentinel_at(SentinelStage::accelerated, AcceleratedPath::magnify);
    case LifeEvent::start_passed: {
        if (life.stage != SentinelStage::standard && life.stage != SentinelStage::accelerated)
            return SentinelWrite{};
        SentinelWrite write = sentinel_at(SentinelStage::running, AcceleratedPath::magnify);
        write.erase_trial = life.trial;
        life.stage = SentinelStage::running;
        life.trial = false;
        return write;
    }
    case LifeEvent::path_first_use: {
        // A stage that stands already covers the path's first frames.
        if (life.stage != SentinelStage::running)
            return SentinelWrite{};
        SentinelWrite write = sentinel_at(SentinelStage::path, path);
        write.write_trial = true;
        write.trial_stage = StrikeKind::path;
        write.trial_path = path;
        life.before_trial = life.stage;
        life.stage = SentinelStage::path;
        life.path = path;
        life.trial = true;
        return write;
    }
    case LifeEvent::path_passed: {
        if (life.stage != SentinelStage::path || life.path != path)
            return SentinelWrite{};
        SentinelWrite write = sentinel_at(SentinelStage::running, AcceleratedPath::magnify);
        write.erase_trial = life.trial;
        life.stage = SentinelStage::running;
        life.trial = false;
        return write;
    }
    }
    return SentinelWrite{};
}

void note_trial_unwritten(SentinelLife& life) noexcept {
    if (!life.trial)
        return;
    life.stage = life.before_trial;
    life.trial = false;
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
