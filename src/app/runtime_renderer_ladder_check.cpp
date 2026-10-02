// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// --check-renderer-ladder: each renderer failure the game handles while it
// runs, forced on the renderer the start made, and the game presenting on
// through it. On the dummy video driver every hardware driver refuses, so
// the walk ends on SDL's software renderer, and every rebuild makes that
// renderer again.
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
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace oa::app {
namespace {

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
};

void Runtime::check_renderer_ladder() {
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
}

} // namespace oa::app
