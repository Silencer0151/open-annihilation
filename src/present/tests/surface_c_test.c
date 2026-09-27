// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

/* Compiled as C11: the canonical presentation header must stay C-compatible. */
#include "oa/present/surface.h"

#include <stdio.h>

int main(void) {
    Palette palette;
    Surface surface;
    Sprite sprite;
    RenderSink sink = {0};
    int failures = 0;
    palette.entries[255].r = 1;
    surface.clip.x2 = 0;
    sprite.key = 0;
    if (sizeof(palette) != 0x400 || sizeof(PaletteEntry) != 4) {
        fprintf(stderr, "palette size\n");
        ++failures;
    }
    if (sizeof(Rect32) != 16 || sink.present != 0 || surface.clip.x2 != sprite.key) {
        ++failures;
    }
    return failures == 0 ? 0 : 1;
}
