# Languages

Open Annihilation shows the game in the language the player chooses,
through the game's own translations: the ones Total Annihilation 3.1c's
data holds for German, French, Italian and Spanish, and any a mod's data
holds. English is the game data's own language.

## How the language is chosen

The first of these that says decides, once at start and again each time
the setting changes:

1. **3.1c's command line.** A word on its own, as `open-annihilation german`
   writes it, names the language the game data knows it by, as 3.1c reads
   it. The setting then shows "Set on the command line" for the run.
2. **The setting.** Language, the first control of Language & Text in the
   OA settings, offers System default and each language the game knows,
   named in itself: English, Deutsch, Español, Français, Italiano. It is
   kept as `open-annihilation.language`: `system`, or the language's
   BCP-47 tag (`de`).
3. **The operating system.** System default takes the first of the user's
   preferred languages, in their order, that the game knows: the preferred
   languages on macOS, the user's interface languages on Windows (the
   user's locale on Windows XP), and `LANGUAGE`, `LC_ALL`, `LC_MESSAGES` or
   `LANG` on Linux. A region does not matter: `de-AT` and `de-CH` choose
   German. When none is known, English.

A preferences file named with `--preferences-file` starts in English, the
game's own default, so that a check plays the same on every machine. 3.1c
takes its language from the command line or the registry and never asks
the operating system; [VARIANCES.md](../VARIANCES.md) says so.

## What is shown in it

Everything comes from the game data, read as 3.1c reads it for its
language; whatever the data leaves untranslated shows in English.

| What | Where the game data holds it |
|---|---|
| Units' names and descriptions: the build menu's buttons and the bottom bar, the unit panel, the F1 panel, the unit restrictions list, the units' spoken lines in the message log | each unit file's `GermanName` and `GermanDescription` (`FrenchName`, …), else `Name` and `Description` |
| Menus, dialogs, message boxes, panels' texts, the kill board, the loading screen, the F1 panel's labels, the units' spoken words, the message log's own phrases, features' descriptions, a chosen map's name and its description | `gamedata\translate.tdf`: a section names the English text, and its key for the language holds the translation |
| Campaign missions' names, briefings, hints and narration | a mission's `<Language>missionname` (also `brief`, `narration`, `missionhint`), and the `camps\briefs-<Language>` and `camps\hints-<Language>` folders |
| Pictures with words drawn in them | `bitmaps-<Language>`, as the battle room's `battleroom.pcx`; `unitpics-<Language>` |
| Fonts | `fonts-<Language>` |

What 3.1c's data holds:

| | German | French | Italian | Spanish |
|---|---|---|---|---|
| `translate.tdf` (3.1c's patch) | 1,028 texts | 1,027 | 1,024 | 1,028 |
| Units' names, of 278 | 277 | 277 | 276 | 276 |
| Campaign missions named, Arm and Core | all 50 | all 50 | all 50 | all 50 |
| Core Contingency missions named | all 25 | all 25 | all 25 | none |
| Briefings | 18 | 16 | 25 | 25 |
| Battle room picture | yes | yes | yes | yes |

A new language shows at once in what is drawn each frame: the bottom bar,
the unit panel, the build menu's lines, the kill board, the loading screen
and the lines the message log posts from then on. Screens, menus and panels
that are open keep their texts until they open again, and a campaign's
lists until they are filled again.

The language changes only what players read. The simulation, a saved game
and what a shared game sends are the same in every language: a unit
keeps its own name in its definition and in saves, a mission is saved under
its own name, and the rules (units, weapons, maps, missions, the computer
players' scripts) are read from their own folders, never from a language's,
though 3.1c looks there too. Players in different languages play together.

## Mods

A mod's `translate.tdf` takes the place of the game's, as 3.1c reads the
first copy the archives hold, and its units' language keys are read like
the game's. A mod profile's `strings` replace English texts; they are not
shown yet.

## The engine's own words

The OA settings dialog and the engine's own notices are written in English
and pass through the interface catalogue (`oa/data/languages/interface_text.hpp`):
TDF files in a `languages` folder beside the game's `fonts` folder, each
section naming an English text and each key a language's tag:

```
[Mouse wheel zoom]
	{
	de = ...;
	fr = ...;
	}
```

No translation ships yet. These words would need one: the dialog's
section names and headings, its labels, hints, values (`per player`,
`fps`, `Desktop`, `None`, `cells`), switch and level captions, lock texts,
Hardware acceleration's status lines, the footer's buttons, `System default`
and the header's title; the names of the standard hacks and their areas
in Developer Mode's list (`Deterministic Wind`, `Interface`: each hack's
and area's title in the mod registry), which the list sorts as the
language shown writes them; and the notices about missing skirmish and
multiplayer maps, the end of the game's missions, the graphics card and
driver, and settings that were not saved.

## Adding a language

A language is one entry in `src/data/languages/src/registry.inc`: its tag,
its name in itself, its name in English, the word the game data knows it
by, the system locales that choose it, the languages it falls back to and
what drawing it needs. Nothing else changes in the code: the setting
offers it, the operating system's locale chooses it, and every lookup
above reads it, from game data that holds its text under that word and
from catalogue files that hold the engine's words under its tag.

A language whose letters are all in the game's 8-bit code page
(Windows-1252), as Portuguese's or Dutch's are, needs nothing more. Others
need drawing the game has in part or not yet:

- **Simplified Chinese** draws in the bundled Noto Sans CJK SC face, whose
  cut holds GB 2312, with its game data in UTF-8 (a mod profile's
  `ui.text-rendering` unicode).
- **Traditional Chinese and Japanese** need the Noto Sans CJK TC and JP
  faces: the SC face draws their characters in Chinese forms, and the cut
  keeps only Big5's common characters and JIS X 0208.
- **Korean** needs the whole face: the cut keeps KS X 1001's 2,350 Hangul
  syllables of 11,172.
- **Hindi** needs a Devanagari face and complex text shaping (HarfBuzz),
  since its letters join and change order; the FreeType-only drawing lays
  characters side by side.

The registry records these needs (`TextNeeds`), and the setting offers only
the languages this build draws.
