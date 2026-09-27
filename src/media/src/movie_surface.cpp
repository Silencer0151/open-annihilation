// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/media/movie_surface.hpp"

#include "oa/present/palette_tables.hpp"

#include <cstdint>

namespace oa::media::movie {

namespace {

constexpr int32_t reserved_system_colors = 10;
constexpr uint8_t palette_no_collapse = 0x04;

bool masks_are(uint32_t r, uint32_t g, uint32_t b, uint32_t er, uint32_t eg, uint32_t eb) noexcept {
    return r == er && g == eg && b == eb;
}

} // namespace

int32_t open_movie(
    MoviePlayback& movie,
    const char* path,
    int32_t sound,
    int32_t simulate_rate,
    int32_t (*simulate)(int32_t rate)
) noexcept {
    movie.stopped = 0;
    movie.last_full_frame = 0;
    if (simulate != nullptr && simulate_rate != 0) {
        simulate(simulate_rate);
    }
    if (movie.decoder.open == nullptr || movie.decoder.open(movie.decoder.user, path) == 0) {
        return 0;
    }
    if (movie.decoder.set_sound != nullptr) {
        movie.decoder.set_sound(movie.decoder.user, sound != 0 ? 1 : 0);
    }
    movie.owns_device = 0;
    return 1;
}

void close_movie(MoviePlayback& movie) noexcept {
    if (movie.decoder.close != nullptr) {
        movie.decoder.close(movie.decoder.user);
    }
    movie.owns_device = 0;
}

uint32_t movie_pixel_format(
    int32_t bits, uint32_t red_mask, uint32_t green_mask, uint32_t blue_mask
) noexcept {
    if (bits == 8) {
        return movie_format_8bit;
    }
    if (masks_are(red_mask, green_mask, blue_mask, 0xF800, 0x07E0, 0x001F)) {
        return movie_format_rgb565;
    }
    if (masks_are(red_mask, green_mask, blue_mask, 0xF800, 0x07C0, 0x003F)) {
        return movie_format_bgr565;
    }
    if (masks_are(red_mask, green_mask, blue_mask, 0x7C00, 0x03E0, 0x001F)) {
        return movie_format_rgb555;
    }
    if (masks_are(red_mask, green_mask, blue_mask, 0xFC00, 0x03E0, 0x001F)) {
        return movie_format_rgb655;
    }
    return 0;
}

void read_system_palette(MoviePlayback& movie, const Palette& system) noexcept {
    movie.palette = system;
    for (int32_t i = 0; i < OA_PALETTE_COLORS; ++i) {
        const bool reserved =
            i < reserved_system_colors || i >= OA_PALETTE_COLORS - reserved_system_colors;
        movie.palette.entries[i].flags = reserved ? 0 : palette_no_collapse;
    }
}

void copy_frame_palette(MoviePlayback& movie, const uint8_t* rgb) noexcept {
    for (int32_t i = 0; i < OA_PALETTE_COLORS; ++i) {
        movie.palette.entries[i].r = rgb[i * 3];
        movie.palette.entries[i].g = rgb[i * 3 + 1];
        movie.palette.entries[i].b = rgb[i * 3 + 2];
    }
    if (present::DisplayContext* display = present::display_context()) {
        present::apply_palette_entries(
            *display, movie.palette.entries, 0, OA_PALETTE_COLORS, display->device_palette
        );
    }
}

void clear_movie_surface(Surface* surface) noexcept {
    present::clear_surface(surface, 0);
}

void render_movie_frame(
    MoviePlayback& movie, const MovieInput& input, Surface& offscreen
) noexcept {
    if ((input.focused != nullptr && input.focused(input.user) == 0) || movie.stopped != 0) {
        return;
    }
    const MovieDecoder& d = movie.decoder;
    if (d.frame_palette != nullptr) {
        if (const uint8_t* rgb = d.frame_palette(d.user)) {
            copy_frame_palette(movie, rgb);
        }
    }
    present::set_active_surface(&offscreen);
    const int32_t height = d.height != nullptr ? d.height(d.user) : 0;
    if (d.decode_frame != nullptr) {
        d.decode_frame(
            d.user,
            offscreen.pixels,
            offscreen.pitch,
            0,
            static_cast<int32_t>(static_cast<uint32_t>(movie_canvas_height - height) >> 1)
        );
    }
    present::draw_frame();
    const int32_t index = d.frame_index != nullptr ? d.frame_index(d.user) : 0;
    if (d.full_frame_updated != nullptr && d.full_frame_updated(d.user) != 0) {
        movie.last_full_frame = index + 1;
    }
    const int32_t count = d.frame_count != nullptr ? d.frame_count(d.user) : 0;
    if (index == count - 1) {
        movie.stopped = 1;
    } else if (d.next_frame != nullptr) {
        d.next_frame(d.user);
    }
}

int32_t play_movie(MoviePlayback& movie, const MovieInput& input, Surface& offscreen) noexcept {
    int32_t skipped = 0;
    while (movie.stopped == 0) {
        const MovieEvent event = input.poll != nullptr ? input.poll(input.user) : MovieEvent::none;
        if (event == MovieEvent::none) {
            if (movie.decoder.wait == nullptr || movie.decoder.wait(movie.decoder.user) == 0) {
                render_movie_frame(movie, input, offscreen);
            }
            continue;
        }
        if (event == MovieEvent::quit_key) {
            movie.stopped = 1;
            if (input.request_quit != nullptr) {
                input.request_quit(input.user);
            }
            return 1;
        }
        if (event == MovieEvent::key_char) {
            movie.stopped = 1;
            skipped = 1;
        }
    }
    return skipped;
}

} // namespace oa::media::movie
