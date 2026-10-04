// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Pointer tracking, menu activation and match unit picking.
#include "oa/app/runtime.hpp"
#include "match_models.hpp"
#include "oa/app/hook_call.hpp"
#include "oa/sim/spatial_state/spatial.hpp"
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

/// Moves a pick candidate of another machine's player to where the frame
/// last drawn showed it, on its playout (mirrored_pose).
///
/// @param models the match's renderer state; null before the match is drawn
/// @param world the match's World
/// @param unit the unit
/// @param[in,out] candidate the candidate, at the unit's simulated place
void pick_where_drawn(
    const MatchModels* models,
    const World& world,
    const Unit& unit,
    oa::sim::gameplay_input::PickUnit& candidate
) {
    if (models == nullptr)
        return;
    const auto pose = mirrored_pose(
        *models, world, static_cast<uint16_t>(unit.id), models->presentation.drawn_moment
    );
    if (!pose)
        return;
    candidate.position = {pose->position.x, pose->position.y, pose->position.z};
    candidate.rotation = {pose->bank, static_cast<int16_t>(pose->heading), pose->pitch};
}

} // namespace

void Runtime::update_pointer(float x, float y) {
    pointer_x_ = x;
    pointer_y_ = y;
    if (screen_ == Screen::match) {
        match_pointer_x_ = x;
        match_pointer_y_ = y;
        hovered_.reset();
        const auto hud_point = hud_source_point(x, y);
        // The HUD of a finished match takes no pointer.
        if (!match_finished_ && match_hud_) {
            const oa::ui::gui_input::MenuObject hud{match_hud_->layout.gadgets, -1};
            hovered_ = oa::ui::gui_input::hit_test(hud, hud_point.x, hud_point.y);
            if (hovered_ && *hovered_ < match_hud_->layout.gadgets.size()) {
                const auto& gadget = match_hud_->layout.gadgets[*hovered_];
                const auto action = match_hud_action(gadget.common.name);
                // A grayed order page status button still takes the click and
                // ignores it. Build page navigation is never on a paused menu.
                if (match_gadget_state(gadget) == nullptr &&
                    ((!pause_menu_shown() && is_build_page_nav(gadget.common.name) &&
                      builder_gui_page_count() <= 1) ||
                     (action == "MISSION" && !campaign_mission_) ||
                     !gadget_command_available(gadget)))
                    hovered_.reset();
            }
        }
        pick_cursor_unit();
        build_tool_motion(x, y);
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
        // MULTI is the extensions' to answer (network play's, unless another
        // takes it first): one may take the game over, or let the menu step
        // into the multiplayer states. Without an answer the main menu stays
        // up and MULTI does nothing.
        if (button_result(menu::MenuHandle{kFrontendMenuHandle}, menu::Button::multiplayer) != 0 &&
            eligible_map_names_.empty()) {
            // Game data with no multiplayer map offers no multiplayer at all.
            play_menu_sound(menu::Sound::big_button);
            show_missing_content(MissingContent::multiplayer_maps);
        } else if (
            button_result(menu::MenuHandle{kFrontendMenuHandle}, menu::Button::multiplayer) != 0
        ) {
            const MultiplayerSelection selection =
                call_hook_or_raise<&Extension::select_multiplayer>(extension_, *this);
            if (selection == MultiplayerSelection::frontend)
                menu::handle_event(state_, event, *this);
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

oa::ui::display_layout::Point Runtime::game_screen_point(float x, float y) const {
    namespace layout = oa::ui::display_layout;
    const auto column = static_cast<int>(x);
    const auto row = static_cast<int>(y);
    if (screen_ != Screen::match || !battlefield_contains(x, y))
        return layout::canvas_to_source(match_layout_, column, row);
    const auto zoom = match_zoom() == 0.0F ? 1.0 : static_cast<double>(match_zoom());
    // As the battlefield maps a canvas pixel to its map pixel
    // (world_renderer::screen_to_map_pixel), from the view as it is drawn:
    // between map pixels in the accelerated tier, so that the pointer picks
    // what is drawn under it. The offset never carries a point past the
    // last map pixel of Game's battlefield (Game.battlefield_rect), where
    // hover and picking would refuse it; a point the view on its camera's
    // map pixel maps past it maps as it always has.
    const auto mapped = [zoom](int32_t from_edge, double offset, int32_t visible) {
        const double on_camera = static_cast<double>(from_edge) / zoom;
        const auto whole = std::llround(on_camera);
        const auto drawn = std::llround(on_camera + offset);
        return static_cast<int>(
            std::min(drawn, std::max(whole, static_cast<long long>(visible) - 1))
        );
    };
    const auto offset = view_offset();
    return {
        layout::kSourceLeft + mapped(column - match_layout_.left, offset.x, visible_map_width()),
        layout::kSourceTop + mapped(row - match_layout_.top, offset.y, visible_map_height())
    };
}

oa::ui::display_layout::Point Runtime::game_screen_canvas(int32_t x, int32_t y) const {
    namespace layout = oa::ui::display_layout;
    const bool in_view = x >= layout::kSourceLeft && y >= layout::kSourceTop &&
                         x < layout::kSourceLeft + visible_map_width() &&
                         y < layout::kSourceTop + visible_map_height();
    if (screen_ != Screen::match || !in_view)
        return layout::source_to_canvas(match_layout_, x, y);
    const auto zoom = match_zoom() == 0.0F ? 1.0 : static_cast<double>(match_zoom());
    // Where the view as it is drawn shows the point.
    const auto offset = view_offset();
    return {
        match_layout_.left + static_cast<int>(std::lround(
                                 (static_cast<double>(x - layout::kSourceLeft) - offset.x) * zoom
                             )),
        match_layout_.top + static_cast<int>(std::lround(
                                (static_cast<double>(y - layout::kSourceTop) - offset.y) * zoom
                            ))
    };
}

oa::sim::selection::VisibleLists Runtime::on_screen_lists() {
    oa::sim::selection::VisibleLists lists{};
    lists.units = on_screen_units_.data();
    lists.unit_capacity = static_cast<uint32_t>(on_screen_units_.size());
    lists.radar = radar_state_.hot_units.data();
    lists.radar_capacity = static_cast<uint32_t>(radar_state_.hot_units.size());
    // The radar renderer places its picture in Game and returns the blips it
    // listed; selection takes both from here rather than reading them itself.
    if (match_) {
        lists.radar_count = static_cast<int32_t>(radar_state_.hot_unit_count);
        lists.radar_picture = match_->state().game.radar_picture_rect;
    }
    return lists;
}

oa::sim::selection::Hooks Runtime::selection_hooks() {
    oa::sim::selection::Hooks hooks{};
    hooks.context = this;
    hooks.player_sees_unit =
        [](void* context, const World&, const Player& player, const Unit& unit) {
            auto& self = *static_cast<Runtime*>(context);
            try {
                return self.match_ != nullptr && self.match_->unit_visible(player.index, unit.id);
            } catch (const std::exception&) {
                return false;
            }
        };
    hooks.pointer_hits_unit =
        [](void* context, const World& world, const Unit& unit, int32_t x, int32_t y) {
            auto& self = *static_cast<Runtime*>(context);
            auto* instance = self.match_ != nullptr ? self.match_->instance(unit.id) : nullptr;
            if (instance == nullptr)
                return false;
            oa::sim::gameplay_input::PickUnit candidate;
            candidate.id = unit.id;
            candidate.position = {unit.position.x, unit.position.y, unit.position.z};
            candidate.rotation = {unit.bank, static_cast<int16_t>(unit.heading), unit.pitch};
            pick_where_drawn(self.drawn_match_models(), world, unit, candidate);
            candidate.model = &instance->model().model();
            // A unit without a root object has no box to pick.
            if (candidate.model->objects.empty())
                return false;
            const oa::sim::gameplay_input::Camera camera{
                static_cast<int32_t>(world.game.camera_x), static_cast<int32_t>(world.game.camera_y)
            };
            return oa::sim::gameplay_input::hits_root_bounds(candidate, camera, {x, y});
        };
    hooks.play_sound = [](void* context, const char* name) {
        static_cast<Runtime*>(context)->play_match_interface_sound(name);
    };
    // A unit selected alone speaks its selection line to its owner.
    hooks.speak = [](void* context, const Unit& unit, uint32_t category) {
        auto& self = *static_cast<Runtime*>(context);
        if (self.match_ == nullptr || unit.id >= self.match_->world().slots.size())
            return;
        auto& slot = self.match_->world().slots[unit.id];
        if (slot.unit == nullptr)
            return;
        try {
            self.offline_services_.command_sound(slot, category);
        } catch (const std::exception&) {
            // Without the announcement runtime, or for a type outside its
            // catalog, the selection stays silent.
        }
    };
    hooks.reset_command = [](void* context) {
        auto& self = *static_cast<Runtime*>(context);
        self.reset_match_command();
        self.pending_build_type_ = 0;
    };
    hooks.selection_cleared = [](void* context) {
        static_cast<Runtime*>(context)->selected_match_unit_ = 0;
    };
    hooks.center_camera = [](void* context, const FixedVec3& position, bool) {
        auto& self = *static_cast<Runtime*>(context);
        self.set_camera_position(
            static_cast<int32_t>(static_cast<int16_t>(static_cast<uint32_t>(position.x) >> 16)) -
                self.visible_map_width() / 2,
            static_cast<int32_t>(static_cast<int16_t>(static_cast<uint32_t>(position.z) >> 16)) -
                self.visible_map_height() / 2,
            0
        );
        self.render_match_surface();
    };
    hooks.stop_follow = [](void* context) {
        static_cast<Runtime*>(context)->stop_match_tracking();
    };
    return hooks;
}

const MatchModels* Runtime::drawn_match_models() const {
    return match_models_ && match_models_->match == match_.get() ? match_models_.get() : nullptr;
}

void Runtime::rebuild_on_screen_units() {
    if (!match_)
        return;
    auto& world = match_->state();
    if (on_screen_units_.size() != world.unit_slot_count) {
        // A new match: nothing is under the cursor yet.
        on_screen_units_.assign(world.unit_slot_count, 0);
        world.game.cursor_unit_id = 0;
        world.game.cursor_feature = oa::sim::spatial_state::no_feature;
    }
    offline_services_.set_on_screen_test(
        [](const void* context, uint16_t unit) {
            auto& self = *static_cast<Runtime*>(const_cast<void*>(context));
            return self.match_ != nullptr && oa::sim::selection::unit_listed(
                                                 self.match_->state(), self.on_screen_lists(), unit
                                             );
        },
        this
    );
    oa::sim::selection::collect_visible_units(world, on_screen_lists(), selection_hooks());
}

void Runtime::refresh_on_screen_view() {
    if (!match_ || !selected_tnt_)
        return;
    // The view a frame would show: the camera held on the map, bound to the
    // Game block, and the units on screen in it.
    const auto [map_width, map_height] = shown_map_size();
    match_camera_x_ = std::clamp(match_camera_x_, 0, std::max(0, map_width - visible_map_width()));
    match_camera_z_ =
        std::clamp(match_camera_z_, 0, std::max(0, map_height - visible_map_height()));
    bind_match_view();
    rebuild_on_screen_units();
}

void Runtime::pick_cursor_unit(bool refresh_view) {
    namespace input = oa::sim::gameplay_input;
    if (!match_ || screen_ != Screen::match)
        return;
    if (refresh_view)
        refresh_on_screen_view();
    auto& game = match_->state().game;
    const auto point = game_screen_point(pointer_x_, pointer_y_);
    game.pointer_state[0] = static_cast<uint32_t>(point.x);
    game.pointer_state[1] = static_cast<uint32_t>(point.y);
    refresh_pointer_area();
    // The ground under the pointer, its cell and the feature on it, which
    // the unit panel shows when no unit is under the cursor.
    if (const auto ground = match_world_point(pointer_x_, pointer_y_)) {
        input::set_pointer_position(game, {(*ground)[0], (*ground)[1], (*ground)[2]});
        store_cursor_cell(*ground);
    }
    const auto flags = input::pointer_flags(game);
    // Placing a building tests its site over the view instead; off the view
    // and the radar the unit picked last stays.
    const bool placing =
        (flags & input::pointer_over_view) != 0 && match_command_ == MatchCommand::build;
    if (!placing && (flags & (input::pointer_over_view | input::pointer_over_radar)) != 0 &&
        on_screen_units_.size() == match_->state().unit_slot_count)
        game.cursor_unit_id = oa::sim::selection::unit_under_pointer(
            match_->state(), on_screen_lists(), selection_hooks()
        );
    hovered_match_unit_ = game.cursor_unit_id;
}

oa::sim::gameplay_input::OrderCursorHooks Runtime::order_cursor_hooks() {
    oa::sim::gameplay_input::OrderCursorHooks hooks{
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
        [](void* context, const World& world, const Unit& actor, const FixedVec3& position) {
            const auto* self = static_cast<Runtime*>(context);
            return oa::sim::weapon_execution::slot_reaches_point(
                world,
                actor,
                position,
                0,
                self->match_ ? self->match_->rules_view() : oa::data::match_rules::MatchRulesView{}
            );
        },
    };
    if (match_)
        hooks.rules = match_->rules_view();
    const auto& fixes = ui_rules().interface_fixes;
    hooks.pad_load_cursor =
        fixes.enabled &&
        fixes.fixes.contains(oa::data::mod_profile::UiInterfaceFixesFixes::pad_cursor);
    return hooks;
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
    return order_command_available(gadget.common.name);
}

bool Runtime::order_command_available(std::string_view name) const {
    const auto* definition =
        selected_match_unit_ != 0 ? definition_for(selected_match_unit_) : nullptr;
    if (definition == nullptr)
        return true;
    const auto action = match_hud_action(name);
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
