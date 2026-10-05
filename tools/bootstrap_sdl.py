#!/usr/bin/env python3
# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

"""Build a pinned SDL into local/deps without modifying system packages.

With no arguments it builds the engine's pinned release (3.4.16) into
local/deps/sdl-install. --version names another release this script pins,
such as 3.2.0, the oldest the build accepts; such a release gets source,
build and install folders of its own (local/deps/sdl-install-<version>),
so that it never replaces the engine's. A release this script does not pin
needs --sha256, the SHA-256 of its archive. Every archive is checked
against its SHA-256 before it is unpacked, and a --sha256 that differs from
a pinned release's is refused. --deps names another folder to build in
instead of local/deps, such as a scratch folder, and --jobs how many
compile jobs the build runs at once.

After unpacking a release, the patches in tools/sdl-patches/<release>
(tools/sdl-patches/README.md) are applied in the order of their names, by
this script's own unified-diff applier: every context and removed line
must match exactly at the line its hunk names. The source folder's
.oa-patches stamp then names each patch and its SHA-256. A source folder
whose stamp differs from the release's patches, or that has no stamp while
the release has patches, is removed and unpacked again from the verified
archive first, so a build never mixes patched and unpatched files. A
release with no patches folder is built as SDL published it. The macOS,
iOS and Windows bootstraps take their source from sdl_source, so they build
the same source.

--self-test checks how the command line is read, and the patches: that
each reads as a unified diff naming files of its release only, applies to
a tree made from its own lines and is refused a second time, and that the
stamp makes a changed source folder be unpacked again. It downloads
nothing; when the release's archive is already in the dependency folder,
it also applies the patches to the archive's own files, in memory.

Exit status: 0 when SDL was built (or the self-test passed); 1 when a
patch does not apply or the self-test fails; 2 for a command line it
refuses.
"""
import argparse
import contextlib
import dataclasses
import hashlib
import io
import pathlib
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import time
import urllib.error
import urllib.request

VERSION = "3.4.16"
SHA256 = "7322236cd12090c3eb40b9728be4d49c76f66ad17d04369584d4ecad5cf77c68"
# The SDL releases the bootstrap builds, with the SHA-256 of each one's
# archive: the engine's pin, and 3.2.0, the oldest release the build accepts
# (find_package(SDL3 3.2)), which CI also builds the engine against. Each
# digest is that of the SDL3-<version>.tar.gz SDL published with the
# release, which libsdl.org/release serves and package distributions pin.
PINNED = {
    VERSION: SHA256,
    "3.2.0": "bf308f92c5688b1479faf5cfe24af72f3cd4ce08d0c0670d6ce55bc2ec1e9a5e",
}
ROOT = pathlib.Path(__file__).resolve().parents[1]
DEFAULT_DEPS = ROOT / "local" / "deps"
# How many compile jobs the build runs at once unless --jobs says.
DEFAULT_JOBS = 8
DOWNLOAD_LIMIT = 64 * 1024 * 1024
# A download that fails with a rate limit (HTTP 429), a server error (5xx) or a
# lost connection is tried again: at most DOWNLOAD_ATTEMPTS times, waiting
# DOWNLOAD_WAITS seconds between them, or the server's Retry-After when it
# names at most DOWNLOAD_LONGEST_WAIT seconds.
DOWNLOAD_ATTEMPTS = 6
DOWNLOAD_WAITS = (5, 10, 20, 40, 60)
DOWNLOAD_LONGEST_WAIT = 120
# A release number as SDL's archives spell it. It becomes part of a URL and
# of folder names, so nothing else is taken.
VERSION_RE = re.compile(r"\d+\.\d+\.\d+")
SHA256_RE = re.compile(r"[0-9a-f]{64}")
# The exit status of a command line the script refuses.
USAGE_ERROR = 2
# The folder of each release's patches (tools/sdl-patches/<release>), the
# suffix of a patch file, the stamp a patched source folder holds, and
# bounds on a patch file and on a file a patch changes.
PATCHES = ROOT / "tools" / "sdl-patches"
PATCH_SUFFIX = ".patch"
STAMP_NAME = ".oa-patches"
MAX_PATCH_BYTES = 1 << 20
MAX_PATCHED_FILE_BYTES = 16 << 20
# What the explanation before a patch's first file must say: that the
# change is offered upstream.
UPSTREAM_WORD = "upstream"
# A file header's two lines, and a hunk header: the old and new start lines,
# each with its line count, which is 1 when left out.
OLD_FILE_PREFIX = "--- "
NEW_FILE_PREFIX = "+++ "
HUNK_RE = re.compile(r"@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@")
NO_NEWLINE_MARK = "\\ No newline at end of file"
# A path a patch may change: relative, with no '..', no backslash and no
# empty part, under the release's source folder.
PATCH_PATH_RE = re.compile(r"(?:[A-Za-z0-9_.+-]+/)*[A-Za-z0-9_.+-]+")
# Command lines and what parse_arguments makes of each: the release and
# SHA-256 it builds, or part of the reason it refuses the command line.
SELF_TEST_ARGUMENTS = (
    ([], (VERSION, SHA256)),
    (["--version", "3.2.0"], ("3.2.0", PINNED["3.2.0"])),
    (["--version", "3.2.0", "--sha256", PINNED["3.2.0"].upper()], ("3.2.0", PINNED["3.2.0"])),
    (["--version", "3.3.0", "--sha256", "AB" * 32], ("3.3.0", "ab" * 32)),
    (["--version", "3.3.0"], "is not pinned"),
    (["--version", "3.2.0", "--sha256", "0" * 64], "differs from the pin"),
    (["--version", "3.2"], "is not a release number"),
    (["--version", "../3.2.0"], "is not a release number"),
    (["--version", "3.2.0-rc1"], "is not a release number"),
    (["--sha256", "0" * 63], "64 hexadecimal digits"),
    (["--version", "3.3.0", "--sha256", "g" * 64], "64 hexadecimal digits"),
    (["--jobs", "0"], "at least 1"),
)
# The dependency folder and compile jobs command lines give.
SELF_TEST_DEPS = (
    ([], DEFAULT_DEPS, DEFAULT_JOBS),
    (["--deps", "scratch/deps", "--jobs", "3"], pathlib.Path("scratch/deps"), 3),
)
# The build and install folders each release gets under local/deps.
SELF_TEST_FOLDERS = {
    VERSION: ("sdl-build", "sdl-install"),
    "3.2.0": ("sdl-build-3.2.0", "sdl-install-3.2.0"),
    "3.3.0": ("sdl-build-3.3.0", "sdl-install-3.3.0"),
}
# The release the self-test's synthetic patches belong to, the lines its
# made-up files hold before and after its hunks, and patch texts the reader
# must refuse, each with part of the reason.
SELF_TEST_RELEASE = "9.8.7"
SELF_TEST_FILLER = "filler line {}"
SELF_TEST_TRAILING_LINES = 3
SELF_TEST_BAD_PATCHES = (
    ("no explanation\n", "names no file"),
    ("x\n--- a/src/a.c\n+++ b/src/b.c\n@@ -1 +1 @@\n-a\n+b\n", "names two files"),
    ("x\n--- a/../a.c\n+++ b/../a.c\n@@ -1 +1 @@\n-a\n+b\n", "is not a path"),
    ("x\n--- /dev/null\n+++ b/src/a.c\n@@ -0,0 +1 @@\n+b\n", "is not a path"),
    ("x\n--- src/a.c\n+++ src/a.c\n@@ -1 +1 @@\n-a\n+b\n", "a/ and b/"),
    ("x\n--- a/src/a.c\n+++ b/src/a.c\n@@ -1,2 +1 @@\n-a\n+b\n", "ends early"),
    ("x\n--- a/src/a.c\n+++ b/src/a.c\n@@ -1 +1 @@\n*a\n", "unexpected line"),
    ("x\n--- a/src/a.c\n+++ b/src/a.c\n", "no hunk"),
    ("x\n--- a/src/a.c\n+++ b/src/a.c\n@@ -5 +5 @@\n-a\n+b\n@@ -2 +2 @@\n-c\n+d\n", "overlaps"),
)


class PatchError(Exception):
    """A patch that cannot be read, or does not apply to the source."""


@dataclasses.dataclass
class Hunk:
    """One hunk of a unified diff."""
    old_start: int = 0  # the first old line it covers, counted from 1 (0 for an empty file)
    old_count: int = 0
    new_start: int = 0
    new_count: int = 0
    lines: list = dataclasses.field(default_factory=list)  # (' ', '-' or '+', the line without its end)
    old_ends_file: bool = False  # its last old line has no line end
    new_ends_file: bool = False  # its last new line has no line end


@dataclasses.dataclass
class FilePatch:
    """The hunks a patch makes to one file."""
    path: str = ""  # relative to the release's source folder
    hunks: list = dataclasses.field(default_factory=list)


def download(url):
    """Read url, at most DOWNLOAD_LIMIT + 1 bytes, trying again after a passing failure."""
    for attempt in range(DOWNLOAD_ATTEMPTS):
        try:
            with urllib.request.urlopen(url, timeout=60) as response:
                return response.read(DOWNLOAD_LIMIT + 1)
        except urllib.error.HTTPError as error:
            if error.code != 429 and error.code < 500 or attempt + 1 == DOWNLOAD_ATTEMPTS:
                raise
            asked = error.headers.get("Retry-After", "") if error.headers else ""
            wait = int(asked) if asked.isdigit() and int(asked) <= DOWNLOAD_LONGEST_WAIT else DOWNLOAD_WAITS[attempt]
            reason = f"HTTP {error.code}"
        except (urllib.error.URLError, TimeoutError, ConnectionError) as error:
            if attempt + 1 == DOWNLOAD_ATTEMPTS:
                raise
            wait = DOWNLOAD_WAITS[attempt]
            reason = str(error)
        print(f"Download failed ({reason}); trying again in {wait} s", flush=True)
        time.sleep(wait)
    raise RuntimeError(f"{url} could not be downloaded")


def fetch_archive(archive, url, sha256):
    """Download url to archive unless it exists; verify the SHA-256 either way."""
    if not archive.exists():
        print(f"Downloading {url}", flush=True)
        data = download(url)
        if len(data) > DOWNLOAD_LIMIT:
            raise RuntimeError(f"{archive.name} exceeds download limit")
        if hashlib.sha256(data).hexdigest() != sha256:
            raise RuntimeError(f"{archive.name} SHA-256 mismatch")
        archive.write_bytes(data)
    if hashlib.sha256(archive.read_bytes()).hexdigest() != sha256:
        raise RuntimeError(f"{archive.name} SHA-256 mismatch; remove the local archive and retry")


def extract_source(archive, source):
    """Unpack archive next to source (its top-level directory) unless present."""
    if not source.exists():
        with tarfile.open(archive, "r:gz") as bundle:
            # Python releases without extraction filters (before 3.11.4,
            # 3.10.12 and 3.9.17) unpack every member as the archive gives
            # it; the archive's SHA-256 is pinned, so its members are the
            # release's own.
            if hasattr(tarfile, "data_filter"):
                bundle.extractall(source.parent, filter="data")
            else:
                bundle.extractall(source.parent)


def patch_files(version, patches=PATCHES):
    """Lists the patches of one release, in the order they apply.

    @param version the release, such as 3.4.16
    @param patches the folder that holds a folder of patches per release
    @return the patch files, by name; none for a release without a folder
    """
    folder = patches / version
    if not folder.is_dir():
        return []
    return sorted((path for path in folder.iterdir() if path.suffix == PATCH_SUFFIX and path.is_file()),
                  key=lambda path: path.name)


def read_patch_file(path):
    """Reads a patch file of at most MAX_PATCH_BYTES as text.

    @param path the patch
    @return its text
    """
    if path.stat().st_size > MAX_PATCH_BYTES:
        raise PatchError(f"{path.name}: larger than {MAX_PATCH_BYTES} bytes")
    try:
        return path.read_bytes().decode("utf-8")
    except UnicodeDecodeError as error:
        raise PatchError(f"{path.name}: not UTF-8 ({error})") from error


def patch_stamp(version, patches=PATCHES):
    """Gives the stamp a source folder patched with one release's patches holds.

    @param version the release
    @param patches the folder that holds a folder of patches per release
    @return one line per patch, its SHA-256 and name; None when the release has no patches
    """
    files = patch_files(version, patches)
    if not files:
        return None
    return "".join(f"{hashlib.sha256(path.read_bytes()).hexdigest()}  {path.name}\n" for path in files)


def patch_path(text, prefix, name, number):
    """Takes the path from a file header line.

    @param text the line after its '--- ' or '+++ '
    @param prefix the prefix the path must have, a/ or b/
    @param name the patch's name, for messages
    @param number the line's number, for messages
    @return the path, relative to the release's source folder
    """
    path = text.split("\t", 1)[0].rstrip()
    if not path.startswith(prefix):
        if path == "/dev/null" or path.startswith("/") or ".." in path.split("/"):
            raise PatchError(f"{name}:{number}: {path} is not a path in the release")
        raise PatchError(f"{name}:{number}: {path} lacks the a/ and b/ before it")
    path = path[len(prefix):]
    if not PATCH_PATH_RE.fullmatch(path) or ".." in path.split("/"):
        raise PatchError(f"{name}:{number}: {path} is not a path in the release")
    return path


def parse_patch(text, name):
    """Reads a unified diff.

    The text before the first file header ('--- ' then '+++ ') explains the
    patch. Each file needs a/ and b/ before its paths, which must be the
    same, and at least one hunk; hunks must come in the order of their
    lines and not overlap. A file a patch adds or removes is refused.

    @param text the patch
    @param name its name, for messages
    @return (the explanation, the FilePatch of each file)
    """
    lines = text.split("\n")
    if lines and lines[-1] == "":
        lines.pop()
    lines = [line[:-1] if line.endswith("\r") else line for line in lines]
    index = 0
    while index < len(lines) and not (lines[index].startswith(OLD_FILE_PREFIX) and index + 1 < len(lines)
                                      and lines[index + 1].startswith(NEW_FILE_PREFIX)):
        index += 1
    explanation = "\n".join(lines[:index]).strip()
    if index >= len(lines):
        raise PatchError(f"{name}: names no file to change")
    files = []
    while index < len(lines):
        if lines[index] == "":
            index += 1
            continue
        if not (lines[index].startswith(OLD_FILE_PREFIX) and index + 1 < len(lines)
                and lines[index + 1].startswith(NEW_FILE_PREFIX)):
            raise PatchError(f"{name}:{index + 1}: unexpected line {lines[index]!r}")
        old = patch_path(lines[index][len(OLD_FILE_PREFIX):], "a/", name, index + 1)
        new = patch_path(lines[index + 1][len(NEW_FILE_PREFIX):], "b/", name, index + 2)
        if old != new:
            raise PatchError(f"{name}:{index + 1}: names two files, {old} and {new}")
        file_patch = FilePatch(path=old)
        index += 2
        while index < len(lines) and lines[index].startswith("@@"):
            match = HUNK_RE.match(lines[index])
            if not match:
                raise PatchError(f"{name}:{index + 1}: unreadable hunk header {lines[index]!r}")
            hunk = Hunk(old_start=int(match[1]), old_count=int(match[2] or 1),
                        new_start=int(match[3]), new_count=int(match[4] or 1))
            header_line = index + 1
            index += 1
            old_seen = new_seen = 0
            while old_seen < hunk.old_count or new_seen < hunk.new_count:
                if index >= len(lines):
                    raise PatchError(f"{name}:{header_line}: the hunk ends early")
                line = lines[index]
                kind, body = (line[:1], line[1:]) if line else (" ", "")
                if kind == "\\":
                    raise PatchError(f"{name}:{index + 1}: misplaced {line!r}")
                if kind not in " -+":
                    if line.startswith(("@@", OLD_FILE_PREFIX)):
                        raise PatchError(f"{name}:{header_line}: the hunk ends early")
                    raise PatchError(f"{name}:{index + 1}: unexpected line {line!r}")
                hunk.lines.append((kind, body))
                old_seen += kind in " -"
                new_seen += kind in " +"
                if old_seen > hunk.old_count or new_seen > hunk.new_count:
                    raise PatchError(f"{name}:{header_line}: the hunk holds more lines than its header says")
                index += 1
                if index < len(lines) and lines[index] == NO_NEWLINE_MARK:
                    hunk.old_ends_file |= kind in " -"
                    hunk.new_ends_file |= kind in " +"
                    index += 1
            if hunk.old_count == 0 and hunk.old_start == 0 or hunk.new_count == 0 and hunk.new_start == 0:
                raise PatchError(f"{name}:{header_line}: adds or removes a whole file")
            previous = file_patch.hunks[-1] if file_patch.hunks else None
            if previous and hunk.old_start < previous.old_start + previous.old_count:
                raise PatchError(f"{name}:{header_line}: the hunk overlaps the one before it")
            file_patch.hunks.append(hunk)
        if not file_patch.hunks:
            raise PatchError(f"{name}: {file_patch.path} has no hunk")
        if any(earlier.path == file_patch.path for earlier in files):
            raise PatchError(f"{name}: changes {file_patch.path} twice")
        files.append(file_patch)
    return explanation, files


def apply_hunks(data, file_patch, name):
    """Applies one file's hunks to its bytes, exactly.

    Each hunk's context and removed lines must equal the file's lines at the
    line the hunk names in the unchanged file; there is no fuzz. Line ends
    are kept as the file has them.

    @param data the file's bytes, UTF-8
    @param file_patch the hunks
    @param name the patch's name, for messages
    @return the changed file's bytes
    """
    try:
        text = data.decode("utf-8")
    except UnicodeDecodeError as error:
        raise PatchError(f"{name}: {file_patch.path} is not UTF-8 ({error})") from error
    parts = text.split("\n")
    lines = [part + "\n" for part in parts[:-1]] + ([parts[-1]] if parts[-1] else [])
    ending = "\r\n" if lines and lines[0].endswith("\r\n") else "\n"
    result = []
    taken = 0  # lines of the old file already copied or replaced
    for hunk in file_patch.hunks:
        start = hunk.old_start - 1 if hunk.old_count else hunk.old_start
        if start < taken or start > len(lines):
            raise PatchError(f"{name}: {file_patch.path}: the hunk at line {hunk.old_start} lies outside the file")
        result.extend(lines[taken:start])
        at = start
        old_lines = [body for kind, body in hunk.lines if kind in " -"]
        for offset, wanted in enumerate(old_lines):
            if at + offset >= len(lines):
                raise PatchError(f"{name}: {file_patch.path}:{at + offset + 1}: the file ends before the hunk")
            got = lines[at + offset]
            got_text = got.rstrip("\n").removesuffix("\r") if got.endswith("\n") else got
            if got_text != wanted:
                raise PatchError(f"{name}: {file_patch.path}:{at + offset + 1}: expected {wanted!r}, found "
                                 f"{got_text!r}; the patch does not apply to this source")
        last_old = at + len(old_lines)
        if hunk.old_ends_file and (last_old != len(lines) or lines[-1].endswith("\n")):
            raise PatchError(f"{name}: {file_patch.path}: the hunk expects the file to end without a line end")
        new_lines = [body for kind, body in hunk.lines if kind in " +"]
        for offset, body in enumerate(new_lines):
            final = offset == len(new_lines) - 1 and hunk.new_ends_file
            result.append(body if final else body + ending)
        taken = last_old
    result.extend(lines[taken:])
    return "".join(result).encode("utf-8")


def apply_patches(source, version, patches=PATCHES):
    """Applies one release's patches to its unpacked source and writes the stamp.

    Every patch is read and applied in memory first, in name order, and the
    files are written only when all of them apply.

    @param source the release's unpacked source folder
    @param version the release
    @param patches the folder that holds a folder of patches per release
    @return the names of the patches applied; none for a release without patches
    """
    files = patch_files(version, patches)
    if not files:
        return []
    changed = {}
    for patch in files:
        _, file_patches = parse_patch(read_patch_file(patch), patch.name)
        for file_patch in file_patches:
            target = source / file_patch.path
            if file_patch.path not in changed:
                if not target.is_file():
                    raise PatchError(f"{patch.name}: {file_patch.path} is not a file of SDL {version}")
                if target.stat().st_size > MAX_PATCHED_FILE_BYTES:
                    raise PatchError(f"{patch.name}: {file_patch.path} is larger than {MAX_PATCHED_FILE_BYTES} bytes")
                changed[file_patch.path] = target.read_bytes()
            changed[file_patch.path] = apply_hunks(changed[file_patch.path], file_patch, patch.name)
    for path, data in changed.items():
        (source / path).write_bytes(data)
    (source / STAMP_NAME).write_text(patch_stamp(version, patches), encoding="utf-8")
    return [patch.name for patch in files]


def read_stamp(source):
    """Reads a source folder's patch stamp.

    @param source the unpacked source folder
    @return the stamp's text; None when the folder has none
    """
    try:
        return (source / STAMP_NAME).read_text(encoding="utf-8")
    except FileNotFoundError:
        return None


def sdl_source(deps, version=VERSION, sha256=None, patches=PATCHES):
    """The verified, extracted and patched source tree of one SDL release under deps.

    sha256 defaults to the release's pin in PINNED. A source folder whose
    patch stamp is not the release's patch set is removed and unpacked
    again from the verified archive, then patched.
    """
    deps.mkdir(parents=True, exist_ok=True)
    archive = deps / f"SDL3-{version}.tar.gz"
    fetch_archive(archive, f"https://www.libsdl.org/release/{archive.name}", sha256 or PINNED[version])
    source = deps / f"SDL3-{version}"
    wanted = patch_stamp(version, patches)
    if source.exists() and read_stamp(source) != wanted:
        print(f"{source} does not hold SDL {version} with this patch set; unpacking it again", flush=True)
        shutil.rmtree(source)
    if not source.exists():
        extract_source(archive, source)
        for name in apply_patches(source, version, patches):
            print(f"Applied {name} to SDL {version}", flush=True)
    return source


def sdl_folders(deps, version=VERSION):
    """The build and install folders of one SDL release under deps.

    The engine's pin keeps sdl-build and sdl-install; any other release has
    folders of its own, named after it.
    """
    suffix = "" if version == VERSION else f"-{version}"
    return deps / f"sdl-build{suffix}", deps / f"sdl-install{suffix}"


def parse_arguments(argv=None):
    """Read the command line.

    Exits with a usage message for a malformed release number or SHA-256,
    for a release PINNED does not hold given without --sha256, for a
    --sha256 that differs from a pinned release's, and for fewer than one
    compile job.

    @param argv the arguments; None for the process's own
    @return (whether to run the self-test, the release to build, its archive's SHA-256, the dependency
            folder, the compile jobs)
    """
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--version", default=VERSION,
                        help=f"the SDL release to build (default {VERSION}; pinned: {', '.join(PINNED)})")
    parser.add_argument("--sha256", help="the SHA-256 of the release's archive, needed for a release not pinned")
    parser.add_argument("--deps", type=pathlib.Path, default=DEFAULT_DEPS,
                        help="the folder to download, unpack, build and install in (default local/deps)")
    parser.add_argument("--jobs", type=int, default=DEFAULT_JOBS,
                        help=f"how many compile jobs to run at once (default {DEFAULT_JOBS})")
    parser.add_argument("--self-test", action="store_true",
                        help="check how the command line is read and the patches, and exit")
    args = parser.parse_args(argv)
    if not VERSION_RE.fullmatch(args.version):
        parser.error(f"--version {args.version} is not a release number such as {VERSION}")
    sha256 = args.sha256.lower() if args.sha256 else None
    if sha256 is not None and not SHA256_RE.fullmatch(sha256):
        parser.error("--sha256 takes 64 hexadecimal digits")
    pinned = PINNED.get(args.version)
    if pinned is None and sha256 is None:
        parser.error(f"SDL {args.version} is not pinned; give the SHA-256 of its archive with --sha256")
    if pinned is not None and sha256 is not None and sha256 != pinned:
        parser.error(f"--sha256 differs from the pin of SDL {args.version}, {pinned}")
    if args.jobs < 1:
        parser.error("--jobs takes at least 1")
    return args.self_test, args.version, sha256 or pinned, args.deps, args.jobs


def synthetic_files(file_patch):
    """Makes the texts of a file before and after a patch, from the patch's own lines.

    Filler lines, numbered by their line in the unchanged file, stand for
    the lines the hunks do not show, so that each hunk lies at the line its
    header names.

    @param file_patch the hunks
    @return (the file before, the file after, a problem with the hunks' new line numbers or None)
    """
    before, after = [], []
    old_line = 1  # the line of the unchanged file the next filler stands for
    problem = None
    for hunk in file_patch.hunks:
        first_old = hunk.old_start if hunk.old_count else hunk.old_start + 1
        while old_line < first_old:
            filler = SELF_TEST_FILLER.format(old_line)
            before.append(filler)
            after.append(filler)
            old_line += 1
        first_new = hunk.new_start if hunk.new_count else hunk.new_start + 1
        if len(after) + 1 != first_new and problem is None:
            problem = f"the hunk at old line {hunk.old_start} says new line {hunk.new_start}, not {len(after) + 1}"
        before.extend(body for kind, body in hunk.lines if kind in " -")
        after.extend(body for kind, body in hunk.lines if kind in " +")
        old_line += hunk.old_count
    for _ in range(SELF_TEST_TRAILING_LINES):
        filler = SELF_TEST_FILLER.format(old_line)
        before.append(filler)
        after.append(filler)
        old_line += 1
    return "\n".join(before) + "\n", "\n".join(after) + "\n", problem


def check_patch_set(version, patches, failures):
    """Checks one release's real patches: they read, explain themselves and apply to trees made from them.

    @param version the release
    @param patches the folder that holds a folder of patches per release
    @param[out] failures receives a line per problem
    @return the FilePatch lists, by patch name
    """
    read = {}
    for patch in patch_files(version, patches):
        try:
            explanation, file_patches = parse_patch(read_patch_file(patch), patch.name)
        except PatchError as error:
            failures.append(str(error))
            continue
        if UPSTREAM_WORD not in explanation.lower():
            failures.append(f"{patch.name}: its explanation does not say it is offered upstream")
        if not explanation:
            failures.append(f"{patch.name}: nothing before its first file says what it does")
        for file_patch in file_patches:
            before, after, problem = synthetic_files(file_patch)
            if problem:
                failures.append(f"{patch.name}: {file_patch.path}: {problem}")
                continue
            before, after = before.encode("utf-8"), after.encode("utf-8")
            try:
                if apply_hunks(before, file_patch, patch.name) != after:
                    failures.append(f"{patch.name}: {file_patch.path} does not come out as the patch's new lines")
                    continue
            except PatchError as error:
                failures.append(f"{patch.name}: does not apply to a tree made from its own lines: {error}")
                continue
            try:
                apply_hunks(after, file_patch, patch.name)
                failures.append(f"{patch.name}: {file_patch.path} took the patch a second time")
            except PatchError:
                pass
        read[patch.name] = file_patches
    return read


def check_against_archive(version, deps, read, failures):
    """Applies a release's patches to the files of its archive, in memory, when the archive is at hand.

    @param version the release
    @param deps the dependency folder that may hold the release's archive
    @param read the release's FilePatch lists, by patch name
    @param failures receives a line per problem
    @return a line saying what was checked
    """
    archive = deps / f"SDL3-{version}.tar.gz"
    if not read:
        return f"SDL {version}: no patches"
    if not archive.is_file() or archive.stat().st_size > DOWNLOAD_LIMIT:
        return f"SDL {version}: archive not at hand, its files not checked"
    data = archive.read_bytes()
    if hashlib.sha256(data).hexdigest() != PINNED.get(version):
        return f"SDL {version}: the archive at hand is not the pinned one, its files not checked"
    wanted = {file_patch.path for file_patches in read.values() for file_patch in file_patches}
    found = {}
    with tarfile.open(fileobj=io.BytesIO(data), mode="r:gz") as bundle:
        for member in bundle:
            path = member.name.split("/", 1)[1] if "/" in member.name else ""
            if path in wanted and member.isfile():
                found[path] = bundle.extractfile(member).read()
    for name, file_patches in read.items():
        for file_patch in file_patches:
            if file_patch.path not in found:
                failures.append(f"{name}: {file_patch.path} is not a file of SDL {version}")
                continue
            try:
                found[file_patch.path] = apply_hunks(found[file_patch.path], file_patch, name)
            except PatchError as error:
                failures.append(f"{name}: does not apply to SDL {version}: {error}")
    return f"SDL {version}: the patches apply to the archive's files"


def check_source_folder(failures):
    """Checks sdl_source's stamp rule on a made-up release with a made-up patch, offline.

    @param failures receives a line per problem
    """
    original = "int value(void)\n{\n    return 1;\n}\n"
    patched = "int value(void)\n{\n    return 2;\n}\n"
    patch_text = ("Changes the value; offered upstream.\n\n--- a/src/value.c\n+++ b/src/value.c\n"
                  "@@ -2,3 +2,3 @@\n {\n-    return 1;\n+    return 2;\n }\n")
    with tempfile.TemporaryDirectory() as directory:
        root = pathlib.Path(directory)
        deps = root / "deps"
        patches = root / "patches"
        (patches / SELF_TEST_RELEASE).mkdir(parents=True)
        (patches / SELF_TEST_RELEASE / f"0001-value{PATCH_SUFFIX}").write_text(patch_text, encoding="utf-8")
        deps.mkdir()
        archive = deps / f"SDL3-{SELF_TEST_RELEASE}.tar.gz"
        member = f"SDL3-{SELF_TEST_RELEASE}/src/value.c"
        with tarfile.open(archive, "w:gz") as bundle:
            info = tarfile.TarInfo(member)
            info.size = len(original)
            bundle.addfile(info, io.BytesIO(original.encode("utf-8")))
        sha256 = hashlib.sha256(archive.read_bytes()).hexdigest()
        with contextlib.redirect_stdout(io.StringIO()):
            source = sdl_source(deps, SELF_TEST_RELEASE, sha256, patches)
            value = source / "src" / "value.c"
            if value.read_text(encoding="utf-8") != patched:
                failures.append("a fresh source folder was not patched")
            if read_stamp(source) != patch_stamp(SELF_TEST_RELEASE, patches):
                failures.append("a fresh source folder holds no stamp of its patches")
            try:
                apply_patches(source, SELF_TEST_RELEASE, patches)
                failures.append("a patched source folder took its patches a second time")
            except PatchError:
                pass
            marker = source / "kept"
            marker.write_text("kept\n", encoding="utf-8")
            sdl_source(deps, SELF_TEST_RELEASE, sha256, patches)
            if not marker.exists():
                failures.append("a source folder whose stamp matches was unpacked again")
            (source / STAMP_NAME).write_text("another patch set\n", encoding="utf-8")
            sdl_source(deps, SELF_TEST_RELEASE, sha256, patches)
            if marker.exists() or value.read_text(encoding="utf-8") != patched:
                failures.append("a source folder whose stamp differs was not unpacked and patched again")
            (source / STAMP_NAME).unlink()
            marker.write_text("kept\n", encoding="utf-8")
            sdl_source(deps, SELF_TEST_RELEASE, sha256, patches)
            if marker.exists() or read_stamp(source) is None:
                failures.append("a source folder without a stamp was not unpacked and patched again")
            for patch in (patches / SELF_TEST_RELEASE).iterdir():
                patch.unlink()
            sdl_source(deps, SELF_TEST_RELEASE, sha256, patches)
            if value.read_text(encoding="utf-8") != original or read_stamp(source) is not None:
                failures.append("a release whose patches were removed kept the patched source")
            (patches / SELF_TEST_RELEASE / f"0001-value{PATCH_SUFFIX}").write_text(
                patch_text.replace("return 1;\n+", "return 3;\n+"), encoding="utf-8")
            shutil.rmtree(source)
            try:
                sdl_source(deps, SELF_TEST_RELEASE, sha256, patches)
                failures.append("a patch that does not match the source was applied")
            except PatchError:
                if read_stamp(source) is not None:
                    failures.append("a source folder whose patch failed holds a stamp")
                elif value.read_text(encoding="utf-8") != original:
                    failures.append("a patch that failed left a changed file")


def self_test(deps=DEFAULT_DEPS):
    """Check the pins, the command lines, each release's folders and the patches; return the exit status.

    @param deps the dependency folder whose archives, when present, the patches are tried on
    @return 0 when every check passes, else 1
    """
    failures = []
    for version, sha256 in PINNED.items():
        if not VERSION_RE.fullmatch(version) or not SHA256_RE.fullmatch(sha256):
            failures.append(f"the pin of SDL {version} is malformed")
    if f"({VERSION})" not in __doc__:
        failures.append(f"the help does not name the engine's pinned release, {VERSION}")
    for argv, expected in SELF_TEST_ARGUMENTS:
        errors = io.StringIO()
        try:
            with contextlib.redirect_stderr(errors):
                got = parse_arguments(argv)[1:3]
            passed = got == expected
        except SystemExit as stop:
            got = f"a refusal (exit status {stop.code}): {errors.getvalue().strip()}"
            passed = isinstance(expected, str) and stop.code == USAGE_ERROR and expected in errors.getvalue()
        if not passed:
            failures.append(f"{' '.join(argv) or 'no arguments'} gives {got}, not {expected}")
    for argv, expected_deps, expected_jobs in SELF_TEST_DEPS:
        got = parse_arguments(argv)[3:]
        if got != (expected_deps, expected_jobs):
            failures.append(f"{' '.join(argv) or 'no arguments'} builds in {got}, not {(expected_deps, expected_jobs)}")
    folders = pathlib.PurePosixPath("local/deps")
    for version, (build, install) in SELF_TEST_FOLDERS.items():
        if sdl_folders(folders, version) != (folders / build, folders / install):
            failures.append(f"SDL {version} builds in {sdl_folders(folders, version)}, not {build} and {install}")
    for text, expected in SELF_TEST_BAD_PATCHES:
        try:
            parse_patch(text, "bad.patch")
            failures.append(f"the patch {text!r} was read, not refused for '{expected}'")
        except PatchError as error:
            if expected not in str(error):
                failures.append(f"the patch {text!r} was refused with '{error}', not '{expected}'")
    for folder in (sorted(path for path in PATCHES.iterdir() if path.is_dir()) if PATCHES.is_dir() else []):
        if folder.name not in PINNED:
            failures.append(f"tools/sdl-patches/{folder.name} names no pinned release")
        stray = [path.name for path in folder.iterdir() if path.suffix != PATCH_SUFFIX]
        if stray:
            failures.append(f"tools/sdl-patches/{folder.name} holds files that are not patches: {stray}")
    archive_lines = []
    patch_count = 0
    for version in PINNED:
        read = check_patch_set(version, PATCHES, failures)
        patch_count += len(read)
        archive_lines.append(check_against_archive(version, deps, read, failures))
    check_source_folder(failures)
    for failure in failures:
        print(f"bootstrap_sdl self-test: {failure}")
    if failures:
        return 1
    print(f"bootstrap_sdl self-test: {len(PINNED)} pins, {len(SELF_TEST_ARGUMENTS) + len(SELF_TEST_DEPS)} command "
          f"lines, {len(SELF_TEST_FOLDERS)} releases' folders, {len(SELF_TEST_BAD_PATCHES)} refused patches, "
          f"{patch_count} patches and the source folder's stamp as expected")
    for line in archive_lines:
        print(f"bootstrap_sdl self-test: {line}")
    return 0


def main(argv=None):
    run_self_test, version, sha256, deps, jobs = parse_arguments(argv)
    if run_self_test:
        return self_test(deps)
    deps = deps.resolve()
    try:
        source = sdl_source(deps, version, sha256)
    except PatchError as error:
        print(f"bootstrap_sdl: {error}", file=sys.stderr)
        return 1
    build, install = sdl_folders(deps, version)
    subprocess.run(["cmake", "-S", str(source), "-B", str(build),
                    "-DCMAKE_BUILD_TYPE=Release", "-DSDL_SHARED=OFF", "-DSDL_STATIC=ON",
                    "-DSDL_TEST_LIBRARY=OFF", "-DSDL_TESTS=OFF",
                    f"-DCMAKE_PROJECT_INCLUDE={(ROOT / 'tools' / 'sdl_build_options.cmake').as_posix()}",
                    f"-DCMAKE_INSTALL_PREFIX={install}"], check=True)
    subprocess.run(["cmake", "--build", str(build), "--config", "Release", "--parallel", str(jobs)], check=True)
    subprocess.run(["cmake", "--install", str(build), "--config", "Release"], check=True)
    print(f"SDL {version} ready: configure open-annihilation with -DCMAKE_PREFIX_PATH={install}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
