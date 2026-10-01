// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/effect_particles.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>

using namespace oa::sim::effect_particles;
using oa::FixedVec3;

namespace {
int failures = 0;

void check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

struct Stream {
    int32_t value{0x4000};
    int64_t calls{};
    int32_t ground{};
    uint32_t seed{1};
    int64_t synced_calls{};
};

int32_t fixed_rand(void* context) {
    auto& s = *static_cast<Stream*>(context);
    ++s.calls;
    return s.value;
}

int32_t ground_of(void* context, const FixedVec3&) {
    return static_cast<Stream*>(context)->ground;
}

// The minimal standard generator of the synced stream.
uint32_t synced_rand(void* context, uint32_t bound) {
    auto& s = *static_cast<Stream*>(context);
    ++s.synced_calls;
    if (static_cast<int32_t>(bound) < 2)
        return 0;
    s.seed = s.seed * 0x41a7U - (s.seed / 0x1f31dU) * 0x7fffffffU;
    if (static_cast<int32_t>(s.seed) < 1)
        s.seed += 0x7fffffffU;
    return s.seed % bound;
}

EffectHost host_for(Stream& s) {
    return {&s, fixed_rand, ground_of, ground_of, synced_rand};
}

oa::formats::gaf::Sequence sequence_of(size_t frames, uint16_t hold) {
    oa::formats::gaf::Sequence sequence{};
    sequence.repeat_flags = 1;
    sequence.frames.resize(frames);
    for (auto& frame : sequence.frames)
        frame.duration = hold;
    return sequence;
}

FixedVec3 units(int32_t x, int32_t y, int32_t z) {
    return {x << 16, y << 16, z << 16};
}

struct Fixture {
    std::unique_ptr<EffectWorld> world = std::make_unique<EffectWorld>();
    oa::formats::gaf::Sequence smoke_1 = sequence_of(12, 5);
    oa::formats::gaf::Sequence flamestream = sequence_of(20, 5);
    oa::formats::gaf::Sequence explosion = sequence_of(3, 2);
    oa::formats::gaf::Sequence explode3 = sequence_of(4, 3);
    oa::formats::gaf::Sequence nuke1 = sequence_of(5, 2);
    oa::formats::gaf::Sequence h2oboom2 = sequence_of(6, 3);
    oa::formats::gaf::Sequence lavasplash = sequence_of(7, 3);
    oa::formats::gaf::Sequence tiers[flash_tier_count]{};
    oa::Game game{};
    Stream stream{};

    Fixture() {
        world->fx[static_cast<uint32_t>(Fx::smoke_1)] = &smoke_1;
        world->fx[static_cast<uint32_t>(Fx::flamestream)] = &flamestream;
        world->fx[static_cast<uint32_t>(Fx::explosion)] = &explosion;
        world->fx[static_cast<uint32_t>(Fx::explode3)] = &explode3;
        world->fx[static_cast<uint32_t>(Fx::nuke1)] = &nuke1;
        world->fx[static_cast<uint32_t>(Fx::h2oboom2)] = &h2oboom2;
        world->fx[static_cast<uint32_t>(Fx::lavasplash)] = &lavasplash;
        for (int32_t i = 0; i < flash_tier_count; ++i) {
            tiers[i] = sequence_of(static_cast<size_t>(12 + i), 2);
            tiers[i].repeat_flags = 0;
            world->flash_tiers[i] = &tiers[i];
        }
        game.tick = 50;
        game.sea_level = 10;
    }

    EffectHost host() { return host_for(stream); }
};

void flash_frame_shades_by_distance() {
    Stream stream{};
    stream.value = 0;
    uint8_t shade[64]{};
    // Size 8: centre 4.0. With no noise the level is trunc(distance / 4 * 32).
    const auto origin = build_flash_frame(8, shade, host_for(stream));
    check(origin == 4, "the frame origin is the centre");
    check(stream.calls == 64, "one rand() draw per pixel");
    check(shade[4 * 8 + 4] == 0x6e, "the centre sits at level 0: value 0x20 is the rim shade");
    check(shade[4 * 8 + 5] == 0x67, "one unit out: level 8, 0x20 - 8 + 0x4f");
    check(shade[4 * 8 + 7] == 0x57, "three units out: level 24");
    check(shade[4 * 8 + 0] == 0x4f, "four units out: level 32 gives the base shade");
    // Corner: sqrt(16 + 16 * 1.33) = 6.1057, level 48, 0x20 - 48 wraps above 0x22.
    check(shade[0] == 0xff, "the corner is transparent");
    stream.value = 0x7fff;
    (void)build_flash_frame(8, shade, host_for(stream));
    // Noise 0x7fff * 10 / 0x8000 = 9: the centre level is 9 / 4 * 32 = 72.
    check(shade[4 * 8 + 4] == 0xff, "full noise pushes even the centre past the rim");
}

// The storage the match allocates before init_explosion_tables fills it.
void size_tiers(oa::formats::gaf::Sequence (&tiers)[flash_tier_count]) {
    for (int32_t tier = 0; tier < flash_tier_count; ++tier) {
        const auto& shape = flash_tier_shapes[tier];
        tiers[tier].frames.resize(static_cast<size_t>(shape.count));
        for (int32_t index = 0; index < shape.count; ++index) {
            const auto side = static_cast<size_t>(flash_frame_side(shape, index));
            tiers[tier].frames[static_cast<size_t>(index)].pixels.resize(side * side);
        }
    }
}

void tables_build_three_tiers() {
    Fixture f;
    check(
        flash_frame_side(flash_tier_shapes[0], 0) == 64 &&
            flash_frame_side(flash_tier_shapes[0], 11) == 20 &&
            flash_frame_side(flash_tier_shapes[2], 14) == 46,
        "frame sides step by the truncated mean shrink"
    );
    oa::formats::gaf::Sequence unsized[flash_tier_count]{};
    unsized[0].frames.resize(12);
    build_flash_tier(unsized[0], flash_tier_shapes[0], f.host());
    check(
        f.stream.calls == 0 && unsized[0].frames[5].width == 44,
        "a frame without matching storage is left blank and draws nothing"
    );
    oa::formats::gaf::Sequence tiers[flash_tier_count]{};
    size_tiers(tiers);
    init_explosion_tables(*f.world, tiers, f.host());
    check(
        tiers[0].frames.size() == 12 && tiers[1].frames.size() == 15 &&
            tiers[2].frames.size() == 15,
        "the three flash tiers have 12, 15 and 15 frames"
    );
    check(
        tiers[0].frames.front().width == 64 && tiers[0].frames.back().width == 20,
        "tier 0 shrinks 64 by (8 - 64) / 12 = -4 to 20"
    );
    check(
        tiers[1].frames.front().width == 128 && tiers[1].frames.back().width == 30,
        "tier 1 shrinks 128 by -7 to 30"
    );
    check(
        tiers[2].frames.front().width == 200 && tiers[2].frames.back().width == 46,
        "tier 2 shrinks 200 by -11 to 46"
    );
    check(
        tiers[2].frames[3].duration == 2 && tiers[2].repeat_flags == 0,
        "each flash frame holds two ticks, once"
    );
    // Sum of squared sizes: 23456 + 107335 + 260815.
    check(f.stream.calls == 391606, "building the tiers draws one rand() number per flash pixel");
    check(
        f.world->flash_tiers[1] == &tiers[1] && f.world->explosion_count == 0,
        "the tables point at the tiers"
    );
}

void log_explosion_adds_smoke_over_land() {
    Fixture f;
    log_explosion(*f.world, f.game, f.host(), units(5, 30, 5), &f.explosion, 0, false);
    check(f.world->explosion_count == 1, "one record logged");
    const auto& record = f.world->explosions[0];
    check(
        record.sprite.sequence == &f.explosion && record.sprite.frame_index == 0 &&
            record.sprite.remaining_ticks == 2 && record.sprite.repeat_flag == 0,
        "the sprite starts at frame 0 without repeating"
    );
    check(record.flash.sequence == &f.tiers[0], "flash tier 0");
    const auto& smoke = f.world->layers[layer_smoke];
    check(
        smoke.count == 1 && smoke.emitters[smoke.head].interval == 7 &&
            smoke.emitters[smoke.head].deadline == 65,
        "a land explosion raises a fifteen-tick column at interval seven"
    );
    log_explosion(*f.world, f.game, f.host(), units(5, 30, 5), &f.explosion, -1, true);
    log_explosion(*f.world, f.game, f.host(), units(5, 10, 5), &f.explosion, -1, false);
    check(f.world->layers[layer_smoke].count == 1, "no smoke under water or at sea level");
    check(!f.world->explosions[1].flash.active(), "a -1 tier has no flash");
    for (int32_t i = 0; i < 400; ++i)
        log_explosion(*f.world, f.game, f.host(), units(0, 0, 0), &f.explosion, -1, true);
    check(f.world->explosion_count == explosion_capacity, "the table holds 300 records");
}

void records_expire_after_their_frames() {
    Fixture f;
    log_explosion(*f.world, f.game, f.host(), units(0, 0, 0), &f.explosion, -1, true);
    // Three frames of two ticks: a span advances when two or fewer ticks
    // remain, so the span ends on the sixth step.
    for (int32_t i = 0; i < 5; ++i)
        tick_explosions(*f.world, f.game, f.host());
    check(
        f.world->explosion_count == 1 && f.world->explosions[0].sprite.frame_index == 2,
        "after five steps the last frame shows"
    );
    tick_explosions(*f.world, f.game, f.host());
    check(f.world->explosion_count == 0, "the record goes when its sprite ends");
    log_explosion(*f.world, f.game, f.host(), units(0, 0, 0), &f.explosion, 0, true);
    log_explosion(*f.world, f.game, f.host(), units(1, 0, 0), &f.nuke1, -1, true);
    for (int32_t i = 0; i < 10; ++i)
        tick_explosions(*f.world, f.game, f.host());
    check(
        f.world->explosion_count == 1 && f.world->explosions[0].position.x == 0,
        "the flash keeps its record alive and survivors keep their order"
    );
}

void cob_bitmaps_log_large_flashes() {
    Fixture f;
    log_cob_explosions(*f.world, f.game, f.host(), units(0, 5, 0), 0x100U | 0x400U | 0x2000U);
    check(f.world->explosion_count == 3, "one record per bitmap bit");
    check(
        f.world->explosions[0].sprite.sequence == &f.explosion &&
            f.world->explosions[1].sprite.sequence == &f.explode3 &&
            f.world->explosions[2].sprite.sequence == &f.nuke1,
        "bits 0x100, 0x400 and 0x2000 select explosion, explode3 and nuke1"
    );
    check(f.world->explosions[2].flash.sequence == &f.tiers[2], "COB bitmaps use the large flash");
}

DebrisPiece falling_piece(int32_t y, int32_t vy, uint32_t flags) {
    DebrisPiece piece{};
    piece.position = {0, y, 0};
    piece.velocity = {0x40000, vy, -0x40000};
    piece.spin_rate[0] = 100;
    piece.spin_rate[1] = 200;
    piece.spin_rate[2] = 300;
    piece.lifetime = 900;
    piece.flags = flags;
    return piece;
}

void debris_flies_bounces_and_explodes() {
    Fixture f;
    f.game.sea_level = 0;
    f.game.gravity = 0x4000;
    f.stream.ground = 0;
    auto piece = falling_piece(0x10000, 0x10000, debris_fall | debris_explode_on_landing);
    // Above ground: 0 << 16 < vy + y, so the piece moves and falls.
    check(settle_debris(piece, *f.world, f.game, f.host()), "an airborne piece keeps flying");
    check(
        piece.position.y == 0x20000 && piece.position.x == 0x40000 && piece.velocity.y == 0xc000,
        "it moves by its velocity and gravity slows the climb"
    );
    check(
        piece.spin[0] == 200 && piece.spin[1] == 300 && piece.spin[2] == 100 &&
            piece.lifetime == 899,
        "the angles take the rates rotated by one"
    );
    // A hard landing: vy -0x60000 bounces to 0x30000 and halves x/z.
    auto bouncer = falling_piece(0x10000, -0x60000, debris_fall | debris_explode_on_landing);
    check(settle_debris(bouncer, *f.world, f.game, f.host()), "a hard landing bounces");
    check(
        bouncer.velocity.x == 0x20000 && bouncer.velocity.z == -0x20000 &&
            bouncer.velocity.y == 0x30000 - 0x4000,
        "the bounce halves x/z, reflects half of y, then gravity applies"
    );
    // A soft landing: vy -0x20000 reflects to 0x10000, under 0x20000, so it explodes.
    auto lander = falling_piece(0x10000, -0x20000, debris_explode_on_landing);
    check(!settle_debris(lander, *f.world, f.game, f.host()), "a soft landing ends the piece");
    check(
        f.world->explosion_count == 1 && f.world->explosions[0].sprite.sequence == &f.explosion &&
            f.world->explosions[0].flash.sequence == &f.tiers[0],
        "an exploding piece logs the explosion entry with the small flash"
    );
    check(f.world->layers[layer_smoke].count == 1, "a landing above sea level smokes");
    auto quiet = falling_piece(0x10000, -0x20000, 0);
    check(
        !settle_debris(quiet, *f.world, f.game, f.host()) && f.world->explosion_count == 1,
        "a piece without the explode bit just stops"
    );
    auto spent = falling_piece(0x10000, 0, 0);
    spent.lifetime = 0;
    check(
        !settle_debris(spent, *f.world, f.game, f.host()),
        "a piece at the end of its lifetime is dropped"
    );
}

void debris_splashes_in_water() {
    Fixture f;
    auto piece = falling_piece(5 << 16, 0, debris_explode_on_landing);
    check(!settle_debris(piece, *f.world, f.game, f.host()), "a piece at or under sea level ends");
    check(
        f.world->explosion_count == 1 && f.world->explosions[0].sprite.sequence == &f.h2oboom2 &&
            !f.world->explosions[0].flash.active(),
        "it splashes with h2oboom2 and no flash"
    );
    check(f.world->layers[layer_smoke].count == 0, "a splash does not smoke");
    f.world->lava_world = true;
    auto lava = falling_piece(5 << 16, 0, debris_explode_on_landing);
    (void)settle_debris(lava, *f.world, f.game, f.host());
    check(f.world->explosions[1].sprite.sequence == &f.lavasplash, "lava worlds splash lava");
    f.world->no_sea_level_trigger = true;
    auto dry = falling_piece(5 << 16, 0, debris_explode_on_landing);
    (void)settle_debris(dry, *f.world, f.game, f.host());
    check(f.world->explosion_count == 2, "nosealeveltrigger maps skip the splash");
}

// The match state a shattering unit belongs to: `unit` is owned by player 0,
// whose player record carries `color`.
std::unique_ptr<oa::World> owning_world(oa::Unit& unit, uint8_t color, int32_t gravity) {
    auto state = std::make_unique<oa::World>();
    state->game.gravity = gravity;
    state->game.players[0].info = 1;
    state->player_info[0].color = color;
    unit.owner = 1;
    return state;
}

// The fragment record and arena slot built for the pieces below.
struct Shard {
    int16_t slot;
    FixedVec3 velocity;
    int32_t rates[3];
    FixedVec3 carried;
    FixedVec3 points[fragment_point_count];
};

void check_shard(
    const EffectWorld& world, int32_t record_index, const Shard& expected, const char* what
) {
    const auto& record = world.explosions[record_index];
    bool same =
        record.fragment == expected.slot && record.velocity.x == expected.velocity.x &&
        record.velocity.y == expected.velocity.y && record.velocity.z == expected.velocity.z &&
        record.carried.x == expected.carried.x && record.carried.y == expected.carried.y &&
        record.carried.z == expected.carried.z && record.spin[0] == 0 && record.spin[1] == 0 &&
        record.spin[2] == 0 && !record.sprite.active() && !record.flash.active();
    for (int32_t axis = 0; axis < 3; ++axis)
        same = same && record.spin_rate[axis] == expected.rates[axis];
    if (same) {
        const auto& fragment = world.fragments[record.fragment];
        same = fragment.live;
        for (uint32_t i = 0; i < fragment_point_count; ++i)
            same = same && fragment.points[i].x == expected.points[i].x &&
                   fragment.points[i].y == expected.points[i].y &&
                   fragment.points[i].z == expected.points[i].z;
    }
    check(same, what);
}

// With 298 records logged, two fragments fit. A unit without a movement
// object leaves the carried words its records held before; slots already in
// use are skipped.
void shatter_fills_the_table() {
    Fixture f;
    f.stream.seed = 987654321;
    f.world->explosion_count = 298;
    f.world->explosions[298].carried = {4096, 8192, 12288};
    f.world->explosions[298].explodes = true;
    f.world->explosions[299].carried = {-5, 6, -7};
    f.world->fragments[0].live = true;
    f.world->fragments[1].live = true;
    static const FixedVec3 points[]{
        {0, 0, 0},
        {655360, 0, 0},
        {655360, 0, 655360},
        {0, 0, 655360},
        {0, 327680, 0},
        {655360, 327680, 0}
    };
    static const uint16_t floor[]{0, 1, 2, 3};
    static const uint16_t wall[]{0, 1, 5, 4};
    static const uint16_t under[]{3, 2, 1, 0};
    const PiecePrimitive primitives[]{{1, 4, floor, 0}, {2, 4, wall, 0}, {3, 4, under, 0}};
    ShatterPiece piece{};
    piece.primitives = primitives;
    piece.primitive_count = 3;
    piece.points = points;
    piece.point_count = 6;
    oa::Unit unit{};
    unit.position = {-65536, 0, 32768};
    const auto state = owning_world(unit, 0, 7040);
    DebrisPiece request{};
    request.flags = debris_shatter;
    shatter_piece(*f.world, *state, f.host(), request, unit, nullptr, piece);
    check(f.world->explosion_count == explosion_capacity, "the table fills");
    check(
        f.stream.synced_calls == 16 && f.stream.seed == 1679350915U, "the third quad draws nothing"
    );
    static const Shard expected[]{
        {2,
         {-12288, 249088, 19968},
         {-370, 278, 230},
         {4096, 8192, 12288},
         {{-327680, -32767, -327680},
          {327680, -32767, -327680},
          {327680, -32767, 327680},
          {-327680, -32767, 327680},
          {-327680, 32768, 327680},
          {327680, 32768, 327680},
          {327680, 32768, -327680},
          {-327680, 32768, -327680}}},
        {3,
         {24064, 242432, -84480},
         {363, -574, 336},
         {-5, 6, -7},
         {{-327680, -163840, 32767},
          {327680, -163840, 32767},
          {327680, 163840, 32767},
          {-327680, 163840, 32767},
          {-327680, 163840, -32768},
          {327680, 163840, -32768},
          {327680, -163840, -32768},
          {-327680, -163840, -32768}}}
    };
    check_shard(*f.world, 298, expected[0], "a flat quad is a slab one unit thick");
    check_shard(*f.world, 299, expected[1], "a wall quad keeps the stale carried words");
    check(!f.world->explosions[298].explodes, "a request without the explode bit clears it");
}

// One fragment: it moves by its fling plus the carried words,
// gravity slows the climb, and a landing reflects half the fall until the
// bounce is under a unit a tick.
void fragments_fly_and_bounce() {
    Fixture f;
    f.game.gravity = 0x4000;
    f.game.sea_level = 0;
    f.stream.ground = 5;
    auto& record = f.world->explosions[0];
    f.world->explosion_count = 1;
    record.fragment = claim_fragment(*f.world);
    record.position = units(0, 7, 0);
    record.carried = {0x100, 0x200, 0x300};
    record.velocity = {0x1000, -0x8000, 0x2000};
    record.spin_rate[0] = 0x10005;
    record.spin_rate[1] = -3;
    record.spin_rate[2] = 7;
    record.explodes = true;
    tick_explosions(*f.world, f.game, f.host());
    check(
        record.position.x == 0x1100 && record.position.y == (7 << 16) - 0x8000 + 0x200 &&
            record.position.z == 0x2300 && record.velocity.y == -0xc000,
        "a fragment moves by its fling and the carried words, then falls"
    );
    check(
        record.spin[0] == 5 && record.spin[1] == -3 && record.spin[2] == 7,
        "the spin takes each rate's low word"
    );
    record.velocity.y = -0x40000;
    tick_explosions(*f.world, f.game, f.host());
    check(
        record.position.y == (7 << 16) - 0x8000 + 0x200 && record.velocity.y == 0x22000 &&
            record.fragment == 0,
        "hitting the ground restores the position and reflects half the fall"
    );
    record.velocity.y = -0x10000;
    tick_explosions(*f.world, f.game, f.host());
    check(
        record.fragment == no_fragment && !f.world->fragments[0].live,
        "a weak bounce ends the fragment"
    );
    check(
        f.world->explosion_count == 1 && f.world->explosions[0].sprite.sequence == &f.explosion &&
            f.world->explosions[0].flash.sequence == &f.tiers[0],
        "an exploding fragment logs the explosion with the small flash in its place"
    );
}

void fragments_splash_in_water() {
    Fixture f;
    f.game.gravity = 0x4000;
    f.stream.ground = 2;
    f.world->explosion_count = 2;
    for (int32_t i = 0; i < 2; ++i) {
        auto& record = f.world->explosions[i];
        record.fragment = claim_fragment(*f.world);
        record.position = units(i, 12, 0);
        record.velocity = {0, -0x40000, 0};
        record.explodes = i == 0;
    }
    tick_explosions(*f.world, f.game, f.host());
    check(
        !f.world->fragments[0].live && !f.world->fragments[1].live,
        "fragments at sea level over a sea bed end"
    );
    check(
        f.world->explosion_count == 1 && f.world->explosions[0].sprite.sequence == &f.h2oboom2 &&
            !f.world->explosions[0].flash.active() && f.world->explosions[0].position.y == 8 << 16,
        "only the exploding one splashes, at its new position"
    );
}

// tick_explosions removes the first spent record by moving the rest down, until
// none is left; the vacated tail keeps what it held.
void spent_records_shift_down() {
    Fixture f;
    f.world->explosion_count = 5;
    const FixedVec3 marks[]{
        units(1, 0, 0), units(2, 0, 0), units(3, 0, 0), units(4, 0, 0), units(5, 0, 0)
    };
    for (int32_t i = 0; i < 5; ++i) {
        f.world->explosions[i].position = marks[i];
        f.world->explosions[i].carried = marks[i];
        if (i % 2 == 0)
            oa::sim::sprite_animation::initialize(f.world->explosions[i].sprite, &f.nuke1, 0);
    }
    tick_explosions(*f.world, f.game, f.host());
    check(f.world->explosion_count == 3, "the two spent records go");
    check(
        f.world->explosions[1].position.x == marks[2].x &&
            f.world->explosions[2].position.x == marks[4].x,
        "survivors keep their order"
    );
    check(
        f.world->explosions[3].carried.x == marks[4].x &&
            f.world->explosions[4].carried.x == marks[4].x,
        "the tail repeats the last record, as the shifting leaves it"
    );
}

void debris_slots_and_draw_particles() {
    Fixture f;
    auto shatter = falling_piece(0, 0, debris_shatter);
    check(!add_debris(*f.world, shatter), "shattering pieces are not kept whole");
    int32_t added = 0;
    for (int32_t i = 0; i < 120; ++i)
        added += add_debris(*f.world, falling_piece(i, 0, 0)) ? 1 : 0;
    check(added == debris_capacity, "the zero block holds a hundred pieces");
    auto burning = falling_piece(20 << 16, 0, debris_smoke | debris_fire);
    const auto before = f.stream.calls;
    debris_drawn(*f.world, f.game, f.host(), burning);
    check(
        f.world->layers[layer_smoke].count == 2, "a smoking, burning piece adds a puff and a spark"
    );
    check(f.stream.calls - before == 5, "one draw for the puff, four for the spark");
    tick_explosions(*f.world, f.game, f.host());
    int32_t live = 0;
    for (const auto& piece : f.world->debris)
        live += piece.live ? 1 : 0;
    check(live == 0, "pieces under sea level without the explode bit are dropped");
}

// draw_debris walks the debris table in slot order: each held piece gets its
// puff or spark before the piece itself is drawn, and drawing
// keeps every slot.
void debris_draw_walk() {
    Fixture f;
    (void)add_debris(*f.world, falling_piece(1 << 16, 0, debris_smoke));
    (void)add_debris(*f.world, falling_piece(2 << 16, 0, 0));
    (void)add_debris(*f.world, falling_piece(3 << 16, 0, debris_fire));
    f.world->debris[1].live = false;

    struct Seen {
        const EffectWorld* world{};
        int32_t count{};
        int32_t height[4]{};
        int32_t smoke[4]{};
    } seen{f.world.get()};

    draw_debris(
        *f.world, f.game, f.host(), true, &seen, [](void* context, const DebrisPiece& piece) {
            auto& s = *static_cast<Seen*>(context);
            s.height[s.count] = piece.position.y;
            s.smoke[s.count] = s.world->layers[layer_smoke].count;
            ++s.count;
        }
    );
    check(
        seen.count == 2 && seen.height[0] == 1 << 16 && seen.height[1] == 3 << 16,
        "the held pieces are drawn in slot order"
    );
    check(
        seen.smoke[0] == 1 && seen.smoke[1] == 2,
        "each piece's particle is started before it is drawn"
    );
    check(f.world->debris[0].live && f.world->debris[2].live, "drawing keeps the slots");
    draw_debris(*f.world, f.game, f.host(), true, nullptr, nullptr);
    check(f.world->layers[layer_smoke].count == 4, "every draw starts the particles again");
    seen.count = 0;
    draw_debris(*f.world, f.game, f.host(), false, &seen, [](void* context, const DebrisPiece&) {
        ++static_cast<Seen*>(context)->count;
    });
    check(
        seen.count == 2 && f.world->layers[layer_smoke].count == 4,
        "without particles the pieces are drawn alone"
    );
}

// The zero-block scan hands out the lowest free slot; the per-tick walk
// frees the slots of spent pieces and keeps the others.
void debris_slot_search_and_settle() {
    Fixture f;
    check(find_free_debris_slot(*f.world) == 0, "an empty table hands out slot 0");
    for (int32_t i = 0; i < debris_capacity; ++i)
        (void)add_debris(*f.world, falling_piece(i, 0, 0));
    check(find_free_debris_slot(*f.world) == -1, "a full table has no free slot");
    f.world->debris[37].live = false;
    f.world->debris[64].live = false;
    check(find_free_debris_slot(*f.world) == 37, "the lowest freed slot comes first");
    check(
        add_debris(*f.world, falling_piece(1, 0, 0)) && f.world->debris[37].live,
        "a new piece takes that slot"
    );

    Fixture g;
    g.game.sea_level = 0;
    g.stream.ground = 0;
    auto spent = falling_piece(0x10000, 0, 0);
    spent.lifetime = 0;
    (void)add_debris(*g.world, spent);
    (void)add_debris(*g.world, falling_piece(0x10000, 0x10000, debris_fall));
    settle_all_debris(*g.world, g.game, g.host());
    check(
        !g.world->debris[0].live && g.world->debris[1].live,
        "a spent piece frees its slot, a flying one keeps it"
    );
    check(
        g.world->debris[1].lifetime == 899 && g.world->debris[1].position.y == 0x20000,
        "the flying piece moved one tick"
    );
}
} // namespace

struct DrawLog {
    DrawKind kinds[8]{};
    const oa::formats::gaf::Sequence* sequences[8]{};
    int32_t count{};
};

// A 640x480 screen's battlefield with the camera at the origin.
constexpr ExplosionView full_view{0, 0, {0x80, 0x20, 639, 480 - 0x21}};

void draw_explosions_visits_flashes_first() {
    Fixture f;
    log_explosion(*f.world, f.game, f.host(), units(5, 0, 5), &f.explosion, 1, true);
    log_explosion(*f.world, f.game, f.host(), units(6, 0, 6), &f.nuke1, -1, true);
    log_explosion(*f.world, f.game, f.host(), units(7, 0, 7), nullptr, 2, true);
    DrawLog log;
    f.world->explosions[1].fragment = claim_fragment(*f.world);
    f.world->explosions[1].spin[2] = 77;
    f.world->explosions[1].spin_rate[2] = 300;
    f.world->explosions[1].velocity = units(2, 3, 0);
    f.world->explosions[1].carried = units(1, 0, -1);
    draw_explosions(*f.world, full_view, &log, [](void* context, const ParticleDraw& item) {
        auto& out = *static_cast<DrawLog*>(context);
        out.kinds[out.count] = item.kind;
        out.sequences[out.count] = item.sequence;
        // A flying fragment's record moves by its velocity and what it
        // carried each tick, and turns by its spin rates; a flash holds still.
        if (item.kind == DrawKind::fragment)
            check(
                item.fragment != nullptr && item.spin[2] == 77 && item.spin_motion[2] == 300 &&
                    item.motion.x == units(3, 0, 0).x && item.motion.y == units(0, 3, 0).y &&
                    item.motion.z == units(0, 0, -1).z,
                "a fragment carries its slot, spin and motion"
            );
        else if (item.kind == DrawKind::flash)
            check(
                item.motion.x == 0 && item.motion.y == 0 && item.motion.z == 0,
                "a flash holds still"
            );
        ++out.count;
        check(!item.sight_gated, "explosion records ignore the sight grid");
    });
    check(log.count == 5, "two flashes, a fragment and two sprites are visited");
    check(
        log.kinds[0] == DrawKind::flash && log.sequences[0] == f.world->flash_tiers[1] &&
            log.kinds[1] == DrawKind::flash && log.sequences[1] == f.world->flash_tiers[2],
        "every flash comes first, in record order"
    );
    check(
        log.kinds[2] == DrawKind::sprite && log.sequences[2] == &f.explosion &&
            log.kinds[3] == DrawKind::fragment && log.kinds[4] == DrawKind::sprite &&
            log.sequences[4] == &f.nuke1,
        "then each record's fragment and sprite, in record order"
    );
}

// A record draws only when its centre, x less the camera plus 0x80 and z
// less the camera less half the height plus 0x20, is inside the rectangle
// (inclusive); its flash, fragment and sprite go together.
void draw_explosions_culls_by_the_centre() {
    Fixture f;
    const ExplosionView view{2, 3, {0x80, 0x20, 0x80 + 8, 0x20 + 7}};
    const FixedVec3 centres[]{
        units(10, 0, 10), // the bottom-right corner
        units(10, 8, 14), // lifted back in by half its height
        units(11, 0, 10), // one pixel right
        units(10, 0, 11), // one pixel down
        units(10, -2, 10) // below ground sinks a pixel
    };
    for (const auto& centre : centres) {
        log_explosion(*f.world, f.game, f.host(), centre, &f.explosion, 1, true);
        f.world->explosions[f.world->explosion_count - 1].fragment = claim_fragment(*f.world);
    }
    DrawLog log;
    draw_explosions(*f.world, view, &log, [](void* context, const ParticleDraw& item) {
        auto& out = *static_cast<DrawLog*>(context);
        if (out.count < 8)
            out.kinds[out.count] = item.kind;
        ++out.count;
    });
    check(log.count == 6, "two records draw a flash, a fragment and a sprite each");
    check(
        log.kinds[0] == DrawKind::flash && log.kinds[1] == DrawKind::flash &&
            log.kinds[2] == DrawKind::fragment && log.kinds[3] == DrawKind::sprite &&
            log.kinds[4] == DrawKind::fragment && log.kinds[5] == DrawKind::sprite,
        "the culled records draw nothing"
    );
}

int main() {
    flash_frame_shades_by_distance();
    draw_explosions_visits_flashes_first();
    draw_explosions_culls_by_the_centre();
    tables_build_three_tiers();
    log_explosion_adds_smoke_over_land();
    records_expire_after_their_frames();
    cob_bitmaps_log_large_flashes();
    debris_flies_bounces_and_explodes();
    debris_splashes_in_water();
    debris_slots_and_draw_particles();
    debris_draw_walk();
    debris_slot_search_and_settle();
    shatter_fills_the_table();
    fragments_fly_and_bounce();
    fragments_splash_in_water();
    spent_records_shift_down();
    if (failures != 0) {
        std::fprintf(stderr, "%d explosion checks failed\n", failures);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
