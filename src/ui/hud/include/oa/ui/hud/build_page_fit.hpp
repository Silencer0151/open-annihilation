// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <cstdint>
#include <limits>
#include <span>

namespace oa::ui::hud {

/// Side of a square build button, in source pixels.
inline constexpr int32_t kBuildButtonSize = 64;
/// Fewest rows of build buttons a page keeps beside its order buttons; a
/// column with room for fewer splits the page under ORDERS and BUILD tabs.
inline constexpr int32_t kMinimumSharedRows = 3;
/// Left edge of the ORDERS tab in its panel, source pixels.
inline constexpr int32_t kOrdersTabX = 3;
/// Left edge of the BUILD tab in its panel, source pixels.
inline constexpr int32_t kBuildTabX = 65;
/// Part to ask fit_build_page for to get a page's last part.
inline constexpr int32_t kLastBuildPart = std::numeric_limits<int32_t>::max();

/// One gadget of a loaded side-panel page as fit_build_page reads and moves it.
///
/// Gadget 0 is the page's panel; the others are placed in screen source
/// pixels, the panel's own offset already added.
struct PanelGadget {
    const char* name{};          ///< control name
    int32_t x{};                 ///< left edge, source pixels
    int32_t y{};                 ///< top edge, source pixels
    int32_t width{};             ///< 0 for a record that is not drawn
    int32_t height{};            ///< 0 for a record that is not drawn
    uint8_t gadget_type{};       ///< kGadgetTypeButton for a button
    uint8_t common_attributes{}; ///< commonattribs; kCommonUnitButton, kCommonWeaponButton
    bool shown{};                ///< drawn and under the pointer; fit_build_page clears it to hide
};

/// How fit_build_page laid out a page.
enum class BuildPageLayout : uint8_t {
    as_authored, ///< the page fits the column, or has no build buttons, and is left as it is
    shared,      ///< fewer rows of build buttons, with the order buttons moved up under them
    build_tab,   ///< the build buttons alone under ORDERS and BUILD tabs; orders are on the
                 ///< general page
};

/// The layout fit_build_page chose for a page.
struct BuildPageFit {
    BuildPageLayout layout{};
    int32_t rows{};         ///< rows of build buttons shown; 0 when as_authored
    int32_t part{};         ///< which part of the page's build buttons is shown, from 0
    int32_t part_count{};   ///< parts the page's build buttons are split into; 1 when as_authored
    int32_t bottom{};       ///< lowest row any shown gadget reaches, source pixels (exclusive)
    int32_t build_tab{};    ///< in build_tab, the gadget that became the BUILD tab; else -1
    int32_t orders_tab_x{}; ///< in build_tab, where the ORDERS tab goes, source pixels
    int32_t orders_tab_y{}; ///< in build_tab, where the ORDERS tab goes, source pixels
};

/// Lays a build page out to fit a side column of `column_rows` source rows.
///
/// A page whose shown gadgets all end inside the column, and a page without
/// square build buttons, are left as they are. Otherwise the page's build
/// buttons (unit and weapon buttons, and the IGPATCH slots between them)
/// keep their reading order and columns but are cut into parts of as many
/// rows as fit; PREV and NEXT step through the parts.
///
/// With room for at least kMinimumSharedRows rows beside them, the order
/// buttons under the grid stay on the page and move up under the rows shown.
/// With less room, the order buttons are hidden, the BUILD title above the
/// grid moves to the BUILD tab's place and the caller adds an ORDERS tab at
/// orders_tab_x and orders_tab_y, so the page splits as the game's own
/// pages do; the build buttons then get every row down to PREV and NEXT.
///
/// @param[in,out] gadgets the loaded page, gadget 0 its panel; positions and
///                        `shown` are rewritten
/// @param column_rows rows of the side column, source pixels from its top
/// @param part part of the build buttons to show, from 0; clamped to the
///             last part, so kLastBuildPart asks for the last
/// @return the layout chosen, with the part shown and the number of parts
[[nodiscard]] BuildPageFit
fit_build_page(std::span<PanelGadget> gadgets, int32_t column_rows, int32_t part);

} // namespace oa::ui::hud
