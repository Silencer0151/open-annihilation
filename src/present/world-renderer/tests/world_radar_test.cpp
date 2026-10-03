// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/present/world_renderer/world_camera.hpp"
#include "oa/present/world_renderer/world_radar.hpp"
#include "oa/present/palette_tables.hpp"
#include "oa/present/surface.hpp"
#include "oa/formats/tnt.hpp"
#include "oa/test/game_assets.hpp"
#include "oa/test/check.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

namespace wr = oa::present::world_renderer;

namespace {

constexpr std::size_t pair_table_size = static_cast<std::size_t>(oa::present::alpha_table_size);

struct SurfacePool {
    std::vector<std::unique_ptr<oa::Surface>> surfaces;
    std::vector<std::unique_ptr<uint8_t[]>> pixels;
    int freed = 0;
};

oa::Surface* create(void* user, const char*, int32_t width, int32_t height) {
    auto& pool = *static_cast<SurfacePool*>(user);
    auto surface = std::make_unique<oa::Surface>();
    auto bytes = std::make_unique<uint8_t[]>(static_cast<std::size_t>(width * height));
    oa::present::init_surface(*surface, width, height, width, bytes.get());
    pool.pixels.push_back(std::move(bytes));
    pool.surfaces.push_back(std::move(surface));
    return pool.surfaces.back().get();
}

void release(void* user, oa::Surface*) {
    ++static_cast<SurfacePool*>(user)->freed;
}

wr::RadarSurfaceHost make_host(SurfacePool& pool) {
    wr::RadarSurfaceHost host;
    host.user = &pool;
    host.create_surface = create;
    host.free_surface = release;
    return host;
}

// Display whose pair table keeps the first index of every pair, so a halved
// pixel is the top-left pixel of its 2x2 block.
struct FirstOfPair {
    std::vector<uint8_t> table = std::vector<uint8_t>(pair_table_size);
    oa::present::DisplayContext display{};

    FirstOfPair() {
        for (std::size_t a = 0; a < OA_PALETTE_COLORS; ++a)
            for (std::size_t b = 0; b < OA_PALETTE_COLORS; ++b)
                table[a * OA_PALETTE_COLORS + b] = static_cast<uint8_t>(a);
        display.alpha_table = table.data();
    }
};

struct WorldDeleter {
    void operator()(oa::World* world) const noexcept { oa::world_destroy(world); }
};

using WorldPtr = std::unique_ptr<oa::World, WorldDeleter>;

void wide_map_from_tiles() {
    auto game = std::make_unique<oa::Game>();
    game->map_pixel_width = 64;
    game->map_pixel_height = 32;
    game->map_width = 4; // 16-pixel cells: two 32-pixel tiles per row
    game->map_height = 2;
    const uint16_t tile_map[2] = {0, 1};
    std::vector<uint8_t> tiles(2 * 32 * 32);
    for (std::size_t i = 0; i < tiles.size(); ++i)
        tiles[i] = i < 32 * 32 ? 5 : 9;
    FirstOfPair pairs;
    wr::RadarPictureSource source;
    source.tile_map = tile_map;
    source.tile_count = 2;
    source.tile_pixels = tiles.data();
    source.display = &pairs.display;
    SurfacePool pool;
    wr::RadarSurfaces surfaces;
    wr::radar_build_picture(*game, surfaces, source, make_host(pool));
    OA_CHECK(game->radar_width == 126 && game->radar_height == 63);
    OA_CHECK(game->radar_offset_x == 0 && game->radar_offset_y == 31);
    OA_CHECK(surfaces.picture != nullptr && surfaces.picture->width == 126);
    OA_CHECK(pool.freed == 1); // the temporary mosaic
    const auto* pixels = surfaces.picture->pixels;
    // Column c samples map x = 64 * 2c / 252: columns 0..62 land in tile 0.
    OA_CHECK(pixels[0] == 5);
    OA_CHECK(pixels[62] == 5);
    OA_CHECK(pixels[63] == 9);
    OA_CHECK(pixels[125] == 9);
    OA_CHECK(pixels[62 * 126 + 125] == 9);
}

void tall_map_from_minimap() {
    auto game = std::make_unique<oa::Game>();
    game->map_pixel_width = 256;
    game->map_pixel_height = 512;
    game->map_width = 16;
    game->map_height = 32;
    std::vector<uint8_t> minimap(256 * 256);
    for (std::size_t y = 0; y < 256; ++y)
        for (std::size_t x = 0; x < 256; ++x)
            minimap[y * 256 + x] = static_cast<uint8_t>(x / 2 + (y / 2) % 3);
    FirstOfPair pairs;
    wr::RadarPictureSource source;
    source.minimap = minimap.data();
    source.minimap_stride = 256;
    source.display = &pairs.display;
    SurfacePool pool;
    wr::RadarSurfaces surfaces;
    wr::radar_build_picture(*game, surfaces, source, make_host(pool));
    OA_CHECK(game->radar_width == 63 && game->radar_height == 126);
    OA_CHECK(game->radar_offset_x == 31 && game->radar_offset_y == 0);
    OA_CHECK(pool.freed == 0);
    const auto* pixels = surfaces.picture->pixels;
    OA_CHECK(pixels[0] == 0);
    OA_CHECK(pixels[10] == 10);
    OA_CHECK(pixels[1 * 63 + 10] == 11);
    OA_CHECK(pixels[2 * 63 + 62] == 62 + 2);
}

// The halving pass mixes each row pair first, then the two row results.
void halving_mixes_rows_then_columns() {
    auto game = std::make_unique<oa::Game>();
    game->map_pixel_width = 64;
    game->map_pixel_height = 64;
    game->map_width = 4;
    game->map_height = 4;
    std::vector<uint8_t> minimap(252 * 252);
    minimap[0] = 1;
    minimap[1] = 2;
    minimap[252] = 3;
    minimap[253] = 4;
    std::vector<uint8_t> table(pair_table_size, 0);
    table[1 * 256 + 2] = 20; // top pair
    table[3 * 256 + 4] = 40; // bottom pair
    table[20 * 256 + 40] = 99;
    oa::present::DisplayContext display{};
    display.alpha_table = table.data();
    wr::RadarPictureSource source;
    source.minimap = minimap.data();
    source.minimap_stride = 252;
    source.display = &display;
    SurfacePool pool;
    wr::RadarSurfaces surfaces;
    wr::radar_build_picture(*game, surfaces, source, make_host(pool));
    OA_CHECK(surfaces.picture->pixels[0] == 99);
}

/// Ends the data run as failed unless a check holds; unlike assert it also
/// checks in builds that define NDEBUG.
///
/// @param condition the check
/// @param what the failure, printed after "FAIL: "
void require_data(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        std::exit(1);
    }
}

// The installed game's Acid Foursome builds its radar picture from its tiles
// and from its minimap.
void acid_foursome_from_assets(const oa::AssetStore& assets) {
    const auto bytes = oa::test::read_game_file(assets, "maps/acid foursome.tnt");
    require_data(!bytes.empty(), "the install holds maps/acid foursome.tnt");
    auto parsed = oa::formats::tnt::parse(bytes);
    require_data(parsed.map.has_value(), "maps/acid foursome.tnt parses");
    const auto& map = *parsed.map;
    auto game = std::make_unique<oa::Game>();
    game->map_pixel_width = static_cast<int32_t>(map.tile_width * 32U);
    game->map_pixel_height = static_cast<int32_t>(map.tile_height * 32U);
    game->map_width = static_cast<int32_t>(map.attribute_width);
    game->map_height = static_cast<int32_t>(map.attribute_height);
    FirstOfPair pairs;
    for (int pass = 0; pass < 2; ++pass) {
        wr::RadarPictureSource source;
        source.tile_map = map.tile_indices.data();
        source.tile_count = static_cast<int32_t>(map.tile_count);
        source.tile_pixels = map.tile_palette_indices.data();
        source.display = &pairs.display;
        if (pass == 1 && map.minimap.has_value()) {
            source.minimap = map.minimap->palette_indices.data();
            source.minimap_stride = static_cast<int32_t>(map.minimap->width);
        }
        SurfacePool pool;
        wr::RadarSurfaces surfaces;
        wr::radar_build_picture(*game, surfaces, source, make_host(pool));
        const int32_t width = game->radar_width;
        const int32_t height = game->radar_height;
        require_data(
            width <= wr::radar_picture_size && height <= wr::radar_picture_size,
            "the radar picture fits its surface"
        );
        require_data(
            width == wr::radar_picture_size || height == wr::radar_picture_size,
            "the radar picture spans its surface in one direction"
        );
        require_data(
            game->radar_offset_x + width <= wr::radar_picture_size &&
                game->radar_offset_y + height <= wr::radar_picture_size,
            "the radar picture's offset keeps it inside its surface"
        );
        require_data(surfaces.picture != nullptr, "the radar picture surface exists");
        int lit = 0;
        for (int32_t i = 0; i < width * height; ++i)
            lit += surfaces.picture->pixels[i] != 0;
        require_data(lit > width * height / 2, "most of the radar picture is drawn");
    }
}

void blink_clock() {
    auto game = std::make_unique<oa::Game>();
    game->radar_blink_countdown = 3;
    game->radar_blink_flags = wr::radar_flag_mapped_dirty;
    wr::radar_step_blink(*game);
    OA_CHECK(
        game->radar_blink_countdown == 2 && game->radar_blink_flags == wr::radar_flag_mapped_dirty
    );
    game->radar_blink_countdown = 0;
    wr::radar_step_blink(*game);
    OA_CHECK(game->radar_blink_countdown == wr::radar_blink_reload);
    OA_CHECK(game->radar_blink_flags == (wr::radar_flag_mapped_dirty | wr::radar_flag_blink));
    for (int step = 0; step < wr::radar_blink_reload; ++step)
        wr::radar_step_blink(*game);
    OA_CHECK(
        game->radar_blink_countdown == 0 && (game->radar_blink_flags & wr::radar_flag_blink) != 0
    );
    wr::radar_step_blink(*game);
    OA_CHECK((game->radar_blink_flags & wr::radar_flag_blink) == 0);
    // Negative countdowns reload as well.
    game->radar_blink_countdown = -1;
    wr::radar_step_blink(*game);
    OA_CHECK(game->radar_blink_countdown == wr::radar_blink_reload);
}

// FX.GAF sequences of 1x1 raw frames in distinct colours; radlogo has
// frames for player colours 0 and 3 only, nuclogo for colour 3 only.
struct Blips {
    uint8_t pixels[4] = {0xa0, 0xa1, 0xb0, 0xc3};
    oa::Sprite frames[4]{};
    oa::present::GafFrameSlot unit_slots[4]{};
    oa::present::GafFrameSlot cursor_slots[1]{};
    oa::present::GafFrameSlot weapon_slots[4]{};
    oa::present::GafSequence units{};
    oa::present::GafSequence cursor{};
    oa::present::GafSequence weapons{};

    Blips() {
        for (int i = 0; i < 4; ++i) {
            frames[i].width = 1;
            frames[i].height = 1;
            frames[i].key = 0xff;
            frames[i].encoding = OA_SPRITE_RAW;
            frames[i].data = &pixels[i];
        }
        unit_slots[0].frame = &frames[0];
        unit_slots[3].frame = &frames[1];
        cursor_slots[0].frame = &frames[2];
        weapon_slots[3].frame = &frames[3];
        units = {4, 0, 0, "radlogo", unit_slots};
        cursor = {1, 0, 0, "radlogohigh", cursor_slots};
        weapons = {4, 0, 0, "nuclogo", weapon_slots};
    }

    [[nodiscard]] wr::RadarSprites sprites() const { return {&units, &cursor, &weapons}; }
};

constexpr uint8_t mapped_fill = 0x11;
constexpr uint8_t ui_sensor = 0x5a;
constexpr uint8_t ui_jammer = 0x5c;
constexpr uint8_t ui_marks = 0x5e;
constexpr uint8_t ui_interceptor = 0x5f;

struct RadarScene {
    WorldPtr world{oa::world_create()};
    SurfacePool pool;
    wr::RadarSurfaces surfaces{};
    std::vector<oa::RadarHotUnit> hot = std::vector<oa::RadarHotUnit>(8);
    uint32_t listed_count{};
    bool point_seen = false;
    const oa::Projectile* hidden = nullptr; // the shot the host leaves off the radar

    RadarScene() {
        oa::WorldCapacity capacity{8, 3, 0};
        // Allocated outside the assert, which an optimised build compiles out.
        const auto allocated = oa::world_alloc_tables(world.get(), &capacity);
        OA_CHECK(allocated != 0);
        (void)allocated;
        auto& game = world->game;
        // A 1600x800 map drawn 100x50 on the radar: 16 map pixels per pixel.
        game.map_pixel_width = 1600;
        game.map_pixel_height = 800;
        game.radar_width = 100;
        game.radar_height = 50;
        game.radar_offset_x = 0;
        game.radar_offset_y = 13;
        game.viewpoint_player = 0;
        game.ui_colors[wr::ui_color_sensor_range] = ui_sensor;
        game.ui_colors[wr::ui_color_jammer_range] = ui_jammer;
        game.ui_colors[wr::ui_color_radar_marks] = ui_marks;
        game.ui_colors[wr::ui_color_interceptor_range] = ui_interceptor;
        for (uint32_t player = 0; player < 2; ++player) {
            game.players[player].info = oa::oa_ref_from_index(player);
            world->player_info[player].color = static_cast<uint8_t>(player);
        }
        world->player_info[1].color = 3; // the only non-empty shot icon frame
        surfaces.final_image = create(&pool, wr::radar_final_name, 100, 50);
        surfaces.mapped = create(&pool, wr::radar_mapped_name, 100, 50);
        for (int32_t i = 0; i < 100 * 50; ++i)
            surfaces.mapped->pixels[i] = mapped_fill;
    }

    oa::Unit& unit(uint32_t slot, uint8_t owner, int32_t x, int32_t y, int32_t z) {
        auto& record = world->units[slot];
        record.type_index = 1;
        record.id = static_cast<uint16_t>(slot);
        record.owner_index = owner;
        record.owner = oa::oa_ref_from_index(owner);
        record.position = {x << 16, y << 16, z << 16};
        return record;
    }

    [[nodiscard]] uint8_t at(int32_t x, int32_t y) const {
        return surfaces.final_image->pixels[y * 100 + x];
    }

    [[nodiscard]] int32_t count(uint8_t color) const {
        int32_t found = 0;
        for (int32_t i = 0; i < 100 * 50; ++i)
            found += surfaces.final_image->pixels[i] == color;
        return found;
    }

    bool allied_units_shown = false;

    void compose(const Blips& blips) {
        wr::RadarContactHost host{};
        host.allied_units_shown = allied_units_shown;
        host.user = this;
        host.point_visible = [](void* user, const oa::FixedVec3&) {
            return static_cast<RadarScene*>(user)->point_seen;
        };
        if (hidden != nullptr)
            host.projectile_hidden = [](void* user, const oa::Projectile& shot) {
                return &shot == static_cast<RadarScene*>(user)->hidden;
            };
        listed_count = wr::radar_compose_final(*world, surfaces, blips.sprites(), host, hot);
    }

    [[nodiscard]] uint32_t listed() const { return listed_count; }
};

void compose_units() {
    RadarScene scene;
    Blips blips;
    auto& game = scene.world->game;
    game.visibility_flags = wr::visibility_flags_radar_limited;
    // Own unit at map (160, 16, 320): column 10, row (320 - 8) / 16 = 19.
    scene.unit(1, 0, 160, 16, 320);
    // Enemy with no contact bits stays hidden; the second one is a contact.
    scene.unit(2, 1, 320, 0, 320);
    auto& contact = scene.unit(3, 1, 480, 0, 480);
    contact.flags = OA_UNIT_FLAG_RADAR_CONTACT;
    game.cursor_unit_id = 3;
    scene.compose(blips);
    OA_CHECK(scene.at(10, 19) == 0xa0);
    OA_CHECK(scene.at(20, 20) == mapped_fill);
    OA_CHECK(scene.at(30, 30) == 0xb0); // the cursor marker drawn over the contact blip
    OA_CHECK(scene.at(0, 0) == mapped_fill);
    OA_CHECK(scene.listed() == 2);
    // The count goes to the caller; the game block's copy is left alone.
    OA_CHECK(game.hot_radar_unit_count == 0);
    OA_CHECK(scene.hot[0].unit_id == 1 && scene.hot[0].x == 10 && scene.hot[0].y == 13 + 19);
    OA_CHECK(scene.hot[1].unit_id == 3 && scene.hot[1].x == 30 && scene.hot[1].y == 13 + 30);
    OA_CHECK((game.radar_blink_flags & wr::radar_flag_redraw) != 0);

    // Mapping and line of sight both off: every unit shows.
    game.visibility_flags = 0;
    scene.compose(blips);
    OA_CHECK(scene.at(20, 20) == 0xa1 && scene.listed() == 3);

    // The full-radar console bit shows every unit under limited sight too.
    game.visibility_flags = wr::visibility_flags_radar_limited;
    game.console_flags = wr::console_flag_full_radar;
    scene.compose(blips);
    OA_CHECK(scene.at(20, 20) == 0xa1 && scene.listed() == 3);

    // Units past the end of the list are drawn but neither listed nor counted.
    scene.hot.resize(1);
    scene.compose(blips);
    OA_CHECK(scene.listed() == 1 && scene.hot[0].unit_id == 1);
    OA_CHECK(scene.at(20, 20) == 0xa1 && scene.at(30, 30) == 0xb0);
}

// ui.allied-unit-display: under limited sight the units of a player who
// allies the viewer show without contact bits; the viewer allying the owner
// shows nothing.
void compose_allied_units() {
    RadarScene scene;
    Blips blips;
    auto& game = scene.world->game;
    game.visibility_flags = wr::visibility_flags_radar_limited;
    game.players[0].alliance[0] = 1;
    game.players[1].alliance[1] = 1;
    scene.unit(1, 0, 160, 16, 320);
    scene.unit(2, 1, 320, 0, 320);
    game.players[0].alliance[1] = 1;
    scene.allied_units_shown = true;
    scene.compose(blips);
    OA_CHECK(scene.at(10, 19) == 0xa0 && scene.at(20, 20) == mapped_fill && scene.listed() == 1);
    game.players[1].alliance[0] = 1;
    scene.allied_units_shown = false;
    scene.compose(blips);
    OA_CHECK(scene.at(20, 20) == mapped_fill && scene.listed() == 1);
    scene.allied_units_shown = true;
    scene.compose(blips);
    OA_CHECK(scene.at(10, 19) == 0xa0 && scene.at(20, 20) == 0xa1 && scene.listed() == 2);
    // An owner that withdraws its own entry hides even the viewer's units.
    game.players[0].alliance[0] = 0;
    scene.compose(blips);
    OA_CHECK(scene.at(10, 19) == mapped_fill && scene.listed() == 1);
}

void compose_damage_blink() {
    RadarScene scene;
    Blips blips;
    auto& hit = scene.unit(1, 0, 160, 0, 160);
    hit.damage_countdown = 5;
    scene.compose(blips);
    OA_CHECK(scene.at(10, 10) == mapped_fill && scene.listed() == 1);
    scene.world->game.radar_blink_flags = wr::radar_flag_blink;
    scene.compose(blips);
    OA_CHECK(scene.at(10, 10) == 0xa0);
}

void compose_rings() {
    RadarScene scene;
    Blips blips;
    auto& world = *scene.world;
    auto& def = world.unit_defs[1];
    def.radar_distance = 320; // 20 radar pixels
    def.radar_distance_jam = 240;
    def.abilities = OA_UNIT_DEF_ABILITY_ON_OFFABLE;
    def.flags = OA_UNIT_DEF_FLAG_ANTI_WEAPONS;
    auto& interceptor = world.game.weapon_defs[4];
    interceptor.flags = OA_WEAPON_FLAG_INTERCEPTOR;
    interceptor.coverage = wr::interceptor_ring_inset + 384; // 24 radar pixels
    auto& unit = scene.unit(1, 0, 800, 0, 400);
    unit.def = oa::oa_ref_from_index(1);
    unit.weapons[1].def = oa::oa_ref_from_index(4);
    // Not selected: no rings.
    scene.compose(blips);
    OA_CHECK(scene.count(ui_sensor) == 0 && scene.count(ui_interceptor) == 0);
    // Selected but switched off: an on/off unit shows only its interceptor ring.
    unit.flags = OA_UNIT_FLAG_SELECTED;
    scene.compose(blips);
    OA_CHECK(scene.count(ui_sensor) == 0 && scene.count(ui_jammer) == 0);
    const auto solid = scene.count(ui_interceptor);
    OA_CHECK(solid > 0);
    unit.state_flags = OA_UNIT_STATE_ACTIVE;
    scene.compose(blips);
    OA_CHECK(scene.count(ui_sensor) > 0 && scene.count(ui_jammer) > 0);
    // A stockpiled interceptor draws the dashed ring, half its chords.
    unit.weapons[1].stockpile = 1;
    scene.compose(blips);
    const auto dashed = scene.count(ui_interceptor);
    OA_CHECK(dashed > 0 && dashed < solid);
}

void compose_projectiles() {
    RadarScene scene;
    Blips blips;
    auto& world = *scene.world;
    auto& game = world.game;
    world.game.weapon_defs[1].flags = 0;
    world.game.weapon_defs[2].flags = OA_WEAPON_FLAG_NO_RADAR;
    world.game.weapon_defs[3].flags = OA_WEAPON_FLAG_TARGETABLE;
    auto& launcher = scene.unit(5, 0, 0, 0, 0);
    launcher.type_index = 0; // not drawn itself
    const auto shot = [&](int32_t index, uint32_t weapon, uint8_t owner, int32_t x) {
        auto& record = world.projectiles[index];
        record.def = oa::oa_ref_from_index(weapon);
        record.owner_index = owner;
        record.position = {x << 16, 0, 320 << 16};
        record.source = oa::oa_unit_ref_from_slot(5);
    };
    shot(0, 1, 0, 160); // own plain shot: dot
    shot(1, 1, 1, 320); // enemy plain shot out of sight: nothing
    shot(2, 2, 0, 480); // noradar: nothing
    shot(3, 3, 1, 640); // enemy nuke from a unit the viewer owns: icon of owner 1
    game.projectile_count = 4;
    scene.compose(blips);
    OA_CHECK(scene.at(10, 20) == ui_marks);
    OA_CHECK(scene.at(20, 20) == mapped_fill);
    OA_CHECK(scene.at(30, 20) == mapped_fill);
    OA_CHECK(scene.at(40, 20) == 0xc3);
    launcher.owner_index = 1;
    scene.compose(blips);
    OA_CHECK(scene.at(40, 20) == mapped_fill);
    scene.point_seen = true;
    scene.compose(blips);
    OA_CHECK(scene.at(20, 20) == ui_marks && scene.at(40, 20) == 0xc3);
    OA_CHECK(scene.at(30, 20) == mapped_fill);
    // A projectile the host hides (weapons.no-map-alert) draws neither its
    // dot nor its icon.
    scene.hidden = &world.projectiles[3];
    scene.compose(blips);
    OA_CHECK(scene.at(40, 20) == mapped_fill);
    OA_CHECK(scene.at(10, 20) == ui_marks && scene.at(20, 20) == ui_marks);
    scene.hidden = &world.projectiles[0];
    scene.compose(blips);
    OA_CHECK(scene.at(10, 20) == mapped_fill && scene.at(40, 20) == 0xc3);
}

void draw_final_image() {
    RadarScene scene;
    Blips blips;
    auto& game = scene.world->game;
    scene.compose(blips);
    game.radar_view_rect = {2, 15, 11, 20};
    std::vector<uint8_t> screen(126 * 126, 0);
    oa::Surface target{};
    oa::present::init_surface(target, 126, 126, 126, screen.data());
    wr::radar_draw(game, scene.surfaces, target);
    OA_CHECK((game.radar_blink_flags & wr::radar_flag_redraw) == 0);
    OA_CHECK(screen[12 * 126 + 50] == 0);           // above the picture
    OA_CHECK(screen[13 * 126 + 50] == mapped_fill); // first picture row
    OA_CHECK(screen[15 * 126 + 2] == ui_marks && screen[20 * 126 + 11] == ui_marks);
    OA_CHECK(screen[17 * 126 + 5] == mapped_fill); // inside the rectangle
    // Nothing is drawn again until the redraw bit is set.
    screen[13 * 126 + 50] = 0;
    wr::radar_draw(game, scene.surfaces, target);
    OA_CHECK(screen[13 * 126 + 50] == 0);
}

} // namespace

int main(int argc, char** argv) {
    if (oa::test::game_data_requested(argc, argv)) {
        acid_foursome_from_assets(
            oa::test::require_game_assets("the installed Acid Foursome radar")
        );
        std::puts("installed Acid Foursome radar passed");
        return oa::test::check_exit_status();
    }
    wide_map_from_tiles();
    tall_map_from_minimap();
    halving_mixes_rows_then_columns();
    blink_clock();
    compose_units();
    compose_allied_units();
    compose_damage_blink();
    compose_rings();
    compose_projectiles();
    draw_final_image();
    return oa::test::check_exit_status();
}
