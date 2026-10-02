// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Networked match integration: the session behind the multiplayer
// screens, the launch from the battle room through the netgame path (the
// start record, the loading screen's work at its pace and the end of
// loading), the per-tick match binding, and the in-match routing of chat,
// pause, speed and leave. --net-loopback-check hosts and joins in one
// process over 127.0.0.1, runs the in-game team panels over the session
// and compares both worlds after a settled run.
#include "oa/app/runtime.hpp"
#include "network_play.hpp"
#include "oa/app/check_host.hpp"
#include "net_options.hpp"
#include "net_state.hpp"
#include "traffic_overlay.hpp"

#include "oa/netgame/match/launch.hpp"
#include "oa/netgame/match/match_binding.hpp"
#include "oa/netgame/match/net_match.hpp"
#include "oa/netgame/match/session_lobby.hpp"
#include "oa/netgame/records.hpp"
#include "oa/app/netgame/extension_api.hpp"
#include "oa/sim/ai.hpp"
#include "oa/sim/messages.hpp"
#include "oa/ui/frontend_dialogs.hpp"
#include "oa/ui/console/console.hpp"
#include "oa/ui/console/game_fields.hpp"
#include "oa/ui/hud/chat_panel.hpp"
#include "oa/ui/hud/player_records.hpp"
#include "oa/ui/hud/share_panel.hpp"
#include "oa/ui/hud/team_panels.hpp"
#include "oa/ui/frontend_multiplayer/connect.hpp"
#include "oa/ui/frontend_multiplayer/screens.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace oa::app {

namespace mp = oa::ui::frontend_multiplayer;
namespace nm = oa::netgame::match;

namespace {

constexpr uint16_t kLoopbackUnitsPerPlayer = 20;
// With --net-loopback-computer: room for the computer player's base and
// army, on a small map of the base game whose bases lie close together; an
// install without it plays its first map.
constexpr uint16_t kLoopbackComputerUnitsPerPlayer = 100;
constexpr const char* kLoopbackComputerMap = "Ashap Plateau";
// Units the computer player has, its commander among them, once it has
// built for the check's ticks.
constexpr uint32_t kComputerBuiltUnits = 5;
// Most ticks the computer player's strike and the fight may take.
constexpr uint32_t kComputerFightTicks = 9000;
// The computer player's losses the fight must reach.
constexpr int16_t kComputerFightLosses = 2;
// The squad the host's player gives the computer player, enough for its
// land strike to set out: Arm kbots made beside the host's commander, this
// far to its side and this far apart, in world units. The joiner's player
// gives one more across the session.
constexpr int32_t kComputerSquadGifts = 6;
constexpr const char* kComputerSquadType = "ARMHAM";
constexpr const char* kComputerAcrossType = "ARMPW";
constexpr int32_t kComputerGiftAside = 96;
constexpr int32_t kComputerGiftSpacing = 40;
// Ticks the owner's poses are kept for comparing the joiner's copies, and
// how far a copy may stand from its owner: a pixel, in 16.16.
constexpr std::size_t kComputerPoseTicks = 128;
constexpr uint32_t kCopyOffsetLimit = 1u << 16;
constexpr uint32_t kLoopbackWaitMs = 15000;
// Milliseconds a networked tick of the loopback checks waits for the other
// machine's records. None: loopback hands them over at once, and a record
// applies at the tick it names however late it arrives, so the checks
// settle until both machines agree instead of waiting on the clock.
constexpr uint32_t kTickPumpWaitMs = 0;
constexpr int32_t kLoopbackMoveDistance = 64 << 16;
// The host commander fires at the ground this far ahead of it, within its
// laser's range of 200, from tick 200 until tick 260.
constexpr int32_t kLoopbackShotDistance = 100 << 16;
constexpr std::size_t kLoopbackFireFrom = 200;
constexpr std::size_t kLoopbackFireUntil = 260;
// Ticks for the client's commander to self-destruct and its defeat countdown
// to run out: the five-second countdown, then six 30-tick outcome checks.
constexpr uint32_t kLoopbackDefeatTicks = 900;
// Ticks for the host's victory to follow once its enemy watches: at most the
// five-call outcome countdown, one call every 30 ticks, and the check that
// starts it.
constexpr uint32_t kLoopbackVictoryTicks = 180;
// The battlefield and font height the check's recording painter reports to
// the traffic readout: the battlefield's height at 640x480, and the side
// panel font's.
constexpr int kLoopbackOverlayBottom = 480 - 32 - 32;
constexpr uint8_t kLoopbackOverlayFontHeight = 11;

// A unit's movement as its owner's record for one tick leaves it.
struct UnitPose {
    int32_t x{};
    int32_t y{};
    int32_t z{};
    int32_t heading{};
    int32_t speed{};
};

// The lobby block of the player with this session id, or null.
const PlayerSetupInfo* info_for_id(const World& world, uint32_t net_id) {
    for (const auto& player : world.game.players)
        if (player.in_use != 0 && player.player_id == net_id)
            return world_player_info(&world, &player);
    return nullptr;
}

bool watches(const PlayerSetupInfo* info) {
    return info != nullptr && (info->options & OA_SETUP_OPTION_WATCHER) != 0;
}

// Largest difference of any pose component, in its raw units.
uint32_t pose_divergence(const UnitPose& a, const UnitPose& b) {
    uint32_t largest = 0;
    for (const auto& [p, q] :
         {std::pair{a.x, b.x},
          std::pair{a.y, b.y},
          std::pair{a.z, b.z},
          std::pair{a.heading, b.heading},
          std::pair{a.speed, b.speed}})
        largest = std::max(largest, static_cast<uint32_t>(std::abs(static_cast<int64_t>(p) - q)));
    return largest;
}

std::string_view field_text(const char* field, std::size_t capacity) {
    return {field, ::strnlen(field, capacity)};
}

// The line of a match's message log at a place of its lines listed oldest
// first, as match_message_lines lists them; nothing past the newest.
std::optional<oa::sim::messages::MessageLine> logged_line(const World& world, std::size_t place) {
    const auto& game = world.game;
    const auto count = static_cast<std::size_t>(
        (game.chat_head + OA_CHAT_LINE_COUNT - game.chat_tail) % OA_CHAT_LINE_COUNT
    );
    if (place >= count)
        return std::nullopt;
    oa::sim::messages::MessageLine line{};
    std::memcpy(&line, game.chat_lines[(game.chat_tail + place) % OA_CHAT_LINE_COUNT], sizeof line);
    return line;
}

bool slot_live(const Player& player) {
    return player.in_use != 0 &&
           (player.status == OA_PLAYER_STATUS_LOCAL || player.status == OA_PLAYER_STATUS_COMPUTER ||
            player.status == OA_PLAYER_STATUS_MIRRORED);
}

std::string player_name(const World& world, uint32_t net_id) {
    for (const auto& player : world.game.players)
        if (player.in_use != 0 && player.player_id == net_id)
            return std::string(field_text(player.name, sizeof player.name));
    return "player " + std::to_string(net_id);
}

// FNV-1a over the tick and every live unit; owners are named by player id
// because slot orders differ between machines.
uint64_t world_digest(const World& world) {
    constexpr uint64_t kFnvOffsetBasis = 0xcbf29ce484222325ull;
    constexpr uint64_t kFnvPrime = 0x100000001b3ull;
    uint64_t hash = kFnvOffsetBasis;
    const auto mix = [&hash](uint64_t value) {
        hash ^= value;
        hash *= kFnvPrime;
    };
    mix(world.game.tick);
    for (uint32_t slot = 1; slot < world.unit_slot_count; ++slot) {
        const auto& unit = world.units[slot];
        if (unit.type_index == 0)
            continue;
        mix(slot);
        mix(unit.type_index);
        mix(unit.owner_index < OA_PLAYER_COUNT ? world.game.players[unit.owner_index].player_id
                                               : 0u);
        mix(static_cast<uint32_t>(unit.position.x));
        mix(static_cast<uint32_t>(unit.position.y));
        mix(static_cast<uint32_t>(unit.position.z));
        mix(static_cast<uint16_t>(unit.health));
    }
    return hash;
}

/// Returns the slot of the in-use player with a session id.
///
/// @param world match world
/// @param net_id session id
/// @return the slot, or OA_PLAYER_COUNT
uint8_t player_slot(const World& world, uint32_t net_id) {
    for (uint8_t slot = 0; slot < OA_PLAYER_COUNT; ++slot)
        if (world.game.players[slot].in_use != 0 && world.game.players[slot].player_id == net_id)
            return slot;
    return OA_PLAYER_COUNT;
}

/// Tells whether a unit slot holds a live unit.
///
/// @param unit unit record
/// @return true for a unit of some type with its live flag
bool unit_live(const Unit& unit) {
    return unit.type_index != 0 && (unit.flags & OA_UNIT_FLAG_LIVE) != 0;
}

/// Counts the live units a player owns.
///
/// @param world match world
/// @param slot player slot
/// @return the count
uint32_t live_units(const World& world, uint8_t slot) {
    uint32_t count = 0;
    for (uint32_t index = 1; index < world.unit_slot_count; ++index)
        count += unit_live(world.units[index]) && world.units[index].owner_index == slot ? 1 : 0;
    return count;
}

/// Counts the live units of one type a player owns.
///
/// @param world match world
/// @param slot player slot
/// @param type UnitDef index
/// @return the count
uint32_t units_of_type(const World& world, uint8_t slot, uint16_t type) {
    uint32_t count = 0;
    for (uint32_t index = 1; index < world.unit_slot_count; ++index) {
        const auto& unit = world.units[index];
        count += unit_live(unit) && unit.owner_index == slot && unit.type_index == type ? 1 : 0;
    }
    return count;
}

/// Finds a player's live commander.
///
/// @param world match world
/// @param slot player slot
/// @return its unit slot, or 0
uint16_t commander_of(const World& world, uint8_t slot) {
    for (uint32_t index = 1; index < world.unit_slot_count; ++index) {
        const auto& unit = world.units[index];
        if (unit_live(unit) && unit.owner_index == slot && unit.type_index < world.unit_def_count &&
            (world.unit_defs[unit.type_index].abilities & OA_UNIT_DEF_ABILITY_COMMANDER) != 0)
            return static_cast<uint16_t>(index);
    }
    return 0;
}

/// Finds a unit type by its unit name, ignoring case.
///
/// @param world match world
/// @param name unit name, such as ARMPW
/// @return its UnitDef index, or 0
uint16_t unit_type_named(const World& world, std::string_view name) {
    for (uint32_t type = 1; type < world.unit_def_count; ++type) {
        const auto held =
            field_text(world.unit_defs[type].unit_name, sizeof world.unit_defs[type].unit_name);
        if (held.size() == name.size() &&
            std::equal(held.begin(), held.end(), name.begin(), [](char a, char b) {
                return std::tolower(static_cast<unsigned char>(a)) ==
                       std::tolower(static_cast<unsigned char>(b));
            }))
            return static_cast<uint16_t>(type);
    }
    return 0;
}

/// Hashes which units a world holds: slot, type and owner by player id (FNV-1a).
///
/// @param world match world
/// @return the hash
uint64_t unit_roster(const World& world) {
    constexpr uint64_t kFnvOffsetBasis = 0xcbf29ce484222325ull;
    constexpr uint64_t kFnvPrime = 0x100000001b3ull;
    uint64_t hash = kFnvOffsetBasis;
    const auto mix = [&hash](uint64_t value) {
        hash ^= value;
        hash *= kFnvPrime;
    };
    for (uint32_t slot = 1; slot < world.unit_slot_count; ++slot) {
        const auto& unit = world.units[slot];
        if (!unit_live(unit))
            continue;
        mix(slot);
        mix(unit.type_index);
        mix(unit.owner_index < OA_PLAYER_COUNT ? world.game.players[unit.owner_index].player_id
                                               : 0u);
    }
    return hash;
}

/// Counts the shots a player's units fired this tick.
///
/// @param world match world
/// @param owner player slot
/// @return shots made at the world's current tick
uint32_t shots_fired(const World& world, uint8_t owner) {
    uint32_t shots = 0;
    const auto live = std::clamp<int32_t>(world.game.projectile_count, 0, OA_PROJECTILE_CAPACITY);
    for (int32_t i = 0; i < live; ++i) {
        const auto& shot = world.projectiles[i];
        const auto source = oa::oa_unit_slot_from_ref(shot.source);
        if (shot.created_tick == world.game.tick && source != 0 && source < world.unit_slot_count &&
            world.units[source].owner_index == owner)
            ++shots;
    }
    return shots;
}

/// Marks the unit types the battle room's verdicts keep and gives them their agreed build limits.
///
/// @param context the battle room's mp::UnitSync
/// @param[in,out] headers unit headers, index 0 empty
/// @param count number of headers
void mark_agreed_units(void* context, UnitDef* headers, uint32_t count) {
    mp::unit_sync_mark_units(*static_cast<const mp::UnitSync*>(context), headers, count);
}

struct LoopbackSide {
    NetworkPlay* play{};
    const char* nickname{};
    std::unique_ptr<Game> game = std::make_unique<Game>();
    std::unique_ptr<mp::Lobby> lobby = std::make_unique<mp::Lobby>();
    std::unique_ptr<mp::ConnectState> connect = std::make_unique<mp::ConnectState>();
    mp::LobbyNet net{};
};

} // namespace

// C-style callbacks handed to the multiplayer screens and the net match, and
// the launch steps they drive.
struct NetworkPlay::NetHost {
    static NetworkPlay& self(void* context) { return *static_cast<NetworkPlay*>(context); }

    static void on_start(void* context, mp::Lobby& lobby) { launch(self(context), lobby); }

    // A chat line another player sent is posted as their chat, with their
    // logo and the arrival sound, through the console's host, which the
    // engine fills as the match starts.
    static void chat(void* context, uint8_t sender, const char* text) {
        const auto* host = self(context).net_->console_host;
        if (host != nullptr && host->post_message != nullptr)
            host->post_message(host->context, text, oa::sim::messages::kind_player_chat, sender);
    }

    static void notice(void* context, const char* text) {
        self(context).runtime_.console_post_message(text);
    }

    static int32_t rand15(void*) { return std::rand() & 0x7fff; }

    // Services the other in-process machine while this one waits on a reply.
    static void pump_peer(void* context, uint32_t wait_ms) {
        auto* peer = static_cast<NetState*>(context);
        if (peer->connection.opened)
            oa::netgame::sock::host_pump(peer->connection.host, wait_ms);
    }

    // The binding's hooks, reached through the runtime's session: its
    // chat, notice and random hooks take network play's state as their context.
    static nm::NetMatchHooks hooks(NetworkPlay& play) {
        nm::NetMatchHooks hooks{};
        hooks.context = &play;
        hooks.chat = chat;
        hooks.notice = notice;
        hooks.rand15 = rand15;
        hooks.credit = [](void*, World* world, uint8_t to, bool metal, float amount) {
            nm::credit_player_resource(world, to, metal, amount);
        };
        hooks.debit = [](void*, World* world, uint8_t from, bool metal, float amount) {
            return nm::debit_player_resource(world, from, metal, amount);
        };
        hooks.destroy_player_units = [](void* context, World*, uint8_t slot) {
            nm::match_binding_destroy_player_units(&self(context).net_->binding, slot);
        };
        hooks.end_local_game = [](void* context) {
            nm::match_binding_end_local_game(&self(context).net_->binding);
        };
        hooks.local_player_won = [](void* context) {
            return nm::match_binding_local_player_won(&self(context).net_->binding);
        };
        hooks.alliance_changed = [](void* context, World*, uint8_t slot) {
            nm::match_binding_follow_alliances(&self(context).net_->binding, slot);
        };
        hooks.share_sight = [](void* context, World*, uint8_t from, uint8_t to) {
            nm::match_binding_share_sight(&self(context).net_->binding, from, to);
        };
        return hooks;
    }

    // The battle room hands over its lobby on the host's START or on the
    // host's 0x08 at a client. The host queues its 0x08 as the battle room
    // closes; the loading screen's first frame sends it, so clients load
    // alongside the host. While the world is built no frame runs: the
    // engine's load_progress hook keeps the connection alive meanwhile.
    static void launch(NetworkPlay& play, mp::Lobby& lobby) {
        auto& state = *play.net_;
        if (!state.connected || !state.connection.in_session || lobby.game == nullptr ||
            lobby.game->local_player_index >= OA_PLAYER_COUNT) {
            abort(play, "the battle room has no live session");
            return;
        }
        const auto local_slot = lobby.game->local_player_index;
        const auto host_slot = mp::lobby_host_slot(lobby);
        const auto* host_info =
            host_slot != mp::kNoSlot ? mp::slot_info(lobby, host_slot) : nullptr;
        if (host_info == nullptr) {
            abort(play, "the battle room has no host");
            return;
        }
        state.hosting = host_slot == local_slot;
        if (state.hosting)
            nm::net_match_queue_game_start(&state.connection, *lobby.game);
        state.building_game = lobby.game;
        state.loading_pace = {};
        state.load_rows = {};
        try {
            build(play, lobby, *host_info, local_slot);
        } catch (const std::exception& error) {
            abort(play, error.what());
        }
        state.building_game = nullptr;
    }

    /// Builds the network match from the battle room and starts it loading.
    ///
    /// Selects the host's map, fills the skirmish slots from the battle
    /// room's players and loads an empty world at the host's unit limit. The
    /// battle room's unit-sync verdicts decide which unit types load and
    /// their limits on every machine alike, and the verdicts are dropped
    /// afterwards. The launch then applies the lobby's slots and alliances,
    /// starts the score report and binds the match to the session. Throws
    /// std::runtime_error when the map is not installed, has too few start
    /// positions, or the lobby holds no local slot.
    ///
    /// @param play network play's state for the runtime that loads the match
    /// @param lobby battle room the match starts from; its unit-sync table is dropped
    /// @param host_info the host's setup block, which names the map and the unit limit
    /// @param local_slot this machine's player slot
    static void build(
        NetworkPlay& play,
        mp::Lobby& lobby,
        const mp::PlayerSetupInfo& host_info,
        uint8_t local_slot
    ) {
        Runtime& runtime = play.runtime_;
        auto& state = *play.net_;
        const std::string map(field_text(host_info.map_name, sizeof host_info.map_name));
        if (!play.select_map_named(map))
            throw std::runtime_error("the multiplayer map is not installed: " + map);
        // Watchers take no start position.
        uint16_t live = 0;
        for (int32_t slot = 0; slot < mp::kSlotCount; ++slot) {
            const auto* info = mp::slot_info(lobby, slot);
            const bool watching = info != nullptr && (info->options & OA_SETUP_OPTION_WATCHER) != 0;
            live += slot_live(mp::slot_player(lobby, slot)) && !watching ? 1 : 0;
        }
        // The start positions come from the schema for the host's slot table.
        runtime.state_.player_count =
            static_cast<uint16_t>(nm::match_layout_player_count(*lobby.game));
        if (runtime.map_player_capacity() < static_cast<int32_t>(live))
            throw std::runtime_error("the map has too few start positions for the lobby");
        const auto* local_info = mp::slot_info(lobby, local_slot);
        const bool local_watcher =
            local_info != nullptr && (local_info->options & OA_SETUP_OPTION_WATCHER) != 0;
        // The shared skirmish bootstrap builds the world for the local
        // viewpoint with the lobby's teams. Remote humans are entered as
        // computer slots so the bootstrap never takes one of them as local;
        // match_launch_apply restores every status afterwards.
        for (int32_t slot = 0; slot < mp::kSlotCount; ++slot) {
            auto& player = mp::slot_player(lobby, slot);
            const auto* info = mp::slot_info(lobby, slot);
            auto& target = runtime.skirmish_settings_.slots[static_cast<std::size_t>(slot)];
            target = {};
            target.controller = slot == local_slot  ? entry::controller::human
                                : slot_live(player) ? entry::controller::computer
                                                    : entry::controller::disabled;
            target.alliance = mp::lobby_player_team(player);
            if (info != nullptr) {
                target.side = info->side;
                target.color = info->color;
                target.metal = info->metal_hundreds * 100;
                target.energy = info->energy_hundreds * 100;
            }
        }
        // The world starts empty at the host's unit limit; the local commander
        // is placed on the start position the load barrier assigns.
        const uint16_t limit =
            host_info.max_units != 0 ? host_info.max_units : kSkirmishUnitsPerPlayer;
        // The battle room's unit verdicts decide which unit types the match
        // loads and their build limits, on every machine alike; the table is
        // then dropped, so the match handles no 0x1a. A battle room that
        // never built its table keeps every type. The player slot tick never
        // tests a watcher for defeat.
        const bool verdicts = lobby.sync.record_count > 0;
        runtime.bootstrap_match(
            {.units_per_player = limit,
             .place_commanders = false,
             .defeat_allowed = !local_watcher,
             .unit_filter = {&lobby.sync, verdicts ? mark_agreed_units : nullptr},
             .multiplayer = true}
        );
        mp::unit_sync_destroy(lobby);
        if (!runtime.match_ || runtime.altitude_sight_blocked_)
            throw std::runtime_error(
                "the multiplayer bootstrap did not leave an empty match world"
            );
        // The host's START plays its computer players at hard
        // difficulty, after the preferences are saved.
        if (state.hosting)
            runtime.match_->set_difficulty(OA_DIFFICULTY_HARD);

        auto& world = runtime.match_->state();
        if (!nm::match_launch_apply(lobby, &world))
            throw std::runtime_error("the lobby has no local slot");
        play.start_reporter(world, lobby.game);
        if (state.hosting)
            world.game.session_flags =
                static_cast<uint8_t>(world.game.session_flags | nm::kNetFlagGameStarted);
        nm::match_binding_init(&state.binding, runtime.match_.get(), state.net.get());
        nm::net_match_begin(
            state.net.get(),
            &state.connection,
            &world,
            nm::match_binding_sim(&state.binding),
            hooks(play)
        );
        nm::match_binding_install(&state.binding);
        for (uint8_t slot = 0; slot < OA_PLAYER_COUNT; ++slot) {
            const auto& player = world.game.players[slot];
            if (player.in_use == 0)
                continue;
            std::array<uint8_t, 10> allies{};
            for (std::size_t other = 0; other < allies.size(); ++other)
                allies[other] = other == slot || player.alliance[other] != 0 ? 1 : 0;
            runtime.match_->configure_player_alliances(slot, allies);
        }
        runtime.match_local_player_ = local_slot;
        runtime.offline_services_.set_viewpoint(local_slot);
        nm::net_match_loader_waiting(state.net.get());
        state.local_slot = local_slot;
        state.loading = true;
        state.active = true;
        state.pause_seen = false;
        state.finish_pending = false;
        state.stalled_player = nm::no_player_id;
        runtime.status_ = "The other players are still loading";
    }

    // One loading-screen frame, at most every 200 ms as the loading screen
    // paces it; true once the start positions are final and the other
    // machines have had time to pass their own barriers, so the commanders
    // can be placed (nm::commander_wait_frames). Each frame before the
    // barrier passes also reports the local players' load progress from the
    // rows the engine last reported.
    static bool loading_frame(NetworkPlay& play) {
        auto& state = *play.net_;
        const bool ready = nm::net_match_paced_loading_frame(
            state.net.get(), &state.loading_pace, state.load_rows.data()
        );
        watch_timeouts(play);
        return ready;
    }

    /// Places the local commanders once the load barrier passed, announces them and enters the
    /// match.
    ///
    /// It runs nm::commander_wait_frames loading frames after the barrier
    /// passed, so the other machines take the commanders' 0x09 and their
    /// copies stand on the start positions from it.
    ///
    /// Every slot simulated here (local or computer) that plays gets its
    /// commander on the start position the barrier assigned, with the host's
    /// starting resources; a watcher gets none, sees the whole map (mapping and
    /// line of sight off) and has its camera at half the view. The sight grids
    /// are rebuilt once the commanders stand. Loading then ends as the loader
    /// ends it (net_match_end_loading): the local player's block says the game
    /// has started and every local player's block and team go out, the host
    /// publishes its session again, and one economy period is counted. Throws
    /// std::runtime_error when the match has no host block, a local player has
    /// no start position or a commander cannot be placed.
    ///
    /// @param play network play's state for the runtime whose network match loaded
    static void finish(NetworkPlay& play) {
        Runtime& runtime = play.runtime_;
        auto& state = *play.net_;
        auto& world = runtime.match_->state();
        const auto* host = nm::launch_host_info(&world);
        if (host == nullptr)
            throw std::runtime_error("the match has no host block");
        std::vector<std::pair<uint8_t, uint8_t>> starts;
        for (uint8_t slot = 0; slot < OA_PLAYER_COUNT; ++slot) {
            const auto& player = world.game.players[slot];
            const bool simulated_here = player.status == OA_PLAYER_STATUS_LOCAL ||
                                        player.status == OA_PLAYER_STATUS_COMPUTER;
            if (player.in_use == 0 || !simulated_here || nm::match_slot_watcher(&world, slot))
                continue;
            const auto start = nm::net_match_start_position(state.net.get(), slot);
            if (start == nm::no_start_position)
                throw std::runtime_error("no start position was assigned to a local player");
            starts.emplace_back(slot, start);
        }
        nm::net_match_enter_game(state.net.get());
        for (const auto& [slot, start] : starts) {
            const auto& info = world.player_info[slot];
            const oa::sim::unit_spawn::PlayerSetup setup{
                info.side, info.color, host->metal_hundreds * 100, host->energy_hundreds * 100
            };
            const auto started = runtime.match_->start_player(
                slot,
                setup,
                runtime.selected_start_markers_,
                start,
                kBattlefieldWidth,
                kBattlefieldHeight,
                runtime
            );
            if (!started.position_found || started.unit == nullptr)
                throw std::runtime_error("a local commander could not be placed");
        }
        // Every store starts at the host's resources.
        oa::ui::hud::set_starting_resources(
            world, oa::data::campaign::SessionKind::multiplayer, nullptr, nullptr
        );
        nm::match_apply_watcher_view(&world);
        if (nm::match_slot_watcher(&world, state.local_slot)) {
            runtime.match_camera_x_ = kBattlefieldWidth / 2;
            runtime.match_camera_z_ = kBattlefieldHeight / 2;
            runtime.match_camera_flags_ = 0;
        }
        runtime.reset_match_sight(true);
        nm::net_match_end_loading(state.net.get());
        nm::net_match_set_speed(state.net.get(), runtime.preferences_.current_game_speed, false);
        state.speed_seen = runtime.preferences_.current_game_speed;
        state.loading = false;
        play.report_game_event(oa::app::netgame::extension_api::report_event::game_started);
        runtime.enter_match_view();
    }

    static void abort(NetworkPlay& play, std::string_view reason) {
        Runtime& runtime = play.runtime_;
        runtime.status_ = "Multiplayer start failed: " + std::string(reason);
        std::cerr << "multiplayer: " << reason << '\n';
        play.net_leave();
        if (runtime.match_)
            runtime.leave_match();
        runtime.pending_screen_ = screen_id(Screen::main_menu);
    }

    static void watch_timeouts(NetworkPlay& play) {
        Runtime& runtime = play.runtime_;
        auto& state = *play.net_;
        const auto stalled = state.net->timeout_player;
        if (stalled != state.stalled_player) {
            state.stalled_player = stalled;
            if (stalled != nm::no_player_id)
                runtime.status_ = "Waiting for " + player_name(*state.net->world, stalled) + "...";
        }
        if (stalled != nm::no_player_id && nm::net_match_timeout_expired(state.net.get(), stalled))
            state.stalled_player = nm::no_player_id;
    }

    // Loopback check: ephemeral ports on 127.0.0.1, the peer serviced while
    // this machine waits.
    static bool loopback_configure(NetworkPlay& play, uint32_t seed, NetState* peer) {
        auto& state = *play.net_;
        if (state.connected)
            nm::net_connection_destroy(&state.connection);
        nm::SessionLobbyConfig config{};
        const uint8_t loopback[4] = {127, 0, 0, 1};
        std::memcpy(config.host.bind_ip, loopback, sizeof loopback);
        std::memcpy(config.host.enum_target, loopback, sizeof loopback);
        config.host.stream_port_first = 0;
        config.host.datagram_port_first = 0;
        config.host.enum_port = 0;
        config.host.seed = seed;
        state.connected = nm::net_connection_create(&state.connection, config);
        if (state.connected) {
            state.connection.pump_context = peer;
            state.connection.pump_other = pump_peer;
        }
        return state.connected;
    }

    static void loopback_pump(NetworkPlay& play, uint32_t wait_ms) {
        if (play.net_->connection.opened)
            oa::netgame::sock::host_pump(play.net_->connection.host, wait_ms);
    }

    // A networked tick pushed out at once; the paced flush of real play
    // would hold records for the wall clock the check does not wait on.
    static void loopback_step(NetworkPlay& play) {
        auto& connection = play.net_->connection;
        play.net_simulation_step();
        nm::packet_layer_flush(connection.packets, nm::net_connection_time(&connection), true);
    }

    static UnitPose unit_pose(NetworkPlay& play, uint16_t unit) {
        Runtime& runtime = play.runtime_;
        const auto& record = runtime.match_->state().units[unit];
        const auto* ground = runtime.match_->ground_runtime(unit);
        return {
            record.position.x,
            record.position.y,
            record.position.z,
            record.heading,
            ground != nullptr ? ground->movement.speed : 0
        };
    }

    static uint16_t local_commander(NetworkPlay& play) {
        Runtime& runtime = play.runtime_;
        const auto& world = runtime.match_->state();
        for (uint32_t slot = 1; slot < world.unit_slot_count; ++slot) {
            const auto& unit = world.units[slot];
            if (unit.type_index != 0 && unit.owner_index == runtime.match_local_player_)
                return static_cast<uint16_t>(slot);
        }
        return 0;
    }

    /// A unit as its owner's machine held it at one tick.
    struct OwnerPose {
        UnitPose pose{};
        uint16_t type{}; ///< UnitDef index; 0 for an empty slot
        bool finished{}; ///< Unit.build_remaining was 0
    };

    /// Returns a unit's position, heading and ground speed in a match.
    ///
    /// @param match match holding the unit
    /// @param unit unit slot
    /// @return the pose; speed 0 for a unit without a ground driver
    static UnitPose match_pose(const sim::match_runtime::Match& match, uint16_t unit) {
        const auto& record = match.state().units[unit];
        const auto* ground = match.ground_runtime(unit);
        return {
            record.position.x,
            record.position.y,
            record.position.z,
            record.heading,
            ground != nullptr ? ground->movement.speed : 0
        };
    }

    /// Plays the loopback match with the computer player the host seated.
    ///
    /// The computer player plays on the host's machine at hard difficulty;
    /// the joiner holds its slot as simulated elsewhere, in the host's
    /// machine group, and copies its units from the records its player
    /// sends. It builds its base for `build_ticks`. The host's player then
    /// gives it a squad of kbots on the host's machine, and the joiner's
    /// player gives it one more across the session. With the squad its land
    /// strike sets out against its enemy's commander (the joiner's; the
    /// host's when the joiner watches) and fights until its units have fired,
    /// killed, and lost two; whatever is left of that commander then
    /// self-destructs, and its player watches. When the joiner played, the
    /// host's side has won: its final economy settles, the computer
    /// player's among it. The host then leaves, and the joiner retires it
    /// and the computer player, keeping their final figures.
    ///
    /// Throughout, the joiner's copy of every finished unit the computer
    /// player moves stays within a pixel of where its owner had it at the
    /// tick of the last 0x2c the joiner applied from the computer player;
    /// after each part of the match both machines hold the same units, the
    /// computer player's shots reach the joiner one for one, and the joiner
    /// counts the computer player's kills and losses as the host does.
    /// Progress goes to stdout; a failure throws std::runtime_error.
    ///
    /// @param host network play's state for the hosting runtime, in its running match
    /// @param joiner network play's state for the joining runtime, in its running match
    /// @param build_ticks ticks the computer player builds before the strike
    /// @param computer_id session id of the computer player
    /// @param watching whether the joiner only watches
    static void computer_match(
        NetworkPlay& host,
        NetworkPlay& joiner,
        std::size_t build_ticks,
        uint32_t computer_id,
        bool watching
    ) {
        const auto require = [](bool ok, const char* what) {
            if (!ok)
                throw std::runtime_error(std::string("net loopback check: ") + what);
        };
        auto& host_world = *host.net_->net->world;
        auto& client_world = *joiner.net_->net->world;
        auto& host_match = *host.net_->binding.match;
        auto& client_match = *joiner.net_->binding.match;
        const auto host_local = host_world.game.local_player_index;
        const auto client_local = client_world.game.local_player_index;
        const auto host_id = host_world.game.players[host_local].player_id;
        const auto client_id = client_world.game.players[client_local].player_id;
        const auto host_ai = player_slot(host_world, computer_id);
        const auto client_ai = player_slot(client_world, computer_id);
        const auto host_on_client = player_slot(client_world, host_id);
        const auto client_on_host = player_slot(host_world, client_id);
        require(
            host_ai < OA_PLAYER_COUNT && client_ai < OA_PLAYER_COUNT &&
                host_on_client < OA_PLAYER_COUNT && client_on_host < OA_PLAYER_COUNT,
            "a machine has no slot for a player"
        );
        const auto& computer_here = host_world.game.players[host_ai];
        const auto& computer_copy = client_world.game.players[client_ai];
        require(
            computer_here.status == OA_PLAYER_STATUS_COMPUTER &&
                computer_copy.status == OA_PLAYER_STATUS_MIRRORED &&
                computer_copy.machine_group != 0 &&
                computer_copy.machine_group ==
                    client_world.game.players[host_on_client].machine_group,
            "the computer player is not played on the host's machine alone"
        );
        require(
            host_world.game.difficulty == OA_DIFFICULTY_HARD,
            "the host does not play its computer player at hard difficulty"
        );
        const bool allied =
            computer_here.alliance[host_local] != 0 && computer_copy.alliance[host_on_client] != 0;
        require(allied != watching, "the computer player's team did not reach the match");

        // The owner's pose of every unit slot at each of the last ticks.
        std::vector<std::vector<OwnerPose>> owner_poses(
            kComputerPoseTicks, std::vector<OwnerPose>(host_world.unit_slot_count)
        );
        uint32_t copies_compared = 0;
        uint32_t copies_exact = 0;
        uint32_t copies_worst_offset = 0;
        uint32_t host_shots = 0;
        uint32_t client_shots = 0;
        const auto note_owner = [&] {
            auto& poses = owner_poses[host_world.game.tick % kComputerPoseTicks];
            for (uint16_t slot = 1; slot < host_world.unit_slot_count; ++slot) {
                const auto& unit = host_world.units[slot];
                poses[slot] =
                    unit_live(unit)
                        ? OwnerPose{match_pose(host_match, slot), unit.type_index, unit.build_remaining == 0.0F}
                        : OwnerPose{};
            }
            host_shots += shots_fired(host_world, host_ai);
        };
        // A copy moved from records matches its owner at the tick of the
        // record the joiner last applied from the computer player. A unit
        // still on its factory's pad turns with the factory's script, which
        // runs a record later on the joiner, so a copy that leaves the pad
        // may head a turn step behind its owner until the unit's next full
        // record; it stays within a pixel meanwhile. Units still being built
        // are not compared.
        const auto compare_copies = [&] {
            client_shots += shots_fired(client_world, client_ai);
            const auto tick = computer_copy.last_sim_tick;
            if (tick <= 0 || static_cast<uint32_t>(tick) > host_world.game.tick ||
                host_world.game.tick - static_cast<uint32_t>(tick) >= kComputerPoseTicks)
                return;
            const auto& poses = owner_poses[static_cast<uint32_t>(tick) % kComputerPoseTicks];
            for (uint16_t slot = 1; slot < client_world.unit_slot_count; ++slot) {
                const auto& unit = client_world.units[slot];
                const auto& owner = poses[slot];
                if (!unit_live(unit) || unit.owner_index != client_ai ||
                    unit.build_remaining != 0.0F || owner.type != unit.type_index ||
                    !owner.finished || client_match.ground_runtime(slot) == nullptr)
                    continue;
                const auto copy = match_pose(client_match, slot);
                ++copies_compared;
                copies_exact += pose_divergence(copy, owner.pose) == 0 ? 1 : 0;
                const UnitPose ground{
                    copy.x, owner.pose.y, copy.z, owner.pose.heading, owner.pose.speed
                };
                copies_worst_offset =
                    std::max(copies_worst_offset, pose_divergence(ground, owner.pose));
            }
        };
        const auto play_tick = [&] {
            host.net_frame();
            joiner.net_frame();
            loopback_step(host);
            note_owner();
            loopback_step(joiner);
            compare_copies();
            loopback_pump(host, kTickPumpWaitMs);
            loopback_pump(joiner, kTickPumpWaitMs);
        };
        // Units are created and die on the joiner a few ticks after the host;
        // both hold the same ones once none is on its way.
        const auto settle_limit = 3u * host_world.game.units_per_player;
        const auto settle = [&](const char* what) {
            for (uint32_t extra = 0; extra < settle_limit; ++extra) {
                play_tick();
                if (unit_roster(host_world) == unit_roster(client_world))
                    return;
            }
            require(false, what);
        };
        const auto score_line = [&] {
            std::cout << "kills " << computer_here.kills << " / " << computer_copy.kills
                      << ", losses " << computer_here.losses << " / " << computer_copy.losses
                      << ", shots " << host_shots << " / " << client_shots;
        };

        // The computer player builds its base. Its commander stands on the
        // joiner from its 0x09 on.
        for (std::size_t tick = 0; tick < build_ticks; ++tick)
            play_tick();
        settle("the joiner does not hold the computer player's base");
        const auto built = live_units(host_world, host_ai);
        require(built >= kComputerBuiltUnits, "the computer player built too little");
        require(
            commander_of(client_world, client_ai) != 0 &&
                commander_of(client_world, client_ai) == commander_of(host_world, host_ai),
            "the joiner has no copy of the computer player's commander"
        );
        std::cout << "net loopback check: the computer player built " << built - 1
                  << " units by tick " << host_world.game.tick << "; both machines hold them\n";

        // A finished unit for a player, standing on its own machine beside
        // another unit, as its factory would have made it.
        const auto make_unit = [&](sim::match_runtime::Match& match,
                                   World& world,
                                   uint8_t player,
                                   uint16_t type,
                                   uint16_t beside,
                                   int32_t aside) -> uint16_t {
            const auto& at = world.units[beside];
            const int32_t width = static_cast<int32_t>(world.game.map_pixel_width) << 16;
            const int32_t offset =
                at.position.x + (aside << 16) < width ? aside << 16 : -(aside << 16);
            oa::sim::unit_spawn::Request request{};
            request.player = player;
            request.type = type;
            request.position = {
                static_cast<uint32_t>(at.position.x + offset),
                static_cast<uint32_t>(at.position.y),
                static_cast<uint32_t>(at.position.z)
            };
            request.finished = true;
            request.state = 1;
            auto* made = match.create(request);
            return made != nullptr && made->unit != nullptr
                       ? static_cast<uint16_t>(made->unit_index)
                       : 0;
        };
        // A player makes a unit beside its commander and gives it to the
        // computer player; the giver's machine hands it over.
        const auto give = [&](sim::match_runtime::Match& giver_match,
                              World& giver_world,
                              uint8_t giver,
                              uint8_t recipient,
                              const char* type_name,
                              int32_t aside) {
            const auto type = unit_type_named(giver_world, type_name);
            const auto commander = commander_of(giver_world, giver);
            require(type != 0 && commander != 0, "a giver has no commander or unit type to give");
            const auto gift = make_unit(giver_match, giver_world, giver, type, commander, aside);
            require(gift != 0, "a giver could not make the unit to give");
            for (int frame = 0; frame < 4; ++frame)
                play_tick();
            require(
                unit_live(host_world.units[gift]) && unit_live(client_world.units[gift]),
                "a machine has no copy of the unit to be given"
            );
            const auto held_before = units_of_type(host_world, host_ai, type);
            giver_match.transfer_unit(gift, recipient);
            settle("the machines disagree on a given unit");
            require(
                !unit_live(host_world.units[gift]) && !unit_live(client_world.units[gift]) &&
                    units_of_type(host_world, host_ai, type) == held_before + 1 &&
                    units_of_type(client_world, client_ai, type) == held_before + 1,
                "the computer player was not given the unit on both machines"
            );
        };
        // The host's player gives the computer player a squad of Arm kbots on
        // the host's machine: each new unit of the computer player reaches the
        // joiner from its player, each old one dies on both. The joiner's
        // player gives it one more across the session (0x14): the host makes
        // the unit for its computer player. The computer player, playing
        // Core, builds neither type itself.
        for (int32_t gift = 0; gift < kComputerSquadGifts; ++gift)
            give(
                host_match,
                host_world,
                host_local,
                host_ai,
                kComputerSquadType,
                kComputerGiftAside + gift * kComputerGiftSpacing
            );
        if (!watching)
            give(
                client_match,
                client_world,
                client_local,
                client_ai,
                kComputerAcrossType,
                kComputerGiftAside
            );
        std::cout << "net loopback check: the host's player gave the computer player "
                  << kComputerSquadGifts << " x " << kComputerSquadType
                  << (watching ? "" : ", the joiner's player one ")
                  << (watching ? "" : kComputerAcrossType)
                  << "; both machines hold them as the computer player's\n";

        // With its squad the computer player strikes the nearest enemy unit:
        // its enemy's commander (the joiner's; the host's when the joiner
        // watches), which fights back. The strike runs until the computer
        // player's units have fired, killed and lost units.
        const auto* computer_players = sim::ai::match_computer_players(host_match);
        const auto strike = std::find_if(
            std::begin(computer_players->players[host_ai].tasks),
            std::end(computer_players->players[host_ai].tasks),
            [](const sim::ai::ComputerTask& task) {
                return task.kind == sim::ai::TaskKind::strike &&
                       task.squad == sim::ai::Squad::land_strike;
            }
        );
        require(
            strike != std::end(computer_players->players[host_ai].tasks),
            "the computer player has no land strike"
        );
        auto& enemy_world = watching ? host_world : client_world;
        const auto enemy_slot = enemy_world.game.local_player_index;
        const auto enemy_commander = commander_of(enemy_world, enemy_slot);
        require(enemy_commander != 0, "the computer player's enemy has no commander");
        const auto enemy_health = enemy_world.units[enemy_commander].health;
        const auto kills_before = computer_here.kills;
        const auto losses_before = computer_here.losses;
        bool struck = false;
        const auto fought = [&] {
            return struck && host_shots != 0 && computer_here.kills > kills_before &&
                   computer_here.losses - losses_before >= kComputerFightLosses;
        };
        uint32_t fight_ticks = 0;
        for (; fight_ticks < kComputerFightTicks && !fought(); ++fight_ticks) {
            play_tick();
            struck = struck || strike->attacking != 0;
        }
        std::cout << "net loopback check: the computer player struck and fought for " << fight_ticks
                  << " ticks: ";
        score_line();
        std::cout << "\n";
        require(struck, "the computer player did not strike");
        require(
            host_shots != 0 && computer_here.kills > kills_before,
            "the computer player's units killed nothing"
        );
        require(
            computer_here.losses - losses_before >= kComputerFightLosses,
            "the computer player lost too little in the fight"
        );
        require(
            !unit_live(enemy_world.units[enemy_commander]) ||
                enemy_world.units[enemy_commander].health < enemy_health,
            "the computer player did the enemy commander no harm"
        );

        // Whatever of its enemy's commander is left self-destructs; its player
        // watches once its defeat countdown runs out, and no one fights any
        // more.
        if (unit_live(enemy_world.units[enemy_commander]))
            (watching ? host_match : client_match)
                .toggle_self_destruct(std::span<const uint16_t>(&enemy_commander, 1));
        const auto enemy_id = enemy_world.game.players[enemy_slot].player_id;
        const auto defeated = [&] {
            return watches(info_for_id(client_world, enemy_id)) &&
                   watches(info_for_id(host_world, enemy_id));
        };
        for (uint32_t tick = 0; tick < kLoopbackDefeatTicks && !defeated(); ++tick)
            play_tick();
        require(defeated(), "the computer player's enemy was not defeated to watching");
        settle("the machines disagree on the units after the fight");
        std::cout << "net loopback check: tick " << host_world.game.tick << " / "
                  << client_world.game.tick << ", computer player's units "
                  << live_units(host_world, host_ai) << " / " << live_units(client_world, client_ai)
                  << ", ";
        score_line();
        std::cout << ", copies compared " << copies_compared << ", exact " << copies_exact
                  << ", farthest off " << copies_worst_offset << " (16.16)\n";
        require(
            client_shots == host_shots, "the computer player's shots did not all reach the joiner"
        );
        require(
            computer_copy.kills == computer_here.kills &&
                computer_copy.losses == computer_here.losses,
            "the joiner counts the computer player's kills or losses otherwise"
        );
        require(
            copies_compared != 0 && copies_worst_offset < kCopyOffsetLimit,
            "a copy left the computer player's unit's path"
        );
        require(
            host.net_->net->record_errors == 0 && joiner.net_->net->record_errors == 0 &&
                joiner.net_->binding.refused_creates == 0,
            "record errors or refused creates"
        );

        // The host's side won against the joiner: its final economy, the
        // computer player's among it, settles before it leaves. Until then
        // the joiner holds no economy figures for the computer player, which
        // sends no periodic 0x28 (only the viewpoint player does).
        if (!watching) {
            // The host's victory comes at one of its 30-tick outcome checks
            // after its enemy watches; play on until the outcome is decided.
            for (uint32_t tick = 0; tick < kLoopbackVictoryTicks &&
                                    host_match.outcome() == sim::scenario::Outcome::ongoing;
                 ++tick)
                play_tick();
            require(
                host_match.outcome() == sim::scenario::Outcome::victory,
                "the host's side did not win"
            );
            std::cout << "net loopback check: the host's side won at tick " << host_world.game.tick
                      << "\n";
            require(
                computer_copy.energy_produced_total == 0.0 &&
                    computer_copy.metal_produced_total == 0.0,
                "the joiner had economy figures for the computer player before its final economy"
            );
            const auto started = std::chrono::steady_clock::now();
            while (!host.net_final_economy_settled()) {
                const auto waited = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - started
                );
                require(
                    waited.count() < kLoopbackWaitMs, "the host's final economy did not settle"
                );
                joiner.net_frame();
                loopback_step(joiner);
                loopback_pump(joiner, 1);
            }
        }
        // The host leaves; the joiner retires its machine group, the host's
        // player and its computer player, as departed.
        host.net_leave();
        const auto started = std::chrono::steady_clock::now();
        while (computer_copy.in_use != 0 || client_world.game.players[host_on_client].in_use != 0) {
            const auto waited = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started
            );
            require(
                waited.count() < kLoopbackWaitMs, "the joiner did not retire the host's machine"
            );
            joiner.net_frame();
            loopback_step(joiner);
            loopback_pump(joiner, 1);
            loopback_pump(host, 1);
        }
        std::cout
            << "net loopback check: the host left; the joiner retired it and its computer player"
            << " with reasons "
            << static_cast<int>(client_world.game.players[host_on_client].reject_reason) << " / "
            << static_cast<int>(computer_copy.reject_reason) << "; the computer player's figures "
            << computer_here.kills << "/" << computer_here.losses << " kills/losses, "
            << computer_here.energy_produced_total << " energy and "
            << computer_here.metal_produced_total << " metal produced; the joiner's copy "
            << computer_copy.kills << "/" << computer_copy.losses << ", "
            << computer_copy.energy_produced_total << ", " << computer_copy.metal_produced_total
            << "\n";
        require(
            computer_copy.reject_reason == 1 &&
                client_world.game.players[host_on_client].reject_reason == 1,
            "the joiner did not retire the host's machine as departed"
        );
        if (!watching)
            require(
                computer_copy.kills == computer_here.kills &&
                    computer_copy.losses == computer_here.losses &&
                    static_cast<float>(computer_copy.energy_produced_total) ==
                        static_cast<float>(computer_here.energy_produced_total) &&
                    static_cast<float>(computer_copy.metal_produced_total) ==
                        static_cast<float>(computer_here.metal_produced_total),
                "the joiner's figures for the computer player differ from its final ones"
            );
        joiner.net_leave();
    }
};

void NetworkPlay::destroy_net_state(NetState* state) noexcept {
    if (state != nullptr && state->connected)
        nm::net_connection_destroy(&state->connection);
    delete state;
}

void NetworkPlay::net_bind_multiplayer() {
    release_match_report();
    if (!net_) {
        net_.reset(new NetState());
        nm::SessionLobbyConfig config{};
        if (net_launch_switches().send_pacing_set != 0)
            config.sends_per_second = net_launch_switches().send_pacing;
        if (net_options().check_host_not_found)
            config.host.clock = host_not_found_clock();
        if (const auto port = net_options().dplay_port) {
            config.host.enum_port = *port;
            config.host.stream_port_first = 0;
            config.host.datagram_port_first = 0;
        }
        net_->connected = nm::net_connection_create(&net_->connection, config);
        if (!net_->connected)
            std::cerr << "multiplayer: session storage is unavailable\n";
    }
    if (net_->connected)
        mp::multiplayer_bind_net(nm::session_lobby_net(&net_->connection));
    if (net_options().check_host_not_found)
        mp::multiplayer_bind_clock({nullptr, [](void*) { return host_not_found_clock_ms(); }});
    mp::multiplayer_bind_player_timeout(net_launch_switches().net_timeout_seconds);
    mp::multiplayer_bind_start(NetHost::on_start, this);
    mp::multiplayer_bind_translation(
        {&runtime_, [](void* context, std::string_view interface_text) {
             return static_cast<Runtime*>(context)->translate_ui(interface_text);
         }}
    );
}

// Per frame: the load barrier while loading; in the match a pause set
// elsewhere announced, a speed set elsewhere taken into the speed
// preferences and the match clock, the paused-frame pump while the pause bit
// is set, the peer ticks for the lag throttle and the remote-player timeout.
// The Pause key's 0x19 goes out as the key is pressed (the extension's
// pause_changed), and the speed keys' and the GAME slider's as they set the
// speed (speed_changed); an in-game menu sends nothing and holds nothing.
//
// A finished game holds on its outcome until its final economy settles
// (net_final_economy_settled). That hold is not a pause: no 0x19 goes out
// and the match clock keeps stepping it, so it keeps sending its per-tick
// records, and the other players play on. The timeout scan keeps running,
// so a player who stops answering is dropped and no longer holds the gate.
void NetworkPlay::net_frame() {
    if (!net_ || !net_->active)
        return;
    auto& state = *net_;
    if (state.loading) {
        try {
            if (NetHost::loading_frame(*this))
                NetHost::finish(*this);
        } catch (const std::exception& error) {
            NetHost::abort(*this, error.what());
        }
        return;
    }
    // A shared match goes on beneath the preferences its menu opens.
    if (!runtime_.match_running())
        return;
    auto& game = runtime_.match_->state().game;
    const bool sim_paused = (game.sim_run_flags & nm::run_flag_paused) != 0;
    if (sim_paused != state.pause_seen) {
        state.pause_seen = sim_paused;
        runtime_.status_ = sim_paused ? "Game paused" : "Game resumed";
    }
    // A speed set elsewhere: one another machine sent, or one this machine's
    // preferences put back (Cancel, UNDO, RESTORE), which stays here.
    if (game.requested_speed != state.speed_seen && game.requested_speed >= nm::min_game_speed &&
        game.requested_speed <= nm::max_game_speed) {
        runtime_.preferences_.current_game_speed = game.requested_speed;
        runtime_.match_timing_.actual_rate = game.requested_speed;
        runtime_.match_timing_.requested_rate = game.requested_speed;
        state.speed_seen = game.requested_speed;
    }
    if (sim_paused)
        nm::net_match_paused_frame(state.net.get());
    nm::net_match_sync_timing(&runtime_.match_->state(), &runtime_.match_timing_);
    NetHost::watch_timeouts(*this);
    step_reporter_frame();
}

bool NetworkPlay::net_match_active() const {
    return net_ && net_->active;
}

const NetState* NetworkPlay::loading_net_match() const {
    return net_ && net_->active && net_->loading ? net_.get() : nullptr;
}

const NetState* NetworkPlay::session_net_match() const {
    return net_ && net_->active ? net_.get() : nullptr;
}

bool NetworkPlay::net_local_watcher() const {
    return net_ && net_->active && runtime_.match_ &&
           nm::match_slot_watcher(
               &runtime_.match_->state(), runtime_.match_->state().game.local_player_index
           );
}

bool NetworkPlay::net_session_open() const {
    return net_ && net_->connected && net_->connection.in_session;
}

bool NetworkPlay::net_simulation_step() {
    if (!net_ || !net_->active || net_->loading || !runtime_.match_)
        return false;
    nm::match_binding_tick(&net_->binding);
    runtime_.match_timing_.tick = runtime_.match_->state().game.tick;
    return true;
}

// A finished network game holds on its frame until the final
// economy is settled, then tears the game down.
bool NetworkPlay::net_final_economy_settled() {
    if (!net_ || !net_->active || net_->loading || !runtime_.match_)
        return true;
    auto& state = *net_;
    (void)nm::net_match_pump(state.net.get());
    state.finish_pending = !nm::net_match_final_economy(state.net.get());
    return !state.finish_pending;
}

// The statistics live with the connection's packet queue; without one
// there is nothing to reset.
void NetworkPlay::net_reset_traffic_stats() {
    if (!net_ || !net_->connected || net_->connection.packets == nullptr)
        return;
    const uint32_t tick = runtime_.match_ ? runtime_.match_->state().game.tick : 0;
    oa::netgame::traffic_stats_reset(&net_->connection.packets->traffic, tick);
}

void NetworkPlay::net_send_player_status() {
    if (net_ && net_->active)
        nm::net_match_send_player_status(net_->net.get(), false);
}

// Leaving sends no record of its own: closing the session destroys the local
// players, and every other machine retires their slots as departed, with no
// chat line.
void NetworkPlay::net_leave() {
    if (!net_ || !net_->active)
        return;
    auto& state = *net_;
    if (runtime_.match_)
        nm::net_connection_finish(&state.connection, &runtime_.match_->state().game);
    state.active = false;
    state.loading = false;
    state.finish_pending = false;
    state.binding = {};
    *state.net = nm::NetMatch{};
    const auto net = nm::session_lobby_net(&state.connection);
    net.close(net.context);
    mp::multiplayer_reset();
}

// "Give" while a network match runs: net_match_give debits, credits and
// sends the resource record, as the share transfers' network branch does.
// Nothing is given while the match loads.
bool NetworkPlay::net_give(uint8_t from, uint8_t to, bool metal, float amount) {
    if (!net_ || !net_->active)
        return false;
    if (!net_->loading)
        nm::net_match_give(net_->net.get(), from, to, metal, amount);
    return true;
}

// The text message send as the chat line calls it: the formatted line to every player.
void NetworkPlay::net_send_chat(const char* line) {
    if (!net_ || !net_->active || net_->loading || line == nullptr || *line == '\0')
        return;
    nm::net_match_say(net_->net.get(), line);
}

void NetworkPlay::net_say(std::string_view text) {
    if (!net_ || !net_->active || net_->loading || !runtime_.match_ || text.empty())
        return;
    const auto& local = runtime_.match_->state().game.players[net_->local_slot];
    const auto name = field_text(local.name, sizeof local.name);
    char line[sizeof(oa::netgame::ChatRecord::text)];
    std::snprintf(
        line,
        sizeof line,
        "<%.*s> %.*s",
        static_cast<int>(name.size()),
        name.data(),
        static_cast<int>(text.size()),
        text.data()
    );
    nm::net_match_say(net_->net.get(), line);
    // The chat formatter also reports lines that reach everyone.
    const uint8_t mode = runtime_.match_->state().game.chat_mode;
    if (mode != OA_CHAT_MODE_CHOSEN && mode != OA_CHAT_MODE_ALLIES &&
        mode != oa::sim::messages::chat_mode_local_only)
        report_chat_line(line);
}

int NetworkPlay::run_net_loopback_check(std::size_t ticks) {
    if (!runtime_options(runtime_).headless_check)
        throw std::runtime_error("--net-loopback-check needs --headless-check");
    std::cout << "net loopback check: hosting and joining in-process over 127.0.0.1\n";
    // The joiner's match writes its own trace files beside the host's.
    auto joiner_options = runtime_options(runtime_);
    for (auto* path : {&joiner_options.trace_digest, &joiner_options.trace_units})
        if (!path->empty())
            *path += ".joiner";
    // Both machines run the same extension table; the check presses Pause
    // through it as the engine's hotkeys do.
    const Extension& extension = runtime_.extension_;
    // The joining machine is a whole second runtime, so it lives on the heap
    // beside this one rather than on the thread's stack.
    const auto joiner_runtime =
        std::make_unique<Runtime>(std::move(joiner_options), runtime_.assets_, extension);
    Runtime& joiner = *joiner_runtime;
    NetworkPlay& joiner_play = NetworkPlay::of(joiner);

    // The host services the joiner while it waits (loopback_configure). It
    // stops before the joiner is destroyed, however the check ends, so the
    // host's later teardown never services a freed machine.
    struct JoinerUnlink {
        NetworkPlay& host;

        ~JoinerUnlink() {
            if (host.net_) {
                host.net_->connection.pump_context = nullptr;
                host.net_->connection.pump_other = nullptr;
            }
        }
    };

    const JoinerUnlink unlink_joiner{*this};
    LoopbackSide host{this, "hoster"};
    LoopbackSide client{&joiner_play, "joiner"};
    const auto require = [](bool ok, const char* what) {
        if (!ok)
            throw std::runtime_error(std::string("net loopback check: ") + what);
    };
    const auto on_screen = [](Runtime& side, Screen screen) {
        const CheckHost side_host = check_host(side);
        return side_host.screen(side_host.context) == screen_id(screen);
    };
    require(
        NetHost::loopback_configure(*this, 0x51, joiner_play.net_.get()) &&
            NetHost::loopback_configure(joiner_play, 0x77, net_.get()),
        "loopback session storage"
    );
    for (auto* side : {&host, &client}) {
        side->net = nm::session_lobby_net(&side->play->net_->connection);
        mp::lobby_reset(*side->lobby, *side->game);
        side->lobby->net = side->net;
        side->lobby->local_version_major = 3;
        std::snprintf(mp::lobby_nickname(*side->game), 17, "%s", side->nickname);
        std::snprintf(mp::lobby_game_name(*side->game), 17, "%s", "Loopback Game");
    }
    const auto pump_both = [&](uint32_t wait_ms) {
        NetHost::loopback_pump(*this, wait_ms);
        NetHost::loopback_pump(joiner_play, wait_ms);
    };
    const auto wait_until = [&](auto done, const char* what) {
        const auto started = std::chrono::steady_clock::now();
        while (!done()) {
            const auto waited = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started
            );
            require(waited.count() < kLoopbackWaitMs, what);
            pump_both(2);
        }
    };
    const auto drain = [](LoopbackSide& side) {
        mp::LobbyEvent event{};
        while (side.net.receive(side.net.context, &event))
            (void)mp::lobby_apply_event(*side.lobby, event);
    };

    mp::Provider providers[mp::kMaxProviders]{};
    require(
        host.net.providers(host.net.context, providers, mp::kMaxProviders) >= 1,
        "no TCP/IP provider"
    );
    require(host.net.open(host.net.context, &providers[0], "127.0.0.1"), "host open");
    require(mp::connect_host(*host.lobby, *host.connect), "host session");
    // The host may seat a computer player, which then plays the joiner, or
    // the host while the joiner watches.
    const bool computer = net_options().net_loopback_computer;
    auto& host_info = mp::local_info(*host.lobby);
    host_info.max_units = computer ? kLoopbackComputerUnitsPerPlayer : kLoopbackUnitsPerPlayer;
    host_info.metal_hundreds = 10;
    host_info.energy_hundreds = 10;
    host_info.side = 0;
    host_info.color = 0;
    const bool small_map = computer && select_map_named(kLoopbackComputerMap);
    std::snprintf(
        host_info.map_name,
        sizeof host_info.map_name,
        "%s",
        small_map ? kLoopbackComputerMap : runtime_.first_map_name_.c_str()
    );
    mp::lobby_publish_session(*host.lobby);

    require(client.net.open(client.net.context, &providers[0], "127.0.0.1"), "client open");
    joiner_play.net_->connection.host->engine.config.enum_port =
        net_->connection.host->enum_port_bound;
    mp::SessionEntry sessions[mp::kMaxSessions]{};
    require(
        client.net.enumerate(client.net.context, sessions, mp::kMaxSessions) >= 1,
        "session enumeration"
    );
    client.connect->chosen = sessions[0];
    require(mp::connect_join(*client.lobby, *client.connect, false), "client join");
    const auto host_id = net_->connection.local_id;
    const auto client_id = joiner_play.net_->connection.local_id;
    wait_until(
        [&] {
            drain(host);
            drain(client);
            return mp::slot_for_player_id(*host.lobby, client_id) >= 0 &&
                   mp::slot_for_player_id(*client.lobby, host_id) >= 0;
        },
        "rosters did not meet"
    );
    const bool watching = net_options().net_loopback_watcher;
    auto& client_info = mp::local_info(*client.lobby);
    client_info.side = 1;
    client_info.color = 1;
    if (watching)
        client_info.options |= OA_SETUP_OPTION_WATCHER;
    // A hosted game starts with watching disallowed; the host's WATCHING
    // button allows it, for the watcher and for a defeated player who keeps
    // watching. Its CHEATING button opens cheats to both players; the
    // watcher's game leaves them closed.
    require(
        (host_info.options & mp::option::watching_allowed) == 0,
        "the hosted game started with watching allowed"
    );
    host_info.options ^= mp::option::watching_allowed;
    if (!watching)
        host_info.options ^= mp::option::cheats_allowed;
    mp::lobby_send_player_info(*host.lobby);
    mp::lobby_send_player_info(*client.lobby);
    wait_until(
        [&] {
            drain(host);
            drain(client);
            const auto host_slot = mp::slot_for_player_id(*client.lobby, host_id);
            const auto client_slot = mp::slot_for_player_id(*host.lobby, client_id);
            return host_slot >= 0 && client_slot >= 0 &&
                   (mp::slot_info(*client.lobby, host_slot)->role & mp::kRoleHost) != 0 &&
                   mp::slot_info(*client.lobby, host_slot)->max_units == host_info.max_units &&
                   mp::slot_info(*client.lobby, host_slot)->options == host_info.options &&
                   mp::slot_info(*host.lobby, client_slot)->side == 1;
        },
        "player info did not arrive"
    );
    // The host seats a computer player in the first open slot, as a click on
    // a closed slot does: a player of the host's session, playing Core. It
    // joins the host's team, unless the joiner only watches: then it plays
    // the host.
    uint32_t computer_id = nm::no_player_id;
    if (computer) {
        int32_t seat = -1;
        for (int32_t slot = 0; slot < mp::kSlotCount && seat < 0; ++slot) {
            const auto& player = mp::slot_player(*host.lobby, slot);
            if (player.in_use == 0 && player.status == mp::kSlotOpen)
                seat = slot;
        }
        require(seat >= 0, "no open slot for the computer player");
        mp::lobby_add_computer(*host.lobby, seat);
        auto* seat_info = mp::slot_info(*host.lobby, seat);
        require(seat_info != nullptr, "the computer player has no setup block");
        seat_info->color = static_cast<uint8_t>(mp::lobby_next_free_color(*host.lobby));
        seat_info->side = 1;
        computer_id = mp::slot_player(*host.lobby, seat).player_id;
        mp::lobby_send_player_info(*host.lobby);
        wait_until(
            [&] {
                drain(host);
                drain(client);
                return mp::slot_for_player_id(*host.lobby, computer_id) == seat &&
                       mp::slot_for_player_id(*client.lobby, computer_id) >= 0;
            },
            "the joiner did not seat the host's computer player"
        );
        if (!watching) {
            for (const auto slot :
                 {static_cast<int32_t>(host.lobby->game->local_player_index), seat}) {
                auto& player = mp::slot_player(*host.lobby, slot);
                mp::lobby_player_team(player) = 0;
                mp::lobby_send_team(*host.lobby, player);
            }
        }
        // The periodic block of the host's battle room sends every player's
        // blocks again, which lets through a frame of the computer player
        // that the joiner holds while the host's player numbers the shared
        // broadcast channel in between.
        const uint8_t team = watching ? mp::kNoTeam : 0;
        uint32_t rounds = 0;
        wait_until(
            [&] {
                if (++rounds % 50 == 0)
                    mp::lobby_send_player_info(*host.lobby);
                drain(host);
                drain(client);
                const auto seen = mp::slot_for_player_id(*client.lobby, computer_id);
                const auto host_seen = mp::slot_for_player_id(*client.lobby, host_id);
                return seen >= 0 && host_seen >= 0 &&
                       mp::slot_info(*client.lobby, seen)->color == seat_info->color &&
                       mp::lobby_player_team(mp::slot_player(*client.lobby, seen)) == team &&
                       mp::slot_player(*client.lobby, seen).machine_group != 0 &&
                       mp::slot_player(*client.lobby, seen).machine_group ==
                           mp::slot_player(*client.lobby, host_seen).machine_group;
            },
            "the computer player's block, team or machine group did not reach the joiner"
        );
        // Each battle room's refresh settles the alliances from the teams.
        for (auto* side : {&host, &client})
            mp::lobby_update_ally_matrix(*side->lobby);
        std::cout << "net loopback check: the host seated computer player "
                  << field_text(mp::slot_player(*host.lobby, seat).name, sizeof(Player::name))
                  << " in slot " << seat << (watching ? " against itself" : " on its team")
                  << ", on " << host_info.map_name << "\n";
    }

    // The battle room's unit sync over the installed unit table. The
    // client's copy of one unit carries a content checksum from other files,
    // so the host's verdict takes that unit away in both battle
    // rooms and its restriction row greys out; every other unit stays. The
    // verdicts stay with each battle room until its launch applies them.
    uint32_t taken_away_key = 0;
    int32_t synced_units = 0;
    {
        int32_t unit_count = 0;
        const CheckHost host_check = check_host(runtime_);
        mp::LobbyUnit* host_units =
            mp::multiplayer_units(*host_check.assets(host_check.context), &unit_count);
        require(unit_count > 100, "the install has no unit table");
        std::vector<mp::LobbyUnit> client_units(host_units, host_units + unit_count);
        int32_t altered = 1;
        while (altered < unit_count && (client_units[altered].abilities & mp::kUnitNoRestrict) != 0)
            ++altered;
        require(altered + 1 < unit_count, "no restrictable unit to alter");
        const int32_t kept = altered + 1;
        client_units[altered].content_checksum =
            mp::multiplayer_unit_checksum(nullptr, host_units[altered]) ^ 1u;
        host.lobby->units = host_units;
        client.lobby->units = client_units.data();
        for (auto* side : {&host, &client}) {
            side->lobby->unit_count = unit_count;
            side->lobby->services.unit_checksum = mp::multiplayer_unit_checksum;
        }
        mp::unit_sync_create(*host.lobby, true);
        mp::unit_sync_create(*client.lobby, false);
        wait_until(
            [&] {
                mp::unit_sync_tick(*host.lobby);
                mp::unit_sync_tick(*client.lobby);
                drain(host);
                drain(client);
                return mp::unit_sync_complete(*host.lobby);
            },
            "the unit sync did not finish"
        );
        mp::UnitSyncRecord record{};
        const auto shared = [&](LoopbackSide& side, int32_t type) {
            return mp::unit_sync_lookup(*side.lobby, host_units[type].fbi_hash, &record);
        };
        require(
            !shared(host, altered) && !shared(client, altered) && shared(host, kept) &&
                shared(client, kept),
            "the unit with other files was not taken away, or another unit was"
        );
        auto restrict = std::make_unique<mp::RestrictPanel>();
        mp::Panel panel;
        mp::restrict_open(*client.lobby, * restrict, panel);
        const auto row_flags = [&](int32_t type) {
            for (int32_t row = 0; row < restrict->count; ++row)
                if (restrict->entries[row].unit == type)
                    return static_cast<int32_t>(restrict->flags[row]);
            return -1;
        };
        require(
            row_flags(altered) >= 0 && (row_flags(altered) & mp::kRestrictRowUnavailable) != 0 &&
                row_flags(kept) >= 0 && (row_flags(kept) & mp::kRestrictRowUnavailable) == 0,
            "the client's restriction row for the unit with other files is not greyed"
        );
        std::cout << "net loopback check: unit sync over " << unit_count - 1 << " units took "
                  << host_units[altered].name << " away\n";
        taken_away_key = host_units[altered].fbi_hash;
        synced_units = unit_count - 1;
        for (auto* side : {&host, &client}) {
            side->lobby->units = nullptr;
            side->lobby->unit_count = 0;
        }
    }

    // Each machine left its single-player speed elsewhere; the match start
    // (through its timing reset) runs a network game at the normal
    // speed and keeps it in the speed preferences.
    constexpr uint16_t kHostLeftSpeed = 4;
    constexpr uint16_t kClientLeftSpeed = 17;
    // Writes a machine's speed into its speed preferences, as the
    // preferences' Cancel, UNDO and RESTORE write back a speed.
    const auto hold_speed = [](Runtime& side, uint16_t speed) {
        side.preferences_.game_speed = side.preferences_.current_game_speed = speed;
    };
    hold_speed(runtime_, kHostLeftSpeed);
    hold_speed(joiner, kClientLeftSpeed);

    // START: the host launches and its 0x08 launches the client. No frame
    // runs while the host builds its world; the loading screen's work goes on
    // through the engine's load progress hook, which sends the 0x08, a probe
    // and the host's rising load progress (0x2a) to the joiner's battle room.
    const auto joiner_received = [&](oa::netgame::RecordType type) {
        return joiner_play.net_->connection.packets->traffic
            .record_count[static_cast<uint8_t>(type)]
                         [static_cast<uint8_t>(oa::netgame::TrafficChannel::received)];
    };
    const auto probes_before_build = joiner_received(oa::netgame::RecordType::probe);
    std::vector<uint8_t> build_progress;
    NetHost::launch(*this, *host.lobby);
    require(net_->active && net_->loading, "host launch");
    wait_until(
        [&] {
            mp::LobbyEvent event{};
            while (client.net.receive(client.net.context, &event)) {
                if (event.kind == mp::LobbyEventKind::record && event.size >= 2 &&
                    event.data[0] == static_cast<uint8_t>(oa::netgame::RecordType::load_progress))
                    build_progress.push_back(event.data[1]);
                (void)mp::lobby_apply_event(*client.lobby, event);
            }
            return (mp::local_info(*client.lobby).options & mp::option::started) != 0;
        },
        "start record did not reach the client"
    );
    require(
        joiner_received(oa::netgame::RecordType::probe) > probes_before_build &&
            !build_progress.empty() && build_progress.front() < nm::load_progress_complete,
        "the host's build sent no probe or load progress to the joiner"
    );
    NetHost::launch(joiner_play, *client.lobby);
    require(joiner_play.net_->active && joiner_play.net_->loading, "client launch");
    // The load barrier and the match entry run through the per-frame path.
    wait_until(
        [&] {
            net_frame();
            joiner_play.net_frame();
            return !net_->loading && !joiner_play.net_->loading;
        },
        "load barrier did not pass"
    );
    require(
        net_->active && joiner_play.net_->active && on_screen(runtime_, Screen::match) &&
            on_screen(joiner, Screen::match),
        "match entry"
    );
    // Joining waits for no match report: each machine's report starts as
    // its match launches.
    require(
        net_->report_started && joiner_play.net_->report_started,
        "a machine's score report did not start as its match launched"
    );
    std::cout << "net loopback check: the host's build sent the joiner load progress";
    for (const auto percent : build_progress)
        std::cout << ' ' << static_cast<int>(percent);
    std::cout << '\n';
    // Each launch applied its battle room's verdicts to the unit table it
    // loaded and dropped them: neither match has the unit taken away, both
    // have every other.
    for (const auto* side : {&host, &client}) {
        const auto& world = *side->play->net_->net->world;
        bool kept = false;
        for (uint32_t type = 1; type < world.unit_def_count; ++type)
            kept = kept || world.unit_defs[type].fbi_hash == taken_away_key;
        require(
            !kept && static_cast<int32_t>(world.unit_def_count) == synced_units &&
                side->lobby->sync.record_count == 0,
            "a launch did not apply its battle room's unit verdicts"
        );
    }
    for (const Runtime* side :
         {static_cast<const Runtime*>(&runtime_), static_cast<const Runtime*>(&joiner)}) {
        const auto& timing = side->match_timing_;
        const auto& game = side->match_->state().game;
        require(
            timing.requested_rate == oa::base::game_loop::normal_game_speed &&
                timing.actual_rate == oa::base::game_loop::normal_game_speed &&
                game.requested_speed == oa::base::game_loop::normal_game_speed &&
                game.current_speed == oa::base::game_loop::normal_game_speed &&
                side->preferences_.game_speed == oa::base::game_loop::normal_game_speed &&
                side->preferences_.current_game_speed == oa::base::game_loop::normal_game_speed,
            "a machine did not start the network game at the normal speed"
        );
    }
    if (computer) {
        NetHost::computer_match(*this, joiner_play, ticks, computer_id, watching);
        return 0;
    }
    const auto host_commander = NetHost::local_commander(*this);
    require(host_commander != 0, "host commander");
    const auto& host_world = runtime_.match_->state();
    const auto& client_world = joiner.match_->state();
    const auto commander_z = host_world.units[host_commander].position.z;
    const auto speed_before = runtime_.preferences_.current_game_speed;
    // The host commander's pose at each host tick; the client's copy is
    // compared with the pose of the last record it applied. Slot orders
    // differ between machines, so the client's host slot is found by id.
    const auto client_host_slot = static_cast<std::size_t>(
        std::find_if(
            std::begin(client_world.game.players),
            std::end(client_world.game.players),
            [host_id](const Player& player) {
                return player.in_use != 0 && player.player_id == host_id;
            }
        ) -
        std::begin(client_world.game.players)
    );
    require(client_host_slot < OA_PLAYER_COUNT, "the client has no slot for the host");
    require(
        client_world.game.players[client_host_slot].load_progress == nm::load_progress_complete,
        "the host's load progress did not reach 100 at the joiner"
    );
    std::vector<UnitPose> host_poses;
    uint32_t copy_compared = 0;
    uint32_t copy_divergence = 0;
    // The host's 0x09 for its commander goes out two loading frames after
    // its barrier passed, once the joiner has passed its own and takes it:
    // the joiner's copy stands on the host's start position from that 0x09,
    // so it is on the host's path from the first unit state record it
    // applies. The copy is compared from then on.
    uint32_t copy_placed_tick = 0;
    uint32_t first_applied_tick = 0; // host tick of the first record the client applied
    const auto note_host_pose = [&] {
        const auto tick = host_world.game.tick;
        if (host_poses.size() <= tick)
            host_poses.resize(tick + 1);
        host_poses[tick] = NetHost::unit_pose(*this, host_commander);
    };
    const auto compare_copy = [&] {
        const auto tick = client_world.game.players[client_host_slot].last_sim_tick;
        if (tick <= 0 || static_cast<std::size_t>(tick) >= host_poses.size())
            return;
        if (first_applied_tick == 0)
            first_applied_tick = static_cast<uint32_t>(tick);
        if (joiner.match_->ground_runtime(host_commander) == nullptr)
            return;
        const auto& owner = host_poses[static_cast<std::size_t>(tick)];
        const auto divergence =
            pose_divergence(owner, NetHost::unit_pose(joiner_play, host_commander));
        if (copy_placed_tick == 0) {
            if (divergence != 0)
                return;
            copy_placed_tick = static_cast<uint32_t>(tick);
        }
        ++copy_compared;
        copy_divergence = std::max(copy_divergence, divergence);
    };

    // Every shot the host commander fires: each machine's records for them,
    // the client's arriving as 0x0d. The first of each must leave from the
    // same point for the same point.
    struct Shots {
        uint32_t count{};
        FixedVec3 origin{};
        FixedVec3 target{};
    };

    Shots host_shots{};
    Shots client_shots{};
    const auto host_commander_ref = oa::oa_unit_ref_from_slot(host_commander);
    const auto count_shots = [host_commander_ref](const World& world, Shots& shots) {
        const int32_t projectile_count = world.game.projectile_count;
        const auto live = std::clamp<int32_t>(projectile_count, 0, OA_PROJECTILE_CAPACITY);
        for (int32_t i = 0; i < live; ++i) {
            const auto& shot = world.projectiles[i];
            if (shot.source != host_commander_ref || shot.created_tick != world.game.tick)
                continue;
            if (shots.count++ == 0) {
                shots.origin = shot.origin;
                shots.target = shot.target;
            }
        }
    };
    const auto same_point = [](const FixedVec3& a, const FixedVec3& b) {
        return a.x == b.x && a.y == b.y && a.z == b.z;
    };

    // Orders, chat, a speed change, the menu, a pause and a volley at the
    // ground from the host; the client follows each through the records.
    // Typed text reaches everyone, an option command's echo stays on the host
    // (the chat line's local-only mode).
    const auto type_line = [&](const char* text) {
        runtime_.chat_buffer_ = text;
        runtime_.submit_chat_line();
    };
    // The Pause key as the engine's hotkeys run it: the pause bit flips, then
    // the extension hears of it.
    auto& host_game = net_->net->world->game;
    const auto press_pause = [&] {
        host_game.sim_run_flags =
            static_cast<uint16_t>(host_game.sim_run_flags ^ nm::run_flag_paused);
        extension.pause_changed(
            extension.context, runtime_, (host_game.sim_run_flags & nm::run_flag_paused) != 0
        );
    };
    const auto pauses_received = [&] {
        const auto& traffic = joiner_play.net_->connection.packets->traffic;
        return traffic.record_count[static_cast<uint8_t>(oa::netgame::RecordType::pause_speed)]
                                   [static_cast<uint8_t>(oa::netgame::TrafficChannel::received)];
    };
    const auto both_paused = [&] {
        return (host_world.game.sim_run_flags & nm::run_flag_paused) != 0 &&
               (client_world.game.sim_run_flags & nm::run_flag_paused) != 0;
    };
    // The speed keys as the engine's hotkeys run them on the host: the
    // engine reports each speed they set to the extension, which sends it.
    const auto raise_host_speed = [&] { runtime_.adjust_game_speed(1); };
    // A machine's preferences put a speed back straight into its match, as
    // their Cancel and UNDO put back the speed they opened with and RESTORE
    // the normal speed; the engine reports none of them, and the other
    // machine hears nothing of it.
    const auto put_back_speed = [&](Runtime& side, uint16_t speed) {
        hold_speed(side, speed);
        auto& side_game = NetworkPlay::of(side).net_->net->world->game;
        side_game.requested_speed = side_game.current_speed = speed;
    };
    const auto host_speeds_received = [&] {
        const auto& traffic = net_->connection.packets->traffic;
        return traffic.record_count[static_cast<uint8_t>(oa::netgame::RecordType::pause_speed)]
                                   [static_cast<uint8_t>(oa::netgame::TrafficChannel::received)];
    };
    uint32_t host_speeds_before_cancel = 0;
    uint16_t host_speed_before_cancel = 0;
    uint32_t joiner_speeds_before_put_back = 0;
    uint16_t joiner_speed_before_put_back = 0;
    // Remembers what the joiner has heard as the host's preferences put
    // back a speed, and puts it back.
    const auto host_puts_back = [&](uint16_t speed) {
        joiner_speeds_before_put_back = pauses_received();
        joiner_speed_before_put_back = client_world.game.requested_speed;
        put_back_speed(runtime_, speed);
    };
    // Checks that the joiner heard nothing of the speed the host put back.
    const auto require_put_back_kept = [&](const char* what, uint16_t speed) {
        const std::string failure =
            std::string("the host's ") + what + " sent the speed it put back";
        require(
            pauses_received() == joiner_speeds_before_put_back &&
                client_world.game.requested_speed == joiner_speed_before_put_back &&
                host_world.game.requested_speed == speed &&
                runtime_.preferences_.current_game_speed == speed,
            failure.c_str()
        );
        std::cout << "net loopback check: the host's " << what << " put back speed " << speed
                  << " and sent nothing; the joiner kept " << joiner_speed_before_put_back << "\n";
    };
    uint32_t pauses_before_menu = 0;
    uint32_t client_tick_before_menu = 0;
    uint32_t held_host_tick = 0;
    uint32_t held_client_tick = 0;
    uint32_t netstats_tick = 0;
    for (std::size_t tick = 0; tick < ticks; ++tick) {
        if (tick == 30) {
            const auto& unit = host_world.units[host_commander];
            runtime_.match_->issue_ground_move(
                host_commander,
                {unit.position.x, unit.position.y, unit.position.z + kLoopbackMoveDistance},
                false
            );
        }
        if (tick == 60)
            net_say("gg");
        if (tick == 61)
            type_line("glhf");
        if (tick == 62 || tick == 63)
            type_line("+clock");
        if (tick == 64)
            type_line("+bps");
        if (tick == 65) {
            type_line("+netstats");
            netstats_tick = host_world.game.tick;
        }
        // A speed key on the host reaches the joiner.
        if (tick == 100)
            raise_host_speed();
        // The host's preferences send nothing: its RESTORE puts back the
        // normal speed, then its Cancel or UNDO the speed they opened with,
        // and the joiner keeps the speed the key set.
        if (tick == 110) {
            require(
                client_world.game.requested_speed == host_world.game.requested_speed &&
                    host_world.game.requested_speed == speed_before + 1,
                "the host's speed key did not reach the joiner"
            );
            host_puts_back(oa::base::game_loop::normal_game_speed);
        }
        if (tick == 116) {
            require_put_back_kept("RESTORE", oa::base::game_loop::normal_game_speed);
            host_puts_back(static_cast<uint16_t>(speed_before + 1));
        }
        if (tick == 122)
            require_put_back_kept("Cancel or UNDO", static_cast<uint16_t>(speed_before + 1));
        // A watcher's preferences put back the speed they opened with
        // straight into its match (their Cancel, UNDO or RESTORE); the host
        // hears nothing of it, and its next speed key reaches the watcher.
        if (watching && tick == 122) {
            host_speeds_before_cancel = host_speeds_received();
            host_speed_before_cancel = host_world.game.requested_speed;
            put_back_speed(joiner, speed_before);
        }
        if (watching && tick == 130) {
            require(
                host_speeds_received() == host_speeds_before_cancel &&
                    host_world.game.requested_speed == host_speed_before_cancel &&
                    client_world.game.requested_speed == speed_before,
                "the watcher's preferences sent the speed they wrote back"
            );
            std::cout << "net loopback check: the watcher's preferences wrote back speed "
                      << speed_before << " and sent nothing\n";
            raise_host_speed();
        }
        // The host's in-game menu holds nothing in a shared match and sends no
        // pause: the client keeps ticking and hears no 0x19.
        if (tick == 150) {
            runtime_.match_paused_ = true;
            pauses_before_menu = pauses_received();
            client_tick_before_menu = client_world.game.tick;
        }
        if (tick == 170) {
            require(
                pauses_received() == pauses_before_menu &&
                    (client_world.game.sim_run_flags & nm::run_flag_paused) == 0 &&
                    client_world.game.tick > client_tick_before_menu,
                "the host's menu paused the client"
            );
            runtime_.match_paused_ = false;
        }
        // The Pause key pauses both machines, whose ticks then hold, and
        // pressed again resumes both.
        if (tick == 172)
            press_pause();
        if (tick == 176) {
            require(both_paused(), "the Pause key did not pause both machines");
            held_host_tick = host_world.game.tick;
            held_client_tick = client_world.game.tick;
        }
        if (tick == 184) {
            require(
                both_paused() && host_world.game.tick == held_host_tick &&
                    client_world.game.tick == held_client_tick,
                "a paused machine's tick moved"
            );
            press_pause();
        }
        if (tick == 190) {
            require(
                (host_world.game.sim_run_flags & nm::run_flag_paused) == 0 &&
                    (client_world.game.sim_run_flags & nm::run_flag_paused) == 0 &&
                    host_world.game.tick > held_host_tick &&
                    client_world.game.tick > held_client_tick,
                "the second Pause did not resume both machines"
            );
            // A machine that runs hears a pause only in a step, after its
            // tick has moved on, while a paused one hears the resume before
            // it steps, so the two machines may hold for a different number
            // of ticks. The machine behind catches up before the worlds are
            // compared at equal ticks.
            const uint32_t host_tick = host_world.game.tick;
            const uint32_t client_tick = client_world.game.tick;
            require(
                std::max(host_tick, client_tick) - std::min(host_tick, client_tick) <= 2,
                "the pause left the machines' ticks far apart"
            );
            std::cout
                << "net loopback check: the host's menu paused nothing; its Pause key held both "
                << "machines at ticks " << held_host_tick << " / " << held_client_tick << "\n";
            while (host_world.game.tick < client_world.game.tick) {
                NetHost::loopback_step(*this);
                note_host_pose();
                count_shots(host_world, host_shots);
                pump_both(kTickPumpWaitMs);
            }
            while (client_world.game.tick < host_world.game.tick) {
                NetHost::loopback_step(joiner_play);
                compare_copy();
                count_shots(client_world, client_shots);
                pump_both(1);
            }
        }
        if (tick == kLoopbackFireFrom) {
            const auto& unit = host_world.units[host_commander];
            const auto map_depth = static_cast<int32_t>(host_world.game.map_pixel_height) << 16;
            const int32_t ahead = unit.position.z + kLoopbackShotDistance < map_depth
                                      ? kLoopbackShotDistance
                                      : -kLoopbackShotDistance;
            (void)runtime_.match_->issue_attack_ground(
                host_commander, {unit.position.x, unit.position.y, unit.position.z + ahead}, false
            );
        }
        if (tick == kLoopbackFireUntil)
            (void)runtime_.match_->issue_stop(host_commander);
        net_frame();
        joiner_play.net_frame();
        NetHost::loopback_step(*this);
        note_host_pose();
        count_shots(host_world, host_shots);
        NetHost::loopback_step(joiner_play);
        compare_copy();
        count_shots(client_world, client_shots);
        pump_both(kTickPumpWaitMs);
    }
    require((client_world.game.sim_run_flags & nm::run_flag_paused) == 0, "client stayed paused");
    require(
        runtime_.preferences_.current_game_speed != speed_before &&
            joiner.preferences_.current_game_speed == runtime_.preferences_.current_game_speed,
        "speed change did not reach the client"
    );
    // Settle: the owner's full record refreshes every slot once per
    // units_per_player ticks.
    const auto settle_limit = 3u * host_world.game.units_per_player;
    uint64_t host_digest = 0;
    uint64_t client_digest = 0;
    uint32_t settled_after = 0;
    bool agreed = false;
    for (uint32_t extra = 0; extra < settle_limit && !agreed; ++extra) {
        NetHost::loopback_step(*this);
        note_host_pose();
        count_shots(host_world, host_shots);
        NetHost::loopback_step(joiner_play);
        compare_copy();
        count_shots(client_world, client_shots);
        pump_both(kTickPumpWaitMs);
        host_digest = world_digest(host_world);
        client_digest = world_digest(client_world);
        agreed = host_digest == client_digest;
        settled_after = extra + 1;
    }
    uint32_t live = 0;
    for (uint32_t slot = 1; slot < host_world.unit_slot_count; ++slot)
        live += host_world.units[slot].type_index != 0 ? 1 : 0;
    const auto& host_net = *net_->net;
    const auto& client_net = *joiner_play.net_->net;
    char digests[64];
    std::snprintf(
        digests,
        sizeof digests,
        "%016llx / %016llx",
        static_cast<unsigned long long>(host_digest),
        static_cast<unsigned long long>(client_digest)
    );
    std::cout << "net loopback check: " << ticks << " ticks + " << settled_after << " settle, tick "
              << host_world.game.tick << " / " << client_world.game.tick << ", digests " << digests
              << ", live units " << live << ", records applied " << host_net.records_applied
              << " / " << client_net.records_applied << ", refused " << host_net.records_refused
              << " / " << client_net.records_refused << ", errors " << host_net.record_errors
              << " / " << client_net.record_errors << ", remote creates "
              << net_->binding.created_remote << " / " << joiner_play.net_->binding.created_remote
              << ", host commander copy divergence " << copy_divergence << " over " << copy_compared
              << " records, host commander shots " << host_shots.count << " / "
              << client_shots.count << "\n";
    require(agreed, "world digests differ after settling");
    require(
        copy_placed_tick != 0 && copy_placed_tick == first_applied_tick,
        "the client's copy of the host commander did not stand on its start position from its 0x09"
    );
    require(
        copy_compared > ticks / 2 && copy_divergence == 0,
        "the client's copy left the host commander's path"
    );
    if (watching) {
        require(
            live == 1 && NetHost::local_commander(joiner_play) == 0,
            "the watcher was given a commander"
        );
        require(
            net_->binding.created_remote == 0 && joiner_play.net_->binding.created_remote == 1,
            "the host commander did not reach the watcher"
        );
        require(
            joiner.match_->outcome() == sim::scenario::Outcome::ongoing, "the watcher was defeated"
        );
    } else {
        require(live == 2, "both commanders are not present");
        require(
            net_->binding.created_remote == 1 && joiner_play.net_->binding.created_remote == 1,
            "remote commanders were not created"
        );
    }
    require(host_net.record_errors == 0 && client_net.record_errors == 0, "record errors");
    require(
        host_shots.count != 0 && client_shots.count == host_shots.count,
        "the host commander's shots did not all reach the client"
    );
    require(
        same_point(client_shots.origin, host_shots.origin) &&
            same_point(client_shots.target, host_shots.target),
        "the client's copy of the first shot left from or for another point"
    );
    require(
        host_world.units[host_commander].position.z != commander_z, "host commander did not move"
    );
    // The in-game team panels over the session, run as the host's panels
    // run them, through the host the extension fills for them. ALLIES.GUI
    // allies the joiner and breaks the alliance again: each change reaches
    // the joiner's alliance tables (0x23) and its line the joiner's log.
    // SHARE.GUI gives the joiner 100 metal (0x16), which the joiner is
    // credited, then a selected unit (0x14) and the map: the joiner's player
    // owns the unit, the host's is gone on both machines. CONTROL.GUI turns
    // watching off, which the joiner's copy of the host's block shows, and
    // on again, and closes, removing nobody.
    if (!watching) {
        auto& panel_world = *net_->net->world;
        const auto panel_host_slot = panel_world.game.local_player_index;
        const auto panel_client_slot = static_cast<uint8_t>(
            std::find_if(
                std::begin(panel_world.game.players),
                std::end(panel_world.game.players),
                [client_id](const Player& player) {
                    return player.in_use != 0 && player.player_id == client_id;
                }
            ) -
            std::begin(panel_world.game.players)
        );
        require(panel_client_slot < OA_PLAYER_COUNT, "the host has no slot for the client");
        oa::ui::hud::TeamPanelHost panels{};
        panels.context = &runtime_;
        extension.team_panel_host(extension.context, runtime_, panels);
        // The chat line goes out as the match's chat lines do (net_send_chat).
        oa::sim::messages::Hooks chat_hooks{};
        chat_hooks.context = this;
        chat_hooks.share_chat = [](void* context, const char* line) {
            static_cast<NetworkPlay*>(context)->net_send_chat(line);
        };
        const oa::ui::hud::PanelControls no_controls{
            nullptr,
            [](void*, const char*) -> int32_t { return -1; },
            [](void*, int32_t, int32_t) {},
            [](void*, int32_t, int32_t) {},
            [](void*, int32_t) {},
            [](void*, int32_t, int32_t) {},
            [](void*, int32_t) -> int32_t { return 0; },
            [](void*, int32_t) -> const char* { return ""; },
            [](void*, int32_t, const char*) {},
            [](void*, int32_t) {},
            [](void*, int32_t, bool) {},
            [](void*, int32_t, bool) {},
            [](void*, int32_t, const char*) {},
        };
        const oa::ui::hud::HudEvents events{
            nullptr, [](void*, const char*) {}, [](void*, const char*, int32_t) {}
        };
        const auto run_both = [&](int frames) {
            for (int frame = 0; frame < frames; ++frame) {
                NetHost::loopback_step(*this);
                NetHost::loopback_step(joiner_play);
                pump_both(kTickPumpWaitMs);
            }
        };
        panel_world.game.chat_mode = OA_CHAT_MODE_EVERYONE;
        const auto& joiner_local = client_world.game.players[client_world.game.local_player_index];
        char ally_control[16];
        std::snprintf(
            ally_control,
            sizeof ally_control,
            "LIVEALLY%u",
            static_cast<unsigned>(panel_client_slot)
        );
        for (const uint8_t allied : {uint8_t{1}, uint8_t{0}}) {
            const auto result = oa::ui::hud::allies_panel_click(
                panel_world, ally_control, no_controls, events, panels, nullptr, nullptr
            );
            require(result.announcement[0] != '\0', "ALLIES.GUI had no line to say");
            oa::sim::messages::post_chat(
                panel_world,
                panel_world.game.players[panel_host_slot],
                result.announcement,
                oa::ui::hud::kMessageKindChat,
                nullptr,
                chat_hooks
            );
            run_both(4);
            require(
                panel_world.game.players[panel_host_slot].alliance[panel_client_slot] == allied &&
                    joiner_local.allied_by[client_host_slot] == allied,
                "the host's alliance change did not reach the joiner"
            );
            require(
                joiner_play.net_->binding.match->allied(
                    static_cast<uint8_t>(client_host_slot), joiner_local.index
                ) == (allied != 0),
                "the joiner's simulation did not follow the host's alliance"
            );
        }

        // SHARE.GUI's view of the host's players.
        std::array<oa::UnitEconomy*, OA_PLAYER_COUNT> economies{};
        std::array<uint8_t, OA_PLAYER_COUNT> setup_flags{};
        for (std::size_t index = 0; index < OA_PLAYER_COUNT; ++index) {
            auto& player = panel_world.game.players[index];
            economies[index] = oa::world_player_economy(&panel_world, &player);
            if (const auto* info = oa::world_player_info(&panel_world, &player))
                setup_flags[index] = static_cast<uint8_t>(info->options);
        }
        oa::ui::hud::ShareWorld share_world{
            panel_world.game.players,
            economies.data(),
            setup_flags.data(),
            panel_host_slot,
            panel_world.game.difficulty
        };
        oa::ui::hud::SharePanel share{};
        uint16_t share_flags = panel_world.game.frame_flags;
        require(
            oa::ui::hud::open_share_panel(share_world, share, share_flags), "SHARE.GUI did not open"
        );
        int32_t share_row = -1;
        for (int32_t row = 0; row < share.recipient_count; ++row)
            if (share.recipients[row] == panel_client_slot)
                share_row = row;
        require(share_row >= 0, "SHARE.GUI does not list the joiner");
        oa::ui::hud::ShareHost share_host{};
        share_host.user = &panels;
        share_host.send_metal = [](void* user, uint8_t from, uint8_t to, float amount) {
            const auto& host = *static_cast<const oa::ui::hud::TeamPanelHost*>(user);
            host.resources_given(host.context, from, to, true, amount);
        };
        share_host.send_energy = [](void* user, uint8_t from, uint8_t to, float amount) {
            const auto& host = *static_cast<const oa::ui::hud::TeamPanelHost*>(user);
            host.resources_given(host.context, from, to, false, amount);
        };
        share_host.share_map = [](void* user, uint8_t from, uint8_t to) {
            const auto& host = *static_cast<const oa::ui::hud::TeamPanelHost*>(user);
            host.sight_shared(host.context, from, to);
        };
        auto& joiner_world = *joiner_play.net_->net->world;
        const auto* joiner_economy = oa::world_player_economy(
            &joiner_world, &joiner_world.game.players[joiner_world.game.local_player_index]
        );
        require(joiner_economy != nullptr, "the joiner has no economy");
        (void)oa::ui::hud::share_panel_click(
            share_world,
            share,
            "OK",
            share_row,
            100,
            0,
            false,
            false,
            share_flags,
            events,
            share_host
        );
        nm::packet_layer_flush(
            net_->connection.packets, nm::net_connection_time(&net_->connection), true
        );
        pump_both(1);
        const auto credited_before = joiner_economy->metal.produced;
        (void)nm::net_match_pump(joiner_play.net_->net.get());
        require(
            joiner_economy->metal.produced - credited_before == 100.0F,
            "the joiner was not credited the metal the host gave"
        );

        // A kbot the host builds is selected alone and given with the map.
        const auto named = [](const char* field, std::size_t capacity, std::string_view name) {
            const auto held = field_text(field, capacity);
            return held.size() == name.size() &&
                   std::equal(held.begin(), held.end(), name.begin(), [](char a, char b) {
                       return std::tolower(static_cast<unsigned char>(a)) ==
                              std::tolower(static_cast<unsigned char>(b));
                   });
        };
        uint16_t kbot_type = 0;
        for (uint32_t type = 1; type < panel_world.unit_def_count && kbot_type == 0; ++type)
            if (named(
                    panel_world.unit_defs[type].unit_name,
                    sizeof panel_world.unit_defs[type].unit_name,
                    "ARMPW"
                ))
                kbot_type = static_cast<uint16_t>(type);
        require(kbot_type != 0, "the install has no ARMPW to give");
        const auto& commander_record = panel_world.units[host_commander];
        const int32_t aside = commander_record.position.x + (96 << 16) <
                                      (static_cast<int32_t>(panel_world.game.map_pixel_width) << 16)
                                  ? (96 << 16)
                                  : -(96 << 16);
        oa::sim::unit_spawn::Request request{};
        request.player = panel_host_slot;
        request.type = kbot_type;
        request.position = {
            static_cast<uint32_t>(commander_record.position.x + aside),
            static_cast<uint32_t>(commander_record.position.y),
            static_cast<uint32_t>(commander_record.position.z)
        };
        request.finished = true;
        request.state = 1;
        auto* made = net_->binding.match->create(request);
        require(
            made != nullptr && made->unit != nullptr, "the host could not build a unit to give"
        );
        const auto gift = made->unit_index;
        run_both(4);
        require(
            (client_world.units[gift].flags & OA_UNIT_FLAG_LIVE) != 0,
            "the joiner has no copy of the unit to be given"
        );
        uint32_t own_count = 0;
        oa::Unit* own = oa::world_player_units(
            &panel_world, &panel_world.game.players[panel_host_slot], &own_count
        );
        for (uint32_t index = 0; index < own_count; ++index)
            own[index].flags &= ~OA_UNIT_FLAG_SELECTED;
        panel_world.units[gift].flags |= OA_UNIT_FLAG_SELECTED;
        share_host.user = this;
        share_host.give_units = [](void* user, uint8_t, uint8_t to) {
            auto& self = *static_cast<NetworkPlay*>(user);
            const oa::ui::hud::UnitTransfer transfer{
                &self, [](void* owner, oa::Unit& unit, oa::Player& recipient) {
                    auto& play = *static_cast<NetworkPlay*>(owner);
                    play.net_->binding.match->transfer_unit(
                        static_cast<uint16_t>(oa::world_unit_slot(play.net_->net->world, &unit)),
                        recipient.index
                    );
                }
            };
            oa::ui::hud::give_selected_units(*self.net_->net->world, to, nullptr, transfer);
        };
        share_host.share_map = [](void* user, uint8_t from, uint8_t to) {
            auto& self = *static_cast<NetworkPlay*>(user);
            nm::net_match_share_sight(self.net_->net.get(), from, to);
        };
        share_host.send_metal = nullptr;
        share_host.send_energy = nullptr;
        const auto received = [&](oa::netgame::RecordType type) {
            return joiner_play.net_->connection.packets->traffic
                .record_count[static_cast<uint8_t>(type)]
                             [static_cast<uint8_t>(oa::netgame::TrafficChannel::received)];
        };
        const auto gifts_before = received(oa::netgame::RecordType::unit_transfer);
        const auto maps_before = received(oa::netgame::RecordType::resource_give);
        // Cells the host's player has mapped on the joiner's machine and the
        // joiner's player has not; the first rows are made so.
        const auto host_bit = static_cast<uint16_t>(1U << client_host_slot);
        const auto joiner_bit = static_cast<uint16_t>(1U << joiner_local.index);
        auto& joiner_mapped = joiner_play.net_->binding.match->sight_mutable().player_bits;
        constexpr std::size_t marked_cells = 256;
        for (std::size_t cell = 0; cell < std::min(joiner_mapped.size(), marked_cells); ++cell)
            joiner_mapped[cell] =
                static_cast<uint16_t>((joiner_mapped[cell] | host_bit) & ~joiner_bit);
        (void)oa::ui::hud::share_panel_click(
            share_world, share, "OK", share_row, 0, 0, true, true, share_flags, events, share_host
        );
        run_both(8);
        require(
            received(oa::netgame::RecordType::unit_transfer) - gifts_before == 1 &&
                received(oa::netgame::RecordType::resource_give) - maps_before == 1,
            "the joiner did not receive the unit and the map the host gave"
        );
        require(
            std::none_of(
                joiner_mapped.begin(),
                joiner_mapped.end(),
                [&](uint16_t bits) { return (bits & host_bit) != 0 && (bits & joiner_bit) == 0; }
            ),
            "the joiner's player did not take the map the host's player gave"
        );
        uint16_t given = 0;
        for (uint32_t slot = 1; slot < client_world.unit_slot_count && given == 0; ++slot) {
            const auto& unit = client_world.units[slot];
            if (unit.type_index == kbot_type && (unit.flags & OA_UNIT_FLAG_LIVE) != 0 &&
                unit.owner_index == client_world.game.local_player_index)
                given = static_cast<uint16_t>(slot);
        }
        require(given != 0 && given != gift, "the joiner's player does not own the given unit");
        require(
            (host_world.units[gift].flags & OA_UNIT_FLAG_LIVE) == 0 &&
                (client_world.units[gift].flags & OA_UNIT_FLAG_LIVE) == 0,
            "the given unit is still on a machine"
        );
        require(
            (host_world.units[given].flags & OA_UNIT_FLAG_LIVE) != 0 &&
                host_world.units[given].owner_index == panel_client_slot,
            "the host does not hold the given unit as the joiner's"
        );
        // The joiner's gift goes again, so its commander is its last unit.
        joiner_play.net_->binding.match->kill_unit(given, 0);
        run_both(8);
        require(
            (host_world.units[given].flags & OA_UNIT_FLAG_LIVE) == 0 &&
                (client_world.units[given].flags & OA_UNIT_FLAG_LIVE) == 0,
            "the given unit did not die on both machines"
        );

        auto* panel_info =
            oa::world_player_info(&panel_world, &panel_world.game.players[panel_host_slot]);
        require(panel_info != nullptr, "the host has no setup block");
        const auto allowed_before = panel_info->options & OA_SETUP_OPTION_WATCHING_ALLOWED;
        (void)oa::ui::hud::control_panel_click(
            panel_world, "WATCHING", no_controls, events, panels
        );
        run_both(4);
        const auto* host_copy_info = info_for_id(client_world, host_id);
        require(
            host_copy_info != nullptr &&
                (host_copy_info->options & OA_SETUP_OPTION_WATCHING_ALLOWED) ==
                    (allowed_before ^ OA_SETUP_OPTION_WATCHING_ALLOWED),
            "the joiner's copy of the host's block did not follow WATCHING"
        );
        // OK publishes the session again from the host's block as it now
        // is: the joiner's view of it follows WATCHING, and keeps the name
        // in full.
        (void)oa::ui::hud::control_panel_click(panel_world, "OK", no_controls, events, panels);
        run_both(4);
        const auto& joiner_view = joiner_play.net_->connection.host->engine;
        const auto published_options = static_cast<uint16_t>(joiner_view.desc.user[0] >> 16);
        require(
            (published_options & OA_SETUP_OPTION_WATCHING_ALLOWED) ==
                    (allowed_before ^ OA_SETUP_OPTION_WATCHING_ALLOWED) &&
                std::strlen(joiner_view.session_name) > 16 &&
                std::strcmp(joiner_view.session_name, net_->connection.host->engine.session_name) ==
                    0,
            "CONTROL.GUI's OK did not publish the host's block and the full name again"
        );
        (void)oa::ui::hud::control_panel_click(
            panel_world, "WATCHING", no_controls, events, panels
        );
        const auto closed =
            oa::ui::hud::control_panel_click(panel_world, "OK", no_controls, events, panels);
        run_both(4);
        require(
            closed.click == oa::ui::hud::TeamPanelClick::closed &&
                (host_copy_info->options & OA_SETUP_OPTION_WATCHING_ALLOWED) == allowed_before &&
                panel_world.game.players[panel_client_slot].in_use != 0 &&
                panel_world.game.players[panel_client_slot].reject_reason == 0,
            "CONTROL.GUI's OK removed a player or left watching changed"
        );
        std::cout << "net loopback check: the host's team panels allied the joiner and broke it "
                     "off, gave it "
                  << "100 metal, unit " << gift << " (now its unit " << given
                  << ") and the map, and "
                  << "turned watching off and on\n";
    }
    const auto client_lines = joiner.match_message_lines();
    const auto host_chat = runtime_.match_message_lines();
    const auto shown = [](const std::vector<std::string>& lines, const char* line) {
        return std::find(lines.begin(), lines.end(), line) != lines.end();
    };
    require(shown(client_lines, "<hoster> gg"), "chat did not reach the client");
    require(
        watching || (shown(client_lines, "<hoster>  allied with joiner") &&
                     shown(client_lines, "<hoster>  broke alliance with joiner")),
        "the lines ALLIES.GUI said did not reach the client"
    );
    require(
        shown(client_lines, "<hoster> glhf") && shown(host_chat, "<hoster> glhf"),
        "the typed chat line did not reach both players"
    );
    // The client stores the line as another player's chat from the host's
    // slot, which starts it with the host's logo; the host's own echo comes
    // from no player.
    namespace messages = oa::sim::messages;
    const auto place = [](const std::vector<std::string>& lines, const char* line) {
        return static_cast<std::size_t>(
            std::find(lines.begin(), lines.end(), line) - lines.begin()
        );
    };
    const auto received = logged_line(client_world, place(client_lines, "<hoster> glhf"));
    require(
        received && (received->kind & messages::kind_mask) == messages::kind_player_chat &&
            received->sender == client_host_slot,
        "the client did not store the host's chat line as the host's chat"
    );
    const auto echoed = logged_line(host_world, place(host_chat, "<hoster> glhf"));
    require(
        echoed && (echoed->kind & messages::kind_mask) == oa::ui::hud::kMessageKindChat &&
            echoed->sender == messages::sender_none,
        "the host's own chat line was not stored as a line from no player"
    );
    require(
        shown(host_chat, "<hoster> +clock") && !shown(client_lines, "<hoster> +clock"),
        "an option command's echo was not local to the host"
    );
    // "BPS" turns the host's traffic readout on and "NetStats" resets its
    // counters, their echoes local too. The readout then draws its two rates
    // over their bars from the connection's counters, and the next "BPS"
    // takes it away.
    auto& traffic = net_->connection.packets->traffic;
    require(
        host_world.game.show_bandwidth == 1 && client_world.game.show_bandwidth == 0,
        "+bps did not turn on the host's traffic readout alone"
    );
    require(
        traffic.reset_tick == netstats_tick && netstats_tick != 0,
        "+netstats did not reset the traffic counters"
    );
    require(
        shown(host_chat, "<hoster> +bps") && !shown(client_lines, "<hoster> +bps") &&
            shown(host_chat, "<hoster> +netstats") && !shown(client_lines, "<hoster> +netstats"),
        "the echo of +bps or +netstats was not local to the host"
    );

    struct Readout {
        std::vector<std::string> labels;
        int fills{};
    };

    const auto draw_readout = [&] {
        Readout readout;
        MatchOverlay overlay{};
        overlay.painter = &readout;
        overlay.game = &host_world.game;
        overlay.bottom = kLoopbackOverlayBottom;
        overlay.scale = 1;
        overlay.font_height = [](void*, OverlayFont) -> uint8_t {
            return kLoopbackOverlayFontHeight;
        };
        overlay.draw_text = [](void* painter, OverlayFont, int, int, const char* written, uint8_t) {
            static_cast<Readout*>(painter)->labels.emplace_back(written);
        };
        overlay.fill_rect = [](void* painter, int, int, int, int, uint8_t) {
            ++static_cast<Readout*>(painter)->fills;
        };
        draw_traffic_overlay(overlay, &traffic, nm::net_connection_time(&net_->connection));
        return readout;
    };
    const auto readout = draw_readout();
    const auto labelled = [](const std::string& label, std::string_view start) {
        return label.starts_with(start) && label.ends_with(" K/s");
    };
    require(
        readout.labels.size() == 2 && labelled(readout.labels[0], "Send - ") &&
            labelled(readout.labels[1], "Receive - ") && readout.fills >= 8,
        "the traffic readout did not draw both rates over their bars"
    );
    type_line("+bps");
    const auto hidden = draw_readout();
    require(
        host_world.game.show_bandwidth == 0 && hidden.labels.empty() && hidden.fills == 0,
        "+bps did not take the traffic readout away"
    );
    std::cout << "net loopback check: +bps showed \"" << readout.labels[0] << "\" and \""
              << readout.labels[1] << "\" and hid them again, +netstats reset the counters at tick "
              << netstats_tick << "\n";
    const auto session_step = [&](bool peer_runs) {
        NetHost::loopback_step(*this);
        if (peer_runs)
            NetHost::loopback_step(joiner_play);
        pump_both(kTickPumpWaitMs);
    };
    check_console_session_cheats(joiner, session_step);
    check_console_network_session(joiner, session_step);
    if (watching) {
        // The opponent check passes over a watcher, so the host wins at once.
        require(
            runtime_.match_->outcome() == sim::scenario::Outcome::victory,
            "the host did not win against a watcher"
        );
    } else {
        // The client's commander self-destructs. The host allows watching, so
        // when the client's defeat countdown runs out it watches instead of
        // losing and sends its lobby block again; the host, its
        // only opponent gone, wins.
        const uint16_t commander = NetHost::local_commander(joiner_play);
        joiner.match_->toggle_self_destruct(std::span<const uint16_t>(&commander, 1));
        const auto defeated = [&] {
            return watches(info_for_id(client_world, client_id)) &&
                   watches(info_for_id(host_world, client_id)) &&
                   runtime_.match_->outcome() == sim::scenario::Outcome::victory;
        };
        for (uint32_t tick = 0; tick < kLoopbackDefeatTicks && !defeated(); ++tick) {
            NetHost::loopback_step(*this);
            NetHost::loopback_step(joiner_play);
            pump_both(kTickPumpWaitMs);
        }
        std::cout << "net loopback check: the defeated client watches ("
                  << (watches(info_for_id(client_world, client_id)) ? "yes" : "no") << " / "
                  << (watches(info_for_id(host_world, client_id)) ? "yes" : "no")
                  << " on the host), tick " << client_world.game.tick << "\n";
        require(
            NetHost::local_commander(joiner_play) == 0,
            "the client's commander did not self-destruct"
        );
        require(
            watches(info_for_id(client_world, client_id)), "the defeated client does not watch"
        );
        require(
            joiner.match_->outcome() == sim::scenario::Outcome::ongoing,
            "the defeated client lost instead of watching"
        );
        require(
            watches(info_for_id(host_world, client_id)), "the host was not told the client watches"
        );
        require(
            ui::frontend_dialogs::dialog_kind() ==
                ui::frontend_dialogs::DialogKind::continue_watching,
            "the defeated client did not open the continue-watching prompt"
        );
        require(!joiner.match_paused_, "the watcher prompt paused the network match");
        auto context = joiner.screen_context();
        require(ui::frontend_dialogs::dialog_click(&context, "CHOICE1"), "cannot accept watching");
        require(
            ui::frontend_dialogs::dialog_kind() == ui::frontend_dialogs::DialogKind::none &&
                joiner.match_->outcome() == sim::scenario::Outcome::ongoing,
            "accepting watching did not return to the running match"
        );
        require(
            runtime_.match_->outcome() == sim::scenario::Outcome::victory, "the host did not win"
        );
    }
    // The host's game is over and ends as a finished game does. The outcome
    // shows, but the clock of a shared match keeps stepping it; the end of
    // each frame retries the end of the match, which holds until the final
    // economy settles, and the next frame's network frame and clock step run,
    // until the host leaves the session and its end screen opens. The hold is
    // no pause: the host's tick moves on and it sends no probe. The joiner's
    // side runs between the host's frames.
    const auto finish_host_game = [&](auto&& between_frames, const char* what) {
        runtime_.match_paused_ = true;
        uint32_t held = 0;
        const auto probes_sent = [&] {
            return net_->connection.packets->traffic
                .record_count[static_cast<uint8_t>(oa::netgame::RecordType::probe)]
                             [static_cast<uint8_t>(oa::netgame::TrafficChannel::sent)];
        };
        const auto probes_before = probes_sent();
        const auto tick_before = host_world.game.tick;
        uint32_t probes_held = 0;
        uint32_t tick_held = 0;
        const auto started = std::chrono::steady_clock::now();
        for (;;) {
            runtime_.finish_match_outcome();
            if (!net_->active)
                break;
            const auto waited = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started
            );
            require(waited.count() < kLoopbackWaitMs, what);
            ++held;
            between_frames();
            net_frame();
            NetHost::loopback_step(*this);
            probes_held = probes_sent() - probes_before;
            tick_held = host_world.game.tick - tick_before;
        }
        require(held > 0, "the final economy settled before the other machine answered");
        require(tick_held == held, "the host's tick did not move on while it held on its outcome");
        require(probes_held == 0, "the host sent probes while it held on its outcome");
        require(
            on_screen(runtime_, Screen::campaign_end),
            "the host's finished game did not open its end screen"
        );
        require(
            reporter_world() == &host_world,
            "the end screen does not keep the host's finished match"
        );
        std::cout << "net loopback check: the host held on its outcome for " << held << " frames\n";
    };
    const auto client_slot_on_host = static_cast<std::size_t>(
        std::find_if(
            std::begin(host_world.game.players),
            std::end(host_world.game.players),
            [client_id](const Player& player) {
                return player.in_use != 0 && player.player_id == client_id;
            }
        ) -
        std::begin(host_world.game.players)
    );
    require(client_slot_on_host < OA_PLAYER_COUNT, "the host has no slot for the client");
    if (watching) {
        // The watcher stops answering once the host's game is over: it runs
        // no more frames. Before each of the host's frames, the time the host
        // last heard from it and the timeout scan's baseline move back past
        // the timeout and the drop time, standing in for that long a silence.
        // The host's hold pauses nothing, so its timeout scan names the
        // watcher and drops it as a lost connection (reason 6), and the final
        // economy stops waiting on it.
        constexpr uint32_t kTimeTicksPerSecond = 30; // the connection clock's rate
        auto& silent = net_->net->world->game.players[client_slot_on_host];
        finish_host_game(
            [&] {
                const auto silence =
                    (host_world.game.player_timeout_seconds + nm::timeout_drop_extra_seconds + 1) *
                    kTimeTicksPerSecond;
                const auto now = nm::net_connection_time(&net_->connection);
                silent.last_update_time = now - silence;
                net_->net->timeout_baseline = now - silence;
                NetHost::loopback_pump(*this, 1);
            },
            "the silent watcher held the host on its outcome"
        );
        require(
            silent.reject_reason == 6,
            "the host did not drop the silent watcher as a lost connection"
        );
    } else {
        // The joiner, watching since its defeat, plays on and answers the
        // host's final economy. The host's hold never pauses it. The host
        // leaves with no chat line: the joiner retires its slot as departed
        // (reason 1) and keeps the figures the host's final economy gave,
        // which are the host's own. Leaving changes nothing in the host's
        // finished world.
        bool paused_by_host = false;
        const auto joiner_frame = [&] {
            joiner_play.net_frame();
            NetHost::loopback_step(joiner_play);
            paused_by_host =
                paused_by_host || (client_world.game.sim_run_flags & nm::run_flag_paused) != 0;
            pump_both(kTickPumpWaitMs);
        };
        const auto& host_player = host_world.game.players[host_world.game.local_player_index];
        const auto host_player_count = host_world.game.player_count;
        std::array<uint8_t, sizeof host_player.alliance> host_alliances{};
        std::copy_n(host_player.alliance, host_alliances.size(), host_alliances.begin());
        finish_host_game(joiner_frame, "the host's final economy did not settle");
        const auto& host_copy = client_world.game.players[client_host_slot];
        wait_until(
            [&] {
                joiner_frame();
                return host_copy.in_use == 0;
            },
            "the joiner did not retire the host's slot"
        );
        const auto client_lines_after = joiner.match_message_lines();
        std::cout << "net loopback check: the host left; the joiner retired it with reason "
                  << static_cast<int>(host_copy.reject_reason) << ", host figures "
                  << host_player.kills << "/" << host_player.losses << " kills/losses, "
                  << host_player.energy_produced_total << " energy and "
                  << host_player.metal_produced_total << " metal produced; the joiner's copy "
                  << host_copy.kills << "/" << host_copy.losses << ", "
                  << host_copy.energy_produced_total << ", " << host_copy.metal_produced_total
                  << "\n";
        require(!paused_by_host, "the host's finished game paused the joiner");
        require(host_copy.reject_reason == 1, "the joiner did not retire the host as departed");
        require(
            !shown(client_lines_after, "Player hoster has disconnected"),
            "the host's leave showed a disconnect line"
        );
        require(
            host_copy.kills == host_player.kills && host_copy.losses == host_player.losses &&
                static_cast<float>(host_copy.energy_produced_total) ==
                    static_cast<float>(host_player.energy_produced_total) &&
                static_cast<float>(host_copy.metal_produced_total) ==
                    static_cast<float>(host_player.metal_produced_total),
            "the joiner's figures for the host differ from the host's final ones"
        );
        require(
            host_world.game.player_count == host_player_count &&
                std::equal(
                    host_alliances.begin(), host_alliances.end(), std::begin(host_player.alliance)
                ),
            "leaving changed the host's finished world"
        );
    }
    joiner.leave_match();
    net_bind_multiplayer();
    return 0;
}

} // namespace oa::app
