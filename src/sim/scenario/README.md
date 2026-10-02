# Scenario state

The scenario controller: construction, GlobalHeader condition registration,
unit event dispatch, victory and defeat tests, the commander rule's deathmatch
branches, and the campaign file and map catalogue loaders. The map loader
creates the controller before selecting the schema.

Call `register_conditions` with a DefinitionHost backed by the selected OTA
GlobalHeader before spawning units. Absent condition keys produce the game's
DestroyAllUnits victory object and AllUnitsKilled defeat object. The controller
owns both arrays; an unregistered controller cannot receive notifications.

`notify_unit_created` is complete: none of the 18 condition kinds reacts to a
unit's creation. `dispatch` (conditions.hpp) runs each kind's unit-destroyed
and unit-captured handler from a table keyed by kind, and `evaluate_condition`
(outcome.hpp) runs each kind's query from another. The handlers and queries
are free functions over the canonical World; the match runtime passes a
`ConditionHost` (the victory cue, the movement query and the MoveUnitToRadius
query, which needs the match's terrain) and, for the queries, a
`QueryContext`. `save_conditions` and `load_conditions` write and read each
condition's savegame account.

Each condition is a plain `Condition` record with named fields. Every kind
uses the two flags and the fields whose comments name it; a field that a
kind's constructor leaves unset keeps its initial value until the kind writes
it. `register_conditions` returns a `DefinitionError` for malformed condition
text or a name past the 32-byte field, leaving the controller unregistered; the
condition builders return nothing for such a name.

Standalone build:

```
cmake -S src/sim/scenario -B local/build-scenario-state
cmake --build local/build-scenario-state
ctest --test-dir local/build-scenario-state --output-on-failure
```

`outcome.hpp` also covers ordinary skirmish victory and defeat queries, the
timed disc-check loss, and the local player's result countdown and flags. The
caller supplies current live-unit counts and the local alliance row, and calls
the countdown once per due 30-tick player interval. A campaign checks victory
first; an ordinary skirmish checks defeat first. Outcome checks must start only
after player initialization. Respawn game mode 2 and multiplayer spectator
transitions are outside this helper. The match runtime owns cadence and frontend
transition consumption.
