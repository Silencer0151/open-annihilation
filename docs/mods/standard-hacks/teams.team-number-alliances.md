# Team Number Alliances

<!-- BEGIN GENERATED: facts -->
| Fact | Value |
| --- | --- |
| Hack id | `teams.team-number-alliances` |
| Area | Teams (`teams`) |
| Scope | sim: part of the profile hash; every machine must agree |
| Runs on | Every machine in the game, and the host's machine. |
| Parameters | 2 |
| Status | implemented |
<!-- END GENERATED: facts -->

## Description

A player's team number in the battle room decides alliances: every machine allies its own players
with the players on the same team and unallies the others, and answers alliance requests for its
players. The host gains the `+autoteam` and `+randomteam` commands, which deal players into teams.
In 3.1c a team number arriving from another machine is only stored.

## Configuration example

```yaml
hacks:
  teams.team-number-alliances: true
```

Read every team value as is, and have the battle room send alliances again from the teams:

```yaml
hacks:
  teams.team-number-alliances:
    bit7-keeps-alliances: false          # a team value of 6 or more is ignored
    lobby-rebroadcast: recompute-from-teams
```

## Details

### Behaviour

**Team values.** A player's team is 0 to 4, or 5 for no team. Each machine sends its players' team
numbers in the player-team record, whose format is unchanged from 3.1c. When a team record arrives:

- With `bit7-keeps-alliances: true`, bit 7 of the value (`0x80`) is taken off first. A value with it
  set stores the team and changes no alliance.
- A value of 6 or more after that is ignored. With `bit7-keeps-alliances: false` a value with bit 7
  set is therefore ignored.
- Otherwise the sender's team is stored, and every counted player this machine runs (a local human
  or a local computer player, seated and not watching) other than the sender allies the sender when
  both are on the same team and unallies it otherwise, in both directions, wherever the alliance
  differs from what it is now. When the sender joins no team, players that are themselves on no team
  keep their alliance with it.

**Alliance requests.** An alliance record whose both-sides word is `0xFFFFFFFF` and whose first
player runs on this machine asks this machine to set that player's alliance: the machine sets it
and announces it to everyone. This lets one machine form alliances for players on other machines,
human and computer.

**Battle-room resend.** After a player's setup block goes out, the battle room sends that player's
alliances with every other seated slot again, in slot order. `lobby-rebroadcast` decides each one:

- `stored-matrix`: each alliance as it stands.
- `recompute-from-teams`: allied exactly when both players are on the same team; a pair where either
  is on no team keeps its alliance.

**Battle-room commands** (typed as chat, any case, host only; on another machine they answer
"*command* can only be used by host"):

- `+autoteam [N]` and `+randomteam [N]` deal the counted players into *N* teams, 2 to 5 (2 when
  *N* is missing). The players are taken in a shuffled slot order. First every pair of counted
  players with an alliance either way is unallied both ways; then the *k*-th player takes team
  *k* modulo *N*; then each player allies its team-mates and unallies the rest. The host's own
  team changes go out with bit 7 set.
- `+autoteam` first posts "Autobalance not available. Setting random teams", since the engine takes
  no balanced order from outside the game; `+randomteam` posts "Setting random teams".

**In-game command.** `+autoteam [N]` in the game deals alliances by start position: each player with
a start position allies every player whose position equals its own modulo *N* and unallies the rest,
and the game reports "Alliances created with *N* teams". It answers instead:

- "+autoteam is only available to the host of a multiplayer game" outside a network game, or on a
  machine that did not work out team start positions;
- "+autoteam not available b/c too few players" with fewer than two players placed;
- "+autoteam not available b/c players have team selections" when a seated player is on a team.

Without the hack, a team record only stores the team, an alliance request is an ordinary alliance
record, the battle room sends no alliances again and the commands do not exist, as in 3.1c.

### In a network game

Every machine applies team records and alliance requests to the players it runs, and the alliance
records it then sends reach the other machines; the host runs the commands. Every machine must
agree on the hack and both parameters: a machine that reads bit 7 and one that does not treat the
host's dealt teams differently. The hack is part of the profile hash.

### Interactions

- [setup.team-start-positions](setup.team-start-positions.md): places team-mates together; the
  in-game `+autoteam` needs its `team-adjacent` mode, which works out the positions it deals by.
- [teams.allied-victory-kept](teams.allied-victory-kept.md): keeps Allied Victory for a player
  alone on a team.

### Implementation notes

- The rules are in `src/ui/frontend-multiplayer/src/team_rules.cpp`
  (`receive_team_number`, `alliance_requested`, `resend_alliances`, `dealt_team_count`,
  `deal_teams`, `alliances_by_position`), which the battle room and the network match share.
- The battle room applies them in `src/ui/frontend-multiplayer/src/battleroom.cpp` (the
  player-team and alliance records, the resend after a player's block, and
  `lobby_run_setup_command` for `+autoteam` and `+randomteam`).
- The game applies team records and alliance requests in `src/netgame/match/src/net_match.cpp`, and
  the in-game `+autoteam` is `net_match_deal_teams`, which the console reaches through
  `src/app/netgame/runtime_console_network.cpp`.
- It reads `MatchRules::teams.team_number_alliances` (`bit7_keeps_alliances`,
  `lobby_rebroadcast`).
- Tests: `ui-multiplayer-team-rules`, `ui-multiplayer-lobby-rules` (`test_team_records`,
  `test_alliance_requests`, `test_resent_alliances`, `test_commands`) and `net-match-rules`
  (`team_records_move_local_alliances`, `alliance_requests_are_carried_out`,
  `the_host_deals_teams_by_position`).

## Full configuration schema

<!-- BEGIN GENERATED: schema -->
Hack id `teams.team-number-alliances`, written under the profile's `hacks` block.

| Write | Means |
| --- | --- |
| `teams.team-number-alliances: true` | On, every parameter at its default. |
| `teams.team-number-alliances: {bit7-keeps-alliances: true}` | On, the parameters named set and the rest at their defaults. |
| `teams.team-number-alliances: false` | Off, the same as leaving it out: 3.1c behaviour. |

### Parameters

| Parameter | Type | Unit | Allowed | Length | Baseline (3.1c) | Default | Adjustable | Scope | Overridden by |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `bit7-keeps-alliances` | `bool` | - | `true`, `false` | - | `false` | `true` | `fixed` | `sim` | - |
| `lobby-rebroadcast` | `enum` | - | `stored-matrix`, `recompute-from-teams` | - | `stored-matrix` | `stored-matrix` | `fixed` | `sim` | - |

Adjustable: `fixed`: only the profile sets it.

### Presets

This hack has no presets.

### Shorthand

This hack takes no shorthand: write `true`, `false` or a parameter map.

### Every parameter at its default

```yaml
hacks:
  teams.team-number-alliances:
    bit7-keeps-alliances: true
    lobby-rebroadcast: stored-matrix
```
<!-- END GENERATED: schema -->
