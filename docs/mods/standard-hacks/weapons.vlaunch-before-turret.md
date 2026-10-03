# Vertical Launch Before Turret

<!-- BEGIN GENERATED: facts -->
| Fact | Value |
| --- | --- |
| Hack id | `weapons.vlaunch-before-turret` |
| Area | Weapons (`weapons`) |
| Scope | sim: part of the profile hash; every machine must agree |
| Runs on | Every machine in the game. |
| Parameters | 0 |
| Status | implemented |
<!-- END GENERATED: facts -->

## Description

A weapon with both `vlaunch=1` and `turret=1` fires as a vertical launcher, straight up, instead of as a turret along its aim. It is still aimed as a turret.

## Configuration example

```yaml
hacks:
  weapons.vlaunch-before-turret: true
```

## Details

### Behaviour

A weapon's flags choose how its shots are fired. The order of the tests is:

- 3.1c: turret, then vertical launch, then line of sight or self-propelled, then dropped.
- With the hack: vertical launch first, then turret, then the rest as before.

So the hack only changes weapons that set both `vlaunch` and `turret`. Such a weapon:

- is still aimed as a turret: which aim script the slot runs does not depend on this choice;
- fires its shot with the vertical-launch rules, so the shot leaves pointing straight up instead of along the turret's aim;
- is shared with the other machines with its start at the muzzle, as for any weapon that is not a fixed line weapon.

### Baseline

3.1c fires a weapon with both flags as a turret, along its aim.

### Network games

The choice comes from the weapon's flags and the hack's state, and every machine makes it the same way for every shot. Every machine must have the hack in the same state.

### Interactions

None of the other standard hacks depend on it.

### Implementation notes

- `select_fire_mode` in `src/sim/weapon-execution/src/weapon_execution.cpp` (declared in `src/sim/weapon-execution/include/oa/sim/weapon_execution.hpp`) takes the switch from the rules record field `weapons.vlaunch_before_turret.enabled`. It is called by the shot planner in `src/sim/weapon-execution/src/weapon_launch.cpp`, the fire step in `src/sim/weapon-execution/src/weapon_execution.cpp` and the shared shot in `src/sim/match-runtime/src/tick_host_weapons.cpp`.
- 3.1c makes this choice once, when the weapon loads; the engine makes it for each shot, with the same result.
- Tested by `weapon-execution` (the fire mode of a turret-and-vlaunch weapon both ways) and `match-weapon-rules` (`vertical_launch_turret`, which checks the shot leaves straight up only with the hack on).

## Full configuration schema

<!-- BEGIN GENERATED: schema -->
Hack id `weapons.vlaunch-before-turret`, written under the profile's `hacks` block.

| Write | Means |
| --- | --- |
| `weapons.vlaunch-before-turret: true` | On. |
| `weapons.vlaunch-before-turret: false` | Off, the same as leaving it out: 3.1c behaviour. |

### Parameters

This hack has no parameters.

### Presets

This hack has no presets.

### Shorthand

This hack has no parameters to set: write `true` or `false`.

### Every parameter at its default

```yaml
hacks:
  weapons.vlaunch-before-turret: true
```
<!-- END GENERATED: schema -->
