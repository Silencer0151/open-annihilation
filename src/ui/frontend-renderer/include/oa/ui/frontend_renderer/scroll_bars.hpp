// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// A screen's scroll bars on the RGB frontend surface: their SLIDERS art, the
// way grayed parts are drawn, the bars and arrows drawn as 3.1c draws them,
// and a layout's bars and lists bound, driven by the pointer and kept in step
// with each other.

#include "oa/ui/frontend_renderer.hpp"
#include "oa/ui/gui_input/scroll_bar.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace oa::ui::frontend_renderer {

/// The GAF sequence a scroll bar's art is drawn from.
inline constexpr std::string_view kScrollArtSequence = "SLIDERS";

/// Returns the size of each frame of a GAF's SLIDERS sequence.
///
/// @param archive GAF searched; the sequence's name is compared without regard to case.
/// @return The sizes in frame order; empty without the sequence.
[[nodiscard]] std::vector<gui_input::ScrollFrame>
scroll_art_frames(const formats::gaf::Archive& archive);

/// Returns the SLIDERS art a panel's bars are bound with.
///
/// @param own The panel's own GAF, named after its GUI file as in 3.1c; null when it has none.
/// @param shared The shared COMMONGUI.GAF.
/// @return The frame sizes of both.
[[nodiscard]] gui_input::ScrollArtFrames
scroll_art(const formats::gaf::Archive* own, const formats::gaf::Archive& shared);

/// Returns the SLIDERS sequence a bound bar is drawn from.
///
/// @param own The panel's own GAF, or null.
/// @param shared The shared COMMONGUI.GAF.
/// @param art Where the bar's art comes from.
/// @return The sequence, or null for a bar with no art.
[[nodiscard]] const formats::gaf::Sequence* scroll_art_sequence(
    const formats::gaf::Archive* own, const formats::gaf::Archive& shared, gui_input::ScrollArt art
);

/// Tells whether a GAF file is a GUI file's own: named after it, extension aside, without regard to case.
///
/// @param layout Path of the GUI file.
/// @param sprites Path of the GAF file.
/// @return True when the two names' stems are the same.
[[nodiscard]] bool gaf_named_after(std::string_view layout, std::string_view sprites) noexcept;

// ---- Grayed parts ----

/// A rectangle of a surface as palette indices, drawn into as the surface is.
struct IndexedRect {
    int32_t left{};
    int32_t top{};
    int32_t width{};
    int32_t height{};
    std::vector<uint8_t> pixels;
};

/// The palettes and tables grayed parts are drawn with; without a shade
/// palette, grayed parts are drawn as they are.
struct GrayedPaint {
    const PaletteBytes* palette{};       ///< the palette frames are drawn in
    const PaletteBytes* shade_palette{}; ///< the palette the tables index, or null
    std::span<const uint8_t> gray;       ///< the gray table, one entry per palette entry
    std::span<const uint8_t> shade;      ///< the 32-row shade table
};

/// Returns a palette's gray table: each entry's nearest colour to the gray of its channels' mean.
///
/// @param palette Palette the table is built for.
/// @return 256 entries.
[[nodiscard]] std::vector<uint8_t> build_gray_table(const PaletteBytes& palette);

/// Returns the palettes and tables a screen's grayed parts are drawn with.
///
/// Frames are drawn in the screen bitmap's palette, or the GUI palette
/// without one. The tables index the screen bitmap's palette, or PALETTE.PAL
/// for a screen drawn in the GUI palette alone; without either, or without a
/// whole shade table, grayed parts are drawn as they are. The gray table is
/// built from PALETTE.PAL, or else from the palette the tables index, the
/// first time it is needed.
///
/// @param screen The screen's resources.
/// @param[in,out] gray_table Keeps the gray table; empty until it is built.
/// @return The palettes and tables, which refer to `screen` and `gray_table`.
[[nodiscard]] GrayedPaint
grayed_paint(const ScreenResources& screen, std::vector<uint8_t>& gray_table);

/// Reads a rectangle of a surface back as palette indices.
///
/// @param surface Image read.
/// @param palette Palette the image was drawn in; the nearest entry stands for any other colour.
/// @param left Left column.
/// @param top Top row.
/// @param width Width in pixels.
/// @param height Height in pixels.
/// @return The indices of the part of the rectangle on the image.
[[nodiscard]] IndexedRect read_indices(
    const Surface& surface,
    const PaletteBytes& palette,
    int32_t left,
    int32_t top,
    int32_t width,
    int32_t height
);

/// Grays a rectangle through the gray table, then shades it at the grayed level.
///
/// This is how 3.1c draws a grayed part: a scroll bar, a scroll bar's arrow,
/// or a button with art.
///
/// @param[in,out] surface Image drawn on.
/// @param paint Palettes and tables, with a shade palette.
/// @param rect The rectangle's indices, read back and drawn into.
/// @quirk Palette entries from 0x80 up are shaded from the row before the
///        grayed level's, as in 3.1c.
void gray_and_shade(Surface& surface, const GrayedPaint& paint, const IndexedRect& rect);

/// Draws a frame's covered pixels from its top-left corner, whatever its origin.
///
/// @param[in,out] surface Image drawn on.
/// @param palette Palette the frame is drawn in.
/// @param frame Frame drawn; null draws nothing.
/// @param x Left column.
/// @param y Top row.
/// @param[in,out] indices Keeps the drawn pixels' indices where it covers them; null keeps none.
void draw_frame_at(
    Surface& surface,
    const PaletteBytes& palette,
    const formats::gaf::Frame* frame,
    int32_t x,
    int32_t y,
    IndexedRect* indices
);

// ---- Drawing ----

/// Draws a bound bar and its arrows as 3.1c draws them.
///
/// The track is its start frame, as many middle frames as fit and its end
/// frame over its last pixels. Down a vertical bar the knob is its start
/// frame 3 pixels past its position, middle frames and its end frame, at most
/// the bar's height less 6 long and ending at least 4 pixels above the bar's
/// end, centred on the track's end frame; across a horizontal bar it is its
/// start frame alone, 3 pixels past its position and at least its width and 2
/// pixels before the bar's last column, centred down the track's end frame.
/// Each arrow shows its frame, or the next one while it is held. A grayed bar
/// (one whose attributes have attribute::text_list too) is drawn, then grayed
/// and shaded over its rectangle, and so is each of its arrows, which then
/// shows its first frame. A hidden bar or one without art draws nothing.
///
/// @param[in,out] surface Image drawn on.
/// @param paint Palettes and tables.
/// @param art The bar's SLIDERS sequence; null draws nothing.
/// @param bar The bar.
/// @param offset_x Column of the bar's panel origin on the surface.
/// @param offset_y Row of the bar's panel origin on the surface.
/// @param held The part the pointer holds; none when it holds none of the bar.
void draw_scroll_bar(
    Surface& surface,
    const GrayedPaint& paint,
    const formats::gaf::Sequence* art,
    const gui_input::ScrollBar& bar,
    int32_t offset_x,
    int32_t offset_y,
    gui_input::ScrollPart held
);

// ---- A layout's bars and lists ----

/// A layout's scroll bars and the lists they scroll, bound as the first draw of its panel binds them.
struct LayoutScrolls {
    /// A scroll bar and its gadget.
    struct Bar {
        std::size_t gadget{}; ///< the bar's index in the layout
        gui_input::ScrollBar bar;
    };

    /// A list and its gadget.
    struct List {
        std::size_t gadget{}; ///< the list's index in the layout
        gui_input::ScrollList list;
    };

    std::vector<Bar> bars;
    std::vector<List> lists;
    gui_input::ScrollHold hold; ///< the bar held; its id is the bar's gadget index
    int32_t line_height{};      ///< the panel font's line height
};

/// What the pointer did to a layout's bars.
struct ScrollInput {
    bool taken{};        ///< the pointer was on a shown bar or arrow, or holds one
    int32_t changed{-1}; ///< the gadget of the bar whose handler runs; -1 for none
};

/// Binds a layout's scroll bars and readies its lists, as 3.1c's first draw of a panel does.
///
/// From gadget `first` on, each bar is bound (bind_scroll_bar) and its
/// gadget takes the bound rectangle, range, knob size and knob; a bar that
/// shares its association with a list is hidden until the list is filled.
/// Each list is readied (scroll_list_trim) and its gadget takes the trimmed
/// height.
///
/// @param[in,out] layout The layout; its bars and lists change as described.
/// @param art SLIDERS frames the panel can use.
/// @param line_height The panel font's line height.
/// @param first First gadget bound; the ones before it are left as they are.
/// @return The bound bars and lists.
[[nodiscard]] LayoutScrolls bind_layout_scrolls(
    ui::gui_layout::Layout& layout,
    const gui_input::ScrollArtFrames& art,
    int32_t line_height,
    std::size_t first = 1
);

/// Takes up what a screen has changed in its layout since binding: each
/// bar's and list's activity, and each bar's knob and lock.
///
/// @param[in,out] scrolls The layout's bars and lists.
/// @param layout The layout.
void refresh_layout_scrolls(LayoutScrolls& scrolls, const ui::gui_layout::Layout& layout);

/// Finds a bound bar by its gadget.
///
/// @param scrolls The layout's bars and lists.
/// @param gadget The bar's index in the layout.
/// @return The bar, or null.
[[nodiscard]] LayoutScrolls::Bar* find_layout_bar(LayoutScrolls& scrolls, std::size_t gadget);

/// Finds a readied list by its gadget.
///
/// @param scrolls The layout's bars and lists.
/// @param gadget The list's index in the layout.
/// @return The list, or null.
[[nodiscard]] LayoutScrolls::List* find_layout_list(LayoutScrolls& scrolls, std::size_t gadget);

/// Fills a list with rows as 3.1c fills a text list, and shows or hides its scroll bar.
///
/// When the list is shown, the first bar that shares its association is
/// shown exactly when the rows do not all fit, and is then fitted
/// (scroll_bar_fit_list) to the association's first list, as in 3.1c; its
/// gadget takes its activity, range and knob size.
///
/// @param[in,out] scrolls The layout's bars and lists.
/// @param[in,out] layout The layout.
/// @param gadget The list's index in the layout; one that is not a readied list is ignored.
/// @param count Rows.
/// @quirk The knob keeps its position, as in 3.1c, so after a refill it can
///        stand away from the first row the list now shows until it moves.
void fill_layout_list(
    LayoutScrolls& scrolls, ui::gui_layout::Layout& layout, std::size_t gadget, int32_t count
);

/// Selects a list's row and brings it into view, as scroll_list_select does, moving its bar's knob.
///
/// @param[in,out] scrolls The layout's bars and lists.
/// @param[in,out] layout The layout; the bar's gadget takes its knob.
/// @param gadget The list's index in the layout.
/// @param row Row selected.
void select_layout_list_row(
    LayoutScrolls& scrolls, ui::gui_layout::Layout& layout, std::size_t gadget, int32_t row
);

/// Moves a list's selection one row, as the Up and Down keys do (scroll_list_step).
///
/// A selection that moved on the page brings the rest of the list's
/// association in line (sync_layout_group); one off the page is brought into
/// view with its bar's knob instead.
///
/// @param[in,out] scrolls The layout's bars and lists.
/// @param[in,out] layout The layout; the bar's gadget takes its knob.
/// @param gadget The list's index in the layout; one that is not a readied list is ignored.
/// @param forward True to move toward the list's end.
/// @return True when the selection moved on the page.
bool step_layout_list_row(
    LayoutScrolls& scrolls, ui::gui_layout::Layout& layout, std::size_t gadget, bool forward
);

/// Brings the rest of a bar's or list's association in line with it, as 3.1c does after one moves.
///
/// A bar puts each list of its association that has a pitch where its knob
/// stands (scroll_list_follow_bar). A list gives the other lists its first
/// row and selection, and puts the bars' knobs where its first row stands
/// (scroll_bar_follow_list). The bars' gadgets take their knobs.
///
/// @param[in,out] scrolls The layout's bars and lists.
/// @param[in,out] layout The layout.
/// @param gadget The bar or list that moved.
void sync_layout_group(LayoutScrolls& scrolls, ui::gui_layout::Layout& layout, std::size_t gadget);

/// Presses the pointer on a layout's bars, as scroll_press does.
///
/// The last bar whose part lies under the pointer takes the press; any other
/// press ends the hold. A bar whose knob moved brings its lists in line.
///
/// @param[in,out] scrolls The layout's bars and lists.
/// @param[in,out] layout The layout; a moved bar's gadget takes its knob.
/// @param x Pointer column in panel coordinates.
/// @param y Pointer row in panel coordinates.
/// @param tick Current 30 Hz tick.
/// @return Whether a bar took the press and whose handler runs.
ScrollInput press_layout_scrolls(
    LayoutScrolls& scrolls, ui::gui_layout::Layout& layout, int32_t x, int32_t y, uint32_t tick
);

/// Moves the pointer over a layout's bars, as scroll_move does.
///
/// @param[in,out] scrolls The layout's bars and lists.
/// @param x Pointer column in panel coordinates.
/// @param y Pointer row in panel coordinates.
/// @return True while a bar is held.
bool move_layout_scrolls(LayoutScrolls& scrolls, int32_t x, int32_t y);

/// Releases the pointer over a layout's bars, as scroll_release does.
///
/// @param[in,out] scrolls The layout's bars and lists.
/// @param[in,out] layout The layout; a moved bar's gadget takes its knob.
/// @param x Pointer column in panel coordinates.
/// @param y Pointer row in panel coordinates.
/// @return Whether a bar was held and whose handler runs.
ScrollInput release_layout_scrolls(
    LayoutScrolls& scrolls, ui::gui_layout::Layout& layout, int32_t x, int32_t y
);

/// Runs a layout's held bar for one update, as scroll_hold_tick does.
///
/// @param[in,out] scrolls The layout's bars and lists.
/// @param[in,out] layout The layout; a moved bar's gadget takes its knob.
/// @param tick Current 30 Hz tick.
/// @return The gadget of the bar whose handler runs, or -1.
int32_t tick_layout_scrolls(LayoutScrolls& scrolls, ui::gui_layout::Layout& layout, uint32_t tick);

/// Draws a layout's shown bars and their arrows, each as draw_scroll_bar does.
///
/// @param[in,out] surface Image drawn on.
/// @param paint Palettes and tables.
/// @param own The panel's own GAF, or null.
/// @param shared The shared COMMONGUI.GAF.
/// @param scrolls The layout's bars and lists.
/// @param offset_x Column of the panel origin on the surface.
/// @param offset_y Row of the panel origin on the surface.
void draw_layout_scrolls(
    Surface& surface,
    const GrayedPaint& paint,
    const formats::gaf::Archive* own,
    const formats::gaf::Archive& shared,
    const LayoutScrolls& scrolls,
    int32_t offset_x,
    int32_t offset_y
);

} // namespace oa::ui::frontend_renderer
