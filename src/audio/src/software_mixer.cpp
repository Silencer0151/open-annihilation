// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/audio/software_mixer.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace oa::audio {
namespace {

constexpr float scale_8 = 1.0F / 128.0F;
constexpr float scale_16 = 1.0F / 32768.0F;
constexpr float scale_32 = 1.0F / 2147483648.0F;
constexpr int32_t unsigned_8_zero = 0x80;
constexpr float full_scale_16 = 32767.0F;
// Converted frames kept before the consumed ones are dropped from the front.
constexpr std::size_t compact_after_samples = 1 << 15;

// Holds the mixer lock for a scope.
class Hold {
  public:

    explicit Hold(OutputStream& stream) : stream_(stream) { stream_.lock(); }

    ~Hold() { stream_.unlock(); }

    Hold(const Hold&) = delete;
    Hold& operator=(const Hold&) = delete;

  private:

    OutputStream& stream_;
};

float read_sample(const uint8_t* bytes, SampleFormat sample) {
    switch (sample) {
    case SampleFormat::u8:
        return static_cast<float>(static_cast<int32_t>(bytes[0]) - unsigned_8_zero) * scale_8;
    case SampleFormat::s16: {
        const auto value = static_cast<int16_t>(bytes[0] | (bytes[1] << 8));
        return static_cast<float>(value) * scale_16;
    }
    case SampleFormat::s32: {
        const auto value = static_cast<int32_t>(
            static_cast<uint32_t>(bytes[0]) | (static_cast<uint32_t>(bytes[1]) << 8) |
            (static_cast<uint32_t>(bytes[2]) << 16) | (static_cast<uint32_t>(bytes[3]) << 24)
        );
        return static_cast<float>(value) * scale_32;
    }
    case SampleFormat::f32: {
        float value = 0.0F;
        std::memcpy(&value, bytes, sizeof(value));
        return value;
    }
    }
    return 0.0F;
}

} // namespace

/// A stream of a SoftwareMixer: its queued bytes, their conversion and its state.
class MixerStream final : public OutputStream {
  public:

    MixerStream(SoftwareMixer& mixer, const StreamFormat& format, StreamFeed feed, void* context)
        : mixer_(mixer), format_(format), feed_(feed), context_(context),
          frame_bytes_(frame_bytes(format)) {}

    /// Configures the rate conversion.
    ///
    /// @return false when the resampler cannot convert the stream's rate
    [[nodiscard]] bool configure() {
        return resampler_.configure(format_.rate, mixer_output_rate, mixer_output_channels);
    }

    ~MixerStream() override {
        mixer_.lock();
        std::erase(mixer_.streams_, this);
        mixer_.unlock();
    }

    bool put(const void* bytes, int32_t count) override {
        if (count < 0)
            return false;
        Hold hold(*this);
        if (ended_) {
            resampler_.reset();
            ended_ = false;
        }
        flushed_ = false;
        const auto* first = static_cast<const uint8_t*>(bytes);
        input_.insert(input_.end(), first, first + count);
        return true;
    }

    bool flush() override {
        Hold hold(*this);
        flushed_ = true;
        return true;
    }

    int32_t queued_bytes() override {
        Hold hold(*this);
        return static_cast<int32_t>(input_.size() - input_read_);
    }

    int32_t available_bytes() override {
        Hold hold(*this);
        return static_cast<int32_t>((converted_.size() - converted_read_) * sizeof(float));
    }

    void clear() override {
        Hold hold(*this);
        input_.clear();
        input_read_ = 0;
        converted_.clear();
        converted_read_ = 0;
        resampler_.reset();
        flushed_ = false;
        ended_ = false;
    }

    bool set_gain(float gain) override {
        Hold hold(*this);
        gain_ = gain;
        return true;
    }

    bool pause() override {
        Hold hold(*this);
        paused_ = true;
        return true;
    }

    bool resume() override {
        Hold hold(*this);
        paused_ = false;
        return true;
    }

    void lock() override { mixer_.lock(); }

    void unlock() override { mixer_.unlock(); }

    /// Adds the stream's next frames to a mix; the caller holds the lock.
    ///
    /// @param[in,out] sum the mix, frames * mixer_output_channels samples
    /// @param frames frames wanted
    void add_to(float* sum, uint32_t frames) {
        if (paused_)
            return;
        uint32_t feed_calls = 0;
        while (converted_frames() < frames) {
            const std::size_t whole = (input_.size() - input_read_) / frame_bytes_;
            if (whole > 0) {
                convert(static_cast<uint32_t>(std::min<std::size_t>(whole, mixer_convert_frames)));
                continue;
            }
            if (feed_ != nullptr && feed_calls < mixer_feed_calls) {
                ++feed_calls;
                // Enough input for the frames still missing, and for the
                // frames the rate conversion holds back.
                const uint64_t missing = frames - converted_frames();
                const uint64_t input_frames =
                    (missing * format_.rate + mixer_output_rate - 1) / mixer_output_rate +
                    resampler_zero_crossings * 2;
                const std::size_t before = input_.size() - input_read_;
                feed_(context_, *this, static_cast<int32_t>(input_frames * frame_bytes_));
                if (input_.size() - input_read_ > before)
                    continue;
            }
            if (flushed_ && !ended_) {
                resampler_.finish(converted_);
                ended_ = true;
                continue;
            }
            break;
        }
        const std::size_t take = std::min<std::size_t>(converted_frames(), frames);
        const float* from = converted_.data() + converted_read_;
        for (std::size_t i = 0; i < take * mixer_output_channels; ++i)
            sum[i] += from[i] * gain_;
        converted_read_ += take * mixer_output_channels;
        if (converted_read_ == converted_.size()) {
            converted_.clear();
            converted_read_ = 0;
        } else if (converted_read_ > compact_after_samples) {
            converted_.erase(
                converted_.begin(),
                converted_.begin() + static_cast<std::ptrdiff_t>(converted_read_)
            );
            converted_read_ = 0;
        }
    }

  private:

    [[nodiscard]] std::size_t converted_frames() const {
        return (converted_.size() - converted_read_) / mixer_output_channels;
    }

    // Converts queued input frames to stereo float and on to the output rate.
    void convert(uint32_t frames) {
        const uint32_t bytes_per_sample = sample_bytes(format_.sample);
        stereo_.resize(static_cast<std::size_t>(frames) * mixer_output_channels);
        const uint8_t* frame = input_.data() + input_read_;
        for (uint32_t i = 0; i < frames; ++i, frame += frame_bytes_) {
            const float left = read_sample(frame, format_.sample);
            const float right = format_.channels == 1
                                    ? left
                                    : read_sample(frame + bytes_per_sample, format_.sample);
            stereo_[2 * static_cast<std::size_t>(i)] = left;
            stereo_[2 * static_cast<std::size_t>(i) + 1] = right;
        }
        input_read_ += static_cast<std::size_t>(frames) * frame_bytes_;
        if (input_read_ == input_.size()) {
            input_.clear();
            input_read_ = 0;
        }
        resampler_.process(stereo_, converted_);
    }

    SoftwareMixer& mixer_;
    StreamFormat format_{};
    StreamFeed feed_{};
    void* context_{};
    uint32_t frame_bytes_{};
    Resampler resampler_;
    std::vector<uint8_t> input_; ///< bytes put, from input_read_ on not yet converted
    std::size_t input_read_{};
    std::vector<float> stereo_; ///< one step's frames before rate conversion
    std::vector<float>
        converted_; ///< stereo frames at the output rate, from converted_read_ on not yet mixed
    std::size_t converted_read_{};
    float gain_{1.0F};
    bool paused_{true};
    bool flushed_{}; ///< the queued input is complete
    bool ended_{};   ///< the resampler has given the frames after the flushed input
};

SoftwareMixer::SoftwareMixer(MixerLock lock) : lock_(lock) {
}

SoftwareMixer::~SoftwareMixer() = default;

std::unique_ptr<OutputStream> SoftwareMixer::open_stream(
    const StreamFormat& format, StreamFeed feed, void* context, std::string& error
) {
    if (format.channels == 0 || format.channels > max_stream_channels) {
        error = "unsupported channel count " + std::to_string(format.channels);
        return nullptr;
    }
    auto stream = std::make_unique<MixerStream>(*this, format, feed, context);
    if (!stream->configure()) {
        error = "unsupported sample rate " + std::to_string(format.rate);
        return nullptr;
    }
    lock();
    streams_.push_back(stream.get());
    unlock();
    return stream;
}

void SoftwareMixer::mix(int16_t* out, uint32_t frames) {
    lock();
    sum_.assign(static_cast<std::size_t>(frames) * mixer_output_channels, 0.0F);
    // A feed may open or close streams; mix the ones open when the call began.
    const std::vector<MixerStream*> streams = streams_;
    for (MixerStream* stream : streams)
        if (std::find(streams_.begin(), streams_.end(), stream) != streams_.end())
            stream->add_to(sum_.data(), frames);
    unlock();
    for (std::size_t i = 0; i < sum_.size(); ++i)
        out[i] =
            static_cast<int16_t>(std::lround(std::clamp(sum_[i], -1.0F, 1.0F) * full_scale_16));
}

std::size_t SoftwareMixer::stream_count() {
    lock();
    const std::size_t count = streams_.size();
    unlock();
    return count;
}

void SoftwareMixer::lock() const {
    if (lock_.lock != nullptr)
        lock_.lock(lock_.context);
}

void SoftwareMixer::unlock() const {
    if (lock_.unlock != nullptr)
        lock_.unlock(lock_.context);
}

} // namespace oa::audio
