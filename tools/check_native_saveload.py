#!/usr/bin/env python3
# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

"""Save a headless skirmish at a tick, load it back and compare world digests.

Run A fights COMBAT_UNITS units a side beside the commanders, with a factory
queue, a building, a patrol, a guard and a move given, for SAVE_TICK ticks and
saves at that tick, so its closing digest is the world the save captured.
Two ticks before the save a tree catches fire, one feature starts its die
sequence, another its reclamate sequence and a fourth is cleared away, so
the save holds them while they play. Run B starts from the save without
stepping and must print the same tick, unit count, digest, orders and saved
features. Run C resumes the save for RESUME_TICKS ticks to show the loaded
match keeps simulating and keeps those orders.

The digest covers each unit's record, economy, weapons, COB script state,
movement state and saved orders, the map's metal, placing-player and sight
words, the camera and the meteor state, and every run must report that the
save wrote and the load restored all script, movement, economy and order
state. The saved features are compared as the Features section writes them:
the counts of normal, 3D and animating records, the animating ones playing a
burn, die or reclamate sequence, and a digest of the section, leaving out the
features on plots a load hides under the map's edges, which do not come back.
The digest follows the order of the feature type names, so it matches across
a load only on a map whose schema names no feature type beyond the map's own,
as the map this check plays does.
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

# Wall-time limit per game run; generous because a loaded machine stretches
# the headless checks without extra CPU work.
RUN_TIMEOUT_SECONDS = 900

SAVE_TICK = 150
RESUME_TICKS = 30
COMBAT_UNITS = 6
DIGEST = re.compile(r"^saveload: tick (\d+) units (\d+) digest ([0-9a-f]{16})$", re.M)
FAILURES = re.compile(r"; (save|restore) failures (\d+)$", re.M)
ORDERS = re.compile(r"^saveload: orders (\d+)((?: \S+=\d+)*)$", re.M)
FEATURES = re.compile(
    r"^saveload: features normal (\d+) 3d (\d+) animating (\d+) burn (\d+) die (\d+) "
    r"reclaim (\d+) digest ([0-9a-f]{16})$", re.M)
# Missions the given orders hold at the save: the commander's building, the
# lab's queue, the patrol, the guard and the move.
SAVED_MISSIONS = {"MobileBuild", "BuildingBuild", "Patrol", "Follow_Ground", "Move_Ground"}
# Missions that still run after the resumed ticks.
RESUMED_MISSIONS = {"BuildingBuild", "Patrol", "Follow_Ground"}


def run(native, game_dir, profile, cwd, *extra):
    result = subprocess.run(
        [*RUNNER, str(native), "--game-dir", str(game_dir), "--skip-intro", "--mute",
         "--headless-check", "--preferences-file", str(profile), *extra],
        cwd=cwd, timeout=RUN_TIMEOUT_SECONDS, check=False, capture_output=True, text=True,
    )
    print(result.stdout, end="")
    if result.returncode != 0:
        print(result.stderr, end="")
        raise SystemExit(f"open-annihilation exited with {result.returncode}")
    match = DIGEST.search(result.stdout)
    if match is None:
        print(result.stderr, end="")
        raise SystemExit("no saveload digest line in the output")
    orders = ORDERS.search(result.stdout)
    if orders is None:
        raise SystemExit("no saveload orders line in the output")
    missions = dict(item.split("=") for item in orders.group(2).split())
    features = FEATURES.search(result.stdout)
    if features is None:
        raise SystemExit("no saveload features line in the output")
    reports = FAILURES.findall(result.stdout)
    if not reports:
        raise SystemExit("no save or restore failure count in the output")
    for kind, count in reports:
        if int(count) != 0:
            raise SystemExit(f"{kind} dropped state of {count} unit parts")
    return (int(match.group(1)), int(match.group(2)), match.group(3), int(orders.group(1)),
            missions, features.groups())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--native", type=Path, required=True)
    parser.add_argument("--game-dir", type=Path, required=True)
    parser.add_argument("--scratch-root", type=Path, required=True)
    args = parser.parse_args()
    native = args.native.resolve()
    game_dir = args.game_dir.resolve()
    scratch_root = args.scratch_root.resolve()
    scratch_root.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="native-saveload-", dir=scratch_root) as temporary:
        profile = Path(temporary) / "preferences.conf"
        save = Path(temporary) / "savegame" / "tick.sav"
        saved = run(native, game_dir, profile, temporary, "--combat", str(COMBAT_UNITS),
                    "--give-orders", "--match-ticks", str(SAVE_TICK), "--save-after", str(SAVE_TICK),
                    "--save-file", str(save))
        if saved[0] != SAVE_TICK or not save.is_file():
            raise SystemExit(f"run A did not save at tick {SAVE_TICK}")
        if saved[1] == 0:
            raise SystemExit("run A saved a world without units")
        burning, dying, reclaimed = (int(count) for count in saved[5][3:6])
        if burning == 0 or dying == 0 or reclaimed == 0:
            raise SystemExit(
                f"run A saved {burning} burning, {dying} dying and {reclaimed} reclaimed features")
        missing = SAVED_MISSIONS - saved[4].keys()
        if missing:
            raise SystemExit(f"run A saved no {', '.join(sorted(missing))} orders")
        loaded = run(native, game_dir, profile, temporary, "--load", str(save), "--match-ticks", "0")
        if loaded != saved:
            raise SystemExit(
                f"loaded world differs: saved tick {saved[0]} units {saved[1]} digest {saved[2]} "
                f"orders {saved[3]} {saved[4]} features {saved[5]}, loaded tick {loaded[0]} "
                f"units {loaded[1]} digest {loaded[2]} orders {loaded[3]} {loaded[4]} "
                f"features {loaded[5]}")
        resumed = run(native, game_dir, profile, temporary,
                      "--load", str(save), "--match-ticks", str(RESUME_TICKS))
        if resumed[0] != SAVE_TICK + RESUME_TICKS or resumed[1] == 0:
            raise SystemExit(f"resumed match did not reach tick {SAVE_TICK + RESUME_TICKS}")
        dropped = RESUMED_MISSIONS - resumed[4].keys()
        if dropped:
            raise SystemExit(f"resumed match lost its {', '.join(sorted(dropped))} orders")
        print(f"saveload check: tick {SAVE_TICK} digest {saved[2]} with {saved[1]} units, "
              f"{saved[3]} orders and features {saved[5][6]} ({saved[5][2]} animating) matches "
              f"after load; resumed to tick {resumed[0]} with {resumed[1]} units and "
              f"{resumed[3]} orders")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
