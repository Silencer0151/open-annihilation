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

The match (`src/sim/match-runtime`) stamps units on creation and movement,
clears a dying unit's stamp and remembers the viewer's dead for 60 ticks, and
expires that memory every tick.
