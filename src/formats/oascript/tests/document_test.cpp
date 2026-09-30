// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The document reader and writer: form detection, JSON and YAML reading into
// one tree, every read status with its position, the named limits at and
// past their bounds, exact decimals, the writers' exact bytes, round trips
// in both forms, and a sweep of truncated and damaged texts.

#include "check.hpp"
#include "document_internal.hpp"

#include "oa/formats/oascript/document.hpp"

#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace oa::formats::oascript;
using oa::formats::oascript::test::bytes_of;
using oa::formats::oascript::test::same_tree;

/// A script in the YAML subset, with comments and both collection styles.
constexpr std::string_view example_yaml{R"(# A director script.
oascript: 1
input:
  demo: game.rec
  tickrate: 30
output:
  resolution: { width: 1920, height: 1080 }
  framerate: 60
  chunking: { mode: seconds, length: 60 }
  showUx: false
director:
  shots:
    - tick: 0
      cameraStart:
        position: { x: 1024, y: 2048, z: 1024 }
      cameraEnd:
        position: { x: 512.5, y: 640, z: 700 }
        orientation: { pitch: 0, yaw: 0, roll: 0 }
      motion: { linear: {} }
    - tick: 900  # continues from the first shot
      cameraEnd:
        position: {x: 300, y: 480,
                   z: 1200.25}
      motion:
        spring: { frequency: 0.5, dampingRatio: 1.0 }
      transition: { type: dissolve, duration: 0.5 }
  endTick: 1800
)"};

/// The same script in JSON.
constexpr std::string_view example_json{R"({
  "oascript": 1,
  "input": {"demo": "game.rec", "tickrate": 30},
  "output": {
    "resolution": {"width": 1920, "height": 1080},
    "framerate": 60,
    "chunking": {"mode": "seconds", "length": 60},
    "showUx": false
  },
  "director": {
    "shots": [
      {
        "tick": 0,
        "cameraStart": {"position": {"x": 1024, "y": 2048, "z": 1024}},
        "cameraEnd": {
          "position": {"x": 512.5, "y": 640, "z": 700},
          "orientation": {"pitch": 0, "yaw": 0, "roll": 0}
        },
        "motion": {"linear": {}}
      },
      {
        "tick": 900,
        "cameraEnd": {"position": {"x": 300, "y": 480, "z": 1200.25}},
        "motion": {"spring": {"frequency": 0.5, "dampingRatio": 1.0}},
        "transition": {"type": "dissolve", "duration": 0.5}
      }
    ],
    "endTick": 1800
  }
}
)"};

/// Reads a text, expecting success.
///
/// @param text the text
/// @return the tree; a null node when the read failed, which is also checked
Node read_ok(std::string_view text) {
    Node root{};
    ReadError error{};
    const bool read{read_document(bytes_of(text), root, error)};
    CHECK(read);
    CHECK(error.status == ReadStatus::ok);
    if (!read)
        std::fprintf(
            stderr,
            "  read failed: %s at %u:%u\n",
            read_status_message(error.status),
            error.position.line,
            error.position.column
        );
    return root;
}

/// Reads a text, expecting a failure with a status at a position.
///
/// @param text the text
/// @param status the status expected
/// @param line the line expected
/// @param column the byte column expected
/// @param line_number the calling test's line, for the report
void expect_failure(
    std::string_view text, ReadStatus status, uint32_t line, uint32_t column, int line_number
) {
    Node root{};
    ReadError error{};
    const bool read{read_document(bytes_of(text), root, error)};
    const bool matches{
        !read && error.status == status && error.position.line == line &&
        error.position.column == column && root.kind == NodeKind::null_value
    };
    oa::formats::oascript::test::check(matches, "expected read failure", __FILE__, line_number);
    if (!matches)
        std::fprintf(
            stderr,
            "  got %s (%d) at %u:%u, expected %s at %u:%u\n",
            read ? "success" : read_status_message(error.status),
            static_cast<int>(error.status),
            error.position.line,
            error.position.column,
            read_status_message(status),
            line,
            column
        );
}

/// Returns a string repeated.
///
/// @param text the string
/// @param count how many times
/// @return the repetitions
std::string repeat(std::string_view text, size_t count) {
    std::string out{};
    out.reserve(text.size() * count);
    for (size_t index{}; index < count; ++index)
        out.append(text);
    return out;
}

/// Checks a number read from YAML and from JSON.
///
/// @param text the number's text, valid in both forms
/// @param mantissa the mantissa expected
/// @param places the places expected
void expect_number(std::string_view text, int64_t mantissa, uint32_t places) {
    for (const bool json : {false, true}) {
        const std::string document{
            json ? "{\"n\": " + std::string{text} + "}" : "n: " + std::string{text} + "\n"
        };
        const Node root{read_ok(document)};
        const Node* number{find_entry(root, "n")};
        const bool matches{
            number != nullptr && number->kind == NodeKind::number &&
            number->number.mantissa == mantissa && number->number.places == places
        };
        CHECK(matches);
        if (!matches)
            std::fprintf(
                stderr,
                "  number %.*s (%s)\n",
                static_cast<int>(text.size()),
                text.data(),
                json ? "JSON" : "YAML"
            );
    }
}

/// Checks form detection.
void test_detect_form() {
    CHECK(detect_form(bytes_of("{}")) == DocumentForm::json);
    CHECK(detect_form(bytes_of(" \t\r\n{\"a\": 1}")) == DocumentForm::json);
    CHECK(detect_form(bytes_of("\xEF\xBB\xBF  {")) == DocumentForm::json);
    CHECK(detect_form(bytes_of("a: 1")) == DocumentForm::yaml);
    CHECK(detect_form(bytes_of("# {\n{}")) == DocumentForm::yaml);
    CHECK(detect_form(bytes_of("")) == DocumentForm::yaml);
    CHECK(detect_form(bytes_of("\xEF\xBB\xBF")) == DocumentForm::yaml);
    CHECK(detect_form(bytes_of("[1]")) == DocumentForm::yaml);
}

/// Checks that the example reads to the same tree in both forms, and what
/// that tree holds.
void test_equivalence() {
    const Node yaml{read_ok(example_yaml)};
    const Node json{read_ok(example_json)};
    CHECK(same_tree(yaml, json));
    CHECK(yaml.kind == NodeKind::mapping);
    CHECK(yaml.children.size() == 4);
    const Node* director{find_entry(yaml, "director")};
    CHECK(director != nullptr);
    if (director == nullptr)
        return;
    const Node* shots{find_entry(*director, "shots")};
    CHECK(shots != nullptr && shots->kind == NodeKind::sequence && shots->children.size() == 2);
    if (shots == nullptr || shots->children.size() != 2)
        return;
    const Node& second{shots->children[1]};
    CHECK(second.position.line == 20 && second.position.column == 7);
    const Node* camera{find_entry(second, "cameraEnd")};
    const Node* position{camera != nullptr ? find_entry(*camera, "position") : nullptr};
    const Node* z{position != nullptr ? find_entry(*position, "z") : nullptr};
    CHECK(z != nullptr && z->number.mantissa == 120025 && z->number.places == 2);
    CHECK(z != nullptr && z->position.line == 23 && z->position.column == 23);
    CHECK(z != nullptr && z->key_position.line == 23 && z->key_position.column == 20);
    const Node* spring{find_entry(*find_entry(second, "motion"), "spring")};
    const Node* damping{spring != nullptr ? find_entry(*spring, "dampingRatio") : nullptr};
    CHECK(damping != nullptr && damping->number.mantissa == 10 && damping->number.places == 1);
    const Node* linear{find_entry(*find_entry(shots->children[0], "motion"), "linear")};
    CHECK(linear != nullptr && linear->kind == NodeKind::mapping && linear->children.empty());

    // JSON positions: the key's quote and the value's first byte.
    const Node* input{find_entry(json, "input")};
    CHECK(input != nullptr && input->key_position.line == 3 && input->key_position.column == 3);
    CHECK(input != nullptr && input->position.line == 3 && input->position.column == 12);
    CHECK(find_entry(json, "missing") == nullptr);
    CHECK(find_entry(*find_entry(json, "oascript"), "oascript") == nullptr);
}

/// Checks the JSON reader's values: literals, escapes and surrogate pairs.
void test_json_values() {
    const Node root{read_ok(
        R"({"t": true, "f": false, "n": null, "s": "a\"\\\/\b\f\n\r\tz", "u": "\u00e9\u20AC\ud83d\ude00\u0000",)"
        R"( "e": [], "m": {}, "l": [1, [2, {"k": "v"}]], "raw": "é€😀"})"
    )};
    CHECK(find_entry(root, "t")->kind == NodeKind::boolean && find_entry(root, "t")->boolean);
    CHECK(find_entry(root, "f")->kind == NodeKind::boolean && !find_entry(root, "f")->boolean);
    CHECK(find_entry(root, "n")->kind == NodeKind::null_value);
    CHECK(find_entry(root, "s")->text == "a\"\\/\b\f\n\r\tz");
    const std::string escaped{"\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80\0", 10};
    CHECK(find_entry(root, "u")->text == escaped);
    CHECK(find_entry(root, "raw")->text == "\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80");
    CHECK(
        find_entry(root, "e")->kind == NodeKind::sequence && find_entry(root, "e")->children.empty()
    );
    CHECK(
        find_entry(root, "m")->kind == NodeKind::mapping && find_entry(root, "m")->children.empty()
    );
    const Node* list{find_entry(root, "l")};
    CHECK(list->children.size() == 2 && list->children[1].children[1].children[0].key == "k");
    // A byte order mark is skipped; columns on its line count after it.
    const Node marked{read_ok("\xEF\xBB\xBF{\"a\": 1}")};
    CHECK(find_entry(marked, "a") != nullptr && find_entry(marked, "a")->key_position.column == 2);
}

/// Checks the YAML reader's structures and the core schema's resolution.
void test_yaml_values() {
    const Node root{read_ok(R"(---
# plain scalars
nulls: [null, Null, NULL, ~, ]
empty:
booleans: [true, True, TRUE, false, False, FALSE]
words: [yes, no, on, off, nulled, trueish]
numbers: [007, +5, -5, 1., .5, -.5, 2.5e2, 1E-3]
strings: [0x, 0xZZ, 0o8, .in, a:b, x-y, 1-2]
"quoted key": 'it''s'
'single': "tab\there \x41\u00e9\U0001F600 \N\_\L\P\/\ \e\a\v\0"
plain with spaces: several words here  # comment
indentless:
- one
- two
nested:
  - - a
    - b
  - k: v
    k2: [1, 2]
  -
    deep: {x: 1}
  -
  - {a: 1, b}
dash in key-name: x
url: http://example/a#frag
...
# nothing after the end marker but comments
)")};
    const Node* nulls{find_entry(root, "nulls")};
    CHECK(nulls != nullptr && nulls->children.size() == 4);
    for (const Node& item : nulls->children)
        CHECK(item.kind == NodeKind::null_value);
    CHECK(find_entry(root, "empty")->kind == NodeKind::null_value);
    const Node* booleans{find_entry(root, "booleans")};
    for (size_t index{}; index < booleans->children.size(); ++index)
        CHECK(
            booleans->children[index].kind == NodeKind::boolean &&
            booleans->children[index].boolean == (index < 3)
        );
    for (const Node& item : find_entry(root, "words")->children)
        CHECK(item.kind == NodeKind::string);
    const Node* numbers{find_entry(root, "numbers")};
    const Decimal expected_numbers[]{
        {7, 0}, {5, 0}, {-5, 0}, {1, 0}, {5, 1}, {-5, 1}, {250, 0}, {1, 3}
    };
    CHECK(numbers->children.size() == std::size(expected_numbers));
    for (size_t index{}; index < numbers->children.size() && index < std::size(expected_numbers);
         ++index) {
        const Node& item{numbers->children[index]};
        CHECK(
            item.kind == NodeKind::number &&
            item.number.mantissa == expected_numbers[index].mantissa &&
            item.number.places == expected_numbers[index].places
        );
    }
    for (const Node& item : find_entry(root, "strings")->children)
        CHECK(item.kind == NodeKind::string);
    CHECK(find_entry(root, "strings")->children[4].text == "a:b");
    CHECK(find_entry(root, "quoted key")->text == "it's");
    const std::string escapes{
        "tab\there A\xC3\xA9\xF0\x9F\x98\x80 \xC2\x85\xC2\xA0\xE2\x80\xA8\xE2\x80\xA9/ "
        "\x1B\x07\x0B\0",
        33
    };
    CHECK(find_entry(root, "single")->text == escapes);
    CHECK(find_entry(root, "plain with spaces")->text == "several words here");
    const Node* indentless{find_entry(root, "indentless")};
    CHECK(
        indentless->kind == NodeKind::sequence && indentless->children.size() == 2 &&
        indentless->children[1].text == "two"
    );
    const Node* nested{find_entry(root, "nested")};
    CHECK(nested->children.size() == 5);
    CHECK(
        nested->children[0].kind == NodeKind::sequence && nested->children[0].children.size() == 2
    );
    CHECK(
        nested->children[1].kind == NodeKind::mapping && nested->children[1].children.size() == 2
    );
    CHECK(
        nested->children[2].kind == NodeKind::mapping &&
        nested->children[2].children[0].key == "deep"
    );
    CHECK(nested->children[3].kind == NodeKind::null_value);
    CHECK(
        nested->children[4].children[1].key == "b" &&
        nested->children[4].children[1].kind == NodeKind::null_value
    );
    CHECK(find_entry(root, "dash in key-name")->text == "x");
    CHECK(find_entry(root, "url")->text == "http://example/a#frag");

    // Line ends of carriage return and line feed read as line feeds.
    const Node crlf{read_ok("a: 1\r\nb:\r\n  - x\r\n")};
    CHECK(same_tree(crlf, read_ok("a: 1\nb:\n  - x\n")));
    CHECK(find_entry(crlf, "b")->children[0].position.line == 3);
    // A top-level flow mapping, after a comment.
    CHECK(same_tree(read_ok("# c\n{a: 1, \"b\":2}\n"), read_ok("{\"a\": 1, \"b\": 2}")));
    // A value on the line after its key.
    CHECK(find_entry(read_ok("a:\n  hello\n"), "a")->text == "hello");
    // A byte order mark before YAML.
    CHECK(
        find_entry(
            read_ok(
                "\xEF\xBB\xBF"
                "a: 1\nb: 2\n"
            ),
            "b"
        ) != nullptr
    );
}

/// Checks every read status with its position.
void test_statuses() {
    // JSON.
    expect_failure("{\"a\": \"\xC3\x28\"}", ReadStatus::invalid_utf8, 1, 8, __LINE__);
    expect_failure("{\n\"a\": \"\xED\xA0\x80\"}", ReadStatus::invalid_utf8, 2, 7, __LINE__);
    expect_failure("{\"a\": \"\xC0\xAF\"}", ReadStatus::invalid_utf8, 1, 8, __LINE__);
    expect_failure("{\"a\": \"\xF4\x90\x80\x80\"}", ReadStatus::invalid_utf8, 1, 8, __LINE__);
    expect_failure("{\"a\": \"\xE2\x82\"", ReadStatus::invalid_utf8, 1, 8, __LINE__);
    expect_failure("{\"a\": 1", ReadStatus::unexpected_end, 1, 8, __LINE__);
    expect_failure("{\"a\": \"x", ReadStatus::unexpected_end, 1, 9, __LINE__);
    expect_failure("{\"a\": tru", ReadStatus::unexpected_end, 1, 10, __LINE__);
    expect_failure("{\"a\": -", ReadStatus::unexpected_end, 1, 8, __LINE__);
    expect_failure("{\"a\" 1}", ReadStatus::unexpected_character, 1, 6, __LINE__);
    expect_failure("{\"a\": 1,}", ReadStatus::unexpected_character, 1, 9, __LINE__);
    expect_failure("{\"a\": [1,]}", ReadStatus::unexpected_character, 1, 10, __LINE__);
    expect_failure("{\"a\": trux}", ReadStatus::unexpected_character, 1, 7, __LINE__);
    expect_failure("{\"a\": 'x'}", ReadStatus::unexpected_character, 1, 7, __LINE__);
    expect_failure("{\"a\": \"x\ty\"}", ReadStatus::unexpected_character, 1, 9, __LINE__);
    expect_failure("{a: 1}", ReadStatus::unexpected_character, 1, 2, __LINE__);
    expect_failure("{\"a\": 1 // no comments\n}", ReadStatus::unexpected_character, 1, 9, __LINE__);
    expect_failure("{\"a\": +1}", ReadStatus::unexpected_character, 1, 7, __LINE__);
    expect_failure("{\"a\": .5}", ReadStatus::unexpected_character, 1, 7, __LINE__);
    expect_failure("{\"a\": \"\\q\"}", ReadStatus::bad_escape, 1, 8, __LINE__);
    expect_failure("{\"a\": \"\\u12G4\"}", ReadStatus::bad_escape, 1, 8, __LINE__);
    expect_failure("{\"a\": \"\\udc00\"}", ReadStatus::bad_escape, 1, 8, __LINE__);
    expect_failure("{\"a\": \"\\ud800\"}", ReadStatus::bad_escape, 1, 8, __LINE__);
    expect_failure("{\"a\": \"\\ud800\\n\"}", ReadStatus::bad_escape, 1, 8, __LINE__);
    expect_failure("{\"a\": \"\\ud800\\u0041\"}", ReadStatus::bad_escape, 1, 8, __LINE__);
    expect_failure("{\"a\": \"\\ud800", ReadStatus::unexpected_end, 1, 14, __LINE__);
    expect_failure("{\"a\": \"\\u12", ReadStatus::unexpected_end, 1, 12, __LINE__);
    expect_failure("{\"a\": 01}", ReadStatus::bad_number, 1, 7, __LINE__);
    expect_failure("{\"a\": -x}", ReadStatus::bad_number, 1, 7, __LINE__);
    expect_failure("{\"a\": 1.}", ReadStatus::bad_number, 1, 7, __LINE__);
    expect_failure("{\"a\": 1.e5}", ReadStatus::bad_number, 1, 7, __LINE__);
    expect_failure("{\"a\": 1e+}", ReadStatus::bad_number, 1, 7, __LINE__);
    expect_failure("{\"a\": 1e19}", ReadStatus::number_out_of_range, 1, 7, __LINE__);
    expect_failure("{\"a\": 1, \"a\": 2}", ReadStatus::duplicate_key, 1, 10, __LINE__);
    expect_failure("{} x", ReadStatus::trailing_content, 1, 4, __LINE__);
    expect_failure("{}\n{}", ReadStatus::trailing_content, 2, 1, __LINE__);

    // YAML.
    expect_failure("a: \xFF\n", ReadStatus::invalid_utf8, 1, 4, __LINE__);
    expect_failure("", ReadStatus::top_level_not_mapping, 1, 1, __LINE__);
    expect_failure("# only a comment\n", ReadStatus::top_level_not_mapping, 2, 1, __LINE__);
    expect_failure("- a\n- b\n", ReadStatus::top_level_not_mapping, 1, 1, __LINE__);
    expect_failure("hello\n", ReadStatus::top_level_not_mapping, 1, 1, __LINE__);
    expect_failure("  [1, 2]\n", ReadStatus::top_level_not_mapping, 1, 3, __LINE__);
    expect_failure("a: \"abc", ReadStatus::unexpected_end, 1, 8, __LINE__);
    expect_failure("a: [1, 2", ReadStatus::unexpected_end, 1, 9, __LINE__);
    expect_failure("a: {b: 1\n", ReadStatus::unexpected_end, 2, 1, __LINE__);
    expect_failure("a: b: c\n", ReadStatus::unexpected_character, 1, 5, __LINE__);
    expect_failure("a: 1\nb\n", ReadStatus::unexpected_character, 2, 2, __LINE__);
    expect_failure("a: \"x\" y\n", ReadStatus::unexpected_character, 1, 8, __LINE__);
    expect_failure("a: \"x\ny\"\n", ReadStatus::unexpected_character, 1, 6, __LINE__);
    expect_failure("a: 'x\n", ReadStatus::unexpected_character, 1, 6, __LINE__);
    expect_failure("a: - b\n", ReadStatus::unexpected_character, 1, 4, __LINE__);
    expect_failure("a: [1 2\n  3]\n", ReadStatus::unexpected_character, 2, 3, __LINE__);
    expect_failure("a: [b: 1]\n", ReadStatus::unexpected_character, 1, 6, __LINE__);
    expect_failure("a: @x\n", ReadStatus::unexpected_character, 1, 4, __LINE__);
    expect_failure("a: `x\n", ReadStatus::unexpected_character, 1, 4, __LINE__);
    expect_failure("a: %x\n", ReadStatus::unexpected_character, 1, 4, __LINE__);
    expect_failure("a: 1\x01\n", ReadStatus::unexpected_character, 1, 5, __LINE__);
    expect_failure("a: 1\rb: 2\n", ReadStatus::unexpected_character, 1, 5, __LINE__);
    expect_failure("a: [1]#c\n", ReadStatus::unexpected_character, 1, 7, __LINE__);
    expect_failure("--- a: 1\n", ReadStatus::unexpected_character, 1, 5, __LINE__);
    expect_failure("a: \"\\q\"\n", ReadStatus::bad_escape, 1, 5, __LINE__);
    expect_failure("a: \"\\ud800\"\n", ReadStatus::bad_escape, 1, 5, __LINE__);
    expect_failure("a: \"\\U00110000\"\n", ReadStatus::bad_escape, 1, 5, __LINE__);
    expect_failure("a: \"\\xG0\"\n", ReadStatus::bad_escape, 1, 5, __LINE__);
    expect_failure("a: 1234567890123456789\n", ReadStatus::number_out_of_range, 1, 4, __LINE__);
    expect_failure(
        "a: [1, 0.0000000000000000001]\n", ReadStatus::number_out_of_range, 1, 8, __LINE__
    );
    expect_failure("a: 1\na: 2\n", ReadStatus::duplicate_key, 2, 1, __LINE__);
    expect_failure("a: {b: 1, 'b': 2}\n", ReadStatus::duplicate_key, 1, 11, __LINE__);
    expect_failure("# c\n{a: 1}\nb: 2\n", ReadStatus::trailing_content, 3, 1, __LINE__);
    expect_failure("a:\n\tb: 1\n", ReadStatus::tab_indentation, 2, 1, __LINE__);
    expect_failure("a:\n  \tb: 1\n", ReadStatus::tab_indentation, 2, 3, __LINE__);
    expect_failure("a: [1,\n\t2]\n", ReadStatus::tab_indentation, 2, 1, __LINE__);
    expect_failure("a: 1\n  b: 2\n", ReadStatus::bad_indentation, 2, 3, __LINE__);
    expect_failure("a:\n    b: 1\n  c: 2\n", ReadStatus::bad_indentation, 3, 3, __LINE__);
    expect_failure("a:\n  - x\n  y: 1\n", ReadStatus::bad_indentation, 3, 3, __LINE__);
    expect_failure("a: 1\n- b\n", ReadStatus::bad_indentation, 2, 1, __LINE__);
    expect_failure("  a: 1\nb: 2\n", ReadStatus::bad_indentation, 2, 1, __LINE__);
    expect_failure("a: [1,\n2]\n", ReadStatus::bad_indentation, 2, 1, __LINE__);
    expect_failure("a: &x 1\n", ReadStatus::unsupported_anchor, 1, 4, __LINE__);
    expect_failure("a: *x\n", ReadStatus::unsupported_alias, 1, 4, __LINE__);
    expect_failure("a: [*x]\n", ReadStatus::unsupported_alias, 1, 5, __LINE__);
    expect_failure("a: !!str 1\n", ReadStatus::unsupported_tag, 1, 4, __LINE__);
    expect_failure("a: |\n  x\n", ReadStatus::unsupported_block_scalar, 1, 4, __LINE__);
    expect_failure("a: >-\n  x\n", ReadStatus::unsupported_block_scalar, 1, 4, __LINE__);
    expect_failure("%YAML 1.2\n---\na: 1\n", ReadStatus::unsupported_directive, 1, 1, __LINE__);
    expect_failure("? a\n: b\n", ReadStatus::unsupported_complex_key, 1, 1, __LINE__);
    expect_failure("[a]: b\n", ReadStatus::unsupported_complex_key, 1, 1, __LINE__);
    expect_failure("a: 1\n{b}: 2\n", ReadStatus::unsupported_complex_key, 2, 1, __LINE__);
    expect_failure("a: {? b}\n", ReadStatus::unsupported_complex_key, 1, 5, __LINE__);
    expect_failure("a: 1\n---\nb: 2\n", ReadStatus::several_documents, 2, 1, __LINE__);
    expect_failure("---\n---\na: 1\n", ReadStatus::several_documents, 2, 1, __LINE__);
    expect_failure("a: 1\n...\nb: 2\n", ReadStatus::several_documents, 3, 1, __LINE__);
    expect_failure("a: 0x1F\n", ReadStatus::unsupported_number_form, 1, 4, __LINE__);
    expect_failure("a: 0o17\n", ReadStatus::unsupported_number_form, 1, 4, __LINE__);
    expect_failure("a: [.inf]\n", ReadStatus::unsupported_number_form, 1, 5, __LINE__);
    expect_failure("a: -.Inf\n", ReadStatus::unsupported_number_form, 1, 4, __LINE__);
    expect_failure("a: .NaN\n", ReadStatus::unsupported_number_form, 1, 4, __LINE__);

    // Too large: longer than the limit, before anything else is looked at.
    const std::string too_large(max_input_bytes + 1, '\xFF');
    expect_failure(too_large, ReadStatus::too_large, 0, 0, __LINE__);

    // Every status has its own message.
    for (int status{}; status <= static_cast<int>(ReadStatus::unsupported_number_form); ++status) {
        const std::string_view message{read_status_message(static_cast<ReadStatus>(status))};
        CHECK(!message.empty());
        for (int other{}; other < status; ++other)
            CHECK(message != read_status_message(static_cast<ReadStatus>(other)));
    }
}

/// Checks the limits at and past each bound.
void test_limits() {
    // The input size: a comment pads a script to exactly the limit.
    std::string largest{"a: 1\n#"};
    largest.append(max_input_bytes - largest.size(), 'x');
    CHECK(largest.size() == max_input_bytes);
    CHECK(find_entry(read_ok(largest), "a") != nullptr);
    largest.push_back('x');
    expect_failure(largest, ReadStatus::too_large, 0, 0, __LINE__);

    // Nesting: the top level is depth 1, so 31 more levels fit.
    const size_t deepest{max_nesting_depth - 1};
    read_ok("{\"a\": " + repeat("[", deepest) + repeat("]", deepest) + "}");
    expect_failure(
        "{\"a\": " + repeat("[", deepest + 1) + repeat("]", deepest + 1) + "}",
        ReadStatus::too_deep,
        1,
        static_cast<uint32_t>(7 + deepest),
        __LINE__
    );
    read_ok("a: " + repeat("[", deepest) + repeat("]", deepest) + "\n");
    expect_failure(
        "a: " + repeat("[", deepest + 1) + repeat("]", deepest + 1) + "\n",
        ReadStatus::too_deep,
        1,
        static_cast<uint32_t>(4 + deepest),
        __LINE__
    );
    std::string block{};
    for (size_t level{}; level < max_nesting_depth; ++level)
        block += std::string(level * 2, ' ') + "k:\n";
    block += std::string(max_nesting_depth * 2, ' ') + "v\n";
    read_ok(block);
    std::string deeper{};
    for (size_t level{}; level <= max_nesting_depth; ++level)
        deeper += std::string(level * 2, ' ') + "k:\n";
    deeper += std::string((max_nesting_depth + 1) * 2, ' ') + "v\n";
    expect_failure(
        deeper, ReadStatus::too_deep, max_nesting_depth + 1, max_nesting_depth * 2 + 1, __LINE__
    );
    std::string sequences{"a:\n"};
    for (size_t level{1}; level < max_nesting_depth; ++level)
        sequences += std::string(level * 2, ' ') + "-\n";
    sequences += std::string(max_nesting_depth * 2, ' ') + "x\n";
    read_ok(sequences);

    // Nodes: the top-level mapping, the sequence and its items.
    const size_t most_items{max_node_count - 2};
    read_ok("{\"a\": [" + repeat("0,", most_items - 1) + "0]}");
    const std::string too_many_json{"{\"a\": [" + repeat("0,", most_items) + "0]}"};
    expect_failure(
        too_many_json,
        ReadStatus::too_many_nodes,
        1,
        static_cast<uint32_t>(8 + 2 * most_items),
        __LINE__
    );
    read_ok("a: [" + repeat("0,", most_items - 1) + "0]\n");
    expect_failure(
        "a: [" + repeat("0,", most_items) + "0]\n",
        ReadStatus::too_many_nodes,
        1,
        static_cast<uint32_t>(5 + 2 * most_items),
        __LINE__
    );

    // Strings and keys.
    const std::string longest(max_string_bytes, 'x');
    const std::string too_long(max_string_bytes + 1, 'x');
    CHECK(find_entry(read_ok("{\"a\": \"" + longest + "\"}"), "a")->text == longest);
    CHECK(find_entry(read_ok("{\"" + longest + "\": 1}"), longest) != nullptr);
    CHECK(find_entry(read_ok("a: " + longest + "\n"), "a")->text == longest);
    CHECK(find_entry(read_ok("a: '" + longest + "'\n"), "a")->text == longest);
    CHECK(find_entry(read_ok("a: \"" + longest + "\"\n"), "a")->text == longest);
    CHECK(find_entry(read_ok(longest + ": 1\n"), longest) != nullptr);
    CHECK(find_entry(read_ok("a: {" + longest + ": 1}\n"), "a")->children[0].key == longest);
    expect_failure("{\"a\": \"" + too_long + "\"}", ReadStatus::string_too_long, 1, 7, __LINE__);
    expect_failure("{\"" + too_long + "\": 1}", ReadStatus::string_too_long, 1, 2, __LINE__);
    expect_failure("a: " + too_long + "\n", ReadStatus::string_too_long, 1, 4, __LINE__);
    expect_failure("a: '" + too_long + "'\n", ReadStatus::string_too_long, 1, 4, __LINE__);
    expect_failure("a: \"" + too_long + "\"\n", ReadStatus::string_too_long, 1, 4, __LINE__);
    expect_failure(too_long + ": 1\n", ReadStatus::string_too_long, 1, 1, __LINE__);
    expect_failure("b: 1\n" + too_long + ": 1\n", ReadStatus::string_too_long, 2, 1, __LINE__);
    expect_failure("a: {" + too_long + ": 1}\n", ReadStatus::string_too_long, 1, 5, __LINE__);
    // An escape counts by the bytes it decodes to.
    CHECK(
        find_entry(
            read_ok("{\"a\": \"" + std::string(max_string_bytes - 2, 'x') + "\\u00e9\"}"), "a"
        )
            ->text.size() == max_string_bytes
    );
    expect_failure(
        "{\"a\": \"" + std::string(max_string_bytes - 1, 'x') + "\\u00e9\"}",
        ReadStatus::string_too_long,
        1,
        7,
        __LINE__
    );
}

/// Checks exact decimals: places, significant digits and exponents.
void test_decimals() {
    expect_number("0", 0, 0);
    expect_number("-0", 0, 0);
    expect_number("0.00", 0, 2);
    expect_number("1.50", 150, 2);
    expect_number("-12.5", -125, 1);
    expect_number("2.5e2", 250, 0);
    expect_number("1.50e1", 150, 1);
    expect_number("15e-1", 15, 1);
    expect_number("0e5", 0, 0);
    expect_number("0e-18", 0, 18);
    expect_number("1e-18", 1, 18);
    expect_number("1e17", 100000000000000000, 0);
    expect_number("999999999999999999", 999999999999999999, 0);
    expect_number("-999999999999999999", -999999999999999999, 0);
    expect_number("0.123456789012345678", 123456789012345678, 18);
    expect_number("0.000000000000000001", 1, 18);
    expect_number("1E+2", 100, 0);
    expect_number("1e00000000000000000000000000002", 100, 0);
    // Leading zeros count for nothing in YAML; JSON refuses them.
    const Node padded{read_ok("a: 000000000000000000000000000.50\nb: 0007\n")};
    CHECK(
        find_entry(padded, "a")->number.mantissa == 50 &&
        find_entry(padded, "a")->number.places == 2
    );
    CHECK(
        find_entry(padded, "b")->number.mantissa == 7 && find_entry(padded, "b")->number.places == 0
    );
    for (const std::string_view text :
         {"1e18",
          "1000000000000000000",
          "0.1234567890123456789",
          "1e-19",
          "0e-19",
          "1e999999999999999999999",
          "12345678901234567.89",
          "1e-999999999999999999999"}) {
        expect_failure(
            "{\"n\": " + std::string{text} + "}", ReadStatus::number_out_of_range, 1, 7, __LINE__
        );
        expect_failure(
            "n: " + std::string{text} + "\n", ReadStatus::number_out_of_range, 1, 4, __LINE__
        );
    }

    CHECK(decimal_text(Decimal{0, 0}) == "0");
    CHECK(decimal_text(Decimal{0, 3}) == "0.000");
    CHECK(decimal_text(Decimal{5, 3}) == "0.005");
    CHECK(decimal_text(Decimal{-5, 3}) == "-0.005");
    CHECK(decimal_text(Decimal{-1250, 2}) == "-12.50");
    CHECK(decimal_text(Decimal{1250, 0}) == "1250");
    CHECK(decimal_text(Decimal{123, 3}) == "0.123");
    CHECK(decimal_text(Decimal{1234, 3}) == "1.234");
    CHECK(decimal_text(Decimal{INT64_MIN, 0}) == "-9223372036854775808");
    CHECK(decimal_text(Decimal{INT64_MAX, 18}) == "9.223372036854775807");

    CHECK(compare_decimals(Decimal{150, 2}, Decimal{15, 1}) == 0);
    CHECK(compare_decimals(Decimal{0, 0}, Decimal{0, 5}) == 0);
    CHECK(compare_decimals(Decimal{1, 0}, Decimal{2, 0}) < 0);
    CHECK(compare_decimals(Decimal{2, 0}, Decimal{1, 0}) > 0);
    CHECK(compare_decimals(Decimal{-2, 0}, Decimal{1, 0}) < 0);
    CHECK(compare_decimals(Decimal{-2, 0}, Decimal{-1, 0}) < 0);
    CHECK(compare_decimals(Decimal{-15, 1}, Decimal{-150, 2}) == 0);
    CHECK(compare_decimals(Decimal{999, 3}, Decimal{1, 0}) < 0);
    CHECK(compare_decimals(Decimal{1001, 3}, Decimal{1, 0}) > 0);
    CHECK(compare_decimals(Decimal{1, 18}, Decimal{0, 0}) > 0);
    CHECK(compare_decimals(Decimal{0, 0}, Decimal{-1, 18}) > 0);
    CHECK(compare_decimals(Decimal{INT64_MAX, 0}, Decimal{INT64_MIN, 0}) > 0);
    CHECK(compare_decimals(Decimal{INT64_MIN, 0}, Decimal{INT64_MIN, 0}) == 0);
    CHECK(compare_decimals(Decimal{1, 4000000000u}, Decimal{1, 3999999999u}) < 0);
    CHECK(compare_decimals(Decimal{10, 1}, Decimal{1, 0}) == 0);
    CHECK(compare_decimals(Decimal{30000, 3}, Decimal{3000, 0}) < 0);
    CHECK(compare_decimals(Decimal{3000001, 3}, Decimal{3000, 0}) > 0);
}

/// Checks the private decimal assembly directly, including exponents that
/// saturate.
void test_assembly() {
    using oa::formats::oascript::detail::assemble_decimal;
    using oa::formats::oascript::detail::NumberParts;
    Decimal value{7, 7};
    CHECK(assemble_decimal(NumberParts{false, "", "", false, ""}, value) == ReadStatus::ok);
    CHECK(value.mantissa == 0 && value.places == 0);
    CHECK(assemble_decimal(NumberParts{true, "12", "34", true, "2"}, value) == ReadStatus::ok);
    CHECK(value.mantissa == -1234 && value.places == 4);
    value = Decimal{7, 7};
    CHECK(
        assemble_decimal(NumberParts{false, "1", "", false, "99999999999999999999"}, value) ==
        ReadStatus::number_out_of_range
    );
    CHECK(value.mantissa == 7 && value.places == 7);
}

/// Checks the writers' exact bytes for a fixed tree.
void test_writer_golden() {
    const Node root{read_ok(R"(name: game.rec
count: 3
ratio: 1.50
on: true
none: null
"needs quotes": "yes"
point: { x: 1, y: -2.5 }
list: [a, "b c", 3]
empty map: {}
empty list: []
nested:
  deeper:
    - k: v
      more: [1, 2]
    - - x
      - y
    - {}
  wide: [aaaaaaaaaa, bbbbbbbbbb, cccccccccc, dddddddddd, eeeeeeeeee, ffffffffff, gggggggggg, hhhhhhhhhh]
)")};
    const std::string yaml{write_document(root, DocumentForm::yaml)};
    const std::string_view expected_yaml{R"(name: game.rec
count: 3
ratio: 1.50
"on": true
none: null
needs quotes: "yes"
point: { x: 1, y: -2.5 }
list: [a, b c, 3]
empty map: {}
empty list: []
nested:
  deeper:
    - k: v
      more: [1, 2]
    - [x, y]
    - {}
  wide:
    - aaaaaaaaaa
    - bbbbbbbbbb
    - cccccccccc
    - dddddddddd
    - eeeeeeeeee
    - ffffffffff
    - gggggggggg
    - hhhhhhhhhh
)"};
    CHECK(yaml == expected_yaml);
    if (yaml != expected_yaml)
        std::fprintf(stderr, "YAML written:\n%s", yaml.c_str());
    const std::string json{write_document(root, DocumentForm::json)};
    const std::string_view expected_json{R"({
  "name": "game.rec",
  "count": 3,
  "ratio": 1.50,
  "on": true,
  "none": null,
  "needs quotes": "yes",
  "point": {"x": 1, "y": -2.5},
  "list": ["a", "b c", 3],
  "empty map": {},
  "empty list": [],
  "nested": {
    "deeper": [
      {
        "k": "v",
        "more": [1, 2]
      },
      ["x", "y"],
      {}
    ],
    "wide": [
      "aaaaaaaaaa",
      "bbbbbbbbbb",
      "cccccccccc",
      "dddddddddd",
      "eeeeeeeeee",
      "ffffffffff",
      "gggggggggg",
      "hhhhhhhhhh"
    ]
  }
}
)"};
    CHECK(json == expected_json);
    if (json != expected_json)
        std::fprintf(stderr, "JSON written:\n%s", json.c_str());
    CHECK(same_tree(read_ok(yaml), root));
    CHECK(same_tree(read_ok(json), root));
    CHECK(write_document(Node{NodeKind::mapping}, DocumentForm::yaml) == "{}\n");
    CHECK(write_document(Node{NodeKind::mapping}, DocumentForm::json) == "{}\n");
    CHECK(same_tree(read_ok("{}\n"), Node{NodeKind::mapping}));
}

/// Returns a mapping entry holding a string.
///
/// @param key the key
/// @param text the string
/// @return the entry
Node string_entry(std::string key, std::string text) {
    Node node{};
    node.kind = NodeKind::string;
    node.key = std::move(key);
    node.text = std::move(text);
    return node;
}

/// Strings that need care in YAML: ones that look like other scalars, hold
/// indicators, white space, escapes or control characters.
const std::vector<std::string>& tricky_strings() {
    static const std::vector<std::string> strings{
        "",
        " ",
        "a ",
        " a",
        "null",
        "Null",
        "~",
        "true",
        "FALSE",
        "yes",
        "No",
        "on",
        "OFF",
        "y",
        "1",
        "-1",
        "1.5",
        ".5",
        "1e3",
        "007",
        "0x1F",
        "0o7",
        ".inf",
        "-.inf",
        ".nan",
        "1e999",
        "-",
        "- a",
        "-a",
        "?",
        "? a",
        ":",
        "a:",
        "a: b",
        "a:b",
        "#",
        "a #b",
        "a#b",
        "&a",
        "*a",
        "!a",
        "|",
        ">",
        "'",
        "\"",
        "%",
        "@",
        "`",
        ",",
        "[",
        "]",
        "{",
        "}",
        "a,b",
        "[a]",
        "{a}",
        "---",
        "...",
        "... a",
        "--- a",
        "a\tb",
        "a\nb",
        "a\rb",
        std::string{"a\0b", 3},
        "\x01",
        "\x1F",
        "\x7F",
        "back\\slash",
        "\"quoted\"",
        "it's",
        "caf\xC3\xA9",
        "\xE2\x82\xAC",
        "\xF0\x9F\x98\x80",
        "\xC2\x85",
        "\xE2\x80\xA8",
        "plain words",
        "C:/path/file.rec",
        "game.rec",
        "x=1;y=2",
        "(a)",
        "$HOME",
        "<tag>",
        "a^b",
        "+",
        "+1",
        "=",
        "<<",
        "1_000",
        "0b1",
        "2nd",
        "\xEF\xBB\xBF",
        "a\xC2\x9F"
        "b",
        "\xC2\xA0",
        "\xE2\x80\xA9",
    };
    return strings;
}

/// Checks that every tricky string survives a round trip as a key and as a
/// value, in both forms.
void test_round_trip_strings() {
    Node root{};
    root.kind = NodeKind::mapping;
    Node flow{};
    flow.kind = NodeKind::sequence;
    flow.key = "in flow";
    Node flow_map{};
    flow_map.kind = NodeKind::mapping;
    flow_map.key = "in flow mapping";
    size_t index{};
    for (const std::string& text : tricky_strings()) {
        root.children.push_back(string_entry(text, text));
        flow.children.push_back(string_entry(std::string{}, text));
        flow_map.children.push_back(string_entry(text, "v" + std::to_string(index++)));
    }
    // Keep the flow collections short enough to be written in flow style.
    Node flows{};
    flows.kind = NodeKind::mapping;
    flows.key = "flows";
    for (size_t first{}; first < flow.children.size(); first += 4) {
        Node part{};
        part.kind = NodeKind::sequence;
        part.key = "s" + std::to_string(first);
        Node part_map{};
        part_map.kind = NodeKind::mapping;
        part_map.key = "m" + std::to_string(first);
        for (size_t item{first}; item < first + 4 && item < flow.children.size(); ++item) {
            part.children.push_back(flow.children[item]);
            part_map.children.push_back(flow_map.children[item]);
        }
        flows.children.push_back(std::move(part));
        flows.children.push_back(std::move(part_map));
    }
    root.children.push_back(std::move(flows));
    for (const DocumentForm form : {DocumentForm::yaml, DocumentForm::json}) {
        const std::string text{write_document(root, form)};
        CHECK(detect_form(bytes_of(text)) == form);
        const Node read{read_ok(text)};
        CHECK(same_tree(read, root));
        CHECK(write_document(read, form) == text);
        if (!same_tree(read, root))
            std::fprintf(stderr, "round trip failed for:\n%s", text.c_str());
    }
    // The strings written plain are the ones that need no quotes.
    const std::string yaml{write_document(root, DocumentForm::yaml)};
    CHECK(yaml.find("\ngame.rec: game.rec\n") != std::string::npos);
    CHECK(yaml.find("\n\"yes\": \"yes\"\n") != std::string::npos);
    CHECK(yaml.find("\n\"1.5\": \"1.5\"\n") != std::string::npos);
    CHECK(yaml.find("\n\"a\\tb\": \"a\\tb\"\n") != std::string::npos);
    CHECK(yaml.find("\nplain words: plain words\n") != std::string::npos);
    CHECK(yaml.find("\n\"<<\": \"<<\"\n") != std::string::npos);
    CHECK(yaml.find("\n\"1_000\": \"1_000\"\n") != std::string::npos);
    CHECK(yaml.find("\ncaf\xC3\xA9: caf\xC3\xA9\n") != std::string::npos);
    CHECK(yaml.find("\n\xC2\xA0: \xC2\xA0\n") != std::string::npos);
    // Characters YAML readers may take for line breaks are escaped.
    CHECK(yaml.find("\n\"\\u0085\": \"\\u0085\"\n") != std::string::npos);
    CHECK(yaml.find("\n\"\\u2028\": \"\\u2028\"\n") != std::string::npos);
    CHECK(yaml.find("\n\"\\ufeff\": \"\\ufeff\"\n") != std::string::npos);
    CHECK(yaml.find("\n\"a\\u009fb\": \"a\\u009fb\"\n") != std::string::npos);
}

/// The state of the fixed-seed generator the random tests draw from.
struct Random {
    uint64_t state{};
};

/// Draws the next value of a linear congruential generator.
///
/// @param[in,out] random the generator
/// @param bound the number of values
/// @return a value below `bound`
uint64_t draw(Random& random, uint64_t bound) {
    random.state = random.state * 6364136223846793005u + 1442695040888963407u;
    return (random.state >> 33) % bound;
}

/// Builds a random tree of every node kind.
///
/// @param[in,out] random the generator
/// @param depth the node's depth, 1 at the top
/// @param kind the kind of the node, or a random one when not a collection
/// @return the node
Node random_node(Random& random, uint32_t depth, NodeKind kind) {
    Node node{};
    node.kind = kind;
    switch (kind) {
    case NodeKind::null_value:
        break;
    case NodeKind::boolean:
        node.boolean = draw(random, 2) == 1;
        break;
    case NodeKind::number: {
        uint64_t magnitude{};
        const uint64_t digits{draw(random, max_significant_digits + 1)};
        for (uint64_t digit{}; digit < digits; ++digit)
            magnitude = magnitude * 10 + draw(random, 10);
        const int64_t signed_value{static_cast<int64_t>(magnitude)};
        node.number = Decimal{
            draw(random, 2) == 1 ? -signed_value : signed_value,
            static_cast<uint32_t>(draw(random, max_decimal_places + 1))
        };
        break;
    }
    case NodeKind::string: {
        const std::vector<std::string>& strings{tricky_strings()};
        if (draw(random, 2) == 0) {
            node.text = strings[draw(random, strings.size())];
        } else {
            const uint64_t length{draw(random, 12)};
            for (uint64_t index{}; index < length; ++index)
                node.text.push_back(static_cast<char>(' ' + draw(random, 95)));
        }
        break;
    }
    case NodeKind::mapping:
    case NodeKind::sequence: {
        const uint64_t count{depth >= 6 ? 0 : draw(random, 6)};
        for (uint64_t index{}; index < count; ++index) {
            const NodeKind child_kind{static_cast<NodeKind>(draw(random, 6))};
            Node child{random_node(random, depth + 1, child_kind)};
            if (kind == NodeKind::mapping) {
                const std::vector<std::string>& strings{tricky_strings()};
                child.key = strings[draw(random, strings.size())] + "#" + std::to_string(index);
                if (draw(random, 4) == 0)
                    child.key = std::to_string(index);
            }
            node.children.push_back(std::move(child));
        }
        break;
    }
    }
    return node;
}

/// Checks round trips of random trees in both forms.
void test_round_trip_random() {
    Random random{20260930};
    for (int round{}; round < 400; ++round) {
        const Node root{random_node(random, 1, NodeKind::mapping)};
        for (const DocumentForm form : {DocumentForm::yaml, DocumentForm::json}) {
            const std::string text{write_document(root, form)};
            Node read{};
            ReadError error{};
            const bool ok{read_document(bytes_of(text), read, error)};
            CHECK(ok && same_tree(read, root));
            if (!ok || !same_tree(read, root)) {
                std::fprintf(
                    stderr,
                    "round %d failed (%s at %u:%u):\n%s",
                    round,
                    read_status_message(error.status),
                    error.position.line,
                    error.position.column,
                    text.c_str()
                );
                return;
            }
            CHECK(write_document(read, form) == text);
        }
    }
}

/// Checks that a read either succeeds, and then round-trips, or fails with a
/// status and a position.
///
/// @param text the text
void check_sweep_case(std::string_view text) {
    Node root{};
    ReadError error{};
    if (read_document(bytes_of(text), root, error)) {
        CHECK(error.status == ReadStatus::ok && root.kind == NodeKind::mapping);
        for (const DocumentForm form : {DocumentForm::yaml, DocumentForm::json}) {
            Node again{};
            ReadError again_error{};
            CHECK(
                read_document(bytes_of(write_document(root, form)), again, again_error) &&
                same_tree(again, root)
            );
        }
        return;
    }
    CHECK(error.status != ReadStatus::ok);
    CHECK(root.kind == NodeKind::null_value && root.children.empty());
    CHECK(error.position.line >= 1 && error.position.column >= 1);
}

/// Feeds every truncation of the example scripts, and damaged copies under
/// a fixed seed, to the reader.
void test_malformed_sweep() {
    for (const std::string_view example : {example_yaml, example_json}) {
        for (size_t length{}; length <= example.size(); ++length)
            check_sweep_case(example.substr(0, length));
    }
    Random random{17};
    for (int round{}; round < 6000; ++round) {
        std::string text{round % 2 == 0 ? example_yaml : example_json};
        const uint64_t flips{1 + draw(random, 4)};
        for (uint64_t flip{}; flip < flips; ++flip) {
            const size_t offset{static_cast<size_t>(draw(random, text.size()))};
            // Mostly bytes the grammars care about, sometimes any byte.
            constexpr std::string_view interesting{" \t\n\r:-,[]{}#&*!|>'\"%@`?\\0x.e"};
            text[offset] = draw(random, 4) == 0 ? static_cast<char>(draw(random, 256))
                                                : interesting[draw(random, interesting.size())];
        }
        check_sweep_case(text);
    }
}

} // namespace

int main() {
    test_detect_form();
    test_equivalence();
    test_json_values();
    test_yaml_values();
    test_statuses();
    test_limits();
    test_decimals();
    test_assembly();
    test_writer_golden();
    test_round_trip_strings();
    test_round_trip_random();
    test_malformed_sweep();
    return oa::formats::oascript::test::finish("formats-oascript-document");
}
