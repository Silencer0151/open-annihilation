# Intro media container boundary

The frontend plays `Data/1.zrb` through `Data/5.zrb` as Smacker movies: it
opens the movie, then decodes each frame, copies it to the screen surface and
advances to the next frame. Decoding sits behind a codec boundary.

`oa-formats-smacker` implements the bounded SMK2 container boundary visible in
the assets: fixed 104-byte little-endian header, seven packed audio track
descriptors, frame-size/type tables, Huffman tree bytes, and individually
streamable compressed frame payloads. Frame-rate values keep the file's
signed encoding (`-3333` is approximately 30 frames per second; positive
values use the millisecond divisor). `kRingFrameFlag` accounts for the extra
table entry used by the container header. Each frame-size entry is used
whole, as a 32-bit payload size; the low-bit flags that other SMK readers
assign are not applied. SMK4 is rejected: 3.1c plays SMK2 only. Audio rate
low 24 bits and high-byte flags are exposed without assigning unresolved flag
semantics. Every table and payload is checked against file and allocation
limits before access.

This component does not decode Smacker pixels or audio; it supplies real
frame payloads and metadata, not a movie player or a pixel-fidelity claim.

## Native playback boundary

`oa-media-intro-player` is an optional platform boundary for development playback.
It uses FFmpeg's `libavformat`/`libavcodec` to decode the real SMK2 streams,
`libswscale` to convert decoded video frames to RGB24, and `libswresample` to
convert decoded audio to signed 16-bit samples. SDL3 presents the RGB frames
with a logical letterboxed renderer and queues the converted samples through an
SDL audio stream. The implementation contains no shell or `ffmpeg` subprocess;
it decodes at the codec boundary and does not claim the game's exact pixels or
mixing.

The game's height-mode check is kept at presentation time: header flag modes
`0x2` and `0x4` double the runtime movie height, while mode `0x0` keeps the
decoded height. Frames are placed centred on the 640x480 surface, as the game
does; the source frame is never silently reinterpreted from its FFmpeg
sample-aspect-ratio metadata. For mode `0x2`, the presentation buffer keeps
each decoded RGB row on even destination rows and fills odd rows from palette
index 0, matching the game's scanline behaviour. Mode `0x4` keeps doubled
presentation dimensions; the engine does not reproduce how the game fills the
added rows, so it is not claimed pixel-identical.

On screen the 640x480 canvas is letterboxed to fill the window (4:3 at the
largest uniform scale: 1440x1080 in a 1920x1080 window). Mode-2 odd rows are
filled from the decoded line for display so linear upscale is a full frame;
nearest-neighbour scaling of blank scanlines at 2.25x looked like broken
interlace. Snapshots still write the blank odd rows.

The target is built when SDL3 and all five FFmpeg development libraries are
available (`avformat`, `avcodec`, `avutil`, `swscale`, and `swresample`). With
the SDL3 3.4.16 install and Homebrew FFmpeg:

```sh
cmake -S src/formats/smacker -B /tmp/oa-intro-media-build \
  -DCMAKE_PREFIX_PATH="$PWD/local/deps/sdl-install:/opt/homebrew/opt/ffmpeg"
cmake --build /tmp/oa-intro-media-build
```

The root build can link `oa-media-intro-player` into the `oa-intro` executable and
compile `src/media/src/intro_player.cpp`. For deterministic validation without opening a
window or audio device, use:

```sh
/path/to/oa-intro --headless-check --frames 2 /path/to/Data/1.ZRB
```

The player bounds dimensions, demuxed payloads, decoded frames, and decoded audio
bytes. `--frames N` is a finite smoke run; without it playback remains bounded
by the player limits. Escape and window-close events return a successful
`skipped` result so the frontend can continue to its next state. Use
`--snapshot PATH.ppm` to save the first decoded RGB frame for visual QA; the
snapshot is a binary PPM and does not alter playback timing.

Build and run synthetic tests:

```sh
cmake -S src/formats/smacker -B /tmp/oa-intro-media-build -DBUILD_TESTING=ON
cmake --build /tmp/oa-intro-media-build
ctest --test-dir /tmp/oa-intro-media-build --output-on-failure
```

Inside the engine's build, `intro-media-smacker-data` opens the five movies
in the `Data` folder of the installation `OA_GAME_DIR` names and checks each
header and first frame.
