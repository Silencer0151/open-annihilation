# Skip Empty Yard Maps

<!-- BEGIN GENERATED: facts -->
| Fact | Value |
| --- | --- |
| Hack id | `units.skip-empty-yardmap` |
| Area | Units (`units`) |
| Scope | sim: part of the profile hash; every machine must agree |
| Runs on | Every machine in the game. |
| Parameters | 0 |
| Status | implemented |
<!-- END GENERATED: facts -->

## Description

A unit file with no `YardMap` key, or with a footprint 0 cells wide or deep, gets no yard block when it loads. Play does not change: such a building is still treated as having a yard of empty cells.

## Configuration example

```yaml
hacks:
  units.skip-empty-yardmap: true
```

## Details

### Behaviour

- When a unit file loads, the loader builds no yard for it when either is true:
  - the file has no `YardMap` key;
  - `FootprintX` or `FootprintZ` is 0.
- A `YardMap` key with no text is still a key: the yard is built as usual (all cells empty).
- A building left without a yard is resolved, for the simulation, to a yard of empty cells, the same cells 3.1c gives it. Site tests, occupancy and construction therefore behave as in 3.1c; what changes is that the loader does not build a yard block it would not use.
- With [`units.mobile-unit-yardmap`](units.mobile-unit-yardmap.md) on, a mobile unit without the key also gets no yard, where it would otherwise get an empty one.

### Baseline

3.1c builds a yard for every building, of `FootprintX` × `FootprintZ` cells, and compiles the `YardMap` text into it; a building without the key gets a yard of empty cells.

### Network games

The decision is made when the unit definitions load, on every machine. Every machine must have the hack in the same state and load the same unit files.

### Interactions

- [`units.mobile-unit-yardmap`](units.mobile-unit-yardmap.md): the skip applies to mobile units too when that hack gives them yards.

### Implementation notes

- `load_yard_map` in `src/data/defs/src/unit_def_loader.cpp` applies the skip through `data::defs::YardMapRules::skip_without_key`, filled from the rules record field `units.skip_empty_yardmap.enabled` by `data::defs::yard_map_rules` (`src/data/defs/include/oa/data/defs/unit_def_loader.hpp`).
- `resolve_runtime_metadata` in `src/data/unit-definitions/src/unit_definitions.cpp` gives a building skipped this way its yard of empty cells.
- Tested by `unit-definitions` (`yard_maps_follow_the_unit_rules`): buildings with no key, an empty key and a zero-wide footprint, and mobile units with and without the key, under each combination of the two yard hacks.

## Full configuration schema

<!-- BEGIN GENERATED: schema -->
Hack id `units.skip-empty-yardmap`, written under the profile's `hacks` block.

| Write | Means |
| --- | --- |
| `units.skip-empty-yardmap: true` | On. |
| `units.skip-empty-yardmap: false` | Off, the same as leaving it out: 3.1c behaviour. |

### Parameters

This hack has no parameters.

### Presets

This hack has no presets.

### Shorthand

This hack has no parameters to set: write `true` or `false`.

### Every parameter at its default

```yaml
hacks:
  units.skip-empty-yardmap: true
```
<!-- END GENERATED: schema -->
