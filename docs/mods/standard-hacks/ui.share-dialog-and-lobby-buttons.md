# Share Dialog and Lobby Buttons

<!-- BEGIN GENERATED: facts -->
| Fact | Value |
| --- | --- |
| Hack id | `ui.share-dialog-and-lobby-buttons` |
| Area | Interface (`ui`) |
| Scope | view: local display only; each player may differ |
| Runs on | Each machine, for its own view. |
| Parameters | 1 |
| Status | implemented |
<!-- END GENERATED: facts -->

## Description

Extends the in-game share dialog with switches for sharing metal and energy, shooting at all and screen shake, a ready button and sliders for the share thresholds, each giving the game's own console commands; and adds host buttons to the battle room. 3.1c's dialog has none of these.

## Configuration example

```yaml
hacks:
  ui.share-dialog-and-lobby-buttons: true
```

```yaml
hacks:
  ui.share-dialog-and-lobby-buttons:
    lobby-buttons: [autopause]   # the battle room offers only AUTOPAUSE
```

## Details

#### Share dialog

Each control below works only when the share dialog's GUI file has a
gadget of that name; a profile's GUI decides which appear.

| Gadget | What it does |
| --- | --- |
| `EN_SHAREMETAL` | Shows whether the player shares metal; a click flips it and gives `+sharemetal`. |
| `EN_SHAREENERGY` | The same for energy, with `+shareenergy`. |
| `EN_SHOOTALL` | Shows the console's shoot-all flag; a click flips it and gives `+shootall`. |
| `EN_NOSHAKE` | Shows the console's no-shake flag; a click flips it and gives `+noshake`. |
| `EN_READY` | Says `.ready` to every player. |
| `SRL_SETSHRMETAL`, `SRL_SETSHAREGRY` | Sliders over the metal and energy storage, their knobs set at the player's share thresholds. |
| `SM#`, `SE#` | The threshold a moved knob stands for, shown while it differs from the player's. |
| `SRL_GAMESPEED` | Asks for its speed as the speed keys do. |

A command a switch gives is shown as a line on this machine and run on the
console, exactly as if typed. On OK, each slider whose value differs from
the player's threshold gives `+setsharemetal <n>` or
`+setshareenergy <n>`.

The sliders' arithmetic, in single precision:

- knob = truncate(threshold / whole storage x knob positions), 0 without
  storage;
- threshold = truncate(clamp(knob / last position, 0, 1) x whole storage).

#### Battle room buttons

`lobby-buttons` lists the buttons the battle room offers the host, each only
when the battle room's GUI file has a gadget of that name:

| Member | Gadget | A press says |
| --- | --- | --- |
| `autoteam` | `AUTOTEAM` | `+autoteam` |
| `autopause` | `AUTOPAUSE` | `.autopause` |
| `randomteam` | `RANDOMTEAM` | `+randomteam` |
| `crcreport` | `CRCREPORT` | `.crcreport` |

A press says its line as if the host had typed it in the battle room chat.
Other players see the buttons grayed, and their presses say nothing.

3.1c's share dialog has none of these controls and its battle room none of
these buttons.

### In a network game

This is a view rule: it changes only what this machine shows and how its own input works, never what another machine is told. It is not part of the match hash, so players in one game may have it on, off or set differently without breaking the game.

The commands the controls give are the game's own console and chat commands
and reach other machines as typed ones do; what they do there is decided by
the hacks that handle them, not by this one.

### Related hacks

- [teams.team-number-alliances](teams.team-number-alliances.md) gives
  `+autoteam` and `+randomteam` their effect.
- [network.recorder-session-commands](network.recorder-session-commands.md)
  handles `.ready` and `.autopause`.
- [ui.options-dialog](ui.options-dialog.md)'s chat macro gives share
  commands too.

### Implementation notes

- The engine reads `UiRules::share_dialog_and_lobby_buttons` (fields
  `enabled`, `lobby_buttons`).
- `src/app/runtime_team_panels.cpp` prepares the share dialog's switches and
  sliders (`prepare_share_dialog_extras`) and takes their clicks
  (`share_dialog_extras_click`); `src/app/view_rules.cpp` holds
  `share_threshold_knob`, `share_threshold_value` and `lobby_button_bits`.
- `src/ui/frontend-multiplayer/src/battleroom.cpp` binds the battle room
  buttons (`multiplayer_bind_lobby_buttons`, the `lobby_button` bits in
  `src/ui/frontend-multiplayer/include/oa/ui/frontend_multiplayer/lobby.hpp`).
- Tests: `app-view-rules` (`share_sliders`) and `ui-multiplayer-lobby`
  (`test_mod_lobby_buttons`: the host's presses, a client's grayed buttons).

## Full configuration schema

<!-- BEGIN GENERATED: schema -->
Hack id `ui.share-dialog-and-lobby-buttons`, written under the profile's `hacks` block.

| Write | Means |
| --- | --- |
| `ui.share-dialog-and-lobby-buttons: true` | On, every parameter at its default. |
| `ui.share-dialog-and-lobby-buttons: {lobby-buttons: [autoteam, autopause]}` | On, the parameters named set and the rest at their defaults. |
| `ui.share-dialog-and-lobby-buttons: false` | Off, the same as leaving it out: 3.1c behaviour. |

### Parameters

| Parameter | Type | Unit | Allowed | Length | Baseline (3.1c) | Default | Adjustable | Scope | Overridden by |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `lobby-buttons` | `set<enum>` | - | `autoteam`, `autopause`, `randomteam`, `crcreport`; each at most once | any | `[]` | `[autoteam, autopause]` | `fixed` | `view` | - |

Adjustable: `fixed`: only the profile sets it.

### Presets

This hack has no presets.

### Shorthand

This hack takes no shorthand: write `true`, `false` or a parameter map.

### Every parameter at its default

```yaml
hacks:
  ui.share-dialog-and-lobby-buttons:
    lobby-buttons: [autoteam, autopause]
```
<!-- END GENERATED: schema -->
