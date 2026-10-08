# Asset formats

The asset reader is a native C++20 implementation of documented container and
image formats. The HPI directory layout, encryption and SQSH framing, and PCX
field and scanline handling follow the published format descriptions; the LZ77
core, SQSH payload transform and payload checksum produce exactly the bytes
3.1c writes.

## HPI version 1

`oa::HpiArchive` accepts the `HAPI` marker and version `0x00010000`. Other HPI
versions are rejected. The 20-byte little-endian header is plaintext. Its raw
key byte is rotated left by two bits; every later encrypted byte is XORed with
that key and the low byte of its absolute file position. A zero key leaves the
archive plaintext.

`HpiArchive::open` reads an archive through an `ArchiveSource` that the code
opening it supplies: the asset store's open file, or bytes in memory. It does
not open files itself; a malformed archive comes back as a `DecodeError` at
its archive offset, as entry reads do.

The directory consists of absolute offsets to directory nodes, nine-byte
entries, names, and file records. Paths returned by `entries()` use `/`
separators. Lookup by `read()` is ASCII case-insensitive and accepts either
slash style. Unsafe components, duplicate case-insensitive paths, invalid
offsets, reused directory nodes, cycles, excessive nesting, and unsupported
file methods are rejected while opening the archive.

Uncompressed file records contain encrypted bytes directly. Compressed file
records begin with one little-endian stored-size value per 64 KiB output chunk,
followed by the chunks. Each chunk has a packed 19-byte `SQSH` header:

| Offset | Size | Meaning |
| ---: | ---: | --- |
| 0 | 4 | `SQSH` marker |
| 4 | 1 | chunk version (1 or 2) |
| 5 | 1 | compression: 0 stored, 1 HPI LZ77, 2 zlib |
| 6 | 1 | add/XOR payload encoding flag |
| 7 | 4 | stored payload size |
| 11 | 4 | decoded size |
| 15 | 4 | unsigned sum of stored payload bytes |

The checksum is checked before reversing the optional per-byte transform
`decoded[i] = (stored[i] - i) ^ i`. The transformed bytes are then stored,
LZ77-decoded, or zlib-decoded according to the chunk header. The chunk table,
header, checksum, and final file record must all agree on their sizes, and a
zlib stream must reach its end, with its check value intact, at exactly the
decoded size.

Parsing is allocation-bounded: directories are limited to 256 MiB and one
million file-or-directory entries, individual stored chunks to 1 MiB,
individual extracted files to 512 MiB, paths to 4096 bytes, and directory
nesting to 128 levels. Every range is checked against the actual archive before it is
read or allocated: reading a whole entry first checks that a stored entry lies
inside the archive and that a compressed one has room for each chunk's size
slot and header, a compressed entry fills its buffer one decoded chunk at a
time, and a stored entry that reads short is an error rather than
zero-padded. Reads return `oa::base::bytes::Decoded` values: the bytes, or the
error's code, archive offset and message; opening an archive still throws.

## PCX

`oa::decode_pcx` reads the 128-byte little-endian PCX header and returns packed
RGB bytes. It supports raw or PCX-RLE encoded scanlines in the two layouts used
by the asset tooling:

- 8 bits per pixel, one indexed plane, with the standard `0x0c` marker and
  768-byte RGB palette at the end of the file;
- 8 bits per plane, three planes stored as red, green, then blue for each row.

`BytesPerLine` padding is decoded but omitted from the RGB result. Coordinate
underflow, short rows, RLE runs crossing a plane boundary, missing indexed
palettes, unsupported layouts, and trailing pixel data are rejected, as a
`Decoded` error with its code and file offset. Output is limited to 64
megapixels and checked for integer overflow before allocation.

The API intentionally does not embed Total Annihilation's shared palette. An
indexed PCX without its own 256-color palette cannot be converted to accurate
RGB in isolation and is rejected; callers that obtain palette-less indexed
images should pair them with an explicitly sourced palette in a future
palette-aware API.

## Resource precedence

`oa::AssetStore` follows the game's lookup order: loose files first, then
mounted HPI archives in insertion order. Mounting appends archives, and names
compare without case. The caller supplies mount order explicitly. Archive
discovery groups revision GP3, CCX, UFO, then at most ten successfully mounted
HPI files, then every `*.hpi` on each CD-ROM root; within each group
`discover()` reproduces the NTFS enumeration order (names compared
upper-cased). The game runs that scan at startup with the game directory
standing in for the disc root, which is where a GOG-style install keeps the
disc archives. Do not silently substitute alphabetical order and call it the
game's precedence.

A `DiscoveryPlan` names the revision archive, the pattern of each group and
the hpi limit; the default plan is the base game's (`rev31.GP3`, `*.CCX`,
`*.UFO`, ten `*.HPI`). It may also name installation archives, mounted in
the order written after the ufo group and before the hpi group and left out
of the other groups; an empty list leaves the order unchanged. A mod
profile's layout replaces them. The store may
also be built over several folders layered in order, a mod folder over a base
folder: a path resolves from the first folder that holds it, listings and
each discovery group merge the folders by name (the earlier folder's file,
the later folder's spelling, as copying one over the other leaves it), and
the hpi limit counts the merged group, so the layered folders read exactly as
a copied install of the same files (`hpi-layering`). An empty file wins its
path like any other: the TDF loader refuses a zero-length file, as 3.1c's
does, so it hides the copy beneath it and contributes nothing.

The portable store resolves each loose path component without ASCII case,
accepts either slash, and reports the winning source. Case collisions and
relative traversal are rejected as explicit portable policies. A loose file
counts only while, every link followed, it lies inside the folder it was
found in: a link to a file or folder inside that folder works, and one that
leads outside it is no loose file, so the archives' copy is read instead.
The walk that marks the archive files loose files hide takes each folder
once, so a link to a folder above it cannot recurse. Archives discovered in
a folder are mounted wherever their links lead. A corrupt
winning archive entry raises an error rather than selecting a different copy.
Archives can disagree: an installation's overlay CCX may hold a 2308-byte
main-menu layout where `totala1.hpi` holds a 2310-byte one.
