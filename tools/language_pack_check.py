#!/usr/bin/env python3
# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only
"""Check a language pack (docs/languages.md) and report its coverage.

    language_pack_check.py PACK [--game-dir DIR --oa-tool EXE]
                                [--catalogue FILE ...] [--strict]

The pack's manifest must read as the game reads it, and each of its tables
must parse, carry its licence header and name each section once. A pack
that fails one of these exits with status 1. An empty value is told, as
the game lets it fall through to the next source.

With --game-dir and --oa-tool, the player's game data is read through the
tool's archive listing: the pack's translate.tdf is compared with the game
data's gamedata/translate.tdf (the English keys it covers and misses), and
its units.tdf with the units' own files (units it misses, units the game
data lacks, and the name-from and description-from fields that no longer
match the English, which the game skips as stale).

With --catalogue, each file is an interface catalogue whose sections name
the engine's English words; the pack's interface.tdf is compared with them.

--strict also fails on stale fields. A PACK folder that is not there is
skipped with status 77, so that a build without the pack passes.
"""

from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import oamod_yaml  # noqa: E402

SKIPPED = 77
"""The status of a run with no pack to check (ctest's SKIP_RETURN_CODE)."""

TABLES = ("translate.tdf", "units.tdf", "missions.tdf", "interface.tdf", "pictures.tdf")
"""The tables a pack may hold."""

LICENCE = "SPDX-License-Identifier: GPL-3.0-only"
"""The licence line each table carries in its header."""

MANIFEST_KEYS = {
    "oalang", "tag", "name", "english-name", "word", "version", "locales",
    "fallbacks", "text", "unicode",
}
"""The keys a manifest may hold."""

ARCHIVE_ORDER = (".hpi", ".ufo", ".ccx", ".gp3")
"""Archive kinds from lowest to highest precedence."""

UNIT_FIELD_BYTES = {"name": 32, "description": 64}
"""The bytes the game data's English is kept in, NUL included."""


class TdfError(ValueError):
    """A table that does not parse."""


def parse_tdf(text: str) -> list:
    """Parse TDF text into nested [name, {key: value}, children] lists."""
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    text = re.sub(r"//[^\n]*", "", text)
    root: list = ["", {}, []]
    stack = [root]
    pending = None
    position = 0
    while position < len(text):
        character = text[position]
        if character.isspace():
            position += 1
        elif character == "[":
            end = text.find("]", position)
            if end < 0:
                raise TdfError("a section name is not closed")
            pending = text[position + 1:end]
            position = end + 1
        elif character == "{":
            if pending is None:
                raise TdfError("a block has no section name")
            section = [pending, {}, []]
            stack[-1][2].append(section)
            stack.append(section)
            pending = None
            position += 1
        elif character == "}":
            if len(stack) == 1:
                raise TdfError("a block closes that was not opened")
            stack.pop()
            position += 1
        else:
            end = text.find(";", position)
            if end < 0:
                raise TdfError("a value does not end in ';'")
            entry = text[position:end]
            if "=" not in entry or len(stack) == 1:
                raise TdfError(f"not a key=value entry: {entry.strip()[:40]!r}")
            key, value = entry.split("=", 1)
            stack[-1][1][key.strip().lower()] = value.strip()
            position = end + 1
    if len(stack) != 1:
        raise TdfError("a block is not closed")
    return root[2]


class Report:
    """What a check found: failures fail the run, notes are told."""

    def __init__(self) -> None:
        self.failures: list[str] = []
        self.stale: list[str] = []

    def fail(self, text: str) -> None:
        self.failures.append(text)
        print(f"FAIL {text}")

    @staticmethod
    def note(text: str) -> None:
        print(text)


def check_manifest(pack: Path, report: Report) -> dict:
    """Read the manifest as the game does and return its values."""
    path = pack / "language.yaml"
    if not path.is_file():
        report.fail("language.yaml is missing")
        return {}
    try:
        values = oamod_yaml.loads(path.read_bytes())
    except Exception as error:  # the reader's own errors name the rule
        report.fail(f"language.yaml does not read: {error}")
        return {}
    if not isinstance(values, dict):
        report.fail("language.yaml is not a mapping")
        return {}
    for key in values:
        if key not in MANIFEST_KEYS:
            report.fail(f"language.yaml has no key {key}")
    if values.get("oalang") != 1:
        report.fail("language.yaml's oalang is not 1")
    for key in ("tag", "word"):
        if not isinstance(values.get(key), str) or not values.get(key):
            report.fail(f"language.yaml names no {key}")
    if pack.name != values.get("tag"):
        report.note(f"the folder {pack.name} is not named by the tag {values.get('tag')}")
    return values


def check_tables(pack: Path, report: Report) -> dict:
    """Parse each table the pack holds and return its sections by file."""
    tables = {}
    for name in TABLES:
        path = pack / name
        if not path.is_file():
            continue
        raw = path.read_bytes()
        try:
            text = raw.decode("utf-8")
        except UnicodeDecodeError:
            report.fail(f"{name} is not UTF-8")
            continue
        if LICENCE not in text.split("[", 1)[0]:
            report.fail(f"{name} has no licence header")
        try:
            sections = parse_tdf(text)
        except TdfError as error:
            report.fail(f"{name} does not parse: {error}")
            continue
        seen = set()
        # The game looks translate.tdf's and interface.tdf's sections up by
        # their exact text, and the other tables' by name in any case.
        exact = name in ("translate.tdf", "interface.tdf")
        for section in sections:
            folded = section[0] if exact else section[0].lower()
            if folded in seen:
                report.fail(f"{name} names [{section[0]}] twice")
            seen.add(folded)
            for key, value in section[1].items():
                if value == "" and name != "pictures.tdf":
                    report.note(f"{name} [{section[0]}] gives {key} no value, which falls through")
        tables[name] = sections
        report.note(f"{name}: {len(sections)} sections")
    return tables


def archives(game_dir: Path) -> list[Path]:
    """The game folder's archives, from lowest to highest precedence."""
    found = [p for p in game_dir.iterdir() if p.suffix.lower() in ARCHIVE_ORDER]
    return sorted(found, key=lambda p: (ARCHIVE_ORDER.index(p.suffix.lower()), p.name.lower()))


def read_game_data(game_dir: Path, tool: Path) -> tuple[str | None, dict]:
    """Read translate.tdf and every unit file, each from the archive that wins."""
    entries: dict[str, tuple[Path, str]] = {}
    for archive in archives(game_dir):
        listing = subprocess.run(
            [str(tool), "list", str(archive)], capture_output=True, text=True, errors="replace"
        )
        for line in listing.stdout.splitlines():
            entry = line.split("\t", 1)[0]
            folded = entry.lower()
            if folded == "gamedata/translate.tdf" or (
                folded.startswith("units/") and folded.endswith(".fbi")
            ):
                entries[folded] = (archive, entry)
    translate = None
    units = {}
    with tempfile.TemporaryDirectory() as scratch:
        output = Path(scratch) / "entry"
        for folded, (archive, entry) in sorted(entries.items()):
            if subprocess.run(
                [str(tool), "extract", str(archive), entry, str(output)], capture_output=True
            ).returncode != 0:
                continue
            text = output.read_bytes().decode("cp1252", errors="replace")
            if folded == "gamedata/translate.tdf":
                translate = text
                continue
            try:
                sections = parse_tdf(text)
            except TdfError:
                continue
            for section in sections:
                fields = section[1]
                if "unitname" in fields:
                    units[fields["unitname"].lower()] = fields
    return translate, units


def cut(english: str, field_bytes: int) -> str:
    """The English as the game data's field of the given size keeps it."""
    return english.encode("cp1252", errors="replace")[: field_bytes - 1].decode("cp1252")


def translates(source: str, english: str, field_bytes: int) -> bool:
    """Whether a -from field still names the English, as the game decides."""
    return source in ("", english) or (
        len(english.encode("cp1252", errors="replace")) + 1 >= field_bytes
        and source.startswith(english)
    )


def check_game_data(tables: dict, game_dir: Path, tool: Path, report: Report) -> None:
    """Compare translate.tdf and units.tdf with the player's game data."""
    translate, units = read_game_data(game_dir, tool)
    if translate is None:
        report.note("the game data has no gamedata/translate.tdf")
    else:
        keys = [section[0] for section in parse_tdf(translate)]
        covered = {section[0] for section in tables.get("translate.tdf", [])}
        missing = [key for key in keys if key not in covered]
        report.note(
            f"translate.tdf covers {len(keys) - len(missing)} of the game data's {len(keys)} texts"
        )
        for key in missing:
            report.note(f"  missing text: {key}")
        for key in sorted(covered - set(keys)):
            report.note(f"  not in the game data: {key}")
    pack_units = {section[0].lower(): section for section in tables.get("units.tdf", [])}
    report.note(f"units.tdf covers {len(set(pack_units) & set(units))} of {len(units)} units")
    for name in sorted(set(units) - set(pack_units)):
        report.note(f"  missing unit: {name.upper()}")
    for name, section in sorted(pack_units.items()):
        fields = units.get(name)
        if fields is None:
            report.note(f"  not in the game data: {section[0]}")
            continue
        for field, field_bytes in UNIT_FIELD_BYTES.items():
            source = section[1].get(f"{field}-from", "")
            english = cut(fields.get(field, ""), field_bytes)
            if field in section[1] and not translates(source, english, field_bytes):
                text = f"stale {field}-from in [{section[0]}]: {source!r} is now {english!r}"
                report.stale.append(text)
                report.note(f"  {text}")


def check_catalogues(tables: dict, catalogues: list[Path], report: Report) -> None:
    """Compare interface.tdf with the engine's catalogue files."""
    words = set()
    for path in catalogues:
        try:
            words |= {s[0] for s in parse_tdf(path.read_text(encoding="utf-8"))}
        except (OSError, UnicodeDecodeError, TdfError) as error:
            report.fail(f"the catalogue {path} does not read: {error}")
    covered = {section[0] for section in tables.get("interface.tdf", [])}
    report.note(f"interface.tdf covers {len(words & covered)} of {len(words)} engine words")
    for word in sorted(words - covered):
        report.note(f"  missing word: {word}")


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n", 1)[0])
    parser.add_argument("pack", type=Path)
    parser.add_argument("--game-dir", type=Path)
    parser.add_argument("--oa-tool", type=Path)
    parser.add_argument("--catalogue", type=Path, action="append", default=[])
    parser.add_argument("--strict", action="store_true")
    options = parser.parse_args(argv)
    if not options.pack.is_dir():
        print(f"SKIP no pack at {options.pack}")
        return SKIPPED
    report = Report()
    check_manifest(options.pack, report)
    tables = check_tables(options.pack, report)
    if options.game_dir is not None and options.oa_tool is not None:
        if options.game_dir.is_dir() and os.access(options.oa_tool, os.X_OK):
            check_game_data(tables, options.game_dir, options.oa_tool, report)
        else:
            report.note("the game data or the tool is not there; coverage not checked")
    if options.catalogue:
        check_catalogues(tables, options.catalogue, report)
    if options.strict and report.stale:
        report.fail(f"{len(report.stale)} stale fields")
    print("language_pack_check: " + ("failed" if report.failures else "clean"))
    return 1 if report.failures else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
