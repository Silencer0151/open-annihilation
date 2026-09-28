// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// World-space drawing: build ghosts, lines and GAF blits.
#include "oa/app/runtime.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace oa::app {

std::array<uint8_t, 3> Runtime::palette_rgb(uint8_t index) const {
    const auto pal = static_cast<std::size_t>(index) * 4U;
    if (pal + 2 >= match_palette_.size())
        return index == kPaletteGreen ? std::array<uint8_t, 3>{0, 255, 0}
                                      : std::array<uint8_t, 3>{255, 0, 0};
    return {match_palette_[pal], match_palette_[pal + 1], match_palette_[pal + 2]};
}

std::optional<Runtime::PendingBuildSite>
Runtime::pending_build_site(oa::sim::ground_orders::Point world) const {
    if (!match_ || pending_build_type_ == 0 || pending_build_type_ >= spawn_types_.size())
        return std::nullopt;
    auto fx = spawn_types_[pending_build_type_].footprint_x;
    auto fz = spawn_types_[pending_build_type_].footprint_z;
    if (fx <= 0)
        fx = 2;
    if (fz <= 0)
        fz = 2;
    oa::sim::match_runtime::snap_build_position(world, fx, fz);
    const auto fx_u = static_cast<uint32_t>(fx);
    const auto fz_u = static_cast<uint32_t>(fz);
    const auto cell_x =
        static_cast<int32_t>((static_cast<uint32_t>(world[0]) - fx_u * 0x80000u + 0x80000u) >> 20);
    const auto cell_z =
        static_cast<int32_t>((static_cast<uint32_t>(world[2]) - fz_u * 0x80000u + 0x80000u) >> 20);
    const auto site =
        match_->building_site(pending_build_type_, cell_x, cell_z, 0, match_local_player_);
    const auto height =
        site ? *site : match_->footprint_height(pending_build_type_, cell_x, cell_z);
    world[1] = static_cast<int32_t>(static_cast<uint32_t>(height) << 16);
    return PendingBuildSite{world, cell_x, cell_z, fx, fz, site.has_value()};
}

std::optional<Runtime::PendingBuildSite> Runtime::build_site_under(float x, float y) const {
    if (!selected_tnt_)
        return std::nullopt;
    const auto viewport = live_viewport(
        static_cast<uint32_t>(match_camera_x_), static_cast<uint32_t>(match_camera_z_)
    );
    const auto screen_map = oa::present::world_renderer::screen_to_map_pixel(
        viewport, {static_cast<int32_t>(x), static_cast<int32_t>(y)}
    );
    if (!screen_map)
        return std::nullopt;
    const oa::sim::unit_movement::Terrain terrain(*selected_tnt_);
    const auto target = oa::sim::gameplay_input::terrain_intersection(
        terrain,
        static_cast<int32_t>(screen_map->x),
        static_cast<int32_t>(screen_map->y),
        static_cast<int32_t>(selected_tnt_->attribute_width * 16U),
        static_cast<int32_t>(selected_tnt_->attribute_height * 16U)
    );
    return pending_build_site({target.x, target.y, target.z});
}

void Runtime::draw_build_ghost(
    oa::present::world_renderer::Surface& destination,
    const oa::present::world_renderer::BattlefieldViewport& viewport
) {
    if (match_command_ != MatchCommand::build || pending_build_type_ == 0 || !selected_tnt_ ||
        pending_build_type_ >= spawn_types_.size())
        return;
    const auto site = build_site_under(match_pointer_x_, match_pointer_y_);
    if (!site)
        return;
    const auto height = static_cast<uint32_t>(site->world[1]);
    const auto top_left = project_match_point(
        viewport,
        {static_cast<uint32_t>(site->cell_x * 16 << 16),
         height,
         static_cast<uint32_t>(site->cell_z * 16 << 16)}
    );
    const auto bottom_right = project_match_point(
        viewport,
        {static_cast<uint32_t>((site->cell_x + site->footprint_x) * 16 << 16),
         height,
         static_cast<uint32_t>((site->cell_z + site->footprint_z) * 16 << 16)}
    );
    ensure_ui_colors();
    const auto color = ui_color_rgb(site->legal ? kBuildSiteClearColor : kBuildSiteRefusedColor);
    for (int inset = 0; inset < 2; ++inset) {
        const auto left = top_left.x + inset;
        const auto top = top_left.y + inset;
        const auto right = bottom_right.x - inset;
        const auto bottom = bottom_right.y - inset;
        draw_match_line(destination, left, top, right, top, color);
        draw_match_line(destination, right, top, right, bottom, color);
        draw_match_line(destination, right, bottom, left, bottom, color);
        draw_match_line(destination, left, bottom, left, top, color);
    }
}

oa::present::world_renderer::ScreenPoint Runtime::project_match_point(
    const oa::present::world_renderer::BattlefieldViewport& viewport,
    const std::array<uint32_t, 3>& position
) const {
    const auto map_x = static_cast<uint32_t>(std::bit_cast<int32_t>(position[0]) >> 16);
    const auto map_z = static_cast<uint32_t>(std::bit_cast<int32_t>(position[2]) >> 16);
    const auto height = std::bit_cast<int32_t>(position[1]) >> 16;
    auto screen = oa::present::world_renderer::map_pixel_to_screen(viewport, {map_x, map_z});
    const auto scale = viewport.scale == 0.0F ? 1.0F : viewport.scale;
    screen.y -= static_cast<int32_t>(
        std::lround(static_cast<double>(height) * 0.5 * static_cast<double>(scale))
    );
    return screen;
}

void Runtime::put_match_pixel(
    oa::present::world_renderer::Surface& destination,
    int x,
    int y,
    const std::array<uint8_t, 3>& color
) {
    if (x < world_pixel_clip_.x || y < world_pixel_clip_.y ||
        x >= world_pixel_clip_.x + world_pixel_clip_.w ||
        y >= world_pixel_clip_.y + world_pixel_clip_.h)
        return;
    if (x < 0 || y < 0 || x >= static_cast<int>(destination.width) ||
        y >= static_cast<int>(destination.height))
        return;
    const auto di =
        (static_cast<std::size_t>(y) * destination.width + static_cast<std::size_t>(x)) * 3U;
    destination.rgb[di] = color[0];
    destination.rgb[di + 1] = color[1];
    destination.rgb[di + 2] = color[2];
}

void Runtime::draw_match_line(
    oa::present::world_renderer::Surface& destination,
    int x0,
    int y0,
    int x1,
    int y1,
    const std::array<uint8_t, 3>& color
) {
    // Clip the segment to the visible world rectangle before stepping it, so
    // off-map endpoints (e.g. a build ghost under an off-map cursor) cannot
    // produce billion-step walks or overflow the differences below.
    const auto left = static_cast<int64_t>(std::max(world_pixel_clip_.x, 0));
    const auto top = static_cast<int64_t>(std::max(world_pixel_clip_.y, 0));
    const auto right =
        std::min<int64_t>(
            static_cast<int64_t>(world_pixel_clip_.x) + world_pixel_clip_.w, destination.width
        ) -
        1;
    const auto bottom =
        std::min<int64_t>(
            static_cast<int64_t>(world_pixel_clip_.y) + world_pixel_clip_.h, destination.height
        ) -
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
        put_match_pixel(destination, x0, y0, color);
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

void Runtime::blit_gaf_on_world(
    oa::present::world_renderer::Surface& destination,
    const oa::formats::gaf::RenderedFrame& frame,
    int destination_x,
    int destination_y,
    const oa::PaletteBytes& palette,
    float scale
) {
    if (scale <= 0.0F)
        scale = 1.0F;
    const auto dest_w =
        std::max(1, static_cast<int>(std::lround(static_cast<double>(frame.width) * scale)));
    const auto dest_h =
        std::max(1, static_cast<int>(std::lround(static_cast<double>(frame.height) * scale)));
    for (int row = 0; row < dest_h; ++row) {
        const auto source_row =
            static_cast<std::size_t>(row) * frame.height / static_cast<std::size_t>(dest_h);
        for (int column = 0; column < dest_w; ++column) {
            const auto source_column =
                static_cast<std::size_t>(column) * frame.width / static_cast<std::size_t>(dest_w);
            const auto offset = source_row * frame.width + source_column;
            if (offset >= frame.coverage.size() || frame.coverage[offset] == 0)
                continue;
            const int x = destination_x + column;
            const int y = destination_y + row;
            const auto pal = static_cast<std::size_t>(frame.pixels[offset]) * 4U;
            if (pal + 2 >= palette.size())
                continue;
            put_match_pixel(destination, x, y, {palette[pal], palette[pal + 1], palette[pal + 2]});
        }
    }
}

void Runtime::blit_gaf_hotspot(
    oa::present::world_renderer::Surface& destination,
    const oa::formats::gaf::RenderedFrame& frame,
    const oa::present::world_renderer::ScreenPoint& screen,
    const oa::PaletteBytes& palette,
    float scale
) {
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
    blit_gaf_on_world(destination, frame, x, y, palette, scale);
}

std::array<uint8_t, 3> Runtime::ui_color_rgb(uint8_t index) const {
    return palette_rgb(ui_colors_[index]);
}

namespace {

// Weapon render types dispatched by the projectile pass.
enum class ProjectileRender : uint8_t {
    laser = 0,
    plasma = 4,
    flame = 5,
    lightning = 7,
};

// FX.GAF plasma sprites selected by the weapon colour for render type 4.
constexpr std::array<std::string_view, 5> kPlasmaSprites{
    "cannonshell", "plasmasm", "plasmamd", "ultrashell", "plasmasm"
};
constexpr uint8_t kNoPlasmaSprite = 0xff;
// Lightning is broken into segments of this many world units, each end
// jittered by up to +-5 units, drawn twice.
constexpr int64_t kLightningSegment = 5;
constexpr int32_t kLightningJitter = 5;
constexpr int kLightningPasses = 2;

std::array<uint32_t, 3> fixed_point(int64_t x, int64_t y, int64_t z) {
    return {static_cast<uint32_t>(x), static_cast<uint32_t>(y), static_cast<uint32_t>(z)};
}

} // namespace

void Runtime::draw_match_projectiles(
    oa::present::world_renderer::Surface& destination,
    const oa::present::world_renderer::BattlefieldViewport& viewport
) {
    if (!match_)
        return;
    ensure_ui_colors();
    const uint32_t now = match_->simulation().tick;
    uint32_t jitter_seed = now * 0x343fdU + 0x269ec3U;
    const auto jitter = [&jitter_seed]() {
        jitter_seed = jitter_seed * 0x343fdU + 0x269ec3U;
        const auto sample = static_cast<int32_t>((jitter_seed >> 16) & 0x7fffU);
        return sample * (2 * kLightningJitter + 1) / 0x8000 - kLightningJitter;
    };
    for (const auto& shot : match_->projectiles()) {
        const auto* weapon = match_->projectile_weapon(shot);
        if (weapon == nullptr || shot.burst_remaining != 0)
            continue;
        const auto shot_position = oa::sim::match_runtime::fixed_words(shot.position);
        const auto shot_origin = oa::sim::match_runtime::fixed_words(shot.origin);
        try {
            if (!match_->point_visible(static_cast<uint8_t>(match_view_player()), shot_position))
                continue;
        } catch (const std::exception&) {
            continue;
        }
        switch (static_cast<ProjectileRender>(weapon->rendertype)) {
        case ProjectileRender::laser: {
            auto head = project_match_point(viewport, shot_position);
            auto tail = project_match_point(viewport, shot_origin);
            if (weapon->color2 != 0) {
                int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
                if (std::abs(head.y - tail.y) < std::abs(head.x - tail.x)) {
                    if (tail.x < head.x)
                        std::swap(head, tail);
                    x0 = head.x, y0 = head.y - 1, x1 = tail.x, y1 = tail.y - 1;
                } else {
                    if (tail.y < head.y)
                        std::swap(head, tail);
                    x0 = head.x - 1, y0 = head.y, x1 = tail.x + 1, y1 = tail.y;
                }
                draw_match_line(destination, x0, y0, x1, y1, ui_color_rgb(weapon->color2));
            }
            draw_match_line(
                destination, head.x, head.y, tail.x, tail.y, ui_color_rgb(weapon->color)
            );
            break;
        }
        case ProjectileRender::plasma: {
            if (weapon->color == kNoPlasmaSprite || weapon->color >= kPlasmaSprites.size())
                break;
            const auto* sequence = gaf_sequence(match_fx_, kPlasmaSprites[weapon->color]);
            if (sequence == nullptr || sequence->frames.empty())
                break;
            const auto frame = (now - shot.burst_tick) % sequence->frames.size();
            const auto rendered = oa::formats::gaf::render_normal(sequence->frames[frame]);
            if (rendered.ok())
                blit_gaf_hotspot(
                    destination,
                    *rendered.frame,
                    project_match_point(viewport, shot_position),
                    match_palette_,
                    viewport.scale
                );
            break;
        }
        case ProjectileRender::flame: {
            const auto* sequence = gaf_sequence(match_fx_, "flamestream");
            if (sequence == nullptr || sequence->frames.empty() || weapon->weapontimer_ticks == 0)
                break;
            // The flame grows through the sequence as its lifetime runs out.
            const auto count = static_cast<int64_t>(sequence->frames.size());
            const auto remaining = static_cast<int64_t>(shot.lifetime_tick) - now;
            const auto frame = count - remaining * count / weapon->weapontimer_ticks;
            if (frame < 0 || frame >= count)
                break;
            const auto rendered =
                oa::formats::gaf::render_normal(sequence->frames[static_cast<std::size_t>(frame)]);
            if (rendered.ok())
                blit_gaf_hotspot(
                    destination,
                    *rendered.frame,
                    project_match_point(viewport, shot_position),
                    match_palette_,
                    viewport.scale
                );
            break;
        }
        case ProjectileRender::lightning: {
            std::array<int64_t, 3> delta{};
            for (std::size_t axis = 0; axis < 3; ++axis)
                delta[axis] = static_cast<int64_t>(static_cast<int32_t>(shot_position[axis])) -
                              static_cast<int32_t>(shot_origin[axis]);
            const auto length = std::sqrt(
                static_cast<double>(delta[0]) * static_cast<double>(delta[0]) +
                static_cast<double>(delta[1]) * static_cast<double>(delta[1]) +
                static_cast<double>(delta[2]) * static_cast<double>(delta[2])
            );
            const auto segments = static_cast<int64_t>(length / 65536.0) / kLightningSegment;
            if (segments <= 0)
                break;
            const auto color = ui_color_rgb(weapon->color);
            for (int pass = 0; pass < kLightningPasses; ++pass) {
                std::array<int64_t, 3> previous{
                    static_cast<int32_t>(shot_origin[0]),
                    static_cast<int32_t>(shot_origin[1]),
                    static_cast<int32_t>(shot_origin[2])
                };
                for (int64_t step = 1; step <= segments; ++step) {
                    std::array<int64_t, 3> point{};
                    for (std::size_t axis = 0; axis < 3; ++axis)
                        point[axis] = static_cast<int32_t>(shot_origin[axis]) +
                                      delta[axis] * step / segments +
                                      static_cast<int64_t>(jitter()) * 65536;
                    const auto from = project_match_point(
                        viewport, fixed_point(previous[0], previous[1], previous[2])
                    );
                    const auto to =
                        project_match_point(viewport, fixed_point(point[0], point[1], point[2]));
                    draw_match_line(destination, from.x, from.y, to.x, to.y, color);
                    previous = point;
                }
            }
            break;
        }
        default:
            break;
        }
    }
}

} // namespace oa::app
