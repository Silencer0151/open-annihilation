// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/data/mission_types.hpp"
#include <iostream>
#include <string>
#include <string_view>
#include <stdexcept>
#include <cstdint>

int main() {
    const auto names = oa::data::mission_types::registered_names();
    if (names.size() != 68 || !names.front().empty())
        throw std::runtime_error("mission table shape");
    for (std::size_t i = 0; i < names.size(); ++i) {
        std::string mixed(names[i]);
        for (auto& c : mixed)
            if (c >= 'A' && c <= 'Z')
                c = static_cast<char>(c + 32);
        if (oa::data::mission_types::index_for_name(mixed) != i)
            throw std::runtime_error("mission name lookup");
    }
    if (oa::data::mission_types::index_for_name("not a mission") != 0)
        throw std::runtime_error("unknown mission");
    std::size_t unnumbered = 0;
    for (std::size_t i = 0; i < names.size(); ++i)
        unnumbered += oa::data::mission_types::mission_flags(static_cast<uint8_t>(i));
    if (unnumbered != 2 ||
        oa::data::mission_types::mission_flags(40) != oa::data::mission_types::unnumbered_order ||
        oa::data::mission_types::mission_flags(42) != oa::data::mission_types::unnumbered_order ||
        oa::data::mission_types::mission_flags(68) != 0)
        throw std::runtime_error("unnumbered orders");

    const std::string_view campaign[] = {
        "GlobalHeader",
        "mission0",
        "Mission1",
        "MISSION2extra",
        "MISSION10",
        std::string_view("MISSION2\0ignored", 16),
    };
    if (oa::data::mission_types::mission_block_count("", campaign) != 0)
        throw std::runtime_error("empty campaign ignores sections");
    if (oa::data::mission_types::mission_block_count(std::string_view("\0Core", 5), campaign) != 0)
        throw std::runtime_error("campaign nul");
    if (oa::data::mission_types::mission_block_count("Core", campaign) != 3)
        throw std::runtime_error("leading mission blocks");
    if (oa::data::mission_types::mission_block_count(" ", campaign) != 3)
        throw std::runtime_error("nonempty campaign text");
    const std::string_view gap[] = {"MISSION0", "MISSION2"};
    if (oa::data::mission_types::mission_block_count("Core", gap) != 1)
        throw std::runtime_error("mission block gap");
    const std::string_view later[] = {"MISSION1"};
    if (oa::data::mission_types::mission_block_count("Core", later) != 0)
        throw std::runtime_error("missing MISSION0");
    if (oa::data::mission_types::mission_block_count("Core", {}) != 0)
        throw std::runtime_error("no mission blocks");
}
