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
a pinned release's is refused. --self-test checks how the command line is
read.
"""
import argparse
import contextlib
import hashlib
import io
import pathlib
import re
import subprocess
import sys
import tarfile
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
DOWNLOAD_LIMIT = 64 * 1024 * 1024
# A release number as SDL's archives spell it. It becomes part of a URL and
# of folder names, so nothing else is taken.
VERSION_RE = re.compile(r"\d+\.\d+\.\d+")
SHA256_RE = re.compile(r"[0-9a-f]{64}")
# The exit status of a command line the script refuses.
USAGE_ERROR = 2
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
)
# The build and install folders each release gets under local/deps.
SELF_TEST_FOLDERS = {
    VERSION: ("sdl-build", "sdl-install"),
    "3.2.0": ("sdl-build-3.2.0", "sdl-install-3.2.0"),
    "3.3.0": ("sdl-build-3.3.0", "sdl-install-3.3.0"),
}


def fetch_archive(archive, url, sha256):
    """Download url to archive unless it exists; verify the SHA-256 either way."""
    if not archive.exists():
        print(f"Downloading {url}", flush=True)
        with urllib.request.urlopen(url, timeout=60) as response:
            data = response.read(DOWNLOAD_LIMIT + 1)
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


def sdl_source(deps, version=VERSION, sha256=None):
    """The verified, extracted source tree of one SDL release under deps.

    sha256 defaults to the release's pin in PINNED.
    """
    deps.mkdir(parents=True, exist_ok=True)
    archive = deps / f"SDL3-{version}.tar.gz"
    fetch_archive(archive, f"https://www.libsdl.org/release/{archive.name}", sha256 or PINNED[version])
    source = deps / f"SDL3-{version}"
    extract_source(archive, source)
    return source


def sdl_folders(deps, version=VERSION):
    """The build and install folders of one SDL release under deps.

    The engine's pin keeps sdl-build and sdl-install; any other release has
    folders of its own, named after it.
    """
    suffix = "" if version == VERSION else f"-{version}"
    return deps / f"sdl-build{suffix}", deps / f"sdl-install{suffix}"


def parse_arguments(argv=None):
    """Read the command line; return whether to run the self-test, the release to build and its archive's SHA-256.

    Exits with a usage message for a malformed release number or SHA-256,
    for a release PINNED does not hold given without --sha256, and for a
    --sha256 that differs from a pinned release's.
    """
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--version", default=VERSION,
                        help=f"the SDL release to build (default {VERSION}; pinned: {', '.join(PINNED)})")
    parser.add_argument("--sha256", help="the SHA-256 of the release's archive, needed for a release not pinned")
    parser.add_argument("--self-test", action="store_true", help="check how the command line is read, and exit")
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
    return args.self_test, args.version, sha256 or pinned


def self_test():
    """Check the pins, the command lines SELF_TEST_ARGUMENTS lists and each release's folders; return the exit status."""
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
                got = parse_arguments(argv)[1:]
            passed = got == expected
        except SystemExit as stop:
            got = f"a refusal (exit status {stop.code}): {errors.getvalue().strip()}"
            passed = isinstance(expected, str) and stop.code == USAGE_ERROR and expected in errors.getvalue()
        if not passed:
            failures.append(f"{' '.join(argv) or 'no arguments'} gives {got}, not {expected}")
    deps = pathlib.PurePosixPath("local/deps")
    for version, (build, install) in SELF_TEST_FOLDERS.items():
        if sdl_folders(deps, version) != (deps / build, deps / install):
            failures.append(f"SDL {version} builds in {sdl_folders(deps, version)}, not {build} and {install}")
    for failure in failures:
        print(f"bootstrap_sdl self-test: {failure}")
    if failures:
        return 1
    print(f"bootstrap_sdl self-test: {len(PINNED)} pins, {len(SELF_TEST_ARGUMENTS)} command lines and "
          f"{len(SELF_TEST_FOLDERS)} releases' folders as expected")
    return 0


def main(argv=None):
    run_self_test, version, sha256 = parse_arguments(argv)
    if run_self_test:
        return self_test()
    deps = ROOT / "local" / "deps"
    source = sdl_source(deps, version, sha256)
    build, install = sdl_folders(deps, version)
    subprocess.run(["cmake", "-S", str(source), "-B", str(build),
                    "-DCMAKE_BUILD_TYPE=Release", "-DSDL_SHARED=OFF", "-DSDL_STATIC=ON",
                    "-DSDL_TEST_LIBRARY=OFF", "-DSDL_TESTS=OFF",
                    f"-DCMAKE_PROJECT_INCLUDE={(ROOT / 'tools' / 'sdl_build_options.cmake').as_posix()}",
                    f"-DCMAKE_INSTALL_PREFIX={install}"], check=True)
    subprocess.run(["cmake", "--build", str(build), "--config", "Release", "--parallel", "8"], check=True)
    subprocess.run(["cmake", "--install", str(build), "--config", "Release"], check=True)
    print(f"SDL {version} ready: configure open-annihilation with -DCMAKE_PREFIX_PATH={install}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
