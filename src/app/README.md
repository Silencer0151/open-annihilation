# src/app

The game application: the native runtime that hosts the frontend and the
match, and, in `netgame/`, network play. The `oa-game` target builds it as `open-annihilation`
(`open-annihilation.exe` on Windows).

## The built game

On macOS the game is the application bundle `open-annihilation.app`, made
from the template `Info.plist.in`: the Dock, the application switcher and
the menu bar show its name, Open Annihilation, and it carries the project's
version and the oldest macOS release its code runs on. `run.sh` starts the
executable inside it, `Contents/MacOS/open-annihilation`, in place. CMake
names a bundle after its executable, so the bundle keeps the executable's
name (`cmake/OaGameBundle.cmake`).

The files that travel with the game (`LICENSE`, `ATTRIBUTIONS.md`,
`licenses/` and what the extensions add through their `GAME_FILES`)
go in the folder `SDL_GetBasePath()` names at run time: the bundle's
`Contents/Resources` on macOS, the executable's folder elsewhere. The
target's `OA_GAME_FILES_DIR` property names it for build commands. The
out-of-memory report goes to `ErrorLog.txt` beside the application: beside
the executable, or beside the bundle, never inside it.

The game carries the Open Annihilation icon in three forms, which
`tools/make_icons.py` makes on macOS from `branding/open-annihilation-icon.png`
and which are committed beside it: the bundle's `open-annihilation.icns`,
which its Info.plist names; `open-annihilation.ico`, which
`open-annihilation.rc.in` compiles into `open-annihilation.exe` with the
version information that names it Open Annihilation; and
`open-annihilation-256.png`, which the build embeds (`window_icon.hpp`) and
start-up gives the window with `SDL_SetWindowIcon`; a failure there is
reported, and the game starts without it. The branding is not under the
project's licence (`COPYRIGHT`). `branding-icons` checks the icons' sizes,
and `app-window-icon` that the embedded one decodes.

Start-up makes the window's renderer by walking SDL's render drivers one
at a time, in SDL's own order (`render_host.hpp`): the first that starts is
kept, which with every driver working is the one SDL's own choice makes,
and each driver that refuses is logged with SDL's reason, as on the dummy
video driver: `open-annihilation: graphics: renderer vulkan refused: No
dynamic Vulkan support in current SDL video driver (dummy)`. Before SDL's
software renderer, the last of the walk, the framebuffer hint is set when
a driver before it refused: `0` where the window has a framebuffer of its
own (the windows, x11, dummy and offscreen video drivers), so that
software presents through it with no graphics driver, and otherwise the
hardware drivers of SDL's order for software to present through, or the
first of them alone before SDL 3.4. A hint that is not taken, as when the
player's own `SDL_FRAMEBUFFER_ACCELERATION` takes priority, is logged and
the walk goes on. Under `SDL_RENDER_DRIVER` the start is SDL's own call,
which tries only the drivers the variable names, with no walk and no hint,
and a failure ends the run as it always has. When nothing starts, the run
ends with `SDL_CreateRenderer:` and the last refusal's reason.
`app-render-host` checks the walk, and `native-renderer-walk` its log on
the dummy video driver.

While the game runs, a failed SDL call of the standard tier's presenting
throws `PresentError` (`scaled_world.hpp`), which `Runtime::render` catches
by its type alone, so that an error from a hook it reaches keeps its own
path; the display sink and `apply_output_mode`'s SDL calls catch it too,
while the layout and the pointer's known place change at once. The
renderer is then made again (`Runtime::rebuild_renderer`,
`RendererHost::rebuild`): every texture the game made is forgotten, the
drivers after the one that failed in SDL's order are tried, under
`SDL_RENDER_DRIVER` only those its list names after it, then SDL's
software renderer, with the framebuffer hint set before it; the screen is
laid out again, the pointer kept on the window as before, and a match's
message log says so. A rebuild waits for the next `render()`, never a hook
or a drain of events, and the failed frame is not shown. The run ends only
when no driver starts, or when SDL's software renderer that a rebuild made
fails before it presents a frame. The render events reach the event
dispatch, the loading pump, the movies' hook and `drain_input`
(`take_render_event`; before the runtime exists, `RendererHost::take_event`
and `service`): a reset device forgets every texture, each made again from
its buffer at the next frame, and the third reset within a minute, or a
lost device, makes the renderer again. A device that says it is lost, as
one does on some renderers while another program holds the screen
(`render_probe::device_state`), is waited for: until its render targets
are reset nothing it fails makes a rebuild and nothing is read back. A
present SDL refuses is the game's own fault: the render target goes back
to the window and it is logged once. Presents over 2 s three times within
10 s of steady frames are logged once and the game carries on. The
renderer records keep what fails (below): a present error, a lost device
and three resets are struck against the driver, a lost device or the
resets also recording at once that the graphics card is not to be used
with it, and the same in the next run on it recording it failed. Nothing
is struck or recorded on a driver whose device is lost in ordinary use,
on SDL's software renderer unless `--force-capable` runs the accelerated
tier on it, or under `SDL_RENDER_DRIVER`. Window-size textures beyond the renderer's texture limit
(the world, the match dialog layer, the OA settings layer and the front
end's) are made as tiles with gutters (`TiledTexture`), and a match makes
no front-end texture beyond it, since it never draws one; within the limit
each stays one texture, as on SDL's software renderer, which has none. On
a hardware driver the walk chose the opaque layers (the match's, the
loading screen's and the front end's) are ARGB8888, drawn with no
blending; SDL's software renderer, and every driver under
`SDL_RENDER_DRIVER`, keep XRGB8888, RGB565 on a 16-bit window, and RGB24
for the front end. SDL's software renderer asks the window for its pixels
at each frame, since a display mode of another depth or another display
can change them while the game runs. `--check-renderer-ladder`
(`native-renderer-ladder`) forces each of these failures on the dummy
video driver and checks that the game presents on through it, then
starts the game again on renderer records in scratch folders to check
what each crash and failure counts for at the next start;
`--render-fault POINT[@FRAME]` narrows it to one
(`native-renderer-ladder-create` makes every driver but software refuse at
start; `--render-fault card` fails a call of the Full tier's own on a
Full match frame). Four cases switch the accelerated tier on and run
only when named: `--render-fault slow` (`native-renderer-ladder-slow`),
slow frames walking the step-down to the standard tier, `--render-fault
memory` (`native-renderer-ladder-memory`), the memory guard refusing
buffers and then dropping the tier, and `--render-fault full-slow` and
`full-memory` (`native-renderer-ladder-full-slow`,
`native-renderer-ladder-full-memory`), the same for the Full tier, whose
rungs the step-down takes first and whose pages the guard drops first;
each skips under 2 GiB.

Once the renderer is made, start-up describes it with the
[render probe](../platform/render-probe/README.md) and logs one line
(`graphics_report.hpp`): the render driver on the video driver, the
adapter in brackets where it was read, the largest texture side as the
render policy corrects it (`corrected_texture_limit`, which asks the
policy's `texture_limit`) and the tier every frame is drawn in, as in `open-annihilation: graphics: metal on
cocoa (Apple M2), textures up to 16384; standard tier: the processor draws
everything`. Under `SDL_RENDER_DRIVER` the adapter is not read and the
brackets are left out, and so they are for SDL's software renderer, which
has no adapter; no limit reads "textures of any size". The +stats overlay
names the same renderer. `app-graphics-report` checks the limit and the
line, and `native-renderer-report` that a start on the dummy video driver
logs it.

## Adding a screen or overlay

1. In your package, write `void oa::app::register_<pkg>_screens(ScreenRegistry*)`
   (include `screen_registry.hpp`; link `oa-ui-screen-registry`).
2. Fill a `ScreenDesc`: pick an id `>= kFirstPackageScreen` (duplicates are
   rejected at startup), a name, GUI `assets` (null `layout` = you draw
   everything), and any of `enter/leave/event/tick/draw`. Put package state in
   `state`; it is passed back to every hook. Call `screen_register`.
3. For overlays (e.g. a status widget over the main menu) fill an
   `OverlayDesc`: `screen` filter (`kScreenAny` for all), `z` (drawn
   ascending, input descending), `create/event/tick/draw`; `overlay_register`.
4. Dispatcher steps: `step_register(registry, frontend::Step::..., fn, state)`.
   Dispatcher queries: `query_register(registry, frontend::Query::..., fn, state)`;
   the handler's result is the query's, and a query no handler takes is
   answered with 0.
5. Append one line `OA_REGISTER(register_<pkg>_screens)` to `screens.inc`,
   or, for an extension's screens, call the function from its
   `register_screens` hook.
6. Hooks receive a `ScreenContext`: assets, current `surface`, `world`
   (null outside a match), `input` (events only) and services. Navigate with
   `screen_request(ctx, id)` (applied after the current event/tick), play
   sounds with `screen_play_sound`, set the status line with `screen_status`,
   and use `ctx->services->read_number/...` for preferences. The services
   also stop every sound (`stop_sounds`), play a sound on the alternate
   route the menu music takes (`play_sound_alternate`), run one pass of the
   frontend dispatcher once the current event or frame is handled
   (`run_frontend`, never while a match is on screen) and end the run with
   a reason and an exit status once the current event or frame is handled
   (`quit`, which leaves a running match first). The context's `host` and `services` stay the same while the
   runtime lives, so a package may keep them and use the services outside
   its callbacks.
7. Event hooks return nonzero to consume input; otherwise the built-in
   handler still runs.

## Files

- `app.hpp`, `runtime.hpp`, `runtime.cpp`, `runtime_frontend_host.cpp`,
  `runtime_builtin_screens.cpp`, `screen_registry.*`: the screen registry,
  screen loading and the host of the frontend dispatcher. Lines are only
  ever added to `screens.inc`.
- `runtime_world_draw.cpp`, `runtime_camera.cpp`: world rendering and the
  camera.
- `runtime_skirmish_start.cpp` builds a match: the feature table's GAF files
  are kept as read and parsed without their pixels (`gaf::PixelData::checked`);
  a feature sequence's pixels are decoded from its file when a feature first
  draws it, and the map's sprite features' sequences once each, as the first
  of them is placed. A 3DO model the map's features share is loaded once.
  The collision plots go once the match holds its own copy.
- `world_draws.hpp`, `world_draws.cpp`, `runtime_match_render.cpp`: the
  battlefield drawn in horizontal bands. `render_match_surface` first works
  out the frame's draws in their order (`WorldDrawList` in `MatchModels`):
  the effect layers' particles, the features and units far to near, the
  projectiles, debris, explosions and smoke. Everything drawing builds or
  changes on the way is done then, once, on the drawing thread: the piece
  transforms and the presented copies, the units' and features' cached
  images and silhouettes and those of the units they carry
  (`plan_unit_supersampled`), the texture animations, the projectiles'
  models, the GAF frames decoded for the particles (each once a frame), the
  selection boxes' lines and the frame's statistics. The model bridge is
  then split into one band of whole tile rows for each drawing thread
  (`bridge_split`), and each band draws the whole list with its own rows
  alone (`draw_world_band`): its own tiles of the bridge are captured, drawn
  and written back, and sprites, squares and lines change only its rows,
  each line and polygon working out its pixels as over the whole frame. A
  band reads the list and the models and writes only its rows of the frame
  and of the bridge, so the bands draw on the job pool at once and give the
  frame drawn whole byte for byte. The first band draws with the models'
  renderer and buffers; each other band keeps a renderer of its own (its
  composite buffer), its supersampling buffers and its own memory of
  colours outside the palette, about 1 MB a band; with one drawing thread
  there is one band and no more. The debug grid before the list, and the
  fog, the order overlays, the build ghost, the selection band, the health
  bars and the HUD after it, are drawn on the drawing thread as before.
  The terrain, the list and the fog are drawn at the draw scale into the
  scene `world_scaling` gives: the scene pixels per map pixel, which the
  terrain fill and its box filter, the fog, the model bridge and the
  sprites, particles and lines of the list take, while units and features
  are culled, and the order overlays, build ghost, selection band and every
  painter after them placed, in screen pixels at the zoom. The standard
  tier draws at the zoom, its scene the world layer itself; a check may
  draw the scene at another scale apart from the world layer, which a
  nearest resample (`resample_nearest_rgb24`) then fills at the zoom; and
  the accelerated presentation (below) draws a zoomed-out scene at a draw
  scale of its own and reduces it by the area pass. What the match reads
  back from drawing (the view in Game, the on-screen list, the piece
  transforms, the radar) follows the zoom and the camera alone;
  `native-match-layers` draws the scene at 1 apart at zoom 1, 0.5 and 2 and
  checks that, and that at zoom 1 the world layer is the same byte for
  byte; at zoom 2 a nano particle drawn on that scene fills its 2 by 2
  square there and, resampled, a 4 by 4 square at twice its place.
- `world_scaling.hpp`, `world_scaling.cpp`: how a frame draws the
  battlefield (`world_scaling`), from the zoom, the battlefield's size and
  the draw scale asked for, as a pure function. Without a draw scale the
  scene is the battlefield at the zoom, the world layer itself; drawn apart
  at the zoom it is the battlefield's size, and at another scale it covers
  the battlefield's map pixels at that scale with two more columns and
  rows, rounded up to even sizes. The accelerated presentation's own
  (`accelerated_world_scaling`): below zoom 1 the scene is drawn at the
  highest draw scale its scene budget allows (`accelerated_draw_scale`: the
  zoom times the square root of the budget's scene pixels per battlefield
  pixel and of its most scene pixels over the battlefield's, from the zoom
  to 1; both numbers provisional, chosen without measurement) and reduced by the
  area pass, unless the budget is none or the zoom over the draw scale is
  above the cut-off of 0.9, where the frame draws at the zoom; zoom 1 draws
  as always; above it the scene is drawn at 1 and magnified, unless magnify
  is off. `area_scale` gives the area pass's 16.16 scale, and
  `largest_magnified_scene` the scene a magnified frame's texture is made
  at. For a view drawn between map pixels (below), `area_phase` gives the
  area pass's start in the scene, `magnified_span` the corner a magnified
  frame draws and where it lands, one more column and row at the same
  scale, `most_view_offset` how far past its camera the view may lie
  before the camera's farthest place, and `scrolled_view_offset` and
  `view_offset_at` the offset a scroll and a zoom's anchor leave.
  `app-world-scaling` checks these by table, that over the zoom range and
  every window's battlefield the scene holds every pixel the nearest
  resample and the area pass read, and, frame by frame, that a scroll
  toward the map's end draws the view at its exact place while the camera
  steps whole map pixels, toward its start or held at the edge never
  jumps or turns back, and on an axis joining a scroll under way catches
  up with the carry both axes step on by that axis's first step.
- The accelerated presentation (`runtime_accelerated.cpp`,
  `scaled_world.hpp`, `scaled_world.cpp`): a component switched on at a
  rung of the step-down ladder (`switch_accelerated_presentation`) when
  the tier of the frame is accelerated (`runtime_render_tier.cpp`, below).
  Headless runs, the director, a named `--preferences-file` at its
  defaults and every check but `--check-render-tiers` draw and present as
  the standard tier always has. Switched on, a zoomed-out match frame
  draws its scene at the draw scale and the exact area pass reduces it into
  the world layer on the drawing threads, which is then painted over and
  uploaded 1:1 as always; lasers, lightning and selection lines are drawn
  thicker in that scene, so they stay about one screen pixel thick
  (`scene_line_thickness`). A zoomed-in frame draws its scene at 1, which is
  uploaded to a streaming ARGB8888 texture made once, at the first zoomed-in
  frame, at the largest size a magnified frame needs, in tiles beyond the
  renderer's texture limit
  (`TiledTexture`), and the card magnifies it into the battlefield
  (`draw_scaled_world`): NEAREST at a whole-number zoom, the renderer's
  PIXELART where it has it (`probe_pixelart`), and otherwise sharp-bilinear,
  NEAREST into a prescale target made once, at the first frame so drawn, at
  its largest within the prescale budget, then LINEAR; the prescale target is split into tiles with
  gutters beyond the renderer's texture limit, as the scene is, and each of
  the scene's tiles is drawn into each of its tiles (`PrescaleTarget`).
  The nearest picture of the scene is kept as
  the base the painters after the fog paint over; what they changed goes up
  as an overlay, transparent elsewhere, in the 32-row bands that hold it now
  or held it last (`convert_rgb24_overlay_argb`), laid over the magnified
  scene 1:1. A change of the window's size remakes the card's textures and
  the overlay but keeps the base the frame being presented drew, so that
  frame is magnified as every other.
  Smooth panning: while a frame is magnified or reduced by the area pass,
  the view may lie between map pixels (`smooth_view_`, `view_offset`). The
  camera, and Game's, steps whole map pixels exactly as in the standard
  tier (`scroll_match_view`, `apply_zoom_anchor`), so nothing reaches the
  simulation, saves, digests or the wire; the view follows the scroll's
  exact travel within the camera's map pixel, an axis joining a scroll
  under way catching up with the carry both axes step on over the frames
  before its camera steps, or the exact point under a zoom's anchor, the
  anchor's whole map pixel the camera's own, held from 0 to one map pixel
  and before the camera's farthest place, and a camera moved any other way
  starts it on its own map pixel. The card draws the scene that far before the battlefield's
  edge (`magnified_span`), the area pass starts its picture that far into
  the scene, and the painters after the fog move by it to the nearest
  screen pixel. Hover, picking, the drag box, the build site and orders'
  map pixels take the same offset (`game_screen_point`,
  `match_world_point`, `screen_to_map_pixel` with a `ViewOffset`), so the
  pointer is over what is drawn under it, and orders stay whole map
  pixels; the offset never carries the pointer past Game's view
  (`Game.battlefield_rect`) at the battlefield's edges. Every other frame (`settle_view_offset`) draws the view on the
  camera's map pixel and forgets the offset; the picture kept for a
  reader is the standard tier's, on the camera's map pixel. The HUD strips, the front end and the
  loading screen are drawn by `sharp_draw`: NEAREST at a whole-number
  scale, else PIXELART or sharp-bilinear, their prescale targets drawn
  again when the layer's revision moved. Whatever paints a layer moves its
  revision, and every frame the loop presents paints the HUD, and the front
  end too unless its panel keeps the frame shown, so the target is drawn
  again once on each such frame, and never for a present without a paint;
  the front end's target is freed during a match. A call
  only this tier makes that fails throws `AccelerationError`, after which
  the tier is dropped for the run and the frame presented as the standard
  tier presents it (`drop_acceleration`); the failing call is struck
  against the driver, but an error of the game's own, which no driver call
  made (`AccelerationFault::engine`), is not (`take_acceleration_error`). Screenshots, film frames, the
  load and save backdrop, the briefing's backdrop and the end screen keep
  the standard tier's picture of the same moment (`ensure_screen_world`),
  drawn again with the frame's counts of units drawn kept and the HUD's
  resource readout, which saves keep in `Game.resource_readout`, not eased
  again.
  `app-scaled-world-software` checks the drawing on SDL's software
  renderer against nearest replication and that renderer's own LINEAR,
  modelled on the processor (`software_linear_rgb24`), within 2 levels,
  scenes and prescale targets in tiles and a view between map pixels
  among it; `app-world-draws` checks
  the thick lines band by band; and
  `--check-render-tiers` (`runtime_render_tiers_check.cpp`,
  `native-render-tiers`, with `--hardware-acceleration` and
  `--force-capable` on that renderer, which the start-up function test
  passes, and `native-demo-render-tiers` over the demo's first Arm
  mission) checks the presented frames of the main menu and a fight at
  zooms from 0.5 to 4, against the references of `scene_filter.hpp` on a
  card and against that model on SDL's software renderer, switching the
  tier off and on as the flags would; that a frame depends on none before
  it, the first after the tier is switched on or the window resized among
  them; that the picture kept for a reader is the standard tier's and eases
  nothing; that a slow scroll at zoom 2.5 and 0.5 moves the battlefield
  read back by at most a pixel a frame, the view drawn at the scroll's
  exact place while the camera steps whole map pixels, where at 2.5 the
  standard tier jumps two or three pixels, and a second axis joining the
  scroll moves at most a pixel a frame too; that at zoom 4 the pointer
  finds a unit three pixels further left with the view three quarters of
  a map pixel on, as it is drawn, and the game view up to the
  battlefield's edges; and that a zoom to the zoom it is at, by the wheel
  or about the centre, keeps the point drawn under its anchor
  (`check_smooth_panning`, `runtime_smooth_pan_check.cpp`); that a zoom
  ease makes no texture; that prescale targets are drawn once a painted
  frame; and that the Full tier's model stage (`runtime_full.hpp`), given
  the zoom-1 frame's list, draws the fight's units, projectiles, debris and
  fragments and their shadows on the game's renderer within the model
  raster bounds of the processor's raster of the same list
  (`check_full_models`); it writes pictures of one moment at zoom 0.5, 1
  and 2.5 in both tiers. Its flag names Full, so its Basic cases set the
  level to `basic` and its Full cases (`check_full_render_tier`) run after
  them at `full`: at zoom 1, 2 and 4 the frame equals the standard tier's
  draw of the same moment beside the card's own draws (`card_draw_mask`:
  the sprites the alpha table blends, the sprites that reach a fog tile
  not wholly clear, the lines, the units, projectiles, debris and
  fragments with their shadows, and the fog tiles whose edges ramp), with
  the draws under the first differing pixel listed and a picture of them
  when it does not; a Full frame leaves what the match reads back
  (`match_draw_read_back`) as the standard tier's frame of the same moment
  does; at zoom 0.5 with the
  camera on an even map pixel the terrain under a transparent overlay,
  where the standard tier shows terrain too, equals that tier's box filter
  exactly, and at 0.75 the blend of the two levels, the card's own filter,
  is printed against it with its mean bounded and held within the
  renderer's tolerance of that renderer's own LINEAR of each tile's quad,
  pass over pass (2 and a mean of 1 on SDL's software renderer, 4 and a
  mean of 0.5 on a card), the references built from the check's own atlas
  of the map; at 1.37 the terrain through the target keeps within
  the renderer's tolerance of the level-0 view enlarged twice and drawn
  LINEAR, the sharp-bilinear reference; at the window whose chrome scales
  by 1.6 the HUD strips keep within the chrome's filter, as the Basic case
  there holds them; the box filter never runs for a Full frame; the match
  loads in Full, its loading screen makes the terrain pages and their
  greyed pages and lets the atlas's texels go, and the first match frame
  draws from them; a Full frame draws its sprites and models in the same
  card frame as its terrain and fog; and the terrain's processor cost, the
  frame's build, the card's call and the overlay, is printed for each zoom
  beside the box filter's, with pictures of each zoom. Its anti-aliasing
  cases set the row to 2x and 4x and, at zooms 0.5, 0.75, 1, 1.37 and 2,
  require the factor the budget allows, no unit drawn finer on the
  processor, and the battlefield under the transparent overlay equal to
  the world target read back and reduced on the processor as the card
  reduces it, by halving from zoom 1 up and by the two-level blend below,
  within the renderer's tolerance, the target's memory printed within the
  budget; with the row off again the target is freed. The fog, the canvas,
  the kill board and the +stats panel (`check_full_overlays`), at zoom 1
  and 2: with line of sight alone, under a fog tile wholly out of sight a
  pixel the tiers agree on with the fog off is the processor's exactly,
  under a tile at an edge it lies between its colour and its grey within
  2, and under no tile it is unchanged, passing over what the design
  accepts, the sprites under tiles of more than one state, the blended
  sprites over the fog and the models; with mapping too the tiles never
  mapped are the processor's black and nothing else changes; the dithered
  option is the even tone within 2; the overlay canvas is painted exactly
  where the overlay is opaque; the kill board pinned by F4 darkens the
  world under it to the shade level's share within 2 with its foreground
  on the overlay, the local player's lit row left out and nothing outside
  it changed, and leaves no pixel behind; and the +stats panel darkens its
  padding and its graph well by its opacities and draws its outline in the
  dark and light edge colours, with nothing outside it changed. At native
  density the Full frame at zoom 1 is the standard tier's draw enlarged,
  beside the card's own draws.
- The Full tier (`runtime_full.cpp`, `runtime_full.hpp`,
  `full_presentation.hpp`, `full_terrain.hpp`, `full_terrain.cpp`,
  `runtime_full_sprites.cpp`, `runtime_full_models.cpp`, `full_fog.hpp`,
  `full_fog.cpp`, `runtime_full_overlays.cpp`): a branch of the
  accelerated presentation, taken when the tier decided for the frame is
  Full (`set_full_presentation`), in which the graphics card draws the
  whole battlefield and the processor paints the HUD and what the painters
  after the fog paint. The planner runs as in Basic and writes what it
  writes there; `world_scaling` returns the zoom with no split, so the
  frame is planned in screen pixels at the zoom, but the terrain is not
  filled, the bands do not draw and the fog is not rasterised
  (`note_full_canvas`): the world layer is cleared to a key colour, the
  lowest colour not in the palette (`full_overlay_key`,
  `overlay_key_colour`), and is the overlay canvas the painters paint on;
  the box filter never runs (`refresh_filtered_terrain`). As the match
  loads, with the terrain step of its loading screen and before the world
  is built or a shared game's load barrier runs (`make_full_match_pages`,
  from `bootstrap_match`), the card's executor (`src/app/card`) is opened
  on the renderer at its texture limit and the Full function test run on
  it, four batches into one target read back once: an opaque page of two
  levels drawn 1:1 NEAREST reads back as its texels, its level 1 drawn
  twice its size LINEAR within 2 of the enlargement (or exactly NEAREST on
  SDL's software renderer), a white quad at alpha one half over a known
  colour as the blend within 2, and a triangle with red, green and blue
  corners shows their mean at its centroid pixel within 4; the map's
  terrain atlas (`src/present/gpu-world`) is built within `fit_page_edge`
  of that limit, through the display gamma, twice, with the palette and
  with a palette of the gray table's entries, the greyed variant the fog
  reads (`greyed_palette`), levels 0 and 1 of each uploaded as pages and
  each page's texels let go once uploaded (`ensure_full_terrain_pages`),
  so that the match's first frame finds the pages and makes none of them.
  A frame builds them again only when the renderer was made again, the
  pages were freed, or the palette, the gamma or the page edge changed
  since (`ensure_full_match_textures`), which also gives the sprite pages
  the match's palette at the display gamma and the fog's gray table, and
  the model stage its palette; the atlas is keyed by the map's own
  storage, since one map record holds every map of the run in turn. A
  card failure at the loading screen drops Full for the run and the match
  plays in Basic. Each frame (`present_full_match_layers`) the builder
  appends to one `CardFrame`, in this order. The terrain: one quad per
  visible tile, a batch for each run of tiles on one page, by the level
  rule (`plan_terrain_draw`): between zoom 0.5 and 1 level 1 LINEAR, then
  level 0 LINEAR over it at alpha 1 - log2(1/zoom), which at 0.5 is level
  1 alone, today's box filter where the camera lies on an even map pixel;
  at 1 and every whole number above it level 0 NEAREST, 3.1c's pixels; at
  another zoom above 1 level 0 by the pixel-art sampling mode where the
  start-up probe found it, which the rung's card filter carries, else
  NEAREST into a target made once at twice the battlefield, a whole number
  of map pixels at zoom 2, 3 and 4, and the target drawn LINEAR to the
  window by zoom over the next whole number, the Basic tier's
  sharp-bilinear, or LINEAR straight where the target cannot be made. The
  fog's greyed pass (`full_fog::append_unseen_terrain`), from the fog grid
  the frame's fog pass built and kept in place of drawing
  (`apply_match_fog`, `note_full_fog_grid`): over each fog tile with a
  corner out of sight, the terrain again from the greyed pages at the
  terrain's levels and sampling, into whatever the terrain was drawn
  into, four quarter-tile quads whose corner alphas are 1 at the corners
  out of sight, so a tile wholly out of sight shows the gray table's
  colours exactly and a tile at an edge ramps from colour to grey where
  the processor's FOG.GAF masks cut, a terrain tile under four such tiles
  as one quad, and where two terrain levels blend each level's greyed pass
  at its share (`pass_alpha`). The model stage's shadows, then the list's
  draws in painter's order, each to the stage of its kind (`sprite_kind`,
  `model_kind`), a stage's batches never joining another's: the sprites,
  blended sprites, particle squares, lines and selection lines
  (`SpriteFrame`, `runtime_full_sprites.cpp`), each sprite a quad from its
  cell on the sprite pages (`src/present/gpu-world`, keyed by the frame
  the planner drew from) drawn by premultiplied alpha, at a vertex alpha
  of one half where the planner blends it through the alpha table, so the
  card blends to the true mean where the table snaps to the palette,
  sampled nearest at a whole-number zoom, linear below 1 and pixel-art
  above, from its greyed cell where the map pixel under its drawn point
  lies in a cell out of sight (`cell_fog`), since the fog lays its tiles
  by map column and row alone, so a sprite at the fog's edge is wholly
  grey or wholly colour, and in colour under the dithered option; squares
  as the planner's rectangles; lines as quads `max(1, zoom)` pixels wide
  through the centres of their end pixels, selection lines the same from
  the bridge's map pixels; a frame whose distinct sprites exceed the
  pages' memory draws none of them, since a cell evicted before the frame
  ran would show another sprite; and the units, 3D features, projectiles,
  debris pieces and shatter fragments (`ModelStage`,
  `runtime_full_models.cpp`) as triangles from the models' meshes, each
  corner placed from the piece transforms the planner rebuilt with the
  arithmetic of the path the processor draws the piece by (a cached piece
  as its image, a moving piece flat, a carried unit as its carrier
  composes it), the polygons of a unit with a depth plane sorted lowest
  first, the palette's tables approximated as the design says: the shade
  rows as a per-vertex multiplier with a bright page of doubled texels for
  the rows above unlit, the alpha table as alpha 0.5 for cloaked units and
  shadows, the blue table as a halved colour with an additive lift, the
  nanoframe's bands per polygon and its outline as line quads, diggers'
  and other players' underwater polygons left out; texture frames go on
  sprite pages on first sight in two variants, the image key transparent
  as in a cached image or a colour as in a flat draw; shadows go into a
  transparent shadow target the battlefield's size, every silhouette
  replacing what is there so overlaps darken once, composed over the
  terrain at half darkness by one resolve before the list's draws, or
  straight into the battlefield where the target cannot be made
  (`emit_shadows`, the extension point a later better-shadows option
  replaces). The fog's dither over everything under the dithered option
  (`append_unseen_dither`), palette index 0 at alpha one half, the even
  tone the processor's every-other pixel averages to, over the objects as
  the processor dithers them, since the pages hold no dithered sprite; and
  its black pass over everything (`append_unmapped`), UI colour 0 at the
  corner alphas of the cells never mapped, which blacks out the units as
  the processor does. Last the painters' quads (`paint_world_level`,
  `paint_world_blend`): the kill board's shade of the world under it and
  the light of the local player's row, and the +stats panel's fills, which
  in Full ask the card for a quad in place of reading and shading the
  canvas, which holds no world: a shade row as black by alpha over the
  world, leaving row x 0.06875 of it, a light row as white added by
  1 - 1 / (1 + row / 30) (`full_fog::level_quad`), a blend as its colour
  at its opacity; their foregrounds go on the canvas as in every tier. The
  executor runs the frame within the battlefield's scissor; the canvas
  goes up as the overlay by its key (`convert_rgb24_keyed_overlay_argb`),
  in the bands that hold paint, laid over the card's picture 1:1, so
  Basic's overlay by difference, which cannot tell a paint of the base's
  own colour from the base, is not needed; the sprite pages' texels and
  the model stage's pages go up as they change; the HUD strips
  (`draw_accelerated_hud_strips`), from the HUD layer's prescale target
  made as Basic makes it (`ensure_accelerated_match_textures`), the
  dialogs, the cursor and the present follow as in Basic, and the readers
  that keep a picture, the capture and the checks among them, get the
  standard tier's draw (`ensure_screen_world`), since the world layer
  holds no picture.
  Full falls back to Basic, which falls back to the standard tier, each
  for the rest of the run (`drop_full`, `TierInputs::full_drop`, a kind
  for each cause): a call of the card's own that fails (`FullCardError`,
  the stages' `full::CardError`), or the failure `--render-fault card`
  forces, drops Full with the failing call struck against the driver as a
  `card` strike (`take_full_failure`), which the same failure in the next
  run on the driver records `full-unusable`; a failed Full function test
  drops it as the card lacking a feature Full needs, with nothing struck;
  the memory guard counts Full's pages and targets
  (`AcceleratedBuffer::card_pages`, `card_targets`) and refuses or drops
  Full before Basic, judging Basic afresh on the memory Full freed
  (`retry_memory_guard`); slow frames take Full's rungs first, its
  anti-aliasing from 4 to 2 to 1 (`LadderState::full`, `supersample`,
  which Full's world target draws at) and then Full itself
  (`StepResult::basic`), never back up within the run; and Full's first
  card calls of a run, the pages made as the match loads
  (`make_full_match_pages`) and its first frame, stand under its own trial
  and sentinel, `path full` (`begin_full_path`), a trial that cannot be
  written keeping Full off with nothing struck. Basic presents the frame
  that failed, with the standard tier's draw of the world, and every frame
  after, with the status saying why Full stopped, until Off and back, a
  raise of the row to Full, or Restore defaults lifts the drop, which
  nothing does for the memory guard's. In a shared game or a replay a
  lower tier applies at once and Full waits for the match to end
  (`SharedMatchGate::full`); its terrain pages, overlay and zoom-in target
  are made as the loading screen begins (`preallocate_full_match_textures`),
  and a page a frame would make later, a sprite page among them, waits for
  the match to end instead (`full_creation_allowed`). The start-up line
  and `+stats` name the full tier, and the step-down is fed the build, the
  stages, the card's call and the overlay as Full's passes. Off and Basic
  are untouched: nothing here runs unless the tier is Full, which only
  `--hardware-acceleration=full` gives while `render_policy::full_ready`
  is false; it stays false until the complete Full frame of
  `native-render-tiers` is signed off against the standard tier's
  (design D76), so a setting of Full resolves to Basic for players.
  Anti-aliasing in Full is the graphics card's (`full_supersampling.hpp`,
  `ensure_full_world_target`): the Enhanced anti-aliasing row's level asks
  for a supersample factor, off 1, 2x and 3x 2, 4x and above 4
  (`render_policy::supersample_factor`), which Full's rungs start from
  (`RendererHost::set_full_supersample`) and follow while the step-down
  has not lowered them (`apply_full_supersample_setting`); the factor is
  lowered to the rung's once frames were slow, and to what the budget S
  of the machine, 2^25 pixels, a quarter of it at 4 GiB or less and on a
  light machine or a Pi (`render_policy::supersample_budget`), the memory
  guard and the renderer's texture limit allow at the battlefield's size
  (`render_policy::fit_supersample_factor`); above 1 the card draws the
  terrain, the fog's greyed pass and the stages into a world target at
  that factor, made once with its half and remade when the factor or the
  battlefield changes, by the plan of
  `full_supersampling::plan_world_target`: from zoom 1 up at the zoom, the
  texture holding the factor's pixels a window pixel, reduced into the
  battlefield by exact halvings (`resolve`); below zoom 1 at one texel a
  map pixel over the part the battlefield shows, reduced by the two-level
  blend (`blend_reduce`), the half at twice the zoom under the part at
  alpha `1 - log2(1 / zoom)`; the fog's black pass, its dither and the
  painters' quads go over the reduced picture, as the overlay does. The
  processor's anti-aliasing never runs in a Full frame
  (`unit_supersampling_` is read as off there), the factor in use is
  logged with the target's size and memory when it changes and shows in
  the `+stats` renderer row and in the row's hint
  (`AccelerationStatus::full_supersample`, beside the rung's
  `supersample`, which the status line names), and a target the renderer
  or the memory guard refuses, or that a shared game's frame would make
  after its loading screen, leaves the tier drawing straight.
  Not yet: the `scale-level` key for Full's rungs, which the game writes
  for no tier, the sprite pages made ahead at a shared game's loading
  screen, smooth panning and native density for the fog passes and the
  painters' quads (they draw on the camera's map pixel at density 1), the
  sprite pages' level 1 (the stage samples level 0 linear when zoomed
  out), and the golden images.
  `app-full-terrain` checks the level rule and the quads over a small map
  on three pages, one batch per page whatever the grid's order;
  `app-full-supersampling` the world target's plan at every kind of zoom
  and battlefield; `app-full-sprites` and `app-full-sprites-data` the
  sprite stage against the bands' picture on SDL's software renderer
  (`full_sprites_test.cpp`); `app-full-models` and `app-full-models-data`
  the model stage against the processor's raster
  (`runtime_full_models_test.cpp`); `app-full-fog` the fog passes' quads,
  alphas and placement and the painters' level quads
  (`full_fog_test.cpp`).
- Native pixel density: a window's density is fixed when it opens.
  `decide_window_density` (`render_host.cpp`) decides it before the window
  opens by the render policy's rule (`decide_native_density`): from 2 GiB,
  with neither a flag that names Off, `SDL_RENDER_DRIVER`, a dummy
  or offscreen video driver, an unattended run nor a capture, with Basic or
  Full asked for by the setting or a flag, in a class of machine measured
  at native density, from a start above budget none and a remembered rung
  above magnify off, and with the `native-density` record an earlier run
  left (`note_density_run_end`, `forget_native_density`, which nothing in
  the game calls). The start reads the records before the window opens
  (`HostDisplay`, `RendererHost::open_records`), so the key's driver
  reaches the rule, though no remembered rung does, since the game writes
  no `scale-level` key. No class has been measured
  (`native_density_measured`), so only `--native-density`, which the
  render tiers check alone takes, opens a window at native density; every
  other opens at the window system's density, as before. On a window at
  native density (`at_native_density`) `apply_output_mode` lays the match
  out in window points and stretches it over the display's pixels by
  logical presentation, so the processor draws what it draws on any other
  window of that size, and the scene and its budget do not grow; the front
  end's letterbox is unchanged. The standard tier's layers are enlarged
  NEAREST; the accelerated tier draws its scaled layers at the display's
  scale, the layout's scale times the density, with NEAREST kept where that
  product is a whole number (`world_display_scale`, `chrome_filter`), and
  its layers laid out 1:1 NEAREST at a whole-number density and by the
  chrome's filter at any other, plain LINEAR where that filter would need a
  prescale target (`one_to_one_scale_mode`). Below zoom 1 the area pass,
  where it runs, still runs at the layout's size at every density, and the
  card enlarges its result by the density: the scene is not magnified
  there by the zoom times the density over the draw scale. Pointer events
  reach the layout through SDL's view, so picking is unchanged; the edge
  scroll is one layout pixel deep there (`edge_scroll_depth`); screenshots,
  film frames and snapshots keep the layout's size. With
  `--native-density`, `--check-render-tiers` runs its density case alone
  after the main menu and the loading screen (`native-render-tiers-density`
  on the dummy video driver, whose density is 1, and by hand on a display
  above density 1).
- The accelerated tier's watch (`runtime_tier_watch.cpp`), made when the
  tier first switches on in a run, so that a run on the standard tier
  holds none of it (`AcceleratedWatch` in `render_run.hpp`). While the
  tier draws, the memory guard samples the system's memory about once a
  second (`watch_accelerated_memory`) and drops the tier for the rest of
  the run when it trips, which neither Off and back nor Restore defaults
  lifts; before the tier makes its scene and overlay or a prescale target
  it asks the guard with a fresh sample (`accelerated_buffer_allowed`), and
  where the guard refuses, the tier stays on the rung below, magnify off or
  the card's magnification one rung lower (`rung_without`). Each match
  frame the tier presents feeds the step-down (`feed_render_step_down`,
  `feed_presented_frame`): its interval with its ticks' time taken out, its
  draw and present measures and the tier's own passes (the area pass or
  the nearest resample, the canvas copy, the overlay's conversion and the
  uploads of the scene and the overlay, timed as they run), against the
  lower of the rate the loop paces at, the lowest 30, and 60, and whether
  it is steady, what it showed and whether the match clock runs below its
  requested rate; the cost test takes a median as over the period only
  past the loop's allowance, so on-time frames on the pacer's grid never
  step. Frames of the standard tier, idle frames, a check's own frames and
  a frame of a second or more after a shorter one, a wait for the window's
  focus or a save, never feed it; a run of such frames, a machine that
  really crawls, does. Each step lowers the rung for the rest of the run
  (`lower_accelerated_rung`), freeing what the lower rung no longer draws
  with: the scene and its terrain buffer when magnify goes off or the
  budget falls, and the prescale targets the chrome or the card's
  magnification no longer use, the magnified scene's only when the card's
  magnification changes, since the NEAREST-chrome rung leaves the scene's
  filter as it was (`world_filter`); each step is logged once. The status
  then says the tier smooths less because frames were slow, and the last
  step drops the tier, as the status then says, until Off and back or
  Restore defaults starts the ladder again from the top. A later switch-on, after
  a lost device or a shared game, keeps the rung reached. The rung is not
  kept for the next start: the game reads and writes the renderer
  records, but not their `scale-level` key, which is reserved for it.
  `--check-renderer-ladder --render-fault slow`
  (`native-renderer-ladder-slow`) forces slow frames and sees idle frames
  and the standard tier never feeding it, a frame's ticks taken out
  against the loop's paced rate, each rung with what it frees and keeps,
  the status, the drop, Off and back and a clock below its rate;
  `--render-fault memory` (`native-renderer-ladder-memory`) forces the
  guard's sample, which refuses each buffer and then drops the tier,
  freeing the scene's buffers.
- The tier each frame is drawn in (`runtime_render_tier.cpp`): at start,
  once the renderer is made, `RendererHost::decide_start_tier` fills the
  render policy's facts (the flags, the Hardware acceleration setting read
  before the window opens, `SDL_RENDER_DRIVER`, a video driver with no
  window, a named preferences file, the machine's physical memory against
  the 2 GiB threshold, and what probe items 1 to 3 found of the renderer:
  on Windows before Vista only `direct3d` is capable, and under
  `SDL_RENDER_DRIVER` the adapter is read only when
  a flag that asks for the card or `--force-capable` asks for more than SDL's
  own start) and, where the tier could be accelerated but for it, runs the
  start-up function test (`run_function_test`): a render target cleared
  and read back, a LINEAR reduction by half within 2 of the texels'
  average, PIXELART (`probe_pixelart`), and a seeded pattern drawn NEAREST
  through a source rectangle into a prescale target, reduced LINEAR and
  overlaid, read back against `sharp_bilinear_rgb24` and `overlay_rgb24`
  within 3 and 0.5 on the mean. The trial `probe <driver>` is written and
  flushed before the test (`RendererHost::test_function`), so a start on
  the player's own profile runs it by itself where the policy allows; a
  trial that cannot be written skips the test and keeps the standard tier
  (`FunctionTest::trial_unwritten`) until the player tries again. With a
  named preferences file the records live in memory and it runs where the
  file sets the setting to Basic or Full. The start-up line names the
  tier, standard, basic or full, with what it does, "(Full is not in this
  build)" after basic where Full was asked for, or the reason the
  processor draws everything (`tier_description`); the `+stats` renderer
  row names the tier the same way. Full, the battlefield drawn on the
  graphics card (the Full tier, above): the render policy's request
  (`TierInputs::setting`, `AccelerationFlag`) carries Off, Basic or Full,
  and `decide_render_tier` gives `RenderTier::full` where Full was asked
  for, Full is ready in this build (`render_policy::full_ready`, false
  until the complete Full frame is signed off, design D76) or
  `--hardware-acceleration=full` forced it, the driver has no
  `full-unusable` record or that flag was given, Full was not dropped for
  the run, and a shared game or a replay began in Full; otherwise Basic,
  with the Full reason (`FullReason`) in the decision, which the status
  and the start-up line's note say. Before each frame, `Runtime::update_render_tier`
  brings the facts up to date (the flags, the setting in effect, the
  director, a lost device) and takes the frame's step from the render
  policy (`step_tier`): the tier, the function test run where only it is
  missing, the frame noted in a shared game or a replay, and the switch
  that makes the accelerated presentation match, on at the machine's
  starting rung or off, with the Full branch marked where the tier is Full
  (`set_full_presentation`). Setting Hardware acceleration to Off applies at
  once; Basic and Full apply at once too, except in a shared game or a replay, known
  from its bootstrap (`MatchBootstrap::multiplayer`, `replay`), which keeps
  the tier it began with until it ends (`begin_render_tier_match`,
  `end_render_tier_match`). Setting it to Off and back, raising it from
  Basic to Full, or Restore defaults,
  clears the renderer records' strikes and failure records in memory, lets
  a failed function test or an unwritten trial try again, lifts a drop of
  either tier other than the memory guard's and starts the step-down again
  from the top, at once where the tier stays on (`take_renderer_retry`,
  `forget_render_failures`); OK writes the cleared records
  (`keep_renderer_records`) and Cancel puts them back
  (`restore_renderer_records`). A failed call of the accelerated tier
  drops it for the run and is struck against the driver
  (`take_acceleration_error`), and so is a renderer made again after a
  present error, a lost device or three resets within a minute; the memory
  guard and the step-down's last rung drop it for the run with nothing
  struck. The dialog's status and locks follow these facts
  (`tier_acceleration_facts`, with what the records hold,
  `RendererHost::fill_record_facts`); a driver that failed in the run, a
  record and a driver a record passed over lock nothing, so that the row
  can retry them. `+stats` names the tier. `native-engine-settings` sets the row to Basic, Full
  and Off through the dialog under `--force-capable` and retries it after a
  drop and after a function test forced to draw wrongly.
- `runtime_match_menus.cpp`: the in-match menus. A dialog opened over the
  match HUD (the exit menu, the surrender confirmation, RESTART.GUI, the
  Game Settings sheet, the removal question) is placed as 3.1c's panel
  loader places it and keeps the panel it opened over drawn under it,
  darkened where 3.1c darkens the panel below (`open_match_dialog`). A
  dialog centred on the whole screen is drawn at the canvas's pixels over
  the side column too (`match_dialog_side_`); while
  the in-game menu or the tab menu is open the panels show their keyboard
  focus. The load and save dialogs darken the panel below with the frontend
  renderer's `shade_panel_below`, as the frontend dialogs do.
- `runtime_scroll_bars.cpp`: the scroll bars of the frontend screen's panel
  and of the match HUD's panel (`renderer::LayoutScrolls`), bound as each
  panel's first draw binds them, drawn over it, driven by the pointer and
  each frame's tick, and kept in step with the lists they scroll; a slider's
  move runs its options callback or sets the share panel's amounts. The
  preferences' sub-panel keeps the side column's scale down to its bottom
  over the battlefield wherever the window puts the bottom bar
  (`place_preferences_rows`). `runtime_scroll_bar_check.cpp` holds
  `--check-scroll-bars` and the pointer helpers other checks drive scroll
  bars with.
- `runtime_team_panels.cpp`: the team panels of a multiplayer game over the
  running match: the tab menu (Tab), SHARE.GUI ('h'), ALLIES.GUI,
  CONTROL.GUI and its removal question, laid out and answered by
  `ui/hud/team_panels.hpp` and `share_panel.hpp`; what they tell the other
  players' machines goes through the extension's `TeamPanelHost`.
  `runtime_team_panel_check.cpp` checks the Pause key and the panels in
  `--check-navigation`.
- `match_clock.hpp`, `match_clock.cpp`: when the match clock steps. A menu
  and the outcome hold a match played on this machine alone; a match shared
  with other players' machines runs on under its menus, the preferences they
  open (`Runtime::match_running`) and while it waits on its outcome. The
  pause bit of `Game.sim_run_flags`, which the Pause key
  flips (and another player's machine may set), holds any match inside the
  clock, whose time moves on so that nothing is caught up on resuming; a
  save stores the bit as the match holds it. A frame whose clock reading
  lies behind the clock's last step (a load, a screenshot or a film frame
  set the clock past the frame's time) holds its step; the reading turns
  over to 0 about every 39.8 hours, and a reading more than half a turn
  behind has turned over: the frame steps and runs no tick, and the frames
  after it step as before, as in 3.1c. `app-match-clock` tests all of
  these, with frames across the turn from a clock just before 2^32
  milliseconds.
- `memory_guard.hpp`, `memory_guard.cpp` (part of `oa-app-render-policy`):
  the memory guard of the accelerated tier, as a pure state machine with no
  clock, which the tier's watch acts on (above). Handed a sample of the system's
  memory about once a second (`oa::platform::sample_system_memory`), it
  asks the tier to drop acceleration for the rest of the run when the
  process's private committed memory rises above half of physical memory,
  or when free physical memory stays under a sixteenth of it for 3 s;
  where the system reports no free memory, its memory pressure at the
  critical level stands in for it, or else more than 128 hard page faults
  a second. These figures are conservative, chosen without a measurement
  on period hardware. Before the tier makes a buffer of its own,
  `memory_guard_allows` tells whether free memory would stay at or above
  its threshold and committed memory at or under its own.
  `app-memory-guard` tests it by table, and `platform-system-memory`
  samples the system it runs on and prints what it reports. The guard is
  built into the render policy's library, under the
  `oa::app::render_policy` namespace it uses; its header, source and test
  stay files of their own.
- `frame_pacing.hpp`, `frame_pacing.cpp`, `frame_stats_panel.hpp`,
  `frame_stats_panel.cpp`, `runtime_frame_stats.cpp`: the
  application loop's frames, apart from the simulation's 30 ticks a second.
  The loop draws up to `--max-fps` frames a second (120 unless it says
  otherwise; 0 for no limit, and never fewer than 30, so that a frame on
  time never runs two ticks at normal speed), each frame standing for its
  time on an evenly spaced run of frames (`FramePacer`), with a precise wait
  between them; a frame that ends late starts the next at once and the run
  goes on from it, never with frames bunched to catch up. At 30 a second,
  the tick rate, each frame is due at the middle of the match clock unit
  after the last one's (`next_clock_unit_middle`), so that each frame on
  time steps the clock by one unit, runs one tick at normal speed and shows
  it whole; frames evenly spaced at that rate from any start would, from
  some starts, run none and then two, as the clock's whole milliseconds turn
  its units over. While nothing
  moves on its own (no stepping match, no camera motion, no input for half
  a second), a paused multiplayer match among them, it draws 30 a second,
  and an event ends the wait at once. While Vertical sync is in effect the
  rate is also held to the largest whole rate below the display's, read
  each frame from the window's display (`vsync_frame_cap`), and never under
  30. The match clock steps to each
  frame's time, and the frame is drawn the fraction of the way between the
  state before the last batch of ticks and the state after it that its time
  stands for (`presentation_alpha()`, `next_presentation_alpha`): never
  past the current tick, whole ticks while the match is paused, waits on
  another machine or catches up, on a check's fixed clock and for a film
  frame, and 1 again once the frame is drawn, so that every other drawing
  shows whole ticks. At 30 a second a frame counts its clock unit whole: at
  normal speed and above it shows the state its ticks reached, and below
  normal speed the progress the clock makes by the unit's end, so that
  frames still move evenly between the ticks. The unit drawing adds what it
  drew to `frame_draws_` (`FrameDrawCounts`). The camera scrolls and the
  zoom eases for each frame's real time (`scroll_distance`), so they move a
  steady amount every
  frame, and a camera tracking a unit is centred, after the clock step,
  where the frame shows the unit (`place_tracking_camera`), so that the
  unit holds still on the screen, and the pointer, clicks and the build box
  map through the camera the frame is drawn from. The resource readout
  eases toward the stores 120 times a second of frame time, as often as it
  eased at the default rate. Drawing a unit refreshes the piece positions
  some of its script's queries read, so a tick with no frame drawn after it
  (a frame slower than a tick, or a batch of ticks above normal speed) can
  still change the match, as it could before frames were drawn between
  ticks. "+stats", an option command the runtime adds to the console,
  shows a panel at the battlefield's bottom right: the battlefield
  darkened under it, a black outline and a raised edge in the GUI
  palette's light and dark edge colours, and on it a table
  (`frame_stats_table`) titled "Frame stats (ms)" of the frames a second
  of the last second, with the rate the loop keeps, the frame, work, tick,
  draw and present times' least, mean and most in columns, the units
  the last frame drew, and the renderer: the tier frames are drawn in and
  the render driver, as in "standard: metal", with the adapter's name, each
  cut to 23 bytes, never inside a character. The panel keeps its width
  whatever the names: it cuts the renderer row, in its font's own widths,
  where it would pass the panel's padding (`fit_run_on_row`). Each
  time is graded as it is taken, against the
  allowance of the frame it belongs to (`frame_allowance_ns`: 1 / the rate
  kept, and half a millisecond after a precise wait or two after an idle
  one, whose wait is rounded up to whole milliseconds), and shows in a
  green within it, the health bar's yellow over it but within a tick, or
  its red, on a red cell, over a tick (`time_severity`), so that times
  keep their colours when the rate changes. Under the table, a graph of
  the last two seconds of frames laid end to end (`FrameHistory`, a
  column for each 1/120 s holding the longest frame that covers it): a
  frame a column at 120 frames a second, a 33 ms idle frame four columns
  wide, and a hitch as wide as it lasted, each bar in its frame's grade's
  colour, capped in white past the graph's 40 ms, over a gray line at a
  tick and a fainter dotted one at the frame's allowance.
  `frame_stats_panel` lays the panel out in the match label font for the
  widest texts the table shows (`frame_stats_widest_table`, which keeps no
  room for the renderer row), so nothing in it moves from frame to frame,
  and places it at the HUD's text scale, or
  at the largest whole scale below it at which it fits the battlefield's
  bottom right quarter; it reads the statistics and writes nothing of the
  match. The console check types "+stats" and checks the panel's outline,
  edge, fill and graph, that it fits the quarter at window sizes from
  640x480 to 3840x2160, that every column of the graph has its frame's
  height and colour over two seconds of late and slow frames, the lines,
  the colours, alignment and red cells of the table, that nothing moves,
  and that nothing outside the battlefield's bottom right quarter changes. 3.1c
  has no command that shows these times; its frame rate shows as "FRATE:"
  on the debug keys' line (F11 after the developer passphrase), with
  "[Release]" and "MODE DEBUG INFO ON" or "OFF", which
  `draw_debug_status_line` draws as 3.1c does and the console check
  checks. `app-frame-pacing` tests the pacing, the fraction, the figures,
  the history and the grades over a fake clock, among them a frame for each
  tick at 30 a second from any start, each shown whole, and how faster and
  slower game speeds show at that rate, and
  `app-frame-stats-panel` the panel's rows, columns, notes and graph, its
  place and scale, the bars and lines and each grade's colour. `--frame-rate FPS` with `--match-ticks` plays
  the headless skirmish frame by frame on a clock of its own, moving the
  camera and stepping the clock as the loop does and drawing each frame as
  the loop does; `--frame-log FILE` writes each frame's time, tick,
  fraction, camera and a unit it follows, where the simulation holds it and
  where the frame drew it, `--scroll-camera` sweeps the camera's scroll
  right and back over the army, `--march` sends the local army south,
  `--follow` tracks the unit the log follows and `--frame-clock MS` starts
  the run's clock MS milliseconds in instead of at 0. `native-frame-rate`
  checks that 30, 60, 120 and 144 frames a second write one trace stream
  and reach one world digest; that at 30 each tick has one frame, which
  shows it whole; that at 120 the camera moves evenly, each frame shows a
  quarter of a tick more, and the unit drawn moves on nearly every frame,
  none carrying more than half the most it moves in a tick; that a
  tracked unit is drawn at one place of the screen on every frame; and
  that runs at 30 and 120 from 2 seconds before 2^32 milliseconds, where
  the match clock's reading turns over to 0, step on through the turn to
  the same trace and digest, at 30 each frame running a tick but the first
  past the turn. The run
  ends with a line giving the world digest, a frames digest of every
  frame's drawn battlefield, the drawing threads it drew on and the most
  bands a frame's battlefield was drawn in. `--busy-combat` adds missile
  trucks to both armies, and for the local player a kbot lab building
  peewees and an air transport loading one, and selects the local army
  with selection boxes shown, so that the frames reach every kind of
  battlefield draw.
- `xrgb_conversion.hpp`, `xrgb_conversion.cpp`: each frame's RGB layers
  converted into the window's 32-bit pixels (0xffRRGGBB) as they are
  uploaded, through the display gamma's table when the gamma is not 1, in
  bands of 32 rows; when SDL's software renderer draws into a 16-bit RGB565
  window (`frame_texture_format`), the match's layers are converted into
  RGB565 pixels instead, the ones SDL would make of the 32-bit pixels, so
  that presenting copies them as they are. In the Full tier the world
  layer is the overlay canvas, cleared to a key colour, the lowest
  0xRRGGBB not in the palette (`overlay_key_colour`), and
  `convert_rgb24_keyed_overlay_argb` makes the overlay of it by that key,
  opaque exactly where the painters painted, in the same bands. The
  Runtime keeps a [job pool](../platform/job-pool/README.md)
  (`draw_pool_`) that this conversion, the terrain fill, the fog and the
  battlefield's draws (`world_draws.hpp`) run their bands on:
  `--draw-threads N` (1 to 32), else the `OA_DRAW_THREADS`
  environment variable, else the pool's default, one thread on a machine of
  one or two logical processors and otherwise one fewer than the
  processors, at most four. With one thread there is no pool and every
  band runs on the drawing thread. The conversion's, the terrain's and the
  fog's bands are fixed by the rows; the battlefield's are one a thread, and
  any number of them draws the same frames, so every count draws the same
  frames.
  `app-xrgb-conversion` checks each pixel's packing and gamma for rows of
  any width, that pools of 2, 3, 4 and 8 threads convert the same bytes,
  the key colour as the lowest outside a palette and the keyed overlay
  holding what was painted;
  `native-draw-threads` draws the seeded skirmish's fight frame by frame at
  zoom 1, 1.37 and 0.6, and a longer `--busy-combat` fight into its
  explosions and debris with enhanced anti-aliasing off and at 4x, on 1, 2,
  3, 4 and 7 drawing threads and checks that every count gives the same
  frames digest and world digest, and draws in one band a thread at zoom 1
  and in more than one elsewhere.
- `full_screen.hpp`, `full_screen.cpp`: Alt+Enter (Return or keypad Enter,
  either Alt key; Option on macOS), which switches the window between full
  screen and a window on every screen, during the movies and while a match
  loads; its Enter key's repeats and release reach no screen, even once Alt
  is let go. While macOS, X11 or Wayland is
  still switching the window, a second press switches from the mode last
  asked for. The window opens with `game_window_flags`: full screen on
  Windows unless `-d` is given, and at the display's own pixel density only
  where `decide_window_density` allows it (`render_host.hpp`). In full
  screen, whether on the desktop's display mode or on the one the Screen
  size setting picks, and while the
  window has the input focus, the pointer is kept on the game's screen, as
  in 3.1c (`keeps_pointer_on_screen`, `keep_pointer_on_screen`): it stops at
  the screen's edges, can rest on their last row or column of pixels, and
  never strays onto another monitor. Windows clips the cursor to the window,
  macOS confines it to the window's content, X11 grabs it inside the window
  and Wayland confines it there, each following the window's size and
  display. Switching to another program (Alt+Tab, Command+Tab), a dialog of
  the system's taking the focus, or Alt+Enter to a window lets the pointer
  go, and coming back to full screen with the focus holds it again; a window
  never holds it. The window events that may change this
  (`changes_pointer_bounds`) settle it in every event loop that takes
  Alt+Enter (the movies, a match loading and the game), and the window's own
  state settles it after the game drops pending input. The game lets the
  pointer go (`release_pointer`) before it shows an error or information
  box, before breaking into a debugger and on exit. A window that leaves
  full screen, by Alt+Enter, by the window system's own control or before a
  debugger break, comes back onto the display it was full screen on
  (`window_on_display`, `bring_window_on_display`): each side of its frame
  (the title bar and borders included where the window system reports them,
  as Windows and X11 do) that lies outside the display's usable area
  (without the menu bar, the dock or the taskbar, so that the title bar can
  be reached) moves 5% of that area's width or height inside it, and a
  window with no side outside stays where it is. A window too wide or too
  tall to fit between two margins keeps the left or top margin, so that its
  title bar and controls are on the display, and shrinks to fit between the
  margins; the screen is laid out again at its new size as after any resize.
  Displays left of or above the primary one, at negative coordinates, are
  handled alike. The window is checked once the window system has given it
  its place as a window, on the window's own events, for at most two
  seconds after it left: Windows places it as it leaves, macOS as it leaves
  its full-screen space, and an X11 window manager once it has put the
  window's decorations back (a window manager that reports no decorations
  leaves the window where it puts it). Wayland places windows itself and
  refuses to move them, so there the window stays where the compositor puts
  it. A maximised window is left as the window system fits it, and a window
  the player moves off the display later stays there. `app-full-screen` tests
  the keys, the modes, when the pointer is held and where a window goes on
  its display (each side out, corners, windows too large, displays at
  negative coordinates, edges exactly on the display's), over windows of
  SDL's dummy video driver that switch modes, lose and regain the focus and
  come back onto the display from off it, also with a usable area smaller
  than the display, and `--check-frontend-controls` presses Alt+Enter on a
  menu, over a message box and in a match, and checks that full screen
  holds the pointer and a window lets it go, and that the window comes back
  at its own size, or onto its display when it started off it (the dummy
  driver's display is smaller than the game's first window).
- `runtime_hud.cpp`, `runtime_match_hud.cpp`: the HUD.
- `runtime_messages.cpp`: the in-game message log (`Game.chat_lines`) drawn
  over the battlefield, and the speed and message part of `--check-navigation`.
- `runtime_console.cpp`, `runtime_console_debug.cpp`: the in-game console's
  host hooks, the debug grid, "Profile" bars and DebugBreak, and the console
  part of `--check-navigation`.
- `runtime_match_menus.cpp` also registers the load-game overlay
  (`register_load_game_screens`).
- The Open Annihilation settings ([oa/ui/engine_settings.hpp](../ui/engine-settings/README.md)):
  `engine_settings_state.hpp` and `runtime_engine_settings.cpp` read them at
  start, put them in effect, save them, and run the dialog for both of its
  hosts. `engine_settings_menu_host.hpp` and
  `runtime_engine_settings_menu.cpp` are the main menu's host: two overlays
  on the main menu, the OA button at the picture's bottom-right corner (its
  top-right corner while an extension's overlay stands over the main menu)
  under the extensions' overlays, and the dialog centred over the darkened
  menu above them, which takes every input while it shows. A press released
  over the button, Cmd+, on macOS or Ctrl+, elsewhere, and the macOS
  application menu's Settings… item open it; nothing opens over a message
  box or a frame a package owns. Enter is OK and Escape is Cancel; the key
  that closed the dialog does nothing more until it is released, so that a
  held Escape never reaches the main menu's own Escape, which ends the
  program. Another screen replacing the main menu closes the dialog as
  Cancel does. `engine_settings_match_host.hpp` and
  `runtime_engine_settings_match.cpp` are the in-game menu's host, and
  `runtime_engine_settings_app_menu.cpp` the application menu's item.
  `acceleration_status.hpp` and `acceleration_status.cpp`
  (`app-acceleration-status`) say what the dialog shows of the renderer:
  Hardware acceleration's status, first reason first, and whether nothing
  could help the run, which locks the row "Not available here". The
  machine's memory comes first, whatever the setting or the flags: it needs
  the render policy's 2 GiB threshold, `smallest_accelerated_memory`,
  1.75 GiB as the system reports it, so that a machine sold with 2 GB
  counts. Then come the setting and the flags, `SDL_RENDER_DRIVER` or a
  video driver with no window, a shared game or a replay, and whether the
  renderer is able. With the game's renderer the status follows the facts
  the tier is decided from (`tier_acceleration_facts`): probe items 1 to 3
  and the start-up function test decide whether the renderer is able, a
  failure in the run says the graphics driver failed and leaves the row
  within reach, whatever renderer it left, and in use the second line says
  what the graphics card does at its rung (`acceleration_reach`). A runtime
  without it does not look at the renderer: only SDL's software renderer
  is known unable. Vertical sync is locked
  on SDL's software renderer; on SDL's `direct3d` renderer
  (`render_probe::vertical_sync_resets_device`), where each change resets
  the graphics device and the game does not recover one the reset leaves
  lost; and once the renderer refused it.
  `Runtime::acceleration_facts` gathers those facts, both hosts refresh
  the status each frame while the dialog is open, and
  `Runtime::apply_vertical_sync` asks the renderer to wait for the display
  only when the setting in effect changes it, never while it stays Off.
  `--check-engine-settings` (`native-engine-settings`) drives them through
  the SDL presenter over a preferences file it empties first:
  `runtime_engine_settings_check.cpp` holds the main menu's part, with each
  look of the button and the darkened menu under the dialog compared pixel
  for pixel with what they should draw;
  `runtime_engine_settings_dialog_check.cpp` the dialog driven by the
  pointer, the wheel and the keys (every section, scrolling, each setting in
  effect at once, Vertical sync read back from the renderer, OK, Cancel,
  Restore defaults and the keys they save) and the main menu with the
  button and the dialog as 640x480, 1280x720, 1920x1080 and 2560x1080
  windows show them, Graphics at its top and its end;
  `runtime_engine_settings_match_check.cpp` the in-game menu's button and
  dialog at those sizes, with the locks of a game played alone and of a
  shared game, Hardware acceleration set to Full and to Basic in a shared
  game, where each waits for the game's end, and the wheel scrolling the dialog, not the
  battlefield; and `runtime_engine_settings_wiring_check.cpp` each setting
  taking effect in a match, Escape's order, Vertical sync, the lock
  either acceleration flag puts on Hardware acceleration and the row set
  to Basic, Full and Off among them. The check runs with `--force-capable`,
  which lifts the software renderer's lock on both rows; every other step
  leaves Hardware acceleration Off, so every frame it compares is drawn on
  the processor. Both dialog steps also scroll a
  section of nine rows, `engine_settings_tall_section.hpp`, shown in place
  of the open section's rows; the wheel events are the check host's
  (`check_host_input.hpp`). With `--snapshot`, the check writes each of
  those frames beside the named file.
  `native-engine-settings-determinism`
  (`tools/check_native_engine_settings.py`) checks that every setting at its
  default plays the game as it plays without any, and that the settings that
  change only the look or the input, enhanced anti-aliasing among them,
  leave the world alone.
- `runtime_notices.cpp`: the notices for the entries the game data cannot
  support, such as skirmish, multiplayer and the missions after the last in
  the Total Annihilation demo (1997): DEMOMSG.GUI when the data can draw it,
  else a message box. `web_link.hpp` and `web_link.cpp` hold the seam the
  notice's website button opens its address through: the player's browser,
  or in a run nobody watches only a record of the request; the runtime keeps
  the hooks it chose and that record in `web_link_state.hpp`.
  `runtime_notice_check.cpp` holds the navigation and load-save checks over
  such data.
- `map_picture_state.hpp`: the map selection's picture, the selected map's
  minimap and where it is fitted in MAPPIC, which `runtime_skirmish_host.cpp`
  loads and fits and the frontend frame draws.
- A screen package registers through `screens.inc` rather than adding its
  cases to `runtime.cpp`.
- `check_host.hpp`, `runtime_check_host.cpp`, `check_host_input.*`: the
  check host, through which a check an extension runs drives the running
  game (see [Extensions](#extensions)); `app-check-host` tests its table and
  the parts that need no running game.
- `renderer_records.hpp`, `renderer_records.cpp` (`oa-app-renderer-records`)
  and `renderer_state.hpp`, `renderer_state.cpp` (`oa-app-renderer-state`):
  the renderer records, which the renderer host keeps (below). What the engine has
  seen of each render driver on this machine is kept in `renderer-state.conf`
  beside the preferences file: a strike against a driver for a stage the game
  died in, or a failure seen while running, which becomes a record only when
  the same is seen at the next start or in the next run (`failed-driver`,
  which the walk of SDL's drivers skips, never for `software`;
  `accelerated-unusable`, which keeps the driver on the standard tier;
  `full-unusable`, from a left-over trial of Full's path, `path full`, or
  a repeated `card` strike, which keeps the driver on the Basic tier where
  Full is asked for, unless `--hardware-acceleration=full` was given), or at
  the first left-over trial on Windows before Vista and on Linux
  (`crash_evidence`); the adapter they were written under, the remembered
  step-down rung, the `native-density` key, the trial of a stage under way and
  the told mark of the main menu's notice. On a machine under 2 GiB
  (`RecordRules`) no trial is written and nothing of the accelerated tier is
  struck or recorded, and what a run with more memory left of it stays for a
  start from 2 GiB to judge. Strikes and records of another adapter or engine
  version are dropped, and `clear_failures` gives every driver a fresh try, as
  Off and back and Restore defaults do. The sentinel of the stage a start has
  reached is kept apart in `renderer-sentinel.conf`; `sentinel_step` moves it
  and the trial through a run, from `create` to `running` and each path's
  first frames, with none under `SDL_RENDER_DRIVER`. Their text and rules are
  pure (`renderer_records.hpp`, tested by table in `app-renderer-records`);
  `RendererState` reads and writes the files: every write best effort, logged
  once on failure with the records kept in memory, except the trial's, whose
  failure the caller is told of; the records written only when one changes,
  flushed with the folder synced, and not while a match runs; while Off then
  On's clearing waits for OK, what it cleared kept in the file with what was
  struck since, and put back with it by Cancel (`confirm_clear`,
  `restore_failures`); a trial written with the strike the last run left; the sentinel rewritten in place
  unflushed; a clean exit erasing the run's trial, writing the records left
  and deleting the sentinel; a missing or garbled file read as empty. With a
  named `--preferences-file` they live in memory, and under
  `SDL_RENDER_DRIVER` nothing is read or written. `app-renderer-state` tests
  the files in scratch folders, read-only and garbled ones among them. The
  main menu's notice of a new record (`next_notice`, `notice_action`) is
  told once (`Runtime::tell_renderer_records`, `runtime_notices.cpp`): once
  the main menu, its own, has shown for a frame and stays, with no
  multiplayer signal waiting to leave it, so that a start with `-n`, whose
  signal waits for the frontend's next pass, or with `--play-demo`, which
  passes the main menu, waits for it to show again; a run nobody watches
  notes the request and leaves the record untold, and
  `--check-renderer-ladder` notes it and marks it told.
- `video_capture.hpp`, `video_capture.cpp`: `--capture-video`, the
  developer's capture of the window's frames and the game's sound as an MP4
  video through the `ffmpeg` program; `runtime_showcase.cpp`: the scripted
  runs `--showcase` plays, among them `skirmish-battle`, which plays a
  skirmish's fight for a minute on the game's own loop and reports the ticks
  and frames a second it kept. [docs/capture.md](../../docs/capture.md)
  describes both. Benchmarks, `--frame-rate` runs, headless saved-game runs
  and the battle end with the memory report, its peaks the largest the
  system saw (`oa/platform/memory_status.hpp`).
- `screen_size.hpp`, `screen_size.cpp`: the settings read before the window
  opens (`start_settings`), with the defaults of a light machine
  (`oa/platform/machine.hpp`): the Screen size, the window opened at that
  size and full screen given the display mode nearest it, and Hardware
  acceleration, which either flag decides over
  (`hardware_acceleration_asked`).
- `render_host.hpp`, `render_host.cpp` (`oa-app-render-host`, with
  `graphics_report.cpp`): the game's renderer. `walk_render_drivers` acts
  on the render policy's walk through hooks (`CreationHooks`): it sets the
  framebuffer hint, tries each driver, logs each refusal and, when the
  records would leave nothing able to present, that the walk starts again
  from the top. `RendererHost` gives it SDL's calls, keeps the renderer,
  what the probe found of it and the walk's attempts, and puts back the
  floating-point settings the game started with once the renderer is made
  and described. `HostDisplay` (`main.cpp`) owns one, and the runtime
  borrows it with the renderer: `render_run.hpp`'s `Runtime::RenderRun` is
  the runtime's own renderer state, made only when it is handed a window,
  a renderer and its host, so a headless run, a window the runtime made
  itself and a loopback check's second runtime have none. The host keeps
  the run's renderer records: `HostDisplay` has it read them before the
  walk (`RendererHost::open_records`, `RecordsPlace`), beside the player's
  own preferences file or in memory with a named one, applying what the
  last run left behind; the walk skips the drivers they hold failed and
  walks again with them ignored, for the run, where that leaves nothing
  able to present; the sentinel stands at `create <driver>` before each
  attempt and `standard <driver>` while the probe reads the renderer, and,
  where the walk passed over no driver by record, the adapter the probe
  describes is noted, since each driver names it in words of its own. The
  runtime moves it on: the first accelerated frame, the start-up stage
  passing after 60 presented frames and 2 s on the host's `StageClock`
  (`note_presented_frame`), and each accelerated path's first use
  (`begin_path`, `begin_accelerated_path` at the first frame drawn through
  a scene, overlay or prescale target, which is made then), a path whose
  trial cannot be written dropping the tier, and switching the tier off
  closing a path's stage under way. Strikes and records a match
  makes are written when it ends, and `HostDisplay`'s destructor ends the
  records cleanly at every exit through `main` (`finish_records`).
  `walk_rebuild_drivers` and `RendererHost::rebuild` make the renderer
  again after a failure, striking it against the driver that failed;
  `RenderFaultHooks` are what `--check-renderer-ladder` forces, among them
  the machine's memory, how a left-over trial counts and the name the
  records keep the renderer under.
  `RendererHost::decide_start_tier` decides the first frame's tier and
  logs the start-up line, running the start-up function test
  (`run_function_test`) where the tier could be accelerated; the facts the
  tier is decided from stay with the host (`tier_inputs`), and a rebuild
  drops the accelerated tier for the run. `app-render-host` runs the
  function test on SDL's software renderer, and sees it fail where its
  faults draw a reduction NEAREST (`FunctionTestFaults`).
  `runtime_renderer.cpp` holds the runtime's side: the render events,
  present errors and rebuilds, a lost device's wait and the stall rule;
  `runtime_tier_watch.cpp` the accelerated tier's memory guard and
  step-down; `runtime_renderer_ladder_check.cpp` the ladder check.
- `scaled_world.hpp`, `scaled_world.cpp`: `TiledTexture`, a streaming
  texture made as one texture within the renderer's limit and as tiles
  with one-texel gutters beyond it, for the standard tier's window-size
  layers and the accelerated tier's scene and overlay; `PresentError` and
  `AccelerationError`; and the accelerated tier's drawing on the card
  (`PrescaleTarget`, `draw_scaled_world`, `sharp_draw`, `probe_pixelart`).
  `app-scaled-world-software` checks on SDL's software renderer that tiles
  read back as one texture does, and the card's drawing against that
  renderer's own filters.
- `render_policy.hpp`, `render_policy.cpp` (`oa-app-render-policy`): the
  decisions of hardware-accelerated presentation as pure functions, with
  no SDL, no files and no clock, of which the game uses so far the walk of
  the render drivers and its rebuilds (`render_host.hpp`), the texture
  limit, in the line it logs at start and for the tiles, the capability,
  the tier each frame is drawn in and the step that acts on it
  (`step_tier`, `tier_action`, `forget_failures`),
  the shared-game gate, the starting rung, the stall rule, the count of
  device resets (`note_device_reset`), the layers' texture formats
  (`layer_formats`), the step-down and the rung below a buffer the memory
  guard refuses, and the tiles of a texture beyond the renderer's
  limit. The walk
  of SDL's render drivers in SDL's own order, skipping drivers recorded as
  failed (`failed_driver_list` of the renderer records) but never
  `software`, with the framebuffer hint set before
  `software` and never empty, a second walk with the records ignored
  whenever they would leave nothing able to present, and SDL's own call
  under `SDL_RENDER_DRIVER` (`start_creation`, `start_rebuild`,
  `next_attempt`); whether probe items 1 to 3 found the renderer capable
  (`assess_renderer`, `texture_limit`); the tier each frame is drawn in,
  standard (today's renderer) or accelerated, with the reason
  (`decide_render_tier`): the standard tier whatever the flags on a
  machine with under 2 GiB of physical memory, or whose memory the system
  does not report, where a machine that reports 1.75 GiB
  (`smallest_accelerated_memory`) counts as having 2 GiB; when the
  start-up function test may run, never under 2 GiB; and the gate that
  keeps a shared game or a replay from starting anything until it ends;
  present stalls;
  the rung a machine starts at on the step-down ladder from 2 GiB, its
  budget sized from its processors and kind and never from its memory
  (`start_budget`), magnify off before Vista and at budget none, and the
  blend only above 4 GiB and never on a driver that excludes it
  (`start_rung`); the remembered rung (`resume_rung`), and the step-down
  itself, fed steady frames with their ticks' time taken out
  (`feed_step_down`) against the lower of the loop's paced rate and 60
  (`step_target_rate`), and only the match frames the accelerated tier
  presented and the loop paced, never a lone wait of a second or more
  (`feeds_step_down`, `feed_presented_frame`, `frame_kind`), each step
  described for the log (`describe_step`); the rung the tier stays on
  where the memory guard refuses a buffer (`rung_without`); the chrome's
  filter (`chrome_filter`), the magnified scene's (`world_filter`) and the
  prescale budget; and the tiles of a texture beyond the renderer's limit
  (`plan_tiles`). The names of the drivers' graphics interfaces stay with
  the platform: the policy takes each driver's traits (`DriverTraits`).
  `app-render-policy` tests them all by table. What a left-over sentinel
  or trial, or a failure while running, counts for, and the sentinel and
  the trial through a run, are the renderer records' (above). The memory
  guard is built into the policy's library (above); the world's scaling
  (`world_scaling`), the native-density rule, which needs 2 GiB as the
  accelerated tier does, and the probe's report join the policy with the
  code that uses them.
- Director scripts ([docs/director.md](../../docs/director.md)):
  `runtime_director.cpp` runs `--generate-script` (the recording replayed
  undrawn through the extension that replays it, its timeline recorded and
  the shots planned) and `--render-script` (the recording replayed tick by
  tick, the shots drawn, the sound mixed offline, the chunks written), and
  `--check-director-render`, which renders a small script over the headless
  skirmish. `runtime_director_view.cpp` and `director_state.hpp` are director
  mode (`director_presentation.hpp`): the frame drawn from the director's
  camera at the output size, the battlefield alone, the match's sounds and
  every player's unit announcements sent to the director's sound hooks;
  as everywhere, nothing drawn changes the match, and
  `--check-director-view` checks that a drawn and an undrawn replay reach
  one world. `director_output.hpp` and
  `director_output.cpp` (`oa-app-director-output`) write a render's files
  (each chunk's frame manifest and sound, the run manifest), run `ffmpeg` on
  the chunks and join them, and read and write the `.oamovie` bundle;
  `app-director-output` tests them without an encoder.
- Frames between ticks: `presentation_interpolation.hpp` and `.cpp` keep
  each unit's pose (place, heading and the pieces its script moved and
  turned), the projectile pool and the debris table at the last two ticks
  the presentation saw, and blend them; a batch of ticks one frame ran
  (above normal speed) blends from the tick before the batch.
  `match_models.hpp` holds the match renderer's state with them
  (`MatchModels`). `render_match_surface` draws at `presentation_alpha()`,
  which the application loop's pacing chooses for each frame: at 1 the
  tick as it is, below 1 each moved unit from copies of its record and
  model instance placed part of the way from the tick before, with a draw
  state of their own, while the match's own pieces are rebuilt as a whole
  tick's draw rebuilds them; projectiles, debris, fragments, particles,
  health bars, order lines and a tracking camera follow. The director
  draws its frames between ticks so. The debug grid draws its random
  numbers from its own generator, never from the match's streams, and
  draws the numbers a tick's first draw took again on the tick's later
  draws (`DebugGridRandom`), so that showing it never changes the game and
  a tick drawn more than once shows one grid. `app-presentation-interpolation`
  tests the blends and the grid's numbers, and `--check-interpolation`
  (`runtime_interpolation_check.cpp`) the frames.
- Units of other machines' players: `advance_match_clock` has the unit
  playout (`src/present/unit-playout`, `Runtime::unit_playout_`) read the
  match after each step, whether the engine or an extension ran it, and the
  director after each tick of a recording it plays; `MatchPresentation::playout`
  gives the draw paths its places. With each observation it reads what each
  unit's movement holds (`unit_motion` in `runtime.cpp`: the movement
  record's speed and velocity, the route head its owner shared with the
  mirrored navigator, the air driver's point and seek goal). A unit of a
  player in use with `OA_PLAYER_STATUS_MIRRORED` is drawn from copies of its
  record on every frame, whole ticks and a paused game included, placed,
  turned and tilted where its owner's playout clock has it at the frame's
  moment (`mirrored_pose`, `playout_moment`): near its newest record, moved on
  ahead of it between records, with what new records change faded in; its
  pieces are placed between their two ticks' poses on every frame, even when
  its records moved it by a jump (`UnitMotion::pieces_moved`,
  `blend_unit_pieces`). Its shadow, selection box, health bar and digits, the
  order lines that start at it, the culling and the far-to-near order of the
  frame, a camera tracking it and the pointer's pick (against the frame last
  drawn, `MatchPresentation::drawn_moment`) all take that place; a unit it
  carries is moved as far as it is drawn from its simulated place. Whether a
  unit is seen, the on-screen list, the radar, projectiles, nanolathe
  streams, explosions and wrecks, and every simulation read keep the
  simulated place. With no such player the frame is drawn as before.
  `--check-unit-playout` (`runtime_unit_playout_check.cpp`) takes the
  skirmish's other player as another machine's, whose records, with the
  runner's speed and route head, arrive within the steps in 3.1c's bursts,
  and checks at 120 frames a second that its runner moves on every frame by
  about its pace, within two ticks of its simulated place on average, on a
  whole tick's frame too, with the tracking camera and the pick where it is
  drawn; that the local runner and the world are as without the playout;
  and that, with no such player, every frame is.

## Game folder

`game_directory.cpp` finds the installation: `--game-dir`, else the folder
remembered in the preferences, else the folder dialog
(`game_directory_dialog.cpp`) until the player picks a usable folder, which
is then remembered. A folder that holds no game archives but holds the
installer of the Total Annihilation demo (1997) is usable too:
`demo_installer.cpp` recognises the installer by its size and SHA-256, never
by its name, and unpacks the game data archive it carries, checked by its
SHA-256, into `demo-1997` in the per-user data folder (`--data-dir` names
another), where later starts check it and use it again; while it passes that
check the installer is recognised by its size alone, since only unpacking
reads it. That folder is the installation, with the checked archive as its
only archive whatever else the folder holds, and the folder the player chose
is the one remembered. Temporary files an unpacking that stopped part way
left there are removed on a later start. The installer is only read, never
run; any other program in the folder is refused with a message saying it is
not the release the engine recognises, and a failed unpacking or a full disk
is reported the same way.
`DemoRelease` holds everything that identifies the release, in one place, so
that the tests (`demo_installer_test.cpp`) substitute a synthetic one.

## Extensions

`extension.hpp` is the table of hooks through which libraries linked into
`oa-game` extend it: long options and game switches, start-up and
shutdown, screens, the frontend's entry (its game name and nickname), run
modes, per-frame work, match events, the Pause key, the speed keys and
the GAME slider, the frontend's application modes, the loading's
progress, the team panels' host (a tournament game withholds CONTROL),
requests to close the window, the label a match's return names in its
menus, whether the preferences keep the stored password, recordings to
replay, console commands and checks. Each such library is an extension.
The engine registers its own, network play ([below](#network-play)); a
project that builds the game registers further ones after adding the
engine, each with the function that fills its table:

```cmake
oa_add_extension(<target> INIT <function> [SWITCHES <letters>] [GAME_FILES <COMMAND ...>])
```

`cmake/OaExtensions.cmake` describes the arguments. When the configure
ends, the engine lists the registered extensions, each after every
registered extension its library links and otherwise in registration
order, compiles the list into `oa-game` and links the libraries.
`main()` has every extension fill a table of its own before the command
line is parsed, and `ExtensionList` (`extension_list.hpp`) combines the
tables into the one table the runtime calls, by the rules `extension.hpp`
states: most hooks are called for every extension in list order;
`shutdown` and `release_runtime` in reverse; the hooks that take something (an option, a
switch, a run, a close request, a recording) ask the last extension in the
list first, since it builds on those before it; answers are combined; and
`frontend_game`, `frontend_states` and each entry of the hosts the
extensions fill belong to one extension at most, so that a second one
stops the start or the call with a message naming both. Every hook no
extension fills keeps the engine's behaviour. A reserved game switch no
extension takes is refused as "not handled by this build". An extension that includes `runtime.hpp` builds against
`oa::extension-sdk`, the include directories and libraries an extension
may use; one that needs only the table links `oa::app::headers`. The
engine calls every hook through one guarded call, `call_hook`
(`hook_call.hpp`), which catches what the hook throws before it reaches
engine code; `hook_call.hpp` lists each hook's handling, which
`extension.hpp` documents: the error is raised as the engine's own at the
call, which ends the game except where a match start the frontend falls
back from catches it; or it is reported and the engine carries on as for
a null hook; or the hook must not throw, and is reported when it does.
`hook-calls` (`tools/check_hook_calls.py`) fails when engine code calls a
hook pointer itself, and `app-hook-call` tests each handling with hooks
that throw. `OA_EXTENSION_API_VERSION` in `extension.hpp` numbers
the table's contract; an extension checks its typed copy,
`oa::app::extension_api_version`, with `static_assert`, and any change to
the contract raises it (its comment says what counts). The recorder test
extension checks it too, beside its count of the table's hooks.

Version 9 adds `open_recording`, through which `--generate-script` and
`--render-script` replay the recording a director script names. The
engine hands the extensions the recording's name and bytes
(`RecordingInput`), asking the last in the list first; the one that
replays recordings of that kind starts the recording's match through the
engine's own match start and returns what the recording holds
(`RecordingInfo`: the tick after its last, when known, its length, the
player it is watched from, how many players it holds and whether the
installation's unit definitions differ from its own) and the replay's
hooks (`ReplayHooks`). The engine then runs none of that match's ticks
itself: it calls `step` once for each tick, `status` for where the replay
stands (`RecordingStatus`: the tick, whether everything recorded has been
replayed, whether the replay is clean and its periodic records paced, its
errors and the last one's text) and `close` once, before it tears the
match down. An extension that does not recognise the recording declines
and the next is asked; one that recognises it but cannot replay it
throws. Each extension asked starts from a zeroed replay and information,
and only the one that takes the recording fills the caller's.

Version 11 adds `release_runtime`, through which an extension frees what
it keeps for one runtime. The engine calls it as each runtime is
destroyed, the game's own and the second one a check creates beside it,
also when the runtime's constructor throws, at the point in the runtime's
teardown where its match is still whole; the runtime is passed only to say
which one goes. Network play keeps its state for each runtime this way.

`app-extension-list` checks each rule of the combined table over two test
extensions, and `tests/extension/` tests the boundary itself.
`extension-layout-mismatch` links a unit that sees `Runtime` with members
`oa-game` does not have and expects the link to fail. A build configured
with `-DOA_RECORD_EXTENSION_HOOKS=ON` registers two test extensions after
network play: the recorder, which fills every hook with a recorder but
`frontend_game` and `frontend_states`, which network play fills, and adds
one `Runtime` member, and the follower, whose library links the
recorder's and which fills the same hooks; the follower is registered
before the recorder and listed after it. `extension-hooks-options` and,
over the installed game, `extension-hooks-game` check that the game calls
every hook of both but `disconnect_text`, which only a shared match
reaches (of `match_event`'s events they see `finished`, `torn_down` and
`results_released`), in the order the rules set, and that a follower that
fills `frontend_game` beside network play stops the start; the navigation check's Pause
key reaches `pause_changed`, its speed keys `speed_changed` and its menus
`app_mode_set`, a skirmish's loading `load_progress`, `team_panel_host` and
`return_label`, its preferences write `keep_stored_password`,
`--check-match-dialogs`'s close requests `close_requested` and its GAME
slider `speed_changed`, a `--generate-script` run over a file no
extension replays `open_recording`, asking the follower first, and the
end of a headless skirmish `release_runtime`, the follower first.
With `--record-quit STATUS` the recorder keeps the screen services an
overlay is given, stops the sounds, plays BGM on the alternate route,
asks for a frontend pass and ends the run through `quit`, which must exit
with STATUS. The recorder drives the check host too, through its entries
alone: after network play's `--check-multiplayer-menu` check it clicks
MULTI twice, taking it over each time (`select_multiplayer` answers
`taken`) so that the main menu stays up, with the cursor, the clock, a
composed frame, a sound's file and the preferences checked on the way; and with `--record-check-host` it takes the headless run for the
entries that work without a window, down to a close request, which ends
the run, and a frame, which needs the window. The navigation check itself puts probes in place of the
hooks to check what the engine does with their answers: a close request
answered or declined, quit's status, one frontend pass for two requests,
a query binding, the launch's nickname, the return label kept as a match
starts and quit leaving a match, with the preferences open over it,
first. CI builds that configuration as a job of its own, but has no game
installation: there `extension-hooks-game` skips, and only the option
hooks and the hook list are checked. The rest of the hook coverage runs
only where `OA_GAME_DIR` is set; run it there before changing the table.

A check an extension runs, from `run_mode` or `check_multiplayer_menu`,
drives the running game through the check host (`check_host.hpp`), as the
engine's own checks drive it: `check_host(runtime)` returns a table whose
entries hand the game SDL events and left-button pointer events at canvas
points (placed in the window as the game shows the canvas, or taken as they
are without one), run a frame of the main loop (with the window up) or a
pass of the screen packages, compose the frame and return it, hold the
frontend clock at a tick and release it, and read the cursor, the window's
SDL id, the screen shown, the frontend's state, whether a package owns the
main menu's frame, a gadget of the shown layout by name, the game's files,
the file a sound name plays, and write the preferences.
`runtime_options(runtime)` returns the command line's options. The header
includes no other engine header, so such a check needs only
`oa::app::headers` and never a private name of `Runtime`. The check host is
not part of the extension table, which `OA_EXTENSION_API_VERSION` numbers
alone.

An extension reaches `oa-game` only through these hooks and declared
headers. When it needs something the table does not offer, add a hook or
declare a header for it here; never give it a new `Runtime` member or
friend, or another of `Runtime`'s private names.

Network play adds no members to `Runtime`. Its state for one runtime, the
network session, the replay of a recording and the match report, is its
own: `NetworkPlay` (`netgame/network_play.hpp`), which network play's
extension creates the first time a hook is called for a runtime, finds
again for each later hook and frees through `release_runtime`.
`NetworkPlay` is still a friend of `Runtime` and reaches some of its
private names until hooks and declared headers cover everything:
`tools/check_runtime_surface.py` (`netgame-runtime-surface`) holds the
private `Runtime` names network play uses, each with its number of uses,
to `tools/runtime-surface-baseline.json`, which may only shrink. The
engine's `runtime-surface-names` test fails when a change to `runtime.hpp`
leaves the baseline naming something that is no longer a private name of
`Runtime`. Renaming one of those names replaces the old name with the new
one in the baseline, keeping its uses, in the same change: the one addition
the baseline takes. Removing one, or making it public, drops it.

One extension that is not part of the engine may still add members to
`Runtime`: the build names its header in the `OA_RUNTIME_EXTENSION_MEMBERS`
CMake variable, and the engine compiles `oa-game` and, through the SDK, the
extension with that definition; `runtime.hpp` includes the header inside
the class. That header declares the extension's own friend of `Runtime`,
under a name other than `NetworkPlay`, which is network play's: two
definitions of one friend in one game do not link as two. The check holds
such a header's declarations, which must be plain declarations with no
preprocessor directive, to the same kind of baseline. A unit compiled
without the definition, or with it where `oa-game` has none, fails to link
(`extension_members.cpp`). In the engine's own tree only the recorder test
extension (`tests/extension`) uses it, beside network play, with its
friend `RecorderExtension`.

## Network play

`netgame/` is network play's extension, `oa-app-netgame`, which the engine
always builds and registers. It takes the 3.1c network switches
`-e -h -n -p -t`, binds the network session (`src/netgame`) to the
multiplayer screens (`src/ui/frontend-multiplayer`), launches the
match from the battle room and runs it over the network, and replays
recorded games (`.tad`, `src/formats/tad` and `src/session/demo`) through
`open_recording` and `--play-demo FILE.tad`. Its long options include
`--net-loopback-check N`, which hosts and joins a match in one process
over 127.0.0.1 and compares both worlds after N ticks,
`--check-recording-hook` and `--check-host-not-found`;
`--check-multiplayer-menu` runs its check of the multiplayer screens.
MULTI on the main menu asks the extensions (`select_multiplayer`), and
network play answers by moving the frontend to its multiplayer states;
when no extension answers, MULTI does nothing. Over game data with no
multiplayer map, such as the 1997 demo's, MULTI asks no extension and
shows the missing-content notice, and `--check-multiplayer-menu` checks
that notice instead.
Extensions built on network play use its own table,
`oa/app/netgame/extension_api.hpp`, numbered by
`OA_NET_EXTENSION_API_VERSION`.
[docs/development/testing.md](../../docs/development/testing.md#network-play)
lists its tests.
