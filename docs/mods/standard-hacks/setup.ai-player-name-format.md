# AI Player Names

<!-- BEGIN GENERATED: facts -->
| Fact | Value |
| --- | --- |
| Hack id | `setup.ai-player-name-format` |
| Area | Game Setup (`setup`) |
| Scope | sim: part of the profile hash; every machine must agree |
| Runs on | The machine that owns the unit or player concerned. |
| Parameters | 1 |
| Status | implemented |
<!-- END GENERATED: facts -->

## Description

Computer players added in the multiplayer battle room are named from a format
that can include their slot, such as `AI:Steve 3`, instead of 3.1c's
`AI:<local player name>`. Several computer players from one machine can then
be told apart.

## Configuration example

Switch it on with the default format, `AI:%s %d`:

```yaml
hacks:
  setup.ai-player-name-format: true
```

Use a format of your own:

```yaml
hacks:
  setup.ai-player-name-format: "Bot %s-%d"   # Steve's computer player in slot 2 is "Bot Steve-2"
```

## Details

### Behaviour

When a machine seats a computer player in the battle room, it names the
player from `format`:

- `%s` is replaced by the local player's name. The format holds exactly one.
- `%d`, when present, is replaced by the computer player's battle-room slot,
  0 to 9. It may appear once, after `%s`.
- Any other text is copied as written; the format holds no other `%`.
- The result is cut to 16 characters. A long player name loses its end, and
  the slot number with it: `AVeryLongPlayerName` gives `AI:AVeryLongPlay`.

The format `AI:%s` names computer players exactly as 3.1c does.

### Baseline

3.1c names every computer player `AI:` followed by the local player's name,
cut to 16 characters, so two computer players from one machine share a name.

### Network games

The machine that seats the computer player names it; the name travels to the
other machines as the player's session name. The format is part of the
profile hash, so every machine uses the same one.

### Interactions

[setup.multiple-local-ai](setup.multiple-local-ai.md) lets one machine seat
several computer players, which is when the slot number matters.

### Implementation notes

- `lobby_add_computer` in `src/ui/frontend-multiplayer/src/battleroom.cpp`
  reads `setup.ai_player_name_format` and calls
  `team_rules::format_computer_name` in
  `src/ui/frontend-multiplayer/src/team_rules.cpp`.
- Tests: `ui-multiplayer-team-rules` (`test_computer_names`) checks the
  formatting and the cut; `ui-multiplayer-lobby-rules` (`test_computer_names`)
  checks the names seated in the battle room, with the hack off and on;
  `data-mod-profile` checks the shorthand.

## Full configuration schema

<!-- BEGIN GENERATED: schema -->
Hack id `setup.ai-player-name-format`, written under the profile's `hacks` block.

| Write | Means |
| --- | --- |
| `setup.ai-player-name-format: true` | On, every parameter at its default. |
| `setup.ai-player-name-format: {format: "AI:%s %d"}` | On, the parameters named set and the rest at their defaults. |
| `setup.ai-player-name-format: {preset: baseline}` | On, starting from a preset; parameters named beside `preset` replace its values. |
| `setup.ai-player-name-format: "AI:%s %d"` | On, the shorthand: a bare value sets `format`. |
| `setup.ai-player-name-format: false` | Off, the same as leaving it out: 3.1c behaviour. |

### Parameters

| Parameter | Type | Unit | Allowed | Length | Baseline (3.1c) | Default | Adjustable | Scope | Overridden by |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `format` | `string` | - | at most 32 characters; matching `^[^%]*%s[^%]*(%d[^%]*)?$` | - | `"AI:%s"` | `"AI:%s %d"` | `fixed` | `sim` | - |

Adjustable: `fixed`: only the profile sets it.

### Presets

| Preset | Values |
| --- | --- |
| `baseline` | `{format: "AI:%s"}` |

`baseline` sets every parameter to its 3.1c value: the hack is on and plays as 3.1c.

### Shorthand

The hack has one parameter, so a bare value sets `format`: `setup.ai-player-name-format: "AI:%s %d"` is `setup.ai-player-name-format: {format: "AI:%s %d"}`.

### Every parameter at its default

```yaml
hacks:
  setup.ai-player-name-format:
    format: "AI:%s %d"
```
<!-- END GENERATED: schema -->
