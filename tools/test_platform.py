#!/usr/bin/env python3
# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

"""Exercise platform parsing, WAV decoding and SDL dummy-device presentation."""
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile
import wave

# A command that starts the executable under test, such as the compatibility
# layer a cross build runs its tests through (tools/build_windows.sh --run-tests
# sets it); empty runs the executable directly.
RUNNER = shlex.split(os.environ.get("OA_TEST_RUNNER", ""))


def main():
    executable = str(Path(sys.argv[1]).resolve())
    with tempfile.TemporaryDirectory(prefix="oa-platform-") as tmp:
        root = Path(tmp)
        image = root / "sample.ppm"
        audio = root / "sample.wav"
        # First raster byte is whitespace: parsers must not skip it.
        image.write_bytes(b"P6\n# sample\n2 1\n255\n" + bytes([10, 20, 30, 255, 0, 0]))
        with wave.open(str(audio), "wb") as w:
            w.setnchannels(1)
            w.setsampwidth(2)
            w.setframerate(22050)
            w.writeframes(b"\0\0" * 2205)
        args = [*RUNNER, executable, "--image", str(image), "--wav", str(audio)]
        subprocess.run(args + ["--headless-check"], check=True, timeout=20)
        env = dict(os.environ, SDL_VIDEO_DRIVER="dummy", SDL_AUDIO_DRIVER="dummy",
                   SDL_RENDER_DRIVER="software")
        subprocess.run(args + ["--frames", "2"], env=env, check=True, timeout=20)
        bad = root / "bad.ppm"
        for data in (b"P6\n0 1\n255\n", b"P6\n1 1\n255\n\0\0",
                     b"P6\n999999999 1\n255\n", b"P3\n1 1\n255\n0 0 0"):
            bad.write_bytes(data)
            result = subprocess.run([*RUNNER, executable, "--image", str(bad), "--headless-check"],
                                    capture_output=True, timeout=20)
            if result.returncode == 0:
                raise RuntimeError(f"accepted invalid PPM: {data!r}")
    print("platform smoke tests passed (dummy devices, not physical output)")


if __name__ == "__main__":
    main()
