// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/state_hash.hpp"

#include "oa/formats/cob.hpp"
#include "oa/sim/ground_orders/ground_runtime.hpp"
#include "oa/data/persist/save_orders.hpp"
#include "oa/data/persist/save_sections.hpp"
#include "oa/sim/script_state.hpp"
#include "oa/sim/trace.hpp"

#include <cstddef>
#include <type_traits>
#include <vector>

namespace oa::sim::trace {
namespace {

namespace persist = oa::data::persist;

/// Folds bytes into a 64-bit FNV-1a digest.
///
/// @param[in,out] hash digest so far
/// @param bytes bytes to fold
/// @param size number of bytes
void mix(uint64_t& hash, const void* bytes, size_t size) noexcept {
    const auto* p = static_cast<const uint8_t*>(bytes);
    for (size_t i = 0; i < size; ++i) {
        hash ^= p[i];
        hash *= digest_prime;
    }
}

/// Folds a value's bytes, as the host holds them, into a 64-bit FNV-1a digest.
///
/// Takes a copy, so a field of a packed record is read wherever it sits. An
/// array is folded with mix instead.
///
/// @param[in,out] hash digest so far
/// @param value value to fold
template <class T>
void mix_value(uint64_t& hash, T value) noexcept {
    static_assert(!std::is_pointer_v<T>, "fold the pointed-to bytes with mix");
    mix(hash, &value, sizeof value);
}

/// Folds an order and its goal as their blobs hold them into the digest `walk` points at.
///
/// @param[in,out] walk the uint64_t digest being built
/// @param order saved order
/// @param goal the order's saved goal
void mix_saved_order(void* walk, const persist::SavedOrder* order, const persist::SavedGoal* goal) {
    auto& hash = *static_cast<uint64_t*>(walk);
    uint8_t blob[persist::order_blob_bytes];
    persist::save_encode_order(order, blob);
    mix(hash, blob, sizeof blob);
    uint8_t goal_blob[persist::goal_blob_max_bytes];
    mix(hash, goal_blob, persist::save_encode_goal(goal, goal_blob));
}

} // namespace

uint64_t match_state_hash(
    sim::match_runtime::Match& match,
    const base::game_loop::Timing& timing,
    int32_t camera_x,
    int32_t camera_z,
    const sim::world_environment::MeteorState* meteor
) {
    uint64_t hash = digest_basis;
    const World& world = match.state();
    mix_value(hash, world.game.tick);
    mix_value(hash, timing.requested_rate);
    mix_value(hash, timing.actual_rate);
    mix_value(hash, timing.adaptation);
    mix_value(hash, timing.flags);
    for (size_t slot = 0; slot < OA_PLAYER_COUNT; ++slot) {
        const Player& player = world.game.players[slot];
        if (player.status == OA_PLAYER_STATUS_FREE)
            continue;
        mix_value(hash, player.index);
        mix_value(hash, player.status);
        mix_value(hash, player.energy);
        mix_value(hash, player.metal);
        mix_value(hash, player.shared_energy_storage);
        mix_value(hash, player.shared_metal_storage);
        mix_value(hash, player.energy_produced_total);
        mix_value(hash, player.metal_produced_total);
        mix_value(hash, player.energy_requested_total);
        mix_value(hash, player.metal_requested_total);
        mix_value(hash, player.energy_wasted_total);
        mix_value(hash, player.metal_wasted_total);
        mix_value(hash, player.kills);
        mix_value(hash, player.losses);
        mix_value(hash, player.next_economy_tick);
        mix_value(hash, player.unit_count);
        mix_value(hash, player.resource_flags);
        mix(hash, player.alliance, sizeof player.alliance);
        // Side and logo colour: PlayerSetupInfo.side and color, through Player.info.
        const PlayerSetupInfo* info = world_player_info(&world, &player);
        mix_value(hash, static_cast<uint8_t>(info != nullptr ? info->side : 0));
        mix_value(hash, static_cast<uint8_t>(info != nullptr ? info->color : 0));
    }
    for (uint32_t slot = 1; slot < world.unit_slot_count; ++slot) {
        const Unit& unit = world.units[slot];
        if ((unit.flags & OA_UNIT_FLAG_LIVE) == 0 || unit.type_index == 0)
            continue;
        mix_value(hash, unit.id);
        mix_value(hash, unit.type_index);
        mix_value(hash, unit.owner_index);
        mix_value(hash, unit.position);
        mix_value(hash, unit.bank);
        mix_value(hash, unit.heading);
        mix_value(hash, unit.pitch);
        mix_value(hash, unit.health);
        mix_value(hash, unit.veteran_level);
        mix_value(hash, unit.build_remaining);
        mix_value(hash, unit.squad);
        mix_value(hash, unit.cell_x);
        mix_value(hash, unit.cell_z);
        mix_value(hash, unit.sight_center_x);
        mix_value(hash, unit.sight_center_z);
        mix_value(hash, unit.footprint_x);
        mix_value(hash, unit.footprint_z);
        mix_value(hash, unit.extracted_metal);
        mix_value(hash, unit.decloak_until_tick);
        // The record links the last attacker only while it lives.
        const Unit* attacker = world_unit_at(&world, unit.last_attacker_id);
        const uint32_t linked =
            attacker != nullptr && (attacker->flags & OA_UNIT_FLAG_LIVE) != 0 ? attacker->id : 0u;
        mix_value(hash, linked);
        mix_value(hash, unit.last_attacker_owner);
        mix_value(hash, unit.damage_kind);
        mix_value(hash, unit.health_percent);
        mix_value(hash, unit.previous_health_percent);
        mix_value(hash, unit.sight_band);
        mix_value(hash, unit.attach_piece);
        mix_value(hash, unit.damage_countdown);
        mix_value(hash, unit.events);
        mix_value(hash, unit.state_flags);
        namespace f = data::persist::unit_record_flags;
        const uint32_t flags = unit.flags & (f::low | f::construction_dirty | f::high_kept);
        mix_value(hash, flags);
        const uint8_t build_flags = unit.build_flags & f::build_nibble;
        mix_value(hash, build_flags);
        mix_value(hash, unit.economy.energy);
        mix_value(hash, unit.economy.metal);
        for (const UnitWeapon& weapon : unit.weapons) {
            mix_value(hash, weapon.target_a);
            mix_value(hash, weapon.target_b);
            mix_value(hash, weapon.aim_ready);
            mix(hash, weapon.muzzle_offset, sizeof weapon.muzzle_offset);
            mix_value(hash, weapon.reload);
            mix_value(hash, weapon.aim_heading);
            mix_value(hash, weapon.aim_pitch);
            mix_value(hash, weapon.stockpile);
            const uint8_t weapon_flags = weapon.flags & persist::weapon_record::saved_flags;
            mix_value(hash, weapon_flags);
        }
        if (auto* instance = match.instance(unit.id);
            instance != nullptr && instance->script() != nullptr) {
            const auto exported = instance->script()->vm().export_state(
                formats::cob::script_identity(instance->script()->program())
            );
            const auto bytes =
                exported.ok() ? sim::script_state::encode(*exported.state) : std::vector<uint8_t>{};
            mix(hash, bytes.data(), bytes.size());
        }
        if (const auto* ground = match.ground_runtime(unit.id))
            mix_value(hash, ground->save_mobility());
        match.visit_saved_orders(unit.id, mix_saved_order, &hash);
    }
    for (const auto& plot : match.spatial().plots) {
        mix_value(hash, plot.metal);
        const uint8_t placer = plot.flags & persist::plot_flags_player_features;
        mix_value(hash, placer);
    }
    const auto& sight = match.sight().player_bits;
    mix(hash, sight.data(), sight.size() * sizeof(sight[0]));
    mix_value(hash, camera_x);
    mix_value(hash, camera_z);
    if (meteor != nullptr)
        mix_value(hash, *meteor);
    return hash;
}

} // namespace oa::sim::trace
