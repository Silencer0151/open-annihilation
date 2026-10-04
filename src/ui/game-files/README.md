# Game files screen

The Game files screen's model (`oa/ui/game_files.hpp`, `oa::ui::game_files`):
what the screen that brings the player's Total Annihilation files into the
game's own storage shows, where it goes and what a press or a key on it
means, with no SDL and no app header. The app (`src/app/game_files_screen.cpp`)
maps the import's state onto a `Model`, lays it out with `lay_out` into a
`Layout` (items in canvas pixels, painting order, focus order) and paints it;
presses and keys come back through `press_down`, `press_up`, `key` and
`scroll` as a `Command` for the app to carry out. See
[docs/game-files.md](../../../docs/game-files.md) for the screen as players
see it.

## Entry points

- `device_class` gives the form from the canvas's size in points: a phone
  when the shorter side is under `touch_hud::phone_short_side_points`, else
  a tablet, so the phone form can be checked on a desktop window of a
  phone's size. A tablet window whose shorter side is under 600 points (a
  640x480 window) keeps the tablet's form with the phone's compact sizes.
- `lay_out(model, viewport, measure)` lays out the model's step (first run,
  looking, the nested offer, already there, Ready to copy, copying,
  checking, Ready to play, a problem, or the management state) with its
  banner and sheet, inside the viewport's safe area, every control at least
  `min_button_points` high. `TextMeasureHooks` measures text with the
  bundled fonts the app opens; the layout wraps and shortens every line with
  that measure.
- `hit_test` finds the control under a point: the containing one, else the
  nearest enabled one within `pick_reach_points` (22 pt). Controls painted
  before a backdrop (under a sheet) and the clipped-off parts of scrolled
  rows take no presses.
- `press_down`, `press_up`, `key` and `scroll` change the sheets, switches,
  focus and scroll themselves and return what the app must do (`Outcome`).
  STOP, COPY with a replacement, SHOW, WHY, REMOVE, REMOVE OLD FOLDER,
  REMOVE ALL, ADD FILES… and DONE with a change waiting open their sheets;
  a sheet's button closes it and returns its command. Tab and Shift+Tab
  move the focus in `Layout::focus_order` and scroll a row into view;
  Return and Space press (Return with no focus shown presses the main
  button); Esc closes a sheet, else goes back (cancels the listing, opens
  the Stop sheet while copying, acts as DONE in the management state).
- `mark_interaction` sets the items' focused and pressed looks before
  painting.
- `platform_word`, `size_text`, `time_left_text`, `ready_text` and
  `summary_text` build the texts. Every player-facing string goes through
  `oa::data::languages::interface_text` whole, with its `{name}` places
  filled after the lookup, so a translation sees the whole sentence. The
  platform's own words come from `Model::words` (the app fills them from
  `GameFilesHooks::text`); without them the engine's neutral words are used:
  "device", "Copy your Total Annihilation folder into Open Annihilation's own
  folder with your file manager.", "Into Open Annihilation's own folder",
  "on this device or anywhere the system's file picker reaches", "anywhere
  the file picker reaches", "Free up space on this device", "In your file
  manager: Open Annihilation's own folder › Total Annihilation" and "the
  cloud".

## Layout

A step is the header bar (the OA badge, "OPEN ANNIHILATION  GAME FILES",
the version, and OA · Aa except in the management state), then a column of
three blocks: a fixed top (title, banner), a body (the cards, the parts'
rows, a problem) and a fixed bottom (totals, warnings, buttons), then on
the first run a footer at the foot of the safe area. When the column does
not fit, the body scrolls in `Layout::rows` between the fixed blocks; when
that would leave the body under 96 points, the whole column scrolls. A sheet
is laid out the same way in its panel over a backdrop. Sizes are in points
(the tablet's after the 1194x834 mock-ups, the phone's after the 852x393
ones), converted with `px_per_point`.

Painting follows the items: text roles draw their lines from the top of the
box, left-aligned, one line height apiece (the measure's line height);
buttons and the switches' halves centre their labels; a button's
glyph is a square of the label's pixel size, or of `Item::glyph_size` when
the button sets one, `button_glyph_gap_em` of the label's size left of the
label. A row's size is a text box exactly as wide as its text,
so right-aligned texts need no other rule.

The OA mark is the Open Annihilation icon, as the settings dialog shows it:
the badge is a box with no text that the app fills with the icon, and OA ·
Aa is a button whose glyph is `Glyph::oa` (the icon, 20 points on the
tablet and 18 on the phone) beside "Aa". The app
draws the icon at the screen's own density, and without it the OA mark the
settings dialog's OA button shows: green "OA" letters in a green outlined
square.

## Sizes

`size_text` writes decimal units as the system shows them (1 GB is
1,000,000,000 bytes): one decimal under 10 and whole numbers above ("1.1
GB", "38 GB", "742 MB"), or with `precise` three significant digits ("1.12
GB", "11.3 GB"); trailing zeros are dropped ("3 MB").

## Tests

`ui-game-files` (`tests/game_files_test.cpp`) lays out every step, banner,
problem and sheet at 1194×834 points, at 852×393 points with a 59/21 pt safe
area (at 3 and 2 pixels a point) and at 640×480, scrolled to the top and to
the bottom, and checks control heights, the safe area, text inside its box
as the bundled fonts measure it, overlaps and the focus order; then hit
tests, every press's command, the problems' buttons, the keys, scrolling,
the texts with neutral and with scripted platform words, and the size and
time tables. `oa-ui-game-files-test --dump <case>` prints the items of the
cases whose names hold the text.

## Limitations

- Only English is shown until translations ship; the catalogue may hold
  the screen's sentences once they do.
- The layout shortens a line it cannot fit with an ellipsis; very long
  locations are cut to two lines.
