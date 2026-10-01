// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Unit, feature, debris and projectile model drawing.
#include "oa/present/model/model_draw.hpp"

#include "oa/present/model/mesh_raster.hpp"
#include "oa/present/blit.hpp"
#include "oa/present/display.hpp"
#include "oa/present/palette_tables.hpp"
#include "oa/present/polygon.hpp"
#include "oa/present/rle.hpp"
#include "oa/present/span_sample.hpp"
#include "oa/base/geometry.hpp"

#include <cmath>
#include <cstdlib>
#include <cstring>

namespace oa::present::model {
namespace {

using formats::objects3d::FixedVector3;
using sim::model_runtime::PieceFlag;
using sim::model_runtime::PieceState;

constexpr uint16_t piece_visible = static_cast<uint16_t>(PieceFlag::visible);
constexpr uint16_t piece_cached = static_cast<uint16_t>(PieceFlag::cached);
constexpr uint16_t piece_shaded = static_cast<uint16_t>(PieceFlag::shaded);
// Margin added around measured model extents.
constexpr int32_t bounds_margin = 2;
// Build-effect colour ramp: palette 0xa0..0xaf, up then down.
constexpr uint8_t nano_ramp_low = 0xa0;
constexpr uint8_t nano_ramp_high = 0xaf;
constexpr float progress_scale = 255.0F;
constexpr int64_t composite_max_pixels = 16 * 1024 * 1024;
constexpr double light_input_scale = 0.01;
// Largest image the arena allocators hand out.
constexpr int64_t image_max_pixels = 16 * 1024 * 1024;

int32_t hi(int32_t value) noexcept {
    return static_cast<int16_t>(static_cast<uint32_t>(value) >> 16);
}

int32_t wrap_add(int32_t a, int32_t b) noexcept {
    return static_cast<int32_t>(static_cast<uint32_t>(a) + static_cast<uint32_t>(b));
}

int32_t wrap_sub(int32_t a, int32_t b) noexcept {
    return static_cast<int32_t>(static_cast<uint32_t>(a) - static_cast<uint32_t>(b));
}

// Low word of a 64-bit truncation toward zero; values without a 64-bit
// representation give INT64_MIN (low word 0).
int32_t truncate_low(double value) noexcept {
    if (!std::isfinite(value) || value >= 9223372036854775808.0 || value < -9223372036854775808.0)
        return 0;
    return static_cast<int32_t>(
        static_cast<uint32_t>(static_cast<uint64_t>(static_cast<int64_t>(value)))
    );
}

uint32_t def_flags(const ModelRef& model) noexcept {
    return model.def != nullptr ? model.def->flags : 0;
}

bool is_digger(const ModelRef& model) noexcept {
    return (def_flags(model) & OA_UNIT_DEF_FLAG_DIGGER) != 0;
}

bool is_building(const Unit& unit) noexcept {
    return (unit.flags & OA_UNIT_FLAG_BUILDING) != 0;
}

int32_t vertex_depth_base(const ModelRef& model) noexcept {
    return model_depth_base + (is_digger(model) ? digger_depth_bias : 0);
}

bool has_image(const ModelState& state) noexcept {
    return state.image.sprite.data != nullptr;
}

uint32_t first_primitive(const PreparedObject& prepared) noexcept {
    return prepared.skips_first ? 1U : 0U;
}

const formats::objects3d::Primitive&
source_of(const formats::objects3d::Object& object, const PreparedPrimitive& primitive) {
    return object.primitives[primitive.source_index];
}

// Frame of an animated primitive's running cursor, ignoring team colouring.
const Sprite* cursor_texture(const PreparedPrimitive& primitive) noexcept {
    if ((primitive.flags & primitive_animated) == 0)
        return primitive.frame != nullptr && primitive.frame->data != nullptr ? primitive.frame
                                                                              : nullptr;
    if (primitive.texture == nullptr || primitive.cursor.sequence == nullptr ||
        primitive.cursor.frame_index >= primitive.texture->frames.size())
        return nullptr;
    const Sprite* sprite = &primitive.texture->frames[primitive.cursor.frame_index];
    return sprite->data != nullptr ? sprite : nullptr;
}

// Gathers a primitive's corners from projected vertices; false when an
// index lies outside the table.
template <typename Vertex>
bool gather(
    const formats::objects3d::Primitive& primitive,
    const std::vector<Vertex>& table,
    std::vector<Vertex>& out
) {
    out.clear();
    for (const uint16_t index : primitive.vertex_indices) {
        if (index >= table.size())
            return false;
        out.push_back(table[index]);
    }
    return true;
}

bool piece_passes(const PieceState& piece, int32_t pass, const Unit& unit) noexcept {
    if ((piece.flags & piece_visible) == 0)
        return false;
    return pass == pass_all_pieces || pass == ((piece.flags >> 1) & 1) ||
           unit.build_remaining != 0.0F;
}

// The composite grows to fit a model larger than its 600x600
// start size.
void ensure_composite(ModelRenderer& renderer, int64_t pixels) {
    auto& composite = renderer.composite;
    const auto plane = static_cast<int64_t>(composite.pixels.size() / 2);
    if (pixels <= plane || pixels > composite_max_pixels)
        return;
    std::vector<uint8_t> grown(static_cast<std::size_t>(pixels) * 2, 0);
    composite.pixels.swap(grown);
    composite.sprite.data = composite.pixels.data();
    composite.sprite.aux = composite.pixels.data() + pixels;
}

int64_t composite_plane(const ModelRenderer& renderer) noexcept {
    return static_cast<int64_t>(renderer.composite.pixels.size() / 2);
}

std::size_t plane_size(const Sprite& sprite) noexcept {
    return static_cast<std::size_t>(sprite.width) * sprite.height;
}

// Projects a piece's vertices into image space.
void project_image_vertices(
    const PieceState& piece,
    const Sprite& target,
    bool doubled,
    int32_t depth_base,
    std::vector<present::DepthVertex>& out
) {
    out.clear();
    for (const FixedVector3& v : piece.transformed_vertices) {
        int32_t x = hi(v.x);
        int32_t y = hi(v.y);
        int32_t z = hi(wrap_sub(0, v.z));
        if (doubled) {
            x <<= 1;
            y <<= 1;
            z <<= 1;
        }
        present::DepthVertex vertex{};
        vertex.x = x + target.origin_x;
        vertex.y = z - (y >> 1) + target.origin_y;
        vertex.depth = depth_base + (doubled ? y / 2 : y);
        out.push_back(vertex);
    }
}

// Starts the double-size pass of an anti-aliased building image in the
// composite buffer; returns the target to draw into.
Sprite*
begin_image_pass(ModelRenderer& renderer, Sprite& image, const ModelRef& model, int32_t pass) {
    if ((renderer.graphics_flags & graphics_anti_alias) == 0 || !is_building(*model.unit) ||
        pass == 0)
        return &image;
    ensure_composite(renderer, static_cast<int64_t>(image.width) * 2 * image.height * 2);
    Sprite& composite = renderer.composite.sprite;
    const int64_t pixels = static_cast<int64_t>(image.width << 1) * (image.height << 1);
    if (pixels > composite_plane(renderer))
        return &image;
    composite.width = static_cast<uint16_t>(image.width << 1);
    composite.encoding = OA_SPRITE_RAW;
    composite.key = image_key;
    composite.height = static_cast<uint16_t>(image.height << 1);
    composite.origin_x = static_cast<int16_t>(static_cast<uint16_t>(image.origin_x) << 1);
    composite.origin_y = static_cast<int16_t>(static_cast<uint16_t>(image.origin_y) << 1);
    std::memset(composite.aux, 0, plane_size(composite));
    std::memset(composite.data, image_key, plane_size(composite));
    return &composite;
}

void finish_image_pass(ModelRenderer& renderer, Sprite& image, const Sprite* target) {
    if (target == &image || (renderer.graphics_flags & graphics_anti_alias) == 0)
        return;
    if (const present::DisplayContext* display = present::display_context(); display != nullptr)
        present::blend_downsample_sprite(*display, *target, image);
    copy_sparse_depth(*target, image);
}

void allocate_planes(present::SpriteBuffer& buffer, int32_t width, int32_t height, bool depth) {
    buffer = {};
    if (width < 0 || height < 0 || static_cast<int64_t>(width) * height > image_max_pixels)
        return;
    const auto size = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    buffer.pixels.assign((depth ? size * 2 : size) + 1, 0);
    Sprite& sprite = buffer.sprite;
    sprite.width = static_cast<uint16_t>(width);
    sprite.height = static_cast<uint16_t>(height);
    sprite.data = buffer.pixels.data();
    sprite.aux = depth ? buffer.pixels.data() + size : nullptr;
    sprite.key = image_key;
    std::memset(sprite.data, image_key, size);
}

// A model as the composer callbacks see it.
struct ComposeContext {
    ModelRenderer* renderer{};
    const ModelRef* model{};
    const Sprite* source{};
};

bool compose_bounds(void* user, const Unit& unit, ui::hud::SpriteBounds& out) {
    auto* context = static_cast<ComposeContext*>(user);
    ModelRef model = *context->model;
    if (&unit != context->model->unit) {
        if (context->renderer->model_of == nullptr)
            return false;
        model = context->renderer->model_of(context->renderer->user, unit);
    }
    if (model.instance == nullptr)
        return false;
    // The composer places a carried unit's extent by `unit`, the record it
    // walked to; the unit draws where the record model_of handed back stands,
    // which a draw between ticks places elsewhere.
    ModelBounds bounds{};
    expand_model_bounds(
        bounds,
        *model.instance,
        wrap_sub(model.unit->position.x, unit.position.x),
        wrap_sub(model.unit->position.y, unit.position.y),
        wrap_sub(model.unit->position.z, unit.position.z)
    );
    out = {bounds.left, bounds.right, bounds.top, bounds.bottom};
    return true;
}

void set_composite_frame(ModelRenderer& renderer, const ui::hud::SpriteFrame& frame, uint8_t key) {
    ensure_composite(renderer, static_cast<int64_t>(frame.width) * frame.height);
    Sprite& composite = renderer.composite.sprite;
    composite.width = frame.width;
    composite.height = frame.height;
    composite.origin_x = frame.origin_x;
    composite.origin_y = frame.origin_y;
    composite.key = key;
}

void compose_copy(void* user, const ui::hud::SpriteFrame& frame) {
    auto* context = static_cast<ComposeContext*>(user);
    const Sprite& source = *context->source;
    set_composite_frame(*context->renderer, frame, source.key);
    Sprite& composite = context->renderer->composite.sprite;
    const auto size = plane_size(source);
    if (static_cast<int64_t>(size) > composite_plane(*context->renderer))
        return;
    std::memmove(composite.data, source.data, size);
    if (source.aux != nullptr)
        std::memmove(composite.aux, source.aux, size);
}

// Clears the composite and draws the source's colour plane, then its depth
// plane (as a keyed sprite), at (x, y).
void compose_redraw(void* user, const ui::hud::SpriteFrame& frame, int32_t x, int32_t y) {
    auto* context = static_cast<ComposeContext*>(user);
    Sprite source = *context->source;
    set_composite_frame(*context->renderer, frame, source.key);
    Sprite& composite = context->renderer->composite.sprite;
    if (static_cast<int64_t>(plane_size(composite)) > composite_plane(*context->renderer))
        return;
    source.origin_x = 0;
    source.origin_y = 0;
    Surface surface{};
    present::surface_from_sprite(surface, composite);
    std::memset(composite.data, composite.key, plane_size(composite));
    std::memset(composite.aux, 0, plane_size(composite));
    present::draw_sprite(&surface, &source, x, y);
    if (source.aux == nullptr)
        return;
    surface.pixels = static_cast<uint8_t*>(composite.aux);
    std::swap(source.data, source.aux);
    present::draw_sprite(&surface, &source, x, y);
}

// The build effect runs at the end of the composition.
void compose_finish(void* user, const ui::hud::SpriteFrame&) {
    auto* context = static_cast<ComposeContext*>(user);
    apply_build_effect(*context->renderer, context->renderer->composite.sprite, *context->model);
}

// Draws coloured primitives filled and textured quads mapped, from vertices
// already on the target surface.
template <typename Texture>
void draw_flat_primitives(
    Surface* target,
    const formats::objects3d::Object& object,
    const PreparedObject& prepared,
    const std::vector<present::PolygonVertex>& projected,
    Texture texture
) {
    thread_local std::vector<present::PolygonVertex> corners;
    for (uint32_t i = first_primitive(prepared); i < prepared.primitives.size(); ++i) {
        const PreparedPrimitive& primitive = prepared.primitives[i];
        if (!gather(source_of(object, primitive), projected, corners))
            continue;
        const auto count = static_cast<int32_t>(corners.size());
        if ((primitive.flags & primitive_colored) != 0) {
            present::fill_polygon(target, corners.data(), count, primitive.color);
        } else if (count == 4) {
            if (const Sprite* sprite = texture(primitive); sprite != nullptr)
                texture_quad(target, sprite, corners.data(), nullptr);
        }
    }
}

} // namespace

bool init_composite_buffer(ModelRenderer& renderer) {
    renderer.composite = present::create_two_plane_sprite(composite_side, composite_side);
    // Every image copied into the composite uses key 1.
    renderer.composite.sprite.key = image_key;
    return renderer.composite.sprite.data != nullptr;
}

void set_model_light(ModelRenderer& renderer, int32_t x, int32_t y, int32_t z) noexcept {
    renderer.light[0] = static_cast<float>(static_cast<double>(x) * light_input_scale);
    renderer.light[1] = static_cast<float>(static_cast<double>(y) * light_input_scale);
    renderer.light[2] = static_cast<float>(static_cast<double>(z) * light_input_scale);
}

ImageFrame
measure_model_bounds(const sim::model_runtime::Instance& instance, const FixedVector3* offset) {
    int32_t min_x = 0;
    int32_t max_x = 0;
    int32_t min_y = 0;
    int32_t max_y = 0;
    const auto pieces = instance.pieces();
    for (std::size_t n = pieces.size(); n != 0; --n) {
        const PieceState& piece = pieces[n - 1];
        if ((piece.flags & piece_visible) == 0)
            continue;
        for (const FixedVector3& v : piece.transformed_vertices) {
            int32_t x = 0;
            int32_t y = 0;
            int32_t z = 0;
            if (offset == nullptr) {
                x = hi(v.x);
                y = hi(v.y);
                z = hi(wrap_sub(0, v.z));
            } else {
                x = hi(wrap_add(v.x, offset->x));
                z = hi(wrap_sub(offset->z, v.z));
                y = hi(wrap_add(v.y, offset->y));
            }
            const int32_t sy = z - (y >> 1);
            min_x = x < min_x ? x : min_x;
            max_x = max_x < x ? x : max_x;
            min_y = sy < min_y ? sy : min_y;
            max_y = max_y < sy ? sy : max_y;
        }
    }
    return {
        (max_x - (min_x - bounds_margin)) + bounds_margin,
        (max_y - (min_y - bounds_margin)) + bounds_margin,
        -(min_x - bounds_margin),
        -(min_y - bounds_margin)
    };
}

void expand_model_bounds(
    ModelBounds& bounds,
    const sim::model_runtime::Instance& instance,
    int32_t x,
    int32_t y,
    int32_t z
) {
    int32_t min_x = 0;
    int32_t max_x = 0;
    int32_t min_y = 0;
    int32_t max_y = 0;
    const auto pieces = instance.pieces();
    for (std::size_t n = pieces.size(); n != 0; --n) {
        const PieceState& piece = pieces[n - 1];
        if ((piece.flags & piece_visible) == 0)
            continue;
        for (const FixedVector3& v : piece.transformed_vertices) {
            const int32_t sx = hi(wrap_add(v.x, x));
            const int32_t sy = hi(wrap_sub(z, v.z)) - (hi(wrap_add(v.y, y)) >> 1);
            min_x = sx < min_x ? sx : min_x;
            max_x = max_x < sx ? sx : max_x;
            min_y = sy < min_y ? sy : min_y;
            max_y = max_y < sy ? sy : max_y;
        }
    }
    if (min_x - bounds_margin < bounds.left)
        bounds.left = min_x - bounds_margin;
    if (min_y - bounds_margin < bounds.top)
        bounds.top = min_y - bounds_margin;
    if (bounds.right < max_x + bounds_margin)
        bounds.right = max_x + bounds_margin;
    if (bounds.bottom < max_y + bounds_margin)
        bounds.bottom = max_y + bounds_margin;
}

void draw_model(
    ModelRenderer& renderer,
    Surface* target,
    const ModelRef& model,
    int32_t camera_x,
    int32_t camera_z,
    bool first_frame
) {
    if (has_image(*model.state)) {
        draw_unit_model(renderer, target, model, camera_x, camera_z, first_frame);
        return;
    }
    const formats::objects3d::Model& source = model.instance->model();
    const auto pieces = model.instance->pieces();
    for (std::size_t n = pieces.size(); n != 0; --n) {
        const PieceState& piece = pieces[n - 1];
        if ((piece.flags & piece_visible) != 0)
            draw_piece_flat(
                renderer,
                target,
                *model.unit,
                camera_x,
                camera_z,
                source.objects[piece.object_index],
                model.prepared->objects[piece.object_index],
                piece,
                model.unit->owner_index,
                first_frame
            );
    }
}

void draw_piece_flat(
    const ModelRenderer& renderer,
    Surface* target,
    const Unit& unit,
    int32_t camera_x,
    int32_t camera_z,
    const formats::objects3d::Object& object,
    const PreparedObject& prepared,
    const PieceState& piece,
    uint8_t owner,
    bool first_frame
) {
    thread_local std::vector<present::PolygonVertex> projected;
    const int32_t dx = wrap_sub(unit.position.x, camera_x);
    const int32_t dz = wrap_sub(unit.position.z, camera_z);
    projected.clear();
    for (const FixedVector3& v : piece.transformed_vertices) {
        present::PolygonVertex vertex{};
        vertex.x = hi(wrap_add(v.x, dx)) + renderer.origin_x;
        vertex.y =
            (hi(wrap_sub(dz, v.z)) - (hi(wrap_add(v.y, unit.position.y)) >> 1)) + renderer.origin_y;
        projected.push_back(vertex);
    }
    const uint8_t team = renderer.team_colors[owner % 10];
    draw_flat_primitives(
        target, object, prepared, projected, [&](const PreparedPrimitive& primitive) {
            return primitive_texture(primitive, first_frame, team);
        }
    );
}

void allocate_image(present::SpriteBuffer& image, int32_t width, int32_t height) {
    allocate_planes(image, width, height, false);
}

void allocate_depth_image(present::SpriteBuffer& image, int32_t width, int32_t height) {
    allocate_planes(image, width, height, true);
}

bool prepare_model_image(
    ModelRenderer& renderer, const ModelRef& model, bool attached, int32_t pass
) {
    const Unit& unit = *model.unit;
    ModelState& state = *model.state;
    const ImageFrame frame = measure_model_bounds(*model.instance, nullptr);
    if (!attached && (unit.flags2 & OA_UNIT_FLAG2_Z_BUFFER) == 0 && unit.build_remaining == 0.0F)
        allocate_image(state.image, frame.width, frame.height);
    else
        allocate_depth_image(state.image, frame.width, frame.height);
    if (!has_image(state))
        return false;
    state.image_unfinished = unit.build_remaining != 0.0F;
    state.image.sprite.origin_x = static_cast<int16_t>(frame.origin_x);
    state.image.sprite.origin_y = static_cast<int16_t>(frame.origin_y);
    if (!is_building(unit) || (renderer.graphics_flags & graphics_shading) == 0)
        build_model_image(renderer, state.image.sprite, model, unit.owner_index, pass);
    else
        build_shaded_model_image(renderer, state.image.sprite, model, unit.owner_index, pass);
    return true;
}

void copy_sparse_depth(const Sprite& source, Sprite& target) noexcept {
    auto* out = static_cast<uint8_t*>(target.aux);
    if (out == nullptr || source.aux == nullptr)
        return;
    const auto* in = static_cast<const uint8_t*>(source.aux);
    for (int32_t y = 0; y < target.height; ++y) {
        for (int32_t x = 0; x < target.width; ++x) {
            *out++ = *in;
            in += 2;
        }
        in += source.width;
    }
}

void remap_depth_bands(
    Sprite& image, uint8_t threshold, int32_t above, int32_t below, int32_t band
) noexcept {
    const uint8_t low = threshold < 4 ? 0 : static_cast<uint8_t>(threshold - 4);
    auto* pixels = static_cast<uint8_t*>(image.data);
    const auto* depth = static_cast<const uint8_t*>(image.aux);
    const int32_t count = static_cast<int32_t>(image.width) * image.height;
    for (int32_t i = 0; i < count; ++i) {
        if (pixels[i] == image.key)
            continue;
        int32_t value = below;
        if (low <= depth[i]) {
            value = above;
            if (depth[i] < threshold)
                value = band;
        }
        if (value == remap_clear)
            pixels[i] = image.key;
        else if (value != remap_keep)
            pixels[i] = static_cast<uint8_t>(value);
    }
}

bool apply_build_effect(const ModelRenderer& renderer, Sprite& image, const ModelRef& model) {
    const Unit& unit = *model.unit;
    if (image.aux == nullptr || unit.build_remaining == 0.0F)
        return false;
    const uint32_t tick = renderer.tick;
    const uint32_t id = unit.id;
    const uint32_t wave_a = (tick * 0x21U) / 0x1eU + (id ^ 5U);
    const uint32_t wave_b = (tick * 0x39U) / 0x1eU + (id ^ 9U);
    const auto ramp = [](uint32_t wave) -> int32_t {
        return (wave & 0x10U) != 0 ? nano_ramp_high - static_cast<int32_t>(wave & 0xfU)
                                   : static_cast<int32_t>(wave & 0xfU) + nano_ramp_low;
    };
    const int32_t color_a = ramp(wave_a);
    const int32_t color_b = ramp(wave_b);
    const int32_t progress =
        truncate_low(static_cast<double>(unit.build_remaining) * progress_scale);
    if (progress > 0xeb) {
        remap_depth_bands(
            image,
            static_cast<uint8_t>(((progress - 0xeb) * 0xff) / 0x14),
            remap_clear,
            remap_clear,
            color_a
        );
    } else if (progress > 0xc8) {
        remap_depth_bands(
            image,
            static_cast<uint8_t>(((progress - 0xc8) * 0xff) / 0x23),
            remap_clear,
            remap_clear,
            color_a
        );
    } else if (progress > 0x73) {
        remap_depth_bands(
            image,
            static_cast<uint8_t>(((0x73 - progress) * 0xff) / 0x55 - 1),
            remap_clear,
            color_a,
            color_b
        );
    } else if (progress > 0x1e) {
        remap_depth_bands(
            image,
            static_cast<uint8_t>(((0x1e - progress) * 0xff) / 0x55 - 1),
            color_a,
            remap_keep,
            color_b
        );
    } else {
        remap_depth_bands(
            image, static_cast<uint8_t>((progress * 0xff) / 0x1e), remap_keep, remap_keep, color_a
        );
    }
    outline_model(image, model, static_cast<uint8_t>(color_b));
    return true;
}

void outline_model(Sprite& image, const ModelRef& model, uint8_t color) {
    thread_local std::vector<present::DepthVertex> projected;
    thread_local std::vector<present::DepthVertex> corners;
    const formats::objects3d::Model& source = model.instance->model();
    const int32_t base = vertex_depth_base(model);
    const auto pieces = model.instance->pieces();
    for (std::size_t n = pieces.size(); n != 0; --n) {
        const PieceState& piece = pieces[n - 1];
        if ((piece.flags & piece_visible) == 0)
            continue;
        project_image_vertices(piece, image, false, base, projected);
        const formats::objects3d::Object& object = source.objects[piece.object_index];
        const PreparedObject& prepared = model.prepared->objects[piece.object_index];
        for (uint32_t i = first_primitive(prepared); i < prepared.primitives.size(); ++i) {
            if (!gather(source_of(object, prepared.primitives[i]), projected, corners) ||
                corners.empty())
                continue;
            corners.push_back(corners.front());
            present::outline_depth_polygon(
                image, corners.data(), static_cast<int32_t>(corners.size()), color
            );
        }
    }
}

void draw_unit_model(
    ModelRenderer& renderer,
    Surface* target,
    const ModelRef& model,
    int32_t camera_x,
    int32_t camera_z,
    bool first_frame
) {
    ModelState& state = *model.state;
    if (!has_image(state))
        return;
    const Unit& unit = *model.unit;
    const Game& game = renderer.world->game;
    const int32_t dx = wrap_sub(unit.position.x, camera_x);
    const int32_t dz = wrap_sub(unit.position.z, camera_z);
    const int32_t ground = renderer.ground_height != nullptr
                               ? renderer.ground_height(renderer.user, unit.position)
                               : 0;
    const int32_t unit_height = hi(unit.position.y);
    const int32_t sx = hi(dx);
    const int32_t shadow_y = hi(dz) - (ground >> 1) + renderer.origin_y;
    const int32_t unit_y = hi(dz) - (unit_height >> 1) + renderer.origin_y;
    const int32_t shadow_x = sx + renderer.origin_x + shadow_offset_x;
    const int32_t image_x = sx + renderer.origin_x;
    const uint32_t flags = def_flags(model);
    const bool shadows = (renderer.graphics_flags & graphics_shadows) != 0 &&
                         (flags & OA_UNIT_DEF_FLAG_NO_SHADOW) == 0;
    const bool vehicle_shadow =
        (renderer.graphics_flags & graphics_vehicle_shadows) != 0 &&
        (flags & (OA_UNIT_DEF_FLAG_FLOATER | OA_UNIT_DEF_FLAG_CAN_HOVER)) == 0;
    const bool submerged_standin =
        unit.type_index == 0 && static_cast<int16_t>(unit_height) < game.sea_level;
    const bool translucent =
        (unit.state_flags & unit_state_cloaked) != 0 || game.debug_overlay != 0;
    const auto draw_building_shadow = [&]() {
        if (submerged_standin)
            return;
        if (state.shadow.sprite.data == nullptr)
            build_shadow_image(renderer, model, state.image.sprite);
        if (state.shadow.sprite.data != nullptr)
            present::draw_sprite_blended(target, &state.shadow.sprite, shadow_x, shadow_y);
    };
    if (state.image.sprite.aux == nullptr) {
        if (shadows) {
            if (is_building(unit) && (flags & OA_UNIT_DEF_FLAG_DIGGER) == 0)
                draw_building_shadow();
            else if (vehicle_shadow)
                present::draw_sprite_blended(
                    target, &copy_silhouette(renderer, state.image.sprite), shadow_x, shadow_y
                );
        }
        if (!has_image(state) && !prepare_model_image(renderer, model, false, pass_cached_pieces))
            return;
        if (!translucent)
            present::draw_sprite(target, &state.image.sprite, image_x, unit_y);
        else
            present::draw_sprite_blended(target, &state.image.sprite, image_x, unit_y);
        const formats::objects3d::Model& source = model.instance->model();
        const auto pieces = model.instance->pieces();
        for (std::size_t n = pieces.size(); n != 0; --n) {
            const PieceState& piece = pieces[n - 1];
            if ((piece.flags & piece_visible) != 0 && (piece.flags & piece_cached) == 0)
                draw_piece_flat(
                    renderer,
                    target,
                    unit,
                    camera_x,
                    camera_z,
                    source.objects[piece.object_index],
                    model.prepared->objects[piece.object_index],
                    piece,
                    unit.owner_index,
                    first_frame
                );
        }
        for (const Unit* child = world_unit(renderer.world, unit.attach_first_child);
             child != nullptr;
             child = world_unit(renderer.world, child->attach_next)) {
            if ((child->flags & unit_flag_attached_without_piece) != 0 ||
                renderer.model_of == nullptr)
                continue;
            const ModelRef carried = renderer.model_of(renderer.user, *child);
            if (carried.instance == nullptr)
                continue;
            const formats::objects3d::Model& child_source = carried.instance->model();
            const auto child_pieces = carried.instance->pieces();
            for (std::size_t n = child_pieces.size(); n != 0; --n) {
                const PieceState& piece = child_pieces[n - 1];
                if ((piece.flags & piece_visible) != 0)
                    draw_piece_flat(
                        renderer,
                        target,
                        *carried.unit,
                        camera_x,
                        camera_z,
                        child_source.objects[piece.object_index],
                        carried.prepared->objects[piece.object_index],
                        piece,
                        carried.unit->owner_index,
                        first_frame
                    );
            }
        }
        return;
    }
    if (shadows) {
        if ((flags & OA_UNIT_DEF_FLAG_DIGGER) != 0) {
            Sprite& silhouette = copy_silhouette(renderer, state.image.sprite);
            present::clear_sprite_below_depth(
                silhouette, static_cast<uint8_t>(vertex_depth_base(model))
            );
            present::draw_sprite_blended(target, &silhouette, shadow_x, shadow_y);
        } else if (is_building(unit)) {
            draw_building_shadow();
        } else if (vehicle_shadow) {
            Sprite& silhouette = copy_silhouette(renderer, state.image.sprite);
            const int32_t lift = static_cast<int32_t>(game.sea_level) - unit_height;
            if (lift > 0)
                present::clear_sprite_below_depth(
                    silhouette, static_cast<uint8_t>(vertex_depth_base(model) + lift)
                );
            present::draw_sprite_blended(target, &silhouette, shadow_x, shadow_y);
        }
    }
    if (!has_image(state) && !prepare_model_image(renderer, model, false, pass_cached_pieces))
        return;
    const Sprite& image = state.image.sprite;
    ComposeContext context{&renderer, &model, &image};
    ui::hud::SpriteComposer composer{};
    composer.user = &context;
    composer.model_bounds = compose_bounds;
    composer.copy = compose_copy;
    composer.redraw = compose_redraw;
    composer.finish = compose_finish;
    const ui::hud::SpriteFrame source_frame{
        image.width, image.height, image.origin_x, image.origin_y
    };
    ui::hud::compose_unit_sprite(*renderer.world, unit, source_frame, composer);
    Sprite& composite = renderer.composite.sprite;
    if (!is_building(unit) || unit.build_remaining == 0.0F)
        build_model_image(renderer, composite, model, unit.owner_index, pass_moving_pieces);
    for (const Unit* child = world_unit(renderer.world, unit.attach_first_child); child != nullptr;
         child = world_unit(renderer.world, child->attach_next)) {
        if ((child->flags & unit_flag_attached_without_piece) != 0 || renderer.model_of == nullptr)
            continue;
        const ModelRef carried = renderer.model_of(renderer.user, *child);
        if (carried.instance == nullptr)
            continue;
        prepare_model_image(renderer, carried, true, pass_all_pieces);
        if (!has_image(*carried.state))
            continue;
        apply_build_effect(renderer, carried.state->image.sprite, carried);
        const int32_t cx = wrap_sub(carried.unit->position.x, unit.position.x);
        const int32_t cy = wrap_sub(carried.unit->position.y, unit.position.y);
        const int32_t cz = wrap_sub(carried.unit->position.z, unit.position.z);
        present::composite_depth_sprite(
            carried.state->image.sprite, composite, hi(cx), hi(cz) - (hi(cy) >> 1), hi(cy)
        );
    }
    const int32_t lift = static_cast<int32_t>(game.sea_level) - unit_height;
    if (lift > 0) {
        const auto threshold = static_cast<uint8_t>(lift + vertex_depth_base(model));
        if ((unit.flags & OA_UNIT_FLAG_VIEWPOINT_OWNED) == 0 &&
            unit.owner_index != game.viewpoint_player)
            present::clear_sprite_below_depth(composite, threshold);
        else
            present::tint_sprite_below_depth(composite, threshold);
    }
    if ((flags & OA_UNIT_DEF_FLAG_DIGGER) != 0)
        present::clear_sprite_below_depth(composite, digger_clip_depth);
    if (!translucent)
        present::draw_sprite(target, &composite, image_x, unit_y);
    else
        present::draw_sprite_blended(target, &composite, image_x, unit_y);
}

void build_model_image(
    ModelRenderer& renderer, Sprite& image, const ModelRef& model, uint8_t owner, int32_t pass
) {
    thread_local std::vector<present::DepthVertex> projected;
    thread_local std::vector<present::DepthVertex> corners;
    Sprite* target = begin_image_pass(renderer, image, model, pass);
    const bool doubled = target != &image;
    const formats::objects3d::Model& source = model.instance->model();
    const int32_t base = vertex_depth_base(model);
    const uint8_t team = renderer.team_colors[owner % 10];
    const auto pieces = model.instance->pieces();
    for (std::size_t n = pieces.size(); n != 0; --n) {
        const PieceState& piece = pieces[n - 1];
        if (!piece_passes(piece, pass, *model.unit))
            continue;
        project_image_vertices(piece, *target, doubled, base, projected);
        const formats::objects3d::Object& object = source.objects[piece.object_index];
        const PreparedObject& prepared = model.prepared->objects[piece.object_index];
        for (uint32_t i = first_primitive(prepared); i < prepared.primitives.size(); ++i) {
            const PreparedPrimitive& primitive = prepared.primitives[i];
            if (!gather(source_of(object, primitive), projected, corners))
                continue;
            const auto count = static_cast<int32_t>(corners.size());
            if ((primitive.flags & primitive_colored) != 0) {
                present::fill_depth_polygon(*target, corners.data(), count, primitive.color);
            } else if (count == 4) {
                if (const Sprite* texture = primitive_texture(primitive, pass != 0, team);
                    texture != nullptr)
                    texture_depth_quad(target, texture, corners.data(), nullptr);
            }
        }
    }
    finish_image_pass(renderer, image, target);
}

void build_shaded_model_image(
    ModelRenderer& renderer, Sprite& image, const ModelRef& model, uint8_t owner, int32_t pass
) {
    struct Normal {
        float x{};
        float y{};
        float z{};
    };

    thread_local std::vector<present::DepthVertex> projected;
    thread_local std::vector<present::ShadedVertex> corners;
    thread_local std::vector<Normal> faces;
    thread_local std::vector<Normal> sums;
    thread_local std::vector<int32_t> uses;
    Sprite* target = begin_image_pass(renderer, image, model, pass);
    const bool doubled = target != &image;
    const formats::objects3d::Model& source = model.instance->model();
    const int32_t base = vertex_depth_base(model);
    const uint8_t team = renderer.team_colors[owner % 10];
    const auto pieces = model.instance->pieces();
    for (std::size_t n = pieces.size(); n != 0; --n) {
        const PieceState& piece = pieces[n - 1];
        if (!piece_passes(piece, pass, *model.unit))
            continue;
        const auto& points = piece.transformed_vertices;
        project_image_vertices(piece, *target, doubled, base, projected);
        uses.assign(points.size(), 0);
        sums.assign(points.size(), Normal{0.0F, 0.0F, 0.0F});
        const formats::objects3d::Object& object = source.objects[piece.object_index];
        const PreparedObject& prepared = model.prepared->objects[piece.object_index];
        const uint32_t first = first_primitive(prepared);
        faces.assign(prepared.primitives.size(), Normal{0.0F, 1.0F, 0.0F});
        for (uint32_t i = first; i < prepared.primitives.size(); ++i) {
            const auto& indices = source_of(object, prepared.primitives[i]).vertex_indices;
            // Fewer than three corners, a repeated corner, or a corner past the
            // table keeps the straight-up normal.
            if (indices.size() < 3 || indices[0] == indices[1] || indices[1] == indices[2] ||
                indices[2] == indices[0] || indices[0] >= points.size() ||
                indices[1] >= points.size() || indices[2] >= points.size())
                continue;
            const FixedVector3& v0 = points[indices[0]];
            const FixedVector3& v1 = points[indices[1]];
            const FixedVector3& v2 = points[indices[2]];
            const base::geometry::Vec3f a =
                base::geometry::vec3f_int_delta(v1.x, v1.y, v1.z, v0.x, v0.y, v0.z);
            const base::geometry::Vec3f b =
                base::geometry::vec3f_int_delta(v1.x, v1.y, v1.z, v2.x, v2.y, v2.z);
            const Normal cross{
                static_cast<float>(static_cast<double>(b.z) * a.y - static_cast<double>(a.z) * b.y),
                static_cast<float>(static_cast<double>(a.z) * b.x - static_cast<double>(b.z) * a.x),
                static_cast<float>(static_cast<double>(b.y) * a.x - static_cast<double>(b.x) * a.y)
            };
            const double length = std::sqrt(
                static_cast<double>(cross.x) * cross.x + static_cast<double>(cross.y) * cross.y +
                static_cast<double>(cross.z) * cross.z
            );
            faces[i] = {
                static_cast<float>(cross.x / length),
                static_cast<float>(cross.y / length),
                static_cast<float>(cross.z / length)
            };
        }
        for (uint32_t i = first; i < prepared.primitives.size(); ++i) {
            for (const uint16_t index : source_of(object, prepared.primitives[i]).vertex_indices) {
                if (index >= points.size())
                    continue;
                ++uses[index];
                sums[index].x += faces[i].x;
                sums[index].y += faces[i].y;
                sums[index].z += faces[i].z;
            }
        }
        for (std::size_t v = 0; v < points.size(); ++v) {
            if (uses[v] == 0)
                continue;
            const auto count = static_cast<float>(uses[v]);
            sums[v] = {sums[v].x / count, sums[v].y / count, sums[v].z / count};
        }
        const bool lit = (piece.flags & piece_shaded) != 0;
        for (uint32_t i = first; i < prepared.primitives.size(); ++i) {
            const PreparedPrimitive& primitive = prepared.primitives[i];
            const auto& indices = source_of(object, primitive).vertex_indices;
            corners.clear();
            bool valid = true;
            for (const uint16_t index : indices) {
                if (index >= projected.size()) {
                    valid = false;
                    break;
                }
                int32_t shade = unlit_shade;
                if (lit) {
                    // z, y then x products summed, scaled by light_scale,
                    // truncated and masked to a shade row.
                    const Normal& normal = sums[index];
                    const double dot = (static_cast<double>(renderer.light[2]) * normal.z +
                                        static_cast<double>(renderer.light[1]) * normal.y) +
                                       static_cast<double>(renderer.light[0]) * normal.x;
                    shade = static_cast<int32_t>(
                        static_cast<uint32_t>(truncate_low(dot * renderer.light_scale)) &
                        shade_row_mask
                    );
                }
                const present::DepthVertex& p = projected[index];
                corners.push_back({p.x, p.y, p.depth, shade});
            }
            if (!valid)
                continue;
            const auto count = static_cast<int32_t>(corners.size());
            if ((primitive.flags & primitive_colored) != 0) {
                present::fill_shaded_polygon(*target, corners.data(), count, primitive.color);
            } else if (count == 4) {
                if (const Sprite* texture = primitive_texture(primitive, pass != 0, team);
                    texture != nullptr)
                    texture_shaded_depth_quad(target, texture, corners.data(), nullptr);
            }
        }
    }
    finish_image_pass(renderer, image, target);
}

Sprite& copy_silhouette(ModelRenderer& renderer, const Sprite& image) {
    Sprite& composite = renderer.composite.sprite;
    ensure_composite(renderer, static_cast<int64_t>(image.width) * image.height);
    if (static_cast<int64_t>(plane_size(image)) > composite_plane(renderer))
        return composite;
    composite.width = image.width;
    composite.height = image.height;
    composite.origin_x = image.origin_x;
    composite.origin_y = image.origin_y;
    composite.key = image.key;
    std::memmove(composite.data, image.data, plane_size(image));
    if (image.aux != nullptr)
        std::memmove(composite.aux, image.aux, plane_size(image));
    present::clear_unkeyed_pixels(composite);
    return composite;
}

ImageFrame measure_shadow_bounds(const sim::model_runtime::Instance& instance) {
    int32_t min_x = 0;
    int32_t max_x = 0;
    int32_t min_y = 0;
    int32_t max_y = 0;
    const auto pieces = instance.pieces();
    for (std::size_t n = pieces.size(); n != 0; --n) {
        const PieceState& piece = pieces[n - 1];
        if ((piece.flags & piece_visible) == 0)
            continue;
        for (const FixedVector3& v : piece.transformed_vertices) {
            const int32_t lean = hi(v.y) >> 2;
            const int32_t sx = hi(v.x) + lean;
            const int32_t sy = hi(wrap_sub(0, v.z)) - lean;
            min_x = sx < min_x ? sx : min_x;
            max_x = max_x < sx ? sx : max_x;
            min_y = sy < min_y ? sy : min_y;
            max_y = max_y < sy ? sy : max_y;
        }
    }
    return {
        (max_x - (min_x - bounds_margin)) + bounds_margin,
        (max_y - (min_y - bounds_margin)) + bounds_margin,
        -(min_x - bounds_margin),
        -(min_y - bounds_margin)
    };
}

void draw_shadow_silhouette(Sprite& target, const ModelRef& model) {
    thread_local std::vector<present::DepthVertex> projected;
    thread_local std::vector<present::DepthVertex> corners;
    const formats::objects3d::Model& source = model.instance->model();
    const auto pieces = model.instance->pieces();
    for (std::size_t n = pieces.size(); n != 0; --n) {
        const PieceState& piece = pieces[n - 1];
        if ((piece.flags & piece_visible) == 0 || (piece.flags & piece_cached) == 0)
            continue;
        projected.clear();
        for (const FixedVector3& v : piece.transformed_vertices) {
            const int32_t height = hi(v.y);
            const int32_t lean = height >> 2;
            projected.push_back(
                {hi(v.x) + lean + target.origin_x,
                 hi(wrap_sub(0, v.z)) - lean + target.origin_y,
                 height + shadow_depth_base}
            );
        }
        const formats::objects3d::Object& object = source.objects[piece.object_index];
        const PreparedObject& prepared = model.prepared->objects[piece.object_index];
        for (uint32_t i = first_primitive(prepared); i < prepared.primitives.size(); ++i) {
            if (gather(source_of(object, prepared.primitives[i]), projected, corners))
                present::fill_depth_polygon(
                    target, corners.data(), static_cast<int32_t>(corners.size()), 0
                );
        }
    }
}

bool build_shadow_image(ModelRenderer& renderer, const ModelRef& model, const Sprite& image) {
    thread_local present::RleEncoder encoder;
    thread_local std::vector<uint8_t> stream;
    const ImageFrame frame = measure_shadow_bounds(*model.instance);
    ensure_composite(renderer, static_cast<int64_t>(frame.width) * frame.height);
    if (static_cast<int64_t>(frame.width) * frame.height > composite_plane(renderer))
        return false;
    Sprite& composite = renderer.composite.sprite;
    composite.width = static_cast<uint16_t>(frame.width);
    composite.height = static_cast<uint16_t>(frame.height);
    composite.origin_x = static_cast<int16_t>(frame.origin_x);
    composite.origin_y = static_cast<int16_t>(frame.origin_y);
    std::memset(
        composite.data, composite.key, static_cast<std::size_t>(frame.width) * frame.height
    );
    std::memset(composite.aux, 0, static_cast<std::size_t>(frame.width) * frame.height);
    draw_shadow_silhouette(composite, model);
    present::stamp_sprite_mask(image, composite, shadow_offset_x, 0);
    const int32_t size = present::encode_rle_sprite(encoder, nullptr, composite);
    stream.assign(static_cast<std::size_t>(size > 0 ? size : 0), 0);
    present::encode_rle_sprite(encoder, stream.data(), composite);
    present::SpriteBuffer& shadow = model.state->shadow;
    allocate_image(shadow, size, 1);
    if (shadow.sprite.data == nullptr)
        return false;
    std::memcpy(shadow.sprite.data, stream.data(), stream.size());
    shadow.sprite.width = composite.width;
    shadow.sprite.height = composite.height;
    shadow.sprite.origin_x = composite.origin_x;
    shadow.sprite.origin_y = composite.origin_y;
    shadow.sprite.encoding = OA_SPRITE_ROW_RLE;
    return true;
}

void set_model_shift(const ModelRef& model, sim::model_runtime::RotationWords rotation) {
    ModelState& state = *model.state;
    const auto near = [](int16_t a, int16_t b) {
        return std::abs(static_cast<int16_t>(a - b)) < shift_threshold;
    };
    if (near(state.shift.yz, rotation.yz) && near(state.shift.xz, rotation.xz) &&
        near(state.shift.xy, rotation.xy))
        return;
    state.shift = rotation;
    state.transforms_dirty = true;
    auto pieces = model.instance->pieces();
    // Reset the piece that holds the model's root object; root_piece() finds
    // it after the pieces take the COB script's order.
    const uint32_t root = model.instance->root_piece();
    if (root >= pieces.size())
        return;
    pieces[root].transform_marker = 0;
    if ((pieces[root].flags & piece_cached) != 0)
        state.cache.draws = 0;
}

void update_model_transforms(const ModelRef& model) {
    const Unit& unit = *model.unit;
    set_model_shift(model, {unit.bank, static_cast<int16_t>(unit.heading), unit.pitch});
    // Either dirty flag rebuilds the transforms: a new root shift sets
    // transforms_dirty, and the piece setters raise model_runtime's own
    // dirty flag.
    if (model.state->transforms_dirty || model.instance->transforms_dirty()) {
        model.instance->rebuild_transforms(model.state->shift);
        model.state->transforms_dirty = false;
    }
}

void note_piece_changes(const ModelRef& model) {
    ModelState& state = *model.state;
    const auto pieces = model.instance->pieces();
    if (state.pieces.size() != pieces.size()) {
        state.pieces.resize(pieces.size());
        for (std::size_t i = 0; i < pieces.size(); ++i)
            state.pieces[i] = {pieces[i].translation, pieces[i].rotation, pieces[i].flags};
        return;
    }
    for (std::size_t i = 0; i < pieces.size(); ++i) {
        const PieceState& piece = pieces[i];
        ModelState::PieceSnapshot& seen = state.pieces[i];
        const bool moved =
            piece.translation.x != seen.translation.x ||
            piece.translation.y != seen.translation.y ||
            piece.translation.z != seen.translation.z || piece.rotation.xy != seen.rotation.xy ||
            piece.rotation.xz != seen.rotation.xz || piece.rotation.yz != seen.rotation.yz;
        const uint16_t changed = static_cast<uint16_t>(piece.flags ^ seen.flags);
        if ((moved || (changed & piece_visible) != 0) && (piece.flags & piece_cached) != 0)
            state.cache.draws = 0;
        if ((changed & piece_cached) != 0)
            state.image = {};
        seen = {piece.translation, piece.rotation, piece.flags};
    }
}

void update_linked_transforms(const ModelRenderer& renderer, const ModelRef& model) {
    const Unit& unit = *model.unit;
    update_model_transforms(model);
    for (const Unit* child = world_unit(renderer.world, unit.attach_first_child); child != nullptr;
         child = world_unit(renderer.world, child->attach_next)) {
        if ((child->flags & unit_flag_attached_without_piece) != 0 || renderer.model_of == nullptr)
            continue;
        const ModelRef carried = renderer.model_of(renderer.user, *child);
        if (carried.instance != nullptr)
            update_model_transforms(carried);
    }
}

void draw_linked_model(
    ModelRenderer& renderer, Surface* target, const ModelRef& model, bool movement_idle
) {
    const Unit& unit = *model.unit;
    if (unit.attach_parent != 0)
        return;
    update_linked_transforms(renderer, model);
    ModelState& state = *model.state;
    // The build finished since the image was built: the image goes, as the
    // build's end drops it.
    if (state.image_unfinished && unit.build_remaining == 0.0F) {
        state.image = {};
        state.image_unfinished = false;
    }
    state.cache.has_image = has_image(state);
    state.cache.image_has_mask = state.image.sprite.aux != nullptr;
    const ui::hud::UnitSpriteDraw draw =
        ui::hud::prepare_unit_sprite(unit, state.cache, movement_idle);
    if (!state.cache.has_image || draw.rebuild)
        state.shadow = {};
    if (draw.rebuild)
        prepare_model_image(renderer, model, false, pass_cached_pieces);
    state.cache.has_image = has_image(state);
    state.cache.image_has_mask = state.image.sprite.aux != nullptr;
    draw_model(
        renderer,
        target,
        model,
        static_cast<int32_t>(static_cast<uint32_t>(renderer.camera_x) << 16),
        static_cast<int32_t>(static_cast<uint32_t>(renderer.camera_y) << 16),
        draw.unlit
    );
}

void draw_debris_piece(
    const ModelRenderer& renderer,
    Surface* target,
    const Rect32& view,
    const formats::objects3d::Object& object,
    const PreparedObject& prepared,
    const FixedVector3* points,
    const FixedVector3& origin,
    uint8_t team_color
) {
    thread_local std::vector<present::PolygonVertex> projected;
    const int32_t dx =
        wrap_sub(origin.x, static_cast<int32_t>(static_cast<uint32_t>(renderer.camera_x) << 16));
    const int32_t dz =
        wrap_sub(origin.z, static_cast<int32_t>(static_cast<uint32_t>(renderer.camera_y) << 16));
    const int32_t x = hi(dx) + renderer.origin_x;
    const int32_t y = hi(dz) - (hi(origin.y) >> 1) + renderer.origin_y;
    if (x < view.x1 || view.x2 < x || y < view.y1 || view.y2 < y)
        return;
    projected.clear();
    for (std::size_t i = 0; i < object.vertices.size(); ++i) {
        const FixedVector3& p = points[i];
        projected.push_back(
            {hi(wrap_add(p.x, dx)) + renderer.origin_x,
             (hi(wrap_sub(dz, p.z)) - (hi(wrap_add(p.y, origin.y)) >> 1)) + renderer.origin_y}
        );
    }
    draw_flat_primitives(
        target, object, prepared, projected, [&](const PreparedPrimitive& primitive) {
            return primitive_texture(primitive, false, team_color);
        }
    );
}

void draw_rotated_debris(
    const ModelRenderer& renderer,
    Surface* target,
    const Rect32& view,
    const formats::objects3d::Object& object,
    const PreparedObject& prepared,
    sim::model_runtime::RotationWords rotation,
    const FixedVector3& origin,
    uint8_t team_color,
    std::vector<FixedVector3>& points
) {
    points.resize(object.vertices.size());
    for (std::size_t n = object.vertices.size(); n != 0; --n) {
        const FixedVector3& v = object.vertices[n - 1];
        points[n - 1] =
            sim::model_runtime::rotate_vector({wrap_sub(0, v.x), v.y, wrap_sub(0, v.z)}, rotation);
    }
    draw_debris_piece(renderer, target, view, object, prepared, points.data(), origin, team_color);
}

void draw_rotated_object(
    const ModelRenderer& renderer,
    Surface* target,
    const FixedVector3& position,
    const FixedVector3* points,
    const formats::objects3d::Object& object,
    const PreparedObject& prepared,
    sim::model_runtime::RotationWords rotation
) {
    thread_local std::vector<present::PolygonVertex> projected;
    projected.clear();
    for (std::size_t i = 0; i < object.vertices.size(); ++i) {
        const FixedVector3 p = sim::model_runtime::rotate_vector(points[i], rotation);
        projected.push_back(
            {hi(wrap_add(p.x, position.x)) + renderer.origin_x,
             (hi(wrap_sub(position.z, p.z)) - (hi(wrap_add(p.y, position.y)) >> 1)) +
                 renderer.origin_y}
        );
    }
    draw_flat_primitives(target, object, prepared, projected, cursor_texture);
}

void draw_projectile_model(
    const ModelRenderer& renderer,
    Surface* target,
    const FixedVector3& position,
    const formats::objects3d::Object& object,
    const PreparedObject& prepared,
    sim::model_runtime::RotationWords rotation
) {
    thread_local std::vector<FixedVector3> points;
    points.clear();
    for (const FixedVector3& v : object.vertices)
        points.push_back({wrap_sub(0, v.x), v.y, wrap_sub(0, v.z)});
    draw_rotated_object(renderer, target, position, points.data(), object, prepared, rotation);
}

void draw_shatter_fragment(
    const ModelRenderer& renderer,
    Surface* target,
    const FixedVector3& position,
    const sim::effect_particles::ShatterFragment& fragment,
    const PreparedPrimitive& source,
    sim::model_runtime::RotationWords spin
) {
    namespace fx = sim::effect_particles;
    // The arena's object record: eight points, six faces over the game's
    // fragment corner table, no selection primitive.
    static const formats::objects3d::Object slab = [] {
        formats::objects3d::Object object;
        object.vertices.resize(fx::fragment_point_count);
        for (const auto& corners : fx::fragment_faces) {
            formats::objects3d::Primitive face;
            face.vertex_indices.assign(std::begin(corners), std::end(corners));
            object.primitives.push_back(face);
        }
        return object;
    }();
    PreparedPrimitive look = source;
    look.flags = fragment.look.flags;
    look.color = static_cast<uint8_t>(fragment.look.color);
    constexpr uint32_t shown_bits = primitive_colored | primitive_animated | primitive_team;
    if ((look.flags & shown_bits) == primitive_team && (source.flags & primitive_animated) != 0)
        look.frame = primitive_texture(source, false, fragment.look.team_color);
    thread_local PreparedObject faces;
    faces.skips_first = false;
    faces.primitives.assign(fx::fragment_face_count, look);
    for (uint32_t i = 0; i < fx::fragment_face_count; ++i)
        faces.primitives[i].source_index = i;
    FixedVector3 points[fx::fragment_point_count];
    for (uint32_t i = 0; i < fx::fragment_point_count; ++i)
        points[i] = {fragment.points[i].x, fragment.points[i].y, fragment.points[i].z};
    draw_rotated_object(renderer, target, position, points, slab, faces, spin);
}

} // namespace oa::present::model
