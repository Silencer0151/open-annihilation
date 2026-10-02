#!/usr/bin/env python3
# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

"""Check the names of a build's ctest tests against the naming rule.

The rule (docs/development/conventions.md, Naming) names a test after what
it tests: the module, its group and module, or the area a family of tests
covers, then a case when there are several. This check reads the tests a
configured build registers (ctest --show-only=json-v1) and fails on a name
that:

  form       is not kebab-case: lowercase letters and digits in words joined
             by single hyphens;
  duplicate  another test of the build has too;
  test-word  holds the word test or tests (every ctest is a test), or
             selftest anywhere but at the end: a check's test of itself ends
             in -selftest;
  native     starts with native- but is not a native check, or is one and
             does not start with native-: a native check's command is the
             game (the open-annihilation executable) or a
             tools/check_native_*.py script, which runs it (a test that
             hands the game to another script, such as the extension
             boundary's, is named after what it checks);
  data       ends in -data but does not read the installed game: it is not
             registered as a game-data test, whose skip return code is
             --skip-code (cmake/OaGameData.cmake).

What a name starts with is otherwise left to review. Exit status is 1 when a
name breaks the rule and 2 when the tests cannot be read. --self-test checks
a built-in list of tests.
"""
import argparse
import json
import posixpath
import re
import subprocess
import sys

NAME_RE = re.compile(r"[a-z0-9]+(?:-[a-z0-9]+)*")
TEST_WORDS = frozenset({"test", "tests"})
SELF_TEST_WORD = "selftest"
NATIVE_PREFIX = "native-"
DATA_SUFFIX = "-data"
GAME_EXECUTABLES = frozenset({"open-annihilation", "open-annihilation.exe"})
NATIVE_SCRIPT_RE = re.compile(r"check_native_\w+\.py")
# Bound on the test listing read.
MAX_LISTING_BYTES = 64 << 20


class ListingError(Exception):
    """A test listing that cannot be read."""


def native_check(command):
    """Tells whether a test is a native check: its command is the game, or runs a native check script."""
    names = [posixpath.basename(argument.replace("\\", "/")) for argument in command]
    return bool(names) and (names[0] in GAME_EXECUTABLES or any(NATIVE_SCRIPT_RE.fullmatch(name) for name in names))


def skip_code(test):
    """Returns a test's SKIP_RETURN_CODE property, or None."""
    for prop in test.get("properties", []):
        if prop.get("name") == "SKIP_RETURN_CODE":
            return prop.get("value")
    return None


def findings_of(tests, data_skip_code):
    """Returns the (name, rule, message) findings of a list of ctest tests.

    @param tests the "tests" list of a ctest json-v1 listing
    @param data_skip_code the skip return code of a game-data test
    """
    findings = []
    seen = set()
    for test in tests:
        name = test.get("name", "")
        if name in seen:
            findings.append((name, "duplicate", "another test has this name"))
            continue
        seen.add(name)
        if not NAME_RE.fullmatch(name):
            findings.append((name, "form", "not lowercase words of letters and digits joined by hyphens"))
            continue
        words = name.split("-")
        if TEST_WORDS & set(words):
            findings.append((name, "test-word", "holds the word test; a check's test of itself ends in -selftest"))
        elif SELF_TEST_WORD in words[:-1]:
            findings.append((name, "test-word", "holds selftest before its end"))
        native = native_check(test.get("command", []))
        if name.startswith(NATIVE_PREFIX) and not native:
            findings.append((name, "native", "starts with native- but is not a native check"))
        elif native and not name.startswith(NATIVE_PREFIX):
            findings.append((name, "native", "is a native check but does not start with native-"))
        if name.endswith(DATA_SUFFIX) and skip_code(test) != data_skip_code:
            findings.append((name, "data", "ends in -data but is not registered as a game-data test"))
    return findings


def read_listing(text):
    """Returns the tests of a ctest json-v1 listing."""
    try:
        listing = json.loads(text)
    except ValueError as error:
        raise ListingError(f"the test listing is not JSON: {error}") from error
    tests = listing.get("tests") if isinstance(listing, dict) else None
    if not isinstance(tests, list) or not all(isinstance(test, dict) for test in tests):
        raise ListingError("the test listing holds no list of tests")
    return tests


def list_tests(ctest, build, config):
    """Asks ctest for the tests a build registers."""
    command = [ctest, "--test-dir", build, "--show-only=json-v1"]
    if config:
        command += ["-C", config]
    try:
        result = subprocess.run(command, capture_output=True, text=True, timeout=120, check=False)
    except (OSError, subprocess.SubprocessError) as error:
        raise ListingError(f"cannot run {ctest}: {error}") from error
    if result.returncode != 0 or len(result.stdout) > MAX_LISTING_BYTES:
        raise ListingError(f"{' '.join(command)} failed: {result.stderr.strip()}")
    return read_listing(result.stdout)


SELF_TEST_TESTS = [
    {"name": "sim-detection", "command": ["/b/oa-sim-detection-test"]},
    {"name": "hpi-data", "command": ["/b/oa-hpi-test", "--data"], "properties": [
        {"name": "SKIP_RETURN_CODE", "value": 77}]},
    {"name": "style-ratchet-selftest", "command": ["python3", "check_style_test.py"]},
    {"name": "native-saveload", "command": ["python3", "/s/tools/check_native_saveload.py"]},
    {"name": "native-trace", "command": ["/b/open-annihilation.app/Contents/MacOS/open-annihilation"]},
    {"name": "native-trace-windows", "command": ["C:\\b\\open-annihilation.exe", "--mute"]},
    {"name": "sim-detection", "command": ["/b/oa-sim-detection-test"]},
    {"name": "Sim_Detection", "command": ["/b/x"]},
    {"name": "sim--detection", "command": ["/b/x"]},
    {"name": "ui-hud-clock-test", "command": ["/b/x"]},
    {"name": "doc-links-self-test", "command": ["/b/x"]},
    {"name": "doc-selftest-links", "command": ["/b/x"]},
    {"name": "native-dialogs", "command": ["/b/oa-dialogs-test"]},
    {"name": "frontend-menus", "command": ["/b/open-annihilation", "--check-menus"]},
    {"name": "frontend-art-data", "command": ["/b/oa-art-test", "--data"]},
    {"name": "extension-hooks-game", "command": ["python3", "/s/check_hooks.py", "/b/open-annihilation"]},
    {"name": "frontend-dialogs-data", "command": ["/b/x"], "properties": [
        {"name": "SKIP_RETURN_CODE", "value": 3}]},
]

SELF_TEST_EXPECTED = [
    ("sim-detection", "duplicate"),
    ("Sim_Detection", "form"),
    ("sim--detection", "form"),
    ("ui-hud-clock-test", "test-word"),
    ("doc-links-self-test", "test-word"),
    ("doc-selftest-links", "test-word"),
    ("native-dialogs", "native"),
    ("frontend-menus", "native"),
    ("frontend-art-data", "data"),
    ("frontend-dialogs-data", "data"),
]


def self_test():
    """Checks the built-in tests and compares the findings with the expected ones."""
    got = sorted((name, rule) for name, rule, _ in findings_of(SELF_TEST_TESTS, 77))
    failures = [f"missing finding {item}" for item in sorted(set(SELF_TEST_EXPECTED) - set(got))]
    failures += [f"unexpected finding {item}" for item in sorted(set(got) - set(SELF_TEST_EXPECTED))]
    for broken in ("not json", '{"tests": 3}', '{"tests": [1]}'):
        try:
            read_listing(broken)
            failures.append(f"a broken listing was read: {broken}")
        except ListingError:
            pass
    for failure in failures:
        print(f"check_ctest_names self-test: {failure}")
    if failures:
        return 1
    print(f"check_ctest_names self-test: {len(SELF_TEST_EXPECTED)} findings as expected")
    return 0


def main(argv=None):
    """Checks a build's test names, or the built-in list; returns the exit status."""
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--ctest", default="ctest", help="the ctest executable (default: %(default)s)")
    parser.add_argument("--build", help="the configured build tree whose tests to check")
    parser.add_argument("--config", default="", help="the configuration of a multi-configuration build")
    parser.add_argument("--skip-code", type=int, default=77,
                        help="the skip return code of a game-data test (default: %(default)s)")
    parser.add_argument("--self-test", action="store_true", help="check a built-in list of tests")
    args = parser.parse_args(argv)
    if args.self_test:
        return self_test()
    if not args.build:
        parser.error("--build is required")
    try:
        tests = list_tests(args.ctest, args.build, args.config)
    except ListingError as error:
        print(f"check_ctest_names: {error}", file=sys.stderr)
        return 2
    findings = findings_of(tests, args.skip_code)
    for name, rule, message in findings:
        print(f"{name}: {rule}: {message}")
    if findings:
        print(f"check_ctest_names: {len(findings)} of {len(tests)} test names break the naming rule "
              "(docs/development/conventions.md, Naming)", file=sys.stderr)
        return 1
    print(f"check_ctest_names: {len(tests)} test names keep to the naming rule")
    return 0


if __name__ == "__main__":
    sys.exit(main())
