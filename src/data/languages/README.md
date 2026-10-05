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
| `zh-Hans` | 简体中文 | `Chinese` | the modern fonts |

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
  for `de-AT`), the longest winning. The locales of languages not offered
  yet (Traditional Chinese's `zh-Hant`, `zh-TW`, `zh-HK` and `zh-MO`) take
  part in that match and choose none, so `zh-TW` never falls to `zh` and
  Simplified Chinese. `preferred_language()` takes the first locale, in
  order, that chooses one, else English.
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
- `oa/data/languages/language_pack.hpp`: language packs, a folder of texts
  for one language that adds to the game data's own translations entry by
  entry. `read_manifest()` reads `language.yaml` with the mod profiles'
  YAML reader; `LanguagePack` holds `translate.tdf` (by 3.1c's English
  text), `units.tdf` (by UnitName, each text with the English it
  translates in a `-from` key, skipped when the game data's English
  differs), `missions.tdf` (by mission file) and `pictures.tdf`
  (`PictureCaptions`, captions drawn over the player's pictures); a pack's
  `interface.tdf` is read by the interface catalogue. A `PackLayer` lists,
  for one game-data word, the packs tried before the game data (a mod's)
  and after it (the player's, then the engine's): `layered_translation()`,
  `layered_mission_text()`, `layered_pictures()` and, once installed with
  `set_unit_pack_layers()`, `unit_display_name()` and
  `unit_display_description()` look through them. The application reads
  the files; nothing here opens one. Missions' texts and files of the
  language folders reach the campaign through `TranslationHooks`
  (`installed_mission_text()`, `installed_language_file()`).

The installed tables are read only by the thread that draws the interface.

## Tests

`data-languages-packs` reads the pseudo-language's pack in
`tests/pseudo-pack` (the tests' own, never shipped), refuses manifests by
rule, and looks each kind of text up through the layers: a mod's pack
first, the game data, the player's pack, the engine's pack, an empty value
and a text no pack holds falling through, and a unit text written for
other English skipped.

`data-languages` checks the registry and its order, the needs that are
drawable, tags and 3.1c's words found in any case, fallback chains, locales
normalised and matched alone and in lists, the settings' choice, the
catalogue read from small built files, malformed and oversized ones
included, the words a language is looked up by, and the units' texts with
their fallbacks, installed and not. `defs` checks that a unit file's
language keys reach the sink, cut to their fields.
