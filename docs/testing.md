# Testing

How to run the engine's tests and checks, and how to write a new test. The
rules themselves are in [conventions.md](conventions.md#tests); this page
explains them and shows how to follow them.

## Running the tests

Build the pinned SDL once (`python3 tools/bootstrap_sdl.py`, see
[CONTRIBUTING.md](../CONTRIBUTING.md#build-and-test)), then:

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
content (a music folder, say) or the build lacks the FFmpeg music decoder
still reports skipped.

The native checks, which run the game headless over the installation, are
added only when `OA_GAME_DIR` names one at configure time. The game is
`open-annihilation` (`open-annihilation.exe` on Windows, and on macOS the
executable inside the application bundle `open-annihilation.app`), the file
the `oa-game` target builds; tests start it through `$<TARGET_FILE:oa-game>`.

Every native check names its preferences file with `--preferences-file`, so
that it never reads or writes the player's own. With a named file, each
Open Annihilation setting's default is the game's own behaviour on every
platform, and the installation's `totala.ini` is not read for the unit
limit, so a check plays the same on every machine. A check that depends on a
setting writes the setting's key into its file first
(`src/platform/preferences/README.md` lists the keys).
`native-engine-settings-determinism` holds this in place: with every key at
its default, the seeded skirmish writes the same trace stream and draws the
same frame as with no file, and the director render keeps its pinned frames
and sound.

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

The game draws the terrain, the fog and each frame's conversion for the
window in bands on spare cores ([job pool](../src/platform/job-pool/README.md)):
up to four threads, one on a machine of one or two logical processors.
`OA_DRAW_THREADS` (1 to 32) sets the count for every run of the game a test
starts, and `--draw-threads N` for one run; every count draws the same
frames, which `native-draw-threads`, `world-draw-bands` and
`app-xrgb-conversion` check. To run the whole suite on one drawing thread,
or on many:

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
  over the demo's data, which has no skirmish map and no save and load
  dialog: the notices, the grayed-out entries and the campaign's way in and
  out. The last is registered only with the default extension, since an
  extension that offers multiplayer checks its own screens;
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
`OA_REQUIRE_GAME_DATA` too, since the demo is optional. `demo-installer`
covers the same code over a synthetic installer and runs everywhere.

### Other builds

- **Core only**, without SDL or game data: configure with
  `-DOA_BUILD_PLATFORM=OFF -DOA_BUILD_INTRO_PLAYER=OFF`.
- **Sanitizers:** configure with `-DOA_SANITIZERS=ON` (Clang or GCC) and run
  the suite as the CI sanitizer job does, since `platform-shims` ends a
  child process with SIGFPE on purpose.

  ```sh
  ASAN_OPTIONS=detect_leaks=0:handle_sigfpe=0 ctest --test-dir build --output-on-failure
  ```

- **Windows:** build with the PowerShell commands in
  [CONTRIBUTING.md](../CONTRIBUTING.md#build-and-test), which install zlib
  through vcpkg and use the vcpkg toolchain. The commands above are for a
  POSIX shell. A generator that holds several configurations, such as Visual
  Studio's, also needs the configuration named when ctest runs:
  `ctest --test-dir build -C Debug --output-on-failure`.
- **Windows from macOS or Linux:** `tools/build_windows.sh` cross-compiles
  the tree with mingw-w64, and `tools/test_windows.sh` builds it in a
  container and runs its tests under Wine. Use it when a change touches
  platform-specific code. The container keeps compiled objects in a Docker
  volume that every run and every checkout shares, so a rebuild compiles only
  what changed.

### What CI runs

Continuous integration builds the tree on macOS, Windows and Linux, starts
`open-annihilation` on each, builds the FFmpeg intro player on macOS and Linux, runs
every test that needs no game data, and runs the suite once more under
AddressSanitizer and UndefinedBehaviorSanitizer and once with the recorder
test extensions (`-DOA_RECORD_EXTENSION_HOOKS=ON`). CI has no game installation: the game-data tests and the
native checks run only on contributors' machines, so run them before asking
for review.

## Kinds of test

| Kind | What it does | Examples |
|---|---|---|
| Unit | Exercises one module with inputs the test builds itself | `sim-detection`, `tdf-parser` |
| Characterisation | Pins what a decoder, raster routine or table does today, edge cases stated byte by byte and seeded sweeps pinned by digest | `present-rle-test`, `hpi-read-node` |
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

## Writing a test

### Where it goes

A module's tests live in its `tests/` directory, one program per file named
`<name>_test.cpp`. The executable and the ctest name start with the module's
own names (`oa-sim-detection-test`, registered as `sim-detection`; a module
with several tests adds each one's case, `<group>-<module>-<case>`), so that
`ctest -R` finds a module's tests by its name. Tests that span modules live
under `tests/` at the root: `tests/content` for the installed-content sweep
and `tests/extension` for the extension boundary.

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
  passes whatever the code does.
- There is no shared harness yet. Where the module's tests already have a
  check macro, use it rather than adding another variant. The registry
  harness in `src/ui/frontend/tests/test_support.hpp` (`OA_TEST`,
  `OA_GAME_DATA_TEST`, `OA_CHECK`) is the model a shared one will follow.
- Test code may use the whole C++20 standard library, and may throw, for
  example from a fixture's checks, as long as the test reports the failure
  with its file and line and exits non-zero. The code under test keeps its
  layer's rules (default; maintainer to confirm).
- Keep each case short and give it a name that says what it checks.
- A test never opens a window or plays sound: run the game with
  `--headless-check`, or with `SDL_VIDEO_DRIVER=dummy` and
  `SDL_AUDIO_DRIVER=dummy`.
- Write temporary files under a fresh temporary directory, never into the
  source tree or the installation.

### Expected values

State expected values plainly: as constants in the test, as values read from
the game data of the installation, or as behaviour ("the second save loads
into the same state as the first"). A reader must be able to see where every
expected value comes from. Suites built on recorded data live outside this
repository; never copy their inputs or expected values into a test here.

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

Every decoder of file data has a test that feeds it malformed input: a
stream that ends early, a count or offset that points past the end, a
length that would need an allocation beyond the decoder's limit. The test
checks that the decoder stops there, allocates nothing unbounded and reports
the error it should. `src/present/tests/rle_test.cpp` is an example
(`test_malformed_streams`).

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

The engine does not yet have a shared test harness, ctest labels (such as
unit, data, native and lint) or fuzz targets for its decoders. Until they
arrive, use the registrations above and name tests after their module so
that `-R` finds them.
