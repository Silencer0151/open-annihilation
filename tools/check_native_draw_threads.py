#!/usr/bin/env python3
# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

"""Check that the drawing threads never change a frame.

The seeded headless skirmish, its armies fighting, is drawn frame by frame
(--frame-rate) with the camera's scroll held, at zoom 1, zoomed in and zoomed
out, on one drawing thread and on several (--draw-threads). The terrain fill
and the fog split their rows into bands by the data alone, so every run of
a view must print the same frames digest, a digest of every frame's drawn
battlefield, and the same world digest; and each run must say it drew on the
threads it was given. (The frame's conversion for the window, which a
headless run does not make, is checked by app-xrgb-conversion.)
"""
import argparse
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

TICKS = 45
COMBAT_UNITS = 12
SEED = 20261001
FRAME_RATE = 60
# The views: window size and zoom. Zoom 1 copies whole tile rows; the others
# sample the map through the zoom's step, in and out.
VIEWS = (("1280x720", "1"), ("1024x768", "1.37"), ("1024x768", "0.6"))
# One drawing thread, then counts that split the bands unevenly and evenly.
THREAD_COUNTS = (1, 3, 4)

RUN_LINE = re.compile(
    r"frame run: .* world digest ([0-9a-f]{16}), frames digest ([0-9a-f]{16}), "
    r"drawing threads (\d+)"
)


def run(native, game_dir, workdir, resolution, zoom, threads):
    """Draws the skirmish's frames and returns its world digest and frames digest."""
    name = f"{resolution}-zoom-{zoom}-threads-{threads}"
    result = subprocess.run(
        [*RUNNER, str(native), "--game-dir", str(game_dir), "--skip-intro", "--mute",
         "--headless-check", "--preferences-file", str(workdir / f"{name}.conf"),
         "--match-ticks", str(TICKS), "--combat", str(COMBAT_UNITS), "--seed", str(SEED),
         "--frame-rate", str(FRAME_RATE), "--scroll-camera", "--resolution", resolution,
         "--zoom", zoom, "--draw-threads", str(threads)],
        cwd=workdir, timeout=900, check=False, capture_output=True, text=True,
    )
    if result.returncode != 0:
        print(result.stdout + result.stderr, end="")
        raise SystemExit(f"{name}: the run exited with {result.returncode}")
    match = RUN_LINE.search(result.stdout)
    if match is None:
        print(result.stdout + result.stderr, end="")
        raise SystemExit(f"{name}: the run printed no frame run line")
    if int(match[3]) != threads:
        raise SystemExit(f"{name}: drew on {match[3]} threads, not {threads}")
    return match[1], match[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--native", type=Path, required=True)
    parser.add_argument("--game-dir", type=Path, required=True)
    parser.add_argument("--scratch-root", type=Path, required=True)
    args = parser.parse_args()
    native = args.native.resolve()
    game_dir = args.game_dir.resolve()
    args.scratch_root.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="native-draw-threads-",
                                     dir=args.scratch_root.resolve()) as temporary:
        workdir = Path(temporary)
        for resolution, zoom in VIEWS:
            alone = run(native, game_dir, workdir, resolution, zoom, THREAD_COUNTS[0])
            for threads in THREAD_COUNTS[1:]:
                digests = run(native, game_dir, workdir, resolution, zoom, threads)
                if digests != alone:
                    raise SystemExit(
                        f"{resolution} at zoom {zoom}: {threads} threads drew frames digest "
                        f"{digests[1]} (world {digests[0]}), one thread {alone[1]} "
                        f"(world {alone[0]})"
                    )
            print(f"  {resolution} at zoom {zoom}: frames digest {alone[1]} on "
                  f"{', '.join(str(count) for count in THREAD_COUNTS)} threads")
        print(f"native draw threads: {TICKS} ticks drawn at {FRAME_RATE} frames a second give "
              f"the same frames on every count of drawing threads")


if __name__ == "__main__":
    main()
