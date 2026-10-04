// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/ui/engine_settings.hpp"

#include "oa/sim/session.hpp"

#include <algorithm>
#include <array>
#include <string>

namespace oa::ui::engine_settings {

namespace mod_profile = oa::data::mod_profile;

static_assert(default_unit_limit == oa::sim::session::kDefaultUnitLimit);
static_assert(lowest_stored_unit_limit == oa::sim::session::kMinUnitLimit);
static_assert(lowest_stored_unit_limit <= lowest_unit_limit);
static_assert(oa::sim::session::kMaxUnitLimit <= highest_unit_limit);
static_assert(highest_offered_unit_limit({}) == highest_unit_limit);
static_assert(raspberry_pi_frame_rate >= lowest_frame_rate);
static_assert(raspberry_pi_frame_rate <= highest_frame_rate);
static_assert((raspberry_pi_frame_rate - lowest_frame_rate) % frame_rate_step == 0);
static_assert(light_machine_frame_rate >= lowest_frame_rate);
static_assert(light_machine_frame_rate <= highest_frame_rate);
static_assert((light_machine_frame_rate - lowest_frame_rate) % frame_rate_step == 0);
static_assert((highest_text_size - lowest_text_size) % text_size_step == 0);
static_assert((default_text_size - lowest_text_size) % text_size_step == 0);
static_assert(
    lowest_text_size <= oa::present::game_font_text_size &&
    oa::present::game_font_text_size <= highest_text_size
);

namespace {

using oa::platform::preferences::Values;

/// Numbers past this size, either side of 0, read as it: every setting's
/// range lies far inside it.
constexpr int64_t number_bound = 1'000'000'000;

/// The section of totala.ini that holds the unit limit.
constexpr std::string_view installation_section = "Preferences";
/// The key of totala.ini's unit limit.
constexpr std::string_view installation_unit_limit_key = "UnitLimit";

/// A number read from the start of a text, and how much of the text it took.
struct LeadingNumber {
    int64_t value{};      ///< the number, within -number_bound..number_bound
    std::size_t length{}; ///< characters taken: the sign and the digits
};

/// Reads the whole decimal number at the start of a text: an optional sign,
/// then digits.
///
/// @param text the text
/// @return the number, its size held to number_bound; nothing without a digit
std::optional<LeadingNumber> leading_number(std::string_view text) noexcept {
    std::size_t at = 0;
    const bool negative = !text.empty() && text.front() == '-';
    if (!text.empty() && (text.front() == '-' || text.front() == '+'))
        at = 1;
    const std::size_t first_digit = at;
    int64_t magnitude = 0;
    while (at < text.size() && text[at] >= '0' && text[at] <= '9') {
        magnitude = std::min(magnitude * 10 + (text[at] - '0'), number_bound);
        ++at;
    }
    if (at == first_digit)
        return std::nullopt;
    return LeadingNumber{negative ? -magnitude : magnitude, at};
}

/// Returns the number a preferences key holds.
///
/// @param values the preferences
/// @param key the key
/// @return the number; nothing when the key is absent or its whole value is
///     not a decimal number
std::optional<int64_t> stored_number(const Values& values, std::string_view key) {
    const auto found = values.find(std::string{key});
    if (found == values.end())
        return std::nullopt;
    const auto number = leading_number(found->second);
    if (!number || number->length != found->second.size())
        return std::nullopt;
    return number->value;
}

/// Returns a stored number clamped into a setting's range.
///
/// @param number the stored number
/// @param lowest the setting's lowest value
/// @param highest the setting's highest value
/// @return the value
template <typename Value>
Value clamped(int64_t number, Value lowest, Value highest) noexcept {
    return static_cast<Value>(
        std::clamp(number, static_cast<int64_t>(lowest), static_cast<int64_t>(highest))
    );
}

/// Returns the anti-aliasing level a stored number reads as.
///
/// @param number the stored number
/// @return the highest level whose factor is not above `number`; off below 2
AntiAliasing anti_aliasing_from_number(int64_t number) noexcept {
    AntiAliasing level = AntiAliasing::off;
    for (const AntiAliasing candidate : anti_aliasing_levels)
        if (static_cast<int64_t>(candidate) <= number)
            level = candidate;
    return level;
}

/// Returns a text with the spaces, tabs and carriage returns at its ends removed.
///
/// @param text the text
/// @return the trimmed text, a view into `text`
std::string_view trimmed(std::string_view text) noexcept {
    constexpr std::string_view blanks = " \t\r";
    const auto first = text.find_first_not_of(blanks);
    if (first == std::string_view::npos)
        return {};
    return text.substr(first, text.find_last_not_of(blanks) - first + 1);
}

/// Tells whether two names are the same, ignoring the case of ASCII letters.
///
/// @param left a name
/// @param right a name
/// @return true when they match
bool same_name(std::string_view left, std::string_view right) noexcept {
    const auto lower = [](char letter) {
        return letter >= 'A' && letter <= 'Z' ? static_cast<char>(letter - 'A' + 'a') : letter;
    };
    return left.size() == right.size() &&
           std::equal(left.begin(), left.end(), right.begin(), [&](char a, char b) {
               return lower(a) == lower(b);
           });
}

/// Writes or erases one setting's key, as write_settings describes.
///
/// @param[in,out] values the preferences
/// @param key the setting's key
/// @param text the chosen value as stored
/// @param changed the chosen value differs from the one the dialog opened with
/// @param at_default the chosen value is the default
/// @param restored Restore defaults was pressed
void store(
    Values& values,
    std::string_view key,
    const std::string& text,
    bool changed,
    bool at_default,
    bool restored
) {
    if (restored && at_default)
        values.erase(std::string{key});
    else if (changed)
        values[std::string{key}] = text;
}

/// The stored text of the desktop's screen size.
constexpr std::string_view desktop_text = "desktop";

/// Returns a switch's stored text.
///
/// @param on the switch's state
/// @return "1" or "0"
std::string switch_text(bool on) {
    return on ? "1" : "0";
}

/// Returns the level of hardware acceleration a stored value reads as.
///
/// @param text the stored value
/// @return the level its word names; for a whole number, as the switch the
///     setting was before, Full above 0 and Off otherwise; nothing for any
///     other text
std::optional<HardwareAcceleration> stored_acceleration(std::string_view text) {
    if (const auto level = hardware_acceleration_from_text(text))
        return level;
    const auto number = leading_number(text);
    if (!number || number->length != text.size())
        return std::nullopt;
    return number->value > 0 ? HardwareAcceleration::full : HardwareAcceleration::off;
}

/// A switch of the Language & Text, the Touch or the Game files section: its
/// key and the member of EngineSettings it is.
struct TextSwitch {
    std::string_view key;            ///< its preferences key
    bool EngineSettings::* member{}; ///< its value
};

/// The Language & Text switches, read and written alike.
constexpr std::array<TextSwitch, 4> text_switches{{
    {key::modern_fonts, &EngineSettings::modern_fonts},
    {key::text_outline, &EngineSettings::text_outline},
    {key::text_shadow, &EngineSettings::text_shadow},
    {key::text_background, &EngineSettings::text_background},
}};

/// The Touch and Game files sections' switches, read and written as the
/// Language & Text ones.
constexpr std::array<TextSwitch, 3> touch_switches{{
    {key::touch_haptics, &EngineSettings::touch_haptics},
    {key::touch_left_handed, &EngineSettings::touch_left_handed},
    {key::game_files_backed_up, &EngineSettings::game_files_backed_up},
}};

/// Returns the key a profile's overrides are kept under.
///
/// @param profile_id the profile's id
/// @return key::hack_overrides followed by the id
std::string overrides_key(std::string_view profile_id) {
    return std::string{key::hack_overrides} + std::string{profile_id};
}

} // namespace

EngineSettings default_settings(const Inputs& inputs) {
    EngineSettings settings{};
    settings.escape_opens_menu = inputs.macos && inputs.players_own_profile;
    // Full for the player's own file on every machine: whether the graphics
    // card is used is decided apart, so the default never moves with it.
    settings.hardware_acceleration =
        inputs.players_own_profile ? HardwareAcceleration::full : HardwareAcceleration::off;
    // A named file draws game text in the game's own fonts.
    settings.modern_fonts = inputs.players_own_profile;
    // A named file plays in English, as the game does without a language on
    // its command line; the player's own follows the operating system.
    settings.language = inputs.players_own_profile
                            ? std::string(oa::data::languages::system_choice)
                            : std::string(oa::data::languages::english().tag);
    if (inputs.players_own_profile)
        settings.unit_limit =
            installation_unit_limit(inputs.installation_ini, inputs.units_per_player)
                .value_or(inputs.units_per_player.default_limit);
    else
        settings.unit_limit = inputs.units_per_player.default_limit;
    if (inputs.players_own_profile && inputs.raspberry_pi) {
        settings.max_frame_rate = raspberry_pi_frame_rate;
        settings.anti_aliasing = AntiAliasing::off;
    }
    if (inputs.players_own_profile && inputs.light_machine) {
        settings.max_frame_rate = light_machine_frame_rate;
        settings.anti_aliasing = AntiAliasing::off;
        const bool small_desktop = inputs.desktop != desktop_screen_size &&
                                   (inputs.desktop.width < light_machine_screen_size.width ||
                                    inputs.desktop.height < light_machine_screen_size.height);
        settings.screen_size =
            small_desktop ? small_desktop_screen_size : light_machine_screen_size;
    }
    return settings;
}

std::string screen_size_text(ScreenSize size) {
    if (size == desktop_screen_size)
        return std::string{desktop_text};
    return std::to_string(size.width) + "x" + std::to_string(size.height);
}

std::optional<ScreenSize> screen_size_from_text(std::string_view text) {
    for (const ScreenSize size : screen_sizes)
        if (text == screen_size_text(size))
            return size;
    return std::nullopt;
}

std::string_view hardware_acceleration_text(HardwareAcceleration level) noexcept {
    switch (level) {
    case HardwareAcceleration::off:
        return "off";
    case HardwareAcceleration::basic:
        return "basic";
    case HardwareAcceleration::full:
        return "full";
    }
    return {};
}

std::optional<HardwareAcceleration>
hardware_acceleration_from_text(std::string_view text) noexcept {
    for (const HardwareAcceleration level : hardware_acceleration_levels)
        if (text == hardware_acceleration_text(level))
            return level;
    return std::nullopt;
}

std::string_view touch_drag_text(TouchDrag drag) noexcept {
    switch (drag) {
    case TouchDrag::automatic:
        return "automatic";
    case TouchDrag::box:
        return "box";
    case TouchDrag::scroll:
        return "scroll";
    }
    return {};
}

std::optional<TouchDrag> touch_drag_from_text(std::string_view text) noexcept {
    for (const TouchDrag drag : touch_drag_choices)
        if (text == touch_drag_text(drag))
            return drag;
    return std::nullopt;
}

std::string_view touch_latches_text(TouchLatches latches) noexcept {
    switch (latches) {
    case TouchLatches::stay_on:
        return "stay-on";
    case TouchLatches::one_action:
        return "one-action";
    }
    return {};
}

std::optional<TouchLatches> touch_latches_from_text(std::string_view text) noexcept {
    for (const TouchLatches latches : touch_latches_choices)
        if (text == touch_latches_text(latches))
            return latches;
    return std::nullopt;
}

EngineSettings read_settings(
    const oa::platform::preferences::Values& values, const Inputs& inputs, bool switch_alt
) {
    EngineSettings settings = default_settings(inputs);
    settings.switch_alt = switch_alt;
    if (const auto number = stored_number(values, key::path_search_nodes))
        settings.path_search_nodes =
            clamped(*number, base_path_search_nodes, highest_path_search_nodes);
    if (const auto number = stored_number(values, key::wheel_zoom))
        settings.wheel_zoom = *number > 0;
    if (const auto number = stored_number(values, key::escape_opens_menu))
        settings.escape_opens_menu = *number > 0;
    if (const auto number = stored_number(values, key::unit_limit))
        settings.unit_limit = clamped(
            *number,
            inputs.units_per_player.minimum,
            highest_offered_unit_limit(inputs.units_per_player)
        );
    if (const auto number = stored_number(values, key::max_frame_rate))
        settings.max_frame_rate = clamped(*number, lowest_frame_rate, highest_frame_rate);
    if (const auto number = stored_number(values, key::anti_aliasing))
        settings.anti_aliasing = anti_aliasing_from_number(*number);
    if (const auto number = stored_number(values, key::frame_stats))
        settings.frame_stats = *number > 0;
    if (const auto found = values.find(std::string{key::screen_size}); found != values.end())
        settings.screen_size = screen_size_from_text(found->second).value_or(settings.screen_size);
    if (const auto found = values.find(std::string{key::hardware_acceleration});
        found != values.end())
        settings.hardware_acceleration =
            stored_acceleration(found->second).value_or(settings.hardware_acceleration);
    if (const auto number = stored_number(values, key::vertical_sync))
        settings.vertical_sync = *number > 0;
    for (const TextSwitch& entry : text_switches)
        if (const auto number = stored_number(values, entry.key))
            settings.*entry.member = *number > 0;
    if (const auto number = stored_number(values, key::text_size))
        settings.text_size = clamped(*number, lowest_text_size, highest_text_size);
    settings.language = stored_language(values, inputs.players_own_profile);
    if (const auto found = values.find(std::string{key::mod_directory}); found != values.end()) {
        const auto offered =
            std::find(inputs.mod_folders.begin(), inputs.mod_folders.end(), found->second);
        if (offered != inputs.mod_folders.end())
            settings.mod = static_cast<uint16_t>(offered - inputs.mod_folders.begin() + 1);
    }
    if (const auto number = stored_number(values, key::developer_mode))
        settings.developer_mode = *number > 0;
    if (const auto found = values.find(std::string{key::touch_drag}); found != values.end())
        settings.touch_drag = touch_drag_from_text(found->second).value_or(settings.touch_drag);
    if (const auto number = stored_number(values, key::touch_hold_delay))
        settings.touch_hold_ms = snapped_touch_hold_ms(*number);
    if (const auto found = values.find(std::string{key::touch_latches}); found != values.end())
        settings.touch_latches =
            touch_latches_from_text(found->second).value_or(settings.touch_latches);
    for (const TextSwitch& entry : touch_switches)
        if (const auto number = stored_number(values, entry.key))
            settings.*entry.member = *number > 0;
    if (!inputs.profile_id.empty())
        if (const auto found = values.find(overrides_key(inputs.profile_id)); found != values.end())
            settings.hack_overrides = mod_profile::read_overrides(found->second);
    return settings;
}

void write_settings(
    oa::platform::preferences::Values& values,
    const EngineSettings& opened,
    const EngineSettings& chosen,
    const EngineSettings& defaults,
    bool restored,
    std::span<const std::string> mod_folders,
    std::string_view profile_id
) {
    // No mod is stored as no key, never as an empty path.
    if ((restored && chosen.mod == defaults.mod) || (chosen.mod != opened.mod && chosen.mod == 0))
        values.erase(std::string{key::mod_directory});
    else if (chosen.mod != opened.mod && chosen.mod <= mod_folders.size())
        values[std::string{key::mod_directory}] = mod_folders[chosen.mod - 1U];
    store(
        values,
        key::path_search_nodes,
        std::to_string(chosen.path_search_nodes),
        chosen.path_search_nodes != opened.path_search_nodes,
        chosen.path_search_nodes == defaults.path_search_nodes,
        restored
    );
    store(
        values,
        key::wheel_zoom,
        switch_text(chosen.wheel_zoom),
        chosen.wheel_zoom != opened.wheel_zoom,
        chosen.wheel_zoom == defaults.wheel_zoom,
        restored
    );
    store(
        values,
        key::escape_opens_menu,
        switch_text(chosen.escape_opens_menu),
        chosen.escape_opens_menu != opened.escape_opens_menu,
        chosen.escape_opens_menu == defaults.escape_opens_menu,
        restored
    );
    store(
        values,
        key::unit_limit,
        std::to_string(chosen.unit_limit),
        chosen.unit_limit != opened.unit_limit,
        chosen.unit_limit == defaults.unit_limit,
        restored
    );
    store(
        values,
        key::max_frame_rate,
        std::to_string(chosen.max_frame_rate),
        chosen.max_frame_rate != opened.max_frame_rate,
        chosen.max_frame_rate == defaults.max_frame_rate,
        restored
    );
    store(
        values,
        key::anti_aliasing,
        std::to_string(static_cast<int>(chosen.anti_aliasing)),
        chosen.anti_aliasing != opened.anti_aliasing,
        chosen.anti_aliasing == defaults.anti_aliasing,
        restored
    );
    store(
        values,
        key::frame_stats,
        switch_text(chosen.frame_stats),
        chosen.frame_stats != opened.frame_stats,
        chosen.frame_stats == defaults.frame_stats,
        restored
    );
    store(
        values,
        key::screen_size,
        screen_size_text(chosen.screen_size),
        chosen.screen_size != opened.screen_size,
        chosen.screen_size == defaults.screen_size,
        restored
    );
    store(
        values,
        key::hardware_acceleration,
        std::string{hardware_acceleration_text(chosen.hardware_acceleration)},
        chosen.hardware_acceleration != opened.hardware_acceleration,
        chosen.hardware_acceleration == defaults.hardware_acceleration,
        restored
    );
    store(
        values,
        key::vertical_sync,
        switch_text(chosen.vertical_sync),
        chosen.vertical_sync != opened.vertical_sync,
        chosen.vertical_sync == defaults.vertical_sync,
        restored
    );
    for (const TextSwitch& entry : text_switches)
        store(
            values,
            entry.key,
            switch_text(chosen.*entry.member),
            chosen.*entry.member != opened.*entry.member,
            chosen.*entry.member == defaults.*entry.member,
            restored
        );
    store(
        values,
        key::text_size,
        std::to_string(chosen.text_size),
        chosen.text_size != opened.text_size,
        chosen.text_size == defaults.text_size,
        restored
    );
    store(
        values,
        key::language,
        chosen.language,
        chosen.language != opened.language,
        chosen.language == defaults.language,
        restored
    );
    store(
        values,
        key::developer_mode,
        switch_text(chosen.developer_mode),
        chosen.developer_mode != opened.developer_mode,
        chosen.developer_mode == defaults.developer_mode,
        restored
    );
    store(
        values,
        key::touch_drag,
        std::string{touch_drag_text(chosen.touch_drag)},
        chosen.touch_drag != opened.touch_drag,
        chosen.touch_drag == defaults.touch_drag,
        restored
    );
    store(
        values,
        key::touch_hold_delay,
        std::to_string(chosen.touch_hold_ms),
        chosen.touch_hold_ms != opened.touch_hold_ms,
        chosen.touch_hold_ms == defaults.touch_hold_ms,
        restored
    );
    store(
        values,
        key::touch_latches,
        std::string{touch_latches_text(chosen.touch_latches)},
        chosen.touch_latches != opened.touch_latches,
        chosen.touch_latches == defaults.touch_latches,
        restored
    );
    for (const TextSwitch& entry : touch_switches)
        store(
            values,
            entry.key,
            switch_text(chosen.*entry.member),
            chosen.*entry.member != opened.*entry.member,
            chosen.*entry.member == defaults.*entry.member,
            restored
        );
    // Restore defaults keeps the overrides: Restore profile values clears them.
    if (!profile_id.empty() && chosen.hack_overrides != opened.hack_overrides) {
        if (chosen.hack_overrides.empty())
            values.erase(overrides_key(profile_id));
        else
            values[overrides_key(profile_id)] = mod_profile::overrides_text(chosen.hack_overrides);
    }
}

std::string
stored_language(const oa::platform::preferences::Values& values, bool players_own_profile) {
    namespace languages = oa::data::languages;
    if (const auto found = values.find(std::string{key::language}); found != values.end()) {
        const std::string_view stored = trimmed(found->second);
        if (same_name(stored, languages::system_choice))
            return std::string(languages::system_choice);
        if (const auto* language = languages::find_by_tag(stored);
            language != nullptr && languages::drawable(*language))
            return std::string(language->tag);
    }
    return players_own_profile ? std::string(languages::system_choice)
                               : std::string(languages::english().tag);
}

std::optional<uint16_t>
installation_unit_limit(std::string_view ini_text, const oa::data::limits::UnitsPerPlayer& units) {
    std::string_view rest = ini_text.substr(0, std::min(ini_text.size(), installation_ini_limit));
    bool in_section = false;
    bool section_seen = false;
    while (!rest.empty()) {
        const auto end = rest.find('\n');
        const std::string_view line =
            trimmed(end == std::string_view::npos ? rest : rest.substr(0, end));
        rest = end == std::string_view::npos ? std::string_view{} : rest.substr(end + 1);
        if (line.empty() || line.front() == ';')
            continue;
        if (line.front() == '[') {
            if (section_seen)
                return std::nullopt;
            const auto close = line.find(']');
            const auto name =
                trimmed(line.substr(1, close == std::string_view::npos ? close : close - 1));
            in_section = same_name(name, installation_section);
            section_seen = in_section;
            continue;
        }
        if (!in_section)
            continue;
        const auto equals = line.find('=');
        if (equals == std::string_view::npos ||
            !same_name(trimmed(line.substr(0, equals)), installation_unit_limit_key))
            continue;
        const auto number = leading_number(trimmed(line.substr(equals + 1)));
        const int64_t limit = number ? std::max(number->value, int64_t{0}) : 0;
        return static_cast<uint16_t>(
            oa::sim::session::clamp_unit_limit(static_cast<int32_t>(limit), units)
        );
    }
    return std::nullopt;
}

int32_t path_search_multiplier(int32_t nodes) noexcept {
    const int64_t nearest = (int64_t{nodes} + base_path_search_nodes / 2) / base_path_search_nodes;
    return clamped(nearest, int32_t{1}, highest_path_search_multiplier);
}

int32_t match_path_search_nodes(
    const EngineSettings& settings, bool shared_or_replay, int32_t game_nodes
) noexcept {
    if (shared_or_replay)
        return game_nodes;
    const int64_t scaled =
        int64_t{settings.path_search_nodes} * game_nodes / base_path_search_nodes;
    return static_cast<int32_t>(
        std::clamp<int64_t>(scaled, 1, oa::data::limits::highest_path_search_nodes)
    );
}

Locks settings_locks(const GameState& state) noexcept {
    Locks locks{};
    const Lock game_lock = !state.in_game                      ? Lock::none
                           : state.shared_game || state.replay ? Lock::set_by_host
                                                               : Lock::in_game;
    locks.path_search = game_lock;
    locks.unit_limit = game_lock;
    locks.max_frame_rate = state.frame_rate_from_command_line ? Lock::command_line : Lock::none;
    locks.shared_game = state.in_game && state.shared_game;
    // A game never locks hardware acceleration: Off must stay possible in a
    // shared game, where On waits for the match to end.
    locks.hardware_acceleration = state.renderer_from_command_line ? Lock::command_line
                                  : state.acceleration_unavailable ? Lock::unavailable
                                                                   : Lock::none;
    locks.vertical_sync = state.vertical_sync_unavailable                        ? Lock::unavailable
                          : state.in_game && (state.shared_game || state.replay) ? Lock::in_game
                                                                                 : Lock::none;
    // The language changes only what players read, so no game locks it.
    locks.language = state.language_from_command_line ? Lock::command_line : Lock::none;
    return locks;
}

} // namespace oa::ui::engine_settings
