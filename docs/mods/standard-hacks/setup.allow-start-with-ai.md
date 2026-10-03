# Start with One Human

<!-- BEGIN GENERATED: facts -->
| Fact | Value |
| --- | --- |
| Hack id | `setup.allow-start-with-ai` |
| Area | Game Setup (`setup`) |
| Scope | sim: part of the profile hash; every machine must agree |
| Runs on | The host's machine. |
| Parameters | 0 |
| Status | implemented |
<!-- END GENERATED: facts -->

## Description

The host can start a multiplayer game with only one human player and
computer players. 3.1c refuses to start until at least one other human has
joined.

## Configuration example

```yaml
hacks:
  setup.allow-start-with-ai: true
```

The hack has no parameters.

## Details

### Behaviour

The battle room's start check first asks for a second participant:

- with the hack on, at least one local human player;
- in 3.1c, at least one remote player.

The rest of the check is unchanged: every local, computer and remote player
must be ready, and a game whose players are all watching does not start.
One human with one or more ready computer players can therefore start.

### Baseline

3.1c needs a remote player in the battle room before the start is allowed,
so a game of one human against computer players is only possible as a
skirmish.

### Network games

The host runs the start check. The hack is part of the profile hash, so every
machine in the battle room plays under the same profile.

### Interactions

[setup.multiple-local-ai](setup.multiple-local-ai.md) lets the one human seat
more than one computer player.

### Implementation notes

- `lobby_ready_to_start` in `src/ui/frontend-multiplayer/src/battleroom.cpp`
  reads `setup.allow_start_with_ai.enabled`.
- Test: `ui-multiplayer-lobby-rules` (`test_start_with_computers`) checks one
  human and a ready computer player, with the hack off and on.

## Full configuration schema

<!-- BEGIN GENERATED: schema -->
Hack id `setup.allow-start-with-ai`, written under the profile's `hacks` block.

| Write | Means |
| --- | --- |
| `setup.allow-start-with-ai: true` | On. |
| `setup.allow-start-with-ai: false` | Off, the same as leaving it out: 3.1c behaviour. |

### Parameters

This hack has no parameters.

### Presets

This hack has no presets.

### Shorthand

This hack has no parameters to set: write `true` or `false`.

### Every parameter at its default

```yaml
hacks:
  setup.allow-start-with-ai: true
```
<!-- END GENERATED: schema -->
