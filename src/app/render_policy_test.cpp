// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The render policy, by table: the corrected texture limit and the
// renderer's capability; the tier over every combination of its inputs,
// checked against the conditions written out again, the standard tier
// under 2 GiB (a machine that reports 1.75 GiB has 2 GiB) whatever the
// flags, and the function test allowed only where the tier could be
// accelerated, so never under 2 GiB; a shared game or a replay starting
// nothing until it ends; the walk of the render drivers, skipping recorded
// ones but never software, with a framebuffer hint that is never empty, a
// second walk with the records ignored whenever they would leave nothing
// able to present, and SDL's own call under SDL_RENDER_DRIVER; present
// stalls; device resets; the layers' texture formats; the starting budget,
// the same at any memory, and the starting rung of every kind of machine,
// with the blend only above 4 GiB; the remembered rung and the step-down
// fed synthetic frames; the chrome's filter; the prescale budget; the
// tiles of textures beyond the renderer's limit; and acting on the tier as
// the game does: the windowless video drivers, the flags, what the host
// does for each decision, what Off then On forgets, one frame's step with
// its function test and its note of a shared game, the function test's
// state at start, and a player's run through the setting, a shared game
// and a failed function test, beside the runs that stay on the standard
// tier. The driver names here are made
// up: the policy reads them only as names.
#include "oa/app/render_policy.hpp"

#include "oa/test/check.hpp"

#include <array>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace oa::app::render_policy;

// ---------------------------------------------------------------------------
// The renderer's facts and its capability

void test_texture_limit() {
    struct Case {
        TextureLimitSource source;
        uint32_t reported;
        uint32_t device;
        uint32_t expected;
    };

    const Case cases[] = {
        {TextureLimitSource::reported, 16384, 0, 16384},
        {TextureLimitSource::reported, 0, 0, 0},
        {TextureLimitSource::reported, 2048, 4096, 2048},
        // A fixed report: the device's own limit when known, never above
        // the report; otherwise the report, at most 8192.
        {TextureLimitSource::fixed_report, 16384, 8192, 8192},
        {TextureLimitSource::fixed_report, 16384, 32768, 16384},
        {TextureLimitSource::fixed_report, 16384, 16384, 16384},
        {TextureLimitSource::fixed_report, 16384, 2048, 2048},
        {TextureLimitSource::fixed_report, 16384, 0, 8192},
        {TextureLimitSource::fixed_report, 4096, 0, 4096},
        {TextureLimitSource::fixed_report, 0, 4096, 4096},
        {TextureLimitSource::fixed_report, 0, 0, 8192},
    };
    for (const Case& test : cases) {
        DriverTraits driver;
        driver.texture_limit_source = test.source;
        OA_CHECK(texture_limit(driver, test.reported, test.device) == test.expected);
    }
}

/// A renderer that items 1 to 3 accept.
RendererFacts capable_renderer() {
    RendererFacts renderer;
    renderer.driver.adapter_required = true;
    renderer.max_texture_size = 16384;
    renderer.adapter_known = true;
    return renderer;
}

void test_assess_renderer() {
    struct Case {
        const char* name;
        void (*change)(RendererFacts&);
        bool legacy_windows;
        bool accept_virtual;
        Capability expected;
    };

    const Case cases[] = {
        {"capable", [](RendererFacts&) {}, false, false, Capability::capable},
        {"software",
         [](RendererFacts& r) { r.driver.software = true; },
         false,
         false,
         Capability::software_renderer},
        {"software before everything",
         [](RendererFacts& r) {
             r.driver.software = true;
             r.under_wine = true;
             r.max_texture_size = 512;
         },
         true,
         true,
         Capability::software_renderer},
        {"before vista, other driver",
         [](RendererFacts&) {},
         true,
         false,
         Capability::before_vista_driver},
        {"before vista, allowed driver",
         [](RendererFacts& r) { r.driver.capable_before_vista = true; },
         true,
         false,
         Capability::capable},
        {"allowed driver after vista",
         [](RendererFacts& r) { r.driver.capable_before_vista = true; },
         false,
         false,
         Capability::capable},
        {"texture limit 512",
         [](RendererFacts& r) { r.max_texture_size = 512; },
         false,
         false,
         Capability::small_texture_limit},
        {"texture limit 1023",
         [](RendererFacts& r) { r.max_texture_size = 1023; },
         false,
         false,
         Capability::small_texture_limit},
        {"texture limit 1024",
         [](RendererFacts& r) { r.max_texture_size = 1024; },
         false,
         false,
         Capability::capable},
        {"no texture limit",
         [](RendererFacts& r) { r.max_texture_size = 0; },
         false,
         false,
         Capability::capable},
        {"wine",
         [](RendererFacts& r) { r.under_wine = true; },
         false,
         true,
         Capability::under_wine},
        {"wine before rasteriser",
         [](RendererFacts& r) {
             r.under_wine = true;
             r.software_rasteriser = true;
         },
         false,
         false,
         Capability::under_wine},
        {"software rasteriser",
         [](RendererFacts& r) { r.software_rasteriser = true; },
         false,
         false,
         Capability::software_rasteriser},
        {"software rasteriser, virtual accepted",
         [](RendererFacts& r) { r.software_rasteriser = true; },
         false,
         true,
         Capability::software_rasteriser},
        {"virtual adapter",
         [](RendererFacts& r) { r.virtual_adapter = true; },
         false,
         false,
         Capability::virtual_adapter},
        {"virtual adapter accepted",
         [](RendererFacts& r) { r.virtual_adapter = true; },
         false,
         true,
         Capability::capable},
        {"unknown adapter where needed",
         [](RendererFacts& r) { r.adapter_known = false; },
         false,
         false,
         Capability::unknown_adapter},
        {"unknown adapter elsewhere",
         [](RendererFacts& r) {
             r.adapter_known = false;
             r.driver.adapter_required = false;
         },
         false,
         false,
         Capability::capable},
    };
    for (const Case& test : cases) {
        RendererFacts renderer = capable_renderer();
        test.change(renderer);
        const Capability capability =
            assess_renderer(renderer, test.legacy_windows, test.accept_virtual);
        if (capability != test.expected)
            std::fprintf(stderr, "assess_renderer case: %s\n", test.name);
        OA_CHECK(capability == test.expected);
    }
}

// ---------------------------------------------------------------------------
// Choosing the tier

constexpr uint64_t mebibyte = uint64_t{1} << 20;

/// A run in which every condition for the accelerated tier holds.
TierInputs accelerated_run() {
    TierInputs inputs;
    inputs.renderer = true;
    inputs.memory = 8 * gibibyte;
    inputs.players_own_profile = true;
    inputs.setting_on = true;
    inputs.capability = Capability::capable;
    inputs.function_test = FunctionTest::passed;
    return inputs;
}

void test_decide_by_table() {
    struct Case {
        const char* name;
        void (*change)(TierInputs&);
        RenderTier tier;
        TierReason reason;
    };

    const Case cases[] = {
        {"accelerated", [](TierInputs&) {}, RenderTier::accelerated, TierReason::accelerated},
        {"headless",
         [](TierInputs& i) { i.renderer = false; },
         RenderTier::standard,
         TierReason::no_renderer},
        {"director",
         [](TierInputs& i) { i.director_frame = true; },
         RenderTier::standard,
         TierReason::director_frame},
        // Under 2 GiB, and with memory not reported, the standard tier
        // whatever the flags; a machine that reports 1.75 GiB has 2 GiB.
        {"memory not reported",
         [](TierInputs& i) { i.memory = 0; },
         RenderTier::standard,
         TierReason::memory},
        {"256 MiB",
         [](TierInputs& i) { i.memory = 256 * mebibyte; },
         RenderTier::standard,
         TierReason::memory},
        {"1 GiB",
         [](TierInputs& i) { i.memory = gibibyte; },
         RenderTier::standard,
         TierReason::memory},
        {"just under 1.75 GiB",
         [](TierInputs& i) { i.memory = 1792 * mebibyte - 1; },
         RenderTier::standard,
         TierReason::memory},
        {"1.75 GiB counts as 2",
         [](TierInputs& i) { i.memory = 1792 * mebibyte; },
         RenderTier::accelerated,
         TierReason::accelerated},
        {"2 GiB",
         [](TierInputs& i) { i.memory = 2 * gibibyte; },
         RenderTier::accelerated,
         TierReason::accelerated},
        {"4 GiB",
         [](TierInputs& i) { i.memory = 4 * gibibyte; },
         RenderTier::accelerated,
         TierReason::accelerated},
        {"under 2 GiB, flag on",
         [](TierInputs& i) {
             i.memory = gibibyte;
             i.flag = AccelerationFlag::on;
         },
         RenderTier::standard,
         TierReason::memory},
        {"under 2 GiB, forced",
         [](TierInputs& i) {
             i.memory = gibibyte;
             i.force_capable = true;
         },
         RenderTier::standard,
         TierReason::memory},
        {"memory not reported, flag on and forced",
         [](TierInputs& i) {
             i.memory = 0;
             i.flag = AccelerationFlag::on;
             i.force_capable = true;
         },
         RenderTier::standard,
         TierReason::memory},
        {"under 2 GiB, flag off",
         [](TierInputs& i) {
             i.memory = gibibyte;
             i.flag = AccelerationFlag::off;
         },
         RenderTier::standard,
         TierReason::memory},
        {"under 2 GiB, headless",
         [](TierInputs& i) {
             i.memory = gibibyte;
             i.renderer = false;
         },
         RenderTier::standard,
         TierReason::no_renderer},
        {"under 2 GiB, director",
         [](TierInputs& i) {
             i.memory = 0;
             i.director_frame = true;
         },
         RenderTier::standard,
         TierReason::director_frame},
        {"flag off",
         [](TierInputs& i) { i.flag = AccelerationFlag::off; },
         RenderTier::standard,
         TierReason::flag_off},
        {"flag off beats force",
         [](TierInputs& i) {
             i.flag = AccelerationFlag::off;
             i.force_capable = true;
         },
         RenderTier::standard,
         TierReason::flag_off},
        {"environment",
         [](TierInputs& i) { i.render_driver_named = true; },
         RenderTier::standard,
         TierReason::environment},
        {"environment, flag on",
         [](TierInputs& i) {
             i.render_driver_named = true;
             i.flag = AccelerationFlag::on;
         },
         RenderTier::accelerated,
         TierReason::accelerated},
        {"environment, forced",
         [](TierInputs& i) {
             i.render_driver_named = true;
             i.force_capable = true;
         },
         RenderTier::accelerated,
         TierReason::accelerated},
        {"dummy video",
         [](TierInputs& i) { i.virtual_video_driver = true; },
         RenderTier::standard,
         TierReason::environment},
        {"dummy video, flag on",
         [](TierInputs& i) {
             i.virtual_video_driver = true;
             i.flag = AccelerationFlag::on;
         },
         RenderTier::accelerated,
         TierReason::accelerated},
        {"setting off",
         [](TierInputs& i) { i.setting_on = false; },
         RenderTier::standard,
         TierReason::setting_off},
        {"setting off, flag on",
         [](TierInputs& i) {
             i.setting_on = false;
             i.flag = AccelerationFlag::on;
         },
         RenderTier::accelerated,
         TierReason::accelerated},
        {"setting off, forced",
         [](TierInputs& i) {
             i.setting_on = false;
             i.force_capable = true;
         },
         RenderTier::standard,
         TierReason::setting_off},
        {"not capable",
         [](TierInputs& i) { i.capability = Capability::software_renderer; },
         RenderTier::standard,
         TierReason::not_capable},
        {"not capable, flag on",
         [](TierInputs& i) {
             i.capability = Capability::software_rasteriser;
             i.flag = AccelerationFlag::on;
         },
         RenderTier::standard,
         TierReason::not_capable},
        {"not capable, forced",
         [](TierInputs& i) {
             i.capability = Capability::software_renderer;
             i.force_capable = true;
         },
         RenderTier::accelerated,
         TierReason::accelerated},
        {"test failed",
         [](TierInputs& i) { i.function_test = FunctionTest::failed; },
         RenderTier::standard,
         TierReason::function_test_failed},
        {"test failed, forced",
         [](TierInputs& i) {
             i.function_test = FunctionTest::failed;
             i.force_capable = true;
         },
         RenderTier::standard,
         TierReason::function_test_failed},
        {"test not run",
         [](TierInputs& i) { i.function_test = FunctionTest::not_run; },
         RenderTier::standard,
         TierReason::function_test_due},
        {"trial unwritten",
         [](TierInputs& i) { i.function_test = FunctionTest::trial_unwritten; },
         RenderTier::standard,
         TierReason::trial_unwritten},
        {"trial unwritten, named file",
         [](TierInputs& i) {
             i.function_test = FunctionTest::trial_unwritten;
             i.players_own_profile = false;
         },
         RenderTier::standard,
         TierReason::function_test_due},
        {"trial unwritten, environment",
         [](TierInputs& i) {
             i.function_test = FunctionTest::trial_unwritten;
             i.render_driver_named = true;
             i.flag = AccelerationFlag::on;
         },
         RenderTier::standard,
         TierReason::function_test_due},
        {"records unreadable",
         [](TierInputs& i) { i.records_unreadable_after_unclean_start = true; },
         RenderTier::standard,
         TierReason::records_unreadable},
        {"records unreadable, named file",
         [](TierInputs& i) {
             i.records_unreadable_after_unclean_start = true;
             i.players_own_profile = false;
         },
         RenderTier::accelerated,
         TierReason::accelerated},
        {"accelerated-unusable",
         [](TierInputs& i) { i.accelerated_unusable_record = true; },
         RenderTier::standard,
         TierReason::accelerated_unusable},
        {"accelerated-unusable, flag on",
         [](TierInputs& i) {
             i.accelerated_unusable_record = true;
             i.flag = AccelerationFlag::on;
         },
         RenderTier::accelerated,
         TierReason::accelerated},
        {"accelerated-unusable, forced",
         [](TierInputs& i) {
             i.accelerated_unusable_record = true;
             i.force_capable = true;
         },
         RenderTier::standard,
         TierReason::accelerated_unusable},
        {"dropped: driver",
         [](TierInputs& i) { i.drop = Drop::driver_failure; },
         RenderTier::standard,
         TierReason::dropped},
        {"dropped: engine fault",
         [](TierInputs& i) { i.drop = Drop::engine_fault; },
         RenderTier::standard,
         TierReason::dropped},
        {"dropped: memory",
         [](TierInputs& i) { i.drop = Drop::memory; },
         RenderTier::standard,
         TierReason::dropped},
        {"dropped: stall",
         [](TierInputs& i) { i.drop = Drop::stall; },
         RenderTier::standard,
         TierReason::dropped},
        {"dropped: slow frames",
         [](TierInputs& i) { i.drop = Drop::slow_frames; },
         RenderTier::standard,
         TierReason::dropped},
        {"dropped: path trial",
         [](TierInputs& i) { i.drop = Drop::path_trial_unwritten; },
         RenderTier::standard,
         TierReason::dropped},
        {"dropped, flag on",
         [](TierInputs& i) {
             i.drop = Drop::memory;
             i.flag = AccelerationFlag::on;
         },
         RenderTier::standard,
         TierReason::dropped},
        {"device lost",
         [](TierInputs& i) { i.device_lost = true; },
         RenderTier::standard,
         TierReason::device_lost},
        {"shared game begun accelerated",
         [](TierInputs& i) { i.match = SharedMatchGate{MatchKind::shared_game, true}; },
         RenderTier::accelerated,
         TierReason::accelerated},
        {"shared game begun standard",
         [](TierInputs& i) { i.match = SharedMatchGate{MatchKind::shared_game, false}; },
         RenderTier::standard,
         TierReason::waiting_for_match_end},
        {"replay begun standard",
         [](TierInputs& i) { i.match = SharedMatchGate{MatchKind::replay, false}; },
         RenderTier::standard,
         TierReason::waiting_for_match_end},
        {"shared game, test not run",
         [](TierInputs& i) {
             i.match = SharedMatchGate{MatchKind::shared_game, false};
             i.function_test = FunctionTest::not_run;
         },
         RenderTier::standard,
         TierReason::waiting_for_match_end},
    };
    for (const Case& test : cases) {
        TierInputs inputs = accelerated_run();
        test.change(inputs);
        const TierDecision decision = decide_render_tier(inputs);
        if (decision.tier != test.tier || decision.reason != test.reason)
            std::fprintf(stderr, "decide_render_tier case: %s\n", test.name);
        OA_CHECK(decision.tier == test.tier);
        OA_CHECK(decision.reason == test.reason);
    }
}

/// The conditions for the accelerated tier, written out again apart from
/// the code under test.
///
/// @param in the inputs
/// @param function_test the function test's state to judge them with
/// @return true when every condition holds
bool accelerated_by_the_rules(const TierInputs& in, FunctionTest function_test) {
    const bool flag_on = in.flag == AccelerationFlag::on;
    const bool on_disk = in.players_own_profile && !in.render_driver_named;
    return in.renderer && !in.director_frame && in.memory >= 1792 * mebibyte &&
           in.flag != AccelerationFlag::off &&
           (flag_on || in.force_capable || (!in.render_driver_named && !in.virtual_video_driver)) &&
           (in.setting_on || flag_on) &&
           (in.capability == Capability::capable || in.force_capable) &&
           function_test == FunctionTest::passed &&
           !(in.records_unreadable_after_unclean_start && on_disk) &&
           !(in.accelerated_unusable_record && !flag_on) && in.drop == Drop::none &&
           !in.device_lost && !(in.match.kind != MatchKind::none && !in.match.accelerated);
}

void test_decide_every_combination() {
    const AccelerationFlag flags[] = {
        AccelerationFlag::none, AccelerationFlag::on, AccelerationFlag::off
    };
    const FunctionTest tests[] = {
        FunctionTest::not_run,
        FunctionTest::passed,
        FunctionTest::failed,
        FunctionTest::trial_unwritten
    };
    const SharedMatchGate gates[] = {
        SharedMatchGate{},
        SharedMatchGate{MatchKind::shared_game, true},
        SharedMatchGate{MatchKind::replay, false}
    };
    const uint64_t memories[] = {0, 1792 * mebibyte - 1, 1792 * mebibyte, 16 * gibibyte};
    uint32_t combinations = 0;
    uint32_t accelerated = 0;
    uint32_t tests_allowed_under_2_gib = 0;
    uint32_t mismatches = 0;
    for (uint32_t bits = 0; bits < (1u << 12); ++bits) {
        for (const AccelerationFlag flag : flags) {
            for (const FunctionTest test : tests) {
                for (const SharedMatchGate& gate : gates) {
                    for (const uint64_t memory : memories) {
                        TierInputs in;
                        in.memory = memory;
                        in.renderer = (bits & 1u) != 0;
                        in.director_frame = (bits & 2u) != 0;
                        in.force_capable = (bits & 4u) != 0;
                        in.render_driver_named = (bits & 8u) != 0;
                        in.virtual_video_driver = (bits & 16u) != 0;
                        in.players_own_profile = (bits & 32u) != 0;
                        in.setting_on = (bits & 64u) != 0;
                        in.capability = (bits & 128u) != 0 ? Capability::capable
                                                           : Capability::software_renderer;
                        in.accelerated_unusable_record = (bits & 256u) != 0;
                        in.records_unreadable_after_unclean_start = (bits & 512u) != 0;
                        in.drop = (bits & 1024u) != 0 ? Drop::memory : Drop::none;
                        in.device_lost = (bits & 2048u) != 0;
                        in.flag = flag;
                        in.function_test = test;
                        in.match = gate;
                        ++combinations;
                        const TierDecision decision = decide_render_tier(in);
                        const bool expected = accelerated_by_the_rules(in, test);
                        if ((decision.tier == RenderTier::accelerated) != expected)
                            ++mismatches;
                        if ((decision.reason == TierReason::accelerated) !=
                            (decision.tier == RenderTier::accelerated))
                            ++mismatches;
                        // The function test may run exactly where the tier would
                        // be accelerated once it passed, and only when it has not
                        // run, or skipped a trial that lives in memory and so
                        // cannot fail to be written.
                        const bool untested =
                            test == FunctionTest::not_run ||
                            (test == FunctionTest::trial_unwritten &&
                             !records_on_disk(in.players_own_profile, in.render_driver_named));
                        if (function_test_may_run(in) !=
                            (untested && accelerated_by_the_rules(in, FunctionTest::passed)))
                            ++mismatches;
                        // Under 2 GiB, or with memory not reported, the tier is
                        // standard for the memory alone, and the function test
                        // never runs.
                        if (memory < 1792 * mebibyte && in.renderer && !in.director_frame &&
                            decide_render_tier(in).reason != TierReason::memory)
                            ++mismatches;
                        if (memory < 1792 * mebibyte && function_test_may_run(in))
                            ++tests_allowed_under_2_gib;
                        if (expected)
                            ++accelerated;
                    }
                }
            }
        }
    }
    OA_CHECK(combinations == (1u << 12) * 3 * 4 * 3 * 4);
    OA_CHECK(mismatches == 0);
    OA_CHECK(tests_allowed_under_2_gib == 0);
    OA_CHECK(accelerated > 0);
    OA_CHECK(smallest_accelerated_memory == 1792 * mebibyte);
}

void test_function_test_gate() {
    struct Case {
        const char* name;
        void (*change)(TierInputs&);
        bool may_run;
    };

    const Case cases[] = {
        {"capable, setting on", [](TierInputs&) {}, true},
        {"software renderer",
         [](TierInputs& i) { i.capability = Capability::software_renderer; },
         false},
        {"software renderer, forced",
         [](TierInputs& i) {
             i.capability = Capability::software_renderer;
             i.force_capable = true;
         },
         true},
        {"software rasteriser, flag on",
         [](TierInputs& i) {
             i.capability = Capability::software_rasteriser;
             i.flag = AccelerationFlag::on;
         },
         false},
        {"virtual adapter",
         [](TierInputs& i) { i.capability = Capability::virtual_adapter; },
         false},
        {"wine", [](TierInputs& i) { i.capability = Capability::under_wine; }, false},
        {"before vista",
         [](TierInputs& i) { i.capability = Capability::before_vista_driver; },
         false},
        {"small texture limit",
         [](TierInputs& i) { i.capability = Capability::small_texture_limit; },
         false},
        {"flag off", [](TierInputs& i) { i.flag = AccelerationFlag::off; }, false},
        {"environment", [](TierInputs& i) { i.render_driver_named = true; }, false},
        {"environment, flag on",
         [](TierInputs& i) {
             i.render_driver_named = true;
             i.flag = AccelerationFlag::on;
         },
         true},
        {"dummy video", [](TierInputs& i) { i.virtual_video_driver = true; }, false},
        {"setting off", [](TierInputs& i) { i.setting_on = false; }, false},
        {"setting off, named file",
         [](TierInputs& i) {
             i.setting_on = false;
             i.players_own_profile = false;
         },
         false},
        {"setting on, named file", [](TierInputs& i) { i.players_own_profile = false; }, true},
        {"accelerated-unusable",
         [](TierInputs& i) { i.accelerated_unusable_record = true; },
         false},
        {"accelerated-unusable, flag on",
         [](TierInputs& i) {
             i.accelerated_unusable_record = true;
             i.flag = AccelerationFlag::on;
         },
         true},
        {"shared game",
         [](TierInputs& i) { i.match = SharedMatchGate{MatchKind::shared_game, false}; },
         false},
        {"replay",
         [](TierInputs& i) { i.match = SharedMatchGate{MatchKind::replay, false}; },
         false},
        {"headless", [](TierInputs& i) { i.renderer = false; }, false},
        {"dropped", [](TierInputs& i) { i.drop = Drop::driver_failure; }, false},
        {"device lost", [](TierInputs& i) { i.device_lost = true; }, false},
        // A test that failed, or whose trial could not be written on disk,
        // does not run again until a retry sets it back to not run.
        {"test already failed",
         [](TierInputs& i) { i.function_test = FunctionTest::failed; },
         false},
        {"trial unwritten",
         [](TierInputs& i) { i.function_test = FunctionTest::trial_unwritten; },
         false},
        {"trial unwritten, named file",
         [](TierInputs& i) {
             i.function_test = FunctionTest::trial_unwritten;
             i.players_own_profile = false;
         },
         true},
        {"test passed", [](TierInputs& i) { i.function_test = FunctionTest::passed; }, false},
        // Never under 2 GiB, nor with memory not reported, whatever the
        // flags; a machine that reports 1.75 GiB has 2 GiB.
        {"under 2 GiB", [](TierInputs& i) { i.memory = gibibyte; }, false},
        {"memory not reported", [](TierInputs& i) { i.memory = 0; }, false},
        {"just under 1.75 GiB", [](TierInputs& i) { i.memory = 1792 * mebibyte - 1; }, false},
        {"1.75 GiB", [](TierInputs& i) { i.memory = 1792 * mebibyte; }, true},
        {"under 2 GiB, flag on",
         [](TierInputs& i) {
             i.memory = gibibyte;
             i.flag = AccelerationFlag::on;
         },
         false},
        {"under 2 GiB, forced",
         [](TierInputs& i) {
             i.memory = gibibyte;
             i.capability = Capability::software_renderer;
             i.force_capable = true;
         },
         false},
        {"memory not reported, flag on and forced",
         [](TierInputs& i) {
             i.memory = 0;
             i.render_driver_named = true;
             i.flag = AccelerationFlag::on;
             i.force_capable = true;
         },
         false},
    };
    for (const Case& test : cases) {
        TierInputs inputs = accelerated_run();
        inputs.function_test = FunctionTest::not_run;
        test.change(inputs);
        if (function_test_may_run(inputs) != test.may_run)
            std::fprintf(stderr, "function_test_may_run case: %s\n", test.name);
        OA_CHECK(function_test_may_run(inputs) == test.may_run);
    }
    OA_CHECK(records_on_disk(true, false));
    OA_CHECK(!records_on_disk(false, false));
    OA_CHECK(!records_on_disk(true, true));
}

void test_shared_match_gate() {
    // Accelerated as the loading screen begins: it stays so until something
    // leaves the tier, and then waits for the match to end.
    TierInputs inputs = accelerated_run();
    begin_match(
        inputs.match,
        MatchKind::shared_game,
        decide_render_tier(inputs).tier == RenderTier::accelerated
    );
    OA_CHECK(inputs.match.kind == MatchKind::shared_game);
    OA_CHECK(decide_render_tier(inputs).tier == RenderTier::accelerated);
    inputs.setting_on = false;
    TierDecision decision = decide_render_tier(inputs);
    OA_CHECK(decision.reason == TierReason::setting_off);
    note_match_frame(inputs.match, decision.tier, false);
    inputs.setting_on = true;
    decision = decide_render_tier(inputs);
    OA_CHECK(decision.tier == RenderTier::standard);
    OA_CHECK(decision.reason == TierReason::waiting_for_match_end);
    end_match(inputs.match);
    OA_CHECK(decide_render_tier(inputs).tier == RenderTier::accelerated);

    // A lost device that a reset brings back does not stop it.
    begin_match(inputs.match, MatchKind::replay, true);
    inputs.device_lost = true;
    decision = decide_render_tier(inputs);
    OA_CHECK(decision.reason == TierReason::device_lost);
    note_match_frame(inputs.match, decision.tier, true);
    inputs.device_lost = false;
    OA_CHECK(decide_render_tier(inputs).tier == RenderTier::accelerated);
    // A drop, then lifting it, still waits.
    inputs.drop = Drop::driver_failure;
    note_match_frame(inputs.match, decide_render_tier(inputs).tier, false);
    inputs.drop = Drop::none;
    OA_CHECK(decide_render_tier(inputs).reason == TierReason::waiting_for_match_end);
    end_match(inputs.match);

    // A device reset that destroyed the tier's textures keeps it standard.
    begin_match(inputs.match, MatchKind::shared_game, true);
    stop_until_match_end(inputs.match);
    OA_CHECK(decide_render_tier(inputs).reason == TierReason::waiting_for_match_end);
    end_match(inputs.match);

    // Begun on the standard tier, with the test not yet run: neither the
    // setting nor the test starts anything before the match ends.
    inputs = accelerated_run();
    inputs.setting_on = false;
    inputs.function_test = FunctionTest::not_run;
    begin_match(
        inputs.match,
        MatchKind::shared_game,
        decide_render_tier(inputs).tier == RenderTier::accelerated
    );
    OA_CHECK(!inputs.match.accelerated);
    inputs.setting_on = true;
    OA_CHECK(decide_render_tier(inputs).reason == TierReason::waiting_for_match_end);
    OA_CHECK(!function_test_may_run(inputs));
    end_match(inputs.match);
    OA_CHECK(function_test_may_run(inputs));

    // A match played alone is no shared match.
    begin_match(inputs.match, MatchKind::none, true);
    OA_CHECK(inputs.match.kind == MatchKind::none);
    OA_CHECK(!inputs.match.accelerated);
    note_match_frame(inputs.match, RenderTier::standard, false);
    inputs.function_test = FunctionTest::passed;
    OA_CHECK(decide_render_tier(inputs).tier == RenderTier::accelerated);

    // Resources are made for the first time only as the loading screen
    // begins.
    SharedMatchGate gate;
    OA_CHECK(first_use_allowed(gate, false));
    OA_CHECK(first_use_allowed(gate, true));
    begin_match(gate, MatchKind::shared_game, true);
    OA_CHECK(first_use_allowed(gate, true));
    OA_CHECK(!first_use_allowed(gate, false));
    begin_match(gate, MatchKind::replay, false);
    OA_CHECK(!first_use_allowed(gate, false));
    end_match(gate);
    OA_CHECK(first_use_allowed(gate, false));
}

// ---------------------------------------------------------------------------
// Creating the renderer

/// SDL's order in these cases: three hardware drivers, then software.
constexpr std::array<std::string_view, 4> sdl_order = {"alpha", "beta", "gamma", "software"};

/// Walks the drivers, refusing those named, and writes down each attempt:
/// a driver's name, then for software the hint as [value] or {} when
/// unset; `*` for SDL's own call; `+` after an attempt made with the
/// records ignored; and `none` when nothing was left.
///
/// @param inputs the drivers and the records
/// @param refusing the drivers that refuse
/// @param rebuild_after the driver that failed, for a rebuild; empty for a start
/// @return the attempts, separated by spaces
std::string walk(
    const CreationInputs& inputs,
    std::vector<std::string_view> refusing,
    std::string_view rebuild_after = {}
) {
    CreationWalk state =
        rebuild_after.empty() ? start_creation(inputs) : start_rebuild(inputs, rebuild_after);
    std::string attempts;
    for (int guard = 0; guard < 64; ++guard) {
        const Attempt attempt = next_attempt(state, inputs);
        if (!attempts.empty())
            attempts += ' ';
        if (attempt.kind == AttemptKind::none) {
            attempts += "none";
            return attempts;
        }
        OA_CHECK(!attempt.set_framebuffer_hint || !attempt.framebuffer_hint.empty());
        std::string_view driver = attempt.driver;
        if (attempt.kind == AttemptKind::sdl_choice) {
            attempts += '*';
            driver = "*";
        } else {
            attempts += attempt.driver;
            std::string lower(attempt.driver);
            for (char& letter : lower)
                letter = static_cast<char>(std::tolower(static_cast<unsigned char>(letter)));
            if (lower == software_driver)
                attempts += attempt.set_framebuffer_hint ? "[" + attempt.framebuffer_hint + "]"
                                                         : std::string("{}");
            else
                OA_CHECK(!attempt.set_framebuffer_hint);
        }
        if (attempt.records_ignored)
            attempts += '+';
        bool refused = false;
        for (const std::string_view name : refusing)
            if (name == driver)
                refused = true;
        if (!refused)
            return attempts;
    }
    return attempts + " (no end)";
}

void test_creation_walk_by_table() {
    struct Case {
        const char* name;
        std::vector<std::string_view> recorded;
        std::vector<std::string_view> refusing;
        bool native;
        bool list;
        const char* expected;
    };

    const std::vector<Case> cases = {
        {"first driver starts", {}, {}, true, true, "alpha"},
        {"one refuses", {}, {"alpha"}, true, true, "alpha beta"},
        {"hardware refuses, window framebuffer",
         {},
         {"alpha", "beta", "gamma"},
         true,
         true,
         "alpha beta gamma software[0]"},
        {"hardware refuses, needs a driver",
         {},
         {"alpha", "beta", "gamma"},
         false,
         true,
         "alpha beta gamma software[alpha,beta,gamma]"},
        {"hardware refuses, single name",
         {},
         {"alpha", "beta", "gamma"},
         false,
         false,
         "alpha beta gamma software[alpha]"},
        {"recorded driver skipped", {"alpha"}, {}, true, true, "beta"},
        {"recorded driver left out of the hint",
         {"beta"},
         {"alpha", "gamma"},
         false,
         true,
         "alpha gamma software[alpha,gamma]"},
        {"recorded first left out of a single name",
         {"alpha"},
         {"beta", "gamma"},
         false,
         false,
         "beta gamma software[beta]"},
        {"all recorded, window framebuffer",
         {"alpha", "beta", "gamma"},
         {},
         true,
         true,
         "software[0]"},
        {"all recorded, needs a driver: walk again",
         {"alpha", "beta", "gamma"},
         {},
         false,
         true,
         "alpha+"},
        {"all recorded, needs a driver, all refuse",
         {"alpha", "beta", "gamma"},
         {"alpha", "beta", "gamma", "software"},
         false,
         true,
         "alpha+ beta+ gamma+ software[alpha,beta,gamma]+ none"},
        {"some recorded, software refuses: walk again",
         {"beta"},
         {"alpha", "gamma", "software"},
         false,
         true,
         "alpha gamma software[alpha,gamma] alpha+ beta+"},
        {"recorded, window framebuffer, software refuses",
         {"alpha"},
         {"beta", "gamma", "software"},
         true,
         true,
         "beta gamma software[0] alpha+"},
        {"software never skipped",
         {"software"},
         {"alpha", "beta", "gamma"},
         true,
         true,
         "alpha beta gamma software[0]"},
        {"nothing recorded, everything refuses",
         {},
         {"alpha", "beta", "gamma", "software"},
         true,
         true,
         "alpha beta gamma software[0] none"},
    };
    for (const Case& test : cases) {
        CreationInputs inputs;
        inputs.sdl_order = sdl_order;
        inputs.failed_drivers = test.recorded;
        inputs.native_window_framebuffer = test.native;
        inputs.hint_takes_list = test.list;
        const std::string attempts = walk(inputs, test.refusing);
        if (attempts != test.expected)
            std::fprintf(stderr, "walk case: %s: %s\n", test.name, attempts.c_str());
        OA_CHECK(attempts == test.expected);
    }

    // With software alone in SDL's order, it is tried with no hint.
    const std::array<std::string_view, 1> software_only = {"software"};
    CreationInputs alone;
    alone.sdl_order = software_only;
    OA_CHECK(walk(alone, {}) == "software{}");
    alone.native_window_framebuffer = true;
    OA_CHECK(walk(alone, {}) == "software{}");
}

void test_environment_and_rebuilds() {
    // SDL_RENDER_DRIVER: SDL's own call, once, with no hint and no records.
    const std::vector<std::string_view> recorded = {"alpha", "beta"};
    const std::array<std::string_view, 2> environment = {"beta", "gamma"};
    CreationInputs inputs;
    inputs.sdl_order = sdl_order;
    inputs.failed_drivers = recorded;
    inputs.environment_order = environment;
    inputs.render_driver_named = true;
    inputs.hint_takes_list = true;
    OA_CHECK(walk(inputs, {}) == "*");
    OA_CHECK(walk(inputs, {"*"}) == "* none");

    // A rebuild under SDL_RENDER_DRIVER: its later drivers, then software,
    // with a hint that leaves out only the failed driver; no second walk.
    OA_CHECK(walk(inputs, {"gamma", "software"}, "beta") == "gamma software[alpha,gamma] none");
    inputs.native_window_framebuffer = true;
    OA_CHECK(walk(inputs, {"gamma"}, "beta") == "gamma software[0]");
    OA_CHECK(walk(inputs, {"gamma", "software"}, "gamma") == "software[0] none");
    const std::array<std::string_view, 3> with_software = {"beta", "software", "gamma"};
    inputs.environment_order = with_software;
    OA_CHECK(walk(inputs, {"software", "gamma"}, "beta") == "software[0] gamma none");

    // A rebuild: the drivers after the failed one, skipping recorded ones,
    // then software with the failed one left out of the hint, then SDL's
    // full order again with the records ignored.
    CreationInputs rebuild;
    rebuild.sdl_order = sdl_order;
    rebuild.hint_takes_list = true;
    OA_CHECK(walk(rebuild, {}, "beta") == "gamma");
    OA_CHECK(walk(rebuild, {"gamma"}, "beta") == "gamma software[alpha,gamma]");
    OA_CHECK(
        walk(rebuild, {"gamma", "software", "alpha", "beta"}, "beta") ==
        "gamma software[alpha,gamma] alpha+ beta+ gamma+ software[alpha,gamma]+ none"
    );
    const std::vector<std::string_view> alpha_recorded = {"alpha"};
    rebuild.failed_drivers = alpha_recorded;
    OA_CHECK(walk(rebuild, {}, "gamma") == "software[beta]");
    const std::vector<std::string_view> two_recorded = {"alpha", "beta"};
    rebuild.failed_drivers = two_recorded;
    OA_CHECK(walk(rebuild, {}, "gamma") == "alpha+");
    OA_CHECK(
        walk(rebuild, {"alpha", "beta", "gamma"}, "gamma") ==
        "alpha+ beta+ gamma+ software[alpha,beta]+"
    );
    rebuild.native_window_framebuffer = true;
    OA_CHECK(walk(rebuild, {}, "gamma") == "software[0]");

    // SDL matches SDL_RENDER_DRIVER's names in any letter case, so a rebuild
    // finds the failed driver in the list however the tester spelled it,
    // and never tries it again.
    CreationInputs spelled;
    spelled.sdl_order = sdl_order;
    spelled.render_driver_named = true;
    spelled.hint_takes_list = true;
    const std::array<std::string_view, 2> beta_first = {"Beta", "gamma"};
    spelled.environment_order = beta_first;
    OA_CHECK(walk(spelled, {"gamma"}, "beta") == "gamma software[alpha,gamma]");
    OA_CHECK(walk(spelled, {"gamma", "software"}, "beta") == "gamma software[alpha,gamma] none");
    const std::array<std::string_view, 4> named_twice = {"beta", "GAMMA", "BETA", "Software"};
    spelled.environment_order = named_twice;
    OA_CHECK(walk(spelled, {"GAMMA"}, "beta") == "GAMMA Software[alpha,gamma]");
    OA_CHECK(walk(spelled, {"GAMMA", "Software"}, "beta") == "GAMMA Software[alpha,gamma] none");
    // A failed driver the list does not name: the whole list, never it.
    const std::array<std::string_view, 2> without_it = {"gamma", "alpha"};
    spelled.environment_order = without_it;
    OA_CHECK(walk(spelled, {"gamma", "alpha"}, "beta") == "gamma alpha software[alpha,gamma]");
}

void test_creation_walk_never_fails_for_records() {
    // Whatever is recorded and whatever refuses, a start fails only when
    // every driver refuses, never sets an empty hint, and always ends.
    const std::array<std::string_view, 4> names = sdl_order;
    uint32_t walks = 0;
    for (uint32_t recorded_bits = 0; recorded_bits < 16; ++recorded_bits) {
        for (uint32_t refusing_bits = 0; refusing_bits < 16; ++refusing_bits) {
            for (int variant = 0; variant < 4; ++variant) {
                std::vector<std::string_view> recorded;
                std::vector<std::string_view> refusing;
                for (uint32_t index = 0; index < names.size(); ++index) {
                    if ((recorded_bits >> index) & 1u)
                        recorded.push_back(names[index]);
                    if ((refusing_bits >> index) & 1u)
                        refusing.push_back(names[index]);
                }
                CreationInputs inputs;
                inputs.sdl_order = sdl_order;
                inputs.failed_drivers = recorded;
                inputs.native_window_framebuffer = (variant & 1) != 0;
                inputs.hint_takes_list = (variant & 2) != 0;
                const std::string attempts = walk(inputs, refusing);
                ++walks;
                const bool failed =
                    attempts.size() >= 4 && attempts.compare(attempts.size() - 4, 4, "none") == 0;
                OA_CHECK(attempts.find("(no end)") == std::string::npos);
                OA_CHECK(failed == (refusing_bits == 15));
                // With nothing recorded the walk makes SDL's own attempts in
                // SDL's order.
                if (recorded_bits == 0 && refusing_bits == 0)
                    OA_CHECK(attempts == "alpha");
            }
        }
    }
    OA_CHECK(walks == 16 * 16 * 4);
}

// ---------------------------------------------------------------------------
// Present stalls

void test_stalls() {
    constexpr uint64_t second = 1'000'000'000;
    // The standard tier logs once; the accelerated tier drops.
    StallWatch watch;
    OA_CHECK(
        note_present(watch, 1 * second, stall_present_ns, RenderTier::standard) == StallAction::none
    );
    OA_CHECK(
        note_present(watch, 2 * second, stall_present_ns + 1, RenderTier::standard) ==
        StallAction::none
    );
    OA_CHECK(
        note_present(watch, 5 * second, 3 * second, RenderTier::standard) == StallAction::none
    );
    OA_CHECK(note_present(watch, 8 * second, 3 * second, RenderTier::standard) == StallAction::log);
    for (uint64_t time = 9; time < 15; ++time)
        OA_CHECK(
            note_present(watch, time * second, 3 * second, RenderTier::standard) ==
            StallAction::none
        );
    StallWatch accelerated;
    OA_CHECK(
        note_present(accelerated, 1 * second, 3 * second, RenderTier::accelerated) ==
        StallAction::none
    );
    OA_CHECK(
        note_present(accelerated, 2 * second, 3 * second, RenderTier::accelerated) ==
        StallAction::none
    );
    OA_CHECK(
        note_present(accelerated, 3 * second, 3 * second, RenderTier::accelerated) ==
        StallAction::drop
    );
    // Three stalls spread over more than 10 s do nothing.
    StallWatch spread;
    OA_CHECK(note_present(spread, 0, 3 * second, RenderTier::accelerated) == StallAction::none);
    OA_CHECK(
        note_present(spread, 6 * second, 3 * second, RenderTier::accelerated) == StallAction::none
    );
    OA_CHECK(
        note_present(spread, 12 * second, 3 * second, RenderTier::accelerated) == StallAction::none
    );
    OA_CHECK(
        note_present(spread, 15 * second, 3 * second, RenderTier::accelerated) == StallAction::drop
    );
}

// ---------------------------------------------------------------------------
// The step-down ladder

void test_start_budget() {
    // The rules written out again: none with two processors or fewer, on
    // ARM other than Apple silicon and the Pis that have been run, before
    // Vista and in a class nobody has run; reduced with three processors and
    // in a class run but not measured; full otherwise. Memory never enters
    // it: the same at 1.75 GiB, 2 GiB, 4 GiB and above, and unreported.
    const ClassTesting classes[] = {
        ClassTesting::untested, ClassTesting::tested, ClassTesting::measured
    };
    const uint64_t memories[] = {
        0, 1792 * mebibyte, 2 * gibibyte, 4 * gibibyte, 4 * gibibyte + 1, 64 * gibibyte
    };
    uint32_t mismatches = 0;
    uint32_t full = 0;
    for (uint32_t processors = 1; processors <= 16; ++processors) {
        for (const ClassTesting run_class : classes) {
            for (uint32_t bits = 0; bits < 4; ++bits) {
                StartInputs machine;
                machine.processors = processors;
                machine.run_class = run_class;
                machine.other_arm = (bits & 1u) != 0;
                machine.legacy_windows = (bits & 2u) != 0;
                SceneBudget expected = SceneBudget::full;
                if (processors <= 2 || machine.other_arm || machine.legacy_windows ||
                    run_class == ClassTesting::untested)
                    expected = SceneBudget::none;
                else if (processors == 3 || run_class == ClassTesting::tested)
                    expected = SceneBudget::reduced;
                for (const uint64_t memory : memories) {
                    machine.memory = memory;
                    if (start_budget(machine) != expected || start_rung(machine).budget != expected)
                        ++mismatches;
                }
                if (expected == SceneBudget::full)
                    ++full;
            }
        }
    }
    OA_CHECK(mismatches == 0);
    OA_CHECK(full > 0);
}

void test_start_rung() {
    struct Case {
        const char* name;
        void (*change)(StartInputs&);
        SceneBudget budget;
        bool magnify;
        CardFilter card;
        bool blend_allowed;
    };

    const Case cases[] = {
        {"measured class",
         [](StartInputs&) {},
         SceneBudget::full,
         true,
         CardFilter::prescale_full,
         true},
        {"four processors",
         [](StartInputs& m) { m.processors = 4; },
         SceneBudget::full,
         true,
         CardFilter::prescale_full,
         true},
        {"three processors",
         [](StartInputs& m) { m.processors = 3; },
         SceneBudget::reduced,
         true,
         CardFilter::prescale_full,
         true},
        {"two processors",
         [](StartInputs& m) { m.processors = 2; },
         SceneBudget::none,
         false,
         CardFilter::prescale_full,
         true},
        {"light machine",
         [](StartInputs& m) {
             m.processors = 1;
             m.light_machine = true;
         },
         SceneBudget::none,
         false,
         CardFilter::prescale_quarter,
         true},
        {"tested class",
         [](StartInputs& m) {
             m.run_class = ClassTesting::tested;
             m.pixelart = true;
         },
         SceneBudget::reduced,
         true,
         CardFilter::pixelart,
         true},
        {"untested class",
         [](StartInputs& m) {
             m.run_class = ClassTesting::untested;
             m.pixelart = true;
         },
         SceneBudget::none,
         false,
         CardFilter::pixelart,
         true},
        {"other ARM, measured class",
         [](StartInputs& m) { m.other_arm = true; },
         SceneBudget::none,
         false,
         CardFilter::prescale_full,
         true},
        {"pi, 4 GiB",
         [](StartInputs& m) {
             m.processors = 4;
             m.memory = 4 * gibibyte;
             m.raspberry_pi = true;
             m.other_arm = true;
             m.run_class = ClassTesting::untested;
         },
         SceneBudget::none,
         false,
         CardFilter::prescale_quarter,
         false},
        {"pi, 8 GiB, pixelart",
         [](StartInputs& m) {
             m.processors = 4;
             m.memory = 8 * gibibyte;
             m.raspberry_pi = true;
             m.other_arm = true;
             m.pixelart = true;
         },
         SceneBudget::none,
         false,
         CardFilter::pixelart,
         true},
        {"before vista, 2 GiB",
         [](StartInputs& m) {
             m.processors = 2;
             m.memory = 2 * gibibyte;
             m.legacy_windows = true;
             m.driver.blend_excluded = true;
         },
         SceneBudget::none,
         false,
         CardFilter::prescale_full,
         false},
        {"before vista, run class",
         [](StartInputs& m) {
             m.legacy_windows = true;
             m.driver.blend_excluded = true;
         },
         SceneBudget::none,
         false,
         CardFilter::prescale_full,
         false},
        {"driver that excludes the blend",
         [](StartInputs& m) { m.driver.blend_excluded = true; },
         SceneBudget::full,
         true,
         CardFilter::prescale_full,
         false},
        {"blend not available",
         [](StartInputs& m) { m.blend_available = false; },
         SceneBudget::full,
         true,
         CardFilter::prescale_full,
         false},
        // Memory from 2 GiB sizes nothing but the blend, which needs more
        // than 4 GiB.
        {"1.75 GiB",
         [](StartInputs& m) { m.memory = 1792 * mebibyte; },
         SceneBudget::full,
         true,
         CardFilter::prescale_full,
         false},
        {"2 GiB",
         [](StartInputs& m) { m.memory = 2 * gibibyte; },
         SceneBudget::full,
         true,
         CardFilter::prescale_full,
         false},
        {"4 GiB",
         [](StartInputs& m) { m.memory = 4 * gibibyte; },
         SceneBudget::full,
         true,
         CardFilter::prescale_full,
         false},
        {"just over 4 GiB",
         [](StartInputs& m) { m.memory = 4 * gibibyte + 1; },
         SceneBudget::full,
         true,
         CardFilter::prescale_full,
         true},
        {"4 GiB, light machine",
         [](StartInputs& m) {
             m.memory = 4 * gibibyte;
             m.light_machine = true;
         },
         SceneBudget::full,
         true,
         CardFilter::prescale_quarter,
         false},
    };
    for (const Case& test : cases) {
        StartInputs machine;
        machine.processors = 8;
        machine.memory = 16 * gibibyte;
        machine.run_class = ClassTesting::measured;
        machine.blend_available = true;
        test.change(machine);
        const LadderState state = start_rung(machine);
        // No machine starts at the NEAREST-chrome rung by its size.
        const bool matches = state.budget == test.budget && state.magnify == test.magnify &&
                             state.filtered_chrome && state.card == test.card &&
                             state.blend_allowed == test.blend_allowed && !state.standard &&
                             state.method == ZoomOutMethod::area;
        if (!matches)
            std::fprintf(stderr, "start_rung case: %s\n", test.name);
        OA_CHECK(matches);
    }
    OA_CHECK(most_memory_without_blend == 4 * gibibyte);
    OA_CHECK(!magnify_measured_at_budget_none);
    OA_CHECK(!magnify_measured_before_vista);
}

/// A machine that starts at the top of the ladder.
StartInputs measured_desktop() {
    StartInputs machine;
    machine.processors = 8;
    machine.memory = 16 * gibibyte;
    machine.run_class = ClassTesting::measured;
    machine.blend_available = true;
    return machine;
}

void test_step_down_rungs() {
    const LadderState top = start_rung(measured_desktop());
    OA_CHECK(top.budget == SceneBudget::full && top.magnify && top.filtered_chrome);
    OA_CHECK(top.card == CardFilter::prescale_full && top.blend_allowed);

    // Zoomed-out frames: the blend first where it helps, then the budget.
    LadderState state = step_down(top, FrameKind::zoomed_out, true);
    OA_CHECK(state.method == ZoomOutMethod::blend && state.budget == SceneBudget::full);
    state = step_down(state, FrameKind::zoomed_out, true);
    OA_CHECK(state.budget == SceneBudget::reduced);
    state = step_down(top, FrameKind::zoomed_out, false);
    OA_CHECK(state.method == ZoomOutMethod::area && state.budget == SceneBudget::reduced);
    LadderState no_blend = top;
    no_blend.blend_allowed = false;
    state = step_down(no_blend, FrameKind::zoomed_out, true);
    OA_CHECK(state.method == ZoomOutMethod::area && state.budget == SceneBudget::reduced);
    state = step_down(state, FrameKind::zoomed_out, false);
    OA_CHECK(state.budget == SceneBudget::none && state.magnify && state.filtered_chrome);
    // With no zoomed-out rung left, they move the others.
    state = step_down(state, FrameKind::zoomed_out, false);
    OA_CHECK(!state.filtered_chrome && state.magnify);

    // Zoomed-in frames move magnify off, and nothing else first.
    state = step_down(top, FrameKind::zoomed_in, false);
    OA_CHECK(!state.magnify && state.budget == SceneBudget::full && state.filtered_chrome);
    state = step_down(state, FrameKind::zoomed_in, false);
    OA_CHECK(!state.filtered_chrome && state.budget == SceneBudget::full);

    // Other frames: NEAREST chrome, then the card's magnification while the
    // magnified world still uses it, then the standard tier.
    state = step_down(top, FrameKind::other, false);
    OA_CHECK(!state.filtered_chrome && state.budget == SceneBudget::full && state.magnify);
    state = step_down(state, FrameKind::other, false);
    OA_CHECK(state.card == CardFilter::prescale_quarter);
    state = step_down(state, FrameKind::other, false);
    OA_CHECK(state.card == CardFilter::linear);
    state = step_down(state, FrameKind::other, false);
    OA_CHECK(state.standard);
    OA_CHECK(step_down(state, FrameKind::zoomed_out, true).standard);

    // PIXELART is one rung, to plain LINEAR.
    LadderState pixelart = top;
    pixelart.card = CardFilter::pixelart;
    pixelart.filtered_chrome = false;
    OA_CHECK(step_down(pixelart, FrameKind::other, false).card == CardFilter::linear);

    // With magnify off and NEAREST chrome, the card's rungs change nothing
    // and are passed: the next step is the standard tier.
    LadderState bottom = top;
    bottom.magnify = false;
    bottom.filtered_chrome = false;
    OA_CHECK(step_down(bottom, FrameKind::other, false).standard);

    // The whole ladder in the maintainer's order takes 6 steps from the top,
    // the card's rungs passed once magnify is off and the chrome NEAREST.
    state = top;
    int steps = 0;
    const FrameKind order[] = {
        FrameKind::zoomed_out,
        FrameKind::zoomed_out,
        FrameKind::zoomed_out,
        FrameKind::zoomed_in,
        FrameKind::other,
        FrameKind::other
    };
    for (const FrameKind pool : order) {
        state = step_down(state, pool, true);
        ++steps;
    }
    OA_CHECK(state.method == ZoomOutMethod::blend && state.budget == SceneBudget::none);
    OA_CHECK(!state.magnify && !state.filtered_chrome && state.standard);
    OA_CHECK(steps == 6);
}

void test_resume_and_step_up() {
    const StartInputs desktop = measured_desktop();
    const LadderState ceiling = start_ceiling(desktop);
    OA_CHECK(ceiling.budget == SceneBudget::full && ceiling.magnify && ceiling.filtered_chrome);

    // Step up undoes the lowest rung taken first.
    LadderState state = start_rung(desktop);
    state.method = ZoomOutMethod::blend;
    state.budget = SceneBudget::none;
    state.magnify = false;
    state.filtered_chrome = false;
    state.card = CardFilter::linear;
    state = step_up(state, ceiling);
    OA_CHECK(state.filtered_chrome && state.card == CardFilter::linear);
    state = step_up(state, ceiling);
    OA_CHECK(state.card == CardFilter::prescale_quarter);
    state = step_up(state, ceiling);
    OA_CHECK(state.card == CardFilter::prescale_full);
    state = step_up(state, ceiling);
    OA_CHECK(state.magnify);
    state = step_up(state, ceiling);
    OA_CHECK(state.budget == SceneBudget::reduced);
    state = step_up(state, ceiling);
    OA_CHECK(state.budget == SceneBudget::full);
    state = step_up(state, ceiling);
    OA_CHECK(state.method == ZoomOutMethod::area);
    const LadderState again = step_up(state, ceiling);
    OA_CHECK(
        again.method == state.method && again.budget == state.budget && again.card == state.card
    );

    // A remembered rung starts the run there, one higher only with headroom.
    LadderState remembered = start_rung(desktop);
    remembered.budget = SceneBudget::reduced;
    OA_CHECK(resume_rung(desktop, remembered, 70).budget == SceneBudget::reduced);
    OA_CHECK(resume_rung(desktop, remembered, headroom_percent).budget == SceneBudget::reduced);
    OA_CHECK(resume_rung(desktop, remembered, headroom_percent - 1).budget == SceneBudget::full);
    remembered.standard = true;
    OA_CHECK(!resume_rung(desktop, remembered, 90).standard);

    // A class not yet measured starts at reduced but may rise to full.
    StartInputs tested = desktop;
    tested.run_class = ClassTesting::tested;
    OA_CHECK(start_rung(tested).budget == SceneBudget::reduced);
    OA_CHECK(resume_rung(tested, start_rung(tested), 50).budget == SceneBudget::full);

    // Never above the starting rung's limit: at 4 GiB the blend never comes
    // back, though the budget may rise.
    StartInputs four = tested;
    four.memory = 4 * gibibyte;
    LadderState generous = start_rung(desktop);
    generous.method = ZoomOutMethod::blend;
    generous.budget = SceneBudget::none;
    const LadderState resumed = resume_rung(four, generous, 10);
    OA_CHECK(!resumed.blend_allowed && resumed.method == ZoomOutMethod::area);
    OA_CHECK(resumed.magnify && resumed.filtered_chrome);
    OA_CHECK(resumed.card == CardFilter::prescale_full && resumed.budget == SceneBudget::reduced);

    // A start at the magnify-off rung, before Vista or at budget none, rises
    // no higher by the remembered rung, whatever its headroom.
    StartInputs before_vista;
    before_vista.processors = 2;
    before_vista.memory = 2 * gibibyte;
    before_vista.legacy_windows = true;
    before_vista.light_machine = true;
    before_vista.run_class = ClassTesting::measured;
    before_vista.driver.blend_excluded = true;
    before_vista.blend_available = true;
    StartInputs untested = desktop;
    untested.run_class = ClassTesting::untested;
    StartInputs two_processors = desktop;
    two_processors.processors = 2;
    for (const StartInputs& machine : {before_vista, untested, two_processors}) {
        const LadderState limit = start_ceiling(machine);
        OA_CHECK(limit.budget == SceneBudget::none && !limit.magnify);
        for (uint32_t median = 0; median < 100; median += 10) {
            const LadderState start = resume_rung(machine, generous, median);
            OA_CHECK(start.budget == SceneBudget::none && !start.magnify);
            OA_CHECK(start.filtered_chrome && !start.standard);
        }
    }
    OA_CHECK(!resume_rung(before_vista, generous, 0).blend_allowed);
    OA_CHECK(resume_rung(before_vista, generous, 0).card == CardFilter::prescale_quarter);
}

/// Builds steady frames for the step-down.
struct FrameClock {
    uint64_t now_ns{};
    uint32_t paced{60};

    /// Returns the next frame, interval_ns after the last.
    ///
    /// @param interval_ns the frame interval
    /// @param kind the frame's kind
    /// @return the frame's sample
    FrameSample frame(uint64_t interval_ns, FrameKind kind) {
        now_ns += interval_ns;
        FrameSample sample;
        sample.now_ns = now_ns;
        sample.interval_ns = interval_ns;
        sample.draw_ns = interval_ns / 2;
        sample.paced_frames_per_second = paced;
        sample.kind = kind;
        sample.window_active = true;
        return sample;
    }
};

constexpr uint64_t millisecond = 1'000'000;

/// Feeds frames until the step-down steps or a time runs out.
///
/// @param ladder the step-down
/// @param clock the frames' clock
/// @param interval_ns each frame's interval
/// @param kind the frames' kind
/// @param limit_ns how long to feed
/// @return the time from the first frame to the step, or 0 when none came
uint64_t time_to_step(
    ScaleStepDown& ladder,
    FrameClock& clock,
    uint64_t interval_ns,
    FrameKind kind,
    uint64_t limit_ns
) {
    const uint64_t start = clock.now_ns;
    while (clock.now_ns - start < limit_ns) {
        if (feed_step_down(ladder, clock.frame(interval_ns, kind)) != StepResult::none)
            return clock.now_ns - start;
    }
    return 0;
}

void test_step_down_rules() {
    const LadderState top = start_rung(measured_desktop());
    constexpr uint64_t seconds_20 = 20'000'000'000;

    // On time at 60: never.
    ScaleStepDown ladder = start_step_down(top);
    FrameClock clock;
    OA_CHECK(time_to_step(ladder, clock, 16'666'667, FrameKind::zoomed_out, seconds_20) == 0);

    // Over 1.25 times the period for 3 s: one step after 3 s, the next not
    // before 10 s later.
    ladder = start_step_down(top);
    clock = FrameClock{};
    uint64_t elapsed =
        time_to_step(ladder, clock, 22 * millisecond, FrameKind::zoomed_out, seconds_20);
    OA_CHECK(elapsed >= slow_window_ns && elapsed < slow_window_ns + 30 * millisecond);
    OA_CHECK(ladder.state.budget == SceneBudget::reduced);
    elapsed = time_to_step(ladder, clock, 22 * millisecond, FrameKind::zoomed_out, seconds_20);
    OA_CHECK(elapsed >= step_spacing_ns && elapsed < step_spacing_ns + 30 * millisecond);
    OA_CHECK(ladder.state.budget == SceneBudget::none);

    // The limits themselves: just under 1.25 times the 16.67 ms period
    // never steps, just over steps after 3 s; just under twice it waits for
    // 3 s, just over steps after 1 s.
    ladder = start_step_down(top);
    clock = FrameClock{};
    OA_CHECK(time_to_step(ladder, clock, 20'700'000, FrameKind::zoomed_out, seconds_20) == 0);
    ladder = start_step_down(top);
    clock = FrameClock{};
    elapsed = time_to_step(ladder, clock, 21'000'000, FrameKind::zoomed_out, seconds_20);
    OA_CHECK(elapsed >= slow_window_ns && elapsed < slow_window_ns + 30 * millisecond);
    ladder = start_step_down(top);
    clock = FrameClock{};
    elapsed = time_to_step(ladder, clock, 33'000'000, FrameKind::zoomed_out, seconds_20);
    OA_CHECK(elapsed >= slow_window_ns && elapsed < slow_window_ns + 40 * millisecond);
    ladder = start_step_down(top);
    clock = FrameClock{};
    elapsed = time_to_step(ladder, clock, 33'500'000, FrameKind::zoomed_out, seconds_20);
    OA_CHECK(elapsed >= very_slow_window_ns && elapsed < very_slow_window_ns + 40 * millisecond);

    // Over twice the period for 1 s: at once, whatever the spacing.
    ladder = start_step_down(top);
    clock = FrameClock{};
    elapsed = time_to_step(ladder, clock, 40 * millisecond, FrameKind::zoomed_out, seconds_20);
    OA_CHECK(elapsed >= very_slow_window_ns && elapsed < very_slow_window_ns + 50 * millisecond);
    elapsed = time_to_step(ladder, clock, 40 * millisecond, FrameKind::zoomed_out, seconds_20);
    OA_CHECK(elapsed >= very_slow_window_ns && elapsed < very_slow_window_ns + 50 * millisecond);
    OA_CHECK(ladder.state.budget == SceneBudget::none);

    // Frames that are not steady never count.
    for (int variant = 0; variant < 4; ++variant) {
        ladder = start_step_down(top);
        clock = FrameClock{};
        bool stepped = false;
        while (clock.now_ns < seconds_20) {
            FrameSample sample = clock.frame(50 * millisecond, FrameKind::zoomed_out);
            sample.idle = variant == 0;
            sample.window_active = variant != 1;
            sample.settling = variant == 2;
            sample.match_warming = variant == 3;
            OA_CHECK(!steady_frame(sample));
            if (feed_step_down(ladder, sample) != StepResult::none)
                stepped = true;
        }
        OA_CHECK(!stepped);
    }

    // At 30 frames a second, frames due at each clock unit's middle: 33 or
    // 34 ms apart, a short one after a late one, never slow.
    ladder = start_step_down(top);
    clock = FrameClock{};
    clock.paced = 30;
    bool stepped = false;
    for (int frame = 0; frame < 900; ++frame) {
        uint64_t interval = (frame % 3 == 0 ? 34 : 33) * millisecond;
        if (frame % 50 == 10)
            interval = 45 * millisecond;
        if (frame % 50 == 11)
            interval = 22 * millisecond;
        if (feed_step_down(ladder, clock.frame(interval, FrameKind::zoomed_out)) !=
            StepResult::none)
            stepped = true;
    }
    OA_CHECK(!stepped);

    // A frame slowed by its ticks never steps under the absolute rule.
    ladder = start_step_down(top);
    clock = FrameClock{};
    stepped = false;
    while (clock.now_ns < seconds_20) {
        FrameSample sample = clock.frame(30 * millisecond, FrameKind::zoomed_out);
        sample.tick_ns = 14 * millisecond;
        if (feed_step_down(ladder, sample) != StepResult::none)
            stepped = true;
    }
    OA_CHECK(!stepped);
    ladder = start_step_down(top);
    clock = FrameClock{};
    OA_CHECK(time_to_step(ladder, clock, 30 * millisecond, FrameKind::zoomed_out, seconds_20) != 0);

    // A loop paced at 120 that misses it but keeps 60 is never slow.
    ladder = start_step_down(top);
    clock = FrameClock{};
    clock.paced = 120;
    OA_CHECK(time_to_step(ladder, clock, 16 * millisecond, FrameKind::zoomed_in, seconds_20) == 0);
}

void test_step_down_pools() {
    const LadderState top = start_rung(measured_desktop());
    constexpr uint64_t seconds_60 = 60'000'000'000;

    // Short zoom-outs pool together: six slow half-second spells between
    // fast frames at zoom 1 step the budget once the pool holds 3 s.
    ScaleStepDown ladder = start_step_down(top);
    FrameClock clock;
    int spells = 0;
    StepResult result = StepResult::none;
    while (result == StepResult::none && spells < 20) {
        for (int frame = 0; frame < 23 && result == StepResult::none; ++frame)
            result = feed_step_down(ladder, clock.frame(22 * millisecond, FrameKind::zoomed_out));
        ++spells;
        for (int frame = 0; frame < 60; ++frame)
            OA_CHECK(
                feed_step_down(ladder, clock.frame(16 * millisecond, FrameKind::other)) ==
                StepResult::none
            );
    }
    OA_CHECK(result == StepResult::stepped);
    OA_CHECK(spells == 6);
    OA_CHECK(ladder.state.budget == SceneBudget::reduced && ladder.state.filtered_chrome);
    OA_CHECK(ladder.zoomed_out.count == 0);

    // Frames at zoom 1 and above never move the zoomed-out rungs.
    ladder = start_step_down(top);
    clock = FrameClock{};
    OA_CHECK(time_to_step(ladder, clock, 22 * millisecond, FrameKind::other, seconds_60) != 0);
    OA_CHECK(ladder.state.budget == SceneBudget::full && !ladder.state.filtered_chrome);
    ladder = start_step_down(top);
    clock = FrameClock{};
    OA_CHECK(time_to_step(ladder, clock, 22 * millisecond, FrameKind::zoomed_in, seconds_60) != 0);
    OA_CHECK(
        ladder.state.budget == SceneBudget::full && !ladder.state.magnify &&
        ladder.state.filtered_chrome
    );

    // Down to the standard tier, step by step, never back up.
    ladder = start_step_down(top);
    clock = FrameClock{};
    LadderState before = ladder.state;
    int steps = 0;
    const FrameKind kinds[] = {FrameKind::zoomed_out, FrameKind::zoomed_in, FrameKind::other};
    for (const FrameKind kind : kinds) {
        for (int attempt = 0; attempt < 8 && !ladder.state.standard; ++attempt) {
            const FrameKind fed =
                kind == FrameKind::zoomed_out && ladder.state.budget == SceneBudget::none
                    ? FrameKind::other
                : kind == FrameKind::zoomed_in && !ladder.state.magnify ? FrameKind::other
                                                                        : kind;
            if (time_to_step(ladder, clock, 40 * millisecond, fed, seconds_60) == 0)
                break;
            ++steps;
            OA_CHECK(ladder.state.budget <= before.budget);
            OA_CHECK(ladder.state.card <= before.card);
            OA_CHECK(!ladder.state.magnify || before.magnify);
            OA_CHECK(!ladder.state.filtered_chrome || before.filtered_chrome);
            before = ladder.state;
        }
    }
    OA_CHECK(ladder.state.standard);
    OA_CHECK(steps >= 4);
    OA_CHECK(
        feed_step_down(ladder, clock.frame(100 * millisecond, FrameKind::other)) == StepResult::none
    );
}

void test_step_down_costs() {
    const LadderState top = start_rung(measured_desktop());
    constexpr uint64_t seconds_20 = 20'000'000'000;

    // Late frames whose own passes take over a tenth of the period step the
    // rung that owns them, within the 25% band.
    for (const FrameKind kind : {FrameKind::zoomed_in, FrameKind::zoomed_out, FrameKind::other}) {
        for (const uint64_t passes : {uint64_t{2} * millisecond, uint64_t{1} * millisecond}) {
            ScaleStepDown ladder = start_step_down(top);
            FrameClock clock;
            uint64_t stepped_at = 0;
            while (clock.now_ns < seconds_20 && stepped_at == 0) {
                FrameSample sample = clock.frame(18 * millisecond, kind);
                sample.passes_ns = passes;
                sample.area_ns = kind == FrameKind::zoomed_out ? passes : 0;
                if (feed_step_down(ladder, sample) != StepResult::none)
                    stepped_at = clock.now_ns;
            }
            const bool expected = kind != FrameKind::other && passes == 2 * millisecond;
            OA_CHECK((stepped_at != 0) == expected);
        }
    }

    // While the clock runs behind, any time in the passes sheds the budget
    // and magnification at once; with no passes nothing is shed.
    ScaleStepDown ladder = start_step_down(top);
    FrameClock clock;
    FrameSample behind = clock.frame(16 * millisecond, FrameKind::other);
    behind.clock_behind = true;
    OA_CHECK(feed_step_down(ladder, behind) == StepResult::none);
    behind = clock.frame(16 * millisecond, FrameKind::zoomed_in);
    behind.clock_behind = true;
    behind.passes_ns = 100'000;
    OA_CHECK(feed_step_down(ladder, behind) == StepResult::shed);
    OA_CHECK(ladder.state.budget == SceneBudget::none && !ladder.state.magnify);
    OA_CHECK(ladder.state.filtered_chrome && !ladder.state.standard);
    behind = clock.frame(16 * millisecond, FrameKind::other);
    behind.clock_behind = true;
    behind.passes_ns = 100'000;
    OA_CHECK(feed_step_down(ladder, behind) == StepResult::none);

    // The blend is chosen where the area pass takes at least a third of the
    // draw, exactly a third included, and uploads are cheap.
    for (const uint64_t area_ns : {uint64_t{4} * millisecond, uint64_t{3'999'000}}) {
        ScaleStepDown third_ladder = start_step_down(top);
        FrameClock third_clock;
        StepResult result = StepResult::none;
        while (result == StepResult::none && third_clock.now_ns < seconds_20) {
            FrameSample sample = third_clock.frame(22 * millisecond, FrameKind::zoomed_out);
            sample.draw_ns = 12 * millisecond;
            sample.area_ns = area_ns;
            sample.present_ns = 1 * millisecond;
            result = feed_step_down(third_ladder, sample);
        }
        OA_CHECK(result == StepResult::stepped);
        const bool blend = area_ns == 4 * millisecond;
        OA_CHECK((third_ladder.state.method == ZoomOutMethod::blend) == blend);
        OA_CHECK(third_ladder.state.budget == (blend ? SceneBudget::full : SceneBudget::reduced));
    }

    // The blend is chosen first where it is allowed, the area pass takes a
    // third of the draw and uploads are cheap; otherwise the budget steps.
    for (const uint64_t present : {uint64_t{1} * millisecond, uint64_t{8} * millisecond}) {
        ScaleStepDown blend_ladder = start_step_down(top);
        FrameClock blend_clock;
        StepResult result = StepResult::none;
        while (result == StepResult::none && blend_clock.now_ns < seconds_20) {
            FrameSample sample = blend_clock.frame(22 * millisecond, FrameKind::zoomed_out);
            sample.draw_ns = 12 * millisecond;
            sample.area_ns = 6 * millisecond;
            sample.present_ns = present;
            result = feed_step_down(blend_ladder, sample);
        }
        OA_CHECK(result == StepResult::stepped);
        if (present == 1 * millisecond)
            OA_CHECK(
                blend_ladder.state.method == ZoomOutMethod::blend &&
                blend_ladder.state.budget == SceneBudget::full
            );
        else
            OA_CHECK(
                blend_ladder.state.method == ZoomOutMethod::area &&
                blend_ladder.state.budget == SceneBudget::reduced
            );
    }
}

// ---------------------------------------------------------------------------
// Chrome filtering and the prescale budget

void test_prescale() {
    OA_CHECK(prescale_budget(CardFilter::prescale_full) == prescale_budget_pixels);
    OA_CHECK(prescale_budget(CardFilter::prescale_quarter) == prescale_budget_pixels / 4);
    OA_CHECK(prescale_budget(CardFilter::linear) == 0);
    OA_CHECK(prescale_budget(CardFilter::pixelart) == 0);

    PrescaleBudget budget{prescale_budget_pixels, 0};
    OA_CHECK(prescale_factor(budget, 640, 480, 1.5) == 2);
    OA_CHECK(prescale_factor(budget, 640, 480, 1.25) == 2);
    OA_CHECK(prescale_factor(budget, 640, 480, 2.0) == 2);
    OA_CHECK(prescale_factor(budget, 640, 480, 3.7) == 4);
    // n falls to the largest whole number that fits.
    OA_CHECK(prescale_factor(budget, 1920, 1080, 3.7) == 2);
    OA_CHECK(prescale_factor(budget, 3840, 2160, 1.5) == 1);
    OA_CHECK(prescale_factor(budget, 640, 480, 0.0) == 1);
    OA_CHECK(prescale_factor(budget, 0, 480, 1.5) == 1);
    OA_CHECK(prescale_factor(PrescaleBudget{}, 640, 480, 1.5) == 1);
    // A target that fills what is left exactly still fits.
    OA_CHECK(prescale_factor(PrescaleBudget{uint64_t{1280} * 960, 0}, 640, 480, 1.5) == 2);
    OA_CHECK(prescale_factor(PrescaleBudget{uint64_t{1280} * 960 - 1, 0}, 640, 480, 1.5) == 1);

    // The sum of all targets alive at once is charged; the front end's,
    // freed during a match, gives its room back.
    PrescaleBudget quarter{prescale_budget(CardFilter::prescale_quarter), 0};
    const uint64_t front_end = uint64_t{1280} * 960;
    OA_CHECK(prescale_factor(quarter, 640, 480, 1.5) == 2);
    OA_CHECK(charge_prescale(quarter, front_end));
    OA_CHECK(prescale_factor(quarter, 640, 480, 1.5) == 1);
    OA_CHECK(!charge_prescale(quarter, front_end));
    OA_CHECK(quarter.charged == front_end);
    release_prescale(quarter, front_end);
    OA_CHECK(quarter.charged == 0);
    OA_CHECK(prescale_factor(quarter, 640, 480, 1.5) == 2);
    release_prescale(quarter, front_end);
    OA_CHECK(quarter.charged == 0);
    OA_CHECK(charge_prescale(quarter, quarter.limit));
    OA_CHECK(!charge_prescale(quarter, 1));
}

void test_chrome_filter() {
    LadderState state;
    state.filtered_chrome = true;
    state.card = CardFilter::pixelart;
    OA_CHECK(chrome_filter(state, 1.5) == ScaleFilter::pixelart);
    OA_CHECK(chrome_filter(state, 2.0) == ScaleFilter::nearest);
    OA_CHECK(chrome_filter(state, 1.0) == ScaleFilter::nearest);
    state.card = CardFilter::prescale_full;
    OA_CHECK(chrome_filter(state, 1.25) == ScaleFilter::sharp_bilinear);
    state.card = CardFilter::prescale_quarter;
    OA_CHECK(chrome_filter(state, 1.25) == ScaleFilter::sharp_bilinear);
    state.card = CardFilter::linear;
    OA_CHECK(chrome_filter(state, 1.25) == ScaleFilter::linear);
    // The NEAREST-chrome rung and the standard tier draw as today.
    state.card = CardFilter::pixelart;
    state.filtered_chrome = false;
    OA_CHECK(chrome_filter(state, 1.25) == ScaleFilter::nearest);
    state.filtered_chrome = true;
    state.standard = true;
    OA_CHECK(chrome_filter(state, 1.25) == ScaleFilter::nearest);
    // The period floor, with 256 MiB, draws every frame in the standard
    // tier, so its chrome at 800x600's scale of 1.25 is NEAREST, as v0.6.1's.
    TierInputs floor = accelerated_run();
    floor.memory = 256 * mebibyte;
    OA_CHECK(decide_render_tier(floor).tier == RenderTier::standard);
    OA_CHECK(!function_test_may_run(floor));
}

// ---------------------------------------------------------------------------
// Tiled textures

void test_tiles_by_table() {
    // Within the limit, or with none, one tile.
    TileGrid grid = plan_tiles(8000, 6000, unlimited_texture_size);
    OA_CHECK(grid.columns == 1 && grid.rows == 1 && grid.tile_size == 0);
    Tile tile = tile_at(grid, 0, 0);
    OA_CHECK(tile.texture.width == 8000 && tile.texture.height == 6000);
    OA_CHECK(tile.content.width == 8000 && tile.source.x == 0 && tile.source.y == 0);
    grid = plan_tiles(1920, 1080, 16384);
    OA_CHECK(grid.columns == 1 && grid.rows == 1);
    grid = plan_tiles(2048, 2048, 2048);
    OA_CHECK(grid.columns == 1 && grid.rows == 1);

    // A 2560x1440 window's 2304-pixel-wide battlefield on a 2048 limit.
    grid = plan_tiles(2304, 1312, 2048);
    OA_CHECK(grid.columns == 2 && grid.rows == 1 && grid.tile_size == 2048);
    tile = tile_at(grid, 0, 0);
    OA_CHECK(tile.texture.x == 0 && tile.texture.width == 2048);
    OA_CHECK(tile.content.x == 0 && tile.content.width == 2047);
    OA_CHECK(tile.source.x == 0 && tile.source.width == 2047);
    OA_CHECK(tile.texture.height == 1312 && tile.content.height == 1312 && tile.source.y == 0);
    tile = tile_at(grid, 1, 0);
    OA_CHECK(tile.content.x == 2047 && tile.content.width == 257);
    OA_CHECK(tile.texture.x == 2046 && tile.texture.width == 258);
    OA_CHECK(tile.source.x == 1 && tile.source.width == 257);

    // A larger limit still tiles at 2048.
    grid = plan_tiles(5000, 3000, 4096);
    OA_CHECK(grid.tile_size == largest_tile_size && grid.columns == 3 && grid.rows == 2);

    // Nothing to tile, or a limit too small.
    OA_CHECK(plan_tiles(0, 100, 2048).columns == 0);
    OA_CHECK(plan_tiles(100, 0, 0).rows == 0);
    OA_CHECK(plan_tiles(100, 100, 2).columns == 0);
    OA_CHECK(tile_at(plan_tiles(100, 100, 2), 0, 0).texture.width == 0);
    OA_CHECK(tile_at(plan_tiles(2304, 1312, 2048), 2, 0).texture.width == 0);
}

void test_tiles_cover_every_texel() {
    const uint32_t limits[] = {3, 4, 64, 100, 1024, 2048, 4096};
    const uint32_t lengths[] = {
        1, 2, 3, 4, 63, 64, 65, 127, 128, 1000, 2046, 2047, 2048, 2049, 2304, 4093, 4094, 5000
    };
    uint32_t grids = 0;
    for (const uint32_t limit : limits) {
        for (const uint32_t width : lengths) {
            for (const uint32_t height : {uint32_t{1}, uint32_t{65}, uint32_t{2049}}) {
                const TileGrid grid = plan_tiles(width, height, limit);
                ++grids;
                OA_CHECK(grid.columns >= 1 && grid.rows >= 1);
                const bool tiled = width > limit || height > limit;
                OA_CHECK((grid.tile_size != 0) == tiled);
                if (tiled)
                    OA_CHECK(
                        grid.tile_size == (limit < largest_tile_size ? limit : largest_tile_size)
                    );
                // Across: the contents follow one another from 0 to the width,
                // each texture within the tile size and the limit, with a
                // one-texel gutter toward each neighbour and none at the edges.
                uint32_t next = 0;
                for (uint32_t column = 0; column < grid.columns; ++column) {
                    const Tile tile = tile_at(grid, column, 0);
                    OA_CHECK(tile.content.x == next);
                    OA_CHECK(tile.content.width > 0);
                    next = tile.content.x + tile.content.width;
                    const uint32_t before = column == 0 ? 0 : tile_gutter;
                    const uint32_t after = column + 1 == grid.columns ? 0 : tile_gutter;
                    OA_CHECK(tile.texture.x + before == tile.content.x);
                    OA_CHECK(tile.texture.width == tile.content.width + before + after);
                    OA_CHECK(tile.source.x == before && tile.source.width == tile.content.width);
                    if (tiled)
                        OA_CHECK(tile.texture.width <= grid.tile_size);
                }
                OA_CHECK(next == width);
                next = 0;
                for (uint32_t row = 0; row < grid.rows; ++row) {
                    const Tile tile = tile_at(grid, grid.columns - 1, row);
                    OA_CHECK(tile.content.y == next);
                    next = tile.content.y + tile.content.height;
                    const uint32_t before = row == 0 ? 0 : tile_gutter;
                    const uint32_t after = row + 1 == grid.rows ? 0 : tile_gutter;
                    OA_CHECK(tile.texture.y + before == tile.content.y);
                    OA_CHECK(tile.texture.height == tile.content.height + before + after);
                    OA_CHECK(tile.source.y == before);
                    if (tiled)
                        OA_CHECK(tile.texture.height <= grid.tile_size);
                }
                OA_CHECK(next == height);
            }
        }
    }
    OA_CHECK(grids == 7 * 18 * 3);
}

// ---------------------------------------------------------------------------
// Device resets and the layers' formats

void test_device_resets() {
    constexpr uint64_t second = 1000;
    // The third reset within 60 s rebuilds, and the count then starts again.
    ResetWatch watch;
    OA_CHECK(!note_device_reset(watch, 0));
    OA_CHECK(!note_device_reset(watch, 20 * second));
    OA_CHECK(note_device_reset(watch, 59 * second));
    OA_CHECK(!note_device_reset(watch, 60 * second));
    OA_CHECK(!note_device_reset(watch, 61 * second));
    OA_CHECK(note_device_reset(watch, 62 * second));
    // Three spread over 61 s do not; the first falls out of the window.
    ResetWatch spread;
    OA_CHECK(!note_device_reset(spread, 0));
    OA_CHECK(!note_device_reset(spread, 30 * second));
    OA_CHECK(!note_device_reset(spread, 61 * second));
    OA_CHECK(note_device_reset(spread, 62 * second));
    // A reset exactly 60 s after the first is outside its window.
    ResetWatch edge;
    OA_CHECK(!note_device_reset(edge, 0));
    OA_CHECK(!note_device_reset(edge, 1));
    OA_CHECK(!note_device_reset(edge, reset_window_ms));
    OA_CHECK(!note_device_reset(edge, reset_window_ms + 1));
    OA_CHECK(note_device_reset(edge, reset_window_ms + 2));
}

void test_layer_formats() {
    struct Case {
        bool software;
        bool rgb565_window;
        bool named;
        LayerFormat opaque;
        LayerFormat loading;
        LayerFormat front_end;
    };

    const Case cases[] = {
        // SDL's software renderer keeps today's formats, named or not.
        {true, false, false, LayerFormat::xrgb8888, LayerFormat::xrgb8888, LayerFormat::rgb24},
        {true, true, false, LayerFormat::rgb565, LayerFormat::xrgb8888, LayerFormat::rgb24},
        {true, false, true, LayerFormat::xrgb8888, LayerFormat::xrgb8888, LayerFormat::rgb24},
        {true, true, true, LayerFormat::rgb565, LayerFormat::xrgb8888, LayerFormat::rgb24},
        // Any driver SDL_RENDER_DRIVER names keeps them too, RGB565 aside.
        {false, false, true, LayerFormat::xrgb8888, LayerFormat::xrgb8888, LayerFormat::rgb24},
        {false, true, true, LayerFormat::xrgb8888, LayerFormat::xrgb8888, LayerFormat::rgb24},
        // A hardware driver of the walk gets ARGB8888 for every opaque layer.
        {false, false, false, LayerFormat::argb8888, LayerFormat::argb8888, LayerFormat::argb8888},
        {false, true, false, LayerFormat::argb8888, LayerFormat::argb8888, LayerFormat::argb8888},
    };
    for (const auto& c : cases) {
        const LayerFormats formats = layer_formats(c.software, c.rgb565_window, c.named);
        OA_CHECK(formats.opaque == c.opaque);
        OA_CHECK(formats.loading == c.loading);
        OA_CHECK(formats.front_end == c.front_end);
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Acting on the tier

/// The video drivers that draw no window, in any letter case, and the
/// command line's flag.
void test_windowless_and_flag() {
    for (const std::string_view driver : {"dummy", "offscreen", "DUMMY", "Offscreen"})
        OA_CHECK(windowless_video_driver(driver));
    for (const std::string_view driver :
         {"cocoa", "windows", "x11", "wayland", "", "dumm", "dummy "})
        OA_CHECK(!windowless_video_driver(driver));
    OA_CHECK(acceleration_flag(std::nullopt) == AccelerationFlag::none);
    OA_CHECK(acceleration_flag(true) == AccelerationFlag::on);
    OA_CHECK(acceleration_flag(false) == AccelerationFlag::off);
}

/// What the host does for each decision, with the presentation on and off.
void test_tier_action() {
    const TierDecision accelerated{RenderTier::accelerated, TierReason::accelerated};
    const TierDecision due{RenderTier::standard, TierReason::function_test_due};
    OA_CHECK(tier_action(accelerated, false) == TierAction::switch_on);
    OA_CHECK(tier_action(accelerated, true) == TierAction::none);
    OA_CHECK(tier_action(due, false) == TierAction::run_function_test);
    for (const TierReason reason :
         {TierReason::no_renderer,
          TierReason::director_frame,
          TierReason::memory,
          TierReason::flag_off,
          TierReason::environment,
          TierReason::setting_off,
          TierReason::not_capable,
          TierReason::function_test_failed,
          TierReason::records_unreadable,
          TierReason::accelerated_unusable,
          TierReason::dropped,
          TierReason::device_lost,
          TierReason::waiting_for_match_end,
          TierReason::trial_unwritten}) {
        const TierDecision standard{RenderTier::standard, reason};
        OA_CHECK(tier_action(standard, true) == TierAction::switch_off);
        OA_CHECK(tier_action(standard, false) == TierAction::none);
    }
}

/// Off then On, or Restore defaults, lets a failed or unwritten function
/// test run again and lifts every drop but the memory guard's; a passed
/// test stays passed.
void test_forget_failures() {
    TierInputs inputs;
    inputs.function_test = FunctionTest::failed;
    inputs.drop = Drop::driver_failure;
    forget_failures(inputs);
    OA_CHECK(inputs.function_test == FunctionTest::not_run);
    OA_CHECK(inputs.drop == Drop::none);
    inputs.function_test = FunctionTest::trial_unwritten;
    inputs.drop = Drop::memory;
    forget_failures(inputs);
    OA_CHECK(inputs.function_test == FunctionTest::not_run);
    OA_CHECK(inputs.drop == Drop::memory);
    inputs.function_test = FunctionTest::passed;
    for (const Drop drop :
         {Drop::engine_fault, Drop::stall, Drop::slow_frames, Drop::path_trial_unwritten}) {
        inputs.drop = drop;
        forget_failures(inputs);
        OA_CHECK(inputs.function_test == FunctionTest::passed);
        OA_CHECK(inputs.drop == Drop::none);
    }
}

/// A stand-in for the start-up function test: what it finds, and how often
/// it ran.
struct StandInTest {
    bool passes{true}; ///< it finds the renderer draws right
    int runs{};        ///< the times it ran
};

/// Runs the stand-in function test (FunctionTestHooks::run).
///
/// @param context the StandInTest
/// @return FunctionTest::passed or FunctionTest::failed, as it finds
FunctionTest run_stand_in_test(void* context) {
    auto& test = *static_cast<StandInTest*>(context);
    ++test.runs;
    return test.passes ? FunctionTest::passed : FunctionTest::failed;
}

/// Steps one frame as the game does (step_tier), and switches the
/// presentation as the step asks.
///
/// @param[in,out] inputs the run's facts
/// @param[in,out] presentation_on the accelerated presentation is on
/// @param[in,out] test the stand-in function test
/// @return the frame's decision
TierDecision drawn_frame(TierInputs& inputs, bool& presentation_on, StandInTest& test) {
    const TierStep step = step_tier(inputs, presentation_on, {&test, run_stand_in_test});
    OA_CHECK(step.action != TierAction::run_function_test);
    if (step.action == TierAction::switch_on)
        presentation_on = true;
    else if (step.action == TierAction::switch_off)
        presentation_on = false;
    return step.decision;
}

/// One frame's step: the function test runs only when only it is missing
/// and its result is kept; with nothing to run it the frame stays standard
/// and a presentation left on is switched off; and in a shared game each
/// frame's decision is noted, a lost device apart, while outside a match
/// the note changes nothing.
void test_step_tier() {
    TierInputs inputs;
    inputs.renderer = true;
    inputs.memory = 8 * gibibyte;
    inputs.setting_on = true;
    StandInTest test;
    const FunctionTestHooks hooks{&test, run_stand_in_test};
    TierStep step = step_tier(inputs, true, {});
    OA_CHECK(step.decision.reason == TierReason::function_test_due);
    OA_CHECK(step.action == TierAction::switch_off);
    OA_CHECK(step_tier(inputs, false, {}).action == TierAction::none);
    OA_CHECK(inputs.function_test == FunctionTest::not_run);
    step = step_tier(inputs, false, hooks);
    OA_CHECK(step.decision.tier == RenderTier::accelerated && step.action == TierAction::switch_on);
    OA_CHECK(test.runs == 1 && inputs.function_test == FunctionTest::passed);
    step = step_tier(inputs, true, hooks);
    OA_CHECK(step.action == TierAction::none && test.runs == 1);
    // A failure is kept, and the test does not run again.
    TierInputs failing = inputs;
    failing.function_test = FunctionTest::not_run;
    StandInTest fails{false, 0};
    const FunctionTestHooks failing_hooks{&fails, run_stand_in_test};
    step = step_tier(failing, false, failing_hooks);
    OA_CHECK(step.decision.reason == TierReason::function_test_failed);
    OA_CHECK(step.action == TierAction::none);
    step = step_tier(failing, false, failing_hooks);
    OA_CHECK(fails.runs == 1 && failing.function_test == FunctionTest::failed);
    // A shared game that began accelerated: a lost device switches the
    // presentation off but keeps the gate; Off closes it until the end.
    begin_match(inputs.match, MatchKind::shared_game, true);
    inputs.device_lost = true;
    step = step_tier(inputs, true, hooks);
    OA_CHECK(step.decision.reason == TierReason::device_lost);
    OA_CHECK(step.action == TierAction::switch_off && inputs.match.accelerated);
    inputs.device_lost = false;
    OA_CHECK(step_tier(inputs, false, hooks).action == TierAction::switch_on);
    inputs.setting_on = false;
    OA_CHECK(step_tier(inputs, true, hooks).action == TierAction::switch_off);
    OA_CHECK(!inputs.match.accelerated);
    inputs.setting_on = true;
    step = step_tier(inputs, false, hooks);
    OA_CHECK(step.decision.reason == TierReason::waiting_for_match_end);
    OA_CHECK(step.action == TierAction::none);
    end_match(inputs.match);
    OA_CHECK(step_tier(inputs, false, hooks).action == TierAction::switch_on);
    inputs.setting_on = false;
    OA_CHECK(step_tier(inputs, true, hooks).action == TierAction::switch_off);
    OA_CHECK(inputs.match.kind == MatchKind::none && !inputs.match.accelerated);
    OA_CHECK(test.runs == 1);
}

/// The function test's state at start, while no trial is written before
/// it: on the player's own profile it waits for the player unless
/// --hardware-acceleration or --force-capable asks for it; with a named
/// preferences file or SDL_RENDER_DRIVER, whose records live in memory, it
/// is free to run.
void test_start_function_test() {
    TierInputs own;
    own.players_own_profile = true;
    OA_CHECK(start_function_test(own) == FunctionTest::trial_unwritten);
    own.setting_on = true;
    OA_CHECK(start_function_test(own) == FunctionTest::trial_unwritten);
    TierInputs refused = own;
    refused.flag = AccelerationFlag::off;
    OA_CHECK(start_function_test(refused) == FunctionTest::trial_unwritten);
    TierInputs flagged = own;
    flagged.flag = AccelerationFlag::on;
    OA_CHECK(start_function_test(flagged) == FunctionTest::not_run);
    TierInputs forced = own;
    forced.force_capable = true;
    OA_CHECK(start_function_test(forced) == FunctionTest::not_run);
    TierInputs named = own;
    named.players_own_profile = false;
    OA_CHECK(start_function_test(named) == FunctionTest::not_run);
    TierInputs environment = own;
    environment.render_driver_named = true;
    OA_CHECK(start_function_test(environment) == FunctionTest::not_run);
}

/// A player's run as the game drives it (step_tier): on the player's own
/// profile the start keeps the standard tier with no function test until
/// the player switches the setting Off then On, unless
/// --hardware-acceleration asks for it; a named preferences file that turns
/// the setting On tests at start; then the setting switched Off and On, a
/// shared game and a replay, a failed function test retried, and the runs
/// that stay on the standard tier whatever happens: a named preferences
/// file at its default, the dummy video driver, under 2 GiB, headless and
/// the director.
void test_host_runs() {
    constexpr uint64_t eight_gibibytes = 8 * gibibyte;
    TierInputs player;
    player.renderer = true;
    player.memory = eight_gibibytes;
    player.players_own_profile = true;
    player.setting_on = true;
    player.capability = Capability::capable;
    TierInputs named_on = player;
    named_on.players_own_profile = false;

    // The player's own profile: no test at start; Off then On runs it once,
    // and the frame is accelerated from then.
    {
        TierInputs inputs = player;
        inputs.function_test = start_function_test(inputs);
        bool on = false;
        StandInTest test;
        OA_CHECK(drawn_frame(inputs, on, test).reason == TierReason::trial_unwritten);
        OA_CHECK(!on && test.runs == 0);
        inputs.setting_on = false;
        OA_CHECK(drawn_frame(inputs, on, test).reason == TierReason::setting_off);
        forget_failures(inputs);
        inputs.setting_on = true;
        OA_CHECK(drawn_frame(inputs, on, test).tier == RenderTier::accelerated);
        OA_CHECK(on && test.runs == 1);
        // Off applies at once, On again at once, with no second test.
        inputs.setting_on = false;
        OA_CHECK(drawn_frame(inputs, on, test).reason == TierReason::setting_off);
        OA_CHECK(!on);
        inputs.setting_on = true;
        OA_CHECK(drawn_frame(inputs, on, test).tier == RenderTier::accelerated);
        OA_CHECK(on && test.runs == 1);
        // A shared game that begins accelerated stays so; Off in it applies
        // at once, and On waits for its end.
        begin_match(inputs.match, MatchKind::shared_game, on);
        OA_CHECK(drawn_frame(inputs, on, test).tier == RenderTier::accelerated);
        inputs.setting_on = false;
        OA_CHECK(drawn_frame(inputs, on, test).reason == TierReason::setting_off);
        OA_CHECK(!on);
        inputs.setting_on = true;
        OA_CHECK(drawn_frame(inputs, on, test).reason == TierReason::waiting_for_match_end);
        OA_CHECK(!on);
        end_match(inputs.match);
        OA_CHECK(drawn_frame(inputs, on, test).tier == RenderTier::accelerated);
        OA_CHECK(on && test.runs == 1);
    }
    // --hardware-acceleration on the player's own profile, and a named
    // preferences file that turns the setting On, test at start.
    for (TierInputs inputs : {player, named_on}) {
        if (inputs.players_own_profile)
            inputs.flag = AccelerationFlag::on;
        inputs.function_test = start_function_test(inputs);
        bool on = false;
        StandInTest test;
        OA_CHECK(drawn_frame(inputs, on, test).tier == RenderTier::accelerated);
        OA_CHECK(on && test.runs == 1);
    }
    // The setting Off at start: no test until it is turned On, and none in
    // a replay until it ends.
    {
        TierInputs inputs = named_on;
        inputs.setting_on = false;
        bool on = false;
        StandInTest test;
        OA_CHECK(drawn_frame(inputs, on, test).reason == TierReason::setting_off);
        begin_match(inputs.match, MatchKind::replay, on);
        inputs.setting_on = true;
        OA_CHECK(drawn_frame(inputs, on, test).reason == TierReason::waiting_for_match_end);
        OA_CHECK(!on && test.runs == 0);
        end_match(inputs.match);
        OA_CHECK(drawn_frame(inputs, on, test).tier == RenderTier::accelerated);
        OA_CHECK(on && test.runs == 1);
    }
    // A failed test keeps the standard tier and does not run again until
    // Off then On.
    {
        TierInputs inputs = named_on;
        bool on = false;
        StandInTest test{false, 0};
        OA_CHECK(drawn_frame(inputs, on, test).reason == TierReason::function_test_failed);
        OA_CHECK(drawn_frame(inputs, on, test).reason == TierReason::function_test_failed);
        OA_CHECK(!on && test.runs == 1);
        forget_failures(inputs);
        test.passes = true;
        OA_CHECK(drawn_frame(inputs, on, test).tier == RenderTier::accelerated);
        OA_CHECK(on && test.runs == 2);
        // A drop keeps it standard until forgotten.
        inputs.drop = Drop::driver_failure;
        OA_CHECK(drawn_frame(inputs, on, test).reason == TierReason::dropped);
        OA_CHECK(!on);
        forget_failures(inputs);
        OA_CHECK(drawn_frame(inputs, on, test).tier == RenderTier::accelerated);
        OA_CHECK(test.runs == 2);
    }

    // The runs that never test and never accelerate.
    struct Standard {
        const char* name;
        void (*change)(TierInputs&);
        TierReason reason;
    };

    const Standard standards[] = {
        {"a named preferences file at its default",
         [](TierInputs& i) {
             i.players_own_profile = false;
             i.setting_on = false;
         },
         TierReason::setting_off},
        {"the dummy video driver",
         [](TierInputs& i) { i.virtual_video_driver = true; },
         TierReason::environment},
        {"SDL_RENDER_DRIVER",
         [](TierInputs& i) { i.render_driver_named = true; },
         TierReason::environment},
        {"one byte under 2 GiB with both flags",
         [](TierInputs& i) {
             i.memory = smallest_accelerated_memory - 1;
             i.flag = AccelerationFlag::on;
             i.force_capable = true;
         },
         TierReason::memory},
        {"memory not reported", [](TierInputs& i) { i.memory = 0; }, TierReason::memory},
        {"--no-hardware-acceleration",
         [](TierInputs& i) { i.flag = AccelerationFlag::off; },
         TierReason::flag_off},
        {"headless", [](TierInputs& i) { i.renderer = false; }, TierReason::no_renderer},
        {"the director",
         [](TierInputs& i) { i.director_frame = true; },
         TierReason::director_frame},
        {"SDL's software renderer",
         [](TierInputs& i) { i.capability = Capability::software_renderer; },
         TierReason::not_capable},
    };
    for (const auto& run : standards) {
        TierInputs inputs = player;
        run.change(inputs);
        inputs.function_test = start_function_test(inputs);
        bool on = false;
        StandInTest test;
        const TierDecision decision = drawn_frame(inputs, on, test);
        if (decision.reason != run.reason || on || test.runs != 0)
            std::fprintf(stderr, "standard run: %s\n", run.name);
        OA_CHECK(decision.reason == run.reason);
        OA_CHECK(!on && test.runs == 0);
    }
    // The dummy video driver with --hardware-acceleration and
    // --force-capable, as native-render-tiers runs: SDL's software renderer
    // is tested and accelerated, but never at the threshold less a byte.
    {
        TierInputs inputs = player;
        inputs.virtual_video_driver = true;
        inputs.players_own_profile = false;
        inputs.setting_on = false;
        inputs.flag = AccelerationFlag::on;
        inputs.force_capable = true;
        inputs.capability = Capability::software_renderer;
        inputs.memory = smallest_accelerated_memory;
        inputs.function_test = start_function_test(inputs);
        bool on = false;
        StandInTest test;
        OA_CHECK(drawn_frame(inputs, on, test).tier == RenderTier::accelerated);
        OA_CHECK(on && test.runs == 1);
    }
}

int main() {
    test_texture_limit();
    test_assess_renderer();
    test_decide_by_table();
    test_decide_every_combination();
    test_function_test_gate();
    test_shared_match_gate();
    test_creation_walk_by_table();
    test_environment_and_rebuilds();
    test_creation_walk_never_fails_for_records();
    test_stalls();
    test_device_resets();
    test_layer_formats();
    test_start_budget();
    test_start_rung();
    test_step_down_rungs();
    test_resume_and_step_up();
    test_step_down_rules();
    test_step_down_pools();
    test_step_down_costs();
    test_prescale();
    test_chrome_filter();
    test_tiles_by_table();
    test_tiles_cover_every_texel();
    test_windowless_and_flag();
    test_tier_action();
    test_forget_failures();
    test_step_tier();
    test_start_function_test();
    test_host_runs();
    return oa::test::check_exit_status();
}
