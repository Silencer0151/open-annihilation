# Map runtime preparation

This component prepares the map-owned state consumed by normal gameplay after
the TNT has been parsed. It does not treat the fourth TNT attribute byte as a
movement or metal value.

`prepare` builds `MapPlot.metal` as follows:

- Current-format (`0x2000`) plots start at the low byte of the configured map
  metal value when that value is nonnegative. Negative settings use zero. The
  legacy format copies each attribute's extra byte into `MapPlot.metal` before
  feature overlays.
- Feature-bearing plots are then visited. A feature whose `FeatureDef.metal`
  is nonzero and ordered (not NaN), and whose flags hold
  `OA_FEATURE_FLAG_INDESTRUCTIBLE` (the metal-overlay bit), paints the low byte
  of its truncated metal value over its signed `footprint_x`/`footprint_z`
  footprint. Plot lookup rejects footprint cells outside the map. A negative
  signed footprint makes the loop empty, as in 3.1c.

The feature inputs therefore come from resolved feature definitions, in the
same order as `tnt::Map::features`; TNT records contain names rather than
these runtime fields. `resolve_feature_terrain` reads the effective
`features/**/*.tdf` documents recursively, as 3.1c does, and searches their
top-level sections case-insensitively for each TNT name, retaining document
order. It reads signed `footprintx` and `footprintz`, stores `metal` as
`(atoi(value) & 0xffff)` converted to float, and sets
`OA_FEATURE_FLAG_INDESTRUCTIBLE`, the metal-overlay bit, from
`(indestructible & 1)` and `OA_FEATURE_FLAG_BLOCKING` from `(blocking & 1)`.
Each collision plot's `high_height`/`low_height` comes from the four
adjacent TNT height vertices, and feature footprints project the blocking
result the movement tests read. Before projecting those fields, features are placed in row-major
order: out-of-map footprints are rejected, removable overlaps are cleared
across the old footprint, indestructible overlaps reject the new placement,
and non-origin cells receive `0xFFFE` backlinks. `prepare` installs every
current-format `0xFFFC` marker in its first pass, before placing ordinary
features. Features with a nonempty `object` field consume one of the 2,048
model-instance slots; removal releases the slot and capacity exhaustion
rejects the placement before origin/backlink writes. Current-format `0xFFFC`
markers remain blocking. A missing definition is an explicit load error, as
3.1c stops with an error there, rather than empty feature data.

`feature_word_at` reads `MapPlot.feature`. A word above `0xFFFA` becomes
`0xFFFF` unless it is the `0xFFFE` footprint continuation. That case returns
the word `z * width + x` plots toward the origin and does not test the result
again. `prepare` stores the z offset in `feature_back_z` and the x offset in
`feature_back_x`, which mirror the low and high bytes of
`MapPlot.feature_record`. An origin outside the supplied plot span has no
readable word and yields `0xFFFF`.

`feature_at_position` resolves the feature under a signed 16.16 world
position. It shifts x and z right by 20, truncates each cell to int16, and
reads that plot with the map bounds check. Plot word `0xFFFE` subtracts the x
offset from x and the z offset from z, then reads the origin once.
A word below `0xFFFB` is returned, with that cell and the feature's footprint
when the index is inside the supplied table. A redirected word is tested
again, so a continuation that lands on another sentinel returns `0xFFFF` and
writes nothing. A missing plot, including a missing origin, yields `0xFFFF`.

`feature_center` places a signed 16.16 point at the center of a feature
footprint. The cell is the packed cell word (x in the low int16, z in the high
int16). `FeatureDef.footprint_x` and `footprint_z` are the signed footprint
extents. Each axis is `(footprint + cell * 2)` shifted left 19, which is the
32-bit multiply by `0x80000`. Y is the terrain height sample at that X/Z
shifted left 16. The sample reads X and Z and does not read Y; the caller
supplies that existing sample instead of a copy. The three words are returned
in X, Y, Z order.

Normal line of sight uses a grid of `attribute_width / 2` by
`attribute_height / 2`. Startup loads archive `vismasks` and sequence
`vismask`. Normal LOS reads each frame's linear pixel bytes directly and
preserves its origin and transparency index for the standard stamps'
`visibility_state::SightContext::masks`. All ten frames in the shipped
`VISMASKS.GAF` are simple, uncompressed frames (11×11 through 29×29); other
frame forms are rejected explicitly. Callers load `anims/vismasks.gaf` through
the asset system and pass the parsed archive.

## Live features (`oa-sim-feature-runtime`)

`feature_runtime.hpp` runs map features over the canonical `World`: the
2,048-record placed-feature pool, placement and removal on `MapPlot` cells,
weapon damage and reclaim, die/reclaim sequences and remnants, tree burning
and spreading, falling 3DO wrecks and tree reproduction in the per-tick pass,
and mission placements. Sound, effects, multiplayer reporting, model objects
and GAF sequence lookups go through `FeatureHost`. Reproduction's row divides
the plot index by the map height, as 3.1c does.

A burning tree lights its neighbours when its spread countdown runs out.
Ignition sets the countdown to half of `FeatureDef.spark_time` plus a draw
below that half, and the per-tick pass counts it down. `spark_time` holds the
TDF `sparktime` in ticks: the seconds times 30, truncated toward zero, with
the low 16 bits kept. Every shipped feature has `sparktime=5`, 150 ticks, so
a fire spreads 75 to 149 ticks (2.5 to 5 seconds) after it starts, if its
burn sequence lasts that long.
