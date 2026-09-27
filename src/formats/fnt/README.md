# Font formats and GUI text raster

This component reads the game's FNT 1-bit bitmap format and the GAF-backed
GUI fonts. It retains palette indices and a separate coverage mask so a covered
pixel remains distinguishable from a transparent skip even when both have the
same numeric palette index.

GUI behaviour follows 3.1c. At startup the game loads `anims/hattfont12.gaf`
and `anims/hattfont11.gaf`; each shipped archive has one 256-frame sequence.
Text width is the sum of glyph frame widths, and line height is the height of
frame `0x49` plus two. While loading, the game also subtracts frame `0x49`'s
height from every glyph's signed Y origin; `hattfont12` consequently changes
from raw origin 11 to runtime origin -1. Drawing ignores control bytes,
advances but does not draw byte `0x20`, and draws the remaining bytes as GAF
frames, origin-aware and drawn normally.

Disk fonts load from `fonts\<name>.FNT`. When the language string is nonempty,
`fonts-<language>\<name>.FNT` is kept if that file opens. The last dotted
suffix is removed before `.FNT` is appended, including a dot in the language
directory. Missing and empty files are fatal.

The FNT disk layout is a little-endian 16-bit height and a second 16-bit word,
256 absolute 16-bit offsets, then a width byte and a continuous row-major
MSB-first bitmap for each present glyph. The second word, 1, 2 or 3 in the
shipped fonts, is kept as `word_after_height` and not interpreted. Parsing
rejects out-of-range dimensions, offsets, truncated bitmaps, and files over
the component's named input limit.
