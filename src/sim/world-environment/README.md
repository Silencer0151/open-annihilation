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
scheduler over the canonical `Game` wind fields. Each run says how it ended
(`WindRefresh`): waiting, changed, or stopped by a rand() value above 32767 or
a zero strength divisor, which leave the `Game` fields as they were. `update_sea_occupy` reads the
canonical `Unit`/`UnitDef` and keeps the occupy code in
`Unit.last_occupy_code`; the match's movement tick runs it for every unit it
moves, and its hook runs the unit script's `setSFXoccupy`. The hook also says
whether a mod's water state rules reorder the checks (units.water-state-rules).

A mod's deterministic-wind rule replaces the draws with one shared generator,
so that every machine plays the same wind: `refresh_shared_wind` keeps the
deadline test, vector and normalized strength, but draws the interval
(150 + 30 * (draw mod 10) ticks), the strength (the minimum plus a draw modulo
the range, or the minimum when the maximum is not above it) and, for a nonzero
strength, the direction (a draw's low 16 bits) from a `WindGenerator`, the
32-bit MT19937 generator. It is seeded at its first change with
`shared_wind_seed`: the network id of the first slot in use on the host's
machine with a host setup state, or the caller's fallback. The generator's
words have no padding, so the match keeps it as rule state as it is.
`shared_wind_test.cpp` checks it against the standard library's MT19937.

`meteor.hpp` runs mission meteor showers. The strike schedule, target and
origin draws use the `rand()` stream as the game does; projectiles are
spawned through `MeteorHost`. The velocity's `origin * 0xfff + target` product
wraps in the 20-bit shift to `(target - origin)` cells over 90 ticks.
