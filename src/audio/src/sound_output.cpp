// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/audio/sound_output.hpp"

#include "oa/audio/sound_output_backends.hpp"

#include <cstdlib>
#include <string_view>

namespace oa::audio {
namespace {

constexpr uint8_t unsigned_8_silence = 0x80;
// The environment variable that picks the wave-out mixer in a Windows build
// that has SDL too, and the value that picks it.
constexpr const char* output_variable = "OA_SOUND_OUTPUT";
constexpr std::string_view wave_out_choice = "waveout";

// An output for a build with no sound device: nothing starts.
class SilentOutput final : public SoundOutput {
  public:

    bool start(std::string& error) override {
        error = "this build has no sound output";
        return false;
    }

    void stop() override {}

    bool started() const override { return false; }

    std::unique_ptr<OutputStream>
    open_stream(const StreamFormat&, StreamFeed, void*, std::string& error) override {
        error = "this build has no sound output";
        return nullptr;
    }

    std::string driver_name() const override { return ""; }

    std::string last_error() const override { return "this build has no sound output"; }
};

SoundOutput* chosen_output = nullptr;
// The process's own output, made on first use. sound_output() is first
// called on the main thread, before any stream exists.
SoundOutput* own_output = nullptr;

std::unique_ptr<SoundOutput> make_own_output() {
    const char* choice = std::getenv(output_variable);
    std::unique_ptr<SoundOutput> output;
    if (choice != nullptr && std::string_view(choice) == wave_out_choice)
        output = wave_out_sound_output_create();
    if (output == nullptr)
        output = sdl_sound_output_create();
    if (output == nullptr)
        output = wave_out_sound_output_create();
    if (output == nullptr)
        output = std::make_unique<SilentOutput>();
    return output;
}

} // namespace

uint32_t sample_bytes(SampleFormat sample) noexcept {
    switch (sample) {
    case SampleFormat::u8:
        return 1;
    case SampleFormat::s16:
        return 2;
    case SampleFormat::s32:
    case SampleFormat::f32:
        return 4;
    }
    return 1;
}

uint32_t frame_bytes(const StreamFormat& format) noexcept {
    return sample_bytes(format.sample) * format.channels;
}

uint8_t silence_byte(SampleFormat sample) noexcept {
    return sample == SampleFormat::u8 ? unsigned_8_silence : 0;
}

SoundOutput& sound_output() {
    if (chosen_output != nullptr)
        return *chosen_output;
    if (own_output == nullptr)
        own_output = make_own_output().release(); // lives until the process ends
    return *own_output;
}

void set_sound_output(SoundOutput* output) noexcept {
    chosen_output = output;
}

} // namespace oa::audio
