// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The condenser's datagram envelope, pinned byte for byte: for a short
// payload always stored, two compressible payloads, a pseudo-random payload
// that compression cannot shrink and a compressible payload sent with
// compression off, the datagram put on the wire must match the size and
// 64-bit FNV-1a below on every platform (the short one byte for byte), and
// a receiving condenser must unwrap each back to its payload. Datagrams a
// 3.1c machine sent in a network game unwrap, and the condenser compresses
// their payloads back into the same bytes.
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

// Compressed datagrams a 3.1c machine sent to this engine, as captured on the
// network (the payloads of its DirectPlay messages after the two player ids).
struct CapturedDatagram {
    const char* name{};
    const char* hex{};
};

constexpr std::array<CapturedDatagram, 4> captured{{
    {"a player setup block and its team, from a battle room",
     "045a1b07d6fa2607284a6b656d0f625f10637d60677c78eb7f19551a431f711c5f25ad26bb2325882f9a22ab"
     "2ecd2f2f1719dec72f753f379930323a36723c374a2048b944464717614cf42546456a2d595453000005"},
    {"a probe and the slot table",
     "04c40701fb1406012e2ee6ff17a72b8e1032d313361417176718199f1e000023"},
    {"six unit-state records of one tick each",
     "041f3b01fb14062b0309cd030c5f0e3f1010124214dd8e17d1085c1a391dd4ec2164c20d242522637a126b28"
     "2c2dee25102e0726d4d9b024883b7e3f10103ef4e14343d34067dee7d9434ad24c4c6cbfaf8edb7954f11617"
     "f8598a6f0c5bf2de64513262c8788667ea6d5b6b4ea19a6b7cd0731374ea77fd75187262ac7861f1d1a28407"
     "d4b6128158880671bf898f000033"},
    {"unit-state records that compression shortens by one byte",
     "047f8e01fb14062b2909b96c0c0f0e1f10f00a1614111256b81983191cdd14bf2021320684cf29962929061a"
     "282d9abe301d32b53a4d763623394a3a1f7c3d1540b1bd5cca14460708607a495c770e4efc705043580dc450"
     "597a583b5ce8cd5f6ff18b6258645766e06aee6a7ccfee586052700d74c377c67d717a667ebebb7aa8e58733"
     "048166b789fb4f8b8b5d8f2fd3f1fc0bc415c9768209922d8c999a9f57a5f0a3d0ad6acab8b00a99e83da80f"
     "a6b1b223d03e76ba38b73bfb050abe1e50b0a2c4b4c4b6c6a34dde03cdfdee4fe0e6d9a36806dfc7d49989db"
     "fc57e3dc20e121736dedbee618eeea45bceb2a6fe540f6e5b5a476f2aafeefcbf84a3ff828e1000f6e0447b7"
     "017e0b2c0c716e1fd0511073fc9501a7381c0d792c5d12088023e323a42d52ad2b182b74b92b8d6f0805353b"
     "26358e843d06d63a70253d0b6251510000ec"},
}};

/// Returns the bytes a string of hexadecimal digit pairs spells.
///
/// @param hex digits, two to a byte
/// @return the bytes
Bytes from_hex(std::string_view hex) {
    Bytes bytes;
    const auto digit = [](char c) {
        return static_cast<uint8_t>(c <= '9' ? c - '0' : c - 'a' + 10);
    };
    for (size_t i = 0; i + 1 < hex.size(); i += 2)
        bytes.push_back(static_cast<uint8_t>(digit(hex[i]) << 4 | digit(hex[i + 1])));
    return bytes;
}

/// Unwraps each captured datagram and condenses its payload again: the
/// engine's compression is 3.1c's, byte for byte.
void captured_datagrams_compress_alike() {
    for (const auto& sample : captured) {
        const Bytes datagram = from_hex(sample.hex);
        check(
            !datagram.empty() && datagram[0] == condenser_type_compressed,
            std::string(sample.name) + " was sent compressed"
        );
        const Bytes payload = unwrap(datagram);
        check(!payload.empty(), std::string(sample.name) + " unwraps");
        check(
            condense(payload, true) == datagram,
            std::string(sample.name) + " compresses back into the bytes 3.1c sent"
        );
    }
}

} // namespace

int main() {
    datagrams_match_pins();
    captured_datagrams_compress_alike();
    if (failures != 0)
        return 1;
    std::cout << "condenser golden datagrams passed\n";
    return 0;
}
