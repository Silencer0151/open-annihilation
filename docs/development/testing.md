# Testing

How to run the engine's tests and checks, and how to write a new test. The
rules themselves are in [conventions.md](conventions.md#tests); this page
explains them and shows how to follow them.

## Running the tests

Build the pinned SDL once (`python3 tools/bootstrap_sdl.py`, see
[CONTRIBUTING.md](../../CONTRIBUTING.md#build-and-test)), then:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_PREFIX_PATH="$PWD/local/deps/sdl-install" \
    -DOA_GAME_DIR="/path/to/Total Annihilation" \
  && cmake --build build --parallel 8 \
  && ctest --test-dir build --output-on-failure
```

That one command runs every test and every source check.

Validate in the Check build: `-DCMAKE_BUILD_TYPE=Check` (in a build
directory such as `build-check`) compiles with optimisation but keeps every
assertion, with line tables for backtraces. It computes the same results as
Debug bit for bit, which the pinned-digest tests check, and runs the suite
several times faster, since most of a Debug run is unoptimised library code.
Keep Debug for stepping through code in a debugger. `CMakePresets.json`
names both, and a sanitizer build (`cmake --preset check`, then
`cmake --build --preset check` and `ctest --preset check`); put your
`OA_GAME_DIR`, compiler launcher and job count in a `CMakeUserPresets.json`
that inherits them, which Git ignores. The Clang check
of the documentation blocks in public headers is a build target instead
(`cmake --build build --target oa-doc-check`, see
[conventions.md](conventions.md#checks)). Some useful variations:

| To | Run |
|---|---|
| run tests in parallel | `ctest --test-dir build -j 8 --output-on-failure` |
| run the tests whose names match a pattern | `ctest --test-dir build -R 'sim-detection'` |
| see one test's full output | `ctest --test-dir build -R '^match-determinism$' -V` |
| list the tests without running them | `ctest --test-dir build -N` |
| run only the source checks | `ctest --test-dir build -R 'doc-links\|runtime-surface\|style-ratchet\|format-check\|licensing-check'` |

Before asking for review, run the whole suite once in a build configured
with `OA_GAME_DIR`, and say in the pull request whether the game-data tests
ran.

### The installed game

`OA_GAME_DIR` names an ordinary Total Annihilation 3.1c installation: the
absolute path of the folder that holds `totala1.hpi`. Set it in the
environment or pass it to CMake as `-DOA_GAME_DIR=PATH`. A build tree keeps
the value it was configured with; an empty entry takes the environment
variable on the next configure. The tests read the installation's archives
as the game does, so nothing needs to be extracted.

Without an installation, each game-data test prints one line saying what it
skipped and exits with code 77, which ctest reports as skipped, not passed.
Configure with `-DOA_REQUIRE_GAME_DATA=ON` to make a missing installation a
failure instead; a test that skips because the installation lacks optional
content (a music folder, say) still reports skipped.

The native checks, which run the game headless over the installation, are
added only when `OA_GAME_DIR` names one at configure time. The game is
`open-annihilation` (`open-annihilation.exe` on Windows, and on macOS the
executable inside the application bundle `open-annihilation.app`), the file
the `oa-game` target builds; tests start it through `$<TARGET_FILE:oa-game>`.

Every native check names its preferences file with `--preferences-file`, so
that it never reads or writes the player's own, nor the renderer records
the game keeps beside it, which with a named file live in memory for the
run. With a named file, each
Open Annihilation setting's default is the game's own behaviour on every
platform, and the installation's `totala.ini` is not read for the unit
limit, so a check plays the same on every machine. A check that depends on a
setting writes the setting's key into its file first
(`src/platform/preferences/README.md` lists the keys).
`native-engine-settings-determinism` holds this in place: with every key at
its default, the seeded skirmish writes the same trace stream and draws the
same frame as with no file, and the director render keeps its pinned frames
and sound. With a named file Hardware acceleration and Vertical sync are
Off, and the windowed checks' `SDL_RENDER_DRIVER=software` locks both, so no
check draws through the graphics card or waits for the display. Four
checks pass `--force-capable`, which lifts those locks:
`native-engine-settings`, so that it can turn Vertical sync On and read it
back, and set Hardware acceleration to Basic, Full and Off through the
dialog in one step of its own, which from 2 GiB draws in the accelerated
tier on SDL's software renderer, Full as Basic with the status saying so,
and retries it after a drop or a function test forced to fail, every other
step leaving it Off; and `native-render-tiers`,
`native-render-tiers-density` and `native-demo-render-tiers`, which with
`--hardware-acceleration` run the start-up function test on SDL's software
renderer and draw in the accelerated tier, switching it off and on as the
flags would. That flag names Full, which forces the Full tier while it is
not ready for players: the Basic cases run at `basic`, and the Full cases,
the terrain drawn by the card from the terrain atlas's pages with the rest
drawn by the processor over it, at `full`.
With the bare flag `native-render-tiers` and `native-demo-render-tiers`
also switch the Full tier's sprite stage on and hold its frames to the
processor's composition (see [src/app/README.md](../../src/app/README.md)).

No window of a check opens at the display's own pixel density but
`native-render-tiers-density`'s, which `--native-density` opens so: on the
dummy video driver its density is 1, and the check holds the match laid out
in window points, read back at the display's size, at zoom 1 equal to the
processor's composition, and picking the unit drawn under the pointer. On a
display above density 1, such as a laptop's built-in display, run the same
check by hand, without `SDL_VIDEO_DRIVER`, to hold the read-back at zoom 1
to the composition enlarged by nearest replication:

```sh
SDL_RENDER_DRIVER=software build/open-annihilation.app/Contents/MacOS/open-annihilation \
    --game-dir "/path/to/Total Annihilation" --skip-intro --mute \
    --check-render-tiers --hardware-acceleration --force-capable --native-density \
    --preferences-file /tmp/render-density.conf
```

### Threads

`installed-content` spreads its work over one thread per logical core, so it
takes a few seconds on a machine with many cores. The `OA_TEST_THREADS`
environment variable sets the number of threads instead: a whole number from
1 to 1024, where 0 or an empty value keeps one per logical core. Each thread
holds the entry it is decoding, so memory grows with the count, to about
1 GB with 24 threads. In a parallel ctest run the sweep starts among the
first tests and its threads share the cores with the others. A container
limited to fewer CPUs than its host usually still reports every core of the
host, so there, and on a machine with little memory, give the sweep fewer
threads:

```sh
OA_TEST_THREADS=4 ctest --test-dir build -R '^installed-content$'
```

The output is the same for any number of threads, apart from the time the
sweep took.

The game draws the terrain, the fog, the battlefield's units, features and
effects and each frame's conversion for the window in bands on spare cores
([job pool](../../src/platform/job-pool/README.md)):
up to four threads, one on a machine of one or two logical processors.
`OA_DRAW_THREADS` (1 to 32) sets the count for every run of the game a test
starts, and `--draw-threads N` for one run; every count draws the same
frames, which `native-draw-threads`, `world-draw-bands`,
`app-xrgb-conversion`, `present-surface-band`,
`model-render-rgb-bridge` and `model-render-mesh-raster`
check. To run the whole suite on one drawing thread, or on many:

```sh
OA_DRAW_THREADS=1 ctest --test-dir build
OA_DRAW_THREADS=8 ctest --test-dir build
```

### The demo's installer

`OA_DEMO_INSTALLER` names the installer of the Total Annihilation demo (1997):
the absolute path of the installer file itself, which the engine recognises by
its size and SHA-256. Set it in the environment or pass it to CMake as
`-DOA_DEMO_INSTALLER=PATH`; like `OA_GAME_DIR`, a build tree keeps the value
it was configured with, and tests receive it at run time. It is the only
setting the demo's tests need: each unpacks the demo's data from the
installer itself. These tests read it:

- `demo-installer-data` unpacks the installer's archive into a temporary data
  folder, checks its size and SHA-256, and checks that a second start reuses
  it and that the unpacked folder is a usable installation;
- `native-demo-installer` starts the game headless, with `--game-dir` on the
  installer's folder and `--data-dir` on a scratch folder, and checks that the
  archive is unpacked, checked and mounted, that every start opens the main
  menu with no dialog over it, that a second start mounts it without
  unpacking it again, that a damaged archive is unpacked again, and that the
  installer's folder is left as it was;
- `native-demo-navigation`, `native-demo-saved-games` and
  `native-demo-multiplayer-menu` start the game the same way and run its
  `--check-navigation`, `--check-load-save` and `--check-multiplayer-menu`
  over the demo's data, which has no skirmish or multiplayer map and no
  save and load dialog: the notices, MULTI's among them, the grayed-out
  entries and the campaign's way in and out;
- `native-demo-render-tiers` starts the game the same way, windowed on
  SDL's software renderer, and runs `--check-render-tiers
  --hardware-acceleration --force-capable` over the main menu and the
  demo's first Arm mission (`--campaign "Arm Campaign" --mission 0`), as
  `native-render-tiers` runs it over a skirmish of the installed game: the
  accelerated presentation's frames held to the processor's composition
  and to that renderer's own filters. It skips with the game's check on a
  machine under 2 GiB of memory;
- `native-demo-campaign-ending` starts the game the same way and plays the
  demo's last Arm mission, AC03, at easy with `--give-orders`: it must be won
  without a failed tick, and its end screen must leave through the ending
  for the main menu;
- `native-demo-mission-ac01`, `-ac02` and `-ac03` start the game the same
  way on each mission of the demo's Arm Campaign (`--mission 0`, `1` and
  `2`), which must start from its briefing with units on both sides and play
  3000 ticks, or up to its outcome, without a failed tick;
- `native-demo-mission-saveload` saves each of those missions at tick 300;
  each save must load back into the same tick, units, world digest and
  orders with no state dropped, and, loaded again with none dropped, play
  on. `tools/check_native_demo_missions.py` says what each run must print;
- `frontend-dialogs-notice` opens the demo's `DEMOMSG.GUI` notice, unpacked
  into a temporary data folder, and checks its text, its buttons and their
  captions.

Without `OA_DEMO_INSTALLER` each reports skipped, under
`OA_REQUIRE_GAME_DATA` too, since the demo is optional; a path that names no
installer, or the wrong file, fails them. Each carries the ctest label
`demo`, so `ctest --test-dir build -L demo` runs them all. Configure with
`-DOA_REQUIRE_DEMO_INSTALLER=ON` to prove they ran: configuration then fails
when `OA_DEMO_INSTALLER` names no file, and a demo test that skips fails.
`demo-installer` covers the same code over a synthetic installer and runs
everywhere.

### Network play

Network play is always built and tested. Its tests are `network`, the
`net-*` tests of the wire and protocol, the session and its loopback over
sockets, the records, the match, the multiplayer frontend states, the
console commands, sync, messages and the launch switches, `tad-format` and
`demo-playback` for recorded games, `ui-multiplayer-*` for the multiplayer
screens, and `netgame-*` for its options, launch, close handlers, traffic
overlay and the `Runtime` names it uses; those ending in `-data` read the
installation and skip without it. Over the installation `OA_GAME_DIR`
names, these run the game headless:

- `native-net-loopback` (`--net-loopback-check N`) hosts and joins a match
  in one process over 127.0.0.1 and compares both worlds after N ticks;
  `native-net-loopback-watcher` has the joiner watch, and
  `native-net-loopback-computer` and `native-net-loopback-computer-watcher`
  have the host seat a computer player that builds, is given a squad and
  attacks;
- `native-multiplayer-menu` (`--check-multiplayer-menu`) clicks MULTI and
  checks the multiplayer screens;
- `native-recording-hook` (`--check-recording-hook`) checks that the game
  declines bytes that are no recording and refuses a truncated one;
- `native-host-not-found` (`tools/check_native_host_not_found.py`) checks
  a joiner whose host is never listed: "Host not found.  Exiting...", and
  the game leaves 4 s later.

`open-annihilation --play-demo FILE.tad` replays a recorded game through
the path a watching machine receives a match on; with `--headless-check`
and `--match-ticks N` it replays without a window.

### The renderer

The game makes its renderer by walking SDL's render drivers and makes it
again when one fails while the game runs ([src/app](../../src/app/README.md)).
On the dummy video driver every hardware driver refuses, so these run on
SDL's software renderer, with `SDL_RENDER_DRIVER` left unset so that the
walk runs:

- `app-render-host` walks made-up drivers through stand-in hooks, at start
  and in a rebuild, then makes, rebuilds and loses a renderer on the dummy
  video driver, with its renderer records in scratch folders, one that
  cannot be written among them; `app-render-host-env` names a driver that
  does not exist through `SDL_RENDER_DRIVER` itself;
- `app-scaled-world-software` draws textures beyond a texture limit of
  1024 as tiles on SDL's software renderer and reads them back as one
  texture with no limit draws them;
- `app-full-sprites` draws one list of sprites, blended sprites, particle
  squares, lines and selection lines on SDL's software renderer through
  the Full tier's sprite stage (`src/app/runtime_full.hpp`) and on the
  processor through the bands, and holds the card's picture to the
  processor's: exact where sprites are opaque at zooms 1 and 2, within 2
  levels where the alpha table blends them, the squares exact, the lines
  covering the game's lines within a pixel; with the stage's order and
  batches, the fog's states (a sprite under a cell out of sight greyed,
  under a never-mapped cell left out), the refusals, a frame overflowing
  the pages and a pinned digest of its frame. `app-full-sprites-data` draws
  a scene of the installed game's GAF frames through its palette and alpha
  table the same way, against the processor and a reference that blends
  to the true mean;
- `native-renderer-ladder` (`--check-renderer-ladder`) forces each failure
  the game handles while it runs and checks that it presents on through
  it, each frame after a failure equal to the frame composed on the
  processor: the walk, a present that fails in a match and on a loading
  frame, a texture SDL cannot make, a device reset in a match, during a
  load, in a drain of input and through a movie's hook, a lost device, a
  device that waits to be reset, present stalls on steady frames and on
  frames that are not, a changed floating-point setting, and window-size
  layers in tiles past a texture limit of 2048. Then it starts the game
  again and again, in the one process, on renderer records in a folder of
  its own beside its reports, removed when it ends, as the player's own
  profile keeps them, with the setting at Basic under `--force-capable`,
  8 GiB of memory taken
  and its own clock for the stages of the sentinel: a left-over trial
  struck at the first restart, also with the sentinel's file garbled or
  lost, and recorded at the second, or at the first where a fault can stop
  the whole system; a clean pass that clears the strike, and a left-over
  `running` marker only logged; a trial that cannot be written; an
  accelerated path's trial and sentinel, the magnified world's written only
  at the first zoomed-in frame of a match and closed by switching the tier
  off; the notice waiting through `-n`'s multiplayer signal and the
  multiplayer screens; an error of the game's own, struck against nothing;
  an accelerated-only failure and the dialog's retry, a failure struck
  during it, Cancel and OK; and, naming the renderer by SDL's first
  hardware driver, left-over `create` and `standard` sentinels that make
  the walk skip it, the walk again with the records ignored, the adapter
  in another driver's words after a skip or a rebuild, lost devices,
  repeated resets, present errors and a machine under 2 GiB, each new
  record told once at the main menu. The Full tier's own fallbacks run
  with them, Full switched on as `--hardware-acceleration=full` and
  `--force-capable` would: a card call that fails on a Full match frame
  (`--render-fault card`) drops Full to Basic for the run, which presents
  the composed frame, with the failing call struck against the driver and
  the status saying Full stopped, Off and back trying Full again; the same
  failure in two runs in a row records `full-unusable`, told once at the
  main menu and cleared by a raise of the row from Basic to Full, a strike
  alone cleared by a clean run; Full's first match frame writes its trial
  and sentinel, `path full`, a left-over Full trial is struck at the first
  restart and recorded at the second, or at the first where a fault can
  stop the whole system, leaving Basic standing; a Full trial that cannot
  be written keeps Basic with nothing struck; and in a shared game or a
  replay Basic applies at once, Full from the next game, a page Full would
  make during the match waits for its end, and the loading screen makes
  every page and target first. A named preferences file keeps
  the cases before them, and the other checks, on records in memory.
  `--render-fault POINT[@FRAME]` forces one failure alone, at a presented
  frame of its case; `native-renderer-ladder-create` makes every driver but
  software refuse at start;
- `native-renderer-ladder-slow` (`--render-fault slow`) and
  `native-renderer-ladder-memory` (`--render-fault memory`) switch the
  accelerated tier on, as `--hardware-acceleration` and `--force-capable`
  would, and run only when named. The first forces the interval of each
  frame, on a clock of its own, so that slow frames walk the step-down one
  rung a step to the standard tier, each step logged once, and checks that
  the standard tier and idle frames never feed it and that a frame's
  ticks, added between frames, are taken out against the loop's paced
  rate; the second forces the memory guard's sample of the system's
  memory, so that the guard refuses the tier's buffers and then drops it
  for the run. `native-renderer-ladder-full-slow` (`--render-fault
  full-slow`) and `native-renderer-ladder-full-memory` (`--render-fault
  full-memory`) do the same for the Full tier, switched on as
  `--hardware-acceleration=full` would: slow frames take Full's rungs
  first, its anti-aliasing from 4 to 2 to 1 and then Full itself, to
  Basic for the run, each step logged once and the status saying so, Off
  and back starting Full again at the top; and the guard refuses Full's
  pages, which drops Full alone with nothing struck and nothing Off and
  back lifts, and, tripped while Full draws, drops Full first, freeing its
  pages, and Basic at the next sample while memory stays short. Each skips
  under 2 GiB.

### Other builds

- **Core only**, without SDL or game data: configure with
  `-DOA_BUILD_PLATFORM=OFF -DOA_BUILD_INTRO_PLAYER=OFF`.
- **Warnings as errors:** configure with `-DOA_WARNINGS_AS_ERRORS=ON` to
  fail the build on any compiler warning in the targets that link
  `oa-options` (`-Werror`, or `/WX` with Visual Studio's compiler, whose
  linker then fails on its warnings too), as CI does with every compiler.
- **Sanitizers:** configure with `-DOA_SANITIZERS=ON` (Clang or GCC) and run
  the suite as the CI sanitizer job does, since `platform-shims` ends a
  child process with SIGFPE on purpose.

  ```sh
  ASAN_OPTIONS=detect_leaks=0:handle_sigfpe=0 ctest --test-dir build --output-on-failure
  ```

- **Windows:** build with the PowerShell commands in
  [CONTRIBUTING.md](../../CONTRIBUTING.md#build-and-test), which install zlib
  through vcpkg and use the vcpkg toolchain. The commands above are for a
  POSIX shell. A generator that holds several configurations, such as Visual
  Studio's, also needs the configuration named when ctest runs:
  `ctest --test-dir build -C Debug --output-on-failure`.
- **Windows from macOS or Linux:** `tools/build_windows.sh` cross-compiles
  the tree with mingw-w64 for x86-64, or for 32-bit x86 with
  `OA_MINGW_TRIPLE=i686-w64-mingw32`, and refuses any other triple. Each
  target has its toolchain file in `cmake/toolchains` and keeps its
  dependencies and build tree apart from the others' (`build-windows`,
  `build-windows-i686`). `tools/test_windows.sh` builds the x86-64 tree in a
  container and runs its tests under Wine. Use it when a change touches
  platform-specific code. The container keeps compiled objects in a Docker
  volume that every run and every checkout shares, so a rebuild compiles only
  what changed.
- **Windows XP:** `OA_MINGW_TRIPLE=i686-w64-mingw32 tools/build_windows.sh
  --xp` builds the tree for 32-bit Windows XP SP3 into
  `build-windows-i686-xp`, and `tools/build_windows.sh --xp` for the 64-bit
  edition into `build-windows-xp`, with zlib and SDL3 built the same way.
  Such a build links the C library every Windows release includes, not the
  one Windows 10 added: the toolchain files switch a cross-compiler that
  links the newer one by default, and a configuration that still links it
  is refused. With another toolchain, build zlib and SDL3 with it and
  configure with
  `-DOA_WINDOWS_XP=ON`. Engine code then compiles against the declarations of
  Windows XP and every executable runs on it; see
  [src/platform/xp-runtime](../../src/platform/xp-runtime/README.md). Check an
  executable's imports against a Windows XP installation's own system DLLs
  before running it there: Windows XP refuses to start a program that imports
  anything they do not export. On XP, which lacks the sound interface SDL
  prefers, SDL plays sound through the older one XP has;
  `OA_SOUND_OUTPUT=waveout` in the environment plays it through the
  engine's own wave-out output instead.
- **32-bit x86:** a 32-bit x86 build needs no SSE2: floats are computed
  with SSE (`OA_X86_FLOAT=sse`, the default; `fpu` computes them on the
  older floating-point unit, which changes the simulation's results) and
  doubles at a double's precision on the older unit
  ([src/base/float-precision](../../src/base/float-precision/README.md)).
  `cmake/toolchains/i686-w64-mingw32.cmake` builds it, and the dependencies
  with it, for the i686 instruction set without SSE2. Its
  results must equal a 64-bit build's: run the pinned-digest tests
  (`match-determinism`, `match-trace`, `persist-bank-golden`, `game-math`,
  `game-math-extended`) on it.
- **The oldest SDL:** the build accepts SDL 3.2 or newer.
  `python3 tools/bootstrap_sdl.py --version 3.2.0` builds 3.2.0, the
  oldest release, into `local/deps/sdl-install-3.2.0`, beside the pinned
  SDL, which it leaves in place; configure with that folder as
  `CMAKE_PREFIX_PATH`, as the `sdl-3-2` CI job does. Engine code that
  needs a newer SDL checks the version with `SDL_VERSION_ATLEAST`, so that
  the tree still builds and runs on 3.2.0.

### What CI runs

Continuous integration (`.github/workflows/build.yml`) runs these jobs:

| Job | What it does |
|---|---|
| `build` | Builds the tree on macOS (arm64), Windows and Linux, in Debug and in Check, starts `open-annihilation` on each, and runs every test that needs no game data, network play's among them. Running both build types runs the pinned-digest tests (`match-determinism`, `match-trace`, `persist-bank-golden`, `game-math`, `game-math-extended`) on an optimised build as well, so a build type that computes different results fails. Linux also runs the format and licence checks, and macOS the documentation check |
| `sdl-3-2` | On macOS, Windows and Linux, builds SDL 3.2.0, the oldest release the build accepts, into its own folder (`tools/bootstrap_sdl.py --version 3.2.0`), checks that the configuration found it, builds the tree in Debug against it and runs every test that needs no game data, so that engine code needing a newer SDL without checking its version fails on the system whose code it is |
| `demo` | On macOS and Linux, downloads the installer of the Total Annihilation demo (1997), checks it against its pinned SHA-256 and caches it, then builds in Check and runs the demo's tests (`ctest -L demo`) with `-DOA_REQUIRE_DEMO_INSTALLER=ON`, so a demo test that skips fails |
| `extension-recorder` | Builds and tests on Linux with the recorder test extensions registered beside network play (`-DOA_RECORD_EXTENSION_HOOKS=ON`) |
| `sanitizers` | Builds Debug with Clang on Linux under AddressSanitizer and UndefinedBehaviorSanitizer and runs the suite |

Every job configures with `-DOA_WARNINGS_AS_ERRORS=ON`, so a warning from
Clang, GCC or Visual Studio's compiler in engine code fails it. Visual
Studio's C library marks standard functions such as `strcpy`, `fopen`,
`getenv` and `sscanf` as unsafe, in favour of variants the other platforms'
C libraries lack: engine code copies text into fixed-size fields with
`oa-base-text` (`oa/base/text.hpp`), opens files with
`oa::platform::open_file` and reads the environment with
`oa::platform::environment_value`.

The installation of Total Annihilation 3.1c is not CI's to download: the
game-data tests and the native checks over it run only on contributors'
machines, so run them before asking for review.

## Kinds of test

| Kind | What it does | Examples |
|---|---|---|
| Unit | Exercises one module with inputs the test builds itself | `sim-detection`, `tdf-parser` |
| Characterisation | Pins what a decoder, raster routine or table does today, edge cases stated byte by byte and seeded sweeps pinned by digest | `present-rle`, `hpi-read-node` |
| Pinned values | Pins digests or bytes that must not move: a whole match, the random streams, the bytes the writers produce | `match-determinism`, `match-shared-random`, `hpi-writer-golden`, `persist-bank-golden` |
| Game data | Reads the installed game named by `OA_GAME_DIR` | `unit-definitions-data`, `installed-content` |
| Native | Runs the game headless over the installation and checks what it does | `native-saveload`, `native-trace` |
| Demo | Runs the game headless over the demo's data, unpacked from the installer `OA_DEMO_INSTALLER` names, and checks what it does | `native-demo-installer`, `native-demo-mission-ac01` |
| Extension boundary | Checks the extension table and the `Runtime` members an extension may add | `extension-layout-mismatch`, `runtime-surface-names` |
| Source checks | Check the tree's files against the conventions | `doc-links`, `style-ratchet`, `licensing-check` |

`installed-content` is the base-content guard: it mounts every archive of
the installation, decodes every entry with the engine's decoders and runs the
definition loaders a skirmish start runs, on every core (see
[Threads](#threads)). A change to a decoder runs it before review.
`installed-tdf-readers` digests what the feature, unit announcement, side
layout, meteor and scenario readers take from the installation's TDF texts,
so a change to how any of them reads shows there.

## Writing a test

### Where it goes

A module's tests live in its `tests/` directory, one program per file named
`<name>_test.cpp`. The executable and the ctest name start with the module's
own names (`oa-sim-detection-test`, registered as `sim-detection`; a module
with several tests adds each one's case, `<group>-<module>-<case>`), so that
`ctest -R` finds a module's tests by its name. A ctest name never holds the
word `test`; it ends in `-data` when the case reads the installed game and
in `-selftest` when a check tests itself, and begins with `native-` exactly
when it is a native check: its command is the game or a
`tools/check_native_*.py` script. `ctest-names` checks these against the
[naming rule](conventions.md#naming). Tests that span modules live
under `tests/` at the root: `tests/content` for the installed-content sweep
and the installed TDF readers, and `tests/extension` for the extension
boundary.

```cmake
if(BUILD_TESTING)
  add_executable(oa-sim-detection-test tests/detection_test.cpp)
  target_link_libraries(oa-sim-detection-test PRIVATE oa-sim-detection oa-options)
  add_test(NAME sim-detection COMMAND oa-sim-detection-test)
endif()
```

Give a test that takes more than a few seconds a `TIMEOUT` with
`set_tests_properties`.

### What a test program looks like

A test is a program that runs its cases, reports each failed check with the
file, line and failed expression, and returns non-zero when any check
failed. Start the file with a comment saying what it covers. Checks count
failures and carry on, so one run shows every broken case:

```cpp
// The detection ranges of radar, sonar and jammers over a synthetic map.
#include "oa/sim/detection.hpp"

#include <cstdio>

namespace {

int failures = 0;

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__,       \
                         __LINE__, #condition);                                 \
            ++failures;                                                         \
        }                                                                       \
    } while (false)

void test_radar_range_edge() {
    ...
    CHECK(sees(world, radar, target_at_range));
    CHECK(!sees(world, radar, target_past_range));
}

} // namespace

int main() {
    test_radar_range_edge();
    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    return 0;
}
```

- Never check with `assert()`: Release builds remove it, and the test then
  passes whatever the code does. `style-ratchet` fails on `assert()`, and on
  `#undef NDEBUG`, in a file under a `tests` directory or named
  `<name>_test`.
- Where the module's tests already have a check macro, use it rather than
  adding another variant. Otherwise link `oa-test-support` and use
  `OA_CHECK` from `oa/test/check.hpp`, which reports and counts failed
  checks as above; `main()` returns `oa::test::check_exit_status()`. The
  registry harness in `src/ui/frontend/tests/test_support.hpp` (`OA_TEST`,
  `OA_GAME_DATA_TEST`, `OA_CHECK`) is the model a fuller shared one will
  follow.
- Test code may use the whole C++20 standard library, and may throw, for
  example from a fixture's checks, as long as the test reports the failure
  with its file and line and exits non-zero. The code under test keeps its
  layer's rules (default; maintainer to confirm).
- Keep each case short and give it a name that says what it checks.
- A test never opens a window or plays sound: run the game with
  `--headless-check`, or with `SDL_VIDEO_DRIVER=dummy` and
  `SDL_AUDIO_DRIVER=dummy`.
- Write temporary files under a fresh temporary directory, never into the
  source tree or the installation: `oa::test::make_scratch_directory()`
  (`oa/test/scratch_directory.hpp`, in `oa-test-support`) creates one that
  no other run shares, so test runs from several build trees at once never
  touch each other's files. Never use a fixed name under the temporary
  directory.
- A test that builds a match links `oa-test-match` and takes what the match
  asks of its application from `oa/test/match_services.hpp`:
  `oa::test::QuietServices`, which takes every call and does nothing,
  `oa::test::StrictServices`, on which every call fails the test, and
  `oa::test::EmptyScenario`. Derive from them and override only the calls
  the test watches. To follow one projectile across ticks, take an
  `oa::test::ProjectileHandle` (`oa/test/projectile_handle.hpp`) and
  `follow()` it after every tick: the pool moves projectiles as it closes
  its gaps, and `Projectile.created_tick` changes after launch.
- Never wait a fixed time for something to happen: a loaded machine can
  stall a test for longer. Drive the clock the code reads where it takes
  one, or poll with a generous limit, and make a check that something has
  not happened yet hold however late it runs.

### Expected values

State expected values plainly: as constants in the test, as values read from
the game data of the installation, or as behaviour ("the second save loads
into the same state as the first"). A reader must be able to see where every
expected value comes from.

### Pinned values

Some tests pin a digest or a byte sequence: `match-determinism` pins the
state and trace digests of a whole synthetic match on every platform,
`match-shared-random` the random streams' draws, and the writer goldens the
exact bytes the engine writes. A pinned value moves only when the change
means it to:

1. Find out why it moved. A value that moves unexpectedly is a regression
   until shown otherwise; a change that must not alter the simulation or
   the save bytes must leave every pinned value alone.
2. When the move is intended, update the constant in the same commit, and
   say in the commit message which values changed and why.

`src/sim/match-runtime/tests/determinism_test.cpp` is the model: its header
comment says what the pinned values cover and how to change them.

### Malformed input

Every decoder of file data or network messages has a test that feeds it
malformed input: a stream that ends early, a count or offset that points
past the end, a length that would need an allocation beyond the decoder's
limit. The test checks that the decoder stops there, allocates nothing
unbounded and reports the error it should. `src/present/tests/rle_test.cpp`
is an example (`test_malformed_streams`). `net-wire-malformed`
(`src/netgame/tests/wire_malformed_test.cpp`) also changes, cuts and splices
real messages at random and feeds them to every network decoder; given
.tad recordings, or directories of them, as arguments, it takes their
packets as seeds too.

### Game data

A test that reads the installed game:

1. links `oa-test-game-data` (and `oa-formats-hpi` to open the archives), whose
   headers are `oa/test/game_data.hpp` and `oa/test/game_assets.hpp`;
2. takes the installation at run time through `require_game_directory()` or
   `require_game_assets()`, which skip the test with exit code 77, or fail it
   under `OA_REQUIRE_GAME_DATA`, when there is none;
3. is registered with `oa_add_game_data_test(<name> <command...>)`, or with
   `oa_game_data_tests(<tests...>)` after `set_tests_properties`, from
   `cmake/OaGameData.cmake`.

Never bake an installation's path into a compile definition. A test program
that has self-contained cases as well as game-data ones runs the game-data
cases when given `--data` (`game_data_requested()`), and is registered twice:

```cmake
add_executable(oa-present-pcx-test tests/pcx_test.cpp)
target_link_libraries(oa-present-pcx-test PRIVATE oa-present oa-formats-hpi oa-test-game-data)
add_test(NAME present-pcx-test COMMAND oa-present-pcx-test)
oa_add_game_data_test(present-pcx-test-data oa-present-pcx-test --data)
```

```cpp
int main(int argc, char** argv) {
    if (oa::test::game_data_requested(argc, argv))
        test_installed_sweep(oa::test::require_game_assets("the installed PCX sweep"));
    else
        test_round_trip();
    ...
}
```

A test that can only skip because optional content is missing (another
edition's archive, a music folder) calls `oa::test::skip_test()` with what it
skipped and why; that stays a skip even under `OA_REQUIRE_GAME_DATA`.

## Not yet in place

The engine does not yet have a shared test harness beyond `oa-test-support`,
ctest labels other than `demo` (such as unit, data, native and lint) or fuzz
targets for its decoders. Until they arrive, use the registrations above and
name tests after their module so that `-R` finds them.
