// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The render policy: the decisions about the renderer the game presents
// through and the tier each frame is drawn in, as pure functions of plain
// facts. Which renderer to create, and in what order to try SDL's drivers;
// whether a renderer can be accelerated; whether a frame is drawn in the
// standard tier (today's renderer) or the accelerated one; what a crash or a
// failure left behind counts for at the next start; where a machine starts
// on the step-down ladder and when slow frames move it down; how the chrome
// is filtered; and how a texture larger than the renderer allows is split
// into tiles. Nothing here calls SDL, reads a file or a clock, or names a
// graphics interface: the host hands it what the renderer, the machine and
// the frame pacer report, and acts on what it hands back.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace oa::app::render_policy {

// ---------------------------------------------------------------------------
// Tiers and the command line

/// How a frame is drawn and presented.
enum class RenderTier : uint8_t {
    standard,    ///< today's renderer: the processor draws and scales everything
    accelerated, ///< the processor draws; the graphics card scales and composes
};

/// What the command line asks of hardware acceleration.
enum class AccelerationFlag : uint8_t {
    none, ///< neither flag: the setting decides
    on,   ///< --hardware-acceleration
    off,  ///< --no-hardware-acceleration
};

// ---------------------------------------------------------------------------
// The renderer's facts and its capability

/// The name SDL gives its own software renderer, which is never recorded as
/// failed, never skipped and never capable.
inline constexpr std::string_view software_driver = "software";

/// The texture size a renderer reports when it sets no limit.
inline constexpr uint32_t unlimited_texture_size = 0;

/// The smallest texture limit, in texels, at which a renderer can be
/// accelerated.
inline constexpr uint32_t smallest_capable_texture_size = 1024;

/// The texture limit taken, in texels, for a driver that reports a fixed
/// limit when the device's own limit is not known.
inline constexpr uint32_t unconfirmed_texture_size_cap = 8192;

/// Where a driver's true texture limit comes from.
enum class TextureLimitSource : uint8_t {
    reported,     ///< the limit the renderer reports is the device's own
    fixed_report, ///< the renderer reports a fixed limit; the device's own is read apart
};

/// What the policy needs to know of a render driver. The platform's render
/// probe fills it from the driver's name, so that this code names no
/// graphics interface.
struct DriverTraits {
    bool software{};             ///< SDL's own software renderer
    bool capable_before_vista{}; ///< the one driver that can be accelerated on Windows before Vista
    /// An adapter that cannot be read leaves the renderer not capable,
    /// since a software rasteriser cannot be ruled out.
    bool adapter_required{};
    TextureLimitSource texture_limit_source{TextureLimitSource::reported};
    /// Its device is lost in ordinary use (switching away from exclusive
    /// full screen, locking the screen), so a failure while running is never
    /// recorded against it.
    bool loses_device_in_normal_use{};
    /// The blend is never used on it, since its textures would take the
    /// blend's memory twice over.
    bool blend_excluded{};
};

/// Returns the texture limit a renderer really has.
///
/// @param driver the driver's traits
/// @param reported the limit the renderer reports, in texels; 0 for none
/// @param device the device's own limit when the probe read it, in texels;
///     0 when it could not
/// @return the limit in texels, unlimited_texture_size (0) for none: the
///     reported limit; for a TextureLimitSource::fixed_report driver the
///     device's own limit, no larger than the reported one, when the probe
///     read it, otherwise the reported limit at most
///     unconfirmed_texture_size_cap
[[nodiscard]] uint32_t
texture_limit(const DriverTraits& driver, uint32_t reported, uint32_t device) noexcept;

/// What probe items 1 to 3 learned of a renderer: its driver, its texture
/// limit and its adapter.
struct RendererFacts {
    DriverTraits driver{};
    uint32_t max_texture_size{}; ///< the corrected limit (texture_limit) in texels; 0 for none
    bool adapter_known{};        ///< the probe read the adapter
    bool software_rasteriser{};  ///< the adapter rasterises on the processor
    bool virtual_adapter{};      ///< the adapter is a virtual machine's display adapter
    bool under_wine{}; ///< the game runs under Wine, whose graphics run through another interface
};

/// Whether a renderer can be accelerated, or why not. The order of the
/// enumerators is the order assess_renderer tests them in.
enum class Capability : uint8_t {
    capable,
    software_renderer,   ///< SDL's software renderer
    before_vista_driver, ///< Windows before Vista, on a driver other than the one allowed there
    small_texture_limit, ///< a texture limit under smallest_capable_texture_size
    under_wine,          ///< under Wine
    software_rasteriser, ///< an adapter that rasterises on the processor
    virtual_adapter,     ///< a virtual machine's display adapter
    unknown_adapter,     ///< an adapter that cannot be read, on a driver that needs it read
};

/// Decides whether a renderer can be accelerated, from probe items 1 to 3.
///
/// @param renderer what the probe read
/// @param legacy_windows the game runs on Windows before Vista
/// @param accept_virtual_adapter --accept-virtual-adapter was given, which
///     lifts only Capability::virtual_adapter
/// @return Capability::capable, or the first reason in the enumeration's
///     order that the renderer is not
[[nodiscard]] Capability assess_renderer(
    const RendererFacts& renderer, bool legacy_windows, bool accept_virtual_adapter
) noexcept;

// ---------------------------------------------------------------------------
// Choosing the tier

/// What the start-up function test did.
enum class FunctionTest : uint8_t {
    not_run,         ///< it has not run in this run
    passed,          ///< it drew the known pattern right
    failed,          ///< it drew wrongly or a call failed
    trial_unwritten, ///< it was skipped because its trial record could not be written
};

/// Why acceleration was dropped for the rest of the run.
enum class Drop : uint8_t {
    none,
    /// An accelerated-only call failed, the device was lost or reset, or a
    /// present error rebuilt the renderer.
    driver_failure,
    engine_fault, ///< presenting failed with a render target still set or an invalid renderer
    memory,       ///< the memory guard
    stall,        ///< the present stalled in the accelerated tier
    slow_frames,  ///< the step-down reached its last rung
    path_trial_unwritten, ///< an accelerated path's trial record could not be written
};

/// What kind of match is under way, from the moment its loading screen
/// begins.
enum class MatchKind : uint8_t {
    none,        ///< no match, or a match played alone
    shared_game, ///< a match played with other machines
    replay,      ///< a recorded game played back
};

/// The tier's state across a shared game or a replay: whatever leaves the
/// accelerated tier applies at once, and whatever would start it waits for
/// the match to end.
struct SharedMatchGate {
    MatchKind kind{MatchKind::none};
    /// The tier was accelerated as the match's loading screen began, with
    /// the function test passed, and nothing has stopped it since.
    bool accelerated{};
};

/// Opens the gate as a match's loading screen begins.
///
/// @param[out] gate the run's gate
/// @param kind the match's kind; MatchKind::none closes nothing
/// @param accelerated_now the tier decided for the loading screen's first
///     frame is accelerated
void begin_match(SharedMatchGate& gate, MatchKind kind, bool accelerated_now) noexcept;

/// Notes a frame's decision while a match is under way: in a shared game
/// or a replay, any decision for the standard tier, other than a lost
/// device that a reset brings back, keeps the tier standard until the match
/// ends.
///
/// @param[in,out] gate the run's gate
/// @param tier the frame's tier
/// @param device_lost the frame is standard only because the device is lost
void note_match_frame(SharedMatchGate& gate, RenderTier tier, bool device_lost) noexcept;

/// Keeps the tier standard until the match ends, as after a device reset
/// destroyed the accelerated tier's textures during a shared game.
///
/// @param[in,out] gate the run's gate
void stop_until_match_end(SharedMatchGate& gate) noexcept;

/// Closes the gate as the match ends.
///
/// @param[out] gate the run's gate
void end_match(SharedMatchGate& gate) noexcept;

/// Tells whether a texture or render target may be made for the first time
/// in the run now.
///
/// @param gate the run's gate
/// @param loading_screen_beginning the match's loading screen is beginning,
///     before the world is built and before the machines wait for each other
/// @return true outside a shared game or a replay, and in one only as its
///     loading screen begins
[[nodiscard]] bool
first_use_allowed(const SharedMatchGate& gate, bool loading_screen_beginning) noexcept;

/// One gibibyte, in bytes.
inline constexpr uint64_t gibibyte = uint64_t{1} << 30;

/// The least physical memory, in bytes as the system reports it, with which
/// the accelerated tier may run. A machine that reports it counts as having
/// 2 GiB: it is 1.75 GiB, since the firmware and the graphics take their
/// share of a machine's 2 GB before the system reports the rest. Under it,
/// and where the system does not report memory, every frame is drawn in the
/// standard tier whatever the flags, and the function test never runs.
inline constexpr uint64_t smallest_accelerated_memory = 7 * gibibyte / 4;

/// Everything decide_render_tier reads.
struct TierInputs {
    bool renderer{};       ///< a renderer exists (headless runs have none)
    bool director_frame{}; ///< the frame is a director render's
    /// The machine's physical memory in bytes, as the system reports it; 0
    /// when it does not say.
    uint64_t memory{};
    AccelerationFlag flag{AccelerationFlag::none};
    bool force_capable{};        ///< --force-capable, which only a check passes
    bool render_driver_named{};  ///< the SDL_RENDER_DRIVER environment variable is set
    bool virtual_video_driver{}; ///< the video driver is dummy or offscreen
    bool players_own_profile{};  ///< no --preferences-file was named
    bool setting_on{};           ///< the Hardware acceleration setting is On
    Capability capability{Capability::capable};
    FunctionTest function_test{FunctionTest::not_run};
    bool accelerated_unusable_record{}; ///< the driver has an accelerated-unusable record
    /// After a start that did not end cleanly, the records file could not
    /// be read.
    bool records_unreadable_after_unclean_start{};
    Drop drop{Drop::none};
    bool device_lost{}; ///< the device is lost until it is reset
    SharedMatchGate match{};
};

/// Why decide_render_tier chose its tier. The order of the enumerators is the
/// order it tests the conditions in.
enum class TierReason : uint8_t {
    accelerated,    ///< every condition holds
    no_renderer,    ///< a headless run
    director_frame, ///< a director render's frame
    /// Physical memory under smallest_accelerated_memory, or not reported,
    /// whatever the flags.
    memory,
    flag_off, ///< --no-hardware-acceleration
    /// SDL_RENDER_DRIVER or a dummy or offscreen video driver, with no flag
    /// that lifts it.
    environment,
    setting_off,           ///< the setting is Off and --hardware-acceleration was not given
    not_capable,           ///< probe items 1 to 3 rejected the renderer
    function_test_failed,  ///< the function test drew wrongly
    records_unreadable,    ///< the records could not be read after an unclean start
    accelerated_unusable,  ///< the driver has an accelerated-unusable record
    dropped,               ///< acceleration was dropped for the run
    device_lost,           ///< the device is lost until it is reset
    waiting_for_match_end, ///< a shared game or a replay, which the tier did not start accelerated
    trial_unwritten,       ///< the function test's trial could not be written, so it was skipped
    function_test_due, ///< every other condition holds: run the function test, then decide again
};

/// The tier for a frame and the reason for it.
struct TierDecision {
    RenderTier tier{RenderTier::standard};
    TierReason reason{TierReason::no_renderer};
};

/// Tells whether the records and the trial live on disk: only the player's
/// own profile keeps them there, and SDL_RENDER_DRIVER keeps none.
///
/// @param players_own_profile no --preferences-file was named
/// @param render_driver_named SDL_RENDER_DRIVER is set
/// @return true when the records are read and written on disk
[[nodiscard]] bool records_on_disk(bool players_own_profile, bool render_driver_named) noexcept;

/// Decides which tier draws a frame.
///
/// The accelerated tier needs every condition: a renderer, a frame that is
/// not a director render's, physical memory of at least
/// smallest_accelerated_memory as the system reports it, which no flag
/// lifts, no --no-hardware-acceleration, neither SDL_RENDER_DRIVER nor a
/// dummy or offscreen video driver unless --hardware-acceleration or
/// --force-capable lifts them, the setting On
/// or --hardware-acceleration, a capable renderer or --force-capable, a
/// function test that passed, readable records after an unclean start, no
/// accelerated-unusable record unless --hardware-acceleration, no drop, no
/// lost device, and in a shared game or a replay a tier that was
/// accelerated as its loading screen began. Where the records live in
/// memory a trial cannot fail to be written, so FunctionTest::trial_unwritten
/// counts as not run there.
///
/// @param inputs the run's and the frame's facts
/// @return the tier, and the first reason in TierReason's order that
///     decided it; TierReason::function_test_due when only the function
///     test is missing, which the host runs before it decides again
[[nodiscard]] TierDecision decide_render_tier(const TierInputs& inputs) noexcept;

/// Tells whether the function test may run now: it runs only when the tier
/// could be accelerated but for the test itself, so never under
/// smallest_accelerated_memory or with memory not reported. A test that
/// failed does not run again, nor one whose trial could not be written on
/// disk, until the host sets function_test back to FunctionTest::not_run,
/// as switching the setting Off then On or Restore defaults does.
///
/// @param inputs the run's and the frame's facts
/// @return true when decide_render_tier answers function_test_due
[[nodiscard]] bool function_test_may_run(const TierInputs& inputs) noexcept;

// ---------------------------------------------------------------------------
// Creating the renderer

/// The framebuffer hint's value on a video driver that presents SDL's
/// software renderer through the window's own framebuffer.
inline constexpr std::string_view framebuffer_hint_window = "0";

/// The drivers SDL may try and what the walk knows of them.
struct CreationInputs {
    /// SDL's drivers in its own order (SDL_GetRenderDriver).
    std::span<const std::string_view> sdl_order{};
    /// The drivers recorded failed-driver; never read for software.
    std::span<const std::string_view> failed_drivers{};
    /// SDL_RENDER_DRIVER's list, in its order, when the variable is set.
    std::span<const std::string_view> environment_order{};
    bool render_driver_named{}; ///< SDL_RENDER_DRIVER is set
    /// The video driver presents SDL's software renderer through the
    /// window's own framebuffer; otherwise it needs a driver to present it
    /// through, which the framebuffer hint names.
    bool native_window_framebuffer{};
    /// The framebuffer hint takes a comma list of drivers (SDL 3.4 and
    /// later); before that it names the first of them alone.
    bool hint_takes_list{};
};

/// What a walk is for.
enum class WalkKind : uint8_t {
    start,               ///< the start's walk of SDL's order
    rebuild,             ///< a rebuild, from the driver after the one that failed
    environment_start,   ///< SDL_RENDER_DRIVER's start: SDL's own call, once
    environment_rebuild, ///< a rebuild under SDL_RENDER_DRIVER: its later drivers, then software
};

/// Where a walk of the drivers stands.
struct CreationWalk {
    WalkKind kind{WalkKind::start};
    /// A rebuild's driver that failed, which the walk does not try again and
    /// leaves out of the hint's list.
    std::string_view failed_driver{};
    size_t position{};           ///< the next place in the order
    bool records_ignored{};      ///< the second walk, from the top with the records ignored
    bool hardware_missed{};      ///< a hardware driver refused, was skipped by a record or was lost
    bool skipped_by_record{};    ///< the walk skipped a driver because of a record
    bool software_appended{};    ///< an environment rebuild has tried software after its list
    bool attempted{};            ///< an attempt is under way: the next call means it was refused
    bool attempt_was_hardware{}; ///< that attempt was of a hardware driver
    bool finished{};             ///< nothing is left to try
};

/// What to try next.
enum class AttemptKind : uint8_t {
    driver,     ///< create the named driver
    sdl_choice, ///< SDL's own call, with no driver named (SDL_RENDER_DRIVER's start)
    none,       ///< nothing is left: the start or rebuild fails
};

/// One attempt of a walk.
struct Attempt {
    AttemptKind kind{AttemptKind::none};
    std::string_view driver{};      ///< the driver, for AttemptKind::driver
    bool set_framebuffer_hint{};    ///< set the framebuffer hint before creating it
    std::string framebuffer_hint{}; ///< the hint's value; never empty when it is set
    bool records_ignored{};         ///< the walk ignores the records for this run
};

/// Starts the walk of a start: SDL's order, or under SDL_RENDER_DRIVER
/// SDL's own call once.
///
/// @param inputs the drivers and the records
/// @return the walk, before its first attempt
[[nodiscard]] CreationWalk start_creation(const CreationInputs& inputs) noexcept;

/// Starts the walk of a rebuild after a driver failed while running: the
/// drivers after it in SDL's order, then software. Under SDL_RENDER_DRIVER
/// it is the drivers after it in the variable's list, whose names match in
/// any letter case as SDL matches them, never the failed one again, then
/// software.
///
/// @param inputs the drivers and the records
/// @param failed_driver the driver that failed; the walk keeps a view of it
/// @return the walk, before its first attempt
[[nodiscard]] CreationWalk
start_rebuild(const CreationInputs& inputs, std::string_view failed_driver) noexcept;

/// Returns the next attempt of a walk. Calling it again means the attempt
/// it returned was refused.
///
/// Drivers recorded failed-driver are skipped, but never software. Before
/// software, when a hardware driver refused, was skipped by a record or was
/// lost, the framebuffer hint is set: framebuffer_hint_window where the
/// window has a framebuffer of its own, otherwise the drivers of SDL's
/// order that are neither software, recorded nor the rebuild's failed one,
/// as a list, or the first of them alone before SDL 3.4. When that list is
/// empty, or software is refused too, nothing could present: a walk that
/// skipped a driver by a record, and every rebuild, walk SDL's full order
/// again from the top with the records ignored. In that second walk an
/// empty list leaves the hint unset, which is SDL's own choice. Under
/// SDL_RENDER_DRIVER there is no second walk.
///
/// @param[in,out] walk the walk
/// @param inputs the inputs the walk was started with
/// @return the attempt, or AttemptKind::none when nothing is left
[[nodiscard]] Attempt next_attempt(CreationWalk& walk, const CreationInputs& inputs);

// ---------------------------------------------------------------------------
// Present stalls

/// A present that takes longer than this, in nanoseconds, is a stall.
inline constexpr uint64_t stall_present_ns = 2'000'000'000;
/// The stalls within stall_window_ns that act.
inline constexpr uint32_t stalls_that_act = 3;
/// The span the stalls are counted over, in nanoseconds of steady frames:
/// the time of frames that are not steady does not count.
inline constexpr uint64_t stall_window_ns = 10'000'000'000;

/// The stalls seen lately.
struct StallWatch {
    /// The latest stalls' times on the steady frames' clock, oldest first.
    std::array<uint64_t, stalls_that_act> stall_times_ns{};
    uint32_t stalls{}; ///< how many of stall_times_ns hold one
    bool logged{};     ///< the standard tier has logged its stalls
};

/// What the stall rule asks for.
enum class StallAction : uint8_t {
    none,
    log,  ///< the standard tier: log it once and carry on
    drop, ///< the accelerated tier: drop acceleration for the run, unrecorded
};

/// Notes a steady frame's present measure. The host calls it for steady
/// frames alone (steady_frame), with the steady frames' own clock: the sum
/// of their intervals, which stands still while frames are not steady.
///
/// @param[in,out] watch the run's stalls
/// @param steady_ns the frame's time on the steady frames' clock, in
///     nanoseconds
/// @param present_ns the frame's present measure, in nanoseconds
/// @param tier the frame's tier
/// @return StallAction::drop or log at the third stall within
///     stall_window_ns, log only once in a run; otherwise none
[[nodiscard]] StallAction
note_present(StallWatch& watch, uint64_t steady_ns, uint64_t present_ns, RenderTier tier) noexcept;

// ---------------------------------------------------------------------------
// The step-down ladder

/// The zoomed-out view's method.
enum class ZoomOutMethod : uint8_t {
    area,  ///< the exact area pass on the processor
    blend, ///< the two-level blend on the graphics card
};

/// The scene pixels the zoomed-out view may draw, in order from the least.
enum class SceneBudget : uint8_t {
    none,    ///< no zoomed-out filtering: below zoom 1 the scene is drawn at the zoom
    reduced, ///< up to 2.25 scene pixels per battlefield pixel
    full,    ///< up to 4 scene pixels per battlefield pixel
};

/// How the graphics card magnifies, in order from the least work.
enum class CardFilter : uint8_t {
    linear,           ///< plain LINEAR, with no prescale target
    prescale_quarter, ///< sharp-bilinear within a quarter of the prescale budget
    prescale_full,    ///< sharp-bilinear within the whole prescale budget
    pixelart,         ///< the renderer's PIXELART scale mode, which needs no prescale target
};

/// Where the accelerated tier stands on the step-down ladder. Each step
/// lowers one member, from the top: the zoomed-out rungs (method, then
/// budget), magnify off, NEAREST chrome, the card's magnification, and the
/// standard tier.
struct LadderState {
    ZoomOutMethod method{ZoomOutMethod::area};
    SceneBudget budget{SceneBudget::none};
    bool blend_allowed{}; ///< blend is built and allowed on this machine and renderer
    /// Above zoom 1 the card magnifies the scene; false is the magnify-off
    /// rung.
    bool magnify{};
    /// The chrome is filtered at scales that are not whole; false is the
    /// NEAREST-chrome rung.
    bool filtered_chrome{};
    CardFilter card{CardFilter::linear};
    bool standard{}; ///< the last rung: the standard tier for the rest of the run
};

/// Whether a kind of machine has been run on the accelerated tier.
enum class ClassTesting : uint8_t {
    untested, ///< nobody has run this operating system, architecture and driver
    tested,   ///< it has been run, but its weak rows are not yet measured
    measured, ///< it has been run and its weak rows measured
};

/// The most logical processors with which a machine starts at budget none.
inline constexpr uint32_t budget_none_most_processors = 2;
/// The logical processors with which a machine starts at budget reduced.
inline constexpr uint32_t budget_reduced_processors = 3;
/// The most physical memory, in bytes, with which the blend is never used.
inline constexpr uint64_t most_memory_without_blend = 4 * gibibyte;

/// The machine and renderer facts the starting rung is sized from.
struct StartInputs {
    uint32_t processors{1}; ///< logical processors
    /// Physical memory in bytes, as the system reports it; 0 when it does
    /// not say. Only the blend reads it: a start is made only from
    /// smallest_accelerated_memory, and is the same at any memory from there.
    uint64_t memory{};
    bool light_machine{}; ///< oa::platform::light_machine
    bool raspberry_pi{};  ///< a Raspberry Pi
    /// An ARM processor other than Apple silicon and the Raspberry Pi models
    /// that have been run on the accelerated tier.
    bool other_arm{};
    bool legacy_windows{}; ///< Windows before Vista
    ClassTesting run_class{ClassTesting::untested};
    DriverTraits driver{};  ///< the renderer's driver
    bool pixelart{};        ///< probe (c) found the PIXELART scale mode
    bool blend_available{}; ///< blend is built and its half level fits this renderer
};

/// The magnify path has been measured no slower than the standard tier on
/// one thread, which lets a start at budget none rise above magnify off.
inline constexpr bool magnify_measured_at_budget_none = false;
/// A real run on Windows before Vista has held the floor with magnify on,
/// which lets such a start rise above magnify off.
inline constexpr bool magnify_measured_before_vista = false;

/// Returns the scene budget a machine starts at. Memory does not enter it:
/// a start is made only from smallest_accelerated_memory.
///
/// The budget is none with budget_none_most_processors or fewer logical
/// processors, on an ARM processor other than Apple silicon and the
/// Raspberry Pis that have been run, on Windows before Vista and in a class
/// nobody has run; reduced with budget_reduced_processors, and in a class
/// that has been run but not yet measured; full with more processors in a
/// measured class.
///
/// @param machine the machine's and the renderer's facts
/// @return the starting budget
[[nodiscard]] SceneBudget start_budget(const StartInputs& machine) noexcept;

/// Returns the rung a machine starts at, from smallest_accelerated_memory.
///
/// The budget is start_budget's. Before Vista and at budget none the start
/// is the magnify-off rung; no machine starts at the NEAREST-chrome rung by
/// its size. The card uses PIXELART where the probe found it, otherwise the
/// prescale budget, a quarter of it on a light machine or a Pi. The blend
/// is allowed where it is available, on a driver that does not exclude it,
/// with more than most_memory_without_blend.
///
/// @param machine the machine's and the renderer's facts
/// @return the starting rung
[[nodiscard]] LadderState start_rung(const StartInputs& machine) noexcept;

/// Returns the highest rung a run on this machine may start at, by the
/// remembered rung or otherwise: the machine's own start with the area
/// pass, its budget free to rise to full where the start is above the
/// magnify-off rung. A start at the magnify-off rung rises no higher.
///
/// @param machine the machine's and the renderer's facts
/// @return the ceiling
[[nodiscard]] LadderState start_ceiling(const StartInputs& machine) noexcept;

/// The recorded median frame interval under which a remembered rung starts
/// the next run one rung higher, in percent of the target period.
inline constexpr uint32_t headroom_percent = 60;

/// Returns the rung a run starts at, given the rung a run before it reached.
///
/// The run starts at the remembered rung, never at the standard tier and
/// never above the ceiling, and one rung higher when the frames recorded at
/// it had a median under headroom_percent of the target period.
///
/// @param machine the machine's and the renderer's facts
/// @param remembered the rung recorded for this driver, adapter and engine version
/// @param median_percent the median frame interval recorded at it, in
///     percent of the target period
/// @return the starting rung
[[nodiscard]] LadderState resume_rung(
    const StartInputs& machine, const LadderState& remembered, uint32_t median_percent
) noexcept;

/// Returns the rung one step up the ladder: the lowest rung taken that the
/// ceiling allows back, passing rungs that would change nothing.
///
/// @param state the rung
/// @param ceiling the highest the run may rise to
/// @return the rung one step up, or state when none is left
[[nodiscard]] LadderState step_up(const LadderState& state, const LadderState& ceiling) noexcept;

/// What a frame showed, for the pool its sample joins.
enum class FrameKind : uint8_t {
    zoomed_out, ///< below zoom 1 with the area pass or the blend active
    zoomed_in,  ///< above zoom 1 with the card magnifying the scene
    other,      ///< every other frame
};

/// One presented frame, as the frame pacer and the frame measures saw it.
struct FrameSample {
    uint64_t now_ns{};      ///< when the frame was presented, nanoseconds on a steady clock
    uint64_t interval_ns{}; ///< since the frame before it
    uint64_t tick_ns{};     ///< the time its ticks took, which is not the tier's doing
    uint64_t draw_ns{};     ///< the draw measure
    uint64_t present_ns{};  ///< the present measure: uploading and presenting
    uint64_t area_ns{};     ///< the area pass's own time, part of passes_ns
    uint64_t passes_ns{};   ///< the time of the tier's own added passes
    uint32_t paced_frames_per_second{}; ///< the rate the loop paces at
    FrameKind kind{FrameKind::other};
    bool clock_behind{};  ///< the match clock runs below its requested rate
    bool idle{};          ///< paced at the idle rate
    bool window_active{}; ///< the window is shown and has the focus
    bool settling{};      ///< within 2 s of a resize, a mode change or a full-screen switch
    bool match_warming{}; ///< within the match's first 5 s
};

/// Tells whether a frame is steady, so that its sample counts.
///
/// @param sample the frame
/// @return true for a frame at the full rate in an active window, outside
///     the settle time and the match's first seconds
[[nodiscard]] bool steady_frame(const FrameSample& sample) noexcept;

/// The highest frame rate the step-down holds the frames to; a faster loop
/// is judged against this rate's period.
inline constexpr uint32_t step_target_frames_per_second = 60;
/// The span of samples the slow rule takes its median over, in nanoseconds.
inline constexpr uint64_t slow_window_ns = 3'000'000'000;
/// The span of samples the very slow rule takes its median over, in nanoseconds.
inline constexpr uint64_t very_slow_window_ns = 1'000'000'000;
/// The shortest time between two steps of the slow rule, in nanoseconds.
inline constexpr uint64_t step_spacing_ns = 10'000'000'000;
/// The slow rule's limit, in percent of the target period.
inline constexpr uint32_t slow_percent = 125;
/// The very slow rule's limit, in percent of the target period.
inline constexpr uint32_t very_slow_percent = 200;
/// The share of the target period, in percent, above which the tier's own
/// passes cost a rung that owns them a step while frames are late. A
/// placeholder until it is measured.
inline constexpr uint32_t passes_percent = 10;
/// The area pass favours a step to the blend when it takes at least one part
/// in this many of the draw measure: a third.
inline constexpr uint32_t blend_area_parts = 3;
/// The present measure's share of the target period, in percent, under
/// which uploads count as cheap enough for the blend. A placeholder until
/// it is measured.
inline constexpr uint32_t blend_present_percent = 25;
/// The most samples a pool keeps: three seconds at the loop's highest
/// paced rate of 120 frames a second, and some.
inline constexpr size_t pool_capacity = 384;

/// One steady frame's figures in a pool, in microseconds.
struct PooledSample {
    uint32_t interval_us{}; ///< the frame interval, which measures the pool's span
    uint32_t time_us{};     ///< the frame interval less its ticks' time
    uint32_t passes_us{};   ///< the tier's own added passes
    uint32_t area_us{};     ///< the area pass
    uint32_t draw_us{};     ///< the draw measure
    uint32_t present_us{};  ///< the present measure
};

/// The latest samples of one kind of frame, pooled across short spells.
struct SamplePool {
    std::array<PooledSample, pool_capacity> samples{}; ///< a ring, oldest at first
    uint32_t first{};                                  ///< the oldest sample's place
    uint32_t count{};                                  ///< the samples held
    uint64_t held_us{};                                ///< the sum of the held samples' intervals
};

/// The step-down: the ladder's state and the pools of samples that move it.
struct ScaleStepDown {
    LadderState state{};
    SamplePool zoomed_out{}; ///< moves the zoomed-out rungs
    SamplePool zoomed_in{};  ///< moves magnify off
    SamplePool other{}; ///< moves NEAREST chrome, the card's magnification and the standard tier
    uint64_t last_step_ns{}; ///< when the slow rule last stepped
    bool stepped{};          ///< it has stepped in this run
};

/// What a sample did to the ladder.
enum class StepResult : uint8_t {
    none,     ///< nothing changed
    stepped,  ///< one rung down
    shed,     ///< the clock ran behind: straight to budget none and magnify off
    standard, ///< the last rung: drop acceleration for the run, unrecorded
};

/// Starts a run's step-down at a rung.
///
/// @param state the starting rung (start_rung or resume_rung)
/// @return the step-down with empty pools
[[nodiscard]] ScaleStepDown start_step_down(const LadderState& state) noexcept;

/// Feeds one presented match frame to the step-down.
///
/// Frames that are not steady are ignored. A sample's time is its interval
/// less its ticks' time, judged against the period of the lower of the
/// paced rate and step_target_frames_per_second. In the frame's pool, the
/// ladder steps one rung when the median over the last 3 s exceeds 1.25
/// times the period, or exceeds the period while the tier's own passes take
/// over a tenth of it, at most once per 10 s; and at once when the median
/// over the last 1 s exceeds twice the period. While the match clock runs
/// behind, any time the passes take sheds budget and magnification at
/// once. A step empties the pool that caused it, and the ladder never steps
/// back up within the run.
///
/// @param[in,out] ladder the run's step-down
/// @param sample the frame
/// @return what changed
StepResult feed_step_down(ScaleStepDown& ladder, const FrameSample& sample) noexcept;

/// Returns the rung one step down the ladder for a pool.
///
/// @param state the rung
/// @param pool the pool that asks for the step: zoomed-out frames move the
///     method and the budget, zoomed-in frames magnify, and other frames
///     NEAREST chrome, the card's magnification and the standard tier,
///     each passing rungs that would change nothing
/// @param blend_favoured the area pass takes enough of the draw and uploads
///     are cheap, so a step to the blend helps
/// @return the rung one step down
[[nodiscard]] LadderState
step_down(const LadderState& state, FrameKind pool, bool blend_favoured) noexcept;

// ---------------------------------------------------------------------------
// Records and strikes

/// Platforms where a fault in the stages a trial covers can stop the whole
/// system, so that one left-over trial is already a record.
enum class CrashEvidence : uint8_t {
    two_in_a_row, ///< Windows from Vista and macOS: two left-over trials in a row
    first_counts, ///< Windows before Vista and Linux: the first left-over trial
};

/// An accelerated path with a first use of its own in a run.
enum class AcceleratedPath : uint8_t {
    magnify,  ///< the magnified world's scene tiles and overlay
    prescale, ///< the prescale target
    blend,    ///< the half level of the two-level zoom-out
};

/// What a strike is against: a stage of a start that was left over, or a
/// failure seen while running.
enum class StrikeKind : uint8_t {
    none,
    create, ///< a left-over create sentinel
    /// A left-over standard sentinel: probe items 1 to 3, the intro or the
    /// first standard frames.
    standard,
    probe,   ///< a left-over probe trial: the function test and the first accelerated frames
    path,    ///< a left-over path trial: an accelerated path's first frames
    present, ///< a present error
    call,    ///< an accelerated-only call failed
    lost,    ///< the device was lost
    resets,  ///< three device resets within 60 s
};

/// A strike against a driver: evidence seen once, which becomes a record
/// only when the same is seen again in a row.
struct Strike {
    StrikeKind kind{StrikeKind::none};
    /// For StrikeKind::path: the path.
    AcceleratedPath path{AcceleratedPath::magnify};
    /// For StrikeKind::present and call: the failing call, as the host
    /// numbers its calls.
    uint16_t call{};
};

/// Tells whether two strikes are the same evidence.
///
/// @param first one strike
/// @param second the other
/// @return true when both are of the same kind, and of the same path or
///     call where their kind has one
[[nodiscard]] bool same_strike(const Strike& first, const Strike& second) noexcept;

/// What a record says failed.
enum class RecordedFailure : uint8_t {
    none,
    stopped, ///< the game stopped there: a left-over sentinel or trial
    present, ///< present errors
    call,    ///< accelerated-only calls
    lost,    ///< a lost device
    resets,  ///< repeated device resets
};

/// The adapter of the records when the probe has never read one: a probe
/// that cannot read the adapter clears nothing.
inline constexpr std::string_view unknown_adapter = "unknown";

/// A rung the step-down reached on a driver, remembered for the next run.
struct RememberedRung {
    bool kept{}; ///< a rung is remembered
    /// The rung, at most the one before the standard tier.
    LadderState rung{};
    /// The median frame interval recorded at it, in percent of the target
    /// period.
    uint32_t median_percent{};
};

/// The records and the strike kept for one driver.
struct DriverRecord {
    std::string driver{};
    /// The adapter they were made under: the records' adapter then.
    std::string adapter{};
    /// The engine version they were made under.
    std::string engine_version{};
    Strike strike{};
    /// The strike was made by a failure while running in this run, so the
    /// same failure again in this run is not the next run's. Never written,
    /// so every run starts with it clear.
    bool struck_this_run{};
    /// The walk skips the driver.
    RecordedFailure failed_driver{RecordedFailure::none};
    /// The driver stays on the standard tier.
    RecordedFailure accelerated_unusable{RecordedFailure::none};
    bool failed_driver_told{};        ///< the main menu's notice has shown failed_driver
    bool accelerated_unusable_told{}; ///< the main menu's notice has shown accelerated_unusable
    RememberedRung scale_level{};     ///< the rung the step-down reached on the driver
};

/// A trial: a flushed record standing through a stage that a fault could
/// end with the whole system, and the sentinel's file with it.
struct TrialRecord {
    /// StrikeKind::probe for the function test and the first accelerated
    /// frames, StrikeKind::path for a path's first frames, or
    /// StrikeKind::none when no trial stands.
    StrikeKind stage{StrikeKind::none};
    AcceleratedPath path{AcceleratedPath::magnify}; ///< for StrikeKind::path
    std::string driver{};
};

/// Where a run that ended cleanly at native density did so.
struct NativeDensityRecord {
    std::string driver{};         ///< empty when none is recorded
    std::string engine_version{}; ///< the engine version it ran
};

/// Everything renderer-state.conf holds.
struct RendererRecords {
    /// The adapter the probe last read, or unknown_adapter.
    std::string adapter{unknown_adapter};
    /// The running engine's version, given to every driver's records made in
    /// this run (clear_on_machine_change sets it).
    std::string engine_version{};
    TrialRecord trial{};
    NativeDensityRecord native_density{};
    /// The records and strikes of every driver that has any.
    std::vector<DriverRecord> drivers{};
};

/// Returns a driver's records, or none.
///
/// @param records the records
/// @param driver the driver's name
/// @return its record, or nullptr when it has none
[[nodiscard]] const DriverRecord*
find_record(const RendererRecords& records, std::string_view driver) noexcept;

/// Returns a driver's records, adding an empty one, made under the records'
/// adapter and engine version, when it has none.
///
/// @param[in,out] records the records
/// @param driver the driver's name
/// @return its record, which stays valid until the next record is added
[[nodiscard]] DriverRecord& record_for(RendererRecords& records, std::string_view driver);

/// Lists the drivers recorded failed-driver, for the walk to skip.
///
/// @param records the records
/// @return views of the names held in records, in their order
[[nodiscard]] std::vector<std::string_view> failed_driver_list(const RendererRecords& records);

/// How the records changed.
struct RecordChange {
    /// A record, a strike or the trial changed, so the records are written.
    bool changed{};
    /// A failed-driver or accelerated-unusable record was made, which the
    /// main menu tells.
    bool new_record{};
};

/// The keys and values of renderer-state.conf, as the preferences store
/// reads and writes them.
using RecordValues = std::map<std::string, std::string>;

/// Reads the records from renderer-state.conf.
///
/// The keys are `adapter`, `trial` (`probe <driver>` or
/// `path <magnify|prescale|blend> <driver>`), `native-density`
/// (`<driver> <engine version>`), and for each driver `strike.<driver>`
/// (`<stage> <adapter> <engine version>`, the stage `create`, `standard`,
/// `probe`, `path <name>`, `present <call>`, `call <call>`, `lost` or
/// `resets`), `failed-driver.<driver>` and `accelerated-unusable.<driver>`
/// (`<failure> <adapter> <engine version>`, then `told` once the main menu
/// has shown it) and `scale-level.<driver>`
/// (`<rung> <adapter> <engine version> <median>`, the median a fraction of
/// the target period). An adapter's name may hold spaces; the other fields
/// hold none. A value the engine never writes, such as `call` as a
/// failed-driver failure, is ignored, as are unknown keys and a driver's
/// values made under another adapter or engine version than its first.
///
/// @param values the file's keys and values
/// @return the records
[[nodiscard]] RendererRecords parse_records(const RecordValues& values);

/// Writes the records as renderer-state.conf's keys and values, which
/// parse_records reads back unchanged.
///
/// @param records the records
/// @return the keys and values; a driver whose name is empty or holds a
///     space is left out
[[nodiscard]] RecordValues format_records(const RendererRecords& records);

/// Clears what another engine version or another adapter recorded: every
/// driver's strike, records and remembered rung made under them, and the
/// native-density record. The host calls it at the start, before the left
/// overs are applied and the walk reads the records, with the adapter
/// unknown_adapter, and again once the probe has read the adapter; the walk
/// has skipped by then, so for the walk that clearing applies from the next
/// start. An adapter that cannot be read clears nothing.
///
/// @param[in,out] records the records
/// @param adapter the adapter the probe read, or unknown_adapter
/// @param engine_version the running engine's version, never empty
/// @return whether the records changed
RecordChange clear_on_machine_change(
    RendererRecords& records, std::string_view adapter, std::string_view engine_version
);

/// Clears every strike, failure record and remembered rung, as switching the
/// setting Off then On or Restore defaults does, so that every driver gets a
/// fresh try. The adapter, the trial and the native-density record stay.
///
/// @param[in,out] records the records
/// @return whether anything changed
RecordChange clear_failures(RendererRecords& records) noexcept;

/// Sets the trial, before the stage it covers.
///
/// @param[in,out] records the records
/// @param stage StrikeKind::probe or StrikeKind::path
/// @param path the path, for StrikeKind::path
/// @param driver the driver
/// @return whether the records changed
RecordChange set_trial(
    RendererRecords& records, StrikeKind stage, AcceleratedPath path, std::string_view driver
);

/// Erases the trial, when the stage it covers has passed.
///
/// @param[in,out] records the records
/// @return whether a trial stood, so that the records changed
RecordChange erase_trial(RendererRecords& records) noexcept;

// ---------------------------------------------------------------------------
// The sentinel and the trial through a run

/// The stage a sentinel stands at.
enum class SentinelStage : uint8_t {
    none,       ///< no sentinel: the last run ended cleanly
    unreadable, ///< a sentinel was left over but cannot be read
    create,     ///< `create <driver>`: creating the renderer
    standard,   ///< `standard <driver>`: probe items 1 to 3, the intro or the first standard frames
    probe,      ///< `probe <driver>`: the function test
    accelerated, ///< `accelerated <driver>`: the first accelerated frames
    path,        ///< `path <name> <driver>`: a path's first frames
    running,     ///< `running <driver>`: the run is past its start-up
};

/// What a start found in the sentinel left over by the run before it.
struct Leftover {
    SentinelStage sentinel{SentinelStage::none};
    std::string_view sentinel_driver{};
    AcceleratedPath sentinel_path{AcceleratedPath::magnify}; ///< for SentinelStage::path
    /// For a software sentinel on a video driver without a framebuffer of
    /// its own: the one driver the framebuffer hint's list named, or empty
    /// when it named none or several.
    std::string_view via_driver{};
};

/// Reads the sentinel's value, renderer-sentinel.conf's `starting` key.
///
/// @param value the value
/// @return the stage and driver, with views into value;
///     SentinelStage::unreadable for a value the engine never writes
[[nodiscard]] Leftover parse_sentinel(std::string_view value);

/// Writes the sentinel's value.
///
/// @param stage the stage, neither none nor unreadable
/// @param driver the driver
/// @param path the path, for SentinelStage::path
/// @param via_list for software at create, standard, probe or accelerated:
///     the framebuffer hint's list, appended as `via <list>`; empty when the
///     hint was not set to a list
/// @return the value
[[nodiscard]] std::string format_sentinel(
    SentinelStage stage, std::string_view driver, AcceleratedPath path, std::string_view via_list
);

/// What a start makes of what was left over.
struct LeftoverOutcome {
    std::string driver{}; ///< the driver struck or recorded; empty when none was
    RecordChange change{};
    bool log_unclean_exit{}; ///< log once that the last run ended without a clean exit
};

/// Applies what the last run left over to the records.
///
/// A left-over trial decides whatever the sentinel holds, or when it cannot
/// be read: it is a strike, and a record of accelerated-unusable when the
/// same strike stood already, or at once where crash evidence counts the
/// first; the trial is then erased. Otherwise a left-over create or standard
/// sentinel is a strike, and the same strike twice in a row records
/// failed-driver; against software it counts only for the one driver its
/// framebuffer hint named. A left-over running sentinel, or one that cannot
/// be read, is only logged.
///
/// @param[in,out] records the records, with the trial left over
/// @param leftover the sentinel left over (parse_sentinel), or
///     SentinelStage::none when its file was gone
/// @param evidence how this platform counts a left-over trial
/// @return the driver struck or recorded and what changed
LeftoverOutcome
note_leftover(RendererRecords& records, const Leftover& leftover, CrashEvidence evidence);

/// Clears a driver's strike of a start-up stage it has passed cleanly.
///
/// @param[in,out] record the driver's record
/// @param function_test_ran the start ran the function test, so it passed
///     the probe stage as well
/// @return whether the record changed
RecordChange note_start_passed(DriverRecord& record, bool function_test_ran) noexcept;

/// Clears a driver's strike of an accelerated path whose first frames
/// passed.
///
/// @param[in,out] record the driver's record
/// @param path the path
/// @return whether the record changed
RecordChange note_path_passed(DriverRecord& record, AcceleratedPath path) noexcept;

/// Clears a driver's strike of a failure seen while running, at the clean
/// end of a run on it, unless this run made the strike.
///
/// @param[in,out] record the driver's record
/// @return whether the record changed
RecordChange note_clean_run(DriverRecord& record) noexcept;

/// Applies a failure seen while running.
///
/// A present error is a strike, and the same in the next run on the driver
/// records failed-driver. An accelerated-only call that fails is a strike,
/// and the same in the next run records accelerated-unusable. A lost device
/// or three resets records accelerated-unusable at once and is a strike,
/// and the same in the next run records failed-driver. A strike made in
/// this run is never the next run's, however often the failure comes back
/// in this one (a rebuild may return to the driver). Nothing is recorded
/// under SDL_RENDER_DRIVER, on a driver whose device is lost in ordinary
/// use, or failed-driver against software.
///
/// @param[in,out] record the driver's record
/// @param failure the failure, a strike of kind present, call, lost or resets
/// @param driver the driver's traits
/// @param render_driver_named SDL_RENDER_DRIVER is set
/// @return what changed
RecordChange note_running_failure(
    DriverRecord& record,
    const Strike& failure,
    const DriverTraits& driver,
    bool render_driver_named
) noexcept;

/// The frames a start presents, at the menus, loading screens and matches
/// alike, before its start-up stage has passed.
inline constexpr uint32_t start_stage_frames = 60;
/// The time a start runs before its start-up stage has passed, in
/// nanoseconds.
inline constexpr uint64_t start_stage_ns = 2'000'000'000;
/// The frames an accelerated path draws before its first use has passed.
inline constexpr uint32_t path_stage_frames = 60;

/// Tells whether a start-up stage has passed.
///
/// @param frames the frames presented since the stage began
/// @param elapsed_ns the time since it began, in nanoseconds
/// @return true after start_stage_frames frames and start_stage_ns both
[[nodiscard]] bool start_stage_passed(uint32_t frames, uint64_t elapsed_ns) noexcept;

/// What moves a run's sentinel or trial.
enum class LifeEvent : uint8_t {
    creating,                ///< before each attempt to create a renderer
    probing,                 ///< before probe items 1 to 3
    function_test,           ///< before the function test
    function_test_done,      ///< after the function test
    first_accelerated_frame, ///< before the first accelerated frame
    start_passed,            ///< the start-up stage passed (start_stage_passed)
    path_first_use,          ///< before a path's first use in the run
    path_passed,             ///< the path drew path_stage_frames frames
};

/// Where a run's sentinel and trial stand.
struct SentinelLife {
    bool kept{}; ///< the run keeps a sentinel: SDL_RENDER_DRIVER is not set
    SentinelStage stage{SentinelStage::none};
    AcceleratedPath path{AcceleratedPath::magnify}; ///< for SentinelStage::path
    bool trial{};                                   ///< a trial stands
    /// The stage that stood before the trial was written, which stands again
    /// when it could not be.
    SentinelStage before_trial{SentinelStage::none};
};

/// What to write for a life event, in this order: the trial, then the
/// sentinel, then the trial's erasure.
struct SentinelWrite {
    /// Write the trial, flushed, before anything else. When it cannot be
    /// written the stage is skipped: nothing else is written, and the host
    /// calls note_trial_unwritten.
    bool write_trial{};
    StrikeKind trial_stage{StrikeKind::none};                ///< the trial's stage
    AcceleratedPath trial_path{AcceleratedPath::magnify};    ///< the trial's path
    bool set_sentinel{};                                     ///< rewrite the sentinel
    SentinelStage sentinel{SentinelStage::none};             ///< its new stage
    AcceleratedPath sentinel_path{AcceleratedPath::magnify}; ///< for SentinelStage::path
    bool erase_trial{}; ///< erase the trial, flushed, as its stage has passed
};

/// Starts a run's sentinel.
///
/// @param render_driver_named SDL_RENDER_DRIVER is set, which keeps no
///     sentinel and no trial
/// @return the life, before its first event
[[nodiscard]] SentinelLife start_sentinel_life(bool render_driver_named) noexcept;

/// Moves a run's sentinel and trial for an event.
///
/// `create <driver>` stands before each attempt to create a renderer and
/// `standard <driver>` around probe items 1 to 3, through the intro and the
/// first standard frames. The function test writes the trial `probe` and
/// stands under `probe <driver>`, then `standard <driver>` again; the first
/// accelerated frame sets `accelerated <driver>`; once the start-up stage
/// passes, the sentinel becomes `running <driver>` and the trial is erased.
/// A path first used after that writes its trial and `path <name> <driver>`,
/// and after path_stage_frames frames goes back to `running <driver>` and
/// erases the trial. A path first used while another stage stands writes
/// nothing of its own: that stage's sentinel and trial cover it. Under
/// SDL_RENDER_DRIVER nothing is written.
///
/// @param[in,out] life the run's sentinel
/// @param event the event
/// @param path the path, for LifeEvent::path_first_use and path_passed
/// @return what to write; nothing when the event moves nothing
[[nodiscard]] SentinelWrite
sentinel_step(SentinelLife& life, LifeEvent event, AcceleratedPath path) noexcept;

/// Notes that the trial of the last step could not be written: its stage is
/// skipped, and the sentinel stands where it stood before the step.
///
/// @param[in,out] life the run's sentinel
void note_trial_unwritten(SentinelLife& life) noexcept;

// ---------------------------------------------------------------------------
// Chrome filtering and the prescale budget

/// The prescale budget B: the most pixels all prescale targets alive at once
/// may hold. A placeholder until it is measured.
inline constexpr uint64_t prescale_budget_pixels = uint64_t{1} << 23;

/// Returns the pixels a rung's prescale targets may hold together.
///
/// @param card the card's magnification
/// @return prescale_budget_pixels, a quarter of it, or 0 where no prescale
///     target is made
[[nodiscard]] uint64_t prescale_budget(CardFilter card) noexcept;

/// The prescale targets alive at once, charged against the budget.
struct PrescaleBudget {
    uint64_t limit{};   ///< prescale_budget for the rung
    uint64_t charged{}; ///< pixels of the targets alive now
};

/// Returns the factor a prescale target enlarges its source by: the scale
/// rounded up, falling to the largest whole number whose target fits what
/// is left of the budget.
///
/// @param budget the targets alive now
/// @param width the source's width in pixels
/// @param height the source's height in pixels
/// @param scale the scale the source is drawn at, above 0
/// @return the factor, at least 1; 1 means no target: plain LINEAR
[[nodiscard]] uint32_t prescale_factor(
    const PrescaleBudget& budget, uint32_t width, uint32_t height, double scale
) noexcept;

/// Charges a target to the budget.
///
/// @param[in,out] budget the targets alive now
/// @param pixels the target's pixels
/// @return true when it fits, and is charged; false when it does not
bool charge_prescale(PrescaleBudget& budget, uint64_t pixels) noexcept;

/// Returns a freed target's pixels to the budget, as the front end's target
/// is freed during a match.
///
/// @param[in,out] budget the targets alive now
/// @param pixels the target's pixels
void release_prescale(PrescaleBudget& budget, uint64_t pixels) noexcept;

/// How a layer is scaled to the window.
enum class ScaleFilter : uint8_t {
    nearest,        ///< as today
    pixelart,       ///< the renderer's PIXELART scale mode
    sharp_bilinear, ///< NEAREST into a prescale target, then LINEAR
    linear,         ///< plain LINEAR
};

/// Returns how the chrome and the 640x480 screens are scaled.
///
/// @param state the rung
/// @param scale the scale at the display's pixels, above 0
/// @return NEAREST in the standard tier, at a whole-number scale and on the
///     NEAREST-chrome rung; otherwise PIXELART, sharp-bilinear or plain
///     LINEAR, as the card's magnification says
[[nodiscard]] ScaleFilter chrome_filter(const LadderState& state, double scale) noexcept;

// ---------------------------------------------------------------------------
// Tiled textures

/// The largest tile, in texels, whatever the renderer allows.
inline constexpr uint32_t largest_tile_size = 2048;
/// The texels each tile carries from its neighbour on a side that has one,
/// so that LINEAR shows no seam.
inline constexpr uint32_t tile_gutter = 1;
/// The smallest tile a grid uses, in texels: room for content between two
/// gutters.
inline constexpr uint32_t smallest_tile_size = 3;

/// How a texture is split into tiles.
struct TileGrid {
    uint32_t width{};     ///< the texture's width in texels
    uint32_t height{};    ///< the texture's height in texels
    uint32_t tile_size{}; ///< the largest tile, gutters included; 0 when the texture is one tile
    uint32_t columns{};   ///< tiles across; 0 for an empty texture or a limit too small
    uint32_t rows{};      ///< tiles down
};

/// A rectangle in texels.
struct TexelRect {
    uint32_t x{};
    uint32_t y{};
    uint32_t width{};
    uint32_t height{};
};

/// One tile of a grid.
struct Tile {
    /// The texels the tile's texture holds, gutters included, in the whole
    /// texture's texels.
    TexelRect texture{};
    /// The texels it is drawn with, gutters left out, in the whole texture's
    /// texels.
    TexelRect content{};
    /// The content in the tile texture's own texels: its source rectangle.
    TexelRect source{};
};

/// Plans the tiles of a texture.
///
/// A texture within the renderer's limit, or with no limit, is one tile.
/// Beyond it, tiles are no larger than the lower of the limit and
/// largest_tile_size, gutters included, and carry a tile_gutter-texel
/// gutter on each side that has a neighbour.
///
/// @param width the texture's width in texels
/// @param height the texture's height in texels
/// @param limit the renderer's texture limit in texels (texture_limit);
///     0 for none
/// @return the grid; no tiles when the texture is empty or the limit is
///     under smallest_tile_size
[[nodiscard]] TileGrid plan_tiles(uint32_t width, uint32_t height, uint32_t limit) noexcept;

/// Returns one tile of a grid.
///
/// @param grid the grid
/// @param column the tile's column, under grid.columns
/// @param row the tile's row, under grid.rows
/// @return the tile
[[nodiscard]] Tile tile_at(const TileGrid& grid, uint32_t column, uint32_t row) noexcept;

} // namespace oa::app::render_policy
