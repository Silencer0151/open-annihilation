# Languages

Open Annihilation shows the game in the language the player chooses,
through the game's own translations: the ones Total Annihilation 3.1c's
data holds for German, French, Italian and Spanish, and any a mod's data
holds. Language packs add to them, entry by entry, and bring Simplified
Chinese. English is the game data's own language.

## How the language is chosen

The first of these that says decides, once at start and again each time
the setting changes:

1. **3.1c's command line.** A word on its own, as `open-annihilation german`
   or `open-annihilation Chinese` writes it, names the language the game
   data knows it by, as 3.1c reads it. The setting then shows "Set on the
   command line" for the run.
2. **The setting.** Language, the first control of the Language section of
   the OA settings, offers System default and each language the game knows,
   named in itself: English, Deutsch, Español, Français, Italiano, 简体中文.
   It is kept as `open-annihilation.language`: `system`, or the language's
   BCP-47 tag (`de`).
3. **The operating system.** System default takes the first of the user's
   preferred languages, in their order, that the game knows: the preferred
   languages on macOS, the user's interface languages on Windows (the
   user's locale on Windows XP), and `LANGUAGE`, `LC_ALL`, `LC_MESSAGES` or
   `LANG` on Linux. A region does not matter: `de-AT` and `de-CH` choose
   German. `zh-Hans`, `zh-CN`, `zh-SG`, `zh-MY` and a bare `zh` choose
   Simplified Chinese; Traditional Chinese (`zh-Hant`, `zh-TW`, `zh-HK`,
   `zh-MO`) is not offered yet and chooses none, never Simplified. When
   none is known, English.

A preferences file named with `--preferences-file` starts in English, the
game's own default, so that a check plays the same on every machine. 3.1c
takes its language from the command line or the registry and never asks
the operating system; the engine differs there on purpose.

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
and the lines the message log posts from then on. The main menu or the
in-game menu under the settings shows it at once too, with the words over
the game's pictures, and so do the titles over the battlefield. The other
menus and panels show it as they open, and a campaign's lists as they are
filled again.

The language changes only what players read. The simulation, a saved game
and what a shared game sends are the same in every language: a unit
keeps its own name in its definition and in saves, a mission is saved under
its own name, and the rules (units, weapons, maps, missions, the computer
players' scripts) are read from their own folders, never from a language's,
though 3.1c looks there too. Players in different languages play together.

## Language packs

A language pack is a folder of texts for one language, named by its tag.
It never replaces the game data's translations; it adds to them, entry by
entry:

```
zh-Hans/
  language.yaml   the manifest
  translate.tdf   the game's own texts, keyed by 3.1c's English
  units.tdf       units' names and descriptions, by UnitName
  missions.tdf    missions' names, descriptions and hints, by mission file
  interface.tdf   the engine's own words, as the interface catalogue reads them
  pictures.tdf    captions drawn over the player's own pictures
  files/          whole files in the game data's language folders
```

Only the manifest is required. It is strict YAML, read as mod profiles are:

```yaml
oalang: 1
tag: zh-Hans
name: 简体中文
english-name: Chinese (Simplified)
word: Chinese
version: "1"
locales: [zh-Hans, zh-CN, zh-SG, zh-MY, zh]
fallbacks: []
text: {needs: modern-fonts}
unicode: true
```

`tag` names the language, `word` the word the game data knows it by
(`ChineseName`, `Chinese=…;`, `camps/briefs-Chinese`). In this version a
pack is used for a language the game knows; `unicode: true` turns Enable
Unicode Multiplayer Chat on while the language is shown
([Settings](settings.md#language)). A language whose text needs the
modern fonts turns it on too, with or without its pack, since its text
is UTF-8.

The tables are UTF-8 TDF files, each starting with its licence in `//`
comments. TDF has no escapes, so a value never holds `;`: write the
full-width `；`.

- **translate.tdf** is keyed by the English text exactly as 3.1c's
  `gamedata\translate.tdf` is, with the word as the key:
  `[Select Map] { Chinese=选择地图; }`. It covers the game's messages, the
  menus' captions, help lines, features' descriptions and maps' names and
  descriptions. An entry the pack leaves out shows as the game data has
  it; the Simplified Chinese pack leaves out the companies' names so.
  A button with stages, as `Easy|Medium|Hard`, is translated whole and
  then stage by stage, as 3.1c translates it; to give one button's stage
  a word of its own, translate its whole caption:
  `[Off|Medium|Full] { Chinese=关闭|中等|全部; }`.
- **units.tdf**: `[ARMCOM] { name=…; description=…; name-from=…;
  description-from=…; }`. `name-from` and `description-from` give the
  English the text translates; when the game data's English differs, as a
  mod that reuses a unit's name makes it, the text is skipped and the
  game data's own shows.
  [tools/language_pack_check.py](../tools/language_pack_check.py) reports
  such stale fields.
- **missions.tdf**: `[Lipar Pass.ota] { missionname=…; }`, by the
  mission's file as the campaign names it; the campaign's mission list
  shows the name.
- **interface.tdf**: the engine's own words, in the catalogue's shape
  below.
- **pictures.tdf**: captions drawn over the player's own pictures (Words
  in pictures, below). The file and each entry are optional: a picture
  with no caption, or a caption left out, is drawn as it is.
- **files/** holds whole files under the game data's language folders, as
  `files/camps/briefs-Chinese/<briefing>.txt`: the missions' briefings in
  the language, in UTF-8.

For a text in a language, the game tries, in order:

1. a mod's pack, `languages/<tag>/` in the mod;
2. the game data in the language's word, read as 3.1c reads it;
3. the player's pack, in `Languages/<tag>/` of their own folder, then the
   engine's, in the `languages` folder beside the game's `fonts`;
4. the same for each of the language's fallbacks, then English.

An absent or empty value falls through to the next, so the player never
sees a blank. Packs change only what players read: they are in no mod
profile's hash and change nothing a shared game sends, and 3.1c's German,
French, Italian and Spanish show exactly as 3.1c shows them.

`python3 tools/language_pack_check.py PACK --game-dir GAME --oa-tool OA_TOOL`
checks a pack: its manifest and tables read, and it tells which of the
game data's texts and units the pack misses and which `-from` fields are
stale. `--catalogue` compares `interface.tdf` with catalogue files of the
engine's words.

### Words in pictures

Some words are part of the game's pictures: the order buttons, the top
bar's METAL and ENERGY, the PAUSED, VICTORY! and DEFEAT titles and the
save and load dialogs' titles. A pack's `pictures.tdf` names such a picture and
gives its words, which the game draws over the player's own picture in
the bundled fonts as it loads, after painting out the old words. Each
section names a GAF sequence as `[<file>.gaf/<sequence>]` or a bitmap as
`[<file>.pcx]`; `text` gives the captions (`|` between frames or spots),
`area` their spots as `x,y,width,height` and `align` left, centre or
right. Leave a picture out, or give its section no keys, and it keeps its
art; leave out `pictures.tdf` and no picture changes. A picture the game
data holds in the language's own folder (`bitmaps-<word>`,
`anims-<word>`) already shows its words and is left as it is.

## Simplified Chinese

Simplified Chinese (`zh-Hans`, the game data's word `Chinese`) is drawn
only in the bundled modern fonts, so choosing it turns Use modern fonts for
game text on and keeps it on while it is shown; its text is UTF-8. Its
pack, in the engine's `languages/zh-Hans/`, asks for Unicode multiplayer
chat. A mission's briefing, a unit's name and the game's texts come from
the pack wherever the game data has none in Chinese. Names of companies
are left out of the pack, so they show as the game data writes them; the
game itself is named by its Chinese title, 横扫千军.

Chinese, Japanese and Korean text is drawn in the bundled Noto Sans CJK SC
Bold, cut to about 15,300 characters: GB 2312, the 8,105 characters of the
Table of General Standard Chinese Characters, the common characters of
Big5, JIS X 0208 and KS X 1001's Hangul, and the CJK punctuation and
full-width forms. While such a language is shown, ideographs are drawn at
12 px at the least, whatever the Text size, and the most common 360 hanzi
are drawn ahead when the language is chosen. Lines break at spaces and
between any two of those characters, never starting a row with a closing
mark, comma or full stop, and never ending one with an opening mark; a
Latin word or a number stays whole. Mission briefings in those languages
wrap the same way and are drawn in the modern fonts. A character the fonts
lack draws as a box.

Text is typed in Chinese through the operating system's input method;
[Typing](typing.md) says how, and how saves and recordings are named in
any script.

## Mods

A mod's `translate.tdf` takes the place of the game's, as 3.1c reads the
first copy the archives hold, and its units' language keys are read like
the game's. A mod may carry language packs in its own `languages/<tag>/`
folders, which come before the game data. A mod profile's `strings`
replace English texts; they are not shown yet.

## Unicode multiplayer chat

A chat line in a shared game is one 64-byte record of text, which 3.1c
reads in the game's 8-bit code page (Windows-1252). Enable Unicode
Multiplayer Chat, in the Language section of the OA settings
(`open-annihilation.unicode-chat`, off by default), sends and reads chat
in UTF-8 between the machines that have it on, so that players can chat
in any script, Chinese among them. A language that needs it, as Simplified
Chinese does, turns it on and holds it on while it is shown, and so does a
mod profile that sets
[ui.text-rendering](mods/standard-hacks/ui.text-rendering.md) `unicode`.

- **Saying so.** A machine with the setting on says so in the setup block
  it sends (record 0x20), in three bytes 3.1c carries unchanged and never
  reads: `U` and `8` at +0xB5 and +0xB6, and bit 0 of +0xB7.
- **Sending.** Each machine gets a line in one form: UTF-8 when its setup
  block says so, else the code page with `?` for each character the code
  page lacks. When every machine reads the same form the line goes once
  to all of them; when they differ, one copy goes to each machine. A
  record all in ASCII reads the same in both forms and goes once, so a
  line all in ASCII that fits one record goes as it would with the
  setting off. The line the ALLIES panel says in a game, "allied with"
  or "broke alliance with" and a name, is in the language shown, as
  3.1c says it, and goes in these forms too.
- **Long lines.** A line longer than one record goes as up to four
  records, each the speaker's `<Name> ` and a part of the line, cut
  between whole characters, at a space when one lies within the part's
  last 16 bytes. What does not fit in four is left out. The speaker's own
  log shows the same lines.
- **Commands.** A line whose text starts with `+` or `.` is a command,
  which the game and the recorders read: it goes as one record, as typed.
- **Reading.** A line from a machine whose setup block says UTF-8 is read
  strictly as UTF-8: each byte that starts no well-formed sequence (an
  overlong form, a surrogate, a value past U+10FFFF, a sequence cut short
  or a stray continuation byte) shows as `?`. A line from any other
  machine is read in the code page, or, while the setting is on, as UTF-8
  where it is well-formed UTF-8.
- **Recordings.** A recording keeps one copy of each line: in UTF-8 for
  each player this machine sent or heard UTF-8 from, whose setup block in
  the recording says so, and as it was heard for the others. Playing it
  back shows those lines as they are with the setting on and in the code
  page with it off.

Players with the setting on and off play together: one with it off reads
`?` for each character its code page lacks. The setting is not one of the
game's rules and is outside every profile's hash, and the simulation never
reads chat. With it off, nothing changes on the wire: the setup block's three
bytes stay as they are, and a line goes as one record holding its first
64 bytes, cut between whole characters, as 3.1c sends it.

## The engine's own words

The OA settings dialog and the engine's own notices are written in English
and pass through the interface catalogue (`oa/data/languages/interface_text.hpp`):
TDF files in a `languages` folder beside the game's `fonts` folder, and
each language pack's `interface.tdf`, each section naming an English text
and each key a language's tag:

```
[Mouse wheel zoom]
	{
	de = ...;
	fr = ...;
	}
```

The Simplified Chinese pack's `interface.tdf` translates them for
`zh-Hans`; the player's packs replace the engine's words, and a mod's
replace both. These are the words: the dialog's
section names and headings, its labels, hints, values (`per player`,
`fps`, `Desktop`, `None`, `cells`), switch and level captions, lock texts,
Hardware acceleration's status lines, the footer's buttons, `System default`
and the header's title; the names of the standard hacks and their areas
in Developer Mode's list (`Deterministic Wind`, `Interface`: each hack's
and area's title in the mod registry), which the list sorts as the
language shown writes them; and the notices about missing skirmish and
multiplayer maps, the end of the game's missions, the graphics card and
driver, and settings that were not saved.

The touch and gamepad controls' words come from the same catalogue, after
3.1c's own table. Where one English word stands for two things, the
control is looked up by a longer text that says which, and without a
translation of that text the word shows in English: the right rail's
NEXT, which centres the next unit, by `NEXT UNIT`, as SELECT ▾'s item is;
the build pages' NEXT, on the build ring and in the phone's drawer, by
`NEXT PAGE`; and the rings' ARM, which arms the aimed order, by
`ARM ORDER`, since 3.1c's table holds `ARM` as the side's name.

## Adding a language

A language is one entry in `src/data/languages/src/registry.inc`: its tag,
its name in itself, its name in English, the word the game data knows it
by, the system locales that choose it, the languages it falls back to and
what drawing it needs. Nothing else changes in the code: the setting
offers it, the operating system's locale chooses it, and every lookup
above reads it, from game data that holds its text under that word, from
language packs for its tag, and from catalogue files that hold the
engine's words under its tag.

A language whose letters are all in the game's 8-bit code page
(Windows-1252), as Portuguese's or Dutch's are, needs nothing more. Others
need drawing the game has in part or not yet:

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
