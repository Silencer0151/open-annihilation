# Patrol Group Size

<!-- BEGIN GENERATED: facts -->
| Fact | Value |
| --- | --- |
| Hack id | `ai.patrol-group-size` |
| Area | AI (`ai`) |
| Scope | sim: part of the profile hash; every machine must agree |
| Runs on | The machine that owns the unit or player concerned. |
| Parameters | 1 |
| Status | implemented |
<!-- END GENERATED: facts -->

## Description

The computer player's aircraft patrol out to a random point on the map edge only once the wing holds N aircraft (default 15). Smaller wings stay near the base and scout around it. In 3.1c a wing goes out at five.

## Configuration example

```yaml
hacks:
  ai.patrol-group-size: true   # wings of 15 or more patrol to the map edge
```

```yaml
hacks:
  ai.patrol-group-size: 8   # wings go out sooner
```

## Details

### Behaviour

The computer player's aircraft task runs every 30 ticks plus a random 0 to 899. It counts the members of the aircraft squad.

- **A wing of `units` or more** patrols to a random point on the map edge: the task draws a side of the map, then a point along it.
- **A smaller wing, when the player has a base,** scouts around it: it moves to one random point and then queues one or two patrol points, each within a square one eighth of the map's width and height, centred on the base.
- **A smaller wing, when the player has no base** (its base position is the map origin), patrols toward the nearest enemy unit to the wing's centre. Under [ai.patrol-null-enemy-skip](ai.patrol-null-enemy-skip.md) it patrols to a random map-edge point instead.

A value of 1 sends every wing to the map edge.

### Baseline (3.1c)

Wings of five or more patrol to the map edge. The `baseline` preset (`units: 5`) plays exactly as 3.1c.

### Network play

In a network game the machine that hosts the computer player runs its AI and applies the hack; the orders the AI gives reach the other machines as any computer player's orders do. Every machine must still load the same setting: the hack is part of the profile hash the machines compare.

### Interactions

- [ai.patrol-null-enemy-skip](ai.patrol-null-enemy-skip.md) changes what a small wing does when the player has no base.
- [ai.nearest-enemy-filter](ai.nearest-enemy-filter.md) changes which enemy a small wing without a base patrols toward.
- [ai.squad-assignment](ai.squad-assignment.md) changes which units join the aircraft squad.

### Implementation notes

- The rules record is `AiPatrolGroupSize` (`rules.ai.patrol_group_size.units`) in `src/data/match-rules/include/oa/data/match_rules/records.inc`.
- `run_air_raid` in `src/sim/ai/src/computer_player.cpp` compares the wing's size with it.
- Tests: ctest `ai-computer-player` (`test_patrol_group_size`, `test_patrol_null_enemy_skip`) and `match-computer-build` (`computer_rules_through_the_match`).

## Full configuration schema

<!-- BEGIN GENERATED: schema -->
Hack id `ai.patrol-group-size`, written under the profile's `hacks` block.

| Write | Means |
| --- | --- |
| `ai.patrol-group-size: true` | On, every parameter at its default. |
| `ai.patrol-group-size: {units: 15}` | On, the parameters named set and the rest at their defaults. |
| `ai.patrol-group-size: {preset: baseline}` | On, starting from a preset; parameters named beside `preset` replace its values. |
| `ai.patrol-group-size: 15` | On, the shorthand: a bare value sets `units`. |
| `ai.patrol-group-size: false` | Off, the same as leaving it out: 3.1c behaviour. |

### Parameters

| Parameter | Type | Unit | Allowed | Length | Baseline (3.1c) | Default | Adjustable | Scope | Overridden by |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `units` | `int` | units | `1` to `1500` | - | `5` | `15` | `fixed` | `sim` | - |

Adjustable: `fixed`: only the profile sets it.

### Presets

| Preset | Values |
| --- | --- |
| `baseline` | `{units: 5}` |

`baseline` sets every parameter to its 3.1c value: the hack is on and plays as 3.1c.

### Shorthand

The hack has one parameter, so a bare value sets `units`: `ai.patrol-group-size: 15` is `ai.patrol-group-size: {units: 15}`.

### Every parameter at its default

```yaml
hacks:
  ai.patrol-group-size:
    units: 15
```
<!-- END GENERATED: schema -->
