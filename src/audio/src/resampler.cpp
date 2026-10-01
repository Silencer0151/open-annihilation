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

// Evaluates the windowed kernel at a distance from its centre, in input frames.
double kernel(double distance, double band, double half_width, double window_scale) {
    const double ratio = distance / half_width;
    if (ratio <= -1.0 || ratio >= 1.0)
        return 0.0;
    const double x = std::numbers::pi * band * distance;
    const double sinc = x == 0.0 ? 1.0 : std::sin(x) / x;
    return sinc * bessel_i0(resampler_kaiser_beta * std::sqrt(1.0 - ratio * ratio)) / window_scale;
}

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
    // Downsampling narrows the band and widens the kernel by the same ratio.
    const double scale = std::min(1.0, static_cast<double>(output_rate) / input_rate);
    const double band = scale * resampler_cutoff;
    const double half_width = resampler_zero_crossings / scale;
    taps_ = 2 * static_cast<uint32_t>(std::ceil(half_width));
    exact_phases_ = upsample_ <= resampler_max_exact_phases;
    phases_ = exact_phases_ ? upsample_ : resampler_max_exact_phases;
    const uint32_t rows = exact_phases_ ? phases_ : phases_ + 1;
    const double window_scale = bessel_i0(resampler_kaiser_beta);
    const int32_t first_tap = 1 - static_cast<int32_t>(taps_ / 2);
    table_.assign(static_cast<std::size_t>(rows) * taps_, 0.0F);
    std::vector<double> row(taps_);
    for (uint32_t phase = 0; phase < rows; ++phase) {
        const double offset = static_cast<double>(phase) / phases_;
        double sum = 0.0;
        for (uint32_t tap = 0; tap < taps_; ++tap) {
            row[tap] = kernel(
                static_cast<double>(first_tap + static_cast<int32_t>(tap)) - offset,
                band,
                half_width,
                window_scale
            );
            sum += row[tap];
        }
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

} // namespace oa::audio
