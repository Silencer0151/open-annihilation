# Unit effects

The COB emit-sfx and explode effects. Simulation emits typed events through
`Sink`; rendering and audio stay outside. Explosion random bounds, velocity
constants, the 900-tick debris lifetime, flag routing and piece transforms
follow the game.

`OfflineEffects` is the two-phase `Match` adapter used by the native
runtime. It binds model transforms, shared random state, sea level and piece
visibility to match-owned state. It rebuilds every piece transform because
`model-runtime` does not yet expose the small angle change below which 3.1c
keeps a piece's transform.
`Host::visible` routes through `Match::unit_visible` (owner identity,
cloak flag, and the four-corner sight test). COB attach-unit and drop-unit
change the simulation's carry links, so the adapter forwards them to
`Match::script_attach_unit` and `Match::script_drop_unit`.

`OfflineEffects` also turns the script events into the match's effect world
(`src/sim/effect-particles`): emit-sfx 0/1 flames (travel 6 or 7, layer 7),
2..5 lit wakes (period 16 or 8, layer 2), 0x101/0x102 light and dark puffs
(layer 9), 0x103 unlit bubbles to sea level (period 8, layer 7), explode
bitmaps as large-flash records and whole-piece debris at the piece origin plus
the unit position. A shattering piece breaks into fragments from its
transformed vertices, its movement object's velocity, its owner's colour and
the object's loaded primitives, which the match takes from
`OfflineInputs::loaded_primitives` (the renderer's prepared model: the model
loader's primitive order and flag words, whose stray file bits can mark a
textured quad coloured). The loader also returns the object's loaded selection
index: the sort moves the selection primitive to the front and sets it to 0,
so the quad left at the file's selection index still shatters. Without that
loader the piece is dropped.
