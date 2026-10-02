# Game-loop coordinator

The per-frame and per-tick control flow of the running game. Target
`oa-base-game-loop` builds standalone or through `add_subdirectory(src/base/game-loop)`.

`set_mode` stores the application mode, selects its tick handler (`ModeCallback`)
and installs the matching close-request handler. `update_timing` turns elapsed
clock units into pending simulation steps: it scales them by the actual game
speed, slows down while another player's simulation lags, carries the fraction,
limits a frame to five steps and adapts the actual speed to the load. The
expression runs with a 53-bit significand by default, which gives 3.1c's
results; a 64-bit significand is also available. `run_ticks` runs each
simulation step's subsystems in their fixed order and updates the tick counters.
`run_frame` covers the live, inhibited, inactive, presentation and periodic
frame-capture branches around it.

Every subsystem call is a required typed `Host` callback; the `Step` enum names
the calls that take no explicit argument. The host owns their implementations
and may update state before later branches. No default callback succeeds
silently. Mode bodies, simulation subsystems, input handling, profiling,
rendering and capture are not implemented here, and running the installed mode
callback belongs to the idle-frame loop.

Where a timing, frame or mode value in the public header stands for a field of
the canonical `Game` or `Player` record, its comment names that field;
`ModeEnvironment` holds what `set_mode` reads of a screen package's window.
These structures are views of the fields the loop reads, not replacements for
those packed records. Clock values are in the game clock's own units;
`scaled_clock` keeps its 32-bit product wrap before the division by 1000,
so its readings turn over to 0 past `scaled_clock_turn`, about every 39.8
hours at 30 units a second. `scaled_clock_before` and `scaled_clock_elapsed`
compare readings across that turn: a reading more than half a turn below
another has turned over since, and lies after it.

The tests cover counter wrap, the clock's turn, lag selection, timing thresholds, callback order,
mutation between callbacks, paused notifications, capture cadence and every mode
target. They use trace hosts, so they check this coordinator and not the
subsystems behind it.
