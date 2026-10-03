# Timed Shell Detonation

<!-- BEGIN GENERATED: facts -->
| Fact | Value |
| --- | --- |
| Hack id | `weapons.timed-shell-detonation` |
| Area | Weapons (`weapons`) |
| Scope | sim: part of the profile hash; every machine must agree |
| Runs on | Every machine in the game, and the machine that owns the unit or player concerned. |
| Parameters | 1 |
| Status | implemented |
<!-- END GENERATED: facts -->

## Description

A ballistic shell whose `weapontimer` runs out explodes, unless its weapon has `noautorange=1`. In 3.1c it explodes only when the weapon has `burnblow=1` and otherwise goes out in a small puff without damage.

## Configuration example

```yaml
hacks:
  weapons.timed-shell-detonation: true
```

```yaml
hacks:
  # The shorthand sets `rule`; `burnblow` keeps 3.1c's rule while the hack is listed.
  weapons.timed-shell-detonation: not-noautorange
```

## Details

### Behaviour

A ballistic projectile of a weapon with a nonzero `weapontimer` has a lifetime. When it runs out in flight, `rule` decides what happens:

- `burnblow` (3.1c): the shell explodes, with its weapon's damage and effects, only when the weapon has `burnblow=1`.
- `not-noautorange`: the shell explodes unless the weapon has `noautorange=1`.

A shell that does not explode goes out in a light puff and is removed without dealing damage. Either way it stops colliding at that moment.

Only ballistic flight is affected; other flight modes treat their timers as before.

### Baseline

3.1c explodes a timed-out shell only with `burnblow=1`.

### Network games

Every machine runs the shared projectile and decides the same way whether it explodes or puffs out; the damage of the explosion is dealt on the shooter's machine, as for any other hit. Every machine must use the same rule.

### Interactions

- [`weapons.high-arc-ballistic`](weapons.high-arc-ballistic.md): lobbed shells stay in the air longer and run out of time more often.

### Implementation notes

- `timed_shell_explodes` in `src/sim/match-runtime/src/tick_projectiles.cpp` applies the rule from the rules record `weapons.timed_shell_detonation` (`enabled` and `rule`) in the ballistic case of the projectile tick.
- Tested by `match-weapon-rules` (`timed_shells_run_out`, with the hack off, on with each rule).

## Full configuration schema

<!-- BEGIN GENERATED: schema -->
Hack id `weapons.timed-shell-detonation`, written under the profile's `hacks` block.

| Write | Means |
| --- | --- |
| `weapons.timed-shell-detonation: true` | On, every parameter at its default. |
| `weapons.timed-shell-detonation: {rule: not-noautorange}` | On, the parameters named set and the rest at their defaults. |
| `weapons.timed-shell-detonation: {preset: baseline}` | On, starting from a preset; parameters named beside `preset` replace its values. |
| `weapons.timed-shell-detonation: not-noautorange` | On, the shorthand: a bare value sets `rule`. |
| `weapons.timed-shell-detonation: false` | Off, the same as leaving it out: 3.1c behaviour. |

### Parameters

| Parameter | Type | Unit | Allowed | Length | Baseline (3.1c) | Default | Adjustable | Scope | Overridden by |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `rule` | `enum` | - | `burnblow`, `not-noautorange` | - | `burnblow` | `not-noautorange` | `fixed` | `sim` | - |

Adjustable: `fixed`: only the profile sets it.

### Presets

| Preset | Values |
| --- | --- |
| `baseline` | `{rule: burnblow}` |

`baseline` sets every parameter to its 3.1c value: the hack is on and plays as 3.1c.

### Shorthand

The hack has one parameter, so a bare value sets `rule`: `weapons.timed-shell-detonation: not-noautorange` is `weapons.timed-shell-detonation: {rule: not-noautorange}`.

### Every parameter at its default

```yaml
hacks:
  weapons.timed-shell-detonation:
    rule: not-noautorange
```
<!-- END GENERATED: schema -->
