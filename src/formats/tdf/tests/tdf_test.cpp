// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/formats/tdf.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>

using namespace oa::formats::tdf;

namespace {

int failures = 0;

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            std::fprintf(                                                                          \
                stderr, "%s:%d: %s: CHECK(%s) failed\n", __FILE__, __LINE__, __func__, #condition  \
            );                                                                                     \
            ++failures;                                                                            \
        }                                                                                          \
    } while (false)

struct Parsed {
    Document document{};
    ParseError error{};
    bool ok = false;

    explicit Parsed(const char* text) {
        document_init(&document);
        ok = parse_text(&document, text, static_cast<uint32_t>(std::strlen(text)), false, &error);
    }

    ~Parsed() { document_free(&document); }

    Parsed(const Parsed&) = delete;
    Parsed& operator=(const Parsed&) = delete;

    [[nodiscard]] const Block* section(const char* name) const {
        return find_child(document.root, name);
    }
};

bool value_is(const Block* block, const char* key, const char* expected) {
    const char* value = find_value(block, key);
    return value != nullptr && std::strcmp(value, expected) == 0;
}

void tdf_parses_nested_blocks_and_trims_tokens() {
    Parsed parsed("[ UNITINFO ]\n{\n\tUnitName = ARMCOM ;\n\t[ SFX ] { select1 = foo ; }\n}\n");
    CHECK(parsed.ok);
    const Block* unit = parsed.section("unitinfo");
    CHECK(unit != nullptr);
    if (unit == nullptr)
        return;
    CHECK(std::strcmp(unit->name, "UNITINFO") == 0);
    CHECK(value_is(unit, "UNITNAME", "ARMCOM"));
    CHECK(value_is(find_child(unit, "sfx"), "select1", "foo"));
    CHECK(child_count(parsed.document.root) == 1);
}

void tdf_later_duplicate_key_wins() {
    Parsed parsed("[A]{x=1; x=2; Name=a; NAME=b; name=c;}");
    CHECK(parsed.ok);
    const Block* block = parsed.section("A");
    CHECK(value_is(block, "x", "2"));
    CHECK(value_is(block, "name", "c"));
    // Case variants keep separate slots (byte-exact key match on insert).
    CHECK(property_count(block) == 4);
}

void tdf_duplicate_section_first_wins() {
    Parsed parsed("[A]{v=first;} [a]{v=second;}");
    CHECK(parsed.ok);
    CHECK(child_count(parsed.document.root) == 2);
    CHECK(value_is(parsed.section("A"), "v", "first"));
}

void tdf_stray_root_brace_stops_parse() {
    Parsed parsed("[A]{v=1;}\n}\n[B]{v=2;}");
    CHECK(parsed.ok);
    CHECK(parsed.section("A") != nullptr);
    CHECK(parsed.section("B") == nullptr);
}

void tdf_value_runs_to_next_semicolon_across_lines() {
    Parsed parsed("[A]{a=1\nb=2;c=3;}");
    CHECK(parsed.ok);
    const Block* block = parsed.section("A");
    CHECK(value_is(block, "a", "1\nb=2"));
    CHECK(find_value(block, "b") == nullptr);
    CHECK(value_is(block, "c", "3"));
}

void tdf_line_comment_inside_value_is_stripped() {
    Parsed parsed("[A]{url=http://x;\nb=2;}");
    CHECK(parsed.ok);
    const Block* block = parsed.section("A");
    const char* url = find_value(block, "url");
    CHECK(url != nullptr && std::strncmp(url, "http:", 5) == 0);
    CHECK(url != nullptr && std::strstr(url, "b=2") != nullptr);
    CHECK(find_value(block, "b") == nullptr);
}

void tdf_block_comment_rules() {
    char text[] = "a/*b*/c/*/d*/e//f\ng/* hij";
    strip_comments(text);
    CHECK(std::strcmp(text, "a     c      e   \ng     j") == 0);
}

void tdf_missing_delimiters_are_errors() {
    CHECK(!Parsed("[A]{x}").ok);
    Parsed equals("[A]{x}");
    CHECK(equals.error.status == ParseStatus::equals_missing);
    Parsed semicolon("[A]{x=1}");
    CHECK(!semicolon.ok && semicolon.error.status == ParseStatus::semicolon_missing);
    Parsed close("[A{x=1;}");
    CHECK(!close.ok && close.error.status == ParseStatus::sub_record_close_missing);
    Parsed open("[A] x=1;");
    CHECK(!open.ok && open.error.status == ParseStatus::sub_record_open_missing);
    Parsed end("[A]{x=1;");
    CHECK(!end.ok && end.error.status == ParseStatus::unexpected_end);
}

void tdf_inline_empty_sections() {
    Parsed parsed("[ARMLLT] {}\r\n[ARMVP] {}\r\n[ARMLAB] { x=1; }");
    CHECK(parsed.ok);
    CHECK(child_count(parsed.document.root) == 3);
    CHECK(value_is(parsed.section("armlab"), "x", "1"));
}

void tdf_bare_key_value_at_root() {
    Parsed parsed("key=value;");
    CHECK(parsed.ok);
    CHECK(value_is(parsed.document.root, "key", "value"));
}

void tdf_empty_text_is_empty_root() {
    Parsed parsed("");
    CHECK(parsed.ok);
    CHECK(parsed.document.root != nullptr && child_count(parsed.document.root) == 0);
    if (parsed.document.root == nullptr)
        return;
    CHECK(parsed.document.root->body_hash == 0);
}

void tdf_block_hash_skips_last_two_bytes() {
    Parsed parsed("[A]{x=1;\n}");
    CHECK(parsed.ok);
    const char* body = "x=1;\n";
    const auto expected = buffer_hash(reinterpret_cast<const uint8_t*>(body), 3);
    CHECK(parsed.section("A")->body_hash == expected);
}

void tdf_atoi_parses_like_atol() {
    CHECK(parse_int("12abc") == 12);
    CHECK(parse_int(" \t-7") == -7);
    CHECK(parse_int("+5") == 5);
    CHECK(parse_int("1.9") == 1);
    CHECK(parse_int("0x10") == 0);
    CHECK(parse_int("") == 0);
    CHECK(parse_int("4294967297") == 1); // wraps without overflow checks
}

void tdf_atof_accepts_only_the_decimal_grammar() {
    CHECK(parse_double("1.5d2") == 150.0);
    CHECK(parse_double(".5") == 0.5);
    CHECK(parse_double("5.") == 5.0);
    CHECK(parse_double("  -2.25e1x") == -22.5);
    CHECK(parse_double("inf") == 0.0);
    CHECK(parse_double("0x10") == 0.0);
    CHECK(parse_double("1e") == 1.0);
}

// A key that is present reads its text's number even when the text holds
// none: the fallback is only for a missing key.
void tdf_present_key_without_a_number_reads_zero() {
    Parsed parsed("[A]{empty=; word=Meteor; plus=+5; half=.5; hex=0x10;}");
    const Block* block = parsed.section("A");
    CHECK(get_int(block, "empty", -1) == 0);
    CHECK(get_int(block, "word", -1) == 0);
    CHECK(get_int(block, "plus", -1) == 5);
    CHECK(get_int(block, "hex", -1) == 0);
    CHECK(get_int(block, "missing", -1) == -1);
    CHECK(get_double(block, "empty", 7.0) == 0.0);
    CHECK(get_double(block, "half", 7.0) == 0.5);
    CHECK(get_double(block, "hex", 7.0) == 0.0);
    CHECK(get_double(block, "missing", 7.0) == 7.0);
}

void tdf_fixed_scales_and_wraps_out_of_range() {
    Parsed parsed("[A]{a=1.9; b=-1.9; c=32768; d=1e30; e=0.15;}");
    const Block* block = parsed.section("A");
    CHECK(get_fixed(block, "a", 9) == 124518);
    CHECK(get_fixed(block, "b", 9) == -124518);
    CHECK(get_fixed(block, "c", 9) == INT32_MIN); // 2^31 keeps its low word
    CHECK(get_fixed(block, "d", 9) == 0);         // INT64_MIN low word
    CHECK(get_fixed(block, "e", 9) == 9830);
    CHECK(get_fixed(block, "missing", 9) == 9);
}

void tdf_get_string_bounds_and_defaults() {
    Parsed parsed("[A]{name=abcdef;}");
    const Block* block = parsed.section("A");
    char out[4];
    CHECK(get_string(block, "name", out, sizeof out, ""));
    CHECK(std::strcmp(out, "abc") == 0);
    CHECK(!get_string(block, "none", out, sizeof out, "zz"));
    CHECK(std::strcmp(out, "zz") == 0);
}

void tdf_cursor_selects_relative_to_current_section() {
    Parsed parsed("[A]{[B]{v=1;}} [C]{}");
    Document* document = &parsed.document;
    CHECK(select_section(document, "a"));
    CHECK(select_section(document, "b"));
    CHECK(value_is(cursor(document), "v", "1"));
    CHECK(!select_section(document, "c")); // not a child of B; cursor resets
    CHECK(cursor(document) == nullptr);
    CHECK(step_entry(document, 1));
    CHECK(std::strcmp(cursor(document)->name, "C") == 0);
    reset_cursor(document);
    CHECK(!step_entry(document, 2));
}

void tdf_property_keys_are_in_nocase_order() {
    Parsed parsed("[A]{b=1; A=2; c=3;}");
    const Block* block = parsed.section("A");
    CHECK(std::strcmp(property_key_at(block, 0), "A") == 0);
    CHECK(std::strcmp(property_key_at(block, 2), "c") == 0);
    CHECK(property_key_at(block, 3) == nullptr);
    CHECK(property_key_at(block, -1) == nullptr);
}

void tdf_compare_nocase_folds_to_lower() {
    CHECK(compare_nocase("abc", "ABC") == 0);
    CHECK(compare_nocase("_", "a") < 0); // '_' (0x5f) sorts before folded letters
    CHECK(compare_nocase("Z", "a") > 0);
}

void tdf_owned_document_moves_its_tree() {
    OwnedDocument first;
    CHECK(first.root() == nullptr);
    CHECK(first.parse("[A]{v=1;}"));
    const Block* section = find_child(first.root(), "a");
    OwnedDocument second(std::move(first));
    CHECK(first.root() == nullptr);
    CHECK(find_child(second.root(), "a") == section);
    CHECK(value_is(section, "v", "1"));
    OwnedDocument third;
    CHECK(third.parse("[B]{}"));
    third = std::move(second);
    CHECK(second.root() == nullptr);
    CHECK(find_child(third.root(), "a") == section);
    CHECK(select_section(third.get(), "A"));
}

void tdf_owned_document_reports_failures() {
    OwnedDocument document;
    ParseError error{};
    CHECK(!document.parse("[A]{v=1}", &error));
    CHECK(document.root() == nullptr);
    CHECK(error.status == ParseStatus::semicolon_missing);
    CHECK(describe(error) == "entry lacks its closing ';' at byte 6 in [A]");
    const std::string large(std::size_t{max_input_bytes} + 1u, ' ');
    CHECK(!document.parse(large, &error) && error.status == ParseStatus::too_large);
    CHECK(document.parse(""));
    CHECK(document.root() != nullptr && child_count(document.root()) == 0);
}

void tdf_rejects_excessive_nesting() {
    std::string text;
    for (int i = 0; i < 80; ++i)
        text += "[x]{";
    Parsed parsed(text.c_str());
    CHECK(!parsed.ok && parsed.error.status == ParseStatus::too_deep);
}

} // namespace

int main() {
    tdf_parses_nested_blocks_and_trims_tokens();
    tdf_later_duplicate_key_wins();
    tdf_duplicate_section_first_wins();
    tdf_stray_root_brace_stops_parse();
    tdf_value_runs_to_next_semicolon_across_lines();
    tdf_line_comment_inside_value_is_stripped();
    tdf_block_comment_rules();
    tdf_missing_delimiters_are_errors();
    tdf_inline_empty_sections();
    tdf_bare_key_value_at_root();
    tdf_empty_text_is_empty_root();
    tdf_block_hash_skips_last_two_bytes();
    tdf_atoi_parses_like_atol();
    tdf_atof_accepts_only_the_decimal_grammar();
    tdf_present_key_without_a_number_reads_zero();
    tdf_fixed_scales_and_wraps_out_of_range();
    tdf_get_string_bounds_and_defaults();
    tdf_cursor_selects_relative_to_current_section();
    tdf_property_keys_are_in_nocase_order();
    tdf_compare_nocase_folds_to_lower();
    tdf_owned_document_moves_its_tree();
    tdf_owned_document_reports_failures();
    tdf_rejects_excessive_nesting();
    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::puts("tdf: all checks passed");
    return 0;
}
