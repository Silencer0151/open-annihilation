# Host Stays as Watcher

<!-- BEGIN GENERATED: facts -->
| Fact | Value |
| --- | --- |
| Hack id | `network.host-stays-as-watcher` |
| Area | Network (`network`) |
| Scope | sim: part of the profile hash; every machine must agree |
| Runs on | The host's machine. |
| Parameters | 0 |
| Status | implemented |
<!-- END GENERATED: facts -->

## Description

When the player hosting a network game is defeated, they stay in the game
as a watcher instead of being asked to leave, so the game goes on for
everyone else.

## Configuration example

```yaml
hacks:
  network.host-stays-as-watcher: true
```

## Details

### Behaviour

At the end-of-game check, when the local player is defeated in a
multiplayer game and holds the host role, the engine makes that player a
watcher whatever the game's watching option says. The game is not over
for them: the outcome stays "ongoing", the "won" flag is cleared, and
the sight buffers are reset as for any new watcher.

The host sees this message:

> You have been defeated.  You stay in the game as a watcher so that it
> goes on for the other players.

A defeated player who does not host is treated exactly as in 3.1c. The
rule needs the local player's setup block. Without one, the player cannot
be marked as watching and loses as before.

### Baseline (3.1c)

A defeated player becomes a watcher only when the game allows watching.
Otherwise the player loses and is offered the exit. When that player is
the host, leaving ends the session for everyone.

### Network games

Only the host's machine acts on the rule, since only there is the local
player the host. The other machines see the host turn into a watcher
through the usual player-status update. The rule is part of the profile
hash, so every machine must have the same setting.

### Interactions

None with other hacks. It uses the same watch mode as a defeated player
in a game that allows watching.

### Implementation notes

- The profile's `network.host_stays_as_watcher.enabled`
  (`src/data/mod-profile`) becomes `WireRules::host_stays_watching`
  in `src/app/netgame/wire_rules_binding.cpp`.
- `src/app/netgame/runtime_net.cpp` passes it to the match with
  `Match::set_host_stays_watching` when a network match starts.
- `Match::advance_local_outcome` and `Match::become_watcher` in
  `src/sim/match-runtime/src/tick_missions.cpp` apply it, and send
  `WatchNotice::host_watching`.
- `src/app/runtime_sides.cpp` shows the message.
- Tests:
  - `defeated_host_stays_watching` in `match-commander-rule`
    (`src/sim/match-runtime/tests/commander_rule_test.cpp`) covers the
    host with the rule, the host without it, and a guest with it.
  - `every_rule_maps` in `netgame-wire-rules` checks the binding.

## Full configuration schema

<!-- BEGIN GENERATED: schema -->
Hack id `network.host-stays-as-watcher`, written under the profile's `hacks` block.

| Write | Means |
| --- | --- |
| `network.host-stays-as-watcher: true` | On. |
| `network.host-stays-as-watcher: false` | Off, the same as leaving it out: 3.1c behaviour. |

### Parameters

This hack has no parameters.

### Presets

This hack has no presets.

### Shorthand

This hack has no parameters to set: write `true` or `false`.

### Every parameter at its default

```yaml
hacks:
  network.host-stays-as-watcher: true
```
<!-- END GENERATED: schema -->
