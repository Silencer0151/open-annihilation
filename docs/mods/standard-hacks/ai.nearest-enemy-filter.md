# Ignore Submerged Targets

<!-- BEGIN GENERATED: facts -->
| Fact | Value |
| --- | --- |
| Hack id | `ai.nearest-enemy-filter` |
| Area | AI (`ai`) |
| Scope | sim: part of the profile hash; every machine must agree |
| Runs on | The machine that owns the unit or player concerned. |
| Parameters | 1 |
| Status | implemented |
<!-- END GENERATED: facts -->

## Description

When the computer player looks for the nearest enemy to attack, its land groups ignore fully submerged units, and every search also passes over a wider set of units. Its naval group still finds submerged units.

## Configuration example

```yaml
hacks:
  ai.nearest-enemy-filter: true   # rules: skip-submerged
```

## Details

### Behaviour

The computer player searches for the nearest enemy unit in two places: an attacking land or naval group picks its target nearest the group's centre (see [ai.attack-wave-size](ai.attack-wave-size.md)), and a small aircraft wing without a base patrols toward the nearest enemy (see [ai.patrol-group-size](ai.patrol-group-size.md)).

In 3.1c the search covers every live unit of every player the computer player is not allied with. It passes over cloaked units, units flagged as not selectable, and units whose occupancy bits hold state 2. Distance is ranked by the high halves of the squared X and Z distances, added; height is ignored. The first unit with a strictly smaller rank wins.

Under `rules: skip-submerged`:

- **Wider skip:** the search passes over a unit whose occupancy bits hold state 2 or 3, or whose move-rate bits hold tier 2 or 3, or which is flagged as not selectable. The engine reproduces this rule as it stands, so a unit in the upper move-rate tiers is never chosen.
- **Submerged units:** every search except the naval group's passes over units that are fully submerged, that is units whose water state is 3. The naval group still finds them. The aircraft task counts as a non-naval search, so a wing without a base does not patrol toward a submerged unit either.

The water state is the one the unit's script last received. [units.water-state-rules](units.water-state-rules.md) is what puts fully submerged units in state 3, so a profile that uses this filter normally switches that hack on too.

### Baseline (3.1c)

The search described above, without the extra skips. The `baseline` preset (`rules: base`) plays exactly as 3.1c.

### Network play

In a network game the machine that hosts the computer player runs its AI and applies the hack; the orders the AI gives reach the other machines as any computer player's orders do. Every machine must still load the same setting: the hack is part of the profile hash the machines compare.

### Interactions

- [units.water-state-rules](units.water-state-rules.md) sets the water state the submerged test reads.
- [ai.attack-wave-size](ai.attack-wave-size.md) and [ai.patrol-group-size](ai.patrol-group-size.md) decide when the searches run.

### Implementation notes

- The rules record is `AiNearestEnemyFilter` (`rules.ai.nearest_enemy_filter.rules`) in `src/data/match-rules/include/oa/data/match_rules/records.inc`.
- `nearest_enemy` in `src/sim/ai/src/computer_player.cpp` builds a `CandidateFilter`: `widened_flags` for the wider skip and `skip_submerged` for every task whose source squad is not the navy.
- `nearest_candidate_unit` in `src/sim/simulation-state/src/simulation.cpp` (declared in `src/sim/simulation-state/include/oa/sim/simulation_state.hpp`) applies the filter; the water state is `Unit.last_occupy_code`.
- Tests: ctest `ai-computer-player` (`test_nearest_enemy_filter`), `simulation-state` (the candidate filter) and `match-computer-build` (`computer_rules_through_the_match`).

## Full configuration schema

<!-- BEGIN GENERATED: schema -->
Hack id `ai.nearest-enemy-filter`, written under the profile's `hacks` block.

| Write | Means |
| --- | --- |
| `ai.nearest-enemy-filter: true` | On, every parameter at its default. |
| `ai.nearest-enemy-filter: {rules: skip-submerged}` | On, the parameters named set and the rest at their defaults. |
| `ai.nearest-enemy-filter: {preset: baseline}` | On, starting from a preset; parameters named beside `preset` replace its values. |
| `ai.nearest-enemy-filter: skip-submerged` | On, the shorthand: a bare value sets `rules`. |
| `ai.nearest-enemy-filter: false` | Off, the same as leaving it out: 3.1c behaviour. |

### Parameters

| Parameter | Type | Unit | Allowed | Length | Baseline (3.1c) | Default | Adjustable | Scope | Overridden by |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `rules` | `enum` | - | `base`, `skip-submerged` | - | `base` | `skip-submerged` | `fixed` | `sim` | - |

Adjustable: `fixed`: only the profile sets it.

### Presets

| Preset | Values |
| --- | --- |
| `baseline` | `{rules: base}` |

`baseline` sets every parameter to its 3.1c value: the hack is on and plays as 3.1c.

### Shorthand

The hack has one parameter, so a bare value sets `rules`: `ai.nearest-enemy-filter: skip-submerged` is `ai.nearest-enemy-filter: {rules: skip-submerged}`.

### Every parameter at its default

```yaml
hacks:
  ai.nearest-enemy-filter:
    rules: skip-submerged
```
<!-- END GENERATED: schema -->
