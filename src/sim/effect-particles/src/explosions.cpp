// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/effect_particles.hpp"

#include "oa/base/geometry.hpp"

#include <bit>
#include <cmath>

namespace oa::sim::effect_particles {
namespace {
constexpr double flash_scale = 32.0;
constexpr double flash_row_weight = 1.33;
constexpr int32_t flash_noise_span = 10;
constexpr uint8_t flash_transparent = 0xffU;
constexpr uint8_t flash_rim = 0x6eU;
constexpr uint8_t flash_shade_base = 0x4fU;
constexpr uint8_t flash_levels = 0x20U;
constexpr uint16_t flash_frame_hold = 2;
constexpr int32_t lcg_rand_range = 0x8000;
constexpr int32_t explosion_smoke_interval = 7;
constexpr int32_t explosion_smoke_duration = 15;
constexpr int32_t large_flash = 2; // COB explode bitmaps
constexpr int32_t small_flash = 0; // landing debris and fragments
constexpr int32_t no_flash = -1;
constexpr int32_t debris_rest_speed = 0x1ffff;
constexpr uint32_t cob_bitmap_first = 0x100U;
constexpr uint32_t cob_bitmap_count = 6;
// Shattering's draws: each axis of the fling is (80 - rand(160)) * 512, the
// spin rates 800 - rand(1600), and the lean along the quad's normal
// rand(200) times the normal component scaled by 512.
constexpr uint32_t fling_range = 0xa0;
constexpr int32_t fling_centre = 0x50;
constexpr int32_t fling_step = 0x200;
constexpr int32_t fling_lift_ticks = 30;
constexpr uint32_t spin_range = 0x640;
constexpr int32_t spin_centre = 800;
constexpr uint32_t lean_range = 200;
constexpr double lean_scale = 512.0;
constexpr double thickness_scale = 65535.0; // one unit less a step
// 16.16 to units by 1/65535, rounded to single.
constexpr float fixed_to_units = std::bit_cast<float>(0x37800080U);
constexpr int32_t fixed_unit = 0x10000;

int32_t wrap_add(int32_t a, int32_t b) noexcept {
    return std::bit_cast<int32_t>(std::bit_cast<uint32_t>(a) + std::bit_cast<uint32_t>(b));
}

int32_t wrap_sub(int32_t a, int32_t b) noexcept {
    return std::bit_cast<int32_t>(std::bit_cast<uint32_t>(a) - std::bit_cast<uint32_t>(b));
}

int32_t wrap_mul(int32_t a, int32_t b) noexcept {
    return std::bit_cast<int32_t>(std::bit_cast<uint32_t>(a) * std::bit_cast<uint32_t>(b));
}

int32_t whole_units(int32_t fixed) noexcept {
    return static_cast<int16_t>(static_cast<uint16_t>(std::bit_cast<uint32_t>(fixed) >> 16));
}

int32_t sea_level_fixed(const oa::Game& game) noexcept {
    return static_cast<int32_t>(static_cast<uint32_t>(game.sea_level) << 16);
}

// Truncation toward zero; 0x80000000 when out of range.
int32_t truncate_to_int(double value) noexcept {
    if (!(value > -2147483649.0 && value < 2147483648.0))
        return std::bit_cast<int32_t>(0x80000000U);
    return static_cast<int32_t>(value);
}

/// Truncates a double toward zero and keeps the low 32 bits of the result.
///
/// @param value value to truncate
/// @return the low 32 bits of the truncated value; 0 for a NaN or a value outside
///         the signed 64-bit range
int32_t truncate_low(double value) noexcept {
    if (!(value > -9223372036854775808.0 && value < 9223372036854775808.0))
        return 0;
    return static_cast<int32_t>(
        static_cast<uint32_t>(static_cast<uint64_t>(static_cast<int64_t>(value)))
    );
}

int16_t add_angle(int16_t angle, int32_t rate) noexcept {
    return static_cast<int16_t>(static_cast<uint16_t>(angle) + static_cast<uint16_t>(rate));
}

const formats::gaf::Sequence* fx(const EffectWorld& world, Fx entry) noexcept {
    return world.fx[static_cast<uint32_t>(entry)];
}

const formats::gaf::Sequence* splash(const EffectWorld& world) noexcept {
    return fx(world, world.lava_world ? Fx::lavasplash : Fx::h2oboom2);
}

bool record_spent(const ExplosionRecord& record) noexcept {
    return record.fragment == no_fragment && !record.sprite.active() && !record.flash.active();
}

// Start one of a record's spans. Every entry the table animates had its loop
// byte cleared when loaded (FX.GAF and the weapon explosions), so no span repeats.
void start_span(
    sim::sprite_animation::Cursor& span, const formats::gaf::Sequence* sequence
) noexcept {
    sim::sprite_animation::initialize(span, sequence, 0);
    span.repeat_flag = 0;
}

uint32_t synced(const EffectHost& host, uint32_t bound) {
    return host.synced_rand(host.context, bound);
}

int32_t fling(const EffectHost& host) {
    const auto draw = static_cast<int32_t>(synced(host, fling_range));
    return (fling_centre - draw) * fling_step;
}

int32_t spin_rate(const EffectHost& host) {
    return spin_centre - static_cast<int32_t>(synced(host, spin_range));
}

// A transformed vertex; an index past the piece's points reads zero (the
// model loader rejects such indices).
FixedVec3 piece_point(const ShatterPiece& piece, uint16_t index) noexcept {
    return index < piece.point_count ? piece.points[index] : FixedVec3{};
}

base::geometry::Vec3f in_units(const FixedVec3& point) noexcept {
    const auto scale = static_cast<double>(fixed_to_units);
    return {
        static_cast<float>(static_cast<double>(point.x) * scale),
        static_cast<float>(static_cast<double>(point.y) * scale),
        static_cast<float>(static_cast<double>(point.z) * scale)
    };
}

// The quad's corners, then the same corners in reverse as the back face.
void cut_slab(
    ShatterFragment& fragment, const ShatterPiece& piece, const PiecePrimitive& quad
) noexcept {
    for (uint32_t corner = 0; corner < fragment_face_corners; ++corner) {
        const auto point = piece_point(piece, quad.vertex_indices[corner]);
        fragment.points[corner] = point;
        fragment.points[fragment_point_count - 1 - corner] = point;
    }
}

// The eight points about their truncated mean.
void centre_slab(ShatterFragment& fragment) noexcept {
    int32_t sum[3]{};
    for (const auto& point : fragment.points) {
        sum[0] = wrap_add(sum[0], point.x);
        sum[1] = wrap_add(sum[1], point.y);
        sum[2] = wrap_add(sum[2], point.z);
    }
    const auto count = static_cast<int32_t>(fragment_point_count);
    const FixedVec3 mean{sum[0] / count, sum[1] / count, sum[2] / count};
    for (auto& point : fragment.points)
        point = {wrap_sub(point.x, mean.x), wrap_sub(point.y, mean.y), wrap_sub(point.z, mean.z)};
}

void release_fragment(ExplosionRecord& record, EffectWorld& world) noexcept {
    world.fragments[record.fragment].live = false;
    record.fragment = no_fragment;
}

// The fragment half of tick_explosions' record step.
void fly_fragment(
    ExplosionRecord& record, EffectWorld& world, const oa::Game& game, const EffectHost& host
) {
    const auto from = record.position;
    record.position = {
        wrap_add(record.position.x, wrap_add(record.velocity.x, record.carried.x)),
        wrap_add(record.position.y, wrap_add(record.carried.y, record.velocity.y)),
        wrap_add(record.position.z, wrap_add(record.velocity.z, record.carried.z))
    };
    record.velocity.y = wrap_sub(record.velocity.y, game.gravity);
    for (int32_t axis = 0; axis < 3; ++axis)
        record.spin[axis] = add_angle(record.spin[axis], record.spin_rate[axis]);
    const auto ground = host.ground_height(host.context, record.position);
    const auto sea = static_cast<int32_t>(game.sea_level);
    if (sea_level_fixed(game) < record.position.y || sea <= ground) {
        if (whole_units(record.position.y) > ground)
            return;
        record.position = from;
        record.velocity.y /= -2;
        if (whole_units(record.velocity.y) > 0)
            return;
        if (record.explodes)
            log_explosion(
                world, game, host, record.position, fx(world, Fx::explosion), small_flash, false
            );
        release_fragment(record, world);
        return;
    }
    if (record.explodes && !world.no_sea_level_trigger)
        log_explosion(world, game, host, record.position, splash(world), no_flash, true);
    release_fragment(record, world);
}

// Drop the first spent record by moving every later one down, until none is
// spent. The vacated tail keeps its stale contents.
void drop_spent_records(EffectWorld& world) noexcept {
    for (;;) {
        int32_t spent = 0;
        while (spent < world.explosion_count && !record_spent(world.explosions[spent]))
            ++spent;
        if (spent >= world.explosion_count)
            return;
        for (int32_t i = spent; i < world.explosion_count - 1; ++i)
            world.explosions[i] = world.explosions[i + 1];
        --world.explosion_count;
    }
}

void move_debris(DebrisPiece& piece, const oa::Game& game) noexcept {
    piece.position = {
        wrap_add(piece.position.x, piece.velocity.x),
        wrap_add(piece.position.y, piece.velocity.y),
        wrap_add(piece.position.z, piece.velocity.z)
    };
    piece.spin[0] = add_angle(piece.spin[0], piece.spin_rate[1]);
    piece.spin[1] = add_angle(piece.spin[1], piece.spin_rate[2]);
    piece.spin[2] = add_angle(piece.spin[2], piece.spin_rate[0]);
    if ((piece.flags & debris_fall) != 0)
        piece.velocity.y = wrap_sub(piece.velocity.y, game.gravity);
}
} // namespace

int32_t build_flash_frame(int32_t size, uint8_t* shade, const EffectHost& host) {
    const auto centre = static_cast<double>(size / 2);
    for (int32_t row = 0; row < size; ++row) {
        const auto dy = centre - static_cast<double>(row);
        const auto row_term = dy * dy * flash_row_weight;
        for (int32_t column = 0; column < size; ++column) {
            const auto dx = centre - static_cast<double>(column);
            const auto distance = std::sqrt(dx * dx + row_term);
            const auto noise = static_cast<int32_t>(
                static_cast<int64_t>(host.lcg_rand(host.context)) * flash_noise_span /
                lcg_rand_range
            );
            const auto level =
                truncate_to_int((static_cast<double>(noise) + distance) / centre * flash_scale);
            const auto value = static_cast<uint8_t>(flash_levels - static_cast<uint8_t>(level));
            auto& pixel = shade[row * size + column];
            if (value >= flash_levels + 2)
                pixel = flash_transparent;
            else if (value >= flash_levels)
                pixel = flash_rim;
            else
                pixel = static_cast<uint8_t>(value + flash_shade_base);
        }
    }
    return truncate_to_int(centre);
}

int32_t flash_frame_side(const FlashTierShape& shape, int32_t frame) noexcept {
    if (shape.count <= 0)
        return 0;
    const auto step = (shape.end_size - shape.start_size) / shape.count;
    return shape.start_size + frame * step;
}

void build_flash_tier(
    formats::gaf::Sequence& tier, const FlashTierShape& shape, const EffectHost& host
) {
    tier.repeat_flags = 0;
    const auto frames = static_cast<int32_t>(tier.frames.size());
    for (int32_t index = 0; index < shape.count && index < frames; ++index) {
        auto& frame = tier.frames[static_cast<size_t>(index)];
        const auto size = flash_frame_side(shape, index);
        const auto side = size > 0 ? size : 0;
        frame.width = static_cast<uint16_t>(side);
        frame.height = static_cast<uint16_t>(side);
        frame.transparency_index = flash_transparent;
        frame.duration = flash_frame_hold;
        const auto area = static_cast<size_t>(side) * static_cast<size_t>(side);
        if (side == 0 || frame.pixels.size() != area)
            continue;
        const auto origin = build_flash_frame(size, frame.pixels.data(), host);
        frame.origin_x = static_cast<int16_t>(origin);
        frame.origin_y = static_cast<int16_t>(origin);
    }
}

void init_explosion_tables(
    EffectWorld& world, formats::gaf::Sequence (&tiers)[flash_tier_count], const EffectHost& host
) {
    world.explosion_count = 0;
    for (int32_t i = 0; i < flash_tier_count; ++i) {
        build_flash_tier(tiers[i], flash_tier_shapes[i], host);
        world.flash_tiers[i] = &tiers[i];
    }
    for (auto& fragment : world.fragments)
        fragment.live = false;
    for (auto& piece : world.debris)
        piece = {};
}

void log_explosion(
    EffectWorld& world,
    const oa::Game& game,
    const EffectHost& host,
    const FixedVec3& position,
    const formats::gaf::Sequence* sprite,
    int32_t flash_tier,
    bool underwater
) {
    if (world.explosion_count >= explosion_capacity)
        return;
    auto& record = world.explosions[world.explosion_count++];
    record.position = position;
    record.sprite = {};
    if (sprite != nullptr)
        start_span(record.sprite, sprite);
    record.flash = {};
    if (flash_tier >= 0 && flash_tier < flash_tier_count)
        start_span(record.flash, world.flash_tiers[flash_tier]);
    record.fragment = no_fragment;
    if (!underwater && whole_units(position.y) > static_cast<int32_t>(game.sea_level))
        spawn_smoke_column(
            world,
            game,
            host,
            position,
            explosion_smoke_interval,
            explosion_smoke_duration,
            layer_smoke
        );
}

void log_cob_explosions(
    EffectWorld& world,
    const oa::Game& game,
    const EffectHost& host,
    const FixedVec3& position,
    uint32_t cob_flags
) {
    for (uint32_t i = 0; i < cob_bitmap_count; ++i)
        if ((cob_flags & (cob_bitmap_first << i)) != 0)
            log_explosion(
                world,
                game,
                host,
                position,
                fx(world, static_cast<Fx>(static_cast<uint32_t>(Fx::explosion) + i)),
                large_flash,
                false
            );
}

bool add_debris(EffectWorld& world, const DebrisPiece& piece) {
    if ((piece.flags & debris_shatter) != 0)
        return false;
    const auto index = find_free_debris_slot(world);
    if (index < 0)
        return false;
    auto& slot = world.debris[index];
    slot = piece;
    slot.live = true;
    return true;
}

int32_t find_free_debris_slot(const EffectWorld& world) noexcept {
    for (int32_t index = 0; index < debris_capacity; ++index)
        if (!world.debris[index].live)
            return index;
    return -1;
}

void settle_all_debris(EffectWorld& world, const oa::Game& game, const EffectHost& host) {
    for (auto& piece : world.debris)
        if (piece.live && !settle_debris(piece, world, game, host))
            piece.live = false;
}

bool settle_debris(
    DebrisPiece& piece, EffectWorld& world, const oa::Game& game, const EffectHost& host
) {
    if (piece.lifetime-- == 0)
        return false;
    if (sea_level_fixed(game) < piece.position.y) {
        const auto vertical = piece.velocity.y;
        const auto ground = host.ground_height(host.context, piece.position);
        if (static_cast<int32_t>(static_cast<uint32_t>(ground) << 16) <
            wrap_add(vertical, piece.position.y)) {
            move_debris(piece, game);
            return true;
        }
        piece.velocity.x >>= 1;
        piece.velocity.z >>= 1;
        piece.velocity.y = -(vertical >> 1);
        if (piece.velocity.y > debris_rest_speed) {
            move_debris(piece, game);
            return true;
        }
        if ((piece.flags & debris_explode_on_landing) == 0)
            return false;
        log_explosion(
            world, game, host, piece.position, fx(world, Fx::explosion), small_flash, false
        );
        return false;
    }
    if ((piece.flags & debris_explode_on_landing) == 0 || world.no_sea_level_trigger)
        return false;
    log_explosion(world, game, host, piece.position, splash(world), no_flash, true);
    return false;
}

int16_t claim_fragment(EffectWorld& world) noexcept {
    for (int16_t slot = 0; slot < fragment_capacity; ++slot)
        if (!world.fragments[slot].live) {
            world.fragments[slot].live = true;
            return slot;
        }
    return no_fragment;
}

void shatter_piece(
    EffectWorld& world,
    const oa::World& state,
    const EffectHost& host,
    const DebrisPiece& request,
    const oa::Unit& unit,
    const sim::unit_movement::Movement* movement,
    const ShatterPiece& piece
) {
    const auto& game = state.game;
    const auto* owner = world_unit_owner(&state, &unit);
    const auto* info = owner != nullptr ? world_player_info(&state, owner) : nullptr;
    const uint8_t team_color = info != nullptr ? info->color : 0;
    const int32_t thickness = request.shatter != 0 ? request.shatter : 1;
    for (uint32_t index = 0; index < piece.primitive_count; ++index) {
        if (world.explosion_count >= explosion_capacity)
            return;
        const auto& quad = piece.primitives[index];
        if (quad.vertex_count != fragment_face_corners || (quad.flags & primitive_colored) != 0 ||
            piece.selection_primitive == static_cast<int32_t>(index))
            continue;
        auto& record = world.explosions[world.explosion_count++];
        record.position = {
            wrap_add(piece.origin.x, unit.position.x),
            wrap_add(piece.origin.y, unit.position.y),
            wrap_add(piece.origin.z, unit.position.z)
        };
        record.fragment = claim_fragment(world);
        if (record.fragment == no_fragment)
            return;
        auto& fragment = world.fragments[record.fragment];
        record.explodes = (request.flags & debris_shatter_explodes) != 0;
        if (movement != nullptr)
            record.carried = {
                movement->velocity[0] >> 1, movement->velocity[1] >> 1, movement->velocity[2] >> 1
            };
        record.velocity.x = fling(host);
        record.velocity.z = fling(host);
        record.velocity.y = wrap_add(fling(host), wrap_mul(game.gravity, fling_lift_ticks));
        for (auto& rate : record.spin_rate)
            rate = spin_rate(host);
        record.spin[0] = record.spin[1] = record.spin[2] = 0;
        record.sprite = {};
        record.flash = {};
        cut_slab(fragment, piece, quad);
        const auto first = in_units(fragment.points[0]);
        const auto second = in_units(fragment.points[1]);
        const auto third = in_units(fragment.points[2]);
        const auto normal = base::geometry::vec3f_normalize(
            base::geometry::vec3f_cross(
                base::geometry::vec3f_sub(second, third), base::geometry::vec3f_sub(second, first)
            )
        );
        const auto lean = [](float component) {
            return static_cast<int16_t>(truncate_low(static_cast<double>(component) * lean_scale));
        };
        const auto lean_x = static_cast<int32_t>(synced(host, lean_range));
        record.velocity.x = wrap_add(record.velocity.x, wrap_mul(lean_x, lean(normal.x)));
        const auto lean_z = static_cast<int32_t>(synced(host, lean_range));
        record.velocity.z = wrap_sub(record.velocity.z, wrap_mul(lean_z, lean(normal.z)));
        const FixedVec3 depth{
            wrap_mul(truncate_low(static_cast<double>(normal.x) * thickness_scale), thickness),
            wrap_mul(truncate_low(static_cast<double>(normal.y) * thickness_scale), thickness),
            wrap_mul(truncate_low(static_cast<double>(normal.z) * thickness_scale), thickness)
        };
        for (uint32_t back = fragment_face_corners; back < fragment_point_count; ++back) {
            auto& point = fragment.points[back];
            point = {
                wrap_sub(point.x, depth.x), wrap_sub(point.y, depth.y), wrap_sub(point.z, depth.z)
            };
        }
        centre_slab(fragment);
        fragment.look = {quad.color, quad.flags, team_color, piece.model, piece.object, index};
        const auto team_texture = primitive_animated | primitive_team;
        if ((quad.flags & (primitive_colored | team_texture)) == team_texture)
            fragment.look.flags &= ~primitive_animated;
    }
}

void tick_explosions(EffectWorld& world, const oa::Game& game, const EffectHost& host) {
    settle_all_debris(world, game, host);
    for (int32_t i = 0; i < world.explosion_count; ++i) {
        auto& record = world.explosions[i];
        if (record.fragment != no_fragment)
            fly_fragment(record, world, game, host);
        if (record.sprite.active())
            (void)sim::sprite_animation::tick(record.sprite);
        if (record.flash.active())
            (void)sim::sprite_animation::tick(record.flash);
    }
    drop_spent_records(world);
}

void draw_explosions(
    const EffectWorld& world, const ExplosionView& view, void* context, ParticleVisitor visit
) {
    if (visit == nullptr)
        return;
    // The record's centre, projected to the screen.
    const auto in_view = [&view](const FixedVec3& position) {
        const int32_t x = whole_units(wrap_sub(position.x, wrap_mul(view.camera_x, fixed_unit))) +
                          battlefield_screen_x;
        const int32_t y = whole_units(wrap_sub(position.z, wrap_mul(view.camera_y, fixed_unit))) -
                          (whole_units(position.y) >> 1) + battlefield_screen_y;
        return x >= view.battlefield.x1 && x <= view.battlefield.x2 && y >= view.battlefield.y1 &&
               y <= view.battlefield.y2;
    };
    for (int32_t i = 0; i < world.explosion_count; ++i) {
        const auto& record = world.explosions[i];
        if (!in_view(record.position) || !record.flash.active())
            continue;
        ParticleDraw item{};
        item.kind = DrawKind::flash;
        item.sight_gated = false;
        item.position = record.position;
        item.sequence = record.flash.sequence;
        item.frame = record.flash.frame_index;
        visit(context, item);
    }
    for (int32_t i = 0; i < world.explosion_count; ++i) {
        const auto& record = world.explosions[i];
        if (!in_view(record.position))
            continue;
        // A record moves only while its fragment flies (fly_fragment).
        const bool flying = record.fragment != no_fragment;
        const FixedVec3 motion = flying ? FixedVec3{wrap_add(record.velocity.x, record.carried.x),
                                                    wrap_add(record.velocity.y, record.carried.y),
                                                    wrap_add(record.velocity.z, record.carried.z)}
                                        : FixedVec3{};
        if (flying) {
            ParticleDraw item{};
            item.kind = DrawKind::fragment;
            item.sight_gated = false;
            item.position = record.position;
            item.fragment = &world.fragments[record.fragment];
            item.spin[0] = record.spin[0];
            item.spin[1] = record.spin[1];
            item.spin[2] = record.spin[2];
            item.motion = motion;
            for (int32_t axis = 0; axis < 3; ++axis)
                item.spin_motion[axis] = static_cast<int16_t>(record.spin_rate[axis]);
            visit(context, item);
        }
        if (!record.sprite.active())
            continue;
        ParticleDraw item{};
        item.kind = DrawKind::sprite;
        item.sight_gated = false;
        item.position = record.position;
        item.sequence = record.sprite.sequence;
        item.frame = record.sprite.frame_index;
        item.motion = motion;
        visit(context, item);
    }
}

void debris_drawn(
    EffectWorld& world, const oa::Game& game, const EffectHost& host, const DebrisPiece& piece
) {
    if ((piece.flags & debris_smoke) != 0)
        spawn_white_smoke(world, game, host, piece.position, layer_smoke);
    if ((piece.flags & debris_fire) != 0)
        spawn_spark(world, game, host, piece.position, layer_smoke);
}

void draw_debris(
    EffectWorld& world,
    const oa::Game& game,
    const EffectHost& host,
    bool start_particles,
    void* context,
    DebrisVisitor visit
) {
    for (const auto& piece : world.debris) {
        if (!piece.live)
            continue;
        if (start_particles)
            debris_drawn(world, game, host, piece);
        if (visit != nullptr)
            visit(context, piece);
    }
}
} // namespace oa::sim::effect_particles
