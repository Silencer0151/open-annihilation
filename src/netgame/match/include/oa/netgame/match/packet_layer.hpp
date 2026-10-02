// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// The game-packet layer between game code and the transport: per-destination
// send channels that batch records into sequenced frames, the condenser
// envelope around every frame, and the receive side that accepts frames per
// peer, splits them into tick-stamped records and hands them out one at a
// time, with transport system messages passed through raw.

#include "oa/core/game_state.h"
#include "oa/netgame/condenser.hpp"
#include "oa/netgame/dplay.hpp"
#include "oa/netgame/frame.hpp"

namespace oa::netgame::match {

inline constexpr std::size_t channel_count = 11; // slot 0 broadcasts
inline constexpr uint32_t free_channel_id =
    0xffffffffu; // SendChannel.destination_id of an unused slot

// The packet queue object.
struct PacketLayer {
    NetTransport transport{};
    bool guaranteed{}; // send dwFlags 1 while set (lobby and loading)
    // The Game whose send options every frame reads: Game.compression_off and
    // Game.send_error_percent. Null: compressed, no loss.
    const Game* send_options{};
    uint32_t ticks_between_sends{6}; // from the queue's pacing, 200 ms until a rate is set
    uint32_t sent_frames{};
    uint32_t send_failures{};
    uint32_t dropped_frames{}; // undecodable or refused by the frame receiver
    SendChannel channels[channel_count]{};
    Condenser send_condenser{}; // the staging object
    Condenser receive_condenser{};
    TrafficStats traffic{};
    // Bind receiver.players to the game's player table for the session:
    // the battle room's, then the match's.
    FrameReceiver receiver{};
    // A frame whose peer ring was still busy waits here, unparsed.
    bool pending{};
    bool pending_fresh{};
    uint32_t pending_from{};
    uint32_t pending_to{};
    uint32_t pending_size{};
    uint8_t pending_frame[max_receive_frame_bytes]{};
    // Second frame of a two-frame delivery, parsed once the first drains.
    bool queued{};
    bool queued_fresh{};
    uint32_t queued_from{};
    uint32_t queued_to{};
    uint32_t queued_size{};
    uint8_t queued_frame[max_receive_frame_bytes]{};
    uint8_t
        rx[condenser_header_bytes +
           condenser_decoded_bytes]{}; // a stored datagram of the largest body
};

static_assert(
    sizeof(PacketLayer::rx) >= condenser_decoded_bytes, "the condenser never holds a message back"
);

enum class PacketKind : uint8_t { none, system, record };

struct Packet {
    PacketKind kind{};
    uint32_t from_id{};
    uint32_t to_id{};
    uint32_t size{};
    const uint8_t* data{}; // valid until the next packet_layer_receive
};

/// Constructs the queue: 200 ms pacing, every channel free, no peers, both condensers empty and no transport.
///
/// PacketLayer is large: allocate it on the heap.
///
/// @param[out] layer Queue to construct.
void packet_layer_create(PacketLayer* layer) noexcept;

/// Applies a packets-per-second rate to the queue and every channel.
///
/// @param[in,out] layer Queue to pace.
/// @param sends_per_second 0 means one send per 200 ms; otherwise clamped to 2..30.
/// @return False, changing nothing, for a negative rate; the game then drops to unframed sends, which are
///         not implemented.
bool packet_layer_set_rate(PacketLayer* layer, int32_t sends_per_second) noexcept;

/// Starts a session: empties the receive state and sets the channels up over a transport at the queue's pacing.
///
/// The game first applies a lobby-launch rate that only a launch's join
/// writes, and only as 0, so the configured rate always stands.
///
/// @param[in,out] layer Queue to start.
/// @param transport Transport the channels send through and the receiver reads.
void packet_layer_start(PacketLayer* layer, const NetTransport& transport) noexcept;

/// Queues one record on its destination's channel and counts it as sent.
///
/// net_match_send checks the players first.
///
/// @param[in,out] layer Queue to send on.
/// @param from_id Transport id of the local player the record is from.
/// @param to_id Destination transport id; 0 broadcasts. A new unicast id claims a free channel, or, with
///        all ten taken, the first whose destination no player of receiver.players names any more,
///        dropping what that channel still held.
/// @param record Record bytes, type byte first.
/// @param size Record length in bytes.
/// @param now_time Current time in game ticks, used by a forced flush.
/// @return ok; bad_argument for a null layer or record; overflow when no channel can be had or the channel is
///         full; the channel's other errors.
[[nodiscard]] WireError packet_layer_send(
    PacketLayer* layer,
    uint32_t from_id,
    uint32_t to_id,
    const uint8_t* record,
    std::size_t size,
    uint32_t now_time
) noexcept;

/// Emits the due frames of every channel in use.
///
/// @param[in,out] layer Queue to flush.
/// @param now_time Current time in game ticks.
/// @param force Ignore the pacing.
void packet_layer_flush(PacketLayer* layer, uint32_t now_time, bool force) noexcept;

/// Hands out the next record or system message.
///
/// Records stamped 1..30 ticks ahead of tick stay queued. A frame that
/// waited behind its peer's busy ring is parsed before the transport is read
/// again, and the call then gives that ring's due record or false. The game
/// grows its buffer for a longer datagram; none decodes into the condenser
/// storage, so one is taken off the transport and dropped. Every packet
/// handed out is counted as received.
///
/// @param[in,out] layer Queue to read.
/// @param tick Current game tick.
/// @param[out] out The packet; its data stays valid until the next call.
/// @return False when nothing is deliverable at this tick.
bool packet_layer_receive(PacketLayer* layer, int32_t tick, Packet* out) noexcept;

/// Forgets a departed peer's queued records and saved frame and releases its unicast channel.
///
/// Its frame slot is taken over once no player of the bound table names it.
///
/// @param[in,out] layer Queue holding the peer.
/// @param peer_id Transport id of the departed player.
void packet_layer_release_peer(PacketLayer* layer, uint32_t peer_id) noexcept;

/// Tells whether no queued record or waiting frame is left to deliver.
///
/// @param layer Queue to test.
/// @return True when every peer ring is empty and no frame waits.
[[nodiscard]] bool packet_layer_idle(const PacketLayer* layer) noexcept;

} // namespace oa::netgame::match
