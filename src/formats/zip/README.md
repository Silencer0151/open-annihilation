# Zip archives

This module reads and writes zip archives in memory, and reads them as a
stream from a file. A director bundle (`.oamovie`) is a zip archive of one
`.oascript` and the recording it names: the application opens a bundle with
the in-memory reader and makes one with the writer. A mod package
(`.oamod`) is a zip archive of a mod's folder, of any size: the application
unpacks it with the streamed reader. The module opens no files itself; the
caller hands it the archive's bytes, or a hook that reads them.

## Entry points

`oa/formats/zip.hpp`, namespace `oa::formats::zip`:

- `read_directory` finds the end record, walks the central directory and
  checks every entry's local header against its central record.
- `find_entry` looks an entry up by its exact name.
- `read_entry` reads one entry's data, inflating it when it is deflated,
  and checks its size and CRC-32.
- `write_archive` writes entries stored, in the order given.
- `name_is_safe` is the name rule both directions apply; `crc32_of` is the
  format's CRC-32; `zip_status_message` describes a status.

`oa/formats/zip/stream.hpp`, the same namespace:

- `read_stream_directory` reads an archive's central directory through a
  `SourceHooks` read hook, within `StreamLimits`.
- `EntryStream` reads one entry's data in steps of a budget of archive
  bytes, handing each piece to a `SinkHooks` write hook, and checks its size
  and CRC-32 at its end.
- `read_stream_entry` reads a small entry whole into memory.

Every function returns its errors as values (`ZipError`: a status, the byte
offset of the record at fault and the entry's name) and leaves its output
empty on failure.

## What the reader takes

One archive on one disk, without the 64-bit extension, of at most
`max_archive_bytes`, with at most `max_entry_count` entries, each stored
(method 0) or deflated (method 8) and at most `max_entry_bytes` once read.
The end record is the last one in the final 65,557 bytes whose comment
reaches exactly to the end of the archive, so a comment may hold anything.
Extra fields and comments of entries are skipped. An entry whose name ends
in '/' names a directory and holds nothing.

It refuses, with the status named:

- disk numbers other than zero, or a disk's entry count unlike the total
  (`several_disks`);
- any 16-bit field holding 0xFFFF or 32-bit field holding 0xFFFFFFFF that
  the 64-bit extension would replace, in the end record, a central record or
  a local header (`zip64`);
- the encryption flags, in either record (`encrypted`);
- methods other than stored and deflated (`unsupported_method`);
- names `name_is_safe` refuses: empty, starting with '/' or a drive letter
  and colon, holding NUL or '\\', with an empty, "." or ".." component, or
  ending in a name Windows keeps for a device, such as `con.txt` or `NUL`
  (`unsafe_name`); names longer than `max_name_bytes` (`name_too_long`); a
  name given twice, compared byte for byte (`duplicate_name`);
- a local header without its signature, or whose name, method, CRC-32 or
  sizes differ from its central record (`bad_local_record`). When the local
  header's data-descriptor flag is set, its CRC-32 and sizes may also be
  zero, as a writer that cannot seek leaves them;
- a stored entry whose two sizes differ, a directory that holds data, or a
  deflated entry that claims more than 1032 bytes for each byte of its data,
  more than any deflate stream holds (`size_mismatch`). This last rule
  refuses an entry before its buffer is allocated;
- a central directory whose records do not fill it exactly
  (`bad_central_record`), and any record or data that runs past the
  directory or the archive (`truncated`).

Deflated data is inflated as a raw stream into a buffer of exactly the
recorded size; the stream must end there and use all of the recorded
compressed data (`size_mismatch` otherwise), and data that is not deflate
is `inflate_failed`. The data's CRC-32 must be the recorded one
(`crc_mismatch`).

## What the streamed reader takes

What the in-memory reader takes, of any size up to `StreamLimits` (64 GiB,
65,535 entries, an 8 MiB directory, 4 GiB an entry), and the 64-bit
extension: the 64-bit end record its locator names, whose counts, size and
offset must equal the end record's own wherever those are not sentinels,
and each record's 64-bit extra field for exactly the fields that hold their
sentinel. It holds, besides the directory, about 44 KiB of decoder state and
two 64 KiB buffers for an open entry, and nothing that grows with an entry's
size: each step reads at most its budget of the archive and hands on at most
four times as much, or 256 KiB.

Besides the in-memory reader's refusals it refuses:

- 64-bit records that are missing where a field holds a sentinel, or that
  disagree with the end record (`bad_zip64_record`);
- a directory larger than its limit (`directory_too_large`);
- an entry whose local header, name and data reach the next entry's local
  header or the directory, as the central record's name gives them and again
  as the local header's own name and extra field give them
  (`overlapping_entries`): no two entries share data;
- a name marked UTF-8 (general purpose bit 11) that is not well-formed UTF-8
  (`bad_name_encoding`);
- a read that fails (`read_failed`), and a write hook that refuses the data
  (`write_failed`).

A name not marked UTF-8 is read as code page 437, the format's default,
unless a Unicode path extra field (0x7075) whose CRC-32 matches the recorded
name gives its UTF-8 spelling. The backslashes of an entry made on MS-DOS or
Windows (hosts 0, 10 and 14) become '/'. An entry made on Unix or macOS
(hosts 3 and 19) whose external attributes give the link file type is
marked `symbolic_link`, and one of another file type than a file or a
folder `special_file`; the reader refuses neither, and the caller decides.
Inflated data past an entry's recorded size is never handed on: its first
piece stops the read with `size_mismatch`.

## What the writer makes

Entries stored uncompressed, each with version 2.0 needed and made by,
00:00 on 1980-01-01, no extra field, no comment and zero attributes; the
UTF-8 flag (bit 11) is set only for a name with a byte outside 7-bit ASCII.
Local headers and data come first, in the order given, then the central
directory and an end record with no comment. The same entries always give
the same bytes on every platform. The writer applies the reader's limits and
name rule, and refuses a directory entry with data.

## Tests

`formats-zip` (`tests/zip_test.cpp`): write and read round trips; the
writer's exact bytes for fixed entries, pinned by SHA-256; archives made by
zlib's deflate in the test, with and without a data descriptor; three
archives made by the Info-ZIP zip command, kept as bytes in
`tests/tool_archives.hpp` (stored, deflated, a directory, extra fields, a
data descriptor, and one whose local header uses the 64-bit extension);
end record comments; every refusal of the reader and the writer; and a
malformed sweep: every prefix and suffix of two archives, and random byte
changes under a fixed seed.

`formats-zip-stream` (`tests/stream_test.cpp`): stored and deflated entries
read in steps of 1, 7 and 65,536 bytes, each step within its budget; small
entries written with the 64-bit extension, the zip command's archive whose
local header uses it, and a sparse archive of 5 GiB whose one entry lies
past 4 GiB; the zip command's other archives read as the in-memory reader
reads them; names in code page 437, from a Unicode path field and from
MS-DOS; links and special files marked; every refusal; and a malformed sweep
in which no allocation exceeds 1 MiB. `oa/test/raw_zip.hpp` (in
`tests/support/include`) builds the archives field by field, as the mod
packages' tests do too.

## Limitations

The in-memory reader keeps the whole archive and each entry in memory, which
the limits bound to 1 GiB each, and refuses the 64-bit extension. Neither
reader reads archives with data before the first entry (self-extracting
archives) or split archives, and both ignore the extended time and
attribute fields, since neither a bundle nor a mod package needs them.
