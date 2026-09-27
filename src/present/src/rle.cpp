// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/present/rle.hpp"

#include <cstddef>

namespace oa::present {

namespace {

constexpr uint8_t command_skip = 0x01;
constexpr uint8_t command_fill = 0x02;
constexpr uint8_t command_copy = 0x00; // neither flag bit
// A skip command's pixel count sits above its flag bit; a fill or copy
// command's count, less one, above its two flag bits.
constexpr int32_t skip_count_shift = 1;
constexpr int32_t run_count_shift = 2;

// How a command that straddles the left clip edge resumes inside it.
enum class Resume : uint8_t { none, skip, fill, copy };

// The commands of one row: stream bytes [at, end) of a row that lies inside
// the stream.
struct RowCommands {
    const uint8_t* bytes{};
    size_t at{};
    size_t end{};
};

/// Finds the commands of the row whose length word is at an offset of a stream.
///
/// @param stream row-RLE stream
/// @param at offset of the row's length word
/// @param[out] row the row's commands; set only on success
/// @return false when the length word, or the commands it counts, leave the stream
bool find_row(std::span<const uint8_t> stream, size_t at, RowCommands& row) noexcept {
    if (stream.size() < rle_row_length_bytes || at > stream.size() - rle_row_length_bytes) {
        return false;
    }
    const size_t length =
        static_cast<size_t>(stream[at]) | (static_cast<size_t>(stream[at + 1]) << 8);
    const size_t first = at + rle_row_length_bytes;
    if (length > stream.size() - first) {
        return false;
    }
    row = RowCommands{stream.data(), first, first + length};
    return true;
}

/// Decodes columns lead .. lead + width - 1 of one row, reading its commands only inside the row.
///
/// Commands wholly left of `lead` are stepped over, and one straddling it
/// resumes with its inside part; skipped pixels leave the destination alone.
///
/// @param row the row's commands
/// @param lead first column decoded; zero or less starts at the row's first pixel
/// @param width columns decoded; at least one
/// @param[in,out] out destination of the first decoded column
/// @param put writes one pixel from a source byte (the fill colour or a literal)
/// @return false when the commands end before the last column, after drawing the columns they cover
template <typename Put>
bool decode_row(
    const RowCommands& row, int64_t lead, int64_t width, uint8_t* out, Put put
) noexcept {
    size_t in = row.at;
    Resume resume = Resume::none;
    int64_t count = 0;
    int64_t skipping = lead;
    while (skipping > 0) {
        if (in >= row.end) {
            return false;
        }
        const uint8_t command = row.bytes[in++];
        if ((command & command_skip) != 0) {
            skipping -= command >> skip_count_shift;
            if (skipping < 0) {
                resume = Resume::skip;
                count = -skipping;
            }
            continue;
        }
        const int64_t run = (command >> run_count_shift) + 1;
        skipping -= run;
        if ((command & command_fill) != 0) {
            // A straddling fill resumes at its colour byte.
            if (skipping < 0) {
                resume = Resume::fill;
                count = -skipping;
            } else {
                ++in;
            }
        } else if (skipping < 0) {
            // A straddling copy resumes with its last literals.
            in += static_cast<size_t>(run + skipping);
            resume = Resume::copy;
            count = -skipping;
        } else {
            in += static_cast<size_t>(run);
        }
    }
    int64_t left = width;
    for (;;) {
        if (resume == Resume::none) {
            if (left <= 0) {
                return true;
            }
            if (in >= row.end) {
                return false;
            }
            const uint8_t command = row.bytes[in++];
            if ((command & command_skip) != 0) {
                resume = Resume::skip;
                count = command >> skip_count_shift;
            } else {
                resume = (command & command_fill) != 0 ? Resume::fill : Resume::copy;
                count = (command >> run_count_shift) + 1;
            }
        }
        const Resume action = resume;
        resume = Resume::none;
        if (action == Resume::skip) {
            if (count >= left) {
                return true;
            }
            out += count;
            left -= count;
            continue;
        }
        if (count > left) {
            count = left;
        }
        left -= count;
        if (action == Resume::fill) {
            if (in >= row.end) {
                return false;
            }
            const uint8_t color = row.bytes[in++];
            for (int64_t i = 0; i < count; ++i) {
                put(out++, color);
            }
        } else {
            if (in > row.end || static_cast<size_t>(count) > row.end - in) {
                return false;
            }
            for (int64_t i = 0; i < count; ++i) {
                put(out++, row.bytes[in++]);
            }
        }
    }
}

/// Decodes a rectangle of a row-RLE stream through `put`, reading only inside the stream.
///
/// Rows before src_rect.y1 are stepped over by their length words; a row of
/// length 0 draws nothing.
///
/// @param[in,out] dst destination pixels
/// @param pitch destination bytes per row
/// @param dst_rect destination; only its top-left corner is used
/// @param stream row-RLE stream starting at its first row
/// @param src_rect inclusive rectangle of the sprite to decode
/// @param put writes one pixel from a source byte (the fill colour or a literal)
/// @return true when every row of the rectangle was decoded; false at the
///     first malformed row, or when src_rect.y1 is negative
template <typename Put>
bool walk_rle_rows(
    uint8_t* dst,
    int32_t pitch,
    const Rect32& dst_rect,
    std::span<const uint8_t> stream,
    const Rect32& src_rect,
    Put put
) noexcept {
    const int64_t rows = static_cast<int64_t>(src_rect.y2) - src_rect.y1 + 1;
    const int64_t width = static_cast<int64_t>(src_rect.x2) - src_rect.x1 + 1;
    if (rows <= 0 || width <= 0) {
        return true;
    }
    if (src_rect.y1 < 0) {
        return false;
    }
    size_t at = 0;
    RowCommands row{};
    for (int32_t skipped = 0; skipped < src_rect.y1; ++skipped) {
        if (!find_row(stream, at, row)) {
            return false;
        }
        at = row.end;
    }
    for (int64_t y = 0; y < rows; ++y) {
        if (!find_row(stream, at, row)) {
            return false;
        }
        uint8_t* out = dst + dst_rect.x1 + (static_cast<std::ptrdiff_t>(dst_rect.y1) + y) * pitch;
        if (row.at != row.end && !decode_row(row, src_rect.x1, width, out, put)) {
            return false;
        }
        at = row.end;
    }
    return true;
}

/// Passes every pixel of a raw source rectangle that is not the key colour to `put`.
///
/// @param[in,out] dst destination surface; `put` writes the pixels under the drawn source pixels
/// @param src source surface
/// @param src_rect inclusive source rectangle; an empty one draws nothing
/// @param dst_rect destination; only its top-left corner is used
/// @param key transparent source colour
/// @param put writes one destination pixel from a source byte
template <typename Put>
void walk_keyed_rect(
    Surface& dst,
    const Surface& src,
    const Rect32& src_rect,
    const Rect32& dst_rect,
    uint8_t key,
    Put put
) noexcept {
    const int32_t width = src_rect.x2 - src_rect.x1 + 1;
    int32_t rows = src_rect.y2 - src_rect.y1 + 1;
    const uint8_t* in =
        src.pixels + static_cast<std::ptrdiff_t>(src_rect.y1) * src.pitch + src_rect.x1;
    uint8_t* out = dst.pixels + static_cast<std::ptrdiff_t>(dst_rect.y1) * dst.pitch + dst_rect.x1;
    if (width <= 0 || rows <= 0) {
        return;
    }
    do {
        for (int32_t x = 0; x < width; ++x) {
            if (in[x] != key) {
                put(out + x, in[x]);
            }
        }
        in += src.pitch;
        out += dst.pitch;
    } while (--rows != 0);
}

/// Returns the copy or fill command byte for a run.
///
/// The byte holds the run's length less one above the flag bits, modulo
/// rle_short_run_max, so a run of 0 comes out as the longest.
///
/// @param run bytes the command covers, 1 to rle_short_run_max
/// @param flags command_fill or command_copy
/// @return the command byte
uint8_t run_command(int32_t run, uint8_t flags) noexcept {
    return static_cast<uint8_t>(((run + rle_short_run_max - 1) << run_count_shift) | flags);
}

} // namespace

uint8_t* emit_literal_runs(RleEncoder& encoder, uint8_t* out, int32_t count) noexcept {
    int32_t index = 0;
    do {
        const int32_t run = count > rle_short_run_max ? rle_short_run_max : count;
        count -= run;
        if (out != nullptr) {
            *out++ = run_command(run, command_copy);
        }
        ++encoder.emitted;
        for (int32_t i = 0; i < run; ++i) {
            const uint8_t value = encoder.literal[index++ & (rle_literal_capacity - 1)];
            if (out != nullptr) {
                *out++ = value;
            }
            ++encoder.emitted;
        }
    } while (count > 0);
    return out;
}

uint8_t* emit_repeat_runs(
    RleEncoder& encoder, uint8_t* out, int32_t count, uint8_t color, uint8_t key
) noexcept {
    if (color == key) {
        do {
            const int32_t run = count > rle_skip_max ? rle_skip_max : count;
            count -= run;
            if (out != nullptr) {
                *out++ = static_cast<uint8_t>((run << skip_count_shift) | command_skip);
            }
            ++encoder.emitted;
        } while (count > 0);
        return out;
    }
    do {
        const int32_t run = count > rle_short_run_max ? rle_short_run_max : count;
        count -= run;
        if (out != nullptr) {
            *out++ = run_command(run, command_fill);
        }
        ++encoder.emitted;
        if (out != nullptr) {
            *out++ = color;
        }
        ++encoder.emitted;
    } while (count > 0);
    return out;
}

// `pending` counts buffered pixels; in literal mode `run_start` marks where a
// trailing repeat began, in run mode where the run's pixels start.
int32_t encode_rle_row(
    RleEncoder& encoder, uint8_t* out, const uint8_t* row, int32_t width, uint8_t key
) noexcept {
    int32_t leading = 0;
    while (leading < width && row[leading] == key) {
        ++leading;
    }
    if (leading >= width) {
        return 0;
    }
    encoder.emitted = 0;
    int32_t run_start = 0;
    uint8_t previous = row[0];
    uint8_t last = previous;
    bool run_mode = previous == key;
    encoder.literal[0] = previous;
    int32_t pending = 1;
    auto restart = [&](uint8_t value) {
        run_start = 0;
        pending = 1;
        encoder.literal[0] = value;
    };
    for (int32_t i = 1; i < width; ++i) {
        const uint8_t value = row[i];
        ++pending;
        encoder.literal[(pending - 1) & (rle_literal_capacity - 1)] = value;
        last = value;
        bool check_run = run_mode;
        if (!run_mode) {
            if (value == key) {
                out = emit_literal_runs(encoder, out, pending - 1);
                restart(value);
                run_mode = true;
            } else if (pending > rle_run_max) {
                out = emit_literal_runs(encoder, out, pending - 1);
                restart(value);
            } else if (value == previous) {
                if (pending - run_start >= rle_min_repeat) {
                    if (run_start > 0) {
                        out = emit_literal_runs(encoder, out, run_start);
                    }
                    run_mode = true;
                } else if (run_start == 0) {
                    run_mode = true;
                }
                check_run = true;
            } else {
                run_start = pending - 1;
            }
        }
        if (check_run && (value != previous || pending - run_start > rle_run_max)) {
            out = emit_repeat_runs(encoder, out, pending - run_start - 1, previous, key);
            restart(value);
            run_mode = value == key;
        }
        previous = value;
    }
    if (run_mode) {
        emit_repeat_runs(encoder, out, pending - run_start, last, key);
    } else {
        emit_literal_runs(encoder, out, pending);
    }
    return encoder.emitted;
}

int32_t encode_rle_sprite(RleEncoder& encoder, uint8_t* out, const Sprite& sprite) noexcept {
    const int32_t width = sprite.width;
    const auto* row = static_cast<const uint8_t*>(sprite.data);
    int32_t total = 0;
    for (int32_t y = 0; y < sprite.height; ++y, row += width) {
        uint8_t* length_slot = out;
        uint8_t* body = out != nullptr ? out + rle_row_length_bytes : nullptr;
        const int32_t size = encode_rle_row(encoder, body, row, width, sprite.key);
        total += size + static_cast<int32_t>(rle_row_length_bytes);
        if (out != nullptr) {
            length_slot[0] = static_cast<uint8_t>(size);
            length_slot[1] = static_cast<uint8_t>(size >> 8);
            out = body + size;
        }
    }
    return total;
}

bool decode_rle_rows(
    uint8_t* dst,
    int32_t pitch,
    const Rect32& dst_rect,
    std::span<const uint8_t> stream,
    const Rect32& src_rect
) noexcept {
    return walk_rle_rows(dst, pitch, dst_rect, stream, src_rect, [](uint8_t* out, uint8_t value) {
        *out = value;
    });
}

bool decode_rle_rows_blended(
    uint8_t* dst,
    int32_t pitch,
    const Rect32& dst_rect,
    std::span<const uint8_t> stream,
    const Rect32& src_rect,
    const uint8_t* table
) noexcept {
    return walk_rle_rows(
        dst, pitch, dst_rect, stream, src_rect, [table](uint8_t* out, uint8_t value) {
            *out = table[value * palette_table_row_bytes + *out];
        }
    );
}

void copy_rect_blended(
    Surface& dst,
    const Surface& src,
    const Rect32& src_rect,
    const Rect32& dst_rect,
    uint8_t key,
    const uint8_t* table
) noexcept {
    walk_keyed_rect(dst, src, src_rect, dst_rect, key, [table](uint8_t* out, uint8_t value) {
        *out = table[value * palette_table_row_bytes + *out];
    });
}

void remap_under_mask(
    Surface& dst,
    const Surface& src,
    const Rect32& src_rect,
    const Rect32& dst_rect,
    uint8_t key,
    const uint8_t* table
) noexcept {
    walk_keyed_rect(dst, src, src_rect, dst_rect, key, [table](uint8_t* out, uint8_t) {
        *out = table[*out];
    });
}

bool decode_rle_rows_shaded(
    uint8_t* dst,
    int32_t pitch,
    const Rect32& dst_rect,
    std::span<const uint8_t> stream,
    const Rect32& src_rect,
    const uint8_t* table
) noexcept {
    return walk_rle_rows(
        dst, pitch, dst_rect, stream, src_rect, [table](uint8_t* out, uint8_t value) {
            *out = table[(value - shade_ramp_base) * palette_table_row_bytes + *out];
        }
    );
}

void copy_rect_shaded(
    Surface& dst,
    const Surface& src,
    const Rect32& src_rect,
    const Rect32& dst_rect,
    uint8_t key,
    const uint8_t* table
) noexcept {
    walk_keyed_rect(dst, src, src_rect, dst_rect, key, [table](uint8_t* out, uint8_t value) {
        *out = table[(value - shade_ramp_base) * palette_table_row_bytes + *out];
    });
}

bool decode_rle_rows_remapped(
    uint8_t* dst,
    int32_t pitch,
    const Rect32& dst_rect,
    std::span<const uint8_t> stream,
    const Rect32& src_rect,
    const uint8_t* table
) noexcept {
    return walk_rle_rows(
        dst, pitch, dst_rect, stream, src_rect, [table](uint8_t* out, uint8_t value) {
            *out = table[value];
        }
    );
}

} // namespace oa::present
