# src/app/automation

The automation endpoint: a service on this machine's loopback address
through which a program on the same machine drives the game and reads
what it shows and holds, in the automation protocol. It is `oa-game`'s
extension `oa-app-automation`, which the engine builds and registers in
every build, after network play's. Without `--fark` it does nothing.
[docs/automation.md](../../../docs/automation.md) describes it for those who
write a program that drives the game.

## The options

```
--fark                     serve the automation endpoint
--fark-address ADDRESS     where: 127.0.0.1:PORT or [::1]:PORT; port 0, the default
                           (127.0.0.1:0), lets the system choose
--fark-file PATH           where the address and token are written; by default
                           automation.json in the folder of the preferences file
```

- Only the command line turns the endpoint on; no preference, environment
  variable or file does.
- Only this machine's loopback address can be served: any other is refused
  with `automation: only a loopback address can be served`, and the game
  exits as for any refused option. `--fark-address` and `--fark-file`
  without `--fark` are refused too.
- A run with `--fark` cannot be used with an option that runs no main loop
  or owns it, since the endpoint is served from the main loop:
  `--headless-check` (and the options that imply it), the `--check-*`
  options, `--benchmark`, `--match-ticks`, `--render-script`,
  `--generate-script` and `--showcase`. The game refuses them as its first
  runtime starts. `--host` and `--join` go with it.
- `--fark` is an option the engine knows as one through which a program
  controls the run (`option_effect::remote_controlled`): the main loop
  runs every frame while the window is inactive, so the endpoint is
  served, and the battle room's engine line ends in ` REMOTED`.

As the first runtime starts, the endpoint listens and writes its file,
first beside it and then renamed over it, with only its owner allowed to
read it where the system has such modes:

```json
{"version":1,"address":"127.0.0.1:53122","token":"9c1f…(32 hex digits)","pid":41877}
```

The token is 128 random bits from the system's generator, new each run.
The log says `automation: serving on 127.0.0.1:53122` and where the file
is, never the token. The endpoint lives as long as the process, so a
client stays connected while a switch of the mod rebuilds the runtime.

## The protocol

Frames are as the codec below reads and writes them, with route `-` both
ways. A request is `{"id", "op", ...}`; an answer `{"id", "ok": true,
"frame", "tick", ...}`, where `frame` counts the frames the endpoint has
been served and `tick` is the running match's (0 outside a match), or
`{"id", "ok": false, "error": {"code", "message"}}`.

- One client at a time. A connection whose first bytes are not `AUTO/1 `
  is closed unanswered, and so is one whose first frame cannot be read or
  is larger than a hello needs: a JSON part above 4 KiB, or a payload. Its
  first request must be `hello` with `versions` (holding 1), `client` and
  `token`: a wrong token, or another request, is answered `denied` and the
  connection closed, compared in the same time whichever characters
  differ. While a client is connected, another connection's first request
  is answered `busy` and the connection closed. A connection that sends
  nothing for 10 seconds is closed.
- A client may send its requests and end its side of the connection, as a
  one-shot client piping them in does: every request it sent is still
  answered, and the connection closes once the answers are written.
- Text is given as the game holds it when it is UTF-8. Text the game holds
  in a code page that is not, such as names, captions and preference
  values, is given whole as `Non-Unicode Text Error::` and the base64 of its
  bytes, as [the codec](#the-codec) says.
- The client's requests are taken in order. A frame that cannot be read is
  answered `bad_frame` with the reason and the bytes skipped, a request
  that is not an object, or has no whole-number `id` or no `op`,
  `bad_request` with the `field`, and an operation the endpoint does not
  answer `unknown_op`; none of these has an `id` when the request gave
  none.
- The endpoint is served at each stage of the frame, on the main thread,
  never waiting and starting no thread: it reads at most 1 MiB a
  connection and takes requests for at most 1 ms each time. It reads
  nothing more from a connection while a whole frame of its bytes waits to
  be taken, so that a client sending faster than the game answers is held
  back by its own connection instead of filling the game's memory. Answers
  wait for a client that is slow to read them; one that lets more than
  68 MiB wait is dropped (`automation: client dropped: not reading`).

| Request | Answer |
|---|---|
| `hello` | `version` (1), `engine` (`version`), `automation` (1), `window` (`width`, `height` in the window's pixels), `canvas` (`width`, `height`, and `rect`, `[x, y, w, h]`, where the game's canvas lies in the window: the 640x480 screen on the menus, the whole window in a match), `tick_rate` (30), `fixed_clock` |
| `screen` | `screen` (`main_menu`, `single_player`, `skirmish`, `map_selection`, `loading`, `match`, `options`, `load_game`, `briefing`, `mp_providers`, `mp_tcp`, `mp_game_list`, `mp_new_game`, `mp_battleroom` and the like; `screen_<id>` for one without a name), `frontend_state`, `dialogs` (the dialogs over the screen whose controls take the pointer, the top one first, by their GUI file's name: `selmap`, `tcp`, `yesorno`, `msgbox` and the like), `focused` (the name of the control that takes the keys, or null), `pointer` (`x`, `y` on the canvas), and in a match `camera` (`x`, `y`, the map pixel at the battlefield's top-left corner; null elsewhere) |
| `controls` | `screen`, and `controls`: an extension's windows first (they take the pointer over the screen), then those of the panel that takes the pointer, a dialog's first while one is up (a message box's before the dialog it is over). Each has `name` (as the GUI file spells it, or as the extension spells it), `kind` (`button`, `check box`, `list`, `text field`, `slider`, `label`, `area`, `image`), `dialog` (null for the screen's own and for an extension's), `window` (the extension window's name, or null), `rect` (`[x, y, w, h]` on the canvas), `window_rect` (in the window's pixels), `enabled`, `visible`, `text` and `focused`; a check box adds `checked`; a list adds `items`, `first_visible`, `rows`, `row_height`, `selected` (-1 for none) and `scroll_up` and `scroll_down` (null: a list's scroll bar carries its own arrows). With `screen`, refused `screen_changed` when another is shown |
| `input` | `consumed` (`frame`, `tick`): the frame and tick at which the game took the events. See below |
| `prefs` | `path`, the preferences file, and `values`, the preferences as the game holds them now, whether written to the file yet or not; with `names`, a list of keys, only those |
| `quit` | nothing more; once the answer is written the game quits as a player closing its window does: at once from the menus, through the surrender question in a match |
| `frame` | the frame the game presented in its window, or the one it composed (`source`: `presented`, the default, or `composed`), whole or a `region`, `[x, y, w, h]`, of it in a `space`, `game` (the default) or `window`; with `format` `rgb` (the default) or `png`, `image` (`format`, `width`, `height`, `source`, `window_rect` for a presented frame, and `region` as asked) and the pixels as the payload; with `hash`, `hash` and `mean` and the same fields without a payload ([below](#frames)) |
| `match` | `in_match`; `paused`; `speed` (1 to 20, 10 normal); `players`, each seated player with `slot`, `name`, `status` (`local`, `computer`, `remote`), `side`, `side_name`, `colour`, `team` (null for none), `allies` (slots), `computer`, `local`, `alive` (has units), `units`, `kills`, `losses`; `local_player` (a slot); `game_over`, `outcome` (`victory` or `defeat` for the local player) and `winner` (the local player when it won, else the one player with units left, else null). With `"state_hash": true`, also `state_hash`: the digest of the match's state a saved game carries, as the saved-game check prints it, in 16 hexadecimal digits, worked out only when asked |
| `room` | `in_room`, while the battle room shows; then `session` (the game's name), `host` and `host_slot`, `local_slot`, `hosting`, `players` (each seated one with `slot`, `name`, `status`, `ready`, `watcher`, `side`, `side_name`, `colour`, `team`, `ping`), `map` (the host's), `options` (the host's: `commander` `continues`, `ends` or `deathmatch`; `line_of_sight` `permanent`, `circular` or `true`; `mapped`, `cheats`, `fixed_locations`, `watching_allowed`, `locked`, `password`; `energy`, `metal` and `max_units` to start with) and `chat`, the room's lines; outside it the members are null or empty |
| `subscribe` | `events`: the kinds of event the client is sent from now on, in place of those it took before, as the request's `events` names them: `"all"`, or a list of kinds; an empty list stops them. Subscribing to `screen` is answered with a `screen` event naming the screen shown |

The endpoint reads the game through the check host (`check_host.hpp`),
the automation host (`automation_host.hpp`), which gives the running
match's world and its digest, and the multiplayer screens' own lobby
(`multiplayer_lobby()`), and reaches no `Runtime` member and nothing of
network play's extension. It never changes the simulation: it leaves null the hooks that
could take over a simulation step, resources, a departing player or a
close request, and the one that tells the session's kind. Nothing of it
goes to other machines, and it has no request that reads or writes files,
starts programs or reaches the network.

### Input

`input` carries `events`, at most 1024, each an object with a `kind`:

| Kind | Fields |
|---|---|
| `key_down`, `key_up` | `scancode`, the key's SDL scancode; `keycode`, its SDL key code (else the one the scancode has); `key`, its name, which the endpoint does not read |
| `text` | `text`, the characters a key press types, at most 1024 bytes |
| `pointer_move` | `x`, `y` and `space`: `game`, the canvas (the default), or `window`, the window's own pixels |
| `button_down`, `button_up` | `x` and `y` (else the pointer's last place), `space`, `button` (`left`, the default, `right` or `middle`) and `clicks` (2 for a double click's second press) |
| `wheel` | `dx` and `dy`, the turn in notches, `dy` positive away from the player; `x`, `y` and `space` optional |
| `finger_down`, `finger_move`, `finger_up` | `x`, `y`, `space` and `finger` (0 to 255), on the machine's touch screen |

The events are pushed onto SDL's event queue at the frame's pump, in
order, and the main loop's next poll hands them to the game exactly as it
hands a device's: the window's focus, the key remaps, touch, the pads,
the overlays, the screen's package, then the game's own handling. The
answer comes at the next frame's pump, once the game has taken them; no
other request is taken meanwhile. A point on the canvas is placed in the
window as the game shows the canvas there. A `text` event is pushed only
while the game takes typed text, as SDL types it only then; a key pressed
on a menu types nothing.

A pushed event leaves SDL's own device state as it was, so the keys and
pointer buttons the events hold down are held for the game's reads of what
is held (the arrow keys that scroll the camera, Shift and Ctrl, a held
button), through the automation host, until an event lets them go or the
client leaves, when the endpoint lets go of all of them with the events
that say so. The pointer itself is never moved on the desktop.

- `expect_screen` names the screen the events are for; when another is
  shown they are refused `screen_changed` and nothing is pushed, so a
  click never lands on the screen that replaced the one it was meant for.
- Events that cannot be read are refused `bad_request`, naming the first
  and what is wrong with it; a finger on a machine with no touch screen
  `unsupported`. Nothing is pushed then either.
- An event that ends the game, such as a click on Exit, ends it before the
  frame that would answer: the link closes instead.

### Frames

- A presented frame is the picture the game drew in its window, copied
  just before it was shown, cursor and all, in the window's own pixels: the
  rectangle hello gives as the canvas's. On the menus that is the 640x480
  canvas scaled into the window, without the bars beside it; in a match,
  drawn at the window's size, it is the whole window. `window_rect` says
  where it lies in the window. A composed frame is the one the game
  composed for the screen before drawing it in the window, 640x480 on the
  menus, as `--snapshot` writes it; in the accelerated tiers it can differ
  from what is shown.
- A frame request is taken at a frame's pump stage and answered at the
  same frame's presented stage, after the frame is drawn and shown, so its
  `frame` and `tick` are those of the frame it gives. While a request for
  a presented frame waits, the game copies each frame it presents
  (`AutomationHost::start_frame_capture`), and stops once it is answered.
  Such a request waits at most 60 frames for the game to present one,
  then is refused `no_window`, as it is in a run without a window.
- A region in the `game` space is in the canvas's pixels, and covers every
  pixel of a presented frame that shows part of it: with the canvas drawn
  at one and a half times its size, its pixel `[0, 0, 1, 1]` is 2x2 pixels
  of the frame. One in the `window` space is in the window's pixels and
  must lie in the frame. A composed frame's regions are in its own pixels.
  A region outside the frame is refused `bad_request` naming `region`, and
  so is an unknown `source`, `format` or `space`, naming it.
- `png` is a PNG file of the pixels, 8-bit RGB with each row stored
  unfiltered and uncompressed, so that the main thread spends little more
  on it than one copy; it is a little larger than the pixels.
- `hash` is the 64-bit FNV-1a hash of the pixels as R, G and B bytes, rows
  from the top, written as 16 lowercase hexadecimal digits, and `mean` is
  each channel's mean over them, rounded down.

## Events

A client that subscribed to them is sent events unasked, among the
answers: `{"event", "seq", "frame", "tick", ...}`, where `seq` numbers
the client's events from 1 and `frame` and `tick` say when the change was
seen. The endpoint learns them by comparing what the game holds at each
frame's pump stage with what it held at the last; while a match loads, when
no frame runs, it learns this machine's progress from the loading screen's
rows (`load_progress`), and a match's end as the match is torn down
(`match_event`). It adds nothing to the simulation to learn them.

| Event | Members | Sent when |
|---|---|---|
| `screen` | `name`, `previous` (null as the client subscribes), `dialogs` (those over the new screen, as `screen` gives them) | the screen shown changes |
| `player` | `change` (`joined`, `left`), `slot`, `name` | a battle-room seat is taken or left, while the room shows; the seats taken as the room first shows count as joined |
| `ready` | `slot`, `name`, `ready` | a seated player's ready flag changes in the battle room |
| `chat` | `where` (`room`, `match`), `from`, `to` (a line to one player), `text` | a chat line (`<Name> text`) reaches the battle room's lines or the match's message log |
| `loaded` | `player`, `slot`, `progress` (0 to 100), `local` | a seated player's load progress changes in the match; while this machine loads, its mean progress over the loading screen's rows, with `player` and `slot` null until the match's players are known |
| `match` | `change` (`started`, `ended`) | the match's screen first shows; the match is torn down after it started |
| `pause` | `paused` | the match's pause changes, whoever paused it |
| `speed` | `speed` | the match's speed changes, whoever set it |
| `alliance` | `from`, `from_slot`, `to`, `to_slot`, `allied` | a seated player allies with another or breaks the alliance |
| `game_over` | `outcome`, `winner` | the match is decided for the local player, as `match` gives them |
| `transfer` | — | never yet: nothing the endpoint reads reports units or resources given; `subscribe` takes the kind |

## The codec

`oa-app-automation-protocol` reads and writes the protocol's frames
(`include/oa/app/automation/protocol.hpp`) and their JSON
(`include/oa/app/automation/json.hpp`). A frame is one ASCII header line, a
JSON part and an optional payload:

```
AUTO/1 <route> <json-length> <payload-length> <crc32>\n
<json-length bytes of UTF-8 JSON><payload-length bytes>
```

- The header is at most 96 bytes, its line feed included. `AUTO/1 ` is the
  framing's name and version.
- The route is 1 to 48 characters of `[a-z0-9._/-]`.
- The lengths are decimal without leading zeros. A JSON part above 1 MiB or
  a payload above 64 MiB is refused before it is read.
- The CRC is CRC-32 (IEEE 802.3) over the JSON part and the payload
  together, as eight lowercase hexadecimal digits.
- The JSON part is one object in UTF-8 without a byte-order mark, or empty;
  the payload is raw bytes the JSON describes.

Frames follow each other without a separator. `FrameReader` takes frames
from a stream as its bytes arrive. Bytes that do not start a well-formed
frame (its header parses, its lengths are within the reader's limits, its
JSON part starts with `{` and its CRC matches) are skipped up to the next
`AUTO/1 ` that does, and the reader reports how many it skipped and the
first problem it met: `noise`, `header`, `crc`, `too_long`, or `stalled`
for a frame cut off where the stream ends. A run of skipped bytes is
reported just before the frame that ends it, so what the reader gives never
depends on how reads split the bytes. One call looks at no more than twice
the largest frame the reader's limits take, whatever it holds: past that it
pauses, and the next call goes on where it stopped.

The JSON reader is strict (RFC 8259, UTF-8, at most 64 arrays and objects
deep) and keeps numbers as written: the protocol's numbers are integers
unless a field says otherwise. The writer writes compactly and escapes `"`,
`\` and the control characters only. What it writes is always UTF-8: every
key and string goes through one function, `unicode_text`, which keeps a text
that is UTF-8 as it is and replaces one that is not, whole, by
`Non-Unicode Text Error::` followed by the standard base64 (RFC 4648, with
padding) of its bytes, from which a client recovers them. The game's own
text in a code page that is not UTF-8 comes out so: the four bytes of
`Jörg` in Latin-1 are written `"Non-Unicode Text Error::SvZyZw=="`.

## Sources

| File | Holds |
|---|---|
| `extension.cpp` | the hooks and `oa_extension_init_automation` |
| `options.cpp` | `--fark`, `--fark-address` and `--fark-file`, their checks, the options a run with the endpoint refuses |
| `token.cpp` | the token and its comparison |
| `endpoint.cpp` | the listener, the connections, the handshake, the order of requests, the answers waiting to be written |
| `requests.cpp` | the table of operations and their handlers, and the work done each time the endpoint is served |
| `controls.cpp` | `controls`, and the dialogs and focus `screen` gives: the automation host's controls, the multiplayer screens' from their panel, and the windows extensions show |
| `input.cpp` | `input`: the SDL events pushed, the keys and buttons held, the answer once they are taken |
| `input_events.cpp` | the device events of an input request, read and checked |
| `frames.cpp` | the frame request |
| `reports.cpp` | what `match` and `room` answer, from a match's world and the lobby (`oa-app-automation-reports`) |
| `events.cpp` | the kinds of event and the watch that learns them (`oa-app-automation-reports`) |
| `serve_reports.cpp` | `match`, `room` and `subscribe`, and the work that sends the events of a frame, a load and a match's end |
| `protocol.cpp`, `json.cpp` | the codec |

The listener and its connections are TCP streams on the loopback address
(`oa/netgame/stream_socket.hpp`), served by the socket driver network play
uses (`src/netgame/socket`): Winsock 2 on Windows from Windows XP on, the
system's sockets elsewhere.

## Tests

- `automation-protocol` reads every frame of `tests/vectors` whole, byte by
  byte and in chunks, each giving what `tests/vectors/vectors.json` says;
  reads back frames the codec writes; feeds the reader malformed input (a
  bad magic, a CRC that does not match, lengths above the limits, a header
  too long or cut short, a JSON part not where the header says, a stream
  that ends inside a frame); checks that one call pauses over headers that
  each announce the same bytes and over long noise, and that limits
  changed under bytes already fed apply to them; checks the JSON reader
  against malformed texts, and the writer against text that is not UTF-8,
  written as the marker and its base64, and against base64's test vectors.
  `tests/vectors/make_vectors.py` writes the vectors and explains
  each, and `automation-protocol-vectors` checks that they are the ones it
  writes.
- `automation-options` parses `--fark`, `--fark-address` and `--fark-file`
  through `oa-game`'s command line, with the addresses and combinations it
  refuses and the engine options a run with the endpoint cannot be used
  with, and checks the token.
- `automation-input` reads the device events of input requests, each kind
  with its fields and their defaults, and refuses those that cannot be
  read: no list, too many events, an unknown kind, missing or out-of-range
  fields, text too long or with a zero character.
- `native-automation-menus` (`tools/check_native_automation.py`) starts the
  game with `--fark` on SDL's dummy drivers, over the installation
  `OA_GAME_DIR` names: the file and its mode, a connection that speaks no
  frames and one whose first frame is larger than a hello closed
  unanswered, a wrong token denied, hello, a second client busy, the main
  menu's screen, the preferences, refusals, the main menu's controls, input
  refused for another screen and for events that cannot be read, a click
  on SINGLE answered once taken and opening Single Player, Load Game's
  message box (no saved games) listed over its dialog and closed by its
  OK, and a hello and
  quit sent at once by a client that then ends its side of the connection,
  both answered, ending the game with status 0; the log names the address
  and never the token.
- `native-automation-input` (`tools/check_native_automation_input.py`)
  drives the game by input alone: input requests sent far faster than the
  game takes them, of which it reads no more than about a frame's worth
  while they wait, so that the sender stalls and the game's memory stays
  flat (not judged in a build with the address sanitizer, whose quarantine
  keeps the memory it frees); a double click on the providers' TCP/IP row placed in the
  window's pixels, keys and text replacing the address in the TCP/IP
  dialog's field, a skirmish started from the menus, its
  camera scrolling while the endpoint holds the right arrow key and
  stopping once it is let go, the match saved from the game menu under a
  typed name, which the save dialog's field holds and Load Game then
  lists, and Yes to the surrender question that quit
  asks ending the game with status 0.
- `native-automation-frame` (`tools/check_native_automation_frame.py`)
  starts the game in the same way on the main menu, in a 640x480 window,
  which the canvas fills, and in a 1280x720 one, where it is drawn larger
  between bars. The presented frame lies where hello says the canvas does
  and, in the first, holds what the composed frame holds but for the few
  pixels the menu animates between two frames; its PNG form holds the same
  picture with every row stored unfiltered; a region of the canvas and of
  the window gives its pixels, and the hash form their FNV-1a hash and
  mean colour; a region of the canvas covers every pixel that shows part
  of it; a region over a bar, and requests with fields it does not take,
  are refused `bad_request`.
- `automation-reports` writes the `match` and `room` answers and runs the
  event watch over a match world and a lobby it builds: every member, the
  outcome and winner, a player's name that is not UTF-8 given as the marker
  and its base64, each kind of event as what it
  follows changes, only the kinds wanted, and the game's bytes unchanged
  by any of it.
- `native-automation-extension-windows`
  (`tools/check_native_automation_windows.py`) starts the game in the same
  way with `--fixture-window`. The fixture extension shows a window on the
  main menu. The endpoint lists that window's button and its clock label
  ahead of the menu's own controls; a click on the button leaves it reading
  pressed, with the main menu still up; and the clock label reads the fixed
  clock's step, one thirtieth of a second, times the frame the answer names,
  before the click and after it. The fixture is a test extension: only a
  build configured with `-DOA_RECORD_EXTENSION_HOOKS=ON` combines it and
  runs this check.
- `native-automation-digest` (`tools/check_native_automation_digest.py`)
  records a network game the game plays with itself
  (`--net-loopback-check`, `--net-record`) and plays it back in the main
  loop on the dummy drivers twice, with the same seed and frames: once
  without `--fark`, and once with it and a client that subscribes to every
  event and asks `screen`, `match` with its `state_hash`, `room` and
  `prefs` at every frame. The two trace streams (`--trace-digest`) must be
  the same over the ticks both reached: the endpoint changes nothing of the
  match. A saved game cannot be played this way yet, since `--fark` refuses
  the headless saved-game run and the main loop loads a saved game only
  through the menus.
