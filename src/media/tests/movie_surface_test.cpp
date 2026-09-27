// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Movie playback against a scripted decoder and input double.

#include "oa/media/movie_surface.hpp"

#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

struct FakeMovie {
    int32_t frame = 0;
    int32_t frames = 3;
    int32_t decoded_y = -1;
    uint8_t rgb[OA_PALETTE_COLORS * 3]{};
    bool opened = false;
};

FakeMovie& fake(void* user) {
    return *static_cast<FakeMovie*>(user);
}

oa::media::movie::MovieDecoder fake_decoder(FakeMovie& movie) {
    oa::media::movie::MovieDecoder d;
    d.user = &movie;
    d.open = [](void* u, const char*) -> int32_t { return fake(u).opened = true; };
    d.wait = [](void*) -> int32_t { return 0; };
    d.decode_frame = [](void* u, uint8_t* pixels, int32_t pitch, int32_t, int32_t y) {
        fake(u).decoded_y = y;
        pixels[static_cast<std::ptrdiff_t>(y) * pitch] = static_cast<uint8_t>(10 + fake(u).frame);
    };
    d.next_frame = [](void* u) { ++fake(u).frame; };
    d.frame_palette = [](void* u) -> const uint8_t* {
        return fake(u).frame == 0 ? fake(u).rgb : nullptr;
    };
    d.frame_index = [](void* u) -> int32_t { return fake(u).frame; };
    d.frame_count = [](void* u) -> int32_t { return fake(u).frames; };
    d.height = [](void*) -> int32_t { return 200; };
    return d;
}

int presented = 0;

} // namespace

int main() {
    oa::present::DisplayContext display;
    display.sink.present =
        [](void*, const uint8_t*, int32_t, int32_t, int32_t, const oa::Palette*) { ++presented; };
    oa::present::bind_display(&display);
    oa::present::init_display(display, false);
    oa::present::OffscreenSurface offscreen;
    offscreen.width = 640;
    offscreen.height = 480;
    oa::present::create_offscreen_surface(offscreen);

    FakeMovie movie;
    movie.rgb[5 * 3] = 99;
    oa::media::movie::MoviePlayback playback;
    playback.decoder = fake_decoder(movie);
    check(
        oa::media::movie::open_movie(playback, "intro.smk", 1, 0, nullptr) == 1 && movie.opened,
        "movie opens"
    );
    oa::media::movie::MovieInput input;
    check(
        oa::media::movie::play_movie(playback, input, offscreen.surface) == 0,
        "plays to the end without a skip"
    );
    check(presented == 3 && playback.stopped == 1, "every frame presented once");
    check(
        movie.decoded_y == 140 && offscreen.pixels[140 * 640] == 12,
        "frames centred on the 480-line canvas"
    );
    check(
        playback.palette.entries[5].r == 99 && display.device_palette.entries[5].r == 99,
        "frame palette applied"
    );
    check(
        oa::media::movie::movie_pixel_format(16, 0xF800, 0x07E0, 0x001F) ==
                oa::media::movie::movie_format_rgb565 &&
            oa::media::movie::movie_pixel_format(16, 1, 2, 3) == 0 &&
            oa::media::movie::movie_pixel_format(8, 0, 0, 0) == 0,
        "pixel format flags"
    );

    oa::media::movie::MoviePlayback skipped;
    FakeMovie second;
    skipped.decoder = fake_decoder(second);
    oa::media::movie::MovieInput keys;
    keys.poll = [](void*) { return oa::media::movie::MovieEvent::key_char; };
    check(
        oa::media::movie::play_movie(skipped, keys, offscreen.surface) == 1 && second.frame == 0,
        "a key skips the movie"
    );
    oa::present::bind_display(nullptr);
    if (failures != 0) {
        return 1;
    }
    std::puts("movie surface tests passed");
    return 0;
}
