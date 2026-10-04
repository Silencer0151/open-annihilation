#!/usr/bin/env python3
# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

"""Make the iOS and iPadOS app icon from the game's icon, when the game is built.

The game's icon (branding/open-annihilation-icon.png) is the riveted metal plate
with the gold "OA", with rounded corners of its own inside a transparent margin.
iOS rounds every app icon itself, so the app icon is the plate alone: a square,
opaque, 1024x1024 image cut from just inside the plate's edge and filling the
whole square, so the system's rounded mask falls on the metal and no second
corner shows. Where the plate's own rounded corner leaves the square's corner
uncovered (inside the part the mask removes), the plate's edge colour carries on
instead of the transparent margin.

Writes an asset catalogue that holds one image set, AppIcon, in the single
1024x1024 size the asset compiler turns into every size iOS needs. The icon is
made from the branding when the game is built and never stored in the
repository. Uses sips, which macOS has, to cut and scale the image, and no other
image library.

  python3 platforms/ios/tools/make_app_icon.py --source branding/open-annihilation-icon.png \\
      --catalogue build-ios-sim/Assets.xcassets
"""

from __future__ import annotations

import argparse
import json
import struct
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path

ICON_SIZE = 1024
# How far inside the plate's edge the square is cut, as a share of the plate's width: enough
# to leave the plate's bevelled rim and its corners inside the system's mask, little enough
# to keep the rivets around the edge whole.
EDGE_INSET = 0.01
# A pixel at least this opaque counts as the plate when its edges are found.
PLATE_ALPHA = 128


class Bitmap:
    """An image as rows of 8-bit RGBA pixels, top row first."""

    def __init__(self, width: int, height: int, pixels: bytearray) -> None:
        self.width = width
        self.height = height
        self.pixels = pixels

    def alpha(self, x: int, y: int) -> int:
        return self.pixels[(y * self.width + x) * 4 + 3]

    def rgb(self, x: int, y: int) -> tuple[int, int, int]:
        at = (y * self.width + x) * 4
        return self.pixels[at], self.pixels[at + 1], self.pixels[at + 2]


def run_sips(*arguments: str) -> None:
    """Runs sips, failing with its own message when it fails."""
    result = subprocess.run(["sips", *arguments], capture_output=True, text=True)
    if result.returncode != 0:
        raise SystemExit(f"make_app_icon.py: sips {' '.join(arguments)} failed:\n{result.stderr}")


def mask_shift(mask: int) -> int:
    """Returns how far a channel's bit mask sits above bit 0."""
    shift = 0
    while mask and not mask & 1:
        mask >>= 1
        shift += 1
    return shift


def read_bitmap(path: Path) -> Bitmap:
    """Reads the 32-bit bitmap with an alpha channel that sips writes."""
    data = path.read_bytes()
    if data[:2] != b"BM":
        raise SystemExit(f"make_app_icon.py: {path} is not a bitmap")
    offset = struct.unpack_from("<I", data, 10)[0]
    header_size, width, height, _, bits, compression = struct.unpack_from("<IiiHHI", data, 14)
    if bits != 32 or compression not in (0, 3) or header_size < 56:
        raise SystemExit(f"make_app_icon.py: {path} is not a 32-bit bitmap with an alpha channel")
    if compression == 3:
        red, green, blue, alpha = struct.unpack_from("<IIII", data, 54)
    else:
        red, green, blue, alpha = 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000
    shifts = [mask_shift(mask) for mask in (red, green, blue, alpha)]
    top_down = height < 0
    height = abs(height)
    pixels = bytearray(width * height * 4)
    for row in range(height):
        source_row = row if top_down else height - 1 - row
        start = offset + source_row * width * 4
        words = struct.unpack_from(f"<{width}I", data, start)
        out = row * width * 4
        for word in words:
            pixels[out] = (word >> shifts[0]) & 0xFF
            pixels[out + 1] = (word >> shifts[1]) & 0xFF
            pixels[out + 2] = (word >> shifts[2]) & 0xFF
            pixels[out + 3] = (word >> shifts[3]) & 0xFF if alpha else 0xFF
            out += 4
    return Bitmap(width, height, pixels)


def plate_square(image: Bitmap) -> tuple[int, int, int]:
    """Returns the square cut from just inside the plate: its left, top and side."""
    rows = [y for y in range(image.height) if any(image.alpha(x, y) >= PLATE_ALPHA for x in range(0, image.width, 2))]
    columns = [x for x in range(image.width) if any(image.alpha(x, y) >= PLATE_ALPHA for y in range(0, image.height, 2))]
    if not rows or not columns:
        raise SystemExit("make_app_icon.py: the icon has no opaque plate")
    left, right, top, bottom = columns[0], columns[-1] + 1, rows[0], rows[-1] + 1
    side = min(right - left, bottom - top)
    inset = round(side * EDGE_INSET)
    side -= 2 * inset
    return left + (right - left - side) // 2, top + (bottom - top - side) // 2, side


def fill_corners(image: Bitmap) -> bytes:
    """Returns the image's RGB rows, every pixel opaque: a pixel the plate does not cover
    fully takes, behind it, the colour of the first fully covered pixel on the way to the
    image's centre, so the plate's edge carries on into the corner."""
    width, height = image.width, image.height
    centre_x, centre_y = (width - 1) / 2, (height - 1) / 2
    out = bytearray(width * height * 3)
    for y in range(height):
        for x in range(width):
            at = (y * width + x) * 4
            red, green, blue, alpha = image.pixels[at : at + 4]
            if alpha < 255:
                back = (0, 0, 0)
                steps = int(max(abs(centre_x - x), abs(centre_y - y)))
                for step in range(1, steps + 1):
                    sx = round(x + (centre_x - x) * step / steps)
                    sy = round(y + (centre_y - y) * step / steps)
                    if image.alpha(sx, sy) == 255:
                        back = image.rgb(sx, sy)
                        break
                red = (red * alpha + back[0] * (255 - alpha)) // 255
                green = (green * alpha + back[1] * (255 - alpha)) // 255
                blue = (blue * alpha + back[2] * (255 - alpha)) // 255
            put = (y * width + x) * 3
            out[put : put + 3] = bytes((red, green, blue))
    return bytes(out)


def write_png(path: Path, width: int, height: int, rgb: bytes) -> None:
    """Writes 8-bit RGB rows as a PNG file without an alpha channel."""

    def chunk(kind: bytes, body: bytes) -> bytes:
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))

    stride = width * 3
    raw = b"".join(b"\x00" + rgb[row * stride : (row + 1) * stride] for row in range(height))
    path.write_bytes(
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(raw, 9))
        + chunk(b"IEND", b"")
    )


def write_json(path: Path, value: dict) -> None:
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--source", type=Path, required=True, help="the game's icon (a PNG file)")
    parser.add_argument("--catalogue", type=Path, required=True, help="the asset catalogue to write (a .xcassets folder)")
    arguments = parser.parse_args(argv)

    with tempfile.TemporaryDirectory(prefix="oa-app-icon-") as scratch_name:
        scratch = Path(scratch_name)
        whole = scratch / "whole.bmp"
        run_sips("-s", "format", "bmp", str(arguments.source), "--out", str(whole))
        left, top, side = plate_square(read_bitmap(whole))
        plate = scratch / "plate.png"
        run_sips("-c", str(side), str(side), "--cropOffset", str(top), str(left), str(arguments.source), "--out", str(plate))
        scaled = scratch / "icon.bmp"
        run_sips("-z", str(ICON_SIZE), str(ICON_SIZE), "-s", "format", "bmp", str(plate), "--out", str(scaled))
        icon = read_bitmap(scaled)
        if (icon.width, icon.height) != (ICON_SIZE, ICON_SIZE):
            raise SystemExit(f"make_app_icon.py: sips made a {icon.width}x{icon.height} icon")
        rgb = fill_corners(icon)

    icon_set = arguments.catalogue / "AppIcon.appiconset"
    icon_set.mkdir(parents=True, exist_ok=True)
    write_png(icon_set / "AppIcon.png", ICON_SIZE, ICON_SIZE, rgb)
    write_json(arguments.catalogue / "Contents.json", {"info": {"author": "xcode", "version": 1}})
    write_json(
        icon_set / "Contents.json",
        {
            "images": [
                {"filename": "AppIcon.png", "idiom": "universal", "platform": "ios", "size": f"{ICON_SIZE}x{ICON_SIZE}"}
            ],
            "info": {"author": "xcode", "version": 1},
        },
    )
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
