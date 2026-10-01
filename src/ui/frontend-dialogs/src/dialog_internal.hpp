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

[[nodiscard]] DialogStack& dialog_stack() noexcept;
// The shared GUI art for `assets`, loaded on first use; null when the fonts,
// the common GUI GAF or the palette tables are missing.
[[nodiscard]] DialogArt* dialog_art(AssetStore& assets);
// A parsed GAF frame as a present sprite: raw frames stay keyed, compressed
// and layered ones become row runs covering exactly the frame's covered
// pixels. False for a frame the normal draw cannot render (a blended child).
bool convert_gaf_frame(const formats::gaf::Frame& frame, present::SpriteBuffer& out);
[[nodiscard]] Dialog* dialog_top() noexcept;
// Pushes a dialog loaded from `layout` with an optional `backdrop` bitmap;
// null when the stack is full or a resource is missing.
Dialog* dialog_push(
    app::ScreenContext* ctx,
    DialogKind kind,
    const char* layout,
    const char* backdrop,
    uint32_t flags
);
// Releases one dialog wherever it sits in the stack.
void dialog_close(Dialog& dialog);
void dialog_pop();

[[nodiscard]] ui::gui_layout::Gadget& dialog_root(Dialog& dialog) noexcept;
// The palette panels are indexed against: the screen below's, else the GUI
// palette.
[[nodiscard]] PaletteBytes dialog_palette(const Dialog& dialog);
[[nodiscard]] int32_t dialog_find(const Dialog& dialog, std::string_view name) noexcept;
// Appends a TEXT label record with the field defaults ui::gui_layout::add_label
// gives a record table.
int32_t dialog_add_label(
    Dialog& dialog, std::string_view text, int16_t x, int16_t y, int32_t width, int32_t attributes
);
void dialog_place(Dialog& dialog, int32_t screen_width, int32_t screen_height);
[[nodiscard]] int32_t dialog_screen_width(const app::ScreenContext* ctx) noexcept;
[[nodiscard]] int32_t dialog_screen_height(const app::ScreenContext* ctx) noexcept;
[[nodiscard]] std::string dialog_translate(std::string_view text);
[[nodiscard]] std::string dialog_wrap(const Dialog& dialog, std::string_view text, int32_t width);
[[nodiscard]] int32_t dialog_text_width(const Dialog& dialog, std::string_view text);
[[nodiscard]] int32_t dialog_line_height(const Dialog& dialog);
void dialog_play_sound(const char* name);
// Draws one dialog over `frame`; false (with the reason) when it cannot.
// With a `layer_key` the frame is a layer whose uncovered pixels hold that
// colour: shading then keeps to covered pixels and leaves the host panel
// below the bottom dialog alone.
bool dialog_compose(
    Dialog& dialog,
    const Dialog* below,
    renderer::Surface& frame,
    std::string* error,
    const uint8_t* layer_key
);
// Darkens a rectangle as the panel below a shade_below panel is darkened
// (renderer::shade_panel_below, through the GUI art's shade table), skipping
// pixels of the layer key colour when one is given.
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

// Overlay hooks registered with the application.
int dialog_event(app::ScreenContext* ctx, void* state);
void dialog_draw_hook(app::ScreenContext* ctx, void* state);

} // namespace oa::ui::frontend_dialogs
