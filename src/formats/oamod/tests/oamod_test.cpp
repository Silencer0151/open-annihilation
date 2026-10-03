// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The profile reader: what each rule of the strict YAML subset accepts, every
// construct it refuses with the rule and position, the limits, and the exact
// numbers with their canonical text.

#include "oa/formats/oamod.hpp"
#include "oa/test/check.hpp"

#include <cstdio>
#include <span>
#include <string>
#include <string_view>

namespace {

using namespace oa::formats::oamod;

/// Views a string's bytes.
///
/// @param text the string
/// @return its bytes
std::span<const uint8_t> bytes_of(std::string_view text) {
    return std::span<const uint8_t>{reinterpret_cast<const uint8_t*>(text.data()), text.size()};
}

/// Reads a document that must be accepted.
///
/// @param text the document
/// @return its top-level mapping
Node read_ok(std::string_view text) {
    Node root{};
    ReadError error{};
    if (!read_document(bytes_of(text), root, error))
        std::fprintf(
            stderr,
            "unexpected refusal (%s at %u:%u) of:\n%.*s\n",
            rule_message(error.rule),
            error.position.line,
            error.position.column,
            static_cast<int>(text.size()),
            text.data()
        );
    return root;
}

/// Reads a document that must be refused.
///
/// @param text the document
/// @return the error
ReadError read_refused(std::string_view text) {
    Node root{};
    ReadError error{};
    if (read_document(bytes_of(text), root, error))
        std::fprintf(
            stderr, "unexpected acceptance of:\n%.*s\n", static_cast<int>(text.size()), text.data()
        );
    return error;
}

/// Tells whether a document is refused under a rule at a position.
///
/// @param text the document
/// @param rule the rule it must break
/// @param line the line of the error, or 0 to leave it unchecked
/// @param column the column of the error, or 0 to leave it unchecked
/// @return true when it is
bool refused_as(std::string_view text, Rule rule, uint32_t line = 0, uint32_t column = 0) {
    const ReadError error = read_refused(text);
    const bool matches = error.rule == rule && (line == 0 || error.position.line == line) &&
                         (column == 0 || error.position.column == column);
    if (!matches)
        std::fprintf(
            stderr,
            "refusal of %.*s: got %s at %u:%u, expected %s\n",
            static_cast<int>(text.size()),
            text.data(),
            rule_message(error.rule),
            error.position.line,
            error.position.column,
            rule_message(rule)
        );
    return matches;
}

/// Tells whether an entry holds a string.
///
/// @param mapping the mapping
/// @param key the entry's key
/// @param text the string
/// @return true when it does
bool holds_string(const Node& mapping, std::string_view key, std::string_view text) {
    const Node* entry = find_entry(mapping, key);
    return entry != nullptr && entry->kind == NodeKind::string && entry->text == text;
}

/// Reads a number's text that must be accepted.
///
/// @param text the number
/// @return the number
Number number_of(std::string_view text) {
    Number number{};
    if (parse_number(text, number) != NumberStatus::ok)
        std::fprintf(stderr, "not a number: %.*s\n", static_cast<int>(text.size()), text.data());
    return number;
}

void test_scalars() {
    // The YAML 1.1 forms are plain strings.
    const Node legacy = read_ok(
        "a: off\nb: yes\nc: 0x10\nd: 1_000\ne: 2024-12-01\nf: +1\ng: .5\n"
        "h: 01\ni: 5.\nj: .inf\n"
    );
    OA_CHECK(holds_string(legacy, "a", "off"));
    OA_CHECK(holds_string(legacy, "b", "yes"));
    OA_CHECK(holds_string(legacy, "c", "0x10"));
    OA_CHECK(holds_string(legacy, "d", "1_000"));
    OA_CHECK(holds_string(legacy, "e", "2024-12-01"));
    OA_CHECK(holds_string(legacy, "f", "+1"));
    OA_CHECK(holds_string(legacy, "g", ".5"));
    OA_CHECK(holds_string(legacy, "h", "01"));
    OA_CHECK(holds_string(legacy, "i", "5."));
    OA_CHECK(holds_string(legacy, "j", ".inf"));

    // The JSON scalars.
    const Node json = read_ok("a: 1.50\nb: -3\nc: true\nd: null\ne: TRUE\nf: ~\ng:\nh: False\n");
    const Node* a = find_entry(json, "a");
    OA_CHECK(a != nullptr && a->kind == NodeKind::number && !a->number.integer);
    OA_CHECK(a != nullptr && a->text == "1.50" && canonical_number_text(a->number) == "1.5");
    const Node* b = find_entry(json, "b");
    OA_CHECK(b != nullptr && b->kind == NodeKind::number && b->number.integer);
    OA_CHECK(b != nullptr && canonical_number_text(b->number) == "-3");
    const Node* c = find_entry(json, "c");
    OA_CHECK(c != nullptr && c->kind == NodeKind::boolean && c->boolean);
    OA_CHECK(find_entry(json, "d")->kind == NodeKind::null_value);
    OA_CHECK(find_entry(json, "e")->kind == NodeKind::boolean && find_entry(json, "e")->boolean);
    OA_CHECK(find_entry(json, "f")->kind == NodeKind::null_value);
    OA_CHECK(find_entry(json, "g")->kind == NodeKind::null_value);
    OA_CHECK(find_entry(json, "h")->kind == NodeKind::boolean && !find_entry(json, "h")->boolean);

    // Quoting keeps text; single quotes keep backslashes as written.
    const Node quoted = read_ok(
        "a: \"4.8\"\nb: 'Software\\TA Mod'\nc: \"\\x41\\u00e9\\t\"\n"
        "d: 'it''s'\ne: \"say \\\"hi\\\"\"\nf: '*.SWX'\n"
    );
    const Node* version = find_entry(quoted, "a");
    OA_CHECK(version != nullptr && version->kind == NodeKind::string && version->quoted);
    OA_CHECK(version != nullptr && version->text == "4.8");
    OA_CHECK(holds_string(quoted, "b", "Software\\TA Mod"));
    OA_CHECK(holds_string(quoted, "c", "A\xC3\xA9\t"));
    OA_CHECK(holds_string(quoted, "d", "it's"));
    OA_CHECK(holds_string(quoted, "e", "say \"hi\""));
    OA_CHECK(holds_string(quoted, "f", "*.SWX"));
    OA_CHECK(find_entry(quoted, "f")->quoted);

    // Plain scalars may hold spaces, colons not followed by a space, and #
    // not preceded by one; a comment ends them.
    const Node plain = read_ok(
        "Sound Mode: 2\nurl: a:b#c\nmsg: Do you wish? # no\n"
        "exit: Surrender this battle and exit?\n"
    );
    const Node* sound = find_entry(plain, "Sound Mode");
    OA_CHECK(sound != nullptr && sound->kind == NodeKind::number);
    OA_CHECK(holds_string(plain, "url", "a:b#c"));
    OA_CHECK(holds_string(plain, "msg", "Do you wish?"));
}

void test_structure() {
    const Node root = read_ok(
        "# a comment line\n"
        "oamod: 1\n"
        "requires: {base: ta-3.1c, catalogue: 1}\n"
        "layout:\n"
        "  directories:\n"
        "    units: unitsE   # trailing comment\n"
        "script-extensions:\n"
        "  get:\n"
        "    - unit.kills-x100\n"
        "    - unit.min-id\n"
        "  set: {}\n"
        "list:\n"
        "- a\n"
        "- [1, 2,\n"
        "   3]\n"
        "- {x: 1}\n"
        "- k: v\n"
        "  l: w\n"
        "empty-flow: {a: , b}\n"
    );
    OA_CHECK(root.kind == NodeKind::mapping);
    OA_CHECK(root.children.size() == 6);
    // Entries keep the order they are written in.
    OA_CHECK(root.children[0].key.text == "oamod" && root.children[5].key.text == "empty-flow");
    const Node* requires_entry = find_entry(root, "requires");
    OA_CHECK(requires_entry != nullptr && requires_entry->kind == NodeKind::mapping);
    OA_CHECK(requires_entry != nullptr && requires_entry->children.size() == 2);
    const Node* layout = find_entry(root, "layout");
    const Node* directories = layout != nullptr ? find_entry(*layout, "directories") : nullptr;
    OA_CHECK(directories != nullptr && holds_string(*directories, "units", "unitsE"));
    const Node* extensions = find_entry(root, "script-extensions");
    const Node* get = extensions != nullptr ? find_entry(*extensions, "get") : nullptr;
    OA_CHECK(get != nullptr && get->kind == NodeKind::sequence && get->children.size() == 2);
    OA_CHECK(get != nullptr && get->children[1].text == "unit.min-id");
    const Node* set = extensions != nullptr ? find_entry(*extensions, "set") : nullptr;
    OA_CHECK(set != nullptr && set->kind == NodeKind::mapping && set->children.empty());
    // A sequence may sit at its key's indentation.
    const Node* list = find_entry(root, "list");
    OA_CHECK(list != nullptr && list->kind == NodeKind::sequence && list->children.size() == 4);
    OA_CHECK(list != nullptr && list->children[1].children.size() == 3);
    OA_CHECK(list != nullptr && list->children[3].kind == NodeKind::mapping);
    OA_CHECK(list != nullptr && list->children[3].children.size() == 2);
    const Node* empty = find_entry(root, "empty-flow");
    OA_CHECK(empty != nullptr && empty->children.size() == 2);
    OA_CHECK(empty != nullptr && empty->children[0].kind == NodeKind::null_value);
    OA_CHECK(empty != nullptr && empty->children[1].kind == NodeKind::null_value);

    // Positions: lines and byte columns from 1, for keys and values.
    const Node positions = read_ok("a: 1\nb:\n  c: [x, 'y']\n");
    const Node* b = find_entry(positions, "b");
    OA_CHECK(b != nullptr && b->key.position.line == 2 && b->key.position.column == 1);
    const Node* c = b != nullptr ? find_entry(*b, "c") : nullptr;
    OA_CHECK(c != nullptr && c->key.position.line == 3 && c->key.position.column == 3);
    OA_CHECK(c != nullptr && c->position.line == 3 && c->position.column == 6);
    OA_CHECK(c != nullptr && c->children[1].position.column == 10 && c->children[1].quoted);

    // Document markers, and carriage returns before line feeds.
    OA_CHECK(read_ok("---\na: 1\n...\n").children.size() == 1);
    OA_CHECK(read_ok("a: 1\r\nb: 2\r\n").children.size() == 2);
}

void test_keys() {
    // An integer key and a quoted key are different keys.
    const Node keys = read_ok("get: {71: unit.my-id, '71': unit.max-id, -2: x}\n");
    const Node* get = find_entry(keys, "get");
    OA_CHECK(get != nullptr && get->children.size() == 3);
    OA_CHECK(get != nullptr && get->children[0].key.kind == KeyKind::integer);
    OA_CHECK(get != nullptr && get->children[0].key.integer == 71);
    OA_CHECK(get != nullptr && get->children[1].key.kind == KeyKind::string);
    OA_CHECK(get != nullptr && get->children[1].key.quoted && get->children[1].key.text == "71");
    OA_CHECK(get != nullptr && get->children[2].key.integer == -2);
    // Keys that are not strings or integers.
    OA_CHECK(refused_as("true: 1\n", Rule::key_not_string, 1, 1));
    OA_CHECK(refused_as("a:\n  1.5: x\n", Rule::key_not_string, 2, 3));
    OA_CHECK(refused_as("{null: 1}\n", Rule::key_not_string));
    OA_CHECK(refused_as("~: 1\n", Rule::key_not_string));
    // Duplicates, block and flow, integer and string.
    OA_CHECK(refused_as("a: 1\na: 2\n", Rule::duplicate_key, 2, 1));
    OA_CHECK(refused_as("{\"a\": 1, \"a\": 2}\n", Rule::duplicate_key, 1, 10));
    OA_CHECK(refused_as("x: {1: a, 1: b}\n", Rule::duplicate_key));
    OA_CHECK(refused_as("x: {-0: a, 0: b}\n", Rule::duplicate_key));
    OA_CHECK(refused_as("x: {'a': 1, a: 2}\n", Rule::duplicate_key));
    OA_CHECK(refused_as("<<: {a: 1}\n", Rule::merge_key, 1, 1));
}

void test_refusals() {
    OA_CHECK(refused_as("a: &x 1\nb: *x\n", Rule::anchor, 1, 4));
    OA_CHECK(refused_as("a: *x\n", Rule::alias, 1, 4));
    OA_CHECK(refused_as("a: !!str 1\n", Rule::tag, 1, 4));
    OA_CHECK(refused_as("a: !local 1\n", Rule::tag));
    OA_CHECK(refused_as("a: [!!str 1]\n", Rule::tag));
    OA_CHECK(refused_as("a: 1\n---\nb: 2\n", Rule::several_documents, 2, 1));
    OA_CHECK(refused_as("---\na: 1\n---\nb: 2\n", Rule::several_documents));
    OA_CHECK(refused_as("%YAML 1.2\n---\na: 1\n", Rule::directive, 1, 1));
    OA_CHECK(refused_as("a: |\n  text\n", Rule::block_scalar, 1, 4));
    OA_CHECK(refused_as("a: >-\n  text\n", Rule::block_scalar));
    OA_CHECK(refused_as("? a\n: b\n", Rule::complex_key, 1, 1));
    OA_CHECK(refused_as("{[a]: 1}\n", Rule::complex_key));
    OA_CHECK(refused_as("a:\n\tb: 1\n", Rule::tab_indentation, 2, 1));
    OA_CHECK(refused_as("a:\n  b: 1\n c: 2\n", Rule::bad_indentation, 3, 2));
    OA_CHECK(refused_as("a: \"one\n  two\"\n", Rule::multi_line_scalar));
    OA_CHECK(refused_as("a: 'one\n", Rule::multi_line_scalar));
    OA_CHECK(refused_as("a: \"\\q\"\n", Rule::bad_escape, 1, 5));
    OA_CHECK(refused_as("a: @b\n", Rule::unexpected_character));
    OA_CHECK(refused_as("a: %b\n", Rule::unexpected_character));
    OA_CHECK(refused_as("{a: 1} x\n", Rule::unexpected_character));
    // The document is one mapping.
    OA_CHECK(refused_as("", Rule::not_mapping));
    OA_CHECK(refused_as("# only a comment\n", Rule::not_mapping));
    OA_CHECK(refused_as("- a\n- b\n", Rule::not_mapping));
    OA_CHECK(refused_as("42\n", Rule::not_mapping));
    // Encoding.
    OA_CHECK(refused_as(
        "\xEF\xBB\xBF"
        "a: 1\n",
        Rule::byte_order_mark,
        1,
        1
    ));
    OA_CHECK(refused_as("a: 1\nb: \xC3\x28\n", Rule::invalid_utf8, 2, 4));
    OA_CHECK(refused_as("a: \xED\xA0\x80\n", Rule::invalid_utf8));
    OA_CHECK(refused_as("a: 1\x01\n", Rule::control_character, 1, 5));
    OA_CHECK(refused_as("a: 1\rb: 2\n", Rule::control_character));
    // Numbers beyond what the profile holds.
    OA_CHECK(refused_as("a: 9007199254740993\n", Rule::integer_too_large, 1, 4));
    OA_CHECK(refused_as("a: -9007199254740993\n", Rule::integer_too_large));
    OA_CHECK(read_ok("a: 9007199254740992\n").children.size() == 1);
    OA_CHECK(refused_as("a: 0.1234567890123456\n", Rule::too_many_digits));
    OA_CHECK(read_ok("a: 0.123456789012345\n").children.size() == 1);
    OA_CHECK(read_ok("a: 1000000000000000.0\n").children.size() == 1);
    OA_CHECK(refused_as("a: 1e301\n", Rule::number_out_of_range));
    OA_CHECK(refused_as("a: 1e-301\n", Rule::number_out_of_range));
    OA_CHECK(read_ok("a: 9.99e300\nb: 1e-300\nc: 0e999\n").children.size() == 3);
}

void test_limits() {
    std::string deep;
    for (int level = 0; level < 15; ++level)
        deep += "a: {";
    deep += "b: 1";
    deep.append(15, '}');
    deep += '\n';
    OA_CHECK(read_ok(deep).kind == NodeKind::mapping);
    std::string deeper;
    for (int level = 0; level < 16; ++level)
        deeper += "a: {";
    deeper += "b: 1";
    deeper.append(16, '}');
    deeper += '\n';
    OA_CHECK(refused_as(deeper, Rule::too_deep));
    std::string block_deep;
    for (int level = 0; level < 16; ++level) {
        block_deep.append(static_cast<size_t>(level) * 2, ' ');
        block_deep += "k:\n";
    }
    block_deep.append(32, ' ');
    block_deep += "- x\n";
    OA_CHECK(refused_as(block_deep, Rule::too_deep));

    std::string large = "a: \"";
    large.append(max_input_bytes, 'x');
    large += "\"\n";
    OA_CHECK(refused_as(large, Rule::too_large));
    std::string long_string = "a: \"";
    long_string.append(max_string_bytes + 1, 'x');
    long_string += "\"\n";
    OA_CHECK(refused_as(long_string, Rule::string_too_long));
}

void test_values() {
    Node value{};
    ReadError error{};
    OA_CHECK(read_value(bytes_of("400"), value, error) && value.kind == NodeKind::number);
    OA_CHECK(read_value(bytes_of("[1, 2]"), value, error) && value.children.size() == 2);
    OA_CHECK(read_value(bytes_of("true"), value, error) && value.kind == NodeKind::boolean);
    OA_CHECK(read_value(bytes_of(""), value, error) && value.kind == NodeKind::null_value);
    OA_CHECK(!read_value(bytes_of("&a 1"), value, error) && error.rule == Rule::anchor);
}

void test_numbers() {
    // RFC 8785's spellings.
    OA_CHECK(canonical_number_text(number_of("1e21")) == "1e+21");
    OA_CHECK(canonical_number_text(number_of("1e-7")) == "1e-7");
    OA_CHECK(canonical_number_text(number_of("0.000001")) == "0.000001");
    OA_CHECK(canonical_number_text(number_of("4294967296000.0")) == "4294967296000");
    OA_CHECK(canonical_number_text(number_of("4.0")) == "4");
    OA_CHECK(canonical_number_text(number_of("0.5")) == "0.5");
    OA_CHECK(canonical_number_text(number_of("-0.0")) == "0");
    OA_CHECK(canonical_number_text(number_of("-0")) == "0");
    OA_CHECK(canonical_number_text(number_of("1e23")) == "1e+23");
    OA_CHECK(canonical_number_text(number_of("100e-2")) == "1");
    OA_CHECK(canonical_number_text(number_of("-12.50")) == "-12.5");
    OA_CHECK(canonical_number_text(number_of("1000000000000000.0")) == "1000000000000000");
    OA_CHECK(
        canonical_number_text(Number{false, 3333333333333333, -7, false}) == "333333333.3333333"
    );
    OA_CHECK(canonical_number_text(Number{false, 5, -324, false}) == "5e-324");
    OA_CHECK(
        canonical_number_text(Number{false, 12345678901234568, 4, false}) == "123456789012345680000"
    );
    OA_CHECK(canonical_number_text(number_from_integer(1500)) == "1500");
    OA_CHECK(canonical_number_text(number_from_integer(-20)) == "-20");

    // Equal values compare equal whatever their spelling; order is by value.
    OA_CHECK(compare_numbers(number_of("1.50"), number_of("15e-1")) == 0);
    OA_CHECK(compare_numbers(number_of("4"), number_of("4.0")) == 0);
    OA_CHECK(compare_numbers(number_of("0.7"), number_of("1")) < 0);
    OA_CHECK(compare_numbers(number_of("-3"), number_of("-20")) > 0);
    OA_CHECK(compare_numbers(number_of("100"), number_of("99.5")) > 0);
    OA_CHECK(compare_numbers(number_of("-0.0"), number_of("0")) == 0);
    OA_CHECK(compare_numbers(number_of("1e15"), number_of("1000000000000000.0")) == 0);

    int64_t whole = 0;
    OA_CHECK(integer_value(number_of("1.5e3"), whole) && whole == 1500);
    OA_CHECK(!integer_value(number_of("1.5"), whole));
    OA_CHECK(integer_value(number_of("-9007199254740992"), whole) && whole == -9007199254740992);
    OA_CHECK(number_of("4294967296000").integer && !number_of("4294967296000.0").integer);

    OA_CHECK(number_to_double(number_of("0.7")) == 0.7);
    OA_CHECK(number_to_double(number_of("-4294967296000")) == -4294967296000.0);
    OA_CHECK(number_to_double(number_of("1e300")) == 1e300);
    OA_CHECK(number_to_double(number_of("1.25e-5")) == 1.25e-5);

    Number number{};
    OA_CHECK(parse_number("0012", number) == NumberStatus::not_a_number);
    OA_CHECK(parse_number("0012", number, true) == NumberStatus::ok && number.significand == 12);
    OA_CHECK(parse_number("1.", number) == NumberStatus::not_a_number);
    OA_CHECK(parse_number("1e", number) == NumberStatus::not_a_number);
    OA_CHECK(parse_number("- 1", number) == NumberStatus::not_a_number);
}

} // namespace

int main() {
    test_scalars();
    test_structure();
    test_keys();
    test_refusals();
    test_limits();
    test_values();
    test_numbers();
    return oa::test::check_exit_status();
}
