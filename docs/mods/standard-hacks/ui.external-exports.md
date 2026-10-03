# External Exports

<!-- BEGIN GENERATED: facts -->
| Fact | Value |
| --- | --- |
| Hack id | `ui.external-exports` |
| Area | Interface (`ui`) |
| Scope | view: local display only; each player may differ |
| Runs on | Each machine, for its own view. |
| Parameters | 2 |
| Status | implemented |
<!-- END GENERATED: facts -->

## Description

Lets a profile ask for two outside integrations: a live export of unit state for external viewers, and an export of the battle room's game state for outside lobbies. Neither changes anything a player sees or plays; 3.1c publishes neither.

## Configuration example

```yaml
hacks:
  ui.external-exports: true
```

```yaml
hacks:
  ui.external-exports:
    live-export: true
    lobby-state-export: false   # export unit state only, not the battle room
```

## Details

The hack names two exports a mod may expect to exist alongside the game:

- `live-export`: the state of the units in a running match, published while
  the match runs so that a separate program can show the game as it happens.
- `lobby-state-export`: the battle room's game state (players, settings and
  readiness), published so that an outside lobby program can follow it.

Neither export feeds back into the game. Turning either on or off changes
no unit, order, picture or sound, so a profile may list the hack whatever
the player has installed beside the game.

3.1c publishes nothing for outside programs.

### In a network game

This is a view rule: it changes only what this machine shows and how its own input works, never what another machine is told. It is not part of the match hash, so players in one game may have it on, off or set differently without breaking the game.

### Implementation notes

- The engine reads the rules record `UiRules::external_exports` (fields
  `enabled`, `live_export`, `lobby_state_export`) and accepts the
  parameters, but publishes nothing: the profile resolves and validates,
  and every value plays exactly as 3.1c does.
- `src/app/README.md` (Display rules) records the rule.
- Test: `app-view-rules` (`src/app/view_rules_test.cpp`) checks that turning
  both exports on leaves the match's display rules, the music source and
  the display-mode floor the same as without the hack.
- Known limit: neither export is implemented. A program that waits for the
  live unit state or the lobby state receives nothing from this engine.

## Full configuration schema

<!-- BEGIN GENERATED: schema -->
Hack id `ui.external-exports`, written under the profile's `hacks` block.

| Write | Means |
| --- | --- |
| `ui.external-exports: true` | On, every parameter at its default. |
| `ui.external-exports: {live-export: true}` | On, the parameters named set and the rest at their defaults. |
| `ui.external-exports: {preset: baseline}` | On, starting from a preset; parameters named beside `preset` replace its values. |
| `ui.external-exports: false` | Off, the same as leaving it out: 3.1c behaviour. |

### Parameters

| Parameter | Type | Unit | Allowed | Length | Baseline (3.1c) | Default | Adjustable | Scope | Overridden by |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `live-export` | `bool` | - | `true`, `false` | - | `false` | `true` | `fixed` | `view` | - |
| `lobby-state-export` | `bool` | - | `true`, `false` | - | `false` | `true` | `fixed` | `view` | - |

Adjustable: `fixed`: only the profile sets it.

### Presets

| Preset | Values |
| --- | --- |
| `baseline` | `{live-export: false, lobby-state-export: false}` |

`baseline` sets every parameter to its 3.1c value: the hack is on and plays as 3.1c.

### Shorthand

This hack takes no shorthand: write `true`, `false` or a parameter map.

### Every parameter at its default

```yaml
hacks:
  ui.external-exports:
    live-export: true
    lobby-state-export: true
```
<!-- END GENERATED: schema -->
