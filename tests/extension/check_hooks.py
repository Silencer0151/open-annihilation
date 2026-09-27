#!/usr/bin/env python3
# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

"""Check that open-annihilation calls the hooks of its extension table.

Runs an open-annihilation built with the recorder test extension
(-DOA_EXTENSIONS_TARGET=oa-extension-recorder) and reads the counts each run
leaves in its --record-hooks file. Every run must record at least the calls
RUNS lists for it; a hook recorded with an enumerator ("frame pump") names
that call only. --runs picks the runs:

  options  the option hooks, without game data: --help, the -r switch, an
           unknown option and a game directory that does not exist;
  game     the rest, over the installation OA_GAME_DIR names: a headless
           skirmish, the headless --check-navigation run, and interactive
           main menu frames and --check-multiplayer-menu through SDL's dummy
           drivers. When OA_GAME_DIR is unset, empty or names no directory
           it exits with --skip-code (OA_GAME_DATA_SKIP_CODE), which ctest
           reports as skipped, or fails when OA_REQUIRE_GAME_DATA=1.

HOOKS must name the hooks src/app/include/oa/app/extension.hpp declares, in its order.
With every run of both sets, each of them is reached except those UNREACHED
lists, which only a shared match reaches; so are match_event's left,
results_reported and watching_kept, which these runs do not ask for. A new
hook must be added to the recorder, to HOOKS and to a run here or to
UNREACHED.
"""
import argparse
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
import tempfile

# A command that starts the executable under test, such as the compatibility
# layer a cross build runs its tests through; empty runs it directly.
RUNNER = shlex.split(os.environ.get("OA_TEST_RUNNER", ""))
# Wall-time limit per game run; a loaded machine stretches headless runs.
RUN_TIMEOUT_SECONDS = 900
# SDL's drivers for runs that open the game's window without a display.
DUMMY_DRIVERS = {"SDL_VIDEO_DRIVER": "dummy", "SDL_AUDIO_DRIVER": "dummy", "SDL_RENDER_DRIVER": "software"}

EXTENSION_HEADER = Path(__file__).resolve().parents[2] / "src" / "app" / "include" / "oa" / "app" / "extension.hpp"
# A hook's declaration in the table: a named pointer to a function whose
# parameters may hold one more (check_console's), default-initialised.
HOOK_RE = re.compile(r"\(\*(\w+)\)\s*\((?:[^()]|\((?:[^()]|\([^()]*\))*\))*\)\s*\{\}\s*;")

# Every hook of src/app/include/oa/app/extension.hpp, as the recorder names them.
HOOKS = [
    "take_option", "check_options", "switch_handler", "text", "startup", "register_screens", "ready",
    "frontend_entry", "frontend_states", "run_mode", "start_scene", "shutdown", "select_multiplayer",
    "frontend_game", "launched_by_service", "check_multiplayer_menu", "state", "frame",
    "simulation_step", "outcome_ready", "match_game", "match_event", "disconnect_text",
    "give_resources", "message_hooks", "player_gone", "console_host", "check_console", "draw_loading",
    "draw_match_hud",
]
# Hooks only a match played with other machines reaches.
UNREACHED = {
    "disconnect_text": "the end-of-game screen asks for it only after a shared match",
}


class Run:
    """One start of open-annihilation: its arguments, whether it needs the game, and what it must record.

    expected names calls recorded at least once; at_least maps a call to
    the fewest times it must be recorded, for a hook every run reaches
    once through --record-hooks itself.
    """

    def __init__(self, name, arguments, expected, *, at_least=None, game=False, status=0, output="",
                 dummy=False):
        self.name = name
        self.arguments = arguments
        self.expected = expected
        self.at_least = at_least or {}
        self.game = game
        self.status = status
        self.output = output
        self.dummy = dummy


# Every run passes --record-hooks first, which take_option takes; a run that
# checks take_option passes another option the engine does not know.
RUNS = {
    "options": [
        Run("help", ["--help"],
            ["text usage_checks", "text usage_runs", "text usage_switches", "text usage_note"],
            output="usage: open-annihilation"),
        Run("register-switch", ["-r"], ["switch_handler", "text register_switch"],
            status=1, output="registers the game for multiplayer"),
        Run("unknown-option", ["--not-an-option"], [], at_least={"take_option": 2}, status=1,
            output="unknown option: --not-an-option"),
        Run("missing-game-dir", ["--game-dir", "{scratch}/no-such-game"],
            ["switch_handler", "check_options"], status=1, output="game directory does not exist"),
    ],
    "game": [
        Run("headless-skirmish",
            ["--game-dir", "{game}", "--skip-intro", "--mute", "--headless-check", "--match-ticks", "60"],
            ["startup", "runtime_member", "register_screens", "ready", "frontend_entry", "frontend_states",
             "frontend_game", "launched_by_service", "run_mode start",
             "run_mode headless_first", "run_mode headless", "match_game", "match_event torn_down",
             "draw_loading", "draw_match_hud", "state"],
            game=True),
        Run("headless-navigation",
            ["--game-dir", "{game}", "--skip-intro", "--mute", "--headless-check", "--check-navigation"],
            ["console_host", "check_console", "give_resources", "message_hooks", "simulation_step",
             "outcome_ready", "player_gone", "match_event finished", "match_event results_released"],
            game=True),
        Run("main-menu-frames", ["--game-dir", "{game}", "--skip-intro", "--mute", "--frames", "30"],
            ["start_scene", "frame pump", "frame after_pump", "shutdown", "state"], game=True, dummy=True),
        Run("multiplayer-menu", ["--game-dir", "{game}", "--skip-intro", "--mute", "--check-multiplayer-menu"],
            ["check_multiplayer_menu", "select_multiplayer"], game=True, dummy=True,
            output="recorder: --check-multiplayer-menu"),
    ],
}


def declared_hooks():
    """The hooks struct Extension declares in src/app/include/oa/app/extension.hpp, in order."""
    text = EXTENSION_HEADER.read_text(encoding="utf-8")
    start = text.index("struct Extension {")
    return HOOK_RE.findall(text[start:text.index("\n};", start)])


def read_record(path):
    """The counts a --record-hooks file holds, by hook and enumerator."""
    counts = {}
    if not path.is_file():
        return counts
    for line in path.read_text(encoding="utf-8").splitlines():
        key, _, count = line.rpartition(" ")
        if key and count.isdigit():
            counts[key] = int(count)
    return counts


def recorded(counts, expected):
    """How many calls of expected, a hook or a hook and enumerator, counts hold."""
    return sum(count for key, count in counts.items() if key == expected or key.startswith(expected + " "))


def run_game(game, run, scratch, game_dir):
    """Starts open-annihilation for a run; returns the failures it found and the hooks it recorded."""
    record = scratch / f"{run.name}.hooks"
    preferences = scratch / f"{run.name}.conf"
    arguments = [argument.format(game=game_dir, scratch=scratch) for argument in run.arguments]
    command = [*RUNNER, str(game), "--record-hooks", str(record), *arguments]
    if run.game:
        command += ["--preferences-file", str(preferences)]
    environment = dict(os.environ)
    if run.dummy:
        environment.update(DUMMY_DRIVERS)
    result = subprocess.run(command, cwd=scratch, env=environment, capture_output=True, text=True,
                            errors="replace", timeout=RUN_TIMEOUT_SECONDS, check=False)
    output = result.stdout + result.stderr
    failures = []
    if result.returncode != run.status:
        failures.append(f"exit status {result.returncode}, expected {run.status}")
    if run.output and run.output not in output:
        failures.append(f"no '{run.output}' in its output")
    counts = read_record(record)
    for expected in run.expected:
        if recorded(counts, expected) == 0:
            failures.append(f"'{expected}' was not recorded")
    for expected, fewest in run.at_least.items():
        calls = recorded(counts, expected)
        if calls < fewest:
            failures.append(f"'{expected}' was recorded {calls} times, expected at least {fewest}")
    if failures:
        tail = "\n".join(output.splitlines()[-20:])
        failures.append(f"its output ended:\n{tail}")
    return failures, counts


def main():
    """Runs the chosen runs and reports what they did not record; returns the exit status."""
    parser = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    parser.add_argument("--game", type=Path, required=True, help="open-annihilation built with the recorder")
    parser.add_argument("--runs", choices=sorted(RUNS), required=True)
    parser.add_argument("--scratch-root", type=Path, required=True)
    parser.add_argument("--skip-code", type=int,
                        help="exit status of a skipped game run (OA_GAME_DATA_SKIP_CODE); needed by --runs game")
    args = parser.parse_args()
    if args.runs == "game" and args.skip_code is None:
        parser.error("--runs game needs --skip-code")
    # Path("") is the current directory, so an empty OA_GAME_DIR is checked first.
    game_dir = os.environ.get("OA_GAME_DIR", "")
    if args.runs == "game" and (not game_dir or not Path(game_dir).is_dir()):
        if os.environ.get("OA_REQUIRE_GAME_DATA") == "1":
            print("check_hooks: OA_REQUIRE_GAME_DATA is set but OA_GAME_DIR names no installation")
            return 1
        print("check_hooks: skipped: OA_GAME_DIR names no installation")
        return args.skip_code
    if declared_hooks() != HOOKS:
        print(f"check_hooks: extension.hpp declares {declared_hooks()}; HOOKS and the recorder must follow it")
        return 1
    args.scratch_root.mkdir(parents=True, exist_ok=True)
    failed = False
    reached = set()
    with tempfile.TemporaryDirectory(prefix=f"hooks-{args.runs}-", dir=args.scratch_root) as temporary:
        scratch = Path(temporary)
        for run in RUNS[args.runs]:
            failures, counts = run_game(args.game.resolve(), run, scratch, game_dir)
            reached.update(key.split(" ", 1)[0] for key in counts)
            for failure in failures:
                print(f"check_hooks: {run.name}: {failure}")
            failed = failed or bool(failures)
            if not failures:
                print(f"check_hooks: {run.name}: recorded {len(run.expected) + len(run.at_least)} expected "
                      f"calls among {len(counts)} kinds")
    if args.runs == "game":
        # The option runs reach the option hooks; together they reach the rest.
        options = {key.split(" ", 1)[0] for run in RUNS["options"] for key in [*run.expected, *run.at_least]}
        missing = sorted(set(HOOKS) - reached - options - set(UNREACHED))
        for hook in missing:
            print(f"check_hooks: no run reached {hook}; add a run that does, or list it in UNREACHED")
        failed = failed or bool(missing)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
