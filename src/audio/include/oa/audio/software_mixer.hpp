// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "oa/audio/resampler.hpp"
#include "oa/audio/sound_output.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace oa::audio {

/// Sample rate of the mixed output, in hertz.
inline constexpr uint32_t mixer_output_rate = 44100;
/// Channels of the mixed output: left and right, interleaved.
inline constexpr uint32_t mixer_output_channels = 2;
/// Most input frames a stream converts in one step.
inline constexpr uint32_t mixer_convert_frames = 1024;
/// Most times one mix asks a stream's feed for samples.
inline constexpr uint32_t mixer_feed_calls = 4;

/// The lock that keeps the mixing thread and the streams' owners apart.
struct MixerLock {
    void* context{};
    /// Takes the lock; the thread holding it may take it again. Null takes no lock.
    void (*lock)(void* context){};
    /// Releases one hold of the lock. Null releases nothing.
    void (*unlock)(void* context){};
};

class MixerStream;

/// Mixes streams of any format into 16-bit stereo at mixer_output_rate.
///
/// Each stream's samples are scaled to -1..1 (8-bit unsigned around 128,
/// integers by 2^-(bits-1)), one channel is played on both sides and more
/// than two play their first two, the rate is converted by Resampler, and
/// the streams are multiplied by their gains and summed. The sum is
/// clamped to -1..1 and rounded to 16 bits by 32767. A paused stream
/// keeps its samples and adds nothing.
class SoftwareMixer {
  public:

    /// Creates a mixer with no streams.
    ///
    /// @param lock the lock that mix() and every stream call take
    explicit SoftwareMixer(MixerLock lock);
    /// Destroys the mixer; every stream must have been destroyed first.
    ~SoftwareMixer();
    SoftwareMixer(const SoftwareMixer&) = delete;
    SoftwareMixer& operator=(const SoftwareMixer&) = delete;

    /// Opens a paused stream that the mixer mixes until it is destroyed.
    ///
    /// @param format the samples the stream takes; 1 to max_stream_channels
    ///        channels at a rate the resampler converts
    /// @param feed asks for more samples as the stream plays; null for none
    /// @param context passed back to the feed
    /// @param[out] error why the stream cannot be opened; untouched on success
    /// @return the stream, or null when the format is outside those limits
    [[nodiscard]] std::unique_ptr<OutputStream>
    open_stream(const StreamFormat& format, StreamFeed feed, void* context, std::string& error);

    /// Mixes the next frames of every playing stream, taking the lock.
    ///
    /// @param[out] out room for frames * mixer_output_channels samples
    /// @param frames frames to mix
    void mix(int16_t* out, uint32_t frames);

    /// Returns how many streams are open.
    ///
    /// @return the open stream count
    [[nodiscard]] std::size_t stream_count();

  private:

    friend class MixerStream;

    /// Takes the mixer's lock.
    void lock() const;

    /// Releases the mixer's lock.
    void unlock() const;

    MixerLock lock_;
    std::vector<MixerStream*> streams_;
    std::vector<float> sum_; ///< the mix of one call, before rounding
};

} // namespace oa::audio
