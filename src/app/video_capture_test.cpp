// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The video capture's pacing and encoder settings: the frames due at each
// point of the sound device's clock, the files a capture writes, and the
// ffmpeg arguments that encode the frames and join them to the mix.
#include "oa/app/video_capture.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char* expression, const char* file, int line) {
    if (!condition) {
        std::fprintf(stderr, "%s:%d: check failed: %s\n", file, line, expression);
        ++failures;
    }
}

#define CHECK(condition) check((condition), #condition, __FILE__, __LINE__)

using namespace oa::app;

// Sample frames of one video frame: 48000 / 30.
constexpr uint64_t samples_per_frame = 1600;

// The value that follows `option` in `arguments`, or "" when it has none.
std::string after(const std::vector<std::string>& arguments, const std::string& option) {
    const auto found = std::find(arguments.begin(), arguments.end(), option);
    return found == arguments.end() || found + 1 == arguments.end() ? std::string() : *(found + 1);
}

void test_frames_due() {
    // Frame 0 shows the screen from the start; frame n from n / 30 seconds.
    CHECK(capture_frames_due(0) == 1);
    CHECK(capture_frames_due(samples_per_frame - 1) == 1);
    CHECK(capture_frames_due(samples_per_frame) == 2);
    CHECK(capture_frames_due(capture_sample_rate) == capture_frame_rate + 1);
    // An hour of sound is 108000 frames on, without drift.
    const uint64_t hour = uint64_t{capture_sample_rate} * 3600;
    CHECK(capture_frames_due(hour) == capture_frame_rate * 3600 + 1);
}

void test_files() {
    const auto files = capture_files(std::filesystem::path("videos") / "showcase.mp4");
    CHECK(files.video == std::filesystem::path("videos") / "showcase.mp4");
    CHECK(files.frames == std::filesystem::path("videos") / "showcase.capture-frames.mp4");
    CHECK(files.mix == std::filesystem::path("videos") / "showcase.capture-mix.f32");
}

void test_frame_encoder() {
    const auto files = capture_files("showcase.mp4");
    const auto arguments = frame_encoder_arguments(files, 1280, 1024);
    CHECK(!arguments.empty() && arguments.front() == capture_encoder);
    // Raw RGB frames of the window's size on standard input, 30 a second.
    CHECK(after(arguments, "-f") == "rawvideo");
    CHECK(after(arguments, "-pix_fmt") == "rgb24");
    CHECK(after(arguments, "-video_size") == "1280x1024");
    CHECK(after(arguments, "-framerate") == "30");
    CHECK(after(arguments, "-i") == "-");
    // H.264 High profile, 4:2:0 in BT.709, constant quality 18, two B-frames
    // and closed groups of 15 frames.
    CHECK(after(arguments, "-c:v") == "libx264");
    CHECK(after(arguments, "-profile:v") == "high");
    CHECK(after(arguments, "-crf") == "18");
    CHECK(after(arguments, "-vf").find("format=yuv420p") != std::string::npos);
    CHECK(after(arguments, "-vf").find("out_color_matrix=bt709") != std::string::npos);
    CHECK(after(arguments, "-vf").find("color_primaries=bt709") != std::string::npos);
    CHECK(after(arguments, "-vf").find("color_trc=bt709") != std::string::npos);
    CHECK(after(arguments, "-bf") == "2");
    CHECK(after(arguments, "-g") == "15");
    CHECK(after(arguments, "-flags") == "+cgop");
    CHECK(arguments.back() == "showcase.capture-frames.mp4");
}

void test_join() {
    const auto files = capture_files("showcase.mp4");
    const auto arguments = join_arguments(files, 90);
    CHECK(!arguments.empty() && arguments.front() == capture_encoder);
    // The frames as encoded, and the mix as SDL's disk driver writes it.
    CHECK(after(arguments, "-i") == "showcase.capture-frames.mp4");
    CHECK(after(arguments, "-f") == "f32le");
    CHECK(after(arguments, "-ac") == "2");
    CHECK(
        std::find(arguments.begin(), arguments.end(), "showcase.capture-mix.f32") != arguments.end()
    );
    CHECK(after(arguments, "-c:v") == "copy");
    // AAC at 48 kHz and 384 kbit/s, cut to the 90 frames' three seconds.
    CHECK(after(arguments, "-c:a") == "aac");
    CHECK(after(arguments, "-b:a") == "384k");
    CHECK(after(arguments, "-ar") == "48000");
    CHECK(after(arguments, "-t") == "3.000000");
    CHECK(after(arguments, "-movflags") == "+faststart");
    CHECK(arguments.back() == "showcase.mp4");
}

} // namespace

int main() {
    test_frames_due();
    test_files();
    test_frame_encoder();
    test_join();
    if (failures != 0)
        std::fprintf(stderr, "%d video capture checks failed\n", failures);
    return failures == 0 ? 0 : 1;
}
