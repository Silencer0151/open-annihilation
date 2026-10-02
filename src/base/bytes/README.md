# Bytes

`oa-base-bytes` (`oa/base/bytes.hpp`) is how the engine's decoders read
files and network messages, and how they say what was wrong with them.

- `ByteReader` is a cursor over a byte span. It reads 8-, 16- and 32-bit
  little-endian fields at the cursor or at an absolute offset, takes byte
  ranges and sub-readers, and reads NUL-terminated strings up to a length
  limit. No read goes outside the span: one that does not fit returns zero
  or an empty span and records a `truncated` error at the offset it was
  attempted. The first failure is kept and every later read fails too, so a
  decoder reads a whole record and checks `ok()` once.
- `load_le16`, `load_le32`, `store_le16` and `store_le32` read and write a
  field at a pointer, for loops over ranges whose bounds are already checked.
- `Decoded<T>` is what a decoder returns: the value, or a `DecodeError`
  holding a shared `DecodeCode`, the byte offset of the problem, a static
  message and, when the decoder has one, its own narrower code in `detail`.
  Code above the formats layer that treats a decode failure as fatal turns
  it into an exception of its own, with the file or entry name added.

The module is in the base layer: it allocates nothing, throws nothing and
uses no heap containers. Its test, `base-bytes`, covers every way a read can
fall off the end of the input.
