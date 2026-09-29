#!/usr/bin/env python3
# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

"""Save a headless skirmish at a tick, load it back and compare world digests.

Run A fights COMBAT_UNITS units a side beside the commanders, with a factory
queue, a building, a patrol, a guard and a move given, for SAVE_TICK ticks and
saves at that tick, so its closing digest is the world the save captured. An
Atlas picks a unit up onto its link piece and is flying it off at the save,
and a transport ship holds units that started aboard. Two ticks before the
save a tree catches fire, one feature starts its die sequence, another its
reclamate sequence and a fourth is cleared away, so the save holds them while
they play. Run B starts from the save without stepping and must print the
same tick, unit count, digest, orders and saved features. Run C resumes the
save for RESUME_TICKS ticks to show the loaded match keeps simulating and
keeps those orders, the transports still carrying their units.

The digest covers each unit's record, economy, weapons, COB script state,
movement state and saved orders, the map's metal, placing-player and sight
words, the camera and the meteor state, and every run must report that the
save wrote and the load restored all script, movement, economy and order
state. The saved features are compared as the Features section writes them:
the counts of normal, 3D and animating records, the animating ones playing a
burn, die or reclamate sequence, and a digest of the section, leaving out the
features on plots a load hides under the map's edges, which do not come back.
The digest names each feature type by its name, since a load orders the
feature type table its own way.

Two campaign missions then check carried units across a save and a load: one
where an Atlas starts with a unit aboard, on a map whose schema places
feature types beyond the map's own, one where Bears and transport ships start
loaded. Each is saved at CARRIED_SAVE_TICK and loaded back, and must come back
with the same unit digest, which covers each unit's carrier, its piece (none
for a unit in the hold, which is not drawn) and its attacker, the same orders,
BeCarried among them, and the same saved features.
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
# lab's queue, the patrol, the guard, the move, the Atlas flying its unit off
# and the carried units.
SAVED_MISSIONS = {"MobileBuild", "BuildingBuild", "Patrol", "Follow_Ground", "Move_Ground",
                  "VTOL_Unload", "BeCarried"}
# Missions that still run after the resumed ticks.
RESUMED_MISSIONS = {"BuildingBuild", "Patrol", "Follow_Ground", "VTOL_Unload", "BeCarried"}
# Units aboard the Atlas and the transport ship at the save and after the
# resumed ticks, at the least.
TRANSPORTED = 1 + 3
# Campaign missions whose units start aboard transports, by campaign name
# (as its file names it) and mission index, and what carries them.
CARRIED_MISSIONS = (
    ("Core Campaign", 22, "an Atlas"),
    ("Arm Campaign - Core Contingency ", 11, "Bears and transport ships"),
)
CARRIED_SAVE_TICK = 20


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
        if int(saved[4]["BeCarried"]) < TRANSPORTED:
            raise SystemExit(f"run A saved {saved[4]['BeCarried']} carried units, not the "
                             f"{TRANSPORTED} aboard the Atlas and the transport ship")
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
        if int(resumed[4]["BeCarried"]) < TRANSPORTED:
            raise SystemExit(f"resumed match carries {resumed[4]['BeCarried']} units, not the "
                             f"{TRANSPORTED} aboard the Atlas and the transport ship")
        print(f"saveload check: tick {SAVE_TICK} digest {saved[2]} with {saved[1]} units "
              f"({saved[4]['BeCarried']} carried), {saved[3]} orders and features {saved[5][6]} "
              f"({saved[5][2]} animating) matches after load; resumed to tick {resumed[0]} with "
              f"{resumed[1]} units and {resumed[3]} orders")
        for campaign, mission, carriers in CARRIED_MISSIONS:
            carried_save = Path(temporary) / "savegame" / f"carried{mission}.sav"
            carried = run(native, game_dir, profile, temporary, "--campaign", campaign,
                          "--mission", str(mission), "--match-ticks", str(CARRIED_SAVE_TICK),
                          "--save-after", str(CARRIED_SAVE_TICK), "--save-file", str(carried_save))
            if carried[0] != CARRIED_SAVE_TICK or not carried_save.is_file():
                raise SystemExit(f"{campaign.strip()} mission {mission} did not save")
            aboard = int(carried[4].get("BeCarried", 0))
            if aboard == 0:
                raise SystemExit(f"{campaign.strip()} mission {mission} saved no units aboard "
                                 f"{carriers}")
            reloaded = run(native, game_dir, profile, temporary, "--load", str(carried_save),
                           "--match-ticks", "0")
            if reloaded != carried:
                raise SystemExit(
                    f"{campaign.strip()} mission {mission} with units aboard {carriers} differs "
                    f"after load: saved digest {carried[2]} orders {carried[4]} features "
                    f"{carried[5]}, loaded digest {reloaded[2]} orders {reloaded[4]} features "
                    f"{reloaded[5]}")
            print(f"saveload check: {aboard} units aboard {carriers} ({campaign.strip()} mission "
                  f"{mission}) match after load, digest {carried[2]}, features {carried[5][6]}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
