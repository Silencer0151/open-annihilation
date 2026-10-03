# Take and Give Commands

<!-- BEGIN GENERATED: facts -->
| Fact | Value |
| --- | --- |
| Hack id | `sharing.recorder-take-give` |
| Area | Sharing (`sharing`) |
| Scope | sim: part of the profile hash; every machine must agree |
| Runs on | Every machine in the game. |
| Parameters | 1 |
| Status | implemented |
<!-- END GENERATED: facts -->

## Description

Adds chat commands with which a player lets named players take over their units: `.give <names>`
grants it and `.stopgive <names>` withdraws it, and allying with a player grants it as well. A
granted player who types `.take` or `.takecmd` takes over the units of a granting player whose
machine has sent nothing for more than 30 seconds. 3.1c has no such commands.

## Configuration example

```yaml
hacks:
  sharing.recorder-take-give: true
```

The same, with the one parameter written out:

```yaml
hacks:
  sharing.recorder-take-give:
    available: true   # the .give, .stopgive, .take and .takecmd commands work
```

The commands need the game recorder:

```yaml
hacks:
  recorder.ta-demo-recorder: true
  sharing.recorder-take-give: true
```

## Details

### Behaviour

The commands are chat lines typed in the game. The recorder on every machine reads each line it
sends or receives, matching the command word without regard to case.

**Granting.**

- `.give <names>`: the player who types it lets each player named take over their units. A name is
  a whole word after the command, matched without regard to case. The named player's machine
  answers every player with "*name* ready to take units from *giver*", and when the giver already
  allies that player it adds "You don't really need to type .give. Allying is enough."
- `.stopgive <names>`: withdraws the grant from each player named, whose machine answers
  "*name* barred from taking units from *giver*".
- Allying with a player grants it as well, and ending the alliance withdraws it.
- A grant also ends when the granting player's machine sends a reject record of any player, unless
  this machine is holding another player's take claim at that moment.
- Each grant is kept by the machine of the player granted.

**Taking.**

- `.take` or `.takecmd` claims the first player, in slot order, who granted the typing player the
  right and from whose machine nothing has arrived for more than 30 seconds, provided the typing
  player's machine holds no claim already. The taker's machine answers every player with
  "*taker* taking *name*s units", and the grant is used up.
- The taker's machine then hands the silent player's units over, walking that player's block of
  unit slots one place at a time each time it reads the network, and going straight on while
  places hand units over. `.take` starts after the commander; `.takecmd`, or a taker who has no
  units, starts at the commander. The block's last two places are not taken.
- A unit is handed over only when the last full unit record from the silent player's machine
  showed it finished, and it takes the health that record gave. A unit no full record has shown
  finished stays where it is.
- Each unit is handed over as if the silent player's machine had given it away: the taker's
  machine creates it again for the taker, finished, where it stands, and it reaches the other
  machines as the taker's new unit.
- At the end of the walk the taker's machine sends every player a reject of the silent player,
  with a lost connection as the reason, and each machine that receives it drops that player from
  the game.

**Claims from other machines.**

- A `.take` or `.takecmd` from a player on another machine is a claim this machine notes, in place
  of any claim it held. When its own player was taking, it stops and says "*name* aborting take
  claim"; that take does not resume, and a later take starts afresh.
- The next reject record this machine receives releases the claim, and "*claimant* take claim
  released" shows on this machine.
- When the claimant's recorder is older than protocol 3, "Sending killing packets" shows at that
  reject instead. When the claimant's machine runs no recorder at all, this machine also destroys
  then whatever stands in each place of the claimant's block, and in the first place after it,
  where it has seen no unit: it sends a damage record for each to every player and applies it
  itself. A claim from a machine with a recorder destroys nothing, even when it replaced a claim
  that would have.

With `available: false`, or without the hack or the recorder, the commands are ordinary chat, as
in 3.1c.

### Baseline (3.1c)

3.1c has no such commands.

### Network games

Every machine reads the commands. The machine of the player granted keeps the grant, the taker's
machine alone carries out the take, and the other machines note the claim. Every machine must
agree that the commands are available; the hack is part of the profile hash.

### Interactions

- [The game recorder](recorder.ta-demo-recorder.md) reads the commands; without it there are none.
- [sharing.take-requires-live-commander](sharing.take-requires-live-commander.md) holds back a
  `.take` or `.takecmd` line while another player's commander lies destroyed. A line held back
  starts no take and makes no claim.
- [network.recorder-session-commands](network.recorder-session-commands.md): the other opt-in
  commands the recorder reads.

### Implementation notes

- The rule is `MatchRules::sharing.recorder_take_give` (`enabled`, `available`), read through
  `NetMatch::match_rules`; the commands need the recorder's protocol
  (`WireRules::recorder_protocol`) as well.
- `parse_recorder_command` in
  [src/netgame/wire/recorder_session.cpp](../../../src/netgame/wire/recorder_session.cpp)
  recognises `.give`, `.stopgive`, `.take` and `.takecmd` and keeps the words after them, and
  `recorder_arguments_name` matches a name among them. `RecorderTake` and `RecorderTakeState`
  ([src/netgame/include/oa/netgame/recorder_session.hpp](../../../src/netgame/include/oa/netgame/recorder_session.hpp))
  hold the grants, the claim and the walk.
- In [src/netgame/match/src/net_match.cpp](../../../src/netgame/match/src/net_match.cpp),
  `recorder_grant` carries out `.give` and `.stopgive`, `recorder_note_alliance` grants on an
  alliance, `recorder_take` claims or notes a claim, `take_step` walks the block (called by
  `net_match_pump` before each record it reads), and `recorder_heard_reject` releases a claim. Each
  hand-over is a unit transfer record applied with `replication_apply_record` as if received from
  the silent player's machine.
- `net_match_note_full_record` keeps each unit as the last full record showed it
  (`RecorderUnitView` in
  [src/netgame/match/include/oa/netgame/match/net_match.hpp](../../../src/netgame/match/include/oa/netgame/match/net_match.hpp));
  the replication module hands it every full record it reads through
  `ReplicationSim::full_record_read`
  ([src/netgame/replication/replication.cpp](../../../src/netgame/replication/replication.cpp)),
  which the match binding
  ([src/netgame/match/src/match_binding.cpp](../../../src/netgame/match/src/match_binding.cpp))
  installs.
- Tests:
  - `net-wire-rules` (`recorder_take_and_base_commands_parse` in
    `src/netgame/tests/wire_rules_test.cpp`): names matched as whole words without regard to case,
    and `.stopgive`, `.take` and `.takecmd` read.
  - `net-match` (`recorder_take_hands_a_silent_players_units_over` in
    `src/netgame/match/tests/net_match_test.cpp`): `.give`, `.stopgive` and an alliance granting,
    no take before 30 seconds of silence, the walk handing over only finished units with their
    health, the closing reject, `.takecmd` starting at the commander, another player's claim
    aborting this machine's take for good and released by the next reject, the slots killed for a
    claimant with no recorder, nothing killed for a protocol 2 claimant that replaced one, and
    nothing without the rule.

## Full configuration schema

<!-- BEGIN GENERATED: schema -->
Hack id `sharing.recorder-take-give`, written under the profile's `hacks` block.

| Write | Means |
| --- | --- |
| `sharing.recorder-take-give: true` | On, every parameter at its default. |
| `sharing.recorder-take-give: {available: true}` | On, the parameters named set and the rest at their defaults. |
| `sharing.recorder-take-give: {preset: baseline}` | On, starting from a preset; parameters named beside `preset` replace its values. |
| `sharing.recorder-take-give: false` | Off, the same as leaving it out: 3.1c behaviour. |

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
  sharing.recorder-take-give:
    available: true
```
<!-- END GENERATED: schema -->
