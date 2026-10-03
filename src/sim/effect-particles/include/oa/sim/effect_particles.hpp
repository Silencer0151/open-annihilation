// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "oa/core/game_state.h"
#include "oa/core/unit.h"
#include "oa/core/world.h"
#include "oa/data/limits.hpp"
#include "oa/sim/unit_movement/movement.hpp"
#include "oa/formats/gaf.hpp"
#include "oa/sim/sprite_animation.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>

namespace oa::formats::objects3d {
struct Model;
}

// Sprite and pixel effects the simulation spawns and advances: the ten
// ordered emitter lists, the 300-record explosion table with its three
// procedural flash tiers, the hundred whole-piece debris slots, and the 300
// polygon fragments a shattering piece breaks into. They live here, not in
// the Game record, whose effect_layers and sprite_and_effect_tables stand in
// for them. The particles draw from the rand() random stream, so spawns stay
// in the game's order even when a record cannot be stored; shattering draws
// from the synced stream every player shares.
namespace oa::sim::effect_particles {

// Boundaries owned by the match: both generators and two terrain queries.
struct EffectHost {
    void* context{};
    int32_t (*lcg_rand)(void* context){};                      // rand() generator, 0..0x7fff
    int32_t (*grid_height)(void* context, const FixedVec3&){}; // terrain height at x/z, whole units
    int32_t (*ground_height)(
        void* context, const FixedVec3&
    ){}; // mean plot height at x/z, whole units
    uint32_t (*synced_rand)(void* context, uint32_t bound){}; // synced stream, 0..bound-1
};

// The FX.GAF entries the effects draw. explosion, explode2..5 and nuke1 follow
// one another in the order the COB explode bitmaps select them. Loading FX.GAF
// also reads the shell sprites, which are not listed here.
enum class Fx : uint8_t {
    smoke_1,
    smoke_2,
    fire1,
    alfboom1,
    radlogo,
    radlogohigh,
    nuclogo,
    h2oboom2,
    lavasplash,
    flamestream,
    explosion,
    explode2,
    explode3,
    explode4,
    explode5,
    nuke1,
    shadow,
    count
};

inline constexpr uint32_t fx_count = static_cast<uint32_t>(Fx::count);

/// Returns the FX.GAF entry name of an effect sprite.
///
/// @param entry effect sprite
/// @return the entry name, or an empty string for an out-of-range value
[[nodiscard]] const char* fx_name(Fx entry) noexcept;

inline constexpr uint16_t layer_count = 10;
// 3.1c's layer limit: queue_emitter drops a layer's oldest emitter past this
// many (data::limits::Effects::queue).
inline constexpr uint32_t layer_evict_above = 400;
inline constexpr uint16_t layer_wake = 2;
inline constexpr uint16_t layer_feature_smoke = 4; // geothermal vents
inline constexpr uint16_t layer_teleport = 5;
inline constexpr uint16_t layer_burning_feature = 5; // burning-feature smoke
inline constexpr uint16_t layer_nano = 6;
inline constexpr uint16_t layer_thrust = 7;
inline constexpr uint16_t layer_smoke = 9;

inline constexpr uint16_t no_record = 0xffffU;

// Every emitter takes a slot of one shared pool, 1000 in 3.1c
// (data::limits::Effects::reserve); the "SFX" console command makes the pool
// refuse every request.
inline constexpr uint32_t emitter_pool_capacity = 1000;

// Particle of the teleport trail. step_trail_particle steps it.
struct TrailParticle {
    const formats::gaf::Sequence* sequence{}; // flamestream
    FixedVec3 position{};
    FixedVec3 target{};
    FixedVec3 delta{};
    int32_t period{}; // frame count - 1
    int32_t frame{};
    int32_t expire_tick{};
};

// Nano-lathe particle.
struct NanoParticle {
    FixedVec3 position{};
    FixedVec3 target{};
    FixedVec3 delta{};
    int32_t unread_word{}; // set to 0x100 at spawn; nothing reads it
    int32_t color{};       // palette index; each step cycles the low nibble 1..7
    int32_t expire_tick{};
};

// Flame particle.
struct FlameParticle {
    const formats::gaf::Sequence* sequence{}; // flamestream
    FixedVec3 position{};
    FixedVec3 target{};
    FixedVec3 delta{};
    int32_t frame_period{}; // frame count - 1
    int32_t frame{};
    int32_t step{};
    int32_t step_period{};
    int32_t expire_tick{};
};

// Wake particle. step_wake_particle steps it; its sequence is never drawn.
struct WakeParticle {
    const formats::gaf::Sequence* sequence{}; // smoke 1
    FixedVec3 position{};
    FixedVec3 target{};
    FixedVec3 delta{};
    int32_t low{};
    int32_t high{};
    int32_t value{}; // palette index drawn
    int32_t value_delta{};
    int32_t phase{};
    int32_t period{};
    int32_t expire_tick{};
};

// Puff of both smoke emitters.
struct SmokeParticle {
    const formats::gaf::Sequence* sequence{};
    FixedVec3 position{};
    int32_t frame_limit{};
    int32_t frame{};
    int32_t hold{};
    int32_t remaining{};
};

// The six emitter classes.
enum class EmitterKind : uint8_t { teleport_trail, nano, flame, wake, smoke, feature_smoke };

// An emitter's particles, oldest first, as a chain through its class's pool.
struct ParticleList {
    uint16_t first{no_record};
    uint16_t last{no_record};
    uint16_t count{};
};

struct Emitter {
    EmitterKind kind{};
    uint32_t deadline{};
    uint32_t next_spawn{};
    ParticleList particles{};
    int32_t interval{}; // trail step count, flame/wake period, smoke spawn interval
    FixedVec3 origin{}; // start point; nano: low corner of the source area; smoke: puff start
    FixedVec3 target{}; // end point; nano: low corner of the target area
    FixedVec3 delta{};
    FixedVec3 origin_extent{}; // nano: size of the source area
    FixedVec3 target_extent{}; // nano: size of the target area
    int32_t hold{};            // smoke ticks per frame
    int32_t frame_cap{};       // smoke frames a puff may show
    int32_t variant{};         // smoke: 1 selects smoke 2; wake: 1 lit
};

// One of the ten lists, oldest first, as a ring of EffectWorld::layer_slots
// emitters in the world's emitter block.
struct Layer {
    uint32_t head{};  // ring slot of the oldest emitter
    uint32_t count{}; // emitters held
};

// The emitter slots of the ten layers' rings, one block an effect world owns
// and copies with itself.
struct EmitterBlock {
    std::unique_ptr<Emitter[]> slots;
    std::size_t count{}; // slots held; 0 when the block could not be allocated

    EmitterBlock() = default;
    /// Allocates a block of emitter slots.
    ///
    /// @param slot_count slots to hold; a block that cannot be allocated holds none
    explicit EmitterBlock(std::size_t slot_count) noexcept;
    /// Copies another block's slots.
    ///
    /// @param other the block to copy; a copy that cannot be allocated holds none
    EmitterBlock(const EmitterBlock& other) noexcept;
    /// Replaces this block's slots with a copy of another's.
    ///
    /// @param other the block to copy; a copy that cannot be allocated holds none
    /// @return this block
    EmitterBlock& operator=(const EmitterBlock& other) noexcept;
    EmitterBlock(EmitterBlock&&) noexcept = default;
    EmitterBlock& operator=(EmitterBlock&&) noexcept = default;
    ~EmitterBlock() = default;
};

/// Returns the ring slots each layer needs for a set of effect limits.
///
/// A layer holds one emitter more than `queue` at most, and never more than
/// the pool hands out, so a ring holds the lower of `queue` and `reserve`,
/// plus one: 401 for 3.1c's 400 and 1000.
///
/// @param limits emitters a layer holds before evicting, and the pool's size
/// @return slots of one layer's ring
[[nodiscard]] constexpr uint32_t layer_ring_slots(const data::limits::Effects& limits) noexcept {
    return (limits.queue < limits.reserve ? limits.queue : limits.reserve) + 1U;
}

// Fixed pool of one particle class, shared by every emitter of that class;
// 3.1c keeps each emitter's particles without a limit, so the capacity is the
// engine's own. A spawn that finds its pool full still draws its random
// numbers, so the stream stays in step, and is then dropped.
template <class Record, uint16_t Capacity>
struct ParticlePool {
    Record records[Capacity]{};
    uint16_t next[Capacity]{};
    uint16_t free_head{no_record};
    uint16_t high_water{};
    uint16_t live{};
};

inline constexpr uint16_t trail_pool_capacity = 2048;
inline constexpr uint16_t nano_pool_capacity = 8192;
inline constexpr uint16_t flame_pool_capacity = 8192;
inline constexpr uint16_t wake_pool_capacity = 4096;
inline constexpr uint16_t smoke_pool_capacity = 8192;

inline constexpr int32_t explosion_capacity = 300;
inline constexpr int32_t debris_capacity = 100;
inline constexpr int32_t flash_tier_count = 3;
inline constexpr int32_t fragment_capacity = 300;

inline constexpr int16_t no_fragment = -1;

// Explosion record. A record logged by log_explosion leaves the fragment
// motion words as the table's previous occupant left them.
struct ExplosionRecord {
    int16_t fragment{no_fragment}; // fragment arena slot
    sim::sprite_animation::Cursor sprite{};
    sim::sprite_animation::Cursor flash{};
    FixedVec3 position{};
    FixedVec3 carried{}; // half the unit's velocity
    FixedVec3 velocity{};
    int32_t spin_rate[3]{};
    int16_t spin[3]{}; // rotation words the fragment draws with
    bool explodes{};   // the fragment logs an explosion when it comes down
};

// Every fragment is a slab: the quad it broke from, a reversed copy behind
// it, and the four sides between them.
inline constexpr uint32_t fragment_point_count = 8;
inline constexpr uint32_t fragment_face_count = 6;
inline constexpr uint32_t fragment_face_corners = 4;

// Corners of the six faces, shared by every arena slot.
inline constexpr uint16_t fragment_faces[fragment_face_count][fragment_face_corners]{
    {0, 1, 2, 3}, {2, 1, 6, 5}, {0, 3, 4, 7}, {1, 0, 7, 6}, {3, 2, 5, 4}, {4, 5, 6, 7}
};

// Bits of a loaded primitive's flag word (PiecePrimitive.flags) that
// shattering reads.
inline constexpr uint32_t primitive_colored = 0x1U;
inline constexpr uint32_t primitive_animated = 0x2U;
inline constexpr uint32_t primitive_team = 0x4U;

// What shatter_piece copies into all six faces of a fragment: the colour and
// flag words of the quad it broke from, and where the renderer finds that
// quad's texture binding. A team texture is resolved with the owner's colour
// and loses primitive_animated.
struct FragmentLook {
    int32_t color{};                          // the source quad's colour word
    uint32_t flags{};                         // the source quad's flag word
    uint8_t team_color{};                     // owner colour; picks the team texture frame
    const formats::objects3d::Model* model{}; // the source quad: model, object and
    uint32_t object{};                        // position in the loaded order
    uint32_t primitive{};
};

// One slot of the fragment arena: a 3DO object of eight points and the six
// faces of fragment_faces.
struct ShatterFragment {
    bool live{};                              // the slot is in use
    FixedVec3 points[fragment_point_count]{}; // in the loaded axes (x and z negated)
    FragmentLook look{};
};

// COB explode flags as packed into DebrisPiece.flags.
inline constexpr uint32_t debris_fire = 0x01U;
inline constexpr uint32_t debris_smoke = 0x02U;
inline constexpr uint32_t debris_shatter = 0x04U;
inline constexpr uint32_t debris_fall = 0x08U;
inline constexpr uint32_t debris_explode_on_landing = 0x10U;
inline constexpr uint32_t debris_shatter_explodes = 0x20U;

// The debris request a COB explode builds, followed by what it copies of the
// piece: its rotation words, its origin and its loaded object.
struct DebrisPiece {
    bool live{};
    uint16_t unit{};  // owning unit
    uint32_t piece{}; // script piece index
    int32_t spin_rate[3]{};
    FixedVec3 velocity{};
    int32_t lifetime{};   // ticks left
    int32_t shatter{};    // fragment thickness in units; 0 counts as one
    uint32_t flags{};     // debris_* bits
    int16_t spin[3]{};    // the piece's yz, xz and xy rotation words
    FixedVec3 position{}; // the piece origin plus the unit position
    // The loaded object the copy keeps drawing after its unit has gone.
    const formats::objects3d::Model* model{};
    uint32_t object{};
};

// A primitive of the shattering piece's object, in the order and with the
// flag word the model loader leaves.
struct PiecePrimitive {
    int32_t color{};
    uint32_t vertex_count{};
    const uint16_t* vertex_indices{};
    uint32_t flags{}; // primitive_* bits
};

// The piece shatter_piece breaks up: its loaded object and its record in the
// unit's piece table.
struct ShatterPiece {
    const PiecePrimitive* primitives{};
    uint32_t primitive_count{};
    // The object's selection primitive as the model loader leaves it: 0 once
    // it has swapped the selection primitive to the front, -1 when the object
    // has none. It is compared with the load position, not the 3DO file's
    // index.
    int32_t selection_primitive{-1};
    const FixedVec3* points{}; // transformed vertices
    uint32_t point_count{};
    FixedVec3 origin{}; // relative to the unit
    const formats::objects3d::Model* model{};
    uint32_t object{};
};

struct EffectWorld {
    Layer layers[layer_count]{};
    // The rings of the ten layers, layer_slots each, in layer order.
    EmitterBlock emitter_block{
        std::size_t{layer_count} * layer_ring_slots(data::limits::Effects{})
    };
    uint32_t layer_slots{layer_ring_slots(data::limits::Effects{})};
    uint32_t evict_above{layer_evict_above};       // a layer drops its oldest past this many
    uint32_t pool_capacity{emitter_pool_capacity}; // emitter pool slots there are
    ParticlePool<TrailParticle, trail_pool_capacity> trail{};
    ParticlePool<NanoParticle, nano_pool_capacity> nano{};
    ParticlePool<FlameParticle, flame_pool_capacity> flame{};
    ParticlePool<WakeParticle, wake_pool_capacity> wake{};
    ParticlePool<SmokeParticle, smoke_pool_capacity> smoke{};
    ExplosionRecord explosions[explosion_capacity]{};
    int32_t explosion_count{};
    DebrisPiece debris[debris_capacity]{};
    ShatterFragment fragments[fragment_capacity]{};
    const formats::gaf::Sequence* fx[fx_count]{};
    const formats::gaf::Sequence* flash_tiers[flash_tier_count]{};
    bool lava_world{};           // OTA lavaworld
    bool no_sea_level_trigger{}; // OTA nosealeveltrigger
    // Each explosion log_explosion records above sea level raises a short
    // smoke column, as in 3.1c; a mod's display rules may turn it off.
    bool explosion_smoke_column{true};
    uint32_t pooled_emitters{}; // emitter pool slots handed out
    bool emitters_refused{};    // the "SFX" console toggle
};

/// Sizes an effect world's layers and emitter pool from a mod's limits.
///
/// Call it on a world with no emitters, before the match first ticks. Without
/// it a world keeps 3.1c's sizes: 400 emitters a layer before eviction and
/// 1000 in the pool.
///
/// @param[in,out] world the world to size; its layers are emptied
/// @param limits emitters a layer holds before evicting, and the pool's size
/// @return false when the layers' rings cannot be allocated; the world then
///     queues no emitter
bool size_effect_world(EffectWorld& world, const data::limits::Effects& limits) noexcept;

/// Returns one emitter of a layer, oldest first.
///
/// @param world layers
/// @param layer layer index 0..9
/// @param index position in the layer, below Layer::count
/// @return the emitter
[[nodiscard]] const Emitter&
layer_emitter(const EffectWorld& world, uint16_t layer, uint32_t index) noexcept;

/// Returns one emitter of a layer, oldest first.
///
/// @param[in,out] world layers
/// @param layer layer index 0..9
/// @param index position in the layer, below Layer::count
/// @return the emitter
[[nodiscard]] Emitter& layer_emitter(EffectWorld& world, uint16_t layer, uint32_t index) noexcept;

/// Advances every emitter layer by one tick.
///
/// Deletes every emitter whose particles are gone, and ticks the rest: advances and
/// expires their particles, then spawns when the emitter is due.
///
/// @param[in,out] world emitter layers and particle pools
/// @param game current tick, wind, gravity and sea level
/// @param host random stream and terrain queries
void tick_particles(EffectWorld& world, const oa::Game& game, const EffectHost& host);

/// Appends an emitter to a layer.
///
/// The emitter must already hold an emitter pool slot. When the layer holds more
/// than EffectWorld::evict_above emitters (400 in 3.1c) its oldest is deleted
/// first; an emitter for a layer out of range, or for a world whose rings could
/// not be allocated, is deleted instead.
///
/// @param[in,out] world layers and pools
/// @param layer layer index 0..9
/// @param emitter emitter to append, copied
void queue_emitter(EffectWorld& world, uint16_t layer, const Emitter& emitter);

/// Starts flame sprites carried from one point to another.
///
/// A sprite starts every tick until `travel` ticks pass; each advances its frame every
/// `period` ticks.
///
/// @param[in,out] world layers and pools the emitter joins
/// @param game current tick
/// @param host unused
/// @param from start point, 16.16 world coordinates
/// @param to end point, 16.16 world coordinates
/// @param period ticks per animation frame
/// @param travel ticks to cover the distance, and the emitter's lifetime
/// @param layer layer index 0..9
void spawn_flame(
    EffectWorld& world,
    const oa::Game& game,
    const EffectHost& host,
    const FixedVec3& from,
    const FixedVec3& to,
    int32_t period,
    int32_t travel,
    uint16_t layer
);

/// Starts colour-cycling wake pixels that brighten through the ramp.
///
/// The pixels drift half a unit a tick from `from` towards `to`, jittered by up to
/// three units, each living six periods; one starts every tick for one tick.
///
/// @param[in,out] world layers and pools the emitter joins
/// @param game current tick
/// @param host random stream and terrain queries
/// @param from start point, 16.16 world coordinates
/// @param to point the pixels drift towards
/// @param period ticks per colour step
/// @param layer layer index 0..9
void spawn_wake_lit(
    EffectWorld& world,
    const oa::Game& game,
    const EffectHost& host,
    const FixedVec3& from,
    const FixedVec3& to,
    int32_t period,
    uint16_t layer
);

/// Starts wake pixels that darken through the ramp (submarine bubbles).
///
/// Otherwise as spawn_wake_lit.
///
/// @param[in,out] world layers and pools the emitter joins
/// @param game current tick
/// @param host random stream and terrain queries
/// @param from start point, 16.16 world coordinates
/// @param to point the pixels drift towards
/// @param period ticks per colour step
/// @param layer layer index 0..9
void spawn_wake_unlit(
    EffectWorld& world,
    const oa::Game& game,
    const EffectHost& host,
    const FixedVec3& from,
    const FixedVec3& to,
    int32_t period,
    uint16_t layer
);

// Min then max corner of a 16.16 box.
struct Box {
    FixedVec3 low{};
    FixedVec3 high{};
};

/// Starts build or repair spray from a nozzle into the middle of a target box.
///
/// Five pixels a tick fly four units a tick from the nozzle to random points in the
/// middle 3/11 of each axis of the box, for one tick.
///
/// @param[in,out] world layers and pools the emitter joins
/// @param game current tick
/// @param host random stream and terrain queries
/// @param nozzle spray origin, 16.16 world coordinates
/// @param target box the spray lands in
/// @param layer layer index 0..9
void spawn_nano_outward(
    EffectWorld& world,
    const oa::Game& game,
    const EffectHost& host,
    const FixedVec3& nozzle,
    const Box& target,
    uint16_t layer
);

/// Starts reclaim or capture spray from the middle of a target box to a nozzle.
///
/// @param[in,out] world layers and pools the emitter joins
/// @param game current tick
/// @param host random stream and terrain queries
/// @param target box the spray starts in
/// @param nozzle point the spray flies to, 16.16 world coordinates
/// @param layer layer index 0..9
void spawn_nano_inward(
    EffectWorld& world,
    const oa::Game& game,
    const EffectHost& host,
    const Box& target,
    const FixedVec3& nozzle,
    uint16_t layer
);

/// Starts flame sprites stepping from a teleported unit's old position to its new one.
///
/// The sprites move five units a tick, one every ten ticks until `lifetime`
/// passes. A move shorter than five units starts no trail.
///
/// @param[in,out] world layers and pools the emitter joins
/// @param game current tick
/// @param host random stream and terrain queries
/// @param from old position, 16.16 world coordinates
/// @param to new position, 16.16 world coordinates
/// @param lifetime ticks the trail keeps spawning
/// @param layer layer index 0..9
void spawn_teleport_trail(
    EffectWorld& world,
    const oa::Game& game,
    const EffectHost& host,
    const FixedVec3& from,
    const FixedVec3& to,
    int32_t lifetime,
    uint16_t layer
);

/// Starts a smoke column: a light puff every `interval` ticks until `duration` passes.
///
/// @param[in,out] world layers and pools the emitter joins
/// @param game current tick
/// @param host random stream and terrain queries
/// @param position where the puffs start, 16.16 world coordinates
/// @param interval ticks between puffs
/// @param duration ticks the column keeps spawning
/// @param layer layer index 0..9
void spawn_smoke_column(
    EffectWorld& world,
    const oa::Game& game,
    const EffectHost& host,
    const FixedVec3& position,
    int32_t interval,
    int32_t duration,
    uint16_t layer
);

/// Starts one light puff.
///
/// @param[in,out] world layers and pools the emitter joins
/// @param game current tick
/// @param host random stream and terrain queries
/// @param position where it starts, 16.16 world coordinates
/// @param layer layer index 0..9
void spawn_white_smoke(
    EffectWorld& world,
    const oa::Game& game,
    const EffectHost& host,
    const FixedVec3& position,
    uint16_t layer
);

/// Starts one dark puff.
///
/// @param[in,out] world layers and pools the emitter joins
/// @param game current tick
/// @param host random stream and terrain queries
/// @param position where it starts, 16.16 world coordinates
/// @param layer layer index 0..9
void spawn_black_smoke(
    EffectWorld& world,
    const oa::Game& game,
    const EffectHost& host,
    const FixedVec3& position,
    uint16_t layer
);

/// Starts a weapon's startsmoke: one light puff of at most three frames, thirty ticks each.
///
/// @param[in,out] world layers and pools the emitter joins
/// @param game current tick
/// @param host random stream and terrain queries
/// @param position where it starts, 16.16 world coordinates
/// @param layer layer index 0..9
void spawn_start_smoke(
    EffectWorld& world,
    const oa::Game& game,
    const EffectHost& host,
    const FixedVec3& position,
    uint16_t layer
);

/// Starts a stationary flame jittered by up to one unit, living one to three ticks.
///
/// @param[in,out] world layers and pools the emitter joins
/// @param game current tick
/// @param host random stream and terrain queries
/// @param position where it starts, 16.16 world coordinates
/// @param layer layer index 0..9
void spawn_spark(
    EffectWorld& world,
    const oa::Game& game,
    const EffectHost& host,
    const FixedVec3& position,
    uint16_t layer
);

/// Starts endless light smoke from a burning feature, a puff every five ticks.
///
/// @param[in,out] world layers and pools the emitter joins
/// @param game current tick
/// @param host random stream and terrain queries
/// @param position where it starts, 16.16 world coordinates
/// @param layer layer index 0..9
void spawn_feature_smoke(
    EffectWorld& world,
    const oa::Game& game,
    const EffectHost& host,
    const FixedVec3& position,
    uint16_t layer
);

/// Moves a trail particle one step and advances its frame.
///
/// The particle's period must be nonzero.
///
/// @param[in,out] particle position advances by its delta; the frame becomes the signed
///        remainder of (frame + 1) / period
/// @return the quotient of (frame + 1) / period
int32_t step_trail_particle(TrailParticle& particle) noexcept;

/// Moves a wake particle one step and advances its colour phase.
///
/// The phase becomes the signed remainder of (phase + 1) / period; on a zero remainder
/// the value moves by value_delta, wrapping to low above high and to high below low.
///
/// @param[in,out] particle wake particle to step
/// @return `high` when the value moved, otherwise the quotient of (phase + 1) / period
/// @quirk Both compares are signed.
int32_t step_wake_particle(WakeParticle& particle) noexcept;

enum class DrawKind : uint8_t { sprite, pixel, flash, fragment };

// A pixel item, a nano-lathe or wake particle, fills a square this many
// battlefield pixels on a side, its top-left corner at the item's position.
inline constexpr int32_t pixel_item_side = 2;

// One item of a draw pass. Sprites blit `frame` of `sequence` at the 16.16
// world `position`; pixels fill a pixel_item_side square with palette index
// `color`; flashes darken through the shade table with the shade levels of
// `frame` of `sequence`; fragments draw as 3DO objects turned by `spin`.
// Layer items other than feature smoke are drawn only where the viewer's
// sight grid shows the point. `motion` and `spin_motion` say how the item
// moves and turns in a tick, so that a frame drawn between two ticks can show
// it part of the way through its step; the simulation never reads them.
struct ParticleDraw {
    DrawKind kind{};
    bool sight_gated{true};
    FixedVec3 position{};
    const formats::gaf::Sequence* sequence{};
    int32_t frame{};
    uint8_t color{};
    const ShatterFragment* fragment{};
    int16_t spin[3]{};
    /// The step each tick moves the item by, 16.16: a teleport-trail, nano,
    /// flame or wake particle's delta, and a flying fragment's velocity and
    /// the velocity it carried off its unit (a tick's fall left out); zero
    /// for smoke, flashes and explosion sprites, which hold still.
    FixedVec3 motion{};
    /// The turn each tick adds to `spin`: a fragment's spin rates; zero otherwise.
    int16_t spin_motion[3]{};
};

using ParticleVisitor = void (*)(void* context, const ParticleDraw&);

/// Visits one layer's particles in emitter, then particle, order.
///
/// @param world layers and pools
/// @param layer layer index 0..9; out of range visits nothing
/// @param context passed through to `visit`
/// @param visit called once per particle; null visits nothing
void draw_layer(const EffectWorld& world, uint16_t layer, void* context, ParticleVisitor visit);

// Screen position of the battlefield's top-left pixel, which draw_explosions
// adds to a record's camera-relative position (the battlefield rectangle
// starts there).
inline constexpr int32_t battlefield_screen_x = 0x80;
inline constexpr int32_t battlefield_screen_y = 0x20;

// What draw_explosions culls explosion records against.
struct ExplosionView {
    int32_t camera_x{};   // Game.camera_x, whole pixels
    int32_t camera_y{};   // Game.camera_y
    Rect32 battlefield{}; // Game.battlefield_rect, inclusive screen pixels
};

/// Visits every explosion record's flash, then every record's fragment and sprite.
///
/// A record whose centre projects outside the battlefield is skipped even when part
/// of its image would show. The debris pieces drawn before them are not visited.
///
/// @param world explosion table and fragment arena
/// @param view camera position and battlefield rectangle
/// @param context passed through to `visit`
/// @param visit called once per item; null visits nothing
void draw_explosions(
    const EffectWorld& world, const ExplosionView& view, void* context, ParticleVisitor visit
);

// ---- explosions -----------------------------------------------------------

/// Draws one radial flash frame of shade levels, 0xff transparent.
///
/// @param size side of the square frame, pixels
/// @param[out] shade receives size x size bytes
/// @param host rand() stream for the edge noise (one draw per pixel)
/// @return the frame origin, the truncated half side
int32_t build_flash_frame(int32_t size, uint8_t* shade, const EffectHost& host);

// Frame count, first side and last side of a flash tier.
struct FlashTierShape {
    int32_t count{};
    int32_t start_size{};
    int32_t end_size{};
};

inline constexpr FlashTierShape flash_tier_shapes[flash_tier_count]{
    {12, 0x40, 8}, {0x1e / 2, 0x80, 0x10}, {0x1e / 2, 200, 0x20}
};

/// Returns the side of one frame of a flash tier.
///
/// @param shape tier's frame count and first and last sides
/// @param frame frame index
/// @return the first side moved `frame` times by the truncated mean step; 0 for a tier
///         without frames
[[nodiscard]] int32_t flash_frame_side(const FlashTierShape& shape, int32_t frame) noexcept;

/// Fills a flash tier, two ticks a frame and not looping.
///
/// The caller sizes `shape.count` frames to flash_frame_side beforehand; a frame of any
/// other size is left blank without drawing its random numbers.
///
/// @param[in,out] tier sequence whose frames are drawn
/// @param shape tier's frame count and sides
/// @param host rand() stream
void build_flash_tier(
    formats::gaf::Sequence& tier, const FlashTierShape& shape, const EffectHost& host
);

/// Builds the three flash tiers and clears the explosion, debris and fragment tables.
///
/// The lens table 3.1c prepares alongside the flash tiers is not built.
///
/// @param[in,out] world explosion, debris and fragment tables
/// @param[in,out] tiers frame storage sized as for build_flash_tier; `world` keeps pointers to it
/// @param host rand() stream
void init_explosion_tables(
    EffectWorld& world, formats::gaf::Sequence (&tiers)[flash_tier_count], const EffectHost& host
);

/// Appends an explosion sprite record, with a flash unless none is asked for.
///
/// A land explosion above sea level also raises a short smoke column, unless
/// the world's explosion_smoke_column is off. A full table drops the record.
///
/// @param[in,out] world explosion table and emitter layers
/// @param game current tick and sea level
/// @param host random stream for the smoke column
/// @param position record centre, 16.16 world coordinates
/// @param sprite sequence to play, or null for none
/// @param flash_tier flash tier 0..2, or -1 for none
/// @param underwater true for a splash; it raises no smoke
void log_explosion(
    EffectWorld& world,
    const oa::Game& game,
    const EffectHost& host,
    const FixedVec3& position,
    const formats::gaf::Sequence* sprite,
    int32_t flash_tier,
    bool underwater
);

/// Advances debris and explosion records by one tick.
///
/// Settles debris first, then each record's fragment, sprite and flash, then drops
/// records with nothing left to show. A fragment flies under gravity and bounces off
/// the ground at half speed until the bounce is under one unit a tick; in water it is
/// spent at once. Records logged by landings this tick are stepped in the same pass.
///
/// @param[in,out] world debris, explosion table and fragment arena
/// @param game gravity, sea level and current tick
/// @param host terrain queries and random streams
void tick_explosions(EffectWorld& world, const oa::Game& game, const EffectHost& host);

/// Claims a debris slot for a whole piece.
///
/// A shattering piece breaks into fragments through shatter_piece instead, and is not
/// kept here.
///
/// @param[in,out] world debris table
/// @param piece debris request to copy into the slot
/// @return false for a shattering piece or when every slot is taken
bool add_debris(EffectWorld& world, const DebrisPiece& piece);

/// Finds the first free debris slot.
///
/// @param world debris table
/// @return the slot index, or -1 when all are taken
[[nodiscard]] int32_t find_free_debris_slot(const EffectWorld& world) noexcept;

/// Settles every held debris piece in slot order, freeing the slot of each spent one.
///
/// @param[in,out] world debris table, explosion table and layers
/// @param game gravity and sea level
/// @param host terrain queries and random streams
void settle_all_debris(EffectWorld& world, const oa::Game& game, const EffectHost& host);

/// Claims the first free fragment slot.
///
/// @param[in,out] world fragment arena
/// @return the slot, or no_fragment when all are taken
int16_t claim_fragment(EffectWorld& world) noexcept;

/// Breaks a piece into flying fragments, one explosion record per textured quad.
///
/// Every four-cornered, textured quad other than the selection primitive becomes a
/// fragment: the quad and a copy one unit (times the request's shatter value) behind
/// it, centred on the piece origin, flung with eight synced draws plus half the unit's
/// velocity. The fragments take the owner's team colour. Stops when the explosion table
/// or the fragment arena is full.
///
/// @param[in,out] world explosion table and fragment arena
/// @param state world the unit and its owner live in
/// @param host synced random stream
/// @param request debris request of the piece (explode flags and shatter thickness)
/// @param unit unit the piece belongs to
/// @param movement the movement record Unit.movement refers to, or null without one
/// @param piece loaded primitives and transformed points of the piece
void shatter_piece(
    EffectWorld& world,
    const oa::World& state,
    const EffectHost& host,
    const DebrisPiece& request,
    const oa::Unit& unit,
    const sim::unit_movement::Movement* movement,
    const ShatterPiece& piece
);

/// Flies, drops and bounces one debris piece for a tick.
///
/// On its last bounce the piece explodes when the request asked for it; in water it
/// splashes instead.
///
/// @param[in,out] piece debris piece; its lifetime counts down
/// @param[in,out] world explosion table for the landing explosion
/// @param game gravity and sea level
/// @param host terrain queries and random streams
/// @return false once the piece is spent
bool settle_debris(
    DebrisPiece& piece, EffectWorld& world, const oa::Game& game, const EffectHost& host
);

/// Starts the particles a debris piece gives off once a tick.
///
/// A smoking piece starts a light puff and a burning one a spark.
///
/// @param[in,out] world emitter layers
/// @param game current tick
/// @param host random stream
/// @param piece debris piece
void start_debris_piece_particles(
    EffectWorld& world, const oa::Game& game, const EffectHost& host, const DebrisPiece& piece
);

/// Starts every held debris piece's puff and spark, in slot order, wherever it is.
///
/// The match does this once at the end of each tick, whether or not the tick
/// is drawn, so that what is drawn never changes the particles or the random
/// stream.
///
/// @param[in,out] world debris table and emitter layers
/// @param game current tick
/// @param host random stream
void start_debris_particles(EffectWorld& world, const oa::Game& game, const EffectHost& host);

using DebrisVisitor = void (*)(void* context, const DebrisPiece&);

/// Draws every held debris piece in slot order.
///
/// Drawing changes nothing: the pieces' particles start in the tick
/// (start_debris_particles).
///
/// @param world debris table
/// @param context passed through to `visit`
/// @param visit called once per held piece; null draws nothing
void draw_debris(const EffectWorld& world, void* context, DebrisVisitor visit);

/// Logs the explosion sprites of a COB explode's bitmap flags.
///
/// Bits 0x100..0x2000 select explosion, explode2..5 and nuke1; each set bit logs one
/// record with the large flash.
///
/// @param[in,out] world explosion table and emitter layers
/// @param game current tick and sea level
/// @param host random stream
/// @param position explosion centre, 16.16 world coordinates
/// @param cob_flags COB explode flags
void log_cob_explosions(
    EffectWorld& world,
    const oa::Game& game,
    const EffectHost& host,
    const FixedVec3& position,
    uint32_t cob_flags
);
} // namespace oa::sim::effect_particles
