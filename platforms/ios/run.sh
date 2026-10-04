#!/usr/bin/env bash
# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# Build Open Annihilation for the iOS simulator from this working tree, install
# it on a simulator, copy the game data into it when it is missing, and launch
# it with --skip-intro --mute. The root ./run.sh --ios-simulator comes here;
# ./run.sh --ios-device comes here with --device, which builds for devices and
# says how to install that build.
set -euo pipefail

ios_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_dir="$(cd -- "$ios_dir/../.." && pwd)"
bundle_id="${OA_IOS_BUNDLE_IDENTIFIER:-net.coreprime.open-annihilation}"
iphone_udid="45BA4005-FBF3-4E1B-AD67-315C19AC3ED9"
ipad_udid="9BE288E6-0FBE-4567-B4B8-D998340C1D59"
udid="${OA_IOS_SIMULATOR:-$iphone_udid}"
device=0
team=""
build=1
push_data=1
force_data=0
reinstall=0
launch=1
game_data=""
screenshot=""
screenshot_delay=10
game_args=()

usage() {
    cat <<'USAGE'
Usage: platforms/ios/run.sh [--ipad | --udid UDID] [--no-build] [--reinstall]
                            [--game-data PATH] [--fresh-data | --no-data]
                            [--no-launch] [--screenshot FILE]
                            [--screenshot-delay SECONDS] [-- game options]
       platforms/ios/run.sh --device [--team ID]

Builds current source for the iOS simulator (stopping on a failed build, never
falling back to an older bundle), signs the bundle for the simulator, installs
it, copies the game data into the game's Documents/Total Annihilation when it
is not there yet (platforms/ios/scripts/push-game-data.sh), and launches the
game with --game-dir <that folder> --skip-intro --mute. The simulator must be
one this Mac has; it is started when it is not running. The game writes its
log to <data container>/Library/Application Support/net.coreprime.open-annihilation/logs.

  --ipad              Run on the iPad Air 11-inch (M3) simulator
  --udid UDID         Run on this simulator (default: OA_IOS_SIMULATOR, else
                      the iPhone 17 simulator)
  --no-build          Install the bundle built last instead of building
  --reinstall         Remove the game from the simulator first, with its data
                      container (the game folder, settings, saves and any
                      copy the Game files screen made), so it starts as a
                      fresh install
  --game-data PATH    The Total Annihilation folder on this Mac to copy
                      (default: see push-game-data.sh --help)
  --fresh-data        Replace the game data already on the simulator
  --no-data           Copy no game data and pass no --game-dir: the game looks
                      for its folder itself, and without one opens the Game
                      files screen
  --no-launch         Install (and copy the data) but do not launch the game
  --screenshot FILE   Save a screenshot of the simulator after the launch
  --screenshot-delay SECONDS  Wait this long before the screenshot (default: 10)
  --device            Build for iPhone and iPad devices instead, and say how
                      to install the build
  --team ID           The Apple developer team that signs device builds
  --help              Show this help

Options this script does not know, and everything after --, go to the game.
USAGE
}

require_value() {
    if [[ $# -lt 2 || -z "$2" ]]; then
        printf 'run.sh: %s requires a value\n' "$1" >&2
        exit 2
    fi
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --help) usage; exit 0 ;;
        --ipad) udid="$ipad_udid"; shift ;;
        --udid) require_value "$@"; udid="$2"; shift 2 ;;
        --device) device=1; shift ;;
        --team) require_value "$@"; team="$2"; shift 2 ;;
        --no-build) build=0; shift ;;
        --game-data) require_value "$@"; game_data="$2"; shift 2 ;;
        --fresh-data) force_data=1; shift ;;
        --no-data) push_data=0; shift ;;
        --reinstall) reinstall=1; shift ;;
        --no-launch) launch=0; shift ;;
        --screenshot) require_value "$@"; screenshot="$2"; shift 2 ;;
        --screenshot-delay) require_value "$@"; screenshot_delay="$2"; shift 2 ;;
        --) shift; game_args+=("$@"); break ;;
        *) game_args+=("$1"); shift ;;
    esac
done
case "$screenshot_delay" in ''|*[!0-9]*) printf 'run.sh: --screenshot-delay must be a whole number of seconds\n' >&2; exit 2 ;; esac
if [[ -n "$screenshot" ]]; then
    case "$screenshot" in /*) ;; *) screenshot="$PWD/$screenshot" ;; esac
fi

if [[ "$device" == 1 ]]; then
    build_args=(--device)
    if [[ -n "$team" ]]; then build_args+=(--team "$team"); fi
    bundle="$("$ios_dir/scripts/build.sh" "${build_args[@]}" --print-bundle)"
    cat <<GUIDANCE
Built $bundle for devices.

Installing on an iPhone or iPad needs the bundle signed with your developer
identity and a provisioning profile for $bundle_id, which Xcode manages:
  platforms/ios/scripts/xcode.sh --device --team <your team ID> --open
then choose the device in Xcode and run the oa-game scheme. Copy your Total
Annihilation folder into the game's Documents folder with the Files app or the
Finder (it must be named Total Annihilation), or build the data into the
bundle with -DOA_IOS_BUNDLED_GAME_DIR=<folder> (see platforms/ios/README.md).
GUIDANCE
    exit 0
fi

# The simulator: it must exist, and is started headless when it is not running.
if ! state="$(xcrun simctl list devices | grep -F "($udid)")"; then
    printf 'run.sh: this Mac has no simulator %s (xcrun simctl list devices lists them)\n' "$udid" >&2
    exit 1
fi
if [[ "$state" != *"(Booted)"* ]]; then
    printf 'Starting simulator %s\n' "$udid"
    xcrun simctl boot "$udid"
    xcrun simctl bootstatus "$udid" -b >/dev/null
fi

build_dir="${OA_IOS_BUILD_DIR:-$repo_dir/build-ios-sim}"
if [[ "$build" == 1 ]]; then
    bundle="$("$ios_dir/scripts/build.sh" --simulator --build-dir "$build_dir" --print-bundle)"
else
    bundle="$build_dir/engine/open-annihilation.app"
    if [[ ! -f "$bundle/Info.plist" ]]; then
        printf 'run.sh: no bundle has been built at %s; run without --no-build\n' "$bundle" >&2
        exit 1
    fi
fi

# The installed copy is a signed copy of the build's bundle, so the build
# tree itself is never signed and relinking never meets an old signature.
stage="$build_dir/stage/open-annihilation.app"
rm -rf "$stage"
mkdir -p "$(dirname -- "$stage")"
if ! cp -cR "$bundle" "$stage" 2>/dev/null; then
    rm -rf "$stage"
    ditto "$bundle" "$stage"
fi
xattr -cr "$stage"
codesign --force --sign - --timestamp=none "$stage"
if [[ "$reinstall" == 1 ]]; then
    printf 'Removing %s and its data from simulator %s\n' "$bundle_id" "$udid"
    xcrun simctl terminate "$udid" "$bundle_id" >/dev/null 2>&1 || true
    xcrun simctl uninstall "$udid" "$bundle_id" || true
fi
printf 'Installing %s on simulator %s\n' "$bundle_id" "$udid"
xcrun simctl install "$udid" "$stage"
container="$(xcrun simctl get_app_container "$udid" "$bundle_id" data)"

launch_args=()
if [[ "$push_data" == 1 ]]; then
    push_args=(--udid "$udid")
    if [[ -n "$game_data" ]]; then push_args+=(--game-dir "$game_data"); fi
    if [[ "$force_data" == 1 ]]; then push_args+=(--force); fi
    "$ios_dir/scripts/push-game-data.sh" "${push_args[@]}"
    launch_args+=(--game-dir "$container/Documents/Total Annihilation")
fi
launch_args+=(--skip-intro --mute)
if [[ ${#game_args[@]} -gt 0 ]]; then launch_args+=("${game_args[@]}"); fi
if [[ "$launch" == 0 ]]; then
    printf 'Installed; launch it with: xcrun simctl launch %s %s' "$udid" "$bundle_id"
    printf ' %q' "${launch_args[@]}"
    printf '\n'
    exit 0
fi

printf 'Launching %s with:' "$bundle_id"
printf ' %q' "${launch_args[@]}"
printf '\n'
xcrun simctl launch --terminate-running-process "$udid" "$bundle_id" "${launch_args[@]}"
printf 'The game logs to %s\n' "$container/Library/Application Support/$bundle_id/logs"

if [[ -n "$screenshot" ]]; then
    sleep "$screenshot_delay"
    mkdir -p "$(dirname -- "$screenshot")"
    xcrun simctl io "$udid" screenshot "$screenshot"
fi
