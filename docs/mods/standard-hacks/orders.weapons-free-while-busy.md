# Fire While Building

<!-- BEGIN GENERATED: facts -->
| Fact | Value |
| --- | --- |
| Hack id | `orders.weapons-free-while-busy` |
| Area | Orders (`orders`) |
| Scope | sim: part of the profile hash; every machine must agree |
| Runs on | The machine that owns the unit or player concerned. |
| Parameters | 1 |
| Status | implemented |
<!-- END GENERATED: facts -->

## Description

A builder's weapons keep choosing their own targets while it builds or does other busy work, where 3.1c holds them idle until the work ends. The profile names which kinds of work leave the weapons free.

## Configuration example

```yaml
hacks:
  orders.weapons-free-while-busy: true   # free while building (nanolathe) only
```

Free the weapons in every busy state, using the shorthand that sets `states`:

```yaml
hacks:
  orders.weapons-free-while-busy: [nanolathe, help-build, capture, reclaim-unit, repair]
```

## Details

When a builder enters a busy state, 3.1c **occupies** all its weapon slots: they fire only at what an order gives them and drop any target they had. For each state listed in `states`, the hack **frees** the slots instead, so an armed builder fights back while it works.

| State | The builder is |
| --- | --- |
| `nanolathe` | building a structure or unit it was ordered to build |
| `help-build` | helping another builder with its construction |
| `capture` | capturing an enemy unit |
| `reclaim-unit` | reclaiming a unit |
| `repair` | repairing a unit |

States the hack does not cover stay as in 3.1c whatever the list says: reclaiming a feature, resurrecting, being built, being transported and being paralysed.

**Baseline.** The `baseline` preset, an empty list, occupies the weapons in every busy state, as 3.1c does.

**Network games.** The machine that owns the builder runs its orders. The list is part of the profile hash, so every machine plays by the same states.

**Related hacks.** [orders.selective-weapon-occupy](orders.selective-weapon-occupy.md) makes the same occupy-or-free choice for Attack orders.

### Implementation notes

- The engine applies it in `GroundMissions::occupy_weapons_while_busy` (`src/sim/match-runtime/src/ground_missions.hpp`), which reads `rules.orders.weapons_free_while_busy.states`. The build, help-build, capture, unit-reclaim and repair missions in `src/sim/match-runtime/src/tick_missions_build.cpp` call it as they enter their busy state.
- Tests: `busy_states_free_the_weapons` in `src/sim/match-runtime/tests/order_rules_test.cpp` (ctest `match-order-rules`) checks each state with and without the hack. `src/data/mod-profile/tests/mod_profile_test.cpp` (ctest `data-mod-profile`) checks the shorthand, the preset and a repeated state.

## Full configuration schema

<!-- BEGIN GENERATED: schema -->
Hack id `orders.weapons-free-while-busy`, written under the profile's `hacks` block.

| Write | Means |
| --- | --- |
| `orders.weapons-free-while-busy: true` | On, every parameter at its default. |
| `orders.weapons-free-while-busy: {states: [nanolathe]}` | On, the parameters named set and the rest at their defaults. |
| `orders.weapons-free-while-busy: {preset: baseline}` | On, starting from a preset; parameters named beside `preset` replace its values. |
| `orders.weapons-free-while-busy: [nanolathe]` | On, the shorthand: a bare value sets `states`. |
| `orders.weapons-free-while-busy: false` | Off, the same as leaving it out: 3.1c behaviour. |

### Parameters

| Parameter | Type | Unit | Allowed | Length | Baseline (3.1c) | Default | Adjustable | Scope | Overridden by |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `states` | `set<enum>` | - | `nanolathe`, `help-build`, `capture`, `reclaim-unit`, `repair`; each at most once | any | `[]` | `[nanolathe]` | `fixed` | `sim` | - |

Adjustable: `fixed`: only the profile sets it.

### Presets

| Preset | Values |
| --- | --- |
| `baseline` | `{states: []}` |

`baseline` sets every parameter to its 3.1c value: the hack is on and plays as 3.1c.

### Shorthand

The hack has one parameter, so a bare value sets `states`: `orders.weapons-free-while-busy: [nanolathe]` is `orders.weapons-free-while-busy: {states: [nanolathe]}`.

### Every parameter at its default

```yaml
hacks:
  orders.weapons-free-while-busy:
    states: [nanolathe]
```
<!-- END GENERATED: schema -->
