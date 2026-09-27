# Frontend renderer

This component turns the game's `FrontendX` PCX, `guipal` palette,
`MAINMENU.GUI` layout, and `MAINMENU.GAF` sprites into a portable RGB surface.
`load_main_menu` uses `oa::AssetStore`, so loose resources and mounted archives
keep the game's lookup order. An install whose `by.ccx` carries its own
`MAINMENU.GUI` shadows TA's with a copy whose four buttons (x 82 and 464) sit
under that archive's main-menu overlay; `MainMenuLayout::base_game` passes
over that copy for `totala1.hpi`'s (x 139 and 409, inside FrontendX's pipe
frames), which the application takes whenever no overlay stands over the
menu.

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
