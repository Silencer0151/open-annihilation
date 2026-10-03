# Mobile Unit Yard Maps

<!-- BEGIN GENERATED: facts -->
| Fact | Value |
| --- | --- |
| Hack id | `units.mobile-unit-yardmap` |
| Area | Units (`units`) |
| Scope | sim: part of the profile hash; every machine must agree |
| Runs on | Every machine in the game. |
| Parameters | 0 |
| Status | implemented |
<!-- END GENERATED: facts -->

## Description

A mobile unit's `YardMap` key is read and compiled into a yard, as a building's is. In 3.1c only buildings have a yard, so a mobile unit placed on the map is tested against a plain footprint; with this hack its own yard cells decide which ground it needs.

## Configuration example

```yaml
hacks:
  units.mobile-unit-yardmap: true
```

## Details

### Behaviour

- When a unit file loads, a unit with a nonzero `BMcode` (a mobile unit) has its `YardMap` text compiled into a yard of `FootprintX` × `FootprintZ` cells, exactly as a building's is.
- A mobile unit whose file has no `YardMap` key gets a yard of empty cells, as a building without the key does in 3.1c. With [`units.skip-empty-yardmap`](units.skip-empty-yardmap.md) on as well it gets no yard at all.
- The yard is read where the engine tests a building site: when a mobile unit is placed on the map as a frame (see [`units.placement-by-builder`](units.placement-by-builder.md)), its yard cells say which cells must be level, which must be unclaimed and which must be free of units. A type without a yard is tested with the default cell everywhere.
- A finished mobile unit still occupies the map by its footprint alone; the yard does not change how it moves or blocks other units.
- A mobile unit whose yard is named but cannot be built (for example, the block could not be allocated) is a load error, as it is for a building.

### Baseline

3.1c reads `YardMap` only for buildings (`BMcode=0`). Mobile units never have a yard.

### Network games

The yard is part of the loaded unit definitions, so every machine builds it. Every machine must have the hack in the same state and load the same unit files, or their site tests disagree.

### Interactions

- [`units.placement-by-builder`](units.placement-by-builder.md) is what lets a player place a mobile unit on the map; it needs this hack for the frame's site to be tested by the unit's own yard.
- [`units.skip-empty-yardmap`](units.skip-empty-yardmap.md) decides whether a mobile unit without the key gets an empty yard or none.
- [`units.build-rotation`](units.build-rotation.md) turns the yard of a type that may face other ways.

### Implementation notes

- The FBI loader reads the switch through `data::defs::YardMapRules::mobile_units`, filled by `data::defs::yard_map_rules` from the rules record field `units.mobile_unit_yardmap.enabled` (`src/data/defs/include/oa/data/defs/unit_def_loader.hpp`). `load_yard_map` in `src/data/defs/src/unit_def_loader.cpp` compiles the yard.
- `resolve_runtime_metadata` in `src/data/unit-definitions/src/unit_definitions.cpp` turns the compiled block into the type's yard cells. The app passes the rules in `src/app/runtime_skirmish_start.cpp` and `src/app/runtime_console.cpp`.
- The site test that reads the yard is `Match::building_site` in `src/sim/match-runtime/src/tick_construction.cpp`, through `Match::build_yard` in `src/sim/match-runtime/src/unit_rules.cpp`.
- Tested by `unit-definitions` (`yard_maps_follow_the_unit_rules`), which loads mobile units and buildings with and without the key under each combination of this hack and `units.skip-empty-yardmap`.

## Full configuration schema

<!-- BEGIN GENERATED: schema -->
Hack id `units.mobile-unit-yardmap`, written under the profile's `hacks` block.

| Write | Means |
| --- | --- |
| `units.mobile-unit-yardmap: true` | On. |
| `units.mobile-unit-yardmap: false` | Off, the same as leaving it out: 3.1c behaviour. |

### Parameters

This hack has no parameters.

### Presets

This hack has no presets.

### Shorthand

This hack has no parameters to set: write `true` or `false`.

### Every parameter at its default

```yaml
hacks:
  units.mobile-unit-yardmap: true
```
<!-- END GENERATED: schema -->
