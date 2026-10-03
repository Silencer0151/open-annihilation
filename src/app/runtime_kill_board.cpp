// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The kills board F4 slides in at the top right of the battlefield.
#include "oa/app/runtime.hpp"
#include "full_fog.hpp"
#include "oa/ui/frontend_dialogs.hpp"
#include "oa/ui/frontend_renderer/gadget_draw.hpp"
#include "oa/present/model/mesh_raster.hpp"
#include "oa/present/raster.hpp"
#include "oa/ui/console/game_fields.hpp"
#include "oa/ui/hud/kill_board.hpp"
#include "oa/ui/hud/resource_bar.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace oa::app {

namespace {

namespace hud = oa::ui::hud;

// An overlay is drawn over these two backgrounds; its pixels are where the
// passes agree.
constexpr uint8_t kPassFill[2] = {0x00, 0xff};
/// Most a channel of the battlefield the Full tier's card darkened under
/// the board may differ from the darkening computed exactly: the renderer
/// rounds the blend its own way.
constexpr int kMostCardShadeDifference = 2;
// Rows above and below a text line, and columns past its width, its glyphs
// may reach.
constexpr int kGlyphReach = 16;

uint32_t rgb_key(const uint8_t* rgb) {
    return static_cast<uint32_t>(rgb[0]) << 16 | static_cast<uint32_t>(rgb[1]) << 8 | rgb[2];
}

// Palette index of a composed pixel: the lowest entry of its colour, else
// the nearest by summed channel difference.
class PaletteLookup {
  public:

    explicit PaletteLookup(const oa::PaletteBytes& palette) : palette_(palette) {
        for (std::size_t i = OA_PALETTE_COLORS; i-- > 0;)
            index_of_[rgb_key(&palette_[i * 4U])] = static_cast<uint8_t>(i);
    }

    uint8_t index(const uint8_t* rgb) {
        const auto key = rgb_key(rgb);
        if (const auto found = index_of_.find(key); found != index_of_.end())
            return found->second;
        int best = std::numeric_limits<int>::max();
        uint8_t nearest = 0;
        for (std::size_t i = 0; i < OA_PALETTE_COLORS; ++i) {
            const auto* entry = &palette_[i * 4U];
            const int distance = std::abs(entry[0] - rgb[0]) + std::abs(entry[1] - rgb[1]) +
                                 std::abs(entry[2] - rgb[2]);
            if (distance < best) {
                best = distance;
                nearest = static_cast<uint8_t>(i);
            }
        }
        index_of_.emplace(key, nearest);
        return nearest;
    }

  private:

    const oa::PaletteBytes& palette_;
    std::unordered_map<uint32_t, uint8_t> index_of_;
};

} // namespace

oa::ui::display_layout::Point Runtime::board_canvas(int x, int y) const {
    const int scale = hud_text_scale();
    return {
        match_layout_.width - (oa::ui::display_layout::kSourceWidth - x) * scale,
        match_layout_.top + (y - oa::ui::display_layout::kSourceTop) * scale
    };
}

uint8_t* Runtime::board_pixel(int canvas_x, int canvas_y) {
    const auto point = canvas_paint(canvas_x, canvas_y);
    auto& layer = paint_target();
    if (point.x < 0 || point.y < 0 || point.x >= static_cast<int>(layer.width) ||
        point.y >= static_cast<int>(layer.height))
        return nullptr;
    const auto offset =
        static_cast<std::size_t>(point.y) * layer.width + static_cast<std::size_t>(point.x);
    return layer.rgb.data() + offset * 3U;
}

void Runtime::shade_board_rect(int x0, int y0, int x1, int y1, int level) {
    const auto corner = board_canvas(x0, y0);
    const auto end = board_canvas(x1 + 1, y1 + 1);
    const int width = end.x - corner.x;
    const int height = end.y - corner.y;
    if (width <= 0 || height <= 0)
        return;
    // In the Full tier the card darkens or lights the battlefield under the
    // board; the board's foreground goes on the overlay canvas.
    const auto at = canvas_paint(corner.x, corner.y);
    if (paint_world_level(at.x, at.y, width, height, level))
        return;
    PaletteLookup lookup(match_palette_);
    auto patch = oa::present::create_surface(width, height);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            if (const auto* rgb = board_pixel(corner.x + x, corner.y + y))
                patch.pixels[static_cast<std::size_t>(y * width + x)] = lookup.index(rgb);
    oa::Rect32 rect{0, 0, width - 1, height - 1};
    if (oa::present::shade_rect_level(&patch.surface, &rect, level) == 0)
        return;
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            if (auto* rgb = board_pixel(corner.x + x, corner.y + y)) {
                const auto index = patch.pixels[static_cast<std::size_t>(y * width + x)];
                const auto color = palette_rgb(index);
                std::copy(color.begin(), color.end(), rgb);
            }
}

void Runtime::overlay_patch(
    oa::ui::display_layout::Point corner,
    int width,
    int height,
    int columns,
    void (*draw)(void* user, oa::Surface& surface),
    void* user
) {
    if (width <= 0 || height <= 0)
        return;
    oa::present::SurfaceBuffer passes[2]{
        oa::present::create_surface(width, height), oa::present::create_surface(width, height)
    };
    for (int pass = 0; pass < 2; ++pass) {
        std::fill(passes[pass].pixels.begin(), passes[pass].pixels.end(), kPassFill[pass]);
        draw(user, passes[pass].surface);
    }
    auto& layer = paint_target();
    const int scale = hud_text_scale();
    columns = std::min(width, columns);
    for (int row = 0; row < height; ++row)
        for (int column = 0; column < columns; ++column) {
            const auto offset = static_cast<std::size_t>(row * width + column);
            const auto index = passes[0].pixels[offset];
            if (index != passes[1].pixels[offset])
                continue;
            const auto color = palette_rgb(index);
            for (int by = 0; by < scale; ++by)
                for (int bx = 0; bx < scale; ++bx) {
                    const int px = corner.x + column * scale + bx;
                    const int py = corner.y + row * scale + by;
                    if (px < 0 || py < 0 || px >= static_cast<int>(layer.width) ||
                        py >= static_cast<int>(layer.height))
                        continue;
                    const auto pixel =
                        static_cast<std::size_t>(py) * layer.width + static_cast<std::size_t>(px);
                    std::copy(color.begin(), color.end(), layer.rgb.data() + pixel * 3U);
                }
        }
}

std::optional<oa::formats::gaf::RenderedFrame>
Runtime::player_logo_frame(const oa::Player& player) {
    if (!match_)
        return std::nullopt;
    const auto* info = oa::world_player_info(&match_->state(), &player);
    const auto* logos = logo_sequence_;
    if (info == nullptr || logos == nullptr || info->color >= logos->frames.size())
        return std::nullopt;
    auto rendered = oa::formats::gaf::render_normal(logos->frames[info->color]);
    if (!rendered.ok())
        return std::nullopt;
    return std::move(*rendered.frame);
}

void Runtime::overlay_board_patch(
    int x, int y, int width, int height, void (*draw)(void* user, oa::Surface& surface), void* user
) {
    const auto corner = board_canvas(x, y);
    overlay_patch(
        canvas_paint(corner.x, corner.y),
        width,
        height,
        oa::ui::display_layout::kSourceWidth - x,
        draw,
        user
    );
}

void Runtime::ensure_gui_font() {
    if (gui_fonts_loaded_)
        return;
    gui_fonts_loaded_ = true;
    const auto load = [this](const char* path, oa::present::GafSprites& font) {
        try {
            const auto bytes = assets_.read(path).bytes;
            const auto status = renderer::load_gui_font(bytes, font);
            if (status != oa::present::GafStatus::ok) {
                font = {};
                std::cerr << path << " unavailable: " << oa::present::gaf_status_text(status)
                          << '\n';
            }
        } catch (const std::exception& error) {
            font = {};
            std::cerr << path << " unavailable: " << error.what() << '\n';
        }
    };
    load("anims/hattfont12.gaf", gui_font_);
    load("anims/hattfont11.gaf", gui_label_font_);
}

void Runtime::overlay_gui_text(
    const oa::present::GafSprites& font,
    oa::ui::display_layout::Point pen,
    std::string_view text,
    int rows_below_pen
) {
    if (font.sequences.empty() || text.empty() || rows_below_pen <= 0)
        return;
    const std::string line(text);
    const auto* glyphs = &font.sequences.front();
    int width = 0;
    for (const unsigned char byte : line)
        if (const oa::Sprite* glyph = byte >= ' ' ? oa::present::gaf_frame(glyphs, byte) : nullptr)
            width += glyph->width;

    struct Run {
        const oa::present::GafSprites* font;
        const char* text;
    } const run{&font, line.c_str()};

    const int scale = hud_text_scale();
    // The patch reaches a glyph's width left of the pen and a glyph's height
    // above it; below, it stops where the caller cuts the text off.
    overlay_patch(
        {pen.x - kGlyphReach * scale, pen.y - kGlyphReach * scale},
        width + 2 * kGlyphReach,
        kGlyphReach + std::min(rows_below_pen, 2 * kGlyphReach),
        width + 2 * kGlyphReach,
        [](void* context, oa::Surface& surface) {
            const auto& run = *static_cast<const Run*>(context);
            renderer::draw_gadget_text(
                &surface,
                run.font,
                run.text,
                kGlyphReach,
                kGlyphReach,
                renderer::gadget_text_unbounded,
                0
            );
        },
        const_cast<Run*>(&run)
    );
}

void Runtime::draw_match_kill_board() {
    if (!match_ || campaign_mission_)
        return;
    ensure_gui_font();
    auto& world = match_->state();
    hud::KillBoardSink sink{};
    sink.user = this;
    sink.play_sound = [](void* user, const char* name) {
        static_cast<Runtime*>(user)->play_match_interface_sound(name);
    };
    sink.shade = [](void* user, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t level) {
        static_cast<Runtime*>(user)->shade_board_rect(x0, y0, x1, y1, level);
    };
    sink.text =
        [](void* user, const char* text, int32_t x, int32_t y, int32_t width, uint8_t flash) {
            struct Line {
                const oa::present::GafSprites* font;
                const char* text;
                int32_t width;
                uint8_t flash;
            } line{&static_cast<Runtime*>(user)->gui_font_, text, width, flash};
            static_cast<Runtime*>(user)->overlay_board_patch(
                x,
                y - kGlyphReach,
                width + kGlyphReach,
                3 * kGlyphReach,
                [](void* context, oa::Surface& surface) {
                    const auto& line = *static_cast<const Line*>(context);
                    renderer::draw_gadget_text(
                        &surface, line.font, line.text, 0, kGlyphReach, line.width, line.flash
                    );
                },
                &line
            );
        };
    // The advances of every byte the font has a glyph for.
    sink.text_width = [](void* user, const char* text) {
        const auto& font = static_cast<Runtime*>(user)->gui_font_;
        const auto* glyphs = font.sequences.empty() ? nullptr : &font.sequences.front();
        int32_t width = 0;
        for (const auto* p = reinterpret_cast<const unsigned char*>(text); *p != 0; ++p)
            if (const oa::Sprite* glyph = oa::present::gaf_frame(glyphs, *p))
                width += glyph->width;
        return width;
    };
    // The colour's logo, less its outer pixel, is stretched over the row.
    sink.logo =
        [](void* user, const oa::Player& player, int32_t x0, int32_t y0, int32_t x1, int32_t y1) {
            auto& runtime = *static_cast<Runtime*>(user);
            const auto rendered = runtime.player_logo_frame(player);
            if (!rendered)
                return;
            const auto& frame = *rendered;
            oa::Sprite texture{};
            texture.width = frame.width;
            texture.height = frame.height;
            texture.key = frame.transparency_index;
            texture.encoding = OA_SPRITE_RAW;
            texture.data = const_cast<uint8_t*>(frame.pixels.data());
            const int32_t right = x1 - x0, bottom = y1 - y0;
            const int32_t u = frame.width - 1, v = frame.height - 1;
            struct Logo {
                oa::Sprite texture;
                oa::present::PolygonVertex quad[4];
                oa::present::model::TexturePoint uv[4];
            } const logo{
                texture,
                {{0, 0}, {right, 0}, {right, bottom}, {0, bottom}},
                {{1, 1}, {u, 1}, {u, v}, {1, v}},
            };
            runtime.overlay_board_patch(
                x0,
                y0,
                right + 1,
                bottom + 1,
                [](void* context, oa::Surface& surface) {
                    const auto& logo = *static_cast<const Logo*>(context);
                    oa::present::model::texture_quad(&surface, &logo.texture, logo.quad, logo.uv);
                },
                const_cast<Logo*>(&logo)
            );
        };
    const bool held = control_key_down(oa::ui::gui_input::ControlKey::space);
    const auto now = oa::base::game_loop::scaled_clock(clock_milliseconds(), match_clock_scale());
    hud::draw_kill_board(
        world,
        kill_board_,
        now,
        hud::kill_board_wanted(world.game, held, chat_composing_),
        oa::ui::display_layout::kSourceWidth,
        sink
    );
}

namespace {

using oa::ui::display_layout::Rect;

// Distance from the pointer that covers every cursor frame.
constexpr int kCursorReach = 64;
// Readout steps that close any store gap (the readout eases an eighth a step).
constexpr int kReadoutSettleSteps = 256;
// Frames the board takes to slide all the way in or out.
constexpr int kSlideFrames = 18;
constexpr int kShadeRow = 0x20 - 0x18; // shade table row of the board's level
constexpr int kHeaderWidth = 30;       // screen columns the "Kills" header covers
constexpr int kHeaderRows = 14;
constexpr int kLogoRowsBeforeName = 4;

bool inside(const Rect& rect, int x, int y) {
    return x >= rect.x && y >= rect.y && x < rect.x + rect.width && y < rect.y + rect.height;
}

const uint8_t* pixel(const renderer::Surface& frame, int x, int y) {
    const auto offset = static_cast<std::size_t>(y) * frame.width + static_cast<std::size_t>(x);
    return &frame.rgb[offset * 3U];
}

bool same_pixel(const renderer::Surface& a, const renderer::Surface& b, int x, int y) {
    return std::equal(pixel(a, x, y), pixel(a, x, y) + 3, pixel(b, x, y));
}

std::size_t
differing_inside(const renderer::Surface& a, const renderer::Surface& b, const Rect& area) {
    std::size_t count = 0;
    for (int y = area.y; y < area.y + area.height; ++y)
        for (int x = area.x; x < area.x + area.width; ++x)
            if (!same_pixel(a, b, x, y))
                ++count;
    return count;
}

std::size_t differing_outside(
    const renderer::Surface& a, const renderer::Surface& b, std::initializer_list<Rect> excluded
) {
    std::size_t count = 0;
    for (int y = 0; y < static_cast<int>(a.height); ++y)
        for (int x = 0; x < static_cast<int>(a.width); ++x) {
            const auto covers = [x, y](const Rect& rect) { return inside(rect, x, y); };
            const bool skipped = std::any_of(excluded.begin(), excluded.end(), covers);
            if (!skipped && !same_pixel(a, b, x, y))
                ++count;
        }
    return count;
}

} // namespace

void Runtime::check_kill_board() {
    namespace dialogs = oa::ui::frontend_dialogs;
    const fs::path report_directory = "local/reports";
    fs::create_directories(report_directory);
    start_benchmark_skirmish();
    auto& game = match_->state().game;
    if (const auto viewer = match_view_player(); viewer < OA_PLAYER_COUNT)
        for (int step = 0; step < kReadoutSettleSteps; ++step)
            hud::update_resource_readout(game.resource_readout, game.players[viewer], game.tick);
    // With mapping and line of sight off the whole map shows, so the board
    // shades terrain rather than unexplored black.
    namespace visibility_flag = oa::ui::console::visibility_flag;
    auto& visibility = game.visibility_flags;
    visibility = static_cast<uint8_t>(
        visibility & ~(visibility_flag::mapping | visibility_flag::line_of_sight)
    );
    reset_sight_presentation(false);
    pointer_x_ = static_cast<float>(match_layout_.left + kCursorReach);
    pointer_y_ = static_cast<float>(match_layout_.height / 2);
    const Rect cursor{
        static_cast<int>(pointer_x_) - kCursorReach,
        static_cast<int>(pointer_y_) - kCursorReach,
        2 * kCursorReach,
        2 * kCursorReach
    };
    bool running = true;
    const auto press_f4 = [&] {
        SDL_Event event{};
        event.type = SDL_EVENT_KEY_DOWN;
        event.key.key = SDLK_F4;
        event.key.scancode = SDL_SCANCODE_F4;
        handle_sdl_event(event, running);
    };
    const auto capture = [&](renderer::Surface& frame) {
        capture_frame_ = &frame;
        render();
        capture_frame_ = nullptr;
    };
    const auto slide_to = [&](int32_t slide, renderer::Surface& frame) {
        int frames = 0;
        while (kill_board_.slide != slide && frames <= kSlideFrames) {
            capture(frame);
            ++frames;
        }
        if (frames != kSlideFrames)
            throw std::runtime_error(
                "kill board check: the board took " + std::to_string(frames) +
                " frames to slide to " + std::to_string(slide)
            );
        capture(frame);
    };

    renderer::Surface hidden;
    capture(hidden);
    write_ppm(report_directory / "native-kill-board-off.ppm", hidden);
    // In the Full tier the board's shading is a black quad the card blends
    // over the battlefield at the shade level's alpha; elsewhere the shade
    // table's row.
    const bool full = full_presentation();
    if (kill_board_.slide != 0)
        throw std::runtime_error("kill board check: the board is out before F4");
    press_f4();
    if ((game.graphics_flags & hud::kGraphicsBoardPinned) == 0)
        throw std::runtime_error("kill board check: F4 did not pin the board");
    if (dialogs::dialog_count() != 0 || match_paused_ || screen_ != Screen::match)
        throw std::runtime_error("kill board check: F4 opened a dialog");
    renderer::Surface shown;
    slide_to(hud::kBoardWidth, shown);
    write_ppm(report_directory / "native-kill-board-on.ppm", shown);
    if (dialogs::dialog_count() != 0 || match_paused_)
        throw std::runtime_error("kill board check: a dialog opened with the board");

    const int scale = hud_text_scale();
    const int left = oa::ui::display_layout::kSourceWidth - hud::kBoardWidth;
    const int bottom = game.player_count * hud::kBoardRowHeight + 0x2e;
    const auto corner = board_canvas(left, hud::kBoardTop);
    const auto end = board_canvas(oa::ui::display_layout::kSourceWidth, bottom + 1);
    const Rect board{corner.x, corner.y, end.x - corner.x, end.y - corner.y};
    if (board.x + board.width != match_layout_.width || board.y != match_layout_.top)
        throw std::runtime_error("kill board check: the board is not at the battlefield's corner");
    if (differing_outside(hidden, shown, {board, cursor}) != 0)
        throw std::runtime_error("kill board check: the frame changed outside the board");
    const auto area = static_cast<std::size_t>(board.width * board.height);
    if (differing_inside(hidden, shown, board) < area / 2)
        throw std::runtime_error("kill board check: the board's corner is not drawn");

    const auto* shade = display_.context.shade_table;
    if (shade == nullptr)
        throw std::runtime_error("kill board check: no shade table");
    PaletteLookup lookup(match_palette_);
    const float kept = 1.0F - oa::app::full_fog::level_quad(hud::kBoardShadeLevel).colour.alpha;
    const auto shaded = [&](int x, int y) {
        if (full) {
            const uint8_t* under = pixel(hidden, x, y);
            std::array<uint8_t, 3> darkened{};
            for (std::size_t channel = 0; channel < 3; ++channel)
                darkened[channel] =
                    static_cast<uint8_t>(std::lround(static_cast<float>(under[channel]) * kept));
            return darkened;
        }
        const auto index = static_cast<int8_t>(lookup.index(pixel(hidden, x, y)));
        return palette_rgb(shade[kShadeRow * 0x100 + index]);
    };
    const auto shows = [&](int x, int y, const std::array<uint8_t, 3>& color) {
        const uint8_t* at = pixel(shown, x, y);
        if (!full)
            return std::equal(color.begin(), color.end(), at);
        for (std::size_t channel = 0; channel < 3; ++channel)
            if (std::abs(int{at[channel]} - int{color[channel]}) > kMostCardShadeDifference)
                return false;
        return true;
    };
    // Screen columns 515-516, left of the header and the highlight.
    for (int y = board.y; y < board.y + board.height; ++y)
        for (int x = board.x; x < board.x + 2 * scale; ++x)
            if (!shows(x, y, shaded(x, y)))
                throw std::runtime_error("kill board check: the margin is not shaded battlefield");
    std::size_t header = 0;
    const auto header_corner = board_canvas(left + 2, hud::kBoardTop);
    for (int y = header_corner.y; y < header_corner.y + kHeaderRows * scale; ++y)
        for (int x = header_corner.x; x < header_corner.x + kHeaderWidth * scale; ++x)
            if (!shows(x, y, shaded(x, y)))
                ++header;
    if (header < static_cast<std::size_t>(kHeaderWidth * scale * scale))
        throw std::runtime_error("kill board check: the Kills header is not drawn");
    const auto& local = game.players[game.local_player_index];
    const auto logo = player_logo_frame(local);
    if (!logo)
        throw std::runtime_error("kill board check: the local player has no colour logo");
    std::unordered_map<uint32_t, bool> logo_colors;
    for (const auto index : logo->pixels)
        logo_colors[rgb_key(palette_rgb(index).data())] = true;
    const int row_top = 0x54 + local.board_row * hud::kBoardRowHeight - 0x24;
    const auto logo_corner = board_canvas(left + 7, row_top);
    const auto logo_end = board_canvas(left + 0x77, row_top + kLogoRowsBeforeName);
    for (int y = logo_corner.y; y < logo_end.y; ++y)
        for (int x = logo_corner.x; x < logo_end.x; ++x)
            if (!logo_colors.contains(rgb_key(pixel(shown, x, y))))
                throw std::runtime_error("kill board check: the local row starts without its logo");

    press_f4();
    if ((game.graphics_flags & hud::kGraphicsBoardPinned) != 0)
        throw std::runtime_error("kill board check: a second F4 did not release the board");
    renderer::Surface gone;
    slide_to(0, gone);
    write_ppm(report_directory / "native-kill-board-gone.ppm", gone);
    if (differing_outside(hidden, gone, {cursor}) != 0)
        throw std::runtime_error("kill board check: the board left pixels behind");
    std::cout << "kill board check: " << board.width << 'x' << board.height << " at " << board.x
              << ',' << board.y << " on the " << match_layout_.width << 'x' << match_layout_.height
              << " canvas, shaded by " << (full ? "the card in the full tier" : "the shade table")
              << '\n';
}

} // namespace oa::app
