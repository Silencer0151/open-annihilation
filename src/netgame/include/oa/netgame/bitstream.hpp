// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// LSB-first bit stream packed into little-endian 32-bit words, as used by
// the 0x2c unit-state record. A field that starts on a byte boundary is an
// ordinary little-endian value. Storage is caller-owned and bounded.

#include "oa/netgame/wire.hpp"

namespace oa::netgame {

inline constexpr unsigned bit_stream_word_bits = 32;
inline constexpr std::size_t bit_stream_word_bytes = 4;
// Word capacity of the unit-state writer's buffer.
inline constexpr std::size_t unit_state_writer_words = 0x100;

struct BitWriter {
    uint8_t* storage{}; // capacity_words * 4 bytes
    std::size_t capacity_words{};
    uint32_t word_index{};
    uint32_t bit_position{};        // 0..31
    WireError error{WireError::ok}; // sticky; set when a word past capacity is needed
};

/// Starts a writer at bit zero of empty storage and clears the first word.
///
/// @param[out] writer Writer to initialise.
/// @param storage Word storage of capacity_words * 4 bytes; null gives a writer with no capacity.
/// @param capacity_words Number of 32-bit words storage holds.
void bit_writer_init(BitWriter* writer, uint8_t* storage, std::size_t capacity_words) noexcept;

/// Resumes a writer over existing words without clearing anything.
///
/// @param[out] writer Writer to initialise; its error is bad_argument when bit_position is 32 or more.
/// @param storage Word storage of capacity_words * 4 bytes; null gives a writer with no capacity.
/// @param capacity_words Number of 32-bit words storage holds.
/// @param word_index Word the next bits go into.
/// @param bit_position Bit within that word, 0..31.
void bit_writer_attach(
    BitWriter* writer,
    uint8_t* storage,
    std::size_t capacity_words,
    uint32_t word_index,
    uint32_t bit_position
) noexcept;

/// Appends the low bits of a value at the current bit position.
///
/// Bits are ORed into the current word; the word after a completed one is
/// cleared. Does nothing once the writer's error is set.
///
/// @param[in,out] writer Writer to append to; its error becomes overflow when a word past capacity is
///        needed and bad_argument when count exceeds 32.
/// @param value Bits to write; only the low count bits are used.
/// @param count Number of bits, 0..32.
void bit_writer_write(BitWriter* writer, uint32_t value, unsigned count) noexcept;

/// Appends one bit at the current bit position.
///
/// Does nothing once the writer's error is set.
///
/// @param[in,out] writer Writer to append to; its error becomes overflow when a word past capacity is needed.
/// @param bit Bit value to write.
void bit_writer_write_bit(BitWriter* writer, bool bit) noexcept;

/// Overwrites one byte of the packed buffer, such as the u16 length of a 0x2c record.
///
/// @param[in,out] writer Writer whose storage is patched; its error becomes overflow when index is past capacity.
/// @param index Byte offset into storage.
/// @param value Byte to store.
void bit_writer_patch_byte(BitWriter* writer, std::size_t index, uint8_t value) noexcept;

/// Returns the number of bytes written so far, counting a partial byte as whole.
///
/// @param writer Writer to measure.
/// @return word_index * 4 + (bit_position + 7) / 8.
[[nodiscard]] std::size_t bit_writer_byte_length(const BitWriter* writer) noexcept;

struct BitReader {
    const uint8_t* base{}; // record start; need not be word aligned
    std::size_t size{};    // readable bytes from base; bytes past it read as zero
    uint32_t word_index{};
    uint32_t bit_position{};
};

/// Starts a reader at bit zero of a byte range.
///
/// @param[out] reader Reader to initialise.
/// @param base First byte of the stream; null gives an empty stream.
/// @param size Readable bytes from base.
void bit_reader_init(BitReader* reader, const uint8_t* base, std::size_t size) noexcept;

/// Reads the next bits as an unsigned value.
///
/// Bytes beyond the reader's size read as zero.
///
/// @param[in,out] reader Reader to advance.
/// @param count Number of bits, 0..32.
/// @return The bits, least significant first, zero-extended.
/// @quirk A field that ends exactly on a word boundary still reads the next whole word.
[[nodiscard]] uint32_t bit_reader_read(BitReader* reader, unsigned count) noexcept;

/// Reads the next bits as a two's-complement value and sign-extends it to 32 bits.
///
/// @param[in,out] reader Reader to advance.
/// @param count Number of bits, 1..32.
/// @return The sign-extended value.
/// @quirk With count 32 and the top bit set the result is 0xffffffff whatever the other bits are,
///        because the shift count is taken modulo 32.
[[nodiscard]] uint32_t bit_reader_read_signed(BitReader* reader, unsigned count) noexcept;

/// Reads the next bit.
///
/// @param[in,out] reader Reader to advance.
/// @return True when the bit is set.
[[nodiscard]] bool bit_reader_read_bit(BitReader* reader) noexcept;

/// Returns the number of bits read so far.
///
/// @param reader Reader to measure.
/// @return word_index * 32 + bit_position.
[[nodiscard]] std::size_t bit_reader_bits_consumed(const BitReader* reader) noexcept;

/// Tells whether a read has consumed bits beyond the reader's size.
///
/// @param reader Reader to test.
/// @return True when more bits were consumed than size * 8.
[[nodiscard]] bool bit_reader_overrun(const BitReader* reader) noexcept;

} // namespace oa::netgame
