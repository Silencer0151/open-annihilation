// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/ui/frontend_state/map_selection.hpp"
#include "oa/data/campaign/directory_list.hpp"
#include <algorithm>
#include <bit>
#include <cstdint>
#include <stdexcept>

namespace oa::ui::frontend_state::map_selection {
namespace {
const std::string& selected_name(const ModalState& modal, Host& h) {
    const auto index = h.selected_map_index();
    if (index < 0 || static_cast<std::size_t>(index) >= modal.names.size())
        throw std::out_of_range("map selection index outside owned map list");
    return modal.names[static_cast<std::size_t>(index)];
}

void release_preview(Host& h) {
    const auto previous = h.picture();
    if (previous.value != 0) {
        h.release_picture(previous);
        h.set_picture({});
    }
}

int32_t world_extent(int32_t dimension) {
    return std::bit_cast<int32_t>(static_cast<uint32_t>(dimension) << 4);
}
} // namespace

void sort_map_names(std::vector<std::string>& names) {
    if (names.size() > sort_entry_limit)
        throw std::invalid_argument("map list exceeds the game's sorting capacity");
    std::size_t bytes = 0;
    for (const auto& name : names) {
        if (name.find('\0') != std::string::npos || name.size() >= 256)
            throw std::invalid_argument("map name cannot fit the game's map field");
        if (name.size() + 1 > sort_byte_limit - bytes)
            throw std::invalid_argument("map list exceeds the game's sorting byte capacity");
        bytes += name.size() + 1;
    }
    std::string list;
    list.reserve(bytes);
    for (const auto& name : names) {
        list += name;
        list.push_back('\0');
    }
    oa::data::campaign::sort_paired_lists(
        list.data(), nullptr, nullptr, static_cast<int32_t>(names.size())
    );
    std::size_t at = 0;
    for (auto& name : names) {
        name = std::string(list.c_str() + at);
        at += name.size() + 1;
    }
}

bool open(ModalState& modal, const game_entry::SkirmishSettings& settings, Host& h) {
    const auto count = h.map_count();
    if (count == 0) {
        h.show_frontend_message(h.translate_ui(no_maps_message), 320, 1, 1);
        return false;
    }
    if (count < 0 || static_cast<std::size_t>(count) > sort_entry_limit)
        throw std::invalid_argument("invalid enumerated map count");
    if (modal.active)
        throw std::logic_error("map selection modal already active");
    modal.menu = h.load_modal("SELMAP.GUI", modal_flags);
    modal.active = true;
    h.install_map_event_callback(modal.menu);
    h.load_background("DSELECTMAP2");
    modal.names = h.copy_map_names();
    if (modal.names.size() < static_cast<std::size_t>(count))
        throw std::invalid_argument("map enumeration changed while opening modal");
    modal.names.resize(static_cast<std::size_t>(count));
    sort_map_names(modal.names);
    h.bind_map_names(modal.names);
    h.install_map_selection_callback();
    for (std::size_t i = 0; i < modal.names.size(); ++i)
        if (modal.names[i] == settings.map_name) {
            h.set_selected_map_index(static_cast<int16_t>(i));
            break;
        }
    preview_selection(modal, h);
    h.set_input_enabled(1);
    h.add_menu_flags(menu_input_flags);
    h.select_cursor_animation(active_cursor_index);
    return true;
}

void update_preview(Host& h) {
    if (h.has_map_name_widget())
        h.set_modal_text("MAPNAME", h.map_display_name(), 0);
    // The host is asked for the player counts, the "Players" label and the
    // memory text in that order, although the summary shows the memory first.
    const auto players = h.permitted_player_counts_text();
    const auto label = h.translate_ui("Players");
    const auto size = h.map_memory_requirement_text();
    const auto summary = size + "  " + label + ": " + players;
    if (summary.size() >= 100)
        throw std::invalid_argument("map summary is 100 bytes or longer");
    h.set_modal_text("SIZE", summary, 0);
    release_preview(h);
    const auto loaded = h.load_picture(h.terrain_resource_path());
    h.set_picture(loaded.picture);
    if (loaded.picture.value != 0) {
        const auto destination = h.picture_size();
        h.fit_picture(
            loaded.picture,
            destination.width,
            destination.height,
            world_extent(loaded.terrain_width),
            world_extent(loaded.terrain_height)
        );
    }
    h.set_modal_text("DESCRIPTION", h.map_description(), 0);
    h.invalidate_menu();
}

void preview_selection(const ModalState& modal, Host& h) {
    if (h.select_map(selected_name(modal, h)) != 0)
        update_preview(h);
}

void handle_event(
    ModalState& modal,
    game_entry::SkirmishSettings& settings,
    const game_entry::Event& event,
    Host& h
) {
    if (event.code == main_menu::destroy_event) {
        release_preview(h);
        std::vector<std::string>{}.swap(modal.names);
        modal.active = false;
        modal.menu = {};
        return;
    }
    if (h.button_result(event.menu, Button::map_names) == 0 &&
        h.button_result(event.menu, Button::load) == 0) {
        if (h.button_result(event.menu, Button::previous_menu) != 0) {
            h.play_ui_sound("Previous", 0);
            return;
        }
        h.clear_event_selection(event.menu);
        return;
    }
    if (h.button_result(event.menu, Button::load) != 0)
        h.play_ui_sound("SmallButton", 0);
    settings.map_name = selected_name(modal, h);
    h.select_map(settings.map_name);
    h.set_parent_map_name(event.menu, settings.map_name);
}
} // namespace oa::ui::frontend_state::map_selection
