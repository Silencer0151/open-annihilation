# Structure Gift Rate Limit

<!-- BEGIN GENERATED: facts -->
| Fact | Value |
| --- | --- |
| Hack id | `sharing.structure-gift-rate-limit` |
| Area | Sharing (`sharing`) |
| Scope | sim: part of the profile hash; every machine must agree |
| Runs on | The machine that owns the unit or player concerned. |
| Parameters | 3 |
| Status | implemented |
<!-- END GENERATED: facts -->

## Description

Structures given away through the share panel go across at once only while few structures went
across lately; otherwise the whole batch waits a fixed time and the giver is told so. Mobile units
still go across at once. In 3.1c every gift goes across immediately.

## Configuration example

```yaml
hacks:
  sharing.structure-gift-rate-limit: true
```

A stricter limit with a shorter wait:

```yaml
hacks:
  sharing.structure-gift-rate-limit:
    immediate-max: 5     # at most 5 structures within the window go across at once
    defer-ticks: 450     # a held-back batch waits 15 seconds at normal speed
```

## Details

### Behaviour

One use of the share panel's give button is one gift. While the gift is collected:

- A unit whose type is a structure (`BMCODE` 0) joins the gift's batch of structures.
- Every other unit goes across at once, as in 3.1c.

When the gift ends, the structures given within the last `window-ticks` ticks are counted. When that
count plus the batch's structures is no more than `immediate-max`, the batch goes across at once.
Otherwise the whole batch waits `defer-ticks` ticks, and the giver sees the message "Sharing *n*
structures - transfer will complete in *s* seconds.", *s* being `defer-ticks` in seconds at normal
speed. The window is not counted again when a waiting batch falls due.

When a batch goes across, each structure is checked again, and goes only while it is still the same
live unit of the same type and owner, is not dying, and the recipient's slot is still in use.
Structures that go across count towards the window. Waiting batches are handed over in the order
they fall due, at the end of the game tick, so the timing does not depend on the frame rate. A
rotated structure keeps its facing.

Commanders stay with their player, as in 3.1c.

### In a network game

The giving machine holds the batch and decides when it goes; the other machines see ordinary unit
transfers when it does. The hack and its parameters are part of the profile hash, and the window and
the waiting batches are part of the match state that is hashed and saved, so every machine must
agree on all three parameters.

### Interactions

- [units.build-rotation](units.build-rotation.md): a rotated structure keeps its facing when it is
  given.

### Implementation notes

- `src/sim/match-runtime/src/structure_gifts.cpp` holds the rule: `Match::begin_share_gift`,
  `share_gift_unit`, `end_share_gift` and `give_due_structure_gifts`, which the tick runs last
  (`src/sim/match-runtime/src/tick.cpp`). The state and its encoding are in
  `src/sim/match-runtime/include/oa/sim/match_runtime/structure_gifts.hpp`; a match keeps it only
  while the rule is on, as the rule-state table `structure-gifts`.
- The share panel's give action, `Runtime::give_selected_units_to` in
  `src/app/runtime_team_panels.cpp`, collects the gift and posts the message.
- It reads `MatchRules::sharing.structure_gift_rate_limit` (`window_ticks`, `immediate_max`,
  `defer_ticks`).
- Tests: `match-structure-gifts` (`window_and_batches`, `without_the_rule`, `structures_wait`,
  `each_parameter`, `saved_and_restored`).
- Limits: at most 64 batches and 2048 structures wait at once, and the window remembers 256 gifts,
  merging the oldest when it is full.

## Full configuration schema

<!-- BEGIN GENERATED: schema -->
Hack id `sharing.structure-gift-rate-limit`, written under the profile's `hacks` block.

| Write | Means |
| --- | --- |
| `sharing.structure-gift-rate-limit: true` | On, every parameter at its default. |
| `sharing.structure-gift-rate-limit: {window-ticks: 900}` | On, the parameters named set and the rest at their defaults. |
| `sharing.structure-gift-rate-limit: false` | Off, the same as leaving it out: 3.1c behaviour. |

### Parameters

| Parameter | Type | Unit | Allowed | Length | Baseline (3.1c) | Default | Adjustable | Scope | Overridden by |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `window-ticks` | `int` | ticks | `1` to `30000` | - | `900` | `900` | `fixed` | `sim` | - |
| `immediate-max` | `int` | structures | `0` to `1500` | - | `10` | `10` | `fixed` | `sim` | - |
| `defer-ticks` | `int` | ticks | `1` to `30000` | - | `900` | `900` | `fixed` | `sim` | - |

Adjustable: `fixed`: only the profile sets it.

### Presets

This hack has no presets.

### Shorthand

This hack takes no shorthand: write `true`, `false` or a parameter map.

### Every parameter at its default

```yaml
hacks:
  sharing.structure-gift-rate-limit:
    window-ticks: 900
    immediate-max: 10
    defer-ticks: 900
```
<!-- END GENERATED: schema -->
