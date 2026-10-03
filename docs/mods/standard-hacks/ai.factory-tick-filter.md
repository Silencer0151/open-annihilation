# Factory Tick Filter

<!-- BEGIN GENERATED: facts -->
| Fact | Value |
| --- | --- |
| Hack id | `ai.factory-tick-filter` |
| Area | AI (`ai`) |
| Scope | sim: part of the profile hash; every machine must agree |
| Runs on | The machine that owns the unit or player concerned. |
| Parameters | 2 |
| Status | implemented |
<!-- END GENERATED: facts -->

## Description

The computer player's factory and power management passes over buildings that are busy with a background order, such as a stockpile build, and switches on and off any building that uses 32 or more energy. In 3.1c it gives orders to busy buildings and switches only metal makers.

## Configuration example

```yaml
hacks:
  ai.factory-tick-filter: true   # skip busy buildings; toggle every building using 32+ energy
```

```yaml
hacks:
  ai.factory-tick-filter:
    power-toggle: makes-metal   # keep 3.1c's metal-maker toggle; still skip busy buildings
```

## Details

### Behaviour

The computer player's structures task runs every 30 ticks over the buildings of its structures squad. For each live building it does the following, in order.

1. **Busy buildings** (`skip-busy-background-queue: true`): a building whose background order queue holds an order is left alone for this run: no power switch and no factory order. The background queue is the unit's second order list, where stockpile builds run.
2. **Power switch** (`power-toggle`): when the building qualifies, the task switches it and goes on to the next building:
   - if the player's stored energy is more than twice its stored metal and its energy produced exceeds its energy requested, the task switches the building on, except that one draw in five from the simulation's random stream leaves it as it is;
   - if stored energy is more than twice stored metal but production does not exceed demand, the building is left as it is;
   - otherwise the task switches the building off.

   Under `makes-metal` a building qualifies when its type's `MakesMetal` is not zero. Under `energy-use-32` it qualifies when its type's `EnergyUse` is 32 or more, and `MakesMetal` no longer matters. The test reads the top byte of the `EnergyUse` float as a signed number and compares it with that of 32.0, so any negative `EnergyUse` (an energy producer) never qualifies.
3. **Factory order:** any other building with a build list and no current order gets one pick from the computer player's build choice.

A building that qualifies for the power switch never gets a factory order from this task. Under `energy-use-32` that includes a factory whose own `EnergyUse` is 32 or more. A metal maker that uses less than 32 energy is no longer switched.

### Baseline (3.1c)

The task does not look at the background queue, and only buildings with `MakesMetal` set are switched. The `baseline` preset (`skip-busy-background-queue: false`, `power-toggle: makes-metal`) plays exactly as 3.1c.

### Network play

In a network game the machine that hosts the computer player runs its AI and applies the hack; the orders the AI gives reach the other machines as any computer player's orders do. Every machine must still load the same setting: the hack is part of the profile hash the machines compare. The power switch's draw comes from the simulation's random stream.

### Interactions

- [ai.squad5-factory-tick](ai.squad5-factory-tick.md) runs the same task over the armed structures squad, so the filter covers stockpile weapons that build in the background.
- [ai.squad-assignment](ai.squad-assignment.md) decides which buildings join the squads this task runs over.

### Implementation notes

- The rules record is `AiFactoryTickFilter` (`rules.ai.factory_tick_filter`, fields `skip_busy_background_queue` and `power_toggle`) in `src/data/match-rules/include/oa/data/match_rules/records.inc`.
- `run_structures` and `runs_power_toggle` in `src/sim/ai/src/computer_player.cpp` apply it. `ComputerType.energy_use` holds the type's `EnergyUse`.
- The host query `ComputerHost::secondary_order` (`src/sim/ai/include/oa/sim/ai.hpp`) answers whether the background queue holds an order; the match answers it from `Unit.secondary_order` in `host_secondary_order`, `src/sim/match-runtime/src/computer_host.cpp`.
- Tests: ctest `ai-computer-player` (`test_factory_tick_filter`) and `match-computer-build` (`computer_rules_through_the_match`).

## Full configuration schema

<!-- BEGIN GENERATED: schema -->
Hack id `ai.factory-tick-filter`, written under the profile's `hacks` block.

| Write | Means |
| --- | --- |
| `ai.factory-tick-filter: true` | On, every parameter at its default. |
| `ai.factory-tick-filter: {skip-busy-background-queue: true}` | On, the parameters named set and the rest at their defaults. |
| `ai.factory-tick-filter: {preset: baseline}` | On, starting from a preset; parameters named beside `preset` replace its values. |
| `ai.factory-tick-filter: false` | Off, the same as leaving it out: 3.1c behaviour. |

### Parameters

| Parameter | Type | Unit | Allowed | Length | Baseline (3.1c) | Default | Adjustable | Scope | Overridden by |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `skip-busy-background-queue` | `bool` | - | `true`, `false` | - | `false` | `true` | `fixed` | `sim` | - |
| `power-toggle` | `enum` | - | `makes-metal`, `energy-use-32` | - | `makes-metal` | `energy-use-32` | `fixed` | `sim` | - |

Adjustable: `fixed`: only the profile sets it.

### Presets

| Preset | Values |
| --- | --- |
| `baseline` | `{skip-busy-background-queue: false, power-toggle: makes-metal}` |

`baseline` sets every parameter to its 3.1c value: the hack is on and plays as 3.1c.

### Shorthand

This hack takes no shorthand: write `true`, `false` or a parameter map.

### Every parameter at its default

```yaml
hacks:
  ai.factory-tick-filter:
    skip-busy-background-queue: true
    power-toggle: energy-use-32
```
<!-- END GENERATED: schema -->
