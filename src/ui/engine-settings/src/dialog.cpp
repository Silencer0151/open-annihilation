// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The settings dialog's sections, rows and controls, where they lie, and
// what pointer and key events do to them.

#include "oa/ui/engine_settings/dialog.hpp"

#include "developer.hpp"
#include "geometry.hpp"

#include "oa/data/languages.hpp"
#include "oa/data/languages/interface_text.hpp"
#include "oa/data/mod_profile/overrides.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace oa::ui::engine_settings {

namespace pad_controls = oa::ui::pad_controls;

namespace geometry {

namespace {

/// Mods' one row: the list of the mods the game can play.
constexpr std::array<Setting, 1> kModsRows{Setting::mod};
/// Controls' rows: the zoom's limits right under the switch for the wheel,
/// and how far past the map's edges the view goes under them.
constexpr std::array<Setting, 6> kControlsRows{
    Setting::wheel_zoom,
    Setting::max_zoom_out,
    Setting::max_zoom_in,
    Setting::view_past_map_edge,
    Setting::escape_opens_menu,
    Setting::switch_alt,
};
/// Common Tweaks' rows: the player's own folder first.
constexpr std::array<Setting, 3> kCommonTweaksRows{
    Setting::user_folder,
    Setting::unit_limit,
    Setting::path_search,
};
/// Graphics' rows: how units look zoomed out last, the zoom they look so
/// from right under the way.
constexpr std::array<Setting, 10> kGraphicsRows{
    Setting::max_frame_rate,
    Setting::anti_aliasing,
    Setting::screen_size,
    Setting::hardware_acceleration,
    Setting::vertical_sync,
    Setting::menu_scaling,
    Setting::native_density,
    Setting::explosion_flash,
    Setting::zoomed_out_units,
    Setting::zoomed_out_after,
};
/// Language's rows: the language first, and the text size right
/// under the switch it needs.
constexpr std::array<Setting, 7> kLanguageRows{
    Setting::language,
    Setting::modern_fonts,
    Setting::text_size,
    Setting::text_outline,
    Setting::text_shadow,
    Setting::text_background,
    Setting::unicode_chat,
};
/// Touch's rows: how a finger's drag and hold work, then the latches, the
/// haptics, the side the controls stand on and their size.
constexpr std::array<Setting, 6> kTouchRows{
    Setting::touch_drag,
    Setting::touch_hold_delay,
    Setting::touch_latches,
    Setting::touch_haptics,
    Setting::touch_left_handed,
    Setting::touch_control_size,
};
/// Controller's rows: the scheme, the right trackpad's pointer, the right
/// stick and the gyro, what the pad feels and shows, the mirror, then the
/// rows it shares with Touch.
constexpr std::array<Setting, 15> kControllerRows{
    Setting::pad_scheme,
    Setting::pad_right_trackpad,
    Setting::pad_pointer_speed,
    Setting::pad_acceleration,
    Setting::pad_glide,
    Setting::pad_right_stick,
    Setting::pad_magnetism,
    Setting::pad_gyro,
    Setting::pad_gyro_speed,
    Setting::pad_haptics,
    Setting::pad_prompts,
    Setting::pad_left_handed,
    Setting::touch_control_size,
    Setting::touch_hold_delay,
    Setting::touch_latches,
};
/// Controller's rows under the Steam Input notice, while the gamepad
/// reaches the game through Steam Input.
constexpr std::array<Setting, kControllerRows.size() + 1> kSteamInputControllerRows = [] {
    std::array<Setting, kControllerRows.size() + 1> rows{};
    rows[0] = Setting::pad_steam_input_notice;
    for (std::size_t index = 0; index < kControllerRows.size(); ++index)
        rows[index + 1] = kControllerRows[index];
    return rows;
}();
/// Game files' rows: what is installed with its MANAGE… button, the
/// backups switch, and where the files are.
constexpr std::array<Setting, 3> kGameFilesRows{
    Setting::game_files_summary,
    Setting::game_files_backed_up,
    Setting::game_files_location,
};
/// Developer's rows, over its list.
constexpr std::array<Setting, developer_row_count> kDeveloperRows{
    Setting::developer_mode,
    Setting::frame_stats,
};
/// The mod's keys.
constexpr std::array<Setting, 3> kModKeysRows{
    Setting::snap_override_key,
    Setting::autoclick_key,
    Setting::rotate_build_key,
};
/// Patrolling builders.
constexpr std::array<Setting, 3> kModPatrolRows{
    Setting::patrol_hold,
    Setting::patrol_maneuver,
    Setting::patrol_roam,
};
/// Guarding builders.
constexpr std::array<Setting, 3> kModGuardRows{
    Setting::guard_hold,
    Setting::guard_maneuver,
    Setting::guard_roam,
};
/// The build tools and the mex snap.
constexpr std::array<Setting, 3> kModToolsRows{
    Setting::optimize_dt_rows,
    Setting::full_rings,
    Setting::mex_snap_radius,
};
/// The wreck snap, the chat and the resource bar.
constexpr std::array<Setting, 3> kModChatRows{
    Setting::wreck_snap_radius,
    Setting::chat_backdrop,
    Setting::panel_background,
};

/// The sections a dialog of the engine's settings lists, in the list's order.
struct EnginePageList {
    std::array<Page, most_listed_pages> pages{}; ///< the sections; the first `count` count
    std::size_t count{};                         ///< how many it lists
};

/// Returns the engine's sections a dialog lists, in the list's order: the
/// five that every dialog lists, then Touch, Controller and Game files where
/// each is listed, then Developer.
///
/// @param touch the dialog lists Touch
/// @param controller the dialog lists Controller
/// @param game_files the dialog lists Game files
/// @return the sections
constexpr EnginePageList engine_page_list(bool touch, bool controller, bool game_files) noexcept {
    EnginePageList list{};
    const auto add = [&list](Page page) { list.pages[list.count++] = page; };
    for (const Page page :
         {Page::mods, Page::controls, Page::common_tweaks, Page::language, Page::graphics})
        add(page);
    if (touch)
        add(Page::touch);
    if (controller)
        add(Page::controller);
    if (game_files)
        add(Page::game_files);
    add(Page::developer);
    return list;
}

/// Returns a list's place among kEnginePageLists.
///
/// @param touch the dialog lists Touch
/// @param controller the dialog lists Controller
/// @param game_files the dialog lists Game files
/// @return 0 to 7: Touch adds 1, Controller 2 and Game files 4
constexpr std::size_t
engine_page_list_index(bool touch, bool controller, bool game_files) noexcept {
    return (touch ? 1U : 0U) + (controller ? 2U : 0U) + (game_files ? 4U : 0U);
}

/// Every list of the engine's sections, by engine_page_list_index.
constexpr std::array<EnginePageList, 8> kEnginePageLists{
    engine_page_list(false, false, false),
    engine_page_list(true, false, false),
    engine_page_list(false, true, false),
    engine_page_list(true, true, false),
    engine_page_list(false, false, true),
    engine_page_list(true, false, true),
    engine_page_list(false, true, true),
    engine_page_list(true, true, true),
};
static_assert(kEnginePageLists.front().count == 6, "six sections without the optional three");
static_assert(
    kEnginePageLists.back().count == most_listed_pages,
    "the longest list holds the most sections a dialog lists"
);
/// The one section a Language dialog lists.
constexpr std::array<Page, 1> kLanguageTextPages{Page::language};

/// Tells whether every section's entry number is its place in the list of
/// the engine's settings with Touch and Controller, a list without either
/// only leaving its number out.
///
/// @return true when page_control follows the list with Touch and Controller
constexpr bool entries_follow_the_list() noexcept {
    const EnginePageList& list = kEnginePageLists[engine_page_list_index(true, true, false)];
    for (std::size_t index = 0; index < list.count; ++index)
        if (page_control(list.pages[index]) != static_cast<int32_t>(index))
            return false;
    return true;
}

static_assert(
    entries_follow_the_list(), "each entry's number is its place with Touch and Controller listed"
);
static_assert(
    page_control(Page::game_files) == page_control(Page::developer) + 1,
    "Game files' entry number follows Developer's, so Touch, Controller and Developer keep theirs"
);
/// The mod options' sections, in the list's order.
constexpr std::array<Page, 5> kModPages{
    Page::mod_keys, Page::mod_patrol, Page::mod_guard, Page::mod_tools, Page::mod_chat
};
/// The choices of a three-way mod option slider.
constexpr int32_t kChoiceStops = 3;

/// Returns a key's place among option_keys.
///
/// @param code the key's SDL key code
/// @return its index; 0 for a key not offered
int32_t option_key_index(uint32_t code) noexcept {
    for (std::size_t index = 0; index < option_keys.size(); ++index)
        if (option_keys[index].code == code)
            return static_cast<int32_t>(index);
    return 0;
}

/// Returns the mod option a key setting keeps its key in.
///
/// @param options the mod options
/// @param setting a key setting
/// @return the key's SDL key code
uint32_t& key_of(ModOptions& options, Setting setting) noexcept {
    if (setting == Setting::autoclick_key)
        return options.autoclick_key;
    if (setting == Setting::rotate_build_key)
        return options.rotate_build_key;
    return options.snap_override_key;
}

/// Returns the mod option a three-way setting keeps its choice in.
///
/// @param options the mod options
/// @param setting a patrol, guard or background setting
/// @return the choice, 0 to 2
uint8_t& choice_of(ModOptions& options, Setting setting) noexcept {
    switch (setting) {
    case Setting::patrol_hold:
        return options.patrol[0];
    case Setting::patrol_maneuver:
        return options.patrol[1];
    case Setting::patrol_roam:
        return options.patrol[2];
    case Setting::guard_hold:
        return options.guard[0];
    case Setting::guard_maneuver:
        return options.guard[1];
    case Setting::guard_roam:
        return options.guard[2];
    default:
        return options.panel_background;
    }
}

/// The lowest level that draws units finer and needs the warning hint.
constexpr AntiAliasing kDemandingLevel = AntiAliasing::x8;

/// Enhanced anti-aliasing's hint while frames are drawn in Full, by the
/// samples a pixel the graphics card draws the battlefield with: there the
/// processor's anti-aliasing never runs, and the row's level is the
/// samples across, as the renderer's texture limit and the memory allow
/// at the window's size; where they allow fewer, the second line says so.
constexpr std::array<std::string_view, 2> kFullAntiAliasingOff{
    "In Full the graphics card draws the view 1:1;", "a level draws it finer for smoother edges."
};
constexpr std::string_view kFullAntiAliasingScaled = "and scales them down for smoother edges.";
constexpr std::string_view kFullAntiAliasingCapped = "the most this window allows, scaled down.";
constexpr std::array<std::string_view, 2> kFullAntiAliasingTwice{
    "In Full the card draws 2x2 samples a pixel", kFullAntiAliasingScaled
};
constexpr std::array<std::string_view, 2> kFullAntiAliasingFourTimes{
    "In Full the card draws 4x4 samples a pixel", kFullAntiAliasingScaled
};
constexpr std::array<std::string_view, 2> kFullAntiAliasingEightTimes{
    "In Full the card draws 8x8 samples a pixel", kFullAntiAliasingScaled
};
constexpr std::array<std::string_view, 2> kFullAntiAliasingSixteenTimes{
    "In Full the card draws 16x16 samples a pixel", kFullAntiAliasingScaled
};

/// A switch setting and the member of EngineSettings, or of its mod
/// options, it is.
struct SwitchMember {
    Setting setting{};               ///< the switch
    bool EngineSettings::* member{}; ///< its value; null for a mod option's
    bool ModOptions::* mod_member{}; ///< a mod option's value; null for the engine's
};

/// Every switch and its value: the one table switch_on and set_switch read.
constexpr std::array<SwitchMember, 21> kSwitches{{
    {Setting::wheel_zoom, &EngineSettings::wheel_zoom, nullptr},
    {Setting::escape_opens_menu, &EngineSettings::escape_opens_menu, nullptr},
    {Setting::switch_alt, &EngineSettings::switch_alt, nullptr},
    {Setting::developer_mode, &EngineSettings::developer_mode, nullptr},
    {Setting::frame_stats, &EngineSettings::frame_stats, nullptr},
    {Setting::vertical_sync, &EngineSettings::vertical_sync, nullptr},
    {Setting::native_density, &EngineSettings::native_density, nullptr},
    {Setting::modern_fonts, &EngineSettings::modern_fonts, nullptr},
    {Setting::text_outline, &EngineSettings::text_outline, nullptr},
    {Setting::text_shadow, &EngineSettings::text_shadow, nullptr},
    {Setting::text_background, &EngineSettings::text_background, nullptr},
    {Setting::unicode_chat, &EngineSettings::unicode_chat, nullptr},
    {Setting::touch_haptics, &EngineSettings::touch_haptics, nullptr},
    {Setting::touch_left_handed, &EngineSettings::touch_left_handed, nullptr},
    {Setting::pad_glide, &EngineSettings::pad_glide, nullptr},
    {Setting::pad_magnetism, &EngineSettings::pad_magnetism, nullptr},
    {Setting::pad_left_handed, &EngineSettings::pad_left_handed, nullptr},
    {Setting::game_files_backed_up, &EngineSettings::game_files_backed_up, nullptr},
    {Setting::optimize_dt_rows, nullptr, &ModOptions::optimize_dt_rows},
    {Setting::full_rings, nullptr, &ModOptions::full_rings},
    {Setting::chat_backdrop, nullptr, &ModOptions::chat_backdrop},
}};

/// Hardware acceleration's captions, in hardware_acceleration_levels' order.
constexpr std::array<std::string_view, 3> kAccelerationCaptions{"Off", "Basic", "Full"};
static_assert(
    kAccelerationCaptions.size() == hardware_acceleration_levels.size(),
    "every level of hardware acceleration has its caption"
);
/// Menu scaling's captions, in menu_scaling_choices' order.
constexpr std::array<std::string_view, 3> kMenuScalingCaptions{
    "Sharp", "Whole steps", "Unfiltered"
};
static_assert(
    kMenuScalingCaptions.size() == menu_scaling_choices.size(),
    "every way of Menu scaling has its caption"
);
/// Explosion flash's captions, in explosion_flash_choices' order.
constexpr std::array<std::string_view, 3> kExplosionFlashCaptions{"Off", "Reduced", "Full"};
static_assert(
    kExplosionFlashCaptions.size() == explosion_flash_choices.size(),
    "every level of Explosion flash has its caption"
);
/// View past the map's edge's captions, in view_past_map_edge_choices' order.
constexpr std::array<std::string_view, 3> kViewPastMapEdgeCaptions{"Off", "25%", "50%"};
static_assert(
    kViewPastMapEdgeCaptions.size() == view_past_map_edge_choices.size(),
    "every choice of View past the map's edge has its caption"
);
/// Zoomed out units' captions, in zoomed_out_units_choices' order.
constexpr std::array<std::string_view, 3> kZoomedOutUnitsCaptions{"Rendered", "Dots", "Icons"};
static_assert(
    kZoomedOutUnitsCaptions.size() == zoomed_out_units_choices.size(),
    "every way of Zoomed out units has its caption"
);
/// One-finger drag's captions, in touch_drag_choices' order.
constexpr std::array<std::string_view, 3> kTouchDragCaptions{"Automatic", "Box", "Scroll"};
static_assert(
    kTouchDragCaptions.size() == touch_drag_choices.size(),
    "every way of One-finger drag has its caption"
);
/// QUEUE and ADD's captions, in touch_latches_choices' order.
constexpr std::array<std::string_view, 2> kTouchLatchesCaptions{"Stay on", "One action"};
static_assert(
    kTouchLatchesCaptions.size() == touch_latches_choices.size(),
    "every way of QUEUE and ADD has its caption"
);

/// Control size's captions, in control_size_choices' order.
constexpr std::array<std::string_view, 3> kControlSizeCaptions{"Standard", "Large", "Larger"};
static_assert(
    kControlSizeCaptions.size() == control_size_choices.size(), "every Control size has its caption"
);
/// The schemes, in the order Scheme's strip offers them.
constexpr std::array<pad_controls::Scheme, 2> kSchemeChoices{
    pad_controls::Scheme::trackpads, pad_controls::Scheme::sticks
};
/// Scheme's captions, in kSchemeChoices' order.
constexpr std::array<std::string_view, 2> kSchemeCaptions{"Trackpads", "Sticks"};
/// The right trackpad's ways, in the order Right trackpad's strip offers them.
constexpr std::array<pad_controls::RightTrackpad, 2> kRightTrackpadChoices{
    pad_controls::RightTrackpad::relative, pad_controls::RightTrackpad::absolute
};
/// Right trackpad's captions, in kRightTrackpadChoices' order: the pointer,
/// relative and absolute.
constexpr std::array<std::string_view, 2> kRightTrackpadCaptions{"Relative", "Absolute"};
/// The accelerations, in the order Pointer acceleration's strip offers them.
constexpr std::array<pad_controls::Acceleration, 3> kAccelerationChoices{
    pad_controls::Acceleration::off,
    pad_controls::Acceleration::low,
    pad_controls::Acceleration::high
};
/// Pointer acceleration's captions, in kAccelerationChoices' order.
constexpr std::array<std::string_view, 3> kPadAccelerationCaptions{"Off", "Low", "High"};
/// The right stick's roles, in the order Right stick's strip offers them.
constexpr std::array<pad_controls::RightStick, 3> kRightStickChoices{
    pad_controls::RightStick::zoom_and_pages,
    pad_controls::RightStick::pointer,
    pad_controls::RightStick::nothing
};
/// Right stick's captions, in kRightStickChoices' order: Zoom is zoom and
/// build pages, which its hint says in full.
constexpr std::array<std::string_view, 3> kRightStickCaptions{"Zoom", "Pointer", "Nothing"};
/// The gyro's ways, in the order Gyro pointer's drop-down offers them.
constexpr std::array<pad_controls::Gyro, 4> kGyroChoices{
    pad_controls::Gyro::off,
    pad_controls::Gyro::right_pad_touched,
    pad_controls::Gyro::right_stick_touched,
    pad_controls::Gyro::always
};
/// Gyro pointer's choices' texts, in kGyroChoices' order.
constexpr std::array<std::string_view, 4> kGyroCaptions{
    "Off", "While the right pad is touched", "While the right stick is touched", "Always"
};
/// The haptics' strengths, in the order Haptics' strip offers them.
constexpr std::array<pad_controls::Haptics, 3> kHapticsChoices{
    pad_controls::Haptics::off, pad_controls::Haptics::light, pad_controls::Haptics::strong
};
/// The Controller section's Haptics' captions, in kHapticsChoices' order.
constexpr std::array<std::string_view, 3> kPadHapticsCaptions{"Off", "Light", "Strong"};
/// The button prompts, in the order Button prompts' drop-down offers them.
constexpr std::array<pad_controls::Prompts, 6> kPromptsChoices{
    pad_controls::Prompts::automatic,
    pad_controls::Prompts::steam_deck,
    pad_controls::Prompts::xbox,
    pad_controls::Prompts::playstation,
    pad_controls::Prompts::nintendo,
    pad_controls::Prompts::off
};
/// Button prompts' choices' texts, in kPromptsChoices' order.
constexpr std::array<std::string_view, 6> kPromptsCaptions{
    "Automatic", "Steam Deck", "Xbox", "PlayStation", "Nintendo", "Off"
};

/// Maximum zoom out's choices' texts, in zoom_out_limits' order.
constexpr std::array<std::string_view, 7> kZoomOutCaptions{
    "Automatic", "Whole map", "1/32", "1/16", "1/8", "1/4", "1/2"
};
static_assert(
    kZoomOutCaptions.size() == zoom_out_limits.size(),
    "every Maximum zoom out choice has its caption"
);
/// After zoom's choices' texts, in zoomed_out_afters' order.
constexpr std::array<std::string_view, 7> kZoomedOutAfterCaptions{
    "1/2", "1/3", "1/4", "1/6", "1/8", "1/12", "1/16"
};
static_assert(
    kZoomedOutAfterCaptions.size() == zoomed_out_afters.size(),
    "every After zoom choice has its caption"
);
/// Maximum zoom in's choices' texts, in zoom_in_limits' order.
constexpr std::array<std::string_view, 4> kZoomInCaptions{"None", "2x", "3x", "4x"};
static_assert(
    kZoomInCaptions.size() == zoom_in_limits.size(), "every Maximum zoom in choice has its caption"
);

/// Returns a choice's place among the choices a strip offers.
///
/// @param choices the strip's choices, left to right
/// @param chosen the choice
/// @return its index; 0 for a choice not offered
template <typename Choice, std::size_t Count>
std::size_t choice_place(const std::array<Choice, Count>& choices, Choice chosen) noexcept {
    const auto found = std::find(choices.begin(), choices.end(), chosen);
    return found == choices.end() ? 0 : static_cast<std::size_t>(found - choices.begin());
}

/// Returns a switch's entry in the table.
///
/// @param setting the setting
/// @return its entry; null for a setting that is not a switch
const SwitchMember* switch_member(Setting setting) noexcept {
    for (const SwitchMember& entry : kSwitches)
        if (entry.setting == setting)
            return &entry;
    return nullptr;
}

/// The texts of Hardware acceleration's status, two lines for each state;
/// an empty second line is the reach line, which says what the graphics
/// card does on this machine.
struct StatusText {
    AccelerationState state{}; ///< the state
    std::string_view first;    ///< what runs, or why not
    std::string_view second;   ///< what draws the view or what to do; empty for the reach
};

/// The second line of a state the processor draws in.
constexpr std::string_view kProcessorDraws = "The processor draws and scales the view.";
/// The first line of the Off states.
constexpr std::string_view kOff = "Off: the processor draws and scales the view.";
/// The second line of a state that setting it to Off and back, or Restore
/// defaults, may lift.
constexpr std::string_view kRetry = "Set it to Off and back, or restore defaults.";
/// The second line of a state at a start that passed over a failed driver.
constexpr std::string_view kDriverSkipped = "A failed graphics driver is skipped.";
/// The first line of a machine under 2 GiB.
constexpr std::string_view kNeedsMemory = "Not in use: it needs at least 2 GB of memory.";

/// The second line of a state in which Full was asked for and Basic is in
/// use, that setting it to Off and back, or Restore defaults, may lift.
constexpr std::string_view kRetryFull = "Set it to Off and back, or restore defaults.";
/// The first line of Full in use.
constexpr std::string_view kFullInUse = "Full in use: the graphics card draws the view.";

/// Every state's status, in AccelerationState's order.
constexpr std::array<StatusText, 25> kStatusTexts{{
    {AccelerationState::off_driver_skipped, kOff, kDriverSkipped},
    {AccelerationState::needs_memory_driver_skipped, kNeedsMemory, kDriverSkipped},
    {AccelerationState::needs_memory, kNeedsMemory, kProcessorDraws},
    {AccelerationState::off_by_setting, kOff, "Basic lets the graphics card scale it evenly."},
    {AccelerationState::off_by_command_line, kOff, "For this run only. The setting is kept."},
    {AccelerationState::environment_driver,
     "Not in use: the environment names a driver.",
     kProcessorDraws},
    {AccelerationState::too_little_memory,
     "Not in use: there is too little memory.",
     kProcessorDraws},
    {AccelerationState::waiting_for_game_end,
     "Off for this game: in a shared game, Basic",
     "takes effect from the next game."},
    {AccelerationState::engine_error, "Not in use: an error stopped it for this run.", kRetry},
    {AccelerationState::driver_failed, "Not in use: the graphics driver failed.", kRetry},
    {AccelerationState::game_stopped, "Not in use: the game stopped while using it.", kRetry},
    {AccelerationState::no_usable_card,
     "Not in use: no usable graphics card was found.",
     kProcessorDraws},
    {AccelerationState::lacks_feature,
     "Not in use: the graphics card lacks a feature.",
     kProcessorDraws},
    {AccelerationState::cannot_save,
     "Not in use: the game cannot save its files.",
     kProcessorDraws},
    {AccelerationState::next_start,
     "Takes effect from the next start.",
     "The processor draws and scales the view until then."},
    {AccelerationState::full_cannot_save, "Basic in use: the game cannot save its files.", {}},
    {AccelerationState::full_too_little_memory,
     "Basic in use: there is too little memory for Full.",
     {}},
    {AccelerationState::full_stopped, "Basic in use: Full stopped for this run.", kRetryFull},
    {AccelerationState::full_failed_before,
     "Basic in use: Full failed before on this driver.",
     kRetryFull},
    {AccelerationState::full_lacks_feature,
     "Basic in use: the card lacks a feature Full needs.",
     {}},
    {AccelerationState::full_waiting_for_game_end,
     "Basic for this game: in a shared game, Full",
     "takes effect from the next game."},
    {AccelerationState::in_use_on_another_driver,
     "Basic in use, on another driver: one failed.",
     {}},
    {AccelerationState::in_use_no_smoothing,
     "Basic in use; no smoothing when zoomed out here.",
     {}},
    {AccelerationState::full_in_use, kFullInUse, {}},
    {AccelerationState::in_use, "Basic in use.", {}},
}};

/// Tells whether kStatusTexts holds every state once, in AccelerationState's order.
///
/// @return true when each entry's state is its index
constexpr bool status_texts_in_order() noexcept {
    for (std::size_t index = 0; index < kStatusTexts.size(); ++index)
        if (static_cast<std::size_t>(kStatusTexts[index].state) != index)
            return false;
    return kStatusTexts.size() == static_cast<std::size_t>(AccelerationState::in_use) + 1;
}

static_assert(status_texts_in_order(), "every state of Hardware acceleration has its status");

/// The first line of AccelerationState::waiting_for_game_end for Full in a
/// shared game; kStatusTexts holds Basic's.
constexpr std::string_view kWaitingForFull = "Off for this game: in a shared game, Full";
/// The first line of AccelerationState::waiting_for_game_end for Basic in a replay.
constexpr std::string_view kWaitingInReplay = "Off for this game: in a replay, Basic";
/// The first line of AccelerationState::waiting_for_game_end for Full in a replay.
constexpr std::string_view kWaitingForFullInReplay = "Off for this game: in a replay, Full";
/// The first line of AccelerationState::full_waiting_for_game_end in a
/// replay; kStatusTexts holds a shared game's.
constexpr std::string_view kBasicWaitingForFullInReplay = "Basic for this game: in a replay, Full";
/// The second line of Full in use, by its anti-aliasing: none, and 2, 4, 8
/// and 16 samples across.
constexpr std::string_view kFullReach = "Smoothed at every zoom.";
constexpr std::string_view kFullReachTwice = "Smoothed at every zoom; 2x2 samples a pixel.";
constexpr std::string_view kFullReachFourfold = "Smoothed at every zoom; 4x4 samples a pixel.";
constexpr std::string_view kFullReachEightfold = "Smoothed at every zoom; 8x8 samples a pixel.";
constexpr std::string_view kFullReachSixteenfold = "Smoothed at every zoom; 16x16 samples a pixel.";
/// The anti-aliasing the lines above name.
constexpr uint8_t kSupersampleTwice = 2;
constexpr uint8_t kSupersampleFourfold = 4;
constexpr uint8_t kSupersampleEightfold = 8;
constexpr uint8_t kSupersampleSixteenfold = 16;

/// Returns the first line of AccelerationState::waiting_for_game_end: the
/// match it waits for, and the level that takes effect after it.
///
/// @param acceleration the status
/// @return the line; Basic's for a status that asks for no more
std::string_view waiting_line(const AccelerationStatus& acceleration) noexcept {
    if (acceleration.state == AccelerationState::full_waiting_for_game_end)
        return acceleration.replay ? kBasicWaitingForFullInReplay
                                   : kStatusTexts[static_cast<std::size_t>(
                                                      AccelerationState::full_waiting_for_game_end
                                                  )]
                                         .first;
    const bool full = acceleration.asked == HardwareAcceleration::full;
    if (acceleration.replay)
        return full ? kWaitingForFullInReplay : kWaitingInReplay;
    return full ? kWaitingForFull
                : kStatusTexts[static_cast<std::size_t>(AccelerationState::waiting_for_game_end)]
                      .first;
}

/// Tells whether a state is Full in use, whose second line names its
/// anti-aliasing rather than the reach.
///
/// @param state the state
/// @return true for Full in use
bool full_in_use(AccelerationState state) noexcept {
    return state == AccelerationState::full_in_use;
}

/// Returns the second line of Full in use: smoothed at every zoom, with its
/// anti-aliasing where there is any.
///
/// @param supersample the samples a pixel across; 1 for no anti-aliasing
/// @return the line
std::string_view full_line(uint8_t supersample) noexcept {
    if (supersample >= kSupersampleSixteenfold)
        return kFullReachSixteenfold;
    if (supersample >= kSupersampleEightfold)
        return kFullReachEightfold;
    if (supersample >= kSupersampleFourfold)
        return kFullReachFourfold;
    if (supersample >= kSupersampleTwice)
        return kFullReachTwice;
    return kFullReach;
}

/// Returns the reach line: what the graphics card does on this machine.
///
/// @param reach the reach
/// @return the line
std::string_view reach_line(AccelerationReach reach) noexcept {
    switch (reach) {
    case AccelerationReach::menus:
        return "It scales the menus and the interface evenly.";
    case AccelerationReach::zoomed_in:
        return "It scales the interface and zoomed-in view evenly.";
    case AccelerationReach::zoomed_out:
        return "It scales evenly and smooths the zoomed-out view.";
    case AccelerationReach::nearest_zoomed_out:
        return "It smooths the zoomed-out view.";
    case AccelerationReach::nearest_none:
        return "Here the view is drawn as when it is off.";
    }
    return {};
}

/// Returns a screen size's place among the Screen size slider's stops.
///
/// @param size the screen size
/// @param offered_sizes the stops, in order
/// @return its index; 0, the first stop's, for a size not offered
int32_t screen_size_index(ScreenSize size, std::span<const ScreenSize> offered_sizes) noexcept {
    const auto found = std::find(offered_sizes.begin(), offered_sizes.end(), size);
    return found == offered_sizes.end() ? 0 : static_cast<int32_t>(found - offered_sizes.begin());
}

/// Returns a setting's lock, a check's own section's when it gives one.
///
/// @param locks the dialog's locks
/// @param setting the setting
/// @param section a check's own section; null for the dialog's
/// @return why it cannot be changed now
Lock row_lock(const Locks& locks, Setting setting, const SectionHooks* section) {
    const Lock lock = lock_of(locks, setting);
    if (section == nullptr || section->lock == nullptr)
        return lock;
    return section->lock(section->context, setting, lock);
}

/// Tells whether a setting's hint lines are its status, as a check's own
/// section has it when it says.
///
/// @param setting the setting
/// @param section a check's own section; null for the dialog's
/// @return true when its hint lines are its status
bool row_hint_is_status(Setting setting, const SectionHooks* section) {
    if (section == nullptr || section->hint_is_status == nullptr)
        return hint_is_status(setting);
    return section->hint_is_status(section->context, setting);
}

/// Returns a section's place among Dialog::scroll.
///
/// @param page the section
/// @return its index, below page_count
std::size_t scroll_index(Page page) noexcept {
    return std::min(static_cast<std::size_t>(page), page_count - 1);
}

/// Returns a whole number of a range's steps, rounded to the nearest.
///
/// @param value the value
/// @param lowest the range's lowest value
/// @param step the step
/// @return (value - lowest) / step, rounded half up
int32_t steps_from(int64_t value, int64_t lowest, int64_t step) noexcept {
    return static_cast<int32_t>((value - lowest + step / 2) / step);
}

} // namespace

bool is_slider(Setting setting) noexcept {
    switch (setting) {
    case Setting::path_search:
    case Setting::unit_limit:
    case Setting::max_frame_rate:
    case Setting::screen_size:
    case Setting::snap_override_key:
    case Setting::autoclick_key:
    case Setting::rotate_build_key:
    case Setting::patrol_hold:
    case Setting::patrol_maneuver:
    case Setting::patrol_roam:
    case Setting::guard_hold:
    case Setting::guard_maneuver:
    case Setting::guard_roam:
    case Setting::mex_snap_radius:
    case Setting::wreck_snap_radius:
    case Setting::panel_background:
    case Setting::text_size:
    case Setting::touch_hold_delay:
    case Setting::pad_pointer_speed:
    case Setting::pad_gyro_speed:
        return true;
    default:
        return false;
    }
}

Slider slider_of(
    Setting setting, uint16_t highest_offered_unit, std::span<const ScreenSize> offered_sizes
) noexcept {
    switch (setting) {
    case Setting::path_search:
        return Slider{highest_path_search_multiplier};
    case Setting::unit_limit:
        return Slider{(highest_offered_unit - lowest_unit_limit) / unit_limit_step + 1};
    case Setting::max_frame_rate:
        return Slider{
            static_cast<int32_t>((highest_frame_rate - lowest_frame_rate) / frame_rate_step + 1)
        };
    case Setting::screen_size:
        return Slider{std::max(static_cast<int32_t>(offered_sizes.size()), int32_t{1})};
    case Setting::snap_override_key:
    case Setting::autoclick_key:
    case Setting::rotate_build_key:
        return Slider{static_cast<int32_t>(option_keys.size())};
    case Setting::patrol_hold:
    case Setting::patrol_maneuver:
    case Setting::patrol_roam:
    case Setting::guard_hold:
    case Setting::guard_maneuver:
    case Setting::guard_roam:
    case Setting::panel_background:
        return Slider{kChoiceStops};
    case Setting::mex_snap_radius:
    case Setting::wreck_snap_radius:
        return Slider{most_snap_radius + 1};
    case Setting::text_size:
        return Slider{(highest_text_size - lowest_text_size) / text_size_step + 1};
    case Setting::touch_hold_delay:
        return Slider{static_cast<int32_t>(
            (highest_touch_hold_ms - lowest_touch_hold_ms) / touch_hold_step_ms + 1
        )};
    case Setting::pad_pointer_speed:
        return Slider{static_cast<int32_t>(
            (pad_controls::highest_pointer_speed - pad_controls::lowest_pointer_speed) /
                pad_controls::pointer_speed_step +
            1
        )};
    case Setting::pad_gyro_speed:
        return Slider{static_cast<int32_t>(
            (pad_controls::highest_gyro_speed - pad_controls::lowest_gyro_speed) /
                pad_controls::gyro_speed_step +
            1
        )};
    default:
        return Slider{2};
    }
}

int32_t stops_of(
    const EngineSettings& settings,
    Setting setting,
    uint16_t highest_offered_unit,
    std::span<const ScreenSize> offered_sizes
) noexcept {
    const auto& options = settings.mod_options;
    if (setting == Setting::mex_snap_radius)
        return std::clamp(options.mex_snap_most, int32_t{1}, most_snap_radius) + 1;
    if (setting == Setting::wreck_snap_radius)
        return std::clamp(options.wreck_snap_most, int32_t{1}, most_snap_radius) + 1;
    return slider_of(setting, highest_offered_unit, offered_sizes).stops;
}

int32_t stop_of(
    const EngineSettings& settings,
    Setting setting,
    uint16_t highest_offered_unit,
    std::span<const ScreenSize> offered_sizes
) noexcept {
    const int32_t last = stops_of(settings, setting, highest_offered_unit, offered_sizes) - 1;
    int32_t stop = 0;
    switch (setting) {
    case Setting::path_search:
        stop = path_search_multiplier(settings.path_search_nodes) - 1;
        break;
    case Setting::unit_limit:
        stop = steps_from(settings.unit_limit, lowest_unit_limit, unit_limit_step);
        break;
    case Setting::max_frame_rate:
        stop = steps_from(settings.max_frame_rate, lowest_frame_rate, frame_rate_step);
        break;
    case Setting::screen_size:
        stop = screen_size_index(settings.screen_size, offered_sizes);
        break;
    case Setting::snap_override_key:
    case Setting::autoclick_key:
    case Setting::rotate_build_key: {
        auto options = settings.mod_options;
        stop = option_key_index(key_of(options, setting));
        break;
    }
    case Setting::patrol_hold:
    case Setting::patrol_maneuver:
    case Setting::patrol_roam:
    case Setting::guard_hold:
    case Setting::guard_maneuver:
    case Setting::guard_roam:
    case Setting::panel_background: {
        auto options = settings.mod_options;
        stop = choice_of(options, setting);
        break;
    }
    case Setting::mex_snap_radius:
        stop = settings.mod_options.mex_snap_radius;
        break;
    case Setting::wreck_snap_radius:
        stop = settings.mod_options.wreck_snap_radius;
        break;
    case Setting::text_size:
        stop = steps_from(settings.text_size, lowest_text_size, text_size_step);
        break;
    case Setting::touch_hold_delay:
        stop = steps_from(settings.touch_hold_ms, lowest_touch_hold_ms, touch_hold_step_ms);
        break;
    case Setting::pad_pointer_speed:
        stop = steps_from(
            settings.pad_pointer_speed,
            pad_controls::lowest_pointer_speed,
            pad_controls::pointer_speed_step
        );
        break;
    case Setting::pad_gyro_speed:
        stop = steps_from(
            settings.pad_gyro_speed, pad_controls::lowest_gyro_speed, pad_controls::gyro_speed_step
        );
        break;
    default:
        break;
    }
    return std::clamp(stop, int32_t{0}, last);
}

void set_stop(
    EngineSettings& settings,
    Setting setting,
    int32_t stop,
    uint16_t highest_offered_unit,
    std::span<const ScreenSize> offered_sizes
) noexcept {
    const int32_t clamped = std::clamp(
        stop, int32_t{0}, stops_of(settings, setting, highest_offered_unit, offered_sizes) - 1
    );
    auto& options = settings.mod_options;
    switch (setting) {
    case Setting::path_search:
        settings.path_search_nodes = base_path_search_nodes * (clamped + 1);
        break;
    case Setting::unit_limit:
        settings.unit_limit = static_cast<uint16_t>(lowest_unit_limit + clamped * unit_limit_step);
        break;
    case Setting::max_frame_rate:
        settings.max_frame_rate =
            lowest_frame_rate + static_cast<uint32_t>(clamped) * frame_rate_step;
        break;
    case Setting::screen_size:
        if (!offered_sizes.empty())
            settings.screen_size = offered_sizes[static_cast<std::size_t>(clamped)];
        break;
    case Setting::snap_override_key:
    case Setting::autoclick_key:
    case Setting::rotate_build_key:
        key_of(options, setting) = option_keys[static_cast<std::size_t>(clamped)].code;
        break;
    case Setting::patrol_hold:
    case Setting::patrol_maneuver:
    case Setting::patrol_roam:
    case Setting::guard_hold:
    case Setting::guard_maneuver:
    case Setting::guard_roam:
    case Setting::panel_background:
        choice_of(options, setting) = static_cast<uint8_t>(clamped);
        break;
    case Setting::mex_snap_radius:
        options.mex_snap_radius = std::min(clamped, std::max(options.mex_snap_most, int32_t{0}));
        break;
    case Setting::wreck_snap_radius:
        options.wreck_snap_radius =
            std::min(clamped, std::max(options.wreck_snap_most, int32_t{0}));
        break;
    case Setting::text_size:
        settings.text_size = lowest_text_size + clamped * text_size_step;
        break;
    case Setting::touch_hold_delay:
        settings.touch_hold_ms =
            lowest_touch_hold_ms + static_cast<uint32_t>(clamped) * touch_hold_step_ms;
        break;
    case Setting::pad_pointer_speed:
        settings.pad_pointer_speed =
            pad_controls::lowest_pointer_speed +
            static_cast<uint32_t>(clamped) * pad_controls::pointer_speed_step;
        break;
    case Setting::pad_gyro_speed:
        settings.pad_gyro_speed = pad_controls::lowest_gyro_speed +
                                  static_cast<uint32_t>(clamped) * pad_controls::gyro_speed_step;
        break;
    default:
        break;
    }
}

bool is_strip(Setting setting) noexcept {
    switch (setting) {
    case Setting::anti_aliasing:
    case Setting::hardware_acceleration:
    case Setting::menu_scaling:
    case Setting::explosion_flash:
    case Setting::zoomed_out_units:
    case Setting::view_past_map_edge:
    case Setting::touch_drag:
    case Setting::touch_latches:
    case Setting::touch_control_size:
    case Setting::pad_scheme:
    case Setting::pad_right_trackpad:
    case Setting::pad_acceleration:
    case Setting::pad_right_stick:
    case Setting::pad_haptics:
        return true;
    default:
        return false;
    }
}

Strip strip_of(Setting setting) noexcept {
    switch (setting) {
    case Setting::anti_aliasing:
        return Strip{anti_aliasing_levels.size(), level_width};
    case Setting::hardware_acceleration:
        return Strip{hardware_acceleration_levels.size(), acceleration_level_width};
    case Setting::menu_scaling:
        return Strip{menu_scaling_choices.size(), menu_scaling_level_width};
    case Setting::explosion_flash:
        return Strip{explosion_flash_choices.size(), explosion_flash_level_width};
    case Setting::zoomed_out_units:
        return Strip{
            zoomed_out_units_choices.size(), zoomed_out_units_level_width, offered_zoomed_out_units
        };
    case Setting::view_past_map_edge:
        return Strip{view_past_map_edge_choices.size(), view_past_map_edge_level_width};
    case Setting::touch_drag:
        return Strip{touch_drag_choices.size(), touch_drag_level_width};
    case Setting::touch_latches:
        return Strip{touch_latches_choices.size(), touch_latches_level_width};
    case Setting::touch_control_size:
        return Strip{control_size_choices.size(), control_size_level_width};
    case Setting::pad_scheme:
        return Strip{kSchemeChoices.size(), scheme_level_width};
    case Setting::pad_right_trackpad:
        return Strip{kRightTrackpadChoices.size(), right_trackpad_level_width};
    case Setting::pad_acceleration:
        return Strip{kAccelerationChoices.size(), pad_acceleration_level_width};
    case Setting::pad_right_stick:
        return Strip{kRightStickChoices.size(), right_stick_level_width};
    case Setting::pad_haptics:
        return Strip{kHapticsChoices.size(), pad_haptics_level_width};
    default:
        return Strip{};
    }
}

std::size_t strip_level(const EngineSettings& settings, Setting setting) noexcept {
    switch (setting) {
    case Setting::anti_aliasing:
        return level_index(settings.anti_aliasing);
    case Setting::hardware_acceleration:
        return choice_place(hardware_acceleration_levels, settings.hardware_acceleration);
    case Setting::menu_scaling:
        return choice_place(menu_scaling_choices, settings.menu_scaling);
    case Setting::explosion_flash:
        return choice_place(explosion_flash_choices, settings.explosion_flash);
    case Setting::zoomed_out_units:
        return choice_place(zoomed_out_units_choices, settings.zoomed_out_units);
    case Setting::view_past_map_edge:
        return choice_place(view_past_map_edge_choices, settings.view_past_map_edge);
    case Setting::touch_drag:
        return choice_place(touch_drag_choices, settings.touch_drag);
    case Setting::touch_latches:
        return choice_place(touch_latches_choices, settings.touch_latches);
    case Setting::touch_control_size:
        return choice_place(control_size_choices, settings.touch_control_size);
    case Setting::pad_scheme:
        return choice_place(kSchemeChoices, settings.pad_scheme);
    case Setting::pad_right_trackpad:
        return choice_place(kRightTrackpadChoices, settings.pad_right_trackpad);
    case Setting::pad_acceleration:
        return choice_place(kAccelerationChoices, settings.pad_acceleration);
    case Setting::pad_right_stick:
        return choice_place(kRightStickChoices, settings.pad_right_stick);
    case Setting::pad_haptics:
        return choice_place(kHapticsChoices, settings.pad_haptics);
    default:
        return 0;
    }
}

void set_strip_level(EngineSettings& settings, Setting setting, std::size_t level) noexcept {
    const Strip strip = strip_of(setting);
    if (strip.levels == 0)
        return;
    // A level the strip shows but does not offer changes nothing.
    if (level >= offered_levels(strip))
        return;
    const std::size_t clamped = std::min(level, strip.levels - 1);
    switch (setting) {
    case Setting::anti_aliasing:
        settings.anti_aliasing = anti_aliasing_levels[clamped];
        break;
    case Setting::hardware_acceleration:
        settings.hardware_acceleration = hardware_acceleration_levels[clamped];
        break;
    case Setting::menu_scaling:
        settings.menu_scaling = menu_scaling_choices[clamped];
        break;
    case Setting::explosion_flash:
        settings.explosion_flash = explosion_flash_choices[clamped];
        break;
    case Setting::zoomed_out_units:
        settings.zoomed_out_units = zoomed_out_units_choices[clamped];
        break;
    case Setting::view_past_map_edge:
        settings.view_past_map_edge = view_past_map_edge_choices[clamped];
        break;
    case Setting::touch_drag:
        settings.touch_drag = touch_drag_choices[clamped];
        break;
    case Setting::touch_latches:
        settings.touch_latches = touch_latches_choices[clamped];
        break;
    case Setting::touch_control_size:
        settings.touch_control_size = control_size_choices[clamped];
        break;
    case Setting::pad_scheme:
        settings.pad_scheme = kSchemeChoices[clamped];
        break;
    case Setting::pad_right_trackpad:
        settings.pad_right_trackpad = kRightTrackpadChoices[clamped];
        break;
    case Setting::pad_acceleration:
        settings.pad_acceleration = kAccelerationChoices[clamped];
        break;
    case Setting::pad_right_stick:
        settings.pad_right_stick = kRightStickChoices[clamped];
        break;
    case Setting::pad_haptics:
        settings.pad_haptics = kHapticsChoices[clamped];
        break;
    default:
        break;
    }
}

std::string_view strip_caption(Setting setting, std::size_t level) noexcept {
    if (level >= strip_of(setting).levels)
        return {};
    switch (setting) {
    case Setting::anti_aliasing:
        return level_caption(anti_aliasing_levels[level]);
    case Setting::menu_scaling:
        return kMenuScalingCaptions[level];
    case Setting::explosion_flash:
        return kExplosionFlashCaptions[level];
    case Setting::zoomed_out_units:
        return kZoomedOutUnitsCaptions[level];
    case Setting::view_past_map_edge:
        return kViewPastMapEdgeCaptions[level];
    case Setting::touch_drag:
        return kTouchDragCaptions[level];
    case Setting::touch_latches:
        return kTouchLatchesCaptions[level];
    case Setting::touch_control_size:
        return kControlSizeCaptions[level];
    case Setting::pad_scheme:
        return kSchemeCaptions[level];
    case Setting::pad_right_trackpad:
        return kRightTrackpadCaptions[level];
    case Setting::pad_acceleration:
        return kPadAccelerationCaptions[level];
    case Setting::pad_right_stick:
        return kRightStickCaptions[level];
    case Setting::pad_haptics:
        return kPadHapticsCaptions[level];
    default:
        return kAccelerationCaptions[level];
    }
}

bool is_button(Setting setting) noexcept {
    return setting == Setting::game_files_summary;
}

bool is_text(Setting setting) noexcept {
    return setting == Setting::game_files_location || setting == Setting::pad_steam_input_notice;
}

bool is_switch(Setting setting) noexcept {
    return !is_slider(setting) && !is_strip(setting) && !is_choice(setting) &&
           !is_button(setting) && !is_text(setting) && !is_buttons(setting);
}

bool is_buttons(Setting setting) noexcept {
    return setting == Setting::user_folder;
}

SourceRect folder_button(const SourceRect& area, std::size_t index) noexcept {
    int32_t left = area.x;
    for (std::size_t before = 0; before < index && before < folder_button_count; ++before)
        left += folder_button_widths[before] + folder_button_gap;
    const int32_t width = index < folder_button_count ? folder_button_widths[index] : 0;
    return {left, area.y, width, area.height};
}

std::size_t folder_button_at(const SourceRect& area, int32_t x, int32_t y) noexcept {
    for (std::size_t index = 0; index < folder_button_count; ++index) {
        const SourceRect button = folder_button(area, index);
        if (x >= button.x && y >= button.y && x < button.x + button.width &&
            y < button.y + button.height)
            return index;
    }
    return folder_button_count;
}

std::string_view folder_button_text(std::size_t index) noexcept {
    constexpr std::array<std::string_view, folder_button_count> captions{
        "SAVES", "SCREENSHOTS", "MODS"
    };
    return index < captions.size() ? captions[index] : std::string_view{};
}

std::string shown_hint_text(
    const Dialog& dialog,
    Setting setting,
    std::size_t line,
    int32_t width,
    const std::function<int32_t(std::string_view)>& text_width
) {
    const HintLine hint = row_hint(dialog, setting, line);
    if (setting == Setting::user_folder && line == 0)
        return dialog.user_folder.empty() ? std::string()
                                          : path_tail(dialog.user_folder, width, text_width);
    return std::string(shown_text(hint.text));
}

bool is_choice(Setting setting) noexcept {
    return setting == Setting::language || setting == Setting::pad_gyro ||
           setting == Setting::pad_prompts || setting == Setting::max_zoom_out ||
           setting == Setting::max_zoom_in || setting == Setting::zoomed_out_after;
}

int32_t choice_field_width(Setting setting) noexcept {
    return setting == Setting::pad_gyro ? wide_choice_width : choice_width;
}

std::span<const oa::data::languages::Language* const> offered_languages() {
    static const std::vector<const oa::data::languages::Language*> offered = [] {
        std::vector<const oa::data::languages::Language*> languages;
        for (const auto& language : oa::data::languages::known_languages())
            if (oa::data::languages::drawable(language))
                languages.push_back(&language);
        return languages;
    }();
    return offered;
}

std::size_t choice_count(const Dialog& dialog, Setting setting) {
    static_cast<void>(dialog);
    switch (setting) {
    case Setting::language:
        return 1 + offered_languages().size();
    case Setting::pad_gyro:
        return kGyroChoices.size();
    case Setting::pad_prompts:
        return kPromptsChoices.size();
    case Setting::max_zoom_out:
        return zoom_out_limits.size();
    case Setting::max_zoom_in:
        return zoom_in_limits.size();
    case Setting::zoomed_out_after:
        return zoomed_out_afters.size();
    default:
        return 0;
    }
}

std::string choice_text(const Dialog& dialog, Setting setting, std::size_t index) {
    if (index >= choice_count(dialog, setting))
        return {};
    if (setting == Setting::pad_gyro)
        return std::string(shown_text(kGyroCaptions[index]));
    if (setting == Setting::pad_prompts)
        return std::string(shown_text(kPromptsCaptions[index]));
    if (setting == Setting::max_zoom_out)
        return std::string(shown_text(kZoomOutCaptions[index]));
    if (setting == Setting::max_zoom_in)
        return std::string(shown_text(kZoomInCaptions[index]));
    if (setting == Setting::zoomed_out_after)
        return std::string(shown_text(kZoomedOutAfterCaptions[index]));
    if (index == 0) {
        const auto* system = dialog.system_language;
        const auto& named = system != nullptr ? *system : oa::data::languages::english();
        return std::string(shown_text("System default")) + " (" + std::string(named.endonym) + ")";
    }
    return std::string(offered_languages()[index - 1]->endonym);
}

std::string shown_choice_text(
    const Dialog& dialog,
    Setting setting,
    std::size_t index,
    int32_t width,
    const std::function<int32_t(std::string_view)>& text_width
) {
    static_cast<void>(width);
    static_cast<void>(text_width);
    return choice_text(dialog, setting, index);
}

std::string field_text(
    const Dialog& dialog,
    const Row& row,
    int32_t width,
    const std::function<int32_t(std::string_view)>& text_width
) {
    return shown_choice_text(
        dialog, row.setting, choice_index(dialog, row.setting), width, text_width
    );
}

std::size_t choice_index(const Dialog& dialog, Setting setting) {
    const EngineSettings& settings = dialog.chosen;
    if (setting == Setting::pad_gyro)
        return choice_place(kGyroChoices, settings.pad_gyro);
    if (setting == Setting::pad_prompts)
        return choice_place(kPromptsChoices, settings.pad_prompts);
    if (setting == Setting::max_zoom_out)
        return choice_place(zoom_out_limits, settings.max_zoom_out);
    if (setting == Setting::max_zoom_in)
        return choice_place(zoom_in_limits, settings.max_zoom_in);
    if (setting == Setting::zoomed_out_after)
        return choice_place(zoomed_out_afters, settings.zoomed_out_after);
    if (setting != Setting::language)
        return 0;
    const auto offered = offered_languages();
    for (std::size_t index = 0; index < offered.size(); ++index)
        if (offered[index]->tag == settings.language)
            return index + 1;
    return 0;
}

void set_choice(Dialog& dialog, Setting setting, std::size_t index) {
    const std::size_t count = choice_count(dialog, setting);
    if (count == 0)
        return;
    const std::size_t clamped = std::min(index, count - 1);
    EngineSettings& settings = dialog.chosen;
    if (setting == Setting::pad_gyro) {
        settings.pad_gyro = kGyroChoices[clamped];
        return;
    }
    if (setting == Setting::pad_prompts) {
        settings.pad_prompts = kPromptsChoices[clamped];
        return;
    }
    if (setting == Setting::max_zoom_out) {
        settings.max_zoom_out = zoom_out_limits[clamped];
        return;
    }
    if (setting == Setting::max_zoom_in) {
        settings.max_zoom_in = zoom_in_limits[clamped];
        return;
    }
    if (setting == Setting::zoomed_out_after) {
        settings.zoomed_out_after = zoomed_out_afters[clamped];
        return;
    }
    settings.language = clamped == 0 ? std::string(oa::data::languages::system_choice)
                                     : std::string(offered_languages()[clamped - 1]->tag);
    // A language drawn in the modern fonts turns them on, and keeps them on
    // after it.
    const oa::data::languages::Language& system = dialog.system_language != nullptr
                                                      ? *dialog.system_language
                                                      : oa::data::languages::english();
    if (oa::data::languages::chosen_language(settings.language, system).needs ==
        oa::data::languages::TextNeeds::modern_fonts)
        settings.modern_fonts = true;
}

int32_t shown_choices(std::size_t choices) noexcept {
    return static_cast<int32_t>(std::min<std::size_t>(choices, most_shown_choices));
}

SourceRect choice_list(const SourceRect& field, std::size_t choices) noexcept {
    const int32_t height = shown_choices(choices) * choice_item_height + 2;
    int32_t top = field.y + field.height;
    // A list that would reach below the footer's line opens over its field.
    if (top + height > footer_rule_row)
        top = std::max(field.y - height, body_top);
    return {field.x, top, field.width, height};
}

SourceRect choice_item(const SourceRect& list, int32_t shown) noexcept {
    return {
        list.x + 1, list.y + 1 + shown * choice_item_height, list.width - 2, choice_item_height
    };
}

std::string_view shown_text(std::string_view english) {
    return oa::data::languages::interface_text(english);
}

bool switch_on(const EngineSettings& settings, Setting setting) noexcept {
    const SwitchMember* entry = switch_member(setting);
    if (entry == nullptr)
        return false;
    return entry->member != nullptr ? settings.*entry->member
                                    : settings.mod_options.*entry->mod_member;
}

void set_switch(EngineSettings& settings, Setting setting, bool on) noexcept {
    const SwitchMember* entry = switch_member(setting);
    if (entry == nullptr)
        return;
    if (entry->member != nullptr)
        settings.*entry->member = on;
    else
        settings.mod_options.*entry->mod_member = on;
}

Lock lock_of(const Locks& locks, Setting setting) noexcept {
    switch (setting) {
    case Setting::path_search:
        return locks.path_search;
    case Setting::unit_limit:
        return locks.unit_limit;
    case Setting::max_frame_rate:
        return locks.max_frame_rate;
    case Setting::hardware_acceleration:
        return locks.hardware_acceleration;
    case Setting::vertical_sync:
        return locks.vertical_sync;
    case Setting::native_density:
        return locks.native_density;
    case Setting::mex_snap_radius:
        return locks.mex_snap;
    case Setting::wreck_snap_radius:
        return locks.wreck_snap;
    case Setting::text_size:
        return locks.text_size;
    case Setting::language:
        return locks.language;
    case Setting::modern_fonts:
        return locks.modern_fonts;
    case Setting::unicode_chat:
        return locks.unicode_chat;
    case Setting::mod:
        return locks.mod;
    case Setting::zoomed_out_after:
        return locks.zoomed_out_after;
    default:
        return Lock::none;
    }
}

bool hint_is_status(Setting setting) noexcept {
    return setting == Setting::hardware_acceleration;
}

RowContext row_context(const Dialog& dialog) noexcept {
    return RowContext{dialog.steam_input, dialog.steam_deck_panel_hz};
}

std::span<const Setting>
section_settings(Page page, const SectionHooks* section, const RowContext& context) {
    if (section == nullptr || section->settings == nullptr)
        return page == Page::controller && context.steam_input
                   ? std::span<const Setting>(kSteamInputControllerRows)
                   : page_settings(page);
    return section->settings(section->context, page);
}

Rows place_rows(
    Page page,
    const Locks& locks,
    int32_t scroll,
    const SectionHooks* section,
    const RowContext& context
) {
    Rows placed{};
    int32_t top = first_row_top;
    const auto settings = section_settings(page, section, context);
    // While the dialog's words are drawn in the modern fonts, a hint's lines
    // lie further apart.
    const bool tall = oa::data::languages::interface_language().needs !=
                      oa::data::languages::TextNeeds::game_fonts;
    // Developer's own rows lie closer, over its list.
    const bool own_section = section != nullptr && section->settings != nullptr;
    const int32_t row_gap =
        page == Page::developer && !own_section ? developer_row_padding : row_padding;
    placed.rows.reserve(settings.size());
    for (std::size_t index = 0; index < settings.size(); ++index) {
        Row& row = placed.rows.emplace_back();
        row.setting = settings[index];
        row.control = first_row_control + static_cast<int32_t>(index);
        row.lock = row_lock(locks, row.setting, section);
        row.hint_is_status = row_hint_is_status(row.setting, section);
        row.top = top;
        const int32_t label_top = top + 1 + row_gap;
        const bool locked = row.lock != Lock::none;
        // The lock, right-aligned on the label line; the label ends short of it.
        const SourceRect right_lock{
            content_right - lock_width, label_top, lock_width, label_line_height
        };
        int32_t label_right = content_right;
        int32_t control_width = 0;
        if (is_strip(row.setting)) {
            const Strip strip = strip_of(row.setting);
            control_width = static_cast<int32_t>(strip.levels) * strip.level_width + 2;
        } else if (is_switch(row.setting)) {
            control_width = switch_width;
        } else if (is_button(row.setting)) {
            control_width = manage_button_width;
        } else if (is_buttons(row.setting)) {
            control_width = folder_buttons_width;
        }
        if (control_width == 0 || (locked && row.hint_is_status)) {
            // A slider's lock, or the lock of a switch or strip whose hint
            // lines are its status, which stands where its control was.
            if (locked) {
                row.lock_area = right_lock;
                label_right = row.lock_area.x - label_gap;
            }
        } else {
            row.control_area = {
                content_right - control_width, label_top, control_width, label_line_height
            };
            label_right = row.control_area.x - label_gap;
            // Any other locked control keeps its place, so that its value
            // shows, with its lock left of it. Its label keeps only the
            // columns left of the lock: 93 beside a switch, too few beside
            // the level strip, which no lock reaches.
            if (locked) {
                row.lock_area = {
                    row.control_area.x - label_gap - lock_width,
                    label_top,
                    lock_width,
                    label_line_height
                };
                label_right = row.lock_area.x - label_gap;
            }
        }
        row.label = {content_left, label_top, label_right - content_left, label_line_height};
        row.hint_lines = hint_line_count(row.setting, context);
        int32_t bottom = label_top + label_line_height + hint_gap;
        for (std::size_t line = 0; line < row.hint_lines; ++line) {
            if (line > 0 && tall)
                bottom += tall_hint_line_gap;
            row.hints[line] = {content_left, bottom, content_width, hint_line_height};
            bottom += hint_line_height;
        }
        if (is_slider(row.setting)) {
            bottom += slider_gap;
            const int32_t value_left = content_right - slider_value_width;
            row.control_area = {
                content_left,
                bottom,
                value_left - slider_value_gap - content_left,
                slider_line_height
            };
            row.value = {value_left, bottom, slider_value_width, slider_line_height};
            bottom += slider_line_height;
        } else if (is_choice(row.setting)) {
            // A drop-down's field stands on its own line, as a slider's track.
            bottom += slider_gap;
            row.control_area = {
                content_left, bottom, choice_field_width(row.setting), choice_line_height
            };
            bottom += choice_line_height;
        }
        bottom += row_gap;
        row.height = bottom - top;
        top = bottom;
    }
    placed.bottom = top;
    scroll_rows(placed, scroll);
    return placed;
}

void scroll_rows(Rows& rows, int32_t by) noexcept {
    const auto lift = [by](SourceRect& rect) {
        if (rect.width > 0 && rect.height > 0)
            rect.y -= by;
    };
    for (Row& row : rows.rows) {
        row.top -= by;
        lift(row.label);
        lift(row.lock_area);
        for (SourceRect& hint : row.hints)
            lift(hint);
        lift(row.control_area);
        lift(row.value);
    }
    rows.bottom -= by;
}

int32_t content_height(const Rows& rows, int32_t scroll) noexcept {
    return rows.bottom + scroll + 1 + end_gap - first_row_top;
}

int32_t scroll_limit(int32_t content_height) noexcept {
    return std::max(content_height - view.height, int32_t{0});
}

Locks shown_locks(const Dialog& dialog) noexcept {
    Locks locks = dialog.locks;
    // The language chosen, System default's included, may need the modern
    // fonts and chat in UTF-8.
    namespace languages = oa::data::languages;
    const languages::Language& system =
        dialog.system_language != nullptr ? *dialog.system_language : languages::english();
    const languages::Language& chosen = languages::chosen_language(dialog.chosen.language, system);
    if (locks.modern_fonts == Lock::none && chosen.needs == languages::TextNeeds::modern_fonts)
        locks.modern_fonts = Lock::set_by_language;
    if (locks.unicode_chat == Lock::none &&
        std::find(
            dialog.unicode_chat_languages.begin(), dialog.unicode_chat_languages.end(), chosen.tag
        ) != dialog.unicode_chat_languages.end())
        locks.unicode_chat = Lock::set_by_language;
    const bool modern_fonts =
        dialog.chosen.modern_fonts || locks.modern_fonts == Lock::set_by_language;
    if (locks.text_size == Lock::none && !modern_fonts)
        locks.text_size = Lock::needs_modern_fonts;
    // After zoom says where units turn to dots; drawn whole, they never do.
    if (locks.zoomed_out_after == Lock::none &&
        dialog.chosen.zoomed_out_units != ZoomedOutUnits::dots)
        locks.zoomed_out_after = Lock::needs_dots;
    return locks;
}

ScrolledRows open_rows(const Dialog& dialog) {
    ScrolledRows open{};
    if (mods_page(dialog)) {
        // Mods' list scrolls in a view of its own, over OPEN MODS FOLDER.
        open.area = mods_scroll(dialog.locks.mod != Lock::none);
        open.rows = place_mod_rows(dialog, 0);
        open.content_height = open.rows.bottom - open.area.view.y;
        open.limit = std::max(open.content_height - open.area.view.height, int32_t{0});
        open.scroll = std::clamp(dialog.scroll[scroll_index(dialog.page)], int32_t{0}, open.limit);
        scroll_rows(open.rows, open.scroll);
        return open;
    }
    open.rows =
        place_rows(dialog.page, shown_locks(dialog), 0, dialog.section_hooks, row_context(dialog));
    if (developer_page(dialog)) {
        // Developer's rows stay at its top; its list scrolls under them in a
        // view of its own, with the end gap under its last row.
        open.area = developer_scroll;
        open.list = place_list(dialog, 0);
        open.content_height = open.list.bottom + end_gap - developer_view.y;
        open.limit = std::max(open.content_height - developer_view.height, int32_t{0});
        open.scroll = std::clamp(dialog.scroll[scroll_index(dialog.page)], int32_t{0}, open.limit);
        scroll_list(open.list, open.scroll);
        return open;
    }
    open.content_height = content_height(open.rows, 0);
    open.limit = scroll_limit(open.content_height);
    // While the dialog's words are drawn in the modern fonts, whose
    // ideographs fill a hint line from its top row, the view's top edge
    // cuts no hint line at the end of the scroll, of which a sliver would
    // show under it: the section scrolls on until the line has passed the
    // edge, its end gap that much taller.
    const bool tall = oa::data::languages::interface_language().needs !=
                      oa::data::languages::TextNeeds::game_fonts;
    if (open.limit > 0 && tall) {
        const int32_t view_top = open.area.view.y;
        for (bool moved = true; moved;) {
            moved = false;
            for (const Row& row : open.rows.rows)
                for (std::size_t line = 0; line < row.hint_lines; ++line) {
                    const SourceRect& box = row.hints[line];
                    const int32_t top = box.y - open.limit;
                    if (top <= view_top && top + box.height > view_top) {
                        open.limit += top + box.height - view_top;
                        moved = true;
                    }
                }
        }
        open.content_height = open.limit + view.height;
    }
    open.scroll = std::clamp(dialog.scroll[scroll_index(dialog.page)], int32_t{0}, open.limit);
    scroll_rows(open.rows, open.scroll);
    return open;
}

int32_t scroll_showing(const ScrolledRows& open, std::size_t index) noexcept {
    if (index >= open.rows.rows.size())
        return open.scroll;
    const Row& row = open.rows.rows[index];
    // The row's line, at the section's top, may come up to the view's first
    // row; the line under it, or the end gap under the last row, down to its
    // last. A row taller than the view would show its top.
    const SourceRect& seen = open.area.view;
    const int32_t line = row.top + open.scroll;
    const int32_t highest = line - seen.y;
    const int32_t lowest = index + 1 == open.rows.rows.size()
                               ? open.limit
                               : line + row.height - (seen.y + seen.height - 1);
    const int32_t scroll = std::min(std::max(open.scroll, lowest), highest);
    return std::clamp(scroll, int32_t{0}, open.limit);
}

SourceRect scroll_thumb(int32_t scroll, int32_t limit, int32_t content_height) noexcept {
    return scroll_thumb(section_scroll, scroll, limit, content_height);
}

SourceRect scroll_thumb(
    const ScrollArea& area, int32_t scroll, int32_t limit, int32_t content_height
) noexcept {
    const SourceRect inside{
        area.well.x + 1, area.well.y + 1, area.well.width - 2, area.well.height - 2
    };
    int32_t height = inside.height;
    if (content_height > area.view.height)
        height = std::max(least_thumb_height, inside.height * area.view.height / content_height);
    const int32_t travel = inside.height - height;
    int32_t top = inside.y;
    if (limit > 0)
        top += (travel * std::clamp(scroll, int32_t{0}, limit) + limit / 2) / limit;
    return {inside.x, top, inside.width, height};
}

int32_t scroll_at(int32_t thumb_top, int32_t limit, int32_t content_height) noexcept {
    return scroll_at(section_scroll, thumb_top, limit, content_height);
}

int32_t scroll_at(
    const ScrollArea& area, int32_t thumb_top, int32_t limit, int32_t content_height
) noexcept {
    const SourceRect thumb = scroll_thumb(area, 0, limit, content_height);
    const int32_t travel = area.well.height - 2 - thumb.height;
    if (travel <= 0 || limit <= 0)
        return 0;
    const int32_t along = std::clamp(thumb_top - thumb.y, int32_t{0}, travel);
    return (limit * along + travel / 2) / travel;
}

SourceRect list_item(Page page, bool touch, bool game_files, bool controller) noexcept {
    // Each entry at its place in the list its dialog shows; Touch and
    // Controller, which only a dialog that lists them asks for, keep their
    // own places.
    const auto kind = static_cast<int32_t>(page) >= static_cast<int32_t>(Page::mod_keys)
                          ? DialogKind::mod_options
                          : DialogKind::engine;
    const auto listed = dialog_pages(kind, touch, game_files, controller);
    const auto found = std::find(listed.begin(), listed.end(), page);
    const auto index = found != listed.end() ? static_cast<int32_t>(found - listed.begin())
                                             : page_control(page) - first_page_control;
    int32_t top = list_first_top + index * (list_item_height + list_item_gap);
    if (page == Page::developer)
        top = list_divider().y + 1 + list_divider_margin;
    return {list_item_left, top, list_item_width, list_item_height};
}

SourceRect list_divider() noexcept {
    // Developer stands at the foot of the list, as far under the divider as
    // the first section stands under the list's top.
    const int32_t row =
        footer_rule_row - (list_first_top - body_top) - list_item_height - list_divider_margin - 1;
    return {
        list_item_left + list_divider_inset,
        row,
        list_item_width - 2 * list_divider_inset,
        1,
    };
}

SourceRect footer_button(int32_t control) noexcept {
    if (control == restore_control)
        return restore_button;
    if (control == cancel_control)
        return cancel_button;
    return ok_button;
}

int32_t knob_column(const SourceRect& track, int32_t stop, int32_t stops) noexcept {
    const int32_t first = track.x + knob_width / 2;
    const int32_t travel = track.width - knob_width;
    if (stops < 2)
        return first;
    return first +
           (travel * std::clamp(stop, int32_t{0}, stops - 1) + (stops - 1) / 2) / (stops - 1);
}

int32_t stop_at(const SourceRect& track, int32_t column, int32_t stops) noexcept {
    const int32_t first = track.x + knob_width / 2;
    const int32_t travel = track.width - knob_width;
    if (stops < 2 || travel <= 0)
        return 0;
    const int32_t along = std::clamp(column - first, int32_t{0}, travel);
    return (along * (stops - 1) + travel / 2) / travel;
}

std::size_t level_at(const SourceRect& area, const Strip& strip, int32_t column) noexcept {
    if (strip.levels == 0 || strip.level_width <= 0)
        return 0;
    const int32_t along = std::max(column - area.x - 1, int32_t{0}) / strip.level_width;
    return std::min(static_cast<std::size_t>(along), strip.levels - 1);
}

std::size_t level_index(AntiAliasing level) noexcept {
    const auto found = std::find(anti_aliasing_levels.begin(), anti_aliasing_levels.end(), level);
    if (found == anti_aliasing_levels.end())
        return 0;
    return static_cast<std::size_t>(found - anti_aliasing_levels.begin());
}

std::string_view page_name(Page page) noexcept {
    switch (page) {
    case Page::mods:
        return "Mods";
    case Page::controls:
        return "Controls";
    case Page::common_tweaks:
        return "Common Tweaks";
    case Page::graphics:
        return "Graphics";
    case Page::language:
        return "Language";
    case Page::touch:
        return "Touch";
    case Page::controller:
        return "Controller";
    case Page::developer:
        return "Developer";
    case Page::game_files:
        return "Game files";
    case Page::mod_keys:
        return "Keys";
    case Page::mod_patrol:
        return "Patrolling";
    case Page::mod_guard:
        return "Guarding";
    case Page::mod_tools:
        return "Build tools";
    case Page::mod_chat:
        return "Snap & chat";
    }
    return {};
}

std::string_view page_heading(Page page) noexcept {
    switch (page) {
    case Page::mods:
        return "MODS";
    case Page::controls:
        return "CONTROLS";
    case Page::common_tweaks:
        return "COMMON TWEAKS";
    case Page::graphics:
        return "GRAPHICS";
    case Page::language:
        return "LANGUAGE";
    case Page::touch:
        return "TOUCH";
    case Page::controller:
        return "CONTROLLER";
    case Page::developer:
        return "DEVELOPER";
    case Page::game_files:
        return "GAME FILES";
    case Page::mod_keys:
        return "MOD KEYS";
    case Page::mod_patrol:
        return "PATROLLING BUILDERS";
    case Page::mod_guard:
        return "GUARDING BUILDERS";
    case Page::mod_tools:
        return "BUILD TOOLS";
    case Page::mod_chat:
        return "SNAP & CHAT";
    }
    return {};
}

std::string_view label_of(Setting setting) noexcept {
    switch (setting) {
    case Setting::path_search:
        return "Pathfinding cycles";
    case Setting::wheel_zoom:
        return "Mouse wheel zoom";
    case Setting::max_zoom_out:
        return "Maximum zoom out";
    case Setting::max_zoom_in:
        return "Maximum zoom in";
    case Setting::view_past_map_edge:
        return "View past the map's edge";
    case Setting::escape_opens_menu:
        return "Escape opens the game menu";
    case Setting::switch_alt:
        return "Select groups without Alt";
    case Setting::unit_limit:
        return "Unit limit";
    case Setting::max_frame_rate:
        return "Maximum frame rate";
    case Setting::anti_aliasing:
        return "Enhanced anti-aliasing";
    case Setting::screen_size:
        return "Screen size";
    case Setting::developer_mode:
        return "Enable Developer Mode";
    case Setting::frame_stats:
        return "Show performance statistics";
    case Setting::hardware_acceleration:
        return "Hardware acceleration";
    case Setting::vertical_sync:
        return "Vertical sync";
    case Setting::menu_scaling:
        return "Menu scaling";
    case Setting::native_density:
        return "Native pixel density";
    case Setting::explosion_flash:
        return "Explosion flash";
    case Setting::zoomed_out_units:
        return "Zoomed out units";
    case Setting::zoomed_out_after:
        return "After zoom";
    case Setting::modern_fonts:
        return "Use modern fonts for game text";
    case Setting::text_outline:
        return "Font outline";
    case Setting::text_shadow:
        return "Font shadow";
    case Setting::text_background:
        return "Game text background";
    case Setting::unicode_chat:
        return "Enable Unicode Multiplayer Chat";
    case Setting::text_size:
        return "Text size";
    case Setting::language:
        return "Language";
    case Setting::touch_drag:
        return "One-finger drag";
    case Setting::touch_hold_delay:
        return "Hold delay";
    case Setting::touch_latches:
        return "QUEUE and ADD";
    case Setting::touch_haptics:
        return "Haptics";
    case Setting::touch_left_handed:
        return "Left-handed layout";
    case Setting::touch_control_size:
        return "Control size";
    case Setting::pad_scheme:
        return "Scheme";
    case Setting::pad_right_trackpad:
        return "Right trackpad";
    case Setting::pad_pointer_speed:
        return "Pointer speed";
    case Setting::pad_acceleration:
        return "Pointer acceleration";
    case Setting::pad_glide:
        return "Trackpad glide";
    case Setting::pad_right_stick:
        return "Right stick";
    case Setting::pad_magnetism:
        return "Magnetism (stick pointer)";
    case Setting::pad_gyro:
        return "Gyro pointer";
    case Setting::pad_gyro_speed:
        return "Gyro speed";
    case Setting::pad_haptics:
        return "Haptics";
    case Setting::pad_prompts:
        return "Button prompts";
    case Setting::pad_left_handed:
        return "Left-handed";
    case Setting::pad_steam_input_notice:
        return "Steam Input";
    case Setting::mod:
        return "Mod";
    case Setting::snap_override_key:
        return "Snap override key";
    case Setting::autoclick_key:
        return "Autoclick key";
    case Setting::rotate_build_key:
        return "Rotate build key";
    case Setting::patrol_hold:
    case Setting::guard_hold:
        return "Hold position";
    case Setting::patrol_maneuver:
    case Setting::guard_maneuver:
        return "Maneuver";
    case Setting::patrol_roam:
    case Setting::guard_roam:
        return "Roam";
    case Setting::mex_snap_radius:
        return "Mex snap radius";
    case Setting::wreck_snap_radius:
        return "Wreck snap radius";
    case Setting::optimize_dt_rows:
        return "Optimize DT rows";
    case Setting::full_rings:
        return "Full rings";
    case Setting::chat_backdrop:
        return "Accessible chat";
    case Setting::panel_background:
        return "Resource bar background";
    case Setting::game_files_summary:
        return "Installed";
    case Setting::game_files_backed_up:
        return "Include in device backups";
    case Setting::game_files_location:
        return "Where the files are";
    case Setting::user_folder:
        return "Your files";
    }
    return {};
}

std::string_view status_line(const AccelerationStatus& acceleration, std::size_t line) noexcept {
    const auto index = static_cast<std::size_t>(acceleration.state);
    if (index >= kStatusTexts.size())
        return {};
    const StatusText& text = kStatusTexts[index];
    if (line == 0)
        return acceleration.state == AccelerationState::waiting_for_game_end ||
                       acceleration.state == AccelerationState::full_waiting_for_game_end
                   ? waiting_line(acceleration)
                   : text.first;
    if (line == 1) {
        if (full_in_use(acceleration.state))
            return full_line(acceleration.supersample);
        return text.second.empty() ? reach_line(acceleration.reach) : text.second;
    }
    return {};
}

std::string_view hint_line(
    Setting setting,
    const EngineSettings& settings,
    const AccelerationStatus& acceleration,
    std::size_t line
) noexcept {
    // Each hint is broken where it reads best, so each line fits the
    // section's width in the small font.
    using Lines = std::array<std::string_view, most_hint_lines>;
    Lines lines{};
    switch (setting) {
    case Setting::path_search:
        lines = {"More cycles find routes faster but use more CPU.", {}};
        break;
    case Setting::wheel_zoom:
        lines = {"Scroll to zoom the battlefield in and out.", {}};
        break;
    case Setting::max_zoom_out: {
        // What the choice stops at, then what says how units look there.
        constexpr std::string_view dots = "Graphics' Zoomed out units says how units look.";
        switch (settings.max_zoom_out) {
        case ZoomOutLimit::automatic:
            lines = {
                "As far as the view always went: 1/6 of normal",
                "size with Full hardware acceleration, else 1/2."
            };
            break;
        case ZoomOutLimit::whole_map:
            lines = {"Out until the whole map fits the view.", dots};
            break;
        case ZoomOutLimit::one_thirty_second:
            lines = {"Out to 1/32 of normal size, or the whole map.", dots};
            break;
        case ZoomOutLimit::one_sixteenth:
            lines = {"Out to 1/16 of normal size, or the whole map.", dots};
            break;
        case ZoomOutLimit::one_eighth:
            lines = {"Out to 1/8 of normal size, or the whole map.", dots};
            break;
        case ZoomOutLimit::one_quarter:
            lines = {"Out to 1/4 of normal size, or the whole map.", dots};
            break;
        case ZoomOutLimit::one_half:
            lines = {"Out to 1/2 of normal size, or the whole map.", dots};
            break;
        }
        break;
    }
    case Setting::max_zoom_in: {
        // What the choice stops at, then every way of zooming it holds.
        constexpr std::string_view every = "The wheel, a pinch and a controller stop there.";
        switch (settings.max_zoom_in) {
        case ZoomInLimit::none:
            lines = {"Never closer than normal size.", every};
            break;
        case ZoomInLimit::twice:
            lines = {"In to 2x normal size at most.", every};
            break;
        case ZoomInLimit::three_times:
            lines = {"In to 3x normal size at most.", every};
            break;
        case ZoomInLimit::four_times:
            lines = {"In to 4x normal size at most.", every};
            break;
        }
        break;
    }
    case Setting::view_past_map_edge:
        // How much of the battlefield may lie past the map, and whether a
        // zoom keeps to it.
        switch (settings.view_past_map_edge) {
        case ViewPastMapEdge::off:
            lines = {
                "The view stays on the map, as the game kept it.",
                "With more than the map in view, it is centred."
            };
            break;
        case ViewPastMapEdge::one_quarter:
            lines = {
                "Up to a quarter of the battlefield past the",
                "map's edges, however the view moves."
            };
            break;
        case ViewPastMapEdge::one_half:
            lines = {
                "Up to half the battlefield past the map's edges;",
                "a zoom keeps the ground under the pointer."
            };
            break;
        }
        break;
    case Setting::escape_opens_menu:
        lines = {"The first press clears the selection,", "the second opens the menu."};
        break;
    case Setting::switch_alt:
        lines = {"A number key selects its group on its own.", {}};
        break;
    case Setting::unit_limit:
        lines = {"Units each player can have.", "Applies from the next game."};
        break;
    case Setting::max_frame_rate:
        lines = {"Lower it to save power.", {}};
        break;
    case Setting::anti_aliasing:
        if (acceleration.full_supersample != 0) {
            const uint8_t drawn = acceleration.full_supersample;
            lines = drawn >= 16  ? kFullAntiAliasingSixteenTimes
                    : drawn >= 8 ? kFullAntiAliasingEightTimes
                    : drawn >= 4 ? kFullAntiAliasingFourTimes
                    : drawn >= 2 ? kFullAntiAliasingTwice
                                 : kFullAntiAliasingOff;
            // Fewer samples than the row asks: the texture limit or the
            // memory allows no more at this window's size.
            if (drawn >= 2 && drawn < static_cast<uint8_t>(settings.anti_aliasing))
                lines[1] = kFullAntiAliasingCapped;
        } else if (settings.anti_aliasing == AntiAliasing::x16)
            lines = {"Units drawn at 16x and scaled down.", "Needs a fast CPU."};
        else if (
            static_cast<uint8_t>(settings.anti_aliasing) >= static_cast<uint8_t>(kDemandingLevel)
        )
            lines = {"Units drawn at 8x and scaled down.", "Needs a fast CPU."};
        else
            lines = {"Units drawn at higher resolution and scaled down", "for smoother edges."};
        break;
    case Setting::screen_size:
        lines = {"Full screen at this size, or a window of it.", "Applies when you press OK."};
        break;
    case Setting::developer_mode:
        lines = {"Your changes to the profile's hacks apply while on.", {}};
        break;
    case Setting::frame_stats:
        lines = {"Frame and tick times over the battlefield.", {}};
        break;
    case Setting::hardware_acceleration:
        // Its hint is its status, which the host keeps up to date.
        return status_line(acceleration, line);
    case Setting::vertical_sync:
        lines = {"Each frame waits for the display: no tearing.", {}};
        break;
    case Setting::menu_scaling:
        // What the way chosen does to the menus.
        switch (settings.menu_scaling) {
        case MenuScaling::sharp:
            lines = {"The menus fill the window, every pixel", "as wide as the next."};
            break;
        case MenuScaling::whole_steps:
            lines = {
                "The largest whole-number scale that fits:", "smaller menus, every pixel square."
            };
            break;
        case MenuScaling::unfiltered:
            lines = {"The menus fill the window, unfiltered,", "as the game always drew them."};
            break;
        }
        break;
    case Setting::native_density:
        lines = {
            "The display's own pixel density (Retina on a Mac).", "Applies from the next start."
        };
        break;
    case Setting::explosion_flash:
        // What the level chosen draws; a mod's profile may draw less.
        switch (settings.explosion_flash) {
        case ExplosionFlash::off:
            lines = {"Explosions do not light up the ground,", "whatever a mod asks for."};
            break;
        case ExplosionFlash::reduced:
            lines = {
                "Explosions light up the ground at half", "strength, or less where a mod asks."
            };
            break;
        case ExplosionFlash::full:
            lines = {
                "Explosions light up the ground as the game", "drew it, or less where a mod asks."
            };
            break;
        }
        break;
    case Setting::zoomed_out_units:
        // What the way chosen draws past After zoom, and what it costs.
        switch (settings.zoomed_out_units) {
        case ZoomedOutUnits::rendered:
        case ZoomedOutUnits::icons:
            lines = {
                "Units are drawn as models at every zoom.",
                "Far out on a large map, needs a fast CPU."
            };
            break;
        case ZoomedOutUnits::dots:
            lines = {
                "Past After zoom, each unit is a dot of its",
                "owner's colour, framed while selected."
            };
            break;
        }
        break;
    case Setting::zoomed_out_after: {
        // Where the dots begin, then what is drawn nearer.
        constexpr std::string_view nearer = "Closer in, units are drawn as models.";
        switch (settings.zoomed_out_after) {
        case ZoomedOutAfter::one_half:
            lines = {"Dots farther out than 1/2 of normal size.", nearer};
            break;
        case ZoomedOutAfter::one_third:
            lines = {"Dots farther out than 1/3 of normal size.", nearer};
            break;
        case ZoomedOutAfter::one_quarter:
            lines = {"Dots farther out than 1/4 of normal size.", nearer};
            break;
        case ZoomedOutAfter::one_sixth:
            lines = {"Dots farther out than 1/6 of normal size.", nearer};
            break;
        case ZoomedOutAfter::one_eighth:
            lines = {"Dots farther out than 1/8 of normal size.", nearer};
            break;
        case ZoomedOutAfter::one_twelfth:
            lines = {"Dots farther out than 1/12 of normal size.", nearer};
            break;
        case ZoomedOutAfter::one_sixteenth:
            lines = {"Dots farther out than 1/16 of normal size.", nearer};
            break;
        }
        break;
    }
    case Setting::modern_fonts:
        lines = {"Modern fonts for in-game text,", "including internationalization."};
        break;
    case Setting::text_outline:
        lines = {"A dark edge round each letter of modern text.", {}};
        break;
    case Setting::text_shadow:
        lines = {"A dark shadow under modern text.", {}};
        break;
    case Setting::text_background:
        lines = {"A shaded box behind each line of game text.", {}};
        break;
    case Setting::unicode_chat:
        lines = {
            "Chat in any language with players who have it;", "others see ? for letters they lack."
        };
        break;
    case Setting::language:
        lines = {"The game's own text and unit names, where its", "data has them in the language."};
        break;
    case Setting::text_size:
        lines = {
            "The size of game text in the modern fonts.",
            settings.modern_fonts ? "Larger sizes are easier to read."
                                  : "The game's own fonts have fixed sizes.",
        };
        break;
    case Setting::touch_drag:
        // What a drag does now, and what still gives the other.
        switch (settings.touch_drag) {
        case TouchDrag::automatic:
            lines = {"Automatic: a selection box on a tablet,", "scrolling on a phone."};
            break;
        case TouchDrag::box:
            lines = {"A drag draws a selection box;", "two fingers scroll the map."};
            break;
        case TouchDrag::scroll:
            lines = {"A drag scrolls the map;", "hold, then drag, for a selection box."};
            break;
        }
        break;
    case Setting::touch_hold_delay:
        lines = {"How long a finger or button is held for a hold.", {}};
        break;
    case Setting::touch_latches:
        lines =
            settings.touch_latches == TouchLatches::one_action
                ? Lines{"A tapped QUEUE, ADD or x5 turns off", "after the next order or selection."}
                : Lines{"A tapped QUEUE, ADD or x5 stays on", "until it is tapped again."};
        break;
    case Setting::touch_haptics:
        lines = {"A short vibration as a touch control acts.", {}};
        break;
    case Setting::touch_left_handed:
        lines = {"The minimap and the thumb controls on the", "right, the orders on the left."};
        break;
    case Setting::touch_control_size:
        lines = {"The size of the touch controls; the game's own", "screens keep theirs."};
        break;
    case Setting::pad_scheme:
        lines = settings.pad_scheme == pad_controls::Scheme::sticks
                    ? Lines{"The right stick moves the pointer and the left", "stick the map."}
                    : Lines{
                          "The right trackpad points and the left one moves",
                          "the map; a pad without trackpads plays Sticks."
                      };
        break;
    case Setting::pad_right_trackpad:
        lines = settings.pad_right_trackpad == pad_controls::RightTrackpad::absolute
                    ? Lines{"Each point of the pad is a point of the view.", {}}
                    : Lines{"The pointer moves as the thumb slides.", {}};
        break;
    case Setting::pad_pointer_speed:
        lines = {"How far the pointer moves for a slide.", {}};
        break;
    case Setting::pad_acceleration:
        lines = {"A quick slide moves the pointer further.", {}};
        break;
    case Setting::pad_glide:
        lines = {"The pointer keeps moving after a quick flick.", {}};
        break;
    case Setting::pad_right_stick:
        switch (settings.pad_right_stick) {
        case pad_controls::RightStick::zoom_and_pages:
            lines = {
                "Up and down zoom about the pointer; a flick left",
                "or right turns the build page while building."
            };
            break;
        case pad_controls::RightStick::pointer:
            lines = {"The right stick moves the pointer, as in the", "Sticks scheme."};
            break;
        case pad_controls::RightStick::nothing:
            lines = {"The right stick does nothing.", {}};
            break;
        }
        break;
    case Setting::pad_magnetism:
        lines = {"The stick pointer settles on a lone unit near it.", {}};
        break;
    case Setting::pad_gyro:
        lines = {"Turning the controller fine-tunes the pointer.", {}};
        break;
    case Setting::pad_gyro_speed:
        lines = {"How far the pointer moves as the controller turns.", {}};
        break;
    case Setting::pad_haptics:
        lines = {"Small ticks and bumps felt through the controller.", {}};
        break;
    case Setting::pad_prompts:
        lines = {"The button pictures the controls and rings show.", {}};
        break;
    case Setting::pad_left_handed:
        lines = {
            "Mirrors the roles: the left pad points, the right",
            "pad moves the map, triggers and grips swap."
        };
        break;
    case Setting::pad_steam_input_notice:
        // Its lines are its notice, broken between words (row_hint).
        break;
    case Setting::mod:
        lines = {"A mod from a mods folder, or one picked.", "Applies from the next start."};
        break;
    case Setting::snap_override_key:
        lines = {"Held, a click is not snapped.", {}};
        break;
    case Setting::autoclick_key:
        lines = {"Held, a build click lays a line or a ring.", {}};
        break;
    case Setting::rotate_build_key:
        lines = {"Turns the building being placed.", {}};
        break;
    case Setting::patrol_hold:
    case Setting::patrol_maneuver:
    case Setting::patrol_roam:
        lines = {"What patrolling builders do.", "Applies from the next game."};
        break;
    case Setting::guard_hold:
    case Setting::guard_maneuver:
    case Setting::guard_roam:
        lines = {"What guarding builders do.", "Applies from the next game."};
        break;
    case Setting::mex_snap_radius:
        lines = {"Cells a metal extractor snaps to metal.", {}};
        break;
    case Setting::wreck_snap_radius:
        lines = {"Cells a reclaim click snaps to a wreck.", {}};
        break;
    case Setting::optimize_dt_rows:
        lines = {"Lines of 2x2 walls build as a double row.", {}};
        break;
    case Setting::full_rings:
        lines = {"Rings include their corners.", {}};
        break;
    case Setting::chat_backdrop:
        lines = {"A dark backdrop under each chat line.", {}};
        break;
    case Setting::panel_background:
        lines = {"Behind the resource bar's text.", {}};
        break;
    case Setting::game_files_summary:
    case Setting::game_files_location:
        // Their lines are the host's (row_hint): what is installed and its
        // sizes, and where the files are.
        break;
    case Setting::game_files_backed_up:
        // row_hint names the device in place of {device}.
        lines = {"After restoring this {device} from a backup,", "add the game files again."};
        break;
    case Setting::user_folder:
        // The first line is the folder's path, which the dialog holds.
        lines = {std::string_view{}, user_folder_hint_text};
        break;
    }
    return line < lines.size() ? lines[line] : std::string_view{};
}

std::size_t hint_line_count(Setting setting) noexcept {
    switch (setting) {
    case Setting::max_zoom_out:
    case Setting::max_zoom_in:
    case Setting::view_past_map_edge:
    case Setting::escape_opens_menu:
    case Setting::unit_limit:
    case Setting::anti_aliasing:
    case Setting::screen_size:
    case Setting::hardware_acceleration:
    case Setting::mod:
    case Setting::modern_fonts:
    case Setting::unicode_chat:
    case Setting::text_size:
    case Setting::language:
    case Setting::menu_scaling:
    case Setting::native_density:
    case Setting::explosion_flash:
    case Setting::zoomed_out_units:
    case Setting::zoomed_out_after:
    case Setting::touch_drag:
    case Setting::touch_latches:
    case Setting::touch_left_handed:
    case Setting::touch_control_size:
    case Setting::pad_scheme:
    case Setting::pad_right_stick:
    case Setting::pad_left_handed:
    case Setting::game_files_summary:
    case Setting::game_files_backed_up:
    case Setting::game_files_location:
    case Setting::user_folder:
        return 2;
    case Setting::pad_steam_input_notice:
        return most_notice_lines;
    default:
        return 1;
    }
}

std::size_t hint_line_count(Setting setting, const RowContext& context) noexcept {
    // On a Steam Deck, Maximum frame rate names the screen's rate it starts at.
    if (setting == Setting::max_frame_rate && context.steam_deck_panel_hz != 0)
        return hint_line_count(setting) + 1;
    return hint_line_count(setting);
}

EngineSettings slider_settings(const Dialog& dialog) {
    EngineSettings shown = dialog.chosen;
    if (dialog.window_screen_size)
        shown.screen_size = *dialog.window_screen_size;
    return shown;
}

std::string value_text(Setting setting, const Dialog& dialog) {
    const EngineSettings shown = slider_settings(dialog);
    if (setting == Setting::screen_size && dialog.custom_screen_size &&
        shown.screen_size == *dialog.custom_screen_size)
        return std::string(shown_text("Custom"));
    return value_text(setting, shown);
}

std::string value_text(Setting setting, const EngineSettings& settings) {
    switch (setting) {
    case Setting::path_search:
        return std::to_string(path_search_multiplier(settings.path_search_nodes)) + "x";
    case Setting::unit_limit:
        return std::to_string(settings.unit_limit) + " " + std::string(shown_text("per player"));
    case Setting::max_frame_rate:
        return std::to_string(settings.max_frame_rate) + " " + std::string(shown_text("fps"));
    case Setting::screen_size:
        return settings.screen_size == desktop_screen_size
                   ? std::string(shown_text("Desktop"))
                   : std::to_string(settings.screen_size.width) + " x " +
                         std::to_string(settings.screen_size.height);
    case Setting::snap_override_key:
    case Setting::autoclick_key:
    case Setting::rotate_build_key: {
        auto options = settings.mod_options;
        return std::string(
            option_keys[static_cast<std::size_t>(option_key_index(key_of(options, setting)))].name
        );
    }
    case Setting::patrol_hold:
    case Setting::patrol_maneuver:
    case Setting::patrol_roam: {
        constexpr std::array<std::string_view, kChoiceStops> names{
            "Reclaim only", "Both", "Assist only"
        };
        auto options = settings.mod_options;
        return std::string(
            shown_text(names[std::min<std::size_t>(choice_of(options, setting), 2)])
        );
    }
    case Setting::guard_hold:
    case Setting::guard_maneuver:
    case Setting::guard_roam: {
        constexpr std::array<std::string_view, kChoiceStops> names{"Stay", "Normal", "Scatter"};
        auto options = settings.mod_options;
        return std::string(
            shown_text(names[std::min<std::size_t>(choice_of(options, setting), 2)])
        );
    }
    case Setting::panel_background: {
        constexpr std::array<std::string_view, kChoiceStops> names{"None", "Text", "Solid"};
        return std::string(
            shown_text(names[std::min<std::size_t>(settings.mod_options.panel_background, 2)])
        );
    }
    case Setting::mex_snap_radius:
        return std::to_string(settings.mod_options.mex_snap_radius) + " " +
               std::string(shown_text("cells"));
    case Setting::wreck_snap_radius:
        return std::to_string(settings.mod_options.wreck_snap_radius) + " " +
               std::string(shown_text("cells"));
    case Setting::text_size:
        return std::to_string(settings.text_size) + "%";
    case Setting::touch_hold_delay:
        return std::to_string(settings.touch_hold_ms) + " " + std::string(shown_text("ms"));
    case Setting::pad_pointer_speed:
        return std::to_string(settings.pad_pointer_speed) + "%";
    case Setting::pad_gyro_speed:
        return std::to_string(settings.pad_gyro_speed) + "%";
    default:
        return {};
    }
}

HintLine row_hint(const Dialog& dialog, Setting setting, std::size_t line) {
    switch (setting) {
    case Setting::user_folder:
        // The player's own folder, then why a folder could not be opened, as
        // a notice.
        if (line == 0)
            return {dialog.user_folder};
        if (line == 1 && !dialog.folder_notice.empty())
            return {dialog.folder_notice, true};
        break;
    case Setting::game_files_summary:
        // What is installed, then its size and the free space.
        if (line == 0)
            return {dialog.game_files_summary};
        return {line == 1 ? dialog.game_files_sizes : std::string{}};
    case Setting::game_files_location: {
        const auto lines = break_lines(dialog.game_files_location, hint_line_characters, 2);
        return {line < lines.size() ? lines[line] : std::string{}};
    }
    case Setting::pad_steam_input_notice: {
        // The notice, in the language shown, between words, as a notice.
        const auto lines = break_lines(
            shown_text(steam_input_notice_text), hint_line_characters, most_notice_lines
        );
        return {line < lines.size() ? lines[line] : std::string{}, true};
    }
    case Setting::max_frame_rate:
        // On a Steam Deck, a second line names the screen's rate, which
        // the setting starts at.
        if (line == 1 && dialog.steam_deck_panel_hz != 0) {
            constexpr std::string_view rate_field = "{rate}";
            std::string text(shown_text(steam_deck_rate_text));
            const auto at = text.find(rate_field);
            if (at != std::string::npos)
                text.replace(at, rate_field.size(), std::to_string(dialog.steam_deck_panel_hz));
            return {text};
        }
        break;
    case Setting::game_files_backed_up: {
        // The device's own name, or the neutral word, in the line shown.
        std::string text(shown_text(hint_line(setting, dialog.chosen, dialog.acceleration, line)));
        constexpr std::string_view device_field = "{device}";
        const std::string device = dialog.game_files_device.empty()
                                       ? std::string(shown_text("device"))
                                       : dialog.game_files_device;
        for (std::size_t at = text.find(device_field); at != std::string::npos;
             at = text.find(device_field, at + device.size()))
            text.replace(at, device_field.size(), device);
        return {text};
    }
    default:
        break;
    }
    return {std::string(hint_line(setting, dialog.chosen, dialog.acceleration, line))};
}

std::string_view row_label(Setting setting) noexcept {
    return label_of(setting);
}

std::vector<std::string>
break_lines(std::string_view text, std::size_t characters, std::size_t most_lines) {
    // The mark between a location's folders: "Open Annihilation › Total
    // Annihilation".
    constexpr std::string_view location_mark = "›";
    std::vector<std::string> lines;
    characters = std::max<std::size_t>(characters, 1);
    // Where each character starts, so that lines count characters and never
    // cut one.
    const auto next = [&text](std::size_t at) {
        const auto lead = static_cast<unsigned char>(text[at]);
        std::size_t bytes = 1;
        if (lead >= 0xf0)
            bytes = 4;
        else if (lead >= 0xe0)
            bytes = 3;
        else if (lead >= 0xc0)
            bytes = 2;
        return std::min(at + bytes, text.size());
    };
    std::size_t start = 0;
    while (start < text.size() && lines.size() < most_lines) {
        while (start < text.size() && text[start] == ' ')
            ++start;
        if (start >= text.size())
            break;
        std::size_t end = start;
        std::size_t count = 0;
        std::size_t last_space = std::string_view::npos;
        // A space after a location's separator, which keeps the folders'
        // names whole.
        std::size_t last_separator = std::string_view::npos;
        while (end < text.size() && count < characters) {
            if (text[end] == ' ') {
                last_space = end;
                if (end >= start + location_mark.size() &&
                    text.substr(end - location_mark.size(), location_mark.size()) == location_mark)
                    last_separator = end;
            }
            end = next(end);
            ++count;
        }
        // The rest fits, or the line breaks after its last separator in its
        // second half, else where a word ends, at its last space, or a word
        // longer than a line is cut.
        if (end < text.size()) {
            if (last_separator != std::string_view::npos &&
                last_separator - start >= (end - start) / 2)
                end = last_separator;
            else if (text[end] != ' ' && last_space != std::string_view::npos && last_space > start)
                end = last_space;
        }
        std::string_view line = text.substr(start, end - start);
        while (!line.empty() && line.back() == ' ')
            line.remove_suffix(1);
        lines.emplace_back(line);
        start = end;
    }
    return lines;
}

SourceRect dialog_list_item(const Dialog& dialog, Page page) noexcept {
    if (dialog.kind != DialogKind::language_text)
        return list_item(page, dialog.touch, dialog.game_files, dialog.controller);
    // A Language dialog lists its one section at the top.
    return {list_item_left, list_first_top, list_item_width, list_item_height};
}

std::string_view lock_text(Lock lock) noexcept {
    switch (lock) {
    case Lock::none:
        return {};
    case Lock::in_game:
        return "Locked during a game";
    case Lock::set_by_host:
        return "Set by the host";
    case Lock::command_line:
        return "Set on the command line";
    case Lock::unavailable:
        return "Not available here";
    case Lock::set_by_mod:
        return "Set by the mod";
    case Lock::needs_modern_fonts:
        return "Needs modern fonts";
    case Lock::always_on:
        return "Always on here";
    case Lock::set_by_language:
        return "Set by the language";
    case Lock::needs_dots:
        return "Needs Dots";
    }
    return {};
}

std::string_view level_caption(AntiAliasing level) noexcept {
    switch (level) {
    case AntiAliasing::off:
        return "Off";
    case AntiAliasing::x2:
        return "2x";
    case AntiAliasing::x4:
        return "4x";
    case AntiAliasing::x8:
        return "8x";
    case AntiAliasing::x16:
        return "16x";
    }
    return {};
}

} // namespace geometry

namespace layout = geometry;

namespace {

/// Tells whether a point lies in a rectangle.
///
/// @param rect the rectangle
/// @param x the point's column
/// @param y the point's row
/// @return true inside it
bool contains(const layout::SourceRect& rect, int32_t x, int32_t y) noexcept {
    return x >= rect.x && y >= rect.y && x < rect.x + rect.width && y < rect.y + rect.height;
}

/// Tells whether a rectangle lies wholly in another.
///
/// @param rect the rectangle
/// @param outer the other
/// @return true when no part of it lies outside
bool wholly_in(const layout::SourceRect& rect, const layout::SourceRect& outer) noexcept {
    return rect.x >= outer.x && rect.y >= outer.y && rect.x + rect.width <= outer.x + outer.width &&
           rect.y + rect.height <= outer.y + outer.height;
}

/// Returns the row a control is, when it is one of the open section's.
///
/// @param rows the open section's rows
/// @param control the control
/// @return the row; nullptr for a control that is not a row's
const layout::Row* row_of(const layout::Rows& rows, int32_t control) noexcept {
    const int32_t index = control - first_row_control;
    if (index < 0 || static_cast<std::size_t>(index) >= rows.rows.size())
        return nullptr;
    return &rows.rows[static_cast<std::size_t>(index)];
}

/// Tells whether a row of Developer's list takes a press and the focus:
/// every header, which opens and closes, and a parameter's control while it
/// can change.
///
/// @param row the row
/// @return true when it does
bool list_row_takes_input(const layout::ListRow& row) noexcept {
    if (row.control == no_control)
        return false;
    return row.kind == layout::ListRowKind::area || row.kind == layout::ListRowKind::hack ||
           !row.locked;
}

/// Returns the rows of Mods that offer ROLL BACK, by their places.
///
/// @param dialog the dialog
/// @param open Mods' rows
/// @return for each placed row, whether it shows ROLL BACK
std::vector<bool> roll_back_rows(const Dialog& dialog, const layout::ScrolledRows& open) {
    std::vector<bool> shown(open.rows.rows.size(), false);
    if (!layout::mods_page(dialog))
        return shown;
    const auto rows = mod_rows(dialog);
    for (std::size_t index = 0; index < shown.size() && index < rows.size(); ++index)
        shown[index] = layout::offers_roll_back(dialog, rows[index]);
    return shown;
}

/// Returns the control under a point that a press can act on.
///
/// A row's control answers only on the part the view shows, and a locked
/// row's takes no press. The scroll bar answers while the section scrolls.
/// On Developer, its list's rows, Show Active Only and, while Developer Mode
/// is on, Restore profile values answer too.
///
/// @param dialog the dialog
/// @param open the open section's rows
/// @param x the point's column
/// @param y the point's row
/// @return the control; no_control when none is there
int32_t
control_at(const Dialog& dialog, const layout::ScrolledRows& open, int32_t x, int32_t y) noexcept {
    if (layout::developer_page(dialog)) {
        if (contains(layout::developer_view, x, y)) {
            for (const layout::ListRow& row : open.list.rows) {
                if (list_row_takes_input(row) && contains(row.control_area, x, y))
                    return row.control;
            }
        }
        if (contains(layout::active_only_switch, x, y))
            return active_only_control;
        if (developer::restore_profile_enabled(dialog) &&
            contains(layout::restore_profile_button, x, y))
            return restore_profile_control;
    }
    // Mods' rows answer in its list's own view; every other section's rows,
    // Developer's above its list among them, in the view under the heading.
    if (contains(layout::mods_page(dialog) ? open.area.view : layout::view, x, y)) {
        // A row's ROLL BACK lies inside the row and answers first.
        const std::vector<bool> roll_backs = roll_back_rows(dialog, open);
        for (std::size_t index = 0; index < roll_backs.size(); ++index) {
            const layout::Row& row = open.rows.rows[index];
            if (roll_backs[index] && row.lock == Lock::none &&
                contains(layout::roll_back_button(row), x, y))
                return layout::roll_back_control(open.rows, index);
        }
        for (const layout::Row& row : open.rows.rows) {
            if (row.lock == Lock::none && contains(row.control_area, x, y))
                return row.control;
        }
    }
    if (layout::mods_page(dialog) && dialog.locks.mod == Lock::none &&
        contains(layout::mods_folder_button, x, y))
        return layout::mods_folder_control(open.rows);
    if (open.limit > 0 && contains(open.area.hit, x, y))
        return scroll_bar_control;
    for (const int32_t control : {restore_control, cancel_control, ok_control}) {
        if (contains(layout::footer_button(control), x, y))
            return control;
    }
    for (const Page page :
         dialog_pages(dialog.kind, dialog.touch, dialog.game_files, dialog.controller)) {
        if (contains(layout::dialog_list_item(dialog, page), x, y))
            return page_control(page);
    }
    return no_control;
}

/// Returns the controls the keyboard focus moves through, in order: the
/// open section's rows that can be changed (on Developer, then its list's
/// rows, Show Active Only and, while Developer Mode is on, Restore profile
/// values), the footer's buttons left to right, then the sections' entries.
///
/// @param dialog the dialog
/// @param open the open section's rows
/// @return the controls
std::vector<int32_t> focus_order(const Dialog& dialog, const layout::ScrolledRows& open) {
    std::vector<int32_t> order;
    // A row that only shows text takes no focus; a row of Mods is followed
    // by its ROLL BACK.
    const std::vector<bool> roll_backs = roll_back_rows(dialog, open);
    for (std::size_t index = 0; index < open.rows.rows.size(); ++index) {
        const layout::Row& row = open.rows.rows[index];
        if (row.lock == Lock::none && row.control_area.width > 0)
            order.push_back(row.control);
        if (row.lock == Lock::none && roll_backs[index])
            order.push_back(layout::roll_back_control(open.rows, index));
    }
    if (layout::developer_page(dialog)) {
        for (const layout::ListRow& row : open.list.rows) {
            if (list_row_takes_input(row))
                order.push_back(row.control);
        }
        order.push_back(active_only_control);
        if (developer::restore_profile_enabled(dialog))
            order.push_back(restore_profile_control);
    }
    if (layout::mods_page(dialog) && dialog.locks.mod == Lock::none)
        order.push_back(layout::mods_folder_control(open.rows));
    order.push_back(restore_control);
    order.push_back(cancel_control);
    order.push_back(ok_control);
    for (const Page page :
         dialog_pages(dialog.kind, dialog.touch, dialog.game_files, dialog.controller))
        order.push_back(page_control(page));
    return order;
}

/// Notes where the pointer is, so that the hover can follow the rows a
/// scroll moves under it.
///
/// @param[in,out] dialog the dialog
/// @param x the pointer's column
/// @param y the pointer's row
void note_pointer(Dialog& dialog, int32_t x, int32_t y) noexcept {
    dialog.pointer_known = true;
    dialog.pointer_x = x;
    dialog.pointer_y = y;
}

/// Finds the control under the last pointer point again, after a scroll
/// moved the rows under it; a held press keeps its hover.
///
/// @param[in,out] dialog the dialog
/// @param open the open section's rows, at the offset the scroll left
void hover_again(Dialog& dialog, const layout::ScrolledRows& open) noexcept {
    if (!dialog.pointer_known || dialog.pressed != no_control)
        return;
    dialog.hovered = control_at(dialog, open, dialog.pointer_x, dialog.pointer_y);
}

/// Scrolls the open section to an offset, moving its placed rows with it;
/// on Developer, its list alone. No scroll changes a setting or moves the
/// focus.
///
/// @param[in,out] dialog the dialog
/// @param[in,out] open the open section's rows (layout::open_rows), left at
///     the new offset
/// @param offset the offset, clamped to the section's limit
/// @return DialogAction::redraw when the section moved, else DialogAction::none
DialogAction scroll_to(Dialog& dialog, layout::ScrolledRows& open, int32_t offset) noexcept {
    const int32_t next = std::clamp(offset, int32_t{0}, open.limit);
    dialog.scroll[layout::scroll_index(dialog.page)] = next;
    if (next == open.scroll)
        return DialogAction::none;
    if (!layout::developer_page(dialog))
        layout::scroll_rows(open.rows, next - open.scroll);
    layout::scroll_list(open.list, next - open.scroll);
    open.scroll = next;
    hover_again(dialog, open);
    return DialogAction::redraw;
}

/// Scrolls the least that shows a row whole, when a control is one of the
/// open section's rows; nothing moves while a press is held.
///
/// @param[in,out] dialog the dialog
/// @param[in,out] open the open section's rows, left at the offset shown
/// @param control the control
/// @return DialogAction::redraw when the section moved, else DialogAction::none
DialogAction show_row(Dialog& dialog, layout::ScrolledRows& open, int32_t control) noexcept {
    if (control < first_row_control || dialog.pressed != no_control)
        return DialogAction::none;
    // Developer's rows and its footer's switch and button stay where they
    // are; its list's rows scroll.
    if (layout::developer_page(dialog))
        return control < first_hack_list_control
                   ? DialogAction::none
                   : scroll_to(dialog, open, layout::list_scroll_showing(open, control));
    if (layout::mods_page(dialog) && control >= layout::mods_folder_control(open.rows)) {
        // A row's ROLL BACK brings its row into view.
        const int32_t row = layout::roll_back_row(open.rows, control);
        if (row < 0)
            return DialogAction::none;
        return scroll_to(dialog, open, layout::scroll_showing(open, static_cast<std::size_t>(row)));
    }
    return scroll_to(
        dialog,
        open,
        layout::scroll_showing(open, static_cast<std::size_t>(control - first_row_control))
    );
}

/// Moves the focus to the next or previous control, and scrolls its row
/// into view when it is a row.
///
/// @param[in,out] dialog the dialog
/// @param[in,out] open the open section's rows, left at the offset shown
/// @param forward true for the next control, false for the previous
/// @return DialogAction::redraw
DialogAction move_focus(Dialog& dialog, layout::ScrolledRows& open, bool forward) {
    const auto order = focus_order(dialog, open);
    const auto found = std::find(order.begin(), order.end(), dialog.focused);
    if (found == order.end()) {
        dialog.focused = forward ? order.front() : order.back();
    } else {
        const auto count = static_cast<std::ptrdiff_t>(order.size());
        const std::ptrdiff_t at = found - order.begin();
        const std::ptrdiff_t next = (at + (forward ? 1 : count - 1)) % count;
        dialog.focused = order[static_cast<std::size_t>(next)];
    }
    static_cast<void>(show_row(dialog, open, dialog.focused));
    return DialogAction::redraw;
}

/// Scrolls the open section for Page Up, Page Down, Home or End, whatever
/// has the focus; nothing moves while a press is held.
///
/// @param[in,out] dialog the dialog
/// @param[in,out] open the open section's rows, left at the new offset
/// @param key the key
/// @return DialogAction::redraw when the section moved, else DialogAction::none
DialogAction scroll_key(Dialog& dialog, layout::ScrolledRows& open, DialogKey key) noexcept {
    if (dialog.pressed != no_control)
        return DialogAction::none;
    int32_t next = open.scroll;
    if (key == DialogKey::page_up)
        next -= open.area.page_step;
    else if (key == DialogKey::page_down)
        next += open.area.page_step;
    else if (key == DialogKey::home)
        next = 0;
    else
        next = open.limit;
    return scroll_to(dialog, open, next);
}

/// Reports a change of the chosen settings, or only a look's. Hardware
/// acceleration passing to a higher level, from Off to Basic or Full or
/// from Basic to Full, asks for the graphics card to be tried afresh.
///
/// @param[in,out] dialog the dialog, its chosen settings after the event
/// @param before the chosen settings before the event
/// @return DialogAction::changed when they differ, else DialogAction::redraw
DialogAction changed_or_redraw(Dialog& dialog, const EngineSettings& before) noexcept {
    if (dialog.chosen.hardware_acceleration > before.hardware_acceleration)
        ++dialog.forget_renderer_failures;
    return before == dialog.chosen ? DialogAction::redraw : DialogAction::changed;
}

/// Moves a row's control one step down or up: a switch to Off or On, a
/// slider one stop, a level strip one level, a drop-down one choice.
///
/// @param[in,out] dialog the dialog, whose chosen settings change
/// @param setting the row's setting
/// @param up true for a step up
void step(Dialog& dialog, Setting setting, bool up) {
    EngineSettings& settings = dialog.chosen;
    if (layout::is_choice(setting)) {
        const std::size_t choice = layout::choice_index(dialog, setting);
        if (up)
            layout::set_choice(dialog, setting, choice + 1);
        else if (choice > 0)
            layout::set_choice(dialog, setting, choice - 1);
        return;
    }
    const uint16_t highest_offered_unit = dialog.highest_offered_unit;
    if (layout::is_slider(setting)) {
        const std::span<const ScreenSize> sizes = dialog.offered_screen_sizes;
        const int32_t stop =
            layout::stop_of(layout::slider_settings(dialog), setting, highest_offered_unit, sizes);
        layout::set_stop(settings, setting, stop + (up ? 1 : -1), highest_offered_unit, sizes);
        // Once moved, Screen size shows the size chosen.
        if (setting == Setting::screen_size)
            dialog.window_screen_size.reset();
        return;
    }
    if (layout::is_strip(setting)) {
        const std::size_t level = layout::strip_level(settings, setting);
        if (up)
            layout::set_strip_level(settings, setting, level + 1);
        else if (level > 0)
            layout::set_strip_level(settings, setting, level - 1);
        return;
    }
    layout::set_switch(settings, setting, up);
}

/// An open drop-down list, as it lies now.
struct OpenList {
    const layout::Row* row{};  ///< the drop-down's row
    layout::SourceRect rect{}; ///< the list, its border included
    std::size_t choices{};     ///< the items it offers
    int32_t shown{};           ///< the items it shows at once
};

/// Returns the open list, placed by its row.
///
/// @param dialog the dialog
/// @param open the open section's rows
/// @return the list; nothing while none is open or its row is not shown
std::optional<OpenList> open_list(const Dialog& dialog, const layout::ScrolledRows& open) {
    if (dialog.open_list == no_control)
        return std::nullopt;
    const layout::Row* row = row_of(open.rows, dialog.open_list);
    if (row == nullptr || !layout::is_choice(row->setting) || row->lock != Lock::none)
        return std::nullopt;
    const std::size_t choices = layout::choice_count(dialog, row->setting);
    return OpenList{
        row,
        layout::choice_list(row->control_area, choices),
        choices,
        layout::shown_choices(choices)
    };
}

/// Returns the item of an open list under a point.
///
/// @param dialog the dialog, whose first shown item counts
/// @param list the list
/// @param x the point's column
/// @param y the point's row
/// @return the item, from 0; -1 for a point on no item
int32_t list_item_at(const Dialog& dialog, const OpenList& list, int32_t x, int32_t y) noexcept {
    for (int32_t shown = 0; shown < list.shown; ++shown)
        if (contains(layout::choice_item(list.rect, shown), x, y)) {
            const int32_t item = dialog.list_first + shown;
            return item < static_cast<int32_t>(list.choices) ? item : -1;
        }
    return -1;
}

/// Closes the open list, choosing nothing.
///
/// @param[in,out] dialog the dialog
void close_list(Dialog& dialog) noexcept {
    dialog.open_list = no_control;
    dialog.list_pressed = -1;
}

/// Scrolls an open list the least that shows an item.
///
/// @param[in,out] dialog the dialog
/// @param list the list
/// @param item the item, from 0
void show_list_item(Dialog& dialog, const OpenList& list, int32_t item) noexcept {
    if (item < dialog.list_first)
        dialog.list_first = item;
    else if (item >= dialog.list_first + list.shown)
        dialog.list_first = item - list.shown + 1;
    dialog.list_first = std::clamp(
        dialog.list_first, int32_t{0}, std::max(static_cast<int32_t>(list.choices) - list.shown, 0)
    );
}

/// Opens a drop-down's list, marking its chosen item and showing it.
///
/// @param[in,out] dialog the dialog
/// @param row the drop-down's row
/// @return DialogAction::redraw
DialogAction open_choices(Dialog& dialog, const layout::Row& row) {
    const std::size_t choices = layout::choice_count(dialog, row.setting);
    const OpenList list{
        &row,
        layout::choice_list(row.control_area, choices),
        choices,
        layout::shown_choices(choices)
    };
    dialog.open_list = row.control;
    dialog.list_pressed = -1;
    dialog.list_first = 0;
    dialog.list_marked = static_cast<int32_t>(layout::choice_index(dialog, row.setting));
    show_list_item(dialog, list, dialog.list_marked);
    return DialogAction::redraw;
}

/// Chooses an open list's item and closes the list.
///
/// @param[in,out] dialog the dialog
/// @param list the list
/// @param item the item, from 0
/// @return DialogAction::changed when the choice moved, else DialogAction::redraw
DialogAction choose(Dialog& dialog, const OpenList& list, int32_t item) {
    const Setting setting = list.row->setting;
    const auto index = static_cast<std::size_t>(item);
    close_list(dialog);
    const EngineSettings before = dialog.chosen;
    layout::set_choice(dialog, setting, index);
    return changed_or_redraw(dialog, before);
}

/// Takes a key while a drop-down list is open: Up and Down mark the item
/// above or below, Page Up and Page Down a list's height of items away, Home
/// and End the first and the last; Enter and Space choose the marked item;
/// Escape closes the list; Tab and Shift+Tab close it and move the focus.
///
/// @param[in,out] dialog the dialog
/// @param[in,out] open the open section's rows
/// @param list the open list
/// @param key the key
/// @return what the key asks of the host
DialogAction
list_key(Dialog& dialog, layout::ScrolledRows& open, const OpenList& list, DialogKey key) {
    const int32_t last = static_cast<int32_t>(list.choices) - 1;
    int32_t marked = dialog.list_marked;
    switch (key) {
    case DialogKey::up:
        --marked;
        break;
    case DialogKey::down:
        ++marked;
        break;
    case DialogKey::page_up:
        marked -= list.shown;
        break;
    case DialogKey::page_down:
        marked += list.shown;
        break;
    case DialogKey::home:
        marked = 0;
        break;
    case DialogKey::end:
        marked = last;
        break;
    case DialogKey::enter:
    case DialogKey::space:
        return choose(dialog, list, std::clamp(marked, int32_t{0}, last));
    case DialogKey::escape:
        close_list(dialog);
        return DialogAction::redraw;
    case DialogKey::tab:
    case DialogKey::back_tab:
        close_list(dialog);
        return move_focus(dialog, open, key == DialogKey::tab);
    default:
        return DialogAction::none;
    }
    marked = std::clamp(marked, int32_t{0}, last);
    if (marked == dialog.list_marked)
        return DialogAction::none;
    dialog.list_marked = marked;
    show_list_item(dialog, list, marked);
    return DialogAction::redraw;
}

/// Returns the question's button under a point.
///
/// @param dialog the dialog, its question showing
/// @param x the point's column
/// @param y the point's row
/// @return question_yes_control, question_no_control, or no_control for neither
int32_t question_button_at(const Dialog& dialog, int32_t x, int32_t y) noexcept {
    if (contains(layout::question_yes_rect(dialog), x, y))
        return question_yes_control;
    if (contains(layout::question_no_rect(dialog), x, y))
        return question_no_control;
    return no_control;
}

/// Answers the question over Mods and puts it away: SWITCH makes the mod
/// offered the Mod setting and asks the host to switch to it; ROLL BACK asks
/// the host to roll the row's folder back; CANCEL leaves everything as it
/// was.
///
/// @param[in,out] dialog the dialog
/// @param yes the answer: SWITCH
/// @return DialogAction::switch_mod for SWITCH, roll_back_mod for ROLL BACK,
///         else DialogAction::redraw
DialogAction answer_question(Dialog& dialog, bool yes) {
    const int32_t offered = dialog.switch_question;
    const ModQuestion asked = dialog.mod_question;
    dialog.switch_question = no_question;
    dialog.mod_question = ModQuestion::switch_mod;
    dialog.question_marks_no = false;
    dialog.hovered = no_control;
    dialog.pressed = no_control;
    if (!yes)
        return DialogAction::redraw;
    if (asked == ModQuestion::roll_back) {
        if (offered < 0 || static_cast<std::size_t>(offered) >= dialog.mod_folders.size())
            return DialogAction::redraw;
        dialog.roll_back_folder = dialog.mod_folders[static_cast<std::size_t>(offered)];
        return DialogAction::roll_back_mod;
    }
    if (offered >= 0 && static_cast<std::size_t>(offered) < dialog.mod_folders.size())
        dialog.chosen.mod_folder = dialog.mod_folders[static_cast<std::size_t>(offered)];
    else
        dialog.chosen.mod_folder.clear();
    return DialogAction::switch_mod;
}

/// Asks the Switch Mod question for a row of Mods; the row of the mod
/// played, and every row while the page is locked, asks nothing.
///
/// @param[in,out] dialog the dialog
/// @param open Mods' rows
/// @param control the row's control
/// @return DialogAction::redraw when the question shows, else DialogAction::none
DialogAction ask_to_switch(Dialog& dialog, const layout::ScrolledRows& open, int32_t control) {
    const layout::Row* row = row_of(open.rows, control);
    if (row == nullptr || row->lock != Lock::none)
        return DialogAction::none;
    const auto rows = mod_rows(dialog);
    const auto index = static_cast<std::size_t>(control - first_row_control);
    if (index >= rows.size() || rows[index].playing)
        return DialogAction::none;
    dialog.switch_question = rows[index].offered;
    dialog.mod_question = ModQuestion::switch_mod;
    dialog.question_marks_no = false;
    dialog.hovered = no_control;
    dialog.pressed = no_control;
    dialog.dragging = false;
    return DialogAction::redraw;
}

/// Asks the Roll Back Mod question for a row of Mods whose folder keeps an
/// earlier version; every row while the page is locked asks nothing.
///
/// @param[in,out] dialog the dialog
/// @param open Mods' rows
/// @param index the row's place
/// @return DialogAction::redraw when the question shows, else DialogAction::none
DialogAction ask_to_roll_back(Dialog& dialog, const layout::ScrolledRows& open, std::size_t index) {
    if (index >= open.rows.rows.size() || open.rows.rows[index].lock != Lock::none)
        return DialogAction::none;
    const auto rows = mod_rows(dialog);
    if (index >= rows.size() || !layout::offers_roll_back(dialog, rows[index]))
        return DialogAction::none;
    dialog.switch_question = rows[index].offered;
    dialog.mod_question = ModQuestion::roll_back;
    dialog.question_marks_no = false;
    dialog.hovered = no_control;
    dialog.pressed = no_control;
    dialog.dragging = false;
    return DialogAction::redraw;
}

/// Takes a key while the question shows: Y answers Yes; N and Escape answer
/// No; Enter and Space answer the marked button; Left marks No, Right marks
/// Yes, and Tab and Shift+Tab move the mark to the other button.
///
/// @param[in,out] dialog the dialog
/// @param key the key
/// @return what the key asks of the host
DialogAction question_key(Dialog& dialog, DialogKey key) {
    bool marks_no = dialog.question_marks_no;
    switch (key) {
    case DialogKey::yes:
        return answer_question(dialog, true);
    case DialogKey::no:
    case DialogKey::escape:
        return answer_question(dialog, false);
    case DialogKey::enter:
    case DialogKey::space:
        return answer_question(dialog, !dialog.question_marks_no);
    case DialogKey::left:
        marks_no = true;
        break;
    case DialogKey::right:
        marks_no = false;
        break;
    case DialogKey::tab:
    case DialogKey::back_tab:
        marks_no = !marks_no;
        break;
    default:
        return DialogAction::none;
    }
    if (marks_no == dialog.question_marks_no)
        return DialogAction::none;
    dialog.question_marks_no = marks_no;
    return DialogAction::redraw;
}

/// Copies one setting's value.
///
/// @param[in,out] to the settings it is copied into
/// @param from the settings it is copied from
/// @param setting the setting
void copy_setting(EngineSettings& to, const EngineSettings& from, Setting setting) noexcept {
    switch (setting) {
    case Setting::path_search:
        to.path_search_nodes = from.path_search_nodes;
        break;
    case Setting::wheel_zoom:
        to.wheel_zoom = from.wheel_zoom;
        break;
    case Setting::max_zoom_out:
        to.max_zoom_out = from.max_zoom_out;
        break;
    case Setting::max_zoom_in:
        to.max_zoom_in = from.max_zoom_in;
        break;
    case Setting::view_past_map_edge:
        to.view_past_map_edge = from.view_past_map_edge;
        break;
    case Setting::escape_opens_menu:
        to.escape_opens_menu = from.escape_opens_menu;
        break;
    case Setting::switch_alt:
        to.switch_alt = from.switch_alt;
        break;
    case Setting::unit_limit:
        to.unit_limit = from.unit_limit;
        break;
    case Setting::max_frame_rate:
        to.max_frame_rate = from.max_frame_rate;
        break;
    case Setting::anti_aliasing:
        to.anti_aliasing = from.anti_aliasing;
        break;
    case Setting::screen_size:
        to.screen_size = from.screen_size;
        break;
    case Setting::developer_mode:
        to.developer_mode = from.developer_mode;
        break;
    case Setting::frame_stats:
        to.frame_stats = from.frame_stats;
        break;
    case Setting::hardware_acceleration:
        to.hardware_acceleration = from.hardware_acceleration;
        break;
    case Setting::vertical_sync:
        to.vertical_sync = from.vertical_sync;
        break;
    case Setting::menu_scaling:
        to.menu_scaling = from.menu_scaling;
        break;
    case Setting::explosion_flash:
        to.explosion_flash = from.explosion_flash;
        break;
    case Setting::zoomed_out_units:
        to.zoomed_out_units = from.zoomed_out_units;
        break;
    case Setting::zoomed_out_after:
        to.zoomed_out_after = from.zoomed_out_after;
        break;
    case Setting::native_density:
        to.native_density = from.native_density;
        break;
    case Setting::modern_fonts:
        to.modern_fonts = from.modern_fonts;
        break;
    case Setting::text_outline:
        to.text_outline = from.text_outline;
        break;
    case Setting::text_shadow:
        to.text_shadow = from.text_shadow;
        break;
    case Setting::text_background:
        to.text_background = from.text_background;
        break;
    case Setting::unicode_chat:
        to.unicode_chat = from.unicode_chat;
        break;
    case Setting::text_size:
        to.text_size = from.text_size;
        break;
    case Setting::language:
        to.language = from.language;
        break;
    case Setting::touch_drag:
        to.touch_drag = from.touch_drag;
        break;
    case Setting::touch_hold_delay:
        to.touch_hold_ms = from.touch_hold_ms;
        break;
    case Setting::touch_latches:
        to.touch_latches = from.touch_latches;
        break;
    case Setting::touch_haptics:
        to.touch_haptics = from.touch_haptics;
        break;
    case Setting::touch_left_handed:
        to.touch_left_handed = from.touch_left_handed;
        break;
    case Setting::touch_control_size:
        to.touch_control_size = from.touch_control_size;
        break;
    case Setting::pad_scheme:
        to.pad_scheme = from.pad_scheme;
        break;
    case Setting::pad_right_trackpad:
        to.pad_right_trackpad = from.pad_right_trackpad;
        break;
    case Setting::pad_pointer_speed:
        to.pad_pointer_speed = from.pad_pointer_speed;
        break;
    case Setting::pad_acceleration:
        to.pad_acceleration = from.pad_acceleration;
        break;
    case Setting::pad_glide:
        to.pad_glide = from.pad_glide;
        break;
    case Setting::pad_right_stick:
        to.pad_right_stick = from.pad_right_stick;
        break;
    case Setting::pad_magnetism:
        to.pad_magnetism = from.pad_magnetism;
        break;
    case Setting::pad_gyro:
        to.pad_gyro = from.pad_gyro;
        break;
    case Setting::pad_gyro_speed:
        to.pad_gyro_speed = from.pad_gyro_speed;
        break;
    case Setting::pad_haptics:
        to.pad_haptics = from.pad_haptics;
        break;
    case Setting::pad_prompts:
        to.pad_prompts = from.pad_prompts;
        break;
    case Setting::pad_left_handed:
        to.pad_left_handed = from.pad_left_handed;
        break;
    case Setting::pad_steam_input_notice:
        // A text row, which keeps no setting.
        break;
    case Setting::game_files_backed_up:
        to.game_files_backed_up = from.game_files_backed_up;
        break;
    case Setting::game_files_summary:
    case Setting::game_files_location:
        // Text rows, which keep no setting.
        break;
    case Setting::mod:
        to.mod_folder = from.mod_folder;
        to.picked_mod_folder = from.picked_mod_folder;
        break;
    case Setting::snap_override_key:
        to.mod_options.snap_override_key = from.mod_options.snap_override_key;
        break;
    case Setting::autoclick_key:
        to.mod_options.autoclick_key = from.mod_options.autoclick_key;
        break;
    case Setting::rotate_build_key:
        to.mod_options.rotate_build_key = from.mod_options.rotate_build_key;
        break;
    case Setting::patrol_hold:
    case Setting::patrol_maneuver:
    case Setting::patrol_roam:
    case Setting::guard_hold:
    case Setting::guard_maneuver:
    case Setting::guard_roam:
    case Setting::panel_background: {
        auto options = from.mod_options;
        layout::choice_of(to.mod_options, setting) = layout::choice_of(options, setting);
        break;
    }
    case Setting::mex_snap_radius:
        to.mod_options.mex_snap_radius = from.mod_options.mex_snap_radius;
        break;
    case Setting::wreck_snap_radius:
        to.mod_options.wreck_snap_radius = from.mod_options.wreck_snap_radius;
        break;
    case Setting::optimize_dt_rows:
        to.mod_options.optimize_dt_rows = from.mod_options.optimize_dt_rows;
        break;
    case Setting::full_rings:
        to.mod_options.full_rings = from.mod_options.full_rings;
        break;
    case Setting::chat_backdrop:
        to.mod_options.chat_backdrop = from.mod_options.chat_backdrop;
        break;
    case Setting::user_folder:
        // It changes no setting.
        break;
    }
}

/// Resets every setting the dialog can change to its default. Each locked
/// setting, found through its row's lock on every section the dialog lists,
/// keeps its value. A dialog of the engine's settings keeps the mod
/// options, and each press there also asks for the graphics card to be
/// tried afresh; a dialog of a mod's options resets the mod options alone,
/// up to the mod's most snap radii. Either reports a change even when no
/// setting moved.
///
/// @param[in,out] dialog the dialog
/// @return DialogAction::changed
DialogAction restore_defaults(Dialog& dialog) {
    const EngineSettings before = dialog.chosen;
    if (dialog.kind == DialogKind::mod_options) {
        ModOptions options = dialog.defaults.mod_options;
        options.mex_snap_most = before.mod_options.mex_snap_most;
        options.wreck_snap_most = before.mod_options.wreck_snap_most;
        options.mex_snap_radius = std::min(options.mex_snap_radius, options.mex_snap_most);
        options.wreck_snap_radius = std::min(options.wreck_snap_radius, options.wreck_snap_most);
        if (dialog.locks.mex_snap != Lock::none)
            options.mex_snap_radius = before.mod_options.mex_snap_radius;
        if (dialog.locks.wreck_snap != Lock::none)
            options.wreck_snap_radius = before.mod_options.wreck_snap_radius;
        dialog.chosen.mod_options = options;
        dialog.restored = true;
        return DialogAction::changed;
    }
    if (dialog.kind == DialogKind::language_text) {
        // Language alone: only its settings go back to their defaults, each
        // locked one kept.
        const auto language = layout::section_settings(Page::language, dialog.section_hooks);
        for (const Setting setting : language)
            if (layout::row_lock(dialog.locks, setting, dialog.section_hooks) == Lock::none)
                copy_setting(dialog.chosen, dialog.defaults, setting);
        dialog.restored = true;
        return DialogAction::changed;
    }
    EngineSettings restored = dialog.defaults;
    restored.mod_options = before.mod_options;
    // The overrides stay: Restore profile values clears them. The mod
    // changes only through the Switch Mod question.
    restored.hack_overrides = before.hack_overrides;
    restored.picked_mod_folder = before.picked_mod_folder;
    restored.mod_folder = before.mod_folder;
    // The backups switch changes only where the dialog lists it.
    if (!dialog.game_files)
        restored.game_files_backed_up = before.game_files_backed_up;
    for (const Page page :
         dialog_pages(dialog.kind, dialog.touch, dialog.game_files, dialog.controller)) {
        const auto settings = layout::section_settings(page, dialog.section_hooks);
        for (const Setting setting : settings) {
            if (layout::row_lock(dialog.locks, setting, dialog.section_hooks) != Lock::none)
                copy_setting(restored, before, setting);
        }
    }
    dialog.chosen = restored;
    dialog.restored = true;
    ++dialog.forget_renderer_failures;
    return DialogAction::changed;
}

/// Closes the dialog keeping what it shows.
///
/// @param[in,out] dialog the dialog
/// @return DialogAction::accepted
DialogAction accept(Dialog& dialog) noexcept {
    dialog.open_list = no_control;
    dialog.pressed = no_control;
    dialog.dragging = false;
    return DialogAction::accepted;
}

/// Closes the dialog putting back what it opened with.
///
/// @param[in,out] dialog the dialog
/// @return DialogAction::cancelled
DialogAction cancel(Dialog& dialog) noexcept {
    dialog.open_list = no_control;
    dialog.chosen = dialog.opened;
    dialog.pressed = no_control;
    dialog.dragging = false;
    return DialogAction::cancelled;
}

/// Shows a section.
///
/// @param[in,out] dialog the dialog
/// @param page the section
/// @return DialogAction::redraw
DialogAction show_page(Dialog& dialog, Page page) noexcept {
    dialog.open_list = no_control;
    dialog.list_pressed = -1;
    dialog.page = page;
    dialog.wheel_rows = 0.0F;
    if (dialog.focused >= first_row_control)
        dialog.focused = page_control(page);
    return DialogAction::redraw;
}

/// Asks the host to open one of Your files' folders.
///
/// @param[in,out] dialog the dialog
/// @param button the button pressed, which the keys then mark
/// @return DialogAction::open_folder
DialogAction open_folder(Dialog& dialog, FolderButton button) noexcept {
    dialog.folder_marked = button;
    dialog.folder_to_open = button;
    return DialogAction::open_folder;
}

/// Presses a button, flips a switch or opens a drop-down's list, as Space
/// or a click does; in Developer's list, opens or closes an area or a hack.
///
/// @param[in,out] dialog the dialog
/// @param open the open section's rows
/// @param control the control
/// @return what it asks of the host
DialogAction activate(Dialog& dialog, const layout::ScrolledRows& open, int32_t control) {
    if (control == restore_control)
        return restore_defaults(dialog);
    if (control == cancel_control)
        return cancel(dialog);
    if (control == ok_control)
        return accept(dialog);
    // A section's entry, by its number: a dialog without Touch has no
    // entry numbered as Touch.
    for (const Page page :
         dialog_pages(dialog.kind, dialog.touch, dialog.game_files, dialog.controller))
        if (control == page_control(page))
            return show_page(dialog, page);
    if (layout::developer_page(dialog)) {
        if (control == active_only_control)
            return developer::set_active_only(dialog, !dialog.developer.active_only);
        if (control == restore_profile_control)
            return developer::restore_profile_values(dialog);
        if (const layout::ListRow* row = layout::list_row(open.list, control))
            return developer::activate(dialog, *row);
    }
    if (layout::mods_page(dialog)) {
        if (control == layout::mods_folder_control(open.rows))
            return dialog.locks.mod == Lock::none ? open_folder(dialog, FolderButton::mods)
                                                  : DialogAction::none;
        if (const int32_t row = layout::roll_back_row(open.rows, control); row >= 0)
            return ask_to_roll_back(dialog, open, static_cast<std::size_t>(row));
        return ask_to_switch(dialog, open, control);
    }
    const layout::Row* row = row_of(open.rows, control);
    if (row != nullptr && row->lock == Lock::none && layout::is_choice(row->setting))
        return open_choices(dialog, *row);
    // MANAGE… asks the host to open the Game files screen.
    if (row != nullptr && row->lock == Lock::none && layout::is_button(row->setting))
        return DialogAction::manage_game_files;
    if (row != nullptr && row->lock == Lock::none && layout::is_buttons(row->setting))
        return open_folder(dialog, dialog.folder_marked);
    if (row == nullptr || row->lock != Lock::none || !layout::is_switch(row->setting))
        return DialogAction::none;
    const EngineSettings before = dialog.chosen;
    layout::set_switch(
        dialog.chosen, row->setting, !layout::switch_on(dialog.chosen, row->setting)
    );
    return changed_or_redraw(dialog, before);
}

/// Sets a slider to the stop under a column.
///
/// @param[in,out] dialog the dialog
/// @param row the slider's row
/// @param column the column
/// @return what it asks of the host
DialogAction drag_to(Dialog& dialog, const layout::Row& row, int32_t column) noexcept {
    const EngineSettings before = dialog.chosen;
    const int32_t stops = layout::stops_of(
        dialog.chosen, row.setting, dialog.highest_offered_unit, dialog.offered_screen_sizes
    );
    layout::set_stop(
        dialog.chosen,
        row.setting,
        layout::stop_at(row.control_area, column, stops),
        dialog.highest_offered_unit,
        dialog.offered_screen_sizes
    );
    // Once moved, Screen size shows the size chosen.
    if (row.setting == Setting::screen_size)
        dialog.window_screen_size.reset();
    return changed_or_redraw(dialog, before);
}

/// A part of the dialog where a press acts on a control.
struct PressArea {
    int32_t control{no_control}; ///< the control
    layout::SourceRect rect{};   ///< where it answers a press
};

/// Returns the part two rectangles share.
///
/// @param a a rectangle
/// @param b another
/// @return the rectangle in both; zero wide or high when they do not meet
layout::SourceRect common_part(const layout::SourceRect& a, const layout::SourceRect& b) noexcept {
    const int32_t left = std::max(a.x, b.x);
    const int32_t top = std::max(a.y, b.y);
    const int32_t right = std::min(a.x + a.width, b.x + b.width);
    const int32_t bottom = std::min(a.y + a.height, b.y + b.height);
    return {left, top, std::max(right - left, int32_t{0}), std::max(bottom - top, int32_t{0})};
}

/// Returns the parts where a press acts on a control now, as control_at
/// tries them: on Developer its list's rows that take input, Show Active
/// Only and Restore profile values while it is enabled; the open section's
/// unlocked rows where the view shows them; the scroll bar while the
/// section scrolls; the footer's buttons; the sections' entries.
///
/// @param dialog the dialog
/// @param open the open section's rows
/// @return the parts; a row the view hides has none
std::vector<PressArea> press_areas(const Dialog& dialog, const layout::ScrolledRows& open) {
    std::vector<PressArea> areas;
    const auto add = [&areas](int32_t control, const layout::SourceRect& rect) {
        if (rect.width > 0 && rect.height > 0)
            areas.push_back(PressArea{control, rect});
    };
    if (layout::developer_page(dialog)) {
        for (const layout::ListRow& row : open.list.rows)
            if (list_row_takes_input(row))
                add(row.control, common_part(row.control_area, layout::developer_view));
        add(active_only_control, layout::active_only_switch);
        if (developer::restore_profile_enabled(dialog))
            add(restore_profile_control, layout::restore_profile_button);
    }
    // Mods' rows answer in its list's own view, under which OPEN MODS
    // FOLDER stands; every other section's rows in the view under the
    // heading.
    const layout::SourceRect& rows_view = layout::mods_page(dialog) ? open.area.view : layout::view;
    const std::vector<bool> roll_backs = roll_back_rows(dialog, open);
    for (std::size_t index = 0; index < roll_backs.size(); ++index)
        if (roll_backs[index] && open.rows.rows[index].lock == Lock::none)
            add(layout::roll_back_control(open.rows, index),
                common_part(layout::roll_back_button(open.rows.rows[index]), rows_view));
    for (const layout::Row& row : open.rows.rows)
        if (row.lock == Lock::none)
            add(row.control, common_part(row.control_area, rows_view));
    if (layout::mods_page(dialog) && dialog.locks.mod == Lock::none)
        add(layout::mods_folder_control(open.rows), layout::mods_folder_button);
    if (open.limit > 0)
        add(scroll_bar_control, open.area.hit);
    for (const int32_t control : {restore_control, cancel_control, ok_control})
        add(control, layout::footer_button(control));
    for (const Page page :
         dialog_pages(dialog.kind, dialog.touch, dialog.game_files, dialog.controller))
        add(page_control(page), layout::dialog_list_item(dialog, page));
    return areas;
}

/// A point of the dialog, in source pixels.
struct SourcePoint {
    int32_t x{}; ///< column
    int32_t y{}; ///< row
};

/// Returns a rectangle's pixel nearest a point.
///
/// @param rect the rectangle, not empty
/// @param x the point's column
/// @param y the point's row
/// @return the point itself inside the rectangle, else the nearest pixel on its edge
SourcePoint nearest_pixel(const layout::SourceRect& rect, int32_t x, int32_t y) noexcept {
    return {
        std::clamp(x, rect.x, rect.x + rect.width - 1),
        std::clamp(y, rect.y, rect.y + rect.height - 1),
    };
}

/// Returns the square of the distance between two points.
///
/// @param a a point
/// @param b another
/// @return the distance squared, in source pixels squared
int64_t distance_squared(SourcePoint a, SourcePoint b) noexcept {
    const int64_t across = int64_t{a.x} - b.x;
    const int64_t down = int64_t{a.y} - b.y;
    return across * across + down * down;
}

/// Returns where a finger's press lands: the finger's own point over a
/// control, else the nearest point of the nearest control within reach;
/// while a drop-down list is open, the finger's point over one of its
/// items, else the nearest point of the nearest item within reach; and
/// while the Switch Mod question shows, the same of its two buttons.
///
/// @param dialog the dialog
/// @param open the open section's rows
/// @param x the finger's column
/// @param y the finger's row
/// @param reach how far a control may lie from the finger, in source pixels
/// @return the point the press takes; the finger's own with nothing within reach
SourcePoint finger_target(
    const Dialog& dialog, const layout::ScrolledRows& open, int32_t x, int32_t y, int32_t reach
) {
    const SourcePoint finger{x, y};
    if (reach <= 0)
        return finger;
    const int64_t within = int64_t{reach} * reach;
    std::optional<SourcePoint> best;
    int64_t best_distance = 0;
    const auto consider = [&](SourcePoint candidate) {
        const int64_t distance = distance_squared(candidate, finger);
        if (distance <= within && (!best || distance < best_distance)) {
            best = candidate;
            best_distance = distance;
        }
    };
    // The Switch Mod question takes every press: the finger's point over one
    // of its buttons, else the nearest point of the nearer within reach.
    if (dialog.switch_question != no_question) {
        if (question_button_at(dialog, x, y) != no_control)
            return finger;
        consider(nearest_pixel(layout::question_yes_rect(dialog), x, y));
        consider(nearest_pixel(layout::question_no_rect(dialog), x, y));
        return best.value_or(finger);
    }
    if (const auto list = open_list(dialog, open)) {
        if (list_item_at(dialog, *list, x, y) >= 0)
            return finger;
        for (int32_t shown = 0; shown < list->shown; ++shown)
            if (dialog.list_first + shown < static_cast<int32_t>(list->choices))
                consider(nearest_pixel(layout::choice_item(list->rect, shown), x, y));
        return best.value_or(finger);
    }
    if (control_at(dialog, open, x, y) != no_control)
        return finger;
    // A part another control covers at its nearest pixel is passed over.
    for (const PressArea& area : press_areas(dialog, open)) {
        const SourcePoint candidate = nearest_pixel(area.rect, x, y);
        if (control_at(dialog, open, candidate.x, candidate.y) == area.control)
            consider(candidate);
    }
    return best.value_or(finger);
}

/// Tells whether a press is held: on a control, or on an open list's item.
///
/// @param dialog the dialog
/// @return true while a press is held
bool press_held(const Dialog& dialog) noexcept {
    return dialog.pressed != no_control || dialog.list_pressed >= 0;
}

} // namespace

std::span<const Setting> page_settings(Page page) noexcept {
    switch (page) {
    case Page::mods:
        return layout::kModsRows;
    case Page::controls:
        return layout::kControlsRows;
    case Page::common_tweaks:
        return layout::kCommonTweaksRows;
    case Page::graphics:
        return layout::kGraphicsRows;
    case Page::language:
        return layout::kLanguageRows;
    case Page::touch:
        return layout::kTouchRows;
    case Page::controller:
        return layout::kControllerRows;
    case Page::developer:
        return layout::kDeveloperRows;
    case Page::game_files:
        return layout::kGameFilesRows;
    case Page::mod_keys:
        return layout::kModKeysRows;
    case Page::mod_patrol:
        return layout::kModPatrolRows;
    case Page::mod_guard:
        return layout::kModGuardRows;
    case Page::mod_tools:
        return layout::kModToolsRows;
    case Page::mod_chat:
        return layout::kModChatRows;
    }
    return {};
}

std::span<const Page>
dialog_pages(DialogKind kind, bool touch, bool game_files, bool controller) noexcept {
    switch (kind) {
    case DialogKind::engine:
        break;
    case DialogKind::mod_options:
        return layout::kModPages;
    case DialogKind::language_text:
        return layout::kLanguageTextPages;
    }
    const layout::EnginePageList& list =
        layout::kEnginePageLists[layout::engine_page_list_index(touch, controller, game_files)];
    return std::span<const Page>(list.pages.data(), list.count);
}

void open_dialog(
    Dialog& dialog,
    const EngineSettings& current,
    const EngineSettings& defaults,
    const Locks& locks,
    std::string_view version,
    Page page,
    const AccelerationStatus& acceleration,
    uint16_t highest_offered_unit,
    const ModOffer& mods,
    std::span<const oa::data::mod_profile::HackState> profile_hacks,
    const oa::data::languages::Language* system_language,
    bool touch,
    bool game_files,
    bool controller
) {
    dialog = Dialog{};
    dialog.touch = touch;
    dialog.game_files = game_files;
    dialog.controller = controller;
    dialog.system_language = system_language;
    dialog.highest_offered_unit = highest_offered_unit;
    dialog.mod_names.assign(mods.names.begin(), mods.names.end());
    dialog.mod_folders.assign(mods.folders.begin(), mods.folders.end());
    dialog.mod_details.assign(mods.details.begin(), mods.details.end());
    dialog.playing_mod_folder = std::string(mods.playing);
    dialog.opened = current;
    dialog.chosen = current;
    dialog.defaults = defaults;
    dialog.locks = locks;
    dialog.acceleration = acceleration;
    dialog.version = std::string(version);
    // Touch, Controller and Game files show only while they are listed.
    dialog.page = (page == Page::touch && !touch) || (page == Page::game_files && !game_files) ||
                          (page == Page::controller && !controller)
                      ? dialog_pages(DialogKind::engine).front()
                      : page;
    if (profile_hacks.empty())
        dialog.developer.profile = oa::data::mod_profile::base_hack_states();
    else
        dialog.developer.profile.assign(profile_hacks.begin(), profile_hacks.end());
    dialog.developer.areas_open.assign(developer_areas().size(), 0);
    dialog.developer.hacks_open.assign(oa::data::mod_profile::standard_hacks().size(), 0);
}

void open_mod_options_dialog(
    Dialog& dialog,
    const EngineSettings& current,
    const EngineSettings& defaults,
    const Locks& locks,
    std::string_view version,
    Page page
) {
    open_dialog(dialog, current, defaults, locks, version, page);
    dialog.kind = DialogKind::mod_options;
    const auto pages = dialog_pages(DialogKind::mod_options);
    if (std::find(pages.begin(), pages.end(), page) == pages.end())
        dialog.page = pages.front();
}

void open_language_text_dialog(
    Dialog& dialog,
    const EngineSettings& current,
    const EngineSettings& defaults,
    const Locks& locks,
    std::string_view version,
    const oa::data::languages::Language* system_language
) {
    open_dialog(
        dialog,
        current,
        defaults,
        locks,
        version,
        Page::language,
        {},
        highest_unit_limit,
        {},
        {},
        system_language
    );
    dialog.kind = DialogKind::language_text;
}

DialogAction
set_acceleration_status(Dialog& dialog, const AccelerationStatus& acceleration) noexcept {
    if (dialog.acceleration == acceleration)
        return DialogAction::none;
    dialog.acceleration = acceleration;
    return DialogAction::redraw;
}

DialogAction set_touch_controls(Dialog& dialog, bool touch) noexcept {
    if (dialog.touch == touch)
        return DialogAction::none;
    dialog.touch = touch;
    if (dialog.kind != DialogKind::engine || touch)
        return DialogAction::redraw;
    // Touch's entry and rows leave the dialog: what pointed at them points
    // at nothing, and Touch's section gives way to the first.
    const int32_t entry = page_control(Page::touch);
    for (int32_t* control : {&dialog.hovered, &dialog.pressed, &dialog.focused})
        if (*control == entry || (dialog.page == Page::touch && *control >= first_row_control))
            *control = no_control;
    if (dialog.page == Page::touch) {
        static_cast<void>(show_page(dialog, dialog_pages(DialogKind::engine).front()));
        dialog.dragging = false;
    }
    return DialogAction::redraw;
}

DialogAction set_controller_section(Dialog& dialog, bool controller, bool steam_input) noexcept {
    if (dialog.controller == controller && dialog.steam_input == steam_input)
        return DialogAction::none;
    const bool showing = dialog.kind == DialogKind::engine && dialog.page == Page::controller;
    if (dialog.steam_input != steam_input && showing && dialog.controller && controller) {
        // The notice comes or goes above Controller's rows: each row's
        // control moves by one, the focus with its row; a hover, a press or
        // an open list on a row lets go.
        const int32_t moved = steam_input ? 1 : -1;
        if (dialog.focused >= first_row_control)
            dialog.focused = std::max(dialog.focused + moved, first_row_control);
        for (int32_t* control : {&dialog.hovered, &dialog.pressed})
            if (*control >= first_row_control)
                *control = no_control;
        if (dialog.open_list >= first_row_control) {
            dialog.open_list = no_control;
            dialog.list_pressed = -1;
        }
        dialog.dragging = false;
    }
    dialog.steam_input = steam_input;
    if (dialog.controller == controller)
        return DialogAction::redraw;
    dialog.controller = controller;
    if (dialog.kind != DialogKind::engine || controller)
        return DialogAction::redraw;
    // Controller's entry and rows leave the dialog: what pointed at them
    // points at nothing, and Controller's section gives way to the first.
    const int32_t entry = page_control(Page::controller);
    for (int32_t* control : {&dialog.hovered, &dialog.pressed, &dialog.focused})
        if (*control == entry || (showing && *control >= first_row_control))
            *control = no_control;
    if (showing) {
        static_cast<void>(show_page(dialog, dialog_pages(DialogKind::engine).front()));
        dialog.dragging = false;
    }
    return DialogAction::redraw;
}

DialogAction dialog_pointer_move(Dialog& dialog, int32_t x, int32_t y) {
    // A finger's held press moves as it was moved to the control it took.
    if (press_held(dialog)) {
        x += dialog.finger_shift_x;
        y += dialog.finger_shift_y;
    }
    note_pointer(dialog, x, y);
    // The question hovers its own buttons only.
    if (dialog.switch_question != no_question) {
        const int32_t button = question_button_at(dialog, x, y);
        if (button == dialog.hovered)
            return DialogAction::none;
        dialog.hovered = button;
        return DialogAction::redraw;
    }
    layout::ScrolledRows open = layout::open_rows(dialog);
    // An open list marks the item under the pointer.
    if (const auto list = open_list(dialog, open)) {
        const int32_t item = list_item_at(dialog, *list, x, y);
        if (item < 0 || item == dialog.list_marked)
            return DialogAction::none;
        dialog.list_marked = item;
        return DialogAction::redraw;
    }
    if (dialog.dragging) {
        // The thumb follows the pointer's row only, and the offset the thumb.
        if (dialog.pressed == scroll_bar_control)
            return scroll_to(
                dialog,
                open,
                layout::scroll_at(
                    open.area, y - dialog.scroll_grab, open.limit, open.content_height
                )
            );
        if (const layout::ListRow* row = layout::list_row(open.list, dialog.pressed))
            return developer::drag_to(dialog, *row, x);
        const layout::Row* row = row_of(open.rows, dialog.pressed);
        if (row != nullptr)
            return drag_to(dialog, *row, x);
    }
    const int32_t hovered = control_at(dialog, open, x, y);
    // Your files lights the button under the pointer.
    const layout::Row* buttons = row_of(open.rows, hovered);
    const std::size_t button = buttons != nullptr && layout::is_buttons(buttons->setting)
                                   ? layout::folder_button_at(buttons->control_area, x, y)
                                   : folder_button_count;
    if (hovered == dialog.hovered && button == dialog.folder_hovered)
        return DialogAction::none;
    dialog.hovered = hovered;
    dialog.folder_hovered = button;
    return DialogAction::redraw;
}

DialogAction dialog_pointer_down(Dialog& dialog, int32_t x, int32_t y) {
    dialog.finger_shift_x = 0;
    dialog.finger_shift_y = 0;
    note_pointer(dialog, x, y);
    // The question takes the press: on a button it holds the button.
    if (dialog.switch_question != no_question) {
        const int32_t button = question_button_at(dialog, x, y);
        dialog.hovered = button;
        dialog.pressed = button;
        dialog.dragging = false;
        return button == no_control ? DialogAction::none : DialogAction::redraw;
    }
    layout::ScrolledRows open = layout::open_rows(dialog);
    // An open list takes the press: on an item it holds the item; anywhere
    // else it closes the list, and the press does nothing more.
    if (const auto list = open_list(dialog, open)) {
        dialog.pressed = no_control;
        dialog.dragging = false;
        const int32_t item = list_item_at(dialog, *list, x, y);
        if (item >= 0) {
            dialog.list_pressed = item;
            dialog.list_marked = item;
        } else {
            close_list(dialog);
        }
        return DialogAction::redraw;
    }
    close_list(dialog);
    const int32_t control = control_at(dialog, open, x, y);
    dialog.hovered = control;
    dialog.pressed = control;
    dialog.dragging = false;
    if (control == no_control)
        return DialogAction::none;
    if (control == scroll_bar_control) {
        // On the thumb, the press grabs it at the row pressed; on the well,
        // the thumb's middle jumps to the pointer and the drag starts there.
        // The scroll bar takes no focus, so the focus stays where it is.
        dialog.dragging = true;
        const layout::SourceRect thumb =
            layout::scroll_thumb(open.area, open.scroll, open.limit, open.content_height);
        if (y >= thumb.y && y < thumb.y + thumb.height) {
            dialog.scroll_grab = y - thumb.y;
            return DialogAction::redraw;
        }
        dialog.scroll_grab = thumb.height / 2;
        static_cast<void>(scroll_to(
            dialog,
            open,
            layout::scroll_at(open.area, y - dialog.scroll_grab, open.limit, open.content_height)
        ));
        return DialogAction::redraw;
    }
    if (dialog.focused != no_control)
        dialog.focused = control;
    if (const layout::ListRow* row = layout::list_row(open.list, control)) {
        // A press on a slider of the list moves its knob and drags it.
        if (row->kind != layout::ListRowKind::slider)
            return DialogAction::redraw;
        dialog.dragging = true;
        return developer::drag_to(dialog, *row, x);
    }
    const layout::Row* row = row_of(open.rows, control);
    if (row != nullptr && layout::is_slider(row->setting)) {
        dialog.dragging = true;
        const DialogAction action = drag_to(dialog, *row, x);
        return action;
    }
    // A press on Your files holds the button under it.
    if (row != nullptr && layout::is_buttons(row->setting))
        dialog.folder_hovered = layout::folder_button_at(row->control_area, x, y);
    return DialogAction::redraw;
}

DialogAction dialog_finger_down(Dialog& dialog, int32_t x, int32_t y, int32_t reach) {
    const SourcePoint target = finger_target(dialog, layout::open_rows(dialog), x, y, reach);
    const DialogAction action = dialog_pointer_down(dialog, target.x, target.y);
    if (press_held(dialog)) {
        dialog.finger_shift_x = target.x - x;
        dialog.finger_shift_y = target.y - y;
    }
    return action;
}

DialogAction dialog_pointer_up(Dialog& dialog, int32_t x, int32_t y) {
    // A finger's release lands as its press was moved; the next press
    // starts afresh.
    if (press_held(dialog)) {
        x += dialog.finger_shift_x;
        y += dialog.finger_shift_y;
    }
    dialog.finger_shift_x = 0;
    dialog.finger_shift_y = 0;
    note_pointer(dialog, x, y);
    // A release over the question's button the press held answers it.
    if (dialog.switch_question != no_question) {
        const int32_t pressed = dialog.pressed;
        const int32_t button = question_button_at(dialog, x, y);
        dialog.pressed = no_control;
        dialog.hovered = button;
        if (pressed == no_control)
            return DialogAction::none;
        if (button != pressed)
            return DialogAction::redraw;
        return answer_question(dialog, button == question_yes_control);
    }
    const layout::ScrolledRows open = layout::open_rows(dialog);
    // A release over the list item the press held chooses it.
    if (const auto list = open_list(dialog, open)) {
        const int32_t held = dialog.list_pressed;
        dialog.list_pressed = -1;
        if (held >= 0 && list_item_at(dialog, *list, x, y) == held)
            return choose(dialog, *list, held);
        return held >= 0 ? DialogAction::redraw : DialogAction::none;
    }
    const int32_t pressed = dialog.pressed;
    const bool dragged = dialog.dragging;
    dialog.pressed = no_control;
    dialog.dragging = false;
    dialog.scroll_grab = 0;
    if (pressed == no_control)
        return DialogAction::none;
    const int32_t control = control_at(dialog, open, x, y);
    dialog.hovered = control;
    if (dragged || control != pressed)
        return DialogAction::redraw;
    if (layout::developer_page(dialog)) {
        if (control == active_only_control)
            return developer::set_active_only(
                dialog, x >= layout::active_only_switch.x + layout::active_only_switch.width / 2
            );
        if (const layout::ListRow* row = layout::list_row(open.list, control))
            return developer::release_on(dialog, *row, x);
    }
    // A mod row asks the Switch Mod question; OPEN MODS FOLDER opens it.
    if (layout::mods_page(dialog))
        return activate(dialog, open, control);
    const layout::Row* row = row_of(open.rows, control);
    if (row != nullptr && layout::is_choice(row->setting))
        return open_choices(dialog, *row);
    if (row != nullptr && layout::is_button(row->setting))
        return DialogAction::manage_game_files;
    if (row != nullptr && layout::is_buttons(row->setting)) {
        // A release over the button the press held opens its folder.
        const std::size_t held = dialog.folder_hovered;
        const std::size_t button = layout::folder_button_at(row->control_area, x, y);
        dialog.folder_hovered = button;
        if (button == folder_button_count || button != held)
            return DialogAction::redraw;
        return open_folder(dialog, static_cast<FolderButton>(button));
    }
    if (row != nullptr) {
        const EngineSettings before = dialog.chosen;
        if (layout::is_strip(row->setting)) {
            layout::set_strip_level(
                dialog.chosen,
                row->setting,
                layout::level_at(row->control_area, layout::strip_of(row->setting), x)
            );
        } else {
            const bool on = x >= row->control_area.x + row->control_area.width / 2;
            layout::set_switch(dialog.chosen, row->setting, on);
        }
        return changed_or_redraw(dialog, before);
    }
    return activate(dialog, open, control);
}

DialogAction dialog_key(Dialog& dialog, DialogKey key) {
    if (dialog.switch_question != no_question)
        return question_key(dialog, key);
    layout::ScrolledRows open = layout::open_rows(dialog);
    const layout::Rows& rows = open.rows;
    if (const auto list = open_list(dialog, open))
        return list_key(dialog, open, *list, key);
    close_list(dialog);
    switch (key) {
    case DialogKey::enter:
        return accept(dialog);
    case DialogKey::escape:
        return cancel(dialog);
    case DialogKey::down:
    case DialogKey::tab:
        return move_focus(dialog, open, true);
    case DialogKey::up:
    case DialogKey::back_tab:
        return move_focus(dialog, open, false);
    case DialogKey::page_up:
    case DialogKey::page_down:
    case DialogKey::home:
    case DialogKey::end:
        return scroll_key(dialog, open, key);
    // Y and N answer a question, and do nothing while none shows.
    case DialogKey::yes:
    case DialogKey::no:
        return DialogAction::none;
    default:
        break;
    }
    if (dialog.focused == no_control)
        return move_focus(dialog, open, true);
    // A key that acts on a row brings it into view first, so that the
    // player sees what it changed.
    const DialogAction shown = show_row(dialog, open, dialog.focused);
    const auto or_shown = [shown](DialogAction action) {
        return action == DialogAction::none ? shown : action;
    };
    if (key == DialogKey::space)
        return or_shown(activate(dialog, open, dialog.focused));
    const bool up = key == DialogKey::right;
    if (layout::developer_page(dialog)) {
        if (dialog.focused == active_only_control)
            return or_shown(developer::set_active_only(dialog, up));
        if (const layout::ListRow* row = layout::list_row(open.list, dialog.focused))
            return or_shown(developer::step(dialog, *row, up));
    }
    const layout::Row* row = row_of(rows, dialog.focused);
    if (row != nullptr && row->lock == Lock::none && layout::is_buttons(row->setting)) {
        // Left and Right move Your files' mark along its buttons.
        const auto marked = static_cast<std::size_t>(dialog.folder_marked);
        const std::size_t next =
            up ? std::min(marked + 1, folder_button_count - 1) : (marked == 0 ? 0 : marked - 1);
        if (next == marked)
            return shown;
        dialog.folder_marked = static_cast<FolderButton>(next);
        return DialogAction::redraw;
    }
    if (row != nullptr) {
        // A button has no steps.
        if (row->lock != Lock::none || layout::is_button(row->setting))
            return shown;
        const EngineSettings before = dialog.chosen;
        step(dialog, row->setting, up);
        return changed_or_redraw(dialog, before);
    }
    // Left and Right move along the footer's buttons.
    constexpr std::array<int32_t, 3> footer{restore_control, cancel_control, ok_control};
    const auto found = std::find(footer.begin(), footer.end(), dialog.focused);
    if (found == footer.end())
        return DialogAction::none;
    const auto at = static_cast<std::size_t>(found - footer.begin());
    const std::size_t next = up ? std::min(at + 1, footer.size() - 1) : (at == 0 ? 0 : at - 1);
    if (next == at)
        return DialogAction::none;
    dialog.focused = footer[next];
    return DialogAction::redraw;
}

DialogAction dialog_wheel(Dialog& dialog, int32_t x, int32_t y, float notches) {
    if (!dialog_contains(x, y) || dialog.pressed != no_control || !std::isfinite(notches) ||
        dialog.switch_question != no_question)
        return DialogAction::none;
    note_pointer(dialog, x, y);
    layout::ScrolledRows open = layout::open_rows(dialog);
    // An open list keeps the section still and scrolls itself, an item a
    // notch, when it holds more items than it shows.
    if (const auto list = open_list(dialog, open)) {
        if (!contains(list->rect, x, y) || static_cast<int32_t>(list->choices) <= list->shown)
            return DialogAction::none;
        const int32_t first = std::clamp(
            dialog.list_first - static_cast<int32_t>(std::lround(notches)),
            int32_t{0},
            static_cast<int32_t>(list->choices) - list->shown
        );
        if (first == dialog.list_first)
            return DialogAction::none;
        dialog.list_first = first;
        return DialogAction::redraw;
    }
    if (open.limit == 0) {
        dialog.wheel_rows = 0.0F;
        return DialogAction::none;
    }
    // Away from the player scrolls towards the top. A turn larger than the
    // section scrolls to its end.
    const float reach = static_cast<float>(open.limit) + 1.0F;
    const float rows = std::clamp(
        dialog.wheel_rows - notches * static_cast<float>(layout::wheel_step), -reach, reach
    );
    const auto whole = static_cast<int32_t>(rows);
    dialog.wheel_rows = rows - static_cast<float>(whole);
    const int32_t next = std::clamp(open.scroll + whole, int32_t{0}, open.limit);
    // What is carried towards an end the section has reached is dropped.
    if ((next == 0 && dialog.wheel_rows < 0.0F) || (next == open.limit && dialog.wheel_rows > 0.0F))
        dialog.wheel_rows = 0.0F;
    return scroll_to(dialog, open, next);
}

DialogAction set_folder_notice(Dialog& dialog, std::string_view reason) {
    if (dialog.folder_notice == reason)
        return DialogAction::none;
    dialog.folder_notice = std::string(reason);
    return DialogAction::redraw;
}

namespace {

/// Tells whether a character separates a path's components.
///
/// @param character the character
/// @return true for '/' and '\\'
bool path_separator(char character) noexcept {
    return character == '/' || character == '\\';
}

/// Tells whether a byte continues a UTF-8 character rather than starting one.
///
/// @param byte the byte
/// @return true for 10xxxxxx
bool continuation_byte(char byte) noexcept {
    return (static_cast<unsigned char>(byte) & 0xC0U) == 0x80U;
}

} // namespace

std::string path_tail(
    std::string_view path, int32_t width, const std::function<int32_t(std::string_view)>& text_width
) {
    while (path.size() > 1 && path_separator(path.back()))
        path.remove_suffix(1);
    if (text_width(path) <= width)
        return std::string(path);
    const std::string ellipsis(layout::path_ellipsis);
    // The most whole components that fit, the separator before them kept.
    for (std::size_t at = 1; at < path.size(); ++at) {
        if (!path_separator(path[at]))
            continue;
        std::string tail = ellipsis + std::string(path.substr(at));
        if (text_width(tail) <= width)
            return tail;
    }
    // The last component alone is too wide: as much of its end as fits.
    std::size_t last = path.size();
    while (last > 0 && !path_separator(path[last - 1]))
        --last;
    for (std::size_t at = last; at < path.size(); ++at) {
        if (continuation_byte(path[at]))
            continue;
        std::string tail = ellipsis + std::string(path.substr(at));
        if (text_width(tail) <= width)
            return tail;
    }
    return ellipsis;
}

bool dialog_contains(int32_t x, int32_t y) noexcept {
    return x >= 0 && y >= 0 && x < dialog_width && y < dialog_height;
}

namespace {

/// Adds a switch's two halves to the parts, its control on each.
///
/// @param[in,out] parts the parts
/// @param area the switch
/// @param control its control; no_control for one that takes no press
void switch_parts(std::vector<LayoutPart>& parts, const layout::SourceRect& area, int32_t control) {
    const int32_t half = (area.width - 2) / 2;
    parts.push_back(
        LayoutPart{
            {area.x + 1, area.y + 1, half, area.height - 2},
            std::string(layout::off_text),
            DialogFont::small,
            0,
            control,
        }
    );
    parts.push_back(
        LayoutPart{
            {area.x + 1 + half, area.y + 1, half, area.height - 2},
            std::string(layout::on_text),
            DialogFont::small,
            0,
            control,
        }
    );
}

/// Adds the parts under Developer's rows: those of its list's rows that lie
/// wholly in the list's view, and its footer.
///
/// @param dialog the dialog
/// @param open Developer's rows and list (layout::open_rows)
/// @param[in,out] parts the parts
void developer_layout(
    const Dialog& dialog, const layout::ScrolledRows& open, std::vector<LayoutPart>& parts
) {
    const auto text_part =
        [&parts](layout::SourceRect rect, std::string text, DialogFont font, int32_t control) {
            parts.push_back(LayoutPart{rect, std::move(text), font, 0, control});
        };
    // The list's rows, only the parts wholly in its view.
    std::vector<LayoutPart> rows;
    const auto row_text =
        [&rows](layout::SourceRect rect, std::string text, DialogFont font, int32_t control) {
            rows.push_back(LayoutPart{rect, std::move(text), font, 0, control});
        };
    for (const layout::ListRow& row : open.list.rows) {
        const bool takes = row.kind == layout::ListRowKind::area ||
                           row.kind == layout::ListRowKind::hack || !row.locked;
        const int32_t control = takes ? row.control : no_control;
        switch (row.kind) {
        case layout::ListRowKind::area:
            rows.push_back(LayoutPart{row.arrow, {}, DialogFont::regular, 0, no_control});
            row_text(row.label, row.text, DialogFont::regular, control);
            row_text(row.value, row.shown, DialogFont::small, no_control);
            break;
        case layout::ListRowKind::hack:
            rows.push_back(LayoutPart{row.arrow, {}, DialogFont::regular, 0, no_control});
            row_text(row.label, row.text, DialogFont::small, control);
            switch_parts(rows, row.toggle, row.locked ? no_control : row.control);
            break;
        case layout::ListRowKind::id:
        case layout::ListRowKind::text:
        case layout::ListRowKind::scope:
        case layout::ListRowKind::heading:
            row_text(row.label, row.text, DialogFont::small, no_control);
            break;
        case layout::ListRowKind::toggle:
            row_text(row.label, row.text, DialogFont::small, no_control);
            switch_parts(rows, row.control_area, control);
            break;
        case layout::ListRowKind::slider:
            row_text(row.label, row.text, DialogFont::small, no_control);
            row_text(row.value, row.shown, DialogFont::small, no_control);
            rows.push_back(LayoutPart{row.control_area, {}, DialogFont::regular, 0, control});
            break;
        }
    }
    for (LayoutPart& part : rows)
        if (wholly_in(part.rect, layout::developer_view))
            parts.push_back(std::move(part));
    // The list's footer, which never scrolls.
    text_part(
        layout::active_only_label,
        layout::active_only_text(
            active_hack_count(dialog), oa::data::mod_profile::standard_hacks().size()
        ),
        DialogFont::regular,
        no_control
    );
    switch_parts(parts, layout::active_only_switch, active_only_control);
    text_part(
        layout::restore_profile_button,
        std::string(layout::restore_profile_text),
        DialogFont::small,
        developer::restore_profile_enabled(dialog) ? restore_profile_control : no_control
    );
}

} // namespace

namespace {

/// Lists Mods' parts: the lock line while it is locked, each row wholly in
/// the list's view (its badge, title, PLAYING tag, version and
/// description; the row itself the control a press switches with),
/// OPEN MODS FOLDER and the lines under it.
///
/// @param dialog the dialog
/// @param open Mods' rows
/// @param[in,out] parts the parts, which Mods' are added to
/// @param text_width a regular text's width
/// @param small_text_width a small text's width
void mods_layout(
    const Dialog& dialog,
    const layout::ScrolledRows& open,
    std::vector<LayoutPart>& parts,
    const std::function<int32_t(std::string_view)>& text_width,
    const std::function<int32_t(std::string_view)>& small_text_width
) {
    const auto text = [&parts](layout::SourceRect rect, std::string shown, DialogFont font) {
        parts.push_back(LayoutPart{rect, std::move(shown), font, 0, no_control});
    };
    if (dialog.locks.mod != Lock::none) {
        // The reason, beside the padlock, over as many of its two lines as
        // it needs.
        const auto lines = layout::wrap_text(
            layout::shown_text(
                dialog.locks.mod == Lock::command_line ? layout::mod_from_command_line_text
                                                       : layout::mod_in_game_text
            ),
            layout::mods_lock_text.width,
            small_text_width
        );
        for (std::size_t line = 0; line < lines.size() && line < 2; ++line) {
            layout::SourceRect rect = layout::mods_lock_text;
            rect.y += static_cast<int32_t>(line) * rect.height;
            text(rect, lines[line], DialogFont::small);
        }
    }
    const auto rows = mod_rows(dialog);
    for (std::size_t index = 0; index < open.rows.rows.size() && index < rows.size(); ++index) {
        const layout::Row& row = open.rows.rows[index];
        if (!wholly_in(row.control_area, open.area.view))
            continue;
        const layout::ModRowText shown = layout::mod_row_text(dialog, rows[index]);
        parts.push_back(
            LayoutPart{
                row.control_area,
                {},
                DialogFont::regular,
                0,
                row.lock == Lock::none ? row.control : no_control
            }
        );
        // The row's own parts lie inside it; only the row is listed as its
        // control, and its texts and badge as texts and a mark, and its ROLL
        // BACK as a control of its own.
        static_cast<void>(text_width);
        static_cast<void>(small_text_width);
        if (layout::offers_roll_back(dialog, rows[index]))
            parts.push_back(
                LayoutPart{
                    layout::roll_back_button(row),
                    std::string(layout::shown_text(layout::roll_back_text)),
                    DialogFont::small,
                    0,
                    row.lock == Lock::none ? layout::roll_back_control(open.rows, index)
                                           : no_control
                }
            );
    }
    parts.push_back(
        LayoutPart{
            layout::mods_folder_button,
            std::string(layout::shown_text(layout::open_mods_folder_text)),
            DialogFont::small,
            0,
            dialog.locks.mod == Lock::none ? layout::mods_folder_control(open.rows) : no_control,
        }
    );
    const std::string_view second =
        dialog.folder_notice.empty() ? layout::mods_folders_text[1] : dialog.folder_notice;
    text(
        layout::mods_note_first,
        std::string(layout::shown_text(layout::mods_folders_text[0])),
        DialogFont::small
    );
    text(layout::mods_note_second, std::string(layout::shown_text(second)), DialogFont::small);
}

} // namespace

std::vector<LayoutPart> dialog_layout(const Dialog& dialog, const DialogFonts* fonts) {
    std::vector<LayoutPart> parts;
    // The player's own folder's path is shortened to its place in the fonts, or at
    // an estimated width a character without them.
    const auto text_width = [fonts](std::string_view text) {
        if (fonts != nullptr)
            return dialog_text_width(*fonts, DialogFont::regular, text);
        int32_t characters = 0;
        for (const char byte : text)
            if (!continuation_byte(byte))
                ++characters;
        return characters * estimated_character_width;
    };
    // Your files' path is shortened to its hint line in the small font.
    const auto small_text_width = [fonts](std::string_view text) {
        if (fonts != nullptr)
            return dialog_text_width(*fonts, DialogFont::small, text);
        int32_t characters = 0;
        for (const char byte : text)
            if (!continuation_byte(byte))
                ++characters;
        return characters * estimated_character_width;
    };
    // Each text as the dialog shows it, the interface's words in the
    // language shown (layout::shown_text).
    const auto text_part =
        [&parts](
            layout::SourceRect rect, std::string_view text, DialogFont font, int32_t tracking = 0
        ) {
            parts.push_back(
                LayoutPart{rect, std::string(layout::shown_text(text)), font, tracking, no_control}
            );
        };
    const auto control_part = [&parts](layout::SourceRect rect, int32_t control) {
        parts.push_back(LayoutPart{rect, {}, DialogFont::regular, 0, control});
    };

    // The header: the mark, the title, and at the right the version and the
    // shared game's note.
    control_part(layout::header_mark, no_control);
    const int32_t title_left =
        layout::header_mark.x + layout::header_mark.width + layout::header_gap;
    text_part(
        {title_left, layout::header_top, layout::title_width, layout::header_height},
        layout::title_text,
        DialogFont::regular,
        layout::heading_tracking
    );
    const int32_t suffix_left = title_left + layout::title_width + layout::header_gap;
    text_part(
        {suffix_left, layout::header_top, layout::title_suffix_width, layout::header_height},
        layout::title_suffix_text,
        DialogFont::regular,
        layout::heading_tracking
    );
    const layout::SourceRect version{
        layout::content_right - layout::version_width,
        layout::header_top,
        layout::version_width,
        layout::header_height,
    };
    text_part(version, dialog.version, DialogFont::small);
    if (dialog.locks.shared_game) {
        const int32_t shared_left = suffix_left + layout::title_suffix_width + layout::header_gap;
        text_part(
            {shared_left,
             layout::header_top,
             version.x - layout::version_gap - shared_left,
             layout::header_height},
            layout::shared_game_text,
            DialogFont::small
        );
    }

    // The section list.
    for (const Page page :
         dialog_pages(dialog.kind, dialog.touch, dialog.game_files, dialog.controller)) {
        const layout::SourceRect item = layout::dialog_list_item(dialog, page);
        parts.push_back(
            LayoutPart{
                {item.x + layout::list_text_offset,
                 item.y,
                 item.width - layout::list_text_offset - layout::list_text_margin,
                 item.height},
                std::string(layout::shown_text(layout::page_name(page))),
                DialogFont::regular,
                0,
                page_control(page),
            }
        );
    }
    if (dialog.kind == DialogKind::engine)
        control_part(layout::list_divider(), no_control);

    // The open section.
    text_part(
        layout::heading,
        layout::page_heading(dialog.page),
        DialogFont::small,
        layout::heading_tracking
    );
    // The rows: only the parts wholly in the view are listed, so that each
    // listed control is pressed where it is drawn and each text is whole.
    const layout::ScrolledRows open = layout::open_rows(dialog);
    const auto row_part = [&parts](LayoutPart part) {
        if (wholly_in(part.rect, layout::view))
            parts.push_back(std::move(part));
    };
    const auto row_text =
        [&row_part](layout::SourceRect rect, std::string_view text, DialogFont font) {
            row_part(LayoutPart{rect, std::string(layout::shown_text(text)), font, 0, no_control});
        };
    if (layout::mods_page(dialog))
        mods_layout(dialog, open, parts, text_width, small_text_width);
    for (const layout::Row& row : open.rows.rows) {
        if (layout::mods_page(dialog))
            break;
        // A locked row's control is drawn but takes no press.
        const int32_t control = row.lock == Lock::none ? row.control : no_control;
        row_text(row.label, layout::row_label(row.setting), DialogFont::regular);
        if (row.lock != Lock::none) {
            const layout::SourceRect text_area{
                row.lock_area.x + layout::padlock_width + layout::padlock_gap,
                row.lock_area.y,
                row.lock_area.width - layout::padlock_width - layout::padlock_gap,
                row.lock_area.height,
            };
            row_part(
                LayoutPart{
                    {row.lock_area.x, row.lock_area.y, layout::padlock_width, row.lock_area.height},
                    {},
                    DialogFont::regular,
                    0,
                    no_control,
                }
            );
            row_text(text_area, layout::lock_text(row.lock), DialogFont::small);
        }
        for (std::size_t line = 0; line < row.hint_lines; ++line) {
            const std::string hint = layout::shown_hint_text(
                dialog, row.setting, line, row.hints[line].width, small_text_width
            );
            if (!hint.empty())
                row_text(row.hints[line], hint, DialogFont::small);
        }
        if (layout::is_text(row.setting))
            continue;
        if (layout::is_button(row.setting)) {
            row_part(
                LayoutPart{
                    row.control_area,
                    std::string(layout::shown_text(layout::manage_text)),
                    DialogFont::small,
                    0,
                    control,
                }
            );
        } else if (layout::is_buttons(row.setting) && row.control_area.width > 0) {
            for (std::size_t button = 0; button < folder_button_count; ++button)
                row_part(
                    LayoutPart{
                        layout::folder_button(row.control_area, button),
                        std::string(layout::shown_text(layout::folder_button_text(button))),
                        DialogFont::small,
                        0,
                        control,
                    }
                );
        } else if (layout::is_strip(row.setting) && row.control_area.width > 0) {
            const layout::Strip strip = layout::strip_of(row.setting);
            for (std::size_t level = 0; level < strip.levels; ++level) {
                row_part(
                    LayoutPart{
                        {row.control_area.x + 1 + static_cast<int32_t>(level) * strip.level_width,
                         row.control_area.y + 1,
                         strip.level_width,
                         row.control_area.height - 2},
                        std::string(layout::shown_text(layout::strip_caption(row.setting, level))),
                        DialogFont::small,
                        0,
                        control,
                    }
                );
            }
        } else if (layout::is_choice(row.setting)) {
            // The field shows the choice, in the regular font.
            row_part(
                LayoutPart{
                    row.control_area,
                    layout::field_text(dialog, row, layout::choice_field_text_room, text_width),
                    DialogFont::regular,
                    0,
                    control,
                }
            );
        } else if (layout::is_slider(row.setting)) {
            row_part(LayoutPart{row.control_area, {}, DialogFont::regular, 0, control});
            row_text(row.value, layout::value_text(row.setting, dialog), DialogFont::regular);
        } else if (row.control_area.width > 0) {
            const int32_t half = (row.control_area.width - 2) / 2;
            row_part(
                LayoutPart{
                    {row.control_area.x + 1,
                     row.control_area.y + 1,
                     half,
                     row.control_area.height - 2},
                    std::string(layout::shown_text(layout::off_text)),
                    DialogFont::small,
                    0,
                    control,
                }
            );
            row_part(
                LayoutPart{
                    {row.control_area.x + 1 + half,
                     row.control_area.y + 1,
                     half,
                     row.control_area.height - 2},
                    std::string(layout::shown_text(layout::on_text)),
                    DialogFont::small,
                    0,
                    control,
                }
            );
        }
    }
    // On Developer, its list and the list's footer under its rows.
    if (layout::developer_page(dialog))
        developer_layout(dialog, open, parts);
    if (open.limit > 0)
        control_part(open.area.well, scroll_bar_control);

    // The footer.
    const std::array<std::pair<int32_t, std::string_view>, 3> buttons{{
        {restore_control, layout::restore_text},
        {cancel_control, layout::cancel_text},
        {ok_control, layout::ok_text},
    }};
    for (const auto& [control, caption] : buttons) {
        parts.push_back(
            LayoutPart{
                layout::footer_button(control),
                std::string(layout::shown_text(caption)),
                DialogFont::small,
                0,
                control,
            }
        );
    }
    // An open drop-down list lies over what is under it, which is not listed:
    // its items are.
    if (const auto list = open_list(dialog, open)) {
        const auto overlaps = [&](const LayoutPart& part) {
            const auto& a = part.rect;
            const auto& b = list->rect;
            return a.x < b.x + b.width && b.x < a.x + a.width && a.y < b.y + b.height &&
                   b.y < a.y + a.height;
        };
        parts.erase(std::remove_if(parts.begin(), parts.end(), overlaps), parts.end());
        for (int32_t shown = 0; shown < list->shown; ++shown) {
            const int32_t item = dialog.list_first + shown;
            if (item >= static_cast<int32_t>(list->choices))
                break;
            parts.push_back(
                LayoutPart{
                    layout::choice_item(list->rect, shown),
                    layout::shown_choice_text(
                        dialog,
                        list->row->setting,
                        static_cast<std::size_t>(item),
                        layout::choice_item_text_room,
                        text_width
                    ),
                    DialogFont::regular,
                    0,
                    no_control,
                }
            );
        }
    }
    // The question lies over everything else, which is not listed under it.
    if (dialog.switch_question != no_question) {
        const auto under_question = [](const LayoutPart& part) {
            const auto& a = part.rect;
            const auto& b = layout::question_box;
            return a.x < b.x + b.width && b.x < a.x + a.width && a.y < b.y + b.height &&
                   b.y < a.y + a.height;
        };
        parts.erase(std::remove_if(parts.begin(), parts.end(), under_question), parts.end());
        text_part(
            layout::question_heading,
            dialog.mod_question == ModQuestion::roll_back ? layout::roll_back_heading_text
                                                          : layout::switch_heading_text,
            DialogFont::small
        );
        control_part(layout::question_badge, no_control);
        const layout::ModRowText offered =
            layout::mod_row_text(dialog, ModRow{dialog.switch_question, false});
        parts.push_back(
            LayoutPart{
                layout::question_title,
                layout::cut_text(offered.title, layout::question_title.width, text_width),
                DialogFont::regular,
                0,
                no_control,
            }
        );
        parts.push_back(
            LayoutPart{
                layout::question_version,
                layout::cut_text(
                    layout::question_version_text(dialog, offered),
                    layout::question_version.width,
                    small_text_width
                ),
                DialogFont::small,
                0,
                no_control,
            }
        );
        const auto lines = layout::question_text_lines(dialog, small_text_width);
        for (std::size_t line = 0; line < lines.size(); ++line) {
            layout::SourceRect rect = layout::question_first_line;
            rect.y += static_cast<int32_t>(line) * rect.height;
            parts.push_back(LayoutPart{rect, lines[line], DialogFont::small, 0, no_control});
        }
        parts.push_back(
            LayoutPart{
                layout::question_no_rect(dialog),
                std::string(layout::shown_text(layout::no_text)),
                DialogFont::small,
                0,
                question_no_control,
            }
        );
        parts.push_back(
            LayoutPart{
                layout::question_yes_rect(dialog),
                std::string(
                    layout::shown_text(
                        dialog.mod_question == ModQuestion::roll_back ? layout::roll_back_text
                                                                      : layout::yes_text
                    )
                ),
                DialogFont::small,
                0,
                question_yes_control,
            }
        );
    }
    return parts;
}

} // namespace oa::ui::engine_settings
