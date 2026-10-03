# platform/locale

The languages the user prefers, as the operating system gives them. The game
picks the language it shows its text in from them when the player's setting
is System default ([src/data/languages](../../data/languages/README.md) says
which it picks).

`oa/platform/locale.hpp` (namespace `oa::platform::locale`):

- `preferred_locales()` returns the user's preferred locales, most preferred
  first. It asks SDL (`SDL_GetPreferredLocales`), which reads the system's
  own settings: the preferred languages on macOS, the user's interface
  languages on Windows Vista and later and the user's locale on Windows XP,
  and the `LANG` and `LANGUAGE` variables on Linux. Each comes as its
  language and country joined by `-` (`de-AT`, or `de` without a country).
  Where SDL gives none, it asks the system itself: on Windows the user's
  interface language (`GetUserDefaultUILanguage`, which Windows XP has),
  elsewhere the locale environment variables.
- `environment_locales()` reads those variables through a reader the caller
  gives: the colon-separated list in `LANGUAGE`, then `LC_ALL`,
  `LC_MESSAGES` and `LANG`, as programs that follow POSIX and GNU read them.
  Each locale is listed once, and empty values, `C` and `POSIX` are left
  out. A list keeps at most `most_locales` (16).

SDL's hint `SDL_PREFERRED_LOCALES` (the environment variable of the same
name), a comma-separated list such as `de_DE,fr`, stands in for the system's
settings; the game's checks use it to start in a language.

The module reads and writes no game state. `platform-locale` tests the
variables' order, each locale listed once, `C` left out and long lists cut,
and SDL's list through its hint.
