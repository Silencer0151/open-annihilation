# Vote to Remove Players

<!-- BEGIN GENERATED: facts -->
| Fact | Value |
| --- | --- |
| Hack id | `network.vote-reject` |
| Area | Network (`network`) |
| Scope | sim: part of the profile hash; every machine must agree |
| Runs on | Every machine in the game, and the host's machine. |
| Parameters | 2 |
| Status | implemented |
<!-- END GENERATED: facts -->

## Description

The host's dialog for dropping a player is replaced by a vote of every
machine. A player who stops answering, or one somebody asks to remove, is
removed when enough players vote yes. When the player has allies, one of
them must agree.

## Configuration example

```yaml
hacks:
  network.vote-reject: true
```

```yaml
hacks:
  network.vote-reject:
    vote-timeout-s: 30    # a vote someone asks for closes after 30 s
    timeout-vote-s: 120   # a vote on a silent player stays open for 2 minutes
```

## Details

### Behaviour

Every machine keeps the same board of open votes, at most one per
player. Every machine counts the votes it hears, and every machine
removes the player itself once a vote passes. Times use the connection's
clock, which counts 1/30 s.

A vote opens in one of two ways:

- **Manual:** a player uses the team panel's remove-player control. The
  proposal goes to every machine and counts as the proposer's yes. The
  vote stays open for `vote-timeout-s`.
- **Timeout:** a player stops answering past the game's timeout. Every
  machine opens a vote of its own on that player; the host does not
  decide alone. The vote stays open for `timeout-vote-s`. A machine never
  votes in a timeout vote on its own player. Hearing from the player
  again cancels the vote.

Players answer with the console command `Vote yes` or `Vote no`. A no
withdraws an earlier yes, and a yes withdraws an earlier no. With n
players in the game (slots in use with a transport id), the yes votes
needed are:

| Vote | Yes votes needed |
| --- | --- |
| manual | `max(1, (2n + 2) / 3)`, rounded down |
| timeout | 2, or 1 when only one player takes part |

A vote passes with the yes votes it needs. When the target has allies
(players allied with them both ways), at least one of those yes votes
must come from an ally. A vote fails as soon as enough players have voted
no that the rest cannot reach the number needed. When time runs out
undecided, a timeout vote passes and a manual vote fails. After a failed
manual vote, the target cannot face a new vote for 90 seconds.

The private channel carries the votes as sub-id `0x2C`:

| Byte | Value |
| --- | --- |
| op | `1` propose, `2` yes, `3` no |
| payload +0 | u32 transport id of the target |
| payload +4 | u8 flag: `1` manual, `6` timeout |

The rest of the payload is zero.

### Baseline (3.1c)

When a player stops answering, the host gets a dialog and alone decides
whether to drop them. A player whose silence runs past the timeout is
dropped. Removing a player from the team panel removes them at once.

### Network games

Every machine runs the board and removes players itself. All machines
must agree on the hack and both windows, which are part of the profile
hash. The two parameters have the same values as 3.1c's timeouts, so the
hack has no `baseline` preset.

### Interactions

- Needs [network.chat-extension-channel](network.chat-extension-channel.md)
  with `framing: sub-id-dispatch`. With any other framing, the engine
  keeps 3.1c's drop dialog.

### Implementation notes

- The profile's `network.vote_reject` becomes `WireRules::vote_reject`,
  `vote_seconds` and `timeout_vote_seconds` in
  `src/app/netgame/wire_rules_binding.cpp`.
- `src/netgame/match/src/vote_reject.cpp` (declared in
  `src/netgame/match/include/oa/netgame/match/vote_reject.hpp`) holds the
  board, the count and the cooldown.
- In `src/netgame/match/src/net_match.cpp`:
  - `propose_vote` and the vote message handler send and receive votes;
  - the timeout check opens timeout votes;
  - `net_match_remove_player` turns a removal into a proposal;
  - `net_match_cast_vote` takes the local player's vote.
- The `Vote` console command is in
  `src/netgame/console/src/console_commands.cpp`.
- Tests:
  - `vote_tally_rules`, `votes_reject_on_every_machine`,
    `silence_opens_a_timeout_vote` and
    `timeout_vote_leaves_out_its_target` in `net-match`
    (`src/netgame/match/tests/net_match_test.cpp`).
  - `every_rule_maps` and `rules_need_their_channel` in
    `netgame-wire-rules`.

## Full configuration schema

<!-- BEGIN GENERATED: schema -->
Hack id `network.vote-reject`, written under the profile's `hacks` block.

| Write | Means |
| --- | --- |
| `network.vote-reject: true` | On, every parameter at its default. |
| `network.vote-reject: {vote-timeout-s: 60}` | On, the parameters named set and the rest at their defaults. |
| `network.vote-reject: false` | Off, the same as leaving it out: 3.1c behaviour. |

### Parameters

| Parameter | Type | Unit | Allowed | Length | Baseline (3.1c) | Default | Adjustable | Scope | Overridden by |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `vote-timeout-s` | `int` | s | `1` to `3600` | - | `60` | `60` | `fixed` | `sim` | - |
| `timeout-vote-s` | `int` | s | `1` to `3600` | - | `90` | `90` | `fixed` | `sim` | - |

Adjustable: `fixed`: only the profile sets it.

### Presets

This hack has no presets.

### Shorthand

This hack takes no shorthand: write `true`, `false` or a parameter map.

### Every parameter at its default

```yaml
hacks:
  network.vote-reject:
    vote-timeout-s: 60
    timeout-vote-s: 90
```
<!-- END GENERATED: schema -->
