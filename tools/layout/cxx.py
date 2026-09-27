# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

"""C and C++ source handling for the layout pass: lexing, namespaces, includes.

The pass rewrites namespace-qualified names, identifiers and #include lines
in place. Everything here works on text with offsets kept, so an edit
touches only the characters it replaces.
"""
import bisect
import re
from collections import defaultdict

IDENT = r"[A-Za-z_]\w*"
# A qualified name: an optional leading ::, then identifiers joined by ::.
CHAIN_RE = re.compile(r"(?<![\w:.$])(::)?(" + IDENT + r"(?:::~?" + IDENT + r")+)")
NAMESPACE_DECL_RE = re.compile(r"\b(?:inline\s+)?namespace\s+(" + IDENT + r"(?:::" + IDENT + r")*)\s*(?=\{)")
USING_NAMESPACE_RE = re.compile(r"\busing\s+namespace\s+(::)?(" + IDENT + r")\s*;")
USING_ANY_RE = re.compile(r"\busing\s+namespace\s+(::)?(" + IDENT + r"(?:::" + IDENT + r")*)\s*;")
ALIAS_DECL_RE = re.compile(r"\bnamespace\s+(" + IDENT + r")\s*=\s*(::)?(" + IDENT + r"(?:::" + IDENT + r")*)\s*;")
ALIAS_RE = re.compile(r"\bnamespace\s+" + IDENT + r"\s*=\s*(::)?(" + IDENT + r")\s*;")
INCLUDE_RE = re.compile(r'^([ \t]*#[ \t]*include[ \t]*)(["<])([^">\n]+)([">])', re.M)
WORD_RE = re.compile(r"\b" + IDENT + r"\b")
TYPE_HEAD_RE = re.compile(r"\b(?:struct|class|union)\s+(?:alignas\s*\([^)]*\)\s*)?" + IDENT + r"\b[^()=;]*$")
NOT_NAMES = {"if", "for", "while", "switch", "return", "sizeof", "decltype", "alignas", "operator",
             "static_assert", "noexcept", "default", "delete"}


def segments(text):
    """Split source text into ('code'|'comment'|'string', start, end) runs.

    String and character literals (raw strings included) are 'string' runs;
    a quote after a digit or letter is a digit separator, not a literal,
    unless it follows an encoding prefix.
    """
    runs = []
    index = 0
    length = len(text)
    code_start = 0

    def flush(end):
        if end > code_start:
            runs.append(("code", code_start, end))

    while index < length:
        char = text[index]
        if char == "/" and text.startswith("//", index):
            flush(index)
            end = text.find("\n", index)
            end = length if end < 0 else end
            runs.append(("comment", index, end))
            index = code_start = end
        elif char == "/" and text.startswith("/*", index):
            flush(index)
            end = text.find("*/", index + 2)
            end = length if end < 0 else end + 2
            runs.append(("comment", index, end))
            index = code_start = end
        elif char == '"' and re.search(r"(?:^|[^\w])(?:u8|u|U|L)?R$", text[max(0, index - 4):index]):
            open_paren = text.find("(", index + 1)
            if open_paren < 0:
                index += 1
                continue
            closing = ")" + text[index + 1:open_paren] + '"'
            end = text.find(closing, open_paren)
            end = length if end < 0 else end + len(closing)
            flush(index)
            runs.append(("string", index, end))
            index = code_start = end
        elif char in "\"'":
            if char == "'" and index > 0 and (text[index - 1].isalnum() or text[index - 1] == "_") and \
                    not re.search(r"(?:^|[^\w])(?:u8|u|U|L)$", text[max(0, index - 3):index]):
                index += 1
                continue
            flush(index)
            end = index + 1
            while end < length and text[end] != char and text[end] != "\n":
                end += 2 if text[end] == "\\" else 1
            end = min(end + 1, length)
            runs.append(("string", index, end))
            index = code_start = end
        else:
            index += 1
    flush(length)
    return runs


def flatten(text, runs):
    """Return the code alone: comments, literals and preprocessor lines blanked, offsets kept."""
    chars = list(text)
    for kind, start, end in runs:
        if kind != "code":
            for position in range(start, end):
                if chars[position] != "\n":
                    chars[position] = " "
    flat = "".join(chars)
    return re.sub(r"^[ \t]*#(?:[^\n]*\\\n)*[^\n]*", lambda m: re.sub(r"[^\n]", " ", m.group(0)), flat, flags=re.M)


def resolve_scope(first, current, registry, usings=()):
    """Find the namespace in which a relative name's first component is found.

    Looks in the current namespace, then each enclosing one out to the
    global namespace, as lookup does for a name before ::, and then in the
    namespaces using-directives name. Returns the namespace the name was
    found in (a tuple), or None when first names no namespace.
    """
    scope = tuple(current)
    while True:
        if scope + (first,) in registry:
            return scope
        if not scope:
            break
        scope = scope[:-1]
    for nominated in usings:
        if tuple(nominated) + (first,) in registry:
            return tuple(nominated)
    return None


class Scopes:
    """What encloses every offset of one source text: its namespace and its block kind.

    A namespace is a tuple of components ('oa', 'sim'); an anonymous
    namespace keeps its parent's name. A block is 'namespace', 'type' (a
    struct, class or union body) or 'other' (a function, an enum, extern
    "C", a plain block). The using-directives and namespace aliases of the
    text are kept with where they were written.
    """

    def __init__(self, text, runs=None):
        runs = runs if runs is not None else segments(text)
        flat = flatten(text, runs)
        self.flat = flat
        self.positions = [0]
        self.stacks = [()]
        self.kinds = ["namespace"]
        self.declared = []  # (full name, written chain)
        self.anonymous = []  # offsets just inside an anonymous namespace's brace
        stack = [((), "namespace")]
        head_start = 0
        for match in re.finditer(r"[{};]", flat):
            char, at = match.group(0), match.start()
            if char == ";":
                head_start = at + 1
                continue
            if char == "}":
                if len(stack) > 1:
                    stack.pop()
                head_start = at + 1
                self.positions.append(at + 1)
                self.stacks.append(stack[-1][0])
                self.kinds.append(stack[-1][1])
                continue
            head = flat[head_start:at]
            head_start = at + 1
            current = stack[-1][0]
            declaration = re.search(r"\bnamespace\s*(" + IDENT + r"(?:\s*::\s*" + IDENT + r")*)?\s*$", head)
            if declaration:
                written = declaration.group(1)
                if written:
                    full = current + tuple(part.strip() for part in written.split("::"))
                    self.declared.append((full, written))
                    stack.append((full, "namespace"))
                else:
                    self.anonymous.append(at + 1)
                    stack.append((current, "namespace"))
            elif TYPE_HEAD_RE.search(head) and not re.search(r"\benum\b", head):
                stack.append((current, "type"))
            else:
                stack.append((current, "other"))
            self.positions.append(at + 1)
            self.stacks.append(stack[-1][0])
            self.kinds.append(stack[-1][1])
        # (offset, namespace written in, chain, leading ::)
        self.usings = [(match.start(), self.at(match.start()), tuple(match.group(2).split("::")), bool(match.group(1)))
                       for match in USING_ANY_RE.finditer(flat)]
        # (offset, namespace written in, alias, chain, leading ::)
        self.aliases = [(match.start(), self.at(match.start()), match.group(1), tuple(match.group(3).split("::")),
                         bool(match.group(2))) for match in ALIAS_DECL_RE.finditer(flat)]

    def at(self, offset):
        """Return the innermost enclosing namespace at an offset."""
        return self.stacks[bisect.bisect_right(self.positions, offset) - 1]

    def kind_at(self, offset):
        """Return the kind of the innermost block at an offset."""
        return self.kinds[bisect.bisect_right(self.positions, offset) - 1]


class Usings:
    """Where using-directives make namespaces visible, across the files of the trees.

    in_namespace maps a namespace to the namespaces directives written inside
    it nominate (visible wherever that namespace is current or nominated);
    a file's own directives at global scope, and those of the local headers
    it includes, are passed per file.
    """

    def __init__(self, in_namespace=None):
        self.in_namespace = in_namespace or {}

    def visible(self, scopes, offset, registry, extra=()):
        """Return the namespaces visible through using-directives at an offset of a text."""
        current = scopes.at(offset)
        found = []
        for position, enclosing, chain, leading in scopes.usings:
            if position >= offset or current[:len(enclosing)] != enclosing:
                continue
            scope = () if leading else resolve_scope(chain[0], enclosing, registry)
            if scope is not None and scope + chain in registry:
                found.append(scope + chain)
        found.extend(extra)
        for length in range(1, len(current) + 1):
            found.extend(self.in_namespace.get(current[:length], ()))
        # Directives inside a nominated namespace are followed too.
        index = 0
        while index < len(found):
            for nominated in self.in_namespace.get(tuple(found[index]), ()):
                if nominated not in found:
                    found.append(nominated)
            index += 1
        return [tuple(name) for name in dict.fromkeys(tuple(name) for name in found)]


def declared_namespaces(text):
    """Return every namespace a source text declares, as component tuples."""
    return {full for full, _ in Scopes(text).declared}


def using_directives(text, registry):
    """Return (namespace written in, nominated namespace) for each using-directive of a text."""
    scopes = Scopes(text)
    found = []
    for _, enclosing, chain, leading in scopes.usings:
        scope = () if leading else resolve_scope(chain[0], enclosing, registry)
        if scope is not None and scope + chain in registry:
            found.append((enclosing, scope + chain))
    return found


def namespace_members(text, namespace):
    """Return the names a source text declares directly in one namespace.

    Types, enums, aliases, nested namespaces, functions, variables and
    constants declared at that namespace's own level count; members of
    types, locals of functions, names in an anonymous namespace and
    out-of-line definitions of members (A::f) do not.
    """
    namespace = tuple(namespace)
    runs = segments(text)
    flat = flatten(text, runs)
    scopes = Scopes(text, runs)
    anonymous = set(scopes.anonymous)
    pieces = []
    stack = []
    head_start = 0
    for index, char in enumerate(flat):
        if char == "{":
            head = flat[head_start:index]
            opens_namespace = re.search(r"\bnamespace\s*(?:" + IDENT + r"(?:\s*::\s*" + IDENT + r")*)?\s*$",
                                        head) is not None
            inner = scopes.at(index + 1)
            direct = opens_namespace and inner == namespace and (index + 1) not in anonymous
            stack.append("ns" if direct else "other")
            if len(stack) >= 2 and stack[-2] == "ns" and stack[-1] == "other":
                pieces.append("{}")
            head_start = index + 1
        elif char == "}":
            if stack:
                stack.pop()
            head_start = index + 1
        else:
            if char == ";":
                head_start = index + 1
            if stack and stack[-1] == "ns":
                pieces.append(char)
    names = set()
    for statement in re.split(r";|\{\}", "".join(pieces)):
        statement = " ".join(statement.split())
        statement = re.sub(r"\[\[[^\]]*\]\]", " ", statement).strip()
        if not statement:
            continue
        type_match = re.search(r"\b(?:struct|class|union|enum(?:\s+class|\s+struct)?)\s+(?:alignas\s*\([^)]*\)\s*)?("
                               + IDENT + r")\s*(?::[^=()]*)?$", statement)
        if type_match:
            names.add(type_match.group(1))
            continue
        alias = re.search(r"\busing\s+(" + IDENT + r")\s*=", statement)
        if alias:
            names.add(alias.group(1))
            continue
        nested = re.search(r"\bnamespace\s+(" + IDENT + r")\s*$", statement)
        if nested:
            names.add(nested.group(1))
            continue
        if statement.startswith(("using ", "static_assert", "friend ")):
            continue
        # A function: the identifier right before the first parenthesis; a
        # variable or constant: the last identifier before =, [ or the end.
        # A qualified declarator (A::f) defines a member, not a name here.
        paren = statement.find("(")
        equals = statement.find("=")
        cut = paren if paren >= 0 and (equals < 0 or paren < equals) else \
            min([position for position in (equals, statement.find("[")) if position >= 0] or [len(statement)])
        name = re.search(r"(::\s*)?(" + IDENT + r")\s*$", statement[:cut].rstrip())
        if name and not name.group(1) and name.group(2) not in NOT_NAMES:
            names.add(name.group(2))
    return names


class NamespaceMap:
    """Old namespace names and where each goes.

    rules maps a namespace (tuple) to its new name (tuple), or to itself when
    it stays; foreign holds namespaces declared only outside the engine, which
    never move. The longest prefix of a name with a rule or in foreign
    decides, so a namespace that stays stops a renamed parent from carrying
    it along. splits maps a namespace to (new name, member names, scopes):
    those members, and declarations of the namespace in files under the
    scopes, go to the split's new name instead.
    """

    def __init__(self, rules, splits, registry, foreign):
        self.rules = rules
        self.splits = splits
        self.registry = registry
        self.foreign = foreign
        self.split_symbols = {}
        for old, (_, symbols, _) in splits.items():
            for symbol in symbols:
                self.split_symbols.setdefault(symbol, []).append(old)

    def lookup(self, components):
        """Return (prefix length, new prefix or None) for the longest prefix that decides."""
        for length in range(len(components), 0, -1):
            prefix = tuple(components[:length])
            if prefix in self.foreign:
                return length, None
            if prefix in self.rules:
                return length, self.rules[prefix]
        return 0, None

    def in_split_scope(self, prefix, path):
        """Whether a file lies under the scopes of a split namespace."""
        _, _, scopes = self.splits[prefix]
        return path is not None and any(path == scope or path.startswith(scope.rstrip("/") + "/")
                                        for scope in scopes)

    def map_name(self, components, path=None):
        """Return the new components of a fully qualified name, or the old ones."""
        components = tuple(components)
        length, rule = self.lookup(components)
        if rule is None:
            return components
        prefix, rest = components[:length], components[length:]
        split = self.splits.get(prefix)
        if split is not None:
            new, symbols, _ = split
            if rest and rest[0] in symbols:
                return new + rest
            if not rest and self.in_split_scope(prefix, path):
                return new + rest
        return rule + rest

    def new_registry(self):
        """Return every namespace as it is named after the pass, with all prefixes."""
        names = {self.map_name(name) for name in self.registry}
        names.update(new for new, _, _ in self.splits.values())
        for name in list(names):
            for length in range(1, len(name)):
                names.add(name[:length])
        return names


def rewrite_chains(text, path, names, new_registry, identifiers, markdown=False, usings=None, extra_usings=()):
    """Rewrite namespace-qualified names and renamed identifiers in one text.

    Code and comments are rewritten; string literals are not. In Markdown
    only fully qualified names (oa::...) are rewritten. A relative name
    stays relative when its new spelling still finds the right namespace
    from where it is written; otherwise it is written fully qualified. A
    name that moves out of a split namespace is qualified where code of the
    namespace it leaves used it unqualified or through an alias. usings
    (a Usings) and extra_usings (the namespaces the directives of included
    local headers make visible) take part in lookups. Returns the new text
    and the number of edits.
    """
    usings = usings or Usings()
    edits = []
    if markdown:
        spans = [("text", 0, len(text))]
        scopes = None
    else:
        spans = [run for run in segments(text) if run[0] != "string"]
        scopes = Scopes(text)

    def context(offset):
        current = scopes.at(offset)
        visible = usings.visible(scopes, offset, names.registry, extra_usings)
        current_new = names.map_name(current, path) if current else ()
        visible_new = [names.map_name(nominated, path) for nominated in visible]
        return current, visible, current_new, visible_new

    def spelling(new_full, scope_old, current_new, visible_new, absolute, offset=None):
        if absolute:
            return "::".join(new_full)
        scope_new = names.map_name(scope_old, path) if scope_old else ()
        candidates = []
        if new_full[:len(scope_new)] == scope_new and len(new_full) > len(scope_new):
            candidates.append(new_full[len(scope_new):])
        if new_full[0] == "oa" and len(new_full) > 1:
            candidates.append(new_full[1:])
        shadowing = alias_names(offset) if offset is not None and scopes else set()
        for relative in candidates:
            if relative[0] in shadowing:
                continue  # a namespace alias of that name is in view
            found = resolve_scope(relative[0], current_new, new_registry, visible_new)
            if found is not None and found + relative == new_full:
                return "::".join(relative)
        return "::".join(new_full)

    def moved_symbol(word, current, visible):
        """Return the new full name of an unqualified name leaving a split namespace, or None."""
        for prefix in names.split_symbols.get(word, ()):
            if names.in_split_scope(prefix, path):
                continue
            if current[:len(prefix)] == prefix or prefix in visible:
                new, _, _ = names.splits[prefix]
                return new + (word,)
        return None

    def alias_names(offset):
        """Return the names of the namespace aliases in view at an offset."""
        current = scopes.at(offset)
        return {alias for position, enclosing, alias, _, _ in scopes.aliases
                if position < offset and current[:len(enclosing)] == enclosing}

    # Namespace aliases: what each names today and after the pass. An alias
    # of a split namespace whose every use names a name the split moves
    # follows the split; any other follows the namespace's own row.
    aliases = []  # (position, enclosing, alias, old target, new target, right-hand side start, end)
    if scopes is not None:
        uses = defaultdict(list)
        for match in CHAIN_RE.finditer(scopes.flat):
            parts = match.group(2).split("::")
            uses[parts[0]].append((match.start(2), parts[1]))
        for match in ALIAS_DECL_RE.finditer(scopes.flat):
            position = match.start()
            enclosing = scopes.at(position)
            chain = tuple(match.group(3).split("::"))
            scope = () if match.group(2) else resolve_scope(chain[0], enclosing, names.registry)
            if scope is None:
                continue
            old_target = scope + chain
            new_target = names.map_name(old_target, path)
            split = names.splits.get(old_target)
            used = [name for offset, name in uses.get(match.group(1), ()) if offset > position]
            if split is not None and used and all(name in split[1] for name in used):
                new_target = split[0]
            aliases.append((position, enclosing, match.group(1), old_target, new_target, match.start(3),
                            match.end(3)))

    def alias_target(word, offset):
        """Return (old target, new target) of the alias a word names at an offset, or None."""
        current = scopes.at(offset)
        for position, enclosing, alias, old_target, new_target, _, _ in reversed(aliases):
            if alias == word and position < offset and current[:len(enclosing)] == enclosing:
                return old_target, new_target
        return None

    def add(start, end, replacement):
        if text[start:end] != replacement:
            edits.append((start, end, replacement))

    # The right-hand side of each alias declaration names its new target.
    alias_sides = []
    for _, _, _, old_target, new_target, side_start, side_end in aliases:
        alias_sides.append((side_start, side_end))
        if new_target != old_target:
            written = text[side_start:side_end]
            leading = text[max(0, side_start - 2):side_start] == "::"
            absolute = leading or written.split("::")[0] == "oa"
            _, visible, current_new, visible_new = context(side_start)
            scope_old = old_target[:len(old_target) - len(written.split("::"))]
            add(side_start, side_end, spelling(new_target, scope_old, current_new, visible_new, absolute, side_start))

    for kind, start, end in spans:
        chunk = text[start:end]
        taken = list(alias_sides)
        if not markdown and kind == "code":
            # `namespace a::b {` declares a namespace inside the current one.
            for match in NAMESPACE_DECL_RE.finditer(chunk):
                offset = start + match.start(1)
                written = match.group(1)
                taken.append((offset, offset + len(written)))
                current = scopes.at(offset)
                full = current + tuple(written.split("::"))
                new_full = names.map_name(full, path)
                if new_full == full:
                    continue
                current_new = names.map_name(current, path) if current else ()
                if new_full[:len(current_new)] != current_new or len(new_full) == len(current_new):
                    raise ValueError(f"{path}: namespace {'::'.join(full)} is declared inside "
                                     f"{'::'.join(current) or 'the global namespace'}, but moves to "
                                     f"{'::'.join(new_full)}, which that block cannot hold")
                add(offset, offset + len(written), "::".join(new_full[len(current_new):]))
        for match in CHAIN_RE.finditer(chunk):
            offset = start + match.start(2)
            if any(low <= offset < high for low, high in taken):
                continue
            leading = bool(match.group(1))
            written = match.group(2)
            parts = tuple(written.split("::"))
            if markdown:
                if leading or parts[0] != "oa":
                    continue
                current, visible, current_new, visible_new = (), [], (), []
            else:
                current, visible, current_new, visible_new = context(offset)
            # A namespace alias in view shadows a namespace of its name further
            # out (namespace ui = oa::ui; inside oa::app).
            aliased = None if (markdown or leading) else alias_target(parts[0], offset)
            if leading:
                if (parts[0],) not in names.registry:
                    continue
                scope_old = ()
            else:
                scope_old = None if aliased is not None else resolve_scope(parts[0], current, names.registry, visible)
            if scope_old is None and not markdown:
                if aliased is not None:
                    # Through an alias: only a name that leaves the namespace
                    # the alias names after the pass needs a new spelling.
                    old_target, new_target = aliased
                    full = old_target + parts[1:]
                    new_full = names.map_name(full, path)
                    if new_full != new_target + parts[1:]:
                        taken.append((offset, offset + len(written)))
                        add(offset, offset + len(written),
                            spelling(new_full, ("oa",), current_new, visible_new, False, offset))
                    continue
                moved = moved_symbol(parts[0], current, visible)
                if moved is not None:
                    # A name leaving a split namespace heads the chain
                    # (SmackerReader::open inside oa::intro_media).
                    taken.append((offset, offset + len(parts[0])))
                    add(offset, offset + len(parts[0]), spelling(moved, ("oa",), current_new, visible_new, False, offset))
                continue
            if scope_old is None:
                continue
            taken.append((offset, offset + len(written)))
            full = scope_old + parts
            new_full = names.map_name(full, path)
            if new_full == full:
                continue
            absolute = leading or (scope_old == () and parts[0] == "oa")
            add(offset, offset + len(written), spelling(new_full, scope_old, current_new, visible_new, absolute, offset))
        if not markdown and kind == "code":
            for pattern in (USING_NAMESPACE_RE, ALIAS_RE):
                for match in pattern.finditer(chunk):
                    offset = start + match.start(2)
                    first = match.group(2)
                    if any(low <= offset < high for low, high in taken):
                        continue
                    current, visible, current_new, visible_new = context(offset)
                    scope_old = () if match.group(1) else resolve_scope(first, current, names.registry, visible)
                    if scope_old is None:
                        continue
                    taken.append((offset, offset + len(first)))
                    full = scope_old + (first,)
                    new_full = names.map_name(full, path)
                    if new_full == full:
                        continue
                    add(offset, offset + len(first),
                        spelling(new_full, scope_old, current_new, visible_new, bool(match.group(1)), offset))
            # Unqualified uses of names that leave a split namespace, outside
            # type bodies (where such a word declares a member).
            if names.split_symbols:
                for match in WORD_RE.finditer(chunk):
                    word = match.group(0)
                    offset = start + match.start()
                    if word not in names.split_symbols or any(low <= offset < high for low, high in taken):
                        continue
                    before = text[max(0, offset - 8):offset].rstrip()
                    if before.endswith(("::", ".", "->", "~")) or scopes.kind_at(offset) == "type":
                        continue
                    current, visible, current_new, visible_new = context(offset)
                    moved = moved_symbol(word, current, visible)
                    if moved is not None:
                        add(offset, offset + len(word), spelling(moved, ("oa",), current_new, visible_new, False, offset))
        if identifiers:
            for match in WORD_RE.finditer(chunk):
                new = identifiers.get(match.group(0))
                if new is not None:
                    edits.append((start + match.start(), start + match.end(), new))
    return apply_edits(text, edits, path)


def apply_edits(text, edits, path=""):
    """Apply (start, end, replacement) edits and return (text, count).

    An identifier edit that falls inside a rewritten chain is applied to
    that chain's new text by word.
    """
    if not edits:
        return text, 0
    edits.sort(key=lambda edit: (edit[0], -(edit[1] - edit[0])))
    merged = []
    for start, end, replacement in edits:
        if merged and start < merged[-1][1]:
            outer_start, outer_end, outer_text = merged[-1]
            if end <= outer_end:
                old_word = text[start:end]
                merged[-1] = (outer_start, outer_end,
                              re.sub(r"\b" + re.escape(old_word) + r"\b", replacement, outer_text))
                continue
            raise ValueError(f"{path}: overlapping edits at offset {start}")
        merged.append((start, end, replacement))
    pieces = []
    last = 0
    for start, end, replacement in merged:
        pieces.append(text[last:start])
        pieces.append(replacement)
        last = end
    pieces.append(text[last:])
    return "".join(pieces), len(merged)


def includes(text):
    """Yield (start, end, delimiter, name) for each #include of a quoted or bracketed name.

    start and end bound the name itself.
    """
    for match in INCLUDE_RE.finditer(text):
        yield match.start(3), match.end(3), match.group(2), match.group(3)
