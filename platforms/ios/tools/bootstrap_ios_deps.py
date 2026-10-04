#!/usr/bin/env python3
# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

"""Build the pinned SDL3 and FreeType as static libraries for iPhone and iPad.

The iOS and iPadOS build (platforms/ios) links them into the game. Each
system image (SDK) gets its own prefix root, <deps>/ios-<sdk>, with one
install prefix per library (sdl/, freetype/): iphonesimulator for the
simulator on this Mac, iphoneos for devices. Both are built for arm64 and
run from iOS 15.0. The sources are the engine's pins: the SDL release
tools/bootstrap_sdl.py names and the FreeType release and options
tools/bootstrap_text_fonts.py names, from the same verified archives in
<deps>. zlib comes with the system image, and the fonts are the host's
(local/deps/text-fonts), which are the same on every platform.

Build trees live beside each root (<root>-build). Each prefix records in
build-settings.json what its library was built from and how: the version,
the archive's SHA-256, the system image and its version, the deployment
target, the architecture and the options. A library whose prefix is complete
and records the settings of this run is not built again; any other is built
afresh. Nothing outside the archive cache, the roots and their build trees
is modified.
"""
import argparse
import json
import pathlib
import shutil
import subprocess
import sys

ENGINE_ROOT = pathlib.Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ENGINE_ROOT / "tools"))
import bootstrap_sdl  # noqa: E402
import bootstrap_text_fonts  # noqa: E402

# The system images the libraries are built for: the simulator's and the
# devices'.
SDKS = ("iphonesimulator", "iphoneos")
ARCHITECTURE = "arm64"
DEPLOYMENT_TARGET = "15.0"
DEFAULT_JOBS = 3
SETTINGS_FILE = "build-settings.json"
# SDL's own options: a static library without its tests and examples, read
# with the options file tools/bootstrap_sdl.py builds the host's SDL with.
SDL_OPTIONS = [
    "-DSDL_SHARED=OFF",
    "-DSDL_STATIC=ON",
    "-DSDL_TEST_LIBRARY=OFF",
    "-DSDL_TESTS=OFF",
    "-DSDL_EXAMPLES=OFF",
    f"-DCMAKE_PROJECT_INCLUDE={(ENGINE_ROOT / 'tools' / 'sdl_build_options.cmake').as_posix()}",
]
SDL_INSTALLED = "lib/cmake/SDL3/SDL3Config.cmake"


def sdk_version(sdk):
    """The version of the system image the Xcode tools hold for sdk, e.g. "26.2"."""
    result = subprocess.run(["xcrun", "--sdk", sdk, "--show-sdk-version"], check=True,
                            capture_output=True, text=True)
    return result.stdout.strip()


def settings(version, sha256, sdk, options):
    """Returns what a library's prefix records it was built from and how."""
    return {"version": version, "sha256": sha256, "sdk": sdk, "sdk_version": sdk_version(sdk),
            "deployment_target": DEPLOYMENT_TARGET, "architecture": ARCHITECTURE,
            "options": options}


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


def launcher_options():
    """The compiler launcher options: ccache when this machine has it, else none."""
    if shutil.which("ccache") is None:
        return []
    return ["-DCMAKE_C_COMPILER_LAUNCHER=ccache", "-DCMAKE_CXX_COMPILER_LAUNCHER=ccache",
            "-DCMAKE_OBJC_COMPILER_LAUNCHER=ccache"]


def generator_options():
    """Ninja when this machine has it, else CMake's default generator."""
    return ["-G", "Ninja"] if shutil.which("ninja") is not None else []


def build_for_ios(source, build, install, sdk, jobs, options):
    """Builds and installs a CMake project for sdk, arm64, afresh."""
    shutil.rmtree(build, ignore_errors=True)
    shutil.rmtree(install, ignore_errors=True)
    subprocess.run(["cmake", "-S", str(source), "-B", str(build), *generator_options(),
                    "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_SYSTEM_NAME=iOS",
                    f"-DCMAKE_OSX_SYSROOT={sdk}", f"-DCMAKE_OSX_ARCHITECTURES={ARCHITECTURE}",
                    f"-DCMAKE_OSX_DEPLOYMENT_TARGET={DEPLOYMENT_TARGET}",
                    f"-DCMAKE_INSTALL_PREFIX={install}", *launcher_options(), *options], check=True)
    subprocess.run(["cmake", "--build", str(build), "--config", "Release", "--parallel", str(jobs)],
                   check=True)
    subprocess.run(["cmake", "--install", str(build), "--config", "Release"], check=True)


def bootstrap(deps, sdk, jobs):
    """Builds what is not current of SDL and FreeType for sdk; returns the prefix root."""
    root = deps / f"ios-{sdk}"
    builds = deps / f"ios-{sdk}-build"
    sdl_install = root / "sdl"
    sdl_settings = settings(bootstrap_sdl.VERSION, bootstrap_sdl.SHA256, sdk, SDL_OPTIONS)
    if not is_current(sdl_install, [SDL_INSTALLED, "lib/libSDL3.a"], sdl_settings):
        build_for_ios(bootstrap_sdl.sdl_source(deps), builds / "sdl", sdl_install, sdk, jobs,
                      SDL_OPTIONS)
        record(sdl_install, sdl_settings)
    freetype_install = root / "freetype"
    freetype_settings = settings(bootstrap_text_fonts.FREETYPE_VERSION,
                                 bootstrap_text_fonts.FREETYPE_SHA256, sdk,
                                 bootstrap_text_fonts.FREETYPE_OPTIONS)
    if not is_current(freetype_install, [bootstrap_text_fonts.FREETYPE_INSTALLED], freetype_settings):
        build_for_ios(bootstrap_text_fonts.freetype_source(deps), builds / "freetype",
                      freetype_install, sdk, jobs, bootstrap_text_fonts.FREETYPE_OPTIONS)
        record(freetype_install, freetype_settings)
    print(f"SDL {bootstrap_sdl.VERSION} and FreeType {bootstrap_text_fonts.FREETYPE_VERSION} "
          f"for {sdk} {sdl_settings['sdk_version']}, {ARCHITECTURE}, iOS {DEPLOYMENT_TARGET} and later: "
          f"{root}")
    return root


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--sdk", choices=(*SDKS, "all"), action="append",
                        help="system image to build for; repeat it, or name all "
                             "(default: iphonesimulator)")
    parser.add_argument("--deps", type=pathlib.Path, default=ENGINE_ROOT / "local" / "deps",
                        help="archive and source cache, and the place of the prefixes "
                             "(default: local/deps)")
    parser.add_argument("--jobs", type=int, default=DEFAULT_JOBS,
                        help=f"parallel build jobs (default: {DEFAULT_JOBS})")
    args = parser.parse_args(argv)
    if sys.platform != "darwin":
        parser.error("the iOS libraries are built on macOS with the Xcode tools")
    if args.jobs < 1:
        parser.error("--jobs must be at least 1")
    wanted = args.sdk or ["iphonesimulator"]
    sdks = list(SDKS) if "all" in wanted else list(dict.fromkeys(wanted))
    deps = args.deps.resolve()
    for sdk in sdks:
        bootstrap(deps, sdk, args.jobs)
    text_fonts = deps / "text-fonts"
    if not text_fonts.is_dir():
        print(f"The fonts are missing from {text_fonts}: run python3 tools/bootstrap_text_fonts.py "
              "--fonts-only from the engine's folder")
    return 0


if __name__ == "__main__":
    sys.exit(main())
