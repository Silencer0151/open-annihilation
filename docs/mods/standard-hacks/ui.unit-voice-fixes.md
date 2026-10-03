# Unit Voice Fixes

<!-- BEGIN GENERATED: facts -->
| Fact | Value |
| --- | --- |
| Hack id | `ui.unit-voice-fixes` |
| Area | Interface (`ui`) |
| Scope | view: local display only; each player may differ |
| Runs on | Each machine, for its own view. |
| Parameters | 3 |
| Status | implemented |
<!-- END GENERATED: facts -->

## Description

Units speak at better moments: a builder reclaiming a feature says "working" once instead of at every reclaim step, an aircraft reclaiming a unit speaks when it starts reclaiming instead of as it sets off, and a landing that finds no free pad may use a voice other than "cannot comply".

## Configuration example

```yaml
hacks:
  ui.unit-voice-fixes: true
```

```yaml
hacks:
  ui.unit-voice-fixes:
    landing-fail-voice: 7   # keep 3.1c's "cannot comply" for failed landings
```

## Details

Each parameter changes one voice:

- **`reclaim-voice-once`**: a ground builder reclaiming a feature speaks the "work started" voice once, as the reclaim begins, and then reclaims in the order's next stage without speaking. 3.1c speaks it again at every reclaim step.
- **`vtol-reclaim-voice-at-start`**: an aircraft reclaiming a unit says nothing as it sets off. It speaks when it arrives and starts reclaiming, then moves into a stage of its own where it reclaims without another word. 3.1c speaks as the aircraft sets off.
- **`landing-fail-voice`**: the speech category of the three landing failures: "Landing failed", "Landing aborted: all pads are occupied" and "Landing aborted: no pads available". 3.1c uses 7, "cannot comply"; the default 6 is the voice a unit gives when an order is done. Any category from 0 to 255 may be named.

**Quirk the engine reproduces.** Under `reclaim-voice-once` the silent stage moves straight on to collecting the feature once the reclaim time has run out. 3.1c takes one more reclaim step first, so with this parameter on the feature is collected one reclaim step sooner.

**3.1c behaviour.** With the hack off, or with the `baseline` preset, the voices are 3.1c's: "working" at every feature reclaim step, the aircraft's voice on setting off, and "cannot comply" for landing failures.

**Network play.** Voices play only on the machine that hears them, and the parameters are not part of the network hash. They are fixed by the profile, so every player of a profile hears the same. The one-step-sooner feature reclaim changes order timing on the machine that runs the builder's orders.

**Interactions.** [ui.effects-tweaks](ui.effects-tweaks.md) is carried to the match in the same display rules record.

### Implementation notes

- The profile record is `ModProfile::ui.unit_voice_fixes` (`reclaim_voice_once`, `vtol_reclaim_voice_at_start`, `landing_fail_voice`). `match_display_rules` in `src/app/view_rules.cpp` copies it into the match's `sim::match_runtime::DisplayRules` (`src/sim/match-runtime/include/oa/sim/match_runtime.hpp`), whose defaults are 3.1c's.
- Feature reclaim reads it in `src/sim/match-runtime/src/tick_missions_ground.cpp`. Aircraft reclaim reads it in `src/sim/match-runtime/src/tick_missions_vtol_build.cpp`. Landing reads it in `src/sim/match-runtime/src/tick_missions_vtol.cpp`.
- Tests: `app-view-rules` (the profile to display rules), `match-missions-ground` (one voice, the shorter reclaim), `match-missions-vtol-build` (voice on arrival and the extra stage), `match-missions-vtol` (landing voice).
- **Known limit:** the hack has view scope, yet `reclaim-voice-once` changes when a feature is collected. That timing is not covered by the network hash.

## Full configuration schema

<!-- BEGIN GENERATED: schema -->
Hack id `ui.unit-voice-fixes`, written under the profile's `hacks` block.

| Write | Means |
| --- | --- |
| `ui.unit-voice-fixes: true` | On, every parameter at its default. |
| `ui.unit-voice-fixes: {reclaim-voice-once: true}` | On, the parameters named set and the rest at their defaults. |
| `ui.unit-voice-fixes: {preset: baseline}` | On, starting from a preset; parameters named beside `preset` replace its values. |
| `ui.unit-voice-fixes: false` | Off, the same as leaving it out: 3.1c behaviour. |

### Parameters

| Parameter | Type | Unit | Allowed | Length | Baseline (3.1c) | Default | Adjustable | Scope | Overridden by |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `reclaim-voice-once` | `bool` | - | `true`, `false` | - | `false` | `true` | `fixed` | `view` | - |
| `vtol-reclaim-voice-at-start` | `bool` | - | `true`, `false` | - | `false` | `true` | `fixed` | `view` | - |
| `landing-fail-voice` | `int` | voice slot | `0` to `255` | - | `7` | `6` | `fixed` | `view` | - |

Adjustable: `fixed`: only the profile sets it.

### Presets

| Preset | Values |
| --- | --- |
| `baseline` | `{reclaim-voice-once: false, vtol-reclaim-voice-at-start: false, landing-fail-voice: 7}` |

`baseline` sets every parameter to its 3.1c value: the hack is on and plays as 3.1c.

### Shorthand

This hack takes no shorthand: write `true`, `false` or a parameter map.

### Every parameter at its default

```yaml
hacks:
  ui.unit-voice-fixes:
    reclaim-voice-once: true
    vtol-reclaim-voice-at-start: true
    landing-fail-voice: 6
```
<!-- END GENERATED: schema -->
