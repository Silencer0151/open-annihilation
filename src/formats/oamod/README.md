# Mod profile text

The strict YAML subset a mod profile (`oamod.yaml`) is written in, read
into a node tree. Target `oa-formats-oamod`, header `oa/formats/oamod.hpp`,
namespace `oa::formats::oamod`. The resolver in
[src/data/mod-profile](../../data/mod-profile/README.md) gives the tree its
meaning.

`read_document` takes a profile's bytes and returns its top-level mapping;
`read_value` reads one value of any kind, as a host's option for one game is
written. Mappings keep their entries in the order they are written, each
with its key; scalars keep whether they were quoted and the text of a plain
scalar as written; every node and key carries its 1-based line and byte
column. A text that breaks a rule gives a `ReadError` naming the `Rule` and
where; `rule_message` describes it. Nothing throws, and the input is
bounded before anything is allocated for it.

The rules, which the header states in full:

- UTF-8 without a byte order mark, at most 256 KiB, one document whose top
  level is a mapping, at most 16 levels of nesting;
- block and flow mappings and sequences, freely mixed; plain, single-quoted
  and double-quoted scalars on one line; `#` comments; one optional `---`
  before the document and `...` after it;
- refused by name: tags, anchors, aliases, merge keys, directives, complex
  keys, block scalars, a second document, duplicate keys, tabs in
  indentation, control characters;
- plain scalars resolve as YAML 1.2's core schema does with decimal numbers
  only, so `yes`, `off`, `0x10`, `1_000` and `2024-12-01` stay strings;
- keys are strings or integers (`71: unit.my-id`); a quoted `'71'` is a
  different key from `71`.

Numbers are exact (`Number`): an integer within ±2^53, or a decimal of at
most 15 significant digits whose leading digit lies within 10^±300. In
those bounds a number is the one double its text reads as, so readers that
use doubles agree with this one, and `canonical_number_text` writes it as
the JSON canonicalization scheme (RFC 8785) writes that double, digit by
digit with no floating point. `compare_numbers`, `integer_value` and
`number_to_double` serve the resolver and the records.

`tools/oamod_yaml.py` reads the same subset for the registry generator
(`tools/gen_mod_registry.py`).

`formats-oamod` checks what each rule accepts, every refusal with its rule
and position, the limits, and the numbers' canonical text and comparison.

Known limitations: a scalar ends on its line (YAML's multi-line plain and
quoted scalars are refused), and block scalars are refused; neither appears
in the profiles the subset was made for.
