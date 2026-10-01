# Application menu

The macOS application menu's **Settings…** item, which opens the Open
Annihilation settings. The windowing library adds a "Preferences…" item with
the key equivalent Cmd+, and no action; `install_settings_item` names it
Settings… and gives it an action that calls the hook the game passes, which
asks the game's event loop to open the settings. Chosen with Cmd+, the item
calls nothing: the key reaches the game as a key press before the menu sees
it, and the game opens the settings from the key. `enable_settings_item`
enables the item where the settings can open, on the main menu and in a match,
and greys it elsewhere.

Other platforms have no application menu: `settings_item_supported` is false
there and the other functions do nothing. The game opens the settings with
Ctrl+, there, and with Cmd+, on macOS when the key reaches it.

The test checks which platforms offer the item and that installing and
enabling it without a menu calls nothing. On macOS a second test gives the
process an application menu shaped like the windowing library's, without a
window, and checks the rename, the action, the greying, that Cmd+, sent
through the application calls nothing, and that a menu without the item gains
one.
