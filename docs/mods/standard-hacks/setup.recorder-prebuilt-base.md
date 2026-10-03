# Prebuilt Base

<!-- BEGIN GENERATED: facts -->
| Fact | Value |
| --- | --- |
| Hack id | `setup.recorder-prebuilt-base` |
| Area | Game Setup (`setup`) |
| Scope | sim: part of the profile hash; every machine must agree |
| Runs on | The host's machine, and every machine in the game. |
| Parameters | 1 |
| Status | implemented |
<!-- END GENERATED: facts -->

## Description

With the game recorder's base commands, the host can offer every player a
prebuilt base: a set of buildings, the standard one or one read from a
file, that the host's machine creates for a player around the spot where
that player's second unit appeared. 3.1c has no prebuilt bases: players
start with their commander only.

## Configuration example

```yaml
hacks:
  setup.recorder-prebuilt-base: true
```

The commands need the game recorder:

```yaml
hacks:
  recorder.ta-demo-recorder: true
  setup.recorder-prebuilt-base: true
```

```yaml
hacks:
  setup.recorder-prebuilt-base:
    available: false   # listed, but the base commands stay ordinary chat: 3.1c behaviour
```

## Details

### Commands

| Command | Who and where | Effect |
| --- | --- | --- |
| `.base` | the host, in the battle room | Offers every seated player the standard base. The host's machine answers "Standard base initiated .baseoff to disable". |
| `.base <file>` | the host, in the battle room | Offers the base the file lists instead: "Fast base initiated from *file* .baseoff to disable". |
| `.baseoff` | the host, in the battle room or the game | Takes every offer back and turns prebuilt bases off for the rest of the session: "Quick base disabled". |
| `.dobase` | a player who was offered a base, in the game | Has the host's machine build that player's base, once. |

Command words are matched without regard to case. Only the host's `.base`
and `.baseoff` count. `.dobase` counts from any player, and only the
host's machine acts on it. `.base` typed in the game, and `.dobase` typed
in the battle room, do nothing. Without the hack, with `available: false`,
or without the recorder, these lines are ordinary chat.

### Offers

- `.base` offers a base to every slot in use when it is typed, computer
  players included.
- A player who joins the battle room from another machine afterwards
  takes every offer back, and the host's machine says "New player. Quick
  base toggled off". The host may type `.base` again.
- After `.baseoff` no base is built for the rest of the session, even if
  the host types `.base` again. A newly hosted game starts afresh.
- The offers and the base list go with the game when it starts. Only the
  host's machine holds them.

### Base files

- `.base <file>` reads the file that the first word after the command
  names, cut to 31 characters: an absolute path on the host's computer
  when a file is there, or else a game file of that name, loose in the
  game's folders or inside one of its archives. A file larger than 64 KiB
  is not read.
- Lines end at a line feed, and a carriage return before it is dropped.
  Empty lines and lines that start with `;` are skipped.
- The first other line is the number of buildings each side's base holds,
  and nothing else.
- Every later line is one building, `entry type x z health;`: five
  numbers, each of the first four followed by one space and the fifth by
  `;`. Whatever follows the `;` is ignored. A number may have a sign, and
  is hexadecimal after `$`.
  - `entry`, 0 to 1000, is the building's place in the base list. With
    *n* buildings a side, ARM's base is entries 1 to *n* and CORE's
    entries *n* + 1 to twice *n*.
  - `type`, 0 to 65535, is the unit type, by the number a unit-creation
    record names it with.
  - `x` and `z`, -32768 to 32767, are the map pixels east and south of
    the base's centre.
  - `health`, 0 to 65535, is the health the building is handed over with.
- Entries the file does not list keep what they held, from the standard
  base or an earlier file.
- Reading stops at the first line in error. The lines before it still
  take effect, and the host's machine answers with what was wrong:
  "Erroneous number of possible buildings" for the first line, or
  "Erroneous base file1" to "Erroneous base file5" for the first to fifth
  number of a building's line. A file in error, or one that cannot be read
  ("Unable to open file *file*"), offers nothing new.

### The standard base

The standard base holds fifteen buildings a side, the same layout for ARM
(entries 1 to 15) and CORE (entries 16 to 30), within 220 map pixels east
or west of the centre and from 200 north to 160 south of it. It names its
buildings by the unit type numbers of 3.1c's own units; where a profile's
units differ, those numbers name other types.

### Building a base

- The centre of a player's base is where the unit in the second place of
  the player's block of unit slots was last created: normally the first
  unit the player builds after the commander. The host's machine notes it
  from the creation records it sends and receives. Until it knows one,
  `.dobase` is answered with "Please build a building to mark the centre
  of your base", and the offer stays.
- Once the centre is known, the host's machine says "*name* just built
  a base". For each building of the player's side it creates the building
  at the centre plus the building's offsets, at the centre's height, in a
  spare unit slot, the next-to-last slot of the host's own block. It
  switches the building on and hands it to the player with the listed
  health, and the player's machine creates it again as the player's own
  finished unit, as it does for any unit handed over.
- A building whose x or z would be 0 or less is left out.
- The offer is then used up: a second `.dobase` builds nothing.
- A player whose side is neither ARM nor CORE gets "not arm/core" and no
  base, and the offer is used up.
- For the host's own base the spare slot is in the block of the first
  player, in slot order, who plays on another machine, and the host's
  machine takes the records itself as if that player's machine had sent
  them. With no such player the answer is "*name* please wait for sync",
  and the offer stays.

### Baseline (3.1c)

3.1c has no prebuilt bases: players start with their commander only.

### Network games

- Only the host's machine reads the base list, keeps the offers and
  builds the bases. Every machine reads the commands, so `.baseoff` turns
  bases off everywhere.
- For another player's base, the host's machine sends that player's
  machine alone the creation, switch-on and hand-over records. That
  machine then creates the buildings as the player's own units, which
  reach the other machines as any new unit of that player does.
- The hack is part of the profile hash, so every machine plays under the
  same rule.

### Interactions

[The game recorder](recorder.ta-demo-recorder.md) carries the commands;
without it there are none.

### Implementation notes

- The rule is `MatchRules::setup.recorder_prebuilt_base` (`enabled`,
  `available`). The battle room reads it through `Lobby::rules`, the game
  through `NetMatch::match_rules`.
- In
  [src/netgame/wire/recorder_session.cpp](../../../src/netgame/wire/recorder_session.cpp),
  `parse_recorder_command` reads the commands, `recorder_command_host_only`
  makes `.base` and `.baseoff` the host's, `recorder_read_base` reads a
  base file, `recorder_standard_base` fills in the standard base and
  `recorder_base_read_text` gives the error lines. The list and the offers
  are `RecorderBase`, part of `RecorderSession`
  ([src/netgame/include/oa/netgame/recorder_session.hpp](../../../src/netgame/include/oa/netgame/recorder_session.hpp)).
- In the battle room,
  [src/ui/frontend-multiplayer/src/battleroom.cpp](../../../src/ui/frontend-multiplayer/src/battleroom.cpp)
  carries out `.base` and `.baseoff` (`recorder_chat_line`,
  `recorder_offer_base`) and takes the offers back as a player joins
  (`lobby_add_player`). The file is read through `LobbyServices::read_file`
  ([lobby.hpp](../../../src/ui/frontend-multiplayer/include/oa/ui/frontend_multiplayer/lobby.hpp)),
  which `service_read_file` in
  [src/ui/frontend-multiplayer/src/screens.cpp](../../../src/ui/frontend-multiplayer/src/screens.cpp)
  provides.
- `NetHost::launch` in
  [src/app/netgame/runtime_net.cpp](../../../src/app/netgame/runtime_net.cpp)
  hands the battle room's recorder session, offers included, to the game.
- In
  [src/netgame/match/src/net_match.cpp](../../../src/netgame/match/src/net_match.cpp),
  `recorder_note_create` notes each centre from the creation records sent
  and received, `recorder_do_base` and `build_base` carry out `.dobase`,
  and `turn_bases_off` carries out `.baseoff` in the game.
- Tests:
  - `net-wire-rules` (`recorder_take_and_base_commands_parse` and
    `recorder_base_files_read` in `src/netgame/tests/wire_rules_test.cpp`):
    which commands are the host's, reading base files and their errors,
    and the standard base.
  - `ui-multiplayer-lobby` (`test_recorder_prebuilt_base_in_the_battle_room`
    in `src/ui/frontend-multiplayer/tests/lobby_test.cpp`): without the
    rule `.base` is only chat; the standard base and a file are offered to
    every seated player; a file in error, a missing file, a joining player
    and `.baseoff`.
  - `net-match` (`recorder_dobase_builds_the_offered_base` in
    `src/netgame/match/tests/net_match_test.cpp`): the centre from the
    second unit, the records the host's machine sends and the player's
    machine takes, a base built once, the host's own base from the next
    player's spare slot with buildings past the map's edge left out, and
    `.baseoff`.

## Full configuration schema

<!-- BEGIN GENERATED: schema -->
Hack id `setup.recorder-prebuilt-base`, written under the profile's `hacks` block.

| Write | Means |
| --- | --- |
| `setup.recorder-prebuilt-base: true` | On, every parameter at its default. |
| `setup.recorder-prebuilt-base: {available: true}` | On, the parameters named set and the rest at their defaults. |
| `setup.recorder-prebuilt-base: {preset: baseline}` | On, starting from a preset; parameters named beside `preset` replace its values. |
| `setup.recorder-prebuilt-base: false` | Off, the same as leaving it out: 3.1c behaviour. |

### Parameters

| Parameter | Type | Unit | Allowed | Length | Baseline (3.1c) | Default | Adjustable | Scope | Overridden by |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `available` | `bool` | - | `true`, `false` | - | `false` | `true` | `fixed` | `sim` | - |

Adjustable: `fixed`: only the profile sets it.

### Presets

| Preset | Values |
| --- | --- |
| `baseline` | `{available: false}` |

`baseline` sets every parameter to its 3.1c value: the hack is on and plays as 3.1c.

### Shorthand

This hack takes no shorthand: write `true`, `false` or a parameter map.

### Every parameter at its default

```yaml
hacks:
  setup.recorder-prebuilt-base:
    available: true
```
<!-- END GENERATED: schema -->
