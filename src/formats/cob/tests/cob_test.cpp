// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/formats/cob.hpp"
#include "oa/base/bytes.hpp"

#include <array>
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void put32(std::vector<uint8_t>& bytes, std::size_t offset, uint32_t value) {
    oa::base::bytes::store_le32(bytes.data() + offset, value);
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

// The fixture with a version 6 header: two more words after the eleven (a
// sound-name table's offset and its count) and every table eight bytes later.
std::vector<uint8_t> extended_header_fixture() {
    const auto plain = fixture();
    constexpr std::size_t extra = 8;
    std::vector<uint8_t> bytes(plain.size() + extra, 0);
    std::copy(plain.begin() + 44, plain.end(), bytes.begin() + 44 + extra);
    const std::array<uint32_t, 13> header{6, 2, 1, 3, 5, 0, 64, 72, 80, 52, 84, 84, 0};
    for (std::size_t i = 0; i < header.size(); ++i)
        put32(bytes, i * 4, header[i]);
    put32(bytes, 72, 84);
    put32(bytes, 76, 90);
    put32(bytes, 80, 95);
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
        require(parsed.ok(), "valid v4 COB rejected");
        require(parsed.value->header.static_variable_count == 5, "static variable count");
        require(
            parsed.value->code.size() == 3 && parsed.value->code[0] == 0x1000000U, "code words"
        );
        require(
            parsed.value->entry_points.size() == 2 && parsed.value->entry_points[1] == 2,
            "script entry words"
        );
        require(
            parsed.value->scripts[0].name == "start" && parsed.value->scripts[1].name == "stop" &&
                parsed.value->piece_names[0] == "root",
            "absolute names"
        );

        // The VersionSignature is not checked: a file whose tables are laid out
        // as the header's offsets say loads whatever the word holds.
        for (const uint32_t version : {0U, 6U, 7U, 0xFFFFFFFFU}) {
            auto other_version = bytes;
            put32(other_version, 0, version);
            const auto other_parsed = oa::formats::cob::parse_cob(other_version);
            require(
                other_parsed.ok() && other_parsed.value->header.version_signature == version &&
                    other_parsed.value->code == parsed.value->code &&
                    other_parsed.value->entry_points == parsed.value->entry_points &&
                    other_parsed.value->scripts[1].name == "stop" &&
                    other_parsed.value->piece_names == parsed.value->piece_names,
                "a COB of another VersionSignature with valid tables rejected"
            );
        }
        // A version 6 header's two further words are not read; the tables are
        // found by the offsets alone.
        const auto extended = oa::formats::cob::parse_cob(extended_header_fixture());
        require(
            extended.ok() && extended.value->header.version_signature == 6 &&
                extended.value->header.code_offset == 52 &&
                extended.value->code == parsed.value->code &&
                extended.value->entry_points == parsed.value->entry_points &&
                extended.value->scripts[0].name == "start" &&
                extended.value->scripts[1].name == "stop" &&
                extended.value->piece_names == parsed.value->piece_names,
            "a version 6 COB with an extended header and valid tables rejected"
        );
        // A version 6 file still gets every bound: here its code runs past the
        // entry table.
        auto extended_bad = extended_header_fixture();
        put32(extended_bad, 12, 4);
        const auto extended_bad_parsed = oa::formats::cob::parse_cob(extended_bad);
        require(
            !extended_bad_parsed.ok() &&
                extended_bad_parsed.error.code == oa::base::bytes::DecodeCode::out_of_range,
            "a version 6 COB with overlapping sections accepted"
        );
        auto sounds = bytes;
        put32(sounds, 20, 1);
        const auto sounds_parsed = oa::formats::cob::parse_cob(sounds);
        require(
            !sounds_parsed.ok() &&
                sounds_parsed.error.code == oa::base::bytes::DecodeCode::unsupported_version &&
                sounds_parsed.error.offset == 20,
            "a non-zero sound count accepted"
        );
        // A header whose tables are all empty and start where it ends is a
        // script with nothing in it.
        std::vector<uint8_t> empty(oa::formats::cob::header_bytes, 0);
        const std::array<uint32_t, 11> empty_header{4, 0, 0, 0, 0, 0, 44, 44, 44, 44, 44};
        for (std::size_t i = 0; i < empty_header.size(); ++i)
            put32(empty, i * 4, empty_header[i]);
        const auto empty_parsed = oa::formats::cob::parse_cob(empty);
        require(
            empty_parsed.ok() && empty_parsed.value->code.empty() &&
                empty_parsed.value->scripts.empty() && empty_parsed.value->piece_names.empty(),
            "a COB of empty tables rejected"
        );
        auto bad_entry = bytes;
        put32(bad_entry, 56, 3);
        const auto bad_entry_parsed = oa::formats::cob::parse_cob(bad_entry);
        require(
            !bad_entry_parsed.ok() &&
                bad_entry_parsed.error.code == oa::base::bytes::DecodeCode::out_of_range &&
                bad_entry_parsed.error.offset == 56,
            "out-of-range entry accepted or reported at the wrong offset"
        );
        const auto short_parsed =
            oa::formats::cob::parse_cob(std::span(bytes).first(oa::formats::cob::header_bytes - 1));
        require(
            !short_parsed.ok() && short_parsed.error.code == oa::base::bytes::DecodeCode::truncated,
            "a file shorter than its header must be truncated"
        );
        auto bad_name = bytes;
        put32(bad_name, 64, 95);
        require(!oa::formats::cob::parse_cob(bad_name).ok(), "unterminated name accepted");
        auto unlimited_name = oa::formats::cob::ParseLimits{};
        unlimited_name.max_name_bytes = static_cast<std::size_t>(-1);
        require(
            oa::formats::cob::parse_cob(bytes, unlimited_name).ok(),
            "maximum name limit rejected valid name"
        );
        auto low_static_limit = oa::formats::cob::ParseLimits{};
        low_static_limit.max_static_variables = 4;
        require(
            !oa::formats::cob::parse_cob(bytes, low_static_limit).ok(),
            "static-variable bound was not enforced"
        );
        std::cout << "COB parser tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
