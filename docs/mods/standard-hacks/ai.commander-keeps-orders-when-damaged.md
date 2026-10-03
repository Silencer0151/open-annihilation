# Commander Keeps Orders When Hit

<!-- BEGIN GENERATED: facts -->
| Fact | Value |
| --- | --- |
| Hack id | `ai.commander-keeps-orders-when-damaged` |
| Area | AI (`ai`) |
| Scope | sim: part of the profile hash; every machine must agree |
| Runs on | The machine that owns the unit or player concerned. |
| Parameters | 0 |
| Status | implemented |
<!-- END GENERATED: facts -->

## Description

A computer player's commander keeps its orders when it is hit. In 3.1c, damage to it clears all its orders and stops it taking build jobs for a short while.

## Configuration example

```yaml
hacks:
  ai.commander-keeps-orders-when-damaged: true
```

## Details

### Behaviour

In 3.1c, when a unit takes damage and its type has the capture ability (`CanCapture`, which the computer player treats as its commander) and its owner is a computer player, the game:

1. draws a number from 0 to 299 from the simulation's random stream;
2. holds that player's commander from build jobs until the current tick plus 30 plus the draw;
3. clears the damaged unit's order queue.

With the hack on, none of this happens, the random draw included. The random stream therefore advances differently from 3.1c from the first such hit on.

The rest of the reaction to damage runs as in 3.1c: weapons still turn on the attacker, and the attack notice still sounds. Units owned by human players were never affected, and still are not.

### Baseline (3.1c)

As in the list above. Leaving the hack out, or writing `false`, keeps it.

### Network play

The damage reaction runs on the machine that owns the damaged unit, which for a computer player is the machine that hosts it. Every machine must load the same setting: the hack is part of the profile hash the machines compare, and it changes the simulation's random draws.

### Interactions

- [ai.builder-withhold-threshold](ai.builder-withhold-threshold.md): the build hold this hack removes is one of the two reasons a commander takes no build job.

### Implementation notes

- The rules record is `AiCommanderKeepsOrdersWhenDamaged` (`rules.ai.commander_keeps_orders_when_damaged.enabled`) in `src/data/match-rules/include/oa/data/match_rules/records.inc`.
- `retaliate` in `src/sim/weapon-execution/src/retaliation.cpp` skips the computer player's alert, and its draw, when the hack is on.
- With the hack off, the damage alert in `src/sim/match-runtime/src/tick_damage.cpp` calls `hold_capturer_builds` (`src/sim/match-runtime/src/computer_host.cpp`) and clears the unit's orders.
- Tests: ctest `weapon-retaliation` (`commander_keeps_orders`) and `match-computer-build` (`computer_rules_through_the_match`).

## Full configuration schema

<!-- BEGIN GENERATED: schema -->
Hack id `ai.commander-keeps-orders-when-damaged`, written under the profile's `hacks` block.

| Write | Means |
| --- | --- |
| `ai.commander-keeps-orders-when-damaged: true` | On. |
| `ai.commander-keeps-orders-when-damaged: false` | Off, the same as leaving it out: 3.1c behaviour. |

### Parameters

This hack has no parameters.

### Presets

This hack has no presets.

### Shorthand

This hack has no parameters to set: write `true` or `false`.

### Every parameter at its default

```yaml
hacks:
  ai.commander-keeps-orders-when-damaged: true
```
<!-- END GENERATED: schema -->
