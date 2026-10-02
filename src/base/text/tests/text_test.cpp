// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// copy_padded, copy_terminated and append_terminated with text shorter than,
// as long as and longer than the field, and the bytes each leaves after the
// text.

#include "oa/base/text.hpp"
#include "oa/test/check.hpp"

#include <array>
#include <cstring>
#include <span>
#include <string_view>

namespace {

using oa::base::text::append_terminated;
using oa::base::text::copy_padded;
using oa::base::text::copy_terminated;

/// Returns whether a field holds exactly the given bytes.
///
/// @param field the field
/// @param bytes the bytes it should hold, as many as the field is long
template <std::size_t size>
bool holds(const std::array<char, size>& field, std::string_view bytes) {
    return bytes.size() == size && std::memcmp(field.data(), bytes.data(), size) == 0;
}

void padded_copies() {
    std::array<char, 6> field{};
    field.fill('x');
    copy_padded(field.data(), "ab", field.size());
    OA_CHECK(holds(field, std::string_view("ab\0\0\0\0", 6)));

    field.fill('x');
    copy_padded(field.data(), "abcdef", field.size());
    OA_CHECK(holds(field, "abcdef"));

    // Longer text fills the field and is read no further than the field.
    const char unterminated[8] = {'1', '2', '3', '4', '5', '6', '7', '8'};
    field.fill('x');
    copy_padded(field.data(), unterminated, field.size());
    OA_CHECK(holds(field, "123456"));

    // A size short of the field leaves the bytes beyond it.
    field.fill('x');
    copy_padded(field.data(), "abc", 5);
    OA_CHECK(holds(field, std::string_view("abc\0\0x", 6)));

    field.fill('x');
    copy_padded(field.data(), "", 0);
    OA_CHECK(holds(field, "xxxxxx"));
}

void terminated_copies() {
    std::array<char, 6> field{};
    field.fill('x');
    copy_terminated(field, "ab");
    OA_CHECK(holds(field, std::string_view("ab\0xxx", 6)));

    field.fill('x');
    copy_terminated(field, "abcde");
    OA_CHECK(holds(field, std::string_view("abcde\0", 6)));

    field.fill('x');
    copy_terminated(field, "abcdefgh");
    OA_CHECK(holds(field, std::string_view("abcde\0", 6)));

    field.fill('x');
    copy_terminated(std::span(field.data(), 0), "ab");
    OA_CHECK(holds(field, "xxxxxx"));

    field.fill('x');
    copy_terminated(std::span(field.data() + 2, 3), "abc");
    OA_CHECK(holds(field, std::string_view("xxab\0x", 6)));
}

void appends() {
    std::array<char, 8> field{};
    field.fill('x');
    copy_terminated(field, "ab");
    append_terminated(field, "cd");
    OA_CHECK(holds(field, std::string_view("abcd\0xxx", 8)));

    append_terminated(field, "efghij");
    OA_CHECK(holds(field, std::string_view("abcdefg\0", 8)));

    // A full field takes nothing more.
    append_terminated(field, "k");
    OA_CHECK(holds(field, std::string_view("abcdefg\0", 8)));

    // Nor does one whose text has no terminating zero.
    field.fill('x');
    append_terminated(field, "k");
    OA_CHECK(holds(field, "xxxxxxxx"));
}

} // namespace

int main() {
    padded_copies();
    terminated_copies();
    appends();
    return oa::test::check_exit_status();
}
