# PE resources

Finds where a resource's data lies in a Windows program file, a Portable
Executable (PE) image, so that the engine can copy game data a program file
carries without running it. Target `oa-formats-pe`, header
`oa/formats/pe.hpp`, namespace `oa::formats::pe`.

`find_resource` takes the whole file as a byte span and a `ResourceKey`: a
type name, an id and a language. It reads the DOS header, the PE signature,
the file header, the 32-bit or 64-bit optional header and its resource data
directory, and the section table, then walks the resource directory from its
root through the type, the id and the language to the data entry, and
returns the data's offset and size in the file. The type is matched by name,
ASCII letters in either case; the id and the language by number.

Nothing in the image is run, mapped or relocated. Every offset, count and
size is checked against the bytes before it is read: at most
`limit::sections` sections, directory tables and name strings inside the
resource directory's bytes, and the directory and the data inside one
section's bytes in the file (its virtual size, when smaller than its size in
the file, bounds it). A subdirectory that leads back to a directory already
on the path is refused. A failed lookup returns an `Error` and the file
offset of the field or structure that stopped it; `describe` words the error
for a message.

The module reads what it needs and no more: it does not check the image's
checksum, certificates, imports or code, and it walks exactly three levels,
as resource directories are laid out.

`formats-pe` builds images in the test (`tests/oa/test/pe_image.hpp`, which
other tests use through `oa-formats-pe-test-image`) and checks lookups by
type, id and language with both optional headers, then malformed images:
truncated headers, section tables, directories and names, too many sections,
loops in the directory, a data entry or a directory where the other belongs,
and RVAs and sizes outside the sections. It also cuts an image at every
length and corrupts every byte of its headers and directory, and checks that
each lookup ends with an error or a range inside the file.
