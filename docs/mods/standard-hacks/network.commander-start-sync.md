# Commander Start Sync

<!-- BEGIN GENERATED: facts -->
| Fact | Value |
| --- | --- |
| Hack id | `network.commander-start-sync` |
| Area | Network (`network`) |
| Scope | sim: part of the profile hash; every machine must agree |
| Runs on | Every machine in the game. |
| Parameters | 1 |
| Status | implemented |
<!-- END GENERATED: facts -->

## Description

Early in a network game each machine sends the start positions of its players' first units, and the other machines move their copy of each player's commander there. This keeps the commander from appearing in different places on different machines at the start.

## Configuration example

```yaml
hacks:
  network.commander-start-sync: true
```

```yaml
hacks:
  # Send the start positions at tick 60 instead of tick 90.
  network.commander-start-sync: 60
```

## Details

### Behaviour

**Sending.** At the tick set by `tick`, as it sends its regular unit state for each local player, a machine also sends one start-position record for each of that player's first units that is live and moves on the ground. It looks at the first min(90, units owned, unit limit) unit slots. Watchers and rejected players send nothing.

Each record is an ordinary unit state record (`0x2c`) holding one entry:

- the entry is always for unit index 0, whatever unit it describes;
- it holds the unit's type and a ground move with two points: the whole parts of the unit's x and z, then the first point of its path (or the position again when it has no path);
- the list terminator follows, with no full unit record.

**Receiving.** While the game's tick is below the unit limit, every unit state record from a remote, non-watching player whose first entry is for unit index 0 and holds a move of two or more points sets the sender's first unit's position: the whole parts of x and z are replaced and the fractions kept. Because every start-position record names unit index 0, a player with several such units sends several records and the receiver keeps the last. The record is then decoded as an ordinary unit state record as well.

`tick: 0` turns the sync off, which is what the `baseline` preset does.

### 3.1c behaviour

3.1c sends no start positions. Each machine places a remote commander from its own unit records, so a commander that moves before the first records arrive can stand in a different place on different machines.

### Network games

Every machine sends for its own players and applies the records of the others. All machines must use the same `tick`; it is part of the profile hash.

### Interactions

- [setup.commander-warp](setup.commander-warp.md) lets each human player move their commander while the game is held at its start; the start-position records this hack sends once the game runs carry the new position to every machine.

### Implementation notes

- The record is written by `unit_state_write_start_position` and read by `unit_state_start_position` (`src/netgame/include/oa/netgame/unit_state.hpp`).
- Sending and receiving are `send_commander_sync` and `apply_commander_sync` in `src/netgame/match/src/net_match.cpp`, called from `net_match_send_player_state` and `dispatch_record`.
- The profile's `network.commander_start_sync.tick` becomes `WireRules::commander_sync_tick` in `src/app/netgame/wire_rules_binding.cpp`.
- Tests: `net-match` (`src/netgame/match/tests/net_match_test.cpp`, `commander_start_sync_places_the_commander`) and `netgame-wire-rules` (`src/app/netgame/tests/wire_rules_test.cpp`, `every_rule_maps`). The network loopback check in `src/app/netgame/runtime_net.cpp` confirms a host sends start-position records when the hack is on.

## Full configuration schema

<!-- BEGIN GENERATED: schema -->
Hack id `network.commander-start-sync`, written under the profile's `hacks` block.

| Write | Means |
| --- | --- |
| `network.commander-start-sync: true` | On, every parameter at its default. |
| `network.commander-start-sync: {tick: 90}` | On, the parameters named set and the rest at their defaults. |
| `network.commander-start-sync: {preset: baseline}` | On, starting from a preset; parameters named beside `preset` replace its values. |
| `network.commander-start-sync: 90` | On, the shorthand: a bare value sets `tick`. |
| `network.commander-start-sync: false` | Off, the same as leaving it out: 3.1c behaviour. |

### Parameters

| Parameter | Type | Unit | Allowed | Length | Baseline (3.1c) | Default | Adjustable | Scope | Overridden by |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `tick` | `int` | ticks | `0` to `30000` | - | `0` | `90` | `fixed` | `sim` | - |

Adjustable: `fixed`: only the profile sets it.

### Presets

| Preset | Values |
| --- | --- |
| `baseline` | `{tick: 0}` |

`baseline` sets every parameter to its 3.1c value: the hack is on and plays as 3.1c.

### Shorthand

The hack has one parameter, so a bare value sets `tick`: `network.commander-start-sync: 90` is `network.commander-start-sync: {tick: 90}`.

### Every parameter at its default

```yaml
hacks:
  network.commander-start-sync:
    tick: 90
```
<!-- END GENERATED: schema -->
