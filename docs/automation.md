# The automation endpoint

A program on the same machine as the game can drive it and watch it
through the automation endpoint: a service on the loopback address that
the game, `open-annihilation`, serves when it is started with `--fark`. The
program reads what the game shows and holds (the screen, its controls, the
frames presented, the preferences, the running match and the battle room),
hands it keys, text, pointer and finger events as a device does, and is
told of changes as they happen. It is meant for programs that test the
game or play scripted journeys through it, on every platform the game
runs on, in the same build players run.

It is off unless `--fark` is on the command line. A player never needs it.

## Contents

- [Turning it on](#turning-it-on)
- [Connecting](#connecting)
- [Requests](#requests)
- [Input](#input)
- [Events](#events)
- [What it never does](#what-it-never-does)
- [In a battle room](#in-a-battle-room)
- [Checks](#checks)

## Turning it on

```
--fark                     serve the automation endpoint
--fark-address ADDRESS     where: 127.0.0.1:PORT or [::1]:PORT; port 0, the default
                           (127.0.0.1:0), lets the system choose
--fark-file PATH           where the address and token are written; by default
                           automation.json in the folder of the preferences file
```

- Only the command line turns it on: no setting, environment variable or
  file does.
- Only the loopback address can be served. Any other is refused with
  `automation: only a loopback address can be served`, and the game exits
  as for any refused option. `--fark-address` or `--fark-file` without
  `--fark` is refused too.
- The endpoint is served from the game's main loop, so `--fark` cannot go
  with an option that runs no main loop or runs its own:
  `--headless-check` and the options that imply it, the `--check-*`
  options, `--benchmark`, `--match-ticks`, `--render-script`,
  `--generate-script` and `--showcase`. It goes with `--host` and
  `--join`, so a game can start in a battle room and still be driven.
- With no window to open, start the game on SDL's dummy drivers
  (`SDL_VIDEO_DRIVER=dummy SDL_AUDIO_DRIVER=dummy`): the endpoint works
  the same, frames included.
- While `--fark` is on, the game keeps running every frame when its window
  is not the active one, so the endpoint is always served.

As it starts, the game listens and writes the file, whole (first beside
it, then renamed over it) and readable only by its owner where the system
has such modes:

```json
{"version":1,"address":"127.0.0.1:53122","token":"9c1f…(32 hex digits)","pid":41877}
```

The token is 128 random bits from the system's generator, new each run.
The log says `automation: serving on 127.0.0.1:53122` and where the file
is, and never holds the token. [settings.md](settings.md#where-the-settings-are-kept)
says where the preferences file, and so the default file, is kept;
`--preferences-file` names another.

## Connecting

The endpoint speaks the automation protocol over one TCP connection. Every
message, both ways, is a frame: a header line, a JSON object and an
optional payload of raw bytes.

```
AUTO/1 - <json-length> <payload-length> <crc32>\n
<json-length bytes of UTF-8 JSON><payload-length bytes>
```

The lengths are in decimal, and the CRC is CRC-32 (IEEE 802.3) over the
JSON and the payload together, in eight lowercase hexadecimal digits. A
JSON part above 1 MiB or a payload above 64 MiB is refused.
[src/app/automation](../src/app/automation/README.md#the-codec) gives the
framing in full.

- One client at a time. A connection whose first bytes are not `AUTO/1 `
  is closed unanswered.
- The first request is `hello` with the file's token:
  `{"id": 1, "op": "hello", "versions": [1], "client": "NAME", "token": "…"}`.
  Its JSON is at most 4 KiB, with no payload, or the connection is closed
  unanswered. A wrong token is answered `denied` and the connection
  closed. While a client is connected, a second connection is answered
  `busy` and closed.
- A request is `{"id", "op", ...}`. Its answer is
  `{"id", "ok": true, "frame", "tick", ...}`, where `frame` counts the
  frames served and `tick` is the running match's, or
  `{"id", "ok": false, "error": {"code", "message"}}`. Requests are
  answered in order. A client may send several requests at once and then
  end its side of the connection: each is still answered before the
  connection closes. The game reads no more from a client that sends
  faster than it answers until it has caught up.
- Every text the game gives is UTF-8. Text it holds in a code page that is
  not, as an installation whose files use another code page or another
  player's game can give it (captions, list rows, names, chat,
  preferences), is replaced whole by `Non-Unicode Text Error::` followed
  by the standard base64 (RFC 4648, with padding) of its bytes, from which
  the client recovers them: the four bytes of `Jörg` in Latin-1 come as
  `"Non-Unicode Text Error::SvZyZw=="`.

A short client in Python:

```python
import json, socket, zlib

def frame(message):
    body = json.dumps(message).encode()
    return b"AUTO/1 - %d 0 %08x\n" % (len(body), zlib.crc32(body)) + body

def read(sock):
    header = b""
    while not header.endswith(b"\n"):
        header += sock.recv(1)
    _, _, json_length, payload_length, _ = header.split()
    size = int(json_length) + int(payload_length)
    data = b""
    while len(data) < size:
        data += sock.recv(size - len(data))
    return json.loads(data[:int(json_length)]), data[int(json_length):]

endpoint = json.load(open("automation.json"))
host, _, port = endpoint["address"].rpartition(":")
sock = socket.create_connection((host.strip("[]"), int(port)))
sock.sendall(frame({"id": 1, "op": "hello", "versions": [1], "client": "example",
                    "token": endpoint["token"]}))
print(read(sock)[0])
sock.sendall(frame({"id": 2, "op": "screen"}))
print(read(sock)[0]["screen"])  # main_menu
```

## Requests

| Request | What it answers |
|---|---|
| `hello` | the protocol's version, the engine's, the window's size, where the game's 640x480 canvas lies in it, the tick rate (30) |
| `screen` | the screen shown (`main_menu`, `skirmish`, `mp_battleroom`, `match` and the like), the dialogs over it, the control that takes the keys, the pointer, and in a match where the camera looks |
| `controls` | an extension's windows first, then every control of the panel that takes the pointer, a dialog's first: its name, its kind, the extension window it belongs to (or none), its place on the canvas and in the window, its state and text, and a list's rows |
| `input` | once the game has taken the events it was given, the frame and tick at which it took them ([below](#input)) |
| `frame` | the frame the game presented in its window, or the one it composed, whole or a region of it, as RGB pixels, a PNG file, or a hash and mean colour |
| `prefs` | the preferences as the game holds them now, and their file |
| `match` | the running match: pause, speed, each player's name, side, team, allies, units, kills and losses, the outcome and winner; with `state_hash`, the digest of its state that a saved game carries |
| `room` | the battle room: the game's name, its host, the seated players with their ready flags, sides, colours, teams and pings, the map, the options and the chat |
| `subscribe` | the kinds of event the client is sent from then on |
| `quit` | nothing more: the game then quits as a player closing its window does |

[src/app/automation](../src/app/automation/README.md#the-protocol) gives
every request's fields and answers, and the errors it is refused with.

## Input

`input` carries `events`: `key_down` and `key_up` with the key's SDL
scancode and key code, `text`, `pointer_move`, `button_down` and
`button_up` with the button and the clicks, `wheel`, and `finger_down`,
`finger_move` and `finger_up`. Points are on the canvas (`space`
`game`, the default) or in the window's pixels (`window`).

- The events go onto SDL's event queue, and the game takes them in its
  next frame exactly as it takes a keyboard's, a mouse's or a touch
  screen's. The endpoint never calls the game's handlers, never gives
  orders and never types console lines on its own.
- The answer comes once the game has taken them: `consumed`, with the
  frame and tick.
- With `expect_screen`, the events are refused `screen_changed`, and none
  is pushed, when another screen shows by then, so that a click never
  lands on the screen that replaced the one it was meant for.
- Keys and buttons the events hold down count as held for the game's
  reads of what is held (the arrow keys that scroll the camera, Shift,
  Ctrl, a held button) until an event lets them go or the client leaves.
  The desktop's pointer is never moved.
- Text is typed only into a field that takes it, as SDL types it.

## Events

After `subscribe`, the client is sent events unasked, among the answers:
`{"event", "seq", "frame", "tick", ...}`. The kinds are `screen`,
`player` (a battle-room seat taken or left), `ready`, `chat`, `loaded`,
`match` (started or ended), `pause`, `speed`, `alliance` and `game_over`.
The endpoint learns them by comparing what the game holds from one frame
to the next; nothing is added to the game to produce them.

## What it never does

- **Change the game.** It reads the match and the battle room and changes
  neither. A match played with the endpoint on, and a client reading it at
  every frame, plays out tick for tick as it does without it.
- **Change what goes to other players.** Nothing the endpoint reads or is
  given is sent over a network game's connections, and the network
  protocol is the same with it on. The one difference the other players
  see is the engine's chat line in the battle room
  ([below](#in-a-battle-room)).
- **Run without `--fark`.** Without it no socket is opened and no file is
  written.
- **Listen beyond this machine.** It serves the loopback address only.
- **Read or write files, start programs or reach the network** for a
  client: it has no request that does.
- **Hold the game up.** It is served on the game's own thread at fixed
  points of each frame, takes requests for at most a millisecond each
  time, and never waits.
  A client that stops reading is dropped once 68 MiB of answers wait for
  it (`automation: client dropped: not reading` in the log).

## In a battle room

Every player in a battle room is told when a game there can be driven: the
engine's line in the room's chat ends with ` REMOTED` while `--fark` is
on, as it ends with ` DEV MODE` while Developer Mode is:

```
[Engine: OpenAnnihilation v0.7.2 REMOTED]
[Engine: OpenAnnihilation v0.7.2 DEV MODE REMOTED]
```

## Checks

The engine's tests of the endpoint are `automation-protocol`,
`automation-options`, `automation-input`, `automation-reports` and the
native checks `native-automation-menus`, `native-automation-input`,
`native-automation-frame` and `native-automation-digest`, which run the
game with `--fark` on the dummy drivers over an installed game.
[testing.md](development/testing.md#the-automation-endpoint) says how to run them,
and [src/app/automation](../src/app/automation/README.md#tests) what each
checks.
