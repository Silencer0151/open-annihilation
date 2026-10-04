// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <cstdint>

namespace oa::ui::hud {

/// What a page shown in the side column is.
enum class SidePage : uint8_t {
    other, ///< not a unit's page, such as the in-game menu: drawn as authored
    unit,  ///< a unit's build or weapon page, its page 0, or the general page
};

/// How a unit's page is drawn in the side column, in source pixels.
///
/// A page that ends within the column is drawn as authored. A taller page is
/// drawn as authored, then scaled down uniformly about its panel's top left
/// corner so that its lowest row meets the column's: every control keeps its
/// place on the page, at that scale, and nothing moves, closes up or hides.
struct SidePageScale {
    int32_t left{};          ///< left edge of the page's panel
    int32_t top{};           ///< top edge of the page's panel
    int32_t authored_rows{}; ///< rows from `top` to the page's lowest drawn row
    int32_t shown_rows{};    ///< rows the page takes in the column; `authored_rows` when it fits

    /// Returns whether the page is drawn smaller than authored.
    [[nodiscard]] constexpr bool scaled() const noexcept {
        return shown_rows > 0 && shown_rows < authored_rows;
    }

    /// Returns where a distance from the panel's corner on the page lies in
    /// the column, rounded down.
    ///
    /// @param distance source pixels from `left` or `top` on the page, at least 0
    /// @return source pixels from `left` or `top` in the column
    [[nodiscard]] constexpr int32_t to_column(int32_t distance) const noexcept {
        return scaled() ? static_cast<int32_t>(
                              static_cast<int64_t>(distance) * shown_rows / authored_rows
                          )
                        : distance;
    }

    /// Returns which distance from the panel's corner on the page a distance
    /// in the column shows, rounded down.
    ///
    /// @param distance source pixels from `left` or `top` in the column, at least 0
    /// @return source pixels from `left` or `top` on the page
    [[nodiscard]] constexpr int32_t to_page(int32_t distance) const noexcept {
        return scaled() ? static_cast<int32_t>(
                              static_cast<int64_t>(distance) * authored_rows / shown_rows
                          )
                        : distance;
    }
};

/// Returns how a unit's page is drawn in a side column of `column_rows`.
///
/// @param left left edge of the page's panel, source pixels
/// @param top top edge of the page's panel, source pixels
/// @param bottom lowest row any drawn gadget of the page reaches, exclusive
/// @param column_rows rows of the side column, source pixels from its top
/// @return the page's scale; unscaled when it ends within the column or has
///         no rows below `top`
[[nodiscard]] SidePageScale
side_page_scale(int32_t left, int32_t top, int32_t bottom, int32_t column_rows) noexcept;

} // namespace oa::ui::hud
