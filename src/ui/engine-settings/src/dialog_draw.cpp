// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// How the settings dialog and the OA button are drawn, and the fonts they
// draw their texts in: a dark gunmetal panel with one-pixel raised edges,
// hairline rules, light text with muted hints, and one green accent for
// what is selected.

#include "oa/ui/engine_settings/dialog.hpp"

#include "geometry.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace oa::ui::engine_settings {

namespace renderer = oa::ui::frontend_renderer;
namespace layout = geometry;

namespace {

using renderer::Rgb;
using renderer::SourceRect;

/// The panel's face.
constexpr Rgb kPanelColor{0x1b, 0x1e, 0x19};
/// The header's and the footer's face.
constexpr Rgb kBandColor{0x14, 0x16, 0x12};
/// The section list's face.
constexpr Rgb kListColor{0x17, 0x1a, 0x15};
/// The selected section's entry.
constexpr Rgb kListSelectedColor{0x26, 0x2b, 0x21};
/// An entry, or a footer button, under the pointer or held.
constexpr Rgb kHoverColor{0x20, 0x24, 0x1c};
/// The hairline rules between parts.
constexpr Rgb kRuleColor{0x2b, 0x30, 0x27};
/// The raised edges' light side, top and left.
constexpr Rgb kEdgeLightColor{0x4a, 0x51, 0x43};
/// The raised edges' dark side, bottom and right.
constexpr Rgb kEdgeDarkColor{0x08, 0x09, 0x07};
/// Labels, values and the title.
constexpr Rgb kTextColor{0xe7, 0xe8, 0xdf};
/// Hints and the title's last word.
constexpr Rgb kHintColor{0x9a, 0xa1, 0x90};
/// The section heading and the version.
constexpr Rgb kQuietColor{0x8a, 0x91, 0x80};
/// The list's entries that are not selected.
constexpr Rgb kListTextColor{0xa3, 0xaa, 0x98};
/// The accent: what is selected, On, the slider's filled track and OK.
constexpr Rgb kAccentColor{0x9c, 0xcc, 0x3c};
/// The accent's lighter edge, and the accent under the pointer.
constexpr Rgb kAccentLightColor{0xb6, 0xe0, 0x5a};
/// The accent while it is held.
constexpr Rgb kAccentHeldColor{0x8a, 0xb8, 0x30};
/// Text on the accent.
constexpr Rgb kOnAccentColor{0x10, 0x12, 0x0d};
/// The well of a switch, a level strip and a slider's track.
constexpr Rgb kWellColor{0x12, 0x14, 0x10};
/// The border of a switch, a level strip, a slider's track and a footer button.
constexpr Rgb kControlBorderColor{0x3a, 0x40, 0x34};
/// That border under the pointer.
constexpr Rgb kControlHoverColor{0x5b, 0x63, 0x52};
/// A switch's selected Off half.
constexpr Rgb kOffSelectedColor{0x2c, 0x32, 0x26};
/// A switch's caption that is not selected.
constexpr Rgb kSwitchIdleColor{0x7d, 0x84, 0x74};
/// The footer buttons' and the levels' captions.
constexpr Rgb kButtonTextColor{0xc9, 0xcd, 0xbf};
/// Locks: the padlock, the lock's text and the shared game's note.
constexpr Rgb kLockColor{0xe0, 0xb0, 0x4f};

/// How far a locked row is faded into the panel, in 256ths.
constexpr uint32_t kLockedFade = 115;
/// The columns between a control and its keyboard focus outline.
constexpr int32_t kFocusInset = 2;

/// The OA mark's letters, 4 by 7 each with a column between: for the
/// header and the in-game button.
constexpr int32_t kSmallMarkWidth = 9;
/// The small mark's height.
constexpr int32_t kSmallMarkHeight = 7;
/// The small mark, row by row.
constexpr std::array<uint8_t, kSmallMarkWidth * kSmallMarkHeight> kSmallMarkBits{
    0, 1, 1, 0, 0, 0, 1, 1, 0, //
    1, 0, 0, 1, 0, 1, 0, 0, 1, //
    1, 0, 0, 1, 0, 1, 0, 0, 1, //
    1, 0, 0, 1, 0, 1, 1, 1, 1, //
    1, 0, 0, 1, 0, 1, 0, 0, 1, //
    1, 0, 0, 1, 0, 1, 0, 0, 1, //
    0, 1, 1, 0, 0, 1, 0, 0, 1, //
};
/// The OA mark's letters, 5 by 9 each with two columns between: for the
/// main menu's button.
constexpr int32_t kLargeMarkWidth = 12;
/// The large mark's height.
constexpr int32_t kLargeMarkHeight = 9;
/// The large mark, row by row.
constexpr std::array<uint8_t, kLargeMarkWidth * kLargeMarkHeight> kLargeMarkBits{
    0, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 0, //
    1, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 1, //
    1, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 1, //
    1, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 1, //
    1, 0, 0, 0, 1, 0, 0, 1, 1, 1, 1, 1, //
    1, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 1, //
    1, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 1, //
    1, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 1, //
    0, 1, 1, 1, 0, 0, 0, 1, 0, 0, 0, 1, //
};
/// The padlock, row by row.
constexpr std::array<uint8_t, layout::padlock_width * layout::padlock_height> kPadlockBits{
    0, 1, 1, 1, 0, //
    1, 0, 0, 0, 1, //
    1, 0, 0, 0, 1, //
    1, 1, 1, 1, 1, //
    1, 1, 0, 1, 1, //
    1, 1, 0, 1, 1, //
    1, 1, 1, 1, 1, //
};

/// The OA button's outlined square, as a share of its side: 20 of 32.
constexpr int32_t kButtonSquareNumerator = 20;
/// The OA button's outlined square's share's denominator.
constexpr int32_t kButtonSquareDenominator = 32;
/// The least columns between the large mark and its square's outline.
constexpr int32_t kLargeMarkMargin = 2;

/// The small OA mark.
constexpr renderer::Mark kSmallMark{kSmallMarkWidth, kSmallMarkHeight, kSmallMarkBits};
/// The large OA mark.
constexpr renderer::Mark kLargeMark{kLargeMarkWidth, kLargeMarkHeight, kLargeMarkBits};
/// The padlock.
constexpr renderer::Mark kPadlock{layout::padlock_width, layout::padlock_height, kPadlockBits};

/// Where a text sits along a box's width.
enum class Align : uint8_t {
    left,   ///< at the box's left
    centre, ///< in the box's middle
    right,  ///< ending at the box's right
};

/// Returns a rectangle grown on every side.
///
/// @param rect the rectangle
/// @param by the columns and rows added on each side
/// @return the grown rectangle
SourceRect grown(const SourceRect& rect, int32_t by) noexcept {
    return {rect.x - by, rect.y - by, rect.width + 2 * by, rect.height + 2 * by};
}

/// Returns a text's width with extra columns after each glyph but the last.
///
/// @param font the font
/// @param text the text
/// @param tracking the extra columns
/// @return the width, in source pixels
int32_t tracked_width(const renderer::TextFont& font, std::string_view text, int32_t tracking) {
    if (text.empty())
        return 0;
    return renderer::text_width(font, text) + tracking * static_cast<int32_t>(text.size() - 1);
}

/// Draws a text in a box, its capitals centred on the box's height.
///
/// @param[in,out] target the surface
/// @param placement where the dialog lands
/// @param font the font
/// @param text the text
/// @param box the box
/// @param align where the text sits along the box's width
/// @param color the text's colour
/// @param tracking extra columns after each glyph but the last
void draw_boxed_text(
    renderer::Surface& target,
    const renderer::Placement& placement,
    const renderer::TextFont& font,
    std::string_view text,
    const SourceRect& box,
    Align align,
    Rgb color,
    int32_t tracking = 0
) {
    const int32_t width = tracked_width(font, text, tracking);
    int32_t pen = box.x;
    if (align == Align::centre)
        pen = box.x + (box.width - width) / 2;
    else if (align == Align::right)
        pen = box.x + box.width - width;
    // A game font's capitals start one row under the pen row.
    const int32_t capitals = font.font.nominal_height;
    const int32_t pen_row = box.y + (box.height - capitals) / 2 - 1;
    if (tracking == 0) {
        renderer::draw_text(target, placement, font, text, pen, pen_row, color);
        return;
    }
    for (std::size_t index = 0; index < text.size(); ++index) {
        pen = renderer::draw_text(
                  target, placement, font, text.substr(index, 1), pen, pen_row, color
              ) +
              tracking;
    }
}

/// Draws the header: the mark, the title, the shared game's note and the version.
///
/// @param[in,out] target the surface
/// @param placement where the dialog lands
/// @param dialog the dialog
/// @param fonts the fonts
void draw_header(
    renderer::Surface& target,
    const renderer::Placement& placement,
    const Dialog& dialog,
    const DialogFonts& fonts
) {
    const int32_t inner = dialog_width - 2 * layout::edge;
    renderer::fill_source_rect(
        target,
        placement,
        {layout::edge, layout::header_top, inner, layout::header_height},
        kBandColor
    );
    renderer::fill_source_rect(
        target, placement, {layout::edge, layout::header_rule_row, inner, 1}, kRuleColor
    );
    const SourceRect mark = layout::header_mark;
    renderer::draw_outline(target, placement, mark, kAccentColor);
    renderer::draw_mark(
        target,
        placement,
        kSmallMark,
        mark.x + (mark.width - kSmallMarkWidth) / 2,
        mark.y + (mark.height - kSmallMarkHeight + 1) / 2,
        kAccentColor
    );
    const int32_t title_left = mark.x + mark.width + layout::header_gap;
    const SourceRect title{
        title_left, layout::header_top, layout::title_width, layout::header_height
    };
    draw_boxed_text(
        target,
        placement,
        fonts.regular,
        layout::title_text,
        title,
        Align::left,
        kTextColor,
        layout::heading_tracking
    );
    const SourceRect suffix{
        title.x + title.width + layout::header_gap,
        layout::header_top,
        layout::title_suffix_width,
        layout::header_height,
    };
    draw_boxed_text(
        target,
        placement,
        fonts.regular,
        layout::title_suffix_text,
        suffix,
        Align::left,
        kHintColor,
        layout::heading_tracking
    );
    const SourceRect version{
        layout::content_right - layout::version_width,
        layout::header_top,
        layout::version_width,
        layout::header_height,
    };
    draw_boxed_text(
        target, placement, fonts.small, dialog.version, version, Align::right, kQuietColor
    );
    if (dialog.locks.shared_game) {
        const int32_t version_left =
            version.x + version.width - renderer::text_width(fonts.small, dialog.version);
        const int32_t shared_left = suffix.x + suffix.width + layout::header_gap;
        const SourceRect shared{
            shared_left,
            layout::header_top,
            version_left - layout::version_gap - shared_left,
            layout::header_height,
        };
        draw_boxed_text(
            target,
            placement,
            fonts.small,
            layout::shared_game_text,
            shared,
            Align::right,
            kLockColor
        );
    }
}

/// Draws the section list.
///
/// @param[in,out] target the surface
/// @param placement where the dialog lands
/// @param dialog the dialog
/// @param fonts the fonts
void draw_list(
    renderer::Surface& target,
    const renderer::Placement& placement,
    const Dialog& dialog,
    const DialogFonts& fonts
) {
    const int32_t body_height = layout::footer_rule_row - layout::body_top;
    renderer::fill_source_rect(
        target,
        placement,
        {layout::edge, layout::body_top, layout::list_width, body_height},
        kListColor
    );
    renderer::fill_source_rect(
        target, placement, {layout::list_rule_column, layout::body_top, 1, body_height}, kRuleColor
    );
    renderer::fill_source_rect(target, placement, layout::list_divider(), kRuleColor);
    for (std::size_t index = 0; index < layout::page_count; ++index) {
        const auto page = static_cast<Page>(index);
        const SourceRect item = layout::list_item(page);
        const int32_t control = page_control(page);
        const bool selected = page == dialog.page;
        const bool hovered = dialog.hovered == control || dialog.pressed == control;
        Rgb text = kListTextColor;
        if (selected) {
            renderer::fill_source_rect(target, placement, item, kListSelectedColor);
            renderer::fill_source_rect(
                target,
                placement,
                {item.x + layout::list_marker_offset,
                 item.y + (item.height - layout::list_marker_height) / 2,
                 layout::list_marker_width,
                 layout::list_marker_height},
                kAccentColor
            );
            text = kTextColor;
        } else if (hovered) {
            renderer::fill_source_rect(target, placement, item, kHoverColor);
            text = kTextColor;
        }
        const SourceRect caption{
            item.x + layout::list_text_offset,
            item.y,
            item.width - layout::list_text_offset - layout::list_text_margin,
            item.height,
        };
        draw_boxed_text(
            target, placement, fonts.regular, layout::page_name(page), caption, Align::left, text
        );
        if (dialog.focused == control)
            renderer::draw_outline(target, placement, item, kAccentColor);
    }
}

/// Tells whether a switch setting is On.
///
/// @param settings the settings
/// @param setting a switch setting
/// @return true for On
bool switch_on(const EngineSettings& settings, Setting setting) noexcept {
    switch (setting) {
    case Setting::wheel_zoom:
        return settings.wheel_zoom;
    case Setting::escape_opens_menu:
        return settings.escape_opens_menu;
    case Setting::switch_alt:
        return settings.switch_alt;
    case Setting::frame_stats:
        return settings.frame_stats;
    default:
        return false;
    }
}

/// Draws an Off/On switch.
///
/// @param[in,out] target the surface
/// @param placement where the dialog lands
/// @param area the switch
/// @param on the switch is On
/// @param hovered the pointer is over it
/// @param fonts the fonts
void draw_switch(
    renderer::Surface& target,
    const renderer::Placement& placement,
    const SourceRect& area,
    bool on,
    bool hovered,
    const DialogFonts& fonts
) {
    renderer::fill_source_rect(target, placement, area, kWellColor);
    renderer::draw_outline(
        target, placement, area, hovered ? kControlHoverColor : kControlBorderColor
    );
    const int32_t half = (area.width - 2) / 2;
    const SourceRect off{area.x + 1, area.y + 1, half, area.height - 2};
    const SourceRect on_half{area.x + 1 + half, area.y + 1, half, area.height - 2};
    if (on)
        renderer::fill_source_rect(target, placement, on_half, kAccentColor);
    else
        renderer::fill_source_rect(target, placement, off, kOffSelectedColor);
    draw_boxed_text(
        target,
        placement,
        fonts.small,
        layout::off_text,
        off,
        Align::centre,
        on ? kSwitchIdleColor : kTextColor
    );
    draw_boxed_text(
        target,
        placement,
        fonts.small,
        layout::on_text,
        on_half,
        Align::centre,
        on ? kOnAccentColor : kSwitchIdleColor
    );
}

/// Draws the strip of anti-aliasing levels.
///
/// @param[in,out] target the surface
/// @param placement where the dialog lands
/// @param area the strip
/// @param level the level chosen
/// @param hovered the pointer is over it
/// @param fonts the fonts
void draw_levels(
    renderer::Surface& target,
    const renderer::Placement& placement,
    const SourceRect& area,
    AntiAliasing level,
    bool hovered,
    const DialogFonts& fonts
) {
    renderer::fill_source_rect(target, placement, area, kWellColor);
    renderer::draw_outline(
        target, placement, area, hovered ? kControlHoverColor : kControlBorderColor
    );
    for (std::size_t index = 0; index < anti_aliasing_levels.size(); ++index) {
        const SourceRect segment{
            area.x + 1 + static_cast<int32_t>(index) * layout::level_width,
            area.y + 1,
            layout::level_width,
            area.height - 2,
        };
        const bool selected = anti_aliasing_levels[index] == level;
        if (selected)
            renderer::fill_source_rect(target, placement, segment, kAccentColor);
        draw_boxed_text(
            target,
            placement,
            fonts.small,
            layout::level_caption(anti_aliasing_levels[index]),
            segment,
            Align::centre,
            selected ? kOnAccentColor : kButtonTextColor
        );
    }
}

/// Draws a slider: its track filled up to the knob, its stops and its knob.
///
/// @param[in,out] target the surface
/// @param placement where the dialog lands
/// @param area the slider's track area
/// @param stop the stop the knob is on
/// @param stops the slider's stops
/// @param locked the slider cannot be moved now
/// @param hovered the pointer is over it, or holds it
void draw_slider(
    renderer::Surface& target,
    const renderer::Placement& placement,
    const SourceRect& area,
    int32_t stop,
    int32_t stops,
    bool locked,
    bool hovered
) {
    const SourceRect track{area.x, area.y + layout::track_offset, area.width, layout::track_height};
    renderer::fill_source_rect(target, placement, track, kWellColor);
    renderer::draw_outline(target, placement, track, kControlBorderColor);
    const int32_t knob = layout::knob_column(area, stop, stops);
    renderer::fill_source_rect(
        target,
        placement,
        {track.x + 1, track.y + 1, knob - track.x - 1, track.height - 2},
        locked ? kControlBorderColor : kAccentColor
    );
    const int32_t travel = area.width - layout::knob_width;
    if (stops > 1 && travel / (stops - 1) >= layout::least_stop_spacing) {
        for (int32_t index = 0; index < stops; ++index) {
            renderer::fill_source_rect(
                target,
                placement,
                {layout::knob_column(area, index, stops),
                 area.y + layout::stop_offset,
                 1,
                 layout::stop_height},
                kControlBorderColor
            );
        }
    }
    const SourceRect knob_rect{
        knob - layout::knob_width / 2, area.y, layout::knob_width, layout::knob_height
    };
    Rgb face = kAccentColor;
    if (locked)
        face = kControlHoverColor;
    else if (hovered)
        face = kAccentLightColor;
    renderer::fill_source_rect(target, placement, knob_rect, face);
    renderer::draw_outline(target, placement, knob_rect, kOnAccentColor);
}

/// Draws the padlock and a lock's text, ending at the lock area's right.
///
/// @param[in,out] target the surface
/// @param placement where the dialog lands
/// @param area the lock area
/// @param lock the lock
/// @param fonts the fonts
void draw_lock(
    renderer::Surface& target,
    const renderer::Placement& placement,
    const SourceRect& area,
    Lock lock,
    const DialogFonts& fonts
) {
    const std::string_view text = layout::lock_text(lock);
    const int32_t text_width = renderer::text_width(fonts.small, text);
    const int32_t left =
        area.x + area.width - text_width - layout::padlock_gap - layout::padlock_width;
    renderer::draw_mark(
        target,
        placement,
        kPadlock,
        left,
        area.y + (area.height - layout::padlock_height) / 2,
        kLockColor
    );
    const SourceRect text_area{
        left + layout::padlock_width + layout::padlock_gap, area.y, text_width, area.height
    };
    draw_boxed_text(target, placement, fonts.small, text, text_area, Align::left, kLockColor);
}

/// Draws the open section: its heading and its rows.
///
/// @param[in,out] target the surface
/// @param placement where the dialog lands
/// @param dialog the dialog
/// @param fonts the fonts
void draw_section(
    renderer::Surface& target,
    const renderer::Placement& placement,
    const Dialog& dialog,
    const DialogFonts& fonts
) {
    draw_boxed_text(
        target,
        placement,
        fonts.small,
        layout::page_heading(dialog.page),
        layout::heading,
        Align::left,
        kQuietColor,
        layout::heading_tracking
    );
    const layout::Rows rows = layout::place_rows(dialog.page, dialog.locks);
    for (std::size_t index = 0; index < rows.count; ++index) {
        const layout::Row& row = rows.rows[index];
        const bool locked = row.lock != Lock::none;
        const bool hovered =
            !locked && (dialog.hovered == row.control || dialog.pressed == row.control);
        renderer::fill_source_rect(
            target, placement, {layout::content_left, row.top, layout::content_width, 1}, kRuleColor
        );
        draw_boxed_text(
            target,
            placement,
            fonts.regular,
            layout::label_of(row.setting),
            row.label,
            Align::left,
            kTextColor
        );
        for (std::size_t line = 0; line < row.hint_lines; ++line) {
            draw_boxed_text(
                target,
                placement,
                fonts.small,
                layout::hint_line(row.setting, dialog.chosen, line),
                row.hints[line],
                Align::left,
                kHintColor
            );
        }
        if (row.setting == Setting::anti_aliasing) {
            draw_levels(
                target, placement, row.control_area, dialog.chosen.anti_aliasing, hovered, fonts
            );
        } else if (layout::is_slider(row.setting)) {
            draw_slider(
                target,
                placement,
                row.control_area,
                layout::stop_of(dialog.chosen, row.setting),
                layout::slider_of(row.setting).stops,
                locked,
                hovered
            );
            draw_boxed_text(
                target,
                placement,
                fonts.regular,
                layout::value_text(row.setting, dialog.chosen),
                row.value,
                Align::right,
                kTextColor
            );
        } else {
            draw_switch(
                target,
                placement,
                row.control_area,
                switch_on(dialog.chosen, row.setting),
                hovered,
                fonts
            );
        }
        if (locked) {
            renderer::blend_source_rect(
                target,
                placement,
                {layout::content_left, row.top + 1, layout::content_width, row.height - 1},
                kPanelColor,
                kLockedFade
            );
            draw_lock(target, placement, row.lock_area, row.lock, fonts);
        }
        if (dialog.focused == row.control && !locked)
            renderer::draw_outline(
                target, placement, grown(row.control_area, kFocusInset), kAccentColor
            );
    }
    renderer::fill_source_rect(
        target, placement, {layout::content_left, rows.bottom, layout::content_width, 1}, kRuleColor
    );
}

/// Draws the footer: Restore defaults, Cancel and OK.
///
/// @param[in,out] target the surface
/// @param placement where the dialog lands
/// @param dialog the dialog
/// @param fonts the fonts
void draw_footer(
    renderer::Surface& target,
    const renderer::Placement& placement,
    const Dialog& dialog,
    const DialogFonts& fonts
) {
    const int32_t inner = dialog_width - 2 * layout::edge;
    renderer::fill_source_rect(
        target, placement, {layout::edge, layout::footer_rule_row, inner, 1}, kRuleColor
    );
    renderer::fill_source_rect(
        target,
        placement,
        {layout::edge, layout::footer_top, inner, layout::footer_height},
        kBandColor
    );
    const std::array<std::pair<int32_t, std::string_view>, 3> buttons{{
        {restore_control, layout::restore_text},
        {cancel_control, layout::cancel_text},
        {ok_control, layout::ok_text},
    }};
    for (const auto& [control, caption] : buttons) {
        const SourceRect button = layout::footer_button(control);
        const bool held = dialog.pressed == control && dialog.hovered == control;
        const bool hovered = dialog.hovered == control;
        if (control == ok_control) {
            Rgb face = kAccentColor;
            if (held)
                face = kAccentHeldColor;
            else if (hovered)
                face = kAccentLightColor;
            renderer::fill_source_rect(target, placement, button, face);
            renderer::draw_outline(target, placement, button, kAccentLightColor);
            draw_boxed_text(
                target, placement, fonts.small, caption, button, Align::centre, kOnAccentColor
            );
        } else {
            if (held || hovered)
                renderer::fill_source_rect(target, placement, button, kHoverColor);
            renderer::draw_outline(
                target, placement, button, hovered ? kControlHoverColor : kControlBorderColor
            );
            draw_boxed_text(
                target,
                placement,
                fonts.small,
                caption,
                button,
                Align::centre,
                hovered ? kTextColor : kButtonTextColor
            );
        }
        if (dialog.focused == control)
            renderer::draw_outline(target, placement, grown(button, kFocusInset), kAccentColor);
    }
}

/// The game's palette, which the GUI fonts' glyph pixels index.
constexpr std::string_view kGamePalettePath = "palettes/palette.pal";

} // namespace

DialogFonts load_dialog_fonts(oa::AssetStore& assets) {
    const std::vector<uint8_t> bytes = assets.read(kGamePalettePath).bytes;
    oa::PaletteBytes palette{};
    if (bytes.size() < palette.size())
        throw std::runtime_error("the game's palette is short: " + std::string(kGamePalettePath));
    std::copy_n(bytes.begin(), palette.size(), palette.begin());
    DialogFonts fonts;
    fonts.regular =
        renderer::text_font(oa::formats::fnt::load_gaf(assets, "anims/hattfont12.gaf"), palette);
    fonts.small =
        renderer::text_font(oa::formats::fnt::load_gaf(assets, "anims/hattfont11.gaf"), palette);
    return fonts;
}

void draw_dialog(
    renderer::Surface& target,
    const renderer::Placement& placement,
    const Dialog& dialog,
    const DialogFonts& fonts
) {
    const SourceRect whole{0, 0, dialog_width, dialog_height};
    renderer::fill_source_rect(target, placement, whole, kPanelColor);
    draw_header(target, placement, dialog, fonts);
    draw_list(target, placement, dialog, fonts);
    draw_section(target, placement, dialog, fonts);
    draw_footer(target, placement, dialog, fonts);
    renderer::draw_bevel(target, placement, whole, kEdgeLightColor, kEdgeDarkColor);
}

void draw_oa_button(
    renderer::Surface& target,
    const renderer::Placement& placement,
    int32_t side,
    ButtonLook look,
    const DialogFonts&
) {
    const SourceRect whole{0, 0, side, side};
    Rgb face = kPanelColor;
    if (look == ButtonLook::hovered)
        face = kHoverColor;
    else if (look == ButtonLook::pressed)
        face = kBandColor;
    renderer::fill_source_rect(target, placement, whole, face);
    if (look == ButtonLook::pressed)
        renderer::draw_bevel(target, placement, whole, kEdgeDarkColor, kEdgeLightColor);
    else
        renderer::draw_bevel(target, placement, whole, kEdgeLightColor, kEdgeDarkColor);
    const int32_t square_side = side * kButtonSquareNumerator / kButtonSquareDenominator;
    const int32_t square_offset = (side - square_side) / 2;
    const Rgb accent = look == ButtonLook::idle ? kAccentColor : kAccentLightColor;
    renderer::draw_outline(
        target, placement, {square_offset, square_offset, square_side, square_side}, accent
    );
    const renderer::Mark& mark =
        square_side - 2 >= kLargeMarkWidth + 2 * kLargeMarkMargin ? kLargeMark : kSmallMark;
    renderer::draw_mark(
        target,
        placement,
        mark,
        square_offset + (square_side - mark.width) / 2,
        square_offset + (square_side - mark.height + 1) / 2,
        accent
    );
}

} // namespace oa::ui::engine_settings
