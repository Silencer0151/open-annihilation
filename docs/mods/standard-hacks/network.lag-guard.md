# Lag Guard

<!-- BEGIN GENERATED: facts -->
| Fact | Value |
| --- | --- |
| Hack id | `network.lag-guard` |
| Area | Network (`network`) |
| Scope | sim: part of the profile hash; every machine must agree |
| Runs on | Each machine, for its own view. |
| Parameters | 1 |
| Status | implemented |
<!-- END GENERATED: facts -->

## Description

While no other player in a network game has been heard from for a short
gap, the game steps at most once per gap and ignores the Pause key. A
notice reports when the gap starts and how long the freeze lasted.

## Configuration example

```yaml
hacks:
  network.lag-guard: true        # a 500 ms gap
```

```yaml
hacks:
  network.lag-guard: 1000        # wait a full second of silence before slowing the game
```

## Details

### Behaviour

Before each simulation step of a running network game, each machine
works out how long it has been since it last heard from any remote
player. Only in-game players count: remote players who are neither
rejected nor gone. The engine takes the most recent of those times. The
guard has two states:

- **Open:** steps run as usual. When every remote player has been silent
  for `gap-ms` or longer, the guard closes. The step that closes it still
  runs, and the player sees `Network gap detected - simulation paused`.
- **Closed:** a step runs only when `gap-ms` have passed since the last
  step the guard let through. Any other step is held: no tick runs. As
  soon as any remote player is heard again, the guard opens. That step
  runs, and the player sees `Network resumed after N ms freeze`, where N
  counts from when the guard closed.

While the guard is closed, the Pause key is ignored: the pause the key
has just toggled is undone on this machine and nothing goes to the other
players. The time is measured on the machine's wall clock, in
milliseconds. A `gap-ms` of 0 turns the guard off.

### Baseline (3.1c)

3.1c has no guard. The game's own wait for the other players' turns
still stalls the simulation when they fall silent, with or without the
hack. The guard only limits how far a machine runs ahead during the
silence and blocks pausing while it lasts.

### Network games

Each machine decides for itself, for its own view, from what it has
heard. No message carries the guard's state. The value is part of the
profile hash, so every machine must use the same `gap-ms`.

### Interactions

None with other hacks.

### Implementation notes

- The profile's `network.lag_guard.gap_ms` becomes
  `WireRules::lag_guard_ms` in `src/app/netgame/wire_rules_binding.cpp`.
- `lag_guard_step` in `src/base/game-loop/src/coordinator.cpp`
  (declared with `LagGuard` in
  `src/base/game-loop/include/oa/base/game_loop.hpp`) makes the decision.
- `NetworkPlay::net_simulation_step`, `net_remote_silence_ms` and
  `net_pause_key` in `src/app/netgame/runtime_net.cpp` apply it and show
  the notices.
- Tests:
  - `test_lag_guard` in `game-loop-coordinator`
    (`src/base/game-loop/tests/coordinator_test.cpp`) covers the
    decision: off at 0, closing, held, running once per gap and opening.
  - `every_rule_maps` in `netgame-wire-rules` checks the binding.

## Full configuration schema

<!-- BEGIN GENERATED: schema -->
Hack id `network.lag-guard`, written under the profile's `hacks` block.

| Write | Means |
| --- | --- |
| `network.lag-guard: true` | On, every parameter at its default. |
| `network.lag-guard: {gap-ms: 500}` | On, the parameters named set and the rest at their defaults. |
| `network.lag-guard: {preset: baseline}` | On, starting from a preset; parameters named beside `preset` replace its values. |
| `network.lag-guard: 500` | On, the shorthand: a bare value sets `gap-ms`. |
| `network.lag-guard: false` | Off, the same as leaving it out: 3.1c behaviour. |

### Parameters

| Parameter | Type | Unit | Allowed | Length | Baseline (3.1c) | Default | Adjustable | Scope | Overridden by |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `gap-ms` | `int` | ms | `0` to `60000` | - | `0` | `500` | `fixed` | `sim` | - |

Adjustable: `fixed`: only the profile sets it.

### Presets

| Preset | Values |
| --- | --- |
| `baseline` | `{gap-ms: 0}` |

`baseline` sets every parameter to its 3.1c value: the hack is on and plays as 3.1c.

### Shorthand

The hack has one parameter, so a bare value sets `gap-ms`: `network.lag-guard: 500` is `network.lag-guard: {gap-ms: 500}`.

### Every parameter at its default

```yaml
hacks:
  network.lag-guard:
    gap-ms: 500
```
<!-- END GENERATED: schema -->
