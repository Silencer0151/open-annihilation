# Game audio

This component holds the sound registry and selection policy of Total
Annihilation 3.1c and supplies a portable SDL3 WAV playback boundary.

The registry is loaded from `gamedata/allsound.tdf`: each section registers its
32-byte section name and `sound` value, up to 255 entries. A configured value
resolves below `sounds` with any existing extension replaced by `wav`; no
localized copy of the file is looked for. Name lookup is case-insensitive and
returns `0xffff` when absent. Selection applies the playback gates, carries the
request to announce the sound to the other players' simulations, and supports a
temporary alternate route. The `s`/`S` command switch sets the
playback-suppressed flag and leaves the diagnostic-direct flag unchanged. The
request to announce a sound is exposed as a flag; telling the other players is
left to the caller.

The SDL backend reads WAV data through `oa::AssetStore`, so loose-file and HPI
precedence remain centralized in the asset layer.

Besides its effects and its one looping sound, the WAV player keeps one
stream, as the game keeps one: the briefing's narration and the end screen's
glamour sound. `SdlWavPlayer::play_stream` plays a sound once, after a delay
of silence, in place of any stream still playing or waiting;
`stop_stream` silences it at once, during its delay too; and `stream_busy`
holds from the request until the sound has played out.

Unit announcements are queued only for the viewpoint player's units whose owner
is allowed to announce. Categories use a fixed descriptor table: the queue
suppresses a duplicate category, orders its eight records by priority, and
drops slot seven when full. `gamedata/SOUND.TDF` maps the unit FBI
`SoundCategory` to one or more direct sound names. Presentation uses the game's
15-bit `rand()` scaling, priority/volume gates, unit text flag, speech-mode bit,
and per-category cooldown in 30 Hz ticks. The selected name is resolved beneath
`sounds` and handed to the SDL player's direct-resource boundary, bypassing the
ALLSOUND registry as the game does.

Effects volume: `fxvol` is shifted left by 10 before the device clamps it at
65535, so preference 64 reaches full scale. `WaveOutVolume` is a scalar restored
at startup; the device clamps it to 16 bits and applies it to both channels.
SDL multiplies these two normalized gains and applies the result to each
active stream.

Sound-screen RESTORE stores fxvol 27, sets the ackfx, buildfx, and speechfx
bits of `Game.sound_flags`, forces that word's low three mode bits to 1, and
stores unitchat 10. Other bits of the word, including its high byte, are kept.
It also clears the sound device's 3D mode word. The saved volumes are then
reapplied, the effects volume through the path above.

Clips the match plays at a map point: with the sound mode's 3D switch on
(set by Sound Mode 2, the sound screen's 3D mode and the Sound3D console
command, and passed to `voice_spatial` as its `spatial_enabled` argument) they
play at -585 placed from the middle of the view, within a minimum distance of
the view's mean size and a maximum of the map's width plus height; otherwise
unplaced at -585 inside the view and -1585 outside it. `Mixer::spatial_enabled`
is a separate switch, set by `mixer_enable_spatial`, for the voices the mixer
itself plays with a position.
`SdlWavPlayer::play_placed` hears a placement through `spatial_stereo_gain`,
which attenuates and pans as a 3D sound buffer does: level min/d beyond the
minimum distance (held from the maximum) and a pan by the source's bearing.
Its other sounds play at the level that stands for -585.

Registered names are kept to 32 characters, and a full 32-character name is
compared on all 32 characters; every comparison stays within the name.

CD music runs the CD player (`cd_music`, `music_mood`) against a music-file
disc: `music/<n>.mp3` (or .ogg/.wav/.flac) is disc track n, track 1 is the data
track, so tracks 2..17 of the game's music are the sixteen playable tracks.
An unknown 16-track data disc gets the game's default kinds: tracks 1-7 battle,
8-16 building (calm). `sdl_music` decodes with FFmpeg on SDL's audio thread and
maps the CD mixer line to the stream gain. `music_session` holds the game-side
music events: startup, main menu (kind 4), game loading (calm), teardown, the
per-frame mood update fed by local hits (+1) and kills (+5), ARMOPT pause, the
CDPlay/CDStop/MusicMode console commands and the MUSIC.GUI transport. The
whole mood (activity ring, applied kind, switch timer and last update tick)
resets at match start and end, so each game's music is independent of the
last; in 3.1c the applied kind, switch timer and last update tick carry over
from one game to the next.

## Offline mix

`oa-audio-offline-mix` (`oa/audio/offline_mix.hpp`, namespace
`oa::audio::offline_mix`) mixes the game's sound effects for director
renders: clips started at exact sample frames, mixed on demand into 16-bit
stereo at 48 kHz, with no sound device, no clock and no SDL. It reads sound
files only through its `ClipFileHooks` table and writes nothing but the
samples it returns; it never reads or writes simulation state.

Entry points: `decode_clip` (a sound file to a 48 kHz mono clip),
`centibel_gain` and `voice_gains` (a volume and a placement to Q15 gains),
`OfflineMix` (`start` queues a clip at a sample frame, `render` mixes the
next sample frames, `position`, `voices_playing`, `errors`) and
`wave_header` (the 44-byte header of a 48 kHz 16-bit stereo WAVE file).

Every step after decoding is integer arithmetic, so the same starts give the
same samples on every platform, and a mix rendered in several calls equals
one rendered in one call:

- Decoding takes the raw, DIGI and RIFF layouts `describe_wave` finds: 8-bit
  unsigned or 16-bit signed, mono or stereo, 4000 to 96000 Hz. 8-bit samples
  widen as `(x - 128) * 256`; stereo averages its two channels, rounding
  down. The clip is resampled to 48 kHz by exact rational phase: output
  sample n reads input position `n * rate / 48000` and interpolates linearly
  to the next input sample (0 past the end), rounding down; a clip of N
  input frames gives `ceil(N * 48000 / rate)` samples. A file larger than
  `max_clip_file_bytes`, a data span that runs past the file's end, no whole
  sample frame, or a clip of more than `max_clip_file_bytes` samples at
  48 kHz is refused with a message.
- `centibel_gain` turns hundredths of a decibel below `full_scale_volume`
  (0, the device's full volume, so the game's near volume of -585 plays
  5.85 dB below full scale, as in the game) into a Q15 gain from two tables typed into
  the source: `10^(-h/20)` for whole decibels 0 to 19 and `10^(-u/2000)` for
  hundredths 0 to 99, both Q30 and rounded to nearest; each further 20 dB
  divides by ten. Louder volumes play at full scale. `voice_gains` takes
  `spatial_stereo_gain`'s levels (float, with + - * / and square root only),
  rounds each down to Q15 and multiplies it by that gain.
- Each output sample is the sum over playing clips of
  `(clip sample * gain) >> 15`, times the master gain (Q15; by default the
  game's default effects volume, 27 << 10 over 65535) `>> 15`, saturated to
  16 bits.

The voice policy is the game's mixer's: at most eight voices; a start that
finds them all taken stops the voice that started first; each clip has up
to four buffers, made as starts need them, and a start reuses an idle one,
else makes one while fewer than four exist (the highest free index first),
else restarts the one that has played furthest (the lowest index among
equals). A restarted buffer keeps the voice it had and takes a second one,
so it counts twice toward the eight until it stops; when the older of its
two voices is the one evicted, the buffer stops, and its other voice is
freed at the next start. A clip that plays to its end frees its voices
before any start on the sample frame it ends on. Starts apply in the order
of their sample frames, and starts on one frame in the order they were
queued; a start queued for a frame already mixed applies at `position()`.

Clips are decoded once and cached under their path, with backslashes turned
into slashes and ASCII letters lowered; the hooks are asked for the path
with slashes and its letters as given. The cache holds `max_cached_clips`
clips; a full cache drops the least recently started clip that no voice
plays. A clip that cannot be read or decoded plays nothing, and its message
is kept once in `errors()`.

Tests: `audio-offline-mix` decodes synthetic 8-bit, 16-bit, stereo, DIGI,
headerless, 11025 Hz and 22254 Hz files to exact samples, refuses malformed
ones (a data span past the end, no data, a four-gigabyte count, unsupported
widths, channels and rates, oversized files and clips), checks the decibel
tables against a power function to one Q15 step, pins voice gains, the
voice limit, the restart bookkeeping, start order, saturation, errors and
the cache, checks that rendering in pieces (with starts queued ahead or just
in time) equals one render, pins the SHA-256 of a two-second synthetic
scene, and pins `wave_header`'s bytes. `audio-offline-mix-data` decodes
every sound file of the installed game.

Limitations: the mix plays each clip once. The looping sound, the stream and
the music are not mixed, so no voice is ever exempt from eviction.
