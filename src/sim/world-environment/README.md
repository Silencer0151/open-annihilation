# World environment

This component runs the live wind scheduler and its initialization at game
start. Cadence uses the rand() stream while strength and direction use the
shared deterministic game stream. The split is part of the interface so a
host cannot accidentally merge the streams.

The fixed-point direction transforms are the sine and cosine scaling exposed
by `oa-sim-unit-movement`. The scheduler keeps the game's unsigned deadline
comparison, deadline-relative cadence, wrapping integer operations,
calm-direction retention, a single rounding of the normalized strength to
float, and changed flag behavior.

`refresh_wind(Game&, ...)` and `initialize_wind(Game&, ...)` run the same
scheduler over the canonical `Game` wind fields. `update_sea_occupy` reads the
canonical `Unit`/`UnitDef` and keeps the occupy code in
`Unit.last_occupy_code`; the match's movement tick runs it for every unit it
moves, and its hook runs the unit script's `setSFXoccupy`.

`meteor.hpp` runs mission meteor showers. The strike schedule, target and
origin draws use the `rand()` stream as the game does; projectiles are
spawned through `MeteorHost`. The velocity's `origin * 0xfff + target` product
wraps in the 20-bit shift to `(target - origin)` cells over 90 ticks.
