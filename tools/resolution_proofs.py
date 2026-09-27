#!/usr/bin/env python3
# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

"""Draw match-HUD composition proofs from oa::ui::display_layout::make_match_layout."""

from __future__ import annotations

import struct
import zlib
from pathlib import Path

SOURCE_W, SOURCE_H = 640, 480
SOURCE_LEFT, SOURCE_TOP, SOURCE_BOTTOM = 128, 32, 32
SOURCE_BF_W = SOURCE_W - SOURCE_LEFT
SOURCE_BF_H = SOURCE_H - SOURCE_TOP - SOURCE_BOTTOM

RESOLUTIONS = [
    (640, 480),
    (800, 600),
    (1024, 768),
    (1280, 720),
    (1280, 1024),
    (1920, 1080),
    (2560, 1440),
    (3840, 2160),
    (7680, 4320),
]

# SIDEDATA / HUD source rects the game uses (640x480).
RADAR = (0, 0, 126, 126)
LEFT_STRIP = (0, 0, 128, 480)
HEADER = (0, 128, 128, 352)
TOP_BAR = (128, 0, 512, 32)
BOTTOM_BAR = (128, 448, 512, 32)
METAL_BAR = (218, 12, 127, 11)
ENERGY_BAR = (471, 12, 127, 11)
SCORE = (129, 347, 511, 133)


def make_match_layout(width: int, height: int) -> dict:
    width = max(width, SOURCE_LEFT + 32)
    height = max(height, SOURCE_TOP + SOURCE_BOTTOM + 32)
    scale = min(width / SOURCE_W, height / SOURCE_H)
    left = max(1, int(round(SOURCE_LEFT * scale)))
    top = max(1, int(round(SOURCE_TOP * scale)))
    bottom = max(1, int(round(SOURCE_BOTTOM * scale)))
    if left >= width:
        left = max(1, width // 4)
    if top + bottom >= height:
        top = max(1, height // 16)
        bottom = max(1, height // 16)
    hud_width = max(left + 32, int(round(SOURCE_W * scale)))
    hud_height = max(top + bottom + 32, int(round(SOURCE_H * scale)))
    return {
        "width": width,
        "height": height,
        "left": left,
        "top": top,
        "bottom": bottom,
        "hud_width": hud_width,
        "hud_height": hud_height,
        "scale": scale,
        "bf_w": width - left,
        "bf_h": height - top - bottom,
    }


def source_to_canvas(layout: dict, x: int, y: int) -> tuple[int, int]:
    return int(round(x * layout["scale"])), int(round(y * layout["scale"]))


def fill_rect(buf: bytearray, w: int, h: int, x: int, y: int, rw: int, rh: int, rgb: tuple[int, int, int]) -> None:
    x0 = max(0, x)
    y0 = max(0, y)
    x1 = min(w, x + rw)
    y1 = min(h, y + rh)
    r, g, b = rgb
    for yy in range(y0, y1):
        row = yy * w * 3
        for xx in range(x0, x1):
            i = row + xx * 3
            buf[i] = r
            buf[i + 1] = g
            buf[i + 2] = b


def blend_rect(buf: bytearray, w: int, h: int, x: int, y: int, rw: int, rh: int, rgb: tuple[int, int, int], a: float) -> None:
    x0 = max(0, x)
    y0 = max(0, y)
    x1 = min(w, x + rw)
    y1 = min(h, y + rh)
    r, g, b = rgb
    ia = 1.0 - a
    for yy in range(y0, y1):
        row = yy * w * 3
        for xx in range(x0, x1):
            i = row + xx * 3
            buf[i] = int(buf[i] * ia + r * a)
            buf[i + 1] = int(buf[i + 1] * ia + g * a)
            buf[i + 2] = int(buf[i + 2] * ia + b * a)


def write_png(path: Path, w: int, h: int, rgb: bytes) -> None:
    def chunk(tag: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    raw = bytearray()
    stride = w * 3
    for y in range(h):
        raw.append(0)
        raw.extend(rgb[y * stride : (y + 1) * stride])
    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 6))
    png += chunk(b"IEND", b"")
    path.write_bytes(png)


# 5x7 glyphs, one byte per row (low 5 bits).
_FONT = {
    " ": [0, 0, 0, 0, 0, 0, 0],
    ".": [0, 0, 0, 0, 0, 0, 2],
    ":": [0, 2, 0, 0, 2, 0, 0],
    "+": [0, 2, 7, 2, 0, 0, 0],
    "/": [1, 1, 2, 2, 4, 4, 8],
    "X": [17, 10, 4, 4, 10, 17, 0],
    "=": [0, 0, 15, 0, 15, 0, 0],
    "0": [14, 17, 19, 21, 25, 17, 14],
    "1": [4, 12, 4, 4, 4, 4, 14],
    "2": [14, 17, 1, 2, 4, 8, 31],
    "3": [14, 17, 1, 6, 1, 17, 14],
    "4": [2, 6, 10, 18, 31, 2, 2],
    "5": [31, 16, 30, 1, 1, 17, 14],
    "6": [6, 8, 16, 30, 17, 17, 14],
    "7": [31, 1, 2, 4, 8, 8, 8],
    "8": [14, 17, 17, 14, 17, 17, 14],
    "9": [14, 17, 17, 15, 1, 2, 12],
    "A": [14, 17, 17, 31, 17, 17, 17],
    "B": [30, 17, 17, 30, 17, 17, 30],
    "C": [14, 17, 16, 16, 16, 17, 14],
    "D": [30, 17, 17, 17, 17, 17, 30],
    "E": [31, 16, 16, 30, 16, 16, 31],
    "F": [31, 16, 16, 30, 16, 16, 16],
    "G": [14, 17, 16, 19, 17, 17, 14],
    "H": [17, 17, 17, 31, 17, 17, 17],
    "I": [14, 4, 4, 4, 4, 4, 14],
    "K": [17, 18, 20, 24, 20, 18, 17],
    "L": [16, 16, 16, 16, 16, 16, 31],
    "M": [17, 27, 21, 21, 17, 17, 17],
    "N": [17, 25, 21, 19, 17, 17, 17],
    "O": [14, 17, 17, 17, 17, 17, 14],
    "P": [30, 17, 17, 30, 16, 16, 16],
    "R": [30, 17, 17, 30, 20, 18, 17],
    "S": [14, 17, 16, 14, 1, 17, 14],
    "T": [31, 4, 4, 4, 4, 4, 4],
    "U": [17, 17, 17, 17, 17, 17, 14],
    "V": [17, 17, 17, 17, 10, 10, 4],
    "W": [17, 17, 17, 21, 21, 27, 17],
    "Y": [17, 17, 10, 4, 4, 4, 4],
}


def draw_text(buf: bytearray, w: int, h: int, x: int, y: int, text: str, rgb: tuple[int, int, int], scale: int = 1) -> None:
    r, g, b = rgb
    cx = x
    for ch in text.upper():
        rows = _FONT.get(ch, [31, 17, 17, 17, 17, 17, 31])
        for row, line in enumerate(rows):
            for col in range(5):
                if line & (16 >> col):
                    for sy in range(scale):
                        for sx in range(scale):
                            px = cx + col * scale + sx
                            py = y + row * scale + sy
                            if 0 <= px < w and 0 <= py < h:
                                i = (py * w + px) * 3
                                buf[i] = r
                                buf[i + 1] = g
                                buf[i + 2] = b
        cx += 6 * scale


def map_rect(layout: dict, sx: int, sy: int, sw: int, sh: int) -> tuple[int, int, int, int]:
    x0, y0 = source_to_canvas(layout, sx, sy)
    x1, y1 = source_to_canvas(layout, sx + sw, sy + sh)
    return x0, y0, max(1, x1 - x0), max(1, y1 - y0)


def compose(layout: dict) -> tuple[int, int, bytearray, str]:
    w, h = layout["width"], layout["height"]
    # Downscale huge canvases so 8K proofs stay reviewable; labels still use native bounds.
    preview = 1
    while w // preview > 1920 or h // preview > 1080:
        preview *= 2
    pw, ph = w // preview, h // preview
    buf = bytearray(pw * ph * 3)
    fill_rect(buf, pw, ph, 0, 0, pw, ph, (18, 18, 22))

    def R(sx, sy, sw, sh):
        x, y, rw, rh = map_rect(layout, sx, sy, sw, sh)
        return x // preview, y // preview, max(1, rw // preview), max(1, rh // preview)

    # Extra battlefield (right of scaled 640 HUD).
    fill_rect(buf, pw, ph, layout["left"] // preview, layout["top"] // preview,
              layout["bf_w"] // preview, layout["bf_h"] // preview, (46, 72, 42))
    # Scaled 640x480 battlefield hole.
    bx, by, bw, bh = R(SOURCE_LEFT, SOURCE_TOP, SOURCE_BF_W, SOURCE_BF_H)
    fill_rect(buf, pw, ph, bx, by, bw, bh, (62, 96, 54))
    lx, ly, lw, lh = R(*LEFT_STRIP)
    fill_rect(buf, pw, ph, lx, ly, lw, lh, (58, 52, 40))
    hx, hy, hw, hh = R(*HEADER)
    fill_rect(buf, pw, ph, hx, hy, hw, hh, (78, 68, 48))
    rx, ry, rw, rh = R(*RADAR)
    fill_rect(buf, pw, ph, rx, ry, rw, rh, (28, 36, 48))
    tx, ty, tw, th = R(*TOP_BAR)
    fill_rect(buf, pw, ph, tx, ty, tw, th, (92, 86, 70))
    bot = R(*BOTTOM_BAR)
    fill_rect(buf, pw, ph, *bot, (92, 86, 70))
    fill_rect(buf, pw, ph, *R(*METAL_BAR), (48, 160, 72))
    fill_rect(buf, pw, ph, *R(*ENERGY_BAR), (210, 180, 48))
    blend_rect(buf, pw, ph, *R(*SCORE), (40, 40, 90), 0.55)

    title = (
        f"{layout['width']}x{layout['height']} scale={layout['scale']:.4g} "
        f"left={layout['left']} top={layout['top']} bot={layout['bottom']} "
        f"hud={layout['hud_width']}x{layout['hud_height']} "
        f"bf={layout['bf_w']}x{layout['bf_h']}"
    )
    if preview > 1:
        title += f" preview=1/{preview}"
    ts = 2 if pw >= 1280 else 1
    draw_text(buf, pw, ph, 8, 8, title[:96], (240, 240, 240), ts)
    draw_text(buf, pw, ph, rx + 4, ry + 4, "RADAR", (180, 200, 255), ts)
    draw_text(buf, pw, ph, hx + 4, hy + 8, "ORDERS", (255, 230, 180), ts)
    draw_text(buf, pw, ph, tx + 8, ty + max(0, th // 2 - 3 * ts), "METAL ENERGY", (20, 20, 20), ts)
    draw_text(buf, pw, ph, bx + 8, by + 8, "BATTLEFIELD", (210, 255, 210), ts)
    extra_x = layout["hud_width"] // preview
    if extra_x + 8 < pw:
        draw_text(buf, pw, ph, extra_x + 8, by + 24, "EXTRA BF", (180, 220, 180), ts)
    return pw, ph, buf, title


def main() -> None:
    out = Path(__file__).resolve().parents[1] / "docs" / "resolution-proofs"
    out.mkdir(parents=True, exist_ok=True)
    lines = []
    for w, h in RESOLUTIONS:
        layout = make_match_layout(w, h)
        pw, ph, buf, title = compose(layout)
        name = f"{w}x{h}.png"
        write_png(out / name, pw, ph, bytes(buf))
        lines.append(title + f" file={name} {pw}x{ph}")
        print(title, "->", name, f"{pw}x{ph}")
    (out / "bounds.txt").write_text("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
