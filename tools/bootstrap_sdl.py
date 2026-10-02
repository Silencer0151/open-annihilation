#!/usr/bin/env python3
# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

"""Build a pinned SDL into local/deps without modifying system packages."""
import hashlib
import pathlib
import subprocess
import tarfile
import urllib.request

VERSION = "3.4.16"
SHA256 = "7322236cd12090c3eb40b9728be4d49c76f66ad17d04369584d4ecad5cf77c68"
ROOT = pathlib.Path(__file__).resolve().parents[1]
DOWNLOAD_LIMIT = 64 * 1024 * 1024


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
            bundle.extractall(source.parent, filter="data")


def sdl_source(deps):
    """The verified, extracted SDL source tree under deps."""
    deps.mkdir(parents=True, exist_ok=True)
    archive = deps / f"SDL3-{VERSION}.tar.gz"
    fetch_archive(archive, f"https://www.libsdl.org/release/{archive.name}", SHA256)
    source = deps / f"SDL3-{VERSION}"
    extract_source(archive, source)
    return source


def main():
    deps = ROOT / "local" / "deps"
    source = sdl_source(deps)
    build = deps / "sdl-build"
    install = deps / "sdl-install"
    subprocess.run(["cmake", "-S", str(source), "-B", str(build),
                    "-DCMAKE_BUILD_TYPE=Release", "-DSDL_SHARED=OFF", "-DSDL_STATIC=ON",
                    "-DSDL_TEST_LIBRARY=OFF", "-DSDL_TESTS=OFF",
                    f"-DCMAKE_PROJECT_INCLUDE={(ROOT / 'tools' / 'sdl_build_options.cmake').as_posix()}",
                    f"-DCMAKE_INSTALL_PREFIX={install}"], check=True)
    subprocess.run(["cmake", "--build", str(build), "--config", "Release", "--parallel", "8"], check=True)
    subprocess.run(["cmake", "--install", str(build), "--config", "Release"], check=True)
    print(f"SDL ready: configure open-annihilation with -DCMAKE_PREFIX_PATH={install}")


if __name__ == "__main__":
    main()
