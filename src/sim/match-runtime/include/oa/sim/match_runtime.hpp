// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "oa/sim/match_runtime/attack_orders.hpp"
#include "oa/sim/match_runtime/construction_orders.hpp"
#include "oa/sim/match_runtime/event_hooks.hpp"
#include "oa/sim/match_runtime/match_trace.hpp"
#include "oa/sim/match_runtime/spawn_bridge.hpp"
#include "oa/sim/combat_state.hpp"
#include "oa/sim/spatial_state/cell_classifier.hpp"
#include "oa/core/move_class.h"
#include "oa/sim/ground_orders/movement_map.hpp"
#include "oa/sim/spatial_state/spatial.hpp"
#include "oa/sim/unit_movement/terrain.hpp"
#include "oa/sim/visibility_state.hpp"
#include <cstdint>
#include <map>
#include "oa/sim/scenario/state.hpp"
#include "oa/sim/scenario/outcome.hpp"
#include "oa/sim/unit_activation.hpp"
#include "oa/sim/unit_health.hpp"
#include "oa/sim/unit_health/paralysis.hpp"
#include <functional>
#include "oa/sim/ground_orders/ground_runtime.hpp"
#include "oa/sim/air/driver.hpp"
#include "oa/sim/ground_orders/search_worker.hpp"
#include "oa/sim/detection.hpp"
#include "oa/sim/world_environment/wind.hpp"
#include "oa/sim/effect_particles.hpp"
#include "oa/sim/feature_runtime.hpp"
#include "oa/data/persist/save_orders.hpp"
#include "oa/sim/weapon_execution/weapon_launch.hpp"
#include <array>
#include <optional>
#include <span>
#include <string>

namespace oa::sim::match_runtime {
class TickHost;
class TargetHost;

// How a unit died: the damage path stores it in Unit.damage_kind and the
// kill handler receives it as KillOutcome.kind. Kinds 1/3/6 take the
// kill-statistics paths; kinds 4/7/9 and everything else skip straight to
// the shared tail.
enum class DeathKind : uint8_t {
    weapon = 1,        // kill-statistics path
    self_destruct = 3, // own statistics branch, then kill statistics
    captured = 4,      // no kill statistics
    reclaim = 5,       // credits (1 - build fraction) * metal cost
    cargo = 6,         // carried unit killed with its transport; kill statistics
    dismissed = 7,     // immediate corpse; no kill statistics
    cancelled = 9,     // cancelled nanoframe; no kill statistics
};

/// How a unit's death comes out: the kill handler settles it on the machine
/// that simulates the unit, MultiplayerHooks::unit_killed shares it, and
/// every machine tears the unit down with it (Match::teardown_dead_unit,
/// Match::apply_kill).
struct KillOutcome {
    /// A DeathKind, or the other damage kind the unit's record held (0 for a
    /// unit whose slot another player's new unit takes).
    DeathKind kind{};
    int8_t killed_percent{}; ///< Killed's first argument; above zero a finished unit explodes
    /// Wreck level: the low nibble of the Killed script's second result, 1
    /// for a dismissed unit; 0 leaves no wreck.
    uint8_t wreck_level{};
};

/// Tells whether the unit is its owner's side commander: the side's
/// commander name against the type's UnitName, ignoring case.
///
/// The kill handler asks this rather than the FBI commander flag.
///
/// @param world Canonical world with the side table.
/// @param unit Unit to test.
/// @return False as well when the unit has no owner, setup block, valid
///     side or type.
[[nodiscard]] bool side_commander(const oa::World& world, const oa::Unit& unit) noexcept;

// Engine boundaries the match needs from its application; they are required,
// not successful placeholder methods.
struct OfflineServices : Effects, sim::spatial_state::Host {
    /// Plays a unit's command speech (order acknowledged, failed, complete).
    ///
    /// @param slot Speaking unit.
    /// @param category Speech category (5 order, 7 failed, 8 complete, ...).
    virtual void command_sound(sim::unit_spawn::Slot& slot, uint32_t category) = 0;
    /// Plays a unit's activation or deactivation sound.
    ///
    /// @param slot Unit whose state changed.
    /// @param sound Which sound the change calls for.
    virtual void
    activation_sound(sim::unit_spawn::Slot& slot, sim::unit_activation::Sound sound) = 0;
    /// Tells the attachment UI that a unit's state changed.
    ///
    /// @param slot Unit whose state changed.
    /// @param value Event bits of the change.
    virtual void attachment_notification(sim::unit_spawn::Slot& slot, uint32_t value) = 0;
    /// Redraws the order panel for a selected unit.
    ///
    /// @param slot Unit the panel shows.
    virtual void refresh_selected_unit(sim::unit_spawn::Slot& slot) = 0;
};

/// What a match asks its application for when a unit speaks with its order's
/// own caption.
struct SpeechHooks {
    void* context{};
    /// Queues a unit's speech of a category, captioned with the order's own
    /// text in place of the category's; null plays the category through
    /// OfflineServices::command_sound, without the order's caption.
    ///
    /// @param context The hooks' context.
    /// @param slot Speaking unit.
    /// @param category Speech category (5 order, 7 failed, 8 complete, ...).
    /// @param caption The caption, in the game's English wording, which the
    ///     application translates.
    void (*speak)(
        void* context, sim::unit_spawn::Slot& slot, uint32_t category, const char* caption
    ){};
};

struct RuntimeTypeFields {
    const data::unit_definitions::UnitDefinition* definition{};
    std::span<const uint8_t> yard_mask;
    std::optional<sim::unit_spawn::AssetHandle>
        movement_class; // resolved UnitDef.move_class; explicit zero means missing
    const data::unit_definitions::RuntimeDefinitionMetadata* runtime_metadata{};
    const data::unit_definitions::UnitTargetCategoryMasks* target_masks{};
    int16_t corpse_feature = -1; // UnitDef.corpse: FeatureDef index of corpse=, -1 none
};

struct OfflineInputs {
    const formats::tnt::Map& map;
    std::span<const sim::unit_spawn::LoadedType> loaded; // includes reserved index0
    std::span<sim::unit_spawn::Type> types; // includes reserved index0; stable lifetime
    std::span<const RuntimeTypeFields> fields;
    const sim::combat_state::WeaponRegistry& weapons;
    // MapPlot.metal (settings metal + feature overlays), NOT TNT padding.
    std::span<const sim::visibility_state::TerrainCell> terrain_values;
    std::span<const sim::visibility_state::SightMask> sight_masks;
    int32_t sight_width{}, sight_height{};
    uint16_t per_player_limit{}, visibility_flags{};
    uint8_t viewpoint_player{};
    int32_t clock_scale{};
    uint32_t random_seed{};
    sim::scenario::DefinitionHost*
        scenario_definitions{}; // required selected OTA GlobalHeader, used during construction
    std::function<uint32_t()>
        uptime_milliseconds; // platform uptime in milliseconds, for the floating ground fit
    // Resolved map extrema and features, required for moving collision. The
    // match copies them while it is constructed and keeps no reference to them.
    std::span<const sim::spatial_state::Plot> collision_plots;
    std::optional<sim::visibility_state::AltitudeSightData>
        altitude_sight; // real LOS terrain and ray patterns
    int32_t minimum_wind{};
    int32_t maximum_wind{};
    float tidal_strength{};
    std::span<const oa::FeatureDef> feature_defs{}; // indexed by the plot feature words
    // Named entry of a loaded GAF archive (the effect and weapon explosion
    // entries resolve at load); an empty archive is FX.GAF.
    std::function<const formats::gaf::Sequence*(std::string_view archive, std::string_view entry)>
        effect_sequence{};
    // One frame of a GAF sequence the FeatureDef table references (the refs
    // its loader's host issued); false past the last frame. Without it no
    // feature sequence plays and burning features never burn out.
    std::function<
        bool(oa_ref32 sequence, uint16_t frame, sim::feature_runtime::FeatureSequenceFrame& out)>
        feature_sequence_frame{};
    // UnitDef records the FBI loader filled, indexed like types.
    // Without them each record is assembled from the runtime type and its
    // parsed definition.
    std::span<const oa::UnitDef> unit_defs{};
    // The mission schema's [features] placements (placed after the map's own
    // when no save is loading). Their FeatureDefs must already be in
    // feature_defs.
    std::span<const sim::feature_runtime::FeaturePlacement> mission_features{};
    // A savegame is resuming (Game.saved_game): only the map's blocking
    // markers are placed; its features and the mission schema's are left for
    // the save's Features section to place.
    bool resuming_saved_game{};
    // One 3DO object's primitives as the model loader leaves them: sorted and
    // with their texture flag words, which decide the quads a shattering
    // piece breaks into. Returns the loaded
    // object's selection primitive: 0 when the sort moved a selection
    // primitive to the front, otherwise -1. Without it a shattering piece is
    // dropped whole, drawing nothing.
    std::function<int32_t(
        const formats::objects3d::Model&,
        uint32_t object,
        std::vector<sim::effect_particles::PiecePrimitive>& out
    )>
        loaded_primitives{};
};

/// Returns a canonical 16.16 point as the raw words the legacy views use.
///
/// @param point Signed 16.16 point.
/// @return The x, y, z words as unsigned bit patterns.
inline std::array<uint32_t, 3> fixed_words(const oa::FixedVec3& point) noexcept {
    return {
        static_cast<uint32_t>(point.x),
        static_cast<uint32_t>(point.y),
        static_cast<uint32_t>(point.z)
    };
}

// A shot shared with the other players, or one fired on another player's
// machine that apply_shot launches here. A meteor fills only the start, its
// fall velocity in `target` and the weapon.
struct ShotEvent {
    FixedVec3 start{};  // muzzle; the unit's position for a fixed line weapon
    FixedVec3 target{}; // aim point
    uint8_t weapon_id{};
    bool interceptor{};
    int16_t aim_heading{}; // the slot's angles once it has fired
    int16_t aim_pitch{};
    uint16_t target_unit{}; // 0 = none
    uint16_t source_unit{}; // 0 = none
    uint8_t slot{};
};

// What a multiplayer match shares with the other players. A match whose
// multiplayer tick reaches a branch without its entry stops there, unless the
// entry says what null does.
struct MultiplayerHooks {
    void* context{};
    /// Runs the per-tick work of one local player.
    ///
    /// @param context The hooks' context.
    /// @param player Local player record.
    void (*local_player_ticked)(void* context, oa::Player& player){};
    /// Shares a unit created on this machine.
    ///
    /// @param context The hooks' context.
    /// @param unit Unit slot.
    void (*unit_created)(void* context, uint16_t unit){};
    /// Shares that the state flags (activation, building, cloak) of a unit
    /// simulated here changed.
    ///
    /// @param context The hooks' context.
    /// @param unit Unit slot.
    /// @param flags The unit's new state flags.
    void (*unit_flags_changed)(void* context, uint16_t unit, uint8_t flags){};
    /// Shares that the local players' setup blocks changed (a defeated
    /// player now watches).
    ///
    /// @param context The hooks' context.
    void (*player_status_changed)(void* context){};
    /// Returns the player a shared health event goes out for.
    ///
    /// @param context The hooks' context.
    /// @param source_unit Unit slot of the event's source, 0 for none.
    /// @return The source unit's owner, or the local default for unit 0.
    sim::unit_health::RouteIdentity (*health_route)(void* context, uint16_t source_unit){};
    /// Shares a health event on a unit simulated elsewhere.
    ///
    /// @param context The hooks' context.
    /// @param route Player the event goes out for.
    /// @param event Amount, kind, direction and units of the event.
    void (*health_shared)(
        void* context,
        sim::unit_health::RouteIdentity route,
        const sim::unit_health::HealthEvent& event
    ){};
    /// Shares a script the unit started that the other players start as
    /// well.
    ///
    /// @param context The hooks' context.
    /// @param unit Unit slot.
    /// @param function The script's COB index, -1 when the unit lacks it.
    /// @param argument_count How many of the four locals are arguments.
    /// @param locals The four locals.
    void (*script_started)(
        void* context,
        uint16_t unit,
        int16_t function,
        uint8_t argument_count,
        const std::array<uint32_t, 4>& locals
    ){};
    /// Shares a shot a local unit fired, or a meteor that fell, for the other
    /// players to launch as well.
    ///
    /// @param context The hooks' context.
    /// @param shot The shot as the other players launch it.
    void (*shot_fired)(void* context, const ShotEvent& shot){};
    /// Shares the death of a unit simulated here once the kill handler has
    /// settled it (the Killed query, and no wreck for an unfinished unit),
    /// before it clears the unit's orders and tears it down, so the unit's
    /// record still holds its owner and last attacker. Every death kind is
    /// shared; a unit simulated elsewhere is not. Null shares nothing.
    ///
    /// @param context The hooks' context.
    /// @param unit Unit slot.
    /// @param outcome The death kind, Killed percentage and wreck level the
    ///     unit dies with.
    void (*unit_killed)(void* context, uint16_t unit, const KillOutcome& outcome){};
    /// Tells whether a weapon hit on a feature is settled on another
    /// player's machine, which then applies it instead of this one.
    ///
    /// @param context The hooks' context.
    /// @param weapon_id The hitting weapon's WeaponDef.weapon_id.
    /// @param cell_x Hit plot column.
    /// @param cell_z Hit plot row.
    /// @return True when the hit went to that machine and nothing is applied
    ///     here; null applies every hit here.
    bool (*feature_hit_elsewhere)(
        void* context, uint8_t weapon_id, int32_t cell_x, int32_t cell_z
    ){};
    /// Shares a feature change settled here: a fire that started here, a
    /// feature weapon damage destroyed, one a unit finished reclaiming, or a
    /// wreck a unit simulated here raised back into a unit. Null shares
    /// nothing.
    ///
    /// @param context The hooks' context.
    /// @param change What happened to the feature.
    /// @param cell_x Feature plot column; the column of a wreck's origin plot
    ///     for a resurrection.
    /// @param cell_z Feature plot row; the row of a wreck's origin plot for a
    ///     resurrection.
    /// @param reclaimer Unit slot of the unit that finished reclaiming or
    ///     resurrecting it, 0 for a fire or a destruction.
    void (*feature_changed)(
        void* context,
        sim::feature_runtime::FeatureChange change,
        int32_t cell_x,
        int32_t cell_z,
        uint16_t reclaimer
    ){};
    /// Shares a shot an interceptor's blast set off here, once the shot has
    /// been detonated: the other players set off the same shot, then the
    /// interceptor's own. Called in projectile order for each live shot
    /// inside the blast of an interceptor whose player is simulated here;
    /// an interceptor simulated elsewhere sets nothing off here (its blast
    /// damages nothing here), so its own machine's notice does. Null shares
    /// nothing.
    ///
    /// @param context The hooks' context.
    /// @param shot The shot the blast set off, detonated; its target point
    ///     and weapon are as it flew.
    /// @param interceptor The interceptor's shot whose blast set it off.
    void (*shot_intercepted)(
        void* context, const oa::Projectile& shot, const oa::Projectile& interceptor
    ){};
    /// Shares that a unit is carried on another at a piece, or set down,
    /// just before the link applies here. Called for every link
    /// set_carry_link accepts, whichever machine simulates the two units; a
    /// link applied through Match::apply_carry_link is not shared again.
    /// Null shares nothing.
    ///
    /// @param context The hooks' context.
    /// @param unit Carried unit slot.
    /// @param carrier Carrier slot, 0 when the unit is set down.
    /// @param piece COB piece of the carrier, -1 for none.
    /// @param mode Movement layer the carried unit takes.
    void (*carry_link_changed)(
        void* context, uint16_t unit, uint16_t carrier, int8_t piece, uint8_t mode
    ){};
    /// Shares that a unit simulated here is finished, while the run flag
    /// (Game.session_flags bit 0) is set: its builder's work completed it,
    /// or it is a building (bmcode 0) created already finished, which names
    /// itself as its builder and is shared after unit_created. The other
    /// players finish their copy (Match::finish_unit). Null shares nothing.
    ///
    /// @param context The hooks' context.
    /// @param unit Finished unit slot.
    /// @param builder Slot of the unit whose work finished it; `unit` for a
    ///     building created finished.
    void (*unit_finished)(void* context, uint16_t unit, uint16_t builder){};
    /// Hands a unit simulated here to a player another machine simulates,
    /// just before the unit dies here as captured: that machine creates the
    /// unit for its player from the unit's build progress, health,
    /// orientation and weapon stockpiles (Match::transfer_unit with a
    /// TransferredUnit). Null hands nothing over; the unit still dies.
    ///
    /// @param context The hooks' context.
    /// @param unit Unit slot, still live and still owned by its old owner.
    /// @param new_owner Player index 0..9 of the receiving player.
    void (*unit_transferred)(void* context, uint16_t unit, uint8_t new_owner){};
};

/// The state a unit takes when another player's machine hands it to a
/// player simulated here (MultiplayerHooks::unit_transferred there,
/// Match::transfer_unit here).
struct TransferredUnit {
    float build_remaining{}; ///< Unit.build_remaining the copy takes; 0 for a finished unit
    int16_t health{};        ///< Unit.health the copy takes
    int16_t bank{};          ///< Unit.bank the copy takes
    uint16_t heading{};      ///< Unit.heading the copy takes
    int16_t pitch{};         ///< Unit.pitch the copy takes
    /// UnitWeapon.stockpile per weapon slot; a slot whose weapon is not
    /// enabled on the copy keeps its own.
    std::array<uint8_t, OA_UNIT_WEAPON_COUNT> stockpiles{};
};

// Owns the real unit pool, model/VM objects, weapon slots, spatial plots/buckets
// and sight grids. Borrowed assets, type tables, masks and services must outlive it.
class Match final : private SpawnSubsystems, private UnitValueHost {
  public:

    /// Builds the match: the unit pool, type tables, map plots and features,
    /// sight grids, movement maps, scenario conditions, wind and effect world.
    ///
    /// Inconsistent tables, a sight grid over 16M cells, a missing scenario
    /// definition host or a viewpoint outside the ten players throw.
    ///
    /// @param input Map, type tables, masks and loaders; the borrowed spans
    ///     must outlive the match.
    /// @param services Engine boundaries; must outlive the match.
    Match(const OfflineInputs& input, OfflineServices& services);
    Match(const Match&) = delete;
    Match& operator=(const Match&) = delete;

    /// Returns the canonical live state; every unit, player and game-state
    /// field lives here.
    oa::World& state() noexcept { return *state_.world; }

    /// Returns the canonical live state.
    const oa::World& state() const noexcept { return *state_.world; }

    /// Returns the legacy simulation view over state(); see
    /// oa/sim/unit_spawn/legacy_views.hpp.
    sim::simulation_state::World& simulation() noexcept { return simulation_; }

    /// Returns the legacy unit-spawn view over state().
    sim::unit_spawn::World& world() noexcept { return world_; }

    /// Returns the legacy unit-spawn view over state().
    const sim::unit_spawn::World& world() const noexcept { return world_; }

    /// Copies OfflineInputs.types into state().unit_defs again; call it after
    /// editing them.
    void reload_unit_defs() { state_.load_types(input_); }

    /// Takes one type's reloaded FBI record; the category handles stay this
    /// match's.
    ///
    /// @param type Type index; one outside the table throws.
    /// @param record The new UnitDef record.
    void replace_unit_def(std::size_t type, const oa::UnitDef& record);

    /// Returns a unit's legacy order queue.
    ///
    /// @param index Unit slot; one outside the pool throws.
    /// @return The unit's primary and secondary queue heads.
    sim::simulation_state::OrderQueue& orders(uint16_t index) { return state_.orders.at(index); }

    /// Returns a unit's legacy order queue.
    ///
    /// @param index Unit slot; one outside the pool throws.
    /// @return The unit's primary and secondary queue heads.
    const sim::simulation_state::OrderQueue& orders(uint16_t index) const {
        return state_.orders.at(index);
    }

    /// Creates a unit through the unit-spawn sequence (SpawnBridge::create).
    ///
    /// @param request Type, player, position, slot and state of the unit.
    /// @return The new unit's slot, or null for an ordinary refusal.
    sim::unit_spawn::Slot* create(const sim::unit_spawn::Request& request);
    /// Places a player's commander at a start position, centring the
    /// viewpoint player's view on it (SpawnBridge::start_player).
    ///
    /// @param player Player index 0..9.
    /// @param setup The player's side and starting resources.
    /// @param markers The map's start markers.
    /// @param start_index Start position to use.
    /// @param viewport_width View width in pixels.
    /// @param viewport_height View height in pixels.
    /// @param host Commander type, missing-start and camera services.
    /// @return Whether the start position was found, and the commander.
    sim::unit_spawn::StartResult start_player(
        uint8_t player,
        const sim::unit_spawn::PlayerSetup& setup,
        std::span<const sim::unit_spawn::StartMarker> markers,
        int32_t start_index,
        int32_t viewport_width,
        int32_t viewport_height,
        sim::unit_spawn::StartHost& host
    );
    /// Returns a unit's model and script instance.
    ///
    /// @param index Unit slot; one outside the pool throws.
    /// @return The instance, or null for an empty slot.
    UnitInstance* instance(uint16_t index);

    /// Draws from the match's shared stream.
    ///
    /// @param bound Exclusive upper limit (see SharedRandom::bounded).
    /// @return A value below `bound`, or 0 for a bound below 2.
    uint32_t random_bounded(uint32_t bound) noexcept { return random_.bounded(bound); }

    /// Returns the shared stream's state.
    uint32_t random_state() const noexcept { return random_.state(); }

    /// Returns the LCG stream's seed.
    uint32_t lcg_state() const noexcept { return lcg_seed_; }

    /// Writes the trace stream (oa/sim/match_runtime/match_trace.hpp) from the next tick
    /// on, headed by the match's random seed.
    ///
    /// @param path File of the trace stream.
    /// @param unit_path File of the per-unit text dump; empty writes none.
    /// @return False when a file cannot be created.
    bool record_trace(const std::string& path, const std::string& unit_path = {});

    /// Sets the computer players' difficulty.
    ///
    /// @param difficulty OA_DIFFICULTY_* value.
    void set_difficulty(int32_t difficulty) noexcept { state().game.difficulty = difficulty; }

    /// Returns the computer players' difficulty (OA_DIFFICULTY_*).
    [[nodiscard]] int32_t difficulty() const noexcept { return state().game.difficulty; }

    /// Sets what the match asks when a unit speaks with its order's own
    /// caption; without it every speech plays its category alone.
    ///
    /// @param hooks The application's speech hooks.
    void set_speech_hooks(const SpeechHooks& hooks) noexcept { speech_hooks_ = hooks; }

    /// Counts the children whose parent link points at this unit.
    ///
    /// @param index Unit slot.
    /// @return The number of units it carries.
    uint32_t loaded_child_count(uint16_t index) const;

    /// Returns a unit's movement object.
    ///
    /// @param index Unit slot; one outside the pool throws.
    /// @return The movement object, or null for a unit without one.
    sim::ground_orders::GroundRuntime* ground_runtime(uint16_t index) {
        return movement_.at(index).get();
    }

    /// Returns a unit's movement object.
    ///
    /// @param index Unit slot; one outside the pool throws.
    /// @return The movement object, or null for a unit without one.
    const sim::ground_orders::GroundRuntime* ground_runtime(uint16_t index) const {
        return movement_.at(index).get();
    }

    // One order of the primary queue for the shift-held order overlay.
    struct QueuedCommandView {
        uint8_t kind{};
        sim::ground_orders::Point destination{};
        int32_t build_type{};
    };

    /// Visits the orders of a unit's primary queue for the shift-held order
    /// overlay, skipping idle orders (the command flags' idle bit, 0x40); an
    /// order aimed at a unit shows the unit's position.
    ///
    /// @param index Unit slot; one outside the pool visits nothing.
    /// @param fn Called once per order, head first.
    void visit_primary_queue(
        uint16_t index, const std::function<void(const QueuedCommandView&)>& fn
    ) const;

    /// Issues an order of any mission kind, built from the kind's descriptor,
    /// then queued.
    ///
    /// Unless `queue` is set the unit's orders are cleared first, when the
    /// descriptor does not keep them. A descriptor whose preserve flags carry
    /// the queue-head bit (0x20), or a secondary-queue order, goes to the
    /// head of its queue; others follow the queue-tail mark.
    ///
    /// @param unit Unit slot; an inactive unit throws.
    /// @param kind Mission kind.
    /// @param queue Whether the command was queued (shift held).
    /// @param target Target unit slot, 0 for none; kept only when the
    ///     descriptor takes one.
    /// @param point Signed 16.16 map point, or null for none.
    /// @param parameter_1 The order's first parameter (type, slot or duration
    ///     by kind).
    /// @param parameter_2 The order's second parameter (count or radius by
    ///     kind).
    /// @return The new order.
    sim::simulation_state::Order& issue_order(
        uint16_t unit,
        uint8_t kind,
        bool queue,
        uint16_t target,
        const sim::ground_orders::Point* point,
        int32_t parameter_1,
        int32_t parameter_2
    );

    /// Removes the first order of the unit's primary queue of a kind whose
    /// target and point match.
    ///
    /// @param unit Unit slot.
    /// @param kind Mission kind.
    /// @param target Target unit slot to match, 0 for any.
    /// @param point Signed 16.16 point the order's must lie within 16 pixels
    ///     of on x and on z, or null for any.
    /// @return True when an order was removed.
    bool cancel_queued_order(
        uint16_t unit, uint8_t kind, uint16_t target, const sim::ground_orders::Point* point
    );

    /// Issues an order, except that a queued order removes its match
    /// (cancel_queued_order) instead of being issued.
    ///
    /// @param unit Unit slot.
    /// @param kind Mission kind.
    /// @param queue Whether the command was queued (shift held).
    /// @param target Target unit slot, 0 for none.
    /// @param point Signed 16.16 map point, or null for none.
    /// @param parameter_1 The order's first parameter.
    /// @param parameter_2 The order's second parameter.
    void issue_or_cancel_order(
        uint16_t unit,
        uint8_t kind,
        bool queue,
        uint16_t target,
        const sim::ground_orders::Point* point,
        int32_t parameter_1,
        int32_t parameter_2
    );

    // An order record, field by field.
    struct OrderRecordView {
        uint8_t kind{};
        uint8_t preserve_flags{};
        uint8_t command_flags{};
        uint8_t flags{};
        uint16_t target{}; // unit, 0 for none
        sim::ground_orders::Point point{};
        // Read from the construction family's copies.
        int32_t parameter_1{}, parameter_2{};
        int16_t seen_x{}, seen_z{}; // where the order overlays last saw the target
        uint32_t issue_tick{};
    };

    /// Copies the records of a unit's primary or secondary queue, head first.
    ///
    /// Inspection only: the tests read what issue_order built; the match walks
    /// the queues itself.
    ///
    /// @param unit Unit slot; one outside the pool writes nothing.
    /// @param secondary Reads the secondary queue instead of the primary.
    /// @param[out] out At least `capacity` records.
    /// @param capacity Most records to write.
    /// @return How many records were written.
    std::size_t
    queue_records(uint16_t unit, bool secondary, OrderRecordView* out, std::size_t capacity) const;
    /// Records the order overlays' write-back: an order of the unit's primary
    /// queue saw its target, and takes the target-seen bit (0x20) of its
    /// flags.
    ///
    /// @param unit Unit slot; one outside the pool is ignored.
    /// @param position Index of the order in the primary queue, head 0.
    /// @param x Where the target was seen, map pixels (Order.seen_x).
    /// @param z Where the target was seen, map pixels (Order.seen_z).
    void note_order_target_seen(uint16_t unit, std::size_t position, int16_t x, int16_t z);

    /// Hands the order words and goal a savegame keeps for each order of the
    /// unit's primary queue, then its secondary queue, head first, to a
    /// visitor.
    ///
    /// @param unit Unit slot; one outside the pool visits nothing.
    /// @param visit Called once per order; null visits nothing.
    /// @param walk The visitor's context.
    void visit_saved_orders(uint16_t unit, data::persist::SavedOrderVisit visit, void* walk) const;

    // Where the unit restore links the next saved order of each queue: the
    // queue heads at first, so the saved orders of a queue replace the ones
    // the unit's creation or carrier link put there, which stay allocated but
    // unqueued as in 3.1c.
    struct SavedOrderTails {
        sim::simulation_state::Order** primary{};
        sim::simulation_state::Order** secondary{};
    };

    /// Returns the restore's link points of a unit's queues: their heads.
    ///
    /// @param unit Unit slot; one outside the pool throws.
    /// @return The primary and secondary queue heads.
    SavedOrderTails saved_order_tails(uint16_t unit);
    /// Rebuilds a saved order and links it at the tail of the primary queue,
    /// or of the secondary queue when bit 2 of the order's flags is set.
    ///
    /// Every order family's copy of a word takes the saved value, the issue
    /// tick takes the current tick and the target is linked whatever the
    /// command flags say. The goal is rebuilt but not installed.
    ///
    /// @param unit Unit slot.
    /// @param saved The saved order words.
    /// @param goal The saved goal; its units must be slots restored before
    ///     it, or 0.
    /// @param[in,out] tails Link points from saved_order_tails; advanced past
    ///     the new order.
    /// @return The restored order.
    sim::simulation_state::Order& restore_saved_order(
        uint16_t unit,
        const data::persist::SavedOrder& saved,
        const data::persist::SavedGoal& goal,
        SavedOrderTails& tails
    );
    /// Hands the goal of the unit's head order, if it has one, to the unit's
    /// movement object: its ground navigator for a ground goal, its air
    /// driver for an air goal.
    ///
    /// @param unit Unit slot; one outside the pool throws.
    void install_head_goal(uint16_t unit);

    /// Returns a unit's match-side state (model instance and its generation,
    /// SFX occupancy).
    ///
    /// @param index Unit slot; one outside the pool throws.
    /// @return The slot's state.
    SlotRuntime& runtime_state(uint16_t index) { return bridge_->runtime(slots_.at(index)); }

    /// Steps the COB contexts of every unit with a script, and nothing else.
    ///
    /// Full unit ticks need tick() (orders, movement, combat and per-player
    /// work).
    ///
    /// @param elapsed Clock time since the last step.
    void tick_scripts(uint32_t elapsed);
    /// Sets a player's alliance row, in the match and in the player record.
    ///
    /// Configure the local identity and alliance row after player setup and
    /// before the first full tick.
    ///
    /// @param player Player index 0..9; another throws.
    /// @param allies Nonzero for each player it counts as an ally.
    void configure_player_alliances(uint8_t player, const std::array<uint8_t, 10>& allies);
    /// Takes a player's alliances from its player record's alliance row
    /// (Player.alliance) as they change during the match, through the team
    /// panels or another player's machine: every entry but the player's
    /// own. A player whose row was never configured is left alone.
    ///
    /// @param player Player index 0..9; another does nothing.
    void follow_player_alliances(uint8_t player);
    /// Gives one player every cell another has mapped, as sharing a map
    /// does (visibility_state::share_mapped_cells).
    ///
    /// @param from Player index 0..9 whose map is shared; another does nothing.
    /// @param to Player index 0..9 receiving it; another does nothing.
    void share_mapped_area(uint8_t from, uint8_t to);
    /// Sets up the local player's victory and defeat checks; full ticks
    /// require it. It may be called once; a second call throws.
    ///
    /// @param local_player Local player index; one outside the table throws.
    /// @param local_allies The local player's alliance row.
    /// @param defeat_allowed Whether the local player may be defeated.
    /// @param campaign Selects the campaign conditions.
    /// @param multiplayer Selects the multiplayer victory test over the
    ///     skirmish one.
    void configure_outcomes(
        uint8_t local_player,
        const std::array<uint8_t, 10>& local_allies,
        bool defeat_allowed,
        bool campaign = false,
        bool multiplayer = false
    );

    /// Returns the local player's final outcome, or ongoing.
    sim::scenario::Outcome outcome() const noexcept { return outcome_result_; }

    /// Returns the outcome countdown and flags.
    const sim::scenario::OutcomeState& outcome_state() const noexcept { return outcome_state_; }

    /// Returns the mission's victory and defeat conditions.
    sim::scenario::Controller& scenario_controller() noexcept { return scenario_; }

    /// Runs the unit sweep with an external simulation host, then the
    /// projectiles, explosions and path search.
    ///
    /// configure_outcomes must have run; otherwise this throws.
    ///
    /// @param host Order, movement, combat and per-player services of the
    ///     unit sweep.
    void tick(sim::simulation_state::Host& host);
    /// Runs one full simulation tick with the match's own host: the unit
    /// sweep, projectiles and explosions, the players' controllers, sight
    /// and economy, features, wind, the meteor storm and particles.
    ///
    /// A gameplay branch the engine does not support throws.
    void tick();
    /// Flies, bursts and collides every record of the projectile pool once,
    /// then compacts the pool.
    ///
    /// @quirk The pass walks the count it started with by position: shots
    ///     retired earlier in the pass still fly and collide.
    void update_projectiles();
    /// Applies the damage a shot deals a unit: the weapon's DAMAGE entry
    /// for the unit's UNITNAME, scaled for an area blast's edge and the
    /// shooter's veterancy, as kind 1 or kind 2 for a paralyzer.
    ///
    /// @param shot The shot; one without a weapon definition throws.
    /// @param target Unit hit.
    /// @param scale Edge effectiveness, 1.0 for a direct hit.
    /// @return The damage applied, in health points.
    int32_t apply_projectile_damage(
        oa::Projectile& shot, sim::unit_spawn::Slot& target, float scale = 1.0F
    );
    /// Strikes one unit with a shot: its damage, then its shooter's hit
    /// reaction, as enemy damage when the shot's owner is not the unit's and
    /// as friendly damage otherwise.
    ///
    /// @param shot The shot.
    /// @param target Unit hit.
    /// @return The damage applied, in health points.
    int32_t strike_unit(oa::Projectile& shot, sim::unit_spawn::Slot& target);
    /// Records the shooter's hit reaction (the high byte of its events word)
    /// from the enemy and friendly damage one shot dealt.
    ///
    /// @param source The shooter.
    /// @param enemy Damage dealt to other players' units.
    /// @param friendly Damage dealt to the shooter's own player's units.
    /// @quirk A shot with no shooter (Projectile.source clear) records no
    ///     reaction and is skipped before this is called.
    void record_shot_reaction(sim::unit_spawn::Slot& source, int32_t enemy, int32_t friendly);
    /// Retires the shot unless noexplode, draws its explosion and, for a
    /// shot of a player simulated here, damages `direct` alone when the
    /// blast is under 17 wide, else the area around it.
    ///
    /// A nosealeveltrigger sea swallows the shot: it is retired, silent and
    /// harmless.
    ///
    /// @param shot The shot.
    /// @param direct Unit the shot struck, or null for none.
    /// @quirk A noexplode shot (the D-gun's ball) flies on, so the contact
    ///     test detonates it again on every tick it strikes an enemy or runs
    ///     below the ground: one blast, hit sound and area of damage a tick,
    ///     a trail along its path until its range runs out.
    void detonate(oa::Projectile& shot, sim::unit_spawn::Slot* direct);
    /// Prepares a path search job for a unit: the occupancy of every other
    /// unit with a movement object and the unit's class map.
    ///
    /// @param slot Searching unit.
    /// @param[out] begin Job inputs: occupancy feed, world size, change tick.
    /// @param[out] map The unit's movement class map.
    /// @return False when the unit has no movement object or known class.
    bool prepare_search_job(
        sim::unit_spawn::Slot& slot,
        sim::ground_orders::SearchBegin& begin,
        sim::ground_orders::MovementMap*& map
    );
    /// Advances the path search one slice: starts a job for the next player
    /// with a waiting unit, expands it and publishes a finished route.
    void advance_path_search();

    /// Returns the path search's worker, whose cell map the debug grid's
    /// search view draws.
    const sim::ground_orders::SearchWorker& path_search_worker() const noexcept {
        return search_.worker;
    }

    /// Returns the path search's job state: its per-tick node credit and base
    /// heuristic weight are the ones the "Search" command sets.
    sim::ground_orders::SearchPlayerJobState& path_search_jobs() noexcept { return search_.jobs; }

    /// Returns the persistent map of a movement class.
    ///
    /// @param movement_class The UnitDef.move_class handle units copy to their
    ///     movement object.
    /// @return The map, or null when no loaded type names the class.
    const sim::ground_orders::MovementMap*
    movement_map(sim::unit_spawn::AssetHandle movement_class) const;
    /// Returns Game.player_count, or the live player slots while setup
    /// leaves it zero.
    uint16_t participant_count() const noexcept;
    /// Visits the units of the spatial buckets around a point whose squared
    /// horizontal distance is within a radius.
    ///
    /// @param centre Signed 16.16 centre.
    /// @param radius Radius, signed 16.16.
    /// @param visit Called once per unit found, in bucket order.
    /// @param context Passed to `visit`.
    void for_each_unit_in_radius(
        const sim::ground_orders::Point& centre,
        int32_t radius,
        void (*visit)(void* context, sim::unit_spawn::Slot& slot),
        void* context
    );
    /// Runs the viewpoint player's contact scan: its own and radar-sharing
    /// units are seen, its active radar and sonar units stamp contacts, other
    /// players' active jammers clear them, cloakers near an enemy their owner
    /// sees lose the cloak, and units in its line of sight are radar
    /// contacts. Nothing happens with a single participant.
    void scan_contacts();
    /// Rebuilds a player's knowledge once a period has passed since the
    /// last: its sightings and, for a computer player, its own-unit counts
    /// and base position; then it rolls one in 30 for its strategic refresh.
    ///
    /// Only a player with a knowledge record (built with its controller)
    /// takes part.
    ///
    /// @param player Player index 0..9.
    void refresh_player_knowledge(uint8_t player);

    /// Returns a player's sightings (seen and radar lists).
    ///
    /// @param player Player index 0..9; another throws.
    /// @return The player's sightings.
    const sim::detection::Sightings& sightings(uint8_t player) const {
        return sightings_.at(player);
    }

    /// Builds an order from its kind's descriptor and puts it at the head of
    /// the unit's primary queue, or of its secondary queue for a descriptor
    /// whose flags carry the secondary-queue bit (0x04); it inherits the old
    /// head's idle bit (0x40 of the command flags).
    ///
    /// @param unit Unit slot; an inactive unit throws.
    /// @param kind Mission kind; one outside the mission table throws.
    /// @param destination Signed 16.16 destination, or nothing.
    /// @param tolerance The order's first parameter.
    /// @param target Unit slot it aims at (the order's target), 0 for none;
    ///     linked as an observer only when the descriptor takes a target.
    /// @return The new order.
    sim::simulation_state::Order& insert_ground_order(
        uint16_t unit,
        uint8_t kind,
        std::optional<sim::ground_orders::Point> destination = std::nullopt,
        int32_t tolerance = 0,
        uint16_t target = 0
    );
    /// Issues the move command (2): Move_Ground or VTOL_Move, or QMove for a
    /// unit without a movement object that can still move.
    ///
    /// Without `queue` the unit's orders are cleared first; idle orders at
    /// the queue head are dropped, and the order follows the queue-tail mark.
    ///
    /// @param unit Unit slot; one without a movement object that cannot move
    ///     throws.
    /// @param destination Signed 16.16 destination.
    /// @param queue Whether the command was queued (shift held).
    /// @return The new order.
    sim::simulation_state::Order&
    issue_ground_move(uint16_t unit, const sim::ground_orders::Point& destination, bool queue);
    /// Tells whether issue_ground_move has an order for the unit: it has a
    /// movement object, or its type can move without one (a factory's
    /// QMove).
    ///
    /// @param unit Unit slot.
    /// @return False as well for an inactive unit.
    bool takes_move_order(uint16_t unit) const;
    /// Issues MobileBuild (VTOL_MobileBuild for an aircraft) of a type at a
    /// site.
    ///
    /// @param unit Builder slot.
    /// @param type Type index to build (the order's first parameter).
    /// @param destination Signed 16.16 site.
    /// @param queue Whether the command was queued (shift held).
    /// @return The new order.
    sim::simulation_state::Order& issue_mobile_build(
        uint16_t unit, uint16_t type, const sim::ground_orders::Point& destination, bool queue
    );
    /// Issues the patrol command (9): RepairPatrol or VTOL_RepairPatrol for a
    /// type with the repair ability, else Patrol or VTOL_Patrol; QPatrol for a
    /// unit without a movement object.
    ///
    /// @param unit Unit slot.
    /// @param destination Signed 16.16 waypoint.
    /// @param queue Whether the command was queued (shift held).
    /// @return The new order.
    sim::simulation_state::Order&
    issue_patrol(uint16_t unit, const sim::ground_orders::Point& destination, bool queue);
    /// Issues RepairPatrol (VTOL_RepairPatrol for an aircraft).
    ///
    /// @param unit Unit slot.
    /// @param destination Signed 16.16 waypoint.
    /// @param queue Whether the command was queued (shift held).
    /// @return The new order.
    sim::simulation_state::Order&
    issue_repair_patrol(uint16_t unit, const sim::ground_orders::Point& destination, bool queue);
    /// Issues Reclaim (VTOL_Reclaim for an aircraft) of the map feature under
    /// a point.
    ///
    /// @param unit Unit slot.
    /// @param destination Signed 16.16 point over the feature.
    /// @param queue Whether the command was queued (shift held).
    /// @return The new order.
    sim::simulation_state::Order&
    issue_feature_reclaim(uint16_t unit, const sim::ground_orders::Point& destination, bool queue);
    /// Puts BuildWeapon at the head of the unit's queue: stockpile rounds for
    /// a weapon slot.
    ///
    /// @param unit Unit slot.
    /// @param weapon_slot Weapon slot 0..2 (the order's first parameter).
    /// @param count Rounds to build (the order's second parameter).
    /// @return The new order.
    sim::simulation_state::Order&
    issue_build_weapon(uint16_t unit, int32_t weapon_slot, int32_t count);
    /// Issues BuildingBuild at the factory's position.
    ///
    /// @param factory Factory slot.
    /// @param type Type index to build (the order's first parameter).
    /// @param count Units to build (the order's second parameter), at least 1.
    /// @param queue Whether the command was queued (shift held).
    /// @return The new order.
    sim::simulation_state::Order&
    issue_building_build(uint16_t factory, uint16_t type, int32_t count, bool queue);
    /// Counts the queued builds of a type in both of the unit's order lists:
    /// the counts (second parameters) of the orders whose command flags mark
    /// them as builds (bit 0x01) and whose first parameter is the type.
    ///
    /// @param unit Unit slot; one outside the pool throws.
    /// @param type Type index.
    /// @return The total count.
    int32_t queued_build_count(uint16_t unit, int32_t type) const;
    /// Changes a factory's queued BuildingBuild count of a type
    /// (change_queued_count).
    ///
    /// @param factory Factory slot.
    /// @param type Type index.
    /// @param delta Units to add, or to take away when negative.
    void queue_factory_build(uint16_t factory, uint16_t type, int32_t delta);
    /// Adds to the count (second parameter) of the unit's queued orders of a
    /// kind for a type (first parameter).
    ///
    /// A positive change goes to the last order of the kind's queue when it
    /// matches, otherwise to a new queued order; a negative one comes off the
    /// latest matching order, and each order it empties is removed.
    ///
    /// @param unit Unit slot.
    /// @param kind Mission kind.
    /// @param type Type index.
    /// @param delta Count to add, or to take away when negative.
    void change_queued_count(uint16_t unit, uint8_t kind, uint16_t type, int32_t delta);
    /// Issues HelpBuild (VTOL_HelpBuild for an aircraft builder) on another
    /// unit's frame.
    ///
    /// @param unit Builder slot.
    /// @param target Frame slot.
    /// @param queue Whether the command was queued (shift held).
    /// @return The new order.
    sim::simulation_state::Order& issue_help_build(uint16_t unit, uint16_t target, bool queue);
    /// Issues RepairUnit (VTOL_RepairUnit for an aircraft builder).
    ///
    /// @param unit Builder slot.
    /// @param target Unit slot to repair.
    /// @param queue Whether the command was queued (shift held).
    /// @return The new order.
    sim::simulation_state::Order& issue_repair(uint16_t unit, uint16_t target, bool queue);
    /// Issues ReclaimUnit (VTOL_ReclaimUnit for an aircraft builder).
    ///
    /// @param unit Builder slot.
    /// @param target Unit slot to reclaim.
    /// @param queue Whether the command was queued (shift held).
    /// @return The new order.
    sim::simulation_state::Order& issue_reclaim(uint16_t unit, uint16_t target, bool queue);
    /// Issues Capture.
    ///
    /// @param unit Capturer slot.
    /// @param target Unit slot to capture.
    /// @param queue Whether the command was queued (shift held).
    /// @return The new order.
    sim::simulation_state::Order& issue_capture(uint16_t unit, uint16_t target, bool queue);
    /// Gives a captured unit to the capturer's owner (transfer_unit with the
    /// unit's own state).
    ///
    /// Nothing happens when either unit is gone or the capturer has no
    /// owner.
    ///
    /// @param original Captured unit.
    /// @param capturer Capturing unit.
    void capture_unit(sim::unit_spawn::Slot& original, sim::unit_spawn::Slot& capturer);
    /// Hands a live unit to another player, as a capture or a gift of units
    /// does.
    ///
    /// The unit's captured event runs first. A unit simulated here given to
    /// a player another machine simulates loses its selection, is kept from
    /// being selected for 150 ticks, goes to that machine through
    /// multiplayer.unit_transferred, then dies here as captured. A unit
    /// given to a player simulated here is created again for that player,
    /// finished, where it stands and in its occupancy, without the standing
    /// move and fire orders it is created with. The copy takes `carried`
    /// when given (its stockpiles only for slots whose weapon is enabled on
    /// the copy), and the old unit is left for its own machine to kill;
    /// otherwise it takes the old unit's build progress, health, orientation
    /// and, for slots whose weapon is enabled on the copy, stockpiles, and
    /// the old unit dies as captured (lethal damage of the captured kind
    /// with no attacker). The copy then takes the old unit's state flags. A
    /// unit simulated elsewhere given to a player simulated elsewhere is
    /// left alone after its captured event.
    ///
    /// Nothing happens when the new owner already owns the unit, is outside
    /// the ten players or is not present, or the unit is not live or is
    /// already dying.
    ///
    /// @param unit Unit slot; one outside the pool throws.
    /// @param new_owner Player index 0..9.
    /// @param carried The state another player's machine handed over with
    ///     the unit, or null to take the unit's own.
    void transfer_unit(uint16_t unit, uint8_t new_owner, const TransferredUnit* carried = nullptr);
    /// Issues Ground_Pickup (VTOL_Pickup for an aircraft).
    ///
    /// @param transport Transport slot.
    /// @param cargo Unit slot to load.
    /// @param queue Whether the command was queued (shift held).
    /// @return The new order.
    sim::simulation_state::Order& issue_load(uint16_t transport, uint16_t cargo, bool queue);
    /// Issues Ground_Unload (VTOL_Unload for an aircraft).
    ///
    /// @param transport Transport slot.
    /// @param destination Signed 16.16 drop point.
    /// @param queue Whether the command was queued (shift held).
    /// @return The new order.
    sim::simulation_state::Order&
    issue_unload(uint16_t transport, const sim::ground_orders::Point& destination, bool queue);
    /// Issues Follow_Ground (VTOL_Follow for an aircraft): guard a unit.
    ///
    /// @param unit Guard slot.
    /// @param target Unit slot to guard.
    /// @param queue Whether the command was queued (shift held).
    /// @return The new order.
    sim::simulation_state::Order& issue_guard(uint16_t unit, uint16_t target, bool queue);
    /// Clears every order of both of the unit's queues.
    ///
    /// @param unit Unit slot.
    void stop_orders(uint16_t unit);
    /// Issues the STOP command: a Stop order replaces the unit's orders as an
    /// order given without queueing does.
    ///
    /// @param unit Unit slot.
    /// @return The new order.
    sim::simulation_state::Order& issue_stop(uint16_t unit);

    struct StrategicEnvironment {
        int32_t maximum_wind{};
        float tidal_strength{}, normalized_wind{};
    };

    /// Sets the wind and tidal inputs of the strategic refresh.
    ///
    /// @param value Maximum wind, tidal strength and normalised wind.
    void configure_strategic_environment(StrategicEnvironment value) {
        strategic_environment_ = value;
    }

    /// Returns a player's strategic type table.
    ///
    /// @param player Player index 0..9; another throws.
    /// @return The player's strategic refresh state.
    const sim::combat_state::StrategicRefreshState& strategic_state(uint8_t player) const {
        return strategic_states_.at(player);
    }

    /// Sends a unit after another on its own account: the attack its missions
    /// (standby, patrol, guard) and a hit's reaction start, not a command.
    ///
    /// The attack order the command resolver picks goes to the head of the
    /// orders, ahead of the orders the unit already has; unforced, a unit on
    /// standing move order 1 also queues a move back to where it stands, and
    /// gives up the chase once it strays its manoeuvre leash from there. A
    /// player's or computer player's attack command is issue_attack_command.
    ///
    /// @param source Attacker slot.
    /// @param target Target slot.
    /// @param forced Whether the unit must attack whatever its standing orders;
    ///     unforced it is refused while the standing move or fire order is zero.
    /// @return False when the attack was refused or no attack resolves.
    bool issue_attack(uint16_t source, uint16_t target, bool forced);
    /// Gives a unit the attack command on another unit, as a player's order
    /// or a computer player's squad order gives it.
    ///
    /// The attack command resolves against the target to the unit's attack
    /// mission (Attack_Chase for a ground unit, Suppress for an allied
    /// target), issued as issue_order issues it: unqueued, the order replaces
    /// the unit's orders, so an order under way no longer steers the unit;
    /// queued, it follows the queue-tail mark. The order keeps the target
    /// only when its mission takes one. It carries no leash and no move back,
    /// and a unit holding its fire or its position takes it.
    ///
    /// @param source Attacker slot.
    /// @param target Target slot.
    /// @param queue Whether the command was queued (shift held).
    /// @param point Signed 16.16 map point the command was given at (the
    ///     ground under the pointer), or null for none, as a computer
    ///     player's squad order gives it.
    /// @return False when the source is the target or no attack resolves
    ///     against it.
    bool issue_attack_command(
        uint16_t source, uint16_t target, bool queue, const sim::ground_orders::Point* point
    );
    /// Flips state flags as the HUD On/Off button does: sets them when none of
    /// the mask is set, else clears them.
    ///
    /// @param unit Unit slot.
    /// @param mask State flag bits.
    void toggle_activation(uint16_t unit, uint8_t mask);
    /// Puts the HUD cloak toggle's Cloak_On or Cloak_Off order at the head of
    /// the primary queue; upkeep then switches the cloak.
    ///
    /// @param unit Unit slot.
    /// @param on True for Cloak_On.
    /// @return The new order.
    sim::simulation_state::Order& issue_cloak(uint16_t unit, bool on);
    /// Puts one of the order panel's state orders (Activate, Deactivate,
    /// Cloak_On, Cloak_Off, Standing_MoveOrder, Standing_FireOrder) at the
    /// head of the primary queue, dropping idle orders there first.
    ///
    /// @param unit Unit slot; an inactive unit throws.
    /// @param kind Mission kind.
    /// @param value The order's first parameter, the new standing order.
    /// @return The new order.
    sim::simulation_state::Order& issue_state_order(uint16_t unit, uint8_t kind, int32_t value);
    /// Handles Ctrl+D and the SELFD button: cancels the SelfDestruct orders of
    /// the units that have one or, when none has, gives each unit one.
    ///
    /// @param units Selected unit slots.
    void toggle_self_destruct(std::span<const uint16_t> units);
    /// Returns the count a unit's SelfDestruct order last spoke.
    ///
    /// @param unit Unit slot.
    /// @return The count, the type's full countdown before counting starts,
    ///     or 0 when the unit has no SelfDestruct order or it is detonating.
    uint16_t self_destruct_remaining(uint16_t unit) const;
    // No player behind a site test: the simulation's own placement checks.
    static constexpr uint8_t no_placing_player = 0xff;
    /// Tests a building's yard map (type bm_code 0) against the plots under
    /// a site.
    ///
    /// Yard cells with bit 3 bound the slope (highest less lowest plot
    /// extreme) and the water depths and set the height to their lowest
    /// plot; without any the building floats at sea level less its
    /// waterline, wrapped to a byte. Submerged cells must not rise above that
    /// height. A placing player (the build ghost) must have the footprint
    /// centre, at the corner cell's terrain height, mapped, and units and
    /// claimed plots only refuse the site where that player has line of
    /// sight. Metal under the yard plays no part: a metal extractor is
    /// accepted on bare ground as on a deposit.
    ///
    /// @param type Type index; 0 or one past the table is refused.
    /// @param cell_x Site's top-left cell x.
    /// @param cell_z Site's top-left cell z.
    /// @param skip_unit Unit slot whose own occupancy does not refuse the
    ///     site, 0 for none.
    /// @param placing_player Player placing the building, or
    ///     no_placing_player for the simulation's own checks.
    /// @return The height the building would stand at, or nothing when the
    ///     site is refused.
    std::optional<uint8_t> building_site(
        uint16_t type,
        int32_t cell_x,
        int32_t cell_z,
        uint16_t skip_unit,
        uint8_t placing_player = no_placing_player
    ) const;

    /// Tests a building site (building_site) without the placing player.
    ///
    /// @param type Type index.
    /// @param cell_x Site's top-left cell x.
    /// @param cell_z Site's top-left cell z.
    /// @param skip_unit Unit slot whose own occupancy does not refuse the
    ///     site, 0 for none.
    /// @return True when the site is accepted.
    bool
    building_site_clear(uint16_t type, int32_t cell_x, int32_t cell_z, uint16_t skip_unit) const {
        return building_site(type, cell_x, cell_z, skip_unit).has_value();
    }

    /// Returns the height a building's yard would stand at on a site, tested
    /// or not.
    ///
    /// @param type Type index; 0 or one past the table gives 0.
    /// @param cell_x Site's top-left cell x.
    /// @param cell_z Site's top-left cell z.
    /// @return Height in whole units.
    uint8_t footprint_height(uint16_t type, int32_t cell_x, int32_t cell_z) const;
    /// Tests whether a type fits a site: a building goes through
    /// building_site; a mobile unit on occupancy layer 1 walks its footprint
    /// for blocking features, ground units, water depths and slopes.
    ///
    /// @param type Type index.
    /// @param cell_x Footprint's top-left cell x.
    /// @param cell_z Footprint's top-left cell z.
    /// @param skip_unit Unit slot whose own occupancy does not block, 0 for
    ///     none.
    /// @param occupancy_kind Occupancy layer of a mobile unit: 1 walks the
    ///     footprint, any other passes.
    /// @return True when the site is clear; for type 0, a type past the table
    ///     or a footprint past the map edge, true only for occupancy kind 2.
    bool site_clear_for(
        uint16_t type, int32_t cell_x, int32_t cell_z, uint16_t skip_unit, uint8_t occupancy_kind
    ) const;
    /// Tells whether a unit's weapon slot can reach another unit: sea and
    /// air gates, ballistic feasibility, then the squared horizontal range.
    ///
    /// @param source Attacker slot.
    /// @param target Target slot.
    /// @param weapon Weapon slot 0..2; one without a definition throws.
    /// @return True when in reach.
    bool weapon_can_reach(uint16_t source, uint16_t target, uint8_t weapon);
    /// Returns a unit's own pick for its orders: the implicit slot-0 search,
    /// and only while it fires at will.
    ///
    /// @param unit Unit searching.
    /// @return The target, or null when none or not firing at will.
    sim::simulation_state::Unit* find_automatic_target(sim::simulation_state::Unit& unit);
    /// Issues the attack command (3) on a ground point: Suppress, not
    /// Attack_NoMove.
    ///
    /// @param unit Unit slot.
    /// @param destination Signed 16.16 point.
    /// @param queue Whether the command was queued (shift held).
    /// @return The new order.
    sim::simulation_state::Order&
    issue_attack_ground(uint16_t unit, const sim::ground_orders::Point& destination, bool queue);
    /// Issues the special attack command (4): AttackSpecial with the third
    /// weapon slot, which turns into Attack_Chase or Suppress when it runs.
    ///
    /// @param unit Unit slot.
    /// @param destination Signed 16.16 point.
    /// @param queue Whether the command was queued (shift held).
    /// @param target Target slot, 0 for none.
    /// @return The new order.
    sim::simulation_state::Order& issue_attack_special(
        uint16_t unit, const sim::ground_orders::Point& destination, bool queue, uint16_t target = 0
    );
    /// Reclassifies a rectangle in every class's movement map.
    ///
    /// Plot edits made outside the match's own feature and occupancy calls
    /// must pass it on.
    ///
    /// @param cell Top-left cell (x, z).
    /// @param footprint Size in cells (x, z).
    void refresh_movement_maps(std::array<int16_t, 2> cell, std::array<int16_t, 2> footprint);
    /// Applies one health event to a live unit that is not dying: healing,
    /// or the damage countdown, the unit's reaction, the last-attacker
    /// record, then paralysis or the health loss with the HitByWeapon and
    /// TakeDamage scripts.
    ///
    /// A unit simulated here that drops below 1 health is marked dying; one
    /// simulated elsewhere stops at 0.
    ///
    /// @param target Unit hit.
    /// @param source Attacker, or null for none.
    /// @param amount Damage in health points (or healing for kind 10).
    /// @param kind 1 weapon, 2 paralyze, 10 healing, 11 not shared, or a
    ///     DeathKind.
    /// @param direction Hit direction in 256ths of a turn.
    void apply_damage_event(
        sim::unit_spawn::Slot& target,
        sim::unit_spawn::Slot* source,
        int16_t amount,
        uint8_t kind,
        uint8_t direction
    );
    /// Records one death: kill and loss counters, commander tallies, the
    /// killer's veteran level, the kills board and the reclaim metal credit.
    ///
    /// @param slot Dying unit.
    /// @param death How it died.
    /// @param killer Unit that killed it, or null.
    void record_death_statistics(
        sim::unit_spawn::Slot& slot, DeathKind death, sim::unit_spawn::Slot* killer
    );
    /// Tears a dead unit down under the death kind its record holds, with no
    /// explosion and no wreck.
    ///
    /// @param slot Dying unit.
    void teardown_dead_unit(sim::unit_spawn::Slot& slot);
    /// Runs the commander rule's sweep for one player
    /// (TickHost::destroy_player_units).
    ///
    /// @param owner Player index 0..9.
    void destroy_player_units(uint8_t owner);

    // Combat observer for the music mood: a non-healing hit with an attacker
    // and a kill-statistics death, by owner and killer player index.
    struct CombatActivityHook {
        void* context{};
        /// Reports a non-healing hit with an attacker.
        ///
        /// @param context The hook's context.
        /// @param attacker_player Attacker's owner.
        /// @param target_player Target's owner.
        void (*hit)(void* context, uint8_t attacker_player, uint8_t target_player){};
        /// Reports a death that counts in the kill statistics.
        ///
        /// @param context The hook's context.
        /// @param killer_player The killer's owner (10 for none).
        void (*kill)(void* context, uint8_t killer_player){};
    };

    CombatActivityHook combat_activity{};

    // Kills board notices from the kill handler: the killer took the top row
    // (announced in chat with its board score), and the highlight of the
    // killer's kills and the victim's losses while the board is pinned
    // open.
    struct KillBoardHook {
        void* context{};
        /// Announces that a player took the top row of the kills board.
        ///
        /// @param context The hook's context.
        /// @param player The killer's owner.
        /// @param score Its board score.
        void (*took_lead)(void* context, uint8_t player, int16_t score){};
        /// Highlights the killer's kills and the victim's losses on the
        /// pinned board.
        ///
        /// @param context The hook's context.
        /// @param killer The killer's owner.
        /// @param victim The victim's owner.
        void (*flash)(void* context, uint8_t killer, uint8_t victim){};
    };

    KillBoardHook kill_board{};

    // A player's unit count reached zero in the kill handler: the host posts
    // a multiplayer game's departure notice or a skirmish's elimination
    // notice for the player.
    struct LastUnitHook {
        void* context{};
        /// Reports that a player's unit count reached zero.
        ///
        /// @param context The hook's context.
        /// @param player Player index 0..9.
        void (*lost)(void* context, uint8_t player){};
    };

    LastUnitHook last_unit{};

    // Viewpoint effects of the deathmatch commander respawn once the new
    // commander holds its resources and the sight grids are rebuilt: the
    // viewer's remembered terrain may be gone, and the view centres on and
    // selects the side's commander.
    struct RespawnHook {
        void* context{};
        /// Reports that the sight grids were rebuilt.
        ///
        /// @param context The hook's context.
        void (*sight_rebuilt)(void* context){};
        /// Centres the view on and selects the side's commander.
        ///
        /// @param context The hook's context.
        void (*select_commander)(void* context){};
    };

    RespawnHook respawn{};

    // What the view says once the local player's defeat made it a watcher:
    // asks whether to go on watching, or, while computer players this
    // machine hosts still play and a human is left, tells it why it must
    // watch.
    enum class WatchNotice : uint8_t { continue_prompt, hosting_computers, none };

    struct WatchHook {
        void* context{};
        /// Reports that the local player now watches.
        ///
        /// @param context The hook's context.
        /// @param notice What the view should say.
        void (*became_watcher)(void* context, WatchNotice notice){};
    };

    WatchHook watch{};
    /// Applies the player's answer to the continue-watching prompt: the
    /// pending win is dropped, and either the other players are told the
    /// setup blocks again or the game ends in defeat.
    ///
    /// @param keep True to go on watching.
    void choose_continue_watching(bool keep);

    // The observer pulse (every 90 ticks while BigBrother runs): shift held
    // pauses it, and the order panel rebuilds when it drops the selection.
    // Without shift_down the key counts as held.
    struct ObserverHook {
        void* context{};
        /// Tells whether shift is held.
        ///
        /// @param context The hook's context.
        /// @return True while shift is held.
        bool (*shift_down)(void* context){};
        /// Rebuilds the order panel after the pulse dropped the selection.
        ///
        /// @param context The hook's context.
        void (*selection_cleared)(void* context){};
    };

    ObserverHook observer{};

    /// Read-only reports of units created, finished, damaged and dying and
    /// of shots placed and detonating, for units simulated here and those a
    /// recording or another machine settles alike; each null entry reports
    /// nothing, and setting them changes nothing the match computes.
    EventHooks event_hooks{};

    // The order panel closed back to its root page, even while a modal state
    // holds it, as a commander's death under the commander rule does before
    // its player's units self-destruct.
    struct OrderPanelHook {
        void* context{};
        /// Closes the order panel back to its root page.
        ///
        /// @param context The hook's context.
        void (*close)(void* context){};
    };

    OrderPanelHook order_panel{};

    // How a clip play_sound_at lets through is heard. With 3D sound on it is
    // placed relative to the middle of the view, x to the right and z up the
    // screen in pixels, within a distance range; otherwise it is unplaced, at
    // the near volume inside the view and the far volume outside it.
    struct PointSound {
        FixedVec3 at{};
        int32_t volume{}; // hundredths of a decibel
        bool placed{};
        int32_t x{};
        int32_t y{};
        int32_t z{};
        float min_distance{};
        float max_distance{};
    };

    static constexpr int32_t point_sound_volume_near = -585;
    static constexpr int32_t point_sound_volume_far = -1585;

    // Playback of a named clip that play_sound_at let through; the volume,
    // sound-option and device gates are the hook's. spatial reads the sound
    // system's 3D switch.
    struct PointSoundHook {
        void* context{};
        /// Tells whether 3D sound is on.
        ///
        /// @param context The hook's context.
        /// @return True when clips are placed.
        bool (*spatial)(void* context){};
        /// Plays a clip.
        ///
        /// @param context The hook's context.
        /// @param name Clip name.
        /// @param sound Volume and placement.
        void (*play)(void* context, const char* name, const PointSound& sound){};
    };

    PointSoundHook point_sound{};

    /// Plays a named clip at a point on the map the viewpoint player sees:
    /// a weapon's soundstart at its muzzle, burst shot or meteor start, a
    /// detonation's soundhit or soundwater, or the file of a sound-table
    /// entry play_named_sound_at looked up.
    ///
    /// Nothing plays for an empty name, without a point_sound.play hook, off
    /// the map or out of the viewpoint player's sight.
    ///
    /// @param name Clip name.
    /// @param at Signed 16.16 map point.
    void play_sound_at(const char* name, const FixedVec3& at);

    /// Plays the sound-table entry registered under a name at a point
    /// through play_sound_at; a name the table lacks plays nothing.
    ///
    /// A feature catching fire plays "treeburn".
    ///
    /// @param name Sound-table entry name.
    /// @param at Signed 16.16 map point.
    void play_named_sound_at(const char* name, const FixedVec3& at);

    // The session's meteor storm, stepped every tick after the wind is
    // rescheduled; the storm state lives with the session.
    struct MeteorHook {
        void* context{};
        /// Steps the meteor storm one tick.
        ///
        /// @param context The hook's context.
        void (*step)(void* context){};
    };

    MeteorHook meteor{};

    // Frame-time profile marks where the game loop samples the clock: the
    // time since the last mark goes to the OA_PROFILE_* category that just
    // ended.
    struct ProfileHook {
        void* context{};
        /// Charges the time since the last mark to a category.
        ///
        /// @param context The hook's context.
        /// @param category OA_PROFILE_* category that just ended.
        void (*mark)(void* context, int32_t category){};
    };

    ProfileHook profile{};

    /// Marks the end of a profile category through the profile hook.
    ///
    /// @param category OA_PROFILE_* category that just ended.
    void mark_profile(int32_t category) const {
        if (profile.mark != nullptr)
            profile.mark(profile.context, category);
    }

    /// Launches a meteor falling from a point at a velocity; the weapon's
    /// soundstart plays there.
    ///
    /// @param weapon Weapon reference of the meteor.
    /// @param position Signed 16.16 start.
    /// @param velocity Signed 16.16 velocity per tick.
    /// @param share Whether to share it through multiplayer.shot_fired.
    /// @return False when the projectile pool is full.
    bool launch_meteor(
        oa_ref32 weapon, const FixedVec3& position, const FixedVec3& velocity, bool share
    );

    // Named sounds of the sound table played unplaced: "Victory Condition"
    // when a campaign victory condition is first met. file gives the
    // configured file of the entry registered under a name, or null when
    // none is.
    struct NamedSoundHook {
        void* context{};
        /// Plays a sound-table entry unplaced.
        ///
        /// @param context The hook's context.
        /// @param name Entry name.
        void (*play)(void* context, const char* name){};
        /// Returns the configured file of a sound-table entry.
        ///
        /// @param context The hook's context.
        /// @param name Entry name.
        /// @return The file name, or null when no entry has the name.
        const char* (*file)(void* context, const char* name){};
    };

    NamedSoundHook named_sound{};

    /// Leaves the game without victory or defeat (sim::scenario::disable), as the
    /// console Kill command and an empty mission schema do.
    void disable_scenario() noexcept;

    /// Returns the feature runtime's host over this match's canonical plots:
    /// the shared and LCG streams, the frames of the feature sequences, the
    /// map listeners (once the match plots carry the changed footprint), the
    /// smoke of vents and fires, play_sound_at, the burn weapon's blast and
    /// the reclaim credit; weapon hits on features and feature changes go to
    /// the multiplayer hooks' feature entries.
    [[nodiscard]] sim::feature_runtime::FeatureHost feature_host() noexcept;
    /// Finishes a unit's reclaim of the feature under a point: the unit is
    /// credited and the feature plays its reclamate sequence.
    ///
    /// @param unit Reclaiming unit.
    /// @param point Signed 16.16 map point.
    /// @return False when there is nothing to reclaim there.
    bool reclaim_feature(oa::Unit& unit, const FixedVec3& point);
    /// Places a feature the savegame's Features section restores, for no
    /// player, across its footprint (sim::feature_runtime::place_feature).
    ///
    /// An object feature takes its saved position and orientation; without
    /// them it stands at its footprint centre, unturned. A type the table does
    /// not hold, or a footprint that does not fit, places nothing.
    ///
    /// @param plot the origin plot within state().plots, as a
    ///     data::persist::SaveContext over those plots hands it
    /// @param def_index FeatureDef index
    /// @param position signed 16.16 X, Y and Z, each 32-bit little-endian;
    ///     null for none
    /// @param orientation three 16-bit little-endian angles; null for none
    /// @quirk A match resuming a save hides the plots under the map's edges
    ///     before the save's features are placed, so a saved feature whose
    ///     footprint reaches one of them is not restored.
    void place_saved_feature(
        uint8_t* plot, uint16_t def_index, const uint8_t* position, const uint8_t* orientation
    );
    /// Sets a restored feature burning as a fire started here, which spreads
    /// (sim::feature_runtime::ignite_feature).
    ///
    /// @param cell_x feature origin column
    /// @param cell_z feature origin row
    void ignite_saved_feature(int32_t cell_x, int32_t cell_z);
    /// Starts a restored feature's die or reclamate sequence
    /// (sim::feature_runtime::start_feature_sequence).
    ///
    /// @param cell_x feature cell column
    /// @param cell_z feature cell row
    /// @param kind 0 for the die sequence, 1 for the reclamate sequence
    void restart_saved_feature_sequence(int32_t cell_x, int32_t cell_z, int32_t kind);
    /// Replaces the match's FeatureDef table with a copy of `defs`.
    ///
    /// The savegame's Features section loads the types it names that the table
    /// lacks, and the link pass their remnants; each lands after the entries
    /// already there, so every placed feature keeps its index.
    ///
    /// @param defs the extended table, starting with the current entries
    void adopt_feature_defs(std::span<const oa::FeatureDef> defs);

    /// Rebuilds the sight grids from the visibility rules and the live units.
    ///
    /// Each active player's coverage is cleared under line of sight and seen
    /// once everywhere without it; then every live unit stamps its sight
    /// again, the fog edge mask goes stale and the mapped radar is marked
    /// for rebuilding. The mapped image and the blips are redrawn when the
    /// radar is next drawn.
    ///
    /// @param refill_mapped Also refills every mapped word: cleared under
    ///     the mapping rule, every player's bit without it.
    void reset_sight_buffers(bool refill_mapped);

    /// Returns the feature word covering a point, following a continuation
    /// cell to its origin.
    ///
    /// @param point Signed 16.16 map point.
    /// @param[out] origin_x The found feature's origin cell x, when given.
    /// @param[out] origin_z The found feature's origin cell z, when given.
    /// @return The FeatureDef index, or sim::spatial_state::no_feature.
    [[nodiscard]] uint16_t feature_word_under(
        const sim::ground_orders::Point& point,
        int16_t* origin_x = nullptr,
        int16_t* origin_z = nullptr
    ) const;

    /// Tells whether a unit can stay selected
    /// (sim::simulation_state::unit_selectable).
    ///
    /// @param unit Unit slot.
    /// @return True when it may stay selected.
    bool selectable(uint16_t unit) const;
    /// Drops a selected unit from the selection when it may no longer stay
    /// selected, clearing the order panel's unit and flagging it for a
    /// redraw.
    ///
    /// @param unit Unit slot.
    void finalize_attachment(uint16_t unit);
    /// Carries a unit on another at a piece, or detaches it.
    ///
    /// Nothing happens unless the child is live, not a building and carries
    /// nothing itself, and a parent is live and not carried itself. When the
    /// link passes these checks, multiplayer.carry_link_changed shares it
    /// before it applies. A detached unit rejoins its bucket chain; a
    /// carried one leaves it. A child simulated here that is not on an air
    /// base gets BeCarried.
    ///
    /// @param child Unit slot to carry.
    /// @param parent Carrier slot, 0 to detach.
    /// @param piece COB piece of the carrier, -1 for none.
    /// @param mode Movement layer the child's movement object takes.
    void set_carry_link(uint16_t child, uint16_t parent, int8_t piece, uint8_t mode);
    /// Carries a unit on another at a piece, or detaches it, as a link
    /// another player's machine shared: set_carry_link's checks and effects
    /// without multiplayer.carry_link_changed.
    ///
    /// @param child Unit slot to carry.
    /// @param parent Carrier slot, 0 to detach.
    /// @param piece COB piece of the carrier, -1 for none.
    /// @param mode Movement layer the child's movement object takes.
    void apply_carry_link(uint16_t child, uint16_t parent, int8_t piece, uint8_t mode);
    /// Runs COB ATTACH-UNIT for a carrier: carries a live unit that is free
    /// or already its own at a piece in a movement layer.
    ///
    /// @param carrier Carrier slot running the script.
    /// @param target Unit id to carry; 0 or an empty slot is ignored.
    /// @param piece COB piece of the carrier.
    /// @param mode Movement layer the carried unit takes.
    void script_attach_unit(uint16_t carrier, int32_t target, int32_t piece, int32_t mode);
    /// Runs COB DROP-UNIT for a carrier: sets a live unit it carries down
    /// where it hangs when its footprint fits there.
    ///
    /// @param carrier Carrier slot running the script.
    /// @param target Unit id to drop; 0 or an empty slot is ignored.
    void script_drop_unit(uint16_t carrier, int32_t target);

    struct SelectionState {
        uint16_t panel_unit_id{};
        uint8_t frame_flags{};
    };

    /// Returns the order panel's unit and its redraw flags.
    SelectionState& selection() noexcept { return selection_; }

    struct WindState {
        bool changed{};
        uint16_t direction{};
        uint32_t speed{};
    };

    /// Returns the current wind as wind generators read it.
    WindState& wind() noexcept { return wind_; }

    /// Returns the spatial state: plots, buckets and unit projections.
    sim::spatial_state::World& spatial() noexcept { return spatial_; }

    /// Returns the spatial state: plots, buckets and unit projections.
    const sim::spatial_state::World& spatial() const noexcept { return spatial_; }

    /// Returns the integer terrain height at a map position.
    ///
    /// @param x Signed 16.16 map x as a bit pattern.
    /// @param z Signed 16.16 map z as a bit pattern.
    /// @return Terrain height in whole units.
    int32_t map_height(uint32_t x, uint32_t z) { return sample_terrain_height(x, z); }

    /// Refreshes and returns a unit's spatial projection.
    ///
    /// @param slot Unit to project.
    /// @return The unit's projection.
    sim::spatial_state::Unit& project_spatial_slot(sim::unit_spawn::Slot& slot) {
        return project_spatial(slot);
    }

    /// Runs a player's controller tick: a computer player's controller runs
    /// its order-callback tick, then the weapon sweep runs with the computer
    /// flag.
    ///
    /// @param player Player index 0..9.
    void run_player_controller(uint8_t player);

    /// Returns the computer players' state (src/sim/ai owns the state and the
    /// decisions).
    std::shared_ptr<void>& computer_player_state() noexcept { return computer_player_state_; }

    /// Returns a type's parsed FBI definition.
    ///
    /// @param type Type index.
    /// @return The definition, or null past the table or without one.
    const data::unit_definitions::UnitDefinition* unit_definition(uint16_t type) const noexcept;
    /// Returns the unit slots of a player's squad.
    ///
    /// @param player Player index 0..9.
    /// @param squad Squad number.
    /// @return The members, empty for an unknown player or squad.
    std::span<const uint16_t> squad_members(uint8_t player, uint32_t squad) const;
    /// Moves a unit into a squad of its owner (assign_squad).
    ///
    /// @param unit Unit slot.
    /// @param squad Squad number; 0xffffffff joins none.
    void set_unit_squad(uint16_t unit, uint32_t squad);
    /// Settles a restored unit's footprint for its yard state, waking the
    /// units overlapping it (sim::spatial_state::refresh_footprint_occupancy).
    ///
    /// @param unit Unit slot; a rejected refresh throws.
    void refresh_restored_footprint(uint16_t unit);
    /// Tells whether a player counts another as an ally.
    ///
    /// @param player Player whose alliance row is read.
    /// @param other Player index 0..9.
    /// @return The row's entry; without a configured row, whether the two
    ///     are the same player.
    bool allied(uint8_t player, uint8_t other) const noexcept;
    /// Returns the command flags of the head of a unit's primary queue.
    ///
    /// @param unit Unit slot.
    /// @return The flags, or 0 when the queue is empty.
    uint8_t primary_order_command_flags(uint16_t unit) const;
    /// Recomputes a player's strategic type table outside the periodic
    /// refresh.
    ///
    /// @param player Player index 0..9.
    void refresh_strategic_state(uint8_t player);
    /// Re-aims the weapon slots of the next few units of a player that fire
    /// at will and whose unit target is gone, allied, a bad target or
    /// already stunned by this paralyzer; a dead target stays for the weapon
    /// tick to drop.
    ///
    /// The sweep visits units_per_player / 30 + 1 units from where it last
    /// stopped. Dropped weapons are skipped.
    ///
    /// @param player Player index 0..9.
    /// @param computer Whether command-fire weapons take part (computer
    ///     players only).
    void sweep_weapon_targets(uint8_t player, bool computer);
    /// Reschedules the wind and stores it in the game record and wind().
    ///
    /// tick() runs it after the players' tick, so the economy reads the
    /// Game.wind_factor of the tick before.
    void refresh_wind();
    /// Runs one player's economy tick: collects every live unit's production
    /// and requests, settles the stores against storage and the waste
    /// totals, and scales each economy block for the next tick.
    ///
    /// Cloak upkeep is paid whole from the energy store; an unpaid tick drops
    /// the cloak. Mirrored players skip the upkeep.
    ///
    /// @param player Player index 0..9.
    void update_player_economy(std::size_t player);
    /// Issues a command order with an explicit descriptor: without `queue`
    /// the unit's orders are cleared first; idle orders at the queue head
    /// are dropped, and the order follows the queue-tail mark.
    ///
    /// @param unit Unit slot; an inactive unit throws.
    /// @param kind Mission kind.
    /// @param destination Signed 16.16 destination.
    /// @param queue Whether the command was queued (shift held).
    /// @param flags Descriptor word: the preserve, command and order flag
    ///     bytes, low byte first.
    /// @return The new order.
    sim::simulation_state::Order& issue_queued_command(
        uint16_t unit,
        uint8_t kind,
        const sim::ground_orders::Point& destination,
        bool queue,
        uint32_t flags
    );
    /// Refreshes every unit's spatial projection (flags, links, owner state)
    /// before spatial mutations.
    void prepare_spatial_state();
    /// Writes the spatial projections' flags and bucket links back to the
    /// units after spatial mutations.
    void synchronize_spatial_state();

    /// Returns the viewpoint player's sight grid.
    const sim::visibility_state::PlayerSightGrid& sight() const noexcept { return sight_; }

    /// Returns the sight grid for the savegame restore, which writes the
    /// mapped-terrain words back.
    sim::visibility_state::PlayerSightGrid& sight_mutable() noexcept { return sight_; }

    /// Draws from the LCG stream, the linear congruential sequence
    /// x = x * 214013 + 2531011 drawing bits 16..30, that the wind and the
    /// effect scatter draw from too.
    ///
    /// @return A value in 0..0x7fff.
    int32_t lcg_rand() noexcept {
        lcg_seed_ = lcg_seed_ * 214013u + 2531011u;
        return static_cast<int32_t>((lcg_seed_ >> 16) & 0x7fffu);
    }

    /// Returns a player's sight coverage grid.
    ///
    /// @param owner Player index 0..9; another throws.
    /// @return One byte per sight cell: how many units see it.
    std::span<const uint8_t> player_coverage(uint8_t owner) const;
    /// Tells whether a player sees a unit: its own always, a cloaked one
    /// never, one whose top is under the sea only when flagged shown under
    /// water; otherwise when one of four corners of its bounds is in sight.
    ///
    /// @param player Player index; one outside the table throws.
    /// @param unit Unit slot; a type without bounds throws.
    /// @return True when seen.
    bool unit_visible(uint8_t player, uint16_t unit) const;
    /// Tells whether a player has line of sight to a point, or under the
    /// mapping rules alone whether the viewer mapped it.
    ///
    /// @param player Player index 0..9.
    /// @param position Signed 16.16 point as bit patterns; the sight cell is
    ///     taken at z raised by half the height.
    /// @return True when seen.
    /// @quirk Under the mapping rules alone the viewpoint player's mapped bit
    ///     is read, whatever player asks.
    bool point_visible(uint8_t player, const std::array<uint32_t, 3>& position) const;
    /// Tells whether a player sees a map cell at a plot height, or the cell a
    /// footprint further on; the cells are whole 16-unit steps.
    ///
    /// @param player Player index 0..9.
    /// @param cell_x Cell x.
    /// @param cell_z Cell z.
    /// @param footprint_x Footprint width in cells.
    /// @param footprint_z Footprint depth in cells.
    /// @param height Plot height in whole units.
    /// @return True when either corner is seen.
    bool cell_or_footprint_corner_visible(
        uint8_t player,
        int16_t cell_x,
        int16_t cell_z,
        int16_t footprint_x,
        int16_t footprint_z,
        int16_t height
    ) const;
    /// Tells whether the viewpoint player has mapped the terrain under a
    /// point.
    ///
    /// @param position Signed 16.16 point as bit patterns.
    /// @return True when the viewpoint player's mapped bit is set there.
    bool point_mapped(const std::array<uint32_t, 3>& position) const;

    /// Returns the live records of the canonical projectile pool.
    std::span<const oa::Projectile> projectiles() const noexcept {
        const auto count = state().game.projectile_count;
        return {state().projectiles, count > 0 ? static_cast<std::size_t>(count) : 0u};
    }

    /// Returns the registry entry (art, sounds, [DAMAGE] table) of a
    /// projectile's weapon.
    ///
    /// @param shot Projectile record.
    /// @return The entry, or null for a weapon reference outside the table.
    const sim::combat_state::WeaponDefinition*
    projectile_weapon(const oa::Projectile& shot) const noexcept {
        if (shot.def == 0 || shot.def > OA_WEAPON_DEF_COUNT)
            return nullptr;
        return &input_.weapons.definition(static_cast<uint8_t>(shot.def - 1u));
    }

    struct NanoLaser {
        std::array<uint32_t, 3> from{};
        // The target box as origin + extent after the 4/11..7/11 shrink; the
        // renderer samples 5 particles, each at an LCG draw * extent / 0x8000.
        std::array<int32_t, 3> to_origin{};
        std::array<int32_t, 3> to_extent{};
    };

    /// Lists the nano beams to draw: from each builder, repairer or helper
    /// nozzle to the shrunk box of its target.
    ///
    /// @return One beam per order with a construction target, and one per
    ///     unit in its build stance for every unfinished unit of its owner.
    std::vector<NanoLaser> nano_lasers() const;

    // Which box a nano spray uses and which way it flows. Build sites spray
    // into the target's full bounds; repair, reclaim and capture sites floor
    // the box at the target's position, and reclaim and capture draw from it.
    enum class NanoSpray : uint8_t { build, repair, reclaim };

    /// Sprays nano particles on layer 6 between the worker's nano piece and
    /// the target's bounds, as the build, repair, reclaim and capture order
    /// ticks spawn them.
    ///
    /// @param worker Unit spraying; without a script its position is the
    ///     nozzle.
    /// @param target Unit sprayed.
    /// @param kind Which box and which way the particles flow.
    void
    spray_nano(sim::unit_spawn::Slot& worker, sim::simulation_state::Unit& target, NanoSpray kind);

    /// Sprays nano particles over a feature: the box over its footprint from
    /// the ground at its origin cell up by its height.
    ///
    /// @param worker Unit spraying; an empty slot sprays nothing.
    /// @param cell_x Feature's origin cell x.
    /// @param cell_z Feature's origin cell z.
    /// @param feature The feature's definition.
    /// @param inward True draws from the box (reclaiming), false sprays into
    ///     it (resurrecting).
    void spray_nano_feature(
        sim::unit_spawn::Slot& worker,
        int16_t cell_x,
        int16_t cell_z,
        const FeatureDef& feature,
        bool inward
    );

    /// Returns the effect world: emitters, explosion records, debris and
    /// fragments. Mutators draw from the match's LCG and synced streams
    /// through effect_host(); the renderer walks the const view.
    [[nodiscard]] const sim::effect_particles::EffectWorld& effects() const noexcept {
        return *effects_;
    }

    /// Returns the effect world for mutation.
    [[nodiscard]] sim::effect_particles::EffectWorld& effects() noexcept { return *effects_; }

    /// Returns the effect host: the LCG and synced streams and the grid and
    /// ground height samplers.
    [[nodiscard]] sim::effect_particles::EffectHost effect_host() noexcept;
    /// Returns the live particles of every particle list.
    [[nodiscard]] std::size_t particle_count() const noexcept;
    /// Loads the primitives of a 3DO object and its selection index
    /// (OfflineInputs::loaded_primitives).
    ///
    /// @param model Loaded model.
    /// @param object Object index in the model.
    /// @param[out] out The object's primitives as the model loader leaves them.
    /// @param[out] selection_primitive 0 when the sort moved a selection
    ///     primitive to the front, otherwise -1.
    /// @return False when the match was given no loader.
    bool loaded_primitives(
        const formats::objects3d::Model& model,
        uint32_t object,
        std::vector<sim::effect_particles::PiecePrimitive>& out,
        int32_t& selection_primitive
    ) const;

    /// Detonates a unit's explodeas weapon, or its selfdestructas weapon,
    /// where it stands through a stand-in shot with no source and no struck
    /// unit, so a blast of a player simulated here damages the units around
    /// it.
    ///
    /// @param unit Exploding unit; a type without the weapon does nothing.
    /// @param self_destruct Uses selfdestructas instead of explodeas.
    void explode_unit(sim::unit_spawn::Slot& unit, bool self_destruct);

    /// Tears a dead unit down: the viewer remembers where its own unit died;
    /// cargo dies with it and is set down; statistics, squad and scenario
    /// events; its footprint, bucket and sight stamp go; its target observers
    /// lose it; then a finished unit explodes as the record asks and leaves
    /// its wreck; finally its instance, movement object and air goals are
    /// released and its owner's unit count drops.
    ///
    /// A unit already not live is left alone.
    ///
    /// @param slot Dying unit.
    /// @param outcome The death kind, Killed percentage and wreck level.
    void teardown_dead_unit(sim::unit_spawn::Slot& slot, const KillOutcome& outcome);

    // A placed corpse: the FeatureDef it stepped to along featuredead
    // (Killed's wreck level less one steps from UNITINFO corpse=).
    struct Wreck {
        uint16_t feature = sim::feature_runtime::no_feature;
        std::array<uint32_t, 3> position{};
        int16_t bank{};
        uint16_t yaw{};
        int16_t pitch{};
        int32_t cell_x{}, cell_z{};
    };

    /// Leaves the unit's corpse, stepped wreck_level - 1 times along
    /// featuredead, where it stood with its orientation and owner.
    ///
    /// A 3DO corpse at or under sea level sinks (unless the unit is a
    /// feature itself) and does not smoke.
    ///
    /// @param unit Dead unit.
    /// @param wreck_level Killed's wreck level, 1 for the plain corpse.
    /// @param smoke Raises a 900-tick smoke column over the corpse.
    void spawn_corpse(sim::unit_spawn::Slot& unit, uint8_t wreck_level, bool smoke);

    /// Returns the corpses placed so far.
    std::span<const Wreck> wrecks() const noexcept { return wrecks_; }

    /// Plays the effects of a weapon going off: its screen shake, then its
    /// soundhit with the land art and small flash or an endsmoke puff, or its
    /// soundwater with the water or lava art.
    ///
    /// The damage tail is detonate's.
    ///
    /// @param weapon The weapon's record.
    /// @param position Signed 16.16 burst point as bit patterns.
    /// @param struck_unit Whether the shot struck a unit.
    /// @return False when a nosealeveltrigger map swallows the shot whole.
    bool spawn_weapon_explosion(
        const oa::WeaponDef& weapon, const std::array<uint32_t, 3>& position, bool struck_unit
    );

    MultiplayerHooks multiplayer{};

    /// Shares that the unit started a script with no arguments
    /// (StopBuilding), by its COB index.
    ///
    /// @param unit Unit slot.
    /// @param function COB index, -1 when the unit lacks the script.
    void share_script_start(uint16_t unit, int16_t function);
    /// Shares that the unit started the named script with the first `count`
    /// of the four locals as arguments.
    ///
    /// @param unit Unit slot.
    /// @param name Script name; one the unit lacks is shared as index -1.
    /// @param count How many of the locals are arguments.
    /// @param locals The four locals.
    void share_named_script_start(
        uint16_t unit, std::string_view name, uint8_t count, const std::array<uint32_t, 4>& locals
    );

    /// Rebinds the legacy per-player unit views after Player.first_unit and
    /// last_unit were reassigned (a multiplayer game orders ranges by player
    /// id).
    void rebind_player_ranges() { state_.views->bind_player_ranges(); }

    /// Moves a unit on an occupancy layer to a position another player's
    /// simulation shared (place_unit).
    ///
    /// @param unit Unit slot; one outside the pool throws.
    /// @param x Signed 16.16 x.
    /// @param y Signed 16.16 y.
    /// @param z Signed 16.16 z.
    /// @param layer Occupancy layer (1 ground, 2 air).
    void place_unit_at(uint16_t unit, int32_t x, int32_t y, int32_t z, uint8_t layer) {
        place_unit(slots_.at(unit), x, y, z, layer);
    }

    /// Launches a shot fired in another player's simulation.
    ///
    /// A meteor weapon's shot falls from the start at the carried velocity.
    /// Otherwise the firing unit, when live, has the slot take the carried
    /// angles, and the constructor the carried weapon's flags pick
    /// (ballistic, vertical launch, line or self-propelled, dropped, in that
    /// order) launches the slot's weapon from the start at the target as the
    /// owner's did, Fire script and all. An interceptor homes on the other
    /// players' shot at the same point fired by the carried target unit.
    ///
    /// @param shot The shared shot.
    void apply_shot(const ShotEvent& shot);

    /// Kills a unit as the kill handler does when the unit's own tick finds
    /// it dying.
    ///
    /// The kind and the unit's health settle the Killed percentage and the
    /// wreck level. A dismissed unit dies at percentage 0 and leaves its
    /// corpse (wreck level 1). A captured, reclaimed or cancelled unit, or
    /// one still above 0 health, dies at percentage 0 with no wreck, and its
    /// Killed script is not run. Any other unit's percentage is
    /// (-health * 100 / maximum health + previous 30-tick health percentage)
    /// / 2, each division truncated, clamped to 1..100; its Killed script
    /// runs with it and returns the percentage and the wreck level it dies
    /// with. An unfinished unit leaves no wreck whatever the script picks. A
    /// last attacker whose player's slot is free credits nobody.
    ///
    /// Then MultiplayerHooks::unit_killed shares the death of a unit
    /// simulated here, its orders are cleared, teardown_dead_unit tears it
    /// down, and, when the match plays the commander rule and the unit is
    /// its player's commander simulated here, the rule's sweep runs.
    ///
    /// A unit already not live is left alone.
    ///
    /// @param unit Unit slot; one outside the pool throws.
    /// @param kind How it died: a DeathKind, or 0 for a unit whose slot
    ///     another player's new unit takes.
    void kill_unit(uint16_t unit, uint8_t kind);

    /// Applies the death of a unit another player's machine simulates, as
    /// that machine settled and shared it: the unit takes the shared last
    /// attacker and its owner, its orders are cleared, and it is torn down
    /// with the shared outcome as teardown_dead_unit tears a unit down. When
    /// the Killed percentage is above zero the unit's Killed script is
    /// started with it once its cargo has left it, for its flying pieces
    /// only; the wreck level that script picks is not read.
    ///
    /// A unit already not live is left alone.
    ///
    /// @param unit Unit slot; one outside the pool throws.
    /// @param outcome The death kind, Killed percentage and wreck level that
    ///     machine settled.
    /// @param attacker Unit slot of the unit that last damaged it there, 0
    ///     for none.
    /// @param attacker_owner Player index 0..9 of that unit's owner, 10 for
    ///     none.
    void apply_kill(
        uint16_t unit, const KillOutcome& outcome, uint16_t attacker, uint8_t attacker_owner
    );

    /// Finishes a unit as the machine that simulates it shared
    /// (MultiplayerHooks::unit_finished there): the builder link marks it
    /// built and flags it for the construction redraw, activates a type
    /// that activates when built and wakes the unit's construction event. A
    /// unit simulated elsewhere stays where it hangs, since its own machine
    /// shares the carry link that sets it down, and nothing is shared for
    /// it; a unit simulated here is also set down off the pad it was built
    /// on and shared as set_carry_link and the builder link do.
    ///
    /// Nothing happens unless both units are live.
    ///
    /// @param unit Finished unit slot; one outside the pool throws.
    /// @param builder Slot of the unit whose work finished it; `unit` for a
    ///     building created finished. One outside the pool does nothing.
    void finish_unit(uint16_t unit, uint16_t builder);

    /// Ends the local player's game at once as a defeat, as another player's
    /// machine reporting this one gone does: the outcome flags gain finished
    /// and lose won, and outcome() reports defeat from now on. A game already
    /// decided ends the same way, so a victory becomes a defeat.
    void end_local_game() noexcept;

    /// Runs one movement tick of a unit simulated elsewhere, once per update
    /// its owner shares.
    ///
    /// @param unit Unit slot; one without a movement object is skipped.
    void step_mirrored_movement(uint16_t unit);
    /// Runs the height and sight refresh that follows a mirrored movement
    /// tick.
    ///
    /// @param unit Unit slot; one without a movement object is skipped.
    void refresh_mirrored_height(uint16_t unit);

    /// Sets or clears state flags, running their scripts and sounds.
    ///
    /// @param unit Unit slot.
    /// @param mask State flag bits.
    /// @param on True sets them, false clears them.
    void set_unit_state_flags(uint16_t unit, uint8_t mask, bool on) {
        set_activation(slots_.at(unit), mask, on);
    }

    /// Returns the air driver of a flying unit's movement object.
    ///
    /// @param unit Unit slot; one outside the pool throws.
    /// @return The driver, or null for a unit that does not fly.
    sim::air::AirDriver* air_driver(uint16_t unit) {
        auto& driver = air_drivers_.at(unit);
        return driver.unit != nullptr ? &driver : nullptr;
    }

    /// Replaces a mirrored air driver's goal with a copy the match keeps for
    /// it. The replaced goal's order is not told.
    ///
    /// @param unit Unit slot; a local or missing driver is left alone.
    /// @param goal Goal to copy, or null to leave the driver none.
    void set_mirrored_air_goal(uint16_t unit, const sim::air::AirGoal* goal);
    /// Switches the movement layer of a unit outside its movement tick; the
    /// movement object is refreshed from the unit first.
    ///
    /// @param unit Unit slot; one without a movement object is skipped.
    /// @param layer New occupancy layer (see the private overload).
    void set_movement_layer(uint16_t unit, uint8_t layer);

  private:

    /// Fills a projectile record a constructor allocated: it starts at
    /// `start`, aimed at `target` when given, fired by the slot's unit (query
    /// piece and owner), with the launch the constructor worked out and the
    /// constructor's target links; the weapon's soundstart plays at the
    /// start.
    ///
    /// @param[out] shot Allocated projectile record.
    /// @param source Firing unit.
    /// @param slot Weapon slot 0..2.
    /// @param launch Heading, pitch, speed, velocity, lifetime and burst of
    ///     the launch.
    /// @param start Signed 16.16 start point.
    /// @param target Signed 16.16 aim point, or null for none.
    /// @param target_unit Target unit slot, 0 for none.
    /// @param intercept_target Projectile an interceptor homes on, 0 for none.
    /// @param query_piece COB piece the shot left from.
    void place_shot(
        oa::Projectile& shot,
        sim::unit_spawn::Slot& source,
        uint8_t slot,
        const sim::weapon_execution::ProjectileLaunch& launch,
        const FixedVec3& start,
        const FixedVec3* target,
        uint16_t target_unit,
        oa_ref32 intercept_target,
        uint16_t query_piece
    );
    /// Runs what the constructors other than the dropped one do once the
    /// shot is out: the slot's Fire script, RockUnit toward the slot's heading
    /// and, for a startsmoke weapon, a puff at the start.
    ///
    /// @param source Firing unit.
    /// @param slot Weapon slot 0..2.
    /// @param heading Slot's aim heading in 65536ths of a turn.
    /// @param start Signed 16.16 start point.
    void run_fire_scripts(
        sim::unit_spawn::Slot& source, uint8_t slot, int16_t heading, const FixedVec3& start
    );
    /// Shares a shot a local unit fired through multiplayer.shot_fired.
    ///
    /// @param shot The shot as the other players launch it.
    void share_shot(const ShotEvent& shot);
    /// Tears a dead unit down as the two-argument teardown_dead_unit does; a
    /// death another player's machine settled also starts the unit's Killed
    /// script with the outcome's percentage once its cargo has left it, for
    /// its flying pieces only.
    ///
    /// @param slot Dying unit.
    /// @param outcome The death kind, Killed percentage and wreck level.
    /// @param settled_elsewhere True for a death another player's machine
    ///     settled.
    void teardown_dead_unit(
        sim::unit_spawn::Slot& slot, const KillOutcome& outcome, bool settled_elsewhere
    );

    /// Gives a unit simulated here that anything but an air base now carries
    /// BeCarried in place of its orders; one resting on an air pad keeps
    /// them.
    ///
    /// @param child Carried unit slot.
    /// @param parent Carrier slot, 0 when detached.
    void attachment_local_update(uint16_t child, uint16_t parent);
    /// Drops a unit's air driver with its movement object; a mirrored copy's
    /// driver owns the goal it was given and frees it.
    ///
    /// @param unit Unit slot.
    void release_air_driver(uint16_t unit);
    /// Clears a carried unit's orders and puts BeCarried at their head.
    ///
    /// @param child Carried unit slot; one not carried is left alone.
    void install_be_carried(uint16_t child);

    friend class TickHost;
    friend class WeaponTickHost;
    friend class TargetHost;
    friend class ProjectileDamageHost;
    friend struct FeatureCalls;

    /// Places the map's features on the canonical plots, from the loader's
    /// resolved plots in row-major order (markers first, then FeatureDefs),
    /// then the mission schema's placements; hides the map edges and
    /// projects the result onto the match plots. While a savegame resumes
    /// only the markers are placed, but each plot's metal still starts from
    /// the loader's, which holds the metal of the map's indestructible
    /// features. The save's Metal section then replaces it; a save whose
    /// Metal section is missing or not one byte per cell keeps that metal,
    /// where the game keeps the map's metal without it.
    ///
    /// A placed-feature pool smaller than the game's throws.
    void place_map_features();
    /// Rewrites the match plots' feature fields over a rectangle from the
    /// canonical plots and FeatureDef table.
    ///
    /// @param cell Top-left cell (x, z); cells off the map are skipped.
    /// @param footprint Size in cells (x, z).
    void project_feature_plots(std::array<int16_t, 2> cell, std::array<int16_t, 2> footprint);
    /// Copies the ground and air occupant words of the match plots to the
    /// canonical plots, so that every canonical plot holds its match plot's.
    ///
    /// The first call copies every plot; a later one copies the plots the
    /// spatial state lists as written since (written_occupants), or every
    /// plot when they come to an eighth of the map's plots or more or the
    /// list lost some, and empties the list.
    void project_plot_occupants();
    /// Sets off a burn weapon at a point: the area damage of a stand-in shot
    /// of no player.
    ///
    /// @param weapon Weapon reference; an unknown one does nothing.
    /// @param at Signed 16.16 point.
    void burn_weapon_blast(oa_ref32 weapon, const FixedVec3& at);

    /// Damages every unit and feature within half the blast width of the
    /// shot, detonates the other live shots within the width of an
    /// interceptor's blast, then records the summed damage on the source's
    /// reaction bits.
    ///
    /// @param shot The exploding shot; its source is spared.
    /// @param definition The weapon's registry entry (area of effect,
    ///     flags).
    /// @param weapon The weapon's record (edge effectiveness).
    void detonate_area(
        oa::Projectile& shot,
        const sim::combat_state::WeaponDefinition& definition,
        const oa::WeaponDef& weapon
    );
    /// Returns the match plot under a world point.
    ///
    /// @param x Signed 16.16 x.
    /// @param z Signed 16.16 z.
    /// @return The plot, or null off the map.
    const sim::spatial_state::Plot* plot_at(int32_t x, int32_t z) const;
    /// Runs the contact test on the plot under a moved shot and acts on its
    /// outcome: retire it off the map, or detonate it on a unit, feature,
    /// ground or water.
    ///
    /// @param shot The moved shot.
    void resolve_projectile_contact(oa::Projectile& shot);
    /// Returns the slot of the unit that fired a projectile.
    ///
    /// @param shot Projectile record.
    /// @return The shooter, or null for a neutral shot.
    sim::unit_spawn::Slot* projectile_source(const oa::Projectile& shot);

    /// Credits metal into a unit's metal accumulator
    /// (Unit.economy.metal.produced), scaled for easy and medium computer
    /// owners; the economy tick moves it into the player store.
    ///
    /// @param target Unit credited.
    /// @param amount Metal before scaling.
    void credit_metal(oa::Unit& target, float amount);
    /// Credits energy into a unit's energy accumulator
    /// (Unit.economy.energy.produced), scaled like credit_metal.
    ///
    /// @param target Unit credited.
    /// @param amount Energy before scaling.
    void credit_energy(oa::Unit& target, float amount);

    struct RuntimeOrder {
        sim::simulation_state::Order order;
        sim::ground_orders::OrderState extra;
        sim::air::AirGoal air_goal{}; // goal of an aircraft order
        AttackOrderState attack;
        ConstructionOrderState construction;
        sim::simulation_state::Unit* unit{};
        RuntimeOrder* observer_next{};
    };

    std::map<sim::simulation_state::Unit*, RuntimeOrder*> target_observers_;
    /// Makes every order that targets the dead unit lose it, emptying the
    /// unit's list of observing orders.
    ///
    /// @param target Dead unit.
    void release_target_observers(sim::simulation_state::Unit& target);
    /// Makes one observing order lose its target: it is woken with the
    /// target-lost event, then no longer points at the target.
    ///
    /// @param[in,out] entry Observing order.
    /// @param target Unit it loses.
    void lose_target(RuntimeOrder& entry, const sim::simulation_state::Unit& target);
    /// Links an order into its target's list of observing orders, as the
    /// head. The order is its own listener.
    ///
    /// @param[in,out] entry Order to link.
    /// @param target Target unit; a missing or typeless one (Unit.type_index
    ///     zero) leaves the order without a target.
    void link_target_observer(RuntimeOrder& entry, sim::simulation_state::Unit* target);
    /// Takes an order out of its target's list of observing orders, leaving
    /// it without a target.
    ///
    /// @param[in,out] entry Order to unlink.
    void unlink_target_observer(RuntimeOrder& entry);
    std::vector<std::unique_ptr<RuntimeOrder>> orders_;
    SelectionState selection_;
    WindState wind_;
    sim::world_environment::WindState environment_wind_{};
    sim::ground_orders::SearchScheduler search_;

    // A movement class of the 32-entry class table: the limits
    // the cell classifier reads and the class's persistent map, whose
    // projection tick is the classifier's occupancy_before_tick. Entries fill
    // in the order loaded types first name their class.
    struct MovementClassMap final : sim::ground_orders::MovementMapSampler {
        Match* match{};
        sim::unit_spawn::AssetHandle handle{};       // UnitDef.move_class
        sim::spatial_state::CellClassifier record{}; // footprint, water depths and slopes
        std::optional<sim::ground_orders::MovementMap> map;
        /// Returns the class record with the map's projection tick as its
        /// occupancy tick.
        sim::spatial_state::CellClassifier classifier() const;
        /// Classifies one movement cell for the class.
        ///
        /// @param x Cell x.
        /// @param z Cell z.
        /// @return The cell's movement class value.
        uint8_t classify_cell(int32_t x, int32_t z) override;
        /// Classifies one plot for the class.
        ///
        /// @param x Plot x.
        /// @param z Plot z.
        /// @return The plot's movement class value.
        uint8_t classify_plot(int32_t x, int32_t z) override;
    };

    std::array<MovementClassMap, OA_MOVE_CLASS_COUNT> movement_classes_{};
    // The live unit list a search job feeds its class's map.
    std::vector<sim::ground_orders::OccupancyChange> occupancy_feed_;
    /// Allocates and builds the map of every movement class a loaded type
    /// names, before any unit stands on the map.
    ///
    /// More classes than the class table holds throw.
    void build_movement_maps();
    /// Finds the entry of a movement class.
    ///
    /// @param movement_class The UnitDef.move_class handle.
    /// @return The entry, or null when no loaded type names the class.
    MovementClassMap* movement_class_map(sim::unit_spawn::AssetHandle movement_class);

    // Spatial registration callbacks: the map-region listeners refresh the
    // movement maps before the services see the change.
    struct MapListeners final : sim::spatial_state::Host {
        Match& match;

        explicit MapListeners(Match& owner) : match(owner) {}

        /// Passes a plot height range refresh on to the services.
        ///
        /// @param origin_minus_one Top-left cell less one (x, z).
        /// @param footprint_plus_two Size in cells plus two (x, z).
        void refresh_plot_height_range(
            std::array<int16_t, 2> origin_minus_one, std::array<int16_t, 2> footprint_plus_two
        ) override;
        /// Reclassifies a changed footprint in every movement map, then tells
        /// the services.
        ///
        /// @param cell Top-left cell (x, z).
        /// @param footprint Size in cells (x, z).
        void notify_footprint_changed(
            std::array<int16_t, 2> cell, std::array<int16_t, 2> footprint
        ) override;
        /// Reclassifies the old rectangle in the maps whose projection held the
        /// object as a wall, then tells the services.
        ///
        /// @param unit Projection of the unit that left.
        /// @param old_tick The occupancy tick the object was registered at.
        void
        notify_object_footprint_removed(sim::spatial_state::Unit& unit, uint32_t old_tick) override;
    } map_listeners_{*this};

    std::array<std::optional<std::array<uint8_t, 10>>, 10> player_alliances_;
    // Each player's sightings, over storage of one entry per unit slot for
    // both lists of every player.
    std::array<sim::detection::Sightings, OA_PLAYER_COUNT> sightings_{};
    std::vector<uint16_t> sighting_slots_;
    std::vector<sim::combat_state::TargetUnit> sighting_query_;
    std::array<sim::combat_state::StrategicRefreshState, 10> strategic_states_;
    std::optional<StrategicEnvironment> strategic_environment_;
    std::vector<sim::combat_state::TargetUnit> target_projection_;
    // Per player: the unit slot the weapon sweep visited last.
    std::array<uint16_t, 10> weapon_sweep_cursor_{};
    /// Re-aims one weapon slot: an interceptor at the first projectile it may
    /// intercept; a slot of a unit that fires at will at a random enemy
    /// within its range. Finding none clears the slot's target.
    ///
    /// @param unit Unit owning the slot.
    /// @param slot Weapon slot 0..2.
    void retarget_weapon_slot(sim::unit_spawn::Slot& unit, uint8_t slot);
    /// Searches a target for one weapon slot through the unit's sightings.
    ///
    /// Categories the unit's type masks resolve are required; missing ones
    /// throw.
    ///
    /// @param unit Unit searching.
    /// @param request Weapon slot and whether its range (else the sight
    ///     radius) bounds the search.
    /// @return The target, or null for none.
    sim::simulation_state::Unit* search_automatic_target(
        sim::simulation_state::Unit& unit, const sim::combat_state::TargetSearchRequest& request
    );
    /// Orders an unforced attack of one unit on another (issue_attack).
    ///
    /// @param source Attacker.
    /// @param target Target.
    /// @return False when the attack was refused.
    bool issue_automatic_attack(
        sim::simulation_state::Unit& source, sim::simulation_state::Unit& target
    );
    /// Finds the match's record of an order.
    ///
    /// @param order Order, or null.
    /// @return The record, or null when the match does not own the order.
    RuntimeOrder* runtime_order(const sim::simulation_state::Order* order);
    /// Raises an event on every order that observes a unit.
    ///
    /// @param unit Observed unit.
    /// @param event Event bits to raise.
    void wake_target_observers(sim::simulation_state::Unit& unit, uint32_t event);
    /// Runs the damaged unit's reaction (sim::weapon_execution::retaliate) over
    /// the match's orders, AI records and categories: its observers wake
    /// with event 0x10, then it may strike back, and an attack notice is
    /// spoken.
    ///
    /// @param target Damaged unit.
    /// @param source Attacker, or null.
    void react_to_damage(sim::unit_spawn::Slot& target, sim::unit_spawn::Slot* source);
    /// Applies kind-2 damage (sim::unit_health::paralyze_action): extends the
    /// Paralyze order at the head of the primary queue, or inserts one.
    ///
    /// @param target Paralysed unit.
    /// @param amount Paralysis in ticks.
    void paralyze(sim::unit_spawn::Slot& target, int16_t amount);
    /// Builds the attack orders the attack resolution asks for and puts each
    /// at the head of its queue.
    ///
    /// @param source Attacker.
    /// @param requests Orders to build; a kind outside the attack family
    ///     throws.
    /// @return True.
    bool commit_attack_orders(
        const sim::combat_state::AttackSource& source,
        std::span<const sim::combat_state::AttackOrderRequest> requests
    );
    OfflineInputs input_;
    OfflineServices& services_;
    SpeechHooks speech_hooks_{};
    // The shot apply_shot is placing, whose aim and target unit place_shot
    // reports to event_hooks.shot_placed in place of its own; place_shot
    // clears it. Null otherwise.
    const ShotEvent* applied_shot_{};

    // Canonical World, its tables, the native side tables indexed by unit slot or
    // player, and the legacy views bound over them. Addresses are stable.
    struct State {
        /// Allocates the world, pool, type, plot, feature and projectile
        /// tables for the inputs and binds the legacy views over them.
        ///
        /// @param input Match inputs; inconsistent type tables or a
        ///     viewpoint outside the ten players throw.
        explicit State(const OfflineInputs& input);
        /// Fills the UnitDef table from the FBI loader's records, or
        /// assembles each from the runtime type, and binds the category
        /// masks to this match's table.
        ///
        /// @param input Match inputs.
        void load_types(const OfflineInputs& input);
        std::unique_ptr<oa::World> world;
        std::vector<oa::Unit> units;
        std::vector<oa::UnitDef> unit_defs;
        std::vector<oa::FeatureDef> feature_defs;
        std::unique_ptr<oa::MapPlot[]> plots;
        std::unique_ptr<sim::feature_runtime::PlacedFeature[]> placed_features;
        std::vector<oa::Projectile> projectiles;
        std::vector<sim::unit_spawn::SlotAssets> assets;
        std::array<sim::unit_spawn::PlayerSetupState, 10> setups{};
        std::vector<sim::simulation_state::OrderQueue> orders;
        // Category objects the UnitDef category refs index (ref - 1).
        std::vector<const data::unit_definitions::UnitCategoryMask*> category_masks;
        sim::unit_spawn::Tables tables;
        std::unique_ptr<sim::unit_spawn::LegacyViews> views;
    };

    State state_;
    sim::simulation_state::World& simulation_;
    sim::unit_spawn::World& world_;
    sim::unit_spawn::legacy::Span<sim::unit_spawn::Slot> slots_;
    sim::unit_spawn::legacy::Span<sim::simulation_state::Unit> units_;
    std::vector<sim::spatial_state::Unit> spatial_units_;
    std::vector<sim::combat_state::UnitWeapons> weapons_;
    std::vector<std::optional<formats::objects3d::UnitTypeBounds>> type_bounds_;
    std::vector<std::unique_ptr<sim::ground_orders::GroundRuntime>> movement_;
    std::vector<sim::air::AirDriver> air_drivers_; // driver of each flying unit's movement object
    // Goal each mirrored air driver owns; drivers point into it, so it is
    // never resized.
    std::vector<sim::air::AirGoal> mirrored_air_goals_;
    sim::spatial_state::World spatial_;
    // Whether the inputs held the map's collision plots, which moving
    // collision requires.
    bool collision_terrain_{};
    // Whether project_plot_occupants has copied every plot once.
    bool occupants_projected_{};
    sim::visibility_state::PlayerSightGrid sight_;
    std::array<std::vector<uint8_t>, 10> other_player_coverage_;
    sim::scenario::Controller scenario_;
    int32_t scenario_gravity_{}; // OTA GlobalHeader gravity, as the scenario holds it
    int32_t lava_world_{};       // OTA GlobalHeader lavaworld, as the scenario holds it
    sim::scenario::OutcomeState outcome_state_;
    sim::scenario::Outcome outcome_result_{sim::scenario::Outcome::ongoing};
    std::optional<sim::scenario::OutcomeView> outcome_view_;
    bool defeat_allowed_{};
    bool campaign_outcomes_{};
    bool multiplayer_outcomes_{};
    // Set once end_local_game ended the local player's game: its outcome
    // checks no longer run, so the defeat stands.
    bool local_game_ended_{};
    std::vector<sim::scenario::UnitStatus> outcome_unit_status_{};
    /// Runs each active slot's controller, knowledge refresh and sight
    /// stamps and, when its 30-tick deadline (Player.next_economy_tick)
    /// comes due, the local slot's outcome checks, the slot's economy and the
    /// viewpoint's contact scan; then the multiplayer game's abandonment test.
    ///
    /// @quirk The deadline advances by the period, not from the current
    ///     tick.
    void update_player_slots();
    /// Runs the local slot's victory and defeat checks when its deadline
    /// comes due, then respawns the commander, makes the player a watcher or
    /// records the outcome as they ask. Nothing runs once end_local_game
    /// ended the game.
    ///
    /// @quirk A campaign's defeat is tested only while its victory is not
    ///     met, and a watcher is never tested for defeat.
    void advance_local_outcome();
    /// Tells whether a deathmatch commander can be placed: the local player
    /// has a setup block whose side names a commander.
    ///
    /// @return True when the respawn is possible.
    bool respawn_bound() const noexcept;
    /// Places a new commander of the local player's side on a random clear
    /// site once its deathmatch defeat countdown runs out, with the host's
    /// start storage and the host's start resources credited to it; then
    /// rebuilds the sight grids and lets the view reselect.
    ///
    /// With no host seated (a skirmish) the eleventh player record's setup
    /// block stands in for the host's. A commander missing from the unit
    /// catalog, or one that cannot be created, throws.
    void respawn_local_commander();
    /// Makes the defeated multiplayer player a watcher: its setup block is
    /// marked watching, mapping and line of sight go off and the sight grids
    /// are rebuilt, the other players are told its setup block, and the view
    /// asks whether to go on watching unless computer players this machine
    /// hosts still play; then, with a human left, a pending win is dropped
    /// and the view says why it must watch.
    ///
    /// @param[in,out] local_info The local player's setup block.
    void become_watcher(PlayerSetupInfo& local_info);
    /// Draws random points inside the map less a tenth of each side until
    /// one is clear for a type.
    ///
    /// @param type Commander type index.
    /// @return Signed 16.16 site as bit patterns (y zero); after 9999 draws
    ///     the last point stands, clear or not.
    std::array<uint32_t, 3> respawn_site(uint16_t type);
    /// Tells whether a respawn site is clear: the type fits at all nine
    /// points of a 3x3 grid around it spaced by the map's cell counts taken
    /// as world units, no feature covers it and, on a lava world, the ground
    /// under it is above the sea.
    ///
    /// @param type Commander type index.
    /// @param site Signed 16.16 site as bit patterns.
    /// @return True when clear.
    /// @quirk Grid points past the near map edge wrap to far cells.
    bool respawn_site_clear(uint16_t type, const std::array<uint32_t, 3>& site) const;
    /// Returns the campaign conditions' view of this match: the "Victory
    /// Condition" voice cue, the ground runtimes' movement objects and the
    /// MoveUnitToRadius query.
    ///
    /// @return The host, bound to this match.
    sim::scenario::ConditionHost scenario_condition_host();
    /// Evaluates MoveUnitToRadius: the stored map point is placed on the
    /// terrain once (a height of sim::scenario::unplaced_point_height says it is
    /// not yet), then every unit within the radius of it is tested.
    ///
    /// @param[in,out] condition The condition; its point and satisfied flag
    ///     are written.
    /// @return Whether the condition is met.
    bool move_unit_to_radius_met(sim::scenario::Condition& condition);
    // The disc-check loss; the engine has no disc check, so it stays
    // inactive.
    sim::scenario::DiagnosticLoss disc_check_loss_{};
    /// Tells every campaign condition that a unit was destroyed, running the
    /// handlers of the kinds that react to it.
    ///
    /// @param destroyed The destroyed unit.
    void scenario_unit_destroyed(sim::unit_spawn::Slot& destroyed);
    /// Tells every campaign condition that a unit was captured, running the
    /// handlers of the kinds that react to it.
    ///
    /// @param captured The captured unit, still with its former owner.
    void scenario_unit_captured(sim::unit_spawn::Slot& captured);
    // The viewer's remembered stamps of its dead units. The match keeps them
    // here and leaves Game.remembered_sight zero.
    sim::visibility_state::EyeballMemory remembered_sight_{};
    /// Returns the rules, tables and grids of this match's sight stamps.
    sim::visibility_state::SightContext sight_context();
    sim::unit_movement::Terrain terrain_;
    std::array<std::map<uint32_t, std::vector<uint16_t>>, 10> groups_;
    SharedRandom random_;
    std::shared_ptr<void> computer_player_state_;
    uint32_t lcg_seed_{};
    std::unique_ptr<sim::effect_particles::EffectWorld> effects_;
    formats::gaf::Sequence flash_tiers_[sim::effect_particles::flash_tier_count];
    bool effect_sequences_resolved_{};
    /// Resolves the FX.GAF effect sequences through the effect loader once
    /// any of them resolves.
    void resolve_effect_sequences();
    /// Resolves a weapon's explosion sequence.
    ///
    /// @param archive GAF archive name.
    /// @param entry Sequence name.
    /// @return The sequence, or null when either key is empty or no loader
    ///     was given.
    const formats::gaf::Sequence*
    weapon_effect_sequence(std::string_view archive, std::string_view entry) const;
    /// Resolves a weapon's water explosion sequence
    /// (WeaponDef.water_explosion_art), which holds the lava art on lava
    /// worlds.
    ///
    /// @param weapon The weapon's registry entry.
    /// @return The sequence, or null.
    const formats::gaf::Sequence*
    water_effect_sequence(const sim::combat_state::WeaponDefinition& weapon) const;
    /// Allocates the frame storage of the explosion flash tiers; each pixel
    /// is literal.
    void allocate_flash_tiers();
    /// Returns the worker's nano piece in world space, or its position
    /// without a script.
    ///
    /// @param worker Spraying unit.
    /// @param fallback Unit whose position stands in for an empty slot.
    /// @return Signed 16.16 point.
    FixedVec3
    nano_nozzle(sim::unit_spawn::Slot& worker, const sim::simulation_state::Unit& fallback);
    /// Spawns a white smoke puff.
    ///
    /// @param position Signed 16.16 point.
    void spawn_light_puff(const FixedVec3& position);
    /// Runs the per-shot tail of the projectile tick: a smoke-trail shot
    /// still in flight puffs whenever its created tick has passed and moves
    /// it on by smokedelay, then a shot dropping from above to at or below
    /// sea level over water splashes with its water art.
    ///
    /// @param[in,out] shot The flying shot.
    /// @param previous_height Its whole-unit height before this tick's move.
    void projectile_trail_effects(oa::Projectile& shot, int16_t previous_height);
    /// Draws from the match's LCG stream for the effect world.
    ///
    /// @param match The Match.
    /// @return A value in 0..0x7fff.
    static int32_t effect_lcg_rand(void* match);
    /// Draws from the match's shared stream for the effect world.
    ///
    /// @param match The Match.
    /// @param bound Exclusive upper limit.
    /// @return A value below `bound`, or 0 for a bound below 2.
    static uint32_t effect_synced_rand(void* match, uint32_t bound);
    /// Samples the terrain height grid for the effect world.
    ///
    /// @param match The Match.
    /// @param position Signed 16.16 point.
    /// @return Height in whole units.
    static int32_t effect_grid_height(void* match, const FixedVec3& position);
    /// Samples the mean plot height for the effect world.
    ///
    /// @param match The Match.
    /// @param position Signed 16.16 point.
    /// @return Height in whole units.
    static int32_t effect_ground_height(void* match, const FixedVec3& position);
    std::vector<Wreck> wrecks_;
    std::unique_ptr<SpawnBridge> bridge_;
    std::unique_ptr<TraceRecorder> trace_;
    /// Returns the runtime type fields of a unit's type.
    ///
    /// @param slot Unit; a type outside the table or without an FBI
    ///     definition throws.
    /// @return The type's fields.
    const RuntimeTypeFields& fields(sim::unit_spawn::Slot& slot) const;
    /// Refreshes and returns a unit's spatial projection.
    ///
    /// @param slot Unit to project.
    /// @return The unit's projection.
    sim::spatial_state::Unit& project_spatial(sim::unit_spawn::Slot& slot);
    /// Starts TargetCleared(slot) on the unit's script, as clearing a weapon
    /// target does.
    ///
    /// @param unit Unit whose weapon lost its target.
    /// @param slot Weapon slot 0..2.
    void target_cleared(oa::Unit& unit, uint8_t slot);
    /// Returns the paralysis hooks whose target_cleared runs the unit's
    /// TargetCleared script.
    sim::unit_health::ParalysisHooks weapon_target_hooks();
    /// Clears a weapon slot's unit or ground target, then runs
    /// TargetCleared.
    ///
    /// @param slot Unit owning the weapon.
    /// @param index Weapon slot 0..2; another throws.
    void stop_weapon(sim::unit_spawn::Slot& slot, uint32_t index) override;
    /// Brings stood-down weapon slots back: an enabled slot carrying the
    /// stand-down bit loses it and its target.
    ///
    /// @param slot Unit owning the weapons.
    /// @param index Weapon slot 0..2, or 3 for all three.
    void release_tracked_weapons(sim::unit_spawn::Slot& slot, uint8_t index);
    /// Moves the unit from its squad's member list (swapping the last member
    /// into its place) to the end of another squad's.
    ///
    /// @param slot Unit to move; an owner outside the ten players throws.
    /// @param group Squad number; 0xffffffff (-1) joins none.
    void assign_squad(sim::unit_spawn::Slot& slot, uint32_t group) override;
    /// Sets up a new unit's weapon slots from its type's three weapons, with
    /// their muzzle offsets from the Query and AimFrom scripts.
    ///
    /// @param slot New unit.
    /// @param runtime Its match-side state; the instance must exist, or this
    ///     throws.
    void initialize_weapons(sim::unit_spawn::Slot& slot, SlotRuntime& runtime) override;
    /// Sets a new unit's metal extraction rate from the plots under its
    /// footprint, calling SetSpeed on its script.
    ///
    /// @param slot New unit.
    /// @param runtime Its match-side state.
    void initialize_extraction_rate(sim::unit_spawn::Slot& slot, SlotRuntime& runtime) override;
    /// Builds a new unit's movement object, with an air driver for a can-fly
    /// type: local, or a mirrored copy for a unit another player's
    /// simulation runs.
    ///
    /// @param slot New mobile unit; an unresolved movement class throws.
    /// @return Handle of the movement object.
    sim::unit_spawn::AssetHandle create_movement(sim::unit_spawn::Slot& slot) override;
    /// Settles a new unit's height when it stands in the ground layer: on
    /// the terrain, or the higher of the terrain and the waterline for a
    /// hovering one, for an upright type; at the waterline for a floater;
    /// else through the movement object's ground fit, which also sets the
    /// pitch and bank from the slope under the model's ground plate. Aircraft
    /// take the ground fit like any other type.
    ///
    /// A hovering ground fit without the platform clock throws.
    ///
    /// @param slot New unit.
    /// @quirk A hovercraft's fit takes the higher of the terrain and the sea
    ///        at each corner plus its bob, and nothing lifts the result back
    ///        to the surface: one created in the first 60 ticks, while its
    ///        bob has not yet faded, can start just under the sea.
    void fit_spawn_height(sim::unit_spawn::Slot& slot) override;
    /// Registers a unit's footprint and bucket on the map; collision can
    /// change flags on units inserted before.
    ///
    /// @param slot Unit to register; a rejected registration throws.
    void register_occupancy(sim::unit_spawn::Slot& slot) override;
    /// Moves a unit on an occupancy layer. A new cell or layer moves its
    /// footprint, bucket and sight; otherwise only the position changes.
    ///
    /// @param slot Unit to move.
    /// @param x Signed 16.16 x.
    /// @param y Signed 16.16 y.
    /// @param z Signed 16.16 z.
    /// @param layer Occupancy layer (1 ground, 2 air).
    void
    place_unit(sim::unit_spawn::Slot& slot, int32_t x, int32_t y, int32_t z, uint8_t layer = 1);
    /// Returns the model bounds of a unit's type.
    ///
    /// @param unit Unit.
    /// @return The bounds, or null for a type without a model.
    const formats::objects3d::UnitTypeBounds*
    bounds_for(const sim::simulation_state::Unit& unit) const;
    /// Shares a new unit through multiplayer.unit_created while the run flag
    /// is set; without a handler then, this throws.
    ///
    /// @param slot New unit.
    void notify_created(sim::unit_spawn::Slot& slot) override;
    /// Shares a building (bmcode 0) created finished, after its creation,
    /// as a unit its own work finished (the two-argument notify_finished
    /// with the unit as its builder).
    ///
    /// @param slot New unit.
    void notify_finished(sim::unit_spawn::Slot& slot) override;
    /// Shares a finished unit through multiplayer.unit_finished while the
    /// run flag (Game.session_flags bit 0) is set and the unit is simulated
    /// here; otherwise, or with the entry null, nothing is shared.
    ///
    /// @param unit Finished unit.
    /// @param builder Unit whose work finished it, or the unit itself.
    void notify_finished(sim::unit_spawn::Slot& unit, sim::unit_spawn::Slot& builder);
    /// Runs the builder link on a unit whose build is complete: its build
    /// progress becomes finished (Unit.build_remaining 0) and it is flagged
    /// for the construction redraw, a type that activates when built is
    /// activated, and the unit's construction event wakes its orders. A
    /// unit simulated here is then set down off the pad that carries it,
    /// unless it is a building, and its completion is shared
    /// (notify_finished).
    ///
    /// @param unit Finished unit; must be live.
    /// @param builder Unit whose work finished it, or the unit itself.
    void link_built_unit(sim::unit_spawn::Slot& unit, sim::unit_spawn::Slot& builder);
    /// Reports a unit the spawn placed to event_hooks.unit_created, then, for
    /// a building (bmcode 0) created finished, to unit_finished as finished
    /// by itself, as the spawn shares it.
    ///
    /// @param slot The new unit's slot; null reports nothing.
    /// @param finished Whether the request created the unit finished.
    void report_created(const sim::unit_spawn::Slot* slot, bool finished);
    /// Reports a unit finished to event_hooks.unit_finished, once from its
    /// creation on, whichever path finishes it first.
    ///
    /// @param unit Finished unit slot.
    /// @param builder Slot of the unit whose work finished it; `unit` for a
    ///     building created finished.
    void report_finished(uint16_t unit, uint16_t builder);
    /// Tests set_carry_link's checks: the child is a live unit, not a
    /// building, carrying nothing; a parent is another live unit, not
    /// carried itself.
    ///
    /// @param child Unit slot to carry.
    /// @param parent Carrier slot, 0 to detach.
    /// @return True when the link may apply.
    bool carry_link_accepted(uint16_t child, uint16_t parent);
    /// Applies a carry link that passed carry_link_accepted: the sibling
    /// chains, bucket membership, piece, movement layer and orders
    /// set_carry_link describes.
    ///
    /// @param child Unit slot to carry.
    /// @param parent Carrier slot, 0 to detach.
    /// @param piece COB piece of the carrier, -1 for none.
    /// @param mode Movement layer the child's movement object takes.
    void link_carried_unit(uint16_t child, uint16_t parent, int8_t piece, uint8_t mode);
    /// Sets or clears state flags with their scripts, sounds, observer wake
    /// and sharing.
    ///
    /// @param slot Unit whose flags change.
    /// @param mask State flag bits.
    /// @param enabled True sets them, false clears them.
    void set_activation(sim::unit_spawn::Slot& slot, uint8_t mask, bool enabled) override;
    /// Switches the movement layer. A layer of exactly 1 stops the unit,
    /// levels its attitude with no velocity change and deactivates it; any
    /// other activates it.
    ///
    /// @param s Unit.
    /// @param g Its movement object.
    /// @param layer New occupancy layer.
    /// @quirk The layer is compared unmasked and only its low bits are
    ///     stored, after the activation change.
    void set_movement_layer(
        sim::unit_spawn::Slot& s, sim::ground_orders::GroundRuntime& g, uint8_t layer
    );
    /// Stamps a unit's sight on its owner's grids.
    ///
    /// @param slot Unit; one without a type or owner throws.
    void update_sight(sim::unit_spawn::Slot& slot) override;
    /// Moves a unit's sight stamp to where it now stands.
    ///
    /// @param slot Unit; one without a type or owner throws.
    void update_moving_sight(sim::unit_spawn::Slot& slot);
    /// Moves the sight stamp of every live unit in the player's range, as the
    /// player loop does each tick.
    ///
    /// @param player Player record.
    void update_player_sight(const oa::Player& player);
    /// Reports a unit's creation to the mission's conditions.
    ///
    /// @param slot New unit.
    void notify_scenario_created(sim::unit_spawn::Slot& slot) override;
    /// Returns the world position of a unit's script piece.
    ///
    /// @param slot Unit; one without an instance throws.
    /// @param piece COB piece index.
    /// @return Signed 16.16 x, y, z bit patterns.
    std::array<uint32_t, 3>
    piece_world_position(sim::unit_spawn::Slot& slot, uint32_t piece) override;
    /// Returns the 16-bit heading of a horizontal vector.
    ///
    /// @param x Signed x component as a bit pattern.
    /// @param z Signed z component as a bit pattern.
    /// @return Heading in 65536ths of a turn.
    uint16_t direction_to(uint32_t x, uint32_t z) override;
    /// Returns the horizontal length of a vector.
    ///
    /// @param x Signed x component as a bit pattern.
    /// @param z Signed z component as a bit pattern.
    /// @return Length of (x, z).
    uint32_t distance(uint32_t x, uint32_t z) override;
    /// Returns the integer terrain height at a map position.
    ///
    /// @param x Signed 16.16 map x as a bit pattern.
    /// @param z Signed 16.16 map z as a bit pattern.
    /// @return Terrain height in whole units.
    int32_t sample_terrain_height(uint32_t x, uint32_t z) override;
    /// Opens or closes a factory yard, updating the footprint occupancy and
    /// the unit's yard bit (build_flags bit 2).
    ///
    /// @param slot Factory.
    /// @param value Nonzero opens the yard, zero closes it.
    void set_yard_open(sim::unit_spawn::Slot& slot, int32_t value) override;
};
} // namespace oa::sim::match_runtime
