#!/usr/bin/env python3
# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

"""Compare two trace streams and name the first tick they part at.

A stream (src/sim/trace oa/sim/trace.hpp) is a 16-byte header (magic "OAT1",
version 1, record size 24, generator seed) and 24-byte records of six
little-endian words whose first word is the record kind: kind 1 per sampled
tick (tick, live units, unit fold, economy fold, generator) and kind 0x10 per
section digest (tick, section, digest low, digest high, items). Records of
other kinds are skipped. Records are matched by tick number, so streams
sampled at different intervals compare over the ticks both hold. Section
digests are compared where both streams carry them.

With --units, the unit dumps ("tick=T slot=S name=value ...") of the two runs
name the unit slot and fields that differ at the first diverging tick.
When the generator words differ and both streams start from the same seed,
each word is named by how many draws from the seeded state reach it.

Exit status: 0 when the shared ticks agree, 1 when they differ or share no
tick, 2 when an input cannot be read.
"""
import argparse
import struct
import sys
from pathlib import Path

MAGIC = 0x3154414F
VERSION = 1
HEADER_SIZE = 16
RECORD_SIZE = 24
KIND_TICK = 1
KIND_SECTION = 0x10

TICK_WORDS = ("live units", "unit fold", "economy fold", "generator")
SECTIONS = ("total", "units", "weapons", "orders", "projectiles", "players", "random")
# Sections in the order a difference is looked for; the total last, as it
# moves whenever any other does.
SECTION_ORDER = (1, 2, 3, 4, 5, 6, 0)
# The game's shared generator: seeding stores (seed ^ mask) | 1 and each draw
# steps it as the minimal-standard LCG in 32-bit words.
SEED_MASK = 0x66E29572
LCG_MULTIPLIER = 0x41A7
LCG_QUOTIENT = 0x1F31D
LCG_MODULUS = 0x7FFFFFFF
DRAW_SEARCH_LIMIT = 1 << 20


class StreamError(Exception):
    pass


class Stream:
    def __init__(self, path):
        self.path = path
        try:
            raw = Path(path).read_bytes()
        except OSError as error:
            raise StreamError(f"{path}: {error.strerror}")
        if len(raw) < HEADER_SIZE:
            raise StreamError(f"{path}: too short to hold a header")
        magic, version, record_size, self.seed = struct.unpack_from("<4I", raw, 0)
        if magic != MAGIC:
            raise StreamError(f"{path}: not a trace stream")
        if version != VERSION or record_size != RECORD_SIZE:
            raise StreamError(f"{path}: version {version} with {record_size}-byte records is not "
                              f"{VERSION}/{RECORD_SIZE}")
        body = raw[HEADER_SIZE:]
        if len(body) % RECORD_SIZE:
            raise StreamError(f"{path}: {len(body) % RECORD_SIZE} bytes past the last whole record")
        self.ticks = {}
        self.sections = {}
        self.duplicates = 0
        for offset in range(0, len(body), RECORD_SIZE):
            kind, a, b, c, d, e = struct.unpack_from("<6I", body, offset)
            if kind == KIND_TICK:
                if a in self.ticks:
                    self.duplicates += 1
                    continue
                self.ticks[a] = (b, c, d, e)
            elif kind == KIND_SECTION:
                if b >= len(SECTIONS):
                    raise StreamError(f"{path}: section {b} at tick {a} is unknown")
                self.sections.setdefault(a, {})[b] = (c | (d << 32), e)

    @property
    def name(self):
        return Path(self.path).name


def generator_step(state):
    state = (state * LCG_MULTIPLIER - (state // LCG_QUOTIENT) * LCG_MODULUS) & 0xFFFFFFFF
    return (state + LCG_MODULUS) & 0xFFFFFFFF if state == 0 or state >= 0x80000000 else state


def draws_from_seed(seed, targets):
    """Draw count at which each target word first appears, None past the limit."""
    found = dict.fromkeys(targets)
    state = (seed ^ SEED_MASK) | 1
    for draw in range(DRAW_SEARCH_LIMIT):
        if state in found and found[state] is None:
            found[state] = draw
            if all(value is not None for value in found.values()):
                break
        state = generator_step(state)
    return found


def describe_draws(seed, x, y):
    found = draws_from_seed(seed, (x, y))
    name = lambda word: f"draw {found[word]}" if found[word] is not None else "no draw found"
    return f" ({name(x)} / {name(y)} from seed {seed})"


def describe_section(index, value):
    if value is None:
        return "missing"
    return f"{value[0]:#018x}" + ("" if SECTIONS[index] == "total" else f" ({value[1]} items)")


def compare_tick(left, right, tick):
    """Every part of one tick that differs, the tick record words first."""
    parts = []
    for name, x, y in zip(TICK_WORDS, left.ticks[tick], right.ticks[tick]):
        if x != y:
            draws = describe_draws(left.seed, x, y) if name == "generator" and left.seed == right.seed else ""
            parts.append(f"{name} {x:#010x} / {y:#010x}{draws}")
    lsec = left.sections.get(tick)
    rsec = right.sections.get(tick)
    if lsec is not None and rsec is not None:
        for index in SECTION_ORDER:
            x = lsec.get(index)
            y = rsec.get(index)
            if x != y:
                parts.append(f"section {SECTIONS[index]} {describe_section(index, x)} / "
                             f"{describe_section(index, y)}")
    return parts


def read_units(path, tick):
    """Slot -> [(name, value)] of one tick of a unit dump."""
    slots = {}
    prefix = f"tick={tick} "
    try:
        handle = open(path)
    except OSError as error:
        raise StreamError(f"{path}: {error.strerror}")
    with handle:
        for number, line in enumerate(handle, 1):
            if not line.startswith(prefix):
                continue
            fields = []
            for token in line.split():
                name, sep, value = token.partition("=")
                if not sep:
                    raise StreamError(f"{path}:{number}: '{token}' is not name=value")
                fields.append((name, value))
            if len(fields) < 2 or fields[1][0] != "slot" or not fields[1][1].isdigit():
                raise StreamError(f"{path}:{number}: no slot after the tick")
            slots[int(fields[1][1])] = fields[2:]
    return slots


def unit_differences(left_path, right_path, tick):
    left = read_units(left_path, tick)
    right = read_units(right_path, tick)
    lines = []
    for slot in sorted(set(left) | set(right)):
        if slot not in left or slot not in right:
            where = "right" if slot not in left else "left"
            lines.append(f"slot {slot}: occupied only in the {where} run")
            continue
        fields = [f"{name} {x} / {y}" for (name, x), (_, y) in zip(left[slot], right[slot]) if x != y]
        if fields:
            lines.append(f"slot {slot}: " + ", ".join(fields))
    return lines


def describe_range(ticks):
    return f"{ticks[0]}..{ticks[-1]}" if ticks else "none"


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("left", help="trace stream")
    parser.add_argument("right", help="trace stream to compare with it")
    parser.add_argument("--units", nargs=2, metavar=("LEFT", "RIGHT"),
                        help="unit dumps of the two runs")
    args = parser.parse_args(argv)

    try:
        left = Stream(args.left)
        right = Stream(args.right)
    except StreamError as error:
        print(error, file=sys.stderr)
        return 2

    print(f"left:  {left.name}: seed {left.seed}, {len(left.ticks)} ticks "
          f"({describe_range(sorted(left.ticks))}), sections on {len(left.sections)}")
    print(f"right: {right.name}: seed {right.seed}, {len(right.ticks)} ticks "
          f"({describe_range(sorted(right.ticks))}), sections on {len(right.sections)}")
    if left.seed != right.seed:
        print("note: the streams were started from different seeds")
    for stream in (left, right):
        if stream.duplicates:
            print(f"note: {stream.name} repeats {stream.duplicates} tick numbers; the first of each is used")

    common = sorted(set(left.ticks) & set(right.ticks))
    if not common:
        print("the streams share no tick")
        return 1
    for tick in common:
        parts = compare_tick(left, right, tick)
        if not parts:
            continue
        print(f"first difference at tick {tick}: {parts[0]}")
        for part in parts[1:]:
            print(f"  also {part}")
        if args.units:
            try:
                lines = unit_differences(args.units[0], args.units[1], tick)
            except StreamError as error:
                print(error, file=sys.stderr)
                return 2
            for line in lines or ["no unit field differs in the dumps"]:
                print(f"  {line}")
        return 1

    compared = sum(1 for tick in common if tick in left.sections and tick in right.sections)
    print(f"the streams agree on {len(common)} shared ticks ({describe_range(common)}), "
          f"section digests on {compared}")
    only_left = len(left.ticks) - len(common)
    only_right = len(right.ticks) - len(common)
    if only_left or only_right:
        print(f"note: {only_left} ticks only in the left stream, {only_right} only in the right")
    return 0


if __name__ == "__main__":
    sys.exit(main())
