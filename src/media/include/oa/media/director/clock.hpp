// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The director's clock: which game tick each video frame shows, which audio
// samples each frame covers and where the video's chunks begin. Rates are
// exact fractions and every step is integer arithmetic, so every platform
// cuts the same frames, samples and chunks.
//
// Frame f shows the world after tick first_tick + floor(f * tickrate /
// framerate) has run. Frame f's audio is the samples [floor(f * 48000 /
// framerate), floor((f + 1) * 48000 / framerate)): the frames' sample counts
// add up to the whole with no drift. A sound that starts while tick t runs
// starts on the first sample of the first frame that shows tick t.
#pragma once

#include "oa/formats/oascript.hpp"

#include <cstdint>

namespace oa::media::director {

/// Audio samples a second of video holds, per channel.
inline constexpr uint32_t audio_sample_rate = 48000;
/// Audio channels: left and right.
inline constexpr uint32_t audio_channels = 2;
/// The most frames a video holds. With the script's limits on rates
/// (oascript.hpp) every product the clock forms stays within 63 bits up to
/// this frame.
inline constexpr uint64_t max_frame_count = uint64_t{1} << 31;

/// An exact fraction, numerator / denominator, with a positive denominator.
struct Rational {
    int64_t numerator{};
    int64_t denominator{1};
};

/// Returns a decimal as a fraction: its mantissa over 10^places.
///
/// @param value the decimal; at most max_rate_places places keeps the
///        clock's products within 63 bits
/// @return the fraction, not reduced
[[nodiscard]] Rational rational_of(oa::formats::oascript::Decimal value) noexcept;

/// The rates that tie frames to ticks and samples.
struct FrameClock {
    uint32_t first_tick{};     ///< the tick frame 0 shows: the first shot's tick
    Rational tickrate{30, 1};  ///< game ticks a second of video; above zero
    Rational framerate{60, 1}; ///< frames a second of video; above zero
};

/// Returns the tick a frame shows.
///
/// @param clock the clock
/// @param frame the frame, counted from 0
/// @return first_tick + floor(frame * tickrate / framerate)
[[nodiscard]] uint32_t frame_tick(const FrameClock& clock, uint64_t frame) noexcept;

/// Returns the first frame that shows a tick or a later one.
///
/// @param clock the clock
/// @param tick the tick; ticks before first_tick give frame 0
/// @return the smallest frame whose frame_tick is at least `tick`
[[nodiscard]] uint64_t first_frame_of_tick(const FrameClock& clock, uint32_t tick) noexcept;

/// A run of audio samples, [first, end), per channel.
struct SampleRange {
    uint64_t first{};
    uint64_t end{};
};

/// Returns the audio samples a frame covers.
///
/// @param clock the clock
/// @param frame the frame, counted from 0
/// @return [floor(frame * audio_sample_rate / framerate),
///         floor((frame + 1) * audio_sample_rate / framerate))
[[nodiscard]] SampleRange frame_samples(const FrameClock& clock, uint64_t frame) noexcept;

/// Returns a duration in whole frames, rounded to the nearest, halves up.
///
/// @param clock the clock whose frame rate counts
/// @param seconds the duration, at least zero
/// @return round(seconds * framerate)
[[nodiscard]] uint64_t
frames_of_seconds(const FrameClock& clock, oa::formats::oascript::Decimal seconds) noexcept;

/// How the video is cut into chunks: output.chunking as fractions.
struct ChunkRule {
    oa::formats::oascript::ChunkMode mode{oa::formats::oascript::ChunkMode::seconds};
    Rational length{60, 1}; ///< seconds of video, or game ticks; above zero
};

/// Returns the first frame of a chunk.
///
/// A chunk of `seconds` mode starts at frame ceil(chunk * length *
/// framerate); a chunk of `ticks` mode at the first frame that shows tick
/// first_tick + chunk * length or a later one.
///
/// @param clock the clock
/// @param rule the chunking
/// @param chunk the chunk, counted from 0
/// @return its first frame
[[nodiscard]] uint64_t
chunk_first_frame(const FrameClock& clock, const ChunkRule& rule, uint32_t chunk) noexcept;

/// Returns the chunk a frame belongs to.
///
/// @param clock the clock
/// @param rule the chunking
/// @param frame the frame, counted from 0
/// @return the chunk whose frames hold `frame`
[[nodiscard]] uint32_t
chunk_of_frame(const FrameClock& clock, const ChunkRule& rule, uint64_t frame) noexcept;

/// Returns how many chunks a video of some frames is cut into.
///
/// @param clock the clock
/// @param rule the chunking
/// @param frame_count the video's frames
/// @return the chunks, the last possibly shorter; 0 for no frames
[[nodiscard]] uint32_t
chunk_count(const FrameClock& clock, const ChunkRule& rule, uint64_t frame_count) noexcept;

} // namespace oa::media::director
