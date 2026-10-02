// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// --check-renderer-ladder: each renderer failure the game handles while it
// runs, forced on the renderer the start made, and the game presenting on
// through it. On the dummy video driver every hardware driver refuses, so
// the walk ends on SDL's software renderer, and every rebuild makes that
// renderer again. Two cases run only when --render-fault names them, since
// they switch the accelerated tier on, as --hardware-acceleration and
// --force-capable would, where every other case draws in the standard tier:
// slow frames, which walk the step-down down its ladder to the standard
// tier, and the memory guard, which refuses the tier's buffers and then
// drops it.
#include "oa/app/runtime.hpp"
#include "engine_settings_match_host.hpp"
#include "render_host.hpp"
#include "render_run.hpp"
#include "oa/base/float_precision.hpp"
#include "oa/platform/render_probe.hpp"
#include "oa/ui/frontend_dialogs.hpp"
#include <SDL3/SDL.h>
#include <cfenv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace oa::app {
namespace {

namespace policy = render_policy;
namespace render_probe = oa::platform::render_probe;

/// Menu frames the walk's case presents.
constexpr uint32_t walk_menu_frames = 60;
/// The presented frame of its case a failure in a match comes at, unless
/// --render-fault gives one.
constexpr uint32_t match_case_frame = 10;
/// Frames a forced failure may take to come before the case fails.
constexpr uint32_t fault_frames_allowed = 1000;
/// Stalls the stall rule logs at.
constexpr uint32_t stalls_logged_at = 3;
/// The texture limit the tiles' case forces, texels.
constexpr uint32_t tiles_texture_limit = 2048;
/// The window the tiles' case draws, pixels: its battlefield is wider than
/// the limit.
constexpr int tiles_window_width = 2560;
constexpr int tiles_window_height = 1440;
/// How long before now the stall case says its match began, nanoseconds:
/// past a match's first 5 s, whose frames are not steady.
constexpr uint64_t match_begun_ns = 6'000'000'000;
/// Exit code of a check that skipped, which ctest reports as skipped.
constexpr int skipped_exit_code = 77;
/// The interval the slow frames case forces on each frame, nanoseconds:
/// over twice the period of 60 frames a second, so that a second of them
/// steps the ladder at once.
constexpr uint64_t slow_frame_ns = 100'000'000;
/// Frames a step of the ladder may take before the slow frames case fails.
constexpr uint32_t step_frames_allowed = 200;
/// Frames the slow frames case presents where the ladder must not move.
constexpr uint32_t unmoved_frames = 40;
/// Frames the memory case presents for the tier to settle on the rungs
/// below the buffers the memory guard refuses.
constexpr uint32_t refused_buffer_frames = 4;
/// The zooms the slow frames case presents at: one the area pass reduces,
/// zoom 1, and one the card magnifies.
constexpr float zoomed_out_zoom = 0.5F;
constexpr float zoom_one = 1.0F;
constexpr float zoomed_in_zoom = 2.0F;
/// A zoom that is not whole, at which the card magnifies the scene by its
/// own filter, not by NEAREST.
constexpr float part_zoom = 1.5F;
/// The rate the slow frames case has the loop pace at while it adds ticks
/// between frames, frames a second: the loop's lowest, under the 60 the
/// step-down holds a faster loop to.
constexpr uint32_t ticked_frames_per_second = 30;
/// How long the slow frames case presents frames whose ticks bring them on
/// time, nanoseconds on the forced clock: past the slow rule's 3 s.
constexpr uint64_t ticked_span_ns = 4'000'000'000;
/// The window the memory case draws, pixels: its chrome scales by 1.6, so
/// the HUD is drawn through a prescale target.
constexpr int part_scale_width = 1024;
constexpr int part_scale_height = 768;

/// Returns the steady clock's time, as render() reads it.
///
/// @return nanoseconds since the clock's epoch
uint64_t steady_now_ns() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                     std::chrono::steady_clock::now().time_since_epoch()
    )
                                     .count());
}

/// Counts the reports of a changed floating-point setting.
///
/// @param context the count
void count_float_change(
    void* context,
    const oa::base::float_precision::FloatControl&,
    const oa::base::float_precision::FloatControl&
) {
    ++*static_cast<uint32_t*>(context);
}

/// Answers the device's state from the case's own answer.
///
/// @param context the answer
/// @return the answer
render_probe::DeviceState answered_state(void* context) {
    return *static_cast<render_probe::DeviceState*>(context);
}

} // namespace

/// The cases, over one runtime and its renderer.
struct Runtime::RendererLadder {
    Runtime& runtime;
    fs::path report_directory{};
    std::vector<std::string> passed{};

    /// Returns the runtime's renderer state.
    RenderRun& run() { return *runtime.render_run_; }

    /// Throws when a case's expectation does not hold.
    ///
    /// @param holds the expectation
    /// @param where the case
    /// @param what what was expected
    static void expect(bool holds, std::string_view where, std::string_view what) {
        if (!holds)
            throw std::runtime_error(
                "renderer ladder check: " + std::string(where) + ": " + std::string(what)
            );
    }

    /// Forces a failure at a presented frame from now.
    ///
    /// @param point the failure
    /// @param frame the presented frame from now it comes at, from 1
    void arm(RenderFaultPoint point, uint32_t frame) {
        run().fault = point;
        run().fault_frame = run().presented + frame;
    }

    /// Presents frames until the forced failure has come.
    ///
    /// @param where the case
    void present_until_fault(std::string_view where) {
        for (uint32_t frame = 0; frame < fault_frames_allowed && run().fault; ++frame)
            runtime.render();
        expect(!run().fault, where, "the forced failure never came");
    }

    /// Posts a render event for the game's window and lets the event
    /// dispatch take it, as the game's loop does.
    ///
    /// @param type the event
    void post_render_event(uint32_t type) {
        SDL_Event event = render_event(type);
        expect(SDL_PushEvent(&event), "events", "SDL refused the event");
        bool running = true;
        SDL_Event taken{};
        while (SDL_PollEvent(&taken))
            runtime.dispatch_event(taken, running);
    }

    /// Returns a render event for the game's window.
    ///
    /// @param type the event
    /// @return the event
    SDL_Event render_event(uint32_t type) const {
        SDL_Event event{};
        event.type = type;
        event.render.windowID = SDL_GetWindowID(runtime.sdl_.window);
        return event;
    }

    /// Leaves any match for the main menu.
    void to_main_menu() {
        if (runtime.match_)
            runtime.leave_match();
        runtime.load(Screen::main_menu);
    }

    /// Starts the benchmark skirmish from the main menu, with the pointer in
    /// the blank corner right of the bottom bar.
    void start_match() {
        to_main_menu();
        runtime.start_benchmark_skirmish();
        runtime.update_pointer(
            static_cast<float>(runtime.match_layout_.width - 1),
            static_cast<float>(runtime.match_layout_.height - 1)
        );
    }

    /// Checks the next frame against the composed one.
    ///
    /// @param where the case
    void expect_composed(std::string_view where) {
        runtime.expect_presented_equals_composed(
            report_directory, "renderer-ladder-" + std::string(where)
        );
    }

    /// The walk of the render drivers ended on SDL's software renderer
    /// after every earlier driver of SDL's order refused, with the
    /// framebuffer hint set; then menu frames present.
    void walk(bool forced) {
        constexpr std::string_view where = "walk";
        const auto& host = *run().host;
        expect(!host.named(), where, "SDL_RENDER_DRIVER is set; the check walks SDL's drivers");
        const auto attempts = host.attempts();
        expect(!attempts.empty() && attempts.back().created, where, "no driver made the renderer");
        expect(
            host.facts().renderer == render_probe::software_renderer,
            where,
            "the walk ended on " + host.facts().renderer + ", not software"
        );
        for (std::size_t index = 0; index < attempts.size(); ++index) {
            const char* name = SDL_GetRenderDriver(static_cast<int>(index));
            expect(
                name != nullptr && attempts[index].driver == name,
                where,
                "attempt " + std::to_string(index) + " is not SDL's driver of that place"
            );
            if (index + 1 == attempts.size())
                continue;
            expect(
                !attempts[index].created,
                where,
                attempts[index].driver + " both refused and started"
            );
            expect(
                !attempts[index].error.empty(),
                where,
                attempts[index].driver + " refused with no reason"
            );
            if (forced)
                expect(
                    attempts[index].error == refused_by_fault,
                    where,
                    attempts[index].driver + " was not refused by --render-fault create"
                );
        }
        const char* hint = SDL_GetHint(SDL_HINT_FRAMEBUFFER_ACCELERATION);
        if (attempts.size() > 1)
            expect(
                hint != nullptr && std::string_view(hint) == "0",
                where,
                "the framebuffer hint is not 0 after a driver refused"
            );
        const uint64_t before = run().presented;
        for (uint32_t frame = 0; frame < walk_menu_frames; ++frame)
            runtime.render();
        expect(
            run().presented == before + walk_menu_frames && run().rebuilds == 0,
            where,
            "the menu frames were not all presented"
        );
        passed.emplace_back(where);
    }

    /// A present that fails in a match makes the renderer again, and the
    /// next frame is the composed one.
    void present(uint32_t frame) {
        constexpr std::string_view where = "present";
        start_match();
        const uint32_t rebuilds = run().rebuilds;
        arm(RenderFaultPoint::present, frame);
        present_until_fault(where);
        expect(run().rebuilds == rebuilds + 1, where, "the renderer was not made again");
        expect(
            runtime.sdl_.renderer == run().host->renderer(),
            where,
            "the runtime kept the old renderer"
        );
        expect_composed(where);
        passed.emplace_back(where);
    }

    /// A present that fails on a loading frame, in the display sink, makes
    /// the renderer again at the loading pump's next frame.
    void loading_present() {
        constexpr std::string_view where = "loading-present";
        to_main_menu();
        const uint32_t rebuilds = run().rebuilds;
        // The next present is the load's first frame.
        arm(RenderFaultPoint::present, 1);
        runtime.start_benchmark_skirmish();
        expect(!run().fault, where, "the forced failure never came");
        expect(run().rebuilds == rebuilds + 1, where, "the renderer was not made again");
        expect(
            run().rebuilt_on == Screen::loading, where, "the loading pump did not make it again"
        );
        expect(run().pending_rebuild.empty(), where, "a rebuild still waits");
        runtime.update_pointer(
            static_cast<float>(runtime.match_layout_.width - 1),
            static_cast<float>(runtime.match_layout_.height - 1)
        );
        expect_composed(where);
        passed.emplace_back(where);
    }

    /// A texture SDL cannot make again leaves none behind: the old one is
    /// forgotten with its size, and the next frame makes it again and is
    /// the composed one.
    void texture() {
        constexpr std::string_view where = "texture";
        start_match();
        runtime.render();
        expect(runtime.match_hud_tex_ != nullptr, where, "the match drew no HUD texture");
        bool thrown = false;
        try {
            // SDL makes no texture of no size.
            runtime.ensure_streaming_texture(
                runtime.match_hud_tex_,
                runtime.opaque_layer_format(),
                0,
                0,
                runtime.match_hud_tex_w_,
                runtime.match_hud_tex_h_
            );
        } catch (const PresentError&) {
            thrown = true;
        }
        expect(thrown, where, "a texture of no size was made");
        expect(
            runtime.match_hud_tex_ == nullptr && runtime.match_hud_tex_w_ == 0 &&
                runtime.match_hud_tex_h_ == 0,
            where,
            "the destroyed texture was kept"
        );
        expect_composed(where);
        expect(runtime.match_hud_tex_ != nullptr, where, "the HUD texture was not made again");
        passed.emplace_back(where);
    }

    /// A device reset in a match forgets every texture; the next frame
    /// makes them again and is the composed one.
    void reset(uint32_t frame) {
        constexpr std::string_view where = "reset";
        start_match();
        for (uint32_t presented = 1; presented < frame; ++presented)
            runtime.render();
        const uint32_t resets = forget_resets();
        const uint32_t rebuilds = run().rebuilds;
        expect(runtime.match_world_tex_.tile_count() > 0, where, "the match drew no world texture");
        post_render_event(SDL_EVENT_RENDER_DEVICE_RESET);
        expect_reset_taken(where, resets, Screen::match);
        expect_composed(where);
        expect(
            runtime.match_world_tex_.tile_count() > 0, where, "the world texture was not made again"
        );
        expect(run().rebuilds == rebuilds, where, "a single reset made the renderer again");
        passed.emplace_back(where);
    }

    /// Forgets the device resets of the cases before, so that a case's own
    /// reset is never the third within a minute.
    ///
    /// @return the resets handled so far
    uint32_t forget_resets() {
        run().resets = {};
        return run().resets_handled;
    }

    /// Checks that one device reset more was taken, on a screen, and that
    /// it forgot the match's textures.
    ///
    /// @param where the case
    /// @param resets the resets handled before it
    /// @param screen the screen it was taken on
    void expect_reset_taken(std::string_view where, uint32_t resets, Screen screen) {
        expect(run().resets_handled == resets + 1, where, "the reset was not handled");
        expect(run().reset_on == screen, where, "the reset was taken on another screen");
        expect(
            runtime.match_world_tex_.tile_count() == 0 && runtime.match_hud_tex_ == nullptr,
            where,
            "the textures were not forgotten"
        );
    }

    /// A device reset in a match that a drain of input meets is taken, not
    /// dropped: the textures are forgotten and the next frame makes them
    /// again.
    void drain_reset() {
        constexpr std::string_view where = "drain-reset";
        start_match();
        runtime.render();
        const uint32_t resets = forget_resets();
        const uint32_t rebuilds = run().rebuilds;
        SDL_Event event = render_event(SDL_EVENT_RENDER_DEVICE_RESET);
        expect(SDL_PushEvent(&event), where, "SDL refused the event");
        runtime.drain_input();
        expect_reset_taken(where, resets, Screen::match);
        expect(
            !SDL_HasEvents(SDL_EVENT_RENDER_TARGETS_RESET, SDL_EVENT_RENDER_DEVICE_LOST),
            where,
            "a render event was left in the queue"
        );
        expect_composed(where);
        expect(run().rebuilds == rebuilds, where, "a single reset made the renderer again");
        passed.emplace_back(where);
    }

    /// A device reset the movie player hands on is taken as the game takes
    /// one; another event is not.
    void movie_reset() {
        constexpr std::string_view where = "movie-reset";
        start_match();
        runtime.render();
        const uint32_t resets = forget_resets();
        SDL_Event key{};
        key.type = SDL_EVENT_KEY_DOWN;
        Runtime::take_movie_event(&runtime, key);
        expect(run().resets_handled == resets, where, "a key was taken as a reset");
        Runtime::take_movie_event(&runtime, render_event(SDL_EVENT_RENDER_DEVICE_RESET));
        expect_reset_taken(where, resets, Screen::match);
        expect_composed(where);
        passed.emplace_back(where);
    }

    /// A device reset during a load is handled by the loading pump, before
    /// the match's first frame.
    void loading_reset() {
        constexpr std::string_view where = "loading-reset";
        to_main_menu();
        const uint32_t resets = forget_resets();
        SDL_Event event = render_event(SDL_EVENT_RENDER_DEVICE_RESET);
        expect(SDL_PushEvent(&event), where, "SDL refused the event");
        runtime.start_benchmark_skirmish();
        expect(run().resets_handled == resets + 1, where, "the load did not handle the reset");
        expect(run().reset_on == Screen::loading, where, "the loading pump did not take the reset");
        runtime.update_pointer(
            static_cast<float>(runtime.match_layout_.width - 1),
            static_cast<float>(runtime.match_layout_.height - 1)
        );
        expect_composed(where);
        passed.emplace_back(where);
    }

    /// A lost device makes the renderer again at the next frame.
    void lost(uint32_t frame) {
        constexpr std::string_view where = "lost";
        start_match();
        for (uint32_t presented = 1; presented < frame; ++presented)
            runtime.render();
        const uint32_t rebuilds = run().rebuilds;
        post_render_event(SDL_EVENT_RENDER_DEVICE_LOST);
        expect(!run().pending_rebuild.empty(), where, "the lost device asked for no rebuild");
        expect(
            run().rebuilds == rebuilds, where, "the event dispatch made the renderer again itself"
        );
        expect_composed(where);
        expect(run().rebuilds == rebuilds + 1, where, "the renderer was not made again");
        passed.emplace_back(where);
    }

    /// A device that answers it is lost, as one does on some renderers
    /// while another program holds the screen, waits for its reset: its failures make no rebuild and nothing is
    /// read back; after the reset a failure makes the renderer again.
    void device_lost(uint32_t frame) {
        constexpr std::string_view where = "d3d9-lost";
        start_match();
        auto& faults = run().host->faults();
        render_probe::DeviceState answer = render_probe::DeviceState::lost;
        faults.context = &answer;
        faults.device_state = answered_state;
        const uint32_t rebuilds = run().rebuilds;
        arm(RenderFaultPoint::present, frame);
        present_until_fault(where);
        expect(run().device_lost, where, "the failure did not wait for the device");
        expect(
            run().rebuilds == rebuilds, where, "a lost device's failure made the renderer again"
        );
        renderer::Surface read{};
        runtime.capture_frame_ = &read;
        runtime.render();
        runtime.capture_frame_ = nullptr;
        expect(read.rgb.empty(), where, "a lost device's frame was read back");
        arm(RenderFaultPoint::present, 1);
        present_until_fault(where);
        expect(run().rebuilds == rebuilds && run().device_lost, where, "the wait did not hold");
        post_render_event(SDL_EVENT_RENDER_TARGETS_RESET);
        expect(!run().device_lost, where, "the reset did not end the wait");
        answer = render_probe::DeviceState::ok;
        arm(RenderFaultPoint::present, 1);
        present_until_fault(where);
        expect(run().rebuilds == rebuilds + 1, where, "a failure after the reset made no rebuild");
        faults.device_state = nullptr;
        faults.context = nullptr;
        expect_composed(where);
        passed.emplace_back(where);
    }

    /// Presents a forced stall on the next frame.
    ///
    /// @param where the case
    void present_stall(std::string_view where) {
        arm(RenderFaultPoint::stall, 1);
        present_until_fault(where);
    }

    /// A stall on a frame that is not steady is not counted.
    ///
    /// @param where the case
    /// @param when what keeps the frame from being steady
    void expect_stall_passed_over(std::string_view where, std::string_view when) {
        const uint32_t counted = run().stalls.stalls;
        present_stall(where);
        expect(run().stalls.stalls == counted, where, "a stall " + std::string(when) + " counted");
    }

    /// Presents over 2 s count on steady frames alone: not in a match's
    /// first 5 s, within 2 s of a resize, nor at the idle rate. Three on
    /// steady frames log once, and make no rebuild; a fourth logs nothing
    /// more.
    void stall(uint32_t frame) {
        constexpr std::string_view where = "stall";
        start_match();
        for (uint32_t presented = 1; presented < frame; ++presented)
            runtime.render();
        const uint32_t logs = run().stall_logs;
        const uint32_t rebuilds = run().rebuilds;
        // Each frame that is not steady differs in one thing alone from the
        // steady frames that follow.
        run().unsteady_until_ns = 0;
        run().screen_since_ns = steady_now_ns();
        expect_stall_passed_over(where, "in a match's first 5 s");
        run().screen_since_ns = steady_now_ns() - match_begun_ns;
        SDL_Event resized{};
        resized.type = SDL_EVENT_WINDOW_RESIZED;
        resized.window.windowID = SDL_GetWindowID(runtime.sdl_.window);
        expect(!runtime.take_render_event(resized), where, "a resize was kept from the screens");
        expect_stall_passed_over(where, "just after a resize");
        run().unsteady_until_ns = 0;
        const auto wait = runtime.frame_wait_;
        runtime.frame_wait_ = frame_pacing::FrameWait::idle;
        expect_stall_passed_over(where, "at the idle rate");
        runtime.frame_wait_ = wait;
        expect(run().stall_logs == logs, where, "a stall that did not count was logged");
        for (uint32_t stall = 1; stall < stalls_logged_at; ++stall) {
            present_stall(where);
            expect(run().stall_logs == logs, where, "a stall was logged before the third");
        }
        present_stall(where);
        expect(run().stall_logs == logs + 1, where, "three stalls were not logged once");
        present_stall(where);
        expect(run().stall_logs == logs + 1, where, "a fourth stall was logged again");
        expect(run().rebuilds == rebuilds, where, "a stall made the renderer again");
        passed.emplace_back(where);
    }

    /// A floating-point setting changed before a present is put back after
    /// it, and reported once.
    void float_state(uint32_t frame) {
        constexpr std::string_view where = "float";
        start_match();
        auto& guard = oa::base::float_precision::program_float_control();
        const auto saved_hooks = guard.hooks;
        const bool saved_reported = guard.reported;
        uint32_t reports = 0;
        guard.hooks = {&reports, count_float_change};
        guard.reported = false;
        arm(RenderFaultPoint::float_state, frame);
        present_until_fault(where);
        const bool restored = std::fegetround() == FE_TONEAREST;
        const uint32_t first_reports = reports;
        arm(RenderFaultPoint::float_state, 1);
        present_until_fault(where);
        const bool restored_again = std::fegetround() == FE_TONEAREST;
        guard.hooks = saved_hooks;
        guard.reported = saved_reported;
        expect(restored && restored_again, where, "the rounding mode was not put back");
        expect(first_reports == 1 && reports == 1, where, "the change was not reported once");
        passed.emplace_back(where);
    }

    /// A window whose layers pass a texture limit of 2048: the world, the
    /// dialog layer and the OA settings layer go in tiles, no front-end
    /// texture is made, and each frame is the composed one.
    void tiles() {
        constexpr std::string_view where = "tiles";
        auto& faults = run().host->faults();
        faults.texture_limit = tiles_texture_limit;
        to_main_menu();
        expect(
            SDL_SetWindowSize(runtime.sdl_.window, tiles_window_width, tiles_window_height) &&
                SDL_SyncWindow(runtime.sdl_.window),
            where,
            std::string("SDL_SetWindowSize: ") + SDL_GetError()
        );
        start_match();
        expect_composed(where);
        expect(runtime.match_world_tex_.tile_count() > 1, where, "the world was not tiled");
        expect(
            runtime.output_texture_w_ == 0 && runtime.output_texture_h_ == 0 &&
                runtime.frontend_texture_.tile_count() == 0,
            where,
            "a front-end texture was made past the limit"
        );
        runtime.show_match_pause_menu();
        runtime.activate_pause_gadget("HELP");
        expect(
            oa::ui::frontend_dialogs::dialog_kind() == oa::ui::frontend_dialogs::DialogKind::help,
            where,
            "HELP did not open HELP.GUI"
        );
        expect_composed("tiles-help");
        expect(runtime.match_dialog_tex_.tile_count() > 1, where, "the dialog layer was not tiled");
        oa::ui::frontend_dialogs::close_dialog();
        runtime.open_engine_settings_in_match();
        expect(runtime.engine_settings_dialog() != nullptr, where, "the OA settings did not open");
        expect_composed("tiles-settings");
        expect(
            runtime.engine_settings_match_host().layer.tile_count() > 1,
            where,
            "the OA settings layer was not tiled"
        );
        faults.texture_limit = 0;
        passed.emplace_back(where);
    }

    /// Returns the accelerated tier's watch, which must exist.
    ///
    /// @param where the case
    /// @return the watch
    AcceleratedWatch& watch(std::string_view where) {
        expect(run().watch != nullptr, where, "the accelerated tier has no watch");
        return *run().watch;
    }

    /// Returns the rung the cases switch the accelerated tier on at: the
    /// full scene budget with the area pass, magnify on, the chrome filtered
    /// and the card's magnification by sharp-bilinear within the whole
    /// prescale budget, so that every rung below it is there to take.
    static policy::LadderState top_rung() {
        policy::LadderState rung;
        rung.method = policy::ZoomOutMethod::area;
        rung.budget = policy::SceneBudget::full;
        rung.blend_allowed = false;
        rung.magnify = true;
        rung.filtered_chrome = true;
        rung.card = policy::CardFilter::prescale_full;
        rung.standard = false;
        return rung;
    }

    /// Switches the accelerated tier on at a rung, as --hardware-acceleration
    /// and --force-capable would, or back to what the run's own options say.
    ///
    /// @param where the case
    /// @param on true to switch it on
    void accelerate(std::string_view where, bool on) {
        if (on) {
            run().rung = top_rung();
            runtime.options_.hardware_acceleration = true;
            runtime.options_.force_capable = true;
        } else {
            run().rung.reset();
            runtime.options_.hardware_acceleration.reset();
            runtime.options_.force_capable = false;
        }
        runtime.update_render_tier();
        if (on)
            expect(runtime.accelerated_presentation(), where, "the accelerated tier did not draw");
    }

    /// Makes the frames that follow steady: past a match's first 5 s and any
    /// window change, at the full rate, with the match clock at its rate.
    void steady_frames() {
        run().unsteady_until_ns = 0;
        run().screen_since_ns = steady_now_ns() - match_begun_ns;
        runtime.frame_wait_ = frame_pacing::FrameWait::precise;
        runtime.match_timing_.actual_rate = runtime.match_timing_.requested_rate;
    }

    /// Sizes the window and lays the screen out for it.
    ///
    /// @param width the window's width in pixels
    /// @param height its height in pixels
    void resize(int width, int height) {
        expect(
            SDL_SetWindowSize(runtime.sdl_.window, width, height) &&
                SDL_SyncWindow(runtime.sdl_.window),
            "resize",
            std::string("SDL_SetWindowSize: ") + SDL_GetError()
        );
        runtime.apply_output_mode();
    }

    /// Sets the battlefield's zoom.
    ///
    /// @param zoom screen pixels per map pixel
    void at_zoom(float zoom) { runtime.match_zoom_ = runtime.match_zoom_target_ = zoom; }

    /// Presents forced slow frames at a zoom until the ladder takes one step.
    ///
    /// @param where the case
    /// @param zoom the zoom the frames show
    /// @param expected the rung the step must reach
    /// @param what the step, for a failure
    void step_to(
        std::string_view where,
        float zoom,
        const policy::LadderState& expected,
        std::string_view what
    ) {
        at_zoom(zoom);
        auto& watched = watch(where);
        const uint32_t steps = watched.steps;
        for (uint32_t frame = 0; frame < step_frames_allowed && watched.steps == steps; ++frame)
            runtime.render();
        expect(
            watched.steps == steps + 1, where, "slow frames did not step to " + std::string(what)
        );
        expect(
            watched.step_down.state == expected, where, "the step was not to " + std::string(what)
        );
        expect(
            expected.standard || runtime.accelerated_.rung == expected,
            where,
            "the presentation does not draw at " + std::string(what)
        );
    }

    /// Presents one frame at part_zoom that does not feed the step-down,
    /// and tells whether the card drew the magnified scene through its
    /// prescale target.
    ///
    /// @param where the case
    /// @return true when the scene went through the prescale target
    bool magnify_once(std::string_view where) {
        const auto forced = run().forced_frame_ns;
        run().forced_frame_ns.reset();
        at_zoom(part_zoom);
        const uint64_t draws = runtime.accelerated_.counts.prescale_draws;
        runtime.render();
        run().forced_frame_ns = forced;
        expect(runtime.accelerated_.magnified, where, "zoom 1.5 was not magnified");
        return runtime.accelerated_.counts.prescale_draws != draws;
    }

    /// Tells whether the scene a frame drew apart and the terrain at its
    /// size hold no memory.
    ///
    /// @return true when both are freed
    bool scene_buffers_freed() const {
        return runtime.match_scene_cpu_.rgb.capacity() == 0 &&
               runtime.match_terrain_cache_.rgb.capacity() == 0;
    }

    /// Slow frames walk the step-down down its whole ladder, one rung a
    /// step, each step logged once: the budget twice from zoomed-out
    /// frames, NEAREST chrome and the card's two rungs at zoom 1, magnify
    /// off from zoomed-in frames, then the standard tier for the rest of
    /// the run. The standard tier and idle frames never feed it; a frame's
    /// ticks, added between frames, are taken out against the rate the loop
    /// paces at; each step frees what its rung no longer draws with, and
    /// NEAREST chrome leaves the magnified scene's filter as it was; the
    /// status says the tier smooths less. Off then On starts it again from
    /// the top, and a match clock below its rate sheds the budget and
    /// magnification at once.
    ///
    /// @param frame the presented frame of the match the case begins at
    /// @return 0, or skipped_exit_code under 2 GiB of memory
    int slow_frames(uint32_t frame) {
        constexpr std::string_view where = "slow";
        if (run().host->tier_inputs().memory < policy::smallest_accelerated_memory) {
            std::cout << "renderer ladder check: slow: skipped: the machine reports less than the "
                         "2 GiB threshold of memory\n";
            return skipped_exit_code;
        }
        start_match();
        for (uint32_t presented = 1; presented < frame; ++presented)
            runtime.render();
        steady_frames();
        run().forced_frame_ns = slow_frame_ns;
        // The standard tier, before the accelerated tier ever drew: no watch.
        at_zoom(zoomed_out_zoom);
        for (uint32_t presented = 0; presented < unmoved_frames; ++presented)
            runtime.render();
        expect(
            run().watch == nullptr, where, "the standard tier made the accelerated tier's watch"
        );
        // The accelerated tier, then the standard tier between its frames.
        accelerate(where, true);
        accelerate(where, false);
        expect(!runtime.accelerated_presentation(), where, "the tier did not go back to standard");
        for (uint32_t presented = 0; presented < unmoved_frames; ++presented)
            runtime.render();
        expect(
            watch(where).steps == 0 && watch(where).step_down.state == top_rung() &&
                watch(where).step_down.zoomed_out.count == 0 &&
                watch(where).step_down.other.count == 0,
            where,
            "frames of the standard tier fed the step-down"
        );
        accelerate(where, true);
        // Idle frames never feed it, however slow.
        at_zoom(zoomed_out_zoom);
        runtime.frame_wait_ = frame_pacing::FrameWait::idle;
        for (uint32_t presented = 0; presented < unmoved_frames; ++presented)
            runtime.render();
        runtime.frame_wait_ = frame_pacing::FrameWait::precise;
        expect(
            watch(where).steps == 0 && watch(where).step_down.zoomed_out.count == 0,
            where,
            "idle frames fed the step-down"
        );
        // A frame's ticks are taken out, against the rate the loop paces at:
        // paced at 30, frames of one and a half periods whose ticks took half
        // a period are on time, though at 60 they would be twice its period.
        const uint32_t max_frames_per_second = runtime.options_.max_frames_per_second;
        runtime.options_.max_frames_per_second = ticked_frames_per_second;
        expect(
            policy::step_target_rate(runtime.frame_stats_notes().paced_frames_per_second) ==
                ticked_frames_per_second,
            where,
            "the step-down is not held to the rate the loop paces at"
        );
        const uint64_t period_ns = frame_pacing::kNanosecondsPerSecond / ticked_frames_per_second;
        const uint64_t ticks_ns = period_ns / 2;
        run().forced_frame_ns = period_ns + ticks_ns;
        for (uint64_t forced = 0; forced < ticked_span_ns; forced += period_ns + ticks_ns) {
            runtime.phase_times_.simulation += static_cast<int64_t>(ticks_ns);
            runtime.render();
            expect(
                watch(where).frame_ticks_ns == ticks_ns,
                where,
                "the frame's ticks were not the time added between frames"
            );
        }
        expect(
            watch(where).steps == 0 && watch(where).step_down.zoomed_out.count != 0,
            where,
            "frames whose ticks bring them on time stepped, or fed nothing"
        );
        expect(
            runtime.acceleration_report().status.state ==
                oa::ui::engine_settings::AccelerationState::in_use,
            where,
            "the status does not say the tier is in use"
        );
        // Down the ladder: the same frames without their ticks are slow.
        policy::LadderState expected = top_rung();
        expected.budget = policy::SceneBudget::reduced;
        step_to(where, zoomed_out_zoom, expected, "budget reduced");
        expect(
            runtime.acceleration_report().status.state ==
                oa::ui::engine_settings::AccelerationState::in_use_less_smoothing,
            where,
            "the status does not say the tier smooths less"
        );
        runtime.options_.max_frames_per_second = max_frames_per_second;
        run().forced_frame_ns = slow_frame_ns;
        expected.budget = policy::SceneBudget::none;
        step_to(where, zoomed_out_zoom, expected, "budget none");
        expect(scene_buffers_freed(), where, "the scene's buffers outlived budget none");
        expect(
            runtime.accelerated_.world_prescale.made(),
            where,
            "the magnified scene has no prescale target"
        );
        expected.filtered_chrome = false;
        step_to(where, zoom_one, expected, "NEAREST chrome");
        expect(
            !runtime.accelerated_.hud_prescale.made() && runtime.accelerated_.world_prescale.made(),
            where,
            "NEAREST chrome did not free the HUD's prescale target alone"
        );
        // The magnified scene keeps the card's magnification, through the
        // target NEAREST chrome kept.
        expect(
            magnify_once(where) && runtime.accelerated_.world_prescale.made(),
            where,
            "NEAREST chrome changed the magnified scene's filter"
        );
        expected.card = policy::CardFilter::prescale_quarter;
        step_to(where, zoom_one, expected, "a quarter of the prescale budget");
        expect(
            !runtime.accelerated_.world_prescale.made(),
            where,
            "the scene's prescale target outlived a smaller budget"
        );
        expected.card = policy::CardFilter::linear;
        step_to(where, zoom_one, expected, "plain LINEAR");
        expect(
            !magnify_once(where) && !runtime.accelerated_.world_prescale.made(),
            where,
            "plain LINEAR drew the magnified scene through a prescale target"
        );
        // Zoomed in, the scene is magnified until magnify goes off.
        at_zoom(zoomed_in_zoom);
        runtime.render();
        expect(runtime.accelerated_.magnified, where, "zoom 2 was not magnified");
        expected.magnify = false;
        step_to(where, zoomed_in_zoom, expected, "magnify off");
        expect(
            !runtime.accelerated_.scene.made() && !runtime.accelerated_.overlay_texture.made() &&
                scene_buffers_freed(),
            where,
            "the scene outlived magnify off"
        );
        expected.standard = true;
        step_to(where, zoom_one, expected, "the standard tier");
        const auto& inputs = run().host->tier_inputs();
        expect(
            !runtime.accelerated_presentation() && inputs.drop == policy::Drop::slow_frames,
            where,
            "the last rung did not drop the accelerated tier for slow frames"
        );
        expect(
            runtime.acceleration_report().status.state ==
                oa::ui::engine_settings::AccelerationState::slow_frames,
            where,
            "the status does not say frames were slow"
        );
        // The standard tier after the drop feeds nothing, and presents the
        // frame the processor composes.
        const uint32_t steps = watch(where).steps;
        for (uint32_t presented = 0; presented < unmoved_frames; ++presented)
            runtime.render();
        runtime.update_render_tier();
        expect(
            watch(where).steps == steps && !runtime.accelerated_presentation(),
            where,
            "the ladder moved, or the tier came back, after the drop"
        );
        expect_composed(where);
        // Off then On tries again from the top.
        runtime.forget_render_failures();
        runtime.update_render_tier();
        expect(
            runtime.accelerated_presentation() && runtime.accelerated_.rung == top_rung() &&
                watch(where).step_down.state == top_rung(),
            where,
            "Off then On did not start the step-down again from the top"
        );
        expect(
            runtime.acceleration_report().status.state ==
                oa::ui::engine_settings::AccelerationState::in_use,
            where,
            "after Off then On the status still says the tier smooths less"
        );
        // A match clock below its rate sheds the budget and magnification
        // at the first frame whose own passes took any time.
        expect(runtime.match_timing_.requested_rate > 1, where, "the match clock has no rate");
        runtime.match_timing_.actual_rate = runtime.match_timing_.requested_rate - 1;
        expected = top_rung();
        expected.budget = policy::SceneBudget::none;
        expected.magnify = false;
        step_to(where, zoomed_in_zoom, expected, "budget none and magnify off at once");
        runtime.match_timing_.actual_rate = runtime.match_timing_.requested_rate;
        run().forced_frame_ns.reset();
        accelerate(where, false);
        std::cout << "renderer ladder check: slow: " << watch(where).steps
                  << " steps down the ladder, each logged once\n";
        passed.emplace_back(where);
        return 0;
    }

    /// The memory guard, with the system's memory forced: free memory just
    /// over its threshold refuses every new buffer of the accelerated tier,
    /// which stays on the rungs below them, magnify off and plain LINEAR,
    /// and draws on, its status not saying frames were slow; committed
    /// memory past its threshold then drops the tier for the rest of the
    /// run, freeing what it made, the zoomed-out scene and its terrain
    /// among them, with the status saying so, and Off then On does not lift
    /// it.
    ///
    /// @param frame the presented frame of the match the case begins at
    /// @return 0, or skipped_exit_code under 2 GiB of memory
    int memory(uint32_t frame) {
        constexpr std::string_view where = "memory";
        if (run().host->tier_inputs().memory < policy::smallest_accelerated_memory) {
            std::cout << "renderer ladder check: memory: skipped: the machine reports less than "
                         "the 2 GiB threshold of memory\n";
            return skipped_exit_code;
        }
        to_main_menu();
        resize(part_scale_width, part_scale_height);
        start_match();
        for (uint32_t presented = 1; presented < frame; ++presented)
            runtime.render();
        accelerate(where, true);
        auto& watched = watch(where);
        const policy::MemoryGuardThresholds thresholds = watched.memory.thresholds;
        expect(thresholds.physical != 0, where, "the memory guard knows no physical memory");
        oa::platform::SystemMemorySample sample{};
        sample.physical = thresholds.physical;
        sample.available = thresholds.free_floor + 1;
        sample.available_known = true;
        sample.committed = 0;
        sample.committed_known = true;
        run().forced_memory = sample;
        at_zoom(zoomed_in_zoom);
        for (uint32_t presented = 0; presented < refused_buffer_frames; ++presented)
            runtime.render();
        const auto& state = runtime.accelerated_;
        expect(
            runtime.accelerated_presentation(), where, "free memory not yet low dropped the tier"
        );
        expect(
            !state.rung.magnify && state.rung.card == policy::CardFilter::linear && watched.moved &&
                watched.step_down.state == state.rung,
            where,
            "the tier did not stay on the rungs below the buffers refused"
        );
        expect(
            !state.scene.made() && !state.overlay_texture.made() && !state.hud_prescale.made() &&
                !state.world_prescale.made(),
            where,
            "a buffer the memory guard refused was made"
        );
        expect(
            runtime.acceleration_report().status.state ==
                oa::ui::engine_settings::AccelerationState::in_use,
            where,
            "a buffer the memory guard refused was taken for slow frames"
        );
        // Zoomed out, the area pass holds a scene larger than the
        // battlefield, and the terrain at its size.
        at_zoom(zoomed_out_zoom);
        runtime.render();
        const std::size_t scene_capacity = runtime.match_terrain_cache_.rgb.capacity();
        expect(
            runtime.accelerated_.frame.method == SceneMethod::area &&
                !runtime.match_scene_cpu_.rgb.empty() &&
                scene_capacity > runtime.match_world_cpu_.rgb.size(),
            where,
            "zoom 0.5 did not draw a scene larger than the battlefield"
        );
        const uint32_t steps = watched.steps;
        // Committed memory past its threshold, at the next sample.
        sample.committed = thresholds.committed_limit + 1;
        run().forced_memory = sample;
        policy::resume_memory_guard(watched.memory);
        runtime.render();
        const auto& inputs = run().host->tier_inputs();
        expect(
            !runtime.accelerated_presentation() && inputs.drop == policy::Drop::memory &&
                watched.memory.tripped == policy::MemoryGuardCause::committed,
            where,
            "committed memory past its threshold did not drop the tier"
        );
        expect(
            state.base.empty() && state.overlay.empty() && !state.screen_prescale.made(),
            where,
            "a buffer of the accelerated tier outlived the drop"
        );
        // The standard tier's frame after the drop draws the terrain at the
        // battlefield's size, in a buffer of that size.
        expect(
            runtime.match_scene_cpu_.rgb.empty() && runtime.match_scene_cpu_.rgb.capacity() == 0 &&
                runtime.match_terrain_cache_.rgb.capacity() < scene_capacity,
            where,
            "the zoomed-out scene's buffers outlived the drop"
        );
        expect(
            runtime.acceleration_report().status.state ==
                oa::ui::engine_settings::AccelerationState::too_little_memory,
            where,
            "the status does not say there is too little memory"
        );
        runtime.forget_render_failures();
        runtime.update_render_tier();
        expect(
            !runtime.accelerated_presentation() && inputs.drop == policy::Drop::memory,
            where,
            "Off then On lifted the memory guard's drop"
        );
        expect(watched.steps == steps, where, "the drop was taken as a step");
        // The standard tier presents the composed frame, at the window the
        // other cases draw, where SDL's NEAREST lays the chrome out as the
        // composition does.
        resize(kDefaultWindowWidth, kDefaultWindowHeight);
        expect_composed(where);
        run().forced_memory.reset();
        accelerate(where, false);
        passed.emplace_back(where);
        return 0;
    }
};

int Runtime::check_renderer_ladder() {
    if (!render_run_)
        throw std::runtime_error(
            "renderer ladder check: the game has no renderer of its host; run it with a window"
        );
    RendererLadder ladder{*this};
    ladder.report_directory = "local/reports";
    fs::create_directories(ladder.report_directory);
    const auto& fault = options_.render_fault;
    const auto runs = [&](RenderFaultPoint point) { return !fault || fault->point == point; };
    const uint32_t frame = fault && fault->frame ? *fault->frame : match_case_frame;
    // The cases of the accelerated tier run only when named.
    if (fault &&
        (fault->point == RenderFaultPoint::slow || fault->point == RenderFaultPoint::memory)) {
        const int status = fault->point == RenderFaultPoint::slow ? ladder.slow_frames(frame)
                                                                  : ladder.memory(frame);
        if (status == 0)
            std::cout << "renderer ladder check: " << ladder.passed.front() << " passed\n";
        return status;
    }
    if (runs(RenderFaultPoint::create))
        ladder.walk(fault.has_value());
    if (runs(RenderFaultPoint::present)) {
        ladder.present(frame);
        ladder.loading_present();
        ladder.texture();
    }
    if (runs(RenderFaultPoint::reset)) {
        ladder.reset(frame);
        ladder.loading_reset();
        ladder.drain_reset();
        ladder.movie_reset();
    }
    if (runs(RenderFaultPoint::lost)) {
        ladder.lost(frame);
        ladder.device_lost(frame);
    }
    if (runs(RenderFaultPoint::stall))
        ladder.stall(frame);
    if (runs(RenderFaultPoint::float_state))
        ladder.float_state(frame);
    // The tiles force no failure: they run with every case.
    if (!fault)
        ladder.tiles();
    std::cout << "renderer ladder check:";
    for (const auto& name : ladder.passed)
        std::cout << ' ' << name;
    std::cout << " passed; " << render_run_->rebuilds << " renderer(s) made again, "
              << render_run_->resets_handled << " reset(s) handled, " << render_run_->stall_logs
              << " stall log(s)\n";
    return 0;
}

} // namespace oa::app
