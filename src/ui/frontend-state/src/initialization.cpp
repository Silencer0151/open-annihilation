// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/ui/frontend_state/initialization.hpp"
#include <algorithm>
#include <bit>
#include <cstring>
#include <stdexcept>

namespace oa::ui::frontend_state::initialization {
void reset_player_slots(State& state, PlayerStorage& storage, bool keep_chat_positions) {
    for (auto& record : storage.records) {
        std::array<uint8_t, 4> identity{};
        std::copy_n(record.begin() + player_offset::info, identity.size(), identity.begin());
        record.fill(0);
        std::copy(identity.begin(), identity.end(), record.begin() + player_offset::info);
    }
    state.local_player_index = 0;
    storage.viewpoint_player = 0;
    storage.slot_table_bytes.fill(0);
    for (std::size_t index = 0; index < storage.records.size(); ++index) {
        auto& record = storage.records[index];
        auto& descriptor = storage.descriptors[index];
        descriptor.fill(0);
        const auto first = "Player " + std::to_string(index) + " First";
        const auto second = "Player " + std::to_string(index) + " Second";
        std::copy(first.begin(), first.end(), record.begin() + player_offset::name);
        std::copy(second.begin(), second.end(), record.begin() + player_offset::second_name);
        record[player_offset::alliance + index] = 1;
        record[player_offset::allied_by + index] = 1;
        descriptor[player_offset::color] = static_cast<uint8_t>(index);
        std::fill_n(record.begin() + player_offset::player_id, 4, uint8_t{0xff});
        record[player_offset::index] = player_offset::inactive_index;
        record[player_offset::team] = player_offset::no_team;
        if (index < state.players.size()) {
            auto& slot = state.players[index];
            slot.present = false;
            slot.player_id = 0xffffffffU;
            slot.machine_flags = 0;
            slot.status = 0;
            slot.descriptor = Descriptor{};
        }
    }
    if (!keep_chat_positions) {
        storage.chat_head = 0;
        storage.chat_tail = 0;
    }
}

void select_map_list(MapListState& state, int32_t selector, MapListHost& host) {
    if (state.object.value != 0) {
        if (host.selector(state.object) == selector)
            return;
        host.destroy(state.object);
        host.release(state.object);
        // Avoid retaining a dangling portable handle if allocation/construct throws.
        state.object = {};
    }
    const auto allocation = host.allocate();
    if (allocation.value == 0) {
        state.object = {};
        return;
    }
    state.object = host.construct(allocation, selector);
}

namespace {
std::string checked(std::string value, std::size_t capacity) {
    if (value.size() >= capacity || value.find('\0') != std::string::npos)
        throw std::invalid_argument(
            "preference string exceeds the game's field capacity or contains NUL"
        );
    return value;
}

void set_flag(uint16_t& flags, uint16_t mask, uint32_t value) {
    flags = static_cast<uint16_t>(
        (flags & static_cast<uint16_t>(~mask)) | ((value & 1U) != 0 ? mask : 0)
    );
}

// The low byte takes source's bits under mask; the high byte of flags stays.
void copy_low_bit(uint16_t& flags, uint8_t source, uint16_t mask) {
    const auto low = static_cast<uint8_t>(static_cast<uint8_t>(flags) ^ source);
    flags = static_cast<uint16_t>((low & mask) ^ flags);
}
} // namespace

void restore_sound_options(
    Preferences& preferences, const OptionsEntrySnapshot& snapshot, PreferencesHost& host
) {
    preferences.fx_volume = snapshot.fx_volume;
    copy_low_bit(preferences.sound_flags, snapshot.sound_flags, preference_flags::ack_fx);
    copy_low_bit(preferences.sound_flags, snapshot.sound_flags, preference_flags::build_fx);
    // Speech is the snapshot build bit shifted up, not the snapshot speech bit.
    preferences.sound_flags = static_cast<uint16_t>(
        (static_cast<uint16_t>(snapshot.sound_flags & preference_flags::build_fx) << 1) |
        (preferences.sound_flags & static_cast<uint16_t>(~preference_flags::speech_fx))
    );
    host.audio_mode(
        (snapshot.sound_flags & preference_flags::sound_mode) == 2 ? AudioMode::spatial
                                                                   : AudioMode::flat
    );
    copy_low_bit(preferences.sound_flags, snapshot.sound_flags, preference_flags::sound_mode);
    preferences.unit_chat = snapshot.unit_chat;
}

void restore_music_options(
    Preferences& preferences, const OptionsEntrySnapshot& snapshot
) noexcept {
    preferences.music_volume = snapshot.music_volume;
    preferences.cd_mode = snapshot.cd_mode;
    copy_low_bit(preferences.music_flags, snapshot.music_flags, preference_flags::music_mode);
}

void restore_speed_options(
    Preferences& preferences, const OptionsEntrySnapshot& snapshot
) noexcept {
    preferences.text_scroll = snapshot.text_scroll;
    preferences.game_speed = snapshot.game_speed;
    preferences.current_game_speed = snapshot.game_speed;
    preferences.scroll_speed = snapshot.scroll_speed;
    preferences.interface_type = std::bit_cast<int32_t>(snapshot.interface_type);
    preferences.unit_chat = snapshot.unit_chat;
    preferences.unit_chat_text = snapshot.unit_chat_text;
    preferences.text_lines = snapshot.text_lines;
}

void restore_visual_options(
    Preferences& preferences, const State& state, const OptionsEntrySnapshot& snapshot
) noexcept {
    for (const uint16_t mask : {
             preference_flags::anti_alias,
             preference_flags::shadows,
             preference_flags::vehicle_shadows,
             preference_flags::feature_shadows,
             preference_flags::shading,
             preference_flags::dithered_fog,
         }) {
        copy_low_bit(preferences.graphics_flags, snapshot.graphics_flags, mask);
    }
    preferences.gamma = snapshot.gamma;
    // While the game loads or runs, both display stores are skipped.
    if ((state.session_flags & flags::loading) == 0) {
        preferences.display_width = snapshot.display_width;
        preferences.display_height = snapshot.display_height;
    }
}

void restore_all_options(
    Preferences& preferences,
    const State& state,
    const OptionsEntrySnapshot& snapshot,
    PreferencesHost& host
) {
    restore_sound_options(preferences, snapshot, host);
    restore_music_options(preferences, snapshot);
    restore_speed_options(preferences, snapshot);
    restore_visual_options(preferences, state, snapshot);
}

void reset_speed_options(Preferences& preferences) noexcept {
    preferences.text_scroll = 10;
    preferences.text_lines = 10;
    preferences.game_speed = 10;
    preferences.current_game_speed = 10;
    preferences.scroll_speed = 0x20;
    preferences.interface_type = 0;
    preferences.unit_chat = 10;
    preferences.unit_chat_text = 5;
}

void reset_visual_options(Preferences& preferences, const State& state) noexcept {
    for (const uint16_t mask : {
             preference_flags::anti_alias,
             preference_flags::shadows,
             preference_flags::vehicle_shadows,
             preference_flags::feature_shadows,
             preference_flags::shading,
         }) {
        preferences.graphics_flags = static_cast<uint16_t>(preferences.graphics_flags | mask);
    }
    preferences.gamma = 12;
    // While the game loads or runs, the display stores and the fog clear are skipped.
    if ((state.session_flags & flags::loading) == 0) {
        preferences.display_width = 640;
        preferences.display_height = 480;
        preferences.graphics_flags = static_cast<uint16_t>(
            preferences.graphics_flags & static_cast<uint16_t>(~preference_flags::dithered_fog)
        );
    }
}

void capture_options_entry(
    const Preferences& preferences, OptionsEntrySnapshot& snapshot
) noexcept {
    snapshot.interface_type = std::bit_cast<uint32_t>(preferences.interface_type);
    snapshot.graphics_flags = static_cast<uint8_t>(preferences.graphics_flags);
    snapshot.gamma = preferences.gamma;
    snapshot.fx_volume = preferences.fx_volume;
    snapshot.music_volume = preferences.music_volume;
    snapshot.music_flags = static_cast<uint8_t>(preferences.music_flags);
    snapshot.cd_mode = preferences.cd_mode;
    snapshot.unit_chat = preferences.unit_chat;
    snapshot.unit_chat_text = preferences.unit_chat_text;
    snapshot.sound_flags = static_cast<uint8_t>(preferences.sound_flags);
    snapshot.display_width = preferences.display_width;
    snapshot.display_height = preferences.display_height;
    snapshot.text_scroll = preferences.text_scroll;
    snapshot.text_lines = preferences.text_lines;
    // Game.requested_speed only; Game.current_speed is not read.
    snapshot.game_speed = preferences.game_speed;
    snapshot.scroll_speed = preferences.scroll_speed;
}

namespace {
bool track_type_name(const std::array<char, 16>& name) noexcept {
    constexpr char text[] = "TRACKTYPE";
    return std::strncmp(name.data(), text, name.size()) == 0;
}

/// Finds the music panel's TRACKTYPE control.
///
/// @param menu Music panel controls.
/// @return The control's index, or -1 when the scan finds none.
int32_t find_track_type(const TrackTypeMenu& menu) noexcept {
    // Scan from 1 while index < the root record's signed record count + 1.
    const int32_t limit = static_cast<int32_t>(menu.control_count) + 1;
    for (int32_t index = 1; index < limit; ++index) {
        const auto slot = static_cast<std::size_t>(index);
        if (slot >= menu.controls.size())
            continue;
        if (track_type_name(menu.controls[slot].name))
            return index;
    }
    return -1;
}
} // namespace

void copy_track_type(
    const Preferences& preferences,
    const TrackTypeMenu& menu,
    int32_t track_index,
    AudioTrackTypes& audio
) {
    // Only CD mode 4 (Game.cd_mode) copies; the controls are consulted only
    // then.
    if (preferences.cd_mode != 4)
        return;
    const int32_t index = find_track_type(menu);
    const uint8_t value = index < 0 ? menu.track_type_before_first
                                    : menu.controls[static_cast<std::size_t>(index)].track_type;
    audio.track_types[track_index] = value;
}

void load_preferences(
    State& state,
    game_entry::SkirmishSettings& settings,
    Preferences& p,
    PreferencesHost& h,
    const DisplayModeSetting& display_mode
) {
    const auto number = [&](std::string_view key, uint32_t fallback, bool persist = false) {
        const auto value = h.read_number(general_section, key);
        if (value)
            return *value;
        if (persist)
            h.write_number(general_section, key, fallback);
        return fallback;
    };
    const auto assign =
        [&]<class T>(
            T& destination, std::string_view key, uint32_t fallback, bool persist = false
        ) {
            const auto value = h.read_number(general_section, key);
            destination = static_cast<T>(value.value_or(fallback));
            if (!value && persist)
                h.write_number(general_section, key, fallback);
        };
    const auto bit = [&](uint16_t& flags,
                         uint16_t mask,
                         std::string_view key,
                         uint32_t fallback,
                         bool persist = false) {
        const auto value = h.read_number(general_section, key);
        set_flag(flags, mask, value.value_or(fallback));
        if (!value && persist)
            h.write_number(general_section, key, fallback);
    };
    const auto string = [&](std::string_view key, std::size_t capacity) {
        const auto value = h.read_string(general_section, key, capacity);
        return value ? checked(*value, capacity) : std::string{};
    };
    const auto rules = [&](Rules& value, std::string_view prefix) {
        const auto key = std::string(prefix);
        assign(value.commander_death, key + "CommanderDeath", 1, true);
        assign(value.mapping, key + "Mapping", 1, true);
        assign(value.line_of_sight, key + "LineOfSight", 1, true);
        assign(value.los_type, key + "LOSType", 1, true);
    };
    const auto interface = h.read_number(general_section, "Interface Type");
    p.interface_type = std::min(std::bit_cast<int32_t>(interface.value_or(0)), 1);
    if (!interface)
        h.write_number(general_section, "Interface Type", 0);
    assign(p.display_width, "DisplaymodeWidth", display_mode.width, true);
    assign(p.display_height, "DisplaymodeHeight", display_mode.height, true);
    if (display_mode.raise_smaller) {
        p.display_width = std::max(p.display_width, display_mode.width);
        p.display_height = std::max(p.display_height, display_mode.height);
    }
    assign(p.side, "side", 0, true);
    assign(p.difficulty, "Difficulty", 1, true);
    p.difficulty &= 0xffffU;
    assign(p.scroll_speed, "scrollspeed", 32, true);
    rules(p.single, "Single");
    assign(p.screen_chat, "screenchat", 1, true);
    bit(p.graphics_flags, preference_flags::damage_bars, "damagebars", 0, true);
    const auto sound = h.read_number(general_section, "Sound Mode");
    if (!sound) {
        h.audio_mode(AudioMode::unchanged);
        h.write_number(general_section, "Sound Mode", 1);
        p.sound_flags = static_cast<uint16_t>((p.sound_flags & ~preference_flags::sound_mode) | 1);
    } else {
        h.audio_mode(*sound == 2 ? AudioMode::spatial : AudioMode::flat);
        p.sound_flags = static_cast<uint16_t>(
            (p.sound_flags & ~preference_flags::sound_mode) |
            (*sound & preference_flags::sound_mode)
        );
    }
    h.mixing_buffers(number("MixingBuffers", 8));
    bit(p.sound_flags, preference_flags::restore_volume, "RestoreVolume", 0);
    if ((p.sound_flags & preference_flags::restore_volume) != 0) {
        if (const auto value = h.read_number(general_section, "WaveOutVolume"))
            h.wave_volume(*value);
        if (const auto value = h.read_number(general_section, "CDAudioVolume"))
            h.cd_volume(*value);
    }
    bit(p.graphics_flags, preference_flags::anti_alias, "Anti-Alias", 1, true);
    bit(p.graphics_flags, preference_flags::shadows, "Shadows", 1, true);
    bit(p.graphics_flags, preference_flags::feature_shadows, "FeatureShadows", 1, true);
    bit(p.graphics_flags, preference_flags::vehicle_shadows, "VehicleShadows", 1, true);
    bit(p.graphics_flags, preference_flags::shading, "Shading", 1, true);
    bit(p.graphics_flags, preference_flags::dithered_fog, "DitheredFog", 0, true);
    assign(p.gamma, "Gamma", 12, true);
    if (p.gamma == 10)
        p.gamma = 12;
    bit(p.graphics_flags, preference_flags::switch_alt, "SwitchAlt", 0, true);
    p.password = string("Password", 11);
    const auto override_enabled = h.nickname_override_enabled();
    const auto nickname = h.nickname_override();
    p.nickname = override_enabled != 0 && !nickname.empty() ? nickname.substr(0, 16)
                                                            : string("Nickname", 17);
    const auto game_name = h.game_name_override();
    p.game_name = game_name.empty() ? string("Game Name", 17) : game_name.substr(0, 16);
    // The engine keeps the folder whole, however deep the game is installed
    // and however long the user's name.
    const auto output = h.read_string(general_section, "Image Output Directory", any_length);
    const auto user = h.user_name();
    const std::string game_default =
        h.application_directory() + "\\" + (user && !user->empty() ? *user : "user_images");
    // A host's own folder stands in for the game's default, and for the
    // default stored as a choice.
    const std::string own_default = h.own_image_output_directory();
    if (output && (own_default.empty() || *output != game_default))
        p.image_output_directory = checked(*output, any_length);
    else
        p.image_output_directory =
            checked(own_default.empty() ? game_default : own_default, any_length);
    p.movie_output_rate = number("Movie Output Rate", 10);
    p.text_lines = number("textlines", 10);
    p.text_scroll = number("textscroll", 10);
    p.mouse_speed = number("mousespeed", 10);
    p.game_speed = static_cast<uint16_t>(number("gamespeed", 10));
    p.current_game_speed = p.game_speed;
    p.unit_chat = static_cast<uint8_t>(number("unitchat", 10));
    p.unit_chat_text = static_cast<uint8_t>(number("unitchattext", 5));
    bit(p.music_flags, preference_flags::music_mode, "musicmode", 1);
    p.cd_mode = static_cast<uint8_t>(number("cdmode", 4));
    bit(p.sound_flags, preference_flags::ack_fx, "ackfx", 1);
    bit(p.sound_flags, preference_flags::build_fx, "buildfx", 1);
    bit(p.sound_flags, preference_flags::speech_fx, "speechfx", 1);
    p.fx_volume = number("fxvol", 27);
    p.music_volume = number("musicvol", 32);
    bit(p.display_flags, preference_flags::clock, "clock", 0);
    settings.slot_count = std::bit_cast<int32_t>(number("NumSkirmishPlayers", 4));
    if (settings.slot_count < 0 ||
        static_cast<std::size_t>(settings.slot_count) > settings.slots.size())
        throw std::invalid_argument("NumSkirmishPlayers exceeds the slot storage");
    rules(p.multi, "Multi");
    rules(p.skirmish, "Skirmish");
    assign(p.skirmish_difficulty, "SkirmishDifficulty", 1, true);
    p.skirmish_difficulty &= 0xffffU;
    assign(p.skirmish_location, "SkirmishLocation", 1, true);
    const auto map = h.read_string(general_section, "SkirmishMap", 256);
    if (map)
        settings.map_name = checked(*map, 256);
    else {
        h.select_map_list(map_list_kind::skirmish);
        h.select_map_index(0);
        settings.map_name = checked(h.selected_map_name(), 256);
        h.select_map_list(map_list_kind::main_menu);
        h.write_string(general_section, "SkirmishMap", settings.map_name);
    }
    for (int32_t index = 0; index < settings.slot_count; ++index) {
        auto& slot = settings.slots[static_cast<std::size_t>(index)];
        const auto player = "Player" + std::to_string(index);
        const auto field = [&](std::string_view suffix, int32_t fallback) {
            const auto value = h.read_number(skirmish_section, player + std::string(suffix));
            return value ? std::bit_cast<int32_t>(*value) : fallback;
        };
        slot.controller = field("Controller", 0);
        slot.side = field("Side", index % 2);
        slot.color = field("Color", index);
        slot.alliance = field("AllyGroup", 5);
        slot.metal = field("Metal", 1000);
        slot.energy = field("Energy", 1000);
    }
    state.play_intro_movie = std::bit_cast<int32_t>(number("PlayMovie", 1));
    const auto depth = number("DisplaymodeDepth", 0);
    set_flag(
        p.display_flags,
        preference_flags::developer,
        depth == 256 && number("Games", 0) == 1 ? 1 : 0
    );
    p.display_flags = static_cast<uint16_t>(
        (p.display_flags | preference_flags::selection_boxes | preference_flags::tree_death) &
        ~preference_flags::no_shake
    );
    bit(p.campaign_unlock_flags, preference_flags::all_missions, "AllMissions", 0);
}

void write_all_missions(const Preferences& p, PreferencesHost& h) {
    h.write_number(
        general_section, "AllMissions", p.campaign_unlock_flags & preference_flags::all_missions
    );
}

void write_skirmish_player_count(const game_entry::SkirmishSettings& s, PreferencesHost& h) {
    h.write_number(general_section, "NumSkirmishPlayers", std::bit_cast<uint32_t>(s.slot_count));
}

void save_preferences(
    const State& state,
    const game_entry::SkirmishSettings& settings,
    Preferences& p,
    PreferencesHost& h
) {
    const auto number = [&](std::string_view key, uint32_t value) {
        h.write_number(general_section, key, value);
    };
    const auto bit = [&](std::string_view key, uint16_t flags, uint16_t mask) {
        number(key, (flags & mask) != 0 ? 1U : 0U);
    };
    const auto rules = [&](const Rules& value, std::string_view prefix) {
        const auto key = std::string(prefix);
        number(key + "CommanderDeath", value.commander_death);
        number(key + "Mapping", value.mapping);
        number(key + "LineOfSight", value.line_of_sight);
        number(key + "LOSType", value.los_type);
    };
    namespace flag = preference_flags;
    number("Interface Type", std::bit_cast<uint32_t>(p.interface_type));
    number("DisplaymodeWidth", p.display_width);
    number("DisplaymodeHeight", p.display_height);
    number("side", p.side);
    number("FixedLocations", p.skirmish_location);
    number("scrollspeed", p.scroll_speed);
    rules(p.single, "Single");
    number("screenchat", p.screen_chat);
    bit("damagebars", p.graphics_flags, flag::damage_bars);
    number("Sound Mode", p.sound_flags & flag::sound_mode);
    bit("RestoreVolume", p.sound_flags, flag::restore_volume);
    number("MixingBuffers", h.mixing_buffer_count());
    if ((p.sound_flags & flag::restore_volume) != 0) {
        number("WaveOutVolume", h.wave_out_volume());
        number("CDAudioVolume", h.cd_audio_volume());
    }
    bit("Anti-Alias", p.graphics_flags, flag::anti_alias);
    bit("Shadows", p.graphics_flags, flag::shadows);
    bit("FeatureShadows", p.graphics_flags, flag::feature_shadows);
    bit("VehicleShadows", p.graphics_flags, flag::vehicle_shadows);
    bit("Shading", p.graphics_flags, flag::shading);
    bit("DitheredFog", p.graphics_flags, flag::dithered_fog);
    number("Difficulty", p.difficulty);
    number("Gamma", p.gamma);
    bit("SwitchAlt", p.graphics_flags, flag::switch_alt);
    if (h.keep_stored_password() == 0)
        h.write_string(general_section, "Password", p.password);
    h.write_string(general_section, "Nickname", p.nickname);
    h.write_string(general_section, "Game Name", p.game_name);
    if (p.image_output_directory_changed != 0) {
        h.write_string(general_section, "Image Output Directory", p.image_output_directory);
        p.image_output_directory_changed = 0;
    }
    if (p.movie_output_rate_changed != 0) {
        number("Movie Output Rate", p.movie_output_rate);
        p.movie_output_rate_changed = 0;
    }
    number("unitchat", p.unit_chat);
    number("unitchattext", p.unit_chat_text);
    number("textlines", p.text_lines);
    number("textscroll", p.text_scroll);
    number("mousespeed", p.mouse_speed);
    number("gamespeed", p.game_speed);
    bit("clock", p.display_flags, flag::clock);
    bit("musicmode", p.music_flags, flag::music_mode);
    number("cdmode", p.cd_mode);
    bit("ackfx", p.sound_flags, flag::ack_fx);
    bit("buildfx", p.sound_flags, flag::build_fx);
    bit("speechfx", p.sound_flags, flag::speech_fx);
    number("fxvol", p.fx_volume);
    number("musicvol", p.music_volume);
    rules(p.multi, "Multi");
    rules(p.skirmish, "Skirmish");
    number("SkirmishLocation", p.skirmish_location);
    number("SkirmishDifficulty", p.skirmish_difficulty);
    h.write_string(general_section, "SkirmishMap", settings.map_name);
    if (settings.slot_count < 0 ||
        static_cast<std::size_t>(settings.slot_count) > settings.slots.size())
        throw std::invalid_argument("skirmish slot count exceeds the slot storage");
    for (int32_t index = 0; index < settings.slot_count; ++index) {
        const auto& slot = settings.slots[static_cast<std::size_t>(index)];
        const auto player = "Player" + std::to_string(index);
        const auto field = [&](std::string_view suffix, int32_t value) {
            h.write_number(
                skirmish_section, player + std::string(suffix), std::bit_cast<uint32_t>(value)
            );
        };
        field("Controller", slot.controller);
        field("Side", slot.side);
        field("Color", slot.color);
        field("AllyGroup", slot.alliance);
        field("Metal", slot.metal);
        field("Energy", slot.energy);
    }
    number("PlayMovie", std::bit_cast<uint32_t>(state.play_intro_movie));
}
} // namespace oa::ui::frontend_state::initialization
