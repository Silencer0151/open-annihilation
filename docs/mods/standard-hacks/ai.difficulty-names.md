# Difficulty Names

<!-- BEGIN GENERATED: facts -->
| Fact | Value |
| --- | --- |
| Hack id | `ai.difficulty-names` |
| Area | AI (`ai`) |
| Scope | sim: part of the profile hash; every machine must agree |
| Runs on | The machine that owns the unit or player concerned, and each machine, for its own view. |
| Parameters | 1 |
| Status | implemented |
<!-- END GENERATED: facts -->

## Description

The three difficulties carry different names. By default Easy and Hard swap places: the game's first difficulty, which 3.1c calls Easy, is called Hard, and the third is called Easy. Every menu label, the computer player's plan selection, its report and the campaign mission's schema choice follow the new names.

## Configuration example

```yaml
hacks:
  ai.difficulty-names: true   # difficulty 0 is Hard, 1 is Medium, 2 is Easy
```

```yaml
hacks:
  ai.difficulty-names: [easy, hard, medium]   # difficulty 1 takes the Hard names, difficulty 2 the Medium names
```

## Details

### Behaviour

The game keeps its difficulty as a number: 0, 1 or 2. 3.1c names each number from fixed tables: the keywords of the computer player's `plan` lines (`easy`, `medium`, `hard`), the upper-case labels of the AI weight report (`EASY`, `MEDIUM`, `HARD`), the labels the menus show (`Easy`, `Medium`, `Hard`) and the `Type` a campaign mission's schema carries.

With the hack on, `names[i]` is the name difficulty `i` carries in every one of these tables. The list holds each name once.

- **Menus:** the skirmish setup's difficulty button, the in-game menu, the save and load dialogs and the single-player campaign menu show the name the difficulty carries.
- **AI plan lines:** a `plan` line of an AI script applies when it names the keyword the game's difficulty carries, or `any`. So with the default swap, lines under `plan hard` apply on difficulty 0. The console's AI plan directive follows the same rule.
- **AI weight report:** the report labels the difficulty with its upper-case name.
- **Campaign schemas:** a campaign mission picks the `Schema N` block whose `Type` is the name the difficulty carries. When no schema has it, the fallback keeps 3.1c's order by number (0 tries 0, 1, 2; 1 tries 1, 0, 2; 2 tries 2, 1, 0), and each number tries the name it carries. Skirmish and multiplayer schemas (`Network 1` to `Network 4`) are not affected.

The difficulty number itself does not change. Everything keyed on the number, such as the income scaling of [ai.income-multipliers](ai.income-multipliers.md) and the difficulty stored in a saved game, stays by number. A game saved under a profile shows the profile's names when it is loaded with the same profile.

### Baseline (3.1c)

Difficulty 0 is Easy, 1 is Medium and 2 is Hard in every table. The `baseline` preset, `[easy, medium, hard]`, plays exactly as 3.1c.

### Network play

The machine that hosts a computer player chooses its plan lines by the keyword, and each machine shows the names in its own menus. Every machine must load the same list: the hack is part of the profile hash the machines compare.

### Interactions

- [ai.income-multipliers](ai.income-multipliers.md) indexes its lists by difficulty number, so a profile that swaps the names writes those lists in the swapped order.

### Implementation notes

- The rules record is `AiDifficultyNames` (`rules.ai.difficulty_names.names`) in `src/data/match-rules/include/oa/data/match_rules/records.inc`. `difficulty_name_index` in `src/data/match-rules/include/oa/data/match_rules/difficulty_names.hpp` turns a difficulty number into the position of its name in 3.1c's tables.
- Callers: `apply_script` in `src/sim/ai/src/computer_knowledge.cpp` (plan lines), `src/sim/ai/src/computer_report.cpp` (weight report), `ai_plan` in `src/ui/console/src/commands.cpp`, `src/ui/frontend-state/src/skirmish_ui.cpp`, `src/ui/frontend/ingame_menu.cpp`, `src/ui/frontend/savegame_dialogs.cpp`, `src/ui/campaign/single_player.cpp` and `find_matching_schema` in `src/data/campaign/src/campaign_file.cpp`.
- The application hands the profile's record to the menus and the campaign through `Runtime::difficulty_names` in `src/app/runtime_skirmish_host.cpp`.
- Tests: ctest `ai-computer-player` (`test_difficulty_names`), `campaign-file`, `ui-console`, `frontend-skirmish-ui`, `ui-frontend-options` (in-game menu and save dialog cases), `data-mod-profile` and `match-computer-build` (`computer_rules_through_the_match`).

## Full configuration schema

<!-- BEGIN GENERATED: schema -->
Hack id `ai.difficulty-names`, written under the profile's `hacks` block.

| Write | Means |
| --- | --- |
| `ai.difficulty-names: true` | On, every parameter at its default. |
| `ai.difficulty-names: {names: [hard, medium, easy]}` | On, the parameters named set and the rest at their defaults. |
| `ai.difficulty-names: {preset: baseline}` | On, starting from a preset; parameters named beside `preset` replace its values. |
| `ai.difficulty-names: [hard, medium, easy]` | On, the shorthand: a bare value sets `names`. |
| `ai.difficulty-names: false` | Off, the same as leaving it out: 3.1c behaviour. |

### Parameters

| Parameter | Type | Unit | Allowed | Length | Baseline (3.1c) | Default | Adjustable | Scope | Overridden by |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `names` | `list<enum>` | - | `easy`, `medium`, `hard`; no item twice | 3 | `[easy, medium, hard]` | `[hard, medium, easy]` | `fixed` | `sim` | - |

Adjustable: `fixed`: only the profile sets it.

### Presets

| Preset | Values |
| --- | --- |
| `baseline` | `{names: [easy, medium, hard]}` |

`baseline` sets every parameter to its 3.1c value: the hack is on and plays as 3.1c.

### Shorthand

The hack has one parameter, so a bare value sets `names`: `ai.difficulty-names: [hard, medium, easy]` is `ai.difficulty-names: {names: [hard, medium, easy]}`.

### Every parameter at its default

```yaml
hacks:
  ai.difficulty-names:
    names: [hard, medium, easy]
```
<!-- END GENERATED: schema -->
