# Squad Assignment

<!-- BEGIN GENERATED: facts -->
| Fact | Value |
| --- | --- |
| Hack id | `ai.squad-assignment` |
| Area | AI (`ai`) |
| Scope | sim: part of the profile hash; every machine must agree |
| Runs on | The machine that owns the unit or player concerned. |
| Parameters | 1 |
| Status | implemented |
<!-- END GENERATED: facts -->

## Description

Changes how a computer player sorts its idle units into squads and which standing orders it gives them. With the role-squad rules, buildings are split by what they do rather than whether they are armed, unarmed mobile units join a squad instead of staying idle, and only commanders get the Maneuver move order.

## Configuration example

```yaml
hacks:
  ai.squad-assignment: true          # the role-squad rules
```

```yaml
hacks:
  ai.squad-assignment:
    rules: base                      # on, but sorting as 3.1c does
```

## Details

### Behaviour

Every 30 ticks a computer player goes through its selectable units. It sets each unit's standing orders, then places each unit that is not yet in a squad.

**Standing orders.** Every unit gets Fire at Will. Its move order depends on the rules:

| Rules | Maneuver | Roam |
| --- | --- | --- |
| `base` (3.1c) | Types that can capture (`CanCapture`) | Every other type |
| `role-squads` | Types marked as commanders | Every other type |

**Squads.** The squads are numbered as in 3.1c: 1 structures, 3 land army, 4 builders, 5 armed structures, 7 navy, 8 aircraft. Each squad has its own task (see [`ai.squad5-factory-tick`](ai.squad5-factory-tick.md) for squad 5).

With `role-squads` a unit is placed by the first rule that matches:

1. A building that builds, or whose `EnergyUse` is 58.5 or more, joins the structures (1).
2. Any other building joins the armed structures (5), armed or not.
3. A mobile unit that builds joins the builders (4).
4. A mobile unit that flies joins the aircraft (8).
5. A mobile unit that needs water (`MinWaterDepth` above 0), can go 128 deep or more (`MaxWaterDepth`), or is amphibious joins the navy (7).
6. Any other mobile unit joins the land army (3), armed or not.

The `EnergyUse` test compares the upper 16 bits of the value's single-precision form as a signed number, and the engine reproduces that exactly. Values from 58.25 up to just under 58.5 count as below the threshold, and so does any negative `EnergyUse` (an energy producer).

A unit that is already in a squad keeps it; only its standing orders are refreshed.

### 3.1c baseline

With `base` (and with the hack off) a building joins the armed structures when it is armed and the structures otherwise. A mobile unit joins the builders when it builds, the aircraft when it flies, the navy when it needs water, and the land army when it is armed. Any other mobile unit, such as an unarmed scout or an unarmed amphibious unit, stays out of every squad and gets no task orders.

### Network games

The machine that owns the computer player sorts its units. The rules are part of the profile hash, so every machine must use the same value.

### Interactions

- [`ai.squad5-factory-tick`](ai.squad5-factory-tick.md): under `role-squads`, squad 5 also holds unarmed buildings that neither build nor use much energy, such as energy producers. They get the structures task only when that hack is on.
- [`ai.factory-tick-filter`](ai.factory-tick-filter.md) changes what the structures task does to the buildings in squads 1 and 5.
- [`ai.attack-wave-size`](ai.attack-wave-size.md) and [`ai.patrol-group-size`](ai.patrol-group-size.md) act on the land army, navy and aircraft squads this hack fills.

### Implementation notes

- The sort table is `computer_sort_squad` and the standing orders are set in `sort_squads`, both in `src/sim/ai/src/computer_player.cpp` (module `src/sim/ai`; the squad numbers are the `Squad` enumeration in `src/sim/ai/include/oa/sim/ai.hpp`). They read `MatchRules::ai.squad_assignment.rules`, of type `AiSquadAssignmentRules`.
- Tested by `test_squad_assignment` in `src/sim/ai/tests/computer_player_test.cpp` (ctest `ai-computer-player`), which covers each row of the table, the `EnergyUse` boundary and the standing orders under both rules. `src/sim/match-runtime/tests/computer_build_test.cpp` (ctest `match-computer-build`) plays a match with every `ai.*` hack on and checks that it replays identically.

## Full configuration schema

<!-- BEGIN GENERATED: schema -->
Hack id `ai.squad-assignment`, written under the profile's `hacks` block.

| Write | Means |
| --- | --- |
| `ai.squad-assignment: true` | On, every parameter at its default. |
| `ai.squad-assignment: {rules: role-squads}` | On, the parameters named set and the rest at their defaults. |
| `ai.squad-assignment: {preset: baseline}` | On, starting from a preset; parameters named beside `preset` replace its values. |
| `ai.squad-assignment: role-squads` | On, the shorthand: a bare value sets `rules`. |
| `ai.squad-assignment: false` | Off, the same as leaving it out: 3.1c behaviour. |

### Parameters

| Parameter | Type | Unit | Allowed | Length | Baseline (3.1c) | Default | Adjustable | Scope | Overridden by |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `rules` | `enum` | - | `base`, `role-squads` | - | `base` | `role-squads` | `fixed` | `sim` | - |

Adjustable: `fixed`: only the profile sets it.

### Presets

| Preset | Values |
| --- | --- |
| `baseline` | `{rules: base}` |

`baseline` sets every parameter to its 3.1c value: the hack is on and plays as 3.1c.

### Shorthand

The hack has one parameter, so a bare value sets `rules`: `ai.squad-assignment: role-squads` is `ai.squad-assignment: {rules: role-squads}`.

### Every parameter at its default

```yaml
hacks:
  ai.squad-assignment:
    rules: role-squads
```
<!-- END GENERATED: schema -->
