# Several Local AI Players

<!-- BEGIN GENERATED: facts -->
| Fact | Value |
| --- | --- |
| Hack id | `setup.multiple-local-ai` |
| Area | Game Setup (`setup`) |
| Scope | sim: part of the profile hash; every machine must agree |
| Runs on | The host's machine. |
| Parameters | 0 |
| Status | implemented |
<!-- END GENERATED: facts -->

## Description

One machine can seat more than one computer player in the multiplayer battle
room. 3.1c allows each machine a single computer player.

## Configuration example

```yaml
hacks:
  setup.multiple-local-ai: true
```

The hack has no parameters.

## Details

### Behaviour

In the battle room, clicking a blocked slot's player control seats a
computer player there. In 3.1c that happens only while the machine has no
computer player yet; with the hack on it happens whatever the count. The
other conditions are unchanged: the game must not be closed, and the
commander option must not be deathmatch.

### Baseline

3.1c seats at most one computer player per machine.

### Network games

The check runs on the machine that adds the computer player. Other machines
take any number of computer players from a remote machine. The hack is part
of the profile hash, so every machine plays under the same profile.

### Interactions

- [setup.ai-player-name-format](setup.ai-player-name-format.md) gives each
  computer player a distinct name.
- [setup.allow-start-with-ai](setup.allow-start-with-ai.md) lets one human
  start against the computer players.
- [setup.map-scripted-units](setup.map-scripted-units.md) may seat a neutral
  computer player as the host presses START.

### Implementation notes

- `handle_panel_event` in `src/ui/frontend-multiplayer/src/battleroom.cpp`
  reads `setup.multiple_local_ai.enabled` before `lobby_add_computer`.
- Test: `ui-multiplayer-lobby-rules` (`test_several_computers`) seats two
  computer players with the hack on and one with it off.

## Full configuration schema

<!-- BEGIN GENERATED: schema -->
Hack id `setup.multiple-local-ai`, written under the profile's `hacks` block.

| Write | Means |
| --- | --- |
| `setup.multiple-local-ai: true` | On. |
| `setup.multiple-local-ai: false` | Off, the same as leaving it out: 3.1c behaviour. |

### Parameters

This hack has no parameters.

### Presets

This hack has no presets.

### Shorthand

This hack has no parameters to set: write `true` or `false`.

### Every parameter at its default

```yaml
hacks:
  setup.multiple-local-ai: true
```
<!-- END GENERATED: schema -->
