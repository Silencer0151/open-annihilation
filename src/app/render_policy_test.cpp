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
// able to present, and SDL's own call under SDL_RENDER_DRIVER; strikes that
// become records only in the second run in a row, or at the first left-over
// trial where crash evidence counts it; the records written and read back,
// corrupt values ignored, and cleared when the engine version or the
// adapter changes; the sentinel and the trial through a run; present
// stalls; the starting budget, the same at any memory, and the starting
// rung of every kind of machine, with the blend only above 4 GiB; the
// remembered rung and the step-down fed synthetic frames; the chrome's
// filter; the prescale budget; and the tiles of textures beyond the
// renderer's limit. The driver names here are made up: the policy reads
// them only as names.
#include "oa/app/render_policy.hpp"

#include "oa/test/check.hpp"

#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
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
// Records and strikes

void test_leftover_sentinels() {
    RendererRecords records;
    Leftover create_alpha;
    create_alpha.sentinel = SentinelStage::create;
    create_alpha.sentinel_driver = "alpha";

    // The first left-over sentinel is a strike; the same again is a record,
    // with crash evidence counted either way for a sentinel.
    for (const CrashEvidence evidence :
         {CrashEvidence::two_in_a_row, CrashEvidence::first_counts}) {
        records = RendererRecords{};
        LeftoverOutcome outcome = note_leftover(records, create_alpha, evidence);
        OA_CHECK(outcome.driver == "alpha");
        OA_CHECK(outcome.change.changed);
        OA_CHECK(!outcome.change.new_record);
        OA_CHECK(find_record(records, "alpha")->strike.kind == StrikeKind::create);
        OA_CHECK(find_record(records, "alpha")->failed_driver == RecordedFailure::none);
        outcome = note_leftover(records, create_alpha, evidence);
        OA_CHECK(outcome.change.new_record);
        OA_CHECK(find_record(records, "alpha")->failed_driver == RecordedFailure::stopped);
        OA_CHECK(find_record(records, "alpha")->strike.kind == StrikeKind::none);
        OA_CHECK(failed_driver_list(records) == std::vector<std::string_view>{"alpha"});
    }

    // A different stage replaces the strike; a clean pass clears it.
    records = RendererRecords{};
    Leftover standard_alpha = create_alpha;
    standard_alpha.sentinel = SentinelStage::standard;
    (void)note_leftover(records, create_alpha, CrashEvidence::two_in_a_row);
    (void)note_leftover(records, standard_alpha, CrashEvidence::two_in_a_row);
    OA_CHECK(find_record(records, "alpha")->strike.kind == StrikeKind::standard);
    OA_CHECK(find_record(records, "alpha")->failed_driver == RecordedFailure::none);
    OA_CHECK(note_start_passed(record_for(records, "alpha"), false).changed);
    OA_CHECK(find_record(records, "alpha")->strike.kind == StrikeKind::none);
    (void)note_leftover(records, standard_alpha, CrashEvidence::two_in_a_row);
    OA_CHECK(find_record(records, "alpha")->failed_driver == RecordedFailure::none);
    OA_CHECK(note_leftover(records, standard_alpha, CrashEvidence::two_in_a_row).change.new_record);

    // Software's sentinel counts only against the one driver its hint named.
    records = RendererRecords{};
    Leftover software_via;
    software_via.sentinel = SentinelStage::standard;
    software_via.sentinel_driver = "software";
    software_via.via_driver = "beta";
    OA_CHECK(note_leftover(records, software_via, CrashEvidence::two_in_a_row).driver == "beta");
    OA_CHECK(note_leftover(records, software_via, CrashEvidence::two_in_a_row).change.new_record);
    OA_CHECK(find_record(records, "beta")->failed_driver == RecordedFailure::stopped);
    Leftover software_alone = software_via;
    software_alone.via_driver = {};
    for (int start = 0; start < 3; ++start) {
        const LeftoverOutcome outcome =
            note_leftover(records, software_alone, CrashEvidence::first_counts);
        OA_CHECK(outcome.driver.empty());
        OA_CHECK(!outcome.change.changed);
    }
    OA_CHECK(find_record(records, "software") == nullptr);

    // A left-over running sentinel, or one that cannot be read, is only
    // logged; a trial's stages are struck only through the trial.
    records = RendererRecords{};
    for (const SentinelStage stage : {SentinelStage::running, SentinelStage::unreadable}) {
        Leftover leftover;
        leftover.sentinel = stage;
        leftover.sentinel_driver = "alpha";
        const LeftoverOutcome outcome =
            note_leftover(records, leftover, CrashEvidence::first_counts);
        OA_CHECK(outcome.log_unclean_exit);
        OA_CHECK(!outcome.change.changed);
    }
    for (const SentinelStage stage :
         {SentinelStage::none,
          SentinelStage::probe,
          SentinelStage::accelerated,
          SentinelStage::path}) {
        Leftover leftover;
        leftover.sentinel = stage;
        leftover.sentinel_driver = "alpha";
        const LeftoverOutcome outcome =
            note_leftover(records, leftover, CrashEvidence::first_counts);
        OA_CHECK(!outcome.log_unclean_exit);
        OA_CHECK(!outcome.change.changed);
    }
    OA_CHECK(records.drivers.empty());
}

/// The engine version the records cases run.
constexpr std::string_view engine_version = "1.2.3";

/// Empty records, as a start of this engine version reads them.
RendererRecords fresh_records() {
    RendererRecords records;
    (void)clear_on_machine_change(records, unknown_adapter, engine_version);
    return records;
}

/// The records as the next start reads them back: written, read, and
/// cleared of what another engine version made.
///
/// @param records the records at the end of a run
/// @return the records the next start holds
RendererRecords next_start(const RendererRecords& records) {
    RendererRecords read = parse_records(format_records(records));
    (void)clear_on_machine_change(read, unknown_adapter, engine_version);
    return read;
}

/// A driver's empty record, made under this engine version.
///
/// @param driver the driver
/// @return the record
DriverRecord driver_record(std::string_view driver) {
    RendererRecords records = fresh_records();
    return record_for(records, driver);
}

/// A driver's record as the next run reads it back from the file.
///
/// @param record the record at the end of a run
/// @return the record the next run holds; an empty one when nothing of it
///     was written
DriverRecord next_run(const DriverRecord& record) {
    RendererRecords records = fresh_records();
    records.drivers.push_back(record);
    const RendererRecords read = next_start(records);
    const DriverRecord* found = find_record(read, record.driver);
    return found != nullptr ? *found : driver_record(record.driver);
}

void test_leftover_trials() {
    const auto with_trial = [](StrikeKind stage, AcceleratedPath path) {
        RendererRecords records = fresh_records();
        OA_CHECK(set_trial(records, stage, path, "alpha").changed);
        return records;
    };
    const Leftover no_sentinel;

    // Two in a row where the platform's driver faults rarely stop the
    // system. The trial is spent once it has struck.
    RendererRecords records = with_trial(StrikeKind::probe, AcceleratedPath::magnify);
    LeftoverOutcome outcome = note_leftover(records, no_sentinel, CrashEvidence::two_in_a_row);
    OA_CHECK(outcome.driver == "alpha");
    OA_CHECK(outcome.change.changed && !outcome.change.new_record);
    OA_CHECK(records.trial.stage == StrikeKind::none);
    OA_CHECK(find_record(records, "alpha")->strike.kind == StrikeKind::probe);
    records = next_start(records);
    outcome = note_leftover(records, no_sentinel, CrashEvidence::two_in_a_row);
    OA_CHECK(outcome.driver.empty() && !outcome.change.changed);
    OA_CHECK(set_trial(records, StrikeKind::probe, AcceleratedPath::magnify, "alpha").changed);
    records = next_start(records);
    outcome = note_leftover(records, no_sentinel, CrashEvidence::two_in_a_row);
    OA_CHECK(outcome.change.new_record);
    OA_CHECK(find_record(records, "alpha")->accelerated_unusable == RecordedFailure::stopped);
    OA_CHECK(find_record(records, "alpha")->failed_driver == RecordedFailure::none);
    OA_CHECK(failed_driver_list(records).empty());
    records = next_start(records);
    OA_CHECK(find_record(records, "alpha")->accelerated_unusable == RecordedFailure::stopped);
    OA_CHECK(!find_record(records, "alpha")->accelerated_unusable_told);

    // At once before Vista and on Linux.
    records = with_trial(StrikeKind::probe, AcceleratedPath::magnify);
    outcome = note_leftover(records, no_sentinel, CrashEvidence::first_counts);
    OA_CHECK(outcome.change.new_record);
    OA_CHECK(find_record(records, "alpha")->accelerated_unusable == RecordedFailure::stopped);

    // The trial decides whatever the sentinel holds, or when it cannot be
    // read, or when its file is gone.
    for (const SentinelStage stage :
         {SentinelStage::standard,
          SentinelStage::create,
          SentinelStage::running,
          SentinelStage::unreadable,
          SentinelStage::none}) {
        records = with_trial(StrikeKind::probe, AcceleratedPath::magnify);
        Leftover both;
        both.sentinel = stage;
        both.sentinel_driver = "beta";
        outcome = note_leftover(records, both, CrashEvidence::two_in_a_row);
        OA_CHECK(outcome.driver == "alpha");
        OA_CHECK(!outcome.log_unclean_exit);
        OA_CHECK(find_record(records, "beta") == nullptr);
        OA_CHECK(find_record(records, "alpha")->strike.kind == StrikeKind::probe);
    }

    // A path's trial is a strike of that path; another path replaces it.
    records = with_trial(StrikeKind::path, AcceleratedPath::magnify);
    (void)note_leftover(records, no_sentinel, CrashEvidence::two_in_a_row);
    OA_CHECK(set_trial(records, StrikeKind::path, AcceleratedPath::prescale, "alpha").changed);
    records = next_start(records);
    OA_CHECK(records.trial.stage == StrikeKind::path);
    OA_CHECK(records.trial.path == AcceleratedPath::prescale);
    (void)note_leftover(records, no_sentinel, CrashEvidence::two_in_a_row);
    OA_CHECK(find_record(records, "alpha")->accelerated_unusable == RecordedFailure::none);
    OA_CHECK(find_record(records, "alpha")->strike.path == AcceleratedPath::prescale);
    // Passing another path keeps the strike; passing it clears it.
    OA_CHECK(!note_path_passed(record_for(records, "alpha"), AcceleratedPath::magnify).changed);
    OA_CHECK(note_path_passed(record_for(records, "alpha"), AcceleratedPath::prescale).changed);
    OA_CHECK(find_record(records, "alpha")->strike.kind == StrikeKind::none);

    // A start passes the probe stage only when its function test ran.
    records = with_trial(StrikeKind::probe, AcceleratedPath::magnify);
    (void)note_leftover(records, no_sentinel, CrashEvidence::two_in_a_row);
    OA_CHECK(!note_start_passed(record_for(records, "alpha"), false).changed);
    OA_CHECK(find_record(records, "alpha")->strike.kind == StrikeKind::probe);
    OA_CHECK(note_start_passed(record_for(records, "alpha"), true).changed);
    OA_CHECK(find_record(records, "alpha")->strike.kind == StrikeKind::none);

    // Setting and erasing the trial changes the records only when it moves.
    records = fresh_records();
    OA_CHECK(!erase_trial(records).changed);
    OA_CHECK(set_trial(records, StrikeKind::probe, AcceleratedPath::blend, "alpha").changed);
    OA_CHECK(records.trial.path == AcceleratedPath::magnify);
    OA_CHECK(!set_trial(records, StrikeKind::probe, AcceleratedPath::magnify, "alpha").changed);
    OA_CHECK(!set_trial(records, StrikeKind::create, AcceleratedPath::magnify, "beta").changed);
    OA_CHECK(records.trial.driver == "alpha");
    OA_CHECK(erase_trial(records).changed);
    OA_CHECK(!erase_trial(records).changed);
}

void test_running_failures() {
    DriverTraits hardware;
    DriverTraits loses_device;
    loses_device.loses_device_in_normal_use = true;
    DriverTraits software;
    software.software = true;
    const Strike present_3{StrikeKind::present, AcceleratedPath::magnify, 3};
    const Strike present_4{StrikeKind::present, AcceleratedPath::magnify, 4};
    const Strike call_7{StrikeKind::call, AcceleratedPath::magnify, 7};
    const Strike lost{StrikeKind::lost, AcceleratedPath::magnify, 0};
    const Strike resets{StrikeKind::resets, AcceleratedPath::magnify, 0};

    // A present error: a strike, and the same in the next run a record.
    DriverRecord record = driver_record("alpha");
    RecordChange change = note_running_failure(record, present_3, hardware, false);
    OA_CHECK(change.changed && !change.new_record);
    OA_CHECK(record.failed_driver == RecordedFailure::none);
    record = next_run(record);
    OA_CHECK(record.strike.kind == StrikeKind::present && record.strike.call == 3);
    OA_CHECK(!record.struck_this_run);
    change = note_running_failure(record, present_3, hardware, false);
    OA_CHECK(change.new_record);
    OA_CHECK(record.failed_driver == RecordedFailure::present);
    OA_CHECK(record.accelerated_unusable == RecordedFailure::none);

    // The same failure again in one run, as when a rebuild returns to the
    // driver, is still that run's alone; its clean end keeps the strike,
    // and the next run's failure records.
    for (const Strike& failure : {present_3, call_7, lost, resets}) {
        record = driver_record("alpha");
        for (int again = 0; again < 3; ++again)
            (void)note_running_failure(record, failure, hardware, false);
        OA_CHECK(record.failed_driver == RecordedFailure::none);
        OA_CHECK(record.strike.kind == failure.kind && record.struck_this_run);
        if (failure.kind == StrikeKind::call)
            OA_CHECK(record.accelerated_unusable == RecordedFailure::none);
        OA_CHECK(!note_clean_run(record).changed);
        record = next_run(record);
        OA_CHECK(note_running_failure(record, failure, hardware, false).new_record);
        if (failure.kind == StrikeKind::call)
            OA_CHECK(record.accelerated_unusable == RecordedFailure::call);
        else
            OA_CHECK(record.failed_driver != RecordedFailure::none);
    }

    // Another call replaces the strike; a clean run clears it.
    record = driver_record("alpha");
    (void)note_running_failure(record, present_3, hardware, false);
    (void)note_running_failure(record, present_4, hardware, false);
    OA_CHECK(record.failed_driver == RecordedFailure::none);
    OA_CHECK(record.strike.call == 4);
    record = next_run(record);
    OA_CHECK(note_clean_run(record).changed);
    record = next_run(record);
    (void)note_running_failure(record, present_4, hardware, false);
    OA_CHECK(record.failed_driver == RecordedFailure::none);

    // An accelerated-only call: accelerated-unusable at the second run.
    record = driver_record("alpha");
    OA_CHECK(!note_running_failure(record, call_7, hardware, false).new_record);
    record = next_run(record);
    OA_CHECK(note_running_failure(record, call_7, hardware, false).new_record);
    OA_CHECK(record.accelerated_unusable == RecordedFailure::call);
    OA_CHECK(record.failed_driver == RecordedFailure::none);

    // A lost device or three resets: accelerated-unusable at once, and
    // failed-driver only in the second run in a row.
    for (const Strike& failure : {lost, resets}) {
        const RecordedFailure recorded =
            failure.kind == StrikeKind::lost ? RecordedFailure::lost : RecordedFailure::resets;
        record = driver_record("alpha");
        change = note_running_failure(record, failure, hardware, false);
        OA_CHECK(change.new_record);
        OA_CHECK(record.accelerated_unusable == recorded);
        OA_CHECK(record.failed_driver == RecordedFailure::none);
        OA_CHECK(record.strike.kind == failure.kind);
        record = next_run(record);
        OA_CHECK(record.accelerated_unusable == recorded);
        change = note_running_failure(record, failure, hardware, false);
        OA_CHECK(change.new_record);
        OA_CHECK(record.failed_driver == recorded);
    }
    record = driver_record("alpha");
    (void)note_running_failure(record, lost, hardware, false);
    record = next_run(record);
    (void)note_running_failure(record, resets, hardware, false);
    OA_CHECK(record.failed_driver == RecordedFailure::none);
    // A clean run in between clears the strike.
    record = driver_record("alpha");
    (void)note_running_failure(record, lost, hardware, false);
    record = next_run(record);
    OA_CHECK(note_clean_run(record).changed);
    record = next_run(record);
    (void)note_running_failure(record, lost, hardware, false);
    OA_CHECK(record.failed_driver == RecordedFailure::none);

    // Nothing under SDL_RENDER_DRIVER, nothing on a driver whose device is
    // lost in normal use, and never failed-driver against software.
    for (const Strike& failure : {present_3, call_7, lost, resets}) {
        record = driver_record("alpha");
        for (int run = 0; run < 3; ++run) {
            OA_CHECK(!note_running_failure(record, failure, hardware, true).changed);
            OA_CHECK(!note_running_failure(record, failure, loses_device, false).changed);
            record = next_run(record);
        }
        OA_CHECK(record.strike.kind == StrikeKind::none);
        DriverRecord soft = driver_record("software");
        for (int run = 0; run < 3; ++run) {
            (void)note_running_failure(soft, failure, software, false);
            soft = next_run(soft);
        }
        OA_CHECK(soft.failed_driver == RecordedFailure::none);
    }

    // A clean run leaves a start-up stage's strike alone.
    record = driver_record("alpha");
    record.strike.kind = StrikeKind::create;
    OA_CHECK(!note_clean_run(record).changed);
    OA_CHECK(record.strike.kind == StrikeKind::create);
}

void test_records_cleared() {
    RendererRecords records = fresh_records();
    records.adapter = "Card A";
    DriverRecord& alpha = record_for(records, "alpha");
    alpha.failed_driver = RecordedFailure::stopped;
    alpha.failed_driver_told = true;
    DriverRecord& beta = record_for(records, "beta");
    beta.accelerated_unusable = RecordedFailure::call;
    record_for(records, "gamma").strike.kind = StrikeKind::lost;
    record_for(records, "gamma").struck_this_run = true;
    record_for(records, "delta").scale_level.kept = true;
    OA_CHECK(records.drivers.size() == 4);
    OA_CHECK(&record_for(records, "beta") == &records.drivers[1]);
    record_for(records, "software").failed_driver = RecordedFailure::stopped;
    OA_CHECK(failed_driver_list(records) == std::vector<std::string_view>{"alpha"});
    (void)set_trial(records, StrikeKind::probe, AcceleratedPath::magnify, "alpha");
    records.native_density.driver = "alpha";
    records.native_density.engine_version = std::string(engine_version);
    OA_CHECK(clear_failures(records).changed);
    for (const DriverRecord& record : records.drivers) {
        OA_CHECK(record.strike.kind == StrikeKind::none);
        OA_CHECK(!record.struck_this_run);
        OA_CHECK(record.failed_driver == RecordedFailure::none);
        OA_CHECK(record.accelerated_unusable == RecordedFailure::none);
        OA_CHECK(!record.failed_driver_told);
        OA_CHECK(!record.scale_level.kept);
    }
    // The adapter, the trial and the native-density record stay.
    OA_CHECK(records.adapter == "Card A");
    OA_CHECK(records.trial.stage == StrikeKind::probe);
    OA_CHECK(records.native_density.driver == "alpha");
    OA_CHECK(!clear_failures(records).changed);
    OA_CHECK(find_record(records, "epsilon") == nullptr);
    OA_CHECK(same_strike(Strike{}, Strike{}));
    OA_CHECK(!same_strike(
        Strike{StrikeKind::call, AcceleratedPath::magnify, 1},
        Strike{StrikeKind::call, AcceleratedPath::magnify, 2}
    ));
    OA_CHECK(same_strike(
        Strike{StrikeKind::lost, AcceleratedPath::magnify, 1},
        Strike{StrikeKind::lost, AcceleratedPath::blend, 2}
    ));
}

/// Tells whether two drivers' records hold the same.
///
/// @param first one record
/// @param second the other
/// @return true when every field the file keeps is the same
bool same_record(const DriverRecord& first, const DriverRecord& second) {
    const LadderState& one = first.scale_level.rung;
    const LadderState& other = second.scale_level.rung;
    const bool same_rung =
        first.scale_level.kept == second.scale_level.kept &&
        (!first.scale_level.kept ||
         (one.budget == other.budget && one.method == other.method &&
          one.magnify == other.magnify && one.filtered_chrome == other.filtered_chrome &&
          one.card == other.card &&
          first.scale_level.median_percent == second.scale_level.median_percent));
    return first.driver == second.driver && first.adapter == second.adapter &&
           first.engine_version == second.engine_version &&
           same_strike(first.strike, second.strike) &&
           first.failed_driver == second.failed_driver &&
           first.accelerated_unusable == second.accelerated_unusable &&
           first.failed_driver_told == second.failed_driver_told &&
           first.accelerated_unusable_told == second.accelerated_unusable_told && same_rung;
}

void test_records_parse_and_format() {
    RendererRecords records = fresh_records();
    (void)clear_on_machine_change(records, "Example  Graphics\t9000 ", engine_version);
    OA_CHECK(records.adapter == "Example Graphics 9000");
    DriverRecord& alpha = record_for(records, "alpha");
    alpha.strike = Strike{StrikeKind::present, AcceleratedPath::magnify, 12};
    alpha.failed_driver = RecordedFailure::lost;
    alpha.failed_driver_told = true;
    alpha.accelerated_unusable = RecordedFailure::call;
    alpha.scale_level.kept = true;
    alpha.scale_level.rung.budget = SceneBudget::reduced;
    alpha.scale_level.rung.filtered_chrome = true;
    alpha.scale_level.rung.card = CardFilter::prescale_quarter;
    alpha.scale_level.median_percent = 55;
    DriverRecord& beta = record_for(records, "beta");
    beta.strike = Strike{StrikeKind::path, AcceleratedPath::blend, 0};
    beta.accelerated_unusable = RecordedFailure::stopped;
    beta.accelerated_unusable_told = true;
    DriverRecord& gamma = record_for(records, "gamma");
    gamma.strike.kind = StrikeKind::resets;
    gamma.scale_level.kept = true;
    gamma.scale_level.rung.budget = SceneBudget::full;
    gamma.scale_level.rung.method = ZoomOutMethod::blend;
    gamma.scale_level.rung.magnify = true;
    gamma.scale_level.rung.filtered_chrome = true;
    gamma.scale_level.rung.card = CardFilter::pixelart;
    gamma.scale_level.median_percent = 105;
    // Software is never recorded failed-driver, so even one set in memory is
    // not written.
    DriverRecord& soft = record_for(records, "software");
    soft.accelerated_unusable = RecordedFailure::stopped;
    soft.failed_driver = RecordedFailure::stopped;
    // A driver with nothing recorded writes nothing.
    (void)record_for(records, "delta");
    (void)set_trial(records, StrikeKind::path, AcceleratedPath::prescale, "beta");
    records.native_density.driver = "alpha";
    records.native_density.engine_version = std::string(engine_version);

    const RecordValues values = format_records(records);
    const RecordValues expected = {
        {"adapter", "Example Graphics 9000"},
        {"trial", "path prescale beta"},
        {"native-density", "alpha 1.2.3"},
        {"strike.alpha", "present 12 Example Graphics 9000 1.2.3"},
        {"failed-driver.alpha", "lost Example Graphics 9000 1.2.3 told"},
        {"accelerated-unusable.alpha", "call Example Graphics 9000 1.2.3"},
        {"scale-level.alpha",
         "reduced/area/no-magnify/filtered-chrome/prescale-quarter Example Graphics 9000 1.2.3 "
         "0.55"},
        {"strike.beta", "path blend Example Graphics 9000 1.2.3"},
        {"accelerated-unusable.beta", "stopped Example Graphics 9000 1.2.3 told"},
        {"strike.gamma", "resets Example Graphics 9000 1.2.3"},
        {"scale-level.gamma",
         "full/blend/magnify/filtered-chrome/pixelart Example Graphics 9000 1.2.3 1.05"},
        {"accelerated-unusable.software", "stopped Example Graphics 9000 1.2.3"},
    };
    for (const auto& [key, value] : values)
        if (expected.count(key) == 0 || expected.at(key) != value)
            std::fprintf(stderr, "format_records: %s=%s\n", key.c_str(), value.c_str());
    OA_CHECK(values == expected);

    // Read back, the records are the same, and write the same again.
    const RendererRecords read = parse_records(values);
    OA_CHECK(read.adapter == records.adapter);
    OA_CHECK(read.trial.stage == StrikeKind::path);
    OA_CHECK(read.trial.path == AcceleratedPath::prescale);
    OA_CHECK(read.trial.driver == "beta");
    OA_CHECK(read.native_density.driver == "alpha");
    OA_CHECK(read.native_density.engine_version == engine_version);
    OA_CHECK(read.drivers.size() == 4);
    for (const char* driver : {"alpha", "beta", "gamma"})
        OA_CHECK(same_record(*find_record(read, driver), *find_record(records, driver)));
    OA_CHECK(find_record(read, "software")->failed_driver == RecordedFailure::none);
    OA_CHECK(find_record(read, "software")->accelerated_unusable == RecordedFailure::stopped);
    OA_CHECK(find_record(read, "delta") == nullptr);
    OA_CHECK(format_records(read) == values);

    // An empty file reads as no records at all, under no known adapter.
    const RendererRecords empty = parse_records(RecordValues{});
    OA_CHECK(empty.adapter == unknown_adapter);
    OA_CHECK(empty.drivers.empty() && empty.trial.stage == StrikeKind::none);
    const RecordValues adapter_alone = {{"adapter", "unknown"}};
    OA_CHECK(format_records(empty) == adapter_alone);

    // The median is a fraction of the target period, read to the hundredth.
    struct Median {
        const char* text;
        uint32_t percent;
    };

    for (const Median& median :
         {Median{"0.5", 50},
          Median{"0.55", 55},
          Median{"0.559", 55},
          Median{"1", 100},
          Median{"2.00", 200},
          Median{"0.05", 5}}) {
        RecordValues one = {
            {"scale-level.alpha",
             std::string("none/area/no-magnify/nearest-chrome/linear Card 1.2.3 ") + median.text}
        };
        const RendererRecords parsed = parse_records(one);
        OA_CHECK(parsed.drivers.size() == 1);
        OA_CHECK(parsed.drivers[0].scale_level.kept);
        OA_CHECK(parsed.drivers[0].scale_level.median_percent == median.percent);
    }

    // A record written with no engine version known is written as unknown,
    // which the next start clears.
    RendererRecords unversioned;
    record_for(unversioned, "alpha").strike.kind = StrikeKind::create;
    const RecordValues unversioned_values = format_records(unversioned);
    OA_CHECK(unversioned_values.at("strike.alpha") == "create unknown unknown");
    OA_CHECK(next_start(unversioned).drivers.empty());
}

void test_records_corrupt_values() {
    struct Entry {
        const char* key;
        const char* value;
    };

    // Each value is one the engine never writes, and is ignored.
    const Entry corrupt[] = {
        {"strike.alpha", ""},
        {"strike.alpha", "create"},
        {"strike.alpha", "create 1.2.3"},
        {"strike.alpha", "crash Card 1.2.3"},
        {"strike.alpha", "none Card 1.2.3"},
        {"strike.alpha", "path zoom Card 1.2.3"},
        {"strike.alpha", "path Card 1.2.3"},
        {"strike.alpha", "present Card 1.2.3"},
        {"strike.alpha", "present 70000 Card 1.2.3"},
        {"strike.alpha", "call -1 Card 1.2.3"},
        {"strike.alpha", "call 1x Card 1.2.3"},
        {"failed-driver.alpha", "call Card 1.2.3"},
        {"failed-driver.alpha", "none Card 1.2.3"},
        {"failed-driver.alpha", "told Card 1.2.3"},
        {"failed-driver.alpha", "stopped 1.2.3"},
        {"failed-driver.software", "stopped Card 1.2.3"},
        {"accelerated-unusable.alpha", "present Card 1.2.3"},
        {"accelerated-unusable.alpha", "stopped told"},
        {"scale-level.alpha", "full/area/magnify/filtered-chrome Card 1.2.3 0.5"},
        {"scale-level.alpha", "full/area/magnify/filtered-chrome/pixelart/linear Card 1.2.3 0.5"},
        {"scale-level.alpha", "huge/area/magnify/filtered-chrome/pixelart Card 1.2.3 0.5"},
        {"scale-level.alpha", "full//magnify/filtered-chrome/pixelart Card 1.2.3 0.5"},
        {"scale-level.alpha", "full/area/magnify/filtered-chrome/pixelart Card 1.2.3 half"},
        {"scale-level.alpha", "full/area/magnify/filtered-chrome/pixelart Card 1.2.3 0."},
        {"scale-level.alpha", "full/area/magnify/filtered-chrome/pixelart Card 1.2.3 .5"},
        {"scale-level.alpha", "full/area/magnify/filtered-chrome/pixelart Card 1.2.3 0.5x"},
        {"scale-level.alpha", "full/area/magnify/filtered-chrome/pixelart Card 1.2.3 99999999.5"},
        {"scale-level.alpha", "full/area/magnify/filtered-chrome/pixelart 1.2.3 0.5"},
        {"trial", ""},
        {"trial", "probe"},
        {"trial", "probe alpha beta"},
        {"trial", "path alpha"},
        {"trial", "path zoom alpha"},
        {"trial", "create alpha"},
        {"native-density", "alpha"},
        {"native-density", "alpha 1.2.3 extra"},
        {"strike.", "create Card 1.2.3"},
        {"strike.al pha", "create Card 1.2.3"},
        {"crash.alpha", "create Card 1.2.3"},
        {"starting", "create alpha"},
    };
    for (const Entry& entry : corrupt) {
        const RecordValues values = {{entry.key, entry.value}};
        const RendererRecords read = parse_records(values);
        const bool ignored = read.drivers.empty() && read.trial.stage == StrikeKind::none &&
                             read.native_density.driver.empty() && read.adapter == unknown_adapter;
        if (!ignored)
            std::fprintf(stderr, "parse_records read %s=%s\n", entry.key, entry.value);
        OA_CHECK(ignored);
    }

    // A corrupt value leaves the others of its driver.
    const RecordValues mixed = {
        {"failed-driver.alpha", "stopped Card 1.2.3 told"},
        {"strike.alpha", "crash Card 1.2.3"},
        {"accelerated-unusable.alpha", "present Card 1.2.3"},
    };
    RendererRecords read = parse_records(mixed);
    OA_CHECK(read.drivers.size() == 1);
    OA_CHECK(read.drivers[0].failed_driver == RecordedFailure::stopped);
    OA_CHECK(read.drivers[0].failed_driver_told);
    OA_CHECK(read.drivers[0].strike.kind == StrikeKind::none);
    OA_CHECK(read.drivers[0].accelerated_unusable == RecordedFailure::none);
    OA_CHECK(read.drivers[0].adapter == "Card");

    // A driver's values made under another adapter or version than its
    // first are ignored.
    const RecordValues disagreeing = {
        {"accelerated-unusable.alpha", "lost Card A 1.2.3"},
        {"failed-driver.alpha", "lost Card B 1.2.3"},
        {"strike.alpha", "lost Card A 1.2.2"},
        {"scale-level.alpha", "full/area/magnify/filtered-chrome/linear Card A 1.2.3 0.40"},
    };
    read = parse_records(disagreeing);
    OA_CHECK(read.drivers.size() == 1);
    OA_CHECK(read.drivers[0].adapter == "Card A");
    OA_CHECK(read.drivers[0].accelerated_unusable == RecordedFailure::lost);
    OA_CHECK(read.drivers[0].failed_driver == RecordedFailure::none);
    OA_CHECK(read.drivers[0].strike.kind == StrikeKind::none);
    OA_CHECK(read.drivers[0].scale_level.kept);
    OA_CHECK(read.drivers[0].scale_level.median_percent == 40);

    // An adapter whose last word is the told mark is still an adapter.
    read = parse_records(RecordValues{{"failed-driver.alpha", "stopped told 1.2.3"}});
    OA_CHECK(read.drivers.size() == 1);
    OA_CHECK(read.drivers[0].adapter == "told" && !read.drivers[0].failed_driver_told);
}

void test_clear_on_machine_change() {
    RendererRecords records = parse_records(
        RecordValues{
            {"adapter", "Card A"},
            {"trial", "probe alpha"},
            {"native-density", "alpha 1.2.2"},
            {"strike.alpha", "create Card A 1.2.3"},
            {"failed-driver.beta", "stopped Card A 1.2.3"},
            {"scale-level.gamma", "full/area/magnify/filtered-chrome/linear Card A 1.2.2 0.40"},
        }
    );
    OA_CHECK(records.drivers.size() == 3);

    // At the start, before the adapter is read: the old version's records
    // go, the rest stay.
    RecordChange change = clear_on_machine_change(records, unknown_adapter, engine_version);
    OA_CHECK(change.changed && !change.new_record);
    OA_CHECK(find_record(records, "gamma") == nullptr);
    OA_CHECK(find_record(records, "alpha")->strike.kind == StrikeKind::create);
    OA_CHECK(find_record(records, "beta")->failed_driver == RecordedFailure::stopped);
    OA_CHECK(records.native_density.driver.empty());
    OA_CHECK(records.trial.stage == StrikeKind::probe);
    OA_CHECK(records.adapter == "Card A");
    OA_CHECK(records.engine_version == engine_version);

    // The same adapter, however it is spaced, changes nothing; nor does an
    // adapter that cannot be read.
    OA_CHECK(!clear_on_machine_change(records, " Card   A", engine_version).changed);
    OA_CHECK(!clear_on_machine_change(records, unknown_adapter, engine_version).changed);
    OA_CHECK(!clear_on_machine_change(records, "", engine_version).changed);
    OA_CHECK(records.drivers.size() == 2);

    // Another adapter clears every driver's records and the native-density
    // record, and becomes the records' adapter; the trial stays.
    records.native_density.driver = "alpha";
    records.native_density.engine_version = std::string(engine_version);
    change = clear_on_machine_change(records, "Card B", engine_version);
    OA_CHECK(change.changed);
    OA_CHECK(records.drivers.empty());
    OA_CHECK(records.native_density.driver.empty());
    OA_CHECK(records.adapter == "Card B");
    OA_CHECK(records.trial.stage == StrikeKind::probe);

    // What is made now is made under the new adapter and version, and a
    // newer engine clears it.
    DriverRecord& delta = record_for(records, "delta");
    OA_CHECK(delta.adapter == "Card B" && delta.engine_version == engine_version);
    delta.strike.kind = StrikeKind::standard;
    OA_CHECK(!clear_on_machine_change(records, "Card B", engine_version).changed);
    OA_CHECK(clear_on_machine_change(records, unknown_adapter, "1.3.0").changed);
    OA_CHECK(records.drivers.empty());

    // A record holding nothing goes without counting as a change.
    (void)record_for(records, "epsilon");
    OA_CHECK(!clear_on_machine_change(records, "Card B", "1.4.0").changed);
    OA_CHECK(records.drivers.empty());
}

void test_sentinel_values() {
    struct Case {
        const char* value;
        SentinelStage stage;
        const char* driver;
        AcceleratedPath path;
        const char* via;
    };

    const Case cases[] = {
        {"create alpha", SentinelStage::create, "alpha", AcceleratedPath::magnify, ""},
        {"standard alpha", SentinelStage::standard, "alpha", AcceleratedPath::magnify, ""},
        {"probe alpha", SentinelStage::probe, "alpha", AcceleratedPath::magnify, ""},
        {"accelerated alpha", SentinelStage::accelerated, "alpha", AcceleratedPath::magnify, ""},
        {"running alpha", SentinelStage::running, "alpha", AcceleratedPath::magnify, ""},
        {"path prescale alpha", SentinelStage::path, "alpha", AcceleratedPath::prescale, ""},
        {"path blend alpha", SentinelStage::path, "alpha", AcceleratedPath::blend, ""},
        {"  standard \t alpha ", SentinelStage::standard, "alpha", AcceleratedPath::magnify, ""},
        // The framebuffer hint's list counts only when it names one driver.
        {"standard software via beta",
         SentinelStage::standard,
         "software",
         AcceleratedPath::magnify,
         "beta"},
        {"create software via beta,gamma",
         SentinelStage::create,
         "software",
         AcceleratedPath::magnify,
         ""},
        {"create software", SentinelStage::create, "software", AcceleratedPath::magnify, ""},
        // Values the engine never writes cannot be read.
        {"", SentinelStage::unreadable, "", AcceleratedPath::magnify, ""},
        {"create", SentinelStage::unreadable, "", AcceleratedPath::magnify, ""},
        {"create alpha beta", SentinelStage::unreadable, "", AcceleratedPath::magnify, ""},
        {"running", SentinelStage::unreadable, "", AcceleratedPath::magnify, ""},
        {"path alpha", SentinelStage::unreadable, "", AcceleratedPath::magnify, ""},
        {"path zoom alpha", SentinelStage::unreadable, "", AcceleratedPath::magnify, ""},
        {"standard alpha via beta", SentinelStage::unreadable, "", AcceleratedPath::magnify, ""},
        {"standard software via", SentinelStage::unreadable, "", AcceleratedPath::magnify, ""},
        {"standard software through beta",
         SentinelStage::unreadable,
         "",
         AcceleratedPath::magnify,
         ""},
        {"running software via beta", SentinelStage::unreadable, "", AcceleratedPath::magnify, ""},
        {"none alpha", SentinelStage::unreadable, "", AcceleratedPath::magnify, ""},
        {"unreadable alpha", SentinelStage::unreadable, "", AcceleratedPath::magnify, ""},
    };
    for (const Case& test : cases) {
        const Leftover read = parse_sentinel(test.value);
        const bool matches = read.sentinel == test.stage && read.sentinel_driver == test.driver &&
                             read.via_driver == test.via &&
                             (test.stage != SentinelStage::path || read.sentinel_path == test.path);
        if (!matches)
            std::fprintf(stderr, "parse_sentinel case: '%s'\n", test.value);
        OA_CHECK(matches);
    }

    // Written, every stage reads back as it was.
    for (const SentinelStage stage :
         {SentinelStage::create,
          SentinelStage::standard,
          SentinelStage::probe,
          SentinelStage::accelerated,
          SentinelStage::path,
          SentinelStage::running}) {
        for (const AcceleratedPath path :
             {AcceleratedPath::magnify, AcceleratedPath::prescale, AcceleratedPath::blend}) {
            const Leftover read = parse_sentinel(format_sentinel(stage, "alpha", path, ""));
            OA_CHECK(read.sentinel == stage && read.sentinel_driver == "alpha");
            OA_CHECK(stage != SentinelStage::path || read.sentinel_path == path);
        }
    }
    OA_CHECK(
        format_sentinel(SentinelStage::path, "alpha", AcceleratedPath::blend, "") ==
        "path blend alpha"
    );
    OA_CHECK(
        format_sentinel(
            SentinelStage::standard, "software", AcceleratedPath::magnify, "beta,gamma"
        ) == "standard software via beta,gamma"
    );
    OA_CHECK(
        format_sentinel(SentinelStage::create, "software", AcceleratedPath::magnify, "beta") ==
        "create software via beta"
    );
    // A via list goes only with software, and only at a start-up stage.
    OA_CHECK(
        format_sentinel(SentinelStage::standard, "alpha", AcceleratedPath::magnify, "beta") ==
        "standard alpha"
    );
    OA_CHECK(
        format_sentinel(SentinelStage::running, "software", AcceleratedPath::magnify, "beta") ==
        "running software"
    );
    OA_CHECK(format_sentinel(SentinelStage::none, "alpha", AcceleratedPath::magnify, "").empty());
    OA_CHECK(
        format_sentinel(SentinelStage::unreadable, "alpha", AcceleratedPath::magnify, "").empty()
    );

    // A left-over software sentinel counts against the one driver its list
    // named, at the second start in a row, and against none when it named
    // several.
    RendererRecords records = fresh_records();
    const std::string one = format_sentinel(SentinelStage::standard, "software", {}, "beta");
    const std::string several =
        format_sentinel(SentinelStage::standard, "software", {}, "beta,gamma");
    for (int start = 0; start < 3; ++start)
        OA_CHECK(!note_leftover(records, parse_sentinel(several), CrashEvidence::first_counts)
                      .change.changed);
    OA_CHECK(records.drivers.empty());
    OA_CHECK(
        !note_leftover(records, parse_sentinel(one), CrashEvidence::two_in_a_row).change.new_record
    );
    records = next_start(records);
    OA_CHECK(
        note_leftover(records, parse_sentinel(one), CrashEvidence::two_in_a_row).change.new_record
    );
    OA_CHECK(failed_driver_list(records) == std::vector<std::string_view>{"beta"});
}

/// Moves a run's sentinel and writes down what the step writes: `trial` and
/// the trial's stage, the sentinel's new value for the driver `d`, and
/// `erase` for the trial's erasure, separated by `; `; `-` for nothing.
///
/// @param life the run's sentinel
/// @param event the event
/// @param path the path, for the path events
/// @return what the step writes
std::string
step(SentinelLife& life, LifeEvent event, AcceleratedPath path = AcceleratedPath::magnify) {
    const SentinelWrite write = sentinel_step(life, event, path);
    std::string text;
    const auto add = [&text](const std::string& part) {
        if (!text.empty())
            text += "; ";
        text += part;
    };
    if (write.write_trial) {
        // A trial reads like a sentinel of its stage.
        const SentinelStage stage =
            write.trial_stage == StrikeKind::path ? SentinelStage::path : SentinelStage::probe;
        add("trial " + format_sentinel(stage, "d", write.trial_path, ""));
    }
    if (write.set_sentinel)
        add(format_sentinel(write.sentinel, "d", write.sentinel_path, ""));
    if (write.erase_trial)
        add("erase");
    return text.empty() ? std::string("-") : text;
}

void test_sentinel_life() {
    // A start that runs the function test and passes its start-up stage;
    // a path first used under the start-up sentinel writes nothing of its
    // own, and each later path brackets its first frames.
    SentinelLife life = start_sentinel_life(false);
    OA_CHECK(step(life, LifeEvent::creating) == "create d");
    OA_CHECK(step(life, LifeEvent::creating) == "create d");
    OA_CHECK(step(life, LifeEvent::probing) == "standard d");
    OA_CHECK(step(life, LifeEvent::function_test) == "trial probe d; probe d");
    OA_CHECK(step(life, LifeEvent::function_test_done) == "standard d");
    OA_CHECK(step(life, LifeEvent::path_first_use, AcceleratedPath::prescale) == "-");
    OA_CHECK(step(life, LifeEvent::first_accelerated_frame) == "accelerated d");
    OA_CHECK(step(life, LifeEvent::first_accelerated_frame) == "-");
    OA_CHECK(step(life, LifeEvent::path_passed, AcceleratedPath::prescale) == "-");
    OA_CHECK(step(life, LifeEvent::start_passed) == "running d; erase");
    OA_CHECK(step(life, LifeEvent::start_passed) == "-");
    OA_CHECK(
        step(life, LifeEvent::path_first_use, AcceleratedPath::magnify) ==
        "trial path magnify d; path magnify d"
    );
    OA_CHECK(step(life, LifeEvent::path_passed, AcceleratedPath::prescale) == "-");
    OA_CHECK(step(life, LifeEvent::path_first_use, AcceleratedPath::blend) == "-");
    OA_CHECK(step(life, LifeEvent::path_passed, AcceleratedPath::magnify) == "running d; erase");
    OA_CHECK(
        step(life, LifeEvent::path_first_use, AcceleratedPath::blend) ==
        "trial path blend d; path blend d"
    );
    OA_CHECK(step(life, LifeEvent::path_passed, AcceleratedPath::blend) == "running d; erase");

    // A start with no function test: standard, then running, with no trial
    // to erase.
    life = start_sentinel_life(false);
    OA_CHECK(step(life, LifeEvent::creating) == "create d");
    OA_CHECK(step(life, LifeEvent::probing) == "standard d");
    OA_CHECK(step(life, LifeEvent::function_test_done) == "-");
    OA_CHECK(step(life, LifeEvent::start_passed) == "running d");
    // The function test run later, when the player turns the setting On:
    // its trial stands through the first accelerated frames.
    OA_CHECK(step(life, LifeEvent::function_test) == "trial probe d; probe d");
    OA_CHECK(step(life, LifeEvent::function_test_done) == "standard d");
    OA_CHECK(step(life, LifeEvent::first_accelerated_frame) == "accelerated d");
    OA_CHECK(step(life, LifeEvent::start_passed) == "running d; erase");

    // A trial that cannot be written skips its stage: the sentinel stands
    // where it stood, and no trial is erased later.
    life = start_sentinel_life(false);
    (void)step(life, LifeEvent::creating);
    (void)step(life, LifeEvent::probing);
    OA_CHECK(step(life, LifeEvent::function_test) == "trial probe d; probe d");
    note_trial_unwritten(life);
    OA_CHECK(life.stage == SentinelStage::standard && !life.trial);
    OA_CHECK(step(life, LifeEvent::function_test_done) == "-");
    OA_CHECK(step(life, LifeEvent::start_passed) == "running d");
    OA_CHECK(
        step(life, LifeEvent::path_first_use, AcceleratedPath::magnify) ==
        "trial path magnify d; path magnify d"
    );
    note_trial_unwritten(life);
    OA_CHECK(life.stage == SentinelStage::running && !life.trial);
    OA_CHECK(step(life, LifeEvent::path_passed, AcceleratedPath::magnify) == "-");
    note_trial_unwritten(life);
    OA_CHECK(life.stage == SentinelStage::running);

    // Under SDL_RENDER_DRIVER nothing is written at all.
    life = start_sentinel_life(true);
    for (const LifeEvent event :
         {LifeEvent::creating,
          LifeEvent::probing,
          LifeEvent::function_test,
          LifeEvent::function_test_done,
          LifeEvent::first_accelerated_frame,
          LifeEvent::start_passed,
          LifeEvent::path_first_use,
          LifeEvent::path_passed})
        OA_CHECK(step(life, event) == "-");

    // The start-up stage passes after 60 frames and 2 s, both.
    OA_CHECK(!start_stage_passed(start_stage_frames - 1, 3 * start_stage_ns));
    OA_CHECK(!start_stage_passed(10 * start_stage_frames, start_stage_ns - 1));
    OA_CHECK(start_stage_passed(start_stage_frames, start_stage_ns));

    // The trial's life in the records: written before the stage, erased when
    // it passes, and the records written only when one changes.
    RendererRecords records = fresh_records();
    life = start_sentinel_life(false);
    (void)sentinel_step(life, LifeEvent::probing, AcceleratedPath::magnify);
    const SentinelWrite before = sentinel_step(life, LifeEvent::function_test, {});
    OA_CHECK(set_trial(records, before.trial_stage, before.trial_path, "alpha").changed);
    records = next_start(records);
    OA_CHECK(records.trial.stage == StrikeKind::probe && records.trial.driver == "alpha");
    (void)sentinel_step(life, LifeEvent::function_test_done, {});
    const SentinelWrite passed = sentinel_step(life, LifeEvent::start_passed, {});
    OA_CHECK(passed.erase_trial && erase_trial(records).changed);
    OA_CHECK(!erase_trial(records).changed);
    OA_CHECK(next_start(records).trial.stage == StrikeKind::none);
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

} // namespace

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
    test_leftover_sentinels();
    test_leftover_trials();
    test_running_failures();
    test_records_cleared();
    test_records_parse_and_format();
    test_records_corrupt_values();
    test_clear_on_machine_change();
    test_sentinel_values();
    test_sentinel_life();
    test_stalls();
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
    return oa::test::check_exit_status();
}
