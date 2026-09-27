// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "oa/audio/mixer.hpp"
#include "oa/audio/game_audio.hpp"

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace oa::audio::game_audio {

class SdlWavPlayer {
  public:

    /// Creates a player that reads WAV resources from the asset store.
    ///
    /// @param assets Asset store; must outlive the player.
    explicit SdlWavPlayer(const oa::AssetStore& assets);
    ~SdlWavPlayer();
    SdlWavPlayer(SdlWavPlayer&&) noexcept;
    SdlWavPlayer& operator=(SdlWavPlayer&&) noexcept;
    SdlWavPlayer(const SdlWavPlayer&) = delete;
    SdlWavPlayer& operator=(const SdlWavPlayer&) = delete;

    /// Plays one selected sound's resource at the near volume.
    ///
    /// @param selection Result of select(); must have status selected.
    /// @param[out] error Reason for a missing or invalid WAV or an unavailable
    ///             SDL device; cleared on success.
    /// @return True when the sound started.
    [[nodiscard]] bool play(const Selection& selection, std::string& error);

    /// Plays a resource directly at the near volume, without the ALLSOUND registry.
    ///
    /// The route unit SOUND.TDF announcements take.
    ///
    /// @param resource Archive path of the WAV file.
    /// @param[out] error Reason for a failure; cleared on success.
    /// @return True when the sound started.
    [[nodiscard]] bool play_resource(std::string_view resource, std::string& error);

    /// Plays a resource at a voice volume with a 3D placement.
    ///
    /// @param resource Archive path of the WAV file.
    /// @param volume Voice volume in hundredths of a decibel, heard relative to
    ///        audio::volume_near, the volume the game gives every unplaced effect.
    /// @param spatial Placement; a disabled mode plays at full level on both sides.
    /// @param[out] error Reason for a failure; cleared on success.
    /// @return True when the sound started.
    [[nodiscard]] bool play_placed(
        std::string_view resource,
        int32_t volume,
        const oa::audio::Spatial& spatial,
        std::string& error
    );
    /// Loops a WAV resolved through the asset store, replacing any current loop.
    ///
    /// Used for the ALLSOUND background sound.
    ///
    /// @param resource Archive path of the WAV file.
    /// @param[out] error Reason for a failure; cleared on success.
    /// @return True when the loop started.
    [[nodiscard]] bool start_loop_resource(std::string_view resource, std::string& error);

    /// Stops the looping sound, if any.
    void stop_loop() noexcept;

    /// Streams a WAV resolved through the asset store once, as the one stream the player keeps.
    ///
    /// The briefing's narration and the end screen's glamour sound take this
    /// route. A stream still playing or waiting stops first, so the new one is
    /// heard from its beginning.
    ///
    /// @param resource Archive path of the WAV file.
    /// @param delay_ms Silence before the sound starts, in milliseconds.
    /// @param[out] error Reason for a failure; cleared on success.
    /// @return True when the stream started.
    [[nodiscard]] bool
    play_stream(std::string_view resource, uint32_t delay_ms, std::string& error);

    /// Stops the stream at once, during its delay too.
    void stop_stream() noexcept;

    /// Tells whether the stream is waiting out its delay or still playing.
    ///
    /// @return False once the stream has played out, has been stopped or never started.
    [[nodiscard]] bool stream_busy() const noexcept;

    /// Sets the output volume of every current and later sound.
    ///
    /// @param wave_out_volume Restored WaveOutVolume scalar, 0..0xffff.
    /// @param fx_volume Effects volume preference, 0..64.
    void set_volume(uint32_t wave_out_volume, uint32_t fx_volume) noexcept;

    /// Releases the streams of sounds, the one stream included, that have finished playing.
    void collect_finished() noexcept;

  private:

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace oa::audio::game_audio

namespace oa::audio {

// SDL3 implementation of the mixer's sound-device boundary. Each buffer is
// an SDL audio stream fed from its PCM image on the audio thread; the play
// position is the next byte handed to SDL. 3D placement is not rendered.
struct SdlAudioDevice;

/// Creates an SDL sound device; SDL audio opens only through the sink's open_device.
///
/// @return The device.
[[nodiscard]] SdlAudioDevice* sdl_audio_device_create();

/// Destroys every voice stream and frees the device.
///
/// @param device Device to free; null is ignored.
void sdl_audio_device_destroy(SdlAudioDevice* device) noexcept;

/// Returns the sound-device boundary table bound to a device.
///
/// @param device Device that becomes the table's context.
/// @return Boundary callbacks for Mixer::sink.
[[nodiscard]] AudioSink sdl_audio_sink(SdlAudioDevice* device) noexcept;

} // namespace oa::audio
