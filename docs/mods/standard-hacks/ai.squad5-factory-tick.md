# Commander Runs Factories

<!-- BEGIN GENERATED: facts -->
| Fact | Value |
| --- | --- |
| Hack id | `ai.squad5-factory-tick` |
| Area | AI (`ai`) |
| Scope | sim: part of the profile hash; every machine must agree |
| Runs on | The machine that owns the unit or player concerned. |
| Parameters | 0 |
| Status | implemented |
<!-- END GENERATED: facts -->

## Description

A computer player's armed buildings (squad 5) run the same task as its other buildings: armed factories queue units from their build lists and power users follow the energy surplus. In 3.1c squad 5 has no task, so an armed building that builds, such as a silo that builds its own stockpile weapons, never builds anything for a computer player.

## Configuration example

```yaml
hacks:
  ai.squad5-factory-tick: true
```

## Details

### Behaviour

A computer player has one task per squad. Squad 1 (structures) runs the structures task in 3.1c. With this hack on, squad 5 (armed structures) runs the structures task too.

The structures task runs every 30 ticks over every live building in its squad:

- A building whose type the power toggle covers (a metal maker in 3.1c) follows the energy surplus. While the player's energy is no more than twice its metal, it is switched off. Otherwise, when the player's energy production exceeds its energy use and a one-in-five draw does not hold it back, it is switched on; when either test fails it is left as it is.
- Any other building with a build list and no current order queues one unit, chosen from the player's build lists as factories choose.

So an armed building that builds, and whose build list names a weapon to stockpile, now builds that weapon. Weapons a building has stockpiled are fired by the normal unit behaviour.

### 3.1c baseline

Squad 5 has an idle task. Armed buildings are sorted into it, so they are neither toggled nor given build orders.

### Network games

The machine that owns the computer player runs its tasks. The hack is part of the profile hash, so every machine must run with the same setting.

### Interactions

- [`ai.squad-assignment`](ai.squad-assignment.md) decides which buildings land in squad 5. With its `role-squads` rules, squad 5 holds every building that neither builds nor uses 58.5 energy or more, armed or not.
- [`ai.factory-tick-filter`](ai.factory-tick-filter.md) changes which buildings the power toggle covers and skips buildings with a busy background queue; it applies to squad 5 as soon as this hack gives squad 5 the task.

### Implementation notes

- `computer_player_create` in `src/sim/ai/src/computer_player.cpp` (module `src/sim/ai`) gives task 5 the kind `TaskKind::structures` instead of `TaskKind::idle` when `MatchRules::ai.squad5_factory_tick.enabled` is set. The task itself is `run_structures` in the same file.
- Tested by `test_squad5_factory_tick` in `src/sim/ai/tests/computer_player_test.cpp` (ctest `ai-computer-player`): an armed factory in squad 5 queues picks only with the hack on. `src/sim/match-runtime/tests/computer_build_test.cpp` (ctest `match-computer-build`) plays a match with every `ai.*` hack on and checks that it replays identically.

## Full configuration schema

<!-- BEGIN GENERATED: schema -->
Hack id `ai.squad5-factory-tick`, written under the profile's `hacks` block.

| Write | Means |
| --- | --- |
| `ai.squad5-factory-tick: true` | On. |
| `ai.squad5-factory-tick: false` | Off, the same as leaving it out: 3.1c behaviour. |

### Parameters

This hack has no parameters.

### Presets

This hack has no presets.

### Shorthand

This hack has no parameters to set: write `true` or `false`.

### Every parameter at its default

```yaml
hacks:
  ai.squad5-factory-tick: true
```
<!-- END GENERATED: schema -->
