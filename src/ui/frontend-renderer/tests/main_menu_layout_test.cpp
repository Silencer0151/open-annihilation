// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The installed game's MAINMENU.GUI layouts: totala1.hpi's, which fits
// FrontendX's pipe frames and is the main menu when no overlay is drawn, and,
// on an install that carries by.ccx, by.ccx's, whose buttons sit under
// by.ccx's main-menu overlay.
#include "oa/ui/frontend_renderer.hpp"
#include "oa/test/game_assets.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <string_view>

namespace {

namespace renderer = oa::ui::frontend_renderer;

int failures = 0;

void check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

struct Rect {
    int16_t x{};
    int16_t y{};
    int16_t width{};
    int16_t height{};
};

bool has_rect(const renderer::MainMenuResources& menu, std::string_view name, Rect expected) {
    for (const auto& gadget : menu.layout.gadgets) {
        if (gadget.common.name != name)
            continue;
        const auto& common = gadget.common;
        return common.x == expected.x && common.y == expected.y && common.width == expected.width &&
               common.height == expected.height;
    }
    return false;
}

const oa::formats::gaf::Sequence*
sequence_named(const oa::formats::gaf::Archive& archive, std::string_view name) {
    for (const auto& sequence : archive.sequences)
        if (sequence.name == name)
            return &sequence;
    return nullptr;
}

// The frame's covered texels in columns [0, columns) drawn at (x, y) through
// FrontendX's palette.
bool frame_drawn_at(
    const renderer::Surface& surface,
    const renderer::MainMenuResources& menu,
    const oa::formats::gaf::Frame& frame,
    int x,
    int y,
    int columns
) {
    const auto rendered = oa::formats::gaf::render_normal(frame);
    if (!rendered.ok() || !menu.background.palette)
        return false;
    const auto& image = *rendered.frame;
    const auto& palette = *menu.background.palette;
    for (int row = 0; row < image.height; ++row)
        for (int column = 0; column < columns; ++column) {
            const auto source =
                static_cast<std::size_t>(row) * image.width + static_cast<std::size_t>(column);
            if (image.coverage[source] == 0)
                continue;
            const auto color =
                static_cast<std::size_t>(image.pixels[source]) * oa::palette_entry_bytes;
            const auto target = (static_cast<std::size_t>(y + row) * surface.width +
                                 static_cast<std::size_t>(x + column)) *
                                3U;
            if (!std::equal(
                    palette.begin() + static_cast<std::ptrdiff_t>(color),
                    palette.begin() + static_cast<std::ptrdiff_t>(color + 3),
                    surface.rgb.begin() + static_cast<std::ptrdiff_t>(target)
                ))
                return false;
        }
    return true;
}

// Rows [top, bottom] of columns [left, right] show FrontendX unchanged.
bool background_kept(
    const renderer::Surface& surface,
    const renderer::MainMenuResources& menu,
    int left,
    int right,
    int top,
    int bottom
) {
    for (int y = top; y <= bottom; ++y)
        for (int x = left; x <= right; ++x) {
            const auto offset =
                (static_cast<std::size_t>(y) * surface.width + static_cast<std::size_t>(x)) * 3U;
            if (!std::equal(
                    surface.rgb.begin() + static_cast<std::ptrdiff_t>(offset),
                    surface.rgb.begin() + static_cast<std::ptrdiff_t>(offset + 3),
                    menu.background.rgb.begin() + static_cast<std::ptrdiff_t>(offset)
                ))
                return false;
        }
    return true;
}

// The archive that carries the main-menu overlay and its own MAINMENU.GUI.
constexpr std::string_view kOverlayArchive = "by.ccx";

/// Compares two file names ignoring ASCII case.
///
/// @param a a name
/// @param b another
/// @return true when they differ only in case
bool same_name_ignoring_case(std::string_view a, std::string_view b) {
    return std::equal(a.begin(), a.end(), b.begin(), b.end(), [](char x, char y) {
        const auto upper = [](char c) {
            return c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c;
        };
        return upper(x) == upper(y);
    });
}

/// Tests whether the store mounted by.ccx.
///
/// @param assets the installed game's store
/// @return true when one mount is by.ccx
bool mounts_overlay_archive(const oa::AssetStore& assets) {
    return std::any_of(
        assets.mount_paths().begin(), assets.mount_paths().end(), [](const auto& path) {
            return same_name_ignoring_case(path.filename().string(), kOverlayArchive);
        }
    );
}

/// Checks totala1.hpi's layout, drawn with COMMONGUI.GAF's buttons in the frames.
///
/// @param assets the installed game's store
void check_base_layout(oa::AssetStore& assets) {
    const auto base = renderer::load_main_menu(assets, renderer::MainMenuLayout::base_game);
    check(has_rect(base, "SINGLE", {139, 393, 96, 20}), "base SINGLE");
    check(has_rect(base, "MULTI", {139, 430, 96, 20}), "base MULTI");
    check(has_rect(base, "INTRO", {409, 393, 96, 20}), "base INTRO");
    check(has_rect(base, "EXIT", {409, 430, 96, 20}), "base EXIT");

    // MAINMENU.GAF has no sequence for the four buttons, so the first panel draw takes
    // COMMONGUI.GAF BUTTONS0's 96x20 group, which starts at frame 12.
    const auto* buttons = sequence_named(base.shared_sprites, "BUTTONS0");
    check(buttons != nullptr && buttons->frames.size() > 12, "COMMONGUI.GAF BUTTONS0");
    if (buttons == nullptr || buttons->frames.size() <= 12)
        return;
    const auto& normal = buttons->frames[12];
    check(normal.width == 96 && normal.height == 20, "BUTTONS0 frame 12 is 96x20");
    constexpr int caption_free_columns = 8;
    const auto drawn = renderer::render_main_menu(base);
    for (const auto& origin : {Rect{139, 393}, Rect{139, 430}, Rect{409, 393}, Rect{409, 430}})
        check(
            frame_drawn_at(drawn, base, normal, origin.x, origin.y, caption_free_columns),
            "each base button is BUTTONS0 frame 12 at its GUI rectangle"
        );
    // by.ccx's left buttons reach past the left frames' slots; TA's leave
    // FrontendX there alone.
    check(background_kept(drawn, base, 82, 130, 393, 449), "nothing drawn left of the frames");
    check(background_kept(drawn, base, 512, 559, 393, 449), "nothing drawn right of the frames");
}

/// Checks by.ccx's layout under its overlay.
///
/// It shares Credits and DebugString with totala1.hpi's.
///
/// @param assets the installed game's store
void check_overlay_layout(oa::AssetStore& assets) {
    const auto by_archive = assets.providing_archive("guis/mainmenu.gui");
    check(
        by_archive && same_name_ignoring_case(by_archive->filename().string(), kOverlayArchive),
        "by.ccx's MAINMENU.GUI shadows totala1.hpi's"
    );
    check(
        assets.providing_archive("anims/main_console.gaf") == by_archive,
        "the by.ccx overlay art comes from the same archive"
    );

    const auto overlaid = renderer::load_main_menu(assets, renderer::MainMenuLayout::with_overlay);
    check(has_rect(overlaid, "SINGLE", {82, 393, 96, 20}), "overlay SINGLE");
    check(has_rect(overlaid, "MULTI", {82, 430, 96, 20}), "overlay MULTI");
    check(has_rect(overlaid, "INTRO", {464, 393, 96, 20}), "overlay INTRO");
    check(has_rect(overlaid, "EXIT", {464, 430, 96, 20}), "overlay EXIT");

    const auto base = renderer::load_main_menu(assets, renderer::MainMenuLayout::base_game);
    check(base.layout.gadgets.size() == overlaid.layout.gadgets.size(), "same gadgets");
    for (std::size_t index = 0; index < base.layout.gadgets.size(); ++index) {
        const auto& ours = base.layout.gadgets[index].common;
        const auto& theirs = overlaid.layout.gadgets[index].common;
        if (ours.name == "Credits" || ours.name == "DebugString")
            check(
                ours.x == theirs.x && ours.y == theirs.y && ours.width == theirs.width &&
                    ours.height == theirs.height,
                "Credits and DebugString are shared by both layouts"
            );
    }
}

} // namespace

int main() {
    oa::AssetStore assets = oa::test::require_game_assets("the installed main menu layouts");
    const bool overlay = mounts_overlay_archive(assets);
    try {
        check_base_layout(assets);
        if (overlay)
            check_overlay_layout(assets);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        return 1;
    }
    if (failures != 0)
        return 1;
    std::puts(
        overlay ? "main menu layouts: totala1.hpi 139/409, by.ccx 82/464 under the overlay"
                : "main menu layout: totala1.hpi 139/409; the install carries no by.ccx overlay"
    );
    return 0;
}
