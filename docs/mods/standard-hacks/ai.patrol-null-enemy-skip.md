# Patrol Edge Fallback

<!-- BEGIN GENERATED: facts -->
| Fact | Value |
| --- | --- |
| Hack id | `ai.patrol-null-enemy-skip` |
| Area | AI (`ai`) |
| Scope | sim: part of the profile hash; every machine must agree |
| Runs on | The machine that owns the unit or player concerned. |
| Parameters | 0 |
| Status | implemented |
<!-- END GENERATED: facts -->

## Description

A computer player's small air wing that has no base yet patrols to a random map edge instead of heading for the nearest enemy. In 3.1c the same wing is sent at the nearest enemy unit, which the search may not find.

## Configuration example

```yaml
hacks:
  ai.patrol-null-enemy-skip: true
```

## Details

### Behaviour

Each computer player runs an aircraft task for its aircraft squad. The task looks at two things:

- **Wing size.** A wing is small when it has fewer members than the patrol group size (5 in 3.1c; see [`ai.patrol-group-size`](ai.patrol-group-size.md)).
- **Base.** The player has no base while its base position is still the map corner (0, 0): it has no armed structures, no other structures and no builders to take a centre from.

With the hack on, a small wing whose player has no base is treated as a large wing: it patrols to a random point on a random map edge. The edge point is drawn exactly as a large wing draws it (a coin for the axis, a random coordinate along it and a coin for which of the two edges), so the random stream advances by those draws.

A small wing whose player has a base still scouts around the base, and a large wing still patrols to a map edge, as in 3.1c.

### 3.1c baseline

A small wing with no base takes the centre of its own members, searches for the nearest enemy unit from there, and queues a patrol to that unit's position. When the search finds no enemy, the engine gives no order. The hack removes that path, so the wing always gets an order and makes no nearest-enemy search.

### Network games

The machine that owns the computer player runs its tasks. The hack is part of the profile hash, so every machine runs with the same setting and draws the same random numbers.

### Interactions

- [`ai.patrol-group-size`](ai.patrol-group-size.md) sets the size below which a wing counts as small.
- [`ai.nearest-enemy-filter`](ai.nearest-enemy-filter.md) changes the nearest-enemy search that this hack skips.

### Implementation notes

- Applied in `run_air_raid` in `src/sim/ai/src/computer_player.cpp` (module `src/sim/ai`), which reads `MatchRules::ai.patrol_null_enemy_skip.enabled`.
- Tested by `test_patrol_null_enemy_skip` in `src/sim/ai/tests/computer_player_test.cpp` (ctest `ai-computer-player`): with the hack off the wing queues a patrol to the enemy, with it on the wing patrols to an edge and the edge's draws are taken. `src/sim/match-runtime/tests/computer_build_test.cpp` (ctest `match-computer-build`) plays a match with every `ai.*` hack on and checks that it replays identically.

## Full configuration schema

<!-- BEGIN GENERATED: schema -->
Hack id `ai.patrol-null-enemy-skip`, written under the profile's `hacks` block.

| Write | Means |
| --- | --- |
| `ai.patrol-null-enemy-skip: true` | On. |
| `ai.patrol-null-enemy-skip: false` | Off, the same as leaving it out: 3.1c behaviour. |

### Parameters

This hack has no parameters.

### Presets

This hack has no presets.

### Shorthand

This hack has no parameters to set: write `true` or `false`.

### Every parameter at its default

```yaml
hacks:
  ai.patrol-null-enemy-skip: true
```
<!-- END GENERATED: schema -->
