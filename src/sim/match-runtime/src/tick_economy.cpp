// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "tick_internal.hpp"

namespace oa::sim::match_runtime {

namespace {
// Owner of an economy block as the income scaling reads it.
struct CreditOwner {
    bool present{};
    uint8_t status{};
};

CreditOwner credit_owner(const oa::World& world, const oa::UnitEconomy& block) {
    const auto* owner = oa::world_player_ref(&world, block.player);
    return owner ? CreditOwner{owner->in_use != 0, owner->status} : CreditOwner{};
}

// Scales the energy and metal accumulators of one economy block by the
// player's supply and stock ratios.
void settle_economy_block(oa::UnitEconomy& block, const std::array<float, 10>& ratios) {
    auto words = economy_words(block);
    for (size_t resource = 0; resource < 2; ++resource) {
        const auto first = resource * metal_block_word;
        float values[6];
        for (size_t i = 0; i < 6; ++i)
            values[i] = std::bit_cast<float>(words[first + i]);
        sim::unit_health::scale_resource_block(values, ratios[8 + resource], ratios[6 + resource]);
        for (size_t i = 0; i < 6; ++i)
            words[first + i] = std::bit_cast<uint32_t>(values[i]);
    }
    store_economy_words(block, words);
}
} // namespace

void Match::credit_metal(oa::Unit& target, float amount) {
    const auto owner = credit_owner(state(), target.economy);
    (void)sim::unit_health::credit_metal(
        target.economy.metal.produced, amount, owner.present, owner.status, state().game.difficulty
    );
}

void Match::credit_energy(oa::Unit& target, float amount) {
    const auto owner = credit_owner(state(), target.economy);
    (void)sim::unit_health::credit_energy(
        target.economy.energy.produced, amount, owner.present, owner.status, state().game.difficulty
    );
}

void Match::update_player_economy(size_t player_index) {
    auto& world = state();
    auto& player = match_player(*this, player_index);
    const bool owner_present = player.in_use != 0;
    const auto owner_status = player.status;
    const auto difficulty = world.game.difficulty;

    player.metal_storage = 0.0F;
    player.energy_storage = 0.0F;

    // 0/1 energy/metal accepted, 2/3 gates, 4/5 requested then pools, 6/7
    // produced then stock ratios, 8/9 supply ratios.
    std::array<float, 10> totals{};

    const auto first = oa::oa_unit_slot_from_ref(player.first_unit);
    const auto last = oa::oa_unit_slot_from_ref(player.last_unit);
    for (auto index = first; player.first_unit != 0 && index <= last; ++index) {
        auto& unit = match_unit(*this, index);
        if ((unit.flags & OA_UNIT_FLAG_LIVE) == 0)
            continue;
        const auto& def = match_unit_def(*this, unit);
        auto& energy = unit.economy.energy;
        auto& metal = unit.economy.metal;
        const bool active = (unit.state_flags & active_state_bit) != 0;

        if ((unit.flags & OA_UNIT_FLAG_BUILDING) == 0) {
            if (active || (unit.flags & OA_UNIT_FLAG_MOVE_RATE_MASK) != 0) {
                const auto use = def.energy_use;
                if (use < 0.0F) {
                    (void)sim::unit_health::credit_energy(
                        energy.produced, -use, owner_present, owner_status, difficulty
                    );
                } else {
                    energy.requested += use;
                    if (energy.gate <= 0.0F)
                        energy.accepted += use;
                }
            }
        } else if (active) {
            // A non-negative EnergyUse on an open gate powers the metal makers
            // and extractors; wind and tidal output is never gated.
            const auto use = def.energy_use;
            bool powered = false;
            if (use < 0.0F) {
                (void)sim::unit_health::credit_energy(
                    energy.produced, -use, owner_present, owner_status, difficulty
                );
            } else {
                energy.requested += use;
                powered = energy.gate <= 0.0F;
                if (powered)
                    energy.accepted += use;
            }
            if (def.extracts_metal > 0.0F) {
                if (powered)
                    (void)sim::unit_health::credit_metal(
                        metal.produced,
                        unit.extracted_metal,
                        owner_present,
                        owner_status,
                        difficulty
                    );
            } else if (def.makes_metal != 0) {
                if (powered)
                    (void)sim::unit_health::credit_metal(
                        metal.produced,
                        static_cast<float>(static_cast<uint8_t>(def.makes_metal)),
                        owner_present,
                        owner_status,
                        difficulty
                    );
            } else if (def.wind_generator > 0.0F) {
                (void)sim::unit_health::credit_energy(
                    energy.produced,
                    def.wind_generator * environment_wind_.normalized_strength,
                    owner_present,
                    owner_status,
                    difficulty
                );
            } else if (def.tidal_generator > 0.0F) {
                (void)sim::unit_health::credit_energy(
                    energy.produced,
                    def.tidal_generator * world.game.tidal_strength,
                    owner_present,
                    owner_status,
                    difficulty
                );
            }
        }

        if (unit.build_remaining == 0.0F) {
            (void)sim::unit_health::credit_energy(
                energy.produced, def.energy_make, owner_present, owner_status, difficulty
            );
            (void)sim::unit_health::credit_metal(
                metal.produced, def.metal_make, owner_present, owner_status, difficulty
            );
            player.metal_storage += def.metal_storage;
            player.energy_storage += def.energy_storage;
        }

        // Cloak upkeep is paid whole from the energy store; an
        // unpaid tick drops activation mask 4. Mirrored players skip it.
        if (!owner_present || owner_status != OA_PLAYER_STATUS_MIRRORED) {
            bool paid = false;
            if ((unit.flags & OA_UNIT_FLAG_CLOAK_RUNNING) != 0 &&
                (unit.flags & OA_UNIT_FLAG_CLOAK_LOCKED) == 0 &&
                unit.decloak_until_tick <= world.game.tick) {
                const auto cost = (unit.flags & OA_UNIT_FLAG_MOVE_RATE_MASK) != 0
                                      ? def.cloak_cost_moving
                                      : def.cloak_cost;
                paid = sim::unit_spawn::player_pay_energy(
                    player, energy.requested, static_cast<float>(truncate_low32(cost))
                );
            }
            set_activation(slots_[index], sim::unit_activation::cloaked_mask, paid);
        }

        // Re-read: the activation scripts may have changed the block.
        totals[6] += unit.economy.energy.produced;
        totals[4] += unit.economy.energy.requested;
        totals[0] += unit.economy.energy.accepted;
        totals[2] += unit.economy.energy.gate;
        totals[7] += unit.economy.metal.produced;
        totals[5] += unit.economy.metal.requested;
        totals[1] += unit.economy.metal.accepted;
        totals[3] += unit.economy.metal.gate;
    }

    auto& staging = match_player_economy(*this, player_index);
    const auto energy_produced = staging.energy.produced + totals[6];
    const auto energy_requested = staging.energy.requested + totals[4];
    totals[0] += staging.energy.accepted;
    totals[2] += staging.energy.gate;
    totals[7] += staging.metal.produced;
    const auto metal_requested = staging.metal.requested + totals[5];
    totals[1] += staging.metal.accepted;
    totals[3] += staging.metal.gate;

    if ((player.resource_flags & 1) != 0) {
        player.energy_storage += player.shared_energy_storage;
        player.metal_storage += player.shared_metal_storage;
    }

    // The running totals are narrowed to float for the add, then widened.
    player.energy_produced = energy_produced;
    player.energy_requested = energy_requested;
    player.energy_produced_total =
        static_cast<double>(energy_produced + static_cast<float>(player.energy_produced_total));
    player.energy_requested_total =
        static_cast<double>(energy_requested + static_cast<float>(player.energy_requested_total));
    player.metal_produced = totals[7];
    player.metal_requested = metal_requested;
    player.metal_produced_total =
        static_cast<double>(totals[7] + static_cast<float>(player.metal_produced_total));
    player.metal_requested_total =
        static_cast<double>(metal_requested + static_cast<float>(player.metal_requested_total));

    totals[4] = energy_produced + player.energy;
    totals[5] = totals[7] + player.metal;

    // Supply covers the gated requests first; the stock ratio then covers the
    // accepted requests from what is left.
    for (size_t resource = 0; resource < 2; ++resource) {
        const auto gate = totals[2 + resource];
        auto pool = totals[4 + resource];
        float supply = 1.0F;
        float taken = gate;
        if (pool < gate) {
            supply = pool / gate;
            taken = pool;
        }
        totals[8 + resource] = supply;
        pool -= taken;
        const auto accepted = totals[resource];
        float stock = 1.0F;
        taken = accepted;
        if (pool < accepted) {
            stock = pool / accepted;
            taken = pool;
        }
        totals[6 + resource] = stock;
        totals[4 + resource] = pool - taken;
    }

    player.energy = totals[4];
    if (player.energy_storage < totals[4]) {
        player.energy_wasted_total = static_cast<double>(
            (totals[4] - player.energy_storage) + static_cast<float>(player.energy_wasted_total)
        );
        player.energy = player.energy_storage;
    }
    player.metal = totals[5];
    if (player.metal_storage < totals[5]) {
        player.metal_wasted_total = static_cast<double>(
            (totals[5] - player.metal_storage) + static_cast<float>(player.metal_wasted_total)
        );
        player.metal = player.metal_storage;
    }

    for (auto index = first; player.first_unit != 0 && index <= last; ++index) {
        auto& unit = match_unit(*this, index);
        if ((unit.flags & OA_UNIT_FLAG_LIVE) != 0)
            settle_economy_block(unit.economy, totals);
    }
    settle_economy_block(staging, totals);
}

} // namespace oa::sim::match_runtime
