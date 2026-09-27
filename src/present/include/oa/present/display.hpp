// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// Process-wide display context: the back buffer, palette, lookup tables and
// cursor overlay. Frames reach the platform through the RenderSink boundary.

#include "oa/present/surface.h"

#include <cstdint>
#include <vector>

namespace oa::present {

inline constexpr int surface_lock_stack_depth = 10;
inline constexpr int32_t default_view_width = 640;
inline constexpr int32_t default_view_height = 480;
// set_text_colors argument that leaves a colour unchanged.
inline constexpr int32_t text_color_keep = -1;

// DisplayContext::flags bits.
enum DisplayFlag : uint16_t {
    display_flag_session = 0x0001,        // toggled by set_session_flag
    display_flag_device_surface = 0x0002, // presenting through a device surface
    display_flag_device_palette = 0x0004, // palette changes go to the device palette
    display_flag_alpha_table = 0x0020,    // alpha_table owned (freed at shutdown)
    display_flag_shade_table = 0x0040,
    display_flag_light_table = 0x0080,
    display_flag_gray_table = 0x0100, // also gates the gray rectangle pass
    display_flag_blue_table = 0x0200,
    display_flag_mouse_capture = 0x0400, // handed to the mouse-capture setup at display start
};

// DisplayContext::startup_request bits. start_display moves bits 1..8 up one
// place into DisplayFlag bits 2..9 and bit 9 into display_flag_mouse_capture.
inline constexpr uint16_t display_request_device_surface =
    0x0001; // init_display's exclusive-mode path
inline constexpr uint16_t display_request_flag_bits = 0x01FE;
// Requests display_flag_mouse_capture, which start_display sets from it.
inline constexpr uint16_t display_request_mouse_capture = 0x0200;
inline constexpr uint16_t display_request_cleared = 0x03FF; // low bits reset_view_defaults clears
// The startup request of a game session: the device palette, every lookup
// table and display_flag_mouse_capture.
inline constexpr uint16_t display_request_session = 0x03F2;

struct LockEntry {
    Surface* surface = nullptr;
    uint8_t presented = 0; // non-zero: unwind presents instead of unlocking
};

// Screen-space software cursor drawn over each presented frame.
struct CursorOverlay {
    int32_t x = 0;
    int32_t y = 0;
    const Sprite* sprite = nullptr;
    int32_t save_x = 0;
    int32_t save_y = 0;
    Surface* backing = nullptr; // pixels under the cursor
    int32_t enabled = 0;
    int32_t visible = 0;
};

// One 12-byte display mode record (appended by the mode enumeration; pitch 0 when unknown).
struct DisplayMode {
    int32_t width = 0;
    int32_t height = 0;
    int32_t pitch = 0;
};

inline constexpr int32_t display_mode_capacity = 16;

struct DisplayModeList {
    int32_t count = 0;
    DisplayMode modes[display_mode_capacity]{};
};

// The process-wide display record: the back buffer and draw target, the
// lookup tables, the cursor overlay, the text state and the palettes.
struct DisplayContext {
    RenderSink sink{};                // platform target every frame is shown on
    std::vector<uint8_t> back_pixels; // storage behind back_buffer
    Palette device_palette{};         // gamma-corrected palette handed to the sink
    Surface back_buffer{};
    Surface* offscreen = nullptr; // copied to a re-created device surface
    int32_t device_ready = 0;     // primary device surface exists
    Rect32 device_clip{};
    Surface* active_surface = nullptr;
    uint8_t* alpha_table = nullptr; // 256x256
    uint8_t* shade_table = nullptr;
    uint8_t* light_table = nullptr;
    uint8_t* gray_table = nullptr; // 256
    uint8_t* blue_table = nullptr;
    int32_t width = default_view_width;
    int32_t height = default_view_height;
    int32_t use_active_surface = 0;
    // No known use: zero-initialised and never written; display_reserved_word
    // returns it.
    uint32_t reserved_word = 0;
    uint16_t flags = 0; // DisplayFlag
    CursorOverlay cursor{};
    Rect32 view_clip{}; // inclusive
    int32_t view_width = default_view_width;
    int32_t view_height = default_view_height;
    uint16_t startup_request{}; // display_request_* bits read by start_display
    const void* font = nullptr;
    // Text colours: draw_text hands their low bytes to draw_font_text as the
    // colour of set glyph bits, of clear bits, and the value never written.
    int32_t text_color = 0;
    int32_t text_background = 0;
    uint32_t text_transparent = 0; // 0xFE after the game's start-up
    Palette palette{};             // as requested by the game
    float gamma = 1.0F;
    LockEntry locks[surface_lock_stack_depth]{};
    int32_t lock_depth = 0;
};

/// Installs the process-wide display context.
///
/// @param context context every display call uses from now on; not owned;
///     null detaches the display
void bind_display(DisplayContext* context) noexcept;

/// Returns the process-wide display context.
///
/// @return the bound context, or null
[[nodiscard]] DisplayContext* display_context() noexcept;

/// Returns the display width.
///
/// @return width in pixels, or 0 when no display is bound
[[nodiscard]] int32_t display_width() noexcept;

/// Returns the display height.
///
/// @return height in rows, or 0 when no display is bound
[[nodiscard]] int32_t display_height() noexcept;

/// Copies the current draw target's descriptor for drawing.
///
/// The target is the active surface while the display presents from it,
/// else the back buffer. Every target is memory-backed, so nothing is locked.
///
/// @param[out] out receives the target's descriptor
/// @return 1, or 0 when no display is bound
[[nodiscard]] int32_t lock_display_surface(Surface& out) noexcept;

/// Ends a lock_display_surface section.
///
/// @return 1, or 0 when no display is bound
int32_t unlock_display_surface() noexcept;

/// Releases every outstanding lock-stack entry, presenting the entries marked so.
///
/// Entries are released from the top; lock_depth ends at 0.
void unwind_surface_locks() noexcept;

/// Copies the back buffer descriptor.
///
/// @param[out] out receives the back buffer's descriptor
/// @return 1, or 0 when no display is bound
[[nodiscard]] int32_t copy_back_buffer_descriptor(Surface& out) noexcept;

/// Shows the back buffer through the sink.
///
/// The sink always receives the whole frame with the device palette.
///
/// @param first first dirty rectangle; unused, the whole frame is shown
/// @param second second dirty rectangle; unused, the whole frame is shown
/// @return 1, or 0 when no display is bound
int32_t present_dirty_rects(const Rect32* first, const Rect32* second) noexcept;

/// Sets or clears the session flag of the display flags.
///
/// @param enabled bit 0 becomes display_flag_session
/// @return 1, or 0 when setting it without a primary device surface or when
///     no display is bound
int32_t set_session_flag(int32_t enabled) noexcept;

/// Stores the off-screen buffer that init_display copies into a re-created device surface.
///
/// @param surface off-screen surface; null forgets it
void set_offscreen_buffer(Surface* surface) noexcept;

/// Restores lost device surfaces.
///
/// Memory surfaces are never lost, so there is nothing to restore.
///
/// @return 0
int32_t restore_surfaces() noexcept;

/// Leaves the device frame and stops presenting from the active surface.
void end_active_surface() noexcept;

/// Composes and presents one frame.
///
/// The active surface (while presenting from it) is copied onto the back
/// buffer, the cursor overlay drawn over it, and the back buffer shown
/// through the sink. Nothing happens when no display is bound.
void draw_frame() noexcept;

/// Saves the pixels under the cursor into its backing surface, then draws the cursor sprite.
///
/// Nothing happens unless the overlay is enabled and visible and has a
/// sprite and a backing surface; the backing surface takes the sprite's size.
///
/// @param[in,out] display display whose cursor overlay is drawn; its save
///     position is updated
/// @param[in,out] target surface the cursor is drawn on
void draw_cursor_overlay(DisplayContext& display, Surface& target) noexcept;

/// Shows or hides the cursor overlay on presented frames.
///
/// @param visible non-zero to show it
void set_cursor_overlay_visible(int32_t visible) noexcept;

/// Fills every pitch byte of a surface's rows with a colour.
///
/// @param[in,out] surface surface to fill; null fills the current draw target
/// @param color fill colour
/// @return 1, or 0 for a null surface when no display is bound
int32_t clear_surface(Surface* surface, uint8_t color) noexcept;

/// Makes draw locks and frames use a surface instead of the back buffer.
///
/// @param surface surface to present from; null falls back to the back buffer
void set_active_surface(Surface* surface) noexcept;

/// Returns the display's reserved word, which has no known use.
///
/// @return DisplayContext::reserved_word, or 0 when no display is bound
[[nodiscard]] uint32_t display_reserved_word() noexcept;

/// Sets the text and background colours text draws use.
///
/// @param color colour of set glyph bits; text_color_keep leaves it unchanged
/// @param background colour of clear glyph bits; text_color_keep leaves it unchanged
void set_text_colors(int32_t color, int32_t background) noexcept;

/// Sets the colour value text draws skip.
///
/// @param value colour never written by text draws (its low byte is used)
void set_text_transparent(uint32_t value) noexcept;

/// Returns the colour value text draws skip.
///
/// @return the value, or 0 when no display is bound
[[nodiscard]] uint32_t text_transparent() noexcept;

/// Installs the active font.
///
/// @param font font record; null keeps the current one
void set_active_font(const void* font) noexcept;

/// Returns the active font.
///
/// @return the font record, or null when none is set or no display is bound
[[nodiscard]] const void* active_font() noexcept;

/// Returns the line height of the active font.
///
/// @return the font's first byte, or 0 without an active font
[[nodiscard]] uint8_t active_font_height() noexcept;

/// Returns the line height of a font record.
///
/// @param font font record
/// @return its first byte
[[nodiscard]] uint8_t font_height(const uint8_t* font) noexcept;

/// Sums the advance widths of a string's characters up to NUL or newline.
///
/// Byte 3 of the font is its first character code, followed by one u16
/// glyph offset per character (0 for no glyph); a glyph's first byte is its
/// advance. Characters below the first code or without a glyph add nothing.
///
/// @param font font record; null measures 0
/// @param text text to measure; null measures 0
/// @return width in pixels
[[nodiscard]] int32_t measure_text_width(const uint8_t* font, const char* text) noexcept;

/// Resets the view rectangle to 640x480 and the gamma to 1.
///
/// The low startup-request bits (display_request_cleared) are cleared too.
///
/// @param[in,out] display display to reset
void reset_view_defaults(DisplayContext& display) noexcept;

/// Appends a display mode with an unknown pitch.
///
/// @param[in,out] list mode list; a full list is left unchanged
/// @param width mode width in pixels
/// @param height mode height in rows
void add_display_mode(DisplayModeList& list, int32_t width, int32_t height) noexcept;

/// Appends an enumerated display mode when it is 8-bit.
///
/// @param[in,out] list mode list; a full list is left unchanged
/// @param mode enumerated mode
/// @param bits mode depth in bits per pixel; other depths than 8 are ignored
/// @return 1, so enumeration continues
int32_t accept_display_mode(DisplayModeList& list, const DisplayMode& mode, int32_t bits) noexcept;

/// Lists the selectable windowed modes for a desktop of a given size.
///
/// 640x480, 800x600 and 1024x768 are always listed, 1280x1024 and 1600x1200
/// when the desktop holds them.
///
/// @param[out] list mode list; emptied first
/// @param desktop_width desktop width in pixels
/// @param desktop_height desktop height in rows
/// @return 1
int32_t
scan_display_modes(DisplayModeList& list, int32_t desktop_width, int32_t desktop_height) noexcept;

/// (Re)creates the back buffer at the context size and applies the palette.
///
/// The sink is asked for an 8-bit mode of the context size first. The back
/// buffer gets a pitch rounded up to four bytes. On the device path the
/// device-surface flag is set and the off-screen buffer, when one is stored,
/// is copied in; otherwise the flag is cleared. Both paths use memory pixels.
///
/// @param[in,out] display display to initialise
/// @param device_surface true for the exclusive-mode (device surface) path
/// @return 1, or 0 when the device path's mode change fails
int32_t init_display(DisplayContext& display, bool device_surface);

/// Starts the display.
///
/// The display takes the view size and its flags from the startup request
/// (plus the session flag), loads the lookup tables those flags select and
/// then, only when display_flag_device_palette is requested, creates the
/// back buffer on the path display_request_device_surface selects. A table whose flag is clear keeps no pointer for shade and
/// light, as in the game; alpha, gray and blue are left as they were.
/// Memory, screen-saver and mouse-capture setup and window creation are host
/// concerns; failures are returned to the host, which owns the error dialog
/// and cleanup.
///
/// @param[in,out] display display to start
/// @return 1 without a back buffer to create, else the init_display result
int32_t start_display(DisplayContext& display);

/// Reinitializes the display on the opposite windowed/device path.
///
/// @param[in,out] display display to reinitialise
/// @return the init_display result
/// @quirk The device-surface flag is inverted, so a device-surface display
///     comes back windowed and a windowed one takes the device path, as in 3.1c.
int32_t reinit_display(DisplayContext& display);

/// Changes the display size and reinitializes it on its current path.
///
/// @param[in,out] display display to resize
/// @param width new width in pixels
/// @param height new height in rows
/// @return the init_display result
int32_t set_display_size(DisplayContext& display, int32_t width, int32_t height);

/// Frees the lookup tables the context owns and drops the back buffer.
///
/// Window, cursor-capture and system-parameter restoration are host concerns.
///
/// @param[in,out] display display to shut down
void shutdown_display(DisplayContext& display) noexcept;

// Off-screen composition surface of the game (Game.offscreen_surface,
// offscreen_width and offscreen_height).
struct OffscreenSurface {
    std::vector<uint8_t> pixels;
    Surface surface{};
    bool allocated = false;
    int32_t width = 0;
    int32_t height = 0;
};

/// Allocates the off-screen surface at its recorded size and registers it as the display's off-screen buffer.
///
/// @param[in,out] offscreen off-screen surface; its pixels are zeroed and its pitch is its width
void create_offscreen_surface(OffscreenSurface& offscreen);

/// Frees the off-screen surface, unregisters it and stops presenting from the active surface.
///
/// @param[in,out] offscreen off-screen surface to free
void destroy_offscreen_surface(OffscreenSurface& offscreen) noexcept;

/// Switches to a 640x480 display presenting from a fresh off-screen surface.
///
/// The off-screen size becomes 640x480 either way; nothing else happens when
/// the display already has that size.
///
/// @param[in,out] display display to resize
/// @param[in,out] offscreen off-screen surface to recreate and present from
void use_standard_offscreen(DisplayContext& display, OffscreenSurface& offscreen);

} // namespace oa::present
