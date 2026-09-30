#!/usr/bin/env python3
# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

"""Cross-build the pinned zlib, SDL3 and FFmpeg for x86-64 Windows into local/deps/windows.

Every library gets its own install prefix under the prefix root, which
cmake/toolchains/x86_64-w64-mingw32.cmake searches. Build trees live beside
the root (<root>-build) so they are never mistaken for prefixes. Nothing
outside local/ is modified.
"""
import argparse
import pathlib
import shutil
import subprocess
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import bootstrap_sdl  # noqa: E402

ROOT = bootstrap_sdl.ROOT
ZLIB_VERSION = "1.3.1"
ZLIB_SHA256 = "9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23"
ZLIB_URL = f"https://github.com/madler/zlib/releases/download/v{ZLIB_VERSION}/zlib-{ZLIB_VERSION}.tar.gz"
FFMPEG_VERSION = "9.0.2"
FFMPEG_SHA256 = "84960df915059e8754fef2cd7c9afeb614062b1b5458ec471eecee619ee04e98"
FFMPEG_URL = f"https://ffmpeg.org/releases/ffmpeg-{FFMPEG_VERSION}.tar.gz"
FFMPEG_LICENCE = "COPYING.LGPLv2.1"


def ffmpeg_options(library_options, thread_option, extra_options=()):
    """Returns the configure options of one package's FFmpeg.

    They are library_options (shared or static libraries), the selection
    every package shares with the target's thread_option in it, then
    extra_options. The selection is only what the intro player and the
    music decoder open: Smacker (.ZRB) movies and the numbered
    .mp3/.ogg/.wav/.flac disc tracks. nasm is not required because the x86
    assembly is left out. tools/bootstrap_macos_deps.py builds the macOS
    release's FFmpeg with these options too.
    """
    return [
        *library_options,
        "--disable-programs", "--disable-doc", "--disable-debug",
        "--disable-autodetect", thread_option, "--disable-network",
        "--disable-avdevice", "--disable-avfilter", "--disable-x86asm",
        "--disable-everything",
        "--enable-protocol=file",
        "--enable-demuxer=smacker,mp3,ogg,wav,flac",
        "--enable-decoder=smacker,smackaud,mp3float,vorbis,flac,pcm_s16le,pcm_s24le,pcm_u8",
        "--enable-parser=mpegaudio,vorbis,flac",
        *extra_options,
    ]


# Shared libraries match the build_windows.sh --ffmpeg contract (import
# libraries in lib/, DLLs in bin/). The GCC runtime and winpthread
# (clock_gettime, nanosleep) are linked into the DLLs so they need nothing
# beyond system DLLs.
FFMPEG_OPTIONS = ffmpeg_options(
    ["--enable-shared", "--disable-static"], "--enable-w32threads",
    ["--extra-ldflags=-static-libgcc", "--extra-libs=-Wl,-Bstatic,-lwinpthread,-Bdynamic"])


def zlib_source(deps):
    deps.mkdir(parents=True, exist_ok=True)
    archive = deps / f"zlib-{ZLIB_VERSION}.tar.gz"
    bootstrap_sdl.fetch_archive(archive, ZLIB_URL, ZLIB_SHA256)
    source = deps / f"zlib-{ZLIB_VERSION}"
    bootstrap_sdl.extract_source(archive, source)
    return source


def ffmpeg_source(deps):
    deps.mkdir(parents=True, exist_ok=True)
    archive = deps / f"ffmpeg-{FFMPEG_VERSION}.tar.gz"
    bootstrap_sdl.fetch_archive(archive, FFMPEG_URL, FFMPEG_SHA256)
    source = deps / f"ffmpeg-{FFMPEG_VERSION}"
    bootstrap_sdl.extract_source(archive, source)
    return source


def cross_build_ffmpeg(source, build, install, triple, jobs):
    build.mkdir(parents=True, exist_ok=True)
    subprocess.run([str(source / "configure"), f"--prefix={install}", "--enable-cross-compile",
                    "--arch=x86_64", "--target-os=mingw32", f"--cross-prefix={triple}-",
                    *FFMPEG_OPTIONS], cwd=build, check=True)
    subprocess.run(["make", f"-j{jobs}"], cwd=build, check=True)
    subprocess.run(["make", "install"], cwd=build, check=True)
    # The *.lib import libraries, which CMake would otherwise find ahead of lib/*.dll.a.
    for import_library in (install / "bin").glob("*.lib"):
        import_library.unlink()


def cross_build(source, build, install, toolchain, jobs, options):
    subprocess.run(["cmake", "-S", str(source), "-B", str(build),
                    f"-DCMAKE_TOOLCHAIN_FILE={toolchain}", "-DCMAKE_BUILD_TYPE=Release",
                    f"-DCMAKE_INSTALL_PREFIX={install}", *options], check=True)
    subprocess.run(["cmake", "--build", str(build), "--parallel", str(jobs)], check=True)
    subprocess.run(["cmake", "--install", str(build)], check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--deps", type=pathlib.Path, default=ROOT / "local" / "deps",
                        help="archive and source cache (default: local/deps)")
    parser.add_argument("--prefix-root", type=pathlib.Path,
                        help="install prefix root (default: <deps>/windows)")
    parser.add_argument("--toolchain", type=pathlib.Path,
                        default=ROOT / "cmake" / "toolchains" / "x86_64-w64-mingw32.cmake")
    parser.add_argument("--triple", default="x86_64-w64-mingw32",
                        help="mingw-w64 tool prefix for the FFmpeg build")
    parser.add_argument("--jobs", type=int, default=6)
    parser.add_argument("--no-ffmpeg", action="store_true",
                        help="skip FFmpeg (a prebuilt prefix is supplied instead)")
    args = parser.parse_args()
    deps = args.deps.resolve()
    prefixes = (args.prefix_root or deps / "windows").resolve()
    builds = prefixes.parent / f"{prefixes.name}-build"
    toolchain = args.toolchain.resolve()

    zlib_install = prefixes / "zlib"
    if not (zlib_install / "lib" / "libzlibstatic.a").exists():
        # zlib's own CMake lists a pre-3.5 minimum version that CMake 4 rejects.
        cross_build(zlib_source(deps), builds / "zlib", zlib_install, toolchain, args.jobs,
                    ["-DZLIB_BUILD_EXAMPLES=OFF", "-DCMAKE_POLICY_VERSION_MINIMUM=3.5"])
    sdl_install = prefixes / "sdl"
    if not (sdl_install / "lib" / "cmake" / "SDL3" / "SDL3Config.cmake").exists():
        cross_build(bootstrap_sdl.sdl_source(deps), builds / "sdl", sdl_install, toolchain, args.jobs,
                    ["-DSDL_SHARED=OFF", "-DSDL_STATIC=ON", "-DSDL_TEST_LIBRARY=OFF",
                     "-DSDL_TESTS=OFF", "-DSDL_EXAMPLES=OFF"])
    ffmpeg_install = prefixes / "ffmpeg"
    if not args.no_ffmpeg:
        source = ffmpeg_source(deps)
        if not (ffmpeg_install / "lib" / "libavformat.dll.a").exists():
            cross_build_ffmpeg(source, builds / "ffmpeg", ffmpeg_install, args.triple, args.jobs)
        # build_windows.sh ships the licence of the pinned source with the DLLs.
        shutil.copyfile(source / FFMPEG_LICENCE, ffmpeg_install / FFMPEG_LICENCE)
    print(f"Windows dependencies ready under {prefixes}")


if __name__ == "__main__":
    main()
