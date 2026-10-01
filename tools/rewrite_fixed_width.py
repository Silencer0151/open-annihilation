#!/usr/bin/env python3
# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

"""Rewrite std::-qualified fixed-width integer types to their unqualified names in the engine's sources.

docs/development/conventions.md writes the fixed-width types unqualified: int32_t, not
std::int32_t. This tool makes every engine source say so. It reads the C,
C++ and Objective-C sources the engine owns (tools/engine_files.py: the
files Git tracks, and new ones it does not ignore, less the nested projects
and third-party directories) and rewrites each name the style check's
qualified-fixed-width rule counts (tools/check_style.py: int8_t to int64_t
and uint8_t to uint64_t, intptr_t, intmax_t and the least and fast types,
signed and unsigned) in code and in comments. It leaves alone:

  - the contents of string and character literals, raw strings included,
    which are data;
  - a name a using-declaration declares ('using std::int32_t;', however it
    is split across lines or among other names), which has no unqualified
    form; the run lists each one for a hand edit and fails.

The unqualified names come from <stdint.h>. Strictly, <cstdint> promises
only the std:: names, but every toolchain the engine supports declares both
there (docs/development/conventions.md). So that each rewritten file names what it uses,
one that includes none of <cstdint>, <cinttypes>, <stdint.h> and
<inttypes.h> itself gains '#include <cstdint>' (<stdint.h> in a C file):

  - among its first run of standard-library includes, in order when that
    run is sorted and after it when not;
  - else after the last include before its first line of code, as a block of
    its own;
  - else after '#pragma once' or its include guard's #define;
  - else after the comments that open the file.

Only includes outside any #if count. A byte order mark stays first. A
fragment another file includes in the middle of itself (.inc, .inl) gains
no include: the run lists it instead.

A run rewrites the files in place and prints what it changed; --check
changes nothing and fails when a file would change; --list prints every
rewritten name. A second run over its own output changes nothing. Once the
tree holds no qualified name, lower the style baseline to the new counts:

  python3 tools/check_style.py --write-baseline

Exit status: 0 when the tree is (or, with --check, already was) rewritten,
1 when --check finds work or a using-declaration or fragment needs a hand
edit, 2 when the tree cannot be read.
"""
import argparse
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from check_style import QUALIFIED_FIXED_WIDTH_RE  # noqa: E402
from engine_files import C_SUFFIXES, FRAGMENT_SUFFIXES, engine_sources  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
# Bound on a source file.
MAX_SOURCE_BYTES = 16 << 20
# The prefixes of a raw string literal, and the longest delimiter one may
# have.
RAW_STRING_PREFIXES = frozenset({"R", "u8R", "uR", "UR", "LR"})
MAX_RAW_DELIMITER = 16
# How far back a quote looks for the token in front of it.
MAX_TOKEN_LOOKBACK = 64
TOKEN_BEFORE_QUOTE_RE = re.compile(r"(?<![\w'])([A-Za-z0-9_']*)$")
# A statement that is a using-declaration, from its start up to a name it
# declares: 'using', then no '=' (which would make it an alias).
USING_DECLARATION_RE = re.compile(r"\s*(?:(?:public|protected|private)\s*:\s*)?using\s+(?!namespace\b)[^=]*")
STATEMENT_BOUNDARIES = ";{}"
BYTE_ORDER_MARK = "\ufeff"
# Headers that declare the fixed-width types.
FIXED_WIDTH_HEADER_RE = re.compile(r"^\s*#\s*include\s*<(?:cstdint|cinttypes|stdint\.h|inttypes\.h)>")
CPP_HEADER = "cstdint"
C_HEADER = "stdint.h"
INCLUDE_RE = re.compile(r"^\s*#\s*include\s*([<\"])([^>\"]*)[>\"]")
# A standard library header: one lower-case word, no extension, no directory.
STANDARD_HEADER_RE = re.compile(r"^[a-z][a-z0-9_]*$")
CONDITIONAL_OPEN_RE = re.compile(r"^\s*#\s*if(?:n?def)?\b")
CONDITIONAL_CLOSE_RE = re.compile(r"^\s*#\s*endif\b")
PRAGMA_ONCE_RE = re.compile(r"^\s*#\s*pragma\s+once\b")
GUARD_IFNDEF_RE = re.compile(r"^\s*#\s*ifndef\s+(\w+)")
GUARD_DEFINE_RE = re.compile(r"^\s*#\s*define\s+(\w+)\s*$")


@dataclass
class Rewrite:
    """What a run did, or would do, to one file."""
    name: str = ""
    text: str = ""
    names: list = field(default_factory=list)       # (line, qualified name) rewritten
    using: list = field(default_factory=list)       # (line, qualified name) a using-declaration declares
    added_include: str = ""                         # the header added, if any
    fragment_without_include: bool = False


def literal_and_comment_spans(text):
    """Finds the string and character literals, and the comments, of a C or C++ source.

    A quote inside a comment opens nothing, and a comment opener inside a
    literal opens nothing; a digit separator (1'000) is no character
    literal.

    @param text the source
    @return (the (start, end) offsets of each literal, quotes included; those of each comment), each in order
    """
    spans = []
    comments = []
    i = 0
    n = len(text)
    while i < n:
        c = text[i]
        if text.startswith("//", i):
            end = text.find("\n", i)
            end = n if end < 0 else end
            comments.append((i, end))
            i = end
            continue
        if text.startswith("/*", i):
            end = text.find("*/", i + 2)
            end = n if end < 0 else end + 2
            comments.append((i, end))
            i = end
            continue
        if c not in "\"'":
            i += 1
            continue
        token = TOKEN_BEFORE_QUOTE_RE.search(text[max(0, i - MAX_TOKEN_LOOKBACK):i]).group(1)
        if c == '"' and token in RAW_STRING_PREFIXES:
            open_paren = text.find("(", i + 1, i + 2 + MAX_RAW_DELIMITER)
            if open_paren >= 0:
                close = ")" + text[i + 1:open_paren] + '"'
                end = text.find(close, open_paren + 1)
                end = n if end < 0 else end + len(close)
                spans.append((i, end))
                i = end
                continue
        if c == "'" and token[:1].isdigit():
            i += 1
            continue
        start = i
        i += 1
        while i < n and text[i] != c and text[i] != "\n":
            i += 2 if text[i] == "\\" else 1
        i = min(i + 1, n)
        spans.append((start, i))
    return spans, comments


def code_mask(text, spans):
    """Blanks a source's literals, comments and preprocessor directives, keeping every offset.

    @param text the source
    @param spans the (start, end) offsets of its literals and comments
    @return the source with each character of those, and of every directive line, but line ends made a space
    """
    chars = list(text)
    for start, end in spans:
        for index in range(start, end):
            if chars[index] != "\n":
                chars[index] = " "
    lines = "".join(chars).split("\n")
    continued = False
    for index, line in enumerate(lines):
        if continued or line.lstrip().startswith("#"):
            continued = line.rstrip().endswith("\\")
            lines[index] = " " * len(line)
    return "\n".join(lines)


def in_using_declaration(masked, start):
    """Tells whether a name of a source's code lies in a using-declaration, which declares it.

    The statement is read back to the ';', '{' or '}' before it, so a
    declaration split across lines, or one that declares several names,
    is found whole.

    @param masked the source as code_mask blanks it
    @param start the name's offset
    @return true when the name's statement opens with 'using' and is no alias or using-directive
    """
    if masked[start].isspace():
        return False  # in a comment or a directive
    boundary = max(masked.rfind(character, 0, start) for character in STATEMENT_BOUNDARIES)
    return USING_DECLARATION_RE.fullmatch(masked, boundary + 1, start) is not None


def rewrite_names(text):
    """Rewrites the qualified fixed-width names of a source outside its literals.

    @param text the source
    @return the rewritten text, the (line, name) pairs rewritten, and the (line, name) pairs a
        using-declaration declares, left as they are
    """
    spans, comments = literal_and_comment_spans(text)
    masked = None
    out = []
    names = []
    using = []
    last = 0
    span_index = 0
    for match in QUALIFIED_FIXED_WIDTH_RE.finditer(text):
        start = match.start()
        while span_index < len(spans) and spans[span_index][1] <= start:
            span_index += 1
        if span_index < len(spans) and spans[span_index][0] <= start:
            continue
        line = text.count("\n", 0, start) + 1
        if masked is None:
            masked = code_mask(text, spans + comments)
        if in_using_declaration(masked, start):
            using.append((line, match.group(0)))
            continue
        out.append(text[last:start])
        out.append(match.group(0)[len("std::"):])
        last = match.end()
        names.append((line, match.group(0)))
    out.append(text[last:])
    return "".join(out), names, using


def code_lines(lines):
    """Marks the lines of a source that hold code: not blank, not only a comment, not a directive.

    @param lines the source's lines
    @return one bool per line
    """
    marks = []
    in_comment = False
    continued = False
    for line in lines:
        stripped = line.strip()
        is_code = False
        if continued:
            continued = stripped.endswith("\\")
        elif in_comment:
            end = stripped.find("*/")
            if end >= 0:
                in_comment = False
                rest = stripped[end + 2:].strip()
                is_code = bool(rest) and not rest.startswith("//")
        elif stripped.startswith("#"):
            continued = stripped.endswith("\\")
        elif stripped.startswith("/*"):
            end = stripped.find("*/", 2)
            if end < 0:
                in_comment = True
            else:
                rest = stripped[end + 2:].strip()
                is_code = bool(rest) and not rest.startswith("//")
        elif stripped and not stripped.startswith("//"):
            is_code = True
        marks.append(is_code)
    return marks


def include_position(lines, header):
    """Finds where a source's new #include goes, and whether it needs a blank line to set it apart.

    @param lines the source's lines, without line ends
    @param header the header to include, without brackets
    @return (index to insert before, lines to insert)
    """
    marks = code_lines(lines)
    first_code = next((index for index, is_code in enumerate(marks) if is_code), len(lines))
    directive = f"#include <{header}>"
    # The include guard, if the file opens with one: its #if is no condition.
    guard_if = guard_define = -1
    directives = [index for index in range(first_code) if lines[index].lstrip().startswith("#")]
    if len(directives) >= 2:
        opening = GUARD_IFNDEF_RE.match(lines[directives[0]])
        defining = GUARD_DEFINE_RE.match(lines[directives[1]])
        if opening and defining and opening.group(1) == defining.group(1):
            guard_if, guard_define = directives[0], directives[1]
    depth = 0
    includes = []  # (index, name, standard) of the includes outside any #if
    for index in range(first_code):
        line = lines[index]
        if index == guard_if:
            continue
        if CONDITIONAL_OPEN_RE.match(line):
            depth += 1
            continue
        if CONDITIONAL_CLOSE_RE.match(line):
            depth = max(0, depth - 1)
            continue
        included = INCLUDE_RE.match(line)
        if included and depth == 0:
            includes.append((index, included.group(2), included.group(1) == "<"
                             and STANDARD_HEADER_RE.match(included.group(2)) is not None))
    standard = [entry for entry in includes if entry[2]]
    if standard:
        run = [standard[0]]
        for entry in standard[1:]:
            if entry[0] != run[-1][0] + 1:
                break
            run.append(entry)
        run_names = [name for _, name, _ in run]
        if run_names == sorted(run_names):
            for index, name, _ in run:
                if header < name:
                    return index, [directive]
        return run[-1][0] + 1, [directive]
    if includes:
        return includes[-1][0] + 1, ["", directive]
    for index in range(first_code):
        if PRAGMA_ONCE_RE.match(lines[index]):
            return index + 1, ["", directive]
    if guard_define >= 0:
        return guard_define + 1, ["", directive]
    opening_comments = 0
    for index in range(first_code):
        stripped = lines[index].strip()
        if stripped.startswith("#") or not stripped:
            break
        opening_comments = index + 1
    return opening_comments, [directive, ""]


def add_include(text, header):
    """Adds '#include <header>' to a source where include_position puts it.

    @param text the source
    @param header the header, without brackets
    @return the source with the include added
    """
    ending = "\r\n" if "\r\n" in text else "\n"
    lines = text.split(ending)
    trailing = lines and lines[-1] == ""
    if trailing:
        lines.pop()
    index, added = include_position(lines, header)
    if added and added[0] == "" and (index == 0 or lines[index - 1].strip() == ""):
        added = added[1:]
    if added and added[-1] == "" and (index >= len(lines) or lines[index].strip() == ""):
        added = added[:-1]
    lines[index:index] = added
    return ending.join(lines) + (ending if trailing else "")


def rewrite_source(name, text):
    """Rewrites one source: its qualified names, and the include its unqualified names need.

    @param name the path relative to the root, which gives the language
    @param text the source
    @return what the rewrite did
    """
    result = Rewrite(name=name)
    bom = BYTE_ORDER_MARK if text.startswith(BYTE_ORDER_MARK) else ""
    body, result.names, result.using = rewrite_names(text[len(bom):])
    if not result.names:
        result.text = text
        return result
    suffix = Path(name).suffix
    if not any(FIXED_WIDTH_HEADER_RE.match(line) for line in body.splitlines()):
        if suffix in FRAGMENT_SUFFIXES:
            result.fragment_without_include = True
        else:
            result.added_include = C_HEADER if suffix in C_SUFFIXES else CPP_HEADER
            body = add_include(body, result.added_include)
    result.text = bom + body
    return result


def run(root, check, listing):
    """Rewrites, or with check only reports, every engine source under root.

    @param root the tree's root
    @param check true to change nothing and report what would change
    @param listing true to print every rewritten name
    @return exit status
    """
    files_changed = names_changed = includes_added = 0
    needs_hand_edit = False
    for name in engine_sources(root):
        path = root / name
        try:
            if path.stat().st_size > MAX_SOURCE_BYTES:
                print(f"rewrite_fixed_width: {name}: larger than {MAX_SOURCE_BYTES} bytes", file=sys.stderr)
                return 2
            data = path.read_bytes()
            text = data.decode("utf-8")
        except (OSError, UnicodeDecodeError) as error:
            print(f"rewrite_fixed_width: {name}: {error}", file=sys.stderr)
            return 2
        result = rewrite_source(name, text)
        for line, qualified in result.using:
            print(f"{name}:{line}: {qualified} is declared by a using-declaration, which has no unqualified "
                  f"form; remove it by hand")
            needs_hand_edit = True
        if result.fragment_without_include:
            print(f"{name}: a fragment that uses the unqualified names; make sure every file that includes it "
                  f"includes <{CPP_HEADER}> first")
            needs_hand_edit = True
        if result.text == text:
            continue
        files_changed += 1
        names_changed += len(result.names)
        includes_added += bool(result.added_include)
        if listing:
            for line, qualified in result.names:
                print(f"{name}:{line}: {qualified}")
        if result.added_include:
            print(f"{name}: #include <{result.added_include}> added")
        if not check:
            path.write_bytes(result.text.encode("utf-8"))
    verb = "would rewrite" if check else "rewrote"
    print(f"rewrite_fixed_width: {verb} {names_changed} name(s) in {files_changed} file(s); "
          f"{includes_added} file(s) {'would gain' if check else 'gained'} an #include")
    if files_changed and not check:
        print("rewrite_fixed_width: lower the style baseline: python3 tools/check_style.py --write-baseline")
    if check and files_changed:
        return 1
    return 1 if needs_hand_edit else 0


SELF_TEST_CASES = (
    # (name, source, expected output)
    ("names.cpp",
     '#include <cstdint>\nstd::uint32_t a = std::int8_t{1}; // std::uint16_t\n'
     'const char* s = "std::uint64_t"; char c = \'x\'; auto r = R"x(std::int32_t)x";\n',
     '#include <cstdint>\nuint32_t a = int8_t{1}; // uint16_t\n'
     'const char* s = "std::uint64_t"; char c = \'x\'; auto r = R"x(std::int32_t)x";\n'),
    ("separator.cpp",
     "#include <cinttypes>\nconstexpr std::uint32_t big = 1'000'000; std::int64_t t = ::std::intmax_t(2);\n",
     "#include <cinttypes>\nconstexpr uint32_t big = 1'000'000; int64_t t = ::intmax_t(2);\n"),
    ("sorted.cpp",
     "// A file.\n#include \"oa/a.hpp\"\n\n#include <array>\n#include <span>\n\nstd::uint8_t x{};\n",
     "// A file.\n#include \"oa/a.hpp\"\n\n#include <array>\n#include <cstdint>\n#include <span>\n\nuint8_t x{};\n"),
    ("unsorted.cpp",
     "#include <vector>\n#include <array>\nstd::uint8_t x{};\n",
     "#include <vector>\n#include <array>\n#include <cstdint>\nuint8_t x{};\n"),
    ("project_only.hpp",
     "#pragma once\n\n#include \"oa/a.hpp\"\n#include <SDL3/SDL.h>\n\nnamespace oa {\nstd::uint8_t x();\n}\n",
     "#pragma once\n\n#include \"oa/a.hpp\"\n#include <SDL3/SDL.h>\n\n#include <cstdint>\n\nnamespace oa {\n"
     "uint8_t x();\n}\n"),
    ("conditional.cpp",
     "#ifdef _WIN32\n#include <windows.h>\n#endif\nstd::uint8_t x{};\n",
     "#include <cstdint>\n\n#ifdef _WIN32\n#include <windows.h>\n#endif\nuint8_t x{};\n"),
    ("pragma.hpp",
     "#pragma once\n\nstd::uint16_t y();\n",
     "#pragma once\n\n#include <cstdint>\n\nuint16_t y();\n"),
    ("guarded.hpp",
     "#ifndef OA_X_HPP\n#define OA_X_HPP\nstd::uint16_t y();\n#endif\n",
     "#ifndef OA_X_HPP\n#define OA_X_HPP\n\n#include <cstdint>\nuint16_t y();\n#endif\n"),
    ("comment_first.cpp",
     "// What this is.\n// More.\nstd::uint16_t y();\n",
     "// What this is.\n// More.\n#include <cstdint>\n\nuint16_t y();\n"),
    ("already.cpp",
     "#include <cstdint>\nuint32_t a;\n",
     "#include <cstdint>\nuint32_t a;\n"),
    ("using.cpp",
     "#include <cstdint>\nusing std::uint32_t;\nstd::int32_t b;\n",
     "#include <cstdint>\nusing std::uint32_t;\nint32_t b;\n"),
    ("table.inc",
     "std::uint8_t x{};\n",
     "uint8_t x{};\n"),
    ("crlf.cpp",
     "#include <array>\r\nstd::uint8_t x{};\r\n",
     "#include <array>\r\n#include <cstdint>\r\nuint8_t x{};\r\n"),
    ("names.h",
     "#include \"oa/core/types.h\"\nstd::uint8_t x;\n",
     "#include \"oa/core/types.h\"\n\n#include <stdint.h>\nuint8_t x;\n"),
    ("using_several.cpp",
     "#include <cstdint>\nusing std::uint32_t, ::std::int32_t;\nstd::int16_t b;\n",
     "#include <cstdint>\nusing std::uint32_t, ::std::int32_t;\nint16_t b;\n"),
    ("using_split.cpp",
     "#include <cstdint>\nnamespace oa {\nusing\n    std::uint32_t; // std::uint8_t\n}\n",
     "#include <cstdint>\nnamespace oa {\nusing\n    std::uint32_t; // uint8_t\n}\n"),
    ("alias.cpp",
     "#include <cstdint>\n// using std::uint8_t;\nusing u32 = std::uint32_t;\n",
     "#include <cstdint>\n// using uint8_t;\nusing u32 = uint32_t;\n"),
    ("bom.cpp",
     f"{BYTE_ORDER_MARK}// A file.\nstd::uint8_t x{{}};\n",
     f"{BYTE_ORDER_MARK}// A file.\n#include <cstdint>\n\nuint8_t x{{}};\n"),
    ("bom_included.cpp",
     f"{BYTE_ORDER_MARK}#include <cstdint>\nstd::uint8_t x{{}};\n",
     f"{BYTE_ORDER_MARK}#include <cstdint>\nuint8_t x{{}};\n"),
)
# The names each case's using-declarations declare, by case; none in the others.
SELF_TEST_USING = {
    "using.cpp": [(2, "std::uint32_t")],
    "using_several.cpp": [(2, "std::uint32_t"), (2, "std::int32_t")],
    "using_split.cpp": [(4, "std::uint32_t")],
}


def self_test():
    """Rewrites small sources whose results are known, twice each; returns the exit status."""
    failures = 0
    for name, source, expected in SELF_TEST_CASES:
        result = rewrite_source(name, source)
        again = rewrite_source(name, result.text)
        if result.text != expected:
            print(f"rewrite_fixed_width self-test: {name}: got\n{result.text!r}\nexpected\n{expected!r}")
            failures += 1
        elif again.text != result.text:
            print(f"rewrite_fixed_width self-test: {name}: a second run changed the result")
            failures += 1
        if result.using != SELF_TEST_USING.get(name, []):
            print(f"rewrite_fixed_width self-test: {name}: using-declarations of {result.using}, "
                  f"expected {SELF_TEST_USING.get(name, [])}")
            failures += 1
    if not rewrite_source("table.inc", "std::uint8_t x{};\n").fragment_without_include:
        print("rewrite_fixed_width self-test: table.inc: a fragment without <cstdint> was not reported")
        failures += 1
    if failures:
        print(f"rewrite_fixed_width self-test: {failures} failure(s)")
        return 1
    print(f"rewrite_fixed_width self-test: {len(SELF_TEST_CASES)} case(s) passed")
    return 0


def main(argv=None):
    """Parses the command line and runs the rewrite or its self-test; returns the exit status."""
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--root", type=Path, default=ROOT, help="the tree to rewrite (default: %(default)s)")
    parser.add_argument("--check", action="store_true", help="change nothing; fail when a file would change")
    parser.add_argument("--list", action="store_true", help="print every rewritten name")
    parser.add_argument("--self-test", action="store_true", help="rewrite built-in cases and compare the results")
    args = parser.parse_args(argv)
    if args.self_test:
        return self_test()
    root = args.root.resolve()
    if not root.is_dir():
        print(f"rewrite_fixed_width: no such tree: {root}", file=sys.stderr)
        return 2
    return run(root, args.check, args.list)


if __name__ == "__main__":
    sys.exit(main())
