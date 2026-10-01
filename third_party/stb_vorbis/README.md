# stb_vorbis

The Ogg Vorbis decoder of the stb libraries, version 1.22, which the
engine's music decoder (`src/audio`) uses for `.ogg` music files.

- Source: <https://github.com/nothings/stb>, file `stb_vorbis.c` at commit
  `2c980bb59875b0d32144a71867fbdebb2f77cd20` (SHA-256 of the file as
  published: `4c7cb2ff1f7011e9d67950446b7eb9ca044f2e464d76bfbb0b84dd2e23e65636`).
- Licence: public domain or MIT, at your choice (`LICENSE`, also at the end
  of `stb_vorbis.c`).

## Changes

One change, marked `Open Annihilation:` in the source: a residue of type 2
interleaves the vectors of every channel of its submap, so its length is the
channel count times the half block, not twice the half block. Without it the
six-channel (5.1) files the reference encoder writes decode to noise. Both the
decoding and the memory estimate at setup use the corrected length.
