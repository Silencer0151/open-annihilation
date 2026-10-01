# Audio tests: encoded tones

`data/` holds the short files `music_decoder_test.cpp` decodes. Each was
encoded from a 16-bit WAVE file whose samples are
`round(0.5 * 32767 * sin(2 * pi * f * i / rate))`, or for the noise the top
24 bits of the sequence `x = x * 1103515245 + 12345` (mod 2^32) starting
from `x = 1`, two samples per frame:

| File | Source | Encoded with |
|---|---|---|
| `sine-44100-2.mp3` | 11025 frames, 440 Hz left, 880 Hz right | `ffmpeg -c:a libmp3lame -b:a 320k` |
| `sine-22050-1.ogg` | 5513 frames, 440 Hz | `oggenc -q 4` |
| `sine-44100-6.ogg` | 11025 frames; front left 440 Hz, front right 660, centre 880, low-frequency silent, back left 1320, back right 1760 (WAVE order) | `oggenc -q 4` |
| `sine-48000-2.flac` | 4800 frames, 440 Hz left, 880 Hz right | `ffmpeg -c:a flac` |
| `noise-44100-2-24.flac` | 2000 frames of 24-bit noise | `ffmpeg -c:a flac` |

The test checks the frame counts exactly, the lossless files sample for
sample, and the lossy ones against the tones they were made from.
