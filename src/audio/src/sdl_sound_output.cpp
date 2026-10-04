// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The sound output over SDL: the engine's software mixer mixes every stream
// into one SDL audio stream, which SDL plays on the default playback device.

#include "oa/audio/sound_output_backends.hpp"

#include "oa/audio/software_mixer.hpp"

#include <SDL3/SDL.h>

#include <vector>

namespace oa::audio {
namespace {

/// Bytes of one mixed frame: 16-bit left and right.
constexpr int mixed_frame_bytes = static_cast<int>(mixer_output_channels * sizeof(int16_t));

class SdlOutput final : public SoundOutput {
  public:

    SdlOutput() : mutex_(SDL_CreateMutex()), mixer_(MixerLock{this, lock_thunk, unlock_thunk}) {}

    // Every stream of the mixer must have been destroyed first.
    ~SdlOutput() override {
        close_device();
        if (mutex_ != nullptr)
            SDL_DestroyMutex(mutex_);
    }

    SdlOutput(const SdlOutput&) = delete;
    SdlOutput& operator=(const SdlOutput&) = delete;

    bool start(std::string& error) override {
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
            error = SDL_GetError();
            return false;
        }
        if (!open_device(error)) {
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
            return false;
        }
        ++start_count_;
        return true;
    }

    void stop() override {
        if (start_count_ > 0)
            --start_count_;
        // The device closes with the last start once no stream is left to play on it.
        if (start_count_ == 0 && mixer_.stream_count() == 0)
            close_device();
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }

    bool started() const override { return SDL_WasInit(SDL_INIT_AUDIO) != 0; }

    std::unique_ptr<OutputStream> open_stream(
        const StreamFormat& format, StreamFeed feed, void* context, std::string& error
    ) override {
        if (!open_device(error))
            return nullptr;
        return mixer_.open_stream(format, feed, context, error);
    }

    std::string driver_name() const override {
        const char* name = SDL_GetCurrentAudioDriver();
        return name != nullptr ? name : "";
    }

    std::string last_error() const override { return SDL_GetError(); }

  private:

    /// Takes the mixer's lock, which the thread holding it may take again.
    ///
    /// @param output the SdlOutput
    static void lock_thunk(void* output) { SDL_LockMutex(static_cast<SdlOutput*>(output)->mutex_); }

    /// Releases one hold of the mixer's lock.
    ///
    /// @param output the SdlOutput
    static void unlock_thunk(void* output) {
        SDL_UnlockMutex(static_cast<SdlOutput*>(output)->mutex_);
    }

    /// Mixes what SDL asks for; runs on SDL's audio thread.
    ///
    /// @param output the SdlOutput
    /// @param stream the stream SDL plays
    /// @param additional bytes SDL needs now
    static void SDLCALL feed_device(void* output, SDL_AudioStream* stream, int additional, int) {
        auto& self = *static_cast<SdlOutput*>(output);
        if (additional <= 0)
            return;
        const int frames = (additional + mixed_frame_bytes - 1) / mixed_frame_bytes;
        self.mixed_.resize(static_cast<std::size_t>(frames) * mixer_output_channels);
        self.mixer_.mix(self.mixed_.data(), static_cast<uint32_t>(frames));
        SDL_PutAudioStreamData(stream, self.mixed_.data(), frames * mixed_frame_bytes);
    }

    /// Opens the device stream that plays the mix, unless it is open.
    ///
    /// The stream holds a start of SDL's audio subsystem of its own, so it
    /// stays open while streams play on it. SDL_Quit ends it with the rest of
    /// SDL; it is opened again after the next start.
    ///
    /// @param[out] error why it cannot be opened; untouched on success
    /// @return true when the device stream is open
    bool open_device(std::string& error) {
        if (device_ != nullptr && SDL_WasInit(SDL_INIT_AUDIO) == 0)
            device_ = nullptr; // SDL has quit, and its streams with it
        if (device_ != nullptr)
            return true;
        if (mutex_ == nullptr) {
            error = "cannot make the sound output's lock";
            return false;
        }
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
            error = SDL_GetError();
            return false;
        }
        const SDL_AudioSpec spec{
            SDL_AUDIO_S16LE,
            static_cast<int>(mixer_output_channels),
            static_cast<int>(mixer_output_rate)
        };
        device_ =
            SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, feed_device, this);
        if (device_ == nullptr || !SDL_ResumeAudioStreamDevice(device_)) {
            error = SDL_GetError();
            if (device_ != nullptr)
                SDL_DestroyAudioStream(device_);
            device_ = nullptr;
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
            return false;
        }
        return true;
    }

    /// Closes the device stream, if it is open, with its start of SDL's audio.
    void close_device() {
        if (device_ == nullptr)
            return;
        if (SDL_WasInit(SDL_INIT_AUDIO) != 0) {
            SDL_DestroyAudioStream(device_);
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
        }
        device_ = nullptr;
    }

    SDL_Mutex* mutex_{};         ///< the mixer's lock
    SoftwareMixer mixer_;        ///< mixes every stream
    SDL_AudioStream* device_{};  ///< the stream SDL plays the mix from
    std::vector<int16_t> mixed_; ///< one call's mix, for SDL's audio thread
    uint32_t start_count_{};     ///< start() calls not yet matched by stop()
};

} // namespace

std::unique_ptr<SoundOutput> sdl_sound_output_create() {
    return std::make_unique<SdlOutput>();
}

} // namespace oa::audio
