// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The OTA parser on a valid map description and on one malformed input per
// error code, each checked for its code and the offset it reports.

#include "oa/formats/ota.hpp"

#include <cstddef>
#include <cstdio>
#include <string>
#include <string_view>

namespace {

using oa::formats::ota::ErrorCode;
namespace limit = oa::formats::ota::limit;

int failures = 0;

/// Records a failed check.
///
/// @param line source line of the check
/// @param what text of the failed condition
void report_failure(int line, const char* what) {
    std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, line, what);
    ++failures;
}

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            report_failure(__LINE__, #condition);                                                  \
        }                                                                                          \
    } while (false)

/// Reports whether parsing fails with a code at an offset.
///
/// @param text the OTA text
/// @param code the error code expected
/// @param offset the byte offset expected
/// @return true when the parse fails so
bool fails_with(std::string_view text, ErrorCode code, std::size_t offset) {
    const auto result = oa::formats::ota::parse(text);
    if (result.ok() || !result.error)
        return false;
    if (result.error->code != code || result.error->offset != offset) {
        std::fprintf(
            stderr,
            "  got code %d at %zu (%s)\n",
            static_cast<int>(result.error->code),
            result.error->offset,
            result.error->message.c_str()
        );
        return false;
    }
    return !result.error->message.empty();
}

/// Builds a GlobalHeader holding one Network 1 schema with `count` start
/// positions, the first of them at (x, z).
///
/// @param count start positions in the schema
/// @param x first start position's XPos
/// @param z first start position's ZPos
/// @return the OTA text
std::string schema_with_starts(std::size_t count, long x, long z) {
    std::string text = "[GlobalHeader]{[Schema 0]{Type=Network 1;[specials]{";
    for (std::size_t i = 0; i < count; ++i) {
        text += "[special" + std::to_string(i) + "]{specialwhat=StartPos" + std::to_string(i + 1) +
                ";XPos=" + std::to_string(i == 0 ? x : 0) +
                ";ZPos=" + std::to_string(i == 0 ? z : 0) + ";}";
    }
    return text + "}}}";
}

void test_valid() {
    const auto result = oa::formats::ota::parse(
        "// a map\n"
        "[GLOBALHEADER]\n"
        "{\n"
        "  missionname=Coast To Coast;\n"
        "  /* a comment\n"
        "     over two lines */\n"
        "  missiondescription=Two islands;\n"
        "  planet=Archipelago;\n"
        "  memory=32;\n"
        "  size=10 x 10;\n"
        "  numplayers=2, 3;\n"
        "  [Schema 0]\n"
        "  {\n"
        "    Type=Network 1;\n"
        "    [specials]\n"
        "    {\n"
        "      [special0]{specialwhat=StartPos2;XPos=-32768;ZPos=32767;}\n"
        "      [special1]{specialwhat=startpos;XPos=+5;ZPos=junk;}\n"
        "    }\n"
        "  }\n"
        "  [Schema 1]{Type=Network 2;}\n"
        "}\n"
    );
    CHECK(result.ok() && !result.error);
    if (!result.ok())
        return;
    const auto& map = *result.metadata;
    CHECK(map.mission_name == "Coast To Coast");
    CHECK(map.mission_description == "Two islands");
    CHECK(map.planet == "Archipelago");
    CHECK(map.memory_requirement == "32");
    CHECK(map.map_size == "10 x 10");
    CHECK(map.permitted_player_counts == "2, 3");
    CHECK(map.schemas.size() == 2);
    if (map.schemas.size() != 2)
        return;
    const auto& starts = map.schemas[0].start_positions;
    CHECK(map.schemas[0].number == 0 && map.schemas[0].type == "Network 1");
    CHECK(starts.size() == 2);
    if (starts.size() == 2) {
        // StartPos2 is index 1; the record without a number takes the next
        // implicit one, 1, so index 0. An unreadable ZPos is zero.
        CHECK(starts[0].index == 1 && starts[0].x == -32768 && starts[0].z == 32767);
        CHECK(starts[1].index == 0 && starts[1].x == 5 && starts[1].z == 0);
    }
    CHECK(map.schemas[1].number == 1 && map.schemas[1].start_positions.empty());
    CHECK(oa::formats::ota::select_multiplayer_schema(map, 2) == &map.schemas[0]);

    // Exactly at each limit still parses.
    CHECK(oa::formats::ota::parse(schema_with_starts(limit::start_positions, 0, 0)).ok());
    const std::string longest(limit::value_bytes, 'v');
    CHECK(oa::formats::ota::parse("[GlobalHeader]{missionname=" + longest + ";}").ok());
    std::string deepest = "[GlobalHeader]";
    for (std::size_t depth = 1; depth < limit::nesting; ++depth)
        deepest += "{[n]";
    deepest += "{" + std::string(limit::nesting, '}');
    CHECK(oa::formats::ota::parse(deepest).ok());
}

void test_limits() {
    // One byte over the input limit is refused before parsing.
    std::string oversized(limit::input_bytes + 1, ' ');
    oversized.replace(0, 17, "[GlobalHeader]{};");
    CHECK(fails_with(oversized, ErrorCode::input_limit, 0));

    // One section nested past the limit is refused at its opening bracket.
    std::string nested = "[GlobalHeader]";
    for (std::size_t depth = 1; depth <= limit::nesting; ++depth)
        nested += "{[n]";
    const std::size_t deepest_bracket = nested.rfind('[');
    nested += "{" + std::string(limit::nesting + 1, '}');
    CHECK(fails_with(nested, ErrorCode::nesting_limit, deepest_bracket));

    // One section more than the limit, at the first one past it.
    std::string many = "[GlobalHeader]{}";
    std::size_t extra_section = 0;
    for (std::size_t section = 1; section <= limit::sections; ++section) {
        if (section == limit::sections)
            extra_section = many.size();
        many += "[s]{}";
    }
    CHECK(fails_with(many, ErrorCode::section_limit, extra_section));

    // A key or a value one byte over the limit, reported after its ';'.
    const std::string long_text(limit::value_bytes + 1, 'v');
    const std::string long_value = "[GlobalHeader]{missionname=" + long_text + ";}";
    CHECK(fails_with(long_value, ErrorCode::value_limit, long_value.size() - 1));
    const std::string long_key = "[GlobalHeader]{" + long_text + "=1;}";
    CHECK(fails_with(long_key, ErrorCode::value_limit, long_key.size() - 1));

    // One start position more than the limit.
    CHECK(fails_with(
        schema_with_starts(limit::start_positions + 1, 0, 0), ErrorCode::start_position_limit, 0
    ));

    // A start position outside the signed 16-bit field, on either axis.
    CHECK(fails_with(schema_with_starts(1, 32768, 0), ErrorCode::number_out_of_range, 0));
    CHECK(fails_with(schema_with_starts(1, 0, -32769), ErrorCode::number_out_of_range, 0));
}

void test_malformed() {
    // An unterminated comment, at the comment's start.
    CHECK(fails_with("[GlobalHeader]{} /* open", ErrorCode::malformed, 17));
    // Text that is not a section, at the text.
    CHECK(fails_with("[GlobalHeader]{} junk", ErrorCode::malformed, 17));
    // An unterminated section name, just after its '['.
    CHECK(fails_with("[GlobalHeader", ErrorCode::malformed, 1));
    // A section name with no body, just after the character found instead.
    CHECK(fails_with("[GlobalHeader] x", ErrorCode::malformed, 16));
    // An unterminated body, at the end of the text.
    CHECK(fails_with("[GlobalHeader]{ a=1;", ErrorCode::malformed, 20));
    // An unterminated value, at the end of the text.
    CHECK(fails_with("[GlobalHeader]{ a=1", ErrorCode::malformed, 19));
    // A property with no '=', where the key stopped.
    CHECK(fails_with("[GlobalHeader]{ a }", ErrorCode::malformed, 18));
    // No GlobalHeader section.
    CHECK(fails_with("[Other]{a=1;}", ErrorCode::malformed, 0));
    CHECK(fails_with("", ErrorCode::malformed, 0));
}

} // namespace

int main() {
    test_valid();
    test_limits();
    test_malformed();
    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    return 0;
}
