// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "match_state.hpp"
#include "oa/sim/air/goal.hpp"
#include "oa/sim/air/host.hpp"
#include "oa/sim/match_runtime/command.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include "oa/sim/ballistics.hpp"
#include "oa/sim/unit_health.hpp"
#include "oa/sim/unit_movement/movement.hpp"
#include "oa/sim/weapon_execution.hpp"
#include "oa/sim/weapon_execution/retaliation.hpp"
#include "oa/sim/weapon_execution/weapon_launch.hpp"
#include "oa/base/game_math.hpp"
#include "oa/sim/spatial_state/cell_classifier.hpp"
#include <cctype>
#include <exception>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace oa::sim::match_runtime::tick_detail {

[[noreturn]] inline void unsupported(const char* operation) {
    throw std::runtime_error(std::string("unsupported simulation branch: ") + operation);
}

[[noreturn]] inline void unsupported(const std::string& operation) {
    throw std::runtime_error("unsupported simulation branch: " + operation);
}

// The economy tick's float-to-integer conversion: the value is truncated to
// 64 bits and its low 32 bits are kept. NaN and values past 64 bits give
// INT64_MIN, whose low word is zero.
inline int32_t truncate_low32(float value) noexcept {
    constexpr double limit = 9223372036854775808.0;
    if (!std::isfinite(value) || value >= limit || value < -limit)
        return 0;
    const auto wide = static_cast<int64_t>(std::trunc(value));
    return static_cast<int32_t>(static_cast<uint32_t>(wide));
}

// Overlay/queue walks must stop at a cycle. Patrol clone and a bad
// queue-tail insert can loop the primary queue; a 100000-step budget still
// freezes Shift-held drawing (visit_primary_queue pushes that many path
// points).
inline constexpr std::size_t overlay_order_budget = 256;

// SelfDestruct keeps its countdown in the order's second parameter with this
// marker in the top nibble once counting has started.
inline constexpr uint32_t self_destruct_countdown_marker = 0xf0000000u;

// Command flags bit: the order the next queued order is placed after.
inline constexpr uint8_t command_queue_tail = 0x10;

/// Queues an order after the one carrying the queue-tail mark, moving the
/// mark to it, or at the end of the primary queue when no order carries it.
///
/// A cycle ends the walk with the order linked after the repeated one; an
/// order the match does not own ends it with the order linked in its place.
/// At most overlay_order_budget orders are walked.
///
/// @param[in,out] unit Unit whose primary queue receives the order.
/// @param[in,out] record Order to queue; it takes the queue-tail mark.
/// @param extra Maps an order to its match-side fields (command flags);
///     throws for an order the match does not own.
template <typename Extra>
inline void insert_after_queue_tail(
    sim::simulation_state::Unit& unit, sim::simulation_state::Order& record, Extra&& extra
) {
    extra(&record).command_flags |= command_queue_tail;
    record.next = nullptr;
    auto** link = &unit.primary;
    sim::simulation_state::Order* seen[overlay_order_budget]{};
    std::size_t n = 0;
    while (*link && n < overlay_order_budget) {
        auto* current = *link;
        if (current == &record)
            break;
        for (std::size_t i = 0; i < n; ++i) {
            if (seen[i] == current) {
                current->next = &record;
                record.next = nullptr;
                return;
            }
        }
        seen[n++] = current;
        uint8_t flags = 0;
        try {
            flags = extra(current).command_flags;
        } catch (const std::exception&) {
            // current is not in orders_ — do not read current->next (use-after-free).
            record.next = nullptr;
            *link = &record;
            return;
        }
        if (flags & command_queue_tail) {
            extra(current).command_flags &= static_cast<uint8_t>(~command_queue_tail);
            auto* next = current->next;
            if (next == &record || next == current)
                next = nullptr;
            else {
                for (std::size_t i = 0; i < n; ++i)
                    if (seen[i] == next) {
                        next = nullptr;
                        break;
                    }
            }
            record.next = next;
            current->next = &record;
            return;
        }
        link = &current->next;
    }
    record.next = nullptr;
    *link = &record;
}

template <typename Fn>
inline void for_each_primary_uncycled(sim::simulation_state::Order* head, Fn&& fn) {
    sim::simulation_state::Order* seen[overlay_order_budget]{};
    std::size_t n = 0;
    for (auto* order = head; order && n < overlay_order_budget; order = order->next) {
        for (std::size_t i = 0; i < n; ++i)
            if (seen[i] == order)
                return;
        seen[n++] = order;
        fn(order);
    }
}

inline int16_t signed_half(uint32_t n) {
    return std::bit_cast<int16_t>(static_cast<uint16_t>(n));
}

inline int16_t high_word(uint32_t n) {
    return std::bit_cast<int16_t>(static_cast<uint16_t>(n >> 16));
}

inline int32_t signed_word(uint32_t n) {
    return std::bit_cast<int32_t>(n);
}

// 16.16 arithmetic with 32-bit wraparound, as 3.1c does it.
inline int32_t wrapping_add(int32_t a, int32_t b) {
    return std::bit_cast<int32_t>(std::bit_cast<uint32_t>(a) + std::bit_cast<uint32_t>(b));
}

inline int32_t wrapping_sub(int32_t a, int32_t b) {
    return std::bit_cast<int32_t>(std::bit_cast<uint32_t>(a) - std::bit_cast<uint32_t>(b));
}

inline int16_t arithmetic_shift20(uint32_t value) {
    const auto high = value >> 20;
    const auto bits = (value & 0x80000000u) ? high | 0xfffff000u : high;
    return std::bit_cast<int16_t>(static_cast<uint16_t>(bits));
}

inline uint32_t square_high(int32_t value) {
    return static_cast<uint32_t>(static_cast<uint64_t>(static_cast<int64_t>(value) * value) >> 32);
}

inline sim::weapon_execution::Point as_point(const std::array<uint32_t, 3>& p) {
    return {{p[0], p[1], p[2]}};
}

inline std::array<uint32_t, 3> as_position(const sim::weapon_execution::Point& p) {
    return {p.fixed[0], p.fixed[1], p.fixed[2]};
}

inline constexpr std::array<const char*, 3> aim_script_names{
    "AimPrimary", "AimSecondary", "AimTertiary"
};
inline constexpr std::array<const char*, 3> fire_script_names{
    "FirePrimary", "FireSecondary", "FireTertiary"
};
inline constexpr uint32_t live_unit_flag = OA_UNIT_FLAG_LIVE;
inline constexpr uint32_t building_unit_flag = OA_UNIT_FLAG_BUILDING; // bm_code==0
inline constexpr uint32_t attached_without_piece = 0x20000u;          // carried with piece -1
inline constexpr uint32_t floater_type_flag = OA_UNIT_DEF_FLAG_FLOATER;
inline constexpr uint32_t death_pending_flag = OA_UNIT_FLAG_DEATH_PENDING;
// The selection bits the kill handler clears.
inline constexpr uint32_t death_clear_mask = OA_UNIT_FLAG_SELECTED | OA_UNIT_FLAG_SELECTABLE;
inline constexpr uint8_t damage_script_countdown = 0xf0; // Unit.damage_countdown at a hit
inline constexpr uint8_t healing_kind = 10;
inline constexpr uint8_t paralyze_kind = 2;
inline constexpr uint8_t paralyze_order_kind = 27; // Paralyze
inline constexpr uint8_t non_relayed_kind = 11;
// Self-destruct damage; at sim::unit_health::damage_scaling_limit, so armour never scales it.
inline constexpr int32_t self_destruct_damage = 30000;
// What kills a carried unit with its transport, a captured unit and a
// cancelled nanoframe, before the damage path's veteran scaling.
inline constexpr int32_t lethal_damage = 30000;
// Aircraft variants the command resolver picks for CAN_FLY units.
inline constexpr uint8_t vtol_follow_kind = 49;
inline constexpr uint8_t vtol_pickup_kind = 57;
inline constexpr uint8_t vtol_unload_kind = 65;
inline constexpr uint8_t weapon_hit_kind = 1;
// Scale of the HitByWeapon cosine and sine arguments.
inline constexpr int32_t hit_script_scale = 400;
inline constexpr uint32_t dummy_type_index = 0; // the reserved type of Game.unit_defs
// Unit.flags bits read by the economy tick.
inline constexpr uint32_t cloak_running_flag = OA_UNIT_FLAG_CLOAK_RUNNING; // cloak active state
inline constexpr uint32_t cloak_locked_flag = OA_UNIT_FLAG_CLOAK_LOCKED;   // no auto-deactivate
inline constexpr uint32_t moving_rate_mask = OA_UNIT_FLAG_MOVE_RATE_MASK;  // the move rate
inline constexpr uint32_t moving_rate_shift = 2;
inline constexpr uint8_t order_building_flag = 0x40;              // order flags; StartBuilding ran
inline constexpr uint8_t active_state_bit = OA_UNIT_STATE_ACTIVE; // Unit.state_flags
// Words of Unit.economy, an energy then a metal ResourceAccumulator: produced,
// requested, accepted, gate, last produced and last requested of each.
inline constexpr std::size_t produced_word = 0;
inline constexpr std::size_t requested_word = 1;
inline constexpr std::size_t accepted_word = 2;
inline constexpr std::size_t gate_word = 3;
inline constexpr std::size_t previous_produced_word = 4;
inline constexpr std::size_t previous_requested_word = 5;
inline constexpr std::size_t metal_block_word = 6; // the first word of the metal accumulator
inline constexpr std::size_t metal_requested_word = 7;
inline constexpr std::size_t metal_accepted_word = 8;
inline constexpr std::size_t metal_gate_word = 9;
inline constexpr std::size_t metal_previous_produced_word = 10;
inline constexpr std::size_t metal_previous_requested_word = 11;

inline bool name_equals_upper(std::string_view candidate, std::string_view want) {
    if (candidate.size() != want.size())
        return false;
    for (std::size_t i = 0; i < candidate.size(); ++i) {
        auto a = candidate[i];
        if (a >= 'a' && a <= 'z')
            a = static_cast<char>(a - 'a' + 'A');
        if (a != want[i])
            return false;
    }
    return true;
}

inline uint16_t find_loaded_type(const OfflineInputs& input, std::string_view want) {
    for (std::size_t i = 1; i < input.loaded.size(); ++i) {
        if (name_equals_upper(input.loaded[i].unit_name, want))
            return static_cast<uint16_t>(i);
    }
    return 0;
}

inline std::string_view factory_product(std::string_view name) {
    if (name_equals_upper(name, "ARMLAB"))
        return "ARMPW";
    if (name_equals_upper(name, "CORLAB"))
        return "CORAK";
    if (name_equals_upper(name, "ARMVP"))
        return "ARMFLASH";
    if (name_equals_upper(name, "CORVP"))
        return "CORGATOR";
    if (name_equals_upper(name, "ARMAP"))
        return "ARMFIG";
    if (name_equals_upper(name, "CORAP"))
        return "CORVENG";
    if (name_equals_upper(name, "ARMSY"))
        return "ARMPT";
    if (name_equals_upper(name, "CORSY"))
        return "CORPT";
    return {};
}

} // namespace oa::sim::match_runtime::tick_detail

namespace oa::sim::match_runtime {

using namespace tick_detail;

/// Returns the rate signal_move_rate keeps in the unit's
/// OA_UNIT_FLAG_MOVE_RATE_MASK bits.
///
/// @param movement The unit's movement object after the tick.
/// @param unit The moving unit.
/// @param def Its type.
/// @return 0 for a carried unit, a movement object whose flags hold the
///     rate-held bit (0x04), or no speed and no turn; otherwise 1, 2 past
///     moverate1 and 3 past moverate2 (signed compares).
inline uint32_t movement_rate(
    const sim::unit_movement::Movement& movement, const oa::Unit& unit, const oa::UnitDef& def
) noexcept {
    constexpr uint8_t rate_held = 0x04; // Movement.flags
    if ((movement.flags & rate_held) != 0 || unit.attach_parent != 0 ||
        (movement.speed == 0 && movement.turn == 0))
        return 0;
    if (!(def.move_rate1 < movement.speed))
        return 1;
    return def.move_rate2 < movement.speed ? 3 : 2;
}

class TickHost final : public sim::simulation_state::Host,
                       public sim::ground_orders::Host,
                       public sim::unit_movement::Host {
    Match& match;
    std::array<uint8_t, 3>* dispatched_flags{};
    sim::unit_spawn::Slot* dispatched_slot{};

    void before_callback();

    void after_callback();

    sim::unit_spawn::Slot& slot(sim::simulation_state::Unit& u);

    sim::simulation_state::Unit& unit_view(oa::Unit& record);

    sim::ground_orders::GroundRuntime& ground(sim::simulation_state::Unit& u);

    Match::RuntimeOrder& owned(sim::simulation_state::Order& order);

    std::array<uint8_t, 3> weapon_flags(sim::unit_spawn::Slot& s);

    void write_flags(sim::unit_spawn::Slot& s, const std::array<uint8_t, 3>& flags);

    // Brings every stood-down weapon slot back inside a mission handler that
    // holds a copy of the weapon flags: the copy is written first and
    // reloaded after, so the handler's final write keeps the cleared bits.
    void release_tracked_weapons(sim::unit_spawn::Slot& s, std::array<uint8_t, 3>& flags);

    sim::ground_orders::UnitView view(
        sim::unit_spawn::Slot& s,
        sim::ground_orders::GroundRuntime& g,
        std::array<uint8_t, 3>& flags
    );

    /// Leaves the ground for an order: weapons back on, off any carrier and
    /// active; a landed unit also climbs to half its cruise altitude over
    /// where it stands, and the order waits for the climb.
    ///
    /// @param s Unit taking off.
    /// @param[in,out] order Order it takes off for; its air goal and wait
    ///     events change when the unit was landed.
    void take_off(sim::unit_spawn::Slot& s, sim::simulation_state::Order& order);

    // Replaces the order's aircraft goal with a point goal (null clears it).
    // Circle goals carry no altitude; arrival_radius 0 means the unit's cell.
    void set_aircraft_goal(
        sim::unit_spawn::Slot& s,
        sim::simulation_state::Order& order,
        const sim::ground_orders::Point* point,
        int32_t arrival_radius
    );

    /// Replaces an order's construction target, keeping the target observer
    /// chain in step.
    ///
    /// @param[in,out] entry Order whose target changes.
    /// @param unit New target, or null for none.
    void retarget(Match::RuntimeOrder& entry, sim::simulation_state::Unit* unit);

    /// Hands a copy of `goal` to the unit's air driver as the order's goal,
    /// dropping the order's previous air goal first.
    ///
    /// @param s Flying unit.
    /// @param[in,out] order Order that owns the goal; its goal events are
    ///     cleared when a goal is installed.
    /// @param goal Goal to copy, or null to leave the driver without one.
    void install_air_goal(
        sim::unit_spawn::Slot& s, sim::simulation_state::Order& order, const sim::air::AirGoal* goal
    );

    void script(sim::unit_spawn::Slot& s, std::string_view name);

    /// UnitDef.abilities of the unit, zero when it has no type.
    uint32_t unit_abilities(const oa::Unit& unit) const;

    sim::unit_spawn::Slot& movement_slot(const sim::unit_movement::Unit& u);

    class AttackAdapter;

    class ConstructionAdapter;

    class PatrolAdapter;

    class HealthHost;

    // Mission handler families dispatched through the mission table. Each
    // returns true and stores the handler result when it owns order.kind.
    class GroundMissions;    // tick_missions_ground.cpp
    class VtolMissions;      // tick_missions_vtol.cpp
    class VtolBuildMissions; // tick_missions_vtol_build.cpp
    class AirAttackMissions; // tick_missions_air.cpp

    bool dispatch_ground_mission(
        sim::unit_spawn::Slot& s,
        sim::simulation_state::Order& order,
        uint32_t events,
        uint32_t& result
    );

    bool dispatch_vtol_mission(
        sim::unit_spawn::Slot& s,
        sim::simulation_state::Order& order,
        uint32_t events,
        uint32_t& result
    );

    bool dispatch_vtol_build_mission(
        sim::unit_spawn::Slot& s,
        sim::simulation_state::Order& order,
        uint32_t events,
        uint32_t& result
    );

    bool dispatch_air_attack_mission(
        sim::unit_spawn::Slot& s,
        sim::simulation_state::Order& order,
        uint32_t events,
        uint32_t& result
    );

  public:

    explicit TickHost(Match& m) : match(m) {}

    // The air module's services over this match.
    sim::air::AirHost air_host();

    // Hands the order's goal to the unit's movement object: its ground
    // navigator for a ground goal, its air driver for an air goal.
    void install_order_goal(sim::unit_spawn::Slot& s, Match::RuntimeOrder& record);

    /// Runs StopBuilding when the order started building, shares the start
    /// with the other players and clears the order's building bit
    /// (order_building_flag); nothing happens for an order that is not
    /// building.
    ///
    /// @param s Builder.
    /// @param[in,out] order Its order.
    void stop_building(sim::unit_spawn::Slot& s, sim::simulation_state::Order& order);

    /// Moves the unit's movement-rate bits (OA_UNIT_FLAG_MOVE_RATE_MASK) to its
    /// movement_rate and, on a change, runs StopMoving (rate 0), or
    /// StartMoving when the old rate was 0, then MoveRate1..3.
    ///
    /// @param s Moving unit.
    /// @param movement Its movement object after the tick.
    void signal_move_rate(sim::unit_spawn::Slot& s, const sim::unit_movement::Movement& movement);

    uint32_t random_bounded(uint32_t n) override;

    uint32_t random(uint32_t n) override { return random_bounded(n); }

    void tick_script(oa::Unit& record, uint32_t steps) override;

    /// Hands a wind generator the new wind when it changed this tick:
    /// SetDirection(direction) then SetSpeed(speed << 4).
    ///
    /// @param record Unit about to tick; only a wind generator is affected,
    ///     and one without a script throws.
    void update_wind_generator(oa::Unit& record) override;

    void tick_weapon_aim(oa::Unit& record) override;

    void wake_weapon(sim::simulation_state::Unit& u, uint32_t index) override;

    void clear_weapon_target(oa::Unit& record, uint32_t index) override {
        wake_weapon(unit_view(record), index);
    }

    void queue_default_mission(oa::World&, oa::Unit& record) override;

    bool nano_repair(sim::unit_spawn::Slot& repairer_slot, sim::simulation_state::Unit& patient);

    /// Runs the handler the mission table holds for the order's kind.
    ///
    /// @param world Canonical world (unused; the match's own is used).
    /// @param record Unit the order belongs to.
    /// @param[in,out] order Order to step; its phase and wait fields change.
    /// @param events Events that woke the order; tearing the order down
    ///     passes event 2.
    /// @return The handler's step result (see the ground:: result codes).
    uint32_t dispatch_mission(
        oa::World& world, oa::Unit& record, sim::simulation_state::Order& order, uint32_t events
    ) override;

    /// Tears an order down: its cleanup event when it waits for one,
    /// StopBuilding, the movement and air goals it owns, the stand-down of
    /// every weapon slot unless the order keeps them (flags bit 0), and its
    /// link in the target's observer chain; then frees it.
    ///
    /// @param record Unit the order belongs to.
    /// @param order Order to destroy; it is freed.
    void destroy_order(oa::Unit& record, sim::simulation_state::Order& order) override;

    int32_t terrain_height_under(oa::Unit& record) override;

    void settle_on_ground(oa::Unit& record) override;

    /// Runs one movement tick of a unit: its air driver, ground or flight
    /// steering, its position (following the carrier's piece when carried),
    /// then the move-rate and setSFXoccupy scripts.
    ///
    /// @param record Unit to move; one without a movement object is skipped.
    void movement_tick(oa::Unit& record) override;

    void cancel_search(sim::ground_orders::Navigation& navigation) override;

    void play_sound(sim::simulation_state::Unit& u, uint32_t category) override;

    sim::simulation_state::Unit* find_target(sim::simulation_state::Unit& unit) override;

    bool issue_attack(sim::simulation_state::Unit& from, sim::simulation_state::Unit& to) override;

    bool can_occupy(
        const sim::unit_movement::Unit& u, std::array<int16_t, 2> cell, uint8_t mode
    ) override;

    void remove_occupancy(sim::unit_movement::Unit& u) override;

    void insert_occupancy(sim::unit_movement::Unit& u) override;

    void update_spatial_membership(sim::unit_movement::Unit& u) override;

    /// Scales damage by the target's armour and veteran level and applies it,
    /// sharing it when the target is simulated elsewhere.
    ///
    /// @param source Attacker, or null for none.
    /// @param target Unit that takes the damage.
    /// @param amount Damage before scaling, in health points.
    /// @param kind Damage kind (1 weapon, 2 paralyze, 10 healing, or a
    ///     DeathKind for a kill).
    void scaled_damage(
        sim::unit_spawn::Slot* source, sim::unit_spawn::Slot& target, int32_t amount, uint32_t kind
    );

    void apply_scaled_damage(oa::Unit& record, int32_t amount, uint32_t kind) override;

    void regenerate_health(oa::Unit& record) override;

    /// Works out a live unit's Killed percentage and wreck level, drops a
    /// last attacker whose player's slot is free, shares the death through
    /// MultiplayerHooks::unit_killed when the unit is simulated here, clears
    /// its orders and hands them to the kill handler, then runs the
    /// commander rule's sweep when the dead commander was simulated here.
    ///
    /// @param record Dying unit; one no longer live is skipped.
    /// @param kind DeathKind value of the death, or 0 for a unit whose slot
    ///     another player's new unit takes.
    /// @quirk An unfinished unit leaves no wreck whatever Killed returns.
    void kill_unit(oa::Unit& record, uint8_t kind) override;

    /// Makes every live unit of the player not already dying self-destruct:
    /// one simulated here takes the self-destruct damage from itself, any
    /// other explodes and dies at once.
    ///
    /// @param owner Player index 0..9; nothing happens once the player has
    ///     no units.
    void destroy_player_units(uint8_t owner);

    void local_player_ticked(oa::Player&) override;

    bool is_key_down(uint32_t) override;

    void select_next_viewpoint_unit() override;

    void follow_next_selected(uint32_t) override;
};

class TickHost::AttackAdapter final : public AttackOrderHost {
    TickHost& host;
    sim::unit_spawn::Slot& source;
    Match::RuntimeOrder& record;

    void set_goal(std::unique_ptr<sim::ground_orders::Goal> goal);

  public:

    AttackAdapter(TickHost& h, sim::unit_spawn::Slot& s, Match::RuntimeOrder& r)
        : host(h), source(s), record(r) {}

    void announce() override;

    /// Returns the weapon slot an attack uses when the order names none.
    ///
    /// @return The first enabled slot, 0 or 1; otherwise the third slot's
    ///     enabled bit itself, 2 when set and 0 when no slot is enabled.
    uint8_t selected_weapon() override;

    void enable_weapon(uint32_t i) override;

    void assign_target(sim::simulation_state::Unit& target, int32_t index) override;

    void reset_weapons() override;

    bool can_reach(sim::simulation_state::Unit& target, uint8_t index) override;

    /// Returns the range of the unit's weapon in a slot (WeaponDef.range).
    ///
    /// @param index Weapon slot 0..2.
    /// @return Range in world units.
    int32_t range(uint8_t index) override;

    void clear_goal() override;

    void circle_goal(const AttackPoint& point, int32_t radius) override;

    uint32_t random(uint32_t limit) override;

    uint8_t morph_attack_command(sim::simulation_state::Unit* target) override;

    void assign_ground(const AttackPoint& point, int32_t slot) override;
};

class TickHost::ConstructionAdapter {
    TickHost& host;
    sim::unit_spawn::Slot& source;
    Match::RuntimeOrder& record;

    sim::unit_health::UnitType health_type(sim::unit_spawn::Slot& s) const;

  public:

    ConstructionAdapter(TickHost& h, sim::unit_spawn::Slot& s, Match::RuntimeOrder& r)
        : host(h), source(s), record(r) {}

    // Flags the build panel for a redraw when the viewpoint player has the
    // unit selected.
    void refresh_selected();

    // The site test for the order's type at its snapped destination.
    bool site_clear();

    /// Snaps a building's site to the footprint grid and sets its height to
    /// the one its yard takes there; other types are left alone.
    ///
    /// Changes the order's destination.
    void snap_build_height();

    // An unfinished unit of the order's type at its destination.
    sim::simulation_state::Unit* spawn_nanoframe();

    // A GetBuilt order at the head of the frame's queue, aimed at the builder.
    void issue_get_built(sim::simulation_state::Unit& nanoframe);

    /// Starts StartBuilding(heading) on the builder's script and marks the
    /// order as building (order_building_flag).
    ///
    /// A builder whose script cannot start it raises its own build stance;
    /// the call is not shared with the other players.
    ///
    /// @param heading Direction to the site in 65536ths of a turn.
    void start_building(int16_t heading);

    // Adds `rate` of worker time to an unfinished unit, or takes it away when
    // negative; true when the builder could pay for the step. No nano spray.
    bool build_progress(sim::simulation_state::Unit& nanoframe, float rate);

    // build_progress with the build nano spray when the step was paid for.
    bool construct(sim::simulation_state::Unit& nanoframe, float rate);

    // The builder link on `nanoframe` when both units are live: the
    // native completion, which marks the frame finished and releases it from
    // the pad. Its build-list test on the source holds for any factory; the
    // match leaves UnitDef.build_ids unset.
    void link_built(sim::simulation_state::Unit& nanoframe);

    // Negative rate at which an unattended frame of the source's type decays
    // over `ticks`.
    float build_decay_rate(int32_t ticks) const;

    int16_t footprint_x() const;

    int16_t footprint_z() const;

    // QueryBuildInfo: the pad piece's world position, or the unit's.
    bool query_build_pad(sim::ground_orders::Point& pad);

    // Carries the child on the factory pad (parent is the order owner). piece is
    // QueryBuildInfo; mode 1 is the BuildingBuild constant.
    void attach_child(sim::simulation_state::Unit& child, int8_t piece, uint8_t mode);

    // set_activation, with the build stance a scriptless unit never raises itself.
    void set_activation_mask(uint8_t mask, bool enabled);
};

class TickHost::PatrolAdapter {
    TickHost& host;
    sim::unit_spawn::Slot& source;
    Match::RuntimeOrder& record;

  public:

    PatrolAdapter(TickHost& h, sim::unit_spawn::Slot& s, Match::RuntimeOrder& r)
        : host(h), source(s), record(r) {}

    /// Queues a copy of the patrol at the unit's position unless an order in
    /// the queue already carries the clone mark (command flags bit 0x80),
    /// then marks this order.
    ///
    /// @quirk The copy takes its command flags from the kind's descriptor, so
    ///     it is left unmarked.
    void clone_patrol();
};

class TickHost::HealthHost final : public sim::unit_health::ConstructionHost {
    Match& match_;
    sim::unit_health::EconomyDebit debit_;
    sim::unit_health::EconomyDebit metal_;
    oa::UnitEconomy& economy_;

    sim::unit_spawn::Slot& slot(const sim::unit_health::Unit& u);

  public:

    HealthHost(Match& match, uint16_t index)
        : match_(match), economy_(match_unit(match, index).economy) {
        debit_ = {economy_.energy.requested, economy_.energy.accepted, economy_.energy.gate};
        metal_ = {economy_.metal.requested, economy_.metal.accepted, economy_.metal.gate};
    }

    void store();

    sim::unit_health::EconomyDebit& energy_debit(sim::unit_health::Unit&) override;

    sim::unit_health::EconomyDebit& metal_debit(sim::unit_health::Unit&) override;

    void refund_metal(sim::unit_health::Unit& target, float amount) override;

    void complete_construction(sim::unit_health::Unit&, sim::unit_health::Unit& target) override;

    bool target_is_live(const sim::unit_health::Unit& u) override;

    void apply_health_event(
        sim::unit_health::Unit& target,
        const sim::unit_health::Unit* source,
        const sim::unit_health::HealthEvent& event
    ) override;

    bool target_owner_present(const sim::unit_health::Unit& u) override;

    uint8_t target_owner_status(const sim::unit_health::Unit& u) override;

    sim::unit_health::RouteIdentity source_owner_route(const sim::unit_health::Unit&) override;

    sim::unit_health::RouteIdentity fallback_route() override;

    void share_health_event(
        sim::unit_health::RouteIdentity, const sim::unit_health::HealthEvent&
    ) override;
};

} // namespace oa::sim::match_runtime
