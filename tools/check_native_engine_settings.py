#!/usr/bin/env python3
# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

"""Check that the Open Annihilation settings leave the game as it plays without them.

A seeded headless skirmish, its local army marching, is played through the
match clock and drawn at 30 frames a second (--frame-rate), each run from a
preferences file the check writes:

- With no preferences file, and with one holding every setting's key at its
  default value, it writes the same trace stream, reaches the same world
  digest and draws the same last frame, byte for byte.
- With every setting that only changes the look or the input away from its
  default (wheel zoom off, Escape opens the game menu, Select groups without
  Alt, the lowest maximum frame rate, the performance statistics), it writes
  the same trace stream and reaches the same world digest.
- At each level of enhanced anti-aliasing it writes the same trace stream
  and reaches the same world digest, and its last frame differs: units are
  drawn finer.
- With Pathfinding cycles at 2x, two runs write the same trace stream, and it
  differs from the default's; with a unit limit of 500, two runs write the
  same trace stream.

The director render (--check-director-render), whose frames and sound are
pinned, prints its pinned hashes with every key at its default and with
enhanced anti-aliasing at 16x: director frames draw units as without it.
"""
import argparse
import hashlib
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

# A command that starts the executable under test, such as the compatibility
# layer a cross build runs its tests through (tools/build_windows.sh --run-tests
# sets it); empty runs the executable directly.
RUNNER = shlex.split(os.environ.get("OA_TEST_RUNNER", ""))

TICKS = 150
COMBAT_UNITS = 16
SEED = 1234567
FRAME_RATE = 30

# A preferences file's first line.
PREFERENCES_HEADER = "open-annihilation-preferences 1"
# 3.1c's own SwitchAlt, which Select groups without Alt sets.
SWITCH_ALT_KEY = "Total Annihilation|SwitchAlt"
# Every Open Annihilation setting's key with its default value, as a
# preferences file named on the command line gives it.
DEFAULTS = {
    "open-annihilation.path-search-nodes": "1333",
    "open-annihilation.wheel-zoom": "1",
    "open-annihilation.escape-opens-menu": "0",
    "open-annihilation.unit-limit": "250",
    "open-annihilation.max-fps": "120",
    "open-annihilation.anti-aliasing": "1",
    "open-annihilation.frame-stats": "0",
}
# The settings that change only the look or the input, away from their defaults.
PRESENTATION = {
    "open-annihilation.wheel-zoom": "0",
    "open-annihilation.escape-opens-menu": "1",
    "open-annihilation.max-fps": "40",
    "open-annihilation.frame-stats": "1",
    SWITCH_ALT_KEY: "1",
}
ANTI_ALIASING_KEY = "open-annihilation.anti-aliasing"
ANTI_ALIASING_LEVELS = (2, 3, 4, 8, 16)
PATH_SEARCH_KEY = "open-annihilation.path-search-nodes"
DOUBLED_PATH_NODES = 2666
UNIT_LIMIT_KEY = "open-annihilation.unit-limit"
RAISED_UNIT_LIMIT = 500

RUN_LINE = re.compile(r"frame run: .* world digest ([0-9a-f]{16})")


def write_preferences(path, values):
    """Writes a preferences file holding the given keys."""
    lines = [PREFERENCES_HEADER]
    lines += [f'"{key}" "{value}"' for key, value in sorted(values.items())]
    path.write_text("\n".join(lines) + "\n")


def skirmish(native, game_dir, workdir, name, values):
    """Plays the drawn skirmish from a preferences file, or none when values is None.

    Returns the world digest, the trace stream's bytes and the last frame's bytes.
    """
    profile = workdir / f"{name}.conf"
    if values is not None:
        write_preferences(profile, values)
    trace = workdir / f"{name}.trace"
    frame = workdir / f"{name}.ppm"
    result = subprocess.run(
        [*RUNNER, str(native), "--game-dir", str(game_dir), "--skip-intro", "--mute",
         "--headless-check", "--preferences-file", str(profile),
         "--match-ticks", str(TICKS), "--combat", str(COMBAT_UNITS), "--march",
         "--seed", str(SEED), "--trace-digest", str(trace), "--frame-rate", str(FRAME_RATE),
         "--snapshot", str(frame)],
        cwd=workdir, timeout=600, check=False, capture_output=True, text=True,
    )
    match = RUN_LINE.search(result.stdout)
    if result.returncode != 0 or match is None:
        print(result.stdout + result.stderr, end="")
        raise SystemExit(f"skirmish {name} exited with {result.returncode}")
    return match.group(1), trace.read_bytes(), frame.read_bytes()


def director_render(native, game_dir, workdir, name, values, pinned):
    """Runs the director render from a preferences file and checks its pinned hashes."""
    directory = workdir / name
    directory.mkdir()
    profile = directory / "director-render.conf"
    write_preferences(profile, values)
    environment = dict(os.environ, SDL_VIDEO_DRIVER="dummy", SDL_AUDIO_DRIVER="dummy")
    result = subprocess.run(
        [*RUNNER, str(native), "--game-dir", str(game_dir), "--mute", "--check-director-render",
         "--preferences-file", str(profile)],
        cwd=directory, env=environment, timeout=600, check=False, capture_output=True, text=True,
    )
    if result.returncode != 0 or pinned not in result.stdout:
        print(result.stdout + result.stderr, end="")
        raise SystemExit(f"the director render {name} did not give its pinned frames and sound")


def short(data):
    """Returns a short hash of some bytes, for the messages."""
    return hashlib.sha256(data).hexdigest()[:16]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--native", type=Path, required=True)
    parser.add_argument("--game-dir", type=Path, required=True)
    parser.add_argument("--scratch-root", type=Path, required=True)
    parser.add_argument("--director-hashes", required=True,
                        help="the line the director render prints with its pinned hashes")
    args = parser.parse_args()
    native = args.native.resolve()
    game_dir = args.game_dir.resolve()
    args.scratch_root.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="native-engine-settings-",
                                     dir=args.scratch_root.resolve()) as temporary:
        workdir = Path(temporary)
        digest, trace, frame = skirmish(native, game_dir, workdir, "none", None)
        defaults = skirmish(native, game_dir, workdir, "defaults", DEFAULTS)
        if defaults != (digest, trace, frame):
            raise SystemExit(
                f"every key at its default moved the game: digest {defaults[0]} trace "
                f"{short(defaults[1])} frame {short(defaults[2])}, not {digest} {short(trace)} "
                f"{short(frame)}")
        shown = skirmish(native, game_dir, workdir, "presentation", {**DEFAULTS, **PRESENTATION})
        if shown[:2] != (digest, trace):
            raise SystemExit(f"the look and input settings moved the world: digest {shown[0]}, "
                             f"not {digest}")
        for level in ANTI_ALIASING_LEVELS:
            smoothed = skirmish(native, game_dir, workdir, f"aa{level}",
                                {**DEFAULTS, ANTI_ALIASING_KEY: str(level)})
            if smoothed[:2] != (digest, trace):
                raise SystemExit(f"enhanced anti-aliasing at {level}x moved the world: digest "
                                 f"{smoothed[0]}, not {digest}")
            if smoothed[2] == frame:
                raise SystemExit(f"enhanced anti-aliasing at {level}x drew the same last frame")
        paths = [skirmish(native, game_dir, workdir, f"path{run}",
                          {**DEFAULTS, PATH_SEARCH_KEY: str(DOUBLED_PATH_NODES)})
                 for run in (1, 2)]
        if paths[0][:2] != paths[1][:2]:
            raise SystemExit("two runs with Pathfinding cycles at 2x wrote different streams")
        if paths[0][1] == trace:
            raise SystemExit("Pathfinding cycles at 2x left the marching army's paths as they were")
        limits = [skirmish(native, game_dir, workdir, f"limit{run}",
                           {**DEFAULTS, UNIT_LIMIT_KEY: str(RAISED_UNIT_LIMIT)})
                  for run in (1, 2)]
        if limits[0][:2] != limits[1][:2]:
            raise SystemExit(f"two runs with a unit limit of {RAISED_UNIT_LIMIT} wrote "
                             "different streams")
        director_render(native, game_dir, workdir, "director-defaults", DEFAULTS,
                        args.director_hashes)
        director_render(native, game_dir, workdir, "director-aa16",
                        {**DEFAULTS, ANTI_ALIASING_KEY: "16"}, args.director_hashes)
        print(f"engine settings determinism: digest {digest} with no preferences, every default, "
              f"the look and input settings and anti-aliasing at "
              f"{', '.join(f'{level}x' for level in ANTI_ALIASING_LEVELS)}; Pathfinding cycles "
              f"at 2x {paths[0][0]}, unit limit {RAISED_UNIT_LIMIT} {limits[0][0]}; the director "
              f"render's pinned frames and sound at every default and at 16x")


if __name__ == "__main__":
    main()
