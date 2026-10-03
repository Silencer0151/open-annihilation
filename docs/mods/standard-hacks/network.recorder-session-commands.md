# Recorder Session Commands

<!-- BEGIN GENERATED: facts -->
| Fact | Value |
| --- | --- |
| Hack id | `network.recorder-session-commands` |
| Area | Network (`network`) |
| Scope | sim: part of the profile hash; every machine must agree |
| Runs on | Every machine in the game. |
| Parameters | 1 |
| Status | implemented |
<!-- END GENERATED: facts -->

## Description

Players can type a set of optional session commands into chat, in the
battle room and in the game: the host can hold the start until everyone
is ready, players can vote to start or watch while seated, and the host
can pick a random map. Without the hack, recorders ignore these commands.

## Configuration example

```yaml
hacks:
  network.recorder-session-commands: true
```

```yaml
hacks:
  network.recorder-session-commands:
    available: false   # listed, but the commands stay off: 3.1c behaviour
```

## Details

### Behaviour

The commands are chat lines that start with a dot. Each machine with the
recorder reads every line it sends or hears, matching the command word
without regard to case. The hack makes these commands available:

| Command | Who | Effect |
| --- | --- | --- |
| `.autopause` | host | The game starts paused, and only the host's unpause starts it. An unpause from anyone else is undone, and the notice `<name> tried to unpause.` is shown. The host's recorder announces `Autopause enabled - At the start only the host can unpause`. |
| `.ready`, `.voteready` | anyone | Marks the sender as ready. Once every player still playing is ready and the game is paused, every machine unpauses it, which also ends the autopause hold. |
| `.votego`, `.forcego` | anyone | In the battle room, watchers count as ready, so the game can start. |
| `.fakewatch` | the local player | Five seconds later, the local player watches while keeping their seat. In place of each record except chat and resource shares, their machine sends a load report, and it sends empty unit states. |
| `.randmap`, `.randmapex` | host, on the host's machine | Picks a map at random from the map list and selects it as the host's map dialog does. |
| `.f1off` | host | Sets the session's "unit help key off" option, which goes to the other recorders with the host's options. |
| `.forcecd` | anyone | Accepted and ignored: the engine never needs the game CD. |

A command from a player other than the one named in the "Who" column is
ignored. The other recorder commands (`.report`, `.syncon`, `.record`
and the rest) belong to
[recorder.ta-demo-recorder](recorder.ta-demo-recorder.md) and work
without this hack, except four hacks' own commands: `.syncon` and
`.syncoff` work only under
[console.game-speed-range](console.game-speed-range.md) with `syncon` on;
`.cmdwarp` only under [setup.commander-warp](setup.commander-warp.md); `.base`, `.baseoff`
and `.dobase` only under
[setup.recorder-prebuilt-base](setup.recorder-prebuilt-base.md); and
`.give`, `.stopgive`, `.take` and `.takecmd` only under
[sharing.recorder-take-give](sharing.recorder-take-give.md).

`available: false` keeps the commands off even when the hack is listed.

### Baseline (3.1c)

3.1c has no chat commands of this kind. With the recorder on and this
hack off, these lines are ordinary chat and nothing acts on them.

### Network games

Every machine with the recorder reads the commands, and each acts on its
own part: the host for host commands, the local machine for its own
player. The state they change (ready marks, the autopause hold, watching
while seated) must match on every machine, so the setting is part of the
profile hash.

### Interactions

- Needs [recorder.ta-demo-recorder](recorder.ta-demo-recorder.md):
  without the recorder, no chat line is read for commands.
- The autopause hold and [setup.commander-warp](setup.commander-warp.md)
  share the recorder's paused start: with either set, every recorder
  pauses as the game begins.
- [ui.share-dialog-and-lobby-buttons](ui.share-dialog-and-lobby-buttons.md)
  can add a battle room button that says `.autopause`.

### Implementation notes

- The profile's `network.recorder_session_commands` (`enabled` and
  `available`) becomes `WireRules::recorder_session_commands` in
  `src/app/netgame/wire_rules_binding.cpp`.
- `parse_recorder_command`, `recorder_session_command` and
  `recorder_apply_host_command` in `src/netgame/wire/recorder_session.cpp`
  read and classify the commands (`RecorderCommand` in
  `src/netgame/include/oa/netgame/recorder_session.hpp`).
- `recorder_chat_line` in `src/ui/frontend-multiplayer/src/battleroom.cpp`
  handles the battle room. `recorder_chat_line`,
  `net_match_recorder_start` and the `pause_speed` record handler in
  `src/netgame/match/src/net_match.cpp` handle the game.
- Tests:
  - `recorder_commands_parse` in `net-wire-rules`
    (`src/netgame/tests/wire_rules_test.cpp`).
  - `autopause_holds_for_the_host` and
    `recorder_session_commands_in_the_match` in `net-match`
    (`src/netgame/match/tests/net_match_test.cpp`).
  - `test_recorder_commands_in_the_battle_room` in
    `ui-multiplayer-lobby` (`src/ui/frontend-multiplayer/tests/lobby_test.cpp`).

**Known limits:**

- `.randmap` and `.randmapex` act alike. Each picks uniformly from the
  maps the game lists, using the clock; neither reads a weighted map list.
- `.f1off` sets and sends the option, but the engine does not yet turn
  the unit help key off.

## Full configuration schema

<!-- BEGIN GENERATED: schema -->
Hack id `network.recorder-session-commands`, written under the profile's `hacks` block.

| Write | Means |
| --- | --- |
| `network.recorder-session-commands: true` | On, every parameter at its default. |
| `network.recorder-session-commands: {available: true}` | On, the parameters named set and the rest at their defaults. |
| `network.recorder-session-commands: {preset: baseline}` | On, starting from a preset; parameters named beside `preset` replace its values. |
| `network.recorder-session-commands: false` | Off, the same as leaving it out: 3.1c behaviour. |

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
  network.recorder-session-commands:
    available: true
```
<!-- END GENERATED: schema -->
