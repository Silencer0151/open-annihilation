// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/netgame/match/net_match.hpp"

#include "oa/netgame/player_slots.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace oa::netgame::match {
namespace {

constexpr uint8_t no_slot = OA_PLAYER_COUNT;
constexpr uint8_t reject_departed = 1;
constexpr uint8_t reject_creator_left = 10;
constexpr uint8_t reject_connection_lost = 6;
constexpr uint8_t reject_watching = 9;
constexpr uint32_t economy_send_every = 4;
constexpr uint32_t share_period_ticks = 0x3c;
constexpr uint32_t share_sight_period_ticks = 0x1c2;
constexpr float share_metal_fraction = 0.33333334F;
constexpr float share_energy_fraction = 0.5F;
constexpr uint8_t role_host = 0x01; // PlayerSetupInfo.role bits
constexpr uint8_t role_share_metal = 0x02;
constexpr uint8_t role_share_energy = 0x04;
constexpr uint8_t role_share_sight = 0x20;
constexpr uint32_t give_energy = 1;
constexpr uint32_t give_metal = 2;
constexpr uint32_t give_sight = 3;
constexpr uint8_t pause_speed_kind_speed = 1;
constexpr uint32_t host_machine_group = 1; // Player.machine_group of the host's own machine
constexpr uint32_t final_economy_wait_ms = 250;
constexpr uint8_t gui_flag_refresh = 0x01; // Game.gui_flags bit 0: redraw the lobby panel
// PlayerSetupInfo.options bits the published description holds the player count in.
constexpr uint16_t player_count_option_mask = 0x000f;

// Player bytes the match reads, named for their use here.
/// Returns the player's load progress, 0 to 100, as its last load-progress record gave it.
uint8_t& load_progress(Player& p) {
    return p.load_progress;
} // record 0x2a

/// Returns the player's machine flags; bit 0 is set when the player's machine answers a probe.
uint8_t& probe_flags(Player& p) {
    return p.machine_flags;
} // bit 0: 0x07 arrived

uint8_t& start_position(Player& p) {
    return p.start_position;
}

uint8_t* economy_requested(Player& p) {
    return p.economy_requested;
}

uint8_t* economy_processed(Player& p) {
    return p.economy_processed;
}

uint8_t* economy_answered(Player& p) {
    return p.economy_answered;
}

void bump_rx_count(Player& p) {
    uint32_t count = 0;
    std::memcpy(&count, p.update_count, sizeof count);
    ++count;
    std::memcpy(p.update_count, &count, sizeof count);
}

void clear_rx_count(Player& p) {
    std::memset(p.update_count, 0, sizeof p.update_count);
}

bool is_local(const Player& p) {
    return p.in_use != 0 &&
           (p.status == OA_PLAYER_STATUS_LOCAL || p.status == OA_PLAYER_STATUS_COMPUTER);
}

bool is_remote(const Player& p) {
    return p.in_use != 0 && p.status == OA_PLAYER_STATUS_MIRRORED;
}

// Remote slot whose info still reports it playing (info state 1).
bool remote_playing(World* world, Player& p) {
    const auto* info = world_player_info(world, &p);
    return is_remote(p) && info != nullptr && info->state == 1;
}

/// Tells whether a slot is in use by a local, computer or remote player that holds a player index.
///
/// @param p Player record.
/// @return True for an active slot.
bool slot_active(const Player& p) {
    return p.in_use != 0 &&
           (p.status == OA_PLAYER_STATUS_LOCAL || p.status == OA_PLAYER_STATUS_COMPUTER ||
            p.status == OA_PLAYER_STATUS_MIRRORED) &&
           p.index != no_slot;
}

/// Tells whether an active slot still takes part in the game: it holds units, or has never built any.
///
/// @param p Player record.
/// @return False for an inactive slot and for a player whose units are all gone.
bool participating(const Player& p) {
    return slot_active(p) && (p.unit_count != 0 || p.units_created == 0);
}

uint8_t slot_of(const World* world, uint32_t id) {
    return player_slot_of(world->game, id);
}

Player* player_of(World* world, uint32_t id) {
    return player_of_id(world->game, id);
}

/// Returns the id of the first player on this machine, human or computer.
///
/// @param world Match world.
/// @return That player's id, or no_player_id.
uint32_t primary_id(const World* world) {
    for (const auto& p : world->game.players)
        if (is_local(p))
            return p.player_id;
    return no_player_id;
}

/// Finds the in-use slot flagged as the game's host (info role bit 0).
///
/// @param world Match world.
/// @return The slot, or no_slot.
uint8_t host_slot(World* world) {
    for (uint8_t i = 0; i < OA_PLAYER_COUNT; ++i) {
        auto& p = world->game.players[i];
        const auto* info = world_player_info(world, &p);
        if (p.in_use != 0 && info != nullptr && (info->role & role_host) != 0)
            return i;
    }
    return no_slot;
}

/// Tells whether the host slot is simulated on this machine.
///
/// @param world Match world.
/// @return True when the host slot is local or computer.
bool host_is_local(World* world) {
    const auto slot = host_slot(world);
    return slot != no_slot && is_local(world->game.players[slot]);
}

uint32_t now_time(const NetMatch* m) {
    return net_connection_time(m->connection);
}

template <class R>
bool send_record(NetMatch* m, uint32_t from, uint32_t to, const R& record) {
    uint8_t bytes[record_length_table[static_cast<uint8_t>(R::type)]];
    std::size_t written = 0;
    if (encode_record(record, bytes, sizeof bytes, &written) != WireError::ok)
        return false;
    return net_match_send(m, from, to, bytes, written);
}

void flush_now(NetMatch* m) {
    packet_layer_flush(m->connection->packets, now_time(m), true);
}

/// Queues one record to every player of the battle room from one of its players, before a match owns the
/// connection.
///
/// While machines are shared (Game.shared_machines) it goes to one player of
/// each other machine instead, as net_match_send's broadcasts do.
///
/// @param[in,out] c The battle room's connection.
/// @param game The battle room's players.
/// @param from_id Transport id of the sending player.
/// @param record Record bytes, type byte first.
/// @param size Record length in bytes.
void queue_to_all(
    NetConnection* c, const Game& game, uint32_t from_id, const uint8_t* record, std::size_t size
) {
    if (game.shared_machines == 0) {
        net_connection_send_from(c, from_id, broadcast_destination_id, record, size, false);
        return;
    }
    uint32_t ids[OA_PLAYER_COUNT];
    const auto count = ui::frontend_multiplayer::machine_broadcast_targets(game, ids);
    for (int32_t i = 0; i < count; ++i)
        net_connection_send_from(c, from_id, ids[i], record, size, false);
}

/// Tells whether a battle-room player is simulated here and still in the game: a local or computer
/// player not rejected.
///
/// @param p Player record.
/// @return True for a player whose machine this is.
bool building_here(const Player& p) {
    return is_local(p) && p.reject_reason == 0;
}

/// Asks the host for the machine group of every active player still in group 0 (0x21), as the battle
/// room does.
///
/// A local or computer player asks for a group (a computer player for its
/// human's); a player simulated elsewhere asks for the group the host gave
/// it. On the host's own machine its players take group 1 and nothing is
/// sent; only a host simulated elsewhere is asked.
///
/// @param[in,out] m Running match.
void request_machine_groups(NetMatch* m) {
    auto* world = m->world;
    auto& game = world->game;
    const auto host = host_slot(world);
    const bool hosting = host != no_slot && is_local(game.players[host]);
    for (auto& p : game.players) {
        if (!slot_active(p) || p.machine_group != 0)
            continue;
        MachineGroupRequestRecord request{};
        request.player_id = p.player_id;
        request.same_machine_id = no_player_id;
        if (is_local(p)) {
            if (hosting) {
                p.machine_group = host_machine_group;
                continue;
            }
            request.assign = 1;
            if (p.status == OA_PLAYER_STATUS_COMPUTER)
                request.same_machine_id = game.players[game.local_player_index].player_id;
        }
        if (host == no_slot || !is_remote(game.players[host]))
            continue;
        send_record(m, primary_id(world), game.players[host].player_id, request);
    }
}

/// Retires a departed player's slot.
///
/// Local players stop allying with it and its units are destroyed on this
/// machine (NetMatchHooks::destroy_player_units); unless the game has started
/// and the slot is local, it becomes free (and so does its lobby block). The
/// player count drops, its disconnect status and alliances are cleared and
/// its packet channel is released. A departed host of a started game hands
/// the host role on.
///
/// @param[in,out] m Running match.
/// @param id Transport id of the departed player; inactive slots are ignored.
void slot_departure(NetMatch* m, uint32_t id) {
    auto* world = m->world;
    auto* p = player_of(world, id);
    if (p == nullptr || !slot_active(*p))
        return;
    const auto index = p->index;
    auto* info = world_player_info(world, p);
    const bool was_host = p->in_use != 0 && info != nullptr && (info->role & role_host) != 0;
    for (auto& other : world->game.players)
        if (is_local(other) && index < OA_PLAYER_COUNT) {
            other.allied_by[index] = 0;
            other.alliance[index] = 0;
            if (m->hooks.alliance_changed != nullptr && other.index < OA_PLAYER_COUNT)
                m->hooks.alliance_changed(m->hooks.context, world, other.index);
        }
    if (m->hooks.destroy_player_units != nullptr)
        m->hooks.destroy_player_units(m->hooks.context, world, slot_of(world, id));
    const bool started = (world->game.session_flags & kNetFlagGameStarted) != 0;
    if (!(started && is_local(*p))) {
        // The game mirrors the free status into the lobby block.
        p->status = OA_PLAYER_STATUS_FREE;
        if (info != nullptr)
            info->state = OA_PLAYER_STATUS_FREE;
        p->player_id = no_player_id;
        p->in_use = 0;
        p->machine_group = 0;
    }
    if (world->game.player_count > 0)
        --world->game.player_count;
    if (info != nullptr)
        info->status = static_cast<uint16_t>(info->status & ~OA_SETUP_STATUS_HAS_DISC);
    std::memset(p->alliance, 0, sizeof p->alliance);
    packet_layer_release_peer(m->connection->packets, id);
    if (started && was_host)
        net_match_designate_host(world);
}

/// Broadcasts {0x1b, id, reason} and retires the slot or slots.
///
/// Rejecting the local human player rejects every player of this machine.
/// Rejecting a human simulated elsewhere that is still playing (info state
/// 1) retires every slot of its machine group, giving each the reason;
/// rejecting any other player simulated elsewhere, a computer player among
/// them, retires that slot alone. A slot already rejected sends nothing.
///
/// @param[in,out] m Running match.
/// @param id Transport id of the rejected player.
/// @param reason RejectReason value, stored as the slot's reject reason.
/// @return True when a record was sent.
/// @quirk A human whose machine group is still 0 takes every slot of group 0
///        with it, as 3.1c does.
bool reject_player(NetMatch* m, uint32_t id, uint8_t reason) {
    auto* world = m->world;
    auto* p = player_of(world, id);
    if (p == nullptr)
        return false;
    bool sent = false;
    RejectRecord record{};
    record.reason = reason;
    if (is_local(*p) && p->reject_reason == 0) {
        if (p->status == OA_PLAYER_STATUS_LOCAL) {
            for (auto& other : world->game.players) {
                if (!is_local(other))
                    continue;
                record.player_id = other.player_id;
                send_record(m, primary_id(world), broadcast_destination_id, record);
                slot_departure(m, id);
                other.reject_reason = reason;
            }
        } else {
            record.player_id = p->player_id;
            send_record(m, primary_id(world), broadcast_destination_id, record);
            slot_departure(m, p->player_id);
        }
        sent = true;
    } else if (is_remote(*p) && p->reject_reason == 0) {
        record.player_id = id;
        sent = send_record(m, primary_id(world), broadcast_destination_id, record);
        if (remote_playing(world, *p)) {
            const auto group = p->machine_group;
            for (auto& other : world->game.players)
                if (other.machine_group == group) {
                    slot_departure(m, other.player_id);
                    other.reject_reason = reason;
                }
        } else {
            slot_departure(m, p->player_id);
        }
    }
    p->reject_reason = reason;
    return sent;
}

/// Retires a player another machine reported disconnected and passes the 0x1c notice on.
///
/// The notice goes out again from the local player to every player. The slot
/// is retired unless it is local and loading is under way. Only a received
/// notice gets here: a machine leaving the game sends none for its own
/// players, as closing the session tells the others. A notice naming this
/// machine's first local human player then ends the local game
/// (NetMatchHooks::end_local_game).
///
/// @param[in,out] m Running match.
/// @param id Transport id of the disconnected player; unknown ids are ignored.
void disconnect_notice(NetMatch* m, uint32_t id) {
    auto* world = m->world;
    const auto slot = slot_of(world, id);
    if (slot == no_slot)
        return;
    auto& p = world->game.players[slot];
    const auto load = world->game.load_flags;
    if (is_remote(p) || (load & load_flag_started) == 0 || (load & load_flag_loader_done) != 0)
        slot_departure(m, id);
    DisconnectNoticeRecord record{};
    record.player_id = id;
    const auto local = world->game.players[world->game.local_player_index].player_id;
    send_record(m, local, broadcast_destination_id, record);
    if (id == first_local_player_id(world->game) && m->hooks.end_local_game != nullptr)
        m->hooks.end_local_game(m->hooks.context);
}

void notice(NetMatch* m, const char* text) {
    if (m->hooks.notice != nullptr)
        m->hooks.notice(m->hooks.context, text);
}

/// Answers a 0x02 ping request, or records the round trip of a local player's answered ping.
///
/// A request is echoed with this machine's clock to its originator, from the
/// player it was addressed to (the local player for one sent to all), without
/// guaranteed delivery; an answer stores the elapsed milliseconds as the
/// sender's ping.
///
/// @param[in,out] m Running match.
/// @param[in,out] from Sender's player record.
/// @param to Addressee's player record.
/// @param data Record bytes.
/// @param size Record length in bytes.
void handle_ping(
    NetMatch* m, Player& from, const Player& to, const uint8_t* data, std::size_t size
) {
    PingRecord ping{};
    if (decode_record(data, size, &ping) != WireError::ok)
        return;
    const auto now_ms =
        m->connection->host != nullptr ? sock::host_now_ms(m->connection->host) : 0u;
    if (ping.echo_tick_count == 0) {
        ping.echo_tick_count = now_ms;
        auto* packets = m->connection->packets;
        flush_now(m);
        const bool guaranteed = packets->guaranteed;
        packets->guaranteed = false;
        send_record(m, to.player_id, ping.origin_player_id, ping);
        flush_now(m);
        packets->guaranteed = guaranteed;
        return;
    }
    if (const auto* origin = player_of(m->world, ping.origin_player_id);
        origin != nullptr && is_local(*origin))
        from.latency = static_cast<int32_t>(now_ms - ping.origin_tick_count);
}

/// Applies a 0x28 economy record and answers it when the sender wants a reply.
///
/// The sender's scores and resources are stored unless a local player has
/// already processed its final economy. A reply (0x29) goes back from each
/// local player, followed by that player's own economy when not yet sent,
/// unless the local player has already won (NetMatchHooks::local_player_won).
///
/// @param[in,out] m Running match.
/// @param[in,out] sender Sender's player record; ignored once rejected.
/// @param data Record bytes.
/// @param size Record length in bytes.
void apply_economy(NetMatch* m, Player& sender, const uint8_t* data, std::size_t size) {
    EconomyRecord record{};
    if (decode_record(data, size, &record) != WireError::ok || sender.reject_reason != 0)
        return;
    auto* world = m->world;
    const auto index = sender.index;
    if (index >= OA_PLAYER_COUNT)
        return;
    bool processed = false;
    for (auto& p : world->game.players)
        if (is_local(p) && economy_processed(p)[index] != 0)
            processed = true;
    if (!processed) {
        sender.kills = static_cast<int16_t>(record.kills);
        sender.losses = static_cast<int16_t>(record.losses);
        sender.commanders_killed = static_cast<int16_t>(record.commanders_killed);
        sender.commanders_lost = static_cast<int16_t>(record.commanders_lost);
        sender.metal = record.metal;
        sender.energy = record.energy;
        sender.metal_storage = record.metal_storage;
        sender.energy_storage = record.energy_storage;
        sender.energy_produced_total = record.energy_produced_total;
        sender.energy_requested_total = record.energy_requested_total;
        sender.energy_wasted_total = record.energy_wasted_total;
        sender.metal_produced_total = record.metal_produced_total;
        sender.metal_requested_total = record.metal_requested_total;
        sender.metal_wasted_total = record.metal_wasted_total;
    }
    if (record.want_reply == 0)
        return;
    for (auto& p : world->game.players) {
        if (!is_local(p) || p.reject_reason != 0)
            continue;
        economy_processed(p)[index] = 1;
        EconomyReplyRecord reply{};
        reply.mark_requested = 1;
        reply.mark_answered = economy_requested(p)[index] != 0 ? 1 : 0;
        send_record(m, p.player_id, sender.player_id, reply);
        const bool won =
            m->hooks.local_player_won != nullptr && m->hooks.local_player_won(m->hooks.context);
        if (!won && economy_requested(p)[index] == 0) {
            EconomyRecord own{};
            own.want_reply = 1;
            own.kills = p.kills;
            own.losses = p.losses;
            own.commanders_killed = p.commanders_killed;
            own.commanders_lost = p.commanders_lost;
            own.metal = p.metal;
            own.energy = p.energy;
            own.metal_storage = p.metal_storage;
            own.energy_storage = p.energy_storage;
            own.energy_produced_total = static_cast<float>(p.energy_produced_total);
            own.energy_requested_total = static_cast<float>(p.energy_requested_total);
            own.energy_wasted_total = static_cast<float>(p.energy_wasted_total);
            own.metal_produced_total = static_cast<float>(p.metal_produced_total);
            own.metal_requested_total = static_cast<float>(p.metal_requested_total);
            own.metal_wasted_total = static_cast<float>(p.metal_wasted_total);
            if (sender.reject_reason == 0)
                send_record(m, p.player_id, sender.player_id, own);
        }
    }
}

/// Sends a local player's economy snapshot (0x28) to one player, or to every human simulated elsewhere
/// that is still playing.
///
/// @param[in,out] m Running match.
/// @param from Local player whose economy is sent; rejected players send nothing.
/// @param to Receiving player, or null for every human still playing on another machine and not rejected.
/// @param want_reply Nonzero asks the receiver to answer.
void send_economy(NetMatch* m, Player& from, Player* to, uint8_t want_reply) {
    if (!is_local(from) || from.reject_reason != 0)
        return;
    EconomyRecord r{};
    r.want_reply = want_reply;
    r.kills = from.kills;
    r.losses = from.losses;
    r.commanders_killed = from.commanders_killed;
    r.commanders_lost = from.commanders_lost;
    r.metal = from.metal;
    r.energy = from.energy;
    r.metal_storage = from.metal_storage;
    r.energy_storage = from.energy_storage;
    r.energy_produced_total = static_cast<float>(from.energy_produced_total);
    r.energy_requested_total = static_cast<float>(from.energy_requested_total);
    r.energy_wasted_total = static_cast<float>(from.energy_wasted_total);
    r.metal_produced_total = static_cast<float>(from.metal_produced_total);
    r.metal_requested_total = static_cast<float>(from.metal_requested_total);
    r.metal_wasted_total = static_cast<float>(from.metal_wasted_total);
    if (to != nullptr) {
        if (to->reject_reason == 0)
            send_record(m, from.player_id, to->player_id, r);
        return;
    }
    for (auto& p : m->world->game.players)
        if (remote_playing(m->world, p) && p.reject_reason == 0)
            send_record(m, from.player_id, p.player_id, r);
}

/// Applies a 0x16 resource give: credits energy or metal, or shares sight.
///
/// @param[in,out] m Running match.
/// @param data Record bytes.
/// @param size Record length in bytes.
void apply_give(NetMatch* m, const uint8_t* data, std::size_t size) {
    ResourceGiveRecord r{};
    if (decode_record(data, size, &r) != WireError::ok)
        return;
    const auto from = slot_of(m->world, r.from_id);
    const auto to = slot_of(m->world, r.to_id);
    if (from == no_slot || to == no_slot)
        return;
    if ((r.subtype == give_energy || r.subtype == give_metal) && m->hooks.credit != nullptr)
        m->hooks.credit(m->hooks.context, m->world, to, r.subtype == give_metal, r.amount);
    else if (r.subtype == give_sight && m->hooks.share_sight != nullptr)
        m->hooks.share_sight(m->hooks.context, m->world, from, to);
}

/// Copies a player's name into a slot's name field, cut so that it keeps its terminator.
///
/// @param[out] field Name field of player_name_copy_bytes.
/// @param name The name.
void copy_slot_name(char* field, const char* name) {
    std::size_t n = 0;
    for (; name[n] != '\0' && n + 1 < player_name_copy_bytes; ++n)
        field[n] = name[n];
    std::memset(field + n, 0, player_name_copy_bytes - n);
}

/// Handles a player name system message: the player's slot takes the new
/// long name as its name and the new short name as its second name.
///
/// A name the message does not carry leaves its field as it was.
///
/// @param[in,out] m Running match.
/// @param packet The system message.
void apply_player_name(NetMatch* m, const Packet& packet) {
    dplay::PlayerNameView view{};
    if (!dplay::decode_player_name_image(packet.data, packet.size, &view))
        return;
    auto* p = player_of(m->world, view.id);
    if (p == nullptr)
        return;
    if (view.long_name != nullptr)
        copy_slot_name(p->name, view.long_name);
    if (view.short_name != nullptr)
        copy_slot_name(p->second_name, view.short_name);
}

/// Handles a player data system message: the data replaces the player's
/// setup block. When this machine's player is the game's host, its game does
/// not allow watching and the new block makes the player a watcher, the
/// player is rejected with reason 9.
///
/// Data shorter than the block replaces only its first bytes.
///
/// @param[in,out] m Running match.
/// @param packet The system message.
void apply_player_data(NetMatch* m, const Packet& packet) {
    dplay::PlayerDataView view{};
    if (!dplay::decode_player_data_image(packet.data, packet.size, &view))
        return;
    auto* world = m->world;
    auto* p = player_of(world, view.id);
    auto* info = p != nullptr ? world_player_info(world, p) : nullptr;
    if (info == nullptr)
        return;
    const std::size_t size =
        view.data_size < player_data_block_bytes ? view.data_size : player_data_block_bytes;
    if (size != 0)
        std::memcpy(static_cast<void*>(info), view.data, size);
    const auto local = world->game.local_player_index;
    if (host_slot(world) != local)
        return;
    const auto* host_info = world_player_info(world, &world->game.players[local]);
    if (host_info != nullptr && (host_info->options & OA_SETUP_OPTION_WATCHING_ALLOWED) == 0 &&
        (info->options & OA_SETUP_OPTION_WATCHER) != 0)
        reject_player(m, view.id, reject_watching);
}

/// Handles a system message addressed to this machine's player.
///
/// A destroyed player departs; when the game's creator leaves before the
/// game started, the local player is rejected too (creator left). A
/// player's name and data changes are applied to its slot. Other messages
/// are ignored, among them this machine becoming the session's name server.
///
/// @param[in,out] m Running match.
/// @param packet The system message.
void dispatch_system(NetMatch* m, const Packet& packet) {
    uint32_t message_type = 0;
    if (!dplay::read_system_message_type(packet.data, packet.size, &message_type))
        return;
    const auto type = static_cast<SystemMessageType>(message_type);
    if (type == SystemMessageType::player_name_changed) {
        apply_player_name(m, packet);
        return;
    }
    if (type == SystemMessageType::player_data_changed) {
        apply_player_data(m, packet);
        return;
    }
    dplay::PlayerDestroyedView destroyed{};
    if (!dplay::decode_player_destroyed_image(packet.data, packet.size, &destroyed) ||
        destroyed.player_type != dplay::system_message::player_type_player)
        return;
    auto* world = m->world;
    const auto id = destroyed.id;
    auto* p = player_of(world, id);
    if (p == nullptr || !slot_active(*p))
        return;
    const auto* info = world_player_info(world, p);
    const bool creator = info != nullptr && (info->role & 0x01) != 0;
    if ((world->game.session_flags & kNetFlagGameStarted) == 0 && creator && is_remote(*p)) {
        reject_player(m, id, reject_departed);
        auto& local = world->game.players[world->game.local_player_index];
        reject_player(m, local.player_id, reject_creator_left);
        local.status = OA_PLAYER_STATUS_FREE;
    } else {
        reject_player(m, id, reject_departed);
    }
    p->status = OA_PLAYER_STATUS_FREE;
    packet_layer_release_peer(m->connection->packets, id);
}

/// Dispatches one admitted record from a remote player.
///
/// @param[in,out] m Running match.
/// @param[in,out] from Sender's player record.
/// @param[in,out] to Addressee's player record (the local player for a broadcast).
/// @param packet The record.
/// @return False for a 0x08 start record, which ends the pump.
bool dispatch_record(NetMatch* m, Player& from, Player& to, const Packet& packet) {
    auto* world = m->world;
    const auto* data = packet.data;
    const auto size = static_cast<std::size_t>(packet.size);
    const auto type = static_cast<RecordType>(data[0]);
    switch (type) {
    case RecordType::ping:
        handle_ping(m, from, to, data, size);
        break;
    case RecordType::chat:
        if (is_local(to) && to.status == OA_PLAYER_STATUS_LOCAL && m->hooks.chat != nullptr &&
            size >= 1) {
            char text[sizeof(ChatRecord::text) + 1]{};
            std::memcpy(
                text,
                data + 1,
                size - 1 < sizeof(ChatRecord::text) ? size - 1 : sizeof(ChatRecord::text)
            );
            m->hooks.chat(m->hooks.context, from.index, text);
        }
        break;
    case RecordType::probe: {
        const uint8_t reply = static_cast<uint8_t>(RecordType::probe_reply);
        net_match_send(m, primary_id(world), from.player_id, &reply, 1);
        break;
    }
    case RecordType::probe_reply:
        probe_flags(from) |= 1;
        break;
    case RecordType::game_start:
        world->game.session_flags =
            static_cast<uint8_t>(world->game.session_flags | kNetFlagGameStarted);
        return false;
    case RecordType::unit_created:
    case RecordType::unit_link:
    case RecordType::unit_damage:
    case RecordType::unit_killed:
    case RecordType::weapon_fire:
    case RecordType::projectile_intercepted:
    case RecordType::feature_event:
    case RecordType::cob_start:
    case RecordType::unit_state_flags:
    case RecordType::builder_link:
    case RecordType::sound:
    case RecordType::unit_transfer:
    case RecordType::unit_state: {
        bool handled = false;
        const auto error =
            replication_apply_record(world, &m->sim, from.index, data, size, &handled);
        if (error != WireError::ok)
            ++m->record_errors;
        else if (handled)
            ++m->records_applied;
        break;
    }
    case RecordType::loaded:
        if ((world->game.load_flags & load_flag_barrier) != 0 && from.index < OA_PLAYER_COUNT)
            m->barrier.loaded[from.index] = 1;
        break;
    case RecordType::resource_give:
        apply_give(m, data, size);
        break;
    case RecordType::player_value_reply: {
        PlayerValueReplyRecord r{};
        if (decode_record(data, size, &r) == WireError::ok)
            if (auto* info = world_player_info(world, &to))
                info->color = r.value;
        break;
    }
    case RecordType::pause_speed: {
        PauseSpeedRecord r{};
        if (decode_record(data, size, &r) != WireError::ok)
            break;
        if (r.kind == pause_speed_kind_pause)
            world->game.sim_run_flags = static_cast<uint16_t>(
                (world->game.sim_run_flags & ~run_flag_paused) | (r.value & run_flag_paused)
            );
        else
            net_match_set_speed(m, r.value, false);
        break;
    }
    case RecordType::reject: {
        RejectRecord r{};
        if (decode_record(data, size, &r) == WireError::ok)
            if (auto* target = player_of(world, r.player_id))
                reject_player(m, target->player_id, r.reason);
        break;
    }
    case RecordType::disconnect_notice: {
        DisconnectNoticeRecord r{};
        if (decode_record(data, size, &r) != WireError::ok)
            break;
        if (const auto* p = player_of(world, r.player_id)) {
            char line[80];
            std::snprintf(line, sizeof line, "Player %.30s has disconnected", p->name);
            notice(m, line);
            disconnect_notice(m, r.player_id);
        }
        break;
    }
    case RecordType::start_position: {
        StartPositionRecord r{};
        if (decode_record(data, size, &r) != WireError::ok)
            break;
        start_position(to) = r.position;
        StartPositionAckRecord ack{};
        ack.player_id = to.player_id;
        send_record(m, to.player_id, from.player_id, ack);
        break;
    }
    case RecordType::start_position_ack: {
        StartPositionAckRecord r{};
        if (decode_record(data, size, &r) == WireError::ok) {
            const auto slot = slot_of(world, r.player_id);
            if (slot != no_slot)
                m->barrier.start_acked[slot] = 1;
        }
        break;
    }
    case RecordType::player_info: {
        PlayerInfoRecord r{};
        if (decode_record(data, size, &r) != WireError::ok)
            break;
        const auto slot = slot_of(world, r.player_id);
        if (slot != no_slot && is_remote(world->game.players[slot])) {
            auto* bytes = reinterpret_cast<uint8_t*>(&world->player_info[slot]);
            std::memcpy(bytes, data + 1, player_info_block_bytes);
            ui::frontend_multiplayer::note_shared_machines(world->game);
        }
        break;
    }
    case RecordType::slot_table: {
        SlotTableRecord r{};
        if (decode_record(data, size, &r) != WireError::ok)
            break;
        std::memcpy(world->game.slot_table, r.slot_ids, sizeof world->game.slot_table);
        world->game.gui_flags = static_cast<uint8_t>(world->game.gui_flags | gui_flag_refresh);
        break;
    }
    case RecordType::alliance: {
        AllianceRecord r{};
        if (decode_record(data, size, &r) != WireError::ok)
            break;
        auto* a = player_of(world, r.player_id_a);
        auto* b = player_of(world, r.player_id_b);
        if (a == nullptr || b == nullptr || a->index >= OA_PLAYER_COUNT ||
            b->index >= OA_PLAYER_COUNT)
            break;
        if (is_local(*b))
            b->allied_by[a->index] = r.value;
        a->alliance[b->index] = r.value;
        if (m->hooks.alliance_changed != nullptr)
            m->hooks.alliance_changed(m->hooks.context, world, a->index);
        break;
    }
    case RecordType::player_team: {
        PlayerTeamRecord r{};
        if (decode_record(data, size, &r) == WireError::ok)
            if (auto* p = player_of(world, r.player_id))
                p->team = r.value;
        break;
    }
    case RecordType::integrity_notice: {
        IntegrityNoticeRecord r{};
        if (decode_record(data, size, &r) == WireError::ok)
            if (const auto* p = player_of(world, r.player_id)) {
                char line[96];
                std::snprintf(
                    line,
                    sizeof line,
                    "%.30s has modified his executable. Game integrity breached.",
                    p->name
                );
                notice(m, line);
            }
        break;
    }
    case RecordType::economy:
        apply_economy(m, from, data, size);
        break;
    case RecordType::economy_reply: {
        EconomyReplyRecord r{};
        if (decode_record(data, size, &r) == WireError::ok && r.mark_requested != 0 &&
            from.index < OA_PLAYER_COUNT) {
            economy_requested(to)[from.index] = 1;
            if (r.mark_answered != 0)
                economy_answered(to)[from.index] = 1;
        }
        break;
    }
    case RecordType::load_progress: {
        LoadProgressRecord r{};
        if (decode_record(data, size, &r) == WireError::ok)
            load_progress(from) = r.percent;
        break;
    }
    default:
        ++m->records_refused;
        break;
    }
    return true;
}

/// Shuffles start positions with the match's rand15 hook.
///
/// Each element from the second on swaps with one before it, chosen as
/// rand() % its index. Does nothing without a rand hook.
///
/// @param m Running match providing the rand hook.
/// @param[in,out] first Positions to shuffle.
/// @param count Number of positions.
void shuffle(NetMatch* m, int32_t* first, int32_t count) {
    if (m->hooks.rand15 == nullptr)
        return;
    for (int32_t i = 1, span = 1; i < count; ++i, ++span) {
        const auto r = static_cast<uint32_t>(m->hooks.rand15(m->hooks.context)) & 0x7fffu;
        const auto j = static_cast<int32_t>(r % static_cast<uint32_t>(span));
        const auto held = first[i];
        first[i] = first[j];
        first[j] = held;
    }
}

bool watcher(World* world, Player& p) {
    const auto* info = world_player_info(world, &p);
    return p.in_use != 0 && info != nullptr && (info->options & OA_SETUP_OPTION_WATCHER) != 0;
}

/// Runs one step of the load barrier.
///
/// On the host's machine the start positions are assigned once (shuffled
/// unless fixed; with fewer than three players a coin flip decides whether
/// to shuffle) and sent to every remote player until acknowledged. Local
/// players then broadcast 0x15 "loaded".
///
/// @param[in,out] m Running match.
/// @return True when every remote player has loaded and, on the host, acknowledged its start position.
bool barrier_step(NetMatch* m) {
    auto* world = m->world;
    auto& game = world->game;
    auto& b = m->barrier;
    const bool assigning = host_is_local(world);
    if (assigning && !b.assigned) {
        const auto& local = game.players[game.local_player_index];
        const auto* local_info = world_player_info(world, &local);
        const bool fixed =
            local_info != nullptr && (local_info->options & OA_SETUP_OPTION_FIXED_LOCATIONS) != 0;
        int32_t next = 0;
        if (!fixed) {
            int32_t order[OA_PLAYER_COUNT];
            int32_t count = 0;
            for (int32_t i = 0; i < OA_PLAYER_COUNT; ++i)
                order[i] = -1;
            for (int32_t i = 0; i < OA_PLAYER_COUNT; ++i) {
                auto& p = game.players[i];
                if (slot_active(p) && !watcher(world, p)) {
                    order[count] = count;
                    ++count;
                }
            }
            bool mix = true;
            if (count < 3 && m->hooks.rand15 != nullptr)
                mix = (m->hooks.rand15(m->hooks.context) * 2) / 0x8000 != 0;
            if (mix)
                shuffle(m, order, count);
            for (int32_t i = 0; i < OA_PLAYER_COUNT; ++i) {
                auto& p = game.players[i];
                b.start_assignment[i] = slot_active(p) && !watcher(world, p) ? order[next++] : -1;
            }
        } else {
            for (int32_t i = 0; i < OA_PLAYER_COUNT; ++i) {
                auto& p = game.players[i];
                b.start_assignment[i] = slot_active(p) && !watcher(world, p) ? next++ : -1;
            }
        }
        b.assigned = true;
    }
    bool ready = true;
    for (int32_t i = 0; i < OA_PLAYER_COUNT && ready; ++i) {
        if (!is_remote(game.players[i]))
            continue;
        ready = b.loaded[i] != 0 && (!assigning || b.start_acked[i] != 0);
    }
    if (assigning) {
        for (int32_t i = 0; i < OA_PLAYER_COUNT; ++i) {
            auto& p = game.players[i];
            if (b.start_acked[i] != 0 || p.in_use == 0)
                continue;
            const auto position = static_cast<uint8_t>(b.start_assignment[i]);
            if (is_remote(p)) {
                StartPositionRecord record{};
                record.position = position;
                send_record(m, first_local_player_id(game), p.player_id, record);
            } else if (is_local(p)) {
                start_position(p) = position;
                b.start_acked[i] = 1;
            }
        }
    }
    if (!assigning || ready)
        for (auto& p : game.players)
            if (is_local(p)) {
                const uint8_t loaded = static_cast<uint8_t>(RecordType::loaded);
                net_match_send(m, p.player_id, broadcast_destination_id, &loaded, 1);
            }
    return ready;
}

} // namespace

void net_match_begin(
    NetMatch* m,
    NetConnection* connection,
    World* world,
    const ReplicationSim& sim,
    const NetMatchHooks& hooks
) noexcept {
    *m = NetMatch{};
    m->connection = connection;
    m->world = world;
    m->sim = sim;
    m->hooks = hooks;
    m->phase = NetPhase::loading;
    connection->packets->receiver.players = world->game.players;
    connection->packets->send_options = &world->game;
    auto& game = world->game;
    game.session_flags = static_cast<uint8_t>(game.session_flags | kNetFlagLive);
    if (game.player_timeout_seconds == 0)
        game.player_timeout_seconds = default_timeout_seconds;
    for (int32_t i = 0; i < OA_PLAYER_COUNT; ++i) {
        auto& p = game.players[i];
        m->barrier.loaded[i] = is_local(p) ? 1u : 0u;
        m->barrier.start_acked[i] = watcher(world, p) ? 1u : 0u;
        m->barrier.start_assignment[i] = -1;
    }
    game.load_flags = static_cast<uint16_t>(game.load_flags | load_flag_started);
    m->timeout_baseline = now_time(m);
    m->keepalive_time = now_time(m);
}

void net_match_send_game_start(NetMatch* m) noexcept {
    auto* world = m->world;
    const auto& local = world->game.players[world->game.local_player_index];
    const auto* info = world_player_info(world, &local);
    if (info == nullptr || (info->role & role_host) == 0)
        return;
    const uint8_t start = static_cast<uint8_t>(RecordType::game_start);
    net_match_send(m, local.player_id, broadcast_destination_id, &start, 1);
}

void net_match_queue_game_start(NetConnection* c, const Game& game) noexcept {
    if (!c->in_session || game.local_player_index >= OA_PLAYER_COUNT)
        return;
    const uint8_t start = static_cast<uint8_t>(RecordType::game_start);
    queue_to_all(c, game, game.players[game.local_player_index].player_id, &start, 1);
}

bool loading_frame_due(LoadingPace* pace, uint32_t now) noexcept {
    if (pace->ran && now - pace->time < loading_frame_ticks)
        return false;
    pace->ran = true;
    pace->time = now;
    return true;
}

void net_match_building_frame(
    NetConnection* c, const Game& game, const uint8_t rows[load_progress_rows]
) noexcept {
    if (!c->in_session)
        return;
    if (c->host != nullptr)
        sock::host_pump(c->host, 0);
    const uint8_t probe = static_cast<uint8_t>(RecordType::probe);
    for (const auto& p : game.players)
        if (building_here(p)) {
            queue_to_all(c, game, p.player_id, &probe, 1);
            net_connection_flush(c);
        }
    int32_t sum = 0;
    for (std::size_t row = 0; row < load_progress_rows; ++row)
        sum += rows[row];
    LoadProgressRecord record{};
    record.percent = static_cast<uint8_t>(sum / static_cast<int32_t>(load_progress_rows));
    uint8_t bytes[record_length_table[static_cast<uint8_t>(RecordType::load_progress)]];
    std::size_t written = 0;
    if (encode_record(record, bytes, sizeof bytes, &written) == WireError::ok)
        for (const auto& p : game.players)
            if (building_here(p))
                queue_to_all(c, game, p.player_id, bytes, written);
    net_connection_flush(c);
}

void net_match_loader_waiting(NetMatch* m) noexcept {
    auto& game = m->world->game;
    game.load_flags = static_cast<uint16_t>(game.load_flags | load_flag_barrier);
}

bool net_match_loading_frame(NetMatch* m) noexcept {
    auto& game = m->world->game;
    if ((game.load_flags & load_flag_barrier_passed) != 0)
        return true;
    for (auto& p : game.players)
        if (is_local(p)) {
            const uint8_t probe = static_cast<uint8_t>(RecordType::probe);
            net_match_send(m, p.player_id, broadcast_destination_id, &probe, 1);
            flush_now(m);
        }
    (void)net_match_pump(m);
    if ((game.load_flags & load_flag_barrier) != 0 && barrier_step(m)) {
        game.load_flags = static_cast<uint16_t>(
            (game.load_flags & ~load_flag_barrier) | load_flag_barrier_passed
        );
        (void)session::session_set_guaranteed(&m->connection->session, false);
        m->connection->packets->guaranteed = false;
    }
    flush_now(m);
    return (game.load_flags & load_flag_barrier_passed) != 0;
}

bool net_match_paced_loading_frame(
    NetMatch* m, LoadingPace* pace, const uint8_t rows[load_progress_rows]
) noexcept {
    if ((m->world->game.load_flags & load_flag_barrier_passed) != 0) {
        if (m->frames_since_barrier < commander_wait_frames &&
            loading_frame_due(pace, now_time(m))) {
            if (m->connection->host != nullptr)
                sock::host_pump(m->connection->host, 0);
            ++m->frames_since_barrier;
        }
        return m->frames_since_barrier >= commander_wait_frames;
    }
    if (!loading_frame_due(pace, now_time(m)))
        return false;
    const bool ready = net_match_loading_frame(m);
    net_match_send_load_progress(m, rows);
    return ready && m->frames_since_barrier >= commander_wait_frames;
}

void net_match_end_loading(NetMatch* m) noexcept {
    auto* world = m->world;
    auto& local = world->game.players[world->game.local_player_index];
    if (auto* info = world_player_info(world, &local))
        info->options = static_cast<uint16_t>(info->options | OA_SETUP_OPTION_STARTED);
    net_match_send_player_status(m, true);
    net_match_republish(m);
    net_match_economy_period(m);
}

void net_match_republish(NetMatch* m) noexcept {
    auto* c = m->connection;
    if (c == nullptr || !c->hosting)
        return;
    auto* world = m->world;
    auto& game = world->game;
    auto* info = world_player_info(world, &game.players[game.local_player_index]);
    if (info == nullptr) {
        net_connection_republish(c, nullptr);
        return;
    }
    info->options = static_cast<uint16_t>(
        (info->options & ~player_count_option_mask) | (game.player_count & player_count_option_mask)
    );
    PlayerSetupInfo published = *info;
    published.status =
        static_cast<uint16_t>(published.status & ~ui::frontend_multiplayer::status::launch_only);
    uint8_t user[ui::frontend_multiplayer::kSessionUserBytes];
    std::memcpy(
        user,
        reinterpret_cast<const uint8_t*>(&published) +
            ui::frontend_multiplayer::kSessionUserInfoOffset,
        sizeof user
    );
    // The description says the game has started from then on.
    if ((published.options & OA_SETUP_OPTION_STARTED) != 0)
        c->session.desc.flags |= session_flag_join_disabled;
    net_connection_republish(c, user);
}

uint8_t net_match_start_position(const NetMatch* m, uint8_t slot) noexcept {
    if (slot >= OA_PLAYER_COUNT)
        return no_start_position;
    return m->world->game.players[slot].start_position;
}

void net_match_send_load_progress(NetMatch* m, const uint8_t rows[load_progress_rows]) noexcept {
    int32_t sum = 0;
    for (std::size_t row = 0; row < load_progress_rows; ++row)
        sum += rows[row];
    LoadProgressRecord record{};
    record.percent = static_cast<uint8_t>(sum / static_cast<int32_t>(load_progress_rows));
    for (int32_t slot = 0; slot < OA_PLAYER_COUNT; ++slot) {
        auto& p = m->world->game.players[slot];
        if (!is_local(p))
            continue;
        load_progress(p) = record.percent;
        send_record(m, p.player_id, broadcast_destination_id, record);
    }
}

void net_match_loading_screen_status(const NetMatch* m, LoadingScreenStatus* out) noexcept {
    *out = LoadingScreenStatus{};
    auto& game = m->world->game;
    if ((game.load_flags & load_flag_barrier_passed) != 0) {
        std::snprintf(out->text, sizeof out->text, "%s", "Synchronization complete");
        return;
    }
    int32_t active = 0;
    for (int32_t slot = 0; slot < OA_PLAYER_COUNT; ++slot) {
        auto& p = game.players[slot];
        if (!slot_active(p))
            continue;
        ++active;
        if (load_progress(p) == load_progress_complete && m->barrier.loaded[slot] != 0)
            ++out->ready;
    }
    // The local player is always active, so the count is not zero here.
    const int32_t share = active != 0 ? loading_bars_width / active : 0;
    const int32_t width = share - loading_bar_gap;
    int32_t left = loading_bars_left;
    for (int32_t slot = 0; slot < OA_PLAYER_COUNT && active != 0; ++slot) {
        auto& p = game.players[slot];
        if (!slot_active(p))
            continue;
        auto& bar = out->bars[out->bar_count++];
        bar.player = &p;
        bar.left = left;
        bar.right = left + width;
        bar.filled = left + load_progress(p) * width / int32_t{load_progress_complete};
        left += share;
    }
    std::snprintf(
        out->text,
        sizeof out->text,
        "%s.  %i %s",
        "Waiting for other players",
        out->ready,
        out->ready == 1 ? "player ready" : "players ready"
    );
}

void net_match_enter_game(NetMatch* m) noexcept {
    auto& game = m->world->game;
    game.load_flags = static_cast<uint16_t>(game.load_flags | load_flag_loader_done);
    m->phase = NetPhase::in_game;
    m->timeout_baseline = now_time(m);
}

uint32_t net_match_pump(NetMatch* m) noexcept {
    auto* world = m->world;
    if ((world->game.session_flags & kNetFlagLive) == 0)
        return 0;
    for (auto& p : world->game.players)
        clear_rx_count(p);
    uint32_t count = 0;
    Packet packet{};
    const auto phase_bit = static_cast<uint8_t>(m->phase);
    const auto local_id = world->game.players[world->game.local_player_index].player_id;
    while (packet_layer_receive(
        m->connection->packets, static_cast<int32_t>(world->game.tick), &packet
    )) {
        ++count;
        const auto to_id = packet.to_id != broadcast_destination_id ? packet.to_id : local_id;
        auto* to = player_of(world, to_id);
        if (packet.kind == PacketKind::system) {
            if (to != nullptr && to->status == OA_PLAYER_STATUS_LOCAL)
                dispatch_system(m, packet);
            continue;
        }
        if (packet.size == 0 || !is_record_type(packet.data[0]) ||
            (record_phase_table[packet.data[0]] & phase_bit) == 0) {
            ++m->records_refused;
            continue;
        }
        auto* from = player_of(world, packet.from_id);
        if (from == nullptr) {
            ++m->records_refused;
            continue;
        }
        if (is_local(*from))
            continue;
        if (!is_remote(*from) || from->index == no_slot) {
            reject_player(m, packet.from_id, reject_connection_lost);
            continue;
        }
        if (to == nullptr || !slot_active(*to)) {
            ++m->records_refused;
            continue;
        }
        bump_rx_count(*from);
        from->last_update_time = now_time(m);
        if (!dispatch_record(m, *from, *to, packet))
            break;
    }
    ui::frontend_multiplayer::note_shared_machines(world->game);
    m->timeout_player = net_match_check_timeouts(m);
    return count;
}

bool net_match_send(
    NetMatch* m, uint32_t from_id, uint32_t to_id, const uint8_t* record, std::size_t size
) noexcept {
    auto* world = m->world;
    if ((world->game.session_flags & kNetFlagLive) == 0 || record == nullptr || size == 0)
        return false;
    const auto* from = player_of(world, from_id);
    if (from == nullptr || !is_local(*from) || from->reject_reason != 0)
        return false;
    if (to_id == broadcast_destination_id && world->game.shared_machines != 0) {
        uint32_t ids[OA_PLAYER_COUNT];
        const auto count = ui::frontend_multiplayer::machine_broadcast_targets(world->game, ids);
        for (int32_t i = 0; i < count; ++i)
            (void)net_match_send(m, from_id, ids[i], record, size);
        return true;
    }
    if (to_id != broadcast_destination_id) {
        const auto* to = player_of(world, to_id);
        if (to == nullptr || !is_remote(*to) || to->reject_reason != 0)
            return false;
    }
    return packet_layer_send(m->connection->packets, from_id, to_id, record, size, now_time(m)) ==
           WireError::ok;
}

void net_match_send_player_state(NetMatch* m, Player* player) noexcept {
    auto* world = m->world;
    if (player == nullptr || !is_local(*player) || player->index >= OA_PLAYER_COUNT)
        return;
    uint8_t storage[unit_state_writer_words * bit_stream_word_bytes];
    BitWriter writer;
    bit_writer_init(&writer, storage, unit_state_writer_words);
    uint16_t length = 0;
    if (replication_pack_player(world, &m->sim, player->index, &writer, &length) != WireError::ok) {
        ++m->record_errors;
        return;
    }
    net_match_send(m, player->player_id, broadcast_destination_id, storage, length);
}

void net_match_send_player_status(NetMatch* m, bool machine_groups) noexcept {
    auto* world = m->world;
    if ((world->game.session_flags & kNetFlagLive) == 0)
        return;
    for (auto& p : world->game.players) {
        const auto* info = world_player_info(world, &p);
        if (!is_local(p) || info == nullptr)
            continue;
        PlayerInfoRecord status{};
        const auto* bytes = reinterpret_cast<const uint8_t*>(info);
        std::memcpy(status.info_head, bytes, sizeof status.info_head);
        status.player_id = p.player_id;
        std::memcpy(status.info_tail, bytes + player_info_tail_offset, sizeof status.info_tail);
        send_record(m, p.player_id, broadcast_destination_id, status);
        PlayerTeamRecord team{};
        team.player_id = p.player_id;
        team.value = p.team;
        send_record(m, p.player_id, broadcast_destination_id, team);
        flush_now(m);
    }
    if (machine_groups)
        request_machine_groups(m);
    flush_now(m);
}

void net_match_send_alliance(NetMatch* m, uint8_t from, uint8_t to, uint8_t allied) noexcept {
    if (from >= OA_PLAYER_COUNT || to >= OA_PLAYER_COUNT)
        return;
    const auto& giver = m->world->game.players[from];
    const auto& other = m->world->game.players[to];
    AllianceRecord record{};
    record.player_id_a = giver.player_id;
    record.player_id_b = other.player_id;
    record.value = allied;
    if (send_record(m, giver.player_id, other.player_id, record))
        flush_now(m);
}

void net_match_remove_player(NetMatch* m, uint8_t slot, uint8_t reason) noexcept {
    if (slot >= OA_PLAYER_COUNT)
        return;
    const auto& p = m->world->game.players[slot];
    if (p.in_use != 0)
        (void)reject_player(m, p.player_id, reason);
}

void net_match_send_unit_created(NetMatch* m, const Unit* unit) noexcept {
    auto* world = m->world;
    if (unit == nullptr)
        return;
    const auto* owner = world_player_ref(world, unit->owner);
    if (owner == nullptr || !is_local(*owner))
        return;
    UnitCreatedRecord r{};
    r.unit_def_index = unit->type_index;
    r.unit_index = unit->id;
    r.position[0] = unit->position.x;
    r.position[1] = unit->position.y;
    r.position[2] = unit->position.z;
    r.bank_heading =
        static_cast<uint16_t>(unit->bank) | (static_cast<uint32_t>(unit->heading) << 16);
    r.pitch = static_cast<uint16_t>(unit->pitch);
    send_record(m, owner->player_id, broadcast_destination_id, r);
}

void net_match_send_builder_link(NetMatch* m, const Unit* source, const Unit* subject) noexcept {
    auto* world = m->world;
    if (source == nullptr)
        return;
    const auto* owner = world_player_ref(world, source->owner);
    if (owner == nullptr || !is_local(*owner))
        return;
    BuilderLinkRecord r{};
    r.subject_unit_index = subject != nullptr ? subject->id : 0;
    r.source_unit_index = source->id;
    send_record(m, owner->player_id, broadcast_destination_id, r);
}

void net_match_send_damage(NetMatch* m, uint32_t route, const UnitDamageRecord& record) noexcept {
    send_record(m, route, broadcast_destination_id, record);
}

uint32_t net_match_local_route(const NetMatch* m) noexcept {
    return primary_id(m->world);
}

uint32_t net_match_host_id(const NetMatch* m) noexcept {
    const auto slot = host_slot(m->world);
    return slot != no_slot ? m->world->game.players[slot].player_id : no_player_id;
}

namespace {

/// Tells whether a slot may receive the local player's shared resources and sight.
///
/// @param world Match world.
/// @param self Local player sharing.
/// @param slot Slot of the candidate, 0..9.
/// @return True for a human simulated elsewhere that is still playing and
///         taking part, set in the sharing player's alliance row.
bool share_target(World* world, const Player& self, std::size_t slot) {
    auto& p = world->game.players[slot];
    return participating(p) && remote_playing(world, p) && self.alliance[slot] != 0;
}

/// Shares resources and sight with allies as the player's lobby options ask.
///
/// Every 60 ticks a third of the metal above Player.metal_share_threshold
/// and half of the energy above Player.energy_share_threshold go to a
/// share_target player, capped at its free storage; every 450 ticks sight
/// goes to each share_target player.
///
/// @param[in,out] m Running match.
/// @param self Local player sharing.
/// @quirk The resources go to the last share_target player in slot order
///        whose store is below the sharing player's, not to the poorest.
void share_resources(NetMatch* m, Player& self) {
    auto* world = m->world;
    const auto tick = world->game.tick;
    const auto* info = world_player_info(world, &self);
    if (info == nullptr || self.index >= OA_PLAYER_COUNT)
        return;
    if (tick % share_period_ticks == 0) {
        if ((info->role & role_share_metal) != 0 && self.metal > self.metal_share_threshold) {
            Player* target = &self;
            for (std::size_t slot = 0; slot < OA_PLAYER_COUNT; ++slot)
                if (share_target(world, self, slot) && world->game.players[slot].metal < self.metal)
                    target = &world->game.players[slot];
            if (target != &self) {
                auto amount = (self.metal - self.metal_share_threshold) * share_metal_fraction;
                if (target->metal_storage - target->metal <= amount)
                    amount = target->metal_storage - target->metal;
                net_match_give(m, self.index, target->index, true, amount);
            }
        }
        if ((info->role & role_share_energy) != 0 && self.energy > self.energy_share_threshold) {
            Player* target = &self;
            for (std::size_t slot = 0; slot < OA_PLAYER_COUNT; ++slot)
                if (share_target(world, self, slot) &&
                    world->game.players[slot].energy < self.energy)
                    target = &world->game.players[slot];
            if (target != &self) {
                auto amount = (self.energy - self.energy_share_threshold) * share_energy_fraction;
                if (target->energy_storage - target->energy <= amount)
                    amount = target->energy_storage - target->energy;
                net_match_give(m, self.index, target->index, false, amount);
            }
        }
    }
    if (tick % share_sight_period_ticks == 0 && (info->role & role_share_sight) != 0)
        for (std::size_t slot = 0; slot < OA_PLAYER_COUNT; ++slot)
            if (share_target(world, self, slot))
                net_match_share_sight(m, self.index, world->game.players[slot].index);
}

} // namespace

void net_match_after_tick(NetMatch* m) noexcept {
    auto& game = m->world->game;
    auto& local = game.players[game.local_player_index];
    if (is_local(local))
        share_resources(m, local);
    packet_layer_flush(m->connection->packets, now_time(m), false);
}

void net_match_paused_frame(NetMatch* m) noexcept {
    packet_layer_flush(m->connection->packets, now_time(m), false);
    (void)net_match_pump(m);
    const auto now = now_time(m);
    // The next probe is due once the clock passes keepalive_time, which is
    // never set more than keepalive_interval ahead of it: a reading further
    // from it has passed it, or turned over to 0 since it was set.
    if (m->keepalive_time - now > keepalive_interval) {
        m->keepalive_time = now + keepalive_interval;
        const uint8_t probe = static_cast<uint8_t>(RecordType::probe);
        net_match_send(m, primary_id(m->world), broadcast_destination_id, &probe, 1);
    }
}

uint32_t net_match_check_timeouts(NetMatch* m) noexcept {
    auto& game = m->world->game;
    // "Drop 0" skips the scan, the paused baseline and the dialog call alike.
    if ((game.console_flags & OA_CONSOLE_FLAG_NO_DROP) != 0)
        return m->timeout_player;
    const auto now = now_time(m);
    if ((game.sim_run_flags & run_flag_paused) != 0) {
        m->timeout_baseline = now;
        return no_player_id;
    }
    const auto limit = game.player_timeout_seconds * 30u;
    // Silent since the player was last heard from, or since the baseline
    // when that is later, counting a turn of the clock to 0 between.
    const auto stalled = [&](const Player& p) {
        const auto silent = std::min(
            base::game_loop::scaled_clock_elapsed(now, p.last_update_time),
            base::game_loop::scaled_clock_elapsed(now, m->timeout_baseline)
        );
        return is_remote(p) && silent > limit;
    };
    int64_t group = -1;
    bool several = false;
    for (const auto& p : game.players) {
        if (!stalled(p))
            continue;
        if (group < 0)
            group = p.machine_group;
        else if (group != p.machine_group)
            several = true;
    }
    if (several)
        return no_player_id;
    for (const auto& p : game.players)
        if (stalled(p))
            return p.player_id;
    return no_player_id;
}

void net_match_designate_host(World* world) noexcept {
    uint32_t highest = 0;
    for (const auto& p : world->game.players)
        if (p.in_use != 0 &&
            (p.status == OA_PLAYER_STATUS_MIRRORED || p.status == OA_PLAYER_STATUS_LOCAL) &&
            highest < p.player_id)
            highest = p.player_id;
    auto* successor = player_of(world, highest);
    if (successor == nullptr)
        return;
    if (auto* info = world_player_info(world, successor))
        info->role = static_cast<uint8_t>(info->role | role_host);
}

bool net_match_timeout_expired(NetMatch* m, uint32_t player_id) noexcept {
    auto* p = player_of(m->world, player_id);
    if (p == nullptr || !is_remote(*p))
        return false;
    const auto seconds =
        base::game_loop::scaled_clock_elapsed(now_time(m), p->last_update_time) / 30u;
    if (seconds < m->world->game.player_timeout_seconds + timeout_drop_extra_seconds)
        return false;
    reject_player(m, player_id, reject_connection_lost);
    return true;
}

void net_match_sync_timing(const World* world, base::game_loop::Timing* timing) noexcept {
    for (std::size_t i = 0; i < OA_PLAYER_COUNT && i < timing->players.size(); ++i) {
        const auto& p = world->game.players[i];
        auto& peer = timing->players[i];
        peer.present = p.in_use != 0;
        peer.status = p.status;
        peer.eligible = p.unit_count;
        peer.tick = static_cast<uint32_t>(p.last_sim_tick);
    }
}

void net_match_set_pause(NetMatch* m, bool paused) noexcept {
    auto& game = m->world->game;
    game.sim_run_flags = static_cast<uint16_t>(
        (game.sim_run_flags & ~run_flag_paused) | (paused ? run_flag_paused : 0)
    );
    PauseSpeedRecord r{};
    r.kind = pause_speed_kind_pause;
    r.value = paused ? 1 : 0;
    send_record(m, primary_id(m->world), broadcast_destination_id, r);
}

void net_match_set_speed(NetMatch* m, int32_t speed, bool broadcast) noexcept {
    auto& game = m->world->game;
    if (speed > max_game_speed)
        speed = max_game_speed;
    if (speed < min_game_speed)
        speed = min_game_speed;
    if (static_cast<uint16_t>(speed) != game.requested_speed) {
        char line[40];
        if (speed == 10)
            std::snprintf(line, sizeof line, "Game Speed Normal");
        else
            std::snprintf(
                line,
                sizeof line,
                "%s  %c%d",
                "Game Speed",
                speed > 10 ? '+' : '-',
                speed > 10 ? speed - 10 : 10 - speed
            );
        notice(m, line);
    }
    game.requested_speed = static_cast<uint16_t>(speed);
    game.current_speed = static_cast<uint16_t>(speed);
    if (!broadcast)
        return;
    PauseSpeedRecord r{};
    r.kind = pause_speed_kind_speed;
    r.value = static_cast<uint8_t>(speed);
    send_record(m, primary_id(m->world), broadcast_destination_id, r);
}

void net_match_say(NetMatch* m, const char* text) noexcept {
    // The text's first 64 bytes, the rest zero: a line of 64 characters or
    // more fills the field with no terminator, as 3.1c's record carries it.
    ChatRecord r{};
    if (text != nullptr)
        std::memcpy(r.text, text, ::strnlen(text, sizeof r.text));
    auto& game = m->world->game;
    const auto from = first_local_player_id(game);
    const uint8_t mode = game.chat_mode;
    if (r.text[0] == '+' || mode == OA_CHAT_MODE_EVERYONE) {
        send_record(m, from, broadcast_destination_id, r);
        return;
    }
    if (mode == OA_CHAT_MODE_CHOSEN) {
        for (std::size_t slot = 0; slot < OA_PLAYER_COUNT; ++slot)
            if (game.chat_targets[slot] != 0 && game.players[slot].player_id != 0)
                send_record(m, from, game.players[slot].player_id, r);
        return;
    }
    const auto& self = game.players[game.local_player_index];
    for (std::size_t slot = 0; slot < OA_PLAYER_COUNT; ++slot) {
        const auto& p = game.players[slot];
        if (!is_remote(p))
            continue;
        const bool allied = self.alliance[slot] != 0;
        if ((mode == OA_CHAT_MODE_ALLIES && allied) || (mode == OA_CHAT_MODE_ENEMIES && !allied))
            send_record(m, from, p.player_id, r);
    }
}

void net_match_give(NetMatch* m, uint8_t from, uint8_t to, bool metal, float amount) noexcept {
    auto* world = m->world;
    if (from >= OA_PLAYER_COUNT || to >= OA_PLAYER_COUNT)
        return;
    const auto& source = world->game.players[from];
    const float stored = metal ? source.metal : source.energy;
    if (stored < amount)
        amount = stored;
    if (amount == 0.0F)
        return;
    if (m->hooks.debit != nullptr)
        (void)m->hooks.debit(m->hooks.context, world, from, metal, amount);
    if (m->hooks.credit != nullptr)
        m->hooks.credit(m->hooks.context, world, to, metal, amount);
    net_match_send_give(m, from, to, metal, amount);
}

void net_match_send_give(NetMatch* m, uint8_t from, uint8_t to, bool metal, float amount) noexcept {
    if (from >= OA_PLAYER_COUNT || to >= OA_PLAYER_COUNT)
        return;
    const auto& source = m->world->game.players[from];
    const auto& target = m->world->game.players[to];
    if (!participating(source) || !participating(target))
        return;
    ResourceGiveRecord r{};
    r.subtype = metal ? give_metal : give_energy;
    r.from_id = source.player_id;
    r.to_id = target.player_id;
    r.amount = amount;
    send_record(m, source.player_id, target.player_id, r);
}

void net_match_share_sight(NetMatch* m, uint8_t from, uint8_t to) noexcept {
    if (from >= OA_PLAYER_COUNT || to >= OA_PLAYER_COUNT)
        return;
    auto& source = m->world->game.players[from];
    auto& target = m->world->game.players[to];
    ResourceGiveRecord r{};
    r.subtype = give_sight;
    r.from_id = source.player_id;
    r.to_id = target.player_id;
    send_record(m, source.player_id, target.player_id, r);
}

void net_match_economy_period(NetMatch* m) noexcept {
    auto& game = m->world->game;
    const auto periods = ++m->connection->economy_periods;
    if ((periods & (economy_send_every - 1)) == 0 && game.viewpoint_player < OA_PLAYER_COUNT)
        send_economy(m, game.players[game.viewpoint_player], nullptr, 0);
}

bool net_match_final_economy(NetMatch* m) noexcept {
    auto& game = m->world->game;
    bool settled = true;
    for (auto& from : game.players) {
        if (!is_local(from) || from.units_created == 0 || from.reject_reason != 0)
            continue;
        for (auto& to : game.players) {
            const bool counted = is_remote(to) || to.units_created == 0 || to.reject_reason != 0;
            if (!counted || to.index > OA_PLAYER_COUNT)
                continue;
            const auto index = to.index;
            const bool unanswered = economy_requested(from)[index] == 0 ||
                                    economy_answered(from)[index] == 0 ||
                                    economy_processed(from)[index] == 0;
            if ((remote_playing(m->world, to) && unanswered) ||
                (is_remote(to) && economy_processed(from)[index] == 0)) {
                send_economy(m, from, &to, 1);
                settled = false;
            }
        }
    }
    if (m->connection != nullptr && m->connection->host != nullptr)
        sock::host_pump(m->connection->host, final_economy_wait_ms);
    return settled;
}

} // namespace oa::netgame::match
