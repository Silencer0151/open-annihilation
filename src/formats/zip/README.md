# Zip archives

This module reads and writes zip archives in memory. A director bundle
(`.oamovie`) is a zip archive of one `.oascript` and the recording it names:
the application opens a bundle with the reader and makes one with the
writer. The module opens no files itself; the caller hands it the archive's
bytes.

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

## Limitations

The reader keeps the whole archive and each entry in memory, which the
limits bound to 1 GiB each. It does not read archives with data before the
first entry (self-extracting archives), split archives or the 64-bit
extension, and it ignores the extended time and attribute fields, since a
bundle carries neither.
