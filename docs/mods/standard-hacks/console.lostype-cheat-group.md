# LOS Type Cheat

<!-- BEGIN GENERATED: facts -->
| Fact | Value |
| --- | --- |
| Hack id | `console.lostype-cheat-group` |
| Area | Console (`console`) |
| Scope | sim: part of the profile hash; every machine must agree |
| Runs on | Every machine in the game. |
| Parameters | 0 |
| Status | implemented |
<!-- END GENERATED: facts -->

## Description

The `+LOSType` console command becomes a cheat. It runs only where cheats are allowed, and using it is announced to every player.

## Configuration example

```yaml
hacks:
  console.lostype-cheat-group: true
```

## Details

### Behaviour

`+LOSType` switches how line of sight is drawn. With the hack on it is registered in the cheat command group instead of the option group:

- it runs only when the caller may run cheats: with cheats enabled in the game, or with the developer passphrase entered;
- typed as a chat `+` line, its echo goes to every player, as every cheat's does.

### 3.1c behaviour

In 3.1c `+LOSType` is an option command: anyone can run it at any time, and its chat echo is shown to the local player only.

### Network games

Every machine registers the command the same way, so every machine must agree that the hack is on; it is part of the profile hash.

### Interactions

- [console.ai-control-cheat-group](console.ai-control-cheat-group.md) moves other console commands into the cheat group in the same way.

### Implementation notes

- Applied in `console_apply_rules` in `src/ui/console/src/commands.cpp`, which re-registers `LOSType` with the cheat class (`command_class::cheat`) instead of the option class. It reads the rules record field `rules.console.lostype_cheat_group.enabled`. The app calls it when a match starts (`src/app/runtime_console.cpp`).
- Tests: `ui-console` (`src/ui/console/tests/console_test.cpp`, `test_mod_console_rules`: the command refuses the option mask, runs with the cheat mask and echoes to everyone).

## Full configuration schema

<!-- BEGIN GENERATED: schema -->
Hack id `console.lostype-cheat-group`, written under the profile's `hacks` block.

| Write | Means |
| --- | --- |
| `console.lostype-cheat-group: true` | On. |
| `console.lostype-cheat-group: false` | Off, the same as leaving it out: 3.1c behaviour. |

### Parameters

This hack has no parameters.

### Presets

This hack has no presets.

### Shorthand

This hack has no parameters to set: write `true` or `false`.

### Every parameter at its default

```yaml
hacks:
  console.lostype-cheat-group: true
```
<!-- END GENERATED: schema -->
