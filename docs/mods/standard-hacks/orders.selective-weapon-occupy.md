# Selective Weapon Use

<!-- BEGIN GENERATED: facts -->
| Fact | Value |
| --- | --- |
| Hack id | `orders.selective-weapon-occupy` |
| Area | Orders (`orders`) |
| Scope | sim: part of the profile hash; every machine must agree |
| Runs on | The machine that owns the unit or player concerned. |
| Parameters | 2 |
| Status | implemented |
<!-- END GENERATED: facts -->

## Description

An Attack order holds only the weapon it was given, and a D-gun fired at the ground holds only the third weapon. The unit's other weapons keep choosing their own targets, where 3.1c makes them wait while the order runs.

## Configuration example

```yaml
hacks:
  orders.selective-weapon-occupy: true
```

Hold only the ordered weapon at the first attack stage, and keep 3.1c's behaviour at the second:

```yaml
hacks:
  orders.selective-weapon-occupy:
    attack: ordered-slot-first-stage   # the second stage occupies slots 0 and 2, as in 3.1c
```

## Details

A unit's weapon slot is either **occupied**, firing only at what an order gives it, or **free**, picking its own targets. Occupying a slot clears its target. This hack changes which slots two orders occupy.

**Attack (`attack`).** An Attack order on a unit aims the ordered weapon at two stages: the first when the attacker first has the target within reach, the second each time it has the target within reach again as it keeps attacking. Before aiming at either stage, 3.1c occupies slots 0 and 2, whichever slot the order names.

- `slots-0-and-2`: both stages occupy slots 0 and 2, as in 3.1c.
- `ordered-slot-first-stage`: the first stage occupies only the ordered slot; the second stage occupies slots 0 and 2.
- `ordered-slot-both-stages`: both stages occupy only the ordered slot.

The ordered slot is read as slot 2 when the order names a slot past 1, and slot 0 otherwise. Slot 1 is therefore never occupied: an order for slot 1 occupies slot 0, and slot 1 then aims at the target without being held. The engine reproduces this.

**D-gun on the ground (`suppress`).** An Attack order on a map position that fires the third weapon (a commander's D-gun) occupies every slot in 3.1c (`all`). With `slot-2` it occupies the third slot alone, so the other weapons go on firing at whatever they choose. An attack-ground order for any other weapon is unchanged: it occupies slots 0 and 1 and aims both at the position, as in 3.1c.

**Baseline.** Without the hack, or with the `baseline` preset, the unit's other weapons stop picking targets for as long as an Attack or D-gun-ground order runs.

**Network games.** The machine that owns the unit runs its orders and its weapons, and the other machines see the result through the unit's state. Both parameters are part of the profile hash, so every machine plays by the same values.

**Related hacks.** [orders.weapons-free-while-busy](orders.weapons-free-while-busy.md) changes the same occupy-or-free choice for a builder's busy states.

### Implementation notes

- The engine applies it in the match runtime's ground missions, `src/sim/match-runtime/src/tick_missions_attack.cpp`: the attack mission's `engage` step reads `rules.orders.selective_weapon_occupy.attack`, and the attack-ground mission reads `rules.orders.selective_weapon_occupy.suppress`.
- Tests: `attack_occupies_the_ordered_slot` and `dgun_ground_occupies_the_third_slot` in `src/sim/match-runtime/tests/order_rules_test.cpp` (ctest `match-order-rules`) compare each value against 3.1c's slots.

## Full configuration schema

<!-- BEGIN GENERATED: schema -->
Hack id `orders.selective-weapon-occupy`, written under the profile's `hacks` block.

| Write | Means |
| --- | --- |
| `orders.selective-weapon-occupy: true` | On, every parameter at its default. |
| `orders.selective-weapon-occupy: {attack: ordered-slot-both-stages}` | On, the parameters named set and the rest at their defaults. |
| `orders.selective-weapon-occupy: {preset: baseline}` | On, starting from a preset; parameters named beside `preset` replace its values. |
| `orders.selective-weapon-occupy: false` | Off, the same as leaving it out: 3.1c behaviour. |

### Parameters

| Parameter | Type | Unit | Allowed | Length | Baseline (3.1c) | Default | Adjustable | Scope | Overridden by |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `attack` | `enum` | - | `slots-0-and-2`, `ordered-slot-first-stage`, `ordered-slot-both-stages` | - | `slots-0-and-2` | `ordered-slot-both-stages` | `fixed` | `sim` | - |
| `suppress` | `enum` | - | `all`, `slot-2` | - | `all` | `slot-2` | `fixed` | `sim` | - |

Adjustable: `fixed`: only the profile sets it.

### Presets

| Preset | Values |
| --- | --- |
| `baseline` | `{attack: slots-0-and-2, suppress: all}` |

`baseline` sets every parameter to its 3.1c value: the hack is on and plays as 3.1c.

### Shorthand

This hack takes no shorthand: write `true`, `false` or a parameter map.

### Every parameter at its default

```yaml
hacks:
  orders.selective-weapon-occupy:
    attack: ordered-slot-both-stages
    suppress: slot-2
```
<!-- END GENERATED: schema -->
