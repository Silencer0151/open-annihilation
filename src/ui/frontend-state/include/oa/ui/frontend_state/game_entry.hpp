// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "oa/ui/frontend_state/main_menu.hpp"
#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace oa::ui::frontend_state::game_entry {
using MenuHandle = main_menu::MenuHandle;
using Event = main_menu::Event;
using Disc = main_menu::Disc;
inline constexpr uint32_t menu_cursor_animation_index = 20; // cursor animation table index
inline constexpr uint32_t sound_argument = 0;
inline constexpr int32_t disc_message_width = 200;
inline constexpr int32_t validation_message_width = 480;
inline constexpr int32_t localized_message_width = 200; // localized message box
// Message box flags: show the OK button, fit the box to its longest line.
inline constexpr int32_t message_show_ok = 1;
inline constexpr int32_t message_fit_width = 1;
enum class Button { new_campaign, skirmish, load_game, options, previous_menu, any_mission };
enum class Sound { big_button, skirmish, options, previous, any_mission };
enum class Message {
    campaign_disc,
    multiplayer_disc,
    missing_terrain,
    opponents_required,
    map_capacity,
    same_alliance
};

/// Returns the gadget name of a SINGLE.GUI button.
///
/// @param value Button.
/// @return Its record name.
constexpr std::string_view resource_name(Button value) noexcept {
    switch (value) {
    case Button::new_campaign:
        return "NewCamp";
    case Button::skirmish:
        return "Skirmish";
    case Button::load_game:
        return "LoadGame";
    case Button::options:
        return "Options";
    case Button::previous_menu:
        return "PrevMenu";
    case Button::any_mission:
        return "AnyMsn";
    }
    return {};
}

/// Returns the ALLSOUND name of a single-player menu sound.
///
/// @param value Sound.
/// @return Its registered sound name.
constexpr std::string_view resource_name(Sound value) noexcept {
    switch (value) {
    case Sound::big_button:
        return "BigButton";
    case Sound::skirmish:
        return "skirmish";
    case Sound::options:
        return "options";
    case Sound::previous:
        return "Previous";
    case Sound::any_mission:
        return "bigButton"; // the game's spelling
    }
    return {};
}

/// Returns the text of a single-player menu message.
///
/// @param value Message.
/// @return The untranslated message text.
constexpr std::string_view message_text(Message value) noexcept {
    switch (value) {
    case Message::campaign_disc:
        return "Please insert the Campaign CD (Disc 2) and try again";
    case Message::multiplayer_disc:
        return "Please insert the Multiplayer CD (Disc 1) and try again";
    case Message::missing_terrain:
        return "The terrain for the selected map does not exist.";
    case Message::opponents_required:
        return "There must be at least one player and one computer opponent";
    case Message::map_capacity:
        return "There are too many players enabled for this map";
    case Message::same_alliance:
        return "All players may not be in the same allied group.";
    }
    return {};
}

// Routines shared by the single-player and skirmish handlers.
class Services {
  public:

    virtual ~Services() = default;

    /// Plays a menu sound.
    ///
    /// @param sound Sound to play.
    /// @param argument Second argument, always sound_argument (0).
    virtual void play_sound(Sound sound, uint32_t argument) = 0;

    /// Selects a cursor animation.
    ///
    /// @param index Cursor animation table index.
    virtual void select_cursor_animation(uint32_t index) = 0;

    /// Looks for a game disc.
    ///
    /// @param disc Disc wanted.
    /// @return Nonzero in the low byte when found; only that byte is tested.
    virtual uint32_t find_disc(Disc disc) = 0;

    /// Refreshes the archives read from the game discs.
    virtual void refresh_disc_archives() = 0;

    /// Translates a message.
    ///
    /// @param message Message to translate.
    /// @return The translated text.
    virtual std::string translate(Message message) = 0;

    /// Shows a frontend message box over the frontend panel.
    ///
    /// @param text Translated message.
    /// @param width Message box width in pixels.
    /// @param show_ok Nonzero shows the OK button (always 1 here).
    /// @param fit_width Nonzero fits the box to its longest line (always 1 here).
    virtual void show_frontend_message(
        std::string_view text, int32_t width, int32_t show_ok, int32_t fit_width
    ) = 0;

    /// Clears the selected record of the menu an event came from.
    ///
    /// @param menu Menu of the event.
    virtual void clear_event_selection(MenuHandle menu) = 0;

    /// Clears the selected record of the frontend panel.
    virtual void clear_frontend_selection() = 0;
};

// Routines the SINGLE.GUI handler calls.
class SinglePlayerHost : public Services {
  public:

    /// Reports whether a button was activated by the event.
    ///
    /// @param menu Menu the event came from.
    /// @param button Button to test.
    /// @return Nonzero when it was; the whole word is tested.
    virtual uint32_t button_result(MenuHandle menu, Button button) = 0;

    /// Opens the load-game screen.
    virtual void open_load_game() = 0;

    /// Opens the options screen.
    virtual void open_options() = 0;
};

/// Handles a SINGLE.GUI event (the callback the single-player setup installs).
///
/// NewCamp and AnyMsn need the campaign disc and Skirmish the multiplayer
/// disc; a missing disc shows a message. LoadGame and Options open their
/// screens and PrevMenu goes back.
///
/// @param[in,out] state Dispatcher state; the pending signal is set.
/// @param event Menu event.
/// @param[in,out] host Routines the handler calls.
void handle_single_player_event(State& state, const Event& event, SinglePlayerHost& host);

namespace controller {
inline constexpr int32_t disabled = 0;
inline constexpr int32_t human = 1;
inline constexpr int32_t computer = 2;
} // namespace controller

inline constexpr int32_t unassigned_alliance = 5;
// The skirmish settings block holds eleven slot records of 0x18 bytes.
inline constexpr std::size_t skirmish_slot_capacity = 11;

// One slot record of the skirmish settings block, its fields in this order.
struct SkirmishSlot {
    int32_t controller{};
    int32_t side{};
    int32_t alliance{};
    int32_t metal{}, energy{}, color{};
};

struct SkirmishSettings {
    std::array<SkirmishSlot, skirmish_slot_capacity> slots{};
    int32_t slot_count{}; // Game.slot_count
    std::string map_name; // the map name the settings block (Game.skirmish_info) refers to
};

/// Counts the computer-controlled skirmish slots.
///
/// @param settings Skirmish settings.
/// @return Slots with controller::computer among the first slot_count.
/// @throws std::invalid_argument for a slot_count outside 0..11.
int32_t computer_count(const SkirmishSettings& settings);

/// Counts the human skirmish slots.
///
/// @param settings Skirmish settings.
/// @return Slots with controller::human among the first slot_count.
/// @throws std::invalid_argument for a slot_count outside 0..11.
int32_t human_count(const SkirmishSettings& settings);

/// Reports whether every enabled slot shares one alliance group.
///
/// @param settings Skirmish settings.
/// @return True when the first enabled slot with an alliance matches every
///         enabled slot; false when no enabled slot has one.
/// @throws std::invalid_argument for a slot_count outside 0..11.
bool all_enabled_players_allied(const SkirmishSettings& settings);

// Routines the skirmish Start branch calls.
class SkirmishHost : public Services {
  public:

    /// Selects a map by name (the map list object at Game.game_options).
    ///
    /// @param map_name Map name.
    /// @return Nonzero when the map's terrain exists.
    virtual int32_t select_map(std::string_view map_name) = 0;

    /// Returns how many players the selected map holds.
    ///
    /// @return The capacity, compared signed.
    virtual int32_t map_player_capacity() = 0;

    /// Applies the skirmish slots to the player controls, descriptors and alliances.
    virtual void apply_skirmish_players() = 0;

    /// Saves the preferences.
    virtual void save_preferences() = 0;
};

/// Runs the Start branch of the SKIRMISH.GUI handler.
///
/// Only the Start branch, not the whole widget handler: the caller must first
/// do its selected-name bookkeeping and establish that Start matched. Other
/// skirmish controls need their own handlers. It validates the map terrain,
/// the human and computer counts, the map capacity and the alliances, and
/// shows the first failure as a message.
///
/// @param[in,out] state Dispatcher state; player_count and the pending signal change.
/// @param[in,out] settings Skirmish settings.
/// @param menu Menu the event came from.
/// @param[in,out] host Routines the branch calls.
/// @return True only after the setup callbacks run and the pending signal is
///         signal_id::proceed; it does not load terrain or run the simulation.
/// @quirk A failed disc check shows its message and still continues into
///        validation, as the game does.
bool start_selected_skirmish(
    State& state, SkirmishSettings& settings, MenuHandle menu, SkirmishHost& host
);

/// Marks every campaign mission unplayed and terminates the results string.
///
/// @param[in,out] state Dispatcher state whose mission_results are reset.
void reset_mission_results(State& state);
} // namespace oa::ui::frontend_state::game_entry
