#!/usr/bin/env python3
# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

"""Check that the engine's Markdown files point only at things that exist.

Reads every tracked .md file under --root, leaving out nested projects that
build on the engine (a directory below the root whose CMakeLists.txt calls
project() and takes OA_ENGINE_DIR), and fails on:

  link      a relative link or image ([text](target), a reference definition
            '[name]: target', or an HTML href or src attribute) whose target
            is neither a tracked file nor a directory holding one; a
            footnote definition '[^name]: text' is not a link;
  anchor    a fragment (#name) that names no heading or HTML anchor in the
            Markdown file the link points into, or in the same file when the
            link is only a fragment; headings take GitHub's anchor names;
  outside   a relative link, or a path in backticks, that leaves the root;
  absolute  a link that starts with '/';
  path      a path in backticks that names nothing tracked. Only paths that
            start with ./ or ../, or whose first part is a top-level
            directory of the tree (src/, tools/, docs/, ...), are checked;
            they are looked up from the file's directory, then from the root.

Links with a scheme (https:, mailto:) are not followed, and a nested
project's files count as missing. When Git tracks nothing under --root (an
unpacked source tree), every file counts as tracked except, as the
checkout's ignore rules have it, those in build* and local/ directly under
the root, in any directory holding CMakeCache.txt, and in .git/ and
__pycache__/ anywhere. Fenced code blocks are skipped. A path in
backticks that holds a space, a wildcard or a placeholder (* < > { } $ ...)
is not checked, and a trailing :LINE or :FIRST-LAST is dropped.

KNOWN lists findings accepted for now as (file, check, target); it may only
shrink. An entry that no longer occurs is reported as stale, without
failing, so that it can be removed.

Each finding prints as '<path>:<line>: <check>: <target>'. Exit status is 1
when anything outside KNOWN is found. --self-test checks a small built-in
tree, as an unpacked source tree with a few accepted findings, and fails
unless exactly the expected findings, failures and stale entries are
reported.
"""
import argparse
import contextlib
import io
import os
import posixpath
import re
import subprocess
import sys
import tempfile
from pathlib import Path
from urllib.parse import unquote

ROOT = Path(__file__).resolve().parents[1]

# Findings accepted until the files that hold them are fixed: remove an
# entry once its link resolves.
KNOWN = set()

FENCE_RE = re.compile(r"^ {0,3}(`{3,}|~{3,})")
ATX_RE = re.compile(r"^ {0,3}(#{1,6})(?:[ \t]+(.*?))?(?:[ \t]+#+)?[ \t]*$")
SETEXT_RE = re.compile(r"^ {0,3}(?:=+|-+)[ \t]*$")
# A line that cannot be the text of a setext heading: a list item, quote,
# table row, heading or HTML line.
NOT_SETEXT_TEXT_RE = re.compile(r"^ {0,3}(?:[-*+>|#<]|\d+[.)])")
CODE_SPAN_RE = re.compile(r"(`+)(.+?)(?<!`)\1(?!`)")
INLINE_LINK_RE = re.compile(
    r"!?\[(?:[^\[\]]|\[[^\[\]]*\])*\]\(\s*(<[^>\n]*>|[^\s)]+)(?:\s+(?:\"[^\"]*\"|'[^']*'|\([^)]*\)))?\s*\)")
# A footnote definition ([^label]: text) holds text, not a target.
REFERENCE_RE = re.compile(r"^ {0,3}\[(?!\^)[^\]]+\]:\s*(<[^>]*>|\S+)")
HTML_ATTRIBUTE_RE = re.compile(r"\b(?:href|src)\s*=\s*(?:\"([^\"]*)\"|'([^']*)')", re.IGNORECASE)
HTML_ANCHOR_RE = re.compile(r"\b(?:name|id)\s*=\s*(?:\"([^\"]*)\"|'([^']*)')", re.IGNORECASE)
SCHEME_RE = re.compile(r"^[A-Za-z][A-Za-z0-9+.-]*:")
LINE_SUFFIX_RE = re.compile(r":\d+(?:-\d+)?$")
PLACEHOLDER_CHARS = set(" \t*?<>{}$|()[]\u2026")
# Directories a tree that Git does not track (an unpacked source tree) leaves
# out, as the checkout's ignore rules do: build trees and local dependencies
# directly under the root, and Git's and Python's own directories anywhere. A
# directory holding BUILD_TREE_MARKER is a build tree wherever it lies.
ROOT_UNTRACKED_DIR_RE = re.compile(r"^(?:build.*|local)$")
UNTRACKED_DIR_RE = re.compile(r"^(?:\.git|__pycache__)$")
BUILD_TREE_MARKER = "CMakeCache.txt"


def tracked(root):
    """Paths of the files Git tracks under root, or of every file when Git tracks none there."""
    try:
        listing = subprocess.run(["git", "-C", str(root), "ls-files", "-z", "--", "."], capture_output=True)
    except OSError:
        listing = None
    names = [name for name in listing.stdout.decode().split("\0") if name] \
        if listing is not None and listing.returncode == 0 else []
    if names:
        return names
    names = []
    for directory, subdirectories, files in os.walk(root):
        top = directory == os.fspath(root)
        subdirectories[:] = [name for name in subdirectories if not UNTRACKED_DIR_RE.match(name)
                             and not (top and ROOT_UNTRACKED_DIR_RE.match(name))
                             and not Path(directory, name, BUILD_TREE_MARKER).exists()]
        for name in files:
            names.append(Path(directory, name).relative_to(root).as_posix())
    return sorted(names)


def nested_projects(root, names):
    """Directory prefixes of the projects below root that build on the engine."""
    projects = []
    for name in names:
        if name.endswith("/CMakeLists.txt"):
            try:
                text = (root / name).read_text(errors="replace")
            except OSError:
                continue
            if re.search(r"(?im)^\s*project\s*\(", text) and re.search(r"\bOA_ENGINE_DIR\b", text):
                projects.append(name.rsplit("/", 1)[0] + "/")
    return tuple(sorted(projects))


def unfenced_lines(text):
    """The file's lines, with those inside fenced code blocks (fences included) blanked."""
    lines = text.split("\n")
    fence = None
    for index, line in enumerate(lines):
        match = FENCE_RE.match(line)
        if fence is None:
            if match:
                fence = match.group(1)
                lines[index] = ""
        else:
            if match and match.group(1)[0] == fence[0] and len(match.group(1)) >= len(fence) \
                    and not line.strip().strip(fence[0]):
                fence = None
            lines[index] = ""
    return lines


def slug(heading):
    """Returns the anchor name GitHub gives a heading's text."""
    text = CODE_SPAN_RE.sub(lambda match: match.group(2), heading)
    text = re.sub(r"!?\[([^\]]*)\]\([^)]*\)", r"\1", text)
    text = re.sub(r"<[^>]+>", "", text)
    text = re.sub(r"[^\w\- ]", "", text.strip().lower())
    return text.replace(" ", "-")


def anchors(lines):
    """Anchor names of a Markdown file's headings and HTML anchors, repeats numbered as GitHub does."""
    names = set()
    seen = {}

    def add(heading):
        name = slug(heading)
        count = seen.get(name, 0)
        seen[name] = count + 1
        names.add(name if count == 0 else f"{name}-{count}")

    previous = ""
    for line in lines:
        atx = ATX_RE.match(line)
        if atx:
            add(atx.group(2) or "")
        elif SETEXT_RE.match(line) and previous.strip() and not NOT_SETEXT_TEXT_RE.match(previous):
            add(previous)
        for match in HTML_ANCHOR_RE.finditer(line):
            names.add(match.group(1) if match.group(1) is not None else match.group(2))
        previous = line
    return names


class Tree:
    """The tracked files and directories of the checked tree, and the anchors of its Markdown files."""

    def __init__(self, root, names):
        self.root = root
        self.files = set(names)
        self.directories = {posixpath.dirname(name) for name in names}
        for directory in list(self.directories):
            while directory:
                directory = posixpath.dirname(directory)
                self.directories.add(directory)
        self.top_directories = {name.split("/", 1)[0] for name in names if "/" in name}
        self._anchors = {}

    def exists(self, path):
        """Tests whether a root-relative path names a tracked file or a directory holding one."""
        path = path.rstrip("/")
        return path in self.files or path in self.directories

    def anchors_of(self, path):
        """Anchor names of a tracked Markdown file, read once."""
        if path not in self._anchors:
            text = (self.root / path).read_text(encoding="utf-8", errors="replace")
            self._anchors[path] = anchors(unfenced_lines(text))
        return self._anchors[path]


def resolve(base, target):
    """Joins a relative target to a root-relative directory; None when the result leaves the root."""
    joined = posixpath.normpath(posixpath.join(base, target)) if base else posixpath.normpath(target)
    if joined == ".." or joined.startswith("../"):
        return None
    return "" if joined == "." else joined


def check_link(tree, name, target):
    """Returns the check a link target fails, or None when it resolves."""
    target = target.strip()
    if target.startswith("<") and target.endswith(">"):
        target = target[1:-1]
    if not target or SCHEME_RE.match(target) or target.startswith("//"):
        return None
    path, _, fragment = target.partition("#")
    path = unquote(path.split("?", 1)[0])
    if path.startswith("/"):
        return "absolute"
    if path:
        resolved = resolve(posixpath.dirname(name), path)
        if resolved is None:
            return "outside"
        if not tree.exists(resolved):
            return "link"
    else:
        resolved = name
    if fragment and resolved.endswith(".md") and resolved in tree.files:
        fragment = unquote(fragment)
        names = tree.anchors_of(resolved)
        if fragment not in names and fragment.lower() not in names:
            return "anchor"
    return None


def check_path(tree, name, token):
    """Returns the check a backticked repository path fails, or None when it resolves or is not checked."""
    token = LINE_SUFFIX_RE.sub("", token.strip())
    if not token or "..." in token or any(ch in PLACEHOLDER_CHARS for ch in token) or SCHEME_RE.match(token):
        return None
    base = posixpath.dirname(name)
    if token.startswith(("./", "../")):
        candidates = [resolve(base, token)]
    elif "/" in token and token.split("/", 1)[0] in tree.top_directories:
        candidates = [resolve(base, token), resolve("", token)]
    else:
        return None
    if all(candidate is None for candidate in candidates):
        return "outside"
    if any(candidate is not None and tree.exists(candidate) for candidate in candidates):
        return None
    return "path"


def findings_in(tree, name):
    """Yields (line, check, target) for every reference in one Markdown file that does not resolve."""
    text = (tree.root / name).read_text(encoding="utf-8", errors="replace")
    lines = unfenced_lines(text)
    spans = []
    # Code spans are blanked so that their text is never read as a link.
    blanked = []
    for number, line in enumerate(lines, 1):
        spans.extend((number, match.group(2)) for match in CODE_SPAN_RE.finditer(line))
        blanked.append(CODE_SPAN_RE.sub(lambda match: " " * len(match.group(0)), line))
    for number, line in enumerate(blanked, 1):
        targets = [match.group(1) for match in [REFERENCE_RE.match(line)] if match]
        targets += [match.group(1) if match.group(1) is not None else match.group(2)
                    for match in HTML_ATTRIBUTE_RE.finditer(line)]
        for target in targets:
            check = check_link(tree, name, target)
            if check:
                yield number, check, target
    # Inline links may wrap across lines.
    body = "\n".join(blanked)
    for match in INLINE_LINK_RE.finditer(body):
        check = check_link(tree, name, match.group(1))
        if check:
            yield body.count("\n", 0, match.start(1)) + 1, check, match.group(1)
    for number, token in spans:
        check = check_path(tree, name, token)
        if check:
            yield number, check, token.strip()


def collect(root, names):
    """Returns (file, line, check, target) for each finding in the Markdown files among names, in order."""
    projects = nested_projects(root, names)
    names = [name for name in names if not name.startswith(projects)]
    tree = Tree(root, names)
    found = []
    for name in sorted(name for name in names if name.lower().endswith(".md")):
        found.extend((name, number, kind, target) for number, kind, target in findings_in(tree, name))
    return sorted(found, key=lambda finding: finding[:2])


def check(root, known=KNOWN):
    """Prints every finding under root and each stale entry of known, and returns how many findings fall outside known."""
    failures = 0
    seen = set()
    for name, number, kind, target in collect(root, tracked(root)):
        key = (name, kind, target)
        seen.add(key)
        if key in known:
            continue
        print(f"{name}:{number}: {kind}: {target}")
        failures += 1
    for name, kind, target in sorted(known - seen):
        print(f"check_links: stale KNOWN entry, remove it: {name}: {kind}: {target}")
    return failures


# A small tree for --self-test and the findings the check must report in it.
# Git tracks none of it, so the check reads it as an unpacked source tree and
# leaves out its two build trees, but not a directory below the root whose
# name starts with build.
SELF_TEST_FILES = {
    "src/y.cpp": "",
    "docs/buildings/c.md": "[m](nothing4.md)\n",
    "build-x/n.md": "[m](missing.md)\n",
    "cmake-build-debug/CMakeCache.txt": "",
    "cmake-build-debug/ATTRIBUTIONS.md": "[l](licenses/x.txt)\n",
    "nested/CMakeLists.txt": "project(nested)\nset(OA_ENGINE_DIR ..)\n",
    "nested/n.md": "[unchecked](nothing.md)\n",
    "b.md": "# Title\n\n## Part two\n\nPart two\n---\n\n<a name=\"custom\"></a>\n",
    "src/mod/README.md": "`src/y.cpp` `include/oa/mod.hpp`\n",
    "a.md": (
        "# Head\n"
        "[x](b.md) [y](missing.md) [z](#head) [w](#nope)\n"
        "[v](b.md#part-two) [v1](b.md#part-two-1) [u](b.md#part-three) [c](b.md#custom)\n"
        "[t](../out.md) [s](/abs.md) [web](https://example.com/x) [dir](src/)\n"
        "`src/x.cpp` `src/y.cpp` `../sibling` `src/*.cpp` `src/y.cpp:12` `gamedata/x.tdf`\n"
        "```\n[a](nothing.md) `src/nothing.cpp`\n```\n"
        "[r]: nothing2.md\n"
        "<a href=\"nothing3.md\">x</a>\n"
        "[wrapped\ntext](b.md) and [wrapped\ntext two](missing2.md)\n"
        "`[not](a-link.md)` [n](nested/n.md)\n"
        "[^1]: See the manual\n"),
}
SELF_TEST_FINDINGS = [
    ("a.md", 2, "link", "missing.md"),
    ("a.md", 2, "anchor", "#nope"),
    ("a.md", 3, "anchor", "b.md#part-three"),
    ("a.md", 4, "outside", "../out.md"),
    ("a.md", 4, "absolute", "/abs.md"),
    ("a.md", 5, "path", "src/x.cpp"),
    ("a.md", 5, "outside", "../sibling"),
    ("a.md", 9, "link", "nothing2.md"),
    ("a.md", 10, "link", "nothing3.md"),
    ("a.md", 13, "link", "missing2.md"),
    ("a.md", 14, "link", "nested/n.md"),
    ("docs/buildings/c.md", 1, "link", "nothing4.md"),
]
# Accepted findings for the self-test: one that occurs and one stale entry.
SELF_TEST_KNOWN = {("a.md", "link", "missing.md"), ("a.md", "link", "gone.md")}
SELF_TEST_OUTPUT = [
    f"{name}:{number}: {kind}: {target}" for name, number, kind, target in SELF_TEST_FINDINGS
    if (name, kind, target) not in SELF_TEST_KNOWN
] + ["check_links: stale KNOWN entry, remove it: a.md: link: gone.md"]


def self_test():
    """Checks SELF_TEST_FILES as an unpacked tree and returns 0 when exactly SELF_TEST_FINDINGS are found and, with SELF_TEST_KNOWN accepted, exactly SELF_TEST_OUTPUT is printed, else 1."""
    with tempfile.TemporaryDirectory(prefix="oa-links-") as temporary:
        root = Path(temporary)
        for name, text in SELF_TEST_FILES.items():
            (root / name).parent.mkdir(parents=True, exist_ok=True)
            (root / name).write_text(text)
        found = collect(root, tracked(root))
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            failures = check(root, SELF_TEST_KNOWN)
    printed = output.getvalue().splitlines()
    if found == SELF_TEST_FINDINGS and printed == SELF_TEST_OUTPUT and failures == len(SELF_TEST_OUTPUT) - 1:
        print("check_links: self-test passed")
        return 0
    for finding in found:
        marker = " " if finding in SELF_TEST_FINDINGS else "+"
        print(f"{marker} {finding}")
    for finding in SELF_TEST_FINDINGS:
        if finding not in found:
            print(f"- {finding}")
    if printed != SELF_TEST_OUTPUT:
        print("printed with SELF_TEST_KNOWN accepted:", *printed, sep="\n  ")
    print(f"findings outside SELF_TEST_KNOWN: {failures}, expected {len(SELF_TEST_OUTPUT) - 1}")
    print("check_links: self-test failed (+ unexpected, - missing)")
    return 1


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--self-test", action="store_true", help="check a small built-in tree instead")
    args = parser.parse_args(argv)
    if args.self_test:
        return self_test()
    failures = check(args.root.resolve())
    if failures:
        print(f"check_links: {failures} finding(s)")
        return 1
    print("check_links: clean")
    return 0


if __name__ == "__main__":
    sys.exit(main())
