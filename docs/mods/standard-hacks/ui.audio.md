# Audio

<!-- BEGIN GENERATED: facts -->
| Fact | Value |
| --- | --- |
| Hack id | `ui.audio` |
| Area | Interface (`ui`) |
| Scope | view: local display only; each player may differ |
| Runs on | Each machine, for its own view. |
| Parameters | 1 |
| Status | implemented |
<!-- END GENERATED: facts -->

## Description

Music can come from a folder of MP3 files instead of the game disc's
layout, the CD music pauses when a finished match leaves for its end
screen, and the victory announcement plays as the victory banner is drawn,
at most once every 300 ticks. 3.1c plays its music from the CD and has no
victory announcement on the banner.

## Configuration example

```yaml
hacks:
  ui.audio: true
```

```yaml
hacks:
  # Every MP3 file of the music folder, in name order, as the music tracks.
  ui.audio: folder-scan
```

## Details

### Music source

`music` picks how the music folder becomes the disc the CD player plays.
The first game folder that has a `music` folder is used, a mod's folder
before the base game's.

| `music` | Tracks |
| --- | --- |
| `cd` | The engine's usual layout, as 3.1c's CD: `music/<n>.mp3` (or `.ogg`, `.wav`, `.flac`) is disc track `n`. Track 1 is the data track, so tracks 2 to 17 are the sixteen playable tracks. |
| `numbered-mp3` | `1.mp3`, `2.mp3` and on, up to the first missing number, are disc tracks 1 and on. Track 1 is still the data track. Without `1.mp3` there is no disc at all, and `01.mp3` is not track 1. |
| `folder-scan` | Every MP3 file of the folder, sorted by name without case, is tracks 2 and on, whatever the names say: `10.mp3` comes before `2.mp3`. |

The music then plays as it does in 3.1c: battle and calm tracks chosen by
the match's activity, and the CD player's own controls.

### End of match and victory announcement

- When a finished match leaves for its end screen, the CD music pauses.
  3.1c plays on.
- Each time the victory banner is drawn, the "Victory Condition" sound plays
  if more than 300 ticks have passed since the banner was last drawn. The
  tick is recorded at every drawing, not only when the sound plays, and it
  starts at 0. So a banner drawn every frame announces once; a match won
  before tick 301 is not announced; and a banner drawn at an earlier tick
  than the last one, as in a later match, always is. The recorded tick is
  kept from one match to the next.

### Sound settings

Sounds keep playing while the window is in the background, and sounds are
placed in 3D by the player's Sound Mode setting. A profile that wants a
different starting Sound Mode or mixing setting gives it as a registry seed
(see [the OA MOD standard](../oamod-standard.md#54-settings-bindings-and-registry-seeds));
the hack itself does not change them.

### Network games

The hack changes only what this machine plays. Each player may choose a
different `music` value; nothing needs to agree between machines.

`music` is an `install` parameter: a profile may bind it to a setting under
`settings` so the player can choose the source.

### Implementation notes

- The application reads `ModProfile::ui.audio` (`enabled`, `music`).
  `view_rules::music_source` in [src/app/view_rules.cpp](../../../src/app/view_rules.cpp)
  turns `music` into a `MusicSource`, and `Runtime::music_start` in
  [src/app/runtime_music.cpp](../../../src/app/runtime_music.cpp) scans the
  folder with `music_disc_scan`, `music_disc_scan_numbered` or
  `music_disc_scan_folder` from [src/audio](../../../src/audio/README.md).
- `Runtime::music_end_game` pauses the music, and `Runtime::announce_victory`
  plays the announcement through `view_rules::victory_announcement_due`.
- Tests: `app-view-rules` (`music_source_follows_the_profile`,
  `victory_announcement_gate`); `audio-music-session` checks both folder
  layouts, and `audio-music-session-mod-install` the numbered files of the
  game folder `OA_MOD_GAME_DIR` names (skipped without it).

## Full configuration schema

<!-- BEGIN GENERATED: schema -->
Hack id `ui.audio`, written under the profile's `hacks` block.

| Write | Means |
| --- | --- |
| `ui.audio: true` | On, every parameter at its default. |
| `ui.audio: {music: numbered-mp3}` | On, the parameters named set and the rest at their defaults. |
| `ui.audio: {preset: baseline}` | On, starting from a preset; parameters named beside `preset` replace its values. |
| `ui.audio: numbered-mp3` | On, the shorthand: a bare value sets `music`. |
| `ui.audio: false` | Off, the same as leaving it out: 3.1c behaviour. |

### Parameters

| Parameter | Type | Unit | Allowed | Length | Baseline (3.1c) | Default | Adjustable | Scope | Overridden by |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `music` | `enum` | - | `cd`, `numbered-mp3`, `folder-scan` | - | `cd` | `numbered-mp3` | `install` | `view` | - |

Adjustable: `install`: the player's settings may set it when the profile binds it under `settings`.

### Presets

| Preset | Values |
| --- | --- |
| `baseline` | `{music: cd}` |

`baseline` sets every parameter to its 3.1c value: the hack is on and plays as 3.1c.

### Shorthand

The hack has one parameter, so a bare value sets `music`: `ui.audio: numbered-mp3` is `ui.audio: {music: numbered-mp3}`.

### Every parameter at its default

```yaml
hacks:
  ui.audio:
    music: numbered-mp3
```
<!-- END GENERATED: schema -->
