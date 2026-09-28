// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Unit naming, GAF sequence helpers and game cursors.
#include "oa/app/runtime.hpp"
#include "oa/sim/gameplay_input/order_cursor.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>

namespace oa::app {

std::string Runtime::match_side_prefix() const {
    const auto side = static_cast<std::size_t>(
        match_view_player() < skirmish_settings_.slots.size()
            ? skirmish_settings_.slots[match_view_player()].side
            : 0
    );
    if (side < side_table_.count) {
        const std::string_view commander = side_table_.sides[side].commander;
        if (commander.size() >= 3 && (commander[0] == 'C' || commander[0] == 'c'))
            return "cor";
    }
    return "arm";
}

const oa::data::unit_definitions::UnitDefinition* Runtime::definition_for(uint16_t unit) const {
    if (!match_ || unit == 0 || unit >= match_->world().slots.size())
        return nullptr;
    const auto* slot_unit = match_->world().slots[unit].unit;
    if (slot_unit == nullptr)
        return nullptr;
    const auto type = static_cast<uint16_t>(slot_unit->type_index);
    if (type == 0 || type > unit_definitions_.size())
        return nullptr;
    return &unit_definitions_[type - 1U];
}

std::string Runtime::unit_gui_name(uint16_t unit) const {
    if (!match_ || unit == 0 || unit >= match_->world().slots.size())
        return {};
    const auto* slot_unit = match_->world().slots[unit].unit;
    if (slot_unit == nullptr)
        return {};
    const auto type = static_cast<uint16_t>(slot_unit->type_index);
    if (type >= spawn_type_names_.size())
        return {};
    return spawn_type_names_[type];
}

std::string Runtime::unit_info_name(uint16_t unit) const {
    const auto* definition = definition_for(unit);
    if (definition == nullptr)
        return unit_gui_name(unit);
    if (!definition->display_name.empty()) {
        if (!definition->side.empty())
            return definition->side + " " + definition->display_name;
        return definition->display_name;
    }
    return definition->unit_name.empty() ? unit_gui_name(unit) : definition->unit_name;
}

std::string Runtime::selected_unit_gui_name() const {
    return unit_gui_name(selected_match_unit_);
}

const oa::formats::gaf::Sequence*
Runtime::gaf_sequence(const oa::formats::gaf::Archive& archive, std::string_view name) const {
    for (const auto& sequence : archive.sequences) {
        if (sequence.name.size() != name.size())
            continue;
        bool same = true;
        for (std::size_t i = 0; i < name.size(); ++i) {
            auto a = static_cast<unsigned char>(sequence.name[i]);
            auto b = static_cast<unsigned char>(name[i]);
            if (a >= 'A' && a <= 'Z')
                a = static_cast<unsigned char>(a + ('a' - 'A'));
            if (b >= 'A' && b <= 'Z')
                b = static_cast<unsigned char>(b + ('a' - 'A'));
            if (a != b) {
                same = false;
                break;
            }
        }
        if (same)
            return &sequence;
    }
    return nullptr;
}

const oa::formats::gaf::Archive& Runtime::explosion_gaf_archive(std::string_view name) {
    std::string key(name);
    for (auto& c : key)
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c + ('a' - 'A'));
    if (key.empty() || key == "fx")
        return match_fx_;
    auto found = match_explosion_gafs_.find(key);
    if (found != match_explosion_gafs_.end())
        return found->second;
    auto& archive = match_explosion_gafs_[key];
    append_gaf_file(archive, "anims/" + std::string(name) + ".GAF");
    return archive;
}

void Runtime::append_gaf_file(oa::formats::gaf::Archive& destination, std::string_view path) {
    std::string key(path);
    for (auto& c : key)
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c + ('a' - 'A'));
    if (missing_gaf_paths_.count(key) != 0)
        return;
    try {
        const auto parsed = oa::formats::gaf::parse(assets_.read(std::string(path)).bytes);
        if (!parsed.ok()) {
            missing_gaf_paths_.insert(key);
            std::cerr << "match HUD GAF '" << path << "' parse failed: " << parsed.error->message
                      << '\n';
            return;
        }
        for (auto& sequence : parsed.archive->sequences)
            destination.sequences.push_back(std::move(sequence));
    } catch (const std::exception& error) {
        missing_gaf_paths_.insert(key);
        const std::string_view what = error.what();
        if (what.find("asset not found") == std::string_view::npos)
            std::cerr << "match HUD GAF '" << path << "' unavailable: " << error.what() << '\n';
    }
}

void Runtime::blit_gaf_frame(
    oa::Image& destination,
    const oa::formats::gaf::RenderedFrame& frame,
    int destination_x,
    int destination_y,
    const oa::PaletteBytes& palette
) {
    for (std::size_t row = 0; row < frame.height; ++row) {
        for (std::size_t column = 0; column < frame.width; ++column) {
            const auto offset = row * frame.width + column;
            if (offset >= frame.coverage.size() || frame.coverage[offset] == 0)
                continue;
            const int x = destination_x + static_cast<int>(column);
            const int y = destination_y + static_cast<int>(row);
            if (x < 0 || y < 0 || x >= static_cast<int>(destination.width) ||
                y >= static_cast<int>(destination.height))
                continue;
            const auto index = frame.pixels[offset];
            const auto pal = static_cast<std::size_t>(index) * 4U;
            if (pal + 2 >= palette.size())
                continue;
            const auto di =
                (static_cast<std::size_t>(y) * destination.width + static_cast<std::size_t>(x)) *
                3U;
            destination.rgb[di] = palette[pal];
            destination.rgb[di + 1] = palette[pal + 1];
            destination.rgb[di + 2] = palette[pal + 2];
        }
    }
}

void Runtime::overlay_gaf_sequence(
    oa::Image& destination,
    const oa::formats::gaf::Archive& archive,
    std::string_view name,
    int x,
    int y,
    const oa::PaletteBytes& palette
) {
    const auto* sequence = gaf_sequence(archive, name);
    if (sequence == nullptr || sequence->frames.empty())
        return;
    const auto rendered = oa::formats::gaf::render_normal(sequence->frames.front());
    if (!rendered.ok())
        return;
    blit_gaf_frame(destination, *rendered.frame, x, y, palette);
}

void Runtime::load_game_cursors() {
    try {
        append_gaf_file(cursor_gaf_, "anims/CURSORS.GAF");
        if (cursor_gaf_.sequences.empty())
            append_gaf_file(cursor_gaf_, "anims/cursors.gaf");
        cursors_loaded_ = !cursor_gaf_.sequences.empty();
    } catch (const std::exception& error) {
        std::cerr << "CURSORS.GAF unavailable: " << error.what() << '\n';
        cursors_loaded_ = false;
    }
    try {
        const auto palette_data = assets_.read("palettes/palette.pal").bytes;
        if (palette_data.size() == match_palette_.size())
            std::copy(palette_data.begin(), palette_data.end(), match_palette_.begin());
    } catch (const std::exception&) {
    }
    // Session start resets the GUI context's cursor to the first
    // frame of the normal cursor.
    bind_gui_context();
    const auto* normal = cursor_sequence(kNormalCursor);
    oa::ui::gui_input::reset_cursor(
        gui_context_,
        normal != nullptr && !normal->frames.empty() ? &normal->frames.front() : nullptr
    );
    cursor_index_ = 0xff;
    select_game_cursor(kNormalCursor);
}

void Runtime::bind_gui_context() {
    auto& host = gui_context_.host;
    host.context = this;
    host.current_tick = [](void* context) {
        return static_cast<Runtime*>(context)->frontend_tick();
    };
    host.current_pointer = [](void* context, oa::ui::gui_input::PointerEvent& out) {
        const auto& runtime = *static_cast<Runtime*>(context);
        out.x = static_cast<int32_t>(runtime.pointer_x_);
        out.y = static_cast<int32_t>(runtime.pointer_y_);
    };
    host.peek_pointer = [](void* context, oa::ui::gui_input::PointerEvent& out) {
        static_cast<Runtime*>(context)->gui_context_.host.current_pointer(context, out);
        return false;
    };
    host.pop_pointer = host.peek_pointer;
    host.set_cursor_image = [](void* context, const oa::formats::gaf::Frame* image) {
        static_cast<Runtime*>(context)->cursor_image_ = image;
    };
    host.cursor_image = [](void* context) { return static_cast<Runtime*>(context)->cursor_image_; };
}

const oa::formats::gaf::Sequence* Runtime::cursor_sequence(uint8_t index) const {
    if (index >= kCursorNames.size() || kCursorNames[index].empty())
        return nullptr;
    return gaf_sequence(cursor_gaf_, kCursorNames[index]);
}

void Runtime::select_game_cursor(uint8_t index) {
    if (index == cursor_index_)
        return;
    cursor_index_ = index;
    const auto* sequence = cursor_sequence(index);
    oa::ui::gui_input::set_cursor_sequence(
        gui_context_, sequence != nullptr ? sequence : cursor_sequence(kNormalCursor)
    );
}

namespace {
namespace input = oa::sim::gameplay_input;

input::OrderCommand armed_order(MatchCommand command) {
    switch (command) {
    case MatchCommand::move:
        return input::OrderCommand::move;
    case MatchCommand::attack:
        return input::OrderCommand::attack;
    case MatchCommand::dgun:
        return input::OrderCommand::blast;
    case MatchCommand::build:
        return input::OrderCommand::build;
    case MatchCommand::patrol:
        return input::OrderCommand::patrol;
    case MatchCommand::repair:
        return input::OrderCommand::repair;
    case MatchCommand::reclaim:
        return input::OrderCommand::reclaim;
    case MatchCommand::capture:
        return input::OrderCommand::capture;
    case MatchCommand::load:
        return input::OrderCommand::load;
    case MatchCommand::unload:
        return input::OrderCommand::unload;
    case MatchCommand::guard:
        return input::OrderCommand::guard;
    case MatchCommand::none:
        break;
    }
    return input::OrderCommand::default_order;
}
} // namespace

uint8_t Runtime::pick_match_cursor() {
    if (hovered_ || !match_)
        return static_cast<uint8_t>(input::OrderCursor::normal);
    auto& world = match_->state();
    world.game.local_player_index = match_local_player_;
    input::set_pointer_command(world.game, armed_order(match_command_));
    refresh_pointer_area();
    if (const auto ground = match_world_point(pointer_x_, pointer_y_)) {
        input::set_pointer_position(world.game, {(*ground)[0], (*ground)[1], (*ground)[2]});
        store_cursor_cell(*ground);
    }
    input::OrderCursor cursor{};
    if (!input::pointer_cursor(world, order_cursor_hooks(), &cursor))
        return static_cast<uint8_t>(input::OrderCursor::build);
    return static_cast<uint8_t>(cursor);
}

bool Runtime::pointer_over_top_panel() {
    const auto* gadgets = screen_ == Screen::match
                              ? (match_hud_ ? &match_hud_->layout.gadgets : nullptr)
                              : &resources_.layout.gadgets;
    if (gadgets == nullptr || gadgets->empty())
        return false;
    const auto& root = gadgets->front().common;
    CanvasRect rect{root.x, root.y, root.width, root.height};
    if (screen_ == Screen::match) {
        const auto mapped = oa::ui::display_layout::source_rect_to_canvas(
            match_layout_, root.x, root.y, root.width, root.height
        );
        rect = {mapped.x, mapped.y, mapped.width, mapped.height};
    }
    const auto x = static_cast<int32_t>(pointer_x_);
    const auto y = static_cast<int32_t>(pointer_y_);
    return x >= rect.x && x <= rect.x + rect.w - 1 && y >= rect.y && y <= rect.y + rect.h - 1;
}

void Runtime::tick_and_draw_cursor() {
    if (!cursors_loaded_ || frame_without_cursor_)
        return;
    const auto tick = frontend_tick();
    gui_context_.tick_delta = tick - gui_context_.last_tick;
    gui_context_.last_tick = tick;
    oa::ui::gui_input::tick_cursor_and_read_pointer(gui_context_);
    if (screen_ == Screen::match)
        select_game_cursor(pick_match_cursor());
    oa::ui::gui_input::follow_hover_cursor(gui_context_, pointer_over_top_panel());
    if (match_use_layers_ && screen_ == Screen::match)
        return;
    if (surface_.rgb.empty() || cursor_image_ == nullptr)
        return;
    const auto rendered = oa::formats::gaf::render_normal(*cursor_image_);
    if (!rendered.ok())
        return;
    const auto& frame = *rendered.frame;
    const auto& pal = match_palette_.size() >= 1024 ? match_palette_ : resources_.gui_palette;
    const int destination_x = static_cast<int>(pointer_x_) - frame.origin_x;
    const int destination_y = static_cast<int>(pointer_y_) - frame.origin_y;
    for (std::size_t row = 0; row < frame.height; ++row) {
        for (std::size_t column = 0; column < frame.width; ++column) {
            const auto offset = row * frame.width + column;
            if (offset >= frame.coverage.size() || frame.coverage[offset] == 0)
                continue;
            const int x = destination_x + static_cast<int>(column);
            const int y = destination_y + static_cast<int>(row);
            if (x < 0 || y < 0 || x >= static_cast<int>(surface_.width) ||
                y >= static_cast<int>(surface_.height))
                continue;
            const auto pal_i = static_cast<std::size_t>(frame.pixels[offset]) * 4U;
            if (pal_i + 2 >= pal.size())
                continue;
            const auto di =
                (static_cast<std::size_t>(y) * surface_.width + static_cast<std::size_t>(x)) * 3U;
            surface_.rgb[di] = pal[pal_i];
            surface_.rgb[di + 1] = pal[pal_i + 1];
            surface_.rgb[di + 2] = pal[pal_i + 2];
        }
    }
}

} // namespace oa::app
