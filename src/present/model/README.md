# Model drawing

Draws 3DO models (units, features, debris, shatter fragments and projectiles)
the way the game does, into 8-bit palette-indexed surfaces, and carries those
draws onto the engine's RGB match frame. It is presentation only: it reads the
simulation's units and piece transforms and never changes what the simulation
holds.

## Entry points

Namespace `oa::present::model`, headers in `include/oa/present/model/`:

- `model_library.hpp`: texture archives, each model's primitives in draw order
  with their textures, and the display tables (alpha, shade, blue) the drawing
  reads.
- `mesh_raster.hpp`: scan conversion of textured 3DO quadrilaterals, with and
  without a depth plane.
- `model_draw.hpp`: units from their cached images and the pieces that move
  every frame, shadows, the build effect, carried units, features, debris,
  fragments and projectiles.
- `unit_supersampling.hpp`: enhanced anti-aliasing of units, drawn at several
  samples a pixel and reduced into the frame.
- `rgb_bridge.hpp`: the bridge between the 8-bit surfaces and the RGB frame.

## The RGB bridge

The game draws models in 8 bits and blends through palette tables, so a draw
needs the palette indices of what is already on screen. The match frame is
RGB, and sprites, particles and the terrain are drawn into it in full colour.
A bridge (`RgbBridge`) lays an 8-bit surface over a rectangle of the frame, at
a scale of frame pixels to 8-bit pixels:

- `bridge_begin` starts a frame's drawing over a rectangle of the frame.
- `bridge_open` captures the 32-pixel tiles a region overlaps: each 8-bit pixel
  starts as the palette index of the frame pixel it captures from (that colour's
  entry, or the nearest entry by squared distance for a colour outside the
  palette), and draws are clipped to the region.
- `bridge_end` writes every 8-bit pixel that a draw changed back to the frame
  through the palette, for the tiles captured since the last write-back.
  Pixels no draw changed keep their colour, so colours outside the palette
  survive.
- `bridge_open_sampled` and `bridge_end_sampled` do the same for a region
  drawn at several samples a pixel.

The bridge keeps a capture copy (`CaptureCopy`) from one capture to the next:
for each frame pixel it captures from, the colour it last mapped and that
colour's index. A capture compares the frame with the copy and maps again only
the pixels whose colour changed, whoever changed them: the bridge's own
write-backs, or sprites and particles drawn into the frame between two model
draws. A write-back at a scale of 1 over a rectangle inside the frame also
brings the copy up to date with the pixels it writes. Every capture therefore
gives the same indices as mapping every pixel afresh, and the frame comes out
byte for byte as it would without the copy.

The copy is kept across `bridge_begin` calls while the frame's size and row
length, the rectangle, the scale and the palette stay the same, so a frame
whose picture did not change under a tile maps nothing there. A change of any
of them forgets it.

Memory: besides the 8-bit surface and its captured indices (one byte each per
8-bit pixel), the copy holds four bytes (colour and index) for each distinct
frame pixel captured from, so never more than four bytes per frame pixel of the
rectangle.

## Tests

`tests/` holds one ctest per file (`model-render-*`). `rgb_bridge_test.cpp`
checks captures, write-backs, scaling and sampled regions, and holds the copy
to a bridge that maps every pixel afresh: over many captures, write-backs and
writes into the frame between them, at scales of 1, 2, 0.5, 0.75 and 1.5, with
the rectangle inside the frame or past its edges and with colours that more than
one entry holds, the indices captured and the frames written back are the same.
It also checks that unchanged pixels are not mapped again. The native checks
and the director render pin the frames the match draws through the bridge.

## Limitations

A capture still compares every frame pixel of each tile it opens with the copy,
since sprites and particles draw into the frame without telling the bridge
where.
