// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "oa/audio/wave_chunks.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace oa::audio {

// Platform boundaries. Every callback receives the owning context first.

using BufferHandle = uint32_t;
inline constexpr BufferHandle no_buffer = 0;

enum class DeviceResult : int32_t { ok, no_driver, failed };

enum class BufferKind : uint8_t {
    sample, // static, volume and 3D control
    stream, // static, volume control; refilled half by half
};

enum class SpatialMode : uint8_t { normal, disabled };

// World position handed to a voice; converted to float for the device.
struct VoicePosition {
    int32_t x{};
    int32_t y{};
    int32_t z{};
};

struct Spatial {
    SpatialMode mode{SpatialMode::disabled};
    float x{};
    float y{};
    float z{};
    float min_distance{};
    float max_distance{};
};

/// Returns the placement a voice is played with.
///
/// 3D at the position within the distance range while 3D sound is on and a
/// position is given, otherwise 3D processing off.
///
/// @param spatial_enabled Nonzero while 3D sound is enabled.
/// @param min_distance Distance below which the voice is not attenuated, in world units.
/// @param max_distance Distance beyond which the voice is no longer attenuated, in world units.
/// @param position World position of the voice; null for a non-positional voice.
/// @return Spatial settings for the device.
[[nodiscard]] Spatial voice_spatial(
    int32_t spatial_enabled, float min_distance, float max_distance, const VoicePosition* position
) noexcept;

// Sound-device boundary: the output device, its sample buffers and the
// output volume. Volumes are in hundredths of a decibel (0 is full scale).
struct AudioSink {
    void* context{};
    DeviceResult (*open_device)(void* context){};
    DeviceResult (*set_primary_format)(void* context, PcmFormat format){};
    void (*close_device)(void* context){};
    BufferHandle (*create_buffer)(
        void* context, BufferKind kind, PcmFormat format, uint32_t bytes
    ){};
    BufferHandle (*duplicate_buffer)(void* context, BufferHandle buffer){};
    void (*release_buffer)(void* context, BufferHandle buffer){};
    bool (*write_buffer)(
        void* context, BufferHandle buffer, uint32_t offset, const uint8_t* bytes, uint32_t count
    ){};
    bool (*query_playing)(void* context, BufferHandle buffer, bool* playing){};
    bool (*play_position)(void* context, BufferHandle buffer, uint32_t* position){};
    bool (*set_play_position)(void* context, BufferHandle buffer, uint32_t position){};
    bool (*set_volume)(void* context, BufferHandle buffer, int32_t centibels){};
    // False when the buffer has no 3D control; the caller continues.
    bool (*set_spatial)(void* context, BufferHandle buffer, const Spatial* spatial){};
    bool (*play)(void* context, BufferHandle buffer, bool looping){};
    void (*stop)(void* context, BufferHandle buffer){};
    int32_t (*wave_out_count)(void* context){};
    bool (*get_wave_out_volume)(void* context, int32_t device, uint32_t* packed){};
    bool (*set_wave_out_volume)(void* context, int32_t device, uint32_t packed){};
};

enum class CdTrackKind : uint8_t { audio, other, unavailable };

// CD-audio boundary: the CD player, its mixer line, and the test for another
// program's CD player holding the drive. A music-file backend maps disc
// tracks to files. play() and resume() report completion later through
// cd_on_play_complete().
struct MusicDevice {
    void* context{};
    bool (*open)(void* context){};
    void (*close)(void* context){};
    bool (*stop)(void* context){};
    bool (*track_count)(void* context, int32_t* count){};
    CdTrackKind (*first_track_kind)(void* context){};
    bool (*is_playing)(void* context){};
    bool (*current_track)(void* context, int32_t* track){};
    // Plays from the start of from_track to the start of to_track, or to the
    // end of the disc when to_track is 0.
    bool (*play)(void* context, int32_t from_track, int32_t to_track){};
    // Continues from the paused position to the start of to_track (0: end).
    bool (*resume)(void* context, int32_t to_track){};
    bool (*pause)(void* context){};
    bool (*disc_id)(void* context, uint32_t* id){};
    // Returns a nonzero handle when another CD player owns the drive.
    uint32_t (*find_foreign_player)(void* context){};
    void (*close_foreign_player)(void* context, uint32_t handle){};
    int32_t (*aux_count)(void* context){};
    bool (*aux_is_cd_audio)(void* context, int32_t device){};
    bool (*get_aux_volume)(void* context, int32_t device, uint32_t* packed){};
    bool (*set_aux_volume)(void* context, int32_t device, uint32_t packed){};
};

using TimerCallback = void (*)(void* user);

// Timer boundary. remove() must accept -1.
struct AudioTimers {
    void* context{};
    int32_t (*add)(void* context, uint32_t interval, TimerCallback callback, void* user){};
    void (*remove)(void* context, int32_t handle){};
};

// Resolves a sound path through the game's archive search order.
struct AudioFiles {
    void* context{};
    bool (*load)(void* context, const char* path, std::vector<uint8_t>* bytes){};
};

// The rand() stream shared with the rest of the game (0..32767).
struct AudioRandom {
    void* context{};
    int32_t (*next)(void* context){};
};

// Application settings boundary.
struct AudioSettings {
    void* context{};
    int32_t (*read_int)(void* context, const char* name, int32_t fallback){};
    bool (*read_blob)(void* context, const char* name, uint8_t* bytes, uint32_t* size){};
    void (*write_blob)(void* context, const char* name, const uint8_t* bytes, uint32_t size){};
};

// Mixer state: the sound voices, the stream and the CD-audio player.

using SampleId = uint32_t; // 1-based index into Mixer::samples
inline constexpr SampleId no_sample = 0;

inline constexpr std::size_t voice_slots = 32;
inline constexpr std::size_t transient_slots = 8;
inline constexpr std::size_t sample_buffers = 4;
inline constexpr std::size_t sample_pool = 512;
inline constexpr int32_t default_voice_limit = 8;
inline constexpr float default_min_distance = 1.0F;
inline constexpr float default_max_distance = 1.0e20F;
inline constexpr uint32_t max_device_volume = 0xffff;
inline constexpr int32_t no_timer = -1;
inline constexpr int32_t no_device = -1;
inline constexpr uint32_t unknown_volume = 0xffffffffU;

// Voice volumes the play requests pass.
inline constexpr int32_t volume_near = -585;
inline constexpr int32_t volume_far = -1585;

inline constexpr std::size_t cd_track_type_bytes = 100;
inline constexpr std::size_t cd_kind_positions = 10;

enum class CdPlayMode : int32_t {
    off = 0,
    sequential = 1,
    random = 2,
    selected = 3,
    by_kind = 4,
};

enum class CdPlayback : int32_t { stopped = 0, playing = 1, paused = 2 };

// Music kinds 2 and 3 fade the current track out before switching; kind 4
// stops music.
inline constexpr int32_t music_kind_stop = 4;

struct Sample {
    bool in_use{};
    BufferHandle buffers[sample_buffers]{};
};

struct CdAudio {
    int32_t device_open{};
    int32_t play_mode{}; // a CdPlayMode
    int32_t track_count{};
    int32_t selected_track{};
    int32_t current_track{};
    int32_t playback{}; // a CdPlayback
    uint32_t disc_id{};
    uint8_t track_types[cd_track_type_bytes]{}; // music kind by track number
    int32_t music_kind{};
    int32_t enabled{};
    int32_t first_track_offset{}; // 1 when track 1 is data
    int32_t fade_step{};
    TimerCallback disc_change_callback{};
    void* disc_change_user{};
    // Fade and foreign-player state of the one CD-audio player.
    uint32_t foreign_player{};
    int32_t fade_level{};
    int32_t fade_timer{no_timer};
    int32_t fade_end_timer{no_timer};
    int32_t kind_positions[cd_kind_positions]{};
};

struct StreamState {
    BufferHandle buffer{};
    std::vector<uint8_t> file; // the whole file being streamed
    WaveCursor cursor{};
    int32_t bits{};
    uint32_t half_bytes{};
    uint32_t write_offset{};
    int32_t end_offset{-1};  // -1 while file data remains
    int32_t timer{no_timer}; // pending delayed start
    char pending_name[256]{};
    int32_t pending_volume{};
};

struct Mixer {
    AudioSink sink{};
    MusicDevice music{};
    AudioTimers timers{};
    AudioFiles files{};
    AudioRandom random{};

    int32_t spatial_enabled{}; // 3D sound on for voices played with a position
    float min_distance{};
    float max_distance{};
    int32_t wave_out_count{};
    int32_t aux_device{no_device}; // the CD-audio line music plays on; no_device when none
    uint32_t saved_wave_out_volume{};
    uint32_t saved_aux_volume{};
    uint32_t music_volume{};
    int32_t device_open{};
    int32_t voice_limit{};
    int32_t voice_count{};
    int32_t voice_serial{};
    BufferHandle voices[voice_slots]{};
    int32_t voice_order[voice_slots]{}; // voice_serial when the voice started
    int32_t voice_looping[voice_slots]{};
    PcmFormat primary{};
    SampleId transients[transient_slots]{};
    StreamState stream{};
    int32_t no_driver{};
    CdAudio cd{};
    Sample samples[sample_pool]{};
};

/// Resets every field, discovers the volume devices, and records the current music line volume.
///
/// The platform boundaries (sink, music, timers, files, random) must already
/// be assigned and are kept.
///
/// @param[in,out] mixer Mixer to reset.
void mixer_init(Mixer& mixer) noexcept;

/// Releases the transient samples and the stream, closes the music device, and closes the sound device.
///
/// @param[in,out] mixer Mixer to shut down.
void mixer_teardown(Mixer& mixer) noexcept;

/// Opens the sound device on first use and sets the primary format.
///
/// A missing driver sets no_driver; any failure tears the mixer down.
///
/// @param[in,out] mixer Mixer that owns the device.
/// @param format Primary buffer format.
/// @return True when the device is open with the format applied.
[[nodiscard]] bool mixer_open_device(Mixer& mixer, PcmFormat format) noexcept;

/// Frees finished transient samples, forgets finished voices, then refills the stream buffer when one is active.
///
/// @param[in,out] mixer Mixer to service.
void mixer_collect_finished(Mixer& mixer) noexcept;

/// Stops every voice.
///
/// @param[in,out] mixer Mixer whose voices stop.
/// @quirk Looping marks are left as they were.
void mixer_stop_voices(Mixer& mixer) noexcept;

/// Stops the oldest non-looping voice.
///
/// @param[in,out] mixer Mixer whose voice table is searched.
/// @return False when no non-looping voice exists.
bool mixer_evict_oldest_voice(Mixer& mixer) noexcept;

/// Sets how many voices may play at once.
///
/// @param[in,out] mixer Mixer to configure.
/// @param limit Voice limit; older non-looping voices are evicted above it.
void mixer_set_voice_limit(Mixer& mixer, int32_t limit) noexcept;

/// Returns how many voices may play at once.
///
/// @param mixer Mixer to query.
/// @return Voice limit.
[[nodiscard]] int32_t mixer_voice_limit(const Mixer& mixer) noexcept;

/// Creates a sample buffer and fills it with bytes from the cursor.
///
/// @param[in,out] mixer Mixer that owns the sample pool.
/// @param[in,out] cursor Source cursor; advances past the copied bytes.
/// @param bytes Number of bytes to copy; fails when fewer remain.
/// @param format PCM format of the bytes.
/// @return New sample id, no_sample on failure.
[[nodiscard]] SampleId
mixer_create_sample(Mixer& mixer, WaveCursor& cursor, uint32_t bytes, PcmFormat format) noexcept;

/// Stops and releases every buffer of the sample, forgetting the voices that used them, and frees the sample.
///
/// @param[in,out] mixer Mixer that owns the sample.
/// @param sample Sample to free; unknown ids are ignored.
void mixer_release_sample(Mixer& mixer, SampleId sample) noexcept;

/// Plays a sample on a free voice.
///
/// An idle buffer of the sample is reused, a duplicate is made while fewer
/// than four exist, otherwise the furthest-played buffer is restarted. The
/// oldest non-looping voices are evicted while the voice limit is reached.
///
/// @param[in,out] mixer Mixer that owns the sample.
/// @param sample Sample to play.
/// @param volume Voice volume in hundredths of a decibel (0 is full scale).
/// @param position World position for 3D placement; null for none.
/// @return True when the voice started.
bool mixer_play_sample(
    Mixer& mixer, SampleId sample, int32_t volume, const VoicePosition* position
) noexcept;

/// Plays a sample as the single looping voice; refused while one is active.
///
/// @param[in,out] mixer Mixer that owns the sample.
/// @param sample Sample to loop.
/// @param volume Voice volume in hundredths of a decibel (0 is full scale).
/// @return True when the voice started.
bool mixer_play_looping(Mixer& mixer, SampleId sample, int32_t volume) noexcept;

/// Loads a sample into a free transient slot and plays it; the slot is freed once the voice finishes.
///
/// @param[in,out] mixer Mixer that owns the transient slots.
/// @param[in,out] cursor Source cursor; advances past the copied bytes.
/// @param bytes Number of sample bytes to copy.
/// @param format PCM format of the bytes.
/// @param volume Voice volume in hundredths of a decibel (0 is full scale).
/// @param position World position for 3D placement; null for none.
/// @return True when the voice started.
bool mixer_play_transient(
    Mixer& mixer,
    WaveCursor& cursor,
    uint32_t bytes,
    PcmFormat format,
    int32_t volume,
    const VoicePosition* position
) noexcept;

/// Starts double-buffered playback of the file's remaining bytes with a two-second looping buffer.
///
/// Stops any previous stream and cancels a pending delayed start first.
///
/// @param[in,out] mixer Mixer that owns the stream.
/// @param file Whole sound file; the mixer takes ownership.
/// @param data_offset Byte offset of the sample data within the file.
/// @param format PCM format of the sample data.
/// @param volume Stream volume in hundredths of a decibel (0 is full scale).
void mixer_start_stream(
    Mixer& mixer, std::vector<uint8_t> file, uint32_t data_offset, PcmFormat format, int32_t volume
) noexcept;

/// Cancels a pending delayed start and stops and releases the stream.
///
/// @param[in,out] mixer Mixer that owns the stream.
void mixer_stop_stream(Mixer& mixer) noexcept;

/// Reports whether a stream plays or a delayed start is pending.
///
/// @param mixer Mixer that owns the stream.
/// @return True while busy.
[[nodiscard]] bool mixer_stream_busy(const Mixer& mixer) noexcept;

/// Refills the half the play cursor has left, or stops the stream once the silence after the file's end has been reached.
///
/// @param[in,out] mixer Mixer that owns the stream.
void mixer_update_stream(Mixer& mixer) noexcept;

/// Writes the next half buffer from the file, padding with silence after its end, and flips the write half.
///
/// A failed write stops the stream.
///
/// @param[in,out] mixer Mixer that owns the stream.
void mixer_fill_stream(Mixer& mixer) noexcept;

/// Turns 3D sound on for voices played with a position.
///
/// @param[in,out] mixer Mixer to configure.
void mixer_enable_spatial(Mixer& mixer) noexcept;

/// Sets the 3D attenuation distance range.
///
/// @param[in,out] mixer Mixer to configure.
/// @param min_distance Distance below which voices are not attenuated, in world units.
/// @param max_distance Distance beyond which voices are no longer attenuated, in world units.
void mixer_set_distance_range(Mixer& mixer, float min_distance, float max_distance) noexcept;

/// Reports whether opening the device failed for lack of a driver.
///
/// @param mixer Mixer to query.
/// @return True when no sound driver was found.
[[nodiscard]] bool mixer_no_driver(const Mixer& mixer) noexcept;

/// Counts the wave output devices, picks the first CD-audio auxiliary line, and saves both volumes for restoration.
///
/// @param[in,out] mixer Mixer to configure.
void mixer_init_volume_devices(Mixer& mixer) noexcept;

/// Reads the volume of the first readable wave output device.
///
/// @param mixer Mixer that knows the devices.
/// @return Low word of the packed volume (0..0xffff), else unknown_volume.
[[nodiscard]] uint32_t mixer_wave_out_volume(const Mixer& mixer) noexcept;

/// Reads the music line volume.
///
/// @param mixer Mixer that knows the music line.
/// @return Low word of the packed volume (0..0xffff), else unknown_volume.
[[nodiscard]] uint32_t mixer_music_line_volume(const Mixer& mixer) noexcept;

/// Applies a level to both channels of every wave output device.
///
/// @param[in,out] mixer Mixer that knows the devices.
/// @param level Device level, clamped to 0..0xffff.
/// @return True when any device rejected the level.
bool mixer_set_wave_out_volume(Mixer& mixer, int32_t level) noexcept;

/// Applies a level to both channels of the music line.
///
/// Ignored while a fade runs unless transient; only a non-transient level is
/// remembered as the music volume.
///
/// @param[in,out] mixer Mixer that knows the music line.
/// @param level Device level, clamped to 0..0xffff.
/// @param transient True for a fade step that must not change the remembered volume.
/// @return The device's result, true when ignored during a fade.
bool mixer_set_music_volume(Mixer& mixer, int32_t level, bool transient) noexcept;

/// Restores the wave output and music line volumes saved by mixer_init_volume_devices().
///
/// @param[in,out] mixer Mixer that knows the devices.
void mixer_restore_volumes(Mixer& mixer) noexcept;

enum class WaveLoadMode : int32_t { sample = 0, play = 1, stream = 2 };

/// Opens a sound file and loads it by mode.
///
/// @param[in,out] mixer Mixer that loads the file.
/// @param path Sound path resolved through the game's archive search order.
/// @param mode sample creates a sample, play plays a transient voice, stream starts the stream.
/// @param volume Volume in hundredths of a decibel (0 is full scale); unused for sample.
/// @param position World position for 3D placement; used by play only, null for none.
/// @return The sample id for sample, 1 on success for play, 1 for stream; 0 on failure.
uint32_t mixer_load_wave(
    Mixer& mixer, const char* path, WaveLoadMode mode, int32_t volume, const VoicePosition* position
) noexcept;

/// Loads a sound file as a sample.
///
/// @param[in,out] mixer Mixer that owns the sample pool.
/// @param path Sound path resolved through the game's archive search order.
/// @return New sample id, no_sample on failure.
[[nodiscard]] SampleId mixer_load_sample(Mixer& mixer, const char* path) noexcept;

/// Loads a sound file and plays it once on a transient voice.
///
/// @param[in,out] mixer Mixer that plays the file.
/// @param path Sound path resolved through the game's archive search order.
/// @param volume Voice volume in hundredths of a decibel (0 is full scale).
/// @param position World position for 3D placement; null for none.
/// @return True when the voice started.
bool mixer_play_wave_file(
    Mixer& mixer, const char* path, int32_t volume, const VoicePosition* position
) noexcept;

/// Starts the stream recorded by mixer_schedule_stream().
///
/// Timer callback.
///
/// @param[in,out] mixer The Mixer, passed as the timer's user value.
void mixer_on_stream_timer(void* mixer) noexcept;

/// Records a file to stream after a delay.
///
/// @param[in,out] mixer Mixer that owns the stream.
/// @param path Sound path; truncated to 255 characters.
/// @param volume Stream volume in hundredths of a decibel (0 is full scale).
/// @param delay Delay in timer ticks.
/// @return Always true.
bool mixer_schedule_stream(Mixer& mixer, const char* path, int32_t volume, uint32_t delay) noexcept;

/// Looks up a sample in the pool.
///
/// @param mixer Mixer that owns the sample pool.
/// @param id 1-based sample id.
/// @return The sample, or null when the id is out of range or unused.
[[nodiscard]] Sample* mixer_sample(Mixer& mixer, SampleId id) noexcept;

} // namespace oa::audio
