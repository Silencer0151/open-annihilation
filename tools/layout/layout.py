#!/usr/bin/env python3
# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

"""Lay the engine out by layer and name every module one way, as one scripted pass.

tools/layout/layout.txt maps today's tree onto the target layout: each
module's new directory, the files that move on their own, every header's
new include path, every library target's new name, every namespace's new
name and the identifiers renamed with them. This script checks that table
against a checkout and performs it:

  --check    validate the table against the tree and compute the whole pass
             in memory: every module, file, header, target and namespace
             the table names exists; every header, namespace and library
             has a row (so it fails whenever --propose prints one), every
             build directory under src/ a module row, and nothing is left
             outside the layout's groups; every include row moves its
             header; no header that moves under include/ keeps a quoted
             include of a file outside include/; no two files land on one
             path; every patch applies;
             and no script, build file or data file still names a file or
             directory the pass removes, whole or joined from quoted
             pieces. Prints what --apply would do. On a tree already laid
             out it says so.
  --apply    do it: move the files (git mv); rewrite #include lines, target
             names (with an ALIAS oa::<group>::<module> for each module
             library), namespaces, the renamed identifiers, the include
             paths and engine paths that comments and prose name, and the
             fully qualified names that Python names; remove the blocks
             that let a module build on its own; turn include directories
             that reach into other modules into links (and drop the if
             blocks those links make moot); drop the sources that leave a
             module from its CMakeLists.txt, with the targets they leave
             empty (a loop that lists such a target keeps it when another
             module builds it); name each module's project() by the rule;
             apply tools/layout/patches.txt and add tools/layout/files/;
             write tools/layout-baseline.json for tools/check_layout.py;
             and stage it all. The projects that build on the engine are
             rewritten in the same pass (their includes, targets,
             namespaces, identifiers and the engine paths they name; their
             own paths, their JSON data and any rename record stay). With
             --rename-record, every moved file the record names and every
             renamed namespace and identifier is added to its renames.tsv,
             and the --relocate-with script is run on the staged tree; a
             project that holds a record refuses --apply without it. The
             pass is computed and every patch matched in memory first, so a
             table or patch that no longer fits the tree stops it before any
             file changes. On a tree already laid out it changes nothing.
  --propose  print the include, file, namespace and target rows the table
             lacks, derived by the naming rule, for a tree that gained files
             since the table was written. A header outside its module's
             include/ (src/app's) takes a file row that moves it there: an
             include row cannot move it.
  --map      print the module map as a Markdown table (group, module,
             directory, target, include path, namespace).
  --self-test
             lay out a small built-in tree in memory (selftest.py) and
             compare the result with the expected text.

Naming rule: a module lives in src/<group>/<module> (kebab-case); its main
header is include/oa/<group>/<module>.hpp and its other headers are
include/oa/<group>/<module>/<file>; its namespace is oa::<group>::<module>
with hyphens as underscores; its library target is oa-<group>-<module> with
ALIAS oa::<group>::<module>. A group-level module (src/core, src/platform,
src/present, src/audio, src/media, src/app) uses oa/<group>/, oa::<group>
and oa-<group>[-part].

A project that builds on the engine is a directory below the root whose
CMakeLists.txt calls project() and takes OA_ENGINE_DIR; --tree adds one
kept elsewhere. Each may be its own Git repository. A directory of such a
project that holds a rename record (a renames.tsv), or that --rename-record
names, is never rewritten: its files are the record's data.
"""
import argparse
import csv
import json
import posixpath
import re
import subprocess
import sys
from collections import defaultdict
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(1, str(HERE.parent))

import check_layout  # noqa: E402
import cmake_edit  # noqa: E402
import cxx  # noqa: E402

TABLE = HERE / "layout.txt"
PATCHES = HERE / "patches.txt"
FILES = HERE / "files"
BASELINE_NAME = "tools/layout-baseline.json"
KINDS = ("module", "file", "include", "root", "owner", "target", "namespace", "split", "identifier", "docpath",
         "allow", "keep")
C_SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".inc", ".inl", ".mm", ".m"}
HEADER_SUFFIXES = {".h", ".hh", ".hpp", ".hxx", ".inc", ".inl"}
TEXT_SUFFIXES = {".py", ".sh", ".md", ".txt", ".json", ".yml", ".yaml", ".cmake", ".toml", ".cfg", ".in"}
# The files whose paths --check reads for names of files and directories the
# pass removes: scripts, build files and data that name paths as strings.
SCANNED_SUFFIXES = {".py", ".json", ".cmake", ".sh", ".yml", ".yaml"}
# A rename record: the table of renamed files and names that a project keeps
# beside the data it maps. Its directory is never rewritten.
RECORD_TABLE = "renames.tsv"
# A path joined from quoted pieces: "src" / "app" / "x.hpp" or "src", "app".
PIECES_RE = re.compile(r"""(["'])[\w.-]+\1(?:\s*[/,]\s*(["'])[\w.-]+\2)+""")
PIECE_RE = re.compile(r"""["']([\w.-]+)["']""")
ROOT_VARIABLES = ("${open_annihilation_SOURCE_DIR}", "${PROJECT_SOURCE_DIR}", "${CMAKE_SOURCE_DIR}")
# Library targets that are not modules and take no alias.
INFRASTRUCTURE_TARGETS = {"oa-options", "oa-extension-sdk", "oa-test-game-data", "oa-extensions-default"}
# Directory names that end a module's own path (src/<group>/<module>/src/...).
MODULE_PARTS = {"src", "include", "tests", "tools"}
ENGINE = "engine"


class LayoutError(Exception):
    """A table, patch or tree problem that stops the pass."""


def git(repo, *args, check=True):
    """Run git in a repository and return its standard output."""
    result = subprocess.run(["git", "-C", str(repo), *args], capture_output=True, text=True)
    if check and result.returncode != 0:
        raise LayoutError(f"git {' '.join(args)} in {repo} failed: {result.stderr.strip()}")
    return result.stdout


# ---- the table ------------------------------------------------------------------


class Row:
    """One table row."""

    def __init__(self, kind, old, new, scope, note, line):
        self.kind, self.old, self.new, self.scope, self.note, self.line = kind, old, new, scope, note, line


def read_table(path=TABLE):
    """Read the layout table into rows."""
    rows = []
    for number, raw in enumerate(path.read_text().splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split(None, 4)
        if len(parts) < 3:
            raise LayoutError(f"{path.name}:{number}: a row needs a kind, an old and a new column")
        while len(parts) < 5:
            parts.append("")
        kind, old, new, scope, note = parts
        if kind not in KINDS:
            raise LayoutError(f"{path.name}:{number}: unknown kind {kind!r}")
        rows.append(Row(kind, old, "" if new == "-" else new, "" if scope == "-" else scope, note, number))
    return rows


def read_patches(path):
    """Read a patch file: hunks of text replaced in named files.

    A hunk starts with `@@ <phase> <path> [<note>]`, where phase is pre (old
    paths, old names) or post (new paths, new names), then `<<<`, the text to
    find (exactly once), `===`, its replacement, and `>>>`. Lines outside
    hunks that start with # are comments. A path is relative to the engine
    root.
    """
    path = Path(path)
    if not path.is_file():
        return []
    hunks = []
    lines = path.read_text().split("\n")
    index = 0
    while index < len(lines):
        line = lines[index]
        if line.startswith("@@ "):
            header = line[3:].split(None, 2)
            if len(header) < 2 or header[0] not in ("pre", "post"):
                raise LayoutError(f"{path.name}:{index + 1}: a hunk header is `@@ pre|post <path> [note]`")
            phase, target = header[0], header[1]
            if index + 1 >= len(lines) or lines[index + 1] != "<<<":
                raise LayoutError(f"{path.name}:{index + 2}: expected <<<")
            index += 2
            old = []
            while index < len(lines) and lines[index] != "===":
                old.append(lines[index])
                index += 1
            index += 1
            new = []
            while index < len(lines) and lines[index] != ">>>":
                new.append(lines[index])
                index += 1
            if index >= len(lines):
                raise LayoutError(f"{path.name}: the hunk for {target} has no >>>")
            hunks.append({"phase": phase, "path": target, "old": "\n".join(old), "new": "\n".join(new),
                          "where": f"{path.name}:{index + 1}"})
        index += 1
    return hunks


# ---- the trees -------------------------------------------------------------------


def tracked_files(root):
    """Return (repository root, files tracked under root, relative to root)."""
    repo = Path(git(root, "rev-parse", "--show-toplevel").strip()).resolve()
    prefix = root.relative_to(repo).as_posix()
    prefix = "" if prefix == "." else prefix + "/"
    listing = git(root, "ls-files", "-z", "--full-name", ".").split("\0")
    files = [path[len(prefix):] for path in listing if path and path.startswith(prefix)]
    return repo, prefix, sorted(path for path in files if (root / path).is_file())


def directories_of(paths):
    """Return every directory that holds one of the paths, at any depth."""
    found = set()
    for path in paths:
        folder = posixpath.dirname(path)
        while folder and folder not in found:
            found.add(folder)
            folder = posixpath.dirname(folder)
    return found


class Area:
    """One tree the pass touches: the engine, or a project that builds on it."""

    def __init__(self, name, root, files=None, repo=None, prefix=None):
        self.name = name
        self.root = Path(root).resolve()
        if files is None:
            repo, prefix, files = tracked_files(self.root)
        self.repo, self.prefix = repo, prefix
        self.files = files
        self.file_set = set(files)
        self.dirs = directories_of(files)
        # Directories holding a rename record, relative to the root: their
        # files are the record's data and are never rewritten.
        self.record_dirs = sorted(posixpath.dirname(path) for path in files
                                  if posixpath.basename(path) == RECORD_TABLE)

    def read(self, path):
        """Read one tracked file."""
        return (self.root / path).read_text(encoding="utf-8", errors="surrogateescape")

    def owns(self, path):
        """Whether a path names one of this tree's own files or directories."""
        return path in self.file_set or path in self.dirs

    def in_record(self, path):
        """Whether a file lies in a rename record's directory."""
        return any(path.startswith(folder + "/") if folder else True for folder in self.record_dirs)


def builds_on_engine(text):
    """Whether a CMakeLists.txt is a project that builds on the engine."""
    return re.search(r"^\s*project\s*\(", text, re.M) is not None and "OA_ENGINE_DIR" in text


def find_areas(args):
    """Locate the engine and the projects that build on it."""
    engine_root = Path(args.root).resolve()
    repo, prefix, files = tracked_files(engine_root)
    nested = sorted(posixpath.dirname(path) for path in files
                    if path.endswith("/CMakeLists.txt") and
                    builds_on_engine((engine_root / path).read_text(errors="replace")))
    nested = [folder for folder in nested if not any(folder.startswith(other + "/") for other in nested)]
    engine_files = [path for path in files if not any(path.startswith(folder + "/") for folder in nested)]
    areas = {ENGINE: Area(ENGINE, engine_root, engine_files, repo, prefix)}
    for folder in nested:
        areas[folder] = Area(folder, engine_root / folder)
    for extra in args.tree or ():
        root = Path(extra).resolve()
        areas[str(root)] = Area(str(root), root)
    if args.rename_record:
        record = Path(args.rename_record).resolve()
        for area in areas.values():
            if area.name != ENGINE and (record == area.root or area.root in record.parents):
                folder = record.relative_to(area.root).as_posix()
                folder = "" if folder == "." else folder
                if folder not in area.record_dirs:
                    area.record_dirs.append(folder)
    return areas


# ---- the plan ----------------------------------------------------------------------


def longest_prefix(path, prefixes):
    """Return the longest of prefixes that is path or a directory holding it."""
    best = None
    for prefix in prefixes:
        if path == prefix or path.startswith(prefix + "/"):
            if best is None or len(prefix) > len(best):
                best = prefix
    return best


def include_string(path):
    """Return the include path of a file under an include/ directory, or None."""
    marker = "/include/"
    index = path.rfind(marker)
    if index < 0:
        return None
    return path[index + len(marker):]


def module_parts(new_dir):
    """Return (group, module or None) of a new module directory under src/."""
    parts = new_dir.split("/")
    if parts[0] != "src" or len(parts) < 2:
        return None, None
    return parts[1], ("-".join(parts[2:]) if len(parts) > 2 else None)


def module_dir_of(path):
    """Return the module directory a path under src/ sits in: src/<group>[/<module>]."""
    parts = path.split("/")
    if parts[0] != "src" or len(parts) < 3:
        return None
    for index in range(2, len(parts)):
        if parts[index] in MODULE_PARTS:
            return "/".join(parts[:index])
    return "/".join(parts[:-1])


def alias_of(target):
    """Return the ALIAS name of a module library target: oa-sim-match-runtime -> oa::sim::match_runtime."""
    parts = target.split("-", 2)
    if len(parts) == 2:
        return f"oa::{parts[1]}"
    return f"oa::{parts[1]}::{parts[2].replace('-', '_')}"


class Plan:
    """Everything the pass does, computed from the table and the trees."""

    def __init__(self, rows, patches, areas):
        self.rows = rows
        self.patches = patches
        self.areas = areas
        self.engine = areas[ENGINE]
        self.problems = []
        self.notes = []
        by_kind = defaultdict(list)
        for row in rows:
            by_kind[row.kind].append(row)
        self.by_kind = by_kind
        self.modules = {row.old: row.new for row in by_kind["module"]}
        self.file_rows = {row.old: row.new for row in by_kind["file"]}
        self.include_rows = {row.old: row.new for row in by_kind["include"]}
        self.roots = [row.old for row in by_kind["root"]]
        self.owners = {row.old: row.new for row in by_kind["owner"]}
        self.targets = {row.old: (row.new or row.old) for row in by_kind["target"]}
        self.identifiers = {row.old: row.new for row in by_kind["identifier"]}
        self.docpaths = {row.old: row.new for row in by_kind["docpath"]}
        self.allows = [(row.old, row.new, row.note) for row in by_kind["allow"]]
        self.keeps = [row.old for row in by_kind["keep"]]
        try:
            self.tool_dir = HERE.relative_to(self.engine.root).as_posix()
        except ValueError:
            self.tool_dir = None
        self._duplicates()

    def _duplicates(self):
        for kind in ("module", "file", "include", "target", "namespace", "identifier", "owner", "docpath"):
            seen = {}
            for row in self.by_kind[kind]:
                if row.old in seen:
                    self.problems.append(f"layout.txt:{row.line}: {kind} {row.old} repeats line {seen[row.old]}")
                seen[row.old] = row.line

    def is_tool_file(self, path):
        """Whether an engine path belongs to this tool (its table and patches are never rewritten)."""
        return self.tool_dir is not None and path.startswith(self.tool_dir + "/")

    def rewrites(self, area, path):
        """Whether the pass rewrites a file's text: not this tool's files, not a keep row's, not a rename record's."""
        if area.name == ENGINE:
            return not self.is_tool_file(path) and longest_prefix(path, self.keeps) is None
        return not area.in_record(path)

    def record_dirs(self):
        """Return the rename records the trees hold, as paths."""
        return [area.root / folder for area in self.areas.values() for folder in area.record_dirs]

    # -- state ----------------------------------------------------------------------

    def state(self):
        """Return 'before', 'applied' or 'partial' for the engine tree."""
        present = self.engine.file_set
        old_dirs = {old for old, new in self.modules.items() if old != new}
        new_dirs = {new for old, new in self.modules.items() if old != new}
        # A directory one row vacates and another fills (src/formats/ota)
        # says nothing about the state.
        reused = old_dirs & (new_dirs | {module_dir_of(new) for new in self.file_rows.values()})
        old_only = {path for path in old_dirs - reused if any(f.startswith(path + "/") for f in present)}
        moved = {old: new for old, new in self.file_rows.items() if old != new}
        old_files = {old for old in moved if old in present and old not in self.file_rows.values()}
        new_files = {new for new in moved.values() if new in present}
        if not old_only and not old_files and new_files:
            return "applied"
        if (old_only or old_files) and not new_files:
            return "before"
        return "partial"

    def check_applied(self):
        """Report table rows whose new paths a laid-out tree lacks."""
        present = self.engine.file_set
        for old, new in self.modules.items():
            if not any(path == new or path.startswith(new + "/") for path in present):
                self.problems.append(f"module {old}: {new} holds no tracked file")
        for old, new in self.file_rows.items():
            if new not in present:
                self.problems.append(f"file {old}: {new} is not tracked")

    # -- files ------------------------------------------------------------------------

    def module_of(self, path):
        """Return the old module directory that holds a path, or None."""
        return longest_prefix(path, self.modules)

    def compute_moves(self):
        """Map every engine file that moves to its new path."""
        self.include_roots = self._include_roots()
        self.index = {}  # old include path -> old file
        for root in self.include_roots:
            for path in self.engine.files:
                if path.startswith(root + "/") and Path(path).suffix in HEADER_SUFFIXES:
                    name = path[len(root) + 1:]
                    if name in self.index and self.index[name] != path:
                        self.problems.append(f"include path {name} names both {self.index[name]} and {path}")
                    self.index[name] = path
        moves = {}
        used = set()  # the include rows that decide a header's new path
        for path in self.engine.files:
            if path in self.file_rows:
                moves[path] = self.file_rows[path]
                continue
            module = self.module_of(path)
            if module is None:
                continue
            new_dir = self.modules[module]
            rest = path[len(module) + 1:]
            root = longest_prefix(path, self.include_roots)
            if root is not None and root.startswith(module) and rest.startswith("include/"):
                name = path[len(root) + 1:]
                new_name = self.include_rows.get(name)
                if new_name is not None:
                    used.add(name)
                elif Path(path).suffix in HEADER_SUFFIXES:
                    new_name = name  # check_coverage reports the missing row
                else:
                    self.problems.append(f"{path} is under include/ but is not a header, so no include row can "
                                         f"name it; give it a file row")
                    new_name = name
                moves[path] = f"{new_dir}/include/{new_name}"
            else:
                moves[path] = f"{new_dir}/{rest}"
        self.moves = {old: new for old, new in moves.items() if old != new}
        self.reverse_moves = {new: old for old, new in self.moves.items()}
        for old in self.file_rows:
            if old not in self.engine.file_set:
                self.problems.append(f"file row {old}: no such tracked file")
        for name in self.include_rows:
            if name not in self.index:
                self.problems.append(f"include row {name}: no header has that include path")
            elif name in used:
                continue
            elif self.index[name] in self.file_rows:
                self.problems.append(f"include row {name}: {self.index[name]} has a file row, which decides its new "
                                     f"path; remove the include row")
            else:
                # Only a header under its module's include/ moves by an
                # include row; one in another include root (src/app) moves
                # by a file row alone.
                self.problems.append(f"include row {name}: {self.index[name]} is not under its module's include/, "
                                     f"so the row cannot move it; give it a file row")
        landing = defaultdict(list)
        for old, new in self.moves.items():
            landing[new].append(old)
        for new, olds in landing.items():
            if len(olds) > 1:
                self.problems.append(f"{', '.join(olds)} would all move to {new}")
            if new in self.engine.file_set and new not in self.moves:
                self.problems.append(f"{olds[0]} would move onto {new}, which stays")
        for module in self.modules:
            if not any(path == module or path.startswith(module + "/") for path in self.engine.files):
                self.problems.append(f"module row {module}: no tracked files there")

    def new_path(self, path):
        """Return an engine file's path after the pass."""
        return self.moves.get(path, path)

    def defined_libraries(self, with_sources=False):
        """Return (target, build file) for each library the root's and src/'s build files define.

        With with_sources, each entry also holds the arguments after the
        target's name (its sources, among other words).
        """
        found = []
        for path in self.engine.files:
            if text_kind(path) != "cmake" or not (path.startswith("src/") or path == "CMakeLists.txt"):
                continue
            for command in cmake_edit.commands(self.engine.read(path)):
                arguments = [cmake_edit.unquote(argument) for argument in command.arguments()]
                if command.name != "add_library" or not arguments or "ALIAS" in arguments or \
                        "IMPORTED" in arguments or "$" in arguments[0]:
                    continue
                found.append((arguments[0], path, arguments[1:]) if with_sources else (arguments[0], path))
        return found

    def check_coverage(self):
        """Report what the table leaves out of the pass; run after compute_namespaces.

        Every row --propose would print is missing: a header with no include
        or file row keeps its path, a namespace with no row keeps its name
        and so does a library with no target row. A build directory under
        src/ that no module row covers stays where it is; and a file that
        ends up outside src/<group>/ of a group the layout has (a new module
        under a group the pass dissolves, or a file directly under src/)
        would leave the tree half laid out.
        """
        for what, row in missing_rows(self):
            self.problems.append(f"{what} has no row; --propose gives `{row}`")
        for path in self.engine.files:
            if path.startswith("src/") and posixpath.basename(path) == "CMakeLists.txt" and \
                    self.module_of(posixpath.dirname(path)) is None:
                self.problems.append(f"{path}: its directory has no module row")
        groups = ", ".join(sorted(check_layout.LAYERS))
        stray = set()
        for path in self.engine.files:
            new = self.new_path(path)
            parts = new.split("/")
            if parts[0] == "src" and (len(parts) < 3 or parts[1] not in check_layout.LAYERS):
                stray.add(module_dir_of(new) or new)
        for path in sorted(stray):
            self.problems.append(f"{path}: would stay outside the layout's groups ({groups}); give it a module or "
                                 f"file row")

    def moved_away(self, path, cmake_dir):
        """Whether a file a build file compiles moves to another module, whose own build compiles it.

        The module is the build file's own, or, for the root's build file,
        the one the file sat in.
        """
        new = self.moves.get(path)
        if new is None:
            return False
        builder = self.module_of(cmake_dir) if cmake_dir else None
        if builder is not None:
            home = self.modules[builder]
        else:
            module = self.module_of(path)
            home = self.modules[module] if module else module_dir_of(path)
        new_home = self.module_of_new(new) or module_dir_of(new)
        return new_home is not None and new_home != home

    def _include_roots(self):
        roots = set(self.roots)
        for path in self.engine.files:
            index = path.find("/include/")
            if path.startswith(("src/", "tests/")) and index >= 0:
                roots.add(path[:index + len("/include")])
        return sorted(roots)

    # -- includes ------------------------------------------------------------------------

    def area_index(self, area):
        """Return the include index (include path -> file) of a project's own headers."""
        index = {}
        for path in area.files:
            position = path.find("/include/")
            if position >= 0:
                index.setdefault(path[position + len("/include/"):], path)
        return index

    def rewrite_includes(self, area, path, text, own_index):
        """Rewrite the #include lines of one file; return (text, edits)."""
        edits = []
        folder = posixpath.dirname(path)
        engine = area.name == ENGINE
        for start, end, delimiter, name in cxx.includes(text):
            target = None
            relative = False
            if delimiter == '"':
                candidate = posixpath.normpath(posixpath.join(folder, name))
                if engine and candidate in self.engine.file_set:
                    target, relative = candidate, True
                elif not engine and candidate in area.file_set:
                    continue  # the project's own file
            if target is None:
                if not engine and name in own_index:
                    continue
                target = self.index.get(name)
            if target is None:
                continue
            new_target = self.new_path(target)
            if relative:
                new_self = self.new_path(path)
                if new_target == target and new_self == path:
                    continue
                new_relative = posixpath.relpath(new_target, posixpath.dirname(new_self))
                if new_relative == name:
                    continue
                spelled = include_string(new_target)
                if spelled is None:
                    if include_string(new_self) is not None:
                        # A public header would reach out of its include
                        # directory into its module's private files.
                        self.problems.append(f"{path}: #include \"{name}\" would reach {new_target}, outside the "
                                             f"include/ directory of {new_self}; give {target} a file row that "
                                             f"moves it under include/")
                    elif new_relative.startswith("../") and \
                            self.module_of_new(new_target) != self.module_of_new(new_self):
                        self.problems.append(f"{path}: #include \"{name}\" would reach {new_target} from "
                                             f"another module; give it a public include path")
                    spelled = new_relative
                edits.append((start, end, spelled))
            else:
                if target not in self.moves:
                    continue
                spelled = include_string(new_target)
                if spelled is None:
                    self.problems.append(f"{path}: #include <{name}> names {target}, which leaves every include root")
                    continue
                if spelled != name:
                    edits.append((start, end, spelled))
        return cxx.apply_edits(text, edits, path)

    def new_home(self, path):
        """Return where an old file or directory is after the pass."""
        if path in self.moves:
            return self.moves[path]
        if path in self.modules:
            return self.modules[path]
        module = self.module_of(path)
        if module is not None:
            return self.modules[module] + path[len(module):]
        return path

    def module_of_new(self, new_path):
        """Return the new module directory holding a new path."""
        if not hasattr(self, "_new_modules"):
            self._new_modules = set(self.modules.values()) | {module_dir_of(path) for path in self.file_rows.values()
                                                             if module_dir_of(path)}
        return longest_prefix(new_path, self._new_modules)

    # -- namespaces -------------------------------------------------------------------------

    def rewritten_areas(self):
        """Return the trees whose files the pass reads and rewrites (a rename record's files aside)."""
        return list(self.areas.values())

    def compute_namespaces(self):
        """Build the namespace map from every tree's declarations and the table."""
        engine_declared = set()
        other_declared = set()
        homes = defaultdict(lambda: defaultdict(int))
        for area in self.rewritten_areas():
            for path in area.files:
                if Path(path).suffix not in C_SUFFIXES:
                    continue
                declared = cxx.declared_namespaces(area.read(path))
                (engine_declared if area.name == ENGINE else other_declared).update(declared)
                if area.name == ENGINE:
                    home = self.module_of_new(self.new_path(path))
                    for name in declared:
                        if home:
                            homes[name][home] += 1
        # The module directory that declares each namespace most.
        self.namespace_homes = {name: max(counts, key=lambda home: (counts[home], home))
                                for name, counts in homes.items()}
        registry = set()
        for name in engine_declared | other_declared:
            for length in range(1, len(name) + 1):
                registry.add(name[:length])
        # A namespace only a project that builds on the engine declares is
        # that project's own and never moves, even inside an engine namespace
        # that does.
        foreign = other_declared - engine_declared
        rules = {}
        for row in self.by_kind["namespace"]:
            old = tuple(row.old.split("::"))
            new = tuple((row.new or row.old).split("::"))
            if old not in registry:
                self.problems.append(f"layout.txt:{row.line}: namespace {row.old} is declared nowhere")
            rules[old] = new
        splits = {}
        for row in self.by_kind["split"]:
            old = tuple(row.old.split("::"))
            new = tuple(row.new.split("::"))
            scopes = [scope for scope in row.scope.split(",") if scope]
            symbols = set()
            for scope in scopes:
                files = [path for path in self.engine.files if path == scope or path.startswith(scope + "/")]
                if not files:
                    self.problems.append(f"layout.txt:{row.line}: split scope {scope} holds no tracked file")
                # The names the split's headers declare; what its sources add
                # at namespace scope is theirs alone.
                for path in files:
                    if Path(path).suffix in HEADER_SUFFIXES:
                        symbols |= cxx.namespace_members(self.engine.read(path), old)
            if old not in rules:
                self.problems.append(f"layout.txt:{row.line}: split namespace {row.old} has no namespace row")
            splits[old] = (new, symbols, scopes)
            self.notes.append(f"split {row.old} -> {row.new}: {', '.join(sorted(symbols)) or '(no names)'}")
        # An engine namespace under oa with no deciding row is reported by
        # check_coverage (a test's own namespace at global scope needs none).
        self.names = cxx.NamespaceMap(rules, splits, registry, foreign)
        self.new_registry = self.names.new_registry()
        self.engine_namespaces = engine_declared
        # using-directives: those inside a namespace hold wherever it is in
        # view; those at global scope hold in their file and its includers.
        in_namespace = defaultdict(list)
        self.global_usings = {}
        for area in self.rewritten_areas():
            for path in area.files:
                if Path(path).suffix not in C_SUFFIXES:
                    continue
                for enclosing, nominated in cxx.using_directives(area.read(path), registry):
                    if enclosing:
                        if nominated not in in_namespace[enclosing]:
                            in_namespace[enclosing].append(nominated)
                    else:
                        self.global_usings.setdefault((area.name, path), []).append(nominated)
        self.usings = cxx.Usings(dict(in_namespace))

    def inherited_usings(self, area, path, depth=0, seen=None):
        """Return the global-scope using-directives of the local headers a file includes, transitively."""
        seen = seen if seen is not None else set()
        found = []
        if depth > 16 or (area.name, path) in seen:
            return found
        seen.add((area.name, path))
        folder = posixpath.dirname(path)
        for _, _, delimiter, name in cxx.includes(area.read(path)):
            if delimiter != '"':
                continue
            candidate = posixpath.normpath(posixpath.join(folder, name))
            if candidate not in area.file_set:
                continue
            found.extend(self.global_usings.get((area.name, candidate), ()))
            found.extend(self.inherited_usings(area, candidate, depth + 1, seen))
        return found

    # -- text rewrites -------------------------------------------------------------------------

    def path_pattern(self, markdown):
        """Return (regex, mapping) for the paths text names."""
        mapping = dict(self.moves)
        for old, new in self.modules.items():
            if old != new:
                mapping[old] = new
        if markdown:
            for old, new in self.docpaths.items():
                mapping.setdefault(old, new)
        keys = sorted(mapping, key=len, reverse=True)
        # A path ends where the name does: not inside a longer name, and not at
        # a dot that starts an extension (src/missions is not src/missions.cpp).
        # It may follow a compiler's -I.
        pattern = re.compile(r"(?:(?<=-I)|(?<![\w.-]))(" + "|".join(re.escape(key) for key in keys) +
                             r")(?![\w-]|\.\w)")
        return pattern, mapping

    def rewrite_paths(self, area, path, text, markdown):
        """Rewrite the engine paths a text names; return (text, count).

        In the engine, the root's CMakeLists.txt and files outside src/ name
        paths from the root. A CMakeLists.txt below the root names its own
        files relative to itself, so only a path written after a variable
        (…/src/app) counts there; a Markdown file below src/ may name its
        module's files relative to itself, and those stay. In a project that
        builds on the engine, only a path that names a file or directory of
        today's engine tree moves, and not when it also names the project's
        own (from its root or from the file's directory).
        """
        pattern, mapping = self._markdown_paths if markdown else self._paths
        folder = posixpath.dirname(path)
        engine = area.name == ENGINE
        nested_cmake = engine and text_kind(path) == "cmake" and folder != "" and not path.startswith("cmake/")
        count = 0

        def replace(match):
            nonlocal count
            before = text[max(0, match.start() - 1):match.start()]
            token = re.match(r"[\w./-]*", text[match.start():]).group(0).rstrip(".")
            if engine:
                if nested_cmake and before != "/":
                    return match.group(0)
                if folder.startswith("src/") and before != "/":
                    local = posixpath.normpath(posixpath.join(folder, token))
                    if local.startswith(folder + "/") and area.owns(local):
                        return match.group(0)
            elif not self.engine.owns(posixpath.normpath(token)) or area.owns(posixpath.normpath(token)) or \
                    area.owns(posixpath.normpath(posixpath.join(folder, token))):
                # Only a path of today's engine tree that is not the
                # project's own moves (<project>/src/session/x, a path of
                # the project written from above it, stays).
                return match.group(0)
            count += 1
            return mapping[match.group(1)]

        return pattern.sub(replace, text), count

    def include_pattern(self):
        """Return (regex, mapping) for the include paths that prose and comments name (oa/... paths that change)."""
        mapping = {}
        for name, target in self.index.items():
            new_target = self.new_path(target)
            spelled = include_string(new_target)
            if "/" in name and target in self.moves and spelled is not None and spelled != name:
                mapping[name] = spelled
        if not mapping:
            return None, mapping
        keys = sorted(mapping, key=len, reverse=True)
        return re.compile(r"(?<![\w./-])(" + "|".join(re.escape(key) for key in keys) + r")(?![\w-]|\.\w)"), mapping

    def rewrite_include_mentions(self, text, own_index):
        """Rewrite the include paths a text names outside #include lines; return (text, count).

        own_index holds a project's own include paths, which stay.
        """
        pattern, mapping = self._include_mentions
        if pattern is None:
            return text, 0
        count = 0

        def replace(match):
            nonlocal count
            if match.group(1) in own_index:
                return match.group(0)
            count += 1
            return mapping[match.group(1)]

        return pattern.sub(replace, text), count

    def rewrite_in_comments(self, text, rewrite):
        """Apply a text rewrite to the comments of a C or C++ source alone; return (text, count)."""
        pieces = []
        last = 0
        count = 0
        for kind, start, end in cxx.segments(text):
            if kind != "comment":
                continue
            new, found = rewrite(text[start:end])
            if found:
                pieces.append(text[last:start])
                pieces.append(new)
                last = end
                count += found
        if not count:
            return text, 0
        pieces.append(text[last:])
        return "".join(pieces), count

    def target_pattern(self):
        """Return (regex, mapping) for the target names the pass renames."""
        renamed = {old: new for old, new in self.targets.items() if old != new}
        if not renamed:
            return None, renamed
        keys = sorted(renamed, key=len, reverse=True)
        return re.compile(r"(?<![\w-])(" + "|".join(re.escape(key) for key in keys) + r")(?![\w-])"), renamed

    def rewrite_targets(self, text):
        """Rewrite renamed target names; return (text, count)."""
        pattern, renamed = self._targets
        if pattern is None:
            return text, 0
        count = 0

        def replace(match):
            nonlocal count
            count += 1
            return renamed[match.group(1)]

        return pattern.sub(replace, text), count

    def rewrite_targets_in_comments(self, text):
        """Rewrite renamed target names in the comments of a C or C++ source; return (text, count)."""
        return self.rewrite_in_comments(text, self.rewrite_targets)


def text_kind(path):
    """Classify a file for rewriting: 'c', 'cmake', 'markdown', 'json', 'text' or None."""
    name = Path(path).name
    suffix = Path(path).suffix
    if suffix in C_SUFFIXES:
        return "c"
    if name == "CMakeLists.txt" or suffix == ".cmake":
        return "cmake"
    if suffix == ".md":
        return "markdown"
    if suffix == ".json":
        return "json"
    if suffix in TEXT_SUFFIXES or name == "run.sh" or path.startswith(".github/"):
        return "text"
    return None


# ---- the pass in memory -----------------------------------------------------------------


class Work:
    """The new contents of every file the pass touches, keyed by (area, path)."""

    def __init__(self, plan):
        self.plan = plan
        self.contents = {}  # (area, today's path) -> text
        self.created = {}  # (area, new path) -> text
        self.counts = defaultdict(int)

    def get(self, area, path):
        key = (area, path)
        if key not in self.contents:
            self.contents[key] = self.plan.areas[area].read(path)
        return self.contents[key]

    def put(self, area, path, text):
        self.contents[(area, path)] = text

    def changed(self):
        """Yield (area, path, text) for files whose text changed."""
        for (area, path), text in sorted(self.contents.items()):
            if text != self.plan.areas[area].read(path):
                yield area, path, text


def split_area_path(path, areas):
    """Split a patch or new-file path (relative to the engine root) into (area, path within it)."""
    for name, area in areas.items():
        if name != ENGINE and not Path(name).is_absolute() and path.startswith(name + "/"):
            return name, path[len(name) + 1:]
    return ENGINE, path


def apply_hunks(work, phase, locate):
    """Apply the patch hunks of one phase.

    locate(area, path) returns ("old", area, today's path) for a file of
    today's tree, ("new", area, path) for a file the pass creates, or None.
    """
    plan = work.plan
    for hunk in plan.patches:
        if hunk["phase"] != phase:
            continue
        area, path = split_area_path(hunk["path"], plan.areas)
        key = locate(area, path)
        where = f"{hunk['where']} ({phase} {hunk['path']})"
        if key is None:
            plan.problems.append(f"{where}: no such file")
            continue
        space, area, path = key
        text = work.created[(area, path)] if space == "new" else work.get(area, path)
        count = text.count(hunk["old"]) if hunk["old"] else 0
        if hunk["old"] == "":
            # An empty find appends the replacement to the file.
            if hunk["new"] + "\n" in text or text.endswith(hunk["new"]):
                continue
            text = text + ("" if text.endswith("\n") else "\n") + hunk["new"] + "\n"
        elif count == 1:
            if hunk["new"] == "" and text.count(hunk["old"] + "\n") == 1:
                # Removed lines take their line end with them.
                text = text.replace(hunk["old"] + "\n", "")
            else:
                text = text.replace(hunk["old"], hunk["new"])
        elif count == 0 and hunk["new"] and hunk["new"] in text:
            continue
        else:
            plan.problems.append(f"{where}: the text to replace occurs {count} times")
            continue
        if space == "new":
            work.created[(area, path)] = text
        else:
            work.put(area, path, text)
        work.counts["patch hunks"] += 1


def new_build_files(files_dir):
    """Yield (path relative to the engine root, text) for each file of tools/layout/files."""
    if files_dir.is_dir():
        for source in sorted(files_dir.rglob("*")):
            if source.is_file():
                yield source.relative_to(files_dir).as_posix(), source.read_text()


def project_name(new_dir):
    """Return the CMake project() name of a module directory: src/sim/unit-health -> oa_sim_unit_health."""
    group, sub = module_parts(new_dir)
    return "oa_" + group + (f"_{sub.replace('-', '_')}" if sub else "")


def build(plan, files_dir=FILES):
    """Compute every change of the pass in memory; return the Work."""
    work = Work(plan)
    engine = plan.engine
    plan._paths = plan.path_pattern(False)
    plan._markdown_paths = plan.path_pattern(True)
    plan._targets = plan.target_pattern()
    plan._include_mentions = plan.include_pattern()
    own_indexes = {name: plan.area_index(area) for name, area in plan.areas.items() if name != ENGINE}

    def old_key(area, path):
        return ("old", area, path) if path in plan.areas[area].file_set else None

    # 1. Patches written against today's tree.
    apply_hunks(work, "pre", old_key)

    # 2. The engine's build files: standalone-build blocks, include
    # directories that reach into other modules, sources that leave.
    def owner_of(path):
        prefix = longest_prefix(path, plan.owners)
        return plan.owners.get(prefix) if prefix else None

    # The build files that define each target after the pass (by its new
    # name), to tell a target that another module builds from one that goes.
    definers = defaultdict(set)
    for path in engine.files:
        if text_kind(path) == "cmake" and plan.rewrites(engine, path):
            for target in cmake_edit.defined_targets(work.get(ENGINE, path)):
                definers[plan.targets.get(target, target)].add(path)
    for relative, text in new_build_files(files_dir):
        if text_kind(relative) == "cmake":
            for target in cmake_edit.defined_targets(text):
                definers[target].add(f"files/{relative}")

    for path in engine.files:
        if text_kind(path) != "cmake" or not plan.rewrites(engine, path):
            continue
        folder = posixpath.dirname(path)
        text = work.get(ENGINE, path)
        text, removed = cmake_edit.remove_standalone(text)
        work.counts["standalone blocks removed"] += removed
        text, replaced, unowned = cmake_edit.replace_reach_ins(text, folder, owner_of, ROOT_VARIABLES)
        for target, directory, owner in replaced:
            work.counts["reached include directories linked"] += 1
            plan.notes.append(f"{path}: {target} links {owner} instead of reaching into {directory}")
        if replaced:
            # A guard or an else branch that made the same link is moot now.
            text, collapsed = cmake_edit.collapse_same_branches(text)
            text, dropped = cmake_edit.drop_redundant_guards(text)
            work.counts["if blocks the new links made moot"] += collapsed + dropped
        for directory in unowned:
            if path.startswith("src/"):
                plan.problems.append(f"{path}: an include directory reaches into {directory}, which has no owner row")

        def moving(target, path=path):
            return any(definer != path for definer in definers.get(plan.targets.get(target, target), ()))

        text, sources, targets = cmake_edit.drop_moved_sources(
            text, folder, lambda source: plan.moved_away(source, folder), ROOT_VARIABLES, moving)
        module = plan.module_of(folder)
        if module is not None and module == folder and module.startswith("src/"):
            text, renamed = cmake_edit.rename_project(text, project_name(plan.modules[module]))
            work.counts["module projects renamed"] += renamed
        work.counts["sources handed to their new modules"] += len(sources)
        for target in targets:
            work.counts["targets handed to their new modules"] += 1
            plan.notes.append(f"{path}: {target} is built by its new module now")
        for subdirectory in cmake_edit.relative_subdirectories(text):
            plan.problems.append(f"{path}: add_subdirectory({subdirectory}) leaves its own directory")
        if text != engine.read(path):
            text = text.rstrip("\n") + "\n"
        work.put(ENGINE, path, text)

    # 3. Text: includes, namespaces, identifiers, targets and paths, in
    # every tree; a project's JSON data and its rename record stay.
    identifiers = plan.identifiers
    for area in plan.rewritten_areas():
        name = area.name
        own_index = own_indexes.get(name, {})

        def mentions(chunk, own_index=own_index):
            return plan.rewrite_include_mentions(chunk, own_index)

        for path in area.files:
            if not plan.rewrites(area, path):
                continue
            kind = text_kind(path)
            if kind is None:
                continue
            if name != ENGINE and kind == "json":
                continue  # a project's recorded data never changes
            text = work.get(name, path)
            if kind == "c":
                text, count = plan.rewrite_includes(area, path, text, own_index)
                work.counts["include lines"] += count
                try:
                    text, count = cxx.rewrite_chains(text, path if name == ENGINE else None, plan.names,
                                                     plan.new_registry, identifiers, usings=plan.usings,
                                                     extra_usings=plan.inherited_usings(area, path))
                except ValueError as error:
                    plan.problems.append(str(error))
                    count = 0
                work.counts["names and identifiers in C and C++"] += count
                text, count = plan.rewrite_targets_in_comments(text)
                work.counts["target names"] += count
                text, count = plan.rewrite_in_comments(text, mentions)
                work.counts["include paths in comments and prose"] += count
            elif kind == "markdown":
                text, count = cxx.rewrite_chains(text, None, plan.names, plan.new_registry, identifiers,
                                                 markdown=True)
                work.counts["names in Markdown"] += count
                text, count = plan.rewrite_targets(text)
                work.counts["target names"] += count
                text, count = mentions(text)
                work.counts["include paths in comments and prose"] += count
            else:
                if kind == "json" and identifiers:
                    text, count = rewrite_words(text, identifiers)
                    work.counts["identifiers in JSON"] += count
                if path.endswith(".py"):
                    # Fully qualified names only, as in Markdown.
                    text, count = cxx.rewrite_chains(text, None, plan.names, plan.new_registry, identifiers,
                                                     markdown=True)
                    work.counts["names in Python"] += count
                text, count = plan.rewrite_targets(text)
                work.counts["target names"] += count
                if kind != "json":
                    text, count = mentions(text)
                    work.counts["include paths in comments and prose"] += count
            text, count = plan.rewrite_paths(area, path, text, kind == "markdown")
            work.counts["paths"] += count
            work.put(name, path, text)

    # Files renamed inside their own module are named relative to it in its
    # CMakeLists.txt.
    for old, new in plan.moves.items():
        module = plan.module_of(old)
        if module is None or old in plan.index.values():
            continue
        new_module = plan.modules[module]
        if not new.startswith(new_module + "/"):
            continue
        old_relative, new_relative = old[len(module) + 1:], new[len(new_module) + 1:]
        cmake = f"{module}/CMakeLists.txt"
        if old_relative == new_relative or cmake not in engine.file_set:
            continue
        text = work.get(ENGINE, cmake)
        text, count = re.subn(r"(?<![\w./-])" + re.escape(old_relative) + r"(?![\w.-])", new_relative, text)
        work.counts["module-relative source names"] += count
        work.put(ENGINE, cmake, text)

    # 4. Where each file lands.
    def new_key(area, path):
        if (area, path) in work.created:
            return ("new", area, path)
        if area != ENGINE:
            return ("old", area, path) if path in plan.areas[area].file_set else None
        old = plan.reverse_moves.get(path)
        if old is not None:
            return ("old", ENGINE, old)
        if path in engine.file_set and path not in plan.moves:
            return ("old", ENGINE, path)
        return None

    # 5. New files, then patches written against the laid-out tree.
    for relative, text in new_build_files(files_dir):
        area, path = split_area_path(relative, plan.areas)
        if new_key(area, path) is not None:
            plan.problems.append(f"files/{relative}: {path} exists already")
            continue
        work.created[(area, path)] = text
        work.counts["new files"] += 1
    apply_hunks(work, "post", new_key)

    # 6. An alias for every module library. Contents are keyed by today's
    # paths and new files by their new paths; one path can be both
    # (src/formats/ota), so the two are walked apart.
    aliases = {new: alias_of(new) for new in plan.targets.values() if new not in INFRASTRUCTURE_TARGETS}
    for path in engine.files:
        if text_kind(path) == "cmake" and plan.rewrites(engine, path):
            new_text, added = cmake_edit.insert_aliases(work.get(ENGINE, path), aliases)
            work.put(ENGINE, path, new_text)
            work.counts["aliases"] += len(added)
    for key in list(work.created):
        area, path = key
        if area == ENGINE and text_kind(path) == "cmake":
            work.created[key], added = cmake_edit.insert_aliases(work.created[key], aliases)
            work.counts["aliases"] += len(added)

    # 7. The layout check's baseline.
    baseline = {"version": 1,
                "layer_exceptions": [{"from": source, "to": target, "reason": note}
                                     for source, target, note in plan.allows]}
    work.created[(ENGINE, BASELINE_NAME)] = json.dumps(baseline, indent=2) + "\n"
    return work


def scan_vanished(plan, work):
    """Report scripts, build files and data that still name a file or directory the pass removes.

    Reads the engine's files as the pass leaves them (SCANNED_SUFFIXES and
    CMakeLists.txt, this tool's files and keep rows aside) for a path that
    names what is gone, written whole (src/game/x) or joined from quoted
    pieces ("src" / "game"). A CMakeLists.txt below the root names its own
    files relative to itself, so only a path after a / counts there.
    """
    engine = plan.engine
    after = {plan.new_path(path) for path in engine.files}
    after |= {path for area, path in work.created if area == ENGINE}
    after |= directories_of(after)
    vanished = (set(engine.files) | engine.dirs) - after
    if not vanished:
        return
    keys = sorted(vanished, key=len, reverse=True)
    whole = re.compile(r"(?:(?<=-I)|(?<![\w.-]))(" + "|".join(re.escape(key) for key in keys) + r")(?![\w-]|\.\w)")
    texts = [(path, plan.new_path(path), work.contents.get((ENGINE, path))) for path in engine.files]
    texts += [(None, path, text) for (area, path), text in work.created.items() if area == ENGINE]
    for old, path, text in texts:
        name = posixpath.basename(path)
        if Path(path).suffix not in SCANNED_SUFFIXES and name != "CMakeLists.txt":
            continue
        if old is not None and not plan.rewrites(engine, old):
            continue
        if text is None:
            text = engine.read(old)
        nested_cmake = text_kind(path) == "cmake" and posixpath.dirname(path) not in ("", "cmake")
        found = set()
        for match in whole.finditer(text):
            if nested_cmake and text[max(0, match.start() - 1):match.start()] != "/":
                continue
            found.add(match.group(1))
        for match in PIECES_RE.finditer(text):
            pieces = PIECE_RE.findall(match.group(0))
            gone = [length for length in range(2, len(pieces) + 1) if "/".join(pieces[:length]) in vanished]
            if gone:
                found.add(" / ".join(f'"{piece}"' for piece in pieces[:max(gone)]))
        for item in sorted(found):
            plan.problems.append(f"{path}: names {item}, which the pass removes; give the file a post hunk "
                                 f"(--check --dump shows it)")


def rewrite_words(text, words):
    """Replace whole identifiers; return (text, count)."""
    pattern = re.compile(r"\b(" + "|".join(re.escape(word) for word in sorted(words, key=len, reverse=True)) + r")\b")
    count = 0

    def replace(match):
        nonlocal count
        count += 1
        return words[match.group(1)]

    return pattern.sub(replace, text), count


# ---- writing ---------------------------------------------------------------------


def stage(area, paths):
    """Stage paths of one tree, deletions included."""
    listing = "\0".join(sorted(paths)) + "\0"
    result = subprocess.run(["git", "-C", str(area.root), "add", "-A", "--pathspec-from-file=-", "--pathspec-file-nul"],
                            input=listing, text=True, capture_output=True)
    if result.returncode != 0:
        raise LayoutError(f"staging the pass in {area.root} failed: {result.stderr.strip()}")


def write_pass(plan, work):
    """Write the computed pass to the trees and stage it."""
    engine = plan.engine
    touched = defaultdict(set)
    # Contents first, at today's paths; then the moves.
    for area, path, text in work.changed():
        (plan.areas[area].root / path).write_text(text, encoding="utf-8", errors="surrogateescape")
        touched[area].add(plan.new_path(path) if area == ENGINE else path)
    for old, new in sorted(plan.moves.items()):
        (engine.root / new).parent.mkdir(parents=True, exist_ok=True)
        git(engine.root, "mv", "--", old, new)
        touched[ENGINE].add(new)
    for (area, path), text in sorted(work.created.items()):
        target = plan.areas[area].root / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(text)
        touched[area].add(path)
    # Directories the moves emptied.
    for old in sorted({posixpath.dirname(path) for path in plan.moves}, key=len, reverse=True):
        folder = engine.root / old
        while folder != engine.root and folder.is_dir() and not any(folder.iterdir()):
            folder.rmdir()
            folder = folder.parent
    for area, paths in touched.items():
        stage(plan.areas[area], paths)


def dump(plan, work, folder):
    """Write every changed or new file, at its new path, below a directory (engine files at its top)."""
    for area, path, text in work.changed():
        target = folder / (plan.new_path(path) if area == ENGINE else f"{area}/{path}")
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(text, encoding="utf-8", errors="surrogateescape")
    for (area, path), text in work.created.items():
        target = folder / (path if area == ENGINE else f"{area}/{path}")
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(text)


# ---- the rename record -----------------------------------------------------------


def record_rows(plan, record):
    """Return the renames.tsv rows of this pass that the record lacks.

    File rows cover the moved files that the record's other tables name
    (their file and current_file columns); namespace rows rename one
    component of a qualified name (match_runtime -> sim::match_runtime,
    scoped to the parent namespace, or to the module directory when nested
    namespaces with rows of their own stay); a split's rows come first,
    scoped to the files that now hold its names, so they win; identifier
    rows carry the table's scope.
    """
    named = set()
    for table in sorted(record.glob("*.tsv")):
        if table.name == "renames.tsv":
            continue
        with open(table, newline="") as handle:
            for row in csv.DictReader(handle, delimiter="\t"):
                for column in ("file", "current_file"):
                    if row.get(column):
                        named.add(row[column])
    existing = set()
    renames = record / "renames.tsv"
    if renames.is_file():
        for line in renames.read_text().splitlines()[1:]:
            existing.add(tuple(line.split("\t")[:3]))
    rows = []
    for old, new in sorted(plan.moves.items()):
        if old in named:
            rows.append((old, new, "file", posixpath.dirname(old)))
    for row in plan.by_kind["split"]:
        old = row.old.split("::")
        new = row.new.split("::")
        if new[:len(old) - 1] != old[:-1]:
            continue
        scopes = sorted({plan.new_home(scope) for scope in row.scope.split(",") if scope})
        rows.append((old[-1], "::".join(new[len(old) - 1:]), "namespace", ",".join(scopes)))
    kept = {row.old for row in plan.by_kind["namespace"] if (row.new or row.old) == row.old}
    for row in plan.by_kind["namespace"]:
        old = row.old.split("::")
        new = (row.new or row.old).split("::")
        if old == new or new[:len(old) - 1] != old[:-1]:
            continue
        scope = "::".join(old[:-1])
        if any(child.startswith(row.old + "::") for child in kept):
            scope = plan.namespace_homes.get(tuple(old), scope)
        rows.append((old[-1], "::".join(new[len(old) - 1:]), "namespace", scope))
    for row in plan.by_kind["identifier"]:
        kind = "type" if row.old[:1].isupper() else "field" if row.old.endswith("_") else "function"
        rows.append((row.old, row.new, kind, row.scope))
    return [row for row in rows if tuple(row[:3]) not in existing]


def update_record(plan, record, relocate):
    """Append this pass's rows to the record's renames.tsv, run the relocation at the staged tree, stage both."""
    rows = record_rows(plan, record)
    renames = record / "renames.tsv"
    if rows:
        with open(renames, "a") as handle:
            for row in rows:
                handle.write("\t".join(row) + "\n")
    print(f"layout: {len(rows)} rows added to {renames}")
    record_repo = Path(git(record, "rev-parse", "--show-toplevel").strip()).resolve()
    git(record_repo, "add", "--", str(renames))
    if relocate is None:
        return 0
    tree = git(plan.engine.repo, "write-tree").strip()
    result = subprocess.run([sys.executable, str(relocate), "relocate", "--repo", str(plan.engine.repo), "--head",
                             tree, "--out", str(record)], capture_output=True, text=True)
    sys.stdout.write(result.stdout)
    sys.stderr.write(result.stderr)
    changed = git(record_repo, "status", "--porcelain", "--", str(record)).split("\n")
    for line in changed:
        if line.strip():
            git(record_repo, "add", "--", str(record_repo / line[3:].strip()))
    return result.returncode


# ---- propose and map ------------------------------------------------------------------


def proposed_include(plan, module, name, siblings):
    """Derive a header's new include path by the naming rule."""
    group, sub = module_parts(plan.modules[module])
    if group is None:
        return name
    prefix = f"oa/{group}" if sub is None else f"oa/{group}/{sub.replace('-', '_')}"
    if name.startswith(prefix + "/") or name == prefix + ".hpp":
        return name
    base = posixpath.basename(name)
    stem, suffix = posixpath.splitext(base)
    if sub is None:
        return f"{prefix}/{base}"
    if len(siblings) == 1 or stem == sub.replace("-", "_"):
        return f"{prefix}{suffix}"
    return f"{prefix}/{base}"


def library_home(plan, path, sources):
    """Return the new module directory of a library: its build file's, or, in the root's, that of all its sources."""
    if path.startswith("src/"):
        return plan.new_home(posixpath.dirname(path))
    homes = {plan.module_of_new(plan.new_path(source)) for source in sources if source in plan.engine.file_set}
    return homes.pop() if len(homes) == 1 else None


def missing_rows(plan):
    """Return (what, row) for each header, namespace and library the table has no row for.

    The row is derived by the naming rule. A header under its module's
    include/ takes an include row. A header in another include root (a
    module directory whose headers are included by bare name, such as
    src/app) cannot move by an include row, so it takes a file row that puts
    it under its module's include/.
    """
    found = []
    headers = defaultdict(list)
    for name, path in sorted(plan.index.items()):
        if path in plan.file_rows or name in plan.include_rows:
            continue
        module = plan.module_of(path)
        root = longest_prefix(path, plan.include_roots)
        if module is None or not (root and root.startswith(module)):
            continue
        headers[module].append((name, path))
    for module, entries in sorted(headers.items()):
        # A module's own headers, not counting those that leave it.
        own = [name for name, path in plan.index.items() if plan.module_of(path) == module
               and path not in plan.file_rows and Path(path).suffix in HEADER_SUFFIXES]
        for name, path in entries:
            new = proposed_include(plan, module, name, own)
            if path.startswith(module + "/include/"):
                found.append((f"header {path} (include path {name})",
                              f"include {name} {new} -" + (" unchanged" if new == name else "")))
            else:
                found.append((f"header {path}", f"file {path} {plan.modules[module]}/include/{new} -"))
    for name in sorted(plan.engine_namespaces):
        if name[0] == "oa" and not any(name[:length] in plan.names.rules for length in range(1, len(name) + 1)):
            home = plan.namespace_homes.get(name)
            group, sub = module_parts(home or "")
            if group is None:
                new = "::".join(name)
            else:
                new = f"oa::{group}" + (f"::{sub.replace('-', '_')}" if sub else "")
            found.append((f"namespace {'::'.join(name)}", f"namespace {'::'.join(name)} {new} - declared in {home}"))
    for target, path, sources in plan.defined_libraries(with_sources=True):
        if target in plan.targets:
            continue
        group, sub = module_parts(library_home(plan, path, sources) or "")
        new = "-" if group is None else f"oa-{group}" + (f"-{sub}" if sub else "")
        found.append((f"{path}: library {target}", f"target {target} {new} - defined in {path}"))
    return found


def propose(plan):
    """Return the rows the table lacks, derived by the naming rule."""
    return [row for _, row in missing_rows(plan)]


def module_map(plan):
    """Return the module map as a Markdown table."""
    lines = ["| Group | Module | Directory | Target | Include path | Namespace |",
             "| --- | --- | --- | --- | --- | --- |"]
    directories = set(plan.modules.values()) | {module_dir_of(path) for path in plan.file_rows.values()
                                                if module_dir_of(path)}
    for new_dir in sorted(directories):
        group, sub = module_parts(new_dir)
        if group is None:
            continue
        prefix = f"oa/{group}" if sub is None else f"oa/{group}/{sub.replace('-', '_')}"
        namespace = f"oa::{group}" if sub is None else f"oa::{group}::{sub.replace('-', '_')}"
        target = f"oa-{group}" + (f"-{sub}" if sub else "")
        lines.append(f"| {group} | {sub or '(group)'} | `{new_dir}` | `{target}` | `{prefix}` | `{namespace}` |")
    return "\n".join(lines)


# ---- main ------------------------------------------------------------------------


def prepare(args):
    """Read the table and the trees and compute the plan."""
    areas = find_areas(args)
    patches = read_patches(PATCHES)
    for extra in args.patches or ():
        patches.extend(read_patches(extra))
    return Plan(read_table(), patches, areas)


def summary(plan, work):
    """Print what the pass does."""
    renamed_targets = sum(1 for old, new in plan.targets.items() if old != new)
    renamed_namespaces = sum(1 for row in plan.by_kind["namespace"] if row.new and row.new != row.old)
    print(f"layout: {len(plan.moves)} files move; {renamed_targets} targets and {renamed_namespaces} namespaces "
          f"are renamed")
    for name, count in sorted(work.counts.items()):
        print(f"  {name}: {count}")
    changed = defaultdict(int)
    for area, _, _ in work.changed():
        changed[area] += 1
    for area, count in sorted(changed.items()):
        print(f"  {area}: {count} files rewritten")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--check", action="store_true", help="validate the table and print what --apply would do")
    mode.add_argument("--apply", action="store_true", help="perform the pass and stage it")
    mode.add_argument("--propose", action="store_true", help="print the rows the table lacks")
    mode.add_argument("--map", action="store_true", help="print the module map as Markdown")
    mode.add_argument("--self-test", action="store_true", help="lay out a small built-in tree and compare")
    parser.add_argument("--root", default=str(HERE.parents[1]), help="the engine checkout (default: this one)")
    parser.add_argument("--tree", action="append", help="another project that builds on the engine (repeatable)")
    parser.add_argument("--patches", action="append", help="another patch file to apply (repeatable)")
    parser.add_argument("--rename-record", help="a directory whose renames.tsv records renamed files and names")
    parser.add_argument("--relocate-with", help="a script run as `SCRIPT relocate --repo ENGINE --head TREE --out "
                                                "RECORD_DIR` after the record is updated")
    parser.add_argument("--verbose", action="store_true", help="print every note")
    parser.add_argument("--dump", help="with --check: write every file the pass changes or adds, at its new path, "
                                       "below this directory (for writing post patches)")
    parser.add_argument("--allow-dirty", action="store_true",
                        help="apply over uncommitted changes (they are staged with the pass)")
    args = parser.parse_args(argv)
    if args.self_test:
        import selftest
        return selftest.run(sys.modules[__name__])
    try:
        plan = prepare(args)
        state = plan.state()
        if state == "applied":
            plan.check_applied()
            for problem in plan.problems:
                print(f"layout: {problem}", file=sys.stderr)
            print("layout: the tree is laid out already; nothing to do")
            return 1 if plan.problems else 0
        if state == "partial":
            raise LayoutError("the tree is part way through the layout: some old paths and some new ones exist; "
                              "reset it to one state and run again")
        plan.compute_moves()
        plan.compute_namespaces()
        plan.check_coverage()
        if args.map:
            print(module_map(plan))
            return 0
        if args.propose:
            for line in propose(plan):
                print(line)
            return 0
        records = plan.record_dirs()
        record = Path(args.rename_record).resolve() if args.rename_record else None
        for found in records:
            if found != record:
                message = f"{found} holds a rename record; pass --rename-record {found}"
                if args.apply:
                    raise LayoutError(message + " so that the pass is recorded there")
                print(f"layout: note: {message} to --apply (its files are left alone either way)")
        if args.apply and not args.allow_dirty:
            for area in plan.areas.values():
                if git(area.root, "status", "--porcelain", "--untracked-files=no", "--", ".").strip():
                    raise LayoutError(f"{area.root} has uncommitted changes; commit or reset them first")
        work = build(plan)
        scan_vanished(plan, work)
        if args.verbose:
            for note in plan.notes:
                print(f"note: {note}")
        if args.dump:
            dump(plan, work, Path(args.dump))
        if plan.problems:
            for problem in plan.problems:
                print(f"layout: {problem}", file=sys.stderr)
            print(f"layout: {len(plan.problems)} problems; nothing was changed", file=sys.stderr)
            return 1
        summary(plan, work)
        if args.check:
            return 0
        write_pass(plan, work)
        if args.rename_record:
            status = update_record(plan, Path(args.rename_record).resolve(),
                                   Path(args.relocate_with).resolve() if args.relocate_with else None)
            if status != 0:
                print("layout: the relocation failed; see above (the pass is staged)", file=sys.stderr)
                return status
        print("layout: done; the pass is staged")
        return 0
    except LayoutError as error:
        print(f"layout: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
