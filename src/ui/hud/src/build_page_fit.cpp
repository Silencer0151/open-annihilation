// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/ui/hud/build_page_fit.hpp"

#include "oa/ui/hud/order_panel.hpp"

#include <algorithm>
#include <cstddef>
#include <string_view>
#include <vector>

namespace oa::ui::hud {
namespace {

constexpr std::string_view kEmptySlotName = "IGPATCH";

/// Returns whether `name` ends with `suffix`.
bool ends_with(const char* name, std::string_view suffix) noexcept {
    return name != nullptr && std::string_view(name).ends_with(suffix);
}

/// Returns whether the gadget is drawn: shown and of non-zero size.
bool drawn(const PanelGadget& gadget) noexcept {
    return gadget.shown && gadget.width > 0 && gadget.height > 0;
}

/// Returns whether the gadget is one square slot of the build grid.
bool build_slot(const PanelGadget& gadget) noexcept {
    if (!drawn(gadget) || gadget.gadget_type != kGadgetTypeButton ||
        gadget.width != kBuildButtonSize || gadget.height != kBuildButtonSize)
        return false;
    return (gadget.common_attributes & (kCommonUnitButton | kCommonWeaponButton)) != 0 ||
           (gadget.name != nullptr && gadget.name == kEmptySlotName);
}

/// Returns the lowest row any drawn gadget after the panel reaches (exclusive).
int32_t lowest_row(std::span<const PanelGadget> gadgets) noexcept {
    int32_t bottom = 0;
    for (std::size_t index = 1; index < gadgets.size(); ++index)
        if (drawn(gadgets[index]))
            bottom = std::max(bottom, gadgets[index].y + gadgets[index].height);
    return bottom;
}

} // namespace

BuildPageFit fit_build_page(std::span<PanelGadget> gadgets, int32_t column_rows, int32_t part) {
    BuildPageFit fit{};
    fit.part_count = 1;
    fit.build_tab = -1;
    fit.bottom = lowest_row(gadgets);
    if (gadgets.empty() || fit.bottom <= column_rows)
        return fit;

    std::vector<std::size_t> slots;
    for (std::size_t index = 1; index < gadgets.size(); ++index)
        if (build_slot(gadgets[index]))
            slots.push_back(index);
    if (slots.empty())
        return fit;
    // Reading order: by row, then by column.
    std::stable_sort(slots.begin(), slots.end(), [&](std::size_t left, std::size_t right) {
        const auto& a = gadgets[left];
        const auto& b = gadgets[right];
        return a.y != b.y ? a.y < b.y : a.x < b.x;
    });
    std::vector<int32_t> columns;
    int32_t grid_top = gadgets[slots.front()].y;
    int32_t grid_bottom = 0;
    for (const auto index : slots) {
        columns.push_back(gadgets[index].x);
        grid_top = std::min(grid_top, gadgets[index].y);
        grid_bottom = std::max(grid_bottom, gadgets[index].y + gadgets[index].height);
    }
    std::sort(columns.begin(), columns.end());
    columns.erase(std::unique(columns.begin(), columns.end()), columns.end());
    // Empty slots after the last build button only pad the authored grid.
    std::size_t used = slots.size();
    while (used > 0 && gadgets[slots[used - 1]].name != nullptr &&
           gadgets[slots[used - 1]].name == kEmptySlotName)
        --used;
    if (used == 0)
        return fit;

    // Everything else is the title above the grid, PREV and NEXT, and the
    // order buttons under the grid.
    std::vector<std::size_t> pager;
    std::vector<std::size_t> orders;
    std::ptrdiff_t title = -1;
    int32_t pager_bottom = grid_bottom;
    int32_t orders_top = 0;
    int32_t orders_bottom = 0;
    for (std::size_t index = 1; index < gadgets.size(); ++index) {
        const auto& gadget = gadgets[index];
        if (!drawn(gadget) || build_slot(gadget))
            continue;
        if (ends_with(gadget.name, "PREV") || ends_with(gadget.name, "NEXT")) {
            pager.push_back(index);
            pager_bottom = std::max(pager_bottom, gadget.y + gadget.height);
        } else if (gadget.y >= grid_bottom) {
            orders_top = orders.empty() ? gadget.y : std::min(orders_top, gadget.y);
            orders_bottom = std::max(orders_bottom, gadget.y + gadget.height);
            orders.push_back(index);
        } else if (gadget.y + gadget.height <= grid_top && ends_with(gadget.name, "BUILD")) {
            title = static_cast<std::ptrdiff_t>(index);
        }
    }
    const int32_t pager_band = (orders.empty() ? pager_bottom : orders_top) - grid_bottom;
    const int32_t orders_height = orders.empty() ? 0 : orders_bottom - orders_top;
    const auto column_count = static_cast<int32_t>(columns.size());
    const auto rows_needed = (static_cast<int32_t>(used) + column_count - 1) / column_count;
    const auto rows_to = [&](int32_t room) {
        return std::clamp(room / kBuildButtonSize, 1, rows_needed);
    };

    const int32_t shared_room = column_rows - grid_top - pager_band - orders_height;
    if (shared_room / kBuildButtonSize >= kMinimumSharedRows || title < 0 || orders.empty()) {
        fit.layout = BuildPageLayout::shared;
        fit.rows = rows_to(shared_room);
    } else {
        fit.layout = BuildPageLayout::build_tab;
        fit.rows = rows_to(column_rows - grid_top - pager_band);
    }
    const int32_t per_part = fit.rows * column_count;
    fit.part_count = (static_cast<int32_t>(used) + per_part - 1) / per_part;
    fit.part = std::clamp(part, 0, fit.part_count - 1);

    for (const auto index : slots)
        gadgets[index].shown = false;
    const auto first = static_cast<std::size_t>(fit.part) * static_cast<std::size_t>(per_part);
    const auto last = std::min(used, first + static_cast<std::size_t>(per_part));
    for (std::size_t order = first; order < last; ++order) {
        auto& gadget = gadgets[slots[order]];
        const auto place = static_cast<int32_t>(order - first);
        gadget.x = columns[static_cast<std::size_t>(place % column_count)];
        gadget.y = grid_top + (place / column_count) * kBuildButtonSize;
        gadget.shown = true;
    }
    // Rows under the grid move up by the rows it gave up.
    const int32_t lift = grid_bottom - (grid_top + fit.rows * kBuildButtonSize);
    for (const auto index : pager)
        gadgets[index].y -= lift;
    if (fit.layout == BuildPageLayout::shared) {
        for (const auto index : orders)
            gadgets[index].y -= lift;
    } else {
        for (const auto index : orders)
            gadgets[index].shown = false;
        auto& build = gadgets[static_cast<std::size_t>(title)];
        fit.build_tab = static_cast<int32_t>(title);
        fit.orders_tab_x = gadgets.front().x + kOrdersTabX;
        fit.orders_tab_y = build.y;
        build.x = gadgets.front().x + kBuildTabX;
    }
    fit.bottom = lowest_row(gadgets);
    return fit;
}

} // namespace oa::ui::hud
