// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// The condenser: the envelope around every game datagram on the wire.
//
//   byte 0      type: 3 stored, 4 LZ77-compressed
//   bytes 1..2  u16 LE sum of the obfuscated bytes [3, length - 3)
//   bytes 3..   payload, each byte XORed with the low byte of its index;
//               the final three bytes stay clear
//
// One condenser object sends (staging one payload per datagram) and one
// receives (keeping a decoded message the caller's buffer could not take
// until the next call). Both count bytes into TrafficStats. Storage is
// fixed-size; malformed input is rejected instead of overrunning it.

#include "oa/netgame/dplay.hpp"

namespace oa::netgame {

inline constexpr uint8_t condenser_type_stored = 3;
inline constexpr uint8_t condenser_type_compressed = 4;
inline constexpr size_t condenser_header_bytes = 3;
inline constexpr size_t condenser_clear_tail_bytes = 3;
inline constexpr size_t condenser_decoded_bytes = 28000;
inline constexpr size_t condenser_stage_bytes = 0xaf0;
inline constexpr uint32_t condenser_compress_minimum = 0xd; // shorter payloads are always stored
inline constexpr uint32_t condenser_loss_roll_range = 101;  // percent roll, 0..100
inline constexpr uint32_t rand15_range = 0x8000;            // rand() draws 0..0x7fff

// Record types the per-type counters cover (2..0x2c); the other types
// only reach the channel totals.
inline constexpr uint8_t traffic_first_counted_type = 2;
inline constexpr size_t traffic_record_types = 0x2d;

// Direction of a counted record or datagram.
enum class TrafficChannel : uint8_t { received = 0, sent = 1 };
inline constexpr size_t traffic_channels = 2;

// Throughput derived from the counters, resampled once more than 30 time
// ticks have passed since the previous sample.
struct TrafficRates {
    uint32_t sample_time{}; // 1/30 s
    uint32_t sampled_sent_bytes{};
    uint32_t sampled_received_bytes{};
    uint32_t send_rate{};    // bytes per second
    uint32_t receive_rate{}; // bytes per second
};

// The four-per-second debug line's samples and derived rates.
struct TrafficDebugRates {
    uint32_t sample_time{}; // 1/30 s
    uint32_t sampled_record_bytes[traffic_channels]{};
    uint32_t sampled_condensed_bytes{};
    uint32_t sampled_sent_bytes{};
    uint32_t sampled_received_bytes{};
    uint32_t sampled_sent_datagrams{};
    uint32_t sampled_received_datagrams{};
    uint32_t record_rate[traffic_channels]{}; // record bytes per second
    int32_t condensed_percent{};              // sent record bytes saved by condensing
    uint32_t sent_rate{};                     // bytes per second
    uint32_t received_rate{};
    uint32_t sent_datagram_rate{}; // datagrams per second
    uint32_t received_datagram_rate{};
};

// Network traffic counters. Record counters see game records as the packet layer queues and delivers
// them; datagram counters see what the condenser sends and receives.
struct TrafficStats {
    uint32_t reset_tick{}; // Game.tick at the last reset
    uint32_t record_count[traffic_record_types][traffic_channels]{};
    uint32_t record_bytes[traffic_record_types][traffic_channels]{};
    uint32_t channel_bytes[traffic_channels]{}; // record bytes
    uint32_t condensed_bytes{};                 // sent datagram bytes after condensing; never reset
    uint32_t received_datagrams{};
    uint32_t received_bytes{}; // wire length
    uint32_t sent_datagrams{};
    uint32_t sent_bytes{}; // payload plus header, before condensing
    TrafficRates rates{};
    TrafficDebugRates debug{};
};

struct Condenser {
    uint8_t decoded[condenser_decoded_bytes]{}; // storage block
    bool retry_pending{};
    uint32_t decoded_length{};
    uint8_t staged[condenser_stage_bytes]{}; // payload of the next datagram
    uint8_t frame[condenser_stage_bytes]{};  // outgoing datagram
    uint32_t staged_length{};
    uint32_t target_id{}; // destination of the staged payload
};

/// Clears the pending retry, the decoded and staged lengths and the target; the buffers are fixed members.
///
/// @param[out] condenser Condenser to reset.
void condenser_init(Condenser* condenser) noexcept;

/// Stages one payload as the body of the next datagram.
///
/// An outgoing frame is at most 0xAF0 bytes, as in 3.1c, so a larger payload is refused
/// rather than staged.
///
/// @param[in,out] condenser Condenser whose staging buffer and target are replaced.
/// @param target_id Transport id the datagram goes to.
/// @param payload Payload bytes; may be null only when size is 0.
/// @param size Payload length in bytes.
/// @return False, staging nothing, when the payload plus the 3-byte header exceeds 0xAF0 bytes.
[[nodiscard]] bool condenser_stage(
    Condenser* condenser, uint32_t target_id, const uint8_t* payload, uint32_t size
) noexcept;

/// Envelopes the staged payload and sends it as one datagram.
///
/// The payload is compressed only when compression is enabled, it is at
/// least 13 bytes and the result plus header is shorter than the payload;
/// otherwise it is stored. The staging is emptied either way, and a sent
/// datagram is counted into stats.
///
/// @param[in,out] condenser Condenser whose staged payload is sent.
/// @param transport Transport that carries the datagram.
/// @param[in,out] stats Traffic counters, or null.
/// @param from_id Transport id of the sending player.
/// @param send_flags Transport send flags.
/// @param compression_enabled Whether LZ77 compression may be used.
/// @param loss_percent Simulated loss in percent; 0 disables it.
/// @param rand15 Source of 0..0x7fff rolls for simulated loss, or null to disable it.
/// @return The transport result; ok when a simulated loss dropped the datagram.
/// @quirk A dropped datagram is still counted and reported sent.
[[nodiscard]] uint32_t condenser_send(
    Condenser* condenser,
    const NetTransport& transport,
    TrafficStats* stats,
    uint32_t from_id,
    uint32_t send_flags,
    bool compression_enabled,
    int32_t loss_percent,
    int32_t (*rand15)()
) noexcept;

/// Receives one datagram into buffer and unwraps it in place.
///
/// System messages (sender 0) and unknown envelope types pass through raw.
/// A decoded message longer than the capacity is kept and delivered by the
/// next call. A stored body too large for the condenser's own block reads as
/// no_messages.
///
/// @param[in,out] condenser Condenser holding a decoded message the last call could not deliver.
/// @param transport Transport to receive from.
/// @param[in,out] stats Traffic counters, or null.
/// @param[out] from_id Transport id of the sender; 0 for a system message.
/// @param[out] to_id Transport id of the addressee.
/// @param[out] buffer Destination of the decoded message.
/// @param[in,out] size Capacity of buffer on entry; message length, or the length needed, on return.
/// @return ok; buffer_too_small with the needed size; no_messages for an empty datagram, a bad checksum,
///         a malformed compressed body or a stored body longer than the storage block; the transport's
///         status otherwise.
[[nodiscard]] uint32_t condenser_receive(
    Condenser* condenser,
    const NetTransport& transport,
    TrafficStats* stats,
    uint32_t* from_id,
    uint32_t* to_id,
    uint8_t* buffer,
    uint32_t* size
) noexcept;

/// Zeroes the record and datagram counters the debug display reads.
///
/// @param[in,out] stats Counters to reset; condensed_bytes and the rate samples are kept.
/// @param game_tick Game.tick recorded as the reset time.
/// @quirk Because the samples are kept, the first rate after a reset wraps in unsigned arithmetic.
void traffic_stats_reset(TrafficStats* stats, uint32_t game_tick) noexcept;

/// Counts one game record into its channel total and, for types 2..0x2c, its per-type counters.
///
/// @param[in,out] stats Counters to update.
/// @param type Record type byte.
/// @param size Record length in bytes.
/// @param channel Whether the record was received or sent.
void traffic_stats_count_record(
    TrafficStats* stats, uint8_t type, uint32_t size, TrafficChannel channel
) noexcept;

/// Counts one condenser datagram.
///
/// @param[in,out] stats Counters to update.
/// @param size Datagram length: payload plus header before condensing when sent, wire length when received.
/// @param condensed Wire length of a sent datagram, added to condensed_bytes when positive; 0 for a received one.
/// @param channel Whether the datagram was received or sent.
void traffic_stats_count_datagram(
    TrafficStats* stats, uint32_t size, int32_t condensed, TrafficChannel channel
) noexcept;

/// Reports send and receive throughput, resampling when more than 30 time ticks have passed.
///
/// @param[in,out] stats Counters whose rate samples are refreshed.
/// @param now_time Current time in 1/30 s ticks.
/// @param[out] send_rate Sent bytes per second at the last sample.
/// @param[out] receive_rate Received bytes per second at the last sample.
void traffic_stats_rates(
    TrafficStats* stats, uint32_t now_time, uint32_t* send_rate, uint32_t* receive_rate
) noexcept;

inline constexpr size_t traffic_debug_line_bytes = 0x50;

/// Resamples the debug rates when more than 30 time ticks have passed and formats the debug line.
///
/// The line reads "pS=... pR=... (S=datagrams/bytes, R=datagrams/bytes) C=percent%": record bytes per
/// second sent and received, datagram and byte rates each way, and the share of sent record bytes saved
/// by condensing.
///
/// @param[in,out] stats Counters whose debug samples are refreshed.
/// @param now_time Current time in 1/30 s ticks.
/// @param[out] out Destination for the NUL-terminated line, or null to only resample.
/// @param capacity Bytes available at out; traffic_debug_line_bytes is enough.
void traffic_stats_debug_line(
    TrafficStats* stats, uint32_t now_time, char* out, size_t capacity
) noexcept;

} // namespace oa::netgame
