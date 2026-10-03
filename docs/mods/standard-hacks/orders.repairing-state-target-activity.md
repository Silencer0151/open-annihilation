# Repair Finish Check

<!-- BEGIN GENERATED: facts -->
| Fact | Value |
| --- | --- |
| Hack id | `orders.repairing-state-target-activity` |
| Area | Orders (`orders`) |
| Scope | sim: part of the profile hash; every machine must agree |
| Runs on | The machine that owns the unit or player concerned. |
| Parameters | 0 |
| Status | implemented |
<!-- END GENERATED: facts -->

## Description

A unit that goes to a repair pad is repaired when the pad is switched on, whatever the unit's own on/off state. In 3.1c the check reads the unit being repaired instead of the pad, so a unit that is switched off is turned away by an active pad.

## Configuration example

```yaml
hacks:
  orders.repairing-state-target-activity: true
```

The hack has no parameters.

## Details

When a unit starts its repair at a pad, the order first checks that the pad can repair it: the pad must be a builder, fully built and active (switched on). If a check fails, the order ends. Otherwise the order occupies the repaired unit's weapons and goes on to repair it each tick until it is at full health.

- **3.1c:** the "active" test reads the repaired unit's own active bit, not the pad's. A unit that is switched off fails it at an active pad, and an active unit passes it at a pad that is switched off.
- **With the hack:** the test reads the pad's active bit. The other tests are unchanged, and the weapons it occupies are still those of the repaired unit.

**Network games.** The machine that owns the repaired unit runs its order. The hack is part of the profile hash, so every machine applies the same test.

**Related hacks.** [air.no-repair-retreat-flag](air.no-repair-retreat-flag.md) decides which aircraft break off to look for a pad at all.

### Implementation notes

- The engine applies it in `TickHost::GroundMissions::self_repair` (`src/sim/match-runtime/src/tick_missions_command.cpp`), which reads `rules.orders.repairing_state_target_activity.enabled`.
- Tests: `repair_pad_tests_the_pad` in `src/sim/match-runtime/tests/order_rules_test.cpp` (ctest `match-order-rules`) lands a unit at an active pad with and without the hack.

## Full configuration schema

<!-- BEGIN GENERATED: schema -->
Hack id `orders.repairing-state-target-activity`, written under the profile's `hacks` block.

| Write | Means |
| --- | --- |
| `orders.repairing-state-target-activity: true` | On. |
| `orders.repairing-state-target-activity: false` | Off, the same as leaving it out: 3.1c behaviour. |

### Parameters

This hack has no parameters.

### Presets

This hack has no presets.

### Shorthand

This hack has no parameters to set: write `true` or `false`.

### Every parameter at its default

```yaml
hacks:
  orders.repairing-state-target-activity: true
```
<!-- END GENERATED: schema -->
