# Placement by Builder

<!-- BEGIN GENERATED: facts -->
| Fact | Value |
| --- | --- |
| Hack id | `units.placement-by-builder` |
| Area | Units (`units`) |
| Scope | sim: part of the profile hash; every machine must agree |
| Runs on | The machine that owns the unit or player concerned. |
| Parameters | 0 |
| Status | implemented |
<!-- END GENERATED: facts -->

## Description

A click on a build-menu item places it on the map or adds it to the queue according to the selected builder, not the item. A mobile builder places everything it can build, mobile units included, as frames on the map; a factory queues everything it can build.

## Configuration example

```yaml
hacks:
  units.placement-by-builder: true
  # Usually switched on together, so the frame's site is tested by the unit's yard:
  units.mobile-unit-yardmap: true
```

## Details

### Behaviour

- When the player clicks an item on a build page, the engine looks at the type of the unit whose build page is shown (the panel unit):
  - a mobile builder (`BMcode` not 0): every item arms placement, so the player chooses a site on the map, and a mobile unit is started there as a frame and built in place like a building;
  - a building, such as a factory (`BMcode=0`): no item arms placement; every click adds to (left button) or takes from (right button) the queue.
- The click plays the same sounds and uses the same queue steps as in 3.1c; only the choice between placement and queue changes.
- The frame of a mobile unit is built by the builder at the site and becomes an ordinary mobile unit when finished.

### Baseline

3.1c decides by the clicked item: a building (`BMcode=0`) arms placement and a mobile unit is queued, whichever builder is selected.

### Network games

The choice is made on the machine of the player who clicks. What it produces is an ordinary build order (build at a site, or a queue change), which every machine runs as it does in 3.1c. The hack is still part of the profile hash, so every machine must have it in the same state.

### Interactions

- [`units.mobile-unit-yardmap`](units.mobile-unit-yardmap.md) gives a mobile unit a yard, so its frame's site is tested by its own cells rather than the default cell.
- [`ui.interface-fixes`](ui.interface-fixes.md) keeps a mobile unit's frame drawn as a frame until it is built.
- [`ui.build-preview`](ui.build-preview.md) and [`units.build-rotation`](units.build-rotation.md) change how the placement cursor is shown and which way a building faces; they apply to placed items as usual.
- [`orders.build-site-kickout`](orders.build-site-kickout.md) changes how the site test treats units standing on the site.

### Implementation notes

- The build-page click is handled in `src/ui/hud/src/order_panel.cpp`; `BuildPanelHost::placement_by_builder` (`src/ui/hud/include/oa/ui/hud/order_panel.hpp`) selects the rule. `src/app/runtime_order_panel.cpp` sets it from the rules record field `units.placement_by_builder.enabled`.
- A placed item becomes a build order through `Match::issue_mobile_build` (`src/sim/match-runtime/src/tick_orders.cpp`), the same order a building placement gives.
- Tested by `ui-hud-order-panel` (`test_build_panel_placement_by_builder`) for the click, and by `match-unit-rules` (`mobile_unit_built_at_a_site`), in which a mobile builder builds a mobile unit at a site into a finished unit.

## Full configuration schema

<!-- BEGIN GENERATED: schema -->
Hack id `units.placement-by-builder`, written under the profile's `hacks` block.

| Write | Means |
| --- | --- |
| `units.placement-by-builder: true` | On. |
| `units.placement-by-builder: false` | Off, the same as leaving it out: 3.1c behaviour. |

### Parameters

This hack has no parameters.

### Presets

This hack has no presets.

### Shorthand

This hack has no parameters to set: write `true` or `false`.

### Every parameter at its default

```yaml
hacks:
  units.placement-by-builder: true
```
<!-- END GENERATED: schema -->
