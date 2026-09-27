// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Match hotkeys, selection commands and overlays.
#include "oa/app/runtime.hpp"
#include "oa/sim/speed.hpp"
#include "oa/ui/console/game_fields.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace oa::app {

bool Runtime::handle_match_hotkey(const SDL_KeyboardEvent& key) {
    if (screen_ != Screen::match || !match_ || key.repeat)
        return false;
    // F4 pins the kills board out (Game.graphics_flags 0x80).
    if (key.key == SDLK_F4 || key.scancode == SDL_SCANCODE_F4)
        return handle_console_hotkey(key);
    if (key.key == SDLK_F1 || key.scancode == SDL_SCANCODE_F1) {
        show_unit_info_ = !show_unit_info_;
        status_ = show_unit_info_ ? "Unit info" : "";
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
    if (key.key == SDLK_PAUSE || key.scancode == SDL_SCANCODE_PAUSE) {
        if (match_paused_)
            resume_match_pause();
        else
            show_match_pause_menu();
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
    if (handle_console_hotkey(key))
        return true;
    if (key.key == SDLK_ESCAPE) {
        if (match_paused_)
            return false;
        if (has_local_selection() || match_command_ != MatchCommand::none ||
            pending_build_type_ != 0) {
            clear_local_selection();
            reset_match_command();
            pending_build_type_ = 0;
            apply_match_hud_for_selection();
            status_ = "Selection cleared";
            return true;
        }
        show_match_pause_menu();
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
        if (sym == SDLK_S) {
            const auto viewport = live_viewport(
                static_cast<uint32_t>(std::max(0, match_camera_x_)),
                static_cast<uint32_t>(std::max(0, match_camera_z_))
            );
            select_units_matching([&](const oa::sim::unit_spawn::Slot& slot) {
                if (!slot.unit)
                    return false;
                const auto screen = project_match_point(viewport, slot.unit->position);
                return screen.x >= match_layout_.left && screen.y >= match_layout_.top &&
                       screen.x < match_layout_.left + match_layout_.battlefield_width() &&
                       screen.y < match_layout_.top + match_layout_.battlefield_height();
            });
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
    if (sym == SDLK_H) {
        share_resources();
        return true;
    }
    if (sym == SDLK_N) {
        select_next_offscreen_unit();
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
    const auto hits = pick_match_units(x, y);
    const auto id = first_local_hit(hits);
    const bool add = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
    if (add) {
        if (id == 0)
            return;
        adopt_selection(id);
        selected_match_unit_ = id;
        apply_match_hud_for_selection();
        status_ = unit_info_name(id);
        return;
    }
    clear_local_selection();
    if (id == 0) {
        apply_match_hud_for_selection();
        status_ = "No unit selected";
        return;
    }
    adopt_selection(id);
    selected_match_unit_ = id;
    int count = 1;
    if (clicks >= 2) {
        const auto type = match_->world().slots[id].unit->type_index;
        for (auto& slot : match_->world().slots) {
            if (slot.unit_index == 0 || slot.unit == nullptr || slot.unit_index == id ||
                slot.owner_index != match_local_player_ || slot.unit->type_index != type ||
                !match_->selectable(slot.unit_index))
                continue;
            adopt_selection(slot.unit_index);
            ++count;
        }
    }
    apply_match_hud_for_selection();
    const auto maximum = match_->world().slots[id].unit->type
                             ? match_->world().slots[id].unit->type->maximum_health
                             : 0;
    status_ = count > 1 ? std::to_string(count) + " " + unit_info_name(id)
                        : unit_info_name(id) + " " +
                              std::to_string(match_->world().slots[id].unit->health) + "/" +
                              std::to_string(maximum);
}

void Runtime::box_select_units(int x0, int y0, int x1, int y1, bool add) {
    if (!match_)
        return;
    if (x0 > x1)
        std::swap(x0, x1);
    if (y0 > y1)
        std::swap(y0, y1);
    if (!add)
        clear_local_selection();
    const auto viewport = live_viewport(
        static_cast<uint32_t>(std::max(0, match_camera_x_)),
        static_cast<uint32_t>(std::max(0, match_camera_z_))
    );
    uint16_t first = selected_match_unit_;
    int count = 0;
    for (auto& slot : match_->world().slots) {
        if (slot.unit_index == 0 || slot.unit == nullptr ||
            slot.owner_index != match_local_player_ || !match_->selectable(slot.unit_index))
            continue;
        const auto screen = project_match_point(viewport, slot.unit->position);
        if (screen.x < x0 || screen.x > x1 || screen.y < y0 || screen.y > y1)
            continue;
        adopt_selection(slot.unit_index);
        if (first == 0)
            first = slot.unit_index;
        ++count;
    }
    selected_match_unit_ = first;
    apply_match_hud_for_selection();
    status_ = count == 0 ? "No units" : std::to_string(count) + " selected";
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
    for (auto& slot : match_->world().slots) {
        if (slot.unit_index == 0 || slot.unit == nullptr || !match_->selectable(slot.unit_index))
            continue;
        const bool enemy = slot.owner_index != match_local_player_;
        const bool local = slot.owner_index == match_local_player_;
        if (kind == "attack" && !enemy)
            continue;
        if (kind == "repair" && !local)
            continue;
        const auto screen = project_match_point(viewport, slot.unit->position);
        if (screen.x < x0 || screen.x > x1 || screen.y < y0 || screen.y > y1)
            continue;
        for_each_selected([&](uint16_t source) {
            if (source == slot.unit_index)
                return;
            try {
                if (kind == "attack")
                    (void)match_->issue_attack(source, slot.unit_index, queueing());
                else if (kind == "reclaim")
                    (void)match_->issue_reclaim(source, slot.unit_index, queueing());
                else if (kind == "repair")
                    issue_resume_or_repair_from(source, slot.unit_index);
            } catch (const std::exception&) {
            }
        });
        ++count;
    }
    finish_issued_command();
    status_ = std::string(kind) + " " + std::to_string(count);
}

void Runtime::share_resources() {
    if (!match_)
        return;
    auto& world = match_->world();
    const auto local = static_cast<std::size_t>(match_local_player_);
    if (local >= world.players.size())
        return;
    const auto alliance = skirmish_settings_.slots[local].alliance;
    for (std::size_t i = 0; i < world.players.size(); ++i) {
        if (i == local || skirmish_settings_.slots[i].controller == entry::controller::disabled ||
            skirmish_settings_.slots[i].alliance != alliance)
            continue;
        auto& from = world.players[local];
        auto& to = world.players[i];
        const auto metal = std::min(100.0F, from.metal);
        const auto energy = std::min(100.0F, from.energy);
        from.metal -= metal;
        from.energy -= energy;
        to.metal = std::min(to.metal_cap, to.metal + metal);
        to.energy = std::min(to.energy_cap, to.energy + energy);
        status_ = "Shared with P" + std::to_string(i + 1);
        return;
    }
    status_ = "No allied player";
}

void Runtime::select_next_offscreen_unit() {
    if (!match_)
        return;
    const auto viewport = live_viewport(
        static_cast<uint32_t>(std::max(0, match_camera_x_)),
        static_cast<uint32_t>(std::max(0, match_camera_z_))
    );
    const int left = match_layout_.left, top = match_layout_.top;
    const int right = left + match_layout_.battlefield_width();
    const int bottom = top + match_layout_.battlefield_height();
    uint16_t first = 0;
    bool passed = selected_match_unit_ == 0;
    for (auto& slot : match_->world().slots) {
        if (slot.unit_index == 0 || slot.unit == nullptr ||
            slot.owner_index != match_local_player_ || !match_->selectable(slot.unit_index))
            continue;
        const auto screen = project_match_point(viewport, slot.unit->position);
        if (screen.x >= left && screen.y >= top && screen.x < right && screen.y < bottom)
            continue;
        if (first == 0)
            first = slot.unit_index;
        if (!passed) {
            if (slot.unit_index == selected_match_unit_)
                passed = true;
            continue;
        }
        clear_local_selection();
        adopt_selection(slot.unit_index);
        selected_match_unit_ = slot.unit_index;
        center_camera_on_unit(slot.unit_index);
        apply_match_hud_for_selection();
        status_ = "Next " + unit_info_name(slot.unit_index);
        return;
    }
    if (first != 0) {
        clear_local_selection();
        adopt_selection(first);
        selected_match_unit_ = first;
        center_camera_on_unit(first);
        apply_match_hud_for_selection();
        status_ = "Next " + unit_info_name(first);
    }
}

void Runtime::adjust_game_speed(int delta) {
    auto& world = match_->state();
    const auto hooks = message_hooks();
    if (delta > 0)
        oa::sim::speed::raise_speed(world, hooks);
    else if (delta < 0)
        oa::sim::speed::lower_speed(world, hooks);
    match_timing_.requested_rate = world.game.requested_speed;
    match_timing_.actual_rate = world.game.current_speed;
    preferences_.current_game_speed = world.game.current_speed;
}

void Runtime::draw_unit_info_overlay() {
    if (!show_unit_info_ || selected_match_unit_ == 0 || !match_)
        return;
    const auto& slot = match_->world().slots[selected_match_unit_];
    if (slot.unit == nullptr)
        return;
    fill_source_rect(4, 132, 120, 160, 10);
    const auto* def = definition_for(selected_match_unit_);
    int y = 136;
    draw_hud_label(8, y, unit_info_name(selected_match_unit_), 255);
    y += 12;
    draw_hud_label(
        8,
        y,
        "HP " + std::to_string(slot.unit->health) + "/" +
            std::to_string(slot.unit->type ? slot.unit->type->maximum_health : 0),
        255
    );
    y += 12;
    if (def != nullptr) {
        draw_hud_label(8, y, "M " + std::to_string(def->build_cost_metal), 255);
        y += 12;
        draw_hud_label(8, y, "E " + std::to_string(def->build_cost_energy), 255);
        y += 12;
        draw_hud_label(8, y, "Build " + std::to_string(def->build_time), 255);
    }
}

void Runtime::draw_chat_overlay() {
    draw_console_clock();
    draw_match_message_log();
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
