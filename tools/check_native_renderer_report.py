#!/usr/bin/env python3
# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

"""Check the line the game logs once its renderer is made.

The game starts with --skip-intro --mute --frames 1 on the dummy SDL drivers,
with SDL_RENDER_DRIVER unset, so the game walks SDL's render drivers in SDL's
order and ends on SDL's software renderer, which has no adapter and no texture
limit (native-renderer-walk checks the refusals logged before it). The named
preferences file leaves Hardware acceleration at its default, Off, so the
line gives that as the reason the processor draws everything. The start
must end with status 0 and log, once and as a whole line, exactly:

  open-annihilation: graphics: software on dummy, textures of any size;
  standard tier: the processor draws everything (hardware acceleration is
  off)

(on one line).
"""
import argparse
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

# A command that starts the executable under test, such as the compatibility
# layer a cross build runs its tests through; empty runs it directly.
RUNNER = shlex.split(os.environ.get("OA_TEST_RUNNER", ""))
# The line a start on SDL's software renderer and the dummy video driver logs.
EXPECTED = ("open-annihilation: graphics: software on dummy, textures of any size; "
            "standard tier: the processor draws everything (hardware acceleration is off)")
FRAMES = "1"
RUN_TIMEOUT = 900


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--native", type=Path, required=True, help="open-annihilation")
    parser.add_argument("--game-dir", type=Path, required=True)
    parser.add_argument("--scratch-root", type=Path, required=True)
    args = parser.parse_args()
    args.scratch_root.mkdir(parents=True, exist_ok=True)
    environment = dict(os.environ, SDL_VIDEO_DRIVER="dummy", SDL_AUDIO_DRIVER="dummy")
    environment.pop("SDL_RENDER_DRIVER", None)
    with tempfile.TemporaryDirectory(prefix="native-renderer-report-",
                                     dir=args.scratch_root.resolve()) as scratch:
        workdir = Path(scratch)
        result = subprocess.run(
            [*RUNNER, str(args.native.resolve()), "--game-dir", str(args.game_dir.resolve()),
             "--skip-intro", "--mute", "--frames", FRAMES,
             "--preferences-file", str(workdir / "renderer-report.conf")],
            cwd=workdir, env=environment, timeout=RUN_TIMEOUT, check=False, capture_output=True,
            text=True, errors="replace")
    output = result.stdout + result.stderr
    if result.returncode != 0:
        print(output, end="")
        print(f"FAIL open-annihilation exited with {result.returncode}")
        return 1
    logged = output.splitlines().count(EXPECTED)
    if logged != 1:
        print(output, end="")
        print(f"FAIL the start logged {logged} lines reading {EXPECTED!r}, not one")
        return 1
    print(EXPECTED)
    print("the renderer report check passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
