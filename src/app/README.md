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
`licenses/` and what an extension adds through `OA_EXTENSION_GAME_FILES`)
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
- `runtime_match_menus.cpp`: the in-match menus.
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
- `runtime_hud.cpp`, `runtime_match_hud.cpp`: the HUD.
- `runtime_messages.cpp`: the in-game message log (`Game.chat_lines`) drawn
  over the battlefield, and the speed and message part of `--check-navigation`.
- `runtime_console.cpp`, `runtime_console_debug.cpp`: the in-game console's
  host hooks, the debug grid, "Profile" bars and DebugBreak, and the console
  part of `--check-navigation`.
- `runtime_match_menus.cpp` also registers the load-game overlay
  (`register_load_game_screens`).
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
- `video_capture.hpp`, `video_capture.cpp`: `--capture-video`, the
  developer's capture of the window's frames and the game's sound as an MP4
  video through the `ffmpeg` program; `runtime_showcase.cpp`: the scripted
  runs `--showcase` plays. [docs/capture.md](../../docs/capture.md)
  describes both.

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

`extension.hpp` is the table of hooks through which one library linked into
`oa-game` extends it: long options and game switches, start-up and
shutdown, screens, the frontend's entry (its game name and nickname), run
modes, per-frame work, match events, the Pause key, the speed keys and
the GAME slider, the frontend's application modes, the loading's
progress, the team panels' host (a tournament game withholds CONTROL),
requests to close the window, the launcher's label in the match menus,
console commands and checks. `main()` has the library's
`oa_extensions_init` fill it before the command line is parsed; every hook
left null keeps the engine's behaviour, the game without multiplayer, which
is what `oa-extensions-default` gives. The CMake cache variable
`OA_EXTENSIONS_TARGET` names the library `oa-game` links; one that
includes `runtime.hpp` builds against `oa::extension-sdk`, the include
directories and libraries an extension may use. An exception a hook
throws ends the game except on the paths `extension.hpp` lists: a match
start the frontend falls back from, and a simulation tick.
`OA_EXTENSION_API_VERSION` in `extension.hpp` numbers the table's
contract; an extension checks its typed copy,
`oa::app::extension_api_version`, with `static_assert`, and any change to
the contract raises it (its comment says what counts). The recorder test
extension checks it too, beside its count of the table's hooks.

`tests/extension/` tests the boundary itself. `extension-layout-mismatch`
links a unit that sees `Runtime` with members `oa-game` does not have and
expects the link to fail. A build configured with
`-DOA_EXTENSIONS_TARGET=oa-extension-recorder` links a test extension that
fills every hook with a recorder and adds one `Runtime` member;
`extension-hooks-options` and, over the installed game,
`extension-hooks-game` check that the game calls every hook but
`disconnect_text`, which only a shared match reaches (of `match_event`'s
events they see `finished`, `torn_down` and `results_released`); the
navigation check's Pause key reaches `pause_changed`, its speed keys
`speed_changed` and its menus `app_mode_set`, a skirmish's loading
`load_progress`, `team_panel_host` and `service_label`, and
`--check-match-dialogs`'s close requests `close_requested` and its GAME
slider `speed_changed`. With
`--record-quit STATUS` the recorder keeps the screen services an overlay
is given, stops the sounds, plays BGM on the alternate route, asks for a
frontend pass and ends the run through `quit`, which must exit with
STATUS. The navigation check itself puts probes in place of the hooks to
check what the engine does with their answers: a close request answered
or declined, quit's status, one frontend pass for two requests, a query
binding, the launch's nickname, the launcher's label kept as a match
starts and quit leaving a match, with the preferences open over it, first. CI builds
that configuration as a job of its own, but has no game installation:
there `extension-hooks-game` skips, and only the option hooks and the hook
list are checked. The rest of the hook coverage runs only where
`OA_GAME_DIR` is set, locally or in a private run; run it there before an
extension moves its engine pin.

An extension reaches `oa-game` only through these hooks and declared
headers. When it needs something the table does not offer, add a hook or
declare a header for it here; never give it a new `Runtime` member or
friend, or another of `Runtime`'s private names.

Until hooks and declared headers cover everything, an extension may still
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
