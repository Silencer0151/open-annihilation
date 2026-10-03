# Keep Remote Player Colours

<!-- BEGIN GENERATED: facts -->
| Fact | Value |
| --- | --- |
| Hack id | `network.preserve-remote-player-colour` |
| Area | Network (`network`) |
| Scope | sim: part of the profile hash; every machine must agree |
| Runs on | Every machine in the game. |
| Parameters | 0 |
| Status | implemented |
<!-- END GENERATED: facts -->

## Description

While a game runs, a setup update from another player no longer
changes that player's colour: each remote player keeps the colour the
battle room gave them.

## Configuration example

```yaml
hacks:
  network.preserve-remote-player-colour: true
```

## Details

### Behaviour

During a network game, machines still send player-info records, which
carry a player's whole setup block. When one arrives for a remote player,
the engine copies the block in as 3.1c does. Under the rule, it then puts
back the byte that holds the player's colour and start position slot
(offset `0x96` of the block). The colour in force when the game left the
battle room therefore stays for the rest of the game. The rest of the
block is taken as sent.

The rule applies only to remote players, and only once the match is
running. Battle room traffic is not affected.

### Baseline (3.1c)

An in-game player-info record replaces the remote player's whole setup
block, colour slot included.

### Network games

Every machine applies the rule to the records it receives. It changes
only how players are shown, but it is part of the profile hash, so every
machine must have the same setting.

### Interactions

- With [recorder.ta-demo-recorder](recorder.ta-demo-recorder.md) on, the
  same handler also keeps the recorder protocol byte the battle room
  learnt, because in-game blocks say "no recorder". The two rules are
  independent.

### Implementation notes

- The profile's `network.preserve_remote_player_colour.enabled` becomes
  `WireRules::keep_remote_colour` in
  `src/app/netgame/wire_rules_binding.cpp`.
- The `RecordType::player_info` case in
  `src/netgame/match/src/net_match.cpp` applies it, using
  `player_info_color_offset` from
  `src/netgame/include/oa/netgame/records.hpp`.
- Tests:
  - `remote_colour_survives_in_game_blocks` in `net-match`
    (`src/netgame/match/tests/net_match_test.cpp`) runs with the rule on
    and off.
  - `every_rule_maps` in `netgame-wire-rules` checks the binding.

## Full configuration schema

<!-- BEGIN GENERATED: schema -->
Hack id `network.preserve-remote-player-colour`, written under the profile's `hacks` block.

| Write | Means |
| --- | --- |
| `network.preserve-remote-player-colour: true` | On. |
| `network.preserve-remote-player-colour: false` | Off, the same as leaving it out: 3.1c behaviour. |

### Parameters

This hack has no parameters.

### Presets

This hack has no presets.

### Shorthand

This hack has no parameters to set: write `true` or `false`.

### Every parameter at its default

```yaml
hacks:
  network.preserve-remote-player-colour: true
```
<!-- END GENERATED: schema -->
