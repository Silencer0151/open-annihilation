// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "render_host.hpp"

#include "graphics_report.hpp"
#include "oa/base/float_precision.hpp"

#include <SDL3/SDL.h>

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

CreationOutcome
walk_render_drivers(const render_policy::CreationInputs& inputs, const CreationHooks& hooks) {
    CreationOutcome outcome;
    render_policy::CreationWalk walk = render_policy::start_creation(inputs);
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

namespace {

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
    SDL_Window* window{};     ///< the window the renderer is made for
    SDL_Renderer* renderer{}; ///< the renderer made; null until one is
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

} // namespace

RendererHost::~RendererHost() {
    destroy();
}

void RendererHost::create(SDL_Window* window) {
    destroy();
    const char* named = SDL_GetHint(SDL_HINT_RENDER_DRIVER);
    std::vector<std::string> names;
    const int driver_count = SDL_GetNumRenderDrivers();
    for (int index = 0; index < driver_count; ++index)
        if (const char* name = SDL_GetRenderDriver(index); name != nullptr)
            names.emplace_back(name);
    const std::vector<std::string_view> order(names.begin(), names.end());
    const char* video_driver = SDL_GetCurrentVideoDriver();

    render_policy::CreationInputs inputs;
    inputs.sdl_order = order;
    // phase 1: the records fill inputs.failed_drivers here; until then the
    // walk skips none and never starts again from the top.
    inputs.render_driver_named = named != nullptr && *named != '\0';
    inputs.native_window_framebuffer =
        render_probe::native_window_framebuffer(video_driver != nullptr ? video_driver : "");
    inputs.hint_takes_list = framebuffer_hint_takes_list();

    SdlCreation creation;
    creation.window = window;
    CreationHooks hooks;
    hooks.context = &creation;
    hooks.set_framebuffer_hint = set_sdl_framebuffer_hint;
    hooks.create = create_sdl_renderer;
    hooks.log = log_graphics_line;
    CreationOutcome outcome = walk_render_drivers(inputs, hooks);
    attempts_ = std::move(outcome.attempts);
    if (!outcome.created)
        throw std::runtime_error(outcome.error);
    renderer_ = creation.renderer;
    facts_ = report_game_renderer(renderer_);
    oa::base::float_precision::restore_program_float_control();
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

} // namespace oa::app
