# Total Annihilation COB format

`oa-formats-cob` parses the VersionSignature 4 COB layout used by the Total
Annihilation 3.1c script assets. The header's offsets locate the tables:
`+0x18` the script entry table, `+0x1c` the script name offsets, `+0x20` the
piece name offsets, `+0x24` the code and `+0x28` the name pool. A loaded
script is identified by the file's size and hash. The parser decodes the
eleven little-endian header words, code words, script entry-word indices,
absolute script-name offsets and absolute piece-name offsets with bounded
allocation and section validation. `static_variable_count` is retained in
`CobHeader` for the VM construction layer.

The parser intentionally rejects VersionSignature 6. That is the TA:
Kingdoms format, whose extra subheader and sound-name table are not part of
the 3.1c target. The parser has no Kingdoms compatibility claim.

The section ordering and count limits are bounded parser policy; they prevent
overlapping tables and excessive allocations, and their diagnostics are the
parser's own. The header's sixth word, tentatively the sound count, is
retained and checked against the value the game's v4 files store (`0`). The
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
