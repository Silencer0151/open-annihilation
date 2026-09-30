// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/audio/offline_mix.hpp"

#include "oa/audio/spatial_gain.hpp"
#include "oa/audio/wave_chunks.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace oa::audio::offline_mix {
namespace {

/// Full scale of a Q15 gain.
constexpr int32_t unity_gain = int32_t{1} << gain_bits;
/// Fraction bits of the decibel tables.
constexpr uint32_t table_bits = 30;
/// Fraction bits dropped when a Q30 gain becomes Q15.
constexpr uint32_t table_to_gain_shift = table_bits - gain_bits;
/// Hundredths of a decibel in one decibel.
constexpr int64_t hundredths_per_decibel = 100;
/// Decibels that divide an amplitude by ten.
constexpr int64_t decibels_per_decade = 20;
/// The factor of one decade of amplitude.
constexpr int64_t decade_factor = 10;

/// 10^(-h/20) for h = 0..19 whole decibels, Q30, rounded to nearest.
constexpr std::array<int64_t, decibels_per_decade> decibel_table{
    1073741824, 956973408, 852903448, 760150998, 677485290, 603809400, 538145694,
    479622855,  427464319, 380977976, 339546978, 302621563, 269711752, 240380852,
    214239660,  190941298, 170176611, 151670064, 135176087, 120475814,
};

/// 10^(-u/2000) for u = 0..99 hundredths of a decibel, Q30, rounded to nearest.
constexpr std::array<int64_t, hundredths_per_decibel> hundredth_table{
    1073741824, 1072506344, 1071272286, 1070039648, 1068808428, 1067578625, 1066350237, 1065123263,
    1063897700, 1062673547, 1061450803, 1060229466, 1059009534, 1057791006, 1056573880, 1055358154,
    1054143827, 1052930897, 1051719364, 1050509224, 1049300476, 1048093119, 1046887152, 1045682572,
    1044479378, 1043277569, 1042077142, 1040878097, 1039680432, 1038484144, 1037289233, 1036095697,
    1034903534, 1033712743, 1032523322, 1031335269, 1030148584, 1028963264, 1027779308, 1026596714,
    1025415481, 1024235607, 1023057091, 1021879931, 1020704125, 1019529672, 1018356571, 1017184819,
    1016014416, 1014845359, 1013677647, 1012511279, 1011346253, 1010182568, 1009020222, 1007859213,
    1006699539, 1005541201, 1004384195, 1003228520, 1002074175, 1000921159, 999769469,  998619104,
    997470063,  996322344,  995175945,  994030866,  992887104,  991744658,  990603527,  989463709,
    988325202,  987188005,  986052117,  984917536,  983784260,  982652289,  981521619,  980392251,
    979264182,  978137411,  977011937,  975887758,  974764872,  973643278,  972522975,  971403961,
    970286234,  969169794,  968054638,  966940765,  965828174,  964716863,  963606831,  962498076,
    961390597,  960284392,  959179459,  958075799,
};

/// The lowest sample rate a clip may have, in Hz.
constexpr uint32_t min_clip_rate = 4000;
/// The highest sample rate a clip may have, in Hz.
constexpr uint32_t max_clip_rate = 96000;
/// The most samples a decoded clip may hold, at sample_rate.
constexpr uint64_t max_clip_samples = max_clip_file_bytes;
/// The value an unsigned 8-bit sample holds at silence.
constexpr int32_t unsigned_8bit_silence = 128;
/// The factor that widens an 8-bit sample to 16 bits.
constexpr int32_t widen_8bit = 256;
/// Bits in a narrow sample.
constexpr uint16_t narrow_bits = 8;
/// Bits in a wide sample.
constexpr uint16_t wide_bits = 16;
/// Channels of a stereo file.
constexpr uint16_t stereo_channels = 2;
/// Bytes per 16-bit sample of the WAVE file wave_header describes.
constexpr uint32_t output_sample_bytes = 2;
/// Bytes per sample frame of that file.
constexpr uint32_t output_frame_bytes = channels * output_sample_bytes;
/// Bytes of the RIFF chunk counted before the data: the header less the RIFF tag and size.
constexpr uint32_t riff_bytes_before_data = wave_header_bytes - 8;
/// Bytes of the "fmt " payload wave_header writes.
constexpr uint32_t format_payload_bytes = 16;
/// The format tag of integer PCM.
constexpr uint16_t pcm_format_tag = 1;

/// Returns a divided by a positive b, rounded toward negative infinity.
int64_t floor_divide(int64_t a, int64_t b) noexcept {
    const int64_t quotient = a / b;
    return (a % b != 0 && a < 0) ? quotient - 1 : quotient;
}

/// Reads one mono sample of a frame, widened to 16 bits and averaged across channels.
int32_t read_frame(const uint8_t* frame, uint16_t bits, uint16_t frame_channels) noexcept {
    int32_t sum{};
    for (uint16_t channel = 0; channel < frame_channels; ++channel) {
        if (bits == narrow_bits) {
            sum += (static_cast<int32_t>(frame[channel]) - unsigned_8bit_silence) * widen_8bit;
        } else {
            const uint8_t* sample = frame + channel * 2;
            sum += static_cast<int16_t>(static_cast<uint16_t>(sample[0] | sample[1] << 8));
        }
    }
    // An arithmetic shift rounds toward negative infinity.
    return frame_channels == stereo_channels ? sum >> 1 : sum;
}

/// Returns a resource path with backslashes turned into forward slashes.
std::string archive_path(std::string_view resource) {
    std::string path{resource};
    std::replace(path.begin(), path.end(), '\\', '/');
    return path;
}

/// Returns the cache key of a resource path: forward slashes, ASCII lower case.
std::string clip_key(std::string_view path) {
    std::string key{path};
    for (char& c : key)
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    return key;
}

/// Appends a 16-bit little-endian value.
void put16(std::array<uint8_t, wave_header_bytes>& header, size_t& at, uint32_t value) noexcept {
    header[at++] = static_cast<uint8_t>(value & 0xff);
    header[at++] = static_cast<uint8_t>(value >> 8 & 0xff);
}

/// Appends a 32-bit little-endian value.
void put32(std::array<uint8_t, wave_header_bytes>& header, size_t& at, uint32_t value) noexcept {
    put16(header, at, value & 0xffff);
    put16(header, at, value >> 16);
}

/// Appends a four-character tag.
void put_tag(std::array<uint8_t, wave_header_bytes>& header, size_t& at, const char* tag) noexcept {
    for (size_t i = 0; i < 4; ++i)
        header[at++] = static_cast<uint8_t>(tag[i]);
}

} // namespace

bool decode_clip(std::span<const uint8_t> file, Clip& clip, std::string& error) {
    clip = Clip{};
    if (file.size() > max_clip_file_bytes) {
        error = "the sound file is larger than the offline mix reads";
        return false;
    }
    WaveCursor cursor = wave_cursor(file.data(), static_cast<uint32_t>(file.size()));
    WaveLayout layout{};
    if (!describe_wave(cursor, layout)) {
        error = "the sound file has no usable format or no sample data";
        return false;
    }
    const PcmFormat format = layout.format;
    if (format.bits != narrow_bits && format.bits != wide_bits) {
        error = "the sound file's samples are neither 8 nor 16 bits";
        return false;
    }
    if (format.channels != 1 && format.channels != stereo_channels) {
        error = "the sound file is neither mono nor stereo";
        return false;
    }
    if (format.sample_rate < min_clip_rate || format.sample_rate > max_clip_rate) {
        error = "the sound file's sample rate is outside 4000 to 96000 Hz";
        return false;
    }
    const uint64_t span_end = uint64_t{layout.data_offset} + layout.data_bytes;
    if (layout.data_offset > file.size() || span_end > file.size()) {
        error = "the sound file's sample data runs past its end";
        return false;
    }
    const uint32_t frame_bytes = uint32_t{format.bits} / narrow_bits * format.channels;
    const uint64_t frames = layout.data_bytes / frame_bytes;
    if (frames == 0) {
        error = "the sound file holds no whole sample frame";
        return false;
    }
    const uint64_t rate = format.sample_rate;
    // ceil(frames * sample_rate / rate): every output sample whose input
    // position falls inside the clip.
    const uint64_t samples = (frames * sample_rate + rate - 1) / rate;
    if (samples > max_clip_samples) {
        error = "the sound file is longer than the offline mix plays";
        return false;
    }

    std::vector<int32_t> mono(frames);
    const uint8_t* data = file.data() + layout.data_offset;
    for (uint64_t frame = 0; frame < frames; ++frame)
        mono[frame] = read_frame(data + frame * frame_bytes, format.bits, format.channels);

    clip.samples.resize(samples);
    uint64_t whole{};
    int64_t remainder{};
    constexpr auto rate_out = static_cast<int64_t>(sample_rate);
    for (uint64_t n = 0; n < samples; ++n) {
        const int64_t here = mono[whole];
        const int64_t next = whole + 1 < frames ? mono[whole + 1] : 0;
        clip.samples[n] = static_cast<int16_t>(
            floor_divide(here * (rate_out - remainder) + next * remainder, rate_out)
        );
        remainder += static_cast<int64_t>(rate);
        while (remainder >= rate_out) {
            remainder -= rate_out;
            ++whole;
        }
    }
    error.clear();
    return true;
}

int32_t centibel_gain(int32_t volume) noexcept {
    const int64_t below = int64_t{full_scale_volume} - volume;
    if (below <= 0)
        return unity_gain;
    const int64_t decibels = below / hundredths_per_decibel;
    const int64_t hundredths = below % hundredths_per_decibel;
    int64_t gain = decibel_table[static_cast<size_t>(decibels % decibels_per_decade)] *
                       hundredth_table[static_cast<size_t>(hundredths)] >>
                   table_bits;
    for (int64_t decade = decibels / decibels_per_decade; decade > 0 && gain > 0; --decade)
        gain /= decade_factor;
    return static_cast<int32_t>(std::clamp<int64_t>(gain >> table_to_gain_shift, 0, unity_gain));
}

VoiceGains voice_gains(int32_t volume, const Spatial& spatial) noexcept {
    const StereoGain level = spatial_stereo_gain(spatial);
    const auto to_q15 = [](float side) noexcept -> int64_t {
        // Scaling by a power of two is exact; NaN and negatives give 0.
        const float scaled = side * static_cast<float>(unity_gain);
        if (!(scaled > 0.0F))
            return 0;
        if (scaled >= static_cast<float>(unity_gain))
            return unity_gain;
        return static_cast<int64_t>(std::floor(scaled));
    };
    const int64_t gain = centibel_gain(volume);
    return VoiceGains{
        static_cast<int32_t>(gain * to_q15(level.left) >> gain_bits),
        static_cast<int32_t>(gain * to_q15(level.right) >> gain_bits),
    };
}

OfflineMix::OfflineMix(ClipFileHooks files, int32_t master_gain)
    : files_{files}, master_gain_{std::clamp(master_gain, 0, unity_gain)} {
}

void OfflineMix::start(const ClipStart& start) {
    PendingStart pending{start, std::max(start.start_sample, position_)};
    const auto at = std::upper_bound(
        pending_.begin(),
        pending_.end(),
        pending.sample,
        [](uint64_t sample, const PendingStart& queued) { return sample < queued.sample; }
    );
    pending_.insert(at, std::move(pending));
}

void OfflineMix::render(std::span<int16_t> samples) {
    const size_t frames = samples.size() / channels;
    for (size_t i = frames * channels; i < samples.size(); ++i)
        samples[i] = 0;
    size_t done{};
    while (done < frames) {
        while (!pending_.empty() && pending_.front().sample <= position_) {
            apply(pending_.front().start);
            pending_.erase(pending_.begin());
        }
        uint64_t segment = frames - done;
        if (!pending_.empty())
            segment = std::min(segment, pending_.front().sample - position_);
        mix(samples.subspan(done * channels, static_cast<size_t>(segment) * channels));
        done += static_cast<size_t>(segment);
        position_ += segment;
    }
    collect_finished();
}

uint64_t OfflineMix::position() const noexcept {
    return position_;
}

int32_t OfflineMix::voices_playing() const noexcept {
    return static_cast<int32_t>(voices_.size());
}

const std::vector<std::string>& OfflineMix::errors() const noexcept {
    return errors_;
}

void OfflineMix::collect_finished() noexcept {
    std::erase_if(voices_, [](const Voice& voice) { return !voice.buffer->playing; });
}

bool OfflineMix::evict_oldest_voice() noexcept {
    if (voices_.empty())
        return false;
    const auto oldest =
        std::min_element(voices_.begin(), voices_.end(), [](const Voice& a, const Voice& b) {
            return a.serial < b.serial;
        });
    oldest->buffer->playing = false;
    voices_.erase(oldest);
    return true;
}

OfflineMix::CachedClip* OfflineMix::find_clip(std::string_view resource) {
    const std::string path = archive_path(resource);
    const std::string key = clip_key(path);
    if (const auto found = clips_.find(key); found != clips_.end())
        return &found->second;
    if (failed_.contains(key))
        return nullptr;

    std::vector<uint8_t> bytes{};
    Clip clip{};
    std::string error{};
    const bool read = files_.load != nullptr && files_.load(files_.context, path.c_str(), &bytes);
    if (!read)
        error = "no such sound file";
    if (!read || !decode_clip(bytes, clip, error)) {
        failed_.insert(key);
        errors_.push_back(path + ": " + error);
        return nullptr;
    }

    if (clips_.size() >= max_cached_clips) {
        // The least recently started clip that no voice plays.
        auto evict = clips_.end();
        for (auto it = clips_.begin(); it != clips_.end(); ++it) {
            const bool busy = std::any_of(
                it->second.buffers.begin(), it->second.buffers.end(), [](const Buffer& buffer) {
                    return buffer.playing;
                }
            );
            if (!busy &&
                (evict == clips_.end() || it->second.last_started < evict->second.last_started))
                evict = it;
        }
        if (evict != clips_.end())
            clips_.erase(evict);
    }
    CachedClip& cached = clips_[key];
    cached.clip = std::move(clip);
    cached.buffers[0].present = true;
    return &cached;
}

void OfflineMix::apply(const ClipStart& start) {
    collect_finished();
    CachedClip* cached = find_clip(start.resource);
    if (cached == nullptr)
        return;
    cached->last_started = ++starts_;

    while (voice_limit <= static_cast<int32_t>(voices_.size())) {
        if (!evict_oldest_voice())
            return;
    }

    // An idle buffer of the clip, a new one while fewer than
    // buffers_per_clip exist (the last empty index first), or else the one
    // that has played furthest (the lowest index among equals).
    Buffer* chosen{};
    uint64_t furthest{};
    size_t furthest_index{};
    size_t empty_index{};
    for (size_t k = 0; k < buffers_per_clip; ++k) {
        Buffer& buffer = cached->buffers[k];
        if (!buffer.present) {
            empty_index = k;
            continue;
        }
        if (!buffer.playing) {
            chosen = &buffer;
            break;
        }
        if (furthest < buffer.played) {
            furthest = buffer.played;
            furthest_index = k;
        }
    }
    if (chosen == nullptr) {
        if (empty_index < 1) {
            chosen = &cached->buffers[furthest_index];
        } else {
            chosen = &cached->buffers[empty_index];
            chosen->present = true;
        }
    }
    chosen->clip = &cached->clip;
    chosen->played = 0;
    chosen->playing = true;
    chosen->gains = voice_gains(start.volume, start.spatial);
    // A restarted buffer keeps the voice it had and gains another, as the
    // game's mixer counts it: it holds two of the voice_limit voices until
    // it stops.
    voices_.push_back(Voice{chosen, ++serial_});
}

void OfflineMix::mix(std::span<int16_t> out) {
    const size_t frames = out.size() / channels;
    if (frames == 0)
        return;
    accumulator_.assign(out.size(), 0);
    for (size_t v = 0; v < voices_.size(); ++v) {
        Buffer* buffer = voices_[v].buffer;
        const bool heard_earlier = std::any_of(
            voices_.begin(),
            voices_.begin() + static_cast<std::ptrdiff_t>(v),
            [buffer](const Voice& voice) { return voice.buffer == buffer; }
        );
        if (heard_earlier || !buffer->playing)
            continue;
        const std::vector<int16_t>& clip = buffer->clip->samples;
        const uint64_t left_in_clip = clip.size() - buffer->played;
        const auto count = static_cast<size_t>(std::min<uint64_t>(frames, left_in_clip));
        const int16_t* source = clip.data() + buffer->played;
        const int32_t left = buffer->gains.left;
        const int32_t right = buffer->gains.right;
        for (size_t k = 0; k < count; ++k) {
            const int32_t sample = source[k];
            accumulator_[k * channels] += sample * left >> gain_bits;
            accumulator_[k * channels + 1] += sample * right >> gain_bits;
        }
        buffer->played += count;
        if (buffer->played >= clip.size())
            buffer->playing = false;
    }
    constexpr int64_t low = std::numeric_limits<int16_t>::min();
    constexpr int64_t high = std::numeric_limits<int16_t>::max();
    for (size_t i = 0; i < out.size(); ++i) {
        const int64_t scaled = int64_t{accumulator_[i]} * master_gain_ >> gain_bits;
        out[i] = static_cast<int16_t>(std::clamp(scaled, low, high));
    }
}

std::array<uint8_t, wave_header_bytes> wave_header(uint64_t sample_frames) noexcept {
    constexpr uint64_t all_ones = std::numeric_limits<uint32_t>::max();
    const uint64_t data_bytes = sample_frames > all_ones / output_frame_bytes
                                    ? all_ones
                                    : sample_frames * output_frame_bytes;
    const uint64_t riff_bytes = std::min(data_bytes + riff_bytes_before_data, all_ones);
    std::array<uint8_t, wave_header_bytes> header{};
    size_t at{};
    put_tag(header, at, "RIFF");
    put32(header, at, static_cast<uint32_t>(riff_bytes));
    put_tag(header, at, "WAVE");
    put_tag(header, at, "fmt ");
    put32(header, at, format_payload_bytes);
    put16(header, at, pcm_format_tag);
    put16(header, at, channels);
    put32(header, at, sample_rate);
    put32(header, at, sample_rate * output_frame_bytes);
    put16(header, at, output_frame_bytes);
    put16(header, at, output_sample_bytes * narrow_bits);
    put_tag(header, at, "data");
    put32(header, at, static_cast<uint32_t>(data_bytes));
    return header;
}

} // namespace oa::audio::offline_mix
