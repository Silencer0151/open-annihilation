# Terrain viewport renderer

This SDL-neutral component renders a camera crop from the TNT 32x32 tile
mosaic into RGB. It uses the game palette supplied by the caller and does not
define camera input, scrolling, zoom, fog, or presentation.

The game's terrain view divides the camera pixel origin by 32, carries the
remainders into clipped edge tiles, and then copies complete interior tiles.
Direct per-pixel selection in `render_viewport` has the same visible result
while keeping those coordinate units. The radar picture uses the same
`tile_index * 0x400 + y * 32 + x` mosaic addressing.

`draw_terrain_view` is the 8-bit form of the terrain view itself: it draws the
tiles cut by the view's edges through `draw_sprite_opaque`, clipped to the
target, and the whole tiles between them through `blit_tile`, reading the
camera, view size and battlefield rectangle from the Game block. Its test
compares the installation's maps with `render_viewport` through a palette
whose red channel is the index.

Out-of-map crops are rejected; the camera limits keep a crop on the map.
Output is capped at 64 million pixels.

`BattlefieldViewport` carries the camera crop and destination clip together.
`render_battlefield_viewport` copies the TNT mosaic directly into that clip;
the TNT already stores the battlefield artwork in screen-map pixel space, the
space the radar sampler reads as well. The default destination is
`(128,32)`, set when the game screen is laid out. That layout reserves the
128-pixel command panel, 32-pixel top bar, and 32-pixel bottom strip,
producing a `512x416` battlefield on the `640x480` surface.
`unit_projection_for_viewport`, `map_pixel_to_screen`, and
`screen_to_map_pixel` use the same origin and camera values. A zero-height
unit projects to `(map_x-camera_x+128, map_y-camera_y+32)`; unit height alone
supplies the additional `-height/2` vertical displacement.

The viewport-derived unit projection also carries the battlefield raster clip.
Solid and textured primitives are clipped to that rectangle, matching the
game's surface clip and preventing units at the camera edge from drawing over
the command panel, top bar, or bottom strip. When the game draws units it
leaves the surface clip's inclusive right column and bottom row undrawn, so
viewport-derived unit clips keep that one-pixel-short right and bottom edge
while terrain and hit testing keep the full inclusive extent.

## Drawing in bands

The match's terrain fill (`fill_scaled_viewport`, the destination-sized
sample of the mosaic at any zoom) and its fog (`draw_fog_grid`) take an
optional [job pool](../../platform/job-pool/README.md) and split their rows
into bands by the data: the fill by `terrain_band_rows` (32) destination
rows, each band finding its first map row and fraction from its first row's
index exactly as the row-by-row step reaches them; the fog by one row of its
grid, which draws only the surface rows its map rows land on. Bands write
nothing another reads, so a pool of any size draws the bytes the calling
thread draws alone, and with no pool the bands run in order on the calling
thread. A missing tile stops the band that finds it; the fill then reports
it, and which rows were written is not specified.

`world-draw-bands` fills a random map at zoom 1, 1.37, 0.6, 0.75 and 2, from
the corner, the middle and past the map's edges, and draws a random fog grid
with random tile art at four zooms, plain and dithered, without a pool and
on pools of 2, 3, 4 and 8 threads: every pool gives the same bytes, each
filled pixel is the map pixel its zoom names, no band writes past its row,
and a missing tile is reported from a pool.

## The area pass

`area_filter_rgb24` (`scene_filter.hpp`) reduces a scene drawn finer than
the screen to the screen's picture: each picture pixel is the average of the
scene under its footprint, each scene pixel weighted by the area of it the
footprint covers. The scale `m`, screen pixels per scene pixel, is 16.16
fixed point from one half to one; the scene starts at the picture's corner,
and picture column `x` covers scene columns `[x / m, (x + 1) / m)`, at most
three of them, and rows alike. Nothing calls it yet: the zoomed-out
battlefield is drawn as before, terrain averaged over whole map pixels and
units point-sampled.

- **Exact weights.** Measured in 16.16 screen units, a picture pixel spans
  65536 and a scene pixel `m`'s 16.16 value, so the part of a footprint a
  scene pixel covers is a whole number of units: the weights of every
  footprint sum to exactly 65536, and each scene pixel counts, over all the
  footprints it falls in, for exactly its length. `plan_area_filter` builds
  them once for a scale and a picture size (an `AreaPlan`), and every frame
  at that scale and size uses them. Only `plan_area_filter` fills a plan, so
  its taps always lie inside the scene it names; rebuilding one at a size no
  larger keeps its storage.
- **Rounding.** The pass is separable: each scene row under a footprint is
  averaged across its columns and kept at 16 bits, eight below the channel's
  level, rounded to the nearest with halves up; those are averaged across
  the rows and rounded once to the 8-bit level, halves up. A level is
  within half a level, and half of 1/256 of one, of the exact average.
- **At one half** every weight is one half, so the picture is the 2x2 box
  `(a + b + c + d + 2) / 4`, the rounding the terrain's box filter gives its
  2x2 footprints at zoom 0.5. Comparing the two byte for byte needs the
  application's terrain filter, and comes with the change that wires the
  pass in. **At one** the picture is a copy of the scene.
- **Cost.** Each band averages every scene row it needs across the columns
  once, then sums those row averages down each picture row's footprint. It
  works in strips of 256 columns, whose row averages, four scene rows of
  them at most, stay on the stack (6 KB), so a frame allocates nothing. A
  1664x952 picture takes 28.5 million multiply-adds at one half and about
  43 million at most, just above one half, where a footprint covers three
  scene pixels a side.
- **Integers only**, in plain C++ with no float and no vector instructions
  of its own, so the bytes are the same on every platform and build type.
- **Bands.** A picture pixel depends only on the plan and the scene, so any
  split of the rows gives the same bytes. `area_filter_rgb24` filters bands
  of `area_band_rows` (32) rows on an optional job pool;
  `area_filter_rgb24_rows` filters any range of rows.
- **Bounds.** A plan takes pictures of 1 to `area_picture_edge_limit` (8192)
  pixels a side; a frame is refused, with nothing written, when it has no
  plan, when the picture's size is not the plan's, when the scene is smaller
  than the plan reads, when a stride is below its width or above
  `area_stride_limit`, or when storage is missing. The pass reads only the
  scene's covered corner and writes only the picture's rows, never the
  padding past them.

`area_sample_reference` is the exact average in double precision, from the
footprint's geometry rather than a plan; tests hold the pass to it.

`world-scene-filter` checks the weights; that the pass gives the bytes of a
straightforward implementation intersecting every footprint with every scene
pixel, at scales across the range; that it stays within half a level and the
row averages' rounding of `area_sample_reference`; the box at one half and
the copy at one; that a line one scene pixel wide keeps its intensity, within
the rounding of the two pixels it touches, at 40 places and six scales from
0.5 to 0.9; that bands of 1, 7, 33 and 350 rows and pools of 1 to 8 threads
give the same bytes and write nothing outside their rows; that malformed
plans and frames are refused, a plan moved from among them; that rebuilding
a plan no larger allocates nothing; and a pinned FNV-1a digest of seeded
scenes. It logs the time of a 1664x952 picture, the battlefield of a
1920x1080 window, from a scene twice its size at one half, just above one
half and at two thirds, the last two with footprints of three scene pixels
a side, for information.

## Draw order

`plan_battlefield_draws` (`world_draw_order.hpp`) orders a frame's features
and units the way the battlefield draws them, far to near in rows one plot
deep. Features lower than `standing_feature_min_height` lie under every unit
and draw first, row by row. Then each row draws its ground units, in the
order given, followed by the standing features whose origin plot is in that
row, left to right: a tree covers the units in its own row and the rows
behind it, and units in the rows in front of it cover the tree. A unit's row
is the camera's plot row plus the whole rows between the camera and the
unit, truncated toward zero, so it follows the camera's position within a
plot. Units not on the ground (in the air, or carried) draw in a last pass,
after the projectiles and explosions. Its test pins each of these rules.

## Solid 3DO primitives

`render_colored_instance` draws the solid-color path. It consumes the
transformed vertices from `model-runtime`, visits visible pieces in reverse
order, projects signed 16.16 coordinates with the orthographic formula, and
fills convex scanline spans with the primitive color's low palette byte. The
battlefield destination origin is `(128,32)` after the command panel and top
bar; camera inputs remain map pixels and are shifted to 16.16 by the
projection.

Textured four-vertex primitives use the textured quad rasterizer. Static,
raw, single-frame GAF textures are resolved by name and sampled with the game
palette. The rasterizer covers clipping, rotation and both windings, and
samples textures of every width alike, the 8, 16, 32, 64 and 128-pixel
widths included. Bounds checks clamp malformed out-of-range UVs to the
texture's edges.

`UnitMaterialState` supplies animation cursors keyed by model object and
primitive, plus the draw call's force-first flag and owner's team-frame byte.
The per-primitive cursor matches the runtime record the model loader builds;
an absent cursor is its initial frame-zero snapshot, and the renderer does
not advance it or derive it from a clock. In `select_material_frame` team
materials use the owner byte even when force-first is set; animated materials
use frame zero only when force-first is set; fixed materials always use frame
zero. Out-of-range team bytes and unsupported indexed frames select null and
remain deferred.

Texture GAF filenames use AssetStore's loose-file, mount, and stored HPI
directory order. A first sequence-name match is kept even when unsupported,
so a later archive cannot incorrectly replace it.
