# Visibility state

Terrain speed and the line-of-sight stamps. Terrain speed scans the unit
footprint with unsigned 16-bit accumulation, then treats that word as signed
for both `type_speed * sum` and SetSpeed.

Every stamp goes through a `SightStamp` descriptor and a `SightContext`: the
game's visibility rules and sea level, the standard masks, the altitude
heights and ray table, each player's coverage grid and the shared mapped
words. `sight_context_error` checks a context once at the boundary; the stamp
routines themselves do not throw.

- `project_sight_cell`, `add_area_coverage`, `remove_area_coverage`,
  `map_area`, `update_area_coverage` and `refresh_area_coverage` are the
  stamp routines, standard and altitude branches both.
- `stamp_unit_sight`, `move_unit_sight` and `clear_unit_sight` build the
  descriptor from the canonical `Unit`, `UnitDef` and `Player` and write the
  stamp words back.
- `remember_sight` keeps the viewer's sight of a dead unit's position in the
  remembered-sight table; `expire_remembered_sight` lets it lapse through
  `remembered_sight_expired` and `compact_remembered_sight`.

Under allied vision (`SightContext::allied_vision`, the hack
`intel.allied-los-sharing`) every count of a stamp goes to each player in
use whose row the owner's alliance row names, in player order, then to the
owner, and marks the fog and radar stale. A player another machine
simulates is counted only while it is the viewpoint player, the owner
included; when the owner is not counted the descriptor stays owned by the
last ally counted, so the rest of the same move or fresh stamp (its second
count and its mapping) goes to that ally.

The match (`src/sim/match-runtime`) stamps units on creation and movement,
clears a dying unit's stamp and remembers the viewer's dead for 60 ticks, and
expires that memory every tick.
