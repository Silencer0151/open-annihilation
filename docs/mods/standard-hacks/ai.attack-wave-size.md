# Attack Wave Size

<!-- BEGIN GENERATED: facts -->
| Fact | Value |
| --- | --- |
| Hack id | `ai.attack-wave-size` |
| Area | AI (`ai`) |
| Scope | sim: part of the profile hash; every machine must agree |
| Runs on | The machine that owns the unit or player concerned. |
| Parameters | 1 |
| Status | implemented |
<!-- END GENERATED: facts -->

## Description

The computer player's land and naval attack groups gather until they hold N units (default 10) before they set off. In 3.1c they set off at six. Once attacking, a group keeps attacking while it has more than three units, as in 3.1c.

## Configuration example

```yaml
hacks:
  ai.attack-wave-size: true   # attack groups set off at 10 units
```

```yaml
hacks:
  ai.attack-wave-size: 20   # larger, later attacks
```

## Details

### Behaviour

A computer player has two attack groups: a land group that recruits from its land army and a naval group that recruits from its navy. Each runs every 300 ticks. After recruiting, a group with more than three members attacks when it is already attacking or when it holds at least `units` members. Otherwise, when the player has a base, the group moves back to the base and stops attacking.

An attacking group orders all its members to attack the nearest enemy unit to the group's centre.

The floor of three is fixed: a group of three or fewer never attacks, so a value of `units` below 4 behaves as 4. The value is read when the computer player is set up at the start of the game and holds for the whole game.

### Baseline (3.1c)

Both groups set off at six members. The `baseline` preset (`units: 6`) plays exactly as 3.1c.

### Network play

In a network game the machine that hosts the computer player runs its AI and applies the hack; the orders the AI gives reach the other machines as any computer player's orders do. Every machine must still load the same setting: the hack is part of the profile hash the machines compare.

### Interactions

- [ai.nearest-enemy-filter](ai.nearest-enemy-filter.md) changes which enemy an attacking group picks.
- [ai.squad-assignment](ai.squad-assignment.md) changes which units join the land army and the navy that the groups recruit from.

### Implementation notes

- The rules record is `AiAttackWaveSize` (`rules.ai.attack_wave_size.units`) in `src/data/match-rules/include/oa/data/match_rules/records.inc`.
- `computer_player_create` in `src/sim/ai/src/computer_player.cpp` sets the launch size of both strike tasks from it; `run_strike` in the same file decides when a group attacks.
- Tests: ctest `ai-computer-player` (`test_attack_wave_size`) and `match-computer-build` (`computer_rules_through_the_match`).

## Full configuration schema

<!-- BEGIN GENERATED: schema -->
Hack id `ai.attack-wave-size`, written under the profile's `hacks` block.

| Write | Means |
| --- | --- |
| `ai.attack-wave-size: true` | On, every parameter at its default. |
| `ai.attack-wave-size: {units: 10}` | On, the parameters named set and the rest at their defaults. |
| `ai.attack-wave-size: {preset: baseline}` | On, starting from a preset; parameters named beside `preset` replace its values. |
| `ai.attack-wave-size: 10` | On, the shorthand: a bare value sets `units`. |
| `ai.attack-wave-size: false` | Off, the same as leaving it out: 3.1c behaviour. |

### Parameters

| Parameter | Type | Unit | Allowed | Length | Baseline (3.1c) | Default | Adjustable | Scope | Overridden by |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `units` | `int` | units | `1` to `1500` | - | `6` | `10` | `fixed` | `sim` | - |

Adjustable: `fixed`: only the profile sets it.

### Presets

| Preset | Values |
| --- | --- |
| `baseline` | `{units: 6}` |

`baseline` sets every parameter to its 3.1c value: the hack is on and plays as 3.1c.

### Shorthand

The hack has one parameter, so a bare value sets `units`: `ai.attack-wave-size: 10` is `ai.attack-wave-size: {units: 10}`.

### Every parameter at its default

```yaml
hacks:
  ai.attack-wave-size:
    units: 10
```
<!-- END GENERATED: schema -->
