// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "oa/ui/hud/boundary.hpp"
#include "oa/ui/hud/order_panel.hpp"

#include "oa/core/world.h"

#include <map>
#include <string>
#include <vector>

namespace hud_test {

/// A World with unit and type tables and linked player records.
struct TestWorld {
    oa::World* world = oa::world_create();

    explicit TestWorld(uint32_t unit_slots = 64, uint32_t unit_defs = 8) {
        oa::WorldCapacity capacity{unit_slots, unit_defs, 4};
        oa::world_alloc_tables(world, &capacity);
        for (uint32_t index = 0; index < OA_PLAYER_COUNT; ++index) {
            auto& player = world->game.players[index];
            player.index = 10;
            player.info = oa::oa_ref_from_index(index);
            player.economy = oa::oa_ref_from_index(index);
        }
    }

    ~TestWorld() { oa::world_destroy(world); }

    TestWorld(const TestWorld&) = delete;
    TestWorld& operator=(const TestWorld&) = delete;

    oa::Game& game() { return world->game; }

    oa::Player& player(uint32_t index) { return world->game.players[index]; }

    oa::Player& add_player(uint8_t index, uint8_t status) {
        auto& p = player(index);
        p.in_use = 1;
        p.status = status;
        p.index = index;
        p.player_id = 100u + index;
        p.unit_count = 1;
        p.units_created = 1;
        std::snprintf(p.name, sizeof p.name, "Player %u", static_cast<unsigned>(index));
        return p;
    }

    /// Gives player `index` the unit slots [first, last].
    void give_range(uint8_t index, uint32_t first, uint32_t last) {
        player(index).first_unit = oa::oa_unit_ref_from_slot(first);
        player(index).last_unit = oa::oa_unit_ref_from_slot(last);
        for (uint32_t slot = first; slot <= last; ++slot) {
            auto& unit = world->units[slot];
            unit.id = static_cast<uint16_t>(slot);
            unit.owner = oa::oa_ref_from_index(index);
            unit.owner_index = index;
        }
    }

    oa::Unit& unit(uint32_t slot) { return world->units[slot]; }

    /// Makes slot a live unit of type `type`.
    oa::Unit& spawn(uint32_t slot, uint16_t type) {
        auto& u = unit(slot);
        u.type_index = type;
        u.def = oa::oa_ref_from_index(type);
        u.flags |= OA_UNIT_FLAG_LIVE;
        return u;
    }
};

/// Named controls and a panel loader that records what the code asks for.
struct FakePanel {
    std::vector<std::string> names;
    std::map<std::string, int32_t> values;
    std::map<std::string, int32_t> groups;
    std::map<std::string, int32_t> states;
    std::map<std::string, std::string> texts;
    std::vector<std::string> loads;
    std::vector<int32_t> load_flags;
    std::vector<std::pair<int32_t, std::string>> links;
    std::string focused;
    int closes = 0;
    int redraws = 0;
    bool loaded_answer = true;

    explicit FakePanel(std::vector<std::string> controls = {}) : names(std::move(controls)) {}

    int32_t index(const std::string& name) const {
        for (size_t i = 0; i < names.size(); ++i)
            if (names[i] == name)
                return static_cast<int32_t>(i);
        return -1;
    }

    const std::string& name(int32_t index) const { return names[static_cast<size_t>(index)]; }

    oa::ui::hud::PanelControls controls() {
        oa::ui::hud::PanelControls c{};
        c.user = this;
        c.find = [](void* u, const char* n) { return static_cast<FakePanel*>(u)->index(n); };
        c.set_group_value = [](void* u, int32_t i, int32_t v) {
            auto* self = static_cast<FakePanel*>(u);
            self->groups[self->name(i)] = v;
        };
        c.set_value = [](void* u, int32_t i, int32_t v) {
            auto* self = static_cast<FakePanel*>(u);
            self->values[self->name(i)] = v;
        };
        c.set_state = [](void* u, int32_t i, int32_t v) {
            auto* self = static_cast<FakePanel*>(u);
            self->states[self->name(i)] = v;
        };
        c.value = [](void* u, int32_t i) {
            auto* self = static_cast<FakePanel*>(u);
            const auto found = self->values.find(self->name(i));
            return found != self->values.end() ? found->second : 0;
        };
        c.text = [](void* u, int32_t i) {
            auto* self = static_cast<FakePanel*>(u);
            return self->texts[self->name(i)].c_str();
        };
        c.set_text = [](void* u, int32_t i, const char* t) {
            auto* self = static_cast<FakePanel*>(u);
            self->texts[self->name(i)] = t;
        };
        c.focus = [](void* u, int32_t i) {
            auto* self = static_cast<FakePanel*>(u);
            self->focused = self->name(i);
        };
        return c;
    }

    oa::ui::hud::PanelLoader loader() {
        oa::ui::hud::PanelLoader l{};
        l.user = this;
        l.close_to_root = [](void* u) {
            ++static_cast<FakePanel*>(u)->closes;
            return true;
        };
        l.is_loaded = [](void* u, const char*) {
            return static_cast<FakePanel*>(u)->loaded_answer;
        };
        l.load = [](void* u, const char* name, const oa::Unit*, int32_t flags) {
            auto* self = static_cast<FakePanel*>(u);
            self->loads.emplace_back(name);
            self->load_flags.push_back(flags);
            return true;
        };
        l.link_button = [](void* u, int32_t gadget, const char* unit_name) {
            static_cast<FakePanel*>(u)->links.emplace_back(gadget, unit_name);
        };
        l.redraw = [](void* u) { ++static_cast<FakePanel*>(u)->redraws; };
        return l;
    }
};

struct Sounds {
    std::vector<std::string> played;

    oa::ui::hud::HudEvents events() {
        return {
            this,
            [](void* u, const char* n) { static_cast<Sounds*>(u)->played.emplace_back(n); },
            nullptr
        };
    }
};

} // namespace hud_test
