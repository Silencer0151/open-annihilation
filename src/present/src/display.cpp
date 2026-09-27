// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/present/display.hpp"

#include "oa/present/blit.hpp"
#include "oa/present/palette_tables.hpp"
#include "oa/present/surface.hpp"

#include <algorithm>
#include <cstdint>

namespace oa::present {

namespace {

DisplayContext* g_display = nullptr;

constexpr int32_t display_bits = 8;
constexpr int32_t pitch_alignment_mask = ~3;

struct StandardMode {
    int32_t width;
    int32_t height;
};

constexpr StandardMode windowed_modes[] = {{640, 480}, {800, 600}, {1024, 768}};
constexpr StandardMode large_mode = {1280, 1024};
constexpr StandardMode largest_mode = {1600, 1200};

void present_back_buffer(const DisplayContext& display) noexcept {
    if (display.sink.present != nullptr && display.back_buffer.pixels != nullptr) {
        display.sink.present(
            display.sink.user,
            display.back_buffer.pixels,
            display.back_buffer.pitch,
            display.back_buffer.width,
            display.back_buffer.height,
            &display.device_palette
        );
    }
}

void fill_bytes(uint8_t* pixels, int32_t pitch, int32_t height, uint8_t color) noexcept {
    if (pixels == nullptr || pitch <= 0 || height <= 0) {
        return;
    }
    std::fill_n(pixels, static_cast<std::size_t>(pitch) * static_cast<std::size_t>(height), color);
}

} // namespace

void bind_display(DisplayContext* context) noexcept {
    g_display = context;
}

DisplayContext* display_context() noexcept {
    return g_display;
}

int32_t display_width() noexcept {
    return g_display != nullptr ? g_display->width : 0;
}

int32_t display_height() noexcept {
    return g_display != nullptr ? g_display->height : 0;
}

int32_t lock_display_surface(Surface& out) noexcept {
    const DisplayContext* display = display_context();
    if (display == nullptr) {
        return 0;
    }
    if (display->use_active_surface != 0 && display->active_surface != nullptr) {
        out = *display->active_surface;
    } else {
        out = display->back_buffer;
    }
    return 1;
}

int32_t unlock_display_surface() noexcept {
    return display_context() != nullptr ? 1 : 0;
}

void unwind_surface_locks() noexcept {
    DisplayContext* display = display_context();
    if (display == nullptr) {
        return;
    }
    int32_t depth = display->lock_depth;
    while (depth > 0) {
        display->lock_depth = depth;
        const LockEntry& entry = display->locks[std::min(depth, surface_lock_stack_depth - 1)];
        if (entry.presented == 0) {
            unlock_display_surface();
        } else {
            present_dirty_rects(nullptr, nullptr);
        }
        depth = display->lock_depth == depth ? display->lock_depth - 1 : display->lock_depth;
    }
    display->lock_depth = depth;
}

int32_t copy_back_buffer_descriptor(Surface& out) noexcept {
    const DisplayContext* display = display_context();
    if (display == nullptr) {
        return 0;
    }
    out = display->back_buffer;
    return 1;
}

int32_t present_dirty_rects(const Rect32*, const Rect32*) noexcept {
    const DisplayContext* display = display_context();
    if (display == nullptr) {
        return 0;
    }
    present_back_buffer(*display);
    return 1;
}

int32_t set_session_flag(int32_t enabled) noexcept {
    DisplayContext* display = display_context();
    if (display == nullptr) {
        return 0;
    }
    display->flags = static_cast<uint16_t>(
        (display->flags & ~display_flag_session) |
        (static_cast<uint16_t>(enabled) & display_flag_session)
    );
    return enabled == 0 || display->device_ready != 0 ? 1 : 0;
}

void set_offscreen_buffer(Surface* surface) noexcept {
    if (DisplayContext* display = display_context()) {
        display->offscreen = surface;
    }
}

int32_t restore_surfaces() noexcept {
    return 0;
}

void end_active_surface() noexcept {
    if (DisplayContext* display = display_context()) {
        display->use_active_surface = 0;
    }
}

void draw_frame() noexcept {
    DisplayContext* display = display_context();
    if (display == nullptr) {
        return;
    }
    if (display->use_active_surface != 0 && display->active_surface != nullptr) {
        blit_surface(&display->back_buffer, display->active_surface, 0, 0);
    }
    draw_cursor_overlay(*display, display->back_buffer);
    present_back_buffer(*display);
}

void set_cursor_overlay_visible(int32_t visible) noexcept {
    if (DisplayContext* display = display_context(); display != nullptr)
        display->cursor.visible = visible;
}

void draw_cursor_overlay(DisplayContext& display, Surface& target) noexcept {
    CursorOverlay& cursor = display.cursor;
    if (cursor.enabled == 0 || cursor.visible == 0 || cursor.sprite == nullptr ||
        cursor.backing == nullptr) {
        return;
    }
    cursor.backing->width = cursor.sprite->width;
    cursor.backing->height = cursor.sprite->height;
    cursor.backing->pitch = cursor.sprite->width;
    cursor.save_x = cursor.x - cursor.sprite->origin_x;
    cursor.save_y = cursor.y - cursor.sprite->origin_y;
    blit_surface(cursor.backing, &target, -cursor.save_x, -cursor.save_y);
    draw_sprite(&target, cursor.sprite, cursor.x, cursor.y);
}

int32_t clear_surface(Surface* surface, uint8_t color) noexcept {
    const DisplayContext* display = display_context();
    if (surface != nullptr) {
        fill_bytes(surface->pixels, surface->pitch, surface->height, color);
        return 1;
    }
    if (display == nullptr) {
        return 0;
    }
    const Surface& target = display->use_active_surface != 0 && display->active_surface != nullptr
                                ? *display->active_surface
                                : display->back_buffer;
    fill_bytes(target.pixels, target.pitch, target.height, color);
    return 1;
}

void set_active_surface(Surface* surface) noexcept {
    if (DisplayContext* display = display_context()) {
        display->use_active_surface = 1;
        display->active_surface = surface;
    }
}

uint32_t display_reserved_word() noexcept {
    const DisplayContext* display = display_context();
    return display != nullptr ? display->reserved_word : 0;
}

void set_text_colors(int32_t color, int32_t background) noexcept {
    DisplayContext* display = display_context();
    if (display == nullptr) {
        return;
    }
    if (color != text_color_keep) {
        display->text_color = color;
    }
    if (background != text_color_keep) {
        display->text_background = background;
    }
}

void set_text_transparent(uint32_t value) noexcept {
    if (DisplayContext* display = display_context()) {
        display->text_transparent = value;
    }
}

uint32_t text_transparent() noexcept {
    const DisplayContext* display = display_context();
    return display != nullptr ? display->text_transparent : 0;
}

void set_active_font(const void* font) noexcept {
    if (font == nullptr) {
        return;
    }
    if (DisplayContext* display = display_context()) {
        display->font = font;
    }
}

const void* active_font() noexcept {
    const DisplayContext* display = display_context();
    return display != nullptr ? display->font : nullptr;
}

uint8_t active_font_height() noexcept {
    const void* font = active_font();
    return font != nullptr ? *static_cast<const uint8_t*>(font) : 0;
}

uint8_t font_height(const uint8_t* font) noexcept {
    return font[0];
}

int32_t measure_text_width(const uint8_t* font, const char* text) noexcept {
    constexpr std::size_t first_char_offset = 3;
    constexpr std::size_t glyph_table_offset = 4;
    int32_t width = 0;
    if (text == nullptr || font == nullptr) {
        return 0;
    }
    const uint8_t first = font[first_char_offset];
    for (const auto* p = reinterpret_cast<const uint8_t*>(text); *p != 0 && *p != '\n'; ++p) {
        if (*p < first) {
            continue;
        }
        const std::size_t slot =
            glyph_table_offset + static_cast<std::size_t>((*p - first) & 0xFFFF) * 2;
        const uint16_t glyph = static_cast<uint16_t>(font[slot] | (font[slot + 1] << 8));
        if (glyph != 0) {
            width += font[glyph];
        }
    }
    return width;
}

void reset_view_defaults(DisplayContext& display) noexcept {
    display.view_clip.x1 = 0;
    display.view_clip.y1 = 0;
    display.startup_request =
        static_cast<uint16_t>(display.startup_request & ~display_request_cleared);
    display.view_width = default_view_width;
    display.gamma = 1.0F;
    display.view_height = default_view_height;
    display.view_clip.x2 = default_view_width - 1;
    display.view_clip.y2 = default_view_height - 1;
}

void add_display_mode(DisplayModeList& list, int32_t width, int32_t height) noexcept {
    if (list.count < 0 || list.count >= display_mode_capacity) {
        return;
    }
    list.modes[list.count] = DisplayMode{width, height, 0};
    ++list.count;
}

int32_t accept_display_mode(DisplayModeList& list, const DisplayMode& mode, int32_t bits) noexcept {
    if (bits == display_bits && list.count >= 0 && list.count < display_mode_capacity) {
        list.modes[list.count] = mode;
        ++list.count;
    }
    return 1;
}

int32_t
scan_display_modes(DisplayModeList& list, int32_t desktop_width, int32_t desktop_height) noexcept {
    list.count = 0;
    for (const StandardMode& mode : windowed_modes) {
        add_display_mode(list, mode.width, mode.height);
    }
    if (desktop_width >= large_mode.width && desktop_height >= large_mode.height) {
        add_display_mode(list, large_mode.width, large_mode.height);
    }
    if (desktop_width >= largest_mode.width && desktop_height >= largest_mode.height) {
        add_display_mode(list, largest_mode.width, largest_mode.height);
    }
    return 1;
}

int32_t init_display(DisplayContext& display, bool device_surface) {
    display.device_ready = 0;
    if (display.sink.set_display_mode != nullptr) {
        const int32_t ok = display.sink.set_display_mode(
            display.sink.user, display.width, display.height, display_bits
        );
        if (device_surface && ok == 0) {
            return 0;
        }
    }
    const int32_t pitch = (display.width + 3) & pitch_alignment_mask;
    display.back_pixels.assign(
        static_cast<std::size_t>(std::max(pitch, 0)) *
            static_cast<std::size_t>(std::max(display.height, 0)),
        0
    );
    init_surface(
        display.back_buffer, display.width, display.height, pitch, display.back_pixels.data()
    );
    if (device_surface) {
        display.flags = static_cast<uint16_t>(display.flags | display_flag_device_surface);
        display.device_ready = 1;
        if (display.offscreen != nullptr) {
            copy_surface_clipped(display.back_buffer, *display.offscreen, 0, 0);
        }
    } else {
        display.flags = static_cast<uint16_t>(display.flags & ~display_flag_device_surface);
    }
    apply_palette_entries(
        display, display.palette.entries, 0, OA_PALETTE_COLORS, display.device_palette
    );
    return 1;
}

int32_t start_display(DisplayContext& display) {
    display.offscreen = nullptr;
    display.use_active_surface = 0;
    constexpr uint16_t request_to_flags_shift = 1;
    constexpr uint16_t kept_before_capture =
        0xF3FFu;                                     // clears display_flag_mouse_capture and bit 11
    constexpr uint16_t kept_before_tables = 0xFC03u; // keeps bits 0, 1 and 10..15
    display.flags = static_cast<uint16_t>(
        ((display.startup_request & display_request_mouse_capture) << request_to_flags_shift) |
        (display.flags & kept_before_capture)
    );
    display.width = display.view_width;
    display.height = display.view_height;
    display.flags = static_cast<uint16_t>(
        ((display.startup_request & display_request_flag_bits) << request_to_flags_shift) |
        (display.flags & kept_before_tables) | display_flag_session
    );
    if ((display.flags & display_flag_shade_table) != 0)
        load_shade_table(display);
    else
        display.shade_table = nullptr;
    if ((display.flags & display_flag_alpha_table) != 0)
        load_alpha_table(display);
    if ((display.flags & display_flag_light_table) != 0)
        load_light_table(display);
    else
        display.light_table = nullptr;
    if ((display.flags & display_flag_gray_table) != 0)
        load_gray_table(display);
    if ((display.flags & display_flag_blue_table) != 0)
        load_blue_table(display);
    if ((display.flags & display_flag_device_palette) == 0)
        return 1;
    display.device_clip = display.view_clip;
    return init_display(display, (display.startup_request & display_request_device_surface) != 0);
}

int32_t reinit_display(DisplayContext& display) {
    return init_display(display, (display.flags & display_flag_device_surface) == 0);
}

int32_t set_display_size(DisplayContext& display, int32_t width, int32_t height) {
    display.width = width;
    display.height = height;
    return init_display(display, (display.flags & display_flag_device_surface) != 0);
}

void shutdown_display(DisplayContext& display) noexcept {
    if ((display.flags & display_flag_shade_table) != 0) {
        free_shade_table(display);
    }
    if ((display.flags & display_flag_alpha_table) != 0) {
        free_alpha_table(display);
    }
    if ((display.flags & display_flag_light_table) != 0) {
        free_light_table(display);
    }
    if ((display.flags & display_flag_gray_table) != 0) {
        free_gray_table(display);
    }
    if ((display.flags & display_flag_blue_table) != 0) {
        free_blue_table(display);
    }
    display.back_buffer = Surface{};
    display.back_pixels.clear();
    display.device_ready = 0;
}

void create_offscreen_surface(OffscreenSurface& offscreen) {
    const auto w = std::max(offscreen.width, 0);
    const auto h = std::max(offscreen.height, 0);
    offscreen.pixels.assign(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), 0);
    offscreen.surface.flags = 0;
    init_surface(
        offscreen.surface,
        offscreen.width,
        offscreen.height,
        offscreen.width,
        offscreen.pixels.data()
    );
    offscreen.allocated = true;
    set_offscreen_buffer(&offscreen.surface);
}

void destroy_offscreen_surface(OffscreenSurface& offscreen) noexcept {
    offscreen.pixels.clear();
    offscreen.pixels.shrink_to_fit();
    offscreen.surface = Surface{};
    offscreen.allocated = false;
    set_offscreen_buffer(nullptr);
    end_active_surface();
}

void use_standard_offscreen(DisplayContext& display, OffscreenSurface& offscreen) {
    offscreen.width = default_view_width;
    offscreen.height = default_view_height;
    if (display.width == default_view_width && display.height == default_view_height) {
        return;
    }
    destroy_offscreen_surface(offscreen);
    set_display_size(display, default_view_width, default_view_height);
    create_offscreen_surface(offscreen);
    set_active_surface(&offscreen.surface);
}

} // namespace oa::present
