# Reclaim Cursor on Any Unit

<!-- BEGIN GENERATED: facts -->
| Fact | Value |
| --- | --- |
| Hack id | `orders.reclaim-command-any-unit` |
| Area | Orders (`orders`) |
| Scope | sim: part of the profile hash; every machine must agree |
| Runs on | The machine that owns the unit or player concerned. |
| Parameters | 0 |
| Status | implemented |
<!-- END GENERATED: facts -->

## Description

With the Reclaim command armed, the pointer shows the reclaim cursor over every unit. In 3.1c the cursor changes back to normal over a unit that cannot be reclaimed, such as a commander, so hovering tells the player which units those are.

## Configuration example

```yaml
hacks:
  orders.reclaim-command-any-unit: true
```

The hack has no parameters.

## Details

Only the cursor changes; the orders do not.

- **3.1c:** with the Reclaim command armed, the cursor over a unit is the reclaim cursor when the selected unit can reclaim that unit, and the normal cursor otherwise. Commanders and other units that cannot be reclaimed show the normal cursor.
- **With the hack:** the cursor over any unit is the reclaim cursor, for any selected unit, even one that cannot reclaim. Clicking issues the command as 3.1c does: only units that can reclaim get a reclaim order, and when that order runs it still refuses a target that cannot be reclaimed. The reclaim cursor over a reclaimable feature is unchanged.

The effect is that hovering no longer reveals which units refuse a reclaim.

**Network games.** It changes only what the player who issues the command sees, and the order travels as in 3.1c. The hack is part of the profile hash, so every machine shares the profile's choice.

**Related hacks.** [orders.resurrector-reclaims-features](orders.resurrector-reclaims-features.md) changes the order the Reclaim command gives over a feature.

### Implementation notes

- The engine applies it in `order_cursor` (`src/sim/gameplay-input/src/order_cursor.cpp`, declared in `src/sim/gameplay-input/include/oa/sim/gameplay_input/order_cursor.hpp`), which reads `rules.orders.reclaim_command_any_unit.enabled`. The order the click resolves to is unchanged.
- Tests: `test_reclaim_command_any_unit` in `src/sim/gameplay-input/tests/order_cursor_test.cpp` (ctest `gameplay-input-order-cursor`) checks the cursor and the issued order with and without the hack.

## Full configuration schema

<!-- BEGIN GENERATED: schema -->
Hack id `orders.reclaim-command-any-unit`, written under the profile's `hacks` block.

| Write | Means |
| --- | --- |
| `orders.reclaim-command-any-unit: true` | On. |
| `orders.reclaim-command-any-unit: false` | Off, the same as leaving it out: 3.1c behaviour. |

### Parameters

This hack has no parameters.

### Presets

This hack has no presets.

### Shorthand

This hack has no parameters to set: write `true` or `false`.

### Every parameter at its default

```yaml
hacks:
  orders.reclaim-command-any-unit: true
```
<!-- END GENERATED: schema -->
