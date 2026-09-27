# Simulation state

The first slice of the per-tick unit update: player eligibility, the unit sweep
and its timers, health percentage history, both order schedulers, order-queue
changes and height correction. It is not a complete movement or combat engine.

Call `oa::sim::simulation_state::update_units(world, orders, host)` from the game
loop's unit-movement step (`Step::tick_unit_movement`) with the game tick
already incremented. Units, players, types and game fields are the canonical
`oa::World` records (`src/core`): populate `Unit.owner`, `Unit.def` and the
players' inclusive `first_unit`/`last_unit` ranges with `oa_ref32` references.
Order list heads are native, in the `OrderQueue` side table indexed by unit
slot. Keep all tables stable while executing callbacks.

Every operation owned by another system is a mandatory `Host` virtual method: no
host that pretends scripts, order handlers, movement, damage, terrain lookup or
weapon work succeeded is supplied. `Host::dispatch_mission` runs the mission
handler of an order's kind; implementing the individual handlers is the next
main integration step.

Build independently:

```
cmake -S src/sim/simulation-state -B local/build-simulation-state
cmake --build local/build-simulation-state
ctest --test-dir local/build-simulation-state --output-on-failure
```
