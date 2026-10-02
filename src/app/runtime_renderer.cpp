// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The renderer while the game runs: render events, a device lost or reset,
// failed SDL calls that make the renderer again, and present stalls.
#include "oa/app/runtime.hpp"
#include "graphics_report.hpp"
#include "render_host.hpp"
#include "render_run.hpp"
#include "oa/app/full_screen.hpp"
#include "oa/platform/render_probe.hpp"
#include "oa/sim/messages.hpp"
#include <SDL3/SDL.h>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>

namespace oa::app {
namespace {

namespace render_probe = oa::platform::render_probe;

/// The reason a rebuild gives after a lost device.
constexpr std::string_view device_lost_reason = "the graphics device was lost";
/// The reason a rebuild gives after repeated device resets.
constexpr std::string_view device_resets_reason = "three device resets within 60 s";
/// What the match's message log says after a rebuild onto another driver.
constexpr const char* other_driver_message =
    "A graphics driver failed, so the game now uses another one.";
/// What it says after a lost device made the game change driver.
constexpr const char* device_lost_message =
    "The graphics device was lost, so the game changed driver.";
/// What it says after a rebuild onto the same driver.
constexpr const char* same_driver_message = "The graphics driver was restarted after an error.";
/// How long after a resize, a display-mode change or a full-screen switch
/// frames are not steady, nanoseconds.
constexpr uint64_t unsteady_after_window_ns = 2'000'000'000;
/// How long after a match's first frame its frames are not steady,
/// nanoseconds.
constexpr uint64_t match_warming_ns = 5'000'000'000;
/// The present measure a forced stall reports, nanoseconds: over the stall
/// rule's 2 s.
constexpr uint64_t forced_stall_ns = 2'500'000'000;

/// Returns the steady clock's time.
///
/// @return nanoseconds since the clock's epoch
uint64_t steady_now_ns() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                     std::chrono::steady_clock::now().time_since_epoch()
    )
                                     .count());
}

/// Logs a line about the graphics on standard output.
///
/// @param text what follows the graphics prefix
void log_graphics(std::string_view text) {
    std::cout << graphics_log_prefix << text << '\n' << std::flush;
}

} // namespace

bool Runtime::take_render_event(const SDL_Event& event) {
    switch (event.type) {
    case SDL_EVENT_RENDER_TARGETS_RESET:
    case SDL_EVENT_RENDER_DEVICE_RESET:
    case SDL_EVENT_RENDER_DEVICE_LOST:
        break;
    case SDL_EVENT_WINDOW_RESIZED:
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
    case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
    case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN:
    case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
    case SDL_EVENT_DISPLAY_CURRENT_MODE_CHANGED:
        // The window may move or shrink for a while after.
        if (render_run_)
            render_run_->unsteady_until_ns = steady_now_ns() + unsteady_after_window_ns;
        return false;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        // Exclusive full screen loses its device with the focus on some
        // renderers; the device says whether it did.
        if (render_run_ && sdl_.window != nullptr &&
            (SDL_GetWindowFlags(sdl_.window) & SDL_WINDOW_FULLSCREEN) != 0 &&
            SDL_GetWindowFullscreenMode(sdl_.window) != nullptr)
            std::ignore = render_device_lost();
        return false;
    default:
        return false;
    }
    // Another window's renderer is not the game's.
    if (sdl_.window == nullptr || event.render.windowID != SDL_GetWindowID(sdl_.window))
        return true;
    switch (event.type) {
    case SDL_EVENT_RENDER_TARGETS_RESET:
        if (render_run_ && render_run_->device_lost) {
            render_run_->device_lost = false;
            log_graphics(render_run_->host->facts().renderer + " device reset; drawing again");
        }
        break;
    case SDL_EVENT_RENDER_DEVICE_RESET:
        // Every texture went with the device; each streams from the
        // processor, so the next frame makes them again.
        forget_render_textures();
        if (!render_run_)
            break;
        ++render_run_->resets_handled;
        render_run_->reset_on = screen_;
        log_graphics(
            render_run_->host->facts().renderer + " device reset; the textures are made again"
        );
        if (render_policy::note_device_reset(render_run_->resets, SDL_GetTicks()) &&
            render_run_->pending_rebuild.empty())
            render_run_->pending_rebuild = std::string(device_resets_reason);
        break;
    case SDL_EVENT_RENDER_DEVICE_LOST:
        if (render_run_ && render_run_->pending_rebuild.empty())
            render_run_->pending_rebuild = std::string(device_lost_reason);
        break;
    default:
        break;
    }
    return true;
}

void Runtime::forget_render_textures() {
    destroy_match_layer_textures();
    // The accelerated presentation makes its own again as it draws.
    free_accelerated_presentation();
    frontend_texture_.reset();
    output_texture_w_ = 0;
    output_texture_h_ = 0;
    if (indexed_output_.texture != nullptr)
        SDL_DestroyTexture(indexed_output_.texture);
    indexed_output_.texture = nullptr;
    indexed_output_.width = 0;
    indexed_output_.height = 0;
}

bool Runtime::render_device_lost() {
    if (!render_run_)
        return false;
    auto& run = *render_run_;
    if (run.device_lost)
        return true;
    const auto state = run.host->device_state();
    if (state != render_probe::DeviceState::lost && state != render_probe::DeviceState::not_reset)
        return false;
    run.device_lost = true;
    log_graphics(run.host->facts().renderer + " device lost; waiting for its reset");
    return true;
}

void Runtime::note_present_error(const std::string& reason, bool now) {
    if (!render_run_)
        throw std::runtime_error(reason);
    auto& run = *render_run_;
    // What fails while the device is lost is no driver's fault: the wait
    // ends with the device's reset.
    if (render_device_lost())
        return;
    if (run.pending_rebuild.empty())
        run.pending_rebuild = reason;
    if (now)
        rebuild_renderer(run.pending_rebuild);
}

void Runtime::rebuild_renderer(const std::string& reason) {
    auto& run = *render_run_;
    auto& host = *run.host;
    const std::string why = reason;
    const std::string failed = host.facts().renderer;
    // SDL's software renderer that a rebuild made and that failed before
    // it showed a frame would only be made again to fail again.
    if (failed == render_probe::software_renderer && run.rebuilds > 0 &&
        run.presented_since_rebuild == 0)
        throw std::runtime_error(why);
    run.pending_rebuild.clear();
    run.device_lost = false;
    forget_render_textures();
    sdl_.renderer = nullptr;
    host.rebuild(why);
    sdl_.renderer = host.renderer();
    take_renderer_names(host.facts().renderer, stats_adapter_name(host.facts()));
    ++run.rebuilds;
    run.rebuilt_on = screen_;
    run.presented_since_rebuild = 0;
    apply_output_mode();
    keep_pointer_on_screen(sdl_.window);
    if (screen_ == Screen::match && match_) {
        const char* text = host.facts().renderer == failed ? same_driver_message
                           : why == device_lost_reason     ? device_lost_message
                                                           : other_driver_message;
        post_match_message(text, oa::sim::messages::kind_status);
    }
}

void Runtime::note_present_refused(const char* what) {
    if (!render_run_)
        throw std::runtime_error(std::string(what) + ": " + SDL_GetError());
    // Only a render target left set or a renderer that is gone make SDL
    // refuse a present: the game's own fault, which a rebuild would not mend.
    std::ignore = SDL_SetRenderTarget(sdl_.renderer, nullptr);
    if (!render_run_->present_false_logged) {
        render_run_->present_false_logged = true;
        log_graphics(std::string("SDL refused to present a frame: ") + SDL_GetError());
    }
}

bool Runtime::render_fault_due(RenderFaultPoint point) {
    if (!render_run_ || render_run_->fault != point ||
        render_run_->presented + 1 < render_run_->fault_frame)
        return false;
    render_run_->fault.reset();
    return true;
}

render_policy::FrameSample Runtime::present_frame_facts(uint64_t now_ns) const {
    const auto& run = *render_run_;
    render_policy::FrameSample sample;
    const SDL_WindowFlags flags = sdl_.window != nullptr ? SDL_GetWindowFlags(sdl_.window) : 0;
    sample.window_active =
        application_active_ && (flags & (SDL_WINDOW_HIDDEN | SDL_WINDOW_MINIMIZED)) == 0;
    sample.idle = frame_wait_ == frame_pacing::FrameWait::idle;
    const auto now_ms = static_cast<uint64_t>(SDL_GetTicks());
    sample.settling = now_ns < run.unsteady_until_ns || full_screen_switch_.places_window(now_ms) ||
                      full_screen_switch_.awaiting_shown;
    sample.match_warming =
        screen_ == Screen::match && now_ns - run.screen_since_ns < match_warming_ns;
    return sample;
}

bool Runtime::present_frame_steady(uint64_t now_ns) const {
    return render_policy::steady_frame(present_frame_facts(now_ns));
}

void Runtime::note_present_time(uint64_t present_ns) {
    if (!render_run_)
        return;
    auto& run = *render_run_;
    const bool forced = render_fault_due(RenderFaultPoint::stall);
    ++run.presented;
    ++run.presented_since_rebuild;
    const uint64_t now = steady_now_ns();
    const uint64_t interval = run.last_present_ns != 0 ? now - run.last_present_ns : 0;
    run.last_present_ns = now;
    // What the step-down measures between two presents, once the
    // accelerated tier has run: the interval and the one before it, the
    // ticks run between them and the draw measure.
    if (run.watch) {
        auto& watch = *run.watch;
        watch.previous_interval_ns = watch.frame_interval_ns;
        watch.frame_interval_ns = interval;
        watch.frame_ticks_ns =
            phase_times_.simulation > watch.ticks_seen_ns
                ? static_cast<uint64_t>(phase_times_.simulation - watch.ticks_seen_ns)
                : 0;
        watch.frame_draw_ns = phase_times_.compose > watch.draw_seen_ns
                                  ? static_cast<uint64_t>(phase_times_.compose - watch.draw_seen_ns)
                                  : 0;
        watch.ticks_seen_ns = phase_times_.simulation;
        watch.draw_seen_ns = phase_times_.compose;
    }
    // A lost device's presents show nothing and say nothing of the driver.
    if (run.device_lost)
        return;
    // A forced stall stands in for the measure alone: whether the frame
    // counts stays the steady frames' rule.
    if (forced)
        present_ns = forced_stall_ns;
    if (!present_frame_steady(now))
        return;
    run.steady_ns += interval;
    // The standard tier only logs: a stall is most likely paging, which
    // another renderer would not mend.
    if (render_policy::note_present(
            run.stalls, run.steady_ns, present_ns, render_policy::RenderTier::standard
        ) == render_policy::StallAction::log) {
        ++run.stall_logs;
        log_graphics(
            "presenting took over 2 s three times in 10 s on " + run.host->facts().renderer +
            "; carrying on"
        );
    }
}

} // namespace oa::app
