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
