// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace oa::audio {

/// Lowest sample rate the resampler converts from or to, in hertz.
inline constexpr uint32_t resampler_min_rate = 1000;
/// Highest sample rate the resampler converts from or to, in hertz.
inline constexpr uint32_t resampler_max_rate = 768000;
/// Most interleaved channels the resampler converts at once.
inline constexpr uint32_t resampler_max_channels = 8;
/// Zero crossings of the interpolation kernel on each side of its centre,
/// counted at the lower of the two rates.
inline constexpr int32_t resampler_zero_crossings = 16;
/// Most kernel phases tabled exactly. A ratio whose output frames fall on
/// more distinct positions between two input frames interpolates linearly
/// between this many evenly spaced tabled phases.
inline constexpr uint32_t resampler_max_exact_phases = 1024;
/// Passband edge, as a fraction of the lower rate's half sample rate.
inline constexpr double resampler_cutoff = 0.97;
/// Shape of the Kaiser window that tapers the kernel; larger values trade a
/// wider transition band for less stop-band leakage.
inline constexpr double resampler_kaiser_beta = 9.0;

/// Converts interleaved float samples from one sample rate to another.
///
/// Output frame n is the band-limited value of the input at input frame
/// n * input_rate / output_rate: a Kaiser-windowed sinc kernel centred on
/// that position, so the output has no delay. Where the kernel reaches
/// before the first input frame or after the last, the input is mirrored
/// about its ends: before the first frame, the frames after it in reverse;
/// after the last, the last frames in reverse, the last one first, so a
/// constant signal stays constant to its ends. A whole input of N frames gives
/// ceil(N * output_rate / input_rate) output frames. Equal rates copy the
/// samples unchanged. The output does not depend on how the input is split
/// between calls.
class Resampler {
  public:

    /// Sets up a conversion and forgets any input already given.
    ///
    /// @param input_rate rate of the input, in hertz
    /// @param output_rate rate of the output, in hertz
    /// @param channels interleaved channels in each frame
    /// @return false, leaving the resampler unconfigured, when a rate or the
    ///         channel count is outside the resampler's limits
    [[nodiscard]] bool configure(uint32_t input_rate, uint32_t output_rate, uint32_t channels);

    /// Forgets the input given so far, so the next input starts a new signal.
    void reset() noexcept;

    /// Converts more input, appending every output frame it completes.
    ///
    /// @param input interleaved input frames; a trailing partial frame is ignored
    /// @param[in,out] output receives the completed interleaved output frames
    void process(std::span<const float> input, std::vector<float>& output);

    /// Ends the input, appending the output frames that the last input frames still owe.
    ///
    /// @param[in,out] output receives the remaining interleaved output frames
    void finish(std::vector<float>& output);

    /// Tells whether the input and output rates are equal, so samples are copied.
    ///
    /// @return true for equal rates
    [[nodiscard]] bool passthrough() const noexcept { return input_rate_ == output_rate_; }

    /// Returns the interleaved channels of each frame.
    ///
    /// @return the configured channel count, 0 before configure succeeds
    [[nodiscard]] uint32_t channels() const noexcept { return channels_; }

  private:

    /// Puts the mirrored frames before the first input frame ahead of the input held.
    void prime();

    /// Appends every output frame whose kernel the held input covers.
    ///
    /// @param[in,out] output receives the interleaved output frames
    /// @param finishing true once the input has ended, which stops at the owed frame count
    void produce(std::vector<float>& output, bool finishing);

    /// Returns the kernel taps of an output position between two input frames.
    ///
    /// @param phase the position past the input frame, in 1/upsample_ths of a frame
    /// @param[out] blended room for the taps when they are interpolated between tabled phases
    /// @return taps_ taps, in the table or in `blended`
    [[nodiscard]] const float* phase_taps(uint32_t phase, std::vector<float>& blended) const;

    uint32_t input_rate_{};
    uint32_t output_rate_{};
    uint32_t channels_{};
    uint32_t upsample_{};      ///< output_rate over the rates' greatest common divisor
    uint32_t downsample_{};    ///< input_rate over the rates' greatest common divisor
    uint32_t taps_{};          ///< kernel taps per phase, an even count
    uint32_t phases_{};        ///< phases tabled; one more row closes an interpolated table
    bool exact_phases_{};      ///< true when every output position has its own row
    std::vector<float> table_; ///< phases_ (+1 when interpolated) rows of taps_ taps
    std::vector<float> scratch_taps_;
    std::vector<float> history_; ///< interleaved input frames from history_start_ on
    int64_t history_start_{}; ///< input frame of history_'s first frame; negative for lead-in zeros
    int64_t next_input_{};    ///< input frame at or before the next output frame
    uint32_t next_phase_{};   ///< the next output frame's offset past next_input_, in upsample_ths
    uint64_t input_frames_{}; ///< real input frames given since the last reset
    uint64_t output_frames_{}; ///< output frames produced since the last reset
    bool primed_{};            ///< the frames before the first input frame are in history_
};

} // namespace oa::audio
