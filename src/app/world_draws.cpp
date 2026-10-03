// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The battlefield's draws of one frame, drawn band by band.
#include "world_draws.hpp"

#include "oa/present/blit.hpp"
#include "oa/present/display.hpp"
#include "oa/present/raster.hpp"
#include "oa/present/world_renderer/world_overlays.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <utility>

namespace oa::app {

namespace model_render = oa::present::model;

void put_world_pixel(
    const WorldTarget& target, int x, int y, const std::array<uint8_t, 3>& color
) noexcept {
    if (x < target.clip_x || y < target.clip_y || x >= target.clip_x + target.clip_width ||
        y >= target.clip_y + target.clip_height)
        return;
    if (x < 0 || y < 0 || x >= target.width || y >= target.height)
        return;
    if (y < target.first_row || y >= target.end_row)
        return;
    const auto at = (static_cast<std::size_t>(y) * static_cast<std::size_t>(target.width) +
                     static_cast<std::size_t>(x)) *
                    3U;
    target.rgb[at] = color[0];
    target.rgb[at + 1] = color[1];
    target.rgb[at + 2] = color[2];
}

void draw_world_line(
    const WorldTarget& target, int x0, int y0, int x1, int y1, const std::array<uint8_t, 3>& color
) noexcept {
    // Clip the segment to the visible world rectangle before stepping it, so
    // off-map endpoints (e.g. a build ghost under an off-map cursor) cannot
    // produce billion-step walks or overflow the differences below. The band
    // takes no part: the line's pixels are those of the whole frame.
    const auto left = static_cast<int64_t>(std::max(target.clip_x, 0));
    const auto top = static_cast<int64_t>(std::max(target.clip_y, 0));
    const auto right =
        std::min<int64_t>(static_cast<int64_t>(target.clip_x) + target.clip_width, target.width) -
        1;
    const auto bottom =
        std::min<int64_t>(static_cast<int64_t>(target.clip_y) + target.clip_height, target.height) -
        1;
    if (right < left || bottom < top)
        return;
    double ax = x0, ay = y0, bx = x1, by = y1;
    const auto outcode = [&](double x, double y) {
        return (x < left ? 1 : 0) | (x > right ? 2 : 0) | (y < top ? 4 : 0) | (y > bottom ? 8 : 0);
    };
    for (int code_a = outcode(ax, ay), code_b = outcode(bx, by);;) {
        if ((code_a | code_b) == 0)
            break;
        if ((code_a & code_b) != 0)
            return;
        const int code = code_a != 0 ? code_a : code_b;
        double x = 0, y = 0;
        if (code & 8) {
            x = ax + (bx - ax) * (bottom - ay) / (by - ay);
            y = static_cast<double>(bottom);
        } else if (code & 4) {
            x = ax + (bx - ax) * (top - ay) / (by - ay);
            y = static_cast<double>(top);
        } else if (code & 2) {
            y = ay + (by - ay) * (right - ax) / (bx - ax);
            x = static_cast<double>(right);
        } else {
            y = ay + (by - ay) * (left - ax) / (bx - ax);
            x = static_cast<double>(left);
        }
        if (code == code_a) {
            ax = x;
            ay = y;
            code_a = outcode(ax, ay);
        } else {
            bx = x;
            by = y;
            code_b = outcode(bx, by);
        }
    }
    x0 = static_cast<int>(std::lround(ax));
    y0 = static_cast<int>(std::lround(ay));
    x1 = static_cast<int>(std::lround(bx));
    y1 = static_cast<int>(std::lround(by));
    int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    while (true) {
        put_world_pixel(target, x0, y0, color);
        if (x0 == x1 && y0 == y1)
            break;
        const auto twice = error * 2;
        if (twice >= dy) {
            error += dy;
            x0 += sx;
        }
        if (twice <= dx) {
            error += dx;
            y0 += sy;
        }
    }
}

void blit_world_frame(
    const WorldTarget& target,
    const oa::formats::gaf::RenderedFrame& frame,
    int destination_x,
    int destination_y,
    const oa::PaletteBytes& palette,
    float scale
) noexcept {
    if (scale <= 0.0F)
        scale = 1.0F;
    const auto dest_w =
        std::max(1, static_cast<int>(std::lround(static_cast<double>(frame.width) * scale)));
    const auto dest_h =
        std::max(1, static_cast<int>(std::lround(static_cast<double>(frame.height) * scale)));
    // The rows and columns of the drawn rectangle that land in the band, the
    // visible world rectangle and the frame; each one's source row or column
    // is its index times the frame's size over the drawn size, rounded down.
    const auto lowest = [](int64_t a, int64_t b, int64_t c) { return std::max({a, b, c}); };
    const auto highest = [](int64_t a, int64_t b, int64_t c) { return std::min({a, b, c}); };
    const int64_t first_row =
        std::max<int64_t>(0, lowest(target.first_row, target.clip_y, 0) - int64_t{destination_y});
    const int64_t end_row = std::min<int64_t>(
        dest_h,
        highest(target.end_row, int64_t{target.clip_y} + target.clip_height, target.height) -
            int64_t{destination_y}
    );
    const int64_t first_column =
        std::max<int64_t>(0, lowest(target.clip_x, 0, 0) - int64_t{destination_x});
    const int64_t end_column = std::min<int64_t>(
        dest_w,
        highest(int64_t{target.clip_x} + target.clip_width, target.width, target.width) -
            int64_t{destination_x}
    );
    if (first_row >= end_row || first_column >= end_column)
        return;
    const auto source_width = static_cast<std::size_t>(frame.width);
    const auto source_pixels = source_width * frame.height;
    const uint8_t* const coverage = frame.coverage.data();
    const uint8_t* const pixels = frame.pixels.data();
    // A frame whose pixels or coverage are short is read only where they reach.
    const std::size_t readable =
        std::min({frame.coverage.size(), frame.pixels.size(), source_pixels});
    const auto columns = static_cast<std::size_t>(dest_w);
    for (int64_t row = first_row; row < end_row; ++row) {
        const auto source_row =
            static_cast<std::size_t>(row) * frame.height / static_cast<std::size_t>(dest_h);
        const std::size_t row_offset = source_row * source_width;
        uint8_t* out = target.rgb + ((static_cast<std::size_t>(destination_y + row) *
                                          static_cast<std::size_t>(target.width) +
                                      static_cast<std::size_t>(destination_x + first_column)) *
                                     3U);
        // The source column of first_column, kept as a whole part and a
        // remainder over the drawn width while the column steps.
        const std::size_t start = static_cast<std::size_t>(first_column) * source_width;
        std::size_t source_column = start / columns;
        std::size_t remainder = start % columns;
        for (int64_t column = first_column; column < end_column; ++column, out += 3) {
            const std::size_t offset = row_offset + source_column;
            remainder += source_width;
            while (remainder >= columns) {
                remainder -= columns;
                ++source_column;
            }
            if (offset >= readable || coverage[offset] == 0)
                continue;
            const auto pal = static_cast<std::size_t>(pixels[offset]) * 4U;
            out[0] = palette[pal];
            out[1] = palette[pal + 1];
            out[2] = palette[pal + 2];
        }
    }
}

void blit_world_hotspot(
    const WorldTarget& target,
    const oa::formats::gaf::RenderedFrame& frame,
    const oa::present::world_renderer::ScreenPoint& screen,
    const oa::PaletteBytes& palette,
    float scale
) noexcept {
    if (scale <= 0.0F)
        scale = 1.0F;
    const auto x =
        screen.x - static_cast<int>(
                       std::lround(static_cast<double>(frame.origin_x) * static_cast<double>(scale))
                   );
    const auto y =
        screen.y - static_cast<int>(
                       std::lround(static_cast<double>(frame.origin_y) * static_cast<double>(scale))
                   );
    blit_world_frame(target, frame, x, y, palette, scale);
}

void blit_world_blended_hotspot(
    const WorldTarget& target,
    const oa::formats::gaf::RenderedFrame& frame,
    const oa::present::world_renderer::ScreenPoint& screen,
    float scale,
    const model_render::ModelDisplay& display,
    model_render::RgbBridge& bridge,
    model_render::BridgeBand* band
) {
    const auto& context = display.context;
    if ((context.flags & oa::present::display_flag_alpha_table) == 0)
        return;
    if (scale <= 0.0F)
        scale = 1.0F;
    const auto left =
        screen.x - static_cast<int>(std::lround(static_cast<double>(frame.origin_x) * scale));
    const auto top =
        screen.y - static_cast<int>(std::lround(static_cast<double>(frame.origin_y) * scale));
    const auto dest_w =
        std::max(1, static_cast<int>(std::lround(static_cast<double>(frame.width) * scale)));
    const auto dest_h =
        std::max(1, static_cast<int>(std::lround(static_cast<double>(frame.height) * scale)));
    const auto& palette = display.palette;
    // The columns of the drawn rectangle that land in the visible world
    // rectangle and the frame; each one's source column is its index times
    // the frame's width over the drawn width, rounded down, kept as a whole
    // part and a remainder while the column steps.
    const int64_t first_column =
        std::max<int64_t>(0, std::max<int64_t>(target.clip_x, 0) - int64_t{left});
    const int64_t end_column = std::min<int64_t>(
        dest_w,
        std::min<int64_t>(int64_t{target.clip_x} + target.clip_width, target.width) - int64_t{left}
    );
    const auto source_width = static_cast<std::size_t>(frame.width);
    const auto columns = static_cast<std::size_t>(dest_w);
    for (int row = 0; row < dest_h && first_column < end_column; ++row) {
        const int y = top + row;
        if (y < target.clip_y || y >= target.clip_y + target.clip_height || y < 0 ||
            y >= target.height || y < target.first_row || y >= target.end_row)
            continue;
        const auto source_row =
            static_cast<std::size_t>(row) * frame.height / static_cast<std::size_t>(dest_h);
        const std::size_t start = static_cast<std::size_t>(first_column) * source_width;
        std::size_t source_column = start / columns;
        std::size_t remainder = start % columns;
        for (int64_t column = first_column; column < end_column; ++column) {
            const auto x = static_cast<int>(left + column);
            const auto offset = source_row * source_width + source_column;
            remainder += source_width;
            while (remainder >= columns) {
                remainder -= columns;
                ++source_column;
            }
            if (offset >= frame.coverage.size() || frame.coverage[offset] == 0)
                continue;
            auto* pixel =
                target.rgb +
                (static_cast<std::size_t>(y) * target.width + static_cast<std::size_t>(x)) * 3U;
            const uint8_t under =
                band != nullptr
                    ? model_render::bridge_index(bridge, *band, pixel[0], pixel[1], pixel[2])
                    : model_render::bridge_index(bridge, pixel[0], pixel[1], pixel[2]);
            const auto& blended =
                palette
                    .entries[context.alpha_table
                                 [static_cast<std::size_t>(frame.pixels[offset]) * 0x100 + under]];
            pixel[0] = blended.r;
            pixel[1] = blended.g;
            pixel[2] = blended.b;
        }
    }
}

void clear_world_draws(WorldDrawList& list) {
    list.draws.clear();
    list.squares.clear();
    list.sprites.clear();
    list.lines.clear();
    list.models.clear();
    list.stand_ins.clear();
    list.projectiles.clear();
    list.debris.clear();
    list.fragments.clear();
    list.decoded.clear();
    list.decoded_of.clear();
}

const oa::formats::gaf::RenderedFrame*
decoded_frame(WorldDrawList& list, const oa::formats::gaf::Frame& frame) {
    if (const auto found = list.decoded_of.find(&frame); found != list.decoded_of.end())
        return found->second;
    auto rendered = oa::formats::gaf::render_normal(frame);
    const oa::formats::gaf::RenderedFrame* decoded = nullptr;
    if (rendered.ok()) {
        list.decoded.push_back(std::move(*rendered.frame));
        decoded = &list.decoded.back();
    }
    list.decoded_of.emplace(&frame, decoded);
    return decoded;
}

void add_world_draw(WorldDrawList& list, WorldDrawKind kind, std::size_t index) {
    list.draws.push_back({kind, static_cast<uint32_t>(index)});
}

void draw_world_band(
    const WorldDrawList& list,
    const WorldFrameDraw& frame,
    model_render::BridgeBand& band,
    model_render::ModelRenderer& renderer,
    model_render::SupersampleScratch& supersample,
    std::vector<oa::formats::objects3d::FixedVector3>& debris_points
) {
    // The models' display, on the thread drawing the band, for as long as it draws.
    struct DisplayBinding {
        oa::present::DisplayContext* previous{};

        explicit DisplayBinding(oa::present::DisplayContext* bound)
            : previous(oa::present::display_context()) {
            oa::present::bind_display(bound);
        }

        DisplayBinding(const DisplayBinding&) = delete;
        DisplayBinding& operator=(const DisplayBinding&) = delete;

        ~DisplayBinding() { oa::present::bind_display(previous); }
    } binding{&frame.display->context};

    model_render::RgbBridge& bridge = *frame.bridge;
    WorldTarget target = frame.target;
    target.first_row = band.frame_first_row;
    target.end_row = band.frame_end_row;
    oa::Surface* surface = &band.surface;
    // The bridge holds draws not yet written back to the frame: sprites and
    // squares draw straight into the frame, so the bridge's pixels go back
    // to the frame before one draws over them.
    bool bridge_holds_draws = false;
    for (const WorldDraw& draw : list.draws) {
        // A kind the graphics card draws is left to it; the commits stay,
        // since the bridge's draws still go back to the frame in order.
        if ((frame.card_kinds & card_kind_bit(draw.kind)) != 0)
            continue;
        switch (draw.kind) {
        case WorldDrawKind::commit:
            if (bridge_holds_draws)
                model_render::bridge_end(bridge, band);
            bridge_holds_draws = false;
            break;
        case WorldDrawKind::commit_always:
            model_render::bridge_end(bridge, band);
            bridge_holds_draws = false;
            break;
        case WorldDrawKind::pixel_square: {
            const SquareDraw& square = list.squares[draw.index];
            const int32_t top = std::max(square.top, target.first_row);
            const int32_t bottom = std::min(square.bottom, target.end_row);
            for (int32_t y = top; y < bottom; ++y)
                for (int32_t x = square.left; x < square.right; ++x)
                    put_world_pixel(target, x, y, square.color);
            break;
        }
        case WorldDrawKind::sprite: {
            const SpriteDraw& sprite = list.sprites[draw.index];
            blit_world_hotspot(target, *sprite.frame, sprite.screen, *frame.palette, frame.scale);
            break;
        }
        case WorldDrawKind::blended_sprite: {
            const SpriteDraw& sprite = list.sprites[draw.index];
            blit_world_blended_hotspot(
                target, *sprite.frame, sprite.screen, frame.scale, *frame.display, bridge, &band
            );
            break;
        }
        case WorldDrawKind::line: {
            // A thick line is the line drawn again at each offset of a
            // square pen, centred where the thickness allows.
            const LineDraw& line = list.lines[draw.index];
            const int32_t thickness = std::max(frame.line_thickness, 1);
            const int32_t first = -(thickness - 1) / 2;
            for (int32_t down = first; down < first + thickness; ++down)
                for (int32_t across = first; across < first + thickness; ++across)
                    draw_world_line(
                        target,
                        line.x0 + across,
                        line.y0 + down,
                        line.x1 + across,
                        line.y1 + down,
                        line.color
                    );
            break;
        }
        case WorldDrawKind::selection_line: {
            // Each line captures the tiles it crosses before drawing, with
            // its thickness.
            const LineDraw& line = list.lines[draw.index];
            const int32_t thickness = std::max(frame.bridge_line_thickness, 1);
            const int32_t first = -(thickness - 1) / 2;
            const int32_t last = first + thickness - 1;
            model_render::bridge_open(
                bridge,
                band,
                {std::min(line.x0, line.x1) + first,
                 std::min(line.y0, line.y1) + first,
                 std::max(line.x0, line.x1) + last,
                 std::max(line.y0, line.y1) + last}
            );
            for (int32_t down = first; down <= last; ++down)
                for (int32_t across = first; across <= last; ++across)
                    oa::present::draw_clipped_line(
                        surface,
                        line.x0 + across,
                        line.y0 + down,
                        line.x1 + across,
                        line.y1 + down,
                        line.palette_index
                    );
            break;
        }
        case WorldDrawKind::model: {
            const ModelDraw& model = list.models[draw.index];
            model_render::ModelRef drawn = model.model;
            if (model.stand_in >= 0)
                drawn.unit = &list.stand_ins[static_cast<std::size_t>(model.stand_in)];
            model_render::draw_planned_unit(
                renderer, bridge, &band, supersample, drawn, model.plan, bridge_holds_draws
            );
            break;
        }
        case WorldDrawKind::projectile: {
            const ProjectileDraw& shot = list.projectiles[draw.index];
            model_render::bridge_open(bridge, band, shot.region);
            if (shot.shadow)
                oa::present::draw_sprite_blended(
                    surface, frame.projectile_shadow, shot.x, shot.shadow_y
                );
            model_render::draw_projectile_model(
                renderer, surface, shot.position, *shot.object, *shot.prepared, shot.rotation
            );
            if (shot.child != nullptr)
                model_render::draw_projectile_model(
                    renderer,
                    surface,
                    shot.position,
                    *shot.child,
                    *shot.child_prepared,
                    shot.child_rotation
                );
            break;
        }
        case WorldDrawKind::debris: {
            const DebrisDraw& piece = list.debris[draw.index];
            model_render::bridge_open(bridge, band, piece.region);
            model_render::draw_rotated_debris(
                renderer,
                surface,
                frame.debris_view,
                *piece.object,
                *piece.prepared,
                piece.spin,
                piece.origin,
                piece.team,
                debris_points
            );
            model_render::bridge_end(bridge, band);
            break;
        }
        case WorldDrawKind::fragment: {
            const FragmentDraw& fragment = list.fragments[draw.index];
            model_render::bridge_open(bridge, band, fragment.region);
            model_render::draw_shatter_fragment(
                renderer,
                surface,
                fragment.position,
                *fragment.fragment,
                *fragment.primitive,
                fragment.spin
            );
            model_render::bridge_end(bridge, band);
            break;
        }
        }
    }
}

} // namespace oa::app
