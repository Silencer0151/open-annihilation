// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "render_host.hpp"

#include "graphics_report.hpp"
#include "oa/base/float_precision.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace oa::app {

namespace render_probe = oa::platform::render_probe;

std::string refusal_log_line(std::string_view driver, std::string_view reason) {
    std::string line(graphics_log_prefix);
    line += "renderer ";
    line += driver;
    line += " refused: ";
    line += reason;
    return line;
}

std::string framebuffer_hint_log_line(std::string_view value, std::string_view reason) {
    std::string line(graphics_log_prefix);
    line += "the framebuffer hint \"";
    line += value;
    line += "\" was not taken: ";
    line += reason;
    return line;
}

std::string second_walk_log_line() {
    std::string line(graphics_log_prefix);
    line += "no driver left could present; trying SDL's render drivers again from the top, "
            "those recorded as failed too";
    return line;
}

namespace {

/// Logs a line through the hooks, where they log.
///
/// @param hooks the walk's hooks
/// @param line the line, without its line break
void log_line(const CreationHooks& hooks, const std::string& line) {
    if (hooks.log != nullptr)
        hooks.log(hooks.context, line);
}

} // namespace

std::string rebuild_log_line(std::string_view driver, std::string_view reason) {
    std::string line(graphics_log_prefix);
    line += driver;
    line += " failed: ";
    line += reason;
    line += "; making another renderer";
    return line;
}

namespace {

/// Walks the render drivers from where a walk stands until one makes the
/// renderer (walk_render_drivers).
///
/// @param walk the walk, before its first attempt
/// @param inputs the inputs the walk was started with
/// @param hooks what sets the hint, makes the renderer and logs
/// @return every attempt, whether the last made the renderer, and the
///     error when none did
CreationOutcome walk_drivers(
    render_policy::CreationWalk walk,
    const render_policy::CreationInputs& inputs,
    const CreationHooks& hooks
);

} // namespace

CreationOutcome
walk_render_drivers(const render_policy::CreationInputs& inputs, const CreationHooks& hooks) {
    return walk_drivers(render_policy::start_creation(inputs), inputs, hooks);
}

CreationOutcome walk_rebuild_drivers(
    const render_policy::CreationInputs& inputs,
    std::string_view failed_driver,
    const CreationHooks& hooks
) {
    return walk_drivers(render_policy::start_rebuild(inputs, failed_driver), inputs, hooks);
}

namespace {

CreationOutcome walk_drivers(
    render_policy::CreationWalk walk,
    const render_policy::CreationInputs& inputs,
    const CreationHooks& hooks
) {
    CreationOutcome outcome;
    bool second_walk_logged = false;
    for (;;) {
        const render_policy::Attempt attempt = render_policy::next_attempt(walk, inputs);
        if (attempt.kind == render_policy::AttemptKind::none)
            break;
        if (attempt.records_ignored && !second_walk_logged) {
            log_line(hooks, second_walk_log_line());
            second_walk_logged = true;
        }
        if (attempt.set_framebuffer_hint) {
            std::string reason;
            if (hooks.set_framebuffer_hint == nullptr ||
                !hooks.set_framebuffer_hint(hooks.context, attempt.framebuffer_hint, reason))
                log_line(
                    hooks,
                    framebuffer_hint_log_line(
                        attempt.framebuffer_hint,
                        reason.empty() ? unexplained_refusal : std::string_view(reason)
                    )
                );
        }
        CreationAttempt made;
        if (attempt.kind == render_policy::AttemptKind::driver)
            made.driver = std::string(attempt.driver);
        std::string reason;
        made.created = hooks.create != nullptr && hooks.create(hooks.context, made.driver, reason);
        if (made.created) {
            outcome.attempts.push_back(std::move(made));
            outcome.created = true;
            return outcome;
        }
        made.error = reason.empty() ? std::string(unexplained_refusal) : std::move(reason);
        if (attempt.kind == render_policy::AttemptKind::driver)
            log_line(hooks, refusal_log_line(made.driver, made.error));
        outcome.attempts.push_back(std::move(made));
    }
    outcome.error = std::string(renderer_creation_error);
    outcome.error += outcome.attempts.empty() ? no_render_driver
                                              : std::string_view(outcome.attempts.back().error);
    return outcome;
}

#if SDL_VERSION_ATLEAST(3, 4, 0)
/// The first SDL release whose framebuffer hint takes a comma list of
/// drivers.
constexpr int framebuffer_list_version = SDL_VERSIONNUM(3, 4, 0);
#endif

/// Says whether the framebuffer hint takes a comma list of drivers: the
/// SDL the game was built against and the SDL it runs on are both 3.4 or
/// later, since a shared SDL library can be older than its headers.
///
/// @return true where a list is taken; otherwise the hint names one driver
bool framebuffer_hint_takes_list() noexcept {
#if SDL_VERSION_ATLEAST(3, 4, 0)
    return SDL_GetVersion() >= framebuffer_list_version;
#else
    return false;
#endif
}

/// What SDL's creation hooks act on.
struct SdlCreation {
    SDL_Window* window{};             ///< the window the renderer is made for
    SDL_Renderer* renderer{};         ///< the renderer made; null until one is
    const RenderFaultHooks* faults{}; ///< what a check forces; null for nothing
};

/// Sets SDL's framebuffer hint at normal priority, so that an environment
/// variable still wins.
///
/// @param value the hint's value
/// @param[out] error SDL's reason when the hint was not taken; empty when
///     SDL gave none, as when the hint is already held at a higher priority
/// @return true when SDL took it
bool set_sdl_framebuffer_hint(void*, const std::string& value, std::string& error) {
    // SDL gives a reason only for some refusals, so an older error must not
    // stand in for one it did not give.
    SDL_ClearError();
    if (SDL_SetHint(SDL_HINT_FRAMEBUFFER_ACCELERATION, value.c_str()))
        return true;
    error = SDL_GetError();
    return false;
}

/// Creates the renderer of a render driver, or SDL's own choice.
///
/// @param context the SdlCreation
/// @param driver SDL's name for the render driver; empty for SDL's own choice
/// @param[out] error SDL's reason when the driver refused
/// @return true when the renderer was made
bool create_sdl_renderer(void* context, const std::string& driver, std::string& error) {
    auto& creation = *static_cast<SdlCreation*>(context);
    if (const auto* faults = creation.faults; faults != nullptr &&
                                              faults->refuse_driver != nullptr && !driver.empty() &&
                                              faults->refuse_driver(faults->context, driver)) {
        error = std::string(refused_by_fault);
        return false;
    }
    creation.renderer =
        SDL_CreateRenderer(creation.window, driver.empty() ? nullptr : driver.c_str());
    if (creation.renderer != nullptr)
        return true;
    error = SDL_GetError();
    return false;
}

/// Logs a line on standard output at once, so that a driver that ends the
/// process while the walk goes on leaves the refusals before it in the log.
///
/// @param line the line, without its line break
void log_graphics_line(void*, const std::string& line) {
    std::cout << line << '\n' << std::flush;
}

/// SDL's render drivers and SDL_RENDER_DRIVER's list, kept while a walk
/// views them.
struct DriverNames {
    std::vector<std::string> sdl_names;              ///< SDL's drivers, in its order
    std::vector<std::string> environment_names;      ///< SDL_RENDER_DRIVER's, in its order
    std::vector<std::string_view> sdl_order;         ///< views of sdl_names
    std::vector<std::string_view> environment_order; ///< views of environment_names
};

/// Reads SDL's render drivers and SDL_RENDER_DRIVER's comma list.
///
/// @param[out] names the names, which the inputs view
/// @return what a walk needs, SDL_RENDER_DRIVER's list where it is set
render_policy::CreationInputs driver_inputs(DriverNames& names) {
    const int driver_count = SDL_GetNumRenderDrivers();
    for (int index = 0; index < driver_count; ++index)
        if (const char* name = SDL_GetRenderDriver(index); name != nullptr)
            names.sdl_names.emplace_back(name);
    const char* named = SDL_GetHint(SDL_HINT_RENDER_DRIVER);
    const std::string_view list = named != nullptr ? std::string_view(named) : std::string_view();
    for (std::size_t start = 0; start < list.size();) {
        const std::size_t comma = std::min(list.find(',', start), list.size());
        if (comma > start)
            names.environment_names.emplace_back(list.substr(start, comma - start));
        start = comma + 1;
    }
    names.sdl_order.assign(names.sdl_names.begin(), names.sdl_names.end());
    names.environment_order.assign(names.environment_names.begin(), names.environment_names.end());
    const char* video_driver = SDL_GetCurrentVideoDriver();
    render_policy::CreationInputs inputs;
    inputs.sdl_order = names.sdl_order;
    inputs.environment_order = names.environment_order;
    // phase 1: the records fill inputs.failed_drivers here; until then the
    // walk skips none and never starts again from the top.
    inputs.render_driver_named = !list.empty();
    inputs.native_window_framebuffer =
        render_probe::native_window_framebuffer(video_driver != nullptr ? video_driver : "");
    inputs.hint_takes_list = framebuffer_hint_takes_list();
    return inputs;
}

/// The hooks that make SDL's renderer for a walk.
///
/// @param creation what the hooks act on
/// @return the hooks
CreationHooks sdl_creation_hooks(SdlCreation& creation) {
    CreationHooks hooks;
    hooks.context = &creation;
    hooks.set_framebuffer_hint = set_sdl_framebuffer_hint;
    hooks.create = create_sdl_renderer;
    hooks.log = log_graphics_line;
    return hooks;
}

/// The reason a rebuild gives after a lost device take_event saw.
constexpr std::string_view device_lost_reason = "the graphics device was lost";

} // namespace

RendererHost::~RendererHost() {
    destroy();
}

void RendererHost::create(SDL_Window* window, const RenderFaultHooks& faults) {
    destroy();
    window_ = window;
    faults_ = faults;
    DriverNames names;
    const render_policy::CreationInputs inputs = driver_inputs(names);
    named_ = inputs.render_driver_named;
    SdlCreation creation;
    creation.window = window;
    creation.faults = &faults_;
    CreationOutcome outcome = walk_render_drivers(inputs, sdl_creation_hooks(creation));
    attempts_ = std::move(outcome.attempts);
    if (!outcome.created)
        throw std::runtime_error(outcome.error);
    renderer_ = creation.renderer;
    take_renderer();
}

void RendererHost::rebuild(std::string_view reason) {
    const std::string failed = facts_.renderer;
    std::cout << rebuild_log_line(failed, reason) << '\n' << std::flush;
    // phase 1: the failure is a strike against the driver, which becomes a
    // record when the same fails in the next run on it.
    destroy();
    lost_noted_ = false;
    DriverNames names;
    const render_policy::CreationInputs inputs = driver_inputs(names);
    SdlCreation creation;
    creation.window = window_;
    creation.faults = &faults_;
    CreationOutcome outcome = walk_rebuild_drivers(inputs, failed, sdl_creation_hooks(creation));
    attempts_ = std::move(outcome.attempts);
    if (!outcome.created)
        throw std::runtime_error(outcome.error);
    renderer_ = creation.renderer;
    take_renderer();
}

void RendererHost::take_renderer() {
    facts_ = report_game_renderer(renderer_);
    oa::base::float_precision::restore_program_float_control();
    layer_formats_ = render_policy::layer_formats(
        facts_.renderer == render_probe::software_renderer, rgb565_window(), named_
    );
}

bool RendererHost::rgb565_window() const {
    if (faults_.rgb565_window != nullptr)
        return faults_.rgb565_window(faults_.context);
    return SDL_GetWindowPixelFormat(window_) == SDL_PIXELFORMAT_RGB565;
}

bool RendererHost::take_event(const SDL_Event& event) {
    switch (event.type) {
    case SDL_EVENT_RENDER_DEVICE_LOST:
        if (renderer_ != nullptr && event.render.windowID == SDL_GetWindowID(window_))
            lost_noted_ = true;
        return true;
    case SDL_EVENT_RENDER_TARGETS_RESET:
    case SDL_EVENT_RENDER_DEVICE_RESET:
        return true;
    default:
        return false;
    }
}

void RendererHost::service() {
    if (lost_noted_ && renderer_ != nullptr)
        rebuild(device_lost_reason);
}

void RendererHost::destroy() noexcept {
    if (renderer_ != nullptr)
        SDL_DestroyRenderer(renderer_);
    renderer_ = nullptr;
    facts_ = {};
}

SDL_Renderer* RendererHost::renderer() const noexcept {
    return renderer_;
}

const render_probe::AdapterFacts& RendererHost::facts() const noexcept {
    return facts_;
}

std::span<const CreationAttempt> RendererHost::attempts() const noexcept {
    return attempts_;
}

bool RendererHost::named() const noexcept {
    return named_;
}

const render_policy::LayerFormats& RendererHost::layer_formats() const noexcept {
    return layer_formats_;
}

render_policy::LayerFormat RendererHost::opaque_format() const {
    if (facts_.renderer != render_probe::software_renderer)
        return layer_formats_.opaque;
    return render_policy::layer_formats(true, rgb565_window(), named_).opaque;
}

uint32_t RendererHost::texture_limit() const noexcept {
    return faults_.texture_limit > 0 ? faults_.texture_limit : corrected_texture_limit(facts_);
}

render_probe::DeviceState RendererHost::device_state() const {
    if (faults_.device_state != nullptr)
        return faults_.device_state(faults_.context);
    return render_probe::device_state(renderer_);
}

RenderFaultHooks& RendererHost::faults() noexcept {
    return faults_;
}

} // namespace oa::app
