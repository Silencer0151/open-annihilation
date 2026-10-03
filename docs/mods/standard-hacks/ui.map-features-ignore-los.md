# Map Features Always Drawn

<!-- BEGIN GENERATED: facts -->
| Fact | Value |
| --- | --- |
| Hack id | `ui.map-features-ignore-los` |
| Area | Interface (`ui`) |
| Scope | view: local display only; each player may differ |
| Runs on | Each machine, for its own view. |
| Parameters | 1 |
| Status | implemented |
<!-- END GENERATED: facts -->

## Description

Draws the features a map places, such as rocks and trees, even where the viewer has no line of sight, instead of hiding them under the fog as 3.1c does for features that hide under the gray fog.

## Configuration example

```yaml
hacks:
  ui.map-features-ignore-los: true
```

```yaml
hacks:
  # Shorthand for feature-owner: 10 keeps 3.1c's sight test, so the hack
  # is listed but changes nothing.
  ui.map-features-ignore-los: 10
```

## Details

Every map cell records an owner slot, 0 to 15, for the feature on it. 3.1c
gives the features the map places owner slot 10, which matches no player.
When the viewer has no line of sight to a cell, a feature that hides under
the gray fog is drawn only when its owner slot is the viewer's own slot.

With the hack on, a feature the map placed is treated as if its owner slot
were `feature-owner`, and a feature whose owner slot is 11 is drawn
whatever the line of sight. The value therefore decides the effect:

- `11` (the default): every player sees the map's features everywhere.
- `10`: 3.1c's owner; the sight test applies as in 3.1c.
- A player slot (0 to 9): that player alone sees the map's features out of
  sight; the others see them as in 3.1c.

The rule applies only while the cell still holds the feature the map put
there. A feature that replaced it, such as a wreck or a new tree, keeps its
own owner slot and the usual sight test. Features that do not hide under
the gray fog are drawn as in 3.1c whatever this hack says.

3.1c hides fog-hidden map features outside line of sight.

### In a network game

This is a view rule: it changes only what this machine shows and how its own input works, never what another machine is told. It is not part of the match hash, so players in one game may have it on, off or set differently without breaking the game.

The cells' owner slots themselves are not changed: they keep 3.1c's values,
which the match hashes and saves, so a save and the network state are the
same with the hack on or off.

### Related hacks

- [ui.megamap](ui.megamap.md) with `feature-blobs` marks the map's features
  on the megamap whatever the line of sight.

### Implementation notes

- The engine reads `UiRules::map_features_ignore_los` (fields `enabled`,
  `feature_owner`) in `src/app/runtime_fog.cpp`, where it decides whether a
  fog-hidden feature is drawn. A feature counts as the map's while its cell
  holds the feature the prepared map put there.
- The decision is `feature_drawn_without_sight` with its `FeatureOwnerRule`
  in `src/present/world-renderer/include/oa/present/world_renderer/world_fog.hpp`.
- Test: `world-fog` (`feature_owner_rule_skips_the_sight_test` in
  `src/present/world-renderer/tests/world_fog_test.cpp`) covers 3.1c's rule,
  owner 11, owner 10 under the rule, a player slot and a replaced feature.

## Full configuration schema

<!-- BEGIN GENERATED: schema -->
Hack id `ui.map-features-ignore-los`, written under the profile's `hacks` block.

| Write | Means |
| --- | --- |
| `ui.map-features-ignore-los: true` | On, every parameter at its default. |
| `ui.map-features-ignore-los: {feature-owner: 11}` | On, the parameters named set and the rest at their defaults. |
| `ui.map-features-ignore-los: {preset: baseline}` | On, starting from a preset; parameters named beside `preset` replace its values. |
| `ui.map-features-ignore-los: 11` | On, the shorthand: a bare value sets `feature-owner`. |
| `ui.map-features-ignore-los: false` | Off, the same as leaving it out: 3.1c behaviour. |

### Parameters

| Parameter | Type | Unit | Allowed | Length | Baseline (3.1c) | Default | Adjustable | Scope | Overridden by |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `feature-owner` | `int` | player slot | `0` to `15` | - | `10` | `11` | `fixed` | `view` | - |

Adjustable: `fixed`: only the profile sets it.

### Presets

| Preset | Values |
| --- | --- |
| `baseline` | `{feature-owner: 10}` |

`baseline` sets every parameter to its 3.1c value: the hack is on and plays as 3.1c.

### Shorthand

The hack has one parameter, so a bare value sets `feature-owner`: `ui.map-features-ignore-los: 11` is `ui.map-features-ignore-los: {feature-owner: 11}`.

### Every parameter at its default

```yaml
hacks:
  ui.map-features-ignore-los:
    feature-owner: 11
```
<!-- END GENERATED: schema -->
