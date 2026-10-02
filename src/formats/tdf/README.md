# Unit definitions

A unit's FBI file is read the way 3.1c reads it, and only there: `src/data/defs`
reads every `units/*.FBI` header (`load_unit_header`), drops the unavailable
types and sorts the rest by unit name (`unit_defs_finalize_catalog`), then
reads each kept type's whole `[UNITINFO]` section into its `UnitDef` record
(`load_unit_def`). Its TDF reader and number getters are 3.1c's: a key that
is present takes its text's value even with no number in it, and a value that
opens with `;` ends at once, so the rest of that line runs into the next key's
name.

`oa-data-unit-definitions` takes what the simulation, renderer, model loader
and script attachment read from that record:

- `unit_definition_from` fills the typed `UnitDefinition` fields from a loaded
  `UnitDef`. Names come from the tables the record refers to: its movement
  class, its sound category (a name SOUND.TDF lacks is read as a number, so it
  gives category 0, as in 3.1c), each weapon's section name (empty for weapon
  0, the stand-in for a missing or unknown name) and every category whose mask
  holds the type. Footprint, water depths and slopes are the record's, its
  movement class's when it names one. Fixed-point fields stay 16.16 integers.
- `resolve_runtime_metadata` gives the movement handle, the footprint, slope
  and water limits, the yard map the FBI loader compiled and the sensor
  ranges.
- `target_category_masks` copies each weapon's bad-target mask and the
  no-chase mask out of the category registry once every type has joined its
  categories.

The library also keeps a bounded TDF reader, `parse_tdf`, which other formats
(scenario, side and feature files) are read with.

Build and test independently:

```sh
cmake -S src/formats/tdf -B local/build-unit-definitions
cmake --build local/build-unit-definitions
ctest --test-dir local/build-unit-definitions --output-on-failure
```

Inside the engine's build, `unit-definitions-data` loads every unit of the
installation `OA_GAME_DIR` names as a match loads them, through its archives,
and checks their fields, metadata and categories.
