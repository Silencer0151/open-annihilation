// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The software mixer and the buffered output on a device the test plays by
// hand; with --wave-out on Windows, the wave-out mixer on the system's
// device.

#include "oa/audio/buffered_output.hpp"
#include "oa/audio/software_mixer.hpp"
#include "oa/audio/sound_output.hpp"
#include "oa/audio/sound_output_backends.hpp"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

using namespace oa::audio;

namespace {

[[noreturn]] void fail(const char* file, int line, const char* expression) {
    std::fprintf(stderr, "%s:%d: check failed: %s\n", file, line, expression);
    std::exit(1);
}

#define CHECK(expression)                                                                          \
    do {                                                                                           \
        if (!(expression))                                                                         \
            fail(__FILE__, __LINE__, #expression);                                                 \
    } while (false)

constexpr int skipped = 77;

std::recursive_mutex mixer_mutex;
int lock_depth = 0;

MixerLock test_lock() {
    MixerLock lock{};
    lock.lock = [](void*) {
        mixer_mutex.lock();
        ++lock_depth;
    };
    lock.unlock = [](void*) {
        --lock_depth;
        mixer_mutex.unlock();
    };
    return lock;
}

template <typename Sample>
std::vector<uint8_t> bytes_of(const std::vector<Sample>& samples) {
    std::vector<uint8_t> bytes(samples.size() * sizeof(Sample));
    std::memcpy(bytes.data(), samples.data(), bytes.size());
    return bytes;
}

std::vector<int16_t> mix(SoftwareMixer& mixer, uint32_t frames) {
    std::vector<int16_t> out(static_cast<std::size_t>(frames) * mixer_output_channels, 123);
    mixer.mix(out.data(), frames);
    return out;
}

void formats() {
    CHECK(sample_bytes(SampleFormat::u8) == 1 && sample_bytes(SampleFormat::s16) == 2);
    CHECK(sample_bytes(SampleFormat::s32) == 4 && sample_bytes(SampleFormat::f32) == 4);
    CHECK(frame_bytes(StreamFormat{SampleFormat::s16, 2, 44100}) == 4);
    CHECK(silence_byte(SampleFormat::u8) == 0x80 && silence_byte(SampleFormat::s16) == 0);
}

void conversion_and_gain() {
    SoftwareMixer mixer(test_lock());
    std::string error;
    CHECK(mixer.open_stream({SampleFormat::s16, 0, 44100}, nullptr, nullptr, error) == nullptr);
    CHECK(mixer.open_stream({SampleFormat::s16, 1, 10}, nullptr, nullptr, error) == nullptr);

    // A stream opens paused and adds nothing.
    auto mono = mixer.open_stream({SampleFormat::s16, 1, 44100}, nullptr, nullptr, error);
    CHECK(mono != nullptr && mixer.stream_count() == 1);
    const auto samples = bytes_of(std::vector<int16_t>{16384, -16384, 32767, -32768});
    CHECK(mono->put(samples.data(), static_cast<int32_t>(samples.size())));
    CHECK(mono->queued_bytes() == 8);
    for (const int16_t sample : mix(mixer, 4))
        CHECK(sample == 0);

    // Mono plays on both sides; -1..1 is rounded by 32767.
    CHECK(mono->resume());
    auto out = mix(mixer, 4);
    const std::vector<int16_t> both{16384, 16384, -16384, -16384, 32766, 32766, -32767, -32767};
    CHECK(out == both);
    CHECK(mono->queued_bytes() == 0 && mono->available_bytes() == 0);
    // Nothing left: silence, not the last samples again.
    for (const int16_t sample : mix(mixer, 4))
        CHECK(sample == 0);

    // Each format scales to -1..1; the gain multiplies.
    struct Case {
        SampleFormat sample;
        std::vector<uint8_t> frame; // one stereo frame
        int16_t left;
        int16_t right;
    };

    float quarter = 0.25F;
    std::vector<uint8_t> float_frame(8);
    std::memcpy(float_frame.data(), &quarter, 4);
    quarter = -1.5F;
    std::memcpy(float_frame.data() + 4, &quarter, 4);
    const Case cases[] = {
        {SampleFormat::u8, {0xc0, 0x40}, 16384, -16384},
        {SampleFormat::s16, {0x00, 0x20, 0x00, 0xe0}, 8192, -8192},
        {SampleFormat::s32, {0, 0, 0, 0x40, 0, 0, 0, 0xc0}, 16384, -16384},
        {SampleFormat::f32, float_frame, 8192, -32767}, // -1.5 clamps to -1
    };
    for (const auto& c : cases) {
        auto stream = mixer.open_stream({c.sample, 2, 44100}, nullptr, nullptr, error);
        CHECK(
            stream != nullptr && stream->put(c.frame.data(), static_cast<int32_t>(c.frame.size()))
        );
        CHECK(stream->resume());
        out = mix(mixer, 1);
        CHECK(out[0] == c.left && out[1] == c.right);
    }
    auto stereo = mixer.open_stream({SampleFormat::s16, 2, 44100}, nullptr, nullptr, error);
    const auto pair = bytes_of(std::vector<int16_t>{16384, 16384});
    CHECK(stereo->set_gain(0.5F) && stereo->put(pair.data(), 4) && stereo->resume());
    out = mix(mixer, 1);
    CHECK(out[0] == 8192 && out[1] == 8192);

    // Two streams add up, clamped.
    auto other = mixer.open_stream({SampleFormat::s16, 2, 44100}, nullptr, nullptr, error);
    CHECK(stereo->set_gain(1.0F) && stereo->put(pair.data(), 4));
    const auto loud = bytes_of(std::vector<int16_t>{24576, -8192});
    CHECK(other->put(loud.data(), 4) && other->resume());
    out = mix(mixer, 1);
    CHECK(out[0] == 32767 && out[1] == 8192);

    // Pause keeps the samples; clear drops them; destroying unregisters.
    CHECK(other->put(loud.data(), 4) && other->pause());
    out = mix(mixer, 1);
    CHECK(out[0] == 0 && out[1] == 0 && other->queued_bytes() == 4);
    other->clear();
    CHECK(other->resume() && other->queued_bytes() == 0);
    out = mix(mixer, 1);
    CHECK(out[0] == 0);
    const std::size_t open = mixer.stream_count();
    other.reset();
    CHECK(mixer.stream_count() == open - 1);
    CHECK(lock_depth == 0);
}

struct Counter {
    int16_t next{};
    int calls{};
    int32_t last_wanted{};
    bool locked_during_feed{};
};

void feed_counter(void* context, OutputStream& stream, int32_t wanted) {
    auto& counter = *static_cast<Counter*>(context);
    ++counter.calls;
    counter.last_wanted = wanted;
    counter.locked_during_feed = lock_depth > 0;
    std::vector<int16_t> samples(static_cast<std::size_t>(wanted) / 2);
    for (auto& sample : samples)
        sample = counter.next++;
    (void)stream.put(samples.data(), static_cast<int32_t>(samples.size() * 2));
}

void feeds() {
    // A mono stream at the output rate, fed as it plays: every frame once,
    // in order, across mixes.
    SoftwareMixer mixer(test_lock());
    Counter counter;
    std::string error;
    auto stream = mixer.open_stream({SampleFormat::s16, 1, 44100}, feed_counter, &counter, error);
    CHECK(stream->resume());
    int16_t expected = 0;
    for (int round = 0; round < 5; ++round) {
        const auto out = mix(mixer, 300);
        for (std::size_t frame = 0; frame < 300; ++frame) {
            const auto want = static_cast<int16_t>(std::lround(expected * (32767.0F / 32768.0F)));
            CHECK(out[2 * frame] == want && out[2 * frame + 1] == want);
            ++expected;
        }
    }
    CHECK(counter.calls >= 1 && counter.last_wanted > 0 && counter.locked_during_feed);

    // A resampled stream: a flushed input of n frames at 22050 Hz plays as
    // 2n frames at 44100, then stops.
    auto slow = mixer.open_stream({SampleFormat::s16, 2, 22050}, nullptr, nullptr, error);
    stream.reset();
    std::vector<int16_t> constant(2 * 1000, 8192);
    const auto bytes = bytes_of(constant);
    CHECK(slow->put(bytes.data(), static_cast<int32_t>(bytes.size())) && slow->flush());
    CHECK(slow->resume());
    std::size_t sounding = 0;
    for (int round = 0; round < 10; ++round)
        for (const int16_t sample : mix(mixer, 512))
            if (sample != 0) {
                CHECK(std::abs(sample - 8192) < 4);
                ++sounding;
            }
    CHECK(sounding == 2000 * 2);
    CHECK(slow->queued_bytes() == 0 && slow->available_bytes() == 0);
}

// A device the test plays by hand: queued buffers stay busy until played.
struct FakeDevice {
    bool fail_open{};
    bool fail_queue{};
    bool open{};
    uint32_t rate{};
    uint32_t frames{};
    std::vector<bool> busy;
    std::vector<uint32_t> order;               // indexes in the order queued
    std::deque<uint32_t> playing;              // queued and not yet played, oldest first
    std::vector<std::vector<int16_t>> written; // samples, in the order queued
    void (*pump)(void*){};
    void* argument{};
    bool pumping{};
    int closes{};

    oa::platform::sound_device::Hooks hooks() {
        oa::platform::sound_device::Hooks device{};
        device.context = this;
        device.open =
            [](void* c, uint32_t rate, uint32_t frames, uint32_t count, std::string& error) {
                auto& self = *static_cast<FakeDevice*>(c);
                if (self.fail_open) {
                    error = "no device";
                    return false;
                }
                self.open = true;
                self.rate = rate;
                self.frames = frames;
                self.busy.assign(count, false);
                return true;
            };
        device.buffer_free = [](void* c, uint32_t index) {
            return !static_cast<FakeDevice*>(c)->busy[index];
        };
        device.queue_buffer = [](void* c, uint32_t index, const int16_t* samples, uint32_t frames) {
            auto& self = *static_cast<FakeDevice*>(c);
            if (self.fail_queue)
                return false;
            self.busy[index] = true;
            self.order.push_back(index);
            self.playing.push_back(index);
            self.written.emplace_back(samples, samples + frames * 2);
            return true;
        };
        device.start_pump = [](void* c, void (*pump)(void*), void* argument) {
            auto& self = *static_cast<FakeDevice*>(c);
            self.pump = pump;
            self.argument = argument;
            self.pumping = true;
            return true;
        };
        device.stop_pump = [](void* c) { static_cast<FakeDevice*>(c)->pumping = false; };
        device.close = [](void* c) {
            auto& self = *static_cast<FakeDevice*>(c);
            self.open = false;
            ++self.closes;
        };
        device.lock = [](void*) { mixer_mutex.lock(); };
        device.unlock = [](void*) { mixer_mutex.unlock(); };
        device.name = "fake";
        return device;
    }

    // Plays out the oldest busy buffer and calls the pump, as the device's thread would.
    void play_one() {
        if (!playing.empty()) {
            busy[playing.front()] = false;
            playing.pop_front();
        }
        pump(argument);
    }
};

void buffered_output() {
    FakeDevice device;
    std::string error;
    {
        device.fail_open = true;
        BufferedOutput output(device.hooks());
        CHECK(!output.start(error) && error == "no device" && !output.started());
        CHECK(output.last_error() == "no device");
        device.fail_open = false;
    }
    BufferedOutput output(device.hooks());
    CHECK(output.driver_name().empty());
    // Start fills and queues the whole ring, then starts the pump.
    CHECK(output.start(error) && output.started() && device.open && device.pumping);
    CHECK(device.rate == mixer_output_rate && device.frames == output_buffer_frames);
    CHECK(device.order == std::vector<uint32_t>({0, 1, 2, 3}) && output.driver_name() == "fake");
    for (const auto& buffer : device.written)
        for (const int16_t sample : buffer)
            CHECK(sample == 0);
    // Nothing is free: a pump queues nothing.
    CHECK(output.pump() == 0);

    // A stream's samples reach the next buffers to be refilled, in ring order.
    auto stream = output.open_stream({SampleFormat::s16, 2, 44100}, nullptr, nullptr, error);
    std::vector<int16_t> ramp(2 * output_buffer_frames * 2);
    for (std::size_t i = 0; i < ramp.size(); ++i)
        ramp[i] = static_cast<int16_t>(i);
    const auto bytes = bytes_of(ramp);
    CHECK(stream->put(bytes.data(), static_cast<int32_t>(bytes.size())) && stream->resume());
    device.play_one();
    device.play_one();
    CHECK(device.order.size() == 6 && device.order[4] == 0 && device.order[5] == 1);
    for (std::size_t i = 0; i < ramp.size(); ++i) {
        const auto& buffer = device.written[4 + i / (2 * output_buffer_frames)];
        const auto want = static_cast<int16_t>(std::lround(ramp[i] * (32767.0F / 32768.0F)));
        CHECK(buffer[i % (2 * output_buffer_frames)] == want);
    }
    device.play_one();
    CHECK(device.order.back() == 2);
    for (const int16_t sample : device.written.back())
        CHECK(sample == 0);
    CHECK(output.buffers_queued() == 7);

    // A refused buffer is reported; starts are counted.
    device.fail_queue = true;
    device.play_one();
    CHECK(!output.last_error().empty());
    device.fail_queue = false;
    CHECK(output.start(error));
    output.stop();
    CHECK(output.started() && device.open);
    stream.reset();
    output.stop();
    CHECK(!output.started() && !device.open && !device.pumping && device.closes == 1);
}

#ifdef _WIN32
// The wave-out mixer on the system's device: a tone plays out and the
// stream drains.
int wave_out() {
    auto output = wave_out_sound_output_create();
    CHECK(output != nullptr);
    std::string error;
    if (!output->start(error)) {
        std::printf("wave-out: skipped, no device (%s)\n", error.c_str());
        return skipped;
    }
    CHECK(output->driver_name() == "waveout");
    auto stream = output->open_stream({SampleFormat::s16, 1, 22050}, nullptr, nullptr, error);
    CHECK(stream != nullptr);
    std::vector<int16_t> tone(22050 / 2);
    for (std::size_t i = 0; i < tone.size(); ++i)
        tone[i] = static_cast<int16_t>(8000.0 * std::sin(0.1 * static_cast<double>(i)));
    CHECK(stream->put(tone.data(), static_cast<int32_t>(tone.size() * 2)) && stream->flush());
    CHECK(stream->resume());
    const auto begin = std::chrono::steady_clock::now();
    while (stream->queued_bytes() > 0 || stream->available_bytes() > 0) {
        CHECK(std::chrono::steady_clock::now() - begin < std::chrono::seconds(10));
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    const double seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
    // A device that keeps time takes about half a second; one that does
    // not (a silent stand-in) takes less.
    std::printf("wave-out: half a second of tone drained in %.2f s\n", seconds);
    stream.reset();
    output->stop();
    CHECK(!output->started());
    return 0;
}
#endif

} // namespace

int main(int argc, char** argv) {
    if (argc > 1 && std::string_view(argv[1]) == "--wave-out") {
#ifdef _WIN32
        return wave_out();
#else
        std::puts("wave-out: skipped, not Windows");
        return skipped;
#endif
    }
    formats();
    conversion_and_gain();
    feeds();
    buffered_output();
    std::puts("sound output: ok");
    return 0;
}
