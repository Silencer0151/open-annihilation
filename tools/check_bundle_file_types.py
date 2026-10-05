#!/usr/bin/env python3
# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

"""Check that the macOS and iOS bundles declare the .oamod mod package as the game's own type.

Reads the two Info.plist templates (src/app/Info.plist.in and platforms/ios/Info.plist.in, or
the two named) as property lists and fails unless each:

  exports    the type net.coreprime.open-annihilation.oamod (UTExportedTypeDeclarations),
             named "Open Annihilation mod", conforming to public.data alone, with the file
             name extension oamod and the MIME type application/x-oamod, as the run-time
             registration (src/platform/file-types) names them;
  opens      that type as its owner (CFBundleDocumentTypes: LSHandlerRank Owner, the role
             Viewer, the type alone in LSItemContentTypes);
  icon       on macOS, gives the type the icon the system draws from the app's icon
             (CFBundleTypeIconSystemGenerated, 1) and the bundle's own icon file before
             macOS 11 (CFBundleTypeIconFile, the same as CFBundleIconFile), an icon file that
             cmake/OaGameBundle.cmake takes from branding/ and that is there; on iOS, names no
             icon file, since iOS draws document icons from the app's icon;
  in place   on iOS, still opens documents in place (LSSupportsOpeningDocumentsInPlace),
             which the Files app needs, and still declares the game's archive type.

Exit status is 1 on any finding and 2 when a file cannot be read.
"""
import argparse
import plistlib
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TYPE_ID = "net.coreprime.open-annihilation.oamod"
TYPE_NAME = "Open Annihilation mod"
EXTENSION = "oamod"
MIME_TYPE = "application/x-oamod"
CONFORMS_TO = ["public.data"]
ARCHIVE_TYPE_ID = "net.coreprime.open-annihilation.game-archive"
ICON_PLACEHOLDER = "${MACOSX_BUNDLE_ICON_FILE}"
# The run-time registration's constants, which the bundles must agree with.
FILE_TYPES_HEADER = Path("src/platform/file-types/include/oa/platform/file_types.hpp")
CONSTANT_RE = re.compile(r'inline constexpr std::string_view (\w+) = "([^"]*)";')
ICON_FILE_RE = re.compile(r"MACOSX_BUNDLE_ICON_FILE\s+(\S+)")
# Bound on each file read.
MAX_FILE_BYTES = 1 << 20


class ReadError(Exception):
    """A file that cannot be read."""


def read_bytes(path):
    """Reads a file of at most MAX_FILE_BYTES bytes."""
    try:
        with open(path, "rb") as stream:
            data = stream.read(MAX_FILE_BYTES + 1)
    except OSError as error:
        raise ReadError(f"{path}: {error.strerror or error}") from error
    if len(data) > MAX_FILE_BYTES:
        raise ReadError(f"{path}: larger than {MAX_FILE_BYTES} bytes")
    return data


def read_plist(path):
    """Reads a property list template; its ${...} and @...@ placeholders are plain strings."""
    try:
        value = plistlib.loads(read_bytes(path))
    except (plistlib.InvalidFileException, ValueError) as error:
        raise ReadError(f"{path}: not a property list: {error}") from error
    if not isinstance(value, dict):
        raise ReadError(f"{path}: not a dictionary at its top")
    return value


def check_exported_type(name, plist, findings):
    """Checks that a bundle exports the mod package's type as the registration names it."""
    exported = [entry for entry in plist.get("UTExportedTypeDeclarations", [])
                if isinstance(entry, dict) and entry.get("UTTypeIdentifier") == TYPE_ID]
    if len(exported) != 1:
        findings.append(f"{name}: exports: {TYPE_ID} is declared {len(exported)} times, not once")
        return
    entry = exported[0]
    tags = entry.get("UTTypeTagSpecification", {})
    expected = {
        "UTTypeDescription": (entry.get("UTTypeDescription"), TYPE_NAME),
        "UTTypeConformsTo": (entry.get("UTTypeConformsTo"), CONFORMS_TO),
        "public.filename-extension": (tags.get("public.filename-extension"), [EXTENSION]),
        "public.mime-type": (tags.get("public.mime-type"), [MIME_TYPE]),
    }
    for key, (found, wanted) in expected.items():
        if found != wanted:
            findings.append(f"{name}: exports: {key} is {found!r}, not {wanted!r}")


def check_document_type(name, plist, findings, macos):
    """Checks that a bundle opens the mod package's type as its owner, with the icon keys its system takes."""
    documents = [entry for entry in plist.get("CFBundleDocumentTypes", [])
                 if isinstance(entry, dict) and TYPE_ID in entry.get("LSItemContentTypes", [])]
    if len(documents) != 1:
        findings.append(f"{name}: opens: {len(documents)} document types name {TYPE_ID}, not one")
        return
    entry = documents[0]
    expected = {
        "CFBundleTypeName": TYPE_NAME,
        "CFBundleTypeRole": "Viewer",
        "LSHandlerRank": "Owner",
        "LSItemContentTypes": [TYPE_ID],
    }
    for key, wanted in expected.items():
        if entry.get(key) != wanted:
            findings.append(f"{name}: opens: {key} is {entry.get(key)!r}, not {wanted!r}")
    if macos:
        generated = entry.get("CFBundleTypeIconSystemGenerated")
        # Xcode writes the integer 1; the boolean true is read the same way.
        if generated is not True and not (type(generated) is int and generated == 1):
            findings.append(f"{name}: icon: CFBundleTypeIconSystemGenerated is {generated!r}, not 1")
        icon = entry.get("CFBundleTypeIconFile")
        if icon != ICON_PLACEHOLDER or plist.get("CFBundleIconFile") != ICON_PLACEHOLDER:
            findings.append(f"{name}: icon: CFBundleTypeIconFile is {icon!r} and CFBundleIconFile "
                            f"{plist.get('CFBundleIconFile')!r}, not both {ICON_PLACEHOLDER}")
    else:
        for key in ("CFBundleTypeIconFile", "CFBundleTypeIconFiles", "CFBundleTypeIconSystemGenerated"):
            if key in entry:
                findings.append(f"{name}: icon: {key} is given; iOS draws the icon from the app's")


def check_bundle_icon(root, findings):
    """Checks that the icon file the macOS bundle names is the one branding/ holds."""
    bundle = root / "cmake" / "OaGameBundle.cmake"
    match = ICON_FILE_RE.search(read_bytes(bundle).decode("utf-8"))
    if match is None:
        findings.append(f"{bundle}: icon: MACOSX_BUNDLE_ICON_FILE is not set")
        return
    icon = root / "branding" / match.group(1)
    if not icon.is_file():
        findings.append(f"{bundle}: icon: the bundle's icon file {match.group(1)} is not in branding/")


def check_constants(root, findings):
    """Checks that the run-time registration names the type as the bundles do."""
    header = root / FILE_TYPES_HEADER
    constants = dict(CONSTANT_RE.findall(read_bytes(header).decode("utf-8")))
    expected = {"mod_extension": EXTENSION, "mod_mime_type": MIME_TYPE, "mod_type_name": TYPE_NAME,
                "desktop_id": TYPE_ID.rsplit(".", 1)[0]}
    for key, wanted in expected.items():
        if constants.get(key) != wanted:
            findings.append(f"{header}: exports: {key} is {constants.get(key)!r}, not {wanted!r}")


def check(root, macos_path, ios_path):
    """Returns the findings of every check."""
    findings = []
    macos = read_plist(macos_path)
    ios = read_plist(ios_path)
    check_exported_type(str(macos_path), macos, findings)
    check_document_type(str(macos_path), macos, findings, macos=True)
    check_exported_type(str(ios_path), ios, findings)
    check_document_type(str(ios_path), ios, findings, macos=False)
    if ios.get("LSSupportsOpeningDocumentsInPlace") is not True:
        findings.append(f"{ios_path}: in place: LSSupportsOpeningDocumentsInPlace is not true")
    archives = [entry for entry in ios.get("UTImportedTypeDeclarations", [])
                if isinstance(entry, dict) and entry.get("UTTypeIdentifier") == ARCHIVE_TYPE_ID]
    if len(archives) != 1:
        findings.append(f"{ios_path}: in place: {ARCHIVE_TYPE_ID} is not declared once")
    if macos.get("CFBundleIdentifier") != TYPE_ID.rsplit(".", 1)[0]:
        findings.append(f"{macos_path}: exports: the type is not named under the bundle's identifier")
    check_bundle_icon(root, findings)
    check_constants(root, findings)
    return findings


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("macos_plist", nargs="?", type=Path, default=ROOT / "src" / "app" / "Info.plist.in",
                        help="the macOS bundle's Info.plist template")
    parser.add_argument("ios_plist", nargs="?", type=Path, default=ROOT / "platforms" / "ios" / "Info.plist.in",
                        help="the iOS bundle's Info.plist template")
    parser.add_argument("--root", type=Path, default=ROOT, help="the engine tree")
    arguments = parser.parse_args(argv)
    try:
        findings = check(arguments.root, arguments.macos_plist, arguments.ios_plist)
    except ReadError as error:
        print(f"check_bundle_file_types: {error}", file=sys.stderr)
        return 2
    for finding in findings:
        print(finding, file=sys.stderr)
    if findings:
        print(f"check_bundle_file_types: {len(findings)} finding(s)", file=sys.stderr)
        return 1
    print("check_bundle_file_types: both bundles declare and open the .oamod type")
    return 0


if __name__ == "__main__":
    sys.exit(main())
