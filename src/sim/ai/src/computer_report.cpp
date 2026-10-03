// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The console's per-player AI weight report.
#include "oa/sim/ai.hpp"

#include "oa/data/match_rules/difficulty_names.hpp"

namespace oa::sim::ai {
namespace {

constexpr uint32_t ticks_per_second = 30;
constexpr uint32_t ticks_per_minute = 60 * ticks_per_second;
constexpr uint32_t ticks_per_hour = 60 * ticks_per_minute;

constexpr const char* difficulty_labels[] = {"EASY", "MEDIUM", "HARD"};

// A null string prints as "(null)".
const char* printable(const char* text) noexcept {
    return text != nullptr ? text : "(null)";
}

const char* controller_label(uint8_t status) noexcept {
    if (status == OA_PLAYER_STATUS_LOCAL)
        return "HUMAN";
    if (status == OA_PLAYER_STATUS_COMPUTER)
        return "AI";
    return "INVALID";
}

} // namespace

void computer_write_report(
    const ComputerPlayers* state,
    const ComputerHost& host,
    uint8_t player,
    const ComputerReportPaths& paths,
    std::FILE* out
) noexcept {
    if (state == nullptr || host.world == nullptr || out == nullptr || player >= OA_PLAYER_COUNT)
        return;
    const World& world = *host.world;
    const Player& record = world.game.players[player];
    const uint32_t tick = world.game.tick;
    const uint32_t within_hour = tick % ticks_per_hour;
    std::fprintf(
        out,
        "Match clock: %02d:%02d:%02d\r\n",
        static_cast<int>(tick / ticks_per_hour),
        static_cast<int>(within_hour / ticks_per_minute),
        static_cast<int>(within_hour % ticks_per_minute / ticks_per_second)
    );
    std::fprintf(
        out,
        "Name: '%.*s' in player slot %d\r\n",
        static_cast<int>(sizeof record.name),
        record.name,
        static_cast<int>(player)
    );
    std::fprintf(out, "Played by: %s\r\n", controller_label(record.status));
    std::fprintf(out, "Map file: '%s'\r\n", printable(paths.terrain));
    std::fprintf(out, "AI settings file: '%s'\r\n", printable(paths.profile));
    const bool known_difficulty =
        host.difficulty >= OA_DIFFICULTY_EASY && host.difficulty <= OA_DIFFICULTY_HARD;
    std::fprintf(
        out,
        "Challenge level: '%s'\r\n",
        printable(
            known_difficulty ? difficulty_labels[data::match_rules::difficulty_name_index(
                                   state->rules.rules().ai.difficulty_names, host.difficulty
                               )]
                             : nullptr
        )
    );
    std::fprintf(out, "================================================\r\n");
    std::fprintf(
        out,
        "Columns: build limit - priority : metal value : energy value = weight percent before "
        "economy adjustment - short name : full name\r\n"
    );
    const ComputerPlayer& ai = state->players[player];
    if (!ai.present)
        return;
    const ComputerKnowledge& knowledge = ai.knowledge;
    for (uint32_t type = 1; type < state->type_count && type < world.unit_def_count; ++type) {
        if (knowledge.limits[type] < 0)
            std::fprintf(out, "n/a ");
        else
            std::fprintf(out, "%4d", static_cast<int>(knowledge.limits[type]));
        const uint8_t* strengths =
            host.strengths != nullptr
                ? host.strengths(host.context, player, static_cast<uint16_t>(type))
                : nullptr;
        const auto strength = [strengths](int index) {
            return strengths != nullptr ? static_cast<int>(static_cast<int8_t>(strengths[index]))
                                        : 0;
        };
        const UnitDef& def = world.unit_defs[type];
        std::fprintf(
            out,
            " - %3d : %3d : %3d = %3d - '%.*s\t\t:%.*s'\r\n",
            strength(0),
            strength(1),
            strength(2),
            static_cast<int>(knowledge.weight_percent[type]),
            static_cast<int>(sizeof def.unit_name),
            def.unit_name,
            static_cast<int>(sizeof def.name),
            def.name
        );
    }
}

} // namespace oa::sim::ai
