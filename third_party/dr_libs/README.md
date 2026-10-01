# dr_libs

The MP3 and FLAC decoders of David Reid's dr_libs, which the engine's music
decoder (`src/audio`) uses for `.mp3` and `.flac` music files.

- Source: <https://github.com/mackron/dr_libs> at commit
  `dfe8377631000664666519fdb83da193fd8037f4`: `dr_mp3.h` (0.7.3 and later
  fixes, SHA-256 `997b7ee18de6e6b81e2a83f1ea9fc62aef25c62b28d48db95635f49e65de0a2f`)
  and `dr_flac.h` (0.13.3 and later fixes, SHA-256
  `111144e778f55738db6851cb226015c419e00d04b916a09506d4856d9cff945c`).
- Licence: public domain (Unlicense) or MIT No Attribution, at your choice
  (`LICENSE`). dr_mp3 is based on minimp3, which is in the public domain
  (CC0).

The files are unchanged. The engine compiles them in
`src/audio/src/music_codecs.c` with the options `src/audio/src/music_codecs.h`
sets.
