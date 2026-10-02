// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/netgame/bitstream.hpp"

#include <cstdint>

namespace oa::netgame {
namespace {

/// Returns a mask of the low `count` bits.
///
/// The count is taken modulo 32, so a count of 32 gives an empty mask.
///
/// @param count Number of bits.
/// @return The mask.
constexpr uint32_t low_mask(uint32_t count) noexcept {
    return (uint32_t{1} << (count & 31u)) - 1u;
}

uint32_t load_word(const BitWriter* writer, uint32_t index) noexcept {
    return load_u32(writer->storage + std::size_t{index} * bit_stream_word_bytes);
}

void store_word(BitWriter* writer, uint32_t index, uint32_t value) noexcept {
    if (index >= writer->capacity_words) {
        writer->error = WireError::overflow;
        return;
    }
    store_u32(writer->storage + std::size_t{index} * bit_stream_word_bytes, value);
}

void or_word(BitWriter* writer, uint32_t index, uint32_t bits) noexcept {
    if (index >= writer->capacity_words) {
        writer->error = WireError::overflow;
        return;
    }
    store_word(writer, index, load_word(writer, index) | bits);
}

/// Moves the writer to its next word and clears it.
///
/// Past the end of the buffer the writer records overflow and writes nothing.
///
/// @param[in,out] writer Writer to advance.
void advance_word(BitWriter* writer) noexcept {
    ++writer->word_index;
    store_word(writer, writer->word_index, 0);
}

uint32_t reader_word(const BitReader* reader, uint32_t index) noexcept {
    uint32_t value = 0;
    const std::size_t at = std::size_t{index} * bit_stream_word_bytes;
    for (std::size_t i = 0; i < bit_stream_word_bytes; ++i)
        if (at + i < reader->size)
            value |= static_cast<uint32_t>(reader->base[at + i]) << (8 * i);
    return value;
}

} // namespace

void bit_writer_init(BitWriter* writer, uint8_t* storage, std::size_t capacity_words) noexcept {
    bit_writer_attach(writer, storage, capacity_words, 0, 0);
    store_word(writer, 0, 0);
}

void bit_writer_attach(
    BitWriter* writer,
    uint8_t* storage,
    std::size_t capacity_words,
    uint32_t word_index,
    uint32_t bit_position
) noexcept {
    writer->storage = storage;
    writer->capacity_words = storage == nullptr ? 0 : capacity_words;
    writer->word_index = word_index;
    writer->bit_position = bit_position;
    writer->error = bit_position < bit_stream_word_bits ? WireError::ok : WireError::bad_argument;
}

void bit_writer_write(BitWriter* writer, uint32_t value, unsigned count) noexcept {
    if (writer->error != WireError::ok)
        return;
    if (count > bit_stream_word_bits) {
        writer->error = WireError::bad_argument;
        return;
    }
    const uint32_t position = writer->bit_position;
    if (position + count < bit_stream_word_bits) {
        or_word(writer, writer->word_index, (low_mask(count) & value) << position);
        writer->bit_position = position + count;
    } else if (position == 0) {
        store_word(writer, writer->word_index, value);
        advance_word(writer);
    } else {
        const uint32_t first = bit_stream_word_bits - position;
        or_word(writer, writer->word_index, (low_mask(first) & value) << position);
        advance_word(writer);
        writer->bit_position = count - first;
        store_word(writer, writer->word_index, low_mask(writer->bit_position) & (value >> first));
    }
}

void bit_writer_write_bit(BitWriter* writer, bool bit) noexcept {
    if (writer->error != WireError::ok)
        return;
    if (bit)
        or_word(writer, writer->word_index, uint32_t{1} << writer->bit_position);
    if (++writer->bit_position == bit_stream_word_bits) {
        writer->bit_position = 0;
        advance_word(writer);
    }
}

void bit_writer_patch_byte(BitWriter* writer, std::size_t index, uint8_t value) noexcept {
    if (index >= writer->capacity_words * bit_stream_word_bytes) {
        writer->error = WireError::overflow;
        return;
    }
    writer->storage[index] = value;
}

std::size_t bit_writer_byte_length(const BitWriter* writer) noexcept {
    return std::size_t{writer->word_index} * bit_stream_word_bytes +
           ((writer->bit_position + 7) >> 3);
}

void bit_reader_init(BitReader* reader, const uint8_t* base, std::size_t size) noexcept {
    reader->base = base;
    reader->size = base == nullptr ? 0 : size;
    reader->word_index = 0;
    reader->bit_position = 0;
}

uint32_t bit_reader_read(BitReader* reader, unsigned count) noexcept {
    const uint32_t position = reader->bit_position;
    const uint32_t word = reader_word(reader, reader->word_index);
    if (position + count < bit_stream_word_bits) {
        reader->bit_position = position + count;
        return (word >> position) & low_mask(count);
    }
    if (position == 0) {
        ++reader->word_index;
        return word;
    }
    const uint32_t first = bit_stream_word_bits - position;
    const uint32_t rest = count - first;
    reader->bit_position = rest;
    ++reader->word_index;
    const uint32_t next = reader_word(reader, reader->word_index);
    return ((word >> position) & low_mask(first)) | ((next & low_mask(rest)) << first);
}

uint32_t bit_reader_read_signed(BitReader* reader, unsigned count) noexcept {
    auto value = bit_reader_read(reader, count);
    if (value & (uint32_t{1} << ((count - 1) & 31u)))
        value |= ~uint32_t{0} << (count & 31u);
    return value;
}

bool bit_reader_read_bit(BitReader* reader) noexcept {
    return bit_reader_read(reader, 1) != 0;
}

std::size_t bit_reader_bits_consumed(const BitReader* reader) noexcept {
    return std::size_t{reader->word_index} * bit_stream_word_bits + reader->bit_position;
}

bool bit_reader_overrun(const BitReader* reader) noexcept {
    return bit_reader_bits_consumed(reader) > reader->size * 8;
}

} // namespace oa::netgame
