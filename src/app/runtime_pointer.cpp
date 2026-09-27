// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Pointer tracking, menu activation and match unit picking.
#include "oa/app/runtime.hpp"
#include "oa/sim/weapon_execution/retaliation.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace oa::app {

namespace {

// Wraps kMultiplayerUnavailable to a single COMIX line; the box then fits it.
constexpr int32_t kMultiplayerUnavailableWidth = 0x140;

} // namespace

void Runtime::update_pointer(float x, float y) {
    pointer_x_ = x;
    pointer_y_ = y;
    if (screen_ == Screen::match) {
        match_pointer_x_ = x;
        match_pointer_y_ = y;
        hovered_.reset();
        hovered_match_unit_ = 0;
        const auto hud_point = oa::ui::display_layout::canvas_to_source(
            match_layout_, static_cast<int>(x), static_cast<int>(y)
        );
        // The HUD of a finished match takes no pointer.
        if (!match_finished_ && match_hud_) {
            const oa::ui::gui_input::MenuObject hud{match_hud_->layout.gadgets, -1};
            hovered_ = oa::ui::gui_input::hit_test(hud, hud_point.x, hud_point.y);
            if (hovered_ && *hovered_ < match_hud_->layout.gadgets.size()) {
                const auto& gadget = match_hud_->layout.gadgets[*hovered_];
                const auto action = match_hud_action(gadget.common.name);
                // A grayed order page status button still takes the click and ignores it.
                if (match_gadget_state(gadget) == nullptr &&
                    ((is_build_page_nav(gadget.common.name) && builder_gui_page_count() <= 1) ||
                     (action == "MISSION" && !campaign_mission_) ||
                     !gadget_command_available(gadget)))
                    hovered_.reset();
            }
        }
        if (!hovered_)
            hovered_match_unit_ = pointer_unit_hit(pick_match_units(x, y));
        if (options_.trace_input)
            std::cerr << "input match pointer x=" << x << " y=" << y
                      << " hud=" << (hovered_ ? std::to_string(*hovered_) : "none") << '\n';
        return;
    }
    if (screen_ == Screen::map_selection) {
        const auto& modal_root = resources_.layout.gadgets.front().common;
        x -= static_cast<float>((kCanvasWidth - static_cast<int>(modal_root.width)) / 2);
        y -= static_cast<float>((kCanvasHeight - static_cast<int>(modal_root.height)) / 2);
    }
    const auto origin = panel_origin();
    x -= static_cast<float>(origin.x);
    y -= static_cast<float>(origin.y);
    // The gadget hover's preselected path deliberately stops after its first
    // candidate. Hover discovery comes before selection, so this hit test runs
    // with no selection (-1) even while the button is down.
    const oa::ui::gui_input::MenuObject menu_object{resources_.layout.gadgets, -1};
    const auto previous_hover = hovered_;
    hovered_ =
        oa::ui::gui_input::hit_test(menu_object, static_cast<int32_t>(x), static_cast<int32_t>(y));
    if (hovered_ != previous_hover)
        refresh_help_text();
    if (options_.trace_input) {
        std::cerr << "input pointer screen=" << static_cast<int>(screen_) << " x=" << x
                  << " y=" << y << " hover=";
        if (hovered_)
            std::cerr << *hovered_ << ':' << resources_.layout.gadgets[*hovered_].common.name;
        else
            std::cerr << "none";
        std::cerr << " selected=" << selected_ << '\n';
    }
}

void Runtime::activate() {
    if (options_.trace_input)
        std::cerr << "input activate before state=" << static_cast<int>(state_.state)
                  << " signal=" << static_cast<int>(state_.signal)
                  << " pending=" << static_cast<int>(state_.pending_signal)
                  << " selected=" << selected_ << '\n';
    const menu::Event event{{kFrontendMenuHandle}, 0};
    // These screens' handlers read the layout itself, so the release steps a
    // staged button here first, as the panel pointer handling does.
    if (screen_ == Screen::main_menu || screen_ == Screen::single_player ||
        screen_ == Screen::skirmish || screen_ == Screen::map_selection)
        step_released_button_stage();
    if (screen_ == Screen::main_menu) {
        // MULTI is the extension's to answer: it may take the game over, or
        // let the menu step into the multiplayer states. Without an answer
        // MULTI only says multiplayer is not available; the main menu stays up.
        if (button_result(menu::MenuHandle{kFrontendMenuHandle}, menu::Button::multiplayer) != 0 &&
            eligible_map_names_.empty()) {
            // Game data with no multiplayer map offers no multiplayer at all.
            play_menu_sound(menu::Sound::big_button);
            show_missing_content(MissingContent::multiplayer_maps);
        } else if (
            button_result(menu::MenuHandle{kFrontendMenuHandle}, menu::Button::multiplayer) != 0
        ) {
            const MultiplayerSelection selection =
                extension_.select_multiplayer == nullptr
                    ? MultiplayerSelection::unavailable
                    : extension_.select_multiplayer(extension_.context, *this);
            if (selection == MultiplayerSelection::frontend) {
                menu::handle_event(state_, event, *this);
            } else if (selection != MultiplayerSelection::taken) {
                play_menu_sound(menu::Sound::big_button);
                show_frontend_message(
                    kMultiplayerUnavailable,
                    kMultiplayerUnavailableWidth,
                    entry::message_show_ok,
                    entry::message_fit_width
                );
            }
        } else {
            menu::handle_event(state_, event, *this);
        }
    } else if (
        screen_ == Screen::single_player &&
        button_result(entry::MenuHandle{kFrontendMenuHandle}, entry::Button::skirmish) != 0 &&
        eligible_map_names_.empty()
    ) {
        // Game data with no skirmish map says so in place of the skirmish setup.
        show_missing_content(MissingContent::skirmish_maps);
        selected_ = -1;
        return;
    } else if (screen_ == Screen::single_player)
        entry::handle_single_player_event(state_, event, *this);
    else if (screen_ == Screen::skirmish) {
        const bool started = skirmish::handle_event(
            state_, skirmish_settings_, preferences_, skirmish_ui_, event, *this
        );
        if (started && match_ && !altitude_sight_blocked_)
            enter_match_view();
    } else if (screen_ == Screen::map_selection) {
        map_modal::handle_event(map_modal_, skirmish_settings_, event, *this);
    } else if (
        screen_ == Screen::options || screen_ == Screen::sound || screen_ == Screen::visuals ||
        screen_ == Screen::speeds || screen_ == Screen::music
    ) {
        activate_options_gadget();
        return;
    } else if (screen_ == Screen::new_campaign || screen_ == Screen::any_mission) {
        activate_campaign_gadget();
        return;
    } else if (screen_ == Screen::load_game) {
        activate_load_game_gadget();
        return;
    } else if (screen_ == Screen::campaign_end) {
        activate_campaign_end_gadget();
        return;
    } else if (screen_ == Screen::briefing) {
        activate_briefing_gadget();
        return;
    } else {
        return;
    }
    if (screen_ == Screen::match)
        return;
    // The frontend mode tick runs its unit header step before
    // each dispatcher pass.
    step(frontend::Step::reload_unit_overrides, state_);
    frontend::dispatch(state_, *this, frontend_states_);
    // A menu callback first selects the next dispatcher state; its
    // initialize signal then constructs that screen on the following
    // dispatcher pass.
    // Dispatching unconditionally every rendered frame would repeatedly
    // rebuild MAINMENU while its signal remains initialize, clearing a
    // mouse-down selection before the matching mouse-up arrives.
    step(frontend::Step::reload_unit_overrides, state_);
    frontend::dispatch(state_, *this, frontend_states_);
    if (options_.trace_input)
        std::cerr << "input activate after state=" << static_cast<int>(state_.state)
                  << " signal=" << static_cast<int>(state_.signal)
                  << " pending=" << static_cast<int>(state_.pending_signal)
                  << " screen=" << static_cast<int>(screen_) << '\n';
}

void Runtime::step_released_button_stage() {
    if (selected_ < 0 || static_cast<std::size_t>(selected_) >= resources_.layout.gadgets.size())
        return;
    const auto& gadget = resources_.layout.gadgets[static_cast<std::size_t>(selected_)];
    const auto found = widget_text_stages_.find(gadget.common.name);
    const auto stage =
        static_cast<uint8_t>(found == widget_text_stages_.end() ? 0U : found->second);
    const auto next = oa::ui::gui_input::released_button_stage(gadget, stage);
    if (next != stage)
        set_button_stage(gadget.common.name, next);
}

oa::sim::gameplay_input::ScreenPoint Runtime::project_pick_point(
    const oa::formats::objects3d::FixedVector3& local,
    const oa::formats::objects3d::FixedVector3& world,
    const oa::present::world_renderer::BattlefieldViewport& viewport
) const {
    const auto x = static_cast<int32_t>(
        static_cast<uint32_t>(local.x) + static_cast<uint32_t>(world.x) - (viewport.source_x << 16)
    );
    const auto z = static_cast<int32_t>(
        static_cast<uint32_t>(world.z) - static_cast<uint32_t>(local.z) - (viewport.source_y << 16)
    );
    const auto y =
        static_cast<int32_t>(static_cast<uint32_t>(local.y) + static_cast<uint32_t>(world.y));
    const auto px = static_cast<int16_t>(static_cast<uint32_t>(x) >> 16);
    const auto pz = static_cast<int16_t>(static_cast<uint32_t>(z) >> 16);
    const auto py = static_cast<int16_t>(static_cast<uint32_t>(y) >> 16);
    const auto scale = viewport.scale == 0.0F ? 1.0 : static_cast<double>(viewport.scale);
    return {
        viewport.destination_x + static_cast<int32_t>(std::lround(static_cast<double>(px) * scale)),
        viewport.destination_y +
            static_cast<int32_t>(std::lround(static_cast<double>(pz - (py / 2)) * scale))
    };
}

bool Runtime::hits_projected_bounds(
    const oa::sim::gameplay_input::PickUnit& unit, oa::sim::gameplay_input::ScreenPoint point
) const {
    const auto viewport = live_viewport(
        static_cast<uint32_t>(std::max(0, match_camera_x_)),
        static_cast<uint32_t>(std::max(0, match_camera_z_))
    );
    if (!unit.model)
        return false;
    oa::formats::objects3d::FixedVector3 minimum{
        std::numeric_limits<int32_t>::max(),
        std::numeric_limits<int32_t>::max(),
        std::numeric_limits<int32_t>::max()
    };
    oa::formats::objects3d::FixedVector3 maximum{
        std::numeric_limits<int32_t>::min(),
        std::numeric_limits<int32_t>::min(),
        std::numeric_limits<int32_t>::min()
    };
    bool any = false;
    for (const auto& object : unit.model->objects) {
        for (const auto& vertex : object.vertices) {
            minimum.x = std::min(minimum.x, vertex.x);
            minimum.y = std::min(minimum.y, vertex.y);
            minimum.z = std::min(minimum.z, vertex.z);
            maximum.x = std::max(maximum.x, vertex.x);
            maximum.y = std::max(maximum.y, vertex.y);
            maximum.z = std::max(maximum.z, vertex.z);
            any = true;
        }
    }
    if (!any)
        return false;
    const std::array<oa::formats::objects3d::FixedVector3, 8> corners{
        {{minimum.x, minimum.y, minimum.z},
         {maximum.x, minimum.y, minimum.z},
         {minimum.x, maximum.y, minimum.z},
         {maximum.x, maximum.y, minimum.z},
         {minimum.x, minimum.y, maximum.z},
         {maximum.x, minimum.y, maximum.z},
         {minimum.x, maximum.y, maximum.z},
         {maximum.x, maximum.y, maximum.z}}
    };
    int32_t left = std::numeric_limits<int32_t>::max();
    int32_t top = std::numeric_limits<int32_t>::max();
    int32_t right = std::numeric_limits<int32_t>::min();
    int32_t bottom = std::numeric_limits<int32_t>::min();
    for (const auto& corner : corners) {
        const auto projected = project_pick_point(corner, unit.position, viewport);
        left = std::min(left, projected.x);
        top = std::min(top, projected.y);
        right = std::max(right, projected.x);
        bottom = std::max(bottom, projected.y);
    }
    return point.x >= left && point.x <= right && point.y >= top && point.y <= bottom;
}

std::vector<uint16_t> Runtime::pick_match_units(float x, float y) {
    if (!match_)
        return {};
    const auto viewport = live_viewport(
        static_cast<uint32_t>(match_camera_x_), static_cast<uint32_t>(match_camera_z_)
    );
    const oa::sim::gameplay_input::ScreenPoint point{
        static_cast<int32_t>(x), static_cast<int32_t>(y)
    };
    if (!oa::present::world_renderer::screen_to_map_pixel(viewport, {point.x, point.y}))
        return {};
    std::vector<uint16_t> hits;
    for (auto& slot : match_->world().slots) {
        if (slot.unit_index == 0 || slot.unit == nullptr || slot.record.type_index == 0)
            continue;
        auto* instance = match_->instance(slot.unit_index);
        if (instance == nullptr)
            continue;
        oa::sim::gameplay_input::PickUnit candidate;
        candidate.id = slot.unit_index;
        candidate.position = {
            std::bit_cast<int32_t>(slot.unit->position[0]),
            std::bit_cast<int32_t>(slot.unit->position[1]),
            std::bit_cast<int32_t>(slot.unit->position[2])
        };
        candidate.rotation = {
            std::bit_cast<int16_t>(slot.bank),
            std::bit_cast<int16_t>(slot.yaw),
            std::bit_cast<int16_t>(slot.pitch)
        };
        candidate.model = &instance->model().model();
        if (hits_projected_bounds(candidate, point))
            hits.push_back(slot.unit_index);
    }
    return hits;
}

uint16_t Runtime::first_local_hit(const std::vector<uint16_t>& hits) {
    for (auto id : hits) {
        auto& slot = match_->world().slots[id];
        if (slot.unit != nullptr && slot.unit->type_index &&
            slot.owner_index == match_local_player_ && match_->selectable(id))
            return id;
    }
    return 0;
}

uint16_t Runtime::first_enemy_hit(const std::vector<uint16_t>& hits) {
    for (auto id : hits) {
        auto& slot = match_->world().slots[id];
        if (slot.unit != nullptr && slot.unit->type_index &&
            slot.owner_index != match_local_player_)
            return id;
    }
    return 0;
}

uint16_t Runtime::pointer_unit_hit(const std::vector<uint16_t>& hits) {
    for (auto id : hits) {
        const auto& slot = match_->world().slots[id];
        if (slot.unit != nullptr && slot.record.type_index != 0 &&
            slot.record.owner_index == match_local_player_)
            return id;
    }
    return first_enemy_hit(hits);
}

oa::sim::gameplay_input::OrderCursorHooks Runtime::order_cursor_hooks() {
    return {
        this,
        [](void* context, const World&, const Player& player, const FixedVec3& position) {
            const auto* self = static_cast<Runtime*>(context);
            return self->match_->point_visible(
                player.index,
                {static_cast<uint32_t>(position.x),
                 static_cast<uint32_t>(position.y),
                 static_cast<uint32_t>(position.z)}
            );
        },
        [](void*, const World& world, const FixedVec3& position) {
            return oa::sim::gameplay_input::feature_at_position(world, position);
        },
        [](void* context, const World&, const Unit& actor, const Unit& target) {
            try {
                return static_cast<Runtime*>(context)->match_->weapon_can_reach(
                    actor.id, target.id, 0
                );
            } catch (const std::exception&) {
                return false;
            }
        },
        [](void*, const World& world, const Unit& actor, const FixedVec3& position) {
            return oa::sim::weapon_execution::slot_reaches_point(world, actor, position, 0);
        },
    };
}

std::string_view Runtime::match_hud_action(std::string_view name) const {
    if (name.size() > 3 && (name.starts_with("ARM") || name.starts_with("COR")))
        return name.substr(3);
    return name;
}

bool Runtime::definition_has_weapon(
    const oa::data::unit_definitions::UnitDefinition& definition
) const {
    auto named = [](const std::string& name) {
        if (name.empty())
            return false;
        auto fold = name;
        for (auto& ch : fold)
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        return fold != "noweapon";
    };
    return named(definition.weapon1) || named(definition.weapon2) || named(definition.weapon3);
}

bool Runtime::definition_has_dgun(
    const oa::data::unit_definitions::UnitDefinition& definition
) const {
    if (definition.can_dgun)
        return true;
    const std::array<const std::string*, 3> names{
        &definition.weapon1, &definition.weapon2, &definition.weapon3
    };
    for (const auto* name : names) {
        if (name->empty())
            continue;
        const auto* weapon = weapon_registry_.find(*name);
        if (weapon && (weapon->flags & oa::sim::combat_state::weapon_commandfire_flag) != 0)
            return true;
    }
    return false;
}

bool Runtime::gadget_command_available(const oa::ui::gui_layout::Gadget& gadget) const {
    if (const auto* button = std::get_if<oa::ui::gui_layout::ButtonFields>(&gadget.fields);
        button != nullptr && button->grayed_out)
        return false;
    if (const auto* state = match_gadget_state(gadget))
        return !state->grayed;
    const auto type = oa::sim::unit_spawn::find_type_index(spawn_type_names_, gadget.common.name);
    if (type != 0)
        return true;
    const auto* definition =
        selected_match_unit_ != 0 ? definition_for(selected_match_unit_) : nullptr;
    if (definition == nullptr)
        return true;
    const auto action = match_hud_action(gadget.common.name);
    if (action == "MOVE")
        return definition->can_move;
    if (action == "ATTACK")
        return definition_has_weapon(*definition);
    if (action == "BLAST" || action == "DGUN")
        return definition_has_dgun(*definition);
    if (action == "PATROL")
        return definition->can_patrol;
    if (action == "REPAIR")
        return definition->builder;
    if (action == "RECLAIM")
        return definition->can_reclamate;
    if (action == "CAPTURE")
        return definition->can_capture;
    if (action == "LOAD" || action == "UNLOAD")
        return definition->can_load;
    if (action == "DEFEND" || action == "GUARD")
        return definition->can_guard;
    if (action == "STOP")
        return definition->can_stop;
    return true;
}

} // namespace oa::app
