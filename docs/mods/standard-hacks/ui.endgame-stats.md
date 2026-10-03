# End-of-Game Statistics

<!-- BEGIN GENERATED: facts -->
| Fact | Value |
| --- | --- |
| Hack id | `ui.endgame-stats` |
| Area | Interface (`ui`) |
| Scope | view: local display only; each player may differ |
| Runs on | Each machine, for its own view. |
| Parameters | 0 |
| Status | implemented |
<!-- END GENERATED: facts -->

## Description

The end-of-game statistics list players who dropped out of the game or
were ejected from it, alongside everyone else, where 3.1c leaves them
out.

## Configuration example

```yaml
hacks:
  ui.endgame-stats: true
```

The hack has no parameters.

## Details

### Behaviour

- The statistics list every active player who is not a watcher, and every
  player who has left after building units, whatever the reason they left
  (dropped, ejected or otherwise). A dropped or ejected player's kills,
  losses, resources produced and wasted, and score show as any other
  player's.
- A watcher who never built a unit is still left out.

### Network games

Each machine builds its own end screen from its own copy of the match.
Nothing needs to agree between machines.

### Implementation notes

- The statistics are built by `build_score_summary` in
  [src/ui/campaign/endgame.cpp](../../../src/ui/campaign/endgame.cpp),
  called from [src/app/runtime_endgame.cpp](../../../src/app/runtime_endgame.cpp).
  It decides who is listed by whether the player is active, a watcher, or
  has built units, and never by why the player left.
- Tests: `ui-campaign-endgame` (`dropped_player_tests`).
- Known limit: the engine already lists dropped and ejected players whether
  or not the hack is on, so `ModProfile::ui.endgame_stats.enabled` is
  accepted and read by nothing. Without the hack the engine does not leave
  dropped players out of the list.

## Full configuration schema

<!-- BEGIN GENERATED: schema -->
Hack id `ui.endgame-stats`, written under the profile's `hacks` block.

| Write | Means |
| --- | --- |
| `ui.endgame-stats: true` | On. |
| `ui.endgame-stats: false` | Off, the same as leaving it out: 3.1c behaviour. |

### Parameters

This hack has no parameters.

### Presets

This hack has no presets.

### Shorthand

This hack has no parameters to set: write `true` or `false`.

### Every parameter at its default

```yaml
hacks:
  ui.endgame-stats: true
```
<!-- END GENERATED: schema -->
