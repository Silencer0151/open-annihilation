// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Package-private state: the panel stack, the host table and the helpers the
// three dialogs share.
#pragma once

#include "oa/formats/hpi.hpp"
#include "oa/ui/frontend_dialogs.hpp"
#include "oa/ui/gui_input/gadget_panel.hpp"
#include "oa/ui/gadget_render.hpp"
#include "oa/ui/gui_layout.hpp"
#include "oa/present/display.hpp"
#include "oa/present/surface.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace oa::formats::gaf {
struct Frame;
} // namespace oa::formats::gaf

namespace oa::ui::frontend_dialogs {

namespace renderer = oa::ui::frontend_renderer;
namespace panel_flag = ui::gui_input::panel_flag;

// Open dialogs sit in a fixed stack of this depth.
inline constexpr std::size_t kDialogStackDepth = 4;
inline constexpr int32_t kNoGadget = -1;
inline constexpr int32_t kCanvasWidth = 640;
inline constexpr int32_t kCanvasHeight = 480;
inline constexpr int32_t kHudStripWidth = ui::gui_input::hud_strip_width;
inline constexpr const char* kGuiPalette = "palettes/guipal.pal";
inline constexpr const char* kCommonGaf = "anims/commongui.gaf";
inline constexpr const char* kShadeTable = "palettes/palette.shd";
inline constexpr const char* kLabelName = "TEXT";
inline constexpr const char* kDefaultButton = "OK";
inline constexpr const char* kOptionsSound = "Options";
// Label attribute values: 2 centres the text; 1 sets neither the centre (2)
// nor the right (4) bit, so the text starts at the left edge.
inline constexpr int32_t kLabelCentred = 2;
inline constexpr int32_t kLabelLeftAligned = 1;
// Label width that runs to five pixels short of the root's right edge.
inline constexpr int32_t kLabelWidthToRootEdge = -1;
// Root position resolved when the panel is placed.
inline constexpr int16_t kCentreMarker = ui::gui_input::root_centred;
inline constexpr std::size_t kPaletteColors = 256;

struct Dialog {
    DialogKind kind = DialogKind::none;
    uint32_t flags = 0;
    std::string name; // panel name given to the loader: the layout's file stem
    // The layout root carries the canvas position and size; records 1.. are
    // root-relative, as in the gadget record table.
    renderer::ScreenResources resources;
    present::SurfaceBuffer backdrop; // drawn instead of the skin with modal_backdrop
    bool has_backdrop = false;
    std::vector<uint8_t> stages; // button stage per record, as its button_stage byte holds it
    // Root position before placement, and the frame size it was placed for:
    // a dialog is placed again when it is drawn on a frame of another size.
    int16_t authored_x = 0;
    int16_t authored_y = 0;
    int32_t placed_width = 0;
    int32_t placed_height = 0;
    int32_t focus = kNoGadget;
    bool focus_initialized = false;
    int32_t hovered = kNoGadget;
    int32_t pressed = kNoGadget;
    void* choice_context{};
    void (*choice_callback)(void*, bool){};
    void (*notice_closed)(void*, NoticeChoice){}; // told which button closed a notice
    int32_t help_page = 0;
    int32_t help_loaded_count = 0; // records HELP.GUI loaded, before any page lines
};

// One GAF file of GUI art converted for the gadget draw host.
struct ArtSequence {
    std::string name;
    std::vector<present::SpriteBuffer> frames;
    std::vector<uint8_t> valid; // 0: a frame the normal draw cannot render
};

struct ArtFile {
    std::string path; // case-folded, '/' separated
    std::vector<ArtSequence> sequences;
};

// GUI art shared by every dialog: the files loaded so far, the GUI fonts, the
// common GUI GAF and the palette tables of the draw display.
struct DialogArt {
    AssetStore* assets = nullptr;
    std::vector<std::unique_ptr<ArtFile>> files;
    std::vector<std::string> missing; // paths probed and absent
    std::array<const void*, 3> fonts{};
    const void* common = nullptr;
    // COMIX.FNT, the default FNT font start-up gives the GUI (the first of
    // Game.common_fonts); empty when the file is missing.
    std::vector<uint8_t> default_font;
    std::vector<uint8_t> light_table;
    std::vector<uint8_t> shade_table;
};

struct DialogStack {
    Dialog dialogs[kDialogStackDepth];
    std::size_t count = 0;
    DialogHost host{};
    std::unique_ptr<DialogArt> art;
    ui::gadget_render::GadgetRenderer renderer;
    // 8-bit screen the panels are drawn over, and the display it backs.
    present::SurfaceBuffer screen;
    present::DisplayContext display;
};

/// Returns the one stack of open dialogs, created empty on first use.
///
/// @return the stack
[[nodiscard]] DialogStack& dialog_stack() noexcept;

/// Returns the GUI art shared by every dialog, loading it on first use.
///
/// @param assets the archives the art is read from
/// @return the art, or null when the fonts, the common GUI GAF or the palette
///         tables are missing
[[nodiscard]] DialogArt* dialog_art(AssetStore& assets);

/// Converts a parsed GAF frame into a present sprite.
///
/// Raw frames stay keyed; compressed and layered ones become row runs covering
/// exactly the frame's covered pixels.
///
/// @param frame the parsed frame
/// @param[out] out the sprite
/// @return false for a frame the normal draw cannot render (a blended child)
bool convert_gaf_frame(const formats::gaf::Frame& frame, present::SpriteBuffer& out);

/// Returns the dialog on top of the stack.
///
/// @return the top dialog, or null when none is open
[[nodiscard]] Dialog* dialog_top() noexcept;

/// Loads a dialog from a layout and pushes it on the stack.
///
/// The dialog keeps the layout root's position as authored and the layout's
/// file stem as its name; an optional backdrop bitmap is drawn instead of the
/// skin with panel_flag::modal_backdrop.
///
/// @param ctx screen context supplying the archives
/// @param kind which dialog it is
/// @param layout archive path of the GUI layout
/// @param backdrop archive path of the backdrop bitmap, or null for none
/// @param flags panel_flag bits of the dialog
/// @return the pushed dialog, or null without a context or archives, when the
///         stack is full, or when the GUI art or a resource is missing
Dialog* dialog_push(
    app::ScreenContext* ctx,
    DialogKind kind,
    const char* layout,
    const char* backdrop,
    uint32_t flags
);

/// Releases one dialog wherever it sits in the stack; the dialogs above it move down.
///
/// A dialog that is not in the stack is left alone.
///
/// @param[in,out] dialog the dialog to release
void dialog_close(Dialog& dialog);

/// Releases the dialog on top of the stack, if any.
void dialog_pop();

/// Returns a dialog's layout root, the record that carries its screen position and size.
///
/// @param[in,out] dialog the dialog
/// @return the root record
[[nodiscard]] ui::gui_layout::Gadget& dialog_root(Dialog& dialog) noexcept;

/// Returns the palette panels are indexed against.
///
/// @param dialog the dialog drawn
/// @return the host's palette of the screen below, else the dialog's GUI palette
[[nodiscard]] PaletteBytes dialog_palette(const Dialog& dialog);

/// Finds a record of a dialog by name, the root left out.
///
/// @param dialog the dialog searched
/// @param name the record name, matched exactly
/// @return the record index, or kNoGadget when no record has that name
[[nodiscard]] int32_t dialog_find(const Dialog& dialog, std::string_view name) noexcept;

/// Appends a TEXT label record with the field defaults ui::gui_layout::add_label gives a record table.
///
/// @param[in,out] dialog the dialog the label is added to
/// @param text the label text, cut to the label's text size
/// @param x left edge relative to the root, in pixels
/// @param y top edge relative to the root, in pixels
/// @param width width in pixels, or kLabelWidthToRootEdge to run to five pixels
///        short of the root's right edge
/// @param attributes label attribute bits (kLabelCentred, kLabelLeftAligned)
/// @return the new record's index, or kNoGadget when the record table is full
int32_t dialog_add_label(
    Dialog& dialog, std::string_view text, int16_t x, int16_t y, int32_t width, int32_t attributes
);

/// Places a dialog's root as the whole-panel draw does on its first draw.
///
/// The root starts from its authored position and is centred, set right of the
/// host's HUD strip (which may be scaled), or re-centred on an axis it runs
/// past; the frame size is kept so a frame of another size places it again.
///
/// @param[in,out] dialog the dialog placed
/// @param screen_width width of the drawn frame in pixels
/// @param screen_height height of the drawn frame in pixels
void dialog_place(Dialog& dialog, int32_t screen_width, int32_t screen_height);

/// Returns the width dialogs are placed against.
///
/// @param ctx screen context, or null
/// @return the screen surface's width, or 640 without one
[[nodiscard]] int32_t dialog_screen_width(const app::ScreenContext* ctx) noexcept;

/// Returns the height dialogs are placed against.
///
/// @param ctx screen context, or null
/// @return the screen surface's height, or 480 without one
[[nodiscard]] int32_t dialog_screen_height(const app::ScreenContext* ctx) noexcept;

/// Translates a text through the host's translation table.
///
/// @param text the text in the game's wording
/// @return the host's replacement, or the text unchanged without one
[[nodiscard]] std::string dialog_translate(std::string_view text);

/// Word-wraps a text in the dialog's font, as a message box wraps it.
///
/// @param dialog the dialog whose font measures the text
/// @param text the text
/// @param width line width in pixels
/// @return the text with CR LF line breaks
[[nodiscard]] std::string dialog_wrap(const Dialog& dialog, std::string_view text, int32_t width);

/// Measures a text in the dialog's font.
///
/// @param dialog the dialog whose font measures the text
/// @param text the text
/// @return its width in pixels
[[nodiscard]] int32_t dialog_text_width(const Dialog& dialog, std::string_view text);

/// Returns the line height of the dialog's font.
///
/// @param dialog the dialog
/// @return the height in pixels
[[nodiscard]] int32_t dialog_line_height(const Dialog& dialog);

/// Plays a named interface sound through the host, when the host plays sounds.
///
/// @param name the sound's name in the game's sound list
void dialog_play_sound(const char* name);

/// Draws one dialog over a frame.
///
/// With a `layer_key` the frame is a layer whose uncovered pixels hold that
/// colour: shading then keeps to covered pixels and leaves the host panel
/// below the bottom dialog alone.
///
/// @param[in,out] dialog the dialog drawn
/// @param below the dialog beneath it, or null for the bottom one
/// @param[in,out] frame the frame drawn into
/// @param[out] error the reason the dialog cannot be drawn, when it cannot
/// @param layer_key the layer's uncovered colour, or null for an opaque frame
/// @return false when the dialog cannot be drawn
bool dialog_compose(
    Dialog& dialog,
    const Dialog* below,
    renderer::Surface& frame,
    std::string* error,
    const uint8_t* layer_key
);

/// Darkens a rectangle as the panel below a shade_below panel is darkened.
///
/// Uses renderer::shade_panel_below through the GUI art's shade table, skipping
/// pixels of the layer key colour when one is given.
///
/// @param[in,out] frame the frame darkened
/// @param x left edge in pixels
/// @param y top edge in pixels
/// @param width width in pixels
/// @param height height in pixels
/// @param palette the palette the frame is indexed against
/// @param layer_key the layer's uncovered colour, or null to darken every pixel
void dialog_shade(
    renderer::Surface& frame,
    int32_t x,
    int32_t y,
    int32_t width,
    int32_t height,
    const PaletteBytes& palette,
    const uint8_t* layer_key
);

/// Releases the watch prompt and reports the choice.
///
/// Plays BigButton on every activation; only CHOICE1 and CHOICE2 close it.
///
/// @param[in,out] dialog The watch prompt; released before the callback runs.
/// @param control Name of the activated button.
void continue_watching_click(Dialog& dialog, std::string_view control);

/// Releases the message box on any activation, as in 3.1c.
///
/// @param ctx Screen context (unused).
/// @param[in,out] dialog The message box.
/// @param control Name of the activated button (unused).
void message_box_click(app::ScreenContext* ctx, Dialog& dialog, std::string_view control);

/// Runs the host's disc-check handler and releases the prompt when it allows.
///
/// @param ctx Screen context (unused).
/// @param[in,out] dialog The disc prompt.
/// @param control Name of the activated button.
void cd_check_dialog_click(app::ScreenContext* ctx, Dialog& dialog, std::string_view control);

/// Releases a DEMOMSG.GUI notice on OK or GotoWebsite and reports which one closed it.
///
/// Plays BigButton first; other controls do nothing.
///
/// @param[in,out] dialog The notice; released before its callback runs.
/// @param control Name of the activated button.
void notice_click(Dialog& dialog, std::string_view control);

/// Handles HELP.GUI buttons.
///
/// OK releases the panel and Page refills the lines for the button's new
/// stage, both with the Options sound.
///
/// @param ctx Screen context supplying the assets for the refill.
/// @param[in,out] dialog The help panel.
/// @param control Name of the activated button.
void help_click(app::ScreenContext* ctx, Dialog& dialog, std::string_view control);

/// Handles one input event for the top dialog, as the overlay hook registered with the application.
///
/// Pointer moves track the hovered button; a left press and release on the same
/// button activates it; Tab and Space move or use the focus; Enter and Escape
/// activate the panel's default buttons.
///
/// @param ctx screen context carrying the input event
/// @return 1 when a dialog took the event, 0 when none is open or there is no event
int dialog_event(app::ScreenContext* ctx, void* state);

/// Draws the open dialogs over the screen, as the overlay draw hook registered with the application.
///
/// @param ctx screen context carrying the screen surface
void dialog_draw_hook(app::ScreenContext* ctx, void* state);

} // namespace oa::ui::frontend_dialogs
