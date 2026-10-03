# Commander Builder Limit

<!-- BEGIN GENERATED: facts -->
| Fact | Value |
| --- | --- |
| Hack id | `ai.builder-withhold-threshold` |
| Area | AI (`ai`) |
| Scope | sim: part of the profile hash; every machine must agree |
| Runs on | The machine that owns the unit or player concerned. |
| Parameters | 1 |
| Status | implemented |
<!-- END GENERATED: facts -->

## Description

A computer player's commander keeps building until the player owns N build-capable units (default 10). In 3.1c it stops taking build jobs once the player owns five, and leaves building to the other builders.

## Configuration example

```yaml
hacks:
  ai.builder-withhold-threshold: true   # the commander builds until the player has 10 builders
```

```yaml
hacks:
  ai.builder-withhold-threshold: 0   # the commander never takes a build job
```

## Details

### Behaviour

The computer player's construction task runs every 90 ticks over its builders squad. A builder whose type has the capture ability (`CanCapture`) is what the computer player treats as its commander. Such a builder is given no build job while either holds:

- the player owns at least `builders` units whose type has a build list (the commander counts itself; the count is refreshed when the computer player updates what it knows of its own units), or
- the builder is held after being damaged (see [ai.commander-keeps-orders-when-damaged](ai.commander-keeps-orders-when-damaged.md)).

Below the threshold the commander builds as in 3.1c, and a site it picks must lie within reach of the base.

The hack moves only the build limit. An idle commander still starts circling the base, at about 640 world units, once the player owns five build-capable units, whatever `builders` says. So with a threshold above five, a commander can circle the base while idle and still take build jobs.

A threshold of 0 means the commander never builds.

### Baseline (3.1c)

The commander takes build jobs while the player owns fewer than five build-capable units. The `baseline` preset (`builders: 5`) plays exactly as 3.1c.

### Network play

In a network game the machine that hosts the computer player runs its AI and applies the hack; the orders the AI gives reach the other machines as any computer player's orders do. Every machine must still load the same setting: the hack is part of the profile hash the machines compare.

### Interactions

- [ai.commander-keeps-orders-when-damaged](ai.commander-keeps-orders-when-damaged.md) removes the build hold that damage starts.

### Implementation notes

- The rules record is `AiBuilderWithholdThreshold` (`rules.ai.builder_withhold_threshold.builders`) in `src/data/match-rules/include/oa/data/match_rules/records.inc`.
- `run_construction` in `src/sim/ai/src/computer_player.cpp` compares it with the player's builder count (`builder_count`, counted in `src/sim/ai/src/computer_knowledge.cpp`). The idle circling keeps its own limit of five (`commander_patrol_quota`).
- Tests: ctest `ai-computer-player` (`test_builder_withhold_threshold`) and `match-computer-build` (`computer_rules_through_the_match`).

## Full configuration schema

<!-- BEGIN GENERATED: schema -->
Hack id `ai.builder-withhold-threshold`, written under the profile's `hacks` block.

| Write | Means |
| --- | --- |
| `ai.builder-withhold-threshold: true` | On, every parameter at its default. |
| `ai.builder-withhold-threshold: {builders: 10}` | On, the parameters named set and the rest at their defaults. |
| `ai.builder-withhold-threshold: {preset: baseline}` | On, starting from a preset; parameters named beside `preset` replace its values. |
| `ai.builder-withhold-threshold: 10` | On, the shorthand: a bare value sets `builders`. |
| `ai.builder-withhold-threshold: false` | Off, the same as leaving it out: 3.1c behaviour. |

### Parameters

| Parameter | Type | Unit | Allowed | Length | Baseline (3.1c) | Default | Adjustable | Scope | Overridden by |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `builders` | `int` | builders | `0` to `1500` | - | `5` | `10` | `fixed` | `sim` | - |

Adjustable: `fixed`: only the profile sets it.

### Presets

| Preset | Values |
| --- | --- |
| `baseline` | `{builders: 5}` |

`baseline` sets every parameter to its 3.1c value: the hack is on and plays as 3.1c.

### Shorthand

The hack has one parameter, so a bare value sets `builders`: `ai.builder-withhold-threshold: 10` is `ai.builder-withhold-threshold: {builders: 10}`.

### Every parameter at its default

```yaml
hacks:
  ai.builder-withhold-threshold:
    builders: 10
```
<!-- END GENERATED: schema -->
