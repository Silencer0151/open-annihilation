# Text

Copying and appending zero-terminated text in fixed-size character fields,
such as the names the canonical records hold and the engine's text
buffers. Target `oa-base-text`, header `oa/base/text.hpp`, namespace
`oa::base::text`.

- `copy_padded` copies a text's characters into a field, up to the text's
  terminating zero or the field's end, and fills the rest of the field with
  zero bytes. Text as long as the field fills it and leaves no terminating
  zero, so the field holds exactly the bytes a fixed-size record stores.
- `copy_terminated` copies as much of a text as fits in a field with a
  terminating zero after it, and leaves the bytes after that zero as they
  were.
- `append_terminated` adds as much of a text as fits to the end of the text
  a field holds, again with a terminating zero after it.

None of them writes outside the field it is given, and `copy_padded` reads
no further into its text than the field is long. Every compiler the engine
supports accepts them as they are written: Visual Studio's C library marks
its own functions for these copies as unsafe, and the replacements it
offers are missing from the other platforms' C libraries.

The module is in the base layer and is only a header: it allocates nothing
and throws nothing. Its test, `base-text`, covers text shorter than, as long
as and longer than each field.
