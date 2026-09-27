// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// SHA-256 as FIPS 180-4 defines it, for checking that a file is the one the
// engine expects. The message is fed in pieces of any size; nothing is
// allocated and nothing throws.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace oa::base::sha256 {

/// Size of a digest, in bytes.
inline constexpr size_t digest_size = 32;
/// Size of the blocks the message is processed in, in bytes.
inline constexpr size_t block_size = 64;
/// Length of a digest written in hexadecimal, in characters.
inline constexpr size_t hex_size = digest_size * 2;
/// Number of 32-bit words in the hash value.
inline constexpr size_t state_words = 8;

/// A digest, its bytes in the order FIPS 180-4 writes them.
using Digest = std::array<uint8_t, digest_size>;

/// The running state of one digest computation.
///
/// A default-initialised Hasher starts an empty message.
struct Hasher {
    /// The hash value, starting from the standard's initial value.
    std::array<uint32_t, state_words> state{
        0x6a09e667,
        0xbb67ae85,
        0x3c6ef372,
        0xa54ff53a,
        0x510e527f,
        0x9b05688c,
        0x1f83d9ab,
        0x5be0cd19,
    };
    /// Message bytes waiting for a whole block.
    std::array<uint8_t, block_size> pending{};
    /// How many bytes of `pending` hold message bytes.
    size_t pending_size{};
    /// Message bytes fed so far; a message is shorter than 2^61 bytes.
    uint64_t message_size{};
};

/// Appends bytes to the message.
///
/// @param[in,out] hasher the computation to extend
/// @param bytes the next part of the message, of any size
void update(Hasher& hasher, std::span<const uint8_t> bytes) noexcept;

/// Pads the message fed so far and returns its digest.
///
/// Works on a copy, so `hasher` may be fed further afterwards and finished
/// again for the longer message.
///
/// @param hasher the computation to finish
/// @return the digest of the message
[[nodiscard]] Digest finish(Hasher hasher) noexcept;

/// Computes the digest of a whole message.
///
/// @param bytes the message
/// @return its digest
[[nodiscard]] Digest digest_of(std::span<const uint8_t> bytes) noexcept;

/// Returns the value of one hexadecimal digit.
///
/// @param digit a character
/// @return 0 to 15 for 0-9, a-f and A-F; nullopt for any other character
[[nodiscard]] constexpr std::optional<uint8_t> hex_digit_value(char digit) noexcept {
    if (digit >= '0' && digit <= '9')
        return static_cast<uint8_t>(digit - '0');
    if (digit >= 'a' && digit <= 'f')
        return static_cast<uint8_t>(digit - 'a' + 10);
    if (digit >= 'A' && digit <= 'F')
        return static_cast<uint8_t>(digit - 'A' + 10);
    return std::nullopt;
}

/// Reads a digest written as hexadecimal digits, as checksum lists write it.
///
/// Usable in constant expressions, so a digest the engine expects can be
/// written as text and checked when it compiles.
///
/// @param text exactly hex_size hexadecimal digits, in either case
/// @return the digest; nullopt for any other length or character
[[nodiscard]] constexpr std::optional<Digest> parse_hex(std::string_view text) noexcept {
    if (text.size() != hex_size)
        return std::nullopt;
    Digest digest{};
    for (size_t index = 0; index < digest_size; ++index) {
        const auto high = hex_digit_value(text[index * 2]);
        const auto low = hex_digit_value(text[index * 2 + 1]);
        if (!high || !low)
            return std::nullopt;
        digest[index] = static_cast<uint8_t>(*high << 4 | *low);
    }
    return digest;
}

/// Writes a digest as lower-case hexadecimal digits.
///
/// @param digest the digest
/// @return hex_size characters, without a terminating null
[[nodiscard]] constexpr std::array<char, hex_size> to_hex(const Digest& digest) noexcept {
    constexpr std::string_view digits = "0123456789abcdef";
    std::array<char, hex_size> text{};
    for (size_t index = 0; index < digest_size; ++index) {
        text[index * 2] = digits[digest[index] >> 4];
        text[index * 2 + 1] = digits[digest[index] & 0xf];
    }
    return text;
}

} // namespace oa::base::sha256
