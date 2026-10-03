# Clear Session Description

<!-- BEGIN GENERATED: facts -->
| Fact | Value |
| --- | --- |
| Hack id | `network.session-desc-clear` |
| Area | Network (`network`) |
| Scope | sim: part of the profile hash; every machine must agree |
| Runs on | The host's machine. |
| Parameters | 0 |
| Status | implemented |
<!-- END GENERATED: facts -->

## Description

When the host publishes the session description again, for example
after renaming the game or changing the map in the battle room, the
description carries no password. The password is never sent a second
time.

## Configuration example

```yaml
hacks:
  network.session-desc-clear: true
```

## Details

### Behaviour

The host publishes a session description when the game is created. It
publishes the description again whenever the battle room changes the
game's name or map. Under the rule, each republication:

- carries an empty password,
- sets the description's password field to 0, and
- does not set the "password required" flag.

The engine's session also takes the empty password as its own. Once the
host has published the description again, later joiners are not asked
for a password.

The first publication, made when the session opens, is unchanged.

### Baseline (3.1c)

Each republication of the description carries the session's password
again, and the session keeps it.

### Network games

Only the host publishes the session description, so only the host's
machine acts on the rule. It is part of the profile hash, so every
machine must have the same setting.

### Interactions

None with other hacks.

### Implementation notes

- The profile's `network.session_desc_clear.enabled` becomes
  `WireRules::clear_session_password` in
  `src/app/netgame/wire_rules_binding.cpp`.
- `src/netgame/match/src/session_lobby.cpp` copies it to
  `Session::publish_without_password`.
- `session_update_game_info` in `src/netgame/session/session.cpp` and
  `engine_set_session_desc` in `src/netgame/dplay/engine.cpp` apply it.
  The engine-side field is `Engine::publish_without_password`.
- Tests:
  - `join_and_description_messages_carry_what_the_game_sends` in
    `net-dplay-protocol` (`src/netgame/tests/dplay_protocol_test.cpp`)
    checks that the republished description has a password field of 0
    and no "password required" flag.
  - `every_rule_maps` in `netgame-wire-rules` checks the binding.

## Full configuration schema

<!-- BEGIN GENERATED: schema -->
Hack id `network.session-desc-clear`, written under the profile's `hacks` block.

| Write | Means |
| --- | --- |
| `network.session-desc-clear: true` | On. |
| `network.session-desc-clear: false` | Off, the same as leaving it out: 3.1c behaviour. |

### Parameters

This hack has no parameters.

### Presets

This hack has no presets.

### Shorthand

This hack has no parameters to set: write `true` or `false`.

### Every parameter at its default

```yaml
hacks:
  network.session-desc-clear: true
```
<!-- END GENERATED: schema -->
