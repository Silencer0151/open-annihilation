#!/usr/bin/env python3
# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

"""Check that engine code calls the extension tables' hooks only through call_hook.

The hooks are the function pointers of oa::app::Extension
(src/app/include/oa/app/extension.hpp), of the ReplayHooks an extension
fills there, and of the command line's SwitchHandler an extension returns
(src/app/command-line/include/oa/app/command_line.hpp). Engine code calls
them through call_hook (src/app/include/oa/app/hook_call.hpp), which catches
what a hook throws, or through the two calls built on it, which handle the
error as the hook's documentation says. This check fails when:

  - a hook has no entry in hook_call.hpp (OA_HOOK_ENTRY), or an entry names
    no hook;
  - the documentation of a hook of Extension or ReplayHooks says it must
    not throw ("must not throw") and its entry is not must_not_throw, or the
    other way round;
  - engine code calls a hook pointer itself: a call such as
    `table.hook(table.context, ...)` or `(table.*hook)(...)` in a source
    under src/, comments and string literals left out, where `table` is a
    variable, parameter or member declared with the hook's table as its type
    anywhere under src/, or an `auto` one initialised from such a name.

Four kinds of source call hooks themselves and are not read: hook_call.hpp,
which holds call_hook; src/app/extension_list.cpp, the combined table,
which calls each extension's own hooks; the command line's parse
(src/app/command-line/src/command_line.cpp), which calls the SwitchHandler
it is given and is given only startup.cpp's guard, whose entries call the
extension's handler through call_hook; and the sources of network play's
extension (src/app/netgame), which call their own functions, as any
extension may. Tests (*_test.cpp) call hooks to test them and are not read
either. The names are matched as written, so a table reached through a
name of another type, or a hook pointer copied out of its table, is not
seen. Exit status is 1 when the check fails and 2 when a file cannot be
read. --self-test checks a small built-in tree.
"""
import argparse
import re
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

# Where each table is declared, by the name hook_call.hpp's entries give it.
TABLE_HEADERS = {
    "Extension": Path("src/app/include/oa/app/extension.hpp"),
    "ReplayHooks": Path("src/app/include/oa/app/extension.hpp"),
    "command_line::SwitchHandler": Path("src/app/command-line/include/oa/app/command_line.hpp"),
}
# The tables whose hooks' documentation says which must not throw.
DOCUMENTED_TABLES = ("Extension", "ReplayHooks")
INVOKER_HEADER = Path("src/app/include/oa/app/hook_call.hpp")
# Sources that call hooks themselves (see the module documentation).
EXEMPT = (
    INVOKER_HEADER.as_posix(),
    "src/app/extension_list.cpp",
    "src/app/command-line/src/command_line.cpp",
)
EXEMPT_DIRECTORIES = ("src/app/netgame/",)
SOURCE_SUFFIXES = (".c", ".cc", ".cpp", ".h", ".hpp", ".inc", ".mm")

# A hook's declaration: a named pointer to a function whose parameters may
# hold one more (check_console's), default-initialised.
HOOK_RE = re.compile(r"\(\*(\w+)\)\s*\((?:[^()]|\((?:[^()]|\([^()]*\))*\))*\)\s*\{\}\s*;")
ENTRY_RE = re.compile(r"^OA_HOOK_ENTRY\(\s*([\w:]+)\s*,\s*(\w+)\s*,\s*(\w+)\s*\)\s*;", re.M)
# A name of an expression: identifiers joined by . or ->.
NAME = r"[A-Za-z_]\w*(?:\s*(?:\.|->)\s*[A-Za-z_]\w*)*"
# A call through a member pointer: (table.*hook)(...).
MEMBER_POINTER_CALL_RE = re.compile(r"\(\s*(" + NAME + r")\s*(?:\.\*|->\*)\s*\w+\s*\)\s*\(")


def strip_comments_and_strings(text):
    """Blanks comments and string and character literals, keeping line breaks."""
    out = []
    i = 0
    n = len(text)
    while i < n:
        c = text[i]
        if text.startswith("//", i):
            end = text.find("\n", i)
            end = n if end < 0 else end
            out.append(" " * (end - i))
            i = end
        elif text.startswith("/*", i):
            end = text.find("*/", i + 2)
            end = n if end < 0 else end + 2
            out.append("".join(ch if ch == "\n" else " " for ch in text[i:end]))
            i = end
        elif c == '"' or c == "'":
            j = i + 1
            while j < n and text[j] != c:
                j += 2 if text[j] == "\\" else 1
            j = min(j + 1, n)
            out.append(c + "".join(ch if ch == "\n" else " " for ch in text[i + 1:j - 1]) + c)
            i = j
        else:
            out.append(c)
            i += 1
    return "".join(out)


def struct_body(text, name):
    """Returns the body of `struct <name> {...};` in a header, or None."""
    short = name.split("::")[-1]
    match = re.search(r"^\s*struct\s+" + re.escape(short) + r"\s*\{", text, re.M)
    if not match:
        return None
    depth = 0
    for index in range(match.end() - 1, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[match.end():index]
    return None


def table_hooks(root):
    """Returns {table: {hook: its documentation}} for every table."""
    tables = {}
    for table, header in TABLE_HEADERS.items():
        text = (root / header).read_text(encoding="utf-8")
        body = struct_body(text, table)
        if body is None:
            raise ValueError(f"{header}: struct {table} not found")
        hooks = {}
        last_end = 0
        for match in HOOK_RE.finditer(body):
            # The documentation is the comment block between the previous
            # declaration and this one.
            between = body[last_end:match.start()]
            documentation = " ".join(
                line.strip().lstrip("/").strip()
                for line in between.splitlines()
                if line.strip().startswith("//")
            )
            hooks[match.group(1)] = documentation
            last_end = match.end()
        tables[table] = hooks
    return tables


def hook_entries(root):
    """Returns {(table, hook): handling} from hook_call.hpp."""
    text = (root / INVOKER_HEADER).read_text(encoding="utf-8")
    return {(m.group(1), m.group(2)): m.group(3) for m in ENTRY_RE.finditer(text)}


def direct_call_pattern(hook_names):
    """Returns the pattern of a call of one of the hooks with its table's context."""
    names = "|".join(sorted(map(re.escape, hook_names)))
    return re.compile(
        "(" + NAME + r")\s*(?:\.|->)\s*(" + names + r")\s*\(\s*\1\s*(?:\.|->)\s*context\b"
    )


def last_name(expression):
    """Returns the last identifier of a name such as runtime.extension_."""
    return re.split(r"\s*(?:\.|->)\s*", expression.strip())[-1]


def table_names(texts):
    """Returns {table: the names declared with it as their type} across sources.

    A declaration names the table's type, with or without namespaces, const,
    & or *; an `auto` declaration initialised from such a name joins it.
    """
    names = {table: set() for table in TABLE_HEADERS}
    for table in TABLE_HEADERS:
        short = table.split("::")[-1]
        declared = re.compile(
            r"(?<![\w:])(?:[\w]+::)*" + short
            + r"\b\s*(?:const\b\s*)?[&*]*\s*(?:const\b\s*)?([A-Za-z_]\w*)\s*[;,)={\[]"
        )
        for text in texts:
            names[table].update(m.group(1) for m in declared.finditer(text))
    copied = re.compile(r"\bauto\s*[&*]*\s*([A-Za-z_]\w*)\s*=\s*(" + NAME + r")\s*;")
    grew = True
    while grew:
        grew = False
        for text in texts:
            for match in copied.finditer(text):
                for table in names:
                    if last_name(match.group(2)) in names[table] and match.group(1) not in names[table]:
                        names[table].add(match.group(1))
                        grew = True
    return names


def source_files(root):
    """Yields (relative path, path, whether it is read for calls) for each source under src/."""
    for path in sorted((root / "src").rglob("*")):
        if not path.is_file() or path.suffix not in SOURCE_SUFFIXES:
            continue
        relative = path.relative_to(root).as_posix()
        read = not (
            relative in EXEMPT
            or relative.startswith(EXEMPT_DIRECTORIES)
            or path.stem.endswith("_test")
        )
        yield relative, path, read


def check(root):
    """Returns the problems found under root, one line each."""
    problems = []
    tables = table_hooks(root)
    entries = hook_entries(root)
    for table, hooks in tables.items():
        for hook, documentation in hooks.items():
            handling = entries.get((table, hook))
            if handling is None:
                problems.append(f"{INVOKER_HEADER.as_posix()}: {table}::{hook} has no OA_HOOK_ENTRY")
                continue
            if table not in DOCUMENTED_TABLES:
                continue
            says_must_not = "must not throw" in documentation
            if says_must_not != (handling == "must_not_throw"):
                problems.append(
                    f"{INVOKER_HEADER.as_posix()}: {table}::{hook} is {handling}, but its documentation "
                    + ("says" if says_must_not else "does not say")
                    + " it must not throw"
                )
    for table, hook in sorted(entries):
        if hook not in tables.get(table, {}):
            problems.append(f"{INVOKER_HEADER.as_posix()}: the entry {table}::{hook} names no hook")
    sources = [
        (relative, strip_comments_and_strings(path.read_text(encoding="utf-8", errors="replace")), read)
        for relative, path, read in source_files(root)
    ]
    names = table_names([text for _, text, _ in sources])
    patterns = {table: direct_call_pattern(hooks) for table, hooks in tables.items() if hooks}
    all_names = set().union(*names.values())
    for relative, text, read in sources:
        if not read:
            continue
        found = []
        for table, pattern in patterns.items():
            found += [m for m in pattern.finditer(text) if last_name(m.group(1)) in names[table]]
        found += [
            m for m in MEMBER_POINTER_CALL_RE.finditer(text) if last_name(m.group(1)) in all_names
        ]
        for match in sorted(found, key=lambda m: m.start()):
            line = text.count("\n", 0, match.start()) + 1
            what = " ".join(match.group(0).split())
            problems.append(
                f"{relative}:{line}: calls a hook pointer itself ({what}); call it through "
                "call_hook, call_hook_or_raise or call_hook_or_report (hook_call.hpp)"
            )
    return problems


SELF_TEST_TREE = {
    "src/app/include/oa/app/extension.hpp": """
struct ReplayHooks {
    void* context{};
    /// Steps. It must not throw.
    bool (*step)(void* context){};
};
struct Extension {
    void* context{};
    /// Starts.
    void (*startup)(void* context, Runtime& runtime){};
    /// Sets the speed. It must not throw.
    void (*speed_changed)(void* context, Runtime& runtime, uint16_t speed){};
    /// Checks lines.
    void (*check_console)(
        void* context, Runtime& runtime, void (*enter_line)(void* user, const char* line), void* user
    ){};
};
""",
    "src/app/command-line/include/oa/app/command_line.hpp": """
struct SwitchHandler {
    void* context{};
    int (*take)(void* context, char letter, const SwitchArguments* arguments, uint32_t* effects){};
};
""",
    "src/app/include/oa/app/hook_call.hpp": """
OA_HOOK_ENTRY(Extension, startup, raise);
OA_HOOK_ENTRY(Extension, speed_changed, must_not_throw);
OA_HOOK_ENTRY(Extension, check_console, raise);
OA_HOOK_ENTRY(command_line::SwitchHandler, take, raise);
OA_HOOK_ENTRY(ReplayHooks, step, must_not_throw);
void call() { table.startup(table.context, runtime); }
""",
    "src/app/extension_list.cpp": "void f() { (e.table.*hook)(e.table.context); e.startup(e.context); }\n",
    "src/app/netgame/extension.cpp": "void f() { replay.step(replay.context); }\n",
    "src/app/runtime_test.cpp": "void f() { extension.startup(extension.context, r); }\n",
    "src/app/runtime_good.cpp": """
// extension_.startup(extension_.context, *this) in a comment
const char* text = "extension_.startup(extension_.context, *this)";
void f() {
    call_hook_or_raise<&Extension::startup>(extension_, *this);
    match_->state();
    probe.startup(context, runtime);
}
""",
    "src/app/include/oa/app/runtime.hpp": "class Runtime {\n    Extension extension_;\n};\n",
    "src/app/runtime_other.cpp": """
void f(const command_line::SwitchHandler* handler, const ReplayHooks& replay, Extension table) {
    const auto& extension = runtime.extension_;
    meteor.step(meteor.context);
    reader.close(reader.context);
    (missions.*handler_of)();
}
""",
    "src/app/runtime_bad.cpp": """
void f() {
    extension_.startup(extension_.context, *this);
    runtime.extension_.speed_changed(
        runtime.extension_.context, runtime, 3);
    handler->take(handler->context, 'x', nullptr, nullptr);
    if (!replay.step(replay.context)) return;
    (table.*hook)(table.context);
    extension.check_console(extension.context, runtime, enter, nullptr);
}
""",
}


def self_test():
    with tempfile.TemporaryDirectory() as scratch:
        root = Path(scratch)
        for relative, text in SELF_TEST_TREE.items():
            path = root / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text, encoding="utf-8")
        problems = check(root)
        lines = sorted(
            int(p.split(":")[1]) for p in problems if p.startswith("src/app/runtime_bad.cpp:")
        )
        expected_bad = [3, 4, 6, 7, 8, 9]
        failures = []
        if lines != expected_bad:
            failures.append(f"direct calls found on lines {lines}, expected {expected_bad}")
        others = [p for p in problems if not p.startswith("src/app/runtime_bad.cpp:")]
        if others:
            failures.append("unexpected problems: " + "; ".join(others))
        # A hook with no entry, an entry with no hook, and a handling that
        # disagrees with the documentation.
        header = root / INVOKER_HEADER
        header.write_text(
            SELF_TEST_TREE[INVOKER_HEADER.as_posix()]
            .replace("OA_HOOK_ENTRY(Extension, startup, raise);\n", "")
            .replace("speed_changed, must_not_throw", "speed_changed, report")
            + "OA_HOOK_ENTRY(Extension, frame, raise);\n",
            encoding="utf-8",
        )
        problems = [p for p in check(root) if p.startswith(INVOKER_HEADER.as_posix())]
        for needle in (
            "Extension::startup has no OA_HOOK_ENTRY",
            "Extension::speed_changed is report, but its documentation says it must not throw",
            "the entry Extension::frame names no hook",
        ):
            if not any(needle in p for p in problems):
                failures.append(f"missing problem: {needle}")
        if len(problems) != 3:
            failures.append(f"expected 3 entry problems, found {problems}")
    for failure in failures:
        print(f"self-test: {failure}", file=sys.stderr)
    if failures:
        return 1
    print("check_hook_calls self-test: ok")
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    parser.add_argument("--root", type=Path, default=ROOT, help="the engine's source tree")
    parser.add_argument("--self-test", action="store_true", help="check a small built-in tree")
    arguments = parser.parse_args(argv)
    if arguments.self_test:
        return self_test()
    try:
        problems = check(arguments.root)
    except (OSError, ValueError) as error:
        print(f"check_hook_calls: {error}", file=sys.stderr)
        return 2
    for problem in problems:
        print(problem, file=sys.stderr)
    if problems:
        return 1
    print("check_hook_calls: every hook is called through call_hook")
    return 0


if __name__ == "__main__":
    sys.exit(main())
