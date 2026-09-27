# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

"""The layout pass's self-test: a small tree in today's layout, laid out in memory.

The tree has a module that moves to another group, a source handed to a
module the pass creates, a foreach loop that names the target it leaves, an
include directory that reaches into another module inside an if/else, app
headers included by bare name that move under include/, a Python path
joined from pieces, Markdown that names paths, include paths, targets and
namespaces, and a project that builds on the engine with a rename record of
its own. The test runs the pass on it and compares what it writes with the
expected text. It then adds a module and an app header the table does not
know and expects --check to name them, and the boundary header's include
of it that would leave include/; gives the header the include row that
cannot move it and expects --check to say so; and appends the rows
--propose prints and expects the header's problems to go.
"""
import tempfile
from pathlib import Path

TABLE = """\
module src/game/alpha src/sim/alpha -
module src/formats/pack src/formats/pack -
module src/app src/app -
root src/app - -
file src/app/registry.cpp src/ui/registry/src/registry.cpp -
file src/app/registry.hpp src/ui/registry/include/oa/ui/registry.hpp -
file src/app/app.hpp src/app/include/oa/app/app.hpp -
file src/app/state.hpp src/app/include/oa/app/state.hpp -
include oa/alpha.hpp oa/sim/alpha.hpp -
include oa/pack.hpp oa/formats/pack.hpp -
target oa-alpha oa-sim-alpha -
target oa-pack oa-formats-pack -
target oa-registry oa-ui-registry -
owner src/formats/pack/include oa-pack -
namespace oa oa -
namespace oa::alpha oa::sim::alpha -
namespace oa::pack oa::formats::pack -
keep tools/fixture.py - made-up paths
"""

PATCHES = """\
@@ post CMakeLists.txt
<<<
add_subdirectory(src/formats/pack)
===
add_subdirectory(src/formats/pack)
add_subdirectory(src/ui/registry)
>>>
@@ post tools/check.py
<<<
HEADER = ROOT / "src" / "app" / "registry.hpp"
===
HEADER = ROOT / "src" / "ui" / "registry" / "include" / "oa" / "ui" / "registry.hpp"
>>>
"""

NEW_FILES = {
    "src/ui/registry/CMakeLists.txt":
        "add_library(oa-ui-registry src/registry.cpp)\n"
        "target_include_directories(oa-ui-registry PUBLIC include)\n",
}

TREE = {
    "CMakeLists.txt": """\
add_subdirectory(src/game/alpha)
add_subdirectory(src/formats/pack)
add_library(oa-registry src/app/registry.cpp)
target_link_libraries(oa-registry PUBLIC oa-alpha)
foreach(target IN ITEMS oa-alpha oa-registry
    oa-pack)
  if(TARGET ${target})
    list(APPEND archives $<TARGET_FILE:${target}>)
  endif()
endforeach()
""",
    "src/app/registry.cpp": '#include "registry.hpp"\n',
    "src/app/registry.hpp": "#pragma once\n",
    "src/app/app.hpp": '#pragma once\n#include "state.hpp"\n',
    "src/app/state.hpp": "#pragma once\n",
    "src/app/main.cpp": '#include "app.hpp"\n#include "state.hpp"\n',
    "src/game/alpha/CMakeLists.txt": """\
cmake_minimum_required(VERSION 3.24)
project(oa_alpha LANGUAGES CXX)
add_library(oa-alpha src/alpha.cpp)
target_include_directories(oa-alpha PUBLIC include)
if(TARGET oa-pack)
  target_link_libraries(oa-alpha PUBLIC oa-pack)
else()
  target_include_directories(oa-alpha PUBLIC ../../formats/pack/include)
endif()
""",
    "src/game/alpha/include/oa/alpha.hpp": "#pragma once\nnamespace oa::alpha {\nstruct Thing {};\nint count();\n}\n",
    "src/game/alpha/src/alpha.cpp": """\
#include "oa/alpha.hpp"
#include "oa/pack.hpp"
// Counts through oa/pack.hpp.
namespace oa::alpha {
int count() { return oa::pack::size(); }
}
""",
    "src/formats/pack/CMakeLists.txt": "add_library(oa-pack src/pack.cpp)\ntarget_include_directories(oa-pack PUBLIC include)\n",
    "src/formats/pack/include/oa/pack.hpp": "#pragma once\nnamespace oa::pack {\nint size();\n}\n",
    "src/formats/pack/src/pack.cpp": '#include "oa/pack.hpp"\nnamespace oa::pack {\nint size() { return 1; }\n}\n',
    "tools/check.py": 'HEADER = ROOT / "src" / "app" / "registry.hpp"\n',
    "tools/joined.py": 'SOURCE = ROOT / "src" / "game" / "alpha"\n',
    "tools/fixture.py": 'FILES = {"src/game/alpha/a.cpp": ""}\n',
    "docs/notes.md": "`src/game/alpha/include/oa/alpha.hpp` declares `oa::alpha::Thing` (oa/alpha.hpp, oa-alpha).\n",
}

PROJECT = {
    "CMakeLists.txt": "project(ext)\nadd_library(ext-lib x.cpp)\ntarget_link_libraries(ext-lib PRIVATE oa-alpha ${OA_ENGINE_DIR})\n",
    "x.cpp": '#include "oa/alpha.hpp"\nint f() { return oa::alpha::count(); }\n',
    "src/own/a.cpp": "",
    "AGENTS.md": "The engine's src/game/alpha and our src/own.\n",
    "record/renames.tsv": "old\tnew\tkind\tscope\n",
    "record/notes.md": "oa::alpha::Thing and src/game/alpha as recorded.\n",
}

EXPECTED = {
    ("engine", "CMakeLists.txt"): """\
add_subdirectory(src/sim/alpha)
add_subdirectory(src/formats/pack)
add_subdirectory(src/ui/registry)
foreach(target IN ITEMS oa-sim-alpha oa-ui-registry
    oa-formats-pack)
  if(TARGET ${target})
    list(APPEND archives $<TARGET_FILE:${target}>)
  endif()
endforeach()
""",
    ("engine", "src/sim/alpha/CMakeLists.txt"): """\
cmake_minimum_required(VERSION 3.24)
project(oa_sim_alpha LANGUAGES CXX)
add_library(oa-sim-alpha src/alpha.cpp)
add_library(oa::sim::alpha ALIAS oa-sim-alpha)
target_include_directories(oa-sim-alpha PUBLIC include)
target_link_libraries(oa-sim-alpha PUBLIC oa-formats-pack)
""",
    ("engine", "src/sim/alpha/src/alpha.cpp"): """\
#include "oa/sim/alpha.hpp"
#include "oa/formats/pack.hpp"
// Counts through oa/formats/pack.hpp.
namespace oa::sim::alpha {
int count() { return oa::formats::pack::size(); }
}
""",
    ("engine", "src/ui/registry/src/registry.cpp"): '#include "oa/ui/registry.hpp"\n',
    ("engine", "src/app/include/oa/app/app.hpp"): TREE["src/app/app.hpp"],
    ("engine", "src/app/main.cpp"): '#include "oa/app/app.hpp"\n#include "oa/app/state.hpp"\n',
    ("engine", "src/ui/registry/CMakeLists.txt"):
        "add_library(oa-ui-registry src/registry.cpp)\n"
        "add_library(oa::ui::registry ALIAS oa-ui-registry)\n"
        "target_include_directories(oa-ui-registry PUBLIC include)\n",
    ("engine", "tools/check.py"): 'HEADER = ROOT / "src" / "ui" / "registry" / "include" / "oa" / "ui" / "registry.hpp"\n',
    ("engine", "tools/fixture.py"): TREE["tools/fixture.py"],
    ("engine", "docs/notes.md"):
        "`src/sim/alpha/include/oa/sim/alpha.hpp` declares `oa::sim::alpha::Thing` (oa/sim/alpha.hpp, "
        "oa-sim-alpha).\n",
    ("ext", "x.cpp"): '#include "oa/sim/alpha.hpp"\nint f() { return oa::sim::alpha::count(); }\n',
    ("ext", "CMakeLists.txt"):
        "project(ext)\nadd_library(ext-lib x.cpp)\ntarget_link_libraries(ext-lib PRIVATE oa-sim-alpha ${OA_ENGINE_DIR})\n",
    ("ext", "AGENTS.md"): "The engine's src/sim/alpha and our src/own.\n",
    ("ext", "record/notes.md"): PROJECT["record/notes.md"],
}

# The one problem the tree gives: a joined path no hunk rewrites.
EXPECTED_PROBLEMS = ['tools/joined.py: names "src" / "game" / "alpha", which the pass removes']

# A module the table does not know, under a group the pass dissolves, and
# an app header it does not know, which a boundary header includes.
DRIFT = {"src/game/stray/CMakeLists.txt": "add_library(oa-stray src/stray.cpp)\n", "src/game/stray/src/stray.cpp": "",
         "src/app/draws.hpp": "#pragma once\n",
         "src/app/app.hpp": '#pragma once\n#include "state.hpp"\n#include "draws.hpp"\n'}
# The problems no row can answer: the stray module needs a module row.
EXPECTED_UNROWED = [
    "src/game/stray/CMakeLists.txt: its directory has no module row",
    "src/game/stray: would stay outside the layout's groups",
]
# The boundary header moves under include/ and the new one stays.
REACH_OUT = 'src/app/app.hpp: #include "draws.hpp" would reach src/app/draws.hpp, outside the include/ directory'
EXPECTED_DRIFT = EXPECTED_UNROWED + [
    "src/game/stray/CMakeLists.txt: library oa-stray has no row",
    "header src/app/draws.hpp has no row",
    REACH_OUT,
]
# The file row --propose gives the header; an include row cannot move it.
PROPOSED_FILE_ROW = "file src/app/draws.hpp src/app/include/oa/app/draws.hpp -"
INERT_ROW = "include draws.hpp oa/app/draws.hpp -\n"
EXPECTED_INERT = EXPECTED_UNROWED + [
    "src/game/stray/CMakeLists.txt: library oa-stray has no row",
    "include row draws.hpp: src/app/draws.hpp is not under its module's include/",
    REACH_OUT,
]


def write_tree(root, files):
    """Write files (path -> text) below a directory."""
    for path, text in files.items():
        (root / path).parent.mkdir(parents=True, exist_ok=True)
        (root / path).write_text(text)


def lay_out(layout, root, files, rows=TABLE):
    """Run the pass in memory on a tree with a table's rows; return (plan, work)."""
    write_tree(root, files)
    write_tree(root / "ext", PROJECT)
    table = root / "table.txt"
    table.write_text(rows)
    patches = root / "patches.txt"
    patches.write_text(PATCHES)
    new_files = root / "files"
    write_tree(new_files, NEW_FILES)
    areas = {layout.ENGINE: layout.Area(layout.ENGINE, root, sorted(files), None, ""),
             "ext": layout.Area("ext", root / "ext", sorted(PROJECT), None, "")}
    plan = layout.Plan(layout.read_table(table), layout.read_patches(patches), areas)
    plan.compute_moves()
    plan.compute_namespaces()
    plan.check_coverage()
    work = layout.build(plan, new_files)
    layout.scan_vanished(plan, work)
    return plan, work


def written(plan, work):
    """Return {(area, path after the pass): text} of every file the pass writes."""
    out = {}
    for area, path, text in work.changed():
        out[(area, plan.new_path(path) if area == "engine" else path)] = text
    for (area, path), text in work.created.items():
        out[(area, path)] = text
    for area, path in ((name, path) for name, area in plan.areas.items() for path in area.files):
        new = plan.new_path(path) if area == "engine" else path
        out.setdefault((area, new), plan.areas[area].read(path))
    return out


def matches(problems, expected):
    """Whether each expected problem starts exactly one problem, and no problem is left over."""
    left = list(problems)
    for start in expected:
        found = [problem for problem in left if problem.startswith(start)]
        if len(found) != 1:
            return False
        left.remove(found[0])
    return not left


def run(layout):
    """Run the self-test; print what differs and return the exit status."""
    failures = []
    with tempfile.TemporaryDirectory() as folder:
        plan, work = lay_out(layout, Path(folder).resolve() / "tree", TREE)
        out = written(plan, work)
        for key, text in EXPECTED.items():
            if out.get(key) != text:
                failures.append(f"{key[0]}:{key[1]}: expected\n{text}got\n{out.get(key)}")
        if not matches(plan.problems, EXPECTED_PROBLEMS):
            failures.append("problems: expected " + repr(EXPECTED_PROBLEMS) + ", got " + repr(plan.problems))
        base = Path(folder).resolve()
        drift, _ = lay_out(layout, base / "drift", {**TREE, **DRIFT})
        if not matches(drift.problems, EXPECTED_PROBLEMS + EXPECTED_DRIFT):
            failures.append("drift problems: expected " + repr(EXPECTED_PROBLEMS + EXPECTED_DRIFT) + ", got "
                            + repr(drift.problems))
        proposed = layout.propose(drift)
        if PROPOSED_FILE_ROW not in proposed:
            failures.append(f"propose: expected {PROPOSED_FILE_ROW!r} among {proposed!r}")
        inert, _ = lay_out(layout, base / "inert", {**TREE, **DRIFT}, TABLE + INERT_ROW)
        if not matches(inert.problems, EXPECTED_PROBLEMS + EXPECTED_INERT):
            failures.append("inert include row problems: expected " + repr(EXPECTED_PROBLEMS + EXPECTED_INERT) +
                            ", got " + repr(inert.problems))
        fixed, _ = lay_out(layout, base / "proposed", {**TREE, **DRIFT}, TABLE + "\n".join(proposed) + "\n")
        if not matches(fixed.problems, EXPECTED_PROBLEMS + EXPECTED_UNROWED):
            failures.append("problems after the proposed rows: expected " +
                            repr(EXPECTED_PROBLEMS + EXPECTED_UNROWED) + ", got " + repr(fixed.problems))
    for failure in failures:
        print(f"layout self-test: {failure}")
    if failures:
        return 1
    print(f"layout self-test: {len(EXPECTED)} files and "
          f"{len(EXPECTED_PROBLEMS) + len(EXPECTED_DRIFT) + len(EXPECTED_INERT)} problems as expected; the proposed "
          f"rows answer the rest")
    return 0
