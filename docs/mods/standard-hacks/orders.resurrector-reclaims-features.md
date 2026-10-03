# Resurrectors Reclaim Wrecks

<!-- BEGIN GENERATED: facts -->
| Fact | Value |
| --- | --- |
| Hack id | `orders.resurrector-reclaims-features` |
| Area | Orders (`orders`) |
| Scope | sim: part of the profile hash; every machine must agree |
| Runs on | The machine that owns the unit or player concerned. |
| Parameters | 0 |
| Status | implemented |
<!-- END GENERATED: facts -->

## Description

A unit that can resurrect reclaims a wreck when the player uses the Reclaim command on it (its button or the E key). In 3.1c the Reclaim command resurrects such a wreck instead. The default order, a right-click, still resurrects.

## Configuration example

```yaml
hacks:
  orders.resurrector-reclaims-features: true
```

The hack has no parameters.

## Details

It applies when the selected unit can both reclaim and resurrect, and the Reclaim command is used on a position that holds a feature.

- **3.1c:** the Reclaim command gives a resurrect order, so a resurrector cannot turn a wreck into resources with it.
- **With the hack:** the Reclaim command gives a reclaim order (the aircraft form for an aircraft), as it does for a builder that cannot resurrect.

Unchanged: the default order (right-click) over a resurrectable feature still resurrects it, the Reclaim command over a unit still reclaims the unit, and units that cannot resurrect behave as in 3.1c.

**Network games.** The issuing player's machine resolves the command into an order for its own units. The hack is part of the profile hash, so every machine shares the profile's choice.

**Related hacks.** [orders.reclaim-command-any-unit](orders.reclaim-command-any-unit.md) changes the cursor the Reclaim command shows over units.

### Implementation notes

- The engine applies it in the function that resolves a command into an order, in `src/sim/gameplay-input/src/order_cursor.cpp` (declared in `src/sim/gameplay-input/include/oa/sim/gameplay_input/order_cursor.hpp`). It reads `rules.orders.resurrector_reclaims_features.enabled`.
- Tests: `test_resurrector_reclaims_features` in `src/sim/gameplay-input/tests/order_cursor_test.cpp` (ctest `gameplay-input-order-cursor`) checks the Reclaim command and the default order over a feature with and without the hack.

## Full configuration schema

<!-- BEGIN GENERATED: schema -->
Hack id `orders.resurrector-reclaims-features`, written under the profile's `hacks` block.

| Write | Means |
| --- | --- |
| `orders.resurrector-reclaims-features: true` | On. |
| `orders.resurrector-reclaims-features: false` | Off, the same as leaving it out: 3.1c behaviour. |

### Parameters

This hack has no parameters.

### Presets

This hack has no presets.

### Shorthand

This hack has no parameters to set: write `true` or `false`.

### Every parameter at its default

```yaml
hacks:
  orders.resurrector-reclaims-features: true
```
<!-- END GENERATED: schema -->
