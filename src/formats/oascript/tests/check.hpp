// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Checks and tree comparison shared by the director script tests.
#pragma once

#include "oa/formats/oascript/document.hpp"

#include <cstdio>
#include <cstdlib>
#include <span>
#include <string>
#include <string_view>

namespace oa::formats::oascript::test {

inline int failures = 0;
inline int checks = 0;

/// Counts one check and reports it with its file and line when it fails.
///
/// @param condition the result
/// @param expression the checked expression's text
/// @param file the test's source file
/// @param line the check's line
inline void check(bool condition, const char* expression, const char* file, int line) {
    ++checks;
    if (!condition) {
        ++failures;
        std::fprintf(stderr, "%s:%d: check failed: %s\n", file, line, expression);
    }
}

/// Prints the totals of a test program.
///
/// @param name the program's name
/// @return the program's exit status: success when no check failed
inline int finish(const char* name) {
    std::printf("%s: %d checks, %d failures\n", name, checks, failures);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

/// Views a string's bytes.
///
/// @param text the string
/// @return its bytes
inline std::span<const uint8_t> bytes_of(std::string_view text) {
    return std::span<const uint8_t>{reinterpret_cast<const uint8_t*>(text.data()), text.size()};
}

/// Compares two trees by kind, key and value, ignoring positions.
///
/// @param left the first tree
/// @param right the second tree
/// @return true when they hold the same document
inline bool same_tree(const Node& left, const Node& right) {
    if (left.kind != right.kind || left.key != right.key ||
        left.children.size() != right.children.size())
        return false;
    switch (left.kind) {
    case NodeKind::boolean:
        if (left.boolean != right.boolean)
            return false;
        break;
    case NodeKind::number:
        if (left.number.mantissa != right.number.mantissa ||
            left.number.places != right.number.places)
            return false;
        break;
    case NodeKind::string:
        if (left.text != right.text)
            return false;
        break;
    case NodeKind::null_value:
    case NodeKind::mapping:
    case NodeKind::sequence:
        break;
    }
    for (size_t index{}; index < left.children.size(); ++index) {
        if (!same_tree(left.children[index], right.children[index]))
            return false;
    }
    return true;
}

} // namespace oa::formats::oascript::test

#define CHECK(expr)                                                                                \
    ::oa::formats::oascript::test::check(static_cast<bool>(expr), #expr, __FILE__, __LINE__)
