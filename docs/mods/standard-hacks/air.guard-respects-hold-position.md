# Guard Respects Hold Position

<!-- BEGIN GENERATED: facts -->
| Fact | Value |
| --- | --- |
| Hack id | `air.guard-respects-hold-position` |
| Area | Aircraft (`air`) |
| Scope | sim: part of the profile hash; every machine must agree |
| Runs on | The machine that owns the unit or player concerned. |
| Parameters | 0 |
| Status | implemented |
<!-- END GENERATED: facts -->

## Description

An aircraft guarding another unit no longer flies off to attack whoever hit that unit while its move order is Hold Position. In 3.1c a guarding aircraft always goes after the attacker.

## Configuration example

```yaml
hacks:
  air.guard-respects-hold-position: true
```

## Details

### Behaviour

When the unit an aircraft guards is hit, the guard is alerted and may react to the last unit that hit it. With this hack on, a guard whose standing move order is Hold Position ignores the alert: it keeps following the guarded unit and does not attack. On Maneuver or Roam it reacts as in 3.1c.

The other checks that come before the move order are unchanged: an allied attacker, or an attacker type the guard may not chase, is ignored in both cases.

Only the aircraft guard order changes. Ground units guarding a unit behave as in 3.1c.

### 3.1c baseline

A guarding aircraft that is alerted orders an attack on the attacker whatever its move order is. If the attack cannot be ordered and the guard's fire order allows it, its weapons that may return fire aim at the attacker instead.

### Network games

The machine that owns the guarding aircraft applies the rule. The hack is part of the profile hash, so every machine must run with the same setting.

### Interactions

- [`air.no-repair-retreat-flag`](air.no-repair-retreat-flag.md) decides whether a damaged guard breaks off to a repair pad; it does not depend on this hack.

### Implementation notes

- Applied in the guard reaction (`defend`) in `src/sim/match-runtime/src/tick_missions_vtol.cpp` (module `src/sim/match-runtime`), which reads `MatchRules::air.guard_respects_hold_position.enabled` and treats a move order field of 0 as Hold Position.
- Tested by `air_guard_holds_position` in `src/sim/match-runtime/tests/order_rules_test.cpp` (ctest `match-order-rules`), which checks every combination of the hack and Hold Position or Maneuver.

## Full configuration schema

<!-- BEGIN GENERATED: schema -->
Hack id `air.guard-respects-hold-position`, written under the profile's `hacks` block.

| Write | Means |
| --- | --- |
| `air.guard-respects-hold-position: true` | On. |
| `air.guard-respects-hold-position: false` | Off, the same as leaving it out: 3.1c behaviour. |

### Parameters

This hack has no parameters.

### Presets

This hack has no presets.

### Shorthand

This hack has no parameters to set: write `true` or `false`.

### Every parameter at its default

```yaml
hacks:
  air.guard-respects-hold-position: true
```
<!-- END GENERATED: schema -->
