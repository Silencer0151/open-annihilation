# Unit Slot Reuse Delay

<!-- BEGIN GENERATED: facts -->
| Fact | Value |
| --- | --- |
| Hack id | `units.id-reuse-delay` |
| Area | Units (`units`) |
| Scope | sim: part of the profile hash; every machine must agree |
| Runs on | The machine that owns the unit or player concerned. |
| Parameters | 1 |
| Status | implemented |
<!-- END GENERATED: facts -->

## Description

A dead unit's slot is not handed to a new unit until a delay has passed since its death. Late network records about the dead unit then cannot reach a newly built unit in the same slot. 3.1c reuses a slot as soon as it is free.

## Configuration example

```yaml
hacks:
  units.id-reuse-delay: true   # wait 150 ticks
```

```yaml
hacks:
  units.id-reuse-delay: 90     # shorthand: wait 90 ticks
```

## Details

**Recording a death.** When a dead unit's record is torn down, the engine records the tick from which its place in the owner's unit range may be taken again: the current game tick plus `ticks`.

**Creating a unit.** A creation on this machine that names no slot takes the first free place whose reuse tick the game tick has reached; the comparison is signed. When no free place has reached its tick, the creation fails, as it does when the range is full. A creation that names its slot is not held back. This covers a unit another machine created, whose record names the slot, and a unit restored from a save.

**Quirks the engine reproduces.**

- A creation or a death at game tick 0 first clears every reuse tick.
- The tick goes in the row of the player numbered by bits 8 to 23 of the unit's capture cooldown, at the unit's place in its owner's range. That number is 0, the first player, for every unit not handed over 256 or more ticks ago. A number past the last player records nothing. Only that row's own creations wait.
- `ticks: 0` keeps no state and behaves as 3.1c.

**3.1c behaviour.** A local creation takes the first free slot at once.

**Network play.** Each machine applies the delay to the creations it makes for its own players. Other machines create the unit in the slot the owner's record names. The delay is simulation state, and all machines must agree on `ticks`, which is part of the network hash. The reuse ticks are a rule-state table, `unit-slot-reuse`: one signed 32-bit tick per place in each player's range. The table is folded into the game's digest and kept in saves.

### Implementation notes

- The profile record is `rules().units.id_reuse_delay` (`enabled`, `ticks`).
- `Match::keep_unit_rules` in `src/sim/match-runtime/src/unit_rules.cpp` allocates the table, registers it as rule state, and hands it to unit creation as `sim::unit_spawn::SpawnRules::reuse_ticks` and `reuse_delay_ticks`.
- `create` and `record_slot_death` in `src/sim/unit-spawn/src/spawn.cpp` use the table.
- Tests:
  - `unit-spawn` (`slot_reuse_waits_for_its_tick`);
  - `match-unit-rules` (`slot_reuse_waits_in_the_match`: the waiting place, the digest, save and restore);
  - `data-mod-profile` (the shorthand, the default and the bounds).

## Full configuration schema

<!-- BEGIN GENERATED: schema -->
Hack id `units.id-reuse-delay`, written under the profile's `hacks` block.

| Write | Means |
| --- | --- |
| `units.id-reuse-delay: true` | On, every parameter at its default. |
| `units.id-reuse-delay: {ticks: 150}` | On, the parameters named set and the rest at their defaults. |
| `units.id-reuse-delay: {preset: baseline}` | On, starting from a preset; parameters named beside `preset` replace its values. |
| `units.id-reuse-delay: 150` | On, the shorthand: a bare value sets `ticks`. |
| `units.id-reuse-delay: false` | Off, the same as leaving it out: 3.1c behaviour. |

### Parameters

| Parameter | Type | Unit | Allowed | Length | Baseline (3.1c) | Default | Adjustable | Scope | Overridden by |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `ticks` | `int` | ticks | `0` to `30000` | - | `0` | `150` | `fixed` | `sim` | - |

Adjustable: `fixed`: only the profile sets it.

### Presets

| Preset | Values |
| --- | --- |
| `baseline` | `{ticks: 0}` |

`baseline` sets every parameter to its 3.1c value: the hack is on and plays as 3.1c.

### Shorthand

The hack has one parameter, so a bare value sets `ticks`: `units.id-reuse-delay: 150` is `units.id-reuse-delay: {ticks: 150}`.

### Every parameter at its default

```yaml
hacks:
  units.id-reuse-delay:
    ticks: 150
```
<!-- END GENERATED: schema -->
