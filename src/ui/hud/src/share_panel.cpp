// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/ui/hud/share_panel.hpp"
#include "oa/ui/hud/order_panel.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace oa::ui::hud {
namespace {

// Player-record liveness: in use, a live status, a real index.
bool player_active(const Player& player) noexcept {
    return player.in_use != 0 &&
           (player.status == OA_PLAYER_STATUS_LOCAL || player.status == OA_PLAYER_STATUS_COMPUTER ||
            player.status == OA_PLAYER_STATUS_MIRRORED) &&
           player.index != kNoPlayer;
}

bool watcher_blocks_sharing(const ShareWorld& world, uint8_t index) noexcept {
    return world.setup_flags != nullptr && (world.setup_flags[index] & kSetupWatcher) != 0;
}

UnitEconomy* economy_of(const ShareWorld& world, uint8_t index) noexcept {
    return world.economies != nullptr ? world.economies[index] : nullptr;
}

float scaled_income(const Player* owner, float amount, int32_t difficulty) noexcept {
    if (owner == nullptr || owner->in_use == 0 || owner->status != OA_PLAYER_STATUS_COMPUTER)
        return amount;
    if (difficulty == OA_DIFFICULTY_EASY)
        return amount * 0.5F;
    if (difficulty == OA_DIFFICULTY_MEDIUM)
        return amount * 0.7F;
    return amount;
}

// Player index whose Player.player_id matches `player_id`, or kNoPlayer.
uint8_t player_by_id(const ShareWorld& world, uint32_t player_id) noexcept {
    if (player_id == 0xffffffffu)
        return kNoPlayer;
    for (uint8_t index = 0; index < OA_PLAYER_COUNT; ++index)
        if (world.players[index].player_id == player_id)
            return index;
    return kNoPlayer;
}

} // namespace

bool player_participating(const Player& player) noexcept {
    return player.in_use != 0 && player_active(player) &&
           (player.unit_count != 0 || player.units_created == 0) && player.index != kNoPlayer;
}

void credit_energy(
    UnitEconomy& economy, const Player* owner, float amount, int32_t difficulty
) noexcept {
    economy.energy.produced = scaled_income(owner, amount, difficulty) + economy.energy.produced;
}

void credit_metal(
    UnitEconomy& economy, const Player* owner, float amount, int32_t difficulty
) noexcept {
    economy.metal.produced = scaled_income(owner, amount, difficulty) + economy.metal.produced;
}

void transfer_energy(
    ShareWorld& world, uint8_t from, uint8_t to, float amount, bool local, const ShareHost& host
) {
    if (from >= kNoPlayer || to >= kNoPlayer)
        return;
    auto& giver = world.players[from];
    if (local && giver.energy < amount)
        amount = giver.energy;
    if (amount == 0.0F)
        return;
    if (local) {
        // Debit only when the store still covers it.
        if (auto* economy = economy_of(world, from); economy != nullptr && amount <= giver.energy) {
            giver.energy -= amount;
            economy->energy.requested = amount + economy->energy.requested;
        }
    }
    if (auto* economy = economy_of(world, to))
        credit_energy(*economy, &world.players[to], amount, world.difficulty);
    if (local && host.send_energy != nullptr)
        host.send_energy(host.user, from, to, amount);
}

void transfer_metal(
    ShareWorld& world, uint8_t from, uint8_t to, float amount, bool local, const ShareHost& host
) {
    if (from >= kNoPlayer || to >= kNoPlayer)
        return;
    auto& giver = world.players[from];
    if (local && giver.metal < amount)
        amount = giver.metal;
    if (amount == 0.0F)
        return;
    if (local) {
        // Debit only when the store still covers it.
        if (auto* economy = economy_of(world, from); economy != nullptr && amount <= giver.metal) {
            giver.metal -= amount;
            economy->metal.requested = amount + economy->metal.requested;
        }
    }
    if (auto* economy = economy_of(world, to))
        credit_metal(*economy, &world.players[to], amount, world.difficulty);
    if (local && host.send_metal != nullptr)
        host.send_metal(host.user, from, to, amount);
}

bool open_share_panel(const ShareWorld& world, SharePanel& panel, uint16_t& frame_flags) noexcept {
    panel = {};
    if (world.local_player >= kNoPlayer || watcher_blocks_sharing(world, world.local_player))
        return false;
    frame_flags |= kFrameSharePanelOpen;
    const auto& local = world.players[world.local_player];
    panel.metal_max = static_cast<int32_t>(local.metal);
    panel.energy_max = static_cast<int32_t>(local.energy);
    for (uint8_t index = 0; index < OA_PLAYER_COUNT; ++index) {
        const auto& player = world.players[index];
        if (!player_participating(player))
            continue;
        if (player.in_use != 0 &&
            (player.status == OA_PLAYER_STATUS_LOCAL || watcher_blocks_sharing(world, index)))
            continue;
        panel.recipients[panel.recipient_count] = index;
        panel.player_ids[panel.recipient_count] = player.player_id;
        ++panel.recipient_count;
    }
    return true;
}

int32_t slider_amount(int16_t position, int16_t track, int32_t maximum) noexcept {
    if (track < 2)
        return 0;
    const auto fraction = static_cast<double>(position) / static_cast<double>(track - 1);
    return static_cast<int32_t>(fraction * static_cast<double>(maximum));
}

void format_share_amount(char* out, std::size_t size, int32_t amount) {
    std::snprintf(out, size, "%d", amount);
}

ShareClick share_panel_click(
    ShareWorld& world,
    const SharePanel& panel,
    const char* name,
    int32_t selected,
    int32_t metal,
    int32_t energy,
    bool give_units,
    bool share_map,
    uint16_t& frame_flags,
    const HudEvents& events,
    const ShareHost& host
) {
    if (name == nullptr) {
        frame_flags = static_cast<uint16_t>(frame_flags & ~kFrameSharePanelOpen);
        return ShareClick::closed;
    }
    if (std::strcmp(name, "MAPINFO") == 0 || std::strcmp(name, "SHARUNIT") == 0) {
        play_sound(events, "Options");
        return ShareClick::toggled;
    }
    if (std::strcmp(name, "OK") == 0) {
        play_sound(events, "Options");
        if (selected < 0 || selected >= panel.recipient_count)
            return ShareClick::none;
        const auto to = player_by_id(world, panel.player_ids[selected]);
        if (to >= kNoPlayer)
            return ShareClick::none;
        const auto& recipient = world.players[to];
        if (!player_active(recipient))
            return ShareClick::none;
        if (recipient.in_use != 0 && watcher_blocks_sharing(world, to))
            return ShareClick::none;
        if (!player_participating(recipient))
            return ShareClick::none;
        transfer_metal(world, world.local_player, to, static_cast<float>(metal), true, host);
        transfer_energy(world, world.local_player, to, static_cast<float>(energy), true, host);
        if (give_units && host.give_units != nullptr)
            host.give_units(host.user, world.local_player, to);
        if (share_map && host.share_map != nullptr)
            host.share_map(host.user, world.local_player, to);
        return ShareClick::none;
    }
    if (std::strcmp(name, "CANCEL") == 0) {
        play_sound(events, "Previous");
        return ShareClick::none;
    }
    return ShareClick::clear_selection;
}

void give_selected_units(
    World& world, uint8_t recipient, const uint32_t* commander_types, const UnitTransfer& transfer
) {
    Player* to = world_player(&world, recipient);
    const Player* local = world_player(&world, world.game.local_player_index);
    if (to == nullptr || local == nullptr || transfer.transfer == nullptr)
        return;
    uint32_t count = 0;
    Unit* first = world_player_units(&world, local, &count);
    if (count == 0)
        return;
    // The selection is collected first: a transfer may move units.
    auto** selected = static_cast<Unit**>(std::malloc(sizeof(Unit*) * count));
    if (selected == nullptr)
        return;
    uint32_t chosen = 0;
    for (uint32_t index = 0; index < count; ++index)
        if ((first[index].flags & OA_UNIT_FLAG_SELECTED) != 0)
            selected[chosen++] = &first[index];
    for (uint32_t index = 0; index < chosen; ++index) {
        Unit& unit = *selected[index];
        const uint32_t type = unit.type_index;
        const bool commander =
            commander_types != nullptr && (commander_types[type >> 5] & (1u << (type & 31u))) != 0;
        if ((unit.flags & OA_UNIT_FLAG_OCCUPANCY_MASK) == kOccupancyAirborne ||
            unit.attach_first_child != 0 || unit.attach_parent != 0 || commander)
            continue;
        transfer.transfer(transfer.user, unit, *to);
    }
    std::free(selected);
}

} // namespace oa::ui::hud
