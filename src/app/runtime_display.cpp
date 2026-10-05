// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The session display: its start, the lookup tables, and the sinks that
// receive each finished 8-bit frame (SDL texture or headless capture).
#include "oa/app/runtime.hpp"
#include "oa/app/game_directory.hpp"
#include "render_run.hpp"
#include "oa/base/float_precision.hpp"
#include "oa/present/palette_tables.hpp"
#include "oa/present/pcx.hpp"
#include <SDL3/SDL.h>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace oa::app {
namespace {

// Text colour value text draws skip after session start.
constexpr uint32_t kSessionTextTransparent = 0xfe;
constexpr uint32_t kOpaqueTexel = 0xff000000u;

// Missing and unreadable optional tables both fall back to rebuilding.
std::vector<uint8_t> read_optional(oa::AssetStore& assets, const std::string& path) {
    try {
        return assets.read(path).bytes;
    } catch (const std::exception&) {
        return {};
    }
}

const char* table_source_text(oa::present::TableSource source) {
    switch (source) {
    case oa::present::TableSource::file:
        return "file";
    case oa::present::TableSource::built:
        return "built";
    case oa::present::TableSource::rejected:
        return "built (file has the wrong size)";
    }
    return "?";
}

// Headless sink: keeps the frame and its palette for snapshots.
void capture_sink_frame(
    void* user,
    const uint8_t* pixels,
    int32_t pitch,
    int32_t width,
    int32_t height,
    const oa::Palette* palette
) {
    auto& captured = *static_cast<CapturedFrame*>(user);
    if (pixels == nullptr || palette == nullptr || width <= 0 || height <= 0 || pitch < width)
        return;
    if (captured.frame.surface.width != width || captured.frame.surface.height != height)
        captured.frame = oa::present::create_surface(width, height);
    for (int32_t y = 0; y < height; ++y)
        std::memcpy(
            captured.frame.pixels.data() +
                static_cast<std::size_t>(y) * static_cast<std::size_t>(width),
            pixels + static_cast<std::ptrdiff_t>(y) * pitch,
            static_cast<std::size_t>(width)
        );
    captured.palette = *palette;
}

} // namespace

SessionDisplay::~SessionDisplay() {
    oa::present::destroy_offscreen_surface(offscreen);
    oa::present::shutdown_display(context);
    if (oa::present::display_context() == &context)
        oa::present::bind_display(nullptr);
}

void Runtime::start_session_display() {
    namespace present = oa::present;
    auto& display = display_.context;
    display.sink = options_.headless_check
                       ? RenderSink{&captured_frame_, nullptr, &capture_sink_frame, nullptr}
                       : RenderSink{this, nullptr, &Runtime::present_sink_frame, nullptr};
    present::reset_view_defaults(display);
    display.startup_request = present::display_request_session;
    present::bind_display(&display);
    if (present::start_display(display) == 0)
        throw std::runtime_error("session display initialization failed");
    display_.offscreen.width = display.width;
    display_.offscreen.height = display.height;
    present::create_offscreen_surface(display_.offscreen);
    present::set_session_flag(0);
    const auto game_palette = load_active_palette(assets_);
    const auto alpha = read_optional(assets_, "palettes/palette.alp");
    const auto shade = read_optional(assets_, "palettes/palette.shd");
    const auto light = read_optional(assets_, "palettes/palette.lht");
    const auto sources = present::load_session_tables(
        display,
        present::palette_from_bytes(game_palette),
        present::PaletteTableFiles{alpha, shade, light}
    );
    if (sources.alpha == present::TableSource::rejected ||
        sources.shade == present::TableSource::rejected ||
        sources.light == present::TableSource::rejected)
        std::cerr << "session display: palette tables alp " << table_source_text(sources.alpha)
                  << ", shd " << table_source_text(sources.shade) << ", lht "
                  << table_source_text(sources.light) << '\n';
    const auto gui_palette = read_optional(assets_, "palettes/guipal.pal");
    if (gui_palette.size() == sizeof(oa::Palette)) {
        const auto palette = present::palette_from_bytes(gui_palette);
        present::apply_palette_entries(
            display, palette.entries, 0, OA_PALETTE_COLORS, display.device_palette
        );
    }
    present::set_text_transparent(kSessionTextTransparent);
}

void Runtime::show_display_frame() {
    oa::present::draw_frame();
    const auto& frame = captured_frame_.frame.surface;
    if (!options_.headless_check || frame.pixels == nullptr)
        return;
    surface_ = {
        static_cast<uint32_t>(frame.width),
        static_cast<uint32_t>(frame.height),
        oa::present::to_rgb(frame, captured_frame_.palette)
    };
}

void Runtime::write_display_pcx(const fs::path& path) const {
    oa::present::MemoryWriter writer;
    auto stream = oa::present::memory_writer_stream(writer);
    const auto status =
        oa::present::save_pcx_surface(stream, display_.context, captured_frame_.frame.surface);
    if (status != oa::present::PcxStatus::ok)
        throw std::runtime_error(
            "cannot encode " + path_to_utf8(path) + ": " + oa::present::pcx_status_text(status)
        );
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(
        reinterpret_cast<const char*>(writer.bytes.data()),
        static_cast<std::streamsize>(writer.bytes.size())
    );
    if (!output)
        throw std::runtime_error("cannot write " + path_to_utf8(path));
}

void Runtime::present_sink_frame(
    void* user,
    const uint8_t* pixels,
    int32_t pitch,
    int32_t width,
    int32_t height,
    const oa::Palette* palette
) {
    if (palette == nullptr)
        return;
    auto& runtime = *static_cast<Runtime*>(user);
    // The sink is called from noexcept presentation code.
    try {
        runtime.present_indexed_frame(pixels, pitch, width, height, *palette);
    } catch (const PresentError& error) {
        // The loading pump's next render() makes the renderer again.
        if (runtime.render_run_)
            runtime.note_present_error(error.what(), false);
        else
            std::cerr << "display sink: " << error.what() << '\n';
    } catch (const std::exception& error) {
        std::cerr << "display sink: " << error.what() << '\n';
    }
}

void Runtime::present_indexed_frame(
    const uint8_t* pixels, int32_t pitch, int32_t width, int32_t height, const oa::Palette& palette
) {
    if (sdl_.renderer == nullptr || pixels == nullptr || width <= 0 || height <= 0 || pitch < width)
        return;
    update_render_tier();
    auto& output = indexed_output_;
    if (!output.texels_ready || std::memcmp(&output.palette, &palette, sizeof palette) != 0) {
        output.palette = palette;
        for (std::size_t index = 0; index < output.texels.size(); ++index) {
            const auto& entry = palette.entries[index];
            output.texels[index] = kOpaqueTexel | (static_cast<uint32_t>(entry.r) << 16) |
                                   (static_cast<uint32_t>(entry.g) << 8) |
                                   static_cast<uint32_t>(entry.b);
        }
        output.texels_ready = true;
    }
    ensure_streaming_texture(
        output.texture, loading_layer_format(), width, height, output.width, output.height
    );
    const auto present_start = std::chrono::steady_clock::now();
    void* texels = nullptr;
    int texture_pitch = 0;
    if (!SDL_LockTexture(output.texture, nullptr, &texels, &texture_pitch))
        throw_present_error("SDL_LockTexture");
    for (int32_t y = 0; y < height; ++y) {
        const uint8_t* row = pixels + static_cast<std::ptrdiff_t>(y) * pitch;
        auto* out = reinterpret_cast<uint32_t*>(
            static_cast<uint8_t*>(texels) + static_cast<std::ptrdiff_t>(y) * texture_pitch
        );
        for (int32_t x = 0; x < width; ++x)
            out[x] = output.texels[row[x]];
    }
    SDL_UnlockTexture(output.texture);
    // The standard tier's scale mode is found before the frame begins: the
    // first look at the pixel-art mode draws into a target of its own.
    const SDL_ScaleMode standard_mode = standard_frame_scale_mode(width);
    if (!SDL_SetRenderDrawColor(sdl_.renderer, 0, 0, 0, 255) || !SDL_RenderClear(sdl_.renderer))
        throw_present_error("SDL render");
    // Each frame the sink presents is a new picture.
    bool drawn = false;
    if (accelerated_presentation()) {
        try {
            draw_accelerated_screen(output.texture, width, height, ++accelerated_.screen_revision);
            drawn = true;
        } catch (const AccelerationError& error) {
            take_acceleration_error(error);
        }
    }
    if (!drawn)
        draw_frame(sdl_.renderer, output.texture, standard_mode);
    present_software_cursor(false);
    capture_render_target();
    if (render_fault_due(RenderFaultPoint::present))
        throw PresentError("injected present error");
    if (!SDL_RenderPresent(sdl_.renderer))
        note_present_refused("SDL_RenderPresent");
    oa::base::float_precision::restore_program_float_control();
    const auto present_ns =
        static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                  std::chrono::steady_clock::now() - present_start
        )
                                  .count());
    frame_pacing::note_frame_measure(frame_stats_, frame_pacing::FrameMeasure::present, present_ns);
    note_present_time(present_ns);
}

} // namespace oa::app
