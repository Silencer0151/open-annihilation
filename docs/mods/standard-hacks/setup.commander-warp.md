# Commander Warp

<!-- BEGIN GENERATED: facts -->
| Fact | Value |
| --- | --- |
| Hack id | `setup.commander-warp` |
| Area | Game Setup (`setup`) |
| Scope | sim: part of the profile hash; every machine must agree |
| Runs on | Every machine in the game. |
| Parameters | 1 |
| Status | implemented |
<!-- END GENERATED: facts -->

## Description

With the game recorder's `.cmdwarp` command, a network game starts held:
each player clicks on the map to move their commander wherever they like,
then presses Done, and the game starts once every human player is done.
3.1c has no such start: each commander starts on its start position and
the game runs at once.

## Configuration example

```yaml
hacks:
  setup.commander-warp: true
```

The `.cmdwarp` command needs the game recorder:

```yaml
hacks:
  recorder.ta-demo-recorder: true
  setup.commander-warp: true
```

```yaml
hacks:
  setup.commander-warp:
    available: false   # listed, but .cmdwarp stays ordinary chat: 3.1c behaviour
```

## Details

### Turning the warp on

- The host types `.cmdwarp` in the battle room chat; the command word is
  matched without regard to case. Every machine notes that the warp is
  on, and the host's machine answers every player with "Cmd warping
  enabled". The next `.cmdwarp` turns the warp off again ("Cmd warping
  disabled").
- Only the host's command counts. From any other player the line is
  ordinary chat.
- Without the hack, with `available: false`, or without the recorder,
  `.cmdwarp` is ordinary chat.

### The start

When a network game with the warp on has loaded:

1. Every machine holds the game as it starts.
2. Each player sees "Place your commander and click done" and a Done
   button over the battlefield. Each left press on the battlefield moves
   the player's commander to the map point under the pointer, as often
   as the player likes.
3. A press and release of the left button on Done ends the placing. The
   prompt becomes "Waiting for others to finish", and the machine tells
   every other machine that runs the recorder that its player is done.
4. Once a machine has heard that every human player still in the game is
   done, it unpauses the game itself. Computer players and watchers are
   not waited for.
5. An unpause before then ends the warp for everyone; with `.autopause`
   also on, only the host's unpause counts. The placing closes as soon as
   the game runs, whether or not the player pressed Done.

Where the commander goes:

- The map point under the pointer is the map pixel the frame draws there,
  read as on flat ground, as the whiteboard finds its marks: without the
  terrain's height.
- The commander's whole x and z become the point's. It keeps its height,
  its fractions of a pixel and its layer, and its footprint and sight move
  with it.
- Any point on the map is accepted; a point off the map moves nothing.
  Once the player has pressed Done the commander stays where it is.

A watcher, or a player without a commander, has nothing to place: its
machine says it is done at once. Computer players place nothing, and their
commanders stay on their start positions.

### Baseline (3.1c)

3.1c has no commander warp: each commander starts on its start position
and the game runs at once.

### Network games

Each machine places only its own player's commander and tells the other
machines when that player is done; each machine releases the game itself
once it has heard from every human player.
[network.commander-start-sync](network.commander-start-sync.md) sends each
commander's position to the other machines early in the game, so a placed
commander stands in the same place on every machine. The hack is part of
the profile hash, so every machine plays under the same rule.

### Interactions

- [The game recorder](recorder.ta-demo-recorder.md) carries the `.cmdwarp`
  command and the "warp done" messages; without it there is no command.
- [network.commander-start-sync](network.commander-start-sync.md) shares
  where each commander ends up.
- The `.autopause` command of
  [network.recorder-session-commands](network.recorder-session-commands.md)
  shares the held start. With both on, only the host's unpause starts the
  game: once the host's machine has heard that every human player is done,
  its unpause releases every machine.

### Implementation notes

- The rule is `MatchRules::setup.commander_warp` (`enabled`, `available`).
  The battle room reads it through `Lobby::rules`, the game through
  `NetMatch::match_rules` (`net_match_bind_rules`).
- `parse_recorder_command` reads `.cmdwarp`, and
  `recorder_apply_host_command` turns the warp on or off, in
  [src/netgame/wire/recorder_session.cpp](../../../src/netgame/wire/recorder_session.cpp).
  In the battle room, `recorder_chat_line` and `commander_warp_available`
  in
  [src/ui/frontend-multiplayer/src/battleroom.cpp](../../../src/ui/frontend-multiplayer/src/battleroom.cpp)
  apply it and send the answer; in the game, `recorder_chat_line` in
  [src/netgame/match/src/net_match.cpp](../../../src/netgame/match/src/net_match.cpp)
  does.
- At the start, `NetHost::finish` in
  [src/app/netgame/runtime_net.cpp](../../../src/app/netgame/runtime_net.cpp)
  calls `net_match_recorder_start`, which holds the game, and opens the
  local player's placing with `Match::begin_commander_placement`. A machine
  with nothing to place calls `net_match_warp_done` at once.
- `Match::begin_commander_placement`, `Match::place_commander` and
  `Match::finish_commander_placement` in
  [src/sim/match-runtime/src/match.cpp](../../../src/sim/match-runtime/src/match.cpp)
  keep the placing's state, a `CommanderPlacement` of none, placing or
  waiting
  ([src/sim/match-runtime/include/oa/sim/match_runtime.hpp](../../../src/sim/match-runtime/include/oa/sim/match_runtime.hpp)),
  and `Match::tick`
  ([src/sim/match-runtime/src/tick.cpp](../../../src/sim/match-runtime/src/tick.cpp))
  closes it. Finishing reports through `MultiplayerHooks::commander_placed`,
  which the match binding
  ([src/netgame/match/src/match_binding.cpp](../../../src/netgame/match/src/match_binding.cpp))
  turns into `net_match_warp_done`.
- `Runtime::commander_placement_pointer` and
  `Runtime::draw_commander_placement` in
  [src/app/runtime_commander_placement.cpp](../../../src/app/runtime_commander_placement.cpp)
  take the presses and draw the prompt and the button; the map point is
  `Runtime::battlefield_map_point`
  ([src/app/runtime_whiteboard.cpp](../../../src/app/runtime_whiteboard.cpp)).
- In `net_match.cpp`, `net_match_paused_frame` releases the game once
  `all_warps_done` finds every active human player who is not watching
  done, and `net_match_set_pause` and the pause record's handler end the
  warp on any unpause.
- Tests:
  - `net-wire-rules` (`recorder_commands_parse` in
    `src/netgame/tests/wire_rules_test.cpp`): `.cmdwarp` turns the warp on
    and the next one off.
  - `ui-multiplayer-lobby` (`test_recorder_commands_in_the_battle_room` in
    `src/ui/frontend-multiplayer/tests/lobby_test.cpp`): without the rule
    `.cmdwarp` is only chat; only the host's applies; the two answers.
  - `net-match` (`recorder_records_reach_the_match` and
    `commander_warp_follows_its_rule` in
    `src/netgame/match/tests/net_match_test.cpp`): the hold, the "warp
    done" exchange and the unpause, computer players not waited for, and
    an early unpause ending the warp.
  - `match-commander-placement` (`commander_moves_while_placing` in
    `src/sim/match-runtime/tests/commander_placement_test.cpp`): the
    placing opens on a live commander, moves its whole x and z and keeps
    its fractions, height, layer and footprint with it, refuses points off
    the map, ends on Done, and closes as the game runs.
  - `native-pointer-interfaces`, which needs the game's data:
    `Runtime::check_commander_placement` drives the placing in a running
    skirmish through synthetic mouse input.

## Full configuration schema

<!-- BEGIN GENERATED: schema -->
Hack id `setup.commander-warp`, written under the profile's `hacks` block.

| Write | Means |
| --- | --- |
| `setup.commander-warp: true` | On, every parameter at its default. |
| `setup.commander-warp: {available: true}` | On, the parameters named set and the rest at their defaults. |
| `setup.commander-warp: {preset: baseline}` | On, starting from a preset; parameters named beside `preset` replace its values. |
| `setup.commander-warp: false` | Off, the same as leaving it out: 3.1c behaviour. |

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
  setup.commander-warp:
    available: true
```
<!-- END GENERATED: schema -->
