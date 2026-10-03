# Retarget Out of Range

<!-- BEGIN GENERATED: facts -->
| Fact | Value |
| --- | --- |
| Hack id | `weapons.retarget-out-of-range` |
| Area | Weapons (`weapons`) |
| Scope | sim: part of the profile hash; every machine must agree |
| Runs on | The machine that owns the unit or player concerned. |
| Parameters | 0 |
| Status | implemented |
<!-- END GENERATED: facts -->

## Description

A weapon whose target has moved out of range drops it and picks a target it can reach. In 3.1c the weapon keeps an out-of-range target for as long as it lives (target locking).

## Configuration example

```yaml
hacks:
  weapons.retarget-out-of-range: true
```

## Details

### Behaviour

Each tick a targeting pass visits a share of each player's units in turn (human and computer players alike), and for each weapon slot that may pick its own targets decides whether to keep the current target or look for another. With the hack on:

- When a slot's unit target is out of the weapon's reach, the pass clears only the slot's target index. It does not run the script's `TargetCleared`, and the rest of the pass still judges the old target, so the slot stays empty for that round.
- The next time the pass visits the unit, the empty slot takes a new target in reach as usual.
- A target in reach is kept, as in 3.1c, unless it is allied, a bad target for the slot or already stunned by a paralyzer.
- The pass also visits units whose fire-order value is 3 as well as those set to fire at will (2). A unit with value 3 loses an out-of-reach target, but only fire at will lets the pass give it a new one.

The pass skips disabled slots, slots that do not return fire, dropped weapons and, for human players, command-fire weapons, as in 3.1c.

### Baseline

3.1c visits only fire-at-will units and keeps a slot's enemy target however far it goes, so a weapon stays fixed on a target it cannot hit.

### Network games

The pass runs on the machine that owns the unit; the targets it chooses reach the other machines as usual. The hack is part of the profile hash, so every machine must have it in the same state.

### Interactions

- [`weapons.high-arc-ballistic`](weapons.high-arc-ballistic.md) widens what a ballistic weapon can reach, so fewer targets count as out of range.
- [`orders.selective-weapon-occupy`](orders.selective-weapon-occupy.md) and [`orders.weapons-free-while-busy`](orders.weapons-free-while-busy.md) change which slots are free to pick targets.

### Implementation notes

- `Match::sweep_weapon_targets` in `src/sim/match-runtime/src/targeting.cpp` reads the rules record field `weapons.retarget_out_of_range.enabled`, tests reach with `weapon_can_reach` and clears the slot's target.
- Tested by `match-weapon-rules` (`out_of_reach_targets` and `fire_order_three`, each with the hack off and on).

## Full configuration schema

<!-- BEGIN GENERATED: schema -->
Hack id `weapons.retarget-out-of-range`, written under the profile's `hacks` block.

| Write | Means |
| --- | --- |
| `weapons.retarget-out-of-range: true` | On. |
| `weapons.retarget-out-of-range: false` | Off, the same as leaving it out: 3.1c behaviour. |

### Parameters

This hack has no parameters.

### Presets

This hack has no presets.

### Shorthand

This hack has no parameters to set: write `true` or `false`.

### Every parameter at its default

```yaml
hacks:
  weapons.retarget-out-of-range: true
```
<!-- END GENERATED: schema -->
