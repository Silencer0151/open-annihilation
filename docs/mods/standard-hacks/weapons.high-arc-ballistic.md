# High-Arc Ballistics

<!-- BEGIN GENERATED: facts -->
| Fact | Value |
| --- | --- |
| Hack id | `weapons.high-arc-ballistic` |
| Area | Weapons (`weapons`) |
| Scope | sim: part of the profile hash; every machine must agree |
| Runs on | The machine that owns the unit or player concerned. |
| Parameters | 0 |
| Status | implemented |
<!-- END GENERATED: facts -->

## Description

A ballistic weapon that cannot reach its target on a flat arc lobs its shot on a high arc (above 45°) instead. In 3.1c such a target is out of reach and the weapon does not fire.

## Configuration example

```yaml
hacks:
  weapons.high-arc-ballistic: true
```

## Details

### Behaviour

A ballistic shot to a point has two launch angles: a flat one and a high one. The solver takes:

1. the flat angle, when it is above the weapon's minimum barrel angle and at most 45°;
2. otherwise the high angle, under the bound this hack sets:
   - off (3.1c): when it is above the minimum barrel angle and at most 45°;
   - on: when it is above the minimum barrel angle and above 45°;
3. otherwise no angle: the target cannot be reached.

The high angle is almost never at or below 45°, so in 3.1c a ballistic weapon effectively never lobs: a target too close for the flat arc, or below the minimum barrel angle, is out of reach. With the hack such a target is reached by a lob.

The same solver answers the reach tests, so the hack changes which targets a ballistic weapon picks and returns fire at, as well as how it aims and fires.

### Baseline

3.1c accepts the high angle only when it is also at most 45°.

### Network games

Aiming and firing happen on the machine that owns the shooter; the shot it fires is then shared with the others. The hack is part of the profile hash, so every machine must have it in the same state.

### Interactions

- [`weapons.timed-shell-detonation`](weapons.timed-shell-detonation.md): a lobbed shell flies longer, so a weapon with `weapontimer` is more likely to run out of time in flight.
- [`weapons.retarget-out-of-range`](weapons.retarget-out-of-range.md) uses the same reach test to decide whether a target is out of range.

### Implementation notes

- `launch_pitch` in `src/sim/ballistics/src/ballistic.cpp` takes the high angle under `BallisticParameters::accept_high_arc` (`src/sim/ballistics/include/oa/sim/ballistics.hpp`). The ballistics module does not read the rules itself.
- `ballistic_parameters` in `src/sim/weapon-execution/include/oa/sim/weapon_execution/weapon_keys.hpp` fills `accept_high_arc` from the rules record field `weapons.high_arc_ballistic.enabled`; the targeting, aiming and launch paths (`src/sim/match-runtime/src/targeting.cpp`, `src/sim/match-runtime/src/tick_host_weapons.cpp`) build their parameters through it.
- Tested by `combat-state-ballistic` (`high_arc_matches`, pitch samples with and without the switch) and `weapon-retaliation` (`reach_under_rules`, which checks that the rule reaches the solver's inputs).

## Full configuration schema

<!-- BEGIN GENERATED: schema -->
Hack id `weapons.high-arc-ballistic`, written under the profile's `hacks` block.

| Write | Means |
| --- | --- |
| `weapons.high-arc-ballistic: true` | On. |
| `weapons.high-arc-ballistic: false` | Off, the same as leaving it out: 3.1c behaviour. |

### Parameters

This hack has no parameters.

### Presets

This hack has no presets.

### Shorthand

This hack has no parameters to set: write `true` or `false`.

### Every parameter at its default

```yaml
hacks:
  weapons.high-arc-ballistic: true
```
<!-- END GENERATED: schema -->
