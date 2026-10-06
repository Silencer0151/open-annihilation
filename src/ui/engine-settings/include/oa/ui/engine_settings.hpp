// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The Open Annihilation settings: their values and defaults, the keys the
// preferences file keeps them under, and which of them a running game
// locks. Every default plays the game as it plays without the settings;
// the dialog that shows them is in oa/ui/engine_settings/dialog.hpp.
#pragma once

#include "oa/data/languages.hpp"
#include "oa/data/limits.hpp"
#include "oa/data/mod_profile/overrides.hpp"
#include "oa/platform/preferences.hpp"
#include "oa/present/text_style.hpp"
#include "oa/ui/pad_controls.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace oa::ui::engine_settings {

/// The preferences keys of the settings. 3.1c's own SwitchAlt keeps its key
/// in the game's section, which the frontend's preferences read and write.
namespace key {
/// Path nodes a game tick, decimal (EngineSettings::path_search_nodes).
inline constexpr std::string_view path_search_nodes = "open-annihilation.path-search-nodes";
/// 1 or 0 (EngineSettings::wheel_zoom).
inline constexpr std::string_view wheel_zoom = "open-annihilation.wheel-zoom";
/// "automatic", "whole-map", or the share of normal size: "1/32", "1/16",
/// "1/8", "1/4" or "1/2" (EngineSettings::max_zoom_out).
inline constexpr std::string_view max_zoom_out = "open-annihilation.max-zoom-out";
/// Times normal size in decimal: "1", "2", "3" or "4"
/// (EngineSettings::max_zoom_in).
inline constexpr std::string_view max_zoom_in = "open-annihilation.max-zoom-in";
/// 1 or 0 (EngineSettings::escape_opens_menu).
inline constexpr std::string_view escape_opens_menu = "open-annihilation.escape-opens-menu";
/// Units per player, decimal (EngineSettings::unit_limit).
inline constexpr std::string_view unit_limit = "open-annihilation.unit-limit";
/// Frames a second, decimal (EngineSettings::max_frame_rate).
inline constexpr std::string_view max_frame_rate = "open-annihilation.max-fps";
/// The level's samples along each axis, 1 for off (EngineSettings::anti_aliasing).
inline constexpr std::string_view anti_aliasing = "open-annihilation.anti-aliasing";
/// 1 or 0 (EngineSettings::frame_stats).
inline constexpr std::string_view frame_stats = "open-annihilation.frame-stats";
/// "desktop", or the width and height in decimal joined by an "x", as
/// "800x600" (EngineSettings::screen_size).
inline constexpr std::string_view screen_size = "open-annihilation.screen-size";
/// The mod folder played from the next start, as a UTF-8 path; absent for
/// none (EngineSettings::mod_folder).
inline constexpr std::string_view mod_directory = "open-annihilation.mod-directory";
/// The folder the player picked with an earlier version's Pick Folder...,
/// as a UTF-8 path, kept only while key::mod_directory names the same
/// folder; the Mods page lists it while it is still a folder. Absent for
/// none (EngineSettings::picked_mod_folder).
inline constexpr std::string_view picked_mod_directory = "open-annihilation.picked-mod-directory";
/// "off", "basic" or "full" (EngineSettings::hardware_acceleration). A
/// whole number reads as the On and Off switch the setting was before: 1
/// or any number above 0 is "full", 0 or below "off".
inline constexpr std::string_view hardware_acceleration = "open-annihilation.hardware-acceleration";
/// 1 or 0 (EngineSettings::vertical_sync).
inline constexpr std::string_view vertical_sync = "open-annihilation.vertical-sync";
/// "sharp", "whole-steps" or "unfiltered" (EngineSettings::menu_scaling).
inline constexpr std::string_view menu_scaling = "open-annihilation.menu-scaling";
/// 1 or 0 (EngineSettings::native_density).
inline constexpr std::string_view native_density = "open-annihilation.native-density";
/// "off", "reduced" or "full" (EngineSettings::explosion_flash).
inline constexpr std::string_view explosion_flash = "open-annihilation.explosion-flash";
/// 1 or 0 (EngineSettings::modern_fonts).
inline constexpr std::string_view modern_fonts = "open-annihilation.modern-fonts";
/// 1 or 0 (EngineSettings::text_outline).
inline constexpr std::string_view text_outline = "open-annihilation.text-outline";
/// 1 or 0 (EngineSettings::text_shadow).
inline constexpr std::string_view text_shadow = "open-annihilation.text-shadow";
/// 1 or 0 (EngineSettings::text_background).
inline constexpr std::string_view text_background = "open-annihilation.text-background";
/// 1 or 0 (EngineSettings::unicode_chat).
inline constexpr std::string_view unicode_chat = "open-annihilation.unicode-chat";
/// Percent of the game fonts' sizes, decimal (EngineSettings::text_size).
inline constexpr std::string_view text_size = "open-annihilation.text-size";
/// "system" for the operating system's choice, or a language's BCP-47 tag,
/// as "de" (EngineSettings::language).
inline constexpr std::string_view language = "open-annihilation.language";
/// 1 or 0 (EngineSettings::developer_mode).
inline constexpr std::string_view developer_mode = "open-annihilation.developer-mode";
/// "automatic", "box" or "scroll" (EngineSettings::touch_drag).
inline constexpr std::string_view touch_drag = "open-annihilation.touch-drag";
/// Milliseconds, decimal (EngineSettings::touch_hold_ms).
inline constexpr std::string_view touch_hold_delay = "open-annihilation.touch-hold-delay";
/// "stay-on" or "one-action" (EngineSettings::touch_latches).
inline constexpr std::string_view touch_latches = "open-annihilation.touch-latches";
/// 1 or 0 (EngineSettings::touch_haptics).
inline constexpr std::string_view touch_haptics = "open-annihilation.touch-haptics";
/// 1 or 0 (EngineSettings::touch_left_handed).
inline constexpr std::string_view touch_left_handed = "open-annihilation.touch-left-handed";
/// "standard", "large" or "larger" (EngineSettings::touch_control_size).
inline constexpr std::string_view touch_control_size = "open-annihilation.touch-control-size";
/// "trackpads" or "sticks" (EngineSettings::pad_scheme).
inline constexpr std::string_view pad_scheme = "open-annihilation.pad-scheme";
/// "relative" or "absolute" (EngineSettings::pad_right_trackpad).
inline constexpr std::string_view pad_right_trackpad = "open-annihilation.pad-right-trackpad";
/// Percent, decimal (EngineSettings::pad_pointer_speed).
inline constexpr std::string_view pad_pointer_speed = "open-annihilation.pad-pointer-speed";
/// "off", "low" or "high" (EngineSettings::pad_acceleration).
inline constexpr std::string_view pad_acceleration = "open-annihilation.pad-acceleration";
/// 1 or 0 (EngineSettings::pad_glide).
inline constexpr std::string_view pad_glide = "open-annihilation.pad-glide";
/// "zoom", "pointer" or "nothing" (EngineSettings::pad_right_stick).
inline constexpr std::string_view pad_right_stick = "open-annihilation.pad-right-stick";
/// 1 or 0 (EngineSettings::pad_magnetism).
inline constexpr std::string_view pad_magnetism = "open-annihilation.pad-magnetism";
/// "off", "right-pad", "right-stick" or "always" (EngineSettings::pad_gyro).
inline constexpr std::string_view pad_gyro = "open-annihilation.pad-gyro";
/// Percent, decimal (EngineSettings::pad_gyro_speed).
inline constexpr std::string_view pad_gyro_speed = "open-annihilation.pad-gyro-speed";
/// "off", "light" or "strong" (EngineSettings::pad_haptics).
inline constexpr std::string_view pad_haptics = "open-annihilation.pad-haptics";
/// "automatic", "steam-deck", "xbox", "playstation", "nintendo" or "off"
/// (EngineSettings::pad_prompts).
inline constexpr std::string_view pad_prompts = "open-annihilation.pad-prompts";
/// 1 or 0 (EngineSettings::pad_left_handed).
inline constexpr std::string_view pad_left_handed = "open-annihilation.pad-left-handed";
/// 1 or 0 (EngineSettings::game_files_backed_up).
inline constexpr std::string_view game_files_backed_up = "open-annihilation.game-files-backed-up";
/// The start of the key a profile's overrides of its standard hacks are
/// kept under, which the profile's id ends (Inputs::profile_id), as
/// "open-annihilation.hack-overrides.ta-3.1c": their text as
/// oa::data::mod_profile::overrides_text writes it
/// (EngineSettings::hack_overrides).
inline constexpr std::string_view hack_overrides = "open-annihilation.hack-overrides.";
} // namespace key

/// Path nodes the path search may visit in a game tick, all players
/// together, at 1x: the credit every match starts with.
inline constexpr int32_t base_path_search_nodes = 1333;
/// The most the Pathfinding cycles setting multiplies base_path_search_nodes by.
inline constexpr int32_t highest_path_search_multiplier = 8;
/// The most path nodes a game tick the setting allows: 8x.
inline constexpr int32_t highest_path_search_nodes =
    base_path_search_nodes * highest_path_search_multiplier;

/// The lowest unit limit the setting offers, in units per player.
inline constexpr uint16_t lowest_unit_limit = 50;
/// The highest unit limit the setting offers, in units per player, unless a
/// mod's limits allow more (highest_offered_unit_limit).
inline constexpr uint16_t highest_unit_limit = 1500;
/// The unit limit setting's step, in units per player.
inline constexpr uint16_t unit_limit_step = 50;
/// The unit limit without a stored value or an installation's own, in units per player.
inline constexpr uint16_t default_unit_limit = 250;
/// The lowest unit limit the preferences keep, in units per player: the
/// lowest an installation's totala.ini can set, below the setting's stops.
inline constexpr uint16_t lowest_stored_unit_limit = 20;

/// The lowest maximum frame rate the setting offers, in frames a second:
/// the simulation's ticks a second, a frame for each tick.
inline constexpr uint32_t lowest_frame_rate = 30;
/// The highest maximum frame rate the setting offers, and its default, in frames a second.
inline constexpr uint32_t highest_frame_rate = 120;
/// The maximum frame rate setting's step, in frames a second.
inline constexpr uint32_t frame_rate_step = 5;
/// The maximum frame rate a Raspberry Pi starts with, in frames a second:
/// what its graphics keep up with at the game's resolutions.
inline constexpr uint32_t raspberry_pi_frame_rate = 60;
/// The maximum frame rate a light machine starts with, in frames a second.
inline constexpr uint32_t light_machine_frame_rate = 60;

/// The smallest text size the setting offers, in percent of the game fonts'
/// sizes.
inline constexpr int32_t lowest_text_size = oa::present::lowest_text_size;
/// The largest text size the setting offers, in percent.
inline constexpr int32_t highest_text_size = oa::present::highest_text_size;
/// The text size setting's step, in percent.
inline constexpr int32_t text_size_step = 10;
/// The text size a player starts with, in percent: a fifth smaller than the
/// game fonts.
inline constexpr int32_t default_text_size = oa::present::default_text_size;

/// The size the game's window, or the screen in full screen, is set to, in
/// pixels; zero by zero is the desktop's own size, the game's default.
struct ScreenSize {
    uint16_t width{};  ///< pixels across; 0 with height 0 for the desktop's size
    uint16_t height{}; ///< pixels down

    friend bool operator==(const ScreenSize&, const ScreenSize&) = default;
};

/// The desktop's size, as ScreenSize keeps it.
inline constexpr ScreenSize desktop_screen_size{};

/// The screen sizes the setting offers where the display's own are not
/// known, in the order the dialog offers them. The game offers Desktop and
/// the display's own sizes instead (Dialog::offered_screen_sizes).
inline constexpr std::array<ScreenSize, 5> screen_sizes{{
    desktop_screen_size,
    {640, 480},
    {800, 600},
    {1024, 768},
    {1280, 1024},
}};

/// The smallest screen size the preferences keep, the game's own screen.
inline constexpr ScreenSize smallest_screen_size{640, 480};
/// The longest side of a screen size the preferences keep, in pixels.
inline constexpr uint16_t longest_screen_side = 8192;

/// The screen size a light machine starts with.
inline constexpr ScreenSize light_machine_screen_size{800, 600};
/// The screen size a light machine starts with when its desktop is smaller
/// than light_machine_screen_size.
inline constexpr ScreenSize small_desktop_screen_size{640, 480};

/// The most bytes of an installation's totala.ini the defaults read.
inline constexpr std::size_t installation_ini_limit = std::size_t{64} * 1024;

/// Enhanced anti-aliasing: how many times finer, along each axis, each
/// unit's model is drawn before it is reduced into the frame. The value is
/// that number; off draws units as without the setting.
enum class AntiAliasing : uint8_t {
    off = 1,
    x2 = 2,
    x4 = 4,
    x8 = 8,
    x16 = 16,
};

/// The levels of enhanced anti-aliasing, in the order the dialog offers them.
inline constexpr std::array<AntiAliasing, 5> anti_aliasing_levels{
    AntiAliasing::off,
    AntiAliasing::x2,
    AntiAliasing::x4,
    AntiAliasing::x8,
    AntiAliasing::x16,
};

/// A key the mod options' key settings offer: its SDL key code and name.
struct OptionKey {
    uint32_t code{};         ///< the SDL key code
    std::string_view name{}; ///< what the dialog shows
};

/// The keys the mod options' key settings offer, in the order their
/// sliders step through them: the modifier keys, Tab, letters and marks.
inline constexpr std::array<OptionKey, 16> option_keys{{
    {0x400000e2U, "Alt"},
    {0x400000e0U, "Ctrl"},
    {0x400000e1U, "Shift"},
    {'\t', "Tab"},
    {'b', "B"},
    {'c', "C"},
    {'e', "E"},
    {'f', "F"},
    {'g', "G"},
    {'q', "Q"},
    {'r', "R"},
    {'v', "V"},
    {'x', "X"},
    {'z', "Z"},
    {'/', "/"},
    {'\\', "\\"},
}};

/// The most cells the mod options' snap radii go up to.
inline constexpr int32_t most_snap_radius = 9;

/// The settings a mod's display rules let the player change (its options
/// dialog, ui.options-dialog), as the mod options' sections show them. The
/// game keeps them with the mod's own settings, not with these; a dialog
/// opened for the engine's settings leaves them as they are.
struct ModOptions {
    uint32_t snap_override_key{}; ///< SDL key code held to stop a click snapping
    uint32_t autoclick_key{};     ///< SDL key code held to lay lines and rings
    uint32_t rotate_build_key{};  ///< SDL key code that turns a building
    /// Patrolling builders under Hold position, Maneuver and Roam: 0 reclaim
    /// only, 1 both, 2 assist only.
    std::array<uint8_t, 3> patrol{};
    /// Guarding builders under Hold position, Maneuver and Roam: 0 stay, 1
    /// as the game does, 2 scatter.
    std::array<uint8_t, 3> guard{};
    int32_t mex_snap_radius{};   ///< cells, 0 to mex_snap_most
    int32_t wreck_snap_radius{}; ///< cells, 0 to wreck_snap_most
    int32_t mex_snap_most{};     ///< the mod's most; the player cannot change it
    int32_t wreck_snap_most{};   ///< the mod's most; the player cannot change it
    bool optimize_dt_rows{};     ///< 2x2 lines lay out as a staggered double row
    bool full_rings{};           ///< rings include their corner places
    bool chat_backdrop{};        ///< chat lines get a dark backdrop
    uint8_t panel_background{};  ///< 0 none, 1 text, 2 solid

    friend bool operator==(const ModOptions&, const ModOptions&) = default;
};

/// Hardware acceleration: how much of each frame the graphics card takes
/// on. The processor still draws every pixel the game decides at Off and
/// Basic.
enum class HardwareAcceleration : uint8_t {
    off,   ///< the processor draws and scales every frame, as without the setting
    basic, ///< the graphics card scales and composes the frames, where it is able to
    /// The graphics card also draws the battlefield. Where it cannot, Basic
    /// draws instead and the status says why: the game cannot save its
    /// files, there is too little memory, the card lacks a feature Full
    /// needs, or Full stopped in this run or failed before on the driver;
    /// a shared game or a replay takes Full from the next game.
    full,
};

/// The levels of hardware acceleration, in the order the dialog offers them.
inline constexpr std::array<HardwareAcceleration, 3> hardware_acceleration_levels{
    HardwareAcceleration::off,
    HardwareAcceleration::basic,
    HardwareAcceleration::full,
};

/// Menu scaling: how the menus and the other screens the game draws at
/// 640x480 are enlarged to the window.
enum class MenuScaling : uint8_t {
    /// As large as the window holds, each of the screen's pixels as wide as
    /// the next: where the graphics card has the pixel-art filter, the edge
    /// between two of them blends over at most one column of the window;
    /// elsewhere as unfiltered.
    sharp,
    /// The largest whole number of window pixels to each of the screen's
    /// pixels that the window holds, the rest of the window black; as sharp
    /// in a window smaller than 640x480.
    whole_steps,
    /// As large as the window holds, each of the screen's pixels repeated
    /// over the window's pixels it covers, as without the setting.
    unfiltered,
};

/// The ways of Menu scaling, in the order the dialog offers them.
inline constexpr std::array<MenuScaling, 3> menu_scaling_choices{
    MenuScaling::sharp,
    MenuScaling::whole_steps,
    MenuScaling::unfiltered,
};

/// Explosion flash: how strongly the flash of each explosion lights the
/// battlefield under it.
enum class ExplosionFlash : uint8_t {
    off,     ///< no flash is drawn
    reduced, ///< at half its light: about one and a half times as bright at the centre
    full,    ///< as 3.1c draws it: about twice as bright at the centre
};

/// The levels of Explosion flash, in the order the dialog offers them.
inline constexpr std::array<ExplosionFlash, 3> explosion_flash_choices{
    ExplosionFlash::off,
    ExplosionFlash::reduced,
    ExplosionFlash::full,
};

/// What a one-finger drag on the battlefield does (the Touch section's One-finger drag).
enum class TouchDrag : uint8_t {
    automatic, ///< a selection box on a tablet, scrolling on a phone
    box,       ///< a selection box
    scroll,    ///< the map follows the finger
};

/// Whether QUEUE, ADD and x5 stay on after use (the Touch section's QUEUE and ADD).
enum class TouchLatches : uint8_t {
    stay_on,    ///< a latched control stays on until tapped again
    one_action, ///< a latched control turns off after the first action that uses it
};

/// The shortest hold delay of the touch controls, in milliseconds.
inline constexpr uint32_t lowest_touch_hold_ms = 250;
/// The longest hold delay of the touch controls, in milliseconds.
inline constexpr uint32_t highest_touch_hold_ms = 700;
/// The hold delay's step, in milliseconds.
inline constexpr uint32_t touch_hold_step_ms = 50;
/// The hold delay of the touch controls when the player has chosen none, in milliseconds.
inline constexpr uint32_t default_touch_hold_ms = 350;
static_assert(
    (highest_touch_hold_ms - lowest_touch_hold_ms) % touch_hold_step_ms == 0 &&
        (default_touch_hold_ms - lowest_touch_hold_ms) % touch_hold_step_ms == 0,
    "the hold delay's stops run from its lowest to its highest, its default among them"
);

/// The ways of One-finger drag, in the order the dialog offers them.
inline constexpr std::array<TouchDrag, 3> touch_drag_choices{
    TouchDrag::automatic,
    TouchDrag::box,
    TouchDrag::scroll,
};

/// The ways of QUEUE and ADD, in the order the dialog offers them.
inline constexpr std::array<TouchLatches, 2> touch_latches_choices{
    TouchLatches::stay_on,
    TouchLatches::one_action,
};

/// The touch layer's size on a dense screen (the Touch and Controller sections' Control size).
enum class ControlSize : uint8_t {
    standard, ///< the touch layer's own sizes
    large,    ///< a quarter larger
    larger,   ///< half as large again
};

/// The Control sizes, in the order the dialog offers them.
inline constexpr std::array<ControlSize, 3> control_size_choices{
    ControlSize::standard,
    ControlSize::large,
    ControlSize::larger,
};

/// Maximum zoom out: how far out the battlefield's view zooms. No choice but
/// Automatic zooms out past the point where the whole map fits the view.
enum class ZoomOutLimit : uint8_t {
    /// As far as the drawing allows: a sixth of normal size while the
    /// graphics card draws the battlefield (Full), a half otherwise.
    automatic,
    whole_map,         ///< until the whole map fits the view
    one_thirty_second, ///< to 1/32 of normal size
    one_sixteenth,     ///< to 1/16
    one_eighth,        ///< to 1/8
    one_quarter,       ///< to 1/4
    one_half,          ///< to 1/2
};

/// The Maximum zoom out choices, in the order the dialog offers them.
inline constexpr std::array<ZoomOutLimit, 7> zoom_out_limits{
    ZoomOutLimit::automatic,
    ZoomOutLimit::whole_map,
    ZoomOutLimit::one_thirty_second,
    ZoomOutLimit::one_sixteenth,
    ZoomOutLimit::one_eighth,
    ZoomOutLimit::one_quarter,
    ZoomOutLimit::one_half,
};

/// Returns the share of normal size a Maximum zoom out choice stops at.
///
/// @param limit the choice
/// @return screen pixels per map pixel: 1/32 to 1/2 for a share, 0 for
///     Whole map, which only the whole map's fit stops; nothing for
///     Automatic, which the drawing decides
[[nodiscard]] constexpr std::optional<float> zoom_out_share(ZoomOutLimit limit) noexcept {
    switch (limit) {
    case ZoomOutLimit::automatic:
        return std::nullopt;
    case ZoomOutLimit::whole_map:
        return 0.0F;
    case ZoomOutLimit::one_thirty_second:
        return 1.0F / 32.0F;
    case ZoomOutLimit::one_sixteenth:
        return 1.0F / 16.0F;
    case ZoomOutLimit::one_eighth:
        return 1.0F / 8.0F;
    case ZoomOutLimit::one_quarter:
        return 1.0F / 4.0F;
    case ZoomOutLimit::one_half:
        return 1.0F / 2.0F;
    }
    return std::nullopt;
}

/// Maximum zoom in: how far in the battlefield's view zooms, in times
/// normal size.
enum class ZoomInLimit : uint8_t {
    none = 1,        ///< never past normal size
    twice = 2,       ///< to 2x normal size
    three_times = 3, ///< to 3x
    four_times = 4,  ///< to 4x
};

/// The Maximum zoom in choices, in the order the dialog offers them.
inline constexpr std::array<ZoomInLimit, 4> zoom_in_limits{
    ZoomInLimit::none,
    ZoomInLimit::twice,
    ZoomInLimit::three_times,
    ZoomInLimit::four_times,
};

/// Returns the zoom a Maximum zoom in choice stops at.
///
/// @param limit the choice
/// @return screen pixels per map pixel: 1, 2, 3 or 4
[[nodiscard]] constexpr float closest_zoom(ZoomInLimit limit) noexcept {
    return static_cast<float>(static_cast<uint8_t>(limit));
}

/// The Control size a Steam Deck starts with: its screen is small and dense,
/// so the touch controls come close to a tablet's in size.
inline constexpr ControlSize steam_deck_control_size = ControlSize::larger;

/// Returns the points scale of a Control size.
///
/// @param size the Control size
/// @return 1, 1.25 or 1.5
[[nodiscard]] float control_size_scale(ControlSize size) noexcept;

/// Returns a hold delay as the setting keeps it: held to its range and put
/// on its nearest stop.
///
/// @param milliseconds the delay, in milliseconds
/// @return lowest_touch_hold_ms to highest_touch_hold_ms, a whole number of
///     touch_hold_step_ms above the lowest; half a step rounds up
[[nodiscard]] constexpr uint32_t snapped_touch_hold_ms(int64_t milliseconds) noexcept {
    const int64_t lowest = lowest_touch_hold_ms;
    const int64_t highest = highest_touch_hold_ms;
    const int64_t step = touch_hold_step_ms;
    const int64_t held = milliseconds < lowest    ? lowest
                         : milliseconds > highest ? highest
                                                  : milliseconds;
    return static_cast<uint32_t>(lowest + (held - lowest + step / 2) / step * step);
}

/// The settings, as the dialog shows them and the game puts them in effect.
struct EngineSettings {
    /// Path nodes the path search may visit in a game tick, all players
    /// together: base_path_search_nodes times the Pathfinding cycles.
    int32_t path_search_nodes{base_path_search_nodes};
    bool wheel_zoom{true}; ///< the mouse wheel zooms the battlefield
    /// How far out the battlefield's view zooms, by the wheel, a pinch, the
    /// touch controls or a controller.
    ZoomOutLimit max_zoom_out{ZoomOutLimit::automatic};
    /// How far in the battlefield's view zooms, by the same.
    ZoomInLimit max_zoom_in{ZoomInLimit::four_times};
    bool escape_opens_menu{}; ///< Escape with nothing to cancel opens the game menu
    bool switch_alt{};        ///< 3.1c's SwitchAlt: a number key alone selects its group
    uint16_t unit_limit{default_unit_limit};       ///< units per player, from the next game
    uint32_t max_frame_rate{highest_frame_rate};   ///< frames a second
    AntiAliasing anti_aliasing{AntiAliasing::off}; ///< enhanced anti-aliasing of units
    bool frame_stats{}; ///< the frame and tick times over the battlefield (+stats)
    TouchDrag touch_drag{TouchDrag::automatic};            ///< One-finger drag
    uint32_t touch_hold_ms{default_touch_hold_ms};         ///< Hold delay, ms
    TouchLatches touch_latches{TouchLatches::stay_on};     ///< QUEUE and ADD
    bool touch_haptics{true};                              ///< Haptics
    bool touch_left_handed{};                              ///< Left-handed layout
    ControlSize touch_control_size{ControlSize::standard}; ///< Control size
    /// Scheme (Controller).
    oa::ui::pad_controls::Scheme pad_scheme{oa::ui::pad_controls::Scheme::trackpads};
    /// Right trackpad (Controller).
    oa::ui::pad_controls::RightTrackpad pad_right_trackpad{
        oa::ui::pad_controls::RightTrackpad::relative
    };
    /// Pointer speed, percent (Controller).
    uint32_t pad_pointer_speed{oa::ui::pad_controls::default_pointer_speed};
    /// Pointer acceleration (Controller).
    oa::ui::pad_controls::Acceleration pad_acceleration{oa::ui::pad_controls::Acceleration::low};
    bool pad_glide{}; ///< Trackpad glide (Controller)
    /// Right stick (Controller).
    oa::ui::pad_controls::RightStick pad_right_stick{
        oa::ui::pad_controls::RightStick::zoom_and_pages
    };
    bool pad_magnetism{true}; ///< Magnetism (stick pointer) (Controller)
    /// Gyro pointer (Controller).
    oa::ui::pad_controls::Gyro pad_gyro{oa::ui::pad_controls::Gyro::off};
    /// Gyro speed, percent (Controller).
    uint32_t pad_gyro_speed{oa::ui::pad_controls::default_gyro_speed};
    /// Haptics (Controller).
    oa::ui::pad_controls::Haptics pad_haptics{oa::ui::pad_controls::Haptics::light};
    /// Button prompts (Controller).
    oa::ui::pad_controls::Prompts pad_prompts{oa::ui::pad_controls::Prompts::automatic};
    bool pad_left_handed{}; ///< Left-handed (Controller)
    /// The game files are kept in the device's backups (only where the platform keeps them).
    bool game_files_backed_up{};
    /// The window's size, and the screen's in full screen, from the next start.
    ScreenSize screen_size{desktop_screen_size};
    /// How much of each frame the graphics card may take on, where it is
    /// able to.
    HardwareAcceleration hardware_acceleration{HardwareAcceleration::off};
    /// Each frame waits for the display to be ready for it, so that no frame
    /// tears, and the frame rate keeps just below the display's.
    bool vertical_sync{};
    /// How the menus are enlarged to the window.
    MenuScaling menu_scaling{MenuScaling::sharp};
    /// The window opens at the display's own pixel density, from the next
    /// start: on macOS the Retina resolution, elsewhere the display's scale.
    /// Always on where the platform opens every window so
    /// (Inputs::native_density_windows).
    bool native_density{};
    /// How strongly explosions' flashes light the battlefield; a mod's
    /// profile may hold them lower still (ui.explosion-flash).
    ExplosionFlash explosion_flash{ExplosionFlash::full};
    /// Game text is drawn in the modern fonts, which hold the letters of
    /// many languages, rather than the game's own 8-bit fonts. On by default
    /// with the player's own preferences file (default_settings).
    bool modern_fonts{};
    bool text_outline{true}; ///< modern text has a dark outline round each letter
    bool text_shadow{true};  ///< modern text casts a dark shadow
    bool text_background{};  ///< each line of modern text is drawn on a shaded box
    /// Multiplayer chat is sent and read as UTF-8, in any language, to the
    /// players' machines that read it, and as the code page with '?' for
    /// the letters it lacks to the others. Off by default; a language pack
    /// that asks for it turns it on while its language is shown. It changes
    /// only how chat lines are written, never the simulation or a saved
    /// game, so players with different settings play together.
    bool unicode_chat{};
    /// The size of game text in the modern fonts, in percent of the game
    /// fonts' sizes, lowest_text_size to highest_text_size; the game's own
    /// fonts keep their sizes.
    int32_t text_size{default_text_size};
    /// The language the game shows its text in: system_choice
    /// (oa/data/languages.hpp) for the operating system's preferred
    /// language, else a known language's tag; English, the game's own,
    /// unless default_settings says otherwise. It changes only what players
    /// read: the simulation, a saved game and what a shared game sends are
    /// the same in every language.
    std::string language{oa::data::languages::english().tag};
    /// The mod folder played from the next start, as an absolute UTF-8 path:
    /// one of Inputs::mod_folders, or a folder the player picked; empty for
    /// none, the game as 3.1c plays it.
    std::string mod_folder;
    /// The folder the player picked with an earlier version's Pick
    /// Folder..., as an absolute UTF-8 path, while it is the mod
    /// (mod_folder); the Mods page lists it while it is still a folder.
    /// Empty for none.
    std::string picked_mod_folder;
    /// Developer Mode: the player's overrides of the standard hacks
    /// (hack_overrides) are laid over the profile the game plays. Off, the
    /// profile plays as it ships, and the overrides are kept.
    bool developer_mode{};
    /// The player's overrides of the standard hacks of the profile the game
    /// plays, or of the plain 3.1c baseline without a mod: at most one for
    /// each hack, kept under the profile's id (Inputs::profile_id). They
    /// apply while developer_mode is on: a display (view-scope) hack's at
    /// once, a rule (sim-scope) hack's from the next match's start.
    std::vector<oa::data::mod_profile::HackOverride> hack_overrides;
    /// The mod's own options, which only a dialog opened for them changes;
    /// read_settings and write_settings leave them out.
    ModOptions mod_options{};

    friend bool operator==(const EngineSettings&, const EngineSettings&) = default;
};

/// What the defaults depend on besides the engine itself.
struct Inputs {
    /// The preferences file is the player's own: --preferences-file did not
    /// name one. With a named file every default is the game's own
    /// behaviour on every platform, and the installation is not read.
    bool players_own_profile{};
    bool macos{}; ///< the game runs on macOS
    /// The installation's totala.ini, at most installation_ini_limit bytes
    /// of it; empty when it has none.
    std::string_view installation_ini{};
    bool raspberry_pi{}; ///< the game runs on a Raspberry Pi
    /// The game runs on a light machine (oa::platform::light_machine): one
    /// processor, no SSE2 or under 512 MiB of memory.
    bool light_machine{};
    /// The desktop's size; zero by zero when it is not known.
    ScreenSize desktop{};
    /// The unit limits the game allows: the limit a player starts with, and
    /// the range a stored or installation limit is clamped to. 3.1c's by
    /// default; a mod's profile may raise them.
    oa::data::limits::UnitsPerPlayer units_per_player{};
    /// The mod folders the game folder offers, as the preferences keep a
    /// chosen one (an absolute UTF-8 path), in the order the setting offers
    /// them.
    std::span<const std::string> mod_folders{};
    /// The id of the profile the game plays, which its overrides are kept
    /// under (key::hack_overrides): the mod's, or
    /// oa::data::mod_profile::base_game_id without a mod. Empty reads no
    /// overrides.
    std::string_view profile_id{};
    /// The game runs on a Steam Deck whose screen refreshes this many times a second (60 on
    /// the LCD model, 90 on the OLED); 0 on every other machine.
    uint32_t steam_deck_panel_hz{};
    /// The platform the game is built for opens every window at the
    /// display's own pixel density, so that Native pixel density is always
    /// on.
    bool native_density_windows{};
};

/// Returns the highest unit limit the setting offers and keeps.
///
/// @param units the unit limits the game allows
/// @return highest_unit_limit, or the limits' maximum when that is higher
[[nodiscard]] constexpr uint16_t
highest_offered_unit_limit(const oa::data::limits::UnitsPerPlayer& units) noexcept {
    return units.maximum > highest_unit_limit ? units.maximum : highest_unit_limit;
}

/// Returns the settings a player has before changing any.
///
/// Escape opens the game menu by default on macOS with the player's own
/// preferences file; the unit limit is the installation's
/// (installation_unit_limit) with the player's own file, else
/// the limits' default_limit (default_unit_limit for 3.1c). On a Raspberry Pi with the player's own file the
/// maximum frame rate is raspberry_pi_frame_rate and enhanced
/// anti-aliasing is off. On a light machine with the player's own file the
/// maximum frame rate is light_machine_frame_rate, enhanced anti-aliasing is
/// off and the screen size is light_machine_screen_size, or
/// small_desktop_screen_size on a known desktop narrower or shorter than it.
/// Hardware acceleration is Full with the player's own file, on every
/// machine, and Off with a named one; whether the graphics card is used is
/// decided apart from the setting. Vertical sync is Off everywhere. Menu
/// scaling is Sharp everywhere. Mouse wheel zoom is On, Maximum zoom out
/// Automatic and Maximum zoom in 4x everywhere. Native pixel density is
/// Off, but On where the platform opens every window at native density.
/// Explosion flash is Full everywhere, as 3.1c draws it. Modern
/// fonts for game text are On with the player's own file and Off with a
/// named one; their outline and shadow are On, their background Off and
/// their size default_text_size everywhere. The language is the operating
/// system's choice with the player's own file and English, the game's own
/// default, with a named one. Developer Mode is Off, with no overrides. The
/// Touch section is the same everywhere: One-finger drag Automatic, Hold
/// delay default_touch_hold_ms, QUEUE and ADD Stay on, Haptics On,
/// Left-handed layout Off and Control size Standard. On a Steam Deck
/// (Inputs::steam_deck_panel_hz above 0) with the player's own file the
/// maximum frame rate is the screen's rate, held to lowest_frame_rate to
/// highest_frame_rate and put on its nearest stop, and Control size is
/// steam_deck_control_size; nothing else changes there. The Controller
/// section is the same everywhere: Scheme Trackpads, Right trackpad
/// relative, Pointer speed default_pointer_speed, Pointer acceleration Low,
/// Trackpad glide Off, Right stick Zoom and build pages, Magnetism On, Gyro
/// pointer Off, Gyro speed default_gyro_speed, Haptics Light, Button prompts
/// Automatic and Left-handed Off (oa/ui/pad_controls.hpp).
///
/// @param inputs the platform, the preferences file and the installation
/// @return the defaults
[[nodiscard]] EngineSettings default_settings(const Inputs& inputs);

/// Reads the settings from the preferences.
///
/// A key that is absent, or whose value is not a whole decimal number,
/// gives the default; a value out of a setting's range is clamped into it.
/// The ranges: path nodes base_path_search_nodes to
/// highest_path_search_nodes; unit limit the limits' minimum
/// (lowest_stored_unit_limit for 3.1c) to highest_offered_unit_limit; frame rate lowest_frame_rate to highest_frame_rate;
/// anti-aliasing the highest level not above the stored number, off below
/// 2; a switch is on for a number above 0. A value between a setting's
/// stops is kept as stored. The screen size is "desktop" or a size as
/// "WIDTHxHEIGHT" (screen_size_from_text); any other value gives the
/// default.
/// Hardware acceleration is "off", "basic" or "full"
/// (hardware_acceleration_from_text), or a whole number as the switch it
/// was before: Full above 0, else Off; any other value gives the default.
/// The mod is the stored folder, and the picked folder the one
/// remembered_picked_folder gives; a mod folder the game folder does not
/// offer is the picked folder whatever that key holds, so that the Mods
/// page lists it. The Language switches
/// (modern fonts, text outline, shadow and background) and Developer Mode
/// read as every
/// switch does, and the text size as every number, lowest_text_size to
/// highest_text_size. The language is stored_language's. The overrides are
/// read from the key that Inputs::profile_id ends, as
/// oa::data::mod_profile::read_overrides reads them: none without the key,
/// from a text the profile grammar does not read as a mapping (one that
/// names a hack twice among them), or with an empty profile id. In the
/// Touch section, One-finger drag and QUEUE and ADD read the words
/// touch_drag_text and touch_latches_text write, and any other value gives
/// the default; the hold delay reads as a number, put on its nearest stop
/// within its range (snapped_touch_hold_ms); Haptics and Left-handed layout
/// read as every switch does. Menu scaling reads the words
/// menu_scaling_text writes, and any other value gives the default; Native
/// pixel density reads as every switch does, but stays on where the
/// platform opens every window at native density. Explosion flash reads
/// "off", "reduced" or "full", and any other value gives the default.
/// Control size and the
/// Controller section's choices read the words their keys name ("standard",
/// "large", "larger"; "trackpads", "sticks"; "relative", "absolute"; "off",
/// "low", "high"; "zoom", "pointer", "nothing"; "off", "right-pad",
/// "right-stick", "always"; "off", "light", "strong"; "automatic",
/// "steam-deck", "xbox", "playstation", "nintendo", "off"), and any other
/// value gives the default; Pointer speed and Gyro speed read as numbers,
/// held to their ranges and put on their nearest stops (half a step rounds
/// up); Trackpad glide, Magnetism and Left-handed read as every switch does.
/// Maximum zoom out reads "automatic", "whole-map", "1/32", "1/16", "1/8",
/// "1/4" or "1/2", and Maximum zoom in "1", "2", "3" or "4"; any other value gives the
/// default. A stored value always wins over a Steam Deck's defaults.
///
/// @param values the preferences
/// @param inputs the platform, the preferences file and the installation
/// @param switch_alt 3.1c's SwitchAlt as the frontend's preferences hold it
/// @return the settings
[[nodiscard]] EngineSettings read_settings(
    const oa::platform::preferences::Values& values, const Inputs& inputs, bool switch_alt
);

/// Writes the settings a player chose into the preferences.
///
/// For each setting but switch_alt: after Restore defaults (`restored`), a
/// setting at its default has its key erased; otherwise a setting that
/// differs from `opened` has its key written, in decimal, a switch as 1 or
/// 0, the screen size as "desktop" or "WIDTHxHEIGHT", hardware
/// acceleration as "off", "basic" or "full", Menu scaling, One-finger drag
/// and QUEUE and ADD as their words (menu_scaling_text, touch_drag_text,
/// touch_latches_text), Explosion flash as "off", "reduced" or "full", the
/// hold delay in milliseconds, Maximum zoom out, Maximum zoom in, Control
/// size and the Controller section's choices as the words read_settings
/// reads, Pointer
/// speed and Gyro speed in percent, the mod and the picked folder as their
/// paths, or erased
/// for none; Restore defaults leaves the picked folder as it is. The picked
/// folder's key is then erased unless the mod key names the same folder,
/// a key an earlier save left included: once another mod or No Mod is
/// chosen, the picked folder is forgotten. The overrides, when they differ
/// from `opened`, are written under the key `profile_id` ends, as
/// oa::data::mod_profile::overrides_text writes them, or that key is
/// erased when none are left; Restore defaults leaves them as they are.
/// Every other key is left as it is. switch_alt is never written here:
/// 3.1c's SwitchAlt key goes with the frontend's own preferences.
///
/// @param[in,out] values the preferences
/// @param opened the settings when the dialog opened
/// @param chosen the settings the player keeps
/// @param defaults the defaults (default_settings)
/// @param restored Restore defaults was pressed while the dialog was open
/// @param profile_id the id the overrides are kept under (Inputs::profile_id);
///     empty writes no overrides
void write_settings(
    oa::platform::preferences::Values& values,
    const EngineSettings& opened,
    const EngineSettings& chosen,
    const EngineSettings& defaults,
    bool restored,
    std::string_view profile_id = {}
);

/// Returns the folder the player picked, as the preferences remember it.
///
/// A picked folder is remembered only while it is the mod played: the
/// picked folder's key (key::picked_mod_directory) counts while the mod key
/// (key::mod_directory) holds the same path; a key naming any other folder,
/// or with no mod stored, is stale and gives none.
///
/// @param values the preferences
/// @return the picked folder's path; empty for none
[[nodiscard]] std::string remembered_picked_folder(const oa::platform::preferences::Values& values);

/// Returns the language the preferences choose: the language key's value
/// when it is oa::data::languages::system_choice or the tag of a language
/// this build draws, matched without regard to case; else, a file without
/// the key or with any other value, the default.
///
/// @param values the preferences
/// @param players_own_profile the preferences file is the player's own
///     (Inputs::players_own_profile)
/// @return system_choice or a known tag, as the registry writes it; the
///     default is system_choice with the player's own file and "en" with a
///     named one
[[nodiscard]] std::string
stored_language(const oa::platform::preferences::Values& values, bool players_own_profile);

/// Returns how game text is drawn under the settings.
///
/// @param settings the settings in effect
/// @return modern fonts, their outline, shadow, background and size as the
///     settings' Language section sets them
[[nodiscard]] constexpr oa::present::TextStyle text_style(const EngineSettings& settings) noexcept {
    return {
        .modern_fonts = settings.modern_fonts,
        .outline = settings.text_outline,
        .shadow = settings.text_shadow,
        .background = settings.text_background,
        .size = settings.text_size,
    };
}

/// Returns the text the preferences keep a screen size as.
///
/// @param size the screen size
/// @return "desktop" for desktop_screen_size, else "WIDTHxHEIGHT" in decimal
[[nodiscard]] std::string screen_size_text(ScreenSize size);

/// Returns the screen size a preferences text names.
///
/// @param text the stored text
/// @return the size, when the text is "desktop" or "WIDTHxHEIGHT" as
///     screen_size_text writes it: whole decimal numbers with no sign or
///     leading zero joined by a lower-case "x", at least smallest_screen_size
///     and neither side above longest_screen_side; nothing otherwise
[[nodiscard]] std::optional<ScreenSize> screen_size_from_text(std::string_view text);

/// Returns the word the preferences and the command line keep a level of
/// hardware acceleration as.
///
/// @param level the level
/// @return "off", "basic" or "full"
[[nodiscard]] std::string_view hardware_acceleration_text(HardwareAcceleration level) noexcept;

/// Returns the level of hardware acceleration a word names.
///
/// @param text the word, in lower case as hardware_acceleration_text gives it
/// @return the level; nothing for any other text
[[nodiscard]] std::optional<HardwareAcceleration>
hardware_acceleration_from_text(std::string_view text) noexcept;

/// Returns the word the preferences keep a way of Menu scaling as.
///
/// @param scaling the way
/// @return "sharp", "whole-steps" or "unfiltered"
[[nodiscard]] std::string_view menu_scaling_text(MenuScaling scaling) noexcept;

/// Returns the way of Menu scaling a word names.
///
/// @param text the word, in lower case as menu_scaling_text gives it
/// @return the way; nothing for any other text
[[nodiscard]] std::optional<MenuScaling> menu_scaling_from_text(std::string_view text) noexcept;

/// Returns the word the preferences keep a way of One-finger drag as.
///
/// @param drag the way
/// @return "automatic", "box" or "scroll"
[[nodiscard]] std::string_view touch_drag_text(TouchDrag drag) noexcept;

/// Returns the way of One-finger drag a word names.
///
/// @param text the word, in lower case as touch_drag_text gives it
/// @return the way; nothing for any other text
[[nodiscard]] std::optional<TouchDrag> touch_drag_from_text(std::string_view text) noexcept;

/// Returns the word the preferences keep a way of QUEUE and ADD as.
///
/// @param latches the way
/// @return "stay-on" or "one-action"
[[nodiscard]] std::string_view touch_latches_text(TouchLatches latches) noexcept;

/// Returns the way of QUEUE and ADD a word names.
///
/// @param text the word, in lower case as touch_latches_text gives it
/// @return the way; nothing for any other text
[[nodiscard]] std::optional<TouchLatches> touch_latches_from_text(std::string_view text) noexcept;

/// Returns the unit limit an installation's totala.ini sets.
///
/// Reads [Preferences] UnitLimit, the section and key matched without
/// regard to case, from the first [Preferences] section; the first
/// UnitLimit line there counts. The value is its leading whole number, 0 when it has none or is negative,
/// clamped to the game's range, 3.1c's own 20 to 500 by default. Lines may end in CR LF or LF;
/// a line starting with ';' is a comment. Only the first
/// installation_ini_limit bytes are read.
///
/// @param ini_text the file's text
/// @param units the unit limits the game allows; the value is clamped to
///     their minimum and maximum
/// @return the limit in units per player; nothing when the file sets none
[[nodiscard]] std::optional<uint16_t> installation_unit_limit(
    std::string_view ini_text, const oa::data::limits::UnitsPerPlayer& units = {}
);

/// Returns the Pathfinding cycles a path credit shows as.
///
/// @param nodes path nodes a game tick
/// @return nodes over base_path_search_nodes, to the nearest whole number,
///     from 1 to highest_path_search_multiplier
[[nodiscard]] int32_t path_search_multiplier(int32_t nodes) noexcept;

/// Returns the path credit a match plays at.
///
/// The setting's Pathfinding cycles multiply the game's own credit, which a
/// mod's limits may raise (data::limits::PathSearch::nodes).
///
/// @param settings the settings in effect
/// @param shared_or_replay the match is played with other machines or
///     replays a recording
/// @param game_nodes the game's own path nodes a tick: base_path_search_nodes
///     for 3.1c
/// @return game_nodes in a shared game or a replay, else the setting's
///     path_search_nodes scaled by game_nodes / base_path_search_nodes,
///     truncated and held to data::limits::highest_path_search_nodes
[[nodiscard]] int32_t match_path_search_nodes(
    const EngineSettings& settings,
    bool shared_or_replay,
    int32_t game_nodes = base_path_search_nodes
) noexcept;

/// Why a setting cannot be changed now.
enum class Lock : uint8_t {
    none,        ///< it can be changed
    in_game,     ///< a game is running; it can be changed outside a game
    set_by_host, ///< a shared game or a replay decides it
    /// --max-fps decides the frame rate, --hardware-acceleration, in any of
    /// its forms, or --no-hardware-acceleration decides hardware
    /// acceleration, or --mod-dir or --base-game decides the mod, for this
    /// run
    command_line,
    unavailable, ///< nothing in the game could make the setting help this run
    set_by_mod,  ///< the mod allows no other value
    /// The modern fonts are off: the game's own fonts, which draw game text
    /// then, have fixed sizes.
    needs_modern_fonts,
    /// The platform opens every window at the display's own pixel density,
    /// so the setting is always on.
    always_on,
    /// The language shown needs it: its text draws in the modern fonts, or
    /// its pack asks for multiplayer chat in UTF-8. The setting shows On.
    set_by_language,
};

/// The game the dialog opens over.
struct GameState {
    bool in_game{};                      ///< a match is running
    bool shared_game{};                  ///< the match is played with other machines
    bool replay{};                       ///< the match replays a recording
    bool frame_rate_from_command_line{}; ///< --max-fps was given
    /// --hardware-acceleration, in any of its forms, or
    /// --no-hardware-acceleration was given.
    bool renderer_from_command_line{};
    /// Nothing in the game could have the graphics card scale this run's
    /// frames: the environment names a render driver, the renderer cannot,
    /// or the machine has under 2 GiB of memory.
    bool acceleration_unavailable{};
    /// The renderer cannot wait for the display, as far as the game can tell,
    /// or cannot change it without resetting its device.
    bool vertical_sync_unavailable{};
    /// 3.1c's command line names the language, which then decides it for the
    /// run.
    bool language_from_command_line{};
    /// --mod-dir or --base-game was given, which decides the mod the run
    /// plays.
    bool mod_from_command_line{};
    /// The platform opens every window at the display's own pixel density
    /// (Inputs::native_density_windows).
    bool native_density_windows{};
    /// --native-density was given, which opened this run's window at the
    /// display's own pixel density.
    bool native_density_from_command_line{};
};

/// What the dialog cannot change, and what its header says of the game.
struct Locks {
    Lock path_search{};           ///< Pathfinding cycles
    Lock unit_limit{};            ///< Unit limit
    Lock max_frame_rate{};        ///< Maximum frame rate
    bool shared_game{};           ///< the header says the shared game is still running
    Lock hardware_acceleration{}; ///< Hardware acceleration
    Lock vertical_sync{};         ///< Vertical sync
    Lock mex_snap{};              ///< the mod options' Mex snap radius
    Lock wreck_snap{};            ///< the mod options' Wreck snap radius
    /// Text size: the dialog itself locks it needs_modern_fonts while it
    /// shows Use modern fonts for game text Off.
    Lock text_size{};
    Lock language{}; ///< Language
    /// Use modern fonts for game text: the dialog itself locks it
    /// set_by_language while the language it shows draws in them.
    Lock modern_fonts{};
    /// Enable Unicode Multiplayer Chat: the dialog itself locks it
    /// set_by_language while a pack of the language it shows asks for it.
    Lock unicode_chat{};
    /// Mod: locked command_line, the stored choice stays shown and plays
    /// from a start without the flag that set it aside; locked in_game, the
    /// row shows the mod the game plays, and the main menu chooses another.
    Lock mod{};
    /// Native pixel density: locked command_line, the stored choice stays
    /// shown and takes effect from a start without the flag.
    Lock native_density{};
};

/// Returns the locks a game state puts on the settings.
///
/// Pathfinding cycles and Unit limit are locked during any game, as
/// set_by_host in a shared game or a replay; the maximum frame rate is
/// locked while --max-fps decides it. Hardware acceleration is locked
/// command_line while a flag decides it, else unavailable when nothing in
/// the game could help, and never by a game, so that it can always be set
/// to Off. Vertical sync is locked unavailable where the renderer
/// cannot wait for the display, else in_game in a shared game or a replay,
/// where its value holds until the match ends. The language is locked
/// command_line while 3.1c's command line names one, and never by a game:
/// it changes only what players read. The mod is locked command_line while
/// --mod-dir or --base-game decides it, else in_game during any game: it is
/// chosen from the main menu. Native pixel density is locked always_on where
/// the platform opens every window at native density, else command_line
/// while --native-density decides it, and never by a game: it takes effect
/// from the next start. No game locks Menu scaling.
///
/// @param state the game the dialog opens over
/// @return the locks
[[nodiscard]] Locks settings_locks(const GameState& state) noexcept;

} // namespace oa::ui::engine_settings
