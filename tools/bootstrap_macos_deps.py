#!/usr/bin/env python3
# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

"""Build the pinned zlib, SDL3 and FFmpeg as universal static libraries for macOS.

The macOS release (tools/release_macos.sh) links them into the application,
which then needs no library beyond the system's. Each library runs on arm64
and x86_64 Macs from the oldest macOS release --deployment-target names, and
gets its own install prefix (zlib/, sdl/, ffmpeg/) under the prefix root,
<deps>/macos-<deployment target> unless --prefix-root names another. zlib
and SDL3 are built for both architectures at once; FFmpeg is built once for
each, with the options tools/bootstrap_windows_deps.py shares, and lipo
combines the two. Build trees live beside the root (<root>-build). Each
prefix records in build-settings.json what its library was built from and
how: the version, the archive's SHA-256, the deployment target, the
architectures and the options. A library whose prefix is complete and
records the settings of this run is not built again; any other is built
afresh, so a new pin or new options never leave older libraries in place.
Nothing outside the cache, the root and its build trees is modified.
"""
import argparse
import json
import pathlib
import platform
import shutil
import subprocess
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import bootstrap_sdl  # noqa: E402
import bootstrap_windows_deps as pins  # noqa: E402

ARCHITECTURES = ("arm64", "x86_64")
DEFAULT_DEPLOYMENT_TARGET = "11.0"
FFMPEG_LIBRARIES = ("libavformat.a", "libavcodec.a", "libavutil.a", "libswscale.a", "libswresample.a")
SETTINGS_FILE = "build-settings.json"
ZLIB_OPTIONS = ["-DZLIB_BUILD_EXAMPLES=OFF", "-DCMAKE_POLICY_VERSION_MINIMUM=3.5"]
SDL_OPTIONS = ["-DSDL_SHARED=OFF", "-DSDL_STATIC=ON", "-DSDL_TEST_LIBRARY=OFF", "-DSDL_TESTS=OFF",
               "-DSDL_EXAMPLES=OFF"]


def settings(version, sha256, deployment_target, options):
    """Returns what a library's prefix records it was built from and how."""
    return {"version": version, "sha256": sha256, "deployment_target": deployment_target,
            "architectures": list(ARCHITECTURES), "options": options}


def is_current(install, files, wanted):
    """Whether install holds files and was built with the settings wanted."""
    if not all((install / name).exists() for name in files):
        return False
    try:
        recorded = json.loads((install / SETTINGS_FILE).read_text())
    except (OSError, ValueError):
        recorded = None
    if recorded != wanted:
        print(f"{install} was built with other settings; building it afresh")
        return False
    return True


def record(install, wanted):
    """Records in install the settings its library was built with."""
    (install / SETTINGS_FILE).write_text(json.dumps(wanted, indent=2, sort_keys=True) + "\n")


def build_universal(source, build, install, deployment_target, jobs, options):
    """Builds and installs a CMake project for both architectures, afresh."""
    shutil.rmtree(build, ignore_errors=True)
    shutil.rmtree(install, ignore_errors=True)
    subprocess.run(["cmake", "-S", str(source), "-B", str(build), "-DCMAKE_BUILD_TYPE=Release",
                    f"-DCMAKE_INSTALL_PREFIX={install}",
                    f"-DCMAKE_OSX_ARCHITECTURES={';'.join(ARCHITECTURES)}",
                    f"-DCMAKE_OSX_DEPLOYMENT_TARGET={deployment_target}", *options], check=True)
    subprocess.run(["cmake", "--build", str(build), "--parallel", str(jobs)], check=True)
    subprocess.run(["cmake", "--install", str(build)], check=True)


def ffmpeg_configure_options(architecture, deployment_target):
    """Returns FFmpeg's configure options for one architecture, but its prefix."""
    target_options = []
    if architecture != platform.machine():
        target_options = ["--enable-cross-compile", f"--arch={architecture}", "--target-os=darwin"]
    minimum = f"-mmacosx-version-min={deployment_target}"
    return [*target_options, f"--cc=clang -arch {architecture}", f"--extra-cflags={minimum}",
            f"--extra-ldflags={minimum}",
            *pins.ffmpeg_options(["--enable-static", "--disable-shared"], "--enable-pthreads")]


def build_ffmpeg(source, build, install, architecture, deployment_target, jobs):
    """Builds and installs FFmpeg's static libraries for one architecture."""
    shutil.rmtree(build, ignore_errors=True)
    shutil.rmtree(install, ignore_errors=True)
    build.mkdir(parents=True)
    subprocess.run([str(source / "configure"), f"--prefix={install}",
                    *ffmpeg_configure_options(architecture, deployment_target)],
                   cwd=build, check=True)
    subprocess.run(["make", f"-j{jobs}"], cwd=build, check=True)
    subprocess.run(["make", "install"], cwd=build, check=True)


def combine_ffmpeg(installs, universal):
    """Combines the per-architecture FFmpeg installs into one universal prefix."""
    shutil.rmtree(universal, ignore_errors=True)
    (universal / "lib").mkdir(parents=True)
    # The installed headers are the same for both architectures.
    shutil.copytree(installs[0] / "include", universal / "include")
    for library in FFMPEG_LIBRARIES:
        subprocess.run(["lipo", "-create", *[str(install / "lib" / library) for install in installs],
                        "-output", str(universal / "lib" / library)], check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--deps", type=pathlib.Path, default=bootstrap_sdl.ROOT / "local" / "deps",
                        help="archive and source cache (default: local/deps)")
    parser.add_argument("--prefix-root", type=pathlib.Path,
                        help="install prefix root (default: <deps>/macos-<deployment target>)")
    parser.add_argument("--deployment-target", default=DEFAULT_DEPLOYMENT_TARGET,
                        help=f"oldest macOS release the libraries run on (default: {DEFAULT_DEPLOYMENT_TARGET})")
    parser.add_argument("--jobs", type=int, default=6)
    args = parser.parse_args()
    if sys.platform != "darwin":
        parser.error("the macOS libraries are built on macOS")
    deps = args.deps.resolve()
    prefixes = (args.prefix_root or deps / f"macos-{args.deployment_target}").resolve()
    builds = prefixes.parent / f"{prefixes.name}-build"

    # zlib's own CMake lists a pre-3.5 minimum version that CMake 4 rejects,
    # hence CMAKE_POLICY_VERSION_MINIMUM in ZLIB_OPTIONS.
    zlib_install = prefixes / "zlib"
    zlib_settings = settings(pins.ZLIB_VERSION, pins.ZLIB_SHA256, args.deployment_target, ZLIB_OPTIONS)
    if not is_current(zlib_install, ["lib/libz.a"], zlib_settings):
        build_universal(pins.zlib_source(deps), builds / "zlib", zlib_install, args.deployment_target,
                        args.jobs, ZLIB_OPTIONS)
        record(zlib_install, zlib_settings)
    sdl_install = prefixes / "sdl"
    sdl_settings = settings(bootstrap_sdl.VERSION, bootstrap_sdl.SHA256, args.deployment_target, SDL_OPTIONS)
    if not is_current(sdl_install, ["lib/cmake/SDL3/SDL3Config.cmake"], sdl_settings):
        build_universal(bootstrap_sdl.sdl_source(deps), builds / "sdl", sdl_install, args.deployment_target,
                        args.jobs, SDL_OPTIONS)
        record(sdl_install, sdl_settings)
    ffmpeg_install = prefixes / "ffmpeg"
    ffmpeg_settings = settings(pins.FFMPEG_VERSION, pins.FFMPEG_SHA256, args.deployment_target,
                               {architecture: ffmpeg_configure_options(architecture, args.deployment_target)
                                for architecture in ARCHITECTURES})
    if not is_current(ffmpeg_install, [f"lib/{library}" for library in FFMPEG_LIBRARIES], ffmpeg_settings):
        source = pins.ffmpeg_source(deps)
        installs = [builds / f"ffmpeg-{architecture}-install" for architecture in ARCHITECTURES]
        for architecture, install in zip(ARCHITECTURES, installs):
            build_ffmpeg(source, builds / f"ffmpeg-{architecture}", install, architecture,
                         args.deployment_target, args.jobs)
        combine_ffmpeg(installs, ffmpeg_install)
        record(ffmpeg_install, ffmpeg_settings)
    for install, wanted in ((zlib_install, zlib_settings), (sdl_install, sdl_settings),
                            (ffmpeg_install, ffmpeg_settings)):
        print(f"{install.name} {wanted['version']} for macOS {args.deployment_target}, {' '.join(ARCHITECTURES)}")
    print(f"macOS dependencies ready under {prefixes}")


if __name__ == "__main__":
    main()
