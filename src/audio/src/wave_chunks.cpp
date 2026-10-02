// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/audio/wave_chunks.hpp"
#include "oa/base/bytes.hpp"

#include <cstdint>
#include <cstring>

namespace oa::audio {
namespace {
using base::bytes::load_le32;
using base::bytes::load_le16;

constexpr char tag_riff[4] = {'R', 'I', 'F', 'F'};
constexpr char tag_wave[4] = {'W', 'A', 'V', 'E'};
constexpr char tag_digi[4] = {'D', 'I', 'G', 'I'};
constexpr char tag_hshd[4] = {'H', 'S', 'H', 'D'};
constexpr char tag_sdat[4] = {'S', 'D', 'A', 'T'};
constexpr char tag_format[4] = {'f', 'm', 't', ' '};
constexpr char tag_data[4] = {'d', 'a', 't', 'a'};

constexpr uint32_t riff_size_offset = 4;
constexpr uint32_t riff_form_offset = 8;
constexpr uint32_t riff_first_chunk = 12;
constexpr uint32_t riff_chunk_header = 8;
constexpr uint32_t digi_header_tag_offset = 8;
constexpr uint32_t digi_samples_tag_offset = 0x20;

bool same_tag(const char* a, const char* b) noexcept {
    return std::memcmp(a, b, 4) == 0;
}

uint32_t read_u32(WaveCursor& cursor) noexcept {
    uint8_t bytes[4]{};
    wave_read(cursor, bytes, 4);
    return load_le32(bytes);
}

} // namespace

WaveCursor wave_cursor(const uint8_t* data, uint32_t size) noexcept {
    return WaveCursor{data, size, 0};
}

void wave_seek(WaveCursor& cursor, uint32_t position) noexcept {
    cursor.position = position;
}

uint32_t wave_read(WaveCursor& cursor, void* destination, uint32_t bytes) noexcept {
    if (cursor.position >= cursor.size)
        return 0;
    const uint32_t available = cursor.size - cursor.position;
    const uint32_t count = bytes < available ? bytes : available;
    std::memcpy(destination, cursor.data + cursor.position, count);
    cursor.position += count;
    return count;
}

uint32_t wave_remaining(const WaveCursor& cursor) noexcept {
    return cursor.position >= cursor.size ? 0 : cursor.size - cursor.position;
}

WaveContainer detect_wave_container(WaveCursor& cursor) noexcept {
    // One 4-byte buffer serves every tag read, so a short read keeps the
    // previous tag's bytes.
    char tag[4]{};
    wave_seek(cursor, 0);
    wave_read(cursor, tag, 4);
    if (same_tag(tag, tag_digi)) {
        wave_seek(cursor, digi_header_tag_offset);
        wave_read(cursor, tag, 4);
        if (same_tag(tag, tag_hshd)) {
            wave_seek(cursor, digi_samples_tag_offset);
            wave_read(cursor, tag, 4);
            if (same_tag(tag, tag_sdat))
                return WaveContainer::digi;
        }
    }
    if (same_tag(tag, tag_riff)) {
        wave_seek(cursor, riff_form_offset);
        wave_read(cursor, tag, 4);
        if (same_tag(tag, tag_wave))
            return WaveContainer::riff;
    }
    return WaveContainer::raw;
}

uint32_t find_wave_chunk(WaveCursor& cursor, const char tag[4]) noexcept {
    wave_seek(cursor, riff_size_offset);
    const uint32_t end = read_u32(cursor) + riff_chunk_header;
    wave_seek(cursor, riff_first_chunk);
    char chunk[4]{};
    wave_read(cursor, chunk, 4);
    uint32_t size = read_u32(cursor);
    uint32_t payload = riff_first_chunk + riff_chunk_header;
    for (;;) {
        if (same_tag(chunk, tag))
            return size;
        const uint32_t next = payload + size;
        wave_seek(cursor, next);
        if (end <= next)
            return 0;
        wave_read(cursor, chunk, 4);
        size = read_u32(cursor);
        payload = next + riff_chunk_header;
    }
}

bool describe_wave(WaveCursor& cursor, WaveLayout& layout) noexcept {
    layout = WaveLayout{};
    layout.container = detect_wave_container(cursor);
    const uint32_t length = cursor.size;
    switch (layout.container) {
    case WaveContainer::raw:
        wave_seek(cursor, 0);
        layout.format = PcmFormat{default_sample_rate, 8, 1};
        layout.data_bytes = length;
        break;
    case WaveContainer::digi: {
        wave_seek(cursor, digi_rate_offset);
        uint32_t rate = read_u32(cursor);
        if (rate == digi_nominal_rate)
            rate = default_sample_rate;
        wave_seek(cursor, digi_samples_offset);
        layout.format = PcmFormat{rate, 8, 1};
        // Unsigned; a short file fails the later read.
        layout.data_bytes = length - digi_samples_offset;
        break;
    }
    case WaveContainer::riff: {
        if (find_wave_chunk(cursor, tag_format) < riff_minimum_format_bytes)
            return false;
        uint8_t format[riff_minimum_format_bytes]{};
        wave_read(cursor, format, sizeof(format));
        layout.format =
            PcmFormat{load_le32(format + 4), load_le16(format + 14), load_le16(format + 2)};
        const uint32_t data_bytes = find_wave_chunk(cursor, tag_data);
        if (static_cast<int32_t>(data_bytes) < 1)
            return false;
        layout.data_bytes = data_bytes;
        break;
    }
    }
    layout.data_offset = cursor.position;
    return true;
}

} // namespace oa::audio
