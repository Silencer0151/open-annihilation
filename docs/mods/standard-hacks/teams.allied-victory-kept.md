# Keep Allied Victory

<!-- BEGIN GENERATED: facts -->
| Fact | Value |
| --- | --- |
| Hack id | `teams.allied-victory-kept` |
| Area | Teams (`teams`) |
| Scope | sim: part of the profile hash; every machine must agree |
| Runs on | The host's machine. |
| Parameters | 0 |
| Status | implemented |
<!-- END GENERATED: facts -->

## Description

In the battle room a player's Allied Victory setting stays as the player set it, even when the player
is on no team or alone on a team. 3.1c clears it in those cases.

## Configuration example

```yaml
hacks:
  teams.allied-victory-kept: true
```

## Details

### Behaviour

Allied Victory is the battle-room setting with which allied players win together. Whenever the
team choices change, the battle room runs its team pass: every pair of team-mates is allied both
ways, and both get Allied Victory. In 3.1c the same pass then clears Allied Victory for every player
whose team has fewer than two members, which includes a player on no team.

With the hack that last step is skipped, so a player on no team, or alone on a team, keeps Allied
Victory as set. Leaving a team still clears it, as in 3.1c: a player who leaves unallies the former
team-mates and loses the setting.

### In a network game

The battle room applies it to the players' setup blocks; the host's battle room decides the
game's setup. The hack is part of the profile hash, so every machine agrees that it is on.

### Interactions

- [teams.team-number-alliances](teams.team-number-alliances.md): moves alliances from team numbers
  as they arrive over the network.

### Implementation notes

- `lobby_update_ally_matrix` in `src/ui/frontend-multiplayer/src/battleroom.cpp` skips clearing
  the Allied Victory bit (`status::allied_victory`) while
  `MatchRules::teams.allied_victory_kept` is on.
- Tests: `ui-multiplayer-lobby-rules` (`test_allied_victory`).

## Full configuration schema

<!-- BEGIN GENERATED: schema -->
Hack id `teams.allied-victory-kept`, written under the profile's `hacks` block.

| Write | Means |
| --- | --- |
| `teams.allied-victory-kept: true` | On. |
| `teams.allied-victory-kept: false` | Off, the same as leaving it out: 3.1c behaviour. |

### Parameters

This hack has no parameters.

### Presets

This hack has no presets.

### Shorthand

This hack has no parameters to set: write `true` or `false`.

### Every parameter at its default

```yaml
hacks:
  teams.allied-victory-kept: true
```
<!-- END GENERATED: schema -->
