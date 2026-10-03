# Ignore Unit 0 Deaths

<!-- BEGIN GENERATED: facts -->
| Fact | Value |
| --- | --- |
| Hack id | `units.ignore-null-death-record` |
| Area | Units (`units`) |
| Scope | sim: part of the profile hash; every machine must agree |
| Runs on | Every machine in the game. |
| Parameters | 0 |
| Status | implemented |
<!-- END GENERATED: facts -->

## Description

A unit-death record that names unit index 0 is ignored entirely, so no unit dies and nothing else changes. 3.1c runs its whole death processing for such a record.

## Configuration example

```yaml
hacks:
  units.ignore-null-death-record: true
```

The hack takes no parameters.

## Details

**Behaviour.** A unit-death record (`0x0c`) received from another machine names the dying unit by its index. When that index is 0, the record is dropped before any death processing. No unit dies and no wreck or explosion is made. The record does not count as an error.

**3.1c behaviour.** The record for unit 0 goes through the whole death processing.

**Network play.** Every machine that receives the record applies the hack. The hack is part of the network hash.

### Implementation notes

- The profile record is `rules().units.ignore_null_death_record.enabled`.
- `apply_kill` in `src/netgame/match/src/match_binding.cpp` handles the record. Its `slot_of` lookup gives no unit for index 0, so the record is dropped.
- Tests: `net-match-runtime` (`death_record_for_unit_zero_changes_nothing`) checks that the receiver's units are unchanged and that the record is not an error.
- **Known limit:** in the engine, unit index 0 names no unit, so such a record is ignored whether the hack is on or off. The engine does not reproduce 3.1c's processing of a death record for unit 0. Nothing reads the rules field, and turning the hack on or off changes nothing.

## Full configuration schema

<!-- BEGIN GENERATED: schema -->
Hack id `units.ignore-null-death-record`, written under the profile's `hacks` block.

| Write | Means |
| --- | --- |
| `units.ignore-null-death-record: true` | On. |
| `units.ignore-null-death-record: false` | Off, the same as leaving it out: 3.1c behaviour. |

### Parameters

This hack has no parameters.

### Presets

This hack has no presets.

### Shorthand

This hack has no parameters to set: write `true` or `false`.

### Every parameter at its default

```yaml
hacks:
  units.ignore-null-death-record: true
```
<!-- END GENERATED: schema -->
