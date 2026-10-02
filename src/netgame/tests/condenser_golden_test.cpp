// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The condenser's datagram envelope, pinned byte for byte: for a short
// payload always stored, two compressible payloads, a pseudo-random payload
// that compression cannot shrink and a compressible payload sent with
// compression off, the datagram put on the wire must match the size and
// 64-bit FNV-1a below on every platform (the short one byte for byte), and
// a receiving condenser must unwrap each back to its payload.
#include "oa/netgame/condenser.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using namespace oa::netgame;

namespace {

using Bytes = std::vector<uint8_t>;

constexpr uint32_t sender_id = 4;
constexpr uint32_t receiver_id = 9;
constexpr uint32_t no_send_flags = 0;
// Loss percentage that turns simulated loss off; the null roll source does too.
constexpr uint32_t no_simulated_loss = 0;
constexpr size_t repetitive_bytes = 200;
constexpr uint32_t repetitive_period = 5;
constexpr size_t mixed_bytes = 300;
constexpr uint32_t mixed_step = 7;
constexpr uint32_t mixed_period = 11;
constexpr size_t random_bytes = 1000;
constexpr uint32_t random_seed = 0x5eed0001u;
constexpr uint32_t random_multiplier = 1664525u;
constexpr uint32_t random_increment = 1013904223u;
constexpr unsigned random_byte_shift = 24;

constexpr uint64_t fnv_basis = 0xcbf29ce484222325ull;
constexpr uint64_t fnv_prime = 0x100000001b3ull;

enum class Sample : uint8_t {
    short_text,
    repetitive,
    mixed,
    random,
    repetitive_uncompressed,
    count
};
constexpr size_t sample_count = static_cast<size_t>(Sample::count);

struct PinnedDatagram {
    const char* name{};
    uint8_t type{}; // condenser_type_stored or condenser_type_compressed
    uint64_t size{};
    uint64_t hash{}; // 64-bit FNV-1a
};

constexpr std::array<PinnedDatagram, sample_count> pinned{{
    {"short text", condenser_type_stored, 15, 0x3dfb9f1e2d8e862bull},
    {"repetitive", condenser_type_compressed, 38, 0x98a8b514fb00ad6dull},
    {"mixed", condenser_type_compressed, 55, 0x77b7a95acffb58bdull},
    {"random", condenser_type_stored, 1003, 0x31f388e60c3577dbull},
    {"repetitive, compression off", condenser_type_stored, 203, 0x72ae015f4d1e6324ull},
}};

// The short text's whole datagram: type, checksum, then the payload XORed
// with each byte's index except the clear three-byte tail.
constexpr std::array<uint8_t, 15> pinned_short_datagram{
    condenser_type_stored,
    51,
    3,
    107,
    97,
    105,
    106,
    104,
    36,
    41,
    125,
    98,
    114,
    101,
    33,
};

int failures = 0;

/// Records a failed check.
///
/// @param held whether the check held
/// @param what description printed when it did not
void check(bool held, const std::string& what) {
    if (!held) {
        std::cerr << "FAIL: " << what << '\n';
        ++failures;
    }
}

/// Returns the payload of a sample.
///
/// @param sample sample
/// @return its bytes; the random one comes from a 32-bit LCG's top byte
Bytes payload(Sample sample) {
    Bytes bytes;
    switch (sample) {
    case Sample::short_text: {
        constexpr std::string_view text = "hello, wire!";
        bytes.assign(text.begin(), text.end());
        break;
    }
    case Sample::repetitive:
    case Sample::repetitive_uncompressed:
        for (size_t i = 0; i < repetitive_bytes; ++i)
            bytes.push_back(static_cast<uint8_t>(i % repetitive_period));
        break;
    case Sample::mixed:
        for (size_t i = 0; i < mixed_bytes; ++i)
            bytes.push_back(static_cast<uint8_t>(i * mixed_step % mixed_period));
        break;
    case Sample::random: {
        uint32_t state = random_seed;
        for (size_t i = 0; i < random_bytes; ++i) {
            state = state * random_multiplier + random_increment;
            bytes.push_back(static_cast<uint8_t>(state >> random_byte_shift));
        }
        break;
    }
    case Sample::count:
        break;
    }
    return bytes;
}

/// Returns the 64-bit FNV-1a of bytes.
///
/// @param bytes bytes to hash
/// @return the hash
uint64_t fnv1a(std::span<const uint8_t> bytes) {
    uint64_t hash = fnv_basis;
    for (const uint8_t byte : bytes) {
        hash ^= byte;
        hash *= fnv_prime;
    }
    return hash;
}

// A transport that keeps what is sent and hands back one datagram to receive.
struct Wire {
    Bytes sent{};
    Bytes inbox{};
    bool delivered{};

    /// Keeps a sent datagram in place of the one sent before it.
    ///
    /// @param[in,out] context the Wire, whose sent bytes are replaced
    /// @param data datagram bytes
    /// @param size datagram length in bytes
    /// @return transport_result::ok
    static uint32_t
    send(void* context, uint32_t, uint32_t, uint32_t, const uint8_t* data, uint32_t size) {
        static_cast<Wire*>(context)->sent.assign(data, data + size);
        return transport_result::ok;
    }

    /// Hands back the inbox datagram once, from sender_id to receiver_id.
    ///
    /// @param[in,out] context the Wire, marked delivered once the datagram is handed back
    /// @param[out] from receives sender_id
    /// @param[out] to receives receiver_id
    /// @param[out] buffer receives the datagram
    /// @param[in,out] size capacity of buffer on entry; the datagram's length on return, also
    ///     when the buffer is too small
    /// @return transport_result::ok, no_messages once delivered, or buffer_too_small
    static uint32_t
    receive(void* context, uint32_t* from, uint32_t* to, uint8_t* buffer, uint32_t* size) {
        auto* wire = static_cast<Wire*>(context);
        if (wire->delivered)
            return transport_result::no_messages;
        if (wire->inbox.size() > *size) {
            *size = static_cast<uint32_t>(wire->inbox.size());
            return transport_result::buffer_too_small;
        }
        *from = sender_id;
        *to = receiver_id;
        std::memcpy(buffer, wire->inbox.data(), wire->inbox.size());
        *size = static_cast<uint32_t>(wire->inbox.size());
        wire->delivered = true;
        return transport_result::ok;
    }

    /// Returns a transport that sends into and receives from this Wire.
    ///
    /// @return the transport, whose context is this Wire
    NetTransport transport() { return {this, send, receive}; }
};

/// Sends one payload through a fresh condenser.
///
/// @param bytes payload
/// @param compress whether compression is enabled
/// @return the datagram put on the wire
Bytes condense(const Bytes& bytes, bool compress) {
    auto condenser = std::make_unique<Condenser>();
    condenser_init(condenser.get());
    Wire wire;
    const auto size = static_cast<uint32_t>(bytes.size());
    check(condenser_stage(condenser.get(), receiver_id, bytes.data(), size), "payload stages");
    const uint32_t sent = condenser_send(
        condenser.get(),
        wire.transport(),
        nullptr,
        sender_id,
        no_send_flags,
        compress,
        no_simulated_loss,
        nullptr
    );
    check(sent == transport_result::ok, "datagram sends");
    return wire.sent;
}

/// Unwraps one datagram through a fresh condenser.
///
/// @param datagram bytes as received from the wire
/// @return the payload, or nothing when the condenser refused the datagram
Bytes unwrap(const Bytes& datagram) {
    auto condenser = std::make_unique<Condenser>();
    condenser_init(condenser.get());
    Wire wire;
    wire.inbox = datagram;
    Bytes buffer(condenser_decoded_bytes);
    uint32_t from = 0;
    uint32_t to = 0;
    auto size = static_cast<uint32_t>(buffer.size());
    const uint32_t status = condenser_receive(
        condenser.get(), wire.transport(), nullptr, &from, &to, buffer.data(), &size
    );
    if (status != transport_result::ok || from != sender_id || to != receiver_id)
        return {};
    buffer.resize(size);
    return buffer;
}

/// Sends every sample, checks each datagram against its pin and unwraps it back.
void datagrams_match_pins() {
    for (size_t index = 0; index < sample_count; ++index) {
        const auto sample = static_cast<Sample>(index);
        const PinnedDatagram& expected = pinned[index];
        const Bytes bytes = payload(sample);
        const Bytes datagram = condense(bytes, sample != Sample::repetitive_uncompressed);
        const uint64_t hash = fnv1a(datagram);
        const uint8_t type = datagram.empty() ? 0 : datagram[0];
        char found[96];
        std::snprintf(
            found,
            sizeof found,
            "type %u, {%zu, 0x%016llxull}",
            type,
            datagram.size(),
            static_cast<unsigned long long>(hash)
        );
        const bool held =
            type == expected.type && datagram.size() == expected.size && hash == expected.hash;
        check(held, std::string(expected.name) + " datagram is " + found);
        check(unwrap(datagram) == bytes, std::string(expected.name) + " unwraps back");
    }
    const Bytes short_datagram = condense(payload(Sample::short_text), true);
    std::string listing;
    for (const uint8_t byte : short_datagram)
        listing += std::to_string(byte) + ", ";
    const bool same =
        short_datagram.size() == pinned_short_datagram.size() &&
        std::equal(short_datagram.begin(), short_datagram.end(), pinned_short_datagram.begin());
    check(same, "short text datagram is {" + listing + "}");
}

} // namespace

int main() {
    datagrams_match_pins();
    if (failures != 0)
        return 1;
    std::cout << "condenser golden datagrams passed\n";
    return 0;
}
