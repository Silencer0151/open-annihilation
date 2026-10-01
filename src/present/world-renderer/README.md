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
