// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/data/mission_types.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <string>

namespace oa::data::mission_types {
namespace {
constexpr std::array<std::string_view, registered_count> names{{
    "",
    "Activate",
    "AirStrike",
    "AirToAir",
    "AirToGround",
    "AirToGroundHover",
    "Attack_Chase",
    "Attack_Kamikaze",
    "Attack_NoMove",
    "AttackSpecial",
    "AttackUType",
    "BeCarried",
    "BuildingBuild",
    "BuildWeapon",
    "Capture",
    "Cloak_Off",
    "Cloak_On",
    "Deactivate",
    "Follow_Ground",
    "GetBuilt",
    "Ground_Pickup",
    "Ground_Unload",
    "Guard_NoMove",
    "HelpBuild",
    "MakeSelectable",
    "MobileBuild",
    "Move_Ground",
    "Paralyze",
    "Park",
    "Patrol",
    "QMove",
    "QPatrol",
    "Reclaim",
    "ReclaimUnit",
    "RepairPatrol",
    "RepairUnit",
    "RepairUnitNoMove",
    "Resurrect",
    "SelfDestruct",
    "SelfDestructFG",
    "SelfRepair",
    "Standby",
    "Standby_Mine",
    "Standing_FireOrder",
    "Standing_MoveOrder",
    "Stop",
    "Suppress",
    "Teleport",
    "VTOL_Evade",
    "VTOL_Follow",
    "VTOL_GetRepaired",
    "VTOL_HelpBuild",
    "VTOL_LandIfCan",
    "VTOL_Landing",
    "VTOL_MobileBuild",
    "VTOL_Move",
    "VTOL_Patrol",
    "VTOL_Pickup",
    "VTOL_Reclaim",
    "VTOL_ReclaimUnit",
    "VTOL_RepairPatrol",
    "VTOL_RepairUnit",
    "VTOL_SeekAttack",
    "VTOL_SeekGuard",
    "VTOL_Standby",
    "VTOL_Unload",
    "Wait",
    "WaitForAttack",
}};

constexpr unsigned char fold(unsigned char c) noexcept {
    return c >= 'A' && c <= 'Z' ? static_cast<unsigned char>(c + ('a' - 'A')) : c;
}

int compare(std::string_view a, std::string_view b) noexcept {
    for (std::size_t i = 0; i < std::min(a.size(), b.size()); ++i) {
        const auto left = fold(static_cast<unsigned char>(a[i]));
        const auto right = fold(static_cast<unsigned char>(b[i]));
        if (left != right)
            return left < right ? -1 : 1;
    }
    return a.size() == b.size() ? 0 : (a.size() < b.size() ? -1 : 1);
}
} // namespace

std::span<const std::string_view> registered_names() noexcept {
    return names;
}

uint8_t mission_flags(uint8_t index) noexcept {
    // SelfRepair and Standby_Mine are the only unnumbered orders.
    constexpr std::string_view unnumbered[] = {"SelfRepair", "Standby_Mine"};
    if (index >= names.size())
        return 0;
    for (const auto name : unnumbered)
        if (names[index] == name)
            return unnumbered_order;
    return 0;
}

uint8_t index_for_name(std::string_view name) noexcept {
    // A NUL ends the name.
    name = name.substr(0, name.find('\0'));
    const auto it = std::lower_bound(
        names.begin(), names.end(), name, [](std::string_view a, std::string_view b) {
            return compare(a, b) < 0;
        }
    );
    return it != names.end() && compare(*it, name) == 0 ? static_cast<uint8_t>(it - names.begin())
                                                        : unknown_mission;
}

int32_t mission_block_count(
    std::string_view campaign_file, std::span<const std::string_view> root_sections
) noexcept {
    // A NUL ends the campaign file name.
    campaign_file = campaign_file.substr(0, campaign_file.find('\0'));
    if (campaign_file.empty())
        return 0;
    for (int32_t count = 0;; ++count) {
        const auto label = std::string(mission_section_prefix) + std::to_string(count);
        const auto found =
            std::find_if(root_sections.begin(), root_sections.end(), [&](std::string_view section) {
                section = section.substr(0, section.find('\0'));
                return compare(section, label) == 0;
            });
        if (found == root_sections.end())
            return count;
    }
}
} // namespace oa::data::mission_types
