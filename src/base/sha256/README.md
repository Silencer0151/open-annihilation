# SHA-256

SHA-256 as FIPS 180-4 defines it. The engine uses it to check that a file is
the one it expects, such as a game release it recognises by its digest.
Target `oa-base-sha256`, header `oa/base/sha256.hpp`, namespace
`oa::base::sha256`.

`digest_of` hashes a whole message. For a message read in pieces, a
default-initialised `Hasher` starts it, `update` feeds each piece, of any
size, and `finish` returns the digest of what was fed so far, working on a
copy so that the same `Hasher` can be fed further. `parse_hex` reads a digest
written as 64 hexadecimal digits, as checksum lists write it, and runs in
constant expressions, so that a digest the engine expects is written as text
and checked when it compiles; `to_hex` writes one in lower case.

The code keeps the base layer's rules: it allocates nothing, throws nothing
and reads no host state. A message is shorter than 2^61 bytes.

`base-sha256` checks the example messages whose digests NIST publishes for
FIPS 180-4 (the empty message, "abc", the 448-bit and 896-bit messages and
one million 'a's), each fed whole and in pieces, and the hexadecimal forms.
