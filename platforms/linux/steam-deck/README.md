# Steam Deck files

This folder goes into the Steam Deck package as `steam-deck`, beside the
game, `open-annihilation`. It holds Open Annihilation's two controller
layouts for Steam Input and its artwork for the Steam library, for a Steam
Deck, or any Linux computer with Steam, that plays the game as a non-Steam
game. Steam Deck support is experimental. The Steam Deck guide,
[docs/installation/steam-deck.md](https://github.com/open-annihilation/open-annihilation/blob/main/docs/installation/steam-deck.md),
walks through every step, from adding the game to Steam to choosing a
layout.

## What it holds

| File | What it is |
|---|---|
| `open-annihilation.vdf` | The Steam Input template "Open Annihilation": the gamepad's sticks and buttons as they are, the right trackpad as the mouse, the left trackpad as the left stick, and the back grips as the keys F13 (R4), F14 (R5), F15 (L4) and F16 (L5), which the game reads as its grips |
| `open-annihilation-keyboard-mouse.vdf` | The Steam Input template "Open Annihilation: keyboard and mouse": mouse and keys only |
| `artwork/` | Steam's library pictures for the game, made from the Open Annihilation icon when the package is built: `portrait.png` (600×900), `wide.png` (920×430), `hero.png` (1920×620), `logo.png` (as wide as the icon and the name need) and `icon.png` (256×256). It is not in the repository |

## Installing them by hand

Nothing here installs itself. Add the game to Steam first, in Desktop Mode:
in Steam, **Games**, then **Add a Non-Steam Game to My Library…**, then
**Browse…** and the package's `open-annihilation` (with **File type** set
to **All Files** if it is not listed), then **Add Selected Programs**; or
right-click `open-annihilation` in Dolphin and choose **Add to Steam**. In
the shortcut's **Properties**, name it Open Annihilation, leave the launch
options empty and force no Steam Play compatibility tool: the game is a
native Linux program, not one for Proton.

- **The templates.** Copy both `.vdf` files into
  `~/.steam/steam/controller_base/templates/`, for example in Konsole from
  the package's folder:

  ```sh
  mkdir -p ~/.steam/steam/controller_base/templates
  cp steam-deck/*.vdf ~/.steam/steam/controller_base/templates/
  ```

  Then restart Steam (switching to Game Mode does it), open the game's
  controller settings and choose **Templates**, then **Open Annihilation**.
- **The artwork.** In Steam in Desktop Mode, right-click the game, choose
  **Manage**, then **Set custom artwork**, and pick
  `steam-deck/artwork/portrait.png`. The guide says where the banner, the
  logo and the icon go.

## For developers

`cmake --build <build folder> --target oa-steam-deck-files` makes the
artwork with `oa-steam-artwork` and copies this folder's README and both
templates beside it, into `steam-deck` in the build folder, ready to go
into the Steam Deck package ([docs/development/releasing.md](https://github.com/open-annihilation/open-annihilation/blob/main/docs/development/releasing.md)).

The artwork is the Open Annihilation branding, whose terms are in
`licenses/LicenseRef-OpenAnnihilation-Branding.txt` beside the game. The
templates are under the game's licence, the GNU General Public License
version 3 only.
