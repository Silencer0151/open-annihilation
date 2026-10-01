# Unit playout

Draws the units of players this machine does not simulate (mirrored units)
moving evenly, near where the simulation has them. It is presentation only,
and a deliberate improvement on 3.1c rather than a part of its behaviour.

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

Each record also tells where each unit is going: a ground unit's route head
and speed, an aircraft's goal. The playout draws each mirrored unit on a
clock per owner that runs evenly at the owner's pace near its newest record.
Behind the newest record the unit is drawn on the path the simulation gave
it; ahead of it, moved on from its newest state the way its movement steers
it. When new records apply, the difference between where the unit was drawn
and where it is drawn now fades out rather than showing as a jump. A live
match and a recorded game played back are drawn the same way.

## Entry points

`oa/present/unit_playout.hpp`, namespace `oa::present::unit_playout`:

- `Playout::observe(world, hooks)` after each tick of the match, once the
  tick's records are applied. It records each followed unit's place, runs
  the owners' clocks on to `Game.tick`, works out each unit's path ahead of
  its newest record, and takes the difference new records made to where the
  unit is drawn. Observing the same tick again records what changed since
  without running the clocks. The runtime calls it from
  `Runtime::advance_match_clock`, after each step, whether the engine or an
  extension ran it, and the director after each tick of a recording it
  plays. It gives it the match's slot generations (`Hooks::slot_generation`)
  and what each unit's movement holds (`Hooks::motion`, a `Motion`). It
  throws nothing: a playout that cannot have its memory plays no unit out,
  and costs the match no step.
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
  record, its clock, rate, the owner's pace, the lead it aims for and the
  lead it has (below 0 behind the newest record), and how often it jumped.
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
(`max_velocity`), acceleration, brake rate and turn rate of its type or of
the unit carrying it, and, through the hooks, the slot's instance
generation and the unit's movement: the movement record's speed, velocity,
layer and blocked bit, the up to three route points its owner shared, and
the air driver's point, its velocity and heading, and a seek goal's step.
It writes nothing of the simulation's and runs no locomotion on it: its
places, paths, corrections and clocks live in the `Playout`, outside `World`
and `Match`, and it draws no random numbers. Targeting, sight, hits and
every simulation result keep using the simulated places, so saves, shared
matches and every recorded digest are unchanged; only the pointer's pick,
which tests the frame drawn, takes the place drawn.

A player is followed while it is in use and neither local nor a computer
player: exactly the owners whose units `oa::sim::simulation_state::locally_simulated`
does not simulate here.

## The clock

Each owner has a clock in owner ticks with 16 bits of fraction. Each tick it
looks back over the last `delay_window_ticks` (90, three seconds):

- The owner's pace is the slope of the least-squares line through the
  arrivals of its records, owner ticks against the match's ticks, between
  `min_rate` and `max_rate`; over fewer than `pace_span_ticks` (30) it is one
  owner tick a tick. An owner whose game runs slower than this machine's has
  a pace below one.
- The middle of its newest records is the mean, over the ticks of the
  window, of the newest record less the pace times the tick: a line at the
  owner's pace through the middle of the steps its records make. Records
  that arrive in bursts put it half a wait below the newest record as each
  burst arrives, and half a wait above it just before the next.
- The clock aims at the owner's pace `aim_behind_middle` (half a tick) below
  the middle, never more than `max_delay_ticks` (12) behind the newest record
  nor more than `predict_ticks` (12) ahead of it. It starts at the newest
  record.
- It runs at the owner's pace and a part of the gap to its aim, closing it
  over `settle_ticks` (16), between `min_rate` and `max_rate` (three
  quarters and one and a half ticks a tick), changing by at most `rate_step`
  (a sixteenth) a tick. Less than `low_water_ticks` (1) short of
  `predict_ticks` ahead of the newest record it drops to `min_rate` at once.
- It never runs more than `predict_ticks` past the newest record: a stall
  longer than that stops the units until records arrive.
- More than `snap_behind_ticks` (30) behind the newest record, it jumps to
  its aim.

It keeps its value at the last `clock_history_ticks` (8) ticks observed, so
that the frames between the ticks of a batch show it as it ran. The clocks
are integer arithmetic.

## Places and jumps

Each unit slot keeps a ring of `places_per_unit` places, each at an owner
tick. A unit that stood still and then moved gets a place at the owner tick
the observation of the tick before saw, so its move is drawn over the ticks
it took, not over the time it stood. While the clock is at or behind the
newest record, the unit is drawn part of the way between the two places
around it, its angles turning the short way round. It jumps, rather than
moving, into a place it reached

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

## The path ahead

At each observation each unit gets a path of `predict_ticks` steps, one an
owner tick, from its newest state; past the newest record the unit is drawn
on it, part of the way from one step to the next, at most `predict_ticks`
on. The path follows what its movement holds, worked out on copies in
double precision:

- On the ground it steers toward its next route point (toward a point on
  the route's leg up to 80 pixels short of it while further away), turning
  at most its turn rate a tick, speeding up by its acceleration while the
  turn and the point after allow and braking otherwise, up to its top
  speed, and goes on to the point after once within about five pixels. With
  no route it brakes to a stop.
- In flight it follows its air point, which moves on by its seek goal's
  step, or as it last moved: the aircraft turns toward it, its velocity
  damped and turned with its heading, and pushed toward the point by at
  most its acceleration. An aircraft on the ground goes on at its movement
  record's velocity.
- A unit with no movement record, or blocked on the ground, goes on at the
  pace of its last two places, no faster than its top speed, unless it stood
  since or jumped into the newer. A carried unit stands with its carrier.

## Corrections

The frames before an observation last showed each unit at some moment, on
its places and path as they were then. The observation takes, at that
moment's clock, where the unit was drawn less where its new places and path
put it, and adds that difference to every later frame, fading it out over
`correction_ticks` (250 milliseconds), easing in and out. A difference left
from earlier records is carried into the new one as far as it had faded.
With no frame drawn since the last observation, as over the ticks of a
batch, the moment stays where it was. The unit jumps instead, with nothing
to fade, into a place it reached by a jump (above), and when the difference
is longer than `snap_pixels` (48) across the map.

Memory is bounded: the places, path and correction of every unit slot
(about 1.7 KB a slot), allocated when the first mirrored player is seen and
freed by `reset()`, and `arrivals_per_owner` arrivals and newest records and
`clock_history_ticks` clock values for each player.

## Tests

`tests/unit_playout_test.cpp` (`present-unit-playout`) plays a walking unit
out over synthetic arrivals: records applied the tick they are made and two
ticks late, sent every six ticks with a late one every fourth send, 3.1c's
queue at its worst (still five ticks then a jump of seven, still five then a
jump of five), a queue that falls six records behind with no wait longer
than two ticks and then catches up, an owner running at nine tenths of this
machine's pace, owners at nine tenths and four fifths sending every six
ticks, a stall of 21 ticks and one of 50. It checks that the drawn unit
moves evenly at 120 frames a second (every step between three quarters and
one and a half of its pace, each close to the one before), that it never
stands while its owner runs slow, that it is drawn within one tick of its
simulated place on average with a record each tick, within two with
bursts and within three with a queue that falls behind, that no frame shows it more than `predict_ticks` past its newest
place, the clock's jump after a stall of 50 ticks, and that the same
arrivals give the same frames. Frames drawn before the tick, over batches of
three ticks, and a tick observed twice move as evenly. It checks the jumps
of creation, of a unit made in a slot freed within the tick, of loading and
unloading, of a correction after a burst of six owner ticks and of a moved
structure; that a small correction fades out over the frames that follow;
that a late record moves no clock and the unit never back; that a slot
emptied by a death is not drawn; that headings turn the short way; that
local units and empty slots are left alone; that the players followed are
those the simulation does not run here; and that observing and resetting
throw nothing.

`--check-unit-playout` (`src/app/runtime_unit_playout_check.cpp`,
`native-unit-playout`) draws it in the application's frames: the skirmish's
other player taken as another machine's, its records, with the runner's
speed and route head, arriving within the steps in 3.1c's bursts, its
runner drawn at 120 frames a second. `app-presentation-interpolation`
checks that a unit's pieces are placed between their two ticks' poses
across a jump.

## Limitations

- What a mirrored unit fires, its nanolathe stream, its explosion and its
  wreck are drawn where the simulation has them: within two ticks of its
  movement of the unit as drawn on average, up to about eight for fast
  aircraft between bursts. The radar, the minimap and the on-screen list
  the pointer picks from also take the simulated place.
- The path ahead follows the route head and goal the newest record left;
  a unit that turns off them, stops or is pushed by its owner's simulation
  is drawn going on until records show it, and the difference then fades
  out.
- It sees the units once a tick. When several of an owner's records are
  applied in one tick, the places between are not seen, and behind the
  newest record the unit is drawn straight across them, which cuts the
  corners of a circling aircraft.
