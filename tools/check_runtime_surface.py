#!/usr/bin/env python3
# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

"""Hold an extension's reach into Runtime's private part to a shrinking budget.

An extension reaches oa-game through the hooks of src/app/include/oa/app/extension.hpp.
Until that is its only way in, it may also name a header of Runtime members
in OA_RUNTIME_EXTENSION_MEMBERS, which runtime.hpp includes inside class
Runtime, and use Runtime's private names from those members and from
Runtime's friends. This check measures both and compares them with a
baseline that may only shrink:

  declarations  the member declarations of the members header: each member
                function, data member, nested type and friend it declares;
  names         the private names of Runtime the extension's sources use,
                each with its number of uses: every name declared only in a
                private or protected part of class Runtime in runtime.hpp
                that appears as an identifier in the given sources, in the
                local headers they include (by "..." relative to their own
                directory) or in the members header, other than the names
                the members header declares.

The members header must hold declarations only: a preprocessor directive
in it (an #include of more members, a macro that expands to several) could
add what the count does not see, so any directive there fails the check.
Comments, string and character literals and preprocessor lines are not
read, except that the body of a #define in the sources is read as code. An
identifier that only spells a private name (a local variable of the same
name, a field of another struct) counts too, so the uses are an upper
bound. The baseline holds each name with its uses, so a new name, or a new
use of a name that only such spellings reached before, fails even when
another use has gone.

The baseline (--baseline, by default tools/runtime-surface-baseline.json
next to this script) holds the allowed declaration count and the allowed
names with their uses. The check fails when the declarations exceed the
count, a name outside the list is used, or a name is used more often than
the baseline allows, and says when the baseline can be lowered.
--write-baseline records the current measure, but only when it lowers the
baseline; raising it is a reviewed edit of the file. --list prints the names
used and their uses. --baseline-names checks only that every name the
baseline lists is still a private name of Runtime, so that a change to
runtime.hpp that renames, removes or publishes one shows in the engine's
own tests: a rename replaces the old name in the baseline with the new one,
keeping its uses, in the same change (the one addition the baseline takes),
and a removal or a move to the public part drops it. Exit status is 1
beyond the baseline or for a directive in the members header, and 2 when a
file cannot be read.
--self-test checks a small built-in tree.
"""
import argparse
import collections
import contextlib
import io
import json
import re
import sys
import tempfile
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
DEFAULT_RUNTIME = TOOLS.parent / "src" / "app" / "include" / "oa" / "app" / "runtime.hpp"
DEFAULT_BASELINE = TOOLS / "runtime-surface-baseline.json"
BASELINE_VERSION = 2
# Bound on any file read; runtime.hpp is well below it.
MAX_SOURCE_BYTES = 8 * 1024 * 1024
# Bound on how deep local includes are followed.
MAX_INCLUDE_DEPTH = 16
RUNTIME_CLASS = "Runtime"

ACCESS_WORDS = {"public", "private", "protected"}
TYPE_WORDS = {"struct", "class", "union", "enum"}
# Words before a parenthesis that do not make a declaration a function.
NOT_CALL_WORDS = {"decltype", "alignas", "noexcept", "sizeof", "alignof", "__attribute__", "__declspec",
                  "explicit"}
# Words that lead a declaration without naming anything.
SPECIFIER_WORDS = {"static", "inline", "constexpr", "consteval", "constinit", "mutable", "virtual",
                   "explicit", "extern", "thread_local", "typename"}

TOKEN_RE = re.compile(
    r"""
    (?P<space>\s+)
  | (?P<line_comment>//[^\n]*)
  | (?P<block_comment>/\*.*?\*/)
  | (?P<raw_string>(?:u8|[uUL])?R"(?P<delimiter>[^()\\\s]{0,16})\(.*?\)(?P=delimiter)")
  | (?P<string>(?:u8|[uUL])?"(?:\\.|[^"\\\n])*")
  | (?P<number>\.?\d(?:[\w.']|[eEpP][+-])*)
  | (?P<char>(?:u8|[uUL])?'(?:\\.|[^'\\\n])+')
  | (?P<identifier>[A-Za-z_]\w*)
  | (?P<punct>::|->|.)
    """,
    re.VERBOSE | re.DOTALL,
)
DIRECTIVE_RE = re.compile(r"^[ \t]*#(?:[^\n\\]|\\.)*", re.MULTILINE | re.DOTALL)
# A #define directive: its name and parameters, then its body.
DEFINE_RE = re.compile(r"[ \t]*#[ \t]*define[ \t]+\w+(?:\([^)]*\))?(?P<body>.*)", re.DOTALL)
LOCAL_INCLUDE_RE = re.compile(r'^[ \t]*#[ \t]*include[ \t]*"([^"\n]+)"', re.MULTILINE)
CLOSING = {"(": ")", "[": "]", "{": "}"}


class SurfaceError(Exception):
    """A file that cannot be read or does not hold what the check needs."""


class SurfaceBreach(Exception):
    """A members header that holds something other than member declarations."""


class Group:
    """A bracketed run of tokens: its opening bracket and the tokens inside."""

    def __init__(self, bracket, items):
        self.bracket = bracket
        self.items = items


def read_bounded(path):
    """The text of path, refused above MAX_SOURCE_BYTES."""
    try:
        with open(path, "rb") as stream:
            data = stream.read(MAX_SOURCE_BYTES + 1)
    except OSError as error:
        raise SurfaceError(f"{path}: {error.strerror or error}") from error
    if len(data) > MAX_SOURCE_BYTES:
        raise SurfaceError(f"{path}: larger than {MAX_SOURCE_BYTES} bytes")
    return data.decode("utf-8", errors="replace")


def directive_replacement(directive, define_bodies):
    """What a preprocessor directive leaves in the text: its line breaks, after a #define's body when asked."""
    breaks = "\n" * directive.count("\n")
    define = DEFINE_RE.match(directive) if define_bodies else None
    if define is None:
        return breaks
    return define.group("body").replace("\\\n", " ") + breaks


def tokens(text, define_bodies=False):
    """The identifiers, numbers and punctuation of C++ text, without comments, literals or directives.

    With define_bodies, the body of each #define stays, to be read as code.
    """
    # Directives go first, so that a quoted include is not read as code.
    text = DIRECTIVE_RE.sub(lambda match: directive_replacement(match.group(0), define_bodies), text)
    result = []
    for match in TOKEN_RE.finditer(text):
        kind = match.lastgroup
        if kind in ("identifier", "punct", "number"):
            result.append(match.group(0))
        elif kind in ("string", "raw_string", "char"):
            result.append('""')
    return result


def group(items):
    """Nests the bracketed runs of a token list into Groups; unbalanced brackets are an error."""
    stack = [("", [])]
    for token in items:
        if token in CLOSING:
            stack.append((token, []))
        elif token in (")", "]", "}"):
            bracket, inner = stack.pop() if len(stack) > 1 else (None, None)
            if bracket is None or CLOSING[bracket] != token:
                raise SurfaceError(f"unbalanced '{token}'")
            stack[-1][1].append(Group(bracket, inner))
        else:
            stack[-1][1].append(token)
    if len(stack) != 1:
        raise SurfaceError(f"unclosed '{stack[-1][0]}'")
    return stack[0][1]


def is_identifier(item):
    """Whether a grouped item is an identifier or keyword."""
    return isinstance(item, str) and (item[0].isalpha() or item[0] == "_")


def split_members(items):
    """Splits a class body into (access, declaration) pairs, starting private.

    A declaration ends at ';' or, for a member function defined in the
    class, at its body. A nested type's body stays inside its declaration.
    """
    access = "private"
    declarations = []
    current = []
    index = 0
    while index < len(items):
        item = items[index]
        index += 1
        if not current and item in ACCESS_WORDS and index < len(items) and items[index] == ":":
            access = item
            index += 1
            continue
        if item == ";":
            if current:
                declarations.append((access, current))
            current = []
            continue
        current.append(item)
        if isinstance(item, Group) and item.bracket == "{" and is_function_body(current[:-1]):
            declarations.append((access, current))
            current = []
            if index < len(items) and items[index] == ";":
                index += 1
    if current:
        declarations.append((access, current))
    return declarations


def without_attributes(declaration):
    """The declaration without [[...]] attribute groups."""
    return [item for item in declaration
            if not (isinstance(item, Group) and item.bracket == "[" and len(item.items) == 1
                    and isinstance(item.items[0], Group) and item.items[0].bracket == "[")]


def top_level(declaration):
    """The items of a declaration with their template-argument depth, up to its initializer."""
    depth = 0
    previous = None
    result = []
    for item in declaration:
        if depth == 0 and (item == "=" or (isinstance(item, Group) and item.bracket == "{")):
            break
        if item == "<" and is_identifier(previous) and previous != "operator":
            depth += 1
        elif item == ">" and depth > 0:
            depth -= 1
        elif depth == 0:
            result.append(item)
        previous = item
    return result


def function_name(declaration):
    """The name a declaration declares as a function, or None when it declares no function.

    The empty string stands for a constructor, destructor or operator.
    """
    items = top_level(without_attributes(declaration))
    for position, item in enumerate(items):
        if not (isinstance(item, Group) and item.bracket == "("):
            continue
        before = items[position - 1] if position > 0 else None
        if not is_identifier(before) or before in NOT_CALL_WORDS:
            continue
        inner = item.items
        if inner and inner[0] in ("*", "&", "^"):
            return None  # a pointer to a function, which is data
        if "operator" in items[:position] or before == RUNTIME_CLASS:
            return ""
        return before
    return None


def is_function_body(head):
    """Whether a brace group after head is a function body rather than an initializer or type body."""
    head = without_attributes(head)
    if not head or head[0] in TYPE_WORDS or (head[0] == "friend" and len(head) > 1 and head[1] in TYPE_WORDS):
        return False
    if "=" in head:
        return False
    return function_name(head) is not None


def data_names(items):
    """The names the declarators of a data member declaration declare, in order."""
    names = []
    name = None
    depth = 0
    previous = None
    in_initializer = False
    ended = False
    for item in items:
        if not in_initializer and item == "<" and is_identifier(previous) and previous != "operator":
            depth += 1
        elif not in_initializer and item == ">" and depth > 0:
            depth -= 1
        elif depth > 0:
            pass
        elif item == ",":
            if name:
                names.append(name)
            name = None
            in_initializer = ended = False
        elif in_initializer or ended:
            pass
        elif item == "=" or (isinstance(item, Group) and item.bracket == "{"):
            in_initializer = True
        elif item == ":" or (isinstance(item, Group) and item.bracket == "["):
            ended = True
        elif isinstance(item, Group) and item.bracket == "(":
            # A pointer to a function or an array: void (*name)(...).
            inner = [part for part in item.items if is_identifier(part)]
            if inner:
                name = inner[-1]
            ended = True
        elif is_identifier(item):
            name = item
        previous = item
    if name:
        names.append(name)
    return names


def declared_names(declaration):
    """The names a member declaration declares; none for a friend, an assertion or a special member."""
    items = without_attributes(declaration)
    while items and items[0] in SPECIFIER_WORDS:
        items = items[1:]
    if not items or items[0] in ("friend", "static_assert"):
        return []
    if items[0] == "template":
        depth = 0
        for position, item in enumerate(items):
            if item == "<":
                depth += 1
            elif item == ">":
                depth -= 1
                if depth == 0:
                    return declared_names(items[position + 1:])
        return []
    if items[0] == "using":
        if "=" in items:
            return [items[1]] if len(items) > 1 and is_identifier(items[1]) else []
        names = [item for item in items[1:] if is_identifier(item)]
        return names[-1:]
    if items[0] in TYPE_WORDS:
        rest = items[1:]
        if rest and rest[0] in ("class", "struct"):
            rest = rest[1:]
        type_name = rest[0] if rest and is_identifier(rest[0]) else None
        after = rest[1:] if type_name else rest
        follower = after[0] if after else None
        if follower is None:
            return [type_name] if type_name else []
        if follower in (":", "final") or (isinstance(follower, Group) and follower.bracket == "{"):
            body = next((position for position, item in enumerate(after)
                         if isinstance(item, Group) and item.bracket == "{"), None)
            variables = data_names(after[body + 1:]) if body is not None else []
            return ([type_name] if type_name else []) + variables
    name = function_name(items)
    if name is not None:
        return [name] if name else []
    return data_names(items)


def class_body(items, name):
    """The body of the definition of class `name` among grouped items."""
    for position, item in enumerate(items):
        if item not in ("class", "struct") or position + 1 >= len(items) or items[position + 1] != name:
            continue
        for follower in items[position + 2:]:
            if isinstance(follower, Group) and follower.bracket == "{":
                return follower.items
            if follower in (";", "=", "(") or isinstance(follower, Group):
                break
    for item in items:
        if isinstance(item, Group) and item.bracket == "{":
            found = class_body(item.items, name)
            if found is not None:
                return found
    return None


def runtime_private_names(runtime_text):
    """The names class Runtime declares only in its private and protected parts."""
    body = class_body(group(tokens(runtime_text)), RUNTIME_CLASS)
    if body is None:
        raise SurfaceError(f"no definition of class {RUNTIME_CLASS}")
    public = set()
    hidden = set()
    for access, declaration in split_members(body):
        (public if access == "public" else hidden).update(declared_names(declaration))
    return hidden - public


def first_directive(text):
    """The line and keyword of the first preprocessor directive outside comments and literals, or None."""
    def blank(match):
        if match.lastgroup in ("line_comment", "block_comment", "raw_string", "string", "char"):
            return " " + "\n" * match.group(0).count("\n")
        return match.group(0)

    code = TOKEN_RE.sub(blank, text)
    found = DIRECTIVE_RE.search(code)
    if found is None:
        return None
    keyword = re.match(r"[ \t]*#[ \t]*(\w*)", found.group(0)).group(1)
    return code.count("\n", 0, found.start()) + 1, keyword


def members_header(text):
    """The declarations of a members header and the names they declare."""
    declarations = split_members(group(tokens(text)))
    names = {name for _, declaration in declarations for name in declared_names(declaration)}
    return len(declarations), names


def local_sources(paths, engine_header):
    """The given files and the local headers they include, each once, in a stable order.

    The engine's header is never among them, even where a local include reaches it.
    """
    seen = {}
    pending = [(Path(path).resolve(), 0) for path in paths]
    engine_header = Path(engine_header).resolve()
    while pending:
        path, depth = pending.pop(0)
        if path in seen or path == engine_header:
            continue
        text = read_bounded(path)
        seen[path] = text
        if depth >= MAX_INCLUDE_DEPTH:
            continue
        for include in LOCAL_INCLUDE_RE.findall(text):
            candidate = (path.parent / include).resolve()
            if candidate.is_file():
                pending.append((candidate, depth + 1))
    return seen


def measure(runtime_path, members_path, sources):
    """The declaration count of the members header and the uses of each private Runtime name, by name.

    Raises SurfaceBreach when the members header holds a preprocessor directive.
    """
    private = runtime_private_names(read_bounded(runtime_path))
    members_text = read_bounded(members_path)
    directive = first_directive(members_text)
    if directive is not None:
        line, keyword = directive
        raise SurfaceBreach(f"{members_path}:{line}: the members header holds a #{keyword} directive; it may "
                            "hold member declarations only, so that the check counts everything it adds")
    count, own = members_header(members_text)
    used = collections.Counter()
    files = local_sources(sources, runtime_path)
    files.setdefault(Path(members_path).resolve(), members_text)
    for text in files.values():
        used.update(runtime_identifiers(tokens(text, define_bodies=True), private - own))
    return count, dict(sorted(used.items()))


def runtime_identifiers(items, names):
    """The uses, by name, of the identifiers among items that are in names and can name a member of Runtime.

    A name qualified by anything other than Runtime (other::name) belongs to
    that namespace or class, not to Runtime.
    """
    found = collections.Counter()
    for position, item in enumerate(items):
        if item not in names:
            continue
        if position >= 2 and items[position - 1] == "::" and items[position - 2] != RUNTIME_CLASS:
            continue
        found[item] += 1
    return found


def read_baseline(path):
    """The declaration count and the uses of each name a baseline file allows."""
    try:
        baseline = json.loads(read_bounded(path))
    except json.JSONDecodeError as error:
        raise SurfaceError(f"{path}: {error}") from error
    if not isinstance(baseline, dict):
        raise SurfaceError(f"{path}: not a version {BASELINE_VERSION} runtime surface baseline")
    count = baseline.get("members_header_declarations")
    names = baseline.get("private_runtime_names")
    if (baseline.get("version") != BASELINE_VERSION or not isinstance(count, int) or isinstance(count, bool)
            or count < 0 or not isinstance(names, dict)
            or not all(isinstance(uses, int) and not isinstance(uses, bool) and uses > 0
                       for uses in names.values())):
        raise SurfaceError(f"{path}: not a version {BASELINE_VERSION} runtime surface baseline")
    return count, names


def write_baseline(path, count, uses):
    """Records a declaration count and the uses of each name as the baseline at path."""
    text = json.dumps({"version": BASELINE_VERSION, "members_header_declarations": count,
                       "private_runtime_names": dict(sorted(uses.items()))}, indent=2)
    Path(path).write_text(text + "\n", encoding="utf-8")


def check(count, uses, baseline_path, write):
    """Compares a measure with the baseline, or lowers the baseline to it; returns the exit status."""
    allowed_count, allowed_uses = read_baseline(baseline_path)
    new_names = [name for name in uses if name not in allowed_uses]
    grown = [name for name in uses if name in allowed_uses and uses[name] > allowed_uses[name]]
    fewer = sorted(name for name, allowed in allowed_uses.items() if uses.get(name, 0) < allowed)
    if count > allowed_count or new_names or grown:
        if count > allowed_count:
            print(f"check_runtime_surface: the members header has {count} declarations against a "
                  f"baseline of {allowed_count}")
        for name in new_names:
            print(f"check_runtime_surface: uses Runtime's private name {name}, which the baseline does not list")
        for name in grown:
            print(f"check_runtime_surface: uses Runtime's private name {name} {uses[name]} times against a "
                  f"baseline of {allowed_uses[name]}")
        print("check_runtime_surface: reach the engine through a hook or a declared header instead; "
              "an identifier that only spells a private name counts too, so rename it"
              + (" (--write-baseline only lowers the baseline)" if write else ""))
        return 1
    if write:
        if count == allowed_count and not fewer:
            print("check_runtime_surface: the baseline is already current")
        else:
            write_baseline(baseline_path, count, uses)
            print(f"check_runtime_surface: baseline lowered to {count} declarations and {sum(uses.values())} "
                  f"uses of {len(uses)} names")
        return 0
    if count < allowed_count:
        print(f"check_runtime_surface: the members header has {count} declarations against a "
              f"baseline of {allowed_count}; lower it")
    for name in fewer:
        if name in uses:
            print(f"check_runtime_surface: the baseline allows {allowed_uses[name]} uses of {name}, which has "
                  f"{uses[name]}; lower it")
        else:
            print(f"check_runtime_surface: the baseline lists {name}, which is no longer used; lower it")
    if count < allowed_count or fewer:
        print("check_runtime_surface: --write-baseline records the lower measure")
    print(f"check_runtime_surface: {count} member declarations and {sum(uses.values())} uses of {len(uses)} "
          "private Runtime names, within the baseline")
    return 0


def check_baseline_names(runtime_path, baseline_path):
    """Checks that every name the baseline lists is a private name of Runtime; returns the exit status."""
    private = runtime_private_names(read_bounded(runtime_path))
    _, allowed = read_baseline(baseline_path)
    stale = sorted(set(allowed) - private)
    for name in stale:
        print(f"check_runtime_surface: the baseline lists {name}, which Runtime no longer declares privately; "
              "when it was renamed, replace it with the new name, keeping its uses; when it was removed "
              "or made public, drop it")
    if stale:
        return 1
    print(f"check_runtime_surface: the baseline's {len(allowed)} names are private names of Runtime")
    return 0


# A small tree for --self-test: a Runtime with public, private and
# protected parts, a members header and a source that reaches both.
SELF_TEST_RUNTIME = """\
#pragma once
#include <memory>
namespace oa::app {
class Runtime; // declared before it is defined
class Runtime {
public:
    explicit Runtime(int seed);
    ~Runtime();
    [[nodiscard]] int run();
    int shared_ = 0;
    void tick(); // also declared privately below; public wins
    friend struct Helper;

private:
    struct Hidden { int inside_ = 0; };
    enum class Screen : unsigned char { menu, match };
    using Clock = long;
    static constexpr int kLimit = 3;
    int frame_ = 0, other_ = 1;
    std::unique_ptr<Hidden, void (*)(Hidden*) noexcept> hidden_{nullptr, nullptr};
    void (*callback_)(int) = nullptr;
    [[nodiscard]] bool ready() const { return frame_ > 0; }
    void tick(int steps);
    template <typename T> void visit(T& value);
    int grid_[4]{};
    unsigned flags_ : 3;
protected:
    const char* text_ = "public: void not_a_member();";
#ifdef OA_RUNTIME_EXTENSION_MEMBERS
#include OA_RUNTIME_EXTENSION_MEMBERS
#endif
};
} // namespace oa::app
"""
SELF_TEST_MEMBERS = """\
    // Members an extension adds.
    friend struct ExtensionFriend;
    struct State;
    static void destroy_state(State* state) noexcept;
    void step_extension();
    [[nodiscard]] bool extension_ready() const;
    std::unique_ptr<State, void (*)(State*) noexcept> state_{nullptr, destroy_state};
    Screen shown_ = Screen::menu; // a private nested type
"""
SELF_TEST_SOURCE = """\
#include "runtime.hpp"
#include "local.hpp"
namespace oa::app {
// frame_ in a comment is not a use, nor is "grid_" in a string.
void Runtime::step_extension() {
    if (ready() && frame_ < kLimit)
        tick(1);
    const char* name = "grid_";
    (void)name;
    shared_ = 1'000;
}
} // namespace oa::app
"""
SELF_TEST_LOCAL = """\
#pragma once
inline int local_frames(oa::app::Runtime& runtime) { return runtime.run(); } // callback_ in a comment
struct Helper { static int peek(oa::app::Runtime& runtime) { return runtime.other_; } };
#define OA_HIDDEN_OF(runtime) \\
    ((runtime).hidden_)
"""
# What the check must find in that tree: the members header's declarations,
# and the private names used with their uses (tick is public too, so it
# does not count; hidden_ is used in a macro's body).
SELF_TEST_COUNT = 7
SELF_TEST_USES = {"Screen": 2, "frame_": 1, "hidden_": 1, "kLimit": 1, "other_": 1, "ready": 1}
# Members headers that bring in or hide declarations, and one whose only
# directives are inside comments.
SELF_TEST_BREACHES = [
    SELF_TEST_MEMBERS + '#include "more_members.hpp"\n',
    "#define OA_MORE_MEMBERS int more_ = 0; int most_ = 0;\n" + SELF_TEST_MEMBERS + "    OA_MORE_MEMBERS\n",
    "  #  pragma once\n" + SELF_TEST_MEMBERS,
]
SELF_TEST_NOT_BREACH = (SELF_TEST_MEMBERS + "    // #include \"a comment.hpp\"\n"
                        + "    /*\n#include \"a block comment.hpp\"\n*/\n")
SELF_TEST_PRIVATE = {"Hidden", "Screen", "Clock", "kLimit", "frame_", "other_", "hidden_", "callback_",
                     "ready", "visit", "grid_", "flags_", "text_"}


def self_test():
    """Checks the measure and the baseline rules on SELF_TEST_*; returns the exit status."""
    failures = []
    private = runtime_private_names(SELF_TEST_RUNTIME)
    if private != SELF_TEST_PRIVATE:
        failures.append(f"private names: +{sorted(private - SELF_TEST_PRIVATE)} "
                        f"-{sorted(SELF_TEST_PRIVATE - private)}")
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        (root / "runtime.hpp").write_text(SELF_TEST_RUNTIME, encoding="utf-8")
        (root / "members.hpp").write_text(SELF_TEST_MEMBERS, encoding="utf-8")
        (root / "source.cpp").write_text(SELF_TEST_SOURCE, encoding="utf-8")
        (root / "local.hpp").write_text(SELF_TEST_LOCAL, encoding="utf-8")
        count, uses = measure(root / "runtime.hpp", root / "members.hpp", [root / "source.cpp"])
        if count != SELF_TEST_COUNT:
            failures.append(f"members header declarations: {count}, expected {SELF_TEST_COUNT}")
        if uses != SELF_TEST_USES:
            failures.append(f"names used: {uses}, expected {SELF_TEST_USES}")
        for number, members in enumerate(SELF_TEST_BREACHES, 1):
            (root / "breach.hpp").write_text(members, encoding="utf-8")
            try:
                measure(root / "runtime.hpp", root / "breach.hpp", [root / "source.cpp"])
                failures.append(f"members header with a directive ({number}) was accepted")
            except SurfaceBreach:
                pass
        (root / "breach.hpp").write_text(SELF_TEST_NOT_BREACH, encoding="utf-8")
        try:
            kept, _ = measure(root / "runtime.hpp", root / "breach.hpp", [root / "source.cpp"])
            if kept != SELF_TEST_COUNT:
                failures.append(f"members header with a commented '#': {kept} declarations, "
                                f"expected {SELF_TEST_COUNT}")
        except SurfaceBreach:
            failures.append("a directive inside a comment was read as one")
        baseline = root / "baseline.json"
        without_first = {name: allowed for name, allowed in SELF_TEST_USES.items() if name != "Screen"}
        with_gone = {**SELF_TEST_USES, "gone_": 1}
        more_screen = {**with_gone, "Screen": SELF_TEST_USES["Screen"] + 1}
        fewer_screen = {**SELF_TEST_USES, "Screen": SELF_TEST_USES["Screen"] - 1}
        cases = [
            # (allowed count, allowed uses, write, expected status, count and uses left in the file)
            (SELF_TEST_COUNT, SELF_TEST_USES, False, 0, None),
            (SELF_TEST_COUNT - 1, SELF_TEST_USES, False, 1, None),
            (SELF_TEST_COUNT, without_first, False, 1, None),
            (SELF_TEST_COUNT, {**without_first, "gone_": 1}, False, 1, None),
            (SELF_TEST_COUNT, fewer_screen, False, 1, None),
            (SELF_TEST_COUNT + 2, more_screen, False, 0, None),
            (SELF_TEST_COUNT + 2, more_screen, True, 0, (SELF_TEST_COUNT, SELF_TEST_USES)),
            (SELF_TEST_COUNT - 1, SELF_TEST_USES, True, 1, (SELF_TEST_COUNT - 1, SELF_TEST_USES)),
            (SELF_TEST_COUNT, fewer_screen, True, 1, (SELF_TEST_COUNT, fewer_screen)),
        ]
        for allowed_count, allowed_uses, write, expected, left in cases:
            write_baseline(baseline, allowed_count, allowed_uses)
            with contextlib.redirect_stdout(io.StringIO()):
                status = check(count, uses, baseline, write)
            if status != expected:
                failures.append(f"baseline {allowed_count} {allowed_uses} (write {write}): status {status}, "
                                f"expected {expected}")
            if left is not None:
                kept = read_baseline(baseline)
                if kept != left:
                    failures.append(f"baseline {allowed_count} {allowed_uses} (write {write}): left {kept}, "
                                    f"expected {left}")
        for allowed_uses, expected in ((SELF_TEST_USES, 0), ({**SELF_TEST_USES, "tick": 1}, 1),
                                       (with_gone, 1)):
            write_baseline(baseline, SELF_TEST_COUNT, allowed_uses)
            with contextlib.redirect_stdout(io.StringIO()):
                status = check_baseline_names(root / "runtime.hpp", baseline)
            if status != expected:
                failures.append(f"baseline names {sorted(allowed_uses)}: status {status}, expected {expected}")
    try:
        group(tokens("void f() { if (x) { ]"))
        failures.append("an unbalanced bracket was accepted")
    except SurfaceError:
        pass
    with tempfile.TemporaryDirectory() as directory:
        for text in ("[1]", "7", '"baseline"', "null", "{}",
                     '{"version": 2, "members_header_declarations": 1, "private_runtime_names": ["name"]}',
                     '{"version": 2, "members_header_declarations": 1, "private_runtime_names": {"name": 0}}',
                     '{"version": 1, "members_header_declarations": 1, "private_runtime_names": {"name": 1}}'):
            malformed = Path(directory) / "baseline.json"
            malformed.write_text(text, encoding="utf-8")
            try:
                read_baseline(malformed)
                failures.append(f"the baseline {text} was accepted")
            except SurfaceError:
                pass
    if failures:
        for failure in failures:
            print(f"check_runtime_surface: self-test: {failure}")
        return 1
    print("check_runtime_surface: self-test passed")
    return 0


def main(argv=None):
    """Runs the check, the self-test or a baseline update; returns the exit status."""
    parser = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    parser.add_argument("sources", nargs="*", type=Path, help="the extension's sources that reach Runtime")
    parser.add_argument("--members", type=Path, help="the header OA_RUNTIME_EXTENSION_MEMBERS names")
    parser.add_argument("--runtime", type=Path, default=DEFAULT_RUNTIME, help="runtime.hpp (default: the engine's)")
    parser.add_argument("--baseline", type=Path, default=DEFAULT_BASELINE,
                        help="allowed declarations and names (default: tools/runtime-surface-baseline.json)")
    parser.add_argument("--write-baseline", action="store_true", help="lower the baseline to the current measure")
    parser.add_argument("--list", action="store_true", help="print the private names used and their uses")
    parser.add_argument("--baseline-names", action="store_true",
                        help="only check that the baseline's names are private names of Runtime")
    parser.add_argument("--self-test", action="store_true", help="check a small built-in tree instead")
    args = parser.parse_args(argv)
    if args.self_test:
        return self_test()
    if args.members is None and not args.baseline_names:
        parser.error("--members is required")
    try:
        if args.baseline_names:
            return check_baseline_names(args.runtime, args.baseline)
        count, uses = measure(args.runtime, args.members, args.sources)
        if args.list:
            for name, used in uses.items():
                print(f"{name} {used}")
        return check(count, uses, args.baseline, args.write_baseline)
    except SurfaceBreach as breach:
        print(f"check_runtime_surface: {breach}")
        return 1
    except SurfaceError as error:
        print(f"check_runtime_surface: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
