// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/audio/resampler.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <numeric>

namespace oa::audio {
namespace {

// Terms of the power series that evaluates the window's Bessel function;
// the terms fall below double precision well before this many.
constexpr int bessel_terms = 64;

// Evaluates the zeroth-order modified Bessel function of the first kind.
double bessel_i0(double x) {
    double sum = 1.0;
    double term = 1.0;
    const double quarter_square = x * x / 4.0;
    for (int k = 1; k < bessel_terms; ++k) {
        term *= quarter_square / (static_cast<double>(k) * static_cast<double>(k));
        sum += term;
        if (term < sum * 1e-17)
            break;
    }
    return sum;
}

/// The design of a Kaiser-windowed sinc kernel.
struct KernelDesign {
    int32_t zero_crossings{}; ///< zero crossings each side of the centre, at the lower rate
    double cutoff{};          ///< passband edge, as a fraction of the lower rate's half sample rate
    double kaiser_beta{};     ///< the window's shape
};

/// Resampler's kernel.
constexpr KernelDesign float_design{
    resampler_zero_crossings, resampler_cutoff, resampler_kaiser_beta
};
/// PcmResampler's kernel.
constexpr KernelDesign pcm_design{
    pcm_resampler_zero_crossings, pcm_resampler_cutoff, pcm_resampler_kaiser_beta
};

/// The kernel of one conversion: its taps per phase and its shape.
struct KernelShape {
    uint32_t taps{};       ///< taps per phase, an even count
    double band{};         ///< passband edge, as a fraction of the input's half sample rate
    double half_width{};   ///< input frames from the centre to either end
    double kaiser_beta{};  ///< the window's shape
    double window_scale{}; ///< the window's value at its centre
    int32_t
        first_tap{}; ///< the first tap's input frame, relative to the frame at or before the centre
};

// Evaluates the windowed kernel at a distance from its centre, in input frames.
double kernel(double distance, const KernelShape& shape) {
    const double ratio = distance / shape.half_width;
    if (ratio <= -1.0 || ratio >= 1.0)
        return 0.0;
    const double x = std::numbers::pi * shape.band * distance;
    const double sinc = x == 0.0 ? 1.0 : std::sin(x) / x;
    return sinc * bessel_i0(shape.kaiser_beta * std::sqrt(1.0 - ratio * ratio)) /
           shape.window_scale;
}

/// Returns the kernel of a conversion between two rates.
///
/// Downsampling narrows the band and widens the kernel by the same ratio.
///
/// @param input_rate rate of the input, in hertz
/// @param output_rate rate of the output, in hertz
/// @param design the kernel's design
/// @return the kernel's shape
KernelShape kernel_shape(uint32_t input_rate, uint32_t output_rate, const KernelDesign& design) {
    KernelShape shape;
    const double scale = std::min(1.0, static_cast<double>(output_rate) / input_rate);
    shape.band = scale * design.cutoff;
    shape.half_width = design.zero_crossings / scale;
    shape.taps = 2 * static_cast<uint32_t>(std::ceil(shape.half_width));
    shape.kaiser_beta = design.kaiser_beta;
    shape.window_scale = bessel_i0(design.kaiser_beta);
    shape.first_tap = 1 - static_cast<int32_t>(shape.taps / 2);
    return shape;
}

/// Computes the taps of an output position between two input frames, before
/// they are scaled to sum to one.
///
/// @param shape the kernel
/// @param offset the position past the input frame at or before it, 0 to 1
/// @param[out] row receives shape.taps taps
/// @return the sum of the taps
double kernel_row(const KernelShape& shape, double offset, std::vector<double>& row) {
    row.assign(shape.taps, 0.0);
    double sum = 0.0;
    for (uint32_t tap = 0; tap < shape.taps; ++tap) {
        row[tap] = kernel(
            static_cast<double>(shape.first_tap + static_cast<int32_t>(tap)) - offset, shape
        );
        sum += row[tap];
    }
    return sum;
}

/// One more than the largest PcmResampler tap: one, at pcm_resampler_tap_bits fraction bits.
constexpr int32_t pcm_unity_tap = int32_t{1} << pcm_resampler_tap_bits;
/// Added before the taps' fraction bits are dropped, so that the sum rounds to nearest.
constexpr int32_t pcm_rounding = pcm_unity_tap / 2;

} // namespace

bool Resampler::configure(uint32_t input_rate, uint32_t output_rate, uint32_t channels) {
    *this = Resampler{};
    if (input_rate < resampler_min_rate || input_rate > resampler_max_rate ||
        output_rate < resampler_min_rate || output_rate > resampler_max_rate || channels == 0 ||
        channels > resampler_max_channels)
        return false;
    input_rate_ = input_rate;
    output_rate_ = output_rate;
    channels_ = channels;
    const uint32_t divisor = std::gcd(input_rate, output_rate);
    upsample_ = output_rate / divisor;
    downsample_ = input_rate / divisor;
    if (passthrough()) {
        reset();
        return true;
    }
    const KernelShape shape = kernel_shape(input_rate, output_rate, float_design);
    taps_ = shape.taps;
    exact_phases_ = upsample_ <= resampler_max_exact_phases;
    phases_ = exact_phases_ ? upsample_ : resampler_max_exact_phases;
    const uint32_t rows = exact_phases_ ? phases_ : phases_ + 1;
    table_.assign(static_cast<std::size_t>(rows) * taps_, 0.0F);
    std::vector<double> row;
    for (uint32_t phase = 0; phase < rows; ++phase) {
        const double sum = kernel_row(shape, static_cast<double>(phase) / phases_, row);
        // Each phase passes a constant signal at unit gain.
        for (uint32_t tap = 0; tap < taps_; ++tap)
            table_[static_cast<std::size_t>(phase) * taps_ + tap] =
                static_cast<float>(row[tap] / sum);
    }
    scratch_taps_.assign(taps_, 0.0F);
    reset();
    return true;
}

void Resampler::reset() noexcept {
    history_.clear();
    history_start_ = 0;
    next_input_ = 0;
    next_phase_ = 0;
    input_frames_ = 0;
    output_frames_ = 0;
    primed_ = false;
}

void Resampler::prime() {
    // The frames before the first are the first frames mirrored about it,
    // as far as the input reaches, then zeros.
    const auto lead = static_cast<int64_t>(taps_ / 2) - 1;
    const auto frames = static_cast<int64_t>(history_.size() / channels_);
    std::vector<float> lead_in(static_cast<std::size_t>(lead) * channels_, 0.0F);
    for (int64_t back = 1; back <= lead && back < frames; ++back)
        std::copy_n(
            history_.begin() + static_cast<std::ptrdiff_t>(back * channels_),
            channels_,
            lead_in.begin() + static_cast<std::ptrdiff_t>((lead - back) * channels_)
        );
    history_.insert(history_.begin(), lead_in.begin(), lead_in.end());
    history_start_ = -lead;
    primed_ = true;
}

void Resampler::process(std::span<const float> input, std::vector<float>& output) {
    if (channels_ == 0)
        return;
    const std::size_t frames = input.size() / channels_;
    const std::size_t samples = frames * channels_;
    input_frames_ += frames;
    if (passthrough()) {
        output.insert(
            output.end(), input.begin(), input.begin() + static_cast<std::ptrdiff_t>(samples)
        );
        output_frames_ += frames;
        return;
    }
    history_.insert(
        history_.end(), input.begin(), input.begin() + static_cast<std::ptrdiff_t>(samples)
    );
    if (!primed_) {
        if (history_.size() / channels_ < taps_ / 2)
            return;
        prime();
    }
    produce(output, false);
}

void Resampler::finish(std::vector<float>& output) {
    if (channels_ == 0 || passthrough() || input_frames_ == 0)
        return;
    if (!primed_)
        prime();
    // The frames after the last are the last frames mirrored, the last
    // included, as far as the input reaches, then zeros.
    const auto tail = static_cast<int64_t>(taps_ / 2);
    const auto frames = static_cast<int64_t>(history_.size() / channels_);
    const int64_t real_end = static_cast<int64_t>(input_frames_) - history_start_;
    std::vector<float> after(static_cast<std::size_t>(tail) * channels_, 0.0F);
    for (int64_t step = 0; step < tail && step < static_cast<int64_t>(input_frames_) &&
                           real_end - 1 - step >= 0 && real_end - 1 - step < frames;
         ++step)
        std::copy_n(
            history_.begin() + static_cast<std::ptrdiff_t>((real_end - 1 - step) * channels_),
            channels_,
            after.begin() + static_cast<std::ptrdiff_t>(step * channels_)
        );
    history_.insert(history_.end(), after.begin(), after.end());
    produce(output, true);
}

const float* Resampler::phase_taps(uint32_t phase, std::vector<float>& blended) const {
    if (exact_phases_)
        return table_.data() + static_cast<std::size_t>(phase) * taps_;
    const double position = static_cast<double>(phase) * phases_ / upsample_;
    const auto row = static_cast<uint32_t>(position);
    const auto weight = static_cast<float>(position - row);
    const float* lower = table_.data() + static_cast<std::size_t>(row) * taps_;
    const float* upper = lower + taps_;
    for (uint32_t tap = 0; tap < taps_; ++tap)
        blended[tap] = lower[tap] + (upper[tap] - lower[tap]) * weight;
    return blended.data();
}

void Resampler::produce(std::vector<float>& output, bool finishing) {
    const auto lead = static_cast<int64_t>(taps_ / 2) - 1;
    const uint64_t owed = (input_frames_ * upsample_ + downsample_ - 1) / downsample_; // rounded up
    const auto history_frames = static_cast<int64_t>(history_.size() / channels_);
    while (next_input_ + static_cast<int64_t>(taps_ / 2) < history_start_ + history_frames) {
        if (finishing && output_frames_ >= owed)
            break;
        const float* taps = phase_taps(next_phase_, scratch_taps_);
        const auto first = static_cast<std::size_t>(next_input_ - lead - history_start_);
        const float* frame = history_.data() + first * channels_;
        for (uint32_t channel = 0; channel < channels_; ++channel) {
            float sum = 0.0F;
            for (uint32_t tap = 0; tap < taps_; ++tap)
                sum += taps[tap] * frame[static_cast<std::size_t>(tap) * channels_ + channel];
            output.push_back(sum);
        }
        ++output_frames_;
        next_phase_ += downsample_;
        next_input_ += next_phase_ / upsample_;
        next_phase_ %= upsample_;
    }
    // Keep only the frames the next output's kernel still reaches.
    const int64_t keep_from = std::min(next_input_ - lead, history_start_ + history_frames);
    if (keep_from > history_start_) {
        const auto drop = static_cast<std::size_t>(keep_from - history_start_) * channels_;
        history_.erase(history_.begin(), history_.begin() + static_cast<std::ptrdiff_t>(drop));
        history_start_ = keep_from;
    }
}

bool PcmResampler::configure(uint32_t input_rate, uint32_t output_rate, uint32_t channels) {
    *this = PcmResampler{};
    if (input_rate < resampler_min_rate || input_rate > resampler_max_rate ||
        output_rate < resampler_min_rate || output_rate > resampler_max_rate || channels == 0 ||
        channels > resampler_max_channels)
        return false;
    input_rate_ = input_rate;
    output_rate_ = output_rate;
    channels_ = channels;
    const uint32_t divisor = std::gcd(input_rate, output_rate);
    upsample_ = output_rate / divisor;
    downsample_ = input_rate / divisor;
    if (passthrough()) {
        reset();
        return true;
    }
    const KernelShape shape = kernel_shape(input_rate, output_rate, pcm_design);
    taps_ = shape.taps;
    exact_phases_ = upsample_ <= resampler_max_exact_phases;
    phases_ = exact_phases_ ? upsample_ : resampler_max_exact_phases;
    table_.assign(static_cast<std::size_t>(phases_ + 1) * taps_, 0);
    std::vector<double> row;
    for (uint32_t phase = 0; phase <= phases_; ++phase) {
        const double sum = kernel_row(shape, static_cast<double>(phase) / phases_, row);
        int16_t* taps = table_.data() + static_cast<std::size_t>(phase) * taps_;
        // Each tap rounded on its own; the largest takes up what the
        // rounding left over, so that the phase sums to exactly one.
        int32_t total = 0;
        uint32_t largest = 0;
        for (uint32_t tap = 0; tap < taps_; ++tap) {
            taps[tap] = static_cast<int16_t>(std::lround(row[tap] / sum * pcm_unity_tap));
            total += taps[tap];
            if (taps[tap] > taps[largest])
                largest = tap;
        }
        taps[largest] = static_cast<int16_t>(taps[largest] + (pcm_unity_tap - total));
    }
    reset();
    return true;
}

void PcmResampler::reset() noexcept {
    // The input is silent before its first frame.
    const int64_t lead = passthrough() ? 0 : static_cast<int64_t>(taps_ / 2) - 1;
    history_.assign(static_cast<std::size_t>(lead) * channels_, 0);
    history_start_ = -lead;
    next_input_ = 0;
    next_phase_ = 0;
    input_frames_ = 0;
    output_frames_ = 0;
}

uint64_t PcmResampler::owed_frames() const noexcept {
    if (channels_ == 0 || passthrough())
        return 0;
    const uint64_t owed = (input_frames_ * upsample_ + downsample_ - 1) / downsample_; // rounded up
    return owed > output_frames_ ? owed - output_frames_ : 0;
}

void PcmResampler::process(std::span<const int16_t> input, std::vector<int32_t>& output) {
    if (channels_ == 0)
        return;
    const std::size_t frames = input.size() / channels_;
    const auto samples = static_cast<std::ptrdiff_t>(frames * channels_);
    input_frames_ += frames;
    if (passthrough()) {
        output.insert(output.end(), input.begin(), input.begin() + samples);
        output_frames_ += frames;
        return;
    }
    history_.insert(history_.end(), input.begin(), input.begin() + samples);
    produce(output, false);
}

void PcmResampler::finish(std::vector<int32_t>& output) {
    if (channels_ == 0 || passthrough() || input_frames_ == 0)
        return;
    // The input is silent after its last frame.
    history_.insert(history_.end(), static_cast<std::size_t>(taps_ / 2) * channels_, 0);
    produce(output, true);
}

void PcmResampler::produce(std::vector<int32_t>& output, bool finishing) {
    const auto lead = static_cast<int64_t>(taps_ / 2) - 1;
    const uint64_t owed = (input_frames_ * upsample_ + downsample_ - 1) / downsample_; // rounded up
    const auto history_frames = static_cast<int64_t>(history_.size() / channels_);
    while (next_input_ + static_cast<int64_t>(taps_ / 2) < history_start_ + history_frames) {
        if (finishing && output_frames_ >= owed)
            break;
        // The nearest tabled position; with exact phases, the position itself.
        const uint32_t row =
            exact_phases_ ? next_phase_
                          : static_cast<uint32_t>(
                                (static_cast<uint64_t>(next_phase_) * phases_ * 2 + upsample_) /
                                (static_cast<uint64_t>(upsample_) * 2)
                            );
        const int16_t* taps = table_.data() + static_cast<std::size_t>(row) * taps_;
        const auto first = static_cast<std::size_t>(next_input_ - lead - history_start_);
        const int16_t* frame = history_.data() + first * channels_;
        for (uint32_t channel = 0; channel < channels_; ++channel) {
            const int16_t* sample = frame + channel;
            int32_t sum = pcm_rounding;
            for (uint32_t tap = 0; tap < taps_; ++tap, sample += channels_)
                sum += static_cast<int32_t>(taps[tap]) * *sample;
            // An arithmetic shift rounds toward negative infinity; with the
            // half added first, the sum rounds to nearest.
            output.push_back(sum >> pcm_resampler_tap_bits);
        }
        ++output_frames_;
        next_phase_ += downsample_;
        next_input_ += next_phase_ / upsample_;
        next_phase_ %= upsample_;
    }
    // Keep only the frames the next output's kernel still reaches.
    const int64_t keep_from = std::min(next_input_ - lead, history_start_ + history_frames);
    if (keep_from > history_start_) {
        const auto drop = static_cast<std::size_t>(keep_from - history_start_) * channels_;
        history_.erase(history_.begin(), history_.begin() + static_cast<std::ptrdiff_t>(drop));
        history_start_ = keep_from;
    }
}

} // namespace oa::audio
