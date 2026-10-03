// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

/* Canonical 8-bit presentation records shared by every drawing system.
 *
 * Everything is drawn into palette-indexed byte buffers described by one
 * surface descriptor, sprites use a GAF-frame shaped header, and the display
 * owns a 256-entry palette. Pointer fields hold native pointers. Compiles as
 * C11 and C++20; C++ types live in namespace oa. */
#ifndef OA_PRESENT_SURFACE_H
#define OA_PRESENT_SURFACE_H

#include "oa/core/types.h"

#define OA_PALETTE_COLORS 256

/* Surface.flags bits. */
#define OA_SURFACE_FLAG_MEMORY 0x1u /* pixels live in memory, not a device surface */
/* No known use: init_surface and surface_from_sprite clear it, and nothing
   sets or tests it. */
#define OA_SURFACE_FLAG_CLEARED_ON_INIT 0x2u
/* The surface's draws are limited to the rows from Surface.band_first_row
   up to but not including Surface.band_end_row, so that a frame drawn band
   by band is the frame drawn whole. These draws honour the band: they work
   out what they draw from the clip alone and leave every other row as it
   is:
   - fill_polygon (and the range rings), fill_line and what draws through it
     (draw_clipped_line, draw_point, draw_rect_outline, outline_rect);
   - the sprite draws that place a sprite by the clip (draw_sprite,
     draw_sprite_blended, draw_sprite_blended_through, draw_sprite_lit,
     draw_sprite_shadow, draw_sprite_opaque, draw_sprite_gray,
     erase_sprite_dithered);
   - texture_quad (the model module's mesh_raster.hpp).
   The rectangle copies (copy_rect, copy_rect_keyed, blit_rect,
   blit_rect_keyed, remap_under_mask and the row decoders of rle.hpp) write
   exactly the rectangle they are given, which a caller drawing on a banded
   surface trims with trim_to_surface first. No other draw honours a band,
   and none other may be given a banded surface: the line remaps
   (remap_line, light_clipped_line, light_rect_edges), fill_rect and
   fill_clipped_rect, shade_rect_level, gray_rect, clear_dithered_rect,
   draw_text and the whole-surface and tile copies check this in debug
   builds. init_surface and surface_from_sprite clear it. */
#define OA_SURFACE_FLAG_BANDED 0x4u

/* Values init_surface and surface_from_sprite store in the two reserved
   words. */
#define OA_SURFACE_RESERVED_AFTER_PIXELS_INIT 10000
#define OA_SURFACE_RESERVED_BEFORE_ORIGIN_INIT (-1)

/* Sprite.encoding values. */
#define OA_SPRITE_RAW 0
#define OA_SPRITE_ROW_RLE 1

OA_CORE_BEGIN

/* One display palette slot: red, green, blue and a flags byte. */
typedef struct PaletteEntry {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t flags; /* zero in every loaded game palette */
} PaletteEntry;

typedef struct Palette {
    PaletteEntry entries[OA_PALETTE_COLORS];
} Palette;

OA_ASSERT_SIZE(PaletteEntry, 4);
OA_ASSERT_SIZE(Palette, 0x400);

/* 8-bit drawing target: back buffer, off-screen bitmap or a view of a
 * sprite. The clip rectangle is inclusive and bounds every clipped draw. */
typedef struct Surface {
    int32_t width;   /* pixels */
    int32_t height;  /* rows */
    int32_t pitch;   /* bytes per row */
    uint8_t* pixels; /* first row */
    /* No known use: set to OA_SURFACE_RESERVED_AFTER_PIXELS_INIT, and
       nothing reads it. */
    int32_t reserved_after_pixels;
    /* No known use: set to OA_SURFACE_RESERVED_BEFORE_ORIGIN_INIT, and
       nothing reads it. */
    int32_t reserved_before_origin;
    int16_t origin_x; /* hotspot subtracted from keyed blit positions */
    int16_t origin_y;
    Rect32 clip;    /* inclusive */
    uint32_t flags; /* OA_SURFACE_FLAG_* */
    /* The first row draws may change, with OA_SURFACE_FLAG_BANDED. */
    int32_t band_first_row;
    /* The row after the last one draws may change, with
       OA_SURFACE_FLAG_BANDED. */
    int32_t band_end_row;
} Surface;

/* In-memory sprite header (GAF frame layout). A sprite with a non-zero
 * child_count stores child Sprite pointers in `data` instead of pixels. */
typedef struct Sprite {
    uint16_t width;
    uint16_t height;
    int16_t origin_x; /* hotspot */
    int16_t origin_y;
    uint8_t key;      /* transparent index */
    uint8_t encoding; /* OA_SPRITE_* */
    uint8_t child_count;
    /* Non-zero on a child of a composite: draw_sprite and
       draw_sprite_opaque draw the child blended through the display alpha
       table. */
    uint8_t child_draw_mode;
    /* No known use: loaded from the GAF frame header as stored, and never
       read. */
    uint32_t reserved_after_child_draw_mode;
    void* data; /* pixels, row-RLE stream, or Sprite* array */
    void* aux;  /* optional second width*height plane */
} Sprite;

/* Platform presentation boundary. Every callback is
 * optional; `user` is passed back unchanged. */
typedef struct RenderSink {
    void* user;
    /* Called before the frame is composed. */
    void (*begin_frame)(void* user);
    /* Shows one finished 8-bit frame with its palette. */
    void (*present)(
        void* user,
        const uint8_t* pixels,
        int32_t pitch,
        int32_t width,
        int32_t height,
        const Palette* palette
    );
    /* Requests a display mode; returns non-zero on success. */
    int32_t (*set_display_mode)(void* user, int32_t width, int32_t height, int32_t bits);
} RenderSink;

OA_CORE_END

#endif
