# data/languages

The languages the game shows its text in, and the tables that hold its text
in them for what players see. Nothing here reaches the simulation, a saved
game or what a shared game sends. [docs/languages.md](../../../docs/languages.md)
describes the whole for players and for adding a language.

## The registry

`oa/data/languages.hpp` (namespace `oa::data::languages`) holds one
`Language` for each language the game knows, in `src/registry.inc`:

| Tag | Name in itself | 3.1c's word | Draws in |
|---|---|---|---|
| `en` | English | `English` | the game's fonts |
| `de` | Deutsch | `German` | the game's fonts |
| `es` | Español | `Spanish` | the game's fonts |
| `fr` | Français | `French` | the game's fonts |
| `it` | Italiano | `Italian` | the game's fonts |

English comes first, then the others in the order of their names, which is
the order the settings list them in. Each entry has its BCP-47 tag, its
name in itself (`endonym`, UTF-8) and in English, 3.1c's word for it
(`game_name`: Translate.tdf's key, the start of a unit file's
`<Language>Name` and `<Language>Description` keys, the end of its folders'
names such as `bitmaps-German`), the system locales that choose it, the
tags its text falls back to before English, and what drawing it needs
(`TextNeeds`). English has 3.1c's word too, which the game runs in by
default; its data holds no English entries, but a mod's may.

- `known_languages()`, `english()`, `find_by_tag()` (any case, `_` read as
  `-`) and `find_by_game_name()` (3.1c's command-line word, any case).
- `fallback_chain()`: the language, its known fallbacks, then English, each
  once.
- `normalised_locale()` writes a system locale (`de_DE.UTF-8@euro`) as a
  tag (`de-DE`); `C` and `POSIX` are none. `match_locale()` finds the
  language one of whose locales is the tag or its leading subtags (`de`
  for `de-AT`), the longest winning: an entry listing `zh-TW` wins over one
  listing `zh` for `zh-TW`. `preferred_language()` takes the first locale,
  in order, that chooses one, else English.
- `chosen_language()` reads the setting: `system_choice` (`"system"`) or a
  tag; anything else is the system's.
- `drawable()`: the needs this build meets, `game_fonts` and
  `modern_fonts`. A language that needs more is kept but not offered.

## Texts for what players see

- `oa/data/languages/unit_texts.hpp`: `UnitTexts`, units' names and
  descriptions in each language their files give, which the unit loaders
  fill through a `oa::data::defs::UnitTextSink`
  (`oa/data/defs/unit_texts.hpp`), and `data_words()`, the words a
  language's text is looked up by. The application installs the table, the
  words and the sink (`set_unit_texts`, `set_unit_text_sink`); every place
  that shows a unit type's name or description asks `unit_display_name()`
  or `unit_display_description()`, which fall back to the type's own
  `UnitDef.name` and `description`. The definitions themselves keep Name
  and Description in every language.
- `oa/data/languages/translation.hpp`: the hook the game's own texts are
  translated through, which the application fills from
  `gamedata\translate.tdf` in the language shown (`set_translation_hooks`).
  `translation_of()` and `installed_translation()` translate by the exact
  text; `language_folder_path()` gives a file's place in the language's
  folder, as 3.1c looks there first; `installed_word()` the word itself.
- `oa/data/languages/interface_text.hpp`: the interface catalogue,
  `InterfaceText`, the engine's own words in the languages translations are
  given for, read from TDF files whose sections name the English text and
  whose keys are tags; `set_interface_language()` and `interface_text()`.
  No translation ships yet.

The installed tables are read only by the thread that draws the interface.

## Tests

`data-languages` checks the registry and its order, the needs that are
drawable, tags and 3.1c's words found in any case, fallback chains, locales
normalised and matched alone and in lists, the settings' choice, the
catalogue read from small built files, malformed and oversized ones
included, the words a language is looked up by, and the units' texts with
their fallbacks, installed and not. `defs` checks that a unit file's
language keys reach the sink, cut to their fields.
