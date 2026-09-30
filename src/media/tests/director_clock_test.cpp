// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The director's clock: frame ticks at common frame and tick rates, the
// first frame of a tick, audio sample ranges without drift, durations in
// frames, chunk boundaries in both modes, and exact results at the largest
// values the script allows.

#include "oa/media/director/clock.hpp"

#include <cstdint>
#include <iostream>
#include <limits>
#include <source_location>
#include <string_view>
#include <vector>

namespace director = oa::media::director;
namespace oascript = oa::formats::oascript;

namespace {

int failures = 0;

/// Records a failed expectation with its line.
void expect(
    bool value,
    std::string_view what,
    const std::source_location where = std::source_location::current()
) {
    if (value)
        return;
    ++failures;
    std::cerr << where.file_name() << ':' << where.line() << ": " << what << '\n';
}

/// Returns a clock from decimal rates.
director::FrameClock
clock_of(uint32_t first_tick, oascript::Decimal tickrate, oascript::Decimal framerate) {
    return director::FrameClock{
        first_tick, director::rational_of(tickrate), director::rational_of(framerate)
    };
}

/// Checks frame_tick for frames 0 onward against a table.
void expect_ticks(
    const director::FrameClock& clock,
    const std::vector<uint32_t>& ticks,
    const std::source_location where = std::source_location::current()
) {
    for (size_t frame{}; frame < ticks.size(); ++frame)
        expect(director::frame_tick(clock, frame) == ticks[frame], "frame tick table", where);
}

/// Checks that first_frame_of_tick is the smallest frame showing the tick,
/// by search over the first frames.
void expect_first_frames(
    const director::FrameClock& clock,
    uint32_t last_tick,
    const std::source_location where = std::source_location::current()
) {
    uint64_t frame{};
    for (uint32_t tick{clock.first_tick}; tick <= last_tick; ++tick) {
        while (director::frame_tick(clock, frame) < tick)
            ++frame;
        expect(director::first_frame_of_tick(clock, tick) == frame, "first frame of a tick", where);
    }
}

/// Checks that the frames' sample ranges join up and add up to a whole.
void expect_samples(
    const director::FrameClock& clock,
    uint64_t frames,
    uint64_t total,
    const std::source_location where = std::source_location::current()
) {
    uint64_t next{};
    for (uint64_t frame{}; frame < frames; ++frame) {
        const director::SampleRange range{director::frame_samples(clock, frame)};
        expect(range.first == next, "sample ranges join up", where);
        expect(range.end > range.first, "every frame has samples", where);
        next = range.end;
    }
    expect(next == total, "sample total", where);
}

/// Checks that chunk_of_frame is the last chunk starting at or before each
/// frame, and chunk_count the chunks up to the last frame.
void expect_chunks_agree(
    const director::FrameClock& clock,
    const director::ChunkRule& rule,
    uint64_t frames,
    const std::source_location where = std::source_location::current()
) {
    uint32_t chunk{};
    for (uint64_t frame{}; frame < frames; ++frame) {
        while (director::chunk_first_frame(clock, rule, chunk + 1) <= frame)
            ++chunk;
        expect(director::chunk_of_frame(clock, rule, frame) == chunk, "chunk of a frame", where);
        expect(director::chunk_count(clock, rule, frame + 1) == chunk + 1, "chunk count", where);
    }
}

void test_rational_of() {
    const director::Rational rate{director::rational_of(oascript::Decimal{2997, 2})};
    expect(rate.numerator == 2997 && rate.denominator == 100, "29.97 is 2997 / 100");
    const director::Rational whole{director::rational_of(oascript::Decimal{60, 0})};
    expect(whole.numerator == 60 && whole.denominator == 1, "60 is 60 / 1");
    const director::Rational kept{director::rational_of(oascript::Decimal{30000, 3})};
    expect(kept.numerator == 30000 && kept.denominator == 1000, "30.000 is not reduced");
}

void test_frame_ticks() {
    expect_ticks(clock_of(0, {30, 0}, {60, 0}), {0, 0, 1, 1, 2, 2, 3, 3});
    expect_ticks(clock_of(0, {30, 0}, {30, 0}), {0, 1, 2, 3, 4, 5});
    expect_ticks(clock_of(0, {30, 0}, {24, 0}), {0, 1, 2, 3, 5, 6, 7, 8, 10});
    expect_ticks(clock_of(0, {45, 0}, {60, 0}), {0, 0, 1, 2, 3, 3, 4, 5, 6});
    expect_ticks(clock_of(100, {30, 0}, {60, 0}), {100, 100, 101, 101, 102});

    // 29.97 frames a second at 30 ticks: one tick in a thousand is never shown.
    const director::FrameClock ntsc{clock_of(0, {30, 0}, {29970, 3})};
    expect(director::frame_tick(ntsc, 998) == 998, "29.97: frame 998");
    expect(director::frame_tick(ntsc, 999) == 1000, "29.97: frame 999 skips tick 999");
    expect(director::first_frame_of_tick(ntsc, 999) == 999, "29.97: tick 999 first shown later");
    expect(director::first_frame_of_tick(ntsc, 1000) == 999, "29.97: tick 1000");

    expect(director::first_frame_of_tick(clock_of(100, {30, 0}, {60, 0}), 50) == 0, "early tick");
    expect(director::first_frame_of_tick(clock_of(100, {30, 0}, {60, 0}), 103) == 6, "offset");

    expect_first_frames(clock_of(0, {30, 0}, {60, 0}), 400);
    expect_first_frames(clock_of(7, {30, 0}, {24, 0}), 400);
    expect_first_frames(clock_of(0, {30, 0}, {29970, 3}), 3000);
    expect_first_frames(clock_of(3, {45, 0}, {60, 0}), 400);
    expect_first_frames(clock_of(0, {300, 0}, {60, 0}), 400);
    expect_first_frames(clock_of(0, {1, 3}, {239999, 3}), 5);
}

void test_frame_samples() {
    const director::FrameClock sixty{clock_of(0, {30, 0}, {60, 0})};
    expect(director::frame_samples(sixty, 0).end == 800, "60 fps: 800 samples a frame");
    expect(director::frame_samples(sixty, 5).first == 4000, "60 fps: frame 5");
    expect_samples(sixty, 60 * 30, uint64_t{director::audio_sample_rate} * 30);
    expect_samples(
        clock_of(0, {30, 0}, {24, 0}), 24 * 30, uint64_t{director::audio_sample_rate} * 30
    );
    // 2997 frames at 29.97 are exactly 100 seconds.
    expect_samples(
        clock_of(0, {30, 0}, {2997, 2}), 2997, uint64_t{director::audio_sample_rate} * 100
    );
    const director::SampleRange ntsc{director::frame_samples(clock_of(0, {30, 0}, {2997, 2}), 0)};
    expect(ntsc.first == 0 && ntsc.end == 1601, "29.97: first frame's samples");
    expect_samples(
        clock_of(0, {30, 0}, {23976, 3}), 23976, uint64_t{director::audio_sample_rate} * 1000
    );
}

void test_frames_of_seconds() {
    expect(director::frames_of_seconds(clock_of(0, {30, 0}, {60, 0}), {5, 1}) == 30, "0.5 s at 60");
    expect(
        director::frames_of_seconds(clock_of(0, {30, 0}, {2997, 2}), {5, 1}) == 15, "0.5 s at 29.97"
    );
    expect(
        director::frames_of_seconds(clock_of(0, {30, 0}, {30, 0}), {25, 2}) == 8, "halves round up"
    );
    expect(
        director::frames_of_seconds(clock_of(0, {30, 0}, {60, 0}), {25, 3}) == 2, "1.5 frames is 2"
    );
    expect(
        director::frames_of_seconds(clock_of(0, {30, 0}, {60, 0}), {8, 3}) == 0, "0.48 frames is 0"
    );
    expect(
        director::frames_of_seconds(clock_of(0, {30, 0}, {60, 0}), {9, 3}) == 1, "0.54 frames is 1"
    );
    expect(director::frames_of_seconds(clock_of(0, {30, 0}, {60, 0}), {0, 0}) == 0, "no time");
    expect(director::frames_of_seconds(clock_of(0, {30, 0}, {60, 0}), {-5, 0}) == 0, "below zero");
    expect(
        director::frames_of_seconds(clock_of(0, {30, 0}, {240, 0}), {60, 0}) == 14400,
        "the longest transition at the highest frame rate"
    );
}

void test_chunks() {
    const director::ChunkRule minute{oascript::ChunkMode::seconds, {60, 1}};
    const director::FrameClock sixty{clock_of(0, {30, 0}, {60, 0})};
    expect(director::chunk_first_frame(sixty, minute, 0) == 0, "seconds: chunk 0");
    expect(director::chunk_first_frame(sixty, minute, 1) == 3600, "seconds: chunk 1");
    expect(director::chunk_first_frame(sixty, minute, 7) == 25200, "seconds: chunk 7");
    expect(director::chunk_of_frame(sixty, minute, 3599) == 0, "seconds: last frame of chunk 0");
    expect(director::chunk_of_frame(sixty, minute, 3600) == 1, "seconds: first frame of chunk 1");
    expect(director::chunk_count(sixty, minute, 0) == 0, "no frames, no chunks");
    expect(director::chunk_count(sixty, minute, 3600) == 1, "one whole chunk");
    expect(director::chunk_count(sixty, minute, 3601) == 2, "a short last chunk");

    const director::FrameClock ntsc{clock_of(0, {30, 0}, {29970, 3})};
    const std::vector<uint64_t> ntsc_starts{0, 1799, 3597, 5395, 7193, 8991};
    for (uint32_t chunk{}; chunk < ntsc_starts.size(); ++chunk)
        expect(
            director::chunk_first_frame(ntsc, minute, chunk) == ntsc_starts[chunk],
            "29.97: chunk start"
        );
    expect_chunks_agree(ntsc, minute, 20000);
    expect_chunks_agree(sixty, director::ChunkRule{oascript::ChunkMode::seconds, {7, 1}}, 5000);
    expect_chunks_agree(
        clock_of(0, {30, 0}, {24, 0}),
        director::ChunkRule{oascript::ChunkMode::seconds, {1333, 1000}},
        5000
    );

    const director::ChunkRule ticks{oascript::ChunkMode::ticks, {1800, 1}};
    expect(director::chunk_first_frame(sixty, ticks, 1) == 3600, "ticks: 30 at 60");
    expect(
        director::chunk_first_frame(clock_of(100, {30, 0}, {60, 0}), ticks, 2) == 7200,
        "ticks: counted from the first tick"
    );
    expect(
        director::chunk_first_frame(clock_of(0, {45, 0}, {60, 0}), ticks, 3) == 7200,
        "ticks: 45 at 60"
    );
    const director::ChunkRule thousand{oascript::ChunkMode::ticks, {1000, 1}};
    expect(director::chunk_first_frame(ntsc, thousand, 4) == 3996, "ticks: 29.97");
    expect_chunks_agree(ntsc, thousand, 20000);
    expect_chunks_agree(clock_of(13, {45, 0}, {24, 0}), ticks, 20000);
    expect_chunks_agree(
        clock_of(0, {3000, 0}, {60, 0}),
        director::ChunkRule{oascript::ChunkMode::ticks, {7, 1}},
        2000
    );
    // A tick past the largest a script names saturates there.
    expect(
        director::chunk_first_frame(sixty, ticks, 4000000000U) ==
            director::first_frame_of_tick(sixty, oascript::max_tick),
        "ticks: saturates at max_tick"
    );
}

void test_maxima() {
    // frame * tickrate numerator * framerate denominator near 2^63 at the
    // script's limits.
    const director::FrameClock fine{clock_of(0, {30001, 3}, {239999, 3})};
    expect(director::frame_tick(fine, director::max_frame_count) == 268445522, "2^31 frames");
    const director::FrameClock fast{clock_of(0, {2999999, 3}, {239999, 3})};
    expect(director::frame_tick(fast, 343590000) == 4294891463U, "the largest tick rate");
    expect(
        director::frame_tick(fast, 343597383) == std::numeric_limits<uint32_t>::max(),
        "a tick past 32 bits saturates"
    );
    expect(director::first_frame_of_tick(fast, 4000000000U) == 319998774, "first frame, fast");
    const director::FrameClock slow{clock_of(0, {1, 3}, {240, 0})};
    expect(
        director::first_frame_of_tick(slow, oascript::max_tick) == 515396075280000ULL,
        "first frame, the smallest tick rate"
    );
    const director::FrameClock top{clock_of(0, {30, 0}, {239999, 3})};
    expect(
        director::frame_samples(top, director::max_frame_count).first == 429498519177ULL,
        "samples at 2^31 frames"
    );
    // Past 64-bit products the clock stays exact.
    const director::SampleRange far{
        director::frame_samples(clock_of(0, {30, 0}, {29970, 3}), uint64_t{1} << 40)
    };
    expect(far.first == 1760979584025625ULL && far.end == 1760979584027227ULL, "samples past 2^64");
}

} // namespace

int main() {
    test_rational_of();
    test_frame_ticks();
    test_frame_samples();
    test_frames_of_seconds();
    test_chunks();
    test_maxima();
    if (failures != 0) {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }
    return 0;
}
