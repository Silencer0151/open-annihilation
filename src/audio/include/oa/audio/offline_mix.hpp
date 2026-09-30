// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// An offline mix of the game's sound effects: clips started at exact sample
// positions, mixed on demand into 16-bit stereo at 48 kHz with no sound
// device and no clock. It keeps the game's voice policy (Mixer's
// attach_sample and mixer_evict_oldest_voice): at most voice_limit voices;
// when a start finds them all taken, the oldest voice that does not loop
// stops; a clip plays on at most buffers_per_clip voices at once, and a
// fifth start of it restarts the one of its voices that has played
// furthest.
//
// Every step after decoding is integer arithmetic, so the same starts give
// the same samples on every platform, and a mix rendered in several calls
// equals one rendered in one:
// - a clip is decoded once (wave_chunks.hpp's layouts: 8-bit unsigned or
//   16-bit signed, mono or stereo, any rate), 8-bit samples widened as
//   (x - 128) * 256 and stereo averaged, rounding toward negative infinity;
// - it is resampled to 48 kHz by exact rational phase: output sample n
//   reads input position n * rate / 48000, whole part i and remainder r,
//   and is (s[i] * (48000 - r) + s[i + 1] * r) / 48000, rounding toward
//   negative infinity, s past the end being 0;
// - a voice's left and right gains are Q15 numbers: its volume through
//   centibel_gain, times spatial_stereo_gain's levels for its placement
//   rounded down to Q15;
// - each output sample is the sum over voices of (clip sample * gain) >> 15,
//   times master_gain >> 15, saturated to 16 bits.
#pragma once

#include "oa/audio/game_audio.hpp"
#include "oa/audio/mixer.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace oa::audio::offline_mix {

/// Samples a second, per channel.
inline constexpr uint32_t sample_rate = 48000;
/// Channels of the mix, left then right in each sample frame.
inline constexpr uint32_t channels = 2;
/// The most voices that play at once, as in the game.
inline constexpr int32_t voice_limit = default_voice_limit;
/// The most voices one clip plays on at once, as in the game.
inline constexpr size_t buffers_per_clip = sample_buffers;
/// The largest sound file decoded, in bytes.
inline constexpr size_t max_clip_file_bytes = size_t{16} << 20;
/// The most clips kept decoded at once.
inline constexpr size_t max_cached_clips = 1024;
/// The fraction bits of the mix's gains: 1 << gain_bits is full scale.
inline constexpr uint32_t gain_bits = 15;
/// The volume, in hundredths of a decibel, that plays at full scale: the
/// device's own full volume, so a sound the game plays near the camera
/// (volume_near) plays 5.85 dB below full scale, as it does in the game.
inline constexpr int32_t full_scale_volume = 0;
/// The left shift that turns the game's effects volume setting into a device
/// volume.
inline constexpr uint32_t effects_volume_shift = 10;
/// The mix's master gain at the game's default effects volume: that volume
/// shifted left by effects_volume_shift, over the device's full volume, as a
/// Q15 number.
inline constexpr int32_t default_master_gain = static_cast<int32_t>(
    (int64_t{game_audio::default_fx_volume} << effects_volume_shift) * (int64_t{1} << gain_bits) /
    int64_t{game_audio::full_wave_out_volume}
);
/// Bytes of the RIFF header wave_header writes.
inline constexpr size_t wave_header_bytes = 44;

/// A decoded clip: mono 16-bit samples at sample_rate.
struct Clip {
    std::vector<int16_t> samples{};
};

/// Decodes a sound file into a clip at sample_rate.
///
/// @param file the sound file, at most max_clip_file_bytes
/// @param[out] clip the clip; left empty on failure
/// @param[out] error why the file could not be decoded
/// @return true when the file was decoded
[[nodiscard]] bool decode_clip(std::span<const uint8_t> file, Clip& clip, std::string& error);

/// Returns the linear gain of a volume relative to full_scale_volume.
///
/// 10^((volume - full_scale_volume) / 2000) as a Q15 number, from two
/// pinned tables (whole decibels and hundredths) rather than a power
/// function; at least 0, at most 1 << gain_bits.
///
/// @param volume hundredths of a decibel
/// @return the gain, Q15
[[nodiscard]] int32_t centibel_gain(int32_t volume) noexcept;

/// A voice's gains, Q15.
struct VoiceGains {
    int32_t left{};
    int32_t right{};
};

/// Returns the gains a clip plays at.
///
/// @param volume hundredths of a decibel
/// @param spatial its placement, relative to the listener, as voice_spatial
///        returns it; 3D processing off plays at the volume on both sides
/// @return centibel_gain(volume) times each of spatial_stereo_gain's levels,
///         each level rounded down to Q15 first
[[nodiscard]] VoiceGains voice_gains(int32_t volume, const Spatial& spatial) noexcept;

/// Where the mix reads sound files: the game's archives, in their search order.
struct ClipFileHooks {
    void* context{};
    /// Reads a sound file.
    ///
    /// @param context ClipFileHooks::context
    /// @param resource the file's path in the archives, such as sounds/xplomed2.wav
    /// @param[out] bytes the file
    /// @return false when there is no such file; null never finds one
    bool (*load)(void* context, const char* resource, std::vector<uint8_t>* bytes){};
};

/// A clip to start.
struct ClipStart {
    std::string resource{};  ///< the sound file's path in the archives
    int32_t volume{};        ///< hundredths of a decibel
    Spatial spatial{};       ///< placement relative to the listener
    uint64_t start_sample{}; ///< the sample frame it starts on
};

/// A mix in progress.
class OfflineMix {
  public:

    /// Starts an empty mix at sample frame 0.
    ///
    /// @param files where clips are read; its context must outlive the mix
    /// @param master_gain the master gain, Q15, 0 to 1 << gain_bits
    explicit OfflineMix(ClipFileHooks files, int32_t master_gain = default_master_gain);

    /// Queues a clip.
    ///
    /// Starts apply in the order of their sample frames, and starts on the
    /// same frame in the order they were queued; a start before position()
    /// applies at position(). A clip that cannot be read or decoded plays
    /// nothing, and its message is kept for errors().
    ///
    /// @param start the clip, its volume, placement and sample frame
    void start(const ClipStart& start);

    /// Mixes the next sample frames.
    ///
    /// @param[out] samples interleaved left and right samples; its size / channels
    ///        sample frames are mixed, from position() on
    void render(std::span<int16_t> samples);

    /// Returns the next sample frame render() mixes.
    ///
    /// @return the sample frame, counted from 0
    [[nodiscard]] uint64_t position() const noexcept;

    /// Returns how many voices are playing.
    ///
    /// @return the voices, at most voice_limit
    [[nodiscard]] int32_t voices_playing() const noexcept;

    /// Returns the messages of the clips that could not be read or decoded.
    ///
    /// @return one message per clip, in the order they were first started
    [[nodiscard]] const std::vector<std::string>& errors() const noexcept;

  private:

    /// One of a clip's buffers, as the game's mixer keeps up to
    /// buffers_per_clip of them per sound: it plays one start at a time.
    struct Buffer {
        const Clip* clip{};
        bool present{};    ///< made: the first on decoding, the others as starts need them
        bool playing{};    ///< false once it has played out or was stopped
        uint64_t played{}; ///< samples of the clip already mixed
        VoiceGains gains{};
    };

    /// A decoded clip and its buffers.
    struct CachedClip {
        Clip clip{};
        std::array<Buffer, buffers_per_clip> buffers{};
        uint64_t last_started{}; ///< the count of starts applied when it last started
    };

    /// One of the voice_limit voices; a restarted buffer holds two.
    struct Voice {
        Buffer* buffer{};
        int32_t serial{}; ///< start order, for the oldest-voice rule
    };

    /// A queued start and the sample frame it applies at.
    struct PendingStart {
        ClipStart start{};
        uint64_t sample{}; ///< its start_sample, or position() when it was queued if later
    };

    /// Frees the voices whose buffer no longer plays.
    void collect_finished() noexcept;

    /// Stops the buffer of the oldest voice and frees that voice.
    ///
    /// @return false when no voice plays
    bool evict_oldest_voice() noexcept;

    /// Returns a clip's cache entry, reading and decoding it on first use.
    ///
    /// A full cache first drops the least recently started clip that no
    /// voice plays. A clip that cannot be read or decoded is remembered, and
    /// its message kept, once.
    ///
    /// @param resource the sound file's path in the archives
    /// @return the entry; null when the clip cannot be played
    CachedClip* find_clip(std::string_view resource);

    /// Starts a clip now, under the voice policy.
    ///
    /// @param start the clip, its volume and placement
    void apply(const ClipStart& start);

    /// Mixes the playing buffers into sample frames that no start falls within.
    ///
    /// @param[out] out interleaved left and right samples
    void mix(std::span<int16_t> out);

    ClipFileHooks files_{};
    int32_t master_gain_{};
    uint64_t position_{};
    int32_t serial_{};
    uint64_t starts_{}; ///< starts applied
    std::map<std::string, CachedClip, std::less<>> clips_{};
    std::set<std::string, std::less<>> failed_{};
    std::vector<PendingStart> pending_{};
    std::vector<Voice> voices_{};
    std::vector<std::string> errors_{};
    std::vector<int32_t> accumulator_{};
};

/// Returns the RIFF header of a WAVE file holding sample_frames frames of
/// 16-bit little-endian stereo at sample_rate.
///
/// A size that does not fit the header's 32 bits is written as all ones, as
/// a file still being written is.
///
/// @param sample_frames the sample frames that follow the header
/// @return the 44 header bytes
[[nodiscard]] std::array<uint8_t, wave_header_bytes> wave_header(uint64_t sample_frames) noexcept;

} // namespace oa::audio::offline_mix
