# Cloak After Build

<!-- BEGIN GENERATED: facts -->
| Fact | Value |
| --- | --- |
| Hack id | `units.init-cloaked-after-build` |
| Area | Units (`units`) |
| Scope | sim: part of the profile hash; every machine must agree |
| Runs on | The machine that owns the unit or player concerned. |
| Parameters | 0 |
| Status | implemented |
<!-- END GENERATED: facts -->

## Description

A unit still under construction neither cloaks nor pays cloak upkeep, so the nanoframe of a unit that starts cloaked stays visible until it is finished. In 3.1c such a nanoframe cloaks and pays from the start.

## Configuration example

```yaml
hacks:
  units.init-cloaked-after-build: true
```

The hack takes no parameters.

## Details

**Behaviour.** Each economy update of a player goes through that player's units. In 3.1c, a unit whose cloak is running pays its cloak upkeep and is cloaked when the payment succeeds, unless its cloak is locked or a decloak hold is still on. The upkeep is the type's cloak cost, or its moving cloak cost while it moves. With the hack on, a unit with any build left skips that step: it pays nothing and is not cloaked. It cloaks on the first economy update after it is finished.

**Quirk the engine reproduces.** The test reads the bits of the unit's build fraction left, so a value of negative zero still counts as build left.

**Who it affects.** Any unit with build left, most visibly the nanoframes of types whose definition starts cloaked (`init_cloaked`). Finished units cloak and pay as in 3.1c.

**3.1c behaviour.** A nanoframe whose cloak is running cloaks and pays upkeep while it is being built.

**Network play.** The machine that owns the unit runs its player's economy and decides whether it cloaks; other machines see the result. The hack is part of the network hash.

### Implementation notes

- The profile record is `rules().units.init_cloaked_after_build.enabled`.
- The cloak step of `Match::update_player_economy` in `src/sim/match-runtime/src/tick_economy.cpp` reads it.
- Tests: `match-economy-rules` (`cloak_waits_for_the_build`) covers a frame with the hack on and off, a finished unit, and negative zero.

## Full configuration schema

<!-- BEGIN GENERATED: schema -->
Hack id `units.init-cloaked-after-build`, written under the profile's `hacks` block.

| Write | Means |
| --- | --- |
| `units.init-cloaked-after-build: true` | On. |
| `units.init-cloaked-after-build: false` | Off, the same as leaving it out: 3.1c behaviour. |

### Parameters

This hack has no parameters.

### Presets

This hack has no presets.

### Shorthand

This hack has no parameters to set: write `true` or `false`.

### Every parameter at its default

```yaml
hacks:
  units.init-cloaked-after-build: true
```
<!-- END GENERATED: schema -->
