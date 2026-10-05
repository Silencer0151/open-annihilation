# Folder chooser

The in-engine game folder chooser's model (`oa/ui/folder_chooser.hpp`,
`oa::ui::folder_chooser`): the screen that asks for the Total Annihilation
folder where the system's folder dialog may not show (Steam's Game Mode on a
Steam Deck), where several folders were found, or where a remembered folder
has gone. It is driven by touch, a gamepad, the keys or the pointer, and has
no SDL and no app header. The app (`folder_chooser_screen.cpp` in
[src/app](../../app/README.md)) fills a `Model`, lays it out with `lay_out`
and paints the result with the Game files screen's painter, in that screen's
look ([src/ui/game-files](../game-files/README.md)); presses and keys come
back as a `Command` for the app to carry out.

## Entry points

- `lay_out(model, state, viewport, measure)` lays out the view the model
  names, in the tablet form (the Steam Deck's 1280x800) or the compact form
  (a phone, or a window whose shorter side is under 600 points), inside the
  viewport's safe area, every control at least `min_button_points` (44 pt)
  high:
  - the list: the title, a line that says how many folders were found, the
    notice (a remembered folder that has gone and why, the system's dialog
    that did not open, or a folder that cannot be played), a row for each
    folder found with where it was found, the folder and what the check
    found there (a folder that cannot be played is shown with why, and takes
    no press), then Browse…, "The Total Annihilation demo (1997)", "Use the
    desktop's folder dialog" where the model offers it, and QUIT;
  - the browser: the folder's name and path, a hint (where the demo's
    installer lies), the notice, the places (Home, Downloads, each drive)
    and PARENT FOLDER, what the check found in the folder, its folders
    (game folders marked, "No folders here." for none), BACK and PLAY THIS
    FOLDER, which takes presses only when the folder can be played.

  The result is a `game_files::Layout` to paint, a box for each target with
  the scrolling region it lies in, the focus order (the enabled targets, top
  to bottom) and how far the rows scroll. The focused and pressed targets of
  `UiState` are marked on their items. A column that does not fit scrolls
  its rows between the fixed top and the buttons; when even that leaves the
  rows too little room, the whole column scrolls.
- `hit_test` finds the target under a point: the one whose visible box holds
  it (none when that target is disabled), else the nearest enabled one
  within reach.
- `press_down` and `press_up` take a press of a finger or the pointer: the
  command comes when the press comes up on the target it went down on, and
  the focus follows it. A press hides the focus the keys showed.
- `key` takes the keys a keyboard or a gamepad gives: the first shows the
  focus; the arrows move it to the nearest target in their direction (the
  rows judged as if unscrolled, so up from BACK reaches the last folder),
  Tab and Shift+Tab through the focus order, and a target the focus reaches
  is scrolled whole into view; Enter presses; Escape goes from the browser
  back to the list; the page keys scroll a page.
- `scroll` scrolls the rows, held within the layout's range.

Every word goes through `oa::data::languages::interface_text`.

## Tests

`ui-folder-chooser` lays out the list and the browser at 1280x800 and at
852x393 with a phone's safe area: every target at least 44 points high and
inside the safe area, the focus order, the dialog's row only where offered,
the notice, the disabled rows and buttons; the keys (the first key, arrows
across the places and down into the folders, scrolling the focused folder
into view, up from BACK, Tab round the order, Enter, Escape, the page keys);
presses down and up, a press that slides off, a disabled target and a press
within reach; and the scroll's limits.
