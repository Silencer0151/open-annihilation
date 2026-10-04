// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/ui/hud/build_page_fit.hpp"

#include <algorithm>

namespace oa::ui::hud {

SidePageScale
side_page_scale(int32_t left, int32_t top, int32_t bottom, int32_t column_rows) noexcept {
    const auto authored = std::max(0, bottom - top);
    // A column with no room under the panel's top still shows one row.
    const auto room = std::max(1, column_rows - top);
    return {left, top, authored, std::min(authored, room)};
}

} // namespace oa::ui::hud
