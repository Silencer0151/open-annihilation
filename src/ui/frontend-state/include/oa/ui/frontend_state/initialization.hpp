// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "oa/core/player.h"
#include "oa/core/player_setup.h"
#include "oa/ui/frontend_state/game_entry.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace oa::ui::frontend_state::initialization {
inline constexpr std::size_t reset_player_count = 11;
inline constexpr std::size_t player_record_bytes = 0x14b;
inline constexpr std::size_t player_descriptor_bytes = 0xb9;

// Where the reset writes each Player and PlayerSetupInfo field in the images.
namespace player_offset {
inline constexpr std::size_t player_id = offsetof(oa::Player, player_id);
inline constexpr std::size_t info = offsetof(oa::Player, info);
inline constexpr std::size_t name = offsetof(oa::Player, name);
inline constexpr std::size_t second_name = offsetof(oa::Player, second_name);
inline constexpr std::size_t alliance = offsetof(oa::Player, alliance);
inline constexpr std::size_t allied_by = offsetof(oa::Player, allied_by);
inline constexpr std::size_t team = offsetof(oa::Player, team);
inline constexpr std::size_t index = offsetof(oa::Player, index);
inline constexpr std::size_t color = offsetof(oa::PlayerSetupInfo, color);
inline constexpr uint8_t no_team = 5, inactive_index = 10;
} // namespace player_offset

// The player records and their setup records, kept as byte images so that
// every field the reset does not write keeps its bytes. Each record's
// Player.info bytes are an opaque identity of its setup record, which the
// reset keeps; they are never used as a pointer. The setup records keep their
// addresses within this aggregate.
struct PlayerStorage {
    std::array<std::array<uint8_t, player_record_bytes>, reset_player_count> records{};
    std::array<std::array<uint8_t, player_descriptor_bytes>, reset_player_count> descriptors{};
    std::array<uint8_t, 0x2c> slot_table_bytes{}; // Game.slot_table and the word after it
    uint8_t viewpoint_player{};
    uint16_t chat_head{}, chat_tail{};
};

/// Resets the eleven player records and their player info to the unjoined defaults.
///
/// Each record keeps its Player.info identity, is named "Player <n> First" and
/// "Player <n> Second", allies itself, and takes colour index = slot,
/// identifier 0xffffffff, team 5 and index 10. The ten dispatcher-visible
/// player entries of the state are reset too, and the local and viewpoint
/// players become 0.
///
/// @param[in,out] state Dispatcher state.
/// @param[out] storage Player record and descriptor images.
/// @param keep_chat_positions True keeps the chat queue positions.
void reset_player_slots(State& state, PlayerStorage& storage, bool keep_chat_positions);

struct MapListHandle {
    uintptr_t value{};
};

struct MapListState {
    MapListHandle object;
};

namespace map_list_kind {
inline constexpr int32_t main_menu = 0, selection_setup = 1, skirmish = 2, multiplayer = 3;
}

// Map-list object lifetime routines.
class MapListHost {
  public:

    virtual ~MapListHost() = default;

    /// Returns the selector a map-list object was built for.
    ///
    /// @param object Map-list object.
    /// @return Its map_list_kind value.
    virtual int32_t selector(MapListHandle object) = 0;

    /// Destroys a map-list object's contents.
    ///
    /// @param object Map-list object.
    virtual void destroy(MapListHandle object) = 0;

    /// Frees a map-list object's memory.
    ///
    /// @param object Map-list object.
    virtual void release(MapListHandle object) = 0;

    /// Allocates memory for a map-list object.
    ///
    /// @return The allocation, or a null handle on failure.
    virtual MapListHandle allocate() = 0;

    /// Builds a map-list object in an allocation.
    ///
    /// @param allocation Memory from allocate().
    /// @param selector map_list_kind value.
    /// @return The constructed object.
    virtual MapListHandle construct(MapListHandle allocation, int32_t selector) = 0;
};

/// Keeps the cached map-list object for a selector, rebuilding it when the selector differs.
///
/// @param[in,out] state Cached map-list object.
/// @param selector map_list_kind value.
/// @param[in,out] host Lifetime routines.
/// @quirk Allocation failure stores a null handle, as the game does.
void select_map_list(MapListState& state, int32_t selector, MapListHost& host);

struct Rules {
    uint32_t commander_death{}, mapping{}, line_of_sight{}, los_type{};
};

struct Preferences {
    int32_t interface_type{};
    uint32_t display_width{}, display_height{}, side{}, difficulty{};
    uint8_t scroll_speed{};
    Rules single, multi, skirmish;
    uint32_t screen_chat{};
    uint16_t graphics_flags{}, sound_flags{}, music_flags{}, display_flags{},
        campaign_unlock_flags{};
    uint32_t gamma{}, movie_output_rate{}, text_lines{}, text_scroll{}, mouse_speed{};
    uint16_t game_speed{}, current_game_speed{};
    uint8_t unit_chat{}, unit_chat_text{}, cd_mode{};
    uint32_t fx_volume{}, music_volume{}, skirmish_difficulty{}, skirmish_location{};
    std::string password, nickname, game_name;
    /// The folder screenshots, posters and movie captures go under, at any
    /// length the system's paths allow; Game.output_directory holds it only
    /// when it fits there.
    std::string image_output_directory;
    // Set when the console changes the value; the next save writes it and clears the flag.
    uint32_t image_output_directory_changed{}; // Game.output_directory_changed
    uint32_t movie_output_rate_changed{};      // Game.capture_rate_changed
};

namespace preference_flags {
inline constexpr uint16_t damage_bars = 1, anti_alias = 2, shadows = 4, vehicle_shadows = 8,
                          feature_shadows = 16, shading = 32, dithered_fog = 64, switch_alt = 256;
inline constexpr uint16_t sound_mode = 7, restore_volume = 8, ack_fx = 16, build_fx = 32,
                          speech_fx = 64;
inline constexpr uint16_t music_mode = 1, all_missions = 1;
// Bits of display_flags, which holds Game.console_flags (OA_CONSOLE_FLAG_*).
// Loading sets developer when DisplaymodeDepth is 256 and Games is 1, and
// clears it otherwise.
inline constexpr uint16_t developer = 2, selection_boxes = 4, tree_death = 8, no_shake = 16,
                          clock = 64;
} // namespace preference_flags

/// The capacity a string setting is read with when the engine keeps it at any
/// length, as it keeps a folder's path, rather than in a field of the game's.
inline constexpr std::size_t any_length = std::numeric_limits<std::size_t>::max();

inline constexpr std::string_view general_section = "Total Annihilation";
inline constexpr std::string_view skirmish_section = "Total Annihilation\\Skirmish";
// What a load or restore of "Sound Mode" does to the sound object's 3D switch:
// mode 2 turns it on, any other mode off; a missing key only reads it.
enum class AudioMode { unchanged, spatial, flat };

// Settings, device and naming services the preference code reaches.
class PreferencesHost {
  public:

    virtual ~PreferencesHost() = default;

    /// Reads a numeric setting.
    ///
    /// @param section Settings section.
    /// @param key Value name.
    /// @return The value, or nothing when absent.
    virtual std::optional<uint32_t> read_number(std::string_view section, std::string_view key) = 0;

    /// Writes a numeric setting.
    ///
    /// @param section Settings section.
    /// @param key Value name.
    /// @param value Value to store.
    virtual void write_number(std::string_view section, std::string_view key, uint32_t value) = 0;

    /// Reads a string setting.
    ///
    /// Strings too long for their field, or holding a NUL, are rejected before
    /// they can overrun the game's fixed-size fields.
    ///
    /// @param section Settings section.
    /// @param key Value name.
    /// @param capacity Field capacity including the terminating zero, or
    ///        any_length for a value the engine keeps at any length.
    /// @return The value, or nothing when absent.
    virtual std::optional<std::string>
    read_string(std::string_view section, std::string_view key, std::size_t capacity) = 0;

    /// Writes a string setting.
    ///
    /// @param section Settings section.
    /// @param key Value name.
    /// @param value Value to store.
    virtual void
    write_string(std::string_view section, std::string_view key, std::string_view value) = 0;

    /// Applies a sound mode to the sound object's 3D switch.
    ///
    /// @param mode What the load or restore does to the switch.
    virtual void audio_mode(AudioMode mode) = 0;

    /// Sets the number of mixing buffers.
    ///
    /// @param count MixingBuffers setting.
    virtual void mixing_buffers(uint32_t count) = 0;

    /// Restores the saved wave output volume.
    ///
    /// @param volume WaveOutVolume setting.
    virtual void wave_volume(uint32_t volume) = 0;

    /// Restores the saved CD-audio volume.
    ///
    /// @param volume CDAudioVolume setting.
    virtual void cd_volume(uint32_t volume) = 0;

    /// Reports whether a nickname override is active.
    ///
    /// @return Nonzero when active; the whole word is tested.
    virtual uint32_t nickname_override_enabled() = 0;

    /// Returns the nickname override.
    ///
    /// @return The override, empty for none.
    virtual std::string nickname_override() = 0;

    /// Returns the game-name override.
    ///
    /// @return The override, empty for none.
    virtual std::string game_name_override() = 0;

    /// Returns the operating-system user name.
    ///
    /// @return The name, or nothing when unavailable.
    virtual std::optional<std::string> user_name() = 0;

    /// Returns the application directory.
    ///
    /// @return The directory, without a trailing separator.
    virtual std::string application_directory() = 0;

    /// Returns the Image Output Directory the host gives in place of the
    /// game's default, the application directory's folder named after the
    /// user: used while the preferences hold none, or hold that default.
    ///
    /// @return The folder; empty to keep the game's default.
    virtual std::string own_image_output_directory() = 0;

    /// Selects the map list.
    ///
    /// @param selector map_list_kind value.
    virtual void select_map_list(int32_t selector) = 0;

    /// Selects a map of the current list.
    ///
    /// @param index Map index, from 0.
    virtual void select_map_index(int32_t index) = 0;

    /// Returns the name of the selected map.
    ///
    /// @return The map name.
    virtual std::string selected_map_name() = 0;

    /// Returns the sound object's mixing buffer count.
    ///
    /// @return The count.
    virtual uint32_t mixing_buffer_count() = 0;

    /// Returns the wave output device volume word.
    ///
    /// @return The packed volume, 0xffffffff when no device answers.
    virtual uint32_t wave_out_volume() = 0;

    /// Returns the CD-audio device volume word.
    ///
    /// @return The packed volume, 0xffffffff when no device answers.
    virtual uint32_t cd_audio_volume() = 0;

    /// Reports whether the preferences write keeps the stored password; the
    /// frontend's password is then not written.
    ///
    /// @return Nonzero to keep the stored password.
    virtual uint8_t keep_stored_password() = 0;
};

/// Stores the all-missions unlock bit under AllMissions.
///
/// @param p Preferences holding campaign_unlock_flags.
/// @param[in,out] h Settings services.
void write_all_missions(const Preferences& p, PreferencesHost& h);

/// Stores the skirmish player-slot count under NumSkirmishPlayers.
///
/// @param s Skirmish settings holding slot_count.
/// @param[in,out] h Settings services.
void write_skirmish_player_count(const game_entry::SkirmishSettings& s, PreferencesHost& h);

/// The display mode the DisplaymodeWidth and DisplaymodeHeight settings
/// start at when they are not stored, and whether a smaller stored one is
/// raised to it.
struct DisplayModeSetting {
    uint32_t width{640};  ///< pixels across when not stored
    uint32_t height{480}; ///< rows when not stored
    /// A stored width or height below this mode's is raised to it, each on
    /// its own; the raised value is not written back.
    bool raise_smaller{};
};

/// 3.1c's display mode setting: 640 by 480, a smaller stored mode kept.
inline constexpr DisplayModeSetting base_display_mode_setting{};
/// The display mode setting of a mod whose display rules keep only modes of
/// 768 rows or more (ui.display-modes min-height-768): 1024 by 768, a
/// smaller stored width or height raised to it.
inline constexpr DisplayModeSetting tall_display_mode_setting{1024, 768, true};

/// Loads the persisted display, sound, rules and skirmish preferences.
///
/// Keeps the game's defaults (writing many of them back when absent) and its
/// bit-preserving writes; restores the device volumes when RestoreVolume is
/// set; picks the first skirmish map when none is stored. The Image Output
/// Directory setting, and its default of the application directory and the
/// user name, are kept at any length.
///
/// @param[in,out] state Dispatcher state; play_intro_movie is loaded.
/// @param[out] settings Skirmish slot count, map and slots.
/// @param[in,out] p Preferences.
/// @param[in,out] h Settings, device and naming services.
/// @param display_mode The display mode setting's default and floor.
/// @throws std::invalid_argument for a slot count outside 0..11 or an
///         oversized string, instead of overrunning fixed fields.
void load_preferences(
    State& state,
    game_entry::SkirmishSettings& settings,
    Preferences& p,
    PreferencesHost& h,
    const DisplayModeSetting& display_mode = base_display_mode_setting
);

/// Writes every persisted display, sound, rules and skirmish preference back.
///
/// Device volumes are saved only with RestoreVolume set; the image directory
/// and movie rate only after a change, whose flags it then clears. The
/// password is skipped while the host keeps the stored one
/// (PreferencesHost::keep_stored_password).
///
/// @param state Dispatcher state; play_intro_movie is saved.
/// @param settings Skirmish map and slots.
/// @param[in,out] p Preferences; the change flags are cleared.
/// @param[in,out] h Settings and device services.
/// @throws std::invalid_argument for a slot count outside 0..11.
/// @quirk NumSkirmishPlayers is not written, as in 3.1c.
void save_preferences(
    const State& state,
    const game_entry::SkirmishSettings& settings,
    Preferences& p,
    PreferencesHost& h
);

// Preference image captured when the options screen opens; the UNDO buttons
// restore from it. Each entry names the live Game field it copies.
struct OptionsEntrySnapshot {
    uint32_t interface_type{}; // Game.interface_type
    uint8_t graphics_flags{};  // low byte of Game.graphics_flags
    uint32_t gamma{};          // Game.gamma
    uint32_t fx_volume{};      // Game.fx_volume
    uint32_t music_volume{};   // Game.music_volume
    uint8_t music_flags{};     // low byte of Game.music_flags
    uint8_t cd_mode{};         // Game.cd_mode
    uint8_t unit_chat{};       // Game.unit_sound_volume
    uint8_t unit_chat_text{};  // Game.unit_text_volume
    uint8_t sound_flags{};     // low byte of Game.sound_flags
    uint32_t display_width{};  // Game.screen_width
    uint32_t display_height{}; // Game.screen_height
    uint32_t text_scroll{};    // Game.text_scroll
    uint32_t text_lines{};     // Game.text_lines
    uint16_t game_speed{};     // Game.requested_speed; UNDO restores Game.current_speed too
    uint8_t scroll_speed{};    // Game.scroll_speed
};

/// Applies the sound panel UNDO: volumes, sound flags and audio mode from the entry image.
///
/// The caller then reapplies the saved volumes.
///
/// @param[in,out] preferences Live preferences.
/// @param snapshot Entry image.
/// @param[in,out] host Receives the audio mode.
/// @quirk The speech bit is restored from the image's build bit shifted up,
///        not from its speech bit.
void restore_sound_options(
    Preferences& preferences, const OptionsEntrySnapshot& snapshot, PreferencesHost& host
);

/// Applies the music panel UNDO: music volume, CD mode and music-mode bit.
///
/// @param[in,out] preferences Live preferences.
/// @param snapshot Entry image.
void restore_music_options(Preferences& preferences, const OptionsEntrySnapshot& snapshot) noexcept;

/// Applies the speeds panel UNDO: game/scroll speed, interface and chat-text settings.
///
/// @param[in,out] preferences Live preferences.
/// @param snapshot Entry image.
void restore_speed_options(Preferences& preferences, const OptionsEntrySnapshot& snapshot) noexcept;

/// Applies the visuals panel UNDO: graphics flags, gamma and the display size.
///
/// The caller then reapplies the saved volumes.
///
/// @param[in,out] preferences Live preferences.
/// @param state Dispatcher state; while flags::loading is set in session_flags
///        (the game is loading or running) the display size is kept.
/// @param snapshot Entry image.
void restore_visual_options(
    Preferences& preferences, const State& state, const OptionsEntrySnapshot& snapshot
) noexcept;

/// Applies the options CANCEL: the sound, music, speed and visual UNDO in that order.
///
/// The caller reapplies the saved volumes and gamma after them.
///
/// @param[in,out] preferences Live preferences.
/// @param state Dispatcher state.
/// @param snapshot Entry image.
/// @param[in,out] host Receives the audio mode.
void restore_all_options(
    Preferences& preferences,
    const State& state,
    const OptionsEntrySnapshot& snapshot,
    PreferencesHost& host
);

/// Applies the speeds panel RESTORE: the eight default speed and interface values.
///
/// @param[in,out] preferences Live preferences.
void reset_speed_options(Preferences& preferences) noexcept;

/// Applies the visuals panel RESTORE.
///
/// Sets the default graphics flags and gamma 12 and, unless flags::loading
/// is set, 640x480 with dithered fog cleared. The caller then reapplies the
/// saved volumes.
///
/// @param[in,out] preferences Live preferences.
/// @param state Dispatcher state; flags::loading in session_flags keeps the display settings.
void reset_visual_options(Preferences& preferences, const State& state) noexcept;

/// Captures the options-screen entry image before the options GUI is shown.
///
/// Only the fields listed in OptionsEntrySnapshot are copied.
///
/// @param preferences Live preferences.
/// @param[out] snapshot Entry image.
void capture_options_entry(const Preferences& preferences, OptionsEntrySnapshot& snapshot) noexcept;

// A menu control as the name scan sees it: its 16-byte gadget name and its
// button stage, the selected track type.
struct MenuControl {
    std::array<char, 16> name{};
    uint8_t track_type{};
};

// The music panel's control array. Scanning starts at control 1 (control 0 is
// the root) and stops before control_count + 1. A failed scan returns -1, which
// indexes track_type_before_first.
struct TrackTypeMenu {
    int16_t control_count{};
    std::vector<MenuControl> controls;
    uint8_t track_type_before_first{};
};

// The audio object's track-type bytes, keyed by the current track index.
struct AudioTrackTypes {
    std::map<int32_t, uint8_t> track_types;
};

/// Stores the TRACKTYPE control's value as the current track's type in CD mode 4.
///
/// @param preferences Live preferences; nothing happens unless cd_mode is 4.
/// @param menu Music panel controls.
/// @param track_index Current track.
/// @param[in,out] audio Track-type bytes of the audio object.
/// @quirk A missing TRACKTYPE control still stores the byte the -1 index reads.
void copy_track_type(
    const Preferences& preferences,
    const TrackTypeMenu& menu,
    int32_t track_index,
    AudioTrackTypes& audio
);
} // namespace oa::ui::frontend_state::initialization
