#!/usr/bin/env python3
# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

"""Format the engine's C, C++ and Objective-C sources with the pinned clang-format, or check that they are.

Every source the engine owns (tools/engine_files.py: the files Git tracks,
and new ones it does not ignore, less the nested projects and third-party
directories) is formatted as the root .clang-format says, except fragments
that another file includes in the middle of itself (.inc, .inl), which have
no context of their own to format.
Paths given on the command line, files or directories relative to --root,
narrow the run to the sources under them; one under which there is no such
source is an error.

Only the clang-format release that tools/format/requirements.txt pins is
used, so that every platform formats alike. The run takes the first of:
--clang-format, the OA_CLANG_FORMAT environment variable, the virtual
environment --setup makes under local/ (LOCAL_ENVIRONMENT), and each
clang-format on PATH in order; one named by --clang-format or
OA_CLANG_FORMAT must be the pinned release, and one found otherwise is
passed over when it is not. --setup makes the virtual environment and
installs the pinned wheel into it, which needs Python's venv and pip and a
connection to PyPI.

A run rewrites the sources that differ and prints how many it changed. A
source is held to the form that clang-format leaves unchanged when run over
it again (it does not always settle in one run), so a second run of this
tool changes nothing. --check changes nothing: it prints each source
that differs, with --diff its differences too, and fails when there is one.

Exit status: 0 when every source is (or, with --check, was) formatted; 1 when
--check finds a source that is not; 2 on an error (a named path with no
source under it, a named clang-format that is not the pinned release, a
source that cannot be read or formatted); 77 when --check finds no pinned
clang-format, which ctest reports as skipped.
"""
import argparse
import concurrent.futures
import difflib
import os
import re
import shutil
import subprocess
import sys
import venv
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from engine_files import FRAGMENT_SUFFIXES, engine_sources  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
# The pinned release, relative to the root.
REQUIREMENTS = Path("tools") / "format" / "requirements.txt"
PIN_RE = re.compile(r"^clang-format==([0-9][0-9.]*)\s*(?:\\|$)", re.MULTILINE)
VERSION_RE = re.compile(r"clang-format version (\d+(?:\.\d+)*)")
# The virtual environment --setup makes, relative to the root, and where the
# executable lies in it on POSIX systems and on Windows.
LOCAL_ENVIRONMENT = Path("local") / "format-venv"
ENVIRONMENT_EXECUTABLES = (Path("bin") / "clang-format", Path("Scripts") / "clang-format.exe")
EXECUTABLE_NAME = "clang-format"
EXECUTABLE_VARIABLE = "OA_CLANG_FORMAT"
CONFIG_NAME = ".clang-format"
# Exit status for a check that cannot run, which ctest reports as skipped.
EXIT_SKIPPED = 77
# Bound on a source file, and on the lines of differences --diff prints for
# one source.
MAX_SOURCE_BYTES = 16 << 20
MAX_DIFF_LINES = 400
VERSION_TIMEOUT_SECONDS = 30
FORMAT_TIMEOUT_SECONDS = 120
# How many times one source may be run through clang-format before its
# output must stop changing.
MAX_FORMAT_RUNS = 4


class FormatError(Exception):
    """A source that cannot be read or formatted, or a clang-format that cannot be used."""


def pinned_version(root):
    """Reads the clang-format release tools/format/requirements.txt pins.

    @param root the tree's root
    @return the release, as its wheel numbers it
    """
    path = root / REQUIREMENTS
    try:
        match = PIN_RE.search(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError) as error:
        raise FormatError(f"{path}: {error}") from error
    if not match:
        raise FormatError(f"{path}: no clang-format==<version> line")
    return match.group(1)


def matches_pin(found, pinned):
    """Tells whether a clang-format reports the pinned release.

    A wheel may add a fourth number to the release it packages (13.0.1.1
    packages 13.0.1).

    @param found the version the executable reports
    @param pinned the wheel's version
    @return true when they name the same release
    """
    return found == pinned or (pinned.count(".") == 3 and pinned.rsplit(".", 1)[0] == found)


def executable_version(executable):
    """Asks a clang-format executable for its version.

    @param executable path or name of the executable
    @return the version it reports, or None when it cannot be run
    """
    try:
        result = subprocess.run([str(executable), "--version"], capture_output=True, text=True,
                                timeout=VERSION_TIMEOUT_SECONDS)
    except (OSError, subprocess.SubprocessError):
        return None
    match = VERSION_RE.search(result.stdout)
    return match.group(1) if result.returncode == 0 and match else None


def find_clang_format(root, named, pinned, environment=None):
    """Finds the pinned clang-format.

    The one named by --clang-format, else by OA_CLANG_FORMAT, is taken as
    it is; else the first of the virtual environment's and each directory
    of PATH's that reports the pinned release.

    @param root the tree's root
    @param named the executable --clang-format names, or None
    @param pinned the pinned release
    @param environment the variables OA_CLANG_FORMAT and PATH are read from; None for this process's
    @return the executable's path, or None when no pinned release is found
    """
    environment = os.environ if environment is None else environment
    named = named or environment.get(EXECUTABLE_VARIABLE) or None
    if named:
        version = executable_version(named)
        if version is None or not matches_pin(version, pinned):
            raise FormatError(f"{named} reports version {version or 'nothing'}; tools/format_sources.py uses "
                              f"clang-format {pinned} only")
        return named
    candidates = [root / LOCAL_ENVIRONMENT / executable for executable in ENVIRONMENT_EXECUTABLES]
    for directory in os.get_exec_path(environment):
        on_path = shutil.which(EXECUTABLE_NAME, path=directory)
        if on_path:
            candidates.append(Path(on_path))
    for candidate in candidates:
        if candidate.is_file():
            version = executable_version(candidate)
            if version is not None and matches_pin(version, pinned):
                return str(candidate)
    return None


def setup(root):
    """Makes the local virtual environment and installs the pinned clang-format into it.

    @param root the tree's root
    @return exit status
    """
    environment = root / LOCAL_ENVIRONMENT
    print(f"format_sources: making {environment}")
    venv.EnvBuilder(with_pip=True, clear=False).create(environment)
    python = next((environment / name for name in (Path("bin") / "python", Path("Scripts") / "python.exe")
                   if (environment / name).is_file()), None)
    if python is None:
        print(f"format_sources: {environment} holds no Python", file=sys.stderr)
        return 2
    result = subprocess.run([str(python), "-m", "pip", "install", "--require-hashes", "-r",
                             str(root / REQUIREMENTS)])
    if result.returncode != 0:
        print("format_sources: pip could not install the pinned clang-format", file=sys.stderr)
        return 2
    executable = find_clang_format(root, None, pinned_version(root))
    if executable is None:
        print(f"format_sources: {environment} holds no clang-format of the pinned release", file=sys.stderr)
        return 2
    print(f"format_sources: {executable} is ready")
    return 0


def selected_sources(root, paths):
    """Lists the sources a run formats, and the paths named that hold none.

    @param root the tree's root
    @param paths files or directories relative to the root that narrow the run; empty for the whole tree
    @return (the sources, relative to the root and sorted; the paths, as given, under which none lies)
    """
    sources = [name for name in engine_sources(root) if Path(name).suffix not in FRAGMENT_SUFFIXES]
    if not paths:
        return sources, []
    chosen = set()
    empty = []
    for path in paths:
        prefix = Path(os.path.relpath((root / path).resolve(), root.resolve())).as_posix()
        under = [name for name in sources
                 if prefix == "." or name == prefix or name.startswith(prefix.rstrip("/") + "/")]
        if not under:
            empty.append(str(path))
        chosen.update(under)
    return sorted(chosen), empty


def clang_format_once(executable, root, path, text):
    """Runs clang-format once over a source's text.

    @param executable the clang-format to run
    @param root the tree's root, whose .clang-format gives the style
    @param path the source's path, which gives its language
    @param text the text to format, as bytes
    @return the formatted text, as bytes
    """
    try:
        result = subprocess.run(
            [executable, f"--style=file:{root / CONFIG_NAME}", "--fallback-style=none",
             f"--assume-filename={path}"],
            input=text, capture_output=True, timeout=FORMAT_TIMEOUT_SECONDS)
    except (OSError, subprocess.SubprocessError) as error:
        raise FormatError(f"{path.relative_to(root)}: {error}") from error
    if result.returncode != 0:
        raise FormatError(f"{path.relative_to(root)}: clang-format failed: "
                          f"{result.stderr.decode('utf-8', 'replace').strip()}")
    return result.stdout


def formatted_text(executable, root, name):
    """Formats one source without changing it, running clang-format until its output settles.

    clang-format does not always settle in one run: a trailing comment it
    aligned in the first may move in the second. The form a source is held
    to is the one a further run leaves unchanged, reached within
    MAX_FORMAT_RUNS runs.

    @param executable the clang-format to run
    @param root the tree's root
    @param name the source, relative to the root
    @return (the source as it is, the source as clang-format settles it), as bytes
    """
    path = root / name
    try:
        if path.stat().st_size > MAX_SOURCE_BYTES:
            raise FormatError(f"{name}: larger than {MAX_SOURCE_BYTES} bytes")
        current = path.read_bytes()
    except OSError as error:
        raise FormatError(f"{name}: {error}") from error
    text = current
    for _ in range(MAX_FORMAT_RUNS):
        formatted = clang_format_once(executable, root, path, text)
        if formatted == text:
            return current, formatted
        text = formatted
    raise FormatError(f"{name}: clang-format's output still changes after {MAX_FORMAT_RUNS} runs")


def print_differences(name, current, formatted):
    """Prints how a source differs from its formatted form, up to MAX_DIFF_LINES lines.

    @param name the source, relative to the root
    @param current the source as it is
    @param formatted the source as clang-format writes it
    """
    lines = list(difflib.unified_diff(current.decode("utf-8", "replace").splitlines(keepends=True),
                                      formatted.decode("utf-8", "replace").splitlines(keepends=True),
                                      f"a/{name}", f"b/{name}"))
    sys.stdout.writelines(lines[:MAX_DIFF_LINES])
    if len(lines) > MAX_DIFF_LINES:
        print(f"... {len(lines) - MAX_DIFF_LINES} more line(s)")


def run(root, executable, sources, check, show_differences, jobs):
    """Formats the sources, or with check reports those that are not formatted.

    @param root the tree's root
    @param executable the clang-format to run
    @param sources the sources, relative to the root
    @param check true to change nothing
    @param show_differences true to print each unformatted source's differences
    @param jobs how many sources to format at once
    @return exit status
    """
    differing = []
    errors = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
        futures = {pool.submit(formatted_text, executable, root, name): name for name in sources}
        results = {}
        for future in concurrent.futures.as_completed(futures):
            name = futures[future]
            try:
                results[name] = future.result()
            except FormatError as error:
                errors.append(str(error))
    for error in sorted(errors):
        print(f"format_sources: {error}", file=sys.stderr)
    if errors:
        return 2
    for name in sources:
        current, formatted = results[name]
        if current == formatted:
            continue
        differing.append(name)
        if check:
            print(f"{name}: not formatted")
            if show_differences:
                print_differences(name, current, formatted)
        else:
            (root / name).write_bytes(formatted)
    if check:
        if differing:
            print(f"format_sources: {len(differing)} of {len(sources)} source(s) not formatted; "
                  f"python3 tools/format_sources.py formats them")
            return 1
        print(f"format_sources: {len(sources)} source(s) formatted")
        return 0
    print(f"format_sources: formatted {len(differing)} of {len(sources)} source(s)")
    return 0


# A stand-in for clang-format that the self-test runs: it reports VERSION,
# and given text removes its first 'x' (MODE 'settle': the output settles
# once no 'x' is left) or appends one (MODE 'grow': it never settles).
STAND_IN_SCRIPT = """import sys
VERSION = {version!r}
MODE = {mode!r}
if "--version" in sys.argv[1:]:
    print("clang-format version " + VERSION)
    sys.exit(0)
data = sys.stdin.buffer.read()
sys.stdout.buffer.write(data.replace(b"x", b"", 1) if MODE == "settle" else data + b"x")
"""


def write_stand_in(directory, version, mode="settle"):
    """Writes a stand-in clang-format, a shell script that runs STAND_IN_SCRIPT with this Python.

    @param directory where it goes, made when missing
    @param version the version it reports
    @param mode 'settle' to remove the first 'x' of its input, 'grow' to append one
    @return its path
    """
    directory.mkdir(parents=True, exist_ok=True)
    script = directory / "clang_format_stand_in.py"
    script.write_text(STAND_IN_SCRIPT.format(version=version, mode=mode), encoding="utf-8")
    launcher = directory / EXECUTABLE_NAME
    launcher.write_text(f'#!/bin/sh\nexec "{sys.executable}" "{script}" "$@"\n', encoding="utf-8")
    launcher.chmod(0o755)
    return launcher


def self_test_sources(root):
    """Checks the pinned release's reading and the choice of sources on a small tree without Git.

    @param root an empty directory to build the tree in
    @return the failures
    """
    failures = []
    (root / REQUIREMENTS).parent.mkdir(parents=True)
    (root / REQUIREMENTS).write_text("# pinned\n--only-binary :all:\nclang-format==23.1.1 \\\n"
                                     "    --hash=sha256:00\n", encoding="utf-8")
    if pinned_version(root) != "23.1.1":
        failures.append("the pinned release was misread")
    for name in ("src/a/a.cpp", "src/a/table.inc", "src/b/b.hpp", "src/b/b.mm", "docs/x.md",
                 "nested/CMakeLists.txt", "nested/n.cpp", "src/vendor/v.cpp"):
        (root / name).parent.mkdir(parents=True, exist_ok=True)
        (root / name).write_text("", encoding="utf-8")
    (root / "nested/CMakeLists.txt").write_text("project(n)\nset(x ${OA_ENGINE_DIR})\n", encoding="utf-8")
    chosen = selected_sources(root, [])
    if chosen != (["src/a/a.cpp", "src/b/b.hpp", "src/b/b.mm"], []):
        failures.append(f"the whole tree chose {chosen}")
    chosen = selected_sources(root, ["src/b"])
    if chosen != (["src/b/b.hpp", "src/b/b.mm"], []):
        failures.append(f"a directory chose {chosen}")
    chosen = selected_sources(root, ["src/b/b.mm", "src/a/table.inc", "docs", "nested", "src/vendor"])
    if chosen != (["src/b/b.mm"], ["src/a/table.inc", "docs", "nested", "src/vendor"]):
        failures.append(f"paths with no source to format chose {chosen}")
    return failures


def self_test_new_files(root):
    """Checks that a source not yet added to Git is chosen and one Git ignores is not.

    @param root an empty directory to make the repository in
    @return the failures
    """
    if shutil.which("git") is None:
        print("format_sources self-test: no git; the case of a file not yet added is skipped")
        return []
    for name in ("src/added.cpp", "src/new.cpp", "ignored/i.cpp"):
        (root / name).parent.mkdir(parents=True, exist_ok=True)
        (root / name).write_text("", encoding="utf-8")
    (root / ".gitignore").write_text("/ignored/\n", encoding="utf-8")
    for command in (["init", "-q"], ["add", "src/added.cpp"]):
        result = subprocess.run(["git", "-C", str(root), *command], capture_output=True, text=True)
        if result.returncode != 0:
            return [f"git {command[0]} failed: {result.stderr.strip()}"]
    chosen = selected_sources(root, [])
    if chosen != (["src/added.cpp", "src/new.cpp"], []):
        return [f"a repository with a new and an ignored source chose {chosen}"]
    return []


def self_test_clang_format(root):
    """Checks the search for the pinned clang-format, and the runs until its output settles, with stand-ins.

    @param root an empty directory to put the stand-ins in
    @return the failures
    """
    import contextlib
    import io
    if os.name == "nt" or not sys.executable:
        print("format_sources self-test: the stand-ins need a POSIX shell; the clang-format cases are skipped")
        return []
    failures = []
    pinned = "23.1.1"
    in_environment = write_stand_in(root / LOCAL_ENVIRONMENT / "bin", pinned)
    older = write_stand_in(root / "older", "22.1.0")
    on_path = write_stand_in(root / "pinned", pinned)
    path = os.pathsep.join(str(root / name) for name in ("older", "pinned"))

    def find(named, environment):
        """Finds clang-format as a run would; returns FormatError when the one named is refused."""
        try:
            return find_clang_format(root, named, pinned, environment)
        except FormatError:
            return FormatError

    for what, named, environment, expected in (
            ("the virtual environment's pinned release", None, {"PATH": path}, str(in_environment)),
            ("--clang-format over OA_CLANG_FORMAT", str(on_path), {EXECUTABLE_VARIABLE: str(older)}, str(on_path)),
            ("OA_CLANG_FORMAT naming another release", None, {EXECUTABLE_VARIABLE: str(older)}, FormatError)):
        if find(named, environment) != expected:
            failures.append(f"{what}: found {find(named, environment)}, expected {expected}")
    write_stand_in(in_environment.parent, "22.1.0")
    for what, environment, expected in (
            ("another release in the virtual environment and first on PATH", {"PATH": path}, str(on_path)),
            ("another release only", {"PATH": str(root / "older")}, None)):
        if find(None, environment) != expected:
            failures.append(f"{what}: found {find(None, environment)}, expected {expected}")
    # The first run leaves "x\n", which the second changes again to its settled form.
    (root / "s.cpp").write_bytes(b"xx\n")
    try:
        if formatted_text(str(on_path), root, "s.cpp") != (b"xx\n", b"\n"):
            failures.append("output that settles on the second run was not taken")
    except FormatError as error:
        failures.append(f"output that settles on the second run: {error}")
    with contextlib.redirect_stdout(io.StringIO()):
        checked = run(root, str(on_path), ["s.cpp"], check=True, show_differences=True, jobs=1)
        unchanged = (root / "s.cpp").read_bytes() == b"xx\n"
        written = run(root, str(on_path), ["s.cpp"], check=False, show_differences=False, jobs=1)
    if checked != 1 or not unchanged:
        failures.append(f"--check on an unformatted source gave {checked} and changed it: {not unchanged}")
    if written != 0 or (root / "s.cpp").read_bytes() != b"\n":
        failures.append(f"formatting gave {written} and wrote {(root / 's.cpp').read_bytes()!r}")
    growing = write_stand_in(root / "growing", pinned, mode="grow")
    try:
        formatted_text(str(growing), root, "s.cpp")
        failures.append("output that never settles was not reported")
    except FormatError:
        pass
    return failures


def self_test():
    """Checks the version rules, the choice of sources and the use of clang-format on small trees.

    @return exit status
    """
    import tempfile
    failures = []
    if not matches_pin("23.1.1", "23.1.1") or not matches_pin("13.0.1", "13.0.1.1"):
        failures.append("a matching release was refused")
    if matches_pin("23.1", "23.1.1") or matches_pin("23.1.10", "23.1.1") or matches_pin("22.1.1", "23.1.1"):
        failures.append("another release was accepted")
    for case in (self_test_sources, self_test_new_files, self_test_clang_format):
        with tempfile.TemporaryDirectory() as directory:
            failures.extend(case(Path(directory).resolve()))
    for failure in failures:
        print(f"format_sources self-test: {failure}")
    if failures:
        return 1
    print("format_sources self-test: passed")
    return 0


def main(argv=None):
    """Parses the command line and formats, checks, sets up or tests itself; returns the exit status."""
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("paths", nargs="*", help="files or directories relative to --root to narrow the run to")
    parser.add_argument("--root", type=Path, default=ROOT, help="the tree to format (default: %(default)s)")
    parser.add_argument("--check", action="store_true", help="change nothing; fail when a source is not formatted")
    parser.add_argument("--diff", action="store_true", help="with --check, print the differences")
    parser.add_argument("--clang-format", dest="clang_format", help="the clang-format executable to run")
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 1, help="sources formatted at once")
    parser.add_argument("--setup", action="store_true",
                        help=f"install the pinned clang-format into --root/{LOCAL_ENVIRONMENT.as_posix()}")
    parser.add_argument("--self-test", action="store_true", help="check the tool's own rules")
    args = parser.parse_args(argv)
    if args.self_test:
        return self_test()
    root = args.root.resolve()
    if not root.is_dir():
        print(f"format_sources: no such tree: {root}", file=sys.stderr)
        return 2
    if not args.setup:
        sources, empty = selected_sources(root, args.paths)
        for path in empty:
            print(f"format_sources: {path}: no source to format there (a C, C++ or Objective-C file of the "
                  f"engine that Git tracks or does not ignore; fragments, nested projects and third-party "
                  f"code are left alone)", file=sys.stderr)
        if empty:
            return 2
    try:
        if args.setup:
            return setup(root)
        pinned = pinned_version(root)
        executable = find_clang_format(root, args.clang_format, pinned)
    except FormatError as error:
        print(f"format_sources: {error}", file=sys.stderr)
        return 2
    if executable is None:
        message = (f"format_sources: clang-format {pinned} is not installed; python3 tools/format_sources.py "
                   f"--setup installs it under {LOCAL_ENVIRONMENT.as_posix()}")
        if args.check:
            print(f"{message} (skipped)")
            return EXIT_SKIPPED
        print(message, file=sys.stderr)
        return 2
    return run(root, executable, sources, args.check, args.diff, max(1, args.jobs))


if __name__ == "__main__":
    sys.exit(main())
