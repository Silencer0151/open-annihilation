# Self-Repair by Heal Time

<!-- BEGIN GENERATED: facts -->
| Fact | Value |
| --- | --- |
| Hack id | `repair.healtime-self-heal` |
| Area | Repair (`repair`) |
| Scope | sim: part of the profile hash; every machine must agree |
| Runs on | The machine that owns the unit or player concerned. |
| Parameters | 3 |
| Status | implemented |
<!-- END GENERATED: facts -->

## Description

Units whose type has a `HealTime` regenerate at a pace set by that `HealTime`
instead of every 8 ticks, and with more work per step. Units still under
construction no longer regenerate.

## Configuration example

Switch it on with every parameter at its default:

```yaml
hacks:
  repair.healtime-self-heal: true
```

Keep 3.1c's pace but give each regeneration step twice its work:

```yaml
hacks:
  repair.healtime-self-heal:
    cadence: every-8-ticks   # regenerate every 8 ticks, as in 3.1c
    work-multiplier: 2       # each step has twice 3.1c's work
```

## Details

### Behaviour

On each tick, a unit simulated on this machine regenerates when its type's
`HealTime` is not 0 and its health is below `MaxDamage` (health is
sign-extended, then compared without sign). The parameters then decide:

- `skip-under-construction`: when true, a unit whose build fraction is not
  exactly +0.0 does not regenerate. A fraction of −0.0 counts as unfinished.
  3.1c lets unfinished units regenerate too.
- `cadence`:
  - `every-8-ticks` (3.1c): the unit regenerates when `tick & 7` is 0.
  - `healtime-mask`: the unit regenerates when `tick & (HealTime & 255)` is 0.
    `HealTime` 1 regenerates every 2 ticks, 3 every 4, 7 every 8 and 15 every
    16. Other values follow the same bit test: 5 regenerates on ticks whose
    bits 0 and 2 are clear, which is uneven, and a `HealTime` whose low eight
    bits are 0 (such as 256) regenerates on every tick.
- `work-multiplier`: the work of one step is
  `trunc(HealTime × 8 × work-multiplier / 30)`. Under `every-8-ticks`
  `HealTime` is read without sign; under `healtime-mask` it is read with its
  sign and the quotient is truncated toward zero, so a negative `HealTime`
  gives no heal. A multiplier of 4 makes the work `HealTime × 8 / 7.5`.

The step is the repair step with the unit as both builder and target: the
unit's owner pays its energy, and [repair.rate](repair.rate.md) decides the
heal. Under 3.1c's repair rate a step heals at most 1 hit point whatever its
work, so the multiplier only shows when `repair.rate` is on.

### Baseline

3.1c regenerates every 8 ticks with work `HealTime × 8 / 30`, `HealTime` read
without sign, including units under construction.

### Network games

The machine that simulates the unit regenerates it; other machines see its
health through the usual unit updates. Every parameter is part of the profile
hash, so every machine plays the same rule.

### Implementation notes

- `self_heal_due` and `self_heal_rate` in
  `src/sim/simulation-state/src/simulation.cpp` (declared in
  `src/sim/simulation-state/include/oa/sim/simulation_state.hpp`) decide the
  tick and the work; `update_unit` calls them with
  `rules().repair.healtime_self_heal` (the `RepairHealtimeSelfHeal` record).
- `TickHost::regenerate_health` in
  `src/sim/match-runtime/src/tick_host_health.cpp` runs the repair step.
- Tests: `simulation-state-self-heal`
  (`src/sim/simulation-state/tests/self_heal_test.cpp`) checks the baseline,
  the defaults and each parameter alone; `match-veterancy-repair` checks
  regeneration in a match.

## Full configuration schema

<!-- BEGIN GENERATED: schema -->
Hack id `repair.healtime-self-heal`, written under the profile's `hacks` block.

| Write | Means |
| --- | --- |
| `repair.healtime-self-heal: true` | On, every parameter at its default. |
| `repair.healtime-self-heal: {skip-under-construction: true}` | On, the parameters named set and the rest at their defaults. |
| `repair.healtime-self-heal: {preset: baseline}` | On, starting from a preset; parameters named beside `preset` replace its values. |
| `repair.healtime-self-heal: false` | Off, the same as leaving it out: 3.1c behaviour. |

### Parameters

| Parameter | Type | Unit | Allowed | Length | Baseline (3.1c) | Default | Adjustable | Scope | Overridden by |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `skip-under-construction` | `bool` | - | `true`, `false` | - | `false` | `true` | `fixed` | `sim` | - |
| `cadence` | `enum` | - | `every-8-ticks`, `healtime-mask` | - | `every-8-ticks` | `healtime-mask` | `fixed` | `sim` | - |
| `work-multiplier` | `int` | x work | `1` to `64` | - | `1` | `4` | `fixed` | `sim` | - |

Adjustable: `fixed`: only the profile sets it.

### Presets

| Preset | Values |
| --- | --- |
| `baseline` | `{skip-under-construction: false, cadence: every-8-ticks, work-multiplier: 1}` |

`baseline` sets every parameter to its 3.1c value: the hack is on and plays as 3.1c.

### Shorthand

This hack takes no shorthand: write `true`, `false` or a parameter map.

### Every parameter at its default

```yaml
hacks:
  repair.healtime-self-heal:
    skip-under-construction: true
    cadence: healtime-mask
    work-multiplier: 4
```
<!-- END GENERATED: schema -->
