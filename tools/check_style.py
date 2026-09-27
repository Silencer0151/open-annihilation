#!/usr/bin/env python3
# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

"""Count breaches of the engine's style rules per directory, against a baseline that may only shrink.

The check reads every tracked C, C++ and Objective-C source (SOURCE_SUFFIXES)
under --root, leaving out the nested projects that build on the engine (a
directory whose CMakeLists.txt calls project() and takes OA_ENGINE_DIR), and
counts each finding against the
directory that owns its file: the nearest directory holding a CMakeLists.txt,
else the file's first two path components (src/app). Rules:

  qualified-fixed-width  a fixed-width integer type written with std::
                         (write int32_t);
  name-offset            a name built from a record offset: field, flags,
                         value, byte, word, dword or offset joined to two or
                         more hex digits;
  name-address           a name that ends in six to eight hex digits, one of
                         them a digit, after an underscore;
  name-placeholder       a generated placeholder name: FUN, DAT, LAB or PTR,
                         an underscore and hex digits;
  name-unresolved        an unresolved name that carries its offset (unk and
                         0x);
  name-unknown           a placeholder name that numbers what it does not
                         know (unknown_3, a flag's UNKNOWN_BIT5, Unknown1,
                         unk_7): name the field, constant or bit by what it
                         holds; a value that means "not known at run time"
                         carries no number and is left alone;
  flags-raw-mask         a hex literal masked into, or with, a name that ends
                         in flags (&, |, ^, their assignments, ~): name the
                         bit;
  member-initializer     a scalar, enumeration or pointer member of a plain
                         struct without a {} or = initializer;
  offset-comment         a comment that gives a byte offset ('+0x' and hex
                         digits; '+' and two to five hex digits holding a
                         digit and a hex letter or a leading zero; 'offset'
                         and a hex number) outside the file-format
                         directories (FORMAT_DIRECTORIES): a record's
                         OA_ASSERT_OFFSET pins give its offsets, and a
                         comment names the field instead;
  doc-block              a function declared or defined in a header without
                         a /// line directly above its declaration; a
                         defaulted or deleted function (= default,
                         = delete), whose behaviour a block could only
                         restate, and the out-of-class definition of a
                         qualified name, documented where it is declared,
                         are left out;
  tier-virtual,          virtual, throw and the heap-allocating standard
  tier-throw,            containers (HEAP_CONTAINERS) in the directories of
  tier-heap-container    the core, base and simulation tiers (TIER_DIRECTORIES),
                         outside their tests.

The name rules read whole lines, comments and string literals included.
offset-comment reads comments only. The other rules read code, with comments
and the contents of string and character literals left out.

What counts as a plain struct: a struct (not a class or a union) outside a
union, in a C++ file (a .h, .c or .m file is C, where members take no
initializers), that declares no constructor and no virtual function. A
member counts when its type is a built-in arithmetic type, a fixed-width or
size type, an enumeration declared anywhere in the tree, an alias of one of
those declared with 'using', or any pointer. Bit-fields, references, static
members and members of local structs inside functions are not read.

Known limits: the checks are textual. A declaration that a macro builds is
not seen, a member whose type is an alias made with typedef does not count,
and a doc block that a preprocessor line separates from its declaration is
not directly above it. Enumerations and aliases are matched by their
unqualified name, so one declared in any namespace counts.

Baseline. The run compares its counts with a baseline of counts per directory
and rule (--baseline FILE, by default tools/style-baseline.json under --root):
it fails when a count grows or a directory gains a rule the baseline does not
hold, and says when a count can be lowered. --write-baseline records the
current counts; it refuses to raise any count unless --accept-growth is given
too (for a change that moves files between directories), so that growth is
a reviewed edit. --report prints the counts per directory; --list prints
every finding, or those of --rule.

Each finding prints as '<path>:<line>: <rule>: <text>'. Exit status is 1 when
anything is found beyond the baseline and 2 when the tree or the baseline
cannot be read.
"""
import argparse
import bisect
import json
import os
import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
# The baseline a run reads by default, relative to --root.
DEFAULT_BASELINE = Path("tools") / "style-baseline.json"
BASELINE_VERSION = 1
# Bound on the baseline file and on a source file.
MAX_BASELINE_BYTES = 4 << 20
MAX_SOURCE_BYTES = 16 << 20
# The files the check reads, and those among them that are headers (the
# doc-block rule) and that are C (no member initializers).
SOURCE_SUFFIXES = frozenset({".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".inc", ".inl", ".m", ".mm"})
HEADER_SUFFIXES = frozenset({".h", ".hh", ".hpp", ".hxx"})
C_SUFFIXES = frozenset({".c", ".h", ".m"})
# A directory whose files own themselves to a module when no CMakeLists.txt
# is found: the first this many components of the path.
FALLBACK_DIRECTORY_PARTS = 2
# Directories a tree that Git does not track (an unpacked source tree) leaves
# out, as the checkout's ignore rules do: build trees and local dependencies
# directly under the root, and Git's and Python's own directories anywhere. A
# directory holding BUILD_TREE_MARKER is a build tree wherever it lies.
ROOT_UNTRACKED_DIR_RE = re.compile(r"^(?:build.*|local)$")
UNTRACKED_DIR_RE = re.compile(r"^(?:\.git|__pycache__)$")
BUILD_TREE_MARKER = "CMakeCache.txt"
# A nested project's CMakeLists.txt: it calls project() and takes the engine
# checkout as OA_ENGINE_DIR.
PROJECT_CALL_RE = re.compile(r"(?im)^\s*project\s*\(")
ENGINE_DIR_VARIABLE_RE = re.compile(r"\bOA_ENGINE_DIR\b")
# The rules, in the order the report lists them.
RULES = (
    "qualified-fixed-width", "name-offset", "name-address", "name-placeholder", "name-unresolved", "name-unknown",
    "flags-raw-mask", "member-initializer", "offset-comment", "doc-block", "tier-virtual", "tier-throw",
    "tier-heap-container",
)
# Where an offset comment describes a file's byte layout: the game data
# decoders and the saved-game code.
FORMAT_DIRECTORIES = ("src/formats/", "src/data/persist/")
# The directories of the core, base and simulation tiers, where per-tick
# state has no virtual dispatch, exceptions or heap-allocating containers:
# the core records, base's arithmetic, geometry and timing, the simulation's
# modules, and the campaign files the scenario reads.
TIER_DIRECTORIES = ("src/core/", "src/base/", "src/sim/", "src/data/campaign/")
# A directory of tests, whose files the tier rules leave out.
TEST_DIRECTORY = "tests"
# How many findings of one directory and rule a failure prints.
SHOWN_FINDINGS = 40

FIXED_WIDTH_TYPE = r"u?int(?:8|16|32|64|ptr|max)_t|u?int_(?:least|fast)(?:8|16|32|64)_t"
QUALIFIED_FIXED_WIDTH_RE = re.compile(r"\bstd::(?:" + FIXED_WIDTH_TYPE + r")\b")
IDENTIFIER_RE = re.compile(r"\b[A-Za-z_][A-Za-z0-9_]*\b")
NAME_RULES = (
    ("name-offset", re.compile(r"(^|_)field_?[0-9a-f]{2,}($|_)")),
    ("name-offset", re.compile(r"(^|_)flags_[0-9a-f]+($|_)")),
    ("name-offset", re.compile(r"(^|_)(value|byte|word|dword|offset)_[0-9a-f]{2,}($|_)")),
    ("name-placeholder", re.compile(r"(^|_)(FUN|DAT|LAB|PTR)_[0-9a-fA-F]+")),
    ("name-address", re.compile(r"_(?=[0-9a-f]*[0-9])[0-9a-f]{6,8}$")),
    ("name-unresolved", re.compile(r"(^|_)unk_?0x[0-9a-f]+", re.IGNORECASE)),
    ("name-unknown", re.compile(r"unknown_?(?:bit_?)?\d+", re.IGNORECASE)),
    ("name-unknown", re.compile(r"(^|_)unk_?\d+($|_)", re.IGNORECASE)),
)
FLAGS_NAME = r"(?:[A-Za-z_][A-Za-z0-9_]*?)?flags\d*"
BIT_OPERATOR = r"(?:&(?!&)|\|(?!\|)|\^)"
HEX_OPERAND = r"~?\s*\(?\s*(?:static_cast<[^<>]*>\s*\(\s*)?0x[0-9a-f]+"
FLAGS_RAW_MASK_RE = re.compile(
    r"\b" + FLAGS_NAME + r"\b(?:\s*\[[^\]]*\])?\s*\)?\s*" + BIT_OPERATOR + r"=?\s*" + HEX_OPERAND
    + r"|0x[0-9a-f]+\w*\s*\)?\s*" + BIT_OPERATOR + r"\s*[\w.>-]*?" + FLAGS_NAME + r"\b", re.IGNORECASE)
OFFSET_COMMENT_RE = re.compile(
    r"(?<![\w+])\+0x[0-9a-f]+"
    r"|(?<![\w+])\+(?=[0-9a-f]*[0-9])(?=[0-9a-f]*[a-f]|0)[0-9a-f]{2,5}(?!\w)"
    r"|\boffsets?\s+(?:of\s+)?0x[0-9a-f]+", re.IGNORECASE)
VIRTUAL_RE = re.compile(r"\bvirtual\b")
THROW_RE = re.compile(r"\bthrow\b")
HEAP_CONTAINERS = (
    "vector", "basic_string", "string", "wstring", "u8string", "u16string", "u32string", "map", "multimap",
    "unordered_map", "unordered_multimap", "set", "multiset", "unordered_set", "unordered_multiset", "deque",
    "list", "forward_list", "queue", "priority_queue", "stack",
)
HEAP_CONTAINER_RE = re.compile(r"\bstd::(?:" + "|".join(HEAP_CONTAINERS) + r")\b")
ENUM_NAME_RE = re.compile(r"\benum\s+(?:class\s+|struct\s+)?(?:\[\[[^\]]*\]\]\s*)?([A-Za-z_]\w*)\s*(?::[^;{]*)?\{")
ALIAS_RE = re.compile(r"\busing\s+([A-Za-z_]\w*)\s*=\s*([^;]+);")

# The words of the built-in arithmetic types.
ARITHMETIC_WORDS = frozenset({
    "bool", "char", "char8_t", "char16_t", "char32_t", "wchar_t", "short", "int", "long", "signed", "unsigned",
    "float", "double",
})
SIZE_TYPE_RE = re.compile(r"^(?:std::)?(?:" + FIXED_WIDTH_TYPE + r"|size_t|ptrdiff_t)$")
# Words a member's type may carry that do not change what it is.
TYPE_QUALIFIERS = frozenset({"const", "volatile", "mutable", "struct", "enum", "class"})
# Keywords that start a declaration in a struct body that is not a data
# member.
NOT_MEMBER_RE = re.compile(
    r"^(?:using|typedef|static|friend|template|enum|struct|class|union|static_assert|constexpr|inline|extern"
    r"|public|private|protected|OA_ASSERT_\w*)\b")
# Keywords that start a statement at namespace or class scope that declares
# no function.
NOT_FUNCTION_RE = re.compile(r"^(?:using|typedef|static_assert|namespace|return|enum)\b")
# Words that are never a function's name.
NOT_FUNCTION_NAMES = frozenset({
    "if", "while", "for", "switch", "return", "sizeof", "alignof", "decltype", "static_assert", "throw", "new",
    "delete", "noexcept", "requires", "catch", "alignas", "__attribute__", "__declspec",
})
# An operator's symbols after 'operator', replaced by a plain name so that '<'
# and '(' in them read as neither brackets nor a parameter list.
OPERATOR_RE = re.compile(
    r"\boperator\s*(?:<=>|<<=|>>=|->\*|\(\s*\)|\[\s*\]|<<|>>|<=|>=|==|!=|&&|\|\||\+\+|--|->|[-+*/%^&|~!=<>,]=?)")
OPERATOR_NAME = "operator_"
# Parts of a declaration that hold parentheses but no parameter list.
ATTRIBUTE_RE = re.compile(r"\[\[.*?\]\]|\b(?:alignas|__attribute__|__declspec|decltype|noexcept)\s*(?=\()",
                          re.DOTALL)
# A lone macro word on a line of its own (OA_CORE_BEGIN), which starts no
# statement.
LEADING_MACRO_RE = re.compile(r"^(?:\s*[A-Z][A-Z0-9_]*[ \t]*\n)+")
TYPE_HEAD_RE = re.compile(
    r"^(?P<typedef>typedef\s+)?(?P<kind>struct|class|union)\b\s*(?:\[\[.*?\]\]\s*)*(?:alignas\s*\([^)]*\)\s*)?"
    r"(?P<name>[A-Za-z_]\w*(?:\s*::\s*[A-Za-z_]\w*)*)?\s*(?:<[^{]*>\s*)?(?:final\s*)?(?::(?!:)[^{]*)?$", re.DOTALL)
NAMESPACE_HEAD_RE = re.compile(r"^(?:inline\s+)?namespace\b|^extern\s*\"\"$")
TEMPLATE_PREFIX_RE = re.compile(r"^template\s*<")
# The end of a defaulted or deleted function's declaration.
DEFAULTED_RE = re.compile(r"=\s*(?:default|delete)\s*$")
TRAILING_NAME_RE = re.compile(r"((?:~\s*)?[A-Za-z_]\w*)\s*$")
ARRAY_SUFFIX_RE = re.compile(r"(?:\[[^\]]*\]\s*)+$")
FUNCTION_POINTER_RE = re.compile(r"\(\s*\*\s*(?:const\s+)?([A-Za-z_]\w*)\s*\)\s*\(")
ACCESS_WORDS = frozenset({"public", "private", "protected"})
# What comes before a '<' that opens a template argument list: a name or a
# closing '>'.
TEMPLATE_OPEN_RE = re.compile(r"[\w>]\s*$")
# The token before a quote: a raw string's prefix (R, u8R, LR), a character
# literal's (u8, L) or the digits of a number a digit separator splits.
RAW_PREFIX_RE = re.compile(r"(?<![\w'])([A-Za-z0-9_']*)$")
MAX_TOKEN_LOOKBACK = 64
RAW_STRING_PREFIXES = frozenset({"R", "u8R", "uR", "UR", "LR"})
# The longest delimiter a raw string may have.
MAX_RAW_DELIMITER = 16


@dataclass
class SourceLine:
    """One line of a source file, as written and split into its code and its comments."""
    text: str = ""
    code: str = ""
    comment: str = ""
    doc: bool = False
    directive: bool = False


@dataclass(frozen=True)
class Finding:
    """One breach of a rule."""
    path: str
    line: int
    rule: str
    text: str

    def printed(self):
        """Formats the finding as a run prints it."""
        return f"{self.path}:{self.line}: {self.rule}: {self.text}"


class BaselineError(Exception):
    """A baseline that cannot be read."""


def split_lines(text):
    """Splits a source into lines of code and comments.

    String and character literals keep their quotes and lose their contents;
    a digit separator (1'000) is no character literal. The lines of a
    preprocessor directive, continuations included, are marked.

    @param text the source
    @return one SourceLine per line
    """
    lines = []
    code, comment = [], []
    state = "code"
    raw_end = ""
    directive = False
    line_start = True
    begin = 0
    i = 0
    n = len(text)
    while i <= n:
        c = text[i] if i < n else "\n"
        if c == "\n":
            raw = text[begin:i]
            leading = raw.lstrip()
            lines.append(SourceLine(raw, "".join(code), "".join(comment),
                                    leading.startswith("///") and not leading.startswith("////"), directive))
            directive = directive and raw.rstrip().endswith("\\")
            if state in ("line-comment", "string", "char"):
                state = "code"
            code, comment = [], []
            line_start = True
            i += 1
            begin = i
            if i > n or (i == n and begin == n):
                break
            continue
        if state == "block-comment":
            if text.startswith("*/", i):
                state = "code"
                i += 2
            else:
                comment.append(c)
                i += 1
            continue
        if state == "line-comment":
            comment.append(c)
            i += 1
            continue
        if state == "raw-string":
            if text.startswith(raw_end, i):
                code.append('"')
                state = "code"
                i += len(raw_end)
            else:
                i += 1
            continue
        if state in ("string", "char"):
            if c == "\\":
                i += 2
                continue
            if c == ('"' if state == "string" else "'"):
                code.append(c)
                state = "code"
            i += 1
            continue
        if text.startswith("//", i):
            state = "line-comment"
            i += 2
            continue
        if text.startswith("/*", i):
            state = "block-comment"
            i += 2
            continue
        if line_start and not c.isspace():
            line_start = False
            directive = directive or c == "#"
        token = RAW_PREFIX_RE.search("".join(code[-MAX_TOKEN_LOOKBACK:])) if c in "\"'" else None
        if c == '"' and token and token.group(1) in RAW_STRING_PREFIXES:
            open_paren = text.find("(", i + 1, i + 1 + MAX_RAW_DELIMITER + 1)
            if open_paren != -1:
                raw_end = ")" + text[i + 1:open_paren] + '"'
                code.append('"')
                state = "raw-string"
                i = open_paren + 1
                continue
        code.append(c)
        if c == '"':
            state = "string"
        elif c == "'" and not (token and token.group(1)[:1].isdigit()):
            state = "char"
        i += 1
    return lines


def owner_directory(name, build_directories):
    """Returns the directory that owns a file: its nearest directory holding a CMakeLists.txt.

    @param name file path relative to the root
    @param build_directories directories relative to the root that hold a CMakeLists.txt
    @return the directory relative to the root; without a CMakeLists.txt above the file, its first two path
        components
    """
    parts = name.split("/")[:-1]
    for end in range(len(parts), 0, -1):
        directory = "/".join(parts[:end])
        if directory in build_directories:
            return directory
    return "/".join(parts[:FALLBACK_DIRECTORY_PARTS]) or "."


def top_level_index(text, targets, start=0):
    """Returns the first index of a character of targets outside brackets, or -1."""
    depth = 0
    angle = 0
    i = start
    while i < len(text):
        c = text[i]
        if c in "([{":
            if depth == 0 and angle == 0 and c in targets:
                return i
            depth += 1
        elif c in ")]}":
            depth = max(0, depth - 1)
        elif depth == 0 and c == "<" and TEMPLATE_OPEN_RE.search(text, 0, i) and not text.startswith("<<", i):
            angle += 1
        elif depth == 0 and c == ">" and angle > 0 and not (i and text[i - 1] == "-"):
            angle -= 1
        elif depth == 0 and angle == 0 and c in targets:
            if c == ":" and (text.startswith("::", i) or (i and text[i - 1] == ":")):
                i += 1
                continue
            if c == "=" and (text.startswith("==", i) or (i and text[i - 1] in "=!<>")):
                i += 1
                continue
            return i
        i += 1
    return -1


def split_top_level(text, separator):
    """Splits text at a separator outside brackets."""
    parts = []
    start = 0
    while True:
        index = top_level_index(text, separator, start)
        if index < 0:
            parts.append(text[start:])
            return parts
        parts.append(text[start:index])
        start = index + 1


def strip_template_prefix(head):
    """Removes leading template parameter lists from a declaration."""
    while TEMPLATE_PREFIX_RE.match(head):
        depth = 0
        for i, c in enumerate(head):
            if c == "<":
                depth += 1
            elif c == ">":
                depth -= 1
                if depth == 0:
                    head = head[i + 1:].lstrip()
                    break
        else:
            return ""
    return head


def remove_parenthesised(text, match):
    """Removes a word matched at match.start() together with the parenthesised group after it."""
    start = match.end()
    depth = 0
    for i in range(start, len(text)):
        if text[i] == "(":
            depth += 1
        elif text[i] == ")":
            depth -= 1
            if depth == 0:
                return text[:match.start()] + " " + text[i + 1:]
    return text[:match.start()]


def normalise_head(head):
    """Returns a declaration without its template prefix, attributes and operator symbols."""
    head = strip_template_prefix(head.strip())
    head = OPERATOR_RE.sub(OPERATOR_NAME, head)
    while True:
        match = ATTRIBUTE_RE.search(head)
        if match is None:
            return head.strip()
        if match.group(0).startswith("[["):
            head = head[:match.start()] + " " + head[match.end():]
        else:
            head = remove_parenthesised(head, match)


def function_name(head, class_name):
    """Returns the name a declaration declares a function by, or None when it declares none.

    @param head declaration up to its ';' or its body's '{'
    @param class_name name of the enclosing struct, class or union, or None
    @return the name as written, qualified with '::' when the declaration defines a qualified name
    """
    head = normalise_head(head)
    if not head or NOT_FUNCTION_RE.match(head):
        return None
    paren = top_level_index(head, "(")
    if paren < 0:
        return None
    before = head[:paren].rstrip()
    if top_level_index(before, "=") >= 0:
        return None
    if head[paren + 1:].lstrip().startswith(("*", "&", "^")):
        return None
    match = TRAILING_NAME_RE.search(before)
    if match is None:
        return None
    name = re.sub(r"\s+", "", match.group(1))
    if name.lstrip("~") in NOT_FUNCTION_NAMES:
        return None
    if name == OPERATOR_NAME:
        name = "operator"
    rest = before[:match.start()].rstrip()
    if rest.endswith("::"):
        qualifier = re.search(r"((?:[A-Za-z_]\w*\s*(?:<[^()]*>)?\s*::\s*)+)$", rest)
        return (qualifier.group(1).replace(" ", "") if qualifier else "::") + name
    if not rest:
        if class_name is not None and name.lstrip("~") == class_name:
            return name
        return None
    if rest.endswith((".", "->", ",", "(", "=", "?", "+", "-", "!", "|")):
        return None
    return name


@dataclass
class Scope:
    """One brace-delimited region of a source."""
    kind: str
    name: str = None
    start_line: int = 0
    typedef: bool = False
    in_union: bool = False
    plain: bool = False
    members: list = None


@dataclass
class Structure:
    """What the declaration scan of one file found."""
    functions: list = None
    members: list = None
    records: list = None


def scan_structure(lines, c_file, scalar_names):
    """Finds the functions, the uninitialized members and the records a file declares.

    @param lines the file's lines as split_lines returns them
    @param c_file true when the file is C, whose structs take no member initializers
    @param scalar_names enumerations and aliases of scalar types declared in the tree
    @return functions as (line, name, defaulted or deleted), members as (line, name), and records as (names, first line,
        last line), a record's names being its tag and the name a typedef gives it
    """
    text = "\n".join("" if line.directive else line.code for line in lines)
    starts = [0]
    for line in lines[:-1]:
        starts.append(starts[-1] + len("" if line.directive else line.code) + 1)

    def line_of(offset):
        return bisect.bisect_right(starts, offset)

    functions, members, records = [], [], []
    scopes = [Scope("file")]
    statement = None
    depth = 0
    pending_alias = None

    def class_name():
        top = scopes[-1]
        return top.name.split("::")[-1].strip() if top.kind in ("struct", "class", "union") and top.name else None

    def head_start(offset, head):
        prefix = LEADING_MACRO_RE.match(head)
        skip = len(prefix.group(0)) if prefix else 0
        skip += len(head[skip:]) - len(head[skip:].lstrip())
        return offset + skip, head[skip:]

    def add_function(offset, head):
        name = function_name(head, class_name())
        if name is None:
            return False
        top = scopes[-1]
        if top.kind in ("struct", "class", "union") and name.lstrip("~") == class_name():
            top.plain = False
        if VIRTUAL_RE.search(head):
            top.plain = False
        functions.append((line_of(offset), name, bool(DEFAULTED_RE.search(head))))
        return True

    def add_members(offset, statement_text):
        top = scopes[-1]
        if top.kind != "struct" or top.in_union or c_file or top.members is None:
            return
        for name in uninitialized_members(statement_text, scalar_names):
            top.members.append((line_of(offset), name))

    i = 0
    n = len(text)
    while i < n:
        c = text[i]
        top = scopes[-1]
        if top.kind == "block":
            if c == "{":
                scopes.append(Scope("block"))
            elif c == "}":
                scopes.pop()
            i += 1
            continue
        if c.isspace():
            i += 1
            continue
        if statement is None:
            statement = i
            depth = 0
        if c in "([":
            depth += 1
        elif c in ")]":
            depth = max(0, depth - 1)
        elif depth == 0 and c == ";":
            offset, head = head_start(statement, text[statement:i])
            statement = None
            if pending_alias is not None:
                alias = head.strip()
                if re.fullmatch(r"[A-Za-z_]\w*", alias):
                    pending_alias.add(alias)
                pending_alias = None
            if head and not add_function(offset, head):
                add_members(offset, head)
        elif depth == 0 and c == "{":
            offset, head = head_start(statement, text[statement:i])
            statement = None
            pending_alias = None
            stripped = normalise_head(head) if head else ""
            type_head = TYPE_HEAD_RE.match(strip_template_prefix(head.strip())) if head else None
            if type_head:
                kind = type_head.group("kind")
                name = (type_head.group("name") or "").replace(" ", "") or None
                scope = Scope(kind, name, line_of(offset), bool(type_head.group("typedef")),
                              top.in_union or top.kind == "union" or kind == "union")
                scope.plain = kind == "struct" and not scope.in_union and not c_file
                scope.members = []
                scopes.append(scope)
            elif head and NAMESPACE_HEAD_RE.match(stripped):
                scopes.append(Scope("namespace"))
            elif head and re.match(r"^(?:typedef\s+)?enum\b", stripped):
                scopes.append(Scope("block"))
            elif not head.strip() or head.lstrip()[0] in ",:":
                scopes.append(Scope("block"))
            elif top_level_index(stripped, "=") < 0 and add_function(offset, head):
                scopes.append(Scope("block"))
            elif stripped.endswith(")"):
                # A body after a head that declares nothing this scan knows,
                # such as a macro that opens a test.
                scopes.append(Scope("block"))
            else:
                # A brace initializer: skip it and go on with the statement.
                close = matching_brace(text, i)
                statement = offset
                i = close + 1
                continue
        elif depth == 0 and c == "}":
            closed = scopes.pop() if len(scopes) > 1 else scopes[0]
            statement = None
            if closed.kind in ("struct", "class", "union"):
                names = {closed.name.split("::")[-1]} if closed.name else set()
                records.append((names, closed.start_line, line_of(i)))
                if closed.typedef:
                    pending_alias = names
                if closed.plain and closed.members:
                    members.extend(closed.members)
        elif depth == 0 and c == ":" and not text.startswith("::", i) and text[statement:i].strip() in ACCESS_WORDS:
            statement = None
        i += 1
    return Structure(functions, members, records)


def matching_brace(text, index):
    """Returns the index of the '}' that closes the '{' at index, or the text's end."""
    depth = 0
    for i in range(index, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return i
    return len(text) - 1


def uninitialized_members(statement, scalar_names):
    """Returns the names of the scalar and pointer members a declaration leaves without an initializer.

    @param statement member declaration without its ';'
    @param scalar_names enumerations and aliases of scalar types declared in the tree
    @return the names of the declarators without an initializer
    """
    statement = normalise_head(statement)
    if not statement or NOT_MEMBER_RE.match(statement):
        return []
    pointer = FUNCTION_POINTER_RE.search(statement)
    if pointer:
        tail = statement[pointer.end() - 1:]
        close = matching_paren(tail)
        initialized = top_level_index(tail[close + 1:], "={") >= 0 if close >= 0 else True
        return [] if initialized else [pointer.group(1)]
    if top_level_index(statement, "(") >= 0 or top_level_index(statement, ":") >= 0:
        return []
    missing = []
    base_type = None
    for index, part in enumerate(split_top_level(statement, ",")):
        initializer = top_level_index(part, "={")
        declarator = (part if initializer < 0 else part[:initializer]).rstrip()
        declarator = ARRAY_SUFFIX_RE.sub("", declarator).rstrip()
        match = re.search(r"([A-Za-z_]\w*)$", declarator)
        if match is None:
            return missing
        type_text = declarator[:match.start()]
        if index == 0:
            base_type = type_text
            if not base_type.strip():
                return missing
        elif "*" not in type_text and "&" not in type_text:
            type_text = base_type
        else:
            type_text = base_type.replace("*", "").replace("&", "") + type_text
        outside = re.sub(r"<[^<>]*>", "", type_text)
        if "&" in outside:
            continue
        words = [word for word in re.findall(r"[A-Za-z_][\w:]*", outside) if word not in TYPE_QUALIFIERS]
        is_pointer = "*" in outside
        is_scalar = bool(words) and (all(word in ARITHMETIC_WORDS for word in words)
                                     or (len(words) == 1 and (SIZE_TYPE_RE.match(words[0])
                                                              or words[0].split("::")[-1] in scalar_names)))
        if (is_pointer or is_scalar) and initializer < 0:
            missing.append(match.group(1))
    return missing


def matching_paren(text):
    """Returns the index of the ')' that closes the '(' text starts with, or -1."""
    depth = 0
    for i, c in enumerate(text):
        if c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
            if depth == 0:
                return i
    return -1


@dataclass
class Source:
    """A source file the check reads."""
    name: str
    directory: str
    lines: list


def tracked(root):
    """Lists the files Git tracks under root, or when it tracks none there, every file outside build trees and caches.

    @param root the tree to list
    @return paths relative to root, with '/' separators
    """
    try:
        listing = subprocess.run(["git", "-C", str(root), "ls-files", "-z", "--", "."], capture_output=True)
    except OSError:
        listing = None
    names = [name for name in listing.stdout.decode().split("\0") if name] \
        if listing is not None and listing.returncode == 0 else []
    if names:
        return names
    for directory, subdirectories, files in os.walk(root):
        top = directory == os.fspath(root)
        subdirectories[:] = [name for name in subdirectories if not UNTRACKED_DIR_RE.match(name)
                             and not (top and ROOT_UNTRACKED_DIR_RE.match(name))
                             and not Path(directory, name, BUILD_TREE_MARKER).exists()]
        names.extend(Path(directory, name).relative_to(root).as_posix() for name in files)
    return sorted(names)


def nested_projects(root, names):
    """Finds the directories below root that hold a project building on the engine.

    @param root the tree the names are relative to
    @param names paths relative to root, as tracked() lists them
    @return each directory whose CMakeLists.txt calls project() and takes OA_ENGINE_DIR, with a trailing '/'
    """
    projects = []
    for name in names:
        if name.endswith("/CMakeLists.txt") and name.count("/") >= 1:
            try:
                text = (root / name).read_text(errors="replace")
            except OSError:
                continue
            if PROJECT_CALL_RE.search(text) and ENGINE_DIR_VARIABLE_RE.search(text):
                projects.append(name.rsplit("/", 1)[0] + "/")
    return tuple(sorted(projects))


def read_sources(root):
    """Reads the tracked sources under root outside nested projects.

    @param root the tree to read
    @return the sources, each with the directory that owns it
    """
    names = tracked(root)
    projects = nested_projects(root, names)
    build_directories = {name.rsplit("/", 1)[0] for name in names if name.endswith("/CMakeLists.txt")}
    sources = []
    for name in names:
        if name.startswith(projects) or Path(name).suffix not in SOURCE_SUFFIXES:
            continue
        path = root / name
        try:
            if path.stat().st_size > MAX_SOURCE_BYTES:
                print(f"check_style: {name}: larger than {MAX_SOURCE_BYTES} bytes; not read", file=sys.stderr)
                continue
            text = path.read_text(encoding="utf-8", errors="replace")
        except OSError:
            continue
        sources.append(Source(name, owner_directory(name, build_directories), split_lines(text)))
    return sources


def scalar_type_names(sources):
    """Returns the enumerations the sources declare and the aliases they give scalar types."""
    names = set()
    aliases = []
    for source in sources:
        code = "\n".join(line.code for line in source.lines)
        names.update(ENUM_NAME_RE.findall(code))
        aliases.extend(ALIAS_RE.findall(code))
    changed = True
    while changed:
        changed = False
        for alias, target in aliases:
            words = [word for word in re.findall(r"[A-Za-z_][\w:]*", target) if word not in TYPE_QUALIFIERS]
            if alias in names or "*" in target or "<" in target or not words:
                continue
            if all(word in ARITHMETIC_WORDS for word in words) or (
                    len(words) == 1 and (SIZE_TYPE_RE.match(words[0]) or words[0].split("::")[-1] in names)):
                names.add(alias)
                changed = True
    return frozenset(names)


def in_tier(name):
    """Tests whether a file lies in a tier directory, outside its tests."""
    return name.startswith(TIER_DIRECTORIES) and TEST_DIRECTORY not in name.split("/")[:-1]


def source_findings(source, scalar_names):
    """Returns the findings of one source.

    @param source the file, split into lines
    @param scalar_names enumerations and aliases of scalar types declared in the tree
    @return the file's findings
    """
    name = source.name
    suffix = Path(name).suffix
    findings = []
    structure = scan_structure(source.lines, suffix in C_SUFFIXES, scalar_names)
    format_file = name.startswith(FORMAT_DIRECTORIES)
    tier = in_tier(name)
    for number, line in enumerate(source.lines, start=1):
        code = line.code
        for match in QUALIFIED_FIXED_WIDTH_RE.finditer(code):
            findings.append(Finding(name, number, "qualified-fixed-width", match.group(0)))
        for match in IDENTIFIER_RE.finditer(line.text):
            identifier = match.group(0)
            for rule, pattern in NAME_RULES:
                if pattern.search(identifier):
                    findings.append(Finding(name, number, rule, identifier))
                    break
        for match in FLAGS_RAW_MASK_RE.finditer(code):
            findings.append(Finding(name, number, "flags-raw-mask", " ".join(match.group(0).split())))
        if not format_file:
            for match in OFFSET_COMMENT_RE.finditer(line.comment):
                findings.append(Finding(name, number, "offset-comment", match.group(0)))
        if tier:
            for rule, pattern in (("tier-virtual", VIRTUAL_RE), ("tier-throw", THROW_RE),
                                  ("tier-heap-container", HEAP_CONTAINER_RE)):
                for match in pattern.finditer(code):
                    findings.append(Finding(name, number, rule, match.group(0)))
    for number, member in structure.members:
        findings.append(Finding(name, number, "member-initializer", member))
    if suffix in HEADER_SUFFIXES:
        for number, function, defaulted in structure.functions:
            if defaulted or "::" in function:
                continue
            if number < 2 or not source.lines[number - 2].doc:
                findings.append(Finding(name, number, "doc-block", function))
    return findings


def tree_findings(root):
    """Returns the sources under root and the findings of all of them, in path and line order."""
    sources = read_sources(root)
    scalar_names = scalar_type_names(sources)
    findings = []
    for source in sources:
        findings.extend(sorted(source_findings(source, scalar_names), key=lambda f: (f.line, f.rule)))
    return sources, findings


def counts_of(findings, directories):
    """Counts findings per directory and rule.

    @param findings the findings to count
    @param directories each finding's file path mapped to the directory that owns it
    """
    counts = {}
    for finding in findings:
        rules = counts.setdefault(directories[finding.path], {})
        rules[finding.rule] = rules.get(finding.rule, 0) + 1
    return {directory: dict(sorted(rules.items())) for directory, rules in sorted(counts.items())}


def read_baseline(path):
    """Reads the counts per directory and rule a baseline file allows."""
    try:
        if path.stat().st_size > MAX_BASELINE_BYTES:
            raise BaselineError(f"{path}: larger than {MAX_BASELINE_BYTES} bytes")
        baseline = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        raise BaselineError(f"{path}: {error}") from error
    counts = baseline.get("counts") if isinstance(baseline, dict) else None
    if not isinstance(counts, dict) or baseline.get("version") != BASELINE_VERSION or not all(
            isinstance(rules, dict) and all(rule in RULES and isinstance(count, int) and count >= 0
                                            for rule, count in rules.items())
            for rules in counts.values()):
        raise BaselineError(f"{path}: not a version {BASELINE_VERSION} baseline of counts per directory and rule")
    return counts


def compare(counts, baseline):
    """Compares counts with a baseline.

    @return the (directory, rule, count, allowed) pairs over the baseline, and those under it
    """
    over = [(directory, rule, count, baseline.get(directory, {}).get(rule, 0))
            for directory, rules in counts.items() for rule, count in rules.items()
            if count > baseline.get(directory, {}).get(rule, 0)]
    under = [(directory, rule, counts.get(directory, {}).get(rule, 0), allowed)
             for directory, rules in sorted(baseline.items()) for rule, allowed in sorted(rules.items())
             if counts.get(directory, {}).get(rule, 0) < allowed]
    return over, under


def write_baseline(path, counts, old, accept_growth):
    """Records counts as the baseline, refusing growth over the old baseline unless accepted.

    @param path baseline file to write
    @param counts counts per directory and rule
    @param old the baseline the file holds, or None
    @param accept_growth true to record counts above the old baseline
    @return exit status
    """
    if old is not None and not accept_growth:
        over, _ = compare(counts, old)
        if over:
            for directory, rule, count, allowed in over:
                print(f"check_style: {directory}: {rule}: {count} finding(s) against a baseline of {allowed}")
            print("check_style: --write-baseline does not raise counts; fix the findings, or add "
                  "--accept-growth when files moved between directories")
            return 1
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps({"version": BASELINE_VERSION, "counts": counts}, indent=1, sort_keys=True) + "\n",
                    encoding="utf-8")
    total = sum(sum(rules.values()) for rules in counts.values())
    print(f"check_style: wrote {path}: {total} finding(s) in {len(counts)} director(ies)")
    return 0


def print_totals(counts):
    """Prints each rule's total and the number of directories that hold it."""
    for rule in RULES:
        holders = [rules[rule] for rules in counts.values() if rule in rules]
        print(f"check_style: {rule}: {sum(holders)} in {len(holders)} director(ies)")


def print_report(counts):
    """Prints the counts of every directory, rule by rule."""
    for directory, rules in counts.items():
        print(f"{directory}: {sum(rules.values())}")
        for rule in RULES:
            if rule in rules:
                print(f"  {rule}: {rules[rule]}")
    print_totals(counts)


def check(findings, counts, directories, baseline):
    """Prints what exceeds the baseline and what can be lowered; returns the exit status."""
    over, under = compare(counts, baseline)
    for directory, rule, count, allowed in over:
        print(f"check_style: {directory}: {rule}: {count} finding(s) against a baseline of {allowed}")
        shown = [finding for finding in findings if finding.rule == rule and directories[finding.path] == directory]
        for finding in shown[:SHOWN_FINDINGS]:
            print(f"  {finding.printed()}")
        if len(shown) > SHOWN_FINDINGS:
            print(f"  ... {len(shown) - SHOWN_FINDINGS} more; --list --rule {rule} prints them all")
    for directory, rule, count, allowed in under:
        print(f"check_style: {directory}: {rule}: {count} finding(s) against a baseline of {allowed}; lower it")
    if under:
        print("check_style: --write-baseline records the lower counts")
    if over:
        print("check_style: follow the rule for the new findings (tools/check_style.py describes each)")
        return 1
    print_totals(counts)
    print("check_style: clean within the baseline")
    return 0


def main(argv=None):
    """Runs the check, a report, a listing or a baseline update; returns the exit status."""
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--root", type=Path, default=ROOT, help="the tree to check (default: %(default)s)")
    parser.add_argument("--baseline", type=Path,
                        help=f"counts per directory and rule (default: --root/{DEFAULT_BASELINE})")
    parser.add_argument("--write-baseline", action="store_true", help="record the current counts as the baseline")
    parser.add_argument("--accept-growth", action="store_true", help="let --write-baseline raise counts")
    parser.add_argument("--report", action="store_true", help="print the counts per directory")
    parser.add_argument("--list", action="store_true", help="print every finding")
    parser.add_argument("--rule", choices=RULES, help="with --list, print this rule's findings only")
    args = parser.parse_args(argv)
    root = args.root.resolve()
    if not root.is_dir():
        print(f"check_style: no such tree: {root}", file=sys.stderr)
        return 2
    sources, findings = tree_findings(root)
    directories = {source.name: source.directory for source in sources}
    counts = counts_of(findings, directories)
    baseline_path = args.baseline or root / DEFAULT_BASELINE
    if args.list:
        for finding in findings:
            if args.rule is None or finding.rule == args.rule:
                print(finding.printed())
        return 0
    if args.report:
        print_report(counts)
        return 0
    old = None
    if baseline_path.is_file() or not args.write_baseline:
        try:
            old = read_baseline(baseline_path)
        except BaselineError as error:
            print(f"check_style: {error}", file=sys.stderr)
            return 2
    if args.write_baseline:
        return write_baseline(baseline_path, counts, old, args.accept_growth)
    return check(findings, counts, directories, old)


if __name__ == "__main__":
    sys.exit(main())
