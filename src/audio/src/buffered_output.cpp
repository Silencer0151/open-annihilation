// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/audio/buffered_output.hpp"

namespace oa::audio {

BufferedOutput::BufferedOutput(platform::sound_device::Hooks device)
    : device_(device), mixer_(MixerLock{device.context, device.lock, device.unlock}) {
}

BufferedOutput::~BufferedOutput() {
    stop_all();
}

void BufferedOutput::stop_all() {
    if (start_count_ > 0) {
        start_count_ = 1;
        stop();
    }
}

bool BufferedOutput::start(std::string& error) {
    if (start_count_ > 0) {
        ++start_count_;
        return true;
    }
    if (device_.open == nullptr) {
        error = error_ = "no sound device";
        return false;
    }
    std::string reason;
    if (!device_.open(
            device_.context, mixer_output_rate, output_buffer_frames, output_buffer_count, reason
        )) {
        error = error_ = reason;
        return false;
    }
    buffers_.assign(
        output_buffer_count,
        std::vector<int16_t>(static_cast<std::size_t>(output_buffer_frames) * mixer_output_channels)
    );
    next_ = 0;
    buffers_queued_ = 0;
    start_count_ = 1;
    pump();
    if (device_.start_pump != nullptr && !device_.start_pump(device_.context, pump_thunk, this)) {
        error = error_ = "cannot start the sound thread";
        device_.close(device_.context);
        start_count_ = 0;
        return false;
    }
    return true;
}

void BufferedOutput::stop() {
    if (start_count_ == 0 || --start_count_ > 0)
        return;
    if (device_.stop_pump != nullptr)
        device_.stop_pump(device_.context);
    if (device_.close != nullptr)
        device_.close(device_.context);
}

bool BufferedOutput::started() const {
    return start_count_ > 0;
}

std::unique_ptr<OutputStream> BufferedOutput::open_stream(
    const StreamFormat& format, StreamFeed feed, void* context, std::string& error
) {
    return mixer_.open_stream(format, feed, context, error);
}

std::string BufferedOutput::driver_name() const {
    return started() ? device_.name : "";
}

std::string BufferedOutput::last_error() const {
    lock();
    std::string error = error_;
    unlock();
    return error;
}

uint32_t BufferedOutput::pump() {
    // The lock keeps the ring position and the error to one thread at a time.
    lock();
    uint32_t queued = 0;
    for (uint32_t tried = 0; tried < output_buffer_count; ++tried) {
        if (!device_.buffer_free(device_.context, next_))
            break;
        auto& buffer = buffers_[next_];
        mixer_.mix(buffer.data(), output_buffer_frames);
        if (!device_.queue_buffer(device_.context, next_, buffer.data(), output_buffer_frames)) {
            error_ = "the sound device refused a buffer";
            break;
        }
        next_ = (next_ + 1) % output_buffer_count;
        ++queued;
        ++buffers_queued_;
    }
    unlock();
    return queued;
}

void BufferedOutput::lock() const {
    if (device_.lock != nullptr)
        device_.lock(device_.context);
}

void BufferedOutput::unlock() const {
    if (device_.unlock != nullptr)
        device_.unlock(device_.context);
}

void BufferedOutput::pump_thunk(void* output) {
    static_cast<BufferedOutput*>(output)->pump();
}

} // namespace oa::audio
