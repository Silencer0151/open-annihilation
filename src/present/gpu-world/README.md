# Card-drawn world

Groundwork for the tier in which the graphics card draws the battlefield
itself: the card-ready forms of the game's assets, built from the same
decoded data the processor draws from today. Each part is a library of its
own with its own test, so that the parts land and build separately. Nothing
in this module touches SDL or a graphics API, and nothing draws from these
records yet: the standard tier is unchanged, and no part of this module runs
unless a later change wires it in.

## Sprite pages

`oa::present::gpu_world::SpritePages`
(`include/oa/present/gpu_world/sprite_pages.hpp`, library
`oa-present-gpu-world-sprite-pages`) holds GAF sprite frames as texels the
card can draw: trees, wrecks and other features, explosions, smoke, flames,
plasma and the sprite shadows. A frame comes in as the GAF reader renders it
for the match today (`oa::formats::gaf::RenderedFrame`: one palette index and
one coverage byte a pixel, with the hotspot), is decoded once into RGBA8
texels and placed on a page; the next request for the same frame is answered
from the page.

**What a request gives back.** `frame(frame_id, mode, source)` returns a
`FrameRecord`: the page, the frame's rectangle of texels on it, the hotspot
as the GAF frame carries it, and the draw mode. `frame_id` is the caller's
own number for the frame, the same every time for the same frame; the pages
never read the source again once a frame is held, so a frame whose pixels
change needs `forget()` first. `find()` answers without a source, and
`holds()` asks without counting a use.

**Texels.** Each texel is four bytes, red, green, blue and alpha, rows top
to bottom with no padding. A covered pixel takes its palette entry's colour
through the display gamma, each channel the truncated channel times gamma,
clamped at 255, as the device palette has it; every other texel, the
two-texel gutter around the frame included, is zero in every byte. Colours
are premultiplied by the alpha, so the card draws every page with the
one-minus-source-alpha blend and may filter the texels without dark fringes.
What is drawn follows the frame's coverage exactly as today's drawers do: a
raw frame's pixels equal to the transparent index are not drawn, while a
row-RLE literal or run equal to it is drawn in that colour. A changed palette
or gamma means new texels: `set_palette` with a value that differs empties
the pages and starts a new `palette_generation()`.

**Draw modes.** The mode is part of the frame's identity on the pages, since
it decides the texels:

| Mode | Today | On the pages |
| --- | --- | --- |
| `opaque` | the keyed copy: a covered pixel replaces the pixel beneath | alpha 255 |
| `greyed` | the fog's gray: the colour's index through the gray table | the gray table's entry for each covered pixel, alpha 255; needs `set_gray_table` |

Translucency is not a mode: a translucent feature or shadow (`ANIM_TRANS`,
`SHAD_TRANS`, the feature shadow sprites) is the opaque cell drawn with a
vertex alpha of one half under the premultiplied alpha blend, so it costs
no second copy of the frame, and a greyed frame can be drawn translucent
the same way.

**Pages and cells.** A page is square, a power of two from `min_page_size`
(64) to `max_page_size` (2048) texels a side; the limits name the ordinary
side (`default_page_size`, 1024) and the largest side a frame too big for
an ordinary page may have of its own. Frames are packed on shelves in
cells: a cell is the frame with `frame_gutter` (2) transparent texels on
each side, its width and height rounded up to a multiple of
`cell_alignment` (2), at a corner that is a multiple of it, and the frame's
rectangle begins two texels inside the cell. That alignment is the packing
contract: a level 1 of the page computed with the exact 2x2 box holds each
frame's texels within its own cell with a one-texel gutter around them. A
frame whose cell exceeds the largest page is refused as `too_large`. A
shelf is a row of cells of one height, and a freed cell is taken again by a
later frame that fits it. `pages()` lists
every page by the index the records carry; a released page keeps its index
with a size of 0 until a new page takes it. Each page counts a `revision`
that grows whenever it is made, written or released, and keeps a `dirty`
rectangle of the texels written since `clear_dirty`, so that an upload
sends what changed.

**Memory and eviction.** `Limits::memory_limit` (`default_memory_limit`,
32 MiB) bounds the texel bytes of the pages alive. A frame that finds no
room on a page gets a new page while the limit allows one; otherwise the
least recently used frames are evicted one by one, each request and each
`find` counting as a use, until a freed slot or a released page makes room.
A page whose last frame goes is released at once. A frame whose own page
could never fit the limit is refused as `no_room` before anything is
evicted for it. `memory()` reports the page bytes, the frame bytes as whole
cells, the limit and the counts; `statistics()` counts hits, decodes,
evictions and refusals.

**Malformed frames.** A frame with a width or height of 0 is `empty`; one
whose pixel or coverage count differs from its size is `malformed` and
nothing is read from it. Today's blitter draws such a frame as far as its
bytes reach; the pages refuse it, since the GAF reader never produces one.
Frames whose composite children need the special blending that the reader
does not render fail in the reader, as they do for the match today, and
never reach the pages.

**Threads.** One set of pages belongs to one thread at a time; nothing
inside is locked.

### Tests

`present-gpu-world-sprite-pages` (`tests/sprite_pages_test.cpp`): the
limits, a frame's texels through the palette against the display's own
device palette at a gamma other than 1, the two modes, the row-RLE edge
cases through the GAF reader (skips, literals equal to the transparent
index, an empty row, a skip and a run past the width, composite children at
their hotspots), malformed frames and streams refused, packing without
overlap in aligned cells on power-of-two pages, eviction in
least-recently-used order under the memory limit, cell reuse and shelf
release, the palette and gray-table changes with the same tables again
changing nothing, the dirty rectangles and revisions.
`present-gpu-world-sprite-pages-data` places every frame of every GAF file
of the installed game and compares the texels with the GAF reader's pixels
through the palette and with `draw_sprite`, today's sprite drawer, holding
the page bytes under the limit throughout.

### Limitations

Not wired into any drawing. The pages hold level 0 only: level 1, the exact
half-size box the design draws sprites from when zoomed out, follows, and
the cells are aligned for it. The gray table is the caller's, as the
palette is; the pages build neither.
