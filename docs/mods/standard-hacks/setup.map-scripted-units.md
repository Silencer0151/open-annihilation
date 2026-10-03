# Map Scripted Units

<!-- BEGIN GENERATED: facts -->
| Fact | Value |
| --- | --- |
| Hack id | `setup.map-scripted-units` |
| Area | Game Setup (`setup`) |
| Scope | sim: part of the profile hash; every machine must agree |
| Runs on | The machine that owns the unit or player concerned, and the host's machine. |
| Parameters | 2 |
| Status | implemented |
<!-- END GENERATED: facts -->

## Description

In skirmish and multiplayer games, a map whose schema lists units places them
for the players: each start position's units appear at the start in place of
its commander, timed units appear once their countdown has passed, and units
for player 11 go to a neutral computer player. 3.1c places schema units only
in campaign missions.

## Configuration example

Switch it on with timed units and the neutral computer player:

```yaml
hacks:
  setup.map-scripted-units: true
```

Place only the start units, and let players seat a neutral computer player
themselves:

```yaml
hacks:
  setup.map-scripted-units:
    timed-spawns: false      # entries with a CreationCountdown are never placed
    auto-neutral-ai: false   # START does not seat a computer player for player 11
```

## Details

### Behaviour

The map's schema lists units entries, each with a unit name, a position, an
optional `CreationCountdown` (in seconds) and an owner player number. Player
numbers 1 to 10 are start positions (1 is the first start position); 11 is
the neutral player. An entry with any flag set in its player number belongs
to no start position.

**At the start.** Each machine places the units of the players it runs,
humans and computer players, but not watchers:

- A player takes the entries with no countdown whose player number is its
  start position + 1. When the map has neutral units, the computer player
  placed at the last counted start position takes the entries of player 11
  instead of its own.
- When a player takes any entries, they replace its commander. A player that
  takes none starts with its commander as usual.
- An entry that names one of the sides' commanders (exactly) takes the slot
  90 + the entry's index past the player's first slot; other entries take
  any free slot.
- Each unit is created finished, at the entry's position, at the height of
  its map cell. The entries' `InitialMission` scripts then run, as they do
  in a campaign mission.
- On a map with neutral units, before its commander is placed, the highest
  placed computer player this machine runs swaps start positions with the
  highest placed human when the human's is higher, so the computer player
  takes the last position. A skirmish with fixed start positions does not
  swap.

**Timed units** (`timed-spawns`). Entries with a unit name and a countdown
above 0 are placed in countdown order, entries of equal countdown in schema
order. An entry is due once its countdown is at most the whole seconds of
game time (tick / 30). It goes to the player placed at its start position, or
to the neutral computer player for player 11, and only the machine that runs
that player places it. The check runs once per rendered frame, so several
entries can come due together. A resumed saved game skips the entries
already due.

**Neutral computer player** (`auto-neutral-ai`). When the host presses START,
map-placed units are on, the selected map lists units for player 11 and no
slot holds a computer player, the battle room seats a computer player in the
first open slot and posts three chat notices instead of starting:

- "An AI has been added to accept neutral units for this map"
- "Remove the AI now if you don't want it"
- "Use +spawnoff to disable extra unit spawn in general"

It does this once per battle room; the next START goes ahead.

**Chat commands.** In the battle room, `+spawnoff` and `+spawnon` (any case)
turn map-placed units off and on, each posting a notice.

### Baseline

3.1c ignores a skirmish or multiplayer map's schema units: every player
starts with its commander only.

### Network games

Each machine places the start and timed units of the players it runs; the
neutral computer player's units are placed by the machine that runs it. The
host seats the neutral computer player. The hack and its parameters are part
of the profile hash, so every machine reads the same rule.

### Interactions

- [setup.team-start-positions](setup.team-start-positions.md) deals start
  positions; on a map with neutral units, computer players go after the
  others.
- [setup.multiple-local-ai](setup.multiple-local-ai.md) lets the host keep
  its own computer players beside the neutral one.

### Implementation notes

- The entry rules (owner numbers, start picks, unit creation, timed order,
  the computer player's swap) are in
  `src/sim/mission-units/src/map_units.cpp`
  (`src/sim/mission-units/include/oa/sim/mission_units/map_units.hpp`).
- `src/app/runtime_map_units.cpp` applies them in a match:
  `Runtime::begin_map_units`, `place_map_units`, `move_map_unit_computer_last`
  and `step_timed_map_units`. They read
  `setup.map_scripted_units.enabled` and `timed_spawns`. A skirmish places
  them from `src/app/runtime_skirmish_start.cpp`, a multiplayer game from
  `src/app/netgame/runtime_net.cpp`; `src/app/runtime.cpp` runs the timed
  check once per frame.
- The battle room's neutral computer player and the chat commands are
  `seat_neutral_computer` and `lobby_run_setup_command` in
  `src/ui/frontend-multiplayer/src/battleroom.cpp`, reading
  `auto_neutral_ai`.
- Tests: `sim-mission-map-units`
  (`src/sim/mission-units/tests/map_units_test.cpp`) covers owner numbers,
  start picks, creation, timed entries and the swap;
  `ui-multiplayer-lobby-rules` (`test_commands`, `test_neutral_computer`)
  covers the chat commands and the neutral computer player.
- Known limit: `+spawnoff` only stops the battle room seating the neutral
  computer player. The match still places the map's units.

## Full configuration schema

<!-- BEGIN GENERATED: schema -->
Hack id `setup.map-scripted-units`, written under the profile's `hacks` block.

| Write | Means |
| --- | --- |
| `setup.map-scripted-units: true` | On, every parameter at its default. |
| `setup.map-scripted-units: {timed-spawns: true}` | On, the parameters named set and the rest at their defaults. |
| `setup.map-scripted-units: false` | Off, the same as leaving it out: 3.1c behaviour. |

### Parameters

| Parameter | Type | Unit | Allowed | Length | Baseline (3.1c) | Default | Adjustable | Scope | Overridden by |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `timed-spawns` | `bool` | - | `true`, `false` | - | `false` | `true` | `fixed` | `sim` | - |
| `auto-neutral-ai` | `bool` | - | `true`, `false` | - | `false` | `true` | `fixed` | `sim` | - |

Adjustable: `fixed`: only the profile sets it.

### Presets

This hack has no presets.

### Shorthand

This hack takes no shorthand: write `true`, `false` or a parameter map.

### Every parameter at its default

```yaml
hacks:
  setup.map-scripted-units:
    timed-spawns: true
    auto-neutral-ai: true
```
<!-- END GENERATED: schema -->
