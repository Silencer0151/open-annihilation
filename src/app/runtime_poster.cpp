// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Captures: MakePoster's battlefield drawn view by view at 1:1 into 8-bit
// strips and streamed into a bottom-up BMP, and the numbered PCX screenshots
// and film frames of the frame on screen.
#include "oa/app/runtime.hpp"
#include "oa/core/map_plot.h"

#include "oa/present/pcx.hpp"
#include "oa/present/surface.hpp"
#include "oa/sim/messages.hpp"
#include "oa/ui/console/game_fields.hpp"
#include "oa/ui/console/hotkeys.hpp"
#include "oa/ui/hud/game_fields.hpp"
#include "oa/platform/files.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <new>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace oa::app {

namespace {

namespace console = oa::ui::console;
namespace present = oa::present;

constexpr const char* kPosterExtension = "bmp";
constexpr std::size_t kPosterPathBytes = 0x104;
constexpr uint8_t kSightFlags =
    console::visibility_flag::mapping | console::visibility_flag::line_of_sight;

// The C stdio stream the strip file opens, as the writer's byte stream.
present::ByteStream file_stream(std::FILE* file) {
    present::ByteStream stream{};
    stream.user = file;
    stream.write = [](void* user, const void* data, int32_t size) {
        return static_cast<int32_t>(
            std::fwrite(data, 1, static_cast<std::size_t>(size), static_cast<std::FILE*>(user))
        );
    };
    stream.seek = [](void* user, int32_t position) {
        return static_cast<int32_t>(std::fseek(static_cast<std::FILE*>(user), position, SEEK_SET));
    };
    stream.tell = [](void* user) {
        return static_cast<int32_t>(std::ftell(static_cast<std::FILE*>(user)));
    };
    stream.close = [](void* user) { std::fclose(static_cast<std::FILE*>(user)); };
    return stream;
}

constexpr const char* kScreenshotPrefix = "SHOT";
constexpr const char* kFilmFramePrefix = "FRAM";
constexpr const char* kScreenshotFolder = "%s\\screenshots";
constexpr int32_t kFilmTicksPerSecond = 30;

// Nearest palette entry by summed channel difference, the first on ties.
uint8_t nearest_index(const oa::PaletteBytes& palette, uint8_t r, uint8_t g, uint8_t b) {
    int best = 0;
    int best_distance = 0x7fffffff;
    for (int index = 0; index < static_cast<int>(oa::palette_color_count); ++index) {
        const auto* entry =
            palette.data() + static_cast<std::size_t>(index) * oa::palette_entry_bytes;
        const int distance =
            std::abs(entry[0] - r) + std::abs(entry[1] - g) + std::abs(entry[2] - b);
        if (distance < best_distance) {
            best = index;
            best_distance = distance;
            if (distance == 0)
                break;
        }
    }
    return static_cast<uint8_t>(best);
}

} // namespace

bool Runtime::save_numbered_frame(const char* directory, const char* prefix) {
    if (screen_ == Screen::match && match_use_layers_) {
        // A world the accelerated presentation scaled is drawn again as the
        // standard tier draws it, so the picture saved is the game's own.
        ensure_screen_world();
        compose_match_frame(surface_);
    }
    if (surface_.width == 0 || surface_.height == 0 ||
        surface_.rgb.size() != static_cast<std::size_t>(surface_.width) * surface_.height * 3U)
        return false;
    present::DisplayContext capture{};
    std::unordered_map<uint32_t, uint8_t> indices;
    const bool match = screen_ == Screen::match && match_;
    const oa::PaletteBytes& palette =
        resources_.background.palette ? *resources_.background.palette : resources_.gui_palette;
    if (match)
        capture.palette = match_display_context().palette;
    else
        capture.palette = present::palette_from_bytes(palette);
    present::SurfaceBuffer frame = present::create_surface(
        static_cast<int32_t>(surface_.width), static_cast<int32_t>(surface_.height)
    );
    for (std::size_t pixel = 0; pixel < frame.pixels.size(); ++pixel) {
        const auto* rgb = surface_.rgb.data() + pixel * 3U;
        const uint32_t key =
            (static_cast<uint32_t>(rgb[0]) << 16) | (static_cast<uint32_t>(rgb[1]) << 8) | rgb[2];
        const auto found = indices.find(key);
        if (found != indices.end()) {
            frame.pixels[pixel] = found->second;
            continue;
        }
        const uint8_t index = match ? match_palette_index(rgb[0], rgb[1], rgb[2])
                                    : nearest_index(palette, rgb[0], rgb[1], rgb[2]);
        indices.emplace(key, index);
        frame.pixels[pixel] = index;
    }
    capture.active_surface = &frame.surface;
    capture.use_active_surface = 1;
    present::NumberedFileHost files{};
    files.context = this;
    files.list =
        [](void* context, const char* pattern, void (*visit)(void*, const char*), void* user) {
            static_cast<Runtime*>(context)->list_save_files(pattern, visit, user);
        };
    files.open = [](void* context, const char* path, present::ByteStream* stream) {
        std::FILE* file = oa::platform::open_file(
            static_cast<Runtime*>(context)->save_game_root() / save_relative_path(path), "wb"
        );
        if (file == nullptr)
            return false;
        *stream = file_stream(file);
        return true;
    };
    return present::save_numbered_pcx(capture, directory, prefix, files);
}

void Runtime::capture_screenshot() {
    if (!match_)
        return;
    oa::Game& game = match_->state().game;
    char output[sizeof game.output_directory + 1] = {};
    std::memcpy(output, game.output_directory, sizeof game.output_directory);
    std::error_code error;
    fs::create_directories(save_game_root() / save_relative_path(output), error);
    char folder[present::numbered_pcx_path_bytes];
    // The path is cut to the buffer; one that cannot be formatted is left empty.
    if (std::snprintf(folder, sizeof folder, kScreenshotFolder, output) < 0)
        folder[0] = '\0';
    fs::create_directories(save_game_root() / save_relative_path(folder), error);
    status_ = save_numbered_frame(folder, kScreenshotPrefix) ? std::string("Saved screenshot")
                                                             : std::string("screenshot not saved");
    match_timing_.previous_clock =
        oa::base::game_loop::scaled_clock(clock_milliseconds(), match_clock_scale());
}

void Runtime::capture_film_frame() {
    if (screen_ != Screen::match || !match_)
        return;
    oa::Game& game = match_->state().game;
    if (game.capture_enabled <= 0 || game.next_capture_tick > game.tick || game.capture_rate <= 0)
        return;
    char path[sizeof game.capture_path + 1] = {};
    std::memcpy(path, game.capture_path, sizeof game.capture_path);
    if (!save_numbered_frame(path, kFilmFramePrefix)) {
        stop_film_capture(path);
        return;
    }
    game.next_capture_tick += static_cast<uint32_t>(kFilmTicksPerSecond / game.capture_rate);
    match_timing_.previous_clock =
        oa::base::game_loop::scaled_clock(clock_milliseconds(), match_clock_scale());
}

void Runtime::begin_film_capture(const char* path) {
    render();
    if (!save_numbered_frame(path, kFilmFramePrefix))
        stop_film_capture(path);
}

void Runtime::stop_film_capture(const char* path) {
    if (match_)
        match_->state().game.capture_enabled = 0;
    status_ = std::string("Film capture stopped: a frame could not be saved in ") + path;
    std::cerr << "open-annihilation: " << status_ << '\n';
}

Runtime::PosterScene Runtime::enter_poster_scene() {
    oa::Game& game = match_->state().game;
    PosterScene scene{
        match_camera_x_,
        match_camera_z_,
        static_cast<uint8_t>(game.visibility_flags & kSightFlags),
        static_cast<uint16_t>(game.sim_run_flags & console::kSimRunPaused),
        match_paused_,
        static_cast<uint16_t>(console::console_flags(game) & console::console_flag::clock),
        oa::ui::hud::message_lines(game),
    };
    game.visibility_flags = static_cast<uint8_t>(game.visibility_flags & ~kSightFlags);
    reset_match_sight(true);
    game.sim_run_flags = static_cast<uint16_t>(game.sim_run_flags & ~console::kSimRunPaused);
    match_paused_ = false;
    console::set_console_flags(
        game, static_cast<uint16_t>(console::console_flags(game) & ~console::console_flag::clock)
    );
    oa::ui::hud::set_message_lines(game, 0);
    return scene;
}

void Runtime::leave_poster_scene(const PosterScene& scene) {
    oa::Game& game = match_->state().game;
    game.sim_run_flags =
        static_cast<uint16_t>((game.sim_run_flags & ~console::kSimRunPaused) | scene.paused_flag);
    match_paused_ = scene.paused;
    console::set_console_flags(
        game,
        static_cast<uint16_t>(
            (console::console_flags(game) & ~console::console_flag::clock) | scene.clock_flag
        )
    );
    oa::ui::hud::set_message_lines(game, scene.message_lines);
    match_camera_x_ = scene.camera_x;
    match_camera_z_ = scene.camera_z;
    game.visibility_flags =
        static_cast<uint8_t>((game.visibility_flags & ~kSightFlags) | scene.sight_flags);
    reset_match_sight(true);
}

void Runtime::render_poster(
    const char* directory, const char* prefix, int32_t x, int32_t y, int32_t width, int32_t height
) {
    if (!match_)
        return;
    char name[kPosterPathBytes];
    console::HotkeyHost files{};
    files.context = this;
    files.list_files =
        [](void* context, const char* pattern, void (*visit)(void*, const char*), void* user) {
            static_cast<Runtime*>(context)->list_save_files(pattern, visit, user);
        };
    console::next_indexed_file_name(name, sizeof name, &files, directory, prefix, kPosterExtension);
    std::FILE* file = oa::platform::open_file(save_game_root() / save_relative_path(name), "wb");
    present::ByteStream stream = file_stream(file);
    present::BmpStripWriter writer;
    present::bmp_strip_writer_init(writer);
    if (!present::bmp_strip_writer_begin(
            writer, file != nullptr ? &stream : nullptr, match_display_context(), width, height
        )) {
        present::bmp_strip_writer_close(writer);
        return;
    }
    // The capture is 1:1 whatever the engine's zoom.
    const float zoom = match_zoom_;
    match_zoom_ = 1.0F;
    bind_match_view();
    oa::Game& game = match_->state().game;
    const int32_t tile_width = game.view_cells_width * OA_MAP_CELL_PIXELS;
    int32_t strip_rows = game.view_cells_height * OA_MAP_CELL_PIXELS - 1;
    std::vector<uint8_t> strip;
    try {
        strip.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(strip_rows));
    } catch (const std::bad_alloc&) {
        strip_rows /= 2;
        try {
            strip.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(strip_rows));
        } catch (const std::bad_alloc&) {
            strip.clear();
        }
    }
    if (!strip.empty()) {
        const PosterScene scene = enter_poster_scene();
        oa::Surface surface{};
        present::init_surface(surface, width, strip_rows, width, strip.data());
        int32_t row = 0;
        if (height > 0) {
            int32_t written_mark = 0;
            int32_t strip_end = strip_rows;
            int32_t skip_mark = -1;
            do {
                std::fill(strip.begin(), strip.end(), uint8_t{0});
                for (int32_t column = 0; column < width; column += tile_width) {
                    match_camera_x_ = column + x;
                    match_camera_z_ = row + y;
                    render_match_surface();
                    const int32_t clip_right = std::min(column + tile_width - 1, width - 1);
                    const int32_t shift_x = static_cast<int32_t>(game.camera_x) - x;
                    const int32_t shift_y = static_cast<int32_t>(game.camera_y) - row - y;
                    const auto& view = match_world_cpu_;
                    for (int32_t j = 0; j < static_cast<int32_t>(view.height); ++j) {
                        const int32_t target_row = j + shift_y;
                        if (target_row < 0 || target_row >= strip_rows)
                            continue;
                        for (int32_t i = 0; i < static_cast<int32_t>(view.width); ++i) {
                            const int32_t target_column = i + shift_x;
                            if (target_column < column || target_column > clip_right)
                                continue;
                            const auto* rgb =
                                view.rgb.data() + (static_cast<std::size_t>(j) * view.width +
                                                   static_cast<std::size_t>(i)) *
                                                      3U;
                            strip
                                [static_cast<std::size_t>(target_row) *
                                     static_cast<std::size_t>(width) +
                                 static_cast<std::size_t>(target_column)] =
                                    match_palette_index(rgb[0], rgb[1], rgb[2]);
                        }
                    }
                }
                int32_t source_row = 0;
                int32_t rows = strip_rows;
                int32_t remaining_mark = written_mark;
                int32_t first = row;
                if (skip_mark < -1) {
                    first = row + 1;
                    remaining_mark = skip_mark;
                    rows = strip_rows - 1;
                    source_row = 1;
                }
                if (height < first + rows)
                    rows = height + remaining_mark;
                if (!present::bmp_strip_writer_write_rows(writer, surface, rows, row, source_row))
                    break;
                if (strip_end < height) {
                    --row;
                    ++written_mark;
                    ++skip_mark;
                    --strip_end;
                }
                written_mark -= strip_rows;
                skip_mark -= strip_rows;
                strip_end += strip_rows;
                row += strip_rows;
            } while (row < height);
        }
        leave_poster_scene(scene);
    }
    match_zoom_ = zoom;
    render_match_surface();
    present::bmp_strip_writer_close(writer);
}

void Runtime::check_console_poster_command(const std::function<void(const char*)>& enter_line) {
    const auto require = [](bool ok, const std::string& what) {
        if (!ok)
            throw std::runtime_error("console poster check: " + what);
    };
    oa::Game& game = match_->state().game;
    const std::string kept_output(
        game.output_directory, strnlen(game.output_directory, sizeof game.output_directory)
    );
    const auto folder = save_game_root() / "posters" / "screenshots";
    std::error_code error;
    fs::remove_all(save_game_root() / "posters", error);
    enter_line("+Film posters");
    const auto camera_x = match_camera_x_;
    const auto camera_z = match_camera_z_;
    const auto sight = game.visibility_flags;
    const auto flags = console::console_flags(game);
    const auto lines = oa::ui::hud::message_lines(game);
    enter_line("+MakePoster all");
    require(
        match_camera_x_ == camera_x && match_camera_z_ == camera_z &&
            game.visibility_flags == sight && console::console_flags(game) == flags &&
            oa::ui::hud::message_lines(game) == lines,
        "the camera, sight rules, clock or message log were not put back"
    );
    const auto read = [&](const fs::path& path) {
        std::ifstream in(path, std::ios::binary);
        return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), {});
    };
    const auto le32 = [](const std::vector<uint8_t>& bytes, std::size_t at) {
        return static_cast<int32_t>(
            bytes[at] | bytes[at + 1] << 8 | bytes[at + 2] << 16 |
            static_cast<uint32_t>(bytes[at + 3]) << 24
        );
    };
    const auto bytes = read(folder / "BIGSHOT0001.bmp");
    const int32_t width = game.map_pixel_width;
    const int32_t height = game.map_pixel_height;
    const int32_t stride = (width + 3) & ~3;
    require(
        bytes.size() == static_cast<std::size_t>(present::bmp_pixel_offset) +
                            static_cast<std::size_t>(stride) * static_cast<std::size_t>(height),
        "BIGSHOT0001.bmp is missing or not one byte per map pixel"
    );
    require(
        bytes[0] == 'B' && bytes[1] == 'M' && le32(bytes, 10) == present::bmp_pixel_offset &&
            le32(bytes, 18) == width && le32(bytes, 22) == height && bytes[28] == 8,
        "BIGSHOT0001.bmp does not have an 8-bit header of the map's size"
    );
    const auto& palette = match_display_context().palette;
    for (int32_t index = 0; index < OA_PALETTE_COLORS; ++index) {
        const auto* quad = bytes.data() + present::bmp_file_header_size +
                           present::bmp_info_header_size + index * 4;
        const auto& entry = palette.entries[index];
        require(
            quad[0] == entry.b && quad[1] == entry.g && quad[2] == entry.r,
            "the colour table is not the match palette"
        );
    }
    const auto pixel = [&](int32_t column, int32_t image_row) {
        return bytes
            [static_cast<std::size_t>(present::bmp_pixel_offset) +
             static_cast<std::size_t>(height - 1 - image_row) * static_cast<std::size_t>(stride) +
             static_cast<std::size_t>(column)];
    };
    // The view the capture drew with the camera at (x, y), as palette indices.
    const float zoom = match_zoom_;
    match_zoom_ = 1.0F;
    const PosterScene scene = enter_poster_scene();
    const int32_t tile_width = game.view_cells_width * OA_MAP_CELL_PIXELS;
    const int32_t strip_rows = game.view_cells_height * OA_MAP_CELL_PIXELS - 1;
    const auto view_at = [&](int32_t x, int32_t y) {
        match_camera_x_ = x;
        match_camera_z_ = y;
        render_match_surface();
        const auto& view = match_world_cpu_;
        std::vector<uint8_t> indices(static_cast<std::size_t>(view.width) * view.height);
        for (std::size_t i = 0; i < indices.size(); ++i)
            indices[i] =
                match_palette_index(view.rgb[i * 3], view.rgb[i * 3 + 1], view.rgb[i * 3 + 2]);
        return std::pair{indices, static_cast<int32_t>(view.width)};
    };
    const auto [corner, corner_width] = view_at(0, 0);
    const auto [second, second_width] = view_at(0, strip_rows - 1);
    leave_poster_scene(scene);
    match_zoom_ = zoom;
    render_match_surface();
    require(height > strip_rows * 2, "the map is not three strips high");
    for (int32_t column = 0; column < tile_width; ++column) {
        for (int32_t row = 0; row < strip_rows - 1; ++row)
            require(
                pixel(column, row) == corner[static_cast<std::size_t>(row * corner_width + column)],
                "the first strip is not the view at the map's corner"
            );
        // The second strip's rows 1.. land from image row strip_rows - 1.
        for (int32_t row = 1; row < strip_rows; ++row)
            require(
                pixel(column, strip_rows - 2 + row) ==
                    second[static_cast<std::size_t>(row * second_width + column)],
                "the second strip is not one row higher than its map rows"
            );
    }
    for (int32_t column = 0; column < width; ++column)
        require(pixel(column, height - 1) == 0, "the image's bottom row was written");

    enter_line("+MakePoster 10 10");
    const auto small = read(folder / "BIGSHOT0002.bmp");
    require(
        small.size() > present::bmp_pixel_offset && le32(small, 18) == tile_width &&
            le32(small, 22) == game.view_cells_height * OA_MAP_CELL_PIXELS,
        "MakePoster 10 10 did not write BIGSHOT0002.bmp one view in size"
    );
    // Ctrl+F9 numbers the frame after the highest SHOTnnnn.pcx
    // there, and a running film writes FRAMnnnn.pcx each FilmSpeed tick.
    capture_screenshot();
    capture_screenshot();
    const auto shot = read(folder / "SHOT0002.pcx");
    const auto le16 = [](const std::vector<uint8_t>& bytes, std::size_t at) {
        return static_cast<int32_t>(bytes[at] | (bytes[at + 1] << 8));
    };
    require(
        fs::exists(folder / "SHOT0001.pcx") && shot.size() > 128 && shot[0] == 0x0a &&
            shot[1] == 5 && le16(shot, 8) + 1 == static_cast<int32_t>(surface_.width) &&
            le16(shot, 10) + 1 == static_cast<int32_t>(surface_.height),
        "Ctrl+F9 did not write SHOT0001.pcx and SHOT0002.pcx of the frame"
    );
    const auto film = save_game_root() / "posters" / "MOVIE001";
    fs::create_directories(film, error);
    std::snprintf(game.capture_path, sizeof game.capture_path, "%s", "posters\\MOVIE001");
    game.capture_enabled = 1;
    game.capture_rate = 10;
    game.next_capture_tick = game.tick;
    capture_film_frame();
    capture_film_frame();
    require(
        fs::exists(film / "FRAM0001.pcx") && !fs::exists(film / "FRAM0002.pcx") &&
            game.next_capture_tick == game.tick + 3,
        "the film did not write FRAM0001.pcx once and wait three ticks"
    );
    game.capture_enabled = 0;
    game.capture_path[0] = '\0';
    // Film's change is undone before any save takes it.
    std::snprintf(game.output_directory, sizeof game.output_directory, "%s", kept_output.c_str());
    game.output_directory_changed = 0U;
    // The log's ring fills with the check's echoes; the checks after it start
    // from an empty log.
    oa::sim::messages::clear_messages(game);
    fs::remove_all(save_game_root() / "posters", error);
    std::cout << "console poster check: MakePoster writes the map as BIGSHOTnnnn.bmp strip by "
                 "strip, each later strip a row higher and the bottom row black, and puts the "
                 "view back; Ctrl+F9 writes SHOT0001/0002.pcx and the film FRAM0001.pcx\n";
}

} // namespace oa::app
