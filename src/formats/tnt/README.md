# Total Annihilation TNT map format

This C++20 component reads Total Annihilation 3.1c TNT maps: the 0x2000 format
the game ships and the legacy 0x1020 layout it also accepts, preserving map
cells, 32x32 palette tiles, raw feature records and the optional embedded
minimap. It also expands the tile mosaic to palette indices and exposes the
bounded feature names and attribute-grid placements consumed by later gameplay
code; palette selection and simulation stay at their separate boundaries.

## Layout

- Only 0x1020 and 0x2000 are accepted; both are read into one view. The
  current layout uses words 3/4/5 for tile map, attributes and tile pixels;
  words 6/7/8 for tile and feature counts/table; word 9 for sea level; and
  words 10/11 for minimap pointer/presence flags. Words 12/13 are zero in
  every shipped map and are decoded but not used; the legacy layout keeps its
  minimap pointer and presence flags in words 14/15.
- Loading copies `(width/2)*(height/2)` uint16 tile indices, `width*height`
  attributes and `tile_count*0x400` tile bytes. Current attributes are four
  bytes `{height:u8, feature:u16, padding:u8}`. Legacy attributes have stride
  eight, with height at byte 0, an 8-bit feature at byte 2 and the cell's
  metal at byte 6; the other bytes are not read. Bit zero of the selected
  presence word gates a minimap containing uint32 width/height followed by
  palette indices.
- Feature records are 0x84 bytes apart, and the name at record offset 4
  names the feature definition. This component keeps the entire record raw.
- Terrain drawing indexes the tile map at width/2 and selects a tile as
  `tile_index*0x400`; pixels are row-major 32x32 palette indices.

TA: Kingdoms 0x4000 maps are not accepted by 3.1c and are rejected here.

Input is capped at 256 MiB, dimensions at 4096 attribute cells per axis,
attribute cells at 16 million, tile and feature counts at 65,536, minimaps at
1024 per axis, and expanded terrain at 512 MiB. Every offset and product is
checked before allocation. Tile references outside the loaded tile set are
rejected.

```sh
cmake -S src/formats/tnt -B local/build-map-format
cmake --build local/build-map-format
ctest --test-dir local/build-map-format --output-on-failure
cmake --build local/build-map-format --target oa-map-format-inspect
local/build-map-format/oa-map-format-inspect path/to/maps/*.tnt
```

The `maps` directory of a 3.1c install holds 275 TNT files, all version
0x2000. All parse: 44,125,856 attribute cells, 1,118,476 unique-tile records,
8,298 feature records and 275 flagged minimaps.

## OTA metadata and start positions

`oa/formats/ota.hpp` parses the sibling text metadata used to select mission schemas.
It exposes `GlobalHeader` display metadata and every numbered schema's type and
`StartPos` records. `select_multiplayer_schema` scans the multiplayer schema
types (`Type=Network 1` to `Network 4`) and prefers the schema with the exact
player count, else the largest. Each start record gives kind 1, a zero-based
suffix, signed 16-bit X and signed 16-bit Z. Skirmish capacity counts kind-1
records, and a matching index converts X/Z to signed 16.16 commander
coordinates. The OTA `memory` and `numplayers` literals are kept as text
without interpreting it. The `size` literal is also retained separately and is
not substituted for `memory`.

The parser caps input, section count, nesting, values and start records. All
275 OTA files of a 3.1c install parse: 635 schemas and 1,569 total start
records across those schemas.
