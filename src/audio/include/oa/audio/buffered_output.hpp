// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "oa/audio/software_mixer.hpp"
#include "oa/audio/sound_output.hpp"
#include "oa/platform/sound_device.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace oa::audio {

/// Frames in each buffer a buffered output fills: about 23 ms.
inline constexpr uint32_t output_buffer_frames = 1024;
/// Buffers a buffered output keeps queued on the device: about 93 ms in all.
inline constexpr uint32_t output_buffer_count = 4;

/// A sound output that mixes its streams with a SoftwareMixer into a ring
/// of buffers on a system sound device (oa/platform/sound_device.hpp).
///
/// start() opens the device, fills and queues every buffer, then starts the
/// pump, which refills each buffer as it plays out, in ring order.
class BufferedOutput final : public SoundOutput {
  public:

    /// Creates an output over a device, not yet started.
    ///
    /// @param device the device; its context must outlive the output
    explicit BufferedOutput(platform::sound_device::Hooks device);
    /// Stops the device if it is still started.
    ~BufferedOutput() override;
    BufferedOutput(const BufferedOutput&) = delete;
    BufferedOutput& operator=(const BufferedOutput&) = delete;

    /// Opens the device and starts playing on the first call; counts the later ones.
    ///
    /// @param[out] error why the device cannot start; untouched on success
    /// @return true when the device plays
    [[nodiscard]] bool start(std::string& error) override;

    /// Counts one start() less, closing the device after the last.
    void stop() override;

    /// Tells whether the device has started.
    ///
    /// @return true between the first start() and the last stop()
    [[nodiscard]] bool started() const override;

    /// Closes the device whatever the count of start() calls.
    void stop_all();

    /// Opens a paused stream of the mixer.
    ///
    /// @param format the samples the stream takes
    /// @param feed asks for more samples as the stream plays; null for none
    /// @param context passed back to the feed
    /// @param[out] error why the stream cannot be opened; untouched on success
    /// @return the stream, or null on failure
    [[nodiscard]] std::unique_ptr<OutputStream> open_stream(
        const StreamFormat& format, StreamFeed feed, void* context, std::string& error
    ) override;

    /// Names the device.
    ///
    /// @return the device's name, or "" when it has not started
    [[nodiscard]] std::string driver_name() const override;

    /// Returns the reason the device last failed to start or to take a buffer.
    ///
    /// @return the reason, "" when none
    [[nodiscard]] std::string last_error() const override;

    /// Fills and queues every free buffer, in ring order.
    ///
    /// The device's pump calls it; a test may call it directly.
    ///
    /// @return the buffers queued
    uint32_t pump();

    /// Returns the buffers queued since start().
    ///
    /// @return the count
    [[nodiscard]] uint64_t buffers_queued() const noexcept { return buffers_queued_; }

  private:

    /// Runs pump() for the device's thread.
    ///
    /// @param output the BufferedOutput
    static void pump_thunk(void* output);

    /// Takes the device's lock.
    void lock() const;

    /// Releases the device's lock.
    void unlock() const;

    platform::sound_device::Hooks device_;
    SoftwareMixer mixer_;
    std::vector<std::vector<int16_t>> buffers_;
    uint32_t next_{};        ///< the buffer that is queued next
    uint32_t start_count_{}; ///< start() calls not yet matched by stop()
    uint64_t buffers_queued_{};
    std::string error_;
};

} // namespace oa::audio
