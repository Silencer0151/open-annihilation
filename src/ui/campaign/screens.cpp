// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Registers the campaign package with the application screen registry.
#include "oa/ui/campaign/screens.hpp"

#include "oa/formats/fnt.hpp"
#include "oa/ui/frontend_renderer.hpp"
#include "oa/ui/frontend_renderer/gadget_draw.hpp"
#include "oa/ui/frontend_renderer/game_text.hpp"
#include "oa/formats/gaf.hpp"
#include "oa/present/model/mesh_raster.hpp"
#include "oa/present/raster.hpp"
#include "oa/ui/screen_registry.hpp"

#include <array>
#include <cstring>
#include <tuple>
#include <vector>

namespace oa::ui::campaign {
namespace {

constexpr std::size_t kPaletteEntryBytes = 4;
// Game.ui_colors slots a stat bar draws with.
constexpr std::size_t kBoxTopLeftColor = 0x00;
constexpr std::size_t kBoxBottomRightColor = 0x11;
constexpr std::size_t kBoxColor = 0x14;
constexpr std::size_t kWellColor = 0x08;
constexpr std::size_t kFillColor = 0x04;
// Game.ui_colors slots of the outlined "Click to continue." label.
constexpr std::size_t kLabelOutlineColor = 0x00;
constexpr std::size_t kLabelTextColor = 0x0f;
// Bytes of one light-table row, one per palette entry.
constexpr std::size_t kLightRowBytes = 0x100;
// The swatch's texture points stay one texel inside the frame's edges.
constexpr int32_t kSwatchTexelInset = 1;
// The score table is drawn twice over these fills of an 8-bit frame: a pixel
// that comes out the same both times was drawn.
constexpr uint8_t kPassFill[2] = {0x00, 0xff};
// The score table keeps the game's own fonts whatever the Language settings
// say: its names and values are not drawn as game text, and a character a
// font lacks is drawn in the modern fonts, as with them off.
constexpr bool kScoreGameText = false;

struct EndgameOverlay {
    EndgameView view{};
    EndgameHost host{};
    bool input{};                // a click or key waits for the screen
    const char* continue_text{}; // drawn over the glamour picture this frame
    // The rows' names: the label font with every glyph pixel mapped through
    // light-table row kScoreNameLight, as the name labels draw.
    oa::formats::fnt::Font name_font{};
    std::vector<uint8_t> passes[2]{}; // the score table's two 8-bit frames
};

EndgameOverlay g_overlay{};

bool take_input(void*) {
    const bool waiting = g_overlay.input;
    g_overlay.input = false;
    return waiting;
}

void show_continue(void*, const char* text) {
    g_overlay.continue_text = text;
}

bool published(const EndgameOverlay& overlay) {
    return overlay.view.world != nullptr && overlay.view.screen != nullptr;
}

uint32_t overlay_now(const EndgameOverlay& overlay) {
    return overlay.host.now != nullptr ? overlay.host.now(overlay.host.context) : 0;
}

void put_pixel(
    oa::ui::frontend_renderer::Surface& surface, int32_t x, int32_t y, const uint8_t* rgb
) {
    if (x < 0 || y < 0 || x >= static_cast<int32_t>(surface.width) ||
        y >= static_cast<int32_t>(surface.height))
        return;
    const auto offset =
        (static_cast<std::size_t>(y) * surface.width + static_cast<std::size_t>(x)) * 3U;
    if (offset + 2 < surface.rgb.size())
        std::memcpy(surface.rgb.data() + offset, rgb, 3);
}

const uint8_t* palette_rgb(const EndgameOverlay& overlay, uint8_t index) {
    return overlay.view.palette + static_cast<std::size_t>(index) * kPaletteEntryBytes;
}

// No glyphs: text measures and draws nothing without a published font.
const oa::formats::fnt::Font kNoFont{};

const oa::formats::fnt::Font& label_font(const EndgameOverlay& overlay) {
    return overlay.view.label_font != nullptr ? *overlay.view.label_font : kNoFont;
}

// Glyph indices written straight into an 8-bit frame.
void draw_indexed_text(
    oa::Surface& target, const oa::formats::fnt::Font& font, const char* text, int32_t x, int32_t y
) {
    const auto size =
        static_cast<std::size_t>(target.pitch) * static_cast<std::size_t>(target.height);
    const oa::formats::fnt::IndexedSurface pixels{
        static_cast<uint32_t>(target.width),
        static_cast<uint32_t>(target.height),
        static_cast<std::size_t>(target.pitch),
        std::span<uint8_t>(target.pixels, size),
        {}
    };
    (void)oa::formats::fnt::raster_text(pixels, font, text, x, y);
}

// The "PlayerColor%d" hot surface: the player's 32xlogos frame textured over
// the swatch rectangle, its texture points one texel inside the frame. The
// quad leaves out its right column and bottom row, so a swatch covers
// kScoreSwatchWidth - 1 by kScoreSwatchHeight - 1 pixels and the next row's
// swatch starts right below it.
void draw_swatch(oa::Surface& target, const EndgameOverlay& overlay, const ScoreRow& row) {
    const oa::formats::gaf::Frame* frame =
        oa::formats::gaf::frame_at(overlay.view.player_logos, row.color);
    if (frame == nullptr)
        return;
    const auto rendered = oa::formats::gaf::render_normal(*frame);
    if (!rendered.ok() || rendered.frame->width == 0 || rendered.frame->height == 0)
        return;
    const auto& image = *rendered.frame;
    oa::Sprite texture{};
    texture.width = static_cast<uint16_t>(image.width);
    texture.height = static_cast<uint16_t>(image.height);
    texture.key = image.transparency_index;
    texture.encoding = OA_SPRITE_RAW;
    texture.data = const_cast<uint8_t*>(image.pixels.data());
    const int32_t left = row.name_x;
    const int32_t top = row.y;
    const int32_t right = left + kScoreSwatchWidth - 1;
    const int32_t bottom = top + kScoreSwatchHeight - 1;
    const oa::present::PolygonVertex quad[4] = {
        {left, top}, {right, top}, {right, bottom}, {left, bottom}
    };
    const int32_t u = static_cast<int32_t>(image.width) - kSwatchTexelInset;
    const int32_t v = static_cast<int32_t>(image.height) - kSwatchTexelInset;
    const oa::present::model::TexturePoint uv[4] = {
        {kSwatchTexelInset, kSwatchTexelInset},
        {u, kSwatchTexelInset},
        {u, v},
        {kSwatchTexelInset, v}
    };
    oa::present::model::texture_quad(&target, &texture, quad, uv);
}

// The name label, drawn through its light-table row.
void draw_name(
    oa::Surface& target, const EndgameOverlay& overlay, const Game& game, const ScoreRow& row
) {
    ScoreNameDraw name{};
    place_score_name(row, game.scores[row.player].name, label_font(overlay), &name);
    // A name with a character the font lacks goes on the screen's RGB pixels
    // after the table (draw_score_game_text).
    if (oa::ui::frontend_renderer::needs_text_runs(name.text, kScoreGameText))
        return;
    draw_indexed_text(target, overlay.name_font, name.text, name.x, name.y);
}

// The type-13 progress gadget as the gadget engine draws it (inclusive
// rectangles): the raised box, the well, the filled part and the value.
void draw_bar(
    oa::Surface& target,
    const EndgameOverlay& overlay,
    const Game& game,
    const ScoreBar& bar,
    int32_t y
) {
    ScoreBarDraw draw{};
    draw_score_bar(bar, y, label_font(overlay), &draw);
    oa::ui::frontend_renderer::fill_box_raised(
        &target,
        oa::Rect32{draw.left, draw.top, draw.right, draw.bottom},
        game.ui_colors[kBoxTopLeftColor],
        game.ui_colors[kBoxBottomRightColor],
        game.ui_colors[kBoxColor]
    );
    const auto region = [&](oa::Rect32 rect, uint8_t color) {
        if (oa::present::clip_rect(target, rect))
            oa::present::fill_rect(target, rect, color);
    };
    region(
        oa::Rect32{draw.inner_left, draw.inner_top, draw.inner_right, draw.inner_bottom},
        game.ui_colors[kWellColor]
    );
    region(
        oa::Rect32{draw.inner_left, draw.inner_top, draw.fill_right, draw.inner_bottom},
        game.ui_colors[kFillColor]
    );
    if (oa::ui::frontend_renderer::needs_text_runs(draw.label, kScoreGameText))
        return;
    draw_indexed_text(target, label_font(overlay), draw.label, draw.label_x, draw.label_y);
}

// The score table on an 8-bit frame, each row's gadgets in the order the
// screen adds them: the swatch, the name, then the shown stat bars.
void draw_score_table(oa::Surface& target, const EndgameOverlay& overlay, const Game& game) {
    const ScoreLayout& layout = overlay.view.screen->layout;
    for (uint32_t r = 0; r < layout.row_count; ++r) {
        const ScoreRow& row = layout.rows[r];
        draw_swatch(target, overlay, row);
        draw_name(target, overlay, game, row);
        for (const ScoreBar& bar : row.bars)
            if (bar.active)
                draw_bar(target, overlay, game, bar, row.y);
    }
}

/// Gives the colour of a font's glyphs: the first drawn pixel of its 'H'
/// through the palette.
std::array<uint8_t, 3>
font_color(const EndgameOverlay& overlay, const oa::formats::fnt::Font& font) {
    std::array<uint8_t, 3> color{};
    uint8_t index = oa::formats::fnt::foreground_index;
    if (const auto& glyph = font.glyphs['H'])
        for (std::size_t at = 0; at < glyph->coverage.size() && at < glyph->pixels.size(); ++at)
            if (glyph->coverage[at] != 0) {
                index = glyph->pixels[at];
                break;
            }
    std::memcpy(color.data(), palette_rgb(overlay, index), color.size());
    return color;
}

// The names and bar values with a character their font lacks, on the
// screen's RGB pixels, that character in the modern fonts and the rest in the
// font: a name centred on its label and cut at the label's right edge, a value
// centred on its bar.
void draw_score_game_text(
    oa::ui::frontend_renderer::Surface& surface, const EndgameOverlay& overlay, const Game& game
) {
    namespace renderer = oa::ui::frontend_renderer;
    oa::PaletteBytes palette{};
    std::memcpy(palette.data(), overlay.view.palette, palette.size());
    const auto& names = overlay.name_font;
    const auto& values = label_font(overlay);
    const ScoreLayout& layout = overlay.view.screen->layout;
    const renderer::TextClip screen{
        0, 0, static_cast<int32_t>(surface.width) - 1, static_cast<int32_t>(surface.height) - 1
    };
    for (uint32_t r = 0; r < layout.row_count; ++r) {
        const ScoreRow& row = layout.rows[r];
        const char* name = game.scores[row.player].name;
        if (renderer::needs_text_runs(name, kScoreGameText)) {
            const int32_t width = renderer::measure_fnt_game_text(names, name, kScoreGameText);
            ScoreNameDraw placed{};
            place_score_name(row, name, values, &placed);
            renderer::TextClip label = screen;
            label.left = row.name_x;
            label.right = row.name_x + kScoreNameWidth - 1;
            std::ignore = renderer::draw_fnt_game_text(
                surface,
                names,
                name,
                std::max(row.name_x, kScoreNameWidth / 2 + row.name_x - width / 2),
                placed.y,
                font_color(overlay, names),
                palette,
                label,
                kScoreGameText
            );
        }
        for (const ScoreBar& bar : row.bars) {
            if (!bar.active)
                continue;
            ScoreBarDraw draw{};
            draw_score_bar(bar, row.y, values, &draw);
            if (!renderer::needs_text_runs(draw.label, kScoreGameText))
                continue;
            const int32_t width =
                renderer::measure_fnt_game_text(values, draw.label, kScoreGameText);
            std::ignore = renderer::draw_fnt_game_text(
                surface,
                values,
                draw.label,
                kScoreBarWidth / 2 - width / 2 + bar.x,
                draw.label_y,
                font_color(overlay, values),
                palette,
                screen,
                kScoreGameText
            );
        }
    }
}

// The score table over the frame through the screen's palette.
void draw_score_rows(
    oa::ui::frontend_renderer::Surface& surface, EndgameOverlay& overlay, const Game& game
) {
    const auto count = static_cast<std::size_t>(surface.width) * surface.height;
    for (std::size_t pass = 0; pass < 2; ++pass) {
        std::vector<uint8_t>& pixels = overlay.passes[pass];
        pixels.assign(count, kPassFill[pass]);
        oa::Surface frame{};
        frame.width = static_cast<int32_t>(surface.width);
        frame.height = static_cast<int32_t>(surface.height);
        frame.pitch = frame.width;
        frame.pixels = pixels.data();
        frame.clip = {0, 0, frame.width - 1, frame.height - 1};
        frame.flags = OA_SURFACE_FLAG_MEMORY;
        draw_score_table(frame, overlay, game);
    }
    for (std::size_t i = 0; i < count && i * 3U + 2 < surface.rgb.size(); ++i)
        if (overlay.passes[0][i] == overlay.passes[1][i])
            std::memcpy(surface.rgb.data() + i * 3U, palette_rgb(overlay, overlay.passes[0][i]), 3);
    draw_score_game_text(surface, overlay, game);
}

// Glyphs of every label on the screen, composed onto the frame in one pass.
struct TextLayer {
    std::vector<uint8_t> indices{};
    std::vector<uint8_t> coverage{};
    oa::formats::fnt::IndexedSurface target{};
};

void begin_text(TextLayer& layer, const oa::ui::frontend_renderer::Surface& surface) {
    const auto count = static_cast<std::size_t>(surface.width) * surface.height;
    layer.indices.assign(count, 0);
    layer.coverage.assign(count, 0);
    layer.target = {surface.width, surface.height, surface.width, layer.indices, layer.coverage};
}

void add_text(
    TextLayer& layer, const EndgameOverlay& overlay, const char* text, int32_t x, int32_t y
) {
    if (overlay.view.font != nullptr)
        (void)oa::formats::fnt::raster_text(layer.target, *overlay.view.font, text, x, y);
}

int32_t text_width(const EndgameOverlay& overlay, const char* text) {
    return overlay.view.font != nullptr
               ? static_cast<int32_t>(oa::formats::fnt::measure_text(*overlay.view.font, text))
               : 0;
}

int32_t text_height(const EndgameOverlay& overlay) {
    return overlay.view.font != nullptr ? oa::formats::fnt::line_height(*overlay.view.font) : 0;
}

// Presses feed the stat-bar screen until its buttons take over; releases are
// swallowed with them so no button fires early.
int endgame_event(oa::app::ScreenContext* ctx, void* state) {
    auto* overlay = static_cast<EndgameOverlay*>(state);
    if (!published(*overlay) || ctx->input == nullptr ||
        overlay->view.world->game.endgame_state >= OA_ENDGAME_PANEL)
        return 0;
    switch (ctx->input->kind) {
    case oa::app::ScreenInputKind::pointer_down:
    case oa::app::ScreenInputKind::key_down:
        overlay->input = true;
        return 1;
    case oa::app::ScreenInputKind::pointer_up:
    case oa::app::ScreenInputKind::key_up:
        return 1;
    default:
        return 0;
    }
}

void endgame_frame(oa::app::ScreenContext*, void* state) {
    auto* overlay = static_cast<EndgameOverlay*>(state);
    if (!published(*overlay))
        return;
    overlay->continue_text = nullptr;
    advance_score_bars(&overlay->view.screen->layout, overlay_now(*overlay));
    endgame_tick(
        *overlay->view.world, overlay->view.campaign, *overlay->view.screen, overlay->host
    );
}

// The glamour picture over the whole screen through the fade's current
// palette, as the game copies it to the screen and fades the palette.
void draw_glamour(oa::ui::frontend_renderer::Surface& surface, const EndgameOverlay& overlay) {
    const EndgamePicture& picture = *overlay.view.glamour;
    const uint8_t* palette = overlay.view.screen->fade.current;
    const uint32_t width = picture.width < surface.width ? picture.width : surface.width;
    const uint32_t height = picture.height < surface.height ? picture.height : surface.height;
    for (uint32_t y = 0; y < height; ++y)
        for (uint32_t x = 0; x < width; ++x) {
            const uint8_t index = picture.pixels[static_cast<std::size_t>(y) * picture.width + x];
            put_pixel(
                surface,
                static_cast<int32_t>(x),
                static_cast<int32_t>(y),
                palette + static_cast<std::size_t>(index) * kPaletteEntryBytes
            );
        }
}

// Every glyph pixel of the layer in one colour of the fade's current palette.
void compose_text_color(
    const TextLayer& layer,
    oa::ui::frontend_renderer::Surface& surface,
    const EndgameOverlay& overlay,
    uint8_t color
) {
    const uint8_t* rgb =
        overlay.view.screen->fade.current + static_cast<std::size_t>(color) * kPaletteEntryBytes;
    for (std::size_t i = 0; i < layer.coverage.size(); ++i)
        if (layer.coverage[i] != 0)
            std::memcpy(surface.rgb.data() + i * 3U, rgb, 3);
}

// Outlined text centred across the screen: four one-pixel offsets in the
// outline colour, then the text.
void draw_continue_label(
    oa::ui::frontend_renderer::Surface& surface, const EndgameOverlay& overlay, const Game& game
) {
    const char* text = overlay.continue_text;
    const int32_t x = (static_cast<int32_t>(surface.width) - text_width(overlay, text)) >> 1;
    const int32_t y =
        static_cast<int32_t>(surface.height) - kContinueTextRise + text_height(overlay);
    TextLayer outline{};
    begin_text(outline, surface);
    add_text(outline, overlay, text, x - 1, y);
    add_text(outline, overlay, text, x + 1, y);
    add_text(outline, overlay, text, x, y - 1);
    add_text(outline, overlay, text, x, y + 1);
    compose_text_color(outline, surface, overlay, game.ui_colors[kLabelOutlineColor]);
    TextLayer label{};
    begin_text(label, surface);
    add_text(label, overlay, text, x, y);
    compose_text_color(label, surface, overlay, game.ui_colors[kLabelTextColor]);
}

// The glamour picture while it shows, otherwise the score table.
void draw_endgame(oa::app::ScreenContext* ctx, void* state) {
    auto* overlay = static_cast<EndgameOverlay*>(state);
    if (!published(*overlay) || ctx->surface == nullptr || overlay->view.palette == nullptr)
        return;
    auto& surface = *ctx->surface;
    const Game& game = overlay->view.world->game;
    if (game.endgame_state == OA_ENDGAME_GLAMOUR && overlay->view.glamour != nullptr &&
        overlay->view.glamour->pixels != nullptr) {
        draw_glamour(surface, *overlay);
        if (overlay->continue_text != nullptr)
            draw_continue_label(surface, *overlay, game);
        return;
    }
    draw_score_rows(surface, *overlay, game);
}

} // namespace

void publish_endgame(const EndgameView& view) {
    g_overlay = {};
    if (view.world == nullptr || view.screen == nullptr || view.host == nullptr)
        return;
    g_overlay.view = view;
    g_overlay.host = *view.host;
    g_overlay.host.input = take_input;
    g_overlay.host.draw_continue = show_continue;
    if (view.label_font == nullptr)
        return;
    g_overlay.name_font = *view.label_font;
    if (view.light_table == nullptr)
        return;
    const uint8_t* light = view.light_table + kScoreNameLight * kLightRowBytes;
    for (auto& glyph : g_overlay.name_font.glyphs)
        if (glyph)
            for (uint8_t& pixel : glyph->pixels)
                pixel = light[pixel];
}

} // namespace oa::ui::campaign

namespace oa::app {

void register_campaign_screens(ScreenRegistry* registry) {
    OverlayDesc scores{};
    scores.name = "end-of-game score screen";
    scores.screen = oa::ui::campaign::kScoreOverlayScreen;
    scores.z = 10;
    scores.event = oa::ui::campaign::endgame_event;
    scores.tick = oa::ui::campaign::endgame_frame;
    scores.draw = oa::ui::campaign::draw_endgame;
    scores.state = &oa::ui::campaign::g_overlay;
    overlay_register(registry, &scores);
}

} // namespace oa::app
