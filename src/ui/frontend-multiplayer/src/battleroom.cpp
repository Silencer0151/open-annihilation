// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Battleroom (LOUNGE2.GUI): slot rules, rows, options, start checks.
#include "oa/ui/frontend_multiplayer/lobby.hpp"

#include "oa/base/game_loop.hpp"
#include "oa/netgame/player_slots.hpp"
#include "oa/netgame/records.hpp"
#include "oa/ui/frontend_multiplayer/dialogs.hpp"
#include "oa/base/text.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace oa::ui::frontend_multiplayer {

namespace {

// Where the accessors below find their Game and Player fields, which the
// packed records may leave unaligned.
constexpr std::size_t kChatRingOffset = offsetof(Game, chat_lines);
constexpr std::size_t kSessionPlayersOffset = offsetof(Game, session_player_limit);
constexpr std::size_t kPlayerCountOffset = offsetof(Game, player_count);
constexpr std::size_t kChatHeadOffset = offsetof(Game, chat_head);
constexpr std::size_t kChatTailOffset = offsetof(Game, chat_tail);
constexpr std::size_t kGameNameOffset = offsetof(Game, game_name);
constexpr std::size_t kNicknameOffset = offsetof(Game, nickname);
constexpr std::size_t kPasswordOffset = offsetof(Game, password);
constexpr std::size_t kLobbyOptionOffset = offsetof(Game, setup_options);
constexpr std::size_t kMaxUnitsOffset = offsetof(Game, max_units);
constexpr std::size_t kMaxUnitsDefaultOffset = offsetof(Game, max_units_setting);
constexpr std::size_t kScreenWidthOffset = offsetof(Game, screen_width);
constexpr std::size_t kScreenHeightOffset = offsetof(Game, screen_height);
constexpr std::size_t kProviderGuidOffset = offsetof(Game, provider_guid);
// Game.start_options, in order: commander rule, unmapped, LOS limited, LOS true.
constexpr std::size_t kCommanderOptionOffset = offsetof(Game, start_options);
constexpr std::size_t kUnmappedOptionOffset = kCommanderOptionOffset + sizeof(int32_t);
constexpr std::size_t kLosLimitedOptionOffset = kUnmappedOptionOffset + sizeof(int32_t);
constexpr std::size_t kLosTrueOptionOffset = kLosLimitedOptionOffset + sizeof(int32_t);
constexpr std::size_t kSideCountOffset = offsetof(Game, side_count);
constexpr std::size_t kPlayerPingOffset = offsetof(Player, latency);
constexpr std::size_t kPlayerJoinTickOffset = offsetof(Player, join_tick);
constexpr std::size_t kPlayerAlliedBackOffset = offsetof(Player, allied_by);
constexpr std::size_t kPlayerTeamOffset = offsetof(Player, team);

constexpr std::size_t kSessionRulesOffset = offsetof(Game, session_rules);

constexpr int32_t kMaxUnitsFloor = 20; // MAXUNITS knob value + 20
// While a launch is active the version byte in the session description is
// biased by this, as status::launch_only marks.
constexpr int32_t kLaunchVersionBias = 100;
// Game.setup_options bit a launch's lock sets: the host's options
// stay locked.
constexpr uint16_t kSetupOptionsLocked = 0x0001;
// Game.gui_flags bit of a game a lobby program launched.
constexpr uint8_t kGuiFlagLobbyLaunch = 0x10;
// The PREVMENU caption, and the longest provider label that takes its place.
constexpr const char* kPreviousMenuCaption = "Previous Menu";
constexpr std::size_t kProviderCaptionLimit = 11;
// LaunchBlock values the battle room applies.
constexpr int32_t kLaunchDeathFirst = 1; // continues; 2 ends, 3 deathmatch
constexpr int32_t kLaunchDeathLast = 3;
constexpr int32_t kLaunchLosTrue = 1;
constexpr int32_t kLaunchLosCircular = 2;
constexpr int32_t kLaunchLosPermanent = 3;
constexpr int32_t kLaunchCheatsAllowed = 2;
constexpr int32_t kLaunchLocationFixed = 1;
constexpr int32_t kLaunchMappingUnmapped = 1;
constexpr int32_t kLaunchWatchingAllowed = 2;
constexpr int32_t kLaunchTeamFirst = 1; // teams 1..5 are Player.team 0..4
constexpr int32_t kLaunchTeamLast = 5;
constexpr int32_t kResourceSliderMaximum = 0x2711;
constexpr int32_t kDefaultResource = 1000;
constexpr uint32_t kComputerRejectTicks = 0x1f;
constexpr uint32_t kStatsInterval = 0x3c;
constexpr int16_t kStartFrames = 9; // battlestart sequence length
constexpr int16_t kStartFrameStop = 8;
constexpr int16_t kStartFrameSound = 4;
constexpr uint32_t kStartFrameTicks = 4;
// While everyone is ready START's art is lit at the tick's low five bits,
// cycling through light levels 0..31.
constexpr uint32_t kStartLightMask = 0x1f;
constexpr uint8_t kRejectPlayer = 1;
constexpr uint8_t kRejectLeaving = 2;
constexpr uint8_t kRejectWatching = 9;
constexpr uint8_t kRejectCreatorLeft = 10;
constexpr uint8_t kRejectDeathmatch = 0xb;
constexpr uint32_t kTicksPerSecond = 30;
constexpr uint32_t kMillisecondsPerSecond = 1000;
constexpr std::size_t kComputerNameBytes = 0x10; // session name of a computer player
constexpr uint16_t kSeatedMaxUnits = 100;        // PlayerSetupInfo.max_units as a player is seated
// Player.machine_flags: the player joined without the battle room; cleared
// when a computer player is seated.
constexpr uint8_t kMachineFlagJoinedWithoutBattleroom = 0x02;
constexpr uint32_t kBroadcastId = 0;
constexpr uint32_t kNoPlayerId = 0xffffffffU;
constexpr uint32_t kHostMachineGroup = 1;
constexpr uint32_t kSessionFlagGame = 0x4;     // published on every session
constexpr uint32_t kSessionFlagStarted = 0x20; // the game started; stays set once published
constexpr uint32_t kMachineGroupCount = 10;    // groups 1..10
constexpr uint16_t kRunFlagPaused = 0x01;      // Game.sim_run_flags: the game is paused

// The ten row templates cloned per slot, in the game's table order.
constexpr const char* kRowKeys[] = {
    "PLAYERx", "READYx", "LOGOx", "SIDEx", "PINGx", "MEMx", "RESx", "ALLYx", "CDx", "TEAMICONSx"
};

enum RowKey {
    row_player,
    row_ready,
    row_logo,
    row_side,
    row_ping,
    row_mem,
    row_res,
    row_ally,
    row_cd,
    row_team
};

// Host-only option buttons grayed for clients.
constexpr const char* kHostOptionButtons[] = {
    "COMMANDER", "MAPPING", "LOSTYPE", "WATCHING", "CHEATING", "FIXEDLOC", "GAMEOPEN"
};

uint8_t* game_bytes(Game& game, std::size_t offset) noexcept {
    return reinterpret_cast<uint8_t*>(&game) + offset;
}

uint8_t* player_bytes(Player& player, std::size_t offset) noexcept {
    return reinterpret_cast<uint8_t*>(&player) + offset;
}

PlayerSetupInfo& info_of(Lobby& lobby, const Player& player) noexcept {
    auto* info = player_info(lobby, player);
    return info != nullptr ? *info : lobby.infos[0];
}

void play(Lobby& lobby, const char* name) noexcept {
    if (lobby.services.play_sound != nullptr)
        lobby.services.play_sound(lobby.services.context, name);
}

void message(Lobby& lobby, const char* text) noexcept {
    if (lobby.services.message != nullptr)
        lobby.services.message(lobby.services.context, text);
}

void notify(Lobby& lobby, int32_t event) noexcept {
    if (lobby.services.notify != nullptr)
        lobby.services.notify(lobby.services.context, event);
}

uint32_t now(Lobby& lobby) noexcept {
    return lobby.services.tick != nullptr ? lobby.services.tick(lobby.services.context) : 0;
}

bool disc_present(Lobby& lobby) noexcept {
    return lobby.services.disc_present != nullptr &&
           lobby.services.disc_present(lobby.services.context);
}

bool map_selected(Lobby& lobby) noexcept {
    return lobby.maps.selected != nullptr && lobby.maps.selected(lobby.maps.context);
}

const char* map_name(Lobby& lobby) noexcept {
    const char* name = lobby.maps.name != nullptr ? lobby.maps.name(lobby.maps.context) : nullptr;
    return name != nullptr ? name : "";
}

int32_t map_memory(Lobby& lobby) noexcept {
    return lobby.maps.memory_mb != nullptr ? lobby.maps.memory_mb(lobby.maps.context) : 0;
}

/// Queues a lobby record from a player of this machine, reaching each machine once.
///
/// While machines are shared a broadcast goes to one player of every
/// machine instead, and to Lobby::refused_joiner when one is set.
///
/// @param lobby Lobby state holding the network table.
/// @param from Id of the local or computer player the record is from.
/// @param to Destination player id; 0 broadcasts.
/// @param data Record bytes.
/// @param size Record length in bytes.
/// @param unguaranteed Send it at once without guaranteed delivery, as a ping is.
void send(
    Lobby& lobby,
    uint32_t from,
    uint32_t to,
    const uint8_t* data,
    std::size_t size,
    bool unguaranteed = false
) noexcept {
    const auto one = [&](uint32_t target) {
        if (lobby.net.send_from != nullptr)
            lobby.net.send_from(lobby.net.context, from, target, data, size, unguaranteed);
        else if (lobby.net.send != nullptr)
            lobby.net.send(lobby.net.context, target, data, size);
    };
    if (to != kBroadcastId || lobby.game->shared_machines == 0) {
        one(to);
        return;
    }
    uint32_t ids[kSlotCount];
    const auto count = machine_broadcast_targets(*lobby.game, ids);
    for (int32_t i = 0; i < count; ++i)
        one(ids[i]);
    // A refused joiner holds no slot, so no machine's target names it.
    if (lobby.refused_joiner != 0)
        one(lobby.refused_joiner);
}

/// Sends every queued record now.
///
/// @param lobby Lobby state holding the network table.
void flush(Lobby& lobby) noexcept {
    if (lobby.net.flush != nullptr)
        lobby.net.flush(lobby.net.context);
}

/// Returns this machine's millisecond clock, or the 30 Hz clock in milliseconds without one.
///
/// @param lobby Lobby state holding the services.
/// @return Milliseconds.
uint32_t milliseconds(Lobby& lobby) noexcept {
    if (lobby.services.milliseconds != nullptr)
        return lobby.services.milliseconds(lobby.services.context);
    return now(lobby) * kMillisecondsPerSecond / kTicksPerSecond;
}

bool occupied_by(const Player& player, uint8_t a, uint8_t b = 0xff) noexcept {
    return player.in_use != 0 && (player.status == a || player.status == b);
}

bool local_or_computer(const Player& player) noexcept {
    return occupied_by(player, kSlotLocal, kSlotComputer);
}

/// Returns the id the records that concern no one player leave from: the
/// first seated local or computer player in slot order.
///
/// @param lobby Lobby state.
/// @return The player id; the local player's when none is seated.
uint32_t primary_id(Lobby& lobby) noexcept {
    for (int32_t slot = 0; slot < kSlotCount; ++slot) {
        const auto& player = slot_player(lobby, slot);
        if (local_or_computer(player))
            return player.player_id;
    }
    return local_player(lobby).player_id;
}

std::string_view text_view(const char* text, std::size_t bound) noexcept {
    return {text, ::strnlen(text, bound)};
}

/// Copies text into a fixed field, cut to fit and terminated.
///
/// @param[out] out Field receiving the text.
/// @param capacity Size of the field, terminator included; at least 1.
/// @param text Text to copy.
void copy_text(char* out, std::size_t capacity, std::string_view text) noexcept {
    const auto length = std::min(text.size(), capacity - 1);
    std::memmove(out, text.data(), length);
    out[length] = '\0';
}

/// Tells whether text begins with a prefix, compared without case.
///
/// @param text Text to test.
/// @param prefix Prefix to find.
/// @return True when text starts with prefix ignoring case.
bool starts_nocase(std::string_view text, std::string_view prefix) noexcept {
    if (text.size() < prefix.size())
        return false;
    for (std::size_t i = 0; i < prefix.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(text[i])) !=
            std::tolower(static_cast<unsigned char>(prefix[i])))
            return false;
    return true;
}

void format(char (&out)[64], const char* pattern, int32_t value) noexcept {
    std::snprintf(out, sizeof(out), pattern, static_cast<int>(value));
}

bool host_is_local(Lobby& lobby) noexcept {
    return (local_info(lobby).role & kRoleHost) != 0;
}

/// Counts the occupied remote slots.
///
/// @param lobby Lobby state.
/// @return The count.
int32_t remote_count(Lobby& lobby) noexcept {
    int32_t count = 0;
    for (int32_t slot = 0; slot < kSlotCount; ++slot)
        if (occupied_by(slot_player(lobby, slot), kSlotRemote))
            ++count;
    return count;
}

/// Counts the occupied local slots and the remote slots whose info reports them playing.
///
/// @param lobby Lobby state.
/// @return The count.
int32_t playing_count(Lobby& lobby) noexcept {
    int32_t count = 0;
    for (int32_t slot = 0; slot < kSlotCount; ++slot) {
        auto& player = slot_player(lobby, slot);
        if (occupied_by(player, kSlotLocal) || slot_remote_playing(lobby, player))
            ++count;
    }
    return count;
}

/// Counts the occupied computer slots and the remote slots whose info reports them defeated.
///
/// @param lobby Lobby state.
/// @return The count.
int32_t idle_participant_count(Lobby& lobby) noexcept {
    int32_t count = 0;
    for (int32_t slot = 0; slot < kSlotCount; ++slot) {
        auto& player = slot_player(lobby, slot);
        if (occupied_by(player, kSlotComputer) || slot_remote_defeated(lobby, player))
            ++count;
    }
    return count;
}

/// Counts the occupied computer slots.
///
/// @param lobby Lobby state.
/// @return The count.
int32_t computer_count(Lobby& lobby) noexcept {
    int32_t count = 0;
    for (int32_t slot = 0; slot < kSlotCount; ++slot)
        if (occupied_by(slot_player(lobby, slot), kSlotComputer))
            ++count;
    return count;
}

void set_group(Panel& panel, const char* pattern, int32_t slot, int32_t value) {
    char name[64];
    format(name, pattern, slot);
    panel_set_value(panel, name, value);
}

Control* row_control(Panel& panel, const char* pattern, int32_t slot) noexcept {
    char name[64];
    format(name, pattern, slot);
    return panel_control(panel, name);
}

/// Sets or clears one bit of an option word.
///
/// @param[in,out] options option word
/// @param bit the bit
/// @param on whether it is set
void set_option_bit(uint16_t& options, uint16_t bit, bool on) noexcept {
    options = static_cast<uint16_t>(on ? options | bit : options & ~bit);
}

/// Applies an active launch's options to the local player as the battle room opens.
///
/// A unit limit becomes the game's and the player's; the lock sets or
/// clears option word bit 0; a commander rule 1 to 3 becomes the game's
/// rule and the player's commander option; a faction the player's side; a
/// team 1 to 5 the player's team, which is sent with the alliances
/// refreshed; line of sight, cheats, fixed positions, mapping and watching
/// their option bits. Energy and metal become the sliders' first values. A
/// field that is 0 leaves its setting alone.
///
/// @param[in,out] lobby Lobby state; its launch must be active.
/// @param[in,out] me the local player
/// @param[in,out] info the local player's lobby block
/// @param[in,out] energy the energy slider's first value
/// @param[in,out] metal the metal slider's first value
void apply_launch_options(
    Lobby& lobby, Player& me, PlayerSetupInfo& info, int32_t& energy, int32_t& metal
) noexcept {
    namespace nl = oa::ui::frontend_multiplayer::launch;
    const nl::LaunchBlock& block = *lobby.launch_link.block;
    auto& game = *lobby.game;
    if (block.max_units != 0) {
        info.max_units = static_cast<uint16_t>(block.max_units);
        lobby_max_units(game) = static_cast<uint16_t>(block.max_units);
    }
    uint16_t word = lobby_option_word(game);
    set_option_bit(word, kSetupOptionsLocked, block.lock_options != 0);
    lobby_option_word(game) = word;
    if (block.death_option >= kLaunchDeathFirst && block.death_option <= kLaunchDeathLast) {
        const auto rule = static_cast<uint32_t>(block.death_option - kLaunchDeathFirst);
        std::memcpy(game_bytes(game, kCommanderOptionOffset), &rule, sizeof(rule));
        std::memcpy(game_bytes(game, kSessionRulesOffset), &rule, sizeof(rule));
        info.options = static_cast<uint16_t>(
            (info.options & ~option::commander_mask) | ((rule << 11) & option::commander_mask)
        );
    }
    if (block.energy != 0)
        energy = block.energy;
    if (block.faction != nl::faction::none)
        info.side = block.faction != nl::faction::arm ? 1 : 0;
    if (block.team >= kLaunchTeamFirst && block.team <= kLaunchTeamLast) {
        lobby_player_team(me) = static_cast<uint8_t>(block.team - kLaunchTeamFirst);
        lobby_send_team(lobby, me);
        lobby_update_ally_matrix(lobby);
    }
    if (block.metal != 0)
        metal = block.metal;
    uint16_t options = info.options;
    if (block.los == kLaunchLosTrue) {
        options |= option::los_limited | option::los_true;
    } else if (block.los == kLaunchLosCircular) {
        options = static_cast<uint16_t>((options & ~option::los_true) | option::los_limited);
    } else if (block.los == kLaunchLosPermanent) {
        options = static_cast<uint16_t>(options & ~option::los_limited);
    }
    if (block.cheats != 0)
        set_option_bit(options, option::cheats_allowed, block.cheats == kLaunchCheatsAllowed);
    if (block.location != 0)
        set_option_bit(options, option::fixed_locations, block.location == kLaunchLocationFixed);
    if (block.mapping != 0)
        set_option_bit(options, option::unmapped, block.mapping == kLaunchMappingUnmapped);
    if (block.watching != 0)
        set_option_bit(options, option::watching_allowed, block.watching == kLaunchWatchingAllowed);
    info.options = options;
}

void set_host_options(Game& game, uint16_t options) noexcept {
    const auto store = [&](std::size_t offset, uint32_t value) {
        std::memcpy(game_bytes(game, offset), &value, sizeof(value));
    };
    store(kLosLimitedOptionOffset, (options & option::los_limited) >> 9);
    store(kLosTrueOptionOffset, (options & option::los_true) >> 10);
    store(kCommanderOptionOffset, (options & option::commander_mask) >> 11);
    store(kUnmappedOptionOffset, (options & option::unmapped) >> 8);
}

} // namespace

// ---------------------------------------------------------------------------
// Accessors

FieldRef<uint16_t> lobby_player_count(Game& game) noexcept {
    return {game_bytes(game, kPlayerCountOffset)};
}

FieldRef<uint16_t> lobby_chat_head(Game& game) noexcept {
    return {game_bytes(game, kChatHeadOffset)};
}

FieldRef<uint16_t> lobby_chat_tail(Game& game) noexcept {
    return {game_bytes(game, kChatTailOffset)};
}

char* lobby_chat_line(Game& game, std::size_t index) noexcept {
    return reinterpret_cast<char*>(
        game_bytes(game, kChatRingOffset + (index % kChatLines) * kChatLineBytes)
    );
}

char* lobby_game_name(Game& game) noexcept {
    return reinterpret_cast<char*>(game_bytes(game, kGameNameOffset));
}

char* lobby_nickname(Game& game) noexcept {
    return reinterpret_cast<char*>(game_bytes(game, kNicknameOffset));
}

char* lobby_password(Game& game) noexcept {
    return reinterpret_cast<char*>(game_bytes(game, kPasswordOffset));
}

FieldRef<uint16_t> lobby_option_word(Game& game) noexcept {
    return {game_bytes(game, kLobbyOptionOffset)};
}

FieldRef<uint16_t> lobby_max_units(Game& game) noexcept {
    return {game_bytes(game, kMaxUnitsOffset)};
}

FieldRef<uint16_t> lobby_max_units_default(Game& game) noexcept {
    return {game_bytes(game, kMaxUnitsDefaultOffset)};
}

FieldRef<int32_t> lobby_screen_width(Game& game) noexcept {
    return {game_bytes(game, kScreenWidthOffset)};
}

FieldRef<int32_t> lobby_screen_height(Game& game) noexcept {
    return {game_bytes(game, kScreenHeightOffset)};
}

uint8_t* lobby_provider_guid(Game& game) noexcept {
    return game_bytes(game, kProviderGuidOffset);
}

FieldRef<int32_t> lobby_session_players(Game& game) noexcept {
    return {game_bytes(game, kSessionPlayersOffset)};
}

FieldRef<int32_t> lobby_player_ping(Player& player) noexcept {
    return {player_bytes(player, kPlayerPingOffset)};
}

FieldRef<uint32_t> lobby_player_join_tick(Player& player) noexcept {
    return {player_bytes(player, kPlayerJoinTickOffset)};
}

uint8_t& lobby_player_team(Player& player) noexcept {
    return *player_bytes(player, kPlayerTeamOffset);
}

uint8_t* lobby_player_allied_back(Player& player) noexcept {
    return player_bytes(player, kPlayerAlliedBackOffset);
}

uint8_t lobby_side_count(const Game& game) noexcept {
    uint32_t count = 0;
    std::memcpy(&count, reinterpret_cast<const uint8_t*>(&game) + kSideCountOffset, sizeof(count));
    return static_cast<uint8_t>(std::min<uint32_t>(count, 0xff));
}

StartedOptions lobby_started_options(Game& game) noexcept {
    const auto load = [&](std::size_t offset) {
        uint32_t value = 0;
        std::memcpy(&value, game_bytes(game, offset), sizeof(value));
        return value;
    };
    return {
        load(kCommanderOptionOffset),
        load(kUnmappedOptionOffset),
        load(kLosLimitedOptionOffset),
        load(kLosTrueOptionOffset),
        0
    };
}

PlayerSetupInfo* player_info(Lobby& lobby, const Player& player) noexcept {
    if (player.info == 0 || player.info > static_cast<uint32_t>(kSlotCount))
        return nullptr;
    return &lobby.infos[player.info - 1];
}

PlayerSetupInfo* slot_info(Lobby& lobby, int32_t slot) noexcept {
    if (slot < 0 || slot >= kSlotCount)
        return nullptr;
    return player_info(lobby, lobby.game->players[slot]);
}

Player& slot_player(Lobby& lobby, int32_t slot) noexcept {
    return lobby.game->players[std::clamp(slot, 0, kSlotCount - 1)];
}

Player& local_player(Lobby& lobby) noexcept {
    return slot_player(lobby, lobby.game->local_player_index);
}

PlayerSetupInfo& local_info(Lobby& lobby) noexcept {
    return info_of(lobby, local_player(lobby));
}

void lobby_reset(Lobby& lobby, Game& game) noexcept {
    lobby.game = &game;
    std::memset(static_cast<void*>(lobby.infos), 0, sizeof(lobby.infos));
    for (int32_t slot = 0; slot < kSlotCount; ++slot) {
        auto& player = game.players[slot];
        std::memset(&player, 0, sizeof(player));
        player.info = static_cast<oa_ref32>(slot + 1);
        player.index = kNoSlot;
        lobby_player_team(player) = kNoTeam;
        lobby.infos[slot].color = kNoColor;
    }
    game.local_player_index = 0;
    lobby_chat_head(game) = 0;
    lobby_chat_tail(game) = 0;
    lobby_player_count(game) = 1;
    lobby.rows_built = false;
    lobby.start_frame = 0;
    lobby.last_player_count = 0xffffffffU;
    lobby.confirm_slot = kNoSlot;
    lobby.timeout_player = 0;
    lobby.stalled_player = 0;
    lobby.timeout_baseline = 0;
    if (game.player_timeout_seconds <= 0)
        game.player_timeout_seconds = kDefaultPlayerTimeoutSeconds;
    lobby.row_base = 0;
    lobby.refused_joiner = 0;
}

void lobby_seat_local(Lobby& lobby, int32_t slot, bool host, const char* nickname) noexcept {
    auto& game = *lobby.game;
    game.local_player_index = static_cast<uint8_t>(slot);
    auto& player = slot_player(lobby, slot);
    player.in_use = 1;
    player.index = static_cast<uint8_t>(slot);
    copy_text(player.name, sizeof(player.name), nickname != nullptr ? nickname : "");
    copy_text(
        player.second_name, sizeof(player.second_name), text_view(player.name, sizeof(player.name))
    );
    slot_set_status(lobby, player, kSlotLocal);
    auto& info = info_of(lobby, player);
    info.role = host ? kRoleHost : 0;
    info.version_major = static_cast<uint8_t>(lobby.local_version_major);
    info.version_minor = static_cast<uint8_t>(lobby.local_version_minor);
    info.net_id = player.player_id;
    if (host && info.color == kNoColor)
        info.color = static_cast<uint8_t>(lobby_next_free_color(lobby));
}

// ---------------------------------------------------------------------------
// Slot predicates

bool slot_active(const Player& player) noexcept {
    return player.in_use != 0 &&
           (player.status == kSlotLocal || player.status == kSlotComputer ||
            player.status == kSlotRemote) &&
           player.index != kNoSlot;
}

bool slot_remote_playing(Lobby& lobby, const Player& player) noexcept {
    return occupied_by(player, kSlotRemote) && info_of(lobby, player).state == kInfoStatePlaying;
}

bool slot_remote_defeated(Lobby& lobby, const Player& player) noexcept {
    return occupied_by(player, kSlotRemote) && info_of(lobby, player).state == kInfoStateDefeated;
}

bool slot_participating(const Player& player) noexcept {
    return player.in_use != 0 && slot_active(player) &&
           (player.unit_count != 0 || player.units_created == 0) && player.index != kNoSlot;
}

uint8_t lobby_host_slot(Lobby& lobby) noexcept {
    for (uint8_t slot = 0; slot < kSlotCount; ++slot) {
        auto& player = slot_player(lobby, slot);
        if (player.status != kSlotOpen && (info_of(lobby, player).role & kRoleHost) != 0)
            return slot;
    }
    return kNoSlot;
}

void slot_set_status(Lobby& lobby, Player& player, uint8_t status) noexcept {
    player.status = status;
    if (status != kSlotRemote)
        info_of(lobby, player).state = status;
}

bool lobby_launch_locked(Lobby& lobby) noexcept {
    return lobby_options_locked(lobby) && lobby.option_4;
}

bool lobby_options_locked(Lobby& lobby) noexcept {
    return (lobby_option_word(*lobby.game) & kSetupOptionsLocked) != 0;
}

bool lobby_launch_active(const Lobby& lobby) noexcept {
    const auto& link = lobby.launch_link;
    return link.launch_active != nullptr && link.launch_active(link.context);
}

bool lobby_launch_block_active(const Lobby& lobby) noexcept {
    return lobby.launch_link.block != nullptr && lobby_launch_active(lobby);
}

bool info_version_compatible(const PlayerSetupInfo& info, uint32_t local_major) noexcept {
    int32_t version = info.version_major;
    if ((info.status & status::launch_only) != 0)
        version -= 100;
    return static_cast<int32_t>(static_cast<int8_t>(local_major)) <= version;
}

char* local_password_field(Lobby& lobby) noexcept {
    return local_info(lobby).password;
}

bool lobby_has_host_map(Lobby& lobby) noexcept {
    if (!map_selected(lobby))
        return false;
    const auto host = lobby_host_slot(lobby);
    if (host != kNoSlot) {
        const auto& info = info_of(lobby, slot_player(lobby, host));
        if (info.version_major > 1 || (info.version_major == 1 && info.version_minor > 1)) {
            const auto hash = lobby.maps.content_hash != nullptr
                                  ? lobby.maps.content_hash(lobby.maps.context)
                                  : 0U;
            return hash == info.map_hash;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// Colours, teams and alliances

int32_t lobby_next_free_color(Lobby& lobby) noexcept {
    int32_t used[kColorCount] = {};
    for (int32_t slot = 0; slot < kSlotCount; ++slot) {
        auto& player = slot_player(lobby, slot);
        if (!slot_active(player))
            continue;
        const auto color = info_of(lobby, player).color;
        used[color < 9 ? color : 9] = 1;
    }
    for (int32_t color = 0; color < kColorCount; ++color)
        if (used[color] == 0)
            return color;
    return 0;
}

int32_t team_member_count(Lobby& lobby, uint32_t team) noexcept {
    if (team == kNoTeam)
        return 0;
    const bool watching_started = (lobby.game->session_flags & kNetFlagGameStarted) != 0;
    int32_t count = 0;
    for (int32_t slot = 0; slot < kSlotCount; ++slot) {
        auto& player = slot_player(lobby, slot);
        if (lobby_player_team(player) != team || !slot_active(player))
            continue;
        if (watching_started && !slot_participating(player))
            continue;
        ++count;
    }
    return count;
}

bool lobby_all_on_one_team(Lobby& lobby) noexcept {
    const auto total = idle_participant_count(lobby) + playing_count(lobby);
    for (uint32_t team = 0; team < kTeamCount; ++team)
        if (team_member_count(lobby, team) == total)
            return true;
    return false;
}

int32_t next_team_mate(Lobby& lobby, int32_t slot, int32_t start) noexcept {
    if (start >= kSlotCount || start < 0)
        return -1;
    const auto team = lobby_player_team(slot_player(lobby, slot));
    for (int32_t other = start; other < kSlotCount; ++other) {
        auto& player = slot_player(lobby, other);
        if (lobby_player_team(player) == team && player.status != kSlotOpen &&
            lobby_player_team(player) != kNoTeam)
            return other;
        if (other == slot)
            return other;
    }
    return -1;
}

void lobby_update_team_icons(Lobby& lobby, Panel& panel) noexcept {
    const bool watching_started = (lobby.game->session_flags & kNetFlagGameStarted) != 0;
    int32_t shown = 0;
    for (int32_t slot = 0; slot < kSlotCount; ++slot) {
        auto& player = slot_player(lobby, slot);
        if (!slot_active(player) || player.status == kSlotBlocked)
            continue;
        if (watching_started &&
            (!slot_participating(player) || info_of(lobby, player).color == kNoColor))
            continue;
        char name[64];
        format(name, "TEAMICONS%d", watching_started ? shown++ : slot);
        const auto team = lobby_player_team(player);
        const auto members = team_member_count(lobby, team);
        int32_t stage = 10;
        if (members != 0)
            stage = team * 2 + (members == 1 ? 1 : 0);
        panel_set_stage(panel, name, stage);
    }
    lobby.game->gui_flags |= 1U;
}

void lobby_update_ally_matrix(Lobby& lobby) noexcept {
    for (int32_t slot = 0; slot < kSlotCount; ++slot) {
        auto& player = slot_player(lobby, slot);
        if (!slot_active(player) || player.status == kSlotBlocked)
            continue;
        int32_t mate = 0;
        while ((mate = next_team_mate(lobby, slot, mate)) != -1) {
            lobby_player_allied_back(player)[mate] = 1;
            player.alliance[mate] = 1;
            info_of(lobby, slot_player(lobby, mate)).status |= status::allied_victory;
            info_of(lobby, player).status |= status::allied_victory;
            ++mate;
        }
        if (team_member_count(lobby, lobby_player_team(player)) < 2)
            info_of(lobby, player).status &= static_cast<uint16_t>(~status::allied_victory);
    }
}

void lobby_leave_team(Lobby& lobby, int32_t slot) noexcept {
    auto& player = slot_player(lobby, slot);
    const auto team = lobby_player_team(player);
    if (team == kNoTeam)
        return;
    for (int32_t other = 0; other < kSlotCount; ++other) {
        auto& mate = slot_player(lobby, other);
        if (!slot_active(mate) || mate.status == kSlotBlocked || lobby_player_team(mate) != team ||
            other == player.index)
            continue;
        lobby_set_alliance(lobby, player, mate, 0, true);
        info_of(lobby, player).status &= static_cast<uint16_t>(~status::allied_victory);
    }
}

void lobby_cycle_team(Lobby& lobby, Panel& panel, int32_t slot) noexcept {
    auto& player = slot_player(lobby, slot);
    const auto team = lobby_player_team(player);
    lobby_leave_team(lobby, slot);
    lobby_player_team(player) = static_cast<uint8_t>((team + 1U) % 6U);
    lobby_send_team(lobby, player);
    lobby_update_ally_matrix(lobby);
    lobby_update_team_icons(lobby, panel);
}

// ---------------------------------------------------------------------------
// Slot table maintenance

void lobby_swap_slots(Lobby& lobby, int32_t first, int32_t second) noexcept {
    auto& a = slot_player(lobby, first);
    auto& b = slot_player(lobby, second);
    const Player held = b;
    b = a;
    a = held;
    slot_set_status(lobby, a, kSlotOpen);
    a.in_use = 0;
    // The game renumbers eleven records; the eleventh lies past the table.
    for (int32_t slot = 0; slot < kSlotCount; ++slot) {
        auto& player = slot_player(lobby, slot);
        player.index = slot_active(player) ? static_cast<uint8_t>(slot) : kNoSlot;
    }
}

void lobby_compact_slots(Lobby& lobby) noexcept {
    const auto active_at = [&](int32_t slot) {
        return slot < kSlotCount && slot_active(slot_player(lobby, slot));
    };
    int32_t low = 0;
    int32_t high = 0;
    for (;;) {
        if (high >= kSlotCount && low >= kSlotCount)
            return;
        while ((active_at(low) ||
                (low < kSlotCount && slot_player(lobby, low).status == kSlotBlocked)) &&
               low < kSlotCount)
            ++low;
        high = low;
        for (;;) {
            ++high;
            if (active_at(high))
                break;
            if (high >= kSlotCount)
                return;
        }
        if (high >= kSlotCount || low >= kSlotCount)
            return;
        lobby_swap_slots(lobby, high, low);
    }
}

// ---------------------------------------------------------------------------
// Battleroom panel

void control_make_label(Control& control) noexcept {
    if (control.type != ControlType::button)
        return;
    control.x = static_cast<int16_t>(control.x + 2);
    control.type = ControlType::label;
    control.attributes |= kAttributeLabelText;
}

void lobby_build_rows(Lobby& lobby, Panel& panel) noexcept {
    const auto local = lobby.game->local_player_index;
    if (lobby.row_base > 0)
        panel.count = lobby.row_base;
    for (int32_t slot = 0; slot < kSlotCount; ++slot) {
        auto& player = slot_player(lobby, slot);
        for (int32_t key = 0; key < static_cast<int32_t>(std::size(kRowKeys)); ++key) {
            const auto source = panel_find(panel, kRowKeys[key]);
            if (source == kNoControl)
                continue;
            const auto index = panel_clone(
                panel, source, static_cast<char>('0' + slot), static_cast<int16_t>(slot * kRowPitch)
            );
            if (index == kNoControl)
                return;
            auto& row = panel.controls[static_cast<std::size_t>(index)];
            row.active = 1;
            if (row.type == ControlType::label)
                continue;
            switch (key) {
            case row_player:
                if (slot == local) {
                    control_make_label(row);
                    row.attributes = 1;
                } else {
                    row.attributes |= kAttributeRemoteRow;
                }
                break;
            case row_ready:
                if (slot != local) {
                    row.grayed = true;
                    row.value = 0;
                }
                break;
            case row_logo:
                if (!local_or_computer(player))
                    row.active = 0;
                break;
            case row_side:
                row.grayed = !local_or_computer(player);
                row.active = 0;
                break;
            case row_res:
                if (slot != local)
                    control_make_label(row);
                if (occupied_by(player, kSlotComputer))
                    row.active = 0;
                break;
            case row_ally:
                if (slot == local)
                    row.active = 0;
                break;
            case row_team:
                row.active = 0;
                row.stage = 10;
                break;
            default:
                control_make_label(row);
                break;
            }
        }
    }
    if (auto* own = row_control(panel, "PLAYER%d", local))
        control_make_label(*own);
    lobby.rows_built = true;
}

void lobby_update_option_buttons(Lobby& lobby, Panel& panel) noexcept {
    auto slot = lobby_host_slot(lobby);
    if (slot == kNoSlot)
        slot = lobby.game->local_player_index;
    const auto options = info_of(lobby, slot_player(lobby, slot)).options;
    panel_set_stage(panel, "COMMANDER", (options & option::commander_mask) >> 11);
    panel_set_stage(panel, "MAPPING", ~(options >> 8) & 1);
    const int32_t los = (options & option::los_limited) == 0 ? 2 : (~options >> 10) & 1;
    panel_set_stage(panel, "LOSTYPE", los);
    panel_set_stage(panel, "WATCHING", (options & 0xff) >> 7);
    panel_set_stage(panel, "CHEATING", (options & option::cheats_allowed) >> 13);
    panel_set_stage(panel, "FIXEDLOC", (options & option::fixed_locations) >> 14);
    panel_set_stage(panel, "GAMEOPEN", (options & option::game_closed) == 0 ? 1 : 0);
}

void lobby_update_side_button(Lobby& lobby, Panel& panel, int32_t slot) noexcept {
    auto& player = slot_player(lobby, slot);
    const auto& info = info_of(lobby, player);
    char name[64];
    format(name, "SIDE%d", slot);
    const bool watcher = player.in_use != 0 && (info.options & option::watcher) != 0;
    panel_set_stage(panel, name, watcher ? 2 : info.side);
}

void lobby_on_max_units(Panel& panel, void* user) noexcept {
    auto& lobby = *static_cast<Lobby*>(user);
    auto* slider = panel_control(panel, "MAXUNITS");
    if (slider == nullptr)
        return;
    const auto host = lobby_host_slot(lobby);
    int32_t value = 0;
    if (host == lobby.game->local_player_index || host == kNoSlot)
        value = oa::ui::gui_input::scroll_value(slider->scroll) + kMaxUnitsFloor;
    else
        value = info_of(lobby, slot_player(lobby, host)).max_units;
    char text[64];
    format(text, "%d", value);
    panel_set_text(panel, "MAXUNITSTEXT", text);
    auto& info = local_info(lobby);
    info.max_units = static_cast<uint16_t>(value);
    if ((info.role & kRoleHost) != 0)
        lobby_send_player_info(lobby);
}

void lobby_on_metal(Panel& panel, void* user) noexcept {
    auto& lobby = *static_cast<Lobby*>(user);
    auto* slider = panel_control(panel, "METAL");
    if (slider == nullptr)
        return;
    const auto value = oa::ui::gui_input::scroll_value(slider->scroll) / 100 * 100;
    char text[64];
    format(text, "%d", value);
    panel_set_text(panel, "METALTEXT", text);
    auto& info = local_info(lobby);
    info.metal_hundreds = static_cast<uint16_t>(value / 100);
    if ((info.role & kRoleHost) != 0) {
        lobby_send_player_info(lobby);
        lobby_publish_session(lobby);
    }
}

void lobby_on_energy(Panel& panel, void* user) noexcept {
    auto& lobby = *static_cast<Lobby*>(user);
    auto* slider = panel_control(panel, "ENERGY");
    if (slider == nullptr)
        return;
    const auto value = oa::ui::gui_input::scroll_value(slider->scroll) / 100 * 100;
    char text[64];
    format(text, "%d", value);
    panel_set_text(panel, "ENERGYTEXT", text);
    auto& info = local_info(lobby);
    info.energy_hundreds = static_cast<uint16_t>(value / 100);
    if ((info.role & kRoleHost) != 0) {
        lobby_send_player_info(lobby);
        lobby_publish_session(lobby);
    }
}

void lobby_bind_slider(
    Lobby& lobby,
    Panel& panel,
    const char* name,
    int32_t maximum,
    int32_t value,
    SliderHandler handler
) noexcept {
    if (auto* slider = panel_control(panel, name)) {
        slider->scroll.maximum = maximum;
        slider->on_change = handler;
        oa::ui::gui_input::scroll_set_value(slider->scroll, value);
    }
    if (handler != nullptr)
        handler(panel, &lobby);
    panel.dirty = true;
}

void lobby_cycle_resolution(Lobby& lobby, bool backwards) noexcept {
    if (lobby.services.display_modes == nullptr)
        return;
    DisplayMode modes[100];
    const auto count = lobby.services.display_modes(lobby.services.context, modes, 100);
    if (count <= 0)
        return;
    auto& info = local_info(lobby);
    for (int32_t index = 0; index < count; ++index) {
        if (modes[index].width != info.screen_width || modes[index].height != info.screen_height)
            continue;
        int32_t next = backwards ? index - 1 : index + 1;
        if (next < 0)
            next = count - 1;
        if (next >= count)
            next = 0;
        info.screen_width = static_cast<uint16_t>(modes[next].width);
        info.screen_height = static_cast<uint16_t>(modes[next].height);
        lobby_send_player_info(lobby);
        lobby_screen_width(*lobby.game) = modes[next].width;
        lobby_screen_height(*lobby.game) = modes[next].height;
        return;
    }
}

void lobby_update_status(Lobby& lobby, Panel& panel) noexcept {
    auto& game = *lobby.game;
    const auto local = game.local_player_index;
    auto& me = local_player(lobby);
    const int32_t my_ready = (info_of(lobby, me).options & option::ready) >> 5;
    uint32_t lowest_latency = 0xffffffffU;

    if (auto* output = panel_control(panel, "OUTPUT")) {
        uint32_t head = lobby_chat_head(game);
        const uint32_t tail = lobby_chat_tail(game);
        if (head < tail)
            head += kChatLines;
        const int32_t rows = output->height / (12 + 2);
        if (rows < static_cast<int32_t>(head - tail)) {
            auto next = static_cast<uint16_t>(lobby_chat_tail(game) + 1);
            if (next > kChatLines - 1)
                next = 0;
            lobby_chat_tail(game) = next;
        }
        std::vector<std::string> lines;
        for (uint32_t at = lobby_chat_tail(game); at != lobby_chat_head(game);
             at = (at + 1) % kChatLines)
            lines.emplace_back(text_view(lobby_chat_line(game, at), kChatLineBytes));
        panel_set_items(panel, "OUTPUT", std::move(lines));
    }
    lobby_update_option_buttons(lobby, panel);

    if (auto* map_label = panel_control(panel, "MAPNAME")) {
        if (!map_selected(lobby)) {
            map_label->color = kWarningColor;
            panel_set_text(panel, "MAPNAME", "NOT SELECTED");
        } else {
            const auto* context = lobby.maps.map_context != nullptr
                                      ? lobby.maps.map_context(lobby.maps.context)
                                      : nullptr;
            const char* name = context != nullptr ? data::campaign::campaign_localized_name(context)
                                                  : map_name(lobby);
            const bool changed = control_text(*map_label) != std::string_view(name);
            if (!lobby_has_host_map(lobby)) {
                map_label->color = ((now(lobby) / 30) & 1U) != 0 ? kWarningColor : 0;
                if (changed) {
                    lobby_say(lobby, me, "does not have this map");
                    info_of(lobby, me).options &= static_cast<uint16_t>(~option::ready);
                    set_group(panel, "READY%d", local, 0);
                    lobby_send_player_info(lobby);
                }
                if (!host_is_local(lobby))
                    panel_set_grayed(panel, "MAP", true);
            } else {
                map_label->color = 0;
                panel_set_grayed(panel, "MAP", false);
            }
            set_control_text(*map_label, name);
        }
    }

    for (int32_t slot = 0; slot < kSlotCount; ++slot) {
        auto& player = slot_player(lobby, slot);
        if (local_or_computer(player)) {
            auto& info = info_of(lobby, player);
            info.options = static_cast<uint16_t>(
                (info.options & ~option::ready) | (info_of(lobby, me).options & option::ready)
            );
        }
    }
    for (int32_t slot = 0; slot < kSlotCount; ++slot) {
        auto& player = slot_player(lobby, slot);
        if ((info_of(lobby, me).options & option::commander_mask) == option::commander_deathmatch &&
            (occupied_by(player, kSlotComputer) || slot_remote_defeated(lobby, player))) {
            lobby_reject(lobby, player.player_id, kRejectDeathmatch);
            lobby_send_player_info(lobby);
        }
        const auto host = lobby_host_slot(lobby);
        if (host != kNoSlot &&
            (info_of(lobby, slot_player(lobby, host)).options & option::watching_allowed) == 0 &&
            player.in_use != 0) {
            auto& info = info_of(lobby, player);
            if ((info.options & option::watcher) != 0) {
                info.options &= static_cast<uint16_t>(~option::watcher);
                info.side = 0;
                lobby_send_player_info(lobby);
            }
        }
    }
    lobby_update_ally_matrix(lobby);
    lobby_update_team_icons(lobby, panel);

    for (int32_t slot = 0; slot < kSlotCount; ++slot) {
        auto& player = slot_player(lobby, slot);
        auto& info = info_of(lobby, player);
        char name[64];
        const bool watcher = player.in_use != 0 && (info.options & option::watcher) != 0;
        if (!slot_active(player) && !watcher) {
            format(name, "CD%d", slot);
            panel_set_active(panel, name, false);
            panel_set_grayed(panel, name, false);
            format(name, "PLAYER%d", slot);
            panel_set_text(panel, name, player.status == kSlotBlocked ? "[BLOCKED]" : "UNUSED");
            panel_set_grayed(panel, name, my_ready != 0);
            for (const char* pattern :
                 {"LOGO%d", "SIDE%d", "TEAMICONS%d", "RES%d", "PING%d", "MEM%d"})
                if (auto* control = row_control(panel, pattern, slot))
                    control->active = 0;
            if (slot != local)
                if (auto* ally = row_control(panel, "ALLY%d", slot))
                    ally->active = 0;
            if (auto* ready = row_control(panel, "READY%d", slot)) {
                ready->value = 0;
                ready->active = 0;
                ready->grayed = true;
            }
            continue;
        }
        const bool present = occupied_by(player, kSlotLocal) || slot_remote_playing(lobby, player);
        format(name, "CD%d", slot);
        panel_set_active(panel, name, present && (info.status & status::has_disc) != 0);
        panel_set_grayed(panel, name, false);
        if (auto* logo = row_control(panel, "LOGO%d", slot)) {
            logo->active = (info.color == kNoColor && my_ready == 0) ? 0 : 1;
            logo->stage = info.color;
            // A colour is not changed while the local player is ready.
            logo->hot = my_ready == 0;
        }
        format(name, "PLAYER%d", slot);
        panel_set_text(panel, name, text_view(player.name, 0x1e));
        panel_set_grayed(panel, name, my_ready != 0);
        if (auto* side = row_control(panel, "SIDE%d", slot)) {
            lobby_update_side_button(lobby, panel, slot);
            side->active = 1;
            side->grayed = !local_or_computer(player) || my_ready != 0;
        }
        if (auto* ally = row_control(panel, "ALLY%d", slot)) {
            ally->stage =
                static_cast<uint8_t>(lobby_player_allied_back(me)[slot] << 1 | me.alliance[slot]);
            const bool live = player.in_use != 0 && (player.status == kSlotLocal || watcher ||
                                                     player.status == kSlotComputer);
            const bool me_watching =
                me.in_use != 0 && (info_of(lobby, me).options & option::watcher) != 0;
            ally->active = (!live && !slot_remote_defeated(lobby, player) && !me_watching) ? 1 : 0;
            ally->grayed = my_ready != 0;
        }
        if (auto* team = row_control(panel, "TEAMICONS%d", slot)) {
            team->active = watcher ? 0 : 1;
            team->grayed = !local_or_computer(player) || my_ready != 0;
        }
        if (auto* res = row_control(panel, "RES%d", slot)) {
            char text[64] = "";
            if (present)
                std::snprintf(text, sizeof(text), "%dx%d", info.screen_width, info.screen_height);
            set_control_text(*res, text);
            if (occupied_by(player, kSlotLocal))
                res->grayed = my_ready != 0;
            else
                res->active = 1;
        }
        if (auto* ping = row_control(panel, "PING%d", slot)) {
            if (!slot_remote_playing(lobby, player)) {
                set_control_text(*ping, "n/a");
            } else {
                const auto value = static_cast<int32_t>(lobby_player_ping(player));
                char text[64];
                format(text, "%d", value);
                if (host_is_local(lobby) &&
                    unit_sync_peer_complete(
                        lobby, netgame::player_slot_id(*lobby.game, static_cast<uint8_t>(slot))
                    ))
                    oa::base::text::append_terminated(text, ":s");
                set_control_text(*ping, text);
                if (static_cast<uint32_t>(value) <= lowest_latency)
                    lowest_latency = static_cast<uint32_t>(value);
            }
            ping->active = 1;
        }
        if (auto* mem = row_control(panel, "MEM%d", slot)) {
            char text[64] = "";
            if (present)
                format(text, "%d", info.memory_mb);
            set_control_text(*mem, text);
            mem->color = info.memory_mb < map_memory(lobby) ? kWarningColor : 0;
            mem->active = 1;
        }
        if (auto* ready = row_control(panel, "READY%d", slot)) {
            ready->active = 1;
            ready->value = static_cast<int16_t>((info.options >> 5) & 1);
            ready->grayed = !occupied_by(player, kSlotLocal);
        }
    }
    panel.dirty = true;
    auto& mine = info_of(lobby, me);
    if ((mine.role & kRoleHost) != 0 && lowest_latency < mine.lowest_latency) {
        mine.lowest_latency = static_cast<uint16_t>(lowest_latency);
        lobby_publish_session(lobby);
    }
}

bool lobby_ready_to_start(Lobby& lobby) noexcept {
    if (remote_count(lobby) == 0)
        return false;
    bool everyone_watching = true;
    for (int32_t slot = 0; slot < kSlotCount; ++slot) {
        auto& player = slot_player(lobby, slot);
        if (player.in_use == 0) {
            everyone_watching = false;
            continue;
        }
        const auto options = info_of(lobby, player).options;
        if ((player.status == kSlotLocal || player.status == kSlotComputer ||
             player.status == kSlotRemote) &&
            (options & option::ready) == 0)
            return false;
        if ((options & option::watcher) == 0)
            everyone_watching = false;
    }
    if (lobby_player_count(*lobby.game) != 1)
        return !everyone_watching;
    return false;
}

void lobby_enter_battleroom(Lobby& lobby, Panel& panel) noexcept {
    auto& game = *lobby.game;
    lobby.last_player_count = 0xffffffffU;
    lobby.rows_built = false;
    lobby.start_frame = 0;
    game.gui_flags |= 1U;
    auto& me = local_player(lobby);
    auto& info = info_of(lobby, me);
    const bool host = (info.role & kRoleHost) != 0;
    if (host)
        lobby_max_units(game) = static_cast<uint16_t>(lobby_max_units_default(game));
    info.screen_width = static_cast<uint16_t>(static_cast<int32_t>(lobby_screen_width(game)));
    info.screen_height = static_cast<uint16_t>(static_cast<int32_t>(lobby_screen_height(game)));
    info.status = static_cast<uint16_t>(
        (info.status & ~status::has_disc) | (disc_present(lobby) ? status::has_disc : 0)
    );
    if (auto* entry = panel_control(panel, "MESSAGE"))
        entry->value = 0x7f;
    int32_t energy = kDefaultResource;
    int32_t metal = kDefaultResource;
    const bool launch_applied = lobby_launch_block_active(lobby);
    if (launch_applied) {
        apply_launch_options(lobby, me, info, energy, metal);
    } else if (host) {
        const auto saved = lobby_started_options(game);
        auto options = static_cast<uint16_t>(info.options & 0xa0ff);
        options |= static_cast<uint16_t>((saved.unmapped & 1U) << 8);
        options |= static_cast<uint16_t>((saved.los_limited & 1U) << 9);
        options |= static_cast<uint16_t>((saved.los_true & 1U) << 10);
        options |= static_cast<uint16_t>((saved.fixed_locations & 1U) << 14);
        options |= static_cast<uint16_t>((saved.commander & 3U) << 11);
        info.options = options;
    }
    const bool host_seat = host && local_or_computer(me);
    if (!host_seat || lobby_launch_locked(lobby)) {
        if (auto* map = panel_control(panel, "MAP")) {
            map->attributes = 2;
            set_control_text(*map, "View Map");
        }
    }
    if (!host || lobby_options_locked(lobby))
        for (const char* name : kHostOptionButtons)
            panel_set_grayed(panel, name, true);
    lobby_update_option_buttons(lobby, panel);
    if (auto* mem = panel_control(panel, "MEMx"))
        mem->color = info.memory_mb < map_memory(lobby) ? kWarningColor : 0;
    unit_sync_create(lobby, host);
    const bool synced = unit_sync_complete(lobby);
    panel_set_active(panel, "START", synced);
    panel_set_grayed(panel, "START", !(host && lobby_ready_to_start(lobby) && synced));
    panel_set_grayed(panel, "RESTRICTIONS", false);
    panel_set_text(panel, "METALTEXT", "0");
    panel_set_text(panel, "ENERGYTEXT", "0");
    // A game a lobby program started, or one with a launch active, shows the
    // launch block's provider label on PREVMENU when it is short enough.
    if ((game.gui_flags & kGuiFlagLobbyLaunch) != 0 || lobby_launch_active(lobby)) {
        const char* label =
            lobby.launch_link.block != nullptr ? lobby.launch_link.block->provider_label : "";
        const bool short_label =
            label[0] != '\0' && ::strnlen(label, sizeof lobby.launch_link.block->provider_label) <=
                                    kProviderCaptionLimit;
        panel_set_text(panel, "PREVMENU", short_label ? label : kPreviousMenuCaption);
    }
    lobby_bind_slider(lobby, panel, "METAL", kResourceSliderMaximum, metal, lobby_on_metal);
    const auto units = static_cast<int32_t>(lobby_max_units(game)) - kMaxUnitsFloor;
    lobby_bind_slider(lobby, panel, "MAXUNITS", units, units, lobby_on_max_units);
    if (!host || lobby_options_locked(lobby))
        for (const char* name : {"MAXUNITS", "ENERGY", "METAL", "CHEATING"})
            panel_set_grayed(panel, name, true);
    if (lobby_launch_locked(lobby))
        panel_set_grayed(panel, "RESTRICTIONS", true);
    if (lobby_launch_active(lobby))
        panel_set_grayed(panel, "COMMANDER", true);
    lobby_bind_slider(lobby, panel, "ENERGY", kResourceSliderMaximum, energy, lobby_on_energy);
    // The host plays the map the launch names.
    if (host && launch_applied && lobby.launch_link.block->mission[0] != '\0' &&
        lobby.maps.select != nullptr)
        (void)lobby.maps.select(lobby.maps.context, lobby.launch_link.block->mission);
    if (!map_selected(lobby))
        message(lobby, "Could not find the multiplayer map!!");
    std::snprintf(info.map_name, sizeof(info.map_name), "%s", map_name(lobby));
    info.map_hash =
        lobby.maps.content_hash != nullptr ? lobby.maps.content_hash(lobby.maps.context) : 0;
    lobby_send_player_info(lobby);
    panel.focus = panel_find(panel, "MESSAGE");
    for (const char* key : kRowKeys)
        panel_set_active(panel, key, false);
    lobby.row_base = panel.count;
    lobby_build_rows(lobby, panel);
    lobby_update_team_icons(lobby, panel);
    lobby_update_status(lobby, panel);
    // The closed doors over START take its clicks until the host's lobby is
    // first ready to start.
    if (auto* start = panel_control(panel, "battlestart")) {
        start->stage = 0;
        start->active = 1;
        start->hot = true;
    }
}

namespace {

/// Tells whether a typed line is text, compared without case.
///
/// @param text Line as typed.
/// @param word Text to compare with.
/// @return True when they match ignoring case.
bool same_nocase(std::string_view text, std::string_view word) noexcept {
    return text.size() == word.size() && starts_nocase(text, word);
}

/// Handles a click on the battle room panel; lobby_handle_event flushes what it queued.
///
/// @param[in,out] lobby Lobby state.
/// @param[in,out] panel The battle room panel.
/// @return What the screen should do next.
LobbyAction handle_panel_event(Lobby& lobby, Panel& panel) noexcept {
    auto& game = *lobby.game;
    if (panel.selected == kNoControl)
        return LobbyAction::none;
    const auto local = game.local_player_index;
    auto& me = local_player(lobby);
    const bool hosting = [&] {
        const auto host = lobby_host_slot(lobby);
        return host != kNoSlot && local_or_computer(slot_player(lobby, host));
    }();
    auto action = LobbyAction::none;
    char name[64];

    for (int32_t slot = 0; slot < kSlotCount; ++slot) {
        auto& player = slot_player(lobby, slot);
        auto& info = info_of(lobby, player);

        format(name, "LOGO%d", slot);
        if (panel_selected_is(panel, name) && local_or_computer(player)) {
            play(lobby, "Multi");
            lobby_request_color(lobby, info.color + 1U);
            game.gui_flags |= 1U;
            lobby_send_player_info(lobby);
        }

        format(name, "PLAYER%d", slot);
        if (panel_selected_is(panel, name) && slot != local) {
            play(lobby, "Multi");
            const auto current = player.status;
            bool publish = true;
            if (current == kSlotOpen && hosting) {
                slot_set_status(lobby, player, kSlotBlocked);
                player.player_id = 0xffffffffU;
                lobby_session_players(game) = lobby_session_players(game) - 1;
            } else if (current != kSlotOpen && current != kSlotBlocked) {
                if (player.in_use != 0 && current == kSlotComputer &&
                    base::game_loop::scaled_clock_elapsed(
                        now(lobby), lobby_player_join_tick(player)
                    ) >= kComputerRejectTicks) {
                    lobby_reject(lobby, player.player_id, kRejectPlayer);
                    slot_set_status(lobby, player, kSlotOpen);
                    player.in_use = 0;
                } else if (hosting && player.in_use != 0 && current == kSlotRemote) {
                    lobby.confirm_slot = static_cast<uint8_t>(slot);
                    action = LobbyAction::confirm_reject;
                }
            } else {
                if (current == kSlotBlocked) {
                    slot_set_status(lobby, player, kSlotOpen);
                    lobby_session_players(game) = lobby_session_players(game) + 1;
                    lobby_publish_session(lobby);
                }
                const auto host = lobby_host_slot(lobby);
                const auto host_options =
                    host == kNoSlot ? 0 : info_of(lobby, slot_player(lobby, host)).options;
                if ((host_options & option::game_closed) != 0) {
                    message(lobby, "Can't add another player when game is closed.");
                    slot_set_status(lobby, player, kSlotOpen);
                    game.gui_flags |= 1U;
                    publish = false;
                } else if (
                    (host_options & option::commander_mask) != option::commander_deathmatch &&
                    computer_count(lobby) == 0 && !lobby.option_4
                ) {
                    // The game colours the slot and notifies whether or
                    // not the session took the player.
                    lobby_add_computer(lobby, slot);
                    info.color = static_cast<uint8_t>(lobby_next_free_color(lobby));
                    notify(lobby, 2);
                }
            }
            if (!publish)
                break;
            game.gui_flags |= 1U;
            lobby_publish_session(lobby);
            lobby_send_player_info(lobby);
        }

        format(name, "SIDE%d", slot);
        if (panel_selected_is(panel, name)) {
            if (!lobby_launch_locked(lobby)) {
                play(lobby, "Multi");
                if (player.in_use != 0 && (info.options & option::watcher) != 0) {
                    info.options &= static_cast<uint16_t>(~option::watcher);
                    info.side = 0;
                } else {
                    ++info.side;
                    if (info.side >= lobby_side_count(game)) {
                        info.side = 0;
                        const auto host = lobby_host_slot(lobby);
                        const auto host_options =
                            host == kNoSlot ? 0 : info_of(lobby, slot_player(lobby, host)).options;
                        if ((host_options & option::watching_allowed) == 0 || player.in_use == 0 ||
                            player.status != kSlotLocal)
                            panel_set_stage(panel, name, 0);
                        else
                            info.options |= option::watcher;
                    }
                }
                game.gui_flags |= 1U;
                notify(lobby, 4);
                lobby_send_player_info(lobby);
            } else {
                play(lobby, "notoktobuild");
            }
        }

        format(name, "ALLY%d", slot);
        if (panel_selected_is(panel, name)) {
            if (!lobby_launch_locked(lobby)) {
                const auto value = static_cast<uint8_t>(me.alliance[slot] ^ 1U);
                lobby_set_alliance(lobby, me, player, value, false);
                me.alliance[slot] = value;
                if (lobby_player_team(me) != kNoTeam &&
                    lobby_player_team(me) == lobby_player_team(player)) {
                    lobby_leave_team(lobby, local);
                    lobby_player_team(me) = kNoTeam;
                    lobby_send_team(lobby, me);
                }
                const bool quiet = value == 0 && lobby_player_allied_back(me)[slot] * 2U != 3U;
                play(lobby, quiet ? "Multi" : "Ally");
                char line[96];
                std::snprintf(
                    line,
                    sizeof(line),
                    " %s %s",
                    value == 0 ? "broke alliance with" : "allied with",
                    std::string(text_view(player.name, sizeof(player.name))).c_str()
                );
                lobby_say(lobby, me, line);
                game.gui_flags |= 1U;
                lobby_send_player_info(lobby);
            } else {
                play(lobby, "notoktobuild");
            }
        }

        format(name, "TEAMICONS%d", slot);
        if (panel_selected_is(panel, name)) {
            if (!lobby_launch_locked(lobby)) {
                play(lobby, "Ally");
                lobby_cycle_team(lobby, panel, slot);
                lobby_send_team(lobby, player);
            } else {
                play(lobby, "notoktobuild");
            }
        }

        format(name, "RES%d", slot);
        if (panel_selected_is(panel, name) && occupied_by(player, kSlotLocal)) {
            play(lobby, "Multi");
            lobby_cycle_resolution(lobby, panel.button == 2);
            panel.selected = kNoControl;
            game.gui_flags |= 1U;
            return action;
        }

        format(name, "READY%d", slot);
        if (panel_selected_is(panel, name) && occupied_by(player, kSlotLocal)) {
            play(lobby, "Multi");
            if (!lobby_has_host_map(lobby)) {
                panel_set_value(panel, name, 0);
            } else {
                const auto* ready = panel_control(panel, name);
                const bool on = ready != nullptr && (ready->value & 1) != 0;
                info.options = static_cast<uint16_t>(
                    (info.options & ~option::ready) | (on ? option::ready : 0)
                );
                for (int32_t other = 0; other < kSlotCount; ++other) {
                    auto& mate = slot_player(lobby, other);
                    if (!local_or_computer(mate))
                        continue;
                    auto& mate_info = info_of(lobby, mate);
                    mate_info.options = static_cast<uint16_t>(
                        (mate_info.options & ~option::ready) |
                        (info_of(lobby, me).options & option::ready)
                    );
                }
                game.gui_flags |= 1U;
                lobby_send_player_info(lobby);
            }
        }
    }
    if (lobby_player_count(game) != kSlotCount)
        game.gui_flags |= 1U;

    auto& mine = info_of(lobby, me);
    const auto finish = [&](bool publish) {
        if (publish) {
            lobby_send_player_info(lobby);
            lobby_publish_session(lobby);
        }
        game.gui_flags |= 1U;
        panel.selected = kNoControl;
    };

    if (panel_selected_is(panel, "PREVMENU")) {
        play(lobby, "Previous");
        for (int32_t slot = 0; slot < kSlotCount; ++slot) {
            auto& player = slot_player(lobby, slot);
            if (local_or_computer(player))
                lobby_reject(lobby, player.player_id, kRejectLeaving);
        }
        game.frontend_pending_signal = 3;
        return LobbyAction::leave;
    }
    if (panel_selected_is(panel, "MESSAGE")) {
        auto* entry = panel_control(panel, "MESSAGE");
        const auto text = std::string(control_text(*entry));
        if (!text.empty()) {
            // The battle room's commands are matched without case; paging
            // sends nothing here.
            const bool syncerr = same_nocase(text, "+syncerr");
            const bool command = syncerr || same_nocase(text, "+page") || same_nocase(text, "+p") ||
                                 starts_nocase(text, "+page ") || starts_nocase(text, "+p ");
            if (syncerr) {
                char diagnostic[kChatLineBytes];
                if (const char* line = unit_sync_diagnostic(lobby, diagnostic, sizeof diagnostic))
                    lobby_post_chat(lobby, line);
            } else if (!command)
                lobby_say(lobby, me, text.c_str());
            game.gui_flags |= 1U;
            set_control_text(*entry, "");
        }
        panel.focus = panel_find(panel, "MESSAGE");
        panel.selected = kNoControl;
        return action;
    }
    if (panel_selected_is(panel, "COMMANDER")) {
        play(lobby, "Multi");
        auto options = mine.options;
        const auto stepped = ((options & 0xf800U) + option::commander_step) ^ options;
        options = static_cast<uint16_t>((stepped & option::commander_mask) ^ options);
        if ((options & option::commander_mask) > option::commander_deathmatch)
            options &= static_cast<uint16_t>(~option::commander_mask);
        mine.options = options;
        finish(true);
        return action;
    }
    if (panel_selected_is(panel, "LOSTYPE")) {
        play(lobby, "Multi");
        if ((mine.options & option::los_limited) == 0)
            mine.options |= option::los_limited | option::los_true;
        else if ((mine.options & option::los_true) != 0)
            mine.options &= static_cast<uint16_t>(~option::los_true);
        else
            mine.options &= static_cast<uint16_t>(~option::los_limited);
        finish(true);
        return action;
    }
    if (panel_selected_is(panel, "WATCHING")) {
        play(lobby, "Multi");
        mine.options ^= option::watching_allowed;
        if ((mine.options & option::watching_allowed) == 0 && me.in_use != 0 &&
            (mine.options & option::watcher) != 0)
            mine.options &= static_cast<uint16_t>(~option::watcher);
        finish(true);
        return action;
    }
    if (panel_selected_is(panel, "CHEATING") || panel_selected_is(panel, "FIXEDLOC")) {
        play(lobby, "Multi");
        mine.options ^=
            panel_selected_is(panel, "CHEATING") ? option::cheats_allowed : option::fixed_locations;
        lobby_send_player_info(lobby);
        finish(false);
        return action;
    }
    if (panel_selected_is(panel, "MAPPING")) {
        play(lobby, "Multi");
        const auto* mapping = panel_control(panel, "MAPPING");
        const bool unmapped = mapping != nullptr && mapping->stage == 0;
        mine.options = static_cast<uint16_t>(
            (mine.options & ~option::unmapped) | (unmapped ? option::unmapped : 0)
        );
        finish(true);
        return action;
    }
    if (panel_selected_is(panel, "START")) {
        play(lobby, "BigButton");
        int32_t discs = 0;
        for (int32_t slot = 0; slot < kSlotCount; ++slot) {
            auto& player = slot_player(lobby, slot);
            if ((occupied_by(player, kSlotLocal) || slot_remote_playing(lobby, player)) &&
                (info_of(lobby, player).status & status::has_disc) != 0)
                ++discs;
        }
        const auto players = playing_count(lobby);
        if (discs < 1 || (discs < 2 && players > 3) || (discs < 3 && players > 6)) {
            panel.selected = kNoControl;
            message(lobby, "There are not enough game CDs present to play");
            return action;
        }
        if (lobby_all_on_one_team(lobby)) {
            panel.selected = kNoControl;
            message(lobby, "Can not start game with all players on the same team.");
            return action;
        }
        if (map_selected(lobby)) {
            if ((mine.options & option::watching_allowed) == 0)
                for (int32_t slot = 0; slot < kSlotCount; ++slot) {
                    auto& player = slot_player(lobby, slot);
                    if (occupied_by(player, kSlotRemote) &&
                        (info_of(lobby, player).options & option::watcher) != 0)
                        lobby_reject(lobby, player.player_id, kRejectWatching);
                }
            game.frontend_pending_signal = 0x11;
            mine.options |= option::started;
            lobby_publish_session(lobby);
            set_host_options(game, mine.options);
            notify(lobby, 10);
            return LobbyAction::start;
        }
        play(lobby, "Multi");
        panel.selected = kNoControl;
        return LobbyAction::select_map;
    }
    if (panel_selected_is(panel, "GAMEOPEN")) {
        play(lobby, "Multi");
        const auto* open = panel_control(panel, "GAMEOPEN");
        const bool closed = open != nullptr && open->stage == 0;
        mine.options = static_cast<uint16_t>(
            (mine.options & ~option::game_closed) | (closed ? option::game_closed : 0)
        );
        finish(true);
        return action;
    }
    if (panel_selected_is(panel, "RESTRICTIONS")) {
        play(lobby, "Options");
        panel.selected = kNoControl;
        return LobbyAction::restrictions;
    }
    if (panel_selected_is(panel, "MAP") || panel_selected_is(panel, "MAPNAME")) {
        play(lobby, "Multi");
        panel.selected = kNoControl;
        if ((mine.role & kRoleHost) == 0 || lobby_launch_locked(lobby))
            return LobbyAction::view_map;
        return LobbyAction::select_map;
    }
    panel.selected = kNoControl;
    return action;
}

/// Broadcasts a ping (0x02) from a player of this machine, stamped with this machine's millisecond clock.
///
/// It goes at once without guaranteed delivery; the echoes fill the Ping
/// column.
///
/// @param[in,out] lobby Lobby state.
/// @param from Local or computer player the ping is from.
void send_ping(Lobby& lobby, uint32_t from) noexcept {
    netgame::PingRecord ping{};
    ping.origin_tick_count = milliseconds(lobby);
    ping.origin_player_id = from;
    uint8_t wire[16];
    std::size_t written = 0;
    if (netgame::encode_record(ping, wire, sizeof(wire), &written) == netgame::WireError::ok)
        send(lobby, from, kBroadcastId, wire, written, true);
}

/// Broadcasts a probe (0x06) from a player of this machine.
///
/// @param[in,out] lobby Lobby state.
/// @param from Local or computer player the probe is from.
void send_probe(Lobby& lobby, uint32_t from) noexcept {
    const auto probe = static_cast<uint8_t>(netgame::RecordType::probe);
    send(lobby, from, kBroadcastId, &probe, 1);
}

/// Runs the battle room's periodic block, about every two seconds.
///
/// For each seated local or computer player in slot order, each record from
/// that player: whatever is queued is flushed, a ping goes out, then a
/// probe; while the player shows no colour the local player asks the host
/// for colour 0 (a host takes it itself when it is free); the host's own
/// player also sends the slot table. Then every such player's lobby block
/// and team, and the machine-group requests, go out as
/// lobby_send_player_info sends them.
///
/// @param[in,out] lobby Lobby state.
void send_periodic_status(Lobby& lobby) noexcept {
    if ((lobby.game->session_flags & kNetFlagLive) == 0)
        return;
    for (int32_t slot = 0; slot < kSlotCount; ++slot) {
        auto& player = slot_player(lobby, slot);
        if (!local_or_computer(player))
            continue;
        flush(lobby);
        send_ping(lobby, player.player_id);
        send_probe(lobby, player.player_id);
        const auto& info = info_of(lobby, player);
        if (info.color == kNoColor)
            lobby_request_color(lobby, 0);
        if ((info.role & kRoleHost) != 0)
            lobby_send_slot_table(lobby);
    }
    lobby_send_player_info(lobby);
}

} // namespace

LobbyAction lobby_handle_event(Lobby& lobby, Panel& panel) noexcept {
    const auto action = handle_panel_event(lobby, panel);
    flush(lobby);
    return action;
}

LobbyAction lobby_tick(Lobby& lobby, Panel& panel, LobbyFront front, Panel* view_map) noexcept {
    auto& game = *lobby.game;
    bool changed = false;
    LobbyEvent event{};
    while (lobby.net.receive != nullptr && lobby.net.receive(lobby.net.context, &event))
        changed = lobby_apply_event(lobby, event) || changed;
    lobby.stalled_player = lobby_check_timeouts(lobby);
    note_shared_machines(game);
    if (changed || !lobby_has_host_map(lobby))
        game.gui_flags |= 1U;

    auto& me = local_player(lobby);
    if (me.reject_reason != 0) {
        game.frontend_pending_signal = 3;
        return LobbyAction::leave;
    }
    if ((game.gui_flags & 1U) != 0) {
        if (!lobby.rows_built)
            lobby_build_rows(lobby, panel);
        else
            lobby_compact_slots(lobby);
        if (lobby_player_count(game) != lobby.last_player_count) {
            lobby.last_player_count = lobby_player_count(game);
            lobby_publish_session(lobby);
        }
        const auto host = host_is_local(lobby) ? kNoSlot : lobby_host_slot(lobby);
        if (host != kNoSlot && front == LobbyFront::battleroom) {
            auto& host_info = info_of(lobby, slot_player(lobby, host));
            if (lobby.maps.select != nullptr)
                lobby.maps.select(lobby.maps.context, host_info.map_name);
            if (auto* units = panel_control(panel, "MAXUNITS"))
                oa::ui::gui_input::scroll_set_value(
                    units->scroll, host_info.max_units - kMaxUnitsFloor
                );
            if (auto* metal = panel_control(panel, "METAL"))
                oa::ui::gui_input::scroll_set_value(metal->scroll, host_info.metal_hundreds * 100);
            if (auto* energy = panel_control(panel, "ENERGY"))
                oa::ui::gui_input::scroll_set_value(
                    energy->scroll, host_info.energy_hundreds * 100
                );
            lobby_on_max_units(panel, &lobby);
            lobby_on_energy(panel, &lobby);
            lobby_on_metal(panel, &lobby);
        } else if (host != kNoSlot && front == LobbyFront::view_map && view_map != nullptr) {
            const auto& host_info = info_of(lobby, slot_player(lobby, host));
            (void)viewmap_follow_host(
                lobby,
                *view_map,
                std::string_view(
                    host_info.map_name, ::strnlen(host_info.map_name, sizeof(host_info.map_name))
                )
            );
        }
        game.gui_flags &= static_cast<uint8_t>(~1U);
        // The battle room's status and doors wait while a dialog is in front.
        if (front == LobbyFront::battleroom) {
            if (host_is_local(lobby)) {
                const bool synced = unit_sync_complete(lobby);
                const bool ready = lobby_ready_to_start(lobby);
                const auto tick = now(lobby);
                panel_set_grayed(panel, "SYNCHING", true);
                if (lobby.start_frame > 0 && lobby_clock_passed(tick, lobby.start_frame_tick)) {
                    if (lobby.start_frame < kStartFrameStop) {
                        ++lobby.start_frame;
                        panel.dirty = true;
                        game.gui_flags |= 1U;
                    }
                    // A reading below the step's time has turned over to 0
                    // since it was set; the steps go on from that reading.
                    lobby.start_frame_tick =
                        std::min(tick, lobby.start_frame_tick) + kStartFrameTicks;
                    if (lobby.start_frame == kStartFrameSound)
                        play(lobby, "Panel");
                }
                if (lobby.start_frame != 0 && lobby.start_frame < kStartFrames - 1)
                    game.gui_flags |= 1U;
                if (ready) {
                    game.gui_flags |= 1U;
                    if (lobby.start_frame == 0) {
                        lobby.start_frame = 1;
                        lobby.start_frame_tick = tick;
                        play(lobby, "Options");
                    }
                    // The doors open for good and no longer take START's clicks.
                    if (auto* doors = panel_control(panel, "battlestart"))
                        doors->hot = false;
                    if (auto* start = panel_control(panel, "START"))
                        start->light_level = static_cast<uint8_t>(tick & kStartLightMask);
                }
                if (auto* sprite = panel_control(panel, "battlestart"))
                    sprite->stage = static_cast<uint8_t>(lobby.start_frame);
                panel_set_grayed(panel, "START", !ready);
                panel_set_active(panel, "START", synced);
                panel_set_active(panel, "SYNCHING", !synced);
            }
            lobby_update_status(lobby, panel);
            panel.dirty = true;
        }
    }
    unit_sync_tick(lobby);
    if (lobby_clock_passed(now(lobby), lobby.next_stats_tick)) {
        lobby.next_stats_tick = now(lobby) + kStatsInterval;
        auto& info = local_info(lobby);
        info.status = static_cast<uint16_t>(
            (info.status & ~status::has_disc) | (disc_present(lobby) ? status::has_disc : 0)
        );
        send_periodic_status(lobby);
    }
    flush(lobby);
    return LobbyAction::none;
}

bool lobby_clock_passed(uint32_t tick, uint32_t due) noexcept {
    return !base::game_loop::scaled_clock_before(tick, due + 1);
}

uint32_t lobby_check_timeouts(Lobby& lobby) noexcept {
    auto& game = *lobby.game;
    if ((game.console_flags & OA_CONSOLE_FLAG_NO_DROP) != 0)
        return lobby.stalled_player;
    const auto tick = now(lobby);
    if ((game.sim_run_flags & kRunFlagPaused) != 0) {
        lobby.timeout_baseline = tick;
        return lobby.stalled_player;
    }
    const auto limit = static_cast<uint32_t>(game.player_timeout_seconds) * kTicksPerSecond;
    // Silence counts from the later of the player's last word and the last
    // pause, counting a turn of the clock to 0 between.
    const auto stalled = [&](const Player& player) {
        const auto silent = std::min(
            base::game_loop::scaled_clock_elapsed(tick, player.last_update_time),
            base::game_loop::scaled_clock_elapsed(tick, lobby.timeout_baseline)
        );
        return occupied_by(player, kSlotRemote) && silent > limit;
    };
    int64_t group = -1;
    for (const auto& player : game.players) {
        if (!stalled(player))
            continue;
        if (group < 0)
            group = player.machine_group;
        else if (group != player.machine_group)
            return 0; // several machines are silent
    }
    for (const auto& player : game.players)
        if (stalled(player))
            return player.player_id;
    return 0;
}

void lobby_leave_battleroom(Lobby& lobby) noexcept {
    lobby_update_ally_matrix(lobby);
    unit_sync_destroy(lobby);
}

// ---------------------------------------------------------------------------
// Records

void lobby_send_player_info(Lobby& lobby) noexcept {
    if ((lobby.game->session_flags & kNetFlagLive) == 0)
        return;
    for (int32_t slot = 0; slot < kSlotCount; ++slot) {
        auto& player = slot_player(lobby, slot);
        if (!local_or_computer(player))
            continue;
        netgame::PlayerInfoRecord record{};
        const auto* bytes = reinterpret_cast<const uint8_t*>(&info_of(lobby, player));
        std::memcpy(record.info_head, bytes, sizeof(record.info_head));
        record.player_id = player.player_id;
        std::memcpy(
            record.info_tail, bytes + netgame::player_info_tail_offset, sizeof(record.info_tail)
        );
        uint8_t wire[kLobbyRecordBytes];
        std::size_t written = 0;
        if (netgame::encode_record(record, wire, sizeof(wire), &written) == netgame::WireError::ok)
            send(lobby, player.player_id, kBroadcastId, wire, written);
        lobby_send_team(lobby, player);
    }
    lobby_request_machine_groups(lobby);
    flush(lobby);
}

bool lobby_color_available(Lobby& lobby, uint32_t player_id, int32_t color) noexcept {
    if (color == kNoColor || color < 0 || color >= kColorCount)
        return false;
    for (int32_t slot = 0; slot < kSlotCount; ++slot) {
        auto& player = slot_player(lobby, slot);
        if (player.status != kSlotOpen && player.player_id != player_id &&
            info_of(lobby, player).color == static_cast<uint8_t>(color))
            return false;
    }
    return true;
}

bool lobby_assign_free_color(Lobby& lobby, uint32_t player_id, uint32_t color) noexcept {
    auto candidate = color;
    for (int32_t tries = 0;;) {
        if (candidate >= static_cast<uint32_t>(kColorCount))
            candidate = 0;
        bool used = false;
        for (int32_t slot = 0; slot < kSlotCount && !used; ++slot) {
            auto& player = slot_player(lobby, slot);
            used = player.status != kSlotOpen && player.status != kSlotBlocked &&
                   player.player_id != player_id && info_of(lobby, player).color == candidate;
        }
        if (!used)
            break;
        ++candidate;
        if (++tries >= kColorCount)
            break;
    }
    const auto value = static_cast<uint8_t>(candidate);
    if (player_id == local_player(lobby).player_id) {
        local_info(lobby).color = value;
        return true;
    }
    netgame::PlayerValueReplyRecord reply{};
    reply.value = value;
    uint8_t wire[4];
    std::size_t written = 0;
    if (netgame::encode_record(reply, wire, sizeof(wire), &written) != netgame::WireError::ok)
        return false;
    send(lobby, local_player(lobby).player_id, player_id, wire, written);
    flush(lobby);
    const auto slot = slot_for_player_id(lobby, player_id);
    if (slot >= 0)
        info_of(lobby, slot_player(lobby, slot)).color = value;
    return true;
}

void lobby_request_color(Lobby& lobby, uint32_t color) noexcept {
    const auto& me = local_player(lobby);
    const auto host = lobby_host_slot(lobby);
    if (host == lobby.game->local_player_index) {
        if (lobby_color_available(lobby, me.player_id, static_cast<int32_t>(color)))
            local_info(lobby).color = static_cast<uint8_t>(color);
        else
            (void)lobby_assign_free_color(lobby, me.player_id, color);
        return;
    }
    netgame::PlayerValueRequestRecord request{};
    request.value = static_cast<uint8_t>(color);
    uint8_t wire[4];
    std::size_t written = 0;
    if (netgame::encode_record(request, wire, sizeof(wire), &written) != netgame::WireError::ok)
        return;
    send(
        lobby,
        me.player_id,
        host == kNoSlot ? kBroadcastId : slot_player(lobby, host).player_id,
        wire,
        written
    );
    flush(lobby);
}

void lobby_send_team(Lobby& lobby, const Player& player) noexcept {
    if (!local_or_computer(player))
        return;
    netgame::PlayerTeamRecord record{};
    record.player_id = player.player_id;
    record.value = lobby_player_team(const_cast<Player&>(player));
    uint8_t wire[16];
    std::size_t written = 0;
    if (netgame::encode_record(record, wire, sizeof(wire), &written) == netgame::WireError::ok)
        send(lobby, player.player_id, kBroadcastId, wire, written);
    flush(lobby);
}

void lobby_set_alliance(
    Lobby& lobby, Player& from, Player& to, uint8_t value, bool both_sides
) noexcept {
    if (from.index >= kSlotCount || to.index >= kSlotCount)
        return;
    if (local_or_computer(from)) {
        from.alliance[to.index] = value;
        if (occupied_by(to, kSlotComputer) || slot_remote_defeated(lobby, to) || both_sides)
            lobby_player_allied_back(from)[to.index] = value;
    }
    if (local_or_computer(to)) {
        lobby_player_allied_back(to)[from.index] = value;
        if (occupied_by(to, kSlotComputer) || both_sides)
            to.alliance[from.index] = value;
    } else if (occupied_by(to, kSlotRemote)) {
        lobby_send_alliance(lobby, from, to, value, both_sides ? 1U : 0U);
    }
    // The game reports only in a multiplayer session, which the lobby and
    // the in-game allies panel always are.
    notify(lobby, 4);
}

void lobby_send_alliance(
    Lobby& lobby, const Player& from, const Player& to, uint8_t value, uint32_t both_sides
) noexcept {
    netgame::AllianceRecord record{};
    record.player_id_a = from.player_id;
    record.player_id_b = to.player_id;
    record.value = value;
    record.both_sides = both_sides;
    uint8_t wire[32];
    std::size_t written = 0;
    if (netgame::encode_record(record, wire, sizeof(wire), &written) == netgame::WireError::ok)
        send(lobby, from.player_id, to.player_id, wire, written);
    flush(lobby);
}

namespace {

/// Returns the first seated local or computer player in slot order, which
/// the records of a rejection leave from.
///
/// @param lobby Lobby state.
/// @param gone A player that has already left the game and no longer
///        counts; null for none.
/// @return The player, or null when none is seated.
const Player* first_seated_here(Lobby& lobby, const Player* gone) noexcept {
    for (int32_t slot = 0; slot < kSlotCount; ++slot) {
        const auto& player = slot_player(lobby, slot);
        if (&player != gone && local_or_computer(player))
            return &player;
    }
    return nullptr;
}

/// Broadcasts 0x1b {id, reason} from a player of this machine.
///
/// Nothing is sent without a sender or from one already rejected, since a
/// rejected player sends nothing.
///
/// @param[in,out] lobby Lobby state.
/// @param from Sending player; null sends nothing.
/// @param player_id Player the record names.
/// @param reason RejectReason value.
void send_reject(Lobby& lobby, const Player* from, uint32_t player_id, uint8_t reason) noexcept {
    if (from == nullptr || from->reject_reason != 0)
        return;
    netgame::RejectRecord record{};
    record.player_id = player_id;
    record.reason = reason;
    uint8_t wire[16];
    std::size_t written = 0;
    if (netgame::encode_record(record, wire, sizeof(wire), &written) == netgame::WireError::ok)
        send(lobby, from->player_id, kBroadcastId, wire, written);
}

/// Takes a rejected player out of the game: it is no longer counted
/// (Game.player_count) and, unless it is this machine's own after the
/// start, its slot opens; before the start a computer player of this
/// machine also leaves the session.
///
/// @param[in,out] lobby Lobby state.
/// @param[in,out] player Player leaving.
void retire_slot(Lobby& lobby, Player& player) noexcept {
    auto& game = *lobby.game;
    const bool started = (game.session_flags & kNetFlagGameStarted) != 0;
    if (!started || !local_or_computer(player)) {
        if (!started && occupied_by(player, kSlotComputer) && lobby.net.remove_player != nullptr)
            lobby.net.remove_player(lobby.net.context, player.player_id);
        slot_set_status(lobby, player, kSlotOpen);
        player.in_use = 0;
        player.machine_group = 0;
    }
    if (lobby_player_count(game) > 0)
        lobby_player_count(game) = static_cast<uint16_t>(lobby_player_count(game) - 1);
}

} // namespace

void lobby_reject(Lobby& lobby, uint32_t player_id, uint8_t reason) noexcept {
    const auto slot = slot_for_player_id(lobby, player_id);
    if (slot < 0)
        return;
    auto& player = slot_player(lobby, slot);
    if (player.reject_reason == 0) {
        if (occupied_by(player, kSlotLocal)) {
            // Each local and computer player of this machine is named in
            // slot order, from the first one still seated. Before the start
            // the local player leaves the game at the first of these, so
            // the next seated player sends the rest and the local player is
            // not named after a computer player seated ahead of it; after
            // the start it stays seated, and once rejected sends no more.
            const bool started = (lobby.game->session_flags & kNetFlagGameStarted) != 0;
            const Player* gone = nullptr;
            for (int32_t other = 0; other < kSlotCount; ++other) {
                auto& mate = slot_player(lobby, other);
                if (!local_or_computer(mate) || &mate == gone)
                    continue;
                send_reject(lobby, first_seated_here(lobby, gone), mate.player_id, reason);
                if (!started)
                    gone = &player;
                mate.reject_reason = reason;
            }
        } else if (occupied_by(player, kSlotComputer)) {
            send_reject(lobby, first_seated_here(lobby, nullptr), player_id, reason);
            retire_slot(lobby, player);
        } else if (occupied_by(player, kSlotRemote)) {
            send_reject(lobby, first_seated_here(lobby, nullptr), player_id, reason);
            if (slot_remote_playing(lobby, player)) {
                // A human still playing takes its machine's other players
                // with it: every player of its machine group.
                const auto group = player.machine_group;
                for (int32_t other = 0; other < kSlotCount; ++other) {
                    auto& mate = slot_player(lobby, other);
                    if (!occupied_by(mate, kSlotRemote) || mate.machine_group != group)
                        continue;
                    retire_slot(lobby, mate);
                    mate.reject_reason = reason;
                }
            } else {
                retire_slot(lobby, player);
            }
        }
    }
    player.reject_reason = reason;
}

void lobby_post_chat(Lobby& lobby, const char* line) noexcept {
    auto& game = *lobby.game;
    const uint16_t head = lobby_chat_head(game);
    copy_text(lobby_chat_line(game, head), kChatLineBytes, line);
    auto next = static_cast<uint16_t>((head + 1U) % kChatLines);
    lobby_chat_head(game) = next;
    if (next == static_cast<uint16_t>(lobby_chat_tail(game)))
        lobby_chat_tail(game) = static_cast<uint16_t>((next + 1U) % kChatLines);
    game.gui_flags |= 1U;
}

void lobby_say(Lobby& lobby, const Player& speaker, const char* text) noexcept {
    char line[200];
    std::snprintf(
        line,
        sizeof(line),
        "<%s> %s",
        std::string(text_view(speaker.name, sizeof(speaker.name))).c_str(),
        text
    );
    // The record carries the line's first 64 bytes, the rest zero, with no
    // terminator when the line fills it.
    netgame::ChatRecord record{};
    std::memcpy(record.text, line, ::strnlen(line, sizeof(record.text)));
    uint8_t wire[80];
    std::size_t written = 0;
    if (netgame::encode_record(record, wire, sizeof(wire), &written) == netgame::WireError::ok)
        send(lobby, speaker.player_id, kBroadcastId, wire, written);
    flush(lobby);
    lobby_post_chat(lobby, line);
}

void lobby_session_description(Lobby& lobby, char* name, uint8_t* user) noexcept {
    auto& game = *lobby.game;
    auto& info = local_info(lobby);
    // While a launch is active the session is marked launch-only and its
    // version biased.
    const bool launched = lobby_launch_active(lobby);
    info.version_major = static_cast<uint8_t>(
        static_cast<int32_t>(lobby.local_version_major) + (launched ? kLaunchVersionBias : 0)
    );
    info.options =
        static_cast<uint16_t>((info.options & ~0xfU) | (lobby_player_count(game) & 0xfU));
    info.status = static_cast<uint16_t>(
        (info.status & ~status::launch_only) | (launched ? status::launch_only : 0)
    );
    std::memcpy(
        user, reinterpret_cast<const uint8_t*>(&info) + kSessionUserInfoOffset, kSessionUserBytes
    );
    std::memset(name, ' ', kSessionNameBytes);
    const auto game_name = text_view(lobby_game_name(game), kSessionGameNameBytes);
    std::memcpy(name, game_name.data(), game_name.size());
    const auto map = text_view(map_name(lobby), kSessionMapNameBytes);
    std::memcpy(name + kSessionGameNameBytes, map.data(), map.size());
    name[kSessionNameBytes - 1] = '\0';
}

void lobby_publish_session(Lobby& lobby) noexcept {
    if (lobby.net.describe == nullptr || !host_is_local(lobby))
        return;
    char name[kSessionNameBytes];
    uint8_t user[kSessionUserBytes];
    lobby_session_description(lobby, name, user);
    const auto& info = local_info(lobby);
    // A password is checked against the joiner's tag, never flagged on the
    // session.
    uint32_t flags = kSessionFlagGame;
    if ((info.options & option::started) != 0)
        flags |= kSessionFlagStarted;
    const auto players = static_cast<int32_t>(lobby_session_players(*lobby.game));
    lobby.net.describe(
        lobby.net.context, name, user, flags, static_cast<uint32_t>(std::max(players, 0))
    );
}

void lobby_send_slot_table(Lobby& lobby) noexcept {
    if ((lobby.game->session_flags & kNetFlagLive) == 0 || !host_is_local(lobby))
        return;
    auto& game = *lobby.game;
    netgame::SlotTableRecord record{};
    for (int32_t slot = 0; slot < kSlotCount; ++slot) {
        const auto& player = slot_player(lobby, slot);
        game.slot_table[slot] = player.status == kSlotBlocked ? kNoPlayerId
                                : player.status == kSlotOpen  ? 0U
                                                              : player.player_id;
        record.slot_ids[slot] = game.slot_table[slot];
    }
    uint8_t wire[64];
    std::size_t written = 0;
    const auto host = lobby_host_slot(lobby);
    if (netgame::encode_record(record, wire, sizeof(wire), &written) == netgame::WireError::ok)
        send(lobby, slot_player(lobby, host).player_id, kBroadcastId, wire, written);
}

void lobby_add_computer(Lobby& lobby, int32_t slot) noexcept {
    auto& game = *lobby.game;
    auto& player = slot_player(lobby, slot);
    slot_set_status(lobby, player, kSlotComputer);
    char name[kComputerNameBytes + 1]{};
    // The name is cut to the field; a failed format leaves it empty.
    if (std::snprintf(name, sizeof(name), "AI:%s", local_player(lobby).name) < 0)
        name[0] = '\0';
    auto& info = info_of(lobby, player);
    const auto host_role = slot == lobby_host_slot(lobby) ? kRoleHost : 0;
    info.role = static_cast<uint8_t>((info.role & ~kRoleHost) | host_role);
    info.color = kNoColor;
    player.machine_flags =
        static_cast<uint8_t>(player.machine_flags & ~kMachineFlagJoinedWithoutBattleroom);
    lobby_player_join_tick(player) = now(lobby);
    const auto count = static_cast<uint16_t>(lobby_player_count(game) & option::player_count_mask);
    info.options = static_cast<uint16_t>(
        (info.options & ~(option::player_count_mask | option::game_closed)) | count
    );
    info.max_units = kSeatedMaxUnits;
    const auto password = lobby_password(game)[0] != '\0' ? status::password : 0;
    const auto launch_only = lobby_launch_active(lobby) ? status::launch_only : 0;
    info.status = static_cast<uint16_t>(
        (info.status & ~(status::password | status::launch_only)) | password | launch_only
    );
    info.screen_width = static_cast<uint16_t>(static_cast<int32_t>(lobby_screen_width(game)));
    info.screen_height = static_cast<uint16_t>(static_cast<int32_t>(lobby_screen_height(game)));
    info.version_major = static_cast<uint8_t>(lobby.local_version_major);
    info.version_minor = static_cast<uint8_t>(lobby.local_version_minor);
    uint32_t id = 0;
    if (lobby.net.add_player == nullptr ||
        lobby.net.add_player(lobby.net.context, name, lobby_password(game), &id) !=
            LobbyResult::ok) {
        slot_set_status(lobby, player, kSlotOpen);
        message(
            lobby,
            host_is_local(lobby) ? "Direct Play failed to add new player.\n\nRecommended you go to "
                                   "previous screen and re-create the game session.\n"
                                 : "Direct Play failed to add new player.\n\nRecommended you go to "
                                   "previous screen and re-join the game session.\n"
        );
        return;
    }
    player.in_use = 0;
    player.player_id = id;
    player.index = static_cast<uint8_t>(slot);
    player.machine_group = 0;
    std::snprintf(player.name, sizeof(player.name), "%s", "Computer");
}

// ---------------------------------------------------------------------------
// Machine groups

void note_shared_machines(Game& game) noexcept {
    uint32_t members[kMachineGroupCount + 1]{};
    game.shared_machines = 0;
    for (const auto& player : game.players) {
        const auto group = player.machine_group;
        if (!slot_active(player) || group == 0 || group > kMachineGroupCount)
            continue;
        if (++members[group] > 1) {
            game.shared_machines = 1;
            return;
        }
    }
}

uint32_t free_machine_group(const Game& game) noexcept {
    for (uint32_t group = 1; group <= kMachineGroupCount; ++group) {
        bool held = false;
        for (const auto& player : game.players)
            held = held || (slot_active(player) && player.machine_group == group);
        if (!held)
            return group;
    }
    return 0;
}

int32_t machine_broadcast_targets(const Game& game, uint32_t (&ids)[kSlotCount]) noexcept {
    bool reached[kMachineGroupCount + 1]{};
    int32_t count = 0;
    for (const auto& player : game.players) {
        const auto group = player.machine_group;
        if (!occupied_by(player, kSlotRemote) || (group <= kMachineGroupCount && reached[group]))
            continue;
        ids[count++] = player.player_id;
        if (group < kMachineGroupCount)
            reached[group] = true;
    }
    return count;
}

void lobby_request_machine_groups(Lobby& lobby) noexcept {
    const bool hosting = host_is_local(lobby);
    const auto host = lobby_host_slot(lobby);
    for (int32_t slot = 0; slot < kSlotCount; ++slot) {
        auto& player = slot_player(lobby, slot);
        if (!slot_active(player) || player.machine_group != 0)
            continue;
        netgame::MachineGroupRequestRecord request{};
        request.player_id = player.player_id;
        request.same_machine_id = kNoPlayerId;
        if (local_or_computer(player)) {
            if (hosting) {
                player.machine_group = kHostMachineGroup;
                continue;
            }
            request.assign = 1;
            if (player.status == kSlotComputer)
                request.same_machine_id = local_player(lobby).player_id;
        }
        // Only a remote host can be asked; the host's own requests go nowhere.
        if (host == kNoSlot || !occupied_by(slot_player(lobby, host), kSlotRemote))
            continue;
        uint8_t wire[16];
        std::size_t written = 0;
        if (netgame::encode_record(request, wire, sizeof(wire), &written) == netgame::WireError::ok)
            send(lobby, primary_id(lobby), slot_player(lobby, host).player_id, wire, written);
    }
}

namespace {

/// Answers a 0x21 request on the host, broadcasting the group as 0x22.
///
/// Assign 0 reports the asking player's group; assign 1 gives it the group
/// of the player named as sharing its machine, or a new one when none is
/// named and it has none yet.
///
/// @param[in,out] lobby Hosting lobby state.
/// @param request The received request.
/// @return False when not hosting, the player is unknown, the named machine mate is inactive or no group
///         could be given.
bool assign_machine_group(
    Lobby& lobby, const netgame::MachineGroupRequestRecord& request
) noexcept {
    const auto slot = slot_for_player_id(lobby, request.player_id);
    if (!host_is_local(lobby) || slot < 0)
        return false;
    auto& player = slot_player(lobby, slot);
    uint32_t group = 0;
    if (request.assign == 0) {
        group = player.machine_group;
    } else {
        const auto same = slot_for_player_id(lobby, request.same_machine_id);
        if (same < 0) {
            if (player.machine_group != 0)
                return false;
            group = free_machine_group(*lobby.game);
        } else {
            const auto& other = slot_player(lobby, same);
            if (!slot_active(other))
                return false;
            group = other.machine_group;
        }
        if (group != 0)
            player.machine_group = group;
    }
    if (group == 0)
        return false;
    netgame::MachineGroupReplyRecord reply{};
    reply.player_id = player.player_id;
    reply.machine_group = static_cast<uint8_t>(group);
    uint8_t wire[16];
    std::size_t written = 0;
    if (netgame::encode_record(reply, wire, sizeof(wire), &written) == netgame::WireError::ok)
        send(
            lobby, slot_player(lobby, lobby_host_slot(lobby)).player_id, kBroadcastId, wire, written
        );
    return true;
}

} // namespace

int32_t slot_for_player_id(Lobby& lobby, uint32_t player_id) noexcept {
    for (int32_t slot = 0; slot < kSlotCount; ++slot) {
        auto& player = slot_player(lobby, slot);
        if (player.in_use != 0 && player.player_id == player_id)
            return slot;
    }
    return -1;
}

bool lobby_add_player(Lobby& lobby, uint32_t player_id, const char* name) noexcept {
    auto& game = *lobby.game;
    auto slot = netgame::player_slot_of(game, player_id);
    if (slot == netgame::no_player_slot) {
        slot = netgame::free_player_slot(game);
        if (slot == netgame::no_player_slot)
            return false;
        game.players[slot].status = kSlotRemote;
    } else {
        const auto& seated = game.players[slot];
        if ((seated.status != kSlotLocal && seated.status != kSlotComputer) || seated.in_use != 0)
            return false;
    }
    auto& player = game.players[slot];
    copy_text(
        player.name,
        sizeof(player.name),
        text_view(name != nullptr ? name : "", sizeof(LobbyEvent{}.name))
    );
    // The session's short name goes to Player.second_name (the name chat lines
    // carry) and its long name to Player.name; the session has one name
    // for both.
    copy_text(
        player.second_name, sizeof(player.second_name), text_view(player.name, sizeof(player.name))
    );
    player.in_use = 1;
    player.index = slot;
    slot_set_status(lobby, player, player.status);
    auto& info = info_of(lobby, player);
    if (player.status == kSlotRemote) {
        info.state = kInfoStatePlaying;
        info.color = kNoColor;
    }
    player.machine_group = 0;
    info.options = static_cast<uint16_t>(info.options & ~option::ready);
    player.reject_reason = 0;
    player.player_id = player_id;
    player.last_update_time = now(lobby);
    lobby_player_count(game) = static_cast<uint16_t>(lobby_player_count(game) + 1);
    lobby_send_player_info(lobby);
    return true;
}

bool lobby_apply_event(Lobby& lobby, const LobbyEvent& event) noexcept {
    auto& game = *lobby.game;
    switch (event.kind) {
    case LobbyEventKind::player_joined:
        if (event.refuse_reason != 0) {
            // A refused joiner is told with this machine's lobby blocks, then
            // its rejection; it is not seated and leaves by itself. Records
            // that go out per machine reach it as well.
            lobby.refused_joiner = event.player_id;
            lobby_send_player_info(lobby);
            send_reject(
                lobby, first_seated_here(lobby, nullptr), event.player_id, event.refuse_reason
            );
            flush(lobby);
            lobby.refused_joiner = 0;
            return false;
        }
        return lobby_add_player(lobby, event.player_id, event.name);
    case LobbyEventKind::player_left: {
        const auto slot = slot_for_player_id(lobby, event.player_id);
        if (slot < 0)
            return false;
        auto& player = slot_player(lobby, slot);
        // The creator leaving before the start ends the game here: its
        // player and this machine's leave with reason 10.
        const bool creator_left = (game.session_flags & kNetFlagGameStarted) == 0 &&
                                  player.status == kSlotRemote &&
                                  (info_of(lobby, player).role & kRoleHost) != 0;
        lobby_reject(lobby, event.player_id, kRejectPlayer);
        if (creator_left)
            lobby_reject(lobby, local_player(lobby).player_id, kRejectCreatorLeft);
        flush(lobby);
        // A slot the rejection did not retire (one already rejected) is
        // retired here.
        if (player.in_use != 0) {
            slot_set_status(lobby, player, kSlotOpen);
            player.in_use = 0;
            if (lobby_player_count(game) > 0)
                lobby_player_count(game) = static_cast<uint16_t>(lobby_player_count(game) - 1);
        }
        player.index = kNoSlot;
        player.machine_group = 0;
        return true;
    }
    case LobbyEventKind::session_lost:
        local_player(lobby).reject_reason = 6;
        return true;
    case LobbyEventKind::record:
        break;
    default:
        return false;
    }
    if (event.size == 0)
        return false;
    // Records from this machine's own players are not heard; any other
    // seated sender was heard from now.
    if (const auto sender = slot_for_player_id(lobby, event.player_id); sender >= 0) {
        auto& from = slot_player(lobby, sender);
        if (local_or_computer(from))
            return false;
        from.last_update_time = now(lobby);
    }
    const auto type = static_cast<netgame::RecordType>(event.data[0]);
    switch (type) {
    case netgame::RecordType::chat: {
        netgame::ChatRecord record{};
        if (netgame::decode_record(event.data, event.size, &record) != netgame::WireError::ok)
            return false;
        char line[sizeof(record.text) + 1];
        std::memcpy(line, record.text, sizeof(record.text));
        line[sizeof(record.text)] = '\0';
        lobby_post_chat(lobby, line);
        return true;
    }
    case netgame::RecordType::player_info: {
        netgame::PlayerInfoRecord record{};
        if (netgame::decode_record(event.data, event.size, &record) != netgame::WireError::ok)
            return false;
        const auto slot = slot_for_player_id(lobby, record.player_id);
        if (slot < 0 || slot_player(lobby, slot).status != kSlotRemote)
            return false;
        auto* bytes = reinterpret_cast<uint8_t*>(slot_info(lobby, slot));
        std::memcpy(bytes, record.info_head, sizeof(record.info_head));
        std::memcpy(
            bytes + offsetof(PlayerSetupInfo, net_id), &record.player_id, sizeof(record.player_id)
        );
        std::memcpy(
            bytes + netgame::player_info_tail_offset, record.info_tail, sizeof(record.info_tail)
        );
        note_shared_machines(game);
        return true;
    }
    case netgame::RecordType::machine_group_request: {
        netgame::MachineGroupRequestRecord record{};
        if (netgame::decode_record(event.data, event.size, &record) != netgame::WireError::ok)
            return false;
        return assign_machine_group(lobby, record);
    }
    case netgame::RecordType::slot_table: {
        netgame::SlotTableRecord record{};
        if (netgame::decode_record(event.data, event.size, &record) != netgame::WireError::ok)
            return false;
        std::memcpy(game.slot_table, record.slot_ids, sizeof(game.slot_table));
        return true;
    }
    case netgame::RecordType::machine_group_reply: {
        netgame::MachineGroupReplyRecord record{};
        if (netgame::decode_record(event.data, event.size, &record) != netgame::WireError::ok)
            return false;
        const auto slot = slot_for_player_id(lobby, record.player_id);
        if (slot < 0)
            return false;
        slot_player(lobby, slot).machine_group = record.machine_group;
        return true;
    }
    case netgame::RecordType::player_team: {
        netgame::PlayerTeamRecord record{};
        if (netgame::decode_record(event.data, event.size, &record) != netgame::WireError::ok)
            return false;
        const auto slot = slot_for_player_id(lobby, record.player_id);
        if (slot < 0)
            return false;
        lobby_player_team(slot_player(lobby, slot)) = record.value;
        return true;
    }
    case netgame::RecordType::alliance: {
        netgame::AllianceRecord record{};
        if (netgame::decode_record(event.data, event.size, &record) != netgame::WireError::ok)
            return false;
        const auto from = slot_for_player_id(lobby, record.player_id_a);
        const auto to = slot_for_player_id(lobby, record.player_id_b);
        if (from < 0 || to < 0)
            return false;
        if (record.value != 0)
            play(lobby, "Ally");
        auto& sender = slot_player(lobby, from);
        auto& target = slot_player(lobby, to);
        if (local_or_computer(target)) {
            lobby_set_alliance(lobby, sender, target, record.value, record.both_sides != 0);
            game.gui_flags |= 1U;
        }
        sender.alliance[to] = record.value;
        return true;
    }
    case netgame::RecordType::reject: {
        netgame::RejectRecord record{};
        if (netgame::decode_record(event.data, event.size, &record) != netgame::WireError::ok)
            return false;
        if (slot_for_player_id(lobby, record.player_id) < 0)
            return false;
        // Passed on once, as for a rejection made here.
        lobby_reject(lobby, record.player_id, record.reason);
        flush(lobby);
        return true;
    }
    case netgame::RecordType::ping: {
        netgame::PingRecord record{};
        if (netgame::decode_record(event.data, event.size, &record) != netgame::WireError::ok ||
            record.echo_tick_count == 0)
            return false;
        // An echo of a ping this machine sent: the echoing player's ping.
        const auto origin = slot_for_player_id(lobby, record.origin_player_id);
        const auto echoing = slot_for_player_id(lobby, event.player_id);
        if (origin < 0 || echoing < 0 || !local_or_computer(slot_player(lobby, origin)))
            return false;
        lobby_player_ping(slot_player(lobby, echoing)) =
            static_cast<int32_t>(milliseconds(lobby) - record.origin_tick_count);
        return true;
    }
    case netgame::RecordType::player_value_request: {
        netgame::PlayerValueRequestRecord record{};
        if (netgame::decode_record(event.data, event.size, &record) != netgame::WireError::ok ||
            !host_is_local(lobby))
            return false;
        const auto color = static_cast<int8_t>(record.value);
        if (!lobby_color_available(lobby, event.player_id, color)) {
            (void)lobby_assign_free_color(lobby, event.player_id, static_cast<uint32_t>(color));
            return true;
        }
        netgame::PlayerValueReplyRecord reply{};
        reply.value = record.value;
        uint8_t wire[4];
        std::size_t written = 0;
        if (netgame::encode_record(reply, wire, sizeof(wire), &written) == netgame::WireError::ok)
            send(lobby, local_player(lobby).player_id, event.player_id, wire, written);
        flush(lobby);
        return true;
    }
    case netgame::RecordType::player_value_reply: {
        netgame::PlayerValueReplyRecord record{};
        if (netgame::decode_record(event.data, event.size, &record) != netgame::WireError::ok)
            return false;
        local_info(lobby).color = record.value;
        lobby_send_player_info(lobby);
        return true;
    }
    case netgame::RecordType::unit_def_handshake:
        unit_sync_receive(
            lobby,
            {event.data, event.size},
            static_cast<uint8_t>(std::max(0, slot_for_player_id(lobby, event.player_id)))
        );
        return true;
    case netgame::RecordType::game_start:
        game.session_flags |= kNetFlagGameStarted;
        info_of(lobby, local_player(lobby)).options |= option::started;
        game.frontend_pending_signal = 0x11;
        return true;
    default:
        return false;
    }
}

int32_t map_memory_mb(int32_t terrain_bytes) noexcept {
    constexpr int32_t k16_bytes = 0x3e6666, k24_bytes = 0x600000, k32_bytes = 0x800000;
    constexpr int32_t k48_bytes = 0xa00000, k64_bytes = 0xc00000;
    if (terrain_bytes < k16_bytes)
        return 16;
    if (terrain_bytes < k24_bytes)
        return 24;
    if (terrain_bytes < k32_bytes)
        return 32;
    if (terrain_bytes < k48_bytes)
        return 48;
    return terrain_bytes < k64_bytes ? 64 : 128;
}

} // namespace oa::ui::frontend_multiplayer
