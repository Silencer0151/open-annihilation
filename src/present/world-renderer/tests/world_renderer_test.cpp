// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/present/world_renderer.hpp"
#include "oa/present/world_renderer/unit_renderer.hpp"
#include "oa/test/check.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>

namespace {

uint8_t red_at(const oa::present::world_renderer::Surface& surface, uint32_t x, uint32_t y) {
    return surface.rgb[(static_cast<std::size_t>(y) * surface.width + x) * 3U];
}

} // namespace

int main() {
    oa::formats::tnt::Map map;
    map.tile_width = 2;
    map.tile_height = 2;
    map.tile_count = 4;
    map.tile_indices = {0, 1, 2, 3};
    map.tile_palette_indices.resize(4U * oa::formats::tnt::layout::tile_bytes);
    for (std::size_t tile = 0; tile < 4; ++tile) {
        std::fill_n(
            map.tile_palette_indices.begin() +
                static_cast<std::ptrdiff_t>(tile * oa::formats::tnt::layout::tile_bytes),
            oa::formats::tnt::layout::tile_bytes,
            static_cast<uint8_t>(tile + 1)
        );
    }
    for (std::size_t pixel = 0; pixel < oa::formats::tnt::layout::tile_bytes; ++pixel) {
        map.tile_palette_indices[pixel] = static_cast<uint8_t>(pixel);
    }
    oa::PaletteBytes palette{};
    for (std::size_t index = 0; index < oa::palette_color_count; ++index) {
        palette[index * oa::palette_entry_bytes] = static_cast<uint8_t>(index);
    }

    const auto crop = oa::present::world_renderer::render_viewport(map, palette, {31, 31, 3, 3});
    OA_CHECK(crop.ok());
    OA_CHECK(crop.surface->width == 3 && crop.surface->height == 3);
    OA_CHECK(red_at(*crop.surface, 0, 0) == 255);
    OA_CHECK(red_at(*crop.surface, 1, 0) == 2);
    OA_CHECK(red_at(*crop.surface, 0, 1) == 3);
    OA_CHECK(red_at(*crop.surface, 1, 1) == 4);
    const auto stride = oa::present::world_renderer::render_viewport(map, palette, {0, 1, 2, 1});
    OA_CHECK(stride.ok());
    OA_CHECK(red_at(*stride.surface, 0, 0) == 32);
    OA_CHECK(red_at(*stride.surface, 1, 0) == 33);

    const auto edge = oa::present::world_renderer::render_viewport(map, palette, {63, 63, 1, 1});
    OA_CHECK(edge.ok() && red_at(*edge.surface, 0, 0) == 4);
    const auto outside = oa::present::world_renderer::render_viewport(map, palette, {63, 63, 2, 1});
    OA_CHECK(!outside.ok());
    OA_CHECK(outside.error->code == oa::present::world_renderer::ErrorCode::viewport_out_of_bounds);
    const auto empty = oa::present::world_renderer::render_viewport(
        map, palette, {0, 0, 0, std::numeric_limits<uint32_t>::max()}
    );
    OA_CHECK(!empty.ok()); // Its nonzero height still exceeds terrain bounds.
    const auto bounded_empty =
        oa::present::world_renderer::render_viewport(map, palette, {0, 0, 0, 64});
    OA_CHECK(bounded_empty.ok() && bounded_empty.surface->rgb.empty());

    map.tile_indices[3] = 9;
    const auto missing = oa::present::world_renderer::render_viewport(map, palette, {32, 32, 1, 1});
    OA_CHECK(!missing.ok());
    OA_CHECK(missing.error->code == oa::present::world_renderer::ErrorCode::invalid_map_model);

    map.tile_indices[3] = 3;
    const oa::present::world_renderer::BattlefieldViewport battlefield{31, 31, 4, 2, 3, 3, 10, 8};
    const auto game_viewport =
        oa::present::world_renderer::game_battlefield_viewport(0, 0, 640, 480);
    OA_CHECK(
        game_viewport && game_viewport->destination_x == 128 &&
        game_viewport->destination_y == 32 && game_viewport->width == 512 &&
        game_viewport->height == 416
    );
    OA_CHECK(!oa::present::world_renderer::game_battlefield_viewport(0, 0, 127, 480));
    OA_CHECK(!oa::present::world_renderer::game_battlefield_viewport(0, 0, 640, 63));
    const oa::present::world_renderer::BattlefieldViewport overflowing_pick{
        std::numeric_limits<uint32_t>::max(), 0, 0, 0, 2, 1, 2, 1
    };
    OA_CHECK(!oa::present::world_renderer::screen_to_map_pixel(overflowing_pick, {1, 0}));
    const auto composed =
        oa::present::world_renderer::render_battlefield_viewport(map, palette, battlefield);
    OA_CHECK(composed.ok());
    OA_CHECK(composed.surface->width == 10 && composed.surface->height == 8);
    OA_CHECK(red_at(*composed.surface, 4, 2) == 255);
    OA_CHECK(red_at(*composed.surface, 5, 2) == 2);
    OA_CHECK(red_at(*composed.surface, 4, 3) == 3);
    OA_CHECK(red_at(*composed.surface, 0, 0) == 0);
    const auto map_point = oa::present::world_renderer::screen_to_map_pixel(battlefield, {5, 3});
    OA_CHECK(map_point && map_point->x == 32 && map_point->y == 32);
    OA_CHECK(!oa::present::world_renderer::screen_to_map_pixel(battlefield, {3, 3}));
    const auto screen_point =
        oa::present::world_renderer::map_pixel_to_screen(battlefield, {32, 32});
    OA_CHECK(screen_point.x == 5 && screen_point.y == 3);
    oa::present::world_renderer::BattlefieldViewport zoomed = battlefield;
    zoomed.scale = 2.0F;
    const auto zoomed_screen = oa::present::world_renderer::map_pixel_to_screen(zoomed, {32, 32});
    OA_CHECK(zoomed_screen.x == 6 && zoomed_screen.y == 4);
    const auto zoomed_map = oa::present::world_renderer::screen_to_map_pixel(zoomed, {6, 4});
    OA_CHECK(zoomed_map && zoomed_map->x == 32 && zoomed_map->y == 32);
    const auto scaled_one =
        oa::present::world_renderer::render_scaled_viewport(map, palette, 31, 31, 3, 3, 1.0F);
    OA_CHECK(scaled_one.ok());
    OA_CHECK(red_at(*scaled_one.surface, 0, 0) == red_at(*crop.surface, 0, 0));
    OA_CHECK(red_at(*scaled_one.surface, 1, 1) == red_at(*crop.surface, 1, 1));
    const auto scaled_two =
        oa::present::world_renderer::render_scaled_viewport(map, palette, 31, 31, 4, 4, 2.0F);
    OA_CHECK(scaled_two.ok());
    OA_CHECK(red_at(*scaled_two.surface, 0, 0) == red_at(*crop.surface, 0, 0));
    OA_CHECK(red_at(*scaled_two.surface, 1, 0) == red_at(*crop.surface, 0, 0));
    OA_CHECK(red_at(*scaled_two.surface, 2, 0) == red_at(*crop.surface, 1, 0));
    const auto outside_destination = oa::present::world_renderer::render_battlefield_viewport(
        map, palette, {0, 0, 8, 7, 3, 3, 10, 8}
    );
    OA_CHECK(!outside_destination.ok());
    OA_CHECK(
        outside_destination.error->code ==
        oa::present::world_renderer::ErrorCode::viewport_out_of_bounds
    );

    auto model = std::make_shared<oa::formats::objects3d::Model>();
    oa::formats::objects3d::Object object;
    object.name = "body";
    object.vertices.resize(4);
    oa::formats::objects3d::Primitive solid;
    solid.color_index = 5;
    solid.is_colored = 1;
    solid.vertex_indices = {0, 1, 2, 3};
    object.primitives.push_back(solid);
    oa::formats::objects3d::Primitive texture;
    texture.texture_name = "bodytex";
    texture.vertex_indices = {0, 1, 2, 3};
    object.primitives.push_back(texture);
    model->objects.push_back(object);
    auto instance = oa::sim::model_runtime::make_instance(model);
    auto& piece = instance.pieces().front();
    constexpr int32_t fixed = oa::formats::objects3d::kThreeDoUnitsPerWorldUnit;
    piece.transformed_vertices = {
        {-4 * fixed, 0, 4 * fixed},
        {4 * fixed, 0, 4 * fixed},
        {4 * fixed, 0, -4 * fixed},
        {-4 * fixed, 0, -4 * fixed}
    };
    const auto shared_projection = oa::present::world_renderer::unit_projection_for_viewport(
        battlefield, {32 * fixed, 0, 32 * fixed}
    );
    OA_CHECK(shared_projection.camera_pixel_x == 31);
    OA_CHECK(shared_projection.camera_pixel_y == 31);
    OA_CHECK(shared_projection.destination_origin_x == 4);
    OA_CHECK(shared_projection.destination_origin_y == 2);
    OA_CHECK(
        shared_projection.raster_clip && shared_projection.raster_clip->x == 4 &&
        shared_projection.raster_clip->y == 2 && shared_projection.raster_clip->width == 2 &&
        shared_projection.raster_clip->height == 2
    );
    oa::present::world_renderer::Surface unit_surface{16, 16, std::vector<uint8_t>(16U * 16U * 3U)};
    palette[5 * oa::palette_entry_bytes] = 55;
    const auto unit_result = oa::present::world_renderer::render_colored_instance(
        unit_surface, instance, palette, {{}, 0, 0, 8, 8}
    );
    OA_CHECK(unit_result.ok());
    OA_CHECK(unit_result.stats->colored_primitives == 1);
    OA_CHECK(unit_result.stats->textured_primitives_deferred == 1);
    OA_CHECK(red_at(unit_surface, 8, 8) == 55);
    OA_CHECK(red_at(unit_surface, 3, 8) == 0);
    std::fill(unit_surface.rgb.begin(), unit_surface.rgb.end(), 200);
    oa::present::world_renderer::UnitMaterialState shadow_state;
    shadow_state.cast_shadow = true;
    const auto shadowed = oa::present::world_renderer::render_colored_instance(
        unit_surface, instance, palette, {{}, 0, 0, 8, 8}, nullptr, shadow_state
    );
    OA_CHECK(shadowed.ok());
    bool darkened = false;
    for (std::size_t i = 0; i < unit_surface.rgb.size(); i += 3) {
        if (unit_surface.rgb[i] == 100) {
            darkened = true;
            break;
        }
    }
    OA_CHECK(darkened);
    std::fill(unit_surface.rgb.begin(), unit_surface.rgb.end(), 0);
    auto clipped_projection = oa::present::world_renderer::UnitProjection{{}, 0, 0, 8, 8};
    clipped_projection.raster_clip = oa::present::world_renderer::RasterClip{9, 0, 7, 16};
    const auto clipped_unit = oa::present::world_renderer::render_colored_instance(
        unit_surface, instance, palette, clipped_projection
    );
    OA_CHECK(clipped_unit.ok());
    OA_CHECK(red_at(unit_surface, 8, 8) == 0);
    OA_CHECK(red_at(unit_surface, 9, 8) == 55);

    oa::present::world_renderer::TextureCatalog catalog;
    oa::formats::gaf::RenderedFrame textured;
    textured.width = 2;
    textured.height = 2;
    textured.pixels = {7, 7, 7, 7};
    textured.coverage = {1, 1, 1, 1};
    oa::present::world_renderer::TextureMaterial material;
    material.mode = oa::present::world_renderer::MaterialFrameMode::animated;
    material.frames.emplace_back(std::move(textured));
    oa::formats::gaf::RenderedFrame textured_second;
    textured_second.width = 2;
    textured_second.height = 2;
    textured_second.pixels = {8, 8, 8, 8};
    textured_second.coverage = {1, 1, 1, 1};
    material.frames.emplace_back(std::move(textured_second));
    catalog.materials.emplace("bodytex", std::move(material));
    palette[7 * oa::palette_entry_bytes] = 77;
    std::fill(unit_surface.rgb.begin(), unit_surface.rgb.end(), 0);
    const auto textured_result = oa::present::world_renderer::render_colored_instance(
        unit_surface, instance, palette, {{}, 0, 0, 8, 8}, &catalog
    );
    OA_CHECK(textured_result.ok());
    OA_CHECK(textured_result.stats->textured_primitives == 1);
    OA_CHECK(textured_result.stats->textured_primitives_deferred == 0);
    OA_CHECK(red_at(unit_surface, 8, 8) == 77);
    palette[8 * oa::palette_entry_bytes] = 88;
    std::fill(unit_surface.rgb.begin(), unit_surface.rgb.end(), 0);
    oa::present::world_renderer::UnitMaterialState render_material_state;
    render_material_state.primitive_cursors = {{0, 1, 1}};
    const auto animated_result = oa::present::world_renderer::render_colored_instance(
        unit_surface, instance, palette, {{}, 0, 0, 8, 8}, &catalog, render_material_state
    );
    OA_CHECK(animated_result.ok());
    OA_CHECK(red_at(unit_surface, 8, 8) == 88);

    // Material frame dispatch, including the probe's full input domain.
    oa::present::world_renderer::TextureMaterial animated;
    animated.mode = oa::present::world_renderer::MaterialFrameMode::animated;
    oa::present::world_renderer::TextureMaterial team;
    team.mode = oa::present::world_renderer::MaterialFrameMode::owner_team;
    oa::present::world_renderer::TextureMaterial fixed_material;
    for (uint8_t index = 0; index < 10; ++index) {
        oa::formats::gaf::RenderedFrame frame;
        frame.width = frame.height = 1;
        frame.pixels = {index};
        frame.coverage = {1};
        animated.frames.emplace_back(frame);
        team.frames.emplace_back(std::move(frame));
    }
    fixed_material.frames.emplace_back(animated.frames.front());
    for (const auto cursor : {0U, 3U, 9U}) {
        for (const auto owner : {0U, 7U, 10U, 255U}) {
            for (const bool force_first : {false, true}) {
                const oa::present::world_renderer::MaterialFrameState state{
                    static_cast<uint16_t>(cursor), force_first, static_cast<uint8_t>(owner)
                };
                const auto* selected_fixed =
                    oa::present::world_renderer::select_material_frame(fixed_material, state);
                OA_CHECK(selected_fixed != nullptr && selected_fixed->pixels[0] == 0);
                const auto* selected_animated =
                    oa::present::world_renderer::select_material_frame(animated, state);
                OA_CHECK(selected_animated != nullptr);
                OA_CHECK(selected_animated->pixels[0] == (force_first ? 0U : cursor));
                const auto* selected_team =
                    oa::present::world_renderer::select_material_frame(team, state);
                if (owner < 10U) {
                    OA_CHECK(selected_team != nullptr);
                    OA_CHECK(selected_team->pixels[0] == owner);
                } else {
                    OA_CHECK(selected_team == nullptr);
                }
            }
        }
    }
    OA_CHECK(
        oa::present::world_renderer::select_material_frame(team, {0, false, std::nullopt}) ==
        nullptr
    );
    OA_CHECK(
        oa::present::world_renderer::select_material_frame(animated, {10, false, 0}) == nullptr
    );

    oa::present::world_renderer::UnitMaterialState unit_material_state;
    unit_material_state.primitive_cursors = {{0, 1, 7}, {0, 2, 3}};
    OA_CHECK(unit_material_state.cursor_for(0, 1) == 7);
    OA_CHECK(unit_material_state.cursor_for(0, 2) == 3);
    OA_CHECK(unit_material_state.cursor_for(1, 1) == 0);

    // shared_radar_contact: both a non-zero alliance byte and ShareRadar (bit 0x40) are
    // required. Other share-flag bits do not qualify, and a zero alliance byte
    // hides the contact even when every share bit is set.
    OA_CHECK(!oa::present::world_renderer::shared_radar_contact({0, 0x40}));
    OA_CHECK(!oa::present::world_renderer::shared_radar_contact({0, 0xff}));
    OA_CHECK(!oa::present::world_renderer::shared_radar_contact({1, 0x00}));
    OA_CHECK(!oa::present::world_renderer::shared_radar_contact({1, 0xbf}));
    OA_CHECK(!oa::present::world_renderer::shared_radar_contact({2, 0x3f}));
    OA_CHECK(oa::present::world_renderer::shared_radar_contact({1, 0x40}));
    OA_CHECK(oa::present::world_renderer::shared_radar_contact({1, 0x41}));
    OA_CHECK(oa::present::world_renderer::shared_radar_contact({0xff, 0x40}));
    OA_CHECK(oa::present::world_renderer::shared_radar_contact({0x80, 0xef}));
    return oa::test::check_exit_status();
}
