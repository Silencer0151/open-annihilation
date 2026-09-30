# Model, script and spawn runtime

This component composes `unit-spawn`, `model-runtime`, `script-vm` and asset
ownership into a running match. `SpawnBridge` creates owned model pieces and a
COB interpreter for each unit, binds them before running `Create`, and keeps
the script across ticks. Its `SpawnSubsystems`, effects and unit-value
services are mandatory interfaces for engine operations that live elsewhere;
none of them has a default that reports success.

## Unit scripts

`ScriptInstance` looks script names up case-sensitively. A named call with
`immediate` set runs a whole scheduler step with elapsed zero right after the
start. `query` allocates one context with four arguments, runs just that
context and copies local slots 0..3 back; a RETURN operand is not the query
result, and a query that sleeps stays scheduled. A missing script or an
exhausted context pool leaves the query arguments untouched. `UnitInstance`
uses these calls for QueryPrimary/Secondary/Tertiary,
AimFromPrimary/Secondary/Tertiary, the fallback piece choice and the
asynchronous SetMaxReloadTime.

`call_no_arguments` keeps the context's old locals and never calls the
failure callback. `call` writes all four slots, zeroing omitted trailing
values, before it sets the argument count, and its failure callback receives
zero. Create uses the no-argument form.

`UnitHost` maps model-piece operations to the unit's own instance. Unit-value
GET/SET covers selectors 1..20 with their flag changes. Geometry, activation,
yard-map and effect operations remain required services. Two piece operations
do nothing, as in 3.1c. A piece position asked for outside the model is the
unit's position.

`SharedRandom` seeds the match stream and draws bounded values, including the
signed bound rejection and the unsigned division of 3.1c. Every unit script
draws from this one stream; do not give a script a private stream. The SLEEP
clock scale is supplied by the owning application; it is not derived from the
simulation frequency.

Script allocation stages a shared COB asset before the interpreter is built
with its model, and no script runs before the bind and Create order. The
bridge rejects a COB asset that does not match its model at bind. Asset
tables, the slot pool and caller services must outlive the bridge.
`UnitInstance` cannot move, because its interpreter and host keep references
to its model and world slot.

## The match

`Match` owns the slot pool and the unit instances, weapon slots,
squads, spatial occupancy and buckets, sight grids, movement maps, projectile
pool, effect world and map features. Its inputs carry the resolved runtime
metadata and map state explicitly. `MapPlot.metal` comes from the map's metal
setting and feature overlays; it is not TNT padding. Sight dimensions and
masks are explicit loader inputs. Ground movement uses `GroundRuntime`;
direction and distance come from `game-math`. A match built to resume a
savegame (`OfflineInputs::resuming_saved_game`) places only the map's
blocking markers; the save's Features section then places its features
through `place_saved_feature`, `ignite_saved_feature` and
`restart_saved_feature_sequence`, after `adopt_feature_defs` has taken any
type the save loads. Its plots' metal still starts with the metal of the
map's indestructible features, which it does not place, until the save's
Metal section replaces it. A limitation: a save whose Metal section is
missing or not one byte per cell keeps that metal, where the game keeps the
map's metal without it.

Unit creation shares nothing while the run flag (`Game.session_flags` bit 0)
is clear; with it set the match needs its `multiplayer` hooks or it stops.
`OfflineServices` supplies command and activation sounds, attachment and UI
callbacks, effects and map-update operations. A unit whose order speaks with
its own caption, as a transport's "Unit is too heavy to transport" or a
builder's "Starting construction", reaches the application through
`SpeechHooks` (`Match::set_speech_hooks`); without them the speech plays its
category through `command_sound` and the category's own caption.

## Event hooks

`Match::event_hooks` (`EventHooks`, `event_hooks.hpp`) reports what happens
in a match to a reader such as a director, whoever simulates the unit: this
machine, another player's, or a recording the match replays. Each entry is
null by default and is called only at the points every such event passes
through:

- `unit_created` at the end of `create` and `start_player`; a building
  (bmcode 0) created finished is then reported finished by itself.
- `unit_finished` from the builder link (`link_built_unit`), which build
  progress reaching the end, a factory's cancelled frame and `finish_unit`
  all take; from the resurrect order, by the unit that raised it; and for a
  building created finished. Each unit is reported finished once from its
  creation on, whichever comes first, so a copy another machine's state
  record already marked finished is still reported when the builder link
  arrives, and a building created finished is not reported again.
- `shot_placed` from `place_shot` (weapon), the burst copy in
  `update_projectiles` (burst) and `launch_meteor` (meteor, no aim). A shot
  `apply_shot` launches reports the aim and target unit it was given, which
  the record of a ballistic or dropped shot does not keep; a burst copy of
  such a shot reports no aim.
- `shot_detonated` at the top of every `detonate`, the stand-in blast of an
  exploding unit included.
- `unit_damaged` in `apply_damage_event` once the unit is known live and not
  dying, before the event changes it.
- `unit_died` as `teardown_dead_unit` starts on a live unit, before its
  type, owner and position are cleared: each death once, settled here or
  elsewhere.

The hooks receive the world as const and must not change match state, draw
from its random streams or call back into it; the match computes nothing
differently with them set (`match-event-hooks` checks the synthetic
skirmish's digests against `match-determinism`'s pins with and without
them). A limitation: a unit only a state record from elsewhere finishes,
with no builder link following, is not reported finished.

## Sharing with the other players

A multiplayer match shares what it settles through `MultiplayerHooks`, and
applies what the other players' machines settled through entries of its own.
Every hook is null in a game played on one machine; an entry whose comment
says what null does may stay null in a multiplayer game too. Besides new
units, state flags, health events, scripts, shots, deaths and features:

- `shot_intercepted` shares each shot an interceptor's blast sets off here,
  once it has gone off, with the interceptor's shot. An interceptor
  simulated elsewhere sets nothing off here.
- `carry_link_changed` shares every carry link `set_carry_link` accepts,
  whichever machine simulates the units, just before it applies: transports,
  factory pads, the AttachUnit and DropUnit scripts, the builder link and a
  dying carrier's cargo. `apply_carry_link` applies a link another machine
  shared without sharing it again.
- `unit_finished` shares a unit simulated here that its builder's work
  finished, or a building created finished, which names itself as its
  builder. `finish_unit` runs the builder link on another machine's copy:
  it is marked built and activated when its type activates when built, and
  stays on its pad until that machine's carry link sets it down.
- `script_started` carries StartBuilding from the build, repair, reclaim,
  capture and resurrect orders, with the heading to the work as its one
  argument, zero-extended from 16 bits.
- `feature_changed` with `FeatureChange::resurrected` names the origin plot
  of a wreck a unit simulated here raised, so the wreck goes everywhere.
- `unit_transferred` hands a unit simulated here to a player another machine
  simulates, just before the unit dies here as captured. `transfer_unit`
  hands a unit to another player, as a capture or a gift of units does, and
  creates the unit another machine handed over from its `TransferredUnit`.
- `end_local_game` ends the local player's game at once as a defeat, as
  another player's machine reporting this one gone does.

`tick_scripts` advances COB contexts only. `tick()` runs a whole simulation
tick: the unit sweep with the match's own host, projectiles and explosions,
the path search, each player's controller, knowledge, sight and economy, the
features, the wind, the meteor storm, particles and the viewer's remembered
sight, in the game loop's order. `tick(Host&)` runs the unit sweep with an
external host instead. A gameplay branch the engine does not support throws;
moving collision and occupancy removal need the resolved type metadata and
collision plots and refuse movement without them.

## Outcomes

Ordinary matches call `configure_outcomes` with the local player index, its
alliance row and the defeat permission; full ticks require it. The local
slot's victory and defeat checks run under its `Player.next_economy_tick`
deadline, which advances by 30 when due, before the slot's economy settles;
there is no separate outcome deadline. The checks read each player's live unit
counters. A condition that becomes false again does not reset the outcome
countdown, and the final outcome is kept for presentation.

Under the deathmatch commander rule an expired defeat countdown places a new
commander instead (`respawn_local_commander`) when the side table is bound;
otherwise the defeat stands. With no host seated the respawn reads the
eleventh player record's setup block. A defeated multiplayer player may go on
as a watcher.

## Sight

Moving units update their sight stamps with the centre and band written back.
Altitude sight needs derived height cells and the LOS.TDF ray patterns
(`OfflineInputs::altitude_sight`); it does not fall back to plain fog.
Coverage is kept for each of the ten players, while the mapped bits are
shared. `unit_visible` samples four corners of the unit's footprint and model
bounds, with the owner, cloak and submerged gates. `point_visible` uses the
asking player's coverage under line of sight, and the viewpoint player's
mapped bit under the mapping rule alone.

## Economy and repair

Natural repair runs on the eight-tick cadence of 3.1c. It uses the unit's
build cost, build time, maximum health, armour and state flags, and the
`Unit.economy` accumulator words; an accepted repair changes health and a
positive resource gate changes the requested debit without accepting it.

`update_player_economy` is the per-player economy tick: the activity gates,
EnergyUse and make credits with the easy and medium computer scales, cloak
upkeep with the cloak dropped when it goes unpaid, the merge of the unit
accumulators with the player block, the supply and stock ratios, the storage
clamps with waste totals, and the scaling of every block for the next tick.
`economy_test.cpp` asserts every player output field for human, easy, medium
and hard computer players across inactive, storage-full, storage-free,
negative-income, wind and tidal branches.

## Orders

`issue_ground_move` is the ordinary command installation once the command
resolver picks Move_Ground: an unqueued command clears the unit's orders and
drops idle orders at the head, then the order follows the queue-tail mark. It
is not a generic command resolver or screen-to-world picker. `issue_order`
inserts an order of any mission kind (descriptor-kept queues, overlay drop,
head, secondary and queue-tail placement); the campaign's InitialMission
scripts queue through it (`mission_unit_binding.hpp`, ctest
`match-mission-units`). The 68-entry mission descriptor table is
`mission_descriptor_table`.

`command.cpp` resolves the move and attack commands with the water-weapon,
aircraft, alliance, availability and capability gates; targeted moves go
through `can_repair_target` and `can_load_target` and the capture, reclaim,
repair, load and guard choices, with the packed capability bytes from
`pack_command_capabilities`. The ground mission family (`ground_missions.hpp`
with the command, attack and construction handlers) and the aircraft
families step every order kind with its phase results, wait events, timers,
leashes and goal retries. Orders link into their target's list of observing
orders and lose the target when it dies.

## Combat

The weapon tick resolves a slot's target (the SweetSpot piece centre, or the
terrain height under a ground point), starts the turret or vertical-launch Aim
script, applies the range and sea gates, runs the Fire scripts, keeps reload
and events, and places a projectile record in the 300-slot pool. The reload
countdown lives only in the unit's canonical `UnitWeapon.reload`, so saves,
traces and the state digest see the live value. Turret and
vertical-launch shots fire only after the Aim script the slot started has
returned nonzero, and each shot spends that aim. `UnitDef.model_height`, the
model top a shot must pass under, is taken from each type's loaded model when
the match is built.

A shot striking a unit applies its DAMAGE entry through the damage path: a
unit simulated here that drops below 1 health is marked dying and is torn down
by the kill handler, which clears its orders, drops its occupancy, removes the
instance and lowers the owner's unit count. `DeathKind` names the kinds; a
reclaim death credits the reclaimer `(1 - build fraction) * metal cost`. The
damaged unit's reaction (observers woken with event `0x10`, the computer
capturer alert, a chase or re-aim, the under-attack notice) runs through
`weapon_execution::retaliate`, and kind-2 damage and the Paralyze mission run
through the `unit_health` paralysis code.

The machine that simulates a unit settles its death: the kill handler works
out the Killed percentage and the wreck level (`KillOutcome`), drops a last
attacker whose player's slot has gone free, and, for a unit simulated here,
shares them through `MultiplayerHooks::unit_killed` before it clears the
unit's orders and tears it down, so the record still names the unit's owner
and last attacker. `Match::kill_unit` runs the kill handler on one unit;
death kind 0 kills a unit whose slot another player's new unit takes, by its
own health and with no statistics. `Match::apply_kill` applies a death
another player's machine settled: the unit takes that machine's last
attacker, its Killed script starts with the shared percentage for its flying
pieces only, a finished unit explodes when that percentage is above zero, and
the wreck is left at the shared level; statistics count by the shared kind.
`feature_host` passes weapon hits on features, and the fires, destructions
and reclaims settled here (a reclaim with the reclaiming unit), to the
multiplayer hooks' feature entries, and the resurrect order passes the wrecks
it raises; with those entries null every hit is applied here and nothing is
shared.

`update_projectiles` flies the pool through its flight modes, tests every
moved shot against the plot under it (intercept burst, occupants, feature,
ground bounce, ground, water) and compacts the pool; a record still live
after its move puffs its smoke trail, and a shot dropping into the sea
splashes. A detonation logs its effects in the match's effect world: the water
or lava art from the plot under the burst when no unit was hit, else the land
art or an endsmoke puff; a nosealeveltrigger sea swallows such a shot whole.
It adds the weapon's shake to the running screen shake in `Game`, which the
app's camera does not apply yet, and plays `soundhit` or `soundwater` where
the viewpoint player sees the burst. An interceptor's blast detonates the
shots near it and shares each one (`shot_intercepted`). A dying unit's
explodeas or selfdestructas weapon detonates through a stand-in shot with no
source after the unit has left its plot.

The commander rule's sweep runs only in the simulation that runs the
commander, after the `order_panel` hook closes the order panel. A commander is
the type its owner's side names, not the FBI flag. Carried units dying with
their transport, captured units, cancelled nanoframes and the hotkey
self-destruct take their 30000 through the veteran scaling of the damage
path.

## Computer players and knowledge

The match sets the computer players up on its first tick. Each tick every
player whose record holds a controller (`Player.controller`) runs it: a
computer player's controller first runs its order-callback tick (the squad
sort and due tasks in `src/sim/ai`), then the weapon sweep retargets the slots of
units that fire at will; command-fire weapons take part only for computer
players. Interceptors aim at an enemy missile; other weapons take a random
enemy in range. The knowledge refresh follows for the same players: every 30
ticks it rebuilds the player's sightings (`src/sim/detection`) and, for a
computer player, its own-unit counts and base position, then a one-in-30 draw
runs the strategic refresh. Offline matches run the wind from the OTA
`MinWindSpeed` and `MaxWindSpeed`.

The computer players reach the match through `ComputerHost`
(`src/computer_host.cpp`): squads, orders, placement queries and the random
stream, and for the siege task a player's sight of a point, its sightings, a
unit's first-weapon reach and the order the Attack command gives over open
ground.

## Path search

`advance_path_search` runs after the projectiles each tick: it starts a job
for the next player with a waiting unit, expands it and publishes a finished
route through `accept_path`. Each movement class keeps a persistent map that
footprint, occupancy and feature changes refresh.

## Verification

The `match-runtime` tests cover case sensitivity, immediate call return
callbacks, synchronous local writeback with an unrelated active context, unit
values and flag updates. The offline integration test builds a one-piece model
and a real COB program, then runs the full spawn order, masked footprint
occupancy, the weapon query fallback, sight changes, local ground controller
allocation and later hide, sleep and show ticks; a script-driven activation
runs the deferred Activate script and checks the sound and UI callback order.
Only the test's external callbacks are recorded fixtures. It also runs 35
stationary ground ticks (idle mission scheduling, health percentage refresh,
completion of the Create sleep) and 60 ground-motion ticks (cell changes,
terrain collision, occupancy removal and insertion, moving sight). The other
tests cover each order family, combat, the deaths shared with and applied
from the other players (`match-shared-deaths`), the interceptions, carry
links, completions, StartBuilding starts, resurrections, unit transfers and
game endings shared with and applied from them (`match-shared-events`),
projectiles, economy, features, transports, outcomes, saved orders, saved
features and the trace stream.
