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
//
// Once the renderer is made, the start decides the tier its first frame is
// drawn in from the command line, the setting, the environment, the
// machine's memory and what the probe found of the renderer, and logs it
// in the start-up line. Where the tier could be accelerated it first runs
// the start-up function test, which draws known patterns into render
// targets as the accelerated tier draws and reads them back
// (run_function_test). The facts the tier is decided from stay with the
// renderer for the runtime to keep up to date (RendererHost::tier_inputs).
#pragma once

#include "oa/app/render_policy.hpp"
#include "oa/platform/render_probe.hpp"

#include <cstdint>
#include <optional>
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

/// What a test forces the start-up function test to draw wrongly, as a
/// graphics card that ignored a scale mode would; a player's run leaves
/// both false.
struct FunctionTestFaults {
    /// (b) draws its reduction by half NEAREST in place of LINEAR.
    bool half_nearest{};
    /// (d) draws its reduction at 0.75 NEAREST in place of LINEAR.
    bool pattern_nearest{};
};

/// What --check-renderer-ladder and the checks force of the renderer; a
/// player's run leaves every member empty.
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
    /// What the start-up function test draws wrongly (run_function_test).
    FunctionTestFaults function_test{};
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

/// The most a channel of the start-up function test's LINEAR reduction by
/// half may differ from the average of the texels it covers: the graphics
/// card weighs neighbours to its own precision and rounds its own way.
inline constexpr int function_test_half_most_difference = 2;
/// The most a channel of the known pattern may differ from what the
/// processor computes for it after its two reductions. SDL's software
/// renderer weighs two texels in 128ths and truncates after each of its
/// passes, where the reference weighs exactly and rounds once, which reads
/// the pattern back up to 3 from it at a few texels.
inline constexpr int function_test_pattern_most_difference = 3;
/// The most the mean difference of the known pattern's channels may be.
inline constexpr double function_test_most_mean_difference = 0.5;

/// What the start-up function test found of a renderer.
struct FunctionTestResult {
    /// A render target read back the colour it was cleared to, a LINEAR
    /// reduction by half read back the average of the texels it covers,
    /// and the known pattern drawn as the accelerated tier draws read back
    /// as the processor computes it.
    bool passed{};
    bool pixelart{};       ///< the renderer's pixel-art scale mode works
    std::string failure{}; ///< what failed first; empty when it passed
};

/// Runs the start-up function test on a renderer, a few milliseconds of
/// work that only a tier that could be accelerated asks for:
/// (a) a 4x4 ARGB8888 render target is cleared and one pixel read back;
/// (b) a 4x4 texture is drawn LINEAR into a 2x2 target, which must read back
/// the average of each 2x2 block within function_test_half_most_difference;
/// (c) whether the pixel-art scale mode works (probe_pixelart);
/// (d) a seeded 32x32 ARGB8888 texture, drawn with no blending through a
/// source rectangle NEAREST at 2x into a 64x64 target, that target LINEAR
/// at 0.75 into a 48x48 target, and a 48x48 overlay, transparent but for an
/// opaque square, blended over it, must read back as the sharp-bilinear
/// reference at 1.5 and then the overlay rule
/// (oa::present::world_renderer::sharp_bilinear_rgb24, overlay_rgb24)
/// within function_test_pattern_most_difference and, on the mean,
/// function_test_most_mean_difference. A failure of (a), (b) or (d), or
/// of any SDL call they make, fails the test; (c) only chooses the filter.
/// Its textures and targets are destroyed and the render target set back
/// to the window when it ends.
///
/// @param renderer the renderer
/// @param faults what a test forces it to draw wrongly; empty in a
///     player's run
/// @return what it found
[[nodiscard]] FunctionTestResult
run_function_test(SDL_Renderer* renderer, const FunctionTestFaults& faults = {});

/// What the command line, the settings and the renderer records ask of the
/// window's pixel density as the game starts, before the window opens.
struct DensityRequest {
    /// --hardware-acceleration (true) or --no-hardware-acceleration (false);
    /// empty for neither.
    std::optional<bool> flag{};
    bool asked{};      ///< --native-density, which only the render tiers check passes
    bool setting_on{}; ///< the Hardware acceleration setting read before the window opens
    bool unattended{}; ///< a check, a benchmark or another scripted run
    bool capture{};    ///< the run captures video (--capture-video)
    /// The driver the native-density record names under the running
    /// engine's version (renderer_state::native_density_driver); empty
    /// when there is none, as while the start reads no records before the
    /// window opens.
    std::string record_driver{};
    /// The step-down rung remembered for that driver; none when none is.
    std::optional<render_policy::LadderState> remembered{};
};

/// Decides whether the game's window opens at the display's own pixel
/// density (render_policy::decide_native_density), from the request and
/// what the machine reports: its physical memory, the scene budget it
/// starts at with the record's driver, SDL_RENDER_DRIVER and the video
/// driver. The class of no machine has been measured at native density yet
/// (render_policy::native_density_measured), so only --native-density opens
/// the window at native density. A window that does is logged.
///
/// SDL's video must be started, and the window not yet made.
///
/// @param request what the command line, the settings and the records ask
/// @return whether the window opens at native density, and why
[[nodiscard]] render_policy::DensityDecision decide_window_density(const DensityRequest& request);

/// What the command line and the settings ask of the tier as the game
/// starts.
struct TierRequest {
    /// --hardware-acceleration (true) or --no-hardware-acceleration (false);
    /// empty for neither.
    std::optional<bool> flag{};
    bool force_capable{};       ///< --force-capable, which only a check passes
    bool players_own_profile{}; ///< no --preferences-file was named
    bool setting_on{};          ///< the Hardware acceleration setting the run starts with
};

/// The game's renderer, made for its window and kept for the run, with what
/// the probe found of it and the attempts that made it, and the facts the
/// tier each frame is drawn in is decided from. HostDisplay owns one; the
/// runtime borrows it.
class RendererHost {
  public:

    RendererHost() = default;
    RendererHost(const RendererHost&) = delete;
    RendererHost& operator=(const RendererHost&) = delete;

    /// Destroys the renderer (destroy).
    ~RendererHost();

    /// Makes the renderer of a window and describes it
    /// (describe_game_renderer), on Windows before Vista with only direct3d
    /// capable (oa::platform::running_on_windows_before_vista);
    /// decide_start_tier logs it. With SDL_RENDER_DRIVER set, by SDL's own
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
    /// logs what was made (report_game_renderer), on the standard tier
    /// since the driver failed: destroys it, since a window holds one
    /// renderer, then walks the drivers after the one that failed
    /// (walk_rebuild_drivers), under SDL_RENDER_DRIVER those its list
    /// names, ending with SDL's software renderer. Then the floating-point
    /// settings the game started with are put back, should a driver have
    /// changed them. The tier's facts take the new renderer's capability,
    /// its function test is to run again, and acceleration is dropped
    /// for the run (render_policy::Drop::driver_failure) unless it was
    /// dropped already. Every texture made on the renderer must be
    /// destroyed first.
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

    /// Decides the tier the first frame is drawn in and logs the start-up
    /// line (graphics_log_line, with tier_description).
    ///
    /// Fills the tier's facts: the request, SDL_RENDER_DRIVER as the start
    /// saw it, whether the video driver draws no window, the machine's
    /// physical memory as the system reports it
    /// (oa::platform::sample_system_memory), and the capability probe items
    /// 1 to 3 found (render_policy::assess_renderer), with the adapter read
    /// under SDL_RENDER_DRIVER too when --hardware-acceleration or
    /// --force-capable asks for more than SDL's own start; and the facts the
    /// starting rung is sized from. On the player's own profile the
    /// function test waits for the player, unless a flag asks for it
    /// (render_policy::start_function_test). Where the tier could be
    /// accelerated but for the function test, and so never under 2 GiB,
    /// runs it first (render_policy::step_tier, test_function).
    ///
    /// @param request what the command line and the settings ask
    void decide_start_tier(const TierRequest& request);

    /// Runs the start-up function test on the renderer (run_function_test,
    /// with the faults' function_test), puts back the floating-point
    /// settings the game started with, and keeps what it found: the
    /// function test passed or failed in the tier's facts, logging a
    /// failure, and whether the pixel-art scale mode works for the starting
    /// rung.
    void test_function();

    /// Returns the hooks through which render_policy::step_tier runs the
    /// start-up function test on this renderer (test_function).
    ///
    /// @return the hooks, valid while the host lives
    [[nodiscard]] render_policy::FunctionTestHooks function_test_hooks() noexcept;

    /// Returns the facts the tier is decided from: those of the start, the
    /// function test's result, and what the runtime keeps up to date (the
    /// setting, a drop, a lost device, the director and a shared game).
    ///
    /// @return the facts
    [[nodiscard]] render_policy::TierInputs& tier_inputs() noexcept;

    /// Returns the facts the tier is decided from.
    ///
    /// @return the facts
    [[nodiscard]] const render_policy::TierInputs& tier_inputs() const noexcept;

    /// Returns the rung the accelerated tier starts at on this machine and
    /// renderer (render_policy::start_rung).
    ///
    /// @return the rung
    [[nodiscard]] render_policy::LadderState start_rung() const noexcept;

  private:

    /// Notes what was made: the facts, with the adapter read as
    /// adapter_asked_ says, the capability, the layers' formats and the
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
    render_policy::TierInputs tier_{};     ///< what the tier is decided from
    render_policy::StartInputs machine_{}; ///< what the starting rung is sized from
    bool named_{};                         ///< SDL_RENDER_DRIVER named the drivers at the start
    /// A flag asks for the adapter under SDL_RENDER_DRIVER, which otherwise
    /// reads none.
    bool adapter_asked_{};
    bool lost_noted_{}; ///< take_event saw the device lost; service rebuilds
};

} // namespace oa::app
