// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The settings dialog's sections, rows and controls, where they lie, and
// what pointer and key events do to them.

#include "oa/ui/engine_settings/dialog.hpp"

#include "geometry.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <string>
#include <utility>

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
constexpr std::array<Setting, 1> kGameplayRows{Setting::unit_limit};
/// Graphics' rows.
constexpr std::array<Setting, 5> kGraphicsRows{
    Setting::max_frame_rate,
    Setting::anti_aliasing,
    Setting::screen_size,
    Setting::hardware_acceleration,
    Setting::vertical_sync,
};
/// Developer's rows.
constexpr std::array<Setting, 1> kDeveloperRows{Setting::frame_stats};

/// The lowest level that draws units finer and needs the warning hint.
constexpr AntiAliasing kDemandingLevel = AntiAliasing::x8;

/// A switch setting and the member of EngineSettings it is.
struct SwitchMember {
    Setting setting{};               ///< the switch
    bool EngineSettings::* member{}; ///< its value
};

/// Every switch and its value: the one table switch_on and set_switch read.
constexpr std::array<SwitchMember, 5> kSwitches{{
    {Setting::wheel_zoom, &EngineSettings::wheel_zoom},
    {Setting::escape_opens_menu, &EngineSettings::escape_opens_menu},
    {Setting::switch_alt, &EngineSettings::switch_alt},
    {Setting::frame_stats, &EngineSettings::frame_stats},
    {Setting::vertical_sync, &EngineSettings::vertical_sync},
}};

/// Hardware acceleration's captions, in hardware_acceleration_levels' order.
constexpr std::array<std::string_view, 3> kAccelerationCaptions{"Off", "Basic", "Full"};
static_assert(
    kAccelerationCaptions.size() == hardware_acceleration_levels.size(),
    "every level of hardware acceleration has its caption"
);

/// Returns a switch's value in the settings.
///
/// @param setting the setting
/// @return its member; null for a setting that is not a switch
bool EngineSettings::* switch_member(Setting setting) noexcept {
    for (const SwitchMember& entry : kSwitches)
        if (entry.setting == setting)
            return entry.member;
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
constexpr std::array<StatusText, 30> kStatusTexts{{
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
    {AccelerationState::slow_frames, "Not in use for this run: frames were slow.", kProcessorDraws},
    {AccelerationState::next_start,
     "Takes effect from the next start.",
     "The processor draws and scales the view until then."},
    {AccelerationState::full_cannot_save, "Basic in use: the game cannot save its files.", {}},
    {AccelerationState::full_too_little_memory,
     "Basic in use: there is too little memory for Full.",
     {}},
    {AccelerationState::full_slow_frames,
     "Basic in use for this run: Full's frames were slow.",
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
    {AccelerationState::full_not_built, "Full is not in this build: Basic is in use.", {}},
    {AccelerationState::in_use_on_another_driver,
     "Basic in use, on another driver: one failed.",
     {}},
    {AccelerationState::in_use_less_smoothing,
     "Basic in use, with less smoothing: frames were slow.",
     {}},
    {AccelerationState::in_use_no_smoothing,
     "Basic in use; no smoothing when zoomed out here.",
     {}},
    {AccelerationState::full_in_use_less_anti_aliasing,
     "Full in use, less anti-aliasing: frames were slow.",
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
/// The second line of Full in use, by its anti-aliasing: none, 2 samples
/// across and 4.
constexpr std::string_view kFullReach = "Smoothed at every zoom.";
constexpr std::string_view kFullReachTwice = "Smoothed at every zoom; anti-aliasing 2x.";
constexpr std::string_view kFullReachFourfold = "Smoothed at every zoom; anti-aliasing 4x.";
/// The anti-aliasing kFullReachTwice and kFullReachFourfold name.
constexpr uint8_t kSupersampleTwice = 2;
constexpr uint8_t kSupersampleFourfold = 4;

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
/// @return true for the two Full in-use states
bool full_in_use(AccelerationState state) noexcept {
    return state == AccelerationState::full_in_use ||
           state == AccelerationState::full_in_use_less_anti_aliasing;
}

/// Returns the second line of Full in use: smoothed at every zoom, with its
/// anti-aliasing where there is any.
///
/// @param supersample the samples a pixel across; 1 for no anti-aliasing
/// @return the line
std::string_view full_line(uint8_t supersample) noexcept {
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
    return setting == Setting::path_search || setting == Setting::unit_limit ||
           setting == Setting::max_frame_rate || setting == Setting::screen_size;
}

Slider slider_of(Setting setting) noexcept {
    switch (setting) {
    case Setting::path_search:
        return Slider{highest_path_search_multiplier};
    case Setting::unit_limit:
        return Slider{(highest_unit_limit - lowest_unit_limit) / unit_limit_step + 1};
    case Setting::max_frame_rate:
        return Slider{
            static_cast<int32_t>((highest_frame_rate - lowest_frame_rate) / frame_rate_step + 1)
        };
    case Setting::screen_size:
        return Slider{static_cast<int32_t>(screen_sizes.size())};
    default:
        return Slider{2};
    }
}

int32_t stop_of(const EngineSettings& settings, Setting setting) noexcept {
    const int32_t last = slider_of(setting).stops - 1;
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
    default:
        break;
    }
    return std::clamp(stop, int32_t{0}, last);
}

void set_stop(EngineSettings& settings, Setting setting, int32_t stop) noexcept {
    const int32_t clamped = std::clamp(stop, int32_t{0}, slider_of(setting).stops - 1);
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
    return !is_slider(setting) && !is_strip(setting);
}

bool switch_on(const EngineSettings& settings, Setting setting) noexcept {
    const auto member = switch_member(setting);
    return member != nullptr && settings.*member;
}

void set_switch(EngineSettings& settings, Setting setting, bool on) noexcept {
    if (const auto member = switch_member(setting))
        settings.*member = on;
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
    placed.rows.reserve(settings.size());
    for (std::size_t index = 0; index < settings.size(); ++index) {
        Row& row = placed.rows.emplace_back();
        row.setting = settings[index];
        row.control = first_row_control + static_cast<int32_t>(index);
        row.lock = row_lock(locks, row.setting, section);
        row.hint_is_status = row_hint_is_status(row.setting, section);
        row.top = top;
        const int32_t label_top = top + 1 + row_padding;
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
        } else if (!is_slider(row.setting)) {
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
        }
        bottom += row_padding;
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

ScrolledRows open_rows(const Dialog& dialog) {
    ScrolledRows open{};
    open.rows = place_rows(dialog.page, dialog.locks, 0, dialog.section_hooks);
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
    const SourceRect inside{
        scroll_well.x + 1, scroll_well.y + 1, scroll_well.width - 2, scroll_well.height - 2
    };
    int32_t height = inside.height;
    if (content_height > view.height)
        height = std::max(least_thumb_height, inside.height * view.height / content_height);
    const int32_t travel = inside.height - height;
    int32_t top = inside.y;
    if (limit > 0)
        top += (travel * std::clamp(scroll, int32_t{0}, limit) + limit / 2) / limit;
    return {inside.x, top, inside.width, height};
}

int32_t scroll_at(int32_t thumb_top, int32_t limit, int32_t content_height) noexcept {
    const SourceRect thumb = scroll_thumb(0, limit, content_height);
    const int32_t travel = scroll_well.height - 2 - thumb.height;
    if (travel <= 0 || limit <= 0)
        return 0;
    const int32_t along = std::clamp(thumb_top - thumb.y, int32_t{0}, travel);
    return (limit * along + travel / 2) / travel;
}

SourceRect list_item(Page page) noexcept {
    const auto index = static_cast<int32_t>(page);
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
    case Page::developer:
        return "Developer";
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
    case Page::developer:
        return "DEVELOPER";
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
    case Setting::frame_stats:
        return "Show performance statistics";
    case Setting::hardware_acceleration:
        return "Hardware acceleration";
    case Setting::vertical_sync:
        return "Vertical sync";
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
        if (settings.anti_aliasing == AntiAliasing::x16)
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
    case Setting::frame_stats:
        lines = {"Frame and tick times over the battlefield.", {}};
        break;
    case Setting::hardware_acceleration:
        // Its hint is its status, which the host keeps up to date.
        return status_line(acceleration, line);
    case Setting::vertical_sync:
        lines = {"Each frame waits for the display: no tearing.", {}};
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
        return 2;
    default:
        return 1;
    }
}

std::string value_text(Setting setting, const EngineSettings& settings) {
    switch (setting) {
    case Setting::path_search:
        return std::to_string(path_search_multiplier(settings.path_search_nodes)) + "x";
    case Setting::unit_limit:
        return std::to_string(settings.unit_limit) + " per player";
    case Setting::max_frame_rate:
        return std::to_string(settings.max_frame_rate) + " fps";
    case Setting::screen_size:
        return settings.screen_size == desktop_screen_size
                   ? std::string{"Desktop"}
                   : std::to_string(settings.screen_size.width) + " x " +
                         std::to_string(settings.screen_size.height);
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
    }
    return {};
}

std::string_view level_caption(AntiAliasing level) noexcept {
    switch (level) {
    case AntiAliasing::off:
        return "Off";
    case AntiAliasing::x2:
        return "2x";
    case AntiAliasing::x3:
        return "3x";
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

/// Returns the control under a point that a press can act on.
///
/// A row's control answers only on the part the view shows, and a locked
/// row's takes no press. The scroll bar answers while the section scrolls.
///
/// @param open the open section's rows
/// @param x the point's column
/// @param y the point's row
/// @return the control; no_control when none is there
int32_t control_at(const layout::ScrolledRows& open, int32_t x, int32_t y) noexcept {
    if (contains(layout::view, x, y)) {
        for (const layout::Row& row : open.rows.rows) {
            if (row.lock == Lock::none && contains(row.control_area, x, y))
                return row.control;
        }
    }
    if (open.limit > 0 && contains(layout::scroll_hit, x, y))
        return scroll_bar_control;
    for (const int32_t control : {restore_control, cancel_control, ok_control}) {
        if (contains(layout::footer_button(control), x, y))
            return control;
    }
    for (std::size_t index = 0; index < layout::page_count; ++index) {
        const auto page = static_cast<Page>(index);
        if (contains(layout::list_item(page), x, y))
            return page_control(page);
    }
    return no_control;
}

/// Returns the controls the keyboard focus moves through, in order: the
/// open section's rows that can be changed, the footer's buttons left to
/// right, then the sections' entries.
///
/// @param rows the open section's rows
/// @return the controls
std::vector<int32_t> focus_order(const layout::Rows& rows) {
    std::vector<int32_t> order;
    for (const layout::Row& row : rows.rows) {
        if (row.lock == Lock::none)
            order.push_back(row.control);
    }
    order.push_back(restore_control);
    order.push_back(cancel_control);
    order.push_back(ok_control);
    for (int32_t index = 0; index < static_cast<int32_t>(layout::page_count); ++index)
        order.push_back(first_page_control + index);
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
    dialog.hovered = control_at(open, dialog.pointer_x, dialog.pointer_y);
}

/// Scrolls the open section to an offset, moving its placed rows with it.
/// No scroll changes a setting or moves the focus.
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
    layout::scroll_rows(open.rows, next - open.scroll);
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
    const auto order = focus_order(open.rows);
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
        next -= layout::page_step;
    else if (key == DialogKey::page_down)
        next += layout::page_step;
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
/// slider one stop, a level strip one level.
///
/// @param[in,out] settings the settings
/// @param setting the row's setting
/// @param up true for a step up
void step(EngineSettings& settings, Setting setting, bool up) noexcept {
    if (layout::is_slider(setting)) {
        layout::set_stop(settings, setting, layout::stop_of(settings, setting) + (up ? 1 : -1));
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
    case Setting::frame_stats:
        to.frame_stats = from.frame_stats;
        break;
    case Setting::hardware_acceleration:
        to.hardware_acceleration = from.hardware_acceleration;
        break;
    case Setting::vertical_sync:
        to.vertical_sync = from.vertical_sync;
        break;
    }
}

/// Resets every setting the dialog can change to its default. Each locked
/// setting, found through its row's lock on every section, keeps its value.
/// Each press also asks for the graphics card to be tried afresh, so it
/// reports a change even when no setting moved.
///
/// @param[in,out] dialog the dialog
/// @return DialogAction::changed
DialogAction restore_defaults(Dialog& dialog) {
    const EngineSettings before = dialog.chosen;
    EngineSettings restored = dialog.defaults;
    for (std::size_t index = 0; index < page_count; ++index) {
        const auto page = static_cast<Page>(index);
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
    dialog.pressed = no_control;
    dialog.dragging = false;
    return DialogAction::accepted;
}

/// Closes the dialog putting back what it opened with.
///
/// @param[in,out] dialog the dialog
/// @return DialogAction::cancelled
DialogAction cancel(Dialog& dialog) noexcept {
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
    dialog.page = page;
    dialog.wheel_rows = 0.0F;
    if (dialog.focused >= first_row_control)
        dialog.focused = page_control(page);
    return DialogAction::redraw;
}

/// Presses a button, or flips a switch, as Space or a click does.
///
/// @param[in,out] dialog the dialog
/// @param rows the open section's rows
/// @param control the control
/// @return what it asks of the host
DialogAction activate(Dialog& dialog, const layout::Rows& rows, int32_t control) {
    if (control == restore_control)
        return restore_defaults(dialog);
    if (control == cancel_control)
        return cancel(dialog);
    if (control == ok_control)
        return accept(dialog);
    if (control >= first_page_control &&
        control < first_page_control + static_cast<int32_t>(layout::page_count))
        return show_page(dialog, static_cast<Page>(control - first_page_control));
    const layout::Row* row = row_of(rows, control);
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
    const int32_t stops = layout::slider_of(row.setting).stops;
    layout::set_stop(dialog.chosen, row.setting, layout::stop_at(row.control_area, column, stops));
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
    case Page::developer:
        return layout::kDeveloperRows;
    }
    return {};
}

void open_dialog(
    Dialog& dialog,
    const EngineSettings& current,
    const EngineSettings& defaults,
    const Locks& locks,
    std::string_view version,
    Page page,
    const AccelerationStatus& acceleration
) {
    dialog = Dialog{};
    dialog.opened = current;
    dialog.chosen = current;
    dialog.defaults = defaults;
    dialog.locks = locks;
    dialog.acceleration = acceleration;
    dialog.version = std::string(version);
    dialog.page = page;
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
    if (dialog.dragging) {
        // The thumb follows the pointer's row only, and the offset the thumb.
        if (dialog.pressed == scroll_bar_control)
            return scroll_to(
                dialog,
                open,
                layout::scroll_at(y - dialog.scroll_grab, open.limit, open.content_height)
            );
        const layout::Row* row = row_of(open.rows, dialog.pressed);
        if (row != nullptr)
            return drag_to(dialog, *row, x);
    }
    const int32_t hovered = control_at(open, x, y);
    if (hovered == dialog.hovered)
        return DialogAction::none;
    dialog.hovered = hovered;
    return DialogAction::redraw;
}

DialogAction dialog_pointer_down(Dialog& dialog, int32_t x, int32_t y) {
    note_pointer(dialog, x, y);
    layout::ScrolledRows open = layout::open_rows(dialog);
    const int32_t control = control_at(open, x, y);
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
            layout::scroll_thumb(open.scroll, open.limit, open.content_height);
        if (y >= thumb.y && y < thumb.y + thumb.height) {
            dialog.scroll_grab = y - thumb.y;
            return DialogAction::redraw;
        }
        dialog.scroll_grab = thumb.height / 2;
        static_cast<void>(scroll_to(
            dialog, open, layout::scroll_at(y - dialog.scroll_grab, open.limit, open.content_height)
        ));
        return DialogAction::redraw;
    }
    if (dialog.focused != no_control)
        dialog.focused = control;
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
    const int32_t pressed = dialog.pressed;
    const bool dragged = dialog.dragging;
    dialog.pressed = no_control;
    dialog.dragging = false;
    dialog.scroll_grab = 0;
    if (pressed == no_control)
        return DialogAction::none;
    const int32_t control = control_at(open, x, y);
    dialog.hovered = control;
    if (dragged || control != pressed)
        return DialogAction::redraw;
    const layout::Row* row = row_of(open.rows, control);
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
    return activate(dialog, open.rows, control);
}

DialogAction dialog_key(Dialog& dialog, DialogKey key) {
    layout::ScrolledRows open = layout::open_rows(dialog);
    const layout::Rows& rows = open.rows;
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
        return or_shown(activate(dialog, rows, dialog.focused));
    const bool up = key == DialogKey::right;
    const layout::Row* row = row_of(rows, dialog.focused);
    if (row != nullptr) {
        if (row->lock != Lock::none)
            return shown;
        const EngineSettings before = dialog.chosen;
        step(dialog.chosen, row->setting, up);
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

std::vector<LayoutPart> dialog_layout(const Dialog& dialog) {
    std::vector<LayoutPart> parts;
    const auto text_part =
        [&parts](
            layout::SourceRect rect, std::string_view text, DialogFont font, int32_t tracking = 0
        ) { parts.push_back(LayoutPart{rect, std::string(text), font, tracking, no_control}); };
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
    for (std::size_t index = 0; index < layout::page_count; ++index) {
        const auto page = static_cast<Page>(index);
        const layout::SourceRect item = layout::list_item(page);
        parts.push_back(
            LayoutPart{
                {item.x + layout::list_text_offset,
                 item.y,
                 item.width - layout::list_text_offset - layout::list_text_margin,
                 item.height},
                std::string(layout::page_name(page)),
                DialogFont::regular,
                0,
                page_control(page),
            }
        );
    }
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
    const auto row_text = [&row_part](
                              layout::SourceRect rect, std::string_view text, DialogFont font
                          ) { row_part(LayoutPart{rect, std::string(text), font, 0, no_control}); };
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
                        std::string(layout::strip_caption(row.setting, level)),
                        DialogFont::small,
                        0,
                        control,
                    }
                );
            }
        } else if (layout::is_slider(row.setting)) {
            row_part(LayoutPart{row.control_area, {}, DialogFont::regular, 0, control});
            row_text(
                row.value, layout::value_text(row.setting, dialog.chosen), DialogFont::regular
            );
        } else if (row.control_area.width > 0) {
            const int32_t half = (row.control_area.width - 2) / 2;
            row_part(
                LayoutPart{
                    {row.control_area.x + 1,
                     row.control_area.y + 1,
                     half,
                     row.control_area.height - 2},
                    std::string(layout::off_text),
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
                    std::string(layout::on_text),
                    DialogFont::small,
                    0,
                    control,
                }
            );
        }
    }
    if (open.limit > 0)
        control_part(layout::scroll_well, scroll_bar_control);

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
                std::string(caption),
                DialogFont::small,
                0,
                control,
            }
        );
    }
    return parts;
}

} // namespace oa::ui::engine_settings
