// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// 8-bit draw host of the gadget engine. Each panel draws into the face
// surface of its root record (GadgetRefs::surface) in panel-relative
// coordinates, and the faces are blitted onto the screen surface afterwards.
// GAF art stays behind the engine's opaque pointers and is reached through
// GadgetArt; text in FNT fonts goes through a hook. Display tables, the
// display size and the screen captured under a new panel come from the bound
// present display.

#include "oa/ui/gui_input/gadget_panel.hpp"
#include "oa/present/surface.hpp"

#include <cstddef>
#include <cstdint>

namespace oa::ui::gadget_render {

using ui::gui_input::GadgetOwner;
using ui::gui_input::GadgetPanel;

// Flags of the whole-panel draw beyond ui::gui_input::panel_flag; no_save_under
// and redraw are the panel_flag bits of the same names.
namespace draw_flag {
inline constexpr uint32_t release = 0x2; // restore the screen under the panel and free its surfaces
inline constexpr uint32_t records = 0x4; // redraw the records without the panel face
inline constexpr uint32_t buttons = 0x8; // with `records`, buttons are redrawn too
inline constexpr uint32_t no_save_under = ui::gui_input::panel_flag::no_save_under;
// Redraw the face and every record.
inline constexpr uint32_t redraw = ui::gui_input::panel_flag::redraw;
} // namespace draw_flag

// Slots of the panel colour map (GadgetPanel::colors) the draws use.
namespace color_slot {
inline constexpr size_t dark_edge = 0;   // bevel shadow, pressed caption
inline constexpr size_t underline = 2;   // quick-key underline
inline constexpr size_t empty_image = 7; // hot surface without art
inline constexpr size_t caret = 9;
inline constexpr size_t quick_key = 10;   // quick-key glyph of a bottom caption
inline constexpr size_t crossed_row = 12; // image-list row flagged 2
inline constexpr size_t scroll_value = 15;
inline constexpr size_t light_edge = 17;
inline constexpr size_t grayed_face = 19;
inline constexpr size_t face = 20;
} // namespace color_slot

// Shade and light levels the draws apply.
namespace shade_level {
inline constexpr int32_t grayed = -0x14;
inline constexpr int32_t header_first = -0x13; // list header rows take -0x13..-0x16
inline constexpr int32_t header_rows = 4;
inline constexpr int32_t image_flagged = -0x1C;
inline constexpr int32_t selected_row = 0x1E;
inline constexpr int32_t selected_image = 0x14;
} // namespace shade_level

// GAF files, sequences and frames behind the engine's pointers. Null members
// act as absent art.
struct GadgetArt {
    void* context = nullptr;
    // Loads a GAF file by path (after a size check); null when missing.
    const void* (*load_gaf)(void* context, const char* path) = nullptr;
    // Loads a whole file for a type-7 or type-8 record; null when missing.
    const void* (*load_file)(void* context, const char* path) = nullptr;
    // The sequence of a loaded GAF file named `name` in any case, or null.
    const void* (*find_sequence)(void* context, const void* file, const char* name) = nullptr;
    // First sequence of a loaded GAF file: a GAF font's glyphs.
    const void* (*first_sequence)(void* context, const void* file) = nullptr;
    // Frame `index` of a sequence, or null when out of range.
    Sprite* (*frame)(void* context, const void* sequence, int32_t index) = nullptr;
    // Leading frame-count word of a sequence.
    int32_t (*frame_count)(void* context, const void* sequence) = nullptr;
    // Frame of row `item` of an image list's pointer table: the frame the
    // record that entry points at carries.
    const Sprite* (*list_image)(void* context, const void* images, int32_t item) = nullptr;
    // Frees a file the first draw loaded, when its panel is released.
    void (*release)(void* context, const void* file) = nullptr;
};

inline constexpr int32_t kPanelSlots = 8;

// Surfaces the whole-panel draw creates for one panel: its face (the root
// record's GadgetRefs::surface) and the screen pixels saved under it.
struct PanelSurfaces {
    const GadgetOwner* owner = nullptr;
    present::SurfaceBuffer face;
    present::SurfaceBuffer save_under;
    bool saved = false;
};

struct GadgetRenderer {
    GadgetArt art;
    void* text_context = nullptr;
    // Draws text in the display's active FNT font in `color`;
    // null draws nothing.
    void (*fnt_text)(
        void* context,
        Surface* target,
        const char* text,
        int32_t x,
        int32_t y,
        int32_t max_width,
        int32_t color
    ) = nullptr;
    int32_t text_color = 0; // FNT text colour as last set
    PanelSurfaces panels[kPanelSlots];
};

/// Installs the renderer as the panel's draw host.
///
/// Routes draw requests, the whole-panel draw, text measures, frame counts
/// and FNT font selection to the renderer.
///
/// @param[in,out] panel GUI context whose host is replaced.
/// @param renderer Renderer; must outlive the binding.
void bind_renderer(GadgetPanel& panel, GadgetRenderer& renderer);

// ---- Text ----

/// Measures a string in the active GAF font, or the display FNT font when none is active.
///
/// @param renderer Renderer supplying the art.
/// @param panel GUI context whose active GAF font is used.
/// @param text String to measure.
/// @return Width in pixels.
[[nodiscard]] int32_t
text_width(const GadgetRenderer& renderer, const GadgetPanel& panel, const char* text);

/// Returns the line height of the active font.
///
/// @param renderer Renderer supplying the art.
/// @param panel GUI context whose active GAF font is used.
/// @return Height of glyph 'I' plus two in the active GAF font, else the
///         display FNT font height, in pixels.
[[nodiscard]] int32_t text_height(const GadgetRenderer& renderer, const GadgetPanel& panel);

/// Draws a string glyph by glyph in the active GAF font.
///
/// Stops before a glyph wider than what is left of the width. With
/// game-text hooks installed (oa/present/game_text.hpp), text with a byte
/// from 0x80 up is read as the settings say: each character the font has a
/// glyph for is drawn with it, and each run of the rest in the modern fonts
/// on the font's baseline, stopping before a run wider than what is left.
///
/// @param[in,out] renderer Renderer supplying the art and FNT hook.
/// @param panel GUI context whose active GAF font is used.
/// @param[in,out] target 8-bit surface drawn into.
/// @param text String to draw.
/// @param x Left edge in target pixels.
/// @param y Top edge in target pixels.
/// @param max_width Width limit in pixels; -1 for no limit.
/// @param level Nonzero light level applied to the glyphs; 0 draws them plainly.
/// @quirk Without a GAF font the FNT path ignores `max_width`.
void draw_text(
    GadgetRenderer& renderer,
    const GadgetPanel& panel,
    Surface* target,
    const char* text,
    int32_t x,
    int32_t y,
    int32_t max_width,
    int32_t level
);

/// Draws text wrapped at spaces and CRs, one line per text height plus two, while the height lasts.
///
/// @param[in,out] renderer Renderer supplying the art and FNT hook.
/// @param panel GUI context whose active GAF font is used.
/// @param[in,out] target 8-bit surface drawn into.
/// @param[in,out] text Text to draw; split in place while drawing and restored.
/// @param x Left edge in target pixels.
/// @param y Top edge of the first line.
/// @param width Line width in pixels.
/// @param height Height available in pixels.
/// @param level Light level passed to draw_text().
/// @return The y below the last line drawn.
/// @quirk A first word wider than `width` draws an empty line and loses its
///        first character.
int32_t draw_wrapped_text(
    GadgetRenderer& renderer,
    const GadgetPanel& panel,
    Surface* target,
    char* text,
    int32_t x,
    int32_t y,
    int32_t width,
    int32_t height,
    int32_t level
);

/// Loads a GAF font into a slot and makes it active.
///
/// Every glyph's y origin moves up by the height of glyph 'I'.
///
/// @param[in,out] renderer Renderer supplying the art.
/// @param[in,out] panel GUI context whose font slot is set.
/// @param name GAF file name in the panel's GAF directory.
/// @param slot Font slot, 0..2.
void load_gui_font(GadgetRenderer& renderer, GadgetPanel& panel, const char* name, int32_t slot);

/// Forgets the characters each GAF font draws, which text drawn with game
/// text hooks works out once per font and keeps by the address of its glyph
/// sequence. They go when the fonts do: a font loaded later may come at the
/// same address with other glyphs.
void forget_font_characters();

/// Loads the GAF every panel falls back to for its art (GadgetPanel::list_skin)
/// from the GAF directory.
///
/// @param[in,out] renderer Renderer supplying the art.
/// @param[in,out] panel GUI context whose common GAF is set.
/// @param name GAF file name; a missing file keeps the one loaded before.
void load_common_gaf(GadgetRenderer& renderer, GadgetPanel& panel, const char* name);

// ---- Record draws (panel-relative, into the root face) ----

/// Draws a skin sequence over a record.
///
/// One frame is drawn once; nine frames are tiled with corner and edge tiles.
///
/// @param[in,out] renderer Renderer supplying the art.
/// @param[in,out] panel GUI context.
/// @param index Record to cover; the root covers the whole face.
/// @param skin Skin sequence; null uses LISTBOX from the common GAF over the
///        rectangle grown by three.
void draw_skin(GadgetRenderer& renderer, GadgetPanel& panel, int32_t index, const void* skin);

/// Draws a type-1 button.
///
/// Draws the GAF frame for its state or a filled bevel, the caption aligned
/// by the attributes with its quick key underlined, and the grayed shade.
///
/// @param[in,out] renderer Renderer supplying the art.
/// @param[in,out] panel GUI context.
/// @param index Button record.
void draw_button(GadgetRenderer& renderer, GadgetPanel& panel, int32_t index);

/// Draws a type-2 list: background, then image rows or text rows with header, selection and wrapping.
///
/// @param[in,out] renderer Renderer supplying the art.
/// @param[in,out] panel GUI context.
/// @param index List record.
void draw_list(GadgetRenderer& renderer, GadgetPanel& panel, int32_t index);

/// Draws a type-4 scroll bar from SLIDERS art (or two filled frames), its value caption and the locked shade.
///
/// @param[in,out] renderer Renderer supplying the art.
/// @param[in,out] panel GUI context.
/// @param index Scroll bar record.
void draw_scroll_bar(GadgetRenderer& renderer, GadgetPanel& panel, int32_t index);

/// Draws a type-3 text box: background, text and, while captured, the caret.
///
/// @param[in,out] renderer Renderer supplying the art.
/// @param[in,out] panel GUI context.
/// @param index Text box record.
void draw_text_box(GadgetRenderer& renderer, GadgetPanel& panel, int32_t index);

/// Draws a type-13 progress bar with an optional centred value.
///
/// The bar is drawn on the display's draw target. Without a root face the
/// target's descriptor goes into a copy of the context backdrop's
/// descriptor, so the backdrop's own descriptor is left unchanged, where
/// 3.1c leaves it referring to the screen surface.
///
/// @param[in,out] renderer Renderer supplying the art.
/// @param[in,out] panel GUI context.
/// @param index Progress record.
/// @quirk The display draw target's descriptor is copied over the root
///        face's first, so the bar lands on the screen surface, not the
///        panel's face, and the face keeps that descriptor afterwards.
void draw_progress(GadgetRenderer& renderer, GadgetPanel& panel, int32_t index);

/// Draws a type-6 hot surface's image, stretched into its rectangle unless the frame is row-RLE; a fill when it has no image.
///
/// @param[in,out] renderer Renderer supplying the art.
/// @param[in,out] panel GUI context.
/// @param index Hot surface record.
void draw_hot_image(GadgetRenderer& renderer, GadgetPanel& panel, int32_t index);

/// Draws a type-5 label: background and text (wrapped when two lines fit), its quick key underlined or the grayed shade.
///
/// @param[in,out] renderer Renderer supplying the art.
/// @param[in,out] panel GUI context.
/// @param index Label record.
/// @quirk The quick-key underline is measured from the rectangle's left edge,
///        not from the aligned text.
void draw_label(GadgetRenderer& renderer, GadgetPanel& panel, int32_t index);

/// Draws a type-12 record's image frame, lit while flashing, shaded when flagged.
///
/// @param[in,out] renderer Renderer (unused by this draw).
/// @param[in,out] panel GUI context.
/// @param index Image record.
void draw_image(GadgetRenderer& renderer, GadgetPanel& panel, int32_t index);

/// Grays a record's rectangle and darkens it by shade level -0x14.
///
/// @param[in,out] renderer Renderer (unused by this draw).
/// @param[in,out] panel GUI context.
/// @param index Record to shade.
void shade_record(GadgetRenderer& renderer, GadgetPanel& panel, int32_t index);

// ---- Panels ----

/// Draws the top panel.
///
/// Places the root (centred, right of the HUD strip, or back on screen); on
/// the first draw binds each record's art, creates the face over the screen
/// pixels and saves them; draws the face and the records; outlines the
/// focus; releases the surfaces when asked.
///
/// @param[in,out] renderer Renderer supplying the art and panel surfaces.
/// @param[in,out] panel GUI context; its top panel is drawn.
/// @param flags ui::gui_input::panel_flag and draw_flag bits.
/// @return 1 when drawn; 0 without a panel, when the root is larger than the
///         display, or when the face cannot be created.
/// @quirk The second focus outline tests the active byte of the record after
///        the last one.
int32_t draw_panel(GadgetRenderer& renderer, GadgetPanel& panel, uint32_t flags);

/// Blits panel faces onto the screen from the bottom of the chain up.
///
/// A face is blitted when its panel waits for a redraw or overlaps the
/// region; its redraw flag is cleared.
///
/// @param owner Top of the panel chain; null does nothing.
/// @param[in,out] screen Screen surface.
/// @param region Screen rectangle that needs repainting; null for none.
/// @return 0 for a null owner, otherwise 1.
int32_t blit_panels(GadgetOwner* owner, Surface* screen, const Rect32* region);

/// Runs blit_panels() over the panel stack.
///
/// @param[in,out] panel GUI context.
/// @param[in,out] screen Screen surface.
/// @param region Screen rectangle that needs repainting; null for none.
void blit_panel_stack(GadgetPanel& panel, Surface* screen, const Rect32* region);

/// Handles one engine draw request (GadgetHost::draw).
///
/// @param[in,out] renderer Renderer supplying the art.
/// @param[in,out] panel GUI context.
/// @param what Kind of draw requested.
/// @param index Record to draw.
void draw_request(
    GadgetRenderer& renderer, GadgetPanel& panel, ui::gui_input::GadgetDraw what, int32_t index
);

/// Returns the face surface of the panel on top of the stack.
///
/// @param panel GUI context.
/// @return The face, or null before its first draw.
[[nodiscard]] Surface* panel_face(const GadgetPanel& panel);

} // namespace oa::ui::gadget_render
