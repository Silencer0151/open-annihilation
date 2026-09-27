# 3DO model format

`oa-formats-objects3d` reads Total Annihilation 3DO object trees using explicit
little-endian decoding and bounded offsets/counts. Coordinates remain signed
16.16 integers (`65536 == 1 world unit`), and the two primitive words after
the texture name offset are kept as read and not interpreted.

The game negates X and Z of every piece offset and vertex after loading a
model; `flatten_for_render` applies that conversion while accumulating piece
offsets. It is an integration adapter, not the game's renderer. The game
selects and sorts primitives at draw time; the reader keeps disk primitive
order and marks the selection primitive so a renderer can reproduce that
behaviour deliberately.

`maximum_height_fixed` computes `UnitDef.model_height` when a unit type
loads. It walks sibling and child links, takes vertex Y plus piece Y offsets,
and keeps the game's recursive zero clamps and signed 32-bit arithmetic. Code
that reads the height's high 16 bits reads this same value; they are not a
separate FBI or model property.

`derive_unit_type_bounds` computes the remaining `UnitDef` bounds, set before
the model height. X and Z minima, maxima and extents come from
`footprintx`/`footprintz` at `0x100000` fixed units per footprint cell. They
do not come from 3DO vertex extrema. Y minimum is zero, Y maximum is the model
height, and Y extent is their difference. Targeting visibility samples exactly
this bound set.

`shadow_bitmap_extent` sizes the bitmap a unit's shadow is drawn into. Pieces
are visited from last to first. A piece is included only when its visible flag
bit 0 is set, which the game does exactly when the object has at least three
vertices. The measured coordinates are the initial point list: local vertices
with X and Z negated, without piece offsets and without later script
changes to those points. Each vertex projects as
`sx = (int16)(X >> 16) + ((int16)(Y >> 16) >> 2)` and
`sy = (int16)((-Z) >> 16) - ((int16)(Y >> 16) >> 2)`. Extents start at 0.
Width and height are `max - (min - 2) + 2`; origins are `-(min - 2)`.
Width, height and origins are full int32 values; the game keeps the low 16
bits of each as the shadow bitmap's size and origin.

The 52-byte object, 32-byte primitive, unsigned 16-bit vertex indices, Y-up
axis, 16.16 units and source visibility rule match the published 3DO format
descriptions.

## Validation

The synthetic unit test covers hierarchy, unsigned indices, exact fixed-point
conversion, preserved primitive words, selection identity, truncation, invalid
indices, unterminated strings and cyclic object links.

The corpus tools read all 608 3DO files of a Total Annihilation 3.1c install
(3,014 objects, 92,086 vertices, 50,443 primitives) and agree with an
independent reader on per-file object, vertex, primitive and unique-texture
counts.
