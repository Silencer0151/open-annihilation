// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/present/world_renderer/world_camera.hpp"
#include "oa/present/world_renderer/world_radar.hpp"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

namespace wr = oa::present::world_renderer;

namespace {

// 128x96-cell map (2048x1536 map pixels) seen through the game's
// 512x416 battlefield with a 126x94 radar picture at (4, 20).
std::unique_ptr<oa::Game> make_game() {
    auto game = std::make_unique<oa::Game>();
    std::memset(game.get(), 0, sizeof(oa::Game));
    game->map_pixel_width = 2048;
    game->map_pixel_height = 1536;
    game->view_cells_width = 32;
    game->view_cells_height = 26;
    game->map_width = 128;
    game->map_height = 96;
    game->viewport_width = 512;
    game->viewport_height = 416;
    game->radar_width = 126;
    game->radar_height = 94;
    game->radar_offset_x = 4;
    game->radar_offset_y = 20;
    return game;
}

int32_t cam_x(const oa::Game& g) {
    return static_cast<int32_t>(g.camera_x);
}

int32_t cam_y(const oa::Game& g) {
    return static_cast<int32_t>(g.camera_y);
}

/// Stores the platform pointer's six state words in Game.pointer_state.
///
/// @param[out] g game block receiving the words
/// @param words x, y, buttons and the rest, as the platform reports them
void set_pointer_state(oa::Game& g, const int32_t (&words)[wr::pointer_state_words]) {
    for (int32_t word = 0; word < wr::pointer_state_words; ++word)
        g.pointer_state[word] = static_cast<uint32_t>(words[word]);
}

struct Sequence {
    std::vector<int32_t> values;
    std::size_t next = 0;
};

int32_t next_value(void* user) {
    auto* s = static_cast<Sequence*>(user);
    return s->values[s->next++ % s->values.size()];
}

void test_clamp_and_radar_rect() {
    auto game = make_game();
    wr::camera_set_position(*game, -40, 5000, 0);
    assert(cam_x(*game) == 0);
    assert(cam_y(*game) == 1536 - 416);
    assert(game->camera_target_x == 0);
    assert(game->camera_target_y == 1120);
    assert((game->radar_blink_flags & wr::radar_flag_redraw) != 0);
    const oa::Rect32 rect = game->radar_view_rect;
    assert(rect.x1 == 4);
    assert(rect.y1 == 94 * 1120 / 1536 + 20);
    assert(rect.x2 == 126 * 32 * 16 / 2048 - 1 + 4);
    assert(rect.y2 == 94 * 26 * 16 / 1536 - 1 + rect.y1);

    game->visibility_flags = 0xff;
    wr::camera_set_position(*game, 3000, -7, 1);
    assert(game->camera_target_x == 2048 - 512);
    assert(game->camera_target_y == 0);
    assert(cam_y(*game) == 1120);
    assert(game->visibility_flags == 0xf7);
}

void test_glide() {
    auto game = make_game();
    wr::camera_set_position(*game, 0, 0, 0);
    wr::camera_set_position(*game, 1000, 11, 1);
    wr::camera_tick(*game, {}, {});
    assert(cam_x(*game) == 320);
    assert(cam_y(*game) == 5); // 0 - (-11 / 2)
    wr::camera_tick(*game, {}, {});
    wr::camera_tick(*game, {}, {});
    assert(cam_x(*game) == 960);
    wr::camera_tick(*game, {}, {});
    assert(cam_x(*game) == 980);
    wr::camera_set_position(*game, 100, 8, 1);
    wr::camera_tick(*game, {}, {});
    assert(cam_x(*game) == 660);
}

void test_follow() {
    auto game = make_game();
    oa::Unit unit{};
    unit.position = {1200 << 16, 40 << 16, 900 << 16};
    unit.flags = OA_UNIT_FLAG_LIVE;
    game->follow_unit = 1;
    wr::camera_tick(*game, {&unit, nullptr}, {});
    // Target = (1200 - 256, 900 - 20 - 208); first glide step caps at 320.
    assert(game->camera_target_x == 944);
    assert(game->camera_target_y == 672);
    assert(cam_x(*game) == 320);
    assert(cam_y(*game) == 320);

    unit.flags = 0;
    game->follow_point_ticks = 0;
    game->follow_target = 0;
    wr::camera_tick(*game, {&unit, nullptr}, {});
    assert(game->follow_unit == 0);

    const oa::FixedVec3 point{600 << 16, 0, 700 << 16};
    game->follow_point = point;
    game->follow_point_ticks = 2;
    wr::camera_tick(*game, {}, {});
    assert(game->follow_point_ticks == 1);
    assert(game->camera_target_x == 344);
    assert(game->camera_target_y == 492);

    // camera_stop_follow zeroes follow_point_ticks, follow_unit and
    // follow_target, and leaves the camera slots and the follow point alone.
    game->follow_unit = 5;
    game->follow_target = 6;
    game->camera_slot_x[0] = 7;
    wr::camera_stop_follow(*game);
    assert(game->follow_point_ticks == 0);
    assert(game->follow_unit == 0);
    assert(game->follow_target == 0);
    assert(game->camera_slot_x[0] == 7);
    const oa::FixedVec3 kept = game->follow_point;
    assert(kept.x == point.x && kept.y == point.y && kept.z == point.z);
}

void test_shake() {
    auto game = make_game();
    wr::camera_set_position(*game, 500, 500, 0);
    game->camera_flags = wr::camera_flag_shake;
    game->shake_duration = 10;
    game->shake_remaining = 5;
    game->shake_amplitude_x = 40;
    game->shake_amplitude_y = 21;
    Sequence rng{{0x7fff, 0}};
    wr::camera_shake(*game, {&rng, next_value});
    // span_x = 20: 0x7fff*20/0x8000 = 19 -> +9; span_y = 10: 0 - 5 -> -5.
    assert(cam_x(*game) == 509);
    assert(cam_y(*game) == 495);
    assert(game->shake_remaining == 4);
    game->shake_remaining = 0;
    wr::camera_shake(*game, {&rng, next_value});
    assert(game->camera_flags == 0);
    assert(rng.next == 2);
}

void test_slots_and_start() {
    auto game = make_game();
    wr::camera_set_position(*game, 300, 200, 0);
    wr::camera_store_slot(*game, 2);
    assert(game->camera_slot_valid[2] == 1);
    wr::camera_set_position(*game, 0, 0, 0);
    game->follow_unit = 9;
    wr::camera_recall_slot(*game, 2);
    assert(cam_x(*game) == 300 && cam_y(*game) == 200);
    assert(game->follow_unit == 0);

    const wr::StartEntry entries[] = {{1, 1, 50, 60}, {0, 0, 70, 80}, {1, 0, 900, 800}};
    wr::camera_to_start_entry(*game, entries, 3);
    assert(cam_x(*game) == 900 - 256 && cam_y(*game) == 800 - 208);
    wr::camera_to_start_entry(*game, entries, 2);
    assert(cam_x(*game) == 644);
}

struct CursorLog {
    int32_t x = 0, y = 0, draws = 0, polls = 0;
};

void test_mouse_look() {
    auto game = make_game();
    wr::camera_set_position(*game, 320, 160, 0);
    CursorLog log;
    wr::CursorSink sink;
    sink.user = &log;
    sink.poll = [](void* u, oa::Game&) { ++static_cast<CursorLog*>(u)->polls; };
    sink.screen_width = [](void*) { return 640; };
    sink.screen_height = [](void*) { return 480; };
    sink.set_position = [](void* u, int32_t x, int32_t y) {
        static_cast<CursorLog*>(u)->x = x;
        static_cast<CursorLog*>(u)->y = y;
    };
    sink.draw = [](void* u) { ++static_cast<CursorLog*>(u)->draws; };
    const int32_t pointer[6] = {100, 90, 2, 0, 0, 0};
    set_pointer_state(*game, pointer);
    wr::mouse_look_begin(*game, sink);
    assert(log.polls == 1 && log.x == 320 && log.y == 240);
    assert(game->mouse_look_active == 1);
    assert(game->mouse_look_cell_x == 20);

    const int32_t moved[6] = {341, 229, 2, 0, 0, 0};
    set_pointer_state(*game, moved);
    wr::mouse_look_update(*game, sink);
    // (21/4 + 20) * 16 = 400; (-11/4 + 10) * 16 = 128.
    assert(cam_x(*game) == 400 && cam_y(*game) == 128);
    assert(log.x == 320 && log.y == 240 && log.draws == 0);

    const int32_t released[6] = {320, 240, 0, 0, 0, 0};
    set_pointer_state(*game, released);
    wr::mouse_look_update(*game, sink);
    assert(game->mouse_look_active == 0);
    assert(log.x == 100 && log.y == 90 && log.draws == 1);
}

void test_projection() {
    auto game = make_game();
    game->camera_x = 0x10100; // only the low 16 bits take part
    game->camera_y = 50;
    int32_t x = 0, y = 0;
    wr::project_unit_to_screen(*game, {300 << 16, 21 << 16, 200 << 16}, x, y);
    assert(x == 300 - 0x100 + 128);
    assert(y == 200 - 50 - 10 + 32);
}

struct SurfacePool {
    std::vector<std::unique_ptr<oa::Surface>> surfaces;
    std::vector<std::vector<uint8_t>> pixels;
    std::vector<const char*> names;
    int freed = 0;
};

oa::Surface* create(void* user, const char* name, int32_t w, int32_t h) {
    auto* pool = static_cast<SurfacePool*>(user);
    pool->pixels.emplace_back(static_cast<std::size_t>(w * h), uint8_t{0xee});
    auto surface = std::make_unique<oa::Surface>();
    surface->width = w;
    surface->height = h;
    surface->pitch = w;
    surface->pixels = pool->pixels.back().data();
    pool->surfaces.push_back(std::move(surface));
    pool->names.push_back(name);
    return pool->surfaces.back().get();
}

void test_radar() {
    auto game = make_game();
    game->radar_width = 4;
    game->radar_height = 3;
    game->map_width = 8;  // sight grid 4 wide
    game->map_height = 6; // 3 rows
    game->viewpoint_player = 1;
    game->ui_colors[0] = 0x11;
    game->radar_blink_flags = wr::radar_flag_blink;
    SurfacePool pool;
    std::vector<uint8_t> picture_pixels(12);
    for (std::size_t i = 0; i < picture_pixels.size(); ++i)
        picture_pixels[i] = static_cast<uint8_t>(0x40 + i);
    oa::Surface picture{};
    picture.width = 4;
    picture.height = 3;
    picture.pitch = 4;
    picture.pixels = picture_pixels.data();
    wr::RadarSurfaces surfaces;
    wr::RadarSurfaceHost host;
    host.user = &pool;
    host.build_picture = [](void*, wr::RadarSurfaces& s) { (void)s; };
    host.create_surface = create;
    host.free_surface = [](void* u, oa::Surface*) { ++static_cast<SurfacePool*>(u)->freed; };
    wr::radar_init_surfaces(*game, surfaces, host);
    surfaces.picture = &picture;
    assert(pool.names.size() == 2);
    assert(std::strcmp(pool.names[0], "radar composed image") == 0);
    assert(std::strcmp(pool.names[1], "radar mapped layer") == 0);
    const oa::Rect32 rect = game->radar_picture_rect;
    assert(rect.x1 == 4 && rect.y1 == 20 && rect.x2 == 7 && rect.y2 == 22);
    assert(game->radar_blink_countdown == 7);
    assert(game->radar_blink_flags == wr::radar_flag_mapped_dirty);

    uint16_t sight[12]{};
    uint8_t coverage[12]{};
    uint8_t gray[256];
    for (int i = 0; i < 256; ++i)
        gray[i] = static_cast<uint8_t>(i ^ 0x80);
    sight[0] = 0x2; // viewer 1 explored, not covered
    sight[1] = 0x3;
    coverage[1] = 1; // explored and covered
    sight[5] = 0x1;  // explored only by player 0
    wr::radar_fill_mapped(*game, surfaces, {sight, coverage, gray});
    const auto* mapped = surfaces.mapped->pixels;
    assert(mapped[0] == (0x40 ^ 0x80));
    assert(mapped[1] == 0x41);
    assert(mapped[2] == 0x11 && mapped[5] == 0x11);
    assert(game->radar_blink_flags == wr::radar_flag_redraw);
    surfaces.mapped->pixels[0] = 0;
    wr::radar_fill_mapped(*game, surfaces, {sight, coverage, gray});
    assert(surfaces.mapped->pixels[0] == 0); // clean: no rebuild

    wr::radar_free_surfaces(surfaces, host);
    assert(pool.freed == 3 && surfaces.mapped == nullptr && surfaces.picture == nullptr);
}

} // namespace

int main() {
    test_clamp_and_radar_rect();
    test_glide();
    test_follow();
    test_shake();
    test_slots_and_start();
    test_mouse_look();
    test_projection();
    test_radar();
    return 0;
}
