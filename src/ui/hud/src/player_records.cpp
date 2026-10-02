// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/ui/hud/player_records.hpp"

#include "oa/ui/hud/chat_panel.hpp"
#include "oa/ui/hud/game_fields.hpp"

#include "oa/data/persist/save_sections.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace oa::ui::hud {
namespace {

constexpr int32_t kBytesPerMegabyte = 0x100000;
constexpr uint8_t kArmSide = 0;
// Names init_player_slot gives in a campaign or skirmish, as the English
// game shows them.
constexpr const char* kHumanName = "Player";
constexpr const char* kArmComputerName = "Arm";
constexpr const char* kCoreComputerName = "Core";

/// Game.last_frame_time through Game.sim_run_flags, saved as one blob.
constexpr size_t kGameClockBytes =
    offsetof(Game, output_directory) - offsetof(Game, last_frame_time);
static_assert(kGameClockBytes == 0x1c);
constexpr uint32_t kAllianceBytes = 11;

void player_account(data::persist::Bank& bank, int32_t index, bool& existed) {
    char name[data::persist::save_name_bytes];
    std::snprintf(name, sizeof name, data::persist::save_key::player_format, index);
    existed = data::persist::bank_open_account(&bank, name);
}

int32_t clamp_storage(int32_t value) noexcept {
    return value < kMinimumStartStorage ? kMinimumStartStorage : value;
}

/// Index of the multiplayer host slot, or OA_PLAYER_COUNT.
uint8_t host_slot(World& world) noexcept {
    for (uint8_t index = 0; index < OA_PLAYER_COUNT; ++index) {
        const Player& player = world.game.players[index];
        const auto* info = world_player_info(&world, &player);
        if (player.status != OA_PLAYER_STATUS_FREE && info != nullptr &&
            (info->role & kSetupRoleHost) != 0)
            return index;
    }
    return OA_PLAYER_COUNT;
}

} // namespace

void set_starting_resources(
    World& world,
    oa::data::campaign::SessionKind session_kind,
    const MissionResources* mission,
    const SkirmishSlot* skirmish
) {
    Game& game = world.game;
    for (int32_t index = 0; index < OA_PLAYER_COUNT; ++index) {
        if (game.saved_game != 0)
            continue;
        Player& player = game.players[index];
        if (session_kind == oa::data::campaign::SessionKind::campaign && mission != nullptr) {
            const auto energy = static_cast<int32_t>(mission->energy[index]);
            const auto metal = static_cast<int32_t>(mission->metal[index]);
            player.resource_flags |= kResourceSharesStorage;
            player.shared_energy_storage = static_cast<float>(clamp_storage(energy));
            player.shared_metal_storage = static_cast<float>(clamp_storage(metal));
            player.energy = mission->energy[index];
            player.metal = mission->metal[index];
        } else if (
            session_kind == oa::data::campaign::SessionKind::skirmish && skirmish != nullptr
        ) {
            player.energy = static_cast<float>(skirmish[index].energy);
            player.metal = static_cast<float>(skirmish[index].metal);
        } else if (session_kind == oa::data::campaign::SessionKind::multiplayer) {
            auto host = host_slot(world);
            if (host == OA_PLAYER_COUNT)
                host = static_cast<uint8_t>(index);
            const auto* info = world_player_info(&world, &game.players[host]);
            if (info == nullptr)
                continue;
            player.energy = static_cast<float>(static_cast<int32_t>(info->energy_hundreds) * 100);
            player.metal = static_cast<float>(static_cast<int32_t>(info->metal_hundreds) * 100);
        }
    }
}

void init_player_slot(
    World& world,
    uint8_t index,
    uint8_t status,
    oa::data::campaign::SessionKind session_kind,
    int32_t physical_memory
) {
    Player* record = world_player_record(&world, index);
    if (record == nullptr)
        return;
    Player& player = *record;
    PlayerSetupInfo* info = world_player_info(&world, &player);
    std::memset(player.alliance, 0, sizeof player.alliance);
    std::memset(player.allied_by, 0, sizeof player.allied_by);
    std::memset(player.economy_requested, 0, sizeof player.economy_requested);
    std::memset(player.economy_processed, 0, sizeof player.economy_processed);
    std::memset(player.economy_answered, 0, sizeof player.economy_answered);
    player.status = status;
    if (status != OA_PLAYER_STATUS_MIRRORED && info != nullptr)
        info->state = status;
    player.allied_by[index] = 1;
    player.controller = 0;
    player.index = index;
    player.board_row = index;
    player.alliance[index] = 1;
    player.in_use = 1;
    player.start_position = index;
    player.team = OA_PLAYER_NO_TEAM;
    if (info != nullptr)
        info->options = static_cast<uint16_t>(info->options & ~OA_SETUP_OPTION_READY);
    player.reject_reason = 0;
    player.machine_group = 0;
    player.player_id = index;
    if (info != nullptr &&
        (status == OA_PLAYER_STATUS_LOCAL || status == OA_PLAYER_STATUS_COMPUTER))
        info->memory_mb =
            static_cast<uint16_t>(static_cast<int16_t>(physical_memory / kBytesPerMegabyte) + 1);
    if (session_kind != oa::data::campaign::SessionKind::campaign &&
        session_kind != oa::data::campaign::SessionKind::skirmish)
        return;
    const char* name = nullptr;
    if (status == OA_PLAYER_STATUS_LOCAL)
        name = kHumanName;
    else if (status == OA_PLAYER_STATUS_COMPUTER && info != nullptr)
        name = info->side == kArmSide ? kArmComputerName : kCoreComputerName;
    if (name != nullptr)
        std::snprintf(player.name, sizeof player.name, "%s", name);
    std::snprintf(
        player.second_name,
        sizeof player.second_name,
        "%.*s",
        static_cast<int>(sizeof player.name),
        player.name
    );
}

int32_t
next_roster_ally(const SkirmishSlot* roster, int32_t count, int32_t entry, int32_t start) noexcept {
    if (entry < 0 || entry >= count || start < 0)
        return -1;
    for (; start < count; ++start) {
        const SkirmishSlot& candidate = roster[start];
        if (candidate.alliance == roster[entry].alliance &&
            candidate.controller != OA_PLAYER_STATUS_FREE &&
            candidate.alliance != kNoRosterAlliance)
            return start;
        if (start == entry)
            return start;
    }
    return -1;
}

void init_player_slots_from_roster(
    World& world,
    const SkirmishSlot* roster,
    int32_t count,
    oa::data::campaign::SessionKind session_kind,
    int32_t physical_memory
) {
    if (count > kRosterCapacity)
        count = kRosterCapacity;
    for (int32_t entry = 0; entry < count; ++entry) {
        const SkirmishSlot& slot = roster[entry];
        const auto index = static_cast<uint8_t>(entry);
        Player& player = *world_player_record(&world, index);
        PlayerSetupInfo* info = world_player_info(&world, &player);
        const bool seated = slot.controller == OA_PLAYER_STATUS_LOCAL ||
                            slot.controller == OA_PLAYER_STATUS_COMPUTER;
        if (seated && info != nullptr) {
            info->color = static_cast<uint8_t>(slot.color);
            info->side = static_cast<uint8_t>(slot.side);
        }
        const auto status = seated ? static_cast<uint8_t>(slot.controller)
                                   : static_cast<uint8_t>(OA_PLAYER_STATUS_FREE);
        init_player_slot(world, index, status, session_kind, physical_memory);
        if (slot.controller == OA_PLAYER_STATUS_LOCAL) {
            world.game.viewpoint_player = index;
            world.game.local_player_index = index;
        }
        if (!seated)
            continue;
        for (int32_t ally = next_roster_ally(roster, count, entry, 0); ally != -1;
             ally = next_roster_ally(roster, count, entry, ally + 1))
            player.alliance[ally] = 1;
    }
}

void load_player_controllers(World& world, data::persist::Bank& bank, SkirmishSlot* skirmish) {
    for (int32_t index = 0; index < OA_PLAYER_COUNT; ++index) {
        Player& player = world.game.players[index];
        bool existed = false;
        player_account(bank, index, existed);
        const int32_t controller =
            existed ? data::persist::bank_get_int(&bank, data::persist::save_key::controller, 0)
                    : 0;
        if (skirmish != nullptr)
            skirmish[index].controller = controller;
        player.status = static_cast<uint8_t>(controller);
    }
}

bool load_players_section(World& world, data::persist::Bank& bank) {
    Game& game = world.game;
    data::persist::bank_open_account(&bank, data::persist::save_key::players);
    game.local_player_index = static_cast<uint8_t>(data::persist::bank_get_int(
        &bank, data::persist::save_key::human_player, data::persist::no_human_player
    ));
    game.viewpoint_player = game.local_player_index;
    data::persist::bank_open_blob_name(&bank, "GameTime");
    if (data::persist::bank_blob_read(&bank, &game.last_frame_time, kGameClockBytes) !=
        kGameClockBytes)
        return false;
    for (int32_t index = 0; index < OA_PLAYER_COUNT; ++index) {
        Player& player = game.players[index];
        bool existed = false;
        player_account(bank, index, existed);
        if (!existed) {
            player.status = OA_PLAYER_STATUS_FREE;
            continue;
        }
        const auto real = [&](const char* key) {
            return data::persist::bank_get_real(&bank, key, 0.0);
        };
        const auto integer = [&](const char* key) {
            return data::persist::bank_get_int(&bank, key, 0);
        };
        player.energy = static_cast<float>(real("Energy"));
        player.metal = static_cast<float>(real("Metal"));
        player.energy_produced_total = real("TotalEnergyProduced");
        player.metal_produced_total = real("TotalMetalProduced");
        player.energy_requested_total = real("TotalEnergyConsumed");
        player.metal_requested_total = real("TotalMetalConsumed");
        player.energy_wasted_total = real("EnergyWasted");
        player.metal_wasted_total = real("MetalWasted");
        player.shared_energy_storage = static_cast<float>(real("PlayerEnergyStorage"));
        player.shared_metal_storage = static_cast<float>(real("PlayerMetalStorage"));
        player.resource_flags = static_cast<uint8_t>(
            (player.resource_flags & ~kResourceSharesStorage) |
            (integer("AddPlayerStorage") & kResourceSharesStorage)
        );
        player.kills = static_cast<int16_t>(integer("Kills"));
        player.losses = static_cast<int16_t>(integer("Losses"));
        player.next_economy_tick = static_cast<uint32_t>(integer("UpdateTime"));
        set_win_lose_time(player, integer("WinLoseTime"));
        set_display_timer(player, integer("DisplayTimer"));
        if (auto* info = world_player_info(&world, &player)) {
            info->color = static_cast<uint8_t>(integer("Logo"));
            info->side = static_cast<uint8_t>(integer("Side"));
        }
        data::persist::bank_open_blob_name(&bank, "Alliances");
        if (data::persist::bank_blob_size(&bank) == static_cast<int32_t>(kAllianceBytes))
            data::persist::bank_blob_read(&bank, player.alliance, kAllianceBytes);
        player.alliance[index] = 1;
    }
    return true;
}

void save_players_section(World& world, data::persist::Bank& bank) {
    Game& game = world.game;
    data::persist::bank_open_account(&bank, data::persist::save_key::players);
    data::persist::bank_set_int(
        &bank, data::persist::save_key::human_player, game.local_player_index
    );
    data::persist::bank_open_blob_name(&bank, "GameTime");
    data::persist::bank_blob_write(&bank, &game.last_frame_time, kGameClockBytes);
    for (int32_t index = 0; index < OA_PLAYER_COUNT; ++index) {
        Player& player = game.players[index];
        if (player.status == OA_PLAYER_STATUS_FREE)
            continue;
        bool existed = false;
        player_account(bank, index, existed);
        data::persist::bank_set_real(&bank, "Energy", player.energy);
        data::persist::bank_set_real(&bank, "Metal", player.metal);
        data::persist::bank_set_real(&bank, "TotalEnergyProduced", player.energy_produced_total);
        data::persist::bank_set_real(&bank, "TotalMetalProduced", player.metal_produced_total);
        data::persist::bank_set_real(&bank, "TotalEnergyConsumed", player.energy_requested_total);
        data::persist::bank_set_real(&bank, "TotalMetalConsumed", player.metal_requested_total);
        data::persist::bank_set_real(&bank, "EnergyWasted", player.energy_wasted_total);
        data::persist::bank_set_real(&bank, "MetalWasted", player.metal_wasted_total);
        data::persist::bank_set_real(&bank, "PlayerEnergyStorage", player.shared_energy_storage);
        data::persist::bank_set_real(&bank, "PlayerMetalStorage", player.shared_metal_storage);
        data::persist::bank_set_int(
            &bank, "AddPlayerStorage", player.resource_flags & kResourceSharesStorage
        );
        data::persist::bank_set_int(&bank, "Kills", player.kills);
        data::persist::bank_set_int(&bank, "Losses", player.losses);
        data::persist::bank_set_int(
            &bank, "UpdateTime", static_cast<int32_t>(player.next_economy_tick)
        );
        data::persist::bank_set_int(&bank, "WinLoseTime", win_lose_time(player));
        data::persist::bank_set_int(&bank, "DisplayTimer", display_timer(player));
        data::persist::bank_set_int(&bank, data::persist::save_key::controller, player.status);
        const auto* info = world_player_info(&world, &player);
        data::persist::bank_set_int(&bank, "Logo", info != nullptr ? info->color : 0);
        data::persist::bank_set_int(&bank, "Side", info != nullptr ? info->side : 0);
        data::persist::bank_open_blob_name(&bank, "Alliances");
        data::persist::bank_blob_write(&bank, player.alliance, kAllianceBytes);
    }
}

bool byte_grid_resize(ByteGrid& grid, int32_t width, int32_t height) {
    grid.width = width;
    grid.height = height;
    std::free(grid.data);
    grid.data = nullptr;
    const auto bytes = static_cast<uint32_t>(width * height + 7) & ~7u;
    grid.capacity = bytes;
    if (bytes == 0)
        return true;
    grid.data = static_cast<uint8_t*>(std::malloc(bytes));
    return grid.data != nullptr;
}

void byte_grid_free(ByteGrid& grid) {
    std::free(grid.data);
    grid = {};
}

} // namespace oa::ui::hud
