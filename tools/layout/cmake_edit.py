# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

"""CMake file handling for the layout pass: commands, standalone blocks, reach-ins.

The editing is textual: a command is found with its exact span, and an
edit replaces whole lines, so the rest of a file keeps its formatting.
"""
import posixpath
import re

COMMAND_RE = re.compile(r"(?m)^([ \t]*)([A-Za-z_][A-Za-z0-9_]*)[ \t]*\(")
KEYWORDS = {"PUBLIC", "PRIVATE", "INTERFACE"}
FLAGS = {"SYSTEM", "BEFORE", "AFTER"}


class Command:
    """One command invocation: its name, arguments and the lines it spans."""

    def __init__(self, text, indent, name, start, args_start, args_end, end):
        self.indent = indent
        self.name = name.lower()
        self.start = start  # start of the command's first line
        self.args_start = args_start
        self.args_end = args_end  # offset of the closing parenthesis
        self.end = end  # after the newline that ends the command's last line
        self.args_text = text[args_start:args_end]

    def arguments(self):
        """Return the command's arguments as written (quotes kept)."""
        return split_arguments(self.args_text)


def strip_comments(text):
    """Blank # comments outside quoted arguments, keeping offsets."""
    out = []
    index = 0
    quoted = False
    while index < len(text):
        char = text[index]
        if quoted:
            out.append(char)
            if char == "\\" and index + 1 < len(text):
                out.append(text[index + 1])
                index += 2
                continue
            if char == '"':
                quoted = False
        elif char == '"':
            quoted = True
            out.append(char)
        elif char == "#":
            end = text.find("\n", index)
            end = len(text) if end < 0 else end
            out.append(" " * (end - index))
            index = end
            continue
        else:
            out.append(char)
        index += 1
    return "".join(out)


def commands(text):
    """Return every command of a CMake file, in order."""
    code = strip_comments(text)
    found = []
    position = 0
    while True:
        match = COMMAND_RE.search(code, position)
        if not match:
            break
        depth = 0
        index = match.end() - 1
        quoted = False
        while index < len(code):
            char = code[index]
            if quoted:
                if char == "\\":
                    index += 2
                    continue
                if char == '"':
                    quoted = False
            elif char == '"':
                quoted = True
            elif char == "(":
                depth += 1
            elif char == ")":
                depth -= 1
                if depth == 0:
                    break
            index += 1
        end = code.find("\n", index)
        end = len(text) if end < 0 else end + 1
        found.append(Command(text, match.group(1), match.group(2), match.start(), match.end(), index, end))
        position = index + 1
    return found


def argument_spans(args_text):
    """Return the (start, end) offsets of each argument in an argument list, quotes and generator expressions kept whole."""
    spans = []
    index = 0
    text = strip_comments(args_text)
    while index < len(text):
        char = text[index]
        if char.isspace():
            index += 1
            continue
        start = index
        if char == '"':
            index += 1
            while index < len(text) and text[index] != '"':
                index += 2 if text[index] == "\\" else 1
            index += 1
        else:
            depth = 0
            while index < len(text):
                char = text[index]
                if char in "<(":
                    depth += 1 if (char == "(" or text[index - 1:index] == "$") else 0
                elif char in ">)" and depth:
                    depth -= 1
                elif char.isspace() and depth == 0:
                    break
                index += 1
        spans.append((start, min(index, len(text))))
    return spans


def split_arguments(args_text):
    """Split an argument list into its arguments, quotes and generator expressions kept whole."""
    text = strip_comments(args_text)
    return [text[start:end] for start, end in argument_spans(args_text)]


def without_arguments(text, command, drop):
    """Return the text of a command with the arguments at the given indexes removed, its line breaks kept.

    An argument that follows another on its line takes the blanks before it
    along; the first argument of a line takes the blanks after it; a line
    left blank goes, and a closing parenthesis left alone on its line joins
    the line before.
    """
    spans = argument_spans(command.args_text)
    body = text[command.start:command.end]
    base = command.args_start - command.start
    for index in sorted(drop, reverse=True):
        start, end = spans[index]
        start, end = start + base, end + base
        line_start = body.rfind("\n", 0, start) + 1
        if body[line_start:start].strip():
            while start > line_start and body[start - 1] in " \t":
                start -= 1
        else:
            while end < len(body) and body[end] in " \t":
                end += 1
        body = body[:start] + body[end:]
        line_end = body.find("\n", line_start)
        line_end = len(body) if line_end < 0 else line_end
        rest = body[line_start:line_end].strip()
        if not rest and line_end < len(body):
            body = body[:line_start] + body[line_end + 1:]
        elif rest.startswith(")") and line_start > 0:
            # The closing parenthesis joins the line before.
            previous_end = line_start - 1
            while previous_end > 0 and body[previous_end - 1] in " \t":
                previous_end -= 1
            body = body[:previous_end] + body[body.index(")", line_start):]
    return body


def unquote(argument):
    """Return an argument without its surrounding quotes."""
    if len(argument) >= 2 and argument[0] == '"' and argument[-1] == '"':
        return argument[1:-1]
    return argument


def if_blocks(found):
    """Return (if index, [else/elseif indexes], endif index) for every if block, innermost first."""
    blocks = []
    stack = []
    for index, command in enumerate(found):
        if command.name == "if":
            stack.append([index, []])
        elif command.name in ("elseif", "else") and stack:
            stack[-1][1].append(index)
        elif command.name == "endif" and stack:
            start, branches = stack.pop()
            blocks.append((start, branches, index))
    return blocks


STANDALONE_RE = re.compile(r"\bCMAKE_(?:CURRENT_)?SOURCE_DIR\s+STREQUAL\s+CMAKE_(?:CURRENT_)?SOURCE_DIR\b")
NOT_TARGET_RE = re.compile(r"^\s*NOT\s+TARGET\s+[\w:-]+\s*$")


def removable_in_standalone(command):
    """Whether a command only serves a module built on its own."""
    arguments = [unquote(argument) for argument in command.arguments()]
    if command.name == "add_subdirectory":
        return True
    if command.name == "add_library" and arguments[:2] == ["oa-options", "INTERFACE"] and len(arguments) == 2:
        return True
    if command.name == "target_compile_options" and arguments[:1] == ["oa-options"]:
        return True
    return False


def remove_standalone(text):
    """Remove the blocks that let a module build on its own.

    A block goes when its condition tests that the module is the top-level
    project (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR), or when it
    is `if(NOT TARGET x)` around nothing but add_subdirectory calls and the
    oa-options stand-in. Comment lines right above a removed block go with
    it when they speak of standalone builds. Returns (text, removed count).
    """
    removed = 0
    while True:
        found = commands(text)
        target = None
        for start, branches, end in if_blocks(found):
            condition = found[start].args_text
            body = found[start + 1:end]
            nested_ok = all(removable_in_standalone(command) or command.name in ("if", "endif")
                            for command in body)
            if branches:
                continue
            if STANDALONE_RE.search(condition) or (NOT_TARGET_RE.match(strip_comments(condition)) and nested_ok
                                                   and body):
                target = (found[start].start, found[end].end)
                break
        if target is None:
            return text, removed
        start, end = target
        # Comment lines just above that describe the standalone build.
        lines_before = text[:start].split("\n")
        comment_start = start
        index = len(lines_before) - 2
        comment_lines = []
        while index >= 0 and lines_before[index].strip().startswith("#"):
            comment_lines.insert(0, lines_before[index])
            index -= 1
        if comment_lines and any("tandalone" in line for line in comment_lines):
            comment_start = start - sum(len(line) + 1 for line in comment_lines)
        # A blank line on each side of the block becomes one.
        if text[:comment_start].endswith("\n\n") and text[end:].startswith("\n"):
            end += 1
        text = text[:comment_start] + text[end:]
        removed += 1


def resolve_item(item, cmake_dir, root_vars):
    """Resolve an include-directory argument to a path relative to the tree root, or None."""
    value = unquote(item)
    for variable in ("${CMAKE_CURRENT_SOURCE_DIR}", "${CMAKE_CURRENT_LIST_DIR}"):
        if value.startswith(variable):
            value = cmake_dir + value[len(variable):] if cmake_dir else value[len(variable) + 1:]
            return posixpath.normpath(value)
    for variable in root_vars:
        if value.startswith(variable + "/"):
            return posixpath.normpath(value[len(variable) + 1:])
    if "$" in value or value.startswith("/"):
        return None
    return posixpath.normpath(posixpath.join(cmake_dir, value)) if cmake_dir else posixpath.normpath(value)


def outside(path, cmake_dir):
    """Whether a resolved path lies outside the directory of a CMakeLists.txt (the root reaches into src/)."""
    if not cmake_dir:
        return path.startswith("src/")
    return not (path == cmake_dir or path.startswith(cmake_dir + "/"))


def replace_reach_ins(text, cmake_dir, owner_of, root_vars):
    """Turn include directories that reach into other modules into links to their owners.

    owner_of(path) returns the target that owns a resolved directory, or
    None. Each reach becomes `target_link_libraries(<target> <scope>
    <owner>)` right after the command, with the same scope. Returns (text,
    [(target, directory, owner)], [unowned directories]).
    """
    replaced = []
    unowned = []
    edits = []
    for command in commands(text):
        if command.name != "target_include_directories":
            continue
        arguments = command.arguments()
        if not arguments:
            continue
        target = arguments[0]
        kept = [target]
        scope = None
        links = {}
        changed = False
        for argument in arguments[1:]:
            bare = unquote(argument)
            if bare in KEYWORDS:
                scope = bare
                kept.append(argument)
                continue
            if bare in FLAGS:
                kept.append(argument)
                continue
            path = resolve_item(argument, cmake_dir, root_vars)
            if path is not None and outside(path, cmake_dir):
                owner = owner_of(path)
                if owner is None:
                    unowned.append(path)
                    kept.append(argument)
                    continue
                changed = True
                replaced.append((target, path, owner))
                if owner != target:
                    links.setdefault(scope or "PUBLIC", [])
                    if owner not in links[scope or "PUBLIC"]:
                        links[scope or "PUBLIC"].append(owner)
                continue
            kept.append(argument)
        if not changed:
            continue
        # Drop scope keywords left with nothing after them.
        cleaned = []
        for index, argument in enumerate(kept):
            if unquote(argument) in KEYWORDS and (index + 1 == len(kept) or unquote(kept[index + 1]) in KEYWORDS):
                continue
            cleaned.append(argument)
        lines = []
        if len(cleaned) > 1:
            lines.append(f"{command.indent}target_include_directories({' '.join(cleaned)})\n")
        for scope_name, owners in links.items():
            lines.append(f"{command.indent}target_link_libraries({target} {scope_name} {' '.join(owners)})\n")
        edits.append((command.start, command.end, "".join(lines)))
    for start, end, replacement in sorted(edits, reverse=True):
        text = text[:start] + replacement + text[end:]
    return text, replaced, unowned


def with_comments_above(text, start):
    """Return where the comment lines right above an offset begin (the offset itself when there are none)."""
    position = start
    while position > 0:
        line_start = text.rfind("\n", 0, position - 1) + 1
        line = text[line_start:position - 1] if text[position - 1:position] == "\n" else ""
        if not line.strip().startswith("#"):
            break
        position = line_start
    return position


SOURCE_COMMANDS = {"add_library", "add_executable", "target_sources"}
TARGET_FLAGS = {"STATIC", "SHARED", "MODULE", "OBJECT", "INTERFACE", "EXCLUDE_FROM_ALL", "WIN32", "MACOSX_BUNDLE",
                "IMPORTED", "ALIAS", "GLOBAL", "UNKNOWN"}
# Commands whose first argument names the target they configure.
TARGET_COMMANDS = {"target_include_directories", "target_link_libraries", "target_compile_features",
                   "target_compile_options", "target_compile_definitions", "target_link_options",
                   "target_sources", "set_target_properties", "add_dependencies", "target_precompile_headers",
                   "add_custom_command"}


def drop_moved_sources(text, cmake_dir, moved_away, root_vars, moving=lambda target: False):
    """Remove the sources that leave this file's module, and the targets they leave empty.

    moved_away(path) says whether a source (a path from the tree root) moves
    to another module, whose own CMakeLists.txt builds it. A library or
    executable left with no sources goes with every command that configures,
    tests or lists it, unless moving(target) says another build file defines
    it after the pass, in which case the lists that name it keep it; if
    blocks and foreach loops left empty go too, and so do the comment lines
    right above anything removed. Returns (text, [sources removed], [targets
    removed]).
    """
    removed_sources = []
    emptied = set()
    edits = []
    for command in commands(text):
        if command.name not in SOURCE_COMMANDS:
            continue
        arguments = command.arguments()
        if not arguments:
            continue
        target = unquote(arguments[0])
        kept = [arguments[0]]
        sources = 0
        dropped = 0
        for argument in arguments[1:]:
            bare = unquote(argument)
            if bare in TARGET_FLAGS or bare in KEYWORDS:
                kept.append(argument)
                continue
            path = resolve_item(argument, cmake_dir, root_vars)
            if path is not None and moved_away(path):
                removed_sources.append(path)
                dropped += 1
                continue
            kept.append(argument)
            sources += 1
        if not dropped:
            continue
        flags = {unquote(argument) for argument in kept[1:]}
        if sources == 0 and not flags & {"INTERFACE", "IMPORTED", "ALIAS"}:
            if command.name != "target_sources":
                emptied.add(target)
            edits.append((with_comments_above(text, command.start), command.end, ""))
            continue
        cleaned = [argument for index, argument in enumerate(kept)
                   if not (unquote(argument) in KEYWORDS and
                           (index + 1 == len(kept) or unquote(kept[index + 1]) in KEYWORDS))]
        edits.append((command.start, command.end, f"{command.indent}{command.name}({' '.join(cleaned)})\n"))
    for start, end, replacement in sorted(edits, reverse=True):
        text = text[:start] + replacement + text[end:]
    if emptied:
        text = remove_target_commands(text, emptied, {target for target in emptied if moving(target)})
    return text, removed_sources, sorted(emptied)


def remove_target_commands(text, targets, moving=frozenset()):
    """Remove every command that configures, tests or lists one of the targets, then the empty blocks.

    A target in moving is built by another build file after the pass (under
    its new name): the commands here that configure or test it go, and so
    does its entry in a foreach loop that configures each item, but a loop
    that only reads the targets it lists (their files, whether they exist)
    keeps it, since the target still exists.
    """
    removed_tests = set()
    edits = []
    found = commands(text)
    for index, command in enumerate(found):
        arguments = [unquote(argument) for argument in command.arguments()]
        if not arguments:
            continue
        drop = False
        if command.name in TARGET_COMMANDS and arguments[0] in targets:
            drop = True
        elif command.name == "add_test":
            if "COMMAND" in arguments:
                runs = arguments[arguments.index("COMMAND") + 1:arguments.index("COMMAND") + 2]
                drop = bool(runs) and runs[0] in targets
            if drop and "NAME" in arguments:
                removed_tests.add(arguments[arguments.index("NAME") + 1])
        elif command.name == "oa_add_game_data_test" and len(arguments) > 1 and arguments[1] in targets:
            drop = True
            removed_tests.add(arguments[0])
        elif command.name == "foreach":
            configures = configures_items(found, index)
            gone = [position for position, argument in enumerate(arguments) if position > 0 and argument in targets
                    and (configures or argument not in moving)]
            if gone:
                edits.append((command.start, command.end, without_arguments(text, command, gone)))
            continue
        if drop:
            edits.append((with_comments_above(text, command.start), command.end, ""))
    for start, end, replacement in sorted(edits, reverse=True):
        text = text[:start] + replacement + text[end:]
    if removed_tests:
        edits = []
        for command in commands(text):
            arguments = [unquote(argument) for argument in command.arguments()]
            if command.name in ("set_tests_properties", "oa_game_data_tests") and arguments:
                cut = arguments.index("PROPERTIES") if "PROPERTIES" in arguments else len(arguments)
                names = arguments[:cut]
                if names and all(name in removed_tests for name in names):
                    edits.append((with_comments_above(text, command.start), command.end, ""))
        for start, end, replacement in sorted(edits, reverse=True):
            text = text[:start] + replacement + text[end:]
    return remove_empty_blocks(text)


def configures_items(found, index):
    """Whether the foreach loop at found[index] configures each item: a target command names ${<loop variable>}."""
    arguments = found[index].arguments()
    if not arguments:
        return False
    variable = "${" + unquote(arguments[0]) + "}"
    depth = 0
    for command in found[index + 1:]:
        if command.name == "foreach":
            depth += 1
        elif command.name == "endforeach":
            if depth == 0:
                return False
            depth -= 1
        elif command.name in TARGET_COMMANDS and command.arguments()[:1] == [variable]:
            return True
    return False


def remove_empty_blocks(text):
    """Remove if blocks and foreach loops that hold no command any more (comments inside go too)."""
    while True:
        found = commands(text)
        target = None
        openers = {"if": "endif", "foreach": "endforeach"}
        stack = []
        for index, command in enumerate(found):
            if command.name in openers:
                stack.append(index)
            elif command.name in ("endif", "endforeach") and stack:
                start = stack.pop()
                inner = [item for item in found[start + 1:index] if item.name not in ("else", "elseif")]
                if not inner:
                    target = (with_comments_above(text, found[start].start), found[index].end)
                    break
        if target is None:
            return text
        start, end = target
        if text[:start].endswith("\n\n") and text[end:].startswith("\n"):
            end += 1
        text = text[:start] + text[end:]


def defined_targets(text):
    """Return the libraries and executables a CMake file defines (aliases and imported targets aside)."""
    found = []
    for command in commands(text):
        if command.name not in ("add_library", "add_executable"):
            continue
        arguments = [unquote(argument) for argument in command.arguments()]
        if arguments and "ALIAS" not in arguments and "IMPORTED" not in arguments and "$" not in arguments[0]:
            found.append(arguments[0])
    return found


def relative_subdirectories(text):
    """Return the add_subdirectory calls whose source directory leaves the file's own directory."""
    found = []
    for command in commands(text):
        if command.name == "add_subdirectory":
            arguments = command.arguments()
            if arguments and (".." in unquote(arguments[0]).split("/")):
                found.append(unquote(arguments[0]))
    return found


def insert_aliases(text, aliases):
    """Add `add_library(<alias> ALIAS <target>)` after each target's add_library.

    aliases maps a target name to its alias; a target that already has one
    in this text is left alone. Returns (text, [targets given an alias]).
    """
    added = []
    edits = []
    existing = set()
    for command in commands(text):
        arguments = [unquote(argument) for argument in command.arguments()]
        if command.name == "add_library" and "ALIAS" in arguments:
            existing.add(arguments[-1])
    for command in commands(text):
        if command.name != "add_library":
            continue
        arguments = [unquote(argument) for argument in command.arguments()]
        if not arguments or "ALIAS" in arguments or "IMPORTED" in arguments:
            continue
        target = arguments[0]
        alias = aliases.get(target)
        if alias is None or target in existing:
            continue
        edits.append((command.end, f"{command.indent}add_library({alias} ALIAS {target})\n"))
        added.append(target)
    for position, line in sorted(edits, reverse=True):
        text = text[:position] + line + text[position:]
    return text, added


def normalized(command):
    """Return a command as its name and arguments, spacing and comments aside."""
    return (command.name, tuple(command.arguments()))


def block_depths(found):
    """Return the nesting depth of every command (if, foreach, while, function and macro blocks)."""
    depths = []
    depth = 0
    for command in found:
        if command.name in ("endif", "endforeach", "endwhile", "endfunction", "endmacro"):
            depth = max(0, depth - 1)
        depths.append(depth - (1 if command.name in ("else", "elseif") and depth else 0))
        if command.name in ("if", "foreach", "while", "function", "macro"):
            depth += 1
    return depths


def reindent(body, indent):
    """Return the lines of a block body moved out to an indent."""
    lines = body.split("\n")
    margins = [len(line) - len(line.lstrip(" \t")) for line in lines if line.strip()]
    margin = min(margins) if margins else 0
    return "\n".join(indent + line[margin:] if line.strip() else line for line in lines)


def collapse_same_branches(text):
    """Replace an if block whose two branches hold the same commands by those commands.

    Turning an include directory into a link can leave `if(TARGET x) link x
    else() link x endif()`; the condition no longer decides anything.
    Returns (text, blocks collapsed).
    """
    collapsed = 0
    while True:
        found = commands(text)
        target = None
        for start, branches, end in if_blocks(found):
            if len(branches) != 1 or found[branches[0]].name != "else":
                continue
            middle = branches[0]
            first = [normalized(command) for command in found[start + 1:middle]]
            second = [normalized(command) for command in found[middle + 1:end]]
            if first and first == second:
                body = text[found[start].end:found[middle].start]
                target = (found[start].start, found[end].end, reindent(body, found[start].indent))
                break
        if target is None:
            return text, collapsed
        start, end, body = target
        text = text[:start] + body + text[end:]
        collapsed += 1


def drop_redundant_guards(text):
    """Remove `if(TARGET x)` blocks whose commands the file also runs unconditionally.

    A link that replaced an include directory is made whatever targets
    exist, so a guard that made the same link only when its target existed
    says nothing any more. Returns (text, blocks removed).
    """
    removed = 0
    while True:
        found = commands(text)
        depths = block_depths(found)
        unconditional = {normalized(command) for command, depth in zip(found, depths) if depth == 0}
        target = None
        for start, branches, end in if_blocks(found):
            if branches or not re.match(r"^\s*TARGET\s+\S+\s*$", strip_comments(found[start].args_text)):
                continue
            body = found[start + 1:end]
            if body and all(command.name == "target_link_libraries" and normalized(command) in unconditional
                            for command in body):
                target = (found[start].start, found[end].end)
                break
        if target is None:
            return text, removed
        start, end = target
        if text[:start].endswith("\n\n") and text[end:].startswith("\n"):
            end += 1
        text = text[:start] + text[end:]
        removed += 1


def rename_project(text, name):
    """Rename the project() a module's CMakeLists.txt declares; return (text, renamed count)."""
    for command in commands(text):
        if command.name != "project":
            continue
        spans = argument_spans(command.args_text)
        if not spans:
            break
        start, end = spans[0]
        start, end = command.args_start + start, command.args_start + end
        if text[start:end] == name or not text[start:end].startswith("oa_"):
            break
        return text[:start] + name + text[end:], 1
    return text, 0
