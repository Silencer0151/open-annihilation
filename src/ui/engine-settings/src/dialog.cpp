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
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace oa::ui::engine_settings {

namespace geometry {

namespace {

/// AI & Pathfinding's rows.
constexpr std::array<Setting, 1> kPathSearchRows{Setting::path_search};
/// Controls & Input's rows.
constexpr std::array<Setting, 3> kControlsRows{
    Setting::wheel_zoom,
    Setting::escape_opens_menu,
    Setting::switch_alt,
};
/// Gameplay's rows.
constexpr std::array<Setting, 2> kGameplayRows{Setting::unit_limit, Setting::mod};
/// Graphics' rows.
constexpr std::array<Setting, 5> kGraphicsRows{
    Setting::max_frame_rate,
    Setting::anti_aliasing,
    Setting::screen_size,
    Setting::hardware_acceleration,
    Setting::vertical_sync,
};
/// Language & Text's rows: the language first, and the text size right
/// under the switch it needs.
constexpr std::array<Setting, 6> kLanguageTextRows{
    Setting::language,
    Setting::modern_fonts,
    Setting::text_size,
    Setting::text_outline,
    Setting::text_shadow,
    Setting::text_background,
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
/// The engine's sections, in the list's order.
constexpr std::array<Page, 6> kEnginePages{
    Page::path_search,
    Page::controls,
    Page::gameplay,
    Page::graphics,
    Page::language_text,
    Page::developer,
};
static_assert(kEnginePages.size() <= most_listed_pages);
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
constexpr std::array<SwitchMember, 13> kSwitches{{
    {Setting::wheel_zoom, &EngineSettings::wheel_zoom, nullptr},
    {Setting::escape_opens_menu, &EngineSettings::escape_opens_menu, nullptr},
    {Setting::switch_alt, &EngineSettings::switch_alt, nullptr},
    {Setting::developer_mode, &EngineSettings::developer_mode, nullptr},
    {Setting::frame_stats, &EngineSettings::frame_stats, nullptr},
    {Setting::vertical_sync, &EngineSettings::vertical_sync, nullptr},
    {Setting::modern_fonts, &EngineSettings::modern_fonts, nullptr},
    {Setting::text_outline, &EngineSettings::text_outline, nullptr},
    {Setting::text_shadow, &EngineSettings::text_shadow, nullptr},
    {Setting::text_background, &EngineSettings::text_background, nullptr},
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

/// Returns a screen size's place among screen_sizes.
///
/// @param size the screen size
/// @return its index; 0, the desktop's, for a size not offered
int32_t screen_size_index(ScreenSize size) noexcept {
    const auto found = std::find(screen_sizes.begin(), screen_sizes.end(), size);
    return found == screen_sizes.end() ? 0 : static_cast<int32_t>(found - screen_sizes.begin());
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
    case Setting::mod:
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
        return true;
    default:
        return false;
    }
}

Slider slider_of(Setting setting, uint16_t highest_offered_unit, size_t offered_mods) noexcept {
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
        return Slider{static_cast<int32_t>(screen_sizes.size())};
    case Setting::mod:
        return Slider{static_cast<int32_t>(offered_mods) + 1};
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
    default:
        return Slider{2};
    }
}

int32_t stops_of(
    const EngineSettings& settings,
    Setting setting,
    uint16_t highest_offered_unit,
    size_t offered_mods
) noexcept {
    const auto& options = settings.mod_options;
    if (setting == Setting::mex_snap_radius)
        return std::clamp(options.mex_snap_most, int32_t{1}, most_snap_radius) + 1;
    if (setting == Setting::wreck_snap_radius)
        return std::clamp(options.wreck_snap_most, int32_t{1}, most_snap_radius) + 1;
    return slider_of(setting, highest_offered_unit, offered_mods).stops;
}

int32_t stop_of(
    const EngineSettings& settings,
    Setting setting,
    uint16_t highest_offered_unit,
    size_t offered_mods
) noexcept {
    const int32_t last = stops_of(settings, setting, highest_offered_unit, offered_mods) - 1;
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
        stop = screen_size_index(settings.screen_size);
        break;
    case Setting::mod:
        stop = settings.mod;
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
    size_t offered_mods
) noexcept {
    const int32_t clamped = std::clamp(
        stop, int32_t{0}, stops_of(settings, setting, highest_offered_unit, offered_mods) - 1
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
        settings.screen_size = screen_sizes[static_cast<std::size_t>(clamped)];
        break;
    case Setting::mod:
        settings.mod = static_cast<uint16_t>(clamped);
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
    default:
        break;
    }
}

bool is_strip(Setting setting) noexcept {
    return setting == Setting::anti_aliasing || setting == Setting::hardware_acceleration;
}

Strip strip_of(Setting setting) noexcept {
    switch (setting) {
    case Setting::anti_aliasing:
        return Strip{anti_aliasing_levels.size(), level_width};
    case Setting::hardware_acceleration:
        return Strip{hardware_acceleration_levels.size(), acceleration_level_width};
    default:
        return Strip{};
    }
}

std::size_t strip_level(const EngineSettings& settings, Setting setting) noexcept {
    switch (setting) {
    case Setting::anti_aliasing:
        return level_index(settings.anti_aliasing);
    case Setting::hardware_acceleration: {
        const auto found = std::find(
            hardware_acceleration_levels.begin(),
            hardware_acceleration_levels.end(),
            settings.hardware_acceleration
        );
        return found == hardware_acceleration_levels.end()
                   ? 0
                   : static_cast<std::size_t>(found - hardware_acceleration_levels.begin());
    }
    default:
        return 0;
    }
}

void set_strip_level(EngineSettings& settings, Setting setting, std::size_t level) noexcept {
    const Strip strip = strip_of(setting);
    if (strip.levels == 0)
        return;
    const std::size_t clamped = std::min(level, strip.levels - 1);
    if (setting == Setting::anti_aliasing)
        settings.anti_aliasing = anti_aliasing_levels[clamped];
    else if (setting == Setting::hardware_acceleration)
        settings.hardware_acceleration = hardware_acceleration_levels[clamped];
}

std::string_view strip_caption(Setting setting, std::size_t level) noexcept {
    if (level >= strip_of(setting).levels)
        return {};
    if (setting == Setting::anti_aliasing)
        return level_caption(anti_aliasing_levels[level]);
    return kAccelerationCaptions[level];
}

bool is_switch(Setting setting) noexcept {
    return !is_slider(setting) && !is_strip(setting) && !is_choice(setting);
}

bool is_choice(Setting setting) noexcept {
    return setting == Setting::language;
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

std::size_t choice_count(Setting setting) {
    return setting == Setting::language ? 1 + offered_languages().size() : 0;
}

std::string
choice_text(Setting setting, std::size_t index, const oa::data::languages::Language* system) {
    if (setting != Setting::language || index >= choice_count(setting))
        return {};
    if (index == 0) {
        const auto& named = system != nullptr ? *system : oa::data::languages::english();
        return std::string(shown_text("System default")) + " (" + std::string(named.endonym) + ")";
    }
    return std::string(offered_languages()[index - 1]->endonym);
}

std::size_t choice_index(const EngineSettings& settings, Setting setting) {
    if (setting != Setting::language)
        return 0;
    const auto offered = offered_languages();
    for (std::size_t index = 0; index < offered.size(); ++index)
        if (offered[index]->tag == settings.language)
            return index + 1;
    return 0;
}

void set_choice(EngineSettings& settings, Setting setting, std::size_t index) {
    if (setting != Setting::language)
        return;
    const std::size_t clamped = std::min(index, choice_count(setting) - 1);
    settings.language = clamped == 0 ? std::string(oa::data::languages::system_choice)
                                     : std::string(offered_languages()[clamped - 1]->tag);
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
    case Setting::mex_snap_radius:
        return locks.mex_snap;
    case Setting::wreck_snap_radius:
        return locks.wreck_snap;
    case Setting::text_size:
        return locks.text_size;
    case Setting::language:
        return locks.language;
    default:
        return Lock::none;
    }
}

bool hint_is_status(Setting setting) noexcept {
    return setting == Setting::hardware_acceleration;
}

std::span<const Setting> section_settings(Page page, const SectionHooks* section) {
    if (section == nullptr || section->settings == nullptr)
        return page_settings(page);
    return section->settings(section->context, page);
}

Rows place_rows(Page page, const Locks& locks, int32_t scroll, const SectionHooks* section) {
    Rows placed{};
    int32_t top = first_row_top;
    const auto settings = section_settings(page, section);
    // Developer's own rows lie closer, over its list.
    const bool own_section = section != nullptr && section->settings != nullptr;
    const int32_t padding =
        page == Page::developer && !own_section ? developer_row_padding : row_padding;
    placed.rows.reserve(settings.size());
    for (std::size_t index = 0; index < settings.size(); ++index) {
        Row& row = placed.rows.emplace_back();
        row.setting = settings[index];
        row.control = first_row_control + static_cast<int32_t>(index);
        row.lock = row_lock(locks, row.setting, section);
        row.hint_is_status = row_hint_is_status(row.setting, section);
        row.top = top;
        const int32_t label_top = top + 1 + padding;
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
        row.hint_lines = hint_line_count(row.setting);
        int32_t bottom = label_top + label_line_height + hint_gap;
        for (std::size_t line = 0; line < row.hint_lines; ++line) {
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
            row.control_area = {content_left, bottom, choice_width, choice_line_height};
            bottom += choice_line_height;
        }
        bottom += padding;
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
    if (locks.text_size == Lock::none && !dialog.chosen.modern_fonts)
        locks.text_size = Lock::needs_modern_fonts;
    return locks;
}

ScrolledRows open_rows(const Dialog& dialog) {
    ScrolledRows open{};
    open.rows = place_rows(dialog.page, shown_locks(dialog), 0, dialog.section_hooks);
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
    const int32_t line = row.top + open.scroll;
    const int32_t highest = line - view.y;
    const int32_t lowest = index + 1 == open.rows.rows.size()
                               ? open.limit
                               : line + row.height - (view.y + view.height - 1);
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

SourceRect list_item(Page page) noexcept {
    const auto index = page_control(page) - first_page_control;
    int32_t top = list_first_top + index * (list_item_height + list_item_gap);
    if (page == Page::developer)
        top = list_divider().y + 1 + list_divider_margin;
    return {list_item_left, top, list_item_width, list_item_height};
}

SourceRect list_divider() noexcept {
    const int32_t above = static_cast<int32_t>(Page::developer);
    const int32_t row = list_first_top + above * (list_item_height + list_item_gap) -
                        list_item_gap + list_divider_margin;
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
    case Page::path_search:
        return "AI & Pathfinding";
    case Page::controls:
        return "Controls & Input";
    case Page::gameplay:
        return "Gameplay";
    case Page::graphics:
        return "Graphics";
    case Page::language_text:
        return "Language & Text";
    case Page::developer:
        return "Developer";
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
    case Page::path_search:
        return "AI & PATHFINDING";
    case Page::controls:
        return "CONTROLS & INPUT";
    case Page::gameplay:
        return "GAMEPLAY";
    case Page::graphics:
        return "GRAPHICS";
    case Page::language_text:
        return "LANGUAGE & TEXT";
    case Page::developer:
        return "DEVELOPER";
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
    case Setting::modern_fonts:
        return "Use modern fonts for game text";
    case Setting::text_outline:
        return "Font outline";
    case Setting::text_shadow:
        return "Font shadow";
    case Setting::text_background:
        return "Game text background";
    case Setting::text_size:
        return "Text size";
    case Setting::language:
        return "Language";
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
        lines = {"Full screen at this size, or a window of it.", "Applies from the next start."};
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
    case Setting::mod:
        lines = {"A mod folder from the game's mods folder.", "Applies from the next start."};
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
    }
    return line < lines.size() ? lines[line] : std::string_view{};
}

std::size_t hint_line_count(Setting setting) noexcept {
    switch (setting) {
    case Setting::escape_opens_menu:
    case Setting::unit_limit:
    case Setting::anti_aliasing:
    case Setting::screen_size:
    case Setting::hardware_acceleration:
    case Setting::mod:
    case Setting::modern_fonts:
    case Setting::text_size:
    case Setting::language:
        return 2;
    default:
        return 1;
    }
}

std::string value_text(
    Setting setting, const EngineSettings& settings, std::span<const std::string> mod_names
) {
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
    case Setting::mod:
        return settings.mod == 0 || settings.mod > mod_names.size()
                   ? std::string(shown_text("None"))
                   : mod_names[settings.mod - 1U];
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
    default:
        return {};
    }
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
    if (contains(layout::view, x, y)) {
        for (const layout::Row& row : open.rows.rows) {
            if (row.lock == Lock::none && contains(row.control_area, x, y))
                return row.control;
        }
    }
    if (open.limit > 0 && contains(open.area.hit, x, y))
        return scroll_bar_control;
    for (const int32_t control : {restore_control, cancel_control, ok_control}) {
        if (contains(layout::footer_button(control), x, y))
            return control;
    }
    for (const Page page : dialog_pages(dialog.kind)) {
        if (contains(layout::list_item(page), x, y))
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
    for (const layout::Row& row : open.rows.rows) {
        if (row.lock == Lock::none)
            order.push_back(row.control);
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
    order.push_back(restore_control);
    order.push_back(cancel_control);
    order.push_back(ok_control);
    for (const Page page : dialog_pages(dialog.kind))
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
/// @param[in,out] settings the settings
/// @param setting the row's setting
/// @param up true for a step up
/// @param highest_offered_unit the unit limit slider's highest value
/// @param offered_mods the mod folders the mod slider offers besides none
void step(
    EngineSettings& settings,
    Setting setting,
    bool up,
    uint16_t highest_offered_unit,
    size_t offered_mods
) {
    if (layout::is_choice(setting)) {
        const std::size_t choice = layout::choice_index(settings, setting);
        if (up)
            layout::set_choice(settings, setting, choice + 1);
        else if (choice > 0)
            layout::set_choice(settings, setting, choice - 1);
        return;
    }
    if (layout::is_slider(setting)) {
        layout::set_stop(
            settings,
            setting,
            layout::stop_of(settings, setting, highest_offered_unit, offered_mods) + (up ? 1 : -1),
            highest_offered_unit,
            offered_mods
        );
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
    const std::size_t choices = layout::choice_count(row->setting);
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
    const std::size_t choices = layout::choice_count(row.setting);
    const OpenList list{
        &row,
        layout::choice_list(row.control_area, choices),
        choices,
        layout::shown_choices(choices)
    };
    dialog.open_list = row.control;
    dialog.list_pressed = -1;
    dialog.list_first = 0;
    dialog.list_marked = static_cast<int32_t>(layout::choice_index(dialog.chosen, row.setting));
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
    const EngineSettings before = dialog.chosen;
    layout::set_choice(dialog.chosen, list.row->setting, static_cast<std::size_t>(item));
    close_list(dialog);
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
    case Setting::text_size:
        to.text_size = from.text_size;
        break;
    case Setting::language:
        to.language = from.language;
        break;
    case Setting::mod:
        to.mod = from.mod;
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
    EngineSettings restored = dialog.defaults;
    restored.mod_options = before.mod_options;
    // The overrides stay: Restore profile values clears them.
    restored.hack_overrides = before.hack_overrides;
    for (const Page page : dialog_pages(dialog.kind)) {
        for (const Setting setting : layout::section_settings(page, dialog.section_hooks)) {
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
    const auto pages = dialog_pages(dialog.kind);
    if (control >= first_page_control &&
        control < first_page_control + static_cast<int32_t>(pages.size()))
        return show_page(dialog, pages[static_cast<std::size_t>(control - first_page_control)]);
    if (layout::developer_page(dialog)) {
        if (control == active_only_control)
            return developer::set_active_only(dialog, !dialog.developer.active_only);
        if (control == restore_profile_control)
            return developer::restore_profile_values(dialog);
        if (const layout::ListRow* row = layout::list_row(open.list, control))
            return developer::activate(dialog, *row);
    }
    const layout::Row* row = row_of(open.rows, control);
    if (row != nullptr && row->lock == Lock::none && layout::is_choice(row->setting))
        return open_choices(dialog, *row);
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
        dialog.chosen, row.setting, dialog.highest_offered_unit, dialog.mod_names.size()
    );
    layout::set_stop(
        dialog.chosen,
        row.setting,
        layout::stop_at(row.control_area, column, stops),
        dialog.highest_offered_unit,
        dialog.mod_names.size()
    );
    return changed_or_redraw(dialog, before);
}

} // namespace

std::span<const Setting> page_settings(Page page) noexcept {
    switch (page) {
    case Page::path_search:
        return layout::kPathSearchRows;
    case Page::controls:
        return layout::kControlsRows;
    case Page::gameplay:
        return layout::kGameplayRows;
    case Page::graphics:
        return layout::kGraphicsRows;
    case Page::language_text:
        return layout::kLanguageTextRows;
    case Page::developer:
        return layout::kDeveloperRows;
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

std::span<const Page> dialog_pages(DialogKind kind) noexcept {
    return kind == DialogKind::mod_options ? std::span<const Page>(layout::kModPages)
                                           : std::span<const Page>(layout::kEnginePages);
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
    std::span<const std::string> mod_names,
    std::span<const oa::data::mod_profile::HackState> profile_hacks,
    const oa::data::languages::Language* system_language
) {
    dialog = Dialog{};
    dialog.system_language = system_language;
    dialog.highest_offered_unit = highest_offered_unit;
    dialog.mod_names.assign(mod_names.begin(), mod_names.end());
    dialog.opened = current;
    dialog.chosen = current;
    dialog.defaults = defaults;
    dialog.locks = locks;
    dialog.acceleration = acceleration;
    dialog.version = std::string(version);
    dialog.page = page;
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

DialogAction
set_acceleration_status(Dialog& dialog, const AccelerationStatus& acceleration) noexcept {
    if (dialog.acceleration == acceleration)
        return DialogAction::none;
    dialog.acceleration = acceleration;
    return DialogAction::redraw;
}

DialogAction dialog_pointer_move(Dialog& dialog, int32_t x, int32_t y) {
    note_pointer(dialog, x, y);
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
    if (hovered == dialog.hovered)
        return DialogAction::none;
    dialog.hovered = hovered;
    return DialogAction::redraw;
}

DialogAction dialog_pointer_down(Dialog& dialog, int32_t x, int32_t y) {
    note_pointer(dialog, x, y);
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
    return DialogAction::redraw;
}

DialogAction dialog_pointer_up(Dialog& dialog, int32_t x, int32_t y) {
    note_pointer(dialog, x, y);
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
    const layout::Row* row = row_of(open.rows, control);
    if (row != nullptr && layout::is_choice(row->setting))
        return open_choices(dialog, *row);
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
    if (row != nullptr) {
        if (row->lock != Lock::none)
            return shown;
        const EngineSettings before = dialog.chosen;
        step(dialog.chosen, row->setting, up, dialog.highest_offered_unit, dialog.mod_names.size());
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
    if (!dialog_contains(x, y) || dialog.pressed != no_control || !std::isfinite(notches))
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

std::vector<LayoutPart> dialog_layout(const Dialog& dialog) {
    std::vector<LayoutPart> parts;
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
    for (const Page page : dialog_pages(dialog.kind)) {
        const layout::SourceRect item = layout::list_item(page);
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
    for (const layout::Row& row : open.rows.rows) {
        // A locked row's control is drawn but takes no press.
        const int32_t control = row.lock == Lock::none ? row.control : no_control;
        row_text(row.label, layout::label_of(row.setting), DialogFont::regular);
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
        for (std::size_t line = 0; line < row.hint_lines; ++line)
            row_text(
                row.hints[line],
                layout::hint_line(row.setting, dialog.chosen, dialog.acceleration, line),
                DialogFont::small
            );
        if (layout::is_strip(row.setting) && row.control_area.width > 0) {
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
                    layout::choice_text(
                        row.setting,
                        layout::choice_index(dialog.chosen, row.setting),
                        dialog.system_language
                    ),
                    DialogFont::regular,
                    0,
                    control,
                }
            );
        } else if (layout::is_slider(row.setting)) {
            row_part(LayoutPart{row.control_area, {}, DialogFont::regular, 0, control});
            row_text(
                row.value,
                layout::value_text(row.setting, dialog.chosen, dialog.mod_names),
                DialogFont::regular
            );
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
                    layout::choice_text(
                        list->row->setting, static_cast<std::size_t>(item), dialog.system_language
                    ),
                    DialogFont::regular,
                    0,
                    no_control,
                }
            );
        }
    }
    return parts;
}

} // namespace oa::ui::engine_settings
