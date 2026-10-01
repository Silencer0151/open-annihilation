# Unit spatial registration

This component registers units in the map's spatial grid and keeps yard
occupancy. `register_unit` timestamps an attached object, moves the unit
between the 128-world-unit spatial bucket lists through `move_bucket`, and
writes its footprint to the ground or air word of each terrain plot.
`occupy` keeps an occupied word after the usual collision branch, setting the
unit flag bits `collision_other` (`0x04000000`) and `collision_self`
(`0x08000000`). An existing unit whose owner status is 3 reverses the bits and
permits the new unit to replace the plot word.

Occupancy kinds 1 and 2 are the plot's `ground_unit` and `air_unit` words
(`MapPlot`). Kind zero performs spatial membership only, covering ordinary
flying units. The `0x20000000` masked-footprint branch reads a per-unit-type
footprint mask, chooses bit 4 for a closed yard or bit 2 for an open yard, and
preserves mask bit 1 as plot flag 2. `change_yard` first rejects a requested state when an
active cell contains a foreign unit, marks the unit for an occupancy update,
changes the yard state, and runs `update_occupancy`: newly active cells use
normal collision insertion and inactive cells clear only this unit's own ID.
The plot-height and movement-map refreshes remain explicit host callbacks.
The callback retains the full script integer: validation treats every nonzero
value as the open mask, while the stored state uses only bit zero, so an even
nonzero value validates as open and stores closed, as in 3.1c.

World vectors and unit IDs are checked before access. Broken intrusive lists,
invalid IDs, inconsistent dimensions, and spatial bucket coordinates outside
configured storage return explicit errors.

The mobile, nonzero-`BMCode` placement test is exposed by `can_occupy`. It
preserves the strict edge bounds, the mode-2 outside exception, the
blocking-feature and occupant tests, signed water-depth comparisons, and the
separate land/water slope limits. Static zero-`BMCode` types use the larger
yard-map test; this API returns `nullopt` at that boundary because moving
commanders and other mobile types do not use it.

Every write of a plot's ground or air word (by `occupy`, a yard closing and
`remove_occupancy`) lists the plot's index in `World::written_occupants`, so
that a copy of the words, such as the match's `MapPlot` table, takes only the
plots written since it last emptied the list; past
`written_occupant_capacity` (4096) writes the list marks itself lost, and the
copy takes every plot.

`remove_occupancy` clears only matching ground or air owners, restores masked
plot flags, consumes collision bits, and visits spatial buckets X-outer,
Z-inner. It includes each parent's one-level `attach_first_child` chain and
runs `update_occupancy` for intersecting units. Object-backed removal
advances the object tick and requires the host to invalidate the
32-record movement-map cache; objectless removal uses the plain footprint
refresh callback.
