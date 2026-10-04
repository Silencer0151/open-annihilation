# Smacker movies

The frontend plays `Data/1.zrb` through `Data/5.zrb` as Smacker movies: it
opens the movie, then decodes each frame, copies it to the screen surface and
advances to the next frame. `oa-formats-smacker` reads the SMK2 container
and decodes its pictures and sound; the intro player in `src/media` reads
the file and presents what it decodes.

## Container

`oa/formats/smacker.hpp`: `SmackerReader` implements the bounded SMK2
container visible in the assets: fixed 104-byte little-endian header, seven
packed audio track descriptors, frame-size/type tables, Huffman tree bytes,
and individually streamable compressed frame payloads. Frame-rate values
keep the file's signed encoding (`-3333` is approximately 30 frames per
second; positive values use the millisecond divisor). `kRingFrameFlag`
accounts for the extra table entry used by the container header; that entry
is not played. Each frame-size entry is used whole, as a 32-bit payload
size; the low-bit flags that other SMK readers assign are not applied (in
3.1c's movies those bits are clear). SMK4 is rejected: 3.1c plays SMK2
only. Every table and payload is checked against file and allocation limits
before access, and `frame()` gives any frame's offset, size and type at once.

## Decoder

`oa/formats/smacker/decoder.hpp` decodes from byte spans and opens no file:

- `split_frame` divides a frame payload into its palette chunk, one audio
  chunk per track named in the frame type, and the video chunk;
- `update_palette` applies a palette chunk: runs of kept colours, runs
  copied from the palette as it was before the chunk, and new colours of
  three six-bit components, each widened to eight bits by repeating its top
  two bits below it;
- `audio_format` and `decode_audio` decode a track's chunk to interleaved
  signed 16-bit samples: packed tracks (Huffman-coded deltas per channel and
  byte, wrapping around) and plain ones, 8-bit samples widened as
  (sample - 128) * 256;
- `VideoDecoder` reads the four 16-bit Huffman tables (mono masks, mono
  colours, full pixels, block types) and updates one frame of palette
  indices in 4x4 blocks: mono, full, skip and fill runs. Three leaves of
  each table repeat recent values; they start at 0 every frame.

A movie decodes one frame at a time in file order. The decoder keeps the
frame (width * height bytes) and its tables (four bytes per entry; the
game's largest movie needs about 670 KB), and reads each table's first
eight bits of a code from a 256-entry lookup. It uses integer arithmetic
only, so every platform decodes the same pictures and sound.

Transform-coded audio tracks, which 3.1c's movies do not use, are not
played (`AudioCoding::none`). An audio chunk whose first bit is clear holds
no samples.

## Tests

- `formats-smacker-decoder` decodes the small movie built bit by bit in
  `tests/support/smacker_test_movie.hpp` (8x8, three frames, a packed 8-bit
  stereo track) and checks every pixel, colour and sample: each block kind,
  a run clipped to the frame, recent values starting again each frame, a
  palette of new colours and one of copied colours. It decodes 16-bit mono
  and stereo and plain tracks, and refuses malformed palettes, audio, frame
  splits, tables and video chunks.
- `intro-media-smacker` checks the container on a synthetic file;
  `intro-media-smacker-data` opens the five movies in the `Data` folder of
  the installation `OA_GAME_DIR` names and checks each header and first
  frame.
- `intro-media-smacker-decode-data` decodes every frame and sample of the
  five installed movies and, for the 3.1c release's files (known by their
  SHA-256), checks the frame and audio-block counts, the SHA-256 of every
  frame's RGB checksum in order and the SHA-256 of all the samples; a movie
  whose file differs is decoded and its digests printed.

## Native playback

`oa-media-intro-player` (see [src/media](../../media/README.md)) shows the
decoded frames through SDL3 and queues the samples on a stream of the sound
output ([src/audio](../../audio/README.md)).
The game's height-mode check is kept at presentation time: header flag
modes `0x2` and `0x4` double the shown movie height, while mode `0x0` keeps
the decoded height. Frames are placed centred on the 640x480 surface, as the
game does. For mode `0x2`, a snapshot keeps each decoded RGB row on even
rows and fills odd rows from palette index 0, matching the game's scanline
behaviour; on screen the odd rows repeat the decoded row so the linear
upscale is a full frame. Mode `0x4` keeps doubled presentation dimensions;
the engine does not reproduce how the game fills the added rows, so it is
not claimed pixel-identical.

For a check without a window or audio device:

```sh
/path/to/oa-intro --headless-check --frames 2 /path/to/Data/1.ZRB
```

`--frames N` is a finite smoke run; without it playback remains bounded by
the player limits. Escape and window-close events return a successful
`skipped` result so the frontend can continue to its next state. Use
`--snapshot PATH.ppm` to save the first decoded RGB frame for visual QA; the
snapshot is a binary PPM and does not alter playback timing.
