#!/usr/bin/env python3
# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

"""Write the automation protocol's test vectors: frames a reader must take, and what it must take from them.

    python3 make_vectors.py          # rewrite the .bin files and vectors.json beside this script
    python3 make_vectors.py --check  # exit 1 when they differ from what it would write

Each vector is made here, so that it stays exact and explained. Each entry of
vectors.json says what reading its file gives, in order: {"frame": {...}} for
a frame (its route, its JSON part byte for byte, its payload's length and
CRC-32, and strings it must read back unchanged) or {"bad_frame": {"reason":
..., "skipped": n}} for a run of skipped bytes. What a reader gives must not
depend on how the bytes are split across reads: automation-protocol feeds
each file whole, byte by byte and in chunks.
"""

import json
import sys
import zlib
from pathlib import Path

HERE = Path(__file__).resolve().parent


def frame(route, message, payload=b""):
    """One frame, its JSON part given as text."""
    body = message.encode()
    crc = zlib.crc32(body + payload)
    return f"AUTO/1 {route} {len(body)} {len(payload)} {crc:08x}\n".encode() + body + payload


def expect_frame(route, message, payload=b"", **extra):
    """What reading a frame must give."""
    result = {
        "route": route,
        "json_text": message,
        "payload_len": len(payload),
        "payload_crc32": f"{zlib.crc32(payload):08x}",
    }
    result.update(extra)
    return {"frame": result}


def bad(reason, skipped):
    """What reading a run of skipped bytes must give."""
    return {"bad_frame": {"reason": reason, "skipped": skipped}}


def vectors():
    """Each vector: its file, what it holds, its bytes and what reading it gives."""
    out = []

    hello = '{"id":1,"op":"hello","client":"check","versions":[1],"token":"5f2c…"}'
    out.append(("hello.bin", "a client's hello", frame("-", hello), [expect_frame("-", hello)]))

    key = '{"id":7,"op":"key","keys":["Return"]}'
    out.append(("key.bin", "a request (header AUTO/1 game 37 0 4ae1fb5a)", frame("game", key),
                [expect_frame("game", key)]))

    ping = '{"id":1,"op":"ping"}'
    out.append(("ping-empty.bin", "a request without a payload (CRC 3a7a3fac)", frame("-", ping),
                [expect_frame("-", ping)]))

    picture = bytes((i * 37 + 11) & 0xFF for i in range(1024))
    response = '{"id":9,"ok":true,"t":81234567,"image":{"format":"bmp","width":16,"height":16}}'
    out.append(("payload.bin", "an answer with a 1 KiB payload", frame("oa", response, picture),
                [expect_frame("oa", response, picture)]))

    first = '{"id":2,"op":"wait","label":"the battle room","until":{"state":"screen","equals":"mp_battleroom"}}'
    second = '{"id":3,"ok":true,"t":61002113,"met":true,"after_ms":7310}'
    out.append(("two-frames.bin", "two frames back to back",
                frame("game", first) + frame("game", second, b"\x00\x01\x02"),
                [expect_frame("game", first), expect_frame("game", second, b"\x00\x01\x02")]))

    noise = b"Serial line noise before any frames\r\n"
    assert len(noise) == 37
    out.append(("noise-then-frame.bin", "37 bytes of noise, then a frame", noise + frame("-", ping),
                [bad("noise", 37), expect_frame("-", ping)]))

    damaged = bytearray(frame("oa", response, picture[:64]))
    damaged[-10] ^= 0x04  # one payload bit flipped after the CRC was taken
    out.append(("bad-crc.bin", "a frame with one payload bit flipped, then a good frame",
                bytes(damaged) + frame("oa", key), [bad("crc", len(damaged)), expect_frame("oa", key)]))

    too_long = b"AUTO/1 oa 2097152 0 00000000\n" + b'{"id":1,"op":"x","padding":"' + b"x" * 64
    out.append(("too-long.bin", "a header announcing a 2 MiB JSON part and the start of its body, then a frame",
                too_long + frame("oa", ping), [bad("too_long", len(too_long)), expect_frame("oa", ping)]))

    utf8 = ('{"id":5,"op":"type","text":"你好，世界 hello","player":"Renée",'
            '"escaped":"Ren\\u00e9e \\ud83d\\ude00 \\"q\\" \\\\ \\n"}')
    out.append(("utf8.bin", "a chat line in Chinese, a player name with an accent, and escapes",
                frame("game", utf8),
                [expect_frame("game", utf8, strings={
                    "text": "你好，世界 hello",
                    "player": "Renée",
                    "escaped": 'Renée \U0001f600 "q" \\ \n',
                })]))

    echo = '{"id":7,"op":"echo","text":"once"}'
    out.append(("replay.bin", "the same request twice in a row", frame("echo", echo) + frame("echo", echo),
                [expect_frame("echo", echo), expect_frame("echo", echo)]))

    nested = '{"id":8,"op":"echo","text":"through a relay"}'
    out.append(("nested-route.bin", "a route of two parts", frame("relay/echo", nested),
                [expect_frame("relay/echo", nested)]))

    header = b"AUTO/1 Bad Route 1 0 zz\nAUTO/1\n"
    out.append(("bad-header.bin", "malformed header lines, then a frame", header + frame("-", ping),
                [bad("header", len(header)), expect_frame("-", ping)]))

    glued = b"stray text without a newline" + frame("-", ping)
    out.append(("noise-glued.bin", "stray text with no line end right before a frame", glued,
                [bad("noise", 28), expect_frame("-", ping)]))

    cut = frame("oa", response, picture)[:150]
    out.append(("stalled.bin", "a frame cut off before its payload ends, at the end of the stream",
                frame("-", ping) + cut, [expect_frame("-", ping), bad("stalled", len(cut))]))

    out.append(("empty-json.bin", "a frame with an empty JSON part, which the framing allows", frame("-", ""),
                [expect_frame("-", "")]))
    return out


def main():
    check = "--check" in sys.argv[1:]
    entries = []
    differ = []
    for name, why, data, expect in vectors():
        entries.append({"file": name, "why": why, "expect": expect})
        path = HERE / name
        if check:
            if not path.is_file() or path.read_bytes() != data:
                differ.append(name)
        else:
            path.write_bytes(data)
    text = json.dumps({"vectors": entries}, indent=1, ensure_ascii=True) + "\n"
    index = HERE / "vectors.json"
    if check:
        if not index.is_file() or index.read_text(encoding="ascii") != text:
            differ.append("vectors.json")
        if differ:
            print("make_vectors: out of date: " + ", ".join(differ), file=sys.stderr)
            return 1
        return 0
    index.write_text(text, encoding="ascii")
    print(f"make_vectors: wrote {len(entries)} vectors in {HERE}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
