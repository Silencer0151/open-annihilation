# src/app

The game application: the native runtime that hosts the frontend and
offline match. The `oa-game` target builds it as `open-annihilation`
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
- `runtime_match_menus.cpp`: the in-match menus. A dialog opened over the
  match HUD (the exit menu, the surrender confirmation, RESTART.GUI, the
  Game Settings sheet, the removal question) is placed as 3.1c's panel
  loader places it and keeps the panel it opened over drawn under it,
  darkened where 3.1c darkens the panel below (`open_match_dialog`). A
  dialog centred on the whole screen is drawn at the canvas's pixels over
  the side column too (`match_dialog_side_`); while
  the in-game menu or the tab menu is open the panels show their keyboard
  focus. `panel_shade.hpp` and `panel_shade.cpp` hold the darkening of the
  panel below, which the load and save dialogs share.
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
  save stores the bit as the match holds it. `app-match-clock` tests both.
- `frame_pacing.hpp`, `frame_pacing.cpp`, `frame_stats_panel.hpp`,
  `frame_stats_panel.cpp`, `runtime_frame_stats.cpp`: the
  application loop's frames, apart from the simulation's 30 ticks a second.
  The loop draws up to `--max-fps` frames a second (120 unless it says
  otherwise; 0 for no limit, and never fewer than 40, so that a frame on
  time never runs two ticks at normal speed), each frame standing for its
  time on an evenly spaced run of frames (`FramePacer`), with a precise wait
  between them; a frame that ends late starts the next at once and the run
  goes on from it, never with frames bunched to catch up. While nothing
  moves on its own (no stepping match, no camera motion, no input for half
  a second), a paused multiplayer match among them, it draws 30 a second,
  and an event ends the wait at once. The match clock steps to each
  frame's time, and the frame is drawn the fraction of the way between the
  state before the last batch of ticks and the state after it that its time
  stands for (`presentation_alpha()`, `next_presentation_alpha`): never
  past the current tick, whole ticks while the match is paused, waits on
  another machine or catches up, on a check's fixed clock and for a film
  frame, and 1 again once the frame is drawn, so that every other drawing
  shows whole ticks. The unit drawing adds what it drew to `frame_draws_`
  (`FrameDrawCounts`). The camera scrolls and the zoom eases for each
  frame's real time (`scroll_distance`), so they move a steady amount every
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
  draw and present times' least, mean and most in columns, and the units
  the last frame drew. Each time is graded as it is taken, against the
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
  widest texts the table shows (`frame_stats_widest_table`), so nothing in
  it moves from frame to frame, and places it at the HUD's text scale, or
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
  the history and the grades over a fake clock, and
  `app-frame-stats-panel` the panel's rows, columns, notes and graph, its
  place and scale, the bars and lines and each grade's colour. `--frame-rate FPS` with `--match-ticks` plays
  the headless skirmish frame by frame on a clock of its own, moving the
  camera and stepping the clock as the loop does and drawing each frame as
  the loop does; `--frame-log FILE` writes each frame's time, tick,
  fraction, camera and a unit it follows, where the simulation holds it and
  where the frame drew it, `--scroll-camera` sweeps the camera's scroll
  right and back over the army, `--march` sends the local army south and
  `--follow` tracks the unit the log follows. `native-frame-rate` checks
  that 30, 60, 120 and 144 frames a second write one trace stream and reach
  one world digest; that at 120 the camera moves evenly, each frame shows a
  quarter of a tick more, and the unit drawn moves on nearly every frame,
  none carrying more than half the most it moves in a tick; and that a
  tracked unit is drawn at one place of the screen on every frame.
- `full_screen.hpp`, `full_screen.cpp`: Alt+Enter (Return or keypad Enter,
  either Alt key; Option on macOS), which switches the window between full
  screen and a window on every screen, during the movies and while a match
  loads; its Enter key's repeats and release reach no screen, even once Alt
  is let go. While macOS, X11 or Wayland is
  still switching the window, a second press switches from the mode last
  asked for. The window opens with `game_window_flags`: full screen on
  Windows unless `-d` is given. `app-full-screen` tests them, and
  `--check-frontend-controls` presses Alt+Enter on a menu, over a message
  box and in a match.
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
  `--check-engine-settings` (`native-engine-settings`) drives them through
  the SDL presenter over a preferences file it empties first:
  `runtime_engine_settings_check.cpp` holds the main menu's part, with each
  look of the button and the darkened menu under the dialog compared pixel
  for pixel with what they should draw;
  `runtime_engine_settings_dialog_check.cpp` the dialog driven by the
  pointer and the keys (every section, each setting in effect at once, OK,
  Cancel, Restore defaults and the keys they save) and the main menu with
  the button and the dialog as 640x480, 1280x720, 1920x1080 and 2560x1080
  windows show them; `runtime_engine_settings_match_check.cpp` the in-game
  menu's button and dialog at those sizes, with the locks of a game played
  alone and of a shared game; and `runtime_engine_settings_wiring_check.cpp`
  each setting taking effect in a match, Escape's order among them. With
  `--snapshot`, the check writes each of those frames beside the named file.
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
  or in a run nobody watches only a record of the request.
  `runtime_notice_check.cpp` holds the navigation and load-save checks over
  such data.
- A screen package registers through `screens.inc` rather than adding its
  cases to `runtime.cpp`.
- `check_host.hpp`, `runtime_check_host.cpp`, `check_host_input.*`: the
  check host, through which a check an extension runs drives the running
  game (see [Extensions](#extensions)); `app-check-host` tests its table and
  the parts that need no running game.
- `video_capture.hpp`, `video_capture.cpp`: `--capture-video`, the
  developer's capture of the window's frames and the game's sound as an MP4
  video through the `ffmpeg` program; `runtime_showcase.cpp`: the scripted
  runs `--showcase` plays. [docs/capture.md](../../docs/capture.md)
  describes both.
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
The project that builds the game registers it after adding the engine,
with the function that fills its table:

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
`shutdown` in reverse; the hooks that take something (an option, a
switch, a run, a close request, a recording) ask the last extension in the
list first, since it builds on those before it; answers are combined; and
`frontend_game`, `frontend_states` and each entry of the hosts the
extensions fill belong to one extension at most, so that a second one
stops the start or the call with a message naming both. Every hook no
extension fills keeps the engine's behaviour, the game without
multiplayer, which is the whole game when no extension is registered. A
reserved game switch no extension takes is refused as "not handled by
this build". An extension that includes `runtime.hpp` builds against
`oa::extension-sdk`, the include directories and libraries an extension
may use; one that needs only the table links `oa::app::headers`. An
exception a hook throws ends the game except on the paths
`extension.hpp` lists: a match start the frontend falls back from, and a
simulation tick. `OA_EXTENSION_API_VERSION` in `extension.hpp` numbers
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

`app-extension-list` checks each rule of the combined table over two test
extensions, and `tests/extension/` tests the boundary itself.
`extension-layout-mismatch` links a unit that sees `Runtime` with members
`oa-game` does not have and expects the link to fail. A build configured
with `-DOA_RECORD_EXTENSION_HOOKS=ON` registers two test extensions: the
recorder, which fills every hook with a recorder and adds one `Runtime`
member, and the follower, whose library links the recorder's and which
fills every hook but `frontend_game` and `frontend_states`; the follower
is registered first and listed second. `extension-hooks-options` and, over
the installed game, `extension-hooks-game` check that the game calls
every hook of both but `disconnect_text`, which only a shared match
reaches (of `match_event`'s events they see `finished`, `torn_down` and
`results_released`), in the order the rules set, and that a follower that
fills `frontend_game` too stops the start; the navigation check's Pause
key reaches `pause_changed`, its speed keys `speed_changed` and its menus
`app_mode_set`, a skirmish's loading `load_progress`, `team_panel_host` and
`return_label`, its preferences write `keep_stored_password`,
`--check-match-dialogs`'s close requests `close_requested` and its GAME
slider `speed_changed`, and a `--generate-script` run over a file no
extension replays `open_recording`, asking the follower first.
With `--record-quit STATUS` the recorder keeps the screen services an
overlay is given, stops the sounds, plays BGM on the alternate route,
asks for a frontend pass and ends the run through `quit`, which must exit
with STATUS. The recorder drives the check host too, through its entries
alone: after the engine's `--check-multiplayer-menu` check it clicks MULTI,
closes the box it opens with Return and clicks it again, with the cursor,
the clock, a composed frame, a sound's file and the preferences checked on
the way; and with `--record-check-host` it takes the headless run for the
entries that work without a window, down to a close request, which ends
the run, and a frame, which needs the window. The navigation check itself puts probes in place of the
hooks to check what the engine does with their answers: a close request
answered or declined, quit's status, one frontend pass for two requests,
a query binding, the launch's nickname, the return label kept as a match
starts and quit leaving a match, with the preferences open over it,
first. CI builds that configuration as a job of its own, but has no game
installation: there `extension-hooks-game` skips, and only the option
hooks and the hook list are checked. The rest of the hook coverage runs
only where `OA_GAME_DIR` is set, locally or in a private run; run it there
before an extension moves its engine pin.

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

Until hooks and declared headers cover everything, one extension may still
add members to `Runtime`: the project that adds the engine names a header
of them in the `OA_RUNTIME_EXTENSION_MEMBERS` CMake variable, the engine
compiles `oa-game` and, through the SDK, the extension with that
definition, `runtime.hpp` includes the header inside the class, and the
extension's `RuntimeExtension`, a friend of `Runtime`, turns its hooks into
calls on those members. A unit compiled without the definition, or with it
where `oa-game` has none, fails to link (`extension_members.cpp`). That
mechanism is frozen: `tools/check_runtime_surface.py` holds the header's
declarations, which must be plain declarations with no preprocessor
directive, and the private `Runtime` names the extension uses, each with
its number of uses, to `tools/runtime-surface-baseline.json`, which may
only shrink, and the mechanism goes away once they are gone. The engine's
`runtime-surface-names` test fails when a change to `runtime.hpp` leaves
the baseline naming something that is no longer a private name of
`Runtime`. Renaming one of those names replaces the old name with the new
one in the baseline, keeping its uses, in the same change: the one addition
the baseline takes. Removing one, or making it public, drops it.
