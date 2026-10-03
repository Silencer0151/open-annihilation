# Water State Rules

<!-- BEGIN GENERATED: facts -->
| Fact | Value |
| --- | --- |
| Hack id | `units.water-state-rules` |
| Area | Units (`units`) |
| Scope | sim: part of the profile hash; every machine must agree |
| Runs on | Every machine in the game. |
| Parameters | 2 |
| Status | implemented |
<!-- END GENERATED: facts -->

## Description

Changes how the engine works out a unit's water state, the code it passes to the unit script's `SetSFXoccupy`. Every unit at or below its waterline gets a state, where 3.1c leaves some deep units with a stale one, and a unit created fully under water can start in the submerged state.

## Configuration example

```yaml
hacks:
  units.water-state-rules: true
```

```yaml
hacks:
  units.water-state-rules:
    rules: reordered
    start-submerged: false   # new units start with state 0, as in 3.1c
```

## Details

### The water states

A ground or air unit has one of these states; the script is told through `SetSFXoccupy` each time it changes, and only then:

| Code | State |
| --- | --- |
| 0 | none (any other movement layer) |
| 1 | surface: less than 5 below sea level |
| 2 | waterline: the unit's waterline is at sea level |
| 3 | submerged: the top of the model is below sea level |
| 4 | above: the unit stands above sea level |

### Behaviour

`rules` chooses how a unit at or below sea level is placed. The checks run in order and the last one that matches wins; when none matches the unit keeps its previous code.

- `base` (3.1c): surface (1) when less than 5 below the sea; waterline (2) when the unit's height plus its `WaterLine` is exactly sea level; submerged (3) when the model top is below the sea.
- `reordered`: waterline (2) when the unit's height plus its `WaterLine` is at or below sea level; then surface (1) when less than 5 below the sea; then submerged (3) when the model top is below the sea.

So under `reordered`, a shallow floater is at the surface (1), not at its waterline (2), and a unit deeper than 5 whose waterline is below the sea gets the waterline state instead of keeping a stale code.

`start-submerged` sets the state of a newly created unit: when the top of its model is below sea level, it starts with the low byte of its stored code at 3 (submerged), and 0 otherwise. The script is not called for this starting state; it is called at the first change after it. The sea level is read as a 16-bit word that also holds the debug overlay mode, so while that overlay is shown the comparison uses a sea level 256 higher.

### Baseline

3.1c uses the `base` order and starts every unit with code 0. Some unit scripts rely on 3.1c's codes (for example, treating 1 and 2 as water and 4 as land), so a profile switches this hack on only when its scripts expect the new states.

### Network games

The state is updated in the movement tick and passed to the unit script on every machine, since every machine runs the scripts. Every machine must use the same rules.

### Interactions

- [`ai.nearest-enemy-filter`](ai.nearest-enemy-filter.md) reads the water state, so the states this hack gives change which enemies the computer player sees as targets.
- Script effects that depend on the state, such as wakes and weapon choice, follow the new codes.

### Implementation notes

- `update_sea_occupy` in `src/sim/world-environment/src/sea.cpp` runs the checks; `SeaOccupyHost::reordered` (`src/sim/world-environment/include/oa/sim/world_environment/wind.hpp`) chooses the order. The movement tick in `src/sim/match-runtime/src/tick_host_movement.cpp` sets it from the rules record field `units.water_state_rules.rules`.
- `Match::keep_unit_rules` in `src/sim/match-runtime/src/unit_rules.cpp` hands `SpawnRules::start_submerged` to unit creation from `units.water_state_rules.enabled` and `.start_submerged`; `src/sim/unit-spawn/src/spawn.cpp` sets the starting code.
- Tested by `world-environment` (the sea occupy cases under both orders), `unit-spawn` (`created_under_the_sea_starts_submerged`, including the debug overlay case) and `match-unit-rules` (`water_state_rules_in_the_match`).

## Full configuration schema

<!-- BEGIN GENERATED: schema -->
Hack id `units.water-state-rules`, written under the profile's `hacks` block.

| Write | Means |
| --- | --- |
| `units.water-state-rules: true` | On, every parameter at its default. |
| `units.water-state-rules: {rules: reordered}` | On, the parameters named set and the rest at their defaults. |
| `units.water-state-rules: {preset: baseline}` | On, starting from a preset; parameters named beside `preset` replace its values. |
| `units.water-state-rules: false` | Off, the same as leaving it out: 3.1c behaviour. |

### Parameters

| Parameter | Type | Unit | Allowed | Length | Baseline (3.1c) | Default | Adjustable | Scope | Overridden by |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `rules` | `enum` | - | `base`, `reordered` | - | `base` | `reordered` | `fixed` | `sim` | - |
| `start-submerged` | `bool` | - | `true`, `false` | - | `false` | `true` | `fixed` | `sim` | - |

Adjustable: `fixed`: only the profile sets it.

### Presets

| Preset | Values |
| --- | --- |
| `baseline` | `{rules: base, start-submerged: false}` |

`baseline` sets every parameter to its 3.1c value: the hack is on and plays as 3.1c.

### Shorthand

This hack takes no shorthand: write `true`, `false` or a parameter map.

### Every parameter at its default

```yaml
hacks:
  units.water-state-rules:
    rules: reordered
    start-submerged: true
```
<!-- END GENERATED: schema -->
