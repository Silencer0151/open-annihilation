#!/usr/bin/env python3
# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

"""Read the strict YAML subset of mod profiles and of the OAMOD registry.

The same rules as the engine's reader (src/formats/oamod/include/oa/formats/oamod.hpp):
UTF-8 without a byte order mark, at most 256 KiB, one document whose top level
is a mapping, at most 16 levels of nesting; block and flow collections; plain,
single- and double-quoted scalars on one line; '#' comments; one optional
'---' before the document and '...' after it. Tags, anchors, aliases, merge
keys, directives, complex keys, block scalars, a second document, duplicate
keys and tabs in indentation are refused with the rule and the position.

Values come back as Python values: dict (keys str, or int for a plain integer
key), list, str, int, float, bool and None. A double-quoted string written
with a byte or Unicode escape (\\x, \\u, \\U) comes back as an EscapedStr, so
that a generator can keep those bytes escaped in what it writes.

loads(text) reads a document; loads_value(text) reads one value of any kind.
"""

import re

MAX_INPUT_BYTES = 256 * 1024
MAX_NESTING_DEPTH = 16
MAX_NODE_COUNT = 1 << 16
MAX_STRING_BYTES = 4096
MAX_INTEGER_MAGNITUDE = 1 << 53
MAX_SIGNIFICANT_DIGITS = 15
MAX_DECIMAL_EXPONENT = 300

NUMBER_RE = re.compile(r"^-?(0|[1-9][0-9]*)(\.[0-9]+)?([eE][-+]?[0-9]+)?$")
NULLS = {"", "~", "null", "Null", "NULL"}
TRUES = {"true", "True", "TRUE"}
FALSES = {"false", "False", "FALSE"}
ESCAPES = {"0": "\0", "a": "\a", "b": "\b", "t": "\t", "\t": "\t", "n": "\n", "v": "\v", "f": "\f",
           "r": "\r", "e": "\x1b", " ": " ", '"': '"', "/": "/", "\\": "\\", "N": "\x85",
           "_": "\xa0", "L": " ", "P": " "}
HEX_ESCAPES = {"x": 2, "u": 4, "U": 8}


class EscapedStr(str):
    """A double-quoted string written with byte or Unicode escapes."""


class OamodYamlError(Exception):
    """A text that breaks a rule of the subset."""

    def __init__(self, rule, line, column):
        self.rule, self.line, self.column = rule, line, column
        super().__init__(f"{line}:{column}: {rule}")


def number_value(text, leading_zeros=False):
    """The int or float a number's text stands for, None when it is not a number.

    Raises OamodYamlError's rule names (as ValueError arguments) for numbers
    beyond what a profile holds."""
    if leading_zeros:
        m = re.match(r"^-?[0-9]+(\.[0-9]+)?([eE][-+]?[0-9]+)?$", text)
    else:
        m = NUMBER_RE.match(text)
    if not m:
        return None
    integer = "." not in text and "e" not in text and "E" not in text
    mantissa, _, exponent = text.lower().partition("e")
    whole, _, fraction = mantissa.lstrip("-").partition(".")
    digits = (whole + fraction).lstrip("0").rstrip("0")
    if integer:
        value = int(text)
        if abs(value) > MAX_INTEGER_MAGNITUDE:
            raise ValueError("integer_too_large")
        return value
    if digits:
        if len(digits) > MAX_SIGNIFICANT_DIGITS:
            raise ValueError("too_many_digits")
        stripped = (whole + fraction).lstrip("0")
        point = len(whole + fraction) - len(stripped) if stripped else 0
        leading = len(whole) - point - 1 + (int(exponent) if exponent else 0)
        if abs(leading) > MAX_DECIMAL_EXPONENT:
            raise ValueError("number_out_of_range")
    return float(text)


class _Reader:
    def __init__(self, text):
        self.text = text
        self.offset = 0
        self.line = 1
        self.line_start = 0
        self.nodes = 0
        self.cached = None

    # ---------------------------------------------------------------- basics
    def peek(self, distance=0):
        i = self.offset + distance
        return self.text[i] if i < len(self.text) else ""

    def here(self):
        return self.line, self.offset - self.line_start + 1

    def column(self):
        return self.offset - self.line_start

    def fail(self, rule, position=None):
        line, column = position or self.here()
        raise OamodYamlError(rule, line, column)

    def fail_here(self, rule="unexpected_character"):
        self.fail("unexpected_end" if self.offset >= len(self.text) else rule)

    def count(self, position):
        if self.nodes >= MAX_NODE_COUNT:
            self.fail("too_many_nodes", position)
        self.nodes += 1

    @staticmethod
    def break_or_end(c):
        return c in ("\n", "\r", "")

    def blank_or_end(self, c):
        return c in (" ", "\t") or self.break_or_end(c)

    @staticmethod
    def flow_indicator(c):
        return c != "" and c in ",[]{}"

    def consume_break(self):
        if self.peek() == "\r":
            self.offset += 1
        self.offset += 1
        self.line += 1
        self.line_start = self.offset

    def skip_inline_space(self):
        while self.peek() in (" ", "\t") and self.peek() != "":
            self.offset += 1

    def at_comment(self):
        if self.peek() != "#":
            return False
        if self.offset == self.line_start:
            return True
        return self.text[self.offset - 1] in (" ", "\t")

    def skip_to_break(self):
        while not self.break_or_end(self.peek()):
            self.offset += 1

    def end_line(self):
        self.skip_inline_space()
        if self.at_comment():
            self.skip_to_break()
        if self.offset >= len(self.text):
            return
        if self.peek() not in ("\n", "\r"):
            self.fail("unexpected_character")
        self.consume_break()

    def line_info(self):
        """(end, marker, indent) of the next line holding content."""
        if self.cached and self.cached[0] == self.offset:
            return self.cached[1]
        while True:
            first_tab = None
            content = self.offset
            while content < len(self.text) and self.text[content] in " \t":
                if self.text[content] == "\t" and first_tab is None:
                    first_tab = content
                content += 1
            if content >= len(self.text):
                self.offset = content
                info = (True, False, 0)
                break
            c = self.text[content]
            if c in "\n\r":
                self.offset = content
                self.consume_break()
                continue
            if c == "#":
                self.offset = content
                self.skip_to_break()
                continue
            if first_tab is not None:
                self.offset = first_tab
                self.fail("tab_indentation")
            self.offset = content
            indent = self.column()
            info = (False, indent == 0 and self.is_marker(), indent)
            break
        self.cached = (self.offset, info)
        return info

    def is_marker(self):
        c = self.peek()
        return c in ("-", ".") and self.peek(1) == c and self.peek(2) == c and self.blank_or_end(self.peek(3))

    def at_sequence_entry(self):
        return self.peek() == "-" and self.blank_or_end(self.peek(1))

    # ---------------------------------------------------------------- document
    def read(self, mapping_only):
        for i, c in enumerate(self.text):
            if (ord(c) < 0x20 and c not in "\t\n\r") or c == "\x7f" or (
                    c == "\r" and (i + 1 >= len(self.text) or self.text[i + 1] != "\n")):
                line = self.text.count("\n", 0, i) + 1
                self.fail("control_character", (line, i - (self.text.rfind("\n", 0, i) + 1) + 1))
        started = False
        while True:
            end, marker, indent = self.line_info()
            if end:
                if mapping_only:
                    self.fail("not_mapping")
                return None
            if not marker:
                if self.peek() == "%" and indent == 0 and not started:
                    self.fail("directive")
                break
            if self.peek() == "." or started:
                self.fail("several_documents")
            started = True
            self.offset += 3
            self.end_line()
        flow_root = self.peek() in ("[", "{")
        position = self.here()
        root = self.block_node(indent, 1, 0)
        if mapping_only and not isinstance(root, dict):
            self.fail("not_mapping", position)
        end, marker, _ = self.line_info()
        if end:
            return root
        if not marker:
            self.fail("trailing_content" if flow_root else "bad_indentation")
        if self.peek() == "-":
            self.fail("several_documents")
        self.offset += 3
        self.end_line()
        end, _, _ = self.line_info()
        if not end:
            self.fail("several_documents")
        return root

    # ---------------------------------------------------------------- scalars
    def check_value_start(self, flow):
        c = self.peek()
        rules = {"&": "anchor", "*": "alias", "!": "tag"}
        if c in rules:
            self.fail(rules[c])
        if c in ("|", ">"):
            self.fail("unexpected_character" if flow else "block_scalar")
        if c == "?":
            if self.blank_or_end(self.peek(1)) or (flow and self.flow_indicator(self.peek(1))):
                self.fail("complex_key")
            return
        if c != "" and c in "%@`,]}#":
            self.fail("unexpected_character")
        if c in ("-", ":") and c != "":
            if self.blank_or_end(self.peek(1)) or (flow and self.flow_indicator(self.peek(1))):
                self.fail_here()

    def plain_text(self, flow):
        first = self.offset
        while True:
            c = self.peek()
            if self.break_or_end(c):
                break
            if c == ":" and (self.blank_or_end(self.peek(1)) or (flow and self.flow_indicator(self.peek(1)))):
                break
            if c == "#" and self.at_comment():
                break
            if flow and self.flow_indicator(c):
                break
            self.offset += 1
        return self.text[first:self.offset].rstrip(" \t")

    def quoted(self):
        start = self.here()
        quote = self.peek()
        self.offset += 1
        out = []
        escaped = False
        while True:
            c = self.peek()
            if self.break_or_end(c):
                self.fail_here("multi_line_scalar")
            if c == quote:
                self.offset += 1
                if quote == "'" and self.peek() == "'":
                    out.append("'")
                    self.offset += 1
                else:
                    break
            elif c == "\\" and quote == '"':
                escape = self.here()
                self.offset += 1
                kind = self.peek()
                if self.break_or_end(kind):
                    self.fail_here("multi_line_scalar")
                self.offset += 1
                if kind in ESCAPES:
                    out.append(ESCAPES[kind])
                elif kind in HEX_ESCAPES:
                    digits = self.text[self.offset:self.offset + HEX_ESCAPES[kind]]
                    if len(digits) < HEX_ESCAPES[kind] and self.offset + len(digits) >= len(self.text):
                        self.offset += len(digits)
                        self.fail_here()
                    if not re.fullmatch(r"[0-9A-Fa-f]+", digits or "-") or len(digits) != HEX_ESCAPES[kind]:
                        self.fail("bad_escape", escape)
                    code = int(digits, 16)
                    if code > 0x10FFFF or 0xD800 <= code <= 0xDFFF:
                        self.fail("bad_escape", escape)
                    out.append(chr(code))
                    self.offset += HEX_ESCAPES[kind]
                    escaped = True
                else:
                    self.fail("bad_escape", escape)
            else:
                out.append(c)
                self.offset += 1
            if len("".join(out).encode("utf-8")) > MAX_STRING_BYTES:
                self.fail("string_too_long", start)
        text = "".join(out)
        return EscapedStr(text) if escaped else text

    def scalar(self, flow):
        """(quoted, value or raw text, position)"""
        position = self.here()
        if self.peek() in ('"', "'"):
            return True, self.quoted(), position
        self.check_value_start(flow)
        raw = self.plain_text(flow)
        if not raw:
            self.fail_here()
        if len(raw.encode("utf-8")) > MAX_STRING_BYTES:
            self.fail("string_too_long", position)
        return False, raw, position

    def resolve(self, raw, position):
        if raw in NULLS:
            return None
        if raw in TRUES:
            return True
        if raw in FALSES:
            return False
        try:
            value = number_value(raw)
        except ValueError as e:
            self.fail(e.args[0], position)
        return raw if value is None else value

    def key(self, quoted, raw, position):
        if quoted:
            return raw
        if raw == "<<":
            self.fail("merge_key", position)
        value = self.resolve(raw, position)
        if isinstance(value, str):
            return value
        if isinstance(value, int) and not isinstance(value, bool):
            return value
        self.fail("key_not_string", position)

    def add_key(self, mapping, key, position):
        if key in mapping:
            self.fail("duplicate_key", position)

    def open_collection(self, depth, position):
        if depth > MAX_NESTING_DEPTH:
            self.fail("too_deep", position)
        self.count(position)

    # ---------------------------------------------------------------- block
    def block_node(self, indent, depth, flow_floor):
        position = self.here()
        if self.at_sequence_entry():
            return self.block_sequence(indent, depth)
        if self.peek() in ("[", "{"):
            value = self.flow_collection(depth, flow_floor)
            self.skip_inline_space()
            if self.peek() == ":" and self.blank_or_end(self.peek(1)):
                self.fail("complex_key", position)
            self.end_line()
            return value
        quoted, raw, position = self.scalar(False)
        self.skip_inline_space()
        if self.peek() == ":" and self.blank_or_end(self.peek(1)):
            return self.block_mapping(indent, depth, self.key(quoted, raw, position), position)
        self.count(position)
        value = raw if quoted else self.resolve(raw, position)
        self.end_line()
        return value

    def block_key(self):
        if self.at_sequence_entry():
            self.fail("bad_indentation")
        if self.peek() in ("[", "{"):
            self.fail("complex_key")
        quoted, raw, position = self.scalar(False)
        self.skip_inline_space()
        if self.peek() != ":" or not self.blank_or_end(self.peek(1)):
            self.fail_here()
        return self.key(quoted, raw, position), position

    def block_mapping(self, indent, depth, key, key_position):
        self.open_collection(depth, key_position)
        out = {}
        while True:
            self.add_key(out, key, key_position)
            self.offset += 1
            out[key] = self.mapping_value(indent, depth)
            end, marker, line_indent = self.line_info()
            if end or marker or line_indent < indent:
                return out
            if line_indent > indent:
                self.fail("bad_indentation")
            key, key_position = self.block_key()

    def mapping_value(self, indent, depth):
        self.skip_inline_space()
        if self.break_or_end(self.peek()) or self.at_comment():
            position = self.here()
            self.end_line()
            end, marker, line_indent = self.line_info()
            if not end and not marker:
                if line_indent > indent:
                    return self.block_node(line_indent, depth + 1, indent + 1)
                if line_indent == indent and self.at_sequence_entry():
                    return self.block_sequence(indent, depth + 1)
            self.count(position)
            return None
        return self.inline_value(depth + 1, indent + 1)

    def inline_value(self, depth, flow_floor):
        position = self.here()
        if self.at_sequence_entry():
            self.fail("unexpected_character")
        if self.peek() in ("[", "{"):
            value = self.flow_collection(depth, flow_floor)
            self.skip_inline_space()
            if self.peek() == ":" and self.blank_or_end(self.peek(1)):
                self.fail("complex_key", position)
            self.end_line()
            return value
        quoted, raw, position = self.scalar(False)
        self.skip_inline_space()
        if self.peek() == ":" and self.blank_or_end(self.peek(1)):
            self.fail("unexpected_character")
        self.count(position)
        value = raw if quoted else self.resolve(raw, position)
        self.end_line()
        return value

    def block_sequence(self, indent, depth):
        self.open_collection(depth, self.here())
        out = []
        while True:
            self.offset += 1
            self.skip_inline_space()
            position = self.here()
            if self.break_or_end(self.peek()) or self.at_comment():
                self.end_line()
                end, marker, line_indent = self.line_info()
                if not end and not marker and line_indent > indent:
                    out.append(self.block_node(line_indent, depth + 1, indent + 1))
                else:
                    self.count(position)
                    out.append(None)
            else:
                out.append(self.block_node(self.column(), depth + 1, indent + 1))
            end, marker, line_indent = self.line_info()
            if end or marker or line_indent < indent:
                return out
            if line_indent > indent:
                self.fail("bad_indentation")
            if not self.at_sequence_entry():
                return out

    # ---------------------------------------------------------------- flow
    def skip_flow_space(self, flow_floor):
        while True:
            self.skip_inline_space()
            if self.at_comment():
                self.skip_to_break()
            if self.peek() not in ("\n", "\r") or self.peek() == "":
                return
            self.consume_break()
            while self.peek() == " ":
                self.offset += 1
            if self.peek() == "\t":
                self.fail("tab_indentation")
            c = self.peek()
            if not self.break_or_end(c) and c not in "#]}" and self.column() < flow_floor:
                self.fail("bad_indentation")

    def flow_node(self, depth, flow_floor):
        if self.peek() in ("[", "{"):
            return self.flow_collection(depth, flow_floor)
        quoted, raw, position = self.scalar(True)
        self.count(position)
        return raw if quoted else self.resolve(raw, position)

    def flow_collection(self, depth, flow_floor):
        position = self.here()
        mapping = self.peek() == "{"
        closing = "}" if mapping else "]"
        self.open_collection(depth, position)
        self.offset += 1
        out = {} if mapping else []
        while True:
            self.skip_flow_space(flow_floor)
            if self.peek() == closing:
                self.offset += 1
                return out
            if mapping:
                key, key_position, value = self.flow_entry(depth, flow_floor)
                self.add_key(out, key, key_position)
                out[key] = value
            else:
                item_position = self.here()
                out.append(self.flow_node(depth + 1, flow_floor))
                self.skip_flow_space(flow_floor)
                if self.peek() == ":":
                    self.fail("complex_key", item_position)
            self.skip_flow_space(flow_floor)
            if self.peek() == ",":
                self.offset += 1
                continue
            if self.peek() != closing:
                self.fail_here()

    def flow_entry(self, depth, flow_floor):
        if self.peek() in ("[", "{"):
            self.fail("complex_key")
        quoted, raw, position = self.scalar(True)
        self.skip_inline_space()
        has_value = self.peek() == ":" and (quoted or self.blank_or_end(self.peek(1))
                                             or self.flow_indicator(self.peek(1)))
        key = self.key(quoted, raw, position)
        if has_value:
            self.offset += 1
            self.skip_flow_space(flow_floor)
        value_position = self.here()
        if not has_value or self.peek() in (",", "}"):
            if not has_value and self.peek() not in (",", "}") and not self.break_or_end(self.peek()) \
                    and not self.at_comment():
                self.fail_here()
            self.count(value_position)
            return key, position, None
        return key, position, self.flow_node(depth + 1, flow_floor)


def _read(data, mapping_only):
    if isinstance(data, str):
        data = data.encode("utf-8")
    if len(data) > MAX_INPUT_BYTES:
        raise OamodYamlError("too_large", 0, 0)
    if data.startswith(b"\xef\xbb\xbf"):
        raise OamodYamlError("byte_order_mark", 1, 1)
    try:
        text = data.decode("utf-8")
    except UnicodeDecodeError as e:
        line = data.count(b"\n", 0, e.start) + 1
        raise OamodYamlError("invalid_utf8", line, e.start - (data.rfind(b"\n", 0, e.start) + 1) + 1)
    return _Reader(text).read(mapping_only)


def loads(data):
    """Read a document whose top level is a mapping."""
    return _read(data, True)


def loads_value(data):
    """Read one value of any kind; empty text is None."""
    return _read(data, False)


def load_file(path):
    with open(path, "rb") as f:
        return loads(f.read())
