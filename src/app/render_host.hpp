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
// the render policy's (render_policy.hpp); this is the part that acts. When a
// renderer fails while the game runs, it is made again the same way from
// the driver after the one that failed (RendererHost::rebuild).
#pragma once

#include "oa/app/render_policy.hpp"
#include "oa/platform/render_probe.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

struct SDL_Renderer;
struct SDL_Window;
union SDL_Event;

namespace oa::app {

/// What the error of a start that made no renderer begins with.
inline constexpr std::string_view renderer_creation_error = "SDL_CreateRenderer: ";
/// The reason given for a refusal that came with none.
inline constexpr std::string_view unexplained_refusal = "no reason given";
/// The reason given when there was no render driver to try.
inline constexpr std::string_view no_render_driver = "no render driver is available";

/// The reason a render driver gives when --render-fault create refuses it.
inline constexpr std::string_view refused_by_fault = "refused by --render-fault create";

/// What --check-renderer-ladder forces of the renderer; a player's run
/// leaves every member empty.
struct RenderFaultHooks {
    void* context{};
    /// Says whether a render driver refuses to start, as if it had failed;
    /// null refuses none.
    bool (*refuse_driver)(void* context, std::string_view driver){};
    /// Answers in place of the renderer's device whether it can draw; null
    /// asks the device (oa::platform::render_probe::device_state).
    oa::platform::render_probe::DeviceState (*device_state)(void* context){};
    /// The texture limit in place of the renderer's, in texels; 0 keeps the
    /// renderer's.
    uint32_t texture_limit{};
    /// Answers in place of the window whether its pixels are 16-bit RGB565;
    /// null asks the window.
    bool (*rgb565_window)(void* context){};
};

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

/// Walks the render drivers of a rebuild, as the render policy plans it
/// (start_rebuild and next_attempt), as walk_render_drivers walks those of
/// a start: the drivers after the one that failed, in SDL's order, or in
/// SDL_RENDER_DRIVER's list where it is set, then SDL's software renderer,
/// with the framebuffer hint set before it; and where nothing of that could
/// present, SDL's whole order again from the top.
///
/// @param inputs the drivers in SDL's order, SDL_RENDER_DRIVER's list, and
///     what the video driver and SDL's hint can take
/// @param failed_driver the driver that failed
/// @param hooks what sets the hint, makes the renderer and logs
/// @return every attempt, whether the last made the renderer, and the
///     error when none did
[[nodiscard]] CreationOutcome walk_rebuild_drivers(
    const render_policy::CreationInputs& inputs,
    std::string_view failed_driver,
    const CreationHooks& hooks
);

/// Returns the line logged when a renderer fails while the game runs and
/// another is made: "open-annihilation: graphics: <driver> failed:
/// <reason>; making another renderer".
///
/// @param driver SDL's name for the render driver that failed
/// @param reason what failed
/// @return the line, without its line break
[[nodiscard]] std::string rebuild_log_line(std::string_view driver, std::string_view reason);

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
    /// @param faults what --check-renderer-ladder forces; empty in a
    ///     player's run. They are kept for the run (faults).
    void create(SDL_Window* window, const RenderFaultHooks& faults = {});

    /// Makes the renderer again after it failed while the game ran, and
    /// logs what was made (report_game_renderer): destroys it, since a
    /// window holds one renderer, then walks the drivers after the one that
    /// failed (walk_rebuild_drivers), under SDL_RENDER_DRIVER those its list
    /// names, ending with SDL's software renderer. Then the floating-point
    /// settings the game started with are put back, should a driver have
    /// changed them. Every texture made on the renderer must be destroyed
    /// first.
    ///
    /// Throws std::runtime_error, beginning renderer_creation_error, when
    /// no driver starts: the run ends, as a start does.
    ///
    /// @param reason what failed, for the log
    void rebuild(std::string_view reason);

    /// Takes a render event that comes while no runtime handles events, as
    /// while the intro movies play: a lost device is noted for service; a
    /// reset needs nothing, since what draws then makes its own textures
    /// again.
    ///
    /// @param event any event
    /// @return true for the three render events, which nothing else needs
    bool take_event(const SDL_Event& event);

    /// Makes the renderer again (rebuild) when take_event noted a lost
    /// device, and does nothing otherwise.
    ///
    /// Throws std::runtime_error when no driver starts.
    void service();

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

    /// Says whether SDL_RENDER_DRIVER named the drivers at the start.
    ///
    /// @return true when it did
    [[nodiscard]] bool named() const noexcept;

    /// Returns the texture formats of the opaque layers on this renderer
    /// (render_policy::layer_formats), with its window's pixels as they were
    /// when it was made. The match's layers follow the window as it is now
    /// (opaque_format).
    ///
    /// @return the formats
    [[nodiscard]] const render_policy::LayerFormats& layer_formats() const noexcept;

    /// Returns the texture format of the match's opaque layers now. On SDL's
    /// software renderer it follows the window's pixels, which a display
    /// mode of another depth or a move to another display can change while
    /// the game runs, so the window is asked at each call; on every other
    /// renderer it is layer_formats' and fixed for the renderer.
    ///
    /// @return the format
    [[nodiscard]] render_policy::LayerFormat opaque_format() const;

    /// Returns the largest texture side the game makes: the faults' limit
    /// where it is set, otherwise the renderer's corrected one.
    ///
    /// @return texels; 0 for no limit
    [[nodiscard]] uint32_t texture_limit() const noexcept;

    /// Asks the renderer's device whether it can draw, or the faults where
    /// they answer.
    ///
    /// @return the device's state; unknown on a renderer whose device cannot
    ///     say
    [[nodiscard]] oa::platform::render_probe::DeviceState device_state() const;

    /// Returns what --check-renderer-ladder forces.
    ///
    /// @return the faults, which the check may change
    [[nodiscard]] RenderFaultHooks& faults() noexcept;

  private:

    /// Notes what was made: the facts, the layers' formats and the
    /// floating-point settings put back.
    void take_renderer();

    /// Says whether the window's pixels are 16-bit RGB565, or the faults'
    /// answer where they give one.
    ///
    /// @return true for RGB565
    [[nodiscard]] bool rgb565_window() const;

    SDL_Window* window_{};
    SDL_Renderer* renderer_{};
    oa::platform::render_probe::AdapterFacts facts_{};
    std::vector<CreationAttempt> attempts_{};
    RenderFaultHooks faults_{};
    render_policy::LayerFormats layer_formats_{};
    bool named_{};      ///< SDL_RENDER_DRIVER named the drivers at the start
    bool lost_noted_{}; ///< take_event saw the device lost; service rebuilds
};

} // namespace oa::app
