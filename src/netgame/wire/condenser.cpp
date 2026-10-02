// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/netgame/condenser.hpp"

#include "oa/formats/sqsh.hpp"

#include <cstdio>
#include <cstring>
#include <new>

namespace oa::netgame {
namespace {

constexpr size_t checksum_offset = 1;
constexpr uint32_t rate_sample_interval = 0x1e; // time ticks between rate samples
constexpr uint32_t time_ticks_per_second = 30;
constexpr int32_t percent = 100;
constexpr size_t received = static_cast<size_t>(TrafficChannel::received);
constexpr size_t sent = static_cast<size_t>(TrafficChannel::sent);

// Per-second rate of a counter since its sample; unsigned 32-bit
// arithmetic, so a counter reset below its sample wraps.
uint32_t rate_since(uint32_t now, uint32_t sampled, uint32_t elapsed) noexcept {
    return (now * time_ticks_per_second - sampled * time_ticks_per_second) / elapsed;
}

// Obfuscate [header, length - clear tail) and store the running sum of the
// obfuscated bytes; the sender XORs first, then sums.
void seal(uint8_t* datagram, size_t length) noexcept {
    uint16_t sum = 0;
    for (size_t index = condenser_header_bytes; index + condenser_clear_tail_bytes < length;
         ++index) {
        datagram[index] ^= static_cast<uint8_t>(index);
        sum = static_cast<uint16_t>(sum + datagram[index]);
    }
    store_u16(datagram + checksum_offset, sum);
}

// Receiver's inverse: sum the obfuscated byte, then XOR it clear.
bool unseal(uint8_t* datagram, size_t length) noexcept {
    uint16_t sum = 0;
    for (size_t index = condenser_header_bytes; index + condenser_clear_tail_bytes < length;
         ++index) {
        sum = static_cast<uint16_t>(sum + datagram[index]);
        datagram[index] ^= static_cast<uint8_t>(index);
    }
    return load_u16(datagram + checksum_offset) == sum;
}

// Compressed length of the staged payload into frame + header, or 0 when
// it would not beat the stored form.
size_t compress_staged(Condenser* c) noexcept {
    try {
        const auto encoded =
            formats::sqsh::encode_lz77({c->staged, c->staged_length}, c->staged_length);
        if (!encoded.ok())
            return 0;
        const auto& compressed = *encoded.value;
        if (compressed.size() + condenser_header_bytes >= c->staged_length)
            return 0;
        std::memcpy(c->frame + condenser_header_bytes, compressed.data(), compressed.size());
        return compressed.size();
    } catch (const std::bad_alloc&) {
        return 0;
    }
}

// Decompress body into the storage block; false on a malformed stream or
// one that would not fit it.
bool decompress_into(Condenser* c, const uint8_t* body, size_t size) noexcept {
    const auto decoded = formats::sqsh::decode_lz77_into({body, size}, {c->decoded});
    if (decoded.status != formats::sqsh::Lz77Status::ok)
        return false;
    c->decoded_length = static_cast<uint32_t>(decoded.written);
    return true;
}

} // namespace

void condenser_init(Condenser* c) noexcept {
    c->retry_pending = false;
    c->decoded_length = 0;
    c->staged_length = 0;
    c->target_id = 0;
}

bool condenser_stage(
    Condenser* c, uint32_t target_id, const uint8_t* payload, uint32_t size
) noexcept {
    if (size + condenser_header_bytes > condenser_stage_bytes || (payload == nullptr && size != 0))
        return false;
    if (size != 0)
        std::memcpy(c->staged, payload, size);
    c->staged_length = size;
    c->target_id = target_id;
    return true;
}

uint32_t condenser_send(
    Condenser* c,
    const NetTransport& transport,
    TrafficStats* stats,
    uint32_t from_id,
    uint32_t send_flags,
    bool compression_enabled,
    int32_t loss_percent,
    int32_t (*rand15)()
) noexcept {
    size_t compressed = 0;
    if (compression_enabled && c->staged_length >= condenser_compress_minimum)
        compressed = compress_staged(c);
    size_t length = 0;
    if (compressed != 0) {
        c->frame[0] = condenser_type_compressed;
        length = compressed + condenser_header_bytes;
    } else {
        c->frame[0] = condenser_type_stored;
        std::memcpy(c->frame + condenser_header_bytes, c->staged, c->staged_length);
        length = c->staged_length + condenser_header_bytes;
    }
    seal(c->frame, length);
    uint32_t result = transport_result::ok;
    bool dropped = false;
    if (loss_percent != 0 && rand15 != nullptr) {
        const auto roll = static_cast<int32_t>(
            (static_cast<int64_t>(rand15()) * condenser_loss_roll_range) / rand15_range
        );
        dropped = loss_percent >= roll;
    }
    if (!dropped)
        result = transport.send != nullptr ? transport.send(
                                                 transport.context,
                                                 from_id,
                                                 c->target_id,
                                                 send_flags,
                                                 c->frame,
                                                 static_cast<uint32_t>(length)
                                             )
                                           : transport_result::no_connection;
    if (result == transport_result::ok && stats != nullptr)
        traffic_stats_count_datagram(
            stats,
            c->staged_length + static_cast<uint32_t>(condenser_header_bytes),
            static_cast<int32_t>(length),
            TrafficChannel::sent
        );
    c->staged_length = 0;
    return result;
}

uint32_t condenser_receive(
    Condenser* c,
    const NetTransport& transport,
    TrafficStats* stats,
    uint32_t* from_id,
    uint32_t* to_id,
    uint8_t* buffer,
    uint32_t* size
) noexcept {
    const auto capacity = *size;
    if (c->retry_pending) {
        *size = c->decoded_length;
        if (capacity < c->decoded_length)
            return transport_result::buffer_too_small;
        std::memcpy(buffer, c->decoded, c->decoded_length);
        c->retry_pending = false;
        return transport_result::ok;
    }
    if (transport.receive == nullptr)
        return transport_result::no_connection;
    const auto status = transport.receive(transport.context, from_id, to_id, buffer, size);
    if (*from_id == system_message_sender_id || status != transport_result::ok)
        return status;
    if (*size > capacity)
        return transport_result::buffer_too_small;
    if (stats != nullptr)
        traffic_stats_count_datagram(stats, *size, 0, TrafficChannel::received);
    if (*size == 0 ||
        (buffer[0] != condenser_type_stored && buffer[0] != condenser_type_compressed))
        return *size == 0 ? transport_result::no_messages : transport_result::ok;
    if (*size <= condenser_header_bytes || !unseal(buffer, *size))
        return transport_result::no_messages;
    const auto body_size = *size - condenser_header_bytes;
    if (buffer[0] == condenser_type_compressed) {
        if (!decompress_into(c, buffer + condenser_header_bytes, body_size))
            return transport_result::no_messages;
    } else {
        if (body_size > condenser_decoded_bytes)
            return transport_result::no_messages;
        std::memcpy(c->decoded, buffer + condenser_header_bytes, body_size);
        c->decoded_length = static_cast<uint32_t>(body_size);
    }
    *size = c->decoded_length;
    if (capacity < c->decoded_length) {
        c->retry_pending = true;
        return transport_result::buffer_too_small;
    }
    std::memcpy(buffer, c->decoded, c->decoded_length);
    return transport_result::ok;
}

void traffic_stats_reset(TrafficStats* stats, uint32_t game_tick) noexcept {
    stats->reset_tick = game_tick;
    stats->channel_bytes[received] = 0;
    stats->channel_bytes[sent] = 0;
    for (size_t type = 1; type < traffic_record_types; ++type)
        for (size_t channel = 0; channel < traffic_channels; ++channel) {
            stats->record_count[type][channel] = 0;
            stats->record_bytes[type][channel] = 0;
        }
    stats->sent_datagrams = 0;
    stats->sent_bytes = 0;
    stats->received_datagrams = 0;
    stats->received_bytes = 0;
}

void traffic_stats_count_record(
    TrafficStats* stats, uint8_t type, uint32_t size, TrafficChannel channel
) noexcept {
    const auto index = static_cast<size_t>(channel);
    stats->channel_bytes[index] += size;
    if (type >= traffic_first_counted_type && type < traffic_record_types) {
        ++stats->record_count[type][index];
        stats->record_bytes[type][index] += size;
    }
}

void traffic_stats_count_datagram(
    TrafficStats* stats, uint32_t size, int32_t condensed, TrafficChannel channel
) noexcept {
    if (condensed > 0)
        stats->condensed_bytes += static_cast<uint32_t>(condensed);
    if (channel == TrafficChannel::received) {
        ++stats->received_datagrams;
        stats->received_bytes += size;
    } else {
        ++stats->sent_datagrams;
        stats->sent_bytes += size;
    }
}

void traffic_stats_rates(
    TrafficStats* stats, uint32_t now_time, uint32_t* send_rate, uint32_t* receive_rate
) noexcept {
    auto& r = stats->rates;
    const auto elapsed = now_time - r.sample_time;
    if (elapsed > rate_sample_interval) {
        r.send_rate = rate_since(stats->sent_bytes, r.sampled_sent_bytes, elapsed);
        r.receive_rate = rate_since(stats->received_bytes, r.sampled_received_bytes, elapsed);
        r.sampled_sent_bytes = stats->sent_bytes;
        r.sampled_received_bytes = stats->received_bytes;
        r.sample_time = now_time;
    }
    *send_rate = r.send_rate;
    *receive_rate = r.receive_rate;
}

void traffic_stats_debug_line(
    TrafficStats* stats, uint32_t now_time, char* out, size_t capacity
) noexcept {
    auto& d = stats->debug;
    const auto elapsed = now_time - d.sample_time;
    if (elapsed > rate_sample_interval) {
        d.sample_time = now_time;
        d.record_rate[received] =
            rate_since(stats->channel_bytes[received], d.sampled_record_bytes[received], elapsed);
        d.sampled_record_bytes[received] = stats->channel_bytes[received];
        const auto sent_records =
            static_cast<int32_t>(stats->channel_bytes[sent] - d.sampled_record_bytes[sent]);
        d.sampled_record_bytes[sent] = stats->channel_bytes[sent];
        d.record_rate[sent] = static_cast<uint32_t>(sent_records) * time_ticks_per_second / elapsed;
        const auto condensed =
            static_cast<int32_t>(stats->condensed_bytes - d.sampled_condensed_bytes);
        d.condensed_percent =
            sent_records < 1 ? 0 : (sent_records - condensed) * percent / sent_records;
        d.sent_rate = rate_since(stats->sent_bytes, d.sampled_sent_bytes, elapsed);
        d.sampled_condensed_bytes = stats->condensed_bytes;
        d.sampled_sent_bytes = stats->sent_bytes;
        d.received_rate = rate_since(stats->received_bytes, d.sampled_received_bytes, elapsed);
        d.sampled_received_bytes = stats->received_bytes;
        d.sent_datagram_rate = rate_since(stats->sent_datagrams, d.sampled_sent_datagrams, elapsed);
        d.sampled_sent_datagrams = stats->sent_datagrams;
        d.received_datagram_rate =
            rate_since(stats->received_datagrams, d.sampled_received_datagrams, elapsed);
        d.sampled_received_datagrams = stats->received_datagrams;
    }
    if (out == nullptr || capacity == 0)
        return;
    std::snprintf(
        out,
        capacity,
        "pS=%4d pR=%4d (S=%d/%4d, R=%d/%4d) C=%3d%%\n",
        static_cast<int32_t>(d.record_rate[sent]),
        static_cast<int32_t>(d.record_rate[received]),
        static_cast<int32_t>(d.sent_datagram_rate),
        static_cast<int32_t>(d.sent_rate),
        static_cast<int32_t>(d.received_datagram_rate),
        static_cast<int32_t>(d.received_rate),
        d.condensed_percent
    );
}

} // namespace oa::netgame
