#!/usr/bin/env python3
# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

"""Check that the automation endpoint hands out the frame the game presented.

The game runs through its main loop on the dummy SDL drivers with --fark, as
check_native_automation.py starts it, and shows the main menu, twice:

- in a 640x480 window, where the menu's canvas fills the window one to one:
  a presented frame is the window's size, lies over the whole window, and
  shows what the composed frame holds, but for the few pixels the menu
  animates from one frame to the next; its PNG form holds the same picture,
  each row stored unfiltered; a region of the canvas, and the same region of
  the window, give those pixels alone; the hash form gives the 64-bit FNV-1a
  hash and the mean colour of a region's pixels;
- in a 1280x720 window, where the canvas is drawn larger between bars: a
  presented frame is the rectangle hello names as the canvas's, a region of
  the canvas covers every pixel of the frame that shows part of it, a region
  of the window maps into the frame, and one over a bar lies outside it.

A frame request with an unknown source, format or space, a malformed region,
or a region outside the frame is refused bad_request, naming the field.
"""
import argparse
import math
from pathlib import Path
import struct
import sys
import tempfile
import time
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parent))
import check_native_automation as automation  # noqa: E402
from check_native_automation import AutomationFailure, Client, Game, refused  # noqa: E402

# The game's canvas, in its own pixels.
CANVAS = (640, 480)
# The window in which the canvas is drawn one to one, and one in which it is drawn larger.
ONE_TO_ONE = "640x480"
LETTERBOXED = "1280x720"
# The share of a menu frame's pixels that may change from one frame to the
# next: the main menu animates a few near its top.
ANIMATED_SHARE = 0.02
# Fewer colours than this in a frame of the main menu would be a blank frame.
FEWEST_COLOURS = 16
# A region of the main menu's canvas below its animation, [x, y, w, h].
STILL_REGION = [0, 240, 640, 240]
# The 64-bit FNV-1a hash's starting value and multiplier.
FNV_OFFSET = 0xcbf29ce484222325
FNV_PRIME = 0x100000001b3
PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


def exchange(client, op, **fields):
    """Sends a request and returns its answer's JSON object and payload."""
    client.next_id += 1
    client.send_raw(automation.encode({"id": client.next_id, "op": op, **fields}))
    answer = client.read_frame()
    if answer is None:
        raise AutomationFailure(f"the endpoint closed the connection instead of answering {op}")
    message, payload = answer
    if message.get("id") != client.next_id:
        raise AutomationFailure(f"the answer to {op} names another request: {message}")
    return message, payload


def frame(client, **fields):
    """Asks for a frame; returns the answer and its payload, checked against what the answer says of it."""
    message, payload = exchange(client, "frame", **fields)
    if not message.get("ok") or not isinstance(message.get("frame"), int) or \
            not isinstance(message.get("tick"), int):
        raise AutomationFailure(f"frame {fields} was answered {message}")
    if fields.get("format") == "hash":
        if payload or "image" in message or len(message.get("hash", "")) != 16 or \
                len(message.get("mean", [])) != 3:
            raise AutomationFailure(f"frame {fields} was answered {message} and {len(payload)} bytes")
        return message, payload
    image = message.get("image", {})
    form = fields.get("format", "rgb")
    if image.get("format") != form or image.get("source") != fields.get("source", "presented") or \
            image.get("region") != fields.get("region") or \
            form == "rgb" and len(payload) != image.get("width", 0) * image.get("height", 0) * 3:
        raise AutomationFailure(f"frame {fields} was answered {message} and {len(payload)} bytes")
    return message, payload


def fnv1a(data):
    """The 64-bit FNV-1a hash of bytes."""
    value = FNV_OFFSET
    for byte in data:
        value = ((value ^ byte) * FNV_PRIME) & 0xffffffffffffffff
    return value


def mean_colour(pixels):
    """The mean of each channel of RGB pixels, rounded down."""
    count = len(pixels) // 3
    return [sum(pixels[channel::3]) // count for channel in range(3)]


def png_pixels(data):
    """The size and RGB pixels of a PNG file the endpoint wrote: 8-bit RGB, not interlaced, rows unfiltered."""
    if not data.startswith(PNG_SIGNATURE):
        raise AutomationFailure("the PNG form does not start with PNG's signature")
    at, header, image_data = len(PNG_SIGNATURE), None, b""
    while at < len(data):
        length = int.from_bytes(data[at:at + 4], "big")
        kind, body = data[at + 4:at + 8], data[at + 8:at + 8 + length]
        if zlib.crc32(kind + body) != int.from_bytes(data[at + 8 + length:at + 12 + length], "big"):
            raise AutomationFailure(f"the PNG form's {kind!r} chunk fails its CRC")
        if kind == b"IHDR":
            header = struct.unpack(">IIBBBBB", body)
        elif kind == b"IDAT":
            image_data += body
        at += 12 + length
    if header is None or header[2:4] != (8, 2) or header[6] != 0:
        raise AutomationFailure(f"the PNG form's header is {header}, not 8-bit RGB without interlacing")
    width, height = header[0], header[1]
    rows = zlib.decompress(image_data)
    stride = width * 3 + 1
    if len(rows) != stride * height or any(rows[row * stride] != 0 for row in range(height)):
        raise AutomationFailure("the PNG form's rows are not stored unfiltered")
    return width, height, b"".join(rows[row * stride + 1:(row + 1) * stride] for row in range(height))


def changed_pixels(first, second):
    """How many pixels of two RGB pictures of one size differ."""
    return sum(first[at:at + 3] != second[at:at + 3] for at in range(0, len(first), 3))


def crop(pixels, width, region):
    """The RGB pixels of a region [x, y, w, h] of a picture width pixels wide."""
    x, y, w, h = region
    return b"".join(pixels[((y + row) * width + x) * 3:((y + row) * width + x + w) * 3] for row in range(h))


def expect_refused(client, fields, field):
    """Raises AutomationFailure unless a frame request is refused bad_request naming the field."""
    message, _ = exchange(client, "frame", **fields)
    if not refused(message, "bad_request") or message.get("error", {}).get("field") != field:
        raise AutomationFailure(f"frame {fields} was answered {message}")


def check_one_to_one(client, hello):
    """The frame of a window the canvas fills one to one."""
    window, rect = hello.get("window", {}), hello.get("canvas", {}).get("rect")
    if (window.get("width"), window.get("height")) != CANVAS or rect != [0, 0, *CANVAS]:
        raise AutomationFailure(f"hello gave the window {window} and the canvas at {rect}")
    presented, pixels = frame(client)
    image = presented["image"]
    if (image.get("width"), image.get("height")) != CANVAS or image.get("window_rect") != rect:
        raise AutomationFailure(f"the presented frame is {image}")
    if len({pixels[at:at + 3] for at in range(0, len(pixels), 3)}) < FEWEST_COLOURS:
        raise AutomationFailure("the presented frame of the main menu is blank")
    composed, composed_pixels = frame(client, source="composed")
    image = composed["image"]
    if (image.get("width"), image.get("height")) != CANVAS or "window_rect" in image or \
            composed["frame"] <= presented["frame"]:
        raise AutomationFailure(f"the composed frame is {composed}, after {presented}")
    allowed = ANIMATED_SHARE * CANVAS[0] * CANVAS[1]
    changed = changed_pixels(pixels, composed_pixels)
    if changed > allowed:
        raise AutomationFailure(f"the presented frame differs from the composed one in {changed} pixels")

    _, data = frame(client, format="png")
    width, height, png_rgb = png_pixels(data)
    changed = changed_pixels(png_rgb, pixels)
    if (width, height) != CANVAS or changed > allowed:
        raise AutomationFailure(f"the PNG form is {width}x{height} and differs in {changed} pixels")

    game_region, still = frame(client, region=STILL_REGION)
    window_region, still_window = frame(client, region=STILL_REGION, space="window")
    for answer in (game_region, window_region):
        if (answer["image"].get("width"), answer["image"].get("height")) != tuple(STILL_REGION[2:]):
            raise AutomationFailure(f"a region of the frame was answered {answer}")
    if still != still_window or still != crop(pixels, CANVAS[0], STILL_REGION):
        raise AutomationFailure(f"the region {STILL_REGION} does not hold the frame's pixels there")
    hashed, _ = frame(client, region=STILL_REGION, format="hash")
    if hashed.get("hash") != f"{fnv1a(still):016x}" or hashed.get("mean") != mean_colour(still) or \
            (hashed.get("width"), hashed.get("height")) != tuple(STILL_REGION[2:]) or \
            hashed.get("region") != STILL_REGION or hashed.get("window_rect") != rect:
        raise AutomationFailure(f"the region's hash was answered {hashed}, its pixels hash to "
                                f"{fnv1a(still):016x} with the mean {mean_colour(still)}")

    for fields, field in (({"source": "drawn"}, "source"), ({"format": "bmp"}, "format"),
                          ({"space": "screen"}, "space"), ({"region": [0, 0, 640]}, "region"),
                          ({"region": [0, 0, 0, 10]}, "region"), ({"region": [600, 0, 41, 1]}, "region")):
        expect_refused(client, fields, field)


def check_letterboxed(client, hello):
    """The frame of a window the canvas is drawn larger in, between bars."""
    window, rect = hello.get("window", {}), hello.get("canvas", {}).get("rect", [])
    if len(rect) != 4 or rect[0] <= 0 or rect[2] <= CANVAS[0] or rect[0] + rect[2] > window.get("width", 0) \
            or rect[1] + rect[3] > window.get("height", 0):
        raise AutomationFailure(f"hello gave the window {window} and the canvas at {rect}")
    presented, pixels = frame(client)
    image = presented["image"]
    if [image.get("width"), image.get("height")] != rect[2:] or image.get("window_rect") != rect:
        raise AutomationFailure(f"the presented frame is {image}, the canvas lies at {rect}")
    whole, _ = frame(client, region=[0, 0, *CANVAS], format="hash")
    if [whole.get("width"), whole.get("height")] != rect[2:]:
        raise AutomationFailure(f"the canvas's region was answered {whole}")
    corner, _ = frame(client, region=[0, 0, 1, 1], format="hash")
    covered = [math.ceil(rect[2] / CANVAS[0]), math.ceil(rect[3] / CANVAS[1])]
    if [corner.get("width"), corner.get("height")] != covered:
        raise AutomationFailure(f"the canvas's first pixel was answered {corner}, not {covered} pixels")
    bottom = [rect[0], rect[1] + rect[3] - 10, 10, 10]
    _, seen = frame(client, region=bottom, space="window")
    if seen != crop(pixels, rect[2], [0, rect[3] - 10, 10, 10]):
        raise AutomationFailure(f"the window's region {bottom} does not hold the frame's pixels there")
    expect_refused(client, {"region": [0, 0, rect[0], 10], "space": "window"}, "region")


def run(native, game_dir, workdir, resolution, check):
    """Starts the game in a window of a size, waits for the main menu, runs a check and quits."""
    game = Game(native, game_dir, workdir, ["--resolution", resolution])
    client = None
    try:
        endpoint = game.endpoint()
        client = Client(endpoint["address"])
        hello = client.request("hello", versions=[1], client="check", token=endpoint["token"])
        deadline = time.monotonic() + automation.TIMEOUT
        while client.request("screen").get("screen") != "main_menu":
            if time.monotonic() > deadline:
                raise AutomationFailure("the main menu did not show")
            time.sleep(0.1)
        check(client, hello)
        if not client.request("quit").get("ok"):
            raise AutomationFailure("quit was refused")
        game.wait_end()
    except (AutomationFailure, OSError, ValueError, zlib.error) as failure:
        game.stop()
        game.take_output()
        print("\n".join(f"game: {line}" for line in game.lines))
        if isinstance(failure, AutomationFailure):
            raise AutomationFailure(f"{resolution}: {failure}") from failure
        raise AutomationFailure(f"{resolution}: {type(failure).__name__}: {failure}") from failure
    finally:
        if client is not None:
            client.close()


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--native", required=True, type=Path, help="the game's executable")
    parser.add_argument("--game-dir", required=True, type=Path, help="the installed game")
    parser.add_argument("--scratch-root", type=Path, help="where the check's own folder is made")
    args = parser.parse_args(argv)
    if args.scratch_root:
        args.scratch_root.mkdir(parents=True, exist_ok=True)
    try:
        for resolution, check in ((ONE_TO_ONE, check_one_to_one), (LETTERBOXED, check_letterboxed)):
            with tempfile.TemporaryDirectory(dir=args.scratch_root) as scratch:
                run(args.native, args.game_dir, Path(scratch), resolution, check)
    except AutomationFailure as failure:
        print(f"native-automation-frame: {failure}", file=sys.stderr)
        return 1
    print("native-automation-frame: the endpoint handed out the frames the game presented")
    return 0


if __name__ == "__main__":
    sys.exit(main())
