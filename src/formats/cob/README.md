# Total Annihilation COB format

`oa-formats-cob` parses the COB unit scripts of Total Annihilation 3.1c.
The header's offsets locate the tables: `+0x18` the script entry table,
`+0x1c` the script name offsets, `+0x20` the piece name offsets, `+0x24` the
code and `+0x28` the name pool. A loaded script is identified by the file's
size and hash. The parser decodes the eleven little-endian header words,
code words, script entry-word indices, absolute script-name offsets and
absolute piece-name offsets with bounded allocation and section validation.
`static_variable_count` is retained in `CobHeader` for the VM construction
layer.

The game reads every table through these offsets and never looks at the
VersionSignature word (`+0x00`), and so does the parser: the word is kept in
`CobHeader` and not checked. The game's own scripts store 4; some add-ons
ship scripts that store 6 or 7 with the same tables, and those load as they
do in the game. A version 6 header carries two more words after the eleven,
a sound-name table's offset and its count; the parser does not read them,
nor any other bytes the offsets leave out. A file whose offsets or tables
break the bounds below is rejected whatever its version, and the unit loader
then loads the unit without a script
([unit-spawn](../../sim/unit-spawn/README.md#parsed-asset-adapter)).

The section ordering and count limits are bounded parser policy; they prevent
overlapping tables and excessive allocations, and their diagnostics are the
parser's own. Each script and piece copies its name, even where entries share
one pool string, so the names one file copies are bounded in all
(`ParseLimits::max_total_name_bytes`, 64 KiB). The header's sixth word, tentatively the sound count, is
retained and checked against the value the game's files store (`0`). The
format component does not depend on `script-vm`: the parsed `code` and
`entry_points` are the inputs for the integration layer to construct
`oa::sim::script_vm::Program`, while names, piece order and header metadata
remain available to the game model. No opcode meaning is inferred here; the
interpreter lives in `src/sim/script-vm`.

Build independently:

```sh
cmake -S src/formats/cob -B /tmp/oa-script-format-build -DBUILD_TESTING=ON
cmake --build /tmp/oa-script-format-build
ctest --test-dir /tmp/oa-script-format-build --output-on-failure
```
