// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The savegame steps the save and restore tests share: a bank, and a unit's
// orders written to it and restored into another match as the savegame's
// unit walk and unit restore do.
#pragma once

#include "combat_fixture.hpp"

#include "oa/data/persist/save_orders.hpp"
#include "oa/data/persist/save_sections.hpp"

#include <cstdint>
#include <cstdio>

namespace saved_game {

using namespace oa;

// A bank emptied on construction and freed on destruction.
struct ScopedBank {
    data::persist::Bank bank{};

    /// Starts the bank with an empty account table and no open account.
    ScopedBank() { data::persist::bank_reset(&bank); }

    /// Releases the bank's accounts, fields, strings and blobs.
    ~ScopedBank() { data::persist::bank_destroy(&bank); }

    /// Returns the bank.
    ///
    /// @return the bank this object owns
    data::persist::Bank* get() { return &bank; }
};

/// Writes a unit's orders to the open account as "u%04xm%04x" blobs, as the
/// savegame's unit walk does.
///
/// @param match match holding the unit
/// @param slot unit whose orders are written
/// @param[in,out] bank bank with the Units account open
/// @return the number of orders written
inline int32_t write_orders(
    sim::match_runtime::Match& match, const sim::unit_spawn::Slot& slot, data::persist::Bank* bank
) {
    struct Walk {
        sim::match_runtime::Match& match;
        const sim::unit_spawn::Slot& slot;
        data::persist::Bank* bank;
        int32_t count;
    } walk{match, slot, bank, 0};

    match.visit_saved_orders(
        slot.unit_index,
        [](void* context,
           const data::persist::SavedOrder* order,
           const data::persist::SavedGoal* goal) {
            auto& w = *static_cast<Walk*>(context);
            char name[data::persist::save_name_bytes];
            std::snprintf(
                name, sizeof(name), "u%04xm%04x", w.slot.record.id, static_cast<unsigned>(w.count)
            );
            CHECK(
                data::persist::save_write_order(
                    &w.match.state(), &w.slot.record, order, goal, w.bank, name
                )
            );
            ++w.count;
        },
        &walk
    );
    return walk.count;
}

/// Restores a unit's orders from the open account and gives the navigator the
/// head order's goal, as the unit restore does.
///
/// @param[in,out] match match holding the unit
/// @param slot unit whose orders are restored
/// @param[in,out] bank bank with the Units account open
/// @param count number of orders write_orders wrote
inline void restore_orders(
    sim::match_runtime::Match& match,
    const sim::unit_spawn::Slot& slot,
    data::persist::Bank* bank,
    int32_t count
) {
    auto tails = match.saved_order_tails(slot.unit_index);
    for (int32_t index = 0; index < count; ++index) {
        char name[data::persist::save_name_bytes];
        std::snprintf(
            name, sizeof(name), "u%04xm%04x", slot.record.id, static_cast<unsigned>(index)
        );
        data::persist::SavedOrder order;
        data::persist::SavedGoal goal;
        CHECK(
            data::persist::save_read_order(&match.state(), &slot.record, bank, name, &order, &goal)
        );
        (void)match.restore_saved_order(slot.unit_index, order, goal, tails);
    }
    match.install_head_goal(slot.unit_index);
}

} // namespace saved_game
