#!/usr/bin/env bash
# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# Cross-build the project for x86-64 Windows from macOS or Linux with the
# mingw-w64 GCC toolchain. zlib, SDL3 and a minimal FFmpeg are built for the
# target into local/deps/windows on first use; the tree (or the project that
# --source names, which adds this one) is configured with
# cmake/toolchains/x86_64-w64-mingw32.cmake and every target is compiled.
set -euo pipefail

repo_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
toolchain="$repo_dir/cmake/toolchains/x86_64-w64-mingw32.cmake"
triple="${OA_MINGW_TRIPLE:-x86_64-w64-mingw32}"
deps="${OA_WINDOWS_DEPS:-$repo_dir/local/deps/windows}"
build_dir="${OA_WINDOWS_BUILD_DIR:-}"
source_dir="$repo_dir"
build_type="${OA_BUILD_TYPE:-Release}"
jobs="${OA_BUILD_JOBS:-6}"
ffmpeg_dir="${OA_WINDOWS_FFMPEG:-}"
testing=ON
run_tests=0
targets=()

usage() {
    cat <<'USAGE'
Usage: tools/build_windows.sh [--ffmpeg PREFIX] [--no-tests] [--run-tests]
                              [--build-type TYPE] [--jobs N] [--target NAME]...
                              [--source DIR]

Cross-builds the project for x86-64 Windows with mingw-w64. Executables land
in build-windows/ as *.exe with the GCC runtime linked statically. They run on
Windows, or here under Wine with --run-tests (tools/test_windows.sh provides a
container with the toolchain and Wine on macOS and Linux).

  --ffmpeg PREFIX   FFmpeg built for x86-64 Windows/mingw: include/, lib/ with
                    import libraries, bin/ with the DLLs and the build's
                    COPYING.LGPLv2.1. Replaces the minimal FFmpeg otherwise
                    cross-built into local/deps/windows/ffmpeg; its DLLs and
                    licence are copied next to open-annihilation.exe and oa-intro.exe.
  --no-tests        Configure with BUILD_TESTING=OFF
  --run-tests       Run ctest after the build, each test executable through
                    Wine (CMAKE_TEST_LAUNCHER); needs wine on PATH and CMake 3.29+.
                    OA_WINDOWS_CTEST_ARGS adds ctest arguments (e.g. -R or -E).
  --build-type TYPE CMake build type (default: Release)
  --jobs N          Parallel compile jobs (default: 6)
  --target NAME     Build only this target (repeatable)
  --source DIR      Configure DIR, a project that adds this one, instead of this
                    tree; its build tree defaults to DIR/build-windows
  -h, --help        Show this help

Environment: OA_WINDOWS_DEPS (default: local/deps/windows), OA_WINDOWS_BUILD_DIR,
OA_WINDOWS_FFMPEG, OA_BUILD_TYPE, OA_BUILD_JOBS, OA_MINGW_TRIPLE,
OA_WINDOWS_CTEST_ARGS, OA_WINDOWS_CCACHE (0 builds without ccache).
USAGE
}

require_value() {
    if [[ $# -lt 2 || -z "$2" ]]; then
        printf 'build_windows.sh: %s requires a value\n' "$1" >&2
        exit 2
    fi
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        -h|--help) usage; exit 0 ;;
        --ffmpeg) require_value "$@"; ffmpeg_dir="$2"; shift 2 ;;
        --build-type) require_value "$@"; build_type="$2"; shift 2 ;;
        --jobs) require_value "$@"; jobs="$2"; shift 2 ;;
        --target) require_value "$@"; targets+=("$2"); shift 2 ;;
        --source) require_value "$@"; source_dir="$2"; shift 2 ;;
        --no-tests) testing=OFF; shift ;;
        --run-tests) run_tests=1; shift ;;
        *) printf 'build_windows.sh: unknown option: %s\n' "$1" >&2; usage >&2; exit 2 ;;
    esac
done

if ! command -v "$triple-g++" >/dev/null 2>&1; then
    cat >&2 <<EOF
build_windows.sh: $triple-g++ is not installed.
  macOS:          brew install mingw-w64
  Debian/Ubuntu:  sudo apt-get install g++-mingw-w64-x86-64
EOF
    exit 1
fi
if ! command -v cmake >/dev/null 2>&1; then
    printf 'build_windows.sh: CMake 3.24+ is required.\n' >&2
    exit 1
fi
# The tests run through Wine, which ctest puts in front of every test
# executable it starts.
launcher_args=()
if [[ "$run_tests" == 1 ]]; then
    if [[ "$testing" == OFF || ${#targets[@]} -gt 0 ]]; then
        printf 'build_windows.sh: --run-tests needs the whole tree with tests; drop --no-tests and --target\n' >&2
        exit 2
    fi
    if ! command -v wine >/dev/null 2>&1; then
        printf 'build_windows.sh: --run-tests needs wine on PATH (tools/test_windows.sh provides it)\n' >&2
        exit 1
    fi
    launcher_args=("-DCMAKE_TEST_LAUNCHER=$(command -v wine)")
fi
case "$source_dir" in /*) ;; *) source_dir="$PWD/$source_dir" ;; esac
case "$build_dir" in "") ;; /*) ;; *) build_dir="$PWD/$build_dir" ;; esac
if [[ -z "$build_dir" ]]; then build_dir="$source_dir/build-windows"; fi

# ccache, when installed, keeps compiled objects between builds (CCACHE_DIR says
# where); OA_WINDOWS_CCACHE=0 turns it off. Ninja, when installed, generates a
# new build tree; an existing tree keeps the generator it was made with.
compiler_launcher=""
if [[ "${OA_WINDOWS_CCACHE:-1}" != 0 ]] && command -v ccache >/dev/null 2>&1; then
    compiler_launcher=ccache
fi
generator_args=()
if [[ ! -f "$build_dir/CMakeCache.txt" ]] && command -v ninja >/dev/null 2>&1; then
    generator_args=(-G Ninja)
fi
case "$deps" in /*) ;; *) deps="$PWD/$deps" ;; esac

bootstrap_args=(--prefix-root "$deps" --toolchain "$toolchain" --triple "$triple" --jobs "$jobs")
ffmpeg_prefix="$deps/ffmpeg"
if [[ -n "$ffmpeg_dir" ]]; then
    case "$ffmpeg_dir" in /*) ;; *) ffmpeg_dir="$PWD/$ffmpeg_dir" ;; esac
    if [[ ! -f "$ffmpeg_dir/include/libavformat/avformat.h" ]]; then
        printf 'build_windows.sh: %s has no include/libavformat/avformat.h\n' "$ffmpeg_dir" >&2
        exit 1
    fi
    ffmpeg_prefix="$ffmpeg_dir"
    bootstrap_args+=(--no-ffmpeg)
fi

printf 'Preparing Windows dependencies under %s...\n' "$deps"
python3 "$repo_dir/tools/bootstrap_windows_deps.py" "${bootstrap_args[@]}"

# The FFmpeg lookups are cleared so switching between the bootstrapped prefix
# and an --ffmpeg override takes effect in an existing build tree.
printf 'Configuring %s (%s, FFmpeg %s)...\n' "$build_dir" "$build_type" "$ffmpeg_prefix"
cmake -S "$source_dir" -B "$build_dir" ${generator_args[@]+"${generator_args[@]}"} -DCMAKE_TOOLCHAIN_FILE="$toolchain" \
    "-DCMAKE_C_COMPILER_LAUNCHER=$compiler_launcher" "-DCMAKE_CXX_COMPILER_LAUNCHER=$compiler_launcher" \
    -U OA_FFMPEG_INCLUDE_DIR -U 'OA_AV*_LIBRARY' -U 'OA_SW*_LIBRARY' \
    "-DCMAKE_BUILD_TYPE=$build_type" "-DOA_WINDOWS_DEPS=$deps" "-DOA_WINDOWS_FFMPEG=$ffmpeg_dir" \
    -DOA_BUILD_PLATFORM=ON -DOA_BUILD_INTRO_PLAYER=ON "-DBUILD_TESTING=$testing" \
    ${launcher_args[@]+"${launcher_args[@]}"}

# The LGPL text travels with the FFmpeg DLLs (see ATTRIBUTIONS.md): the
# bootstrap puts the pinned source's copy in its prefix, and an --ffmpeg
# prefix must provide its own.
copy_ffmpeg_dlls() {
    if [[ ( -f "$build_dir/open-annihilation.exe" || -f "$build_dir/oa-intro.exe" ) && -d "$ffmpeg_prefix/bin" ]]; then
        if [[ ! -f "$ffmpeg_prefix/COPYING.LGPLv2.1" ]]; then
            printf 'build_windows.sh: %s has no COPYING.LGPLv2.1; add the licence of that FFmpeg build there\n' \
                "$ffmpeg_prefix" >&2
            exit 1
        fi
        find "$ffmpeg_prefix/bin/" -maxdepth 1 -name '*.dll' -exec cp -f {} "$build_dir/" \;
        cp -f "$ffmpeg_prefix/COPYING.LGPLv2.1" "$build_dir/COPYING.LGPLv2.1"
    fi
}

if [[ ${#targets[@]} -gt 0 ]]; then
    for target in "${targets[@]}"; do
        cmake --build "$build_dir" --target "$target" --parallel "$jobs"
    done
    copy_ffmpeg_dlls
    exit 0
fi
cmake --build "$build_dir" --parallel "$jobs"
copy_ffmpeg_dlls

count="$(find "$build_dir" -name '*.exe' | wc -l | tr -d ' ')"
printf 'Windows cross-build complete: %s executables under %s\n' "$count" "$build_dir"
if [[ -f "$build_dir/open-annihilation.exe" ]]; then
    printf 'open-annihilation.exe: %s\n' "$build_dir/open-annihilation.exe"
else
    printf 'open-annihilation.exe was not built; see the build output above.\n'
fi

if [[ "$run_tests" == 1 ]]; then
    # Test executables outside the build root find the FFmpeg DLLs through
    # WINEPATH, as they would through PATH on Windows.
    if [[ -d "$ffmpeg_prefix/bin" ]]; then
        WINEPATH="$(winepath -w "$ffmpeg_prefix/bin" 2>/dev/null || true)"
        export WINEPATH
    fi
    # The scripted checks start the executables they drive through the same
    # runner, which ctest's launcher does not reach.
    OA_TEST_RUNNER="$(command -v wine)"
    export OA_TEST_RUNNER
    printf 'Running the Windows tests under %s...\n' "$(wine --version 2>/dev/null || echo wine)"
    ctest_args=()
    if [[ -n "${OA_WINDOWS_CTEST_ARGS:-}" ]]; then
        read -r -a ctest_args <<<"$OA_WINDOWS_CTEST_ARGS"
    fi
    ctest --test-dir "$build_dir" --output-on-failure --parallel "$jobs" \
        ${ctest_args[@]+"${ctest_args[@]}"}
fi
