// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/present/model/unit_supersampling.hpp"

#include <algorithm>
#include <array>
#include <memory>

namespace oa::present::model {
namespace {

// The levels from the finest down.
constexpr std::array<UnitSupersampling, 6> levels_down{
    UnitSupersampling::x16,
    UnitSupersampling::x8,
    UnitSupersampling::x4,
    UnitSupersampling::x3,
    UnitSupersampling::x2,
    UnitSupersampling::off,
};

// The finer draw state of a model's draw state, made when first asked for.
FinerModel& finer_model(ModelState& state) {
    if (!state.finer)
        state.finer = std::make_unique<FinerModel>();
    return *state.finer;
}

// The renderer's own callbacks and their context, which a finer draw passes
// its callbacks through.
struct FinerLinks {
    void* user{};
    int32_t (*ground_height)(void* user, const FixedVec3& position){};
    ModelRef (*model_of)(void* user, const Unit& unit){};
};

// Returns the terrain height through the renderer's own callback.
int32_t finer_ground_height(void* user, const FixedVec3& position) {
    const auto& links = *static_cast<const FinerLinks*>(user);
    return links.ground_height(links.user, position);
}

// Returns a carried unit's model with its finer draw state in place of its
// own, so that a finer draw builds the carried unit's image there and leaves
// the game's image alone. That image holds the unit as carried, so the
// unit's own finer draws build theirs again.
ModelRef finer_model_of(void* user, const Unit& unit) {
    const auto& links = *static_cast<const FinerLinks*>(user);
    ModelRef model = links.model_of(links.user, unit);
    if (model.state != nullptr) {
        FinerModel& finer = finer_model(*model.state);
        finer.samples = 0;
        model.state = &finer.state;
    }
    return model;
}

// Sets a renderer up to draw finer over a sampled region, and puts it back
// as it was when it goes.
class FinerRenderer {
  public:

    FinerRenderer(ModelRenderer& renderer, const SampledRegion& sampled)
        : renderer_(renderer), links_{renderer.user, renderer.ground_height, renderer.model_of},
          samples_(renderer.samples), origin_x_(renderer.origin_x), origin_y_(renderer.origin_y) {
        const auto factor = static_cast<int32_t>(sampled.factor);
        renderer.samples = sampled.factor;
        renderer.origin_x = (origin_x_ - sampled.region.x1) * factor;
        renderer.origin_y = (origin_y_ - sampled.region.y1) * factor;
        renderer.user = &links_;
        renderer.ground_height = links_.ground_height != nullptr ? finer_ground_height : nullptr;
        renderer.model_of = links_.model_of != nullptr ? finer_model_of : nullptr;
    }

    FinerRenderer(const FinerRenderer&) = delete;
    FinerRenderer& operator=(const FinerRenderer&) = delete;

    ~FinerRenderer() {
        renderer_.samples = samples_;
        renderer_.origin_x = origin_x_;
        renderer_.origin_y = origin_y_;
        renderer_.user = links_.user;
        renderer_.ground_height = links_.ground_height;
        renderer_.model_of = links_.model_of;
    }

  private:

    ModelRenderer& renderer_;
    FinerLinks links_{};
    uint32_t samples_{};
    int32_t origin_x_{};
    int32_t origin_y_{};
};

// Game pixels added around the region a unit's draw covers, for the
// rounding of places drawn finer.
constexpr int32_t drawn_region_margin = 2;

// Grows a region to hold a rectangle.
void include_rect(Rect32& region, int32_t x, int32_t y, int32_t width, int32_t height) noexcept {
    region.x1 = std::min(region.x1, x);
    region.y1 = std::min(region.y1, y);
    region.x2 = std::max(region.x2, x + width);
    region.y2 = std::max(region.y2, y + height);
}

// The region of the bridge, in its 8-bit pixels, a unit's draw covers: its
// model, its cached image and building silhouette, its ground shadow and the
// units it carries, from its current transforms, with drawn_region_margin
// around them.
Rect32 drawn_region(const ModelRenderer& renderer, const ModelRef& model) {
    const Unit& unit = *model.unit;
    const auto high = [](int32_t value) { return value >> 16; };
    const auto camera = [](int32_t pixels) {
        return static_cast<int32_t>(static_cast<uint32_t>(pixels) << 16);
    };
    const int32_t x = high(
                          static_cast<int32_t>(
                              static_cast<uint32_t>(unit.position.x) -
                              static_cast<uint32_t>(camera(renderer.camera_x))
                          )
                      ) +
                      renderer.origin_x;
    const int32_t z = high(
                          static_cast<int32_t>(
                              static_cast<uint32_t>(unit.position.z) -
                              static_cast<uint32_t>(camera(renderer.camera_y))
                          )
                      ) +
                      renderer.origin_y;
    const int32_t unit_y = z - (high(unit.position.y) >> 1);
    const int32_t ground = renderer.ground_height != nullptr
                               ? renderer.ground_height(renderer.user, unit.position)
                               : 0;
    const int32_t shadow_x = x + shadow_offset_x;
    const int32_t shadow_y = z - (ground >> 1);
    Rect32 region{x, unit_y, x, unit_y};
    const ImageFrame frame = measure_model_bounds(*model.instance, nullptr);
    include_rect(region, x - frame.origin_x, unit_y - frame.origin_y, frame.width, frame.height);
    include_rect(
        region, shadow_x - frame.origin_x, shadow_y - frame.origin_y, frame.width, frame.height
    );
    const ImageFrame shear = measure_shadow_bounds(*model.instance);
    include_rect(
        region, shadow_x - shear.origin_x, shadow_y - shear.origin_y, shear.width, shear.height
    );
    for (const Sprite* sprite : {&model.state->image.sprite, &model.state->shadow.sprite}) {
        if (sprite->data == nullptr)
            continue;
        include_rect(
            region, x - sprite->origin_x, unit_y - sprite->origin_y, sprite->width, sprite->height
        );
        include_rect(
            region,
            shadow_x - sprite->origin_x,
            shadow_y - sprite->origin_y,
            sprite->width,
            sprite->height
        );
    }
    ModelBounds carried{};
    bool carries = false;
    for (const Unit* child = world_unit(renderer.world, unit.attach_first_child); child != nullptr;
         child = world_unit(renderer.world, child->attach_next)) {
        if (renderer.model_of == nullptr)
            break;
        const ModelRef cargo = renderer.model_of(renderer.user, *child);
        if (cargo.instance == nullptr)
            continue;
        expand_model_bounds(
            carried,
            *cargo.instance,
            static_cast<int32_t>(
                static_cast<uint32_t>(cargo.unit->position.x) -
                static_cast<uint32_t>(unit.position.x)
            ),
            static_cast<int32_t>(
                static_cast<uint32_t>(cargo.unit->position.y) -
                static_cast<uint32_t>(unit.position.y)
            ),
            static_cast<int32_t>(
                static_cast<uint32_t>(cargo.unit->position.z) -
                static_cast<uint32_t>(unit.position.z)
            )
        );
        carries = true;
    }
    if (carries)
        include_rect(
            region,
            x + carried.left,
            unit_y + carried.top,
            carried.right - carried.left,
            carried.bottom - carried.top
        );
    return {
        region.x1 - drawn_region_margin,
        region.y1 - drawn_region_margin,
        region.x2 + drawn_region_margin,
        region.y2 + drawn_region_margin
    };
}

// Draws a unit as draw_linked_model draws it at UnitSupersampling::off.
void draw_unit_plain(
    ModelRenderer& renderer,
    RgbBridge& bridge,
    const ModelRef& model,
    const Rect32& region,
    bool movement_idle,
    bool& bridge_holds_draws
) {
    model.state->finer.reset();
    bridge_open(bridge, region);
    draw_linked_model(renderer, &bridge.surface, model, movement_idle);
    bridge_holds_draws = true;
}

} // namespace

std::optional<UnitSupersampling> unit_supersampling_from_factor(uint32_t factor) noexcept {
    switch (factor) {
    case 1:
        return UnitSupersampling::off;
    case 2:
        return UnitSupersampling::x2;
    case 3:
        return UnitSupersampling::x3;
    case 4:
        return UnitSupersampling::x4;
    case 8:
        return UnitSupersampling::x8;
    case 16:
        return UnitSupersampling::x16;
    default:
        return std::nullopt;
    }
}

UnitSupersampling fitting_supersampling(const Rect32& region, UnitSupersampling level) noexcept {
    if (region.x1 > region.x2 || region.y1 > region.y2)
        return UnitSupersampling::off;
    const auto pixels = static_cast<uint64_t>(region.x2 - region.x1 + 1) *
                        static_cast<uint64_t>(region.y2 - region.y1 + 1);
    for (const UnitSupersampling candidate : levels_down) {
        const uint64_t factor = supersampling_factor(candidate);
        if (factor <= supersampling_factor(level) &&
            pixels * factor * factor <= supersample_max_samples)
            return candidate;
    }
    return UnitSupersampling::off;
}

void draw_unit_supersampled(
    ModelRenderer& renderer,
    RgbBridge& bridge,
    SupersampleScratch& scratch,
    const ModelRef& model,
    const Rect32& region,
    bool movement_idle,
    UnitSupersampling level,
    bool& bridge_holds_draws
) {
    const Rect32 clipped{
        std::max(region.x1, 0),
        std::max(region.y1, 0),
        std::min(region.x2, bridge.surface.width - 1),
        std::min(region.y2, bridge.surface.height - 1)
    };
    const UnitSupersampling fitting =
        level == UnitSupersampling::off ? level : fitting_supersampling(clipped, level);
    if (fitting == UnitSupersampling::off) {
        draw_unit_plain(renderer, bridge, model, region, movement_idle, bridge_holds_draws);
        return;
    }
    const LinkedDraw draw = prepare_linked_draw(renderer, model, movement_idle);
    if (!draw.drawn)
        return;
    // Everything the bridge holds goes to the frame first, so that the unit
    // draws over it: the draws it notes, and lines drawn into the bridge
    // without being noted, such as the unit's own selection box.
    bridge_end(bridge);
    bridge_holds_draws = false;
    // The unit's transforms are now those it draws with, so its draw covers
    // no more than drawn_region, which is far smaller than the region the
    // caller allows from the last transforms.
    const Rect32 drawn = drawn_region(renderer, model);
    const Rect32 covered{
        std::max(clipped.x1, drawn.x1),
        std::max(clipped.y1, drawn.y1),
        std::min(clipped.x2, drawn.x2),
        std::min(clipped.y2, drawn.y2)
    };
    SampledRegion& sampled = scratch.region;
    bridge_open_sampled(bridge, sampled, covered, supersampling_factor(fitting));
    ModelState& state = *model.state;
    FinerModel& finer = finer_model(state);
    ModelRef finer_ref = model;
    finer_ref.state = &finer.state;
    {
        const FinerRenderer finer_renderer(renderer, sampled);
        if (state.image.sprite.data == nullptr) {
            finer.state.image = {};
            finer.state.shadow = {};
        } else if (
            finer.samples != sampled.factor || finer.image_builds != state.image_builds ||
            finer.state.image.sprite.data == nullptr
        ) {
            finer.state.shadow = {};
            prepare_model_image(renderer, finer_ref, false, pass_cached_pieces);
            finer.samples = sampled.factor;
            finer.image_builds = state.image_builds;
        }
        draw_model(
            renderer,
            &sampled.surface,
            finer_ref,
            static_cast<int32_t>(static_cast<uint32_t>(renderer.camera_x) << 16),
            static_cast<int32_t>(static_cast<uint32_t>(renderer.camera_y) << 16),
            draw.unlit
        );
    }
    bridge_end_sampled(bridge, sampled);
}

} // namespace oa::present::model
