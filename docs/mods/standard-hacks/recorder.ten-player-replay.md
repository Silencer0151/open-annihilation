# Ten-Player Replay

<!-- BEGIN GENERATED: facts -->
| Fact | Value |
| --- | --- |
| Hack id | `recorder.ten-player-replay` |
| Area | Recorder (`recorder`) |
| Scope | sim: part of the profile hash; every machine must agree |
| Runs on | Each machine, for its own view. |
| Parameters | 1 |
| Status | implemented |
<!-- END GENERATED: facts -->

## Description

A recording of a full ten-player game can be watched. In 3.1c the viewer needs a player slot of its own, so a recording that uses all ten slots is refused.

## Configuration example

```yaml
hacks:
  recorder.ten-player-replay: true   # form: watcher-view
```

The shorthand sets `form`:

```yaml
hacks:
  recorder.ten-player-replay: allied-fake-player
```

## Details

Watching a recording normally seats the viewer as a watcher in the first free slot after the recorded players. With ten recorded players there is none.

- `off`, as in 3.1c: a recording of ten players is refused with "demo has no free slot for the watcher".
- `watcher-view`: the viewer takes no slot and watches.
- `allied-fake-player`: the viewer takes no slot and is every recorded player's ally.

Without a slot, the viewer looks through the first recorded player's slot, which stays remote: nothing is simulated or sent for it on the viewer's machine, and the recorded players' records drive the game as they do for any recording. The viewer only watches, as a seated watcher does:

- The whole map shows: the host's mapping and line-of-sight rules are off for the viewer, and the line-of-sight type stays.
- Its radar shows every unit, and no jammer hides one where a seated watcher sees no jamming ([intel.allied-jammers-ignored](intel.allied-jammers-ignored.md)).
- It is shown every recorded chat line, and cannot open a chat line or the alliance panel.
- With [ui.resource-panel](ui.resource-panel.md) on, it sees every player's row and can switch its view to a recorded player and back.

The forms differ in a switched view and in what being allied shows:

- `watcher-view`: a view switched to a recorded player shows that player's radar; the viewer's own view shows every unit again. Each recorded player's economy records also give that player income figures: the resource readout of a player simulated on another machine has no production or use of its own, so from a player's second record on, each figure becomes the growth of its running total since the previous record, scaled to one 30-tick settlement. This applies to every recording played back under this form, with a slot for the viewer or without.
- `allied-fake-player`: the radar shows every unit in every view, a switched one included. With [ui.allied-unit-display](ui.allied-unit-display.md) on, the unit panel shows every player's units as an ally's: damage, rates, kills, mission and target.

Recordings with fewer than ten players seat the viewer as in 3.1c whatever the form.

**Network games.** It changes only how this machine plays a recording back, so it runs on each machine for its own view. The form is part of the profile hash.

**Related hacks.** [recorder.ta-demo-recorder](recorder.ta-demo-recorder.md) makes the recordings.

### Implementation notes

- `wire_rules_of` (`src/app/netgame/wire_rules_binding.cpp`) turns `recorder.ten_player_replay.form` into `WireRules::ten_player_replay`. `src/app/netgame/runtime_demo.cpp` copies it into the playback (`DemoPlayback::ten_player_replay` in `src/session/demo/include/oa/session/demo.hpp`). `demo_watches_without_slot` and `demo_bind_players` in `src/session/demo/src/demo_playback.cpp` seat the viewer and show it the whole map (`match_show_whole_map`, `src/netgame/match/src/launch.cpp`).
- `demo_session_begin` tells the match how the viewer sees (`demo_slotless_viewer`, `Match::set_slotless_viewer` in `src/sim/match-runtime/include/oa/sim/match_runtime.hpp`). `Match::scan_contacts` (`src/sim/match-runtime/src/tick_detection.cpp`) and `Match::jammer_jams` (`src/sim/match-runtime/src/intel.cpp`) read it; `Runtime::switch_watched_view` (`src/app/runtime_view_panels.cpp`) notes a switched view, and `Runtime::local_player_watches` (`src/app/runtime_console.cpp`) counts the viewer as watching. The session's `note_record` shows recorded chat and calls `demo_follow_economy`. The unit panel reads `UnitPanelHooks::viewer_allies_every_player` (`src/ui/hud/src/unit_panel.cpp`).
- Tests: `ten_players_watch_without_a_slot`, `slotless_viewer_watches_the_recording` and `economy_records_give_income_figures` in `src/session/demo/tests/demo_playback_test.cpp` (ctest `demo-playback`); `slotless_viewers_see_every_unit` in `src/sim/match-runtime/tests/intel_test.cpp` (ctest `match-intel`); `test_viewer_allied_with_every_player` in `src/ui/hud/tests/unit_panel_test.cpp` (ctest `ui-hud-unit-panel`); `every_rule_maps` in `src/app/netgame/tests/wire_rules_test.cpp` (ctest `netgame-wire-rules`).

**Known limits.**

- Both forms show the whole map. A player allied with everyone under [intel.allied-los-sharing](intel.allied-los-sharing.md) would see only what some recorded player sees.
- Elsewhere the first recorded player's slot still stands for the viewer's own: the unit panel shows that player's units as the viewer's.

## Full configuration schema

<!-- BEGIN GENERATED: schema -->
Hack id `recorder.ten-player-replay`, written under the profile's `hacks` block.

| Write | Means |
| --- | --- |
| `recorder.ten-player-replay: true` | On, every parameter at its default. |
| `recorder.ten-player-replay: {form: watcher-view}` | On, the parameters named set and the rest at their defaults. |
| `recorder.ten-player-replay: {preset: baseline}` | On, starting from a preset; parameters named beside `preset` replace its values. |
| `recorder.ten-player-replay: watcher-view` | On, the shorthand: a bare value sets `form`. |
| `recorder.ten-player-replay: false` | Off, the same as leaving it out: 3.1c behaviour. |

### Parameters

| Parameter | Type | Unit | Allowed | Length | Baseline (3.1c) | Default | Adjustable | Scope | Overridden by |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `form` | `enum` | - | `off`, `watcher-view`, `allied-fake-player` | - | `off` | `watcher-view` | `fixed` | `sim` | - |

Adjustable: `fixed`: only the profile sets it.

### Presets

| Preset | Values |
| --- | --- |
| `baseline` | `{form: off}` |

`baseline` sets every parameter to its 3.1c value: the hack is on and plays as 3.1c.

### Shorthand

The hack has one parameter, so a bare value sets `form`: `recorder.ten-player-replay: watcher-view` is `recorder.ten-player-replay: {form: watcher-view}`.

### Every parameter at its default

```yaml
hacks:
  recorder.ten-player-replay:
    form: watcher-view
```
<!-- END GENERATED: schema -->
