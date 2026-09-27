# Unit definitions

`oa-data-unit-definitions` provides a bounded reusable TDF reader and converts a
Total Annihilation `[UNITINFO]` FBI record into native typed fields used by the
simulation, renderer, model loader, and script attachment code. Lookup is ASCII
case-insensitive and unconsumed fields remain available in `unknown_fields`.

Field defaults, narrowing and derived values match 3.1c: `objectname` falls
back to `unitname`; standing orders default to 2; the self-destruct countdown
defaults to 5; `bankscale` and `damagemodifier` default to integer `0x10000`;
both move-rate fields default to twice the already converted `maxvelocity`;
bytes and words keep their low bits; and boolean fields use only bit zero.
Fixed-point fields stay 16.16 integers (the parsed value times 65536,
truncated toward zero with the low 32 bits kept) rather than native floats.

`load_unit_catalog` builds the unit catalog. Its `CatalogAssetReader` asks the
host resource layer for the effective merged `units/*.FBI` view, loads each
winning file, applies the runtime compatibility verdict, sorts by `unitname`
case-insensitively, and assigns one-based 16-bit type IDs. Slot zero remains
reserved as in the game.

`load_movement_classes` and `resolve_runtime_metadata` turn MOVEINFO plus an
FBI record into the movement handle, final footprint/slope/water constraints,
compiled per-cell yard mask, and runtime sensor ranges.

`resolve_unit_categories` builds the 512-type inverted category registry and
resolves each unit's three weapon bad-target masks plus its no-chase mask for
automatic targeting.

Build and test independently:

```sh
cmake -S src/formats/tdf -B local/build-unit-definitions
cmake --build local/build-unit-definitions
ctest --test-dir local/build-unit-definitions --output-on-failure
```

Inside the engine's build, `unit-definitions-data` loads every unit
definition of the installation `OA_GAME_DIR` names through its archives and
resolves it against the installation's movement classes and categories.
