# Unit playout

Draws the units of players this machine does not simulate (mirrored units)
moving evenly. It is presentation only, and a deliberate improvement on
3.1c rather than a part of its behaviour.

## Why

A mirrored unit moves only when its owner's unit-state record is applied:
one step of the owner's tick for each record, for all of that owner's units
at once, and never on this machine's own tick. The records arrive unevenly.
They are sent in bursts, every 200 milliseconds by default, and released one
a tick with backlogs flushed at once, so a mirrored army stands still for one
to seven ticks, sometimes eleven, and then jumps. A full record also puts
each unit back where its owner has it, one unit a tick. 3.1c draws only
what the simulation holds, with no frames in between, so mirrored armies
stutter.

The playout draws each mirrored unit on the path the simulation actually
gave it, a few of its owner's ticks behind the newest record applied, on a
clock per owner that runs evenly and adapts its delay to how that owner's
records arrive.

## Entry points

`oa/present/unit_playout.hpp`, namespace `oa::present::unit_playout`:

- `Playout::observe(world, hooks)` after each tick of the match, once the
  tick's records are applied. It records each followed unit's place and runs
  the owners' clocks on to `Game.tick`. Observing the same tick again
  records what changed since without running the clocks. The runtime calls
  it from `Runtime::advance_match_clock`, after each step, whether the engine
  or an extension ran it, and gives it the match's slot generations
  (`Hooks::slot_generation`). It throws nothing: a playout that cannot have
  its memory plays no unit out, and costs the match no step.
- `Playout::unit_pose(slot, time)` tells where a frame draws a unit: its
  position, heading, pitch and bank at a `FrameTime`, a tick and a part of a
  tick after it. A slot the playout does not follow gives nothing, and the
  frame draws it as the simulation has it. The runtime's draw paths reach it
  through `MatchPresentation::playout` and `mirrored_pose`
  (`src/app/match_models.hpp`), which draw the units of players in use with
  `OA_PLAYER_STATUS_MIRRORED` from it; a player in use but free or closed
  is followed here, but its units are drawn where the simulation has them.
- `frame_time_between(tick_before, tick, fraction)` places a frame drawn
  part of the way from one tick to a later one, up to seven ticks on, as a
  frame between the ticks of a batch is drawn.
- `Playout::owner(player)` tells where an owner's clock stands: its newest
  record, its clock, rate, the owner's pace, the delay it aims for and the
  delay it has, and how often it jumped.
- `Playout::reset()` forgets everything and frees the places' memory; the
  runtime calls it as the match is torn down. A tick earlier than the last
  observed, another `World` or another unit table also starts afresh. An
  owner's newest record more than `restart_behind_ticks` (30) before the
  newest seen starts that owner afresh; a nearer one arrived late and moves
  no clock.

A frame may show any moment from seven ticks before the tick last observed
up to it, on the clocks as they ran from one tick to the next; a moment past
it, up to a tick, shows them running on at their rates. A frame drawn part
of the way from the tick before to the current one passes
`frame_time_between(tick before, current tick, part)`. Moments that run on
from frame to frame show the units moving on evenly.

## What it reads and writes

It reads `Game.tick`, each player's `in_use`, `status`, `last_sim_tick` and
unit range, and each followed unit's `type_index`, `def`, `position`,
`heading`, `pitch`, `bank` and `attach_parent`, with the top speed
(`max_velocity`) of its type or of the unit carrying it, and, through the
hook, the slot's instance generation. It writes nothing of the simulation's:
its places and clocks live in the `Playout`, outside `World` and `Match`.
Targeting, sight, hits and every simulation result keep using the simulated
places, so saves, shared matches and every recorded digest are unchanged;
only the pointer's pick, which tests the frame drawn, takes the place drawn.

A player is followed while it is in use and neither local nor a computer
player: exactly the owners whose units `oa::sim::simulation_state::locally_simulated`
does not simulate here.

## The clock

Each owner has a clock in owner ticks with 16 bits of fraction. Each tick it
looks back over the arrivals of the owner's records in the last
`delay_window_ticks` (90, three seconds):

- The owner's pace is the slope of the least-squares line through the
  arrivals, owner ticks against the match's ticks, between `min_rate` and
  `max_rate`; over fewer than `pace_span_ticks` (30) it is one owner tick a
  tick. An owner whose game runs slower than this machine's has a pace below
  one.
- On a line that rises at that pace, each arrival puts the owner's newest
  record at a height: furthest up as a record arrives (the freshest), and
  furthest down just before the next one arrives (the stalest), the wait
  still open included.
- The clock aims to run at the owner's pace `delay_margin_ticks` (2) below
  the stalest moment, so that it keeps that many ticks of places in hand at
  the longest wait the window saw, and at least `min_delay_ticks` (2) below
  the freshest. Records arriving evenly make that the longest wait between
  them plus the margin; a queue that falls behind and catches up adds as far
  as it fell. It never aims more than `max_delay_ticks` (12) behind the
  newest record. It starts `start_delay_ticks` (6, the default send
  interval) behind the newest record and aims at least that far behind the
  freshest for its first `delay_window_ticks`.
- It runs at the owner's pace and a part of the gap to its aim, closing it
  over `settle_ticks` (16), between `min_rate` and `max_rate` (three
  quarters and one and a half ticks a tick), changing by at most `rate_step`
  (a sixteenth) a tick. Less than `low_water_ticks` (1) behind the newest
  record it drops to `min_rate` at once: a short buffer slows the units down
  rather than stopping them.
- It never passes the newest record: units are never drawn beyond the last
  place the simulation gave them. A stall longer than the delay still stops
  them until records arrive.
- More than `snap_behind_ticks` (30) behind the newest record, it jumps to
  its aim.

It keeps its value at the last `clock_history_ticks` (8) ticks observed, so
that the frames between the ticks of a batch show it as it ran. All of it is
integer arithmetic: the same observations and frame times give the same
poses on every machine.

## Places and jumps

Each unit slot keeps a ring of `places_per_unit` places, each at an owner
tick. A unit that stood still and then moved gets a place at the owner tick
the observation of the tick before saw, so its move is drawn over the ticks
it took, not over the time it stood. Between two places the unit is drawn
part of the way along, its angles turning the short way round. It jumps,
rather than moving, into a place it reached

- as it was created: it stands where it was created until its owner's clock
  reaches the tick, then follows its path. A unit is new to its slot when
  the slot was empty, or its type, owner or instance generation changed, so
  a unit made in a slot freed during the same tick is new too;
- by being loaded into a transport or set down from one (`attach_parent`
  changed);
- by moving further than its top speed allows in the owner ticks between
  the places, by more than `jump_speed_steps` (2) steps of that speed or
  `jump_pixels` (48) pixels, whichever is less: a full record's correction,
  or a structure moved. A carried unit goes as fast as the unit carrying it.

A unit that dies leaves its slot empty within the tick, so from that tick on
it is not drawn, wherever its clock had it.

Memory is bounded: the places of every unit slot (about 1.2 KB a slot),
allocated when the first mirrored player is seen and freed by `reset()`,
and `arrivals_per_owner` arrivals and `clock_history_ticks` clock values for
each player.

## Tests

`tests/unit_playout_test.cpp` (`present-unit-playout`) plays a walking unit
out over synthetic arrivals: records applied the tick they are made and two
ticks late, sent every six ticks with a late one every fourth send, 3.1c's
queue at its worst (still five ticks then a jump of seven, still five then a
jump of five), a queue that falls six records behind with no wait longer
than two ticks and then catches up, an owner running at nine tenths of this
machine's pace, owners at nine tenths and four fifths sending every six
ticks, a stall of 21 ticks and one of 40. It checks that the drawn unit
moves evenly at 120 frames a second (every step between three quarters and
one and a half of its pace, each close to the one before), that it never
stands while its owner runs slow, that the delay settles on its aim, that no
frame shows the unit past its newest place, the clock's jump past 30 ticks,
and that the same arrivals give the same frames. Frames drawn before the
tick, over batches of three ticks, and a tick observed twice move as
evenly. It checks the jumps of creation, of a unit made in a slot freed
within the tick, of loading and unloading, of a large correction, of a
correction after a burst of six owner ticks and of a moved structure; that
small corrections are drawn as part of the move; that a slot emptied by a
death is not drawn; that a late record moves no clock; that headings turn
the short way; that local units and empty slots are left alone; that the
players followed are those the simulation does not run here; and that
observing and resetting throw nothing.

`--check-unit-playout` (`src/app/runtime_unit_playout_check.cpp`,
`native-unit-playout`) draws it in the application's frames: the skirmish's
other player taken as another machine's, its records arriving within the
steps in 3.1c's bursts, its runner drawn at 120 frames a second.

## Limitations

- What a mirrored unit fires, its nanolathe stream, its explosion and its
  wreck are drawn where the simulation has them, a few ticks ahead of the
  unit as drawn; a unit that dies vanishes from where it was drawn as its
  explosion starts at its simulated place. The radar, the minimap and the
  on-screen list the pointer picks from also take the simulated place: a
  unit drawn just inside the view while its simulated place is outside it
  is not picked.
- It sees the units once a tick. When several of an owner's records are
  applied in one tick, the places between are not seen, and the unit is
  drawn straight across them, which cuts the corners of a circling
  aircraft. Observing after each record applied would keep them.
