// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/ui/gui_input.hpp"

#include "oa/ui/gui_layout/gui_gadget.hpp"

#include <algorithm>
#include <cstdint>

namespace oa::ui::gui_input {

std::optional<Rect>
gadget_geometry(std::span<const ui::gui_layout::Gadget> gadgets, std::size_t index) noexcept {
    if (index >= gadgets.size())
        return std::nullopt;
    const auto& common = gadgets[index].common;
    // The origin is zero when the record's type byte is zero; right/bottom
    // always come from the stored dimensions.
    const auto left =
        common.type == ui::gui_layout::GadgetType::panel ? 0 : static_cast<int32_t>(common.x);
    const auto top =
        common.type == ui::gui_layout::GadgetType::panel ? 0 : static_cast<int32_t>(common.y);
    return Rect{
        left,
        top,
        left + static_cast<int32_t>(common.width) - 1,
        top + static_cast<int32_t>(common.height) - 1
    };
}

namespace {

// Records past the hot surface (font and file resources, timers, progress
// bars) have no click handler in the menu pointer path, so a button under them keeps
// the click.
bool takes_click(ui::gui_layout::GadgetType type) noexcept {
    return static_cast<uint8_t>(type) <=
           static_cast<uint8_t>(ui::gui_layout::GadgetType::hot_surface);
}

} // namespace

std::optional<std::size_t> hit_test(const MenuObject& menu, int32_t x, int32_t y) noexcept {
    std::optional<std::size_t> result;
    for (std::size_t index = 1; index < menu.gadgets.size(); ++index) {
        const auto& gadget = menu.gadgets[index];
        if (gadget.common.active == 0) {
            if (menu.selected_index != -1)
                break;
            continue;
        }
        const auto rectangle = gadget_geometry(menu.gadgets, index);
        if (rectangle && rectangle->contains(x, y) && takes_click(gadget.common.type))
            result = index;
        if (menu.selected_index != -1)
            break;
    }
    return result;
}

bool button_result(const MenuObject& menu, std::string_view query) noexcept {
    if (menu.gadgets.empty() || menu.selected_index < 0 ||
        static_cast<std::size_t>(menu.selected_index) >= menu.gadgets.size())
        return false;
    const auto& name = menu.gadgets[static_cast<std::size_t>(menu.selected_index)].common.name;
    const auto name_end = name.find('\0');
    const auto query_end = query.find('\0');
    const auto name_view =
        std::string_view(name.data(), name_end == std::string::npos ? name.size() : name_end);
    const auto query_view =
        query.substr(0, query_end == std::string_view::npos ? query.size() : query_end);
    return name_view == query_view;
}

uint8_t released_button_stage(const ui::gui_layout::Gadget& gadget, uint8_t stage) noexcept {
    const auto* button = std::get_if<ui::gui_layout::ButtonFields>(&gadget.fields);
    if (gadget.common.type != ui::gui_layout::GadgetType::button || button == nullptr ||
        button->grayed_out)
        return stage;
    namespace attribute = ui::gui_layout::attribute;
    constexpr uint32_t keeps_stage = attribute::text_list | attribute::toggle |
                                     attribute::checkbox | attribute::cycle_frames |
                                     attribute::scroll_step_mask;
    const auto stages = static_cast<uint8_t>(button->stages);
    if ((static_cast<uint32_t>(gadget.common.attributes) & keeps_stage) != 0 || stages == 0)
        return stage;
    const auto next = static_cast<uint8_t>(stage + 1U);
    return stages <= next ? 0 : next;
}

void clear_selection(MenuObject& menu) noexcept {
    menu.selected_index = -1;
}

void mark_label_shadows(std::span<ui::gui_layout::Gadget> gadgets) noexcept {
    if (gadgets.empty())
        return;
    const auto* panel = std::get_if<ui::gui_layout::PanelFields>(&gadgets.front().fields);
    // The walk never writes the root, so its count is read once.
    if (panel == nullptr || panel->loaded_total_gadgets <= 0)
        return;
    const auto count = static_cast<std::size_t>(panel->loaded_total_gadgets);
    const auto last = std::min(count, gadgets.size() - 1);
    for (std::size_t index = 1; index <= last; ++index) {
        auto& common = gadgets[index].common;
        if (common.type == ui::gui_layout::GadgetType::label)
            common.attributes |= kLabelShadowAttribute;
    }
}

std::optional<VirtualKey> physical_key(ControlKey key) noexcept {
    switch (key) {
    case ControlKey::left:
        return VirtualKey::left;
    case ControlKey::up:
        return VirtualKey::up;
    case ControlKey::right:
        return VirtualKey::right;
    case ControlKey::down:
        return VirtualKey::down;
    case ControlKey::space:
        return VirtualKey::space;
    case ControlKey::shift:
        return VirtualKey::shift;
    case ControlKey::control:
        return VirtualKey::control;
    case ControlKey::alt:
        return VirtualKey::alt;
    }
    return std::nullopt;
}

bool control_key_down(int32_t code, AsyncKeyState state, void* context) noexcept {
    switch (code) {
    case static_cast<int32_t>(ControlKey::left):
    case static_cast<int32_t>(ControlKey::up):
    case static_cast<int32_t>(ControlKey::right):
    case static_cast<int32_t>(ControlKey::down):
    case static_cast<int32_t>(ControlKey::space):
    case static_cast<int32_t>(ControlKey::shift):
    case static_cast<int32_t>(ControlKey::control):
    case static_cast<int32_t>(ControlKey::alt):
        break;
    default:
        return false;
    }
    const auto key = physical_key(static_cast<ControlKey>(code));
    return key && state != nullptr && key_is_down(state(context, *key));
}

std::optional<NavigationDirection> navigation_direction(ControlKey key) noexcept {
    switch (key) {
    case ControlKey::left:
        return NavigationDirection::left;
    case ControlKey::up:
        return NavigationDirection::up;
    case ControlKey::right:
        return NavigationDirection::right;
    case ControlKey::down:
        return NavigationDirection::down;
    default:
        return std::nullopt;
    }
}

} // namespace oa::ui::gui_input
