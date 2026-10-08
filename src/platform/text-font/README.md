# Text font

Game text in the modern fonts, and the text the game's own 8-bit fonts
cannot draw, such as Greek, Cyrillic, Chinese, Japanese, Korean and emoji,
drawn through FreeType from fonts that travel with the game. Every platform
draws it the same way, pixel for pixel: macOS, Windows (XP included) and
Linux. The application draws game text through it
([runtime_game_text.cpp](../../app/runtime_game_text.cpp)), with the
outline, shadow and background of
[oa/present/game_text.hpp](../../present/include/oa/present/game_text.hpp).

## The fonts

`FontStack::open` opens four fonts from one folder, `face_files` in
[text_font.hpp](include/oa/platform/text_font.hpp). Each character comes from
the first font that has it:

| Face | File | Draws |
|---|---|---|
| `dejavu_sans_bold` | `DejaVuSans-Bold.ttf` | Latin, Greek, Cyrillic and symbols in the bold weight; skipped in the regular weight |
| `dejavu_sans` | `DejaVuSans.ttf` | the same scripts in the regular weight, and what the bold face lacks |
| `noto_sans_cjk` | `NotoSansCJKsc-Bold.otf` | Chinese, Japanese kana and kanji, Korean Hangul, CJK punctuation and full-width forms |
| `noto_emoji` | `NotoEmoji.ttf` | emoji, in one colour like any character; its weight axis follows the line's weight |

A character no font has draws the first font's missing-glyph box. Control
characters, variation selectors, zero-width spaces and joiners, and the
byte-order mark draw nothing and take no room (`is_invisible`).

`tools/bootstrap_text_fonts.py` fetches the fonts and FreeType, pinned by
SHA-256, into `local/deps`, and cuts Noto Sans CJK SC Bold down to the
characters of the common Chinese, Japanese and Korean character sets, the
Table of General Standard Chinese Characters among them (its help lists
them). The build copies the fonts into the `fonts` folder beside
the game (`cmake/OaTextFonts.cmake`), which `bundled_font_directory` finds at
run time: the bundle's `Contents/Resources/fonts` on macOS, the fonts folder
beside the executable elsewhere. Their licences are in the repository's
`licenses/` folder and travel beside the game with the others
([ATTRIBUTIONS.md](../../../ATTRIBUTIONS.md)).

## Entry points

- `FontStack::draw(text, style)` draws one line of UTF-8 as coverage, one byte
  a pixel. `Style` holds the DejaVu faces' pixel size, the weight, the
  rendering and the letter spacing. `Rendering::mono` hints the outlines to
  whole pixels and draws one bit a pixel, coverage 0 or 255, crisp like the
  game's pixel fonts; `Rendering::antialiased` hints them and draws 256
  levels. The pen starts at the coverage's `origin` column and moves by each
  character's advance rounded to a whole pixel, plus the letter spacing
  after each character that moves it. Characters are not shaped, kerned or
  reordered, so scripts that need shaping (Arabic, Hebrew, the Indic
  scripts) are drawn letter by letter, left to right.
- The coverage is at least the line's ascent and descent tall
  (`FontStack::metrics`), and grows for a glyph that reaches past them;
  `baseline` gives the baseline's row and `advance` how far the pen moved.
  A line wider than `max_line_width` is cut off there.
- `FontStack::layout` gives each character's font and pen without drawing,
  and `FontStack::face_for` the font a character comes from.
  `fallback_chain` gives the faces a weight looks in, in that order.
- `FontStack::face_metrics` gives one face's pixel size and rows at the size
  a style draws it at. `FontStack::metrics` is the greatest of those rows
  over the weight's chain.
- `related_pixel_size` gives the size Noto Sans CJK and Noto Emoji are drawn
  at beside the DejaVu faces: 12 px beside 14 px, so ideographs stand a row
  or two taller than DejaVu's capitals, as the game's outlined capitals do.
  `Style::least_cjk_pixel_size` holds Noto Sans CJK to a least size, and
  the line's rows grow to hold it: the application draws ideographs at
  `least_cjk_language_pixel_size` (12 px) at the least while a Chinese,
  Japanese or Korean language is shown, since smaller ones fill in.
  `message_log_pixel_size` (14), `status_readout_pixel_size` (11) and
  `label_pixel_size` (11) are the sans faces beside the message log, the
  status readouts, and the labels and the chat line.
- `decode_utf8` decodes text, refusing what is not UTF-8.

The sizes that match the game's fonts: DejaVu Sans Bold at 14 px, mono,
beside the message log's `hattfont12` (capitals 10 rows, x-height 8,
2-pixel stems), and DejaVu Sans at 11 px beside `CONSOLE.FNT`, the chat
line's and the labels' font (x-height 6, 1-pixel strokes). At 14 px the
bold sans face has 13 rows above the baseline and 4 below; the line is 14
and 4, which is the CJK face at 12 px. The application
draws them at the player's Text size, from half to three times those
sizes and never under 7 px, mono at every size.

## Invariants

- A stack draws only with its own FreeType library, made without FreeType's
  default properties, which an environment variable could change, so every
  machine draws alike.
- The fonts are read from their files as glyphs are needed, through the
  module's own file stream, so a folder whose path is not in the Windows code
  page opens too.
- Each glyph is drawn once per font, size, weight and rendering and kept, up
  to `FontStack::kept_glyphs`; past it, the glyphs used longest ago are
  forgotten, and only between lines.
- One thread at a time uses a stack. The drawing is the view's alone: it
  reads no game state and writes none.

## Tests

`platform-text-font` (`tests/text_font_test.cpp`) checks the UTF-8 decoder
and what it refuses (`decodes_only_utf8`), the limits on size, length and
spacing (`refuses_what_it_cannot_draw`), the fallback chain
(`falls_back_through_the_chain`), the sizes that match the game's fonts:
DejaVu Sans Bold at 14 px gives an H of 10 rows and an x of 8, DejaVu Sans at
11 px an x of 6, and ideographs at 12 px stand 11 or 12 rows, and each face's
rows at that size are what `face_metrics` reports, the line taking the
greatest of its chain (`matches_the_game_fonts_sizes`), mono, anti-aliased
and spaced lines and the glyph store (`draws_mono_and_antialiased`), a store
that stays within its bound and forgets the glyph used longest ago
(`keeps_the_glyphs_used_last`),
and ideographs held to a least size while Latin letters keep theirs
(`holds_ideographs_to_a_least_size`). `platform-text-font-pixels`
runs the same program with `--pixels`: "Ab", a Chinese character and an emoji
in bold at 14 px, mono, pixel for pixel as FreeType 2.14.3 draws them; it
skips with another FreeType. `--show TEXT` prints a line as it is drawn.

## Limitations

- No shaping, kerning or bidirectional text, and no colour emoji.
- The cut CJK font holds about 15,300 characters; rarer ones draw the
  missing-glyph box unless the bootstrap ships the whole face
  (`--full-cjk`).
- The CJK font is the Simplified Chinese face, so Japanese and Traditional
  Chinese text takes its forms of the characters the languages share.
