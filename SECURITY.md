# Security policy

## Reporting a problem

Report security problems privately, through GitHub's private vulnerability
reporting: open this repository's **Security** tab and choose **Report a
vulnerability**, or go straight to
<https://github.com/open-annihilation/open-annihilation/security/advisories/new>.
Only the maintainers can read the report.

Do not describe the problem in a public issue, pull request or chat until a
fix is published.

A useful report says:

- which release or commit, and which platform, shows the problem;
- what goes wrong, and what someone could do with it;
- how to reproduce it, with any file you made to trigger it attached to the
  report. Do not attach the game's own files; name the installed file
  instead;
- any fix or workaround you know of.

## What to report

Open Annihilation reads files that players download and share, such as maps,
unit and weapon definitions, unit scripts, sounds, movies and saved games,
as well as the archives of the installed game. Report privately any input
that makes it:

- read or write outside the memory it allocated, or crash;
- use unbounded memory or time;
- read or write files outside the folders it is meant to use;
- run code of the file's choosing.

Problems in the libraries it uses (SDL, zlib, and the music decoders in
`third_party/`) belong to those
projects; if you find one through Open Annihilation, report it to that
project and tell us too, so that the release can pick up the fix.

## What happens next

A maintainer confirms the report in the private advisory, works on a fix
there with you, and publishes the fix and the advisory together. Say in the
report whether you would like to be credited.

Fixes are made on `main` and ship in the next release.
