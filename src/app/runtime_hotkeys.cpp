// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Match hotkeys, selection commands and overlays.
#include "oa/app/runtime.hpp"
#include "oa/app/hook_call.hpp"
#include "oa/ui/decoded.hpp"
#include "engine_settings_state.hpp"
#include "match_models.hpp"
#include "oa/sim/speed.hpp"
#include "oa/ui/console/game_fields.hpp"
#include "oa/ui/hud/chat_panel.hpp"
#include "oa/ui/hud/unit_info.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

namespace oa::app {

bool Runtime::handle_match_hotkey(const SDL_KeyboardEvent& key) {
    if (screen_ != Screen::match || !match_ || key.repeat)
        return false;
    // F4 pins the kills board out (Game.graphics_flags 0x80).
    if (key.key == SDLK_F4 || key.scancode == SDL_SCANCODE_F4)
        return handle_console_hotkey(key);
    // F1 opens the unit info panel; Shift+F1 pins the unit under the cursor
    // instead (Game.pinned_unit_a), or unpins without one.
    if (key.key == SDLK_F1 || key.scancode == SDL_SCANCODE_F1) {
        auto& game = match_->state().game;
        if ((SDL_GetModState() & SDL_KMOD_SHIFT) != 0) {
            game.pinned_unit_a_valid = game.cursor_unit_id != 0 ? 1U : 0U;
            if (game.cursor_unit_id != 0)
                game.pinned_unit_a = game.cursor_unit_id;
            return true;
        }
        // Without a unit to show, F1 does nothing.
        std::ignore = open_unit_info();
        return true;
    }
    if (key.key == SDLK_F2 || key.scancode == SDL_SCANCODE_F2) {
        if (match_paused_)
            resume_match_pause();
        else
            show_match_pause_menu();
        return true;
    }
    // F3 goes to the next unit the message log reported.
    if (key.key == SDLK_F3 || key.scancode == SDL_SCANCODE_F3) {
        oa::sim::messages::cycle_reported_units(match_->state(), message_hooks());
        return true;
    }
    if (key.key == SDLK_F12 || key.scancode == SDL_SCANCODE_F12) {
        oa::sim::messages::clear_messages(match_->state().game);
        return true;
    }
    // Pause flips the pause bit and opens no menu; a finished match keeps it
    // as it is.
    if (key.key == SDLK_PAUSE || key.scancode == SDL_SCANCODE_PAUSE) {
        // Pause is a console hotkey and a match is running, so it is always
        // taken.
        if (!match_finished_)
            std::ignore = handle_console_hotkey(key);
        return true;
    }
    if ((SDL_GetModState() & SDL_KMOD_CTRL) != 0 &&
        (key.key == SDLK_F9 || key.scancode == SDL_SCANCODE_F9)) {
        capture_screenshot();
        return true;
    }
    if (chat_composing_) {
        if (key.key == SDLK_ESCAPE) {
            close_chat_line();
            return true;
        }
        if (key.key == SDLK_RETURN || key.key == SDLK_KP_ENTER) {
            submit_chat_line();
            return true;
        }
        if (key.key == SDLK_BACKSPACE && !chat_buffer_.empty()) {
            chat_buffer_.pop_back();
            return true;
        }
        return true;
    }
    // The surrender confirmation answers Enter as No, its Enter default;
    // escape_match_menu answers its Escape.
    if (match_paused_ && (key.key == SDLK_RETURN || key.key == SDLK_KP_ENTER) && enter_match_menu())
        return true;
    // The unit info panel's Enter and Escape defaults are both DONE.
    if (unit_info_panel_ &&
        (key.key == SDLK_RETURN || key.key == SDLK_KP_ENTER || key.key == SDLK_ESCAPE)) {
        press_unit_info_done();
        return true;
    }
    // The in-game menu's and the tab menu's panels, and the preferences a
    // match opens, take the keys they answer while they hold the keyboard.
    if (press_match_panel_key(key))
        return true;
    if (handle_console_hotkey(key))
        return true;
    // Escape takes back an armed command and keeps the selection; with none
    // armed it drops the selection. With the Escape opens the game menu
    // setting on and nothing selected, it opens the in-game menu as F2 does,
    // which does not stop a shared game; with the menu open, the event
    // handler closes it.
    if (key.key == SDLK_ESCAPE) {
        if (match_paused_)
            return false;
        if (match_command_ != MatchCommand::none || pending_build_type_ != 0) {
            reset_match_command();
            pending_build_type_ = 0;
            oa::sim::gameplay_input::set_pointer_command(
                match_->state().game, oa::sim::gameplay_input::OrderCommand::default_order
            );
            apply_match_hud_for_selection();
            status_ = "Command cancelled";
            return true;
        }
        if (!match_finished_ && !has_local_selection() &&
            EngineSettingsState::escape_opens_menu(*this)) {
            show_match_pause_menu();
            return true;
        }
        clear_local_selection();
        apply_match_hud_for_selection();
        status_ = "Selection cleared";
        return true;
    }
    if (match_paused_)
        return false;
    const auto mods = SDL_GetModState();
    if ((mods & SDL_KMOD_CTRL) != 0 && (key.key == SDLK_D || key.scancode == SDL_SCANCODE_D) &&
        selected_match_unit_ != 0) {
        match_->toggle_self_destruct(selected_local_ids());
        status_ = "Self-destruct";
        return true;
    }
    const bool ctrl = (mods & SDL_KMOD_CTRL) != 0;
    const bool alt = (mods & SDL_KMOD_ALT) != 0;
    const bool shift = (mods & SDL_KMOD_SHIFT) != 0;
    const auto sym = key.key;
    if (const int n = squad_from_key(key); n != 0) {
        if (ctrl) {
            assign_squad(n);
            return true;
        }
        // A digit picks its squad with Alt down, or with Alt up
        // once SwitchAlt is on; otherwise it picks build page digit - 1.
        const bool switch_alt =
            (match_->state().game.graphics_flags & oa::ui::console::graphics_flag::switch_alt) != 0;
        if (alt != switch_alt)
            select_squad(n, shift);
        else
            show_match_page_by_key(n - 1);
        return true;
    }
    if (ctrl) {
        if (sym == SDLK_A) {
            select_units_matching([](const oa::sim::unit_spawn::Slot&) { return true; });
            return true;
        }
        if (sym == SDLK_C) {
            select_and_follow_commander(shift);
            return true;
        }
        if (sym == SDLK_B) {
            select_units_matching([&](const oa::sim::unit_spawn::Slot& slot) {
                const auto* def = definition_for(slot.unit_index);
                return def && def->builder && def->bm_code != 0;
            });
            return true;
        }
        if (sym == SDLK_F) {
            select_units_matching([&](const oa::sim::unit_spawn::Slot& slot) {
                const auto* def = definition_for(slot.unit_index);
                return def && def->builder && def->bm_code == 0;
            });
            return true;
        }
        if (sym == SDLK_V) {
            select_units_matching([&](const oa::sim::unit_spawn::Slot& slot) {
                const auto* def = definition_for(slot.unit_index);
                return def && def->can_fly;
            });
            return true;
        }
        // Ctrl+S selects the local units of the on-screen list: those whose
        // box overlaps the view.
        if (sym == SDLK_S) {
            auto& world = match_->state();
            world.game.local_player_index = match_local_player_;
            oa::sim::selection::select_visible_units(world, on_screen_lists(), selection_hooks());
            adopt_selected_units();
            return true;
        }
        if (sym == SDLK_Z && selected_match_unit_ != 0) {
            const auto type = match_->world().slots[selected_match_unit_].unit->type_index;
            select_units_matching([&](const oa::sim::unit_spawn::Slot& slot) {
                return slot.unit && slot.unit->type_index == type;
            });
            return true;
        }
        return false;
    }
    if (sym == SDLK_TAB && selected_match_unit_ != 0) {
        cycle_selected_primary(shift);
        return true;
    }
    if (sym == SDLK_T || key.scancode == SDL_SCANCODE_T) {
        cycle_match_tracking(shift);
        return true;
    }
    if (sym == SDLK_SPACE || key.scancode == SDL_SCANCODE_HOME) {
        if (selected_match_unit_ != 0)
            center_camera_on_unit(selected_match_unit_);
        else {
            select_local_commander();
            apply_match_hud_for_selection();
            if (selected_match_unit_ != 0)
                center_camera_on_unit(selected_match_unit_);
        }
        status_ = "Center";
        return true;
    }
    // A multiplayer game's 'h' reached the console's hotkeys above; outside one
    // it does nothing.
    if (sym == SDLK_H)
        return true;
    // 'n' centres the view on the next local unit not yet visited and marks
    // the local units then on screen visited; it selects nothing.
    if (sym == SDLK_N) {
        auto& world = match_->state();
        world.game.local_player_index = match_local_player_;
        oa::sim::selection::cycle_next_unit(world, on_screen_lists(), selection_hooks());
        if (const auto next = world.game.cycle_unit_id; next != 0)
            status_ = "Next " + unit_info_name(next);
        return true;
    }
    // A multiplayer game's watcher changes no speed.
    if (sym == SDLK_MINUS || sym == SDLK_KP_MINUS || key.scancode == SDL_SCANCODE_MINUS ||
        key.scancode == SDL_SCANCODE_KP_MINUS) {
        if ((current_extension_state() & extension_state::local_watcher) == 0)
            adjust_game_speed(-1);
        return true;
    }
    if (sym == SDLK_EQUALS || sym == SDLK_PLUS || sym == SDLK_KP_PLUS ||
        key.scancode == SDL_SCANCODE_EQUALS || key.scancode == SDL_SCANCODE_KP_PLUS) {
        if ((current_extension_state() & extension_state::local_watcher) == 0)
            adjust_game_speed(1);
        return true;
    }
    if (sym == SDLK_COMMA) {
        show_match_build_page(match_build_page_ - 1);
        return true;
    }
    if (sym == SDLK_PERIOD) {
        show_match_build_page(match_build_page_ + 1);
        return true;
    }
    if (sym == SDLK_RETURN || key.scancode == SDL_SCANCODE_RETURN) {
        open_chat_line();
        return true;
    }
    // A button whose quick key is the typed letter in either case
    // is clicked (ATTACK 'a', BLAST 'd', STOP 's', ...).
    if (match_hud_ && sym < 0x80) {
        const auto typed = std::tolower(static_cast<int>(sym));
        for (std::size_t i = 0; i < match_hud_->layout.gadgets.size(); ++i) {
            const auto* button = std::get_if<oa::ui::gui_layout::ButtonFields>(
                &match_hud_->layout.gadgets[i].fields
            );
            if (button == nullptr || button->quick_key == 0)
                continue;
            if (std::tolower(static_cast<unsigned char>(button->quick_key)) == typed) {
                activate_match_hud(i);
                return true;
            }
        }
    }
    return false;
}

void Runtime::select_match_unit(float x, float y, int32_t clicks) {
    if (!match_)
        return;
    update_pointer(x, y);
    auto& world = match_->state();
    world.game.local_player_index = match_local_player_;
    const auto id = hovered_match_unit_;
    const bool toggle = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
    // The unit under the cursor, when it is a selectable local unit, is
    // selected in place of the others, or toggled with shift; the local
    // units on screen are then visited for 'n'.
    oa::sim::selection::select_cursor_unit(world, on_screen_lists(), toggle, selection_hooks());
    const auto* unit = id != 0 ? oa::world_unit_at(&world, id) : nullptr;
    if (unit == nullptr || (unit->flags & OA_UNIT_FLAG_SELECTED) == 0) {
        adopt_selected_units();
        return;
    }
    int count = 1;
    if (!toggle && clicks >= 2) {
        const auto type = unit->type_index;
        for (auto& slot : match_->world().slots) {
            if (slot.unit_index == 0 || slot.unit == nullptr || slot.unit_index == id ||
                slot.owner_index != match_local_player_ || slot.unit->type_index != type ||
                !match_->selectable(slot.unit_index))
                continue;
            slot.unit->flags |= OA_UNIT_FLAG_SELECTED;
            ++count;
        }
    }
    if (!toggle)
        selected_match_unit_ = id;
    adopt_selected_units();
    const auto maximum = match_->world().slots[id].unit->type
                             ? match_->world().slots[id].unit->type->maximum_health
                             : 0;
    status_ = count > 1 ? std::to_string(count) + " " + unit_info_name(id)
                        : unit_info_name(id) + " " +
                              std::to_string(match_->world().slots[id].unit->health) + "/" +
                              std::to_string(maximum);
}

void Runtime::adopt_selected_units() {
    if (!match_)
        return;
    uint16_t first = 0;
    bool primary_kept = false;
    for (auto& slot : match_->world().slots) {
        if (slot.unit_index == 0 || slot.unit == nullptr ||
            slot.owner_index != match_local_player_ ||
            (slot.unit->flags & OA_UNIT_FLAG_SELECTED) == 0)
            continue;
        offline_services_.refresh_selected_unit(slot);
        if (first == 0)
            first = slot.unit_index;
        if (slot.unit_index == selected_match_unit_)
            primary_kept = true;
    }
    if (!primary_kept)
        selected_match_unit_ = first;
    apply_match_hud_for_selection();
}

void Runtime::box_select_units(int x0, int y0, int x1, int y1, bool add) {
    if (!match_)
        return;
    auto& world = match_->state();
    world.game.local_player_index = match_local_player_;
    // The corners are the drag's ends on the canvas, projected from the
    // camera the view has now; the box goes back to Game.drag_start and
    // drag_end as map points at height 0.
    const auto zoom = match_zoom() == 0.0F ? 1.0 : static_cast<double>(match_zoom());
    const auto map_point = [&](int x, int y) {
        return std::array<int32_t, 3>{
            match_camera_x_ + static_cast<int32_t>(
                                  std::llround(static_cast<double>(x - match_layout_.left) / zoom)
                              ),
            0,
            match_camera_z_ + static_cast<int32_t>(
                                  std::llround(static_cast<double>(y - match_layout_.top) / zoom)
                              )
        };
    };
    const auto start = map_point(x0, y0);
    const auto end = map_point(x1, y1);
    static_assert(sizeof start == sizeof world.game.drag_start);
    static_assert(sizeof end == sizeof world.game.drag_end);
    std::memcpy(world.game.drag_start, start.data(), sizeof world.game.drag_start);
    std::memcpy(world.game.drag_end, end.data(), sizeof world.game.drag_end);
    if (!add)
        selected_match_unit_ = 0;
    const bool any =
        oa::sim::selection::select_units_in_box(world, on_screen_lists(), add, selection_hooks());
    adopt_selected_units();
    int count = 0;
    for (const auto& slot : match_->world().slots)
        if (slot.unit != nullptr && slot.owner_index == match_local_player_ &&
            (slot.unit->flags & OA_UNIT_FLAG_SELECTED) != 0)
            ++count;
    status_ = !any ? "No units" : std::to_string(count) + " selected";
}

void Runtime::area_order_units(int x0, int y0, int x1, int y1, std::string_view kind) {
    if (!match_)
        return;
    if (x0 > x1)
        std::swap(x0, x1);
    if (y0 > y1)
        std::swap(y0, y1);
    const auto viewport = live_viewport(
        static_cast<uint32_t>(std::max(0, match_camera_x_)),
        static_cast<uint32_t>(std::max(0, match_camera_z_))
    );
    int count = 0;
    // Each selected unit takes the box's first order in place of its orders
    // (or after them, with shift held) and queues the rest behind it.
    std::vector<uint16_t> ordered;
    for (auto& slot : match_->world().slots) {
        if (slot.unit_index == 0 || slot.unit == nullptr || !match_->selectable(slot.unit_index))
            continue;
        const bool enemy = slot.owner_index != match_local_player_;
        const bool local = slot.owner_index == match_local_player_;
        if (kind == "attack" && !enemy)
            continue;
        if (kind == "repair" && !local)
            continue;
        // A unit of another machine's player is tested where the frame last
        // drawn showed it, on its playout, as the pointer picks it.
        auto screen = project_match_point(viewport, slot.unit->position);
        if (const MatchModels* models = drawn_match_models(); models != nullptr) {
            if (const auto pose = mirrored_pose(
                    *models, match_->state(), slot.unit_index, models->presentation.drawn_moment
                ))
                screen = project_match_point(
                    viewport,
                    {static_cast<uint32_t>(pose->position.x),
                     static_cast<uint32_t>(pose->position.y),
                     static_cast<uint32_t>(pose->position.z)}
                );
        }
        if (screen.x < x0 || screen.x > x1 || screen.y < y0 || screen.y > y1)
            continue;
        for_each_selected([&](uint16_t source) {
            if (source == slot.unit_index)
                return;
            const bool queue =
                queueing() || std::find(ordered.begin(), ordered.end(), source) != ordered.end();
            bool issued = true;
            try {
                if (kind == "attack")
                    issued = match_->issue_attack_command(source, slot.unit_index, queue, nullptr);
                else if (kind == "reclaim")
                    match_->issue_reclaim(source, slot.unit_index, queue);
                else if (kind == "repair")
                    issue_resume_or_repair_from(source, slot.unit_index, queue);
                else
                    issued = false;
            } catch (const std::exception&) {
                issued = false;
            }
            if (issued && !queue)
                ordered.push_back(source);
        });
        ++count;
    }
    finish_issued_command();
    status_ = std::string(kind) + " " + std::to_string(count);
}

void Runtime::adjust_game_speed(int delta) {
    auto& world = match_->state();
    const auto hooks = message_hooks();
    // '+' sets the speed only below the fastest, '-' only above the slowest.
    const int32_t before = world.game.requested_speed;
    const bool sets = delta > 0   ? before < oa::sim::speed::fastest
                      : delta < 0 ? before > oa::sim::speed::slowest
                                  : false;
    if (delta > 0)
        oa::sim::speed::raise_speed(world, hooks);
    else if (delta < 0)
        oa::sim::speed::lower_speed(world, hooks);
    match_timing_.requested_rate = world.game.requested_speed;
    match_timing_.actual_rate = world.game.current_speed;
    preferences_.current_game_speed = world.game.current_speed;
    if (sets)
        call_hook_or_report<&Extension::speed_changed>(
            extension_, hook_error_report(), *this, world.game.requested_speed
        );
}

namespace {

// The panel control that shows the unit's picture, and the one that closes it.
constexpr const char* kUnitInfoPicture = "HOTR";
constexpr const char* kUnitInfoDone = "DONE";
// Height of a statistic label the panel adds: one line of the GUI font.
constexpr int16_t kUnitInfoLabelHeight = 12;

} // namespace

bool Runtime::open_unit_info() {
    namespace hud = oa::ui::hud;
    if (!match_)
        return false;
    auto& world = match_->state();
    if ((world.game.frame_flags & hud::kFrameUnitInfoOpen) != 0)
        return false;
    const auto type = hud::unit_info_subject(
        world,
        hovered_gadget_name(),
        [](void* user, const char* name) -> uint16_t {
            return oa::sim::unit_spawn::find_type_index(
                static_cast<Runtime*>(user)->spawn_type_names_, name
            );
        },
        [](void* user, const oa::Player& viewer, const oa::Unit& unit) {
            try {
                return static_cast<Runtime*>(user)->match_->unit_visible(viewer.index, unit.id);
            } catch (const std::exception&) {
                return false;
            }
        },
        this
    );
    if (type == 0)
        return false;
    hud::PanelLoader loader{};
    loader.user = this;
    loader.load = [](void* user, const char* name, const oa::Unit*, int32_t) {
        auto& self = *static_cast<Runtime*>(user);
        const auto prefix = self.match_side_prefix();
        const auto chrome = prefix == "cor" ? "anims/CORINT.GAF" : "anims/ARMINT.GAF";
        UnitInfoPanel panel;
        try {
            panel.screen = renderer::load_screen(
                self.assets_,
                {std::string("guis/") + name,
                 "",
                 "palettes/guipal.pal",
                 "anims/commongui.gaf",
                 chrome}
            );
        } catch (const std::exception& error) {
            std::cerr << "unit info panel unavailable: " << error.what() << '\n';
            return false;
        }
        auto& gadgets = panel.screen->layout.gadgets;
        if (gadgets.empty())
            return false;
        // The controls lie in the panel, which the file places on the screen.
        panel.root = gadgets.front().common;
        for (std::size_t index = 1; index < gadgets.size(); ++index) {
            gadgets[index].common.x = static_cast<int16_t>(gadgets[index].common.x + panel.root.x);
            gadgets[index].common.y = static_cast<int16_t>(gadgets[index].common.y + panel.root.y);
        }
        // The panel has no picture of its own; it is drawn over black in
        // the game palette.
        auto& background = panel.screen->background;
        background.width = static_cast<uint32_t>(kCanvasWidth);
        background.height = static_cast<uint32_t>(kCanvasHeight);
        background.rgb.assign(static_cast<std::size_t>(kCanvasWidth * kCanvasHeight * 3), 0);
        if (std::any_of(self.match_palette_.begin(), self.match_palette_.end(), [](uint8_t b) {
                return b != 0;
            }))
            background.palette = self.match_palette_;
        self.unit_info_panel_ = std::move(panel);
        self.match_->state().game.frame_flags =
            static_cast<uint16_t>(self.match_->state().game.frame_flags | hud::kFrameUnitInfoOpen);
        return true;
    };
    hud::PanelControls controls{};
    controls.user = this;
    controls.find = [](void* user, const char* name) {
        auto& self = *static_cast<Runtime*>(user);
        if (!self.unit_info_panel_ || !self.unit_info_panel_->screen)
            return int32_t{-1};
        const auto& gadgets = self.unit_info_panel_->screen->layout.gadgets;
        for (std::size_t index = 0; index < gadgets.size(); ++index)
            if (gadgets[index].common.name == name)
                return static_cast<int32_t>(index);
        return int32_t{-1};
    };
    controls.set_text = [](void* user, int32_t index, const char* text) {
        auto& self = *static_cast<Runtime*>(user);
        auto& gadgets = self.unit_info_panel_->screen->layout.gadgets;
        if (index < 0 || static_cast<std::size_t>(index) >= gadgets.size())
            return;
        if (auto* label = std::get_if<oa::ui::gui_layout::LabelFields>(
                &gadgets[static_cast<std::size_t>(index)].fields
            ))
            label->text = text;
    };
    hud::UnitInfoHost host{};
    host.user = this;
    host.add_label = [](void* user, const char* text, int16_t x, int16_t y, uint32_t) {
        auto& self = *static_cast<Runtime*>(user);
        if (!self.unit_info_panel_ || !self.unit_info_panel_->screen)
            return;
        const auto& root = self.unit_info_panel_->root;
        oa::ui::gui_layout::Gadget label;
        label.common.type = oa::ui::gui_layout::GadgetType::label;
        label.common.x = static_cast<int16_t>(root.x + x);
        label.common.y = static_cast<int16_t>(root.y + y);
        label.common.width = static_cast<int16_t>(std::max(1, root.width - x));
        label.common.height = kUnitInfoLabelHeight;
        label.common.active = 1;
        oa::ui::gui_layout::LabelFields fields;
        fields.text = text;
        label.fields = std::move(fields);
        self.unit_info_panel_->screen->layout.gadgets.push_back(std::move(label));
    };
    host.set_picture = [](void* user, const char* path) {
        auto& self = *static_cast<Runtime*>(user);
        if (self.unit_info_panel_)
            self.unit_info_panel_->picture_path = path;
    };
    host.free_picture = [](void* user) {
        auto& self = *static_cast<Runtime*>(user);
        if (self.unit_info_panel_)
            self.unit_info_panel_->picture_path.clear();
    };
    if (!hud::open_unit_info_panel(
            world,
            type,
            static_cast<int32_t>(oa::sim::messages::ticks_per_second),
            loader,
            controls,
            host
        )) {
        unit_info_panel_.reset();
        return false;
    }
    // The panel is drawn once, its picture copied at HOTR at its own size.
    auto& panel = *unit_info_panel_;
    try {
        panel.frame = renderer::render_screen(*panel.screen);
    } catch (const std::exception& error) {
        std::cerr << "unit info panel: " << error.what() << '\n';
        panel.frame = {};
    }
    const oa::ui::gui_layout::Gadget* picture_area = nullptr;
    for (const auto& gadget : panel.screen->layout.gadgets)
        if (gadget.common.name == kUnitInfoPicture)
            picture_area = &gadget;
    if (picture_area != nullptr && !panel.picture_path.empty() && !panel.frame.rgb.empty()) {
        auto path = panel.picture_path;
        std::replace(path.begin(), path.end(), '\\', '/');
        try {
            const auto picture =
                oa::ui::decoded::require(oa::decode_pcx(assets_.read(path).bytes), path);
            const auto& palette = panel.screen->background.palette;
            for (uint32_t row = 0; row < picture.height; ++row)
                for (uint32_t column = 0; column < picture.width; ++column) {
                    const auto x = picture_area->common.x + static_cast<int>(column);
                    const auto y = picture_area->common.y + static_cast<int>(row);
                    if (x < 0 || y < 0 || x >= static_cast<int>(panel.frame.width) ||
                        y >= static_cast<int>(panel.frame.height))
                        continue;
                    const auto at = static_cast<std::size_t>(row) * picture.width + column;
                    auto* out =
                        panel.frame.rgb.data() + (static_cast<std::size_t>(y) * panel.frame.width +
                                                  static_cast<std::size_t>(x)) *
                                                     3U;
                    // The picture's colour indices are the game palette's.
                    if (palette && at < picture.indices.size()) {
                        const auto entry = static_cast<std::size_t>(picture.indices[at]) * 4U;
                        out[0] = (*palette)[entry];
                        out[1] = (*palette)[entry + 1];
                        out[2] = (*palette)[entry + 2];
                    } else if (at * 3U + 2U < picture.rgb.size()) {
                        out[0] = picture.rgb[at * 3U];
                        out[1] = picture.rgb[at * 3U + 1];
                        out[2] = picture.rgb[at * 3U + 2];
                    }
                }
        } catch (const std::exception& error) {
            std::cerr << "unit info picture " << path << " unavailable: " << error.what() << '\n';
        }
    }
    status_ = "Unit info";
    return true;
}

void Runtime::close_unit_info() {
    if (!unit_info_panel_)
        return;
    oa::ui::hud::UnitInfoHost host{};
    host.user = this;
    host.free_picture = [](void* user) {
        auto& self = *static_cast<Runtime*>(user);
        if (self.unit_info_panel_)
            self.unit_info_panel_->picture_path.clear();
    };
    // A panel closing asks for nothing more.
    if (match_)
        std::ignore = oa::ui::hud::unit_info_panel_click(match_->state().game, nullptr, host, {});
    unit_info_panel_.reset();
}

void Runtime::press_unit_info_done() {
    if (!unit_info_panel_ || !match_)
        return;
    oa::ui::hud::HudEvents events{};
    events.user = this;
    events.play_sound = [](void* user, const char* name) {
        static_cast<Runtime*>(user)->play_ui_sound(name, 0);
    };
    if (oa::ui::hud::unit_info_panel_click(match_->state().game, kUnitInfoDone, {}, events) ==
        oa::ui::hud::UnitInfoClick::done)
        close_unit_info();
}

bool Runtime::click_unit_info(float x, float y) {
    if (!unit_info_panel_ || !unit_info_panel_->screen)
        return false;
    const auto point = oa::ui::display_layout::canvas_to_source(
        match_layout_, static_cast<int>(x), static_cast<int>(y)
    );
    const auto& root = unit_info_panel_->root;
    if (point.x < root.x || point.y < root.y || point.x >= root.x + root.width ||
        point.y >= root.y + root.height)
        return false;
    for (const auto& gadget : unit_info_panel_->screen->layout.gadgets) {
        const auto& area = gadget.common;
        if (area.name == kUnitInfoDone && point.x >= area.x && point.y >= area.y &&
            point.x < area.x + area.width && point.y < area.y + area.height) {
            press_unit_info_done();
            break;
        }
    }
    return true;
}

void Runtime::draw_unit_info_panel() {
    if (!unit_info_panel_ || unit_info_panel_->frame.rgb.empty())
        return;
    const auto& root = unit_info_panel_->root;
    const auto top_left = hud_canvas(root.x, root.y);
    const auto bottom_right = hud_canvas(root.x + root.width, root.y + root.height);
    scale_blit(
        paint_target(),
        unit_info_panel_->frame,
        top_left.x,
        top_left.y,
        std::max(1, bottom_right.x - top_left.x),
        std::max(1, bottom_right.y - top_left.y),
        root.x,
        root.y,
        root.width,
        root.height
    );
}

void Runtime::draw_chat_overlay() {
    draw_console_clock();
    draw_match_message_log();
    draw_debug_status_line();
    draw_frame_stats();
}

void Runtime::ensure_talk_panel() {
    if (talk_layout_)
        return;
    try {
        const auto bytes = assets_.read("guis/talk.gui").bytes;
        auto parsed = oa::ui::gui_layout::parse(bytes);
        if (!parsed.ok())
            throw std::runtime_error(
                parsed.error ? parsed.error->message : "TALK.GUI parse failed"
            );
        talk_layout_ = std::move(*parsed.layout);
    } catch (const std::exception& error) {
        std::cerr << "TALK.GUI unavailable: " << error.what() << '\n';
        talk_layout_.emplace();
        return;
    }
    append_gaf_file(match_talk_, "anims/talk.gaf");
}

void Runtime::draw_chat_entry() {
    if (!chat_composing_)
        return;
    ensure_talk_panel();
    if (talk_layout_->gadgets.empty())
        return;
    const auto& root = talk_layout_->gadgets.front().common;
    const int origin_x = root.x;
    const int origin_y = root.y < 0 ? kCanvasHeight + root.y : root.y;
    for (const auto& gadget : talk_layout_->gadgets) {
        const auto& common = gadget.common;
        if (&common == &root)
            continue;
        const int x = origin_x + common.x;
        const int y = origin_y + common.y;
        if (common.type == oa::ui::gui_layout::GadgetType::text_box) {
            const oa::formats::fnt::Font* font = match_label_font();
            const int text_h =
                font != nullptr ? static_cast<int>(oa::formats::fnt::line_height(*font)) : 0;
            draw_hud_label(x + 2, y + (common.height - text_h) / 2, chat_buffer_ + "_", 255);
            continue;
        }
        // CONSOLE is the TALK.GAF picture of the same name.
        const auto* sequence = gaf_sequence(match_talk_, common.name);
        if (sequence == nullptr || sequence->frames.empty())
            continue;
        if (const auto rendered = oa::formats::gaf::render_normal(sequence->frames.front());
            rendered.ok())
            blit_gaf_source(*rendered.frame, x, y);
    }
}

namespace {

// A release this many game ticks or fewer after the
// press, over a box under this many map pixels across in x and z, counts as a click.
constexpr uint32_t kDragClickTicks = 0x19;
constexpr int32_t kDragClickPixels = 0x20;
// UI colours of the drag box's outer and inset outlines.
constexpr uint8_t kDragBoxOuterColor = 15;
constexpr uint8_t kDragBoxInnerColor = 0;

} // namespace

std::optional<std::array<int32_t, 3>> Runtime::match_pointer_ground(float x, float y) {
    if (!match_ || !selected_tnt_)
        return std::nullopt;
    const auto viewport = live_viewport(
        static_cast<uint32_t>(match_camera_x_), static_cast<uint32_t>(match_camera_z_)
    );
    const oa::present::world_renderer::ScreenPoint pointer{
        std::clamp(
            static_cast<int32_t>(x),
            match_layout_.left,
            match_layout_.left + match_layout_.battlefield_width() - 1
        ),
        std::clamp(
            static_cast<int32_t>(y),
            match_layout_.top,
            match_layout_.top + match_layout_.battlefield_height() - 1
        )
    };
    const auto map = oa::present::world_renderer::screen_to_map_pixel(viewport, pointer);
    if (!map)
        return std::nullopt;
    const oa::sim::unit_movement::Terrain terrain(*selected_tnt_);
    const auto ground = oa::sim::gameplay_input::terrain_intersection(
        terrain,
        static_cast<int32_t>(map->x),
        static_cast<int32_t>(map->y),
        static_cast<int32_t>(selected_tnt_->attribute_width * 16U),
        static_cast<int32_t>(selected_tnt_->attribute_height * 16U)
    );
    const auto whole = [](int32_t fixed) {
        return static_cast<int32_t>(static_cast<int16_t>(fixed >> 16));
    };
    return std::array<int32_t, 3>{whole(ground.x), whole(ground.y), whole(ground.z)};
}

void Runtime::track_match_drag() {
    if (!match_drag_)
        return;
    if (const auto ground = match_pointer_ground(match_pointer_x_, match_pointer_y_))
        match_drag_->end = *ground;
}

std::array<oa::present::world_renderer::ScreenPoint, 2> Runtime::match_drag_corners(
    const oa::present::world_renderer::BattlefieldViewport& viewport
) const {
    const double scale = viewport.scale == 0.0F ? 1.0 : static_cast<double>(viewport.scale);
    const auto project = [&](const std::array<int32_t, 3>& point) {
        return oa::present::world_renderer::ScreenPoint{
            viewport.destination_x +
                static_cast<int32_t>(
                    std::lround((point[0] - static_cast<int32_t>(viewport.source_x)) * scale)
                ),
            viewport.destination_y +
                static_cast<int32_t>(std::lround(
                    (point[2] - (point[1] >> 1) - static_cast<int32_t>(viewport.source_y)) * scale
                ))
        };
    };
    if (!match_drag_)
        return {};
    return {project(match_drag_->start), project(match_drag_->end)};
}

bool Runtime::match_drag_is_click() const {
    if (!match_drag_)
        return true;
    const auto released = static_cast<int32_t>(frontend_tick());
    return released < static_cast<int32_t>(match_drag_->pressed_tick + kDragClickTicks) &&
           std::abs(match_drag_->start[0] - match_drag_->end[0]) < kDragClickPixels &&
           std::abs(match_drag_->start[2] - match_drag_->end[2]) < kDragClickPixels;
}

void Runtime::draw_selection_band(
    oa::present::world_renderer::Surface& destination,
    const oa::present::world_renderer::BattlefieldViewport& viewport
) {
    if (!match_drag_)
        return;
    ensure_ui_colors();
    const auto [from, to] = match_drag_corners(viewport);
    int x1 = std::min(from.x, to.x), y1 = std::min(from.y, to.y);
    int x2 = std::max(from.x, to.x), y2 = std::max(from.y, to.y);
    for (const auto color : {kDragBoxOuterColor, kDragBoxInnerColor}) {
        const auto rgb = ui_color_rgb(color);
        draw_match_line(destination, x1, y1, x2, y1, rgb);
        draw_match_line(destination, x2, y1, x2, y2, rgb);
        draw_match_line(destination, x1, y2, x2, y2, rgb);
        draw_match_line(destination, x1, y1, x1, y2, rgb);
        ++x1;
        ++y1;
        --x2;
        --y2;
    }
}

void Runtime::cycle_selected_primary(bool reverse) {
    if (!match_ || selected_match_unit_ == 0)
        return;
    std::vector<uint16_t> ids;
    for (const auto& slot : match_->world().slots) {
        if (slot.unit != nullptr && (slot.unit->flags & OA_UNIT_FLAG_SELECTED) != 0)
            ids.push_back(slot.unit_index);
    }
    if (ids.size() < 2)
        return;
    auto it = std::find(ids.begin(), ids.end(), selected_match_unit_);
    if (it == ids.end())
        it = ids.begin();
    if (reverse) {
        if (it == ids.begin())
            it = ids.end();
        --it;
    } else {
        ++it;
        if (it == ids.end())
            it = ids.begin();
    }
    selected_match_unit_ = *it;
    apply_match_hud_for_selection();
    status_ = unit_info_name(selected_match_unit_);
}

void Runtime::center_camera_on_unit(uint16_t id) {
    if (!match_ || id == 0)
        return;
    const auto* unit = match_->world().slots[id].unit;
    if (unit == nullptr)
        return;
    set_camera_position(
        static_cast<int32_t>(unit->position[0] >> 16) - visible_map_width() / 2,
        static_cast<int32_t>(unit->position[2] >> 16) - visible_map_height() / 2,
        0
    );
}

void Runtime::apply_match_hud_for_selection() {
    // An open menu, or the outcome, keeps its panel; the menu's closing shows
    // the page for the selection then.
    if (match_paused_)
        return;
    if (selected_match_unit_ == 0) {
        match_build_page_ = 0;
        show_match_orders_page();
        return;
    }
    const auto* definition = definition_for(selected_match_unit_);
    if (definition != nullptr && definition->builder)
        show_match_build_page(1);
    else
        show_match_orders_page();
}

} // namespace oa::app
