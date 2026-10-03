# Team Start Positions

<!-- BEGIN GENERATED: facts -->
| Fact | Value |
| --- | --- |
| Hack id | `setup.team-start-positions` |
| Area | Game Setup (`setup`) |
| Scope | sim: part of the profile hash; every machine must agree |
| Runs on | The host's machine. |
| Parameters | 1 |
| Status | implemented |
<!-- END GENERATED: facts -->

## Description

Players on the same team start next to each other in the order of the map's start positions,
instead of taking positions one after another in slot order (or in a shuffled order) as in 3.1c.
On a map that places units for the neutral player, computer players that are alone on their team
start after everyone else.

## Configuration example

```yaml
hacks:
  setup.team-start-positions: true
```

The shorthand sets `mode`; `base` keeps 3.1c's assignment while the hack is listed:

```yaml
hacks:
  setup.team-start-positions: team-adjacent   # team-mates start together
```

## Details

### Behaviour

The host works out every player's start position when the game starts, and sends each machine its
position as 3.1c does. With `mode: team-adjacent` the host replaces both of 3.1c's branches, fixed
and random start positions, with this rule:

1. **Teams.** Only seated players that are not watching count. When every counted player is on a
   team (team numbers 0 to 4), each player's team is its team number. Otherwise the players are
   grouped by their alliances: players linked by alliances, directly or through others, form one
   team.
2. **Team-mates together.** When some team has more than one player, team *t* starts at position
   *t* - 1 and its players take every *n*-th position from there, *n* being the number of teams.
   Players are placed in slot order with fixed positions, and in a shuffled order with random
   positions. A position that is already taken, or one past the last slot, is replaced in a second
   pass by the lowest free position.
3. **No teams.** When no team has two players, the players take positions one after another, in
   slot order or shuffled, as in 3.1c.
4. **Neutral-unit maps.** On a map that places units for the neutral player (see
   [setup.map-scripted-units](setup.map-scripted-units.md)), computer players run by the host's
   machine that are alone on their team are placed after the others; when there are more than two
   teams, one fewer team then spreads the positions. Last, the computer player with the highest
   position swaps with the human holding the highest one when the human's position is higher, so a
   computer player takes the last position.

With random positions the shuffle draws from the game's random numbers, and 3.1c's own coin flip is
still drawn once, so the random sequence stays in step with a 3.1c host.

`mode: base` keeps 3.1c's assignment: with fixed positions each counted player takes the next
position in slot order, and with random positions the order is shuffled.

### In a network game

Only the host's machine works the positions out; the other machines receive their positions in the
start records, whose format is unchanged. The hack is part of the profile hash, so every machine
agrees that it is on.

### Interactions

- [teams.team-number-alliances](teams.team-number-alliances.md): the team numbers it carries decide
  the teams here, and its in-game `+autoteam` command deals alliances by the start positions this
  hack worked out, so that command needs `mode: team-adjacent`.
- [setup.map-scripted-units](setup.map-scripted-units.md): decides whether the map places units for
  the neutral player.

### Implementation notes

- The rule itself is `team_rules::team_start_positions` in
  `src/ui/frontend-multiplayer/src/team_rules.cpp`
  (`src/ui/frontend-multiplayer/include/oa/ui/frontend_multiplayer/team_rules.hpp`), which works
  over a plain view of the ten slots so that the battle room and the network match share it.
- The host's start barrier, `barrier_step` in `src/netgame/match/src/net_match.cpp`, calls it when
  `MatchRules::setup.team_start_positions.mode` is `team_adjacent`; `net_match_bind_rules` binds
  the rules and whether the map places neutral units (`src/app/netgame/runtime_net.cpp`).
- Tests: `ui-multiplayer-team-rules` (`test_start_positions`, `test_neutral_maps`) and
  `net-match-rules` (`start_positions_keep_team_mates_together`,
  `start_positions_put_computers_last_on_neutral_maps`).
- Limits: the hack applies to network games only; a skirmish keeps 3.1c's start positions. The
  engine takes no start order from outside the game, so the teams and alliances in the battle room
  are the first source of the order.

## Full configuration schema

<!-- BEGIN GENERATED: schema -->
Hack id `setup.team-start-positions`, written under the profile's `hacks` block.

| Write | Means |
| --- | --- |
| `setup.team-start-positions: true` | On, every parameter at its default. |
| `setup.team-start-positions: {mode: team-adjacent}` | On, the parameters named set and the rest at their defaults. |
| `setup.team-start-positions: {preset: baseline}` | On, starting from a preset; parameters named beside `preset` replace its values. |
| `setup.team-start-positions: team-adjacent` | On, the shorthand: a bare value sets `mode`. |
| `setup.team-start-positions: false` | Off, the same as leaving it out: 3.1c behaviour. |

### Parameters

| Parameter | Type | Unit | Allowed | Length | Baseline (3.1c) | Default | Adjustable | Scope | Overridden by |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `mode` | `enum` | - | `base`, `team-adjacent` | - | `base` | `team-adjacent` | `fixed` | `sim` | - |

Adjustable: `fixed`: only the profile sets it.

### Presets

| Preset | Values |
| --- | --- |
| `baseline` | `{mode: base}` |

`baseline` sets every parameter to its 3.1c value: the hack is on and plays as 3.1c.

### Shorthand

The hack has one parameter, so a bare value sets `mode`: `setup.team-start-positions: team-adjacent` is `setup.team-start-positions: {mode: team-adjacent}`.

### Every parameter at its default

```yaml
hacks:
  setup.team-start-positions:
    mode: team-adjacent
```
<!-- END GENERATED: schema -->
