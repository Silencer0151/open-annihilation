// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The game's renderer and how it is made. Start-up walks SDL's render
// drivers one at a time, in SDL's own order, and keeps the first that
// starts, logging each refusal; with every driver working that is the
// driver SDL's own choice would make. Before SDL's software renderer, the
// last of the walk, it sets the framebuffer hint, so that the software
// renderer presents through the window's own framebuffer where there is
// one, and otherwise through the hardware drivers not recorded as failed.
// Under SDL_RENDER_DRIVER the start is SDL's own call, which tries only the
// drivers the variable names: no walk, no hint, and a failure ends the run.
// The order of the walk, the hint's value and when the walk starts over are
// the render policy's (render_policy.hpp); this is the part that acts.
#pragma once

#include "oa/app/render_policy.hpp"
#include "oa/platform/render_probe.hpp"

#include <span>
#include <string>
#include <string_view>
#include <vector>

struct SDL_Renderer;
struct SDL_Window;

namespace oa::app {

/// What the error of a start that made no renderer begins with.
inline constexpr std::string_view renderer_creation_error = "SDL_CreateRenderer: ";
/// The reason given for a refusal that came with none.
inline constexpr std::string_view unexplained_refusal = "no reason given";
/// The reason given when there was no render driver to try.
inline constexpr std::string_view no_render_driver = "no render driver is available";

/// One attempt of a walk of the render drivers.
struct CreationAttempt {
    /// The render driver tried; empty for SDL's own choice, under SDL_RENDER_DRIVER.
    std::string driver{};
    bool created{};      ///< the driver made the renderer
    std::string error{}; ///< why it refused; empty when it made the renderer
};

/// What a walk of the render drivers calls to act.
struct CreationHooks {
    void* context{};
    /// Sets the framebuffer hint to a value; returns false, with the reason
    /// in error, when it was not taken, as when an environment variable
    /// takes priority. Null takes nothing.
    bool (*set_framebuffer_hint)(void* context, const std::string& value, std::string& error){};
    /// Creates the renderer of a render driver, or SDL's own choice for an
    /// empty name; returns false, with the reason in error, when it refused.
    /// Null refuses every driver.
    bool (*create)(void* context, const std::string& driver, std::string& error){};
    /// Logs one line, without its line break. Null logs nothing.
    void (*log)(void* context, const std::string& line){};
};

/// What a walk of the render drivers did.
struct CreationOutcome {
    std::vector<CreationAttempt> attempts{}; ///< every attempt, in the order made
    bool created{};                          ///< the last attempt made the renderer
    /// When nothing was made: renderer_creation_error and the last refusal's
    /// reason, or no_render_driver when nothing was tried.
    std::string error{};
};

/// Returns the line logged when a render driver refuses to make the
/// renderer: "open-annihilation: graphics: renderer <driver> refused:
/// <reason>".
///
/// @param driver SDL's name for the render driver
/// @param reason why it refused
/// @return the line, without its line break
[[nodiscard]] std::string refusal_log_line(std::string_view driver, std::string_view reason);

/// Returns the line logged when the framebuffer hint was not taken.
///
/// @param value the hint's value
/// @param reason why it was not taken
/// @return the line, without its line break
[[nodiscard]] std::string
framebuffer_hint_log_line(std::string_view value, std::string_view reason);

/// Returns the line logged when no driver left could present and the walk
/// starts again from the top of SDL's order, with the drivers recorded as
/// failed tried too.
///
/// @return the line, without its line break
[[nodiscard]] std::string second_walk_log_line();

/// Walks the render drivers as the render policy plans it (start_creation
/// and next_attempt) until one makes the renderer: one driver at a time in
/// SDL's order, or SDL's own call once under SDL_RENDER_DRIVER. Each driver
/// that refuses is logged (refusal_log_line) and the walk goes on; SDL's
/// own call is not, since its failure ends the run with SDL's own words.
/// Before an attempt the policy marks, the framebuffer hint is set first;
/// a hint that is not taken is logged (framebuffer_hint_log_line), and the
/// walk goes on. The first attempt of a walk that starts again with the
/// records ignored is preceded by second_walk_log_line.
///
/// @param inputs the drivers in SDL's order, those recorded as failed, and
///     what the video driver and SDL's hint can take
/// @param hooks what sets the hint, makes the renderer and logs
/// @return every attempt, whether the last made the renderer, and the
///     error when none did
[[nodiscard]] CreationOutcome
walk_render_drivers(const render_policy::CreationInputs& inputs, const CreationHooks& hooks);

/// The game's renderer, made for its window and kept for the run, with what
/// the probe found of it and the attempts that made it. HostDisplay owns
/// one; the runtime borrows it.
class RendererHost {
  public:

    RendererHost() = default;
    RendererHost(const RendererHost&) = delete;
    RendererHost& operator=(const RendererHost&) = delete;

    /// Destroys the renderer (destroy).
    ~RendererHost();

    /// Makes the renderer of a window and logs what was made
    /// (report_game_renderer). With SDL_RENDER_DRIVER set, by SDL's own
    /// call, which tries only the drivers it names. Otherwise by walking
    /// SDL's render drivers (walk_render_drivers): no driver is recorded as
    /// failed yet, so the walk tries every driver in SDL's order and stops
    /// at the first that starts. The framebuffer hint is set before SDL's
    /// software renderer only when an earlier driver refused: "0" where the
    /// window has a framebuffer of its own
    /// (oa::platform::render_probe::native_window_framebuffer), otherwise
    /// the hardware drivers in SDL's order, or the first of them alone when
    /// SDL's headers or library are older than 3.4. Then the floating-point
    /// settings the game started with are put back, should the driver or
    /// the probe's reading of it have changed them.
    ///
    /// Throws std::runtime_error, beginning renderer_creation_error, when
    /// no renderer was made.
    ///
    /// @param window the game's window, which has no renderer yet
    void create(SDL_Window* window);

    /// Destroys the renderer, if there is one; the window stays.
    void destroy() noexcept;

    /// Returns the renderer.
    ///
    /// @return the renderer; null before create, after destroy or in a
    ///     headless run
    [[nodiscard]] SDL_Renderer* renderer() const noexcept;

    /// Returns what the probe found of the renderer.
    ///
    /// @return the facts; empty while there is no renderer
    [[nodiscard]] const oa::platform::render_probe::AdapterFacts& facts() const noexcept;

    /// Returns the attempts of the walk that made the renderer, or of the
    /// last walk, which made none.
    ///
    /// @return the attempts, in the order made
    [[nodiscard]] std::span<const CreationAttempt> attempts() const noexcept;

  private:

    SDL_Renderer* renderer_{};
    oa::platform::render_probe::AdapterFacts facts_{};
    std::vector<CreationAttempt> attempts_{};
};

} // namespace oa::app
