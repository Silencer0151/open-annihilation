# Frontend renderer

This component turns the game's `FrontendX` PCX, `guipal` palette,
`MAINMENU.GUI` layout, and `MAINMENU.GAF` sprites into a portable RGB surface.
`load_main_menu` uses `oa::AssetStore`, so loose resources and mounted archives
keep the game's lookup order. An add-on archive may carry its own
`MAINMENU.GUI`, laid out for a main-menu overlay drawn over the menu, which
shadows the one laid out for `FrontendX`. `MainMenuLayout::with_overlay` takes
the top copy. `MainMenuLayout::base_game`, which the application takes
whenever no overlay stands over the menu, takes the copy in the archive that
provides `FrontendX` (`totala1.hpi`'s, whose four buttons at x 139 and 409 sit
inside FrontendX's pipe frames), so that the layout goes with the background
it was drawn for. It takes the top copy when `FrontendX` or that copy is a
loose file, or when the archive that provides `FrontendX` holds no
`MAINMENU.GUI`; an add-on archive that carries a `MAINMENU.GUI` but no
`FrontendX` is then passed over, where 3.1c would show its layout.

`load_screen` accepts explicit layout, PCX, palette, and GAF resource names for
the other frontend screens. The explicit names matter: the renderer does not
infer that every screen follows the main-menu convention.

`FrontendX` remains the active display palette. As in the game's screen setup,
`guipal` indices used by GUI primitives are mapped into that active PCX palette
with the nearest-color rule. Normal GAF pixels use the active PCX palette
directly.

The main menu is set up as in 3.1c. Type-1 buttons are filled and given
raised or sunken borders as the game draws its buttons.
GAF-backed gadgets use the normal sprite path and its explicit coverage mask.
In the shipped main menu, `Credits` resolves to the five-frame `Credits`
sequence in `MAINMENU.GAF`; the four named menu buttons fall back to the shared
`COMMONGUI.GAF` `BUTTONS0` sequence, selecting the closest frame among the
four-state group starts (0, 4, 8, 12, ...) and its following depressed partner.

Button captions use the `hattfont12.GAF` glyph path. Width, line height,
centering, pressed offset, space handling, and origin-aware GAF glyph drawing
are those of the game's captions.

Type-2 text lists: content begins two pixels inside the gadget, zero
`itemheight` selects font line height plus one, rows are clipped to the
authored rectangle, and the selected row uses level `0x1E` from the game's
`PALETTE.LHT` table. Type-5 labels keep the alignment bits, clipping, and the
multiline threshold and spacing.

Runtime image pointers are explicit presentation bindings. Dynamic `SIDEx`
buttons and `ally icons` bind to the screen GAF, while player colors bind to
`32xlogos` in the globally loaded `textures/LOGOS.GAF`; these are not inferred
from widget dimensions or stage counts.

## Scroll bars

`oa/ui/frontend_renderer/scroll_bars.hpp` draws scroll bars on the RGB
surface as 3.1c draws them from the SLIDERS art: the track's start, middle
and end frames, the knob 3 pixels past its position (a vertical knob as its
start, middle and end frames, at most the bar's height less 6 long and
ending at least 4 pixels above the bar's end; a horizontal knob as its start
frame, at least its width and 2 pixels before the bar's last column), and
each arrow's frame, or its held face while the pointer holds it. A grayed bar
and its arrows are grayed through the palette's gray table and shaded at the
grayed level over their rectangles, as a grayed button with art is
(`grayed_paint`, `gray_and_shade`).

`LayoutScrolls` binds a loaded layout's bars and readies its lists as a
panel's first draw does (`bind_layout_scrolls`), writing the bound rectangle,
positions, knob size and knob back into the gadgets, routes the pointer's
presses, moves, releases and holds to them, keeps each list's first row in
step with its bar, and draws them over the screen (`draw_layout_scrolls`).
`bind_screen_buttons` binds a panel's buttons as the first draw does, for a
panel merged into a loaded one. `scroll_bars_test.cpp` (`frontend-scroll-bars`)
pins the art, the drawing and a layout's bars and lists.
