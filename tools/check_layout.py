#!/usr/bin/env python3
# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

"""Check that the engine keeps its layout: modules, links, layers and names.

A module is a directory under src/ whose CMakeLists.txt defines targets
(src/<group>/<module>, or src/<group> itself for a group-level module such
as src/core); its group is the first directory under src/. The check reads
the build files and sources under --root and the targets a configuration
defined (--targets, written by cmake/OaLayout.cmake), and fails on:

  reach        a build file that names a directory outside its own with ..
               (add_subdirectory, an include directory or a source), or a
               target's include directory or source in another module
  foreign      a target linked from another directory's CMakeLists.txt
  include      an #include that leaves its module by a relative path, a
               public header's relative #include of a file outside its
               module's include/ (which the targets that export the header
               do not carry), or an #include that names another module's
               header when no link of the including target (or of the
               header's own target) reaches a target that exports it
  layer        a link from a module library to a library of a later layer
               (the order is core; base; platform and formats, which may
               not use each other; data; sim, which may not use platform;
               present, audio and media; ui; app), unless
               tools/layout-baseline.json lists it
  stale        a baseline entry for a link that no longer exists, which
               must be removed: the baseline may only shrink
  name         a module library not named oa-<group>-<module>[-part]
               (oa-<group>[-part] in a group-level module) or without the
               ALIAS oa::<group>::<module>[_part], or a public header outside
               include/oa/<group>/<module>/ and include/oa/<group>/<module>.*
               (include/oa/<group>/ in a group-level module)

Tests (executables) and tools may link anything, but must still link what
they include. Code outside src/ is not checked.

--self-test checks a small built-in tree. Exit status is 1 on any finding
and 2 when the targets file or the baseline cannot be read.
"""
import argparse
import contextlib
import io
import json
import os
import posixpath
import re
import sys
import tempfile
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BASELINE = ROOT / "tools" / "layout-baseline.json"
LAYERS = {"core": 1, "base": 2, "platform": 3, "formats": 3, "data": 4, "sim": 5, "present": 6, "audio": 6,
          "media": 6, "ui": 7, "app": 8}
# Pairs of groups in one layer that may not use each other.
APART = {("platform", "formats"), ("formats", "platform")}
# Groups a group may not use although they come earlier.
FORBIDDEN = {("sim", "platform")}
# Groups whose directory is itself a module (src/core, not src/core/<module>).
GROUP_LEVEL = {"core", "platform", "present", "audio", "media", "app"}
LIBRARY_TYPES = {"STATIC_LIBRARY", "SHARED_LIBRARY", "MODULE_LIBRARY", "OBJECT_LIBRARY", "INTERFACE_LIBRARY"}
# Targets that are build infrastructure rather than modules.
INFRASTRUCTURE = {"oa-options", "oa-extension-sdk", "oa-test-game-data"}
SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".inc", ".inl", ".mm", ".m"}
INCLUDE_RE = re.compile(r'^[ \t]*#[ \t]*include[ \t]*(["<])([^">\n]+)[">]', re.M)
COMMAND_RE = re.compile(r"(?m)^[ \t]*(add_subdirectory|target_include_directories|add_library|add_executable|"
                        r"target_sources)[ \t]*\(([^)]*)\)")
ALIAS_RE = re.compile(r"add_library\(\s*(\S+)\s+ALIAS\s+(\S+)\s*\)")
# The largest baseline or targets file read; the tree's are a few kilobytes
# and a few megabytes.
MAX_BASELINE_BYTES = 1 << 20
MAX_TARGETS_BYTES = 64 << 20


class InputError(Exception):
    """A targets file or baseline that cannot be read."""


def read_bounded(path, limit, what):
    """Read a UTF-8 text file of at most limit bytes."""
    path = Path(path)
    try:
        size = path.stat().st_size
        if size > limit:
            raise InputError(f"{what} {path} is {size} bytes, more than the {limit} this check reads")
        return path.read_text(encoding="utf-8", errors="replace")
    except OSError as error:
        raise InputError(f"cannot read {what} {path}: {error.strerror}") from error


def strip_genex(value):
    """Return the target or path an entry names, or None for other generator expressions."""
    match = re.fullmatch(r"\$<(?:BUILD_INTERFACE|LINK_ONLY):(.*)>", value)
    if match:
        return match.group(1)
    if value.startswith("$<"):
        return None
    return value


def split_list(value):
    """Split a CMake list, keeping generator expressions whole."""
    items = []
    depth = 0
    current = []
    for index, char in enumerate(value):
        if char == "<" and value[index - 1:index] == "$":
            depth += 1
        elif char == ">" and depth:
            depth -= 1
        if char == ";" and depth == 0:
            items.append("".join(current))
            current = []
            continue
        current.append(char)
    items.append("".join(current))
    return [item for item in items if item]


def longest_prefix(path, prefixes):
    """Return the longest of prefixes that is path or a directory holding it."""
    best = None
    for prefix in prefixes:
        if path == prefix or path.startswith(prefix + "/"):
            if best is None or len(prefix) > len(best):
                best = prefix
    return best


def module_names(module):
    """Return (group, module or None) of a module directory."""
    parts = module.split("/")
    return parts[1], ("-".join(parts[2:]) if len(parts) > 2 else None)


def layer_allowed(source_group, target_group):
    """Whether a module of one group may link a module of another."""
    if source_group == target_group:
        return True
    if (source_group, target_group) in FORBIDDEN or (source_group, target_group) in APART:
        return False
    return LAYERS[target_group] <= LAYERS[source_group]


class Targets:
    """The targets a configuration defined, read from its layout-targets file."""

    def __init__(self, path, root):
        self.root = root
        self.props = defaultdict(dict)
        self.aliases = {}
        for line in read_bounded(path, MAX_TARGETS_BYTES, "the targets file").splitlines():
            parts = line.split("\t", 2)
            if len(parts) != 3:
                continue
            if parts[0] == "ALIAS":
                self.aliases[parts[1]] = parts[2]
                continue
            self.props[parts[0]][parts[1]] = parts[2]
        self.names = set(self.props)

    def get(self, target, name):
        return self.props[target].get(name, "")

    def rel(self, path):
        """Return a path relative to the root, or None outside it."""
        try:
            return Path(path).resolve().relative_to(self.root).as_posix()
        except ValueError:
            return None

    def links(self, target, interface=False, usage=False):
        """Return the targets one target links (its interface links, when asked).

        usage leaves out $<LINK_ONLY:...> entries, which bring no include
        directories.
        """
        value = self.get(target, "INTERFACE_LINK_LIBRARIES" if interface else "LINK_LIBRARIES")
        found = []
        for item in split_list(value):
            if item.startswith("::@"):
                continue
            if usage and item.startswith("$<LINK_ONLY:"):
                continue
            name = strip_genex(item)
            if name is None:
                continue
            name = self.aliases.get(name, name)
            if name in self.names:
                found.append(name)
        return found

    def foreign_links(self, target):
        """Whether a link of a target was made from another directory (CMake marks it ::@(...))."""
        return "::@(" in self.get(target, "LINK_LIBRARIES") + self.get(target, "INTERFACE_LINK_LIBRARIES")

    def closure(self, target):
        """Return every target whose include directories a target sees through its links."""
        seen = set()
        stack = list(self.links(target, usage=True)) + list(self.links(target, interface=True, usage=True))
        while stack:
            name = stack.pop()
            if name in seen:
                continue
            seen.add(name)
            stack.extend(self.links(name, interface=True, usage=True))
        return seen

    def include_dirs(self, target, interface):
        """Return a target's own include directories that lie inside the root."""
        value = self.get(target, "INTERFACE_INCLUDE_DIRECTORIES" if interface else "INCLUDE_DIRECTORIES")
        found = []
        for item in split_list(value):
            path = strip_genex(item)
            if path is not None:
                relative = self.rel(path)
                if relative is not None:
                    found.append(relative)
        return found

    def sources(self, target):
        """Return a target's sources that lie inside the root."""
        found = []
        source_dir = self.get(target, "SOURCE_DIR")
        for item in split_list(self.get(target, "SOURCES")):
            path = strip_genex(item)
            if path is None:
                continue
            relative = self.rel(path if os.path.isabs(path) else os.path.join(source_dir, path))
            if relative is not None and not self.generated(relative):
                found.append(relative)
        return found

    def generated(self, relative):
        """Whether a path lies in a build tree inside the root, such as a file CMake generates there.

        A build tree is a top-level folder that holds a CMakeCache.txt.
        """
        top = relative.split("/", 1)[0]
        return "/" in relative and (self.root / top / "CMakeCache.txt").is_file()


class Checker:
    """Runs every rule over one tree and collects findings."""

    def __init__(self, root, targets_path, baseline):
        self.root = Path(root).resolve()
        self.findings = []
        self.targets = Targets(targets_path, self.root)
        self.baseline = baseline
        self.engine = {name for name in self.targets.names if self._inside(self.targets.get(name, "SOURCE_DIR"))}
        self.module_of_target = {name: self._module_of_target(name) for name in self.engine}
        self.modules = sorted({module for module in self.module_of_target.values() if module})
        self.files = self._files()

    def _inside(self, directory):
        """Whether a target's directory lies in the engine tree (the configuration may hold more)."""
        return self.targets.rel(directory) is not None

    def _module_of_target(self, name):
        source_dir = self.targets.rel(self.targets.get(name, "SOURCE_DIR"))
        if source_dir and source_dir.startswith("src/"):
            return source_dir
        # A target the root defines belongs to the module that holds all its
        # sources (oa-game: src/app), or, with no sources, all its include
        # directories, if one does.
        folders = {posixpath.dirname(path) for path in self.targets.sources(name)}
        if not folders:
            folders = set(self.targets.include_dirs(name, True))
        if not folders or not all(folder.startswith("src/") for folder in folders):
            return None
        parts = posixpath.commonpath(sorted(folders)).split("/")
        if len(parts) >= 2 and parts[1] in GROUP_LEVEL:
            return "/".join(parts[:2])
        return "/".join(parts[:3]) if len(parts) >= 3 else None

    def _files(self):
        files = []
        for folder, subfolders, names in os.walk(self.root / "src"):
            subfolders[:] = [name for name in subfolders if not name.startswith(".")]
            for name in names:
                files.append((Path(folder) / name).relative_to(self.root).as_posix())
        return sorted(files)

    def report(self, rule, where, message):
        """Record a finding once (a header several targets reach is read once for each)."""
        if (rule, where, message) not in self.findings:
            self.findings.append((rule, where, message))

    def module_of(self, path):
        return longest_prefix(path, self.modules)

    # -- rules ----------------------------------------------------------------

    def check_build_files(self):
        """Report build files under src/ that name directories outside their own with .."""
        for path in self.files:
            if Path(path).name != "CMakeLists.txt" and not path.endswith(".cmake"):
                continue
            text = re.sub(r"#[^\n]*", "", (self.root / path).read_text(encoding="utf-8", errors="replace"))
            for match in COMMAND_RE.finditer(text):
                for argument in match.group(2).split():
                    value = re.sub(r"^\$\{CMAKE_CURRENT_(?:SOURCE|LIST)_DIR\}/?", "", argument.strip('"'))
                    if ".." in value.split("/"):
                        line = text[:match.start()].count("\n") + 1
                        self.report("reach", f"{path}:{line}", f"{match.group(1)} names {argument}")

    def check_targets(self):
        """Report foreign links and include directories or sources of other modules."""
        targets = self.targets
        for name in sorted(self.engine):
            module = self.module_of_target[name]
            if module is None:
                continue
            if targets.foreign_links(name):
                self.report("foreign", name, "is linked from another directory's CMakeLists.txt")
            for directory in targets.include_dirs(name, False) + targets.include_dirs(name, True):
                if directory.startswith("src/") and self.module_of(directory) != module:
                    self.report("reach", name, f"has the include directory {directory} of another module")
            for source in targets.sources(name):
                if source.startswith("src/") and self.module_of(source) != module:
                    self.report("reach", name, f"compiles {source} of another module")

    def include_index(self):
        """Map include paths to files, and each exported header to the targets that export it."""
        index = {}
        owners = defaultdict(set)
        exporters = defaultdict(set)
        for name in self.engine:
            for directory in set(self.targets.include_dirs(name, True)):
                exporters[directory].add(name)
        for directory in sorted(exporters):
            base = self.root / directory
            if not base.is_dir():
                continue
            for folder, _, names in os.walk(base):
                for file_name in names:
                    full = Path(folder) / file_name
                    key = full.relative_to(base).as_posix()
                    path = full.relative_to(self.root).as_posix()
                    index.setdefault(key, path)
                    owners[path].update(exporters[directory])
        return index, owners

    def resolve(self, source, delimiter, name, index):
        """Resolve an #include: next to the including file first, then through the include index."""
        if delimiter == '"':
            candidate = posixpath.normpath(posixpath.join(posixpath.dirname(source), name))
            if (self.root / candidate).is_file():
                return candidate, True
        found = index.get(name)
        return (found, False) if found else (None, False)

    def check_includes(self):
        """Report includes that leave a module by a relative path or that no link reaches."""
        index, owners = self.include_index()
        closures = {}

        def reach(target):
            if target not in closures:
                closures[target] = self.targets.closure(target) | {target}
            return closures[target]

        def check_file(path, module, allowed, where, seen):
            if path in seen or not (self.root / path).is_file():
                return
            seen.add(path)
            text = (self.root / path).read_text(encoding="utf-8", errors="replace")
            for match in INCLUDE_RE.finditer(text):
                delimiter, name = match.group(1), match.group(2)
                resolved, relative = self.resolve(path, delimiter, name, index)
                if resolved is None:
                    continue
                other = self.module_of(resolved)
                line = text[:match.start()].count("\n") + 1
                if relative and ".." in name.split("/") and other != module:
                    self.report("include", f"{path}:{line}", f"reaches {resolved} of {other} by a relative path")
                    continue
                public = f"{self.module_of(path)}/include/"
                if relative and path.startswith(public) and not resolved.startswith(public):
                    self.report("include", f"{path}:{line}", f"is a public header that reaches {resolved}, outside "
                                                             f"{public}, by a relative path")
                    continue
                if other == module or other is None:
                    if relative and resolved.startswith("src/") and resolved not in owners:
                        check_file(resolved, module, allowed, where, seen)
                    continue
                if not owners.get(resolved, set()) & allowed:
                    self.report("include", f"{path}:{line}",
                                f"includes {name} of {other}, which no link of {where} reaches")

        for name in sorted(self.engine):
            module = self.module_of_target[name]
            if module is None:
                continue
            allowed = reach(name)
            seen = set()
            for source in self.targets.sources(name):
                if Path(source).suffix in SOURCE_SUFFIXES:
                    check_file(source, module, allowed, name, seen)
        # A public header answers for its own includes through the links of its
        # module's targets and of the targets that export it.
        by_module = defaultdict(set)
        for name, module in self.module_of_target.items():
            if module:
                by_module[module].add(name)
        for header, names in sorted(owners.items()):
            module = self.module_of(header)
            if module is None or Path(header).suffix not in SOURCE_SUFFIXES:
                continue
            allowed = set()
            for owner in names | by_module[module]:
                allowed |= reach(owner)
            check_file(header, module, allowed, f"the targets of {module}", set())

    def check_layers(self):
        """Report links from module libraries against the layer order."""
        known = {(entry["from"], entry["to"]) for entry in self.baseline.get("layer_exceptions", [])}
        used = set()
        for name in sorted(self.engine):
            module = self.module_of_target[name]
            if module is None or self.targets.get(name, "TYPE") not in LIBRARY_TYPES:
                continue
            group, _ = module_names(module)
            if group not in LAYERS:
                self.report("layer", name, f"is in {module}, whose group {group} has no layer")
                continue
            for dep in sorted(set(self.targets.links(name)) | set(self.targets.links(name, interface=True))):
                dep_module = self.module_of_target.get(dep)
                if dep_module is None or dep in INFRASTRUCTURE:
                    continue
                dep_group, _ = module_names(dep_module)
                if dep_group not in LAYERS or layer_allowed(group, dep_group):
                    continue
                if (name, dep) in known:
                    used.add((name, dep))
                    continue
                self.report("layer", name, f"({group}) links {dep} ({dep_group}), against the layer order")
        for source, target in sorted(known - used):
            self.report("stale", f"{source} -> {target}", "no longer links against the layer order; remove the "
                                                          "baseline entry")

    def check_names(self):
        """Report module libraries and public headers that break the naming rule."""
        declared = defaultdict(set)
        for path in self.files:
            if Path(path).name == "CMakeLists.txt":
                for match in ALIAS_RE.finditer((self.root / path).read_text(encoding="utf-8", errors="replace")):
                    declared[match.group(2)].add(match.group(1))
        for name in sorted(self.engine):
            module = self.module_of_target[name]
            if module is None or name in INFRASTRUCTURE or self.targets.get(name, "TYPE") not in LIBRARY_TYPES:
                continue
            source_dir = self.targets.rel(self.targets.get(name, "SOURCE_DIR"))
            if not source_dir or not source_dir.startswith("src/"):
                continue  # the app's targets the root defines
            group, sub = module_names(module)
            stem = f"oa-{group}" + (f"-{sub}" if sub else "")
            if name != stem and not name.startswith(stem + "-"):
                self.report("name", name, f"is in {module}; its name should be {stem} or {stem}-<part>")
                continue
            rest = name[len("oa-"):]
            alias = "oa::" + (rest.replace("-", "::", 1).replace("-", "_") if "-" in rest else rest)
            if alias not in declared.get(name, set()):
                self.report("name", name, f"has no ALIAS {alias}")
        for path in self.files:
            module = self.module_of(path)
            if module is None or not path.startswith(module + "/include/"):
                continue
            group, sub = module_names(module)
            include = path[len(module) + len("/include/"):]
            prefix = f"oa/{group}" if sub is None else f"oa/{group}/{sub.replace('-', '_')}"
            good = include.startswith(prefix + "/") or (sub is not None and posixpath.splitext(include)[0] == prefix)
            if not good:
                self.report("name", path, f"is included as {include}; headers of {module} belong under {prefix}")

    def run(self):
        self.check_build_files()
        self.check_targets()
        self.check_includes()
        self.check_layers()
        self.check_names()
        return self.findings


def load_baseline(path):
    """Read the baseline of known layer exceptions; a missing file is an empty baseline."""
    if not path or not Path(path).is_file():
        return {}
    text = read_bounded(path, MAX_BASELINE_BYTES, "the baseline")
    try:
        baseline = json.loads(text)
    except json.JSONDecodeError as error:
        raise InputError(f"the baseline {path} is not JSON: {error.msg} at line {error.lineno}") from error
    entries = baseline.get("layer_exceptions", []) if isinstance(baseline, dict) else None
    if not isinstance(entries, list) or not all(isinstance(entry, dict) and isinstance(entry.get("from"), str)
                                                and isinstance(entry.get("to"), str) for entry in entries):
        raise InputError(f"the baseline {path} needs a layer_exceptions list of {{\"from\", \"to\"}} entries")
    return baseline


def check(root, targets, baseline_path):
    """Check one configured tree; print the findings and return the exit status."""
    try:
        checker = Checker(root, targets, load_baseline(baseline_path))
    except InputError as error:
        print(f"check_layout: {error}", file=sys.stderr)
        return 2
    findings = checker.run()
    for rule, where, message in findings:
        print(f"{where}: {rule}: {message}")
    if findings:
        print(f"check_layout: {len(findings)} findings in {len(checker.modules)} modules", file=sys.stderr)
        return 1
    print(f"check_layout: {len(checker.modules)} modules and {len(checker.engine)} targets keep the layout")
    return 0


# ---- self-test --------------------------------------------------------------------


SELF_TEST_FILES = {
    "src/core/CMakeLists.txt": "add_library(oa-core-types INTERFACE)\nadd_library(oa::core::types ALIAS oa-core-types)\n",
    "src/core/include/oa/core/types.h": "#pragma once\n",
    "src/base/game-math/CMakeLists.txt":
        "add_library(oa-base-game-math src/math.cpp)\nadd_library(oa::base::game_math ALIAS oa-base-game-math)\n",
    "src/base/game-math/include/oa/base/game_math.hpp": '#pragma once\n#include "oa/core/types.h"\n',
    "src/base/game-math/src/math.cpp": '#include "oa/base/game_math.hpp"\n#include "oa/sim/unit.hpp"\n',
    "src/base/game-math/src/table.inc": "\n",
    "src/sim/unit/CMakeLists.txt":
        "add_library(oa-sim-unit src/unit.cpp)\nadd_library(oa::sim::unit ALIAS oa-sim-unit)\n"
        "add_subdirectory(../../base/game-math math)\n",
    "src/sim/unit/include/oa/sim/unit.hpp": '#pragma once\n#include "oa/base/game_math.hpp"\n',
    "src/sim/unit/include/oa/misplaced.hpp": "#pragma once\n",
    "src/sim/unit/src/unit.cpp": '#include "oa/sim/unit.hpp"\n#include "../../../base/game-math/src/table.inc"\n',
    "src/platform/CMakeLists.txt": "add_library(oa-platform-shims src/files.cpp)\n",
    "src/platform/include/oa/platform/files.hpp": '#pragma once\n#include "../../../src/detail.hpp"\n',
    "src/platform/src/detail.hpp": "#pragma once\n",
    "src/platform/src/files.cpp": '#include "oa/platform/files.hpp"\n',
    "src/ui/hud/CMakeLists.txt": "add_library(hud src/hud.cpp)\nadd_library(oa::ui::hud ALIAS hud)\n",
    "src/ui/hud/src/hud.cpp": '#include "oa/sim/unit.hpp"\n',
}

# (rule, where) of each finding the self-test tree must give.
SELF_TEST_EXPECTED = [
    ("reach", "src/sim/unit/CMakeLists.txt:3"),  # add_subdirectory(../..)
    ("foreign", "hud"),  # linked from another directory
    ("reach", "oa-sim-unit"),  # platform's include directory
    ("include", "src/sim/unit/src/unit.cpp:2"),  # a relative path into game-math
    ("include", "src/sim/unit/include/oa/sim/unit.hpp:2"),  # game-math without a link
    ("include", "src/platform/include/oa/platform/files.hpp:2"),  # out of include/ to a private header
    ("layer", "oa-base-game-math"),  # base links sim
    ("name", "oa-platform-shims"),  # no alias
    ("name", "hud"),  # not oa-ui-hud
    ("name", "src/sim/unit/include/oa/misplaced.hpp"),  # outside oa/sim/unit
    ("stale", "oa-sim-unit -> oa-core-types"),  # a baseline entry for a link that keeps the order
]


def self_test_targets(root):
    """Return the layout-targets text of the self-test tree."""
    def target(name, kind, folder, sources="", includes="", links="", interface_links="", extra_includes=""):
        lines = [f"{name}\tTYPE\t{kind}", f"{name}\tSOURCE_DIR\t{root / folder}"]
        if sources:
            lines.append(f"{name}\tSOURCES\t{sources}")
        if includes:
            lines.append(f"{name}\tINTERFACE_INCLUDE_DIRECTORIES\t{root / folder / includes}")
            lines.append(f"{name}\tINCLUDE_DIRECTORIES\t{root / folder / includes}{extra_includes}")
        if links:
            lines.append(f"{name}\tLINK_LIBRARIES\t{links}")
        if interface_links:
            lines.append(f"{name}\tINTERFACE_LINK_LIBRARIES\t{interface_links}")
        return "\n".join(lines)

    return "\n".join([
        target("oa-core-types", "INTERFACE_LIBRARY", "src/core", includes="include"),
        target("oa-base-game-math", "STATIC_LIBRARY", "src/base/game-math", "src/math.cpp", "include",
               "oa-core-types;oa-sim-unit", "oa-core-types;oa-sim-unit"),
        target("oa-sim-unit", "STATIC_LIBRARY", "src/sim/unit", "src/unit.cpp", "include",
               extra_includes=f";{root}/src/platform/include"),
        target("oa-platform-shims", "STATIC_LIBRARY", "src/platform", "src/files.cpp", "include"),
        target("hud", "STATIC_LIBRARY", "src/ui/hud", "src/hud.cpp", links="::@(0x1);oa::sim::unit;::@"),
        "ALIAS\toa::sim::unit\toa-sim-unit",
    ]) + "\n"


def self_test():
    """Check the built-in tree and compare the findings with the expected ones."""
    with tempfile.TemporaryDirectory() as folder:
        root = Path(folder).resolve()
        for path, text in SELF_TEST_FILES.items():
            (root / path).parent.mkdir(parents=True, exist_ok=True)
            (root / path).write_text(text)
        targets = root / "targets.tsv"
        targets.write_text(self_test_targets(root))
        stale = {"layer_exceptions": [{"from": "oa-sim-unit", "to": "oa-core-types"}]}
        findings = Checker(root, targets, stale).run()
        got = sorted((rule, where) for rule, where, _ in findings)
        if got != sorted(SELF_TEST_EXPECTED):
            for item in sorted(set(SELF_TEST_EXPECTED) - set(got)):
                print(f"check_layout self-test: missing finding {item}")
            for rule, where, message in findings:
                if (rule, where) not in SELF_TEST_EXPECTED:
                    print(f"check_layout self-test: unexpected finding {where}: {rule}: {message}")
            return 1
        allowed = {"layer_exceptions": [{"from": "oa-base-game-math", "to": "oa-sim-unit"}]}
        if any(rule in ("layer", "stale") for rule, _, _ in Checker(root, targets, allowed).run()):
            print("check_layout self-test: a baseline entry did not allow its link")
            return 1
        malformed = root / "baseline.json"
        malformed.write_text('{"layer_exceptions": [{"from": 1}]}')
        with contextlib.redirect_stderr(io.StringIO()) as errors:
            status = check(root, targets, malformed)
        if status != 2 or "layer_exceptions" not in errors.getvalue():
            print("check_layout self-test: a malformed baseline was not refused")
            return 1
    print(f"check_layout self-test: {len(SELF_TEST_EXPECTED)} findings as expected")
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--root", default=str(ROOT), help="the engine tree (default: this checkout)")
    parser.add_argument("--targets", help="the layout-targets.tsv a configuration wrote")
    parser.add_argument("--baseline", default=str(BASELINE), help="the known layer exceptions")
    parser.add_argument("--self-test", action="store_true", help="check a small built-in tree")
    args = parser.parse_args(argv)
    if args.self_test:
        return self_test()
    if not args.targets:
        parser.error("--targets is required")
    return check(args.root, args.targets, args.baseline)


if __name__ == "__main__":
    sys.exit(main())
