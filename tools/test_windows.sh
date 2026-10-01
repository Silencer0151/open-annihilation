#!/usr/bin/env bash
# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# Cross-build the tree for x86-64 Windows and run its tests under Wine, inside
# the container tools/windows/Dockerfile describes, from macOS or Linux.
#
# Needs Docker. On Apple silicon a Linux VM with Rosetta runs the x86-64
# container at near-native speed, e.g.
#   colima start --vm-type vz --vz-rosetta
# The checkout and the game installation are mounted at their host paths, so
# build trees and test data paths are the same inside and outside the
# container, and the container runs as the calling user, so everything it
# writes under the checkout belongs to that user.
set -euo pipefail

repo_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
image="${OA_WINDOWS_IMAGE:-oa-windows-test}"
rebuild_image=0
build_args=()

usage() {
    cat <<'USAGE'
Usage: tools/test_windows.sh [--rebuild-image] [build_windows.sh options]

Builds the Windows test image when it is missing, then runs
tools/build_windows.sh --run-tests inside it. Remaining options go to
build_windows.sh (--build-type, --jobs, --source).

  --rebuild-image   Build the image again before running
  -h, --help        Show this help

Environment: OA_GAME_DIR (a TA 3.1c installation for the game-data tests),
OA_WINDOWS_IMAGE (default: oa-windows-test), OA_WINDOWS_BUILD_DIR (default:
build-windows-wine), OA_WINDOWS_DEPS (default: local/deps/windows-wine),
OA_WINDOWS_CTEST_ARGS, OA_BUILD_TYPE, OA_BUILD_JOBS, OA_WINDOWS_CCACHE_VOLUME
(the Docker volume that keeps compiled objects; default: oa-windows-ccache,
empty for none), OA_WINDOWS_CCACHE_SIZE (default: 20G).
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        -h|--help) usage; exit 0 ;;
        --rebuild-image) rebuild_image=1; shift ;;
        *) build_args+=("$1"); shift ;;
    esac
done

if ! command -v docker >/dev/null 2>&1 || ! docker info >/dev/null 2>&1; then
    printf 'test_windows.sh: Docker is not available; on macOS start a Linux VM first, e.g.\n' >&2
    printf '  colima start --vm-type vz --vz-rosetta\n' >&2
    exit 1
fi
if [[ "$rebuild_image" == 1 ]] || ! docker image inspect "$image" >/dev/null 2>&1; then
    printf 'Building the %s image...\n' "$image"
    docker build --platform linux/amd64 -t "$image" "$repo_dir/tools/windows"
fi

# Dependencies and build trees made inside the container stay apart from a
# cross-build made directly on the host, whose compilers live elsewhere.
build_dir="${OA_WINDOWS_BUILD_DIR:-$repo_dir/build-windows-wine}"
deps="${OA_WINDOWS_DEPS:-$repo_dir/local/deps/windows-wine}"
mkdir -p "$repo_dir/local"
run_args=(--rm --platform linux/amd64 --user "$(id -u):$(id -g)"
    -v "$repo_dir:$repo_dir" -w "$repo_dir"
    -e HOME=/tmp -e "WINEPREFIX=$repo_dir/local/wine-prefix"
    -e "OA_WINDOWS_BUILD_DIR=$build_dir" -e "OA_WINDOWS_DEPS=$deps")
for name in OA_BUILD_TYPE OA_BUILD_JOBS OA_WINDOWS_CTEST_ARGS; do
    if [[ -n "${!name:-}" ]]; then run_args+=(-e "$name=${!name}"); fi
done
if [[ -n "${OA_GAME_DIR:-}" ]]; then
    if [[ ! -d "$OA_GAME_DIR" ]]; then
        printf 'test_windows.sh: OA_GAME_DIR is not a directory: %s\n' "$OA_GAME_DIR" >&2
        exit 1
    fi
    game_dir="$(cd -- "$OA_GAME_DIR" && pwd)"
    run_args+=(-v "$game_dir:$game_dir:ro" -e "OA_GAME_DIR=$game_dir")
fi

# Compiled objects are kept in a Docker volume that every run and every
# checkout shares. Paths under the home directory are hashed relative to the
# build tree, so checkouts of the same sources reuse each other's objects.
ccache_volume="${OA_WINDOWS_CCACHE_VOLUME-oa-windows-ccache}"
if [[ -n "$ccache_volume" ]]; then
    if ! docker volume inspect "$ccache_volume" >/dev/null 2>&1; then
        docker volume create "$ccache_volume" >/dev/null
        # A new volume belongs to root; the build runs as the calling user.
        docker run --rm --platform linux/amd64 --user 0:0 -v "$ccache_volume:/ccache" "$image" \
            chown "$(id -u):$(id -g)" /ccache
    fi
    run_args+=(-v "$ccache_volume:/ccache" -e CCACHE_DIR=/ccache -e "CCACHE_BASEDIR=$HOME"
        -e CCACHE_NOHASHDIR=1 -e "CCACHE_MAXSIZE=${OA_WINDOWS_CCACHE_SIZE:-20G}")
else
    run_args+=(-e OA_WINDOWS_CCACHE=0)
fi

docker run "${run_args[@]}" "$image" \
    "$repo_dir/tools/build_windows.sh" --run-tests ${build_args[@]+"${build_args[@]}"}
