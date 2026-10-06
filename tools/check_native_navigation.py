#!/usr/bin/env python3
# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

"""Run the asset-backed native navigation check without touching user settings.

--group runs one group of the check (--check-navigation GROUP), which stands
alone; without it the whole check runs in one game.
"""
import argparse
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

# A command that starts the executable under test, such as the compatibility
# layer a cross build runs its tests through (tools/build_windows.sh --run-tests
# sets it); empty runs the executable directly.
RUNNER = shlex.split(os.environ.get("OA_TEST_RUNNER", ""))

# Wall-time limit per game run; generous because a loaded machine stretches
# the headless checks without extra CPU work. OA_TEST_TIMEOUT_SCALE, a
# positive number, multiplies it for a build whose run-time error checks
# make the game several times slower.
RUN_TIMEOUT_SECONDS = 900 * float(os.environ.get("OA_TEST_TIMEOUT_SCALE") or 1)
# The groups --check-navigation takes.
GROUPS = ("screens", "orders", "outcomes", "zoom", "zoom-1366x768", "zoom-1920x1080",
          "zoom-2560x1440", "campaign")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--native", type=Path, required=True)
    parser.add_argument("--game-dir", type=Path, required=True)
    parser.add_argument("--scratch-root", type=Path, required=True)
    parser.add_argument("--group", choices=GROUPS, help="the group to run; every group without")
    args = parser.parse_args()
    native = args.native.resolve()
    game_dir = args.game_dir.resolve()
    scratch_root = args.scratch_root.resolve()
    scratch_root.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="native-check-", dir=scratch_root) as temporary:
        profile = Path(temporary) / "preferences.conf"
        group = [args.group] if args.group else []
        result = subprocess.run(
            [*RUNNER, str(native), "--game-dir", str(game_dir), "--skip-intro", "--mute",
             "--headless-check", "--check-navigation", *group, "--preferences-file",
             str(profile)],
            cwd=temporary, timeout=RUN_TIMEOUT_SECONDS, check=False,
        )
        return result.returncode


if __name__ == "__main__":
    raise SystemExit(main())
