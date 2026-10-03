# AI and Control Cheats

<!-- BEGIN GENERATED: facts -->
| Fact | Value |
| --- | --- |
| Hack id | `console.ai-control-cheat-group` |
| Area | Console (`console`) |
| Scope | sim: part of the profile hash; every machine must agree |
| Runs on | Every machine in the game. |
| Parameters | 0 |
| Status | implemented |
<!-- END GENERATED: facts -->

## Description

The `+AI` and `+Control` console commands become cheats instead of developer commands, so they work in a multiplayer game whenever cheats are allowed. In 3.1c they need developer mode.

## Configuration example

```yaml
hacks:
  console.ai-control-cheat-group: true
```

## Details

### Behaviour

Every console command belongs to a class that decides when it runs:

| Class | Runs when |
| --- | --- |
| Option | Always |
| Cheat | Cheats are allowed: in a campaign or a skirmish, or when the multiplayer lobby's Cheats option is on |
| Developer | Developer mode is on |

With this hack on, `+AI [player]` (switch a player between local and computer control) and `+Control <player>` (make another player the local and viewed player) are cheats. With cheats allowed, a player can type them on the chat line; the line is echoed to every player and runs on every machine, as other cheats do. With cheats not allowed, they are refused like other cheats.

Every other command keeps its class.

### 3.1c baseline

`+AI` and `+Control` are developer commands. They run only in developer mode, and the multiplayer Cheats option does not enable them.

### Network games

Every machine applies the classes to its own console. A `+` line is sent to every player and each machine runs it with its own command table, so every machine must use the same setting; the hack is part of the profile hash.

### Interactions

- [`console.lostype-cheat-group`](console.lostype-cheat-group.md) moves `+LOSType` into the cheat class in the same way; the two are independent.

### Implementation notes

- Applied in `console_apply_rules` in `src/ui/console/src/commands.cpp` (module `src/ui/console`), which reads `MatchRules::console.ai_control_cheat_group.enabled` and sets the class of the `AI` and `Control` entries. The application calls it when it binds the console to a match; applying 3.1c's rules restores the developer class.
- Tested by `test_mod_console_rules` in `src/ui/console/tests/console_test.cpp` (ctest `ui-console`): the commands run under the cheat class only with the hack on, a chat line runs `+ai` only while cheats are allowed, and 3.1c's rules put the classes back.

## Full configuration schema

<!-- BEGIN GENERATED: schema -->
Hack id `console.ai-control-cheat-group`, written under the profile's `hacks` block.

| Write | Means |
| --- | --- |
| `console.ai-control-cheat-group: true` | On. |
| `console.ai-control-cheat-group: false` | Off, the same as leaving it out: 3.1c behaviour. |

### Parameters

This hack has no parameters.

### Presets

This hack has no presets.

### Shorthand

This hack has no parameters to set: write `true` or `false`.

### Every parameter at its default

```yaml
hacks:
  console.ai-control-cheat-group: true
```
<!-- END GENERATED: schema -->
