# SDL3 platform probe

`tools/platform-probe/platform_probe.cpp` is a small presentation and audio smoke target for the
portable engine. It accepts an extracted binary PPM (`P6`) image,
uploads that image to an SDL3 texture, and presents it through a resizable
window. An optional WAV is loaded with SDL3 and queued on an
`SDL_AudioStream`. This is platform scaffolding; it is not an implementation
of the game's renderer, and the image/audio output makes no claim of fidelity
to the game.

The executable is named `oa-platform` by the project CMake configuration. A
normal run is:

```text
oa-platform --image extracted-frame.ppm --wav intro.wav
```

Use `--frames N` for a finite smoke run. The value is the number of frames to
present and may be zero; each presented frame advances by roughly 16 ms so a
queued short WAV has time to play. Without it, the window runs until it
receives a quit event, its close request, or Escape. Resizing uses SDL3 logical
presentation in letterbox mode, with the source image's dimensions as the
logical resolution. A successful window run prints the selected SDL video,
renderer, and (when requested) audio backends, followed by the number of
frames completed.

`--headless-check` parses and validates the PPM and, if provided, loads the WAV
through SDL3 without creating a window, renderer, or audio device:

```text
oa-platform --headless-check --image extracted-frame.ppm --wav intro.wav
```

The PPM parser accepts comments and `maxval` values from 1 through 255, scales
lower max values to 8-bit RGB, and bounds dimensions at 16384 by 16384 with a
128 MiB decoded-pixel limit. Input files are bounded at 256 MiB. PPM raster
bytes are treated as binary data after the mandatory header separator, so a
leading whitespace-valued pixel is preserved.

The code uses SDL3 APIs available since SDL 3.2.0: `SDL_LoadWAV`,
`SDL_OpenAudioDeviceStream`, `SDL_PutAudioStreamData`,
`SDL_ResumeAudioStreamDevice`, and `SDL_SetRenderLogicalPresentation`, so
the project's CMake asks for SDL 3.2 or newer. The `oa-platform` target links
`SDL3::SDL3`, which `tools/bootstrap_sdl.py` builds at the pinned version
into `local/deps`; SDL3 is not vendored in the tree.
