// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/netgame/sync/unit_checksum.hpp"

#include "oa/formats/tdf.hpp"

#include <array>
#include <limits>
#include <string>

namespace oa::netgame::sync {

using data::unit_definitions::CatalogAssetReader;
using data::unit_definitions::ErrorCode;
using data::unit_definitions::Result;

uint32_t unit_file_checksum(std::string_view bytes) noexcept {
    if (bytes.empty() ||
        bytes.size() > static_cast<std::size_t>(std::numeric_limits<int32_t>::max()))
        return 0;
    return oa::formats::tdf::buffer_hash(
        reinterpret_cast<const uint8_t*>(bytes.data()), static_cast<int32_t>(bytes.size())
    );
}

namespace {

char ascii_upper(char c) noexcept {
    const auto u = static_cast<unsigned char>(c);
    if (u >= 'a' && u <= 'z')
        return static_cast<char>(u - ('a' - 'A'));
    return static_cast<char>(u);
}

// The game's wildcard match: active pattern indexes, capped at 100. '?' and
// '*' are matched after an ASCII upper-case fold. A trailing '*' succeeds at
// end.
bool wildcard_match(std::string_view name, std::string_view pattern) {
    std::array<int, 100> states{};
    int count = 1;
    for (const char raw : name) {
        const char folded = ascii_upper(raw);
        if (folded == '\0')
            break;
        if (count <= 0)
            continue;
        int seen = 0;
        int read = 0;
        int write = count;
        while (seen < count) {
            const int at = states[static_cast<std::size_t>(read)];
            const char pattern_char = at >= 0 && static_cast<std::size_t>(at) < pattern.size()
                                          ? ascii_upper(pattern[static_cast<std::size_t>(at)])
                                          : '\0';
            if (pattern_char == '?' || pattern_char == folded) {
                states[static_cast<std::size_t>(read)] = at + 1;
            } else if (pattern_char == '*') {
                if (count < 100) {
                    ++count;
                    states[static_cast<std::size_t>(write)] = at + 1;
                    ++write;
                }
            } else {
                --count;
                --write;
                if (count == 0)
                    return false;
                --seen;
                states[static_cast<std::size_t>(read)] = states[static_cast<std::size_t>(write)];
                --read;
            }
            ++seen;
            ++read;
        }
    }
    for (int i = 0; i < count; ++i) {
        const int at = states[static_cast<std::size_t>(i)];
        if (at < 0 || static_cast<std::size_t>(at) >= pattern.size())
            return true;
        if (pattern[static_cast<std::size_t>(at)] == '*' &&
            (static_cast<std::size_t>(at) + 1U >= pattern.size() ||
             pattern[static_cast<std::size_t>(at) + 1U] == '\0'))
            return true;
    }
    return false;
}

std::string_view filename_of(std::string_view path) noexcept {
    const auto slash = path.find_last_of("/\\");
    if (slash == std::string_view::npos)
        return path;
    return path.substr(slash + 1);
}

} // namespace

Result<uint32_t> mix_unit_file_checksum(
    const CatalogAssetReader& assets,
    std::string_view unit_name,
    uint32_t content_checksum,
    uint32_t weapon_checksum
) {
    Result<uint32_t> result;
    if (unit_name.find_first_of("/\\") != std::string_view::npos) {
        result.error = {ErrorCode::malformed, 0, "unit name contains a path separator"};
        return result;
    }
    if (content_checksum != 0) {
        result.value = content_checksum;
        return result;
    }
    uint32_t mixed = 0;
    if (auto cob = assets.read(std::string("scripts/") + std::string(unit_name) + ".cob"))
        mixed ^= unit_file_checksum(cob.value);
    auto guis = assets.list_effective("guis", ".gui");
    if (!guis) {
        result.error = std::move(guis.error);
        return result;
    }
    const std::string pattern = std::string(unit_name) + "*.gui";
    for (const auto& path : guis.value) {
        const auto name = filename_of(path);
        // The game's file search drops the entries "." and "..".
        if (name == "." || name == "..")
            continue;
        if (!wildcard_match(name, pattern))
            continue;
        if (auto gui = assets.read(path))
            mixed ^= unit_file_checksum(gui.value);
    }
    if (auto download = assets.read(std::string("download/") + std::string(unit_name) + ".tdf")) {
        if (!download.value.empty() &&
            download.value.size() <= static_cast<std::size_t>(std::numeric_limits<int32_t>::max()))
            mixed ^= unit_file_checksum(download.value);
    }
    result.value = mixed ^ weapon_checksum;
    return result;
}

} // namespace oa::netgame::sync
