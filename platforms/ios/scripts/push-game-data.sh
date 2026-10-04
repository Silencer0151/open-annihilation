#!/usr/bin/env bash
# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# Copy a Total Annihilation folder from this Mac into the installed game's
# Documents folder on an iOS simulator, as Documents/Total Annihilation, where
# the game looks for it by default. On the same volume the copy is an APFS
# clone (instant, no extra space); elsewhere ditto copies it. The game must be
# installed on the simulator first (platforms/ios/run.sh installs it).
set -euo pipefail

ios_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
repo_dir="$(cd -- "$ios_dir/../.." && pwd)"
bundle_id="${OA_IOS_BUNDLE_IDENTIFIER:-net.coreprime.open-annihilation}"
iphone_udid="45BA4005-FBF3-4E1B-AD67-315C19AC3ED9"
ipad_udid="9BE288E6-0FBE-4567-B4B8-D998340C1D59"
udid="${OA_IOS_SIMULATOR:-$iphone_udid}"
game_dir=""
force=0
remembered="$repo_dir/local/ios/game-data-source"

usage() {
    cat <<'USAGE'
Usage: platforms/ios/scripts/push-game-data.sh [--udid UDID | --ipad]
                                               [--game-dir PATH] [--force]

Copies a Total Annihilation folder (the folder holding totala1.hpi) into the
installed game's Documents/Total Annihilation on a simulator. Nothing is
copied when that folder is there already, unless --force replaces it.

The folder copied is --game-dir, else OA_GAME_DIR, else the folder this
script copied last time (local/ios/game-data-source), else the OA_GAME_DIR a
desktop build tree of this checkout was configured with (build/, then the
other build-* trees).

  --udid UDID         The simulator (default: OA_IOS_SIMULATOR, else the iPhone 17)
  --ipad              The iPad Air 11-inch (M3) simulator
  --game-dir PATH     The Total Annihilation folder on this Mac
  --force             Replace the copy already on the simulator
  --help              Show this help
USAGE
}

require_value() {
    if [[ $# -lt 2 || -z "$2" ]]; then
        printf 'push-game-data.sh: %s requires a value\n' "$1" >&2
        exit 2
    fi
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --help) usage; exit 0 ;;
        --udid) require_value "$@"; udid="$2"; shift 2 ;;
        --ipad) udid="$ipad_udid"; shift ;;
        --game-dir) require_value "$@"; game_dir="$2"; shift 2 ;;
        --force) force=1; shift ;;
        *) printf 'push-game-data.sh: unknown option %s (see --help)\n' "$1" >&2; exit 2 ;;
    esac
done

# The OA_GAME_DIR a configured build tree of this checkout records, if any.
configured_game_dir() {
    local cache value
    for cache in "$repo_dir/build/CMakeCache.txt" "$repo_dir"/build-*/CMakeCache.txt; do
        [[ -f "$cache" ]] || continue
        value="$(sed -n 's/^OA_GAME_DIR:[A-Z]*=//p' "$cache" | head -n 1)"
        if [[ -n "$value" && -d "$value" ]]; then
            printf '%s\n' "$value"
            return 0
        fi
    done
    return 1
}

source_note=""
if [[ -z "$game_dir" && -n "${OA_GAME_DIR:-}" ]]; then
    game_dir="$OA_GAME_DIR"
fi
if [[ -z "$game_dir" && -f "$remembered" ]]; then
    game_dir="$(head -n 1 "$remembered")"
    source_note=" (remembered in local/ios/game-data-source)"
fi
if [[ -z "$game_dir" ]] && game_dir="$(configured_game_dir)"; then
    source_note=" (the OA_GAME_DIR of a build tree here)"
fi

# The game's data container moves whenever the game is installed afresh, so it is asked for every time.
if ! container="$(xcrun simctl get_app_container "$udid" "$bundle_id" data 2>/dev/null)" || [[ -z "$container" ]]; then
    printf 'push-game-data.sh: %s is not installed on simulator %s; run platforms/ios/run.sh first\n' \
        "$bundle_id" "$udid" >&2
    exit 1
fi
target="$container/Documents/Total Annihilation"
if [[ -d "$target" && "$force" == 0 ]]; then
    printf 'The game data is on the simulator already: %s\n' "$target"
    exit 0
fi

if [[ -z "$game_dir" ]]; then
    printf 'push-game-data.sh: name the Total Annihilation folder with --game-dir or OA_GAME_DIR\n' >&2
    exit 2
fi
case "$game_dir" in /*) ;; *) game_dir="$PWD/$game_dir" ;; esac
game_dir="${game_dir%/}"
if [[ ! -d "$game_dir" ]]; then
    printf 'push-game-data.sh: the game folder does not exist: %s\n' "$game_dir" >&2
    exit 1
fi

mkdir -p "$container/Documents"
rm -rf "$target"
printf 'Copying %s%s\n     to %s\n' "$game_dir" "$source_note" "$target"
start=$SECONDS
if ! cp -cR "$game_dir" "$target" 2>/dev/null; then
    rm -rf "$target"
    ditto "$game_dir" "$target"
fi
mkdir -p "$(dirname -- "$remembered")"
printf '%s\n' "$game_dir" > "$remembered"
printf 'Copied in %d s\n' "$((SECONDS - start))"
