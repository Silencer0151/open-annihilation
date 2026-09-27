# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

"""List the files the engine owns, for the tools that rewrite, format or license the whole tree.

The engine's files are those Git tracks under the root, and those it would
track (new files not yet added and not ignored, so that a file just created
is formatted and given its header before it is first committed), less:

  - the nested projects that build on the engine (a directory whose
    CMakeLists.txt calls project() and takes OA_ENGINE_DIR), which keep
    their own rules;
  - code kept as its authors wrote it, in a directory named as
    THIRD_PARTY_DIRECTORIES lists;
  - symbolic links, and tracked files missing from the working tree.

When Git cannot list the tree (an unpacked source tree, or a checkout seen
from a container that cannot reach its repository), every file counts as
tracked except those tools/check_style.py leaves out of such a tree (build
trees, local/, .git/ and caches), a checkout's .git file, and those the
patterns of the root .gitignore match.

The files are found afresh on every run, so the tools follow files that
move.
"""
import fnmatch
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
# The files Git tracks, the nested projects, and the C, C++ and Objective-C
# sources as the style check reads them, and among them the C ones.
from check_style import C_SUFFIXES, SOURCE_SUFFIXES, nested_projects, tracked  # noqa: E402,F401

# Directory names that hold code from other projects, kept as its authors
# wrote it; no file of the tree lies in one yet.
THIRD_PARTY_DIRECTORIES = frozenset({"3rdparty", "external", "extern", "third-party", "third_party", "vendor"})
# Sources that are fragments of another file, included in the middle of it
# (a table inside an initializer, a list of macro calls): they have no
# context of their own to format, and an #include added to one would land
# wherever the including file puts it.
FRAGMENT_SUFFIXES = frozenset({".inc", ".inl"})
# The file a linked checkout holds in place of its .git directory.
GIT_LINK_NAME = ".git"
IGNORE_FILE = ".gitignore"


def is_third_party(name):
    """Tells whether a path lies in a directory of third-party code.

    @param name path relative to the root, with '/' separators
    @return true when a directory of the path is named as THIRD_PARTY_DIRECTORIES lists
    """
    return any(part in THIRD_PARTY_DIRECTORIES for part in name.split("/")[:-1])


def ignore_patterns(root):
    """Reads the patterns of the root .gitignore, leaving out negations, which are not followed.

    @param root the tree's root
    @return (pattern, anchored, directory only) triples
    """
    try:
        lines = (root / IGNORE_FILE).read_text(encoding="utf-8").splitlines()
    except (OSError, UnicodeDecodeError):
        return []
    patterns = []
    for line in lines:
        line = line.strip()
        if not line or line.startswith(("#", "!")):
            continue
        directory_only = line.endswith("/")
        pattern = line.strip("/")
        patterns.append((pattern, line.startswith("/") or "/" in pattern, directory_only))
    return patterns


def is_ignored(name, patterns):
    """Tells whether .gitignore patterns match a file or a directory above it.

    @param name path relative to the root, with '/' separators
    @param patterns the patterns ignore_patterns read
    @return true when a pattern matches
    """
    parts = name.split("/")
    for pattern, anchored, directory_only in patterns:
        ends = range(1, len(parts)) if directory_only else range(1, len(parts) + 1)
        for end in ends:
            candidate = "/".join(parts[:end]) if anchored else parts[end - 1]
            if fnmatch.fnmatchcase(candidate, pattern):
                return True
    return False


def listed_files(root):
    """Lists the files Git tracks, or would track, under a root; without Git, those its ignore rules keep.

    A file Git would track is one not yet added that its ignore rules do not
    match. A nested repository is listed as its directory, which no tool
    reads as a file.

    @param root the tree's root
    @return paths relative to the root, with '/' separators
    """
    try:
        listing = subprocess.run(["git", "-C", str(root), "ls-files", "-z", "--cached", "--others",
                                  "--exclude-standard", "--", "."], capture_output=True)
    except OSError:
        listing = None
    if listing is not None and listing.returncode == 0:
        # A path with a merge conflict is listed once per side.
        names = list(dict.fromkeys(name for name in listing.stdout.decode("utf-8").split("\0") if name))
        if names:
            return names
    patterns = ignore_patterns(root)
    return [name for name in tracked(root)
            if name.rsplit("/", 1)[-1] != GIT_LINK_NAME and not is_ignored(name, patterns)]


def engine_files(root):
    """Lists the files the engine owns under a root, sorted.

    @param root the tree's root
    @return paths relative to the root, with '/' separators
    """
    names = listed_files(root)
    projects = nested_projects(root, names)
    files = []
    for name in names:
        if name.startswith(projects) or is_third_party(name):
            continue
        path = root / name
        if path.is_symlink() or not path.is_file():
            continue
        files.append(name)
    return sorted(files)


def engine_sources(root):
    """Lists the engine's own C, C++ and Objective-C sources under a root, sorted.

    @param root the tree's root
    @return paths relative to the root, with '/' separators
    """
    return [name for name in engine_files(root) if Path(name).suffix in SOURCE_SUFFIXES]


def nested_project_directories(root):
    """Finds the nested projects below a root.

    @param root the tree's root
    @return their directories relative to the root, each ending in '/'
    """
    return nested_projects(root, listed_files(root))
