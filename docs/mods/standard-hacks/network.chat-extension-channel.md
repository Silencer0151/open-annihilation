# Chat Extension Channel

<!-- BEGIN GENERATED: facts -->
| Fact | Value |
| --- | --- |
| Hack id | `network.chat-extension-channel` |
| Area | Network (`network`) |
| Scope | sim: part of the profile hash; every machine must agree |
| Runs on | Every machine in the game. |
| Parameters | 1 |
| Status | implemented |
<!-- END GENERATED: facts -->

## Description

Chat records whose text starts with a zero byte carry private binary messages between machines, such as integrity checks and votes to reject a player. They are handed to their handlers and never shown as chat.

## Configuration example

```yaml
hacks:
  network.chat-extension-channel: true
```

```yaml
hacks:
  # Accept only integrity-check messages; drop every other private message.
  network.chat-extension-channel: integrity-only
```

## Details

### Behaviour

A private message is one whole 65-byte chat record (type `0x05`):

| Offset | Holds |
| --- | --- |
| +0 | `0x05`, the chat record type |
| +1 | `0x00`, the zero byte that marks a private message |
| +2 | the sub-id: `0x2b` integrity check, `0x2c` vote to reject a player |
| +3 | a 16-bit length word, always `0x0041` |
| +5 | the operation |
| +6 | the payload, zero padded to the end of the record |

A reply in two parts travels as two such records back to back.

With the hack on, a received chat record whose second byte is zero goes to the private channel and is never shown as chat, in the battle room or in the game. It is decoded and then:

- `framing: sub-id-dispatch` hands integrity-check messages and votes to their handlers;
- `framing: integrity-only` hands on only integrity-check messages whose operation is 1 to 7, `0x20` or `0x21`;
- anything else, a record shorter than 65 bytes, or one whose length word is not `0x0041` is dropped unseen.

Ordinary chat lines are unchanged. `framing: none` keeps 3.1c behaviour while the hack is on.

### 3.1c behaviour

3.1c reads every chat record as text. A record whose text starts with a zero byte is an empty line.

### Network games

The channel is a wire format, so every machine in the game must use the same `framing`; it is part of the profile hash. A machine without it shows the private records as empty chat lines.

### Interactions

- [network.vercheck](network.vercheck.md): the integrity check rides on this channel and is off when the channel is `none`.
- [network.vote-reject](network.vote-reject.md): votes need `framing: sub-id-dispatch`.

### Implementation notes

- The record format, decoding and the acceptance rule are in `src/netgame/include/oa/netgame/private_channel.hpp` and `src/netgame/wire/private_channel.cpp` (`is_private_chat`, `decode_private_message`, `encode_private_message`, `private_message_accepted`).
- Received records are routed in `src/netgame/match/src/net_match.cpp` (`dispatch_record`, `handle_private`); the battle room drops them in `src/ui/frontend-multiplayer/src/battleroom.cpp`.
- The profile's `network.chat_extension_channel.framing` becomes `WireRules::private_channel` in `src/app/netgame/wire_rules_binding.cpp` (`wire_rules_of`).
- Tests: `net-wire-rules` (`src/netgame/tests/wire_rules_test.cpp`: `captured_challenge_decodes`, `captured_vote_decodes`, `malformed_private_records`), `netgame-wire-rules` (`src/app/netgame/tests/wire_rules_test.cpp`: `every_rule_maps`, `rules_need_their_channel`) and `net-match` (`src/netgame/match/tests/net_match_test.cpp`, `private_lines_stay_hidden`).

## Full configuration schema

<!-- BEGIN GENERATED: schema -->
Hack id `network.chat-extension-channel`, written under the profile's `hacks` block.

| Write | Means |
| --- | --- |
| `network.chat-extension-channel: true` | On, every parameter at its default. |
| `network.chat-extension-channel: {framing: sub-id-dispatch}` | On, the parameters named set and the rest at their defaults. |
| `network.chat-extension-channel: {preset: baseline}` | On, starting from a preset; parameters named beside `preset` replace its values. |
| `network.chat-extension-channel: sub-id-dispatch` | On, the shorthand: a bare value sets `framing`. |
| `network.chat-extension-channel: false` | Off, the same as leaving it out: 3.1c behaviour. |

### Parameters

| Parameter | Type | Unit | Allowed | Length | Baseline (3.1c) | Default | Adjustable | Scope | Overridden by |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `framing` | `enum` | - | `none`, `sub-id-dispatch`, `integrity-only` | - | `none` | `sub-id-dispatch` | `fixed` | `sim` | - |

Adjustable: `fixed`: only the profile sets it.

### Presets

| Preset | Values |
| --- | --- |
| `baseline` | `{framing: none}` |

`baseline` sets every parameter to its 3.1c value: the hack is on and plays as 3.1c.

### Shorthand

The hack has one parameter, so a bare value sets `framing`: `network.chat-extension-channel: sub-id-dispatch` is `network.chat-extension-channel: {framing: sub-id-dispatch}`.

### Every parameter at its default

```yaml
hacks:
  network.chat-extension-channel:
    framing: sub-id-dispatch
```
<!-- END GENERATED: schema -->
