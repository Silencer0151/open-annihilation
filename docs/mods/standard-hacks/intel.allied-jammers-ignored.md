# Ignore Allied Jammers

<!-- BEGIN GENERATED: facts -->
| Fact | Value |
| --- | --- |
| Hack id | `intel.allied-jammers-ignored` |
| Area | Vision and Radar (`intel`) |
| Scope | sim: part of the profile hash; every machine must agree |
| Runs on | Each machine, for its own view. |
| Parameters | 1 |
| Status | implemented |
<!-- END GENERATED: facts -->

## Description

Radar and sonar jammers of players you ally no longer hide units from your own radar picture. A watching player sees no jamming at all.

## Configuration example

```yaml
hacks:
  intel.allied-jammers-ignored: true
```

```yaml
hacks:
  intel.allied-jammers-ignored:
    # While viewing another player, apply only the alliance test.
    view-switch-branch: true
```

## Details

### Behaviour

During the contact scan a live, active jammer owned by someone other than the viewing player normally jams. With the hack on, such a jammer does not jam when:

- its owner is a player the viewing player allies (the viewer's alliance row names the owner; an owner who allies the viewer one way still jams), or
- the local player is a watcher: watchers, and replays watched as a watcher, see no jamming at all.

With `view-switch-branch: true` there is one more rule. While the viewing player is not the local player, in a game with mapping or line of sight on, only the alliance test applies: the watcher exception is skipped. With `view-switch-branch: false` (the default) the watcher exception applies in every case.

The hack changes the radar picture only: what is shown on radar and the minimap. Targeting is not affected.

### 3.1c behaviour

In 3.1c every active jammer that the viewing player does not own jams, allied or not, and watchers see jamming like any player.

### Network games

Each machine works out jamming for its own view. Every machine must agree that the hack is on and on `view-switch-branch`; it is part of the profile hash.

### Interactions

- With [intel.allied-los-sharing](intel.allied-los-sharing.md) the scan runs once per ally, each pass as that ally, and this hack's test is made in each pass with that ally as the viewer.

### Implementation notes

- The test is `Match::jammer_jams` in `src/sim/match-runtime/src/intel.cpp`, called from the jammer walk of `Match::scan_contacts_for` in `src/sim/match-runtime/src/tick_detection.cpp`.
- It reads the rules record fields `rules.intel.allied_jammers_ignored.enabled` and `view_switch_branch`.
- Tests: `match-intel` (`src/sim/match-runtime/tests/intel_test.cpp`: `allied_jammers_do_not_jam`, `watchers_see_no_jamming`, the latter covering the view-switch branch with and without mapping or line of sight).

## Full configuration schema

<!-- BEGIN GENERATED: schema -->
Hack id `intel.allied-jammers-ignored`, written under the profile's `hacks` block.

| Write | Means |
| --- | --- |
| `intel.allied-jammers-ignored: true` | On, every parameter at its default. |
| `intel.allied-jammers-ignored: {view-switch-branch: false}` | On, the parameters named set and the rest at their defaults. |
| `intel.allied-jammers-ignored: false` | Off, the same as leaving it out: 3.1c behaviour. |

### Parameters

| Parameter | Type | Unit | Allowed | Length | Baseline (3.1c) | Default | Adjustable | Scope | Overridden by |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `view-switch-branch` | `bool` | - | `true`, `false` | - | `false` | `false` | `fixed` | `sim` | - |

Adjustable: `fixed`: only the profile sets it.

### Presets

This hack has no presets.

### Shorthand

This hack takes no shorthand: write `true`, `false` or a parameter map.

### Every parameter at its default

```yaml
hacks:
  intel.allied-jammers-ignored:
    view-switch-branch: false
```
<!-- END GENERATED: schema -->
