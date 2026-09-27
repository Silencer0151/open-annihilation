// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/ui/frontend_state/initialization.hpp"
#include <bit>
#include <cstddef>
#include <iostream>
#include <algorithm>
#include <map>
#include <stdexcept>
#include <vector>
using namespace oa::ui::frontend_state;
namespace init = oa::ui::frontend_state::initialization;

namespace {
void require(bool v, const char* m) {
    if (!v)
        throw std::runtime_error(m);
}

struct SettingsHost final : init::PreferencesHost {
    std::map<std::string, uint32_t> numbers, written;
    std::map<std::string, std::string> strings;
    std::vector<std::string> calls;
    uint32_t override_enabled{};
    std::string nickname, game_name;
    std::optional<std::string> user;

    static std::string key(std::string_view section, std::string_view name) {
        return std::string(section) + ":" + std::string(name);
    }

    std::optional<uint32_t> read_number(std::string_view s, std::string_view k) override {
        calls.push_back("read:" + key(s, k));
        const auto it = numbers.find(key(s, k));
        if (it == numbers.end())
            return {};
        return it->second;
    }

    void write_number(std::string_view s, std::string_view k, uint32_t v) override {
        calls.push_back("write:" + key(s, k));
        written[key(s, k)] = v;
    }

    std::optional<std::string>
    read_string(std::string_view s, std::string_view k, std::size_t) override {
        calls.push_back("string:" + key(s, k));
        const auto it = strings.find(key(s, k));
        if (it == strings.end())
            return {};
        return it->second;
    }

    void write_string(std::string_view s, std::string_view k, std::string_view v) override {
        calls.push_back("write-string:" + key(s, k));
        strings[key(s, k)] = v;
    }

    void audio_mode(init::AudioMode mode) override {
        calls.push_back("audio:" + std::to_string(static_cast<unsigned>(mode)));
    }

    void mixing_buffers(uint32_t v) override { calls.push_back("buffers:" + std::to_string(v)); }

    void wave_volume(uint32_t v) override { calls.push_back("wave:" + std::to_string(v)); }

    void cd_volume(uint32_t v) override { calls.push_back("cd:" + std::to_string(v)); }

    uint32_t nickname_override_enabled() override { return override_enabled; }

    std::string nickname_override() override { return nickname; }

    std::string game_name_override() override { return game_name; }

    std::optional<std::string> user_name() override { return user; }

    std::string application_directory() override { return "root"; }

    void select_map_list(int32_t v) override { calls.push_back("map-list:" + std::to_string(v)); }

    void select_map_index(int32_t v) override { calls.push_back("map-index:" + std::to_string(v)); }

    std::string selected_map_name() override {
        calls.push_back("map-name");
        return "First Map";
    }

    uint32_t mixing_buffer_count() override { return buffers; }

    uint32_t wave_out_volume() override { return wave; }

    uint32_t cd_audio_volume() override { return cd; }

    uint8_t launched_by_service() override { return service; }

    void number(std::string_view key, uint32_t value) {
        numbers[SettingsHost::key(init::general_section, key)] = value;
    }

    uint32_t buffers{}, wave{}, cd{};
    uint8_t service{};
};

void preference_writers() {
    SettingsHost host;
    init::Preferences p;
    p.campaign_unlock_flags = 0x0003;
    init::write_all_missions(p, host);
    require(
        host.written[SettingsHost::key(init::general_section, "AllMissions")] == 1,
        "AllMissions stores only the unlock bit"
    );
    p.campaign_unlock_flags = 0x0002;
    init::write_all_missions(p, host);
    require(
        host.written[SettingsHost::key(init::general_section, "AllMissions")] == 0,
        "AllMissions clear"
    );
    game_entry::SkirmishSettings settings;
    settings.slot_count = 7;
    init::write_skirmish_player_count(settings, host);
    require(
        host.written[SettingsHost::key(init::general_section, "NumSkirmishPlayers")] == 7,
        "NumSkirmishPlayers stores the slot count"
    );
}

/// Checks the player reset's byte images, its dispatcher slots and the chat positions.
void player_slots() {
    State state;
    init::PlayerStorage storage;
    for (auto& p : storage.records)
        p.fill(0xab);
    for (auto& d : storage.descriptors)
        d.fill(0xdc);
    storage.slot_table_bytes.fill(0xff);
    state.local_player_index = 7;
    storage.viewpoint_player = 9;
    storage.chat_head = 11;
    storage.chat_tail = 12;
    init::reset_player_slots(state, storage, true);
    require(
        state.local_player_index == 0 && storage.viewpoint_player == 0 && storage.chat_head == 11 &&
            storage.chat_tail == 12,
        "chat positions kept"
    );
    require(storage.slot_table_bytes == decltype(storage.slot_table_bytes){}, "slot table cleared");
    for (std::size_t i = 0; i < 11; ++i) {
        std::array<uint8_t, sizeof(oa::Player)> expected{};
        const auto player_id = offsetof(oa::Player, player_id);
        std::fill_n(expected.begin() + player_id, sizeof(uint32_t), 0xff);
        const auto info = offsetof(oa::Player, info);
        std::fill_n(expected.begin() + info, sizeof(oa::oa_ref32), 0xab);
        const auto first = "Player " + std::to_string(i) + " First",
                   second = "Player " + std::to_string(i) + " Second";
        std::copy(first.begin(), first.end(), expected.begin() + offsetof(oa::Player, name));
        std::copy(
            second.begin(), second.end(), expected.begin() + offsetof(oa::Player, second_name)
        );
        expected[offsetof(oa::Player, alliance) + i] = 1;
        expected[offsetof(oa::Player, allied_by) + i] = 1;
        expected[offsetof(oa::Player, team)] = 5;
        expected[offsetof(oa::Player, index)] = 10;
        require(storage.records[i] == expected, "complete player byte image");
        std::array<uint8_t, sizeof(oa::PlayerSetupInfo)> descriptor{};
        descriptor[offsetof(oa::PlayerSetupInfo, color)] = static_cast<uint8_t>(i);
        require(storage.descriptors[i] == descriptor, "complete descriptor byte image");
    }
    for (const auto& p : state.players)
        require(
            !p.present && p.player_id == 0xffffffffU && p.descriptor.has_value() &&
                p.descriptor->role == 0,
            "dispatcher player projection"
        );
    init::reset_player_slots(state, storage, false);
    require(storage.chat_head == 0 && storage.chat_tail == 0, "ordinary reset fields");
}

void preferences() {
    State state;
    game_entry::SkirmishSettings settings;
    init::Preferences p;
    SettingsHost host;
    p.graphics_flags = p.sound_flags = p.music_flags = p.display_flags = p.campaign_unlock_flags =
        0xffff;
    settings.slots[10].metal = 77;
    init::load_preferences(state, settings, p, host);
    require(
        p.interface_type == 0 && p.display_width == 640 && p.display_height == 480 && p.side == 0 &&
            p.difficulty == 1 && p.scroll_speed == 32,
        "display defaults"
    );
    require(
        p.single.commander_death == 1 && p.multi.mapping == 1 && p.skirmish.los_type == 1 &&
            p.screen_chat == 1,
        "rule defaults"
    );
    require(
        p.graphics_flags == 0xfebe && p.sound_flags == 0xfff1 && p.music_flags == 0xffff &&
            p.display_flags == 0xffad && p.campaign_unlock_flags == 0xfffe,
        "preserve unrelated flag bits"
    );
    require(
        p.gamma == 12 && p.fx_volume == 27 && p.music_volume == 32 && p.cd_mode == 4 &&
            p.unit_chat_text == 5 && p.unit_chat == 10,
        "audio/text defaults"
    );
    require(
        p.game_speed == 10 && p.current_game_speed == 10 && p.mouse_speed == 10 &&
            p.movie_output_rate == 10 && p.text_lines == 10 && p.text_scroll == 10,
        "speed defaults"
    );
    require(
        p.password.empty() && p.nickname.empty() && p.game_name.empty() &&
            p.image_output_directory == "root\\user_images",
        "string defaults"
    );
    require(
        settings.slot_count == 4 && settings.map_name == "First Map" &&
            state.play_intro_movie == 1 && p.skirmish_difficulty == 1 && p.skirmish_location == 1,
        "skirmish defaults"
    );
    for (int i = 0; i < 4; ++i) {
        const auto& s = settings.slots[static_cast<std::size_t>(i)];
        require(
            s.controller == 0 && s.side == i % 2 && s.color == i && s.alliance == 5 &&
                s.metal == 1000 && s.energy == 1000,
            "slot defaults"
        );
    }
    require(settings.slots[10].metal == 77, "outside active slots preserved");
    require(host.written.size() == 31, "only the game's persisted defaults");
    const auto map = std::find(host.calls.begin(), host.calls.end(), "map-list:2");
    require(
        map != host.calls.end() && std::vector<std::string>(map, map + 5) ==
                                       std::vector<std::string>{
                                           "map-list:2",
                                           "map-index:0",
                                           "map-name",
                                           "map-list:0",
                                           "write-string:Total Annihilation:SkirmishMap"
                                       },
        "map fallback ordering"
    );
    require(
        !host.written.contains(SettingsHost::key(init::general_section, "NumSkirmishPlayers")),
        "unpersisted default"
    );
    SettingsHost custom;
    custom.number("Interface Type", 0xffffffffU);
    custom.number("Difficulty", 0x12345678);
    custom.number("scrollspeed", 0x123);
    custom.number("Gamma", 10);
    custom.number("Sound Mode", 2);
    custom.number("RestoreVolume", 3);
    custom.number("WaveOutVolume", 77);
    custom.number("CDAudioVolume", 88);
    custom.number("gamespeed", 0x12345678);
    custom.number("damagebars", 2);
    custom.number("NumSkirmishPlayers", 1);
    custom.number("DisplaymodeDepth", 256);
    custom.number("Games", 1);
    custom.number("PlayMovie", 0);
    custom.numbers[SettingsHost::key(init::skirmish_section, "Player0Controller")] = 2;
    custom.strings[SettingsHost::key(init::general_section, "SkirmishMap")] = "Saved Map";
    custom.override_enabled = 0x100;
    custom.nickname = "abcdefghijklmnopq";
    custom.game_name = "My Game";
    custom.user = "User";
    init::load_preferences(state, settings, p, custom);
    require(
        p.interface_type == -1 && p.difficulty == 0x5678 && p.scroll_speed == 0x23 &&
            p.game_speed == 0x5678 && p.gamma == 12,
        "signed cap and narrowing"
    );
    require(
        p.nickname == "abcdefghijklmnop" && p.game_name == "My Game" &&
            p.image_output_directory == "root\\User",
        "overrides"
    );
    require(
        settings.map_name == "Saved Map" && settings.slots[0].controller == 2 &&
            state.play_intro_movie == 0,
        "stored values"
    );
    require((p.display_flags & 2) != 0 && (p.graphics_flags & 1) == 0, "bit extraction");
    require(
        std::find(custom.calls.begin(), custom.calls.end(), "wave:77") != custom.calls.end() &&
            std::find(custom.calls.begin(), custom.calls.end(), "cd:88") != custom.calls.end(),
        "volume restore"
    );
    for (auto count : {12U, 0xffffffffU}) {
        SettingsHost bad;
        bad.number("NumSkirmishPlayers", count);
        bool caught = false;
        try {
            init::load_preferences(state, settings, p, bad);
        } catch (const std::invalid_argument&) {
            caught = true;
        }
        require(caught, "reject invalid slots");
    }
}

struct Maps final : init::MapListHost {
    int32_t current{};
    std::vector<std::string> calls;
    bool fail{};

    int32_t selector(init::MapListHandle h) override {
        require(h.value == 7, "old object");
        calls.push_back("selector");
        return current;
    }

    void destroy(init::MapListHandle h) override {
        require(h.value == 7, "destroy identity");
        calls.push_back("destroy");
    }

    void release(init::MapListHandle h) override {
        require(h.value == 7, "release identity");
        calls.push_back("release");
    }

    init::MapListHandle allocate() override {
        calls.push_back("allocate");
        return {fail ? 0U : 8U};
    }

    init::MapListHandle construct(init::MapListHandle h, int32_t s) override {
        require(h.value == 8 && s == 2, "construct args");
        calls.push_back("construct");
        return {9};
    }
};

void save_preferences() {
    State state;
    state.play_intro_movie = 0;
    game_entry::SkirmishSettings settings;
    settings.slot_count = 2;
    settings.map_name = "Lava Run";
    settings.slots[0] = {1, 0, 5, 1000, 2000, 3};
    settings.slots[1] = {2, 1, 2, -1, 700, 9};
    init::Preferences p;
    p.interface_type = 1;
    p.display_width = 1024;
    p.display_height = 768;
    p.side = 1;
    p.difficulty = 2;
    p.scroll_speed = 32;
    p.single = {1, 0, 1, 0};
    p.multi = {0, 1, 0, 1};
    p.skirmish = {2, 1, 1, 0};
    p.screen_chat = 1;
    // Damage bars, shadows, dithered fog and SwitchAlt; bit 7 is not a saved flag.
    p.graphics_flags = 0x1c5;
    // Sound mode 6, RestoreVolume, build and speech acknowledgements.
    p.sound_flags = 0x6e;
    p.music_flags = 0;
    p.display_flags = 0x40;
    p.gamma = 12;
    p.movie_output_rate = 15;
    p.text_lines = 7;
    p.text_scroll = 8;
    p.mouse_speed = 9;
    p.game_speed = 14;
    p.unit_chat = 3;
    p.unit_chat_text = 4;
    p.cd_mode = 4;
    p.fx_volume = 27;
    p.music_volume = 32;
    p.skirmish_difficulty = 1;
    p.skirmish_location = 0;
    p.password = "pw";
    p.nickname = "Nick";
    p.game_name = "Game";
    p.image_output_directory = "shots";
    p.image_output_directory_changed = 1;
    SettingsHost host;
    host.buffers = 8;
    host.wave = 0xffffffffU;
    host.cd = 0x8000;
    init::save_preferences(state, settings, p, host);
    const std::vector<std::string> keys{
        "Interface Type",
        "DisplaymodeWidth",
        "DisplaymodeHeight",
        "side",
        "FixedLocations",
        "scrollspeed",
        "SingleCommanderDeath",
        "SingleMapping",
        "SingleLineOfSight",
        "SingleLOSType",
        "screenchat",
        "damagebars",
        "Sound Mode",
        "RestoreVolume",
        "MixingBuffers",
        "WaveOutVolume",
        "CDAudioVolume",
        "Anti-Alias",
        "Shadows",
        "FeatureShadows",
        "VehicleShadows",
        "Shading",
        "DitheredFog",
        "Difficulty",
        "Gamma",
        "SwitchAlt"
    };
    for (std::size_t i = 0; i < keys.size(); ++i)
        require(
            host.calls[i] == "write:" + SettingsHost::key(init::general_section, keys[i]),
            "save writes the display and sound keys in the game's order"
        );
    const auto at = [&](std::string_view key) {
        return host.written.at(SettingsHost::key(init::general_section, key));
    };
    const auto player = [&](std::string_view key) {
        return host.written.at(SettingsHost::key(init::skirmish_section, key));
    };
    require(at("Interface Type") == 1 && at("DisplaymodeWidth") == 1024, "display values");
    require(at("side") == 1 && at("FixedLocations") == 0 && at("scrollspeed") == 32, "side");
    require(at("SingleCommanderDeath") == 1 && at("SingleMapping") == 0, "single rules");
    require(at("damagebars") == 1 && at("Anti-Alias") == 0 && at("Shadows") == 1, "graphics");
    require(
        at("FeatureShadows") == 0 && at("VehicleShadows") == 0 && at("Shading") == 0, "shadow bits"
    );
    require(at("DitheredFog") == 1 && at("SwitchAlt") == 1, "fog and alt");
    require(
        at("Sound Mode") == 6 && at("RestoreVolume") == 1 && at("MixingBuffers") == 8, "sound mode"
    );
    require(at("WaveOutVolume") == 0xffffffffU && at("CDAudioVolume") == 0x8000, "volumes");
    require(at("ackfx") == 0 && at("buildfx") == 1 && at("speechfx") == 1, "acknowledgements");
    require(at("clock") == 1 && at("musicmode") == 0 && at("cdmode") == 4, "music");
    require(at("gamespeed") == 14 && at("mousespeed") == 9 && at("textlines") == 7, "speeds");
    require(at("MultiMapping") == 1 && at("SkirmishCommanderDeath") == 2, "rule blocks");
    require(at("SkirmishLocation") == 0 && at("SkirmishDifficulty") == 1, "skirmish");
    require(at("PlayMovie") == 0, "play movie");
    require(
        host.written.count(SettingsHost::key(init::general_section, "Movie Output Rate")) == 0,
        "unchanged movie rate is not written"
    );
    require(
        host.written.count(SettingsHost::key(init::general_section, "NumSkirmishPlayers")) == 0,
        "slot count is not written"
    );
    require(
        host.strings.at(SettingsHost::key(init::general_section, "Password")) == "pw" &&
            host.strings.at(SettingsHost::key(init::general_section, "SkirmishMap")) ==
                "Lava Run" &&
            host.strings.at(SettingsHost::key(init::general_section, "Image Output Directory")) ==
                "shots",
        "strings"
    );
    require(p.image_output_directory_changed == 0, "image directory flag cleared");
    require(
        player("Player0Controller") == 1 && player("Player0Color") == 3 &&
            player("Player0AllyGroup") == 5 && player("Player0Energy") == 2000,
        "first slot"
    );
    require(player("Player1Metal") == 0xffffffffU && player("Player1Side") == 1, "second slot");
    const auto color = std::find(
        host.calls.begin(),
        host.calls.end(),
        "write:" + SettingsHost::key(init::skirmish_section, "Player1Color")
    );
    require(
        color != host.calls.end() &&
            *(color + 1) ==
                "write:" + SettingsHost::key(init::skirmish_section, "Player1AllyGroup"),
        "colour precedes ally group"
    );
    require(
        host.calls.back() == "write:" + SettingsHost::key(init::general_section, "PlayMovie"),
        "PlayMovie is last"
    );

    SettingsHost launched;
    launched.service = 1;
    p.sound_flags = 0;
    p.movie_output_rate_changed = 1;
    init::save_preferences(state, settings, p, launched);
    require(
        launched.strings.count(SettingsHost::key(init::general_section, "Password")) == 0,
        "a service launch keeps the password out"
    );
    require(
        launched.written.count(SettingsHost::key(init::general_section, "WaveOutVolume")) == 0,
        "volumes only with RestoreVolume"
    );
    require(
        launched.written.at(SettingsHost::key(init::general_section, "Movie Output Rate")) == 15 &&
            p.movie_output_rate_changed == 0,
        "changed movie rate is written once"
    );
}

void maps() {
    init::MapListState state{{7}};
    Maps h;
    init::select_map_list(state, 0, h);
    require(
        state.object.value == 7 && h.calls == std::vector<std::string>{"selector"}, "reuse selector"
    );
    h.calls.clear();
    init::select_map_list(state, 2, h);
    require(
        state.object.value == 9 &&
            h.calls ==
                std::vector<std::string>{"selector", "destroy", "release", "allocate", "construct"},
        "replacement lifecycle"
    );
    state.object = {};
    h.calls.clear();
    h.fail = true;
    init::select_map_list(state, 2, h);
    require(
        state.object.value == 0 && h.calls == std::vector<std::string>{"allocate"},
        "allocation failure"
    );
}

void restore_options_entry() {
    init::Preferences live;
    live.interface_type = 1;
    live.display_width = 800;
    live.display_height = 600;
    live.side = 3;
    live.difficulty = 2;
    live.scroll_speed = 7;
    live.screen_chat = 9;
    live.graphics_flags = 0x1111;
    live.sound_flags = 0x1234;
    live.music_flags = 0x55;
    live.gamma = 12;
    live.text_lines = 4;
    live.text_scroll = 6;
    live.mouse_speed = 8;
    live.game_speed = 1;
    live.current_game_speed = 2;
    live.unit_chat = 3;
    live.unit_chat_text = 4;
    live.fx_volume = 27;
    live.music_volume = 32;
    live.password = "keep";
    live.nickname = "nick";
    init::OptionsEntrySnapshot snapshot;
    snapshot.interface_type = 0x80000001U;
    snapshot.unit_chat = 0;
    snapshot.unit_chat_text = 255;
    snapshot.text_scroll = 0xffffffffU;
    snapshot.text_lines = 0;
    snapshot.game_speed = 0xabcd;
    snapshot.scroll_speed = 0xfe;
    init::restore_speed_options(live, snapshot);
    require(
        live.interface_type == std::bit_cast<int32_t>(snapshot.interface_type), "interface word"
    );
    require(live.unit_chat == 0 && live.unit_chat_text == 255, "chat bytes");
    require(live.text_scroll == 0xffffffffU && live.text_lines == 0, "text fields");
    require(live.game_speed == 0xabcd && live.current_game_speed == 0xabcd, "both speed words");
    require(live.scroll_speed == 0xfe, "scroll byte");
    require(
        live.display_width == 800 && live.display_height == 600 && live.side == 3 &&
            live.difficulty == 2 && live.screen_chat == 9,
        "untouched display"
    );
    require(
        live.graphics_flags == 0x1111 && live.sound_flags == 0x1234 && live.music_flags == 0x55 &&
            live.gamma == 12 && live.mouse_speed == 8,
        "untouched flags"
    );
    require(
        live.fx_volume == 27 && live.music_volume == 32 && live.password == "keep" &&
            live.nickname == "nick",
        "untouched volumes and strings"
    );
}

void restore_options_undo() {
    {
        init::Preferences live;
        live.sound_flags = 0xff7f;
        live.fx_volume = 1;
        live.unit_chat = 9;
        live.music_volume = 3;
        live.graphics_flags = 0x1111;
        init::OptionsEntrySnapshot snapshot;
        snapshot.fx_volume = 0x11223344U;
        snapshot.sound_flags = 0x15;
        snapshot.unit_chat = 0x5a;
        SettingsHost host;
        init::restore_sound_options(live, snapshot, host);
        require(live.fx_volume == 0x11223344U, "fx volume");
        require(live.sound_flags == 0xff1d, "sound inserts keep high byte");
        require(live.unit_chat == 0x5a, "unit chat byte");
        require(live.music_volume == 3 && live.graphics_flags == 0x1111, "sound restore neighbors");
        require(
            host.calls ==
                std::vector<std::string>{
                    "audio:" + std::to_string(static_cast<unsigned>(init::AudioMode::flat))
                },
            "mode other than 2 turns 3D sound off"
        );
    }
    {
        init::Preferences live;
        live.sound_flags = 0;
        init::OptionsEntrySnapshot snapshot;
        snapshot.sound_flags = 0x22;
        SettingsHost host;
        init::restore_sound_options(live, snapshot, host);
        require(live.sound_flags == 0x62, "build bit also sets speech");
        require(
            host.calls ==
                std::vector<std::string>{
                    "audio:" + std::to_string(static_cast<unsigned>(init::AudioMode::spatial))
                },
            "mode 2 turns 3D sound on"
        );
    }
    {
        init::Preferences live;
        live.music_flags = 0;
        live.music_volume = 1;
        live.cd_mode = 1;
        live.sound_flags = 0x1234;
        init::OptionsEntrySnapshot snapshot;
        snapshot.music_volume = 0x89abcdefU;
        snapshot.cd_mode = 0x07;
        snapshot.music_flags = 0xfe;
        init::restore_music_options(live, snapshot);
        require(
            live.music_volume == 0x89abcdefU && live.cd_mode == 0x07, "music volume and cd mode"
        );
        require(live.music_flags == 0, "only music-mode bit is inserted");
        require(live.sound_flags == 0x1234, "music restore leaves sound flags");
        snapshot.music_flags = 0x01;
        live.music_flags = 0x0100;
        init::restore_music_options(live, snapshot);
        require(live.music_flags == 0x0101, "music-mode bit preserves high byte");
    }
    {
        init::Preferences live;
        live.graphics_flags = 0;
        live.gamma = 1;
        live.display_width = 11;
        live.display_height = 22;
        live.sound_flags = 0x1234;
        init::OptionsEntrySnapshot snapshot;
        snapshot.graphics_flags = 0xff;
        snapshot.gamma = 0x01020304U;
        snapshot.display_width = 800;
        snapshot.display_height = 600;
        State state;
        state.session_flags = 0x0400;
        init::restore_visual_options(live, state, snapshot);
        require(live.graphics_flags == 0x007e, "graphics bits 1..6 only");
        require(live.gamma == 0x01020304U, "gamma word");
        require(
            live.display_width == 800 && live.display_height == 600,
            "session_flags bit 10 does not block size"
        );
        require(live.sound_flags == 0x1234, "graphics restore leaves sound flags");
        live.graphics_flags = 0xffff;
        snapshot.graphics_flags = 0;
        snapshot.gamma = 0;
        live.display_width = 11;
        live.display_height = 22;
        state.session_flags = 0x0104;
        init::restore_visual_options(live, state, snapshot);
        require(live.graphics_flags == 0xff81, "cleared graphics bits keep neighbors");
        require(live.gamma == 0, "gamma still copied when size is blocked");
        require(
            live.display_width == 11 && live.display_height == 22, "loading blocks display size"
        );
    }
    init::Preferences live;
    live.sound_flags = 0;
    live.music_flags = 0x0100;
    live.graphics_flags = 0xffff;
    live.display_flags = 0xabcd;
    live.campaign_unlock_flags = 0xbeef;
    live.display_width = 11;
    live.display_height = 22;
    live.gamma = 1;
    live.fx_volume = 1;
    live.music_volume = 1;
    live.cd_mode = 1;
    live.mouse_speed = 8;
    live.screen_chat = 9;
    live.password = "keep";
    live.nickname = "nick";
    live.text_scroll = 1;
    live.text_lines = 1;
    live.game_speed = 1;
    live.current_game_speed = 1;
    live.scroll_speed = 1;
    live.interface_type = 1;
    live.unit_chat = 1;
    live.unit_chat_text = 1;
    init::OptionsEntrySnapshot snapshot;
    snapshot.interface_type = 0x80000001U;
    snapshot.graphics_flags = 0;
    snapshot.gamma = 12;
    snapshot.fx_volume = 27;
    snapshot.music_volume = 32;
    snapshot.music_flags = 0x01;
    snapshot.cd_mode = 4;
    snapshot.unit_chat = 0x11;
    snapshot.unit_chat_text = 0x22;
    snapshot.sound_flags = 0x22;
    snapshot.display_width = 640;
    snapshot.display_height = 480;
    snapshot.text_scroll = 9;
    snapshot.text_lines = 8;
    snapshot.game_speed = 0x1234;
    snapshot.scroll_speed = 0x63;
    State state;
    state.session_flags = 0x0400;
    SettingsHost host;
    init::restore_all_options(live, state, snapshot, host);
    require(live.fx_volume == 27 && live.sound_flags == 0x62, "sound UNDO in cancel");
    require(live.unit_chat == 0x11, "unit chat from entry image");
    require(
        live.music_volume == 32 && live.cd_mode == 4 && live.music_flags == 0x0101,
        "music UNDO in cancel"
    );
    require(
        live.interface_type == std::bit_cast<int32_t>(0x80000001U) && live.text_scroll == 9 &&
            live.text_lines == 8 && live.game_speed == 0x1234 &&
            live.current_game_speed == 0x1234 && live.scroll_speed == 0x63 &&
            live.unit_chat_text == 0x22,
        "speed UNDO in cancel"
    );
    require(
        live.graphics_flags == 0xff81 && live.gamma == 12 && live.display_width == 640 &&
            live.display_height == 480,
        "visual UNDO in cancel"
    );
    require(
        live.display_flags == 0xabcd && live.campaign_unlock_flags == 0xbeef &&
            live.mouse_speed == 8 && live.screen_chat == 9 && live.password == "keep" &&
            live.nickname == "nick",
        "undo leaves unrelated fields"
    );
    require(
        host.calls ==
            std::vector<std::string>{
                "audio:" + std::to_string(static_cast<unsigned>(init::AudioMode::spatial))
            },
        "undo calls only the existing audio_mode host"
    );
    live.display_width = 11;
    live.display_height = 22;
    state.session_flags = 4;
    host.calls.clear();
    init::restore_all_options(live, state, snapshot, host);
    require(live.display_width == 11 && live.display_height == 22, "undo honors size gate");
}

void restore_speeds_defaults() {
    init::Preferences live;
    live.interface_type = -1;
    live.display_width = 800;
    live.display_height = 600;
    live.side = 3;
    live.difficulty = 2;
    live.scroll_speed = 7;
    live.screen_chat = 9;
    live.graphics_flags = 0x1111;
    live.sound_flags = 0x1234;
    live.music_flags = 0x55;
    live.display_flags = 0xabcd;
    live.campaign_unlock_flags = 0xbeef;
    live.gamma = 12;
    live.text_lines = 0xffffffffU;
    live.text_scroll = 0xffffffffU;
    live.mouse_speed = 8;
    live.game_speed = 0xffff;
    live.current_game_speed = 0xffff;
    live.unit_chat = 255;
    live.unit_chat_text = 255;
    live.cd_mode = 4;
    live.fx_volume = 27;
    live.music_volume = 32;
    live.password = "keep";
    live.nickname = "nick";
    init::reset_speed_options(live);
    require(live.interface_type == 0, "interface word cleared");
    require(live.text_scroll == 10 && live.text_lines == 10, "text defaults");
    require(live.game_speed == 10 && live.current_game_speed == 10, "both speed words");
    require(live.scroll_speed == 0x20, "scroll byte");
    require(live.unit_chat == 10 && live.unit_chat_text == 5, "chat defaults");
    require(
        live.display_width == 800 && live.display_height == 600 && live.side == 3 &&
            live.difficulty == 2 && live.screen_chat == 9,
        "untouched display"
    );
    require(
        live.graphics_flags == 0x1111 && live.sound_flags == 0x1234 && live.music_flags == 0x55 &&
            live.display_flags == 0xabcd && live.campaign_unlock_flags == 0xbeef &&
            live.gamma == 12 && live.mouse_speed == 8 && live.cd_mode == 4,
        "untouched flags"
    );
    require(
        live.fx_volume == 27 && live.music_volume == 32 && live.password == "keep" &&
            live.nickname == "nick",
        "untouched volumes and strings"
    );
}

void restore_graphics_defaults() {
    init::Preferences live;
    live.interface_type = -1;
    live.display_width = 800;
    live.display_height = 600;
    live.side = 3;
    live.difficulty = 2;
    live.scroll_speed = 7;
    live.screen_chat = 9;
    live.graphics_flags = 0x80c1;
    live.sound_flags = 0x1234;
    live.music_flags = 0x55;
    live.display_flags = 0xabcd;
    live.campaign_unlock_flags = 0xbeef;
    live.gamma = 0xffffffffU;
    live.text_lines = 4;
    live.text_scroll = 6;
    live.mouse_speed = 8;
    live.game_speed = 1;
    live.current_game_speed = 2;
    live.unit_chat = 3;
    live.unit_chat_text = 4;
    live.cd_mode = 4;
    live.fx_volume = 27;
    live.music_volume = 32;
    live.password = "keep";
    live.nickname = "nick";
    State state;
    state.session_flags = 0x0400;
    init::reset_visual_options(live, state);
    require(live.graphics_flags == 0x80bf, "ors set bits 1..5 and clear fog");
    require(live.gamma == 12, "gamma default");
    require(
        live.display_width == 640 && live.display_height == 480,
        "session_flags bit 10 does not block size"
    );
    require(
        live.interface_type == -1 && live.side == 3 && live.difficulty == 2 &&
            live.scroll_speed == 7 && live.screen_chat == 9,
        "untouched interface"
    );
    require(
        live.sound_flags == 0x1234 && live.music_flags == 0x55 && live.display_flags == 0xabcd &&
            live.campaign_unlock_flags == 0xbeef && live.mouse_speed == 8 && live.cd_mode == 4,
        "untouched neighbor flags"
    );
    require(
        live.text_lines == 4 && live.text_scroll == 6 && live.game_speed == 1 &&
            live.current_game_speed == 2 && live.unit_chat == 3 && live.unit_chat_text == 4,
        "untouched speeds"
    );
    require(
        live.fx_volume == 27 && live.music_volume == 32 && live.password == "keep" &&
            live.nickname == "nick",
        "untouched volumes and strings"
    );
    live.graphics_flags = 0x80c1;
    live.display_width = 800;
    live.display_height = 600;
    live.gamma = 1;
    state.session_flags = 0x0104;
    init::reset_visual_options(live, state);
    require(live.graphics_flags == 0x80ff, "loading keeps dithered fog");
    require(live.gamma == 12, "gamma still stored when size is blocked");
    require(live.display_width == 800 && live.display_height == 600, "loading blocks display size");
}

void capture_options_entry() {
    init::Preferences live;
    live.interface_type = std::bit_cast<int32_t>(0x80000001U);
    live.display_width = 800;
    live.display_height = 600;
    live.side = 3;
    live.difficulty = 2;
    live.scroll_speed = 0xfe;
    live.screen_chat = 9;
    live.graphics_flags = 0xabcd;
    live.sound_flags = 0x1234;
    live.music_flags = 0x0155;
    live.display_flags = 0xbeef;
    live.campaign_unlock_flags = 0x0111;
    live.gamma = 0x01020304U;
    live.text_lines = 8;
    live.text_scroll = 9;
    live.mouse_speed = 7;
    live.game_speed = 0x1234;
    live.current_game_speed = 0x5678;
    live.unit_chat = 0x11;
    live.unit_chat_text = 0x22;
    live.cd_mode = 0x07;
    live.fx_volume = 27;
    live.music_volume = 32;
    live.password = "keep";
    live.nickname = "nick";
    init::OptionsEntrySnapshot snapshot;
    snapshot.interface_type = 1;
    snapshot.graphics_flags = 1;
    snapshot.gamma = 1;
    snapshot.fx_volume = 1;
    snapshot.music_volume = 1;
    snapshot.music_flags = 1;
    snapshot.cd_mode = 1;
    snapshot.unit_chat = 1;
    snapshot.unit_chat_text = 1;
    snapshot.sound_flags = 1;
    snapshot.display_width = 1;
    snapshot.display_height = 1;
    snapshot.text_scroll = 1;
    snapshot.text_lines = 1;
    snapshot.game_speed = 1;
    snapshot.scroll_speed = 1;
    init::capture_options_entry(live, snapshot);
    require(snapshot.interface_type == 0x80000001U, "interface word");
    require(snapshot.graphics_flags == 0xcd, "graphics low byte");
    require(snapshot.gamma == 0x01020304U, "gamma word");
    require(snapshot.fx_volume == 27 && snapshot.music_volume == 32, "volumes");
    require(snapshot.music_flags == 0x55, "music-flag low byte");
    require(snapshot.cd_mode == 0x07, "cd mode");
    require(snapshot.unit_chat == 0x11 && snapshot.unit_chat_text == 0x22, "chat bytes");
    require(snapshot.sound_flags == 0x34, "sound-flag low byte");
    require(snapshot.display_width == 800 && snapshot.display_height == 600, "display size");
    require(snapshot.text_scroll == 9 && snapshot.text_lines == 8, "text fields");
    require(snapshot.game_speed == 0x1234, "speed word is the requested speed only");
    require(snapshot.scroll_speed == 0xfe, "scroll byte");
    require(
        live.interface_type == std::bit_cast<int32_t>(0x80000001U) &&
            live.current_game_speed == 0x5678 && live.side == 3 && live.difficulty == 2 &&
            live.screen_chat == 9 && live.mouse_speed == 7 && live.graphics_flags == 0xabcd &&
            live.sound_flags == 0x1234 && live.music_flags == 0x0155 &&
            live.display_flags == 0xbeef && live.campaign_unlock_flags == 0x0111 &&
            live.password == "keep" && live.nickname == "nick",
        "capture does not write preferences"
    );
    init::Preferences restored;
    restored.game_speed = 1;
    restored.current_game_speed = 1;
    restored.display_width = 11;
    init::restore_speed_options(restored, snapshot);
    require(restored.interface_type == live.interface_type, "undo reads captured interface");
    require(
        restored.game_speed == live.game_speed && restored.current_game_speed == live.game_speed,
        "undo writes the captured speed word to both speeds"
    );
    require(restored.scroll_speed == live.scroll_speed, "undo reads captured scroll");
    require(
        restored.unit_chat == live.unit_chat && restored.unit_chat_text == live.unit_chat_text &&
            restored.text_scroll == live.text_scroll && restored.text_lines == live.text_lines,
        "undo reads captured chat and text"
    );
    require(restored.display_width == 11, "entry undo still leaves display");
}

init::MenuControl named(std::string_view name, uint8_t value) {
    init::MenuControl control;
    std::copy_n(name.begin(), std::min(name.size(), control.name.size()), control.name.begin());
    control.track_type = value;
    return control;
}

uint8_t track_byte(const init::AudioTrackTypes& audio, int32_t key) {
    const auto it = audio.track_types.find(key);
    require(it != audio.track_types.end(), "track byte missing");
    return it->second;
}

void copy_track_type() {
    init::Preferences preferences;
    preferences.cd_mode = 3;
    preferences.music_volume = 99;
    preferences.music_flags = 0x55;
    init::TrackTypeMenu menu;
    menu.control_count = 2;
    menu.track_type_before_first = 0x5a;
    menu.controls.push_back(named("ROOT", 0));
    menu.controls.push_back(named("TRACKTYPE", 7));
    menu.controls.push_back(named("OTHER", 1));
    init::AudioTrackTypes audio;
    audio.track_types[3] = 0xff;
    audio.track_types[1] = 0x11;
    init::copy_track_type(preferences, menu, 3, audio);
    require(audio.track_types.size() == 2, "cd mode 3 adds no index");
    require(
        track_byte(audio, 3) == 0xff && track_byte(audio, 1) == 0x11, "cd mode 3 skips the store"
    );
    preferences.cd_mode = 5;
    init::copy_track_type(preferences, menu, 3, audio);
    require(track_byte(audio, 3) == 0xff, "cd mode 5 skips the store");
    preferences.cd_mode = 4;
    init::copy_track_type(preferences, menu, 3, audio);
    require(track_byte(audio, 3) == 7, "stores the control's button stage as the track type");
    require(track_byte(audio, 1) == 0x11, "other track index stays");
    require(
        preferences.music_volume == 99 && preferences.music_flags == 0x55 &&
            preferences.cd_mode == 4,
        "preferences are not written"
    );

    menu.controls[0] = named("TRACKTYPE", 9);
    menu.controls[1] = named("NOPE", 4);
    init::copy_track_type(preferences, menu, 0, audio);
    require(track_byte(audio, 0) == 0x5a, "control 0 is not scanned");

    menu.controls[0] = named("ROOT", 0);
    init::copy_track_type(preferences, menu, -5, audio);
    require(track_byte(audio, -5) == 0x5a, "missing name stores base-0x24");

    menu.controls[1] = named("OTHER", 1);
    menu.controls[2] = named("TRACKTYPE", 8);
    menu.control_count = 1;
    init::copy_track_type(preferences, menu, 4, audio);
    require(track_byte(audio, 4) == 0x5a, "count short hides a later control");
    menu.control_count = 2;
    init::copy_track_type(preferences, menu, 4, audio);
    require(track_byte(audio, 4) == 8, "count short includes index 2");

    menu.controls[1] = named("TRACKTYPE", 2);
    menu.controls[2] = named("TRACKTYPE", 9);
    init::copy_track_type(preferences, menu, 6, audio);
    require(track_byte(audio, 6) == 2, "first scanned name wins");

    menu.controls[1] = named("tracktype", 3);
    menu.controls[2] = named("TRACKTYP", 4);
    init::copy_track_type(preferences, menu, 6, audio);
    require(track_byte(audio, 6) == 0x5a, "case and prefix do not match");

    menu.controls[1].name.fill('X');
    std::copy_n(std::string_view("TRACKTYPE").begin(), 9, menu.controls[1].name.begin());
    menu.controls[1].track_type = 6;
    menu.controls[2] = named("OTHER", 1);
    init::copy_track_type(preferences, menu, 6, audio);
    require(track_byte(audio, 6) == 0x5a, "the byte after TRACKTYPE is compared");

    menu.controls[1] = named("TRACKTYPE", 6);
    menu.controls[1].name[10] = 'Z';
    init::copy_track_type(preferences, menu, 6, audio);
    require(track_byte(audio, 6) == 6, "the name comparison stops at the NUL");

    menu.control_count = -1;
    init::copy_track_type(preferences, menu, 8, audio);
    require(track_byte(audio, 8) == 0x5a, "negative count scans nothing");

    preferences.cd_mode = 4;
    menu.control_count = 1;
    menu.controls[1] = named("TRACKTYPE", 0);
    audio.track_types[2] = 0xff;
    init::copy_track_type(preferences, menu, 2, audio);
    require(track_byte(audio, 2) == 0, "a zero selection is stored");
}
} // namespace

int main() {
    try {
        player_slots();
        preferences();
        save_preferences();
        maps();
        restore_options_entry();
        restore_options_undo();
        restore_speeds_defaults();
        restore_graphics_defaults();
        capture_options_entry();
        preference_writers();
        copy_track_type();
        std::cout << "initialization passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
