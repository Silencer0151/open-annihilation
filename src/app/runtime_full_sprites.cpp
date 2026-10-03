// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The Full tier's sprite stage (runtime_full.hpp): the frame's sprites,
// particle squares, lines and selection lines as quads of the card's
// command list, in the planner's order, each sprite from its cell on the
// sprite pages.
#include "runtime_full.hpp"

#include "oa/present/world_renderer/world_fog.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace oa::app::full {

namespace {

namespace gpu = oa::present::gpu_world;
using oa::formats::gaf::RenderedFrame;

/// The bit that tells a key made from a GAF frame, one decoded for this
/// frame's draws alone, from one made from a rendered frame the match
/// keeps, so that the two kinds of address never name one cell.
constexpr uint64_t decoded_key_bit = uint64_t{1} << 63;

/// The vertex colour and alpha of a sprite the planner blends through the
/// alpha table: one half, which under the premultiplied blend halves the
/// sprite and leaves half of what is under it.
constexpr float translucent_level = 0.5F;

/// Highest value of a colour's channel.
constexpr float channel_full = 255.0F;

/// The GAF frame each rendered frame of a list was decoded from this
/// frame, for the frames decoded anew each frame (WorldDrawList::decoded).
using DecodedFrom = std::unordered_map<const RenderedFrame*, const oa::formats::gaf::Frame*>;

/// Returns the key a sprite's picture is held under on the pages: the
/// address of the GAF frame it was decoded from, for a frame the list
/// decoded for this frame's draws alone, which lives in the match's
/// archives; else the address of the rendered frame itself, which the
/// match keeps for a feature's animation.
///
/// @param frame the rendered frame drawn
/// @param decoded_from the frames decoded this frame, by their rendered frame
/// @return the key
uint64_t frame_key(const RenderedFrame* frame, const DecodedFrom& decoded_from) {
    if (const auto found = decoded_from.find(frame); found != decoded_from.end())
        return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(found->second)) | decoded_key_bit;
    return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(frame));
}

/// What tells one batch of the stage from the next.
struct BatchKey {
    card::PageHandle page{};
    card::Blend blend{card::Blend::none};
    card::Sampling sampling{card::Sampling::nearest};

    friend bool operator==(const BatchKey&, const BatchKey&) = default;
};

/// Appends the stage's quads to a frame, consecutive quads that share
/// their key in one batch.
class Emitter {
  public:

    /// Readies the emitter for a view.
    ///
    /// @param frame the frame appended to
    /// @param view the view every batch draws through
    Emitter(card::CardFrame& frame, const SceneView& view) : frame_(frame), view_(view) {
        scissor_.x = static_cast<int32_t>(std::lround(view.origin_x));
        scissor_.y = static_cast<int32_t>(std::lround(view.origin_y));
        scissor_.width =
            static_cast<int32_t>(std::lround(static_cast<double>(view.width) * view.scale));
        scissor_.height =
            static_cast<int32_t>(std::lround(static_cast<double>(view.height) * view.scale));
    }

    /// Returns a scene pixel column's place in the target.
    ///
    /// @param x scene pixel column, at the zoom
    /// @return the target pixel column
    [[nodiscard]] float place_x(float x) const noexcept {
        return view_.origin_x + (x - static_cast<float>(view_.offset.x) * view_.zoom) * view_.scale;
    }

    /// Returns a scene pixel row's place in the target.
    ///
    /// @param y scene pixel row, at the zoom
    /// @return the target pixel row
    [[nodiscard]] float place_y(float y) const noexcept {
        return view_.origin_y + (y - static_cast<float>(view_.offset.y) * view_.zoom) * view_.scale;
    }

    /// Appends an axis-aligned quad, in target pixels, to the batch of a
    /// key, opening one when the last batch has another key.
    ///
    /// @param key the batch's page, blend and sampling
    /// @param x the left edge
    /// @param y the top edge
    /// @param width pixels across
    /// @param height pixels down
    /// @param u0 the left edge's texture coordinate
    /// @param v0 the top edge's
    /// @param u1 the right edge's
    /// @param v1 the bottom edge's
    /// @param colour the colour of every corner
    void quad(
        const BatchKey& key,
        float x,
        float y,
        float width,
        float height,
        float u0,
        float v0,
        float u1,
        float v1,
        const card::Colour& colour
    ) {
        open(key);
        card::append_quad(frame_, x, y, width, height, u0, v0, u1, v1, colour);
        frame_.batches.back().index_count += indices_per_quad;
    }

    /// Appends a line's quad (append_line_quad) to the untextured batch.
    ///
    /// @param x0 the first pixel's centre column, in target pixels
    /// @param y0 its centre row
    /// @param x1 the last pixel's centre column
    /// @param y1 its centre row
    /// @param width pixels across the line
    /// @param colour the line's colour
    void line(float x0, float y0, float x1, float y1, float width, const card::Colour& colour) {
        open(BatchKey{});
        append_line_quad(frame_, x0, y0, x1, y1, width, colour);
        frame_.batches.back().index_count += indices_per_quad;
    }

    /// Returns the batches opened.
    ///
    /// @return the count
    [[nodiscard]] uint32_t batches() const noexcept { return batches_; }

  private:

    /// Indices a quad adds: two triangles.
    static constexpr uint32_t indices_per_quad = 6;

    /// Opens a batch of a key unless the last batch opened has it.
    ///
    /// @param key the batch's page, blend and sampling
    void open(const BatchKey& key) {
        if (open_ && key_ == key)
            return;
        card::Batch batch;
        batch.operation = card::Operation::draw;
        batch.target = view_.target;
        batch.page = key.page;
        batch.level = 0;
        batch.blend = key.blend;
        batch.sampling = key.sampling;
        batch.scissored = true;
        batch.scissor = scissor_;
        batch.first_index = static_cast<card::Index>(frame_.indices.size());
        batch.index_count = 0;
        frame_.batches.push_back(batch);
        open_ = true;
        key_ = key;
        ++batches_;
    }

    card::CardFrame& frame_;
    const SceneView& view_;
    card::Rect scissor_{};
    bool open_{};
    BatchKey key_{};
    uint32_t batches_{};
};

/// A sprite placed on the pages this frame, to be found again after the
/// frame's draws are emitted when the pages evicted frames meanwhile.
struct Placed {
    uint64_t key{};
    gpu::DrawMode mode{};
    gpu::FrameRecord record{};
};

} // namespace

CellFog cell_fog(const SightView& sight, int32_t map_x, int32_t map_z) noexcept {
    if (sight.width <= 0 || sight.height <= 0 || map_x < 0 || map_z < 0)
        return CellFog::seen;
    const int32_t cell_x = map_x / oa::present::world_renderer::fog_cell_pixels;
    const int32_t cell_z = map_z / oa::present::world_renderer::fog_cell_pixels;
    if (cell_x >= sight.width || cell_z >= sight.height)
        return CellFog::seen;
    const auto index = static_cast<std::size_t>(cell_z) * static_cast<std::size_t>(sight.width) +
                       static_cast<std::size_t>(cell_x);
    const bool in_sight = index < sight.coverage.size() && sight.coverage[index] != 0;
    if (!in_sight && sight.line_of_sight)
        return CellFog::unseen;
    const bool mapped = !sight.mapping || (index < sight.player_bits.size() &&
                                           (sight.player_bits[index] & sight.viewer_bit) != 0);
    return mapped ? CellFog::seen : CellFog::unmapped;
}

uint16_t card_kinds(uint8_t stages) noexcept {
    uint16_t kinds = 0;
    if ((stages & stage_sprites) != 0)
        kinds = static_cast<uint16_t>(
            kinds | card_kind_bit(WorldDrawKind::sprite) |
            card_kind_bit(WorldDrawKind::blended_sprite) |
            card_kind_bit(WorldDrawKind::pixel_square) | card_kind_bit(WorldDrawKind::line) |
            card_kind_bit(WorldDrawKind::selection_line)
        );
    return kinds;
}

card::Sampling sprite_sampling(float zoom) noexcept {
    if (std::floor(zoom) == zoom)
        return card::Sampling::nearest;
    return zoom < 1.0F ? card::Sampling::linear : card::Sampling::pixel_art;
}

float line_width(const SceneView& view) noexcept {
    return std::max(1.0F, view.zoom) * view.scale;
}

card::Colour
flat_colour(const std::array<uint8_t, 3>& rgb, const std::array<uint8_t, 256>* gamma) noexcept {
    const auto shown = [&](uint8_t channel) {
        return static_cast<float>(gamma != nullptr ? (*gamma)[channel] : channel) / channel_full;
    };
    return {shown(rgb[0]), shown(rgb[1]), shown(rgb[2]), 1.0F};
}

void append_line_quad(
    card::CardFrame& frame,
    float x0,
    float y0,
    float x1,
    float y1,
    float width,
    const card::Colour& colour
) {
    float along_x = x1 - x0;
    float along_y = y1 - y0;
    const float length = std::sqrt(along_x * along_x + along_y * along_y);
    if (length > 0.0F) {
        along_x /= length;
        along_y /= length;
    } else {
        // A line of one pixel runs along its row.
        along_x = 1.0F;
        along_y = 0.0F;
    }
    const float half = width * 0.5F;
    const float start_x = x0 - along_x * half;
    const float start_y = y0 - along_y * half;
    const float end_x = x1 + along_x * half;
    const float end_y = y1 + along_y * half;
    const float across_x = -along_y * half;
    const float across_y = along_x * half;
    const auto first = static_cast<card::Index>(frame.vertices.size());
    frame.vertices.push_back({start_x + across_x, start_y + across_y, colour, 0.0F, 0.0F});
    frame.vertices.push_back({end_x + across_x, end_y + across_y, colour, 0.0F, 0.0F});
    frame.vertices.push_back({end_x - across_x, end_y - across_y, colour, 0.0F, 0.0F});
    frame.vertices.push_back({start_x - across_x, start_y - across_y, colour, 0.0F, 0.0F});
    frame.indices.push_back(first);
    frame.indices.push_back(first + 1);
    frame.indices.push_back(first + 2);
    frame.indices.push_back(first);
    frame.indices.push_back(first + 2);
    frame.indices.push_back(first + 3);
}

SpriteStageResult emit_sprites(
    const SpriteStageInputs& inputs,
    gpu::SpritePages& pages,
    const SpritePageHooks& hooks,
    card::CardFrame& frame
) {
    SpriteStageResult result;
    if (inputs.list == nullptr || inputs.view.width <= 0 || inputs.view.height <= 0)
        return result;
    const WorldDrawList& list = *inputs.list;
    const SceneView& view = inputs.view;
    // Where the frame stood before the stage, to take the stage back.
    const std::size_t vertices_before = frame.vertices.size();
    const std::size_t indices_before = frame.indices.size();
    const std::size_t batches_before = frame.batches.size();
    const uint64_t evictions_before = pages.statistics().evictions;
    DecodedFrom decoded_from;
    for (const auto& [source, decoded] : list.decoded_of)
        if (decoded != nullptr)
            decoded_from.emplace(decoded, source);
    std::vector<Placed> placed;
    placed.reserve(list.sprites.size());
    Emitter emitter(frame, view);
    const card::Sampling sampling = sprite_sampling(view.zoom);
    const float zoom = view.zoom;
    const float width = line_width(view);
    const bool greys = pages.has_gray_table();
    const card::Colour opaque{1.0F, 1.0F, 1.0F, 1.0F};
    const card::Colour translucent{
        translucent_level, translucent_level, translucent_level, translucent_level
    };
    const auto emit_sprite = [&](const SpriteDraw& sprite, bool blended) {
        if (sprite.frame == nullptr || hooks.card_page == nullptr) {
            ++result.refused;
            return;
        }
        // The fog's state where the sprite is drawn: the map pixel under
        // its screen point, as the fog lays its tiles by map column and row
        // alone, whatever height the point was lifted by.
        const CellFog fog = cell_fog(
            inputs.sight,
            view.camera_x +
                static_cast<int32_t>(std::floor(static_cast<float>(sprite.screen.x) / zoom)),
            view.camera_y +
                static_cast<int32_t>(std::floor(static_cast<float>(sprite.screen.y) / zoom))
        );
        const bool greyed = fog == CellFog::unseen && greys;
        const auto mode = greyed ? gpu::DrawMode::greyed : gpu::DrawMode::opaque;
        const uint64_t key = frame_key(sprite.frame, decoded_from);
        const gpu::FrameResult found = pages.frame(key, mode, *sprite.frame);
        if (found.status != gpu::FrameStatus::ok) {
            ++result.refused;
            return;
        }
        const card::PageHandle page = hooks.card_page(hooks.context, found.record.page);
        if (page == card::PageHandle{}) {
            ++result.refused;
            return;
        }
        placed.push_back({key, mode, found.record});
        const auto size = static_cast<float>(pages.pages()[found.record.page].size);
        const gpu::TexelRect& rect = found.record.rect;
        const float left =
            static_cast<float>(sprite.screen.x) - static_cast<float>(sprite.frame->origin_x) * zoom;
        const float top =
            static_cast<float>(sprite.screen.y) - static_cast<float>(sprite.frame->origin_y) * zoom;
        emitter.quad(
            {page, card::Blend::alpha_premultiplied, sampling},
            emitter.place_x(left),
            emitter.place_y(top),
            static_cast<float>(sprite.frame->width) * zoom * view.scale,
            static_cast<float>(sprite.frame->height) * zoom * view.scale,
            static_cast<float>(rect.x) / size,
            static_cast<float>(rect.y) / size,
            static_cast<float>(rect.x + rect.width) / size,
            static_cast<float>(rect.y + rect.height) / size,
            blended ? translucent : opaque
        );
        ++result.sprites;
        if (greyed)
            ++result.greyed;
    };
    for (const WorldDraw& draw : list.draws) {
        switch (draw.kind) {
        case WorldDrawKind::sprite:
        case WorldDrawKind::blended_sprite:
            if (draw.index < list.sprites.size())
                emit_sprite(list.sprites[draw.index], draw.kind == WorldDrawKind::blended_sprite);
            break;
        case WorldDrawKind::pixel_square: {
            if (draw.index >= list.squares.size())
                break;
            const SquareDraw& square = list.squares[draw.index];
            if (square.right <= square.left || square.bottom <= square.top)
                break;
            emitter.quad(
                BatchKey{},
                emitter.place_x(static_cast<float>(square.left)),
                emitter.place_y(static_cast<float>(square.top)),
                static_cast<float>(square.right - square.left) * view.scale,
                static_cast<float>(square.bottom - square.top) * view.scale,
                0.0F,
                0.0F,
                0.0F,
                0.0F,
                flat_colour(square.color, inputs.gamma)
            );
            ++result.squares;
            break;
        }
        case WorldDrawKind::line: {
            if (draw.index >= list.lines.size())
                break;
            const LineDraw& line = list.lines[draw.index];
            emitter.line(
                emitter.place_x(static_cast<float>(line.x0) + 0.5F),
                emitter.place_y(static_cast<float>(line.y0) + 0.5F),
                emitter.place_x(static_cast<float>(line.x1) + 0.5F),
                emitter.place_y(static_cast<float>(line.y1) + 0.5F),
                width,
                flat_colour(line.color, inputs.gamma)
            );
            ++result.lines;
            break;
        }
        case WorldDrawKind::selection_line: {
            if (draw.index >= list.lines.size() || inputs.palette == nullptr)
                break;
            // The line's pixels are map pixels about the camera, which the
            // bridge writes back at the zoom.
            const LineDraw& line = list.lines[draw.index];
            const auto entry = static_cast<std::size_t>(line.palette_index) * 4U;
            const std::array<uint8_t, 3> rgb{
                (*inputs.palette)[entry], (*inputs.palette)[entry + 1], (*inputs.palette)[entry + 2]
            };
            emitter.line(
                emitter.place_x((static_cast<float>(line.x0) + 0.5F) * zoom),
                emitter.place_y((static_cast<float>(line.y0) + 0.5F) * zoom),
                emitter.place_x((static_cast<float>(line.x1) + 0.5F) * zoom),
                emitter.place_y((static_cast<float>(line.y1) + 0.5F) * zoom),
                width,
                flat_colour(rgb, inputs.gamma)
            );
            ++result.lines;
            break;
        }
        case WorldDrawKind::commit:
        case WorldDrawKind::commit_always:
        case WorldDrawKind::model:
        case WorldDrawKind::projectile:
        case WorldDrawKind::debris:
        case WorldDrawKind::fragment:
            break;
        }
    }
    result.batches = emitter.batches();
    // The pages evict least recently used first, so a frame placed this
    // frame goes only once every older one has: when the frame's distinct
    // sprites exceed the pages' memory. A cell drawn from after its frame
    // left it would show another sprite, so the stage then draws nothing.
    if (pages.statistics().evictions != evictions_before)
        for (const Placed& sprite : placed) {
            const gpu::FrameResult held = pages.find(sprite.key, sprite.mode);
            if (held.status == gpu::FrameStatus::ok && held.record.page == sprite.record.page &&
                held.record.rect.x == sprite.record.rect.x &&
                held.record.rect.y == sprite.record.rect.y)
                continue;
            frame.vertices.resize(vertices_before);
            frame.indices.resize(indices_before);
            frame.batches.resize(batches_before);
            result = {};
            result.pages_overflowed = true;
            break;
        }
    return result;
}

} // namespace oa::app::full
