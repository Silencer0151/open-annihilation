// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/formats/cob.hpp"

#include <array>
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void put32(std::vector<uint8_t>& bytes, std::size_t offset, uint32_t value) {
    for (unsigned shift = 0; shift != 32; shift += 8) {
        bytes[offset + shift / 8] = static_cast<uint8_t>(value >> shift);
    }
}

std::vector<uint8_t> fixture() {
    // v4 header, 3 code words, two scripts, one piece, and an absolute name
    // pool, in the section order of the game's COB files.
    std::vector<uint8_t> bytes(95, 0);
    const std::array<uint32_t, 11> header{4, 2, 1, 3, 5, 0, 56, 64, 72, 44, 76};
    for (std::size_t i = 0; i < header.size(); ++i)
        put32(bytes, i * 4, header[i]);
    put32(bytes, 44, 0x1000000U);
    put32(bytes, 48, 0x1000001U);
    put32(bytes, 52, 0xFFFFFFFFU);
    put32(bytes, 56, 0);
    put32(bytes, 60, 2);
    put32(bytes, 64, 76);
    put32(bytes, 68, 82);
    put32(bytes, 72, 87);
    const char names[] = "start\0stop\0root\0";
    std::copy(std::begin(names), std::end(names), bytes.begin() + static_cast<std::ptrdiff_t>(76));
    return bytes;
}

void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
} // namespace

int main() {
    try {
        auto bytes = fixture();
        const auto parsed = oa::formats::cob::parse_cob(bytes);
        require(static_cast<bool>(parsed), "valid v4 COB rejected");
        require(parsed.program->header.static_variable_count == 5, "static variable count");
        require(
            parsed.program->code.size() == 3 && parsed.program->code[0] == 0x1000000U, "code words"
        );
        require(
            parsed.program->entry_points.size() == 2 && parsed.program->entry_points[1] == 2,
            "script entry words"
        );
        require(
            parsed.program->scripts[0].name == "start" &&
                parsed.program->scripts[1].name == "stop" &&
                parsed.program->piece_names[0] == "root",
            "absolute names"
        );

        auto kingdom = bytes;
        put32(kingdom, 0, 6);
        require(!oa::formats::cob::parse_cob(kingdom), "Kingdoms v6 must be rejected");
        auto bad_entry = bytes;
        put32(bad_entry, 56, 3);
        require(!oa::formats::cob::parse_cob(bad_entry), "out-of-range entry accepted");
        auto bad_name = bytes;
        put32(bad_name, 64, 95);
        require(!oa::formats::cob::parse_cob(bad_name), "unterminated name accepted");
        auto unlimited_name = oa::formats::cob::ParseLimits{};
        unlimited_name.max_name_bytes = static_cast<std::size_t>(-1);
        require(
            static_cast<bool>(oa::formats::cob::parse_cob(bytes, unlimited_name)),
            "maximum name limit rejected valid name"
        );
        auto low_static_limit = oa::formats::cob::ParseLimits{};
        low_static_limit.max_static_variables = 4;
        require(
            !oa::formats::cob::parse_cob(bytes, low_static_limit),
            "static-variable bound was not enforced"
        );
        std::cout << "COB v4 parser tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
