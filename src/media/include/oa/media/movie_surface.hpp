// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// In-game SMK movie playback onto the 8-bit presentation surfaces. The
// decoder sits behind MovieDecoder and the window's input behind MovieInput.

#include "oa/present/display.hpp"
#include "oa/present/surface.h"

#include <cstdint>

namespace oa::media::movie {

inline constexpr int32_t movie_canvas_height = 480;

// SMK decoder boundary: opening, pacing, decoding and palettes of one movie.
struct MovieDecoder {
    void* user = nullptr;
    // Opens a movie; returns non-zero on success.
    int32_t (*open)(void* user, const char* path) = nullptr;
    void (*close)(void* user) = nullptr;
    // Enables or disables the movie's sound track.
    void (*set_sound)(void* user, int32_t enabled) = nullptr;
    // Non-zero while the next frame is not due yet.
    int32_t (*wait)(void* user) = nullptr;
    // Decodes the current frame into 8-bit pixels at (x, y).
    void (*decode_frame)(void* user, uint8_t* pixels, int32_t pitch, int32_t x, int32_t y) =
        nullptr;
    void (*next_frame)(void* user) = nullptr;
    // Current frame's palette as 256 RGB triples, or null when unchanged.
    const uint8_t* (*frame_palette)(void* user) = nullptr;
    // Non-zero when the last decode refreshed the whole frame.
    int32_t (*full_frame_updated)(void* user) = nullptr;
    int32_t (*frame_index)(void* user) = nullptr;
    int32_t (*frame_count)(void* user) = nullptr;
    int32_t (*width)(void* user) = nullptr;
    int32_t (*height)(void* user) = nullptr;
};

enum class MovieEvent : uint8_t {
    none,
    other,    // any other input; playback continues
    key_char, // a typed character: stop playback
    quit_key, // Alt+F4: stop playback and quit the game
};

struct MovieInput {
    void* user = nullptr;
    // Returns the next pending event, or none.
    MovieEvent (*poll)(void* user) = nullptr;
    // Non-zero while the game window has input focus.
    int32_t (*focused)(void* user) = nullptr;
    // Requests application shutdown.
    void (*request_quit)(void* user) = nullptr;
};

// Decoder colour-remap flags of a display pixel format (0 = 8-bit).
enum MovieFormat : uint32_t {
    movie_format_8bit = 0,
    movie_format_rgb555 = 0x80000000u,
    movie_format_bgr565 = 0xA0000000u, // 0xF800/0x07C0/0x003F masks
    movie_format_rgb565 = 0xC0000000u,
    movie_format_rgb655 = 0xE0000000u, // 0xFC00/0x03E0/0x001F masks
};

// One movie's playback state.
struct MoviePlayback {
    MovieDecoder decoder{};
    int32_t last_full_frame = 0; // one past the last full-frame update
    int32_t stopped = 0;         // nonzero once the last frame shows or a key stops playback
    Palette palette{};
    uint32_t format = 0;     // a MovieFormat value; stays movie_format_8bit
    int32_t owns_device = 0; // cleared by open_movie() and close_movie(); never read
};

/// Opens a movie and sets its sound on or off.
///
/// Resets the stopped flag and full-frame counter first.
///
/// @param[in,out] movie Playback record whose decoder opens the file.
/// @param path Movie file path.
/// @param sound Nonzero to play the movie's sound track.
/// @param simulate_rate Rate passed to `simulate` before opening; 0 skips the call.
/// @param simulate Optional pacing hook run once before opening; may be null.
/// @return 1 when the movie opened, 0 when it cannot be opened.
int32_t open_movie(
    MoviePlayback& movie,
    const char* path,
    int32_t sound,
    int32_t simulate_rate,
    int32_t (*simulate)(int32_t rate)
) noexcept;

/// Closes the movie.
///
/// @param[in,out] movie Playback record whose decoder closes.
void close_movie(MoviePlayback& movie) noexcept;

/// Maps a display pixel format to the decoder's colour-remap flags.
///
/// @param bits Bits per pixel of the display.
/// @param red_mask Red channel mask.
/// @param green_mask Green channel mask.
/// @param blue_mask Blue channel mask.
/// @return A MovieFormat value; 0 for 8-bit and for unsupported formats.
[[nodiscard]] uint32_t movie_pixel_format(
    int32_t bits, uint32_t red_mask, uint32_t green_mask, uint32_t blue_mask
) noexcept;

/// Seeds the movie palette from the system palette.
///
/// The 236 non-reserved entries are marked no-collapse; the ten reserved
/// colours at each end get no flags.
///
/// @param[out] movie Playback record whose palette is replaced.
/// @param system System palette to copy.
void read_system_palette(MoviePlayback& movie, const Palette& system) noexcept;

/// Loads the frame's RGB palette into the movie palette and the display.
///
/// @param[in,out] movie Playback record whose palette colours are replaced.
/// @param rgb 256 RGB triples (768 bytes).
void copy_frame_palette(MoviePlayback& movie, const uint8_t* rgb) noexcept;

/// Clears the playback surface to colour 0.
///
/// @param[out] surface Surface to clear.
void clear_movie_surface(Surface* surface) noexcept;

/// Decodes and presents one frame onto the off-screen surface.
///
/// The frame is vertically centred on a 480-line canvas. Nothing happens
/// while the window lacks focus or the movie has stopped; the movie stops
/// after its last frame.
///
/// @param[in,out] movie Playback record.
/// @param input Window input boundary, used for the focus test.
/// @param[in,out] offscreen 8-bit surface the frame is decoded into and presented from.
void render_movie_frame(MoviePlayback& movie, const MovieInput& input, Surface& offscreen) noexcept;

/// Plays until the last frame or a key or quit event.
///
/// A quit key also requests application shutdown.
///
/// @param[in,out] movie Playback record.
/// @param input Window input boundary polled between frames.
/// @param[in,out] offscreen 8-bit surface frames are decoded into.
/// @return Nonzero when the player skipped the movie or quit (the caller
///         clears its skip flag), 0 when it played to the end.
int32_t play_movie(MoviePlayback& movie, const MovieInput& input, Surface& offscreen) noexcept;

} // namespace oa::media::movie
