# Take Waits for Commander Deaths

<!-- BEGIN GENERATED: facts -->
| Fact | Value |
| --- | --- |
| Hack id | `sharing.take-requires-live-commander` |
| Area | Sharing (`sharing`) |
| Scope | sim: part of the profile hash; every machine must agree |
| Runs on | The machine that owns the unit or player concerned. |
| Parameters | 0 |
| Status | implemented |
<!-- END GENERATED: facts -->

## Description

A `.take` or `.takecmd` chat line is held back while another player's commander lies destroyed but
not yet removed, and the player is told why. In 3.1c the line always goes out.

## Configuration example

```yaml
hacks:
  sharing.take-requires-live-commander: true
```

## Details

### Behaviour

When the local player sends a chat line that is exactly `.take` or `.takecmd` (any case; spaces and
tabs before it, and spaces, tabs and line ends after it, are allowed), the engine looks for another
player whose commander is destroyed:

- Only a game where losing the commander matters is checked.
- Every player slot in use other than the local player's is searched, lowest first, for a unit that
  is still in the world, belongs to the commander category and has health 0 or less.

When such a player is found, the line goes to no one, and the local player sees "Cannot take
*name*: their commander has been destroyed." Otherwise the line goes out as usual. The local
player's own destroyed commander does not count, nor does another player's destroyed unit of any
other category.

### In a network game

Only the machine of the player who types the line applies it. The hack is part of the profile hash,
so every machine agrees that it is on.

### Interactions

- [sharing.recorder-take-give](sharing.recorder-take-give.md): the commands this hack holds back.

### Implementation notes

- `Runtime::refuse_take_line` in `src/app/runtime_console.cpp` holds the line back while
  `MatchRules::sharing.take_requires_live_commander` is on.
- The tests it applies are in `src/ui/hud/src/share_panel.cpp`: `is_take_command`,
  `commander_destroyed_elsewhere` and `format_take_refusal`
  (`src/ui/hud/include/oa/ui/hud/share_panel.hpp`).
- Tests: `ui-hud-share-panel` (`test_take_guard`).

## Full configuration schema

<!-- BEGIN GENERATED: schema -->
Hack id `sharing.take-requires-live-commander`, written under the profile's `hacks` block.

| Write | Means |
| --- | --- |
| `sharing.take-requires-live-commander: true` | On. |
| `sharing.take-requires-live-commander: false` | Off, the same as leaving it out: 3.1c behaviour. |

### Parameters

This hack has no parameters.

### Presets

This hack has no presets.

### Shorthand

This hack has no parameters to set: write `true` or `false`.

### Every parameter at its default

```yaml
hacks:
  sharing.take-requires-live-commander: true
```
<!-- END GENERATED: schema -->
