// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <array>
#include <optional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace oa::data::unit_definitions {

enum class ErrorCode { none, io, malformed, limit, missing_unitinfo, invalid_number };

struct Error {
    ErrorCode code = ErrorCode::none;
    std::size_t offset = 0;
    std::string message;
};

template <typename T>
struct Result {
    T value{};
    Error error{};

    /// Returns whether no error was reported.
    [[nodiscard]] explicit operator bool() const noexcept { return error.code == ErrorCode::none; }
};

struct TdfSection {
    std::string name;
    // Lower-case ASCII keys. Later assignments replace earlier ones, as in 3.1c.
    std::map<std::string, std::string, std::less<>> fields;
    std::vector<TdfSection> children;

    /// Finds the first child section with a name, ignoring ASCII case.
    ///
    /// @param name section name
    /// @return the child, or null
    [[nodiscard]] const TdfSection* child(std::string_view name) const;
    /// Looks up a field value, ignoring ASCII case.
    ///
    /// @param key field name
    /// @return the value text, or null when missing
    [[nodiscard]] const std::string* find(std::string_view key) const;
};

struct TdfDocument {
    std::vector<TdfSection> sections;
};

namespace limit {
inline constexpr std::size_t input_bytes = 16U * 1024U * 1024U;
inline constexpr std::size_t sections = 4096;
inline constexpr std::size_t nesting_depth = 32;
inline constexpr std::size_t fields_per_section = 4096;
inline constexpr std::size_t name_bytes = 1024;
inline constexpr std::size_t value_bytes = 1024U * 1024U;
} // namespace limit

/// Parses TDF text into sections with lower-cased keys.
///
/// @param source the whole text, at most limit::input_bytes
/// @return the top-level sections, or an error when the text is malformed or
///         exceeds a limit
[[nodiscard]] Result<TdfDocument> parse_tdf(std::string_view source);

// The values a UNITINFO section loads to, as 3.1c reads them. The *_fixed
// members are signed 16.16 results: the parsed floating value times 65536,
// truncated toward zero to 64 bits with the low 32 bits kept.
struct UnitDefinition {
    std::string source_name;
    std::string unit_name, object_name, display_name, description, designation, side, ted_class;
    std::string default_mission_type, movement_class, sound_category, corpse, yard_map;
    std::string weapon1, weapon2, weapon3, explode_as, self_destruct_as;
    std::string bad_target_category, wpri_bad_target_category;
    std::string wsec_bad_target_category, wspe_bad_target_category, no_chase_category;
    std::vector<std::string> categories;
    double version = 0.0;
    std::string copyright;

    int32_t build_cost_energy = 0, build_cost_metal = 0, build_time = 0;
    int32_t max_damage = 0;
    int32_t max_velocity_fixed = 0, brake_rate_fixed = 0, acceleration_fixed = 0;
    int32_t bank_scale_fixed = 65536, pitch_scale_fixed = 0;
    int32_t damage_modifier_fixed = 65536, move_rate1_fixed = 0, move_rate2_fixed = 0;
    int16_t turn_rate = 0, worker_time = 0, heal_time = 0;
    int16_t sight_distance = 0, radar_distance = 0, sonar_distance = 0;
    int16_t radar_distance_jam = 0, sonar_distance_jam = 0;
    int16_t min_cloak_distance = 0, build_angle = 0, build_distance = 0;
    int16_t sort_bias = 0, cruise_altitude = 0, maneuver_leash_length = 0;
    int16_t attack_run_length = 0, kamikaze_distance = 0;
    int16_t footprint_x = 0, footprint_z = 0;
    int16_t max_water_depth = 10000, min_water_depth = -10000;
    uint8_t max_slope = 255, bad_slope = 127;
    uint8_t max_water_slope = 255, bad_water_slope = 127;
    int8_t waterline = 0, transport_size = 0, transport_capacity = 0, bm_code = 0;
    int8_t makes_metal = 0;
    float energy_make = 0, energy_use = 0, metal_make = 0, extracts_metal = 0;
    float wind_generator = 0, tidal_generator = 0, energy_storage = 0, metal_storage = 0;
    float cloak_cost = 0, cloak_cost_moving = 0;
    uint8_t standing_move_order = 2, standing_fire_order = 2;
    uint8_t self_destruct_countdown = 5;

    bool init_cloaked = false, downloadable = false, builder = false, stealth = false;
    bool can_cloak = false; // derived by the loader from cloak_cost > 0
    bool z_buffer = false, is_airbase = false, targeting_upgrade = false, teleporter = false;
    bool hide_damage = false, shoot_me = false, armored_state = false, activate_when_built = false;
    bool can_fly = false, can_hover = false, upright = false, floater = false, amphibious = false;
    bool is_feature = false, no_shadow = false, immune_to_paralyzer = false, hover_attack = false;
    bool anti_weapons = false, digger = false, on_offable = false;
    bool mobile_stand_orders = false, fire_stand_orders = false, can_stop = false;
    bool can_attack = false, can_guard = false, can_patrol = false, can_move = false;
    bool can_load = false, can_reclamate = false, can_resurrect = false, can_capture = false;
    bool can_dgun = false, kamikaze = false, no_restrict = false, show_player_name = false;
    bool commander = false, cant_be_transported = false;

    // Unconsumed lower-case keys and their unmodified value text.
    std::map<std::string, std::string, std::less<>> unknown_fields;
};

// Bits of UnitDef.flags that loading an FBI sets.
enum class UnitFlag : uint32_t {
    init_cloaked = 1U << 4,
    downloadable = 1U << 5,
    builder = 1U << 6,
    z_buffer = 1U << 7,
    stealth = 1U << 8,
    is_airbase = 1U << 9,
    targeting_upgrade = 1U << 10,
    can_fly = 1U << 11,
    can_hover = 1U << 12,
    teleporter = 1U << 13,
    hide_damage = 1U << 14,
    shoot_me = 1U << 15,
    armored_state = 1U << 17,
    activate_when_built = 1U << 18,
    floater = 1U << 19,
    upright = 1U << 20,
    amphibious = 1U << 21,
    is_feature = 1U << 24,
    no_shadow = 1U << 25,
    immune_to_paralyzer = 1U << 26,
    hover_attack = 1U << 27,
    kamikaze = 1U << 28,
    anti_weapons = 1U << 29,
    digger = 1U << 30,
};

/// Returns the bit of a unit flag.
///
/// @param flag unit flag
/// @return its mask in UnitDef.flags
[[nodiscard]] constexpr uint32_t flag_mask(UnitFlag flag) noexcept {
    return static_cast<uint32_t>(flag);
}

/// Packs the UnitDef.flags word.
///
/// @param definition parsed FBI record
/// @return standing move orders in bits 0..1, fire orders in bits 2..3, and
///         each UnitFlag that is set
[[nodiscard]] uint32_t pack_unit_flags(const UnitDefinition& definition) noexcept;
/// Packs the UnitDef.abilities word.
///
/// Bit 13 is cloakcost > 0 and bits 20..22 are the self-destruct countdown.
///
/// @param definition parsed FBI record
/// @return the packed word
/// @quirk Canreclamate also sets bit 9, as in 3.1c.
[[nodiscard]] uint32_t pack_unit_abilities(const UnitDefinition& definition) noexcept;

// The UnitDef fields spawning reads, under the same names; unit_flags is
// UnitDef.flags.
struct SpawnDefinitionFields {
    uint32_t unit_flags = 0;
    int16_t footprint_x = 0; // after movement-class resolution
    int16_t footprint_z = 0; // after movement-class resolution
    int32_t max_damage = 0;
    int16_t heal_time = 0;
    int16_t build_angle = 0;
    int8_t makes_metal = 0; // as the FBI loads it
    int8_t bm_code = 0;
};

/// Projects the scalar fields spawning reads directly from a unit type.
///
/// Runtime-owned model, COB, player-limit and availability state is not
/// manufactured here.
///
/// @param definition parsed FBI record
/// @return the flag word and the spawn fields
[[nodiscard]] SpawnDefinitionFields project_for_spawn(const UnitDefinition& definition) noexcept;

struct MovementClassDefinition {
    std::string name;
    int16_t footprint_x = 0, footprint_z = 0;
    int16_t max_water_depth = 10000, min_water_depth = -10000;
    uint8_t max_slope = 255, bad_slope = 127;
    uint8_t max_water_slope = 255, bad_water_slope = 127;
};

struct MovementClassTable {
    // Slot n holds section CLASSn, and n is the movement_class_handle a unit
    // type naming that class resolves to. Missing sections remain empty and
    // are not compacted.
    std::array<std::optional<MovementClassDefinition>, 32> slots;
};

/// Reads MOVEINFO.TDF into the 32 movement-class slots.
///
/// A missing bad slope defaults to half its maximum; the maximum slope is
/// clamped to the maximum water slope and each bad slope to its maximum.
///
/// @param moveinfo_tdf the whole MOVEINFO.TDF text
/// @return the table, or a parse or number error
[[nodiscard]] Result<MovementClassTable> load_movement_classes(std::string_view moveinfo_tdf);

struct RuntimeDefinitionMetadata {
    std::optional<uint8_t> movement_class_handle;
    int16_t footprint_x = 0, footprint_z = 0;
    int16_t max_water_depth = 10000, min_water_depth = -10000;
    uint8_t max_slope = 255, bad_slope = 127;
    uint8_t max_water_slope = 255, bad_water_slope = 127;
    int32_t slope_speed_step_fixed = 0; // UnitDef.slope_speed_step
    std::vector<uint8_t> yard_cells;    // UnitDef.yard_map, row-major Z then X
    int16_t sight_distance = 0, radar_distance = 0, sonar_distance = 0;
    int16_t radar_distance_jam = 0, sonar_distance_jam = 0;
};

/// Resolves a unit type's movement class, footprint, slope limits, yard and sensors.
///
/// A named movement class supplies the footprint, depth and slope limits;
/// otherwise the FBI values are used. For a building (bmcode 0) the yard map
/// is compiled to one cell code per footprint cell, repeating the last
/// recognised character to fill a short map.
///
/// @param definition parsed FBI record
/// @param movement_classes loaded MOVEINFO table
/// @return the metadata, or an error for a negative footprint, an oversized
///         yard or a yard map with no recognised cell
[[nodiscard]] Result<RuntimeDefinitionMetadata> resolve_runtime_metadata(
    const UnitDefinition& definition, const MovementClassTable& movement_classes
);

/// Parses the [UNITINFO] section of an FBI into typed fields.
///
/// Defaults, narrowing and derived values match 3.1c;
/// unconsumed keys go to unknown_fields.
///
/// @param source the whole FBI text
/// @param source_name name recorded in the result
/// @return the definition, or a parse, number or missing_unitinfo error
[[nodiscard]] Result<UnitDefinition>
load_fbi(std::string_view source, std::string source_name = {});
/// Reads and parses an FBI from a host file (see load_fbi).
///
/// @param path host path of the file
/// @return the definition, or an io, limit or parse error
[[nodiscard]] Result<UnitDefinition> load_fbi_file(const std::filesystem::path& path);

// 3.1c merges loose files and ordered archives before it lists units/*.FBI.
// Implementations must therefore return one winning logical path per
// case-insensitive name from list_effective().
class CatalogAssetReader {
  public:

    virtual ~CatalogAssetReader() = default;
    /// Lists the effective resources of a directory.
    ///
    /// @param directory resource directory, e.g. "units"
    /// @param extension required suffix, e.g. ".FBI"
    /// @return one winning logical path per case-insensitive name
    [[nodiscard]] virtual Result<std::vector<std::string>>
    list_effective(std::string_view directory, std::string_view extension) const = 0;
    /// Reads a resource's text.
    ///
    /// @param logical_path path from list_effective
    /// @return the content, or an error
    [[nodiscard]] virtual Result<std::string> read(std::string_view logical_path) const = 0;
};

struct CatalogOptions {
    // A unit type's availability comes from build-version, copyright and
    // runtime mode checks before unit types are filtered. The host supplies
    // that verdict; omission keeps every successfully parsed FBI.
    std::function<bool(const UnitDefinition&)> compatible;
};

struct CatalogEntry {
    uint16_t type_id = 0; // one-based; slot zero is reserved
    std::string logical_path;
    UnitDefinition definition;
};

struct UnitCatalog {
    std::vector<CatalogEntry> entries;
};

/// Loads every effective units/*.FBI and numbers the unit types.
///
/// Each winning file is parsed and checked by the compatibility verdict; the
/// survivors are sorted by unitname ignoring ASCII case and given one-based
/// type ids.
///
/// @param assets resource view listing and reading the FBIs
/// @param options optional compatibility verdict
/// @return the catalog, or the first listing, read or parse error, or a limit
///         error when there are too many types for 16-bit ids
[[nodiscard]] Result<UnitCatalog>
load_unit_catalog(const CatalogAssetReader& assets, const CatalogOptions& options = {});

inline constexpr std::size_t category_mask_words = 16; // a 64-byte mask, as in 3.1c
inline constexpr std::size_t category_mask_bits = category_mask_words * 32;

struct UnitCategoryMask {
    std::array<uint32_t, category_mask_words> words{};
    /// Tests whether a unit type is in the category.
    ///
    /// @param type_id unit type id
    /// @return true when its bit is set; false past the mask
    [[nodiscard]] bool contains(uint16_t type_id) const noexcept;
};

struct UnitTargetCategoryMasks {
    UnitCategoryMask primary_bad;   // resolved from UnitDef.primary_bad_target_category
    UnitCategoryMask secondary_bad; // resolved from UnitDef.secondary_bad_target_category
    UnitCategoryMask special_bad;   // resolved from UnitDef.special_bad_target_category
    UnitCategoryMask no_chase;      // resolved from UnitDef.no_chase_category
};

struct ResolvedCategoryRegistry {
    // ASCII lower-case category names. Values are bitsets of sorted unit type
    // IDs, 64 bytes each, as 3.1c keeps its categories.
    std::map<std::string, UnitCategoryMask, std::less<>> categories;
    std::vector<UnitTargetCategoryMasks> target_masks; // indexed by type_id
};

/// Builds the category registry and each unit's target-category masks.
///
/// Every category a unit lists gets that unit's type bit. Each weapon's bad
/// target category and the no-chase category are then resolved; an unknown
/// name resolves to an empty mask.
///
/// @param catalog numbered unit catalog
/// @return the registry, or a limit error for a type id past the 512-bit mask
[[nodiscard]] Result<ResolvedCategoryRegistry> resolve_unit_categories(const UnitCatalog& catalog);

// One download-menu section, read before its download build ids are
// appended. menu_unit_index is the UNITMENU unit's array index, which is also
// written to UnitDef.type_id (one-based; slot zero stays reserved).
// unit_name is the entry's UNITNAME, cut to its bound.
struct DownloadMenuEntry {
    uint16_t menu_unit_index = 0;
    std::string unit_name;
};

// Appending refuses another id once UnitDef.build_id_count reaches this.
inline constexpr std::size_t download_build_id_limit = 31;

/// Appends download-menu build entries to the units' build lists.
///
/// For each unit, every menu entry naming it appends the type id of the
/// entry's UNITNAME (first catalog unitname, ASCII case-insensitive; unknown
/// names add nothing) while the list is below download_build_id_limit.
/// Existing ids (from CANBUILD) are kept.
///
/// @param catalog numbered unit catalog
/// @param[in,out] build_lists parallel to catalog.entries; a disengaged
///        element is a type without a build list (UnitDef.build_ids 0) and is
///        left untouched, since
///        this function does not allocate lists
/// @param menus download-menu sections
void append_download_build_ids(
    const UnitCatalog& catalog,
    std::vector<std::optional<std::vector<uint16_t>>>& build_lists,
    const std::vector<std::vector<DownloadMenuEntry>>& menus
);

} // namespace oa::data::unit_definitions
