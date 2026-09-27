// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "oa/sim/simulation_state.hpp"
#include "oa/sim/spatial_state/spatial.hpp"
#include <array>
#include <cstdint>
#include <span>

namespace oa::sim::unit_spawn {
using AssetHandle = uintptr_t;

// Runtime fields not all sourced from FBI: enabled, limits, model and COB are
// established when the game loads the type. Zero type index is reserved.
struct Type {
    sim::simulation_state::UnitType simulation;
    int16_t footprint_x{}, footprint_z{}; // UnitDef.footprint_x, footprint_z
    int32_t player_limit{-1};             // UnitDef.player_limit
    AssetHandle model{}, cob{};           // the loaded 3DO model and COB script
    uint16_t build_angle{};               // UnitDef.build_angle
    uint8_t gui_page_count{}, bm_code{};  // UnitDef.gui_page_count, bm_code
};

/// Copies the fields a runtime Type carries into the canonical record of the same index.
///
/// Handles to parsed assets stay native in Type.
///
/// @param type runtime type
/// @param[out] def canonical UnitDef of the same index
void load_unit_def(const Type& type, oa::UnitDef& def) noexcept;

// Handles of what a unit owns outside its record (script, model instance and
// movement object); indexed by unit slot.
struct SlotAssets {
    AssetHandle script_instance{}, model_instance{}, movement_object{};
    std::array<bool, 3> weapon_slots_initialized{};
};

// Player-setup fields recorded while starting a player, as PlayerSetupInfo.side
// and color hold them.
struct PlayerSetupState {
    uint8_t side{}, color{};
};

// Native tables that accompany oa::World during unit creation. `types` is
// indexed like World.unit_defs; `assets` by unit slot; `setups` by player.
struct Tables {
    std::span<Type> types;
    std::span<SlotAssets> assets;
    std::span<PlayerSetupState> setups;
};

/// Debits a player's energy store when it covers an amount.
///
/// @param[in,out] player player whose energy store (Player.energy) is debited
/// @param[in,out] requested the unit's requested word; grows by `amount` when paid
/// @param amount energy to pay
/// @return true when paid
/// @quirk An unordered (NaN) comparison fails, as in 3.1c.
[[nodiscard]] bool player_pay_energy(oa::Player& player, float& requested, float amount) noexcept;
/// Debits a player's metal store (Player.metal) when it covers an amount.
///
/// @param[in,out] player player whose metal store is debited
/// @param[in,out] requested the unit's requested word; grows by `amount` when paid
/// @param amount metal to pay
/// @return true when paid
[[nodiscard]] bool player_pay_metal(oa::Player& player, float& requested, float amount) noexcept;
/// Debits both stores when both cover their cost, else neither.
///
/// @param[in,out] player player whose stores are debited
/// @param[in,out] energy_requested the unit's requested energy word
/// @param[in,out] metal_requested the unit's requested metal word
/// @param energy energy to pay
/// @param metal metal to pay
/// @return true when both were paid
[[nodiscard]] bool player_pay_resources(
    oa::Player& player, float& energy_requested, float& metal_requested, float energy, float metal
) noexcept;

// Request.state of a unit placed on the ground: occupancy 1 in Unit.flags.
inline constexpr uint32_t ground_occupancy_state = 1;

struct Request {
    uint8_t player{};
    uint16_t type{};
    std::array<uint32_t, 3> position{}; // x/y/z signed16.16 bit patterns
    bool finished{};
    uint32_t state{};          // Unit.flags occupancy bits, written after initialization
    uint16_t requested_slot{}; // zero chooses first free; nonzero is exact global index
};

// Work unit creation hands to other systems, in the order create calls it.
struct Host {
    virtual ~Host() = default;
    /// Draws from the synced random stream.
    ///
    /// @param exclusive_limit upper bound of the draw
    /// @return a value in [0, exclusive_limit)
    virtual uint32_t random_bounded(uint32_t exclusive_limit) = 0;
    /// Initializes one weapon slot's target record.
    ///
    /// @param unit new unit
    /// @param slot weapon slot 0..2
    virtual void init_weapon_target(oa::Unit& unit, uint32_t slot) = 0;
    /// Resets one weapon slot's targets.
    ///
    /// @param unit new unit
    /// @param slot weapon slot 0..2
    virtual void reset_weapon_targets(oa::Unit& unit, uint8_t slot) = 0;
    /// Initializes the unit's economy block for its owner.
    ///
    /// @param unit new unit
    /// @param owner owner index (Unit.owner_index)
    virtual void init_unit_economy(oa::Unit& unit, uint8_t owner) = 0;
    /// Assigns the unit's squad.
    ///
    /// @param unit new unit
    /// @param squad always 0 from unit creation
    virtual void assign_squad(oa::Unit& unit, uint32_t squad) = 0;
    /// Creates a plain model instance for a type without a script.
    ///
    /// @param model the type's model handle
    /// @return the instance, or 0 when allocation fails
    virtual AssetHandle create_model_instance(AssetHandle model) = 0;
    /// Makes the unit the owner of its new plain model instance.
    ///
    /// @param unit new unit
    virtual void model_owner(oa::Unit& unit) = 0;
    /// Allocates a script VM (a CobMachine).
    ///
    /// @return the VM, or 0 when allocation fails
    virtual AssetHandle allocate_script() = 0;
    /// Loads a COB program into a script VM.
    ///
    /// @param instance script VM
    /// @param cob the type's COB handle
    virtual void load_script_state(AssetHandle instance, AssetHandle cob) = 0;
    /// Creates a model instance driven by a script.
    ///
    /// @param model the type's model handle
    /// @param cob the type's COB handle
    /// @param unit new unit
    /// @return the instance, or 0 when allocation fails
    virtual AssetHandle
    create_scripted_model(AssetHandle model, AssetHandle cob, oa::Unit& unit) = 0;
    /// Binds a script VM to its model instance.
    ///
    /// @param script script VM
    /// @param model model instance
    virtual void bind_script_model(AssetHandle script, AssetHandle model) = 0;
    /// Starts the script's Create function ("Create", 0 arguments, 1).
    ///
    /// @param script script VM
    virtual void call_script_create(AssetHandle script) = 0;
    /// Clears the model instance's cached image word (SlotRuntime::cached_image_word).
    ///
    /// @param unit new unit
    virtual void model_reset(oa::Unit& unit) = 0;
    /// Arms the unit's three weapon slots.
    ///
    /// @param unit new unit
    virtual void initialize_weapons(oa::Unit& unit) = 0;
    /// Initializes the unit's resource extraction rate from the terrain.
    ///
    /// @param unit new unit
    virtual void initialize_extraction_rate(oa::Unit& unit) = 0;
    /// Creates the movement object of a bmcode 1 type.
    ///
    /// @param unit new unit
    /// @return the movement object; 0 is allowed and leaves Unit.movement clear
    virtual AssetHandle create_movement(oa::Unit& unit) = 0;
    /// Fits the new unit's height to the ground.
    ///
    /// @param unit new unit
    virtual void fit_spawn_height(oa::Unit& unit) = 0;
    /// Registers the unit's footprint in the occupancy map.
    ///
    /// @param unit new unit
    virtual void register_occupancy(oa::Unit& unit) = 0;
    /// Notifies that a unit was created.
    ///
    /// @param unit new unit
    virtual void notify_created(oa::Unit& unit) = 0;
    /// Notifies that a finished structure (bmcode 0) was created.
    ///
    /// @param unit new unit
    virtual void notify_finished(oa::Unit& unit) = 0;
    /// Activates a finished unit whose type activates when built (sim::unit_activation::change).
    ///
    /// @param unit new unit
    /// @param mask true changes the activation bit (OA_UNIT_STATE_ACTIVE); always true
    ///        from unit creation
    /// @param enabled true sets the bit, false clears it; always true from unit creation
    virtual void set_activation(oa::Unit& unit, bool mask, bool enabled) = 0;
    /// Updates the unit's sight and radar coverage.
    ///
    /// @param unit new unit
    virtual void update_sight(oa::Unit& unit) = 0;
    /// Notifies the campaign scenario that a unit was created.
    ///
    /// @param unit new unit
    virtual void notify_scenario_created(oa::Unit& unit) = 0;
};

/// Returns the unit pool length for a per-player limit: ten equal ranges plus reserved slot 0.
///
/// Throws std::invalid_argument for a zero limit or one that would pass the 16-bit count.
///
/// @param per_player_limit units each player may own
/// @return `per_player_limit * 10 + 1`
std::size_t offline_pool_size(uint16_t per_player_limit);
/// Lays out the unit pool for a single-player game.
///
/// Reserves slot 0, gives each of the ten players an equal owner range, and sets
/// indices and type-zero linkage. The caller owns the unit and type tables
/// (World.units/unit_defs). Multiplayer player ordering depends on a separate key and
/// is not inferred here.
///
/// Throws std::invalid_argument when the tables do not match the pool size or type zero
/// is missing.
///
/// @param[in,out] world units and players are initialized
/// @param per_player_limit units each player may own
void init_unit_pool(oa::World& world, uint16_t per_player_limit);
/// Chooses a free slot in the player's range and constructs a unit there.
///
/// Runs numeric initialization, model and script attachment, weapons, extraction rate,
/// the movement object (bmcode 1), placement and the creation notifications, then counts
/// the unit for its owner.
///
/// Throws std::out_of_range or std::invalid_argument for malformed tables.
///
/// @param[in,out] world world the unit joins
/// @param[in,out] tables runtime types, slot assets and player setups
/// @param request owner, type, position, finished state and slot choice
/// @param host work owned by other systems
/// @return the unit, or null for the game's ordinary failures: a disabled or zero type,
///         an exhausted limit, an occupied exact slot, or no free slot
oa::Unit* create(oa::World& world, Tables& tables, const Request& request, Host& host);
/// Initializes the numeric fields of a freshly claimed unit record.
///
/// Sets the type, flags, health and build state, position and cell, a random heading
/// from the type's build angle, the weapon target records, the economy block, a random
/// bob phase and the squad.
///
/// Throws for a slot without an owner, a type out of range, or a callback that removes
/// the type.
///
/// @param[in,out] world world the unit lives in
/// @param[in,out] unit claimed unit record; its owner must be set
/// @param request type, position and finished state
/// @param host random stream and weapon, economy and squad setup
/// @quirk The heading's half-angle is read from the type the unit holds after the
///        random draw, and the type is reloaded after the weapon callbacks.
void initialize_numeric(oa::World& world, oa::Unit& unit, const Request& request, Host& host);
/// Attaches the model and COB script of a freshly claimed unit.
///
/// A scripted type allocates a VM, loads the COB, creates the scripted model instance,
/// binds them and starts Create; a type without a script gets a plain instance linked
/// to the unit. Last, Host::model_reset clears the model instance word nothing reads.
///
/// Throws std::runtime_error when an allocation returns 0.
///
/// @param world world the unit lives in
/// @param[in,out] tables slot assets receive the handles
/// @param[in,out] unit claimed unit record
/// @param host model and script services
void attach_model_script(oa::World& world, Tables& tables, oa::Unit& unit, Host& host);

struct StartMarker {
    int32_t kind{}, index{};
    int16_t x{}, z{}; // 12-byte map record, integer world coordinates
};

/// Counts the kind-1 (start position) markers: the players a map can seat.
///
/// @param markers map markers
/// @return the number of start positions
[[nodiscard]] int32_t count_start_positions(std::span<const StartMarker> markers) noexcept;

/// Finds the first kind-1 marker with an index.
///
/// @param markers map markers
/// @param index start position index
/// @param[out] output the marker's integer x/z converted to 16.16, y 0
/// @return false when no marker matches
bool start_position(
    std::span<const StartMarker> markers, int32_t index, std::array<uint32_t, 3>& output
);

struct PlayerSetup {
    uint8_t side{}, color{}; // the player's side and logo colour
    int32_t metal{}, energy{};
};

struct StartHost {
    virtual ~StartHost() = default;
    /// Returns the commander unit type of a side.
    ///
    /// @param side side index from the player setup
    /// @return unit type index
    virtual uint16_t commander_type_for_side(uint8_t side) = 0;
    /// Reports a start position the map does not have.
    ///
    /// @param index missing start position index
    virtual void report_missing_start_position(int32_t index) = 0;
    /// Moves the local camera.
    ///
    /// @param x camera left edge, whole world units
    /// @param z camera top edge, whole world units
    /// @param flags always 0 from commander placement
    virtual void set_camera_position(int32_t x, int32_t z, uint32_t flags) = 0;
};

struct StartResult {
    bool position_found{};
    oa::Unit* unit{};
};

/// Grants a player's start storage.
///
/// Sets Player.resource_flags bit 0 and floors the energy and metal storage
/// (Player.shared_energy_storage and shared_metal_storage) at 200.
///
/// @param[in,out] player player record
/// @param metal metal storage from the player setup
/// @param energy energy storage from the player setup
void grant_start_storage(oa::Player& player, int32_t metal, int32_t energy) noexcept;

/// Places a player's commander at its start position.
///
/// Records the chosen side, grants the setup's resources as start storage, spawns the
/// side's commander at the start marker, finished and on the ground, and centres the
/// local camera on it. The stores themselves are seeded afterwards.
///
/// When the start marker is missing the report is made and no commander is
/// spawned; 3.1c can carry on with the player's start after the report.
///
/// Throws std::out_of_range for a player past the ten players or the setup table.
///
/// @param[in,out] world world the commander joins
/// @param[in,out] tables runtime types, slot assets and player setups
/// @param player player index
/// @param setup the player's side and start resources
/// @param markers map markers
/// @param start_index start position of the player
/// @param local_player index of the player at this machine's screen
/// @param viewport_width game view width, pixels
/// @param viewport_height game view height, pixels
/// @param host unit creation services
/// @param start commander type, missing-start report and camera services
/// @return whether the start position was found, and the commander (null when creation
///         failed)
StartResult spawn_player_commander(
    oa::World& world,
    Tables& tables,
    uint8_t player,
    const PlayerSetup& setup,
    std::span<const StartMarker> markers,
    int32_t start_index,
    uint8_t local_player,
    int32_t viewport_width,
    int32_t viewport_height,
    Host& host,
    StartHost& start
);

/// Counts the units attached to a unit.
///
/// Walks the Unit.attach_first_child and attach_next chain and counts the children whose
/// attach_parent is this unit.
///
/// Throws std::invalid_argument for a chain longer than the 16-bit unit index space.
///
/// @param world world the units live in
/// @param unit carrier
/// @return the number of attached children
int32_t attached_child_count(const oa::World& world, const oa::Unit& unit);

/// Returns the mean of the low and high plot heights under a position.
///
/// The signed high words of X and Z are divided by 16 toward zero to select the plot;
/// Y is unread.
///
/// @param position 16.16 x/y/z bit patterns
/// @param plots the map's plot table
/// @param map_width map width in plots
/// @param map_height map height in plots
/// @return the mean height, or -1 when the cell has no plot
int32_t average_plot_height(
    std::span<const uint32_t, 3> position,
    std::span<const sim::spatial_state::Plot> plots,
    int32_t map_width,
    int32_t map_height
);
} // namespace oa::sim::unit_spawn

#include "oa/sim/unit_spawn/legacy_views.hpp"
