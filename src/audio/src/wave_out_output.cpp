// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The wave-out mixer: a BufferedOutput over the platform's wave-out device,
// which owns the device and frees it with the output.

#include "oa/audio/sound_output_backends.hpp"

#include "oa/audio/buffered_output.hpp"
#include "oa/platform/sound_device.hpp"

namespace oa::audio {
namespace {

class WaveOutOutput final : public SoundOutput {
  public:

    explicit WaveOutOutput(platform::sound_device::Hooks device)
        : device_(device), output_(device_) {}

    ~WaveOutOutput() override {
        output_.stop_all();
        platform::sound_device::wave_out_destroy(device_);
    }

    WaveOutOutput(const WaveOutOutput&) = delete;
    WaveOutOutput& operator=(const WaveOutOutput&) = delete;

    bool start(std::string& error) override { return output_.start(error); }

    void stop() override { output_.stop(); }

    bool started() const override { return output_.started(); }

    std::unique_ptr<OutputStream> open_stream(
        const StreamFormat& format, StreamFeed feed, void* context, std::string& error
    ) override {
        return output_.open_stream(format, feed, context, error);
    }

    std::string driver_name() const override { return output_.driver_name(); }

    std::string last_error() const override { return output_.last_error(); }

  private:

    platform::sound_device::Hooks device_;
    BufferedOutput output_;
};

} // namespace

std::unique_ptr<SoundOutput> wave_out_sound_output_create() {
    platform::sound_device::Hooks device = platform::sound_device::wave_out_create();
    if (device.open == nullptr) {
        platform::sound_device::wave_out_destroy(device);
        return nullptr;
    }
    return std::make_unique<WaveOutOutput>(device);
}

} // namespace oa::audio
