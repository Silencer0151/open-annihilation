// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The order and build pages: the selection summary, the pages and their
// download buttons, button states and clicks, through the order panel.
#include "oa/app/runtime.hpp"

#include "oa/sim/match_runtime/construction_orders.hpp"
#include "oa/core/weapon_def.h"
#include "oa/ui/gui_layout/gui_gadget.hpp"
#include "oa/sim/match_runtime.hpp"
#include "oa/data/mission_types.hpp"
#include "oa/ui/hud/order_panel.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <variant>

namespace oa::app {

namespace {

namespace hud = oa::ui::hud;

// guis\<name> with its extension replaced by GUI, as the game builds it.
std::string gui_panel_path(const char* name) {
    std::string stem(name);
    if (const auto dot = stem.rfind('.'); dot != std::string::npos)
        stem.resize(dot);
    return "guis/" + stem + ".GUI";
}

bool ascii_iequals(std::string_view left, std::string_view right) {
    return left.size() == right.size() &&
           std::equal(left.begin(), left.end(), right.begin(), [](char a, char b) {
               return std::tolower(static_cast<unsigned char>(a)) ==
                      std::tolower(static_cast<unsigned char>(b));
           });
}

// Mission descriptor bit 0x200, which an order carries as bit 0x02 of its
// command flags: the mission takes a target unit.
constexpr uint32_t kMissionTakesTarget = 0x200;

// FIREORD, MOVEORD, ONOFF and CLOAK draw their status as the frame of their art.
bool shows_status_frame(const oa::ui::gui_layout::Gadget& gadget) {
    return gadget.common.type == oa::ui::gui_layout::GadgetType::button &&
           (static_cast<uint32_t>(gadget.common.attributes) &
            oa::ui::gui_layout::attribute::cycle_frames) != 0;
}

} // namespace

oa::ui::hud::UnitTable Runtime::order_panel_table() {
    oa::World& world = match_->state();
    return {
        world.units,
        static_cast<uint16_t>(std::min<uint32_t>(world.unit_slot_count, 0xffffu)),
        world.unit_defs,
        static_cast<int32_t>(world.unit_def_count)
    };
}

oa::ui::hud::PanelControls Runtime::order_panel_controls() {
    hud::PanelControls controls{};
    controls.user = this;
    // The first gadget after the panel whose name holds `name`.
    controls.find = [](void* user, const char* name) -> int32_t {
        const auto& self = *static_cast<Runtime*>(user);
        if (!self.match_hud_)
            return -1;
        const auto& gadgets = self.match_hud_->layout.gadgets;
        for (std::size_t index = 1; index < gadgets.size(); ++index)
            if (gadgets[index].common.name.find(name) != std::string::npos)
                return static_cast<int32_t>(index);
        return -1;
    };
    const auto set_status = [](void* user, int32_t index, int32_t value) {
        auto& states = static_cast<Runtime*>(user)->match_hud_states_;
        if (index >= 0 && static_cast<std::size_t>(index) < states.size())
            states[static_cast<std::size_t>(index)].status = static_cast<int16_t>(value);
    };
    controls.set_group_value = set_status;
    controls.set_value = set_status;
    controls.disable = [](void* user, int32_t index) {
        auto& states = static_cast<Runtime*>(user)->match_hud_states_;
        if (index >= 0 && static_cast<std::size_t>(index) < states.size())
            states[static_cast<std::size_t>(index)].grayed = true;
    };
    return controls;
}

oa::ui::hud::HudEvents Runtime::order_panel_events() {
    return {
        this,
        [](void* user, const char* name) {
            static_cast<Runtime*>(user)->play_match_interface_sound(name);
        },
        [](void* user, const char* tag, int32_t value) {
            static_cast<Runtime*>(user)->apply_group_order(tag, value);
        },
    };
}

void Runtime::apply_group_order(const char* tag, int32_t value) {
    const auto kind = oa::data::mission_types::index_for_name(tag);
    if (!match_ || kind == oa::data::mission_types::unknown_mission)
        return;
    const auto table = order_panel_table();
    for_each_selected([&](uint16_t id) {
        if (id >= table.unit_count)
            return;
        const auto* def = hud::unit_def(table, table.units[id]);
        if (def != nullptr && hud::group_order_reaches(tag, *def))
            match_->issue_state_order(id, kind, value);
    });
}

void Runtime::issue_group_mission(uint8_t kind, int32_t parameter_1, int32_t parameter_2) {
    namespace runtime = oa::sim::match_runtime;
    if (!match_ || kind == oa::data::mission_types::unknown_mission ||
        kind >= runtime::mission_descriptor_table.size())
        return;
    const auto& world = match_->state();
    const bool takes_target = (runtime::mission_descriptor_table[kind] & kMissionTakesTarget) != 0;
    const uint16_t target = takes_target ? world.game.cursor_unit_id : 0;
    const char* tag = oa::data::mission_types::registered_names()[kind].data();
    const bool queue = queueing();
    const auto table = order_panel_table();
    for_each_selected([&](uint16_t id) {
        if (id >= table.unit_count || (target != 0 && id == target))
            return;
        const auto* def = hud::unit_def(table, table.units[id]);
        if (def == nullptr || !hud::group_order_reaches(tag, *def))
            return;
        try {
            match_->issue_or_cancel_order(
                id, kind, queue, target, nullptr, parameter_1, parameter_2
            );
        } catch (const std::exception& error) {
            status_ = std::string("assign: ") + error.what();
        }
    });
}

oa::ui::hud::SelectionSummary Runtime::summarize_order_panel(hud::OrderPanelState& state) {
    oa::World& world = match_->state();
    uint32_t count = 0;
    const auto* first =
        oa::world_player_units(&world, &world.game.players[match_local_player_], &count);
    const auto first_index = first != nullptr ? oa::world_unit_slot(&world, first) : 1u;
    const auto last_index = first != nullptr ? first_index + count - 1u : 0u;
    const auto summary = hud::summarize_selection(
        state,
        order_panel_table(),
        static_cast<uint16_t>(first_index),
        static_cast<uint16_t>(last_index)
    );
    state.frame_flags = summary.frame_flags;
    state.order_flags = summary.order_flags;
    state.order_flags2 = summary.order_flags2;
    return summary;
}

std::string Runtime::match_side_name_prefix() const {
    const auto view = match_view_player();
    const auto side = static_cast<std::size_t>(
        view < skirmish_settings_.slots.size() ? skirmish_settings_.slots[view].side : 0
    );
    if (side < side_table_.count) {
        const auto& prefix = side_table_.sides[side].name_prefix;
        return std::string(prefix, strnlen(prefix, sizeof prefix));
    }
    auto prefix = match_side_prefix();
    for (auto& c : prefix)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return prefix;
}

void Runtime::bind_gadget_gaf_art() {
    if (!match_hud_)
        return;
    for (const auto& gadget : match_hud_->layout.gadgets)
        if (gadget.common.gaf_file &&
            gaf_sequence(match_hud_->sprites, gadget.common.name) == nullptr)
            append_gaf_file(match_hud_->sprites, "anims/" + gadget.common.name + "_gadget.gaf");
}

oa::ui::hud::PanelLoader Runtime::order_panel_loader() {
    hud::PanelLoader loader{};
    loader.user = this;
    loader.close_to_root = [](void*) { return true; };
    loader.is_loaded = [](void* user, const char* name) {
        return ascii_iequals(static_cast<Runtime*>(user)->match_hud_panel_, gui_panel_path(name));
    };
    loader.load = [](void* user, const char* name, const oa::Unit*, int32_t) {
        return static_cast<Runtime*>(user)->load_match_hud_layout(gui_panel_path(name));
    };
    loader.downloads = unit_table_.tables.downloads.groups;
    loader.download_count = unit_table_.tables.downloads.count;
    // The linked gadget becomes a unit button named after the unit, ungreyed,
    // with its art from the unit's _gadget GAF.
    loader.link_button = [](void* user, int32_t index, const char* unit_name) {
        auto& self = *static_cast<Runtime*>(user);
        if (!self.match_hud_ || index < 0 ||
            static_cast<std::size_t>(index) >= self.match_hud_->layout.gadgets.size())
            return;
        auto& gadget = self.match_hud_->layout.gadgets[static_cast<std::size_t>(index)];
        gadget.common.name = unit_name;
        gadget.common.common_attributes = static_cast<int8_t>(hud::kCommonUnitButton);
        gadget.common.gaf_file = true;
        if (auto* button = std::get_if<oa::ui::gui_layout::ButtonFields>(&gadget.fields))
            button->grayed_out = false;
        if (static_cast<std::size_t>(index) < self.match_hud_states_.size())
            self.match_hud_states_[static_cast<std::size_t>(index)].grayed = false;
    };
    loader.redraw = [](void* user) { static_cast<Runtime*>(user)->bind_gadget_gaf_art(); };
    loader.exists = [](void* user, const char* name) {
        return static_cast<Runtime*>(user)->assets_.file_size(gui_panel_path(name)) != 0;
    };
    loader.format_counts = [](void* user, const oa::Unit&) {
        static_cast<Runtime*>(user)->refresh_build_page(false);
    };
    loader.check_validity = [](void* user) {
        static_cast<Runtime*>(user)->refresh_build_page(true);
    };
    return loader;
}

oa::ui::hud::BuildPanelHost Runtime::build_panel_host() {
    hud::BuildPanelHost host{};
    host.user = this;
    host.type_for_name = [](void* user, const char* name) {
        return oa::sim::unit_spawn::find_type_index(
            static_cast<Runtime*>(user)->spawn_type_names_, name
        );
    };
    host.order_click = [](void* user, const char* name) {
        auto& self = *static_cast<Runtime*>(user);
        const auto& gadgets = self.match_hud_->layout.gadgets;
        for (std::size_t index = 1; index < gadgets.size(); ++index)
            if (gadgets[index].common.name == name)
                return self.run_match_order_button(index, name);
        return false;
    };
    host.shift_down = [](void* user) {
        return static_cast<Runtime*>(user)->control_key_down(oa::ui::gui_input::ControlKey::shift);
    };
    host.change_queue = [](void* user, const char* name, oa::Unit& builder, int32_t count) {
        auto& self = *static_cast<Runtime*>(user);
        const auto type_for_name = [](void* context, const char* unit_name) {
            return oa::sim::unit_spawn::find_type_index(
                static_cast<Runtime*>(context)->spawn_type_names_, unit_name
            );
        };
        const auto change = hud::classify_build_queue_change(
            name,
            builder,
            self.match_view_player(),
            count,
            type_for_name,
            user,
            self.order_panel_events()
        );
        if (change.kind == hud::BuildQueueKind::none) {
            self.status_ = std::string(name) + " is not implemented yet.";
            std::cerr << "unsupported operation: " << self.status_ << '\n';
            return;
        }
        try {
            self.match_->change_queued_count(
                builder.id, oa::data::mission_types::index_for_name(change.tag), change.type, count
            );
            self.status_ = std::string("Build ") + name + " x" +
                           std::to_string(self.match_->queued_build_count(builder.id, change.type));
        } catch (const std::exception& error) {
            self.status_ = std::string("build queue: ") + error.what();
            std::cerr << "unsupported operation: " << self.status_ << '\n';
        }
    };
    host.stockpiles = [](void* user, const oa::Unit& unit) {
        const auto* weapon = oa::world_weapon_def(
            &static_cast<Runtime*>(user)->match_->state(), unit.weapons[0].def
        );
        return weapon != nullptr && (weapon->flags & OA_WEAPON_FLAG_STOCKPILE) != 0;
    };
    host.format_counts = [](void* user, const oa::Unit&) {
        static_cast<Runtime*>(user)->refresh_build_page(false);
    };
    return host;
}

void Runtime::toggle_order_button(std::size_t index) {
    if (!match_ || !match_hud_ || index >= match_hud_->layout.gadgets.size())
        return;
    oa::World& world = match_->state();
    auto state = hud::order_panel_load(world.game);
    // A control that is not a toggle leaves the order words as they were,
    // and storing them back changes nothing.
    std::ignore = hud::order_panel_toggle(
        state,
        order_panel_table(),
        order_panel_controls(),
        static_cast<int32_t>(index),
        match_hud_->layout.gadgets[index].common.name.c_str(),
        order_panel_events()
    );
    hud::order_panel_store(world.game, state);
}

const Runtime::MatchGadgetState*
Runtime::match_gadget_state(const oa::ui::gui_layout::Gadget& gadget) const {
    if (!match_hud_ || !shows_status_frame(gadget))
        return nullptr;
    const auto& gadgets = match_hud_->layout.gadgets;
    if (&gadget < gadgets.data() || &gadget >= gadgets.data() + gadgets.size())
        return nullptr;
    const auto index = static_cast<std::size_t>(&gadget - gadgets.data());
    return index < match_hud_states_.size() ? &match_hud_states_[index] : nullptr;
}

std::optional<std::size_t> Runtime::match_status_frame(std::size_t index) const {
    if (!match_hud_ || index >= match_hud_->layout.gadgets.size())
        return std::nullopt;
    const auto& gadget = match_hud_->layout.gadgets[index];
    const auto* state = match_gadget_state(gadget);
    if (state == nullptr)
        return std::nullopt;
    const auto* sequence = gaf_sequence(match_hud_->sprites, gadget.common.name);
    if (sequence == nullptr)
        sequence = gaf_sequence(match_hud_->shared_sprites, gadget.common.name);
    if (sequence == nullptr || sequence->frames.empty())
        return std::nullopt;
    const auto last = sequence->frames.size() - 1U;
    if (state->grayed)
        return last;
    return std::min(static_cast<std::size_t>(std::max<int16_t>(state->status, 0)), last);
}

} // namespace oa::app
